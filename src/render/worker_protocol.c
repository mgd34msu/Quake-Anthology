/* Port of src/render/worker-protocol.ts. GPL-2.0-or-later. */
#include "qa/render/worker_protocol.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- arena --- */
typedef struct arena_block { struct arena_block *next; size_t used, capacity; unsigned char *data; } arena_block;
struct qa_wire_arena { arena_block *blocks; size_t total; };
qa_wire_arena *qa_wire_arena_create(void) { qa_wire_arena *a = (qa_wire_arena *)calloc(1, sizeof *a); if (a == NULL) qa_range_error("wire arena allocation failed"); return a; }
void qa_wire_arena_destroy(qa_wire_arena *a) {
  if (a == NULL) return;
  for (arena_block *b = a->blocks; b != NULL;) { arena_block *next = b->next; free(b->data); free(b); b = next; }
  free(a);
}
size_t qa_wire_arena_used(const qa_wire_arena *a) { return a->total; }
static void *arena_alloc(qa_wire_arena *a, size_t size) {
  size_t aligned = (size + 15u) & ~(size_t)15u;
  if (aligned == 0) aligned = 16;
  a->total += aligned;
  arena_block *b = a->blocks;
  if (b == NULL || b->capacity - b->used < aligned) {
    size_t capacity = aligned > 65536 ? aligned : 65536;
    b = (arena_block *)malloc(sizeof *b);
    unsigned char *data = (unsigned char *)malloc(capacity);
    if (b == NULL || data == NULL) qa_range_error("wire arena allocation failed");
    b->data = data; b->used = 0; b->capacity = capacity; b->next = a->blocks; a->blocks = b;
  }
  void *out = b->data + b->used; b->used += aligned;
  memset(out, 0, aligned);
  return out;
}
void *qa_wire_arena_copy(qa_wire_arena *a, const void *source, size_t size) {
  if (source == NULL || size == 0) return NULL;
  void *out = arena_alloc(a, size); memcpy(out, source, size); return out;
}
static const void *arena_copy(qa_wire_arena *a, const void *source, size_t size) { return qa_wire_arena_copy(a, source, size); }
#define ARENA_ARRAY(a, source, count) arena_copy((a), (source), (count) * sizeof *(source))
static const char *arena_string(qa_wire_arena *a, const char *text) { return text == NULL ? NULL : (const char *)arena_copy(a, text, strlen(text) + 1); }

/* --- validation --- */
static double wire_integer(double v) { if (v != floor(v) || v < 0 || v > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Invalid renderer wire integer"); return v; }
static const char *choice(const char *value, const char *const *choices, size_t n) {
  if (value != NULL) for (size_t i = 0; i < n; i++) if (!strcmp(value, choices[i])) return choices[i];
  qa_range_error("Invalid renderer wire variant");
}
#define CHOICE(value, table) choice((value), (table), sizeof(table) / sizeof *(table))
static const char *const CULLS[] = {"none", "back", "front"};
static const char *const BLENDS[] = {"zero", "one", "src-color", "one-minus-src-color", "dst-color", "one-minus-dst-color", "src-alpha", "one-minus-src-alpha", "dst-alpha", "one-minus-dst-alpha", "src-alpha-saturate"};
static const char *const FILTERS[] = {"nearest", "linear", "nearest-mipmap-nearest", "linear-mipmap-nearest", "nearest-mipmap-linear", "linear-mipmap-linear"};
static const char *const WRAPS[] = {"repeat", "clamp"};
static const char *const DEPTH_TESTS[] = {"less-equal", "equal", "always"};
static const char *const ALPHA_TESTS[] = {"none", "gt0", "lt128", "ge128"};
static const char *const FOG_EFFECTS[] = {"color", "none", "rgb", "alpha", "rgba", "overlay"};
static const char *const ENVIRONMENTS[] = {"modulate", "add", "replace"};
static const char *const BUFFERS[] = {"front", "back", "back-left", "back-right"};
static const char *const WORLD_PASSES[] = {"lightmap", "texture", "material-lightmap"};
static void validate_shadow(const qa_q2_shadow_projection *s) {
  if (s->kind == NULL || (strcmp(s->kind, "none") && strcmp(s->kind, "point") && strcmp(s->kind, "cone"))) qa_range_error("Invalid renderer shadow projection");
}
qa_image_level qa_decode_wire_image_level(qa_image_level level) {
  double w = wire_integer(level.width), h = wire_integer(level.height);
  if ((double)level.pixels.length != w * h * 4) qa_range_error("Invalid renderer RGBA readback");
  return level;
}

/* Variant strings are TS literals: a retained snapshot names the static spelling, so
 * a journal that keeps the operation after acknowledgment needs no arena. Unknown
 * spellings stay arena copies for the decoder to reject. */
static const char *canonical(qa_wire_arena *a, const char *value, const char *const *choices, size_t n) {
  if (value == NULL) return NULL;
  for (size_t i = 0; i < n; i++) if (!strcmp(value, choices[i])) return choices[i];
  return (const char *)qa_wire_arena_copy(a, value, strlen(value) + 1);
}
#define CANONICAL(a, value, table) canonical((a), (value), (table), sizeof(table) / sizeof *(table))
static const char *const TRANSPARENCIES[] = {"opaque", "index", "q1-fence"};

/* --- encoder --- */
typedef struct img_entry { double ordinal; qa_renderer_image image; char *name; } img_entry;
typedef struct src_entry { double token; qa_dynamic_image_source source; } src_entry;
typedef struct ack_entry { double id; qa_image_resource_operation op; qa_wire_arena *arena; } ack_entry;
typedef struct eimpl {
  img_entry *images; size_t image_count, image_cap;
  src_entry *sources; size_t source_count, source_cap;
  ack_entry *acks; size_t ack_count, ack_cap;
  double next_token, next_ack;
  qa_wire_arena *acknowledged; /* backs the operation acknowledge last returned */
  char **failures; size_t failure_count, failure_cap;
} eimpl;
static eimpl *E(qa_wire_encoder *e) { return (eimpl *)e->impl; }
static void *grow(void *ptr, size_t *cap, size_t elem) {
  size_t next = *cap ? *cap * 2 : 8;
  void *grown = realloc(ptr, next * elem);
  if (grown == NULL) qa_range_error("wire encoder allocation failed");
  *cap = next; return grown;
}
static const char *image_name(const qa_renderer_image *image) { const char *n = image->source_is_resource ? image->source_resource.requested_path : image->source_name; return n != NULL ? n : ""; }
bool qa_wire_encoder_init(qa_wire_encoder *e, qa_renderer_resource_owner owner) {
  e->impl = calloc(1, sizeof(eimpl)); if (!e->impl) return false;
  e->owner = owner; E(e)->next_token = 1; E(e)->next_ack = 1; return true;
}
void qa_wire_encoder_clear_sources(qa_wire_encoder *e) { E(e)->source_count = 0; }
void qa_wire_encoder_clear(qa_wire_encoder *e) {
  eimpl *p = E(e);
  for (size_t i = 0; i < p->image_count; i++) free(p->images[i].name);
  p->image_count = 0; p->source_count = 0;
  for (size_t i = 0; i < p->ack_count; i++) qa_wire_arena_destroy(p->acks[i].arena);
  p->ack_count = 0;
  qa_wire_arena_destroy(p->acknowledged); p->acknowledged = NULL;
  for (size_t i = 0; i < p->failure_count; i++) free(p->failures[i]);
  p->failure_count = 0;
}
void qa_wire_encoder_destroy(qa_wire_encoder *e) {
  if (!e->impl) return;
  qa_wire_encoder_clear(e);
  eimpl *p = E(e);
  free(p->images); free(p->sources); free(p->acks); free(p->failures); free(p); e->impl = NULL;
}
qa_renderer_image qa_wire_encoder_original_image(qa_wire_encoder *e, double ordinal) {
  eimpl *p = E(e);
  for (size_t i = 0; i < p->image_count; i++) if (p->images[i].ordinal == ordinal) return p->images[i].image;
  qa_range_error("Unknown renderer image ordinal");
}
qa_dynamic_image_source qa_wire_encoder_source(qa_wire_encoder *e, double token) {
  eimpl *p = E(e);
  for (size_t i = 0; i < p->source_count; i++) if (p->sources[i].token == token) return p->sources[i].source;
  qa_range_error("Unknown renderer dynamic token");
}
qa_image_resource_operation qa_wire_encoder_acknowledge(qa_wire_encoder *e, double token) {
  eimpl *p = E(e); double id = wire_integer(token);
  for (size_t i = 0; i < p->ack_count; i++) if (p->acks[i].id == id) {
    qa_image_resource_operation op = p->acks[i].op;
    qa_wire_arena_destroy(p->acknowledged); p->acknowledged = p->acks[i].arena;
    memmove(&p->acks[i], &p->acks[i + 1], (p->ack_count - i - 1) * sizeof *p->acks); p->ack_count--;
    return op;
  }
  qa_range_error("Unknown renderer image acknowledgment");
}
qa_wire_image qa_wire_encoder_image(qa_wire_encoder *e, qa_renderer_image image) {
  if (image.owner.identity != e->owner.identity || image.owner.session.token != e->owner.session.token || image.owner.generation != e->owner.generation)
    qa_range_error("Image belongs to another renderer lifetime");
  eimpl *p = E(e);
  const char *name = image_name(&image);
  qa_wire_image o; o.ordinal = image.ordinal; o.width = image.width; o.height = image.height;
  for (size_t i = 0; i < p->image_count; i++) if (p->images[i].ordinal == image.ordinal) {
    if (p->images[i].image.width != image.width || p->images[i].image.height != image.height || strcmp(p->images[i].name, name))
      qa_range_error("Renderer image ordinal has another identity");
    o.name = name; return o;
  }
  if (p->image_count == p->image_cap) p->images = (img_entry *)grow(p->images, &p->image_cap, sizeof(img_entry));
  char *owned = strdup(name);
  if (owned == NULL) qa_range_error("wire encoder allocation failed");
  p->images[p->image_count].ordinal = image.ordinal; p->images[p->image_count].image = image; p->images[p->image_count].name = owned; p->image_count++;
  o.name = name;
  return o;
}
/* The name of a bare wire image stays the caller's; packets carry their own copy. */
static qa_wire_image packet_image(qa_wire_encoder *e, qa_wire_arena *a, qa_renderer_image image) {
  qa_wire_image o = qa_wire_encoder_image(e, image);
  o.name = arena_string(a, o.name);
  return o;
}
qa_wire_binding qa_wire_encoder_binding(qa_wire_encoder *e, qa_texture_binding binding) {
  qa_wire_binding o; memset(&o, 0, sizeof o); o.kind = binding.kind;
  if (binding.kind == QA_TEXTURE_BINDING_BIND_IMAGE) { o.image = qa_wire_encoder_image(e, binding.image); return o; }
  if (binding.kind == QA_TEXTURE_BINDING_RETAIN_CURRENT) return o;
  eimpl *p = E(e);
  for (size_t i = 0; i < p->source_count; i++) if (p->sources[i].source.ctx == binding.source.ctx && p->sources[i].source.resolve == binding.source.resolve) { o.token = p->sources[i].token; return o; }
  if (p->source_count == p->source_cap) p->sources = (src_entry *)grow(p->sources, &p->source_cap, sizeof(src_entry));
  double token = p->next_token++;
  p->sources[p->source_count].token = token; p->sources[p->source_count].source = binding.source; p->source_count++;
  o.token = token;
  return o;
}
/* TS deferred(): a throw while encoding becomes data the decoder raises when reached. */
static const char *deferred(qa_wire_encoder *e, void (*body)(void *), void *ctx) {
  char message[1024];
  if (qa_range_error_catch(body, ctx, message, sizeof message)) return NULL;
  eimpl *p = E(e);
  if (p->failure_count == p->failure_cap) p->failures = (char **)grow(p->failures, &p->failure_cap, sizeof(char *));
  char *owned = strdup(message);
  if (owned == NULL) qa_range_error("wire encoder allocation failed");
  p->failures[p->failure_count++] = owned;
  return owned;
}
static const qa_image_level *copy_levels(qa_wire_arena *a, const qa_image_level *levels, size_t count) {
  if (levels == NULL) return NULL;
  qa_image_level *out = (qa_image_level *)arena_alloc(a, count * sizeof *out);
  for (size_t i = 0; i < count; i++) { out[i] = levels[i]; out[i].pixels.bytes = (const uint8_t *)arena_copy(a, levels[i].pixels.bytes, levels[i].pixels.length); }
  return out;
}
static const qa_depth_image_level *copy_depth_levels(qa_wire_arena *a, const qa_depth_image_level *levels, size_t count) {
  if (levels == NULL) return NULL;
  qa_depth_image_level *out = (qa_depth_image_level *)arena_alloc(a, count * sizeof *out);
  for (size_t i = 0; i < count; i++) { out[i] = levels[i]; out[i].pixels = (const float *)ARENA_ARRAY(a, levels[i].pixels, levels[i].pixel_count); }
  return out;
}
typedef struct image_scope { qa_wire_encoder *e; qa_image_resource_operation op; qa_wire_image_operation *out; qa_wire_arena *arena; } image_scope;
static void image_body(void *ctx) {
  image_scope *s = (image_scope *)ctx; qa_image_resource_operation op = s->op; qa_wire_arena *a = s->arena;
  if (op.kind != QA_IMAGE_OP_TEXTURE_MODE) s->out->image = packet_image(s->e, a, op.image);
  else op.filter = CANONICAL(a, op.filter, FILTERS);
  if (op.kind == QA_IMAGE_OP_UPDATE) {
    if (op.update_level != NULL) op.update_level = copy_levels(a, op.update_level, 1);
    if (op.update_depth_level != NULL) op.update_depth_level = copy_depth_levels(a, op.update_depth_level, 1);
  }
  if (op.kind == QA_IMAGE_OP_CREATE) {
    op.sampling.wrap = CANONICAL(a, op.sampling.wrap, WRAPS); op.sampling.filter = CANONICAL(a, op.sampling.filter, FILTERS);
    op.content.levels = copy_levels(a, op.content.levels, op.content.level_count);
    op.content.depth_levels = copy_depth_levels(a, op.content.depth_levels, op.content.depth_level_count);
    if (op.content.kind == QA_RENDER_IMAGE_INDEXED8) {
      op.content.palette.colors.bytes = (const uint8_t *)arena_copy(a, op.content.palette.colors.bytes, op.content.palette.colors.length);
      op.content.transparency.kind = CANONICAL(a, op.content.transparency.kind, TRANSPARENCIES);
      if (op.content.fullbright != NULL) op.content.fullbright = (const qa_fullbright_range *)ARENA_ARRAY(a, op.content.fullbright, 1);
      if (op.content.translation != NULL) {
        qa_byte_view *view = (qa_byte_view *)arena_alloc(a, sizeof *view);
        view->length = op.content.translation->length;
        view->bytes = (const uint8_t *)arena_copy(a, op.content.translation->bytes, view->length);
        op.content.translation = view;
      }
    }
  }
  s->out->operation = op;
}
qa_wire_image_operation qa_wire_encoder_image_operation(qa_wire_encoder *e, qa_image_resource_operation op) {
  qa_wire_image_operation o; memset(&o, 0, sizeof o);
  qa_wire_arena *arena = qa_wire_arena_create();
  image_scope scope = {e, op, &o, arena};
  o.failure = deferred(e, image_body, &scope);
  if (o.failure != NULL) { qa_wire_arena_destroy(arena); return o; }
  eimpl *p = E(e);
  if (p->ack_count == p->ack_cap) p->acks = (ack_entry *)grow(p->acks, &p->ack_cap, sizeof(ack_entry));
  o.acknowledgment = p->next_ack++;
  p->acks[p->ack_count].id = o.acknowledgment; p->acks[p->ack_count].op = o.operation; p->acks[p->ack_count].arena = arena; p->ack_count++;
  return o;
}
static const qa_polygon_offset *copy_offset(qa_wire_arena *a, const qa_polygon_offset *v) { return v == NULL ? NULL : (const qa_polygon_offset *)ARENA_ARRAY(a, v, 1); }
static qa_double_view copy_doubles(qa_wire_arena *a, qa_double_view v) { qa_double_view o; o.count = v.count; o.values = (const double *)ARENA_ARRAY(a, v.values, v.count); return o; }
typedef struct batch_scope { qa_wire_encoder *e; qa_wire_arena *arena; qa_draw_batch batch; qa_wire_batch *out; } batch_scope;
static void batch_body(void *ctx) {
  batch_scope *s = (batch_scope *)ctx; qa_wire_encoder *e = s->e; qa_wire_arena *a = s->arena; qa_draw_batch b = s->batch; qa_wire_batch *o = s->out;
  bool model_shadow = !strcmp(b.lighting.kind, "q2-model-shadow");
  bool lit = strcmp(b.lighting.kind, "vertex") != 0;
  if (lit) {
    if (model_shadow) { o->atlas = packet_image(e, a, b.lighting.atlas_value.image); o->has_atlas = true; }
    else if (b.lighting.atlas != NULL) { o->atlas = packet_image(e, a, b.lighting.atlas->image); o->has_atlas = true; }
  }
  o->texture = qa_wire_encoder_binding(e, b.texture);
  o->texture.image.name = arena_string(a, o->texture.image.name);
  if (b.texturing_is_pair) { o->second = qa_wire_encoder_binding(e, b.second_texture.binding); o->second.image.name = arena_string(a, o->second.image.name); }
  qa_batch_lighting *l = &b.lighting;
  l->kind = arena_string(a, l->kind); l->pass = arena_string(a, l->pass);
  l->world_positions = (const qa_vec3 *)ARENA_ARRAY(a, l->world_positions, l->world_position_count);
  l->normals = (const qa_vec3 *)ARENA_ARRAY(a, l->normals, l->normal_count);
  if (l->atlas != NULL) l->atlas = (const qa_q2_shadow_atlas *)ARENA_ARRAY(a, l->atlas, 1);
  if (l->light_count != 0) {
    qa_q2_fragment_light *lights = (qa_q2_fragment_light *)arena_alloc(a, l->light_count * sizeof *lights);
    for (size_t i = 0; i < l->light_count; i++) {
      lights[i] = l->lights[i];
      if (lights[i].cone != NULL) lights[i].cone = (const qa_q2_light_cone *)ARENA_ARRAY(a, lights[i].cone, 1);
      lights[i].shadow.kind = arena_string(a, lights[i].shadow.kind);
    }
    l->lights = lights;
  }
  if (l->model_shadow_light_count != 0) {
    qa_q2_model_shadow_light *lights = (qa_q2_model_shadow_light *)arena_alloc(a, l->model_shadow_light_count * sizeof *lights);
    for (size_t i = 0; i < l->model_shadow_light_count; i++) { lights[i] = l->model_shadow_lights[i]; lights[i].shadow.kind = arena_string(a, lights[i].shadow.kind); }
    l->model_shadow_lights = lights;
  }
  if (b.fog != NULL) {
    qa_batch_fog *fog = (qa_batch_fog *)arena_alloc(a, sizeof *fog); *fog = *b.fog;
    fog->kind = arena_string(a, fog->kind); fog->effect = arena_string(a, fog->effect); b.fog = fog;
  }
  b.texture_effect = arena_string(a, b.texture_effect);
  b.space = arena_string(a, b.space);
  b.indices = copy_doubles(a, b.indices);
  b.state.blend.source = arena_string(a, b.state.blend.source); b.state.blend.destination = arena_string(a, b.state.blend.destination);
  b.state.depth_test = arena_string(a, b.state.depth_test); b.state.alpha_test = arena_string(a, b.state.alpha_test); b.state.cull = arena_string(a, b.state.cull);
  b.state.polygon_offset = copy_offset(a, b.state.polygon_offset);
  b.vertices = (const qa_render_vertex *)ARENA_ARRAY(a, b.vertices, b.vertex_count);
  b.pair_vertices = (const qa_multitexture_vertex *)ARENA_ARRAY(a, b.pair_vertices, b.pair_vertex_count);
  b.second_texture.environment = arena_string(a, b.second_texture.environment);
  memset(&b.texture.source, 0, sizeof b.texture.source); memset(&b.second_texture.binding.source, 0, sizeof b.second_texture.binding.source);
  o->batch = b;
}
qa_wire_batch qa_wire_encoder_batch(qa_wire_encoder *e, qa_wire_arena *arena, qa_draw_batch batch) {
  qa_wire_batch o; memset(&o, 0, sizeof o);
  batch_scope scope = {e, arena, batch, &o};
  o.failure = deferred(e, batch_body, &scope);
  return o;
}
typedef struct op_scope { qa_wire_encoder *e; qa_wire_arena *arena; qa_render_operation op; qa_wire_operation *out; } op_scope;
static void op_body(void *ctx) {
  op_scope *s = (op_scope *)ctx; qa_wire_encoder *e = s->e; qa_wire_arena *a = s->arena; qa_render_operation op = s->op; qa_wire_operation *o = s->out;
  switch (op.kind) {
  case QA_RENDER_OP_DRAW: case QA_RENDER_OP_OBJECT_OPACITY:
    if (op.batch_count != 0) {
      o->batches = (qa_wire_batch *)arena_alloc(a, op.batch_count * sizeof *o->batches);
      for (size_t i = 0; i < op.batch_count; i++) o->batches[i] = qa_wire_encoder_batch(e, a, op.batches[i]);
    }
    o->batch_count = op.batch_count; op.batches = NULL;
    break;
  case QA_RENDER_OP_DEPTH_ATLAS: {
    o->image = packet_image(e, a, op.image);
    qa_depth_atlas_pass *passes = op.pass_count ? (qa_depth_atlas_pass *)arena_alloc(a, op.pass_count * sizeof *passes) : NULL;
    for (size_t i = 0; i < op.pass_count; i++) {
      passes[i] = op.passes[i];
      if (passes[i].draw_count == 0) continue;
      qa_depth_atlas_draw *draws = (qa_depth_atlas_draw *)arena_alloc(a, passes[i].draw_count * sizeof *draws);
      for (size_t j = 0; j < passes[i].draw_count; j++) {
        draws[j] = passes[i].draws[j];
        draws[j].positions = (const double *)ARENA_ARRAY(a, draws[j].positions, draws[j].position_count);
        draws[j].indices = copy_doubles(a, draws[j].indices);
        draws[j].cull = arena_string(a, draws[j].cull);
        draws[j].polygon_offset = copy_offset(a, draws[j].polygon_offset);
      }
      passes[i].draws = draws;
    }
    op.passes = passes;
    break;
  }
  case QA_RENDER_OP_SKY_SIDE: {
    o->image = packet_image(e, a, op.image);
    qa_sky_strip *strips = op.strip_count ? (qa_sky_strip *)arena_alloc(a, op.strip_count * sizeof *strips) : NULL;
    for (size_t i = 0; i < op.strip_count; i++) { strips[i] = op.strips[i]; strips[i].vertices = (const qa_vertex_position_tex_coord *)ARENA_ARRAY(a, strips[i].vertices, strips[i].vertex_count); }
    op.strips = strips;
    break;
  }
  case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH:
    o->image = packet_image(e, a, op.white_image);
    op.positions = (const qa_vec4 *)ARENA_ARRAY(a, op.positions, op.position_count);
    op.indices = copy_doubles(a, op.indices);
    break;
  case QA_RENDER_OP_Q2_FOG:
    op.fog.fog.kind = arena_string(a, op.fog.fog.kind); op.fog.fog.plane = NULL;
    break;
  case QA_RENDER_OP_CULL: op.cull = arena_string(a, op.cull); break;
  case QA_RENDER_OP_POLYGON_OFFSET: op.polygon_offset = copy_offset(a, op.polygon_offset); break;
  case QA_RENDER_OP_DEPTH_RANGE: case QA_RENDER_OP_DISABLE_PORTAL_CLIP: break;
  }
  o->operation = op;
}
qa_wire_operation qa_wire_encoder_operation(qa_wire_encoder *e, qa_wire_arena *arena, qa_render_operation op) {
  qa_wire_operation o; memset(&o, 0, sizeof o);
  op_scope scope = {e, arena, op, &o};
  o.failure = deferred(e, op_body, &scope);
  return o;
}
typedef struct cmd_scope { qa_wire_encoder *e; qa_wire_arena *arena; qa_render_command c; const double *captures; size_t capture_count; qa_wire_command *out; } cmd_scope;
static qa_wire_operation *encode_ops(qa_wire_encoder *e, qa_wire_arena *a, const qa_render_operation *ops, size_t count) {
  if (count == 0) return NULL;
  qa_wire_operation *out = (qa_wire_operation *)arena_alloc(a, count * sizeof *out);
  for (size_t i = 0; i < count; i++) out[i] = qa_wire_encoder_operation(e, a, ops[i]);
  return out;
}
static void cmd_body(void *ctx) {
  cmd_scope *s = (cmd_scope *)ctx; qa_wire_encoder *e = s->e; qa_wire_arena *a = s->arena; qa_render_command c = s->c; qa_wire_command *o = s->out;
  switch (c.kind) {
  case QA_RENDER_COMMAND_VIEW: {
    qa_render_view_state state = c.view.state;
    if (state.clear != NULL) {
      qa_render_view_clear *clear = (qa_render_view_clear *)arena_alloc(a, sizeof *clear); *clear = *state.clear;
      if (clear->color != NULL) clear->color = (const qa_vec4 *)ARENA_ARRAY(a, clear->color, 1);
      state.clear = clear;
    }
    if (state.clip_plane != NULL) state.clip_plane = (const qa_vec4 *)ARENA_ARRAY(a, state.clip_plane, 1);
    /* TS command() snapshots viewport, clear and clipPlane only. */
    state.has_view_projection = false;
    o->view.state = state;
    o->view.before_view = encode_ops(e, a, c.view.before_view, c.view.before_view_count); o->view.before_view_count = c.view.before_view_count;
    o->view.operations = encode_ops(e, a, c.view.operations, c.view.operation_count); o->view.operation_count = c.view.operation_count;
    return;
  }
  case QA_RENDER_COMMAND_IMAGE_RESOURCE: o->operation = qa_wire_encoder_image_operation(e, c.operation); return;
  case QA_RENDER_COMMAND_STRETCH_PIC: o->image = packet_image(e, a, c.image); o->rect = c.rect; o->uv = c.uv; return;
  case QA_RENDER_COMMAND_SWAP_BUFFERS: o->captures = (const double *)ARENA_ARRAY(a, s->captures, s->capture_count); o->capture_count = s->capture_count; return;
  case QA_RENDER_COMMAND_DRAW_BUFFER: o->buffer = arena_string(a, c.buffer); o->clear = c.clear; return;
  case QA_RENDER_COMMAND_SET_COLOR: o->color = c.color; return;
  }
}
qa_wire_command qa_wire_encoder_command(qa_wire_encoder *e, qa_wire_arena *arena, qa_render_command command, const double *captures, size_t capture_count) {
  qa_wire_command o; memset(&o, 0, sizeof o); o.kind = command.kind;
  cmd_scope scope = {e, arena, command, captures, capture_count, &o};
  o.failure = deferred(e, cmd_body, &scope);
  return o;
}

/* --- decoder --- */
typedef struct dyn_ctx { qa_wire_decoder *decoder; double token; } dyn_ctx;
typedef struct dsrc_entry { double token; qa_dynamic_image_source source; dyn_ctx *ctx; } dsrc_entry;
typedef struct dack_entry { double token; qa_image_resource_operation_kind kind; double ordinal; const void *key; } dack_entry;
typedef struct dimpl {
  img_entry *images; size_t image_count, image_cap;
  dsrc_entry *sources; size_t source_count, source_cap;
  dack_entry *acks; size_t ack_count, ack_cap;
} dimpl;
static dimpl *D(qa_wire_decoder *d) { return (dimpl *)d->impl; }
bool qa_wire_decoder_init(qa_wire_decoder *d, qa_renderer_resource_owner owner, qa_wire_reached_dynamic_fn reached, void *ctx, qa_resolved_resource_reference palette_source) {
  d->impl = calloc(1, sizeof(dimpl)); if (!d->impl) return false;
  d->owner = owner; d->reached_dynamic = reached; d->reached_ctx = ctx; d->palette_source = palette_source;
  return true;
}
void qa_wire_decoder_clear_sources(qa_wire_decoder *d) {
  dimpl *p = D(d);
  for (size_t i = 0; i < p->source_count; i++) free(p->sources[i].ctx);
  p->source_count = 0;
}
void qa_wire_decoder_destroy(qa_wire_decoder *d) {
  if (!d->impl) return;
  qa_wire_decoder_clear_sources(d);
  dimpl *p = D(d);
  for (size_t i = 0; i < p->image_count; i++) free(p->images[i].name);
  free(p->images); free(p->sources); free(p->acks); free(p); d->impl = NULL;
}
qa_renderer_image qa_wire_decoder_image(qa_wire_decoder *d, qa_wire_image wire) {
  double ordinal = wire_integer(wire.ordinal), width = wire_integer(wire.width), height = wire_integer(wire.height);
  if (wire.name == NULL) qa_range_error("Invalid renderer wire variant");
  dimpl *p = D(d);
  for (size_t i = 0; i < p->image_count; i++) if (p->images[i].ordinal == ordinal) {
    qa_renderer_image existing = p->images[i].image;
    if (existing.width != width || existing.height != height || strcmp(p->images[i].name, wire.name)) qa_range_error("Renderer image metadata changed");
    return existing;
  }
  char *name = strdup(wire.name);
  if (name == NULL) qa_range_error("wire decoder allocation failed");
  qa_renderer_image image; memset(&image, 0, sizeof image);
  image.owner = d->owner; image.ordinal = ordinal; image.width = width; image.height = height; image.source_is_resource = false; image.source_name = name;
  if (p->image_count == p->image_cap) p->images = (img_entry *)grow(p->images, &p->image_cap, sizeof(img_entry));
  p->images[p->image_count].ordinal = ordinal; p->images[p->image_count].image = image; p->images[p->image_count].name = name; p->image_count++;
  return image;
}
static qa_renderer_image dyn_resolve(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  dyn_ctx *c = (dyn_ctx *)ctx;
  return c->decoder->reached_dynamic(c->decoder->reached_ctx, c->token, apply, apply_ctx);
}
qa_texture_binding qa_wire_decoder_binding(qa_wire_decoder *d, qa_wire_binding wire) {
  qa_texture_binding o; memset(&o, 0, sizeof o); o.kind = wire.kind;
  if (wire.kind == QA_TEXTURE_BINDING_BIND_IMAGE) { o.image = qa_wire_decoder_image(d, wire.image); return o; }
  if (wire.kind == QA_TEXTURE_BINDING_RETAIN_CURRENT) return o;
  if (wire.kind != QA_TEXTURE_BINDING_DYNAMIC_IMAGE) qa_range_error("Invalid renderer texture binding");
  double token = wire_integer(wire.token); dimpl *p = D(d);
  for (size_t i = 0; i < p->source_count; i++) if (p->sources[i].token == token) { o.source = p->sources[i].source; return o; }
  dyn_ctx *c = (dyn_ctx *)malloc(sizeof *c);
  if (c == NULL) qa_range_error("wire decoder allocation failed");
  c->decoder = d; c->token = token;
  qa_dynamic_image_source source; source.ctx = c; source.resolve = dyn_resolve;
  if (p->source_count == p->source_cap) p->sources = (dsrc_entry *)grow(p->sources, &p->source_cap, sizeof(dsrc_entry));
  p->sources[p->source_count].token = token; p->sources[p->source_count].source = source; p->sources[p->source_count].ctx = c; p->source_count++;
  o.source = source;
  return o;
}
static const void *ack_key(const qa_image_resource_operation *op) {
  switch (op->kind) {
  case QA_IMAGE_OP_CREATE: return op->content.kind == QA_RENDER_IMAGE_DEPTH32F ? (const void *)op->content.depth_levels : (const void *)op->content.levels;
  case QA_IMAGE_OP_UPDATE: return op->update_depth_level != NULL ? (const void *)op->update_depth_level : (const void *)op->update_level;
  case QA_IMAGE_OP_RELEASE: case QA_IMAGE_OP_TEXTURE_MODE: return NULL;
  }
  return NULL;
}
double qa_wire_decoder_acknowledgment(qa_wire_decoder *d, const qa_image_resource_operation *op) {
  dimpl *p = D(d); const void *key = ack_key(op);
  double ordinal = op->kind == QA_IMAGE_OP_TEXTURE_MODE ? -1 : op->image.ordinal;
  for (size_t i = p->ack_count; i-- > 0;) if (p->acks[i].kind == op->kind && p->acks[i].ordinal == ordinal && p->acks[i].key == key) {
    double token = p->acks[i].token;
    memmove(&p->acks[i], &p->acks[i + 1], (p->ack_count - i - 1) * sizeof *p->acks); p->ack_count--;
    return token;
  }
  qa_range_error("Renderer image operation has no acknowledgment");
}
static void validate_level(double width, double height) { (void)wire_integer(width); (void)wire_integer(height); }
qa_image_resource_operation qa_wire_decoder_image_operation(qa_wire_decoder *d, qa_wire_image_operation wire) {
  if (wire.failure != NULL) qa_range_error("%s", wire.failure);
  double token = wire_integer(wire.acknowledgment);
  qa_image_resource_operation op = wire.operation;
  switch (op.kind) {
  case QA_IMAGE_OP_TEXTURE_MODE: op.filter = CHOICE(op.filter, FILTERS); memset(&op.image, 0, sizeof op.image); break;
  case QA_IMAGE_OP_RELEASE: op.image = qa_wire_decoder_image(d, wire.image); break;
  case QA_IMAGE_OP_UPDATE:
    op.image = qa_wire_decoder_image(d, wire.image);
    (void)wire_integer(op.level);
    if (op.update_level != NULL) validate_level(op.update_level->width, op.update_level->height);
    else if (op.update_depth_level != NULL) validate_level(op.update_depth_level->width, op.update_depth_level->height);
    else qa_range_error("Invalid renderer image pixels");
    break;
  case QA_IMAGE_OP_CREATE: {
    op.image = qa_wire_decoder_image(d, wire.image);
    size_t levels = op.content.kind == QA_RENDER_IMAGE_DEPTH32F ? op.content.depth_level_count : op.content.level_count;
    if (levels == 0) qa_range_error("Image has no levels");
    op.sampling.wrap = CHOICE(op.sampling.wrap, WRAPS); op.sampling.filter = CHOICE(op.sampling.filter, FILTERS);
    if (op.content.kind == QA_RENDER_IMAGE_INDEXED8) {
      if (op.content.palette.colors.length != 768) qa_range_error("Invalid indexed palette");
      const char *t = op.content.transparency.kind;
      if (t == NULL) qa_range_error("Invalid palette transparency");
      if (!strcmp(t, "opaque")) op.content.transparency.kind = "opaque";
      else if (!strcmp(t, "index")) { (void)wire_integer(op.content.transparency.index); op.content.transparency.kind = "index"; }
      else if (!strcmp(t, "q1-fence") && op.content.transparency.index == 255) op.content.transparency.kind = "q1-fence";
      else qa_range_error("Invalid palette transparency");
      if (op.content.fullbright != NULL) { (void)wire_integer(op.content.fullbright->first); (void)wire_integer(op.content.fullbright->last); }
      op.content.palette.source = d->palette_source;
    }
    break;
  }
  default: qa_range_error("Invalid renderer resource operation");
  }
  dimpl *p = D(d);
  if (p->ack_count == p->ack_cap) p->acks = (dack_entry *)grow(p->acks, &p->ack_cap, sizeof(dack_entry));
  dack_entry *entry = &p->acks[p->ack_count++];
  entry->token = token; entry->kind = op.kind; entry->ordinal = op.kind == QA_IMAGE_OP_TEXTURE_MODE ? -1 : op.image.ordinal; entry->key = ack_key(&op);
  return op;
}
qa_draw_batch qa_wire_decoder_batch(qa_wire_decoder *d, qa_wire_batch *wire) {
  if (wire->failure != NULL) qa_range_error("%s", wire->failure);
  qa_draw_batch b = wire->batch;
  for (size_t i = 0; i < b.indices.count; i++) (void)wire_integer(b.indices.values[i]);
  b.texture = qa_wire_decoder_binding(d, wire->texture);
  b.state.blend.source = CHOICE(b.state.blend.source, BLENDS); b.state.blend.destination = CHOICE(b.state.blend.destination, BLENDS);
  b.state.depth_test = CHOICE(b.state.depth_test, DEPTH_TESTS); b.state.alpha_test = CHOICE(b.state.alpha_test, ALPHA_TESTS); b.state.cull = CHOICE(b.state.cull, CULLS);
  qa_batch_lighting *l = &b.lighting;
  if (!strcmp(l->kind, "vertex")) { l->kind = "vertex"; }
  else if (!strcmp(l->kind, "q2-model-shadow")) {
    l->kind = "q2-model-shadow";
    l->atlas_value.image = qa_wire_decoder_image(d, wire->atlas);
    for (size_t i = 0; i < l->model_shadow_light_count; i++) validate_shadow(&l->model_shadow_lights[i].shadow);
  } else if (!strcmp(l->kind, "q2-world")) {
    l->kind = "q2-world";
    if (l->atlas != NULL) { wire->decoded_atlas = *l->atlas; wire->decoded_atlas.image = qa_wire_decoder_image(d, wire->atlas); l->atlas = &wire->decoded_atlas; }
    if (l->pass != NULL && !strcmp(l->pass, "model")) l->pass = "model"; else l->pass = CHOICE(l->pass, WORLD_PASSES);
    for (size_t i = 0; i < l->light_count; i++) validate_shadow(&l->lights[i].shadow);
  } else qa_range_error("Invalid batch lighting");
  if (b.fog != NULL) {
    if (strcmp(b.fog->kind, "constant") && strcmp(b.fog->kind, "exp2")) qa_range_error("Invalid renderer fog");
    if (b.fog->effect != NULL) (void)CHOICE(b.fog->effect, FOG_EFFECTS);
  }
  if (b.texture_effect != NULL) { if (strcmp(b.texture_effect, "luminance-alpha")) qa_range_error("Invalid renderer wire variant"); b.texture_effect = "luminance-alpha"; }
  b.space = NULL; /* TS batch() rebuilds the record without space. */
  if (b.texturing_is_pair) {
    b.second_texture.environment = CHOICE(b.second_texture.environment, ENVIRONMENTS);
    b.second_texture.binding = qa_wire_decoder_binding(d, wire->second);
  }
  return b;
}
qa_execution_operation qa_wire_decoder_operation(qa_wire_decoder *d, const qa_wire_operation *wire) {
  if (wire->failure != NULL) qa_range_error("%s", wire->failure);
  qa_execution_operation op = wire->operation;
  switch (op.kind) {
  case QA_RENDER_OP_DRAW: case QA_RENDER_OP_OBJECT_OPACITY: case QA_RENDER_OP_DISABLE_PORTAL_CLIP: case QA_RENDER_OP_DEPTH_RANGE: case QA_RENDER_OP_POLYGON_OFFSET: break;
  case QA_RENDER_OP_CULL: op.cull = CHOICE(op.cull, CULLS); break;
  case QA_RENDER_OP_DEPTH_ATLAS:
    op.image = qa_wire_decoder_image(d, wire->image);
    for (size_t i = 0; i < op.pass_count; i++) for (size_t j = 0; j < op.passes[i].draw_count; j++) {
      const qa_depth_atlas_draw *draw = &op.passes[i].draws[j];
      for (size_t k = 0; k < draw->indices.count; k++) (void)wire_integer(draw->indices.values[k]);
      (void)CHOICE(draw->cull, CULLS);
    }
    break;
  case QA_RENDER_OP_SKY_SIDE: op.image = qa_wire_decoder_image(d, wire->image); break;
  case QA_RENDER_OP_SHADOW_VOLUME:
    op.white_image = qa_wire_decoder_image(d, wire->image);
    for (size_t i = 0; i < op.indices.count; i++) (void)wire_integer(op.indices.values[i]);
    break;
  case QA_RENDER_OP_SHADOW_FINISH: op.white_image = qa_wire_decoder_image(d, wire->image); break;
  case QA_RENDER_OP_Q2_FOG:
    if (op.fog.fog.kind == NULL || strcmp(op.fog.fog.kind, "q2")) qa_range_error("Invalid Q2 fog kind");
    op.fog.fog.kind = "q2";
    break;
  default: qa_range_error("Invalid renderer operation");
  }
  return op;
}
qa_render_view_state qa_wire_decoder_view_state(qa_wire_decoder *d, const qa_wire_view *view) {
  (void)d; /* TS viewState reads only its argument; the method form is kept for its callers. */
  qa_render_view_state state = view->state;
  state.has_view_projection = false;
  return state;
}
qa_execution_command qa_wire_decoder_command(qa_wire_decoder *d, const qa_wire_command *wire) {
  if (wire->failure != NULL) qa_range_error("%s", wire->failure);
  qa_execution_command o; memset(&o, 0, sizeof o);
  switch (wire->kind) {
  case QA_RENDER_COMMAND_DRAW_BUFFER: o.kind = QA_EXEC_COMMAND_DRAW_BUFFER; o.buffer = CHOICE(wire->buffer, BUFFERS); o.clear = wire->clear; return o;
  case QA_RENDER_COMMAND_SET_COLOR: o.kind = QA_EXEC_COMMAND_SET_COLOR; o.color = wire->color; return o;
  case QA_RENDER_COMMAND_IMAGE_RESOURCE: o.kind = QA_EXEC_COMMAND_IMAGE_RESOURCE; o.operation = qa_wire_decoder_image_operation(d, wire->operation); return o;
  case QA_RENDER_COMMAND_STRETCH_PIC: o.kind = QA_EXEC_COMMAND_STRETCH_PIC; o.image = qa_wire_decoder_image(d, wire->image); o.rect = wire->rect; o.uv = wire->uv; return o;
  case QA_RENDER_COMMAND_SWAP_BUFFERS:
    o.kind = QA_EXEC_COMMAND_SWAP_BUFFERS; o.captures = wire->captures; o.capture_count = wire->capture_count;
    for (size_t i = 0; i < o.capture_count; i++) (void)wire_integer(o.captures[i]);
    return o;
  case QA_RENDER_COMMAND_VIEW: o.kind = QA_EXEC_COMMAND_VIEW; return o;
  }
  qa_range_error("Invalid renderer command");
}
