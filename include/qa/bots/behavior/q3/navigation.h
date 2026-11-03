/*
 * Port of src/bots/behavior/q3/navigation.ts: source AI navigation operations over
 * the shared graph and selected movement. The host and its runtimes are
 * borrowed; the struct must not move after init (its travel host points at it).
 * Returned arrays are malloc'd and owned by the caller.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_NAVIGATION_H
#define QA_BOTS_BEHAVIOR_Q3_NAVIGATION_H
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/travel/controller.h"
#include "qa/bots/navigation/runtime.h"
#include <stdbool.h>
#include <stdint.h>
/* TS SourceBotNavigationHost; every member receives ctx. */
typedef struct qa_source_bot_navigation_host_bots_a {
  void *ctx;
  qa_navigation_runtime *runtime;
  qa_aas_bsp_entities *bsp_entities; /* borrowed */
  qa_navigation_runtime *(*for_client)(void *ctx, int64_t client);
  qa_bot_move_state_store *move_states;
  qa_bot_action_buffer *actions;
  qa_bot_random random;
  qa_bounds crouched_bounds;
  double (*time)(void *ctx);
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
  int64_t (*entity_model_index)(void *ctx, int64_t entity);
  /* bounds NULL is TS null. */
  qa_bot_server_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                      int64_t pass_entity, int64_t mask);
  /* Executes the selected actor movement provider on detached state; the result's
   * trajectory stays owned by the host until its next call. */
  qa_bot_travel_prediction_result (*predict_client_movement)(void *ctx,
                                                             qa_bot_movement_prediction query);
  /* Absolute bounds of a brush model, including models the game never sends to
   * the bots; false is TS null. */
  bool (*model_bounds)(void *ctx, int64_t model, qa_bounds *out);
  bool (*model_info)(void *ctx, int64_t model, qa_bot_travel_model *out);
  int64_t (*next_entity)(void *ctx, int64_t after);
  int64_t (*entity_type)(void *ctx, int64_t entity);
  int64_t (*entity_weapon)(void *ctx, int64_t entity);
  bool (*travel_weapon)(void *ctx, int64_t client, const char *mode, int64_t *out);
} qa_source_bot_navigation_host_bots_a;
/* TS SourceBotNavigation. */
typedef struct qa_source_bot_navigation_bots_a {
  qa_source_bot_navigation_host_bots_a host;
  int64_t offset;
  qa_navigation_runtime *selected; /* NULL: the host runtime */
  qa_source_bot_travel_host travel_host;
  qa_source_bot_travel travel;
} qa_source_bot_navigation_bots_a;
void qa_source_bot_navigation_bots_a_init(qa_source_bot_navigation_bots_a *n,
                                          const qa_source_bot_navigation_host_bots_a *host);
void qa_source_bot_navigation_with_client(qa_source_bot_navigation_bots_a *n, int64_t client,
                                          void (*think)(void *ctx), void *ctx);
bool qa_source_bot_navigation_ready(qa_source_bot_navigation_bots_a *n);
int64_t qa_source_bot_navigation_point_area(qa_source_bot_navigation_bots_a *n, qa_vec3 origin);
qa_bot_navigation_area qa_source_bot_navigation_area(qa_source_bot_navigation_bots_a *n,
                                                     int64_t number);
int64_t *qa_source_bot_navigation_bbox_areas(qa_source_bot_navigation_bots_a *n, qa_bounds bounds,
                                             size_t *out_count);
qa_bot_area_crossing *qa_source_bot_navigation_trace_areas(qa_source_bot_navigation_bots_a *n,
                                                           qa_vec3 start, qa_vec3 end,
                                                           int64_t maximum, size_t *out_count);
void qa_source_bot_navigation_set_area_enabled(qa_source_bot_navigation_bots_a *n, int64_t area,
                                               bool enabled);
qa_route_result qa_source_bot_navigation_route(qa_source_bot_navigation_bots_a *n, qa_route_query q);
double qa_source_bot_navigation_area_travel_time_to_goal(qa_source_bot_navigation_bots_a *n,
                                                         qa_area_travel_time_query q);
qa_predicted_route qa_source_bot_navigation_predict_route(qa_source_bot_navigation_bots_a *n,
                                                          qa_predict_route_query q);
qa_alternative_goal *qa_source_bot_navigation_alternative_route_goals(
    qa_source_bot_navigation_bots_a *n, qa_alternative_route_query q, size_t *out_count);
qa_bounds qa_source_bot_navigation_presence_bounds(qa_source_bot_navigation_bots_a *n,
                                                   int64_t presence);
qa_goal_drop_result qa_source_bot_navigation_drop_to_floor(qa_source_bot_navigation_bots_a *n,
                                                           qa_vec3 origin, qa_bounds bounds);
qa_goal_reachable_position qa_source_bot_navigation_best_reachable_area(
    qa_source_bot_navigation_bots_a *n, qa_vec3 origin, qa_bounds bounds);
int64_t qa_source_bot_navigation_fuzzy_point_reachability_area(qa_source_bot_navigation_bots_a *n,
                                                               qa_vec3 origin);
int64_t qa_source_bot_navigation_reachability_area(qa_source_bot_navigation_bots_a *n,
                                                   qa_vec3 origin, int64_t client);
int64_t qa_source_bot_navigation_best_reachable_from_jump_pad_area(
    qa_source_bot_navigation_bots_a *n, qa_vec3 origin, qa_bounds bounds);
bool qa_source_bot_navigation_swimming(qa_source_bot_navigation_bots_a *n, qa_vec3 origin);
qa_vec3 qa_source_bot_navigation_predict_client_movement_end(qa_source_bot_navigation_bots_a *n,
                                                             qa_bot_movement_prediction query);
bool qa_source_bot_navigation_move_in_direction(qa_source_bot_navigation_bots_a *n, int64_t handle,
                                                qa_vec3 direction, double speed, int64_t move_type);
void qa_source_bot_navigation_move_to_goal(qa_source_bot_navigation_bots_a *n,
                                           qa_bot_move_result *result, int64_t handle,
                                           qa_bot_goal goal, int64_t travel_flags);
bool qa_source_bot_navigation_movement_view_target(qa_source_bot_navigation_bots_a *n,
                                                   int64_t handle, qa_bot_goal goal,
                                                   int64_t travel_flags, double look_ahead,
                                                   qa_vec3 *out);
bool qa_source_bot_navigation_predict_visible_position(qa_source_bot_navigation_bots_a *n,
                                                       qa_vec3 origin, int64_t area,
                                                       qa_bot_goal goal, int64_t travel_flags,
                                                       qa_vec3 *out);
/* The BotNavigation and GoalNavigation views of n (ctx = n). */
qa_bot_navigation_bots_a qa_source_bot_navigation_interface(qa_source_bot_navigation_bots_a *n);
qa_goal_navigation qa_source_bot_navigation_goal_interface(qa_source_bot_navigation_bots_a *n);
#endif
