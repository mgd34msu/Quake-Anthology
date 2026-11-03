/* Port of src/app/bootstrap/simulation/network.ts (createQ2ApplicationServerHost).
 *
 * The TS adapters take `simulation: SharedSimulation` and `content:
 * LoadedApplicationContent` as concrete classes. runtime.ts is not yet ported as a
 * class with the members these adapters call, so the members they read are gathered
 * in qa_app_sim_network_simulation and qa_app_sim_network_content: real C objects
 * (actor registry, body table, scene queries, recipe, options, source games) are
 * pointers, and each SharedSimulation / LoadedApplicationContent method the TS calls
 * is one fn+ctx entry with the TS name. Every entry receives `ctx`.
 *
 * Presentation events reach the Q1, QW and Q2 adapters as qa_app_sim_presentation_event
 * (kind + borrowed typed payload in `event`: qa_q1_event, qa_q2_presentation_event,
 * qa_q2_player_event, qa_q2w_event); the unified adapter receives them as
 * unified-event-codec qa_save_value trees, the form the unified server encodes.
 * TS throws become qa_raise("Error", ...) with the TS message; async TS functions are
 * synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* network/q2/state.h declares the Q2 pm_type enumerators that movement/q2/types.h
 * (reached through players.h) defines as macros: it must come first. */
#include "qa/network/q2/frames.h"
#include "qa/network/q2/mvd_encoding.h"
#include "qa/network/q2/server_messages.h"
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/app/bootstrap/network/q3_downloads.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/network/unified_prediction.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/composition/q2/index.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q3/foundation/presentation.h"
#include "qa/contracts/content.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/endpoint.h"
#include "qa/persistence/value.h"
#include "qa/text/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/session.h"

typedef struct qa_app_q3_source_runtime qa_app_q3_source_runtime;
typedef struct qa_app_q3_qvm_server_game qa_app_q3_qvm_server_game;

/* TS SharedSimulation["sourceRestartPlan"] result. */
typedef struct qa_app_sim_network_restart_plan {
  bool source_reset; /* kind "source-reset"; false is "replace-world" */
  const char *reason;
} qa_app_sim_network_restart_plan;

/* The SharedSimulation members the network adapters read (see the file comment). */
typedef struct qa_app_sim_network_simulation {
  void *ctx;
  qa_session_id session;
  const qa_executable_recipe *recipe;          /* simulation.recipe */
  const qa_app_simulation_options *options;    /* simulation.options */
  qa_session_actor_registry *actors;           /* simulation.actors */
  qa_shared_body_table *bodies;                /* simulation.bodies */
  qa_shared_scene_queries *scene;              /* simulation.scene */
  qa_shared_inventory_table *inventory;        /* simulation.inventory */
  /* The SharedSimulation surface projectUnifiedPrediction reads. */
  qa_app_unified_prediction_simulation prediction;
  /* Source runtimes: NULL is TS null. */
  qa_q2_product_runtime *(*q2_source)(void *ctx);
  qa_cvar_registry *(*q2_server_cvars)(void *ctx);
  bool (*q2_movement_config)(void *ctx, qa_app_sim_q2_movement_config *out);
  /* q2PlayerView(actor): false is TS null. */
  bool (*q2_player_view)(void *ctx, qa_actor_id actor, qa_q2_base_player_view *out);
  qa_comp_q1_source_composition *(*q1_source)(void *ctx);
  qa_app_q3_source_runtime *(*q3_source)(void *ctx);
  qa_app_q3_qvm_server_game *(*q3_guest)(void *ctx);
  /* Players and admission. players() is borrowed until the next call. */
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  qa_app_sim_movement_player *(*movement_player)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  qa_app_sim_player_admission (*admit_player)(void *ctx, qa_client_id client);
  void (*disconnect_player)(void *ctx, qa_actor_id actor);
  void (*notify_client_event)(void *ctx, const char *kind, qa_actor_id actor);
  void (*observe_client_command)(void *ctx, const qa_actor_command *input);
  void (*player_command)(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t count);
  qa_owned_actor (*register_q2_native_client)(void *ctx, qa_client_id client);
  /* q2NativePlayers(): parallel client/actor arrays, borrowed until the next call. */
  size_t (*q2_native_players)(void *ctx, const qa_client_id **out_clients, const qa_actor_id **out_actors);
  /* Presentation reads, borrowed until the next call. */
  const qa_app_simulation_presentation *(*presentations)(void *ctx, size_t *out_count);
  qa_app_sim_player_view (*player_view)(void *ctx, qa_actor_id actor);
  qa_app_sim_player_ui (*player_ui)(void *ctx, qa_actor_id actor);
  const qa_q3_character_view *(*character_views)(void *ctx, size_t *out_count);
  const qa_world_text *(*world_text)(void *ctx, size_t *out_count);
  double (*time_seconds)(void *ctx);
  /* q1Paused / toggleQ1Pause(actor): actor NULL is TS null; the text is borrowed. */
  bool (*q1_paused)(void *ctx);
  const char *(*toggle_q1_pause)(void *ctx, const qa_actor_id *actor);
  /* simulation.events.lightStyle(index), .capture().persistent (borrowed events),
   * .persistentPresentation() (borrowed unified-event trees), .resource(id) (false = TS null). */
  const char *(*light_style)(void *ctx, double index);
  const qa_app_sim_presentation_event *const *(*persistent_events)(void *ctx, size_t *out_count);
  qa_save_value *const *(*persistent_presentation)(void *ctx, size_t *out_count);
  bool (*event_resource)(void *ctx, qa_resource_id id, qa_resolved_resource_reference *out);
  void (*register_resource)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource);
  qa_app_sim_network_restart_plan (*source_restart_plan)(void *ctx);
  /* QuakeWorld command queues. */
  void (*queue_quakeworld_action)(void *ctx, qa_client_id client, void (*action)(void *action_ctx), void *action_ctx);
  void (*queue_quakeworld_commands)(void *ctx, qa_client_id client, const qa_qw_user_command *commands, size_t count, double sequence);
  qa_actor_id (*reserve_netquake_client)(void *ctx, qa_client_id client);
} qa_app_sim_network_simulation;

/* The LoadedApplicationContent members the network adapters read. */
typedef struct qa_app_sim_network_content {
  void *ctx;
  const qa_executable_recipe *recipe;  /* content.recipe */
  qa_decoded_world world;              /* content.world */
  /* forContent(content): the mounted content as the Q2 download service reads it. */
  qa_app_q2_mounts (*for_content)(void *ctx, qa_content_id content);
  /* forContent(content).resolve(path): false is TS null. */
  bool (*resolve)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference *out);
  /* mounts.read(reference): malloc'd bytes. */
  uint8_t *(*read)(void *ctx, const qa_resolved_resource_reference *reference, size_t *out_length);
  /* mounts.read(path) / mounts.resolve(path) / mounts.open(path) (open false is TS null). */
  uint8_t *(*read_path)(void *ctx, const char *path, size_t *out_length);
  bool (*resolve_path)(void *ctx, const char *path, qa_resolved_resource_reference *out);
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length,
      qa_resolved_resource_reference *out_reference);
  /* catalog.product(content).expectation.contentDirectory (borrowed). */
  const char *(*content_directory)(void *ctx, qa_content_id content);
  bool q3_demo_restricted;  /* content.q3Product?.restriction.kind === 'demo' */
  /* Q3ApplicationPackages.open(content): content.mounts.plan.defaultOrder as package mounts
   * with each mount's product expectation (borrowed until the next call). NULL fns: no mounts. */
  const qa_app_q3_package_mount *(*q3_package_mounts)(void *ctx, size_t *out_count);
  /* Q3ApplicationPackages.collect(content): every openedResources entry of every
   * content.openedMounts() mount, with whether its mount's product family is q3. */
  void (*q3_collect_opened)(void *ctx,
      void (*visit)(void *visit_ctx, const qa_resolved_resource_reference *reference, bool is_q3),
      void *visit_ctx);
  bool has_prepared_quakec; /* content.preparedQuakeC !== null */
} qa_app_sim_network_content;

/* TS Q2ApplicationServerBindingOptions. Optional TS members are NULL fns/pointers. */
typedef struct qa_app_sim_q2_server_binding_options {
  qa_engine_session *session;
  void *rejects_ctx; bool (*rejects)(void *ctx, const qa_net_address *address);
  const qa_app_sim_network_simulation *simulation;
  const qa_app_sim_network_content *content;
  qa_q2_protocol_identity protocol;
  const void *administration;
  const void *const *masters; size_t master_count;
  /* playerIdentity(client, identity): has_identity false is TS null. */
  void *player_identity_ctx;
  void (*player_identity)(void *ctx, qa_client_id client, bool has_identity, double seat, const char *social_id);
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_sim_q2_server_binding_options;

/* The host's ctx is owned by the returned host: free it with _destroy. TS Errors raise. */
qa_app_q2_server_host qa_app_sim_create_q2_application_server_host(qa_app_sim_q2_server_binding_options options);
void qa_app_sim_q2_application_server_host_destroy(qa_app_q2_server_host host);

/* Shared helpers for the adapters (TS inline expressions used by several files). */
/* `contentDirectory.split('/').at(-1) ?? fallback` (borrowed tail of the text). */
const char *qa_app_sim_network_gamedir(const char *content_directory);
/* TS q2Userinfo(source) with its first-occurrence-wins Map, as an ordered pair list. */
typedef struct qa_app_sim_info_pair { char *key, *value; } qa_app_sim_info_pair;
typedef struct qa_app_sim_info { qa_app_sim_info_pair *pairs; size_t count, capacity; } qa_app_sim_info;
void qa_app_sim_info_parse_q2(qa_app_sim_info *info, const char *source);
const char *qa_app_sim_info_get(const qa_app_sim_info *info, const char *key);
void qa_app_sim_info_set(qa_app_sim_info *info, const char *key, const char *value);
void qa_app_sim_info_delete(qa_app_sim_info *info, const char *key);
void qa_app_sim_info_copy(qa_app_sim_info *out, const qa_app_sim_info *info);
/* `[...pairs].map(([k, v]) => `\\${k}\\${v}`).join('')`, malloc'd. */
char *qa_app_sim_info_text(const qa_app_sim_info *info);
void qa_app_sim_info_clear(qa_app_sim_info *info);
/* TS rerelease/KEX split-seat key folding: every key ending in `_${seat}` also sets its
 * unsuffixed name. */
void qa_app_sim_info_fold_seat(qa_app_sim_info *info, double seat);
/* TS addressKey(address) (port included), malloc'd. */
char *qa_app_sim_address_key(const qa_net_address *address);
#endif
