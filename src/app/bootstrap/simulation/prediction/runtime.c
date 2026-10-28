/* Port of src/app/bootstrap/simulation/prediction/runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/foundation/support.h"

qa_user_command qa_app_copy_prediction_command(qa_user_command command) { return command; }

struct qa_app_selected_movement_prediction {
  qa_app_movement_prediction_options options;
  qa_q2_rerelease_movement_context rerelease_movement;
  qa_app_movement_prediction_snapshot snapshot;
  qa_app_prediction_command commands[64];
  size_t command_count;
  double discarded_sequence;
};

static const char *state_kind_name(qa_movement_state_kind kind) {
  switch (kind) {
    case QA_MOVEMENT_STATE_Q1: return "q1-netquake";
    case QA_MOVEMENT_STATE_QW: return "q1-quakeworld";
    case QA_MOVEMENT_STATE_Q2: return "q2-classic";
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return "q2-rerelease";
    case QA_MOVEMENT_STATE_Q3: return "q3";
  }
  return "";
}
static const char *command_kind_name(qa_user_command_kind kind) {
  switch (kind) {
    case QA_USER_COMMAND_Q1: return "q1-netquake";
    case QA_USER_COMMAND_QW: return "q1-quakeworld";
    case QA_USER_COMMAND_Q2: return "q2-classic";
    case QA_USER_COMMAND_Q2_RERELEASE: return "q2-rerelease";
    case QA_USER_COMMAND_Q3: return "q3";
  }
  return "";
}

qa_app_selected_movement_prediction *qa_app_selected_movement_prediction_create(
    qa_app_movement_prediction_options options, qa_app_movement_prediction_snapshot initial) {
  if (!qa_same_actor((qa_actor_id){options.probe.actor.id.session, 0, 0, options.probe.actor.id.token},
                     (qa_actor_id){options.seat.session, 0, 0, options.probe.actor.id.token})
      && options.probe.actor.id.session.token != options.seat.session.token)
    qa_range_error("Prediction actor and seat belong to different sessions");
  if (!options.probe.profile.kind || strcmp(state_kind_name(initial.state.kind), options.probe.profile.kind) != 0)
    qa_range_error("Prediction snapshot does not match selected movement");
  qa_app_selected_movement_prediction *prediction = calloc(1, sizeof *prediction);
  if (!prediction) return NULL;
  prediction->options = options;
  prediction->snapshot = qa_app_copy_prediction_snapshot(initial);
  prediction->discarded_sequence = -1;
  return prediction;
}
void qa_app_selected_movement_prediction_destroy(qa_app_selected_movement_prediction *prediction) {
  if (!prediction) return;
  qa_app_movement_prediction_snapshot_destroy(&prediction->snapshot);
  free(prediction);
}
const qa_app_movement_prediction_options *qa_app_selected_movement_prediction_options(
    const qa_app_selected_movement_prediction *prediction) { return &prediction->options; }

void qa_app_selected_movement_prediction_receive(qa_app_selected_movement_prediction *prediction,
    qa_app_movement_prediction_snapshot snapshot) {
  if (strcmp(state_kind_name(snapshot.state.kind), prediction->options.probe.profile.kind) != 0)
    qa_range_error("Prediction snapshot changed movement family");
  qa_app_movement_prediction_snapshot_destroy(&prediction->snapshot);
  prediction->snapshot = qa_app_copy_prediction_snapshot(snapshot);
  while (prediction->command_count && prediction->commands[0].sequence <= snapshot.sequence) {
    memmove(prediction->commands, prediction->commands + 1, (prediction->command_count - 1) * sizeof *prediction->commands);
    prediction->command_count--;
  }
}

void qa_app_selected_movement_prediction_submit(qa_app_selected_movement_prediction *prediction,
    qa_app_prediction_command entry) {
  if (entry.sequence != (double)(long long)entry.sequence || !isfinite(entry.time_milliseconds))
    qa_range_error("Prediction command needs a finite clock and integer sequence");
  if (strcmp(command_kind_name(entry.command.kind), prediction->options.probe.profile.kind) != 0)
    qa_range_error("Prediction command does not match selected movement");
  if (entry.sequence <= prediction->snapshot.sequence) return;
  if (prediction->command_count && entry.sequence <= prediction->commands[prediction->command_count - 1].sequence) return;
  if (prediction->command_count >= 64) {
    prediction->discarded_sequence = prediction->commands[0].sequence;
    memmove(prediction->commands, prediction->commands + 1, 63 * sizeof *prediction->commands);
    prediction->command_count = 63;
  }
  entry.command = qa_app_copy_prediction_command(entry.command);
  prediction->commands[prediction->command_count++] = entry;
}

static double short_angle(double word) { return (double)((((long long)word) << 16) >> 16) * 360.0 / 65536.0; }

qa_app_movement_prediction_result qa_app_selected_movement_prediction_replay(
    qa_app_selected_movement_prediction *prediction,
    void (*observe)(void *ctx, qa_app_movement_prediction_snapshot snapshot), void *observe_ctx) {
  qa_app_movement_prediction_result out; memset(&out, 0, sizeof out);
  qa_app_movement_prediction_snapshot player = qa_app_copy_prediction_snapshot(prediction->snapshot);
  bool q2 = player.state.kind == QA_MOVEMENT_STATE_Q2 || player.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE;
  double flags = player.state.kind == QA_MOVEMENT_STATE_Q2 ? player.state.as.q2.flags
      : player.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE ? player.state.as.q2_rerelease.flags : 0;
  if (q2 && (((long long)flags) & 64) != 0) {
    if (prediction->command_count) {
      qa_app_prediction_command latest = prediction->commands[prediction->command_count - 1];
      qa_user_command command = prediction->options.probe.relative_command
          ? prediction->options.probe.relative_command(prediction->options.probe.host_ctx, latest, player.state)
          : latest.command;
      if (player.state.kind == QA_MOVEMENT_STATE_Q2 && command.kind == QA_USER_COMMAND_Q2) {
        player.view_angles.x = short_angle(command.as.q2.angle_shorts.values[0]) + short_angle(player.state.as.q2.delta_angle_shorts.values[0]);
        player.view_angles.y = short_angle(command.as.q2.angle_shorts.values[1]) + short_angle(player.state.as.q2.delta_angle_shorts.values[1]);
        player.view_angles.z = short_angle(command.as.q2.angle_shorts.values[2]) + short_angle(player.state.as.q2.delta_angle_shorts.values[2]);
      } else if (player.state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE && command.kind == QA_USER_COMMAND_Q2_RERELEASE) {
        player.view_angles.x = command.as.q2_rerelease.angles.x + player.state.as.q2_rerelease.delta_angles.x;
        player.view_angles.y = command.as.q2_rerelease.angles.y + player.state.as.q2_rerelease.delta_angles.y;
        player.view_angles.z = command.as.q2_rerelease.angles.z + player.state.as.q2_rerelease.delta_angles.z;
      }
    }
    out.status = "disabled"; out.player = player; return out;
  }
  double newest = prediction->command_count ? prediction->commands[prediction->command_count - 1].sequence : player.sequence;
  if (prediction->discarded_sequence > player.sequence || newest - player.sequence >= 63) {
    out.status = "history-exhausted"; out.player = player; return out;
  }
  bool first = true;
  for (size_t i = 0; i < prediction->command_count; i++) {
    qa_app_prediction_step_options options; memset(&options, 0, sizeof options);
    options.scene = prediction->options.probe.scene;
    options.rerelease_movement = &prediction->rerelease_movement;
    if (strcmp(prediction->options.probe.profile.kind, "q3") == 0) {
      options.has_fixed_milliseconds = prediction->options.probe.profile.has_fixed_milliseconds;
      options.fixed_milliseconds = prediction->options.probe.profile.fixed_milliseconds;
      options.no_footsteps = prediction->options.probe.profile.no_footsteps;
    }
    options.first_command = first;
    qa_app_prediction_step_result stepped = qa_app_predict_movement_command(&prediction->options.probe, player,
        prediction->commands[i], options, NULL);
    qa_app_movement_prediction_snapshot_destroy(&player);
    player = stepped.player;
    if (stepped.result.effect_count) {
      qa_ordered_movement_effect *effects = realloc(out.effects, (out.effect_count + stepped.result.effect_count) * sizeof *effects);
      if (!effects) qa_range_error("Out of memory");
      out.effects = effects;
      memcpy(out.effects + out.effect_count, stepped.result.effects, stepped.result.effect_count * sizeof *effects);
      out.effect_count += stepped.result.effect_count;
    }
    first = false;
    if (observe) observe(observe_ctx, qa_app_copy_prediction_snapshot(player));
  }
  out.status = first ? "unchanged" : "predicted";
  out.player = player;
  return out;
}
