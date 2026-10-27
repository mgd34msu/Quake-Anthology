/*
 * Port of src/contracts/source-items.ts: source item definitions,
 * naming, and weapon handoff shapes. Generic helpers are specialized
 * over qa_source_item_named_entry rows in C; the TS SourceItemRetired
 * Error subclass has no exception counterpart -- the C form is a
 * record a caller can build/test, and its exact TS message is exposed
 * as QA_SOURCE_ITEM_RETIRED_MESSAGE.
 * Runtime helpers in source_items.c abort with the identical TS
 * Error messages. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_SOURCE_ITEMS_H
#define QA_CONTRACTS_SOURCE_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/held_weapon.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"

typedef struct qa_source_equipment_context {
  qa_provider_id provider;
  qa_item_id item; /* nullable */
} qa_source_equipment_context;

/* Aborts (TS Error) when the provider has no context or duplicates. */
qa_item_id qa_source_equipment_item(const qa_source_equipment_context *contexts,
                                    size_t count, qa_provider_id provider);

/* TS SourceItemAction = "use" | "drop". */
typedef enum qa_source_item_action {
  QA_SOURCE_ITEM_ACTION_USE, QA_SOURCE_ITEM_ACTION_DROP
} qa_source_item_action;
const char *qa_source_item_action_name(qa_source_item_action action);

/* TS SourceItemActionCalls<Call>: optional use/drop call payloads.
 * The C form keeps the payloads as opaque pointers; NULL = absent. */
typedef struct qa_source_item_action_calls {
  const void *use_call;
  const void *drop_call;
} qa_source_item_action_calls;

/* Writes present action names in TS order (use, then drop) into out
 * (capacity 2 suffices) and returns the count. */
size_t qa_source_item_action_names(qa_source_item_action_calls calls,
                                   qa_source_item_action *out, size_t out_capacity);

/* Row type for the generic sourceItemNamed<Item extends {item,label}>. */
typedef struct qa_source_item_named_entry {
  qa_item_id item;
  const char *label;
} qa_source_item_named_entry;

/* TS result: {kind:"match",item,exact} | {kind:"ambiguous",items} | null.
 * The C form returns indices into the caller's array; for the
 * ambiguous case all matching indices are written to match_indices
 * (when non-NULL; match_count still reports the full count). */
typedef enum qa_source_item_named_kind {
  QA_SOURCE_ITEM_NAMED_NONE, QA_SOURCE_ITEM_NAMED_MATCH, QA_SOURCE_ITEM_NAMED_AMBIGUOUS
} qa_source_item_named_kind;
typedef struct qa_source_item_named_result {
  qa_source_item_named_kind kind;
  size_t item_index; /* match only */
  bool exact;        /* match only */
  size_t match_count; /* ambiguous only */
} qa_source_item_named_result;
/* original may be NULL (the TS default). Lowercasing is ASCII-only in
 * C; TS toLowerCase is Unicode (noted accommodation). */
qa_source_item_named_result qa_source_item_named(
    const qa_source_item_named_entry *items, size_t count, const char *text,
    qa_item_id original, size_t *match_indices, size_t match_capacity);

/* TS SourceItemIconDeclaration union on kind:
 * "image"(path) | "wad-picture"(path,lump) | "shader"(name). */
typedef struct qa_source_item_icon_declaration {
  const char *kind;
  const char *path;
  const char *lump;
  const char *name;
} qa_source_item_icon_declaration;

/* TS SourceWeaponItem: {kind:"weapon", ammo, held?}. */
typedef struct qa_source_weapon_item {
  qa_item_id ammo; /* nullable */
  const qa_held_weapon_declaration *held; /* optional */
} qa_source_weapon_item;

/* TS SourceItemDefinition = base & ({kind:"counter"} | SourceWeaponItem).
 * icon is a WeaponHudIcon or null, and may be absent entirely. */
typedef struct qa_source_item_definition {
  qa_item_id item;
  const char *label;
  qa_provider_reference source;
  bool has_icon;
  const qa_weapon_hud_icon *icon; /* nullable when has_icon */
  const qa_source_item_action *actions; /* optional */
  size_t action_count;
  bool is_weapon;
  qa_source_weapon_item weapon;
} qa_source_item_definition;

/* admission: "add" | "replace-primary" (exact strings). */
typedef struct qa_source_item_admission {
  qa_source_item_definition definition;
  const char *admission;
} qa_source_item_admission;

typedef struct qa_source_item_store {
  qa_inventory_entry before, after;
} qa_source_item_store;

/** TS SourceItemLease. */
typedef struct qa_source_item_lease {
  void *ctx;
  bool (*current)(void *ctx);
  void (*stored)(void *ctx, const qa_source_item_store *changes, size_t count);
  void (*close)(void *ctx);
} qa_source_item_lease;

/* TS SourceItemRetired message / payload record (see header note). */
#define QA_SOURCE_ITEM_RETIRED_MESSAGE "Committed inventory source is no longer current"
typedef struct qa_source_item_retired {
  qa_source_item_lease lease;
  qa_owned_actor actor;
  qa_provider_id owner;
} qa_source_item_retired;

typedef struct qa_weapon_reference {
  qa_provider_id provider;
  qa_item_id item;
} qa_weapon_reference;

/* TS SourceWeaponRequest status: "pending"|"accepted"|"refused". */
typedef struct qa_source_weapon_request {
  void *ctx;
  double id;
  const char *(*status)(void *ctx);
  void (*cancel)(void *ctx);
} qa_source_weapon_request;

/* TS SourceWeaponHandoff = base & ({kind:"immediate", resume->boolean}
 * | {kind:"source-input", resume/restoreRequest -> request}).
 * resume arguments are nullable item ids. */
typedef struct qa_source_weapon_handoff {
  void *ctx;
  qa_provider_id provider;
  bool (*accepts)(void *ctx, qa_item_id item);
  bool (*select)(void *ctx, qa_item_id item);
  void (*holster)(void *ctx);
  bool (*is_holstered)(void *ctx);
  bool is_source_input;
  bool (*resume_immediate)(void *ctx, qa_item_id item);
  qa_source_weapon_request (*resume_request)(void *ctx, qa_item_id item);
  qa_source_weapon_request (*restore_request)(void *ctx, double id, qa_item_id item);
} qa_source_weapon_handoff;

/* SourceWeaponPresentation.model: ({kind:"resolved",resource} |
 * {kind:"source-path",path}) & {frame}, or null. */
typedef struct qa_source_weapon_presentation_model {
  bool is_resolved;
  qa_resolved_resource_reference resource;
  const char *path;
  double frame;
} qa_source_weapon_presentation_model;

typedef struct qa_source_weapon_presentation {
  qa_provider_reference source;
  qa_item_id active;  /* nullable */
  qa_item_id pending; /* nullable */
  const qa_source_weapon_presentation_model *model; /* nullable */
  const qa_source_item_definition *items;
  size_t item_count;
} qa_source_weapon_presentation;

/** TS SourceWeaponBinding. */
typedef struct qa_source_weapon_binding {
  void *ctx;
  qa_source_weapon_handoff handoff;
  bool (*current)(void *ctx);
  qa_source_weapon_presentation (*read)(void *ctx);
} qa_source_weapon_binding;

/* TS bind() returns an unbind closure: `() => undefined`. */
typedef struct qa_source_weapon_unbind {
  void *ctx;
  void (*invoke)(void *ctx);
} qa_source_weapon_unbind;

/** TS SourceWeaponServices. */
typedef struct qa_source_weapon_services {
  void *ctx;
  qa_source_weapon_unbind (*bind)(void *ctx, qa_owned_actor actor,
                                  qa_source_weapon_binding binding);
  bool (*selected)(void *ctx, qa_actor_id actor, qa_provider_id provider);
  bool (*presented)(void *ctx, qa_actor_id actor, qa_provider_id provider);
  bool (*request)(void *ctx, qa_actor_id actor, qa_weapon_reference weapon);
} qa_source_weapon_services;

#endif
