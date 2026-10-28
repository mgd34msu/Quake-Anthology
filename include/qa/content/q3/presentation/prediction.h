/* Port of src/content/q3/presentation/prediction.ts. GPL-2.0-or-later.
 * Client prediction from id Software's code/cgame/cg_predict.c.
 * TS SourcePlayerState / UserCommand are the landed base/shared records
 * (qa/content/q3/base/shared/player_state.h), as is the embedded
 * PresentationMovementHost (movement_host.h). The TS async
 * transitionPlayerState is a synchronous host call. */
#ifndef QA_CONTENT_Q3_PRESENTATION_PREDICTION_H
#define QA_CONTENT_Q3_PRESENTATION_PREDICTION_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/collision_host.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
/* TS CommandSource. read false is TS null (out untouched). */
typedef struct qa_q3_pres_command_source {
  void *ctx;
  double (*current_number)(void *ctx);
  bool (*read)(void *ctx, double number, qa_q3_base_user_command *out);
} qa_q3_pres_command_source;
/* TS ClientCommandHistory: CMD_BACKUP ring of 64 owned commands. */
typedef struct qa_q3_pres_client_command_history {
  qa_q3_base_user_command commands[64];
  double number;
} qa_q3_pres_client_command_history;
void qa_q3_pres_client_command_history_init(qa_q3_pres_client_command_history *history);
double qa_q3_pres_client_command_history_current_number(const qa_q3_pres_client_command_history *history);
double qa_q3_pres_client_command_history_append(qa_q3_pres_client_command_history *history,
    qa_q3_base_user_command command);
/* False is TS null; bad numbers abort with the exact TS error text. */
bool qa_q3_pres_client_command_history_read(qa_q3_pres_client_command_history *history, double number,
    qa_q3_base_user_command *out);
qa_q3_pres_command_source qa_q3_pres_client_command_history_source(qa_q3_pres_client_command_history *history);
typedef struct qa_q3_pres_prediction_settings {
  qa_q3_base_game_type game_type;
  double dm_flags;
  bool demo_playback, no_predict, synchronous_clients, predict_items, pmove_fixed;
  double pmove_msec, error_decay_integer, error_decay_value, show_miss;
} qa_q3_pres_prediction_settings;
/* TS predictItem source closure. */
typedef void (*qa_q3_pres_predict_item_source_fn)(void *ctx);
typedef struct qa_q3_pres_prediction_host {
  qa_q3_pres_movement_host movement; /* TS extends PresentationMovementHost */
  void *ctx;
  void (*predict_item)(void *ctx, qa_q3_pres_client_entity *entity,
                       qa_q3_pres_predict_item_source_fn source, void *source_ctx);
  qa_q3_base_predictable_event_debug *event_debug; /* borrowed, nullable (TS optional) */
  qa_q3_pres_command_source commands;
  qa_q3_pres_prediction_settings (*settings)(void *ctx);
  void (*set_pmove_msec)(void *ctx, double value);
  void (*transition_player_state)(void *ctx, qa_q3_base_source_player_state *current,
                                  const qa_q3_base_source_player_state *previous);
  void (*warn)(void *ctx, const char *message);
} qa_q3_pres_prediction_host;
typedef struct qa_q3_pres_prediction_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_collision_world collision;
  qa_q3_pres_prediction_host host;
  qa_q3_base_user_command command; /* TS private command */
} qa_q3_pres_prediction_runtime;
void qa_q3_pres_prediction_runtime_init(qa_q3_pres_prediction_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_collision_world collision, qa_q3_pres_prediction_host host);
qa_q3_pres_movement_trace qa_q3_pres_prediction_runtime_trace(qa_q3_pres_prediction_runtime *runtime,
    qa_vec3 start, qa_vec3 end, qa_bounds bounds, double skip_number, double mask);
double qa_q3_pres_prediction_runtime_point_contents(qa_q3_pres_prediction_runtime *runtime,
    qa_vec3 point, double pass_entity);
/* False only on OOM copying the snapshot player state. */
bool qa_q3_pres_prediction_runtime_interpolate_player_state(qa_q3_pres_prediction_runtime *runtime,
    bool grab_angles);
void qa_q3_pres_prediction_runtime_touch_item(qa_q3_pres_prediction_runtime *runtime,
    qa_q3_pres_client_entity *entity, qa_q3_pres_prediction_settings settings);
void qa_q3_pres_prediction_runtime_touch_trigger_prediction(qa_q3_pres_prediction_runtime *runtime,
    qa_bounds bounds, qa_q3_pres_prediction_settings settings);
/* TS async predictPlayerState, synchronous here. False only on OOM. */
bool qa_q3_pres_prediction_runtime_predict_player_state(qa_q3_pres_prediction_runtime *runtime);
/* TS buildSolidList. False only on OOM growing the entity lists. */
bool qa_q3_pres_build_solid_list(qa_q3_pres_client_game_state *state);
#endif
