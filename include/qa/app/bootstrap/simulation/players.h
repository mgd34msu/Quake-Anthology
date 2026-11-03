/* Port of src/app/bootstrap/simulation/players.ts. Optional TS host
 * members are NULL-able callbacks. ModClientApplications slicing is
 * carried through the host begin/finish callbacks. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PLAYERS_H
#define QA_APP_BOOTSTRAP_SIMULATION_PLAYERS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/quakec_client_adapter.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/movement/q2/rerelease.h"
#include "qa/movement/q3/provider.h"
#include "qa/movement/q3/types.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"

typedef struct qa_app_sim_netquake_client_binding {
  void *ctx;
  bool has_projection;
  qa_q1_movement_state (*projection_read)(void *ctx, qa_q1_movement_state state);
  void (*projection_write)(void *ctx, qa_q1_movement_state state);
  const char *jump_authority; /* "selected-movement"|"source-gamecode" */
  void (*input)(void *ctx, qa_q1_user_command command);
  void (*before_physics)(void *ctx, qa_frame_context frame);
  void (*think)(void *ctx, qa_frame_context frame);
  void (*after_physics)(void *ctx, qa_frame_context frame);
} qa_app_sim_netquake_client_binding;

/* Which TS literal built a ClientMovementOptions, i.e. its key order
 * (encodeCheckpointValue keeps it): ClientThink's moveClient options,
 * the spectator moveClient options (client-policy.ts), or the
 * nativeQ3RuntimeReader record of a restored save. */
typedef enum qa_app_sim_client_movement_order {
  QA_APP_SIM_MOVEMENT_ORDER_THINK, QA_APP_SIM_MOVEMENT_ORDER_SPECTATOR, QA_APP_SIM_MOVEMENT_ORDER_SAVED
} qa_app_sim_client_movement_order;
/* TS ClientMovementOptions view (q3/types sibling slice). */
typedef struct qa_app_sim_client_movement_options {
  bool has_trace_mask; double trace_mask;
  bool has_fixed_msec; double fixed_msec;
  bool no_footsteps;
  bool gauntlet_hit; /* TS ClientMovementOptions.gauntletHit */
  double debug_level; /* TS debugLevel */
  bool ladders;       /* TS ladders?: written by ClientThink (order THINK); undefined for spectator and saved options */
  qa_app_sim_client_movement_order order;
} qa_app_sim_client_movement_options;
typedef struct qa_app_sim_q2_movement_config { double air_accelerate; bool n64_physics; } qa_app_sim_q2_movement_config;

typedef struct qa_app_sim_movement_player qa_app_sim_movement_player;
typedef struct qa_app_sim_player_movement_host {
  void *ctx;
  /* Optional (NULL = TS undefined). */
  qa_mod_client_movement_outputs (*client_outputs)(void *ctx);
  bool input_applications_active;
  bool (*input_begin)(void *ctx, qa_app_sim_movement_player *player, const char *scope, qa_user_command command,
      qa_frame_context frame, qa_movement_state state, const qa_arsenal_intent *arsenal,
      qa_user_command *out_command, const qa_arsenal_intent **out_arsenal);
  void (*input_finish)(void *ctx, bool failed);
  bool has_source_client;
  qa_movement_state (*project_state)(void *ctx, qa_movement_state state);
  qa_app_sim_quakec_client_movement (*before_movement)(void *ctx, qa_actor_command command, qa_frame_context frame, const qa_qw_user_command *source_command);
  void (*after_movement)(void *ctx, qa_app_sim_movement_player *player, qa_frame_context frame, const qa_qw_user_command *source_command);
  bool (*q2_movement_config)(void *ctx, qa_app_sim_q2_movement_config *out);
  const qa_app_sim_netquake_client_binding *netquake; /* NULL absent */
  bool has_quakeworld;
  qa_qw_movement_state (*qw_read)(void *ctx, qa_qw_movement_state state);
  void (*qw_write)(void *ctx, qa_qw_movement_state state);
  void (*qw_before_physics)(void *ctx, qa_qw_user_command command, qa_frame_context frame);
  void (*qw_water)(void *ctx, double level, double type);
  qa_qw_movement_profile (*qw_profile)(void *ctx, qa_qw_movement_profile profile);
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  qa_scene_queries scene;
  qa_q2_rerelease_movement_context *rerelease_movement;
  qa_movement_services services; /* weapon/animation/touch members used */
  qa_q3_movement_hooks q3_hooks;
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  bool (*is_brush)(void *ctx, qa_actor_id actor);
  bool (*world_actor)(void *ctx, qa_actor_id *out);
  double (*speed_multiplier)(void *ctx, qa_owned_actor actor); /* NULL -> 1 */
  const qa_fixed_movement_pose *(*fixed_pose)(void *ctx, qa_owned_actor actor); /* NULL fn/ret */
  bool (*source_punch)(void *ctx, qa_actor_id actor, qa_vec3 *out);
  bool (*gibbed)(void *ctx);
  void (*jump)(void *ctx, qa_owned_actor actor, const char *action);
} qa_app_sim_player_movement_host;

qa_game_family qa_app_sim_provider_family(const char *provider);
qa_provider_timing qa_app_sim_provider_timing(qa_executable_recipe recipe, const char *provider);
qa_movement_profile qa_app_sim_movement_profile(qa_executable_recipe recipe);
qa_vec3 qa_app_sim_movement_origin(qa_movement_state state);
qa_vec3 qa_app_sim_movement_velocity(qa_movement_state state);

/* TS moveCommand q3 weapon-hook wrapper state: records motion
 * viewangles on the player, then delegates to host.q3_hooks. */
typedef struct qa_app_sim_q3_weapon_wrap {
  qa_app_sim_movement_player *player;
  qa_q3_movement_hooks inner;
} qa_app_sim_q3_weapon_wrap;

struct qa_app_sim_movement_player {
  qa_owned_actor actor; qa_client_id client; qa_executable_recipe recipe;
  qa_game_family character;
  qa_provider_reference character_definition, character_appearance;
  qa_bounds standing_bounds;
  qa_movement_profile profile;
  qa_movement_services services;
  qa_movement_state state;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_vec3 view_angles, command_angles;
  double view_height;
  qa_bounds bounds;
  qa_trace_hit ground;
  double water_level, water_type;
  bool intermission;
  bool has_cutscene; qa_vec3 cutscene_origin, cutscene_angles, cutscene_view_offset;
  bool fixed_pose_active;
  bool has_body_shape_base; qa_bounds body_shape_base;
  double gravity_multiplier;
  bool flight;
  double world_gravity;
  double buttons;
  /* Vertical move axis of the latest command; Q2 spectators read it as a chase-cycle jump press (TS upMove = 0). */
  double up_move;
  double previous_buttons, last_sequence;
  bool has_netquake_command; qa_q1_user_command netquake_command;
  double last_weapon_seconds;
  bool has_arsenal_intent; qa_arsenal_intent arsenal_intent;
  bool has_source_movement; qa_app_sim_client_movement_options source_movement;
  bool has_source_environment; qa_movement_environment source_environment;
  /* private (C-visible) */
  qa_app_sim_player_movement_host host;
  bool netquake_application_open, netquake_slice_open;
  bool has_netquake_slice_command; qa_q1_user_command netquake_slice_command;
  qa_q3_movement_provider_options q3_provider_options;
  qa_app_sim_q3_weapon_wrap q3_weapon_wrap;
  /* The contact and effect arrays of the last movement result this player
   * returned (TS garbage once the next move begins): freed when the next
   * move starts and when the player is destroyed. */
  const qa_movement_contact *owned_contacts;
  const qa_ordered_movement_effect *owned_effects;
  /* The effects of the last weapon step (qa_app_shared_sim_weapon_step). */
  const qa_movement_effect *owned_weapon_effects;
  /* The ammo array of the last Q3 arsenal step (stepQ3Arsenal's fresh state, GC-owned in TS); freed by the next step or destroy. */
  const void *owned_arsenal_ammo;
};
/* character_definition/appearance NULL use recipe.character. */
qa_app_sim_movement_player *qa_app_sim_movement_player_create(qa_owned_actor actor, qa_client_id client,
    qa_executable_recipe recipe, qa_app_sim_player_movement_host host, qa_vec3 origin, qa_vec3 angles,
    qa_arsenal_state arsenal, const qa_provider_reference *character_definition, const qa_provider_reference *character_appearance);
void qa_app_sim_movement_player_destroy(qa_app_sim_movement_player *player);
qa_movement_profile qa_app_sim_movement_player_prediction_profile(qa_app_sim_movement_player *player);
double qa_app_sim_movement_player_speed_multiplier(qa_app_sim_movement_player *player);
qa_movement_environment qa_app_sim_movement_player_prediction_environment(qa_app_sim_movement_player *player);
bool qa_app_sim_movement_player_q2_config(qa_app_sim_movement_player *player, qa_app_sim_q2_movement_config *out);
bool qa_app_sim_movement_player_set_flight(qa_app_sim_movement_player *player, bool enabled);
void qa_app_sim_movement_player_set_source_view_roll(qa_app_sim_movement_player *player, double roll);
qa_app_sim_player_view qa_app_sim_movement_player_view(qa_app_sim_movement_player *player);
qa_movement_state qa_app_sim_movement_player_read_state(qa_app_sim_movement_player *player);
qa_movement_continuation qa_app_sim_movement_player_commit(qa_app_sim_movement_player *player, qa_movement_state state, bool link, bool triggers);
void qa_app_sim_movement_player_receive_netquake(qa_app_sim_movement_player *player, qa_actor_command input);
/* prepare may be NULL. */
void qa_app_sim_movement_player_prepare_netquake(qa_app_sim_movement_player *player, qa_frame_context frame,
    const qa_arsenal_intent *(*prepare)(void *ctx, qa_q1_user_command command, const qa_arsenal_intent *arsenal, bool *out_has), void *prepare_ctx);
void qa_app_sim_movement_player_finish_netquake_input(qa_app_sim_movement_player *player, bool failed);
qa_movement_result qa_app_sim_movement_player_physics_netquake(qa_app_sim_movement_player *player, qa_frame_context frame);
qa_movement_result qa_app_sim_movement_player_move(qa_app_sim_movement_player *player, qa_actor_command input, qa_frame_context frame, const qa_qw_user_command *source_command);
/* TS withInputCommand(command, frame, operation, () => undefined, arsenal): arsenal NULL is the TS default
 * (this.arsenalIntent). The arsenal operation receives is valid for the call. */
void qa_app_sim_movement_player_with_input_command(qa_app_sim_movement_player *player, qa_user_command command, qa_frame_context frame,
    void (*operation)(void *ctx, qa_user_command command, const qa_arsenal_intent *arsenal), void *operation_ctx,
    const qa_arsenal_intent *arsenal);
qa_movement_result qa_app_sim_movement_player_move_command(qa_app_sim_movement_player *player, qa_actor_command input, qa_frame_context frame, const qa_qw_user_command *source_command);
#endif
