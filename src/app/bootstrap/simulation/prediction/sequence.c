/* Port of src/app/bootstrap/simulation/prediction/sequence.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/sequence.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/prediction/step.h"
#include "qa/foundation/support.h"
static qa_vec3 origin_of(qa_app_movement_prediction_snapshot player) {
  if (player.state.kind == QA_MOVEMENT_STATE_Q2) {
    qa_vec3 origin = {player.state.as.q2.origin_eighths.values[0] / 8, player.state.as.q2.origin_eighths.values[1] / 8,
                      player.state.as.q2.origin_eighths.values[2] / 8};
    return origin;
  }
  switch (player.state.kind) {
    case QA_MOVEMENT_STATE_Q1: return player.state.as.q1.origin;
    case QA_MOVEMENT_STATE_QW: return player.state.as.qw.origin;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return player.state.as.q2_rerelease.origin;
    case QA_MOVEMENT_STATE_Q3: return player.state.as.q3.origin;
    default: return player.state.as.q2_rerelease.origin;
  }
}
qa_app_prediction_sequence_result qa_app_predict_movement_sequence(const qa_app_movement_probe_options *options,
    qa_app_movement_prediction_snapshot initial, const qa_app_prediction_command *commands, size_t command_count,
    const qa_trace_shape *shape) {
  qa_app_prediction_sequence_result out; memset(&out, 0, sizeof out);
  qa_trace_shape default_shape; memset(&default_shape, 0, sizeof default_shape);
  default_shape.kind = "box"; default_shape.bounds = options->standing_bounds;
  if (!shape) shape = &default_shape;
  out.player = qa_app_copy_prediction_snapshot(initial);
  out.trajectory = malloc((command_count + 1) * sizeof *out.trajectory);
  if (!out.trajectory) qa_range_error("Out of memory");
  out.trajectory[out.trajectory_count++] = origin_of(out.player);
  qa_q2_rerelease_movement_context rerelease; memset(&rerelease, 0, sizeof rerelease);
  bool first = true;
  for (size_t i = 0; i < command_count; i++) {
    qa_app_prediction_step_options step; memset(&step, 0, sizeof step);
    step.scene = options->scene; step.rerelease_movement = &rerelease;
    if (options->profile.kind && strcmp(options->profile.kind, "q3") == 0) {
      step.has_fixed_milliseconds = options->profile.has_fixed_milliseconds;
      step.fixed_milliseconds = options->profile.fixed_milliseconds;
      step.no_footsteps = options->profile.no_footsteps;
    }
    /* ladders: profile.kind === "q3" && profile.ladders !== false */
    step.has_ladders = true;
    step.ladders = options->profile.kind && strcmp(options->profile.kind, "q3") == 0 && !options->profile.ladders_disabled;
    step.first_command = first;
    qa_app_prediction_step_result stepped = qa_app_predict_movement_command(options, out.player, commands[i], step, shape);
    qa_app_movement_prediction_snapshot_destroy(&out.player);
    out.player = stepped.player;
    if (stepped.result.effect_count) {
      qa_ordered_movement_effect *effects = realloc(out.effects, (out.effect_count + stepped.result.effect_count) * sizeof *effects);
      if (!effects) qa_range_error("Out of memory");
      out.effects = effects;
      memcpy(out.effects + out.effect_count, stepped.result.effects, stepped.result.effect_count * sizeof *effects);
      out.effect_count += stepped.result.effect_count;
    }
    /* The step's movement result owns its effects and contacts; replay keeps only the copied effects. */
    free((void *)stepped.result.effects); free((void *)stepped.result.contacts);
    out.trajectory[out.trajectory_count++] = origin_of(out.player);
    first = false;
  }
  out.seconds = (out.player.command_time_milliseconds - initial.command_time_milliseconds) / 1000;
  return out;
}
