/* Port of src/movement/q3/jump-pad.ts (BG_TouchJumpPad). GPL-2.0-or-later. */
#ifndef QA_MOVEMENT_Q3_JUMP_PAD_H
#define QA_MOVEMENT_Q3_JUMP_PAD_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"

typedef struct qa_q3_jump_pad_result {
  qa_q3_movement_state state;
  bool has_event; qa_predictable_movement_event event;
  qa_actor_id pad; /* owned copy when touched (state.jump_pad points at caller storage in TS) */
} qa_q3_jump_pad_result;

/* pad_storage receives the pad id; result.state.jump_pad points to it. */
qa_q3_jump_pad_result qa_q3_touch_jump_pad(qa_q3_movement_state state, qa_actor_id pad, qa_vec3 velocity,
                                           bool flight, qa_provider_id provider, qa_actor_id *pad_storage);
qa_q3_movement_state qa_q3_finish_jump_pad_prediction(qa_q3_movement_state state);
#endif
