/*
 * Port of src/compat/qvm/mod-protection.ts. The TS class name
 * collides with the landed contract type qa_qvm_mod_protection (the
 * definition), so the runtime class is qa_qvm_mod_protection_runtime.
 * Mirrored surface (TS owner src/world/session/mods.ts):
 * qa_qvmy_protection_services carries the clients/actors/combat
 * members used; reservations are fn + ctx bundles. Bind payloads
 * are the landed qa_protection_binding (TS owner
 * src/world/gameplay/authority.ts, authority.h). create() fills a
 * shared component of channels.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_PROTECTION_H
#define QA_COMPAT_QVM_MOD_PROTECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/time.h"
#include "qa/world/gameplay/authority.h"

void qa_qvm_validate_mod_protection(const qa_qvm_mod_callback_declaration *declaration);

typedef struct qa_qvm_protection_input {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_qvm_protection_input;

typedef struct qa_qvm_mod_protection_runtime qa_qvm_mod_protection_runtime;

typedef qa_armor_stage_result (*qa_qvm_protection_absorb_fn)(void *ctx, qa_armor_stage_input input,
                                                             qa_protection_observer observer);
typedef qa_armor_stage_result (*qa_qvm_protection_execute_fn)(void *ctx);

/* Bind payloads are qa_protection_binding (authority.h). */

typedef struct qa_qvmy_protection_services {
  void *ctx;
  bool has_clients;
  size_t (*clients)(void *ctx, qa_actor_id *out, size_t capacity);
  bool (*for_actor)(void *ctx, qa_actor_id actor, qa_client_id *out);
  bool (*actor)(void *ctx, qa_client_id client, qa_actor_id *out);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  size_t (*reserve_protection)(void *ctx, qa_owned_actor owned, qa_protection_channel channel,
                               const qa_protection_binding *claim);
  void (*bind_reservation)(void *ctx, size_t reservation, const qa_protection_binding *binding);
  void (*close_reservation)(void *ctx, size_t reservation);
  qa_source_time (*time)(void *ctx);
} qa_qvmy_protection_services;

typedef struct qa_qvm_mod_protection_operations {
  void *ctx;
  void (*current)(void *ctx);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  int64_t (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  int32_t (*invoke)(void *ctx, const qa_qvm_mod_source_call *call,
                    const qa_qvm_protection_input *inputs, size_t input_count);
  /* Optional; fills borrowed rule pointers, returns count. */
  size_t (*pickups)(void *ctx, qa_actor_id actor, qa_protection_channel channel,
                    const qa_original_pickup_rule **out, size_t capacity);
} qa_qvm_mod_protection_operations;

typedef struct qa_qvm_mod_protection_component {
  qa_qvm_mod_protection_runtime **channels;
  size_t channel_count;
  void *stages; /* opaque */
  size_t stage_count;
  size_t stage_capacity;
  uint64_t stage_generation_counter;
} qa_qvm_mod_protection_component;

struct qa_qvm_mod_protection_runtime {
  const qa_qvm_mod_protection *definition;
  qa_qvm_memory *memory;
  const qa_qvmy_protection_services *services;
  qa_provider_id owner;
  const qa_qvm_mod_protection_operations *operations;
  qa_qvm_mod_protection_component *component;
  void *entries; /* opaque */
  size_t entry_count;
  size_t entry_capacity;
  bool active;
};

/* Fills component->channels with caller-owned runtime storage. */
void qa_qvm_mod_protection_create(qa_qvm_mod_protection_component *component,
                                  qa_qvm_mod_protection_runtime *storage,
                                  qa_qvm_mod_protection_runtime **channels,
                                  const qa_qvm_mod_protection *definitions, size_t definition_count,
                                  qa_qvm_memory *memory, const qa_qvmy_protection_services *services,
                                  qa_provider_id owner, const qa_qvm_mod_protection_operations *operations);
void qa_qvm_mod_protection_component_destroy(qa_qvm_mod_protection_component *component);
void qa_qvm_mod_protection_runtime_destroy(qa_qvm_mod_protection_runtime *protection);
bool qa_qvm_mod_protection_runtime_get_is_active(const qa_qvm_mod_protection_runtime *protection);
void qa_qvm_mod_protection_reserve(qa_qvm_mod_protection_runtime *protection);
void qa_qvm_mod_protection_reserve_actor(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor);
void qa_qvm_mod_protection_bind_actor(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor);
void qa_qvm_mod_protection_activate(qa_qvm_mod_protection_runtime *protection);
void qa_qvm_mod_protection_sync(qa_qvm_mod_protection_runtime *protection);
qa_armor_stage_result qa_qvm_mod_protection_observe(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                                                    qa_protection_observer observer,
                                                    qa_qvm_protection_execute_fn execute, void *execute_ctx);
void qa_qvm_mod_protection_assert_idle(qa_qvm_mod_protection_runtime *protection);
void qa_qvm_mod_protection_release(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor);
void qa_qvm_mod_protection_close(qa_qvm_mod_protection_runtime *protection);

#endif
