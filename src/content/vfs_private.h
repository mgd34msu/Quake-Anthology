#ifndef QA_CONTENT_VFS_PRIVATE_H
#define QA_CONTENT_VFS_PRIVATE_H
#include "qa/vfs.h"
#include "qa/filesystem.h"
typedef struct package {
    struct package *next;
    size_t references;
    /* Directory owner stays stable for borrowed archive views. Equal payloads
     * retain the one canonical package before publishing member bytes. */
    struct package *canonical;
    qa_resource_pool *pool;
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
    char *root_prefix;
    qa_fs_object_reference *root_references;
    size_t root_reference_count;
    qa_fs_identity identity;
    bool writable;
    bool user_overlay;
    bool referenced;
    bool q3_demo;
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
typedef struct resource_origin {
    struct resource_origin *next;
    qa_mount_id mount;
    uint64_t resource;
    qa_vfs_resource_origin receipt;
} resource_origin;
typedef struct retained_read {
    struct retained_read *next;
    uint64_t resource;
    qa_vfs_read_reference recipe;
} retained_read;
struct qa_vfs {
    size_t references;
    qa_resource_pool *pool;
    mount **mounts;
    size_t count;
    qa_mount_id next_mount;
    uint64_t next_temporary;
    prefix_order *prefixes;
    resource_link *links;
    qa_sha256_digest *pure;
    size_t pure_count;
    qa_vfs_read_reference *reads;
    size_t read_count, read_capacity;
    size_t *read_slots;
    size_t read_slot_count;
    uint64_t read_generation;
    resource_origin *origins;
    retained_read *history;
    bool q3_demo;
};
struct qa_vfs_file {
    qa_fs_stream *stream;
    char *path;
    qa_vfs_write_mode mode;
    uint64_t position;
};
package *vfs_package_canonical(package *);
uint64_t vfs_package_index(const qa_resource_pool *, const package *);
package *vfs_package_at(qa_resource_pool *, uint64_t);
bool vfs_package_materialize(package *, qa_error *);
void vfs_package_release(package *);
void vfs_mount_free(mount *);
bool vfs_root_reference_add(mount *, const qa_fs_object_reference *, qa_error *);
bool vfs_read_record(qa_vfs *, mount *, qa_resource *, const char *, const char *, const char *, const char *, bool, const qa_vfs_read_opening *, qa_error *);
bool vfs_origin_record(qa_vfs *, const mount *, const qa_resource *, qa_error *);
bool vfs_history_record(qa_vfs *, const qa_vfs_read_reference *, qa_error *);
bool vfs_read_valid(const qa_vfs *, const qa_vfs_read_reference *, qa_error *);
bool vfs_demo_package_allowed(const package *, qa_error *);
#endif
