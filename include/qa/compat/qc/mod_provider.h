/* Port of src/compat/qc/mod-provider.ts. GPL-2.0-or-later.
 * validateQcMod and the provider core (actor projection, field
 * refresh/write-through, invoke, lifecycle, console commands,
 * match binding, checkpoint) are implemented against the landed
 * headers. Subsystem wiring whose TS inputs are opaque in the
 * landed ModHostServices shape (engine scene/physics/events are
 * const void * in world/session/mods.h) cannot be constructed
 * here: spatial/movement/presentation/pusher/aim builtins, the
 * owned-actor/message/combat/pickup/item subsystems, and client
 * presentation frames are supplied by the caller through
 * extra_host bindings and remain unresolved (see the .c header
 * note). TS plain Error throws abort via qa_range_error with the
 * identical message. */
#ifndef QA_COMPAT_QC_MOD_PROVIDER_H
#define QA_COMPAT_QC_MOD_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/builtins.h"
#include "qa/compat/qc/executor.h"
#include "qa/compat/qc/mod_clients.h"
#include "qa/compat/qc/mod_environment.h"
#include "qa/compat/qc/mod_input.h"
#include "qa/compat/qc/presentation_host.h"
#include "qa/compat/qc/source_call.h"
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
  qa_actor_id *retired;
  size_t retired_count, retired_capacity;
  struct { char *key; qa_compat_qc_precached_resource value; } *precached;
  size_t precached_count, precached_capacity;
  /* Pickup scopes are pushed only by the (unresolved) pickups
   * subsystem; the stack exists so the store observers keep the
   * TS shape. */
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
};

/* TS constructor (minus the unresolved subsystem wiring above).
 * env_clients may be NULL (no destination visibility scene).
 * extra_host carries caller-built host builtins (spawn/remove,
 * spatial, movement, presentation, ...); environment host entries
 * are installed first, later entries win (TS Map set order for the
 * environment vs the omitted host literals is the caller's).
 * False only on allocation failure; invalid declarations abort. */
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
/* TS activateMatch()/initialize()/advance(). advance returns after
 * the frame call; the owned-actor advance is unresolved wiring. */
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
/* TS close(). */
void qa_compat_qc_mod_provider_close(qa_compat_qc_mod_provider *p);
#endif
