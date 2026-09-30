#ifndef QA_CONTENT_VFS_PRIVATE_H
#define QA_CONTENT_VFS_PRIVATE_H
#include "qa/vfs.h"
#include "qa/filesystem.h"
typedef struct package {
    struct package *next;
    size_t references;
    qa_fs_identity identity;
    qa_sha256_digest digest;
    qa_buffer storage;
    qa_archive *archive;
    /* Weak pointers; each live resource retains this package. */
    qa_resource **members;
} package;
struct qa_resource {
    qa_resource *next;
    qa_resource *identity_next;
    size_t references;
    uint64_t id;
    char *path;
    package *archive;
    size_t ordinal;
    qa_fs_identity identity;
    qa_sha256_digest digest;
    qa_archive_data data;
};
struct qa_resource_pool {
    size_t references;
    uint64_t next_resource;
    package *packages;
    qa_resource *resources;
    qa_resource **loose_buckets;
    size_t loose_bucket_count;
    size_t loose_count;
};
typedef struct mount {
    qa_mount_id id;
    char *path;
    qa_archive_comparison comparison;
    package *archive;
    qa_fs_file *archive_file;
    qa_fs_root *root;
    qa_fs_identity identity;
    bool writable;
    bool user_overlay;
    bool referenced;
} mount;
typedef struct resource_link {
    struct resource_link *next;
    char *source;
    char *target;
    qa_mount_id mount;
} resource_link;
typedef struct prefix_order {
    struct prefix_order *next;
    char *prefix;
    qa_mount_id *order;
} prefix_order;
struct qa_vfs {
    qa_resource_pool *pool;
    mount **mounts;
    size_t count;
    qa_mount_id next_mount;
    uint64_t next_temporary;
    prefix_order *prefixes;
    resource_link *links;
    qa_sha256_digest *pure;
    size_t pure_count;
    bool q3_demo;
};
struct qa_vfs_file {
    qa_fs_stream *stream;
    char *path;
    qa_vfs_write_mode mode;
    uint64_t position;
};
void vfs_package_release(package *);
void vfs_mount_free(mount *);
#endif
