/* Port of src/movement/q3/prediction.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/prediction.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

static double f32(double v) { return (double)(float)v; }
static qa_vec3 v3(double x, double y, double z) { qa_vec3 r = {x, y, z}; return r; }
static qa_q3_user_command empty_command(void) {
  qa_q3_user_command c = {0};
  return c;
}
static bool is_integer(double v) { return isfinite(v) && trunc(v) == v; }

void qa_q3_command_history_init(qa_q3_command_history *history) {
  for (int i = 0; i < 64; i++) history->commands[i] = empty_command();
  history->number = 0;
}
double qa_q3_command_history_current_number(const qa_q3_command_history *history) { return history->number; }
double qa_q3_command_history_append(qa_q3_command_history *history, qa_q3_user_command command) {
  history->number = (double)qa_int32(history->number + 1);
  history->commands[qa_int32(history->number) & 63] = command;
  return history->number;
}
bool qa_q3_command_history_read(const qa_q3_command_history *history, double number, qa_q3_user_command *out) {
  if (!is_integer(number)) qa_range_error("Invalid user command number");
  if (number > history->number)
    qa_range_error("CL_GetUserCmd: %.0f >= %.0f", number, history->number);
  if (number <= (double)qa_int32(history->number - 64)) return false;
  *out = history->commands[qa_int32(number) & 63];
  return true;
}
static double history_current(void *ctx) { return ((qa_q3_command_history *)ctx)->number; }
static bool history_read(void *ctx, double number, qa_q3_user_command *out) {
  return qa_q3_command_history_read(ctx, number, out);
}
qa_q3_command_source qa_q3_command_history_source(qa_q3_command_history *history) {
  qa_q3_command_source s = {history, history_current, history_read};
  return s;
}

static qa_vec3 interpolate_vector(qa_vec3 a, qa_vec3 b, double fraction) {
  return v3(f32(a.x + f32(fraction * f32(b.x - a.x))), f32(a.y + f32(fraction * f32(b.y - a.y))),
            f32(a.z + f32(fraction * f32(b.z - a.z))));
}
static int32_t word16(double a, double b) { return (int32_t)((int16_t)qa_int32(a + b)); }

qa_q3_movement_state qa_q3_update_prediction_view(qa_q3_movement_state state, double health, qa_q3_user_command command) {
  if (state.movement_type == QA_Q3_PM_INTERMISSION || state.movement_type == QA_Q3_PM_SPINTERMISSION ||
      (state.movement_type != QA_Q3_PM_SPECTATOR && health <= 0))
    return state;
  double delta_pitch = state.delta_angle_words.values[0];
  int32_t pitch = word16(command.angle_words.values[0], delta_pitch);
  if (pitch > 16000) { delta_pitch = (double)qa_int32(16000 - command.angle_words.values[0]); pitch = 16000; }
  else if (pitch < -16000) { delta_pitch = (double)qa_int32(-16000 - command.angle_words.values[0]); pitch = -16000; }
  int32_t yaw = word16(command.angle_words.values[1], state.delta_angle_words.values[1]);
  int32_t roll = word16(command.angle_words.values[2], state.delta_angle_words.values[2]);
  state.delta_angle_words.values[0] = delta_pitch;
  state.view_angles = v3(pitch * (360.0 / 65536.0), yaw * (360.0 / 65536.0), roll * (360.0 / 65536.0));
  return state;
}

void qa_q3_prediction_runtime_init(qa_q3_prediction_runtime *runtime, qa_q3_prediction_host host) {
  memset(runtime, 0, sizeof *runtime);
  runtime->host = host;
  runtime->command = empty_command();
}
static qa_q3_user_command required_command(qa_q3_prediction_runtime *r, double number) {
  qa_q3_user_command command;
  if (!r->host.commands.read(r->host.commands.ctx, number, &command))
    qa_range_error("Prediction command source violated its CMD_BACKUP window");
  return command;
}
static qa_q3_predicted_actor interpolate(qa_q3_prediction_runtime *r, qa_q3_prediction_frame frame, bool grab_angles) {
  qa_q3_prediction_snapshot previous = frame.snapshot;
  qa_q3_movement_state movement = previous.actor.movement;
  if (grab_angles)
    movement = qa_q3_update_prediction_view(movement, frame.health,
        required_command(r, r->host.commands.current_number(r->host.commands.ctx)));
  const qa_q3_prediction_snapshot *next = frame.next_snapshot;
  if (frame.next_frame_teleport || next == NULL || next->server_time_milliseconds <= previous.server_time_milliseconds) {
    qa_q3_predicted_actor actor = previous.actor; actor.movement = movement; return actor;
  }
  double fraction = f32(f32((double)qa_int32(frame.time_milliseconds - previous.server_time_milliseconds)) /
                        f32((double)qa_int32(next->server_time_milliseconds - previous.server_time_milliseconds)));
  qa_q3_movement_state a = previous.actor.movement, b = next->actor.movement;
  double cycle = b.bob_cycle < a.bob_cycle ? (double)qa_int32(b.bob_cycle + 256) : b.bob_cycle;
  qa_q3_predicted_actor actor = previous.actor;
  actor.movement = movement;
  actor.movement.bob_cycle = qa_qvm_float_to_int(f32(f32(a.bob_cycle) + f32(fraction * f32((double)qa_int32(cycle - a.bob_cycle)))));
  actor.movement.origin = interpolate_vector(a.origin, b.origin, fraction);
  actor.movement.velocity = interpolate_vector(a.velocity, b.velocity, fraction);
  if (!grab_angles)
    actor.movement.view_angles = v3(
        qa_lerp_angle_with_rule(a.view_angles.x, b.view_angles.x, fraction, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a.view_angles.y, b.view_angles.y, fraction, QA_LERP_ANGLE_FLOAT),
        qa_lerp_angle_with_rule(a.view_angles.z, b.view_angles.z, fraction, QA_LERP_ANGLE_FLOAT));
  return actor;
}
static void push_effects(qa_ordered_movement_effect **effects, size_t *count, size_t *capacity,
                         const qa_ordered_movement_effect *src, size_t src_count) {
  for (size_t i = 0; i < src_count; i++) {
    if (*count == *capacity) {
      *capacity = *capacity ? *capacity * 2 : 8;
      *effects = realloc(*effects, *capacity * sizeof **effects);
      if (*effects == NULL) qa_range_error("Out of memory");
    }
    (*effects)[(*count)++] = src[i];
  }
}

qa_q3_prediction_output qa_q3_prediction_predict(qa_q3_prediction_runtime *r, qa_q3_prediction_frame frame) {
  qa_q3_prediction_settings settings = r->host.settings(r->host.ctx);
  qa_q3_prediction_snapshot snapshot = frame.snapshot;
  if (!r->has_predicted) { r->predicted = snapshot.actor; r->has_predicted = true; }
  bool follow = (qa_int32(snapshot.actor.movement.movement_flags) & QA_Q3_FLAG_FOLLOW) != 0;
  if (settings.demo_playback || follow || settings.no_predict || settings.synchronous_clients) {
    bool grab_angles = !settings.demo_playback && !follow;
    r->predicted = interpolate(r, frame, grab_angles);
    qa_q3_prediction_output out = {"interpolated", true, r->predicted, false, NULL, 0, false};
    return out;
  }
  qa_q3_predicted_actor old = r->predicted;
  double current = r->host.commands.current_number(r->host.commands.ctx);
  qa_q3_user_command oldest = required_command(r, (double)qa_int32(current - 63));
  if (oldest.server_time_milliseconds > snapshot.actor.movement.command_time_milliseconds &&
      oldest.server_time_milliseconds < frame.time_milliseconds) {
    if (settings.show_miss != 0) r->host.warn(r->host.ctx, "exceeded PACKET_BACKUP on commands\n");
    qa_q3_prediction_output out = {"command-backup-exceeded", true, old, false, NULL, 0, false};
    return out;
  }
  qa_q3_user_command latest = required_command(r, current);
  qa_q3_prediction_snapshot selected = (frame.next_snapshot != NULL && !frame.next_frame_teleport &&
                                        !frame.this_frame_teleport) ? *frame.next_snapshot : snapshot;
  r->host.restore_snapshot(r->host.ctx, selected);
  r->predicted = selected.actor;
  double physics_time = selected.server_time_milliseconds;
  if (settings.movement_milliseconds < 8) r->host.set_movement_milliseconds(r->host.ctx, 8);
  else if (settings.movement_milliseconds > 33) r->host.set_movement_milliseconds(r->host.ctx, 33);
  qa_ordered_movement_effect *effects = NULL; size_t effect_count = 0, effect_capacity = 0;
  bool moved = false, hyperspace = false, consumed_teleport = false;
  for (int32_t number = qa_int32(current - 63); number <= qa_int32(current); number++) {
    qa_q3_user_command read_command;
    if (r->host.commands.read(r->host.commands.ctx, number, &read_command)) r->command = read_command;
    if (settings.fixed) r->predicted.movement = qa_q3_update_prediction_view(r->predicted.movement, frame.health, r->command);
    qa_q3_movement_state ps = r->predicted.movement;
    if (r->command.server_time_milliseconds <= ps.command_time_milliseconds ||
        r->command.server_time_milliseconds > latest.server_time_milliseconds) continue;
    if (ps.command_time_milliseconds == old.movement.command_time_milliseconds) {
      if (frame.this_frame_teleport && !consumed_teleport) {
        r->error = v3(0, 0, 0); consumed_teleport = true;
        if (settings.show_miss != 0) r->host.warn(r->host.ctx, "PredictionTeleport\n");
      } else {
        qa_vec3 adjusted = r->host.adjust_for_mover(r->host.ctx, ps.origin, ps.ground, physics_time, frame.previous_time_milliseconds);
        qa_vec3 delta = qa_sub3(old.movement.origin, adjusted);
        double length = f32(qa_length3(delta));
        if (length > f32(0.1)) {
          if (settings.show_miss != 0) {
            char buffer[128];
            snprintf(buffer, sizeof buffer, "Prediction miss: %f\n", length);
            r->host.warn(r->host.ctx, buffer);
          }
          if (settings.error_decay_integer != 0) {
            double elapsed = (double)qa_int32(frame.time_milliseconds - r->error_time);
            double fraction = f32(f32(f32(settings.error_decay_value) - f32(elapsed)) / f32(settings.error_decay_value));
            if (fraction < 0) fraction = 0;
            r->error = qa_scale3(r->error, fraction);
          } else r->error = v3(0, 0, 0);
          r->error = qa_add3(delta, r->error); r->error_time = frame.previous_time_milliseconds;
        }
      }
    }
    if (settings.fixed)
      r->command.server_time_milliseconds = (double)(int32_t)(trunc((double)qa_int32(
          r->command.server_time_milliseconds + settings.movement_milliseconds - 1) /
          settings.movement_milliseconds) * (int64_t)settings.movement_milliseconds);
    qa_q3_movement_input input = r->host.movement_input(r->host.ctx, r->predicted, r->command, number, physics_time);
    input.state.kind = QA_MOVEMENT_STATE_Q3; input.state.as.q3 = r->predicted.movement;
    input.arsenal = r->predicted.arsenal; input.animation = r->predicted.animation;
    input.command.kind = QA_USER_COMMAND_Q3; input.command.as.q3 = r->command;
    input.command_sequence = number; input.execution = "prediction";
    input.profile.has_fixed_milliseconds = settings.fixed;
    input.profile.fixed_milliseconds = settings.movement_milliseconds;
    qa_movement_result result = r->host.movement.move(r->host.movement.ctx, input,
                                                      r->host.movement_services(r->host.ctx, physics_time));
    push_effects(&effects, &effect_count, &effect_capacity, result.effects, result.effect_count);
    if (result.status != NULL && strcmp(result.status, "actor-removed") == 0) {
      qa_q3_prediction_output out = {0}; out.status = "actor-removed"; out.effects = effects; out.effect_count = effect_count; out.consumed_teleport = consumed_teleport;
      return out;
    }
    r->predicted.movement = result.state.as.q3;
    r->predicted.arsenal = result.arsenal; r->predicted.animation = result.animation;
    moved = true;
    qa_q3_prediction_triggers triggers = r->host.touch_triggers(r->host.ctx, r->predicted, result.bounds, physics_time);
    r->predicted = triggers.actor; hyperspace = hyperspace || triggers.hyperspace;
    push_effects(&effects, &effect_count, &effect_capacity, triggers.effects, triggers.effect_count);
    /* TS result contacts/effects arrays are GC-owned; C result buffers stay caller-owned here. */
  }
  if (!moved) {
    qa_q3_prediction_output out = {"unchanged", true, r->predicted, hyperspace, effects, effect_count, consumed_teleport};
    return out;
  }
  r->predicted.movement.origin = r->host.adjust_for_mover(r->host.ctx, r->predicted.movement.origin,
      r->predicted.movement.ground, physics_time, frame.time_milliseconds);
  if (settings.show_miss != 0 &&
      r->predicted.movement.predictable_event_sequence > (double)qa_int32(old.movement.predictable_event_sequence + 2))
    r->host.warn(r->host.ctx, "WARNING: dropped event\n");
  r->host.transition(r->host.ctx, r->predicted, old, effects, effect_count);
  qa_q3_prediction_output out = {"predicted", true, r->predicted, hyperspace, effects, effect_count, consumed_teleport};
  return out;
}
