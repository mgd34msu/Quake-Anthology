/* Port of src/app/bootstrap/video-restart.ts: ApplicationVideoRestart and
 * prepareVideoGuests over the real ApplicationQ3Client and ApplicationInput.
 * Async drain/restart are synchronous; TS throws are qa_raise (Error, or
 * AggregateError with the inner error count in detail) and the TS catch
 * blocks are qa_try. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_VIDEO_RESTART_H
#define QA_APP_BOOTSTRAP_VIDEO_RESTART_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/support.h"
#include "qa/platform/sdl.h"
/* TS PreparedVideoPresentation. validate_publication and restart raise the
 * TS errors. release (C ownership; NULL = nothing) frees ctx once the
 * restart that prepared it has settled. */
typedef struct qa_app_prepared_video_presentation {
  void *ctx;
  void (*validate_publication)(void *ctx);
  void (*restart)(void *ctx);
  void (*release)(void *ctx);
} qa_app_prepared_video_presentation;
/* TS VideoRestartHost. Every member may raise the TS error (published
 * included). failed is TS failed(error): Promise<never> and receives the
 * caught error (an AggregateError's inner errors are counted in the drain,
 * not passed); it raises, and when it returns the drain raises the error. */
typedef struct qa_app_video_restart_host {
  void *ctx;
  qa_app_capture *(*capture)(void *ctx); /* NULL = TS null */
  bool (*render_worker)(void *ctx, bool *out);                  /* NULL or false = absent */
  bool (*prepare)(void *ctx, qa_app_prepared_video_presentation *out); /* false = TS null */
  void (*publish_window)(void *ctx, qa_sdl_window *window);
  void (*published)(void *ctx, const char *kind);
  void (*settled)(void *ctx);
  void (*print)(void *ctx, const char *text, const qa_command_context *source);
  void (*failed)(void *ctx, const qa_caught_error *error);
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
/* TS VideoGuestSeat: the seat's current ApplicationQ3Client, its viewport over
 * a window, and the publication of the reopened client. */
typedef struct qa_app_video_guest_seat {
  qa_app_application_q3_client *client;
  void *ctx;
  qa_rect (*viewport)(void *ctx, qa_sdl_window *window);
  void (*publish)(void *ctx, qa_app_application_q3_client *client);
} qa_app_video_guest_seat;
/* TS prepareVideoGuests(input, renderer, seats, assertCurrent): false is TS
 * null (no seats). Each client's captureVideoReopen runs here (raising for a
 * non-QVM seat). The seats are copied. ctx (assertCurrent's context) is
 * borrowed for the prepared presentation's life; a seat's ctx is borrowed by
 * its reopened client's viewport closure and must outlive that client. The
 * previous clients are shut down and closed, not destroyed: publish receives
 * the replacement and the publisher frees the previous client. */
bool qa_app_prepare_video_guests(qa_app_application_input *input, qa_app_native_renderer *renderer,
                                 const qa_app_video_guest_seat *seats, size_t seat_count,
                                 void (*assert_current)(void *ctx), void *ctx,
                                 qa_app_prepared_video_presentation *out);
#endif
