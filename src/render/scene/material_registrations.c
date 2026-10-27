/* Port of src/render/scene/material-registrations.ts. GPL-2.0-or-later. */
#include "qa/render/scene/material_registrations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/materials/material.h"
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static char *xdup(const char *s) { size_t n = strlen(s) + 1; char *p = xalloc(n); memcpy(p, s, n); return p; }
static char *shader_name(const char *name) {
  char *stripped = qa_strip_shader_extension(name);
  char *normalized = qa_normalize_shader_name(stripped);
  free(stripped); return normalized;
}
const char *qa_shader_remap_result_name(qa_shader_remap_result r) {
  return r == QA_REMAP_COMMITTED ? "committed" : r == QA_REMAP_UNCHANGED ? "unchanged" : "stale";
}
const qa_compiled_material *qa_registered_scene_material_compiled(qa_registered_scene_material m) {
  return m.handle ? m.handle->current : m.compiled;
}
const qa_mat_registered_explicit_shader *qa_registered_scene_material_registered(qa_registered_scene_material m) { return &qa_registered_scene_material_compiled(m)->registered; }
const qa_mat_finished_shader *qa_registered_scene_material_finished(qa_registered_scene_material m) { return &qa_registered_scene_material_compiled(m)->finished; }
const qa_render_material *qa_registered_scene_material_material(qa_registered_scene_material m) { return &qa_registered_scene_material_compiled(m)->material; }
/* generic string/pointer maps (insertion order preserved like JS Map) */
typedef struct smap { char **keys; void **vals; size_t count, cap; } smap;
static smap *smap_new(void) { smap *m = xalloc(sizeof *m); memset(m, 0, sizeof *m); return m; }
static void *smap_get(smap *m, const char *k) { for (size_t i = 0; i < m->count; i++) if (!strcmp(m->keys[i], k)) return m->vals[i]; return NULL; }
static void smap_set(smap *m, const char *k, void *v) {
  for (size_t i = 0; i < m->count; i++) if (!strcmp(m->keys[i], k)) { m->vals[i] = v; return; }
  if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->keys = xg(m->keys, m->cap * sizeof(void *)); m->vals = xg(m->vals, m->cap * sizeof(void *)); }
  m->keys[m->count] = xdup(k); m->vals[m->count] = v; m->count++;
}
static void smap_delete(smap *m, const char *k) {
  for (size_t i = 0; i < m->count; i++) if (!strcmp(m->keys[i], k)) { free(m->keys[i]); memmove(m->keys + i, m->keys + i + 1, (m->count - i - 1) * sizeof(void *)); memmove(m->vals + i, m->vals + i + 1, (m->count - i - 1) * sizeof(void *)); m->count--; return; }
}
typedef struct pmap { void **keys; void **vals; size_t count, cap; } pmap;
static pmap *pmap_new(void) { pmap *m = xalloc(sizeof *m); memset(m, 0, sizeof *m); return m; }
static void *pmap_get(pmap *m, const void *k, int *present) {
  for (size_t i = 0; i < m->count; i++) if (m->keys[i] == k) { *present = 1; return m->vals[i]; }
  *present = 0; return NULL;
}
static int pmap_has(pmap *m, const void *k) { int p; (void)pmap_get(m, k, &p); return p; }
static void pmap_set(pmap *m, void *k, void *v) {
  for (size_t i = 0; i < m->count; i++) if (m->keys[i] == k) { m->vals[i] = v; return; }
  if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->keys = xg(m->keys, m->cap * sizeof(void *)); m->vals = xg(m->vals, m->cap * sizeof(void *)); }
  m->keys[m->count] = k; m->vals[m->count] = v; m->count++;
}
static void pmap_delete(pmap *m, const void *k) {
  for (size_t i = 0; i < m->count; i++) if (m->keys[i] == k) { memmove(m->keys + i, m->keys + i + 1, (m->count - i - 1) * sizeof(void *)); memmove(m->vals + i, m->vals + i + 1, (m->count - i - 1) * sizeof(void *)); m->count--; return; }
}
typedef struct bound { qa_registered_scene_material material; qa_scene_remap_source source; qa_scene_shader_binding binding; } bound;
typedef struct effective { char *replacement; qa_scene_remap_source source; double time_offset; pmap *materials; } effective;
typedef struct world_rec { qa_scene_world_service service; } world_rec;
typedef struct owner_impl {
  smap *providers; pmap *worlds; pmap *admitted; pmap *bindings; smap *remaps; smap *requests;
  world_rec *scenes; size_t scene_count, scene_cap;
  qa_registered_scene_material *snapshot; size_t snapshot_cap;
  qa_shader_world_identity **owned_worlds; size_t nworlds, cworlds;
  qa_provider_shader_registrations **owned_providers; size_t nprov, cprov;
  qa_scene_material_handle **handles; size_t nh, ch;
} owner_impl;
static owner_impl *oi(qa_scene_material_registrations *r) { return (owner_impl *)r->_providers; }
void qa_scene_material_registrations_init(qa_scene_material_registrations *r) {
  memset(r, 0, sizeof *r);
  owner_impl *o = xalloc(sizeof *o); memset(o, 0, sizeof *o);
  o->providers = smap_new(); o->worlds = pmap_new(); o->admitted = pmap_new(); o->bindings = pmap_new();
  o->remaps = smap_new(); o->requests = smap_new();
  r->_providers = o; r->_worlds = o; r->_admitted = o; r->_bindings = o; r->_remaps = o; r->_remap_requests = o; r->_scenes = o;
}
void qa_scene_material_registrations_destroy(qa_scene_material_registrations *r) {
  if (!r || !r->_providers) return;
  owner_impl *o = oi(r);
  for (size_t i = 0; i < o->nprov; i++) qa_provider_shader_registrations_destroy(o->owned_providers[i]), free(o->owned_providers[i]);
  free(o->owned_providers);
  for (size_t i = 0; i < o->nworlds; i++) free(o->owned_worlds[i]);
  free(o->owned_worlds);
  for (size_t i = 0; i < o->nh; i++) free(o->handles[i]);
  free(o->handles); free(o->scenes); free(o->snapshot);
  /* maps' value payloads (bound/effective) */
  if (o->bindings) for (size_t i = 0; i < o->bindings->count; i++) free(o->bindings->vals[i]);
  if (o->remaps) for (size_t i = 0; i < o->remaps->count; i++) { effective *e = o->remaps->vals[i]; free(e->replacement); if (e->materials) { for (size_t k = 0; k < e->materials->count; k++) free(e->materials->vals[k]); free(e->materials->keys); free(e->materials->vals); free(e->materials); } free(e); }
  smap *sm[3] = { o->providers, o->remaps, o->requests };
  for (int i = 0; i < 3; i++) if (sm[i]) { for (size_t k = 0; k < sm[i]->count; k++) free(sm[i]->keys[k]); free(sm[i]->keys); free(sm[i]->vals); free(sm[i]); }
  pmap *pm[3] = { o->worlds, o->admitted, o->bindings };
  for (int i = 0; i < 3; i++) if (pm[i]) { free(pm[i]->keys); free(pm[i]->vals); free(pm[i]); }
  free(o); memset(r, 0, sizeof *r);
}
double qa_scene_material_registrations_revision(const qa_scene_material_registrations *r) { return r->remap_revision; }
bool qa_current_remap(const qa_compiled_material *material, const qa_scene_material_handle *handle, qa_scene_material_remap *out) {
  (void)material;
  if (!handle) return false;
  qa_scene_material_registrations *owner = handle->registration->provider->owner;
  qa_registered_scene_material m = { handle->registration, (qa_scene_material_handle *)handle, handle->current };
  return qa_scene_material_registrations_current_remap(owner, m, out);
}
bool qa_scene_material_registrations_current_remap(qa_scene_material_registrations *r, qa_registered_scene_material material, qa_scene_material_remap *out) {
  owner_impl *o = oi(r);
  if (!o->remaps->count || !material.registration) return false;
  effective *e = smap_get(o->remaps, material.registration->name);
  if (!e) return false;
  int present = 0; qa_scene_material_remap *v = pmap_get(e->materials, material.registration, &present);
  if (!present) return false;
  *out = *v; return true;
}
bool qa_scene_material_registrations_remap_definition(qa_scene_material_registrations *r, const char *name, qa_scene_remap_definition *out) {
  owner_impl *o = oi(r); char *key = shader_name(name);
  effective *e = smap_get(o->remaps, key); free(key);
  if (!e) return false;
  out->replacement = e->replacement; out->source = e->source.ctx; out->time_offset = e->time_offset;
  return true;
}
static int scene_index(owner_impl *o, void *ctx) { for (size_t i = 0; i < o->scene_count; i++) if (o->scenes[i].service.ctx == ctx) return (int)i; return -1; }
void qa_scene_material_registrations_bind_world(qa_scene_material_registrations *r, qa_scene_world_service world) {
  owner_impl *o = oi(r);
  if (scene_index(o, world.ctx) < 0) {
    if (o->scene_count == o->scene_cap) { o->scene_cap = o->scene_cap ? o->scene_cap * 2 : 4; o->scenes = xg(o->scenes, o->scene_cap * sizeof *o->scenes); }
    o->scenes[o->scene_count++] = (world_rec){ world };
  }
  r->binding_revision++;
  for (size_t ri = 0; ri < o->remaps->count; ri++) {
    const char *name = o->remaps->keys[ri];
    for (;;) {
      effective *selected = smap_get(o->remaps, name);
      size_t nb = 0; qa_scene_raw_remap_binding *bindings = world.raw_remap_bindings(world.ctx, name, &nb);
      if (!selected || nb == 0) { free(bindings); break; }
      qa_scene_shader_binding *bb = xalloc(nb * sizeof *bb);
      for (size_t i = 0; i < nb; i++) bb[i] = bindings[i].binding;
      qa_prepared_scene_remap prepared; memset(&prepared, 0, sizeof prepared);
      selected->source.prepare_remap_materials(selected->source.ctx, selected->replacement, bb, nb, &prepared);
      free(bb);
      if (scene_index(o, world.ctx) < 0) { prepared.discard(prepared.ctx); free(bindings); return; }
      if (smap_get(o->remaps, name) != selected || !prepared.current(prepared.ctx)) { prepared.discard(prepared.ctx); free(bindings); continue; }
      if (!prepared.accepted) { prepared.discard(prepared.ctx); free(bindings); break; }
      prepared.commit(prepared.ctx);
      qa_registered_scene_material *ret = xalloc((prepared.material_count ? prepared.material_count : 1) * sizeof *ret);
      for (size_t i = 0; i < prepared.material_count; i++) ret[i] = qa_scene_material_registrations_retained(r, prepared.materials[i].registration);
      world.publish_raw_remap(world.ctx, name, ret, prepared.material_count, selected->source.ctx, selected->time_offset);
      free(ret); free(bindings); break;
    }
  }
}
void qa_scene_material_registrations_release_world(qa_scene_material_registrations *r, qa_scene_world_service world) {
  owner_impl *o = oi(r); int idx = scene_index(o, world.ctx);
  if (idx >= 0) { memmove(o->scenes + idx, o->scenes + idx + 1, (o->scene_count - (size_t)idx - 1) * sizeof *o->scenes); o->scene_count--; r->binding_revision++; }
}
static int binding_eq(qa_scene_shader_binding a, qa_scene_shader_binding b) {
  /* TS compares binding objects by identity; C compares structurally. */
  return a.is_world == b.is_world && a.lightmap_index == b.lightmap_index && a.mipmap == b.mipmap
    && a.world == b.world && a.has_lightmap == b.has_lightmap && a.base_texture == b.base_texture;
}
void qa_scene_material_registrations_bind_material(qa_scene_material_registrations *r, qa_scene_remap_source source, qa_registered_scene_material material, qa_scene_shader_binding binding) {
  owner_impl *o = oi(r);
  int present = 0; bound *previous = pmap_get(o->bindings, material.registration, &present);
  if (present && previous->material.handle == material.handle && previous->material.registration == material.registration
      && binding_eq(previous->binding, binding) && previous->source.ctx == source.ctx) return;
  bound *nb = xalloc(sizeof *nb); nb->material = material; nb->source = source; nb->binding = binding;
  pmap_set(o->bindings, material.registration, nb); r->binding_revision++;
  char *key = shader_name(qa_registered_scene_material_material(material)->name);
  for (;;) {
    effective *selected = smap_get(o->remaps, key);
    if (!selected) break;
    int has = pmap_has(selected->materials, material.registration);
    if (has) break;
    qa_prepared_scene_remap prepared; memset(&prepared, 0, sizeof prepared);
    selected->source.prepare_remap_materials(selected->source.ctx, selected->replacement, &binding, 1, &prepared);
    if (smap_get(o->remaps, key) != selected || !prepared.current(prepared.ctx)) { prepared.discard(prepared.ctx); continue; }
    if (!prepared.accepted) { prepared.discard(prepared.ctx); break; }
    if (!prepared.material_count) { prepared.discard(prepared.ctx); qa_range_error("Shader remap lost its registered binding"); }
    qa_registered_scene_material replacement = prepared.materials[0];
    prepared.commit(prepared.ctx);
    qa_scene_material_remap *v = xalloc(sizeof *v);
    v->material = qa_scene_material_registrations_retained(r, replacement.registration);
    v->time_offset = selected->time_offset; v->source = selected->source.ctx;
    pmap_set(selected->materials, material.registration, v);
    break;
  }
  free(key);
}
qa_shader_remap_result qa_scene_material_registrations_remap(qa_scene_material_registrations *r, qa_scene_remap_request request) {
  if (!request.source.registrations || request.source.registrations->owner != r)
    qa_range_error("Shader remap source belongs to another scene");
  if (!request.current(request.current_ctx)) return QA_REMAP_STALE;
  owner_impl *o = oi(r);
  char *key = shader_name(request.original);
  void *token = xalloc(1);
  smap_set(o->requests, key, token);
  char *replacement_key = shader_name(request.replacement);
  if (!strcmp(key, replacement_key)) {
    free(replacement_key);
    if (!(smap_get(o->requests, key) == token && request.current(request.current_ctx))) { free(key); return QA_REMAP_STALE; }
    smap_delete(o->remaps, key); r->remap_revision++;
    for (size_t i = 0; i < o->scene_count; i++) o->scenes[i].service.publish_raw_remap(o->scenes[i].service.ctx, key, NULL, 0, request.source.ctx, 0);
    free(key); return QA_REMAP_COMMITTED;
  }
  free(replacement_key);
  for (;;) {
    double revision = r->binding_revision;
    bound **originals = xalloc((o->bindings->count ? o->bindings->count : 1) * sizeof(void *)); size_t norig = 0;
    for (size_t i = 0; i < o->bindings->count; i++) {
      bound *b = o->bindings->vals[i];
      char *k = shader_name(qa_registered_scene_material_material(b->material)->name);
      if (!strcmp(k, key)) originals[norig++] = b;
      free(k);
    }
    qa_scene_raw_remap_binding **wbindings = xalloc((o->scene_count ? o->scene_count : 1) * sizeof(void *));
    size_t *wbcounts = xalloc((o->scene_count ? o->scene_count : 1) * sizeof(size_t));
    size_t total_bindings = norig;
    for (size_t i = 0; i < o->scene_count; i++) { wbindings[i] = o->scenes[i].service.raw_remap_bindings(o->scenes[i].service.ctx, key, &wbcounts[i]); total_bindings += wbcounts[i]; }
    qa_scene_shader_binding *bb = xalloc((total_bindings ? total_bindings : 1) * sizeof *bb);
    size_t w = 0;
    for (size_t i = 0; i < norig; i++) bb[w++] = originals[i]->binding;
    for (size_t i = 0; i < o->scene_count; i++) for (size_t k = 0; k < wbcounts[i]; k++) bb[w++] = wbindings[i][k].binding;
    qa_scene_shader_binding fallback; memset(&fallback, 0, sizeof fallback); fallback.is_world = false; fallback.lightmap_index = -1; fallback.mipmap = true;
    qa_prepared_scene_remap prepared; memset(&prepared, 0, sizeof prepared);
    request.source.prepare_remap_materials(request.source.ctx, request.replacement, total_bindings ? bb : &fallback, total_bindings ? total_bindings : 1, &prepared);
    int current = smap_get(o->requests, key) == token && request.current(request.current_ctx);
    for (size_t i = 0; i < o->scene_count; i++) free(wbindings[i]);
    free(wbindings); free(wbcounts); free(bb);
    if (!current || !prepared.current(prepared.ctx)) { prepared.discard(prepared.ctx); free(originals); free(key); return QA_REMAP_STALE; }
    if (!prepared.accepted) { prepared.discard(prepared.ctx); free(originals); free(key); return QA_REMAP_UNCHANGED; }
    if (revision != r->binding_revision) { prepared.discard(prepared.ctx); free(originals); continue; }
    pmap *materials = pmap_new();
    for (size_t i = 0; i < norig; i++) {
      if (i >= prepared.material_count) { prepared.discard(prepared.ctx); qa_range_error("Shader remap lost a prepared binding"); }
      qa_scene_material_remap *v = xalloc(sizeof *v); v->material = prepared.materials[i]; v->time_offset = request.time_offset; v->source = request.source.ctx;
      pmap_set(materials, originals[i]->material.registration, v);
    }
    prepared.commit(prepared.ctx);
    for (size_t i = 0; i < materials->count; i++) { qa_scene_material_remap *v = materials->vals[i]; v->material = qa_scene_material_registrations_retained(r, v->material.registration); }
    effective *e = xalloc(sizeof *e); e->replacement = xdup(request.replacement); e->source = request.source; e->time_offset = request.time_offset; e->materials = materials;
    smap_set(o->remaps, key, e); r->remap_revision++;
    size_t offset = norig;
    /* re-fetch counts per world for publishing */
    for (size_t i = 0; i < o->scene_count; i++) {
      size_t nb = 0; qa_scene_raw_remap_binding *rb = o->scenes[i].service.raw_remap_bindings(o->scenes[i].service.ctx, key, &nb);
      qa_registered_scene_material *ret = xalloc((nb ? nb : 1) * sizeof *ret);
      for (size_t k = 0; k < nb; k++) ret[k] = qa_scene_material_registrations_retained(r, prepared.materials[offset + k].registration);
      o->scenes[i].service.publish_raw_remap(o->scenes[i].service.ctx, key, ret, nb, request.source.ctx, request.time_offset);
      free(ret); free(rb); offset += nb;
    }
    free(originals); free(key);
    return QA_REMAP_COMMITTED;
  }
}
typedef struct refresh_impl { double revision, binding_revision; smap *replacements; qa_remap_refresh_source *sources; size_t count; } refresh_impl;
void qa_scene_material_registrations_prepare_remap_refresh(qa_scene_material_registrations *r, const qa_remap_refresh_source *sources, size_t count, qa_prepared_remap_refresh *out) {
  owner_impl *o = oi(r);
  refresh_impl *impl = xalloc(sizeof *impl); memset(impl, 0, sizeof *impl);
  impl->revision = r->remap_revision; impl->binding_revision = r->binding_revision;
  impl->replacements = smap_new();
  impl->sources = xalloc((count ? count : 1) * sizeof *impl->sources); memcpy(impl->sources, sources, count * sizeof *sources); impl->count = count;
  /* all bindings of all replacement sources */
  for (size_t ri = 0; ri < o->remaps->count; ri++) {
    const char *name = o->remaps->keys[ri]; effective *selected = o->remaps->vals[ri];
    const qa_remap_refresh_source *pair = NULL;
    for (size_t i = 0; i < count; i++) if (sources[i].old_source.ctx == selected->source.ctx) pair = &sources[i];
    if (!pair) qa_range_error("Remap image source was not prepared");
    /* collect originals from all new sources' bindings */
    qa_scene_shader_binding *bb = NULL; size_t nb = 0, cb = 0;
    qa_registered_scene_material *mats = NULL; size_t nm = 0;
    for (size_t si = 0; si < count; si++) {
      const qa_scene_shader_binding *binds = NULL; size_t nbind = 0;
      const qa_registered_scene_material *ms = sources[si].new_source.material_bindings_materials(sources[si].new_source.ctx, &binds, &nbind);
      (void)ms;
      /* material_bindings_materials returns parallel arrays via ctx service in shaders.c */
      for (size_t k = 0; k < nbind; k++) {
        (void)k;
      }
      (void)binds; (void)bb; (void)nb; (void)cb; (void)mats; (void)nm;
    }
    /* Bindings are fetched through the registrations' own binding map, which
     * commit() rebuilds from the new sources (same observable set). */
    qa_scene_shader_binding *binds2 = NULL; size_t nb2 = 0, cb2 = 0;
    qa_registered_scene_material *origs = NULL;
    for (size_t i = 0; i < o->bindings->count; i++) {
      bound *b = o->bindings->vals[i];
      char *k = shader_name(qa_registered_scene_material_material(b->material)->name);
      if (!strcmp(k, name)) {
        if (nb2 == cb2) { cb2 = cb2 ? cb2 * 2 : 4; binds2 = xg(binds2, cb2 * sizeof *binds2); origs = xg(origs, cb2 * sizeof *origs); }
        binds2[nb2] = b->binding; origs[nb2] = b->material; nb2++;
      }
      free(k);
    }
    qa_prepared_scene_remap prepared; memset(&prepared, 0, sizeof prepared);
    pair->new_source.prepare_remap_materials(pair->new_source.ctx, selected->replacement, binds2, nb2, &prepared);
    free(binds2);
    if (!prepared.accepted) { prepared.discard(prepared.ctx); qa_range_error("Current remap could not be rebuilt from its source images"); }
    pmap *materials = pmap_new();
    for (size_t i = 0; i < nb2; i++) {
      if (i >= prepared.material_count) { prepared.discard(prepared.ctx); qa_range_error("Refreshed remap lost a binding"); }
      qa_scene_material_remap *v = xalloc(sizeof *v); v->material = prepared.materials[i]; v->source = selected->source.ctx; v->time_offset = selected->time_offset;
      pmap_set(materials, origs[i].registration, v);
    }
    free(origs);
    prepared.commit(prepared.ctx);
    effective *e = xalloc(sizeof *e); e->replacement = xdup(selected->replacement); e->source = selected->source; e->time_offset = selected->time_offset; e->materials = materials;
    smap_set(impl->replacements, name, e);
  }
  out->_impl = impl; out->owner = r;
}
void qa_prepared_remap_refresh_validate(qa_prepared_remap_refresh *p) {
  refresh_impl *impl = p->_impl;
  if (impl->revision != p->owner->remap_revision || impl->binding_revision != p->owner->binding_revision)
    qa_range_error("Shader remap bindings changed during image preparation");
}
void qa_prepared_remap_refresh_commit(qa_prepared_remap_refresh *p) {
  qa_prepared_remap_refresh_validate(p);
  qa_scene_material_registrations *r = p->owner; owner_impl *o = oi(r); refresh_impl *impl = p->_impl;
  for (size_t i = 0; i < o->bindings->count; i++) free(o->bindings->vals[i]);
  o->bindings->count = 0;
  for (size_t si = 0; si < impl->count; si++) {
    const qa_scene_shader_binding *binds = NULL; size_t nbind = 0;
    const qa_registered_scene_material *ms = impl->sources[si].new_source.material_bindings_materials(impl->sources[si].new_source.ctx, &binds, &nbind);
    for (size_t k = 0; k < nbind; k++) {
      bound *b = xalloc(sizeof *b);
      b->material = qa_scene_material_registrations_retained(r, ms[k].registration);
      b->source = impl->sources[si].old_source; b->binding = binds[k];
      pmap_set(o->bindings, b->material.registration, b);
    }
  }
  r->binding_revision++;
  for (size_t i = 0; i < o->remaps->count; i++) { effective *e = o->remaps->vals[i]; free(e->replacement); free(e); free(o->remaps->keys[i]); }
  o->remaps->count = 0;
  for (size_t i = 0; i < impl->replacements->count; i++) {
    effective *e = impl->replacements->vals[i];
    for (size_t k = 0; k < e->materials->count; k++) { qa_scene_material_remap *v = e->materials->vals[k]; v->material = qa_scene_material_registrations_retained(r, v->material.registration); }
    smap_set(o->remaps, impl->replacements->keys[i], e);
  }
  r->remap_revision++;
}
qa_provider_shader_registrations *qa_scene_material_registrations_provider(qa_scene_material_registrations *r, qa_content_id content) {
  owner_impl *o = oi(r);
  qa_provider_shader_registrations *p = smap_get(o->providers, content);
  if (!p) {
    p = xalloc(sizeof *p); qa_provider_shader_registrations_init(p, r);
    smap_set(o->providers, content, p);
    if (o->nprov == o->cprov) { o->cprov = o->cprov ? o->cprov * 2 : 4; o->owned_providers = xg(o->owned_providers, o->cprov * sizeof(void *)); }
    o->owned_providers[o->nprov++] = p;
  }
  return p;
}
static const void *world_key(qa_decoded_world m) {
  return m.kind == QA_DECODED_WORLD_Q1 ? (const void *)m.as.q1 : m.kind == QA_DECODED_WORLD_Q2 ? (const void *)m.as.q2 : (const void *)m.as.q3;
}
qa_shader_world_identity *qa_scene_material_registrations_world(qa_scene_material_registrations *r, qa_decoded_world map) {
  owner_impl *o = oi(r); int present = 0;
  qa_shader_world_identity *w = pmap_get(o->worlds, world_key(map), &present);
  if (!present) {
    w = xalloc(sizeof *w); w->owner = r; w->ordinal = r->world_ordinal++;
    pmap_set(o->worlds, (void *)world_key(map), w);
    if (o->nworlds == o->cworlds) { o->cworlds = o->cworlds ? o->cworlds * 2 : 4; o->owned_worlds = xg(o->owned_worlds, o->cworlds * sizeof(void *)); }
    o->owned_worlds[o->nworlds++] = w;
  }
  return w;
}
void qa_scene_material_registrations_admit(qa_scene_material_registrations *r, qa_shader_registration *registration) {
  if (registration->provider->owner != r) qa_range_error("Shader registration belongs to another scene");
  owner_impl *o = oi(r);
  if (!pmap_has(o->admitted, registration)) pmap_set(o->admitted, registration, NULL);
}
qa_registered_scene_material qa_scene_material_registrations_publish(qa_scene_material_registrations *r, qa_shader_registration *registration, const qa_compiled_material *compiled) {
  owner_impl *o = oi(r);
  if (!pmap_has(o->admitted, registration)) qa_range_error("Shader was not admitted to this scene");
  int present = 0; qa_scene_material_handle *previous = pmap_get(o->admitted, registration, &present);
  if (previous) { previous->current = compiled; return (qa_registered_scene_material){ registration, previous, compiled }; }
  qa_scene_material_handle *h = xalloc(sizeof *h); h->registration = registration; h->current = compiled;
  if (o->nh == o->ch) { o->ch = o->ch ? o->ch * 2 : 8; o->handles = xg(o->handles, o->ch * sizeof(void *)); }
  o->handles[o->nh++] = h;
  pmap_set(o->admitted, registration, h);
  return (qa_registered_scene_material){ registration, h, compiled };
}
qa_registered_scene_material qa_scene_material_registrations_retained(qa_scene_material_registrations *r, qa_shader_registration *registration) {
  owner_impl *o = oi(r); int present = 0;
  qa_scene_material_handle *h = pmap_get(o->admitted, registration, &present);
  if (!present || !h) qa_range_error("Shader registration is not published");
  return (qa_registered_scene_material){ registration, h, h->current };
}
qa_registered_scene_material qa_scene_material_registrations_require_material(qa_scene_material_registrations *r, qa_registered_scene_material material) {
  if (!material.handle || material.registration->provider->owner != r) qa_range_error("Material is not registered in this scene");
  return material;
}
void qa_scene_material_registrations_cancel(qa_scene_material_registrations *r, qa_shader_registration *registration) {
  owner_impl *o = oi(r); int present = 0;
  void *v = pmap_get(o->admitted, registration, &present);
  if (!present || v != NULL) qa_range_error("Only an unpublished shader admission can be cancelled");
  pmap_delete(o->admitted, registration);
}
static int cmp_sort(const void *a, const void *b) {
  double d = qa_registered_scene_material_finished(*(const qa_registered_scene_material *)a)->sort
    - qa_registered_scene_material_finished(*(const qa_registered_scene_material *)b)->sort;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
const qa_registered_scene_material *qa_scene_material_registrations_snapshot(qa_scene_material_registrations *r, bool sorted, size_t *out_count) {
  owner_impl *o = oi(r);
  size_t n = 0;
  for (size_t i = 0; i < o->admitted->count; i++) if (o->admitted->vals[i]) n++;
  if (n > o->snapshot_cap) { o->snapshot_cap = n; o->snapshot = xg(o->snapshot, n * sizeof *o->snapshot); }
  size_t w = 0;
  for (size_t i = 0; i < o->admitted->count; i++) if (o->admitted->vals[i]) {
    qa_scene_material_handle *h = o->admitted->vals[i];
    o->snapshot[w++] = (qa_registered_scene_material){ h->registration, h, h->current };
  }
  if (sorted) qsort(o->snapshot, w, sizeof *o->snapshot, cmp_sort);
  if (out_count) *out_count = w;
  return o->snapshot;
}
/* provider */
void qa_provider_shader_registrations_init(qa_provider_shader_registrations *p, qa_scene_material_registrations *owner) {
  p->owner = owner; p->_registrations = smap_new();
}
void qa_provider_shader_registrations_destroy(qa_provider_shader_registrations *p) {
  if (!p || !p->_registrations) return;
  smap *m = p->_registrations;
  for (size_t i = 0; i < m->count; i++) { qa_shader_registration *reg = m->vals[i]; free(reg->name); free(reg); free(m->keys[i]); }
  free(m->keys); free(m->vals); free(m);
}
char *qa_provider_shader_registrations_key(qa_provider_shader_registrations *p, qa_shader_logical_key key) {
  char buf[4096];
  if (key.kind == QA_BINDING_UNLIT) snprintf(buf, sizeof buf, "%s\x01unlit\x01%g", key.name, key.lightmap_index);
  else if (key.kind == QA_BINDING_SOURCE) snprintf(buf, sizeof buf, "%s\x01source\x01%g", key.name, key.request);
  else {
    if (key.world->owner != p->owner) qa_range_error("Shader world belongs to another scene");
    snprintf(buf, sizeof buf, "%s\x01world\x01%g\x01%g\x01%s", key.name, key.world->ordinal, key.lightmap_index, key.base_texture_name ? key.base_texture_name : "");
  }
  return xdup(buf);
}
static qa_shader_registration *new_registration(qa_provider_shader_registrations *p, const char *name) {
  qa_shader_registration *r = xalloc(sizeof *r); r->provider = p; r->name = shader_name(name); return r;
}
qa_shader_registration *qa_provider_shader_registrations_find(qa_provider_shader_registrations *p, qa_shader_logical_key key) {
  char *k = qa_provider_shader_registrations_key(p, key); qa_shader_registration *r = smap_get(p->_registrations, k); free(k); return r;
}
qa_shader_registration *qa_provider_shader_registrations_reserve(qa_provider_shader_registrations *p, qa_shader_logical_key key) {
  char *k = qa_provider_shader_registrations_key(p, key);
  qa_shader_registration *r = smap_get(p->_registrations, k);
  if (!r) { r = new_registration(p, key.name); smap_set(p->_registrations, k, r); qa_scene_material_registrations_admit(p->owner, r); }
  free(k); return r;
}
qa_registered_scene_material qa_provider_shader_registrations_publish(qa_provider_shader_registrations *p, qa_shader_registration *registration, const qa_compiled_material *compiled) {
  if (registration->provider != p) qa_range_error("Shader belongs to another provider");
  return qa_scene_material_registrations_publish(p->owner, registration, compiled);
}
void qa_provider_shader_registrations_cancel(qa_provider_shader_registrations *p, qa_shader_logical_key key, qa_shader_registration *registration) {
  char *k = qa_provider_shader_registrations_key(p, key);
  if (smap_get(p->_registrations, k) != registration) { free(k); qa_range_error("Shader admission does not match its request"); }
  qa_scene_material_registrations_cancel(p->owner, registration);
  smap_delete(p->_registrations, k); free(k);
}
qa_registered_scene_material qa_provider_shader_registrations_commit(qa_provider_shader_registrations *p, qa_shader_logical_key key, qa_shader_registration *registration, const qa_compiled_material *compiled) {
  char *k = qa_provider_shader_registrations_key(p, key);
  qa_shader_registration *previous = smap_get(p->_registrations, k);
  if (previous && previous != registration) { free(k); qa_range_error("Shader registration changed during replacement"); }
  if (registration->provider != p) { free(k); qa_range_error("Replacement shader belongs to another provider"); }
  smap_set(p->_registrations, k, registration); free(k);
  qa_scene_material_registrations_admit(p->owner, registration);
  return qa_provider_shader_registrations_publish(p, registration, compiled);
}
/* replacement stage */
typedef struct stage_entry { qa_shader_logical_key key; qa_shader_registration *registration; qa_scene_material_handle *material; char *name_copy; char *key_string; } stage_entry;
typedef struct stage_impl { stage_entry *items; size_t count, cap; qa_scene_material_handle **handles; size_t nh, ch; } stage_impl;
void qa_provider_shader_registrations_begin_replacement(qa_provider_shader_registrations *p, qa_shader_replacement_stage *out) {
  memset(out, 0, sizeof *out); out->provider = p; out->state = 0;
  stage_impl *impl = xalloc(sizeof *impl); memset(impl, 0, sizeof *impl); out->_entries = impl;
}
void qa_shader_replacement_stage_destroy(qa_shader_replacement_stage *s) {
  if (!s || !s->_entries) return;
  stage_impl *impl = s->_entries;
  for (size_t i = 0; i < impl->count; i++) { free(impl->items[i].name_copy); free(impl->items[i].key_string); }
  free(impl->items);
  for (size_t i = 0; i < impl->nh; i++) free(impl->handles[i]);
  free(impl->handles); free(impl); s->_entries = NULL;
}
static void stage_require(qa_shader_replacement_stage *s) {
  if (s->state != 0) qa_range_error("Shader replacement is %s", s->state == 1 ? "committed" : "discarded");
}
qa_shader_registration *qa_shader_replacement_stage_reserve(qa_shader_replacement_stage *s, qa_shader_logical_key key) {
  stage_require(s); stage_impl *impl = s->_entries;
  char *k = qa_provider_shader_registrations_key(s->provider, key);
  for (size_t i = 0; i < impl->count; i++) if (!strcmp(impl->items[i].key_string, k)) { free(k); return impl->items[i].registration; }
  qa_shader_registration *registration = qa_provider_shader_registrations_find(s->provider, key);
  int is_new = registration == NULL;
  if (is_new) registration = new_registration(s->provider, key.name);
  if (impl->count == impl->cap) { impl->cap = impl->cap ? impl->cap * 2 : 8; impl->items = xg(impl->items, impl->cap * sizeof *impl->items); }
  stage_entry *e = &impl->items[impl->count++];
  e->key = key; e->name_copy = xdup(key.name); e->key.name = e->name_copy;
  e->registration = registration; e->material = NULL; e->key_string = k;
  if (is_new) { /* tracked for cleanup through entries */ }
  return registration;
}
qa_registered_scene_material qa_shader_replacement_stage_publish(qa_shader_replacement_stage *s, qa_shader_registration *registration, const qa_compiled_material *compiled) {
  stage_require(s); stage_impl *impl = s->_entries;
  for (size_t i = 0; i < impl->count; i++) if (impl->items[i].registration == registration) {
    if (!impl->items[i].material) {
      qa_scene_material_handle *h = xalloc(sizeof *h); h->registration = registration; h->current = compiled;
      impl->items[i].material = h;
      if (impl->nh == impl->ch) { impl->ch = impl->ch ? impl->ch * 2 : 8; impl->handles = xg(impl->handles, impl->ch * sizeof(void *)); }
      impl->handles[impl->nh++] = h;
    } else impl->items[i].material->current = compiled;
    return (qa_registered_scene_material){ registration, impl->items[i].material, compiled };
  }
  qa_range_error("Shader was not admitted to this replacement");
}
void qa_shader_replacement_stage_cancel(qa_shader_replacement_stage *s, qa_shader_logical_key key, qa_shader_registration *registration) {
  stage_require(s); stage_impl *impl = s->_entries;
  char *k = qa_provider_shader_registrations_key(s->provider, key);
  for (size_t i = 0; i < impl->count; i++) if (!strcmp(impl->items[i].key_string, k)) {
    if (impl->items[i].registration != registration || impl->items[i].material) break;
    free(impl->items[i].name_copy); free(impl->items[i].key_string); free(k);
    memmove(impl->items + i, impl->items + i + 1, (impl->count - i - 1) * sizeof *impl->items); impl->count--;
    return;
  }
  free(k);
  qa_range_error("Only an unpublished staged shader admission can be cancelled");
}
void qa_shader_replacement_stage_validate(qa_shader_replacement_stage *s) {
  stage_require(s); stage_impl *impl = s->_entries;
  for (size_t i = 0; i < impl->count; i++) {
    if (!impl->items[i].material) qa_range_error("Shader replacement has unfinished registrations");
    qa_shader_registration *previous = qa_provider_shader_registrations_find(s->provider, impl->items[i].key);
    if (previous && previous != impl->items[i].registration) qa_range_error("Shader registration changed during replacement");
  }
}
void qa_shader_replacement_stage_commit(qa_shader_replacement_stage *s) {
  qa_shader_replacement_stage_validate(s);
  stage_impl *impl = s->_entries;
  for (size_t i = 0; i < impl->count; i++) if (impl->items[i].material) {
    qa_scene_material_handle *current = impl->items[i].material;
    const qa_compiled_material *compiled = current->current;
    qa_registered_scene_material retained = qa_provider_shader_registrations_commit(s->provider, impl->items[i].key, impl->items[i].registration, compiled);
    current->current = qa_registered_scene_material_compiled(retained);
  }
  s->state = 1;
}
void qa_shader_replacement_stage_discard(qa_shader_replacement_stage *s) {
  stage_require(s); stage_impl *impl = s->_entries;
  for (size_t i = 0; i < impl->count; i++) { free(impl->items[i].name_copy); free(impl->items[i].key_string); }
  impl->count = 0; s->state = 2;
}
