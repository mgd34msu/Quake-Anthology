/* Port of src/app/bootstrap/simulation/bot-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_commands.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double angle_of(double v) { return (double)(qa_js_to_int32(trunc(v * 65536.0 / 360.0)) & 65535); } /* Math.trunc(...) & 65535 */
static double move_of(double v) { double t = trunc(v * 127.0 / 320.0); return t < -127 ? -127 : t > 127 ? 127 : t; }
qa_user_command qa_app_sim_rerelease_bot_command(qa_bot_usercmd source, const char *dialect,
    double milliseconds, double server_time) {
  long attack = (long)(source.buttons & QA_BOT_BUTTON_ATTACK);
  bool jump = (source.buttons & QA_BOT_BUTTON_JUMP) != 0;
  bool use = (source.buttons & QA_BOT_BUTTON_USE) != 0;
  double up = jump ? qa_js_max(200, source.upmove) : source.upmove;
  qa_user_command out = {0};
  if (is(dialect, "q1-netquake")) {
    out.kind = QA_USER_COMMAND_Q1;
    out.as.q1.acknowledged_server_time_seconds = server_time / 1000;
    out.as.q1.view_angles = source.view_angles;
    out.as.q1.forward_move = source.forwardmove; out.as.q1.side_move = source.sidemove; out.as.q1.up_move = up;
    out.as.q1.buttons = (double)(attack | (jump ? 2 : 0)); out.as.q1.impulse = 0;
  } else if (is(dialect, "q1-quakeworld")) {
    out.kind = QA_USER_COMMAND_QW;
    out.as.qw.milliseconds = milliseconds; out.as.qw.angles = source.view_angles;
    out.as.qw.forward_move = source.forwardmove; out.as.qw.side_move = source.sidemove; out.as.qw.up_move = up;
    out.as.qw.buttons = (double)attack; out.as.qw.impulse = 0;
  } else if (is(dialect, "q2-classic")) {
    out.kind = QA_USER_COMMAND_Q2;
    out.as.q2.milliseconds = milliseconds;
    out.as.q2.angle_shorts.values[0] = angle_of(source.view_angles.x);
    out.as.q2.angle_shorts.values[1] = angle_of(source.view_angles.y);
    out.as.q2.angle_shorts.values[2] = angle_of(source.view_angles.z);
    out.as.q2.forward_move = source.forwardmove; out.as.q2.side_move = source.sidemove; out.as.q2.up_move = up;
    out.as.q2.buttons = (double)(attack | (use ? 2 : 0)); out.as.q2.impulse = 0; out.as.q2.light_level = 0;
  } else if (is(dialect, "q2-rerelease")) {
    out.kind = QA_USER_COMMAND_Q2_RERELEASE;
    out.as.q2_rerelease.milliseconds = milliseconds; out.as.q2_rerelease.angles = source.view_angles;
    out.as.q2_rerelease.forward_move = source.forwardmove; out.as.q2_rerelease.side_move = source.sidemove;
    out.as.q2_rerelease.buttons = (double)(attack | (use ? 2 : 0) | (up > 0 ? 8 : up < 0 ? 16 : 0));
    out.as.q2_rerelease.server_frame = 0;
  } else if (is(dialect, "q3")) {
    out.kind = QA_USER_COMMAND_Q3;
    out.as.q3.server_time_milliseconds = server_time;
    out.as.q3.angle_words.values[0] = angle_of(source.view_angles.x);
    out.as.q3.angle_words.values[1] = angle_of(source.view_angles.y);
    out.as.q3.angle_words.values[2] = angle_of(source.view_angles.z);
    out.as.q3.forward_move = move_of(source.forwardmove); out.as.q3.right_move = move_of(source.sidemove);
    out.as.q3.up_move = jump ? 127 : move_of(source.upmove); out.as.q3.buttons = (double)attack; out.as.q3.weapon = 0;
  } else qa_range_error("Unsupported bot movement dialect");
  return out;
}
