/*
 * Port of src/contracts/qc-pickup-callers.ts (types only).
 * The QC statement shape comes from mod_callbacks.h (it is repeated
 * inline in the TS). GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QC_PICKUP_CALLERS_H
#define QA_CONTRACTS_QC_PICKUP_CALLERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/original_pickups.h"

/* TS QcPickupScalar: {kind:"field",name} | {kind:"global",word}. */
typedef struct qa_qc_pickup_scalar {
  bool is_global;
  const char *name;
  double word;
} qa_qc_pickup_scalar;

/* TS QcPickupItem (module-private in TS). count is optional,
 * resource nullable. */
typedef struct qa_qc_pickup_item {
  qa_item_id item;
  const qa_pickup_resource *resource;
  const qa_qc_pickup_scalar *count;
} qa_qc_pickup_item;

typedef struct qa_qc_pickup_item_string_value {
  qa_qc_pickup_item item;
  const char *value;
} qa_qc_pickup_item_string_value;
typedef struct qa_qc_pickup_item_float_value {
  qa_qc_pickup_item item;
  double value;
} qa_qc_pickup_item_float_value;

/* descriptor union on kind: "constant"(item fields inline) |
 * "string"(field, values) | "float"(field, values). */
typedef struct qa_qc_pickup_descriptor {
  const char *kind;
  qa_qc_pickup_item constant_item;
  const char *field;
  const qa_qc_pickup_item_string_value *string_values;
  size_t string_value_count;
  const qa_qc_pickup_item_float_value *float_values;
  size_t float_value_count;
} qa_qc_pickup_descriptor;

/* operation: {kind:"decision",word,accepted} |
 * {kind:"admission"|"grant"|"consume"}. */
typedef struct qa_qc_pickup_region {
  double entry, exit;
  const qa_qc_statement *statements;
  size_t statement_count;
  const char *operation_kind;
  double operation_word;
  double operation_accepted;
} qa_qc_pickup_region;

typedef struct qa_qc_pickup_caller_declaration {
  const char *function;
  qa_qc_pickup_descriptor descriptor;
  const qa_qc_pickup_scalar *dropped; /* optional */
  const qa_qc_pickup_region *regions;
  size_t region_count;
} qa_qc_pickup_caller_declaration;

#endif
