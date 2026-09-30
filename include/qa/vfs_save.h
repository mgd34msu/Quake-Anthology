#ifndef QA_VFS_SAVE_H
#define QA_VFS_SAVE_H
#include "qa/vfs.h"

/* Checkpoints contain the actual private cache inventory and immutable bytes.
 * Native filesystem identities remain historical weak lookup keys, rather
 * than authority to reopen a file. VFS mounts require separate admission. */
bool qa_resource_pool_checkpoint(const qa_resource_pool *, qa_buffer *empty,
                                 qa_error *);
/* Import before constructing resource/image/media holders. Existing empty or
 * directory-only VFS views may retain this pool; resource readers and archive
 * mounts must have retired. The pool address and its live reference count stay
 * unchanged. Failure preserves its complete installed cache. */
bool qa_resource_pool_restore_ready(const qa_resource_pool *, qa_error *);
bool qa_resource_pool_restore(qa_resource_pool *, qa_bytes, qa_error *);
#endif
