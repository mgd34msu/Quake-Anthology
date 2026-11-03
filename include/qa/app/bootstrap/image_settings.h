/* Port of src/app/bootstrap/image-settings.ts: ApplicationImageSettings,
 * the shared renderer, display, audio-volume and accessibility cvars that
 * are local configuration, independent of source game state. Async members
 * are synchronous (docs/PORTING.md). Errors raise through qa_raise with the
 * TS class name and message; the members whose TS body catches (refresh,
 * refreshDisplay's display apply, save) print the TS diagnostic through
 * the print sink instead. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_IMAGE_SETTINGS_H
#define QA_APP_BOOTSTRAP_IMAGE_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/audio/output.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/render/scene/image_policy.h"
#include "qa/render/scene/models/replacements.h"
typedef struct qa_app_world_seat_presentation qa_app_world_seat_presentation;
typedef struct qa_app_rerelease_presentation qa_app_rerelease_presentation;
typedef struct qa_app_native_renderer qa_app_native_renderer;
typedef struct qa_app_view_settings qa_app_view_settings;
/* TS ModelReplacementPolicy (no separate C type). */
typedef qa_model_replacement_policy qa_app_model_policy;
typedef struct qa_app_image_settings qa_app_image_settings;
/* TS ImageSettingsOptions. Strings are copied. print is the TS print. */
typedef struct qa_app_image_settings_options {
  bool has_audio_format; qa_audio_output_format audio_output_format;
  bool defer_persistence;
  qa_command_context context;
  qa_command_dialect dialect;
  const char *user_content_root; /* NULL = TS undefined */
  bool has_gamma; double gamma;
  bool has_render_worker; bool render_worker;
  /* TS displayOverrides { width?, height?, gamma? }. */
  bool has_display_width; double display_width;
  bool has_display_height; double display_height;
  bool has_display_gamma; double display_gamma;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} qa_app_image_settings_options;
/* TS ApplicationImageSettings.open: loads settings/images.cfg (archive
 * entries it sets become persistedEntries), then applies the render-worker
 * and display overrides. NULL only on OOM. */
qa_app_image_settings *qa_app_image_settings_open(qa_app_image_settings_options options);
/* Frees without saving (TS close() saves; call qa_app_image_settings_close first). */
void qa_app_image_settings_destroy(qa_app_image_settings *settings);
qa_cvar_registry *qa_app_image_settings_cvars(qa_app_image_settings *settings);
void qa_app_image_settings_enable_persistence(qa_app_image_settings *settings);
/* TS persistedEntries: borrowed until destroy. */
const qa_cvar_archive_entry *qa_app_image_settings_persisted_entries(const qa_app_image_settings *settings, size_t *out_count);
void qa_app_image_settings_bind_print(qa_app_image_settings *settings, void (*print)(void *ctx, const char *text), void *print_ctx);
/* TS bindViewSettings(view): the returned binding's release is the TS
 * closure (idempotent); bindings are owned by the settings. */
typedef struct qa_app_image_view_binding qa_app_image_view_binding;
qa_app_image_view_binding *qa_app_image_settings_bind_view_settings(qa_app_image_settings *settings, qa_app_view_settings *view);
void qa_app_image_view_binding_release(qa_app_image_view_binding *binding);
/* TS prepareClientSettings(): a deferred-persistence candidate whose fov
 * is validated, and the cvar transfer that publishes it into these
 * settings' registry. destroy frees the candidate and the transfer. */
typedef struct qa_app_image_client_settings {
  qa_app_image_settings *settings;
  qa_cvar_transfer *transfer;
} qa_app_image_client_settings;
qa_app_image_client_settings qa_app_image_settings_prepare_client_settings(qa_app_image_settings *settings);
void qa_app_image_client_settings_validate_publication(const qa_app_image_client_settings *client);
void qa_app_image_client_settings_publish(const qa_app_image_client_settings *client);
void qa_app_image_client_settings_destroy(qa_app_image_client_settings *client);
/* TS policy getter (imagePolicyFromControls). */
void qa_app_image_settings_policy(qa_app_image_settings *settings, qa_scene_image_policy_owned *out);
double qa_app_image_settings_debug_line_width(qa_app_image_settings *settings);
double qa_app_image_settings_gamma(qa_app_image_settings *settings);
/* Raises Error "r_model_distance requires source or a finite distance". */
qa_app_model_policy qa_app_image_settings_model_policy(qa_app_image_settings *settings);
/* TS refreshDisplay(renderer): called every frame. */
void qa_app_image_settings_refresh_display(qa_app_image_settings *settings, qa_app_native_renderer *renderer);
/* TS refresh(assets, presentations, rerelease, renderer). rerelease and
 * renderer NULL are TS null / undefined. */
void qa_app_image_settings_refresh(qa_app_image_settings *settings, qa_app_application_assets *assets,
    qa_app_world_seat_presentation *const *presentations, size_t presentation_count,
    qa_app_rerelease_presentation *rerelease, qa_app_native_renderer *renderer);
/* TS close(): saves. */
void qa_app_image_settings_close(qa_app_image_settings *settings);
#endif
