/*
 * See header. Bot view and command conversion translated from id Software's
 * game/ai_main.c. Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qa/bots/behavior/q3/ai_input.h"
#include "qa/bots/behavior/library/character.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/movement/q3/constants.h"
#include <stdbool.h>

static int32_t source_abs(int32_t value) {
  return value < 0 ? (int32_t)(0u - (uint32_t)value) : value;
}

float qa_bot_angle_difference(double first, double second) {
  float difference = (float)(first - second);
  if (first > second) {
    if (difference > 180)
      difference = (float)((double)difference - 360);
  } else if (difference < -180)
    difference = (float)((double)difference + 360);
  return difference;
}

float qa_bot_change_view_angle(double angle, double ideal, double speed) {
  angle = qa_qvm_angle_mod(angle);
  ideal = qa_qvm_angle_mod(ideal);
  if (angle == ideal)
    return (float)angle;
  double move = (float)(ideal - angle);
  if (ideal > angle) {
    if (move > 180)
      move = (float)(move - 360);
  } else if (move < -180)
    move = (float)(move + 360);
  if (move > 0) {
    if (move > speed)
      move = speed;
  } else if (move < -speed)
    move = -speed;
  return (float)qa_qvm_angle_mod((float)(angle + move));
}

void qa_bot_change_view_angles(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               double think_time) {
  qa_bot_state_source *s = state->src;
  if (s->ideal_viewangles.x > 180)
    s->ideal_viewangles.x = (float)((double)s->ideal_viewangles.x - 360);
  qa_bot_character_library *characters = &ctx->library->characters;
  double factor = s->enemy >= 0 ? (double)qa_bot_character_bounded_float(
                                      characters, s->character, QA_CHAR_VIEW_FACTOR, (float)0.01, 1)
                                : (double)(float)0.05;
  double maximum = s->enemy >= 0 ? (double)qa_bot_character_bounded_float(
                                       characters, s->character, QA_CHAR_VIEW_MAXCHANGE, 1, 1800)
                                 : 360;
  if (maximum < 240)
    maximum = 240;
  maximum = (float)(maximum * think_time);
  float *angles[2] = {&s->viewangles.x, &s->viewangles.y};
  float *ideals[2] = {&s->ideal_viewangles.x, &s->ideal_viewangles.y};
  float *speeds[2] = {&s->viewangle_speed.x, &s->viewangle_speed.y};
  for (int axis = 0; axis < 2; axis++) {
    double angle = *angles[axis], ideal = *ideals[axis], angular_velocity = *speeds[axis];
    if (qa_game_ai_context_cvar(ctx, "bot_challenge")->integer_value != 0) {
      int32_t difference =
          source_abs(qa_qvm_float_to_int(qa_bot_angle_difference(angle, ideal)));
      double speed = (float)((double)(float)difference * factor);
      if (speed > maximum)
        speed = maximum;
      angle = qa_bot_change_view_angle(angle, ideal, speed);
    } else {
      angle = qa_qvm_angle_mod(angle);
      ideal = qa_qvm_angle_mod(ideal);
      double desired = (float)((double)qa_bot_angle_difference(angle, ideal) * factor);
      angular_velocity = (float)(angular_velocity + (double)(float)(angular_velocity - desired));
      if (angular_velocity > 180)
        angular_velocity = maximum;
      if (angular_velocity < -180)
        angular_velocity = -maximum;
      double speed = angular_velocity;
      if (speed > maximum)
        speed = maximum;
      if (speed < -maximum)
        speed = -maximum;
      angle = qa_qvm_angle_mod((float)(angle + speed));
      angular_velocity =
          (float)(angular_velocity * (double)(float)((double)(float)0.45 * (double)(float)(1 - factor)));
    }
    *angles[axis] = (float)angle;
    *ideals[axis] = (float)ideal;
    *speeds[axis] = (float)angular_velocity;
  }
  if (s->viewangles.x > 180)
    s->viewangles.x = (float)((double)s->viewangles.x - 360);
  qa_bot_action_view(&ctx->library->actions, s->client, qa_bot_v3(s->viewangles));
}

/* TS (value << 24) >> 24 */
static int8_t signed_byte(int32_t value) { return (int8_t)(uint8_t)(uint32_t)value; }

static int32_t command_angle(double angle, int32_t delta) {
  int32_t encoded = qa_qvm_float_to_int((float)((double)(float)(angle * 65536) / 360)) & 65535;
  return (int16_t)(uint16_t)((uint32_t)encoded - (uint32_t)delta);
}

/** The input is the detached EA_GetInput result; only the caller's usercmd is retained. */
void qa_bot_input_to_user_command(qa_bot_input input, qa_bot_user_command_source *command,
                                  qa_bot_ivec3 delta_angles, int64_t time) {
  int32_t flags = input.action_flags;
  if ((flags & QA_BOT_ACTION_DELAYED_JUMP) != 0)
    flags = (flags | QA_BOT_ACTION_JUMP) & ~QA_BOT_ACTION_DELAYED_JUMP;
  command->server_time = (int32_t)time;
  command->buttons = 0;
  if ((flags & (QA_BOT_ACTION_RESPAWN | QA_BOT_ACTION_ATTACK)) != 0)
    command->buttons = QA_Q3_BUTTON_ATTACK;
  static const int32_t buttons[][2] = {
      {QA_BOT_ACTION_TALK, QA_Q3_BUTTON_TALK},
      {QA_BOT_ACTION_GESTURE, QA_Q3_BUTTON_GESTURE},
      {QA_BOT_ACTION_USE, QA_Q3_BUTTON_USE_HOLDABLE},
      {QA_BOT_ACTION_WALK, QA_Q3_BUTTON_WALKING},
      {QA_BOT_ACTION_AFFIRMATIVE, QA_Q3_BUTTON_AFFIRMATIVE},
      {QA_BOT_ACTION_NEGATIVE, QA_Q3_BUTTON_NEGATIVE},
      {QA_BOT_ACTION_GET_FLAG, QA_Q3_BUTTON_GETFLAG},
      {QA_BOT_ACTION_GUARD_BASE, QA_Q3_BUTTON_GUARDBASE},
      {QA_BOT_ACTION_PATROL, QA_Q3_BUTTON_PATROL},
      {QA_BOT_ACTION_FOLLOW_ME, QA_Q3_BUTTON_FOLLOWME},
  };
  for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
    if ((flags & buttons[i][0]) != 0)
      command->buttons |= buttons[i][1];
  command->weapon = (uint8_t)(input.weapon & 255);
  command->angles.x = command_angle(input.view_angles.x, delta_angles.x);
  command->angles.y = command_angle(input.view_angles.y, delta_angles.y);
  command->angles.z = command_angle(input.view_angles.z, delta_angles.z);
  qa_vec3 angles = {(float)(input.direction.z != 0 ? input.view_angles.x : 0),
                    (float)input.view_angles.y, 0};
  qa_angle_vectors vectors = qa_qvm_angle_vectors(angles);
  double speed = (float)((double)(float)((double)input.speed * 127) / 400);
  command->forwardmove =
      signed_byte(qa_qvm_float_to_int((float)(qa_dot3(vectors.forward, input.direction) * speed)));
  command->rightmove =
      signed_byte(qa_qvm_float_to_int((float)(qa_dot3(vectors.right, input.direction) * speed)));
  command->upmove = signed_byte(qa_qvm_float_to_int((float)(
      (double)(float)((double)(float)source_abs(qa_qvm_float_to_int(vectors.forward.z)) *
                      input.direction.z) *
      speed)));
  if ((flags & QA_BOT_ACTION_MOVE_FORWARD) != 0)
    command->forwardmove = signed_byte(command->forwardmove + 127);
  if ((flags & QA_BOT_ACTION_MOVE_BACK) != 0)
    command->forwardmove = signed_byte(command->forwardmove - 127);
  if ((flags & QA_BOT_ACTION_MOVE_LEFT) != 0)
    command->rightmove = signed_byte(command->rightmove - 127);
  if ((flags & QA_BOT_ACTION_MOVE_RIGHT) != 0)
    command->rightmove = signed_byte(command->rightmove + 127);
  if ((flags & QA_BOT_ACTION_JUMP) != 0)
    command->upmove = signed_byte(command->upmove + 127);
  if ((flags & QA_BOT_ACTION_CROUCH) != 0)
    command->upmove = signed_byte(command->upmove - 127);
}

static float delta_degrees(int32_t delta) {
  return (float)((double)(float)delta * (double)(float)(360.0 / 65536));
}

static void apply_delta_angles(qa_bot_state_bots_a *state, double sign) {
  qa_bot_fvec3 *angles = &state->src->viewangles;
  qa_bot_ivec3 delta = state->src->cur_ps.delta_angles;
  float x = (float)qa_qvm_angle_mod((float)(angles->x + sign * delta_degrees(delta.x)));
  float y = (float)qa_qvm_angle_mod((float)(angles->y + sign * delta_degrees(delta.y)));
  float z = (float)qa_qvm_angle_mod((float)(angles->z + sign * delta_degrees(delta.z)));
  angles->x = x;
  angles->y = y;
  angles->z = z;
}

void qa_bot_add_delta_angles(qa_bot_state_bots_a *state) { apply_delta_angles(state, 1); }
void qa_bot_subtract_delta_angles(qa_bot_state_bots_a *state) { apply_delta_angles(state, -1); }

void qa_bot_update_input_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                int64_t time, int64_t elapsed_time) {
  qa_bot_add_delta_angles(state);
  qa_bot_change_view_angles(ctx, state, (float)((double)(float)elapsed_time / 1000));
  qa_bot_input input = qa_bot_action_get_input(&ctx->library->actions, state->src->client,
                                               (float)((double)(float)time / 1000));
  if ((input.action_flags & QA_BOT_ACTION_RESPAWN) != 0 &&
      (state->src->last_ucmd.buttons & QA_Q3_BUTTON_ATTACK) != 0)
    input.action_flags &= ~(QA_BOT_ACTION_RESPAWN | QA_BOT_ACTION_ATTACK);
  qa_bot_input_to_user_command(input, &state->src->last_ucmd, state->src->cur_ps.delta_angles, time);
  qa_bot_subtract_delta_angles(state);
}
