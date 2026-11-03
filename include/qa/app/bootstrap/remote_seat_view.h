/* Port of src/app/bootstrap/remote-seat-view.ts (RemoteSeatView and
 * RemoteSeatViewOptions): a remote channel's WorldSeatPresentation, effects and
 * PVS-free presentation over the retained frontend assets.
 *
 * Async members are synchronous (a TS rejection is a raise). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_VIEW_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/remote_seat_source.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/ui.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/network/q3/browser_view.h"

/* TS RemoteSeatViewOptions. Every pointer is borrowed for the view's life. The
 * closures share ctx. q3 NULL is TS null. */
typedef struct qa_app_remote_seat_view_q3 {
  qa_q3_client_connection *connection;
  qa_q3_browser_view *browser;
  qa_app_key_profile *keys;
} qa_app_remote_seat_view_q3;
typedef struct qa_app_remote_seat_view_options {
  /* TS systemCinematics: NonNullable<ApplicationModPresentationsOptions["systemCinematics"]>, with its own ctx
   * (RemoteApplication.componentCinematics(audio)); the unified replica's component view calls it. */
  const qa_app_system_cinematic_host *(*system_cinematics)(void *cinematics_ctx, const struct qa_active_mod_presentation *source,
      const struct qa_app_viewing_seat *presentation, const struct qa_app_component_cinematic_scope *scope);
  void *system_cinematics_ctx;
  qa_app_local_input *local;
  qa_app_application_input *controls;
  qa_app_remote_seat_source *source;
  qa_cvar_registry *cvars;
  qa_app_application_assets *assets;
  qa_app_loaded_content *content;
  const qa_native_ui_art *art;
  qa_text_font_selection font;
  qa_app_application_audio *audio;
  qa_app_native_renderer *renderer;
  qa_app_image_settings *images;
  qa_app_view_settings *view;
  const qa_app_options *options;
  const qa_app_remote_seat_view_q3 *q3;
  void *ctx;
  qa_rect (*viewport)(void *ctx);
  double (*count)(void *ctx);
  double (*index)(void *ctx);
  double (*now)(void *ctx);
  void (*assert_current)(void *ctx);
  void (*quit)(void *ctx);
  void (*execute)(void *ctx, const char *name, const char *const *args, size_t arg_count);
  void (*print)(void *ctx, const char *text);
} qa_app_remote_seat_view_options;

typedef struct qa_app_remote_seat_view qa_app_remote_seat_view;
/* TS static prepare(options). The options are copied. Raises the TS errors
 * after closing what it created ("Remote seat view preparation failed" when a
 * cleanup also fails). */
qa_app_remote_seat_view *qa_app_remote_seat_view_prepare(const qa_app_remote_seat_view_options *options);
/* Frees a view (closing it first when still open). */
void qa_app_remote_seat_view_destroy(qa_app_remote_seat_view *view);
/* TS readonly presentation / effects / assets. */
qa_app_world_seat_presentation *qa_app_remote_seat_view_presentation(const qa_app_remote_seat_view *view);
qa_app_application_effects *qa_app_remote_seat_view_effects(const qa_app_remote_seat_view *view);
qa_app_application_assets *qa_app_remote_seat_view_assets(const qa_app_remote_seat_view *view);
/* TS publish(index, count). */
void qa_app_remote_seat_view_publish(qa_app_remote_seat_view *view, double index, double count);
/* TS prepareFrame(output, music = false): the rows of *out are owned by the view
 * until the next prepareFrame. */
void qa_app_remote_seat_view_prepare_frame(qa_app_remote_seat_view *view, const qa_simulation_output *output, bool music,
    qa_app_audio_seat_events *out);
/* TS close(): raises "Remote seat view retirement failed" when a step failed. */
void qa_app_remote_seat_view_close(qa_app_remote_seat_view *view);
#endif
