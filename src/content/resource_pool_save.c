#include "qa/vfs_save.h"
#include "vfs_private.h"
#include "vfs_save_io.h"

static const uint8_t pool_magic[4] = {'Q','A','R','P'};

static size_t package_count(const qa_resource_pool *pool)
{
    size_t count = 0;
    for (const package *p = pool->packages; p; p = p->next) ++count;
    return count;
}
static size_t resource_count(const qa_resource_pool *pool)
{
    size_t count = 0;
    for (const qa_resource *r = pool->resources; r; r = r->next) ++count;
    return count;
}
static uint64_t resource_index(const qa_resource_pool *pool, const qa_resource *wanted)
{
    uint64_t index = 0;
    for (const qa_resource *r = pool->resources; r; r = r->next, ++index)
        if (r == wanted) return index;
    return UINT64_MAX;
}
static const char *mounted_package_path(const package *p,
    const qa_vfs *const *views, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const qa_vfs *view = views[i];
        if (!view) continue;
        for (size_t j = 0; j < view->count; ++j) {
            const mount *m = view->mounts[j];
            if (m->archive == p && m->archive_file) return m->path;
        }
    }
    return p->path;
}
static bool package_storage(qa_source_save_io *io, package *p, const package *physical, qa_archive_kind kind,
    const qa_vfs *const *views, size_t view_count,
    const qa_vfs_checkpoint_refs *refs)
{
    char *path = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)mounted_package_path(physical, views, view_count);
    size_t size = (size_t)qa_fs_identity_size(&p->identity);
    bool ok = vfs_save_text(io, &path) && path[0] && vfs_save_size(io, &size);
    if (ok && (io->direction == QA_SOURCE_SAVE_READ)) {
        qa_fs_file *file = NULL;
        qa_fs_identity identity;
        ok = refs && refs->archive_open ?
            refs->archive_open(refs->context, path, &file, &identity, io->error) :
            qa_fs_file_open(path, &file, &identity, io->error);
        if (ok && !file) ok = vfs_save_fail(io, QA_ERROR_ARGUMENT, "Linked archive resolver returned no file");
        if (ok && qa_fs_identity_size(&identity) != size)
            ok = vfs_save_fail(io, QA_ERROR_FORMAT, "Linked archive size differs");
        if (ok) {
            ok = qa_archive_open_retained(file, &identity, kind, &p->archive, io->error);
            if (ok) p->identity = identity;
        }
        qa_fs_file_close(file);
    }
    if (io->direction == QA_SOURCE_SAVE_READ) p->path = path;
    return ok;
}
static bool packages(qa_source_save_io *io, qa_resource_pool *pool,
    const qa_vfs *const *views, size_t view_count, const qa_vfs_checkpoint_refs *refs)
{
    size_t count = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : package_count(pool);
    if (!vfs_save_count(io, &count, 25, sizeof(package))) return false;
    package **tail = &pool->packages;
    for (size_t i = 0; i < count; ++i) {
        package saved = {0};
        package *p;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            p = calloc(1, sizeof(*p));
            if (!p) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS package");
            p->references = 1; p->pool = pool;
            *tail = p;
        } else { saved = **tail; p = &saved; }
        uint64_t kind = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : qa_archive_get_kind(p->archive);
        if (!qa_source_save_u64(io, &kind) || kind < QA_ARCHIVE_PAK || kind > QA_ARCHIVE_KPF ||
            !package_storage(io, p, (io->direction == QA_SOURCE_SAVE_READ) ? NULL : *tail,
                (qa_archive_kind)kind, views, view_count, refs)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            size_t members = qa_archive_count(p->archive);
            if (members > SIZE_MAX / sizeof(*p->members))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS package member table overflow");
            p->members = members ? calloc(members, sizeof(*p->members)) : NULL;
            if (members && !p->members)
                return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS member table");
        } else if (!p->archive || !p->references ||
                   (qa_archive_count(p->archive) && !p->members)) {
            return vfs_save_fail(io, QA_ERROR_FORMAT, "incomplete installed VFS package");
        }
        tail = &(*tail)->next;
    }
    return true;
}

static bool resource_record(qa_source_save_io *io, qa_resource_pool *pool,
                            qa_resource *r, uint64_t previous_id)
{
    uint64_t origin = (io->direction == QA_SOURCE_SAVE_READ) ? UINT64_MAX : vfs_package_index(pool, r->archive);
    if (io->direction != QA_SOURCE_SAVE_READ && r->archive && origin == UINT64_MAX)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "resource has a foreign VFS package");
    if (!qa_source_save_u64(io, &r->id) || !r->id || (previous_id && r->id >= previous_id) ||
        (pool->next_resource && r->id >= pool->next_resource) ||
        !vfs_save_text(io, &r->path) || !qa_source_save_u64(io, &origin) ||
        !vfs_save_size(io, &r->ordinal) || !vfs_save_identity(io, &r->identity))
        return false;
    char *normalized = qa_vfs_normalize_path(r->path, io->error);
    if (!normalized) return false;
    bool path_matches = !strcmp(normalized, r->path);
    free(normalized);
    if (!path_matches) return vfs_save_fail(io, QA_ERROR_FORMAT, "unnormalized VFS resource path");
    package *archive = origin == UINT64_MAX ? NULL : vfs_package_at(pool, origin);
    if (origin != UINT64_MAX && !archive)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "unknown retained VFS package");
    const qa_archive_entry *entry = archive ? qa_archive_entry_at(archive->archive, r->ordinal) : NULL;
    if (archive && (!entry || entry->is_directory || strcmp(entry->path, r->path)))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource member identity differs");
    if ((io->direction == QA_SOURCE_SAVE_READ) && archive) {
        /* Install only a bounded member ordinal before ordinary failure cleanup. */
        r->archive = archive;
        ++archive->references;
    }
    qa_fs_identity zero = {0};
    if ((archive && !qa_fs_identity_equal(&r->identity, &zero)) || (!archive && r->ordinal))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid retained VFS resource metadata");
    if (archive) {
        if (io->direction == QA_SOURCE_SAVE_READ && !qa_archive_read(archive->archive, r->ordinal, &r->data, io->error)) return false;
    } else {
        qa_buffer bytes = io->direction == QA_SOURCE_SAVE_READ ? (qa_buffer){0} :
            (qa_buffer){(uint8_t *)r->data.bytes.data, r->data.bytes.size};
        if (!vfs_save_buffer(io, &bytes)) {
            if (io->direction == QA_SOURCE_SAVE_READ) qa_buffer_free(&bytes);
            return false;
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            r->data.owned = bytes;
            if (!bytes.data) {
                r->data.owned.data = malloc(1);
                if (!r->data.owned.data) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating empty restored resource");
            }
            r->data.bytes = (qa_bytes){r->data.owned.data, bytes.size};
        }
    }
    if ((archive ? entry->size : qa_fs_identity_size(&r->identity)) != r->data.bytes.size)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource retained bytes differ");
    if (archive) {
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (archive->members[r->ordinal])
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS resource member");
            archive->members[r->ordinal] = r;
        } else if (archive->members[r->ordinal] !=
                   qa_resource_pool_find(pool, r->id) || r->identity_next) {
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS member cache linkage differs");
        }
    }
    return true;
}

static bool resources(qa_source_save_io *io, qa_resource_pool *pool,
                       qa_resource ***index_out, size_t *count_out)
{
    size_t count = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : resource_count(pool);
    if (!vfs_save_count(io, &count, 89, sizeof(qa_resource *))) return false;
    qa_resource **index = count ? calloc(count, sizeof(*index)) : NULL;
    if (count && !index) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS resource graph");
    *index_out = index;
    *count_out = count;
    qa_resource **tail = &pool->resources;
    uint64_t previous_id = 0;
    for (size_t i = 0; i < count; ++i) {
        qa_resource saved = {0};
        qa_resource *r;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            r = calloc(1, sizeof(*r));
            if (!r) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS resource");
            r->references = 1;
            *tail = r;
        } else { saved = **tail; r = &saved; }
        index[i] = *tail;
        if (!r->references || !resource_record(io, pool, r, previous_id)) return false;
        previous_id = r->id;
        tail = &(*tail)->next;
    }
    return true;
}

static bool loose_index(qa_source_save_io *io, qa_resource_pool *pool,
                         qa_resource **resources, size_t resource_total)
{
    size_t capacity = pool->loose_bucket_count;
    size_t count = pool->loose_count;
    if (!vfs_save_count(io, &capacity, 8, sizeof(qa_resource *)) ||
        !vfs_save_size(io, &count) || count > resource_total ||
        (capacity && (capacity < 64 || (capacity & (capacity - 1)))) ||
        (!capacity && count) || (capacity && count > capacity - capacity / 4))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS loose cache capacity");
    bool *seen = resource_total ? calloc(resource_total, sizeof(*seen)) : NULL;
    if (resource_total && !seen) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS loose cache admission");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        pool->loose_buckets = capacity ? calloc(capacity, sizeof(*pool->loose_buckets)) : NULL;
        if (capacity && !pool->loose_buckets) {
            free(seen);
            return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS loose cache");
        }
        pool->loose_bucket_count = capacity;
        pool->loose_count = count;
    }
    size_t admitted = 0;
    bool success = false;
    for (size_t bucket = 0; bucket < capacity; ++bucket) {
        size_t chain_count = 0;
        if (io->direction != QA_SOURCE_SAVE_READ)
            for (qa_resource *r = pool->loose_buckets[bucket]; r; r = r->identity_next) {
                if (++chain_count > count) goto invalid;
            }
        if (!vfs_save_count(io, &chain_count, 8, sizeof(qa_resource *)) ||
            chain_count > count - admitted) goto done;
        qa_resource **tail = &pool->loose_buckets[bucket];
        for (size_t i = 0; i < chain_count; ++i) {
            uint64_t ordinal = (io->direction == QA_SOURCE_SAVE_READ) ? UINT64_MAX : resource_index(pool, *tail);
            if (!qa_source_save_u64(io, &ordinal)) goto done;
            if (ordinal >= resource_total || seen[ordinal] || resources[ordinal]->archive ||
                ((size_t)qa_fs_identity_hash(&resources[ordinal]->identity) & (capacity - 1)) != bucket)
                goto invalid;
            qa_resource *r = resources[ordinal];
            for (qa_resource *other = pool->loose_buckets[bucket]; other != *tail; other = other->identity_next)
                if (qa_fs_identity_equal(&other->identity, &r->identity)) goto invalid;
            if (io->direction == QA_SOURCE_SAVE_READ) *tail = r;
            tail = &r->identity_next;
            seen[ordinal] = true;
            ++admitted;
        }
    }
    if (admitted != count) goto invalid;
    for (size_t i = 0; i < resource_total; ++i)
        if (seen[i] != (resources[i]->archive == NULL)) goto invalid;
    success = true;
    goto done;
invalid:
    vfs_save_fail(io, QA_ERROR_FORMAT, "VFS loose cache topology differs");
done:
    free(seen);
    return success;
}

static bool pool_fields(qa_source_save_io *io, qa_resource_pool *pool,
    const qa_vfs *const *views, size_t view_count, const qa_vfs_checkpoint_refs *refs)
{
    qa_resource **index = NULL;
    size_t count = 0;
    bool success = vfs_save_magic(io, pool_magic) &&
        qa_source_save_u64(io, &pool->next_resource) && packages(io, pool, views, view_count, refs) &&
        resources(io, pool, &index, &count) && loose_index(io, pool, index, count);
    free(index);
    if (!success && io->error && io->error->code == QA_OK)
        vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS resource pool continuation");
    return success;
}

bool qa_resource_pool_checkpoint(const qa_resource_pool *pool, qa_buffer *out, qa_error *error)
{
    if (!pool || !out || out->data || out->size || !pool->references) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource checkpoint requires a live pool and empty output");
        return false;
    }
    qa_resource_pool saved = *pool;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) &&
        pool_fields(&io, &saved, NULL, 0, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return success;
}
bool qa_resource_pool_checkpoint_linked(const qa_resource_pool *pool,
    const qa_vfs *const *views, size_t count, qa_buffer *out, qa_error *error)
{
    if (!pool || !out || out->data || out->size || !pool->references || (count && !views)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Linked resource capture requires its actual graph views");
        return false;
    }
    qa_resource_pool saved = *pool;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) &&
        pool_fields(&io, &saved, views, count, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return success;
}

bool qa_resource_pool_restore_ready(const qa_resource_pool *pool, qa_error *error)
{
    if (!pool || !pool->references) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource restore requires a live isolated pool");
        return false;
    }
    for (const qa_resource *r = pool->resources; r; r = r->next)
        if (r->references != 1) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource restore must precede external readers");
            return false;
        }
    for (const package *p = pool->packages; p; p = p->next) {
        size_t references = 1;
        for (const qa_resource *r = pool->resources; r; r = r->next)
            if (r->archive == p) ++references;
        if (p->references != references) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource restore must precede archive mounts");
            return false;
        }
    }
    return true;
}

bool qa_resource_pool_restore(qa_resource_pool *pool, qa_bytes bytes, qa_error *error)
{
    if (bytes.size && !bytes.data) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "resource restore has no checkpoint bytes");
        return false;
    }
    return qa_resource_pool_restore_linked(pool, NULL, bytes, error);
}

bool qa_resource_pool_restore_linked(qa_resource_pool *pool,
    const qa_vfs_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!qa_resource_pool_restore_ready(pool, error) || (bytes.size && !bytes.data)) return false;
    qa_resource_pool *candidate = qa_resource_pool_create(error);
    if (!candidate) return false;
    qa_source_save_io io = {.direction = QA_SOURCE_SAVE_READ, .input = bytes, .error = error};
    if (!pool_fields(&io, candidate, NULL, 0, refs) || !qa_source_save_finish(&io, NULL) ||
        !qa_resource_pool_restore_ready(pool, error)) {
        qa_resource_pool_destroy(candidate); return false;
    }
    qa_resource_pool displaced = *pool;
    *pool = *candidate; pool->references = displaced.references;
    for (package *p = pool->packages; p; p = p->next) p->pool = pool;
    *candidate = displaced; candidate->references = 1;
    qa_resource_pool_destroy(candidate); return true;
}
