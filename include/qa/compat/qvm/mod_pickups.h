/*
 * Port of src/compat/qvm/mod-pickups.ts. Mirrored surface (TS owner
 * src/world/session/mods.ts): qa_qvmy_pickup_services carries only
 * the ModHostServices members this module calls (clients list,
 * resolveOwned, isLive, inventory.bindPickup) as fn + ctx. Inputs
 * maps become a fixed entry array. Per-actor rules are built into
 * caller-visible rule storage owned by the pickups object.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_PICKUPS_H
#define QA_COMPAT_QVM_MOD_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/qvm_mod_items.h"

void qa_qvm_validate_mod_pickups(const qa_qvm_mod_callback_declaration *declaration);

/* One ModCallbackInput -> ModRuntimeValue entry. */
typedef struct qa_qvm_pickup_input {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_qvm_pickup_input;

typedef struct qa_qvmy_pickup_services {
  void *ctx;
  bool has_clients;
  size_t (*clients)(void *ctx, qa_actor_id *out, size_t capacity); /* returns count */
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  /* Returns a removal id removed through unbind_pickup. */
  size_t (*bind_pickup)(void *ctx, qa_owned_actor owned, qa_provider_id owner,
                        const qa_original_pickup_rule *rules, size_t rule_count);
  void (*unbind_pickup)(void *ctx, size_t id);
} qa_qvmy_pickup_services;

typedef qa_original_pickup_decision (*qa_qvm_pickup_execute_fn)(void *ctx);

typedef struct qa_qvm_mod_pickup_operations {
  void *ctx;
  void (*current)(void *ctx);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  qa_original_pickup_decision (*context)(void *ctx, const qa_qvm_mod_pickup *definition,
                                         const qa_original_pickup_offer *offer,
                                         const qa_qvm_pickup_input *inputs, size_t input_count,
                                         qa_qvm_pickup_execute_fn execute, void *execute_ctx);
  qa_original_pickup_decision (*observe)(void *ctx, qa_actor_id actor,
                                         const qa_original_pickup_execution *observer,
                                         qa_qvm_pickup_execute_fn execute, void *execute_ctx);
  int32_t (*invoke)(void *ctx, const qa_qvm_mod_source_call *call,
                    const qa_qvm_pickup_input *inputs, size_t input_count);
} qa_qvm_mod_pickup_operations;

typedef struct qa_qvm_mod_pickups {
  const qa_qvm_mod_pickup *definitions;
  size_t definition_count;
  const qa_qvmy_pickup_services *services;
  qa_provider_id owner;
  const qa_qvm_mod_pickup_operations *operations;
  bool active;
  int64_t depth;
  void *entries; /* opaque per-actor rules/delegates */
  size_t entry_count;
  size_t entry_capacity;
} qa_qvm_mod_pickups;

void qa_qvm_mod_pickups_init(qa_qvm_mod_pickups *pickups, const qa_qvm_mod_pickup *definitions,
                             size_t definition_count, const qa_qvmy_pickup_services *services,
                             qa_provider_id owner, const qa_qvm_mod_pickup_operations *operations);
void qa_qvm_mod_pickups_destroy(qa_qvm_mod_pickups *pickups);
bool qa_qvm_mod_pickups_get_is_active(const qa_qvm_mod_pickups *pickups);
/* Returns the actor's built rules (owned); protection filters into out. */
size_t qa_qvm_mod_pickups_protection(qa_qvm_mod_pickups *pickups, qa_actor_id actor,
                                     qa_protection_channel channel,
                                     const qa_original_pickup_rule **out, size_t capacity);
void qa_qvm_mod_pickups_activate(qa_qvm_mod_pickups *pickups);
void qa_qvm_mod_pickups_bind_actor(qa_qvm_mod_pickups *pickups, qa_actor_id actor);
void qa_qvm_mod_pickups_assert_idle(const qa_qvm_mod_pickups *pickups);
void qa_qvm_mod_pickups_release(qa_qvm_mod_pickups *pickups, qa_actor_id actor);
void qa_qvm_mod_pickups_close(qa_qvm_mod_pickups *pickups);

#endif
