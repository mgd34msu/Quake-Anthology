/* Port of src/compat/qc/world-host.ts. GPL-2.0-or-later.
 * slots_options must match slots creation (TS slots.options).
 * Body-binding storage is allocated per slot by init (capacity). */
#ifndef QA_COMPAT_QC_WORLD_HOST_H
#define QA_COMPAT_QC_WORLD_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/actor_state.h"
#include "qa/compat/qc/entity_host.h"
#include "qa/compat/qc/spatial_host.h"
typedef struct qa_compat_qc_world_host_options {
  qa_compat_qc_program *program; qa_compat_qc_entity_memory *entities;
  qa_session_actor_registry *actors; qa_source_actor_slots *slots; qa_source_actor_slots_options slots_options;
  qa_shared_body_table *bodies; qa_scene_queries scene; qa_numeric_profile numeric;
  void *ctx;
  bool (*model)(void *ctx, const char *name, qa_compat_qc_spatial_model *out);
  bool (*foreign_actor)(void *ctx, int64_t slot, qa_owned_actor *out); /* nullable */
  int64_t (*foreign_reference)(void *ctx, qa_actor_id actor);
  void (*admit)(void *ctx, qa_owned_actor actor, int64_t slot); /* nullable */
  /* nullable: replaces world.actor(slot) for hosts whose edicts project
   * actors the source slots do not own (TS QcModProvider world pick). */
  qa_owned_actor (*actor_at)(void *ctx, int64_t slot);
} qa_compat_qc_world_host_options;
typedef struct qa_compat_qc_world_host {
  qa_compat_qc_world_host_options options;
  qa_compat_qc_actor_bindings_state bindings_state;
  qa_compat_qc_spatial_world spatial;
  qa_compat_qc_host_binding host_entries[16]; size_t host_count;
  qa_compat_qc_body_binding_state *body_storage; /* owned, per slot */
  void *wrap_storage; /* owned builtin wrap state */
} qa_compat_qc_world_host;
qa_bounds qa_compat_qc_link_bounds(qa_body_state state, double flags, const qa_numeric_operations *numeric);
bool qa_compat_qc_world_host_init(qa_compat_qc_world_host *h, qa_compat_qc_world_host_options options);
void qa_compat_qc_world_host_destroy(qa_compat_qc_world_host *h);
bool qa_compat_qc_world_host_is_free_entity(qa_compat_qc_world_host *h, int64_t slot);
int64_t qa_compat_qc_world_host_reference(qa_compat_qc_world_host *h, qa_actor_id actor);
/* TS prepareEntities: findradius walks QC rows, so foreign actors need their borrowed rows before it runs. */
void qa_compat_qc_world_host_prepare_entities(qa_compat_qc_world_host *h);
qa_owned_actor qa_compat_qc_world_host_actor(qa_compat_qc_world_host *h, int64_t slot);
void qa_compat_qc_world_host_link(qa_compat_qc_world_host *h, int64_t slot);
#endif
