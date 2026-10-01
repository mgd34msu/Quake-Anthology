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

/* The field codec owns every copied metadata table and string. The VFS clone
 * retains its genuine native handles and independently owns policy flags. No
 * serialized bytes enter this trusted in-process copy boundary. */
static bool clone_files_encode(void *context, const qa_vfs *files,
    qa_buffer *out, qa_error *error)
{
    (void)context; (void)files; (void)out; (void)error;
    return true;
}
static bool clone_files_decode(void *context, qa_resource_pool *resources,
    qa_bytes bytes, qa_vfs **out, qa_error *error)
{
    const qa_catalog *source = context;
    if (bytes.size || resources != source->resources)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog clone changed its retained mount authority");
    *out = qa_vfs_clone(source->mounts, error);
    return *out != NULL;
}
static bool clone_physical_ready(void *context, const qa_catalog_mount *mount,
    const qa_catalog_member_identity *members, size_t count, qa_error *error)
{
    const catalog_physical *source = catalog_package(context, mount->id);
    if (!source || strcmp(source->view.path, mount->path) || source->view.format != mount->format ||
        source->view.writable != mount->writable || source->member_count != count ||
        (mount->digest && (!source->view.digest || !qa_sha256_equal(source->view.digest, mount->digest))))
        return fail(error, QA_ERROR_FORMAT, "Catalog clone changed physical package identity");
    for (size_t i = 0; i < count; ++i)
        if (source->members[i].ordinal != members[i].ordinal || strcmp(source->members[i].path, members[i].path))
            return fail(error, QA_ERROR_FORMAT, "Catalog clone changed retained archive inventory");
    return true;
}
bool qa_catalog_clone(const qa_catalog *source, qa_catalog **out, qa_error *error)
{
    if (!source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog clone requires an actual retained snapshot");
    qa_catalog_checkpoint_refs refs = {.context = (void *)source,
        .files_encode = clone_files_encode, .files_decode = clone_files_decode,
        .physical_ready = clone_physical_ready};
    qa_buffer bytes = {0};
    bool okay = qa_catalog_checkpoint(source, &refs, &bytes, error) &&
        qa_catalog_restore(source->resources, &refs, (qa_bytes){bytes.data, bytes.size}, out, error);
    qa_buffer_free(&bytes);
    return okay;
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

static bool copy_array(void **out, const void *source, size_t count,
    size_t size, qa_error *error)
{
    if (!count) { *out = NULL; return true; }
    if (count > SIZE_MAX / size) return fail(error, QA_ERROR_MEMORY, "Q3 demo media table is too large");
    void *copy = malloc(count * size);
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Retaining genuine Q3 demo media table");
    memcpy(copy, source, count * size);
    *out = copy;
    return true;
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
