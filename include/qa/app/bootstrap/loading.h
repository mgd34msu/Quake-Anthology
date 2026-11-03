/* Port of src/app/bootstrap/loading.ts: keep the native window serviced while
 * a loading operation owns it. The async frame pump becomes synchronous
 * cooperative pumping (docs/PORTING.md): the operation runs to completion on
 * the caller's stack, and each qa_app_loading_next_frame call it makes runs
 * service() once, where TS resolves the pending frame after a service pass.
 * Errors raise (qa_raise): an operation failure propagates; a service failure
 * rejects that and every later next_frame of the operation with the service
 * error, and once the operation has settled the service error is raised
 * whatever the operation did (TS "Retain rejection until the operation is
 * joined, including a service failure"). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_LOADING_H
#define QA_APP_BOOTSTRAP_LOADING_H
#include <stdbool.h>
#include "qa/foundation/support.h"
/* TS LoadingState as the operation's nextFrame closure sees it. */
typedef struct qa_app_loading_frame {
  void (*service)(void *ctx); void *service_ctx;
  bool failed; qa_caught_error failure; /* TS state.failure */
} qa_app_loading_frame;
/* TS nextFrame(): services the window once; raises the service failure. */
void qa_app_loading_next_frame(qa_app_loading_frame *frame);
typedef void (*qa_app_loading_operation_fn)(void *ctx, qa_app_loading_frame *frame);
/* TS serviceLoading(operation, service): the operation's value is written
 * through its ctx. */
void qa_app_service_loading(qa_app_loading_operation_fn operation, void *operation_ctx,
    void (*service)(void *ctx), void *service_ctx);
#endif
