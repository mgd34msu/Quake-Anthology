#include "internal.h"
#include "qa/source_save.h"
#include "qa/save.h"

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
#define ENUM(object, name, maximum) do { uint32_t value = (object)->name; \
    if (!qa_source_save_u32(io, &value) || value > (maximum)) return false; \
    if (io->direction == QA_SOURCE_SAVE_READ) (object)->name = value; } while (0)
#define ARRAY(object, name, count, minimum) do { size_t total = (object)->count; \
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / (minimum) : SIZE_MAX; \
    if (maximum > SIZE_MAX / sizeof(*(object)->name)) maximum = SIZE_MAX / sizeof(*(object)->name); \
    if (!qa_source_save_count(io, &total, maximum)) return false; \
    if (io->direction == QA_SOURCE_SAVE_READ) { \
        (object)->name = total ? calloc(total, sizeof(*(object)->name)) : NULL; \
        if (total && !(object)->name) return fail(io->error, QA_ERROR_MEMORY, "Allocating retained catalog table"); \
        (object)->count = total; \
    } } while (0)
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static bool blob(qa_source_save_io *io, qa_buffer *bytes)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &bytes->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && bytes->size) {
        bytes->data = malloc(bytes->size);
        if (!bytes->data) return fail(io->error, QA_ERROR_MEMORY, "Allocating retained catalog bytes");
    }
    return qa_source_save_bytes(io, bytes->data, bytes->size);
}
static bool text(qa_source_save_io *io, qa_catalog *catalog, const char **value)
{
    uint8_t kind = 0; qa_string_id id = QA_STRING_NONE; size_t length = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE && *value) {
        length = strlen(*value); id = qa_strings_find(catalog->strings, (qa_bytes){(const uint8_t *)*value, length});
        kind = id ? 1 : 2;
    }
    if (!qa_source_save_u8(io, &kind) || kind > 2) return false;
    if (!kind) { if (io->direction == QA_SOURCE_SAVE_READ) *value = NULL; return true; }
    if (kind == 1) {
        if (!qa_source_save_u32(io, &id) || !id) return false;
        const char *actual = qa_strings_cstr(catalog->strings, id);
        if (!actual) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) *value = actual;
        return true;
    }
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &length, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)*value, length);
    if (length > io->input.size - io->offset) return false;
    qa_bytes bytes = {io->input.data + io->offset, length};
    if (memchr(bytes.data, 0, bytes.size)) return fail(io->error, QA_ERROR_FORMAT, "Retained catalog text contains NUL");
    io->offset += length;
    if (!qa_strings_intern(catalog->restored_literals, bytes, &id, io->error)) return false;
    *value = qa_strings_cstr(catalog->restored_literals, id); return *value != NULL;
}
static bool strings(qa_source_save_io *io, qa_catalog *catalog, const char ***values, size_t *count)
{
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) : SIZE_MAX;
    if (maximum > SIZE_MAX / sizeof(**values)) maximum = SIZE_MAX / sizeof(**values);
    size_t total = *count;
    if (!qa_source_save_count(io, &total, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        const char **owned = total ? calloc(total, sizeof(*owned)) : NULL;
        if (total && !owned) return fail(io->error, QA_ERROR_MEMORY, "Allocating retained catalog string references");
        *values = owned; *count = total;
    }
    for (size_t i = 0; i < *count; ++i)
        if (!text(io, catalog, &(*values)[i]) || !(*values)[i]) return false;
    return true;
}
static bool mount_ids(qa_source_save_io *io, qa_catalog *catalog, qa_mount_id **values, size_t *count)
{
    size_t maximum = catalog->physical_count;
    size_t total = *count;
    if (!qa_source_save_count(io, &total, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *values = total ? calloc(total, sizeof(**values)) : NULL;
        if (total && !*values) return fail(io->error, QA_ERROR_MEMORY, "Allocating retained catalog mount plan");
        *count = total;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (!qa_source_save_u64(io, &(*values)[i]) || !(*values)[i] || (*values)[i] > catalog->physical_count) return false;
        for (size_t j = 0; j < i; ++j) if ((*values)[i] == (*values)[j]) return false;
    }
    return true;
}
static bool physical(qa_source_save_io *io, qa_catalog *catalog)
{
    ARRAY(catalog, physical, physical_count, 47);
    for (size_t i = 0; i < catalog->physical_count; ++i) {
        catalog_physical *package = &catalog->physical[i]; qa_catalog_mount *view = &package->view;
        FIELD(u64, view, id);
        if (view->id != i + 1 || !text(io, catalog, &view->path) || !view->path || !*view->path) return false;
        ENUM(view, format, QA_ARCHIVE_KPF); FIELD(bool, view, writable);
        if (!qa_source_save_bytes(io, &package->digest, sizeof(package->digest))) return false;
        ARRAY(package, members, member_count, 9);
        for (size_t j = 0; j < package->member_count; ++j) {
            if (!text(io, catalog, &package->members[j].path) || !package->members[j].path || !*package->members[j].path ||
                !qa_source_save_count(io, &package->members[j].ordinal, SIZE_MAX) ||
                (j && package->members[j - 1].ordinal >= package->members[j].ordinal)) return false;
        }
        if (view->format == QA_ARCHIVE_AUTO && package->member_count) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) view->digest = view->format == QA_ARCHIVE_AUTO ? NULL : &package->digest;
        else if ((view->format == QA_ARCHIVE_AUTO && view->digest) ||
            (view->format != QA_ARCHIVE_AUTO && view->digest != &package->digest)) return false;
    }
    return true;
}
static bool products(qa_source_save_io *io, qa_catalog *catalog, uint32_t schema)
{
    ARRAY(catalog, products, product_count, 78);
    if (catalog->product_count > UINT32_MAX) return false;
    for (size_t i = 0; i < catalog->product_count; ++i) {
        catalog_product *product = &catalog->products[i]; qa_product *view = &product->view;
        FIELD(u32, view, id); FIELD(u32, view, base);
        if (schema >= 4) {
            FIELD(u32, product, configuration_base); FIELD(u64, product, write_mount);
        } else if (io->direction == QA_SOURCE_SAVE_READ) product->configuration_base = view->base;
        if (product->configuration_base >= view->id || product->write_mount > catalog->physical_count) return false;
        if (view->id != i + 1 || view->base >= view->id ||
            !text(io, catalog, &view->key) || !view->key || !*view->key ||
            !text(io, catalog, &view->identity) || !view->identity || !*view->identity ||
            !text(io, catalog, &view->title) ||
            !text(io, catalog, &view->campaign) || !view->campaign ||
            !text(io, catalog, &view->directory) || !view->directory) return false;
        ENUM(view, family, QA_GAME_Q3); ENUM(view, edition, QA_EDITION_DEMO);
        ENUM(view, availability, QA_CONTENT_INVALID); FIELD(bool, view, builtin);
        if (!view->title && view->availability != QA_CONTENT_INVALID) return false;
        if (!strings(io, catalog, (const char ***)&view->requirements, &view->requirement_count) ||
            !text(io, catalog, &view->program)) return false;
        ENUM(view, program_kind, QA_PROGRAM_NATIVE); FIELD(u32, view, program_product);
        if (view->program_product > catalog->product_count ||
            !mount_ids(io, catalog, &product->own_mounts, &product->own_count) ||
            !mount_ids(io, catalog, &product->mounts, &product->mount_count) ||
            !text(io, catalog, &product->witness) ||
            !qa_source_save_count(io, &product->required_count, 4)) return false;
        for (size_t j = 0; j < 4; ++j)
            if (!text(io, catalog, &product->required[j]) || (j < product->required_count && !product->required[j])) return false;
        ARRAY(product, maps, map_count, 18);
        for (size_t j = 0; j < product->map_count; ++j) {
            qa_catalog_map *map = &product->maps[j];
            if (!text(io, catalog, &map->path) || !map->path || !*map->path) return false;
            FIELD(u64, map, mount);
            if (!map->mount || map->mount > catalog->physical_count || !qa_source_save_count(io, &map->member, SIZE_MAX)) return false;
            FIELD(bool, map, archived);
            const catalog_physical *package = &catalog->physical[map->mount - 1];
            if (map->archived != (package->view.format != QA_ARCHIVE_AUTO)) return false;
            bool admitted = false;
            for (size_t k = 0; k < product->mount_count; ++k) admitted |= product->mounts[k] == map->mount;
            if (!admitted) return false;
            if (map->archived) {
                admitted = false;
                for (size_t k = 0; k < package->member_count; ++k)
                    admitted |= package->members[k].ordinal == map->member && !strcmp(package->members[k].path, map->path);
                if (!admitted) return false;
            }
        }
        ARRAY(product, starts, start_count, 8);
        for (size_t j = 0; j < product->start_count; ++j) {
            qa_catalog_start *start = &product->starts[j];
            if (!text(io, catalog, &start->episode) || !start->episode || !*start->episode ||
                !text(io, catalog, &start->bsp) || !start->bsp || !*start->bsp ||
                !text(io, catalog, &start->path) || !start->path || !*start->path ||
                !text(io, catalog, &start->title) || !start->title ||
                !text(io, catalog, &start->start_items) || !start->start_items) return false;
            FIELD(bool, start, singleplayer); FIELD(bool, start, cooperative); FIELD(bool, start, capture_the_flag);
        }
        FIELD(bool, product, has_episode);
        qa_catalog_episode *episode = &product->episode;
        if (!text(io, catalog, &episode->id) || !text(io, catalog, &episode->command) ||
            !text(io, catalog, &episode->name) || !text(io, catalog, &episode->activity)) return false;
        FIELD(bool, episode, needs_skill_select);
        if (product->has_episode && (!episode->id || !episode->command || !episode->name || !episode->activity)) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(catalog->products[j].view.key, view->key)) return false;
        if (schema < 4 && io->direction == QA_SOURCE_SAVE_READ && !catalog->q3_demo_restricted)
            for (size_t j = 0; j < product->own_count; ++j) {
                const qa_catalog_mount *mount = catalog_mount(catalog, product->own_mounts[j]);
                if (mount->format == QA_ARCHIVE_AUTO && mount->writable && !product->write_mount)
                    product->write_mount = mount->id;
            }
    }
    for (size_t i = 0; i < catalog->product_count; ++i) {
        const catalog_product *product = &catalog->products[i];
        const qa_product *configuration_base = qa_catalog_product(catalog, product->configuration_base);
        if (configuration_base && configuration_base->family != product->view.family) return false;
        if ((!catalog->q3_demo_restricted || product->view.family != QA_GAME_Q3) &&
            product->configuration_base != product->view.base) return false;
        if (product->write_mount) {
            const qa_catalog_mount *write = catalog_mount(catalog, product->write_mount);
            if (!write || write->format != QA_ARCHIVE_AUTO || !write->writable) return false;
            if (catalog->q3_demo_restricted && product->view.family == QA_GAME_Q3) {
                const qa_catalog_mount *family = catalog_mount(catalog, catalog->q3_download_mount);
                const char *leaf = strrchr(product->view.directory, '/');
                size_t length = family ? strlen(family->path) : 0;
                if (!family || !leaf || strlen(write->path) <= length ||
                    memcmp(write->path, family->path, length) || write->path[length] != '/' ||
                    !catalog_ascii_equal(write->path + length + 1, leaf + 1)) return false;
            } else {
                bool own = false;
                for (size_t j = 0; j < product->own_count; ++j) own |= product->own_mounts[j] == product->write_mount;
                if (!own) return false;
            }
        }
        const catalog_product *base = product->view.base ? &catalog->products[product->view.base - 1] : NULL;
        size_t at = 0;
        for (size_t j = 0; j < product->own_count; ++j, ++at)
            if (at >= product->mount_count || product->mounts[at] != product->own_mounts[j]) return false;
        for (size_t j = 0; base && j < base->mount_count; ++j) {
            bool own = false;
            for (size_t k = 0; k < product->own_count; ++k) own |= product->own_mounts[k] == base->mounts[j];
            if (own) continue;
            if (at >= product->mount_count || product->mounts[at++] != base->mounts[j]) return false;
        }
        if (at != product->mount_count) return false;
    }
    return true;
}
static bool payload(qa_source_save_io *io, qa_bytes *value)
{
    qa_buffer bytes = {(uint8_t *)value->data, value->size};
    bool ok = blob(io, &bytes);
    if (io->direction == QA_SOURCE_SAVE_READ) *value = (qa_bytes){bytes.data, bytes.size};
    return ok;
}
static bool mods(qa_source_save_io *io, qa_catalog *catalog)
{
    ARRAY(catalog, mods, mod_count, 103);
    for (size_t i = 0; i < catalog->mod_count; ++i) {
        qa_catalog_mod *mod = &catalog->mods[i]; FIELD(u32, mod, product);
        if (!mod->product || mod->product > catalog->product_count ||
            !text(io, catalog, &mod->key) || !mod->key || !qa_catalog_mod_key(mod->key) ||
            !text(io, catalog, &mod->id) || !mod->id || !text(io, catalog, &mod->title) || !mod->title) return false;
        ENUM(mod, purpose, QA_MOD_GAME_TYPE); ENUM(mod, runtime, QA_PROGRAM_NATIVE);
        if (!strings(io, catalog, (const char ***)&mod->requires, &mod->requires_count) ||
            !strings(io, catalog, (const char ***)&mod->conflicts, &mod->conflicts_count) ||
            !text(io, catalog, &mod->declaration_path) || !text(io, catalog, &mod->program_path) ||
            !qa_source_save_bytes(io, &mod->declaration_digest, sizeof(mod->declaration_digest)) ||
            !qa_source_save_bytes(io, &mod->program_digest, sizeof(mod->program_digest)) ||
            !payload(io, &mod->declaration) || !text(io, catalog, &mod->unavailable)) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp(catalog->mods[j].key, mod->key)) return false;
    }
    return true;
}
static bool behaviors(qa_source_save_io *io, qa_catalog *catalog)
{
    ARRAY(catalog, behaviors, behavior_count, 92);
    for (size_t i = 0; i < catalog->behavior_count; ++i) {
        qa_catalog_weapon_behavior *behavior = &catalog->behaviors[i]; FIELD(u32, behavior, product);
        if (!behavior->product || behavior->product > catalog->product_count ||
            !text(io, catalog, &behavior->id) || !behavior->id || !*behavior->id ||
            !text(io, catalog, &behavior->title) || !behavior->title || !text(io, catalog, &behavior->artifact_path)) return false;
        ENUM(behavior, runtime, QA_PROGRAM_NATIVE); ENUM(behavior, role, QA_BUILTIN_GRAPPLE);
        if (!text(io, catalog, &behavior->declaration_path) ||
            !qa_source_save_bytes(io, &behavior->declaration_digest, sizeof(behavior->declaration_digest)) ||
            !qa_source_save_bytes(io, &behavior->artifact_digest, sizeof(behavior->artifact_digest)) ||
            !payload(io, &behavior->entry) || !text(io, catalog, &behavior->unavailable)) return false;
        for (size_t j = 0; j < i; ++j)
            if (catalog->behaviors[j].product == behavior->product && !strcmp(catalog->behaviors[j].id, behavior->id)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_catalog *catalog, qa_buffer *files)
{
    uint8_t magic[4] = {'Q','C','A','T'}; uint32_t schema = 4;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QCAT", 4) ||
        !qa_source_save_u32(io, &schema) || (schema != 2 && schema != 3 && schema != 4)) return false;
    FIELD(u64, catalog, generation);
    FIELD(bool, catalog, q3_demo_restricted);
    if (schema >= 3) { FIELD(u64, catalog, q3_download_mount); }
    qa_buffer dictionary = {0};
    bool ok = io->direction == QA_SOURCE_SAVE_READ || qa_save_strings_encode(catalog->strings, &dictionary, io->error);
    if (ok) ok = blob(io, &dictionary);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) ok = qa_save_strings_decode(
        (qa_bytes){dictionary.data, dictionary.size}, &catalog->strings, io->error);
    qa_buffer_free(&dictionary);
    if (!ok || !text(io, catalog, &catalog->root) || !catalog->root || !*catalog->root ||
        !text(io, catalog, &catalog->user) || !blob(io, files)) return false;
    if (!physical(io, catalog) || !products(io, catalog, schema) || !mods(io, catalog) || !behaviors(io, catalog)) return false;
    if (catalog->q3_download_mount) {
        const catalog_physical *root = catalog_package(catalog, catalog->q3_download_mount);
        if (!catalog->user || !*catalog->user || !root || root->view.format != QA_ARCHIVE_AUTO || !root->view.writable) return false;
        for (size_t i = 0; i < catalog->product_count; ++i)
            for (size_t j = 0; j < catalog->products[i].mount_count; ++j)
                if (catalog->products[i].mounts[j] == catalog->q3_download_mount) return false;
    }
    return true;
}
bool qa_catalog_checkpoint(const qa_catalog *catalog, const qa_catalog_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!catalog || !catalog->references || !refs || !refs->files_encode || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog capture requires its actual retained immutable snapshot");
    if (!catalog_q3_restriction_valid(catalog, error)) return false;
    qa_buffer files = {0};
    if (!refs->files_encode(refs->context, catalog->mounts, &files, error)) { qa_buffer_free(&files); return false; }
    qa_source_save_io io;
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, (qa_catalog *)catalog, &files) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&files);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Catalog snapshot leaves its actual source field domains");
    return ok;
}
bool qa_catalog_restore(qa_resource_pool *resources, const qa_catalog_checkpoint_refs *refs,
    qa_bytes bytes, qa_catalog **out, qa_error *error)
{
    if (!resources || !refs || !refs->files_decode || !refs->physical_ready || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog restore requires an isolated pool and actual snapshot admissions");
    qa_catalog *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) return fail(error, QA_ERROR_MEMORY, "Allocating retained catalog snapshot");
    catalog->references = 1; catalog->resources = resources;
    if (!qa_strings_create(&catalog->restored_literals, error)) { qa_catalog_release(catalog); return false; }
    qa_source_save_io io; qa_buffer files = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, catalog, &files) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    for (size_t i = 0; ok && i < catalog->physical_count; ++i)
        ok = refs->physical_ready(refs->context, &catalog->physical[i].view,
            catalog->physical[i].members, catalog->physical[i].member_count, error);
    if (ok) ok = refs->files_decode(refs->context, resources, (qa_bytes){files.data, files.size}, &catalog->mounts, error) && catalog->mounts;
    if (ok) ok = catalog_q3_restriction_valid(catalog, error);
    if (ok && catalog->q3_download_mount && !qa_catalog_q3_download_root(catalog)) ok = false;
    for (size_t i = 0; ok && i < catalog->product_count; ++i)
        if (catalog->products[i].write_mount && !qa_catalog_product_write_root(catalog, (qa_product_id)i + 1)) ok = false;
    qa_buffer_free(&files);
    if (!ok) {
        qa_catalog_release(catalog);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Unqualified retained catalog snapshot");
        return false;
    }
    catalog->product_capacity = catalog->product_count; catalog->physical_capacity = catalog->physical_count;
    catalog->mod_capacity = catalog->mod_count; catalog->behavior_capacity = catalog->behavior_count;
    *out = catalog; return true;
}
