/* Port of src/compat/qc/entity-host.ts. GPL-2.0-or-later.
 * Slot-storage state is caller-owned storage (ctx). Actor bindings
 * host entries are caller-owned arrays; their builtin ctx points at
 * caller-owned qa_compat_qc_actor_bindings_state. slots_options must
 * be the options the slots were created with (TS slots.options). */
#ifndef QA_COMPAT_QC_ENTITY_HOST_H
#define QA_COMPAT_QC_ENTITY_HOST_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/compat/qc/builtins.h"
#include "qa/world/actors/registry.h"
#include "qa/world/actors/source_slots.h"
typedef struct qa_compat_qc_edict_metadata_layout { int64_t free_offset_bytes, free_time_offset_bytes; } qa_compat_qc_edict_metadata_layout;
typedef struct qa_compat_qc_source_slot_storage_state {
  qa_compat_qc_program *program; qa_compat_qc_entity_memory *entities;
  qa_compat_qc_edict_metadata_layout metadata;
  int64_t cleared[7], origin, angles, nextthink;
} qa_compat_qc_source_slot_storage_state;
qa_source_slot_storage qa_compat_qc_create_source_slot_storage(qa_compat_qc_program *program, qa_compat_qc_entity_memory *entities, qa_compat_qc_edict_metadata_layout metadata, qa_compat_qc_source_slot_storage_state *storage);
typedef struct qa_compat_qc_actor_bindings_state {
  qa_compat_qc_entity_memory *entities; qa_session_actor_registry *actors;
  qa_source_actor_slots *slots; qa_source_actor_slots_options slots_options;
} qa_compat_qc_actor_bindings_state;
typedef struct qa_compat_qc_actor_bindings {
  qa_compat_qc_host_binding host_entries[2]; size_t host_count;
  qa_compat_qc_actor_bindings_state *state;
} qa_compat_qc_actor_bindings;
qa_compat_qc_actor_bindings qa_compat_qc_create_actor_bindings(qa_compat_qc_entity_memory *entities, qa_session_actor_registry *actors, qa_source_actor_slots *slots, qa_source_actor_slots_options slots_options, qa_compat_qc_actor_bindings_state *storage);
bool qa_compat_qc_actor_bindings_is_free_entity(qa_compat_qc_actor_bindings_state *state, int64_t slot);
#endif
