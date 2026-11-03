/* Port of src/app/bootstrap/simulation/quakec-source.ts: the original
 * QuakeC game over compat/qc, one source state owner borrowed by the
 * session's actor traversal. GPL-2.0-or-later.
 *
 * TS async prepares become synchronous over qa_content_mounted.
 * Errors TS throws (Error/RangeError) reach the top level through
 * qa_raise (abort when nothing catches), as the QC machine does.
 * Arrays returned by query methods are owned by the source and valid
 * until the next call of the same method or destroy, unless noted. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_QUAKEC_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/events.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/quakec_client_adapter.h"
#include "qa/app/bootstrap/simulation/quakec_local_messages.h"
#include "qa/app/bootstrap/simulation/quakec_player_ui.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/compat/qc/compatibility.h"
#include "qa/compat/qc/presentation_host.h"
#include "qa/compat/qc/program.h"
#include "qa/compat/qc/world_host.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/quakec/compat_types.h"
#include "qa/content/q1/quakec/id1_damage.h"
#include "qa/content/q1/quakec/id1_attacks.h"
#include "qa/content/q1/quakec/id1_environment.h"
#include "qa/content/q1/quakec/id1_projectiles.h"
#include "qa/content/q1/quakec/id1_pickups.h"
#include "qa/content/q1/quakec/pickup_stage.h"
#include "qa/content/q1/quakec/weapon_stage.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"
#include "qa/core/cvars/index.h"
#include "qa/movement/q1/types.h"
#include "qa/network/q1/netquake.h"
#include "qa/persistence/q1.h"
#include "qa/compat/qc/q1_save.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

/* TS qcProgram view the content/q1/quakec bindings read: one view per
 * program, arrays owned here, names borrowed from the program. */
typedef struct qa_app_sim_quakec_program_view {
  qa_q1qc_program program;
  qa_q1qc_definition *globals, *fields;
  qa_q1qc_function *functions;
  qa_q1qc_statement *statements;
} qa_app_sim_quakec_program_view;
bool qa_app_sim_quakec_program_view_init(qa_app_sim_quakec_program_view *view, const qa_compat_qc_program *program);
void qa_app_sim_quakec_program_view_destroy(qa_app_sim_quakec_program_view *view);

/* TS PreparedQuakeCSource["resources"] entry. name owned; resource
 * borrows the resource mounts, which must outlive the prepared source. */
typedef struct qa_app_sim_quakec_resource {
  char *name;
  qa_resolved_resource_reference resource;
  bool has_model_bounds; qa_bounds model_bounds; /* TS modelBounds null = !has */
} qa_app_sim_quakec_resource;
typedef struct qa_app_sim_quakec_resources {
  qa_app_sim_quakec_resource *items; size_t count;
} qa_app_sim_quakec_resources;
void qa_app_sim_quakec_resources_destroy(qa_app_sim_quakec_resources *resources);
const qa_app_sim_quakec_resource *qa_app_sim_quakec_resources_get(const qa_app_sim_quakec_resources *resources, const char *name);

/* TS PreparedQuakeCSource. Optional members: NULL / has_* false = TS
 * absent. kind is id1ProgramBinding(program).kind, kept for the
 * unported runtime.c consumer. */
typedef struct qa_app_sim_prepared_quakec_source {
  qa_execution_module execution;
  qa_compat_qc_program program; /* owned */
  const char *kind; /* "netquake"|"quakeworld" */
  qa_app_sim_quakec_program_view view; /* owned view of program */
  bool has_teams; const qa_source_team_alias *teams; size_t team_count;
  const qa_mod_qc_combat *combat_declaration;
  const char *message_dialect; /* RereleaseMessages */
  const qa_qc_primary_weapon_stage_declaration *weapon_declaration;
  /* TS weaponStage?: QcWeaponStage | null. has false = undefined. */
  bool has_weapon_stage; qa_q1qc_weapon_stage *weapon_stage; /* owned */
  bool has_declared_pickups; qa_q1qc_pickup_stage *declared_pickups; size_t declared_pickup_count; /* owned */
  qa_app_sim_quakec_resources resources;
  /* Owned storage behind teams/combat/message dialect/weapon declaration. */
  bool has_compatibility; qa_compat_qc_quakec_compatibility compatibility;
} qa_app_sim_prepared_quakec_source;

/* TS prepareQuakeCSource: decode the selected artifact and genuine
 * assets before synchronous source precaching. resource_mounts NULL =
 * mounts (TS default). entity_text NULL = "". */
void qa_app_sim_prepare_quakec_source_mounted(qa_execution_module execution, qa_content_mounted *mounts,
    const char *entity_text, qa_content_mounted *resource_mounts, qa_app_sim_prepared_quakec_source *out);
/* TS prepareQuakeCResources. */
qa_app_sim_quakec_resources qa_app_sim_prepare_quakec_resources(const qa_compat_qc_program *program,
    qa_content_mounted *mounts, const char *entity_text);
/* TS preparedQuakeCWeaponStage: the prepared stage, or the program's
 * stage when undefined (*out_owned true: caller destroys it). */
qa_q1qc_weapon_stage *qa_app_sim_prepared_quakec_weapon_stage(const qa_app_sim_prepared_quakec_source *prepared, bool *out_owned);
void qa_app_sim_prepared_quakec_source_destroy(qa_app_sim_prepared_quakec_source *prepared);

/* Retained for the unported runtime.c/application.c composition, which
 * reads only execution/program/kind: reads the artifact through the
 * host, checks the API and id1 binding. It has no mounts, so it reads
 * no compatibility declarations and prepares no resources; the
 * QuakeCSource composition uses qa_app_sim_prepare_quakec_source_mounted. */
typedef struct qa_app_sim_quakec_prepare_host {
  void *ctx;
  bool (*read_artifact)(void *ctx, const qa_resolved_resource_reference *artifact, uint8_t **out, size_t *out_length);
  const void *(*prepare_resources)(void *ctx, qa_compat_qc_program *program, const char *entity_text); /* unused */
} qa_app_sim_quakec_prepare_host;
qa_app_sim_prepared_quakec_source qa_app_sim_prepare_quakec_source(qa_execution_module execution,
    const char *entity_text, qa_app_sim_quakec_prepare_host host);

typedef struct qa_app_sim_quakec_source qa_app_sim_quakec_source;

/* TS QuakeCSourceOptions. Optional callbacks are NULL when absent; all
 * receive ctx. Borrowed collaborators must outlive the source. */
typedef struct qa_app_sim_quakec_source_restore {
  const qa_guest_checkpoint *checkpoint;
  const qa_client_id *clients; size_t client_count;
} qa_app_sim_quakec_source_restore;
typedef struct qa_app_sim_quakec_source_options {
  void *ctx;
  bool original_save_candidate;
  qa_cvar_registry *source_registry; /* NULL = source creates its own */
  const qa_app_sim_quakec_source_restore *restore; /* NULL = fresh */
  const qa_executable_recipe *recipe;
  qa_decoded_world world;
  const char *entity_text; /* NULL = quakeCMapEntities(world, mode) */
  qa_shared_scene_queries *scene;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_app_sim_shared_physics *physics;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_original_pickup_admission pickups;
  bool (*primary_weapon_selected)(void *ctx, qa_actor_id actor);
  void (*bind_inventory)(void *ctx, qa_owned_actor actor, qa_inventory_state_binding binding);
  bool (*give_inventory)(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count);
  void (*client_spawned)(void *ctx, qa_owned_actor actor);
  const char *(*foreign_classname)(void *ctx, qa_actor_id actor);
  bool (*damage_allowed)(void *ctx, qa_damage_request request);
  const qa_q1qc_pickup_policy *(*pickup_policy)(void *ctx); /* NULL return = TS null */
  bool (*owns_weapon)(void *ctx, qa_actor_id actor, qa_item_id item);
  qa_app_sim_simulation_events *events;
  qa_app_sim_source_random *random;
  int skill; /* 0..3 */
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  double max_clients;
  double initial_source_time_seconds;
  void (*admit)(void *ctx, qa_owned_actor actor, int64_t slot, qa_app_sim_quakec_source *source);
  qa_damage_request (*damage_request)(void *ctx, qa_q1_id1_damage_call call);
  void (*print)(void *ctx, const char *text);
  void (*change_level)(void *ctx, const char *map);
} qa_app_sim_quakec_source_options;

/* prepared is borrowed and must outlive the source. */
qa_app_sim_quakec_source *qa_app_sim_quakec_source_create(const qa_app_sim_prepared_quakec_source *prepared,
    qa_app_sim_quakec_source_options options);
void qa_app_sim_quakec_source_destroy(qa_app_sim_quakec_source *source);

/* Fields and getters (TS readonly members). */
qa_compat_qc_machine *qa_app_sim_quakec_source_machine(qa_app_sim_quakec_source *source);
qa_compat_qc_entity_memory *qa_app_sim_quakec_source_entities(qa_app_sim_quakec_source *source);
qa_source_actor_slots *qa_app_sim_quakec_source_slots(qa_app_sim_quakec_source *source);
qa_compat_qc_world_host *qa_app_sim_quakec_source_world_host(qa_app_sim_quakec_source *source);
qa_cvar_registry *qa_app_sim_quakec_source_cvars(qa_app_sim_quakec_source *source);
qa_compat_qc_broadcast_messages *qa_app_sim_quakec_source_messages(qa_app_sim_quakec_source *source);
qa_q1_id1_environment *qa_app_sim_quakec_source_environment(qa_app_sim_quakec_source *source);
const qa_q1_pusher_services *qa_app_sim_quakec_source_pusher_services(qa_app_sim_quakec_source *source);
int64_t qa_app_sim_quakec_source_reserved_client_slots(const qa_app_sim_quakec_source *source);
const qa_app_sim_prepared_quakec_source *qa_app_sim_quakec_source_prepared(const qa_app_sim_quakec_source *source);
/* TS currentPhysicsCallback: false = null. */
bool qa_app_sim_quakec_source_current_physics_callback(const qa_app_sim_quakec_source *source, qa_q1_id1_physics_callback *out);
qa_owned_actor qa_app_sim_quakec_source_world_actor(qa_app_sim_quakec_source *source);
double qa_app_sim_quakec_source_time_seconds(const qa_app_sim_quakec_source *source);
bool qa_app_sim_quakec_source_loading(const qa_app_sim_quakec_source *source);
const char *qa_app_sim_quakec_source_kind(const qa_app_sim_quakec_source *source);
/* TS QuakeCSource.attacks, .projectiles and .machine (the q1qc view of the
 * machine, refreshed on each call; owned by the source). */
qa_q1_id1_sync_attacks *qa_app_sim_quakec_source_attacks(qa_app_sim_quakec_source *source);
qa_q1_id1_projectile_attacks *qa_app_sim_quakec_source_projectiles(qa_app_sim_quakec_source *source);
qa_q1qc_machine *qa_app_sim_quakec_source_q1qc_machine(qa_app_sim_quakec_source *source);
/* Legacy accessors kept for the unported runtime.c consumer. */
qa_compat_qc_program *qa_app_sim_quakec_source_program(qa_app_sim_quakec_source *source);
qa_execution_module qa_app_sim_quakec_source_execution(qa_app_sim_quakec_source *source);

/* Checkpoints. The returned checkpoint borrows source storage valid
 * until the next checkpoint or destroy. */
const qa_guest_checkpoint *qa_app_sim_quakec_source_checkpoint(qa_app_sim_quakec_source *source);
/* format: 5 (game_directories NULL) or 6. Fully owned result. */
bool qa_app_sim_quakec_source_capture_original_save(qa_app_sim_quakec_source *source, int format_version,
    const char *game_directories, const char *comment, qa_q1_save_data *out);
bool qa_app_sim_quakec_source_restore_original_save(qa_app_sim_quakec_source *source, const qa_q1_save_data *save,
    qa_q1_unknown_save_fields *out);

/* Teams, scores, client info. Returned strings are source-owned until
 * the next call. */
const char *qa_app_sim_quakec_source_match_team(qa_app_sim_quakec_source *source, qa_actor_id actor);
void qa_app_sim_quakec_source_set_match_team(qa_app_sim_quakec_source *source, qa_actor_id actor, const char *team);
double qa_app_sim_quakec_source_match_score(qa_app_sim_quakec_source *source, qa_actor_id actor);
void qa_app_sim_quakec_source_set_match_score(qa_app_sim_quakec_source *source, qa_actor_id actor, double score);
const char *qa_app_sim_quakec_source_death_type(qa_app_sim_quakec_source *source, qa_actor_id actor);
/* Ordered key/value info (TS ReadonlyMap<string,string>). */
typedef struct qa_app_sim_quakec_info { const qa_app_sim_cvar_name_value *items; size_t count; } qa_app_sim_quakec_info;
void qa_app_sim_quakec_source_set_client_info(qa_app_sim_quakec_source *source, qa_client_id client, qa_app_sim_quakec_info values);
void qa_app_sim_quakec_source_set_client_info_storage(qa_app_sim_quakec_source *source, qa_client_id client, qa_app_sim_quakec_info values);
qa_app_sim_quakec_info qa_app_sim_quakec_source_client_info(qa_app_sim_quakec_source *source, qa_client_id client);
void qa_app_sim_quakec_source_set_client_role(qa_app_sim_quakec_source *source, qa_client_id client, const char *role);
bool qa_app_sim_quakec_source_is_spectator_client(qa_app_sim_quakec_source *source, qa_actor_id actor);
qa_owned_actor qa_app_sim_quakec_source_reserved_client(qa_app_sim_quakec_source *source, qa_client_id client);
typedef struct qa_app_sim_quakec_client_network {
  void *ctx; const char *(*address)(void *ctx); double (*ping)(void *ctx);
} qa_app_sim_quakec_client_network;
void qa_app_sim_quakec_source_set_client_network(qa_app_sim_quakec_source *source, qa_client_id client, qa_app_sim_quakec_client_network info);

/* Travel. The captured travel's arrays are source-owned until the next
 * capture or destroy. */
qa_app_sim_quakec_source_travel qa_app_sim_quakec_source_capture_travel(qa_app_sim_quakec_source *source);
void qa_app_sim_quakec_source_restore_travel(qa_app_sim_quakec_source *source, qa_app_sim_quakec_source_travel travel);

/* Clients. */
void qa_app_sim_quakec_source_prepare_client_spawn(qa_app_sim_quakec_source *source, qa_client_id client);
typedef struct qa_app_sim_quakec_frag { qa_actor_id killer, victim; } qa_app_sim_quakec_frag;
const qa_app_sim_quakec_frag *qa_app_sim_quakec_source_drain_frag_log(qa_app_sim_quakec_source *source, size_t *out_count);
typedef struct qa_app_sim_quakec_routed {
  qa_compat_qc_routed_message *entries; size_t entry_count;
  qa_compat_qc_message_destination destination;
} qa_app_sim_quakec_routed;
const qa_app_sim_quakec_routed *qa_app_sim_quakec_source_drain_messages(qa_app_sim_quakec_source *source, size_t *out_count);
const qa_compat_qc_routed_message *qa_app_sim_quakec_source_signon_messages(qa_app_sim_quakec_source *source, size_t *out_count);
void qa_app_sim_quakec_source_attach_netquake_wire(qa_app_sim_quakec_source *source);
const qa_netquake_message *qa_app_sim_quakec_source_netquake_signon_messages(qa_app_sim_quakec_source *source, size_t *out_count);
typedef struct qa_app_sim_quakec_netquake_routed {
  qa_netquake_message *messages; size_t message_count;
  qa_compat_qc_message_destination destination;
} qa_app_sim_quakec_netquake_routed;
const qa_app_sim_quakec_netquake_routed *qa_app_sim_quakec_source_drain_netquake_messages(qa_app_sim_quakec_source *source, size_t *out_count);
const qa_client_id *qa_app_sim_quakec_source_connected_client_identities(qa_app_sim_quakec_source *source, size_t *out_count);
bool qa_app_sim_quakec_source_client_actor(qa_app_sim_quakec_source *source, qa_client_id client, qa_actor_id *out);
/* kind "model"|"sound". Names borrowed from the source tables. */
const char *const *qa_app_sim_quakec_source_precache_names(qa_app_sim_quakec_source *source, const char *kind, size_t *out_count);
bool qa_app_sim_quakec_source_source_slot(qa_app_sim_quakec_source *source, qa_actor_id actor, int64_t *out);
bool qa_app_sim_quakec_source_is_reserved_client(qa_app_sim_quakec_source *source, qa_actor_id actor);
bool qa_app_sim_quakec_source_is_active_client(qa_app_sim_quakec_source *source, qa_actor_id actor);
/* name "god"|"notarget"|"noclip"|"fly"|"give". */
void qa_app_sim_quakec_source_host_cheat(qa_app_sim_quakec_source *source, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count);
qa_owned_actor qa_app_sim_quakec_source_admit_client(qa_app_sim_quakec_source *source, qa_client_id client);
bool qa_app_sim_quakec_source_client_kill(qa_app_sim_quakec_source *source, qa_actor_id actor);
bool qa_app_sim_quakec_source_has_client(qa_app_sim_quakec_source *source, qa_client_id client);
void qa_app_sim_quakec_source_disconnect_client(qa_app_sim_quakec_source *source, qa_owned_actor actor);
bool qa_app_sim_quakec_source_client_weapon_settled(qa_app_sim_quakec_source *source, qa_actor_id actor);
typedef struct qa_app_sim_quakec_equipment { double max_health, quad_until; } qa_app_sim_quakec_equipment;
qa_app_sim_quakec_equipment qa_app_sim_quakec_source_client_equipment(qa_app_sim_quakec_source *source, qa_actor_id actor);
/* actor NULL = TS null (world). */
double qa_app_sim_quakec_source_damage_amount(qa_app_sim_quakec_source *source, const qa_actor_id *actor, double amount);
/* powerup "quad"|"invulnerability"|"invisibility"|"suit". */
double qa_app_sim_quakec_source_client_powerup_expires(qa_app_sim_quakec_source *source, qa_actor_id actor, const char *powerup);
typedef struct qa_app_sim_quakec_weapon_target { bool monster, aimed_damage; } qa_app_sim_quakec_weapon_target;
bool qa_app_sim_quakec_source_weapon_target(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_app_sim_quakec_weapon_target *out);
void qa_app_sim_quakec_source_set_client_max_health(qa_app_sim_quakec_source *source, qa_actor_id actor, double value);
void qa_app_sim_quakec_source_drop_client_objectives(qa_app_sim_quakec_source *source, qa_actor_id actor);
typedef struct qa_app_sim_quakec_spawn_point { qa_vec3 origin, angles; } qa_app_sim_quakec_spawn_point;
qa_app_sim_quakec_spawn_point qa_app_sim_quakec_source_client_spawn_point(qa_app_sim_quakec_source *source, qa_actor_id actor);
bool qa_app_sim_quakec_source_pickup_selection_deferred(qa_app_sim_quakec_source *source, qa_original_pickup_offer offer);
qa_q1_id1_pickup_supply qa_app_sim_quakec_source_pickup_supply(qa_app_sim_quakec_source *source, qa_original_pickup_offer offer);

/* Movement and input. postThink "immediate"|"deferred" (NULL = immediate);
 * movement is the MovementProfile kind. */
qa_app_sim_quakec_client_movement qa_app_sim_quakec_source_mixed_client_pre_think(qa_app_sim_quakec_source *source,
    qa_owned_actor actor, qa_user_command command, qa_frame_context frame);
qa_app_sim_quakec_client_movement qa_app_sim_quakec_source_mixed_quakeworld_pre_think(qa_app_sim_quakec_source *source,
    qa_owned_actor actor, qa_qw_user_command source_command, qa_user_command command, qa_frame_context frame);
void qa_app_sim_quakec_source_mixed_client_post_think(qa_app_sim_quakec_source *source, qa_owned_actor actor,
    qa_trace_hit ground, double water_level, double water_type, const char *movement, const char *post_think);
void qa_app_sim_quakec_source_client_input(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_q1_user_command command);
qa_qw_movement_state qa_app_sim_quakec_source_read_quakeworld_state(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_qw_movement_state state);
void qa_app_sim_quakec_source_write_quakeworld_state(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_qw_movement_state state);
void qa_app_sim_quakec_source_quakeworld_water(qa_app_sim_quakec_source *source, qa_actor_id actor, double level, double type);
qa_qw_movement_profile qa_app_sim_quakec_source_quakeworld_profile(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_qw_movement_profile profile);
void qa_app_sim_quakec_source_quakeworld_pre_think(qa_app_sim_quakec_source *source, qa_owned_actor actor,
    qa_qw_user_command command, qa_frame_context frame);
bool qa_app_sim_quakec_source_take_new_missile(qa_app_sim_quakec_source *source, qa_owned_actor *out);
bool qa_app_sim_quakec_source_run_actor_once(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_frame_context frame);
qa_q1_movement_state qa_app_sim_quakec_source_read_client_state(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_q1_movement_state state);
qa_vec3 qa_app_sim_quakec_source_client_punch_angles(qa_app_sim_quakec_source *source, qa_actor_id actor);
void qa_app_sim_quakec_source_set_client_view_roll(qa_app_sim_quakec_source *source, qa_actor_id actor, double roll);
bool qa_app_sim_quakec_source_client_punch_advances(qa_app_sim_quakec_source *source, qa_actor_id actor);
void qa_app_sim_quakec_source_set_client_punch_angles(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_vec3 angles);
void qa_app_sim_quakec_source_write_client_state(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_q1_movement_state state);
typedef struct qa_app_sim_quakec_netquake_damage { double armor, blood; qa_vec3 source; } qa_app_sim_quakec_netquake_damage;
bool qa_app_sim_quakec_source_consume_netquake_damage(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_app_sim_quakec_netquake_damage *out);
bool qa_app_sim_quakec_source_consume_client_view_reset(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_vec3 *out);
bool qa_app_sim_quakec_source_request_client_weapon(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_item_id item);
void qa_app_sim_quakec_source_client_pre_think(qa_app_sim_quakec_source *source, qa_owned_actor actor);
void qa_app_sim_quakec_source_client_post_think(qa_app_sim_quakec_source *source, qa_owned_actor actor);
/* TS clientUi: quakeCWeaponUi spread plus powerup timers. items is
 * caller-freed (qa_app_sim_quakec_weapon_ui contract). */
typedef struct qa_app_sim_quakec_client_ui {
  qa_app_sim_quakec_weapon_ui weapon;
  qa_active_powerup_timer powerups[4]; size_t powerup_count;
} qa_app_sim_quakec_client_ui;
qa_app_sim_quakec_client_ui qa_app_sim_quakec_source_client_ui(qa_app_sim_quakec_source *source, qa_actor_id actor);
qa_arsenal_state qa_app_sim_quakec_source_client_arsenal(qa_app_sim_quakec_source *source, qa_actor_id actor);
qa_actor_animation_state qa_app_sim_quakec_source_client_animation(qa_app_sim_quakec_source *source, qa_actor_id actor);
qa_app_sim_quakec_session_state qa_app_sim_quakec_source_local_client_session(qa_app_sim_quakec_source *source, qa_actor_id actor);
/* TS boolean | null: false return = null. */
bool qa_app_sim_quakec_source_local_client_intermission(qa_app_sim_quakec_source *source, qa_actor_id actor, bool *out);
bool qa_app_sim_quakec_source_local_client_view(qa_app_sim_quakec_source *source, qa_actor_id actor, qa_app_sim_player_view *out);
qa_vec3 qa_app_sim_quakec_source_client_view_offset(qa_app_sim_quakec_source *source, qa_actor_id actor);

/* Frame lifecycle. */
void qa_app_sim_quakec_source_spawn_map(qa_app_sim_quakec_source *source);
void qa_app_sim_quakec_source_begin_frame(qa_app_sim_quakec_source *source, qa_frame_context frame);
void qa_app_sim_quakec_source_before_actor(qa_app_sim_quakec_source *source, qa_owned_actor actor);
void qa_app_sim_quakec_source_end_frame(qa_app_sim_quakec_source *source);
void qa_app_sim_quakec_source_run_think(qa_app_sim_quakec_source *source, qa_owned_actor actor, qa_frame_context frame);
/* False = TS null. */
bool qa_app_sim_quakec_source_read_move_type(qa_app_sim_quakec_source *source, qa_actor_id actor, double *out);
bool qa_app_sim_quakec_source_notarget(qa_app_sim_quakec_source *source, qa_actor_id actor, bool *out);
bool qa_app_sim_quakec_source_collision(qa_app_sim_quakec_source *source, qa_owned_actor actor, qa_app_sim_shared_solid *out);
bool qa_app_sim_quakec_source_motion(qa_app_sim_quakec_source *source, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out);
qa_app_sim_shared_physics_flags qa_app_sim_quakec_source_flags(qa_app_sim_quakec_source *source, qa_owned_actor actor);
void qa_app_sim_quakec_source_write_flags(qa_app_sim_quakec_source *source, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes);
/* Source-owned copy valid until the next classname call. */
const char *qa_app_sim_quakec_source_classname(qa_app_sim_quakec_source *source, qa_actor_id actor);
void qa_app_sim_quakec_source_check_water_transition(qa_app_sim_quakec_source *source, qa_owned_actor actor);
void qa_app_sim_quakec_source_write_angular_velocity(qa_app_sim_quakec_source *source, qa_owned_actor actor, qa_vec3 value);
#endif
