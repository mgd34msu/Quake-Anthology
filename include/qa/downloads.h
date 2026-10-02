#ifndef QA_DOWNLOADS_H
#define QA_DOWNLOADS_H
#include "qa/http.h"
#include "qa/filesystem.h"
#include "qa/hash.h"

typedef struct qa_downloads qa_downloads;
typedef uint64_t qa_download_id;
typedef enum qa_download_state {
    QA_DOWNLOAD_RECEIVING, QA_DOWNLOAD_INSTALLING, QA_DOWNLOAD_COMPLETE, QA_DOWNLOAD_FAILED, QA_DOWNLOAD_CANCELED
} qa_download_state;
typedef struct qa_download_request {
    const char *path;
    uint64_t maximum_bytes, expected_bytes;
    qa_sha256_digest digest;
    bool exact_identity;
    uint64_t stage_nonce;
    bool resume;
} qa_download_request;
typedef struct qa_download_view {
    qa_download_id id;
    const char *path;
    uint64_t received, limit;
    qa_download_state state;
    qa_error failure;
    bool published, mounted;
    qa_sha256_digest digest;
    uint64_t stage_nonce;
    /* Inspection completed; the exact sealed publication still needs retry. */
    bool publication_pending;
} qa_download_view;
typedef struct qa_download_hooks {
    void *context;
    bool (*permit)(void *, const qa_download_request *, const char *url, qa_error *);
    /* Inspect downloaded packages/module contracts before installed publication. */
    bool (*inspect)(void *, const char *path, qa_fs_stage *, uint64_t bytes, qa_error *);
    /* Application invalidates/resolves content and resumes signon here. Failure
     * reports published=true, mounted=false; installed content is retained. */
    bool (*remount)(void *, const char *path, const qa_sha256_digest *, qa_error *);
    void (*changed)(void *, const qa_download_view *);
} qa_download_hooks;
typedef struct qa_download_options {
    uint32_t jobs;
    uint64_t maximum_pending_bytes;
    qa_download_hooks hooks;
} qa_download_options;
/* Borrows the application's shared HTTP owner, retains filesystem root. One
 * contained disk stage per active job feeds HTTP and native blocks equally.
 * Owner destruction/cancel retires callbacks before releasing their contexts. */
bool qa_downloads_create(qa_http *, qa_fs_root *, const qa_download_options *, qa_downloads **, qa_error *);
void qa_downloads_destroy(qa_downloads *);
bool qa_downloads_begin(qa_downloads *, const qa_download_request *, const char *http_url,
                         qa_download_id *, qa_error *);
bool qa_downloads_append(qa_downloads *, qa_download_id, uint64_t offset, qa_bytes, qa_error *);
bool qa_downloads_finish(qa_downloads *, qa_download_id, qa_error *);
/* Publish/inspection may complete inside HTTP callbacks. Installation runs
 * once here, after the shared HTTP pump returns, at the application's idle
 * publication boundary. Pending sealed publication retries here without
 * rerunning inspection; individual failures remain in each job's view. */
bool qa_downloads_pump(qa_downloads *, qa_error *);
void qa_downloads_cancel(qa_downloads *, qa_download_id);
/* Stops callbacks and retains the private stage for a fresh-process resume.
 * Persist request identity/path and view.stage_nonce before releasing the job. */
void qa_downloads_suspend(qa_downloads *, qa_download_id);
bool qa_downloads_view(const qa_downloads *, qa_download_id, qa_download_view *);
/* Occupied retained job slots, in physical storage order. Paths remain
 * borrowed until that actual job is released or the owner is destroyed. */
size_t qa_downloads_count(const qa_downloads *);
bool qa_downloads_at(const qa_downloads *, size_t ordinal, qa_download_view *);
void qa_downloads_release(qa_downloads *, qa_download_id);
/* Shared bounded reliable block sender. The admitted content bytes are borrowed
 * until close; source dialect adapters choose block width/opcodes/EOF encoding. */
typedef struct qa_download_window qa_download_window;
typedef struct qa_download_block {
    uint64_t sequence, offset;
    qa_bytes bytes;
    bool eof;
} qa_download_block;
bool qa_download_window_create(qa_bytes, size_t block_bytes, uint32_t window,
                                uint64_t retry_ns, qa_download_window **, qa_error *);
void qa_download_window_destroy(qa_download_window *);
bool qa_download_window_next(qa_download_window *, uint64_t now_ns,
                              qa_download_block *, bool *present, qa_error *);
bool qa_download_window_sent(qa_download_window *, uint64_t sequence, uint64_t now_ns, qa_error *);
bool qa_download_window_acknowledge(qa_download_window *, uint64_t sequence, qa_error *);
bool qa_download_window_complete(const qa_download_window *);
#endif
