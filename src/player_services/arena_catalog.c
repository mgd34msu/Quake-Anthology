/* Authored campaign numbering from UI_LoadArenas and base-arena-catalog.ts. */
#include "qa/arena_progress_catalog.h"
#include "qa/common_parse.h"
#include "save_io.h"

#include <limits.h>
#include <stdio.h>

typedef struct arena_resource { qa_resource *resource; char *path; } arena_resource;
typedef struct arena_row {
    qa_base_arena view;
    char *map, *title, *special, *type, *bot_text;
    char **bots;
} arena_row;
struct qa_base_arena_catalog {
    arena_resource *resources;
    size_t resource_count, resource_capacity;
    arena_row *rows;
    size_t count, capacity;
    qa_arena_catalog levels;
    bool busy;
};

static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static char *copy(const char *text, qa_error *error)
{
    size_t size = strlen(text);
    char *out = malloc(size + 1);
    if (!out) { fail(error, QA_ERROR_MEMORY, "Allocating authored arena text"); return NULL; }
    memcpy(out, text, size + 1); return out;
}
static bool replace(char **field, const char *text, qa_error *error)
{
    char *next = copy(text, error);
    if (!next) return false;
    free(*field); *field = next; return true;
}
static char *latin1_text(const char *text, size_t size, qa_error *error)
{
    size_t length = size;
    for (size_t i = 0; i < size; ++i) {
        if ((unsigned char)text[i] < 128) continue;
        if (length == SIZE_MAX) { fail(error, QA_ERROR_MEMORY, "Authored arena text exceeds UTF-8 storage"); return NULL; }
        ++length;
    }
    if (length == SIZE_MAX) { fail(error, QA_ERROR_MEMORY, "Authored arena text exceeds UTF-8 storage"); return NULL; }
    char *out = malloc(length + 1);
    if (!out) { fail(error, QA_ERROR_MEMORY, "Allocating authored arena UTF-8 text"); return NULL; }
    size_t written = 0;
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 128) out[written++] = (char)c;
        else { out[written++] = (char)(0xc0u | (c >> 6)); out[written++] = (char)(0x80u | (c & 0x3fu)); }
    }
    out[written] = 0; return out;
}
static bool publish_text(char **field, qa_error *error)
{
    char *next = latin1_text(*field, strlen(*field), error);
    if (!next) return false;
    free(*field); *field = next; return true;
}
static unsigned char lower(unsigned char c)
{ return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c; }
static bool equal(const char *a, const char *b)
{
    while (*a && *b) if (lower((unsigned char)*a++) != lower((unsigned char)*b++)) return false;
    return !*a && !*b;
}
static bool js_space(unsigned char c)
{ return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 160; }
static double limit_value(const char *text)
{
    while (js_space((unsigned char)*text)) ++text;
    char digits[QA_COMMON_TOKEN_CAPACITY + 2]; size_t n = 0;
    if (*text == '+' || *text == '-') digits[n++] = *text++;
    size_t first = n;
    while (*text >= '0' && *text <= '9') digits[n++] = *text++;
    if (n == first) return 0;
    digits[n] = 0;
    double value = strtod(digits, NULL);
    return value == 0 ? 0 : value;
}
static void row_destroy(arena_row *row)
{
    free(row->map); free(row->title); free(row->special); free(row->type); free(row->bot_text);
    for (size_t i = 0; i < row->view.bot_count; ++i) free(row->bots[i]);
    free(row->bots); *row = (arena_row){0};
}
void qa_base_arena_catalog_destroy(qa_base_arena_catalog *catalog)
{
    if (!catalog) return;
    for (size_t i = 0; i < catalog->count; ++i) row_destroy(catalog->rows + i);
    for (size_t i = 0; i < catalog->resource_count; ++i) {
        qa_resource_release(catalog->resources[i].resource); free(catalog->resources[i].path);
    }
    free(catalog->rows); free(catalog->resources); free(catalog);
}
static bool reserve_rows(qa_base_arena_catalog *catalog, qa_error *error)
{
    if (catalog->count < catalog->capacity) return true;
    size_t capacity = catalog->capacity ? catalog->capacity * 2 : 16;
    if (capacity < catalog->capacity || capacity > INT32_MAX || capacity > SIZE_MAX / sizeof(arena_row))
        return fail(error, QA_ERROR_MEMORY, "Authored arena rows exceed native storage");
    arena_row *rows = realloc(catalog->rows, capacity * sizeof(*rows));
    if (!rows) return fail(error, QA_ERROR_MEMORY, "Allocating authored arena rows");
    catalog->rows = rows; catalog->capacity = capacity; return true;
}
static bool parse_resource(qa_base_arena_catalog *catalog, qa_resource *resource, qa_error *error)
{
    qa_common_cursor cursor; qa_common_parser parser = {0};
    if (!qa_common_cursor_init(&cursor, qa_resource_bytes(resource), QA_COMMON_TERMINATED, error)) return false;
    for (;;) {
        if (!qa_common_parse(&parser, &cursor, true, error)) return false;
        if (!parser.token[0]) return true;
        if (strcmp(parser.token, "{")) return fail(error, QA_ERROR_FORMAT, "Malformed base arena definition");
        arena_row row = {0}; bool okay = true;
        while (okay) {
            okay = qa_common_parse(&parser, &cursor, true, error);
            if (!okay || !strcmp(parser.token, "}")) break;
            if (!parser.token[0]) { okay = fail(error, QA_ERROR_FORMAT, "Unterminated base arena definition"); break; }
            char key[QA_COMMON_TOKEN_CAPACITY + 1]; memcpy(key, parser.token, strlen(parser.token) + 1);
            okay = qa_common_parse(&parser, &cursor, false, error);
            if (!okay) break;
            const char *value = parser.token[0] ? parser.token : "<NULL>";
            if (!strcmp(key, "map")) okay = replace(&row.map, value, error);
            else if (!strcmp(key, "longname")) okay = replace(&row.title, value, error);
            else if (!strcmp(key, "special")) okay = replace(&row.special, value, error);
            else if (!strcmp(key, "type")) okay = replace(&row.type, value, error);
            else if (!strcmp(key, "bots")) okay = replace(&row.bot_text, value, error);
            else if (!strcmp(key, "fraglimit")) row.view.frag_limit = limit_value(value);
            else if (!strcmp(key, "timelimit")) row.view.time_limit = limit_value(value);
        }
        if (okay && row.type && strstr(row.type, "single")) {
            okay = reserve_rows(catalog, error);
            if (okay) { catalog->rows[catalog->count++] = row; row = (arena_row){0}; }
        }
        row_destroy(&row);
        if (!okay) return false;
    }
}
static bool add_resource(qa_base_arena_catalog *catalog, qa_resource *resource,
    const char *path, qa_error *error)
{
    if (catalog->resource_count == catalog->resource_capacity) {
        size_t capacity = catalog->resource_capacity ? catalog->resource_capacity * 2 : 16;
        if (capacity < catalog->resource_capacity || capacity > SIZE_MAX / sizeof(arena_resource))
            return fail(error, QA_ERROR_MEMORY, "Authored arena resource order exceeds storage");
        arena_resource *resources = realloc(catalog->resources, capacity * sizeof(*resources));
        if (!resources) return fail(error, QA_ERROR_MEMORY, "Allocating authored arena resource order");
        catalog->resources = resources; catalog->resource_capacity = capacity;
    }
    char *requested = copy(path, error);
    if (!requested) return false;
    qa_resource_retain(resource);
    catalog->resources[catalog->resource_count++] = (arena_resource){resource, requested};
    return parse_resource(catalog, resource, error);
}
static bool acquire(qa_base_arena_catalog *catalog, qa_vfs *files, const char *path, qa_error *error)
{
    qa_resource *resource = NULL; qa_error local = {0};
    if (!qa_vfs_acquire(files, path, &resource, NULL, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    bool okay = add_resource(catalog, resource, path, error);
    qa_resource_release(resource); return okay;
}
static bool bots(arena_row *row, qa_error *error)
{
    const char *text = row->bot_text ? row->bot_text : "";
    while (*text) {
        while (js_space((unsigned char)*text)) ++text;
        if (!*text) break;
        const char *end = text;
        while (*end && !js_space((unsigned char)*end)) ++end;
        if (row->view.bot_count == SIZE_MAX / sizeof(char *)) return fail(error, QA_ERROR_MEMORY, "Arena bot names exceed storage");
        char **names = realloc(row->bots, (row->view.bot_count + 1) * sizeof(*names));
        if (!names) return fail(error, QA_ERROR_MEMORY, "Allocating authored arena bot names");
        row->bots = names;
        char *name = latin1_text(text, (size_t)(end - text), error);
        if (!name) return false;
        row->bots[row->view.bot_count++] = name; text = end;
    }
    row->view.bots = (const char *const *)row->bots; return true;
}
static bool finish(qa_base_arena_catalog *catalog, qa_error *error)
{
    int32_t ordinary = 0, specials = 0;
    for (size_t i = 0; i < catalog->count; ++i)
        if (catalog->rows[i].special && catalog->rows[i].special[0]) ++specials; else ++ordinary;
    int32_t regular = ordinary / 4 * 4, number = 0, special_number = regular;
    catalog->levels = (qa_arena_catalog){regular, regular + specials, -1, -1};
    for (size_t i = 0; i < catalog->count; ++i) {
        arena_row *row = catalog->rows + i;
        if (!row->map || !row->map[0]) return fail(error, QA_ERROR_FORMAT, "Invalid base arena map name");
        for (const unsigned char *p = (const unsigned char *)row->map; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '/' || *p == '-'))
                return fail(error, QA_ERROR_FORMAT, "Invalid base arena map name");
        row->view.number = row->special && row->special[0] ? special_number++ : number++;
        if (!row->special && !replace(&row->special, "", error)) return false;
        if (!row->title && !replace(&row->title, row->map, error)) return false;
        size_t length = strlen(row->map);
        char *map = malloc(length + 10);
        if (!map) return fail(error, QA_ERROR_MEMORY, "Allocating authored arena map path");
        snprintf(map, length + 10, "maps/%s.bsp", row->map); free(row->map); row->map = map;
        row->view.selection = equal(row->special, "training") ? -4 : equal(row->special, "final") ? regular : row->view.number;
        if (catalog->levels.training == -1 && equal(row->special, "training")) catalog->levels.training = row->view.number;
        if (catalog->levels.final == -1 && equal(row->special, "final")) catalog->levels.final = row->view.number;
        if (row->view.frag_limit == 0.0 && row->view.time_limit == 0.0) row->view.frag_limit = 10;
        if (!bots(row, error)) return false;
        if (!publish_text(&row->title, error) || !publish_text(&row->special, error)) return false;
        row->view.map = row->map; row->view.title = row->title; row->view.special = row->special;
    }
    return true;
}
bool qa_base_arena_catalog_create(qa_vfs *files, qa_base_arena_catalog **out, qa_error *error)
{
    if (!files || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "Base arena catalog needs its actual source VFS and empty output");
    qa_base_arena_catalog *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) return fail(error, QA_ERROR_MEMORY, "Allocating authored base arena catalog");
    qa_vfs_listing listing = {0};
    bool okay = qa_vfs_list(files, "scripts", ".arena", &listing, error) && acquire(catalog, files, "scripts/arenas.txt", error);
    for (size_t i = 0; okay && i < listing.count; ++i) {
        size_t length = strlen(listing.names[i]);
        char *path = malloc(length + 9);
        if (!path) { okay = fail(error, QA_ERROR_MEMORY, "Allocating authored arena resource path"); break; }
        snprintf(path, length + 9, "scripts/%s", listing.names[i]);
        okay = acquire(catalog, files, path, error); free(path);
    }
    qa_vfs_listing_free(&listing);
    if (okay) okay = finish(catalog, error);
    if (!okay) { qa_base_arena_catalog_destroy(catalog); return false; }
    *out = catalog; return true;
}
bool qa_base_arena_catalog_ready(const qa_base_arena_catalog *catalog, qa_error *error)
{ return (catalog && !catalog->busy) || fail(error, QA_ERROR_ARGUMENT, "Base arena catalog is absent or executing a reference callback"); }
qa_arena_catalog qa_base_arena_catalog_levels(const qa_base_arena_catalog *catalog)
{ return catalog ? catalog->levels : (qa_arena_catalog){0, 0, -1, -1}; }
size_t qa_base_arena_catalog_count(const qa_base_arena_catalog *catalog)
{ return catalog ? catalog->count : 0; }
const qa_base_arena *qa_base_arena_catalog_at(const qa_base_arena_catalog *catalog, size_t index)
{ return catalog && index < catalog->count ? &catalog->rows[index].view : NULL; }
const qa_base_arena *qa_base_arena_catalog_find(const qa_base_arena_catalog *catalog, const char *map)
{
    if (!catalog || !map) return NULL;
    const char *name = map;
    if (strlen(name) >= 5 && lower((unsigned char)name[0]) == 'm' && lower((unsigned char)name[1]) == 'a' &&
        lower((unsigned char)name[2]) == 'p' && lower((unsigned char)name[3]) == 's' && name[4] == '/') name += 5;
    size_t length = strlen(name);
    if (length >= 4 && equal(name + length - 4, ".bsp")) length -= 4;
    for (size_t i = 0; i < catalog->count; ++i) {
        const char *candidate = catalog->rows[i].map + 5;
        if (strlen(candidate) != length + 4) continue;
        size_t j = 0;
        while (j < length && lower((unsigned char)name[j]) == lower((unsigned char)candidate[j])) ++j;
        if (j == length) return &catalog->rows[i].view;
    }
    return NULL;
}
size_t qa_base_arena_catalog_resource_count(const qa_base_arena_catalog *catalog)
{ return catalog ? catalog->resource_count : 0; }
const qa_resource *qa_base_arena_catalog_resource_at(const qa_base_arena_catalog *catalog,
    size_t index, const char **path)
{
    if (!catalog || index >= catalog->resource_count) return NULL;
    if (path) *path = catalog->resources[index].path;
    return catalog->resources[index].resource;
}
static const uint8_t magic[8] = {'Q','A','B','A',1,0,0,0};
static bool authored_path(const char *path)
{
    if (!path || strncmp(path, "scripts/", 8)) return false;
    if (!strcmp(path, "scripts/arenas.txt")) return true;
    size_t length = strlen(path);
    if (length < 14 || !equal(path + length - 6, ".arena")) return false;
    const char *component = path + 8;
    for (const char *p = component;; ++p) {
        if (*p == '\\') return false;
        if (*p && *p != '/') continue;
        size_t size = (size_t)(p - component);
        if (!size || (size == 1 && component[0] == '.') ||
            (size == 2 && component[0] == '.' && component[1] == '.')) return false;
        if (!*p) return true;
        component = p + 1;
    }
}
bool qa_base_arena_catalog_checkpoint(const qa_base_arena_catalog *source,
    const qa_base_arena_catalog_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!qa_base_arena_catalog_ready(source, error) || !refs || !refs->resource_encode || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Base catalog capture needs real resource identity bindings and empty output");
    qa_base_arena_catalog *catalog = (qa_base_arena_catalog *)source;
    catalog->busy = true; qa_source_save_io io = {0}; size_t count = catalog->resource_count;
    bool okay = qa_source_save_writer(&io, NULL, error) && ps_magic(&io, magic) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; okay && i < count; ++i) {
        char *path = catalog->resources[i].path; uint64_t reference = 0;
        okay = refs->resource_encode(refs->context, catalog->resources[i].resource, path, &reference, error) &&
            qa_source_save_owned_text(&io, &path) && qa_source_save_u64(&io, &reference);
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); catalog->busy = false; return okay;
}
bool qa_base_arena_catalog_restore(qa_bytes bytes, const qa_base_arena_catalog_refs *refs,
    qa_base_arena_catalog **out, qa_error *error)
{
    if (!refs || !refs->resource_decode || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Base catalog reconstruction needs real resource identity bindings");
    qa_base_arena_catalog *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) return fail(error, QA_ERROR_MEMORY, "Allocating private authored arena catalog");
    qa_source_save_io io = {0}; size_t count = 0;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && ps_magic(&io, magic) &&
        ps_count(&io, &count, 17, sizeof(arena_resource));
    for (size_t i = 0; okay && i < count; ++i) {
        char *path = NULL; uint64_t reference = 0; qa_resource *resource = NULL;
        okay = qa_source_save_owned_text(&io, &path);
        if (okay && !authored_path(path))
            okay = fail(error, QA_ERROR_FORMAT, "Base catalog reference is not an authored arena path");
        if (okay) okay = qa_source_save_u64(&io, &reference) &&
            refs->resource_decode(refs->context, reference, path, &resource, error);
        if (okay && !resource)
            okay = fail(error, QA_ERROR_FORMAT, "Base catalog resource resolver returned no actual source resource");
        if (okay) okay = add_resource(catalog, resource, path, error);
        free(path);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL) && finish(catalog, error);
    qa_source_save_dispose(&io);
    if (!okay) { qa_base_arena_catalog_destroy(catalog); return false; }
    *out = catalog; return true;
}
