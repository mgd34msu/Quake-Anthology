/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_VFS_H
#define QA_VFS_H

#include "qa/archive.h"
#include "qa/hash.h"

typedef struct qa_resource_pool qa_resource_pool;
typedef struct qa_resource qa_resource;
typedef struct qa_vfs qa_vfs;
typedef struct qa_vfs_file qa_vfs_file;
typedef uint64_t qa_mount_id;

/* A pool shares immutable file versions and decoded members across VFS views.
 * Mutable mount orders remain private to each view. Calls require one owner
 * thread or external synchronization. Destroy releases the caller's reference;
 * existing views and acquired resources retain their own lifetimes. */
qa_resource_pool *qa_resource_pool_create(qa_error *error);
void qa_resource_pool_destroy(qa_resource_pool *pool);
/* Drops cached resources/packages with no live readers or mounts. */
void qa_resource_pool_trim(qa_resource_pool *pool);
qa_vfs *qa_vfs_create(qa_resource_pool *pool, qa_error *error);
/* Copies mount/order/policy state while sharing immutable resources. Neither
 * view depends on the other's lifetime or subsequent configuration changes. */
qa_vfs *qa_vfs_clone(const qa_vfs *vfs, qa_error *error);
void qa_vfs_destroy(qa_vfs *vfs);

typedef struct qa_vfs_mount_info {
    qa_mount_id id;
    bool is_archive;
    qa_archive_kind format;
    qa_archive_comparison comparison;
    bool writable;
    bool user_overlay;
    bool referenced;
    const qa_sha256_digest *digest;
} qa_vfs_mount_info;
size_t qa_vfs_mount_count(const qa_vfs *vfs);
/* Info follows default search order. Its digest is borrowed until unmount. */
bool qa_vfs_mount_at(const qa_vfs *vfs, size_t index, qa_vfs_mount_info *out);
void qa_vfs_clear_references(qa_vfs *vfs);

/* New mounts append at lowest priority. Paths are native filesystem paths.
 * Repeated archive mounts share storage when file identity and format agree.
 * A writable directory permits explicit replacement/removal through this API;
 * its read precedence is still selected by the caller's order. */
bool qa_vfs_mount_archive(qa_vfs *vfs, const char *path, qa_archive_kind kind,
                          qa_archive_comparison comparison, qa_mount_id *out,
                          qa_error *error);
bool qa_vfs_mount_directory(qa_vfs *vfs, const char *path,
                            qa_archive_comparison comparison, bool writable,
                            qa_mount_id *out, qa_error *error);
bool qa_vfs_unmount(qa_vfs *vfs, qa_mount_id mount, qa_error *error);
/* Orders include every current mount exactly once, highest priority first.
 * A prefix is a relative directory without its trailing separator. The first
 * matching prefix rule wins. Setting an existing prefix replaces its order;
 * NULL order removes that rule. New/unmounted IDs update all existing orders. */
bool qa_vfs_set_order(qa_vfs *vfs, const qa_mount_id *order, size_t count,
                      qa_error *error);
bool qa_vfs_set_prefix_order(qa_vfs *vfs, const char *prefix,
                             const qa_mount_id *order, size_t count,
                             qa_error *error);
/* User overlays precede resource links and ordinary orders. */
bool qa_vfs_set_user_overlay(qa_vfs *vfs, qa_mount_id mount, bool enabled,
                             qa_error *error);
/* Required archive digests must be mounted. Listed archives gain priority;
 * other archives and nonconfiguration loose assets are excluded. Zero digests
 * disables pure restrictions. Priority changes persist until an explicit new
 * order is selected. Demo mode validates source PK3 checksums and
 * restricts loose assets. User overlays remain available in both modes. */
bool qa_vfs_set_restrictions(qa_vfs *vfs, const qa_sha256_digest *archives,
                              size_t count, bool q3_demo, qa_error *error);
const qa_sha256_digest *qa_vfs_archive_digest(const qa_vfs *vfs, qa_mount_id mount);
bool qa_vfs_archive_checksums(qa_vfs *vfs, qa_mount_id mount, uint32_t feed,
                               uint32_t *checksum, uint32_t *pure_checksum,
                               qa_error *error);
/* First matching link wins and does not fall through on a miss. Prefixes retain
 * their trailing separator when supplied. The target must be a loose mount;
 * an empty target prefix refers to its root. NULL target removes the link. */
bool qa_vfs_set_link(qa_vfs *vfs, const char *source_prefix, qa_mount_id mount,
                     const char *target_prefix, qa_error *error);

typedef struct qa_vfs_listing {
    char **names;
    size_t count;
} qa_vfs_listing;
/* Source Q3 unfiltered listing: ordered mounts, at most 4095 distinct folded
 * names, archive descendants at the source depth, immediate loose entries.
 * Extension "/" requests directories. Empty directory selects the root. */
bool qa_vfs_list(qa_vfs *vfs, const char *directory, const char *extension,
                  qa_vfs_listing *out, qa_error *error);
void qa_vfs_listing_free(qa_vfs_listing *listing);

/* Resource requests normalize backslashes and reject absolute paths, empty
 * components, dot/parent components and colon. Failure sets *out to NULL.
 * PAK duplicate names use the first record; ZIP-family names use the last.
 * A changed archive fails future acquisition; held bytes remain valid. Changed
 * loose files acquire a new immutable version. out_mount is optional. */
bool qa_vfs_acquire(qa_vfs *vfs, const char *path, qa_resource **out,
                    qa_mount_id *out_mount, qa_error *error);
typedef bool (*qa_vfs_accept_mount)(qa_mount_id mount, void *context);
/* The filter must not mutate this VFS during acquisition. A rejected link
 * destination counts as a miss without falling through to ordinary mounts. */
bool qa_vfs_acquire_filtered(qa_vfs *vfs, const char *path,
                              qa_vfs_accept_mount accept, void *context,
                              qa_resource **out, qa_mount_id *out_mount,
                              qa_error *error);
bool qa_vfs_acquire_from(qa_vfs *vfs, qa_mount_id mount, const char *path,
                         qa_resource **out, qa_error *error);
void qa_resource_retain(qa_resource *resource);
void qa_resource_release(qa_resource *resource);
qa_bytes qa_resource_bytes(const qa_resource *resource);
/* IDs are unique within their resource pool. Paths retain source spelling. */
uint64_t qa_resource_id(const qa_resource *resource);
const char *qa_resource_path(const qa_resource *resource);
/* Persistent content identity, independent of process-local numeric handles. */
const qa_sha256_digest *qa_resource_digest(const qa_resource *resource);
bool qa_resource_archive_origin(const qa_resource *resource,
                                  qa_sha256_digest *archive_digest,
                                  size_t *member_ordinal);

/* Replace writes a complete sibling temporary file, then renames it atomically.
 * Parent directories are created within the selected writable root. Write paths
 * forbid symlinks. Existing acquired byte versions are never modified. */
bool qa_vfs_write(qa_vfs *vfs, qa_mount_id mount, const char *path,
                  qa_bytes bytes, qa_error *error);
bool qa_vfs_remove(qa_vfs *vfs, qa_mount_id mount, const char *path,
                   qa_error *error);

typedef enum qa_vfs_write_mode {
    QA_VFS_WRITE,
    QA_VFS_APPEND,
    QA_VFS_APPEND_SYNC
} qa_vfs_write_mode;
typedef enum qa_vfs_seek_origin {
    QA_VFS_SEEK_SET,
    QA_VFS_SEEK_CURRENT,
    QA_VFS_SEEK_END
} qa_vfs_seek_origin;
typedef struct qa_vfs_file_state {
    const char *path;
    qa_vfs_write_mode mode;
    uint64_t position;
} qa_vfs_file_state;

/* Native writable descriptors survive VFS teardown. WRITE truncates only on a
 * fresh open; resume preserves bytes. Append modes create a missing target.
 * APPEND_SYNC preserves the source fflush contract with unbuffered writes. */
bool qa_vfs_file_open(qa_vfs *vfs, qa_mount_id mount, const char *path,
                       qa_vfs_write_mode mode, qa_vfs_file **out, qa_error *error);
bool qa_vfs_file_resume(qa_vfs *vfs, qa_mount_id mount,
                         const qa_vfs_file_state *state, qa_vfs_file **out,
                         qa_error *error);
bool qa_vfs_file_write(qa_vfs_file *file, qa_bytes bytes, size_t *written,
                        qa_error *error);
bool qa_vfs_file_seek(qa_vfs_file *file, int64_t offset,
                       qa_vfs_seek_origin origin, qa_error *error);
/* The returned path is borrowed until close; copy it into owned save state. */
qa_vfs_file_state qa_vfs_file_capture(const qa_vfs_file *file);
void qa_vfs_file_close(qa_vfs_file *file);

#endif
