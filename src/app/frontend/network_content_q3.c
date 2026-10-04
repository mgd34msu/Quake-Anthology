#include "network_content_q3.h"
#include "qa/network_q3_pak_role.h"
#include "qa/network_q3_pak_save.h"
#include "qa/network_q3_fields_save.h"
#include "qa/source_save.h"
#include "qa/vfs_view_save.h"
#include "qa/catalog_save.h"
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
    bool (*native_media_read)(void *, frontend_q3_content_native_receipt *,
        frontend_q3_content_role_receipt *, qa_error *);
    bool (*modules_media_read)(void *, const application_native_q3_client_modules **,
        frontend_q3_content_role_receipt *, frontend_q3_content_role_receipt *, qa_error *);
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
    bool pure, native_cgame, acquired_cgame, pending_native;
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
            unsigned digit = *cursor >= '0' && *cursor <= '9' ? (unsigned)(*cursor - '0') :
                lower(*cursor) >= 'a' && lower(*cursor) <= 'f' ? (unsigned)(lower(*cursor) - 'a' + 10) : radix;
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
static bool policy_current(const frontend_q3_content *, const qa_vfs *, qa_error *);
static bool initialized_cut(frontend_q3_content *, qa_error *);
static bool binding_current(const frontend_q3_content_request *binding, qa_error *error)
{
    if (!binding || !binding->application || !binding->descriptor || !binding->connection_epoch ||
        !binding->connection_current || !binding->receiver.receiver || binding->receiver.source_owner ||
        binding->receiver.source_actor.registry || !binding->receiver.service_owner || !binding->receiver.frontend_lifetime)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content requires its actual source receiver and connection binding");
    qa_application_q3_remote_source source = {.descriptor = binding->descriptor,
        .receiver = binding->receiver, .configuration_generation = binding->configuration_generation,
        .connection_epoch = binding->connection_epoch};
    return (qa_application_q3_remote_source_current(binding->application, &source) &&
        binding->connection_current(binding->connection, binding->connection_epoch, error)) ||
        fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content lost its private source generation or connection epoch");
}
static bool source_current(const frontend_q3_content *content, qa_error *error)
{
    if (!content) return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content has no retained source owner");
    frontend_q3_content_request binding = {.application = content->application,
        .descriptor = qa_launch_instance_lease_view(content->descriptor), .receiver = content->receiver,
        .configuration_generation = content->configuration_generation, .connection_epoch = content->connection_epoch,
        .connection = content->connection, .connection_current = content->connection_current};
    if (binding.receiver.native_source) {
        qa_application_q3_client_context actual;
        if (!qa_application_q3_remote_context_read(content->application, binding.receiver.receiver,
            binding.receiver.seat, &actual, error)) return false;
        if (content->receiver.initialized && !actual.initialized)
            return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content lost its completed compiled CLIENT Init");
        binding.receiver.initialized = actual.initialized;
    }
    return binding_current(&binding, error);
}
static bool current(const frontend_q3_content *content, qa_error *error)
{
    if (content && content->pending_native)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content awaits its actual restored native media owners");
    return source_current(content, error);
}
static bool owns_mount(const qa_catalog *catalog, qa_product_id product,
    const qa_vfs *view, const qa_vfs_mount_info *info, qa_error *error)
{
    const qa_mount_id *owned; size_t count;
    if (!qa_catalog_product_own_mounts(catalog, product, &owned, &count)) return false;
    const char *path = qa_vfs_mount_path(view, info->id);
    for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < qa_catalog_mount_count(catalog); ++j) {
        const qa_catalog_mount *mount = qa_catalog_mount_at(catalog, j);
        if (mount->id != owned[i] || !path || strcmp(path, mount->path) ||
            mount->format != info->format || mount->writable != info->writable) continue;
        if (!info->is_archive) return true;
        const qa_sha256_digest *actual = NULL, *physical = NULL;
        if (!qa_vfs_archive_digest_read(view, info->id, &actual, error) ||
            !qa_catalog_mount_digest_read(catalog, mount->id, &physical, error)) return false;
        if (qa_sha256_equal(actual, physical)) return true;
    }
    return false;
}
static bool identify_mount(frontend_q3_content *content, const qa_vfs_mount_info *info,
    qa_product_id *out, qa_error *error)
{
    *out = QA_PRODUCT_NONE;
    if (qa_catalog_q3_restricted(content->catalog)) {
        const qa_product *selected = qa_catalog_product(content->catalog, content->selected);
        if (!selected || selected->family != QA_GAME_Q3 ||
            !owns_mount(content->catalog, selected->id, content->mounts, info, error) || !info->q3_demo)
            return (error && error->code != QA_OK) ? false :
                fail(error, QA_ERROR_FORMAT, "Restricted remote Q3 mount lost its actual selected demo media scope");
        *out = selected->id;
        return true;
    }
    for (size_t i = 0; i < qa_catalog_count(content->catalog); ++i) {
        const qa_product *product = qa_catalog_at(content->catalog, i);
        if (!owns_mount(content->catalog, product->id, content->mounts, info, error)) {
            if (error && error->code != QA_OK) return false;
            continue;
        }
        if (product->family != QA_GAME_Q3 || (*out && *out != product->id))
            return fail(error, QA_ERROR_FORMAT, "Private remote Q3 mount has foreign or ambiguous product ownership");
        *out = product->id;
    }
    return *out || fail(error, QA_ERROR_FORMAT, "Private remote Q3 mount lacks its real catalog owner");
}
static bool inventory(frontend_q3_content *content, const qa_mount_id *saved_order,
    size_t saved_count, qa_error *error)
{
    content->mount_count = qa_vfs_mount_count(content->mounts);
    if (content->mount_count > QA_Q3_SEARCH_PATHS || (saved_order && saved_count != content->mount_count))
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 content exceeds the source search path capacity");
    size_t capacity = content->mount_count ? content->mount_count : 1;
    const qa_mount_id *catalog_order; size_t catalog_count;
    if (!qa_catalog_product_mounts(content->catalog, content->selected, &catalog_order, &catalog_count) ||
        catalog_count != content->mount_count)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 inventory differs from its actual selected catalog scope");
    content->inventory = calloc(capacity, sizeof(*content->inventory));
    content->packs = calloc(capacity, sizeof(*content->packs));
    content->loaded_checksums = calloc(capacity, sizeof(*content->loaded_checksums));
    if (!content->inventory || !content->packs || !content->loaded_checksums)
        return fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 mounted packages");
    for (size_t i = 0; i < content->mount_count; ++i) {
        qa_vfs_mount_info info; content_mount *mount = content->inventory + i;
        bool found = false;
        for (size_t j = 0; j < content->mount_count; ++j) {
            if (!qa_vfs_mount_at(content->mounts, j, &info)) return false;
            if (saved_order ? info.id == saved_order[i] : j == i) { found = true; break; }
        }
        if (!found || !identify_mount(content, &info, &mount->product, error)) return false;
        const qa_catalog_mount *original = NULL;
        for (size_t j = 0; j < qa_catalog_mount_count(content->catalog); ++j) {
            const qa_catalog_mount *candidate = qa_catalog_mount_at(content->catalog, j);
            if (candidate->id == catalog_order[i]) { original = candidate; break; }
        }
        const char *retained_path = qa_vfs_mount_path(content->mounts, info.id);
        if (original && info.is_archive) {
            const qa_sha256_digest *physical = NULL;
            if (!qa_catalog_mount_digest_read(content->catalog, original->id, &physical, error) ||
                !qa_vfs_archive_digest_read(content->mounts, info.id, &info.digest, error)) return false;
            original = qa_catalog_mount_at(content->catalog, (size_t)original->id - 1);
        }
        if (!original || !retained_path || strcmp(original->path, retained_path) ||
            original->format != info.format || original->writable != info.writable || info.user_overlay ||
            info.q3_demo != qa_catalog_q3_restricted(content->catalog) ||
            (info.is_archive && (!original->digest || !info.digest || !qa_sha256_equal(original->digest, info.digest))))
            return fail(error, QA_ERROR_FORMAT, "Remote Q3 original inventory slot lost its genuine catalog mount identity");
        for (size_t j = 0; j < i; ++j) if (content->inventory[j].id == info.id)
            return fail(error, QA_ERROR_FORMAT, "Remote Q3 inventory repeats a retained mount identity");
        mount->id = info.id;
        const qa_product *product = qa_catalog_product(content->catalog, mount->product);
        const char *path = qa_vfs_mount_path(content->mounts, info.id);
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
static bool create(const frontend_q3_content_request *request, qa_vfs **imported,
    const qa_mount_id *saved_order, size_t saved_count, frontend_q3_content **out, qa_error *error)
{
    if (!out || *out || !binding_current(request, error) || !request->catalog || !request->gamestate)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 content requires its actual decoded source receiver");
    const qa_product *selected = qa_catalog_product(qa_launch_instance_catalog(request->descriptor), request->descriptor->selection.product);
    const qa_product *base = qa_catalog_find(request->catalog, "q3-baseq3");
    if (!selected || selected->family != QA_GAME_Q3 || !base || base->family != QA_GAME_Q3)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 receiver lacks its real selected Q3 catalog descriptor");
    frontend_q3_content *content = calloc(1, sizeof(*content));
    if (!content) return fail(error, QA_ERROR_MEMORY, "Retaining remote Q3 gamestate content");
    if (imported) { content->mounts = *imported; *imported = NULL; }
    content->application = request->application; content->catalog = request->catalog; qa_catalog_retain(content->catalog);
    content->receiver = request->receiver; content->configuration_generation = request->configuration_generation;
    content->connection_epoch = request->connection_epoch; content->base = base->id;
    content->connection = request->connection; content->connection_current = request->connection_current;
    content->native_media_read = request->native_media_read;
    content->modules_media_read = request->modules_media_read;
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
    memcpy(content->map_path, "maps/", 5);
    memcpy(content->map_path + 5, map, map_length);
    memcpy(content->map_path + 5 + map_length, ".bsp", 5);
    char *normalized = qa_vfs_normalize_path(content->map_path, error);
    if (!normalized) goto failed;
    free(normalized);
    selected = qa_catalog_product(content->catalog, content->selected);
    content->selected_directory = copy(last_component(selected->directory), error);
    content->base_directory = copy(last_component(base->directory), error);
    const qa_catalog_mount *selected_write = qa_catalog_product_write_mount(content->catalog, content->selected);
    const qa_catalog_mount *base_write = qa_catalog_product_write_mount(content->catalog, content->base);
    content->selected_write_root = qa_catalog_product_write_root(content->catalog, content->selected);
    content->base_write_root = qa_catalog_product_write_root(content->catalog, content->base);
    if (selected_write) content->selected_write_path = copy(selected_write->path, error);
    if (base_write) content->base_write_path = copy(base_write->path, error);
    if ((selected_write && !content->selected_write_path) || (base_write && !content->base_write_path)) goto failed;
    if (!content->selected_directory || !content->base_directory ||
        (!content->mounts && !qa_catalog_open(content->catalog, content->selected, &content->mounts, error)) ||
        !inventory(content, saved_order, saved_count, error) || !qa_q3_server_pak_set_create(&content->loaded_server, error) ||
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
bool frontend_q3_content_create(const frontend_q3_content_request *request,
    frontend_q3_content **out, qa_error *error)
{ return create(request, NULL, NULL, 0, out, error); }
frontend_q3_content_phase frontend_q3_content_state(const frontend_q3_content *content)
{ return content ? content->pending_native ? FRONTEND_Q3_CONTENT_PUBLISHED : content->phase : FRONTEND_Q3_CONTENT_CATALOG; }
static void content_view(const frontend_q3_content *content, frontend_q3_content_view *out)
{
    *out = (frontend_q3_content_view){.catalog = content->catalog, .selected = content->selected, .base = content->base,
        .mounts = content->mounts, .gamestate = content->gamestate, .map = content->map, .map_path = content->map_path,
        .selected_directory = content->selected_directory, .base_directory = content->base_directory,
        .selected_write_path = content->selected_write_path, .base_write_path = content->base_write_path,
        .selected_write_root = content->selected_write_root, .base_write_root = content->base_write_root,
        .referenced = content->referenced, .referenced_count = content->referenced_count,
        .loaded_checksums = content->loaded_checksums, .loaded_count = content->pack_count,
        .checksum_feed = (uint32_t)content->gamestate->checksum_feed, .server_id = content->server_id, .pure = content->pure};
}
bool frontend_q3_content_read(const frontend_q3_content *content, frontend_q3_content_view *out, qa_error *error)
{
    if (!out || !current(content, error)) return false;
    content_view(content, out);
    return true;
}
bool frontend_q3_content_metadata_read(const frontend_q3_content *content,
    frontend_q3_content_metadata *out, qa_error *error)
{
    if (!content || !out || content->pending_native)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 inventory cannot read missing or pending content");
    frontend_q3_content_metadata metadata = {.application = content->application,
        .source = {.descriptor = qa_launch_instance_lease_view(content->descriptor),
            .receiver = content->receiver, .configuration_generation = content->configuration_generation,
            .connection_epoch = content->connection_epoch}};
    content_view(content, &metadata.content);
    *out = metadata; return true;
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
        const qa_sha256_digest *digest = NULL;
        if (!qa_vfs_archive_digest_read(content->mounts, mount->id, &digest, error)) { ok = false; break; }
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
        publication->receiver.seat != content->receiver.seat || publication->receiver.source_owner || publication->receiver.source_actor.registry ||
        publication->receiver.receiver != content->receiver.receiver ||
        !publication->receiver.service_owner ||
        (publication->receiver.service_owner == content->receiver.service_owner &&
            !(publication->receiver.native_source && content->receiver.native_source &&
                publication->descriptor->selection.runtime == QA_PROGRAM_BUILTIN && !publication->descriptor->artifact)) ||
        !publication->receiver.frontend_lifetime ||
        !qa_application_q3_remote_context_current(content->application, &publication->receiver) ||
        !matching_view(content, publication->descriptor->content, error) ||
        !content->connection_current(content->connection, content->connection_epoch, error))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication lacks its actual fresh private receiver at the retained connection epoch");
    frontend_q3_content_request binding = {.application = content->application,
        .descriptor = publication->descriptor, .receiver = publication->receiver,
        .configuration_generation = publication->configuration_generation, .connection_epoch = publication->connection_epoch,
        .connection = content->connection, .connection_current = content->connection_current};
    if (!binding_current(&binding, error)) return false;
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
    if (found && actual.is_archive && !qa_vfs_archive_digest_read(view, id, &actual.digest, error)) return NULL;
    for (size_t i = 0; path && i < content->mount_count; ++i) {
        content_mount *mount = content->inventory + i; qa_vfs_mount_info expected = {0};
        for (size_t j = 0; j < content->mount_count; ++j)
            if (qa_vfs_mount_at(content->mounts, j, &expected) && expected.id == mount->id) break;
        const char *selected = qa_vfs_mount_path(content->mounts, mount->id);
        if (selected && !strcmp(selected, path) && expected.is_archive &&
            !qa_vfs_archive_digest_read(content->mounts, mount->id, &expected.digest, error)) return NULL;
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
    const qa_sha256_digest *actual, *expected; size_t actual_count, expected_count; bool actual_demo, expected_demo;
    if (!qa_vfs_restrictions_read(view, &actual, &actual_count, &actual_demo) ||
        !qa_vfs_restrictions_read(content->mounts, &expected, &expected_count, &expected_demo) ||
        actual_count != expected_count || actual_demo != expected_demo)
        return fail(error, QA_ERROR_FORMAT, "Remote Q3 media view changed its actual pure restriction policy");
    for (size_t i = 0; i < actual_count; ++i)
        if (!qa_sha256_equal(actual + i, expected + i))
            return fail(error, QA_ERROR_FORMAT, "Remote Q3 media view changed its ordered pure package identities");
    if (qa_vfs_prefix_count(view) != qa_vfs_prefix_count(content->mounts)) return false;
    for (size_t i = 0; i < qa_vfs_prefix_count(view); ++i) {
        const char *a, *b; const qa_mount_id *left, *right; size_t x, y;
        if (!qa_vfs_prefix_at(view, i, &a, &left, &x) || !qa_vfs_prefix_at(content->mounts, i, &b, &right, &y) ||
            strcmp(a, b) || x != y) return false;
        for (size_t j = 0; j < x; ++j) {
            content_mount *mount = matching_mount(content, view, left[j], error);
            if (!mount || mount->id != right[j]) return false;
        }
    }
    return true;
}
static bool policy_current(const frontend_q3_content *content, const qa_vfs *view, qa_error *error)
{
    const qa_sha256_digest *actual; size_t actual_count; bool demo;
    if (!qa_vfs_restrictions_read(view, &actual, &actual_count, &demo) || demo)
        return fail(error, QA_ERROR_FORMAT, "Private Q3 continuation changed its per-source restriction contract");
    size_t count; const uint32_t *sums = qa_q3_server_pak_set_sums(content->loaded_server, &count);
    if (content->phase == FRONTEND_Q3_CONTENT_CATALOG) count = 0;
    size_t capacity = content->mount_count ? content->mount_count : 1;
    qa_q3_pure_path *paths = calloc(capacity, sizeof(*paths));
    qa_mount_id *order = calloc(capacity, sizeof(*order));
    if (!paths || !order) {
        free(paths); free(order);
        return fail(error, QA_ERROR_MEMORY, "Qualifying retained Q3 pure policy");
    }
    for (size_t i = 0; i < content->mount_count; ++i) {
        const content_mount *mount = content->inventory + i;
        paths[i] = (qa_q3_pure_path){.kind = mount->pack.archive_path ? QA_Q3_PURE_PACKAGE : QA_Q3_PURE_DIRECTORY,
            .value = (void *)mount, .checksum = mount->pack.checksum};
        order[i] = mount->id;
    }
    bool ok = qa_q3_reorder_pure_paths(paths, content->mount_count, sums, count, error); size_t accepted = 0;
    for (size_t i = 0; ok && count && i < content->mount_count; ++i) {
        if (paths[i].kind != QA_Q3_PURE_PACKAGE || !qa_q3_pak_is_pure(paths[i].checksum, sums, count)) continue;
        const content_mount *mount = paths[i].value;
        const qa_sha256_digest *digest = NULL;
        if (!qa_vfs_archive_digest_read(content->mounts, mount->id, &digest, error) ||
            accepted >= actual_count || !qa_sha256_equal(actual + accepted, digest)) ok = false;
        ++accepted;
    }
    if (ok && accepted == actual_count) {
        size_t first = 0;
        for (size_t i = 0; i < actual_count; ++i) for (size_t j = first; j < content->mount_count; ++j) {
            const qa_sha256_digest *digest = qa_vfs_archive_digest(view, order[j]);
            if (!digest || !qa_sha256_equal(digest, actual + i)) continue;
            qa_mount_id selected = order[j];
            memmove(order + first + 1, order + first, (j - first) * sizeof(*order));
            order[first++] = selected; break;
        }
        for (size_t i = 0; ok && i < content->mount_count; ++i) {
            qa_vfs_mount_info mount;
            ok = qa_vfs_mount_at(view, i, &mount) && mount.id == order[i];
        }
        ok = ok && !qa_vfs_prefix_count(view);
    }
    free(paths); free(order);
    return (ok && accepted == actual_count && (!count || accepted)) ||
        fail(error, QA_ERROR_FORMAT, "Saved Q3 pure policy differs from its actual admitted server package order");
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
    if (!receipt || receipt->role != role || receipt->receiver != content->receiver.receiver || receipt->seat != content->receiver.seat ||
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
static bool native_receipt(frontend_q3_content *content,
    const frontend_q3_content_native_receipt *receipt, qa_error *error)
{
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    qa_native_q3_remote_client_basis basis;
    if (content->pure)
        return fail(error, QA_ERROR_FORMAT, "Pure Q3 requires its genuinely acquired CGAME module rather than compiled media");
    if (!receipt || !receipt->client || !receipt->media_view || !receipt->current ||
        !receipt->current(receipt->producer, receipt, error) ||
        !qa_native_q3_remote_client_basis_read(receipt->client, &basis, error) ||
        !basis.client.initialized || !basis.client.native_source ||
        basis.application != content->application || basis.client.receiver != content->receiver.receiver ||
        basis.client.seat != content->receiver.seat || basis.client.service_owner != content->receiver.service_owner ||
        basis.epoch != content->connection_epoch || basis.configuration_generation != content->configuration_generation ||
        !basis.descriptor || basis.descriptor->storage != descriptor->storage ||
        basis.descriptor->content != descriptor->content || basis.descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        basis.descriptor->artifact || !qa_sha256_equal(&basis.descriptor->identity, &descriptor->identity) ||
        basis.content != descriptor->content || basis.content_product != content->selected || basis.map != content->map ||
        !basis.gamestate || basis.gamestate->client_number != content->gamestate->client_number ||
        basis.gamestate->checksum_feed != content->gamestate->checksum_feed ||
        !matching_view(content, receipt->media_view, error))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 compiled completion lacks its actual initialized service and private media view");
    for (size_t i = 0; i < qa_q3_pak_reference_count(content->references); ++i) {
        qa_q3_pak_reference reference;
        if (!qa_q3_pak_reference_at(content->references, i, &reference)) return false;
        if (reference.flags & QA_Q3_PAK_CGAME)
            return fail(error, QA_ERROR_FORMAT, "Compiled Q3 media cannot retain an acquired CGAME package role");
    }
    return receipt->current(receipt->producer, receipt, error);
}
static bool modules_current(frontend_q3_content *content,
    const application_native_q3_client_modules *modules, qa_error *error)
{
    qa_application_q3_remote_source source;
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    if (!source_current(content, error) || !modules ||
        !qa_application_q3_remote_source_read(content->application, content->receiver.receiver,
            content->receiver.seat, content->connection_epoch, &source, error) ||
        !source.receiver.native_source || source.descriptor->selection.runtime != QA_PROGRAM_BUILTIN ||
        source.descriptor->artifact || source.descriptor->storage != descriptor->storage ||
        source.descriptor->content != descriptor->content ||
        source.configuration_generation != content->configuration_generation ||
        !qa_application_native_q3_client_modules_current(modules, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Remote acquired client modules lost their actual physical CLIENT source");
    return true;
}
static bool module_receipt(frontend_q3_content *content,
    const application_native_q3_client_modules *modules,
    const frontend_q3_content_role_receipt *receipt, qa_qvm_role role,
    content_mount **mount, qa_error *error)
{
    qa_application_q3_role_receipt actual;
    if (!modules_current(content, modules, error) || !receipt || receipt->role != role ||
        !qa_application_native_q3_client_modules_receipt_read(modules, role, &actual, error) ||
        !qa_application_native_q3_client_modules_receipt_current(modules, &actual) ||
        receipt->receiver != actual.receiver || receipt->seat != actual.seat ||
        receipt->service_owner != actual.service_owner ||
        receipt->configuration_generation != actual.configuration_generation ||
        receipt->connection_epoch != actual.connection_epoch || !receipt->descriptor ||
        receipt->descriptor->storage != actual.descriptor->storage ||
        receipt->descriptor->content != actual.descriptor->content ||
        receipt->descriptor->state != actual.descriptor->state ||
        !qa_sha256_equal(&receipt->descriptor->identity, &actual.descriptor->identity) ||
        receipt->artifact != actual.artifact || receipt->acquisition != actual.acquisition ||
        receipt->artifact_view != actual.artifact_view || !receipt->current ||
        (receipt->media_view_count && !receipt->media_views) ||
        actual.acquisition->resource_id != qa_resource_id(actual.artifact) ||
        qa_resource_pool_find(qa_vfs_resources(actual.artifact_view), actual.acquisition->resource_id) != actual.artifact ||
        !receipt->current(receipt->producer, receipt, error) ||
        !qa_vfs_acquisition_retained(actual.artifact_view, actual.acquisition, error) ||
        !matching_view(content, actual.artifact_view, error))
        return fail(error, QA_ERROR_ARGUMENT, "Remote acquired client media lacks its actual module namespace and successful Init receipt");
    for (size_t i = 0; i < receipt->media_view_count; ++i)
        if (!matching_view(content, receipt->media_views[i], error)) return false;
    *mount = matching_mount(content, actual.artifact_view, actual.acquisition->mount, error);
    if (!*mount || (content->pure && !(*mount)->pack.archive_path))
        return fail(error, QA_ERROR_FORMAT, "Pure Q3 acquired client modules require their actual selected PK3 openings");
    return receipt->current(receipt->producer, receipt, error) &&
        qa_application_native_q3_client_modules_receipt_current(modules, &actual) && modules_current(content, modules, error);
}
bool frontend_q3_content_media_ready(frontend_q3_content *content,
    const frontend_q3_content_role_receipt *cgame, const frontend_q3_content_role_receipt *ui, qa_error *error)
{
    qa_application_q3_client_context receiver;
    if (!current(content, error) || content->phase != FRONTEND_Q3_CONTENT_PUBLISHED ||
        !qa_application_q3_remote_context_read(content->application, content->receiver.receiver, content->receiver.seat, &receiver, error) ||
        !receiver.initialized)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 media completion requires the actual published initialized receiver");
    if (!role_receipt(content, cgame, QA_QVM_CGAME, error) || !role_receipt(content, ui, QA_QVM_UI, error) ||
        !collect_view(content, content->mounts, error) || !cgame->current(cgame->producer, cgame, error) ||
        !ui->current(ui->producer, ui, error) || !current(content, error)) return false;
    content->receiver.initialized = true; content->phase = FRONTEND_Q3_CONTENT_MEDIA_READY; return true;
}
bool frontend_q3_content_native_media_ready(frontend_q3_content *content,
    const frontend_q3_content_native_receipt *cgame,
    const frontend_q3_content_role_receipt *ui, qa_error *error)
{
    if (!current(content, error) || content->phase != FRONTEND_Q3_CONTENT_PUBLISHED ||
        !content->native_media_read || !native_receipt(content, cgame, error) ||
        !role_receipt(content, ui, QA_QVM_UI, error) || !collect_view(content, cgame->media_view, error) ||
        !collect_view(content, content->mounts, error) || !native_receipt(content, cgame, error) ||
        !ui->current(ui->producer, ui, error) || !current(content, error)) return false;
    content->native_cgame = true; content->receiver.initialized = true;
    content->phase = FRONTEND_Q3_CONTENT_MEDIA_READY; return true;
}
bool frontend_q3_content_modules_media_ready(frontend_q3_content *content,
    const application_native_q3_client_modules *modules,
    const frontend_q3_content_role_receipt *cgame,
    const frontend_q3_content_role_receipt *ui, qa_error *error)
{
    const application_native_q3_client_modules *actual = NULL;
    frontend_q3_content_role_receipt held_cgame = {0}, held_ui = {0};
    content_mount *cgame_mount, *ui_mount;
    if (!current(content, error) || content->phase != FRONTEND_Q3_CONTENT_PUBLISHED ||
        !content->modules_media_read ||
        !content->modules_media_read(content->connection, &actual, &held_cgame, &held_ui, error) || actual != modules ||
        !module_receipt(content, modules, &held_cgame, QA_QVM_CGAME, &cgame_mount, error) ||
        !module_receipt(content, modules, &held_ui, QA_QVM_UI, &ui_mount, error) ||
        !module_receipt(content, modules, cgame, QA_QVM_CGAME, &cgame_mount, error) ||
        !module_receipt(content, modules, ui, QA_QVM_UI, &ui_mount, error)) return false;
    const frontend_q3_content_role_receipt *roles[] = {cgame, ui};
    content_mount *mounts[] = {cgame_mount, ui_mount};
    for (size_t i = 0; i < 2; ++i) {
        if (!collect_view(content, roles[i]->artifact_view, error)) return false;
        if (mounts[i]->pack.archive_path &&
            !qa_q3_pak_record_client_role(content->references, &mounts[i]->pack, roles[i]->role, error)) return false;
        for (size_t j = 0; j < roles[i]->media_view_count; ++j)
            if (!collect_view(content, roles[i]->media_views[j], error)) return false;
    }
    if (!collect_view(content, content->mounts, error) ||
        !module_receipt(content, modules, cgame, QA_QVM_CGAME, &cgame_mount, error) ||
        !module_receipt(content, modules, ui, QA_QVM_UI, &ui_mount, error) || !current(content, error)) return false;
    content->acquired_cgame = true; content->phase = FRONTEND_Q3_CONTENT_MEDIA_READY; return true;
}
bool frontend_q3_content_media_current(frontend_q3_content *content, qa_error *error)
{
    return current(content, error) && content->phase == FRONTEND_Q3_CONTENT_MEDIA_READY &&
        initialized_cut(content, error) && current(content, error);
}
bool frontend_q3_content_pure_command(frontend_q3_content *content, char *out, size_t capacity, qa_error *error)
{
    if (!out || !capacity || !current(content, error) ||
        content->phase != FRONTEND_Q3_CONTENT_MEDIA_READY)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 CP requires completed real CGAME and UI media initialization");
    if (content->acquired_cgame && !frontend_q3_content_media_current(content, error)) return false;
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

bool frontend_q3_content_visit(const frontend_q3_content *content,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!visitor || !visitor->pool || !visitor->catalog || !visitor->view || !current(content, error)) return false;
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    return visitor->pool(visitor->context, qa_catalog_resources(content->catalog), error) &&
        visitor->catalog(visitor->context, content->catalog, error) &&
        visitor->view(visitor->context, qa_catalog_files(content->catalog), error) &&
        visitor->pool(visitor->context, qa_vfs_resources(content->mounts), error) &&
        visitor->view(visitor->context, content->mounts, error) &&
        visitor->pool(visitor->context, qa_vfs_resources(descriptor->content), error) &&
        visitor->catalog(visitor->context, qa_launch_instance_catalog(descriptor), error) &&
        visitor->view(visitor->context, descriptor->content, error);
}

typedef struct content_saved {
    uint32_t phase, selected, base, seat;
    uint64_t generation, epoch, receiver, service_owner, catalog, view, descriptor_view;
    qa_sha256_digest descriptor_identity;
    qa_buffer gamestate, references;
    qa_mount_id *order;
    size_t order_count;
    bool has_map, native_cgame, acquired_cgame;
    uint64_t map_pool, map_resource;
    qa_vfs_acquisition map_acquisition;
} content_saved;
static bool saved_blob(qa_source_save_io *io, qa_buffer *buffer)
{
    size_t limit = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &buffer->size, limit)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && buffer->size) {
        buffer->data = malloc(buffer->size);
        if (!buffer->data) return fail(io->error, QA_ERROR_MEMORY, "Retaining remote content field bytes");
    }
    return qa_source_save_bytes(io, buffer->data, buffer->size);
}
static bool saved_text(qa_source_save_io *io, char **text)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    size_t limit = io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1;
    if (!qa_source_save_count(io, &length, limit) || length == SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *text = malloc(length + 1);
        if (!*text) return fail(io->error, QA_ERROR_MEMORY, "Retaining remote map opening provenance");
        (*text)[length] = 0;
    }
    return qa_source_save_bytes(io, *text, length) && !memchr(*text, 0, length);
}
#define CONTENT_FIELD(kind, name) do { if (!qa_source_save_##kind(io, &saved->name)) return false; } while (0)
static bool saved_fields(qa_source_save_io *io, content_saved *saved)
{
    uint8_t magic[4] = {'Q','3','C','T'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q3CT", 4) ) return false;
    CONTENT_FIELD(u32, phase); CONTENT_FIELD(u32, selected); CONTENT_FIELD(u32, base); CONTENT_FIELD(u32, seat);
    CONTENT_FIELD(u64, generation); CONTENT_FIELD(u64, epoch); CONTENT_FIELD(u64, receiver); CONTENT_FIELD(u64, service_owner);
    CONTENT_FIELD(u64, catalog); CONTENT_FIELD(u64, view); CONTENT_FIELD(u64, descriptor_view);
    CONTENT_FIELD(bool, native_cgame);
    CONTENT_FIELD(bool, acquired_cgame);
    if ((saved->native_cgame && saved->acquired_cgame) ||
        ((saved->native_cgame || saved->acquired_cgame) && saved->phase != FRONTEND_Q3_CONTENT_MEDIA_READY)) return false;
    if (saved->phase > FRONTEND_Q3_CONTENT_MEDIA_READY || !saved->selected || !saved->base ||
        !saved->generation || !saved->epoch || !saved->receiver || !saved->service_owner ||
        !saved->catalog || !saved->view || !saved->descriptor_view ||
        !qa_source_save_bytes(io, &saved->descriptor_identity, sizeof(saved->descriptor_identity)) ||
        !saved_blob(io, &saved->gamestate) || !saved->gamestate.size ||
        !qa_source_save_count(io, &saved->order_count, QA_Q3_SEARCH_PATHS)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (saved->order_count > (io->input.size - io->offset) / sizeof(uint64_t)) return false;
        saved->order = calloc(saved->order_count ? saved->order_count : 1, sizeof(*saved->order));
        if (!saved->order) return fail(io->error, QA_ERROR_MEMORY, "Retaining original Q3 package inventory slots");
    }
    for (size_t i = 0; i < saved->order_count; ++i) {
        if (!qa_source_save_u64(io, &saved->order[i]) || !saved->order[i]) return false;
        for (size_t j = 0; j < i; ++j) if (saved->order[j] == saved->order[i]) return false;
    }
    CONTENT_FIELD(bool, has_map);
    if (saved->has_map != (saved->phase >= FRONTEND_Q3_CONTENT_PREPARED)) return false;
    if (saved->has_map) {
        CONTENT_FIELD(u64, map_pool); CONTENT_FIELD(u64, map_resource);
        if (!saved->map_pool || !saved->map_resource ||
            !qa_source_save_u64(io, &saved->map_acquisition.mount) || !saved->map_acquisition.mount ||
            !qa_source_save_u64(io, &saved->map_acquisition.resource_id) ||
            saved->map_acquisition.resource_id != saved->map_resource ||
            !saved_text(io, &saved->map_acquisition.path) || !saved->map_acquisition.path ||
            !saved_text(io, &saved->map_acquisition.lookup_path) || !saved->map_acquisition.lookup_path ||
            !saved_text(io, &saved->map_acquisition.link_source) ||
            !saved_text(io, &saved->map_acquisition.link_target)) return false;
    }
    return saved_blob(io, &saved->references) && saved->references.size;
}
#undef CONTENT_FIELD
static void saved_dispose(content_saved *saved)
{
    qa_buffer_free(&saved->gamestate); qa_buffer_free(&saved->references);
    qa_vfs_acquisition_dispose(&saved->map_acquisition); free(saved->order);
}
static bool initialized_cut(frontend_q3_content *content, qa_error *error)
{
    if (content->phase != FRONTEND_Q3_CONTENT_MEDIA_READY) return true;
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    if (content->acquired_cgame) {
        const application_native_q3_client_modules *modules = NULL;
        frontend_q3_content_role_receipt cgame = {0}, ui = {0};
        if (!content->modules_media_read ||
            !content->modules_media_read(content->connection, &modules, &cgame, &ui, error)) return false;
        const frontend_q3_content_role_receipt *roles[] = {&cgame, &ui};
        const qa_qvm_role kinds[] = {QA_QVM_CGAME, QA_QVM_UI};
        for (size_t i = 0; i < 2; ++i) {
            content_mount *mount;
            if (!module_receipt(content, modules, roles[i], kinds[i], &mount, error)) return false;
            if (!mount->pack.archive_path) continue;
            unsigned flag = kinds[i] == QA_QVM_CGAME ? QA_Q3_PAK_CGAME : QA_Q3_PAK_UI;
            bool marked = false;
            for (size_t j = 0; j < qa_q3_pak_reference_count(content->references); ++j) {
                qa_q3_pak_reference reference;
                if (!qa_q3_pak_reference_at(content->references, j, &reference)) return false;
                if (reference.pack == &mount->pack && (reference.flags & flag)) marked = true;
            }
            if (!marked) return fail(error, QA_ERROR_FORMAT, "Acquired client continuation lost its actual module package role");
        }
        return modules_current(content, modules, error) &&
            cgame.current(cgame.producer, &cgame, error) && ui.current(ui.producer, &ui, error);
    }
    if (content->native_cgame) {
        frontend_q3_content_native_receipt cgame = {0};
        frontend_q3_content_role_receipt ui = {0};
        if (!content->native_media_read || !content->native_media_read(content->connection, &cgame, &ui, error) ||
            !native_receipt(content, &cgame, error) || ui.role != QA_QVM_UI ||
            ui.receiver != content->receiver.receiver || ui.seat != content->receiver.seat ||
            !ui.service_owner || ui.configuration_generation != content->configuration_generation ||
            ui.connection_epoch != content->connection_epoch || !ui.descriptor ||
            ui.descriptor->content != descriptor->content || ui.descriptor->state != descriptor->state ||
            !qa_sha256_equal(&ui.descriptor->identity, &descriptor->identity) ||
            !ui.artifact || !ui.acquisition || !ui.artifact_view || !ui.current ||
            (ui.media_view_count && !ui.media_views) ||
            ui.acquisition->resource_id != qa_resource_id(ui.artifact) ||
            qa_resource_pool_find(qa_vfs_resources(ui.artifact_view), ui.acquisition->resource_id) != ui.artifact ||
            !ui.current(ui.producer, &ui, error) ||
            !qa_vfs_acquisition_retained(ui.artifact_view, ui.acquisition, error) ||
            !matching_view(content, ui.artifact_view, error)) return false;
        for (size_t i = 0; i < ui.media_view_count; ++i)
            if (!matching_view(content, ui.media_views[i], error)) return false;
        content_mount *mount = matching_mount(content, ui.artifact_view, ui.acquisition->mount, error);
        if (!mount) return false;
        if (mount->pack.archive_path) {
            bool marked = false;
            for (size_t i = 0; i < qa_q3_pak_reference_count(content->references); ++i) {
                qa_q3_pak_reference reference;
                if (!qa_q3_pak_reference_at(content->references, i, &reference)) return false;
                if (reference.pack == &mount->pack && (reference.flags & QA_Q3_PAK_UI)) marked = true;
            }
            if (!marked) return fail(error, QA_ERROR_FORMAT, "Compiled CGAME continuation lost its actual acquired UI package role");
        }
        return native_receipt(content, &cgame, error) && ui.current(ui.producer, &ui, error);
    }
    const qa_qvm_role roles[] = {QA_QVM_CGAME, QA_QVM_UI};
    for (size_t i = 0; i < 2; ++i) {
        qa_application_q3_role_receipt receipt;
        if (!qa_application_q3_role_receipt_read(content->application, content->receiver.receiver,
            roles[i], content->receiver.seat, &receipt, error) ||
            !qa_application_q3_role_receipt_current(content->application, &receipt) ||
            receipt.configuration_generation != content->configuration_generation ||
            receipt.connection_epoch != content->connection_epoch || !receipt.artifact || !receipt.acquisition ||
            !receipt.descriptor || receipt.descriptor->content != descriptor->content ||
            !qa_sha256_equal(&receipt.descriptor->identity, &descriptor->identity) ||
            (roles[i] == QA_QVM_CGAME && receipt.service_owner != content->receiver.service_owner) ||
            receipt.acquisition->resource_id != qa_resource_id(receipt.artifact) ||
            !qa_vfs_acquisition_retained(receipt.artifact_view, receipt.acquisition, error) ||
            !matching_view(content, receipt.artifact_view, error)) return false;
        content_mount *mount = matching_mount(content, receipt.artifact_view, receipt.acquisition->mount, error);
        if (!mount) return false;
        if (!mount->pack.archive_path) {
            if (content->pure) return fail(error, QA_ERROR_FORMAT, "Pure Q3 continuation lost its actual packed client module");
            continue;
        }
        unsigned role_flag = roles[i] == QA_QVM_CGAME ? QA_Q3_PAK_CGAME : QA_Q3_PAK_UI;
        bool marked = false;
        for (size_t j = 0; j < qa_q3_pak_reference_count(content->references); ++j) {
            qa_q3_pak_reference reference;
            if (!qa_q3_pak_reference_at(content->references, j, &reference)) return false;
            if (reference.pack == &mount->pack && (reference.flags & role_flag)) marked = true;
        }
        if (!marked) return fail(error, QA_ERROR_FORMAT, "Q3 continuation role flags differ from its genuine initialized artifact");
    }
    return true;
}
static bool reference_order_current(const frontend_q3_content *content, qa_error *error)
{
    size_t slot = 0;
    for (size_t i = 0; i < content->mount_count; ++i) {
        qa_vfs_mount_info info;
        if (!qa_vfs_mount_at(content->mounts, i, &info)) return false;
        if (!info.is_archive) continue;
        const content_mount *mount = NULL;
        for (size_t j = 0; j < content->mount_count; ++j)
            if (content->inventory[j].id == info.id) mount = content->inventory + j;
        qa_q3_pak_reference reference;
        if (!mount || !qa_q3_pak_reference_at(content->references, slot++, &reference) || reference.pack != &mount->pack)
            return fail(error, QA_ERROR_FORMAT, "Q3 reference order differs from its actual retained filesystem");
    }
    return slot == qa_q3_pak_reference_count(content->references) ||
        fail(error, QA_ERROR_FORMAT, "Q3 reference catalog differs from its mounted archive inventory");
}
static bool map_cut(const frontend_q3_content *content, const qa_resource *map,
    const qa_vfs_acquisition *receipt, qa_error *error)
{
    if (!map || !receipt->path || !receipt->lookup_path || !receipt->link_source || !receipt->link_target ||
        strcmp(receipt->path, content->map_path) || receipt->resource_id != qa_resource_id(map) ||
        qa_resource_pool_find(qa_vfs_resources(content->mounts), receipt->resource_id) != map)
        return fail(error, QA_ERROR_FORMAT, "Q3 map continuation changed its actual held resource or request");
    for (size_t i = 0; i < qa_vfs_read_count(content->mounts); ++i) {
        qa_vfs_read_reference read;
        if (!qa_vfs_read_at(content->mounts, i, &read)) return false;
        if (read.mount == receipt->mount && read.resource == map &&
            !strcmp(read.path, receipt->path) && !strcmp(read.lookup_path, receipt->lookup_path) &&
            !strcmp(read.link_source, receipt->link_source) && !strcmp(read.link_target, receipt->link_target))
            return qa_vfs_acquisition_retained(content->mounts, receipt, error);
    }
    return fail(error, QA_ERROR_FORMAT, "Q3 map continuation lacks its genuine retained journal opening");
}
bool frontend_q3_content_checkpoint(const frontend_q3_content *content,
    const qa_application_content_graph *graph, qa_buffer *out, qa_error *error)
{
    if (!graph || !out || out->data || out->size || !current(content, error) ||
        !initialized_cut((frontend_q3_content *)content, error) || !reference_order_current(content, error) ||
        !policy_current(content, content->mounts, error)) return false;
    const qa_launch_instance *descriptor = qa_launch_instance_lease_view(content->descriptor);
    content_saved saved = {.phase = content->phase, .selected = content->selected, .base = content->base,
        .native_cgame = content->native_cgame, .acquired_cgame = content->acquired_cgame,
        .seat = content->receiver.seat, .generation = content->configuration_generation,
        .epoch = content->connection_epoch, .receiver = content->receiver.receiver,
        .service_owner = content->receiver.service_owner,
        .catalog = qa_application_content_catalog_id(graph, content->catalog),
        .view = qa_application_content_view_id(graph, content->mounts),
        .descriptor_view = qa_application_content_view_id(graph, descriptor->content),
        .descriptor_identity = descriptor->identity, .order_count = content->mount_count, .has_map = content->map != NULL};
    saved.order = calloc(saved.order_count ? saved.order_count : 1, sizeof(*saved.order));
    size_t capacity = sizeof(qa_q3_gamestate) * 2 + 1024;
    saved.gamestate.data = malloc(capacity);
    bool ok = saved.order && saved.gamestate.data;
    if (!ok) fail(error, QA_ERROR_MEMORY, "Retaining genuine remote content continuation");
    qa_net_writer writer;
    if (ok) {
        qa_net_writer_init(&writer, saved.gamestate.data, capacity, error);
        ok = qa_q3_save_gamestate_fields(&writer, content->gamestate);
        saved.gamestate.size = qa_net_writer_size(&writer);
        for (size_t i = 0; i < saved.order_count; ++i) saved.order[i] = content->inventory[i].id;
    }
    if (ok && saved.has_map) {
        ok = qa_application_content_resource_id(graph, content->map, &saved.map_pool, &saved.map_resource) &&
            map_cut(content, content->map, &content->map_acquisition, error);
        if (ok) saved.map_acquisition = content->map_acquisition;
    }
    if (ok) ok = qa_q3_pak_references_checkpoint(content->references, &saved.references, error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, NULL, error) && saved_fields(&io, &saved) && qa_source_save_finish(&io, out);
    saved.map_acquisition = (qa_vfs_acquisition){0};
    qa_source_save_dispose(&io); saved_dispose(&saved);
    if (!ok && (!error || error->code == QA_OK))
        fail(error, QA_ERROR_FORMAT, "Remote content continuation leaves its actual graph and source field domains");
    return ok;
}
static bool restore(const frontend_q3_content_request *binding,
    qa_application_content_graph *graph, qa_bytes bytes, bool native_staged, bool modules_staged,
    frontend_q3_content **out, qa_error *error)
{
    if (!out || *out || !graph || !binding_current(binding, error)) return false;
    content_saved saved = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && saved_fields(&io, &saved) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && native_staged && (!binding->receiver.native_source ||
        binding->descriptor->selection.runtime != QA_PROGRAM_BUILTIN || binding->descriptor->artifact ||
        (saved.phase == FRONTEND_Q3_CONTENT_MEDIA_READY && !saved.native_cgame)))
        ok = fail(error, QA_ERROR_FORMAT, "Staged Q3 content import requires its actual compiled CLIENT completion variant");
    if (ok && modules_staged && (!binding->receiver.native_source || !binding->modules_media_read ||
        binding->descriptor->selection.runtime != QA_PROGRAM_BUILTIN || binding->descriptor->artifact ||
        (saved.phase == FRONTEND_Q3_CONTENT_MEDIA_READY && !saved.acquired_cgame)))
        ok = fail(error, QA_ERROR_FORMAT, "Staged acquired content import requires its actual native-parent module completion variant");
    qa_q3_gamestate *state = NULL; qa_catalog *catalog = NULL; qa_vfs *mounts = NULL;
    frontend_q3_content *content = NULL;
    if (ok && (saved.generation != binding->configuration_generation || saved.epoch != binding->connection_epoch ||
        saved.receiver != binding->receiver.receiver || saved.seat != binding->receiver.seat ||
        saved.service_owner != binding->receiver.service_owner ||
        !qa_sha256_equal(&saved.descriptor_identity, &binding->descriptor->identity) ||
        qa_application_content_view(graph, saved.descriptor_view) != binding->descriptor->content ||
        (binding->catalog && qa_application_content_catalog(graph, saved.catalog) != binding->catalog)))
        ok = fail(error, QA_ERROR_FORMAT, "Saved remote content differs from its real restored private source");
    if (ok) {
        state = malloc(sizeof(*state));
        if (!state) ok = fail(error, QA_ERROR_MEMORY, "Retaining restored decoded Q3 gamestate");
    }
    if (ok) {
        qa_net_reader reader; qa_net_reader_init(&reader, (qa_bytes){saved.gamestate.data, saved.gamestate.size}, error);
        ok = qa_q3_restore_gamestate_fields(&reader, state) && qa_net_reader_finish(&reader);
    }
    if (ok) ok = qa_application_content_retain_catalog(graph, saved.catalog, &catalog, error) &&
        qa_application_content_claim_view(graph, saved.view, &mounts, error);
    if (ok) {
        frontend_q3_content_request request = *binding;
        request.catalog = catalog; request.gamestate = state;
        ok = create(&request, &mounts, saved.order, saved.order_count, &content, error);
    }
    if (ok && (content->selected != saved.selected || content->base != saved.base))
        ok = fail(error, QA_ERROR_FORMAT, "Saved remote selected content differs from its genuine catalog");
    if (ok && saved.has_map) {
        const qa_resource *map = qa_application_content_resource(graph, saved.map_pool, saved.map_resource);
        if (!map || qa_application_content_pool(graph, saved.map_pool) != qa_vfs_resources(content->mounts) ||
            !map_cut(content, map, &saved.map_acquisition, error))
            ok = fail(error, QA_ERROR_FORMAT, "Saved Q3 map lacks its actual retained opening and immutable resource");
        else {
            qa_resource_retain((qa_resource *)map); content->map = (qa_resource *)map;
            content->map_acquisition = saved.map_acquisition; saved.map_acquisition = (qa_vfs_acquisition){0};
        }
    }
    if (ok) {
        qa_q3_pak_references *references = NULL;
        ok = qa_q3_pak_references_restore((qa_bytes){saved.references.data, saved.references.size}, content->packs,
            content->pack_count, (uint32_t)content->gamestate->checksum_feed, loose_random, NULL, &references, error);
        if (ok) {
            qa_q3_pak_references_destroy(content->references); content->references = references;
            content->phase = saved.phase; content->native_cgame = saved.native_cgame;
            content->acquired_cgame = saved.acquired_cgame;
            content->pending_native = (native_staged && saved.native_cgame) || (modules_staged && saved.acquired_cgame);
            ok = policy_current(content, content->mounts, error) && reference_order_current(content, error) &&
                (content->phase < FRONTEND_Q3_CONTENT_PUBLISHED || matching_view(content, binding->descriptor->content, error)) &&
                (content->pending_native || initialized_cut(content, error)) && source_current(content, error);
        }
    }
    free(state); qa_catalog_release(catalog); qa_vfs_destroy(mounts); saved_dispose(&saved);
    if (!ok) {
        frontend_q3_content_destroy(content);
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Unqualified remote content continuation fields");
        return false;
    }
    *out = content; return true;
}
bool frontend_q3_content_restore(const frontend_q3_content_request *binding,
    qa_application_content_graph *graph, qa_bytes bytes, frontend_q3_content **out, qa_error *error)
{ return restore(binding, graph, bytes, false, false, out, error); }
bool frontend_q3_content_restore_native_staged(const frontend_q3_content_request *binding,
    qa_application_content_graph *graph, qa_bytes bytes, frontend_q3_content **out, qa_error *error)
{ return restore(binding, graph, bytes, true, false, out, error); }
bool frontend_q3_content_restore_modules_staged(const frontend_q3_content_request *binding,
    qa_application_content_graph *graph, qa_bytes bytes, frontend_q3_content **out, qa_error *error)
{ return restore(binding, graph, bytes, false, true, out, error); }
bool frontend_q3_content_native_restore_pending(const frontend_q3_content *content)
{ return content && content->pending_native; }
bool frontend_q3_content_native_restore_read(const frontend_q3_content *content,
    frontend_q3_content_view *out, qa_error *error)
{
    if (!out || !content || !content->pending_native || !source_current(content, error))
        return fail(error, QA_ERROR_ARGUMENT, "Native content staging read requires its actual pending imported owner");
    content_view(content, out); return true;
}
bool frontend_q3_content_native_restore_finish(frontend_q3_content *content, qa_error *error)
{
    if (!content || !content->pending_native || !source_current(content, error) ||
        !initialized_cut(content, error) || !reference_order_current(content, error) ||
        !policy_current(content, content->mounts, error) || !source_current(content, error)) return false;
    content->pending_native = false;
    if (!content->acquired_cgame) content->receiver.initialized = true;
    return true;
}
bool frontend_q3_content_rebind(frontend_q3_content *content,
    const frontend_q3_content_request *binding, qa_error *error)
{
    if (!content || !binding_current(binding, error) ||
        content->configuration_generation != binding->configuration_generation ||
        content->connection_epoch != binding->connection_epoch || content->receiver.receiver != binding->receiver.receiver ||
        content->receiver.seat != binding->receiver.seat || content->receiver.service_owner != binding->receiver.service_owner ||
        !qa_sha256_equal(&qa_launch_instance_lease_view(content->descriptor)->identity, &binding->descriptor->identity))
        return fail(error, QA_ERROR_FORMAT, "Remote content rebind changes its genuine saved source cut");
    qa_launch_instance_lease *descriptor = NULL;
    if (!qa_launch_instance_retain_metadata(binding->descriptor, &descriptor, error)) return false;
    frontend_q3_content candidate = *content;
    candidate.descriptor = descriptor; candidate.application = binding->application; candidate.receiver = binding->receiver;
    candidate.connection = binding->connection; candidate.connection_current = binding->connection_current;
    candidate.native_media_read = binding->native_media_read;
    candidate.modules_media_read = binding->modules_media_read;
    if (!current(&candidate, error) || !initialized_cut(&candidate, error)) {
        qa_launch_instance_lease_release(descriptor); return false;
    }
    qa_launch_instance_lease_release(content->descriptor); content->descriptor = descriptor;
    content->application = binding->application; content->receiver = binding->receiver;
    content->connection = binding->connection; content->connection_current = binding->connection_current;
    content->native_media_read = binding->native_media_read;
    content->modules_media_read = binding->modules_media_read;
    return true;
}
