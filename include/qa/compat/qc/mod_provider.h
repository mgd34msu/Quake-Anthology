/* Port of src/compat/qc/mod-provider.ts. GPL-2.0-or-later.
 * validateQcMod and the full provider: actor projection, field
 * refresh/write-through, invoke, lifecycle, console commands, match
 * binding, checkpoint, owned actors, messages, presentation, spatial,
 * movement, pusher and aim builtins, combat, protection, pickups and
 * items. Engine members typed const void * in qa_session_mod_engine_
 * services are read through world/session/mod_engine.h. TS plain Error
 * throws abort via qa_range_error with the identical message, so the
 * TS AggregateError collection in close paths never runs. */
#ifndef QA_COMPAT_QC_MOD_PROVIDER_H
#define QA_COMPAT_QC_MOD_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/actor_state.h"
#include "qa/compat/qc/builtins.h"
#include "qa/compat/qc/client_host.h"
#include "qa/compat/qc/executor.h"
#include "qa/compat/qc/mod_actors.h"
#include "qa/compat/qc/mod_clients.h"
#include "qa/compat/qc/mod_combat.h"
#include "qa/compat/qc/mod_environment.h"
#include "qa/compat/qc/mod_input.h"
#include "qa/compat/qc/mod_items.h"
#include "qa/compat/qc/mod_messages.h"
#include "qa/compat/qc/mod_pickups.h"
#include "qa/compat/qc/mod_protection.h"
#include "qa/compat/qc/movement_host.h"
#include "qa/compat/qc/presentation_host.h"
#include "qa/compat/qc/pusher_host.h"
#include "qa/compat/qc/source_call.h"
#include "qa/compat/qc/spatial_host.h"
#include "qa/compat/qc/world_host.h"
#include "qa/content/q1/quakec/weapon_stage.h"
#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/core/commands/index.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_objectives.h"
#include "qa/world/session/mods.h"

/* TS validateQcMod. Aborts with the TS Error message on failure. */
void qa_compat_qc_validate_mod(qa_compat_qc_program *program,
                               qa_mod_callback_declaration declaration);

/* TS QcModRandom extends RandomSource with restore(). restore
 * receives source.ctx (TS `this`). */
typedef struct qa_compat_qc_mod_random {
  qa_random_source source;
  void (*restore)(void *ctx, qa_random_state state);
} qa_compat_qc_mod_random;

/* TS QcModMedia: resources map becomes a borrowed name array.
 * model_bounds is valid only when has_bounds (TS Bounds | null). */
typedef struct qa_compat_qc_mod_media_resource {
  const char *name;
  qa_resolved_resource_reference resource;
  bool has_bounds;
  qa_bounds model_bounds;
} qa_compat_qc_mod_media_resource;
typedef struct qa_compat_qc_mod_media {
  qa_content_id content;
  const qa_compat_qc_mod_media_resource *resources;
  size_t resource_count;
} qa_compat_qc_mod_media;

/* TS FieldBinding. */
typedef struct qa_compat_qc_mod_provider_field {
  qa_mod_actor_field declaration;
  int64_t offset;
  int64_t words; /* 1 | 3 */
} qa_compat_qc_mod_provider_field;

typedef struct qa_compat_qc_mod_provider qa_compat_qc_mod_provider;
struct qa_compat_qc_mod_provider {
  qa_compat_qc_program *program;
  qa_module_identity module;
  qa_mod_callback_declaration declaration;
  qa_mod_host_services *services;
  qa_compat_qc_mod_random *random;
  const qa_compat_qc_mod_media *media; /* NULL = TS undefined */
  qa_compat_qc_machine machine;
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_mod_environment environment;
  qa_compat_qc_builtin_registry registry;
  const qa_numeric_operations *numeric;
  qa_compat_qc_mod_provider_field *fields;
  size_t field_count;
  qa_compat_qc_mod_input_field *input_fields;
  size_t input_field_count;
  qa_compat_qc_mod_input input;
  qa_source_mod_client_outputs *client_outputs;
  qa_compat_qc_mod_client_bindings clients;
  bool has_clients;
  qa_mod_source_objectives *objectives;
  struct { qa_actor_id actor; int64_t slot; } *projections;
  size_t projection_count, projection_capacity;
  /* TS freeSlots: released foreign slots, oldest first. A slot is reused only after others are released behind it,
   * so stale references see it freed for a while (FOREIGN_SLOT_QUARANTINE). */
  int64_t *free_slots;
  size_t free_slot_count, free_slot_capacity;
  qa_actor_id *retired;
  size_t retired_count, retired_capacity;
  struct { char *key; qa_compat_qc_precached_resource value; } *precached;
  size_t precached_count, precached_capacity;
  /* TS pickupScopes: pushed by the pickups watch closure. */
  struct {
    qa_actor_id actor;
    qa_original_pickup_execution execution;
    int64_t depth;
  } *pickup_scopes;
  size_t pickup_scope_count, pickup_scope_capacity;
  bool has_frame;
  qa_frame_context frame;
  int64_t depth;
  bool loading, initialized, closed;
  qa_mod_command_port *commands; /* NULL = unbound */
  int64_t presentation_generation;
  int64_t release_subscription;
  bool has_release_subscription;
  qa_source_match_unbind release_match;
  bool has_release_match;
  struct qa_compat_qc_match_player_cell *match_cells;
  size_t match_cell_count, match_cell_capacity;
  qa_compat_qc_executor_host host_state;
  qa_guest_checkpoint checkpoint_storage;
  qa_compat_qc_machine_snapshot snapshot;
  uint8_t *host_state_bytes;
  bool has_checkpoint;
  qa_random_state host_random_state;
  char *host_encoded; /* scratch owned by host checkpoint */
  size_t host_encoded_length;
  char *objective_location; /* scratch, compared by value */
  void **owned_cells; size_t owned_cell_count, owned_cell_capacity;
  /* TS host Map: later sets replace earlier names. */
  qa_compat_qc_host_binding host_table[128];
  size_t host_count;
  qa_compat_qc_mod_environment_clients engine_clients;
  /* TS QcMachine view consumed by content/q1/quakec boundaries. */
  qa_q1qc_program qprogram;
  qa_q1qc_definition *qglobals, *qfields;
  qa_q1qc_function *qfunctions;
  qa_q1qc_statement *qstatements;
  qa_q1qc_entity_memory qentities;
  qa_q1qc_machine qmachine;
  /* Machine boundaries. */
  qa_compat_qc_function_boundary function_boundary;
  int64_t *boundary_functions;
  qa_compat_qc_inline_boundary inline_boundary;
  qa_compat_qc_inline_region *inline_regions;
  qa_q1qc_inline_boundary armor_regions;
  qa_q1qc_inline_region *armor_region_list;
  size_t armor_region_count;
  qa_q1qc_function_boundary weapon_inner;
  qa_q1qc_weapon_function_composed weapon_functions;
  qa_q1qc_weapon_region_composed weapon_regions;
  bool has_weapon_compose;
  /* Subsystems (TS nullable members). */
  qa_compat_qc_mod_items items; bool has_items;
  qa_compat_qc_mod_protection protection; bool has_protection;
  qa_compat_qc_mod_pickups pickups; bool has_pickups;
  qa_compat_qc_mod_combat combat; bool has_combat;
  qa_compat_qc_mod_actors owned_actors; bool has_owned_actors;
  qa_compat_qc_actor_state actor_state;
  qa_compat_qc_body_binding_state *body_states; /* per slot */
  /* Messages and presentation. */
  qa_compat_qc_mod_messages messages; bool has_messages;
  qa_app_sim_quakec_local_messages *local_messages;
  qa_compat_qc_local_message_app_adapter local_adapter;
  qa_compat_qc_presentation_world presentation_world;
  qa_compat_qc_presentation_state presentation_state;
  struct { struct qa_compat_qc_mod_provider *p; bool has_target; qa_actor_id target; bool has_fixed; qa_actor_id fixed; } message_host;
  qa_actor_id *players_scratch; size_t players_capacity;
  /* Spatial, world, movement, aim, pusher. */
  qa_compat_qc_spatial_world spatial_world;
  qa_compat_qc_world_host world_host; bool has_world_host;
  qa_compat_qc_movement_state_compat movement_state;
  qa_compat_qc_movement_bindings movement_bindings; bool has_movement;
  struct { struct qa_compat_qc_mod_provider *p; const char *name; } movement_wraps[4];
  qa_compat_qc_aim_state aim_state;
  qa_compat_qc_builtin aim_builtin;
  qa_compat_qc_aim_target_ref *aim_targets; size_t aim_target_capacity;
  qa_compat_qc_pusher_state pusher_state;
  qa_q1_pusher_services pusher_services; bool has_pusher;
  qa_app_sim_q1_pusher_env pusher_env;
  /* TS sourcePhysics: per-actor unbind handles and their closure cells. */
  struct qa_compat_qc_source_physics_cell **source_physics;
  size_t source_physics_count, source_physics_capacity;
  /* TS presentations() and clientPresentation(). */
  qa_app_simulation_presentation *presentation_payloads;
  size_t presentation_capacity;
  qa_mod_client_presentation_source client_source;
  bool has_client_source;
};

/* TS constructor. env_clients NULL reads engine.clients from the
 * engine services. extra_host entries are set last into the host
 * table (later names win) and carry test or embedder builtins with no
 * TS engine service. False only on allocation failure; invalid
 * declarations abort. */
bool qa_compat_qc_mod_provider_init(
    qa_compat_qc_mod_provider *p, qa_compat_qc_program *program,
    qa_module_identity module, qa_mod_callback_declaration declaration,
    qa_mod_host_services *services, qa_compat_qc_mod_random *random,
    const qa_compat_qc_mod_media *media,
    const qa_compat_qc_mod_environment_clients *env_clients,
    const qa_compat_qc_host_binding *extra_host, size_t extra_host_count);
void qa_compat_qc_mod_provider_destroy(qa_compat_qc_mod_provider *p);

/* TS get cvars. */
qa_cvar_registry *qa_compat_qc_mod_provider_get_cvars(
    qa_compat_qc_mod_provider *p);
/* TS private reference()/actor(): exposed for caller-built host
 * wiring (the TS closures). actor NULL yields reference 0. */
int64_t qa_compat_qc_mod_provider_reference(qa_compat_qc_mod_provider *p,
                                            const qa_actor_id *actor);
qa_actor_id qa_compat_qc_mod_provider_actor(qa_compat_qc_mod_provider *p,
                                            int64_t reference);
/* TS private lookup(): kind "model"|"sound"; false is TS null. */
bool qa_compat_qc_mod_provider_lookup(qa_compat_qc_mod_provider *p,
                                      const char *kind, const char *name,
                                      qa_compat_qc_precached_resource *out);
/* TS invoke(). region NULL is the TS undefined (plain execute). */
double qa_compat_qc_mod_provider_invoke(
    qa_compat_qc_mod_provider *p, qa_mod_source_call call,
    const qa_compat_qc_mod_input_entry *inputs, size_t input_count,
    const qa_q1qc_armor_stage *region);
/* TS bindCommands()/consoleCommand(). */
void qa_compat_qc_mod_provider_bind_commands(qa_compat_qc_mod_provider *p,
                                             qa_mod_command_port *commands);
bool qa_compat_qc_mod_provider_console_command(
    qa_compat_qc_mod_provider *p, qa_command_invocation *invocation);
/* TS activateMatch()/initialize()/advance(). */
void qa_compat_qc_mod_provider_activate_match(qa_compat_qc_mod_provider *p);
void qa_compat_qc_mod_provider_initialize(qa_compat_qc_mod_provider *p);
void qa_compat_qc_mod_provider_advance(qa_compat_qc_mod_provider *p,
                                       qa_frame_context frame);
/* TS checkpoint()/restore(). The returned checkpoint borrows
 * provider storage until the next checkpoint/destroy. */
const qa_guest_checkpoint *qa_compat_qc_mod_provider_checkpoint(
    qa_compat_qc_mod_provider *p);
void qa_compat_qc_mod_provider_restore(qa_compat_qc_mod_provider *p,
                                       const qa_guest_checkpoint *saved);
/* TS clientPresentation(): source generation is refreshed on read. */
qa_mod_client_presentation_source qa_compat_qc_mod_provider_client_presentation(
    qa_compat_qc_mod_provider *p);
/* TS presentations(): borrowed until the next call; each entry's
 * opaque is a const qa_app_simulation_presentation *. */
const qa_app_simulation_presentation *qa_compat_qc_mod_provider_presentations(
    qa_compat_qc_mod_provider *p, size_t *out_count);
/* TS close(). */
void qa_compat_qc_mod_provider_close(qa_compat_qc_mod_provider *p);
#endif
