/* Port of src/compat/qvm/mod-provider.ts. GPL-2.0-or-later.
 * validateQvmMod / validateQvmModCheckpoint and the provider core
 * (actor projection, field refresh/write-through, begin/invoke,
 * pickup observation, objectives, client outputs, cvars and
 * configstrings, presentation publication, lifecycle hooks,
 * engine/spatial syscalls, checkpoint/restore) are implemented
 * against the landed headers. TS plain Error throws abort via
 * qa_range_error with the identical message, so TS catch/finally
 * cleanup that depends on recoverable throws cannot run.
 * Accommodations:
 *  - QvmModule is built from caller-supplied interpreter/guest
 *    memory (module.h accommodation); async initialize/readScript
 *    are synchronous. readScript decodes through the caller mounts
 *    synchronously when an open result is already materialized.
 *  - Engine scene/physics/events are opaque in the landed
 *    ModHostServices shape (world/session/mods.h): Q3GuestWorld
 *    portals, physics bindSource/setCollision, scene trace/contact/
 *    contents/portal queries and Q3SourceEvent emission cannot be
 *    constructed here. Their TS "service missing" errors are
 *    reproduced when the service is absent; when present-but-opaque
 *    the same paths remain unresolved (see the .c notes).
 *  - presentations() fills the landed qa_session_simulation_
 *    presentation (actor/viewWeapon only); the TS payload fields
 *    have no landed representation and stay unresolved.
 *  - close() aggregates TS cleanup errors; C cleanup failures abort
 *    at their source, so the AggregateError path is unreachable. */
#ifndef QA_COMPAT_QVM_MOD_PROVIDER_H
#define QA_COMPAT_QVM_MOD_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qvm/entity_tokens.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/mod_actor_frame.h"
#include "qa/compat/qvm/mod_actors.h"
#include "qa/compat/qvm/mod_clients.h"
#include "qa/compat/qvm/mod_input.h"
#include "qa/compat/qvm/mod_items.h"
#include "qa/compat/qvm/mod_pickups.h"
#include "qa/compat/qvm/mod_player_events.h"
#include "qa/compat/qvm/mod_protection.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/server_info_syscalls.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/time.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/network/q3/game_state.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_client_input_values.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_objectives.h"
#include "qa/world/session/mod_presentations.h"
#include "qa/world/session/mods.h"

/* Provider presentation context returned through the opaque
 * qa_session_qvm_presentation_context slot of
 * qa_mod_qvm_presentation_source (TS QvmPresentationContext minus
 * frameTimeMilliseconds/viewOrigin). */
typedef struct qa_qvm_mod_provider_presentation_context {
  int64_t client_number;
  bool weapon_presented;
  const qa_q3_source_game_state_record *game_state;
  int64_t game_state_revision;
  int64_t server_time;
  qa_q3_player_state player_state;
} qa_qvm_mod_provider_presentation_context;

/* TS validateQvmMod. Aborts with the TS Error message on failure. */
void qa_qvm_validate_mod(const qa_qvmy_artifact *artifact,
                         const qa_qvm_mod_callback_declaration *declaration);
/* TS validateQvmModCheckpoint (QvmCheckpoint is the qvm kind of
 * qa_guest_checkpoint). */
void qa_qvm_validate_mod_checkpoint(
    const qa_qvmy_artifact *artifact,
    const qa_qvm_mod_callback_declaration *declaration,
    const qa_guest_checkpoint *checkpoint);

/* One invoke() input (TS ReadonlyMap<ModCallbackInput,
 * ModRuntimeValue>), shared shape with mod-actors. */
typedef qa_qvm_mod_actor_input qa_qvm_mod_provider_input;

/* TS assertCurrent closure. */
typedef void (*qa_qvm_mod_provider_current_fn)(void *ctx);

typedef struct qa_qvm_mod_provider qa_qvm_mod_provider;
struct qa_qvm_mod_provider {
  const qa_qvmy_artifact *artifact;
  const qa_qvm_mod_callback_declaration *declaration;
  qa_mod_host_services *services;
  qa_qvm_mod_provider_current_fn assert_current;
  void *assert_current_ctx;
  qa_content_id content;
  qa_content_mounted *mounts;      /* NULL = TS undefined */
  qa_user_file_store *writable;    /* NULL = TS null */
  qa_qvm_module module;
  bool module_live;
  qa_mod_source_objectives *objectives;
  qa_source_mod_client_outputs *client_outputs;
  qa_cvar_registry *cvars;
  qa_qvm_entity_tokens entity_tokens;
  qa_qvm_server_information_services information;
  qa_qvm_files *files;             /* NULL when no mounts */
  qa_qvm_mod_player_events player_events;
  bool has_player_events;
  qa_qvm_mod_actors actor_semantics;
  bool has_actor_semantics;
  qa_qvm_mod_input input;
  qa_qvm_mod_client_bindings client_bindings;
  bool has_client_bindings;
  qa_qvm_mod_protection_component protection_component;
  qa_qvm_mod_protection_runtime *protection_storage;
  qa_qvm_mod_protection_runtime **protection_channels;
  size_t protection_count;
  qa_qvmy_protection_services protection_services;
  qa_qvmy_pickup_services pickup_services;
  qa_qvm_mod_pickups pickups;
  qa_qvmy_item_services item_services;
  qa_qvm_mod_items_runtime items;
  bool has_items;
  qa_qvm_mod_actor_frame_runtime actor_frame;
  bool has_actor_frame;
  qa_save_value *saved_items;      /* owned; NULL = TS undefined */
  struct { qa_actor_id actor; int64_t slot; } *projections;
  size_t projection_count, projection_capacity;
  qa_actor_id *retired;
  size_t retired_count, retired_capacity;
  struct { qa_actor_id id; qa_owned_actor actor; } *owned;
  size_t owned_count, owned_capacity;
  qa_actor_id *removing;
  size_t removing_count, removing_capacity;
  struct { qa_actor_id actor; char *key; } *event_keys;
  size_t event_key_count, event_key_capacity;
  struct { const char *id; uint8_t *bytes; size_t length; } *defaults;
  size_t default_count;
  struct { int64_t index; char *value; } *configstrings;
  size_t configstring_count, configstring_capacity;
  int64_t presentation_revision, presentation_generation;
  bool has_presentation_state;
  qa_q3_source_game_state_record presentation_state;
  void *scene_state;    /* owned checkpoint-form publication or NULL */
  void *scene_baseline; /* owned checkpoint-form publication or NULL */
  qa_qvm_mod_scene_publication scene_view;
  qa_qvm_scene_client *scene_view_clients;
  qa_qvm_scene_entity *scene_view_entities;
  qa_qvm_scene_command *scene_view_commands;
  size_t scene_view_client_capacity, scene_view_entity_capacity,
      scene_view_command_capacity;
  qa_qvm_mod_scene_publication scene_baseline_view;
  qa_qvm_scene_client *scene_baseline_clients;
  qa_qvm_scene_entity *scene_baseline_entities;
  qa_qvm_scene_command *scene_baseline_commands;
  size_t scene_baseline_client_capacity, scene_baseline_entity_capacity,
      scene_baseline_command_capacity;
  struct qa_qvm_mod_provider_presentation_context *presentation_context;
  bool scene_dirty;
  int64_t scene_revision, scene_command_sequence;
  /* BoundedQueue(64) of owned scene command cells (element size is
   * the cell struct in the .c file). */
  qa_bounded_queue scene_commands;
  void *frames; /* opaque Frame stack (observations + pending) */
  size_t frame_count, frame_capacity;
  void *pickup_scopes; /* opaque */
  size_t pickup_scope_count, pickup_scope_capacity;
  void *weapon_inputs; /* opaque */
  size_t weapon_input_count, weapon_input_capacity;
  int64_t projection_writes, pickup_contexts;
  int64_t scratch_start, scratch, next_slot;
  qa_mod_command_port *commands; /* NULL = unbound */
  bool closed;
  qa_source_match_unbind release_match;
  bool has_release_match;
  void *match_cells; /* opaque per-actor match player cells */
  size_t match_cell_count, match_cell_capacity;
  int64_t release_subscription;
  bool has_release_subscription;
  size_t *hooks; /* module bindFunction removal ids */
  size_t hook_count, hook_capacity;
  void *reservations; /* opaque protection reservation cells */
  size_t reservation_count, reservation_capacity;
  void *pickup_bindings; /* opaque inventory pickup binding cells */
  size_t pickup_binding_count, pickup_binding_capacity;
  qa_mod_qvm_presentation_source presentation_source;
  qa_qvm_presentation_binding *bindings_scratch;
  size_t bindings_scratch_capacity;
  qa_session_simulation_presentation *presentations_scratch;
  size_t presentations_scratch_capacity;
  char *objective_location; /* scratch, compared by value */
  char *configstring_scratch;
  qa_qvm_host_state host_state;
  qa_guest_checkpoint checkpoint_storage;
  bool has_checkpoint;
  uint8_t *host_state_bytes;
  size_t host_state_length;
  qa_qvm_mod_client_slot *client_scratch;
  size_t client_scratch_capacity;
};

/* TS constructor. interpreter/guest_memory build the QvmModule
 * (module.h accommodation). False only on allocation failure;
 * invalid declarations abort. */
bool qa_qvm_mod_provider_init(qa_qvm_mod_provider *p,
                              const qa_qvmy_artifact *artifact,
                              const qa_qvm_mod_callback_declaration *declaration,
                              qa_mod_host_services *services,
                              qa_qvm_mod_provider_current_fn assert_current,
                              void *assert_current_ctx, qa_content_id content,
                              qa_content_mounted *mounts,
                              qa_user_file_store *writable,
                              qa_qvmy_interpreter_runtime *interpreter,
                              const qa_guest_memory *guest_memory);
void qa_qvm_mod_provider_destroy(qa_qvm_mod_provider *p);

/* TS invoke(): float32 returns widen to double (TS number). */
double qa_qvm_mod_provider_invoke(qa_qvm_mod_provider *p,
                                  const qa_qvm_mod_source_call *call,
                                  const qa_qvm_mod_provider_input *inputs,
                                  size_t input_count);
/* TS bindCommands()/consoleCommand(). */
void qa_qvm_mod_provider_bind_commands(qa_qvm_mod_provider *p,
                                       qa_mod_command_port *commands);
bool qa_qvm_mod_provider_console_command(qa_qvm_mod_provider *p,
                                         qa_command_invocation *command);
/* TS readScript: synchronous; malloc'd text or NULL (TS undefined).
 * Only materialized mount resources resolve (see header note). */
char *qa_qvm_mod_provider_read_script(qa_qvm_mod_provider *p, const char *name);
/* TS initialize()/reserveProtection()/activateProtection(). */
void qa_qvm_mod_provider_initialize(qa_qvm_mod_provider *p);
void qa_qvm_mod_provider_reserve_protection(qa_qvm_mod_provider *p);
void qa_qvm_mod_provider_activate_protection(qa_qvm_mod_provider *p);
/* TS advance(): returns undefined. */
void qa_qvm_mod_provider_advance(qa_qvm_mod_provider *p, qa_frame_context frame);
/* Borrowed scratch (see header note on the landed shape). */
const qa_session_simulation_presentation *qa_qvm_mod_provider_presentations(
    qa_qvm_mod_provider *p, size_t *out_count);
/* TS checkpoint()/restore(). The returned checkpoint borrows
 * provider storage until the next checkpoint/destroy. */
const qa_guest_checkpoint *qa_qvm_mod_provider_checkpoint(qa_qvm_mod_provider *p);
void qa_qvm_mod_provider_restore(qa_qvm_mod_provider *p,
                                 const qa_guest_checkpoint *checkpoint);
/* TS close(). */
void qa_qvm_mod_provider_close(qa_qvm_mod_provider *p);
#endif
