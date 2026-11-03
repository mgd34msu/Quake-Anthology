/* Port of src/app/bootstrap/simulation/q3-commands.ts. Mirrored copies
 * exist in src/compat/qvm/mod_clients.c (parent reconciliation).
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include <math.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal_intent.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_q3_base_user_command qa_app_sim_relative_q3_source_command(qa_command_source source, const char *dialect,
    qa_q3_base_user_command command, qa_vec3 delta, const char *angle_space) {
  bool relative = is(angle_space, "absolute")
      || (angle_space == NULL && (source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT || source.kind == QA_COMMAND_SOURCE_BOT) && !is(dialect, "q3"));
  if (!relative) return command;
  command.angles = (qa_vec3){command.angles.x - delta.x, command.angles.y - delta.y, command.angles.z - delta.z};
  return command;
}
qa_q3_base_user_command qa_app_sim_q3_command_for_controls(qa_user_command command, double milliseconds, double requested_weapon, bool use_holdable) {
  double raw_buttons = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.buttons
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.buttons
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.buttons
      : command.kind == QA_USER_COMMAND_Q2_RERELEASE ? command.as.q2_rerelease.buttons : command.as.q3.buttons;
  double buttons = ((command.kind == QA_USER_COMMAND_Q3 ? (qa_js_to_int32(raw_buttons) & ~4) : (qa_js_to_int32(raw_buttons) & 1)) | (use_holdable ? 4 : 0));
  qa_q3_base_user_command out; memset(&out, 0, sizeof out);
  if (command.kind == QA_USER_COMMAND_Q3) {
    out.server_time = command.as.q3.server_time_milliseconds;
    out.angles = (qa_vec3){command.as.q3.angle_words.values[0], command.as.q3.angle_words.values[1], command.as.q3.angle_words.values[2]};
    out.buttons = buttons; out.weapon = requested_weapon;
    out.forwardmove = command.as.q3.forward_move; out.rightmove = command.as.q3.right_move; out.upmove = command.as.q3.up_move;
    return out;
  }
  qa_vec3 angles;
  if (command.kind == QA_USER_COMMAND_Q2)
    angles = (qa_vec3){command.as.q2.angle_shorts.values[0], command.as.q2.angle_shorts.values[1], command.as.q2.angle_shorts.values[2]};
  else {
    qa_vec3 value = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.view_angles
        : command.kind == QA_USER_COMMAND_QW ? command.as.qw.angles : command.as.q2_rerelease.angles;
    angles = (qa_vec3){(double)(qa_js_to_int32(trunc(value.x * 65536 / 360)) & 65535), (double)(qa_js_to_int32(trunc(value.y * 65536 / 360)) & 65535), (double)(qa_js_to_int32(trunc(value.z * 65536 / 360)) & 65535)};
  }
  double scale = (command.kind == QA_USER_COMMAND_Q1 || command.kind == QA_USER_COMMAND_QW) ? 320 : 200;
  double forward = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.forward_move
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.forward_move
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.forward_move : command.as.q2_rerelease.forward_move;
  double side = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.side_move
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.side_move
      : command.kind == QA_USER_COMMAND_Q2 ? command.as.q2.side_move : command.as.q2_rerelease.side_move;
  double up = command.kind == QA_USER_COMMAND_Q1 ? command.as.q1.up_move
      : command.kind == QA_USER_COMMAND_QW ? command.as.qw.up_move : command.as.q2.up_move;
  out.server_time = trunc(milliseconds); out.angles = angles; out.buttons = buttons; out.weapon = requested_weapon;
  out.forwardmove = trunc(qa_js_max(-127, qa_js_min(127, forward * 127 / scale)));
  out.rightmove = trunc(qa_js_max(-127, qa_js_min(127, side * 127 / scale)));
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE)
    out.upmove = (qa_js_to_int32(raw_buttons) & 8) != 0 ? 127 : (qa_js_to_int32(raw_buttons) & 16) != 0 ? -127 : 0;
  else if ((command.kind == QA_USER_COMMAND_Q1 || command.kind == QA_USER_COMMAND_QW) && (qa_js_to_int32(raw_buttons) & 2) != 0)
    out.upmove = 127;
  else out.upmove = trunc(qa_js_max(-127, qa_js_min(127, up * 127 / scale)));
  return out;
}
qa_q3_base_user_command qa_app_sim_q3_source_command(qa_actor_command input, qa_app_sim_q3_command_player player, double milliseconds, double native_weapon) {
  double requested; bool holdable;
  if (is(player.arsenal.state.kind, "q3")) {
    const char *product = (player.entities_content && strstr(player.entities_content, "missionpack")) ? "missionpack" : "baseq3";
    qa_q3_arsenal_controls controls = qa_app_sim_resolve_q3_arsenal_controls(player.arsenal, input.arsenal, input.command, product);
    requested = controls.requested_weapon; holdable = controls.use_holdable;
  } else {
    requested = native_weapon;
    holdable = input.arsenal ? input.arsenal->use_holdable
        : (input.command.kind == QA_USER_COMMAND_Q3 && (qa_js_to_int32(input.command.as.q3.buttons) & 4) != 0);
  }
  return qa_app_sim_q3_command_for_controls(input.command, milliseconds, requested, holdable);
}
qa_q3_wire_user_command qa_app_sim_wire_q3_user_command(qa_user_command command, double milliseconds, double weapon) {
  qa_q3_base_user_command c = qa_app_sim_q3_command_for_controls(command, milliseconds, weapon, false);
  qa_q3_wire_user_command out;
  out.server_time = qa_js_to_int32(c.server_time);
  out.angles[0] = qa_js_to_int32(c.angles.x); out.angles[1] = qa_js_to_int32(c.angles.y); out.angles[2] = qa_js_to_int32(c.angles.z);
  out.buttons = qa_js_to_int32(c.buttons); out.weapon = qa_js_to_int32(c.weapon);
  out.forwardmove = qa_js_to_int32(c.forwardmove); out.rightmove = qa_js_to_int32(c.rightmove); out.upmove = qa_js_to_int32(c.upmove);
  return out;
}
qa_user_command qa_app_sim_selected_q3_command(qa_q3_base_user_command command, qa_movement_profile profile, double milliseconds) {
  qa_vec3 angles = {command.angles.x * 360 / 65536, command.angles.y * 360 / 65536, command.angles.z * 360 / 65536};
  double forward = command.forwardmove * 320 / 127, side = command.rightmove * 320 / 127, up = command.upmove * 320 / 127;
  qa_user_command out; memset(&out, 0, sizeof out);
  if (is(profile.kind, "q1-netquake")) {
    out.kind = QA_USER_COMMAND_Q1;
    out.as.q1.acknowledged_server_time_seconds = command.server_time / 1000;
    out.as.q1.view_angles = angles; out.as.q1.forward_move = forward; out.as.q1.side_move = side; out.as.q1.up_move = up;
    out.as.q1.buttons = (qa_js_to_int32(command.buttons) & 1) | (command.upmove > 0 ? 2 : 0); out.as.q1.impulse = 0;
  } else if (is(profile.kind, "q1-quakeworld")) {
    out.kind = QA_USER_COMMAND_QW; out.as.qw.milliseconds = milliseconds; out.as.qw.angles = angles;
    out.as.qw.forward_move = forward; out.as.qw.side_move = side; out.as.qw.up_move = up;
    out.as.qw.buttons = qa_js_to_int32(command.buttons) & 1; out.as.qw.impulse = 0;
  } else if (is(profile.kind, "q2-classic")) {
    out.kind = QA_USER_COMMAND_Q2; out.as.q2.milliseconds = milliseconds;
    out.as.q2.angle_shorts.values[0] = command.angles.x; out.as.q2.angle_shorts.values[1] = command.angles.y; out.as.q2.angle_shorts.values[2] = command.angles.z;
    out.as.q2.forward_move = command.forwardmove * 200 / 127; out.as.q2.side_move = command.rightmove * 200 / 127; out.as.q2.up_move = command.upmove * 200 / 127;
    out.as.q2.buttons = qa_js_to_int32(command.buttons) & 1; out.as.q2.impulse = 0; out.as.q2.light_level = 0;
  } else if (is(profile.kind, "q2-rerelease")) {
    out.kind = QA_USER_COMMAND_Q2_RERELEASE; out.as.q2_rerelease.milliseconds = milliseconds; out.as.q2_rerelease.angles = angles;
    out.as.q2_rerelease.forward_move = command.forwardmove * 200 / 127; out.as.q2_rerelease.side_move = command.rightmove * 200 / 127;
    out.as.q2_rerelease.buttons = (qa_js_to_int32(command.buttons) & 1) | (command.upmove > 0 ? 8 : command.upmove < 0 ? 16 : 0);
    out.as.q2_rerelease.server_frame = 0;
  } else {
    out.kind = QA_USER_COMMAND_Q3; out.as.q3.server_time_milliseconds = command.server_time;
    out.as.q3.angle_words.values[0] = command.angles.x; out.as.q3.angle_words.values[1] = command.angles.y; out.as.q3.angle_words.values[2] = command.angles.z;
    out.as.q3.forward_move = command.forwardmove; out.as.q3.right_move = command.rightmove; out.as.q3.up_move = command.upmove;
    out.as.q3.buttons = command.buttons; out.as.q3.weapon = command.weapon;
  }
  return out;
}
qa_app_sim_relative_movement_input qa_app_sim_relative_movement_command(qa_app_sim_relative_movement_input input, qa_movement_state state) {
  if (!is(input.angle_space, "absolute")) return input;
  qa_user_command command = input.command;
  if (command.kind == QA_USER_COMMAND_Q3 && state.kind == QA_MOVEMENT_STATE_Q3) {
    input.angle_space = "source-relative";
    for (int i = 0; i < 3; i++) input.command.as.q3.angle_words.values[i] = command.as.q3.angle_words.values[i] - state.as.q3.delta_angle_words.values[i];
    return input;
  }
  if (command.kind == QA_USER_COMMAND_Q2 && state.kind == QA_MOVEMENT_STATE_Q2) {
    input.angle_space = "source-relative";
    for (int i = 0; i < 3; i++) input.command.as.q2.angle_shorts.values[i] = command.as.q2.angle_shorts.values[i] - state.as.q2.delta_angle_shorts.values[i];
    return input;
  }
  if (command.kind == QA_USER_COMMAND_Q2_RERELEASE && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    input.angle_space = "source-relative";
    input.command.as.q2_rerelease.angles = (qa_vec3){command.as.q2_rerelease.angles.x - state.as.q2_rerelease.delta_angles.x,
        command.as.q2_rerelease.angles.y - state.as.q2_rerelease.delta_angles.y,
        command.as.q2_rerelease.angles.z - state.as.q2_rerelease.delta_angles.z};
    return input;
  }
  return input;
}
