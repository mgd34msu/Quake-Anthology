#ifndef QA_NETWORK_DOWNLOADS_SAVE_H
#define QA_NETWORK_DOWNLOADS_SAVE_H
#include "qa/downloads.h"

/* Content belongs to an admitted candidate resource and remains borrowed for
 * the restored window's lifetime. Size and digest must match the original
 * content; process pointers and resource handles are never stored. */
bool qa_download_window_checkpoint(const qa_download_window *, qa_buffer *, qa_error *);
bool qa_download_window_restore_checkpoint(qa_bytes record, qa_bytes content,
    qa_download_window **, qa_error *);
#endif
