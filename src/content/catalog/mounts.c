#include "internal.h"
#include <stdio.h>

static bool contains(const qa_mount_id *ids, size_t count, qa_mount_id id)
{
    for (size_t i = 0; i < count; ++i) if (ids[i] == id) return true;
    return false;
}

bool catalog_view(const qa_catalog *c, const qa_mount_id *ids, size_t count,
                   qa_vfs **out, qa_error *error)
{
    qa_vfs *view = qa_vfs_create(c->resources, error);
    if (!view) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_catalog_mount *source = catalog_mount(c, ids[i]);
        qa_mount_id mounted;
        if (!source) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unknown catalog mount"); goto fail; }
        if (source->format == QA_ARCHIVE_AUTO) {
            if (!qa_vfs_mount_directory(view, source->path, QA_ARCHIVE_CASE_INSENSITIVE,
                source->writable, &mounted, error)) goto fail;
        } else {
            if (!qa_vfs_mount_archive(view, source->path, source->format, QA_ARCHIVE_CASE_INSENSITIVE, &mounted, error)) goto fail;
            if (!qa_sha256_equal(source->digest, qa_vfs_archive_digest(view, mounted))) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "package changed since catalog discovery: %s", source->path); goto fail;
            }
        }
    }
    *out = view; return true;
fail:
    qa_vfs_destroy(view); return false;
}

bool qa_catalog_open(const qa_catalog *c, qa_product_id id, qa_vfs **out, qa_error *error)
{
    const qa_product *p = qa_catalog_product(c, id);
    if (!p || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unknown content product"); return false; }
    if (p->availability != QA_CONTENT_INSTALLED) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "%s requires %s", p->key,
                     p->requirement_count ? p->requirements[0] : "installed content"); return false;
    }
    return catalog_view(c, c->products[id - 1].mounts, c->products[id - 1].mount_count, out, error);
}

static bool append_product(const qa_catalog *c, qa_product_id id, qa_mount_id *ids,
                            size_t *count, qa_error *error)
{
    const qa_product *p = qa_catalog_product(c, id);
    if (!p || p->availability != QA_CONTENT_INSTALLED) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "selected content %s is unavailable", p ? p->key : "identity"); return false;
    }
    const catalog_product *entry = &c->products[id - 1];
    for (size_t i = 0; i < entry->mount_count; ++i)
        if (!contains(ids, *count, entry->mounts[i])) ids[(*count)++] = entry->mounts[i];
    return true;
}

bool qa_catalog_mount_plan(const qa_catalog *c, const qa_catalog_mount_selection *s,
                            qa_vfs **out, qa_error *error)
{
    if (!c || !s || !out || (s->additional_count && !s->additional)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid content mount selection"); return false;
    }
    qa_mount_id *ids = malloc((c->physical_count ? c->physical_count : 1) * sizeof(*ids));
    qa_mount_id *geometry = malloc((c->physical_count ? c->physical_count : 1) * sizeof(*geometry));
    if (!ids || !geometry) { free(ids); free(geometry); qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot prepare mount recipe"); return false; }
    bool ok = false; size_t count = 0, gc = 0;
    const qa_product *combat = qa_catalog_product(c, s->combat);
    bool rerelease_assets = combat && combat->family == QA_GAME_Q2 && combat->edition == QA_EDITION_RERELEASE;
    qa_product_id assets = rerelease_assets && !s->explicit_presentation ? s->combat : s->assets;
    if (!append_product(c, assets, ids, &count, error) || !append_product(c, s->geometry, ids, &count, error)) goto done;
    if (rerelease_assets && !append_product(c, s->combat, ids, &count, error)) goto done;
    for (size_t i = 0; i < s->additional_count; ++i)
        if (!append_product(c, s->additional[i], ids, &count, error)) goto done;
    qa_vfs *view;
    if (!catalog_view(c, ids, count, &view, error)) goto done;
    if (assets != s->geometry) {
        if (!append_product(c, s->geometry, geometry, &gc, error)) { qa_vfs_destroy(view); goto done; }
        for (size_t i = 0; i < count; ++i) if (!contains(geometry, gc, ids[i])) geometry[gc++] = ids[i];
        for (size_t i = 0; i < gc; ++i) {
            for (size_t j = 0; j < count; ++j) if (geometry[i] == ids[j]) {
                qa_vfs_mount_info info;
                qa_vfs_mount_at(view, j, &info); geometry[i] = info.id; break;
            }
        }
        if (!qa_vfs_set_prefix_order(view, "maps", geometry, gc, error)) { qa_vfs_destroy(view); goto done; }
    }
    *out = view; ok = true;
done:
    free(ids); free(geometry); return ok;
}

bool qa_catalog_remote(const qa_catalog *c, qa_product_id base_id, const char *directory,
                        qa_product_id *out, qa_error *error)
{
    const qa_product *base = qa_catalog_product(c, base_id);
    if (!base || !directory || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid remote content selection"); return false; }
    if (base->edition == QA_EDITION_QUAKEWORLD && (!*directory || catalog_ascii_equal(directory, "id1") || catalog_ascii_equal(directory, "qw"))) { *out = base_id; return true; }
    if (!*directory && base->family != QA_GAME_Q1) { *out = base_id; return true; }
    if (!catalog_safe_name(directory)) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "remote game directory must be one safe name"); return false; }
    const char *slash = strrchr(base->directory, '/');
    size_t parent = slash ? (size_t)(slash - base->directory + 1) : 0;
    for (size_t i = 0; i < c->product_count; ++i) {
        const qa_product *p = &c->products[i].view;
        if (p->family != base->family || p->edition != base->edition || strlen(p->directory) <= parent ||
            memcmp(p->directory, base->directory, parent) || !catalog_ascii_equal(p->directory + parent, directory)) continue;
        const qa_product *ancestor = p;
        for (size_t j = 0; ancestor && j <= c->product_count; ++j) {
            if (ancestor->id == base_id) { *out = p->id; return true; }
            ancestor = qa_catalog_product(c, ancestor->base);
        }
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "remote content directory %s is not in the installed catalog", directory);
    return false;
}
