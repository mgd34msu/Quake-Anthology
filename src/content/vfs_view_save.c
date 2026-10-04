#include "qa/vfs_view_save.h"
#include "vfs_private.h"
#include "vfs_save_io.h"

static const uint8_t view_magic[4] = {'Q','A','V','F'};
typedef struct mount_binding {
    char *root_path;
    qa_fs_identity root_identity;
    qa_fs_object_reference root_reference;
} mount_binding;

static mount *find_mount(const qa_vfs *vfs, qa_mount_id id)
{
    for (size_t i = 0; i < vfs->count; ++i)
        if (vfs->mounts[i]->id == id) return vfs->mounts[i];
    return NULL;
}
bool qa_vfs_mount_root_references(const qa_vfs *vfs, qa_mount_id id,
    const qa_fs_object_reference **references, size_t *count)
{
    const mount *source = vfs ? find_mount(vfs, id) : NULL;
    if (!source || source->archive || !source->root || !references || !count) return false;
    *references = source->root_references; *count = source->root_reference_count;
    return true;
}

static bool root_reference_fields(qa_source_save_io *io, qa_fs_object_reference *reference)
{
    uint64_t platform = reference->platform;
    if (!qa_source_save_u64(io, &platform) || platform < 1 || platform > 2)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "Invalid native root platform");
    if (io->direction == QA_SOURCE_SAVE_READ) reference->platform = (uint32_t)platform;
    for (size_t i = 0; i < 3; ++i)
        if (!qa_source_save_u64(io, &reference->words[i])) return false;
    return (platform != 1 || !reference->words[2]) ||
        vfs_save_fail(io, QA_ERROR_FORMAT, "Invalid POSIX root reference");
}
static bool root_reference_equal(const qa_fs_object_reference *left,
    const qa_fs_object_reference *right)
{
    return left->platform == right->platform &&
        !memcmp(left->words, right->words, sizeof(left->words));
}
static bool root_references_fields(qa_source_save_io *io, mount *source,
    const qa_fs_object_reference *admitted)
{
    size_t count = source->root_reference_count;
    if (!vfs_save_count(io, &count, 32, sizeof(*source->root_references)) || !count)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "Missing native root lineage");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        source->root_references = calloc(count, sizeof(*source->root_references));
        if (!source->root_references) return vfs_save_fail(io, QA_ERROR_MEMORY, "Restoring native root lineage");
        source->root_reference_count = count;
    } else if (!source->root_references)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "Missing retained native root lineage");
    bool present = false;
    for (size_t i = 0; i < count; ++i) {
        qa_fs_object_reference *reference = source->root_references + i;
        if (!root_reference_fields(io, reference)) return false;
        for (size_t j = 0; j < i; ++j)
            if (root_reference_equal(reference, source->root_references + j))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "Duplicate native root lineage");
        if (root_reference_equal(reference, admitted)) present = true;
    }
    return present || vfs_save_fail(io, QA_ERROR_FORMAT, "Admitted root absent from native lineage");
}
static bool normalized_prefix(qa_source_save_io *io, const char *text,
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

static bool mounts(qa_source_save_io *io, qa_vfs *vfs, mount_binding **bindings_out)
{
    size_t count = vfs->count;
    if (!vfs_save_count(io, &count, 92, sizeof(mount *)) ||
        count > SIZE_MAX / sizeof(mount_binding)) return false;
    mount_binding *bindings = count ? calloc(count, sizeof(*bindings)) : NULL;
    if (count && !bindings) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS native bindings");
    *bindings_out = bindings;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        vfs->mounts = count ? calloc(count, sizeof(*vfs->mounts)) : NULL;
        if (count && !vfs->mounts) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS mounts");
    }
    for (size_t i = 0; i < count; ++i) {
        mount saved = {0};
        mount *m;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            m = calloc(1, sizeof(*m));
            if (!m) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS mount");
            vfs->mounts[vfs->count++] = m;
        } else { saved = *vfs->mounts[i]; m = &saved; }
        uint64_t origin = (io->direction == QA_SOURCE_SAVE_READ) ? UINT64_MAX : vfs_package_index(vfs->pool, m->archive);
        uint64_t comparison = m->comparison;
        if ((io->direction != QA_SOURCE_SAVE_READ && m->archive && origin == UINT64_MAX) ||
            !qa_source_save_u64(io, &m->id) || !m->id ||
            (vfs->next_mount && m->id >= vfs->next_mount) ||
            !vfs_save_text(io, &m->path) || !m->path[0] ||
            !qa_source_save_u64(io, &comparison) || comparison > QA_ARCHIVE_CASE_INSENSITIVE ||
            !qa_source_save_bool(io, &m->writable) || !qa_source_save_bool(io, &m->user_overlay) ||
            !qa_source_save_bool(io, &m->referenced) || !qa_source_save_bool(io, &m->q3_demo) ||
            !vfs_save_identity(io, &m->identity) ||
            !qa_source_save_u64(io, &origin)) return false;
        m->comparison = (qa_archive_comparison)comparison;
        bool child = m->root_prefix != NULL;
        if (!qa_source_save_bool(io, &child)) return false;
        if (child && (!vfs_save_text(io, &m->root_prefix) ||
            !normalized_prefix(io, m->root_prefix, false, false))) return false;
        for (size_t j = 0; j < i; ++j)
            if (vfs->mounts[j]->id == m->id)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS mount identity");
        if (origin != UINT64_MAX) {
            package *p = vfs_package_at(vfs->pool, origin);
            if (!p || m->writable || m->user_overlay || m->root || m->root_prefix ||
                m->root_references || m->root_reference_count)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS archive mount");
            uint64_t kind = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : qa_archive_get_kind(p->archive);
            if (!qa_source_save_u64(io, &kind) || kind != (uint64_t)qa_archive_get_kind(p->archive))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS mount package namespace differs");
            if (io->direction == QA_SOURCE_SAVE_READ) { m->archive = p; ++p->references; }
            else if (!m->archive_file)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS archive mount lacks native file");
        } else {
            qa_fs_identity zero = {0};
            if (m->archive_file || !qa_fs_identity_equal(&m->identity, &zero))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS directory metadata");
            if (io->direction != QA_SOURCE_SAVE_READ) {
                qa_fs_entry_kind kind;
                if (!m->root || !qa_fs_root_join(m->root, "", &bindings[i].root_path, io->error) ||
                    !qa_fs_root_status(m->root, "", &kind, &bindings[i].root_identity, io->error) ||
                    !qa_fs_root_reference_read(m->root, &bindings[i].root_reference))
                    return false;
                if (kind != QA_FS_DIRECTORY)
                    return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS retained root is not a directory");
                if (!qa_fs_root_identity_is(m->root, &bindings[i].root_identity))
                    return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS retained root object differs from its metadata");
                const qa_fs_object_reference *reference = &bindings[i].root_reference;
                bindings[i].root_identity = (qa_fs_identity){{
                    reference->words[0], reference->words[1], 0, 0, 0,
                    reference->platform == 2 ? reference->words[2] : 0, 0
                }};
            }
            if (!vfs_save_text(io, &bindings[i].root_path) || !bindings[i].root_path[0] ||
                !vfs_save_identity(io, &bindings[i].root_identity) ||
                !root_reference_fields(io, &bindings[i].root_reference) ||
                !root_references_fields(io, m, &bindings[i].root_reference)) return false;
            const qa_fs_object_reference *reference = &bindings[i].root_reference;
            if (reference->words[0] != bindings[i].root_identity.words[0] ||
                reference->words[1] != bindings[i].root_identity.words[1] ||
                (reference->platform == 2 && reference->words[2] != bindings[i].root_identity.words[5]))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "Native root receipt differs from admitted metadata");
        }
    }
    return true;
}

static bool prefixes(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = 0;
    if (io->direction != QA_SOURCE_SAVE_READ) for (prefix_order *p = vfs->prefixes; p; p = p->next) ++count;
    if (!vfs_save_count(io, &count, 8, sizeof(prefix_order))) return false;
    prefix_order **tail = &vfs->prefixes;
    for (size_t i = 0; i < count; ++i) {
        prefix_order saved = {0};
        prefix_order *p;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            p = calloc(1, sizeof(*p));
            if (!p) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS prefix");
            *tail = p;
        } else { saved = **tail; p = &saved; }
        if (!vfs_save_text(io, &p->prefix) || !normalized_prefix(io, p->prefix, false, false)) return false;
        for (prefix_order *other = vfs->prefixes; other != *tail; other = other->next)
            if (qa_archive_paths_equal(other->prefix, p->prefix, QA_ARCHIVE_CASE_INSENSITIVE))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS prefix rule");
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (vfs->count > (io->input.size - io->offset) / 8)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "truncated VFS prefix order");
            p->order = vfs->count ? calloc(vfs->count, sizeof(*p->order)) : NULL;
            if (vfs->count && !p->order)
                return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS prefix order");
        } else if (vfs->count && !p->order)
            return vfs_save_fail(io, QA_ERROR_FORMAT, "missing VFS prefix order");
        for (size_t j = 0; j < vfs->count; ++j) {
            uint64_t id = p->order[j];
            if (!qa_source_save_u64(io, &id) || !find_mount(vfs, id)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) p->order[j] = id;
            for (size_t k = 0; k < j; ++k)
                if (p->order[k] == id) return vfs_save_fail(io, QA_ERROR_FORMAT, "duplicate VFS prefix mount");
        }
        tail = &(*tail)->next;
    }
    return true;
}
static bool links(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = 0;
    if (io->direction != QA_SOURCE_SAVE_READ) for (resource_link *l = vfs->links; l; l = l->next) ++count;
    if (!vfs_save_count(io, &count, 24, sizeof(resource_link))) return false;
    resource_link **tail = &vfs->links;
    for (size_t i = 0; i < count; ++i) {
        resource_link saved = {0};
        resource_link *l;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            l = calloc(1, sizeof(*l));
            if (!l) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS link");
            *tail = l;
        } else { saved = **tail; l = &saved; }
        if (!vfs_save_text(io, &l->source) || !vfs_save_text(io, &l->target) ||
            !normalized_prefix(io, l->source, false, true) ||
            !normalized_prefix(io, l->target, true, true) || !qa_source_save_u64(io, &l->mount)) return false;
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

static bool priority_matches(const qa_vfs *vfs, const qa_mount_id *order)
{
    size_t first = 0;
    for (size_t i = 0; i < vfs->pure_count; ++i) {
        for (size_t j = first; j < vfs->count; ++j) {
            const mount *m = order ? find_mount(vfs, order[j]) : vfs->mounts[j];
            if (!m->archive || !qa_fs_identity_equal(&m->archive->identity, &vfs->pure[i])) continue;
            if (j != first) return false;
            ++first;
            break;
        }
    }
    return true;
}
static bool restrictions(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = vfs->pure_count;
    if (!vfs_save_count(io, &count, 8, sizeof(qa_fs_identity))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        vfs->pure = count ? calloc(count, sizeof(*vfs->pure)) : NULL;
        if (count && !vfs->pure) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating restored VFS restrictions");
        vfs->pure_count = count;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_mount_id id = 0;
        if (io->direction != QA_SOURCE_SAVE_READ) for (size_t j = 0; j < vfs->count; ++j) {
            mount *m = vfs->mounts[j];
            if (m->archive && qa_fs_identity_equal(&m->archive->identity, &vfs->pure[i])) { id = m->id; break; }
        }
        if (!qa_source_save_u64(io, &id)) return false;
        mount *m = find_mount(vfs, id);
        if (!m || !m->archive) return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS required archive is missing");
        if (io->direction == QA_SOURCE_SAVE_READ) vfs->pure[i] = m->archive->identity;
    }
    if (!qa_source_save_bool(io, &vfs->q3_demo)) return false;
    for (size_t i = 0; i < vfs->count; ++i)
        if ((vfs->q3_demo || vfs->mounts[i]->q3_demo) && vfs->mounts[i]->archive &&
            !vfs_demo_package_allowed(vfs->mounts[i]->archive, io->error))
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS demo archive policy differs");
    if (!priority_matches(vfs, NULL))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS pure mount priority differs");
    for (prefix_order *p = vfs->prefixes; p; p = p->next)
        if (!priority_matches(vfs, p->order))
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS pure prefix priority differs");
    return true;
}

static bool opening_fields(qa_source_save_io *io, qa_vfs *vfs,
    qa_vfs_read_opening *opening, qa_mount_id mount_id, const char *path, bool linked)
{
    uint64_t rank = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : opening->rank < 0 ? UINT64_MAX : (uint64_t)opening->rank;
    char *prefix = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)(opening->prefix ? opening->prefix : "");
    bool ok = qa_source_save_u64(io, &rank) && (rank == UINT64_MAX || rank <= INT64_MAX) &&
        qa_source_save_bool(io, &opening->user_overlay) && vfs_save_text(io, &prefix) &&
        normalized_prefix(io, prefix, true, false) &&
        vfs_save_count(io, &opening->order_count, 8, sizeof(qa_mount_id));
    qa_mount_id *order = (io->direction == QA_SOURCE_SAVE_READ) && ok && opening->order_count ?
        calloc(opening->order_count, sizeof(*order)) : (qa_mount_id *)opening->order;
    if (ok && opening->order_count && !order)
        ok = vfs_save_fail(io, QA_ERROR_MEMORY, "Restoring first VFS opening order");
    for (size_t i = 0; ok && i < opening->order_count; ++i) {
        ok = qa_source_save_u64(io, order + i) && qa_vfs_mount_id_was_issued(vfs, order[i]);
        for (size_t j = 0; ok && j < i; ++j) if (order[j] == order[i]) ok = false;
    }
    if (ok && linked) ok = rank == UINT64_MAX && !opening->order_count && !*prefix && !opening->user_overlay;
    else if (ok) {
        ok = rank < opening->order_count && order[rank] == mount_id && (!*prefix || !opening->user_overlay);
        if (ok && *prefix) {
            size_t length = strlen(prefix);
            ok = strlen(path) > length && path[length] == '/';
            for (size_t i = 0; ok && i < length; ++i) {
                unsigned char a = (unsigned char)path[i], b = (unsigned char)prefix[i];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) ok = false;
            }
        }
    }
    if (io->direction == QA_SOURCE_SAVE_READ) {
        opening->rank = rank > INT64_MAX ? -1 : (int64_t)rank;
        opening->order = order;
        opening->prefix = prefix;
        if (ok && prefix && !*prefix) { free(prefix); opening->prefix = NULL; }
    }
    if (!ok && io->error && io->error->code != QA_OK) return false;
    return ok || vfs_save_fail(io, QA_ERROR_FORMAT, "Invalid historical VFS opening order");
}
static bool reads(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = vfs->read_count;
    if (!qa_source_save_u64(io, &vfs->read_generation) || !vfs->read_generation ||
        !vfs_save_count(io, &count, 48, sizeof(qa_vfs_read_reference))) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_mount_id mount_id = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : vfs->reads[i].mount;
        uint64_t resource_id = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : qa_resource_id(vfs->reads[i].resource);
        if (!qa_source_save_u64(io, &mount_id) || !qa_source_save_u64(io, &resource_id) || !resource_id) return false;
        mount *source = find_mount(vfs, mount_id);
        const qa_resource *resource = qa_resource_pool_find(vfs->pool, resource_id);
        if (!source || !resource || resource->archive != source->archive ||
            (source->archive && !source->referenced))
            return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS read provenance differs from its actual resource and mounted owner");
        char *path = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)vfs->reads[i].path;
        char *lookup = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)vfs->reads[i].lookup_path;
        char *from = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)vfs->reads[i].link_source;
        char *to = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)vfs->reads[i].link_target;
        bool fields = vfs_save_text(io, &path) && normalized_prefix(io, path, false, false) &&
            vfs_save_text(io, &lookup) && normalized_prefix(io, lookup, false, false) &&
            vfs_save_text(io, &from) && vfs_save_text(io, &to);
        qa_vfs_read_opening opening = (io->direction == QA_SOURCE_SAVE_READ) ? (qa_vfs_read_opening){0} : vfs->reads[i].opening;
        fields = fields && opening_fields(io, vfs, &opening, mount_id, path, from[0] != 0);
        if (!fields) {
            if (io->direction == QA_SOURCE_SAVE_READ) { free(path); free(lookup); free(from); free(to);
                free((void *)opening.order); free((char *)opening.prefix); }
            return false;
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            size_t previous = vfs->read_count;
            bool recorded = vfs_read_record(vfs, source, (qa_resource *)resource, path, lookup, from, to, false, &opening, io->error);
            free(path); free(lookup); free(from); free(to);
            free((void *)opening.order); free((char *)opening.prefix);
            if (!recorded || vfs->read_count != previous + 1)
                return vfs_save_fail(io, QA_ERROR_FORMAT, "Duplicate VFS read provenance");
        } else if (vfs->reads[i].resource != resource || !vfs_read_valid(vfs, vfs->reads + i, io->error)) return false;
    }
    return true;
}
static bool origins(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = 0;
    if (io->direction != QA_SOURCE_SAVE_READ) for (const resource_origin *row = vfs->origins; row; row = row->next)
        if (qa_resource_pool_find(vfs->pool, row->resource)) ++count;
    if (!vfs_save_count(io, &count, 80, sizeof(resource_origin))) return false;
    const resource_origin *source = vfs->origins;
    resource_origin **tail = &vfs->origins;
    for (size_t i = 0; i < count; ++i) {
        while (io->direction != QA_SOURCE_SAVE_READ && source && !qa_resource_pool_find(vfs->pool, source->resource)) source = source->next;
        resource_origin copy = source ? *source : (resource_origin){0};
        resource_origin *row = (io->direction == QA_SOURCE_SAVE_READ) ? calloc(1, sizeof(*row)) : &copy;
        if (!row) return vfs_save_fail(io, QA_ERROR_MEMORY, "Restoring historical resource origins");
        if (io->direction == QA_SOURCE_SAVE_READ) { *tail = row; tail = &row->next; }
        qa_vfs_resource_origin *r = &row->receipt;
        uint64_t format = r->format, comparison = r->comparison;
        char *path = (io->direction == QA_SOURCE_SAVE_READ) ? NULL : (char *)r->mount_path;
        bool ok = qa_source_save_u64(io, &row->mount) && qa_vfs_mount_id_was_issued(vfs, row->mount) &&
            qa_source_save_u64(io, &row->resource) && row->resource && vfs_save_text(io, &path) && *path &&
            qa_source_save_bool(io, &r->archive) && qa_source_save_u64(io, &format) &&
            qa_source_save_u64(io, &comparison) && comparison <= QA_ARCHIVE_CASE_INSENSITIVE &&
            vfs_save_identity(io, &r->mount_identity);
        if (io->direction == QA_SOURCE_SAVE_READ) r->mount_path = path;
        r->format = (qa_archive_kind)format; r->comparison = (qa_archive_comparison)comparison;
        if (ok && r->archive) ok = format > QA_ARCHIVE_AUTO && format <= QA_ARCHIVE_KPF;
        else if (ok) ok = format == QA_ARCHIVE_AUTO && root_reference_fields(io, &r->root_reference);
        const qa_resource *resource = ok ? qa_resource_pool_find(vfs->pool, row->resource) : NULL;
        if (!resource || (resource->archive != NULL) != r->archive ||
            (r->archive && qa_archive_get_kind(resource->archive->archive) != r->format)) ok = false;
        if (ok && r->archive) {
            if (io->direction == QA_SOURCE_SAVE_READ) r->archive_identity = resource->archive->identity;
            else ok = qa_fs_identity_equal(&resource->archive->identity, &r->archive_identity);
        }
        mount *live = find_mount(vfs, row->mount);
        if (ok && live) {
            ok = !strcmp(live->path, r->mount_path) && (live->archive != NULL) == r->archive;
            if (ok && r->archive) {
                ok = live->archive == resource->archive;
                if (io->direction == QA_SOURCE_SAVE_READ) r->mount_identity = live->archive->identity;
            }
            if (ok && !r->archive) {
                bool found = false;
                for (size_t n = 0; n < live->root_reference_count; ++n)
                    if (root_reference_equal(&live->root_references[n], &r->root_reference)) found = true;
                ok = found;
            }
        }
        for (const resource_origin *prior = vfs->origins; ok && (io->direction == QA_SOURCE_SAVE_READ) && prior != row; prior = prior->next)
            if (prior->mount == row->mount && prior->resource == row->resource) ok = false;
        if (!ok) return vfs_save_fail(io, QA_ERROR_FORMAT, "Invalid retained resource origin");
        if (io->direction != QA_SOURCE_SAVE_READ) source = source->next;
    }
    return true;
}
static bool historical_reads(qa_source_save_io *io, qa_vfs *vfs)
{
    size_t count = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : qa_vfs_retained_read_count(vfs);
    if (!vfs_save_count(io, &count, 64, sizeof(retained_read))) return false;
    const retained_read *source = vfs->history;
    retained_read **tail = &vfs->history;
    for (size_t i = 0; i < count; ++i) {
        while (io->direction != QA_SOURCE_SAVE_READ && source && !qa_resource_pool_find(vfs->pool, source->resource)) source = source->next;
        retained_read copy = source ? *source : (retained_read){0};
        retained_read *row = (io->direction == QA_SOURCE_SAVE_READ) ? calloc(1, sizeof(*row)) : &copy;
        if (!row) return vfs_save_fail(io, QA_ERROR_MEMORY, "Restoring retained first opening");
        if (io->direction == QA_SOURCE_SAVE_READ) { *tail = row; tail = &row->next; }
        qa_vfs_read_reference *r = &row->recipe;
        char *path = (char *)r->path, *lookup = (char *)r->lookup_path;
        char *from = (char *)r->link_source, *to = (char *)r->link_target;
        bool ok = qa_source_save_u64(io, &row->resource) && row->resource &&
            qa_source_save_u64(io, &r->mount) && qa_vfs_mount_id_was_issued(vfs, r->mount) &&
            vfs_save_text(io, &path) && normalized_prefix(io, path, false, false) &&
            vfs_save_text(io, &lookup) && normalized_prefix(io, lookup, false, false) &&
            vfs_save_text(io, &from) && normalized_prefix(io, from, true, true) &&
            vfs_save_text(io, &to) && normalized_prefix(io, to, true, true);
        if (io->direction == QA_SOURCE_SAVE_READ) { r->path = path; r->lookup_path = lookup; r->link_source = from; r->link_target = to; }
        const qa_resource *resource = ok ? qa_resource_pool_find(vfs->pool, row->resource) : NULL;
        qa_vfs_resource_origin origin;
        ok = ok && resource && qa_vfs_resource_origin_read(vfs, r->mount, resource, &origin);
        if (ok && !*from) ok = !*to && !strcmp(path, lookup);
        else if (ok) {
            size_t start = strlen(from), target = strlen(to), length = strlen(path);
            ok = !origin.archive && length >= start && !strncmp(path, from, start) &&
                target <= SIZE_MAX - (length - start) - 1;
            if (ok) {
                char *joined = malloc(target + length - start + 1);
                if (!joined) return vfs_save_fail(io, QA_ERROR_MEMORY, "Validating historical lookup recipe");
                memcpy(joined, to, target); memcpy(joined + target, path + start, length - start + 1);
                char *normalized = qa_vfs_normalize_path(joined, io->error); free(joined);
                ok = normalized && !strcmp(normalized, lookup); free(normalized);
            }
        }
        ok = ok && opening_fields(io, vfs, &r->opening, r->mount, path, *from != 0);
        for (const retained_read *prior = vfs->history; ok && (io->direction == QA_SOURCE_SAVE_READ) && prior != row; prior = prior->next)
            if (prior->resource == row->resource && prior->recipe.mount == r->mount &&
                !strcmp(prior->recipe.path, path) && !strcmp(prior->recipe.lookup_path, lookup) &&
                !strcmp(prior->recipe.link_source, from) && !strcmp(prior->recipe.link_target, to)) ok = false;
        if (!ok) return vfs_save_fail(io, QA_ERROR_FORMAT, "Invalid retained first-opening recipe");
        if (io->direction != QA_SOURCE_SAVE_READ) source = source->next;
    }
    return true;
}
static bool view_fields(qa_source_save_io *io, qa_vfs *vfs, mount_binding **bindings)
{
    bool success = vfs_save_magic(io, view_magic) && qa_source_save_u64(io, &vfs->next_mount) &&
        qa_source_save_u64(io, &vfs->next_temporary) && mounts(io, vfs, bindings) &&
        prefixes(io, vfs) && links(io, vfs) && restrictions(io, vfs) && reads(io, vfs) &&
        origins(io, vfs) && historical_reads(io, vfs);
    if (!success && io->error && io->error->code == QA_OK)
        vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS view continuation");
    return success;
}
static void bindings_free(mount_binding *bindings, size_t count)
{
    if (bindings) for (size_t i = 0; i < count; ++i) free(bindings[i].root_path);
    free(bindings);
}
static bool archive_ready(qa_source_save_io *io, const mount *m)
{
    bool unchanged = false;
    if (!qa_fs_file_path_unchanged(m->archive_file, &m->identity, &unchanged, io->error)) return false;
    return unchanged || vfs_save_fail(io, QA_ERROR_IO, "VFS mounted archive changed");
}
static bool native_bind(qa_source_save_io *io, qa_vfs *vfs, const mount_binding *bindings,
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
            if (qa_fs_identity_size(&m->identity) != qa_fs_identity_size(&m->archive->identity))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS mounted archive size differs");
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
            qa_fs_object_reference admitted;
            if (!qa_fs_root_reference_read(m->root, &admitted) ||
                (!mapped && !root_reference_equal(&admitted, &bindings[i].root_reference)))
                return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS retained native root reference differs");
            if (!vfs_root_reference_add(m, &admitted, io->error)) return false;
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
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) && view_fields(&io, &saved, &bindings);
    bindings_free(bindings, vfs->count);
    success = success && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return success;
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
    qa_source_save_io io = {.direction = QA_SOURCE_SAVE_READ, .input = bytes, .error = error};
    bool success = view_fields(&io, candidate, &bindings) && qa_source_save_finish(&io, NULL) &&
        native_bind(&io, candidate, bindings, refs);
    for (size_t i = 0; success && i < candidate->read_count; ++i) {
        const qa_vfs_read_reference *read = candidate->reads + i;
        qa_vfs_acquisition receipt = {
            .mount = read->mount, .resource_id = qa_resource_id(read->resource),
            .path = (char *)read->path, .lookup_path = (char *)read->lookup_path,
            .link_source = (char *)read->link_source, .link_target = (char *)read->link_target,
            .opening = read->opening, .opening_present = true
        };
        success = vfs_read_valid(candidate, read, error) &&
            qa_vfs_acquisition_retained(candidate, &receipt, error);
    }
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
