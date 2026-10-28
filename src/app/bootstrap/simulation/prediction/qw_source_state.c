/* Port of src/app/bootstrap/simulation/prediction/qw-source-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/qw_source_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/foundation/support.h"

qa_movement_profile qa_app_qw_prediction_profile(qa_movement_profile base, qa_app_qw_move_variables variables) {
  qa_movement_profile profile = base;
  profile.kind = "q1-quakeworld";
  memset(&profile.clock, 0, sizeof profile.clock);
  profile.clock.kind = QA_CLOCK_Q1_QUAKEWORLD;
  profile.clock.maximum_command_milliseconds = 50;
  profile.parameters = variables;
  return profile;
}

qa_app_movement_prediction_snapshot qa_app_qw_prediction_snapshot(qa_app_movement_prediction_snapshot base,
    qa_qw_player_state player, qa_app_qw_prediction_status status,
    const qa_app_movement_prediction_snapshot *acknowledged, double sequence, double command_time_milliseconds) {
  if (status.spectator != 0) qa_range_error("The native QW graphical player slice does not admit spectators");
  if (base.state.kind != QA_MOVEMENT_STATE_QW) qa_range_error("QW prediction requires a QW snapshot");
  qa_qw_movement_state continuation = acknowledged && acknowledged->state.kind == QA_MOVEMENT_STATE_QW
      ? acknowledged->state.as.qw : base.state.as.qw;
  double view_height = (((long long)player.flags) & 1024) != 0 ? 8 : (((long long)player.flags) & 512) != 0 ? -16 : 22;
  qa_app_movement_prediction_snapshot next = base;
  next.sequence = sequence; next.command_time_milliseconds = command_time_milliseconds;
  next.state.kind = QA_MOVEMENT_STATE_QW;
  next.state.as.qw.origin = player.origin; next.state.as.qw.velocity = player.velocity;
  next.state.as.qw.angles = base.view_angles;
  next.state.as.qw.old_buttons = continuation.old_buttons;
  next.state.as.qw.water_jump_time_seconds = continuation.water_jump_time_seconds;
  next.state.as.qw.ground = continuation.ground;
  next.state.as.qw.dead = status.health <= 0; next.state.as.qw.spectator = 0;
  next.view_height = view_height; next.view_offset = (qa_vec3){0, 0, view_height};
  next.environment.health = status.health; next.environment.gravity_multiplier = 1;
  if (acknowledged) {
    next.has_contact = acknowledged->has_contact; next.contact_ground = acknowledged->contact_ground;
    next.contact_water_level = acknowledged->contact_water_level; next.contact_water_type = acknowledged->contact_water_type;
  }
  return qa_app_copy_prediction_snapshot(next);
}

typedef struct sent_prediction {
  double sequence;
  qa_qw_user_command command;
  double time_milliseconds;
  bool has_player;
  qa_app_movement_prediction_snapshot player;
} sent_prediction;

struct qa_app_quakeworld_prediction {
  qa_movement_profile profile;
  qa_app_selected_movement_prediction *prediction;
  sent_prediction history[64];
  size_t history_count;
  double acknowledged_sequence, last_sent, view_height;
  qa_app_movement_prediction_result result;
};

static qa_app_movement_prediction_snapshot client_continuation(qa_app_quakeworld_prediction *prediction,
    qa_app_movement_prediction_snapshot player, qa_qw_user_command command) {
  if (player.state.kind != QA_MOVEMENT_STATE_QW) qa_range_error("QW replay changed movement family");
  player.view_height = prediction->view_height;
  player.view_offset = (qa_vec3){0, 0, prediction->view_height};
  player.state.as.qw.old_buttons = command.buttons;
  return player;
}
typedef struct observe_ctx { qa_app_quakeworld_prediction *prediction; } observe_ctx;
static void observe_cb(void *v, qa_app_movement_prediction_snapshot snapshot) {
  observe_ctx *ctx = (observe_ctx *)v;
  for (size_t i = 0; i < ctx->prediction->history_count; i++) {
    if (ctx->prediction->history[i].sequence == snapshot.sequence) {
      if (ctx->prediction->history[i].has_player) qa_app_movement_prediction_snapshot_destroy(&ctx->prediction->history[i].player);
      ctx->prediction->history[i].player = client_continuation(ctx->prediction, qa_app_copy_prediction_snapshot(snapshot), ctx->prediction->history[i].command);
      ctx->prediction->history[i].has_player = true;
      break;
    }
  }
  qa_app_movement_prediction_snapshot_destroy(&snapshot);
}
static void predict(qa_app_quakeworld_prediction *prediction) {
  observe_ctx ctx = {prediction};
  qa_app_movement_prediction_result result = qa_app_selected_movement_prediction_replay(prediction->prediction, observe_cb, &ctx);
  for (size_t i = 0; i < prediction->history_count; i++) {
    if (prediction->history[i].sequence == result.player.sequence) {
      qa_app_movement_prediction_snapshot continued = client_continuation(prediction, qa_app_copy_prediction_snapshot(result.player), prediction->history[i].command);
      qa_app_movement_prediction_snapshot_destroy(&result.player);
      result.player = continued;
      break;
    }
  }
  qa_app_movement_prediction_snapshot_destroy(&prediction->result.player);
  free(prediction->result.effects);
  prediction->result = result;
}

qa_app_quakeworld_prediction *qa_app_quakeworld_prediction_create(qa_app_movement_prediction_options options,
    qa_app_movement_prediction_snapshot initial, qa_app_qw_move_variables variables) {
  if (!options.probe.profile.kind || (strcmp(options.probe.profile.kind, "q1-netquake") != 0 && strcmp(options.probe.profile.kind, "q1-quakeworld") != 0))
    qa_range_error("QW prediction requires Q1 movement parameters");
  if (initial.state.kind != QA_MOVEMENT_STATE_QW || initial.state.as.qw.spectator != 0)
    qa_range_error("QW prediction requires an admitted native player");
  qa_app_quakeworld_prediction *prediction = calloc(1, sizeof *prediction);
  if (!prediction) return NULL;
  prediction->profile = qa_app_qw_prediction_profile(options.probe.profile, variables);
  options.probe.profile = prediction->profile;
  prediction->prediction = qa_app_selected_movement_prediction_create(options, initial);
  if (!prediction->prediction) { free(prediction); return NULL; }
  prediction->acknowledged_sequence = initial.sequence; prediction->last_sent = initial.sequence;
  prediction->view_height = initial.view_height;
  predict(prediction);
  return prediction;
}
void qa_app_quakeworld_prediction_destroy(qa_app_quakeworld_prediction *prediction) {
  if (!prediction) return;
  qa_app_selected_movement_prediction_destroy(prediction->prediction);
  for (size_t i = 0; i < prediction->history_count; i++)
    if (prediction->history[i].has_player) qa_app_movement_prediction_snapshot_destroy(&prediction->history[i].player);
  qa_app_movement_prediction_snapshot_destroy(&prediction->result.player);
  free(prediction->result.effects);
  free(prediction);
}
void qa_app_quakeworld_prediction_sent(qa_app_quakeworld_prediction *prediction, double sequence,
    qa_qw_user_command command, double now) {
  if (sequence <= prediction->last_sent) return;
  qa_app_prediction_command entry; memset(&entry, 0, sizeof entry);
  entry.sequence = sequence; entry.time_milliseconds = now;
  entry.command.kind = QA_USER_COMMAND_QW; entry.command.as.qw = command;
  qa_app_selected_movement_prediction_submit(prediction->prediction, entry);
  prediction->last_sent = sequence;
  if (prediction->history_count >= 64) {
    if (prediction->history[0].has_player) qa_app_movement_prediction_snapshot_destroy(&prediction->history[0].player);
    memmove(prediction->history, prediction->history + 1, 63 * sizeof *prediction->history);
    prediction->history_count = 63;
  }
  sent_prediction *sent = &prediction->history[prediction->history_count++];
  memset(sent, 0, sizeof *sent);
  sent->sequence = sequence; sent->command = command; sent->time_milliseconds = now;
  predict(prediction);
}
void qa_app_quakeworld_prediction_acknowledged(qa_app_quakeworld_prediction *prediction, double sequence, double now) {
  (void)now;
  if (sequence != (double)(long long)sequence) qa_range_error("QW acknowledgement needs an integer channel sequence");
  if (sequence > prediction->acknowledged_sequence) prediction->acknowledged_sequence = sequence;
}
void qa_app_quakeworld_prediction_receive(qa_app_quakeworld_prediction *prediction,
    qa_app_movement_prediction_snapshot base, qa_qw_player_state player, qa_app_qw_move_variables variables,
    qa_app_qw_prediction_status status) {
  sent_prediction *acknowledged = NULL;
  for (size_t i = 0; i < prediction->history_count; i++)
    if (prediction->history[i].sequence == prediction->acknowledged_sequence) acknowledged = &prediction->history[i];
  qa_app_movement_prediction_snapshot snapshot = qa_app_qw_prediction_snapshot(base, player, status,
      acknowledged && acknowledged->has_player ? &acknowledged->player : NULL,
      prediction->acknowledged_sequence, acknowledged ? acknowledged->time_milliseconds : base.command_time_milliseconds);
  prediction->profile = qa_app_qw_prediction_profile(prediction->profile, variables);
  /* TS getter profile: update the inner prediction's borrowed options copy. */
  prediction->view_height = snapshot.view_height;
  qa_app_selected_movement_prediction_receive(prediction->prediction, snapshot);
  qa_app_movement_prediction_snapshot_destroy(&snapshot);
  size_t kept = 0;
  for (size_t i = 0; i < prediction->history_count; i++) {
    if (prediction->history[i].sequence <= prediction->acknowledged_sequence) {
      if (prediction->history[i].has_player) qa_app_movement_prediction_snapshot_destroy(&prediction->history[i].player);
    } else prediction->history[kept++] = prediction->history[i];
  }
  prediction->history_count = kept;
  predict(prediction);
}
qa_app_movement_prediction_result qa_app_quakeworld_prediction_replay(qa_app_quakeworld_prediction *prediction) {
  qa_app_movement_prediction_result result = prediction->result;
  result.player = qa_app_copy_prediction_snapshot(prediction->result.player);
  if (result.effect_count) {
    result.effects = malloc(result.effect_count * sizeof *result.effects);
    if (!result.effects) qa_range_error("Out of memory");
    memcpy(result.effects, prediction->result.effects, result.effect_count * sizeof *result.effects);
  } else result.effects = NULL;
  return result;
}
