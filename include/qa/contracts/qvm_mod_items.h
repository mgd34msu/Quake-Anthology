/*
 * Port of src/contracts/qvm-mod-items.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_QVM_MOD_ITEMS_H
#define QA_CONTRACTS_QVM_MOD_ITEMS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/source_items.h"

typedef struct qa_qvm_item_field {
  const char *record;
  double offset;
} qa_qvm_item_field;

/* mask is number | null (has_mask). comparison:
 * "equals" | "at-most" (exact strings). */
typedef struct qa_qvm_item_test {
  qa_qvm_item_field field;
  bool has_mask;
  double mask;
  const char *comparison;
  double value;
} qa_qvm_item_test;

/* override comparison: "equals" | "not-equals". */
typedef struct qa_qvm_item_capacity_override {
  double address;
  const char *comparison;
  double value;
  double instruction;
} qa_qvm_item_capacity_override;

/* TS QvmItemCapacity union on kind: "constant" | "field" | "source". */
typedef struct qa_qvm_item_capacity {
  const char *kind;
  double value;
  qa_qvm_item_field field;
  double instruction;
  const qa_qvm_item_capacity_override *overrides;
  size_t override_count;
} qa_qvm_item_capacity;

typedef struct qa_qvm_item_mask { qa_item_id item; double mask; } qa_qvm_item_mask;

/* TS QvmItemStorage union on kind: "counter" | "bits". */
typedef struct qa_qvm_item_storage {
  const char *kind;
  qa_qvm_item_field field;
  qa_item_id item;                /* counter */
  qa_qvm_item_capacity capacity;  /* counter */
  double private_mask;            /* bits */
  const qa_qvm_item_mask *items;  /* bits */
  size_t item_count;
} qa_qvm_item_storage;

typedef struct qa_qvm_weapon_actor {
  const char *record;
  qa_qvm_mod_input_pointer pointer;
} qa_qvm_weapon_actor;

typedef struct qa_qvm_weapon_predicate {
  double instruction;
  bool unselected;
} qa_qvm_weapon_predicate;

typedef struct qa_qvm_weapon_selection_value { double value; qa_item_id item; } qa_qvm_weapon_selection_value;

typedef struct qa_qvm_weapon_projection {
  qa_qvm_mod_input_pointer movement;
  double byte_length, minimum, maximum;
  qa_qvm_item_field view_height, ground;
} qa_qvm_weapon_projection;

typedef struct qa_qvm_weapon_continuation_call {
  double instruction;
  qa_qvm_mod_source_call call;
} qa_qvm_weapon_continuation_call;

typedef struct qa_qvm_weapon_continuation {
  double entry;
  qa_qvm_weapon_actor actor;
  double instruction;
  bool original_taken;
  const qa_qvm_item_test *when;
  size_t when_count;
  const qa_qvm_weapon_predicate *predicates;
  size_t predicate_count;
  qa_qvm_weapon_projection projection;
  const qa_qvm_weapon_continuation_call *calls;
  size_t call_count;
} qa_qvm_weapon_continuation;

typedef struct qa_qvm_weapon_dispatcher {
  double entry;
  qa_qvm_weapon_actor actor;
} qa_qvm_weapon_dispatcher;
typedef struct qa_qvm_weapon_selection {
  qa_qvm_item_field field;
  const qa_qvm_weapon_selection_value *values;
  size_t value_count;
} qa_qvm_weapon_selection;
typedef struct qa_qvm_weapon_request {
  double entry, argument;
  const qa_qvm_item_test *accepted;
  size_t accepted_count;
} qa_qvm_weapon_request;

typedef struct qa_qvm_weapon_stage {
  qa_qvm_weapon_dispatcher dispatcher;
  const qa_qvm_weapon_predicate *predicates;
  size_t predicate_count;
  const qa_qvm_item_test *settled;
  size_t settled_count;
  qa_qvm_weapon_selection selection;
  qa_qvm_weapon_request request;
  qa_qvm_weapon_continuation continuation;
} qa_qvm_weapon_stage;

/* Item definitions: same base shape as mod-callbacks.ts with
 * Call=qa_qvm_mod_source_call (see qa_mod_qc_item_definition). */
typedef struct qa_qvm_mod_item_definition {
  qa_item_id item;
  const char *label;
  bool has_icon;
  const qa_source_item_icon_declaration *icon; /* nullable */
  const char *admission; /* "add" | "replace-primary" */
  bool has_actions;
  const qa_qvm_mod_source_call *action_use;
  const qa_qvm_mod_source_call *action_drop;
  bool is_weapon;
  qa_source_weapon_item weapon;
} qa_qvm_mod_item_definition;

typedef struct qa_qvm_mod_weapon_input {
  double entry;
  qa_qvm_item_field clock;
} qa_qvm_mod_weapon_input;
typedef struct qa_qvm_mod_weapons {
  qa_qvm_mod_weapon_input input;
  qa_qvm_weapon_stage stage;
} qa_qvm_mod_weapons;

struct qa_qvm_mod_items {
  const qa_qvm_mod_item_definition *definitions;
  size_t definition_count;
  const qa_qvm_item_storage *storage;
  size_t storage_count;
  const qa_qvm_mod_weapons *weapons; /* optional */
};

#endif
