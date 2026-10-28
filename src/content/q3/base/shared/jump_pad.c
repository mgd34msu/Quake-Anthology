/* Port of src/content/q3/base/shared/jump-pad.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/jump_pad.h"
#include <math.h>
#include "qa/core/math.h"

void qa_q3_base_touch_jump_pad(qa_q3_base_source_player_state *state, const qa_q3_entity_state_record *jump_pad) {
  if (state->pm_type != QA_Q3_PM_NORMAL
      || qa_q3_base_player_state_slots_get(&state->powerups, QA_Q3_PW_FLIGHT) != 0) return;
  if (state->jumppad_ent != (double)jump_pad->number) {
    double pitch = fabs(qa_angle_normalize180(qa_vector_to_angles(jump_pad->origin2).x));
    qa_q3_base_player_state_record_add_event(state, QA_Q3_EV_JUMP_PAD, pitch < 45 ? 0 : 1);
  }
  state->jumppad_ent = (double)jump_pad->number;
  state->jumppad_frame = state->pmove_framecount;
  qa_q3_base_player_state_record_set_velocity(state,
      qa_math_vec3(jump_pad->origin2.x, jump_pad->origin2.y, jump_pad->origin2.z));
}
