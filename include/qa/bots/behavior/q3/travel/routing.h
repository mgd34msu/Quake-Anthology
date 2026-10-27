/* Port of src/bots/behavior/q3/travel/routing.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_ROUTING_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_ROUTING_H
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/contracts/math.h"
#include <stdint.h>
/* TS TravelReachability (owner: travel/types.ts, bots-b) mirrored. */
typedef struct qa_travel_reachability_bots_a {
  qa_vec3 start, end;
  int64_t travel_type, area;
} qa_travel_reachability_bots_a;
float qa_movement_angle_difference(double first, double second);
double qa_movement_distance_squared(qa_vec3 first, qa_vec3 second);
double qa_distance_from_line_squared(qa_vec3 point, qa_vec3 start, qa_vec3 end);
int64_t qa_avoid_movement_spots(qa_vec3 origin, qa_travel_reachability_bots_a reach,
                                const qa_bot_avoid_spot *spots, int64_t count);
typedef struct qa_travel_graph_bots_a {
  int64_t offset;
} qa_travel_graph_bots_a;
void qa_travel_graph_bots_a_init(qa_travel_graph_bots_a *g, int is_aas);
int64_t qa_travel_graph_get_area(const qa_travel_graph_bots_a *g, int64_t node);
int64_t qa_travel_graph_get_node(const qa_travel_graph_bots_a *g, int64_t area);
int64_t qa_travel_graph_get_handle(const qa_travel_graph_bots_a *g, int64_t edge_id);
#endif
