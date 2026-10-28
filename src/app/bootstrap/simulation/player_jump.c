/* Port of src/app/bootstrap/simulation/player-jump.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/player_jump.h"
#include <string.h>
#include "qa/movement/q2/types.h"
#include "qa/movement/q3/constants.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
bool qa_app_sim_movement_jumped(qa_movement_state before, qa_trace_hit ground, qa_user_command command, qa_movement_result result) {
  if (!is(result.status, "active")) return false;
  if (is(result.kind, "q1-netquake")) return false;
  if (is(result.kind, "q1-quakeworld"))
    return before.kind == QA_MOVEMENT_STATE_QW && ground.kind != QA_TRACE_HIT_NONE
        && (((int)before.as.qw.old_buttons & 2) == 0) && (((int)result.state.as.qw.old_buttons & 2) != 0)
        && result.state.as.qw.velocity.z > 0 && !result.state.as.qw.dead;
  if (is(result.kind, "q2-classic"))
    return command.kind == QA_USER_COMMAND_Q2 && ground.kind != QA_TRACE_HIT_NONE && result.ground.kind == QA_TRACE_HIT_NONE
        && command.as.q2.up_move >= 10 && result.water_level == 0;
  if (is(result.kind, "q2-rerelease"))
    return result.jump_sound && (((int)result.state.as.q2_rerelease.flags & QA_Q2_PMF_ON_LADDER) == 0);
  if (is(result.kind, "q3")) {
    for (size_t i = 0; i < result.effect_count; i++)
      if (result.effects[i].effect.kind == QA_MOVEMENT_EFFECT_EVENT
          && result.effects[i].effect.value.event == QA_Q3_EV_JUMP) return true;
    return false;
  }
  return false;
}
bool qa_app_sim_publish_q3_character_movement_event(qa_game_family character, double event) {
  return event != QA_Q3_EV_JUMP || character == QA_GAME_FAMILY_Q3;
}
