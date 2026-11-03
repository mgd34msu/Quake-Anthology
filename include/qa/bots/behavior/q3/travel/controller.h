/*
 * Port of src/bots/behavior/q3/travel/controller.ts (BotMoveInDirection and
 * BotMoveToGoal from be_ai_move.c). TS moveToGoalCalls is a generator only for the
 * grapple console command; special.c executes that command synchronously (see
 * travel/special.h), so moveToGoal and moveToGoalCalls are one function here.
 * The host and everything it points to are borrowed. GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_CONTROLLER_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_CONTROLLER_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/ground.h"
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/bots/behavior/q3/travel/types.h"
typedef struct qa_source_bot_travel {
  qa_source_bot_travel_host *host;
} qa_source_bot_travel;
void qa_source_bot_travel_init(qa_source_bot_travel *t, qa_source_bot_travel_host *host);
bool qa_source_bot_travel_move_in_direction(qa_source_bot_travel *t, int64_t handle,
                                            qa_vec3 direction, double speed, int64_t type);
/* goal NULL is TS null. */
void qa_source_bot_travel_move_to_goal(qa_source_bot_travel *t, qa_bot_move_result *result,
                                       int64_t handle, const qa_bot_goal *goal,
                                       int64_t travel_flags);
#endif
