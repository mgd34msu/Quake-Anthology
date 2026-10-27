/* Port of src/bots/behavior/q3/travel/ground.ts. BotTravelContext (owner:
 * travel/types.ts, bots-b) mirrored as fn+ctx over BotActionBuffer.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_GROUND_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_GROUND_H
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/routing.h"
#include <stdbool.h>
typedef struct qa_bot_travel_context_bots_a {
  qa_bot_action_buffer *actions;
  void *ctx;
  void (*check_blocked)(void *ctx, qa_bot_move_state *state, qa_vec3 direction, bool check,
                        qa_bot_move_result *result);
  int64_t (*area_presence)(void *ctx, int64_t area);
  double (*gap_distance)(void *ctx, qa_vec3 origin, qa_vec3 direction, int64_t entity_num);
  qa_vec3 (*vector_to_angles_fn)(void *ctx, qa_vec3 value);
  int64_t (*random_next_int)(void *ctx);
} qa_bot_travel_context_bots_a;
qa_bot_move_result qa_travel_walk_bots_a(qa_bot_travel_context_bots_a *ctx,
                                         qa_bot_move_state *state,
                                         qa_travel_reachability_bots_a reach, bool is_waypoint);
qa_bot_move_result qa_finish_travel_walk_bots_a(qa_bot_travel_context_bots_a *ctx,
                                                qa_bot_move_state *state,
                                                qa_travel_reachability_bots_a reach);
qa_bot_move_result qa_travel_crouch_bots_a(qa_bot_travel_context_bots_a *ctx,
                                           qa_bot_move_state *state,
                                           qa_travel_reachability_bots_a reach);
qa_bot_move_result qa_travel_barrier_jump_bots_a(qa_bot_travel_context_bots_a *ctx,
                                                 qa_bot_move_state *state,
                                                 qa_travel_reachability_bots_a reach);
qa_bot_move_result qa_finish_travel_barrier_jump_bots_a(qa_bot_travel_context_bots_a *ctx,
                                                        qa_bot_move_state *state,
                                                        qa_travel_reachability_bots_a reach);
qa_bot_move_result qa_travel_swim_bots_a(qa_bot_travel_context_bots_a *ctx,
                                         qa_bot_move_state *state,
                                         qa_travel_reachability_bots_a reach);
qa_bot_move_result qa_travel_teleport_bots_a(qa_bot_travel_context_bots_a *ctx,
                                             qa_bot_move_state *state,
                                             qa_travel_reachability_bots_a reach);
#endif
