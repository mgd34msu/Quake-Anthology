/* Port of src/render/scene/q2-sky.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_Q2_SKY_H
#define QA_RENDER_SCENE_Q2_SKY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/geometry.h"
typedef struct qa_q2_sky_view {
  const qa_renderer_image *images; size_t image_count; /* must be 6 */
  double rotation; bool auto_rotate; qa_vec3 axis;
} qa_q2_sky_view;
typedef qa_vec4 (*qa_scene_project_fn)(void *ctx, qa_vec3 point);
typedef struct qa_q2_sky_operations {
  qa_render_operation *operations; size_t count;
  qa_sky_strip *strips; qa_vertex_position_tex_coord *vertices; /* per-op blocks */
  size_t strip_capacity;
} qa_q2_sky_operations;
void qa_q2_sky_operations_destroy(qa_q2_sky_operations *ops);
qa_render_operation *qa_q2_sky_sides(qa_material_geometry geometry, qa_vec3 origin, qa_q2_sky_view sky, double seconds, qa_scene_project_fn project, void *project_ctx, qa_q2_sky_operations *out, size_t *out_count);
#endif
