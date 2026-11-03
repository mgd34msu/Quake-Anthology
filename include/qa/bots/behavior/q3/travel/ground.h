/* Port of src/bots/behavior/q3/travel/ground.ts (be_ai_move.c ground and air
 * travel). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_GROUND_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_GROUND_H
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/types.h"
#define QA_GROUND_TRAVEL(name)                                                                     \
  qa_bot_move_result name(qa_bot_travel_context *context, qa_bot_move_state *state,                \
                          qa_travel_reachability reach)
QA_GROUND_TRAVEL(qa_travel_walk);
QA_GROUND_TRAVEL(qa_finish_travel_walk);
QA_GROUND_TRAVEL(qa_travel_crouch);
QA_GROUND_TRAVEL(qa_travel_barrier_jump);
QA_GROUND_TRAVEL(qa_finish_travel_barrier_jump);
QA_GROUND_TRAVEL(qa_travel_swim);
QA_GROUND_TRAVEL(qa_travel_water_jump);
QA_GROUND_TRAVEL(qa_finish_travel_water_jump);
QA_GROUND_TRAVEL(qa_travel_walk_off_ledge);
QA_GROUND_TRAVEL(qa_finish_travel_walk_off_ledge);
QA_GROUND_TRAVEL(qa_travel_jump);
QA_GROUND_TRAVEL(qa_finish_travel_jump);
QA_GROUND_TRAVEL(qa_travel_ladder);
QA_GROUND_TRAVEL(qa_travel_teleport);
QA_GROUND_TRAVEL(qa_travel_jump_pad);
QA_GROUND_TRAVEL(qa_finish_travel_jump_pad);
#endif
