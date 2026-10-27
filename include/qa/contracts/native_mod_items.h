/*
 * Port of src/contracts/native-mod-items.ts (types only).
 * Test lists nested one level (readonly (readonly NativeItemTest[])[])
 * become rows of pointer + count.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_NATIVE_MOD_ITEMS_H
#define QA_CONTRACTS_NATIVE_MOD_ITEMS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/source_items.h"

/* TS NativeItemField = NativeModArmorField. */
typedef qa_native_mod_armor_field qa_native_item_field;

typedef struct qa_native_item_pointer {
  const char *record;
  double offset;
} qa_native_item_pointer;

/* TS NativeItemCapacity union on kind: "constant" | "field" | "source". */
typedef struct qa_native_item_capacity {
  const char *kind;
  double value;
  qa_native_item_field field;
  qa_native_mod_address address;
  qa_native_mod_scalar encoding;
} qa_native_item_capacity;

typedef struct qa_native_item_mask { qa_item_id item; double mask; } qa_native_item_mask;

/* TS NativeItemStorage union on kind: "counter" | "bits". */
typedef struct qa_native_item_storage {
  const char *kind;
  qa_native_item_field field;
  qa_item_id item;                  /* counter */
  qa_native_item_capacity capacity; /* counter */
  double private_mask;              /* bits */
  const qa_native_item_mask *items; /* bits */
  size_t item_count;
} qa_native_item_storage;

/* TS NativeItemTest: {kind:"scalar",field,mask|null,comparison,value}
 * | {kind:"pointer",field,value NativeModAddress|null}.
 * comparison: "equals" | "at-most". */
typedef struct qa_native_item_test {
  const char *kind;
  qa_native_item_field field;
  bool has_mask;
  double mask;
  const char *comparison;
  double scalar_value;
  qa_native_item_pointer pointer_field;
  const qa_native_mod_address *pointer_value; /* nullable */
} qa_native_item_test;

typedef struct qa_native_item_test_row {
  const qa_native_item_test *tests;
  size_t count;
} qa_native_item_test_row;

typedef struct qa_native_weapon_dispatcher {
  qa_native_mod_entry entry;
  const char *record;
  double argument, arguments;
} qa_native_weapon_dispatcher;

typedef struct qa_native_weapon_decision_field {
  qa_native_item_field field;
  double clear_mask;
} qa_native_weapon_decision_field;
typedef struct qa_native_weapon_decision {
  double entry, join;
  const qa_native_weapon_decision_field *fields;
  size_t field_count;
} qa_native_weapon_decision;

typedef struct qa_native_weapon_selection_value {
  qa_item_id item;
  qa_native_mod_address address;
  qa_native_mod_source_call request;
} qa_native_weapon_selection_value;
typedef struct qa_native_weapon_selection {
  qa_native_item_pointer active;
  const qa_native_item_pointer *pending; /* nullable */
  const qa_native_weapon_selection_value *values;
  size_t value_count;
} qa_native_weapon_selection;

typedef struct qa_native_weapon_stage {
  qa_native_weapon_dispatcher dispatcher;
  const qa_native_weapon_decision *decisions;
  size_t decision_count;
  const qa_native_item_test_row *committed_input; /* optional */
  size_t committed_input_row_count;
  const qa_native_item_test_row *continuations;
  size_t continuation_row_count;
  const qa_native_item_test_row *settled;
  size_t settled_row_count;
  qa_native_weapon_selection selection;
} qa_native_weapon_stage;

/* Item definitions: same base shape as mod-callbacks.ts with
 * Call=qa_native_mod_source_call. */
typedef struct qa_native_mod_item_definition {
  qa_item_id item;
  const char *label;
  bool has_icon;
  const qa_source_item_icon_declaration *icon; /* nullable */
  const char *admission; /* "add" | "replace-primary" */
  bool has_actions;
  const qa_native_mod_source_call *action_use;
  const qa_native_mod_source_call *action_drop;
  bool is_weapon;
  qa_source_weapon_item weapon;
} qa_native_mod_item_definition;

struct qa_native_mod_items {
  const qa_native_mod_item_definition *definitions;
  size_t definition_count;
  const qa_native_item_storage *storage;
  size_t storage_count;
  const qa_native_weapon_stage *weapons; /* optional */
};

#endif
