/* Port of src/render/scene/models/prepare.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/prepare.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/game_numeric.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/md4.h"
#include "qa/formats/q3_model/md5.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/models/lighting.h"
#include "qa/render/scene/models/md3_bounds.h"
#include "qa/render/scene/models/replacements.h"
#include "qa/render/scene/models/shadow_bounds.h"
#include "qa/render/scene/models/sprites.h"
#include "qa/render/scene/models/transform.h"
#include "qa/render/scene/particles/legacy.h"
#include "qa/foundation/js.h"
#define MAX_MODEL_BEAM_SEGMENTS 512

static double fr(double value) { return (double)(float)value; }
static bool bit(double flags, int32_t mask) { return (qa_int32(flags) & mask) != 0; }
static bool flags_is(const qa_scene_entity *e, const char *kind) {
  return e->flags_kind != NULL && strcmp(e->flags_kind, kind) == 0;
}
static bool str_is(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static char *dup_format(const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  size_t length = strlen(buffer);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, buffer, length + 1);
  return out;
}

static double count_frames(const qa_decoded_model *model) {
  switch (model->kind) {
    case QA_DECODED_MODEL_BRUSH_MODEL: return 1;
    case QA_DECODED_MODEL_Q1_ALIAS: return (double)model->as.q1_alias->frame_count;
    case QA_DECODED_MODEL_Q2_ALIAS: return (double)model->as.q2_alias->frame_count;
    case QA_DECODED_MODEL_Q1_SPRITE: return (double)model->as.q1_sprite->frame_count;
    case QA_DECODED_MODEL_Q2_SPRITE: return (double)model->as.q2_sprite->frame_count;
    case QA_DECODED_MODEL_Q3_MESH: return (double)model->as.q3_mesh->frame_count;
    case QA_DECODED_MODEL_MD4: return (double)model->as.md4->frame_count;
    case QA_DECODED_MODEL_MD5: return (double)model->as.md5->frame_count;
  }
  return 0;
}

qa_repaired_frames qa_repair_frames(const qa_scene_entity *entity) {
  if (!entity->pose.is_frame) return (qa_repaired_frames){0, 0, 0, false};
  double frame = entity->pose.frame, previous = entity->pose.previous_frame;
  double back_lerp = entity->pose.back_lerp;
  if (trunc(frame) != frame || trunc(previous) != previous || !isfinite(back_lerp))
    qa_range_error("Invalid scene model pose");
  if (flags_is(entity, "q2") && bit(entity->flags_bits, 128))
    return (qa_repaired_frames){frame, previous, back_lerp, false};
  if (entity->model.kind == QA_DECODED_MODEL_MD5 &&
      str_is(entity->model.as.md5->skin_selection.kind, "q2-md2-replacement")) {
    double count = entity->model.as.md5->skin_selection.source_frame_count;
    bool fallback = frame < 0 || frame >= count || previous < 0 || previous >= count;
    if (fallback) { frame = 0; previous = 0; }
    return (qa_repaired_frames){frame, previous,
                                frame == previous ? 0 : back_lerp, fallback};
  }
  double count = count_frames(&entity->model);
  if (count == 0) qa_range_error("Scene model has no animation frames");
  if (entity->model.kind == QA_DECODED_MODEL_Q2_SPRITE ||
      (entity->model.kind == QA_DECODED_MODEL_MD5 &&
       !str_is(entity->model.as.md5->skin_selection.kind, "q1-mdl-replacement")) ||
      (flags_is(entity, "q3") && bit(entity->flags_bits, 512))) {
    frame = fmod(frame, count);
    previous = fmod(previous, count);
  }
  bool bad_frame = frame < 0 || frame >= count;
  bool bad_old = previous < 0 || previous >= count;
  if ((flags_is(entity, "q3") || entity->model.kind == QA_DECODED_MODEL_Q2_ALIAS) &&
      (bad_frame || bad_old)) {
    frame = 0; previous = 0;
  } else {
    if (bad_frame) frame = 0;
    if (bad_old) previous = 0;
  }
  if (frame == previous && entity->model.kind != QA_DECODED_MODEL_Q2_ALIAS)
    back_lerp = 0;
  return (qa_repaired_frames){frame, previous, back_lerp, bad_frame || bad_old};
}

static const void *at_any(const void *values, size_t count, size_t size,
                          double index, const char *label) {
  return qa_model_at(values, count, size, index, label);
}
#define AT_PTR(type, values, count, index, label) \
  ((const type *)at_any((values), (count), sizeof(type), (index), (label)))

static qa_model_image_selection selected_shader(
    const char *name, const char *const *shaders, size_t shader_count,
    const qa_scene_entity *entity, const qa_model_source_options *options) {
  qa_model_image_selection out = {0};
  out.has_transparent_index = true;
  out.transparent_index = 0;
  /* normalized below per kind */
  if (options->custom_shader != NULL) {
    out.kind = QA_MODEL_IMAGE_EXTERNAL;
    out.name = options->custom_shader;
    return out;
  }
  if (options->custom_skin != NULL) {
    for (size_t i = 0; i < options->custom_skin_count; i++)
      if (strcmp(options->custom_skin[i].name, name) == 0) {
        out.kind = QA_MODEL_IMAGE_EXTERNAL;
        out.name = options->custom_skin[i].shader;
        return out;
      }
    out.kind = QA_MODEL_IMAGE_DEFAULT;
    out.reason = "missing-skin-surface";
    return out;
  }
  if (shader_count == 0) {
    out.kind = QA_MODEL_IMAGE_DEFAULT;
    out.reason = "no-skin";
    return out;
  }
  double index = entity->model.kind == QA_DECODED_MODEL_Q3_MESH
                     ? fmod(entity->skin, (double)shader_count)
                     : (entity->skin >= 0 && entity->skin < (double)shader_count
                            ? entity->skin
                            : 0);
  out.kind = QA_MODEL_IMAGE_EXTERNAL;
  out.name = *AT_PTR(const char *, shaders, shader_count, index, "model skin");
  return out;
}

static double lod_index(const qa_scene_entity *entity, double count,
                        double radius,
                        const qa_model_preparation_context *context,
                        const qa_model_source_options *options) {
  double lod = 0;
  if (count > 1) {
    qa_vec3 forward = context->camera.axis.vectors[0];
    double distance = fr(qa_dot3(forward, entity->transform.origin) -
                         qa_dot3(forward, context->camera.origin));
    const double *m = context->camera.projection.values;
    double projected =
        distance > 0
            ? qa_js_min(fr(fr(radius * m[5] - distance * m[9] + m[13]) /
                      fr(radius * m[7] - distance * m[11] + m[15])), 1)
            : 0;
    double fraction = projected != 0
                          ? fr(1 - fr(projected * qa_js_min(options->has_lod_scale
                                                            ? options->lod_scale
                                                            : 5, 20)))
                          : 0;
    lod = qa_js_max(0, qa_js_min(count - 1, trunc(fr(fraction * count))));
  }
  return qa_js_max(0, qa_js_min(count - 1, lod + (options->has_lod_bias ? options->lod_bias : 0)));
}

qa_model_vertex *qa_interpolate_scene_md2(
    const qa_q2_alias_model *model, const qa_scene_entity *entity,
    double frame, double old_frame, double back_lerp, bool shell,
    size_t *out_count) {
  const qa_q2_alias_frame *current =
      AT_PTR(qa_q2_alias_frame, model->frames, model->frame_count, frame, "MD2 frame");
  const qa_q2_alias_frame *old =
      AT_PTR(qa_q2_alias_frame, model->frames, model->frame_count, old_frame, "MD2 old frame");
  qa_vec3 delta = qa_model_local_delta(
      entity->transform, qa_sub3(entity->previous_origin, entity->transform.origin));
  double front = 1 - back_lerp;
  qa_vec3 move = {back_lerp * (delta.x + old->translation.x) + front * current->translation.x,
                  back_lerp * (delta.y + old->translation.y) + front * current->translation.y,
                  back_lerp * (delta.z + old->translation.z) + front * current->translation.z};
  size_t count = current->compressed_vertex_count;
  if (old->compressed_vertex_count != count ||
      current->frame.vertex_count != count)
    qa_range_error("MD2 frame vertex counts differ");
  qa_model_vertex *result = calloc(count ? count : 1, sizeof *result);
  if (result == NULL) { *out_count = 0; return NULL; }
  double back_x = back_lerp * old->scale.x, back_y = back_lerp * old->scale.y,
         back_z = back_lerp * old->scale.z;
  double front_x = front * current->scale.x, front_y = front * current->scale.y,
         front_z = front * current->scale.z;
  for (size_t i = 0; i < count; i++) {
    const qa_packed_alias_vertex *vertex = &current->compressed_vertices[i];
    const qa_packed_alias_vertex *previous = &old->compressed_vertices[i];
    qa_vec3 normal = current->frame.vertices[i].normal;
    result[i].normal = normal;
    result[i].position = (qa_vec3){
        fr(move.x + previous->position[0] * back_x + vertex->position[0] * front_x +
           (shell ? normal.x * 4 : 0)),
        fr(move.y + previous->position[1] * back_y + vertex->position[1] * front_y +
           (shell ? normal.y * 4 : 0)),
        fr(move.z + previous->position[2] * back_z + vertex->position[2] * front_z +
           (shell ? normal.z * 4 : 0))};
  }
  *out_count = count;
  return result;
}

/* --- prepareEntityAtTransform machinery --- */

typedef struct append_vertex {
  qa_model_vertex vertex;
  qa_vec2 tex_coord;
  bool has_color;
  qa_vec4 color;
} append_vertex;

typedef struct prep {
  const qa_model_preparation_context *context;
  qa_scene_entity entity;
  qa_model_source_options options;
  qa_vec4 color;
  bool has_shell;
  bool translucent;
  double alpha;
  bool lighting_ready, has_lighting;
  qa_prepared_vertex_lighting lighting;
  qa_prepared_model_surface *surfaces;
  size_t count, capacity;
  bool has_fog;
  qa_vec3 fog_origin;
  double fog_radius;
  bool oom;
} prep;

static bool is_shadow(const prep *p) { return str_is(p->context->purpose, "shadow"); }

static void ensure_lighting(prep *p) {
  if (is_shadow(p) || p->lighting_ready) return;
  p->lighting_ready = true;
  if (p->context->prepare_vertex_lighting != NULL) {
    p->lighting = p->context->prepare_vertex_lighting(
        p->context->prepare_vertex_lighting_ctx, &p->entity, p->options);
    p->has_lighting = p->lighting.shade != NULL;
  }
}

static qa_material_vertex local_vertex(prep *p, append_vertex vertex,
                                       double corner, bool unlit) {
  qa_material_vertex out = {0};
  out.position = vertex.vertex.position;
  out.normal = vertex.vertex.normal;
  out.tex_coord = vertex.tex_coord;
  out.lightmap_coord = (qa_vec2){0, 0};
  qa_vec4 base = vertex.has_color ? vertex.color : p->color;
  if (is_shadow(p)) {
    out.color = base;
    return out;
  }
  bool has_sampled = false;
  qa_vec3 sampled = {1, 1, 1};
  if (p->context->light_vertex != NULL) {
    sampled = p->context->light_vertex(p->context->light_vertex_ctx, &p->entity,
                                       vertex.vertex.normal, vertex.vertex.position);
    has_sampled = true;
  }
  qa_vec3 light;
  if (p->has_lighting)
    light = p->lighting.shade(p->lighting.shade_ctx, vertex.vertex.normal,
                              vertex.vertex.position, corner);
  else if (flags_is(&p->entity, "q2"))
    light = qa_q2_alias_light(p->entity.flags_bits,
                              has_sampled ? sampled : (qa_vec3){1, 1, 1},
                              p->context->time_seconds, false,
                              p->options.has_infrared && p->options.infrared);
  else
    light = has_sampled ? sampled : (qa_vec3){1, 1, 1};
  out.color = (unlit && !p->has_shell)
                  ? base
                  : (qa_vec4){base.x * light.x, base.y * light.y,
                              base.z * light.z, base.w};
  return out;
}

static void push_surface(prep *p, const char *name, char *owned_name,
                         qa_model_image_selection image,
                         qa_material_vertex *local, size_t vertex_count,
                         qa_material_vertex *world,
                         const double *indices, size_t index_count,
                         double *owned_indices, bool unlit) {
  if (p->count == p->capacity) {
    size_t next = p->capacity ? p->capacity * 2 : 4;
    qa_prepared_model_surface *grown =
        realloc(p->surfaces, next * sizeof *grown);
    if (grown == NULL) {
      p->oom = true;
      free(local); free(world); free(owned_indices); free(owned_name);
      free(image.owned_name);
      return;
    }
    p->surfaces = grown;
    p->capacity = next;
  }
  qa_prepared_model_surface *surface = &p->surfaces[p->count];
  memset(surface, 0, sizeof *surface);
  const qa_scene_entity *entity = &p->entity;
  bool depth_hack = flags_is(entity, "q1")
                        ? (p->options.has_view_model && p->options.view_model)
                        : flags_is(entity, "q2")
                              ? bit(entity->flags_bits, 16)
                              : bit(entity->flags_bits, 8);
  surface->surface_index = (double)p->count;
  surface->has_fog_sphere = p->has_fog;
  surface->fog_local_origin = p->fog_origin;
  surface->fog_radius = p->fog_radius;
  surface->options = p->options;
  surface->name = owned_name != NULL ? owned_name : name;
  surface->owned_name = owned_name;
  surface->entity = *entity;
  surface->transform = entity->transform;
  surface->image = image;
  surface->local_geometry = (qa_material_geometry){local, vertex_count, indices, index_count};
  surface->geometry = (qa_material_geometry){world != NULL ? world : local,
                                             vertex_count, indices, index_count};
  surface->owned_local_vertices = local;
  surface->owned_world_vertices = world;
  surface->owned_indices = owned_indices;
  surface->depth_range[0] = 0;
  surface->depth_range[1] = depth_hack ? 0.3 : 1;
  qa_decoded_model_kind kind = entity->model.kind;
  surface->cull =
      (kind == QA_DECODED_MODEL_Q1_SPRITE || kind == QA_DECODED_MODEL_Q2_SPRITE)
          ? "none"
          : (kind == QA_DECODED_MODEL_Q1_ALIAS || kind == QA_DECODED_MODEL_Q2_ALIAS ||
             (kind == QA_DECODED_MODEL_MD5 &&
              (str_is(entity->model.as.md5->skin_selection.kind, "q1-mdl-replacement") ||
               str_is(entity->model.as.md5->skin_selection.kind, "q2-md2-replacement"))))
                ? "front"
                : "back";
  surface->alpha_test = kind == QA_DECODED_MODEL_Q1_SPRITE
                            ? "gt0"
                            : (kind == QA_DECODED_MODEL_Q2_SPRITE && p->alpha == 1)
                                  ? "ge128"
                                  : "none";
  surface->translucent = p->translucent;
  surface->unlit = unlit || p->has_shell;
  surface->mirror_weapon = flags_is(entity, "q2") && bit(entity->flags_bits, 4) &&
                           p->options.has_left_hand && p->options.left_hand == 1;
  p->count++;
}

static void append_geometry(prep *p, const char *name,
                            qa_model_image_selection image,
                            qa_material_vertex *local, size_t vertex_count,
                            const double *indices, size_t index_count,
                            double *owned_indices, bool unlit, bool world) {
  if (world) {
    push_surface(p, name, NULL, image, local, vertex_count, NULL, indices,
                 index_count, owned_indices, unlit);
    return;
  }
  qa_material_vertex *world_verts =
      calloc(vertex_count ? vertex_count : 1, sizeof *world_verts);
  if (world_verts == NULL) {
    p->oom = true;
    free(local); free(owned_indices); free(image.owned_name);
    return;
  }
  for (size_t i = 0; i < vertex_count; i++) {
    world_verts[i] = local[i];
    world_verts[i].position =
        qa_model_world_point(p->entity.transform, local[i].position);
    world_verts[i].normal =
        qa_model_world_direction(p->entity.transform, local[i].normal);
  }
  push_surface(p, name, NULL, image, local, vertex_count, world_verts, indices,
               index_count, owned_indices, unlit);
}

static void append_vertices(prep *p, const char *name,
                            qa_model_image_selection image,
                            const append_vertex *vertices, size_t vertex_count,
                            const double *indices, size_t index_count,
                            double *owned_indices, bool unlit, bool world) {
  ensure_lighting(p);
  qa_material_vertex *local =
      calloc(vertex_count ? vertex_count : 1, sizeof *local);
  if (local == NULL) {
    p->oom = true;
    free(owned_indices); free(image.owned_name);
    return;
  }
  for (size_t i = 0; i < vertex_count; i++)
    local[i] = local_vertex(p, vertices[i], (double)i, unlit);
  append_geometry(p, name, image, local, vertex_count, indices, index_count,
                  owned_indices, unlit, world);
}

static const char *cull_geometry(bool has_bounds, qa_bounds bounds,
                                 const qa_model_preparation_context *context) {
  if (context->no_cull || context->frustum == NULL || !has_bounds) return "clip";
  bool clipped = false;
  for (size_t i = 0; i < context->frustum_count; i++) {
    qa_plane plane = context->frustum[i];
    qa_vec3 near = {plane.normal.x >= 0 ? bounds.min.x : bounds.max.x,
                    plane.normal.y >= 0 ? bounds.min.y : bounds.max.y,
                    plane.normal.z >= 0 ? bounds.min.z : bounds.max.z};
    qa_vec3 far = {plane.normal.x >= 0 ? bounds.max.x : bounds.min.x,
                   plane.normal.y >= 0 ? bounds.max.y : bounds.min.y,
                   plane.normal.z >= 0 ? bounds.max.z : bounds.min.z};
    if (qa_dot3(far, plane.normal) <= plane.distance) return "out";
    if (qa_dot3(near, plane.normal) <= plane.distance) clipped = true;
  }
  return clipped ? "clip" : "in";
}

static qa_model_image_selection white_image(void) {
  qa_model_image_selection image = {0};
  image.kind = QA_MODEL_IMAGE_WHITE;
  return image;
}
static qa_model_image_selection indexed_image(char *owned_name, double width,
                                              double height, qa_byte_view pixels,
                                              bool has_transparent,
                                              double transparent, bool fullbright) {
  qa_model_image_selection image = {0};
  image.kind = QA_MODEL_IMAGE_INDEXED;
  image.owned_name = owned_name;
  image.name = owned_name;
  image.width = width;
  image.height = height;
  image.pixels = pixels;
  image.has_transparent_index = has_transparent;
  image.transparent_index = transparent;
  image.fullbright = fullbright;
  return image;
}

typedef struct md2_map_ctx { prep *p; bool unlit; } md2_map_ctx;
static void md2_map(void *ctx, qa_model_vertex vertex, qa_vec2 tex_coord,
                    size_t corner, void *out_element) {
  md2_map_ctx *map = ctx;
  append_vertex av = {vertex, tex_coord, false, {0, 0, 0, 0}};
  *(qa_material_vertex *)out_element =
      local_vertex(map->p, av, (double)corner, map->unlit);
}

static bool prepare_at_transform(qa_scene_entity entity,
                                 const qa_scene_entity *source,
                                 const qa_model_preparation_context *context,
                                 qa_prepared_model_entity *out);

static bool prepare_at_transform(qa_scene_entity entity_value,
                                 const qa_scene_entity *source,
                                 const qa_model_preparation_context *context,
                                 qa_prepared_model_entity *out) {
  memset(out, 0, sizeof *out);
  prep p = {0};
  p.context = context;
  p.options = context->options != NULL
                  ? context->options(context->options_ctx, source)
                  : (qa_model_source_options){0};
  qa_repaired_frames pose = qa_repair_frames(&entity_value);
  qa_scene_entity native = entity_value;
  qa_scene_entity entity = entity_value;
  if (entity.model.kind != QA_DECODED_MODEL_Q1_ALIAS || !p.options.has_indexed_skin) {
    qa_model_replacement_policy policy = context->has_model_policy
                                             ? context->model_policy
                                             : qa_default_model_replacement_policy;
    entity = qa_select_model_entity(&entity, context->camera.origin, policy,
                                    context->purpose);
  }
  p.entity = entity;
  double bits = entity.flags_bits;
  qa_vec3 shell_color;
  p.has_shell = flags_is(&entity, "q2") && qa_q2_shell_color(bits, &shell_color);
  bool portal = context->camera.clip_is_portal;
  bool personal = flags_is(&entity, "q3") && bit(bits, 2) && !portal;
  bool invisible_weapon =
      (flags_is(&entity, "q3") && bit(bits, 4) && portal) ||
      (flags_is(&entity, "q2") && bit(bits, 4) && p.options.has_left_hand &&
       p.options.left_hand == 2);
  p.translucent = entity.color.w < 1 ||
                  (flags_is(&entity, "q2") && bit(bits, 32 | 128));
  p.alpha = p.translucent ? entity.color.w : 1;
  qa_vec4 entity_color = entity.color;
  entity_color.w = p.alpha;
  p.color = qa_byte_color(entity_color);
  double lod = 0;
  double frame = pose.frame, previous_frame = pose.previous_frame;
  double back_lerp = pose.back_lerp;
  bool has_bounds = false;
  qa_bounds bounds = {0};
  if (native.model.kind == QA_DECODED_MODEL_Q1_ALIAS) {
    bounds = qa_model_world_bounds(native.transform, native.model.as.q1_alias->bounds);
    has_bounds = true;
  } else if (native.model.kind == QA_DECODED_MODEL_Q2_ALIAS && !bit(bits, 128)) {
    qa_bounds local = qa_empty_bounds();
    double wanted[2] = {frame, previous_frame};
    for (int i = 0; i < 2; i++) {
      const qa_q2_alias_frame *source_frame = AT_PTR(
          qa_q2_alias_frame, native.model.as.q2_alias->frames,
          native.model.as.q2_alias->frame_count, wanted[i], "MD2 bounds frame");
      local = qa_add_point_to_bounds(local, source_frame->translation);
      local = qa_add_point_to_bounds(
          local, qa_add3(source_frame->translation,
                         qa_scale3(source_frame->scale, 255)));
    }
    bounds = qa_model_world_bounds(native.transform, local);
    has_bounds = true;
  }
  bool weapon = flags_is(&entity, "q2") &&
                ((p.options.has_view_model && p.options.view_model) || bit(bits, 4));
  const char *source_cull =
      invisible_weapon ? "out"
      : weapon         ? "in"
      : !has_bounds    ? NULL
                       : cull_geometry(has_bounds, bounds, context);
  const qa_decoded_model *model = &entity.model;
  if (!str_is(source_cull, "out") && flags_is(&entity, "q2") && bit(bits, 128)) {
    if (context->palette_color == NULL)
      qa_range_error("Q2 beam preparation requires its source palette");
    qa_vec3 palette = context->palette_color(
        context->palette_color_ctx, &entity,
        (double)(qa_int32(entity.skin) & 255));
    qa_vec4 beam_color = {palette.x, palette.y, palette.z, entity.color.w * 255};
    qa_model_owned_geometry geometry = qa_q2_beam_geometry(
        entity.transform.origin, entity.previous_origin, frame, beam_color);
    append_vertex *verts =
        calloc(geometry.vertex_count ? geometry.vertex_count : 1, sizeof *verts);
    if (verts == NULL) p.oom = true;
    else {
      for (size_t i = 0; i < geometry.vertex_count; i++) {
        verts[i].vertex = (qa_model_vertex){geometry.vertices[i].position,
                                            geometry.vertices[i].normal};
        verts[i].tex_coord = geometry.vertices[i].tex_coord;
        verts[i].has_color = true;
        verts[i].color = geometry.vertices[i].color;
      }
      append_vertices(&p, "beam", white_image(), verts, geometry.vertex_count,
                      geometry.indices, geometry.index_count, geometry.indices,
                      true, true);
      free(verts);
    }
    free(geometry.vertices);
  } else if (!str_is(source_cull, "out")) {
    switch (model->kind) {
      case QA_DECODED_MODEL_Q1_ALIAS: {
        const qa_q1_alias_model *mdl = model->as.q1_alias;
        double sync = p.options.has_sync_base ? p.options.sync_base : 0;
        const qa_q1_alias_frame_set *cur_set =
            AT_PTR(qa_q1_alias_frame_set, mdl->frames, mdl->frame_count, frame, "MDL frame");
        const qa_q1_alias_frame_set *old_set = AT_PTR(
            qa_q1_alias_frame_set, mdl->frames, mdl->frame_count, previous_frame, "MDL old frame");
        const qa_q1_alias_frame *current =
            qa_q1_sample_alias_frame(cur_set, context->time_seconds, sync);
        const qa_q1_alias_frame *previous =
            qa_q1_sample_alias_frame(old_set, context->time_seconds, sync);
        size_t pose_count = 0;
        /* TS interpolateAliasFrames with its module PoseLru: the pose is borrowed from the cache (MDL frames
           are forgotten from it in qa_q1_alias_model_destroy before they are freed). */
        const qa_model_vertex *posed = qa_interpolate_alias_frames_cached(
            &current->frame, &previous->frame, back_lerp, (qa_vec3){0, 0, 0},
            &pose_count);
        qa_alias_geometry geometry = {0};
        qa_build_mdl_geometry(mdl, posed, pose_count, &geometry);
        double skin = (entity.skin >= 0 && entity.skin < (double)mdl->skin_count)
                          ? entity.skin : 0;
        const qa_timed_frames *skin_frames =
            AT_PTR(qa_timed_frames, mdl->skins, mdl->skin_count, skin, "MDL skin");
        const qa_byte_view *pixels = qa_sample_timed_frame(
            skin_frames, context->time_seconds, sync);
        long skin_frame = 0;
        if (skin_frames->is_group) {
          skin_frame = -1;
          for (size_t i = 0; i < skin_frames->frame_count; i++)
            if (skin_frames->frames[i].frame == (const void *)pixels) {
              skin_frame = (long)i;
              break;
            }
        }
        qa_model_image_selection image;
        if (!p.options.has_indexed_skin) {
          char *name = dup_format("%s:skin:%lld:%lld", entity.resource.id,
                                  (long long)skin, (long long)skin_frame);
          image = indexed_image(name, mdl->skin_width, mdl->skin_height,
                                pixels != NULL ? *pixels : (qa_byte_view){0},
                                false, 0, true);
        } else {
          image = indexed_image(NULL, p.options.indexed_skin.width,
                                p.options.indexed_skin.height,
                                p.options.indexed_skin.pixels, false, 0, true);
          image.name = p.options.indexed_skin.name;
        }
        append_vertex *verts = calloc(
            geometry.vertex_count ? geometry.vertex_count : 1, sizeof *verts);
        if (verts == NULL) { p.oom = true; free(image.owned_name); }
        else {
          for (size_t i = 0; i < geometry.vertex_count; i++) {
            verts[i].vertex = geometry.vertices[i].vertex;
            verts[i].tex_coord = geometry.vertices[i].tex_coord;
          }
          append_vertices(&p, "alias", image, verts, geometry.vertex_count,
                          geometry.indices, geometry.index_count, geometry.indices,
                          false, false);
          free(verts);
        }
        free(geometry.vertices);
        break;
      }
      case QA_DECODED_MODEL_Q2_ALIAS: {
        const qa_q2_alias_model *md2 = model->as.q2_alias;
        size_t vertex_count = 0;
        qa_model_vertex *posed = qa_interpolate_scene_md2(
            md2, &entity, frame, previous_frame, back_lerp, p.has_shell,
            &vertex_count);
        qa_model_image_selection image =
            !p.has_shell
                ? selected_shader("alias", md2->skins.values, md2->skins.count,
                                  &entity, &p.options)
                : white_image();
        ensure_lighting(&p);
        md2_map_ctx map = {&p, p.has_shell};
        double *indices = NULL;
        size_t count = 0;
        qa_material_vertex *local = qa_map_md2_geometry(
            md2, posed, vertex_count, sizeof(qa_material_vertex), md2_map,
            &map, &indices, &count);
        free(posed);
        append_geometry(&p, "alias", image, local, count, indices, count,
                        indices, p.has_shell, false);
        break;
      }
      case QA_DECODED_MODEL_Q3_MESH: {
        const qa_q3_mesh_model *md3 = model->as.q3_mesh;
        const qa_q3_mesh_model *choices_fallback[1] = {md3};
        const qa_q3_mesh_model *const *choices =
            p.options.q3_lods != NULL ? p.options.q3_lods : choices_fallback;
        size_t choice_count = p.options.q3_lods != NULL ? p.options.q3_lod_count : 1;
        const qa_q3_mesh_frame *frame_row = AT_PTR(
            qa_q3_mesh_frame, md3->frames, md3->frame_count, frame, "MD3 frame");
        lod = lod_index(&entity, (double)choice_count,
                        qa_radius_from_bounds(frame_row->bounds), context, &p.options);
        const qa_q3_mesh_model *selected =
            *AT_PTR(const qa_q3_mesh_model *, choices, choice_count, lod, "MD3 LOD");
        if (selected == NULL)
          qa_range_error("Missing source MD3 LOD slot %s", QA_JS_NUMBER(lod));
        const qa_q3_mesh_frame *fog_frame = AT_PTR(
            qa_q3_mesh_frame, selected->frames, selected->frame_count, frame, "MD3 fog frame");
        p.has_fog = true;
        p.fog_origin = fog_frame->local_origin;
        p.fog_radius = fog_frame->radius;
        bool culled = false;
        if (!context->no_cull && context->frustum != NULL &&
            !str_is(source_cull, "in") && trunc(entity.skin) == entity.skin &&
            entity.skin >= 0) {
          qa_bounds envelope;
          if (qa_md3_world_envelope(selected, frame, previous_frame, back_lerp,
                                    entity.transform, &envelope) &&
              str_is(cull_geometry(true, envelope, context), "out")) {
            bounds = envelope;
            has_bounds = true;
            culled = true;
          }
        }
        if (!culled)
          for (size_t si = 0; si < selected->surface_count; si++) {
            const qa_q3_mesh_surface *surface = &selected->surfaces[si];
            (void)AT_PTR(const qa_model_vertex *, surface->frame_vertices,
                         surface->frame_count, frame, "MD3 frame");
            size_t fi = (size_t)frame;
            const qa_model_vertex *current = surface->frame_vertices[fi];
            size_t vcount = surface->frame_vertex_counts[fi];
            qa_model_vertex *interpolated = NULL;
            if (back_lerp != 0) {
              (void)AT_PTR(const qa_model_vertex *, surface->frame_vertices,
                           surface->frame_count, previous_frame, "MD3 old frame");
              interpolated = qa_md3_interpolate_frames(
                  current, surface->frame_vertices[(size_t)previous_frame],
                  vcount, back_lerp);
              current = interpolated;
            }
            append_vertex *verts = calloc(vcount ? vcount : 1, sizeof *verts);
            if (verts == NULL) { p.oom = true; free(interpolated); break; }
            for (size_t i = 0; i < vcount; i++) {
              verts[i].vertex = current[i];
              verts[i].tex_coord = *AT_PTR(qa_vec2, surface->texture_coordinates,
                                           surface->texture_coordinate_count,
                                           (double)i, "MD3 UV");
            }
            append_vertices(&p, surface->name,
                            selected_shader(surface->name, surface->shaders.values,
                                            surface->shaders.count, &entity, &p.options),
                            verts, vcount, surface->indices.values,
                            surface->indices.count, NULL, false, false);
            free(verts);
            free(interpolated);
          }
        break;
      }
      case QA_DECODED_MODEL_MD4: {
        const qa_md4_model *md4 = model->as.md4;
        const qa_md4_frame *frame_row =
            AT_PTR(qa_md4_frame, md4->frames, md4->frame_count, frame, "MD4 frame");
        lod = lod_index(&entity, (double)md4->lod_count, frame_row->radius,
                        context, &p.options);
        const qa_md4_lod *lod_row =
            AT_PTR(qa_md4_lod, md4->lods, md4->lod_count, lod, "MD4 LOD");
        for (size_t si = 0; si < lod_row->surface_count; si++) {
          const qa_md4_surface *surface = &lod_row->surfaces[si];
          size_t vcount = 0;
          qa_model_vertex *posed = qa_skin_md4_surface(
              md4, surface, frame, previous_frame, back_lerp, &vcount);
          append_vertex *verts = calloc(vcount ? vcount : 1, sizeof *verts);
          double *indices = malloc((surface->triangle_count * 3 + 1) * sizeof *indices);
          if (verts == NULL || indices == NULL) {
            p.oom = true; free(verts); free(indices); free(posed); break;
          }
          for (size_t i = 0; i < vcount; i++) {
            verts[i].vertex = posed[i];
            verts[i].tex_coord =
                AT_PTR(qa_md4_vertex, surface->vertices, surface->vertex_count,
                       (double)i, "MD4 UV")->tex_coords;
          }
          for (size_t i = 0; i < surface->triangle_count; i++)
            memcpy(indices + i * 3, surface->triangles[i].indices,
                   3 * sizeof *indices);
          const char *shaders[1] = {surface->shader};
          append_vertices(&p, surface->name,
                          selected_shader(surface->name, shaders, 1, &entity, &p.options),
                          verts, vcount, indices, surface->triangle_count * 3,
                          indices, false, false);
          free(verts);
          free(posed);
        }
        break;
      }
      case QA_DECODED_MODEL_MD5: {
        const qa_md5_model *md5 = model->as.md5;
        const qa_md5_skin_selection *selection = &md5->skin_selection;
        double sync = p.options.has_sync_base ? p.options.sync_base : 0;
        bool has_elapsed = str_is(selection->kind, "q1-mdl-replacement") &&
                           str_is(selection->timing_kind, "elapsed-time");
        double elapsed_frame = 0;
        if (has_elapsed) {
          double elapsed = floor((context->time_seconds + sync) * selection->timing_frame_rate);
          double count = (double)md5->frame_count;
          elapsed_frame = fmod(fmod(elapsed, count) + count, count);
        }
        double pose_frame = has_elapsed ? elapsed_frame : frame;
        double pose_previous = has_elapsed ? elapsed_frame : previous_frame;
        double pose_lerp = has_elapsed ? 0 : back_lerp;
        const qa_skeleton_joint_pose *joints;
        size_t joint_count;
        bool owned_joints = false;
        if (!entity.pose.is_frame) {
          joints = entity.pose.joints;
          joint_count = entity.pose.joint_count;
        } else {
          joints = qa_sample_md5_pose(md5, pose_frame, pose_previous, pose_lerp,
                                      &owned_joints, &joint_count);
        }
        /* Per-mesh images (selected before the shadow-body test). */
        qa_model_image_selection *images =
            calloc(md5->mesh_count ? md5->mesh_count : 1, sizeof *images);
        char **owned_shaders = calloc(md5->mesh_count ? md5->mesh_count : 1,
                                      sizeof *owned_shaders);
        if (images == NULL || owned_shaders == NULL) {
          p.oom = true;
          free(images); free(owned_shaders);
          if (owned_joints) free((void *)joints);
          break;
        }
        for (size_t mi = 0; mi < md5->mesh_count; mi++) {
          char mesh_name[32];
          (void)snprintf(mesh_name, sizeof mesh_name, "mesh%zu", mi);
          if (p.has_shell) { images[mi] = white_image(); continue; }
          if (str_is(selection->kind, "q2-md2-replacement")) {
            images[mi] = selected_shader(mesh_name, selection->skins.values,
                                         selection->skins.count, &entity, &p.options);
          } else if (str_is(selection->kind, "q1-mdl-replacement")) {
            const qa_timed_frames *groups = NULL;
            size_t group_count = 0;
            if (mi < selection->mesh_skin_group_row_count) {
              groups = selection->mesh_skin_group_rows[mi];
              group_count = selection->mesh_skin_group_row_counts[mi];
            }
            if (groups == NULL) { /* TS at() throws */
              (void)AT_PTR(qa_timed_frames, groups, 0, (double)mi, "Q1 replacement mesh skin");
            }
            const char **names = calloc(group_count ? group_count : 1, sizeof *names);
            /* Owned "${name}.lmp" strings live until selected_shader copies
             * the pointer; retain them in owned_shaders[mi] chain via a
             * single joined allocation per name (leak-free enough: freed
             * with the last name pointer kept simple below). */
            for (size_t gi = 0; gi < group_count; gi++) {
              const char *const *sampled = qa_sample_timed_frame(
                  &groups[gi], context->time_seconds, sync);
              const char *base = sampled != NULL ? *sampled : "";
              names[gi] = dup_format("%s.lmp", base);
            }
            /* Keep only the chosen string; free the rest after selection. */
            images[mi] = selected_shader(mesh_name, names, group_count, &entity, &p.options);
            for (size_t gi = 0; gi < group_count; gi++)
              if (names[gi] != images[mi].name) free((void *)names[gi]);
              else owned_shaders[mi] = (char *)names[gi];
            free(names);
            if (owned_shaders[mi] != NULL) images[mi].owned_name = owned_shaders[mi];
          } else {
            const char *shaders[1] = {md5->meshes[mi].shader};
            images[mi] = selected_shader(mesh_name, shaders, 1, &entity, &p.options);
          }
        }
        bool skip_body = false;
        if (context->retain_shadow_body != NULL && is_shadow(&p) &&
            !p.has_shell && !p.options.has_model_beam) {
          qa_render_scene_shadow_sphere envelope;
          if (qa_md5_shadow_envelope(md5->meshes, md5->mesh_count, joints,
                                     joint_count, entity.transform, &envelope) &&
              !context->retain_shadow_body(context->retain_shadow_body_ctx,
                                           &entity, envelope, images,
                                           md5->mesh_count, p.options))
            skip_body = true;
        }
        if (!skip_body)
          for (size_t mi = 0; mi < md5->mesh_count; mi++) {
            const qa_md5_mesh *mesh = &md5->meshes[mi];
            size_t vcount = 0;
            qa_model_vertex *skinned =
                qa_skin_md5_mesh(mesh, joints, joint_count, &vcount);
            append_vertex *verts = calloc(vcount ? vcount : 1, sizeof *verts);
            if (verts == NULL) { p.oom = true; free(skinned); break; }
            for (size_t i = 0; i < vcount; i++) {
              verts[i].vertex = skinned[i];
              if (p.has_shell)
                verts[i].vertex.position = qa_add3(
                    skinned[i].position, qa_scale3(skinned[i].normal, 4));
              verts[i].tex_coord =
                  AT_PTR(qa_md5_mesh_vertex, mesh->vertices, mesh->vertex_count,
                         (double)i, "MD5 UV")->tex_coord;
            }
            char mesh_name[32];
            (void)snprintf(mesh_name, sizeof mesh_name, "mesh%zu", mi);
            /* name must outlive the surface: duplicate it */
            char *owned = dup_format("%s", mesh_name);
            qa_model_image_selection image =
                *AT_PTR(qa_model_image_selection, images, md5->mesh_count,
                        (double)mi, "MD5 material");
            ensure_lighting(&p);
            qa_material_vertex *local =
                calloc(vcount ? vcount : 1, sizeof *local);
            if (local == NULL) {
              p.oom = true; free(owned); free(image.owned_name);
            } else {
              for (size_t i = 0; i < vcount; i++)
                local[i] = local_vertex(&p, verts[i], (double)i, p.has_shell);
              /* push with owned surface name */
              qa_material_vertex *world =
                  calloc(vcount ? vcount : 1, sizeof *world);
              if (world == NULL) {
                p.oom = true; free(local); free(owned); free(image.owned_name);
              } else {
                for (size_t i = 0; i < vcount; i++) {
                  world[i] = local[i];
                  world[i].position = qa_model_world_point(entity.transform, local[i].position);
                  world[i].normal = qa_model_world_direction(entity.transform, local[i].normal);
                }
                push_surface(&p, owned, owned, image, local, vcount, world,
                             mesh->indices.values, mesh->indices.count, NULL,
                             p.has_shell);
              }
            }
            images[mi].owned_name = NULL; /* consumed (or freed) above */
            owned_shaders[mi] = NULL;
            free(verts);
            free(skinned);
          }
        for (size_t mi = 0; mi < md5->mesh_count; mi++) free(images[mi].owned_name);
        free(images);
        free(owned_shaders);
        if (owned_joints) free((void *)joints);
        break;
      }
      case QA_DECODED_MODEL_Q1_SPRITE: {
        const qa_q1_sprite_model *spr = model->as.q1_sprite;
        double sync = p.options.has_sync_base ? p.options.sync_base : 0;
        const qa_timed_frames *frames =
            AT_PTR(qa_timed_frames, spr->frames, spr->frame_count, frame, "SPR frame");
        const qa_sprite_frame *selected =
            qa_sample_timed_frame(frames, context->time_seconds, sync);
        long subframe = 0;
        if (frames->is_group) {
          subframe = -1;
          for (size_t i = 0; i < frames->frame_count; i++)
            if (frames->frames[i].frame == (const void *)selected) {
              subframe = (long)i;
              break;
            }
        }
        qa_sprite_frame sprite = selected != NULL ? *selected : (qa_sprite_frame){0};
        qa_model_owned_geometry geometry = qa_q1_sprite_geometry(
            spr, sprite, entity.transform, context->camera, p.color,
            p.options.has_sprite_roll ? p.options.sprite_roll : 0);
        char *name = dup_format("%s:frame:%lld:%lld", entity.resource.id,
                                (long long)frame, (long long)subframe);
        qa_model_image_selection image = indexed_image(
            name, sprite.width, sprite.height, sprite.pixels, true, 255, true);
        append_vertex *verts = calloc(
            geometry.vertex_count ? geometry.vertex_count : 1, sizeof *verts);
        if (verts == NULL) { p.oom = true; free(name); }
        else {
          for (size_t i = 0; i < geometry.vertex_count; i++) {
            verts[i].vertex = (qa_model_vertex){geometry.vertices[i].position,
                                                geometry.vertices[i].normal};
            verts[i].tex_coord = geometry.vertices[i].tex_coord;
            verts[i].has_color = true;
            verts[i].color = geometry.vertices[i].color;
          }
          append_vertices(&p, "sprite", image, verts, geometry.vertex_count,
                          geometry.indices, geometry.index_count, geometry.indices,
                          true, true);
          free(verts);
        }
        free(geometry.vertices);
        break;
      }
      case QA_DECODED_MODEL_Q2_SPRITE: {
        const qa_q2_sprite_model *sp2 = model->as.q2_sprite;
        const qa_q2_sprite_frame *selected = AT_PTR(
            qa_q2_sprite_frame, sp2->frames, sp2->frame_count, frame, "SP2 frame");
        qa_sprite_extents extents = {
            -selected->origin_x * entity.transform.scale.x,
            (selected->width - selected->origin_x) * entity.transform.scale.x,
            (selected->height - selected->origin_y) * entity.transform.scale.z,
            -selected->origin_y * entity.transform.scale.z};
        qa_model_owned_geometry geometry = qa_sprite_quad(
            entity.transform.origin, context->camera.axis, extents, p.color,
            portal && context->camera.clip_is_portal && context->camera.clip_mirror);
        qa_model_image_selection image = {0};
        image.kind = QA_MODEL_IMAGE_EXTERNAL;
        image.name = selected->image;
        append_vertex *verts = calloc(
            geometry.vertex_count ? geometry.vertex_count : 1, sizeof *verts);
        if (verts == NULL) p.oom = true;
        else {
          for (size_t i = 0; i < geometry.vertex_count; i++) {
            verts[i].vertex = (qa_model_vertex){geometry.vertices[i].position,
                                                geometry.vertices[i].normal};
            verts[i].tex_coord = geometry.vertices[i].tex_coord;
            verts[i].has_color = true;
            verts[i].color = geometry.vertices[i].color;
          }
          append_vertices(&p, "sprite", image, verts, geometry.vertex_count,
                          geometry.indices, geometry.index_count, geometry.indices,
                          true, true);
          free(verts);
        }
        free(geometry.vertices);
        break;
      }
      case QA_DECODED_MODEL_BRUSH_MODEL: break;
    }
  }
  if (!has_bounds) {
    for (size_t si = 0; si < p.count; si++)
      for (size_t vi = 0; vi < p.surfaces[si].geometry.vertex_count; vi++) {
        qa_vec3 position = p.surfaces[si].geometry.vertices[vi].position;
        bounds = qa_add_point_to_bounds(has_bounds ? bounds : qa_empty_bounds(),
                                        position);
        has_bounds = true;
      }
  }
  const char *cull = source_cull != NULL
                         ? source_cull
                         : cull_geometry(has_bounds, bounds, context);
  /* Attachments. */
  qa_scene_entity repaired = entity;
  if (entity.pose.is_frame) {
    repaired.pose.frame = frame;
    repaired.pose.previous_frame = previous_frame;
    repaired.pose.back_lerp = back_lerp;
  }
  qa_prepared_model_entity *attachments = NULL;
  size_t attachment_count = 0, attachment_capacity = 0;
  const char **missing = NULL;
  size_t missing_count = 0, missing_capacity = 0;
  for (size_t ai = 0; ai < entity.attachment_count; ai++) {
    const qa_scene_entity_attachment *attachment = &entity.attachments[ai];
    qa_model_attachment_tag_result tag;
    if (!qa_model_attachment_tag(&repaired, attachment->tag, &tag)) {
      if (missing_count == missing_capacity) {
        missing_capacity = missing_capacity ? missing_capacity * 2 : 4;
        missing = realloc(missing, missing_capacity * sizeof *missing);
        if (missing == NULL) { p.oom = true; break; }
      }
      missing[missing_count++] = attachment->tag;
    } else {
      if (attachment_count == attachment_capacity) {
        attachment_capacity = attachment_capacity ? attachment_capacity * 2 : 4;
        attachments = realloc(attachments,
                              attachment_capacity * sizeof *attachments);
        if (attachments == NULL) { p.oom = true; break; }
      }
      qa_scene_entity attached =
          qa_attach_scene_entity(&repaired, attachment->entity, tag);
      if (!prepare_at_transform(attached, attachment->entity, context,
                                &attachments[attachment_count]))
        p.oom = true;
      attachment_count++;
    }
  }
  double effect_flags =
      model->kind == QA_DECODED_MODEL_Q1_ALIAS
          ? model->as.q1_alias->flags
          : (model->kind == QA_DECODED_MODEL_MD5 &&
             str_is(model->as.md5->skin_selection.kind, "q1-mdl-replacement")
                 ? model->as.md5->skin_selection.flags
                 : 0);
  out->entity = entity;
  out->frame = frame;
  out->previous_frame = previous_frame;
  out->frame_fallback = pose.fallback;
  out->lod = lod;
  out->has_bounds = has_bounds;
  out->bounds = bounds;
  out->cull = cull;
  out->personal_model = personal;
  out->surfaces = p.surfaces;
  out->surface_count = p.count;
  out->attachments = attachments;
  out->attachment_count = attachment_count;
  out->missing_attachments = missing;
  out->missing_attachment_count = missing_count;
  out->model_effect_flags = effect_flags;
  return !p.oom;
}

void qa_prepared_model_surface_destroy(qa_prepared_model_surface *surface) {
  free(surface->owned_local_vertices);
  free(surface->owned_world_vertices);
  free(surface->owned_indices);
  free(surface->owned_name);
  free(surface->image.owned_name);
  memset(surface, 0, sizeof *surface);
}
void qa_prepared_model_entity_destroy(qa_prepared_model_entity *entity) {
  for (size_t i = 0; i < entity->surface_count; i++)
    qa_prepared_model_surface_destroy(&entity->surfaces[i]);
  free(entity->surfaces);
  for (size_t i = 0; i < entity->attachment_count; i++)
    qa_prepared_model_entity_destroy(&entity->attachments[i]);
  free(entity->attachments);
  free((void *)entity->missing_attachments);
  memset(entity, 0, sizeof *entity);
}

bool qa_prepare_scene_entity(const qa_scene_entity *entity,
                             const qa_model_preparation_context *context,
                             qa_prepared_model_entity *out) {
  qa_model_source_options beam_options =
      context->options != NULL
          ? context->options(context->options_ctx, entity)
          : (qa_model_source_options){0};
  if (!beam_options.has_model_beam)
    return prepare_at_transform(*entity, entity, context, out);
  memset(out, 0, sizeof *out);
  qa_vec3 delta = qa_sub3(entity->previous_origin, entity->transform.origin);
  double distance = qa_length3(delta);
  double segment_length = beam_options.model_beam_segment_length == 0
                              ? 30
                              : beam_options.model_beam_segment_length;
  if (!isfinite(segment_length) || segment_length <= 0)
    qa_range_error("Invalid model beam segment length");
  qa_vec3 direction = distance == 0 ? delta : qa_scale3(delta, 1 / distance);
  qa_vec3 angles = qa_vector_to_angles(direction);
  qa_game_random random;
  int64_t slot = entity->actor != NULL ? entity->actor->slot : 0;
  /* new GameRandom(Math.trunc(t * 1000) + slot) throws unless that is a safe integer. */
  double seed = trunc(context->time_seconds * 1000) + (double)slot;
  if (!(fabs(seed) <= 9007199254740991.0)) qa_range_error("Game random seed must be a safe integer");
  qa_game_random_init(&random, (int64_t)seed);
  qa_prepared_model_entity *segments = NULL;
  size_t count = 0, capacity = 0;
  bool oom = false;
  double frames = count_frames(&entity->model);
  for (double offset = 0, made = 0; offset < distance && made < MAX_MODEL_BEAM_SEGMENTS; offset += segment_length, made++) {
    double length = qa_js_min(distance - offset, segment_length);
    double segment_frame = fmod((double)qa_game_random_rand(&random), frames);
    qa_scene_entity segment = *entity;
    segment.transform.origin = qa_add3(
        entity->transform.origin, qa_scale3(direction, offset + length * 0.5));
    qa_vec3 segment_angles = angles;
    segment_angles.z = fmod((double)qa_game_random_rand(&random), 360);
    segment.transform.axis = qa_angles_to_axis(segment_angles);
    segment.transform.scale = (qa_vec3){length / segment_length, 1, 1};
    segment.pose = (qa_model_pose){true, segment_frame, segment_frame, 0, NULL, 0};
    segment.flags_bits = 8192;
    segment.attachments = NULL;
    segment.attachment_count = 0;
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 4;
      segments = realloc(segments, capacity * sizeof *segments);
      if (segments == NULL) { oom = true; break; }
    }
    if (!prepare_at_transform(segment, entity, context, &segments[count]))
      oom = true;
    count++;
  }
  out->entity = *entity;
  out->frame = 0;
  out->previous_frame = 0;
  out->frame_fallback = false;
  out->lod = 0;
  out->has_bounds = false;
  out->cull = "in";
  out->personal_model = false;
  out->attachments = segments;
  out->attachment_count = count;
  out->model_effect_flags = 0;
  return !oom;
}

typedef struct group_list {
  qa_scene_model_group *groups; size_t count, capacity; bool oom;
} group_list;
static void collect_groups(group_list *list,
                           const qa_prepared_model_entity *model,
                           const qa_model_group_context *material) {
  if (!str_is(model->cull, "out") && !model->personal_model) {
    for (size_t si = 0; si < model->surface_count; si++) {
      size_t drawn_count = 0;
      qa_scene_model_group *drawn =
          material->draw(material->ctx, &model->surfaces[si], &drawn_count);
      for (size_t i = 0; i < drawn_count; i++) {
        if (list->count == list->capacity) {
          list->capacity = list->capacity ? list->capacity * 2 : 8;
          list->groups = realloc(list->groups,
                                 list->capacity * sizeof *list->groups);
          if (list->groups == NULL) { list->oom = true; continue; }
        }
        list->groups[list->count++] = drawn[i];
      }
      free(drawn);
    }
  }
  for (size_t ai = 0; ai < model->attachment_count; ai++)
    collect_groups(list, &model->attachments[ai], material);
}

qa_scene_model_group *qa_prepared_model_groups(
    const qa_prepared_model_entity *model,
    const qa_model_group_context *material, size_t *out_count) {
  group_list list = {0};
  collect_groups(&list, model, material);
  *out_count = list.count;
  return list.groups;
}

qa_scene_model_group *qa_prepare_scene_entity_groups(
    const qa_scene_entity *entity, const qa_model_preparation_context *context,
    const qa_model_group_context *material, size_t *out_count) {
  qa_prepared_model_entity prepared;
  *out_count = 0;
  if (!qa_prepare_scene_entity(entity, context, &prepared)) {
    qa_prepared_model_entity_destroy(&prepared);
    return NULL;
  }
  qa_scene_model_group *groups = qa_prepared_model_groups(&prepared, material, out_count);
  qa_prepared_model_entity_destroy(&prepared);
  return groups;
}

double qa_model_projected_radius(const qa_scene_entity *entity, double radius,
                                 const qa_model_preparation_context *context) {
  double distance =
      qa_length3(qa_sub3(entity->transform.origin, context->camera.origin));
  return distance == 0 ? 1 : radius / distance;
}


