#ifndef QA_NETWORK_Q1_DOWNLOAD_SAVE_H
#define QA_NETWORK_Q1_DOWNLOAD_SAVE_H
#include "qa/network_q1_session.h"
#include "qa/filesystem.h"
#include "qa/vfs.h"

typedef struct qa_qw_download_admission {
    qa_fs_root *root;
    uint64_t maximum_bytes;
    qa_vfs *content;
} qa_qw_download_admission;
/* The host supplies exactly one admitted contained root or mounted content
 * owner. A found file transfers an immutable owned snapshot to native signon.
 * These functions never execute signon/emit/source-host callbacks. */
bool qa_qw_file_download_open(const qa_qw_download_admission *, const char *,
    bool *found, qa_qw_download *, qa_error *);
bool qa_qw_file_download_checkpoint(const qa_qw_download *, qa_buffer *, qa_error *);
bool qa_qw_file_download_restore_checkpoint(qa_bytes, const qa_qw_download_admission *,
    qa_qw_download *, qa_error *);
#endif
