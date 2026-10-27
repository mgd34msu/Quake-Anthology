/*
 * Port of src/materials/dlight.ts. Returned batch arrays are malloc'd;
 * free with qa_mat_draw_batches_destroy (evaluate.h). The TS generator
 * iterateProjectedDlights is realized by the same array builder, as in
 * its own projectDlightTexture. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_DLIGHT_H
#define QA_MATERIALS_DLIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/deform.h"
#include "qa/materials/q3_lighting.h"
#include "qa/materials/shader_types.h"

/* project closure as fn+ctx. */
typedef struct qa_mat_project_fn {
  void *ctx;
  qa_vec4 (*project)(void *ctx, qa_vec3 position);
} qa_mat_project_fn;

/* Malloc'd transformed copy; caller frees the array. */
qa_q3_dynamic_light *qa_transform_dlights(const qa_q3_dynamic_light *lights,
    size_t light_count, qa_vec3 origin, qa_axis axis);

typedef struct qa_dlight_mask_pair { int32_t front, back; } qa_dlight_mask_pair;
qa_dlight_mask_pair qa_split_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_plane plane);
int32_t qa_face_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_plane plane);
int32_t qa_grid_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, int32_t mask, qa_bounds bounds);
int32_t qa_bmodel_dlight_mask(const qa_q3_dynamic_light *lights,
    size_t light_count, qa_bounds bounds);

bool qa_receives_projected_dlights(const qa_mat_compiled_material *material);

qa_draw_batch *qa_project_dlight_texture(qa_deform_geometry geometry,
    int32_t mask, const qa_q3_dynamic_light *lights, size_t light_count,
    qa_renderer_image image, qa_mat_project_fn project, const char *cull,
    size_t *out_count);

#endif
