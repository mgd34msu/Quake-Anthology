#ifndef QA_NETWORK_Q3_CLIENT_DOWNLOAD_H
#define QA_NETWORK_Q3_CLIENT_DOWNLOAD_H
#include "qa/network_q3.h"
#include "qa/filesystem.h"

typedef struct qa_q3_client_downloads qa_q3_client_downloads;
typedef enum qa_q3_client_download_phase {
    QA_Q3_CLIENT_DOWNLOAD_PENDING, QA_Q3_CLIENT_DOWNLOAD_RUNNING
} qa_q3_client_download_phase;
typedef struct qa_q3_client_download_progress {
    const char *path;
    qa_q3_client_download_phase phase;
    uint64_t received, total;
    bool total_known;
} qa_q3_client_download_progress;
typedef struct qa_q3_client_download_bindings {
    void *context;
    qa_fs_root *root;
    bool (*current)(void *, qa_error *);
    bool (*permission)(void *, bool *allowed, qa_error *);
    /* Actual selected/base mount spelling, relative to the retained root. */
    bool (*destination)(void *, const char *, qa_buffer *, qa_error *);
    /* Qualifies this tuple against the actual admitted gamestate references. */
    bool (*reference)(void *, const char *remote, uint32_t checksum, qa_error *);
    bool (*nonce)(void *, uint64_t *, qa_error *);
    bool (*reliable)(void *, const char *, qa_error *);
    bool (*send_packet)(void *, qa_error *);
    bool (*reload_packages)(void *, qa_error *);
    void (*progress)(void *, const char *, int32_t count, int32_t size);
    /* Reports retained operation errors; accepted blocks remain retryable. */
    void (*failure)(void *,const qa_error *);
    /* Detached restore creates a new private native stage with the saved
     * logical identity and prefix. It must not publish or reopen a writer. */
    bool (*prepare_stage)(void *, const char *, uint64_t logical_nonce, qa_bytes,
        qa_fs_stage **, uint64_t *native_nonce, qa_error *);
} qa_q3_client_download_bindings;

bool qa_q3_client_downloads_create(const qa_q3_client_download_bindings *, qa_q3_client_downloads **, qa_error *);
void qa_q3_client_downloads_destroy(qa_q3_client_downloads *);
bool qa_q3_client_downloads_begin(qa_q3_client_downloads *, const qa_q3_package *, size_t,
    const uint32_t *loaded_checksums, size_t, bool *downloading, qa_error *);
bool qa_q3_client_downloads_size(qa_q3_client_downloads *, int32_t, int32_t *effective, qa_error *);
bool qa_q3_client_downloads_receive(qa_q3_client_downloads *, const qa_q3_download *, qa_error *);
/* Progress retained block writes, reliable acknowledgements, publication and
 * filesystem refresh after packet/source callbacks return. Successful stages
 * run once; operation errors are reported through failure and retried idle. */
bool qa_q3_client_downloads_pump(qa_q3_client_downloads *, qa_error *);
bool qa_q3_client_downloads_cancel(qa_q3_client_downloads *, qa_error *);
bool qa_q3_client_downloads_retry(qa_q3_client_downloads *, bool *started, qa_error *);
void qa_q3_client_downloads_close(qa_q3_client_downloads *);
bool qa_q3_client_downloads_active(const qa_q3_client_downloads *);
/* Borrowed queue rows expire at the next receiver mutation or close. */
size_t qa_q3_client_downloads_progress_count(const qa_q3_client_downloads *);
bool qa_q3_client_downloads_progress_at(const qa_q3_client_downloads *, size_t,
    qa_q3_client_download_progress *);
bool qa_q3_client_downloads_checkpoint(const qa_q3_client_downloads *, qa_buffer *, qa_error *);
bool qa_q3_client_downloads_restore(qa_bytes, const qa_q3_client_download_bindings *, qa_q3_client_downloads **, qa_error *);
bool qa_q3_client_downloads_handoff_ready(const qa_q3_client_downloads *, const qa_q3_client_downloads *, qa_error *);
void qa_q3_client_downloads_handoff_publish(qa_q3_client_downloads *, qa_q3_client_downloads *);
/* The enclosing owner qualifies the connection/role before rebinding. */
void qa_q3_client_downloads_rebind(qa_q3_client_downloads *, void *context);
#endif
