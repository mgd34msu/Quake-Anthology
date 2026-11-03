/* Port of src/app/bootstrap/simulation/prediction/presentation.ts.
 * Mirrors (sibling slices): relativeQ3SourceCommand (q3-commands.ts),
 * publishQ3CharacterMovementEvent (player-jump.ts) and SharedSimulation
 * (simulation/runtime.ts) are host hooks below; NULL relative hook is
 * identity, NULL publish hook publishes nothing. The adapter also
 * fills a landed qa_q3_pres_movement_host. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_PRESENTATION_H
#include "qa/app/bootstrap/simulation/prediction/source_state.h"
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/contracts/session.h"

typedef struct qa_app_presentation_prediction_options {
  qa_app_movement_prediction_options movement;
  qa_app_movement_prediction_snapshot initial;
  qa_app_prediction_source_entities entities;
  void *host_ctx;
  qa_q3_base_user_command (*relative_source_command)(void *ctx, qa_command_source source,
      qa_user_command_kind kind, qa_q3_base_user_command presented, qa_vec3 delta_angles);
  bool (*publish_event)(void *ctx, const char *animation_kind, double event);
} qa_app_presentation_prediction_options;

qa_q3_base_user_command qa_app_presentation_source_command(qa_actor_command input, double milliseconds, double weapon);

typedef struct qa_app_presentation_prediction_adapter qa_app_presentation_prediction_adapter;
qa_app_presentation_prediction_adapter *qa_app_presentation_prediction_adapter_create(
    qa_app_presentation_prediction_options options);
void qa_app_presentation_prediction_adapter_destroy(qa_app_presentation_prediction_adapter *adapter);
const char *qa_app_presentation_prediction_adapter_command_timing(const qa_app_presentation_prediction_adapter *adapter);
void qa_app_presentation_prediction_adapter_capture(qa_app_presentation_prediction_adapter *adapter,
    qa_app_movement_prediction_snapshot snapshot);
qa_q3_base_user_command qa_app_presentation_prediction_adapter_submit(
    qa_app_presentation_prediction_adapter *adapter, qa_actor_command input, double milliseconds);
qa_bounds qa_app_presentation_prediction_adapter_move_player(qa_app_presentation_prediction_adapter *adapter,
    qa_q3_base_player_state_record *ps, qa_q3_base_user_command command, qa_q3_pres_movement_options movement);
void qa_app_presentation_prediction_adapter_update_view_angles(qa_app_presentation_prediction_adapter *adapter,
    qa_q3_base_player_state_record *ps, qa_q3_base_user_command command);
qa_q3_pres_movement_host qa_app_presentation_prediction_adapter_host(qa_app_presentation_prediction_adapter *adapter);
qa_app_presentation_prediction_adapter *qa_app_create_presentation_movement_host(
    qa_app_presentation_prediction_options options);

/* Mirror of SharedSimulation reads used by createSimulationPredictionHost. */
typedef struct qa_app_simulation_movement_player_view {
  qa_owned_actor actor;
  double last_sequence;
  qa_movement_state state;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  bool has_source_environment;
  qa_movement_environment source_environment;
  double gravity_multiplier, world_gravity;
  qa_bounds bounds, standing_bounds;
  qa_vec3 view_angles;
  double view_height;
  qa_trace_hit ground;
  double water_level, water_type;
  qa_movement_profile profile;
  bool character_is_q3;
  bool ground_is_inline_model; /* unused; is_brush resolved by host */
} qa_app_simulation_movement_player_view;
typedef struct qa_app_simulation_prediction_source {
  void *ctx;
  const qa_executable_recipe *recipe; /* borrowed */
  qa_scene_queries scene;
  bool (*movement_player)(void *ctx, qa_actor_id actor, qa_app_simulation_movement_player_view *out);
  bool (*has_q3_source)(void *ctx);
  const qa_q3_base_player_state_record *(*source_client_state)(void *ctx, qa_actor_id actor);
  bool (*actor_at_slot)(void *ctx, double slot, qa_actor_id *out);
  bool (*slot_of_actor)(void *ctx, qa_actor_id actor, double *out);
  bool (*actor_is_inline_model)(void *ctx, qa_actor_id actor);
  void *host_ctx;
  qa_q3_base_user_command (*relative_source_command)(void *ctx, qa_command_source source,
      qa_user_command_kind kind, qa_q3_base_user_command presented, qa_vec3 delta_angles);
  bool (*publish_event)(void *ctx, const char *animation_kind, double event);
  qa_q3_arsenal_controls (*resolve_controls)(void *ctx, qa_arsenal_state arsenal,
      const qa_arsenal_intent *intent, qa_user_command command, const char *product);
} qa_app_simulation_prediction_source;
typedef struct qa_app_simulation_prediction_host qa_app_simulation_prediction_host;
qa_app_simulation_prediction_host *qa_app_create_simulation_prediction_host(
    qa_app_simulation_prediction_source source, qa_actor_id actor, qa_seat_id seat);
void qa_app_simulation_prediction_host_destroy(qa_app_simulation_prediction_host *host);
qa_app_presentation_prediction_adapter *qa_app_simulation_prediction_host_adapter(qa_app_simulation_prediction_host *host);
/* TS captureSource(published): reads the published client row for the host's actor. */
void qa_app_simulation_prediction_host_capture_source(qa_app_simulation_prediction_host *host,
    const qa_app_q3_source_presentation_state *published);
#endif
