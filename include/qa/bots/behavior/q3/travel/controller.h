/* Port of src/bots/behavior/q3/travel/controller.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_CONTROLLER_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_CONTROLLER_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/ground.h"
typedef struct qa_source_bot_travel_bots_a {
  qa_bot_travel_context_bots_a context;
  void *ctx;
  bool (*reachability)(void *ctx, int64_t handle, qa_travel_reachability_bots_a *out);
  int64_t (*route_reachability)(void *ctx, qa_bot_move_state *state, qa_bot_goal goal,
                                int64_t travel_flags);
} qa_source_bot_travel_bots_a;
void qa_source_bot_travel_bots_a_init(qa_source_bot_travel_bots_a *t,
                                      qa_bot_action_buffer *actions);
qa_bot_move_result qa_source_bot_travel_move_to_goal_bots_a(qa_source_bot_travel_bots_a *t,
                                                            qa_bot_move_state *state,
                                                            qa_bot_goal goal, int64_t travel_flags);
#endif
