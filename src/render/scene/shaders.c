/* Port of src/render/scene/shaders.ts. GPL-2.0-or-later. */
#include "qa/render/scene/shaders.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/materials/material.h"
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static char *xdup(const char *s) { size_t n = strlen(s) + 1; char *p = xalloc(n); memcpy(p, s, n); return p; }
static char *key_of(const char *name) { char *s = qa_strip_shader_extension(name); char *n = qa_normalize_shader_name(s); free(s); return n; }
typedef struct program_entry { char *key; qa_shader_registration_program *program; int cinematic; } program_entry;
typedef struct compiled_entry { char *key; qa_registered_scene_material material; int defaulted; } compiled_entry;
typedef struct binding_entry { qa_shader_registration *registration; qa_registered_scene_material material; qa_scene_shader_binding binding; } binding_entry;
typedef struct gen_pic { char *key; qa_render_image image; } gen_pic;
typedef struct gen_tex { char *key; const qa_scene_texture *texture; } gen_tex;
typedef struct img_req { char *key; char *name; qa_scene_shader_binding binding; } img_req;
typedef struct src_record {
  qa_shader_logical_key logical; char *logical_name;
  double order; char *name; int kind; /* 0 default 1 stencil-shadow 2 ordinary */
  qa_scene_shader_binding binding; qa_registered_scene_material material; int defaulted;
} src_record;
typedef struct rimpl {
  program_entry *programs; size_t nprog, cprog;
  compiled_entry *compiled; size_t ncomp, ccomp;
  char **defaulted; size_t ndef, cdef;
  binding_entry *bindings; size_t nbind, cbind;
  qa_scene_shader_binding *bind_scratch; qa_registered_scene_material *mat_scratch; qa_scene_material_binding *mb_scratch;
  struct { qa_shader_registration *reg; double order; } *pic_orders; size_t npic, cpic;
  gen_pic *gen_pics; size_t ngp, cgp;
  gen_tex *gen_texs; size_t ngt, cgt;
  img_req *img_reqs; size_t nreq, creq;
  int has_source; qa_source_scene_materials source_materials;
  int source_initialized;
  qa_registered_scene_material source_default; int has_source_default;
  double source_request;
  src_record *records; size_t nrec, crec;
  src_record *pending_worlds; size_t npend, cpend;
  int has_replacement_count; double replacement_count;
  qa_scene_play_cinematic_fn play; void *play_ctx;
  char *family;
  qa_shader_replacement_stage *stage; int stage_owned;
  /* owned compiled materials + backing storage */
  qa_compiled_material **owned_compiled; size_t nowned, cowned;
  qa_shader_script_inspection *inspections; size_t ninsp, cinsp;
  qa_finished_shader_result *finishes; size_t nfin, cfin;
  qa_finish_shader_input_result *implicit_inputs; size_t nimp, cimp;
  qa_shader_registration_result *reg_results; size_t nreg, creg;
  void **scratch; size_t nscratch, cscratch;
  qa_scene_shader_registry *self;
} rimpl;
static rimpl *ri(qa_scene_shader_registry *r) { return (rimpl *)r->_impl; }
static void scratch_add(rimpl *o, void *p) { if (o->nscratch == o->cscratch) { o->cscratch = o->cscratch ? o->cscratch * 2 : 16; o->scratch = xg(o->scratch, o->cscratch * sizeof(void *)); } o->scratch[o->nscratch++] = p; }
/* finish service adapter over material_finish (results owned by registry) */
static qa_mat_finished_shader svc_finish(void *ctx, qa_mat_finish_input input) {
  rimpl *o = ctx;
  if (o->nfin == o->cfin) { o->cfin = o->cfin ? o->cfin * 2 : 8; o->finishes = xg(o->finishes, o->cfin * sizeof *o->finishes); }
  qa_finished_shader_result *res = &o->finishes[o->nfin++];
  memset(res, 0, sizeof *res);
  qa_finish_shader(input, res);
  return res->shader;
}
static qa_mat_finish_input svc_implicit(void *ctx, qa_mat_finish_implicit_input input) {
  rimpl *o = ctx;
  if (o->nimp == o->cimp) { o->cimp = o->cimp ? o->cimp * 2 : 8; o->implicit_inputs = xg(o->implicit_inputs, o->cimp * sizeof *o->implicit_inputs); }
  qa_finish_shader_input_result *res = &o->implicit_inputs[o->nimp++];
  memset(res, 0, sizeof *res);
  qa_implicit_shader_input(input, res);
  return res->input;
}
static qa_mat_finish_service finish_service(rimpl *o) { return (qa_mat_finish_service){ o, svc_finish, svc_implicit }; }
static const qa_compiled_material *own_compiled(rimpl *o, qa_compiled_material value) {
  qa_compiled_material *box = xalloc(sizeof *box); *box = value;
  if (o->nowned == o->cowned) { o->cowned = o->cowned ? o->cowned * 2 : 8; o->owned_compiled = xg(o->owned_compiled, o->cowned * sizeof(void *)); }
  o->owned_compiled[o->nowned++] = box;
  return box;
}
static qa_mat_registered_image registered_image(qa_renderer_image image, double tmu) {
  qa_mat_registered_image r; memset(&r, 0, sizeof r); r.frame.image = image; r.tmu = tmu; return r;
}
static qa_mat_registered_stage loaded_stage(qa_renderer_image image, double tmu) {
  qa_mat_registered_stage s; memset(&s, 0, sizeof s);
  s.kind = "loaded"; s.tmu = tmu; s.binding.kind = "images";
  s.binding.playback.kind = "single"; s.binding.playback.image.image = image;
  return s;
}
typedef struct effects { qa_scene_shader_registry *registry; int deferred; void (**fns)(void *); void **ctxs; size_t n, cap; } effects;
static void effect_push(effects *e, void (*fn)(void *), void *ctx) {
  if (!e->deferred) { fn(ctx); free(ctx); return; }
  if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 4; e->fns = xg(e->fns, e->cap * sizeof(void *)); e->ctxs = xg(e->ctxs, e->cap * sizeof(void *)); }
  e->fns[e->n] = fn; e->ctxs[e->n] = ctx; e->n++;
}
typedef struct sun_ctx { qa_scene_shader_registry *r; qa_mat_registered_sun value; } sun_ctx;
static void apply_sun(void *v) { sun_ctx *c = v; c->r->sun_value = c->value; c->r->has_sun = true; }
typedef struct sky_ctx { qa_scene_shader_registry *r; double height; } sky_ctx;
static void apply_sky(void *v) { sky_ctx *c = v; qa_sky_builder_initialize_cloud_coordinates(&c->r->sky, c->height); }
typedef struct warn_ctx { qa_scene_shader_registry *r; char *message; } warn_ctx;
static void apply_warning(void *v) {
  warn_ctx *c = v; qa_scene_shader_registry *r = c->r;
  if (r->warning_count == r->warning_capacity) { r->warning_capacity = r->warning_capacity ? r->warning_capacity * 2 : 8; r->warnings = xg(r->warnings, r->warning_capacity * sizeof(char *)); }
  r->warnings[r->warning_count++] = c->message;
}
typedef struct host_ctx {
  qa_scene_shader_registry *r; effects *effects;
  const qa_scene_texture *base_texture; double lightmap_index;
} host_ctx;
static bool host_find_image(void *vctx, qa_mat_source_image_request request, qa_mat_registered_image *out) {
  host_ctx *hc = vctx; qa_scene_shader_registry *r = hc->r; rimpl *o = ri(r);
  if (hc->base_texture) {
    char *rk = key_of(request.name), *bk = key_of(hc->base_texture->name);
    char *with_tex = xalloc(strlen(hc->base_texture->name) + 10);
    sprintf(with_tex, "textures/%s", hc->base_texture->name);
    char *tk = key_of(with_tex); free(with_tex);
    int match = !strcmp(rk, bk) || !strcmp(rk, tk);
    free(rk); free(bk); free(tk);
    if (match) {
      qa_scene_surface_options so = { request.mipmap, request.wrap };
      const qa_scene_texture *sampled = qa_scene_texture_loader_sample_surface(r->textures, hc->base_texture, so);
      *out = registered_image(sampled->image, 0); return true;
    }
  }
  qa_scene_texture_load_options lo; memset(&lo, 0, sizeof lo);
  lo.has_mipmap = true; lo.mipmap = request.mipmap; lo.wrap = request.wrap; lo.family = o->family;
  lo.usage = hc->lightmap_index == -4 ? "picture" : "wall";
  const qa_scene_texture *texture = qa_scene_texture_loader_load(r->textures, request.name, &lo);
  if (!texture) return false;
  *out = registered_image(texture->image, 0); return true;
}
static bool host_play(void *vctx, const char *name, qa_mat_registered_video *out) {
  host_ctx *hc = vctx; rimpl *o = ri(hc->r);
  return o->play && o->play(o->play_ctx, name, out);
}
static void host_sun(void *vctx, qa_mat_registered_sun value) {
  host_ctx *hc = vctx; sun_ctx *c = xalloc(sizeof *c); c->r = hc->r; c->value = value; effect_push(hc->effects, apply_sun, c);
}
static void host_sky(void *vctx, double height) {
  host_ctx *hc = vctx;
  if (hc->effects->deferred && !isfinite((float)height)) qa_range_error("Sky cloud height must be finite float32");
  sky_ctx *c = xalloc(sizeof *c); c->r = hc->r; c->height = height; effect_push(hc->effects, apply_sky, c);
}
static void host_warning(void *vctx, const char *message) {
  host_ctx *hc = vctx; warn_ctx *c = xalloc(sizeof *c); c->r = hc->r; c->message = xdup(message); effect_push(hc->effects, apply_warning, c);
}
typedef struct prepared_material { const qa_compiled_material *compiled; int defaulted; } prepared_material;
static prepared_material compile_shader(qa_scene_shader_registry *r, const char *name, const qa_renderer_image *lightmap, double lightmap_index, const qa_scene_texture *base_texture, bool mipmap, effects *fx) {
  rimpl *o = ri(r);
  char *name_key = key_of(name);
  for (size_t i = 0; i < o->ngp; i++) if (!strcmp(o->gen_pics[i].key, name_key)) {
    const qa_scene_texture *texture = NULL;
    for (size_t k = 0; k < o->ngt; k++) if (!strcmp(o->gen_texs[k].key, name_key)) texture = o->gen_texs[k].texture;
    if (!texture) {
      qa_texture_sampling samp = { "clamp", "linear" };
      texture = qa_scene_texture_loader_register(r->textures, name, o->gen_pics[i].image, &samp, NULL, false, 0, 0);
      if (o->ngt == o->cgt) { o->cgt = o->cgt ? o->cgt * 2 : 4; o->gen_texs = xg(o->gen_texs, o->cgt * sizeof *o->gen_texs); }
      o->gen_texs[o->ngt++] = (gen_tex){ xdup(name_key), texture };
    }
    qa_mat_finish_implicit_input input; memset(&input, 0, sizeof input);
    input.kind = "picture"; input.name = name; input.profile = r->profile;
    input.base_image = loaded_stage(texture->image, 0);
    qa_compiled_material compiled = qa_compile_implicit_material(input, finish_service(o));
    free(name_key);
    return (prepared_material){ own_compiled(o, compiled), 0 };
  }
  program_entry *entry = NULL;
  for (size_t i = 0; i < o->nprog; i++) if (!strcmp(o->programs[i].key, name_key)) entry = &o->programs[i];
  if (entry) {
    host_ctx hc = { r, fx, base_texture, lightmap_index };
    qa_mat_registration_host host;
    memset(&host, 0, sizeof host);
    host.white_image = registered_image(r->textures->white->image, 0);
    host.default_image = registered_image(r->textures->missing->image, 0);
    host.lightmap_image = registered_image(lightmap ? *lightmap : r->textures->white->image, 1);
    host.ctx = &hc; host.find_image = host_find_image; host.play_shader_cinematic = host_play;
    host.apply_sun = host_sun; host.initialize_sky_tex_coords = host_sky; host.print_warning = host_warning;
    if (o->nreg == o->creg) { o->creg = o->creg ? o->creg * 2 : 8; o->reg_results = xg(o->reg_results, o->creg * sizeof *o->reg_results); }
    qa_shader_registration_result *result = &o->reg_results[o->nreg++];
    memset(result, 0, sizeof *result);
    qa_shader_program_register(entry->program, &host, result);
    qa_mat_finish_input finput; memset(&finput, 0, sizeof finput);
    finput.definition = result->shader.definition; finput.images = result->shader.stages; finput.image_count = result->shader.stage_count;
    finput.lightmap_index = lightmap_index; finput.profile = r->profile;
    qa_mat_finished_shader finished = svc_finish(o, finput);
    qa_render_material material = qa_shader_render_material(result->shader.definition);
    qa_compiled_material compiled; compiled.registered = result->shader; compiled.finished = finished; compiled.material = material;
    int defaulted = result->shader.kind && !strcmp(result->shader.kind, "defaulted");
    free(name_key);
    return (prepared_material){ own_compiled(o, compiled), defaulted };
  }
  qa_scene_texture_load_options lo; memset(&lo, 0, sizeof lo);
  lo.has_mipmap = true; lo.mipmap = mipmap; lo.wrap = mipmap ? "repeat" : "clamp"; lo.family = o->family;
  lo.usage = lightmap_index == -4 ? "picture" : "wall";
  const qa_scene_texture *loaded = qa_scene_texture_loader_load(r->textures, name, &lo);
  const qa_scene_texture *texture = loaded ? loaded : r->textures->missing;
  qa_mat_registered_stage base = loaded_stage(texture->image, 0);
  if (!loaded) {
    char *msg = xalloc(strlen(name) + 64);
    sprintf(msg, "%s: missing shader image, using the source default material", name);
    warn_ctx *c = xalloc(sizeof *c); c->r = r; c->message = msg; effect_push(fx, apply_warning, c);
    qa_mat_finish_implicit_input input; memset(&input, 0, sizeof input);
    input.kind = "default"; input.name = name; input.base_image = base; input.profile = r->profile;
    qa_compiled_material compiled = qa_compile_implicit_material(input, finish_service(o));
    free(name_key);
    return (prepared_material){ own_compiled(o, compiled), 1 };
  }
  qa_mat_finish_implicit_input input; memset(&input, 0, sizeof input);
  input.name = name; input.base_image = base; input.profile = r->profile;
  if (lightmap) { input.kind = "lightmap"; input.lightmap_index = lightmap_index; input.lightmap_image = loaded_stage(*lightmap, 1); }
  else if (lightmap_index == -2) { input.kind = "white"; input.white_image = loaded_stage(r->textures->white->image, 0); }
  else input.kind = lightmap_index == -4 ? "picture" : lightmap_index == -3 ? "vertex" : "dynamic";
  qa_compiled_material compiled = qa_compile_implicit_material(input, finish_service(o));
  free(name_key);
  return (prepared_material){ own_compiled(o, compiled), 0 };
}
static prepared_material compile_default_effects(qa_scene_shader_registry *r, const char *name, const qa_renderer_image *lightmap, double lightmap_index, const qa_scene_texture *base, bool mipmap) {
  effects fx; memset(&fx, 0, sizeof fx); fx.registry = r; fx.deferred = 0;
  return compile_shader(r, name, lightmap, lightmap_index, base, mipmap, &fx);
}
static qa_registered_scene_material publish_with(qa_scene_shader_registry *r, qa_shader_registration *registration, const qa_compiled_material *compiled) {
  rimpl *o = ri(r);
  if (o->stage) return qa_shader_replacement_stage_publish(o->stage, registration, compiled);
  return qa_provider_shader_registrations_publish(r->registrations, registration, compiled);
}
static qa_shader_registration *reserve_with(qa_scene_shader_registry *r, qa_shader_logical_key logical) {
  rimpl *o = ri(r);
  return o->stage ? qa_shader_replacement_stage_reserve(o->stage, logical) : qa_provider_shader_registrations_reserve(r->registrations, logical);
}
static void record_push(rimpl *o, src_record rec) {
  if (o->nrec == o->crec) { o->crec = o->crec ? o->crec * 2 : 8; o->records = xg(o->records, o->crec * sizeof *o->records); }
  o->records[o->nrec++] = rec;
}
static const qa_compiled_material *compile_internal(qa_scene_shader_registry *r, int kind) {
  qa_mat_finish_implicit_input input; memset(&input, 0, sizeof input);
  input.kind = kind == 0 ? "default" : "stencil-shadow";
  input.name = kind == 0 ? "*default" : "<stencil shadow>";
  input.profile = r->profile;
  input.base_image = loaded_stage(r->textures->missing->image, 0);
  qa_compiled_material compiled = qa_compile_implicit_material(input, finish_service(ri(r)));
  return own_compiled(ri(r), compiled);
}
static qa_scene_shader_binding unlit_binding(double index, bool mipmap) {
  qa_scene_shader_binding b; memset(&b, 0, sizeof b); b.is_world = false; b.lightmap_index = index; b.mipmap = mipmap; return b;
}
static qa_registered_scene_material register_bound(qa_scene_shader_registry *r, qa_registered_scene_material material, qa_scene_shader_binding binding);
static qa_registered_scene_material register_material(qa_scene_shader_registry *r, const char *name, qa_scene_shader_binding binding);
static qa_registered_scene_material register_source(qa_scene_shader_registry *r, const char *name, qa_scene_shader_binding binding) {
  rimpl *o = ri(r);
  char *key = key_of(name);
  double request = o->source_request++;
  qa_shader_logical_key logical = { key, QA_BINDING_SOURCE, 0, request, NULL, NULL };
  qa_shader_registration *reservation = reserve_with(r, logical);
  /* rebound pending world */
  int rebound_index = -1;
  for (size_t i = 0; i < o->npend; i++) {
    src_record *rec = &o->pending_worlds[i];
    char *rk = key_of(rec->name);
    int match = !strcmp(rk, key) && binding.is_world && rec->binding.is_world && rec->binding.world == binding.world
      && rec->binding.lightmap_index == binding.lightmap_index
      && ((rec->binding.base_texture == NULL && binding.base_texture == NULL)
          || (rec->binding.base_texture && binding.base_texture && !strcmp(rec->binding.base_texture->name, binding.base_texture->name)));
    free(rk);
    if (match) { rebound_index = (int)i; break; }
  }
  if (rebound_index >= 0) {
    src_record rebound = o->pending_worlds[rebound_index];
    if (o->stage) qa_shader_replacement_stage_cancel(o->stage, logical, reservation);
    else qa_provider_shader_registrations_cancel(r->registrations, logical, reservation);
    prepared_material prepared = compile_default_effects(r, name, binding.has_lightmap ? &binding.lightmap : NULL, binding.lightmap_index, binding.base_texture, binding.lightmap_index != -4);
    qa_registered_scene_material material = publish_with(r, reserve_with(r, rebound.logical), prepared.compiled);
    memmove(o->pending_worlds + rebound_index, o->pending_worlds + rebound_index + 1, (o->npend - (size_t)rebound_index - 1) * sizeof *o->pending_worlds);
    o->npend--;
    rebound.binding = binding; rebound.material = material; rebound.defaulted = prepared.defaulted;
    record_push(o, rebound);
    free(key);
    return material;
  }
  src_record *previous = NULL;
  for (size_t i = 0; i < o->nrec; i++) {
    src_record *record = &o->records[i];
    char *rk = key_of(record->name); int same = !strcmp(rk, key); free(rk);
    if (!same || (previous && previous->order > record->order)) continue;
    double index = qa_registered_scene_material_finished(record->material)->lightmap_index;
    if (record->defaulted || (index == binding.lightmap_index && (index < 0 || (binding.is_world && record->binding.is_world && record->binding.world == binding.world))))
      previous = record;
  }
  if (previous) {
    if (o->stage) qa_shader_replacement_stage_cancel(o->stage, logical, reservation);
    else qa_provider_shader_registrations_cancel(r->registrations, logical, reservation);
    int has_generated = 0; for (size_t i = 0; i < o->ngp; i++) if (!strcmp(o->gen_pics[i].key, key)) has_generated = 1;
    if (previous->defaulted && has_generated) {
      prepared_material prepared = compile_default_effects(r, name, binding.is_world && binding.has_lightmap ? &binding.lightmap : NULL, binding.lightmap_index, binding.is_world ? binding.base_texture : NULL, binding.is_world ? binding.lightmap_index != -4 : binding.mipmap);
      qa_registered_scene_material material = publish_with(r, reserve_with(r, previous->logical), prepared.compiled);
      previous->binding = binding; previous->material = material; previous->defaulted = prepared.defaulted;
      free(key);
      return material;
    }
    qa_registered_scene_material m = previous->material;
    free(key);
    return m;
  }
  prepared_material prepared = compile_default_effects(r, name, binding.is_world && binding.has_lightmap ? &binding.lightmap : NULL, binding.lightmap_index, binding.is_world ? binding.base_texture : NULL, binding.is_world ? binding.lightmap_index != -4 : binding.mipmap);
  qa_registered_scene_material material = publish_with(r, reservation, prepared.compiled);
  src_record rec; memset(&rec, 0, sizeof rec);
  rec.logical = logical; rec.logical_name = key; rec.logical.name = key;
  rec.order = (double)(o->nrec + o->npend); rec.name = xdup(name); rec.kind = 2; rec.binding = binding; rec.material = material; rec.defaulted = prepared.defaulted;
  record_push(o, rec);
  return material;
}
static qa_registered_scene_material register_material(qa_scene_shader_registry *r, const char *name, qa_scene_shader_binding binding) {
  rimpl *o = ri(r);
  if ((name[0] == '\0') && o->has_source_default) return o->source_default;
  if (o->has_source_default) return register_source(r, name, binding);
  const qa_renderer_image *lightmap = binding.is_world && binding.has_lightmap ? &binding.lightmap : NULL;
  const qa_scene_texture *base_texture = binding.is_world ? binding.base_texture : NULL;
  double lightmap_index = binding.lightmap_index;
  bool mipmap = binding.is_world ? lightmap_index != -4 : binding.mipmap;
  char *name_key = key_of(name); scratch_add(o, name_key);
  qa_shader_logical_key logical; memset(&logical, 0, sizeof logical);
  logical.name = name_key; logical.lightmap_index = binding.lightmap_index;
  if (!binding.is_world) logical.kind = QA_BINDING_UNLIT;
  else { logical.kind = QA_BINDING_WORLD; logical.world = binding.world; logical.base_texture_name = base_texture ? base_texture->name : NULL; }
  char *provider_key = qa_provider_shader_registrations_key(r->registrations, logical);
  char cache_key[4096];
  snprintf(cache_key, sizeof cache_key, "%s|%s|%s", provider_key,
           QA_JS_NUMBER(lightmap ? lightmap->ordinal : -1.0), QA_JS_NUMBER(base_texture ? base_texture->image.ordinal : -1.0));
  free(provider_key);
  for (size_t i = 0; i < o->ncomp; i++) if (!strcmp(o->compiled[i].key, cache_key)) return o->compiled[i].material;
  if (!binding.is_world) {
    char * key2;
    char * name2;
    if (o->nreq == o->creq) { o->creq = o->creq ? o->creq * 2 : 8; o->img_reqs = xg(o->img_reqs, o->creq * sizeof *o->img_reqs); }
    o->img_reqs[o->nreq++] = (key2 = xdup(cache_key), name2 = xdup(name), (img_req){ key2, name2, binding });
  }
  qa_shader_registration *registration = reserve_with(r, logical);
  prepared_material current = compile_default_effects(r, name, lightmap, lightmap_index, base_texture, mipmap);
  if (current.defaulted) {
    int present2 = 0; for (size_t i = 0; i < o->ndef; i++) if (!strcmp(o->defaulted[i], cache_key)) present2 = 1;
    if (!present2) { if (o->ndef == o->cdef) { o->cdef = o->cdef ? o->cdef * 2 : 8; o->defaulted = xg(o->defaulted, o->cdef * sizeof(char *)); } o->defaulted[o->ndef++] = xdup(cache_key); }
  } else {
    for (size_t i = 0; i < o->ndef; i++) if (!strcmp(o->defaulted[i], cache_key)) { free(o->defaulted[i]); memmove(o->defaulted + i, o->defaulted + i + 1, (o->ndef - i - 1) * sizeof(char *)); o->ndef--; break; }
  }
  qa_registered_scene_material material = publish_with(r, registration, current.compiled);
  if (o->ncomp == o->ccomp) { o->ccomp = o->ccomp ? o->ccomp * 2 : 8; o->compiled = xg(o->compiled, o->ccomp * sizeof *o->compiled); }
  o->compiled[o->ncomp++] = (compiled_entry){ xdup(cache_key), material, current.defaulted };
  return material;
}
static qa_registered_scene_material register_bound(qa_scene_shader_registry *r, qa_registered_scene_material material, qa_scene_shader_binding binding) {
  rimpl *o = ri(r);
  for (size_t i = 0; i < o->nbind; i++) if (o->bindings[i].registration == material.registration) return material;
  if (o->nbind == o->cbind) { o->cbind = o->cbind ? o->cbind * 2 : 8; o->bindings = xg(o->bindings, o->cbind * sizeof *o->bindings); }
  o->bindings[o->nbind++] = (binding_entry){ material.registration, material, binding };
  if (!o->stage) {
    qa_scene_remap_source source = qa_scene_shader_registry_remap_source(r);
    qa_scene_material_registrations_bind_material(r->registrations->owner, source, material, binding);
  }
  return material;
}
qa_registered_scene_material qa_scene_shader_registry_register(qa_scene_shader_registry *r, const char *name, const qa_scene_shader_binding *binding) {
  qa_scene_shader_binding b = binding ? *binding : unlit_binding(-1, true);
  qa_registered_scene_material material = register_material(r, name, b);
  return register_bound(r, material, b);
}
qa_registered_scene_material qa_scene_shader_registry_register_unlit(qa_scene_shader_registry *r, const char *name) {
  return qa_scene_shader_registry_register(r, name, NULL);
}
void qa_scene_shader_registry_init(qa_scene_shader_registry *r, qa_scene_texture_loader *textures, qa_provider_shader_registrations *registrations,
    const qa_finish_shader_profile *profile, qa_scene_play_cinematic_fn play_cinematic, void *play_ctx, const char *family, qa_shader_replacement_stage *stage) {
  memset(r, 0, sizeof *r);
  rimpl *o = xalloc(sizeof *o); memset(o, 0, sizeof *o); o->self = r;
  o->play = play_cinematic; o->play_ctx = play_ctx;
  o->family = xdup(family ? family : "q3");
  o->stage = stage;
  r->_impl = o; r->textures = textures; r->registrations = registrations;
  r->profile = profile ? *profile : qa_default_shader_profile;
  qa_sky_builder_init(&r->sky);
}
void qa_scene_shader_registry_destroy(qa_scene_shader_registry *r) {
  if (!r || !r->_impl) return;
  rimpl *o = ri(r);
  for (size_t i = 0; i < o->nprog; i++) free(o->programs[i].key);
  free(o->programs);
  for (size_t i = 0; i < o->ninsp; i++) qa_shader_script_inspection_free(&o->inspections[i]);
  free(o->inspections);
  for (size_t i = 0; i < o->ncomp; i++) free(o->compiled[i].key);
  free(o->compiled);
  for (size_t i = 0; i < o->ndef; i++) free(o->defaulted[i]);
  free(o->defaulted); free(o->bindings); free(o->bind_scratch); free(o->mat_scratch); free(o->mb_scratch); free(o->pic_orders);
  for (size_t i = 0; i < o->ngp; i++) free(o->gen_pics[i].key);
  free(o->gen_pics);
  for (size_t i = 0; i < o->ngt; i++) free(o->gen_texs[i].key);
  free(o->gen_texs);
  for (size_t i = 0; i < o->nreq; i++) { free(o->img_reqs[i].key); free(o->img_reqs[i].name); }
  free(o->img_reqs);
  for (size_t i = 0; i < o->nrec; i++) { free(o->records[i].name); free(o->records[i].logical_name); }
  free(o->records); free(o->pending_worlds);
  for (size_t i = 0; i < o->nowned; i++) { qa_compiled_material_destroy(o->owned_compiled[i]); free(o->owned_compiled[i]); }
  free(o->owned_compiled);
  for (size_t i = 0; i < o->nfin; i++) qa_finished_shader_result_free(&o->finishes[i]);
  free(o->finishes);
  for (size_t i = 0; i < o->nimp; i++) qa_finish_shader_input_result_free(&o->implicit_inputs[i]);
  free(o->implicit_inputs);
  for (size_t i = 0; i < o->nreg; i++) qa_shader_registration_result_free(&o->reg_results[i]);
  free(o->reg_results);
  for (size_t i = 0; i < o->nscratch; i++) free(o->scratch[i]);
  free(o->scratch); free(o->family);
  if (o->stage_owned) { qa_shader_replacement_stage_destroy(o->stage); free(o->stage); }
  free(o);
  for (size_t i = 0; i < r->warning_count; i++) free(r->warnings[i]);
  free(r->warnings);
  memset(r, 0, sizeof *r);
}
qa_source_scene_materials qa_scene_shader_registry_source_materials(qa_scene_shader_registry *r) {
  rimpl *o = ri(r);
  if (!o->has_source) qa_range_error("Source materials have not finished initialization");
  return o->source_materials;
}
bool qa_scene_shader_registry_preparing_replacement(const qa_scene_shader_registry *r) { return ((rimpl *)r->_impl)->stage != NULL; }
qa_registered_scene_material qa_scene_shader_registry_source_world_material(qa_scene_shader_registry *r, qa_registered_scene_material material) {
  rimpl *o = ri(r);
  for (size_t i = 0; i < o->nrec; i++) if (o->records[i].material.registration == material.registration)
    return o->records[i].defaulted ? qa_scene_shader_registry_source_materials(r).default_material : material;
  return material;
}
void qa_scene_shader_registry_initialize_source_materials(qa_scene_shader_registry *r, qa_scene_load_scripts_fn load_scripts, void *ctx) {
  rimpl *o = ri(r);
  if (o->source_initialized) return;
  o->source_initialized = 1;
  const char *names[2] = { "<default>", "<stencil shadow>" };
  qa_registered_scene_material published[2];
  for (int k = 0; k < 2; k++) {
    double request = o->source_request++;
    char *nk = key_of(names[k]); scratch_add(o, nk);
    qa_shader_logical_key logical = { nk, QA_BINDING_SOURCE, 0, request, NULL, NULL };
    qa_shader_registration * registration = reserve_with(r, logical);
    const qa_compiled_material * compiled = compile_internal(r, k);
    qa_registered_scene_material material = publish_with(r, registration, compiled);
    src_record rec; memset(&rec, 0, sizeof rec);
    /* The record owns its logical name (destroy frees it); nk stays in scratch for the registration. */
    rec.logical = logical; rec.logical_name = xdup(nk); rec.logical.name = rec.logical_name; rec.order = (double)o->nrec; rec.name = xdup(names[k]); rec.kind = k;
    rec.binding = unlit_binding(-1, true); rec.material = material; rec.defaulted = 0;
    record_push(o, rec);
    published[k] = material;
  }
  o->source_default = published[0]; o->has_source_default = 1;
  load_scripts(ctx);
  qa_registered_scene_material projection = qa_scene_shader_registry_register(r, "projectionShadow", NULL);
  qa_registered_scene_material flare = qa_scene_shader_registry_register(r, "flareShader", NULL);
  qa_registered_scene_material sun = qa_scene_shader_registry_register(r, "sun", NULL);
  o->source_materials = (qa_source_scene_materials){ published[0], published[1], projection, flare, sun };
  o->has_source = 1;
}
static void register_script_entry(rimpl *o, qa_shader_catalog_entry *entry) {
  char *key = key_of(entry->name);
  int present = 0; for (size_t k = 0; k < o->nprog; k++) if (!strcmp(o->programs[k].key, key)) present = 1;
  if (present) { free(key); return; }
  if (o->nprog == o->cprog) { o->cprog = o->cprog ? o->cprog * 2 : 8; o->programs = xg(o->programs, o->cprog * sizeof *o->programs); }
  int cinematic = 0;
  for (size_t s = 0; s < entry->definition.stage_count; s++)
    if (entry->definition.stages[s].stage.map.kind && !strcmp(entry->definition.stages[s].stage.map.kind, "video")) cinematic = 1;
  o->programs[o->nprog++] = (program_entry){ key, &entry->program, cinematic };
}
void qa_scene_shader_registry_add_script(qa_scene_shader_registry *r, const char *text, const char *source) {
  qa_scene_shader_registry_add_script_files(r, &text, &source, 1);
}
/* TS addScriptFiles: files arrive in FS_ListFilteredFiles order. id (tr_shader.c
 * ScanAndLoadShaderFiles) hashes the first shader of every file but the last-listed ahead of the
 * concatenation, which is built in reverse; the first hashed name wins. */
void qa_scene_shader_registry_add_script_files(qa_scene_shader_registry *r, const char *const *texts, const char *const *sources, size_t count) {
  rimpl *o = ri(r);
  /* Unlexable scripts contribute no shaders in TS (try/catch). The C inspector captures
   * per-entry errors instead of throwing for bad entries, matching the observable program set.
   * Inspections stay owned by the registry: programs point into them. */
  size_t first = o->ninsp;
  for (size_t f = 0; f < count; f++) {
    if (o->ninsp == o->cinsp) { o->cinsp = o->cinsp ? o->cinsp * 2 : 4; o->inspections = xg(o->inspections, o->cinsp * sizeof *o->inspections); }
    qa_shader_script_inspection *insp = &o->inspections[o->ninsp];
    memset(insp, 0, sizeof *insp);
    const char *source = sources != NULL && sources[f] != NULL ? sources[f] : "<shader>";
    qa_inspect_shader_script(texts[f], source, NULL, insp);
    o->ninsp++;
  }
  for (size_t f = 0; f + 1 < count; f++) {
    qa_shader_script_inspection *insp = &o->inspections[first + f];
    if (insp->count > 0) register_script_entry(o, &insp->entries[0]);
  }
  for (size_t f = count; f-- > 0;) {
    qa_shader_script_inspection *insp = &o->inspections[first + f];
    for (size_t i = 0; i < insp->count; i++) register_script_entry(o, &insp->entries[i]);
  }
}
bool qa_scene_shader_registry_has_authored(qa_scene_shader_registry *r, const char *name) {
  rimpl *o = ri(r); char *k = key_of(name); int f = 0;
  for (size_t i = 0; i < o->nprog; i++) if (!strcmp(o->programs[i].key, k)) f = 1;
  free(k); return f;
}
bool qa_scene_shader_registry_has_cinematic(qa_scene_shader_registry *r, const char *name) {
  rimpl *o = ri(r); char *k = key_of(name); int f = 0;
  for (size_t i = 0; i < o->nprog; i++) if (!strcmp(o->programs[i].key, k) && o->programs[i].cinematic) f = 1;
  free(k); return f;
}
const qa_scene_material_binding *qa_scene_shader_registry_material_bindings(qa_scene_shader_registry *r, size_t *out_count) {
  rimpl *o = ri(r);
  free(o->mb_scratch);
  o->mb_scratch = xalloc((o->nbind ? o->nbind : 1) * sizeof *o->mb_scratch);
  for (size_t i = 0; i < o->nbind; i++) o->mb_scratch[i] = (qa_scene_material_binding){ o->bindings[i].material, o->bindings[i].binding };
  if (out_count) *out_count = o->nbind;
  return o->mb_scratch;
}
typedef struct remap_ctx {
  qa_scene_shader_registry *r; qa_shader_replacement_stage *stage; int parent_stage;
  qa_scene_texture_loader *textures; effects fx; int finished;
  qa_registered_scene_material *materials;
} remap_ctx;
static bool remap_current(void *v) { remap_ctx *c = v; return !c->finished && c->r->textures == c->textures && ((rimpl *)c->r->_impl)->stage == (c->parent_stage ? c->stage : ((rimpl *)c->r->_impl)->stage); }
static void remap_commit(void *v) {
  remap_ctx *c = v;
  if (!remap_current(v)) qa_range_error("Prepared shader remap belongs to retired images");
  if (!c->parent_stage) qa_shader_replacement_stage_commit(c->stage);
  for (size_t i = 0; i < c->fx.n; i++) c->fx.fns[i](c->fx.ctxs[i]), free(c->fx.ctxs[i]);
  c->fx.n = 0; c->finished = 1;
}
static void remap_discard(void *v) {
  remap_ctx *c = v;
  if (c->finished) return;
  if (!c->parent_stage) qa_shader_replacement_stage_discard(c->stage);
  for (size_t i = 0; i < c->fx.n; i++) free(c->fx.ctxs[i]);
  c->fx.n = 0; c->finished = 1;
}
void qa_scene_shader_registry_prepare_remap_materials(qa_scene_shader_registry *r, const char *name, const qa_scene_shader_binding *bindings, size_t count, qa_prepared_scene_remap *out) {
  rimpl *o = ri(r);
  remap_ctx *c = xalloc(sizeof *c); memset(c, 0, sizeof *c);
  scratch_add(o, c);
  c->r = r; c->textures = r->textures; c->parent_stage = o->stage != NULL;
  qa_shader_replacement_stage *stage;
  if (c->parent_stage) stage = o->stage;
  else { stage = xalloc(sizeof *stage); scratch_add(o, stage); qa_provider_shader_registrations_begin_replacement(r->registrations, stage); }
  c->stage = stage; c->fx.deferred = 1; c->fx.registry = r;
  c->materials = xalloc((count ? count : 1) * sizeof *c->materials); scratch_add(o, c->materials);
  int accepted = 1;
  char *name_key = key_of(name); scratch_add(o, name_key);
  size_t done = 0;
  for (size_t i = 0; i < count; i++) {
    qa_scene_shader_binding binding = bindings[i];
    const qa_renderer_image *lightmap = binding.is_world && binding.has_lightmap ? &binding.lightmap : NULL;
    const qa_scene_texture *base = binding.is_world ? binding.base_texture : NULL;
    qa_shader_logical_key logical; memset(&logical, 0, sizeof logical); logical.name = name_key;
    if (!binding.is_world) { logical.kind = QA_BINDING_UNLIT; logical.lightmap_index = binding.lightmap_index; }
    else { logical.kind = QA_BINDING_WORLD; logical.world = binding.world; logical.lightmap_index = binding.lightmap_index; logical.base_texture_name = base ? base->name : NULL; }
    prepared_material compiled = compile_shader(r, name, lightmap, binding.lightmap_index, base,
      binding.is_world ? binding.lightmap_index != -4 : binding.mipmap, &c->fx);
    accepted = accepted && !compiled.defaulted;
    c->materials[done++] = qa_shader_replacement_stage_publish(stage, qa_shader_replacement_stage_reserve(stage, logical), compiled.compiled);
  }
  memset(out, 0, sizeof *out);
  out->ctx = c; out->materials = c->materials; out->material_count = done; out->accepted = accepted;
  out->current = remap_current; out->commit = remap_commit; out->discard = remap_discard;
}
void qa_scene_shader_registry_replacement(qa_scene_shader_registry *r, qa_scene_texture_loader *textures, qa_scene_shader_registry *replacement) {
  rimpl *o = ri(r);
  qa_shader_replacement_stage *stage = xalloc(sizeof *stage);
  qa_provider_shader_registrations_begin_replacement(r->registrations, stage);
  qa_scene_shader_registry_init(replacement, textures, r->registrations, &r->profile, o->play, o->play_ctx, o->family, stage);
  rimpl *ro = ri(replacement); ro->stage_owned = 1;
  for (size_t i = 0; i < o->ngp; i++) {
    if (ro->ngp == ro->cgp) { ro->cgp = ro->cgp ? ro->cgp * 2 : 4; ro->gen_pics = xg(ro->gen_pics, ro->cgp * sizeof *ro->gen_pics); }
    ro->gen_pics[ro->ngp++] = (gen_pic){ xdup(o->gen_pics[i].key), o->gen_pics[i].image };
  }
  for (size_t i = 0; i < o->nprog; i++) {
    if (ro->nprog == ro->cprog) { ro->cprog = ro->cprog ? ro->cprog * 2 : 8; ro->programs = xg(ro->programs, ro->cprog * sizeof *ro->programs); }
    ro->programs[ro->nprog++] = (program_entry){ xdup(o->programs[i].key), o->programs[i].program, o->programs[i].cinematic };
  }
}
void qa_scene_shader_registry_prepare_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement) {
  rimpl *o = ri(r), *ro = ri(replacement);
  if (o->has_source) {
    if (!ro->stage) qa_range_error("Source image replacement has no staging owner");
    ro->source_request = o->source_request;
    ro->has_replacement_count = 1; ro->replacement_count = (double)o->nrec;
    for (size_t i = 0; i < o->nrec; i++) {
      src_record *record = &o->records[i];
      qa_shader_registration *registration = qa_shader_replacement_stage_reserve(ro->stage, record->logical);
      if (record->binding.is_world) {
        if (ro->npend == ro->cpend) { ro->cpend = ro->cpend ? ro->cpend * 2 : 4; ro->pending_worlds = xg(ro->pending_worlds, ro->cpend * sizeof *ro->pending_worlds); }
        src_record copy = *record; copy.name = xdup(record->name); copy.logical_name = xdup(record->logical_name); copy.logical.name = copy.logical_name;
        ro->pending_worlds[ro->npend++] = copy;
        continue;
      }
      prepared_material prepared;
      if (record->kind == 2) prepared = compile_default_effects(replacement, record->name, NULL, record->binding.lightmap_index, NULL, record->binding.mipmap);
      else { prepared.compiled = compile_internal(replacement, record->kind); prepared.defaulted = 0; }
      qa_registered_scene_material material = qa_shader_replacement_stage_publish(ro->stage, registration, prepared.compiled);
      src_record rec = *record; rec.name = xdup(record->name); rec.logical_name = xdup(record->logical_name); rec.logical.name = rec.logical_name;
      rec.material = material; rec.defaulted = prepared.defaulted;
      record_push(ro, rec);
      if (ro->nbind == ro->cbind) { ro->cbind = ro->cbind ? ro->cbind * 2 : 8; ro->bindings = xg(ro->bindings, ro->cbind * sizeof *ro->bindings); }
      ro->bindings[ro->nbind++] = (binding_entry){ material.registration, material, record->binding };
    }
    qa_source_scene_materials rebound;
    qa_registered_scene_material *fields[5] = { &rebound.default_material, &rebound.stencil_shadow, &rebound.projection_shadow, &rebound.flare, &rebound.sun };
    qa_registered_scene_material srcs[5] = { o->source_materials.default_material, o->source_materials.stencil_shadow, o->source_materials.projection_shadow, o->source_materials.flare, o->source_materials.sun };
    for (int k = 0; k < 5; k++) {
      int found = 0;
      for (size_t i = 0; i < ro->nrec; i++) if (ro->records[i].material.registration == srcs[k].registration) { *fields[k] = ro->records[i].material; found = 1; }
      if (!found) qa_range_error("Source internal material was not replaced");
    }
    ro->source_materials = rebound; ro->has_source = 1;
    ro->source_default = rebound.default_material; ro->has_source_default = 1;
    ro->source_initialized = 1;
  }
  for (size_t i = 0; i < o->nreq; i++) (void)qa_scene_shader_registry_register(replacement, o->img_reqs[i].name, &o->img_reqs[i].binding);
}
void qa_scene_shader_registry_validate_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement) {
  rimpl *o = ri(r), *ro = ri(replacement);
  if (replacement->registrations != r->registrations || !ro->stage)
    qa_range_error("Shader replacement belongs to another provider or is already complete");
  /* TS sourcePending is always 0 in the sync port. */
  if (ro->has_replacement_count && ro->replacement_count != (double)o->nrec)
    qa_range_error("Source shader allocations changed during replacement");
  qa_shader_replacement_stage_validate(ro->stage);
}
void qa_scene_shader_registry_commit_replacement(qa_scene_shader_registry *r, qa_scene_shader_registry *replacement) {
  qa_scene_shader_registry_validate_replacement(r, replacement);
  rimpl *o = ri(r), *ro = ri(replacement);
  qa_shader_replacement_stage *stage = ro->stage;
  if (!stage) qa_range_error("Shader replacement lost its stage");
  qa_shader_replacement_stage_commit(stage);
  ro->stage = NULL;
  qa_scene_material_registrations *owner = r->registrations->owner;
  if (ro->has_source) {
    qa_source_scene_materials s = ro->source_materials;
    #define RETAINED(m) qa_scene_material_registrations_retained(owner, (m).registration)
    o->source_materials = (qa_source_scene_materials){ RETAINED(s.default_material), RETAINED(s.stencil_shadow), RETAINED(s.projection_shadow), RETAINED(s.flare), RETAINED(s.sun) };
    #undef RETAINED
    o->has_source = 1;
    o->source_default = o->source_materials.default_material; o->has_source_default = 1;
    ro->source_materials = o->source_materials; ro->source_default = o->source_default;
    if (ro->source_request > o->source_request) o->source_request = ro->source_request;
    /* adopt replacement records sorted by order, retaining materials */
    for (size_t i = 0; i < o->nrec; i++) { free(o->records[i].name); free(o->records[i].logical_name); }
    free(o->records); o->records = NULL; o->nrec = 0; o->crec = 0;
    for (size_t i = 0; i < ro->nrec; i++) {
      src_record rec = ro->records[i];
      rec.name = xdup(rec.name); rec.logical_name = xdup(rec.logical_name); rec.logical.name = rec.logical_name;
      rec.material = qa_scene_material_registrations_retained(owner, rec.material.registration);
      record_push(o, rec);
    }
    /* orders are rewritten below (simple stable order) */
    for (size_t i = 1; i < o->nrec; i++) { src_record t = o->records[i]; size_t j = i; while (j && o->records[j-1].order > t.order) { o->records[j] = o->records[j-1]; j--; } o->records[j] = t; }
    for (size_t i = 0; i < o->nrec; i++) o->records[i].order = (double)i;
    ro->source_request = o->source_request;
  }
  /* compiled cache entries become retained materials */
  for (size_t i = 0; i < ro->ncomp; i++) ro->compiled[i].material = qa_scene_material_registrations_retained(owner, ro->compiled[i].material.registration);
  r->textures = replacement->textures;
  for (size_t i = 0; i < o->ngt; i++) free(o->gen_texs[i].key);
  o->ngt = 0;
  for (size_t i = 0; i < ro->ngt; i++) {
    if (o->ngt == o->cgt) { o->cgt = o->cgt ? o->cgt * 2 : 4; o->gen_texs = xg(o->gen_texs, o->cgt * sizeof *o->gen_texs); }
    o->gen_texs[o->ngt++] = (gen_tex){ xdup(ro->gen_texs[i].key), ro->gen_texs[i].texture };
  }
  for (size_t i = 0; i < o->ncomp; i++) free(o->compiled[i].key);
  o->ncomp = 0;
  for (size_t i = 0; i < o->ndef; i++) free(o->defaulted[i]);
  o->ndef = 0;
  for (size_t i = 0; i < ro->ndef; i++) { if (o->ndef == o->cdef) { o->cdef = o->cdef ? o->cdef * 2 : 8; o->defaulted = xg(o->defaulted, o->cdef * sizeof(char *)); } o->defaulted[o->ndef++] = xdup(ro->defaulted[i]); }
  for (size_t i = 0; i < ro->ncomp; i++) { if (o->ncomp == o->ccomp) { o->ccomp = o->ccomp ? o->ccomp * 2 : 8; o->compiled = xg(o->compiled, o->ccomp * sizeof *o->compiled); } o->compiled[o->ncomp++] = (compiled_entry){ xdup(ro->compiled[i].key), ro->compiled[i].material, ro->compiled[i].defaulted }; }
  for (size_t i = 0; i < o->nreq; i++) { free(o->img_reqs[i].key); free(o->img_reqs[i].name); }
  o->nreq = 0;
  for (size_t i = 0; i < ro->nreq; i++) { char * key2; char * name2; if (o->nreq == o->creq) { o->creq = o->creq ? o->creq * 2 : 8; o->img_reqs = xg(o->img_reqs, o->creq * sizeof *o->img_reqs); } o->img_reqs[o->nreq++] = (key2 = xdup(ro->img_reqs[i].key), name2 = xdup(ro->img_reqs[i].name), (img_req){ key2, name2, ro->img_reqs[i].binding }); }
  r->sun_value = replacement->sun_value; r->has_sun = replacement->has_sun;
  o->nbind = 0;
  for (size_t i = 0; i < ro->nbind; i++) {
    if (o->nbind == o->cbind) { o->cbind = o->cbind ? o->cbind * 2 : 8; o->bindings = xg(o->bindings, o->cbind * sizeof *o->bindings); }
    binding_entry be = ro->bindings[i];
    be.material = qa_scene_material_registrations_retained(owner, be.material.registration);
    o->bindings[o->nbind++] = be;
  }
}
void qa_scene_shader_registry_discard_replacement(qa_scene_shader_registry *replacement) {
  rimpl *o = ri(replacement);
  if (!o->stage) qa_range_error("Live shader registry cannot be discarded as a replacement");
  qa_shader_replacement_stage_discard(o->stage);
  for (size_t i = 0; i < o->ncomp; i++) free(o->compiled[i].key);
  o->ncomp = 0;
  for (size_t i = 0; i < o->nreq; i++) { free(o->img_reqs[i].key); free(o->img_reqs[i].name); }
  o->nreq = 0;
}
static qa_picture_asset picture_of(qa_scene_shader_registry *r, const char *name, qa_registered_scene_material compiled) {
  rimpl *o = ri(r);
  double order = 0; int found = 0;
  for (size_t i = 0; i < o->npic; i++) if (o->pic_orders[i].reg == compiled.registration) { order = o->pic_orders[i].order; found = 1; }
  if (!found) {
    order = (double)(o->npic + 1);
    if (o->npic == o->cpic) { o->cpic = o->cpic ? o->cpic * 2 : 8; o->pic_orders = xg(o->pic_orders, o->cpic * sizeof *o->pic_orders); }
    o->pic_orders[o->npic].reg = compiled.registration; o->pic_orders[o->npic].order = order; o->npic++;
  }
  qa_picture_asset asset; memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_MATERIAL; asset.material.name = name; asset.material.order = order;
  asset.material.compiled = compiled.handle ? (const void *)compiled.handle : (const void *)compiled.compiled;
  asset.material.compiled_is_handle = compiled.handle != NULL;
  return asset;
}
qa_picture_asset qa_scene_shader_registry_register_picture(qa_scene_shader_registry *r, const char *name, bool mipmap) {
  qa_scene_shader_binding b = unlit_binding(-4, mipmap);
  qa_registered_scene_material compiled = qa_scene_shader_registry_register(r, name, &b);
  return picture_of(r, name, compiled);
}
qa_picture_asset qa_scene_shader_registry_register_generated_picture(qa_scene_shader_registry *r, const char *name, qa_render_image image) {
  rimpl *o = ri(r);
  char *key = key_of(name);
  for (size_t i = 0; i < o->nreq; i++) {
    char *rk = key_of(o->img_reqs[i].name);
    if (!strcmp(rk, key)) {
      /* await compiled (sync: ensure compiled entry exists) */
      for (size_t k = 0; k < o->ndef; k++) if (!strcmp(o->defaulted[k], o->img_reqs[i].key)) {
        for (size_t c = 0; c < o->ncomp; c++) if (!strcmp(o->compiled[c].key, o->img_reqs[i].key)) { free(o->compiled[c].key); memmove(o->compiled + c, o->compiled + c + 1, (o->ncomp - c - 1) * sizeof *o->compiled); o->ncomp--; break; }
        break;
      }
    }
    free(rk);
  }
  int present = 0; for (size_t i = 0; i < o->ngp; i++) if (!strcmp(o->gen_pics[i].key, key)) present = 1;
  if (!present) {
    if (o->ngp == o->cgp) { o->cgp = o->cgp ? o->cgp * 2 : 4; o->gen_pics = xg(o->gen_pics, o->cgp * sizeof *o->gen_pics); }
    o->gen_pics[o->ngp++] = (gen_pic){ key, image };
  } else free(key);
  return qa_scene_shader_registry_register_picture(r, name, false);
}
bool qa_scene_shader_registry_register_source_picture(qa_scene_shader_registry *r, const char *name, bool mipmap, qa_picture_asset *out) {
  qa_scene_shader_binding b = unlit_binding(-4, mipmap);
  qa_registered_scene_material compiled = qa_scene_shader_registry_register(r, name, &b);
  qa_registered_scene_material world = qa_scene_shader_registry_source_world_material(r, compiled);
  qa_registered_scene_material def = qa_scene_shader_registry_source_materials(r).default_material;
  if (world.handle == def.handle && world.registration == def.registration) return false;
  *out = picture_of(r, name, compiled);
  return true;
}
qa_picture_asset qa_scene_shader_registry_source_default_picture(qa_scene_shader_registry *r) {
  return picture_of(r, "*default", qa_scene_shader_registry_source_materials(r).default_material);
}
/* remap source service */
static void svc_prepare(void *ctx, const char *name, const qa_scene_shader_binding *bindings, size_t count, qa_prepared_scene_remap *out) {
  qa_scene_shader_registry_prepare_remap_materials((qa_scene_shader_registry *)ctx, name, bindings, count, out);
}
static const qa_registered_scene_material *svc_bindings(void *ctx, const qa_scene_shader_binding **bindings_out, size_t *out_count) {
  qa_scene_shader_registry *r = ctx; rimpl *o = ri(r);
  free(o->bind_scratch); free(o->mat_scratch);
  o->bind_scratch = xalloc((o->nbind ? o->nbind : 1) * sizeof *o->bind_scratch);
  o->mat_scratch = xalloc((o->nbind ? o->nbind : 1) * sizeof *o->mat_scratch);
  for (size_t i = 0; i < o->nbind; i++) { o->bind_scratch[i] = o->bindings[i].binding; o->mat_scratch[i] = o->bindings[i].material; }
  *bindings_out = o->bind_scratch;
  if (out_count) *out_count = o->nbind;
  return o->mat_scratch;
}
qa_scene_remap_source qa_scene_shader_registry_remap_source(qa_scene_shader_registry *r) {
  return (qa_scene_remap_source){ r, r->registrations, svc_prepare, svc_bindings };
}
