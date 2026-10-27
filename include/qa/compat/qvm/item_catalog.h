/*
 * Port of src/compat/qvm/item-catalog.ts. The qa_qvmx_memory mirror
 * has no write observers (memory-writes.ts, qvm-b), so the catalog
 * cache is invalidated by reset() only. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_ITEM_CATALOG_H
#define QA_COMPAT_QVM_ITEM_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/persistence/value.h"

typedef struct qa_qvm_item_layout {
  bool address_is_number;
  double address;        /* number form */
  double address_global; /* {global} form */
  bool count_is_number;
  double count;
  double count_global, count_maximum;
  bool has_source;
  const char *source; /* "live" */
  double stride;
  double field_class_name, field_pickup_name, field_type, field_tag;
  double weapon_type, ammo_type;
} qa_qvm_item_layout;

typedef struct qa_qvm_catalog_item {
  char *class_name;   /* owned */
  char *pickup_name;  /* owned */
  double type, tag;
} qa_qvm_catalog_item;

typedef struct qa_qvm_catalog_record {
  char *class_name;   /* owned */
  char *pickup_name;  /* owned */
  double type, tag, index, address;
} qa_qvm_catalog_record;

typedef struct qa_qvm_catalog_records {
  qa_qvm_catalog_record *entries; /* owned */
  size_t count;
} qa_qvm_catalog_records;

typedef struct qa_qvm_catalog_items {
  qa_qvm_catalog_item *entries; /* owned */
  size_t count;
} qa_qvm_catalog_items;

qa_qvm_item_layout qa_qvm_parse_item_layout(const qa_save_reader *reader);

/* `types` NULL means TS undefined (no filter). `range` is the optional
 * (offset,length) callback. False only on OOM. */
typedef struct qa_qvm_item_range_fn {
  void *ctx;
  void (*call)(void *ctx, double offset, double length);
} qa_qvm_item_range_fn;
bool qa_qvm_read_item_records(const uint8_t *data, size_t length, qa_qvm_item_layout layout,
                              const double *types, size_t type_count,
                              const qa_qvm_item_range_fn *range, qa_qvm_catalog_records *out);
bool qa_qvm_read_item_catalog(const uint8_t *data, size_t length, qa_qvm_item_layout layout,
                              bool private_inventory, qa_qvm_catalog_items *out);
void qa_qvm_catalog_records_destroy(qa_qvm_catalog_records *records);
void qa_qvm_catalog_items_destroy(qa_qvm_catalog_items *items);

typedef struct qa_qvm_source_item_catalog {
  qa_qvmx_memory *memory; /* borrowed */
  qa_qvm_item_layout layout;
  const uint8_t *initialized;
  size_t initialized_length;
  bool has_records;
  qa_qvm_catalog_records records;
  bool closed;
} qa_qvm_source_item_catalog;

void qa_qvm_source_item_catalog_init(qa_qvm_source_item_catalog *catalog, qa_qvmx_memory *memory,
                                     qa_qvm_item_layout layout, const uint8_t *initialized,
                                     size_t initialized_length);
void qa_qvm_source_item_catalog_destroy(qa_qvm_source_item_catalog *catalog);
void qa_qvm_source_item_catalog_reset(qa_qvm_source_item_catalog *catalog);
/* Borrowed cached records. False only on OOM. */
bool qa_qvm_source_item_catalog_records(qa_qvm_source_item_catalog *catalog,
                                        const qa_qvm_catalog_records **out);
void qa_qvm_source_item_catalog_close(qa_qvm_source_item_catalog *catalog);

#endif
