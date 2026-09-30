#ifndef QA_VFS_VIEW_SAVE_H
#define QA_VFS_VIEW_SAVE_H
#include "qa/vfs.h"
#include "qa/filesystem.h"

typedef struct qa_vfs_checkpoint_refs {
    void *context;
    /* Optional read-only service admissions. Each returns an owned real native
     * handle, including on partial failure, for ordinary candidate cleanup.
     * Archive content is independently checked against the restored pool.
     * directory_open must qualify the saved retained root identity against
     * its actual destination mapping; it must not create a directory. */
    bool (*archive_open)(void *, const char *mount_path, qa_fs_file **,
                         qa_fs_identity *, qa_error *);
    bool (*directory_open)(void *, const char *mount_path, const char *retained_path,
                           const qa_fs_identity *, qa_fs_root **, qa_error *);
} qa_vfs_checkpoint_refs;

qa_resource_pool *qa_vfs_resources(const qa_vfs *);
/* Actual retained archive files must still match their immutable cached bytes.
 * Captures real mount/order/rule/restriction/reference/allocator state. */
bool qa_vfs_checkpoint(const qa_vfs *, qa_buffer *empty, qa_error *);
/* Decode the whole owner before read-only native handle admission. NULL refs
 * reopens the saved archive paths and retained directory objects locally.
 * Restore the actual pool first; package ordinals plus kind/digest qualify its
 * namespace. No mounts, links, policy setters or acquisitions are replayed. */
bool qa_vfs_create_restored(qa_resource_pool *, const qa_vfs_checkpoint_refs *,
                            qa_bytes, qa_vfs **empty, qa_error *);
/* Preserves the installed VFS address and its actual pool. Caller owns the
 * idle view and must retire borrowed mount/rule observations before import. */
bool qa_vfs_restore(qa_vfs *, const qa_vfs_checkpoint_refs *, qa_bytes, qa_error *);
#endif
