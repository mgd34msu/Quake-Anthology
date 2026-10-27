/* Port of src/ui/settings/accessibility.ts. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_ACCESSIBILITY_H
#define QA_UI_SETTINGS_ACCESSIBILITY_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/core/cvars/index.h"
#include "qa/ui/settings/index.h"

extern const qa_ui_preference_values qa_ui_default_preferences;

/* Aborts with "Missing accessibility bounds" only in the TS dead
 * branch (bounds are static here, so it never fires). Registration
 * aborts (TS Error) on allocation failure inside bind. */
void qa_ui_register_accessibility_settings(qa_cvar_registry *cvars);
qa_ui_preference_values qa_ui_read_preferences(qa_cvar_registry *cvars,
                                               int64_t seat);
void qa_ui_write_preferences(qa_cvar_registry *cvars, int64_t seat,
                             qa_ui_preference_values values);

#endif
