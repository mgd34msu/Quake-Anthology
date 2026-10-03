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
static uint64_t package_index(const qa_resource_pool *pool, const package *wanted)
{
    uint64_t index = 0;
    for (const package *p = pool->packages; p; p = p->next, ++index)
        if (p == wanted) return index;
    return UINT64_MAX;
}
static package *package_at(qa_resource_pool *pool, uint64_t index)
{
    package *p = pool->packages;
    while (p && index) { p = p->next; --index; }
    return p;
}
static uint64_t resource_index(const qa_resource_pool *pool, const qa_resource *wanted)
{
    uint64_t index = 0;
    for (const qa_resource *r = pool->resources; r; r = r->next, ++index)
        if (r == wanted) return index;
    return UINT64_MAX;
}
static bool digest_matches(qa_bytes bytes, const qa_sha256_digest *expected)
{
    qa_sha256_digest actual;
    qa_sha256(bytes, &actual);
    return qa_sha256_equal(&actual, expected);
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
    return NULL;
}
static bool package_storage(vfs_save_io *io, package *p, const package *physical,
    const qa_vfs *const *views, size_t view_count,
    const qa_vfs_checkpoint_refs *refs)
{
    char *path = io->reading ? NULL : (char *)mounted_package_path(physical, views, view_count);
    bool linked = path != NULL;
    if (!vfs_save_bool(io, &linked)) return false;
    if (!linked) return vfs_save_buffer(io, &p->storage);
    size_t size = p->storage.size;
    bool ok = vfs_save_text(io, &path) && path[0] && vfs_save_size(io, &size);
    if (ok && io->reading) {
        qa_fs_file *file = NULL;
        qa_fs_identity identity;
        ok = refs && refs->archive_open ?
            refs->archive_open(refs->context, path, &file, &identity, io->error) :
            qa_fs_file_open(path, &file, &identity, io->error);
        if (ok && !file) ok = vfs_save_fail(io, QA_ERROR_ARGUMENT, "Linked archive resolver returned no file");
        if (ok) ok = qa_fs_file_read_snapshot(file, &identity, &p->storage, io->error);
        qa_fs_file_close(file);
        if (ok && p->storage.size != size)
            ok = vfs_save_fail(io, QA_ERROR_FORMAT, "Linked archive size differs");
    }
    if (io->reading) free(path);
    return ok;
}
static bool packages(vfs_save_io *io, qa_resource_pool *pool,
    const qa_vfs *const *views, size_t view_count, const qa_vfs_checkpoint_refs *refs)
{
    size_t count = io->reading ? 0 : package_count(pool);
    if (!vfs_save_count(io, &count, 104, sizeof(package))) return false;
    package **tail = &pool->packages;
    for (size_t i = 0; i < count; ++i) {
        package saved = {0};
        package *p;
        if (io->reading) {
            p = calloc(1, sizeof(*p));
            if (!p) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS package");
            p->references = 1;
            *tail = p;
        } else { saved = **tail; p = &saved; }
        uint64_t kind = io->reading ? 0 : qa_archive_get_kind(p->archive);
        if (!vfs_save_u64(io, &kind) || kind < QA_ARCHIVE_PAK || kind > QA_ARCHIVE_KPF ||
            !vfs_save_identity(io, &p->identity) ||
            !vfs_save_bytes(io, p->digest.bytes, sizeof(p->digest.bytes)) ||
            !package_storage(io, p, io->reading ? NULL : *tail, views, view_count, refs)) return false;
        qa_bytes storage = {p->storage.data, p->storage.size};
        if (qa_fs_identity_size(&p->identity) != storage.size || !digest_matches(storage, &p->digest))
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS package retained identity differs");
        if (io->reading) {
            if (!qa_archive_open_memory(storage, (qa_archive_kind)kind, &p->archive, io->error)) return false;
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
        /* The actual package cache coalesces equal bytes and format. */
        for (package *other = pool->packages; other != *tail; other = other->next)
            if (qa_archive_get_kind(other->archive) == kind &&
                other->storage.size == storage.size &&
                qa_sha256_equal(&other->digest, &p->digest) &&
                (!storage.size || !memcmp(other->storage.data, storage.data, storage.size)))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate retained VFS package");
        tail = &(*tail)->next;
    }
    return true;
}

static bool resource_record(vfs_save_io *io, qa_resource_pool *pool,
                            qa_resource *r, uint64_t previous_id)
{
    uint64_t origin = io->reading ? UINT64_MAX : package_index(pool, r->archive);
    if (!io->reading && r->archive && origin == UINT64_MAX)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "resource has a foreign VFS package");
    if (!vfs_save_u64(io, &r->id) || !r->id || (previous_id && r->id >= previous_id) ||
        (pool->next_resource && r->id >= pool->next_resource) ||
        !vfs_save_text(io, &r->path) || !vfs_save_u64(io, &origin) ||
        !vfs_save_size(io, &r->ordinal) || !vfs_save_identity(io, &r->identity) ||
        !vfs_save_bytes(io, r->digest.bytes, sizeof(r->digest.bytes)))
        return false;
    char *normalized = qa_vfs_normalize_path(r->path, io->error);
    if (!normalized) return false;
    bool path_matches = !strcmp(normalized, r->path);
    free(normalized);
    if (!path_matches) return vfs_save_fail(io, QA_ERROR_FORMAT, "unnormalized VFS resource path");
    package *archive = origin == UINT64_MAX ? NULL : package_at(pool, origin);
    if (origin != UINT64_MAX && !archive)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "unknown retained VFS package");
    const qa_archive_entry *entry = archive ? qa_archive_entry_at(archive->archive, r->ordinal) : NULL;
    if (archive && (!entry || entry->is_directory || strcmp(entry->path, r->path)))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource member identity differs");
    if (io->reading && archive) {
        /* Install only a bounded member ordinal before ordinary failure cleanup. */
        r->archive = archive;
        ++archive->references;
    }
    qa_fs_identity zero = {0};
    if ((archive && !qa_fs_identity_equal(&r->identity, &zero)) || (!archive && r->ordinal))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid retained VFS resource metadata");
    bool owned = archive ? entry->compression_method == 8 : true;
    bool saved_owned = owned;
    if (!vfs_save_bool(io, &saved_owned) || saved_owned != owned)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource byte ownership differs");
    if (owned) {
        if (!vfs_save_buffer(io, &r->data.owned)) return false;
        if (io->reading) {
            /* Deflated empty members have a genuine owned one-byte allocation. */
            if (archive && !r->data.owned.size) {
                r->data.owned.data = malloc(1);
                if (!r->data.owned.data)
                    return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating empty restored member");
            }
            r->data.bytes = (qa_bytes){r->data.owned.data, r->data.owned.size};
        } else if (r->data.bytes.data != r->data.owned.data ||
                   r->data.bytes.size != r->data.owned.size) {
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource owned span differs");
        }
    } else {
        qa_bytes expected = {archive->storage.data + entry->data_offset, entry->size};
        if (io->reading) r->data.bytes = expected;
        else if (r->data.owned.data || r->data.owned.size ||
                 r->data.bytes.data != expected.data || r->data.bytes.size != expected.size)
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource borrowed span differs");
    }
    if ((archive ? entry->size : qa_fs_identity_size(&r->identity)) != r->data.bytes.size ||
        !digest_matches(r->data.bytes, &r->digest))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS resource retained bytes differ");
    if (archive) {
        /* Qualify immutable member provenance without replacing the saved
         * cache bytes or publishing a new resource/decoder owner. */
        qa_archive_data actual = {0};
        if (!qa_archive_read(archive->archive, r->ordinal, &actual, io->error)) return false;
        bool same = actual.bytes.size == r->data.bytes.size &&
            (!actual.bytes.size || !memcmp(actual.bytes.data, r->data.bytes.data, actual.bytes.size));
        qa_archive_data_free(&actual);
        if (!same) return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS decoded member provenance differs");
    }
    if (archive) {
        if (io->reading) {
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

static bool resources(vfs_save_io *io, qa_resource_pool *pool,
                       qa_resource ***index_out, size_t *count_out)
{
    size_t count = io->reading ? 0 : resource_count(pool);
    if (!vfs_save_count(io, &count, 121, sizeof(qa_resource *))) return false;
    qa_resource **index = count ? calloc(count, sizeof(*index)) : NULL;
    if (count && !index) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS resource graph");
    *index_out = index;
    *count_out = count;
    qa_resource **tail = &pool->resources;
    uint64_t previous_id = 0;
    for (size_t i = 0; i < count; ++i) {
        qa_resource saved = {0};
        qa_resource *r;
        if (io->reading) {
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

static bool loose_index(vfs_save_io *io, qa_resource_pool *pool,
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
    if (io->reading) {
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
        if (!io->reading)
            for (qa_resource *r = pool->loose_buckets[bucket]; r; r = r->identity_next) {
                if (++chain_count > count) goto invalid;
            }
        if (!vfs_save_count(io, &chain_count, 8, sizeof(qa_resource *)) ||
            chain_count > count - admitted) goto done;
        qa_resource **tail = &pool->loose_buckets[bucket];
        for (size_t i = 0; i < chain_count; ++i) {
            uint64_t ordinal = io->reading ? UINT64_MAX : resource_index(pool, *tail);
            if (!vfs_save_u64(io, &ordinal)) goto done;
            if (ordinal >= resource_total || seen[ordinal] || resources[ordinal]->archive ||
                ((size_t)qa_fs_identity_hash(&resources[ordinal]->identity) & (capacity - 1)) != bucket)
                goto invalid;
            qa_resource *r = resources[ordinal];
            for (qa_resource *other = pool->loose_buckets[bucket]; other != *tail; other = other->identity_next)
                if (qa_fs_identity_equal(&other->identity, &r->identity)) goto invalid;
            if (io->reading) *tail = r;
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

static bool pool_fields(vfs_save_io *io, qa_resource_pool *pool,
    const qa_vfs *const *views, size_t view_count, const qa_vfs_checkpoint_refs *refs)
{
    qa_resource **index = NULL;
    size_t count = 0;
    bool success = vfs_save_magic(io, pool_magic) &&
        vfs_save_u64(io, &pool->next_resource) && packages(io, pool, views, view_count, refs) &&
        resources(io, pool, &index, &count) && loose_index(io, pool, index, count) &&
        vfs_save_finish(io);
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
    vfs_save_io io = {.error = error};
    if (!pool_fields(&io, &saved, NULL, 0, NULL)) { qa_buffer_free(&io.output); return false; }
    *out = io.output;
    return true;
}
bool qa_resource_pool_checkpoint_linked(const qa_resource_pool *pool,
    const qa_vfs *const *views, size_t count, qa_buffer *out, qa_error *error)
{
    if (!pool || !out || out->data || out->size || !pool->references || (count && !views)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Linked resource capture requires its actual graph views");
        return false;
    }
    qa_resource_pool saved = *pool;
    vfs_save_io io = {.error = error};
    if (!pool_fields(&io, &saved, views, count, NULL)) { qa_buffer_free(&io.output); return false; }
    *out = io.output; return true;
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

/* Borrow only bounded primitive spans during admission. No native handle or
 * resource owner is constructed until the complete pool has passed this cut. */
static bool admit_buffer(vfs_save_io *io, qa_bytes *bytes)
{
    size_t size = 0;
    if (!vfs_save_size(io, &size) || size > io->input.size - io->position)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "Truncated retained resource span");
    *bytes = (qa_bytes){io->input.data + io->position, size};
    io->position += size; return true;
}
typedef struct admitted_resource {
    qa_fs_identity identity;
    bool loose, seen;
} admitted_resource;
bool qa_resource_pool_checkpoint_validate(qa_bytes bytes, qa_error *error)
{
    if (!bytes.data || bytes.size < sizeof(pool_magic) || memcmp(bytes.data, pool_magic, sizeof(pool_magic))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid resource pool checkpoint magic"); return false;
    }
    vfs_save_io io = {.reading = true, .input = bytes, .position = sizeof(pool_magic), .error = error};
    uint64_t next = 0;
    size_t package_total = 0;
    bool ok = vfs_save_u64(&io, &next) && vfs_save_count(&io, &package_total, 104, sizeof(package));
    for (size_t i = 0; ok && i < package_total; ++i) {
        uint64_t kind = 0;
        qa_fs_identity identity;
        qa_sha256_digest digest;
        bool linked = false;
        ok = vfs_save_u64(&io, &kind) && kind >= QA_ARCHIVE_PAK && kind <= QA_ARCHIVE_KPF &&
            vfs_save_identity(&io, &identity) && vfs_save_bytes(&io, digest.bytes, sizeof(digest.bytes)) &&
            vfs_save_bool(&io, &linked);
        if (ok && linked) {
            char *path = NULL; size_t size = 0;
            ok = vfs_save_text(&io, &path) && path[0] && vfs_save_size(&io, &size) &&
                size == qa_fs_identity_size(&identity);
            free(path);
        } else if (ok) {
            qa_bytes storage;
            ok = admit_buffer(&io, &storage) && storage.size == qa_fs_identity_size(&identity) &&
                digest_matches(storage, &digest);
        }
    }
    size_t total = 0;
    ok = ok && vfs_save_count(&io, &total, 121, sizeof(admitted_resource));
    admitted_resource *rows = ok && total ? calloc(total, sizeof(*rows)) : NULL;
    if (ok && total && !rows) ok = vfs_save_fail(&io, QA_ERROR_MEMORY, "Admitting retained resource metadata");
    uint64_t previous = 0;
    for (size_t i = 0; ok && i < total; ++i) {
        uint64_t id = 0, origin = 0; size_t ordinal = 0;
        char *path = NULL;
        qa_sha256_digest digest;
        bool owned = false;
        ok = vfs_save_u64(&io, &id) && id && (!previous || id < previous) && (!next || id < next) &&
            vfs_save_text(&io, &path) && vfs_save_u64(&io, &origin) &&
            (origin == UINT64_MAX || origin < package_total) && vfs_save_size(&io, &ordinal) &&
            vfs_save_identity(&io, &rows[i].identity) &&
            vfs_save_bytes(&io, digest.bytes, sizeof(digest.bytes)) && vfs_save_bool(&io, &owned);
        if (ok) {
            char *normalized = qa_vfs_normalize_path(path, error);
            ok = normalized && !strcmp(normalized, path); free(normalized);
        }
        free(path);
        rows[i].loose = origin == UINT64_MAX;
        qa_fs_identity zero = {0};
        if (ok) ok = rows[i].loose ? (!ordinal && owned) : qa_fs_identity_equal(&rows[i].identity, &zero);
        if (ok && owned) {
            qa_bytes data;
            ok = admit_buffer(&io, &data) && digest_matches(data, &digest) &&
                (!rows[i].loose || data.size == qa_fs_identity_size(&rows[i].identity));
        }
        previous = id;
    }
    size_t capacity = 0, count = 0;
    ok = ok && vfs_save_count(&io, &capacity, 8, sizeof(qa_resource *)) &&
        vfs_save_size(&io, &count) && count <= total &&
        (!capacity || (capacity >= 64 && !(capacity & (capacity - 1)))) &&
        (capacity ? count <= capacity - capacity / 4 : !count);
    size_t admitted = 0;
    for (size_t bucket = 0; ok && bucket < capacity; ++bucket) {
        size_t chain = 0;
        ok = vfs_save_count(&io, &chain, 8, sizeof(qa_resource *)) && chain <= count - admitted;
        for (size_t i = 0; ok && i < chain; ++i) {
            uint64_t ordinal = 0;
            ok = vfs_save_u64(&io, &ordinal) && ordinal < total && rows[ordinal].loose &&
                !rows[ordinal].seen &&
                ((size_t)qa_fs_identity_hash(&rows[ordinal].identity) & (capacity - 1)) == bucket;
            for (size_t j = 0; ok && j < total; ++j)
                if (rows[j].seen && qa_fs_identity_equal(&rows[j].identity, &rows[ordinal].identity)) ok = false;
            if (ok) { rows[ordinal].seen = true; ++admitted; }
        }
    }
    for (size_t i = 0; ok && i < total; ++i) ok = rows[i].seen == rows[i].loose;
    ok = ok && admitted == count && vfs_save_finish(&io);
    free(rows);
    if (!ok && error && error->code == QA_OK)
        vfs_save_fail(&io, QA_ERROR_FORMAT, "Invalid resource pool primitive continuation");
    return ok;
}
bool qa_resource_pool_restore_linked(qa_resource_pool *pool,
    const qa_vfs_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!qa_resource_pool_restore_ready(pool, error) ||
        !qa_resource_pool_checkpoint_validate(bytes, error)) return false;
    qa_resource_pool *candidate = qa_resource_pool_create(error);
    if (!candidate) return false;
    vfs_save_io io = {.reading = true, .input = bytes, .error = error};
    if (!pool_fields(&io, candidate, NULL, 0, refs) ||
        !qa_resource_pool_restore_ready(pool, error)) {
        qa_resource_pool_destroy(candidate); return false;
    }
    qa_resource_pool displaced = *pool;
    *pool = *candidate; pool->references = displaced.references;
    *candidate = displaced; candidate->references = 1;
    qa_resource_pool_destroy(candidate); return true;
}
