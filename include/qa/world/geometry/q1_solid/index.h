/* Port of src/world/geometry/q1-solid/index.ts (Q1SolidSpace).
 * cells() generator becomes a visitor; include fn NULL = c===-2.
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_GEOMETRY_Q1_SOLID_INDEX_H
#define QA_WORLD_GEOMETRY_Q1_SOLID_INDEX_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
typedef struct qa_q1_solid_cell { qa_convex_cell *cell; double leaf; double contents; } qa_q1_solid_cell;
typedef struct qa_q1_solid_space { const qa_q1_world_geometry *geometry; const char *representation; } qa_q1_solid_space;
void qa_q1_solid_space_init(qa_q1_solid_space *s, const qa_q1_world_geometry *geometry);
typedef bool (*qa_q1_solid_include_fn)(void *ctx, double contents);
typedef void (*qa_q1_solid_visit_fn)(void *ctx, qa_q1_solid_cell cell);
void qa_q1_solid_space_cells(qa_q1_solid_space *s, qa_bounds envelope, double model_index,
  qa_q1_solid_include_fn include, void *include_ctx, qa_q1_solid_visit_fn visit, void *visit_ctx);
#endif
