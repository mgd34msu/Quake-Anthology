/* Port of src/ui/settings/action-catalog.ts. The returned
 * actions are owned by qa_ui_binding_actions; per-item matches
 * closures keep the items array in the owner state.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_ACTION_CATALOG_H
#define QA_UI_SETTINGS_ACTION_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/input/weapon_bindings.h"
#include "qa/ui/settings/bindings.h"

/** TS BindingCapabilities. score_command NULL is TS null. */
typedef struct qa_ui_binding_capabilities {
  bool chat;
  const char *score_command; /* "score"|"+scores"|NULL */
  bool offhand_grapple;
  bool offhand_grenades;
} qa_ui_binding_capabilities;

/** TS BindableItem is qa_weapon_binding_item (kind weapon/powerup). */

typedef struct qa_ui_binding_actions {
  qa_ui_binding_action *items;
  size_t count;
  void *owner;
} qa_ui_binding_actions;
void qa_ui_binding_actions_free(qa_ui_binding_actions *actions);

qa_ui_binding_actions qa_ui_shared_binding_actions(
    qa_command_dialect dialect, const qa_weapon_binding_item *items,
    size_t item_count, qa_ui_binding_capabilities capabilities);

#endif
