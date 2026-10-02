#include "remote_q2_private.h"
#include "qa/archive.h"
#include "qa/network_q2_materials.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
            (suffix(path, ".mdl") || suffix(path, ".md2") || suffix(path, ".md3") ||
                suffix(path, ".sp2") || model_image(path) || suffix(path, ".wav") ||
                suffix(path, ".shader") || suffix(path, ".lmp") || suffix(path, ".wal") ||
                suffix(path, ".roq") || suffix(path, ".cin") || suffix(path, ".ogv"))) ||
        (prefix(path, "sound/") && suffix(path, ".wav")) ||
        (prefix(path, "pics/") && suffix(path, ".pcx")) ||
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
    (row->options.material_scripts || !(suffix(path, ".shader") || suffix(path, ".lmp") ||
        suffix(path, ".roq") || suffix(path, ".cin") || suffix(path, ".ogv") ||
        ((prefix(path, "models/") || prefix(path, "players/")) && suffix(path, ".wal")))) ?
    (prefix(path, "players/") ? row->content.base_write_root : row->content.selected_write_root) : NULL; }

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
static void pending_clear(frontend_remote_q2 *row)
{
    qa_fs_stage_close(row->download_stage, false); row->download_stage = NULL;
    qa_fs_root_close(row->download_root); row->download_root = NULL;
    free(row->download_path); row->download_path = NULL; row->download_bytes = 0; row->download_percent = 0;
    row->download_nonce = row->download_logical_nonce = 0;
}
void remote_q2_download_clear(frontend_remote_q2 *row)
{
    pending_clear(row);
    for (size_t i = 0; i < row->download_attempted_count; ++i) free(row->download_attempted[i]);
    free(row->download_attempted); row->download_attempted = NULL; row->download_attempted_count = 0;
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
    qa_resource *present = NULL; qa_error issue = {0};
    if (qa_vfs_acquire(row->content.mounts, normalized, &present, NULL, &issue)) {
        qa_resource_release(present); free(normalized); return true;
    }
    if (issue.code != QA_ERROR_NOT_FOUND) { free(normalized); if (error) *error = issue; return false; }
    if (attempted(row, normalized)) { free(normalized); return true; }
    bool allowed = false;
    if (!row->options.download_allowed(row->options.context, normalized, &allowed, error) || !remote_q2_live(row, error)) {
        free(normalized); return false;
    }
    if (!allowed) { free(normalized); return true; }
    qa_fs_root *root = remote_q2_download_destination(row, normalized);
    if (!root) { free(normalized); return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 download lacks its actual selected/base write capability"); }
    const char *slash = strrchr(normalized, '/'); size_t parent = (size_t)(slash - normalized);
    char *directory = malloc(parent + 1);
    if (!directory) { free(normalized); return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 download parent"); }
    memcpy(directory, normalized, parent); directory[parent] = 0;
    bool ok = qa_fs_root_create_directory(root, directory, error); free(directory);
    uint64_t initial = 0;
    if (ok) ok = row->options.download_nonce(row->options.context, &row->download_nonce, error) &&
        row->download_nonce && remote_q2_live(row, error);
    if (ok) ok = qa_fs_stage_open(root, normalized, row->download_nonce, false,
        &row->download_stage, &initial, error);
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
    if (!ok) { pending_clear(row); return false; }
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
    qa_scene_family family;
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
    const char *const *extensions = state->family == QA_SCENE_Q1 ? q1 : state->family == QA_SCENE_Q2 ? q2 : q3;
    size_t count = state->family == QA_SCENE_Q1 ? 8 : 7;
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
            if (!suffix(name, ".shader")) continue;
            if (!row->options.material_scripts)
                return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 MD3 shader requires its negotiated full material capability");
            qa_resource *script = NULL;
            if (!qa_vfs_acquire(row->content.mounts, name, &script, NULL, error)) return false;
            qa_q2_material_scope scope;
            material_download state = {.row = row};
            bool ok = qa_q2_material_script_scope(qa_resource_bytes(script), &scope, error);
            if (ok) { state.family = scope.family; ok = qa_q2_material_script_dependencies(
                qa_resource_bytes(script), material_dependency, &state, error); }
            qa_resource_release(script); *waiting = state.waiting;
            if (!ok) return false;
        }
    return true;
}
bool remote_q2_download_prepare(frontend_remote_q2 *row, qa_q2_preparation *result, qa_error *error)
{
    *result = QA_Q2_PREPARATION_RECEIVING;
    if (row->download_stage) return true;
    bool waiting = false;
    const char *map = frontend_remote_q2_config(row, row->layout.models + 1);
    if (!*map) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 server supplied no map download identity");
    if (!request(row, map, &waiting, error) || waiting) return waiting;
    qa_resource *map_resource = NULL; qa_bsp_view map_view;
    bool admitted = qa_vfs_acquire(row->content.mounts, map, &map_resource, NULL, error) &&
        remote_q2_map_validate(row, map_resource, &map_view, error);
    qa_resource_release(map_resource);
    if (!admitted) return false;
    for (size_t i = 1; i < row->layout.max_models; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.models + i));
        if (!*name || name[0] == '*' || name[0] == '#' || !strcmp(name, map)) continue;
        if (!request(row, name, &waiting, error) || waiting) return waiting;
        qa_resource *resource = NULL; qa_error issue = {0};
        if (!qa_vfs_acquire(row->content.mounts, name, &resource, NULL, &issue)) {
            if (issue.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = issue;
            return false;
        }
        qa_model model = {0}; bool ok = qa_model_load(qa_resource_bytes(resource), &model, error);
        if (ok) ok = model_materials(row, &model, &waiting, error);
        if (ok) for (size_t j = 0; j < model.skin_count && !waiting; ++j)
            if (*model.skins[j].name && !request(row, model.skins[j].name, &waiting, error)) { ok = false; break; }
        if (ok) for (size_t j = 0; j < model.sprite_count && !waiting; ++j)
            if (*model.sprites[j].image && !request(row, model.sprites[j].image, &waiting, error)) { ok = false; break; }
        qa_model_free(&model); qa_resource_release(resource);
        if (!ok || waiting) return ok;
    }
    for (size_t i = 1; i < row->layout.max_sounds; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.sounds + i));
        if (!*name || name[0] == '*') continue;
        if (!path_join(row, name[0] == '#' ? "" : "sound/", name[0] == '#' ? name + 1 : name, "", &waiting, error) || waiting) return waiting;
    }
    for (size_t i = 1; i < row->layout.max_images; ++i) {
        const char *name = frontend_remote_q2_config(row, (uint16_t)(row->layout.images + i));
        if (!*name) continue;
        if (!path_join(row, name[0] == '/' ? "" : "pics/", name[0] == '/' ? name + 1 : name,
            name[0] == '/' ? "" : ".pcx", &waiting, error) || waiting) return waiting;
    }
    for (size_t i = 0; i < 256; ++i) {
        const char *value = frontend_remote_q2_config(row, (uint16_t)(row->layout.players + i));
        const char *appearance = strchr(value, '\\'); appearance = appearance ? appearance + 1 : value;
        const char *slash = strchr(appearance, '/');
        if (!slash || slash == appearance || !slash[1]) continue;
        size_t length = (size_t)(slash - appearance); char *prefix = malloc(length + 10);
        if (!prefix) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 player download path");
        snprintf(prefix, length + 10, "players/%.*s/", (int)length, appearance);
        const char *fixed[] = {"tris.md2", "weapon.md2", "weapon.pcx"}; bool ok = true;
        for (size_t j = 0; ok && j < 3 && !waiting; ++j) ok = path_join(row, prefix, fixed[j], "", &waiting, error);
        if (ok && !waiting) ok = path_join(row, prefix, slash + 1, ".pcx", &waiting, error);
        if (ok && !waiting) ok = path_join(row, prefix, slash + 1, "_i.pcx", &waiting, error);
        free(prefix); if (!ok || waiting) return ok;
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
bool remote_q2_download_receive(frontend_remote_q2 *row, const qa_q2_server_event *event,
    bool *complete, qa_error *error)
{
    if (!event || event->kind != QA_Q2_SVC_DOWNLOAD || !complete) return false;
    *complete = false; if (!row->download_stage) return true;
    bool allowed = false;
    if (!row->options.download_allowed(row->options.context, row->download_path, &allowed, error) || !remote_q2_live(row, error)) return false;
    if (!allowed || event->data.download.missing) {
        if (!remember(row, row->download_path, error)) return false;
        pending_clear(row); *complete = true; return true;
    }
    qa_bytes bytes = event->data.download.bytes;
    uint8_t percent = event->data.download.percent;
    if (percent < row->download_percent || percent > 100 || bytes.size > INT32_MAX - row->download_bytes)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Invalid native Q2 download progress");
    size_t written = 0;
    if (!qa_fs_stage_write(row->download_stage, row->download_bytes, bytes, &written, error) || written != bytes.size) return false;
    row->download_bytes += written; row->download_percent = percent;
    if (percent == 100 || (!bytes.size && !row->download_bytes)) {
        qa_fs_identity identity; bool created = false;
        if (!qa_fs_stage_seal(row->download_stage, &identity, error) ||
            !qa_fs_stage_publish(row->download_stage, &identity, true, &created, error)) return false;
        if (!remember(row, row->download_path, error)) return false;
        pending_clear(row);
        if (!refresh(row, error)) return false;
        *complete = true; return true;
    }
    if (!bytes.size) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 download made no progress");
    return qa_network_q2_client_command(row->options.domain.runtime, row->options.domain.client, "nextdl", 0, error);
}
