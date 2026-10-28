/* Port of src/app/bootstrap/image-settings.ts. Cvar registration and
 * documentation strings are kept verbatim; window/image-refresh
 * application is host-wired. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_IMAGE_SETTINGS_H
#define QA_APP_BOOTSTRAP_IMAGE_SETTINGS_H
#include <stdbool.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/audio/output.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_image_settings qa_app_image_settings;
typedef struct qa_app_image_settings_options {
  bool has_audio_format; qa_audio_output_format audio_output_format;
  bool defer_persistence;
  qa_command_context context;
  qa_command_dialect dialect;
  const char *user_content_root; /* NULL = TS undefined */
  bool has_gamma; double gamma;
  bool has_render_worker; bool render_worker;
  bool has_display_width; double display_width;
  bool has_display_height; double display_height;
  bool has_display_gamma; double display_gamma;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} qa_app_image_settings_options;
typedef struct qa_app_image_settings_host {
  void *ctx;
  /* registerSharedClientSettings / accessibility / q1 settings are
   * landed register* calls made by the host during create. */
  void (*register_shared)(void *ctx, qa_cvar_registry *cvars, bool has_format, qa_audio_output_format format);
  /* images.cfg load/save through the settings store. */
  bool (*load_text)(void *ctx, const char *name, char **out_text);
  const char *(*dump_text)(void *ctx, const char *name, const char *text);
  /* Window/display application + image refresh outcomes. */
  const char *(*apply_display)(void *ctx, qa_app_image_settings *settings);
  const char *(*refresh_images)(void *ctx, qa_app_image_settings *settings, qa_app_image_policy policy, qa_app_model_policy model_policy);
  void (*print)(void *ctx, const char *text);
} qa_app_image_settings_host;
/* TS ApplicationImageSettings.open (synchronous). */
qa_app_image_settings *qa_app_image_settings_open(qa_app_image_settings_options options, qa_app_image_settings_host host);
void qa_app_image_settings_destroy(qa_app_image_settings *settings);
qa_cvar_registry *qa_app_image_settings_cvars(qa_app_image_settings *settings);
void qa_app_image_settings_enable_persistence(qa_app_image_settings *settings);
void qa_app_image_settings_bind_print(qa_app_image_settings *settings, void (*print)(void *ctx, const char *text), void *print_ctx);
qa_app_image_policy qa_app_image_settings_policy(qa_app_image_settings *settings);
double qa_app_image_settings_debug_line_width(qa_app_image_settings *settings);
double qa_app_image_settings_gamma(qa_app_image_settings *settings);
/* Aborts "r_model_distance requires source or a finite distance". */
qa_app_model_policy qa_app_image_settings_model_policy(qa_app_image_settings *settings);
const char *qa_app_image_settings_refresh_display(qa_app_image_settings *settings);
const char *qa_app_image_settings_refresh(qa_app_image_settings *settings);
const char *qa_app_image_settings_close(qa_app_image_settings *settings);
/* TS prepareClientSettings candidate creation (deferred persistence). */
qa_app_image_settings *qa_app_image_settings_prepare_client(qa_app_image_settings *settings);
#endif
