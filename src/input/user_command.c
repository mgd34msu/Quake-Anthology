/* Port of src/input/user-command.ts. GPL-2.0-or-later. */
#include "qa/input/user_command.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"

qa_view_input_tuning qa_default_view_input_tuning(qa_command_dialect dialect) {
  bool q1 = qa_command_dialect_is_q1(dialect);
  qa_view_input_tuning tuning = {200, 200, q1 ? 350 : 200, 200,
                                 140, 150, 1.5, 2, !q1};
  return tuning;
}

static double angle_word(double angle) {
  return (double)(qa_input_to_int32(trunc(angle * 65536 / 360)) & 65535);
}

static qa_angle_words words(qa_vec3 angles) {
  qa_angle_words out;
  out.values[0] = angle_word(angles.x);
  out.values[1] = angle_word(angles.y);
  out.values[2] = angle_word(angles.z);
  return out;
}

static double q1_yaw(double angle) {
  return (double)(qa_input_to_int32(trunc(angle * 65536 / 360)) & 65535) *
         (360.0 / 65536);
}

static double clamp_sym(double value, double maximum) {
  return qa_js_max(-maximum, qa_js_min(maximum, value));
}

struct qa_input_command_builder {
  qa_command_dialect dialect;
  qa_mouse_input *mouse;
  bool owns_mouse;
  qa_mouse_input owned_mouse;
  qa_pitch_drift pitch_drift;
  bool previous_mouse_look;
  qa_pitch_drift_settings_fn drift_settings;
  void *drift_settings_ctx;
  qa_vec3 angles;
  qa_view_input_tuning tuning;
  qa_always_run_read_fn run_read;
  qa_always_run_write_fn run_write;
  void *run_ctx;
};

static void default_drift_settings(void *ctx, double *out_speed, double *out_delay) {
  (void)ctx;
  *out_speed = 500;
  *out_delay = 0.15;
}

qa_input_command_builder *qa_input_command_builder_create(
    qa_command_dialect dialect, qa_mouse_input *mouse,
    const qa_view_input_tuning *tuning) {
  qa_input_command_builder *builder =
      (qa_input_command_builder *)calloc(1, sizeof *builder);
  if (builder == NULL) return NULL;
  builder->dialect = dialect;
  if (mouse != NULL) {
    builder->mouse = mouse;
  } else {
    qa_mouse_input_init(&builder->owned_mouse, qa_default_mouse_tuning);
    builder->mouse = &builder->owned_mouse;
    builder->owns_mouse = true;
  }
  qa_pitch_drift_init(&builder->pitch_drift);
  builder->drift_settings = default_drift_settings;
  builder->tuning =
      tuning != NULL ? *tuning : qa_default_view_input_tuning(dialect);
  (void)builder->owns_mouse;
  return builder;
}

void qa_input_command_builder_destroy(qa_input_command_builder *builder) {
  free(builder);
}

qa_command_dialect qa_input_command_builder_dialect(
    const qa_input_command_builder *builder) {
  return builder->dialect;
}

qa_mouse_input *qa_input_command_builder_mouse(qa_input_command_builder *builder) {
  return builder->mouse;
}

void qa_input_command_builder_bind_pitch_drift(
    qa_input_command_builder *builder, qa_pitch_drift_settings_fn settings,
    void *settings_ctx) {
  builder->drift_settings = settings;
  builder->drift_settings_ctx = settings_ctx;
}

qa_view_input_tuning qa_input_command_builder_tuning(
    const qa_input_command_builder *builder) {
  qa_view_input_tuning tuning = builder->tuning;
  if (builder->run_read != NULL) tuning.always_run = builder->run_read(builder->run_ctx);
  return tuning;
}

void qa_input_command_builder_set_tuning(qa_input_command_builder *builder,
                                         qa_view_input_tuning value) {
  if (builder->run_write == NULL) {
    builder->tuning = value;
  } else {
    builder->run_write(builder->run_ctx, value.always_run);
    builder->tuning = value;
  }
}

void qa_input_command_builder_bind_always_run(qa_input_command_builder *builder,
                                              qa_always_run_read_fn read,
                                              qa_always_run_write_fn write,
                                              void *ctx) {
  builder->run_read = read;
  builder->run_write = write;
  builder->run_ctx = ctx;
}

qa_vec3 qa_input_command_builder_view_angles(
    const qa_input_command_builder *builder) {
  return builder->angles;
}

void qa_input_command_builder_set_view_angles(qa_input_command_builder *builder,
                                              qa_vec3 angles) {
  if (!isfinite(angles.x) || !isfinite(angles.y) || !isfinite(angles.z)) {
    qa_range_error("View angles must be finite");
  }
  builder->angles = angles;
}

void qa_input_command_builder_center_view(qa_input_command_builder *builder,
                                          double delta_pitch) {
  builder->angles.x = -delta_pitch;
}

void qa_input_command_builder_clear(qa_input_command_builder *builder) {
  builder->angles.x = 0;
  builder->angles.y = 0;
  builder->angles.z = 0;
  qa_mouse_input_clear(builder->mouse);
  qa_pitch_drift_clear(&builder->pitch_drift);
  builder->previous_mouse_look = false;
}

static const qa_button_sample *find_button(const qa_seat_input_sample *sample,
                                           const char *action) {
  for (size_t i = 0; i < sample->button_count; i++) {
    if (strcmp(sample->buttons[i].action, action) == 0) return &sample->buttons[i];
  }
  return NULL;
}

static double fr(const qa_seat_input_sample *sample, const char *action) {
  const qa_button_sample *button = find_button(sample, action);
  return button != NULL ? button->fraction : 0;
}

static bool act(const qa_seat_input_sample *sample, const char *action) {
  const qa_button_sample *button = find_button(sample, action);
  return button != NULL && button->active;
}

static bool pressed_of(const qa_seat_input_sample *sample, const char *action) {
  const qa_button_sample *button = find_button(sample, action);
  return act(sample, action) || (button != NULL && button->pressed);
}

qa_user_command qa_input_command_builder_build(
    qa_input_command_builder *builder, const qa_seat_input_sample *sample,
    const qa_user_command_frame *frame, double source_frame_ms) {
  if (frame->kind != builder->dialect) {
    qa_input_error("User-command frame and input dialect differ");
  }
  bool q3 = frame->kind == QA_COMMAND_DIALECT_Q3;
  bool q1 = qa_command_dialect_is_q1(frame->kind);
  qa_view_input_tuning tuning = qa_input_command_builder_tuning(builder);
  bool speed = act(sample, "walk");
  bool strafe = act(sample, "strafe");
  bool klook = act(sample, "klook");
  double angle_speed =
      q3 ? (double)(float)(source_frame_ms / 1000 *
                           (speed ? tuning.angle_speed_multiplier : 1))
         : source_frame_ms / 1000 * (speed ? tuning.angle_speed_multiplier : 1);
  double previous_pitch = builder->angles.x;
  double pitch = builder->angles.x, yaw = builder->angles.y, roll = builder->angles.z;
#define F(value) (q3 ? (double)(float)(value) : (value))
  if (!strafe) {
    yaw = F(yaw - F(F(angle_speed * tuning.yaw_speed) * fr(sample, "turn-right")));
    yaw = F(yaw + F(F(angle_speed * tuning.yaw_speed) * fr(sample, "turn-left")));
    if (q1) yaw = q1_yaw(yaw);
  }
  if (klook && !q3) {
    pitch = F(pitch - F(angle_speed * tuning.pitch_speed * fr(sample, "forward")));
    pitch = F(pitch + F(angle_speed * tuning.pitch_speed * fr(sample, "back")));
  }
  pitch = F(pitch - F(F(angle_speed * tuning.pitch_speed) * fr(sample, "look-up")));
  pitch = F(pitch + F(F(angle_speed * tuning.pitch_speed) * fr(sample, "look-down")));
  if (q1) {
    pitch = qa_js_max(-70, qa_js_min(80, pitch));
    roll = clamp_sym(roll, 50);
  }
  bool running = speed != tuning.always_run;
  double move_speed = q3 ? (running ? 127 : 64) : 1;
  double forward = 0, side = 0, up = 0;
#define ADD(value, amount) \
  (q3 ? trunc(F(F(value) + F(amount))) : (value) + (amount))
  if (strafe) {
    side = ADD(side, F((q3 ? move_speed : tuning.side_speed) * fr(sample, "turn-right")));
    side = ADD(side, -F((q3 ? move_speed : tuning.side_speed) * fr(sample, "turn-left")));
  }
  side = ADD(side, F((q3 ? move_speed : tuning.side_speed) * fr(sample, "move-right")));
  side = ADD(side, -F((q3 ? move_speed : tuning.side_speed) * fr(sample, "move-left")));
  double jump = q1 ? fr(sample, "move-up")
                   : qa_js_max(fr(sample, "jump"), fr(sample, "move-up"));
  double crouch = q1 ? fr(sample, "move-down")
                     : qa_js_max(fr(sample, "crouch"), fr(sample, "move-down"));
  up = ADD(up, F((q3 ? move_speed : tuning.up_speed) * jump));
  up = ADD(up, -F((q3 ? move_speed : tuning.up_speed) * crouch));
  if (!klook || q3) {
    forward = ADD(forward, F((q3 ? move_speed : tuning.forward_speed) * fr(sample, "forward")));
    forward = ADD(forward, -F((q3 ? move_speed : tuning.back_speed) * fr(sample, "back")));
  }
  if (!q3 && running) {
    forward *= tuning.move_speed_multiplier;
    side *= tuning.move_speed_multiplier;
    up *= tuning.move_speed_multiplier;
  }
  qa_mouse_move mouse = qa_mouse_input_sample(
      builder->mouse, sample->mouse, sample->frame_milliseconds, strafe,
      act(sample, "mlook"), q3 ? frame->sensitivity : 1, q3);
  forward = ADD(forward, mouse.forward);
  side = ADD(side, mouse.side);
  yaw = F(yaw + mouse.yaw - sample->gamepad_look_degrees.x);
  pitch = F(pitch + mouse.pitch + sample->gamepad_look_degrees.y);
  double pad_forward =
      q3 ? move_speed
         : tuning.forward_speed * (running ? tuning.move_speed_multiplier : 1);
  double pad_side =
      q3 ? move_speed
         : tuning.side_speed * (running ? tuning.move_speed_multiplier : 1);
  forward = ADD(forward, sample->gamepad_move.y * pad_forward);
  side = ADD(side, sample->gamepad_move.x * pad_side);
  qa_mouse_tuning mouse_tuning = qa_mouse_input_tuning(builder->mouse);
  if (q1 && frame->has_pitch_drift) {
    bool mouse_look = act(sample, "mlook");
    double drift_speed, drift_delay;
    builder->drift_settings(builder->drift_settings_ctx, &drift_speed, &drift_delay);
    bool manual = mouse_look || mouse_tuning.free_look ||
                  (klook && (act(sample, "forward") || act(sample, "back"))) ||
                  fr(sample, "look-up") != 0 || fr(sample, "look-down") != 0 ||
                  sample->gamepad_look_degrees.y != 0;
    pitch = qa_pitch_drift_sample(
        &builder->pitch_drift, pitch, source_frame_ms, frame->pitch_drift, manual,
        !mouse_look && (builder->previous_mouse_look || pressed_of(sample, "mlook")) &&
            mouse_tuning.look_spring,
        forward,
        frame->kind == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ? 200 : tuning.forward_speed,
        drift_speed, drift_delay);
  }
  builder->previous_mouse_look = act(sample, "mlook");
  int64_t buttons = 0;
  bool any = sample->any_key_down != 0;
  bool game_focus = sample->focus.kind != NULL &&
                    strcmp(sample->focus.kind, "game") == 0;
  if (q3) {
    for (int index = 0; index < 15; index++) {
      char name[16];
      snprintf(name, sizeof name, "button%d", index);
      if (pressed_of(sample, name)) buttons |= (int64_t)1 << index;
    }
    if (pressed_of(sample, "attack")) buttons |= 1;
    if (pressed_of(sample, "use")) buttons |= 4;
    if (!running) buttons |= 16;
    if (!game_focus) buttons |= 2;
    else if (any) buttons |= 2048;
    pitch = qa_js_max(previous_pitch - 90, qa_js_min(previous_pitch + 90, pitch));
  } else {
    if (pressed_of(sample, "attack") && (q1 || frame->attack_allowed)) buttons |= 1;
    if ((q1 && (pressed_of(sample, "jump") || pressed_of(sample, "move-up"))) ||
        (!q1 && pressed_of(sample, "use"))) {
      buttons |= 2;
    }
    if (!q1 && any && game_focus) buttons |= 128;
    if (frame->kind == QA_COMMAND_DIALECT_Q2_RERELEASE) {
      if (pressed_of(sample, "holster")) buttons |= 4;
      if (pressed_of(sample, "jump") || pressed_of(sample, "move-up")) buttons |= 8;
      if (pressed_of(sample, "crouch") || pressed_of(sample, "move-down")) buttons |= 16;
    }
  }
  if (q1) pitch = qa_js_max(-70, qa_js_min(80, pitch));
  if (frame->kind == QA_COMMAND_DIALECT_Q2_CLASSIC ||
      frame->kind == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    double delta = frame->delta_angles.x;
    if (delta > 180) delta -= 360;
    if (pitch + delta < -360) pitch += 360;
    if (pitch + delta > 360) pitch -= 360;
    pitch = qa_js_max(-89 - delta, qa_js_min(89 - delta, pitch));
    forward = clamp_sym(forward, 400);
    side = clamp_sym(side, 400);
  }
  builder->angles.x = pitch;
  builder->angles.y = yaw;
  builder->angles.z = roll;
  double milliseconds = trunc(source_frame_ms > 250 ? 100 : source_frame_ms);
  qa_user_command command;
  memset(&command, 0, sizeof command);
  switch (frame->kind) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE:
      command.kind = QA_USER_COMMAND_Q1;
      command.as.q1.acknowledged_server_time_seconds =
          frame->acknowledged_server_time_seconds;
      command.as.q1.view_angles = builder->angles;
      command.as.q1.forward_move = trunc(forward);
      command.as.q1.side_move = trunc(side);
      command.as.q1.up_move = trunc(up);
      command.as.q1.buttons = (double)buttons;
      command.as.q1.impulse = sample->impulse;
      break;
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD:
      command.kind = QA_USER_COMMAND_QW;
      command.as.qw.milliseconds = milliseconds;
      command.as.qw.angles = builder->angles;
      command.as.qw.forward_move = trunc(forward);
      command.as.qw.side_move = trunc(side);
      command.as.qw.up_move = trunc(up);
      command.as.qw.buttons = (double)buttons;
      command.as.qw.impulse = sample->impulse;
      break;
    case QA_COMMAND_DIALECT_Q2_CLASSIC:
      command.kind = QA_USER_COMMAND_Q2;
      command.as.q2.milliseconds = milliseconds;
      command.as.q2.angle_shorts = words(builder->angles);
      command.as.q2.forward_move = trunc(forward);
      command.as.q2.side_move = trunc(side);
      command.as.q2.up_move = trunc(up);
      command.as.q2.buttons = (double)buttons;
      command.as.q2.impulse = sample->impulse;
      command.as.q2.light_level = frame->light_level;
      break;
    case QA_COMMAND_DIALECT_Q2_RERELEASE:
      command.kind = QA_USER_COMMAND_Q2_RERELEASE;
      command.as.q2_rerelease.milliseconds = milliseconds;
      command.as.q2_rerelease.angles = builder->angles;
      command.as.q2_rerelease.forward_move = forward;
      command.as.q2_rerelease.side_move = side;
      command.as.q2_rerelease.buttons = (double)buttons;
      command.as.q2_rerelease.server_frame = frame->server_frame;
      break;
    case QA_COMMAND_DIALECT_Q3:
      command.kind = QA_USER_COMMAND_Q3;
      command.as.q3.server_time_milliseconds = frame->server_time_milliseconds;
      command.as.q3.angle_words = words(builder->angles);
      command.as.q3.forward_move = trunc(clamp_sym(forward, 127));
      command.as.q3.right_move = trunc(clamp_sym(side, 127));
      command.as.q3.up_move = trunc(clamp_sym(up, 127));
      command.as.q3.buttons = (double)buttons;
      command.as.q3.weapon = frame->weapon;
      break;
  }
#undef ADD
#undef F
  return command;
}
