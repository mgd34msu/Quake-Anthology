#include "qa/vfs_view_save.h"
#include "vfs_private.h"
#include "vfs_save_io.h"

static const uint8_t view_magic[8] = {'Q','A','V','F',1,0,0,0};
typedef struct mount_binding {
    char *root_path;
    qa_fs_identity root_identity;
} mount_binding;

qa_resource_pool *qa_vfs_resources(const qa_vfs *vfs)
{
    return vfs ? vfs->pool : NULL;
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
static mount *find_mount(const qa_vfs *vfs, qa_mount_id id)
{
    for (size_t i = 0; i < vfs->count; ++i)
        if (vfs->mounts[i]->id == id) return vfs->mounts[i];
    return NULL;
}
static bool normalized_prefix(vfs_save_io *io, const char *text,
                               bool allow_empty, bool allow_trailing)
{
    size_t length = strlen(text);
    if (!length) return allow_empty || vfs_save_fail(io, QA_ERROR_FORMAT, "empty VFS rule prefix");
    bool trailing = text[length - 1] == '/';
    if (trailing && !allow_trailing)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS rule prefix separator");
    char *input = malloc(length + 1);
    if (!input) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS rule admission");
    memcpy(input, text, length + 1);
    if (trailing) input[length - 1] = 0;
    char *normalized = qa_vfs_normalize_path(input, io->error);
    bool matches = normalized && !strcmp(normalized, input);
    free(input);
    free(normalized);
    return matches || vfs_save_fail(io, QA_ERROR_FORMAT, "unnormalized VFS rule prefix");
}

static bool mounts(vfs_save_io *io, qa_vfs *vfs, mount_binding **bindings_out)
{
    size_t count = vfs->count;
    if (!vfs_save_count(io, &count, 91, sizeof(mount *)) ||
        count > SIZE_MAX / sizeof(mount_binding)) return false;
    mount_binding *bindings = count ? calloc(count, sizeof(*bindings)) : NULL;
    if (count && !bindings) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS native bindings");
    *bindings_out = bindings;
    if (io->reading) {
        vfs->mounts = count ? calloc(count, sizeof(*vfs->mounts)) : NULL;
        if (count && !vfs->mounts) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS mounts");
    }
    for (size_t i = 0; i < count; ++i) {
        mount saved = {0};
        mount *m;
        if (io->reading) {
            m = calloc(1, sizeof(*m));
            if (!m) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS mount");
            vfs->mounts[vfs->count++] = m;
        } else { saved = *vfs->mounts[i]; m = &saved; }
        uint64_t origin = io->reading ? UINT64_MAX : package_index(vfs->pool, m->archive);
        uint64_t comparison = m->comparison;
        if ((!io->reading && m->archive && origin == UINT64_MAX) ||
            !vfs_save_u64(io, &m->id) || !m->id ||
            (vfs->next_mount && m->id >= vfs->next_mount) ||
            !vfs_save_text(io, &m->path) || !m->path[0] ||
            !vfs_save_u64(io, &comparison) || comparison > QA_ARCHIVE_CASE_INSENSITIVE ||
            !vfs_save_bool(io, &m->writable) || !vfs_save_bool(io, &m->user_overlay) ||
            !vfs_save_bool(io, &m->referenced) || !vfs_save_identity(io, &m->identity) ||
            !vfs_save_u64(io, &origin)) return false;
        m->comparison = (qa_archive_comparison)comparison;
        for (size_t j = 0; j < i; ++j)
            if (vfs->mounts[j]->id == m->id)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS mount identity");
        if (origin != UINT64_MAX) {
            package *p = package_at(vfs->pool, origin);
            if (!p || m->writable || m->user_overlay || m->root)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS archive mount");
            uint64_t kind = io->reading ? 0 : qa_archive_get_kind(p->archive);
            qa_sha256_digest digest = p->digest;
            if (!vfs_save_u64(io, &kind) || !vfs_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
                kind != (uint64_t)qa_archive_get_kind(p->archive) || !qa_sha256_equal(&digest, &p->digest))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS mount package namespace differs");
            if (io->reading) { m->archive = p; ++p->references; }
            else if (!m->archive_file)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS archive mount lacks native file");
        } else {
            qa_fs_identity zero = {0};
            if (m->archive_file || !qa_fs_identity_equal(&m->identity, &zero))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS directory metadata");
            if (!io->reading) {
                qa_fs_entry_kind kind;
                if (!m->root || !qa_fs_root_join(m->root, "", &bindings[i].root_path, io->error) ||
                    !qa_fs_root_status(m->root, "", &kind, &bindings[i].root_identity, io->error))
                    return false;
                if (kind != QA_FS_DIRECTORY)
                    return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS retained root is not a directory");
            }
            if (!vfs_save_text(io, &bindings[i].root_path) || !bindings[i].root_path[0] ||
                !vfs_save_identity(io, &bindings[i].root_identity)) return false;
        }
    }
    return true;
}

static bool prefixes(vfs_save_io *io, qa_vfs *vfs)
{
    size_t count = 0;
    if (!io->reading) for (prefix_order *p = vfs->prefixes; p; p = p->next) ++count;
    if (!vfs_save_count(io, &count, 8, sizeof(prefix_order))) return false;
    prefix_order **tail = &vfs->prefixes;
    for (size_t i = 0; i < count; ++i) {
        prefix_order saved = {0};
        prefix_order *p;
        if (io->reading) {
            p = calloc(1, sizeof(*p));
            if (!p) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS prefix");
            *tail = p;
        } else { saved = **tail; p = &saved; }
        if (!vfs_save_text(io, &p->prefix) || !normalized_prefix(io, p->prefix, false, false)) return false;
        for (prefix_order *other = vfs->prefixes; other != *tail; other = other->next)
            if (qa_archive_paths_equal(other->prefix, p->prefix, QA_ARCHIVE_CASE_INSENSITIVE))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS prefix rule");
        if (io->reading) {
            if (vfs->count > (io->input.size - io->position) / 8)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "truncated VFS prefix order");
            p->order = vfs->count ? calloc(vfs->count, sizeof(*p->order)) : NULL;
            if (vfs->count && !p->order)
                return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS prefix order");
        } else if (vfs->count && !p->order)
            return vfs_save_fail(io, QA_ERROR_FORMAT, "missing VFS prefix order");
        for (size_t j = 0; j < vfs->count; ++j) {
            uint64_t id = p->order[j];
            if (!vfs_save_u64(io, &id) || !find_mount(vfs, id)) return false;
            if (io->reading) p->order[j] = id;
            for (size_t k = 0; k < j; ++k)
                if (p->order[k] == id) return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS prefix mount");
        }
        tail = &(*tail)->next;
    }
    return true;
}
static bool links(vfs_save_io *io, qa_vfs *vfs)
{
    size_t count = 0;
    if (!io->reading) for (resource_link *l = vfs->links; l; l = l->next) ++count;
    if (!vfs_save_count(io, &count, 24, sizeof(resource_link))) return false;
    resource_link **tail = &vfs->links;
    for (size_t i = 0; i < count; ++i) {
        resource_link saved = {0};
        resource_link *l;
        if (io->reading) {
            l = calloc(1, sizeof(*l));
            if (!l) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS link");
            *tail = l;
        } else { saved = **tail; l = &saved; }
        if (!vfs_save_text(io, &l->source) || !vfs_save_text(io, &l->target) ||
            !normalized_prefix(io, l->source, false, true) ||
            !normalized_prefix(io, l->target, true, true) || !vfs_save_u64(io, &l->mount)) return false;
        mount *destination = find_mount(vfs, l->mount);
        if (!destination || destination->archive)
            return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS link destination");
        for (resource_link *other = vfs->links; other != *tail; other = other->next)
            if (!strcmp(other->source, l->source))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS link rule");
        tail = &(*tail)->next;
    }
    return true;
}

static bool demo_package(const package *p)
{
    if (qa_archive_get_kind(p->archive) != QA_ARCHIVE_PK3) return false;
    qa_md4_context hash;
    qa_md4_init(&hash);
    for (size_t i = 0; i < qa_archive_count(p->archive); ++i) {
        const qa_archive_entry *entry = qa_archive_entry_at(p->archive, i);
        if (!entry->size) continue;
        uint8_t word[4];
        qa_store_u32le(word, entry->crc32);
        qa_md4_update(&hash, (qa_bytes){word, sizeof(word)});
    }
    qa_md4_digest digest;
    qa_md4_final(&hash, &digest);
    return qa_md4_fold(&digest) == UINT32_C(437558517);
}
static bool priority_matches(const qa_vfs *vfs, const qa_mount_id *order)
{
    size_t first = 0;
    for (size_t i = 0; i < vfs->pure_count; ++i) {
        for (size_t j = first; j < vfs->count; ++j) {
            const mount *m = order ? find_mount(vfs, order[j]) : vfs->mounts[j];
            if (!m->archive || !qa_sha256_equal(&m->archive->digest, &vfs->pure[i])) continue;
            if (j != first) return false;
            ++first;
            break;
        }
    }
    return true;
}
static bool restrictions(vfs_save_io *io, qa_vfs *vfs)
{
    size_t count = vfs->pure_count;
    if (!vfs_save_count(io, &count, 32, sizeof(qa_sha256_digest))) return false;
    if (io->reading) {
        vfs->pure = count ? calloc(count, sizeof(*vfs->pure)) : NULL;
        if (count && !vfs->pure) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS restrictions");
        vfs->pure_count = count;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_sha256_digest digest = vfs->pure[i];
        if (!vfs_save_bytes(io, digest.bytes, sizeof(digest.bytes))) return false;
        if (io->reading) vfs->pure[i] = digest;
        bool found = false;
        for (size_t j = 0; j < vfs->count; ++j)
            if (vfs->mounts[j]->archive && qa_sha256_equal(&vfs->mounts[j]->archive->digest, &digest)) found = true;
        if (!found) return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS required archive is missing");
    }
    if (!vfs_save_bool(io, &vfs->q3_demo)) return false;
    if (vfs->q3_demo)
        for (size_t i = 0; i < vfs->count; ++i)
            if (vfs->mounts[i]->archive && !demo_package(vfs->mounts[i]->archive))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS demo archive policy differs");
    if (!priority_matches(vfs, NULL))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS pure mount priority differs");
    for (prefix_order *p = vfs->prefixes; p; p = p->next)
        if (!priority_matches(vfs, p->order))
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS pure prefix priority differs");
    return true;
}

static bool view_fields(vfs_save_io *io, qa_vfs *vfs, mount_binding **bindings)
{
    bool success = vfs_save_magic(io, view_magic) && vfs_save_u64(io, &vfs->next_mount) &&
        vfs_save_u64(io, &vfs->next_temporary) && mounts(io, vfs, bindings) &&
        prefixes(io, vfs) && links(io, vfs) && restrictions(io, vfs) && vfs_save_finish(io);
    if (!success && io->error && io->error->code == QA_OK)
        vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS view continuation");
    return success;
}
static void bindings_free(mount_binding *bindings, size_t count)
{
    if (bindings) for (size_t i = 0; i < count; ++i) free(bindings[i].root_path);
    free(bindings);
}
static bool archive_ready(vfs_save_io *io, const mount *m)
{
    bool unchanged;
    if (!qa_fs_file_path_unchanged(m->archive_file, &m->identity, &unchanged, io->error)) return false;
    if (!unchanged) return vfs_save_fail(io, QA_ERROR_IO, "VFS mounted archive changed");
    qa_buffer bytes = {0};
    if (!qa_fs_file_read_snapshot(m->archive_file, &m->identity, &bytes, io->error)) return false;
    bool matches = bytes.size == m->archive->storage.size &&
        (!bytes.size || !memcmp(bytes.data, m->archive->storage.data, bytes.size));
    qa_buffer_free(&bytes);
    return matches || vfs_save_fail(io, QA_ERROR_FORMAT, "VFS mounted archive bytes differ");
}
static bool native_bind(vfs_save_io *io, qa_vfs *vfs, const mount_binding *bindings,
                         const qa_vfs_checkpoint_refs *refs)
{
    for (size_t i = 0; i < vfs->count; ++i) {
        mount *m = vfs->mounts[i];
        if (m->archive) {
            bool opened = refs && refs->archive_open ?
                refs->archive_open(refs->context, m->path, &m->archive_file, &m->identity, io->error) :
                qa_fs_file_open(m->path, &m->archive_file, &m->identity, io->error);
            if (!opened) return false;
            if (!m->archive_file) return vfs_save_fail(io, QA_ERROR_ARGUMENT, "VFS archive admission returned no file");
            if (!archive_ready(io, m)) return false;
        } else {
            bool mapped = refs && refs->directory_open;
            bool opened = mapped ?
                refs->directory_open(refs->context, m->path, bindings[i].root_path,
                    &bindings[i].root_identity, &m->root, io->error) :
                qa_fs_root_open(bindings[i].root_path, &m->root, io->error);
            if (!opened) return false;
            qa_fs_entry_kind kind;
            qa_fs_identity identity;
            if (!m->root || !qa_fs_root_status(m->root, "", &kind, &identity, io->error)) return false;
            if (kind != QA_FS_DIRECTORY || (!mapped &&
                (identity.words[0] != bindings[i].root_identity.words[0] ||
                 identity.words[1] != bindings[i].root_identity.words[1])))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS retained directory object differs");
        }
    }
    return true;
}

bool qa_vfs_checkpoint(const qa_vfs *vfs, qa_buffer *out, qa_error *error)
{
    if (!vfs || !vfs->pool || !out || out->data || out->size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS checkpoint requires a live view and empty output");
        return false;
    }
    qa_vfs saved = *vfs;
    mount_binding *bindings = NULL;
    vfs_save_io io = {.error = error};
    bool success = view_fields(&io, &saved, &bindings);
    if (success) for (size_t i = 0; i < vfs->count; ++i)
        if (vfs->mounts[i]->archive && !archive_ready(&io, vfs->mounts[i])) { success = false; break; }
    bindings_free(bindings, vfs->count);
    if (!success) { qa_buffer_free(&io.output); return false; }
    *out = io.output;
    return true;
}
bool qa_vfs_create_restored(qa_resource_pool *pool, const qa_vfs_checkpoint_refs *refs,
                            qa_bytes bytes, qa_vfs **out, qa_error *error)
{
    if (!pool || !out || *out || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS restore requires its real pool and empty output");
        return false;
    }
    qa_vfs *candidate = qa_vfs_create(pool, error);
    if (!candidate) return false;
    mount_binding *bindings = NULL;
    vfs_save_io io = {.reading = true, .input = bytes, .error = error};
    bool success = view_fields(&io, candidate, &bindings) && native_bind(&io, candidate, bindings, refs);
    bindings_free(bindings, candidate->count);
    if (!success) {
        if (error && error->code == QA_OK) vfs_save_fail(&io, QA_ERROR_FORMAT, "VFS native admission failed");
        qa_vfs_destroy(candidate);
        return false;
    }
    *out = candidate;
    return true;
}
bool qa_vfs_restore(qa_vfs *vfs, const qa_vfs_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!vfs || !vfs->pool) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "VFS import requires its installed idle view");
        return false;
    }
    qa_vfs *candidate = NULL;
    if (!qa_vfs_create_restored(vfs->pool, refs, bytes, &candidate, error)) return false;
    qa_vfs displaced = *vfs;
    *vfs = *candidate;
    *candidate = displaced;
    qa_vfs_destroy(candidate);
    return true;
}
