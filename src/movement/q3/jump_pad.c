/* Port of src/movement/q3/jump-pad.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/jump_pad.h"

#include <math.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/movement/q3/constants.h"

qa_q3_jump_pad_result qa_q3_touch_jump_pad(qa_q3_movement_state state, qa_actor_id pad, qa_vec3 velocity,
                                           bool flight, qa_provider_id provider, qa_actor_id *pad_storage) {
  qa_q3_jump_pad_result r = {state, false, {0}, pad};
  if (state.movement_type != QA_Q3_PM_NORMAL || flight) return r;
  bool changed = state.jump_pad == NULL || !qa_same_actor(*state.jump_pad, pad);
  double pitch = fabs(qa_angle_normalize180(qa_vector_to_angles(velocity).x));
  *pad_storage = pad;
  r.state.jump_pad = pad_storage;
  r.state.jump_pad_frame = state.movement_frame;
  r.state.velocity = qa_math_vec3(velocity.x, velocity.y, velocity.z);
  r.state.predictable_event_sequence = changed
      ? (double)qa_int32(state.predictable_event_sequence + 1) : state.predictable_event_sequence;
  if (changed) {
    r.has_event = true;
    r.event = (qa_predictable_movement_event){provider, state.predictable_event_sequence,
                                               QA_Q3_EV_JUMP_PAD, pitch < 45 ? 0 : 1};
  }
  return r;
}

qa_q3_movement_state qa_q3_finish_jump_pad_prediction(qa_q3_movement_state state) {
  if (state.jump_pad_frame == state.movement_frame) return state;
  state.jump_pad = NULL; state.jump_pad_frame = 0;
  return state;
}
