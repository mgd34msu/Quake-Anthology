#include "network_content_q3.h"
#include "qa/network_q3_pak_role.h"
#include "qa/vfs_view_save.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct content_mount {
    qa_mount_id id;
    qa_product_id product;
    qa_q3_pak_entry pack;
    char *game, *basename;
} content_mount;
struct frontend_q3_content {
    qa_application *application;
    qa_launch_instance_lease *descriptor;
    qa_catalog *catalog;
    qa_application_q3_client_context receiver;
    uint64_t configuration_generation, connection_epoch;
    void *connection;
    bool (*connection_current)(void *, uint64_t, qa_error *);
    frontend_q3_content_phase phase;
    qa_product_id selected, base;
    qa_vfs *mounts;
    qa_q3_gamestate *gamestate;
    qa_resource *map;
    qa_vfs_acquisition map_acquisition;
    char map_path[1024];
    char *selected_directory, *base_directory;
    char *selected_write_path, *base_write_path;
    qa_fs_root *selected_write_root, *base_write_root;
    content_mount *inventory;
    size_t mount_count, pack_count, referenced_count;
    const qa_q3_pak_entry **packs;
    uint32_t *loaded_checksums;
    qa_q3_package *referenced;
    qa_q3_server_pak_set *loaded_server, *referenced_server;
    qa_q3_pak_references *references;
    int32_t server_id;
    bool pure;
};
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static char *copy(const char *text, qa_error *error)
{
    size_t length = strlen(text); char *out = malloc(length + 1);
    if (!out) { fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 content identity"); return NULL; }
    memcpy(out, text, length + 1); return out;
}
static unsigned lower(unsigned ch) { return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch; }
static bool equal(const char *left, const char *right)
{
    while (*left && *right) if (lower((unsigned char)*left++) != lower((unsigned char)*right++)) return false;
    return !*left && !*right;
}
static const char *last_component(const char *path)
{ const char *slash = strrchr(path, '/'); return slash ? slash + 1 : path; }
static int32_t number(const char *text)
{
    char *end; long long value = strtoll(text, &end, 10);
    if (end == text) return 0;
    return value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (int32_t)value;
}
static size_t number_space(const unsigned char *text)
{
    if (*text == ' ' || (*text >= '\t' && *text <= '\r')) return 1;
    if (text[0] == 0xc2 && text[1] == 0xa0) return 2;
    if (text[0] == 0xe1 && text[1] && text[2] && text[1] == 0x9a && text[2] == 0x80) return 3;
    if (text[0] == 0xe2 && text[1] && text[2] &&
        ((text[1] == 0x80 && ((text[2] >= 0x80 && text[2] <= 0x8a) || text[2] == 0xa8 || text[2] == 0xa9 || text[2] == 0xaf)) ||
         (text[1] == 0x81 && text[2] == 0x9f))) return 3;
    if (text[0] == 0xe3 && text[1] && text[2] && text[1] == 0x80 && text[2] == 0x80) return 3;
    if (text[0] == 0xef && text[1] && text[2] && text[1] == 0xbb && text[2] == 0xbf) return 3;
    return 0;
}
static bool game_type(const char *text, int32_t *out)
{
    const unsigned char *start = (const unsigned char *)text, *end = start + strlen(text);
    size_t space;
    while ((space = number_space(start)) != 0) start += space;
    while (end > start) {
        size_t width = 0;
        for (size_t i = 1; i <= 3 && i <= (size_t)(end - start); ++i)
            if (number_space(end - i) == i) { width = i; break; }
        if (!width) break;
        end -= width;
    }
    if (start == end) { *out = 0; return true; }
    if (end - start >= 2 && start[0] == '0' &&
        (lower(start[1]) == 'x' || lower(start[1]) == 'b' || lower(start[1]) == 'o')) {
        unsigned radix = lower(start[1]) == 'x' ? 16 : lower(start[1]) == 'b' ? 2 : 8;
        unsigned value = 0; const unsigned char *cursor = start + 2;
        if (cursor == end) return false;
        for (; cursor < end; ++cursor) {
            unsigned digit = *cursor >= '0' && *cursor <= '9' ? *cursor - '0' :
                lower(*cursor) >= 'a' && lower(*cursor) <= 'f' ? lower(*cursor) - 'a' + 10 : radix;
            if (digit >= radix || value > 4 || digit > 4 || value * radix + digit > 4) return false;
            value = value * radix + digit;
        }
        *out = (int32_t)value; return true;
    }
    const unsigned char *cursor = start;
    if (*cursor == '+' || *cursor == '-') ++cursor;
    bool digits = false;
    while (cursor < end && *cursor >= '0' && *cursor <= '9') { digits = true; ++cursor; }
    if (cursor < end && *cursor == '.') {
        ++cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') { digits = true; ++cursor; }
    }
    if (!digits) return false;
    if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        if (cursor < end && (*cursor == '+' || *cursor == '-')) ++cursor;
        const unsigned char *exponent = cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9') ++cursor;
        if (cursor == exponent) return false;
    }
    if (cursor != end) return false;
    char *parsed; double value = strtod((const char *)start, &parsed);
    if (parsed != (const char *)end || !isfinite(value) || value < 0 || value > 4 || value != (double)(int32_t)value) return false;
    *out = (int32_t)value; return true;
}
static double loose_random(void *context) { (void)context; return 1; }
static bool matching_view(frontend_q3_content *, const qa_vfs *, qa_error *);
static bool current(const frontend_q3_content *content, qa_error *error)
{
    return (content && content->application && content->descriptor &&
        content->configuration_generation == qa_application_configuration_generation(content->application) &&
        qa_application_q3_remote_context_current(content->application, &content->receiver) &&
        content->connection_current(content->connection, content->connection_epoch, error)) ||
        fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content lost its actual receiver and configuration generation");
}
static bool owns_mount(const qa_catalog *catalog, qa_product_id product,
    const qa_vfs *view, const qa_vfs_mount_info *info)
{
    const qa_mount_id *owned; size_t count;
    if (!qa_catalog_product_own_mounts(catalog, product, &owned, &count)) return false;
    const char *path = qa_vfs_mount_path(view, info->id);
    for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < qa_catalog_mount_count(catalog); ++j) {
        const qa_catalog_mount *mount = qa_catalog_mount_at(catalog, j);
        if (mount->id == owned[i] && path && !strcmp(path, mount->path) &&
            mount->format == info->format && mount->writable == info->writable &&
            (!info->is_archive || (mount->digest && info->digest && qa_sha256_equal(mount->digest, info->digest)))) return true;
    }
    return false;
}
static bool identify_mount(frontend_q3_content *content, const qa_vfs_mount_info *info,
    qa_product_id *out, qa_error *error)
{
    *out = QA_PRODUCT_NONE;
    for (size_t i = 0; i < qa_catalog_count(content->catalog); ++i) {
        const qa_product *product = qa_catalog_at(content->catalog, i);
        if (!owns_mount(content->catalog, product->id, content->mounts, info)) continue;
        if (product->family != QA_GAME_Q3 || (*out && *out != product->id))
            return fail(error, QA_ERROR_FORMAT, "Private remote Q3 mount has foreign or ambiguous product ownership");
        *out = product->id;
    }
    return *out || fail(error, QA_ERROR_FORMAT, "Private remote Q3 mount lacks its real catalog owner");
}
static bool inventory(frontend_q3_content *content, qa_error *error)
{
    content->mount_count = qa_vfs_mount_count(content->mounts);
    if (content->mount_count > QA_Q3_SEARCH_PATHS)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 content exceeds the source search path capacity");
    size_t capacity = content->mount_count ? content->mount_count : 1;
    content->inventory = calloc(capacity, sizeof(*content->inventory));
    content->packs = calloc(capacity, sizeof(*content->packs));
    content->loaded_checksums = calloc(capacity, sizeof(*content->loaded_checksums));
    if (!content->inventory || !content->packs || !content->loaded_checksums)
        return fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 mounted packages");
    for (size_t i = 0; i < content->mount_count; ++i) {
        qa_vfs_mount_info info; content_mount *mount = content->inventory + i;
        if (!qa_vfs_mount_at(content->mounts, i, &info) || !identify_mount(content, &info, &mount->product, error)) return false;
        mount->id = info.id;
        const qa_product *product = qa_catalog_product(content->catalog, mount->product);
        const char *path = qa_vfs_mount_path(content->mounts, info.id);
        if (!info.is_archive && info.writable) {
            if (mount->product == content->selected && !content->selected_write_root) {
                content->selected_write_root = qa_vfs_mount_root(content->mounts, info.id);
                content->selected_write_path = copy(path, error);
            }
            if (mount->product == content->base && !content->base_write_root) {
                content->base_write_root = qa_vfs_mount_root(content->mounts, info.id);
                content->base_write_path = copy(path, error);
            }
            if ((mount->product == content->selected && !content->selected_write_path) ||
                (mount->product == content->base && !content->base_write_path)) return false;
        }
        if (!info.is_archive) continue;
        if (info.format != QA_ARCHIVE_PK3)
            return fail(error, QA_ERROR_FORMAT, "Private remote Q3 package is not a genuine PK3 archive");
        const char *name = last_component(path); size_t length = strlen(name);
        if (length <= 4 || !equal(name + length - 4, ".pk3"))
            return fail(error, QA_ERROR_FORMAT, "Mounted Q3 package lacks its physical PK3 basename");
        mount->game = copy(last_component(product->directory), error);
        mount->basename = malloc(length - 3);
        if (!mount->game || !mount->basename) return fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 package name");
        memcpy(mount->basename, name, length - 4); mount->basename[length - 4] = 0;
        mount->pack = (qa_q3_pak_entry){.game = mount->game, .basename = mount->basename, .archive_path = path};
        if (!qa_vfs_archive_checksums(content->mounts, info.id, (uint32_t)content->gamestate->checksum_feed,
            &mount->pack.checksum, &mount->pack.pure_checksum, error)) return false;
        content->packs[content->pack_count] = &mount->pack;
        content->loaded_checksums[content->pack_count++] = mount->pack.checksum;
    }
    return qa_q3_pak_references_create(content->packs, content->pack_count,
        (uint32_t)content->gamestate->checksum_feed, loose_random, NULL, &content->references, error);
}
void frontend_q3_content_destroy(frontend_q3_content *content)
{
    if (!content) return;
    qa_q3_pak_references_destroy(content->references);
    qa_q3_server_pak_set_destroy(content->loaded_server);
    qa_q3_server_pak_set_destroy(content->referenced_server);
    qa_resource_release(content->map); qa_vfs_acquisition_dispose(&content->map_acquisition);
    if (content->inventory) for (size_t i = 0; i < content->mount_count; ++i) {
        free(content->inventory[i].game); free(content->inventory[i].basename);
    }
    free(content->inventory); free(content->packs); free(content->loaded_checksums); free(content->referenced);
    free(content->selected_directory); free(content->base_directory);
    free(content->selected_write_path); free(content->base_write_path); free(content->gamestate);
    qa_vfs_destroy(content->mounts); qa_catalog_release(content->catalog);
    qa_launch_instance_lease_release(content->descriptor); free(content);
}
bool frontend_q3_content_create(const frontend_q3_content_request *request,
    frontend_q3_content **out, qa_error *error)
{
    if (!request || !out || *out || !request->application || !request->descriptor || !request->catalog ||
        !request->gamestate || !request->connection_epoch || !request->connection_current ||
        !request->connection_current(request->connection, request->connection_epoch, error) ||
        !request->receiver.receiver || request->receiver.seat ||
        request->receiver.source_owner || request->receiver.source_actor.registry ||
        !request->receiver.service_owner || !request->receiver.frontend_lifetime ||
        request->configuration_generation != qa_application_configuration_generation(request->application) ||
        !qa_application_q3_remote_context_current(request->application, &request->receiver))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content requires its actual decoded seat-zero receiver");
    const qa_launch_snapshot *snapshot = qa_application_launch(request->application);
    if (!snapshot ||
        qa_launch_snapshot_find(snapshot, request->descriptor->selection.instance) != request->descriptor)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 descriptor differs from the current selected configuration");
    const qa_product *selected = qa_catalog_product(qa_launch_snapshot_catalog(snapshot), request->descriptor->selection.product);
    const qa_product *base = qa_catalog_find(request->catalog, "q3-baseq3");
    if (!selected || selected->family != QA_GAME_Q3 || !base || base->family != QA_GAME_Q3)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 receiver lacks its real selected Q3 catalog descriptor");
    frontend_q3_content *content = calloc(1, sizeof(*content));
    if (!content) return fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 gamestate content");
    content->application = request->application; content->catalog = request->catalog; qa_catalog_retain(content->catalog);
    content->receiver = request->receiver; content->configuration_generation = request->configuration_generation;
    content->connection_epoch = request->connection_epoch; content->base = base->id;
    content->connection = request->connection; content->connection_current = request->connection_current;
    content->gamestate = malloc(sizeof(*content->gamestate));
    if (!content->gamestate) { fail(error, QA_ERROR_MEMORY, "Retaining decoded Q3 gamestate"); goto failed; }
    *content->gamestate = *request->gamestate;
    char info[QA_Q3_BIG_INFO_CHARS], game[1024], paks[QA_Q3_BIG_INFO_CHARS], names[QA_Q3_BIG_INFO_CHARS], map[1024];
    if (!qa_launch_instance_retain_metadata(request->descriptor, &content->descriptor, error) ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "fs_game", game, sizeof(game), error) ||
        !qa_catalog_remote(content->catalog, content->base, game, &content->selected, error) ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "sv_pure", info, sizeof(info), error)) goto failed;
    content->pure = number(info) != 0;
    if (!qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "sv_serverid", info, sizeof(info), error)) goto failed;
    content->server_id = number(info);
    if (content->gamestate->client_number < 0 || content->gamestate->client_number >= 64 ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 0), "protocol", info, sizeof(info), error) || strcmp(info, "68") ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 0), "mapname", map, sizeof(map), error)) {
        fail(error, QA_ERROR_FORMAT, "Remote Q3 gamestate lacks its genuine protocol and client ordinal"); goto failed;
    }
    int32_t selected_game_type;
    if (!qa_q3_info_value(qa_q3_configstring(content->gamestate, 0), "g_gametype", info, sizeof(info), error) ||
        !game_type(info, &selected_game_type)) {
        fail(error, QA_ERROR_FORMAT, "Remote Q3 gamestate changes the selected baseq3 game type contract"); goto failed;
    }
    size_t map_length = strlen(map);
    if (!map_length || map_length + 10 > sizeof(content->map_path)) goto bad_map;
    for (size_t i = 0; i < map_length; ++i) {
        unsigned ch = (unsigned char)map[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '/')) goto bad_map;
    }
    snprintf(content->map_path, sizeof(content->map_path), "maps/%s.bsp", map);
    char *normalized = qa_vfs_normalize_path(content->map_path, error);
    if (!normalized) goto failed;
    free(normalized);
    selected = qa_catalog_product(content->catalog, content->selected);
    content->selected_directory = copy(last_component(selected->directory), error);
    content->base_directory = copy(last_component(base->directory), error);
    if (!content->selected_directory || !content->base_directory ||
        !qa_catalog_open(content->catalog, content->selected, &content->mounts, error) ||
        !inventory(content, error) || !qa_q3_server_pak_set_create(&content->loaded_server, error) ||
        !qa_q3_server_pak_set_create(&content->referenced_server, error) ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "sv_paks", paks, sizeof(paks), error) ||
        !qa_q3_server_pak_set_checksums(content->loaded_server, paks, error) ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "sv_referencedPaks", paks, sizeof(paks), error) ||
        !qa_q3_server_pak_set_checksums(content->referenced_server, paks, error) ||
        !qa_q3_info_value(qa_q3_configstring(content->gamestate, 1), "sv_referencedPakNames", names, sizeof(names), error) ||
        !qa_q3_server_pak_set_names(content->referenced_server, names, SIZE_MAX, error)) goto failed;
    qa_q3_server_pak_set_sums(content->referenced_server, &content->referenced_count);
    content->referenced = calloc(content->referenced_count ? content->referenced_count : 1, sizeof(*content->referenced));
    if (!content->referenced) { fail(error, QA_ERROR_MEMORY, "Retaining real Q3 server package references"); goto failed; }
    for (size_t i = 0; i < content->referenced_count; ++i)
        if (!qa_q3_server_pak_set_at(content->referenced_server, i, content->referenced + i)) goto failed;
    *out = content; return true;
bad_map:
    fail(error, QA_ERROR_FORMAT, "Remote Q3 map name violates its source path contract");
failed:
    frontend_q3_content_destroy(content); return false;
}
frontend_q3_content_phase frontend_q3_content_state(const frontend_q3_content *content)
{ return content ? content->phase : FRONTEND_Q3_CONTENT_CATALOG; }
bool frontend_q3_content_read(const frontend_q3_content *content, frontend_q3_content_view *out, qa_error *error)
{
    if (!out || !current(content, error)) return false;
    *out = (frontend_q3_content_view){.catalog = content->catalog, .selected = content->selected, .base = content->base,
        .mounts = content->mounts, .gamestate = content->gamestate, .map = content->map, .map_path = content->map_path,
        .selected_directory = content->selected_directory, .base_directory = content->base_directory,
        .selected_write_path = content->selected_write_path, .base_write_path = content->base_write_path,
        .selected_write_root = content->selected_write_root, .base_write_root = content->base_write_root,
        .referenced = content->referenced, .referenced_count = content->referenced_count,
        .loaded_checksums = content->loaded_checksums, .loaded_count = content->pack_count,
        .checksum_feed = (uint32_t)content->gamestate->checksum_feed, .server_id = content->server_id, .pure = content->pure};
    return true;
}
bool frontend_q3_content_prepare(frontend_q3_content *content, qa_error *error)
{
    if (!current(content, error) || content->phase != FRONTEND_Q3_CONTENT_CATALOG)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 filesystem preparation requires its fresh gamestate catalog");
    size_t count; const uint32_t *sums = qa_q3_server_pak_set_sums(content->loaded_server, &count);
    size_t capacity = content->mount_count ? content->mount_count : 1;
    qa_sha256_digest *digests = calloc(capacity, sizeof(*digests));
    qa_q3_pure_path *paths = calloc(capacity, sizeof(*paths));
    if (!digests || !paths) { free(digests); free(paths); return fail(error, QA_ERROR_MEMORY, "Retaining server Q3 pure mount order"); }
    for (size_t i = 0; i < content->mount_count; ++i) {
        content_mount *mount = content->inventory + i;
        paths[i] = (qa_q3_pure_path){.kind = mount->pack.archive_path ? QA_Q3_PURE_PACKAGE : QA_Q3_PURE_DIRECTORY,
            .value = mount, .checksum = mount->pack.checksum};
    }
    bool ok = qa_q3_reorder_pure_paths(paths, content->mount_count, sums, count, error);
    size_t accepted = 0;
    for (size_t i = 0; ok && count && i < content->mount_count; ++i) {
        if (paths[i].kind != QA_Q3_PURE_PACKAGE || !qa_q3_pak_is_pure(paths[i].checksum, sums, count)) continue;
        const content_mount *mount = paths[i].value;
        const qa_sha256_digest *digest = qa_vfs_archive_digest(content->mounts, mount->id);
        if (!digest) { ok = fail(error, QA_ERROR_FORMAT, "Server Q3 package lost its retained immutable archive"); break; }
        digests[accepted++] = *digest;
    }
    if (ok && count && !accepted) ok = fail(error, QA_ERROR_NOT_FOUND, "No installed Q3 archives match the server pure list");
    if (ok) ok = qa_vfs_set_restrictions(content->mounts, digests, accepted, false, error);
    free(digests); free(paths);
    if (!ok) return false;
    const qa_q3_pak_entry *order[QA_Q3_SEARCH_PATHS]; size_t packs = 0;
    for (size_t i = 0; i < content->mount_count; ++i) {
        qa_vfs_mount_info info;
        if (!qa_vfs_mount_at(content->mounts, i, &info)) return fail(error, QA_ERROR_FORMAT, "Remote Q3 pure order lost a mounted source");
        for (size_t j = 0; j < content->mount_count; ++j)
            if (content->inventory[j].id == info.id && content->inventory[j].pack.archive_path)
                order[packs++] = &content->inventory[j].pack;
    }
    if (!qa_q3_pak_reorder(content->references, order, packs, error) ||
        !qa_vfs_acquire_receipt(content->mounts, content->map_path, &content->map, &content->map_acquisition, error)) return false;
    content->phase = FRONTEND_Q3_CONTENT_PREPARED; return true;
}
bool frontend_q3_content_publish(frontend_q3_content *content,
    const frontend_q3_content_publication *publication, qa_error *error)
{
    if (!content || !publication || content->phase != FRONTEND_Q3_CONTENT_PREPARED ||
        !publication->descriptor || !publication->descriptor->content ||
        publication->descriptor->content == content->mounts ||
        publication->descriptor->selection.product != content->selected ||
        publication->connection_epoch != content->connection_epoch ||
        publication->configuration_generation <= content->configuration_generation ||
        publication->configuration_generation != qa_application_configuration_generation(content->application) ||
        publication->receiver.seat || publication->receiver.source_owner || publication->receiver.source_actor.registry ||
        publication->receiver.receiver != content->receiver.receiver ||
        !publication->receiver.service_owner || publication->receiver.service_owner == content->receiver.service_owner ||
        !publication->receiver.frontend_lifetime ||
        !qa_application_q3_remote_context_current(content->application, &publication->receiver) ||
        !matching_view(content, publication->descriptor->content, error) ||
        !content->connection_current(content->connection, content->connection_epoch, error))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication lacks its actual fresh private receiver at the retained connection epoch");
    qa_launch_instance_lease *descriptor = NULL;
    if (!qa_launch_instance_retain_metadata(publication->descriptor, &descriptor, error)) return false;
    qa_launch_instance_lease_release(content->descriptor); content->descriptor = descriptor;
    content->receiver = publication->receiver; content->configuration_generation = publication->configuration_generation;
    content->phase = FRONTEND_Q3_CONTENT_PUBLISHED; return true;
}
static content_mount *matching_mount(frontend_q3_content *content, const qa_vfs *view,
    qa_mount_id id, qa_error *error)
{
    qa_vfs_mount_info actual = {0}; bool found = false;
    for (size_t i = 0; i < qa_vfs_mount_count(view); ++i)
        if (qa_vfs_mount_at(view, i, &actual) && actual.id == id) { found = true; break; }
    const char *path = found ? qa_vfs_mount_path(view, id) : NULL;
    for (size_t i = 0; path && i < content->mount_count; ++i) {
        content_mount *mount = content->inventory + i; qa_vfs_mount_info expected = {0};
        for (size_t j = 0; j < content->mount_count; ++j)
            if (qa_vfs_mount_at(content->mounts, j, &expected) && expected.id == mount->id) break;
        const char *selected = qa_vfs_mount_path(content->mounts, mount->id);
        if (selected && !strcmp(selected, path) && actual.is_archive == expected.is_archive &&
            actual.format == expected.format && actual.writable == expected.writable &&
            actual.user_overlay == expected.user_overlay && actual.q3_demo == expected.q3_demo &&
            (!actual.is_archive || (actual.digest && expected.digest && qa_sha256_equal(actual.digest, expected.digest)))) return mount;
    }
    fail(error, QA_ERROR_FORMAT, "Remote Q3 media read lacks its actual selected private mount identity"); return NULL;
}
static bool matching_view(frontend_q3_content *content, const qa_vfs *view, qa_error *error)
{
    if (!view || qa_vfs_resources(view) != qa_vfs_resources(content->mounts) ||
        qa_vfs_mount_count(view) != content->mount_count)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 media view differs from its private filesystem replacement");
    for (size_t i = 0; i < qa_vfs_mount_count(view); ++i) {
        qa_vfs_mount_info actual, expected;
        if (!qa_vfs_mount_at(view, i, &actual) || !qa_vfs_mount_at(content->mounts, i, &expected)) return false;
        content_mount *mount = matching_mount(content, view, actual.id, error);
        if (!mount || mount->id != expected.id)
            return fail(error, QA_ERROR_FORMAT, "Remote Q3 media view changed the server pure search order");
    }
    return true;
}
static bool collect_view(frontend_q3_content *content, qa_vfs *view, qa_error *error)
{
    if (!matching_view(content, view, error)) return false;
    size_t count; const uint32_t *allowed = qa_q3_server_pak_set_sums(content->loaded_server, &count);
    for (size_t i = 0; i < qa_vfs_read_count(view); ++i) {
        qa_vfs_read_reference read;
        if (!qa_vfs_read_at(view, i, &read) || !read.resource || !read.path)
            return fail(error, QA_ERROR_FORMAT, "Remote Q3 media journal lost its held immutable resource");
        content_mount *mount = matching_mount(content, view, read.mount, error);
        if (!mount) return false;
        if (mount->pack.archive_path) {
            if (count && !qa_q3_pak_is_pure(mount->pack.checksum, allowed, count))
                return fail(error, QA_ERROR_FORMAT, "Remote Q3 media consumed an archive outside its server pure package set");
            if (!qa_q3_pak_record_packed(content->references, &mount->pack, read.path, error)) return false;
        } else if (!qa_q3_pak_record_loose(content->references, read.path, error)) return false;
    }
    return true;
}
static bool role_receipt(frontend_q3_content *content,
    const frontend_q3_content_role_receipt *receipt, qa_qvm_role role, qa_error *error)
{
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    if (!receipt || receipt->role != role || receipt->receiver != content->receiver.receiver || receipt->seat ||
        !receipt->service_owner || receipt->configuration_generation != content->configuration_generation ||
        receipt->connection_epoch != content->connection_epoch || !receipt->descriptor ||
        receipt->descriptor->content != descriptor->content || receipt->descriptor->state != descriptor->state ||
        !qa_sha256_equal(&receipt->descriptor->identity, &descriptor->identity) ||
        !receipt->artifact || !receipt->acquisition || !receipt->artifact_view || !receipt->current ||
        (receipt->media_view_count && !receipt->media_views) ||
        (role == QA_QVM_CGAME && receipt->service_owner != content->receiver.service_owner) ||
        receipt->acquisition->resource_id != qa_resource_id(receipt->artifact) ||
        qa_resource_pool_find(qa_vfs_resources(receipt->artifact_view), receipt->acquisition->resource_id) != receipt->artifact ||
        !receipt->current(receipt->producer, receipt, error) ||
        !qa_vfs_acquisition_retained(receipt->artifact_view, receipt->acquisition, error))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 media completion lacks its real initialized role artifact receipt");
    content_mount *mount = matching_mount(content, receipt->artifact_view, receipt->acquisition->mount, error);
    if (!mount || !collect_view(content, receipt->artifact_view, error)) return false;
    if (mount->pack.archive_path) {
        if (!qa_q3_pak_record_client_role(content->references, &mount->pack, role, error)) return false;
    } else if (content->pure) {
        return fail(error, QA_ERROR_FORMAT, "Pure Q3 requires its actual CGAME and UI artifacts from selected PK3 packages");
    }
    for (size_t i = 0; i < receipt->media_view_count; ++i)
        if (!collect_view(content, receipt->media_views[i], error)) return false;
    return receipt->current(receipt->producer, receipt, error) && current(content, error);
}
bool frontend_q3_content_media_ready(frontend_q3_content *content,
    const frontend_q3_content_role_receipt *cgame, const frontend_q3_content_role_receipt *ui, qa_error *error)
{
    qa_application_q3_client_context receiver;
    if (!current(content, error) || content->phase != FRONTEND_Q3_CONTENT_PUBLISHED ||
        !qa_application_q3_remote_context_read(content->application, content->receiver.receiver, 0, &receiver, error) ||
        !receiver.initialized)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 media completion requires the actual published initialized receiver");
    if (!role_receipt(content, cgame, QA_QVM_CGAME, error) || !role_receipt(content, ui, QA_QVM_UI, error) ||
        !collect_view(content, content->mounts, error) || !cgame->current(cgame->producer, cgame, error) ||
        !ui->current(ui->producer, ui, error) || !current(content, error)) return false;
    content->receiver.initialized = true; content->phase = FRONTEND_Q3_CONTENT_MEDIA_READY; return true;
}
bool frontend_q3_content_pure_command(frontend_q3_content *content, char *out, size_t capacity, qa_error *error)
{
    if (!out || !capacity || !current(content, error) ||
        content->phase != FRONTEND_Q3_CONTENT_MEDIA_READY)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 CP requires completed real CGAME and UI media initialization");
    char report[QA_Q3_BIG_INFO_CHARS];
    if (!qa_q3_pak_report(content->references, QA_Q3_PAK_REFERENCED_PURE_CHECKSUMS, report, sizeof(report), error)) return false;
    int length = snprintf(out, capacity, "cp %d %s", content->server_id, report);
    return (length >= 0 && (size_t)length < capacity && length < QA_Q3_COMMAND_CHARS) ||
        fail(error, QA_ERROR_FORMAT, "Remote Q3 CP command exceeds retained reliable storage");
}
bool frontend_q3_content_download_reference(const frontend_q3_content *content,
    const char *remote, uint32_t checksum, qa_error *error)
{
    if (!current(content, error) || !qa_q3_download_name(remote, error)) return false;
    bool found = false;
    for (size_t i = 0; i < content->referenced_count; ++i) {
        const qa_q3_package *pack = content->referenced + i;
        if (!pack->name) continue;
        size_t length = strlen(pack->name);
        if (strlen(remote) != length + 4 || memcmp(remote, pack->name, length) || strcmp(remote + length, ".pk3")) continue;
        if (pack->checksum != checksum)
            return fail(error, QA_ERROR_FORMAT, "Server Q3 references conflicting package checksums for this download");
        found = true;
    }
    return found || fail(error, QA_ERROR_FORMAT, "Q3 download has no actual admitted gamestate package reference");
}
bool frontend_q3_content_download_destination(const frontend_q3_content *content,
    const char *remote, char **out, qa_error *error)
{
    if (!out || *out || !current(content, error) || !qa_q3_download_name(remote, error)) return false;
    const char *slash = strchr(remote, '/');
    if (!slash) { *out = copy(remote, error); return *out != NULL; }
    if (!content->selected_write_root || !content->base_write_root ||
        !content->selected_write_path || !content->base_write_path)
        return fail(error, QA_ERROR_NOT_FOUND, "Q3 downloads lack the actual selected and base writable catalog mounts");
    const char *selected = strrchr(content->selected_write_path, '/'), *base = strrchr(content->base_write_path, '/');
    if (!selected || !base || selected - content->selected_write_path != base - content->base_write_path ||
        memcmp(content->selected_write_path, content->base_write_path, (size_t)(selected - content->selected_write_path)))
        return fail(error, QA_ERROR_FORMAT, "Q3 download mounts do not share the actual selected family root");
    size_t directory_length = (size_t)(slash - remote); char directory[64];
    if (directory_length >= sizeof(directory)) return fail(error, QA_ERROR_FORMAT, "Q3 download directory exceeds source path storage");
    memcpy(directory, remote, directory_length); directory[directory_length] = 0;
    const char *physical = equal(directory, content->selected_directory) ? selected + 1 :
        equal(directory, content->base_directory) ? base + 1 : NULL;
    if (!physical) { *out = copy(remote, error); return *out != NULL; }
    size_t length = strlen(physical) + strlen(slash) + 1; char *mapped = malloc(length);
    if (!mapped) return fail(error, QA_ERROR_MEMORY, "Retaining physical Q3 download destination spelling");
    snprintf(mapped, length, "%s%s", physical, slash);
    if (!qa_q3_download_name(mapped, error)) { free(mapped); return false; }
    *out = mapped; return true;
}
