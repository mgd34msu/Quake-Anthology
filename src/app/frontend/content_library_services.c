#include "content_library_services.h"
#include "qa/json.h"
#include "qa/text.h"
#include "qa/archive.h"
#include "qa/downloads.h"
#include "qa/source_save.h"
#include "global_settings_storage.h"
#include "config_scripts.h"
#include <inttypes.h>
#include <stdio.h>
#include <limits.h>
#include <unicode/ucol.h>

typedef struct library_rows {
    qa_ui_row *rows;
    size_t count;
    uint64_t revision;
    char status[512];
} library_rows;
typedef struct addon_mapping { char *from, *to; } addon_mapping;
typedef struct addon_package {
    char *id, *title, *filename, *group, *url, *start, *game;
    char **tags;
    size_t tag_count;
    addon_mapping *mappings;
    size_t mapping_count;
    uint64_t bytes;
    char *unavailable;
    bool local;
} addon_package;
typedef struct addon_installed {
    char *directory, *product, *title, *group, *id, *start;
} addon_installed;
typedef struct addon_library {
    library_rows view;
    addon_package *packages;
    size_t package_count, selected;
    addon_installed *installed;
    size_t installed_count, selected_local;
    qa_fs_root *root;
    qa_http *http;
    qa_http_request_id request;
    qa_source_save_io response;
    qa_error response_error;
    bool response_ready;
    qa_downloads *downloads;
    qa_download_id download;
    size_t *plan, plan_count, plan_cursor;
    char scope[160];
    uint64_t nonce;
    bool canceled;
    const char *parent;
} addon_library;
struct frontend_content_library_services {
    frontend_seat *seat;
    qa_ui *ui;
    qa_input_seat *input;
    qa_seat_console *console;
    library_rows progress;
    library_rows demos, profiles;
    frontend_demo_service *demo;
    addon_library addons;
    frontend_content_library_selection selection;
    bool busy;
};
static bool bound(const frontend_content_library_services *owner)
{
    const frontend_seat *seat = owner ? owner->seat : NULL;
    const qa_frontend *frontend = seat ? seat->frontend : NULL;
    return frontend && frontend->seats && seat->id < frontend->options.seats &&
        seat == frontend->seats + seat->id && seat->ui == owner->ui &&
        seat->input == owner->input && seat->console == owner->console;
}
static void rows_clear(library_rows *view)
{
    for (size_t i = 0; i < view->count; ++i) {
        free((void *)view->rows[i].key);
        free((void *)view->rows[i].label);
        free((void *)view->rows[i].detail);
    }
    free(view->rows); view->rows = NULL; view->count = 0;
}
static bool text_copy(qa_bytes text, char **out, qa_error *error)
{
    size_t zeros = 0;
    for (size_t i = 0; i < text.size; ++i) zeros += text.data[i] == 0;
    if (text.size == SIZE_MAX || zeros > SIZE_MAX - text.size - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "Library text exceeds its retained extent");
    char *copy = malloc(text.size + zeros + 1);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Library entry text");
    size_t used = 0;
    for (size_t i = 0; i < text.size; ++i) {
        if (!text.data[i]) { copy[used++] = '\\'; copy[used++] = '0'; }
        else copy[used++] = (char)text.data[i];
    }
    copy[used] = 0; *out = copy; return true;
}
static const char *family(qa_game_family value)
{
    switch (value) {
    case QA_GAME_Q1: return "q1";
    case QA_GAME_Q2: return "q2";
    case QA_GAME_Q3: return "q3";
    }
    return "";
}
static bool progress_key(const qa_progress_event *event, char **out, qa_error *error)
{
    qa_buffer participant = {0}, identity = {0};
    bool okay = qa_json_quote(event->participant, &participant, error) &&
        qa_json_quote(event->event, &identity, error);
    size_t prefix = strlen(family(event->source)) + 6;
    if (okay && (participant.size > SIZE_MAX - prefix - 3 ||
        identity.size > SIZE_MAX - prefix - 3 - participant.size))
        okay = frontend_fail(error, QA_ERROR_MEMORY, "Progress identity exceeds its retained extent");
    char *key = okay ? malloc(prefix + participant.size + identity.size + 3) : NULL;
    if (okay && !key) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining progress entry identity");
    if (okay) {
        int length = snprintf(key, prefix + 1, "[\"%s\",", family(event->source));
        size_t used = (size_t)length;
        memcpy(key + used, participant.data, participant.size); used += participant.size;
        key[used++] = ','; memcpy(key + used, identity.data, identity.size); used += identity.size;
        key[used++] = ']'; key[used] = 0; *out = key;
    }
    qa_buffer_free(&participant); qa_buffer_free(&identity); return okay;
}
static bool progress_load(frontend_content_library_services *owner, qa_error *error)
{
    if (!bound(owner) || owner->busy)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Progress Library lost its returned physical seat");
    owner->busy = true;
    qa_player_progress *store = qa_application_player_progress(owner->seat->frontend->application);
    uint32_t launch_seat;
    bool okay = store && frontend_seat_launch_id_read(owner->seat->frontend, owner->seat->id, &launch_seat);
    if (!okay) frontend_fail(error, QA_ERROR_NOT_FOUND, "Player progress has no selected local profile");
    if (okay) okay = qa_player_progress_reload(store, error);
    char participant[32];
    int length = okay ? snprintf(participant, sizeof(participant), "local-seat:%" PRIu32, launch_seat) : 0;
    library_rows next = {0};
    size_t cursor = 0; qa_progress_event event;
    while (okay && qa_player_progress_next(store,
        (qa_bytes){(const uint8_t *)participant, (size_t)length}, &cursor, &event)) {
        if (next.count >= SIZE_MAX / sizeof(*next.rows) - 1) {
            okay = frontend_fail(error, QA_ERROR_MEMORY, "Progress rows exceed their retained extent"); break;
        }
        qa_ui_row *rows = realloc(next.rows, (next.count + 1) * sizeof(*rows));
        if (!rows) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining progress rows"); break; }
        next.rows = rows; qa_ui_row *row = rows + next.count++; *row = (qa_ui_row){.enabled = true};
        char *key = NULL, *label = NULL, *detail = NULL;
        qa_bytes subject = event.kind == QA_PROGRESS_ACHIEVEMENT ? event.value.award :
            event.kind == QA_PROGRESS_LEVEL_COMPLETED ? event.value.map : event.value.match.map;
        char description[160], score[32];
        const char *source = event.source == QA_GAME_Q1 ? "Q1" : event.source == QA_GAME_Q2 ? "Q2" : "Q3";
        if (event.kind == QA_PROGRESS_MATCH_COMPLETED) {
            okay = qa_format_number(event.value.match.score, score, error);
            if (okay) snprintf(description, sizeof(description), "%s · Match completed · Score %s", source, score);
        } else snprintf(description, sizeof(description), "%s · %s", source,
            event.kind == QA_PROGRESS_ACHIEVEMENT ? "Achievement earned" : "Level completed");
        if (okay) okay = progress_key(&event, &key, error) && text_copy(subject, &label, error) &&
            text_copy((qa_bytes){(const uint8_t *)description, strlen(description)}, &detail, error);
        row->key = key; row->label = label; row->detail = detail;
    }
    if (okay) {
        bool changed = next.count != owner->progress.count;
        for (size_t i = 0; !changed && i < next.count; ++i)
            changed = strcmp(next.rows[i].key, owner->progress.rows[i].key) != 0;
        next.revision = owner->progress.revision + changed;
        if (next.count) snprintf(next.status, sizeof(next.status), "%zu progress records", next.count);
        else snprintf(next.status, sizeof(next.status), "No recorded achievements or completed levels for this player.");
        rows_clear(&owner->progress); owner->progress = next;
    } else {
        rows_clear(&next);
        snprintf(owner->progress.status, sizeof(owner->progress.status), "Cannot read progress: %.*s",
            (int)sizeof(owner->progress.status) - 23,
            error && error->message[0] ? error->message : "Profile unavailable");
    }
    owner->busy = false; return okay;
}
static bool progress_entries(void *context, const qa_ui_row **out, size_t *count,
    uint64_t *revision, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !out || !count || !revision)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Progress entries require their actual Library owner");
    *out = owner->progress.rows; *count = owner->progress.count; *revision = owner->progress.revision;
    return true;
}
static const char *progress_status(void *context)
{ return ((frontend_content_library_services *)context)->progress.status; }
static bool progress_refresh(void *context, qa_error *error)
{ return progress_load(context, error); }
static bool progress_activate(void *context, const char *id, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !id)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Progress selection lost its actual Library owner");
    for (size_t i = 0; i < owner->progress.count; ++i) {
        const qa_ui_row *row = owner->progress.rows + i;
        if (!strcmp(id, row->key)) {
            snprintf(owner->progress.status, sizeof(owner->progress.status), "%s: %s", row->label, row->detail);
            return true;
        }
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Progress entry is no longer listed");
}
static const char addon_catalog_url[] = "https://www.quaddicted.com/api/v1/?q=%2Btags%3A%22game%3Dquake%22%20%2Btags%3A%22game_mode%3Dsingleplayer%22&fl=sha256,tags,urls,bytes,install";
static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1; char *out = malloc(size);
    if (out) memcpy(out, value, size);
    return out;
}
static bool json_text(const qa_json_document *doc, qa_json_id id, char **out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_json_string(doc, id, &text, error)) return false;
    if (memchr(text.data, 0, text.size)) {
        qa_buffer_free(&text);
        return frontend_fail(error, QA_ERROR_FORMAT, "Add-on metadata contains an embedded zero byte");
    }
    *out = (char *)text.data; return true;
}
static bool package_id_valid(const char *id)
{
    size_t size = strlen(id);
    if (!size || size > 64) return false;
    for (size_t i = 0; i < size; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') ||
            id[i] == '-' || id[i] == '_')) return false;
    return true;
}
static void package_clear(addon_package *package)
{
    free(package->id); free(package->title); free(package->filename); free(package->group);
    free(package->url); free(package->start); free(package->game); free(package->unavailable);
    if (package->tags) for (size_t i = 0; i < package->tag_count; ++i) free(package->tags[i]);
    free(package->tags);
    for (size_t i = 0; i < package->mapping_count; ++i) {
        free(package->mappings[i].from); free(package->mappings[i].to);
    }
    free(package->mappings); *package = (addon_package){0};
}
static void installed_clear(addon_library *library)
{
    for (size_t i = 0; i < library->installed_count; ++i) {
        addon_installed *value = library->installed + i;
        free(value->directory); free(value->product); free(value->title); free(value->group); free(value->id); free(value->start);
    }
    free(library->installed); library->installed = NULL; library->installed_count = 0;
}
static const char *tag_at(const addon_package *package, const char *name, size_t ordinal)
{
    size_t length = strlen(name);
    for (size_t i = 0; i < package->tag_count; ++i) {
        const char *tag = package->tags[i];
        if (!strncmp(tag, name, length) && tag[length] == '=') {
            if (!ordinal) return tag + length + 1;
            --ordinal;
        }
    }
    return NULL;
}
static bool has_tag(const addon_package *package, const char *tag)
{
    for (size_t i = 0; i < package->tag_count; ++i) if (!strcmp(package->tags[i], tag)) return true;
    return false;
}
static bool suffix(const char *value, const char *end)
{
    size_t length = strlen(value), count = strlen(end);
    return length >= count && !strcmp(value + length - count, end);
}
static bool prefix_path(const char *value, char **out, qa_error *error)
{
    const char *text = value;
    if (!strncmp(text, "{base}", 6)) { text += 6; if (*text == '/') ++text; }
    if (*text == '/') ++text;
    size_t length = strlen(text); bool slash = length && text[length - 1] == '/';
    char *copy = copy_string(text);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored extraction mapping");
    if (slash) copy[--length] = 0;
    char *normalized = length ? qa_vfs_normalize_path(copy, error) : copy_string("");
    free(copy);
    if (!normalized) return false;
    if (slash && length) {
        size_t size = strlen(normalized); char *expanded = realloc(normalized, size + 2);
        if (!expanded) { free(normalized); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining extraction prefix"); }
        normalized = expanded; normalized[size] = '/'; normalized[size + 1] = 0;
    }
    *out = normalized; return true;
}
static bool package_mapping(const qa_json_document *doc, qa_json_id install,
    addon_package *package, qa_error *error)
{
    if (qa_json_type(doc, install) != QA_JSON_OBJECT)
        return frontend_fail(error, QA_ERROR_FORMAT, "No authored installation metadata");
    qa_json_id mappings = qa_json_get(doc, install, "extractmapping");
    if (qa_json_type(doc, mappings) == QA_JSON_OBJECT) {
        size_t count = qa_json_size(doc, mappings);
        if (count > SIZE_MAX / sizeof(*package->mappings))
            return frontend_fail(error, QA_ERROR_MEMORY, "Extraction mappings exceed their retained extent");
        package->mappings = count ? calloc(count, sizeof(*package->mappings)) : NULL;
        if (count && !package->mappings) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining extraction mappings");
        for (size_t i = 0; i < count; ++i) {
            qa_json_id key = qa_json_key_at(doc, mappings, i), value = qa_json_at(doc, mappings, i);
            char *from = NULL, *to = NULL; addon_mapping *mapping = package->mappings + package->mapping_count++;
            bool okay = json_text(doc, key, &from, error) && prefix_path(from, &mapping->from, error);
            if (okay && qa_json_type(doc, value) != QA_JSON_NULL)
                okay = json_text(doc, value, &to, error) && prefix_path(to, &mapping->to, error);
            free(from); free(to); if (!okay) return false;
        }
        return true;
    }
    char *to = NULL;
    if (!json_text(doc, qa_json_get(doc, install, "extract"), &to, error))
        return frontend_fail(error, QA_ERROR_FORMAT, "No authored extraction mapping");
    package->mappings = calloc(1, sizeof(*package->mappings));
    if (!package->mappings) { free(to); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining extraction mapping"); }
    package->mapping_count = 1; package->mappings[0].from = copy_string("");
    bool okay = package->mappings[0].from && prefix_path(to, &package->mappings[0].to, error);
    free(to); return okay;
}
static bool game_name(const addon_package *package, char **out, char **unavailable, qa_error *error)
{
    bool selected = false;
    *out = copy_string("id1");
    if (!*out) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining add-on game directory");
    for (size_t i = 0;; ++i) {
        const char *line = tag_at(package, "commandline", i); if (!line) break;
        while (*line) {
            while (*line && (unsigned char)*line <= 32) ++line;
            const char *start = line; while (*line && (unsigned char)*line > 32) ++line;
            size_t size = (size_t)(line - start);
            if ((size == 9 && !memcmp(start, "-hipnotic", size)) ||
                (size == 6 && (!memcmp(start, "-rogue", size) || !memcmp(start, "-quoth", size)))) {
                free(*unavailable); *unavailable = copy_string("Requires a source gameplay option not yet supported by this installer");
                if (!*unavailable) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining package availability");
            }
            if (size != 5 || memcmp(start, "-game", 5)) continue;
            while (*line && (unsigned char)*line <= 32) ++line;
            start = line; while (*line && (unsigned char)*line > 32) ++line; size = (size_t)(line - start);
            bool valid = size > 0;
            for (size_t j = 0; valid && j < size; ++j) {
                unsigned char byte = (unsigned char)start[j];
                valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                    (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '+' || byte == '-';
            }
            if (valid && !selected) {
                char *game = malloc(size + 1);
                if (!game) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored game directory");
                memcpy(game, start, size); game[size] = 0; free(*out); *out = game;
                selected = true;
            }
        }
    }
    return true;
}
static bool catalog_parse(addon_library *library, qa_bytes bytes, qa_error *error)
{
    qa_json_document *doc = NULL;
    if (!qa_json_parse(bytes, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc);
    bool okay = qa_json_type(doc, root) == QA_JSON_ARRAY;
    if (!okay) frontend_fail(error, QA_ERROR_FORMAT, "Invalid Quaddicted catalog");
    size_t count = okay ? qa_json_size(doc, root) : 0, used = 0;
    addon_package *packages = count && count <= SIZE_MAX / sizeof(*packages) ? calloc(count, sizeof(*packages)) : NULL;
    if (count && !packages) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining Quaddicted packages");
    for (size_t i = 0; okay && i < count; ++i) {
        addon_package package = {0}; qa_json_id row = qa_json_at(doc, root, i), tags = qa_json_get(doc, row, "tags");
        if (qa_json_type(doc, row) != QA_JSON_OBJECT || qa_json_type(doc, tags) != QA_JSON_ARRAY) {
            okay = frontend_fail(error, QA_ERROR_FORMAT, "Invalid Quaddicted string list"); break;
        }
        package.tag_count = qa_json_size(doc, tags);
        package.tags = package.tag_count <= SIZE_MAX / sizeof(*package.tags) ? calloc(package.tag_count, sizeof(*package.tags)) : NULL;
        if (package.tag_count && !package.tags) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining package tags");
        for (size_t j = 0; okay && j < package.tag_count; ++j)
            okay = json_text(doc, qa_json_at(doc, tags, j), package.tags + j, error);
        const char *filename = tag_at(&package, "filename", 0);
        if (!okay || !has_tag(&package, "game=quake") || !has_tag(&package, "game_mode=singleplayer") ||
            !filename || !suffix(filename, ".zip")) { package_clear(&package); continue; }
        okay = json_text(doc, qa_json_get(doc, row, "sha256"), &package.id, error) &&
            qa_json_u64(doc, qa_json_get(doc, row, "bytes"), &package.bytes, error);
        if (okay && (!package.bytes || package.bytes > UINT64_C(9007199254740991) ||
            !package_id_valid(package.id)))
            okay = frontend_fail(error, QA_ERROR_FORMAT, "Invalid Quaddicted package identity");
        qa_json_id urls = qa_json_get(doc, row, "urls");
        if (okay && urls != QA_JSON_NONE && qa_json_type(doc, urls) != QA_JSON_ARRAY)
            okay = frontend_fail(error, QA_ERROR_FORMAT, "Invalid Quaddicted URL list");
        char prefix[160]; if (okay) snprintf(prefix, sizeof(prefix), "https://www.quaddicted.com/files/by-sha256/%.2s/%s/", package.id, package.id);
        for (size_t j = 0; okay && urls != QA_JSON_NONE && j < qa_json_size(doc, urls); ++j) {
            char *url = NULL; okay = json_text(doc, qa_json_at(doc, urls, j), &url, error);
            if (okay && !package.url && !strncmp(url, prefix, strlen(prefix))) { package.url = url; url = NULL; }
            free(url);
        }
        if (okay && !package.url) { package_clear(&package); continue; }
        if (okay) {
            package.filename = copy_string(filename);
            package.title = copy_string(tag_at(&package, "title", 0) ? tag_at(&package, "title", 0) : filename);
            const char *group = tag_at(&package, "release_group", 0);
            package.group = copy_string(group ? group : filename);
            if (!group && package.group) package.group[strlen(package.group) - 4] = 0;
            okay = package.filename && package.title && package.group;
            if (!okay) frontend_fail(error, QA_ERROR_MEMORY, "Retaining package presentation");
        }
        if (okay) okay = game_name(&package, &package.game, &package.unavailable, error);
        for (size_t j = 0; okay && !package.start; ++j) {
            const char *start = tag_at(&package, "startmap", j); if (!start) break;
            bool valid = !strstr(start, "..");
            for (size_t k = 0; valid && start[k]; ++k) {
                unsigned char byte = (unsigned char)start[k];
                valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                    (byte >= '0' && byte <= '9') || strchr("_+./-", byte) != NULL;
            }
            if (valid) { package.start = copy_string(start); if (!package.start) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored start map"); }
        }
        if (okay) {
            qa_error mapping_error = {0};
            if (!package_mapping(doc, qa_json_get(doc, row, "install"), &package, &mapping_error)) {
                if (mapping_error.code == QA_ERROR_MEMORY) { if (error) *error = mapping_error; okay = false; }
                else { free(package.unavailable); package.unavailable = copy_string(mapping_error.message); if (!package.unavailable) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining unavailable package reason"); }
            }
        }
        if (okay) packages[used++] = package; else package_clear(&package);
    }
    qa_json_destroy(doc);
    if (!okay) { for (size_t i = 0; i < used; ++i) package_clear(packages + i); free(packages); return false; }
    char selected[65] = {0};
    if (library->selected < library->package_count) snprintf(selected, sizeof(selected), "%s", library->packages[library->selected].id);
    for (size_t i = 0; i < library->package_count; ++i) package_clear(library->packages + i);
    free(library->packages); library->packages = packages; library->package_count = used; library->selected = SIZE_MAX;
    for (size_t i = 0; i < used; ++i) if (!strcmp(selected, packages[i].id)) library->selected = i;
    return true;
}
static bool root_read(qa_fs_root *root, const char *path, qa_buffer *out, qa_error *error)
{
    qa_fs_file *file = NULL; qa_fs_identity identity;
    if (!qa_fs_root_file_open(root, path, &file, &identity, error)) return false;
    bool okay = qa_fs_file_read_snapshot(file, &identity, out, error);
    qa_fs_file_close(file); return okay;
}
static bool addon_prepare(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons;
    if (!bound(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-ons lost their actual physical Library owner");
    qa_http *http = frontend_tools_http(owner->seat->frontend);
    qa_settings_store settings = frontend_global_settings_storage_user_store(owner->seat->frontend->global_settings_storage);
    qa_fs_root *root = qa_vfs_mount_root(settings.vfs, settings.mount);
    if (!http || !root) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Add-ons require the actual user content directory and HTTP service");
    if (library->root && (!qa_fs_root_same_object(library->root, root) || library->http != http))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-ons changed their actual user directory or HTTP owner");
    if (!library->root) {
        qa_fs_root_retain(root); library->root = root; library->http = http;
        const qa_product *classic = qa_catalog_find(qa_application_catalog(owner->seat->frontend->application), "q1-classic-id1");
        library->parent = classic && classic->availability == QA_CONTENT_INSTALLED ? "q1" : "q1/rerelease";
    }
    return qa_fs_root_create_directory(root, ".addons", error);
}
static bool managed_name(const char *name)
{
    if (strncmp(name, "qd_", 3) || !name[3]) return false;
    for (const char *p = name + 3; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    return true;
}
static bool installed_load(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons; addon_library next = {0};
    qa_catalog *catalog = qa_application_catalog(owner->seat->frontend->application);
    char selected[512] = {0};
    if (library->selected_local < library->installed_count)
        snprintf(selected, sizeof(selected), "%s", library->installed[library->selected_local].directory);
    bool okay = true;
    for (size_t i = 0; okay && i < qa_catalog_count(catalog); ++i) {
        const qa_product *product = qa_catalog_at(catalog, i);
        if (!product || product->family != QA_GAME_Q1 || product->builtin ||
            !managed_name(product->campaign) || product->availability != QA_CONTENT_INSTALLED) continue;
        size_t length = strlen(product->directory);
        if (length > SIZE_MAX - 19) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Managed add-on directory exceeds its retained extent"); break; }
        char *path = malloc(length + 19);
        if (!path) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining managed add-on path"); break; }
        snprintf(path, length + 19, "%s/.quaddicted.json", product->directory);
        qa_fs_entry_kind kind; okay = qa_fs_root_status(library->root, path, &kind, NULL, error);
        qa_buffer bytes = {0}; qa_json_document *doc = NULL; addon_installed value = {0};
        if (okay && kind == QA_FS_REGULAR) {
            qa_error metadata = {0};
            bool valid = root_read(library->root, path, &bytes, &metadata) &&
                qa_json_parse((qa_bytes){bytes.data, bytes.size}, &doc, &metadata);
            qa_json_id row = valid ? qa_json_root(doc) : QA_JSON_NONE;
            qa_json_id package_id = valid ? qa_json_get(doc, row, "id") : QA_JSON_NONE;
            if (valid && package_id == QA_JSON_NONE) package_id = qa_json_get(doc, row, "sha256");
            if (valid) valid = json_text(doc, package_id, &value.id, &metadata) &&
                json_text(doc, qa_json_get(doc, row, "title"), &value.title, &metadata) &&
                json_text(doc, qa_json_get(doc, row, "group"), &value.group, &metadata);
            qa_json_id start = valid ? qa_json_get(doc, row, "start") : QA_JSON_NONE;
            if (valid && qa_json_type(doc, start) != QA_JSON_NULL) valid = json_text(doc, start, &value.start, &metadata);
            if (valid) valid = package_id_valid(value.id);
            if (valid) {
                value.directory = copy_string(product->directory);
                value.product = copy_string(product->key);
                addon_installed *entries = next.installed_count < SIZE_MAX / sizeof(*entries) - 1 ?
                    realloc(next.installed, (next.installed_count + 1) * sizeof(*entries)) : NULL;
                if (entries) next.installed = entries;
                if (!value.directory || !value.product || !entries) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining installed add-ons");
                else { next.installed[next.installed_count++] = value; value = (addon_installed){0}; }
            } else if (metadata.code == QA_ERROR_MEMORY || metadata.code == QA_ERROR_IO) { if (error) *error = metadata; okay = false; }
        }
        free(value.directory); free(value.product); free(value.title); free(value.group); free(value.id); free(value.start);
        qa_json_destroy(doc); qa_buffer_free(&bytes); free(path);
    }
    if (!okay) { installed_clear(&next); return false; }
    installed_clear(library); library->installed = next.installed; library->installed_count = next.installed_count;
    library->selected_local = SIZE_MAX;
    for (size_t i = 0; i < library->installed_count; ++i)
        if (!strcmp(selected, library->installed[i].directory)) library->selected_local = i;
    return true;
}
static bool row_add(library_rows *view, const char *key, const char *label,
    const char *detail, bool enabled, qa_error *error)
{
    if (view->count >= SIZE_MAX / sizeof(*view->rows) - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "Library rows exceed their retained extent");
    qa_ui_row *rows = realloc(view->rows, (view->count + 1) * sizeof(*rows));
    if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Library rows");
    view->rows = rows; qa_ui_row *row = rows + view->count++; *row = (qa_ui_row){.enabled = enabled};
    row->key = copy_string(key); row->label = copy_string(label); row->detail = copy_string(detail ? detail : "");
    return (row->key && row->label && row->detail) || frontend_fail(error, QA_ERROR_MEMORY, "Retaining Library entry");
}
static addon_installed *installed_group(addon_library *library, const char *group)
{
    for (size_t i = 0; i < library->installed_count; ++i)
        if (!strcmp(library->installed[i].group, group)) return library->installed + i;
    return NULL;
}
static bool addon_rows(addon_library *library, qa_error *error)
{
    library_rows next = {0}; bool okay = true;
    if (library->selected_local < library->installed_count) {
        addon_installed *local = library->installed + library->selected_local;
        snprintf(library->scope, sizeof(library->scope), "installed:%s", local->directory);
        okay = row_add(&next, "local-play", "Play", local->title, true, error) &&
            row_add(&next, "local-remove", "Remove installed add-on", local->title, true, error) &&
            row_add(&next, "back", "Back to add-ons", "", true, error);
    } else if (library->selected < library->package_count) {
        addon_package *package = library->packages + library->selected;
        addon_installed *installed = installed_group(library, package->group);
        snprintf(library->scope, sizeof(library->scope), "package:%s", package->id);
        if (installed) okay = row_add(&next, "play", "Play", installed->title, true, error) &&
            row_add(&next, "remove", "Remove installed add-on", "Only this managed copy", true, error);
        if (okay && (!installed || strcmp(installed->id, package->id))) {
            char detail[512]; snprintf(detail, sizeof(detail), "%s — %" PRIu64 " KiB", package->title,
                package->bytes / 1024 + (package->bytes % 1024 != 0));
            okay = row_add(&next, "install", installed ? "Update" : "Install",
                package->unavailable ? package->unavailable : detail, package->unavailable == NULL, error);
        }
        if (okay) okay = row_add(&next, "back", "Back to add-ons", "", true, error);
    } else {
        snprintf(library->scope, sizeof(library->scope), "catalog");
        for (size_t i = 0; okay && i < library->installed_count; ++i) {
            addon_installed *installed = library->installed + i; bool listed = false;
            for (size_t j = 0; j < library->package_count; ++j)
                if (!library->packages[j].local && !strcmp(installed->id, library->packages[j].id)) { listed = true; break; }
            if (listed) continue;
            char key[512]; snprintf(key, sizeof(key), "local:%s", installed->directory);
            okay = row_add(&next, key, installed->title, "Installed — play", true, error);
        }
        for (size_t i = 0; okay && i < library->package_count; ++i) {
            addon_package *package = library->packages + i;
            if (package->local) continue;
            okay = row_add(&next, package->id, package->title,
                installed_group(library, package->group) ? "Installed" : "Quaddicted", true, error);
        }
    }
    if (!okay) { rows_clear(&next); return false; }
    bool changed = next.count != library->view.count;
    for (size_t i = 0; !changed && i < next.count; ++i) changed = strcmp(next.rows[i].key, library->view.rows[i].key) != 0;
    next.revision = library->view.revision + changed; memcpy(next.status, library->view.status, sizeof(next.status));
    rows_clear(&library->view); library->view = next; return true;
}
static bool catalog_headers(void *context, qa_http_request_id request, const qa_http_response *response, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || request != owner->addons.request)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Quaddicted response lost its actual request owner");
    if (response->status >= 300 && response->status < 400) return true;
    return response->status == 200 || frontend_fail(error, QA_ERROR_IO, "Quaddicted catalog request was rejected");
}
static bool catalog_body(void *context, qa_http_request_id request, const qa_http_response *response,
    qa_bytes bytes, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (response->status >= 300 && response->status < 400) return true;
    if (!bound(owner) || request != owner->addons.request)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Quaddicted bytes lost their actual request owner");
    bool okay = qa_source_save_bytes(&owner->addons.response, (void *)bytes.data, bytes.size);
    if (!okay && error) *error = owner->addons.response_error;
    return okay;
}
static void catalog_complete(void *context, qa_http_request_id request,
    const qa_http_response *response, const qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (request != library->request) return;
    if (error) library->response_error = *error;
    else if (response->status != 200) qa_error_set(&library->response_error, QA_ERROR_IO, 0, "Quaddicted returned HTTP %u", response->status);
    library->request = 0; library->response_ready = true;
}
static bool addons_refresh(void *context, qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (!addon_prepare(owner, error)) return false;
    if (library->request || library->response_ready || library->plan_count) return true;
    if (!qa_application_rediscover(owner->seat->frontend->application, true, error) ||
        !installed_load(owner, error) || !addon_rows(library, error)) return false;
    qa_source_save_dispose(&library->response); library->response_error = (qa_error){0};
    library->response_ready = false; library->canceled = false;
    if (!qa_source_save_writer(&library->response, NULL, &library->response_error)) return false;
    qa_http_request request = {.url = addon_catalog_url, .timeout_ms = 120000, .connect_timeout_ms = 10000,
        .maximum_response_bytes = UINT64_C(67108864), .maximum_redirects = 5,
        .callbacks = {owner, catalog_headers, catalog_body, catalog_complete}};
    if (!qa_http_submit(library->http, &request, &library->request, error)) return false;
    snprintf(library->view.status, sizeof(library->view.status), "Reading Quaddicted catalog…"); return true;
}
static bool relative_path(const char *first, const char *second, char **out, qa_error *error)
{
    size_t a = strlen(first), b = strlen(second);
    if (a > SIZE_MAX - b - 2) return frontend_fail(error, QA_ERROR_MEMORY, "Add-on path exceeds its retained extent");
    char *path = malloc(a + b + 2);
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining add-on path");
    memcpy(path, first, a); path[a] = '/'; memcpy(path + a + 1, second, b + 1);
    *out = qa_vfs_normalize_path(path, error); free(path); return *out != NULL;
}
static bool cache_name(const addon_package *package, char out[78])
{
    if (!package_id_valid(package->id)) return false;
    int size = snprintf(out, 78, ".addons/%s.zip", package->id);
    return size > 0 && size < 78;
}
static bool archive_admit(qa_fs_root *root, const char *path, qa_archive **out, qa_error *error)
{
    qa_fs_file *file = NULL; qa_fs_identity identity;
    if (!qa_fs_root_file_open(root, path, &file, &identity, error)) return false;
    bool okay = qa_archive_open_retained(file, &identity, QA_ARCHIVE_ZIP, out, error);
    qa_fs_file_close(file); return okay;
}
static bool cached_package(addon_library *library, const addon_package *package, bool *found, qa_error *error)
{
    char path[78]; *found = false;
    if (!cache_name(package, path)) return frontend_fail(error, QA_ERROR_FORMAT, "Invalid add-on package identity");
    qa_fs_entry_kind kind; qa_fs_identity identity;
    if (!qa_fs_root_status(library->root, path, &kind, &identity, error)) return false;
    if (kind == QA_FS_MISSING) return true;
    if (kind != QA_FS_REGULAR || qa_fs_identity_size(&identity) != package->bytes)
        return qa_fs_root_remove(library->root, path, error);
    if (package->bytes > SIZE_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "Add-on package exceeds the native archive extent");
    *found = true; return true;
}
static bool download_permit(void *context, const qa_download_request *request, const char *url, qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (!bound(owner) || library->canceled || library->plan_cursor >= library->plan_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on download lost its actual installation plan");
    const addon_package *package = library->packages + library->plan[library->plan_cursor]; char path[78];
    bool same_url = url && package->url ? !strcmp(url, package->url) : url == package->url;
    return (cache_name(package, path) && !strcmp(request->path, path) && same_url &&
        request->exact_length && request->expected_bytes == package->bytes) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on download differs from its actual package identity");
}
static bool download_inspect(void *context, const char *path, qa_fs_stage *stage, uint64_t bytes, qa_error *error)
{
    (void)path; (void)bytes; frontend_content_library_services *owner = context;
    if (!bound(owner) || owner->addons.canceled)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on inspection lost its actual installation owner");
    qa_fs_stage_mapping *mapping = NULL; qa_archive *archive = NULL;
    bool okay = qa_fs_stage_map(stage, &mapping, error) &&
        qa_archive_open_memory(qa_fs_stage_mapping_bytes(mapping), QA_ARCHIVE_ZIP, &archive, error);
    qa_archive_close(archive); qa_fs_stage_unmap(mapping); return okay;
}
static bool download_cached(void *context, const char *path, qa_error *error)
{
    (void)path; frontend_content_library_services *owner = context;
    return (bound(owner) && !owner->addons.canceled) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on cache publication lost its actual owner");
}
static void download_changed(void *context, const qa_download_view *view)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    const char *title = library->selected < library->package_count ? library->packages[library->selected].title : "add-on";
    if (view->state == QA_DOWNLOAD_RECEIVING)
        snprintf(library->view.status, sizeof(library->view.status), "Downloading %.240s: %" PRIu64 "/%" PRIu64 " bytes", title, view->received, view->limit);
    else if (view->state == QA_DOWNLOAD_FAILED)
        snprintf(library->view.status, sizeof(library->view.status), "%s", view->failure.message);
}
static bool version_compare(const char *a, const char *b, int *out, qa_error *error)
{
    size_t left = strlen(a), right = strlen(b);
    if (left > INT32_MAX || right > INT32_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "Dependency version exceeds its Unicode collation extent");
    UErrorCode status = U_ZERO_ERROR; UCollator *collator = ucol_open("en", &status);
    if (U_SUCCESS(status)) ucol_setAttribute(collator, UCOL_NUMERIC_COLLATION, UCOL_ON, &status);
    if (U_SUCCESS(status)) ucol_setAttribute(collator, UCOL_NORMALIZATION_MODE, UCOL_ON, &status);
    UCollationResult result = UCOL_EQUAL;
    if (U_SUCCESS(status)) result = ucol_strcollUTF8(collator, a, (int32_t)left, b, (int32_t)right, &status);
    ucol_close(collator);
    if (U_FAILURE(status)) return frontend_fail(error,
        status == U_MEMORY_ALLOCATION_ERROR ? QA_ERROR_MEMORY : QA_ERROR_UNSUPPORTED,
        "English numeric dependency collation is unavailable");
    *out = result < UCOL_EQUAL ? -1 : result > UCOL_EQUAL ? 1 : 0; return true;
}
static bool dependency_push(size_t **indices, size_t *count, size_t index, qa_error *error)
{
    if (*count >= SIZE_MAX / sizeof(**indices) - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "Add-on dependency list exceeds its retained extent");
    size_t *next = realloc(*indices, (*count + 1) * sizeof(*next));
    if (!next) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining add-on dependencies");
    *indices = next; next[(*count)++] = index; return true;
}
static bool version_word(const char *text, size_t size)
{
    if (!size) return false;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("_.+-", c))) return false;
    }
    return true;
}
static bool dependency_list(const addon_library *library, size_t index, size_t **out, size_t *count, qa_error *error)
{
    const addon_package *package = library->packages + index;
    const char *dependency = tag_at(package, "dependency", 0);
    if (dependency) {
        for (size_t i = 0; (dependency = tag_at(package, "dependency", i)) != NULL; ++i) {
            size_t match = SIZE_MAX;
            for (size_t j = 0; j < library->package_count; ++j) {
                const addon_package *candidate = library->packages + j; size_t length = strlen(dependency);
                if (!strcmp(candidate->id, dependency) || !strcmp(candidate->filename, dependency) ||
                    (strlen(candidate->filename) == length + 4 && !memcmp(candidate->filename, dependency, length) && suffix(candidate->filename, ".zip"))) { match = j; break; }
            }
            if (match == SIZE_MAX) { qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Missing dependency: %s", dependency); return false; }
            if (!dependency_push(out, count, match, error)) return false;
        }
        return true;
    }
    for (size_t i = 0;; ++i) {
        const char *expression = tag_at(package, "depends", i); if (!expression) break;
        const char *cursor = expression; bool any = false;
        while ((cursor = strchr(cursor, '\'')) != NULL) {
            const char *name = ++cursor;
            while (*cursor && *cursor != '\'' && *cursor != '=' && *cursor != '<' && *cursor != '>') ++cursor;
            size_t name_size = (size_t)(cursor - name); const char *operation = cursor;
            if (!name_size || !*cursor || !strchr("=<>", *cursor)) continue;
            ++cursor; if (*cursor == '=') ++cursor; size_t operation_size = (size_t)(cursor - operation);
            const char *version = cursor; while (*cursor && *cursor != '\'') ++cursor;
            if (*cursor != '\'' || cursor == version) continue;
            if (!version_word(name, name_size) || !version_word(version, (size_t)(cursor - version)) ||
                (*operation == '=' && operation_size != 1)) { ++cursor; continue; }
            char *expected = malloc((size_t)(cursor - version) + 1);
            if (!expected) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining dependency version");
            memcpy(expected, version, (size_t)(cursor - version)); expected[cursor - version] = 0;
            size_t match = SIZE_MAX;
            for (size_t j = 0; j < library->package_count && match == SIZE_MAX; ++j) {
                for (size_t k = 0;; ++k) {
                    const char *provided = tag_at(library->packages + j, "provides", k); if (!provided) break;
                    if (*provided == '\'') ++provided;
                    const char *equal = strchr(provided, '=');
                    if (!equal || (size_t)(equal - provided) != name_size || memcmp(provided, name, name_size)) continue;
                    char *actual = copy_string(equal + 1);
                    if (!actual) { free(expected); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining provided version"); }
                    size_t length = strlen(actual); if (length && actual[length - 1] == '\'') actual[length - 1] = 0;
                    int compared = 0; bool compared_okay = version_compare(actual, expected, &compared, error); free(actual);
                    if (!compared_okay) { free(expected); return false; }
                    if ((*operation == '=' && operation_size == 1 && compared == 0) ||
                        (*operation == '>' && (compared > 0 || (operation_size == 2 && compared == 0))) ||
                        (*operation == '<' && (compared < 0 || (operation_size == 2 && compared == 0)))) { match = j; break; }
                }
            }
            free(expected);
            if (match == SIZE_MAX) { qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Missing dependency: %s", expression); return false; }
            if (!dependency_push(out, count, match, error)) return false;
            any = true; ++cursor;
        }
        if (!any) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Unsupported dependency requirement: %s", expression); return false; }
    }
    return true;
}
static bool plan_create(addon_library *library, size_t selected, qa_error *error)
{
    typedef struct visit { size_t index, *dependencies, count, cursor; } visit;
    visit *stack = calloc(library->package_count, sizeof(*stack));
    uint8_t *states = calloc(library->package_count, 1); size_t *plan = calloc(library->package_count, sizeof(*plan));
    if (!stack || !states || !plan) { free(stack); free(states); free(plan); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual add-on installation plan"); }
    size_t depth = 1, count = 0; stack[0].index = selected; bool okay = true;
    while (okay && depth) {
        visit *frame = stack + depth - 1; const addon_package *package = library->packages + frame->index;
        if (!states[frame->index]) {
            if (package->unavailable) { qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "%s: %s", package->title, package->unavailable); okay = false; break; }
            states[frame->index] = 1;
            if (!dependency_list(library, frame->index, &frame->dependencies, &frame->count, error)) { okay = false; break; }
        }
        if (frame->cursor < frame->count) {
            size_t next = frame->dependencies[frame->cursor++];
            if (states[next] == 1) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Cyclic add-on dependency: %s", library->packages[next].title); okay = false; break; }
            if (!states[next]) stack[depth++] = (visit){.index = next};
        } else { states[frame->index] = 2; plan[count++] = frame->index; free(frame->dependencies); frame->dependencies = NULL; --depth; }
    }
    for (size_t i = 0; i < depth; ++i) free(stack[i].dependencies);
    free(stack); free(states);
    if (!okay) { free(plan); return false; }
    free(library->plan); library->plan = plan; library->plan_count = count; library->plan_cursor = 0;
    library->canceled = false; return true;
}
static bool downloads_prepare(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons;
    if (!addon_prepare(owner, error)) return false;
    if (library->downloads) return true;
    qa_download_options options = {.jobs = 1, .maximum_pending_bytes = UINT64_MAX,
        .hooks = {owner, download_permit, download_inspect, download_cached, download_changed}};
    return qa_downloads_create(library->http, library->root, &options, &library->downloads, error);
}
static bool marker(addon_library *library, const char *directory, bool hidden, qa_error *error)
{
    char *path = NULL;
    if (!relative_path(".addons/removed", directory, &path, error)) return false;
    bool okay = hidden ? qa_fs_root_replace(library->root, path, (qa_bytes){0}, ++library->nonce, error) :
        qa_fs_root_remove(library->root, path, error);
    free(path); return okay;
}
static bool package_directory(addon_library *library, const addon_package *package, char **out, qa_error *error)
{
    for (size_t i = 0; i < library->installed_count; ++i) {
        if (!strcmp(library->installed[i].id, package->id)) {
            *out = copy_string(library->installed[i].directory);
            return *out != NULL || frontend_fail(error, QA_ERROR_MEMORY, "Retaining managed add-on directory");
        }
    }
    char group[49], name[117]; size_t size = strlen(package->group);
    if (size > sizeof(group) - 1) size = sizeof(group) - 1;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)package->group[i];
        group[i] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ? (char)c : '_';
    }
    group[size] = 0;
    snprintf(name, sizeof(name), "qd_%s_%s", group, package->id);
    return relative_path(library->parent, name, out, error);
}
static bool mapped_member(const addon_package *package, const char *member, char **out, qa_error *error)
{
    *out = NULL; const addon_mapping *selected = NULL; size_t longest = 0;
    char *normalized = qa_vfs_normalize_path(member, error); if (!normalized) return false;
    for (size_t i = 0; i < package->mapping_count; ++i) {
        const addon_mapping *mapping = package->mappings + i; size_t size = strlen(mapping->from);
        bool matched = size == 0 || (mapping->from[size - 1] == '/' ? !strncmp(normalized, mapping->from, size) : !strcmp(normalized, mapping->from));
        if (matched && (!selected || size > longest)) { selected = mapping; longest = size; }
    }
    if (!selected || !selected->to) { free(normalized); return true; }
    size_t first = strlen(selected->to), rest = strlen(normalized + longest);
    if (first > SIZE_MAX - rest - 1) { free(normalized); return frontend_fail(error, QA_ERROR_MEMORY, "Mapped member exceeds its retained extent"); }
    char *combined = malloc(first + rest + 1);
    if (!combined) { free(normalized); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining mapped package member"); }
    memcpy(combined, selected->to, first); memcpy(combined + first, normalized + longest, rest + 1);
    free(normalized); normalized = qa_vfs_normalize_path(combined, error); free(combined);
    if (!normalized) return false;
    const char *slash = strchr(normalized, '/'); size_t length = slash ? (size_t)(slash - normalized) : strlen(normalized);
    static const char *const resource_roots[] = {"maps", "progs", "gfx", "sound", "music", "env", "textures"};
    bool base = !slash;
    for (size_t i = 0; !base && i < sizeof(resource_roots) / sizeof(*resource_roots); ++i)
        base = strlen(resource_roots[i]) == length && !memcmp(normalized, resource_roots[i], length);
    if (base) { bool okay = relative_path("id1", normalized, out, error); free(normalized); return okay; }
    *out = normalized; return true;
}
static bool writer_text(qa_source_save_io *io, const char *text)
{ return qa_source_save_bytes(io, (void *)text, strlen(text)); }
static bool writer_json(qa_source_save_io *io, const char *text)
{
    if (!text) return writer_text(io, "null");
    qa_buffer quote = {0};
    bool okay = qa_json_quote((qa_bytes){(const uint8_t *)text, strlen(text)}, &quote, io->error) &&
        qa_source_save_bytes(io, quote.data, quote.size);
    qa_buffer_free(&quote); return okay;
}
static bool metadata_write(addon_library *library, const addon_package *package,
    const char *directory, qa_error *error)
{
    qa_source_save_io io = {0}; qa_buffer bytes = {0}; char *path = NULL;
    bool okay = qa_source_save_writer(&io, NULL, error) && writer_text(&io, "{\"id\":") &&
        writer_json(&io, package->id) && writer_text(&io, ",\"title\":") && writer_json(&io, package->title) &&
        writer_text(&io, ",\"group\":") && writer_json(&io, package->group) && writer_text(&io, ",\"start\":") &&
        writer_json(&io, package->start) && writer_text(&io, "}\n") && qa_source_save_finish(&io, &bytes) &&
        relative_path(directory, ".quaddicted.json", &path, error) &&
        qa_fs_root_replace(library->root, path, (qa_bytes){bytes.data, bytes.size}, ++library->nonce, error);
    free(path); qa_buffer_free(&bytes); qa_source_save_dispose(&io); return okay;
}
static bool install_plan(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons; const addon_package *selected = library->packages + library->selected;
    char *directory = NULL;
    if (!package_directory(library, selected, &directory, error)) return false;
    qa_fs_entry_kind kind; bool okay = qa_fs_root_status(library->root, directory, &kind, NULL, error);
    bool already = false, was_visible = false;
    if (okay && kind != QA_FS_MISSING && kind != QA_FS_DIRECTORY)
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Managed add-on destination is not a directory");
    if (okay && kind != QA_FS_MISSING) {
        char *path = NULL; qa_buffer bytes = {0}; qa_json_document *doc = NULL;
        okay = relative_path(directory, ".quaddicted.json", &path, error);
        if (okay) {
            qa_error existing = {0};
            bool admitted = root_read(library->root, path, &bytes, &existing) &&
                qa_json_parse((qa_bytes){bytes.data, bytes.size}, &doc, &existing) &&
                (qa_json_string_equal(doc, qa_json_get(doc, qa_json_root(doc), "id"), selected->id) ||
                 qa_json_string_equal(doc, qa_json_get(doc, qa_json_root(doc), "sha256"), selected->id));
            already = admitted;
            if (admitted) {
                char *hidden = NULL; qa_fs_entry_kind marker_kind;
                okay = relative_path(".addons/removed", directory, &hidden, error) &&
                    qa_fs_root_status(library->root, hidden, &marker_kind, NULL, error);
                if (okay) was_visible = marker_kind == QA_FS_MISSING;
                free(hidden);
            }
            if (!admitted) {
                char *hidden = NULL; qa_fs_entry_kind marker_kind;
                okay = relative_path(".addons/removed", directory, &hidden, error) &&
                    qa_fs_root_status(library->root, hidden, &marker_kind, NULL, error) && marker_kind == QA_FS_REGULAR;
                free(hidden);
                if (!okay) frontend_fail(error, QA_ERROR_ARGUMENT, "Managed add-on destination conflicts with existing content");
            }
        }
        free(path); qa_json_destroy(doc); qa_buffer_free(&bytes);
    }
    bool staging = false, has_game = already;
    if (okay) {
        okay = marker(library, directory, true, error);
        staging = okay;
        if (okay) okay = qa_fs_root_create_directory(library->root, directory, error);
    }
    for (size_t i = 0; okay && !already && i < library->plan_count; ++i) {
        const addon_package *package = library->packages + library->plan[i]; char cache[78]; qa_archive *archive = NULL;
        okay = cache_name(package, cache) && archive_admit(library->root, cache, &archive, error);
        for (size_t j = 0; okay && j < qa_archive_count(archive); ++j) {
            const qa_archive_entry *entry = qa_archive_entry_at(archive, j);
            if (entry->is_directory) continue;
            char *mapped = NULL, *path = NULL; qa_archive_data data = {0};
            okay = mapped_member(package, entry->path, &mapped, error);
            size_t game_size = strlen(selected->game);
            if (okay && mapped && !strncmp(mapped, selected->game, game_size) && mapped[game_size] == '/') {
                okay = relative_path(directory, mapped + game_size + 1, &path, error) &&
                    qa_archive_read(archive, entry->ordinal, &data, error) &&
                    qa_fs_root_replace(library->root, path, data.bytes, ++library->nonce, error);
                if (okay) has_game = true;
            }
            free(mapped); free(path); qa_archive_data_free(&data);
        }
        qa_archive_close(archive);
    }
    if (okay && !has_game) okay = frontend_fail(error, QA_ERROR_NOT_FOUND, "Add-on package has no authored game directory");
    if (okay && !already) okay = metadata_write(library, selected, directory, error);
    size_t hidden_count = 0;
    for (size_t i = 0; okay && i < library->installed_count; ++i) {
        addon_installed *old = library->installed + i;
        if (!strcmp(old->group, selected->group) && strcmp(old->directory, directory)) {
            okay = marker(library, old->directory, true, error); if (okay) hidden_count = i + 1;
        }
    }
    if (okay) okay = marker(library, directory, false, error) &&
        qa_application_rediscover(owner->seat->frontend->application, true, error);
    if (!okay) {
        qa_error ignored = {0}; if (staging) marker(library, directory, !was_visible, &ignored);
        for (size_t i = 0; i < hidden_count; ++i) {
            addon_installed *old = library->installed + i;
            if (!strcmp(old->group, selected->group) && strcmp(old->directory, directory)) marker(library, old->directory, false, &ignored);
        }
    } else {
        snprintf(library->view.status, sizeof(library->view.status), "Installed %.440s — ready to play", selected->title);
        okay = installed_load(owner, error) && addon_rows(library, error);
    }
    free(directory); return okay;
}
static void plan_clear(addon_library *library)
{
    if (library->download) qa_downloads_release(library->downloads, library->download);
    library->download = 0; free(library->plan); library->plan = NULL; library->plan_count = library->plan_cursor = 0;
}
static bool download_next(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons;
    while (library->plan_cursor < library->plan_count) {
        const addon_package *package = library->packages + library->plan[library->plan_cursor]; bool cached;
        if (!cached_package(library, package, &cached, error)) return false;
        if (cached) { ++library->plan_cursor; continue; }
        qa_download_request request = {.expected_bytes = package->bytes, .maximum_bytes = package->bytes,
            .exact_length = true, .stage_nonce = ++library->nonce}; char path[78];
        if (!cache_name(package, path)) return false;
        request.path = path;
        return qa_downloads_begin(library->downloads, &request, package->url, &library->download, error);
    }
    bool okay = install_plan(owner, error); plan_clear(library); return okay;
}
static bool addons_stop(void *context, qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (!bound(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on cancellation lost its actual owner");
    library->canceled = true;
    if (library->request) qa_http_cancel(library->http, library->request);
    library->request = 0; library->response_ready = false; qa_source_save_dispose(&library->response);
    plan_clear(library); snprintf(library->view.status, sizeof(library->view.status), "Add-on download canceled");
    return true;
}
static bool addon_remove(frontend_content_library_services *owner, const addon_installed *installed, qa_error *error)
{
    addon_library *library = &owner->addons; char title[448]; snprintf(title, sizeof(title), "%s", installed->title);
    if (!marker(library, installed->directory, true, error)) return false;
    if (!qa_application_rediscover(owner->seat->frontend->application, true, error)) {
        qa_error ignored = {0}; marker(library, installed->directory, false, &ignored); return false;
    }
    library->selected_local = SIZE_MAX;
    if (!installed_load(owner, error) || !addon_rows(library, error)) return false;
    snprintf(library->view.status, sizeof(library->view.status), "Removed %s", title); return true;
}
static bool addon_play(frontend_content_library_services *owner, const addon_installed *installed, qa_error *error)
{
    if (!owner->selection.play_addon) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Add-on launch requires its actual startup selection owner");
    return owner->selection.play_addon(owner->selection.context, installed->product, installed->start, error);
}
static bool addons_activate(void *context, const char *id, qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (!id || !addon_prepare(owner, error)) return false;
    if (library->request || library->response_ready || library->plan_count) return true;
    if (!strcmp(id, "back")) { library->selected = library->selected_local = SIZE_MAX; return addon_rows(library, error); }
    if (library->selected_local < library->installed_count) {
        const addon_installed *local = library->installed + library->selected_local;
        if (!strcmp(id, "local-play")) return addon_play(owner, local, error);
        if (!strcmp(id, "local-remove")) return addon_remove(owner, local, error);
    }
    if (!strncmp(id, "local:", 6)) {
        for (size_t i = 0; i < library->installed_count; ++i) if (!strcmp(id + 6, library->installed[i].directory)) {
            library->selected_local = i; return addon_rows(library, error);
        }
    }
    if (library->selected < library->package_count) {
        addon_package *package = library->packages + library->selected;
        addon_installed *installed = installed_group(library, package->group);
        if (!strcmp(id, "play") && installed) return addon_play(owner, installed, error);
        if (!strcmp(id, "remove") && installed) return addon_remove(owner, installed, error);
        if (!strcmp(id, "install")) {
            bool okay = downloads_prepare(owner, error) && plan_create(library, library->selected, error) && download_next(owner, error);
            if (!okay) plan_clear(library);
            return okay;
        }
    } else for (size_t i = 0; i < library->package_count; ++i) if (!strcmp(id, library->packages[i].id)) {
        library->selected = i; return addon_rows(library, error);
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Add-on entry is no longer listed");
}
static const char *addons_scope(void *context)
{ return ((frontend_content_library_services *)context)->addons.scope; }
static const char *addons_status(void *context)
{ return ((frontend_content_library_services *)context)->addons.view.status; }
static bool addons_entries(void *context, const qa_ui_row **rows, size_t *count,
    uint64_t *revision, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !rows || !count || !revision)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Add-on entries require their actual Library owner");
    *rows = owner->addons.view.rows; *count = owner->addons.view.count; *revision = owner->addons.view.revision; return true;
}
static bool local_game(qa_archive *archive, char **out, qa_error *error)
{
    char *root = NULL; bool multiple = false;
    for (size_t i = 0; i < qa_archive_count(archive); ++i) {
        const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
        if (entry->is_directory) continue;
        const char *slash = strchr(entry->path, '/');
        size_t size = slash ? (size_t)(slash - entry->path) : strlen(entry->path);
        if (root && (strlen(root) != size || memcmp(root, entry->path, size))) { multiple = true; break; }
        if (!root) {
            root = malloc(size + 1);
            if (!root) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining imported ZIP game directory");
            memcpy(root, entry->path, size); root[size] = 0;
        }
    }
    bool valid = root && !multiple && strcmp(root, "maps") && strcmp(root, "progs") &&
        strcmp(root, "gfx") && strcmp(root, "sound") && strcmp(root, "music") && strcmp(root, "textures");
    for (const char *c = valid ? root : ""; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
            (*c >= '0' && *c <= '9') || strchr("_+-", *c))) valid = false;
    if (!valid) { free(root); root = copy_string("id1"); }
    if (!root) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining imported ZIP game directory");
    *out = root; return true;
}
static bool addons_import(void *context, const char *path, qa_error *error)
{
    frontend_content_library_services *owner = context; addon_library *library = &owner->addons;
    if (!path || !*path || !downloads_prepare(owner, error)) return false;
    if (library->request || library->response_ready || library->plan_count) return frontend_fail(error, QA_ERROR_ARGUMENT, "An add-on transfer is already active");
    qa_fs_file *file = NULL; qa_fs_identity identity; qa_archive *archive = NULL;
    bool okay = qa_fs_file_open(path, &file, &identity, error) &&
        qa_archive_open_retained(file, &identity, QA_ARCHIVE_ZIP, &archive, error);
    addon_package value = {0}; uint8_t block[65536];
    uint64_t size = okay ? qa_fs_identity_size(&identity) : 0;
    if (okay && (size == 0 || size > SIZE_MAX)) okay = frontend_fail(error, QA_ERROR_FORMAT, "Imported ZIP exceeds its native extent");
    char id[23] = {0};
    bool available = false;
    while (okay && !available) {
        if (library->nonce == UINT64_MAX) { okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Local add-on identifiers exhausted"); break; }
        snprintf(id, sizeof(id), "local-%016" PRIx64, ++library->nonce);
        addon_package candidate = {.id = id}; char cache[78]; qa_fs_entry_kind kind;
        okay = cache_name(&candidate, cache) && qa_fs_root_status(library->root, cache, &kind, NULL, error);
        available = okay && kind == QA_FS_MISSING;
        for (size_t i = 0; available && i < library->package_count; ++i)
            if (!strcmp(library->packages[i].id, id)) available = false;
        for (size_t i = 0; available && i < library->installed_count; ++i)
            if (!strcmp(library->installed[i].id, id)) available = false;
    }
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    const char *backslash = strrchr(name, '\\'); if (backslash) name = backslash + 1;
    qa_buffer folded = {0};
    if (okay) okay = local_game(archive, &value.game, error) &&
        qa_utf8_lower((qa_bytes){(const uint8_t *)name, strlen(name)}, &folded, error);
    if (okay) {
        value.id = copy_string(id); value.filename = copy_string(name); value.title = copy_string(name);
        value.group = folded.size <= SIZE_MAX - 7 ? malloc(folded.size + 7) : NULL;
        value.mappings = calloc(1, sizeof(*value.mappings)); value.bytes = size;
        if (!value.id || !value.filename || !value.title || !value.group || !value.mappings)
            okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining imported Quake ZIP");
        else {
            memcpy(value.group, "local:", 6); memcpy(value.group + 6, folded.data, folded.size); value.group[folded.size + 6] = 0;
            size_t title_size = strlen(value.title);
            if (folded.size >= 4 && !memcmp(folded.data + folded.size - 4, ".zip", 4)) value.title[title_size - 4] = 0;
            value.mapping_count = 1; value.mappings[0].from = copy_string(""); value.mappings[0].to = copy_string("");
            if (!value.mappings[0].from || !value.mappings[0].to) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining imported ZIP mapping");
        }
    }
    qa_buffer_free(&folded); qa_archive_close(archive);
    size_t selected = SIZE_MAX;
    if (okay) {
        addon_package *packages = library->package_count < SIZE_MAX / sizeof(*packages) - 1 ?
            realloc(library->packages, (library->package_count + 1) * sizeof(*packages)) : NULL;
        if (!packages) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining imported ZIP package");
        else { library->packages = packages; selected = library->package_count++; value.local = true;
            packages[selected] = value; value = (addon_package){0}; }
    }
    package_clear(&value);
    if (okay) {
        library->selected = selected; library->selected_local = SIZE_MAX;
        okay = plan_create(library, selected, error) && addon_rows(library, error);
    }
    bool cached = false;
    if (okay) okay = cached_package(library, library->packages + selected, &cached, error);
    if (okay && cached) okay = download_next(owner, error);
    else if (okay) {
        const addon_package *package = library->packages + selected; char cache[78];
        /* A local import feeds the same length/inspection/publication owner. */
        qa_download_request request = {.maximum_bytes = size, .expected_bytes = size,
            .exact_length = true, .stage_nonce = ++library->nonce};
        okay = cache_name(package, cache); request.path = cache;
        if (okay) okay = qa_downloads_begin(library->downloads, &request, NULL, &library->download, error);
        for (size_t offset = 0; okay && offset < (size_t)size;) {
            size_t count = (size_t)size - offset; if (count > sizeof(block)) count = sizeof(block);
            okay = qa_fs_file_read_range(file, &identity, offset, block, count, error) &&
                qa_downloads_append(library->downloads, library->download, offset, (qa_bytes){block, count}, error);
            offset += count;
        }
        if (okay) okay = qa_downloads_finish(library->downloads, library->download, error);
    }
    qa_fs_file_close(file);
    if (!okay) plan_clear(library);
    return okay;
}
static bool catalog_settle(frontend_content_library_services *owner, qa_error *error)
{
    addon_library *library = &owner->addons; qa_buffer bytes = {0};
    qa_error failure = library->response_error; bool fresh = failure.code == QA_OK;
    bool okay = fresh && qa_source_save_finish(&library->response, &bytes);
    if (okay) okay = catalog_parse(library, (qa_bytes){bytes.data, bytes.size}, error);
    if (!okay) {
        if (fresh) failure = library->response_error.code != QA_OK ? library->response_error : error ? *error : (qa_error){0};
        fresh = false; qa_buffer_free(&bytes);
        okay = root_read(library->root, ".addons/quaddicted.json", &bytes, error) &&
            catalog_parse(library, (qa_bytes){bytes.data, bytes.size}, error);
    }
    if (okay && fresh) okay = qa_fs_root_replace(library->root, ".addons/quaddicted.json",
        (qa_bytes){bytes.data, bytes.size}, ++library->nonce, error);
    qa_buffer_free(&bytes); qa_source_save_dispose(&library->response); library->response_ready = false;
    if (okay) okay = installed_load(owner, error) && addon_rows(library, error);
    if (okay) {
        if (fresh) snprintf(library->view.status, sizeof(library->view.status), "%zu Quake add-ons", library->package_count);
        else snprintf(library->view.status, sizeof(library->view.status), "Cached Quaddicted catalog — %.430s", failure.message);
    } else {
        if (failure.code != QA_OK && error && error->code == QA_ERROR_NOT_FOUND) *error = failure;
        snprintf(library->view.status, sizeof(library->view.status), "Cannot read Quaddicted: %.480s",
            error && error->message[0] ? error->message : failure.message);
    }
    return okay;
}
static qa_fs_root *user_root(const qa_frontend *frontend)
{
    qa_settings_store store = frontend_global_settings_storage_user_store(frontend->global_settings_storage);
    return qa_vfs_mount_root(store.vfs, store.mount);
}
static bool demo_source(qa_frontend *frontend, const qa_command_context *source,
    qa_fs_root **root, qa_vfs **mounted, qa_error *error)
{
    uint32_t ordinal; frontend_config_files *scripts = NULL; qa_command_context actual;
    if (!frontend_command_seat_read(frontend, source, &ordinal) ||
        !frontend_content_library_source_read(frontend->seats + ordinal, &scripts, &actual, mounted, NULL, NULL, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo files require their actual local Source seat");
    *root = scripts ? frontend_config_files_root(scripts, false) : user_root(frontend);
    return *root != NULL || frontend_fail(error, QA_ERROR_NOT_FOUND, "Demo files have no actual user recording directory");
}
bool frontend_content_library_demo_root(qa_frontend *frontend, const qa_command_context *source,
    qa_fs_root **root, qa_error *error)
{
    qa_vfs *mounted;
    if (!frontend || !source || !root) return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo root requires its actual Source context");
    return demo_source(frontend, source, root, &mounted, error);
}
bool frontend_content_library_demo_read(qa_frontend *frontend, const qa_command_context *source,
    const char *name, qa_buffer *out, bool *found, qa_error *error)
{
    if (!frontend || !source || !name || !out || out->data || out->size || !found)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo read requires its actual Source and empty destination");
    *found = false; char *path = qa_vfs_normalize_path(name, error); if (!path) return false;
    qa_fs_root *root = NULL; qa_vfs *mounted = NULL; qa_fs_entry_kind kind;
    bool okay = demo_source(frontend, source, &root, &mounted, error) && qa_fs_root_status(root, path, &kind, NULL, error);
    if (okay && kind == QA_FS_REGULAR) { okay = root_read(root, path, out, error); *found = okay; }
    else if (okay && kind != QA_FS_MISSING) okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Demo recording path is not a regular file");
    else if (okay) {
        qa_resource *resource = NULL; qa_error lookup = {0};
        okay = qa_vfs_acquire(mounted, path, &resource, NULL, &lookup);
        if (!okay && lookup.code == QA_ERROR_NOT_FOUND) okay = true;
        else if (!okay) { if (error) *error = lookup; }
        else {
            qa_bytes bytes = qa_resource_bytes(resource);
            uint8_t *copy = bytes.size ? malloc(bytes.size) : NULL;
            if (bytes.size && !copy) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining mounted demo recording");
            else { if (bytes.size) memcpy(copy, bytes.data, bytes.size); *out = (qa_buffer){copy, bytes.size}; *found = true; }
        }
        qa_resource_release(resource);
    }
    free(path); return okay;
}
static frontend_demo_format demo_format(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension && !strcmp(extension, ".qwd")) return FRONTEND_DEMO_QW;
    if (extension && !strcmp(extension, ".dm2")) return FRONTEND_DEMO_Q2;
    if (extension && !strcmp(extension, ".mvd")) return FRONTEND_DEMO_MVD;
    if (extension && !strncmp(extension, ".dm_", 4)) return FRONTEND_DEMO_Q3;
    return FRONTEND_DEMO_NQ;
}
static bool demo_name(const char *path)
{
    const char *extension = strrchr(path, '.'); if (!extension) return false;
    if (!strcmp(extension, ".dem") || !strcmp(extension, ".qwd") ||
        !strcmp(extension, ".dm2") || !strcmp(extension, ".mvd")) return true;
    if (strncmp(extension, ".dm_", 4) || !extension[4]) return false;
    for (const char *c = extension + 4; *c; ++c) if (*c < '0' || *c > '9') return false;
    return true;
}
static bool demo_row(library_rows *view, const char *name, qa_error *error)
{
    char *path = qa_vfs_normalize_path(name, error); if (!path) return false;
    qa_buffer folded = {0}; bool okay = qa_utf8_lower((qa_bytes){(const uint8_t *)path, strlen(path)}, &folded, error);
    if (okay && !demo_name((const char *)folded.data)) { qa_buffer_free(&folded); free(path); return true; }
    size_t slot = view->count;
    for (size_t i = 0; okay && i < view->count; ++i) {
        qa_buffer existing = {0}; okay = qa_utf8_lower((qa_bytes){(const uint8_t *)view->rows[i].key, strlen(view->rows[i].key)}, &existing, error);
        bool same = okay && existing.size == folded.size && !memcmp(existing.data, folded.data, folded.size);
        qa_buffer_free(&existing); if (same) { slot = i; break; }
    }
    frontend_demo_format format = okay ? demo_format((const char *)folded.data) : FRONTEND_DEMO_NQ;
    const char *label = format == FRONTEND_DEMO_Q3 ? "Q3" : format == FRONTEND_DEMO_Q2 || format == FRONTEND_DEMO_MVD ? "Q2" :
        format == FRONTEND_DEMO_QW ? "QW" : "Q1";
    if (okay && slot == view->count) okay = row_add(view, path, path, label, true, error);
    else if (okay) {
        char *key = copy_string(path), *title = copy_string(path), *detail = copy_string(label);
        if (!key || !title || !detail) { free(key); free(title); free(detail); okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining user demo recording"); }
        else { qa_ui_row *row = view->rows + slot; free((void *)row->key); free((void *)row->label); free((void *)row->detail);
            *row = (qa_ui_row){.key = key, .label = title, .detail = detail, .enabled = true}; }
    }
    qa_buffer_free(&folded); free(path); return okay;
}
static bool path_push(qa_vfs_listing *list, const char *path, qa_error *error)
{
    if (list->count >= SIZE_MAX / sizeof(*list->names) - 1) return frontend_fail(error, QA_ERROR_MEMORY, "Library directories exceed their extent");
    char *copy = copy_string(path); char **names = realloc(list->names, (list->count + 1) * sizeof(*names));
    if (names) list->names = names;
    if (!copy || !names) { free(copy); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Library directory"); }
    list->names[list->count++] = copy; return true;
}
static bool demos_scan(qa_fs_root *root, library_rows *view, qa_error *error)
{
    qa_vfs_listing directories = {0}; bool okay = path_push(&directories, "", error);
    for (size_t i = 0; okay && i < directories.count; ++i) {
        qa_fs_listing listing = {0}; okay = qa_fs_root_list(root, directories.names[i], &listing, error);
        for (size_t j = 0; okay && j < listing.count; ++j) {
            const qa_fs_entry *entry = listing.entries + j;
            if (entry->kind != QA_FS_DIRECTORY && entry->kind != QA_FS_REGULAR) continue;
            char *path = NULL;
            if (directories.names[i][0]) okay = relative_path(directories.names[i], entry->name, &path, error);
            else { path = qa_vfs_normalize_path(entry->name, error); okay = path != NULL; }
            if (okay) okay = entry->kind == QA_FS_DIRECTORY ? path_push(&directories, path, error) : demo_row(view, path, error);
            free(path);
        }
        qa_fs_listing_free(&listing);
    }
    qa_vfs_listing_free(&directories); return okay;
}
static int row_compare(const void *left, const void *right)
{ return strcmp(((const qa_ui_row *)left)->label, ((const qa_ui_row *)right)->label); }
static void rows_adopt(library_rows *current, library_rows *next)
{
    bool changed = next->count != current->count;
    for (size_t i = 0; !changed && i < next->count; ++i) changed = strcmp(next->rows[i].key, current->rows[i].key) != 0;
    next->revision = current->revision + changed; rows_clear(current); *current = *next; *next = (library_rows){0};
}
static bool demos_refresh(void *context, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Demos lost their actual Library owner");
    frontend_config_files *scripts; qa_command_context actual; qa_vfs *mounted; qa_fs_root *root;
    if (!frontend_content_library_source_read(owner->seat, &scripts, &actual, &mounted, NULL, NULL, error) ||
        !demo_source(owner->seat->frontend, &actual, &root, &mounted, error)) return false;
    static const struct { const char *directory, *extension; } searches[] = {
        {"", ".dem"}, {"", ".qwd"}, {"demos", ".dm2"}, {"demos", ".mvd"},
        {"demos", ".dm_66"}, {"demos", ".dm_67"}, {"demos", ".dm_68"}};
    library_rows next = {0}; bool okay = true;
    for (size_t i = 0; okay && i < sizeof(searches) / sizeof(*searches); ++i) {
        qa_vfs_listing listing = {0}; okay = qa_vfs_list(mounted, searches[i].directory, searches[i].extension, &listing, error);
        for (size_t j = 0; okay && j < listing.count; ++j) {
            char *path = NULL;
            if (*searches[i].directory) okay = relative_path(searches[i].directory, listing.names[j], &path, error);
            else { path = copy_string(listing.names[j]); if (!path) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining mounted demo path"); }
            if (okay) okay = demo_row(&next, path, error);
            free(path);
        }
        qa_vfs_listing_free(&listing);
    }
    if (okay) okay = demos_scan(root, &next, error);
    if (okay) {
        if (next.count > 1) qsort(next.rows, next.count, sizeof(*next.rows), row_compare);
        snprintf(next.status, sizeof(next.status), "%zu demos", next.count); rows_adopt(&owner->demos, &next);
    }
    rows_clear(&next); return okay;
}
static bool demos_request(frontend_content_library_services *owner, frontend_demo_action action, const char *name, qa_error *error)
{
    if (!bound(owner) || !owner->demo) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Demos require their actual playback and recording service");
    frontend_config_files *scripts; qa_command_context actual; qa_vfs *mounted;
    if (!frontend_content_library_source_read(owner->seat, &scripts, &actual, &mounted, NULL, NULL, error)) return false;
    frontend_demo_format fallback = actual.dialect == QA_CONSOLE_Q3 ? FRONTEND_DEMO_Q3 :
        actual.dialect == QA_CONSOLE_Q2 || actual.dialect == QA_CONSOLE_Q2_RERELEASE ? FRONTEND_DEMO_Q2 :
        actual.dialect == QA_CONSOLE_QW ? FRONTEND_DEMO_QW : FRONTEND_DEMO_NQ;
    return frontend_demo_stage(owner->demo, &(frontend_demo_request){.action = action, .source = actual,
        .name = name, .fallback = fallback}, error);
}
static bool demos_activate(void *context, const char *id, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!id) return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo selection requires its listed identity");
    for (size_t i = 0; i < owner->demos.count; ++i) if (!strcmp(id, owner->demos.rows[i].key))
        return demos_request(owner, FRONTEND_DEMO_PLAY, id, error);
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Demo recording is no longer listed");
}
static bool demos_create(void *context, const char *name, qa_error *error)
{ return demos_request(context, FRONTEND_DEMO_RECORD, name, error); }
static bool demos_stop(void *context, qa_error *error)
{ return demos_request(context, FRONTEND_DEMO_STOP_RECORD, NULL, error); }
static const char *demos_status(void *context)
{
    frontend_content_library_services *owner = context;
    const char *path = owner->demo ? frontend_demo_recording_path(owner->demo) : NULL;
    if (path) snprintf(owner->demos.status, sizeof(owner->demos.status), "%s", path);
    else snprintf(owner->demos.status, sizeof(owner->demos.status), "%zu demos", owner->demos.count);
    return owner->demos.status;
}
static bool demos_entries(void *context, const qa_ui_row **rows, size_t *count, uint64_t *revision, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !rows || !count || !revision) return frontend_fail(error, QA_ERROR_ARGUMENT, "Demo entries require their actual Library owner");
    *rows = owner->demos.rows; *count = owner->demos.count; *revision = owner->demos.revision; return true;
}
static bool profile_name(const char *name)
{
    size_t size = strlen(name); if (size < 6 || size > 37 || strcmp(name + size - 5, ".json")) return false;
    for (size_t i = 0; i < size - 5; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            (i && (c == '_' || c == '-')))) return false;
    }
    return true;
}
static bool profiles_refresh(void *context, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Server profiles lost their actual Library owner");
    qa_fs_root *root = user_root(owner->seat->frontend); qa_fs_entry_kind kind;
    if (!root) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Server profiles have no user settings directory");
    bool okay = qa_fs_root_status(root, "servers", &kind, NULL, error); qa_fs_listing listing = {0}; library_rows next = {0};
    if (okay && kind == QA_FS_DIRECTORY) okay = qa_fs_root_list(root, "servers", &listing, error);
    else if (okay && kind != QA_FS_MISSING) okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Server profile storage is not a directory");
    for (size_t i = 0; okay && i < listing.count; ++i) {
        qa_fs_entry *entry = listing.entries + i; if (entry->kind != QA_FS_REGULAR || !profile_name(entry->name)) continue;
        char *path = NULL, *label = copy_string(entry->name);
        if (!label) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining server profile label"); break; }
        label[strlen(label) - 5] = 0;
        okay = relative_path("servers", entry->name, &path, error) && row_add(&next, path, label, path, true, error);
        free(path); free(label);
    }
    qa_fs_listing_free(&listing);
    if (okay) { if (next.count > 1) qsort(next.rows, next.count, sizeof(*next.rows), row_compare);
        snprintf(next.status, sizeof(next.status), "%zu server profiles", next.count); rows_adopt(&owner->profiles, &next); }
    rows_clear(&next); return okay;
}
static bool profiles_activate(void *context, const char *id, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !id || !owner->selection.select_server_profile)
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Server profiles require their actual startup selection owner");
    for (size_t i = 0; i < owner->profiles.count; ++i) {
        const qa_ui_row *row = owner->profiles.rows + i;
        if (strcmp(row->key, id)) continue;
        if (!owner->selection.select_server_profile(owner->selection.context, row->key, error)) return false;
        snprintf(owner->profiles.status, sizeof(owner->profiles.status), "Selected %s", row->label); return true;
    }
    return frontend_fail(error, QA_ERROR_NOT_FOUND, "Server profile is no longer listed");
}
static const char *profiles_status(void *context)
{ return ((frontend_content_library_services *)context)->profiles.status; }
static bool profiles_entries(void *context, const qa_ui_row **rows, size_t *count, uint64_t *revision, qa_error *error)
{
    frontend_content_library_services *owner = context;
    if (!bound(owner) || !rows || !count || !revision) return frontend_fail(error, QA_ERROR_ARGUMENT, "Server profile entries require their actual Library owner");
    *rows = owner->profiles.rows; *count = owner->profiles.count; *revision = owner->profiles.revision; return true;
}
bool frontend_content_library_services_create(frontend_seat *seat,
    frontend_content_library_services **out, qa_error *error)
{
    if (!seat || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Content Library requires its actual seat");
    frontend_content_library_services *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating Content Library adapters");
    owner->seat = seat; owner->ui = seat->ui; owner->input = seat->input; owner->console = seat->console;
    owner->addons.selected = owner->addons.selected_local = SIZE_MAX;
    snprintf(owner->addons.scope, sizeof(owner->addons.scope), "catalog");
    snprintf(owner->addons.view.status, sizeof(owner->addons.view.status), "Quaddicted add-ons and imported Quake ZIP packages");
    if (!bound(owner)) { free(owner); return frontend_fail(error, QA_ERROR_ARGUMENT, "Content Library seat is no longer attached"); }
    *out = owner; return true;
}
bool frontend_content_library_services_read(frontend_content_library_services *owner,
    frontend_library_service services[FRONTEND_LIBRARY_COUNT], qa_error *error)
{
    if (!bound(owner) || !services)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Content Library services require their attached owner");
    services[FRONTEND_LIBRARY_PLAYER_PROGRESS] = (frontend_library_service){.context = owner,
        .entries = progress_entries, .status = progress_status, .refresh = progress_refresh,
        .activate = progress_activate};
    services[FRONTEND_LIBRARY_ADDONS] = (frontend_library_service){.context = owner,
        .scope = addons_scope, .entries = addons_entries, .status = addons_status,
        .refresh = addons_refresh, .activate = addons_activate,
        .create_label = "Import Quake ZIP", .create = addons_import,
        .stop_label = "Cancel download", .stop = addons_stop};
    services[FRONTEND_LIBRARY_DEMOS] = (frontend_library_service){.context = owner,
        .entries = demos_entries, .status = demos_status, .refresh = demos_refresh,
        .activate = demos_activate, .create_label = "Record", .create = demos_create,
        .stop_label = "Stop recording", .stop = demos_stop};
    services[FRONTEND_LIBRARY_SERVER_PROFILES] = (frontend_library_service){.context = owner,
        .entries = profiles_entries, .status = profiles_status, .refresh = profiles_refresh,
        .activate = profiles_activate};
    return true;
}
bool frontend_content_library_services_bind_selection(frontend_content_library_services *owner,
    const frontend_content_library_selection *selection, qa_error *error)
{
    if (!bound(owner) || owner->busy) return frontend_fail(error, QA_ERROR_ARGUMENT, "Library selection requires its returned physical owner");
    owner->selection = selection ? *selection : (frontend_content_library_selection){0}; return true;
}
bool frontend_content_library_services_bind_demo(frontend_content_library_services *owner,
    frontend_demo_service *service, qa_error *error)
{
    if (!bound(owner) || owner->busy) return frontend_fail(error, QA_ERROR_ARGUMENT, "Library demo binding requires its returned physical owner");
    owner->demo = service; return true;
}
bool frontend_content_library_services_pump(frontend_content_library_services *owner, qa_error *error)
{
    if (!bound(owner) || owner->busy) return frontend_fail(error, QA_ERROR_ARGUMENT, "Library pump requires its returned physical owner");
    addon_library *library = &owner->addons;
    if (!library->http) return true;
    if (!qa_http_callbacks_idle(library->http)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Library publication requires returned HTTP callbacks");
    qa_error operation = {0};
    if (library->response_ready) catalog_settle(owner, &operation);
    if (library->downloads && library->download) {
        bool okay = qa_downloads_pump(library->downloads, &operation); qa_download_view view;
        if (okay) okay = qa_downloads_view(library->downloads, library->download, &view);
        if (okay && (view.state == QA_DOWNLOAD_FAILED || view.state == QA_DOWNLOAD_CANCELED)) {
            snprintf(library->view.status, sizeof(library->view.status), "%s", view.failure.message[0] ? view.failure.message : "Add-on download canceled");
            plan_clear(library);
        } else if (okay && view.state == QA_DOWNLOAD_COMPLETE && view.mounted) {
            qa_downloads_release(library->downloads, library->download); library->download = 0; ++library->plan_cursor;
            okay = download_next(owner, &operation);
        }
        if (!okay) {
            snprintf(library->view.status, sizeof(library->view.status), "Cannot install add-on: %s",
                operation.message[0] ? operation.message : "Download owner is unavailable");
            plan_clear(library);
        }
    }
    /* A package/network failure belongs to its menu status, as in the authored
     * Library. It does not fail the running engine's ordinary frame. */
    return true;
}
bool frontend_content_library_services_idle(const frontend_content_library_services *owner)
{ return !owner || (!owner->busy && (!owner->addons.http || qa_http_callbacks_idle(owner->addons.http))); }
bool frontend_content_library_services_destroy(frontend_content_library_services **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_content_library_services *owner = *slot;
    if (!frontend_content_library_services_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Content Library still owns an entered service callback");
    addon_library *library = &owner->addons;
    if (library->request) qa_http_cancel(library->http, library->request);
    plan_clear(library); qa_downloads_destroy(library->downloads); qa_source_save_dispose(&library->response);
    for (size_t i = 0; i < library->package_count; ++i) package_clear(library->packages + i);
    free(library->packages); installed_clear(library); rows_clear(&library->view); qa_fs_root_close(library->root);
    rows_clear(&owner->progress); rows_clear(&owner->demos); rows_clear(&owner->profiles);
    free(owner); *slot = NULL; return true;
}
