/* Port of src/app/bootstrap/loading.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/loading.h"
#include <stddef.h>
typedef struct loading_frame {
  qa_app_loading_service_fn service;
  void *service_ctx;
  const char *failure; /* TS `failure` */
} loading_frame;
static void next_frame(void *v) {
  loading_frame *frame = v;
  if (frame->failure != NULL) return; /* already rejected */
  const char *error = frame->service(frame->service_ctx);
  if (error != NULL) frame->failure = error;
}
const char *qa_app_loading_frame_error(void *frame_ctx) {
  return ((const loading_frame *)frame_ctx)->failure;
}
const char *qa_app_service_loading(qa_app_loading_operation_fn operation, void *operation_ctx,
    qa_app_loading_service_fn service, void *service_ctx, void *result) {
  loading_frame frame = {service, service_ctx, NULL};
  const char *error = operation(operation_ctx, next_frame, &frame, result);
  if (frame.failure != NULL) return frame.failure; /* service failure is rethrown */
  return error;
}
