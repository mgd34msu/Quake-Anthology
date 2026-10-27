/*
 * Port of src/settings/server/types.ts (types only): server setting
 * definitions, collections, owners, and profiles. C accommodations:
 *  - The TS Control<Kind> helper extracts CvarSettingSpec variants
 *    from src/ui/settings/index.ts (ui module not yet ported); the
 *    resulting fields are spelled out here directly: toggle carries
 *    no extra fields, slider carries minimum/maximum/step plus the
 *    TS integer flag, choice carries qa_ui_choice rows, text-entry
 *    carries maximumLength (the spec's optional submitOnly is never
 *    set by any server definition and is not observable here).
 *  - ServerSettingId / labels / descriptions are borrowed
 *    NUL-terminated strings (string literals for every built-in
 *    collection). default_value is borrowed too, except definitions
 *    built by qa_server_limit, where it points at default_text.
 *  - ServerSettingsOwner becomes a function pointer plus ctx pair.
 *    read() results are malloc'd strings the caller frees with
 *    qa_server_setting_values_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_TYPES_H
#define QA_SETTINGS_SERVER_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/ui.h"

/* TS ServerSettingId: `server:${string}`. */
typedef const char *qa_server_setting_id;

/* TS ServerApplyAt: "live" | "next-match" | "next-map" | "restart". */
typedef enum qa_server_apply_at {
  QA_SERVER_APPLY_LIVE,
  QA_SERVER_APPLY_NEXT_MATCH,
  QA_SERVER_APPLY_NEXT_MAP,
  QA_SERVER_APPLY_RESTART
} qa_server_apply_at;
const char *qa_server_apply_at_name(qa_server_apply_at apply_at);

/* TS ServerSettingTarget union on kind: "value" | "bit". The TS mask
 * is a number consumed by bitwise operators, hence int32 here. */
typedef struct qa_server_setting_target {
  bool is_bit;
  const char *name; /* borrowed */
  int32_t mask;     /* bit */
  bool inverted;    /* bit */
} qa_server_setting_target;

/* TS ServerSettingDefinition kind: "toggle" | "slider" | "choice" |
 * "text-entry". */
typedef enum qa_server_setting_kind {
  QA_SERVER_SETTING_TOGGLE,
  QA_SERVER_SETTING_SLIDER,
  QA_SERVER_SETTING_CHOICE,
  QA_SERVER_SETTING_TEXT_ENTRY
} qa_server_setting_kind;

typedef struct qa_server_setting_definition {
  qa_server_setting_id id;
  const char *label;       /* borrowed */
  const char *description; /* borrowed */
  qa_server_apply_at apply_at;
  const char *default_value; /* borrowed, or default_text */
  char default_text[32];     /* qa_server_limit backing store */
  qa_server_setting_target target;
  qa_server_setting_kind kind;
  double minimum, maximum, step; /* slider */
  bool integer;                  /* slider */
  const qa_ui_choice *choices;   /* choice, borrowed */
  size_t choice_count;
  double maximum_length; /* text-entry */
} qa_server_setting_definition;

/** TS ServerSettingCollection. Definitions are borrowed. */
typedef struct qa_server_setting_collection {
  const char *id;
  const qa_server_setting_definition *definitions;
  size_t definition_count;
} qa_server_setting_collection;

/* TS ServerSettingValues. Strings are malloc'd; free with
 * qa_server_setting_values_free. */
typedef struct qa_server_setting_values {
  char *desired;
  char *effective;
} qa_server_setting_values;
void qa_server_setting_values_free(qa_server_setting_values *values);

/** TS ServerSettingsOwner. State and scheduling remain in the
 * existing source/configuration owner behind ctx. */
typedef struct qa_server_settings_owner {
  void *ctx;
  qa_server_setting_values (*read)(void *ctx, qa_server_setting_target target);
  void (*write)(void *ctx, qa_server_setting_target target, const char *value,
                qa_server_apply_at apply_at);
} qa_server_settings_owner;

/* TS BoundServerSetting. */
typedef struct qa_bound_server_setting {
  const qa_server_setting_definition *definition;
  qa_server_settings_owner owner;
} qa_bound_server_setting;

/* TS ServerSettingStatus extends ServerSettingValues. */
typedef struct qa_server_setting_status {
  char *desired;
  char *effective;
  bool pending;
  qa_server_apply_at apply_at;
} qa_server_setting_status;
void qa_server_setting_status_free(qa_server_setting_status *status);

/* TS ServerProfile. Strings are owned copies; free the profile with
 * qa_server_profile_free (declared in profile.h). */
typedef struct qa_server_profile_override {
  char *id;
  char *value;
} qa_server_profile_override;
typedef struct qa_server_profile {
  int32_t version; /* TS literal 1 */
  qa_server_profile_override *overrides;
  size_t override_count;
} qa_server_profile;

#endif
