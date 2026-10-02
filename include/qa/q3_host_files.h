#ifndef QA_Q3_HOST_FILES_H
#define QA_Q3_HOST_FILES_H

#include "qa/filesystem.h"

typedef struct qa_q3_host_write_file qa_q3_host_write_file;

/* The real selected product's user-file authority, independent of immutable
 * content/media lookup. Successful host construction retains this root;
 * failure leaves the caller's reference untouched. NULL means no write owner.
 * Resolver context is borrowed from the host's retained frontend lifetime. */
typedef struct qa_q3_host_write_view {
    qa_fs_root *root;
    qa_fs_stream_resolver resolver;
} qa_q3_host_write_view;

#endif
