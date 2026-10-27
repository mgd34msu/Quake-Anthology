/*
 * Port of src/compat/qvm/primary-inventory-profile.ts. Mirrored types
 * (TS owner src/compat/qvm/game-inventory.ts, parallel slice):
 * qa_qvmy_inventory_profile mirrors QvmInventoryProfile (public form
 * carries the parsed capacity declaration; private form carries the
 * storage list read through the landed content reader).
 * qa_qvmy_validate_item_storage mirrors validateQvmItemStorage (TS
 * owner src/compat/qvm/item-storage.ts) for reuse by mod-items.
 * The capacity closure becomes qa_qvmy_inventory_capacity().
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_INVENTORY_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_INVENTORY_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/persistence/value.h"

typedef void (*qa_qvmy_item_field_check_fn)(void *ctx, const qa_qvm_item_field *field, const char *usage);

void qa_qvmy_validate_item_storage(const qa_qvm_item_storage *storage, size_t storage_count,
                                   const qa_item_id *items, size_t item_count,
                                   const qa_qvmy_region_instruction *instructions, size_t instruction_count,
                                   int64_t initialized_data_length, int64_t bss_length,
                                   qa_qvmy_item_field_check_fn field, void *field_ctx);

/* Mirrored SourceWord: kind exact TS strings. */
typedef struct qa_qvmy_source_word {
  const char *kind; /* constant|weapon|client|entity|client-number|maximum-grant */
  int64_t value;    /* constant */
} qa_qvmy_source_word;

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
  qa_qvmy_source_word *capacity_arguments; /* owned */
  size_t capacity_argument_count;
  qa_qvm_region_evaluation capacity_region;
  int64_t *capacity_region_inputs; /* owned */
  qa_qvmy_source_word *capacity_inputs; /* owned */
  size_t capacity_input_count;
  bool has_stack;
  qa_qvmy_evaluation_stack stack;
  /* private */
  int64_t entity_stride;
  int64_t client_stride;
  qa_qvm_item_storage *storage; /* owned array; nested spans borrowed from reader values */
  size_t storage_count;
} qa_qvmy_inventory_profile;

typedef struct qa_qvmy_inventory_records {
  int64_t client_stride;
  int64_t entity_stride;
} qa_qvmy_inventory_records;

typedef struct qa_qvmy_capacity_context {
  qa_qvm_module *module;
  int64_t client;
  int64_t entity;
  int64_t client_number;
} qa_qvmy_capacity_context;

void qa_qvm_read_primary_inventory_profile(const qa_save_reader *reader, const qa_qvmy_artifact *artifact,
                                           const qa_qvmy_inventory_records *records,
                                           qa_qvmy_inventory_profile *out);
int32_t qa_qvmy_inventory_capacity(const qa_qvmy_inventory_profile *profile, qa_qvm_memory *memory,
                                   int32_t weapon, const qa_qvmy_capacity_context *context);
void qa_qvmy_inventory_profile_free(qa_qvmy_inventory_profile *profile);

#endif
