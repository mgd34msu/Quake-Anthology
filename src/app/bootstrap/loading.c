/* Port of src/app/bootstrap/loading.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/loading.h"
#include <stddef.h>
typedef struct service_call { qa_app_loading_frame *frame; } service_call;
static void run_service(void *ctx) {
  qa_app_loading_frame *frame = ((service_call *)ctx)->frame;
  frame->service(frame->service_ctx);
}
void qa_app_loading_next_frame(qa_app_loading_frame *frame) {
  if (frame->failed) qa_rethrow(&frame->failure); /* Promise.reject(state.failure.error) */
  service_call call = {frame};
  if (!qa_try(run_service, &call, &frame->failure)) {
    frame->failed = true;
    qa_rethrow(&frame->failure); /* state.frame?.reject(error) */
  }
}
typedef struct operation_call {
  qa_app_loading_operation_fn operation; void *ctx;
  qa_app_loading_frame *frame;
} operation_call;
static void run_operation(void *ctx) {
  operation_call *call = ctx;
  call->operation(call->ctx, call->frame);
}
void qa_app_service_loading(qa_app_loading_operation_fn operation, void *operation_ctx,
    void (*service)(void *ctx), void *service_ctx) {
  qa_app_loading_frame frame = {service, service_ctx, false, {0}};
  operation_call call = {operation, operation_ctx, &frame};
  qa_caught_error caught;
  bool settled = qa_try(run_operation, &call, &caught);
  if (frame.failed) qa_rethrow(&frame.failure); /* the service failure is rethrown after the operation joins */
  if (!settled) qa_rethrow(&caught);
}
