/*
 * Port of src/compat/qvm/mod-pickups.ts. Services are the landed
 * qa_mod_host_services (TS owner src/world/session/mods.ts;
 * mods.h): this module calls its clients list, the actor registry
 * (resolveOwned/isLive) and inventory bindPickup members directly.
 * Inputs maps become a fixed entry array. Per-actor rules are built
 * into caller-visible rule storage owned by the pickups object.
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
#include "qa/world/session/mods.h"

void qa_qvm_validate_mod_pickups(const qa_qvm_mod_callback_declaration *declaration);

/* One ModCallbackInput -> ModRuntimeValue entry. */
typedef struct qa_qvm_pickup_input {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_qvm_pickup_input;

typedef qa_original_pickup_decision (*qa_qvm_mod_pickup_execute_fn)(void *ctx);

typedef struct qa_qvm_mod_pickup_operations {
  void *ctx;
  void (*current)(void *ctx);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  qa_original_pickup_decision (*context)(void *ctx, const qa_qvm_mod_pickup *definition,
                                         const qa_original_pickup_offer *offer,
                                         const qa_qvm_pickup_input *inputs, size_t input_count,
                                         qa_qvm_mod_pickup_execute_fn execute, void *execute_ctx);
  qa_original_pickup_decision (*observe)(void *ctx, qa_actor_id actor,
                                         const qa_original_pickup_execution *observer,
                                         qa_qvm_mod_pickup_execute_fn execute, void *execute_ctx);
  /* The source return as a number (a float32 return is the decoded float). */
  double (*invoke)(void *ctx, const qa_qvm_mod_source_call *call,
                   const qa_qvm_pickup_input *inputs, size_t input_count);
} qa_qvm_mod_pickup_operations;

typedef struct qa_qvm_mod_pickups {
  const qa_qvm_mod_pickup *definitions;
  size_t definition_count;
  qa_mod_host_services *services;
  qa_provider_id owner;
  const qa_qvm_mod_pickup_operations *operations;
  bool active;
  int64_t depth;
  void *entries; /* opaque per-actor rules/delegates */
  size_t entry_count;
  size_t entry_capacity;
} qa_qvm_mod_pickups;

void qa_qvm_mod_pickups_init(qa_qvm_mod_pickups *pickups, const qa_qvm_mod_pickup *definitions,
                             size_t definition_count, qa_mod_host_services *services,
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
