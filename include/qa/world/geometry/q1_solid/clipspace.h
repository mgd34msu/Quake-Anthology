/* Port of src/world/geometry/q1-solid/clipspace.ts. Returned cells owned.
 * Prepared deriver: qa_prepare_q1_clip_solids returns opaque state;
 * qa_prepared_clip_derive returns false for TS null. GPL-2.0-or-later. */
#ifndef QA_WORLD_GEOMETRY_Q1_SOLID_CLIPSPACE_H
#define QA_WORLD_GEOMETRY_Q1_SOLID_CLIPSPACE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
qa_convex_cell **qa_derive_q1_clip_solids(const qa_q1_hull *hulls, size_t hull_count, qa_bounds envelope, size_t *out_count);
void qa_convex_cells_destroy(qa_convex_cell **cells, size_t count);
typedef struct qa_prepared_clip_deriver qa_prepared_clip_deriver;
qa_prepared_clip_deriver *qa_prepare_q1_clip_solids(const qa_q1_hull *hulls, size_t hull_count);
void qa_prepared_clip_deriver_destroy(qa_prepared_clip_deriver *d);
bool qa_prepared_clip_derive(qa_prepared_clip_deriver *d, qa_bounds envelope, qa_convex_cell ***out, size_t *out_count);
#endif
