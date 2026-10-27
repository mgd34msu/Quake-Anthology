/* Port of src/world/collision/q1/hull.ts. createQ1Hulls returns 3 hulls;
 * hull0 clipnodes are caller-owned conversion storage freed by
 * qa_q1_hulls_destroy. blocks NULL = c===SOLID. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q1_HULL_H
#define QA_WORLD_COLLISION_Q1_HULL_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/scene.h"
#define QA_Q1_CONTENTS_EMPTY (-1)
#define QA_Q1_CONTENTS_SOLID (-2)
#define QA_Q1_DISTANCE_EPSILON (1.0/32.0)
extern const qa_bounds qa_q1_hull_bounds[3];
typedef struct qa_q1_hull_trace {
  double fraction; qa_vec3 end; bool start_solid; bool all_solid;
  bool in_open; bool in_water; qa_plane plane; double contents;
} qa_q1_hull_trace;
typedef struct qa_q1_hulls { qa_q1_hull hulls[3]; qa_q1_clip_node *drawing; size_t drawing_count; } qa_q1_hulls;
bool qa_create_q1_hulls(const qa_q1_world_geometry *map, double model_index, qa_q1_hulls *out);
void qa_q1_hulls_destroy(qa_q1_hulls *hulls);
double qa_q1_hull_point_contents(const qa_q1_hull *hull, qa_vec3 point, qa_numeric_profile numeric);
double qa_q1_hull_point_contents_at(const qa_q1_hull *hull, qa_vec3 point, qa_numeric_profile numeric, double start_node);
typedef bool (*qa_q1_blocks_fn)(void *ctx, double contents);
qa_q1_hull_trace qa_trace_q1_hull(const qa_q1_hull *hull, qa_vec3 start, qa_vec3 end, qa_numeric_profile numeric, qa_q1_blocks_fn blocks, void *blocks_ctx);
#endif
