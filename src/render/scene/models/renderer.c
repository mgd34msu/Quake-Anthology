/* Port of src/render/scene/models/renderer.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/renderer.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q12_model/normals.h"
#include "qa/foundation/support.h"
#include "qa/materials/color.h"
#include "qa/materials/deform.h"
#include "qa/materials/evaluate.h"
#include "qa/materials/legacy.h"
#include "qa/render/scene/models/image_path.h"
#include "qa/render/scene/models/lighting.h"
#include "qa/render/scene/models/prepare.h"
#include "qa/render/scene/models/shadedots.h"
#include "qa/render/scene/models/transform.h"
#include "qa/render/scene/skin.h"
#include "qa/foundation/js.h"

static double fr(double v) { return (double)(float)v; }
static bool bit(double flags, int32_t mask) { return (qa_int32(flags) & mask) != 0; }
static bool str_is(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static bool flags_is(const qa_scene_entity *e, const char *kind) {
  return str_is(e->flags_kind, kind);
}
static double source_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static char *dup_format(const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  size_t length = strlen(buffer);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, buffer, length + 1);
  return out;
}
static char *dup_str(const char *text) {
  if (text == NULL) return NULL;
  return dup_format("%s", text);
}

/* --- Mirrored view.ts helpers (TS owner: render/scene/view.ts) --- */
typedef struct projector {
  qa_vec4 eye_x, eye_y, eye_z;
  qa_mat4 projection;
} projector;
static projector make_projector(qa_scene_camera camera) {
  projector p;
  qa_vec3 forward = camera.axis.vectors[0], left = camera.axis.vectors[1],
          up = camera.axis.vectors[2];
  qa_vec3 neg_left = qa_scale3(left, -1), neg_forward = qa_scale3(forward, -1);
  p.eye_x = (qa_vec4){neg_left.x, neg_left.y, neg_left.z, qa_dot3(camera.origin, left)};
  p.eye_y = (qa_vec4){up.x, up.y, up.z, -qa_dot3(camera.origin, up)};
  p.eye_z = (qa_vec4){neg_forward.x, neg_forward.y, neg_forward.z,
                      qa_dot3(camera.origin, forward)};
  p.projection = camera.projection;
  return p;
}
static double proj_component(double x, double y, double z, double a, double b,
                             double c, double d) {
  return fr(fr(fr(fr(x * a) + fr(y * b)) + fr(z * c)) + d);
}
static qa_vec4 apply_projector(const projector *p, qa_vec3 point) {
  double x = fr(qa_dot3(point, (qa_vec3){p->eye_x.x, p->eye_x.y, p->eye_x.z}) + p->eye_x.w);
  double y = fr(qa_dot3(point, (qa_vec3){p->eye_y.x, p->eye_y.y, p->eye_y.z}) + p->eye_y.w);
  double z = fr(qa_dot3(point, (qa_vec3){p->eye_z.x, p->eye_z.y, p->eye_z.z}) + p->eye_z.w);
  const double *m = p->projection.values;
  return (qa_vec4){proj_component(x, y, z, m[0], m[4], m[8], m[12]),
                   proj_component(x, y, z, m[1], m[5], m[9], m[13]),
                   proj_component(x, y, z, m[2], m[6], m[10], m[14]),
                   proj_component(x, y, z, m[3], m[7], m[11], m[15])};
}
typedef struct projector_ctx {
  const projector *projector;
  bool mirror;
  const qa_model_transform *transform; /* nullable model-space point */
} projector_ctx;
static qa_vec4 identity_project(void *ctx, qa_vec3 point) {
  (void)ctx;
  return (qa_vec4){point.x, point.y, point.z, 1};
}
static qa_vec4 project_fn(void *ctx, qa_vec3 point) {
  projector_ctx *pc = ctx;
  qa_vec3 world = pc->transform != NULL
                      ? qa_model_world_point(*pc->transform, point)
                      : point;
  qa_vec4 result = apply_projector(pc->projector, world);
  if (pc->mirror) result.x = -result.x;
  return result;
}
static size_t camera_frustum(qa_scene_camera camera, qa_plane out[5]) {
  size_t count = 0;
  const int dirs[2] = {1, 2};
  for (int d = 0; d < 2; d++) {
    qa_vec3 direction = camera.axis.vectors[dirs[d]];
    double scale = camera.projection.values[d == 0 ? 0 : 5];
    for (int s = 0; s < 2; s++) {
      double sign = s == 0 ? 1 : -1;
      double inverse = 1 / hypot(1, scale);
      qa_vec3 normal = qa_add3(qa_scale3(camera.axis.vectors[0], inverse),
                               qa_scale3(direction, scale * inverse * sign));
      out[count++] = (qa_plane){normal, qa_dot3(camera.origin, normal)};
    }
  }
  /* TS order: left +/-, up +/- (same set, order as constructed) */
  if (camera.clip_is_portal) out[count++] = camera.clip_plane;
  return count;
}

/* --- Materials map --- */
typedef struct material_entry {
  char *key; /* owned */
  bool is_q3;
  char *name; /* owned, q3 */
  qa_registered_scene_material original; /* q3 */
  const qa_scene_texture *texture; /* legacy; loader-owned */
} material_entry;
typedef struct mat_state {
  material_entry *entries;
  size_t count, capacity;
  uint8_t **retained;
  size_t retained_count, retained_capacity;
  /* Individually allocated: registered textures keep views into them. */
  qa_model_image_scratch **scratches;
  size_t scratch_count, scratch_capacity;
  /* world.materialContext results the prepared batches point into. */
  qa_world_material_context **contexts;
  size_t context_count, context_capacity;
} mat_state;
static mat_state *state_of(qa_scene_model_renderer *r) { return r->materials; }
static void retain(qa_scene_model_renderer *r, uint8_t *bytes) {
  mat_state *state = state_of(r);
  if (bytes == NULL || state == NULL) return;
  if (state->retained_count == state->retained_capacity) {
    state->retained_capacity = state->retained_capacity ? state->retained_capacity * 2 : 16;
    state->retained = realloc(state->retained, state->retained_capacity * sizeof *state->retained);
    if (state->retained == NULL) return;
  }
  state->retained[state->retained_count++] = bytes;
}
static qa_model_image_scratch *new_scratch(qa_scene_model_renderer *r) {
  mat_state *state = state_of(r);
  if (state->scratch_count == state->scratch_capacity) {
    size_t capacity = state->scratch_capacity ? state->scratch_capacity * 2 : 16;
    qa_model_image_scratch **grown = realloc(state->scratches, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    state->scratches = grown;
    state->scratch_capacity = capacity;
  }
  qa_model_image_scratch *scratch = calloc(1, sizeof *scratch);
  if (scratch == NULL) qa_range_error("Out of memory");
  state->scratches[state->scratch_count++] = scratch;
  return scratch;
}
static qa_world_material_context *new_world_context(qa_scene_model_renderer *r) {
  mat_state *state = state_of(r);
  if (state->context_count == state->context_capacity) {
    size_t capacity = state->context_capacity ? state->context_capacity * 2 : 16;
    qa_world_material_context **grown = realloc(state->contexts, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    state->contexts = grown;
    state->context_capacity = capacity;
  }
  qa_world_material_context *context = calloc(1, sizeof *context);
  if (context == NULL) qa_range_error("Out of memory");
  state->contexts[state->context_count++] = context;
  return context;
}

qa_scene_texture_loader *qa_model_render_provider_textures(const qa_model_render_provider *provider) {
  return provider->textures_getter != NULL ? provider->textures_getter(provider->getter_ctx)
                                           : provider->textures;
}
/* TS this.provider.textures / this.provider.modelPolicy (getter or value). */
static qa_scene_texture_loader *provider_textures(const qa_scene_model_renderer *r) {
  return qa_model_render_provider_textures(&r->provider);
}
static bool provider_model_policy(const qa_scene_model_renderer *r,
                                  qa_model_replacement_policy *out) {
  if (r->provider.model_policy_getter != NULL) {
    *out = r->provider.model_policy_getter(r->provider.getter_ctx);
    return true;
  }
  *out = r->provider.model_policy;
  return r->provider.has_model_policy;
}

static const char *image_kind_name(qa_model_image_selection_kind kind) {
  switch (kind) {
    case QA_MODEL_IMAGE_EXTERNAL: return "external";
    case QA_MODEL_IMAGE_INDEXED: return "indexed";
    case QA_MODEL_IMAGE_WHITE: return "white";
    case QA_MODEL_IMAGE_DEFAULT: return "default";
  }
  return "";
}
/* q1PlayerTranslation(top, bottom): RangeError unless both are integers in 0..13
 * (checked before any int64 cast; 1.5 must not truncate to a valid colour). */
static uint8_t *player_translation(const qa_model_source_options *options) {
  double top = options->player_color_top, bottom = options->player_color_bottom;
  if (!(top >= 0 && top <= 13 && top == floor(top) && bottom >= 0 && bottom <= 13 && bottom == floor(bottom)))
    qa_range_error("Q1 player colors must be in 0..13");
  return qa_q1_player_translation((int64_t)top, (int64_t)bottom);
}
static char *material_key(const qa_resolved_resource_reference *resource,
                          const qa_decoded_model *model,
                          const qa_model_image_selection *image,
                          const qa_model_source_options *options) {
  bool translated = image->kind == QA_MODEL_IMAGE_INDEXED ||
                    (model->kind == QA_DECODED_MODEL_MD5 &&
                     str_is(model->as.md5->skin_selection.kind, "q1-mdl-replacement"));
  char translation[96] = "";
  if (translated && options->has_player_colors)
    (void)snprintf(translation, sizeof translation, "%s:%s",
                   QA_JS_NUMBER(options->player_color_top),
                   QA_JS_NUMBER(options->player_color_bottom));
  const char *middle = (image->kind == QA_MODEL_IMAGE_EXTERNAL ||
                        image->kind == QA_MODEL_IMAGE_INDEXED)
                           ? (image->name != NULL ? image->name : "")
                           : image->kind == QA_MODEL_IMAGE_DEFAULT
                                 ? (image->reason != NULL ? image->reason : "")
                                 : "";
  /* TS joins with NUL bytes; \x01 is the C separator. */
  return dup_format("%s\x01%s\x01%s\x01%s", resource->id,
                    image_kind_name(image->kind), middle, translation);
}
static material_entry *find_material(qa_scene_model_renderer *r, const char *key) {
  mat_state *state = state_of(r);
  if (state == NULL) return NULL;
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->entries[i].key, key) == 0) return &state->entries[i];
  return NULL;
}
static void put_material(qa_scene_model_renderer *r, char *key,
                         material_entry entry) {
  mat_state *state = state_of(r);
  entry.key = key;
  if (state->count == state->capacity) {
    state->capacity = state->capacity ? state->capacity * 2 : 16;
    state->entries = realloc(state->entries, state->capacity * sizeof *state->entries);
    if (state->entries == NULL) { free(key); free(entry.name); return; }
  }
  state->entries[state->count++] = entry;
  r->material_count = state->count;
  r->material_capacity = state->capacity;
}

bool qa_scene_model_renderer_init(qa_scene_model_renderer *renderer,
                                  qa_model_render_provider provider,
                                  qa_world_scene *world) {
  memset(renderer, 0, sizeof *renderer);
  renderer->provider = provider;
  renderer->world = world;
  renderer->materials = calloc(1, sizeof(mat_state));
  renderer->textures = provider_textures(renderer);
  if (renderer->materials == NULL) return false;
  return qa_model_light_sampler_init(&renderer->lighting, world);
}
void qa_scene_model_renderer_destroy(qa_scene_model_renderer *renderer) {
  mat_state *state = state_of(renderer);
  if (state != NULL) {
    for (size_t i = 0; i < state->count; i++) {
      free(state->entries[i].key);
      free(state->entries[i].name);
    }
    free(state->entries);
    for (size_t i = 0; i < state->retained_count; i++) free(state->retained[i]);
    free(state->retained);
    for (size_t i = 0; i < state->scratch_count; i++) free(state->scratches[i]);
    free(state->scratches);
    for (size_t i = 0; i < state->context_count; i++) free(state->contexts[i]);
    free(state->contexts);
    free(state);
  }
  qa_model_light_sampler_destroy(&renderer->lighting);
  memset(renderer, 0, sizeof *renderer);
}
static void reset_if_loader_changed(qa_scene_model_renderer *r) {
  if (r->textures != provider_textures(r)) {
    mat_state *state = state_of(r);
    for (size_t i = 0; i < state->count; i++) {
      free(state->entries[i].key);
      free(state->entries[i].name);
    }
    state->count = 0;
    r->material_count = 0;
    r->textures = provider_textures(r);
  }
}

/* --- selections (TS selections()) --- */
typedef struct selection_list {
  qa_model_image_selection *items; size_t count, capacity;
} selection_list;
static void sel_push(selection_list *list, qa_model_image_selection item) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    list->items = realloc(list->items, list->capacity * sizeof *list->items);
    if (list->items == NULL) return;
  }
  list->items[list->count++] = item;
}
static void sel_external(selection_list *list, const char *name) {
  qa_model_image_selection item = {0};
  item.kind = QA_MODEL_IMAGE_EXTERNAL;
  item.name = name;
  sel_push(list, item);
}
static void sel_free(selection_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i].owned_name);
  free(list->items);
  memset(list, 0, sizeof *list);
}
static void sel_release(void *list) { sel_free(list); }
/* frames<T>: element pointers of a TimedFrames. */
static const void **timed_elements(const qa_timed_frames *frames, size_t *out_count) {
  if (!frames->is_group) {
    const void **items = calloc(1, sizeof *items);
    if (items != NULL) items[0] = frames->single_frame;
    *out_count = 1;
    return items;
  }
  const void **items = calloc(frames->frame_count ? frames->frame_count : 1, sizeof *items);
  if (items != NULL)
    for (size_t i = 0; i < frames->frame_count; i++) items[i] = frames->frames[i].frame;
  *out_count = frames->frame_count;
  return items;
}

static selection_list selections_of(const qa_resolved_resource_reference *resource,
                                    const qa_decoded_model *model,
                                    const qa_model_source_options *options) {
  selection_list list = {0};
  qa_model_image_selection white = {0};
  white.kind = QA_MODEL_IMAGE_WHITE;
  sel_push(&list, white);
  qa_model_image_selection missing = {0};
  missing.kind = QA_MODEL_IMAGE_DEFAULT;
  missing.reason = "missing-skin-surface";
  sel_push(&list, missing);
  qa_model_image_selection noskin = {0};
  noskin.kind = QA_MODEL_IMAGE_DEFAULT;
  noskin.reason = "no-skin";
  sel_push(&list, noskin);
  if (options->custom_shader != NULL) sel_external(&list, options->custom_shader);
  if (options->custom_skin != NULL)
    for (size_t i = 0; i < options->custom_skin_count; i++)
      sel_external(&list, options->custom_skin[i].shader);
  switch (model->kind) {
    case QA_DECODED_MODEL_Q1_ALIAS: {
      const qa_q1_alias_model *mdl = model->as.q1_alias;
      if (options->has_indexed_skin) {
        qa_model_image_selection item = {0};
        item.kind = QA_MODEL_IMAGE_INDEXED;
        item.name = options->indexed_skin.name;
        item.width = options->indexed_skin.width;
        item.height = options->indexed_skin.height;
        item.pixels = options->indexed_skin.pixels;
        item.has_transparent_index = false;
        item.fullbright = true;
        sel_push(&list, item);
        break;
      }
      for (size_t skin = 0; skin < mdl->skin_count; skin++) {
        size_t count = 0;
        const void **elements = timed_elements(&mdl->skins[skin], &count);
        for (size_t frame = 0; frame < count; frame++) {
          const qa_byte_view *pixels = elements[frame];
          qa_model_image_selection item = {0};
          item.kind = QA_MODEL_IMAGE_INDEXED;
          item.owned_name = dup_format("%s:skin:%zu:%zu", resource->id, skin, frame);
          item.name = item.owned_name;
          item.width = mdl->skin_width;
          item.height = mdl->skin_height;
          item.pixels = pixels != NULL ? *pixels : (qa_byte_view){0};
          item.has_transparent_index = false;
          item.fullbright = true;
          sel_push(&list, item);
        }
        free((void *)elements);
      }
      break;
    }
    case QA_DECODED_MODEL_Q1_SPRITE: {
      const qa_q1_sprite_model *spr = model->as.q1_sprite;
      for (size_t frame = 0; frame < spr->frame_count; frame++) {
        size_t count = 0;
        const void **elements = timed_elements(&spr->frames[frame], &count);
        for (size_t sub = 0; sub < count; sub++) {
          const qa_sprite_frame *sprite = elements[sub];
          if (sprite == NULL) continue;
          qa_model_image_selection item = {0};
          item.kind = QA_MODEL_IMAGE_INDEXED;
          item.owned_name = dup_format("%s:frame:%zu:%zu", resource->id, frame, sub);
          item.name = item.owned_name;
          item.width = sprite->width;
          item.height = sprite->height;
          item.pixels = sprite->pixels;
          item.has_transparent_index = true;
          item.transparent_index = 255;
          item.fullbright = true;
          sel_push(&list, item);
        }
        free((void *)elements);
      }
      break;
    }
    case QA_DECODED_MODEL_Q2_ALIAS:
      for (size_t i = 0; i < model->as.q2_alias->skins.count; i++)
        sel_external(&list, model->as.q2_alias->skins.values[i]);
      break;
    case QA_DECODED_MODEL_Q2_SPRITE:
      for (size_t i = 0; i < model->as.q2_sprite->frame_count; i++)
        sel_external(&list, model->as.q2_sprite->frames[i].image);
      break;
    case QA_DECODED_MODEL_Q3_MESH: {
      const qa_q3_mesh_model *fallback[1] = {model->as.q3_mesh};
      const qa_q3_mesh_model *const *lods = options->q3_lods != NULL ? options->q3_lods : fallback;
      size_t lod_count = options->q3_lods != NULL ? options->q3_lod_count : 1;
      for (size_t li = 0; li < lod_count; li++) {
        if (lods[li] == NULL) continue;
        for (size_t si = 0; si < lods[li]->surface_count; si++)
          for (size_t hi = 0; hi < lods[li]->surfaces[si].shaders.count; hi++)
            sel_external(&list, lods[li]->surfaces[si].shaders.values[hi]);
      }
      break;
    }
    case QA_DECODED_MODEL_MD4:
      for (size_t li = 0; li < model->as.md4->lod_count; li++)
        for (size_t si = 0; si < model->as.md4->lods[li].surface_count; si++)
          sel_external(&list, model->as.md4->lods[li].surfaces[si].shader);
      break;
    case QA_DECODED_MODEL_MD5: {
      const qa_md5_model *md5 = model->as.md5;
      if (str_is(md5->skin_selection.kind, "q1-mdl-replacement")) {
        for (size_t mi = 0; mi < md5->skin_selection.mesh_skin_group_row_count; mi++) {
          const qa_timed_frames *groups = md5->skin_selection.mesh_skin_group_rows[mi];
          size_t group_count = md5->skin_selection.mesh_skin_group_row_counts[mi];
          for (size_t gi = 0; gi < group_count; gi++) {
            size_t count = 0;
            const void **elements = timed_elements(&groups[gi], &count);
            for (size_t ei = 0; ei < count; ei++) {
              const char *const *name = elements[ei];
              qa_model_image_selection item = {0};
              item.kind = QA_MODEL_IMAGE_EXTERNAL;
              item.owned_name = dup_format("%s.lmp", name != NULL ? *name : "");
              item.name = item.owned_name;
              sel_push(&list, item);
            }
            free((void *)elements);
          }
        }
      } else if (str_is(md5->skin_selection.kind, "q2-md2-replacement")) {
        for (size_t i = 0; i < md5->skin_selection.skins.count; i++)
          sel_external(&list, md5->skin_selection.skins.values[i]);
      } else {
        for (size_t mi = 0; mi < md5->mesh_count; mi++)
          sel_external(&list, md5->meshes[mi].shader);
      }
      break;
    }
    case QA_DECODED_MODEL_BRUSH_MODEL: break;
  }
  return list;
}

static const qa_scene_texture *indexed_texture(
    qa_scene_model_renderer *r, const qa_decoded_model *model,
    const qa_model_image_selection *selection,
    const qa_model_source_options *options) {
  const qa_palette *palette = r->provider.palette;
  if (palette == NULL)
    qa_raise("Error", 0, 0, "Indexed model %s has no content palette", selection->name);
  qa_model_image_selection image = *selection;
  uint8_t *flooded = NULL;
  if (model->kind == QA_DECODED_MODEL_Q1_ALIAS) {
    /* floodSkin (render/scene/skin.ts): a palette entry past the palette is undefined, not black. */
    flooded = qa_flood_skin(selection->pixels.bytes, selection->pixels.length, selection->width,
                            selection->height, *palette, NULL);
    if (flooded == NULL) qa_range_error("Out of memory");
    retain(r, flooded);
    image.pixels = (qa_byte_view){flooded, selection->pixels.length};
  }
  qa_byte_view translation = {0};
  const qa_byte_view *translation_ptr = NULL;
  if (options->has_player_colors) {
    uint8_t *bytes = player_translation(options);
    retain(r, bytes);
    translation = (qa_byte_view){bytes, 256};
    translation_ptr = &translation;
  }
  qa_model_image_scratch *scratch = new_scratch(r);
  qa_render_image render_image = qa_model_image(&image, *palette, translation_ptr, scratch);
  char *name = options->has_player_colors
                   ? dup_format("%s:%s:%s", selection->name,
                                QA_JS_NUMBER(options->player_color_top),
                                QA_JS_NUMBER(options->player_color_bottom))
                   : NULL;
  qa_texture_sampling sampling = {"repeat", "linear"};
  const qa_scene_texture *result = qa_scene_texture_loader_register(
      provider_textures(r), name != NULL ? name : selection->name, render_image,
      &sampling, NULL, false, 0, 0);
  /* Registered name is retained by the loader in TS; keep ours too. */
  if (name != NULL) retain(r, (uint8_t *)name);
  return result;
}

static const qa_scene_texture *external_texture(
    qa_scene_model_renderer *r, const qa_decoded_model *model,
    const char *name, const qa_model_source_options *options) {
  bool sprite = model->kind == QA_DECODED_MODEL_Q2_SPRITE;
  char *path = qa_model_image_path(name);
  if (path == NULL) qa_range_error("Out of memory");
  qa_scene_texture_load_options load = {0};
  load.has_mipmap = true;
  load.mipmap = !sprite;
  load.family = qa_game_family_name(r->provider.family);
  load.usage = sprite ? "sprite" : "skin";
  const qa_scene_texture *texture = qa_scene_texture_loader_load(provider_textures(r), path, &load);
  free(path);
  if (texture == NULL) texture = provider_textures(r)->missing;
  if (model->kind == QA_DECODED_MODEL_MD5 &&
      str_is(model->as.md5->skin_selection.kind, "q1-mdl-replacement") &&
      options->has_player_colors && texture->content.kind == QA_RENDER_IMAGE_INDEXED8) {
    uint8_t *bytes = player_translation(options);
    retain(r, bytes);
    qa_render_image content = texture->content;
    qa_model_image_scratch *scratch = new_scratch(r);
    scratch->translation = (qa_byte_view){bytes, 256};
    content.translation = &scratch->translation;
    char *registered = dup_format("%s:%s:%s", name,
                                  QA_JS_NUMBER(options->player_color_top),
                                  QA_JS_NUMBER(options->player_color_bottom));
    qa_texture_sampling sampling = {"repeat", "linear-mipmap-nearest"};
    const qa_scene_texture *result = qa_scene_texture_loader_register(
        provider_textures(r), registered, content, &sampling, &texture->image, false, 0, 0);
    retain(r, (uint8_t *)registered);
    return result;
  }
  return texture;
}

/* TS load() returns Promise.reject for a deferred cinematic while the other
 * selections' loads still run; the preload rejects after all of them start.
 * The first deferred name is kept here and raised once the loops finish. */
typedef struct deferred_cinematic { char *name; } deferred_cinematic;
static void deferred_release(void *value) {
  deferred_cinematic *deferred = value;
  free(deferred->name);
  deferred->name = NULL;
}
static void raise_deferred(deferred_cinematic *deferred) {
  if (deferred->name == NULL) return;
  char text[512];
  (void)snprintf(text, sizeof text, "%s", deferred->name);
  deferred_release(deferred);
  qa_raise("Error", 0, 0, "Model cinematic deferred until use: %s", text);
}
static void load_material(qa_scene_model_renderer *r,
                          const qa_resolved_resource_reference *resource,
                          const qa_decoded_model *model,
                          const qa_model_image_selection *selection,
                          const qa_model_source_options *options,
                          bool allow_cinematics, deferred_cinematic *deferred) {
  if (!allow_cinematics && r->provider.family == QA_GAME_FAMILY_Q3 &&
      (selection->kind == QA_MODEL_IMAGE_EXTERNAL ||
       selection->kind == QA_MODEL_IMAGE_DEFAULT)) {
    const char *name = selection->kind == QA_MODEL_IMAGE_EXTERNAL ? selection->name : "*default";
    /* TS this.world.shaders.registrations.owner.remapDefinition(name). */
    qa_scene_remap_definition remap;
    bool has_remap = qa_scene_material_registrations_remap_definition(
        r->world->shaders->registrations->owner, name, &remap);
    const char *effective = has_remap && remap.replacement != NULL ? remap.replacement : name;
    qa_scene_shader_registry *registry = has_remap && remap.source != NULL
                                             ? remap.source : r->provider.shaders;
    if (qa_scene_shader_registry_has_cinematic(registry, effective)) {
      if (deferred->name == NULL) deferred->name = dup_str(effective);
      return;
    }
  }
  char *key = material_key(resource, model, selection, options);
  if (key == NULL) return;
  if (find_material(r, key) != NULL) { free(key); return; }
  material_entry entry = {0};
  if (r->provider.family == QA_GAME_FAMILY_Q3 &&
      (selection->kind == QA_MODEL_IMAGE_EXTERNAL ||
       selection->kind == QA_MODEL_IMAGE_DEFAULT)) {
    const char *name = selection->kind == QA_MODEL_IMAGE_EXTERNAL ? selection->name : "*default";
    entry.is_q3 = true;
    entry.name = dup_str(name);
    entry.original = qa_scene_shader_registry_register(r->provider.shaders, name, NULL);
    put_material(r, key, entry);
    return;
  }
  const qa_scene_texture *texture = NULL;
  switch (selection->kind) {
    case QA_MODEL_IMAGE_WHITE: texture = provider_textures(r)->white; break;
    case QA_MODEL_IMAGE_DEFAULT: texture = provider_textures(r)->missing; break;
    case QA_MODEL_IMAGE_INDEXED: texture = indexed_texture(r, model, selection, options); break;
    case QA_MODEL_IMAGE_EXTERNAL: texture = external_texture(r, model, selection->name, options); break;
  }
  entry.is_q3 = false;
  entry.texture = texture;
  put_material(r, key, entry);
}

static void preload_selections(qa_scene_model_renderer *r,
                               const qa_resolved_resource_reference *resource,
                               const qa_decoded_model *model,
                               const qa_model_source_options *options,
                               bool allow_cinematics, deferred_cinematic *deferred) {
  selection_list list = selections_of(resource, model, options);
  /* A load can raise (TS rejects the preload); free the list then. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, sel_release, &list);
  for (size_t i = 0; i < list.count; i++)
    load_material(r, resource, model, &list.items[i], options, allow_cinematics, deferred);
  qa_cleanup_pop(&guard);
  sel_free(&list);
}

static void preload_visit(qa_scene_model_renderer *r, const qa_scene_entity *entity,
                          qa_model_source_options_fn options, void *options_ctx) {
  qa_model_source_options source = options != NULL
                                       ? options(options_ctx, entity)
                                       : (qa_model_source_options){0};
  preload_selections(r, &entity->resource, &entity->model, &source, true, NULL);
  bool skip = entity->model.kind == QA_DECODED_MODEL_Q1_ALIAS && source.has_indexed_skin;
  qa_scene_entity replacement;
  if (!skip && qa_replacement_entity(entity, &replacement))
    preload_selections(r, &replacement.resource, &replacement.model, &source, true, NULL);
  for (size_t i = 0; i < entity->attachment_count; i++)
    preload_visit(r, entity->attachments[i].entity, options, options_ctx);
}

void qa_scene_model_renderer_preload(qa_scene_model_renderer *renderer,
                                     const qa_scene_entity *entities,
                                     size_t entity_count,
                                     qa_model_source_options_fn options,
                                     void *options_ctx) {
  reset_if_loader_changed(renderer);
  for (size_t i = 0; i < entity_count; i++)
    preload_visit(renderer, &entities[i], options, options_ctx);
}

void qa_scene_model_renderer_preload_model(
    qa_scene_model_renderer *renderer,
    const qa_resolved_resource_reference *resource,
    const qa_decoded_model *model, qa_model_source_options options,
    bool allow_cinematics) {
  reset_if_loader_changed(renderer);
  deferred_cinematic deferred = {NULL};
  qa_cleanup guard;
  qa_cleanup_push(&guard, deferred_release, &deferred);
  preload_selections(renderer, resource, model, &options, allow_cinematics, &deferred);
  if ((model->kind == QA_DECODED_MODEL_Q1_ALIAS ||
       model->kind == QA_DECODED_MODEL_Q2_ALIAS) &&
      !(model->kind == QA_DECODED_MODEL_Q1_ALIAS && options.has_indexed_skin)) {
    const qa_model_replacement *replacement = model->replacement;
    if (replacement != NULL) {
      qa_decoded_model replacement_model = *model;
      replacement_model.kind = QA_DECODED_MODEL_MD5;
      replacement_model.as.md5 = &replacement->model;
      replacement_model.replacement = NULL;
      preload_selections(renderer, &replacement->resource, &replacement_model,
                         &options, allow_cinematics, &deferred);
    }
  }
  qa_cleanup_pop(&guard);
  raise_deferred(&deferred);
}

/* --- prepare() vertex lighting --- */
static bool same_value_zero(double a, double b) {
  return a == b || (isnan(a) && isnan(b));
}
static long alias_normal_index(qa_vec3 normal) {
  size_t count = 0;
  const qa_vec3 *table = qa_alias_normal_table(&count);
  for (size_t i = 0; i < count; i++)
    if (same_value_zero(table[i].x, normal.x) &&
        same_value_zero(table[i].y, normal.y) &&
        same_value_zero(table[i].z, normal.z))
      return (long)i;
  return -1;
}
static char *key_number(char *buffer, size_t size, double value) {
  if (value == 0 && signbit(value)) (void)snprintf(buffer, size, "-0");
  else (void)snprintf(buffer, size, "%s", QA_JS_NUMBER(value));
  return buffer;
}

typedef struct prepare_call prepare_call;
typedef struct illum {
  prepare_call *call;
  qa_scene_entity entity;
  qa_model_source_options source;
  bool has_light;
  qa_vec3 light;
  bool has_entity_lighting;
  qa_entity_lighting entity_lighting;
  bool has_basis;
  double yaw;
  long row;
  bool has_direction;
  qa_vec3 direction;
  bool has_last;
  qa_vec3 last_normal, last_light;
  qa_vec3 indexed[256];
  bool indexed_set[256];
  struct { qa_vec3 normal, light; } vertex_lights[128];
  size_t vertex_light_count;
  char *key; /* owned retention key or NULL */
} illum;

struct prepare_call {
  qa_scene_model_renderer *renderer;
  const qa_world_view_input *input;
  qa_world_view_input lighting_input_storage;
  const qa_world_view_input *lighting_input;
  qa_surface_dynamic_light *lighting_lights; /* owned */
  double time;
  const qa_q2_fragment_light *cone_lights;
  size_t cone_count;
  qa_q2_fragment_light *cone_owned;
  bool q2_lighting;
  illum **states;
  size_t state_count, state_capacity;
  /* light cache records (entity snapshot -> light) */
  struct { qa_scene_entity entity; qa_vec3 light; } *lights;
  size_t light_count, light_capacity;
  /* options cache by entity pointer */
  struct { const qa_scene_entity *entity; qa_model_source_options options; } *option_cache;
  size_t option_count, option_capacity;
  qa_model_source_options_fn user_options;
  void *user_options_ctx;
  projector projector;
};

static void record_light(prepare_call *call, const qa_scene_entity *entity,
                         qa_vec3 light) {
  if (call->light_count == call->light_capacity) {
    call->light_capacity = call->light_capacity ? call->light_capacity * 2 : 16;
    call->lights = realloc(call->lights, call->light_capacity * sizeof *call->lights);
    if (call->lights == NULL) return;
  }
  call->lights[call->light_count].entity = *entity;
  call->lights[call->light_count].light = light;
  call->light_count++;
}
static bool find_light(prepare_call *call, const qa_scene_entity *entity,
                       qa_vec3 *out) {
  for (size_t i = 0; i < call->light_count; i++) {
    const qa_scene_entity *candidate = &call->lights[i].entity;
    if (candidate->resource.id == entity->resource.id &&
        candidate->transform.origin.x == entity->transform.origin.x &&
        candidate->transform.origin.y == entity->transform.origin.y &&
        candidate->transform.origin.z == entity->transform.origin.z &&
        candidate->skin == entity->skin &&
        candidate->previous_origin.x == entity->previous_origin.x &&
        candidate->previous_origin.y == entity->previous_origin.y &&
        candidate->previous_origin.z == entity->previous_origin.z) {
      *out = call->lights[i].light;
      return true;
    }
  }
  return false;
}

static qa_vec3 illuminate(void *ctx, qa_vec3 normal, qa_vec3 position, double corner);

static bool previous_normal_index_of(illum *st, double corner, double *out) {
  qa_scene_entity *entity = &st->entity;
  if (!(st->call->renderer->provider.family == QA_GAME_FAMILY_Q1 &&
        entity->model.kind == QA_DECODED_MODEL_Q1_ALIAS && entity->pose.is_frame))
    return false;
  const qa_q1_alias_model *mdl = entity->model.as.q1_alias;
  double triangle_index = trunc(corner / 3);
  if (!(triangle_index >= 0) || triangle_index >= (double)mdl->triangle_count)
    return false;
  double vertex = mdl->triangles[(size_t)triangle_index].vertices[(size_t)fmod(corner, 3)];
  double old_index = entity->pose.previous_frame;
  if (!(old_index >= 0) || trunc(old_index) != old_index ||
      old_index >= (double)mdl->frame_count)
    return false;
  const qa_q1_alias_frame *old_frame = qa_q1_sample_alias_frame(
      &mdl->frames[(size_t)old_index], st->call->time,
      st->source.has_sync_base ? st->source.sync_base : 0);
  if (old_frame == NULL || !(vertex >= 0) || trunc(vertex) != vertex ||
      vertex >= (double)old_frame->compressed_vertex_count)
    return false;
  *out = old_frame->compressed_vertices[(size_t)vertex].normal_index;
  return true;
}

static qa_vec3 illuminate(void *ctx, qa_vec3 normal, qa_vec3 position, double corner) {
  (void)position;
  illum *st = ctx;
  prepare_call *call = st->call;
  qa_scene_model_renderer *r = call->renderer;
  qa_scene_entity *entity = &st->entity;
  if (r->provider.family == QA_GAME_FAMILY_Q3) return (qa_vec3){1, 1, 1};
  double previous_index = 0;
  bool has_previous = previous_normal_index_of(st, corner, &previous_index);
  if (r->provider.family == QA_GAME_FAMILY_Q1 &&
      r->world->map.kind == QA_DECODED_WORLD_Q3) {
    if (!st->has_entity_lighting) {
      st->entity_lighting = qa_model_light_sampler_entity_lighting(
          &r->lighting, entity, call->input, false);
      st->has_entity_lighting = true;
    }
    qa_vec3 current = qa_diffuse_color(normal, st->entity_lighting);
    qa_vec3 color = current;
    if (has_previous && entity->pose.is_frame) {
      size_t table_count = 0;
      const qa_vec3 *table = qa_alias_normal_table(&table_count);
      if (previous_index >= 0 && trunc(previous_index) == previous_index &&
          previous_index < (double)table_count) {
        qa_vec3 previous = table[(size_t)previous_index];
        color = qa_add3(qa_scale3(current, 1 - entity->pose.back_lerp),
                        qa_scale3(qa_diffuse_color(previous, st->entity_lighting),
                                  entity->pose.back_lerp));
      }
    }
    return qa_scale3(color, 1.0 / 255);
  }
  bool skinned = entity->model.kind == QA_DECODED_MODEL_MD5;
  bool use_vertex_lights = r->provider.family == QA_GAME_FAMILY_Q2 && !skinned;
  if (!has_previous && st->has_last &&
      memcmp(&normal, &st->last_normal, sizeof normal) == 0)
    return st->last_light;
  long normal_index = alias_normal_index(normal);
  if (normal_index >= 0 && normal_index < 256 && st->indexed_set[normal_index]) {
    st->last_normal = normal;
    st->last_light = st->indexed[normal_index];
    st->has_last = true;
    return st->last_light;
  }
  if (use_vertex_lights)
    for (size_t i = 0; i < st->vertex_light_count; i++)
      if (memcmp(&st->vertex_lights[i].normal, &normal, sizeof normal) == 0) {
        st->last_normal = normal;
        st->last_light = st->vertex_lights[i].light;
        st->has_last = true;
        return st->last_light;
      }
  if (!st->has_light) {
    qa_vec3 sampled = qa_model_light_sampler_sample(
                          &r->lighting, entity->transform.origin,
                          call->lighting_input, call->q2_lighting).color;
    if (r->provider.family == QA_GAME_FAMILY_Q2) {
      st->light = qa_q2_alias_light(
          flags_is(entity, "q2") ? entity->flags_bits : 0, sampled, call->time,
          false, st->source.has_infrared && st->source.infrared);
    } else if (r->world->map.kind == QA_DECODED_WORLD_Q2 &&
               !(st->source.has_view_model && st->source.view_model)) {
      st->light = sampled;
    } else {
      qa_vec3 static_light = r->world->map.kind == QA_DECODED_WORLD_Q2
                                 ? qa_model_light_sampler_sample(
                                       &r->lighting, entity->transform.origin,
                                       call->lighting_input, false).color
                                 : sampled;
      qa_vec3 values = {static_light.x, static_light.y, static_light.z};
      qa_vec3 totals = sampled;
      double channels[3];
      double value_arr[3] = {values.x, values.y, values.z};
      double total_arr[3] = {totals.x, totals.y, totals.z};
      for (int ci = 0; ci < 3; ci++) {
        double value = value_arr[ci], total = total_arr[ci];
        double ambient = value * 255, shade = ambient;
        if (st->source.has_view_model && st->source.view_model && ambient < 24)
          ambient = shade = 24;
        if (r->world->map.kind == QA_DECODED_WORLD_Q2) {
          double amount = (total - value) * 255;
          ambient += amount;
          shade += amount;
        }
        if (r->world->map.kind != QA_DECODED_WORLD_Q2) { /* TS input.lights ?? [] */
          for (size_t li = 0; li < call->input->light_count; li++) {
            qa_vec3 difference = qa_sub3(entity->transform.origin,
                                         call->input->lights[li].origin);
            double amount = call->input->lights[li].radius -
                            qa_js_hypot3(difference.x, difference.y, difference.z);
            if (amount > 0) { ambient += amount; shade += amount; }
          }
        }
        ambient = qa_js_min(128, ambient);
        shade = qa_js_min(shade, 192 - ambient);
        if (((st->source.has_player && st->source.player) ||
             str_is(entity->resource.requested_path, "progs/player.mdl")) && ambient < 8)
          shade = 8;
        if (str_is(entity->resource.requested_path, "progs/flame.mdl") ||
            str_is(entity->resource.requested_path, "progs/flame2.mdl"))
          shade = 256;
        channels[ci] = shade / 200 * ((st->source.has_overbright_models &&
                                        !st->source.overbright_models) ? 1 : 2);
      }
      st->light = (qa_vec3){channels[0], channels[1], channels[2]};
    }
    st->has_light = true;
    record_light(call, entity, st->light);
  }
  if (flags_is(entity, "q2")) {
    qa_vec3 shell;
    if (qa_q2_shell_color(entity->flags_bits, &shell)) return st->light;
  }
  if (!st->has_basis) {
    st->yaw = atan2(entity->transform.axis.vectors[0].y,
                    entity->transform.axis.vectors[0].x);
    st->row = (long)qa_int32(trunc(st->yaw * 16 / (2 * acos(-1.0)))) & 15;
    st->has_basis = true;
    st->has_direction = false;
  }
  bool has_shade = normal_index >= 0;
  double shade = has_shade ? (double)qa_r_avertexnormal_dots[st->row * 256 + normal_index] : 0;
  if (has_previous && entity->pose.is_frame && has_shade) {
    double old_shade = shade;
    if (previous_index >= 0 && trunc(previous_index) == previous_index &&
        previous_index < (double)256)
      old_shade = (double)qa_r_avertexnormal_dots[st->row * 256 + (size_t)previous_index];
    shade = shade * (1 - entity->pose.back_lerp) + old_shade * entity->pose.back_lerp;
  }
  if (!has_shade) {
    if (!st->has_direction) {
      st->direction = qa_normalize3(
          (qa_vec3){cos(-st->yaw), sin(-st->yaw), 1});
      st->has_direction = true;
    }
    double d = qa_dot3(normal, st->direction);
    shade = 1 + (d < 0 ? d * 0.3 : d);
  }
  qa_vec3 result = qa_scale3(st->light, shade);
  if (use_vertex_lights) {
    if (normal_index >= 0 && normal_index < 256) {
      st->indexed[normal_index] = result;
      st->indexed_set[normal_index] = true;
    } else if (st->vertex_light_count < 128) {
      st->vertex_lights[st->vertex_light_count].normal = normal;
      st->vertex_lights[st->vertex_light_count].light = result;
      st->vertex_light_count++;
    }
  }
  if (!has_previous) {
    st->last_normal = normal;
    st->last_light = result;
    st->has_last = true;
  }
  return result;
}

static qa_prepared_vertex_lighting prepare_vertex_lighting_of(
    void *ctx, const qa_scene_entity *entity, qa_model_source_options source) {
  prepare_call *call = ctx;
  qa_prepared_vertex_lighting out = {0};
  /* TS caches per entity object; each C entity preparation calls this
   * once, so a fresh state per call reproduces the values. */
  illum *st = calloc(1, sizeof *st);
  if (st == NULL) return out;
  st->call = call;
  st->entity = *entity;
  st->source = source;
  if (call->state_count == call->state_capacity) {
    call->state_capacity = call->state_capacity ? call->state_capacity * 2 : 16;
    call->states = realloc(call->states, call->state_capacity * sizeof *call->states);
    if (call->states == NULL) { free(st); return out; }
  }
  call->states[call->state_count++] = st;
  char *key = NULL;
  qa_scene_model_renderer *r = call->renderer;
  if (r->provider.family == QA_GAME_FAMILY_Q3) key = dup_str("q3");
  else if (r->provider.family == QA_GAME_FAMILY_Q2) {
    double bits = flags_is(entity, "q2") ? entity->flags_bits : 0;
    qa_vec3 shell;
    if (!qa_q2_shell_color(bits, &shell) && !bit(bits, 512)) {
      (void)illuminate(st, (qa_vec3){0, 0, 1}, entity->transform.origin, 0);
      if (st->has_light && st->has_basis) {
        char bx[64], by[64], bz[64], byaw[64];
        key = dup_format("q2|%s,%s,%s|%ld|%s",
                         key_number(bx, sizeof bx, st->light.x),
                         key_number(by, sizeof by, st->light.y),
                         key_number(bz, sizeof bz, st->light.z), st->row,
                         key_number(byaw, sizeof byaw, st->yaw));
      }
    }
  }
  st->key = key;
  out.shade = illuminate;
  out.shade_ctx = st;
  out.key = key;
  return out;
}

static qa_model_source_options cached_options(void *ctx,
                                              const qa_scene_entity *entity) {
  prepare_call *call = ctx;
  for (size_t i = 0; i < call->option_count; i++)
    if (call->option_cache[i].entity == entity)
      return call->option_cache[i].options;
  qa_model_source_options value = call->user_options != NULL
                                      ? call->user_options(call->user_options_ctx, entity)
                                      : (qa_model_source_options){0};
  if (call->option_count == call->option_capacity) {
    call->option_capacity = call->option_capacity ? call->option_capacity * 2 : 16;
    call->option_cache = realloc(call->option_cache,
                                 call->option_capacity * sizeof *call->option_cache);
    if (call->option_cache == NULL) return value;
  }
  call->option_cache[call->option_count].entity = entity;
  call->option_cache[call->option_count].options = value;
  call->option_count++;
  return value;
}

static qa_vec3 renderer_palette_color(qa_scene_model_renderer *r, double index) {
  const qa_palette *palette = r->provider.palette;
  if (palette == NULL)
    qa_raise("Error", 0, 0, "Indexed model effects require a source palette");
  double offset = index * 3;
  qa_vec3 out = {0, 0, 0};
  double *channels[3] = {&out.x, &out.y, &out.z};
  for (int i = 0; i < 3; i++) {
    double at = offset + i;
    if (at >= 0 && trunc(at) == at && at < (double)palette->colors.length)
      *channels[i] = palette->colors.bytes[(size_t)at];
  }
  return out;
}
static qa_vec3 palette_color_cb(void *ctx, const qa_scene_entity *entity,
                                double index) {
  (void)entity;
  return renderer_palette_color(ctx, index);
}

/* --- fogFor / world arrays / shade division --- */
static const qa_fog_selection *fog_for(
    qa_scene_model_renderer *r, const qa_prepared_model_surface *surface) {
  const qa_scene_entity *entity = &surface->entity;
  const qa_decoded_model *model = &entity->model;
  double frame = entity->pose.is_frame ? entity->pose.frame : 0;
  if (surface->options.has_no_world_model && surface->options.no_world_model)
    return NULL;
  qa_vec3 center = entity->transform.origin;
  double radius = 0;
  if (surface->has_fog_sphere) {
    center = (qa_vec3){fr(center.x + surface->fog_local_origin.x),
                       fr(center.y + surface->fog_local_origin.y),
                       fr(center.z + surface->fog_local_origin.z)};
    radius = surface->fog_radius;
    for (size_t i = 0; i < r->world->fog_selection_count; i++) {
      qa_bounds b = r->world->fog_selections[i].volume.bounds;
      if (fr(center.x - radius) < b.max.x && fr(center.x + radius) > b.min.x &&
          fr(center.y - radius) < b.max.y && fr(center.y + radius) > b.min.y &&
          fr(center.z - radius) < b.max.z && fr(center.z + radius) > b.min.z)
        return &r->world->fog_selections[i];
    }
    return NULL;
  }
  if (model->kind == QA_DECODED_MODEL_Q3_MESH)
    qa_raise("Error", 0, 0, "Prepared MD3 surface lost its selected-frame fog sphere");
  if (surface->options.has_source && model->kind == QA_DECODED_MODEL_MD4)
    return NULL;
  if (model->kind == QA_DECODED_MODEL_MD4) {
    const qa_md4_frame *pose = NULL;
    if (frame >= 0 && trunc(frame) == frame && frame < (double)model->as.md4->frame_count)
      pose = &model->as.md4->frames[(size_t)frame];
    else if (model->as.md4->frame_count > 0) pose = &model->as.md4->frames[0];
    if (pose != NULL) {
      center = qa_add3(center, pose->local_origin);
      radius = pose->radius;
    }
  } else if (model->kind == QA_DECODED_MODEL_MD5) {
    const qa_md5_model_frame *pose = NULL;
    if (frame >= 0 && trunc(frame) == frame && frame < (double)model->as.md5->frame_count)
      pose = &model->as.md5->frames[(size_t)frame];
    else if (model->as.md5->frame_count > 0) pose = &model->as.md5->frames[0];
    if (pose != NULL) radius = qa_radius_from_bounds(pose->bounds);
  } else if (model->kind != QA_DECODED_MODEL_BRUSH_MODEL) {
    qa_bounds bounds = {0};
    switch (model->kind) {
      case QA_DECODED_MODEL_Q1_ALIAS: bounds = model->as.q1_alias->bounds; break;
      case QA_DECODED_MODEL_Q2_ALIAS: bounds = model->as.q2_alias->bounds; break;
      case QA_DECODED_MODEL_Q1_SPRITE: bounds = model->as.q1_sprite->bounds; break;
      case QA_DECODED_MODEL_Q2_SPRITE: bounds = model->as.q2_sprite->bounds; break;
      default: break;
    }
    radius = qa_radius_from_bounds(bounds);
  }
  for (size_t i = 0; i < r->world->fog_selection_count; i++) {
    qa_bounds b = r->world->fog_selections[i].volume.bounds;
    if (center.x - radius < b.max.x && center.x + radius > b.min.x &&
        center.y - radius < b.max.y && center.y + radius > b.min.y &&
        center.z - radius < b.max.z && center.z + radius > b.min.z)
      return &r->world->fog_selections[i];
  }
  return NULL;
}

/* Retained world arrays: batch lighting views them; freed never
 * (TS retains them in a WeakMap for the geometry lifetime). */
static void world_arrays(qa_material_geometry geometry, qa_vec3 **out_positions,
                         qa_vec3 **out_normals) {
  size_t count = geometry.vertex_count;
  qa_vec3 *positions = calloc(count ? count : 1, sizeof *positions);
  qa_vec3 *normals = calloc(count ? count : 1, sizeof *normals);
  if (positions != NULL && normals != NULL)
    for (size_t i = 0; i < count; i++) {
      positions[i] = geometry.vertices[i].position;
      normals[i] = qa_normalize3(geometry.vertices[i].normal);
    }
  *out_positions = positions;
  *out_normals = normals;
}

static qa_vec4 divide_color(qa_vec4 color, double scale) {
  return (qa_vec4){color.x / scale, color.y / scale, color.z / scale, color.w};
}
/* Returns divided copy storage in *owned (single or pair vertices). */
static void shade_divide_batch(qa_draw_batch *batch, double scale) {
  if (!batch->texturing_is_pair) {
    qa_render_vertex *copy = calloc(batch->vertex_count ? batch->vertex_count : 1,
                                    sizeof *copy);
    if (copy == NULL) return;
    for (size_t i = 0; i < batch->vertex_count; i++) {
      copy[i] = batch->vertices[i];
      copy[i].color = divide_color(copy[i].color, scale);
    }
    /* Retained: batch vertex storage is owned by the batch array in
     * the evaluate port; the replaced array is intentionally leaked
     * with the original (destroy frees the current pointer). */
    batch->vertices = copy;
  } else {
    qa_multitexture_vertex *copy =
        calloc(batch->pair_vertex_count ? batch->pair_vertex_count : 1, sizeof *copy);
    if (copy == NULL) return;
    for (size_t i = 0; i < batch->pair_vertex_count; i++) {
      copy[i] = batch->pair_vertices[i];
      copy[i].vertex.color = divide_color(copy[i].vertex.color, scale);
    }
    batch->pair_vertices = copy;
  }
}

typedef struct cone_lighting_ctx {
  qa_model_transform transform;
  const qa_q2_fragment_light *cone_lights;
  size_t cone_count;
  const qa_q2_shadow_atlas *atlas;
} cone_lighting_ctx;
static qa_batch_lighting cone_model_lighting(void *ctx, qa_material_geometry geometry) {
  cone_lighting_ctx *cone = ctx;
  qa_batch_lighting lighting = {0};
  lighting.kind = "q2-world";
  lighting.pass = "model";
  lighting.has_shade_scale = false;
  lighting.atlas = cone->atlas;
  qa_q2_fragment_light *lights =
      calloc(cone->cone_count ? cone->cone_count : 1, sizeof *lights);
  qa_vec3 *positions = calloc(geometry.vertex_count ? geometry.vertex_count : 1, sizeof *positions);
  qa_vec3 *normals = calloc(geometry.vertex_count ? geometry.vertex_count : 1, sizeof *normals);
  if (lights != NULL)
    for (size_t i = 0; i < cone->cone_count; i++) {
      lights[i] = cone->cone_lights[i];
      lights[i].fraction = (qa_vec3){0, 0, 0};
    }
  if (positions != NULL && normals != NULL)
    for (size_t i = 0; i < geometry.vertex_count; i++) {
      positions[i] = qa_model_world_point(cone->transform, geometry.vertices[i].position);
      normals[i] = qa_normalize3(qa_model_world_direction(cone->transform, geometry.vertices[i].normal));
    }
  lighting.lights = lights;
  lighting.light_count = cone->cone_count;
  lighting.world_positions = positions;
  lighting.world_position_count = geometry.vertex_count;
  lighting.normals = normals;
  lighting.normal_count = geometry.vertex_count;
  return lighting;
}

typedef struct draw_call {
  prepare_call *call;
  const qa_world_view_input *input;
} draw_call;

/* TS currentRemap(original)?.material ?? original, with its timeOffset. */
static const qa_mat_compiled_material *remapped(qa_registered_scene_material original,
                                                double *time_offset_out) {
  *time_offset_out = 0;
  qa_scene_material_remap remap;
  if (qa_current_remap(qa_registered_scene_material_compiled(original), original.handle, &remap)) {
    *time_offset_out = remap.time_offset;
    return qa_registered_scene_material_compiled(remap.material);
  }
  return qa_registered_scene_material_compiled(original);
}

static qa_scene_model_group *draw_surface(void *ctx,
                                          const qa_prepared_model_surface *surface,
                                          size_t *out_count);

static qa_scene_model_group *draw_surface(void *ctx,
                                          const qa_prepared_model_surface *surface,
                                          size_t *out_count) {
  draw_call *dc = ctx;
  prepare_call *call = dc->call;
  qa_scene_model_renderer *r = call->renderer;
  const qa_world_view_input *input = dc->input;
  qa_model_source_options options = surface->options;
  *out_count = 0;
  char *key = material_key(&surface->entity.resource, &surface->entity.model,
                           &surface->image, &options);
  material_entry *material = key != NULL ? find_material(r, key) : NULL;
  free(key);
  if (material == NULL)
    qa_raise("Error", 0, 0, "Model material was not preloaded: %s/%s",
                   surface->entity.resource.requested_path, surface->name);
  double time = source_seconds(input->time);
  double flags = flags_is(&surface->entity, "q2") ? surface->entity.flags_bits : 0;
  bool has_shadows = input->has_q2_fragment_lighting;
  bool receives_cone =
      call->cone_lights != NULL && has_shadows && !surface->unlit &&
      !(options.has_view_model && options.view_model) &&
      ((qa_int32(flags) & (QA_Q2_SHELL_MASK | 8 | 4 | 16)) == 0) &&
      !((options.has_infrared && options.infrared) && bit(flags, 32768));
  qa_scene_model_group *group = calloc(1, sizeof *group);
  if (group == NULL) return NULL;
  if (material->is_q3) {
    double remap_offset = 0;
    const qa_mat_compiled_material *compiled = remapped(material->original, &remap_offset);
    qa_axis axis = surface->transform.axis;
    qa_axis scaled_axis = {{qa_scale3(axis.vectors[0], surface->transform.scale.x),
                            qa_scale3(axis.vectors[1], surface->transform.scale.y),
                            qa_scale3(axis.vectors[2], surface->transform.scale.z)}};
    const qa_fog_selection *fog = fog_for(r, surface);
    qa_scene_model_transform world_transform = {surface->transform.origin, scaled_axis, false, 0};
    qa_world_material_context *base = new_world_context(r);
    qa_world_scene_material_context(r->world, input, &world_transform,
                                    fog != NULL ? &fog->volume : NULL, base);
    qa_material_draw_context context = base->context;
    cone_lighting_ctx cone = {surface->transform, call->cone_lights,
                              call->cone_count, input->fragment_atlas};
    if (receives_cone) {
      /* Called only inside the batch prepare below. */
      context.model_lighting = cone_model_lighting;
      context.model_lighting_ctx = &cone;
    }
    context.entity_rgba = qa_byte_color(surface->entity.color);
    base->lighting_value = qa_model_light_sampler_entity_lighting(
        &r->lighting, &surface->entity, input,
        options.has_no_world_model && options.no_world_model);
    context.lighting = &base->lighting_value;
    if (options.has_shader_tex_coord) context.shader_tex_coord = options.shader_tex_coord;
    qa_vec3 origin = surface->transform.origin;
    context.local_view_origin = qa_q3_model_view_origin(
        origin, scaled_axis, input->camera.origin,
        options.has_non_normalized_axes && options.non_normalized_axes);
    context.depth_range[0] = surface->depth_range[0];
    context.depth_range[1] = surface->depth_range[1];
    context.time_offset = source_seconds(surface->entity.shader_time) + remap_offset;
    qa_vec3 non_normalized_axis = scaled_axis.vectors[0];
    if (options.has_non_normalized_axes && options.non_normalized_axes) {
      context.deform_view.non_normalized_axis = &non_normalized_axis;
    } else {
      context.deform_view.non_normalized_axis = NULL;
    }
    /* Batches keep no projector: it runs only inside the prepare below. */
    projector_ctx pc = {&call->projector, false, &surface->transform};
    context.project = (qa_mat_project_fn){&pc, project_fn};
    size_t batch_count = 0;
    qa_draw_batch *batches = qa_prepare_material_batches(
        compiled, surface->local_geometry, &context, &batch_count);
    group->batches = batches;
    group->batch_count = batch_count;
    if (!options.has_source) {
      group->kind = "compiled";
      group->material = compiled;
    } else {
      group->kind = "source";
      group->source_material = material->original;
      group->source_view = options.source_view;
      group->source_entity = options.source_entity;
      group->surface = surface->surface_index;
      group->fog = fog == NULL ? 0 : fog->index + 1;
      group->dlight = 0;
    }
    *out_count = 1;
    return group;
  }
  /* Legacy path. */
  const qa_scene_texture *texture = material->texture;
  double alpha = surface->translucent ? surface->entity.color.w : 1;
  qa_surface_lighting lighting = {0};
  lighting.kind = "vertex";
  qa_render_material definition;
  if (r->provider.family == QA_GAME_FAMILY_Q1) {
    qa_create_q1_options create_options = {0};
    create_options.has_alpha = true;
    create_options.alpha = alpha;
    definition = qa_create_q1_material(texture->name, texture->image, lighting,
                                       create_options);
  } else {
    definition = qa_create_q2_material(texture->name, &texture->image, 1, lighting, 0, NULL);
    definition.alpha = alpha;
  }
  bool liquid = str_is(definition.kind, "q1")
                    ? (str_is(definition.surface, "water") ||
                       str_is(definition.surface, "slime") ||
                       str_is(definition.surface, "lava") ||
                       str_is(definition.surface, "teleport"))
                    : (definition.warp || definition.flowing);
  bool world_space = input->world_space_batches && !input->camera.clip_is_portal &&
                     !surface->mirror_weapon && !liquid;
  size_t batch_count = 0;
  qa_draw_batch *batches = NULL;
  qa_legacy_material_draw_context legacy_context = {0};
  legacy_context.time = time;
  legacy_context.animation_frame = 0;
  legacy_context.alternate_animation = false;
  legacy_context.fullbright = surface->unlit ? NULL : texture->fullbright;
  legacy_context.q1_lightmap_encoding = "rgb";
  legacy_context.cull = surface->cull;
  legacy_context.depth_range[0] = surface->depth_range[0];
  legacy_context.depth_range[1] = surface->depth_range[1];
  if (world_space) {
    legacy_context.project = (qa_mat_project_fn){NULL, identity_project};
    batches = qa_prepare_legacy_material_batches(&definition, surface->geometry,
                                                 &legacy_context, &batch_count);
    for (size_t i = 0; i < batch_count; i++) batches[i].space = "world";
  } else {
    projector_ctx pc = {&call->projector, surface->mirror_weapon, NULL};
    legacy_context.project = (qa_mat_project_fn){&pc, project_fn};
    batches = qa_prepare_legacy_material_batches(&definition, surface->geometry,
                                                 &legacy_context, &batch_count);
  }
  qa_vec3 shade;
  bool has_shade = find_light(call, &surface->entity, &shade);
  bool receives = r->provider.family == QA_GAME_FAMILY_Q2 && has_shade &&
                  has_shadows && input->fragment_atlas != NULL &&
                  !surface->unlit &&
                  !(options.has_view_model && options.view_model) &&
                  ((qa_int32(flags) & (QA_Q2_SHELL_MASK | 8 | 4 | 16)) == 0) &&
                  !((options.has_infrared && options.infrared) && bit(flags, 32768));
  size_t affecting_count = 0;
  qa_q2_model_shadow_light *affecting = NULL;
  if (receives) {
    if (call->cone_lights == NULL) {
      affecting = qa_alias_shadow_light_fractions(
          surface->entity.transform.origin, shade, input->fragment_lights,
          input->fragment_light_count, 1, false, &affecting_count);
    } else {
      qa_q2_fragment_light *point_lights =
          calloc(input->fragment_light_count ? input->fragment_light_count : 1,
                 sizeof *point_lights);
      size_t point_count = 0;
      if (point_lights != NULL)
        for (size_t i = 0; i < input->fragment_light_count; i++)
          if (input->fragment_lights[i].cone == NULL)
            point_lights[point_count++] = input->fragment_lights[i];
      affecting = qa_alias_shadow_light_fractions(
          surface->entity.transform.origin, shade, point_lights, point_count, 1,
          false, &affecting_count);
      free(point_lights);
    }
  }
  double shade_scale = (receives && affecting_count != 0)
                           ? qa_alias_shade_divisor(shade)
                           : 1;
  for (size_t bi = 0; bi < batch_count; bi++) {
    qa_draw_batch *batch = &batches[bi];
    qa_render_state state = batch->state;
    if (!str_is(surface->alpha_test, "none")) state.alpha_test = surface->alpha_test;
    if (surface->mirror_weapon)
      state.cull = str_is(state.cull, "front") ? "back"
                   : str_is(state.cull, "back") ? "front" : "none";
    batch->state = state;
    if (bi != 0) continue;
    bool alias_shadow = affecting_count != 0 && has_shadows &&
                        input->fragment_atlas != NULL;
    if (alias_shadow) {
      qa_vec3 *positions = NULL, *normals = NULL;
      world_arrays(surface->geometry, &positions, &normals);
      free(normals);
      batch->lighting.kind = "q2-model-shadow";
      batch->lighting.world_positions = positions;
      batch->lighting.world_position_count = surface->geometry.vertex_count;
      batch->lighting.model_shadow_lights = affecting;
      batch->lighting.model_shadow_light_count = affecting_count;
      batch->lighting.has_shade_scale = true;
      batch->lighting.shade_scale = shade_scale;
      batch->lighting.atlas_value = *input->fragment_atlas;
      shade_divide_batch(batch, shade_scale);
    }
    if (receives_cone && has_shadows) {
      qa_vec3 *positions = NULL, *normals = NULL;
      world_arrays(surface->geometry, &positions, &normals);
      qa_q2_fragment_light *lights =
          calloc(input->fragment_light_count ? input->fragment_light_count : 1,
                 sizeof *lights);
      if (lights != NULL)
        for (size_t li = 0; li < input->fragment_light_count; li++) {
          lights[li] = input->fragment_lights[li];
          bool cone = lights[li].cone != NULL;
          if (!cone) {
            lights[li].color = (qa_vec3){0, 0, 0};
            lights[li].scale = 0;
          }
          lights[li].fraction = (qa_vec3){0, 0, 0};
          for (size_t ai = 0; ai < affecting_count; ai++)
            if (affecting[ai].origin.x == lights[li].origin.x &&
                affecting[ai].origin.y == lights[li].origin.y &&
                affecting[ai].origin.z == lights[li].origin.z)
              lights[li].fraction = affecting[ai].fraction;
        }
      bool was_shadow = str_is(batch->lighting.kind, "q2-model-shadow");
      batch->lighting = (qa_batch_lighting){0};
      batch->lighting.kind = "q2-world";
      batch->lighting.pass = "model";
      batch->lighting.has_shade_scale = was_shadow;
      batch->lighting.shade_scale = was_shadow ? shade_scale : 0;
      batch->lighting.world_positions = positions;
      batch->lighting.world_position_count = surface->geometry.vertex_count;
      batch->lighting.normals = normals;
      batch->lighting.normal_count = surface->geometry.vertex_count;
      batch->lighting.atlas = input->fragment_atlas;
      batch->lighting.lights = lights;
      batch->lighting.light_count = input->fragment_light_count;
    }
  }
  if (affecting_count == 0) free(affecting);
  /* Planar shadow (Q1). */
  if ((options.has_planar_shadow && options.planar_shadow) &&
      !(options.has_view_model && options.view_model) &&
      r->provider.family == QA_GAME_FAMILY_Q1 &&
      (surface->entity.model.kind == QA_DECODED_MODEL_Q1_ALIAS ||
       surface->entity.model.kind == QA_DECODED_MODEL_MD5)) {
    qa_model_light_sample floor_sample = qa_model_light_sampler_sample(
        &r->lighting, surface->transform.origin, input, false);
    if (floor_sample.has_floor) {
      double yaw = atan2(surface->transform.axis.vectors[0].y,
                         surface->transform.axis.vectors[0].x);
      qa_vec3 direction = qa_q1_alias_shadow_direction(yaw);
      qa_model_transform shadow_transform = surface->transform;
      shadow_transform.scale = (qa_vec3){1, 1, 1};
      qa_draw_batch *grown = realloc(batches, (batch_count + 1) * sizeof *grown);
      if (grown != NULL) {
        batches = grown;
        qa_draw_batch *shadow = &batches[batch_count];
        memset(shadow, 0, sizeof *shadow);
        size_t vcount = surface->local_geometry.vertex_count;
        qa_render_vertex *vertices = calloc(vcount ? vcount : 1, sizeof *vertices);
        double *indices = calloc(surface->local_geometry.index_count
                                       ? surface->local_geometry.index_count : 1,
                                   sizeof *indices);
        if (vertices != NULL && indices != NULL) {
          projector_ctx pc = {&call->projector, false, NULL};
          for (size_t vi = 0; vi < vcount; vi++) {
            qa_material_vertex vertex = surface->local_geometry.vertices[vi];
            qa_vec3 scaled = {fr(vertex.position.x * surface->transform.scale.x),
                              fr(vertex.position.y * surface->transform.scale.y),
                              fr(vertex.position.z * surface->transform.scale.z)};
            vertices[vi].position = project_fn(
                &pc, qa_model_world_point(
                         shadow_transform,
                         qa_q1_alias_shadow_point(scaled, direction,
                                                  surface->transform.origin.z,
                                                  floor_sample.floor_point.z)));
            vertices[vi].tex_coord = (qa_vec2){0, 0};
            vertices[vi].color = (qa_vec4){0, 0, 0, 0.5 * alpha};
          }
          memcpy(indices, surface->local_geometry.indices,
                 surface->local_geometry.index_count * sizeof *indices);
          shadow->lighting.kind = "vertex";
          shadow->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
          shadow->texture.image = provider_textures(r)->white->image;
          shadow->state = (qa_render_state){{"src-alpha", "one-minus-src-alpha"},
                                            "less-equal", true, "none",
                                            surface->cull,
                                            {surface->depth_range[0], surface->depth_range[1]},
                                            NULL};
          shadow->vertices = vertices;
          shadow->vertex_count = vcount;
          shadow->indices.values = indices;
          shadow->indices.count = surface->local_geometry.index_count;
          batch_count++;
        } else {
          free(vertices);
          free(indices);
        }
      }
    }
  }
  group->kind = "sequence";
  group->sequence = alpha < 1 ? "translucent" : "opaque";
  group->batches = batches;
  group->batch_count = batch_count;
  *out_count = 1;
  return group;
}

qa_scene_model_group *qa_scene_model_renderer_prepare(
    qa_scene_model_renderer *renderer, const qa_scene_entity *entities,
    size_t entity_count, const qa_world_view_input *input,
    qa_model_source_options_fn source_options, void *source_options_ctx,
    const qa_model_skinning_frame *skinning_frame, size_t *out_count) {
  prepare_call call = {0};
  call.renderer = renderer;
  call.input = input;
  call.time = source_seconds(input->time);
  call.user_options = source_options;
  call.user_options_ctx = source_options_ctx;
  call.projector = make_projector(input->camera);
  /* Cone lights. */
  bool any_cone = false;
  if (input->has_q2_fragment_lighting)
    for (size_t i = 0; i < input->fragment_light_count; i++)
      if (input->fragment_lights[i].cone != NULL) any_cone = true;
  if (any_cone) {
    call.cone_owned = calloc(input->fragment_light_count, sizeof *call.cone_owned);
    if (call.cone_owned != NULL)
      for (size_t i = 0; i < input->fragment_light_count; i++)
        if (input->fragment_lights[i].cone != NULL)
          call.cone_owned[call.cone_count++] = input->fragment_lights[i];
    call.cone_lights = call.cone_owned;
  }
  call.q2_lighting = renderer->provider.family == QA_GAME_FAMILY_Q2 ||
                     (renderer->provider.family == QA_GAME_FAMILY_Q1 &&
                      renderer->world->map.kind == QA_DECODED_WORLD_Q2);
  call.lighting_input = input;
  if (call.q2_lighting && input->has_q2_fragment_lighting) {
    call.lighting_input_storage = *input;
    size_t total = input->fragment_light_count +
                   (input->has_lights ? input->light_count : 0);
    call.lighting_lights = calloc(total ? total : 1, sizeof *call.lighting_lights);
    size_t count = 0;
    if (call.lighting_lights != NULL) {
      if (!input->has_lights) {
        for (size_t i = 0; i < input->fragment_light_count; i++) {
          const qa_q2_fragment_light *light = &input->fragment_lights[i];
          if (call.cone_lights != NULL && light->cone != NULL) continue;
          call.lighting_lights[count++] =
              (qa_surface_dynamic_light){light->origin, light->radius, 0, light->color};
        }
      } else {
        for (size_t i = 0; i < input->light_count; i++)
          call.lighting_lights[count++] = input->lights[i];
        for (size_t i = 0; i < input->fragment_light_count; i++) {
          const qa_q2_fragment_light *light = &input->fragment_lights[i];
          if (light->cone != NULL || str_is(light->shadow.kind, "none")) continue;
          call.lighting_lights[count++] =
              (qa_surface_dynamic_light){light->origin, light->radius, 0, light->color};
        }
      }
    }
    call.lighting_input_storage.lights = call.lighting_lights;
    call.lighting_input_storage.light_count = count;
    call.lighting_input_storage.has_lights = true;
    call.lighting_input = &call.lighting_input_storage;
  }
  qa_plane frustum[5];
  size_t frustum_count = camera_frustum(input->camera, frustum);
  qa_model_preparation_context context = {0};
  context.skinning_frame = skinning_frame;
  context.camera = input->camera;
  context.time_seconds = call.time;
  context.frustum = frustum;
  context.frustum_count = frustum_count;
  context.options = cached_options;
  context.options_ctx = &call;
  context.prepare_vertex_lighting = prepare_vertex_lighting_of;
  context.prepare_vertex_lighting_ctx = &call;
  context.palette_color = palette_color_cb;
  context.palette_color_ctx = renderer;
  draw_call dc = {&call, input};
  qa_model_group_context material = {&dc, draw_surface};
  qa_scene_model_group *result = NULL;
  size_t result_count = 0, result_capacity = 0;
  for (size_t ei = 0; ei < entity_count; ei++) {
    /* TS reads this.provider.modelPolicy per entity. */
    context.has_model_policy = provider_model_policy(renderer, &context.model_policy);
    qa_prepared_model_entity prepared;
    if (!qa_prepare_scene_entity(&entities[ei], &context, &prepared)) {
      qa_prepared_model_entity_destroy(&prepared);
      continue;
    }
    size_t group_count = 0;
    qa_scene_model_group *groups =
        qa_prepared_model_groups(&prepared, &material, &group_count);
    qa_prepared_model_entity_destroy(&prepared);
    for (size_t gi = 0; gi < group_count; gi++) {
      if (result_count == result_capacity) {
        result_capacity = result_capacity ? result_capacity * 2 : 16;
        result = realloc(result, result_capacity * sizeof *result);
        if (result == NULL) break;
      }
      result[result_count++] = groups[gi];
    }
    free(groups);
  }
  for (size_t i = 0; i < call.state_count; i++) {
    free(call.states[i]->key);
    free(call.states[i]);
  }
  free(call.states);
  free(call.lights);
  free(call.option_cache);
  free(call.lighting_lights);
  free(call.cone_owned);
  *out_count = result_count;
  return result;
}

/* --- Shadow casters (shadow-geometry.ts / shadows.ts mirrors) --- */
static bool entity_casts_shadow(const qa_scene_entity *entity, bool view_model) {
  if (view_model || entity->model.kind == QA_DECODED_MODEL_Q1_SPRITE ||
      entity->model.kind == QA_DECODED_MODEL_Q2_SPRITE)
    return false;
  if (flags_is(entity, "q2"))
    return (qa_int32(entity->flags_bits) & (4 | 16 | 32 | 128 | 8192 | 0x00200000)) == 0;
  if (flags_is(entity, "q3") && bit(entity->flags_bits, 4 | 8 | 64)) return false;
  return entity->color.w >= 1;
}

static bool shadow_material_geometry(const qa_mat_compiled_material *shader,
                                     qa_material_geometry geometry,
                                     qa_material_draw_context *context,
                                     qa_material_geometry *out,
                                     qa_material_deform_state *state) {
  const qa_mat_shader_definition *definition = &shader->registered.definition;
  if (str_is(shader->finished.iterator.kind, "sky") || shader->finished.sort > 3)
    return false;
  for (size_t i = 0; i < definition->surface_parms.count; i++) {
    const char *parm = definition->surface_parms.values[i];
    if (str_is(parm, "nodraw") || str_is(parm, "trans") || str_is(parm, "water") ||
        str_is(parm, "slime") || str_is(parm, "lava"))
      return false;
  }
  for (size_t i = 0; i < definition->deform_count; i++) {
    const char *kind = definition->deforms[i].kind;
    if (str_is(kind, "autosprite") || str_is(kind, "autosprite2") ||
        str_is(kind, "projectionshadow"))
      return false;
  }
  double time = context->time - context->time_offset;
  if (definition->clamp_time != 0 && time >= definition->clamp_time)
    time = definition->clamp_time;
  qa_material_deform_state_init(state, geometry, context->refdef_time,
                                context->render_text, context->render_text_count,
                                definition->name);
  *out = qa_deform_geometry_apply(state, definition->deforms,
                                  definition->deform_count, context->deform_view,
                                  fr(time), context->noise,
                                  context->projection_shadow);
  return true;
}

static qa_render_scene_shadow_mesh shadow_mesh_of(qa_material_geometry geometry) {
  qa_render_scene_shadow_mesh mesh = {0};
  mesh.positions = calloc(geometry.vertex_count ? geometry.vertex_count : 1,
                          sizeof *mesh.positions);
  if (mesh.positions != NULL)
    for (size_t i = 0; i < geometry.vertex_count; i++)
      mesh.positions[i] = geometry.vertices[i].position;
  mesh.position_count = geometry.vertex_count;
  /* The prepared surface frees generated index storage before the caster is
   * used (TS shares the array, kept alive by the GC): copy it. */
  mesh.owned_indices = calloc(geometry.index_count ? geometry.index_count : 1,
                              sizeof *mesh.owned_indices);
  if (mesh.owned_indices != NULL && geometry.index_count != 0)
    memcpy(mesh.owned_indices, geometry.indices, geometry.index_count * sizeof *mesh.owned_indices);
  mesh.indices = mesh.owned_indices;
  mesh.index_count = geometry.index_count;
  return mesh;
}
static qa_render_scene_shadow_caster shadow_caster_of(
    qa_vec3 origin, qa_render_scene_shadow_mesh *meshes, size_t mesh_count) {
  double radius_squared = 0;
  for (size_t mi = 0; mi < mesh_count; mi++)
    for (size_t pi = 0; pi < meshes[mi].position_count; pi++) {
      qa_vec3 position = meshes[mi].positions[pi];
      double dx = position.x - origin.x, dy = position.y - origin.y,
             dz = position.z - origin.z;
      radius_squared = qa_js_max(radius_squared, dx * dx + dy * dy + dz * dz);
    }
  double radius = sqrt(radius_squared);
  return (qa_render_scene_shadow_caster){origin, radius > 0 ? radius : 64,
                                         meshes, mesh_count};
}
void qa_render_scene_shadow_casters_destroy(
    qa_render_scene_shadow_caster *casters, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t mi = 0; mi < casters[i].mesh_count; mi++) {
      free(casters[i].meshes[mi].positions);
      free(casters[i].meshes[mi].owned_indices);
    }
    free(casters[i].meshes);
  }
  free(casters);
}

typedef struct shadow_visit {
  qa_scene_model_renderer *renderer;
  const qa_world_view_input *input;
  qa_model_source_options_fn options;
  void *options_ctx;
  const qa_model_skinning_frame *skinning_frame;
  qa_model_retain_body_fn retain_body;
  void *retain_body_ctx;
  double time;
  qa_render_scene_shadow_caster *result;
  size_t count, capacity;
} shadow_visit;

typedef struct retain_ctx {
  shadow_visit *visit;
} retain_ctx;
static bool retain_shadow_body_cb(void *ctx, const qa_scene_entity *selected,
                                  qa_render_scene_shadow_sphere sphere,
                                  const qa_model_image_selection *images,
                                  size_t image_count,
                                  qa_model_source_options selected_options) {
  retain_ctx *retain = ctx;
  shadow_visit *visit = retain->visit;
  qa_scene_model_renderer *r = visit->renderer;
  for (size_t i = 0; i < image_count; i++) {
    char *key = material_key(&selected->resource, &selected->model, &images[i],
                             &selected_options);
    material_entry *material = key != NULL ? find_material(r, key) : NULL;
    free(key);
    if (material == NULL) return true;
    if (material->is_q3) {
      double offset = 0;
      const qa_mat_compiled_material *compiled = remapped(material->original, &offset);
      if (compiled->registered.definition.deform_count != 0) return true;
    }
  }
  return visit->retain_body(visit->retain_body_ctx, sphere);
}
static qa_model_source_options fixed_options(void *ctx,
                                             const qa_scene_entity *entity) {
  (void)entity;
  return *(qa_model_source_options *)ctx;
}

static void visit_shadow(shadow_visit *visit, qa_scene_entity entity,
                         const qa_scene_entity *original) {
  qa_scene_model_renderer *r = visit->renderer;
  const qa_world_view_input *input = visit->input;
  qa_model_source_options source = visit->options != NULL
                                       ? visit->options(visit->options_ctx, original)
                                       : (qa_model_source_options){0};
  bool view_model = source.has_view_model && source.view_model;
  if (!entity_casts_shadow(&entity, view_model)) return;
  qa_scene_entity body = entity;
  body.attachments = NULL;
  body.attachment_count = 0;
  if (flags_is(&body, "q2"))
    body.flags_bits = (double)(qa_int32(body.flags_bits) & ~QA_Q2_SHELL_MASK);
  if (body.pose.is_frame)
    body.pose.back_lerp = qa_js_min(1, qa_js_max(0, body.pose.back_lerp));
  retain_ctx retain = {visit};
  qa_model_preparation_context context = {0};
  context.camera = input->camera;
  context.time_seconds = visit->time;
  context.no_cull = true;
  context.purpose = "shadow";
  if (visit->retain_body != NULL) {
    context.retain_shadow_body = retain_shadow_body_cb;
    context.retain_shadow_body_ctx = &retain;
  }
  context.skinning_frame = visit->skinning_frame;
  context.has_model_policy = provider_model_policy(r, &context.model_policy);
  qa_model_source_options fixed = source;
  context.options = fixed_options;
  context.options_ctx = &fixed;
  qa_prepared_model_entity prepared;
  if (!qa_prepare_scene_entity(&body, &context, &prepared)) {
    qa_prepared_model_entity_destroy(&prepared);
    return;
  }
  qa_render_scene_shadow_mesh *meshes = NULL;
  size_t mesh_count = 0, mesh_capacity = 0;
  for (size_t si = 0; si < prepared.surface_count; si++) {
    qa_prepared_model_surface *surface = &prepared.surfaces[si];
    char *key = material_key(&surface->entity.resource, &surface->entity.model,
                             &surface->image, &source);
    material_entry *material = key != NULL ? find_material(r, key) : NULL;
    free(key);
    if (material == NULL) {
      for (size_t mi = 0; mi < mesh_count; mi++) {
        free(meshes[mi].positions);
        free(meshes[mi].owned_indices);
      }
      free(meshes);
      qa_prepared_model_entity_destroy(&prepared);
      qa_raise("Error", 0, 0, "Shadow material was not preloaded: %s/%s",
                     entity.resource.requested_path, surface->name);
    }
    qa_render_scene_shadow_mesh mesh;
    if (!material->is_q3) {
      mesh = shadow_mesh_of(surface->geometry);
    } else {
      qa_axis axis = surface->transform.axis;
      qa_axis scaled_axis = {{qa_scale3(axis.vectors[0], surface->transform.scale.x),
                              qa_scale3(axis.vectors[1], surface->transform.scale.y),
                              qa_scale3(axis.vectors[2], surface->transform.scale.z)}};
      /* TS this.world.materialContext(input, transform): the deformed
       * geometry is copied out below, so the context need not outlive it. */
      qa_scene_model_transform world_transform = {surface->transform.origin, scaled_axis, false, 0};
      qa_world_material_context base;
      memset(&base, 0, sizeof base);
      qa_world_scene_material_context(r->world, input, &world_transform, NULL, &base);
      qa_material_draw_context draw_context = base.context;
      draw_context.entity_rgba = qa_byte_color(entity.color);
      draw_context.local_view_origin = qa_q3_model_view_origin(
          surface->transform.origin, scaled_axis, input->camera.origin,
          source.has_non_normalized_axes && source.non_normalized_axes);
      draw_context.time_offset = source_seconds(entity.shader_time);
      qa_vec3 non_normalized_axis = scaled_axis.vectors[0];
      if (source.has_non_normalized_axes && source.non_normalized_axes) {
        draw_context.deform_view.non_normalized_axis = &non_normalized_axis;
      } else {
        draw_context.deform_view.non_normalized_axis = NULL;
      }
      double offset = 0;
      const qa_mat_compiled_material *compiled = remapped(material->original, &offset);
      draw_context.time_offset += offset;
      qa_material_geometry deformed;
      qa_material_deform_state deform_state = {0};
      if (!shadow_material_geometry(compiled, surface->local_geometry,
                                    &draw_context, &deformed, &deform_state))
        continue;
      mesh = (qa_render_scene_shadow_mesh){0};
      mesh.positions = calloc(deformed.vertex_count ? deformed.vertex_count : 1,
                              sizeof *mesh.positions);
      if (mesh.positions != NULL)
        for (size_t vi = 0; vi < deformed.vertex_count; vi++)
          mesh.positions[vi] = qa_model_world_point(
              surface->transform, deformed.vertices[vi].position);
      mesh.position_count = deformed.vertex_count;
      mesh.indices = deformed.indices;
      mesh.index_count = deformed.index_count;
      mesh.owned_indices = calloc(deformed.index_count ? deformed.index_count : 1,
                                  sizeof *mesh.owned_indices);
      if (mesh.owned_indices != NULL) {
        memcpy(mesh.owned_indices, deformed.indices,
               deformed.index_count * sizeof *mesh.owned_indices);
        mesh.indices = mesh.owned_indices;
      }
      qa_material_deform_state_destroy(&deform_state);
    }
    if (mesh_count == mesh_capacity) {
      mesh_capacity = mesh_capacity ? mesh_capacity * 2 : 4;
      meshes = realloc(meshes, mesh_capacity * sizeof *meshes);
      if (meshes == NULL) break;
    }
    meshes[mesh_count++] = mesh;
  }
  if (mesh_count != 0) {
    if (visit->count == visit->capacity) {
      visit->capacity = visit->capacity ? visit->capacity * 2 : 8;
      visit->result = realloc(visit->result, visit->capacity * sizeof *visit->result);
    }
    if (visit->result != NULL)
      visit->result[visit->count++] =
          shadow_caster_of(entity.transform.origin, meshes, mesh_count);
    else
      free(meshes);
  } else {
    free(meshes);
  }
  qa_scene_entity parent = prepared.entity;
  if (body.pose.is_frame) {
    parent.pose = body.pose;
    parent.pose.frame = prepared.frame;
    parent.pose.previous_frame = prepared.previous_frame;
  } else {
    parent.pose = body.pose;
  }
  qa_prepared_model_entity_destroy(&prepared);
  for (size_t ai = 0; ai < entity.attachment_count; ai++) {
    const qa_scene_entity_attachment *attachment = &entity.attachments[ai];
    qa_model_attachment_tag_result tag;
    if (qa_model_attachment_tag(&parent, attachment->tag, &tag))
      visit_shadow(visit, qa_attach_scene_entity(&parent, attachment->entity, tag),
                   attachment->entity);
  }
}

qa_render_scene_shadow_caster *qa_scene_model_renderer_prepare_shadow_casters(
    qa_scene_model_renderer *renderer, const qa_scene_entity *entities,
    size_t entity_count, const qa_world_view_input *input,
    qa_model_source_options_fn options, void *options_ctx,
    const qa_model_skinning_frame *skinning_frame,
    qa_model_retain_body_fn retain_body, void *retain_body_ctx,
    size_t *out_count) {
  shadow_visit visit = {0};
  visit.renderer = renderer;
  visit.input = input;
  visit.options = options;
  visit.options_ctx = options_ctx;
  visit.skinning_frame = skinning_frame;
  visit.retain_body = retain_body;
  visit.retain_body_ctx = retain_body_ctx;
  visit.time = source_seconds(input->time);
  for (size_t i = 0; i < entity_count; i++)
    visit_shadow(&visit, entities[i], &entities[i]);
  *out_count = visit.count;
  return visit.result;
}


