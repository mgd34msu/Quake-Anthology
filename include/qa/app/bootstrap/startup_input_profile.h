/* Port of src/app/bootstrap/startup-input-profile.ts. The JSON.stringify
 * change detection compares the settings' compact JSON texts
 * (qa_input_bindings_json, qa_gamepad_tuning_json, qa_seat_settings_json).
 * Async -> synchronous; a failed write raises. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_INPUT_PROFILE_H
#define QA_APP_BOOTSTRAP_STARTUP_INPUT_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/contracts/common.h"
#include "qa/input/seat.h"
#include "qa/input/weapon_bindings.h"
#include "qa/settings/config.h"
typedef struct qa_app_startup_input_profile qa_app_startup_input_profile;
/* TS static retained(settings, input) / open(settings, input, dialect, items = []).
 * The store's root is copied; the input is borrowed. */
qa_app_startup_input_profile *qa_app_startup_input_profile_retained(const qa_config_store *settings, qa_seat_input *input);
qa_app_startup_input_profile *qa_app_startup_input_profile_open(const qa_config_store *settings, qa_seat_input *input, qa_command_dialect dialect,
                                                                const qa_weapon_binding_item *items, size_t item_count);
void qa_app_startup_input_profile_destroy(qa_app_startup_input_profile *p);
qa_seat_input *qa_app_startup_input_profile_input(const qa_app_startup_input_profile *p);
/* TS save(values = {}, history?): values NULL = TS {}; has_history false = TS undefined. */
void qa_app_startup_input_profile_save(qa_app_startup_input_profile *p, const qa_app_frontend_preference_overrides *values,
                                       const char *const *history, size_t history_count, bool has_history);
#endif
