/*
 * Port of src/compat/qvm/primary-inventory-profile.ts. The profile
 * is the landed qa_qvm_inventory_profile union (TS owner
 * src/compat/qvm/game-inventory.ts; game_inventory.h): the public
 * form carries the parsed capacity declaration behind its capacity
 * closure; the private form carries the storage list read through
 * the landed content reader. Storage validation is the landed
 * qa_qvm_validate_item_storage (TS owner
 * src/compat/qvm/item-storage.ts, item_storage.h); the capacity
 * context is the landed qa_qvm_capacity_context (game_inventory.h).
 * Reader-owned storage is freed by
 * qa_qvm_primary_inventory_profile_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_INVENTORY_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_INVENTORY_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game_inventory.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/persistence/value.h"

/* TS SourceWord (local to primary-inventory-profile.ts): kind exact
 * TS strings. */
typedef struct qa_qvm_source_word {
  const char *kind; /* constant|weapon|client|entity|client-number|maximum-grant */
  int64_t value;    /* constant */
} qa_qvm_source_word;

typedef struct qa_qvm_inventory_records {
  int64_t client_stride;
  int64_t entity_stride;
} qa_qvm_inventory_records;

/* The capacity context is qa_qvm_capacity_context (game_inventory.h). */

void qa_qvm_read_primary_inventory_profile(const qa_save_reader *reader, const qa_resolved_qvm_artifact *artifact,
                                           const qa_qvm_inventory_records *records,
                                           qa_qvm_inventory_profile *out);
/* Public profile whose capacity closure yields a fixed constant. */
void qa_qvm_primary_inventory_profile_constant(qa_module_identity module, qa_qvm_abi_profile abi_profile,
                                               double weapons_offset, double ammo_offset, int32_t constant,
                                               qa_qvm_inventory_profile *out);
/* Public profile whose capacity is a content closure over live memory (TS
 * q3NativeInventoryProfile, which depends on the weapon and the source's game
 * mode); `release`, optional, runs on capacity_ctx in the profile's free. */
typedef double (*qa_qvm_source_capacity_fn)(void *ctx, qa_qvm_memory *memory, double weapon);
void qa_qvm_primary_inventory_profile_source(qa_module_identity module, qa_qvm_abi_profile abi_profile,
                                             double weapons_offset, double ammo_offset,
                                             qa_qvm_source_capacity_fn capacity, void *capacity_ctx,
                                             void (*release)(void *ctx),
                                             qa_qvm_inventory_profile *out);
void qa_qvm_primary_inventory_profile_free(qa_qvm_inventory_profile *profile);

#endif
