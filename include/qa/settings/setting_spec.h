/*
 * Port of src/settings/setting-spec.ts: the shared description of a
 * cvar-backed setting. The TS union becomes one struct tagged by `kind`;
 * restart is meaningful when has_restart. GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SETTING_SPEC_H
#define QA_SETTINGS_SETTING_SPEC_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/ui.h"
#include "qa/settings/restart.h"

/** TS SettingCategory (exact strings): "display"|"video"|"audio"|"input"|
 * "network"|"accessibility"|"language". */
typedef const char *qa_setting_category;

/** TS CvarSettingSpec. */
typedef struct qa_cvar_setting_spec {
  const char *name;
  const char *label;
  qa_setting_category category;
  bool has_restart;
  qa_restart_kind restart;
  const char *group; /* NULL when absent */
  const char *kind;  /* toggle|slider|choice|text-entry */
  double minimum, maximum, step;
  const qa_ui_choice *choices;
  size_t choice_count;
  double maximum_length;
  bool submit_only;
} qa_cvar_setting_spec;

#endif
