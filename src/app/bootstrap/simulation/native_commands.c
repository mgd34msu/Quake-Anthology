/* Port of src/app/bootstrap/simulation/native-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_commands.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double word_of(double a) { return (double)((long)trunc(a * 65536.0 / 360.0) & 65535); }
static double axis127(double v) { double t = v * 127.0 / 200.0; return trunc(t < -127 ? -127 : t > 127 ? 127 : t); }
qa_user_command qa_app_sim_selected_native_command(qa_user_command command, qa_vec3 aim,
    qa_app_sim_native_player_view player, double server_time_milliseconds) {
  bool classic = command.kind == QA_USER_COMMAND_Q2;
  double msec = classic ? command.as.q2.milliseconds : command.as.q2_rerelease.milliseconds;
  double fwd = classic ? command.as.q2.forward_move : command.as.q2_rerelease.forward_move;
  double side = classic ? command.as.q2.side_move : command.as.q2_rerelease.side_move;
  double buttons = classic ? command.as.q2.buttons : command.as.q2_rerelease.buttons;
  bool jump = classic ? command.as.q2.up_move > 0 : ((long)buttons & 8) != 0;
  bool crouch = classic ? command.as.q2.up_move < 0 : ((long)buttons & 16) != 0;
  bool attack = ((long)buttons & 1) != 0;
  double up = classic ? command.as.q2.up_move * 320.0 / 200.0 : jump ? 320 : crouch ? -320 : 0;
  qa_user_command out = {0};
  if (is(player.profile_kind, "q1-netquake")) {
    out.kind = QA_USER_COMMAND_Q1; out.as.q1.acknowledged_server_time_seconds = server_time_milliseconds / 1000;
    out.as.q1.view_angles = aim; out.as.q1.forward_move = fwd * 320.0 / 200.0; out.as.q1.side_move = side * 320.0 / 200.0;
    out.as.q1.up_move = up; out.as.q1.buttons = (double)((attack ? 1 : 0) | (jump ? 2 : 0));
    out.as.q1.impulse = classic ? command.as.q2.impulse : 0;
  } else if (is(player.profile_kind, "q1-quakeworld")) {
    out.kind = QA_USER_COMMAND_QW; out.as.qw.milliseconds = msec; out.as.qw.angles = aim;
    out.as.qw.forward_move = fwd * 320.0 / 200.0; out.as.qw.side_move = side * 320.0 / 200.0; out.as.qw.up_move = up;
    out.as.qw.buttons = (double)((attack ? 1 : 0) | (jump ? 2 : 0)); out.as.qw.impulse = classic ? command.as.q2.impulse : 0;
  } else if (is(player.profile_kind, "q2-classic")) {
    out.kind = QA_USER_COMMAND_Q2; out.as.q2.milliseconds = msec;
    out.as.q2.angle_shorts.values[0] = word_of(aim.x); out.as.q2.angle_shorts.values[1] = word_of(aim.y); out.as.q2.angle_shorts.values[2] = word_of(aim.z);
    out.as.q2.forward_move = trunc(fwd); out.as.q2.side_move = trunc(side);
    out.as.q2.up_move = classic ? command.as.q2.up_move : jump ? 200 : crouch ? -200 : 0;
    out.as.q2.buttons = attack ? 1 : 0; out.as.q2.impulse = classic ? command.as.q2.impulse : 0; out.as.q2.light_level = 0;
  } else if (is(player.profile_kind, "q2-rerelease")) {
    out.kind = QA_USER_COMMAND_Q2_RERELEASE; out.as.q2_rerelease.milliseconds = msec; out.as.q2_rerelease.angles = aim;
    out.as.q2_rerelease.forward_move = fwd; out.as.q2_rerelease.side_move = side;
    out.as.q2_rerelease.buttons = (double)((attack ? 1 : 0) | (jump ? 8 : 0) | (crouch ? 16 : 0));
    out.as.q2_rerelease.server_frame = classic ? 0 : command.as.q2_rerelease.server_frame;
  } else if (is(player.profile_kind, "q3")) {
    out.kind = QA_USER_COMMAND_Q3; out.as.q3.server_time_milliseconds = server_time_milliseconds;
    out.as.q3.angle_words.values[0] = word_of(aim.x); out.as.q3.angle_words.values[1] = word_of(aim.y); out.as.q3.angle_words.values[2] = word_of(aim.z);
    out.as.q3.forward_move = axis127(fwd); out.as.q3.right_move = axis127(side);
    out.as.q3.up_move = jump ? 127 : crouch ? -127 : 0; out.as.q3.buttons = attack ? 1 : 0;
    out.as.q3.weapon = is(player.arsenal_kind, "q3") ? player.source_weapon : 0;
  } else qa_range_error("Unsupported native movement profile");
  return out;
}
