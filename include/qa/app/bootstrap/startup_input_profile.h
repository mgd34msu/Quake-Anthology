/* Port of src/app/bootstrap/startup-input-profile.ts. frontendSeatSettings
 * (frontend-preferences.ts, sibling slice) and JSON.stringify change
 * detection are mirrored as host fn+ctx. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_INPUT_PROFILE_H
#define QA_APP_BOOTSTRAP_STARTUP_INPUT_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/input/seat.h"
#include "qa/input/weapon_bindings.h"
#include "qa/settings/config.h"
/* Mirror: TS FrontendPreferenceOverrides stays opaque; host applies
 * frontendSeatSettings(values, baseline) and reports value count. */
typedef struct qa_app_startup_input_profile_host {
  void *ctx;
  char *(*serialize_bindings)(void *ctx, qa_seat_input *input);
  char *(*serialize_gamepad)(void *ctx, qa_seat_input *input);
  size_t (*override_count)(void *ctx, const void *overrides);
  qa_seat_settings (*frontend_seat_settings)(void *ctx, const void *overrides, qa_seat_settings baseline);
  char *(*serialize_settings)(void *ctx, const qa_seat_settings *settings);
} qa_app_startup_input_profile_host;
typedef struct qa_app_startup_input_profile qa_app_startup_input_profile;
qa_app_startup_input_profile *qa_app_startup_input_profile_retained(const qa_config_store *settings, qa_seat_input *input, qa_app_startup_input_profile_host host);
qa_app_startup_input_profile *qa_app_startup_input_profile_open(const qa_config_store *settings, qa_seat_input *input, qa_command_dialect dialect,
                                                                const qa_weapon_binding_item *items, size_t item_count, qa_app_startup_input_profile_host host);
void qa_app_startup_input_profile_destroy(qa_app_startup_input_profile *p);
qa_seat_input *qa_app_startup_input_profile_input(const qa_app_startup_input_profile *p);
/* overrides NULL = TS {}; history NULL (count ignored) = TS undefined. */
void qa_app_startup_input_profile_save(qa_app_startup_input_profile *p, const void *overrides,
                                       const char *const *history, size_t history_count, bool has_history);
#endif
