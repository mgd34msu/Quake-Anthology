/* Port of src/bots/navigation/runtime.ts. Graph and world are borrowed.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_RUNTIME_H
#define QA_BOTS_NAVIGATION_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/estimates.h"
#include "qa/bots/navigation/types.h"
#include "qa/persistence/value.h"

typedef bool (*qa_navigation_edge_filter)(void *ctx, const qa_navigation_edge *edge);
typedef struct qa_navigation_route_query {
  qa_vec3 start, goal;
  bool has_start_node; int64_t start_node;
  bool has_goal_node; int64_t goal_node;
  bool has_travel_flags; int64_t travel_flags;
  const int64_t *disabled_areas; size_t disabled_area_count;
  void *edge_filter_ctx; qa_navigation_edge_filter edge_filter; /* NULL = TS undefined */
  bool has_maximum_searches; int64_t maximum_searches;
} qa_navigation_route_query;

typedef struct qa_navigation_id_bool { int64_t id; bool enabled; } qa_navigation_id_bool;
typedef struct qa_navigation_id_reason { int64_t id; char *reason; } qa_navigation_id_reason;
typedef struct qa_navigation_id_seconds { int64_t id; double seconds; } qa_navigation_id_seconds;
typedef struct qa_navigation_runtime_checkpoint {
  const char *map_name, *map_format, *map_digest; /* borrowed from graph */
  qa_navigation_id_bool *enabled; size_t enabled_count;
  qa_navigation_id_reason *blocked; size_t blocked_count;
  qa_navigation_id_seconds *admission_seconds; size_t admission_count;
  double world_revision;
  int64_t generation;
} qa_navigation_runtime_checkpoint;
void qa_navigation_runtime_checkpoint_destroy(qa_navigation_runtime_checkpoint *c);

/* TS trainStep union; has=false is TS null. kind: 0 move, 1 wait, 2 ride, 3 unavailable. */
typedef struct qa_bot_transport_step {
  bool has; int kind; bool exit_stage; qa_vec3 target;
} qa_bot_transport_step;
enum { QA_TRANSPORT_MOVE = 0, QA_TRANSPORT_WAIT = 1, QA_TRANSPORT_RIDE = 2, QA_TRANSPORT_UNAVAILABLE = 3 };

typedef struct qa_navigation_runtime {
  const qa_navigation_graph *graph;
  const qa_navigation_world *world;
  qa_navigation_estimates estimates;
  const qa_navigation_edge ***outgoing; size_t *outgoing_counts; /* per graph node index */
  qa_navigation_id_bool *enabled; size_t enabled_count, enabled_cap;
  qa_navigation_id_reason *blocked; size_t blocked_count, blocked_cap;
  qa_navigation_id_seconds *admission_seconds; size_t admission_count, admission_cap;
  int64_t world_revision;
  int64_t generation;
} qa_navigation_runtime;
/* False only on OOM; duplicate/invalid graph identities abort (TS RangeError). */
bool qa_navigation_runtime_init(qa_navigation_runtime *rt, const qa_navigation_graph *graph,
                                const qa_navigation_world *world);
void qa_navigation_runtime_destroy(qa_navigation_runtime *rt);
bool qa_navigation_runtime_make_checkpoint(qa_navigation_runtime *rt, qa_navigation_runtime_checkpoint *out);
/* TS restoreCheckpoint(unknown): malformed input aborts via SaveReader. */
void qa_navigation_runtime_restore_checkpoint(qa_navigation_runtime *rt, const qa_save_value *value);
int64_t qa_navigation_runtime_generation(qa_navigation_runtime *rt);
const qa_navigation_node *qa_navigation_runtime_node(const qa_navigation_runtime *rt, int64_t id);
/* Borrowed edge-pointer list (empty for unknown id, TS []). */
const qa_navigation_edge *const *qa_navigation_runtime_outgoing(const qa_navigation_runtime *rt, int64_t id, size_t *out_count);
typedef struct qa_navigation_boarding_elevator {
  const qa_navigation_edge *edge; qa_actor_id actor;
  qa_vec3 origin, bottom, top; int phase;
} qa_navigation_boarding_elevator;
bool qa_navigation_runtime_boarding_elevator(qa_navigation_runtime *rt, int64_t node, qa_navigation_boarding_elevator *out);
typedef struct qa_navigation_boarding_train {
  const qa_navigation_edge *edge; qa_navigation_entity_state state;
  qa_navigation_train_stop boarding, arrival;
} qa_navigation_boarding_train;
bool qa_navigation_runtime_boarding_train(qa_navigation_runtime *rt, int64_t node, qa_navigation_boarding_train *out);
/* ground NULL = TS null. */
qa_bot_transport_step qa_navigation_runtime_train_step(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                                                       qa_vec3 origin, const qa_actor_id *ground);
bool qa_navigation_runtime_enable_area(qa_navigation_runtime *rt, int64_t id, bool enabled);
/* reason NULL clears the block. */
void qa_navigation_runtime_block_edge(qa_navigation_runtime *rt, int64_t id, const char *reason);
bool qa_navigation_runtime_area_at(qa_navigation_runtime *rt, qa_vec3 point, int64_t *out);
const qa_navigation_node *qa_navigation_runtime_nearest(qa_navigation_runtime *rt, qa_vec3 point, double radius);
/* Caller frees the returned array. */
int64_t *qa_navigation_runtime_bbox_areas(qa_navigation_runtime *rt, qa_bounds bounds, size_t *out_count);
typedef struct qa_navigation_area_crossing { int64_t area; qa_vec3 point; } qa_navigation_area_crossing;
qa_navigation_area_crossing *qa_navigation_runtime_trace_areas(qa_navigation_runtime *rt, qa_vec3 start, qa_vec3 end,
                                                               int64_t maximum, size_t *out_count);
bool qa_navigation_runtime_edge_allowed(qa_navigation_runtime *rt, const qa_navigation_edge *edge,
                                        const qa_navigation_route_query *query);
qa_traversal_admission qa_navigation_runtime_admit_edge(qa_navigation_runtime *rt, const qa_navigation_edge *edge, qa_vec3 origin);
qa_navigation_estimate_result qa_navigation_runtime_estimate(qa_navigation_runtime *rt, qa_navigation_estimate_query query);
qa_navigation_route_result qa_navigation_runtime_route(qa_navigation_runtime *rt, qa_navigation_route_query query);
bool qa_navigation_runtime_route_still_valid(qa_navigation_runtime *rt, const qa_navigation_route *route);
typedef struct qa_navigation_debug_line { qa_vec3 from, to; int64_t edge; bool enabled; qa_travel_mode mode; } qa_navigation_debug_line;
qa_navigation_debug_line *qa_navigation_runtime_debug_lines(qa_navigation_runtime *rt, size_t *out_count);
#endif
