#ifndef QA_Q3_HOST_FILES_INTERNAL_H
#define QA_Q3_HOST_FILES_INTERNAL_H

#include "internal.h"
#include "qa/q3_host_files.h"

typedef struct q3_write_file_state {
    const char *path;
    qa_fs_stream_mode mode;
    uint64_t position;
    qa_fs_stream_reference reference;
} q3_write_file_state;

typedef struct q3_write_result {
    size_t written;
    bool zero_retry_exhausted;
} q3_write_result;

/* Pure lookup of an explicit root or a genuine legacy writable loose mount.
 * When both are supplied they must retain the same native directory. */
bool q3_write_view_root(const qa_q3_host_options *, qa_fs_root **, qa_error *);
bool q3_write_file_open(qa_fs_root *, const char *, qa_fs_stream_mode,
                        qa_q3_host_write_file **, qa_fs_stream_open_stage *, qa_error *);
bool q3_write_file_resume(const qa_q3_host_write_view *, const q3_write_file_state *,
                          qa_q3_host_write_file **, qa_error *);
bool q3_write_file_write(qa_q3_host_write_file *, qa_bytes, q3_write_result *, qa_error *);
bool q3_write_file_seek(qa_q3_host_write_file *, int64_t,
                        qa_vfs_seek_origin, qa_error *);
q3_write_file_state q3_write_file_capture(const qa_q3_host_write_file *);
/* Read only the installed stream receipt; never resolve or reopen its root. */
bool q3_write_file_portable_ready(const qa_q3_host_write_view *,
    const qa_q3_host_write_file *, qa_error *);
bool q3_write_file_close_checked(qa_q3_host_write_file *, qa_error *);
void q3_write_file_close(qa_q3_host_write_file *);
/* Consumes the actual slot even when native close reports an error. */
bool q3_file_close_checked(q3_file *, qa_error *);

#endif
