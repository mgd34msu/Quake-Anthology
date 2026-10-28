/* Port of src/app/bootstrap/capture.ts. Console command registration
 * and frame capture are host services; pending-operation bookkeeping,
 * closed guards and cancellation text are kept here. Supersedes
 * qa_app_video_capture_mirror in video_restart.h. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CAPTURE_H
#define QA_APP_BOOTSTRAP_CAPTURE_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
/* Malloc'd; user_content_root NULL = TS undefined. */
char *qa_app_application_capture_root(const char *user_content_root);
typedef struct qa_app_capture qa_app_capture;
typedef struct qa_app_capture_host {
  void *ctx;
  const void *commands; /* borrowed CommandBuffer */
  bool (*console_for)(void *ctx, qa_seat_id seat); /* false = TS null */
  bool (*can_chat)(void *ctx);
  const char *(*root)(void *ctx);
  const char *(*map_name)(void *ctx);
  void (*print)(void *ctx, const char *text);
  /* registerConsoleCommands + FrameCapture wiring; error or NULL. */
  const char *(*activate)(void *ctx, qa_app_capture *capture);
  void (*unregister)(void *ctx);
  const char *(*screenshot)(void *ctx); /* captureFrame body */
  const char *(*write)(void *ctx, const char *(*operation)(void *op_ctx), void *op_ctx);
  /* Renderer readback: begin/end bracket one pending read. */
  void (*read_begin)(void *ctx);
  void (*read_end)(void *ctx);
  void (*read_abort)(void *ctx, const char *message);
} qa_app_capture_host;
qa_app_capture *qa_app_capture_create(qa_app_capture_host host);
void qa_app_capture_destroy(qa_app_capture *capture);
void qa_app_capture_activate(qa_app_capture *capture);
void qa_app_capture_frame(qa_app_capture *capture);
bool qa_app_capture_pending_readback(const qa_app_capture *capture);
/* Read brackets used by the host renderer path (TS reads set). */
void qa_app_capture_read_begin(qa_app_capture *capture);
void qa_app_capture_read_end(qa_app_capture *capture);
void qa_app_capture_drain(qa_app_capture *capture);
void qa_app_capture_before_world_change(qa_app_capture *capture);
void qa_app_capture_close(qa_app_capture *capture);
/* TS queue() error reporting for host operations. */
void qa_app_capture_queue(qa_app_capture *capture, const char *(*operation)(void *op_ctx), void *op_ctx, bool frame_readback);
#endif
