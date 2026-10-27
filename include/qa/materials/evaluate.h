/*
 * Port of src/materials/evaluate.ts. Batches (vertices, indices, fog)
 * are malloc'd; free with qa_mat_draw_batches_destroy. evaluateTexCoords
 * and stageState (TS owner: material.ts, unported in this slice) are
 * mirrored locally in evaluate.c. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_EVALUATE_H
#define QA_MATERIALS_EVALUATE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/color.h"
#include "qa/materials/deform.h"
#include "qa/materials/dlight.h"
#include "qa/materials/geometry.h"
#include "qa/materials/q3_lighting.h"
#include "qa/materials/shader_types.h"

typedef struct qa_mat_draw_context_fog {
  void *ctx;
  qa_vec2 (*coordinates)(void *ctx, qa_vec3 position);
  qa_texture_binding texture;
  qa_vec4 color;
} qa_mat_draw_context_fog;

typedef struct qa_mat_draw_context_q1_fog {
  double density;
  qa_vec3 color;
  qa_texture_binding texture;
} qa_mat_draw_context_q1_fog;

/* TS MaterialDrawContext. Optional callbacks are NULL fn pointers;
 * fog NULL = TS fog null; dynamic_lights/q1_fog presence flags mirror
 * TS undefined. Batch-returning callbacks hand ownership to the caller
 * of the evaluate function (appended into its result). */
typedef struct qa_material_draw_context {
  double time, identity_light;
  qa_vec4 entity_rgba;
  const qa_entity_lighting *lighting; /* nullable */
  qa_vec3 view_origin, local_view_origin;
  const qa_renderer_noise *noise;
  double time_offset, refdef_time;
  qa_vec2 shader_tex_coord;
  qa_deform_view deform_view;
  const qa_projection_shadow_context *projection_shadow; /* nullable */
  const char *const *render_text; size_t render_text_count;
  bool has_dynamic_lights;
  const qa_q3_dynamic_light *dynamic_lights; size_t dynamic_light_count;
  int32_t dynamic_light_mask;
  qa_renderer_image dynamic_light_image;
  void *dynamic_light_batches_ctx;
  qa_draw_batch *(*dynamic_light_batches)(void *ctx,
      qa_material_geometry geometry, size_t *out_count);
  void *model_lighting_ctx;
  qa_batch_lighting (*model_lighting)(void *ctx, qa_material_geometry geometry);
  void *lightmap_lighting_ctx;
  qa_batch_lighting (*lightmap_lighting)(void *ctx,
                                         qa_material_geometry geometry);
  double depth_range[2];
  const qa_polygon_offset *polygon_offset; /* nullable */
  bool has_q1_fog;
  qa_mat_draw_context_q1_fog q1_fog;
  const qa_mat_draw_context_fog *fog; /* nullable */
  qa_mat_project_fn project;
} qa_material_draw_context;

qa_draw_batch *qa_prepare_material_batches(
    const qa_mat_compiled_material *compiled, qa_material_geometry input,
    const qa_material_draw_context *context, size_t *out_count);
qa_draw_batch *qa_evaluate_material_passes(
    const qa_mat_compiled_material *compiled, qa_material_geometry input,
    const qa_material_draw_context *context, size_t *out_count);

void qa_mat_draw_batches_destroy(qa_draw_batch *batches, size_t count);

#endif
