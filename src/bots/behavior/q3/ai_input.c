/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_input.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include <math.h>
#include <string.h>
static double angmod(double a) { return qa_qvm_angle_mod(a); }
float qa_bot_angle_difference(double first, double second) {
  float d = (float)(first - second);
  if (first > second) {
    if (d > 180)
      d = (float)((double)d - 360);
  } else if (d < -180)
    d = (float)((double)d + 360);
  return d;
}
float qa_bot_change_view_angle(double angle, double ideal, double speed) {
  angle = angmod(angle);
  ideal = angmod(ideal);
  if (angle == ideal)
    return (float)angle;
  float move = (float)(ideal - angle);
  if (ideal > angle) {
    if (move > 180)
      move = (float)((double)move - 360);
  } else if (move < -180)
    move = (float)((double)move + 360);
  if (move > 0) {
    if (move > speed)
      move = (float)speed;
  } else if (move < -speed)
    move = (float)(-speed);
  return (float)angmod((double)(float)(angle + move));
}
void qa_bot_change_view_angles(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *s, double think,
                               qa_bot_action_buffer *actions) {
  if (s->ideal_viewangles.x > 180)
    s->ideal_viewangles.x = (float)(s->ideal_viewangles.x - 360);
  double factor = 0.05, maximum = 360;
  if (s->enemy >= 0 && ctx->character_bounded) {
    factor = ctx->character_bounded(ctx->host_ctx, s->character, 4, (float)0.01, 1);
    maximum = ctx->character_bounded(ctx->host_ctx, s->character, 5, 1, 1800);
  } else if (s->enemy >= 0) {
    factor = (float)0.01;
    maximum = 360;
  }
  if (maximum < 240)
    maximum = 240;
  double max_change = (float)(maximum * think);
  double *view[2] = {&s->viewangles.x, &s->viewangles.y};
  double *ideal[2] = {&s->ideal_viewangles.x, &s->ideal_viewangles.y};
  double *vel[2] = {&s->viewangle_speed.x, &s->viewangle_speed.y};
  int challenge = ctx->cvar_int ? ctx->cvar_int(ctx->host_ctx, "bot_challenge") != 0 : 0;
  for (int i = 0; i < 2; i++) {
    double angle = *view[i], id = *ideal[i], av = *vel[i];
    if (challenge) {
      int32_t diff_i = qa_qvm_float_to_int(qa_bot_angle_difference(angle, id));
      int32_t adiff = diff_i < 0 ? (int32_t)((-(int64_t)diff_i) & 0xffffffffLL) : diff_i;
      double speed = (float)((double)(float)adiff * factor);
      if (speed > max_change)
        speed = max_change;
      angle = qa_bot_change_view_angle(angle, id, speed);
    } else {
      angle = angmod(angle);
      id = angmod(id);
      double desired = (float)((double)qa_bot_angle_difference(angle, id) * factor);
      av = (float)((double)(float)av + (double)(float)((double)(float)av - desired));
      if (av > 180)
        av = max_change;
      if (av < -180)
        av = -max_change;
      double speed = av;
      if (speed > max_change)
        speed = max_change;
      if (speed < -max_change)
        speed = -max_change;
      angle = angmod((double)(float)(angle + speed));
      av = (float)((double)(float)av *
                   (double)(float)((double)(float)0.45 * (double)(float)(1.0 - factor)));
    }
    *view[i] = angle;
    *ideal[i] = id;
    *vel[i] = av;
  }
  if (s->viewangles.x > 180)
    s->viewangles.x = (float)(s->viewangles.x - 360);
  if (actions)
    qa_bot_action_view(actions, s->client, s->viewangles);
}
static int32_t cmd_angle(double angle, double delta) {
  int32_t enc =
      qa_qvm_float_to_int((double)(float)((double)(float)(angle * 65536.0) / 360.0)) & 65535;
  return (int32_t)(((enc - (int32_t)delta) << 16) >> 16);
}
void qa_bot_input_to_user_command(qa_bot_input in, qa_user_command_bots_a *cmd, qa_vec3 delta,
                                  int64_t time) {
  memset(cmd, 0, sizeof *cmd);
  cmd->server_time = time;
  cmd->angles[0] = cmd_angle(in.view_angles.x, delta.x);
  cmd->angles[1] = cmd_angle(in.view_angles.y, delta.y);
  cmd->angles[2] = cmd_angle(in.view_angles.z, delta.z);
  cmd->weapon = in.weapon;
  /* Direction/speed to moves uses QVM angle vectors (core qa_qvm_angle_vectors). */
  qa_angle_vectors av = qa_qvm_angle_vectors(in.view_angles);
  double fwd = qa_dot3_double(in.direction, av.forward),
         right = qa_dot3_double(in.direction, av.right), up = qa_dot3_double(in.direction, av.up);
  cmd->forward_move = (int32_t)qa_qvm_float_to_int((double)(float)(fwd * in.speed));
  cmd->right_move = (int32_t)qa_qvm_float_to_int((double)(float)(right * in.speed));
  cmd->up_move = (int32_t)qa_qvm_float_to_int((double)(float)(up * in.speed));
  int32_t b = 0;
  if (in.action_flags & QA_BOT_ACTION_ATTACK)
    b |= 1;
  if (in.action_flags & QA_BOT_ACTION_TALK)
    b |= 32;
  if (in.action_flags & QA_BOT_ACTION_GESTURE)
    b |= 256;
  if (in.action_flags & QA_BOT_ACTION_WALK)
    b |= 128;
  cmd->buttons = b;
}
void qa_bot_add_delta_angles(qa_bot_state_bots_a *s, qa_vec3 d) {
  s->viewangles = qa_add3(s->viewangles, d);
}
void qa_bot_subtract_delta_angles(qa_bot_state_bots_a *s, qa_vec3 d) {
  s->viewangles = qa_sub3(s->viewangles, d);
}
void qa_bot_update_input_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *s,
                                int64_t time, double elapsed, qa_bot_action_buffer *actions,
                                qa_user_command_bots_a *out) {
  (void)ctx;
  qa_bot_input in = qa_bot_action_get_input(actions, s->client, elapsed);
  qa_vec3 delta = {0, 0, 0};
  qa_bot_input_to_user_command(in, out, delta, time);
  qa_bot_action_reset_input(actions, s->client);
}
