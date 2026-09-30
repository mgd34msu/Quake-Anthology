#ifndef QA_PERSISTENCE_APPLICATION_H
#define QA_PERSISTENCE_APPLICATION_H

#include "qa/application.h"
#include "qa/save.h"

/* Services outside the application's concrete codecs supply their actual
 * producer here. Every shared owner and selected provider must be represented;
 * an uninstalled service still supplies its declared explicit absent codec.
 * Descriptors and contexts remain borrowed for the entire operation. */
typedef struct qa_application_persistence_owner {
    qa_save_owner identity;
    void *context;
    bool (*capture)(void *, qa_application *, qa_buffer *, qa_error *);
    bool (*restore)(void *, qa_application *, qa_bytes, qa_error *);
} qa_application_persistence_owner;

typedef struct qa_application_persistence_ops {
    void *context;
    const qa_application_persistence_owner *owners;
    size_t owner_count;
    /* Optional detached external consumer preparation after the application's
     * own restored world/providers are constructed. Source initialization/spawn
     * callbacks must not run. Snapshot is borrowed until this callback returns. */
    bool (*prepare_content)(void *, qa_application *, const qa_launch_snapshot *,
                            const qa_save_image *, qa_error *);
    /* After every owner restores its private fields, rebuild source leases,
     * pickup observations and body/collision/target bindings against the same
     * candidate stores. Final world and scheduler restore follows this step. */
    bool (*reconnect)(void *, qa_application *, const qa_save_image *, qa_error *);
    /* Validate every service's restored references and pending continuation.
     * No external publication, file writes or source callbacks are allowed. */
    bool (*validate)(void *, qa_application *, const qa_save_image *, qa_error *);
} qa_application_persistence_ops;

bool qa_application_persistence_capture(qa_application *,
    const qa_application_persistence_ops *, qa_save_purpose, qa_save_image **, qa_error *);
/* The active owner and displaced output remain unchanged on failure. The
 * retained_on_failure output must initially be NULL; failed candidate teardown
 * transfers its owner there for the caller to retry qa_application_destroy.
 * On success the active pointer changes once and displaced transfers to the
 * caller, who closes it separately. Options describe isolated external services;
 * candidate construction/validation must not publish into active consumers. */
bool qa_application_persistence_restore(qa_application **active,
    const qa_application_options *, const qa_application_persistence_ops *,
    const qa_save_image *, qa_application **displaced,
    qa_application **retained_on_failure, qa_error *);

#endif
