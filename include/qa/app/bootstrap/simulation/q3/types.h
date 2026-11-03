/* Port of src/app/bootstrap/simulation/q3/types.ts (types only).
 * Q3SourceHost is fn+ctx over the borrowed session owners (typed); serverState
 * is this slice's qa_app_q3_server_state.
 * ClientMovementOptions/Result re-export the team_arena movement host types.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_TYPES_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_TYPES_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_q3_game_entity qa_q3_game_entity; /* landed content/q3/base/game/state.h */
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/base/game/mover.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/content/q3/team_arena/client_events.h"
#include "qa/content/q3/team_arena/movement_host.h"
#include "qa/contracts/original_pickups.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/armor.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/spatial/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"

#include "qa/contracts/weapon_behavior.h"
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

/* TS Q3SourceBots = ClientBotServices & ({ kind: "unavailable"; reason } |
 * { kind: "available"; testAas; interbreedEndMatch; consoleCommand }).
 * client is the ClientBotServices part; its kind/reason mirror kind/reason. */
typedef struct qa_app_q3_source_bots {
  void *ctx;
  const char *kind; /* "unavailable"|"available" */
  const char *reason;
  void (*test_aas)(void *ctx, qa_vec3 origin);
  void (*interbreed_end_match)(void *ctx);
  void (*console_command)(void *ctx, const char *const *argv, size_t argc);
  qa_q3_ta_client_bot_services client;
} qa_app_q3_source_bots;

typedef struct qa_app_q3_server_state qa_app_q3_server_state;

/* TS Q3SourceHost (extends ClientMovementHost). Every function member takes
 * ctx, except entity_event and source_command, which take their own context
 * (createQ3SourceHost replaces exactly those two). Optional TS members are NULL
 * when undefined. Owners are borrowed and outlive the source runtime. */
typedef struct qa_app_q3_source_host {
  void *ctx;
  /* ClientMovementHost.moveClient. */
  qa_q3_ta_client_movement_result (*move_client)(void *ctx, qa_q3_game_entity *entity,
      qa_q3_base_user_command command, qa_q3_ta_client_movement_options options);
  void (*ammo_timer_stored)(void *ctx, qa_actor_id actor, double weapon, double value);
  /* timerOwnership?(actor): fills out and returns true. */
  bool (*timer_ownership)(void *ctx, qa_actor_id actor, qa_q3_ta_client_timer_ownership *out);
  double (*speed_multiplier)(void *ctx, qa_actor_id actor);
  qa_app_q3_server_state *server_state;
  /* Pick<MoverActorAccess, "observe"|"write"|"link"|"release">: native and
   * participant stay NULL (the runtime supplies them). */
  qa_q3_mover_actor_access mover_actors;
  bool (*primary_attack_allowed)(void *ctx, qa_actor_id actor);
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category); /* "weapons"|"ammo" */
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t argc);
  qa_q3_source_pickup_preview (*preview_pickup)(void *ctx, qa_q3_source_pickup_descriptor item);
  qa_q3_source_pickup_admission (*admit_pickup)(void *ctx, qa_q3_source_pickup_descriptor item);
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  const qa_original_pickup_admission *original_pickups; /* NULL = undefined */
  qa_shared_scene_queries *scene;
  qa_app_q3_source_engine engine;
  qa_cvar_registry *cvars;
  void *configstrings_ctx;
  const char *(*configstring_get)(void *ctx, double index);
  void (*configstring_set)(void *ctx, double index, const char *value);
  qa_q3_death_animation_sequence *death_animations;
  qa_app_q3_source_bots bots;
  double (*now)(void *ctx);
  void (*schedule)(void *ctx, qa_owned_actor actor, bool has_due, double due_milliseconds);
  void (*run_think)(void *ctx, qa_owned_actor actor, double time_milliseconds);
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  qa_victim_armor_context (*armor_context)(void *ctx, qa_damage_request request);
  qa_q3_game_entity *(*foreign)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  bool (*is_player)(void *ctx, qa_actor_id actor);
  void *source_command_ctx;
  qa_q3_base_user_command (*source_command)(void *ctx, qa_actor_command input);
  void (*spawn_player)(void *ctx, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose);
  void (*select_spawn_weapon)(void *ctx, qa_q3_game_entity *entity);
  void *entity_event_ctx;
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
  const qa_weapon_behavior_projectile_port *weapon_behavior; /* NULL = undefined */
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
