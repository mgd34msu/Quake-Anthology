/* Port of src/bots/behavior/q3/navigation.ts. NavigationRuntime (owner:
 * bots/navigation, bots-b) mirrored as host fn+ctx. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_NAVIGATION_H
#define QA_BOTS_BEHAVIOR_Q3_NAVIGATION_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_source_bot_navigation_bots_a {
  bool ready;
  void *ctx;
  int64_t (*point_area)(void *ctx, qa_vec3 origin);
  int64_t (*reachability_area)(void *ctx, qa_vec3 origin, int64_t client);
  int64_t (*fuzzy_area)(void *ctx, qa_vec3 origin);
  qa_bot_navigation_area (*get_area)(void *ctx, int64_t number);
  void (*set_area_enabled)(void *ctx, int64_t area, bool enabled);
  double (*travel_time)(void *ctx, qa_area_travel_time_query q);
  qa_route_result (*route)(void *ctx, qa_route_query q);
  qa_predicted_route (*predict_route)(void *ctx, qa_predict_route_query q);
  bool (*swimming)(void *ctx, qa_vec3 origin);
  qa_bounds (*presence_bounds)(void *ctx, int64_t presence);
  void (*move_to_goal)(void *ctx, qa_bot_move_result *result, int64_t move_state, qa_bot_goal goal,
                       int64_t travel_flags);
  bool (*move_in_direction)(void *ctx, int64_t move_state, qa_vec3 direction, double speed,
                            int64_t move_type);
} qa_source_bot_navigation_bots_a;
void qa_source_bot_navigation_bots_a_init(qa_source_bot_navigation_bots_a *n);
int64_t qa_source_bot_navigation_point_area(qa_source_bot_navigation_bots_a *n, qa_vec3 origin);
qa_route_result qa_source_bot_navigation_route(qa_source_bot_navigation_bots_a *n,
                                               qa_route_query q);
bool qa_source_bot_navigation_get_ready(const qa_source_bot_navigation_bots_a *n);
#endif
