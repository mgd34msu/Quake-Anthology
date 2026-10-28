/* Port of src/bots/behavior/q3/travel/special.ts. The TS *Calls
 * generators are synchronous here (commandCalls yields finish through
 * qa_bot_action_command). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_SPECIAL_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_SPECIAL_H
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/types.h"
qa_bot_move_result qa_travel_elevator(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_finish_travel_elevator(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_travel_func_bobbing(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_finish_travel_func_bobbing(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
void qa_reset_grapple(qa_bot_travel_context *context, qa_bot_move_state *state);
qa_bot_move_result qa_travel_grapple(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_travel_rocket_jump(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_travel_bfg_jump(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
qa_bot_move_result qa_finish_travel_weapon_jump(qa_bot_travel_context *context, qa_bot_move_state *state, qa_travel_reachability reach);
#endif
