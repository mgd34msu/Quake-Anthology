/* Port of src/app/bootstrap/simulation/player-movement.ts.
 * createPlayerMovementPrediction's SharedSimulation collaborators are
 * the callback view below (runtime.ts provides them). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PLAYER_MOVEMENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_PLAYER_MOVEMENT_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/movement/q1/types.h"
#include "qa/movement/q3/types.h"
typedef struct qa_app_sim_locomotion_player {
  qa_movement_profile profile;
  qa_bounds standing_bounds;
  bool has_source_movement; qa_app_sim_client_movement_options source_movement;
  qa_game_family character;
  double world_gravity;
  bool has_q2_movement_config; qa_app_sim_q2_movement_config q2_movement_config;
  bool flight;
} qa_app_sim_locomotion_player;
qa_app_sim_locomotion_player qa_app_sim_capture_player_locomotion(qa_app_sim_locomotion_player player);
bool qa_app_sim_player_locomotion_matches(qa_app_sim_locomotion_player player, qa_app_sim_locomotion_player previous);
qa_trace_policy qa_app_sim_player_trace_policy(qa_app_sim_locomotion_player player);
qa_bounds qa_app_sim_player_standing_bounds(qa_game_family character);
qa_q3_postures qa_app_sim_player_postures(qa_game_family character, qa_bounds standing_bounds);
qa_app_sim_locomotion_player qa_app_sim_locomotion_template(qa_executable_recipe recipe);
qa_bounds qa_app_sim_player_crouched_bounds(qa_app_sim_locomotion_player player);
typedef struct qa_app_sim_selected_profile_player {
  qa_movement_profile profile; double world_gravity;
  bool has_source_movement; qa_app_sim_client_movement_options source_movement;
  bool has_q2_movement_config; qa_app_sim_q2_movement_config q2_movement_config;
} qa_app_sim_selected_profile_player;
/* profile NULL uses player.profile. */
qa_movement_profile qa_app_sim_selected_movement_profile(qa_app_sim_selected_profile_player player, const qa_movement_profile *profile);
typedef struct qa_app_sim_movement_environment_player {
  bool has_source_environment; qa_movement_environment source_environment;
  double gravity_multiplier; qa_movement_state state; bool flight; double movement_speed_multiplier;
} qa_app_sim_movement_environment_player;
qa_movement_environment qa_app_sim_player_movement_environment(qa_app_sim_movement_environment_player player, qa_combat_state combat);
typedef struct qa_app_sim_player_movement_hooks {
  bool native_quakeworld;
  void *publish_ctx; void (*publish_posture)(void *ctx, qa_bounds bounds, double view_height);
  qa_q1_movement_options q1;
  qa_q2_rerelease_movement_context *q2;
  qa_q3_movement_hooks q3;
  qa_q3_movement_provider_options *q3_options_storage; /* caller-owned, q3 only */
} qa_app_sim_player_movement_hooks;
typedef struct qa_app_sim_provider_player {
  qa_movement_profile profile; qa_game_family character; qa_bounds standing_bounds; double view_height;
  bool has_source_movement; qa_app_sim_client_movement_options source_movement;
} qa_app_sim_provider_player;
qa_movement_provider qa_app_sim_create_player_movement_provider(qa_app_sim_provider_player player, qa_app_sim_player_movement_hooks hooks);

typedef struct qa_app_sim_player_movement_prediction qa_app_sim_player_movement_prediction;
typedef struct qa_app_sim_prediction_simulation {
  void *ctx;
  /* Q3 source entity view: false when no q3 source/entity. */
  bool (*q3_baseline)(void *ctx, qa_client_id client, qa_movement_state *out_state, double *out_weapon, bool *has_weapon);
  bool (*q3_arsenal_runtime)(void *ctx, qa_client_id client, const char **out_product, qa_q3_arsenal_runtime_state *out);
  qa_combat_state_result (*combat_read)(void *ctx, qa_actor_id actor);
  qa_scene_queries scene;
  bool native_quakeworld; /* simulation.quakecSource()?.kind === "quakeworld" */
  double time_seconds;
} qa_app_sim_prediction_simulation;
typedef struct qa_app_sim_movement_prediction_player {
  qa_client_id client; qa_owned_actor actor;
  qa_movement_profile profile; qa_bounds standing_bounds, bounds;
  bool has_source_movement; qa_app_sim_client_movement_options source_movement;
  qa_game_family character; double world_gravity;
  bool has_q2_movement_config; qa_app_sim_q2_movement_config q2_movement_config;
  bool flight;
  bool has_source_environment; qa_movement_environment source_environment;
  double gravity_multiplier; double movement_speed_multiplier;
  qa_movement_state state; qa_arsenal_state arsenal; qa_actor_animation_state animation; double view_height;
  qa_numeric_operations numeric;
  bool has_q3_arsenal; qa_q3_arsenal_runtime_state q3_arsenal;
} qa_app_sim_movement_prediction_player;
/* selected_profile NULL uses selectedMovementProfile(player). */
qa_app_sim_player_movement_prediction *qa_app_sim_create_player_movement_prediction(
    qa_app_sim_prediction_simulation simulation, qa_app_sim_movement_prediction_player player,
    qa_vec3 origin, qa_vec3 velocity, double milliseconds, bool crouched, const qa_movement_profile *selected_profile);
void qa_app_sim_player_movement_prediction_destroy(qa_app_sim_player_movement_prediction *prediction);
qa_movement_provider qa_app_sim_player_movement_prediction_provider(qa_app_sim_player_movement_prediction *prediction);
qa_movement_services qa_app_sim_player_movement_prediction_services(qa_app_sim_player_movement_prediction *prediction);
/* previous may be NULL. */
qa_movement_input qa_app_sim_player_movement_prediction_input(qa_app_sim_player_movement_prediction *prediction,
    const qa_movement_result *previous, double index, qa_vec3 command_move);
typedef struct qa_app_sim_movement_observation { qa_vec3 origin; bool grounded; const char *medium; bool damaging_fall; } qa_app_sim_movement_observation;
qa_app_sim_movement_observation qa_app_sim_movement_observation_of(qa_movement_result result);
#endif
