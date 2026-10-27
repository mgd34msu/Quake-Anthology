/*
 * Port of src/input/weapon-bindings.ts: the official boot weapon
 * catalog, default number-key weapon bindings, and single-command
 * weapon-selection resolution. Returned item/pair arrays own
 * their strings (free with the matching helper); resolution
 * results borrow item ids from the caller's catalog.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_WEAPON_BINDINGS_H
#define QA_INPUT_WEAPON_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"

/** TS WeaponBindingItem.kind. */
typedef enum qa_weapon_binding_kind {
  QA_WEAPON_BINDING_WEAPON, QA_WEAPON_BINDING_POWERUP
} qa_weapon_binding_kind;

/** TS WeaponBindingItem. */
typedef struct qa_weapon_binding_item {
  const char *id;
  const char *label;
  qa_weapon_binding_kind kind;
} qa_weapon_binding_item;

/* campaign/edition NULL select the TS defaults "" and "classic".
 * Malloc'd items with malloc'd strings; NULL only on OOM. */
qa_weapon_binding_item *qa_base_weapon_binding_items(qa_game_family family,
                                                     const char *campaign,
                                                     const char *edition,
                                                     size_t *out_count);
void qa_weapon_binding_items_free(qa_weapon_binding_item *items, size_t count);

/** One TS [key, command] default-weapon-binding pair. */
typedef struct qa_weapon_key_binding {
  char *key;
  char *command;
} qa_weapon_key_binding;

/* Malloc'd pairs with malloc'd strings; NULL only on OOM. */
qa_weapon_key_binding *qa_default_weapon_bindings(const qa_weapon_binding_item *items,
                                                  size_t item_count,
                                                  size_t *out_count);
void qa_weapon_key_bindings_free(qa_weapon_key_binding *bindings, size_t count);

/** TS ResolvedWeaponSelection; item borrows the catalog id. */
typedef struct qa_resolved_weapon_selection {
  qa_weapon_binding_kind kind;
  const char *item;
} qa_resolved_weapon_selection;

/* False is the TS null. */
bool qa_resolve_weapon_selection(const char *command, const char *const *args,
                                 size_t arg_count,
                                 const qa_weapon_binding_item *items,
                                 size_t item_count,
                                 qa_resolved_weapon_selection *out);
/* Borrowed catalog id, or NULL for the TS null. */
const char *qa_weapon_binding_item_for_text(const char *text,
                                   const qa_weapon_binding_item *items,
                                   size_t item_count);

#endif
