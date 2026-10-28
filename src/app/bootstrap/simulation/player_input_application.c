/* Port of src/app/bootstrap/simulation/player-input-application.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/player_input_application.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
#include "qa/movement/q1/quakeworld.h"
#include "qa/movement/q2/view.h"
#include "qa/movement/q3/prediction.h"
qa_vec3 qa_app_sim_movement_application_aim(qa_user_command command, qa_movement_state state, double health, qa_numeric_operations numeric) {
  if (command.kind == QA_USER_COMMAND_Q1) return command.as.q1.view_angles;
  if (command.kind == QA_USER_COMMAND_QW) return command.as.qw.angles;
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    qa_q2_vec3 view = {0, 0, 0};
    qa_q2_vec3 angles = {command.as.q2_rerelease.angles.x, command.as.q2_rerelease.angles.y, command.as.q2_rerelease.angles.z};
    qa_q2_vec3 delta = {state.as.q2_rerelease.delta_angles.x, state.as.q2_rerelease.delta_angles.y, state.as.q2_rerelease.delta_angles.z};
    qa_q2_rerelease_view_angles(view, angles, delta, state.as.q2_rerelease.flags, &numeric);
    return (qa_vec3){view[0], view[1], view[2]};
  }
  if (command.kind == QA_USER_COMMAND_Q2 && state.kind == QA_MOVEMENT_STATE_Q2) {
    qa_q2_vec3 view = {0, 0, 0};
    qa_q2_vec3 angles = {command.as.q2.angle_shorts.values[0], command.as.q2.angle_shorts.values[1], command.as.q2.angle_shorts.values[2]};
    qa_q2_vec3 delta = {state.as.q2.delta_angle_shorts.values[0], state.as.q2.delta_angle_shorts.values[1], state.as.q2.delta_angle_shorts.values[2]};
    qa_q2_classic_view_angles(view, angles, delta, state.as.q2.flags, &numeric);
    return (qa_vec3){view[0], view[1], view[2]};
  }
  if (command.kind == QA_USER_COMMAND_Q3 && state.kind == QA_MOVEMENT_STATE_Q3)
    return qa_q3_update_prediction_view(state.as.q3, health, command.as.q3).view_angles;
  qa_range_error("Input application command does not match movement state");
}
qa_frame_context qa_app_sim_movement_application_frame(qa_user_command command, qa_movement_state state, qa_movement_profile profile, qa_frame_context frame) {
  if (command.kind == QA_USER_COMMAND_Q1) return frame;
  double milliseconds;
  if (command.kind == QA_USER_COMMAND_Q3) {
    if (state.kind != QA_MOVEMENT_STATE_Q3) qa_range_error("Q3 input application has no command clock");
    double delta = command.as.q3.server_time_milliseconds - state.as.q3.command_time_milliseconds;
    milliseconds = delta < 0 ? 0 : delta > 1000 ? 1000 : delta;
  } else if (command.kind == QA_USER_COMMAND_QW && profile.clock.kind == QA_CLOCK_Q1_QUAKEWORLD) {
    milliseconds = 0;
    size_t count = 0;
    qa_qw_user_command *slices = qa_quakeworld_command_slices(command.as.qw, profile.clock.maximum_command_milliseconds, &count);
    for (size_t i = 0; i < count; i++) milliseconds += slices[i].milliseconds;
    free(slices);
  } else if (command.kind == QA_USER_COMMAND_QW) return frame;
  else if (command.kind == QA_USER_COMMAND_Q2) milliseconds = command.as.q2.milliseconds;
  else milliseconds = command.as.q2_rerelease.milliseconds;
  frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, milliseconds};
  return frame;
}
