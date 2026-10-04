#ifndef QA_VFS_SAVE_H
#define QA_VFS_SAVE_H
#include "qa/vfs_view_save.h"

/* Checkpoints contain resource IDs and installed-file/member references.
 * Native identities and lookup indexes are rebuilt from actual file admission.
 * VFS mounts require separate admission; installed asset bytes stay outside SAVE. */
bool qa_resource_pool_checkpoint(const qa_resource_pool *, qa_buffer *empty,
                                 qa_error *);
/* Content graphs already require native archive admission for their mounted
 * views. Preserve those packages by path/kind/size receipts; acquired members
 * restore through their original ordinals; loose assets reopen their backing
 * files. Installed content must remain available. */
bool qa_resource_pool_checkpoint_linked(const qa_resource_pool *,
    const qa_vfs *const *views, size_t view_count, qa_buffer *empty, qa_error *);
/* Import before constructing resource/image/media holders. Existing empty or
 * directory-only VFS views may retain this pool; resource readers and archive
 * mounts must have retired. The pool address and its live reference count stay
 * unchanged. Failure preserves its complete installed cache. */
bool qa_resource_pool_restore_ready(const qa_resource_pool *, qa_error *);
bool qa_resource_pool_restore(qa_resource_pool *, qa_bytes, qa_error *);
bool qa_resource_pool_restore_linked(qa_resource_pool *,
    const qa_vfs_checkpoint_refs *, qa_bytes, qa_error *);
#endif
