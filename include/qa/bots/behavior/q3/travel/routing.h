/* Port of src/bots/behavior/q3/travel/routing.ts: source avoid-reach and avoid-spot
 * policy over the one shared navigation router. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_ROUTING_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_ROUTING_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/types.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/contracts/math.h"
#include <stdint.h>
float qa_movement_angle_difference(double first, double second);
double qa_movement_distance_squared(qa_vec3 first, qa_vec3 second);
double qa_distance_from_line_squared(qa_vec3 point, qa_vec3 start, qa_vec3 end);
int64_t qa_avoid_movement_spots(qa_vec3 origin, qa_travel_reachability reach,
                                const qa_bot_avoid_spot *spots, int64_t count);
/* TS TravelGraph: source handles keep zero invalid while a foreign graph may use
 * node and edge zero. The runtime is borrowed. time starts 0. */
typedef struct qa_travel_graph {
  int64_t offset;
  qa_navigation_runtime *runtime;
  double time;
} qa_travel_graph;
typedef struct qa_travel_selection {
  int64_t reachability, flags;
} qa_travel_selection;
void qa_travel_graph_init(qa_travel_graph *g, qa_navigation_runtime *runtime);
int64_t qa_travel_graph_area(const qa_travel_graph *g, int64_t node);
int64_t qa_travel_graph_node(const qa_travel_graph *g, int64_t area);
int64_t qa_travel_graph_handle(const qa_travel_graph *g, const qa_navigation_edge *edge);
/* False is TS null. */
bool qa_travel_graph_reach(const qa_travel_graph *g, int64_t number, qa_travel_reachability *out);
qa_travel_reachability qa_travel_graph_describe(const qa_travel_graph *g,
                                                const qa_navigation_edge *edge);
/* TS select with moveTravelFlags defaulting to travelFlags: pass the same value. */
qa_travel_selection qa_travel_graph_select(qa_travel_graph *g, const qa_bot_move_state *state,
                                           const qa_bot_goal *goal, int64_t travel_flags,
                                           int64_t move_travel_flags);
/* TS SelectionStart: `from` replaces the state's own position and history, as the
 * lookahead callers pass a reachability's end. avoid NULL is TS null (no avoid
 * reach or avoid spots). */
typedef struct qa_travel_selection_start {
  int64_t area;
  qa_vec3 origin;
  int64_t last_goal_area, last_area;
  const qa_bot_move_state *avoid;
} qa_travel_selection_start;
qa_travel_selection qa_travel_graph_select_from(qa_travel_graph *g, const qa_bot_goal *goal, int64_t travel_flags,
                                                int64_t move_travel_flags, const qa_travel_selection_start *from);
#endif
