/* Port of src/ui/settings/gameplay.ts. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_GAMEPLAY_H
#define QA_UI_SETTINGS_GAMEPLAY_H

#include <stdbool.h>

#include "qa/ui/settings/index.h"

/** TS GameplaySettingsSource. client NULL reuses cvars;
 * weapon_pickup_shared is (weaponPickupPolicy === "shared"). */
typedef struct qa_ui_gameplay_settings_source {
  qa_cvar_registry *cvars;
  qa_cvar_registry *client;
  bool has_weapon_pickup_policy;
  bool weapon_pickup_shared;
} qa_ui_gameplay_settings_source;

qa_ui_setting_bindings qa_ui_bind_gameplay_settings(
    const qa_ui_gameplay_settings_source *source);
void qa_ui_reset_gameplay_settings(
    const qa_ui_gameplay_settings_source *source);

#endif
