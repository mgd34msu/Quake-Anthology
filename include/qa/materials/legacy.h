/*
 * Port of src/materials/legacy.ts. Q1/Q2 materials are the contracts
 * qa_render_material ("q1"/"q2" kinds). Animation arrays and sky-split
 * pixels are malloc'd. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_LEGACY_H
#define QA_MATERIALS_LEGACY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/dlight.h"
#include "qa/materials/geometry.h"
#include "qa/materials/turbulence.h"

/* Returns exact TS surface strings. */
const char *qa_q1_surface_kind(const char *name);

typedef struct qa_create_q1_options {
  bool has_alpha;
  double alpha;
  const qa_q1_material_frame *animation; size_t animation_count;
  const qa_q1_material_frame *alternate_animation;
  size_t alternate_animation_count;
} qa_create_q1_options;
qa_render_material qa_create_q1_material(const char *name,
    qa_renderer_image texture, qa_surface_lighting lighting,
    qa_create_q1_options options);

typedef struct qa_legacy_texture_entry {
  const char *name;
  qa_renderer_image image;
} qa_legacy_texture_entry;
typedef struct qa_q1_texture_animations {
  qa_q1_material_frame *animation; size_t animation_count;       /* owned */
  qa_q1_material_frame *alternate_animation;
  size_t alternate_animation_count;                              /* owned */
} qa_q1_texture_animations;
qa_q1_texture_animations qa_q1_texture_animations_build(const char *name,
    const qa_legacy_texture_entry *textures, size_t texture_count);
void qa_q1_texture_animations_destroy(qa_q1_texture_animations *animations);

qa_renderer_image qa_q1_animated_texture(const qa_render_material *material,
                                         double time, bool alternate);
qa_render_material qa_create_q2_material(const char *name,
    const qa_renderer_image *frames, size_t frame_count,
    qa_surface_lighting lighting, double surface_flags, const char *material);
qa_renderer_image qa_q2_animated_texture(const qa_render_material *material,
                                         double frame);

qa_vec2 qa_liquid_tex_coords(qa_vec2 uv, double time,
                             qa_turbulence_profile profile, bool flowing);
qa_vec2 qa_q2_flowing_tex_coords(qa_vec2 uv, double time);
/* layer: "solid"|"overlay". */
qa_vec2 qa_q1_sky_tex_coords(qa_vec3 position, qa_vec3 view_origin,
                             double time, const char *layer);

/* TS LegacyMaterialDrawContext. fullbright/translucent/fragment are
 * nullable pointers; has_translucent_lightmap mirrors TS undefined. */
typedef struct qa_legacy_material_draw_context {
  bool has_entity_rgba;
  qa_vec4 entity_rgba;
  double time;
  double animation_frame;
  bool alternate_animation;
  const qa_renderer_image *fullbright; /* nullable */
  bool q1_fog_active;
  const char *q1_lightmap_encoding;
  bool has_translucent_lightmap;
  qa_renderer_image translucent_lightmap;
  const qa_batch_lighting *fragment_lighting; /* nullable */
  const char *cull;
  double depth_range[2];
  qa_mat_project_fn project;
} qa_legacy_material_draw_context;

qa_draw_batch *qa_prepare_legacy_material_batches(
    const qa_render_material *material, qa_material_geometry geometry,
    const qa_legacy_material_draw_context *context, size_t *out_count);

typedef struct qa_split_sky_texture {
  qa_image_level solid, overlay; /* pixels owned */
} qa_split_sky_texture;
void qa_split_sky_texture_destroy(qa_split_sky_texture *split);
qa_split_sky_texture qa_split_q1_sky_texture(qa_render_image image);
qa_split_sky_texture qa_split_q64_sky_texture(qa_render_image image);

#endif
