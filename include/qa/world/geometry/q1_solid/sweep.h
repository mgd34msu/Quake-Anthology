/* Port of src/world/geometry/q1-solid/sweep.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_GEOMETRY_Q1_SOLID_SWEEP_H
#define QA_WORLD_GEOMETRY_Q1_SOLID_SWEEP_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
typedef struct qa_cell_shape {
  const char *kind; /* "box" | "capsule" */
  qa_vec3 axes[3]; qa_vec3 extents;
  qa_vec3 axis; double radius; double half_segment;
} qa_cell_shape;
typedef struct qa_sweep_interval { double enter; double exit; qa_plane plane; double contact; } qa_sweep_interval;
double qa_shape_support(qa_cell_shape shape, qa_vec3 normal);
bool qa_sweep_box_cell(const qa_convex_cell *cell, qa_vec3 start, qa_vec3 end, qa_cell_shape shape, double epsilon, qa_sweep_interval *out);
bool qa_sweep_capsule_cell(const qa_convex_cell *cell, qa_vec3 start, qa_vec3 end, qa_cell_shape shape, double epsilon, qa_sweep_interval *out);
#endif
