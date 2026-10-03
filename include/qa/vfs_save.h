#ifndef QA_VFS_SAVE_H
#define QA_VFS_SAVE_H
#include "qa/vfs_view_save.h"

/* Checkpoints contain the actual private cache inventory and immutable bytes.
 * Native filesystem identities remain historical weak lookup keys, rather
 * than authority to reopen a file. VFS mounts require separate admission. */
bool qa_resource_pool_checkpoint(const qa_resource_pool *, qa_buffer *empty,
                                 qa_error *);
/* Content graphs already require native archive admission for their mounted
 * views. Preserve those packages by exact path/kind/size/digest receipts;
 * packages absent from these actual views remain embedded. */
bool qa_resource_pool_checkpoint_linked(const qa_resource_pool *,
    const qa_vfs *const *views, size_t view_count, qa_buffer *empty, qa_error *);
/* Pure complete primitive admission, before any linked archive resolver. */
bool qa_resource_pool_checkpoint_validate(qa_bytes, qa_error *);
/* Import before constructing resource/image/media holders. Existing empty or
 * directory-only VFS views may retain this pool; resource readers and archive
 * mounts must have retired. The pool address and its live reference count stay
 * unchanged. Failure preserves its complete installed cache. */
bool qa_resource_pool_restore_ready(const qa_resource_pool *, qa_error *);
bool qa_resource_pool_restore(qa_resource_pool *, qa_bytes, qa_error *);
bool qa_resource_pool_restore_linked(qa_resource_pool *,
    const qa_vfs_checkpoint_refs *, qa_bytes, qa_error *);
#endif
