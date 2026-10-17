#ifndef QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H
#define QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H

#include "qa/persistence_application.h"
#include "qa/native.h"

struct application_provider;
struct application_native_q2;
struct application_native_q2_record_restore;
bool application_native_q2_capture_checkpoint(struct application_native_q2 *,
    qa_native_checkpoint_request, qa_native_checkpoint *, qa_error *);
bool application_native_q2_save_capture(struct application_provider *, qa_save_purpose,
    const qa_application_native_resource_refs *, qa_buffer *, qa_error *);
/* The native engine owns this operation; persistence borrows its pointer.
 * The immutable image and service descriptors remain borrowed until completion
 * or cancellation. Dispose becomes cancel-only before releasing anything and
 * retains the engine owner when actual call or baseline cleanup refuses. */
bool application_native_q2_record_restore_begin(struct application_provider *,
    struct application_provider *, qa_bytes, const qa_application_options *,
    const qa_application_persistence_ops *, struct application_native_q2_record_restore **,
    qa_error *);
bool application_native_q2_record_restore_advance(struct application_native_q2_record_restore *,
    size_t quantum, bool *complete, qa_error *);
bool application_native_q2_record_restore_dispose(struct application_native_q2_record_restore **,
    qa_error *);
/* Pure envelope lookup before native process construction. */
bool application_native_q2_save_resource_recipe(const qa_save_record *, qa_bytes *, qa_error *);

#endif
