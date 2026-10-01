#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_CONTENT_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_CONTENT_H

#include "qa/application_q3_factory.h"

typedef struct qa_application_native_q3_remote_content_admission {
    qa_application_q3_remote_source previous;
    qa_catalog *catalog;
    qa_product_id product;
    const qa_vfs *prepared_mounts;
    uint64_t connection_epoch;
    void *producer;
    /* Read-only proof of the transport's actual prepared content, retained
     * map, decoded gamestate and unchanged connection epoch. The previous
     * source remains current during both calls, before metadata binding. */
    bool (*prepared_current)(void *,
        const struct qa_application_native_q3_remote_content_admission *, qa_error *);
} qa_application_native_q3_remote_content_admission;

/* The genuine old frontend media and service must already be retired. Keeps
 * the physical CLIENT console, registry and service namespace; clones the
 * prepared view and advances only its private content generation at the same
 * epoch. Output borrows the actual native row's retained descriptor. This is
 * metadata admission, not source UI construction, CGAME Init or media/pure
 * completion. Restore uses the native row codec's already claimed descriptor,
 * rather than replaying this operation or claiming its view again. */
bool qa_application_native_q3_remote_content_admit(qa_application *,
    const qa_application_native_q3_remote_content_admission *,
    qa_application_q3_remote_source *, qa_error *);

#endif
