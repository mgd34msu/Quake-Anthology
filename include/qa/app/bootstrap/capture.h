/* Port of src/app/bootstrap/capture.ts: ApplicationCaptureServices,
 * inputCaptureServices over ApplicationInput, and ApplicationCapture over the
 * console commands (console/commands.c), FrameCapture (capture/index.c) and
 * NativeRenderer.captureNextFrame. GPL-2.0-or-later.
 * C accommodations:
 *  - TS promises become pumped state. A frame-readback operation arms one
 *    renderer capture (the TS read controller); the renderer resolves it on the
 *    next presented frame (synchronous-on-swap), and the operation then runs with
 *    FrameCapture's readRgba returning that frame. beforeWorldChange aborts each
 *    armed read (qa_app_native_renderer_abort_capture, the TS AbortSignal), which
 *    runs the operation against the rejection. Other operations run inside
 *    services.write as they are queued, so drain() has nothing to wait for.
 *  - Operation failures are raises (qa_raise) caught here and printed as
 *    "Console output failed: <message>". console/commands.c reports a failed
 *    capture as "Screenshot capture failed"; when the frame read itself was
 *    rejected, the rejection's message is printed, as TS sees it.
 *  - An operation that never runs (a closed owner, a retired script writer)
 *    keeps its operation context: console/commands.c frees it only when run.
 *  - The TS unregister closure is the console command registration. */
#ifndef QA_APP_BOOTSTRAP_CAPTURE_H
#define QA_APP_BOOTSTRAP_CAPTURE_H
#include <stdbool.h>
#include "qa/console/commands.h"
#include "qa/console/session.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/input/seat.h"
/* TS applicationCaptureRoot. Malloc'd; user_content_root NULL = TS undefined. */
char *qa_app_application_capture_root(const char *user_content_root);
/* TS ApplicationCaptureServices. One ctx serves every callback. console and
 * input return borrowed objects or NULL (TS null); root and map_name are
 * borrowed for the call. write runs operation (TS write(operation)) and returns
 * the failure message or NULL. configuration_write_started NULL is TS undefined.
 * release (C-only) frees ctx when the capture is destroyed; NULL = caller owns. */
typedef struct qa_app_application_capture_services {
  void *ctx;
  qa_command_buffer *commands;
  qa_configuration_write_started_fn configuration_write_started;
  qa_seat_console *(*console)(void *ctx, qa_seat_id seat);
  qa_seat_input *(*input)(void *ctx, qa_seat_id seat);
  bool (*can_chat)(void *ctx);
  const char *(*root)(void *ctx);
  const char *(*map_name)(void *ctx);
  const char *(*write)(void *ctx, const char *(*operation)(void *op_ctx), void *op_ctx);
  void (*print)(void *ctx, const char *text);
  void (*release)(void *ctx);
} qa_app_application_capture_services;
struct qa_app_application_input;
/* TS inputCaptureServices(input, root, mapName, print). Copies root; the input
 * is borrowed. map_name and print are called with their own contexts. The
 * returned services own their ctx (release). */
qa_app_application_capture_services qa_app_input_capture_services(struct qa_app_application_input *input, const char *root,
    const char *(*map_name)(void *ctx), void *map_ctx, void (*print)(void *ctx, const char *text), void *print_ctx);
/* TS ApplicationCapture. */
typedef struct qa_app_capture qa_app_capture;
struct qa_app_native_renderer;
/* new ApplicationCapture(services, renderer): takes the services (and their
 * release); the renderer is borrowed. */
qa_app_capture *qa_app_capture_create(const qa_app_application_capture_services *services, struct qa_app_native_renderer *renderer);
/* new ApplicationCapture(inputCaptureServices(input, root, mapName, print), renderer). */
qa_app_capture *qa_app_application_capture_open(struct qa_app_application_input *input, const char *root,
    const char *(*map_name)(void *ctx), void *map_ctx, void (*print)(void *ctx, const char *text), void *print_ctx,
    struct qa_app_native_renderer *renderer);
/* Closes, then frees the owner and its services. */
void qa_app_capture_destroy(qa_app_capture *capture);
void qa_app_capture_activate(qa_app_capture *capture);
/* TS captureFrame (cl_avidemo). */
void qa_app_capture_frame(qa_app_capture *capture);
bool qa_app_capture_pending_readback(const qa_app_capture *capture);
void qa_app_capture_drain(qa_app_capture *capture);
void qa_app_capture_before_world_change(qa_app_capture *capture);
void qa_app_capture_close(qa_app_capture *capture);
#endif
