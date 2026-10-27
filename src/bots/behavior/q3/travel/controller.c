/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/controller.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include <string.h>
void qa_source_bot_travel_bots_a_init(qa_source_bot_travel_bots_a *t,
                                      qa_bot_action_buffer *actions) {
  memset(t, 0, sizeof *t);
  t->context.actions = actions;
}
qa_bot_move_result qa_source_bot_travel_move_to_goal_bots_a(qa_source_bot_travel_bots_a *t,
                                                            qa_bot_move_state *s, qa_bot_goal goal,
                                                            int64_t tfl) {
  qa_bot_move_result result;
  qa_bot_move_result_init(&result);
  if (!t->route_reachability) {
    result.failure = true;
    return result;
  }
  int64_t handle = t->route_reachability(t->ctx, s, goal, tfl);
  if (handle == 0) {
    /* At goal area: finish walk toward goal origin. */
    qa_travel_reachability_bots_a reach;
    memset(&reach, 0, sizeof reach);
    reach.start = goal.origin;
    reach.end = goal.origin;
    reach.area = goal.area;
    reach.travel_type = QA_TRAVEL_WALK;
    return qa_finish_travel_walk_bots_a(&t->context, s, reach);
  }
  qa_travel_reachability_bots_a reach;
  memset(&reach, 0, sizeof reach);
  if (t->reachability && !t->reachability(t->ctx, handle, &reach)) {
    result.failure = true;
    return result;
  }
  s->last_reachability = handle;
  switch (reach.travel_type & QA_TRAVEL_MASK) {
  case QA_TRAVEL_CROUCH:
    return qa_travel_crouch_bots_a(&t->context, s, reach);
  case QA_TRAVEL_BARRIERJUMP:
    return qa_travel_barrier_jump_bots_a(&t->context, s, reach);
  case QA_TRAVEL_SWIM:
    return qa_travel_swim_bots_a(&t->context, s, reach);
  case QA_TRAVEL_TELEPORT:
    return qa_travel_teleport_bots_a(&t->context, s, reach);
  default:
    return qa_travel_walk_bots_a(&t->context, s, reach, false);
  }
}
