#include "vfs_private.h"
#include "qa/binary.h"
#include "qa/filesystem.h"
#include "qa/source_save.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

void vfs_package_release(package *archive)
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
    vfs_package_release(resource->archive);
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

void qa_resource_pool_retain(qa_resource_pool *pool)
{
    if (pool != NULL) ++pool->references;
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
        vfs_package_release(archive);
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
            vfs_package_release(removed);
        } else archive = &(*archive)->next;
    }
}

const qa_resource *qa_resource_pool_find(const qa_resource_pool *pool, uint64_t id)
{
    if (pool == NULL || id == 0) return NULL;
    for (const qa_resource *resource = pool->resources; resource != NULL; resource = resource->next)
        if (resource->id == id) return resource;
    return NULL;
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
    vfs->references = 1;
    vfs->next_mount = 1;
    vfs->read_generation = 1;
    pool->references++;
    return vfs;
}

size_t qa_vfs_mount_count(const qa_vfs *vfs)
{
    return vfs == NULL ? 0 : vfs->count;
}

bool qa_vfs_mount_id_was_issued(const qa_vfs *vfs, qa_mount_id id)
{
    return vfs && id > 0 && (!vfs->next_mount || id < vfs->next_mount);
}

bool qa_vfs_mount_at(const qa_vfs *vfs, size_t index, qa_vfs_mount_info *out)
{
    if (vfs == NULL || out == NULL || index >= vfs->count) return false;
    const mount *source = vfs->mounts[index];
    *out = (qa_vfs_mount_info){
        source->id, source->archive != NULL,
        source->archive == NULL ? QA_ARCHIVE_AUTO : qa_archive_get_kind(source->archive->archive),
        source->comparison, source->writable, source->user_overlay, source->referenced,
        source->archive == NULL ? NULL : &source->archive->digest, source->q3_demo
    };
    return true;
}
bool qa_vfs_resource_origin_read(const qa_vfs *vfs, qa_mount_id id,
    const qa_resource *resource, qa_vfs_resource_origin *out)
{
    if (!vfs || !resource || !out ||
        qa_resource_pool_find(vfs->pool, qa_resource_id(resource)) != resource) return false;
    for (const resource_origin *row = vfs->origins; row; row = row->next)
        if (row->mount == id && row->resource == qa_resource_id(resource)) {
            *out = row->receipt; return true;
        }
    return false;
}
bool vfs_origin_record(qa_vfs *vfs, const mount *source,
    const qa_resource *resource, qa_error *error)
{
    qa_vfs_resource_origin existing;
    if (qa_vfs_resource_origin_read(vfs, source->id, resource, &existing)) return true;
    resource_origin *row = calloc(1, sizeof(*row));
    char *path = copy_string(source->path);
    if (!row || !path) {
        free(row); free(path);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining successful resource origin"); return false;
    }
    row->mount = source->id; row->resource = qa_resource_id(resource);
    row->receipt.mount_path = path; row->receipt.mount_identity = source->identity;
    row->receipt.comparison = source->comparison;
    row->receipt.archive = source->archive != NULL;
    if (source->archive) {
        row->receipt.archive_digest = source->archive->digest;
        row->receipt.format = qa_archive_get_kind(source->archive->archive);
    } else if (!source->root || !qa_fs_root_reference_read(source->root, &row->receipt.root_reference)) {
        free(path); free(row);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Resource origin lost its admitted native root"); return false;
    }
    row->next = vfs->origins; vfs->origins = row; return true;
}

void qa_vfs_clear_references(qa_vfs *vfs)
{
    if (vfs == NULL) return;
    for (size_t i = 0; i < vfs->count; i++) vfs->mounts[i]->referenced = false;
    for (size_t i = 0; i < vfs->read_count; ++i) {
        qa_resource_release((qa_resource *)vfs->reads[i].resource); free((char *)vfs->reads[i].path);
        free((char *)vfs->reads[i].lookup_path); free((char *)vfs->reads[i].link_source); free((char *)vfs->reads[i].link_target);
        free((void *)vfs->reads[i].opening.order); free((char *)vfs->reads[i].opening.prefix);
    }
    free(vfs->reads); free(vfs->read_slots);
    vfs->reads = NULL; vfs->read_slots = NULL;
    vfs->read_count = vfs->read_capacity = vfs->read_slot_count = 0;
    vfs->read_generation = vfs->read_generation == UINT64_MAX ? 0 : vfs->read_generation + 1;
}
uint64_t qa_vfs_read_generation(const qa_vfs *vfs) { return vfs ? vfs->read_generation : 0; }
size_t qa_vfs_read_count(const qa_vfs *vfs) { return vfs ? vfs->read_count : 0; }
bool qa_vfs_read_at(const qa_vfs *vfs, size_t index, qa_vfs_read_reference *out)
{
    if (!vfs || !out || index >= vfs->read_count) return false;
    *out = vfs->reads[index]; return true;
}
bool qa_vfs_read_opening_at(const qa_vfs *vfs, size_t index, qa_vfs_read_opening *out)
{
    if (!vfs || !out || index >= vfs->read_count) return false;
    *out = vfs->reads[index].opening; return true;
}
static bool read_opening_capture(const qa_vfs *vfs, const mount *source,
    const char *path, bool linked, bool prefix_lookup, const qa_vfs_read_opening *retained,
    qa_vfs_read_opening *out, qa_error *error)
{
    qa_vfs_read_opening value = {.rank = -1};
    if (retained) value = *retained;
    else if (!linked) {
        value.order_count = vfs->count;
        value.user_overlay = source->user_overlay;
        if (prefix_lookup && !source->user_overlay) for (const prefix_order *p = vfs->prefixes; p; p = p->next) {
            size_t length = strlen(p->prefix);
            if (strlen(path) <= length || path[length] != '/') continue;
            bool matches = true;
            for (size_t i = 0; i < length; ++i) {
                unsigned char a = (unsigned char)path[i], b = (unsigned char)p->prefix[i];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) { matches = false; break; }
            }
            if (matches) { value.order = p->order; value.prefix = p->prefix; break; }
        }
        for (size_t i = 0; i < value.order_count; ++i)
            if ((value.order ? value.order[i] : vfs->mounts[i]->id) == source->id) {
                if (i > INT64_MAX) break;
                value.rank = (int64_t)i; break;
            }
    }
    if ((retained && value.order_count && !value.order) ||
        value.order_count > SIZE_MAX / sizeof(qa_mount_id) ||
        (linked ? value.rank != -1 || value.order_count || value.prefix || value.user_overlay :
         value.rank < 0 || (uint64_t)value.rank >= value.order_count)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid first VFS opening order"); return false;
    }
    qa_mount_id *order = value.order_count ? malloc(value.order_count * sizeof(*order)) : NULL;
    char *prefix = value.prefix ? copy_string(value.prefix) : NULL;
    if ((value.order_count && !order) || (value.prefix && !prefix)) {
        free(order); free(prefix);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining first VFS opening order"); return false;
    }
    for (size_t i = 0; i < value.order_count; ++i)
        order[i] = value.order ? value.order[i] : vfs->mounts[i]->id;
    value.order = order; value.prefix = prefix; *out = value; return true;
}
static size_t read_hash(qa_mount_id mount_id, uint64_t resource_id, size_t capacity)
{
    uint64_t value = resource_id ^ (mount_id * UINT64_C(11400714819323198485));
    value ^= value >> 33; value *= UINT64_C(14029467366897019727); value ^= value >> 29;
    return (size_t)value & (capacity - 1);
}
static bool read_recipe_equal(const qa_vfs_read_reference *entry, const char *path,
    const char *lookup, const char *from, const char *to)
{
    return entry->path && entry->lookup_path && entry->link_source && entry->link_target &&
        !strcmp(entry->path, path) && !strcmp(entry->lookup_path, lookup) &&
        !strcmp(entry->link_source, from) && !strcmp(entry->link_target, to);
}
size_t qa_vfs_retained_read_count(const qa_vfs *vfs)
{
    size_t count = 0;
    if (vfs) for (const retained_read *row = vfs->history; row; row = row->next)
        if (qa_resource_pool_find(vfs->pool, row->resource)) ++count;
    return count;
}
bool qa_vfs_retained_read_at(const qa_vfs *vfs, size_t index, qa_vfs_read_reference *out)
{
    if (!vfs || !out) return false;
    for (const retained_read *row = vfs->history; row; row = row->next) {
        const qa_resource *resource = qa_resource_pool_find(vfs->pool, row->resource);
        if (!resource) continue;
        if (index) { --index; continue; }
        *out = row->recipe; out->resource = resource; return true;
    }
    return false;
}
static void history_free(retained_read *row)
{
    free((char *)row->recipe.path); free((char *)row->recipe.lookup_path);
    free((char *)row->recipe.link_source); free((char *)row->recipe.link_target);
    free((void *)row->recipe.opening.order); free((char *)row->recipe.opening.prefix); free(row);
}
bool vfs_history_record(qa_vfs *vfs, const qa_vfs_read_reference *read, qa_error *error)
{
    uint64_t resource = qa_resource_id(read->resource);
    for (const retained_read *row = vfs->history; row; row = row->next)
        if (row->resource == resource && row->recipe.mount == read->mount &&
            read_recipe_equal(&row->recipe, read->path, read->lookup_path, read->link_source, read->link_target)) return true;
    retained_read *row = calloc(1, sizeof(*row));
    if (!row) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining first resource opening"); return false; }
    row->resource = resource; row->recipe = *read; row->recipe.resource = NULL;
    row->recipe.path = copy_string(read->path); row->recipe.lookup_path = copy_string(read->lookup_path);
    row->recipe.link_source = copy_string(read->link_source); row->recipe.link_target = copy_string(read->link_target);
    row->recipe.opening.order = NULL; row->recipe.opening.prefix = NULL;
    size_t count = read->opening.order_count;
    if (count <= SIZE_MAX / sizeof(qa_mount_id) && count) {
        qa_mount_id *order = malloc(count * sizeof(*order));
        if (order) memcpy(order, read->opening.order, count * sizeof(*order));
        row->recipe.opening.order = order;
    }
    if (read->opening.prefix) row->recipe.opening.prefix = copy_string(read->opening.prefix);
    if (!row->recipe.path || !row->recipe.lookup_path || !row->recipe.link_source || !row->recipe.link_target ||
        (count && !row->recipe.opening.order) || (read->opening.prefix && !row->recipe.opening.prefix)) {
        history_free(row); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining complete historical resource recipe"); return false;
    }
    row->next = vfs->history; vfs->history = row; return true;
}
static bool read_index(qa_vfs *vfs, size_t capacity, qa_error *error)
{
    if (capacity > SIZE_MAX / sizeof(*vfs->read_slots)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "VFS read journal index exhausted"); return false;
    }
    size_t *slots = calloc(capacity, sizeof(*slots));
    if (!slots) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining VFS read journal index"); return false; }
    for (size_t i = 0; i < vfs->read_count; ++i) {
        size_t index = read_hash(vfs->reads[i].mount, qa_resource_id(vfs->reads[i].resource), capacity);
        while (slots[index]) index = (index + 1) & (capacity - 1);
        slots[index] = i + 1;
    }
    free(vfs->read_slots); vfs->read_slots = slots; vfs->read_slot_count = capacity; return true;
}
bool vfs_read_record(qa_vfs *vfs, mount *source, qa_resource *resource, const char *path,
    const char *lookup_path, const char *link_source, const char *link_target,
    bool prefix_lookup, const qa_vfs_read_opening *retained, qa_error *error)
{
    if (!vfs->read_generation || vfs->read_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "VFS read journal generation exhausted"); return false;
    }
    size_t capacity = vfs->read_slot_count;
    if (!capacity || vfs->read_count + 1 > capacity - capacity / 4) {
        if (capacity > SIZE_MAX / 2) { qa_error_set(error, QA_ERROR_MEMORY, 0, "VFS read journal exhausted"); return false; }
        if (!read_index(vfs, capacity ? capacity * 2 : 64, error)) return false;
        capacity = vfs->read_slot_count;
    }
    char *requested = qa_vfs_normalize_path(path, error);
    if (!requested) return false;
    char *lookup = qa_vfs_normalize_path(lookup_path, error);
    char *from = copy_string(link_source ? link_source : ""), *to = copy_string(link_target ? link_target : "");
    if (!lookup || !from || !to) {
        free(requested); free(lookup); free(from); free(to);
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual VFS lookup recipe");
        return false;
    }
    size_t index = read_hash(source->id, qa_resource_id(resource), capacity);
    while (vfs->read_slots[index]) {
        const qa_vfs_read_reference *entry = vfs->reads + vfs->read_slots[index] - 1;
        if (entry->mount == source->id && entry->resource == resource &&
            read_recipe_equal(entry, requested, lookup, from, to)) {
            free(requested); free(lookup); free(from); free(to);
            return retained || (vfs_origin_record(vfs, source, resource, error) && vfs_history_record(vfs, entry, error));
        }
        index = (index + 1) & (capacity - 1);
    }
    if (vfs->read_count == vfs->read_capacity) {
        size_t next = vfs->read_capacity ? vfs->read_capacity * 2 : 32;
        if (next < vfs->read_capacity || next > SIZE_MAX / sizeof(*vfs->reads)) {
            free(requested); free(lookup); free(from); free(to);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "VFS read journal extent exhausted"); return false;
        }
        qa_vfs_read_reference *records = realloc(vfs->reads, next * sizeof(*records));
        if (!records) {
            free(requested); free(lookup); free(from); free(to);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining VFS source read provenance"); return false;
        }
        vfs->reads = records; vfs->read_capacity = next;
    }
    qa_vfs_read_opening opening = {0};
    if (!read_opening_capture(vfs, source, requested, from[0] != 0, prefix_lookup, retained, &opening, error)) {
        free(requested); free(lookup); free(from); free(to); return false;
    }
    if (!retained && !vfs_origin_record(vfs, source, resource, error)) {
        free(requested); free(lookup); free(from); free(to);
        free((void *)opening.order); free((char *)opening.prefix); return false;
    }
    qa_vfs_read_reference read = {source->id, resource, requested, lookup, from, to, opening};
    if (!retained && !vfs_history_record(vfs, &read, error)) {
        free(requested); free(lookup); free(from); free(to);
        free((void *)opening.order); free((char *)opening.prefix); return false;
    }
    qa_resource_retain(resource);
    vfs->reads[vfs->read_count++] = read;
    vfs->read_slots[index] = vfs->read_count; return true;
}
bool qa_vfs_restore_references(qa_vfs *vfs,const bool *flags,size_t count,qa_error *error)
{
    if(vfs==NULL || count!=vfs->count || (count && flags==NULL)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"VFS reference continuation requires the complete qualified mount inventory");
        return false;
    }
    for(size_t i=0;i<count;++i) vfs->mounts[i]->referenced=flags[i];
    return true;
}

static bool root_references_copy(mount *destination, const mount *source,
    qa_error *error)
{
    destination->root_references = NULL;
    destination->root_reference_count = 0;
    if (!source->root_reference_count) return true;
    if (!source->root_references || source->root_reference_count > SIZE_MAX / sizeof(*source->root_references)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid retained native root inventory"); return false;
    }
    qa_fs_object_reference *copy = malloc(source->root_reference_count * sizeof(*copy));
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying retained native root inventory"); return false; }
    memcpy(copy, source->root_references, source->root_reference_count * sizeof(*copy));
    destination->root_references = copy;
    destination->root_reference_count = source->root_reference_count;
    return true;
}

bool vfs_root_reference_add(mount *source, const qa_fs_object_reference *reference,
    qa_error *error)
{
    for (size_t i = 0; i < source->root_reference_count; ++i)
        if (source->root_references[i].platform == reference->platform &&
            !memcmp(source->root_references[i].words, reference->words, sizeof(reference->words))) return true;
    if (source->root_reference_count >= SIZE_MAX / sizeof(*source->root_references)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Native root inventory overflow"); return false;
    }
    qa_fs_object_reference *references = realloc(source->root_references,
        (source->root_reference_count + 1) * sizeof(*references));
    if (!references) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining admitted native root"); return false; }
    source->root_references = references;
    source->root_references[source->root_reference_count++] = *reference;
    return true;
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
        if (!root_references_copy(source, vfs->mounts[i], error)) { free(source); goto memory_failure; }
        source->path = copy_string(vfs->mounts[i]->path);
        if (source->path == NULL) { free(source->root_references); free(source); goto memory_failure; }
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
    resource_origin **origin_tail = &copy->origins;
    for (const resource_origin *original = vfs->origins; original; original = original->next) {
        resource_origin *row = malloc(sizeof(*row));
        char *path = copy_string(original->receipt.mount_path);
        if (!row || !path) { free(row); free(path); goto memory_failure; }
        *row = *original; row->next = NULL; row->receipt.mount_path = path;
        *origin_tail = row; origin_tail = &row->next;
    }
    for (const retained_read *row = vfs->history; row; row = row->next) {
        qa_vfs_read_reference read = row->recipe;
        read.resource = qa_resource_pool_find(vfs->pool, row->resource);
        if (read.resource && !vfs_history_record(copy, &read, error)) goto memory_failure;
    }
    return copy;
memory_failure:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot clone VFS");
    qa_vfs_destroy(copy);
    return NULL;
}

bool qa_vfs_lookup_equal(const qa_vfs *left, const qa_vfs *right)
{
    if (!left || !right || left->pool != right->pool || left->count != right->count ||
        left->pure_count != right->pure_count || left->q3_demo != right->q3_demo) return false;
    for (size_t i = 0; i < left->count; ++i) {
        const mount *a = left->mounts[i], *b = right->mounts[i];
        if (a->id != b->id || a->archive != b->archive || a->archive_file != b->archive_file ||
            a->root != b->root || !qa_fs_identity_equal(&a->identity, &b->identity) ||
            strcmp(a->path, b->path) || a->comparison != b->comparison ||
            a->writable != b->writable || a->user_overlay != b->user_overlay ||
            a->q3_demo != b->q3_demo) return false;
    }
    const prefix_order *a = left->prefixes, *b = right->prefixes;
    for (; a && b; a = a->next, b = b->next)
        if (strcmp(a->prefix, b->prefix) ||
            (left->count && memcmp(a->order, b->order, left->count * sizeof(*a->order)))) return false;
    if (a || b) return false;
    const resource_link *c = left->links, *d = right->links;
    for (; c && d; c = c->next, d = d->next)
        if (c->mount != d->mount || strcmp(c->source, d->source) || strcmp(c->target, d->target)) return false;
    if (c || d) return false;
    return !left->pure_count || !memcmp(left->pure, right->pure, left->pure_count * sizeof(*left->pure));
}

void vfs_mount_free(mount *source)
{
    if (source->archive != NULL) vfs_package_release(source->archive);
    qa_fs_file_close(source->archive_file);
    qa_fs_root_close(source->root);
    free(source->root_references);
    free(source->path);
    free(source);
}

bool qa_vfs_retain(qa_vfs *vfs, qa_error *error)
{
    if (!vfs || !vfs->references || vfs->references == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual VFS owner"); return false;
    }
    ++vfs->references; return true;
}
void qa_vfs_destroy(qa_vfs *vfs)
{
    if (vfs == NULL) return;
    if (vfs->references > 1) { --vfs->references; return; }
    qa_vfs_clear_references(vfs);
    for (size_t i = 0; i < vfs->count; i++) vfs_mount_free(vfs->mounts[i]);
    free(vfs->mounts);
    resource_origin *origin = vfs->origins;
    while (origin) {
        resource_origin *next = origin->next;
        free((char *)origin->receipt.mount_path); free(origin); origin = next;
    }
    while (vfs->history) {
        retained_read *row = vfs->history; vfs->history = row->next; history_free(row);
    }
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
        vfs_package_release(archive);
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
            vfs_package_release(archive);
            previous->references++;
            *out_file = file;
            *out_identity = identity;
            return previous;
        }
    }
    if (!qa_archive_open_memory((qa_bytes){archive->storage.data, archive->storage.size},
                                kind, &archive->archive, error)) {
        qa_fs_file_close(file);
        vfs_package_release(archive);
        return NULL;
    }
    size_t member_count = qa_archive_count(archive->archive);
    if (member_count > SIZE_MAX / sizeof(*archive->members)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "archive resource index overflow");
        qa_fs_file_close(file);
        vfs_package_release(archive);
        return NULL;
    }
    if (member_count != 0) archive->members = calloc(member_count, sizeof(*archive->members));
    if (member_count != 0 && archive->members == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate archive resource index");
        qa_fs_file_close(file);
        vfs_package_release(archive);
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

bool qa_vfs_mount_retained(qa_vfs *vfs, const qa_vfs *retained, qa_mount_id id,
    qa_archive_comparison comparison, bool writable, qa_mount_id *out, qa_error *error)
{
    if (!vfs || !retained || !out || !valid_comparison(comparison)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid retained mount authority");
        return false;
    }
    const mount *source = NULL;
    for (size_t i = 0; i < retained->count; ++i)
        if (retained->mounts[i]->id == id) { source = retained->mounts[i]; break; }
    if (!source || (source->archive && vfs->pool != retained->pool) ||
        (!source->archive && !source->root) || (writable && (!source->writable || source->archive))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Retained mount does not admit the requested authority");
        return false;
    }
    if (source->archive) {
        bool unchanged = false;
        qa_buffer bytes = {0};
        bool valid = qa_fs_file_path_unchanged(source->archive_file, &source->identity, &unchanged, error) &&
            unchanged && qa_fs_file_read_snapshot(source->archive_file, &source->identity, &bytes, error);
        if (valid) valid = bytes.size == source->archive->storage.size &&
            !memcmp(bytes.data, source->archive->storage.data, bytes.size);
        qa_buffer_free(&bytes);
        if (!valid) {
            if (!error || error->code == QA_OK)
                qa_error_set(error, QA_ERROR_IO, 0, "Retained archive changed: %s", source->path);
            return false;
        }
        if ((vfs->q3_demo || source->q3_demo) && !vfs_demo_package_allowed(source->archive, error)) return false;
    }
    mount *copy = malloc(sizeof(*copy));
    char *path = copy_string(source->path);
    if (!copy || !path) {
        free(copy); free(path);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot retain scoped mount authority");
        return false;
    }
    *copy = *source;
    if (!root_references_copy(copy, source, error)) { free(copy); free(path); return false; }
    copy->path = path; copy->comparison = comparison;
    copy->writable = writable; copy->user_overlay = false; copy->referenced = false;
    qa_fs_file_retain(copy->archive_file);
    qa_fs_root_retain(copy->root);
    if (copy->archive) ++copy->archive->references;
    if (!add_mount(vfs, copy, out, error)) { vfs_mount_free(copy); return false; }
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
    source->path = copy_string(path);
    if (!source->path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain mount path");
        vfs_mount_free(source); return false;
    }
    if (vfs->q3_demo && !vfs_demo_package_allowed(source->archive, error)) {
        vfs_mount_free(source);
        return false;
    }
    if (!add_mount(vfs, source, out, error)) {
        vfs_mount_free(source);
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
    qa_fs_object_reference reference;
    if (!qa_fs_root_reference_read(root, &reference) || !vfs_root_reference_add(source, &reference, error)) {
        vfs_mount_free(source); return false;
    }
    source->path = copy_string(path);
    if (!source->path) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot retain mount path");
        vfs_mount_free(source); return false;
    }
    source->comparison = comparison;
    source->writable = writable;
    if (!add_mount(vfs, source, out, error)) {
        vfs_mount_free(source);
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

bool qa_vfs_retained_recipe_matches(const qa_vfs *view, const qa_vfs *retained,
    const qa_mount_id *ordered_ids, size_t count, qa_archive_comparison comparison,
    bool force_mount_q3_demo)
{
    if (!view || !retained || view->pool != retained->pool || view->count != count ||
        count > retained->count || (count && !ordered_ids) || !valid_comparison(comparison) ||
        view->prefixes || view->links || view->pure_count || view->q3_demo)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const mount *actual = view->mounts[i];
        const mount *source = find_mount(retained, ordered_ids[i]);
        if (!source || actual->id != (qa_mount_id)i + 1 || actual->archive != source->archive ||
            !qa_fs_identity_equal(&actual->identity, &source->identity) ||
            strcmp(actual->path, source->path) || actual->comparison != comparison ||
            actual->writable != source->writable || actual->user_overlay ||
            actual->q3_demo != (source->q3_demo || force_mount_q3_demo))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (ordered_ids[j] == ordered_ids[i]) return false;
        if (source->archive) {
            if (!actual->archive_file || !source->archive_file || actual->root || source->root)
                return false;
        } else if (actual->archive_file || source->archive_file ||
            !qa_fs_root_same_object(actual->root, source->root))
            return false;
    }
    return true;
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
bool qa_vfs_archive_bytes(const qa_vfs *vfs, qa_mount_id id, qa_bytes *out, qa_error *error)
{
    const mount *source = vfs ? find_mount(vfs, id) : NULL; bool unchanged = false;
    if (!source || !source->archive || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Whole package requires its actual mounted archive"); return false;
    }
    if (!qa_fs_file_path_unchanged(source->archive_file, &source->identity, &unchanged, error)) return false;
    if (!unchanged) { qa_error_set(error, QA_ERROR_IO, 0, "Mounted package changed after admission"); return false; }
    *out = (qa_bytes){source->archive->storage.data, source->archive->storage.size}; return true;
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

bool vfs_demo_package_allowed(const package *archive, qa_error *error)
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
            if (archive != NULL && !vfs_demo_package_allowed(archive, error)) return false;
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

bool qa_vfs_restrictions_read(const qa_vfs *vfs, const qa_sha256_digest **archives,
    size_t *count, bool *q3_demo)
{
    if (!vfs || !archives || !count || !q3_demo) return false;
    *archives = vfs->pure; *count = vfs->pure_count; *q3_demo = vfs->q3_demo;
    return true;
}

bool qa_vfs_set_mount_q3_demo(qa_vfs *vfs, qa_mount_id id, bool enabled, qa_error *error)
{
    mount *source = find_mount(vfs, id);
    if (!source) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 demo admission requires its actual mount");
        return false;
    }
    if (enabled && source->archive && !vfs_demo_package_allowed(source->archive, error)) return false;
    source->q3_demo = enabled;
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
    if (vfs->pure_count == 0 && !vfs->q3_demo && !source->q3_demo) return true;
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
        bool recorded = false;
        for (size_t j = 0; j < vfs->read_count; ++j) if (vfs->reads[j].mount == id) recorded = true;
        if (recorded && (!vfs->read_generation || vfs->read_generation == UINT64_MAX)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "VFS read journal generation exhausted"); return false;
        }
        if (recorded) {
            size_t kept = 0;
            for (size_t j = 0; j < vfs->read_count; ++j) {
                if (vfs->reads[j].mount == id) {
                    qa_resource_release((qa_resource *)vfs->reads[j].resource); free((char *)vfs->reads[j].path);
                    free((char *)vfs->reads[j].lookup_path); free((char *)vfs->reads[j].link_source); free((char *)vfs->reads[j].link_target);
                    free((void *)vfs->reads[j].opening.order); free((char *)vfs->reads[j].opening.prefix);
                }
                else vfs->reads[kept++] = vfs->reads[j];
            }
            vfs->read_count = kept; ++vfs->read_generation;
            memset(vfs->read_slots, 0, vfs->read_slot_count * sizeof(*vfs->read_slots));
            for (size_t j = 0; j < kept; ++j) {
                size_t index = read_hash(vfs->reads[j].mount, qa_resource_id(vfs->reads[j].resource), vfs->read_slot_count);
                while (vfs->read_slots[index]) index = (index + 1) & (vfs->read_slot_count - 1);
                vfs->read_slots[index] = j + 1;
            }
        }
        vfs_mount_free(vfs->mounts[i]);
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

static bool archive_member(const mount *source, const char *path,
    const qa_archive_entry **out, qa_error *error)
{
    const qa_archive_entry *selected = NULL;
    size_t start = 0;
    for (;;) {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find_normalized(source->archive->archive, path, source->comparison, start, &entry, error)) return false;
        if (entry == NULL) break;
        if (!entry->is_directory) {
            selected = entry;
            if (qa_archive_get_kind(source->archive->archive) == QA_ARCHIVE_PAK) break;
        }
        start = entry->ordinal + 1;
    }
    *out = selected; return true;
}
static bool acquire_archive(qa_resource_pool *pool, const mount *source,
                             const char *path, qa_resource **out, qa_error *error)
{
    package *archive = source->archive;
    bool unchanged = false;
    if (!qa_fs_file_path_unchanged(source->archive_file, &source->identity, &unchanged, error)) return false;
    if (!unchanged) { qa_error_set(error, QA_ERROR_IO, 0, "mounted archive changed: %s", path); return false; }
    const qa_archive_entry *selected = NULL;
    if (!archive_member(source, path, &selected, error)) return false;
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

void qa_vfs_acquisition_dispose(qa_vfs_acquisition *receipt)
{
    if (!receipt) return;
    free(receipt->path); free(receipt->lookup_path); free(receipt->link_source); free(receipt->link_target);
    free((void *)receipt->opening.order); free((char *)receipt->opening.prefix);
    *receipt = (qa_vfs_acquisition){0};
}
bool qa_vfs_acquisition_copy(const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    if (!source || !out || source == out || !source->mount || !source->resource_id ||
        !source->path || !source->lookup_path || out->mount || out->resource_id ||
        out->path || out->lookup_path || out->link_source || out->link_target ||
        out->opening_present || out->opening.order || out->opening.prefix ||
        out->opening.order_count || out->opening.rank || out->opening.user_overlay ||
        source->opening.order_count > SIZE_MAX / sizeof(qa_mount_id) ||
        (source->opening.order_count && !source->opening.order)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Receipt copy requires an actual acquisition and empty destination"); return false;
    }
    qa_vfs_acquisition copy = {.mount = source->mount, .resource_id = source->resource_id,
        .opening_present = source->opening_present,
        .opening = {.rank = source->opening.rank, .order_count = source->opening.order_count,
            .user_overlay = source->opening.user_overlay}};
    const char *values[] = {source->path, source->lookup_path, source->link_source, source->link_target};
    char **targets[] = {&copy.path, &copy.lookup_path, &copy.link_source, &copy.link_target};
    for (size_t i = 0; i < 4; ++i) if (values[i]) {
        *targets[i] = copy_string(values[i]);
        if (!*targets[i]) goto fail;
    }
    if (source->opening.prefix) {
        copy.opening.prefix = copy_string(source->opening.prefix);
        if (!copy.opening.prefix) goto fail;
    }
    if (copy.opening.order_count) {
        size_t size = copy.opening.order_count * sizeof(qa_mount_id);
        qa_mount_id *order = malloc(size);
        if (!order) goto fail;
        memcpy(order, source->opening.order, size); copy.opening.order = order;
    }
    *out = copy; return true;
fail:
    qa_vfs_acquisition_dispose(&copy);
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual acquisition receipt"); return false;
}
bool qa_vfs_acquisition_opening_codec(qa_source_save_io *io, const qa_vfs *vfs, qa_vfs_acquisition *receipt)
{
    if (!io || !vfs || !receipt || !receipt->path || !receipt->link_source ||
        !qa_vfs_mount_id_was_issued(vfs, receipt->mount)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (reading && (receipt->opening_present || receipt->opening.order || receipt->opening.prefix)) return false;
    if (!qa_source_save_bool(io, &receipt->opening_present)) return false;
    if (!receipt->opening_present) return !receipt->opening.rank && !receipt->opening.order &&
        !receipt->opening.order_count && !receipt->opening.prefix && !receipt->opening.user_overlay;
    qa_vfs_read_opening *opening = &receipt->opening;
    size_t length = reading ? 0 : opening->prefix ? strlen(opening->prefix) : 0;
    if (!qa_source_save_i64(io, &opening->rank) || opening->rank < -1 ||
        !qa_source_save_bool(io, &opening->user_overlay) ||
        !qa_source_save_count(io, &length, reading ? io->input.size - io->offset : SIZE_MAX - 1) ||
        length == SIZE_MAX) return false;
    char *prefix = reading && length ? malloc(length + 1) : (char *)opening->prefix;
    if (reading) opening->prefix = prefix;
    if (length && !prefix) {
        qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring acquisition opening prefix"); return false;
    }
    if (!qa_source_save_bytes(io, prefix, length) ||
        (length && memchr(prefix, 0, length))) return false;
    if (reading && prefix) prefix[length] = 0;
    if (length) {
        char *normalized = qa_vfs_normalize_path(prefix, io->error);
        bool same = normalized && !strcmp(normalized, prefix); free(normalized);
        if (!same || opening->user_overlay || strlen(receipt->path) <= length || receipt->path[length] != '/') return false;
        for (size_t i = 0; i < length; ++i) {
            unsigned char a = (unsigned char)receipt->path[i], b = (unsigned char)prefix[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) return false;
        }
    }
    if (!qa_source_save_count(io, &opening->order_count, reading ? io->input.size / 8 : SIZE_MAX / sizeof(qa_mount_id)) ||
        opening->order_count > SIZE_MAX / sizeof(qa_mount_id)) return false;
    qa_mount_id *order = reading && opening->order_count ? calloc(opening->order_count, sizeof(*order)) :
        (qa_mount_id *)opening->order;
    if (reading) opening->order = order;
    if (opening->order_count && !order) {
        qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring acquisition opening order"); return false;
    }
    for (size_t i = 0; i < opening->order_count; ++i) {
        if (!qa_source_save_u64(io, order + i) || !qa_vfs_mount_id_was_issued(vfs, order[i])) return false;
        for (size_t j = 0; j < i; ++j) if (order[j] == order[i]) return false;
    }
    if (*receipt->link_source) return opening->rank == -1 && !opening->order_count && !length && !opening->user_overlay;
    return opening->rank >= 0 && (uint64_t)opening->rank < opening->order_count &&
        order[opening->rank] == receipt->mount;
}
static bool acquisition_capture(const qa_vfs *vfs, mount *source, qa_resource *resource, const char *requested,
    const char *path, const resource_link *link, bool prefix_lookup, qa_vfs_acquisition *out, qa_error *error)
{
    if (!out) return true;
    qa_vfs_acquisition receipt = {.mount = source->id, .resource_id = qa_resource_id(resource)};
    receipt.path = qa_vfs_normalize_path(requested, error);
    receipt.lookup_path = qa_vfs_normalize_path(path, error);
    receipt.link_source = copy_string(link ? link->source : "");
    receipt.link_target = copy_string(link ? link->target : "");
    if (!receipt.path || !receipt.lookup_path || !receipt.link_source || !receipt.link_target ||
        !read_opening_capture(vfs, source, receipt.path, link != NULL, prefix_lookup, NULL, &receipt.opening, error)) {
        qa_vfs_acquisition_dispose(&receipt);
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual VFS acquisition receipt");
        return false;
    }
    receipt.opening_present = true; *out = receipt; return true;
}
static bool acquire_mount(qa_vfs *vfs, mount *source,
                           const char *path, const char *requested, const resource_link *link,
                           bool prefix_lookup, qa_resource **out, qa_vfs_acquisition *receipt, qa_error *error)
{
    if (!source_allowed(vfs, source, path)) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "resource excluded by content policy: %s", path);
        return false;
    }
    bool result = source->archive != NULL ? acquire_archive(vfs->pool, source, path, out, error) :
                                           acquire_loose(vfs->pool, source, path, out, error);
    if (result) {
        if (!vfs_read_record(vfs, source, *out, requested, path, link ? link->source : NULL, link ? link->target : NULL, prefix_lookup, NULL, error)) {
            qa_resource_release(*out); *out = NULL; return false;
        }
        if (source->archive != NULL) source->referenced = true;
        if (!acquisition_capture(vfs, source, *out, requested, path, link, prefix_lookup, receipt, error)) {
            qa_resource_release(*out); *out = NULL; return false;
        }
    }
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
    bool result = acquire_mount(vfs, source, normalized, path, NULL, false, out, NULL, error);
    free(normalized);
    return result;
}

bool qa_vfs_acquire(qa_vfs *vfs, const char *path, qa_resource **out,
                    qa_mount_id *out_mount, qa_error *error)
{
    return qa_vfs_acquire_filtered(vfs, path, NULL, NULL, out, out_mount, error);
}

static bool acquire_filtered_mode(qa_vfs *vfs, const char *path,
                              qa_vfs_accept_mount accept, void *context,
                              qa_resource **out, qa_mount_id *out_mount,
                              qa_vfs_acquisition *receipt, qa_error *error)
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
        if (acquire_mount(vfs, source, normalized, path, NULL, true, out, receipt, &local)) {
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
        bool result = acquire_mount(vfs, source, normalized, path, link, true, out, receipt, error);
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
        if (acquire_mount(vfs, source, normalized, path, NULL, true, out, receipt, &local)) {
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
bool qa_vfs_acquire_filtered(qa_vfs *vfs, const char *path, qa_vfs_accept_mount accept,
    void *context, qa_resource **out, qa_mount_id *out_mount, qa_error *error)
{ return acquire_filtered_mode(vfs, path, accept, context, out, out_mount, NULL, error); }
bool qa_vfs_acquire_receipt(qa_vfs *vfs, const char *path, qa_resource **out,
    qa_vfs_acquisition *receipt, qa_error *error)
{
    if (out) *out = NULL;
    if (!receipt || receipt->mount || receipt->resource_id || receipt->path || receipt->lookup_path ||
        receipt->link_source || receipt->link_target || receipt->opening_present || receipt->opening.rank ||
        receipt->opening.order || receipt->opening.order_count || receipt->opening.prefix || receipt->opening.user_overlay) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS acquisition requires an empty receipt"); return false;
    }
    return acquire_filtered_mode(vfs, path, NULL, NULL, out, NULL, receipt, error);
}
static bool probe_mount(const qa_vfs *vfs, const mount *source, const char *path,
    bool *found, uint64_t *size, qa_error *error)
{
    if (!source_allowed(vfs, source, path)) return true;
    if (source->archive) {
        bool unchanged = false;
        if (!qa_fs_file_path_unchanged(source->archive_file, &source->identity, &unchanged, error)) return false;
        if (!unchanged) { qa_error_set(error, QA_ERROR_IO, 0, "Mounted probe archive changed"); return false; }
        const qa_archive_entry *entry = NULL;
        if (!archive_member(source, path, &entry, error)) return false;
        if (entry) { *found = true; *size = entry->size; }
        return true;
    }
    qa_error local = {0}; char *resolved = resolve_spelling(source, path, &local);
    if (!resolved) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    qa_fs_file *file = NULL; qa_fs_identity identity;
    bool ok = qa_fs_root_file_open(source->root, resolved, &file, &identity, &local);
    free(resolved);
    if (!ok) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    qa_fs_file_close(file); *found = true; *size = qa_fs_identity_size(&identity); return true;
}
static bool read_recipe_valid(const qa_vfs *vfs, const qa_vfs_read_reference *entry, bool native_identity, qa_error *error)
{
    mount *source = find_mount(vfs, entry->mount);
    const qa_resource *resource = entry->resource;
    if (!source || !resource || qa_resource_pool_find(vfs->pool, resource->id) != resource ||
        resource->archive != source->archive || !entry->path || !entry->lookup_path ||
        !entry->link_source || !entry->link_target) goto invalid;
    if (source->archive && *entry->link_source) goto invalid;
    if (!*entry->link_source) {
        if (*entry->link_target || strcmp(entry->path, entry->lookup_path)) goto invalid;
    } else {
        size_t from = strlen(entry->link_source), to = strlen(entry->link_target);
        if (strncmp(entry->path, entry->link_source, from)) goto invalid;
        size_t suffix = strlen(entry->path + from);
        if (to > SIZE_MAX - suffix - 1) goto invalid;
        char *target = malloc(to + suffix + 1);
        if (!target) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Validating retained VFS lookup"); return false; }
        memcpy(target, entry->link_target, to); memcpy(target + to, entry->path + from, suffix + 1);
        char *normalized = qa_vfs_normalize_path(target, error); free(target);
        bool matches = normalized && !strcmp(normalized, entry->lookup_path); free(normalized);
        if (!matches) goto invalid;
    }
    if (source->archive) {
        const qa_archive_entry *member = NULL;
        if (!archive_member(source, entry->lookup_path, &member, error)) return false;
        if (!member || member->ordinal != resource->ordinal || !source->referenced) goto invalid;
    } else if (native_identity) {
        char *resolved = resolve_spelling(source, entry->lookup_path, error);
        if (!resolved) return false;
        qa_fs_file *file = NULL; qa_fs_identity identity;
        bool opened = qa_fs_root_file_open(source->root, resolved, &file, &identity, error); free(resolved);
        if (!opened) return false;
        qa_fs_file_close(file);
        if (!qa_fs_identity_equal(&identity, &resource->identity)) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "VFS read recipe differs from its actual mounted resource"); return false;
}
bool vfs_read_valid(const qa_vfs *vfs, const qa_vfs_read_reference *entry, qa_error *error)
{ return read_recipe_valid(vfs, entry, true, error); }
static bool acquisition_valid(const qa_vfs *vfs, const qa_vfs_acquisition *receipt, bool native_identity, qa_error *error)
{
    if (vfs && receipt && receipt->mount && receipt->resource_id && receipt->path &&
        receipt->lookup_path && receipt->link_source && receipt->link_target) {
        const char *paths[] = {receipt->path, receipt->lookup_path, receipt->link_source, receipt->link_target};
        for (size_t i = 0; i < 4; ++i) {
            char *normalized = i < 2 ? qa_vfs_normalize_path(paths[i], error) : link_prefix(paths[i], true, error);
            bool matches = normalized && !strcmp(normalized, paths[i]); free(normalized);
            if (!matches) {
                if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "VFS acquisition receipt has an invalid lookup spelling");
                return false;
            }
        }
        for (size_t i = 0; i < vfs->read_count; ++i) {
            const qa_vfs_read_reference *row = vfs->reads + i;
            if (row->mount != receipt->mount || qa_resource_id(row->resource) != receipt->resource_id) continue;
            if (!read_recipe_equal(row, receipt->path, receipt->lookup_path,
                receipt->link_source, receipt->link_target)) continue;
            return read_recipe_valid(vfs, row, native_identity, error);
        }
    }
    qa_error_set(error, QA_ERROR_FORMAT, 0, "VFS acquisition receipt lacks its actual retained mounted resource"); return false;
}
bool qa_vfs_acquisition_valid(const qa_vfs *vfs, const qa_vfs_acquisition *receipt, qa_error *error)
{ return acquisition_valid(vfs, receipt, true, error); }
bool qa_vfs_acquisition_retained(const qa_vfs *vfs, const qa_vfs_acquisition *receipt, qa_error *error)
{ return acquisition_valid(vfs, receipt, false, error); }
bool qa_vfs_probe(qa_vfs *vfs, const char *path, bool *found, uint64_t *size, qa_error *error)
{
    if (!vfs || !found || !size) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS probe requires its view and outputs"); return false; }
    *found = false; *size = 0;
    char *normalized = qa_vfs_normalize_path(path, error);
    if (!normalized) return false;
    bool ok = true;
    for (size_t i = 0; ok && !*found && i < vfs->count; ++i)
        if (vfs->mounts[i]->user_overlay) ok = probe_mount(vfs, vfs->mounts[i], normalized, found, size, error);
    if (!ok || *found) { free(normalized); return ok; }
    for (resource_link *link = vfs->links; link; link = link->next) {
        size_t from = strlen(link->source), to = strlen(link->target);
        if (strncmp(normalized, link->source, from)) continue;
        size_t suffix = strlen(normalized + from);
        if (to > SIZE_MAX - suffix - 1) { free(normalized); qa_error_set(error, QA_ERROR_MEMORY, 0, "VFS probe link extent"); return false; }
        char *target = malloc(to + suffix + 1);
        if (!target) { free(normalized); qa_error_set(error, QA_ERROR_MEMORY, 0, "VFS probe link path"); return false; }
        memcpy(target, link->target, to); memcpy(target + to, normalized + from, suffix + 1);
        free(normalized); normalized = qa_vfs_normalize_path(target, error); free(target);
        if (!normalized) return false;
        mount *source = find_mount(vfs, link->mount);
        ok = source && probe_mount(vfs, source, normalized, found, size, error);
        free(normalized); return ok;
    }
    qa_mount_id *order = NULL; size_t length = strlen(normalized);
    for (prefix_order *prefix = vfs->prefixes; prefix; prefix = prefix->next) {
        size_t prefix_length = strlen(prefix->prefix);
        if (prefix_length < length && normalized[prefix_length] == '/') {
            normalized[prefix_length] = 0;
            bool matches = qa_archive_paths_equal(normalized, prefix->prefix, QA_ARCHIVE_CASE_INSENSITIVE);
            normalized[prefix_length] = '/';
            if (matches) { order = prefix->order; break; }
        }
    }
    for (size_t i = 0; ok && !*found && i < vfs->count; ++i) {
        mount *source = order ? find_mount(vfs, order[i]) : vfs->mounts[i];
        if (!source->user_overlay) ok = probe_mount(vfs, source, normalized, found, size, error);
    }
    free(normalized); return ok;
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
            if (!source->user_overlay && (vfs->pure_count != 0 || vfs->q3_demo || source->q3_demo)) continue;
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

static bool write_publish(qa_vfs *vfs, qa_mount_id id, const char *path,
                          qa_bytes bytes, bool exclusive, bool private_file,
                          bool *created, qa_error *error)
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
    bool success = qa_fs_root_publish(source->root, normalized, bytes,
                                      nonce, exclusive, private_file, created, error);
    free(normalized);
    return success;
}

bool qa_vfs_write(qa_vfs *vfs, qa_mount_id id, const char *path, qa_bytes bytes, qa_error *error)
{
    bool created;
    return write_publish(vfs, id, path, bytes, false, false, &created, error);
}
bool qa_vfs_write_exclusive(qa_vfs *vfs, qa_mount_id id, const char *path,
                            qa_bytes bytes, bool *created, qa_error *error)
{
    if (!created) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing exclusive write result"); return false; }
    return write_publish(vfs, id, path, bytes, true, false, created, error);
}
bool qa_vfs_write_private(qa_vfs *vfs, qa_mount_id id, const char *path, qa_bytes bytes, qa_error *error)
{
    bool created;
    return write_publish(vfs, id, path, bytes, false, true, &created, error);
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

const char *qa_vfs_mount_path(const qa_vfs *vfs, qa_mount_id id) {
    const mount *source = find_mount(vfs, id); return source ? source->path : NULL;
}
qa_fs_root *qa_vfs_mount_root(const qa_vfs *vfs, qa_mount_id id) {
    const mount *source = vfs ? find_mount(vfs, id) : NULL;
    return source && !source->archive ? source->root : NULL;
}
size_t qa_vfs_link_count(const qa_vfs *vfs) {
    size_t count = 0;
    for (const resource_link *link = vfs ? vfs->links : NULL; link; link = link->next) ++count;
    return count;
}
bool qa_vfs_link_at(const qa_vfs *vfs, size_t index, const char **source,
    qa_mount_id *target_mount, const char **target) {
    if (!vfs || !source || !target_mount || !target) return false;
    const resource_link *link = vfs->links;
    while (link && index) { link = link->next; --index; }
    if (!link) return false;
    *source = link->source; *target_mount = link->mount; *target = link->target;
    return true;
}
size_t qa_vfs_prefix_count(const qa_vfs *vfs) {
    size_t count = 0;
    if (vfs) for (const prefix_order *rule = vfs->prefixes; rule; rule = rule->next) ++count;
    return count;
}
bool qa_vfs_prefix_at(const qa_vfs *vfs, size_t index, const char **prefix,
                       const qa_mount_id **order, size_t *count) {
    if (!vfs || !prefix || !order || !count) return false;
    const prefix_order *rule = vfs->prefixes;
    while (rule && index) { rule = rule->next; --index; }
    if (!rule) return false;
    *prefix = rule->prefix; *order = rule->order; *count = vfs->count; return true;
}
qa_resource_pool *qa_vfs_resources(const qa_vfs *vfs)
{
    return vfs ? vfs->pool : NULL;
}

size_t qa_vfs_resource_count(const qa_vfs *vfs) {
    size_t count = 0;
    if (vfs) for (const qa_resource *resource = vfs->pool->resources; resource; resource = resource->next) ++count;
    return count;
}
const qa_resource *qa_vfs_resource_at(const qa_vfs *vfs, size_t index, size_t *readers) {
    if (!vfs) return NULL;
    const qa_resource *resource = vfs->pool->resources;
    while (resource && index) { resource = resource->next; --index; }
    if (resource && readers) *readers = resource->references ? resource->references - 1 : 0;
    return resource;
}
