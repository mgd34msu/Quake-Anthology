/*
 * Port of src/compat/qvm/primary-inventory-profile.ts. Mirrored types
 * (TS owner src/compat/qvm/game-inventory.ts, parallel slice):
 * qa_qvmy_inventory_profile mirrors QvmInventoryProfile (public form
 * carries the parsed capacity declaration; private form carries the
 * storage list read through the landed content reader).
 * Storage validation is the landed qa_qvm_validate_item_storage
 * (TS owner src/compat/qvm/item-storage.ts, item_storage.h); the
 * capacity context is the landed qa_qvm_capacity_context
 * (game_inventory.h). The capacity closure becomes
 * qa_qvmy_inventory_capacity().
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

typedef struct qa_qvmy_inventory_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  bool is_private;
  /* public */
  int64_t weapons_offset;
  int64_t ammo_offset;
  const char *capacity_kind; /* constant|global|region|counter */
  int64_t capacity_constant;
  int64_t capacity_address;
  int64_t capacity_function;
  int64_t *capacity_functions; /* owned */
  size_t capacity_function_count;
  qa_qvm_source_word *capacity_arguments; /* owned */
  size_t capacity_argument_count;
  qa_qvm_region_evaluation capacity_region;
  int64_t *capacity_region_inputs; /* owned */
  qa_qvm_source_word *capacity_inputs; /* owned */
  size_t capacity_input_count;
  bool has_stack;
  qa_qvm_evaluation_stack stack;
  /* private */
  int64_t entity_stride;
  int64_t client_stride;
  qa_qvm_item_storage *storage; /* owned array; nested spans borrowed from reader values */
  size_t storage_count;
} qa_qvmy_inventory_profile;

typedef struct qa_qvm_inventory_records {
  int64_t client_stride;
  int64_t entity_stride;
} qa_qvm_inventory_records;

/* The capacity context is qa_qvm_capacity_context (game_inventory.h). */

void qa_qvm_read_primary_inventory_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                           const qa_qvm_inventory_records *records,
                                           qa_qvmy_inventory_profile *out);
int32_t qa_qvmy_inventory_capacity(const qa_qvmy_inventory_profile *profile, qa_qvm_memory *memory,
                                   int32_t weapon, const qa_qvm_capacity_context *context);
void qa_qvmy_inventory_profile_free(qa_qvmy_inventory_profile *profile);

#endif
