/*
 * Port of src/bots/behavior/q3/ai-navigation.ts (game/ai_dmq3.c navigation and
 * activation). GPL-2.0-or-later.
 *
 * GameAiDeathmatchState lives here as in the TS, and ai_context.h embeds it, so
 * this header forward-declares the context instead of including ai_context.h.
 *
 * C accommodations:
 *  - TS `null` returns of BotWaypoint/BotActivateGoal are NULL pointers.
 *  - botAlternateRoute returns the selected goal by value (no TS caller keeps
 *    its identity).
 *  - The alternate goal arrays are fixed cells: the only query asks for
 *    MAX_ALT_ROUTE_GOALS (32) goals.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_NAVIGATION_H
#define QA_BOTS_BEHAVIOR_Q3_AI_NAVIGATION_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QA_MAX_ALT_ROUTE_GOALS_BOTS_A 32

typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;

/* ai_dmq3.c's globals have the game AI lifetime, including retained goal cells.
 * All-zero is the TS initial state. */
typedef struct qa_game_ai_deathmatch_state_bots_a {
  int64_t gametype, maxclients;
  qa_vec3 last_teleport_origin;
  double last_teleport_time;
  int64_t max_bsp_model_index;
  qa_bot_goal ctf_red_flag, ctf_blue_flag, ctf_neutral_flag;
  qa_bot_goal red_obelisk, blue_obelisk, neutral_obelisk;
  bool alternate_routes_setup;
  qa_alternative_goal red_alternate_goals[QA_MAX_ALT_ROUTE_GOALS_BOTS_A];
  size_t red_alternate_goal_count;
  qa_alternative_goal blue_alternate_goals[QA_MAX_ALT_ROUTE_GOALS_BOTS_A];
  size_t blue_alternate_goal_count;
  qa_bot_waypoint_bots_a waypoints[QA_MAX_WAYPOINTS_BOTS_A];
  qa_bot_waypoint_bots_a *free_waypoints;
} qa_game_ai_deathmatch_state_bots_a;

int64_t qa_bot_point_area_num_bots_a(qa_game_ai_context_bots_a *ctx, qa_vec3 origin);
void qa_bot_setup_for_movement_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
qa_bot_waypoint_bots_a *qa_bot_create_way_point_bots_a(qa_game_ai_context_bots_a *ctx,
                                                       const char *name, qa_vec3 origin,
                                                       int64_t area);
qa_bot_waypoint_bots_a *qa_bot_find_way_point_bots_a(qa_bot_waypoint_bots_a *waypoints,
                                                     const char *name);
void qa_bot_free_waypoints_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_waypoint_bots_a *first);
void qa_bot_init_waypoints_bots_a(qa_game_ai_context_bots_a *ctx);
qa_vec3 qa_bot_roam_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
qa_bot_move_result qa_bot_attack_move_bots_a(qa_game_ai_context_bots_a *ctx,
                                             qa_bot_state_bots_a *state, int64_t travel_flags);
void qa_bot_set_movedir_bots_a(qa_vec3 angles, qa_vec3 *movedir);
/* mins/maxs NULL are the TS null outputs. */
int64_t qa_bot_model_mins_maxs_bots_a(qa_game_ai_context_bots_a *ctx, int64_t model_index,
                                      int64_t entity_type, int64_t contents, qa_vec3 *mins,
                                      qa_vec3 *maxs);
bool qa_bot_func_button_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx,
                                             qa_bot_state_bots_a *state, int64_t bsp_entity,
                                             qa_bot_activate_goal_bots_a *activation);
bool qa_bot_func_door_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state, int64_t bsp_entity,
                                           qa_bot_activate_goal_bots_a *activation);
bool qa_bot_trigger_multiple_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx,
                                                  qa_bot_state_bots_a *state, int64_t bsp_entity,
                                                  qa_bot_activate_goal_bots_a *activation);
void qa_bot_enable_activate_goal_areas_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_activate_goal_bots_a *activation, bool enable);
bool qa_bot_pop_from_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx,
                                                qa_bot_state_bots_a *state);
bool qa_bot_push_onto_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state,
                                                 const qa_bot_activate_goal_bots_a *activation);
void qa_bot_clear_activate_goal_stack_bots_a(qa_game_ai_context_bots_a *ctx,
                                             qa_bot_state_bots_a *state);
bool qa_bot_is_going_to_activate_entity_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state, int64_t entity);
void qa_bot_map_scripts_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
/* Returns the BSP entity; the output goal stores the corresponding game entity. */
int64_t qa_bot_get_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        int64_t entity_num,
                                        qa_bot_activate_goal_bots_a *activation);
bool qa_bot_go_for_activate_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        qa_bot_activate_goal_bots_a *activation);
void qa_bot_print_activate_goal_info_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state,
                                            const qa_bot_activate_goal_bots_a *activation,
                                            int64_t bsp_entity);
void qa_bot_random_move_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               qa_bot_move_result *result);
void qa_bot_ai_blocked_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              qa_bot_move_result *result, bool activate);
bool qa_bot_ai_predict_obstacles_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_bot_goal *goal);
qa_bot_goal qa_bot_alternate_route_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                          qa_bot_goal *goal);
bool qa_bot_get_alternate_route_goal_bots_a(qa_game_ai_context_bots_a *ctx,
                                            qa_bot_state_bots_a *state, int64_t base);
void qa_bot_setup_alternative_route_goals_bots_a(qa_game_ai_context_bots_a *ctx);
void qa_bot_set_entity_num_for_goal_with_model_bots_a(qa_game_ai_context_bots_a *ctx,
                                                      qa_bot_goal *goal, int64_t entity_type,
                                                      const char *model_name);
void qa_bot_set_entity_num_for_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_goal *goal,
                                           const char *classname);
bool qa_bot_goal_for_bsp_entity_bots_a(qa_game_ai_context_bots_a *ctx, const char *classname,
                                       qa_bot_goal *goal);
void qa_bot_setup_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx);
void qa_bot_shutdown_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *ctx);
#endif
