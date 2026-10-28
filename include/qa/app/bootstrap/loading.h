/* Port of src/app/bootstrap/loading.ts. The async frame pump becomes
 * synchronous cooperative pumping (PORTING.md): the operation runs to
 * completion, and each next_frame() call it makes invokes service()
 * once, exactly the TS interleave. Operation/service failures are
 * reported as a borrowed error string (NULL on success); a service
 * failure is delivered to the operation's next next_frame() call via
 * qa_app_loading_frame_error() and wins over the operation result.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_LOADING_H
#define QA_APP_BOOTSTRAP_LOADING_H
typedef void (*qa_app_loading_next_frame_fn)(void *frame_ctx);
/* Returns a borrowed error string or NULL. result is operation-defined. */
typedef const char *(*qa_app_loading_operation_fn)(void *ctx,
    qa_app_loading_next_frame_fn next_frame, void *frame_ctx, void *result);
typedef const char *(*qa_app_loading_service_fn)(void *ctx);
/* Non-NULL inside the operation after service() failed (TS: the
 * pending nextFrame() promise rejects with that error). */
const char *qa_app_loading_frame_error(void *frame_ctx);
const char *qa_app_service_loading(qa_app_loading_operation_fn operation, void *operation_ctx,
    qa_app_loading_service_fn service, void *service_ctx, void *result);
#endif
