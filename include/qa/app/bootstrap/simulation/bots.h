/* Port of src/app/bootstrap/simulation/bots.ts: SimulationBotServices,
 * ApplicationBots (the Q3 source director transport over shared players),
 * the transport checkpoint codec, the bot log files and bot admission.
 *
 * C accommodations:
 *  - TS bots take the concrete SharedSimulation. The C SharedSimulation does
 *    not expose the members bots read, so they take qa_app_sim_bot_simulation:
 *    the real lower-layer owners the TS fields name (actor registry, body
 *    table, physics, combat, inventory, scene queries, random) plus the
 *    SharedSimulation methods as fn+ctx. Every member receives ctx.
 *  - The TS BotArsenalBinding (bot-arsenal.ts) is qa_app_sim_bot_arsenal; the
 *    Q1/Q2/Q3 knowledge modules fill it. qa_app_sim_create_bot_arsenal is TS
 *    createBotArsenalBinding.
 *  - Checkpoints are qa_save_value trees (the TS objects encoded by
 *    encodeCheckpointValue); returned trees are owned by the caller.
 *  - TS throws that reach the top level abort through qa_raise ("Error").
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/bots/behavior/director.h"
#include "qa/bots/behavior/library/log.h"
#include "qa/bots/behavior/population.h"
#include "qa/bots/behavior/q3/arsenal_knowledge.h"
#include "qa/bots/behavior/q3/navigation_services.h"
#include "qa/bots/behavior/q3/source_game.h"
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/composition/q2/index.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/core/cvars/index.h"
#include "qa/network/q3/reliable.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/session.h"

struct qa_app_q3_source_runtime;
struct qa_app_q3_source_subsystems_owner;
struct qa_app_q3_source_host;
struct qa_app_sim_bot_services;

/* TS SourceRuntime {kind:"q1"} as q1Source() returns it. */
typedef struct qa_app_sim_bot_q1_source {
  qa_q1_services *game; /* TS Q1Foundation */
  qa_comp_q1_source_composition *composition;
  qa_cvar_registry *cvars;
} qa_app_sim_bot_q1_source;
/* TS Q3SourceRuntime as q3Source() returns it: the frame runtime, its
 * subsystem owner (pool, records, world, level, admission) and host. */
typedef struct qa_app_sim_bot_q3_source {
  struct qa_app_q3_source_runtime *runtime;
  struct qa_app_q3_source_subsystems_owner *owner;
  struct qa_app_q3_source_host *host;
} qa_app_sim_bot_q3_source;
/* TS SharedSimulation.botEntity() result; strings borrowed until the next call. */
typedef struct qa_app_sim_bot_entity {
  const char *classname, *model, *targetname;
  double health, spawnflags;
  bool hidden;
} qa_app_sim_bot_entity;
/* TS sourceObjectives() entry: botGoal and state {carrier, target, complete}. */
typedef struct qa_app_sim_source_objective {
  bool bot_goal, complete;
  bool has_target; qa_actor_id target;
  bool has_carrier; qa_actor_id carrier;
} qa_app_sim_source_objective;
/* TS setSourcePlayerIdentity identity; NULL / has_* false are absent members. */
typedef struct qa_app_sim_player_identity {
  const char *name, *skin;
  bool has_shirt; double shirt;
  bool has_pants; double pants;
} qa_app_sim_player_identity;

/* TS SharedSimulation, as bots read it. Arrays returned by members are
 * borrowed until the next call of the same member. */
typedef struct qa_app_sim_bot_simulation {
  void *ctx;
  qa_session_id session;
  const qa_executable_recipe *recipe;
  const char *mode; /* options.mode */
  double max_clients, skill; /* options.maxClients / options.skill */
  size_t world_model_count; /* options.world.models.length */
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_app_sim_shared_physics *physics;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_shared_scene_queries *scene;
  qa_app_sim_source_random *random;
  struct qa_app_sim_bot_services *bot_services;
  double (*time_seconds)(void *ctx);
  const char *(*source_entity_text)(void *ctx);
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  const qa_client_id *(*client_identities)(void *ctx, size_t *out_count);
  /* NULL is TS null. */
  qa_app_sim_movement_player *(*movement_player)(void *ctx, qa_actor_id actor);
  qa_app_sim_player_ui (*player_ui)(void *ctx, qa_actor_id actor);
  /* false is TS null. */
  bool (*bot_entity)(void *ctx, qa_actor_id actor, qa_app_sim_bot_entity *out);
  void (*player_command)(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
  qa_owned_actor (*prepare_bot_client)(void *ctx, qa_client_id client);
  void (*disconnect_player)(void *ctx, qa_actor_id actor);
  void (*set_source_player_identity)(void *ctx, qa_actor_id actor, const qa_app_sim_player_identity *identity);
  const qa_app_sim_source_objective *(*source_objectives)(void *ctx, size_t *out_count);
  qa_cvar_registry *(*q2_server_cvars)(void *ctx); /* NULL result is TS null */
  const qa_app_sim_bot_q1_source *(*q1_source)(void *ctx);
  qa_q2_product_runtime *(*q2_source)(void *ctx);
  const qa_app_sim_bot_q3_source *(*q3_source)(void *ctx);
  bool (*q3_guest)(void *ctx);
  qa_q1_services *(*q1_weapon_source)(void *ctx);
  /* false is TS null. */
  bool (*q2_weapon_source)(void *ctx, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons);
  qa_app_q3_selected_arsenal *(*selected_q3_weapon_source)(void *ctx);
  /* The createPlayerMovementPrediction collaborators (player-movement.ts). */
  qa_app_sim_prediction_simulation (*prediction)(void *ctx);
} qa_app_sim_bot_simulation;

/* recipe.timing entry for recipe.engineBehavior.provider; false is TS undefined. */
bool qa_app_sim_bot_numeric(const qa_app_sim_bot_simulation *simulation, qa_numeric_profile *out);

/* ---- TS BotArsenalBinding (bot-arsenal.ts) and botKnowledgePersistence ---- */
/* TS (client) => ActorId | null; false is TS null. */
typedef bool (*qa_app_sim_actor_for_client_fn)(void *ctx, int64_t client, qa_actor_id *out);
typedef struct qa_app_sim_bot_knowledge_handle { int64_t handle; qa_actor_id actor; } qa_app_sim_bot_knowledge_handle;
/* Heap allocated; must not move (knowledge.knowledge.ctx is &knowledge). */
typedef struct qa_app_sim_bot_arsenal {
  const char *source; /* "q1"|"q2"|"q3" */
  qa_bot_arsenal_knowledge_bots_a knowledge; /* .knowledge is TS binding.knowledge */
  /* TS actorsForHandle Map, insertion order. */
  qa_app_sim_bot_knowledge_handle *actors; size_t actor_count, actor_capacity;
  const char **uncovered_weapons; size_t uncovered_count; /* strings borrowed */
  const qa_app_sim_bot_simulation *simulation;
  qa_app_sim_actor_for_client_fn actor_for_client; void *actor_ctx;
  void *impl;
  qa_item_id (*resolve_weapon)(struct qa_app_sim_bot_arsenal *arsenal, int64_t client, int64_t decision_slot);
  int64_t (*source_weapon)(struct qa_app_sim_bot_arsenal *arsenal, int64_t client);
  int64_t (*source_weapon_state)(struct qa_app_sim_bot_arsenal *arsenal, int64_t client);
  void (*destroy_impl)(void *impl);
  qa_q3_bot_arsenal_binding_bots_a q3_binding; /* the subset q3BotGame reads */
} qa_app_sim_bot_arsenal;
/* TS createBotArsenalBinding: Q2, then Q1, then selected Q3; NULL is TS null. */
qa_app_sim_bot_arsenal *qa_app_sim_create_bot_arsenal(const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx);
void qa_app_sim_bot_arsenal_destroy(qa_app_sim_bot_arsenal *arsenal);
/* Shared constructor tail for the knowledge modules: ctx of data must be the arsenal. */
qa_app_sim_bot_arsenal *qa_app_sim_bot_arsenal_alloc(const char *source, const qa_app_sim_bot_simulation *simulation,
    qa_app_sim_actor_for_client_fn actor_for_client, void *actor_ctx);
void qa_app_sim_bot_arsenal_bind(qa_app_sim_bot_arsenal *arsenal,
    void (*update_inventory)(void *ctx, struct qa_bot_state_bots_a *state),
    qa_bot_weapon_knowledge_bots_a *(*candidates)(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle, size_t *out_count));
bool qa_app_sim_bot_arsenal_actor(const qa_app_sim_bot_arsenal *arsenal, int64_t client, qa_actor_id *out);
/* TS actorsForHandle.set / delete. */
void qa_app_sim_bot_arsenal_track(qa_app_sim_bot_arsenal *arsenal, int64_t handle, const qa_actor_id *actor);
bool qa_app_sim_bot_arsenal_handle_actor(const qa_app_sim_bot_arsenal *arsenal, int64_t handle, qa_actor_id *out);
/* TS BotKnowledgePersistence.checkpoint / restoreCheckpoint. */
qa_save_value *qa_app_sim_bot_arsenal_checkpoint(const qa_app_sim_bot_arsenal *arsenal);
typedef qa_actor_id (*qa_app_sim_saved_actor_ref_fn)(void *ctx, qa_saved_actor_id saved);
typedef int64_t (*qa_app_sim_weapon_handle_ref_fn)(void *ctx, int64_t saved);
void qa_app_sim_bot_arsenal_restore_checkpoint(qa_app_sim_bot_arsenal *arsenal, const qa_save_value *value,
    qa_app_sim_saved_actor_ref_fn actor, void *actor_ctx, qa_app_sim_weapon_handle_ref_fn weapon_handle, void *weapon_ctx);

/* ---- transport types ---- */
/* TS ApplicationBotClient; userinfo NULL is the absent member. */
typedef struct qa_app_sim_bot_client {
  qa_session_client *client;
  qa_q3_server_reliable_commands *reliable;
  const char *userinfo;
} qa_app_sim_bot_client;
/* TS SelectedBotNavigation | ApplicationBotNavigation. checkpoint and
 * restore_checkpoint NULL: not ApplicationBotNavigation ("checkpoint" in
 * navigation false); restart_round NULL: the absent member. */
typedef struct qa_app_sim_bot_navigation {
  qa_selected_bot_navigation_bots_a selected;
  void *ctx;
  qa_save_value *(*checkpoint)(void *ctx);
  void (*restore_checkpoint)(void *ctx, const qa_save_value *value);
  void (*restart_round)(void *ctx);
} qa_app_sim_bot_navigation;
typedef struct qa_app_sim_bot_transport_connection {
  double client_slot, client_generation;
  qa_saved_actor_id actor;
  double sequence, acknowledge;
  char *slots[QA_Q3_MAX_RELIABLE_COMMANDS]; size_t slot_count;
} qa_app_sim_bot_transport_connection;
typedef struct qa_app_sim_bot_transport_snapshot { double client; double *entities; size_t entity_count; } qa_app_sim_bot_transport_snapshot;
/* TS ApplicationBotTransportCheckpoint (decoded, owned). */
typedef struct qa_app_sim_bot_transport_checkpoint {
  double elapsed_milliseconds;
  qa_app_sim_bot_transport_connection *connections; size_t connection_count;
  qa_app_sim_bot_transport_snapshot *snapshots; size_t snapshot_count;
} qa_app_sim_bot_transport_checkpoint;
typedef struct qa_app_sim_bot_observation { double number; qa_saved_actor_id actor; } qa_app_sim_bot_observation;
/* TS DecodedApplicationBotsCheckpoint. director/navigation/knowledge/
 * shared_world borrow the decoded value (NULL is TS undefined). */
typedef struct qa_app_sim_decoded_bots_checkpoint {
  qa_app_sim_bot_transport_checkpoint transport;
  const qa_save_value *director, *navigation, *knowledge, *shared_world;
  qa_app_sim_bot_observation *observations; size_t observation_count;
} qa_app_sim_decoded_bots_checkpoint;
void qa_app_sim_decode_application_bots_checkpoint(const qa_save_value *value, qa_app_sim_decoded_bots_checkpoint *out);
void qa_app_sim_decoded_bots_checkpoint_free(qa_app_sim_decoded_bots_checkpoint *image);
void qa_app_sim_decode_bot_transport(const qa_save_value *value, qa_app_sim_bot_transport_checkpoint *out);
void qa_app_sim_bot_transport_checkpoint_free(qa_app_sim_bot_transport_checkpoint *image);
qa_save_value *qa_app_sim_encode_bot_transport_connection(qa_session_client *client, qa_actor_id actor,
    qa_q3_server_reliable_commands *reliable);
/* TS RestoredBotReliableCommands: NULL only on OOM; invalid images raise. */
qa_q3_server_reliable_commands *qa_app_sim_restored_bot_reliable_commands(const qa_app_sim_bot_transport_connection *image);

/* TS ApplicationBotsOptions. Callback members receive ctx. resolve_client
 * NULL result is TS null. clients NULL is the absent member. */
typedef struct qa_app_sim_bots_options {
  qa_engine_session *session;
  const qa_app_sim_bot_simulation *simulation;
  qa_source_bot_files files;
  qa_app_sim_bot_navigation *navigation;
  double leaf_count;
  qa_cvar_registry *configuration; /* NULL is TS undefined */
  bool restart, automatic_frame;
  const qa_app_sim_bot_client *clients; size_t client_count;
  const qa_app_sim_decoded_bots_checkpoint *restore; /* NULL: no restore */
  qa_session_client *(*resolve_client)(void *ctx, double slot, double generation);
  void *ctx;
  void (*insert_console_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  qa_bot_log_open_result (*open_log)(void *ctx, const char *filename);
  qa_bot_log_open_result (*resume_log)(void *ctx, const char *filename, int64_t position); /* NULL absent */
} qa_app_sim_bots_options;

/* TS ApplicationBotService (Pick<ApplicationBots, ...>). Every member receives ctx. */
typedef struct qa_app_sim_bot_service {
  void *ctx;
  const qa_app_sim_bots_options *options;
  qa_cvar_registry *(*configuration)(void *ctx);
  int32_t (*move_to_point)(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance);
  int32_t (*follow_actor)(void *ctx, qa_actor_id actor, qa_actor_id target);
  bool (*is_bot)(void *ctx, qa_actor_id actor);
  bool (*actor)(void *ctx, qa_client_id client, qa_actor_id *out);
  /* Returned commands are borrowed until the next frame. */
  const qa_actor_command *(*frame)(void *ctx, double time, double elapsed, size_t *out_count);
  void (*receive)(void *ctx, const qa_app_sim_presentation_event *events, size_t count);
  /* Borrowed until the next clients call. */
  const qa_app_sim_bot_client *(*clients)(void *ctx, size_t *out_count);
  void (*console_command)(void *ctx, const char *const *argv, size_t argc);
  bool (*disconnect)(void *ctx, int64_t client);
  void (*close)(void *ctx, bool restart);
  qa_save_value *(*checkpoint)(void *ctx);
  const qa_app_sim_bot_client *(*begin_round_restart)(void *ctx, size_t *out_count);
  void (*bind_restarted_round)(void *ctx);
  bool (*reconnect_restarted_client)(void *ctx, qa_client_id client);
  void (*resume_round_bots)(void *ctx);
} qa_app_sim_bot_service;

/* ---- TS SimulationBotServices ---- */
typedef struct qa_app_sim_bot_services {
  qa_source_bot_director *director;
  const qa_app_sim_bot_service *transport;
  qa_app_q3_source_bots source; /* TS source: Q3SourceBots; ctx is the services */
} qa_app_sim_bot_services;
void qa_app_sim_bot_services_init(qa_app_sim_bot_services *services);
int32_t qa_app_sim_bot_services_move_to_point(qa_app_sim_bot_services *services, qa_actor_id actor, qa_vec3 point, double tolerance);
int32_t qa_app_sim_bot_services_follow_actor(qa_app_sim_bot_services *services, qa_actor_id actor, qa_actor_id target);
bool qa_app_sim_bot_services_is_bot(qa_app_sim_bot_services *services, qa_actor_id actor);
bool qa_app_sim_bot_services_try_add_bot(qa_app_sim_bot_services *services);
bool qa_app_sim_bot_services_try_disconnect_bot(qa_app_sim_bot_services *services, qa_actor_id actor);
qa_cvar_registry *qa_app_sim_bot_services_configuration(qa_app_sim_bot_services *services);
qa_save_value *qa_app_sim_bot_services_checkpoint(qa_app_sim_bot_services *services);
const qa_actor_command *qa_app_sim_bot_services_frame(qa_app_sim_bot_services *services, double time, double elapsed, size_t *out_count);
/* TS navigation(actor, start, goal); points malloc'd (caller frees). */
typedef enum qa_app_sim_bot_route_kind { QA_APP_SIM_BOT_ROUTE_PATH, QA_APP_SIM_BOT_ROUTE_NO_NAVIGATION, QA_APP_SIM_BOT_ROUTE_UNREACHABLE } qa_app_sim_bot_route_kind;
typedef struct qa_app_sim_bot_route { qa_app_sim_bot_route_kind kind; double distance_squared; qa_vec3 *points; size_t point_count; } qa_app_sim_bot_route;
qa_app_sim_bot_route qa_app_sim_bot_services_navigation(qa_app_sim_bot_services *services, qa_actor_id actor, qa_vec3 start, qa_vec3 goal);
/* TS source members connect/shutdownClient/removeQueuedBegin. */
bool qa_app_sim_bot_services_source_connect(qa_app_sim_bot_services *services, int64_t client, bool restart);
void qa_app_sim_bot_services_source_shutdown_client(qa_app_sim_bot_services *services, int64_t client, bool restart);
void qa_app_sim_bot_services_source_remove_queued_begin(qa_app_sim_bot_services *services, int64_t client);
void qa_app_sim_bot_services_attach(qa_app_sim_bot_services *services, qa_source_bot_director *director, const qa_app_sim_bot_service *transport);
void qa_app_sim_bot_services_attach_transport(qa_app_sim_bot_services *services, const qa_app_sim_bot_service *transport);
void qa_app_sim_bot_services_detach_transport(qa_app_sim_bot_services *services, const qa_app_sim_bot_service *transport);
void qa_app_sim_bot_services_detach(qa_app_sim_bot_services *services, qa_source_bot_director *director);

/* ---- TS ApplicationBots ---- */
typedef struct qa_app_sim_application_bots qa_app_sim_application_bots;
/* TS constructor; TS throws raise. The options are copied; their pointers
 * stay borrowed. NULL only on OOM. */
qa_app_sim_application_bots *qa_app_sim_application_bots_create(const qa_app_sim_bots_options *options);
/* Releases C storage; close() first for the TS shutdown. */
void qa_app_sim_application_bots_destroy(qa_app_sim_application_bots *bots);
const qa_app_sim_bot_service *qa_app_sim_application_bots_service(qa_app_sim_application_bots *bots);
qa_source_bot_director *qa_app_sim_application_bots_director(qa_app_sim_application_bots *bots);
qa_source_bot_game_bots_a *qa_app_sim_application_bots_game(qa_app_sim_application_bots *bots);
qa_save_value *qa_app_sim_application_bots_checkpoint_orchestration(qa_app_sim_application_bots *bots);
void qa_app_sim_application_bots_restore_orchestration(qa_app_sim_application_bots *bots, const qa_save_value *value);

/* TS openApplicationBotLog / resumeApplicationBotLog, under
 * $HOME/.local/state/quake-typescript/bots (directory overridable for tests:
 * NULL uses the TS home path). */
qa_bot_log_open_result qa_app_sim_open_application_bot_log(const char *directory, const char *filename);
qa_bot_log_open_result qa_app_sim_resume_application_bot_log(const char *directory, const char *filename, double position);
/* TS botAdmissionError: NULL is TS null; else static text. */
const char *qa_app_sim_bot_admission_error(const qa_app_sim_bot_simulation *simulation);
#endif
