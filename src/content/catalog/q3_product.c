#include "internal.h"

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

bool qa_catalog_q3_identification_open(const qa_catalog *catalog,
    qa_product_id selected, qa_vfs **out, qa_error *error)
{
    const qa_product *product = qa_catalog_product(catalog, selected);
    if (!product || product->family != QA_GAME_Q3 || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Product identification requires actual Q3 catalog content");
    const catalog_product *entry = &catalog->products[selected - 1];
    return catalog_view(catalog, entry->mounts, entry->mount_count, out, error);
}

bool qa_catalog_q3_restricted(const qa_catalog *catalog)
{
    return catalog && catalog->q3_demo_restricted;
}

static bool copy_array(void **out, const void *source, size_t count,
    size_t size, qa_error *error)
{
    if (!count) { *out = NULL; return true; }
    if (count > SIZE_MAX / size) return fail(error, QA_ERROR_MEMORY, "Content catalog table is too large");
    void *copy = source ? malloc(count * size) : calloc(count, size);
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Copying content catalog table");
    if (source) memcpy(copy, source, count * size);
    *out = copy;
    return true;
}


static bool copy_text(qa_catalog *catalog, const char **value, qa_error *error)
{
    if (!*value) return true;
    *value = catalog_string(catalog, *value, error);
    return *value != NULL;
}

static bool copy_text_array(qa_catalog *catalog, const char *const *source,
    const char *const **out, size_t count, qa_error *error)
{
    const char **copy = NULL;
    if (!copy_array((void **)&copy, source, count, sizeof(*copy), error)) return false;
    *out = copy;
    for (size_t i = 0; i < count; ++i)
        if (!copy_text(catalog, &copy[i], error)) return false;
    return true;
}

static bool copy_product(qa_catalog *catalog, const catalog_product *source,
    catalog_product *product, qa_error *error)
{
    *product = *source;
    product->view.requirements = NULL;
    product->own_mounts = NULL; product->mounts = NULL;
    product->maps = NULL; product->starts = NULL;
#define TEXT(object, name) do { if (!copy_text(catalog, &(object)->name, error)) return false; } while (0)
    qa_product *view = &product->view;
    TEXT(view, key); TEXT(view, identity); TEXT(view, title); TEXT(view, campaign);
    TEXT(view, directory); TEXT(view, program);
    TEXT(product, installed_directory); TEXT(product, witness);
    for (size_t i = 0; i < sizeof(product->required) / sizeof(*product->required); ++i)
        if (!copy_text(catalog, &product->required[i], error)) return false;
    TEXT((&product->episode), id); TEXT((&product->episode), command);
    TEXT((&product->episode), name); TEXT((&product->episode), activity);
    if (!copy_text_array(catalog, source->view.requirements, &view->requirements,
            view->requirement_count, error) ||
        !copy_array((void **)&product->own_mounts, source->own_mounts,
            product->own_count, sizeof(*product->own_mounts), error) ||
        !copy_array((void **)&product->mounts, source->mounts,
            product->mount_count, sizeof(*product->mounts), error) ||
        !copy_array((void **)&product->maps, source->maps,
            product->map_count, sizeof(*product->maps), error) ||
        !copy_array((void **)&product->starts, source->starts,
            product->start_count, sizeof(*product->starts), error)) return false;
    for (size_t i = 0; i < product->map_count; ++i)
        TEXT((&product->maps[i]), path);
    for (size_t i = 0; i < product->start_count; ++i) {
        qa_catalog_start *start = &product->starts[i];
        TEXT(start, episode); TEXT(start, bsp); TEXT(start, path);
        TEXT(start, title); TEXT(start, start_items);
    }
#undef TEXT
    return true;
}

static bool copy_mod(qa_catalog *catalog, const qa_catalog_mod *source,
    qa_catalog_mod *mod, qa_error *error)
{
    *mod = *source;
    mod->requires = NULL; mod->conflicts = NULL;
    qa_resource_retain((qa_resource *)mod->declaration_resource);
    qa_resource_retain((qa_resource *)mod->program_resource);
#define TEXT(name) do { if (!copy_text(catalog, &mod->name, error)) return false; } while (0)
    TEXT(key); TEXT(id); TEXT(title); TEXT(declaration_path);
    TEXT(program_path); TEXT(unavailable);
#undef TEXT
    return copy_text_array(catalog, source->requires, &mod->requires,
            mod->requires_count, error) &&
        copy_text_array(catalog, source->conflicts, &mod->conflicts,
            mod->conflicts_count, error);
}

static bool copy_behavior(qa_catalog *catalog, const qa_catalog_weapon_behavior *source,
    qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    *behavior = *source; behavior->entry.data = NULL;
    qa_resource_retain((qa_resource *)behavior->declaration_resource);
    qa_resource_retain((qa_resource *)behavior->artifact_resource);
#define TEXT(name) do { if (!copy_text(catalog, &behavior->name, error)) return false; } while (0)
    TEXT(id); TEXT(title); TEXT(artifact_path); TEXT(declaration_path); TEXT(unavailable);
#undef TEXT
    return copy_array((void **)&behavior->entry.data, source->entry.data,
        behavior->entry.size, 1, error);
}

bool qa_catalog_clone(const qa_catalog *source, qa_catalog **out, qa_error *error)
{
    if (!source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog clone requires an actual retained snapshot");
    qa_catalog *copy = calloc(1, sizeof(*copy));
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Copying content catalog");
    copy->references = 1;
    copy->generation = source->generation;
    copy->q3_demo_restricted = source->q3_demo_restricted;
    copy->resources = source->resources;
    copy->q3_install_mount = source->q3_install_mount;
    copy->q3_download_mount = source->q3_download_mount;
    copy->corpus_mount = source->corpus_mount;
    memcpy(copy->q2_download_mount, source->q2_download_mount, sizeof(copy->q2_download_mount));
    if (!qa_strings_create(&copy->strings, error)) goto fail;
    copy->mounts = qa_vfs_clone(source->mounts, error);
    if (!copy->mounts) goto fail;
    copy->root = source->root; copy->user = source->user;
    if (!copy_text(copy, &copy->root, error) || !copy_text(copy, &copy->user, error) ||
        !copy_text_array(copy, source->install_roots, (const char *const **)&copy->install_roots,
            source->install_root_count, error)) goto fail;
    copy->install_root_count = source->install_root_count;
    if (!copy_array((void **)&copy->locations, source->locations,
            source->location_count, sizeof(*copy->locations), error)) goto fail;
    copy->location_count = copy->location_capacity = source->location_count;
    for (size_t i = 0; i < copy->location_count; ++i)
        if (!copy_text(copy, &copy->locations[i].logical, error) ||
            !copy_text(copy, &copy->locations[i].path, error)) goto fail;
    if (!copy_array((void **)&copy->physical, NULL, source->physical_count,
            sizeof(*copy->physical), error)) goto fail;
    copy->physical_count = copy->physical_capacity = source->physical_count;
    for (size_t i = 0; i < copy->physical_count; ++i) {
        catalog_physical *physical = &copy->physical[i];
        *physical = source->physical[i]; physical->members = NULL;
        physical->view.identity = qa_vfs_archive_identity(copy->mounts, physical->view.id);
        if (!copy_text(copy, &physical->view.path, error) ||
            !copy_array((void **)&physical->members, source->physical[i].members,
                physical->member_count, sizeof(*physical->members), error)) goto fail;
        for (size_t j = 0; j < physical->member_count; ++j)
            if (!copy_text(copy, &physical->members[j].path, error)) goto fail;
    }
    if (!copy_array((void **)&copy->products, NULL, source->product_count,
            sizeof(*copy->products), error)) goto fail;
    copy->product_count = copy->product_capacity = source->product_count;
    for (size_t i = 0; i < copy->product_count; ++i)
        if (!copy_product(copy, &source->products[i], &copy->products[i], error) ||
            !catalog_bind_product_names(copy, &copy->products[i], error)) goto fail;
    if (!copy_array((void **)&copy->mods, NULL, source->mod_count,
            sizeof(*copy->mods), error)) goto fail;
    copy->mod_count = copy->mod_capacity = source->mod_count;
    for (size_t i = 0; i < copy->mod_count; ++i)
        if (!copy_mod(copy, &source->mods[i], &copy->mods[i], error)) goto fail;
    if (!copy_array((void **)&copy->behaviors, NULL, source->behavior_count,
            sizeof(*copy->behaviors), error)) goto fail;
    copy->behavior_count = copy->behavior_capacity = source->behavior_count;
    for (size_t i = 0; i < copy->behavior_count; ++i)
        if (!copy_behavior(copy, &source->behaviors[i], &copy->behaviors[i], error)) goto fail;
    *out = copy;
    return true;
fail:
    qa_catalog_release(copy);
    return false;
}

bool catalog_q3_restriction_valid(const qa_catalog *catalog, qa_error *error)
{
    if (!catalog->q3_demo_restricted) return true;
    const qa_product *demo = qa_catalog_find(catalog, "q3-demota");
    if (!demo || demo->family != QA_GAME_Q3 || demo->base ||
        demo->availability != QA_CONTENT_INSTALLED || strcmp(demo->directory, "q3a/demota"))
        return fail(error, QA_ERROR_FORMAT, "Restricted catalog lost its genuine demota product");
    const catalog_product *media = &catalog->products[demo->id - 1];
    bool has_package = false;
    for (size_t i = 0; i < media->mount_count; ++i) {
        const qa_catalog_mount *physical = catalog_mount(catalog, media->mounts[i]);
        if (!physical) return fail(error, QA_ERROR_FORMAT, "Restricted catalog lost its demota mount");
        const qa_archive *archive = qa_vfs_archive(catalog->mounts, physical->id);
        if (physical->format != QA_ARCHIVE_AUTO) {
            uint32_t checksum;
            if (!archive || qa_archive_get_kind(archive) != QA_ARCHIVE_PK3 ||
                !qa_vfs_archive_checksums(catalog->mounts, physical->id, 0, &checksum, NULL, error) ||
                checksum != UINT32_C(437558517))
                return fail(error, QA_ERROR_FORMAT, "Restricted catalog contains invalid Q3 demo media");
            has_package = true;
        }
    }
    if (!has_package) return fail(error, QA_ERROR_FORMAT, "Restricted catalog has no genuine demo PK3");
    for (size_t i = 0; i < catalog->product_count; ++i) {
        const catalog_product *product = &catalog->products[i];
        if (product->view.family != QA_GAME_Q3) continue;
        if (product->view.base || product->view.availability != QA_CONTENT_INSTALLED ||
            product->view.requirement_count || product->own_count != media->own_count ||
            product->mount_count != media->mount_count || product->map_count != media->map_count)
            return fail(error, QA_ERROR_FORMAT, "Restricted Q3 product differs from its actual demota media");
        for (size_t j = 0; j < media->own_count; ++j)
            if (product->own_mounts[j] != media->own_mounts[j])
                return fail(error, QA_ERROR_FORMAT, "Restricted Q3 own mount order differs");
        for (size_t j = 0; j < media->mount_count; ++j)
            if (product->mounts[j] != media->mounts[j])
                return fail(error, QA_ERROR_FORMAT, "Restricted Q3 mount order differs");
        for (size_t j = 0; j < media->map_count; ++j)
            if (!product->maps[j].archived || product->maps[j].mount != media->maps[j].mount ||
                product->maps[j].member != media->maps[j].member ||
                strcmp(product->maps[j].path, media->maps[j].path))
                return fail(error, QA_ERROR_FORMAT, "Restricted Q3 map is outside its genuine demo archive");
    }
    return true;
}

static void replacement_free(catalog_product *replacement, size_t count)
{
    if (replacement) for (size_t i = 0; i < count; ++i) {
        free(replacement[i].own_mounts);
        free(replacement[i].mounts);
        free(replacement[i].maps);
        free(replacement[i].starts);
    }
    free(replacement);
}


static bool admitted_map(const catalog_product *media, const char *path)
{
    for (size_t i = 0; i < media->map_count; ++i)
        if (media->maps[i].archived && catalog_ascii_equal(media->maps[i].path, path)) return true;
    return false;
}

bool qa_catalog_q3_restrict(qa_catalog *catalog, qa_error *error)
{
    if (!catalog) return fail(error, QA_ERROR_ARGUMENT, "Q3 restriction requires its actual catalog");
    if (catalog->q3_demo_restricted) return true;
    if (catalog->references != 1)
        return fail(error, QA_ERROR_ARGUMENT, "Resolve Q3 media before a launch draft retains the catalog");
    const qa_product *demo = qa_catalog_find(catalog, "q3-demota");
    if (!demo || demo->availability != QA_CONTENT_INSTALLED)
        return fail(error, QA_ERROR_NOT_FOUND, "Q3 restricted media requires installed q3-demota");
    const catalog_product *media = &catalog->products[demo->id - 1];
    if (demo->base || !media->mount_count)
        return fail(error, QA_ERROR_FORMAT, "Q3 demo media has an invalid inherited search path");
    qa_vfs *verified = NULL;
    if (!catalog_view(catalog, media->mounts, media->mount_count, &verified, error)) return false;
    bool okay = true, has_package = false;
    for (size_t i = 0; okay && i < qa_vfs_mount_count(verified); ++i) {
        qa_vfs_mount_info mount;
        okay = qa_vfs_mount_at(verified, i, &mount) &&
            qa_vfs_set_mount_q3_demo(verified, mount.id, true, error);
        if (okay && mount.is_archive) has_package = true;
    }
    qa_vfs_destroy(verified);
    if (!okay) return false;
    if (!has_package) return fail(error, QA_ERROR_NOT_FOUND, "Q3 demo media has no genuine PK3 package");

    /* Reindex the actual retained archive members under demo admission before
     * deduplication. A denied loose map must not hide an archive fallback. */
    catalog_product indexed = *media;
    indexed.maps = NULL;
    indexed.map_count = 0;
    if (!catalog_index_maps(catalog, &indexed, true, error)) {
        free(indexed.maps);
        return false;
    }
    media = &indexed;

    catalog_product *replacement = calloc(catalog->product_count, sizeof(*replacement));
    if (!replacement) {
        free(indexed.maps);
        return fail(error, QA_ERROR_MEMORY, "Preparing restricted Q3 catalog media");
    }
    for (size_t i = 0; i < catalog->product_count; ++i) {
        if (catalog->products[i].view.family != QA_GAME_Q3) continue;
        catalog_product *next = &replacement[i];
        if (!copy_array((void **)&next->own_mounts, media->own_mounts,
                media->own_count, sizeof(*next->own_mounts), error) ||
            !copy_array((void **)&next->mounts, media->mounts,
                media->mount_count, sizeof(*next->mounts), error) ||
            !copy_array((void **)&next->maps, media->maps,
                media->map_count, sizeof(*next->maps), error) ||
            !copy_array((void **)&next->starts, media->starts,
                media->start_count, sizeof(*next->starts), error)) {
            replacement_free(replacement, catalog->product_count);
            free(indexed.maps);
            return false;
        }
        next->own_count = media->own_count;
        next->mount_count = media->mount_count;
        for (size_t j = 0; j < media->map_count; ++j)
            if (media->maps[j].archived) next->maps[next->map_count++] = media->maps[j];
        for (size_t j = 0; j < media->start_count; ++j)
            if (admitted_map(media, media->starts[j].path)) next->starts[next->start_count++] = media->starts[j];
        next->episode = media->episode;
        next->has_episode = media->has_episode;
        next->witness = media->witness;
        next->required_count = media->required_count;
        memcpy(next->required, media->required, sizeof(next->required));
    }
    /* Every allocation and genuine package admission precedes this no-fail
     * swap. Product identities and native/source gameplay selections persist. */
    for (size_t i = 0; i < catalog->product_count; ++i) {
        catalog_product *product = &catalog->products[i];
        if (product->view.family != QA_GAME_Q3) continue;
        qa_product view = product->view;
        replacement[i].configuration_base = product->configuration_base;
        replacement[i].write_mount = product->write_mount;
        replacement[i].loose_mount = product->loose_mount;
        view.base = QA_PRODUCT_NONE;
        view.availability = QA_CONTENT_INSTALLED;
        view.requirements = NULL;
        view.requirement_count = 0;
        free((void *)product->view.requirements);
        free(product->own_mounts);
        free(product->mounts);
        free(product->maps);
        free(product->starts);
        *product = replacement[i];
        product->view = view;
        replacement[i] = (catalog_product){0};
    }
    free(replacement);
    free(indexed.maps);
    catalog->q3_demo_restricted = true;
    return true;
}
