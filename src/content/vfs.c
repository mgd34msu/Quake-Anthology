#include "qa/vfs.h"
#include "qa/binary.h"
#include "qa/filesystem.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

static bool demo_package_allowed(const package *archive, qa_error *error);
static void prioritize_mounts(const qa_vfs *vfs, mount **order);
static void prioritize_ids(qa_vfs *vfs, qa_mount_id *order);

static char *copy_string(const char *source)
{
    size_t length = strlen(source);
    char *copy = length == SIZE_MAX ? NULL : malloc(length + 1);
    if (copy != NULL) memcpy(copy, source, length + 1);
    return copy;
}

static char *copy_string_n(const char *source, size_t length)
{
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, source, length);
        copy[length] = '\0';
    }
    return copy;
}

static size_t identity_bucket(const qa_fs_identity *identity, size_t bucket_count)
{
    return (size_t)qa_fs_identity_hash(identity) & (bucket_count - 1);
}

static qa_resource *loose_lookup(const qa_resource_pool *pool,
                                 const qa_fs_identity *identity)
{
    if (pool->loose_bucket_count == 0) return NULL;
    size_t bucket = identity_bucket(identity, pool->loose_bucket_count);
    for (qa_resource *resource = pool->loose_buckets[bucket]; resource != NULL; resource = resource->identity_next)
        if (qa_fs_identity_equal(&resource->identity, identity)) return resource;
    return NULL;
}

static bool loose_reserve(qa_resource_pool *pool, qa_error *error)
{
    if (pool->loose_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "loose resource index overflow");
        return false;
    }
    size_t capacity = pool->loose_bucket_count;
    if (capacity != 0 && pool->loose_count + 1 <= capacity - capacity / 4) return true;
    if (capacity > SIZE_MAX / 2) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "loose resource index overflow");
        return false;
    }
    capacity = capacity == 0 ? 64 : capacity * 2;
    if (capacity > SIZE_MAX / sizeof(*pool->loose_buckets)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "loose resource index overflow");
        return false;
    }
    qa_resource **buckets = calloc(capacity, sizeof(*buckets));
    if (buckets == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate loose resource index");
        return false;
    }
    for (size_t i = 0; i < pool->loose_bucket_count; i++) {
        qa_resource *resource = pool->loose_buckets[i];
        while (resource != NULL) {
            qa_resource *next = resource->identity_next;
            size_t bucket = identity_bucket(&resource->identity, capacity);
            resource->identity_next = buckets[bucket];
            buckets[bucket] = resource;
            resource = next;
        }
    }
    free(pool->loose_buckets);
    pool->loose_buckets = buckets;
    pool->loose_bucket_count = capacity;
    return true;
}

static void loose_remove(qa_resource_pool *pool, qa_resource *resource)
{
    size_t bucket = identity_bucket(&resource->identity, pool->loose_bucket_count);
    qa_resource **position = &pool->loose_buckets[bucket];
    while (*position != resource) position = &(*position)->identity_next;
    *position = resource->identity_next;
    pool->loose_count--;
}

static bool valid_comparison(qa_archive_comparison comparison)
{
    return comparison == QA_ARCHIVE_EXACT || comparison == QA_ARCHIVE_ASCII_INSENSITIVE ||
           comparison == QA_ARCHIVE_CASE_INSENSITIVE;
}

char *qa_vfs_normalize_path(const char *path, qa_error *error)
{
    if (path == NULL || path[0] == '\0') {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource path is empty");
        return NULL;
    }
    const char *part = path;
    for (const char *cursor = path;; cursor++) {
        if (*cursor == ':') {
            qa_error_set(error, QA_ERROR_ARGUMENT, (size_t)(cursor - path), "unsafe resource path");
            return NULL;
        }
        if (*cursor == '/' || *cursor == '\\' || *cursor == '\0') {
            size_t length = (size_t)(cursor - part);
            if (length == 0 || (length == 1 && part[0] == '.') ||
                (length == 2 && part[0] == '.' && part[1] == '.')) {
                qa_error_set(error, QA_ERROR_ARGUMENT, (size_t)(part - path), "unsafe resource path");
                return NULL;
            }
            if (*cursor == '\0') break;
            part = cursor + 1;
        }
    }
    return qa_archive_normalize_path(path, error);
}

static void package_release(package *archive)
{
    if (archive != NULL && --archive->references == 0) {
        qa_archive_close(archive->archive);
        qa_buffer_free(&archive->storage);
        free(archive->members);
        free(archive);
    }
}

void qa_resource_retain(qa_resource *resource)
{
    if (resource != NULL) resource->references++;
}

void qa_resource_release(qa_resource *resource)
{
    if (resource != NULL && --resource->references == 0) {
        if (resource->archive != NULL && resource->archive->members[resource->ordinal] == resource)
            resource->archive->members[resource->ordinal] = NULL;
        qa_archive_data_free(&resource->data);
        package_release(resource->archive);
        free(resource->path);
        free(resource);
    }
}

qa_bytes qa_resource_bytes(const qa_resource *resource)
{
    return resource == NULL ? (qa_bytes){0} : resource->data.bytes;
}

uint64_t qa_resource_id(const qa_resource *resource)
{
    return resource == NULL ? 0 : resource->id;
}

const char *qa_resource_path(const qa_resource *resource)
{
    return resource == NULL ? NULL : resource->path;
}

const qa_sha256_digest *qa_resource_digest(const qa_resource *resource)
{
    return resource == NULL ? NULL : &resource->digest;
}

bool qa_resource_archive_origin(const qa_resource *resource,
                                  qa_sha256_digest *digest, size_t *ordinal)
{
    if (resource == NULL || resource->archive == NULL) return false;
    if (digest != NULL) *digest = resource->archive->digest;
    if (ordinal != NULL) *ordinal = resource->ordinal;
    return true;
}

qa_resource_pool *qa_resource_pool_create(qa_error *error)
{
    qa_resource_pool *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource pool");
        return NULL;
    }
    pool->references = 1;
    pool->next_resource = 1;
    return pool;
}

void qa_resource_pool_destroy(qa_resource_pool *pool)
{
    if (pool == NULL || --pool->references != 0) return;
    qa_resource *resource = pool->resources;
    while (resource != NULL) {
        qa_resource *next = resource->next;
        qa_resource_release(resource);
        resource = next;
    }
    package *archive = pool->packages;
    while (archive != NULL) {
        package *next = archive->next;
        package_release(archive);
        archive = next;
    }
    free(pool->loose_buckets);
    free(pool);
}

void qa_resource_pool_trim(qa_resource_pool *pool)
{
    if (pool == NULL) return;
    qa_resource **resource = &pool->resources;
    while (*resource != NULL) {
        if ((*resource)->references == 1) {
            qa_resource *removed = *resource;
            *resource = removed->next;
            if (removed->archive == NULL) loose_remove(pool, removed);
            qa_resource_release(removed);
        } else resource = &(*resource)->next;
    }
    package **archive = &pool->packages;
    while (*archive != NULL) {
        if ((*archive)->references == 1) {
            package *removed = *archive;
            *archive = removed->next;
            package_release(removed);
        } else archive = &(*archive)->next;
    }
}

qa_vfs *qa_vfs_create(qa_resource_pool *pool, qa_error *error)
{
    if (pool == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS requires a resource pool");
        return NULL;
    }
    qa_vfs *vfs = calloc(1, sizeof(*vfs));
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate VFS");
        return NULL;
    }
    vfs->pool = pool;
    vfs->next_mount = 1;
    pool->references++;
    return vfs;
}

size_t qa_vfs_mount_count(const qa_vfs *vfs)
{
    return vfs == NULL ? 0 : vfs->count;
}

bool qa_vfs_mount_at(const qa_vfs *vfs, size_t index, qa_vfs_mount_info *out)
{
    if (vfs == NULL || out == NULL || index >= vfs->count) return false;
    const mount *source = vfs->mounts[index];
    *out = (qa_vfs_mount_info){
        source->id, source->archive != NULL,
        source->archive == NULL ? QA_ARCHIVE_AUTO : qa_archive_get_kind(source->archive->archive),
        source->comparison, source->writable, source->user_overlay, source->referenced,
        source->archive == NULL ? NULL : &source->archive->digest
    };
    return true;
}

void qa_vfs_clear_references(qa_vfs *vfs)
{
    if (vfs == NULL) return;
    for (size_t i = 0; i < vfs->count; i++) vfs->mounts[i]->referenced = false;
}

qa_vfs *qa_vfs_clone(const qa_vfs *vfs, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return NULL;
    }
    qa_vfs *copy = qa_vfs_create(vfs->pool, error);
    if (copy == NULL) return NULL;
    copy->next_mount = vfs->next_mount;
    copy->next_temporary = vfs->next_temporary;
    if (vfs->count != 0) copy->mounts = calloc(vfs->count, sizeof(*copy->mounts));
    if (vfs->count != 0 && copy->mounts == NULL) goto memory_failure;
    for (size_t i = 0; i < vfs->count; i++) {
        mount *source = malloc(sizeof(*source));
        if (source == NULL) goto memory_failure;
        *source = *vfs->mounts[i];
        qa_fs_file_retain(source->archive_file);
        qa_fs_root_retain(source->root);
        source->referenced = false;
        if (source->archive != NULL) source->archive->references++;
        copy->mounts[copy->count++] = source;
    }
    prefix_order **prefix_tail = &copy->prefixes;
    for (const prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next) {
        prefix_order *item = calloc(1, sizeof(*item));
        if (item == NULL) goto memory_failure;
        *prefix_tail = item;
        prefix_tail = &item->next;
        item->prefix = copy_string(prefix->prefix);
        if (copy->count != 0) item->order = malloc(copy->count * sizeof(*item->order));
        if (item->prefix == NULL || (copy->count != 0 && item->order == NULL)) goto memory_failure;
        if (copy->count != 0) memcpy(item->order, prefix->order, copy->count * sizeof(*item->order));
    }
    resource_link **link_tail = &copy->links;
    for (const resource_link *link = vfs->links; link != NULL; link = link->next) {
        resource_link *item = calloc(1, sizeof(*item));
        if (item == NULL) goto memory_failure;
        *link_tail = item;
        link_tail = &item->next;
        item->source = copy_string(link->source);
        item->target = copy_string(link->target);
        item->mount = link->mount;
        if (item->source == NULL || item->target == NULL) goto memory_failure;
    }
    if (vfs->pure_count != 0) {
        copy->pure = malloc(vfs->pure_count * sizeof(*copy->pure));
        if (copy->pure == NULL) goto memory_failure;
        memcpy(copy->pure, vfs->pure, vfs->pure_count * sizeof(*copy->pure));
        copy->pure_count = vfs->pure_count;
    }
    copy->q3_demo = vfs->q3_demo;
    return copy;
memory_failure:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot clone VFS");
    qa_vfs_destroy(copy);
    return NULL;
}

static void mount_free(mount *source)
{
    if (source->archive != NULL) package_release(source->archive);
    qa_fs_file_close(source->archive_file);
    qa_fs_root_close(source->root);
    free(source);
}

void qa_vfs_destroy(qa_vfs *vfs)
{
    if (vfs == NULL) return;
    for (size_t i = 0; i < vfs->count; i++) mount_free(vfs->mounts[i]);
    free(vfs->mounts);
    prefix_order *prefix = vfs->prefixes;
    while (prefix != NULL) {
        prefix_order *next = prefix->next;
        free(prefix->prefix);
        free(prefix->order);
        free(prefix);
        prefix = next;
    }
    resource_link *link = vfs->links;
    while (link != NULL) {
        resource_link *next = link->next;
        free(link->source);
        free(link->target);
        free(link);
        link = next;
    }
    qa_resource_pool_destroy(vfs->pool);
    free(vfs->pure);
    free(vfs);
}

static package *package_open(qa_resource_pool *pool, const char *path,
                              qa_archive_kind kind, qa_fs_file **out_file,
                              qa_fs_identity *out_identity, qa_error *error)
{
    qa_fs_file *file = NULL;
    qa_fs_identity identity;
    if (!qa_fs_file_open(path, &file, &identity, error))
        return NULL;
    qa_archive_kind extension_kind = kind == QA_ARCHIVE_AUTO ? qa_archive_kind_for_path(path) : kind;
    for (package *archive = pool->packages; archive != NULL; archive = archive->next) {
        qa_archive_kind existing_kind = qa_archive_get_kind(archive->archive);
        bool matches_kind = kind == QA_ARCHIVE_AUTO ?
            (existing_kind == QA_ARCHIVE_PAK || extension_kind == QA_ARCHIVE_AUTO || extension_kind == existing_kind) :
            kind == existing_kind;
        if (qa_fs_identity_equal(&archive->identity, &identity) &&
            matches_kind) {
            archive->references++;
            *out_file = file;
            *out_identity = identity;
            return archive;
        }
    }
    package *archive = calloc(1, sizeof(*archive));
    if (archive == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate archive source");
        qa_fs_file_close(file);
        return NULL;
    }
    archive->identity = identity;
    archive->references = 1;
    if (!qa_fs_file_read_snapshot(file, &identity, &archive->storage, error)) {
        qa_fs_file_close(file);
        package_release(archive);
        return NULL;
    }
    if (kind == QA_ARCHIVE_AUTO) {
        kind = archive->storage.size >= 4 && memcmp(archive->storage.data, "PACK", 4) == 0 ?
            QA_ARCHIVE_PAK : qa_archive_kind_for_path(path);
    }
    qa_sha256((qa_bytes){archive->storage.data, archive->storage.size}, &archive->digest);
    for (package *previous = pool->packages; previous != NULL; previous = previous->next) {
        if ((kind == QA_ARCHIVE_AUTO || qa_archive_get_kind(previous->archive) == kind) &&
            previous->storage.size == archive->storage.size &&
            qa_sha256_equal(&previous->digest, &archive->digest) &&
            memcmp(previous->storage.data, archive->storage.data, archive->storage.size) == 0) {
            package_release(archive);
            previous->references++;
            *out_file = file;
            *out_identity = identity;
            return previous;
        }
    }
    if (!qa_archive_open_memory((qa_bytes){archive->storage.data, archive->storage.size},
                                kind, &archive->archive, error)) {
        qa_fs_file_close(file);
        package_release(archive);
        return NULL;
    }
    size_t member_count = qa_archive_count(archive->archive);
    if (member_count > SIZE_MAX / sizeof(*archive->members)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "archive resource index overflow");
        qa_fs_file_close(file);
        package_release(archive);
        return NULL;
    }
    if (member_count != 0) archive->members = calloc(member_count, sizeof(*archive->members));
    if (member_count != 0 && archive->members == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate archive resource index");
        qa_fs_file_close(file);
        package_release(archive);
        return NULL;
    }
    archive->next = pool->packages;
    pool->packages = archive;
    archive->references++;
    *out_file = file;
    *out_identity = identity;
    return archive;
}

static bool add_mount(qa_vfs *vfs, mount *source, qa_mount_id *out, qa_error *error)
{
    if (vfs->next_mount == 0 || vfs->count == SIZE_MAX ||
        vfs->count + 1 > SIZE_MAX / sizeof(*vfs->mounts) ||
        vfs->count + 1 > SIZE_MAX / sizeof(qa_mount_id)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "mount table overflow");
        return false;
    }
    mount **mounts = realloc(vfs->mounts, (vfs->count + 1) * sizeof(*mounts));
    if (mounts == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow mount table");
        return false;
    }
    vfs->mounts = mounts;
    for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next) {
        qa_mount_id *order = realloc(prefix->order, (vfs->count + 1) * sizeof(*order));
        if (order == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow prefix order");
            return false;
        }
        prefix->order = order;
    }
    source->id = vfs->next_mount++;
    for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next) {
        prefix->order[vfs->count] = source->id;
    }
    vfs->mounts[vfs->count++] = source;
    prioritize_mounts(vfs, vfs->mounts);
    for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next)
        prioritize_ids(vfs, prefix->order);
    *out = source->id;
    return true;
}

bool qa_vfs_mount_archive(qa_vfs *vfs, const char *path, qa_archive_kind kind,
                          qa_archive_comparison comparison, qa_mount_id *out,
                          qa_error *error)
{
    if (vfs == NULL || path == NULL || path[0] == '\0' || out == NULL ||
        !valid_comparison(comparison) || kind < QA_ARCHIVE_AUTO || kind > QA_ARCHIVE_KPF) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid archive mount");
        return false;
    }
    mount *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate mount");
        return false;
    }
    source->archive = package_open(vfs->pool, path, kind,
                                   &source->archive_file,
                                   &source->identity, error);
    if (source->archive == NULL) {
        free(source);
        return false;
    }
    source->comparison = comparison;
    if (vfs->q3_demo && !demo_package_allowed(source->archive, error)) {
        mount_free(source);
        return false;
    }
    if (!add_mount(vfs, source, out, error)) {
        mount_free(source);
        return false;
    }
    return true;
}

bool qa_vfs_mount_directory(qa_vfs *vfs, const char *path,
                            qa_archive_comparison comparison, bool writable,
                            qa_mount_id *out, qa_error *error)
{
    if (vfs == NULL || path == NULL || path[0] == '\0' || out == NULL || !valid_comparison(comparison)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid directory mount");
        return false;
    }
    qa_fs_root *root = NULL;
    if (!qa_fs_root_open(path, &root, error))
        return false;
    mount *source = calloc(1, sizeof(*source));
    if (source == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate mount");
        qa_fs_root_close(root);
        return false;
    }
    source->root = root;
    source->comparison = comparison;
    source->writable = writable;
    if (!add_mount(vfs, source, out, error)) {
        mount_free(source);
        return false;
    }
    return true;
}

static mount *find_mount(const qa_vfs *vfs, qa_mount_id id)
{
    if (vfs == NULL) return NULL;
    for (size_t i = 0; i < vfs->count; i++) {
        if (vfs->mounts[i]->id == id) return vfs->mounts[i];
    }
    return NULL;
}

const qa_sha256_digest *qa_vfs_archive_digest(const qa_vfs *vfs, qa_mount_id id)
{
    const mount *source = find_mount(vfs, id);
    return source != NULL && source->archive != NULL ? &source->archive->digest : NULL;
}

const qa_archive *qa_vfs_archive(const qa_vfs *vfs, qa_mount_id id)
{
    const mount *source = find_mount(vfs, id);
    return source != NULL && source->archive != NULL ? source->archive->archive : NULL;
}

static bool archive_checksums(const package *archive, uint32_t feed,
                               uint32_t *checksum, uint32_t *pure_checksum,
                               qa_error *error)
{
    if (qa_archive_get_kind(archive->archive) == QA_ARCHIVE_PAK) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 checksums require a ZIP-family archive");
        return false;
    }
    qa_md4_context normal, pure;
    qa_md4_init(&normal);
    qa_md4_init(&pure);
    uint8_t word[4];
    qa_store_u32le(word, feed);
    qa_md4_update(&pure, (qa_bytes){word, sizeof(word)});
    for (size_t i = 0; i < qa_archive_count(archive->archive); i++) {
        const qa_archive_entry *entry = qa_archive_entry_at(archive->archive, i);
        if (entry->size == 0) continue;
        qa_store_u32le(word, entry->crc32);
        qa_md4_update(&normal, (qa_bytes){word, sizeof(word)});
        qa_md4_update(&pure, (qa_bytes){word, sizeof(word)});
    }
    qa_md4_digest first, second;
    qa_md4_final(&normal, &first);
    qa_md4_final(&pure, &second);
    if (checksum != NULL) *checksum = qa_md4_fold(&first);
    if (pure_checksum != NULL) *pure_checksum = qa_md4_fold(&second);
    return true;
}

bool qa_vfs_archive_checksums(qa_vfs *vfs, qa_mount_id id, uint32_t feed,
                               uint32_t *checksum, uint32_t *pure_checksum,
                               qa_error *error)
{
    mount *source = vfs == NULL ? NULL : find_mount(vfs, id);
    if (source == NULL || source->archive == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "checksums require an archive mount");
        return false;
    }
    return archive_checksums(source->archive, feed, checksum, pure_checksum, error);
}

static bool demo_package_allowed(const package *archive, qa_error *error)
{
    uint32_t checksum;
    if (qa_archive_get_kind(archive->archive) != QA_ARCHIVE_PK3) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "restricted Q3 content requires PK3 archives");
        return false;
    }
    if (!archive_checksums(archive, 0, &checksum, NULL, error)) return false;
    if (checksum != UINT32_C(437558517)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid Q3 demo package checksum: %" PRIu32, checksum);
        return false;
    }
    return true;
}

static void prioritize_mounts(const qa_vfs *vfs, mount **order)
{
    size_t first = 0;
    for (size_t i = 0; i < vfs->pure_count; i++) {
        for (size_t j = first; j < vfs->count; j++) {
            if (order[j]->archive == NULL || !qa_sha256_equal(&order[j]->archive->digest, &vfs->pure[i])) continue;
            mount *selected = order[j];
            memmove(order + first + 1, order + first, (j - first) * sizeof(*order));
            order[first++] = selected;
            break;
        }
    }
}

static void prioritize_ids(qa_vfs *vfs, qa_mount_id *order)
{
    size_t first = 0;
    for (size_t i = 0; i < vfs->pure_count; i++) {
        for (size_t j = first; j < vfs->count; j++) {
            mount *source = find_mount(vfs, order[j]);
            if (source->archive == NULL || !qa_sha256_equal(&source->archive->digest, &vfs->pure[i])) continue;
            qa_mount_id selected = order[j];
            memmove(order + first + 1, order + first, (j - first) * sizeof(*order));
            order[first++] = selected;
            break;
        }
    }
}

bool qa_vfs_set_restrictions(qa_vfs *vfs, const qa_sha256_digest *archives,
                              size_t count, bool q3_demo, qa_error *error)
{
    if (vfs == NULL || (count != 0 && archives == NULL) || count > SIZE_MAX / sizeof(*archives)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid content restrictions");
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        bool found = false;
        for (size_t j = 0; j < vfs->count; j++) {
            package *archive = vfs->mounts[j]->archive;
            if (archive != NULL && qa_sha256_equal(&archive->digest, &archives[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, i, "required pure archive is not mounted");
            return false;
        }
    }
    if (q3_demo) {
        for (size_t i = 0; i < vfs->count; i++) {
            package *archive = vfs->mounts[i]->archive;
            if (archive != NULL && !demo_package_allowed(archive, error)) return false;
        }
    }
    qa_sha256_digest *copy = count == 0 ? NULL : malloc(count * sizeof(*copy));
    if (count != 0 && copy == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate pure archive policy");
        return false;
    }
    if (count != 0) memcpy(copy, archives, count * sizeof(*copy));
    free(vfs->pure);
    vfs->pure = copy;
    vfs->pure_count = count;
    vfs->q3_demo = q3_demo;
    prioritize_mounts(vfs, vfs->mounts);
    for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next)
        prioritize_ids(vfs, prefix->order);
    return true;
}

static bool source_allowed(const qa_vfs *vfs, const mount *source, const char *path)
{
    if (source->user_overlay) return true;
    if (source->archive != NULL) {
        if (vfs->pure_count == 0) return true;
        for (size_t i = 0; i < vfs->pure_count; i++)
            if (qa_sha256_equal(&source->archive->digest, &vfs->pure[i])) return true;
        return false;
    }
    if (vfs->pure_count == 0 && !vfs->q3_demo) return true;
    const char *extension = strrchr(path, '.');
    if (extension == NULL) return false;
    static const char *const allowed[] = {".cfg", ".menu", ".game", ".dm_68", ".dat"};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        if (qa_archive_paths_equal(extension, allowed[i], QA_ARCHIVE_CASE_INSENSITIVE)) return true;
    return false;
}

bool qa_vfs_unmount(qa_vfs *vfs, qa_mount_id id, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return false;
    }
    for (size_t i = 0; i < vfs->count; i++) {
        if (vfs->mounts[i]->id != id) continue;
        package *archive = vfs->mounts[i]->archive;
        if (archive != NULL) {
            for (size_t p = 0; p < vfs->pure_count; p++) {
                if (!qa_sha256_equal(&archive->digest, &vfs->pure[p])) continue;
                bool retained = false;
                for (size_t j = 0; j < vfs->count; j++) {
                    package *other = vfs->mounts[j]->archive;
                    if (j != i && other != NULL && qa_sha256_equal(&other->digest, &vfs->pure[p])) retained = true;
                }
                if (!retained) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "cannot unmount a required pure archive");
                    return false;
                }
            }
        }
        mount_free(vfs->mounts[i]);
        memmove(vfs->mounts + i, vfs->mounts + i + 1, (vfs->count - i - 1) * sizeof(*vfs->mounts));
        for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next) {
            for (size_t j = 0; j < vfs->count; j++) {
                if (prefix->order[j] == id) {
                    memmove(prefix->order + j, prefix->order + j + 1,
                            (vfs->count - j - 1) * sizeof(*prefix->order));
                    break;
                }
            }
        }
        vfs->count--;
        resource_link **link = &vfs->links;
        while (*link != NULL) {
            if ((*link)->mount == id) {
                resource_link *removed = *link;
                *link = removed->next;
                free(removed->source);
                free(removed->target);
                free(removed);
            } else link = &(*link)->next;
        }
        return true;
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "unknown mount: %" PRIu64, id);
    return false;
}

static bool valid_order(qa_vfs *vfs, const qa_mount_id *order, size_t count, qa_error *error)
{
    if (count != vfs->count || (order == NULL && count != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "mount order must include every mount");
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (find_mount(vfs, order[i]) == NULL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "mount order contains an unknown ID");
            return false;
        }
        for (size_t j = 0; j < i; j++) {
            if (order[i] == order[j]) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i, "mount order repeats an ID");
                return false;
            }
        }
    }
    return true;
}

bool qa_vfs_set_order(qa_vfs *vfs, const qa_mount_id *order, size_t count, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return false;
    }
    if (!valid_order(vfs, order, count, error)) return false;
    if (count == 0) return true;
    mount **sorted = malloc(count * sizeof(*sorted));
    if (sorted == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate mount order");
        return false;
    }
    for (size_t i = 0; i < count; i++) sorted[i] = find_mount(vfs, order[i]);
    free(vfs->mounts);
    vfs->mounts = sorted;
    prioritize_mounts(vfs, vfs->mounts);
    return true;
}

bool qa_vfs_set_prefix_order(qa_vfs *vfs, const char *path,
                             const qa_mount_id *order, size_t count, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return false;
    }
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    prefix_order **position = &vfs->prefixes;
    while (*position != NULL && !qa_archive_paths_equal((*position)->prefix, normalized,
                                                        QA_ARCHIVE_CASE_INSENSITIVE)) {
        position = &(*position)->next;
    }
    if (order == NULL) {
        free(normalized);
        if (*position != NULL) {
            prefix_order *previous = *position;
            *position = previous->next;
            free(previous->prefix);
            free(previous->order);
            free(previous);
        }
        return true;
    }
    if (!valid_order(vfs, order, count, error)) {
        free(normalized);
        return false;
    }
    qa_mount_id *copy = count == 0 ? NULL : malloc(count * sizeof(*copy));
    prefix_order *rule = *position;
    if ((count != 0 && copy == NULL) || (rule == NULL && (rule = calloc(1, sizeof(*rule))) == NULL)) {
        free(copy);
        free(normalized);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate prefix order");
        return false;
    }
    if (count != 0) memcpy(copy, order, count * sizeof(*copy));
    free(rule->prefix);
    free(rule->order);
    rule->prefix = normalized;
    rule->order = copy;
    *position = rule;
    prioritize_ids(vfs, rule->order);
    return true;
}

bool qa_vfs_set_user_overlay(qa_vfs *vfs, qa_mount_id id, bool enabled, qa_error *error)
{
    mount *source = vfs == NULL ? NULL : find_mount(vfs, id);
    if (source == NULL || source->archive != NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "user overlay requires a loose mount");
        return false;
    }
    source->user_overlay = enabled;
    return true;
}

static char *link_prefix(const char *prefix, bool allow_empty, qa_error *error)
{
    if (prefix == NULL || (!allow_empty && prefix[0] == '\0')) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid resource link prefix");
        return NULL;
    }
    size_t length = strlen(prefix);
    if (length == 0) {
        char *empty = copy_string("");
        if (empty == NULL) qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource link");
        return empty;
    }
    bool trailing = prefix[length - 1] == '/' || prefix[length - 1] == '\\';
    char *input = copy_string_n(prefix, length - (trailing ? 1u : 0u));
    if (input == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource link");
        return NULL;
    }
    char *normalized = qa_vfs_normalize_path(input, error);
    free(input);
    if (normalized == NULL || !trailing) return normalized;
    size_t normalized_length = strlen(normalized);
    char *result = realloc(normalized, normalized_length + 2);
    if (result == NULL) {
        free(normalized);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource link");
        return NULL;
    }
    result[normalized_length] = '/';
    result[normalized_length + 1] = '\0';
    return result;
}

bool qa_vfs_set_link(qa_vfs *vfs, const char *source_prefix, qa_mount_id id,
                     const char *target_prefix, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return false;
    }
    char *source = link_prefix(source_prefix, false, error);
    if (source == NULL) return false;
    resource_link **position = &vfs->links;
    while (*position != NULL && strcmp((*position)->source, source) != 0) position = &(*position)->next;
    if (target_prefix == NULL) {
        free(source);
        if (*position != NULL) {
            resource_link *removed = *position;
            *position = removed->next;
            free(removed->source);
            free(removed->target);
            free(removed);
        }
        return true;
    }
    mount *destination = find_mount(vfs, id);
    if (destination == NULL || destination->archive != NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource link requires a loose mount");
        free(source);
        return false;
    }
    char *target = link_prefix(target_prefix, true, error);
    if (target == NULL) {
        free(source);
        return false;
    }
    resource_link *link = *position;
    if (link == NULL) link = calloc(1, sizeof(*link));
    if (link == NULL) {
        free(source);
        free(target);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource link");
        return false;
    }
    free(link->source);
    free(link->target);
    link->source = source;
    link->target = target;
    link->mount = id;
    *position = link;
    return true;
}

static bool loose_name_equal(const char *left, const char *right,
                             void *context)
{
    const qa_archive_comparison *comparison = context;
    return qa_archive_paths_equal(left, right, *comparison);
}

static char *resolve_spelling(const mount *source, const char *path,
                              qa_error *error)
{
    qa_archive_comparison comparison = source->comparison;
    char *resolved = NULL;
    if (!qa_fs_root_resolve(source->root, path, loose_name_equal,
                            &comparison, true, &resolved, error))
        return NULL;
    return resolved;
}

static qa_resource *new_resource(qa_resource_pool *pool, const char *path, qa_error *error)
{
    if (pool->next_resource == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "resource identity overflow");
        return NULL;
    }
    qa_resource *resource = calloc(1, sizeof(*resource));
    if (resource != NULL) resource->path = copy_string(path);
    if (resource == NULL || resource->path == NULL) {
        free(resource);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource");
        return NULL;
    }
    resource->references = 1;
    resource->id = pool->next_resource++;
    return resource;
}

static bool cache_resource(qa_resource_pool *pool, qa_resource *resource, qa_error *error)
{
    if (resource->archive != NULL) resource->archive->members[resource->ordinal] = resource;
    else {
        if (!loose_reserve(pool, error)) return false;
        size_t bucket = identity_bucket(&resource->identity, pool->loose_bucket_count);
        resource->identity_next = pool->loose_buckets[bucket];
        pool->loose_buckets[bucket] = resource;
        pool->loose_count++;
    }
    resource->next = pool->resources;
    pool->resources = resource;
    qa_resource_retain(resource);
    return true;
}

static bool acquire_archive(qa_resource_pool *pool, const mount *source,
                             const char *path, qa_resource **out, qa_error *error)
{
    package *archive = source->archive;
    bool unchanged = false;
    if (!qa_fs_file_path_unchanged(source->archive_file, &source->identity,
                                   &unchanged, error))
        return false;
    if (!unchanged) {
        qa_error_set(error, QA_ERROR_IO, 0, "mounted archive changed: %s", path);
        return false;
    }
    const qa_archive_entry *selected = NULL;
    size_t start = 0;
    for (;;) {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find_normalized(archive->archive, path, source->comparison, start, &entry, error)) return false;
        if (entry == NULL) break;
        if (!entry->is_directory) {
            selected = entry;
            if (qa_archive_get_kind(archive->archive) == QA_ARCHIVE_PAK) break;
        }
        start = entry->ordinal + 1;
    }
    if (selected == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "archive member not found: %s", path);
        return false;
    }
    qa_resource *cached = archive->members[selected->ordinal];
    if (cached != NULL) {
        qa_resource_retain(cached);
        *out = cached;
        return true;
    }
    qa_resource *resource = new_resource(pool, selected->path, error);
    if (resource == NULL) return false;
    if (!qa_archive_read(archive->archive, selected->ordinal, &resource->data, error)) {
        qa_resource_release(resource);
        return false;
    }
    resource->archive = archive;
    archive->references++;
    resource->ordinal = selected->ordinal;
    qa_sha256(resource->data.bytes, &resource->digest);
    if (!cache_resource(pool, resource, error)) {
        qa_resource_release(resource);
        return false;
    }
    *out = resource;
    return true;
}

static bool acquire_loose(qa_resource_pool *pool, const mount *source,
                           const char *path, qa_resource **out, qa_error *error)
{
    char *resolved = resolve_spelling(source, path, error);
    if (resolved == NULL) return false;
    qa_fs_file *file = NULL;
    qa_fs_identity identity;
    if (!qa_fs_root_file_open(source->root, resolved, &file, &identity,
                              error)) {
        free(resolved);
        return false;
    }
    qa_resource *cached = loose_lookup(pool, &identity);
    if (cached != NULL) {
        qa_fs_file_close(file);
        free(resolved);
        qa_resource_retain(cached);
        *out = cached;
        return true;
    }
    qa_resource *resource = new_resource(pool, resolved, error);
    free(resolved);
    if (resource == NULL) {
        qa_fs_file_close(file);
        return false;
    }
    if (!qa_fs_file_read_snapshot(file, &identity,
                                  &resource->data.owned, error)) {
        qa_fs_file_close(file);
        qa_resource_release(resource);
        return false;
    }
    qa_fs_file_close(file);
    resource->data.bytes = (qa_bytes){resource->data.owned.data, resource->data.owned.size};
    resource->identity = identity;
    qa_sha256(resource->data.bytes, &resource->digest);
    if (!cache_resource(pool, resource, error)) {
        qa_resource_release(resource);
        return false;
    }
    *out = resource;
    return true;
}

static bool acquire_mount(qa_vfs *vfs, mount *source,
                           const char *path, qa_resource **out, qa_error *error)
{
    if (!source_allowed(vfs, source, path)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "resource excluded by content policy: %s", path);
        return false;
    }
    bool result = source->archive != NULL ? acquire_archive(vfs->pool, source, path, out, error) :
                                           acquire_loose(vfs->pool, source, path, out, error);
    if (result && source->archive != NULL) source->referenced = true;
    return result;
}

bool qa_vfs_acquire_from(qa_vfs *vfs, qa_mount_id id, const char *path,
                         qa_resource **out, qa_error *error)
{
    if (out != NULL) *out = NULL;
    if (vfs == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid resource acquisition");
        return false;
    }
    mount *source = find_mount(vfs, id);
    if (source == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "unknown mount: %" PRIu64, id);
        return false;
    }
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    bool result = acquire_mount(vfs, source, normalized, out, error);
    free(normalized);
    return result;
}

bool qa_vfs_acquire(qa_vfs *vfs, const char *path, qa_resource **out,
                    qa_mount_id *out_mount, qa_error *error)
{
    return qa_vfs_acquire_filtered(vfs, path, NULL, NULL, out, out_mount, error);
}

bool qa_vfs_acquire_filtered(qa_vfs *vfs, const char *path,
                              qa_vfs_accept_mount accept, void *context,
                              qa_resource **out, qa_mount_id *out_mount,
                              qa_error *error)
{
    if (out != NULL) *out = NULL;
    if (vfs == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid resource acquisition");
        return false;
    }
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    qa_error local = {0};
    for (size_t i = 0; i < vfs->count; i++) {
        mount *source = vfs->mounts[i];
        if (!source->user_overlay) continue;
        if (accept != NULL && !accept(source->id, context)) continue;
        if (acquire_mount(vfs, source, normalized, out, &local)) {
            if (out_mount != NULL) *out_mount = source->id;
            free(normalized);
            return true;
        }
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error != NULL) *error = local;
            free(normalized);
            return false;
        }
    }
    for (resource_link *link = vfs->links; link != NULL; link = link->next) {
        size_t source_length = strlen(link->source);
        if (strncmp(normalized, link->source, source_length) != 0) continue;
        if (accept != NULL && !accept(link->mount, context)) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "resource link mount was excluded");
            free(normalized);
            return false;
        }
        const char *suffix = normalized + source_length;
        size_t target_length = strlen(link->target), suffix_length = strlen(suffix);
        if (target_length > SIZE_MAX - suffix_length - 1) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "resource link path overflow");
            free(normalized);
            return false;
        }
        char *target = malloc(target_length + suffix_length + 1);
        if (target == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate resource link path");
            free(normalized);
            return false;
        }
        memcpy(target, link->target, target_length);
        memcpy(target + target_length, suffix, suffix_length + 1);
        free(normalized);
        normalized = qa_vfs_normalize_path(target, error);
        free(target);
        if (normalized == NULL) return false;
        mount *source = find_mount(vfs, link->mount);
        bool result = acquire_mount(vfs, source, normalized, out, error);
        if (result && out_mount != NULL) *out_mount = source->id;
        free(normalized);
        return result;
    }
    qa_mount_id *order = NULL;
    size_t path_length = strlen(normalized);
    for (prefix_order *prefix = vfs->prefixes; prefix != NULL; prefix = prefix->next) {
        size_t length = strlen(prefix->prefix);
        if (length < path_length && normalized[length] == '/') {
            normalized[length] = '\0';
            bool matches = qa_archive_paths_equal(normalized, prefix->prefix, QA_ARCHIVE_CASE_INSENSITIVE);
            normalized[length] = '/';
            if (matches) {
                order = prefix->order;
                break;
            }
        }
    }
    for (size_t i = 0; i < vfs->count; i++) {
        mount *source = order == NULL ? vfs->mounts[i] : find_mount(vfs, order[i]);
        if (source->user_overlay) continue;
        if (accept != NULL && !accept(source->id, context)) continue;
        if (acquire_mount(vfs, source, normalized, out, &local)) {
            if (out_mount != NULL) *out_mount = source->id;
            free(normalized);
            return true;
        }
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error != NULL) *error = local;
            free(normalized);
            return false;
        }
    }
    free(normalized);
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "resource not found: %s", path);
    return false;
}

void qa_vfs_listing_free(qa_vfs_listing *listing)
{
    if (listing == NULL) return;
    for (size_t i = 0; i < listing->count; i++) free(listing->names[i]);
    free(listing->names);
    *listing = (qa_vfs_listing){0};
}

static bool listing_add(qa_vfs_listing *listing, size_t *capacity,
                         const char *name, qa_error *error)
{
    if (listing->count == 4095) return true;
    for (size_t i = 0; i < listing->count; i++) {
        if (qa_archive_paths_equal(listing->names[i], name, QA_ARCHIVE_CASE_INSENSITIVE)) return true;
    }
    if (listing->count == *capacity) {
        size_t next = *capacity == 0 ? 32 : *capacity * 2;
        if (next > 4095) next = 4095;
        char **names = realloc(listing->names, next * sizeof(*names));
        if (names == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate file listing");
            return false;
        }
        listing->names = names;
        *capacity = next;
    }
    char *copy = copy_string(name);
    if (copy == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate file listing name");
        return false;
    }
    listing->names[listing->count++] = copy;
    return true;
}

static size_t path_depth(const char *path)
{
    size_t depth = 0;
    for (; *path != '\0'; path++) if (*path == '/' || *path == '\\') depth++;
    return depth;
}

static bool has_suffix(const char *name, const char *extension)
{
    size_t length = strlen(name), suffix = strlen(extension);
    return length >= suffix && qa_archive_paths_equal(name + length - suffix, extension,
                                                      QA_ARCHIVE_CASE_INSENSITIVE);
}

bool qa_vfs_list(qa_vfs *vfs, const char *path, const char *extension,
                  qa_vfs_listing *out, qa_error *error)
{
    if (out != NULL) *out = (qa_vfs_listing){0};
    if (vfs == NULL || out == NULL || path == NULL || extension == NULL || strlen(path) >= 256) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid file listing");
        return false;
    }
    size_t length = strlen(path);
    if (length != 0 && (path[length - 1] == '/' || path[length - 1] == '\\')) length--;
    char directory[256];
    memcpy(directory, path, length);
    directory[length] = '\0';
    if (length != 0) {
        char *normalized = qa_vfs_normalize_path(directory, error);
        if (normalized == NULL) return false;
        memcpy(directory, normalized, length + 1);
        free(normalized);
    }
    qa_mount_id *order = NULL;
    for (prefix_order *rule = vfs->prefixes; rule != NULL; rule = rule->next) {
        size_t prefix_length = strlen(rule->prefix);
        if (prefix_length > length || (prefix_length < length && directory[prefix_length] != '/')) continue;
        char candidate[256];
        memcpy(candidate, directory, prefix_length);
        candidate[prefix_length] = '\0';
        if (qa_archive_paths_equal(candidate, rule->prefix, QA_ARCHIVE_CASE_INSENSITIVE)) {
            order = rule->order;
            break;
        }
    }
    qa_vfs_listing listing = {0};
    size_t capacity = 0;
    for (size_t i = 0; i < vfs->count; i++) {
        mount *source = order == NULL ? vfs->mounts[i] : find_mount(vfs, order[i]);
        if (source->archive != NULL) {
            size_t count = qa_archive_count(source->archive->archive);
            for (size_t j = 0; j < count; j++) {
                const qa_archive_entry *entry = qa_archive_entry_at(source->archive->archive, j);
                const char *name = entry->path;
                if (!source_allowed(vfs, source, name)) continue;
                const char *last = strrchr(name, '/');
                size_t separator = last == NULL ? 0 : (size_t)(last - name);
                if (path_depth(name) > path_depth(path) + 2 || length > separator || !has_suffix(name, extension)) continue;
                char candidate[256];
                memcpy(candidate, name, length);
                candidate[length] = '\0';
                if (!qa_archive_paths_equal(candidate, directory, QA_ARCHIVE_CASE_INSENSITIVE)) continue;
                if (!listing_add(&listing, &capacity, name + (length == 0 ? 0 : length + 1), error)) goto fail;
            }
        } else {
            if (!source->user_overlay && (vfs->pure_count != 0 || vfs->q3_demo)) continue;
            qa_error local = {0};
            char *resolved = resolve_spelling(source, directory, &local);
            if (resolved == NULL) {
                if (local.code == QA_ERROR_NOT_FOUND) continue;
                if (error != NULL) *error = local;
                goto fail;
            }
            qa_fs_listing entries = {0};
            bool opened = qa_fs_root_list(source->root, resolved,
                                          &entries, &local);
            free(resolved);
            if (!opened) {
                if (local.code == QA_ERROR_NOT_FOUND) continue;
                if (error != NULL) *error = local;
                goto fail;
            }
            for (size_t j = 0; j < entries.count; ++j) {
                const qa_fs_entry *entry = &entries.entries[j];
                bool directories = strcmp(extension, "/") == 0;
                if (entry->kind == QA_FS_LINK || entry->kind == QA_FS_OTHER
                    || directories != (entry->kind == QA_FS_DIRECTORY)
                    || (!directories
                        && (entry->kind != QA_FS_REGULAR
                            || !has_suffix(entry->name, extension))))
                    continue;
                if (!listing_add(&listing, &capacity, entry->name, error)) {
                    qa_fs_listing_free(&entries);
                    goto fail;
                }
            }
            qa_fs_listing_free(&entries);
        }
    }
    *out = listing;
    return true;
fail:
    qa_vfs_listing_free(&listing);
    return false;
}

static mount *writable_mount(qa_vfs *vfs, qa_mount_id id, qa_error *error)
{
    if (vfs == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS is NULL");
        return NULL;
    }
    mount *source = find_mount(vfs, id);
    if (source == NULL || source->archive != NULL || !source->writable) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "mount is not a writable directory");
        return NULL;
    }
    return source;
}

bool qa_vfs_write(qa_vfs *vfs, qa_mount_id id, const char *path,
                  qa_bytes bytes, qa_error *error)
{
    mount *source = writable_mount(vfs, id, error);
    if (source == NULL) return false;
    if ((bytes.data == NULL && bytes.size != 0) || bytes.size > (size_t)PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid write buffer");
        return false;
    }
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    uint64_t nonce = vfs->next_temporary++;
    bool success = qa_fs_root_replace(source->root, normalized, bytes,
                                      nonce, error);
    free(normalized);
    return success;
}

bool qa_vfs_remove(qa_vfs *vfs, qa_mount_id id, const char *path, qa_error *error)
{
    mount *source = writable_mount(vfs, id, error);
    if (source == NULL) return false;
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    bool success = qa_fs_root_remove(source->root, normalized, error);
    free(normalized);
    return success;
}

static bool open_stream(qa_vfs *vfs, qa_mount_id id, const char *path,
                         qa_vfs_write_mode mode, bool resume, uint64_t position,
                         qa_vfs_file **out, qa_error *error)
{
    if (out != NULL) *out = NULL;
    if (out == NULL || mode < QA_VFS_WRITE || mode > QA_VFS_APPEND_SYNC || position > INT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid writable file state");
        return false;
    }
    mount *source = writable_mount(vfs, id, error);
    if (source == NULL) return false;
    char *normalized = qa_vfs_normalize_path(path, error);
    if (normalized == NULL) return false;
    qa_vfs_file *file = malloc(sizeof(*file));
    if (file == NULL) {
        free(normalized);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot allocate writable file");
        return false;
    }
    bool append = mode != QA_VFS_WRITE;
    qa_fs_stream_mode fs_mode = mode == QA_VFS_WRITE ? QA_FS_STREAM_WRITE
        : mode == QA_VFS_APPEND ? QA_FS_STREAM_APPEND
                                : QA_FS_STREAM_APPEND_SYNC;
    qa_fs_stream *stream = NULL;
    uint64_t initial_size = 0;
    if (!qa_fs_root_stream_open(source->root, normalized, fs_mode, resume,
                                &stream, &initial_size, error)) {
        free(file);
        free(normalized);
        return false;
    }
    *file = (qa_vfs_file){stream, normalized, mode,
                          resume ? position : (append ? initial_size : 0)};
    *out = file;
    return true;
}

bool qa_vfs_file_open(qa_vfs *vfs, qa_mount_id id, const char *path,
                       qa_vfs_write_mode mode, qa_vfs_file **out, qa_error *error)
{
    return open_stream(vfs, id, path, mode, false, 0, out, error);
}

bool qa_vfs_file_resume(qa_vfs *vfs, qa_mount_id id,
                         const qa_vfs_file_state *state, qa_vfs_file **out,
                         qa_error *error)
{
    if (state == NULL) {
        if (out != NULL) *out = NULL;
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "writable checkpoint is NULL");
        return false;
    }
    return open_stream(vfs, id, state->path, state->mode, true, state->position, out, error);
}

bool qa_vfs_file_write(qa_vfs_file *file, qa_bytes bytes, size_t *written, qa_error *error)
{
    if (written != NULL) *written = 0;
    if (file == NULL || (bytes.size != 0 && bytes.data == NULL) || bytes.size > (size_t)PTRDIFF_MAX ||
        (file != NULL && file->mode == QA_VFS_WRITE && (uint64_t)bytes.size > (uint64_t)INT64_MAX - file->position)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid writable file buffer or position");
        return false;
    }
    size_t amount = 0;
    uint64_t resulting_size = file->position;
    bool ok = qa_fs_stream_write(file->stream, bytes, file->position,
                                 &amount, &resulting_size, error);
    if (written != NULL) *written = amount;
    if (file->mode == QA_VFS_WRITE) {
        file->position += amount;
    } else if (bytes.size != 0 && ok) {
        file->position = resulting_size;
    } else if (amount != 0) {
        qa_error ignored = {0};
        uint64_t current;
        if (qa_fs_stream_size(file->stream, &current, &ignored))
            file->position = current;
    }
    return ok;
}

bool qa_vfs_file_seek(qa_vfs_file *file, int64_t offset,
                       qa_vfs_seek_origin origin, qa_error *error)
{
    if (file == NULL || origin < QA_VFS_SEEK_SET || origin > QA_VFS_SEEK_END) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid writable seek");
        return false;
    }
    uint64_t base = 0;
    if (origin == QA_VFS_SEEK_CURRENT) base = file->position;
    else if (origin == QA_VFS_SEEK_END) {
        if (!qa_fs_stream_size(file->stream, &base, error))
            return false;
    }
    uint64_t magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
    if ((offset < 0 && magnitude > base) ||
        (offset >= 0 && (base > INT64_MAX || magnitude > (uint64_t)INT64_MAX - base))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "writable seek is outside the supported range");
        return false;
    }
    file->position = offset < 0 ? base - magnitude : base + magnitude;
    return true;
}

qa_vfs_file_state qa_vfs_file_capture(const qa_vfs_file *file)
{
    return file == NULL ? (qa_vfs_file_state){0} :
        (qa_vfs_file_state){file->path, file->mode, file->position};
}

void qa_vfs_file_close(qa_vfs_file *file)
{
    if (file == NULL) return;
    qa_fs_stream_close(file->stream);
    free(file->path);
    free(file);
}
