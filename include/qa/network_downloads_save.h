#ifndef QA_NETWORK_DOWNLOADS_SAVE_H
#define QA_NETWORK_DOWNLOADS_SAVE_H
#include "qa/downloads.h"

/* Content belongs to an admitted candidate resource and remains borrowed for
 * the restored window's lifetime. Size and digest must match the original
 * content; process pointers and resource handles are never stored. */
bool qa_download_window_checkpoint(const qa_download_window *, qa_buffer *, qa_error *);
bool qa_download_window_restore_checkpoint(qa_bytes record, qa_bytes content,
    qa_download_window **, qa_error *);

typedef struct qa_download_checkpoint_refs {
    void *context;
    /* Qualify installed content and the actual target namespace against
     * the actual candidate filesystem owner. This is read-only and must reject
     * missing or incompatible dependencies, never remount or replay a job. */
    bool (*resource)(void *, const qa_download_request *, const qa_download_view *, bool staged, qa_error *);
    /* Allocate an exclusive candidate stage for this actual target/prefix.
     * It must never return an active stage or open its saved nonce. On success
     * stage transfers, and nonce identifies its fresh private filesystem slot.
     * On failure outputs remain empty. No installed file may be published. */
    bool (*stage)(void *, const qa_download_request *, const qa_download_view *, qa_bytes prefix,
        qa_fs_stage **, uint64_t *nonce, qa_error *);
    /* Open only the genuine existing retained artifact for prefix inspection.
     * The returned handle must be read-only and closing it must not alter the
     * artifact. It is not a resumed writer or a preparation stage. */
    bool (*artifact)(void *, const qa_download_request *, const qa_download_view *, qa_fs_stage **, qa_error *);
} qa_download_checkpoint_refs;
/* Real job inventory, HTTP consumer identities and native staged prefixes.
 * HTTP recipes and handles belong to the separate HTTP owner. Closed retained
 * artifacts are inspected through their actual read-only native owner. An
 * accepted pending publication acquires its qualified target through root,
 * compares its complete saved prefix, and retains actual native durability
 * continuation; it does not allocate a replacement private stage. */
bool qa_downloads_checkpoint(const qa_downloads *, qa_buffer *, qa_error *);
bool qa_downloads_restore_checkpoint(qa_bytes, qa_http *, qa_fs_root *, const qa_download_options *,
    const qa_download_checkpoint_refs *, qa_downloads **, qa_error *);
/* Requalify actual job resources before publication. No stage factory, write,
 * transfer, inspection hook, remount or notification runs. Resolver callbacks
 * execute under the owner guard and must only observe candidate resources. */
bool qa_downloads_resources_ready(qa_downloads *, const qa_download_checkpoint_refs *, qa_error *);
/* Compare the genuine live logical job/prefix cut. A candidate's native stage
 * namespace is separate from that logical identity. Publication transfers the
 * original native stage handle and leaves preparation with the retiring owner. */
bool qa_downloads_handoff_ready(const qa_downloads *, const qa_downloads *, qa_error *);
void qa_downloads_handoff_publish(qa_downloads *active, qa_downloads *candidate);
#endif
