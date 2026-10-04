#include "network_q3_packages.h"
#include "qa/launch.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct package_mount {
    qa_mount_id mount;
    const qa_product *product;
    qa_q3_pak_entry entry;
    char *game, *basename;
    bool archive;
} package_mount;
struct frontend_q3_packages {
    qa_application *application;
    qa_actor_owner owner;
    qa_vfs *content;
    uint64_t source_generation, read_generation;
    size_t read_count, mount_count, pack_count, seen_count, seen_capacity;
    package_mount *mounts;
    const qa_q3_pak_entry **packs;
    uint32_t *pure_checksums;
    uint64_t *seen;
    qa_q3_pak_references *references;
    qa_vfs_acquisition cgame;
};
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static double loose_random(void *context) { (void)context; return 1; }
static bool current(const frontend_q3_packages *packages, qa_error *error)
{
    bool valid = packages && packages->application &&
        packages->source_generation == qa_application_configuration_generation(packages->application) &&
        packages->content == qa_application_network_q3_content(packages->application, packages->owner, error) &&
        packages->read_generation == qa_vfs_read_generation(packages->content) &&
        packages->read_count <= qa_vfs_read_count(packages->content) &&
        packages->mount_count == qa_vfs_mount_count(packages->content);
    for (size_t i = 0; valid && i < packages->mount_count; ++i) {
        qa_vfs_mount_info view;
        valid = qa_vfs_mount_at(packages->content, i, &view) &&
            view.id == packages->mounts[i].mount && view.is_archive == packages->mounts[i].archive;
    }
    return valid || fail(error, QA_ERROR_FORMAT, "Q3 package factory no longer owns its actual source content, mount order and read generation");
}
static const qa_product *mount_product(qa_catalog *catalog, const qa_vfs *content,
    const qa_vfs_mount_info *view, qa_error *error)
{
    const char *path = qa_vfs_mount_path(content, view->id);
    const qa_product *selected = NULL;
    const qa_sha256_digest *digest = NULL;
    if (view->is_archive && !qa_vfs_archive_digest_read(content, view->id, &digest, error)) return NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *product = qa_catalog_at(catalog, i);
        if (product->family != QA_GAME_Q3) continue;
        const qa_mount_id *ids = NULL; size_t count = 0;
        if (!qa_catalog_product_own_mounts(catalog, product->id, &ids, &count)) continue;
        for (size_t j = 0; j < count; ++j) for (size_t k = 0; k < qa_catalog_mount_count(catalog); ++k) {
            const qa_catalog_mount *mount = qa_catalog_mount_at(catalog, k);
            if (mount->id != ids[j] || strcmp(path, mount->path) || mount->format != view->format) continue;
            if (view->is_archive) {
                const qa_sha256_digest *physical = NULL;
                if (!qa_catalog_mount_digest_read(catalog, mount->id, &physical, error)) return NULL;
                if (!qa_sha256_equal(physical, digest)) continue;
            }
            if (selected && selected != product) {
                fail(error, QA_ERROR_FORMAT, "Q3 mount has ambiguous actual catalog product ownership"); return NULL;
            }
            selected = product;
        }
    }
    return selected;
}
static bool package_fields(package_mount *mount, const qa_archive *archive,
    const char *path, uint32_t feed, qa_error *error)
{
    const char *game = strrchr(mount->product->directory, '/');
    game = game ? game + 1 : mount->product->directory;
    const char *basename = strrchr(path, '/'); basename = basename ? basename + 1 : path;
    size_t game_length = strlen(game), name_length = strlen(basename);
    if (!game_length || name_length <= 4 || qa_archive_kind_for_path(basename) != QA_ARCHIVE_PK3)
        return fail(error, QA_ERROR_FORMAT, "Actual Q3 package lacks its declared game directory and PK3 basename");
    mount->game = malloc(game_length + 1); mount->basename = malloc(name_length - 3);
    if (!mount->game || !mount->basename) return fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 package names");
    memcpy(mount->game, game, game_length + 1); memcpy(mount->basename, basename, name_length - 4);
    mount->basename[name_length - 4] = 0;
    mount->entry.game = mount->game; mount->entry.basename = mount->basename; mount->entry.archive_path = path;
    size_t count = qa_archive_count(archive);
    if (count > SIZE_MAX / sizeof(uint64_t)) return fail(error, QA_ERROR_MEMORY, "Q3 archive checksum directory extent");
    uint32_t *crc = count ? malloc(count * sizeof(*crc)) : NULL;
    uint64_t *sizes = count ? malloc(count * sizeof(*sizes)) : NULL;
    if (count && (!crc || !sizes)) { free(crc); free(sizes); return fail(error, QA_ERROR_MEMORY, "Retaining Q3 CRC directory"); }
    for (size_t i = 0; i < count; ++i) {
        const qa_archive_entry *member = qa_archive_entry_at(archive, i);
        crc[i] = member->crc32; sizes[i] = member->size;
    }
    bool ok = qa_q3_package_checksums(crc, sizes, count, feed, &mount->entry.checksum, &mount->entry.pure_checksum, error);
    free(crc); free(sizes); return ok;
}
void frontend_q3_packages_destroy(frontend_q3_packages *packages)
{
    if (!packages) return;
    qa_q3_pak_references_destroy(packages->references);
    qa_vfs_acquisition_dispose(&packages->cgame);
    if (packages->mounts) for (size_t i = 0; i < packages->mount_count; ++i) { free(packages->mounts[i].game); free(packages->mounts[i].basename); }
    free(packages->mounts); free(packages->packs); free(packages->pure_checksums); free(packages->seen); free(packages);
}
bool frontend_q3_packages_create(qa_application *application, qa_actor_owner owner, uint32_t feed,
    frontend_q3_packages **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 package factory requires an empty output");
    qa_vfs *content = qa_application_network_q3_content(application, owner, error);
    qa_catalog *catalog = qa_launch_snapshot_catalog(qa_application_launch(application));
    if (!content || !catalog) return false;
    qa_product_id source_product;
    if (!qa_application_network_q3_content_product(application, owner, &source_product, error)) return false;
    const qa_product *overlay_product = qa_catalog_product(catalog, source_product);
    if (!overlay_product || overlay_product->family != QA_GAME_Q3)
        return fail(error, QA_ERROR_FORMAT, "Q3 content factory lacks its actual selected GAME product");
    frontend_q3_packages *packages = calloc(1, sizeof(*packages));
    if (!packages) return fail(error, QA_ERROR_MEMORY, "Allocating actual Q3 package owner");
    packages->application = application; packages->owner = owner; packages->content = content;
    packages->source_generation = qa_application_configuration_generation(application);
    packages->read_generation = qa_vfs_read_generation(content); packages->mount_count = qa_vfs_mount_count(content);
    if (packages->mount_count > SIZE_MAX / sizeof(*packages->mounts)) goto memory;
    packages->mounts = calloc(packages->mount_count ? packages->mount_count : 1, sizeof(*packages->mounts));
    packages->packs = calloc(QA_Q3_SEARCH_PATHS, sizeof(*packages->packs));
    packages->pure_checksums = calloc(QA_Q3_SEARCH_PATHS, sizeof(*packages->pure_checksums));
    if (!packages->mounts || !packages->packs || !packages->pure_checksums) goto memory;
    for (size_t i = 0; i < packages->mount_count; ++i) {
        qa_vfs_mount_info view;
        if (!qa_vfs_mount_at(content, i, &view)) goto invalid;
        package_mount *mount = packages->mounts + i; mount->mount = view.id; mount->archive = view.is_archive;
        qa_error local = {0}; mount->product = mount_product(catalog, content, &view, &local);
        if (local.code != QA_OK) { if (error) *error = local; goto invalid; }
        if (!mount->product && view.user_overlay && !view.is_archive) mount->product = overlay_product;
        if (!mount->product || mount->product->family != QA_GAME_Q3 || !view.is_archive || view.format != QA_ARCHIVE_PK3) continue;
        if (packages->pack_count == QA_Q3_SEARCH_PATHS) goto invalid;
        const qa_archive *archive = qa_vfs_archive(content, view.id);
        if (!archive || !package_fields(mount, archive, qa_vfs_mount_path(content, view.id), feed, error)) goto invalid;
        packages->packs[packages->pack_count] = &mount->entry;
        packages->pure_checksums[packages->pack_count++] = mount->entry.pure_checksum;
    }
    if (!qa_q3_pak_references_create(packages->packs, packages->pack_count, feed, loose_random, NULL, &packages->references, error) ||
        !frontend_q3_packages_collect(packages, error)) goto invalid;
    *out = packages; return true;
memory:
    fail(error, QA_ERROR_MEMORY, "Allocating actual Q3 package inventory");
invalid:
    if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Actual Q3 package inventory is incomplete");
    frontend_q3_packages_destroy(packages); return false;
}
static size_t resource_slot(uint64_t id, size_t capacity)
{ id ^= id >> 33; id *= UINT64_C(11400714819323198485); return (size_t)id & (capacity - 1); }
static bool reserve_seen(frontend_q3_packages *packages, qa_error *error)
{
    if (packages->seen_capacity && packages->seen_count + 1 <= packages->seen_capacity / 2) return true;
    size_t next = packages->seen_capacity ? packages->seen_capacity * 2 : 64;
    if (next < packages->seen_capacity || next > SIZE_MAX / sizeof(*packages->seen))
        return fail(error, QA_ERROR_MEMORY, "Q3 opened resource identity index exhausted");
    uint64_t *seen = calloc(next, sizeof(*seen));
    if (!seen) return fail(error, QA_ERROR_MEMORY, "Retaining Q3 actual opened resource identities");
    for (size_t i = 0; i < packages->seen_capacity; ++i) if (packages->seen[i]) {
        size_t slot = resource_slot(packages->seen[i], next); while (seen[slot]) slot = (slot + 1) & (next - 1);
        seen[slot] = packages->seen[i];
    }
    free(packages->seen); packages->seen = seen; packages->seen_capacity = next; return true;
}
bool frontend_q3_packages_collect(frontend_q3_packages *packages, qa_error *error)
{
    if (!current(packages, error)) return false;
    size_t count = qa_vfs_read_count(packages->content);
    while (packages->read_count < count) {
        qa_vfs_read_reference read;
        if (!qa_vfs_read_at(packages->content, packages->read_count, &read)) return false;
        package_mount *mount = NULL;
        for (size_t i = 0; i < packages->mount_count; ++i) if (packages->mounts[i].mount == read.mount) mount = packages->mounts + i;
        if (!mount) return fail(error, QA_ERROR_FORMAT, "Actual opened resource lacks its mounted Q3 factory owner");
        if (!mount->product || mount->product->family != QA_GAME_Q3) { ++packages->read_count; continue; }
        if (!reserve_seen(packages, error)) return false;
        uint64_t id = qa_resource_id(read.resource); size_t slot = resource_slot(id, packages->seen_capacity);
        while (packages->seen[slot] && packages->seen[slot] != id) slot = (slot + 1) & (packages->seen_capacity - 1);
        if (!packages->seen[slot]) {
            if (mount->archive && !mount->entry.archive_path)
                return fail(error, QA_ERROR_FORMAT, "Opened Q3 archive does not belong to the actual PK3 package catalog");
            bool ok = mount->entry.archive_path ? qa_q3_pak_record_packed(packages->references, &mount->entry, read.path, error) :
                qa_q3_pak_record_loose(packages->references, read.path, error);
            if (!ok) return false;
            packages->seen[slot] = id; ++packages->seen_count;
        }
        ++packages->read_count;
    }
    return true;
}
bool frontend_q3_packages_view(frontend_q3_packages *packages,
    qa_application_network_q3_package_view *out, qa_error *error)
{
    if (!out || !frontend_q3_packages_collect(packages, error)) return false;
    *out = (qa_application_network_q3_package_view){.owner = packages->owner, .content = packages->content,
        .references = packages->references, .source_generation = packages->source_generation,
        .read_generation = packages->read_generation, .read_count = packages->read_count};
    return true;
}
void frontend_q3_packages_rebind(frontend_q3_packages *packages, qa_application *application)
{ if (packages) packages->application = application; }
static bool receipt_valid(const frontend_q3_packages *packages, const qa_vfs_acquisition *receipt, bool native_identity, qa_error *error)
{
    if (!receipt->resource_id)
        return (!receipt->mount && !receipt->path && !receipt->lookup_path && !receipt->link_source && !receipt->link_target) ||
            fail(error, QA_ERROR_FORMAT, "Absent Q3 cgame producer retains acquisition authority");
    if (!receipt->path || strcmp(receipt->path, "vm/cgame.qvm") ||
        !(native_identity ? qa_vfs_acquisition_valid(packages->content, receipt, error) :
            qa_vfs_acquisition_retained(packages->content, receipt, error)))
        return fail(error, QA_ERROR_FORMAT, "Saved Q3 cgame acquisition differs from its actual canonical lookup and mounted source");
    for (size_t i = 0; i < packages->mount_count; ++i) {
        const package_mount *mount = packages->mounts + i;
        if (mount->mount == receipt->mount && mount->product && mount->product->family == QA_GAME_Q3) return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Saved Q3 cgame receipt has another physical product owner");
}
static bool prepare(frontend_q3_packages *packages, bool restoring, bool native_identity, qa_error *error)
{
    if (!frontend_q3_packages_collect(packages, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_host_cvars(packages->application, packages->owner, error);
    const qa_cvar_view *pure = cvars ? qa_cvars_find(cvars, "sv_pure") : NULL;
    bool enabled = pure && pure->integer;
    if (!receipt_valid(packages, &packages->cgame, native_identity, error)) return false;
    if (enabled && !packages->cgame.resource_id) {
        if (restoring) return fail(error, QA_ERROR_FORMAT, "Saved pure source lacks its genuine cgame acquisition");
        qa_resource *resource = NULL;
        if (!qa_vfs_acquire_receipt(packages->content, "vm/cgame.qvm", &resource, &packages->cgame, error)) return false;
        qa_resource_release(resource);
        if (!frontend_q3_packages_collect(packages, error) || !receipt_valid(packages, &packages->cgame, false, error)) return false;
    }
    if (!cvars) return false;
    const char *names[] = {"sv_paks", "sv_pakNames", "sv_referencedPaks", "sv_referencedPakNames"};
    const qa_q3_pak_report_kind kinds[] = {QA_Q3_PAK_LOADED_CHECKSUMS, QA_Q3_PAK_LOADED_NAMES,
        QA_Q3_PAK_REFERENCED_CHECKSUMS, QA_Q3_PAK_REFERENCED_NAMES};
    for (size_t i = 0; i < 4; ++i) {
        char report[QA_Q3_BIG_INFO_CHARS] = {0};
        if ((i >= 2 || enabled) && !qa_q3_pak_report(packages->references, kinds[i], report, sizeof(report), error)) return false;
        if (restoring) {
            const qa_cvar_view *saved = qa_cvars_find(cvars, names[i]);
            if (!saved || strcmp(saved->value, report)) return fail(error, QA_ERROR_FORMAT, "Saved Q3 package report differs from its actual opened resource journal");
        } else if (!qa_cvars_register(cvars, names[i], "", QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY,
            packages->owner, "Original Q3 mounted package report", error) || !qa_cvars_set(cvars, names[i], report, true, error)) return false;
    }
    return true;
}
bool frontend_q3_packages_prepare(frontend_q3_packages *packages, bool restoring, qa_error *error)
{ return prepare(packages, restoring, restoring, error); }
bool frontend_q3_packages_check(frontend_q3_packages *packages, qa_error *error)
{ return prepare(packages, true, false, error); }
static bool receipt_path(qa_source_save_io *io, char **path)
{
    size_t length = *path ? strlen(*path) : 0;
    if (!qa_source_save_count(io, &length, 65535)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *path = malloc(length + 1);
        if (!*path) return fail(io->error, QA_ERROR_MEMORY, "Restoring actual Q3 acquisition recipe");
        (*path)[length] = 0;
    }
    if (length && !qa_source_save_bytes(io, *path, length)) return false;
    return !length || !memchr(*path, 0, length) || fail(io->error, QA_ERROR_FORMAT, "Q3 acquisition recipe contains an embedded terminator");
}
static bool package_fields_save(qa_source_save_io *io, const frontend_q3_packages *packages, qa_vfs_acquisition *receipt)
{
    uint32_t magic = UINT32_C(0x50473351);
    uint64_t owner = packages->owner, source = packages->source_generation, reads = packages->read_generation;
    size_t count = packages->read_count; uint32_t feed = qa_q3_pak_checksum_feed(packages->references);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x50473351) ||
        !qa_source_save_u64(io, &owner) || owner != packages->owner ||
        !qa_source_save_u64(io, &source) || source != packages->source_generation ||
        !qa_source_save_u64(io, &reads) || reads != packages->read_generation ||
        !qa_source_save_count(io, &count, SIZE_MAX) || count != packages->read_count ||
        !qa_source_save_u32(io, &feed) || feed != qa_q3_pak_checksum_feed(packages->references) ||
        !qa_source_save_u64(io, &receipt->mount) || !qa_source_save_u64(io, &receipt->resource_id)) return false;
    if (!receipt->resource_id) return !receipt->mount;
    return receipt_path(io, &receipt->path) && receipt_path(io, &receipt->lookup_path) &&
        receipt_path(io, &receipt->link_source) && receipt_path(io, &receipt->link_target);
}
bool frontend_q3_packages_checkpoint(const frontend_q3_packages *packages, qa_buffer *out, qa_error *error)
{
    if (!out || !current(packages, error) || packages->read_count != qa_vfs_read_count(packages->content) ||
        !receipt_valid(packages, &packages->cgame, true, error)) return false;
    qa_source_save_io io = {0}; qa_vfs_acquisition receipt = packages->cgame;
    bool ok = qa_source_save_writer(&io, NULL, error) && package_fields_save(&io, packages, &receipt) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_q3_packages_restore_receipt(frontend_q3_packages *packages, qa_bytes bytes, qa_error *error)
{
    if (!current(packages, error) || packages->cgame.resource_id)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 package receipt requires its empty candidate producer");
    qa_source_save_io io = {0}; qa_vfs_acquisition receipt = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && package_fields_save(&io, packages, &receipt) &&
        qa_source_save_finish(&io, NULL) && receipt_valid(packages, &receipt, true, error);
    qa_source_save_dispose(&io);
    if (!ok) { qa_vfs_acquisition_dispose(&receipt); return false; }
    packages->cgame = receipt; return true;
}
static bool member_checksum(frontend_q3_packages *packages, const char *path, int32_t *checksum,
    bool *present, qa_error *error)
{
    *present = false;
    for (size_t i = 0; i < packages->mount_count; ++i) {
        package_mount *mount = packages->mounts + i; if (!mount->entry.archive_path) continue;
        const qa_archive *archive = qa_vfs_archive(packages->content, mount->mount);
        size_t start = 0;
        for (;;) {
            const qa_archive_entry *entry = NULL;
            if (!qa_archive_find_normalized(archive, path, QA_ARCHIVE_ASCII_INSENSITIVE, start, &entry, error)) return false;
            if (!entry) break;
            if (!entry->is_directory) {
                uint32_t value = mount->entry.pure_checksum;
                *checksum = value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296));
                *present = true; return true;
            }
            start = entry->ordinal + 1;
        }
    }
    return true;
}
bool frontend_q3_packages_pure(frontend_q3_packages *packages, int32_t server_id,
    qa_q3_pure_server *out, qa_error *error)
{
    if (!out || !frontend_q3_packages_collect(packages, error)) return false;
    qa_cvars *cvars = qa_application_network_q3_host_cvars(packages->application, packages->owner, error);
    if (!cvars) return false;
    const qa_cvar_view *pure = qa_cvars_find(cvars, "sv_pure");
    uint32_t feed = qa_q3_pak_checksum_feed(packages->references);
    *out = (qa_q3_pure_server){.enabled = pure && pure->integer, .checksum_feed_server_id = server_id,
        .checksum_feed = feed <= INT32_MAX ? (int32_t)feed : (int32_t)((int64_t)feed - INT64_C(4294967296)),
        .loaded = packages->pure_checksums, .loaded_count = packages->pack_count};
    return member_checksum(packages, "vm/cgame.qvm", &out->cgame_checksum, &out->has_cgame, error) &&
        member_checksum(packages, "vm/ui.qvm", &out->ui_checksum, &out->has_ui, error);
}
bool frontend_q3_packages_download(frontend_q3_packages *packages, const char *name,
    qa_bytes *out, const qa_sha256_digest **digest, qa_error *error)
{
    if (!out || !digest || !current(packages, error) || !qa_q3_download_name(name, error)) return false;
    for (size_t i = 0; i < packages->mount_count; ++i) {
        package_mount *mount = packages->mounts + i; if (!mount->entry.archive_path) continue;
        size_t game = strlen(mount->game), base = strlen(mount->basename), length = strlen(name);
        if (game > SIZE_MAX - base - 6 || length != game + base + 5) continue;
        char *path = malloc(length + 1);
        if (!path) return fail(error, QA_ERROR_MEMORY, "Matching actual mounted Q3 download name");
        snprintf(path, length + 1, "%s/%s.pk3", mount->game, mount->basename);
        bool matches = qa_archive_paths_equal(path, name, QA_ARCHIVE_ASCII_INSENSITIVE); free(path);
        if (!matches) continue;
        qa_vfs_mount_info info;
        if (!qa_vfs_archive_bytes(packages->content, mount->mount, out, error) ||
            !qa_vfs_mount_at(packages->content, i, &info) || !info.digest) return false;
        *digest = info.digest; return true;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Requested Q3 package is absent from the actual mounted source catalog");
}
