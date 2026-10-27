/* Port of src/compat/qvm/item-storage.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_ITEM_STORAGE_H
#define QA_COMPAT_QVM_ITEM_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/image.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/qvm_mod_items.h"

typedef struct qa_qvm_item_field_validator {
  void *ctx;
  /* usage: NULL (storage) or "capacity". */
  void (*call)(void *ctx, qa_qvm_item_field source, const char *usage);
} qa_qvm_item_field_validator;

/* `items` is the declared item-id set. */
void qa_qvm_validate_item_storage(const qa_qvm_item_storage *storage_values, size_t storage_count,
                                  const qa_item_id *items, size_t item_count,
                                  const qa_qvm_image *image, qa_qvm_item_field_validator field);

typedef struct qa_qvm_item_storage_access {
  void *ctx;
  double (*read)(void *ctx, qa_qvm_item_field field);
  double (*global)(void *ctx, double address);
  void (*write)(void *ctx, qa_qvm_item_field field, double value);
} qa_qvm_item_storage_access;

double qa_qvm_get_item_capacity(const qa_qvm_image *image, qa_qvm_item_capacity capacity,
                                qa_qvm_item_storage_access access);

typedef struct qa_qvm_inventory_entries {
  qa_inventory_entry *entries; /* owned */
  size_t count;
} qa_qvm_inventory_entries;

/* False only on OOM. */
bool qa_qvm_read_item_storage(const qa_qvm_image *image, qa_qvm_item_storage storage,
                              qa_qvm_item_storage_access access, qa_qvm_inventory_entries *out);
void qa_qvm_inventory_entries_destroy(qa_qvm_inventory_entries *entries);
void qa_qvm_write_item_storage(const qa_qvm_image *image, qa_qvm_item_storage storage,
                               qa_inventory_entry entry, qa_qvm_item_storage_access access);

#endif
