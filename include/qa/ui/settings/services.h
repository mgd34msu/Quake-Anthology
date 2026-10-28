/* Port of src/ui/settings/services.ts.
 * C accommodations:
 *  - The TS try/catch around window/worker operations reported
 *    error.message; landed SDL setters abort on failure, so the
 *    report path covers only the binding-level RangeErrors.
 *  - mutate(operation) becomes a mutation callback receiving a
 *    qa_ui_window_mutation; qa_ui_apply_window_mutation is the
 *    TS default (run it now). NULL mutate uses the default.
 *  - LanguageChoice.load() is synchronous and fills the tiers,
 *    returning an error message (borrowed) or NULL; failures go
 *    to the failed callback with that message.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_SERVICES_H
#define QA_UI_SETTINGS_SERVICES_H

#include <stdbool.h>

#include "qa/audio/engine.h"
#include "qa/platform/sdl.h"
#include "qa/text/localization.h"
#include "qa/ui/settings/index.h"

/** A single binding (not an array): dispose by freeing items via
 * the returned owner. */
qa_ui_setting_bindings qa_ui_bind_effects_volume(
    qa_unified_audio *audio, double (*read)(void *ctx), void *ctx);

/** TS bindRendererSettings options. backend strings: "cpu"|"gl".
 * enabled NULL means always true; has_worker gates the worker row;
 * apply/report return values are void as in TS (failures inside
 * worker write/apply are reported through report_error_message:
 * worker_write/apply return an error message or NULL). */
typedef struct qa_ui_renderer_settings_options {
  void *ctx;
  const char *(*current)(void *ctx);
  bool has_worker;
  bool (*worker_read)(void *ctx);
  const char *(*worker_write)(void *ctx, bool value);
  const char *(*apply)(void *ctx, const char *backend);
  void (*report)(void *ctx, const char *message);
  bool (*enabled)(void *ctx); /* NULL = always */
} qa_ui_renderer_settings_options;
qa_ui_setting_bindings qa_ui_bind_renderer_settings(
    const qa_ui_renderer_settings_options *options);

/** TS mutate operation. */
typedef struct qa_ui_window_mutation {
  qa_sdl_window *window;
  bool is_fullscreen;
  double width, height;
  bool fullscreen;
} qa_ui_window_mutation;
void qa_ui_apply_window_mutation(const qa_ui_window_mutation *mutation);

/** current_window borrows; mutate NULL applies immediately. */
qa_ui_setting_bindings qa_ui_bind_native_video_settings(
    qa_sdl_window *(*current_window)(void *ctx), void *window_ctx,
    qa_cvar_registry *registry,
    void (*report)(void *ctx, const char *message), void *report_ctx,
    void (*mutate)(void *ctx, const qa_ui_window_mutation *mutation),
    void *mutate_ctx);

/** TS LanguageChoice. */
typedef struct qa_ui_language_choice {
  const char *id;
  const char *label;
  void *ctx;
  const char *(*load)(void *ctx, qa_loc_load_tier *out_primary,
                      qa_loc_load_tier *out_fallback);
} qa_ui_language_choice;

typedef struct qa_ui_native_language_settings {
  qa_localization_catalog *localization;
  qa_ui_language_choice *available;
  size_t available_count;
  char *selection;
  bool loading;
  void (*failed)(void *ctx, const char *message);
  void *failed_ctx;
  qa_ui_setting_binding binding;
  qa_ui_choice *choice_view; /* id/label view of available */
} qa_ui_native_language_settings;
/** available is copied (array); choice ctxs are borrowed. */
bool qa_ui_native_language_settings_init(
    qa_ui_native_language_settings *settings,
    qa_localization_catalog *localization,
    const qa_ui_language_choice *available, size_t available_count,
    const char *current,
    void (*failed)(void *ctx, const char *message), void *failed_ctx);
void qa_ui_native_language_settings_destroy(
    qa_ui_native_language_settings *settings);
/** TS select(id): aborts (TS Error) for an uninstalled language;
 * load failures are returned (borrowed message) or NULL. */
const char *qa_ui_native_language_settings_select(
    qa_ui_native_language_settings *settings, const char *id);
/** TS binding(). */
const qa_ui_setting_binding *qa_ui_native_language_settings_binding(
    qa_ui_native_language_settings *settings);

#endif
