/* Port of src/materials/deform.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_DEFORM_H
#define QA_MATERIALS_DEFORM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/geometry.h"

/* TS DeformGeometry has the MaterialGeometry shape. */
typedef qa_material_geometry qa_deform_geometry;

typedef struct qa_deform_view {
  qa_axis axis;
  bool mirror;
  const qa_axis *entity_axis;         /* nullable */
  const qa_vec3 *non_normalized_axis; /* nullable */
} qa_deform_view;

typedef struct qa_projection_shadow_context {
  qa_axis axis;
  qa_vec3 origin;
  double shadow_plane;
  qa_vec3 light_dir;
} qa_projection_shadow_context;

/* TS RendererNoise: immutable seeded tables. */
typedef struct qa_renderer_noise {
  double values[256];
  int permutation[256];
} qa_renderer_noise;
void qa_renderer_noise_init(qa_renderer_noise *noise);
double qa_renderer_noise_sample(const qa_renderer_noise *noise, double x,
                                double y, double z, double time);

/* Result views into tess-owned storage (see geometry.h). */
qa_deform_geometry qa_deform_geometry_apply(
    qa_material_deform_state *tess, const qa_vertex_deformation *deforms,
    size_t deform_count, qa_deform_view view, double time,
    const qa_renderer_noise *noise,
    const qa_projection_shadow_context *projection_shadow);

#endif
