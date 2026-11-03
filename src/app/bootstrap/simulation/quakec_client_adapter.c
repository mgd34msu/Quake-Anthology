/* Port of src/app/bootstrap/simulation/quakec-client-adapter.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_client_adapter.h"
#include <string.h>
#include "qa/foundation/js.h"
qa_q1_user_command qa_app_sim_quakec_client_command(qa_user_command command) {
  if (command.kind == QA_USER_COMMAND_Q1) return command.as.q1;
  qa_vec3 angles = {0, 0, 0}; bool jump = false; double side = 0, up = 0, impulse = 0, forward = 0, buttons = 0;
  switch (command.kind) {
    case QA_USER_COMMAND_QW:
      angles = command.as.qw.angles; jump = (qa_js_to_int32(command.as.qw.buttons) & 2) != 0;
      side = command.as.qw.side_move; up = command.as.qw.up_move; impulse = command.as.qw.impulse;
      forward = command.as.qw.forward_move; buttons = command.as.qw.buttons; break;
    case QA_USER_COMMAND_Q2_RERELEASE:
      angles = command.as.q2_rerelease.angles; jump = (qa_js_to_int32(command.as.q2_rerelease.buttons) & 8) != 0;
      side = command.as.q2_rerelease.side_move; up = jump ? 1 : 0; impulse = 0;
      forward = command.as.q2_rerelease.forward_move; buttons = command.as.q2_rerelease.buttons; break;
    case QA_USER_COMMAND_Q2:
      angles = (qa_vec3){command.as.q2.angle_shorts.values[0] * 360 / 65536, command.as.q2.angle_shorts.values[1] * 360 / 65536, command.as.q2.angle_shorts.values[2] * 360 / 65536};
      jump = command.as.q2.up_move >= 10; side = command.as.q2.side_move; up = command.as.q2.up_move; impulse = command.as.q2.impulse;
      forward = command.as.q2.forward_move; buttons = command.as.q2.buttons; break;
    case QA_USER_COMMAND_Q3:
      angles = (qa_vec3){command.as.q3.angle_words.values[0] * 360 / 65536, command.as.q3.angle_words.values[1] * 360 / 65536, command.as.q3.angle_words.values[2] * 360 / 65536};
      jump = command.as.q3.up_move >= 10; side = command.as.q3.right_move; up = command.as.q3.up_move; impulse = 0;
      forward = command.as.q3.forward_move; buttons = command.as.q3.buttons; break;
    case QA_USER_COMMAND_Q1: break;
  }
  qa_q1_user_command out; memset(&out, 0, sizeof out);
  out.acknowledged_server_time_seconds = 0; out.view_angles = angles;
  out.forward_move = forward; out.side_move = side; out.up_move = up;
  out.buttons = (qa_js_to_int32(buttons) & 1) | (jump ? 2 : 0); out.impulse = impulse;
  return out;
}
bool qa_app_sim_quakec_source_jump(qa_user_command command, qa_app_sim_quakec_client_transition_state before, qa_app_sim_quakec_client_transition_state after) {
  const int jump_flags = 512 | 4096;
  return ((qa_js_to_int32(qa_app_sim_quakec_client_command(command).buttons) & 2) != 0)
      && ((qa_js_to_int32(before.flags) & jump_flags) == jump_flags) && ((qa_js_to_int32(after.flags) & jump_flags) == 0)
      && after.velocity.z > before.velocity.z;
}
qa_user_command qa_app_sim_consume_quakec_jump(qa_user_command command) {
  switch (command.kind) {
    case QA_USER_COMMAND_Q1: command.as.q1.buttons = qa_js_to_int32(command.as.q1.buttons) & ~2; break;
    case QA_USER_COMMAND_QW: command.as.qw.buttons = qa_js_to_int32(command.as.qw.buttons) & ~2; break;
    case QA_USER_COMMAND_Q2_RERELEASE: command.as.q2_rerelease.buttons = qa_js_to_int32(command.as.q2_rerelease.buttons) & ~8; break;
    case QA_USER_COMMAND_Q2: command.as.q2.up_move = command.as.q2.up_move > 0 ? 0 : command.as.q2.up_move; break;
    case QA_USER_COMMAND_Q3: command.as.q3.up_move = command.as.q3.up_move > 0 ? 0 : command.as.q3.up_move; break;
  }
  return command;
}
qa_movement_state qa_app_sim_quakec_free_movement(qa_movement_state state) {
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1: state.as.q1.move_type = 8; break;
    case QA_MOVEMENT_STATE_QW: state.as.qw.spectator = 1; break;
    case QA_MOVEMENT_STATE_Q2: state.as.q2.type = 1; break;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: state.as.q2_rerelease.type = 2; break;
    case QA_MOVEMENT_STATE_Q3: state.as.q3.movement_type = 1; break;
  }
  return state;
}
