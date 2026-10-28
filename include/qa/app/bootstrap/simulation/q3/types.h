/* Port of src/app/bootstrap/simulation/q3/types.ts (types only).
 * ClientMovementOptions/Result re-export landed team_arena movement
 * host types. Q3SourceHost is fn+ctx over borrowed session owners
 * (opaque handles); serverState is this slice's qa_app_q3_server_state.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_TYPES_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_TYPES_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_q3_game_entity qa_q3_game_entity; /* landed content/q3/base/game/state.h */
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
/* Landed content/q3/team_arena/movement_host.h pulls game/state.h whose
 * product enum collides with network/q3 product.h in one TU; forward
 * typedefs keep both usable without editing landed headers. */
typedef struct qa_q3_ta_client_movement_options qa_q3_ta_client_movement_options;
typedef struct qa_q3_ta_client_movement_result qa_q3_ta_client_movement_result;
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"

typedef qa_q3_ta_client_movement_options qa_app_client_movement_options;
typedef qa_q3_ta_client_movement_result qa_app_client_movement_result;

typedef struct qa_app_q3_source_entity_event {
  const char *kind; /* "entity-event" */
  qa_actor_id actor;
  qa_q3_entity_state_record state;
  qa_vec3 origin;
  double time;
} qa_app_q3_source_entity_event;

typedef struct qa_app_q3_source_player_event {
  const char *kind; /* "player-event" */
  qa_actor_id actor;
  qa_module_identity source_module;
  qa_qvm_abi_profile source_abi_profile;
  qa_q3_player_state player_state;
  double event, parameter;
  bool sequence_is_predictable;
  double sequence_time, sequence_number;
  qa_vec3 origin;
  double time;
} qa_app_q3_source_player_event;

typedef struct qa_app_q3_source_engine {
  void *ctx;
  void (*print)(void *ctx, const char *text);
  void (*log)(void *ctx, const char *text);
  void (*send_server_command)(void *ctx, double client, const char *text);
  void (*drop_client)(void *ctx, double client, const char *reason);
  char *(*get_userinfo)(void *ctx, double client); /* malloc'd */
  void (*set_userinfo)(void *ctx, double client, const char *value);
  qa_q3_base_user_command (*get_user_command)(void *ctx, double client);
  void (*append_console_command)(void *ctx, const char *text);
  void (*execute_console_now)(void *ctx, const char *text);
} qa_app_q3_source_engine;

typedef struct qa_app_q3_source_bots {
  void *ctx;
  const char *kind; /* "unavailable"|"available" */
  const char *reason;
  void (*test_aas)(void *ctx, qa_vec3 origin);
  void (*interbreed_end_match)(void *ctx);
  void (*console_command)(void *ctx, const char *const *argv, size_t argc);
  /* ClientBotServices surface is host-owned beyond these seams. */
  void *services;
} qa_app_q3_source_bots;

typedef struct qa_app_q3_server_state qa_app_q3_server_state;

typedef struct qa_app_q3_source_host {
  void *ctx;
  /* ClientMovementHost base (landed movement_host shape, flattened). */
  void *movement_ctx;
  qa_q3_ta_client_movement_result (*move_client)(void *ctx, qa_q3_game_entity *entity,
      qa_q3_base_user_command command, qa_q3_ta_client_movement_options options);
  void (*ammo_timer_stored)(void *ctx, qa_actor_id actor, double weapon, double value); /* NULL absent */
  bool (*timer_ownership)(void *ctx, qa_actor_id actor, void *out_ownership);
  double (*speed_multiplier)(void *ctx, qa_actor_id actor);
  qa_app_q3_server_state *server_state; /* borrowed */
  void *mover_actors; /* borrowed MoverActorAccess handle */
  bool (*primary_attack_allowed)(void *ctx, qa_actor_id actor);
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t argc);
  bool (*preview_pickup)(void *ctx, const void *descriptor, void *out_preview);
  bool (*admit_pickup)(void *ctx, const void *descriptor, void *out_admission);
  void *actors, *bodies, *callbacks, *combat, *inventory, *scene; /* borrowed owners */
  const void *original_pickups; /* nullable */
  qa_app_q3_source_engine engine;
  qa_cvar_registry *cvars; /* borrowed */
  void *configstrings_ctx;
  const char *(*configstring_get)(void *ctx, double index);
  void (*configstring_set)(void *ctx, double index, const char *value);
  void *death_animations;
  qa_app_q3_source_bots bots;
  double (*now)(void *ctx);
  void (*schedule)(void *ctx, qa_owned_actor actor, bool has_due, double due_milliseconds);
  void (*run_think)(void *ctx, qa_owned_actor actor, double time_milliseconds);
  void (*collision)(void *ctx, qa_owned_actor actor, const void *collision);
  void (*armor_context)(void *ctx, qa_damage_request request, void *out_context);
  qa_q3_game_entity *(*foreign)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  bool (*is_player)(void *ctx, qa_actor_id actor);
  qa_q3_base_user_command (*source_command)(void *ctx, qa_actor_command input);
  void (*spawn_player)(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin, qa_vec3 angles);
  void (*entity_event)(void *ctx, qa_app_q3_source_entity_event event);
} qa_app_q3_source_host;

typedef struct qa_app_q3_source_session_carry_client {
  double slot;
  const char *session, *userinfo;
} qa_app_q3_source_session_carry_client;
typedef struct qa_app_q3_source_session_carry {
  const char *kind; /* "q3" */
  const char *world;
  const qa_app_q3_source_session_carry_client *clients;
  size_t client_count;
} qa_app_q3_source_session_carry;

typedef struct qa_app_q3_source_options {
  const void *weapon_behavior; /* nullable */
  qa_executable_recipe recipe;
  qa_provider_reference weapon_provider;
  const char *product; /* "baseq3"|"missionpack" */
  const char *entities;
  bool foreign_world;
  double seed, max_clients;
  const char *build_date;
  const qa_app_q3_source_session_carry *session_carry; /* nullable */
} qa_app_q3_source_options;
#endif
