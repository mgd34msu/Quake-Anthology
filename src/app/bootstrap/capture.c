/* Port of src/app/bootstrap/capture.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/capture.h"
#include <stdio.h>
#include <stdlib.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/foundation/support.h"
char *qa_app_application_capture_root(const char *user_content_root) {
  return qa_app_console_config_root(user_content_root);
}
struct qa_app_capture {
  qa_app_capture_host host;
  bool registered, closed;
  long reads, operations;
};
qa_app_capture *qa_app_capture_create(qa_app_capture_host host) {
  qa_app_capture *capture = calloc(1, sizeof *capture);
  if (!capture) return NULL;
  capture->host = host;
  return capture;
}
void qa_app_capture_destroy(qa_app_capture *capture) {
  if (!capture) return;
  qa_app_capture_close(capture);
  free(capture);
}
void qa_app_capture_activate(qa_app_capture *capture) {
  if (capture->closed) qa_range_error("Cannot activate a closed capture owner");
  if (capture->registered) return;
  const char *error = capture->host.activate(capture->host.ctx, capture);
  if (error) qa_range_error("%s", error);
  capture->registered = true;
}
void qa_app_capture_queue(qa_app_capture *capture, const char *(*operation)(void *op_ctx), void *op_ctx, bool frame_readback) {
  if (capture->closed) { capture->host.print(capture->host.ctx, "Console output owner is closed\n"); return; }
  capture->operations++;
  const char *error = frame_readback ? operation(op_ctx) : capture->host.write(capture->host.ctx, operation, op_ctx);
  capture->operations--;
  if (error) {
    char text[2048];
    snprintf(text, sizeof text, "Console output failed: %s\n", error);
    capture->host.print(capture->host.ctx, text);
  }
}
static const char *screenshot_operation(void *ctx) {
  qa_app_capture *capture = ctx;
  return capture->host.screenshot(capture->host.ctx);
}
void qa_app_capture_frame(qa_app_capture *capture) {
  qa_app_capture_queue(capture, screenshot_operation, capture, true);
}
bool qa_app_capture_pending_readback(const qa_app_capture *capture) { return capture->reads != 0; }
void qa_app_capture_read_begin(qa_app_capture *capture) {
  capture->reads++;
  if (capture->host.read_begin) capture->host.read_begin(capture->host.ctx);
}
void qa_app_capture_read_end(qa_app_capture *capture) {
  capture->reads--;
  if (capture->host.read_end) capture->host.read_end(capture->host.ctx);
}
void qa_app_capture_drain(qa_app_capture *capture) { (void)capture; /* synchronous: operations settle inline */ }
void qa_app_capture_before_world_change(qa_app_capture *capture) {
  if (capture->reads != 0 && capture->host.read_abort)
    capture->host.read_abort(capture->host.ctx, "Screenshot cancelled before the requested frame was presented");
  qa_app_capture_drain(capture);
}
void qa_app_capture_close(qa_app_capture *capture) {
  if (!capture || capture->closed) return;
  capture->closed = true;
  if (capture->registered) { capture->host.unregister(capture->host.ctx); capture->registered = false; }
  qa_app_capture_before_world_change(capture);
}
