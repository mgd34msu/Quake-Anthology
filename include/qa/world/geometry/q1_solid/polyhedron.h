/* Port of src/world/geometry/q1-solid/polyhedron.ts. Cells are heap-owned;
 * clip/split return the input pointer unchanged when TS returns `cell`.
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_GEOMETRY_Q1_SOLID_POLYHEDRON_H
#define QA_WORLD_GEOMETRY_Q1_SOLID_POLYHEDRON_H
#include <stddef.h>
#include "qa/contracts/math.h"
typedef struct qa_cell_face { qa_plane plane; qa_vec3 *vertices; size_t vertex_count; } qa_cell_face;
typedef struct qa_convex_cell { qa_cell_face *faces; size_t face_count; } qa_convex_cell;
double qa_cell_dot(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_cell_add(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_cell_sub(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_cell_scale(qa_vec3 a, double s);
qa_vec3 qa_cell_cross(qa_vec3 a, qa_vec3 b);
double qa_cell_length(qa_vec3 a);
qa_vec3 qa_cell_unit(qa_vec3 a);
qa_plane qa_negate_plane(qa_plane p);
qa_vec3 qa_cell_lerp(qa_vec3 a, qa_vec3 b, double t);
extern const qa_vec3 qa_cell_axes[3];
qa_convex_cell *qa_box_cell(qa_bounds bounds);
void qa_convex_cell_destroy(qa_convex_cell *cell);
qa_convex_cell *qa_clip_cell(qa_convex_cell *cell, qa_plane plane);
typedef struct qa_split_result { qa_convex_cell *front; qa_convex_cell *back; } qa_split_result;
qa_split_result qa_split_cell(qa_convex_cell *cell, qa_plane plane);
/* Malloc'd flat vertices; caller frees. */
qa_vec3 *qa_cell_vertices(const qa_convex_cell *cell, size_t *out_count);
qa_plane *qa_box_separating_planes(const qa_convex_cell *cell, const qa_vec3 *axes, size_t axis_count, size_t *out_count);
#endif
