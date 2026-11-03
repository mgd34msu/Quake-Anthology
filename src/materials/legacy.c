/* Port of src/materials/legacy.ts. GPL-2.0-or-later. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/materials/evaluate.h"
#include "qa/materials/legacy.h"
#include "qa/materials/shader_types.h"

static float fr(double v) { return (float)v; }
static bool eq(const char *a, const char *b) {
  return a != NULL && strcmp(a, b) == 0;
}
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

const char *qa_q1_surface_kind(const char *name) {
  if (starts_with(name, "sky")) return "sky";
  if (starts_with(name, "{")) return "fence";
  if (!starts_with(name, "*")) return "ordinary";
  if (starts_with(name, "*lava")) return "lava";
  if (starts_with(name, "*slime")) return "slime";
  if (starts_with(name, "*tele")) return "teleport";
  return "water";
}

qa_render_material qa_create_q1_material(const char *name,
    qa_renderer_image texture, qa_surface_lighting lighting,
    qa_create_q1_options options) {
  qa_render_material out;
  memset(&out, 0, sizeof out);
  out.kind = "q1";
  out.name = name;
  out.texture = texture;
  out.lighting = lighting;
  out.surface = qa_q1_surface_kind(name);
  out.alpha = options.has_alpha ? options.alpha : 1;
  out.animation = options.animation;
  out.animation_count = options.animation_count;
  out.alternate_animation = options.alternate_animation;
  out.alternate_animation_count = options.alternate_animation_count;
  return out;
}

struct frame_map {
  const qa_renderer_image *images[10];
  int max_index; /* -1 when empty */
};
static qa_q1_material_frame *build_sequence(struct frame_map frames,
                                            const char *name,
                                            size_t *out_count) {
  size_t count = frames.max_index < 0 ? 0 : (size_t)frames.max_index + 1;
  qa_q1_material_frame *result = NULL;
  if (count > 0) {
    result = malloc(count * sizeof *result);
    if (result == NULL) qa_range_error("legacy allocation failed");
  }
  for (size_t frame = 0; frame < count; frame++) {
    if (frames.images[frame] == NULL)
      qa_range_error("Missing Q1 animation frame %zu in %s", frame, name);
    result[frame].image = *frames.images[frame];
    result[frame].start_tenths = (double)frame * 2;
    result[frame].end_tenths = ((double)frame + 1) * 2;
  }
  *out_count = count;
  return result;
}

qa_q1_texture_animations qa_q1_texture_animations_build(const char *name,
    const qa_legacy_texture_entry *textures, size_t texture_count) {
  qa_q1_texture_animations out;
  memset(&out, 0, sizeof out);
  if (!starts_with(name, "+")) return out;
  struct frame_map primary = {{NULL}, -1}, alternate = {{NULL}, -1};
  const char *suffix = strlen(name) >= 2 ? name + 2 : name + strlen(name);
  for (size_t i = 0; i < texture_count; i++) {
    const char *texture_name = textures[i].name;
    if (!starts_with(texture_name, "+")) continue;
    const char *texture_suffix = strlen(texture_name) >= 2
        ? texture_name + 2 : texture_name + strlen(texture_name);
    if (strcmp(texture_suffix, suffix) != 0) continue;
    int code = strlen(texture_name) >= 2
        ? toupper((unsigned char)texture_name[1]) : 0;
    if (code >= 48 && code <= 57) {
      primary.images[code - 48] = &textures[i].image;
      if (code - 48 > primary.max_index) primary.max_index = code - 48;
    } else if (code >= 65 && code <= 74) {
      alternate.images[code - 65] = &textures[i].image;
      if (code - 65 > alternate.max_index) alternate.max_index = code - 65;
    } else {
      qa_range_error("Bad Q1 animated texture frame %s", texture_name);
    }
  }
  qa_q1_material_frame *animation = build_sequence(primary, name,
                                                   &out.animation_count);
  qa_q1_material_frame *alternate_animation = build_sequence(alternate, name,
      &out.alternate_animation_count);
  int code = strlen(name) >= 2 ? toupper((unsigned char)name[1]) : 0;
  if (code >= 65) {
    out.animation = alternate_animation;
    out.alternate_animation = animation;
    size_t swap = out.animation_count;
    out.animation_count = out.alternate_animation_count;
    out.alternate_animation_count = swap;
  } else {
    out.animation = animation;
    out.alternate_animation = alternate_animation;
  }
  return out;
}

void qa_q1_texture_animations_destroy(qa_q1_texture_animations *animations) {
  free(animations->animation);
  free(animations->alternate_animation);
  animations->animation = NULL;
  animations->alternate_animation = NULL;
  animations->animation_count = 0;
  animations->alternate_animation_count = 0;
}

qa_renderer_image qa_q1_animated_texture(const qa_render_material *material,
                                         double time, bool alternate) {
  const qa_q1_material_frame *frames = material->animation;
  size_t count = material->animation_count;
  if (alternate && material->alternate_animation_count != 0) {
    frames = material->alternate_animation;
    count = material->alternate_animation_count;
  }
  if (count == 0) return material->texture;
  double end = frames[count - 1].end_tenths;
  double ticks = trunc(time * 10);
  double phase = fmod(fmod(ticks, end) + end, end);
  for (size_t i = 0; i < count; i++)
    if (phase >= frames[i].start_tenths && phase < frames[i].end_tenths)
      return frames[i].image;
  qa_range_error("Broken animation cycle in %s", material->name);
}

qa_render_material qa_create_q2_material(const char *name,
    const qa_renderer_image *frames, size_t frame_count,
    qa_surface_lighting lighting, double surface_flags, const char *material) {
  if (frame_count == 0)
    qa_range_error("Q2 material requires at least one texture frame");
  int32_t flags = qa_mat_to_int32(surface_flags);
  qa_render_material out;
  memset(&out, 0, sizeof out);
  out.kind = "q2";
  out.name = name;
  out.frames = frames;
  out.frame_count = frame_count;
  out.lighting = lighting;
  out.surface_flags = surface_flags;
  out.material = material;
  out.flowing = (flags & 64) != 0;
  out.warp = (flags & 8) != 0;
  out.alpha = (flags & 16) != 0 ? 0.33 : (flags & 32) != 0 ? 0.66 : 1;
  return out;
}

qa_renderer_image qa_q2_animated_texture(const qa_render_material *material,
                                         double frame) {
  int64_t count = (int64_t)material->frame_count;
  if (count <= 0)
    qa_range_error("Broken Q2 animation cycle in %s", material->name);
  /* ((Math.trunc(frame) % n) + n) % n on doubles; frames[NaN] is undefined. */
  double index = fmod(fmod(trunc(frame), (double)count) + (double)count, (double)count);
  if (!(index >= 0 && index < (double)count))
    qa_range_error("Broken Q2 animation cycle in %s", material->name);
  return material->frames[(size_t)index];
}

qa_vec2 qa_liquid_tex_coords(qa_vec2 uv, double time,
                             qa_turbulence_profile profile, bool flowing) {
  double scroll = profile == QA_TURBULENCE_Q2 && flowing
      ? -64 * (time * 0.5 - trunc(time * 0.5)) : 0;
  qa_vec2 out = {fr((uv.x + qa_source_turbulence(uv.y * 0.125 + time, profile) +
                     scroll) / 64),
                 fr((uv.y + qa_source_turbulence(uv.x * 0.125 + time,
                                                 profile)) / 64)};
  return out;
}

qa_vec2 qa_q2_flowing_tex_coords(qa_vec2 uv, double time) {
  double scroll = -64 * (time / 40 - trunc(time / 40));
  qa_vec2 out = {uv.x + (scroll == 0 ? -64 : scroll), uv.y};
  return out;
}

qa_vec2 qa_q1_sky_tex_coords(qa_vec3 position, qa_vec3 view_origin,
                             double time, const char *layer) {
  double x = position.x - view_origin.x, y = position.y - view_origin.y;
  double z = (position.z - view_origin.z) * 3;
  double scale = 378 / sqrt(x * x + y * y + z * z);
  double scroll = time * (eq(layer, "solid") ? 8 : 16);
  scroll -= (double)(qa_mat_to_int32(scroll) & ~127);
  qa_vec2 out = {(scroll + x * scale) / 128, (scroll + y * scale) / 128};
  return out;
}

static double *copy_indices(qa_material_geometry geometry) {
  double *indices = malloc(
      (geometry.index_count > 0 ? geometry.index_count : 1) * sizeof *indices);
  if (indices == NULL) qa_range_error("legacy allocation failed");
  if (geometry.index_count > 0)
    memcpy(indices, geometry.indices, geometry.index_count * sizeof *indices);
  return indices;
}

qa_draw_batch *qa_prepare_legacy_material_batches(
    const qa_render_material *material, qa_material_geometry geometry,
    const qa_legacy_material_draw_context *context, size_t *out_count) {
  bool is_q1 = eq(material->kind, "q1"), is_q2 = eq(material->kind, "q2");
  if ((is_q1 && eq(material->surface, "sky")) ||
      (is_q2 && (qa_mat_to_int32(material->surface_flags) & 4) != 0))
    qa_range_error("Sky surfaces require the sky geometry and layer path");
  qa_renderer_image image = is_q1
      ? qa_q1_animated_texture(material, context->time,
                               context->alternate_animation)
      : qa_q2_animated_texture(material, context->animation_frame);
  qa_vec4 tint = context->has_entity_rgba ? context->entity_rgba
                                          : (qa_vec4){255, 255, 255, 255};
  double alpha = material->alpha * tint.w / 255;
  bool blended = alpha < 1;
  bool fence = is_q1 && eq(material->surface, "fence");
  bool has_lightmap = eq(material->lighting.kind, "lightmap") ||
                      eq(material->lighting.kind, "decoupled-lightmap");
  qa_renderer_image lightmap = material->lighting.image;
  qa_render_state state;
  memset(&state, 0, sizeof state);
  state.blend.source = blended ? "src-alpha" : "one";
  state.blend.destination = blended ? "one-minus-src-alpha" : "zero";
  state.depth_test = "less-equal";
  state.depth_write = !blended;
  state.alpha_test = fence ? "gt0" : "none";
  state.cull = context->cull;
  state.depth_range[0] = context->depth_range[0];
  state.depth_range[1] = context->depth_range[1];
  bool q1_liquid = is_q1 && (eq(material->surface, "water") ||
      eq(material->surface, "slime") || eq(material->surface, "lava") ||
      eq(material->surface, "teleport"));
  bool q2_warp = is_q2 && material->warp;
  bool q2_flowing = is_q2 && !material->warp && material->flowing;
  bool q2_flow = is_q2 && material->flowing;
  double intensity = is_q2 && !eq(material->lighting.kind, "vertex") &&
      (material->warp || blended) ? 0.5 : 1;
  bool vertex_lit = eq(material->lighting.kind, "vertex");
  size_t vcount = geometry.vertex_count;
  qa_render_vertex *vertices = malloc(
      (vcount > 0 ? vcount : 1) * sizeof *vertices);
  if (vertices == NULL) qa_range_error("legacy allocation failed");
  for (size_t i = 0; i < vcount; i++) {
    qa_material_vertex vertex = geometry.vertices[i];
    qa_vec2 uv = vertex.tex_coord;
    if (q1_liquid) uv = qa_liquid_tex_coords(uv, context->time, QA_TURBULENCE_Q1, false);
    else if (q2_warp) uv = qa_liquid_tex_coords(uv, context->time, QA_TURBULENCE_Q2, q2_flow);
    else if (q2_flowing) uv = qa_q2_flowing_tex_coords(uv, context->time);
    qa_vec4 color;
    if (vertex_lit)
      color = (qa_vec4){vertex.color.x / 255 * tint.x / 255,
                        vertex.color.y / 255 * tint.y / 255,
                        vertex.color.z / 255 * tint.z / 255, alpha};
    else
      color = (qa_vec4){intensity * tint.x / 255, intensity * tint.y / 255,
                        intensity * tint.z / 255, alpha};
    vertices[i].position = context->project.project(context->project.ctx,
                                                    vertex.position);
    vertices[i].tex_coord = uv;
    vertices[i].color = color;
  }
  qa_draw_batch *batches = calloc(3, sizeof *batches);
  if (batches == NULL) qa_range_error("legacy allocation failed");
  size_t count = 1;
  batches[0].lighting.kind = "vertex";
  if (context->fragment_lighting != NULL && !has_lightmap) {
    batches[0].lighting = *context->fragment_lighting;
    batches[0].lighting.pass = "texture";
  }
  batches[0].indices.values = copy_indices(geometry);
  batches[0].indices.count = geometry.index_count;
  batches[0].vertices = vertices;
  batches[0].vertex_count = vcount;
  batches[0].state = state;
  batches[0].texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
  batches[0].texture.image = image;

  bool paired_converted = false;
  if (has_lightmap) {
    if (blended || context->q1_fog_active) {
      paired_converted = true;
      if (!context->has_translucent_lightmap)
        qa_range_error("Translucent lightmapped surfaces require an uploaded directLightmapPixels image");
      qa_multitexture_vertex *paired = malloc(
          (vcount > 0 ? vcount : 1) * sizeof *paired);
      if (paired == NULL) qa_range_error("legacy allocation failed");
      for (size_t i = 0; i < vcount; i++) {
        paired[i].vertex = vertices[i];
        paired[i].tex_coord2 = geometry.vertices[i].lightmap_coord;
      }
      batches[0].vertices = NULL;
      batches[0].vertex_count = 0;
      batches[0].texturing_is_pair = true;
      batches[0].pair_vertices = paired;
      batches[0].pair_vertex_count = vcount;
      if (context->fragment_lighting == NULL) {
        batches[0].lighting.kind = "vertex";
        batches[0].lighting.pass = NULL;
        batches[0].texture.image = image;
        batches[0].second_texture.binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
        batches[0].second_texture.binding.image =
            context->translucent_lightmap;
        batches[0].second_texture.environment = "modulate";
      } else {
        for (size_t i = 0; i < vcount; i++) {
          qa_vec2 swap = paired[i].vertex.tex_coord;
          paired[i].vertex.tex_coord = paired[i].tex_coord2;
          paired[i].tex_coord2 = swap;
        }
        batches[0].lighting = *context->fragment_lighting;
        batches[0].lighting.pass = "texture";
        batches[0].texture.image = context->translucent_lightmap;
        batches[0].second_texture.binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
        batches[0].second_texture.binding.image = image;
        batches[0].second_texture.environment = "modulate";
      }
    } else {
      qa_blend_state blend = {"dst-color", "zero"};
      if (is_q1 && !eq(context->q1_lightmap_encoding, "rgb") &&
          context->fragment_lighting == NULL) {
        blend.source = "zero";
        blend.destination = eq(context->q1_lightmap_encoding, "inverted-alpha")
            ? "one-minus-src-alpha" : "one-minus-src-color";
      }
      bool has_direct = true;
      qa_renderer_image direct = lightmap;
      if (is_q1 && context->fragment_lighting != NULL) {
        has_direct = context->has_translucent_lightmap;
        direct = context->translucent_lightmap;
      }
      if (!has_direct)
        qa_range_error("Fragment-lit lightmap requires uploaded direct RGB pixels");
      qa_draw_batch *batch = &batches[count++];
      batch->lighting.kind = "vertex";
      if (context->fragment_lighting != NULL) {
        batch->lighting = *context->fragment_lighting;
        batch->lighting.pass = "lightmap";
      }
      batch->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
      batch->texture.image = direct;
      batch->indices.values = copy_indices(geometry);
      batch->indices.count = geometry.index_count;
      batch->state = state;
      batch->state.blend = blend;
      batch->state.depth_test = "equal";
      batch->state.depth_write = false;
      batch->state.alpha_test = "none";
      qa_render_vertex *lit = malloc(
          (vcount > 0 ? vcount : 1) * sizeof *lit);
      if (lit == NULL) qa_range_error("legacy allocation failed");
      for (size_t i = 0; i < vcount; i++) {
        lit[i].position = vertices[i].position;
        lit[i].tex_coord = geometry.vertices[i].lightmap_coord;
        lit[i].color = (qa_vec4){1, 1, 1, 1};
      }
      batch->vertices = lit;
      batch->vertex_count = vcount;
    }
  }

  if (context->fullbright != NULL) {
    qa_draw_batch *batch = &batches[count++];
    batch->lighting.kind = "vertex";
    batch->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
    batch->texture.image = *context->fullbright;
    batch->indices.values = copy_indices(geometry);
    batch->indices.count = geometry.index_count;
    /* Fullbright uses the single-texture vertex list even when the base
     * batch was converted to a pair (TS keeps `vertices`). */
    qa_render_vertex *full = malloc(
        (vcount > 0 ? vcount : 1) * sizeof *full);
    if (full == NULL) qa_range_error("legacy allocation failed");
    for (size_t i = 0; i < vcount; i++) {
      full[i] = vertices[i];
      full[i].color = (qa_vec4){tint.x / 255, tint.y / 255, tint.z / 255, alpha};
    }
    batch->vertices = full;
    batch->vertex_count = vcount;
    batch->state = state;
    batch->state.blend.source = "src-alpha";
    batch->state.blend.destination = "one-minus-src-alpha";
    batch->state.depth_test = blended ? "less-equal" : "equal";
    batch->state.depth_write = false;
    batch->state.alpha_test = "gt0";
  }
  if (paired_converted) free(vertices);
  *out_count = count;
  return batches;
}

void qa_split_sky_texture_destroy(qa_split_sky_texture *split) {
  free((void *)split->solid.pixels.bytes);
  free((void *)split->overlay.pixels.bytes);
  split->solid.pixels.bytes = NULL;
  split->overlay.pixels.bytes = NULL;
}

static uint8_t palette_at(qa_render_image image, size_t index,
                          const char *message) {
  if (index >= image.palette.colors.length) qa_range_error("%s", message);
  return image.palette.colors.bytes[index];
}

qa_split_sky_texture qa_split_q1_sky_texture(qa_render_image image) {
  if (image.level_count == 0 || image.levels[0].width != 256 ||
      image.levels[0].height != 128)
    qa_range_error("Classic Q1 sky requires a 256x128 texture");
  qa_image_level level = image.levels[0];
  uint8_t *solid = malloc(128 * 128 * 4);
  uint8_t *overlay = malloc(128 * 128 * 4);
  if (solid == NULL || overlay == NULL)
    qa_range_error("legacy allocation failed");
  qa_split_sky_texture out = {{128, 128, {solid, 128 * 128 * 4}},
                              {128, 128, {overlay, 128 * 128 * 4}}};
  if (image.kind == QA_RENDER_IMAGE_RGBA8) {
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 128; x++) {
        memcpy(overlay + (y * 128 + x) * 4,
               level.pixels.bytes + (y * 256 + x) * 4, 4);
        memcpy(solid + (y * 128 + x) * 4,
               level.pixels.bytes + (y * 256 + x + 128) * 4, 4);
      }
    }
    return out;
  }
  if (image.kind != QA_RENDER_IMAGE_INDEXED8) {
    qa_split_sky_texture_destroy(&out);
    qa_range_error("Classic Q1 sky requires an indexed or RGBA texture");
  }
  int64_t red = 0, green = 0, blue = 0;
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) {
      size_t at = (size_t)(y * 256 + x + 128);
      if (at >= level.pixels.length)
        qa_range_error("Q1 sky texture is incomplete");
      int index = level.pixels.bytes[at];
      uint8_t *p = solid + (y * 128 + x) * 4;
      p[0] = palette_at(image, (size_t)index * 3, "Q1 sky palette is incomplete");
      p[1] = palette_at(image, (size_t)index * 3 + 1, "Q1 sky palette is incomplete");
      p[2] = palette_at(image, (size_t)index * 3 + 2, "Q1 sky palette is incomplete");
      p[3] = 255;
      red += p[0]; green += p[1]; blue += p[2];
    }
  }
  uint8_t average[4] = {(uint8_t)(red / 16384), (uint8_t)(green / 16384),
                        (uint8_t)(blue / 16384), 0};
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) {
      size_t at = (size_t)(y * 256 + x);
      if (at >= level.pixels.length)
        qa_range_error("Q1 sky texture is incomplete");
      int index = level.pixels.bytes[at];
      uint8_t *p = overlay + (y * 128 + x) * 4;
      if (index == 0) {
        memcpy(p, average, 4);
      } else {
        p[0] = palette_at(image, (size_t)index * 3, "Q1 sky palette is incomplete");
        p[1] = palette_at(image, (size_t)index * 3 + 1, "Q1 sky palette is incomplete");
        p[2] = palette_at(image, (size_t)index * 3 + 2, "Q1 sky palette is incomplete");
        p[3] = 255;
      }
    }
  }
  return out;
}

qa_split_sky_texture qa_split_q64_sky_texture(qa_render_image image) {
  if (image.kind != QA_RENDER_IMAGE_INDEXED8)
    qa_range_error("Quake64 sky requires an indexed source image");
  if (image.level_count == 0 || image.levels[0].height < 2 ||
      fmod(image.levels[0].height, 2) != 0) /* height % 2 on a double */
    qa_range_error("Quake64 sky requires two vertically stacked layers");
  qa_image_level level = image.levels[0];
  double width = level.width, height = level.height / 2;
  size_t count = (size_t)(width * height);
  uint8_t *solid = malloc(count * 4);
  uint8_t *overlay = malloc(count * 4);
  if (solid == NULL || overlay == NULL)
    qa_range_error("legacy allocation failed");
  for (size_t index = 0; index < count; index++) {
    if (index >= level.pixels.length || count + index >= level.pixels.length)
      qa_range_error("Quake64 sky texture is incomplete");
    int front = level.pixels.bytes[index];
    int back = level.pixels.bytes[count + index];
    for (int channel = 0; channel < 3; channel++) {
      overlay[index * 4 + channel] = palette_at(image,
          (size_t)front * 3 + channel, "Quake64 sky palette is incomplete");
      solid[index * 4 + channel] = palette_at(image,
          (size_t)back * 3 + channel, "Quake64 sky palette is incomplete");
    }
    overlay[index * 4 + 3] = 128;
    solid[index * 4 + 3] = 255;
  }
  qa_split_sky_texture out = {{width, height, {solid, count * 4}},
                              {width, height, {overlay, count * 4}}};
  return out;
}
