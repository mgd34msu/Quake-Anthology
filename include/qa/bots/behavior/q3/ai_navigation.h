/* Port of src/bots/behavior/q3/ai-navigation.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_NAVIGATION_H
#define QA_BOTS_BEHAVIOR_Q3_AI_NAVIGATION_H
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_game_ai_deathmatch_state_bots_a {
  int64_t gametype, maxclients;
  bool alternate_routes_setup;
} qa_game_ai_deathmatch_state_bots_a;
qa_bot_waypoint_bots_a *qa_bot_create_waypoint_bots_a(const char *name, qa_vec3 origin,
                                                      int64_t area);
qa_bot_waypoint_bots_a *qa_bot_find_waypoint_bots_a(qa_bot_waypoint_bots_a *waypoints,
                                                    const char *name);
void qa_bot_free_waypoints_bots_a(qa_bot_waypoint_bots_a *first);
void qa_bot_set_movedir_bots_a(qa_vec3 angles, qa_vec3 *movedir);
void qa_bot_clear_activate_goal_stack_bots_a(qa_bot_state_bots_a *state,
                                             qa_bot_activate_goal_bots_a **stack_top);
bool qa_bot_push_activate_goal_bots_a(qa_bot_activate_goal_bots_a **stack_top,
                                      qa_bot_activate_goal_bots_a *activation, int64_t *depth);
bool qa_bot_pop_activate_goal_bots_a(qa_bot_activate_goal_bots_a **stack_top, int64_t *depth);
#endif
