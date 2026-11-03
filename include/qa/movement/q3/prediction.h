/*
 * Port of src/movement/q3/prediction.ts (cg_predict.c). GPL-2.0-or-later.
 * C accommodations: Q3CommandSource is fn+ctx; predict is synchronous
 * (TS async only awaits host.transition). Effects/output actor arrays
 * are malloc'd in outputs (caller frees effects).
 */
#ifndef QA_MOVEMENT_Q3_PREDICTION_H
#define QA_MOVEMENT_Q3_PREDICTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/scene.h"

typedef struct qa_q3_command_source {
  void *ctx;
  double (*current_number)(void *ctx);
  bool (*read)(void *ctx, double number, qa_q3_user_command *out); /* false = TS null */
} qa_q3_command_source;

typedef struct qa_q3_command_history {
  qa_q3_user_command commands[64];
  double number;
} qa_q3_command_history;
void qa_q3_command_history_init(qa_q3_command_history *history);
double qa_q3_command_history_current_number(const qa_q3_command_history *history);
double qa_q3_command_history_append(qa_q3_command_history *history, qa_q3_user_command command);
/* RangeError on non-integer; aborts with TS Error text when number > current or slot missing. */
bool qa_q3_command_history_read(const qa_q3_command_history *history, double number, qa_q3_user_command *out);
qa_q3_command_source qa_q3_command_history_source(qa_q3_command_history *history);

typedef struct qa_q3_predicted_actor {
  qa_q3_movement_state movement; qa_arsenal_state arsenal; qa_actor_animation_state animation;
} qa_q3_predicted_actor;
typedef struct qa_q3_prediction_snapshot {
  double server_time_milliseconds; qa_q3_predicted_actor actor;
} qa_q3_prediction_snapshot;
typedef struct qa_q3_prediction_settings {
  bool demo_playback, no_predict, synchronous_clients, fixed;
  double movement_milliseconds, error_decay_integer, error_decay_value, show_miss;
} qa_q3_prediction_settings;
typedef struct qa_q3_prediction_frame {
  double time_milliseconds, previous_time_milliseconds;
  qa_q3_prediction_snapshot snapshot;
  const qa_q3_prediction_snapshot *next_snapshot; /* NULL = TS null */
  bool next_frame_teleport, this_frame_teleport;
  double health;
} qa_q3_prediction_frame;
typedef struct qa_q3_prediction_triggers {
  qa_q3_predicted_actor actor; bool hyperspace;
  const qa_ordered_movement_effect *effects; size_t effect_count;
} qa_q3_prediction_triggers;

typedef struct qa_q3_prediction_host {
  void *ctx;
  qa_q3_command_source commands;
  qa_movement_provider movement; /* kind "q3" */
  qa_q3_prediction_settings (*settings)(void *ctx);
  void (*set_movement_milliseconds)(void *ctx, double value);
  void (*restore_snapshot)(void *ctx, qa_q3_prediction_snapshot snapshot);
  qa_q3_movement_input (*movement_input)(void *ctx, qa_q3_predicted_actor actor,
                                         qa_q3_user_command command, double command_number,
                                         double physics_time_milliseconds);
  qa_movement_services (*movement_services)(void *ctx, double physics_time_milliseconds);
  qa_q3_prediction_triggers (*touch_triggers)(void *ctx, qa_q3_predicted_actor actor,
                                              qa_bounds bounds, double physics_time_milliseconds);
  qa_vec3 (*adjust_for_mover)(void *ctx, qa_vec3 origin, qa_trace_hit ground,
                              double from_milliseconds, double to_milliseconds);
  void (*transition)(void *ctx, qa_q3_predicted_actor current, qa_q3_predicted_actor previous,
                     const qa_ordered_movement_effect *effects, size_t effect_count);
  void (*warn)(void *ctx, const char *message);
} qa_q3_prediction_host;

typedef struct qa_q3_prediction_output {
  const char *status; /* "predicted"|"interpolated"|"unchanged"|"command-backup-exceeded"|"actor-removed" */
  bool has_actor; qa_q3_predicted_actor actor;
  bool hyperspace;
  qa_ordered_movement_effect *effects; size_t effect_count; /* malloc'd, caller frees */
  bool consumed_teleport;
} qa_q3_prediction_output;

typedef struct qa_q3_prediction_runtime {
  qa_q3_prediction_host host;
  bool has_predicted; qa_q3_predicted_actor predicted;
  qa_q3_user_command command;
  qa_vec3 error; double error_time;
} qa_q3_prediction_runtime;
void qa_q3_prediction_runtime_init(qa_q3_prediction_runtime *runtime, qa_q3_prediction_host host);
qa_q3_prediction_output qa_q3_prediction_predict(qa_q3_prediction_runtime *runtime, qa_q3_prediction_frame frame);
/* The pmove_fixed command time: Math.imul(Math.trunc(((serverTime + msec - 1) | 0) / msec), msec). */
double qa_q3_fixed_server_time(double server_time_milliseconds, double movement_milliseconds);
qa_q3_movement_state qa_q3_update_prediction_view(qa_q3_movement_state state, double health, qa_q3_user_command command);
#endif
