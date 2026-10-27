/*
 * Port of src/settings/server/common.ts: the serverLimit, serverToggle
 * and serverFriendlyFire definition constructors. Every string field
 * is borrowed from the caller except the formatted slider default
 * (TS String(defaultValue)), which qa_server_limit stores in the
 * output struct's default_text — hence the out parameter: the
 * struct must not be copied after construction, because
 * default_value points into it.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_COMMON_H
#define QA_SETTINGS_SERVER_COMMON_H

#include <stdbool.h>

#include "qa/settings/server/types.h"

/* TS serverLimit(id, name, label, defaultValue, description,
 * applyAt = "live", integer = true). Fills *out in place. */
void qa_server_limit(qa_server_setting_definition *out, qa_server_setting_id id,
                     const char *name, const char *label, double default_value,
                     const char *description, qa_server_apply_at apply_at,
                     bool integer);
/* TS serverToggle(id, target, label, defaultValue, description,
 * applyAt = "live"). */
qa_server_setting_definition qa_server_toggle(qa_server_setting_id id,
                                              qa_server_setting_target target,
                                              const char *label,
                                              bool default_value,
                                              const char *description,
                                              qa_server_apply_at apply_at);
/* TS serverFriendlyFire(target, defaultValue). */
qa_server_setting_definition qa_server_friendly_fire(
    qa_server_setting_target target, bool default_value);

#endif
