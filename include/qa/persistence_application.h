#ifndef QA_PERSISTENCE_APPLICATION_H
#define QA_PERSISTENCE_APPLICATION_H

#include "qa/application.h"
#include "qa/save.h"
#include "qa/persistence_content.h"
#include "qa/rankings_save.h"
#include "qa/player_progress_save.h"
#include "qa/application_rankings.h"

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

/* A reconstruction graph owns its platform callbacks separately from the
 * candidate. destroy returns false only while this context remains owned. */
typedef struct qa_application_native_baseline_services {
    qa_application_options options;
    void *context;
    bool (*ready)(void *, qa_error *);
    bool (*destroy)(void *, qa_error *);
} qa_application_native_baseline_services;

/* The external save graph holds captured native capability owners separately
 * from the running application. capture performs resources_capture and retains
 * its output even on a checked cleanup failure. resolve borrows that exact held
 * capture for an in-memory image. A file image returns NULL capture and its
 * qualified named recipe for reconstruction under the actual prepared source
 * authority. Resolve itself never opens a file or installs source services.
 * Recipes contain named identities, not pointers. The graph outlives every save image that uses
 * it and every candidate borrowing it during resources_rebind. */
typedef struct qa_application_native_resource_refs {
    void *context;
    bool (*capture)(void *, const char *instance, uint64_t source,
        const qa_native_process_resources *, qa_buffer *, qa_error *);
    bool (*resolve)(void *, const char *instance, uint64_t source, qa_bytes,
        const qa_native_process_resources **, qa_bytes *lower_recipe, qa_error *);
    bool (*attach)(void *, qa_save_image *, qa_error *);
} qa_application_native_resource_refs;

typedef struct qa_application_persistence_ops {
    void *context;
    const qa_application_persistence_owner *owners;
    size_t owner_count;
    /* Read-only enumeration of additional retained content holders. The graph
     * is restored before any candidate content/provider factory is admitted. */
    qa_application_content_visit_fn visit_content;
    const qa_vfs_checkpoint_refs *content_files;
    const qa_application_native_resource_refs *native_resources;
    /* Resolve the saved profile's actual writable view before progression and
     * application construction. The output is borrowed from the prepared graph;
     * NULL denotes an explicitly absent saved profile. No files are replayed or
     * graph owners transferred. The graph holds the root through construction. */
    bool (*resolve_player_profile_root)(void *, const qa_save_image *,
        const qa_application_content_graph *, qa_fs_root **, qa_error *);
    /* Actual optional backend/native profile qualifications. Default rankings
     * are unconfigured; installed external providers require both readonly
     * binding/continuation refs and the final transactional handoff below. */
    const qa_rankings_checkpoint_refs *rankings;
    const qa_player_progress_checkpoint_refs *progress;
    const qa_application_ranking_checkpoint_refs *ranking_source;
    /* Last fallible step, after complete candidate validation. Failure must
     * leave both backend ownerships unchanged. Success qualifies and transfers
     * the genuine candidate backend continuation; relinquish_active is true
     * only if that transfer consumed the old lifecycle's match ownership.
     * An independently owned candidate must leave it false so displaced close
     * still logs out/finishes its unrelated backend. No application/source
     * mutation, file write, login/report replay or candidate revalidation may
     * follow a successful handoff; pointer publication must then be nofail. */
    qa_rankings_handoff_fn rankings_handoff;
    /* Optional detached services before content/provider construction. Install
     * actual routing, handlers and empty consumers without source callbacks or
     * active-service aliases. Resources referenced by a candidate must survive
     * failed teardown until a retained candidate is successfully destroyed. */
    bool (*prepare_services)(void *, qa_application *, const qa_save_image *, qa_error *);
    /* Fill options and an owned lease for original native map reconstruction.
     * The input options describe the isolated candidate; installed callbacks
     * must instead borrow the returned, distinct platform context. Failed
     * preparation may still return a lease, which the application retains. */
    bool (*prepare_native_baseline)(void *, qa_application *, qa_actor_owner,
                                    qa_application_native_baseline_services *, qa_error *);
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
    /* Last external qualification before backend continuation handoff. It must
     * leave both applications and their external owners unchanged on failure.
     * The paired publish callback transfers qualified external ownership without
     * allocation, failure or source calls, immediately before pointer publication. */
    bool (*publish_ready)(void *, qa_application *active,
                          qa_application *candidate, qa_error *);
    void (*publish)(void *, qa_application *active, qa_application *candidate);
    /* Failed candidate teardown closes external consumers while their borrowed
     * application is still alive. False retains both owners for a later retry;
     * the application's ordinary destruction follows only after true. */
    bool (*discard_services)(void *, qa_application *, qa_error *);
    /* Neutral physical CLIENT consoles belong to the frontend's QFCS owner.
     * Capture returns that exact queue codec. Restore qualifies the queue
     * already imported by the CONNECTIONS prefix against these saved bytes;
     * it must not replay configuration or import the queue a second time.
     * Both callbacks are required whenever the real console inventory contains
     * a CLIENT scope. Other scopes use the application's ordinary codec. */
    bool (*client_commands_capture)(void *, qa_application *,
        const qa_application_console_scope *, qa_console *, qa_buffer *, qa_error *);
    bool (*client_commands_restore)(void *, qa_application *,
        const qa_application_console_scope *, qa_console *, qa_bytes, qa_error *);
    bool (*command_binding)(void *, qa_application *, const qa_console *,
        const qa_console_entry *, uint64_t registration_owner, qa_command_handler *, void **, qa_error *);
    bool (*commands_restored)(void *, qa_application *, qa_console *, qa_error *);
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
