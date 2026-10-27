/* Port of src/render/scene/geometry.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_GEOMETRY_H
#define QA_RENDER_SCENE_GEOMETRY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"
#include "qa/materials/lighting.h"
qa_bounds qa_scene_geometry_bounds(const qa_vec3 *positions, size_t count);
qa_bounds qa_scene_geometry_bounds_vertices(const qa_material_vertex *vertices, size_t count);
typedef struct qa_prepared_brush_face {
  qa_material_geometry geometry;
  qa_material_vertex *owned_vertices; double *owned_indices; /* owned */
  qa_plane plane; qa_lightmap_face lightmap; bool decoupled;
} qa_prepared_brush_face;
void qa_prepared_brush_face_destroy(qa_prepared_brush_face *face);
/* is_q1 mirrors TS map.kind; the matching pointer is non-NULL. */
bool qa_prepare_brush_face(bool is_q1, const qa_q1_world_geometry *q1, const qa_q2_world_geometry *q2,
  qa_bsp_face face, double face_index, qa_vec2 image_size, bool warp, double subdivision, qa_prepared_brush_face *out);
#endif
