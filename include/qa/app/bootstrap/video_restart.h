/* Port of src/app/bootstrap/video-restart.ts. ApplicationCapture /
 * ApplicationInput / ApplicationQ3Client (sibling bootstrap slices)
 * are mirrored as host fn+ctx. Async drain -> synchronous; host
 * failures are reported as error strings (borrowed) instead of throws.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_VIDEO_RESTART_H
#define QA_APP_BOOTSTRAP_VIDEO_RESTART_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/platform/sdl.h"
/* TS PreparedVideoPresentation. */
typedef struct qa_app_prepared_video_presentation {
  void *ctx;
  void (*validate_publication)(void *ctx);
  const char *(*restart)(void *ctx); /* error or NULL */
} qa_app_prepared_video_presentation;
typedef struct qa_app_video_restart_host {
  void *ctx;
  qa_app_capture *(*capture)(void *ctx); /* NULL = TS null */
  bool (*render_worker)(void *ctx, bool *out);                  /* false = absent */
  bool (*prepare)(void *ctx, qa_app_prepared_video_presentation *out); /* false = TS null */
  void (*publish_window)(void *ctx, qa_sdl_window *window);
  const char *(*published)(void *ctx, const char *kind);
  void (*settled)(void *ctx);
  void (*print)(void *ctx, const char *text, const qa_command_context *source);
  void (*failed)(void *ctx, const char *error); /* TS failed(): Promise<never> */
} qa_app_video_restart_host;
typedef struct qa_app_video_restart qa_app_video_restart;
qa_app_video_restart *qa_app_video_restart_create(qa_app_native_renderer *renderer, qa_app_video_restart_host host);
void qa_app_video_restart_destroy(qa_app_video_restart *v);
bool qa_app_video_restart_pending(const qa_app_video_restart *v);
double qa_app_video_restart_generation(const qa_app_video_restart *v);
/* kind NULL = current window backend; source NULL = TS undefined. */
void qa_app_video_restart_request(qa_app_video_restart *v, const char *kind, const qa_command_context *source);
void qa_app_video_restart_register(qa_app_video_restart *v, qa_command_buffer *commands);
bool qa_app_video_restart_drain(qa_app_video_restart *v);
void qa_app_video_restart_close(qa_app_video_restart *v);
/* TS VideoGuestSeat + prepareVideoGuests. Client/registration mirrors. */
typedef struct qa_app_video_guest_client {
  void *ctx;
  qa_seat_id seat;
  const char *(*shutdown)(void *ctx);
  const char *(*close)(void *ctx);
  /* captureVideoReopen: produce replacement client ctx or error. */
  const char *(*reopen)(void *ctx, qa_app_native_renderer *renderer, void *registration, void **out_replacement_ctx);
} qa_app_video_guest_client;
typedef struct qa_app_video_guest_seat {
  qa_app_video_guest_client client;
  void *ctx;
  void (*viewport)(void *ctx, qa_sdl_window *window, double *out_x, double *out_y, double *out_w, double *out_h);
  void (*publish)(void *ctx, void *replacement_client_ctx);
} qa_app_video_guest_seat;
typedef struct qa_app_video_guest_input {
  void *ctx;
  void *(*client_command_registration)(void *ctx, qa_seat_id seat);
  const char *(*registration_close)(void *ctx, void *registration);
} qa_app_video_guest_input;
/* False = TS null (no seats). */
bool qa_app_prepare_video_guests(qa_app_video_guest_input input, qa_app_native_renderer *renderer,
                                 const qa_app_video_guest_seat *seats, size_t seat_count,
                                 void (*assert_current)(void *ctx), void *assert_ctx,
                                 qa_app_prepared_video_presentation *out);
#endif
