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
    uint8_t kind = io->direction == QA_SOURCE_SAVE_WRITE && *value ? 2 : 0;
    if (!qa_source_save_u8(io, &kind) || kind > 2) return false;
    if (!kind) { if (io->direction == QA_SOURCE_SAVE_READ) *value = NULL; return true; }
    if (kind == 1) {
        qa_string_id id = QA_STRING_NONE;
        if (!qa_source_save_u32(io, &id) || !id) return false;
        *value = qa_strings_cstr(catalog->strings, id);
        return *value != NULL;
    }
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &length, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)*value, length);
    qa_bytes bytes; qa_string_id id;
    if (!qa_source_save_span(io, length, &bytes)) return false;
    if (memchr(bytes.data, 0, bytes.size)) return fail(io->error, QA_ERROR_FORMAT, "Catalog text contains NUL");
    if (!qa_strings_intern(catalog->strings, bytes, &id, io->error)) return false;
    *value = qa_strings_cstr(catalog->strings, id); return *value != NULL;
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
    ARRAY(catalog, physical, physical_count, 15);
    for (size_t i = 0; i < catalog->physical_count; ++i) {
        catalog_physical *package = &catalog->physical[i]; qa_catalog_mount *view = &package->view;
        FIELD(u64, view, id);
        if (view->id != i + 1 || !text(io, catalog, &view->path) || !view->path || !*view->path) return false;
        ENUM(view, format, QA_ARCHIVE_KPF); FIELD(bool, view, writable);
        size_t saved_members = 0;
        size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 9 : 0;
        if (!qa_source_save_count(io, &saved_members, maximum)) return false;
        for (size_t j = 0; j < saved_members; ++j) {
            const char *ignored = NULL; size_t ordinal = 0;
            if (!text(io, catalog, &ignored) || !qa_source_save_count(io, &ordinal, SIZE_MAX)) return false;
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            qa_vfs_mount_info mount; bool found = false;
            for (size_t j = 0; j < qa_vfs_mount_count(catalog->mounts); ++j)
                if (qa_vfs_mount_at(catalog->mounts, j, &mount) && mount.id == view->id) { found = true; break; }
            const char *path = found ? qa_vfs_mount_path(catalog->mounts, view->id) : NULL;
            if (!path || strcmp(path, view->path) || mount.format != view->format || mount.writable != view->writable)
                return fail(io->error, QA_ERROR_FORMAT, "Catalog mount is absent from its installed content view");
            if (!catalog_index_package(catalog, package, io->error)) return false;
            view->identity = qa_vfs_archive_identity(catalog->mounts, view->id);
        }
    }
    return true;
}
static bool products(qa_source_save_io *io, qa_catalog *catalog)
{
    ARRAY(catalog, products, product_count, 78);
    if (catalog->product_count > UINT32_MAX) return false;
    for (size_t i = 0; i < catalog->product_count; ++i) {
        catalog_product *product = &catalog->products[i]; qa_product *view = &product->view;
        FIELD(u32, view, id); FIELD(u32, view, base);
        FIELD(u32, product, configuration_base); FIELD(u64, product, write_mount);
        FIELD(u64, product, loose_mount);
        FIELD(u64, product, family_mount); FIELD(u32, product, family_product);
        if (product->configuration_base >= view->id || product->write_mount > catalog->physical_count ||
            product->loose_mount > catalog->physical_count) return false;
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
            !mount_ids(io, catalog, &product->mounts, &product->mount_count)) return false;
        if (!text(io,catalog,&product->installed_directory)) return false;
        if (
            !text(io, catalog, &product->witness) ||
            !qa_source_save_count(io, &product->required_count, 4)) return false;
        for (size_t j = 0; j < 4; ++j)
            if (!text(io, catalog, &product->required[j]) || (j < product->required_count && !product->required[j])) return false;
        /* Map indexes and mapdb starts are installed content, rebuilt below.
         * Consume any prior descriptor in the same reader without retaining it. */
        size_t maps = 0;
        size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 18 : 0;
        if (!qa_source_save_count(io, &maps, maximum)) return false;
        for (size_t j = 0; j < maps; ++j) {
            qa_catalog_map map = {0};
            if (!text(io, catalog, &map.path)) return false;
            FIELD(u64, &map, mount);
            if (!qa_source_save_count(io, &map.member, SIZE_MAX)) return false;
            FIELD(bool, &map, archived);
        }
        size_t starts = 0;
        maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 8 : 0;
        if (!qa_source_save_count(io, &starts, maximum)) return false;
        for (size_t j = 0; j < starts; ++j) {
            qa_catalog_start start = {0};
            if (!text(io, catalog, &start.episode) || !text(io, catalog, &start.bsp) ||
                !text(io, catalog, &start.path) || !text(io, catalog, &start.title) ||
                !text(io, catalog, &start.start_items)) return false;
            FIELD(bool, &start, singleplayer); FIELD(bool, &start, cooperative); FIELD(bool, &start, capture_the_flag);
        }
        bool has_episode = false; qa_catalog_episode episode = {0};
        if (!qa_source_save_bool(io, &has_episode) ||
            !text(io, catalog, &episode.id) || !text(io, catalog, &episode.command) ||
            !text(io, catalog, &episode.name) || !text(io, catalog, &episode.activity)) return false;
        FIELD(bool, &episode, needs_skill_select);
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(catalog->products[j].view.key, view->key)) return false;
    }
    for (size_t i = 0; i < catalog->product_count; ++i) {
        const catalog_product *product = &catalog->products[i];
        const qa_product *configuration_base = qa_catalog_product(catalog, product->configuration_base);
        if (configuration_base && configuration_base->family != product->view.family) return false;
        if ((!catalog->q3_demo_restricted || product->view.family != QA_GAME_Q3) &&
            product->configuration_base != product->view.base) return false;
        if (product->family_mount || product->family_product) {
            if (product->view.family != QA_GAME_Q1 || !product->family_mount || !product->family_product) return false;
            const catalog_product *installed = product; size_t depth = 0;
            while (!installed->installed_directory && installed->view.base) {
                if (++depth > catalog->product_count) return false;
                installed = catalog->products + installed->view.base - 1;
            }
            const qa_catalog_mount *family = catalog_mount(catalog, product->family_mount);
            if (!family || family->format != QA_ARCHIVE_AUTO || family->writable ||
                !installed->installed_directory || installed->view.id != product->family_product) return false;
            const char *parent = catalog_native_parent(catalog, installed->installed_directory, io->error);
            if (!parent || strcmp(parent, family->path)) return false;
        }
        if (product->loose_mount) {
            const qa_catalog_mount *loose = catalog_mount(catalog, product->loose_mount);
            const qa_catalog_mount *corpus = catalog_mount(catalog, catalog->corpus_mount);
            if (!loose || loose->format != QA_ARCHIVE_AUTO) return false;
            if (!product->installed_directory || strcmp(product->installed_directory,loose->path)) return false;
            size_t path_length = strlen(loose->path), directory_length = strlen(product->view.directory);
            size_t root_length = corpus?strlen(corpus->path):0;
            bool separator = root_length && corpus->path[root_length - 1] != '/' && corpus->path[root_length - 1] != '\\';
            bool corpus_path=corpus && corpus->format==QA_ARCHIVE_AUTO && !corpus->writable && loose->id!=corpus->id &&
                directory_length && path_length>root_length && !memcmp(loose->path,corpus->path,root_length) &&
                (!separator || loose->path[root_length]=='/' || loose->path[root_length]=='\\') &&
                path_length-root_length-(separator?1:0)==directory_length &&
                catalog_ascii_equal(loose->path+root_length+(separator?1:0),product->view.directory);
            if (!corpus_path && !catalog_location_matches(catalog,product->view.directory,loose->path)) return false;
            if (!catalog->q3_demo_restricted || product->view.family != QA_GAME_Q3) {
                bool own = false;
                for (size_t j = 0; j < product->own_count; ++j) own |= product->own_mounts[j] == product->loose_mount;
                if (!own) return false;
            }
        }
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
    if (io->direction == QA_SOURCE_SAVE_READ) {
        for (size_t i = 0; i < catalog->product_count; ++i) {
            catalog_product *product = &catalog->products[i];
            bool archives_only = catalog->q3_demo_restricted && product->view.family == QA_GAME_Q3;
            if (!catalog_index_maps(catalog, product, archives_only, io->error) ||
                !catalog_read_starts(catalog, product, io->error)) return false;
        }
    }
    return true;
}
static bool skip_payload(qa_source_save_io *io)
{
    size_t size = 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : 0;
    if (!qa_source_save_count(io, &size, maximum)) return false;
    qa_bytes ignored;
    return !size || qa_source_save_span(io, size, &ignored);
}
static bool mod_fields(qa_source_save_io *io, qa_catalog *catalog, qa_catalog_mod *mod)
{
    FIELD(u32, mod, product);
    if (!text(io, catalog, &mod->key) || !text(io, catalog, &mod->id) ||
        !text(io, catalog, &mod->title)) return false;
    ENUM(mod, purpose, QA_MOD_GAME_TYPE); ENUM(mod, runtime, QA_PROGRAM_NATIVE);
    return strings(io, catalog, (const char ***)&mod->requires, &mod->requires_count) &&
        strings(io, catalog, (const char ***)&mod->conflicts, &mod->conflicts_count) &&
        text(io, catalog, &mod->declaration_path) && text(io, catalog, &mod->program_path) &&
        qa_source_save_bytes(io, &mod->declaration_digest, sizeof(mod->declaration_digest)) &&
        qa_source_save_bytes(io, &mod->program_digest, sizeof(mod->program_digest)) &&
        skip_payload(io) && text(io, catalog, &mod->unavailable);
}
static bool mods(qa_source_save_io *io, qa_catalog *catalog)
{
    size_t count = 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 103 : 0;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_catalog_mod mod = {0};
        bool ok = mod_fields(io, catalog, &mod);
        free((void *)mod.requires); free((void *)mod.conflicts);
        if (!ok) return false;
    }
    return true;
}
static bool behaviors(qa_source_save_io *io, qa_catalog *catalog)
{
    size_t count = 0;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? (io->input.size - io->offset) / 92 : 0;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_catalog_weapon_behavior behavior = {0}; FIELD(u32, &behavior, product);
        if (!text(io, catalog, &behavior.id) || !text(io, catalog, &behavior.title) ||
            !text(io, catalog, &behavior.artifact_path)) return false;
        ENUM(&behavior, runtime, QA_PROGRAM_NATIVE); ENUM(&behavior, role, QA_BUILTIN_GRAPPLE);
        if (!text(io, catalog, &behavior.declaration_path) ||
            !qa_source_save_bytes(io, &behavior.declaration_digest, sizeof(behavior.declaration_digest)) ||
            !qa_source_save_bytes(io, &behavior.artifact_digest, sizeof(behavior.artifact_digest)) ||
            !skip_payload(io) || !text(io, catalog, &behavior.unavailable)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_catalog *catalog, qa_buffer *files,
    const qa_catalog_checkpoint_refs *refs)
{
    uint8_t magic[4] = {'Q','C','A','T'};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QCAT", sizeof(magic))) return false;
    FIELD(u64, catalog, generation);
    FIELD(bool, catalog, q3_demo_restricted);
    FIELD(u64, catalog, q3_download_mount);
    FIELD(u64, catalog, corpus_mount);
    for (size_t i = 0; i < 2; ++i)
        if (!qa_source_save_u64(io, &catalog->q2_download_mount[i])) return false;
    FIELD(u64,catalog,q3_install_mount);
    qa_buffer dictionary = {0};
    bool ok = blob(io, &dictionary);
    if (ok && io->direction == QA_SOURCE_SAVE_READ && dictionary.size) {
        qa_strings *strings = NULL;
        ok = qa_save_strings_decode((qa_bytes){dictionary.data, dictionary.size}, &strings, io->error);
        if (ok) { qa_strings_destroy(catalog->strings); catalog->strings = strings; }
    }
    qa_buffer_free(&dictionary);
    if (!ok) return false;
    if (!text(io, catalog, &catalog->root) || !catalog->root || !*catalog->root ||
        !text(io, catalog, &catalog->user)) return false;
    if (!strings(io,catalog,&catalog->install_roots,&catalog->install_root_count)) return false;
    for (size_t i=0;i<catalog->install_root_count;++i) if (!*catalog->install_roots[i]) return false;
    ARRAY(catalog,locations,location_count,2);
    static const char *const names[]={"q1","q1/id1","q1/rerelease","q1/rerelease/id1",
        "q2","q2/baseq2","q2/rerelease","q2/rerelease/baseq2","q3a","q3a/baseq3","q3a/demota"};
    for (size_t i=0;i<catalog->location_count;++i) {
        catalog_location *location=catalog->locations+i;
        if (!text(io,catalog,&location->logical) || !location->logical ||
            !text(io,catalog,&location->path) || !location->path || !*location->path) return false;
        bool known=false;
        for (size_t j=0;j<sizeof(names)/sizeof(*names);++j) known|=!strcmp(names[j],location->logical);
        if (!known) return false;
        for (size_t j=0;j<i;++j) if (!strcmp(catalog->locations[j].logical,location->logical) &&
            !strcmp(catalog->locations[j].path,location->path)) return false;
    }
    catalog->location_capacity=catalog->location_count;
    if (!blob(io,files)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ &&
        (!refs->files_decode(refs->context, catalog->resources, (qa_bytes){files->data, files->size},
            &catalog->mounts, io->error) || !catalog->mounts)) return false;
    if (!physical(io, catalog) || !products(io, catalog) || !mods(io, catalog) || !behaviors(io, catalog)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        for (size_t i = 0; i < catalog->product_count; ++i)
            if (!catalog_read_components(catalog, &catalog->products[i], io->error)) return false;
    if (catalog->corpus_mount) {
        const qa_catalog_mount *root = catalog_mount(catalog, catalog->corpus_mount);
        if (!root || root->format != QA_ARCHIVE_AUTO || root->writable) return false;
        for (size_t i = 0; i < catalog->product_count; ++i)
            for (size_t j = 0; j < catalog->products[i].mount_count; ++j)
                if (catalog->products[i].mounts[j] == catalog->corpus_mount) return false;
    }
    if (catalog->q3_install_mount) {
        const qa_catalog_mount *root=catalog_mount(catalog,catalog->q3_install_mount);
        if (!root || root->format!=QA_ARCHIVE_AUTO || root->writable) return false;
        bool mapped=catalog_location_matches(catalog,"q3a",root->path);
        const qa_catalog_mount *corpus=catalog_mount(catalog,catalog->corpus_mount);
        if (!mapped && corpus) {
            size_t length=strlen(corpus->path);
            bool separator=length && corpus->path[length-1]!='/' && corpus->path[length-1]!='\\';
            mapped=strlen(root->path)==length+(separator?1:0)+3 && !memcmp(root->path,corpus->path,length) &&
                (!separator || root->path[length]=='/' || root->path[length]=='\\') &&
                catalog_ascii_equal(root->path+length+(separator?1:0),"q3a");
        }
        if (!mapped) return false;
        for (size_t i=0;i<catalog->product_count;++i)
            for (size_t j=0;j<catalog->products[i].mount_count;++j)
                if (catalog->products[i].mounts[j]==root->id) return false;
    }
    if (catalog->q3_download_mount) {
        const catalog_physical *root = catalog_package(catalog, catalog->q3_download_mount);
        if (!catalog->user || !*catalog->user || !root || root->view.format != QA_ARCHIVE_AUTO || !root->view.writable) return false;
        for (size_t i = 0; i < catalog->product_count; ++i)
            for (size_t j = 0; j < catalog->products[i].mount_count; ++j)
                if (catalog->products[i].mounts[j] == catalog->q3_download_mount) return false;
    }
    for (size_t edition = 0; edition < 2; ++edition) {
        qa_mount_id id = catalog->q2_download_mount[edition];
        if (!id) continue;
        if (id == catalog->corpus_mount || id == catalog->q3_download_mount ||
            (edition && id == catalog->q2_download_mount[0])) return false;
        const catalog_physical *root = catalog_package(catalog, id);
        if (!catalog->user || !*catalog->user || !root || root->view.format != QA_ARCHIVE_AUTO || !root->view.writable) return false;
        for (size_t i = 0; i < catalog->product_count; ++i)
            for (size_t j = 0; j < catalog->products[i].mount_count; ++j)
                if (catalog->products[i].mounts[j] == id) return false;
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
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, (qa_catalog *)catalog, &files, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&files);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Catalog snapshot leaves its actual source field domains");
    return ok;
}
bool qa_catalog_restore(qa_resource_pool *resources, const qa_catalog_checkpoint_refs *refs,
    qa_bytes bytes, qa_catalog **out, qa_error *error)
{
    if (!resources || !refs || !refs->files_decode || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Catalog restore requires an isolated pool and actual snapshot admissions");
    qa_catalog *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) return fail(error, QA_ERROR_MEMORY, "Allocating retained catalog snapshot");
    catalog->references = 1; catalog->resources = resources;
    if (!qa_strings_create(&catalog->strings, error)) { qa_catalog_release(catalog); return false; }
    qa_source_save_io io; qa_buffer files = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, catalog, &files, refs) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) ok = catalog_q3_restriction_valid(catalog, error);
    if (ok && catalog->corpus_mount) {
        const qa_catalog_mount *root = catalog_mount(catalog, catalog->corpus_mount);
        const char *path = qa_vfs_mount_path(catalog->mounts, catalog->corpus_mount);
        bool admitted = false;
        for (size_t i = 0; i < qa_vfs_mount_count(catalog->mounts); ++i) {
            qa_vfs_mount_info actual = {0};
            if (qa_vfs_mount_at(catalog->mounts, i, &actual) && actual.id == catalog->corpus_mount)
                admitted = !actual.is_archive && !actual.writable;
        }
        ok = root && path && !strcmp(root->path, path) && admitted &&
            qa_vfs_mount_root(catalog->mounts, catalog->corpus_mount);
    }
    if (ok && catalog->q3_download_mount && !qa_catalog_q3_download_root(catalog)) ok = false;
    if (ok && catalog->q3_install_mount) {
        const qa_catalog_mount *root=catalog_mount(catalog,catalog->q3_install_mount);
        const char *path=qa_vfs_mount_path(catalog->mounts,catalog->q3_install_mount);
        ok=root && path && !strcmp(root->path,path) && qa_vfs_mount_root(catalog->mounts,root->id);
    }
    for (size_t i = 0; ok && i < 2; ++i)
        if (catalog->q2_download_mount[i] && !qa_catalog_q2_download_root(catalog,
            i ? QA_EDITION_RERELEASE : QA_EDITION_CLASSIC)) ok = false;
    for (size_t i = 0; ok && i < catalog->product_count; ++i) {
        if (catalog->products[i].write_mount && !qa_catalog_product_write_root(catalog, (qa_product_id)i + 1)) ok = false;
        if (catalog->products[i].loose_mount && !qa_catalog_product_loose_root(catalog, (qa_product_id)i + 1)) ok = false;
        if (catalog->products[i].family_mount && !qa_catalog_product_family_root(catalog, (qa_product_id)i + 1)) ok = false;
    }
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

bool catalog_copy_metadata(const qa_catalog *source, const qa_catalog_checkpoint_refs *refs,
    qa_catalog **out, qa_error *error)
{
    qa_buffer bytes = {0};
    bool ok = qa_catalog_checkpoint(source, refs, &bytes, error) &&
        qa_catalog_restore(source->resources, refs, (qa_bytes){bytes.data, bytes.size}, out, error);
    qa_buffer_free(&bytes); return ok;
}
