#include "remote_q2_private.h"
#include "remote_q2_restore.h"
#include "qa/archive.h"
#include "qa/network_q2_materials.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { Q2_HTTP_FILELIST_BYTES = 1 << 20 };
typedef struct remote_q2_http_asset {
    struct remote_q2_http_asset *next;
    qa_string_id path;
    qa_download_id job;
    bool package, game_local, settled;
} remote_q2_http_asset;
typedef struct remote_q2_http_downloads {
    frontend_remote_q2 *owner;
    qa_downloads *jobs;
    qa_arena storage;
    qa_id_index assets;
    remote_q2_http_asset *first, **tail;
    qa_http_request_id metadata_request;
    size_t metadata_size;
    unsigned metadata_list;
    bool metadata_done, metadata_success, manifest_seeded, initial_done;
    uint8_t metadata[Q2_HTTP_FILELIST_BYTES];
} remote_q2_http_downloads;

static bool refresh(frontend_remote_q2 *, qa_error *);
static void http_clear(frontend_remote_q2 *);
static bool http_prepare(frontend_remote_q2 *, const char *, bool *, qa_error *);
static bool http_request(frontend_remote_q2 *, const char *, bool *, qa_error *);

static unsigned char fold(unsigned char value)
{ return value >= 'A' && value <= 'Z' ? (unsigned char)(value + 'a' - 'A') : value; }
static bool prefix(const char *value, const char *expected)
{
    while (*expected) if (fold((unsigned char)*value++) != (unsigned char)*expected++) return false;
    return true;
}
static bool suffix(const char *value, const char *expected)
{ size_t a = strlen(value), b = strlen(expected); return a >= b && prefix(value + a - b, expected); }
static bool model_image(const char *path)
{
    return suffix(path, ".pcx") || suffix(path, ".png") || suffix(path, ".jpg") ||
        suffix(path, ".jpeg") || suffix(path, ".tga") || suffix(path, ".bmp") || suffix(path, ".gif");
}
static bool native_asset(const char *path)
{
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("_+./-", *p))) return false;
    return (prefix(path, "maps/") && suffix(path, ".bsp")) ||
        ((prefix(path, "models/") || prefix(path, "players/")) &&
            (suffix(path, ".mdl") || suffix(path, ".spr") || suffix(path, ".md2") || suffix(path, ".md3") ||
                suffix(path, ".sp2") || model_image(path) || suffix(path, ".wav") ||
                suffix(path, ".shader") || suffix(path, ".qai") || suffix(path, ".qpm") || suffix(path, ".lmp") || suffix(path, ".wal") ||
                suffix(path, ".roq") || suffix(path, ".cin") || suffix(path, ".ogv"))) ||
        (prefix(path, "sound/") && suffix(path, ".wav")) ||
        ((prefix(path, "pics/") || prefix(path, "sprites/")) && model_image(path)) ||
        (prefix(path, "env/") && (suffix(path, ".tga") || suffix(path, ".pcx"))) ||
        (prefix(path, "textures/") && suffix(path, ".wal"));
}
bool remote_q2_download_path_valid(const char *path)
{
    if (!path || !*path || !strchr(path, '/') || path[0] == '.' || !native_asset(path)) return false;
    qa_error error = {0}; char *normalized = qa_archive_normalize_path(path, &error);
    bool valid = normalized && !strcmp(normalized, path); free(normalized); return valid;
}
qa_fs_root *remote_q2_download_destination(const frontend_remote_q2 *row, const char *path)
{ return row && remote_q2_download_path_valid(path) &&
    (row->options.material_scripts || !(suffix(path, ".shader") || suffix(path, ".qai") || suffix(path, ".qpm") || suffix(path, ".lmp") ||
        suffix(path, ".roq") || suffix(path, ".cin") || suffix(path, ".ogv") ||
        ((prefix(path, "models/") || prefix(path, "players/")) && suffix(path, ".wal")))) ?
    (prefix(path, "players/") ? row->content.base_write_root : row->content.selected_write_root) : NULL; }

uint64_t remote_q2_download_extent(const frontend_remote_q2 *row)
{
    return row->download_block_pending && !row->download_block_committed ?
        row->download_block_offset + row->download_block_cursor : row->download_bytes;
}
static bool download_resource(void *context, const qa_download_request *request,
    const qa_download_view *view, bool staged, qa_error *error)
{
    frontend_remote_q2 *row = context;
    frontend_remote_q2_view owned = {0};
    if (!row || row->busy || row->image_policy || !request || !view || !staged ||
        !row->download_path || !row->download_root || !row->download_logical_nonce ||
        !request->path || !view->path || strcmp(request->path, row->download_path) ||
        strcmp(view->path, row->download_path) || request->stage_nonce != row->download_logical_nonce ||
        view->stage_nonce != row->download_logical_nonce || request->maximum_bytes != INT32_MAX ||
        request->expected_bytes || request->exact_length || request->resume ||
        view->received != remote_q2_download_extent(row) || view->received > INT32_MAX || view->limit != INT32_MAX ||
        view->state != QA_DOWNLOAD_RECEIVING || view->published || view->mounted ||
        row->download_root != remote_q2_download_destination(row, row->download_path) ||
        !qa_catalog_product_view_current(row->content.catalog, row->content.selected, row->content.mounts))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 download leaves its retained target and prefix");
    if (row->importing) return row->frontend->source_restoring &&
        frontend_remote_q2_import_read(row, &owned, error);
    if (row->frontend->capture) return remote_q2_capture_owned(row);
    if (row->retiring) return remote_q2_retirement_current(row, error);
    return frontend_remote_q2_read(row, &owned, error);
}
static bool download_stage(void *context, const qa_download_request *request,
    const qa_download_view *view, qa_bytes bytes, qa_fs_stage **out, uint64_t *nonce, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!out || *out || !nonce || *nonce || !download_resource(row, request, view, true, error) ||
        !row->importing || !row->frontend->source_restoring || row->download_stage ||
        !row->options.restore_stage || bytes.size != remote_q2_download_extent(row) || (bytes.size && !bytes.data))
        return false;
    qa_fs_stage *candidate = NULL; uint64_t native_nonce = 0, size = 0; qa_fs_identity identity = {0};
    ++row->busy;
    bool ok = row->options.restore_stage(row->options.context, row->download_root,
        row->download_path, row->download_logical_nonce, row->download_published, bytes, &candidate, &native_nonce, &identity, error);
    --row->busy;
    if (ok) ok = candidate && native_nonce && (row->download_published ? native_nonce == row->download_logical_nonce :
            native_nonce != row->download_logical_nonce) &&
        download_resource(row, request, view, true, error) &&
        qa_fs_stage_size(candidate, &size, error) && size == bytes.size;
    if (!ok) { *out = candidate; return false; }
    row->download_stage_sealed = row->download_published; row->download_identity = identity;
    *out = candidate; *nonce = native_nonce; return true;
}
static bool download_artifact(void *context, const qa_download_request *request,
    const qa_download_view *view, qa_fs_stage **out, qa_error *error)
{
    frontend_remote_q2 *row = context; uint64_t size = 0;
    if (!out || *out || !download_resource(row, request, view, true, error) ||
        !row->download_stage || !row->download_nonce) return false;
    qa_fs_stage *artifact = NULL;
    if (!qa_fs_stage_open_readonly(row->download_root, row->download_path,
        row->download_nonce, &artifact, &size, error)) return false;
    if (size != remote_q2_download_extent(row) || !download_resource(row, request, view, true, error)) {
        qa_fs_stage_close(artifact, true); return false;
    }
    *out = artifact; return true;
}
bool frontend_remote_q2_download_refs(frontend_remote_q2 *row,
    qa_download_checkpoint_refs *out, qa_error *error)
{
    if (!row || !out) return false;
    qa_download_request request = {.path = row->download_path, .maximum_bytes = INT32_MAX,
        .stage_nonce = row->download_logical_nonce};
    qa_download_view view = {.path = row->download_path, .received = remote_q2_download_extent(row),
        .limit = INT32_MAX, .state = QA_DOWNLOAD_RECEIVING, .stage_nonce = row->download_logical_nonce};
    if (!download_resource(row, &request, &view, true, error)) return false;
    *out = (qa_download_checkpoint_refs){.context = row, .resource = download_resource,
        .stage = download_stage, .artifact = download_artifact};
    return true;
}

static bool attempted(const frontend_remote_q2 *row, const char *path)
{
    for (size_t i = 0; i < row->download_attempted_count; ++i)
        if (!strcmp(row->download_attempted[i], path)) return true;
    return false;
}
static bool installed(frontend_remote_q2 *row, const char *path, bool *present, qa_error *error)
{
    qa_resource *resource = NULL; qa_error issue = {0};
    *present = qa_vfs_acquire(row->content.mounts, path, &resource, NULL, &issue);
    qa_resource_release(resource);
    if (*present || issue.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = issue;
    return false;
}
static bool remember(frontend_remote_q2 *row, const char *path, qa_error *error)
{
    if (attempted(row, path)) return true;
    size_t count = row->download_attempted_count;
    if (count >= SIZE_MAX / sizeof(char *) - 1) return false;
    char *copy = malloc(strlen(path) + 1);
    char **items = copy ? realloc(row->download_attempted, (count + 1) * sizeof(*items)) : NULL;
    if (!items) { free(copy); return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 download refusal/attempt"); }
    strcpy(copy, path); items[count] = copy; row->download_attempted = items; ++row->download_attempted_count; return true;
}
static bool http_enabled(const frontend_remote_q2 *row)
{
    const qa_cvar_view *value = qa_cvars_read(row->options.domain.cvars, row->http_downloads);
    return *row->download_server && value && value->number != 0 && !row->options.demo;
}
static bool http_package(const char *path)
{ return suffix(path, ".pak") || suffix(path, ".pkz") || suffix(path, ".pk3"); }
static bool http_path_valid(const char *path)
{
    if (!path || !*path || path[0] == '.') return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("_+./-", *p))) return false;
    if (http_package(path)) return !strchr(path, '/');
    if (!strchr(path, '/')) return false;
    static const char *const extensions[] = {".bsp", ".dm2", ".ent", ".jpg", ".loc", ".md2", ".md3",
        ".ogg", ".pcx", ".png", ".sp2", ".tga", ".txt", ".wal", ".wav", ".jpeg", ".bmp", ".gif"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i)
        if (suffix(path, extensions[i])) return true;
    return native_asset(path);
}
static qa_fs_root *http_destination(frontend_remote_q2 *row, const char *path)
{ return prefix(path, "players/") ? row->content.base_write_root : row->content.selected_write_root; }
static const char *http_asset_path(const remote_q2_http_downloads *owner, const remote_q2_http_asset *asset)
{ return qa_strings_cstr(qa_session_strings(qa_application_session(owner->owner->options.domain.application)), asset->path); }
static remote_q2_http_asset *http_asset_find(remote_q2_http_downloads *owner, qa_string_id path)
{
    qa_id_index_entry *entry = qa_id_index_find(&owner->assets, path, 0);
    return entry ? (remote_q2_http_asset *)(uintptr_t)entry->value : NULL;
}
static bool http_asset_admit(remote_q2_http_downloads *owner, const char *path, bool game_local,
    remote_q2_http_asset **out, qa_error *error)
{
    qa_strings *strings = qa_session_strings(qa_application_session(owner->owner->options.domain.application));
    qa_string_id id;
    if (!qa_strings_intern_cstr(strings, path, &id, error)) return false;
    remote_q2_http_asset *asset = http_asset_find(owner, id);
    if (!asset) {
        if (!qa_id_index_reserve(&owner->assets, 1, error)) return false;
        asset = qa_arena_alloc(&owner->storage, sizeof(*asset), _Alignof(remote_q2_http_asset), error);
        if (!asset) return false;
        *asset = (remote_q2_http_asset){.path = id, .package = http_package(path)};
        *owner->tail = asset; owner->tail = &asset->next;
        qa_id_index_insert(&owner->assets, id, 0, (size_t)(uintptr_t)asset);
    }
    asset->game_local |= game_local || asset->package;
    *out = asset; return true;
}
static bool http_local_mount(qa_mount_id mount, void *context)
{
    frontend_remote_q2 *row = context; qa_product_id product; qa_mount_id catalog_mount;
    return qa_catalog_product_mount_origin(row->content.catalog, row->content.selected, row->content.mounts,
        mount, &product, &catalog_mount) && product == row->content.selected;
}
static bool http_asset_installed(remote_q2_http_downloads *owner, const remote_q2_http_asset *asset,
    bool *present, qa_error *error)
{
    frontend_remote_q2 *row = owner->owner; const char *path = http_asset_path(owner, asset);
    if (asset->package) {
        qa_fs_entry_kind kind;
        if (!qa_fs_root_status(http_destination(row, path), path, &kind, NULL, error)) return false;
        *present = kind != QA_FS_MISSING; return true;
    }
    if (!asset->game_local) return installed(row, path, present, error);
    qa_resource *resource = NULL; qa_error issue = {0};
    *present = qa_vfs_acquire_filtered(row->content.mounts, path, http_local_mount, row, &resource, NULL, &issue);
    qa_resource_release(resource);
    if (*present || issue.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = issue;
    return false;
}
static void http_note(frontend_remote_q2 *row, const char *path)
{
    qa_error ignored = {0};
    qa_application_log_emit(row->options.domain.application, &row->options.domain.command_context,
        http_package(path) ? "HTTP package unavailable; continuing with individual files: " :
        "HTTP download unavailable; continuing with native downloads: ", QA_APPLICATION_LOG_CONSOLE, &ignored);
    qa_application_log_emit(row->options.domain.application, &row->options.domain.command_context,
        path, QA_APPLICATION_LOG_CONSOLE, &ignored);
    qa_application_log_emit(row->options.domain.application, &row->options.domain.command_context,
        "\n", QA_APPLICATION_LOG_CONSOLE, &ignored);
}
static bool http_permit(void *context, const qa_download_request *request, const char *url, qa_error *error)
{
    frontend_remote_q2 *row = context; bool allowed = false;
    return http_enabled(row) && url && http_path_valid(request->path) &&
        (!native_asset(request->path) || remote_q2_download_destination(row, request->path)) &&
        row->options.download_allowed(row->options.context, request->path, &allowed, error) && allowed &&
        remote_q2_live(row, error);
}
static qa_fs_root *http_target_root(void *context, const qa_download_request *request, qa_error *error)
{ (void)error; return http_destination(context, request->path); }
static bool http_inspect(void *context, const char *path, qa_fs_stage *stage, uint64_t size, qa_error *error)
{
    frontend_remote_q2 *row = context; bool allowed = false;
    if (!http_enabled(row) || !row->options.download_allowed(row->options.context, path, &allowed, error) ||
        !allowed || !remote_q2_live(row, error)) return false;
    if (!http_package(path) && !suffix(path, ".bsp")) return true;
    qa_fs_stage_mapping *mapping = NULL;
    if (!qa_fs_stage_map(stage, &mapping, error)) return false;
    qa_bytes bytes = qa_fs_stage_mapping_bytes(mapping);
    bool ok = bytes.size == size;
    if (ok && http_package(path)) {
        qa_archive *archive = NULL;
        ok = qa_archive_open_memory(bytes, qa_archive_kind_for_path(path), &archive, error);
        qa_archive_close(archive);
    } else if (ok) {
        qa_bsp_view bsp;
        ok = qa_bsp_open(bytes, &bsp, error) && qa_bsp_validate(&bsp, error);
    }
    qa_fs_stage_unmap(mapping); return ok;
}
static bool http_remount(void *context, const char *path, qa_error *error)
{ (void)path; return refresh(context, error); }
static const char *http_game(const frontend_remote_q2 *row)
{ return *row->data.gamedir ? row->data.gamedir : "baseq2"; }
static char *http_url(remote_q2_http_downloads *owner, const char *game, const char *path,
    const char *ending, qa_error *error)
{
    size_t base = strlen(owner->owner->download_server), a = strlen(game), b = strlen(path), c = strlen(ending);
    if (a > (SIZE_MAX - base - c - 2) / 3 || b > (SIZE_MAX - base - c - 2) / 3 - a) return NULL;
    char *out = qa_arena_alloc(&owner->storage, base + (a + b) * 3 + c + 2, 1, error);
    if (!out) return NULL;
    memcpy(out, owner->owner->download_server, base); size_t at = base;
    const char *parts[] = {game, path};
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 2; ++i) {
        if (i && *path) out[at++] = '/';
        for (const unsigned char *p = (const unsigned char *)parts[i]; *p; ++p) {
            unsigned char value = *p;
            if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') || strchr("-._~/", value)) out[at++] = (char)value;
            else { out[at++] = '%'; out[at++] = hex[value >> 4]; out[at++] = hex[value & 15]; }
        }
    }
    memcpy(out + at, ending, c + 1); return out;
}
static bool http_parent(remote_q2_http_downloads *owner, const char *path, qa_error *error)
{
    const char *slash = strrchr(path, '/');
    if (!slash) return true;
    size_t length = (size_t)(slash - path);
    char *directory = qa_arena_alloc(&owner->storage, length + 1, 1, error);
    if (!directory) return false;
    memcpy(directory, path, length); directory[length] = 0;
    return qa_fs_root_create_directory(http_destination(owner->owner, path), directory, error);
}
static bool http_asset_start(remote_q2_http_downloads *owner, remote_q2_http_asset *asset, qa_error *error)
{
    const char *path = http_asset_path(owner, asset); bool present = false, allowed = false;
    if (!http_asset_installed(owner, asset, &present, error)) return false;
    if (present) { asset->settled = true; return true; }
    if (!http_enabled(owner->owner)) { asset->settled = true; return true; }
    if (!owner->owner->options.download_allowed(owner->owner->options.context, path, &allowed, error)) return false;
    if (!allowed) { asset->settled = true; return true; }
    char *url = http_url(owner, http_game(owner->owner), path, "", error);
    qa_download_request request = {.path = path, .maximum_bytes = INT32_MAX};
    if (!url || !http_parent(owner, path, error) ||
        !qa_downloads_begin(owner->jobs, &request, url, &asset->job, error)) {
        http_note(owner->owner, path); asset->settled = true;
        if (error) *error = (qa_error){0};
    }
    return true;
}
static bool http_metadata_headers(void *context, qa_http_request_id id,
    const qa_http_response *response, qa_error *error)
{
    (void)context; (void)id;
    if (response->status == 200 || (response->status >= 300 && response->status < 400)) return true;
    return remote_q2_fail(error, QA_ERROR_IO, "Optional Q2 filelist is unavailable");
}
static bool http_metadata_body(void *context, qa_http_request_id id,
    const qa_http_response *response, qa_bytes bytes, qa_error *error)
{
    (void)id; remote_q2_http_downloads *owner = context;
    if (response->status != 200) return true;
    if (bytes.size > sizeof(owner->metadata) - owner->metadata_size)
        return remote_q2_fail(error, QA_ERROR_CAPACITY, "Q2 filelist exceeds its original 1 MiB limit");
    if (bytes.size) memcpy(owner->metadata + owner->metadata_size, bytes.data, bytes.size);
    owner->metadata_size += bytes.size; return true;
}
static void http_metadata_complete(void *context, qa_http_request_id id,
    const qa_http_response *response, const qa_error *error)
{
    (void)id; remote_q2_http_downloads *owner = context;
    owner->metadata_request = 0; owner->metadata_done = true;
    owner->metadata_success = (!error || error->code == QA_OK) && response->status == 200;
}
static bool http_metadata_parse(remote_q2_http_downloads *owner, qa_error *error)
{
    for (size_t at = 0; at < owner->metadata_size;) {
        size_t end = at;
        while (end < owner->metadata_size && owner->metadata[end] != '\n') ++end;
        size_t size = end - at;
        if (size && owner->metadata[at + size - 1] == '\r') --size;
        if (size && !memchr(owner->metadata + at, 0, size)) {
            bool game_local = owner->metadata[at] == '@';
            size_t begin = at + game_local, length = size - game_local;
            char *path = qa_arena_alloc(&owner->storage, length + 1, 1, error);
            if (!path) return false;
            memcpy(path, owner->metadata + begin, length); path[length] = 0;
            if (http_path_valid(path) && !(game_local && http_package(path))) {
                qa_error issue = {0}; char *normalized = qa_vfs_normalize_path(path, &issue);
                if (normalized) {
                    remote_q2_http_asset *asset;
                    bool ok = http_asset_admit(owner, normalized, game_local, &asset, error);
                    free(normalized); if (!ok) return false;
                }
            }
        }
        at = end + (end < owner->metadata_size);
    }
    return true;
}
static bool http_owner(frontend_remote_q2 *row, qa_error *error)
{
    if (row->download_http) return true;
    remote_q2_http_downloads *owner = calloc(1, sizeof(*owner));
    if (!owner) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 HTTP metadata");
    row->download_http = owner; owner->owner = row; owner->tail = &owner->first;
    qa_arena_init(&owner->storage, 0); owner->assets.storage = &owner->storage;
    qa_download_options options = {.jobs = 2, .maximum_pending_bytes = UINT64_C(2) * INT32_MAX,
        .same_origin_redirects = true,
        .hooks = {.context = row, .permit = http_permit, .inspect = http_inspect,
            .remount = http_remount, .target_root = http_target_root}};
    bool ok = qa_downloads_create(frontend_tools_http(row->frontend), row->content.selected_write_root,
        &options, &owner->jobs, error);
    if (!ok) http_clear(row);
    return ok;
}
static bool http_poll(remote_q2_http_downloads *owner, qa_error *error)
{
    if (!qa_downloads_pump(owner->jobs, error)) return false;
    for (remote_q2_http_asset *asset = owner->first; asset; asset = asset->next) if (asset->job) {
        qa_download_view view;
        if (!qa_downloads_view(owner->jobs, asset->job, &view)) return false;
        bool allowed = false;
        if (!owner->owner->options.download_allowed(owner->owner->options.context, view.path, &allowed, error)) return false;
        if ((!http_enabled(owner->owner) || !allowed) && view.state == QA_DOWNLOAD_RECEIVING) {
            qa_downloads_cancel(owner->jobs, asset->job);
            if (!qa_downloads_view(owner->jobs, asset->job, &view)) return false;
        }
        if (view.state == QA_DOWNLOAD_RECEIVING || view.state == QA_DOWNLOAD_INSTALLING || view.publication_pending) continue;
        if (view.state != QA_DOWNLOAD_COMPLETE) http_note(owner->owner, view.path);
        qa_downloads_release(owner->jobs, asset->job); asset->job = 0; asset->settled = true;
    }
    return true;
}
static bool http_manifest_seed(remote_q2_http_downloads *owner, const char *map, qa_error *error)
{
    remote_q2_http_asset *asset; frontend_remote_q2 *row = owner->owner;
    if (http_path_valid(map) && !http_asset_admit(owner, map, false, &asset, error)) return false;
    for (uint32_t i = 1; i < row->hud_names.layout.max_models; ++i) {
        const char *path = frontend_remote_q2_config(row, row->hud_names.layout.models + i);
        if (!*path || path[0] == '*' || path[0] == '#' || !http_path_valid(path)) continue;
        if (!http_asset_admit(owner, path, false, &asset, error)) return false;
    }
    for (uint32_t i = 1; i < row->hud_names.layout.max_sounds; ++i) {
        const char *name = frontend_remote_q2_config(row, row->hud_names.layout.sounds + i);
        if (!*name || name[0] == '*') continue;
        const char *path = name + (name[0] == '#');
        if (name[0] != '#') {
            size_t size = strlen(name);
            char *sound = qa_arena_alloc(&owner->storage, size + sizeof("sound/"), 1, error);
            if (!sound) return false;
            memcpy(sound, "sound/", sizeof("sound/") - 1);
            memcpy(sound + sizeof("sound/") - 1, name, size + 1); path = sound;
        }
        if (http_path_valid(path) && !http_asset_admit(owner, path, false, &asset, error)) return false;
    }
    owner->manifest_seeded = true; return true;
}
static bool http_prepare(frontend_remote_q2 *row, const char *map, bool *waiting, qa_error *error)
{
    *waiting = false;
    if (!row->download_http && !http_enabled(row)) return true;
    bool allowed = false;
    if (!row->options.download_allowed(row->options.context, "", &allowed, error)) return false;
    if (!row->download_http && !allowed) return true;
    if (!http_owner(row, error)) return false;
    remote_q2_http_downloads *owner = row->download_http;
    if (!http_poll(owner, error)) return false;
    if (owner->initial_done) return true;
    if (!http_enabled(row) || !allowed) {
        qa_http_cancel(frontend_tools_http(row->frontend), owner->metadata_request); owner->metadata_request = 0;
        owner->metadata_list = 2;
    }
    if (owner->metadata_request) { *waiting = true; return true; }
    if (owner->metadata_done) {
        if (owner->metadata_success && !http_metadata_parse(owner, error)) return false;
        owner->metadata_done = owner->metadata_success = false;
        owner->metadata_size = 0; ++owner->metadata_list;
    }
    if (owner->metadata_list < 2) {
        const char *path = map;
        if (owner->metadata_list && suffix(map, ".bsp")) {
            size_t size = strlen(map) - 4;
            char *stem = qa_arena_alloc(&owner->storage, size + 1, 1, error);
            if (!stem) return false;
            memcpy(stem, map, size); stem[size] = 0; path = stem;
        }
        char *url = http_url(owner, http_game(row), owner->metadata_list ? path : "", ".filelist", error);
        if (!url) return false;
        qa_http_request request = {.url = url, .method = "GET", .timeout_ms = 30000, .connect_timeout_ms = 10000,
            .maximum_response_bytes = Q2_HTTP_FILELIST_BYTES, .maximum_redirects = 5, .same_origin_redirects = true,
            .callbacks = {owner, http_metadata_headers, http_metadata_body, http_metadata_complete}};
        qa_error issue = {0};
        if (!qa_http_submit(frontend_tools_http(row->frontend), &request, &owner->metadata_request, &issue))
            owner->metadata_done = true;
        *waiting = true; return true;
    }
    if (!owner->manifest_seeded && !http_manifest_seed(owner, map, error)) return false;
    bool packages = false;
    for (remote_q2_http_asset *asset = owner->first; asset; asset = asset->next)
        if (asset->package && !asset->settled) { packages = true; break; }
    for (remote_q2_http_asset *asset = owner->first; asset; asset = asset->next) {
        if (asset->settled || asset->job || asset->package != packages) continue;
        size_t active = qa_downloads_count(owner->jobs);
        if (active >= 2 || (packages && active)) break;
        if (!http_asset_start(owner, asset, error)) return false;
        if (packages && asset->job) break;
    }
    for (remote_q2_http_asset *asset = owner->first; asset; asset = asset->next)
        if (!asset->settled) { *waiting = true; return true; }
    owner->initial_done = true; return true;
}
static bool http_request(frontend_remote_q2 *row, const char *path, bool *waiting, qa_error *error)
{
    *waiting = false;
    if (!http_enabled(row) || !row->download_http) return true;
    remote_q2_http_asset *asset;
    if (!http_asset_admit(row->download_http, path, false, &asset, error)) return false;
    if (!asset->settled && !asset->job && !http_asset_start(row->download_http, asset, error)) return false;
    *waiting = asset->job != 0; return true;
}
static void http_clear(frontend_remote_q2 *row)
{
    remote_q2_http_downloads *owner = row->download_http;
    if (!owner) return;
    qa_http_cancel(frontend_tools_http(row->frontend), owner->metadata_request);
    qa_downloads_destroy(owner->jobs);
    qa_id_index_destroy(&owner->assets); qa_arena_destroy(&owner->storage); free(owner);
    row->download_http = NULL;
}
bool frontend_remote_q2_download_server(frontend_remote_q2 *row, const char *url, qa_error *error)
{
    if (!row || !url || strlen(url) >= sizeof(row->download_server))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 HTTP address exceeds the negotiated handshake extent");
    if (strcmp(row->download_server, url)) { http_clear(row); strcpy(row->download_server, url); }
    row->http_downloads = qa_cvars_resolve(row->options.domain.cvars, "cl_http_downloads");
    return true;
}
static void block_clear(frontend_remote_q2 *row)
{
    qa_buffer_free(&row->download_block);
    row->download_block_offset = row->download_block_cursor = 0;
    row->download_block_percent = 0; row->download_block_pending = row->download_block_committed = false;
    row->download_remembered = row->download_sealed = row->download_published = row->download_refresh_pending = row->download_refreshed = false;
}
static bool stage_clear(frontend_remote_q2 *row, qa_error *error)
{
    if (!qa_fs_stage_close_checked(&row->download_stage, false, error)) return false;
    qa_fs_root_close(row->download_root); row->download_root = NULL;
    free(row->download_path); row->download_path = NULL; row->download_bytes = 0; row->download_percent = 0;
    row->download_nonce = row->download_logical_nonce = 0;
    row->download_stage_sealed = row->download_stage_published = false;
    row->download_identity = (qa_fs_identity){0}; return true;
}
static bool pending_clear(frontend_remote_q2 *row, qa_error *error)
{ if (!stage_clear(row, error)) return false; block_clear(row); return true; }
bool remote_q2_download_clear(frontend_remote_q2 *row, qa_error *error)
{
    http_clear(row);
    if (!pending_clear(row, error)) return false;
    for (size_t i = 0; i < row->download_attempted_count; ++i) free(row->download_attempted[i]);
    free(row->download_attempted); row->download_attempted = NULL; row->download_attempted_count = 0; return true;
}
static bool request(frontend_remote_q2 *row, const char *path, bool *waiting, qa_error *error)
{
    *waiting = false;
    if (!path || !*path) return true;
    char *normalized = qa_archive_normalize_path(path, error);
    if (!normalized) return false;
    if (!strchr(normalized, '/') || normalized[0] == '.' || !native_asset(normalized)) {
        free(normalized); return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 download path cannot be represented by the native command");
    }
    if (!remote_q2_download_destination(row, normalized)) {
        free(normalized); return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 material download lacks its actual negotiated capability and write root");
    }
    bool tried = attempted(row, normalized);
    bool companion = (prefix(normalized, "models/qa/") || prefix(normalized, "players/qa/")) &&
        suffix(normalized, ".qpm");
    if (!companion || tried) {
        qa_resource *present = NULL; qa_error issue = {0};
        if (qa_vfs_acquire(row->content.mounts, normalized, &present, NULL, &issue)) {
            qa_resource_release(present); free(normalized); return true;
        }
        if (issue.code != QA_ERROR_NOT_FOUND) { free(normalized); if (error) *error = issue; return false; }
    }
    if (tried) { free(normalized); return true; }
    bool allowed = false;
    if (!row->options.download_allowed(row->options.context, normalized, &allowed, error) || !remote_q2_live(row, error)) {
        free(normalized); return false;
    }
    if (!allowed) { free(normalized); return true; }
    if (!http_request(row, normalized, waiting, error) || *waiting) {
        free(normalized); return *waiting;
    }
    qa_fs_root *root = remote_q2_download_destination(row, normalized);
    if (!root) { free(normalized); return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 download lacks its actual selected/base write capability"); }
    const char *slash = strrchr(normalized, '/'); size_t parent = (size_t)(slash - normalized);
    char *directory = malloc(parent + 1);
    if (!directory) { free(normalized); return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 download parent"); }
    memcpy(directory, normalized, parent); directory[parent] = 0;
    bool ok = qa_fs_root_create_directory(root, directory, error); free(directory);
    uint64_t initial = 0;
    if (ok) ok = row->options.download_stage(row->options.context, root, normalized,
        &row->download_stage, &row->download_nonce, error) && row->download_stage &&
        row->download_nonce && remote_q2_live(row, error) &&
        qa_fs_stage_size(row->download_stage, &initial, error) && !initial;
    if (ok) {
        row->download_logical_nonce = row->download_nonce;
        qa_fs_root_retain(root); row->download_root = root; row->download_path = normalized;
        normalized = NULL; row->download_bytes = initial;
        size_t size = strlen(row->download_path) + 11; char *command = malloc(size);
        if (!command) ok = remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 download command");
        else {
            snprintf(command, size, "download %s", row->download_path);
            ok = qa_network_q2_client_command(row->options.domain.runtime, row->options.domain.client, command, 0, error);
            free(command);
        }
    }
    free(normalized);
    if (!ok) { (void)pending_clear(row, error); return false; }
    *waiting = true; return true;
}
static bool path_join(frontend_remote_q2 *row, const char *prefix, const char *name,
    const char *suffix, bool *waiting, qa_error *error)
{
    size_t a = strlen(prefix), b = strlen(name), c = strlen(suffix);
    if (a > SIZE_MAX - b - c - 1) return false;
    char *path = malloc(a + b + c + 1);
    if (!path) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 resource path");
    snprintf(path, a + b + c + 1, "%s%s%s", prefix, name, suffix);
    bool ok = request(row, path, waiting, error); free(path); return ok;
}
typedef struct material_download {
    frontend_remote_q2 *row;
    qa_game_family family;
    bool waiting;
} material_download;
static bool material_dependency(void *context, const qa_q2_material_dependency *dependency, qa_error *error)
{
    material_download *state = context;
    if (state->waiting) return true;
    if (qa_q2_material_dependency_builtin(dependency)) return true;
    if (dependency->kind != QA_Q2_MATERIAL_SKY)
        return request(state->row, dependency->path, &state->waiting, error);
    if (!strcmp(dependency->path, "-")) return true;
    static const char *faces[] = {"rt", "bk", "lf", "ft", "up", "dn"};
    static const char *q1[] = {".tga", ".lmp", ".jpg", ".png", ".jpeg", ".pcx", ".bmp", ".gif"};
    static const char *q2[] = {".tga", ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".pcx"};
    static const char *q3[] = {".tga", ".jpg", ".png", ".jpeg", ".pcx", ".bmp", ".gif"};
    const char *const *extensions = state->family == QA_GAME_Q1 ? q1 : state->family == QA_GAME_Q2 ? q2 : q3;
    size_t count = state->family == QA_GAME_Q1 ? 8 : 7;
    for (size_t i = 0; i < 6 && !state->waiting; ++i) {
        bool present = false;
        for (size_t j = 0; j < count && !present; ++j) {
            size_t length = strlen(dependency->path) + strlen(extensions[j]) + 4;
            char *path = malloc(length);
            if (!path) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining actual material sky dependency");
            snprintf(path, length, "%s_%s%s", dependency->path, faces[i], extensions[j]);
            bool ok = installed(state->row, path, &present, error);
            free(path); if (!ok) return false;
        }
        if (present) continue;
        for (size_t j = 0; j < count && !state->waiting; ++j) {
            size_t length = strlen(dependency->path) + strlen(extensions[j]) + 4;
            char *path = malloc(length);
            if (!path) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining material sky transfer path");
            snprintf(path, length, "%s_%s%s", dependency->path, faces[i], extensions[j]);
            bool ok = request(state->row, path, &state->waiting, error);
            free(path); if (!ok) return false;
        }
    }
    return true;
}
static bool model_materials(frontend_remote_q2 *row, const qa_model *model, bool *waiting, qa_error *error)
{
    if (model->format != QA_MODEL_MD3) return true;
    for (size_t i = 0; i < model->mesh_count && !*waiting; ++i)
        for (size_t j = 0; j < model->meshes[i].shader_count && !*waiting; ++j) {
            const char *name = model->meshes[i].shaders[j].name;
            if (!*name) continue;
            if (!request(row, name, waiting, error) || *waiting) return *waiting;
            bool image_receipt = suffix(name, ".qai");
            if (!suffix(name, ".shader") && !image_receipt) continue;
            if (!row->options.material_scripts)
                return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 MD3 shader requires its negotiated full material capability");
            qa_resource *script = NULL;
            if (!qa_vfs_acquire(row->content.mounts, name, &script, NULL, error)) return false;
            material_download state = {.row = row};
            bool ok;
            if (image_receipt) ok = qa_q2_material_image_dependencies(
                qa_resource_bytes(script), material_dependency, &state, error);
            else {
                qa_q2_material_scope scope;
                ok = qa_q2_material_script_scope(qa_resource_bytes(script), &scope, error);
                if (ok) { state.family = scope.family; ok = qa_q2_material_script_dependencies(
                    qa_resource_bytes(script), material_dependency, &state, error); }
            }
            qa_resource_release(script); *waiting = state.waiting;
            if (!ok) return false;
        }
    return true;
}
static bool model_palette(frontend_remote_q2 *row, const char *path, qa_bytes model, bool *waiting, qa_error *error)
{
    if (!remote_q2_model_scope_required(path, model)) return true;
    char companion[1024];
    if (!row->options.material_scripts)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 indexed model requires its negotiated Source palette companion");
    if (!qa_q2_material_model_scope_path(path, companion, error)) return false;
    if (!request(row, companion, waiting, error) || *waiting) return *waiting;
    qa_resource *artifact = NULL, *palette = NULL;
    qa_q2_material_model_scope scope; qa_scene_image_options options;
    material_download state = {.row = row};
    bool ok = qa_vfs_acquire(row->content.mounts, companion, &artifact, NULL, error) &&
        qa_q2_material_model_scope_read(qa_resource_bytes(artifact), path, &scope, error);
    if (ok) {
        qa_q2_material_dependency dependency = {.kind = QA_Q2_MATERIAL_IMAGE, .path = scope.palette_alias};
        ok = material_dependency(&state, &dependency, error);
    }
    *waiting = state.waiting;
    if (ok && !*waiting) ok = qa_vfs_acquire(row->content.mounts, scope.palette_alias, &palette, NULL, error) &&
        qa_q2_material_model_scope_apply(&scope, qa_resource_bytes(palette), &options, error);
    qa_resource_release(artifact); qa_resource_release(palette);
    return ok;
}
bool remote_q2_download_prepare(frontend_remote_q2 *row, qa_q2_preparation *result, qa_error *error)
{
    *result = QA_Q2_PREPARATION_RECEIVING;
    if (row->download_stage || row->download_refresh_pending) return true;
    bool waiting = false;
    const char *map = frontend_remote_q2_config(row, row->hud_names.layout.models + 1);
    if (!*map) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 server supplied no map download identity");
    if (!http_prepare(row, map, &waiting, error)) return false;
    if (waiting) { *result = QA_Q2_PREPARATION_WAITING; return true; }
    if (!request(row, map, &waiting, error) || waiting) return waiting;
    qa_resource *map_resource = NULL; qa_bsp_view map_view;
    bool admitted = qa_vfs_acquire(row->content.mounts, map, &map_resource, NULL, error) &&
        remote_q2_map_validate(row, map_resource, &map_view, error);
    qa_resource_release(map_resource);
    if (!admitted) return false;
    for (size_t i = 1; i < row->hud_names.layout.max_models; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->hud_names.layout.models + i));
        if (!*name || name[0] == '*' || name[0] == '#' || !strcmp(name, map)) continue;
        if (!request(row, name, &waiting, error) || waiting) return waiting;
        qa_resource *resource = NULL; qa_error issue = {0};
        if (!qa_vfs_acquire(row->content.mounts, name, &resource, NULL, &issue)) {
            if (issue.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = issue;
            return false;
        }
        qa_model model = {0}; bool ok = qa_model_load(qa_resource_bytes(resource), &model, error);
        if (ok) ok = model_palette(row, name, qa_resource_bytes(resource), &waiting, error);
        if (ok && !waiting) ok = model_materials(row, &model, &waiting, error);
        if (ok) for (size_t j = 0; j < model.skin_count && !waiting; ++j)
            if (*model.skins[j].name && !request(row, model.skins[j].name, &waiting, error)) { ok = false; break; }
        if (ok) for (size_t j = 0; j < model.sprite_count && !waiting; ++j)
            if (*model.sprites[j].image && !request(row, model.sprites[j].image, &waiting, error)) { ok = false; break; }
        qa_model_free(&model); qa_resource_release(resource);
        if (!ok || waiting) return ok;
    }
    for (size_t i = 1; i < row->hud_names.layout.max_sounds; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->hud_names.layout.sounds + i));
        if (!*name || name[0] == '*') continue;
        if (!path_join(row, name[0] == '#' ? "" : "sound/", name[0] == '#' ? name + 1 : name, "", &waiting, error) || waiting) return waiting;
    }
    for (size_t i = 1; i < row->hud_names.layout.max_images; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->hud_names.layout.images + i));
        if (!*name) continue;
        char path[4096];
        if (!qa_scene_q2_picture_path(name,remote_q2_rerelease_presentation(row),path,sizeof(path),error) ||
            !path_join(row,"",path,"",&waiting,error) || waiting) return waiting;
    }
    for (size_t i = 0; i < 256; ++i) {
        const char *value = frontend_remote_q2_config(row, (uint16_t)(row->hud_names.layout.player_skins + i));
        if (!*value) continue;
        qa_hud_q2_player_names names = qa_hud_q2_player_names_read(value,remote_q2_rerelease_presentation(row));
        char path[4096];
        const char *fixed[] = {"tris.md2", "weapon.md2", "weapon.pcx"}; bool ok = true;
        for (size_t j = 0; ok && j < 3 && !waiting; ++j)
            ok = qa_hud_q2_player_model_path(names.model, (qa_bytes){(const uint8_t *)fixed[j], strlen(fixed[j])},
                "", path, sizeof(path), error) && path_join(row, "", path, "", &waiting, error);
        const char *suffixes[] = {".pcx", "_i.pcx"};
        for (size_t j = 0; ok && j < 2 && !waiting; ++j)
            ok = qa_hud_q2_player_model_path(names.model, names.skin, suffixes[j], path, sizeof(path), error) &&
                path_join(row, "", path, "", &waiting, error);
        if (!ok || waiting) return ok;
    }
    const char *sky = frontend_remote_q2_config(row, 2);
    static const char *sides[] = {"rt", "bk", "lf", "ft", "up", "dn"};
    if (*sky) for (size_t i = 0; i < 6; ++i) {
        size_t length = strlen(sky) + 12;
        char *tga = malloc(length), *pcx = malloc(length);
        if (!tga || !pcx) { free(tga); free(pcx); return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 sky fallback paths"); }
        snprintf(tga, length, "env/%s%s.tga", sky, sides[i]);
        snprintf(pcx, length, "env/%s%s.pcx", sky, sides[i]);
        bool has_tga = false, has_pcx = false;
        bool ok = installed(row, tga, &has_tga, error) && installed(row, pcx, &has_pcx, error);
        if (ok && !has_tga && !has_pcx) {
            ok = request(row, tga, &waiting, error);
            if (ok && !waiting) ok = request(row, pcx, &waiting, error);
        }
        free(tga); free(pcx); if (!ok || waiting) return ok;
    }
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(row->content.mounts, map, &resource, NULL, error)) return false;
    qa_bsp_view bsp; bool ok = qa_bsp_open(qa_resource_bytes(resource), &bsp, error) && bsp.family == QA_BSP_Q2;
    for (size_t i = 0; ok && i < qa_bsp_record_count(&bsp, QA_BSP_TEXINFO) && !waiting; ++i) {
        qa_bsp_texinfo info;
        ok = qa_bsp_read_texinfo(&bsp, i, &info, error);
        if (!ok) break;
        char *name = malloc(info.name.size + 1);
        if (!name) { ok = remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 BSP texture name"); break; }
        memcpy(name, info.name.data, info.name.size); name[info.name.size] = 0;
        ok = path_join(row, "textures/", name, ".wal", &waiting, error); free(name);
    }
    qa_resource_release(resource); if (!ok || waiting) return ok;
    *result = QA_Q2_PREPARATION_READY; return true;
}
static bool refresh(frontend_remote_q2 *row, qa_error *error)
{
    const qa_product *base = qa_catalog_product(row->content.catalog, row->content.base);
    if (!base || row->content_generation == UINT64_MAX) return false;
    qa_catalog *fresh = NULL; qa_product_id selected = 0; qa_vfs *mounts = NULL;
    if (!qa_catalog_discover_remote_q2(row->content.catalog, base->id, row->data.gamedir,
        row->content_generation + 1, &fresh, &selected, error)) return false;
    const qa_product *new_base = qa_catalog_find(fresh, base->key);
    if (!new_base || !qa_catalog_open(fresh, selected, &mounts, error) || !remote_q2_live(row, error)) {
        qa_vfs_destroy(mounts); qa_catalog_release(fresh); return false;
    }
    qa_vfs_destroy(row->content.mounts); qa_catalog_release(row->content.catalog);
    row->content = (frontend_remote_q2_content){fresh, selected, new_base->id, mounts,
        qa_catalog_product_write_root(fresh, selected), qa_catalog_product_write_root(fresh, new_base->id)};
    ++row->content_generation; row->content_admitted = false; return true;
}
static bool publication_continue(frontend_remote_q2 *row, qa_error *error)
{
    if (!row->download_stage_sealed) {
        if (!qa_fs_stage_seal(row->download_stage, &row->download_identity, error)) return false;
        row->download_stage_sealed = row->download_sealed = true;
    }
    if (!row->download_stage_published) {
        bool created = false;
        bool published = qa_fs_stage_publish(row->download_stage, &row->download_identity, true, &created, error);
        if (created) row->download_published = true;
        if (!published) return false;
        if (!created) return remote_q2_fail(error, QA_ERROR_IO,
            "Q2 download target appeared before exclusive publication");
        row->download_stage_published = row->download_published = true;
    }
    return true;
}
bool remote_q2_download_receive(frontend_remote_q2 *row, const qa_q2_server_event *event,
    bool *complete, qa_error *error)
{
    if (!event || event->kind != QA_Q2_SVC_DOWNLOAD || !complete) return false;
    *complete = false;
    if (!row->download_stage && !row->download_refresh_pending) return true;
    qa_bytes bytes = event->data.download.bytes;
    uint8_t percent = event->data.download.percent;
    if (row->download_block_pending) {
        if (event->data.download.missing || percent != row->download_block_percent ||
            bytes.size != row->download_block.size || (bytes.size &&
                (!bytes.data || memcmp(bytes.data, row->download_block.data, bytes.size))))
            return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 retry changed its retained native download block");
    }
    if (row->download_refresh_pending) {
        if (!publication_continue(row, error)) return false;
        if (!row->download_refreshed) {
            if (!refresh(row, error)) return false;
            row->download_refreshed = true;
        }
        if (!pending_clear(row, error)) return false;
        *complete = true; return true;
    }
    bool allowed = false;
    if (!row->options.download_allowed(row->options.context, row->download_path, &allowed, error) || !remote_q2_live(row, error)) return false;
    if ((!allowed || event->data.download.missing) && !row->download_block_pending) {
        if (!remember(row, row->download_path, error)) return false;
        if (!pending_clear(row, error)) return false;
        *complete = true; return true;
    }
    if (!row->download_block_pending) {
        if (percent < row->download_percent || percent > 100 || bytes.size > INT32_MAX - row->download_bytes ||
            (bytes.size && !bytes.data) || (!bytes.size && row->download_bytes && percent != 100))
            return remote_q2_fail(error, QA_ERROR_FORMAT, "Invalid native Q2 download progress");
        row->download_block.data = bytes.size ? malloc(bytes.size) : NULL;
        if (bytes.size && !row->download_block.data)
            return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining accepted Q2 download block");
        if (bytes.size) memcpy(row->download_block.data, bytes.data, bytes.size);
        row->download_block.size = bytes.size; row->download_block_offset = row->download_bytes;
        row->download_block_percent = percent; row->download_block_pending = true;
    }
    if (!row->download_block_committed) {
        size_t cursor = (size_t)row->download_block_cursor, written = 0;
        qa_bytes remaining = {row->download_block.size ? row->download_block.data + cursor : NULL,
            row->download_block.size - cursor};
        bool okay = qa_fs_stage_write(row->download_stage, row->download_block_offset + cursor, remaining, &written, error);
        row->download_block_cursor += written;
        if (!okay || written != remaining.size) return false;
        row->download_bytes = row->download_block_offset + row->download_block_cursor;
        row->download_percent = percent; row->download_block_committed = true;
    }
    if (percent == 100 || (!bytes.size && !row->download_bytes)) {
        if (!row->download_remembered) {
            if (!remember(row, row->download_path, error)) return false;
            row->download_remembered = true;
        }
        if (!publication_continue(row, error)) return false;
        row->download_refresh_pending = true;
        if (!refresh(row, error)) return false;
        row->download_refreshed = true;
        if (!pending_clear(row, error)) return false;
        *complete = true; return true;
    }
    if (!qa_network_q2_client_command(row->options.domain.runtime, row->options.domain.client, "nextdl", 0, error)) return false;
    block_clear(row); return true;
}
