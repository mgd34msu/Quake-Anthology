/* Port of src/compat/qc/mod-actors.ts. GPL-2.0-or-later.
 * think/nextthink -1 = TS null. first_dynamic_slot 0 = TS default 1
 * (0 is invalid as a slot here). Owns slots+scheduler. */
#ifndef QA_COMPAT_QC_MOD_ACTORS_H
#define QA_COMPAT_QC_MOD_ACTORS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/entity_host.h"
#include "qa/world/actors/body.h"
#include "qa/world/scheduler.h"
#include "qa/world/session/mods.h"
typedef struct qa_compat_qc_mod_actor_options {
  qa_compat_qc_machine *machine; qa_provider_id provider;
  qa_mod_host_services *services;
  int64_t think, nextthink; /* -1 = null */
  int64_t first_dynamic_slot; /* 0 = default 1 */
  void *ctx;
  bool (*client_frame)(void *ctx, int64_t slot, qa_frame_context frame); /* nullable */
  qa_body_state_binding (*body)(void *ctx, int64_t slot);
  void (*admitted)(void *ctx, qa_owned_actor actor, int64_t slot);
  void (*retired)(void *ctx, qa_owned_actor actor);
  void (*step)(void *ctx, qa_owned_actor actor, qa_frame_context frame, void *think_ctx, void (*think)(void *think_ctx));
  void (*touch)(void *ctx, qa_owned_actor actor, qa_actor_id other);
  void (*use)(void *ctx, qa_owned_actor actor, const qa_actor_id *other, const qa_actor_id *activator);
  void (*pain)(void *ctx, qa_pain_reaction reaction);
  void (*die)(void *ctx, qa_death_reaction reaction);
  void (*remember)(void *ctx, qa_actor_id actor, int64_t slot);
  void (*invoke)(void *ctx, qa_owned_actor actor, int64_t function_index, qa_frame_context frame);
} qa_compat_qc_mod_actor_options;
typedef struct qa_compat_qc_mod_actors {
  qa_compat_qc_mod_actor_options options;
  qa_source_actor_slots *slots; qa_source_actor_slots_options slots_options;
  qa_compat_qc_source_slot_storage_state storage_state;
  qa_source_slot_storage storage_base;
  qa_compat_qc_actor_bindings_state bindings_state;
  qa_compat_qc_builtin spawn, remove;
  qa_frame_scheduler *scheduler;
  qa_scheduler_clock clock_entry;
  qa_owned_actor *owned_actors; int64_t *owned_slots; size_t owned_count, owned_capacity;
  int64_t release_id; bool closed;
  struct qa_compat_qc_mod_actors *self_ref;
} qa_compat_qc_mod_actors;
bool qa_compat_qc_mod_actors_init(qa_compat_qc_mod_actors *a, qa_compat_qc_mod_actor_options options);
void qa_compat_qc_mod_actors_destroy(qa_compat_qc_mod_actors *a);
void qa_compat_qc_mod_actors_admit(qa_compat_qc_mod_actors *a, qa_owned_actor actor, int64_t slot);
void qa_compat_qc_mod_actors_schedule(qa_compat_qc_mod_actors *a, qa_owned_actor actor);
void qa_compat_qc_mod_actors_advance(qa_compat_qc_mod_actors *a, qa_frame_context frame);
void qa_compat_qc_mod_actors_restored(qa_compat_qc_mod_actors *a);
void qa_compat_qc_mod_actors_close(qa_compat_qc_mod_actors *a);
#endif
