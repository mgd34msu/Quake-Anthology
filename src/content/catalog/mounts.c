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
        qa_vfs_mount_info retained = {0}; bool found = false;
        for (size_t j = 0; j < qa_vfs_mount_count(c->mounts); ++j)
            if (qa_vfs_mount_at(c->mounts, j, &retained) && retained.id == ids[i]) { found = true; break; }
        const char *path = found ? qa_vfs_mount_path(c->mounts, ids[i]) : NULL;
        if (!path || strcmp(path, source->path) || retained.format != source->format ||
            retained.is_archive != (source->format != QA_ARCHIVE_AUTO) ||
            retained.writable != source->writable ||
            (retained.is_archive && source->identity && (!retained.identity ||
                !qa_fs_identity_equal(retained.identity, source->identity)))) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "catalog mount disagrees with retained authority: %s", source->path);
            goto fail;
        }
        if (!qa_vfs_mount_retained(view, c->mounts, ids[i], QA_ARCHIVE_CASE_INSENSITIVE,
            source->writable, &mounted, error)) goto fail;
        if (c->q3_demo_restricted) {
            const qa_product *demo = qa_catalog_find(c, "q3-demota");
            const catalog_product *media = demo ? &c->products[demo->id - 1] : NULL;
            if (media && contains(media->mounts, media->mount_count, ids[i]) &&
                !qa_vfs_set_mount_q3_demo(view, mounted, true, error)) goto fail;
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

bool qa_catalog_product_view_current(const qa_catalog *c, qa_product_id id, const qa_vfs *view)
{
    const qa_product *product = qa_catalog_product(c, id);
    if (!product || product->availability != QA_CONTENT_INSTALLED) return false;
    const catalog_product *entry = &c->products[id - 1];
    return qa_vfs_retained_recipe_matches(view, c->mounts, entry->mounts, entry->mount_count,
        QA_ARCHIVE_CASE_INSENSITIVE, c->q3_demo_restricted && product->family == QA_GAME_Q3);
}

bool qa_catalog_product_mount_origin(const qa_catalog *c, qa_product_id selected,
    const qa_vfs *view, qa_mount_id mount, qa_product_id *content, qa_mount_id *physical)
{
    if (!content || !physical || !mount || !qa_catalog_product_view_current(c, selected, view)) return false;
    const catalog_product *entry = &c->products[selected - 1];
    qa_mount_id source = 0;
    for (size_t i = 0; i < entry->mount_count; ++i) {
        qa_vfs_mount_info actual;
        if (!qa_vfs_mount_at(view, i, &actual)) return false;
        if (actual.id == mount) { source = entry->mounts[i]; break; }
    }
    if (!source) return false;
    for (size_t depth = 0; selected && depth < c->product_count; ++depth) {
        const qa_product *product = qa_catalog_product(c, selected);
        if (!product) return false;
        const catalog_product *owner = &c->products[selected - 1];
        if (contains(owner->own_mounts, owner->own_count, source)) {
            *content = selected; *physical = source; return true;
        }
        selected = product->base;
    }
    return false;
}

bool qa_catalog_product_acquisition_origin(const qa_catalog *c, qa_product_id selected,
    const qa_vfs *view, const qa_vfs_acquisition *receipt, qa_product_id *content,
    qa_mount_id *physical, qa_error *error)
{
    if (!receipt || !receipt->opening_present || !receipt->link_source || *receipt->link_source ||
        !receipt->link_target || *receipt->link_target || receipt->opening.prefix ||
        receipt->opening.user_overlay || receipt->opening.rank < 0 ||
        receipt->opening.order_count != qa_vfs_mount_count(view) ||
        (uint64_t)receipt->opening.rank >= receipt->opening.order_count || !receipt->opening.order ||
        receipt->opening.order[receipt->opening.rank] != receipt->mount ||
        !qa_catalog_product_mount_origin(c, selected, view, receipt->mount, content, physical) ||
        !qa_vfs_acquisition_retained(view, receipt, error)) return false;
    for (size_t i = 0; i < receipt->opening.order_count; ++i) {
        qa_vfs_mount_info actual;
        if (!qa_vfs_mount_at(view, i, &actual) || receipt->opening.order[i] != actual.id) return false;
    }
    return true;
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
    if (!(base->family == QA_GAME_Q3 ? catalog_remote_name(directory) : catalog_safe_name(directory))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "remote game directory must be one safe name"); return false;
    }
    const char *slash = strrchr(base->directory, '/');
    size_t parent = slash ? (size_t)(slash - base->directory + 1) : 0;
    for (size_t i = 0; i < c->product_count; ++i) {
        const qa_product *p = &c->products[i].view;
        if (p->family != base->family || p->edition != base->edition || strlen(p->directory) <= parent ||
            memcmp(p->directory, base->directory, parent) || !catalog_ascii_equal(p->directory + parent, directory)) continue;
        const qa_product *ancestor = p;
        for (size_t j = 0; ancestor && j <= c->product_count; ++j) {
            if (ancestor->id == base_id) { *out = p->id; return true; }
            ancestor = qa_catalog_product(c, qa_catalog_configuration_base(c, ancestor->id));
        }
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "remote content directory %s is not in the installed catalog", directory);
    return false;
}
