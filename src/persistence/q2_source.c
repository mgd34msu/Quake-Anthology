#include "internal.h"
#include "qa/q2_save.h"
#include "qa/binary.h"
#include "qa/archive.h"
#include <limits.h>
#include <zlib.h>

#define Q2_SERVER_HEADER (QA_Q2_SAVE_COMMENT_BYTES + QA_Q2_SAVE_MAP_COMMAND_BYTES)
#define Q2_SERVER_CVAR (QA_Q2_SAVE_CVAR_BYTES * 2)
#define Q2_LEVEL_STRINGS (QA_Q2_SAVE_CONFIGSTRINGS * QA_Q2_SAVE_CONFIGSTRING_BYTES)
#define Q2_LEVEL_BYTES (Q2_LEVEL_STRINGS + QA_Q2_SAVE_AREA_PORTALS * 4)

uint32_t qa_q2_save_configstring_count(const qa_q2_save_level *level)
{
    return level->rerelease ? QA_Q2_SAVE_RERELEASE_CONFIGSTRINGS : QA_Q2_SAVE_CONFIGSTRINGS;
}

uint32_t qa_q2_save_configstring_width(const qa_q2_save_level *level)
{
    return level->rerelease ? QA_Q2_SAVE_RERELEASE_CONFIGSTRING_BYTES : QA_Q2_SAVE_CONFIGSTRING_BYTES;
}

const char *qa_q2_save_configstring(const qa_q2_save_level *level, uint32_t index)
{
    if (!level || index >= qa_q2_save_configstring_count(level)) return "";
    size_t low = 0, high = level->configstrings.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (level->configstrings.spans[middle].index <= index) low = middle + 1;
        else high = middle;
    }
    if (!low) return "";
    const qa_q2_save_config_span *span = level->configstrings.spans + low - 1;
    uint32_t row = index - span->index;
    return row < span->rows ? (const char *)level->configstrings.bytes.data + span->offset +
        (size_t)row * qa_q2_save_configstring_width(level) : "";
}

static void configstrings_dispose(qa_q2_save_configstrings *configs)
{
    free(configs->spans);
    qa_buffer_free(&configs->bytes);
    *configs = (qa_q2_save_configstrings){0};
}

static bool occupied_row(const uint8_t *row, uint32_t width)
{
    for (uint32_t i = 0; i < width; ++i) if (row[i]) return true;
    return false;
}

bool qa_q2_save_configstrings_capture(qa_q2_save_level *level, qa_bytes table, qa_error *error)
{
    if (!level || !table.data || table.size !=
        (size_t)qa_q2_save_configstring_count(level) * qa_q2_save_configstring_width(level))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II config table has no actual Source extent");
    uint32_t width = qa_q2_save_configstring_width(level), rows = qa_q2_save_configstring_count(level);
    size_t count = 0, size = 0;
    bool occupied = false;
    for (uint32_t i = 0; i < rows; ++i) {
        bool next = occupied_row(table.data + (size_t)i * width, width);
        if (next) { size += width; if (!occupied) { ++count; ++size; } }
        occupied = next;
    }
    qa_q2_save_configstrings configs = {.count = count};
    if (count) {
        configs.spans = malloc(count * sizeof(*configs.spans));
        configs.bytes = (qa_buffer){.data = malloc(size), .size = size};
        if (!configs.spans || !configs.bytes.data) {
            configstrings_dispose(&configs);
            return persistence_fail(error, QA_ERROR_MEMORY, "Retaining occupied Quake II config rows");
        }
    }
    size_t cursor = 0, at = 0;
    for (uint32_t i = 0; i < rows;) {
        if (!occupied_row(table.data + (size_t)i * width, width)) { ++i; continue; }
        uint32_t begin = i;
        do { ++i; } while (i < rows && occupied_row(table.data + (size_t)i * width, width));
        size_t bytes = (size_t)(i - begin) * width;
        configs.spans[at++] = (qa_q2_save_config_span){.index = begin, .rows = i - begin, .offset = cursor};
        memcpy(configs.bytes.data + cursor, table.data + (size_t)begin * width, bytes);
        configs.bytes.data[cursor + bytes] = 0; cursor += bytes + 1;
    }
    configstrings_dispose(&level->configstrings); level->configstrings = configs;
    return true;
}

bool qa_q2_save_configstrings_expand(const qa_q2_save_level *level, qa_buffer *out, qa_error *error)
{
    if (!level || !out || out->data)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II config expansion requires an empty output");
    uint32_t rows = qa_q2_save_configstring_count(level), width = qa_q2_save_configstring_width(level);
    qa_buffer table = {.size = (size_t)rows * width};
    table.data = calloc(1, table.size);
    if (!table.data) return persistence_fail(error, QA_ERROR_MEMORY, "Expanding Quake II physical config rows");
    uint32_t end = 0;
    for (size_t i = 0; i < level->configstrings.count; ++i) {
        const qa_q2_save_config_span *span = level->configstrings.spans + i;
        size_t bytes = (size_t)span->rows * width;
        if (!span->rows || span->index < end || span->index >= rows || span->rows > rows - span->index ||
            span->offset >= level->configstrings.bytes.size || bytes >= level->configstrings.bytes.size - span->offset ||
            !level->configstrings.bytes.data || level->configstrings.bytes.data[span->offset + bytes]) {
            qa_buffer_free(&table);
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II occupied config span leaves its Source table");
        }
        memcpy(table.data + (size_t)span->index * width, level->configstrings.bytes.data + span->offset, bytes);
        end = span->index + span->rows;
    }
    *out = table; return true;
}

bool qa_q2_save_configstring_set(qa_q2_save_level *level, uint32_t index, const char *text, qa_error *error)
{
    if (!level || !text || index >= qa_q2_save_configstring_count(level))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II config assignment leaves its Source table");
    qa_buffer table = {0};
    if (!qa_q2_save_configstrings_expand(level, &table, error)) return false;
    size_t offset = (size_t)index * qa_q2_save_configstring_width(level), size = strlen(text) + 1;
    bool ok = size <= table.size - offset;
    if (ok) { memcpy(table.data + offset, text, size); ok = qa_q2_save_configstrings_capture(level,
        (qa_bytes){table.data, table.size}, error); }
    else persistence_fail(error, QA_ERROR_FORMAT, "Quake II config string exceeds its Source physical table");
    qa_buffer_free(&table); return ok;
}

void qa_q2_save_level_dispose(qa_q2_save_level *level)
{
    if (!level) return;
    configstrings_dispose(&level->configstrings); qa_buffer_free(&level->game);
    *level = (qa_q2_save_level){0};
}

bool qa_q2_save_level_copy(const qa_q2_save_level *source, qa_q2_save_level *out, qa_error *error)
{
    if (!source || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II level copy requires actual file owners");
    qa_q2_save_level value = *source;
    value.configstrings = (qa_q2_save_configstrings){0}; value.game = (qa_buffer){0};
    if ((source->game.size && !source->game.data) ||
        (source->configstrings.count && (!source->configstrings.spans || !source->configstrings.bytes.data)))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II level copy lacks its actual backing");
    value.game.size = source->game.size;
    if (source->game.size) value.game.data = malloc(source->game.size);
    value.configstrings.count = source->configstrings.count;
    if (source->configstrings.count) value.configstrings.spans =
        malloc(source->configstrings.count * sizeof(*value.configstrings.spans));
    value.configstrings.bytes.size = source->configstrings.bytes.size;
    if (source->configstrings.bytes.size) value.configstrings.bytes.data = malloc(source->configstrings.bytes.size);
    if ((value.game.size && !value.game.data) || (value.configstrings.count && !value.configstrings.spans) ||
        (value.configstrings.bytes.size && !value.configstrings.bytes.data)) {
        qa_q2_save_level_dispose(&value);
        return persistence_fail(error, QA_ERROR_MEMORY, "Copying Quake II level file state");
    }
    if (value.game.size) memcpy(value.game.data, source->game.data, value.game.size);
    if (value.configstrings.count) memcpy(value.configstrings.spans, source->configstrings.spans,
        value.configstrings.count * sizeof(*value.configstrings.spans));
    if (value.configstrings.bytes.size) memcpy(value.configstrings.bytes.data,
        source->configstrings.bytes.data, value.configstrings.bytes.size);
    *out = value; return true;
}

bool qa_q2_save_configstrings_io(qa_source_save_io *io, qa_q2_save_level *level)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bool(io, &level->rerelease)) return false;
    uint32_t rows = qa_q2_save_configstring_count(level), width = qa_q2_save_configstring_width(level);
    size_t count = reading ? 0 : level->configstrings.count;
    if (!qa_source_save_count(io, &count, rows)) return false;
    qa_q2_save_configstrings configs = reading ? (qa_q2_save_configstrings){.count = count} : level->configstrings;
    if (reading && count) {
        configs.spans = calloc(count, sizeof(*configs.spans));
        if (!configs.spans) return persistence_io_fail(io, QA_ERROR_MEMORY, "Retaining campaign Q2 config spans");
    }
    bool ok = true;
    uint32_t end = 0; size_t extent = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_q2_save_config_span *span = configs.spans + i;
        ok = qa_source_save_u32(io, &span->index) && qa_source_save_u32(io, &span->rows);
        if (ok && (!span->rows || span->index < end || span->index >= rows || span->rows > rows - span->index))
            ok = persistence_io_fail(io, QA_ERROR_FORMAT, "Campaign Q2 config span leaves its Source table");
        if (ok) {
            size_t size = (size_t)span->rows * width;
            if (reading) span->offset = extent;
            else if (span->offset != extent || span->offset >= configs.bytes.size ||
                size >= configs.bytes.size - span->offset || !configs.bytes.data || configs.bytes.data[span->offset + size])
                ok = persistence_io_fail(io, QA_ERROR_FORMAT, "Campaign Q2 config span has no terminated backing");
            extent += size + 1; end = span->index + span->rows;
        }
    }
    if (ok && reading && extent) {
        configs.bytes = (qa_buffer){.data = calloc(1, extent), .size = extent};
        if (!configs.bytes.data) ok = persistence_io_fail(io, QA_ERROR_MEMORY, "Retaining campaign Q2 config bytes");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        qa_q2_save_config_span *span = configs.spans + i;
        ok = qa_source_save_memory_delta(io, configs.bytes.data + span->offset,
            (size_t)span->rows * width, (qa_bytes){0});
    }
    if (reading) {
        if (ok) { configstrings_dispose(&level->configstrings); level->configstrings = configs; }
        else configstrings_dispose(&configs);
    }
    return ok;
}

void qa_q2_save_server_dispose(qa_q2_save_server *server)
{
    if (!server) return;
    free(server->cvars); qa_buffer_free(&server->cvar_text);
    *server = (qa_q2_save_server){0};
}

static bool cvar_text(const qa_q2_save_server *server, const char *text)
{
    uintptr_t begin = (uintptr_t)server->cvar_text.data, at = (uintptr_t)text;
    if (!text || !begin || at < begin || at - begin >= server->cvar_text.size) return false;
    size_t remaining = server->cvar_text.size - (size_t)(at - begin);
    if (!server->rerelease && remaining < QA_Q2_SAVE_CVAR_BYTES) return false;
    return memchr(text, 0, server->rerelease ? remaining : QA_Q2_SAVE_CVAR_BYTES) != NULL;
}

static bool server_text(const qa_q2_save_server *server, qa_error *error)
{
    size_t comment = server->rerelease ? sizeof(server->comment) : QA_Q2_SAVE_COMMENT_BYTES;
    size_t map = server->rerelease ? sizeof(server->map_command) : QA_Q2_SAVE_MAP_COMMAND_BYTES;
    if (!memchr(server->comment, 0, comment) || !memchr(server->map_command, 0, map) ||
        !server->map_command[0] || (server->cvar_count && !server->cvars))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server save fields are unterminated or absent");
    for (size_t i = 0; i < server->cvar_count; ++i) {
        const qa_q2_save_cvar *cvar = server->cvars + i;
        if (!cvar_text(server, cvar->name) || !*cvar->name || !cvar_text(server, cvar->value))
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II latched cvar has no actual text backing");
    }
    return true;
}

bool qa_q2_save_server_copy(const qa_q2_save_server *source, qa_q2_save_server *out, qa_error *error)
{
    if (!source || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server copy requires its actual owner");
    if (!server_text(source, error)) return false;
    qa_q2_save_server value = *source;
    value.cvars = NULL; value.cvar_text = (qa_buffer){0};
    if (source->cvar_count > SIZE_MAX / sizeof(*value.cvars))
        return persistence_fail(error, QA_ERROR_MEMORY, "Quake II cvar roster exceeds memory");
    if (source->cvar_count) value.cvars = malloc(source->cvar_count * sizeof(*value.cvars));
    if (source->cvar_text.size) value.cvar_text.data = malloc(source->cvar_text.size);
    value.cvar_text.size = source->cvar_text.size;
    if ((source->cvar_count && !value.cvars) || (value.cvar_text.size && !value.cvar_text.data)) {
        qa_q2_save_server_dispose(&value);
        return persistence_fail(error, QA_ERROR_MEMORY, "Copying original Quake II server state");
    }
    if (value.cvar_text.size) memcpy(value.cvar_text.data, source->cvar_text.data, value.cvar_text.size);
    for (size_t i = 0; i < value.cvar_count; ++i) {
        value.cvars[i].name = (char *)value.cvar_text.data +
            ((uintptr_t)source->cvars[i].name - (uintptr_t)source->cvar_text.data);
        value.cvars[i].value = (char *)value.cvar_text.data +
            ((uintptr_t)source->cvars[i].value - (uintptr_t)source->cvar_text.data);
    }
    *out = value; return true;
}

/* KEX sv_save.cpp writes little-endian lengths and bytes without a NUL. */
static bool read_text(qa_bytes bytes, size_t *cursor, qa_bytes *text, qa_error *error)
{
    if (*cursor > bytes.size || bytes.size - *cursor < 4)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server.ssv text length is truncated");
    uint32_t size = qa_load_u32le(bytes.data + *cursor); *cursor += 4;
    if (size > bytes.size - *cursor || memchr(bytes.data + *cursor, 0, size))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server.ssv text leaves its file or contains a NUL");
    *text = (qa_bytes){bytes.data + *cursor, size}; *cursor += size; return true;
}

static bool rerelease_server_decode(qa_bytes bytes, qa_q2_save_server *server, qa_error *error)
{
    if (bytes.size < 5 || bytes.data[4] > 1)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II EVAS autosave field is invalid");
    server->rerelease = true; server->autosave = bytes.data[4] != 0;
    size_t cursor = 5; qa_bytes text;
    if (!read_text(bytes, &cursor, &text, error)) return false;
    if (text.size >= sizeof(server->comment))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II EVAS description exceeds CS_NAME");
    memcpy(server->comment, text.data, text.size);
    if (bytes.size - cursor < 8)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II EVAS timestamp is truncated");
    server->timestamp = qa_load_u64le(bytes.data + cursor); cursor += 8;
    if (!read_text(bytes, &cursor, &text, error)) return false;
    if (!text.size || text.size >= sizeof(server->map_command))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II EVAS map command exceeds the original reader");
    memcpy(server->map_command, text.data, text.size);
    size_t begin = cursor, count = 0, extent = 0;
    while (cursor < bytes.size) {
        qa_bytes name, value;
        if (!read_text(bytes, &cursor, &name, error) || !read_text(bytes, &cursor, &value, error)) return false;
        if (!name.size || name.size > SIZE_MAX - value.size - 2 || extent > SIZE_MAX - name.size - value.size - 2 ||
            count == SIZE_MAX / sizeof(*server->cvars))
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II EVAS cvar roster exceeds its text extent");
        ++count; extent += name.size + value.size + 2;
    }
    if (count) server->cvars = malloc(count * sizeof(*server->cvars));
    server->cvar_text = (qa_buffer){.data = extent ? malloc(extent) : NULL, .size = extent};
    if ((count && !server->cvars) || (extent && !server->cvar_text.data))
        return persistence_fail(error, QA_ERROR_MEMORY, "Retaining original rerelease latched cvars");
    server->cvar_count = count; cursor = begin; size_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        qa_bytes name, value;
        if (!read_text(bytes, &cursor, &name, error) || !read_text(bytes, &cursor, &value, error)) return false;
        server->cvars[i].name = (char *)server->cvar_text.data + at;
        memcpy(server->cvar_text.data + at, name.data, name.size);
        server->cvar_text.data[at + name.size] = 0; at += name.size + 1;
        server->cvars[i].value = (char *)server->cvar_text.data + at;
        memcpy(server->cvar_text.data + at, value.data, value.size);
        server->cvar_text.data[at + value.size] = 0; at += value.size + 1;
    }
    return true;
}

bool qa_q2_save_server_decode(qa_bytes bytes, qa_q2_save_server *out, qa_error *error)
{
    if (!out || !bytes.data)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server.ssv requires its actual bytes");
    qa_q2_save_server server = {0}; bool ok;
    if (bytes.size >= 4 && !memcmp(bytes.data, "EVAS", 4)) {
        ok = rerelease_server_decode(bytes, &server, error);
    } else {
        if (bytes.size < Q2_SERVER_HEADER || (bytes.size - Q2_SERVER_HEADER) % Q2_SERVER_CVAR)
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server.ssv has a truncated fixed record");
        memcpy(server.comment, bytes.data, QA_Q2_SAVE_COMMENT_BYTES);
        memcpy(server.map_command, bytes.data + QA_Q2_SAVE_COMMENT_BYTES, QA_Q2_SAVE_MAP_COMMAND_BYTES);
        server.cvar_count = (bytes.size - Q2_SERVER_HEADER) / Q2_SERVER_CVAR;
        server.cvar_text.size = server.cvar_count * Q2_SERVER_CVAR;
        if (server.cvar_count) {
            server.cvars = malloc(server.cvar_count * sizeof(*server.cvars));
            server.cvar_text.data = malloc(server.cvar_text.size);
            if (!server.cvars || !server.cvar_text.data) {
                qa_q2_save_server_dispose(&server);
                return persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II latched cvars");
            }
            memcpy(server.cvar_text.data, bytes.data + Q2_SERVER_HEADER, server.cvar_text.size);
            for (size_t i = 0; i < server.cvar_count; ++i) {
                server.cvars[i].name = (char *)server.cvar_text.data + i * Q2_SERVER_CVAR;
                server.cvars[i].value = server.cvars[i].name + QA_Q2_SAVE_CVAR_BYTES;
            }
        }
        ok = true;
    }
    if (!ok || !server_text(&server, error)) { qa_q2_save_server_dispose(&server); return false; }
    *out = server; return true;
}

static void write_text(uint8_t *data, size_t *cursor, const char *text)
{
    size_t size = strlen(text); qa_store_u32le(data + *cursor, (uint32_t)size); *cursor += 4;
    memcpy(data + *cursor, text, size); *cursor += size;
}

bool qa_q2_save_server_encode(const qa_q2_save_server *server, qa_buffer *out, qa_error *error)
{
    if (!server || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server save owner/output are required");
    if (!server_text(server, error)) return false;
    size_t size;
    if (server->rerelease) {
        size = 21 + strlen(server->comment) + strlen(server->map_command);
        for (size_t i = 0; i < server->cvar_count; ++i) {
            size_t name = strlen(server->cvars[i].name), value = strlen(server->cvars[i].value);
            if (name > UINT32_MAX || value > UINT32_MAX || name > SIZE_MAX - value - 8 || size > SIZE_MAX - name - value - 8)
                return persistence_fail(error, QA_ERROR_MEMORY, "Quake II EVAS cvars exceed their file extent");
            size += name + value + 8;
        }
    } else {
        if (server->cvar_count > (SIZE_MAX - Q2_SERVER_HEADER) / Q2_SERVER_CVAR)
            return persistence_fail(error, QA_ERROR_MEMORY, "Quake II latched cvars exceed their file extent");
        size = Q2_SERVER_HEADER + server->cvar_count * Q2_SERVER_CVAR;
    }
    qa_buffer bytes = {.data = malloc(size), .size = size};
    if (!bytes.data) return persistence_fail(error, QA_ERROR_MEMORY, "Writing Quake II server.ssv");
    if (server->rerelease) {
        memcpy(bytes.data, "EVAS", 4); bytes.data[4] = server->autosave;
        size_t cursor = 5; write_text(bytes.data, &cursor, server->comment);
        qa_store_u64le(bytes.data + cursor, server->timestamp); cursor += 8;
        write_text(bytes.data, &cursor, server->map_command);
        for (size_t i = 0; i < server->cvar_count; ++i) {
            write_text(bytes.data, &cursor, server->cvars[i].name);
            write_text(bytes.data, &cursor, server->cvars[i].value);
        }
    } else {
        memcpy(bytes.data, server->comment, QA_Q2_SAVE_COMMENT_BYTES);
        memcpy(bytes.data + QA_Q2_SAVE_COMMENT_BYTES, server->map_command, QA_Q2_SAVE_MAP_COMMAND_BYTES);
        for (size_t i = 0; i < server->cvar_count; ++i) {
            memcpy(bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR, server->cvars[i].name, QA_Q2_SAVE_CVAR_BYTES);
            memcpy(bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR + QA_Q2_SAVE_CVAR_BYTES,
                server->cvars[i].value, QA_Q2_SAVE_CVAR_BYTES);
        }
    }
    *out = bytes; return true;
}

static bool level_name(const char *name, qa_error *error)
{
    size_t size = strlen(name);
    if (!size || size >= QA_Q2_SAVE_MAP_BYTES || !strcmp(name, ".") || !strcmp(name, ".."))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved map name is invalid");
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == '/' || c == '\\' || c == ':')
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved map name is outside its directory");
    }
    return true;
}

static char *file_path(const char *directory, const char *name, const char *suffix, qa_error *error)
{
    size_t a = strlen(directory), b = strlen(name), c = strlen(suffix);
    if (b > SIZE_MAX - 2 || a > SIZE_MAX - b - 2 || c > SIZE_MAX - a - b - 2) {
        persistence_fail(error, QA_ERROR_MEMORY, "Quake II save path exceeds memory"); return NULL;
    }
    char *path = malloc(a + b + c + 2);
    if (!path) { persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II save path"); return NULL; }
    memcpy(path, directory, a); path[a] = '/';
    memcpy(path + a + 1, name, b); memcpy(path + a + b + 1, suffix, c + 1);
    return path;
}

static bool read_file(qa_fs_root *root, const char *directory, const char *name,
    const char *suffix, qa_buffer *out, qa_error *error)
{
    char *path = file_path(directory, name, suffix, error);
    if (!path) return false;
    qa_fs_file *file = NULL; qa_fs_identity identity;
    bool ok = qa_fs_root_file_open(root, path, &file, &identity, error) &&
        qa_fs_file_read_snapshot(file, &identity, out, error);
    qa_fs_file_close(file); free(path);
    return ok;
}

typedef struct q2_save_files {
    qa_fs_root *root;
    const char *directory;
    qa_fs_listing files;
    qa_archive *pack;
    qa_buffer unpacked;
} q2_save_files;

static void close_files(q2_save_files *files)
{
    qa_fs_listing_free(&files->files); qa_archive_close(files->pack);
    qa_buffer_free(&files->unpacked); *files = (q2_save_files){0};
}

static bool open_pack(q2_save_files *files, qa_bytes bytes, qa_error *error)
{
    if (!bytes.data || bytes.size < 18 || bytes.size > UINT_MAX ||
        bytes.data[0] != 31 || bytes.data[1] != 139 || bytes.data[2] != 8)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease save lacks its gzip PACK payload");
    uint32_t size = qa_load_u32le(bytes.data + bytes.size - 4);
    if (size < 12 || size > INT32_MAX)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease PACK exceeds its original signed offsets");
    qa_buffer raw = {.data = malloc(size), .size = size};
    if (!raw.data) return persistence_fail(error, QA_ERROR_MEMORY, "Inflating original Quake II PACK state");
    z_stream stream = {0};
    int status = inflateInit2(&stream, 31);
    bool ok = status == Z_OK;
    if (ok) {
        stream.next_in = (Bytef *)(uintptr_t)bytes.data; stream.avail_in = (uInt)bytes.size;
        stream.next_out = raw.data; stream.avail_out = size;
        status = inflate(&stream, Z_FINISH);
        ok = status == Z_STREAM_END && stream.total_in == bytes.size && stream.total_out == raw.size;
        int ended = inflateEnd(&stream); if (ended != Z_OK) ok = false;
    }
    if (!ok) {
        qa_buffer_free(&raw);
        return persistence_fail(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT,
            "Quake II rerelease gzip state is truncated or corrupt");
    }
    if (!qa_archive_open_memory((qa_bytes){raw.data, raw.size}, QA_ARCHIVE_PAK, &files->pack, error)) {
        qa_buffer_free(&raw); return false;
    }
    files->unpacked = raw; return true;
}

static bool open_files(qa_fs_root *root, const char *path, q2_save_files *files, qa_error *error)
{
    qa_fs_entry_kind kind;
    if (!qa_fs_root_status(root, path, &kind, NULL, error)) return false;
    files->root = root; files->directory = path;
    if (kind == QA_FS_DIRECTORY) return qa_fs_root_list(root, path, &files->files, error);
    if (kind != QA_FS_REGULAR)
        return persistence_fail(error, QA_ERROR_NOT_FOUND, "Quake II original save is absent");
    qa_fs_file *file = NULL; qa_fs_identity identity; qa_buffer bytes = {0};
    bool ok = qa_fs_root_file_open(root, path, &file, &identity, error) &&
        qa_fs_file_read_snapshot(file, &identity, &bytes, error) &&
        open_pack(files, (qa_bytes){bytes.data, bytes.size}, error);
    qa_fs_file_close(file); qa_buffer_free(&bytes); return ok;
}

static bool read_member(const q2_save_files *files, const char *name, const char *suffix,
    qa_buffer *out, qa_error *error)
{
    if (!files->pack) return read_file(files->root, files->directory, name, suffix, out, error);
    size_t a = strlen(name), b = strlen(suffix);
    if (a + b >= 56) return persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved member exceeds its PACK name");
    char path[56]; memcpy(path, name, a); memcpy(path + a, suffix, b + 1);
    const qa_archive_entry *entry = NULL, *duplicate = NULL;
    if (!qa_archive_find_normalized(files->pack, path, QA_ARCHIVE_EXACT, 0, &entry, error)) return false;
    if (!entry || entry->is_directory) return persistence_fail(error, QA_ERROR_NOT_FOUND, "Quake II original PACK member is absent");
    if (!qa_archive_find_normalized(files->pack, path, QA_ARCHIVE_EXACT, entry->ordinal + 1, &duplicate, error)) return false;
    if (duplicate) return persistence_fail(error, QA_ERROR_FORMAT, "Quake II original PACK repeats a saved member");
    qa_archive_data data = {0};
    if (!qa_archive_read(files->pack, entry->ordinal, &data, error)) return false;
    qa_buffer value = {.data = data.bytes.size ? malloc(data.bytes.size) : NULL, .size = data.bytes.size};
    bool ok = !value.size || value.data;
    if (ok && value.size) memcpy(value.data, data.bytes.data, value.size);
    qa_archive_data_free(&data);
    if (!ok) return persistence_fail(error, QA_ERROR_MEMORY, "Retaining original Quake II module state");
    *out = value; return true;
}

static bool game_member(const q2_save_files *files, qa_error *error)
{
    if (files->pack) {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find_normalized(files->pack, "game.ssv", QA_ARCHIVE_EXACT, 0, &entry, error)) return false;
        return (entry && !entry->is_directory && entry->size) ||
            persistence_fail(error, QA_ERROR_NOT_FOUND, "Quake II original PACK has no GAME file");
    }
    char *path = file_path(files->directory, "game.ssv", "", error);
    if (!path) return false;
    qa_fs_entry_kind kind; qa_fs_identity identity;
    bool ok = qa_fs_root_status(files->root, path, &kind, &identity, error); free(path);
    if (ok && (kind != QA_FS_REGULAR || !qa_fs_identity_size(&identity)))
        ok = persistence_fail(error, QA_ERROR_NOT_FOUND, "Quake II original directory has no GAME file");
    return ok;
}

bool qa_q2_save_server_read(qa_fs_root *root, const char *path,
    qa_q2_save_server *out, qa_error *error)
{
    if (!root || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server location/output are required");
    if (!qa_save_slot_name(path, error)) return false;
    q2_save_files files = {0}; qa_buffer bytes = {0}; qa_q2_save_server server = {0};
    bool ok = open_files(root, path, &files, error) && game_member(&files, error) &&
        read_member(&files, "server.ssv", "", &bytes, error) &&
        qa_q2_save_server_decode((qa_bytes){bytes.data, bytes.size}, &server, error);
    if (ok && files.pack && !server.rerelease)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II original PACK lacks its EVAS server");
    if (ok) *out = server; else qa_q2_save_server_dispose(&server);
    qa_buffer_free(&bytes); close_files(&files); return ok;
}

static bool write_file(qa_fs_root *root, const char *directory, const char *name,
    const char *suffix, qa_bytes bytes, uint64_t nonce, qa_error *error)
{
    char *path = file_path(directory, name, suffix, error);
    if (!path) return false;
    bool ok = qa_fs_root_replace(root, path, bytes, nonce, error);
    free(path);
    return ok;
}

bool qa_q2_save_level_decode(qa_bytes bytes, qa_q2_save_level *level, qa_error *error)
{
    if (!level || !bytes.data) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II .sv2 requires its actual bytes");
    if (!level->rerelease) {
        if (bytes.size != Q2_LEVEL_BYTES)
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II .sv2 has an invalid configstring/portal extent");
        if (!qa_q2_save_configstrings_capture(level, (qa_bytes){bytes.data, Q2_LEVEL_STRINGS}, error)) return false;
        for (size_t i = 0; i < QA_Q2_SAVE_AREA_PORTALS; ++i)
            level->portal_open[i] = qa_load_i32le(bytes.data + Q2_LEVEL_STRINGS + i * 4);
        level->portal_count = 0; return true;
    }
    uint32_t rows = qa_q2_save_configstring_count(level), width = qa_q2_save_configstring_width(level);
    qa_buffer table = {.data = calloc(rows, width), .size = (size_t)rows * width};
    if (!table.data) return persistence_fail(error, QA_ERROR_MEMORY, "Reading Quake II rerelease config rows");
    size_t cursor = 0; bool ok = true;
    /* Each Source writer visits physical rows in ascending order. */
    int32_t previous = -1;
    for (;;) {
        if (bytes.size - cursor < 4) { ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease .sv2 lacks its terminator"); break; }
        int32_t index = qa_load_i32le(bytes.data + cursor); cursor += 4;
        if (index == -1) break;
        if (index < 0 || (uint32_t)index >= rows || index <= previous || cursor == bytes.size) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease config row leaves its physical table"); break;
        }
        uint8_t size = bytes.data[cursor++];
        if (size > width || size > bytes.size - cursor) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease config row is truncated or too wide"); break;
        }
        memcpy(table.data + (size_t)index * width, bytes.data + cursor, size);
        cursor += size; previous = index;
    }
    uint16_t portals = 0;
    if (ok) {
        if (bytes.size - cursor < 2) ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease .sv2 lacks its portal count");
        else {
            portals = qa_load_u16le(bytes.data + cursor); cursor += 2;
            if (portals > QA_Q2_SAVE_AREA_PORTALS || bytes.size - cursor != portals)
                ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease portal bytes leave the loaded CM extent");
        }
    }
    if (ok) ok = qa_q2_save_configstrings_capture(level, (qa_bytes){table.data, table.size}, error);
    if (ok) {
        memset(level->portal_open, 0, sizeof(level->portal_open));
        for (uint16_t i = 0; i < portals; ++i) level->portal_open[i] = bytes.data[cursor + i];
        level->portal_count = portals;
    }
    qa_buffer_free(&table); return ok;
}

bool qa_q2_save_level_encode(const qa_q2_save_level *level, qa_buffer *out, qa_error *error)
{
    if (!level || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II .sv2 requires its actual level owner");
    qa_buffer table = {0};
    if (!qa_q2_save_configstrings_expand(level, &table, error)) return false;
    size_t size = Q2_LEVEL_BYTES;
    if (level->rerelease) {
        if (level->portal_count > QA_Q2_SAVE_AREA_PORTALS) {
            qa_buffer_free(&table); return persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease portals exceed CM capacity");
        }
        size = 6 + level->portal_count;
        uint32_t rows = qa_q2_save_configstring_count(level), width = qa_q2_save_configstring_width(level);
        for (uint32_t i = 0; i < rows; ++i) {
            const uint8_t *row = table.data + (size_t)i * width;
            if (!*row) continue;
            const uint8_t *end = memchr(row, 0, width);
            size += 5 + (end ? (size_t)(end - row) : width);
        }
    }
    qa_buffer bytes = {.data = malloc(size), .size = size};
    if (!bytes.data) { qa_buffer_free(&table); return persistence_fail(error, QA_ERROR_MEMORY, "Writing original Quake II .sv2"); }
    if (!level->rerelease) {
        memcpy(bytes.data, table.data, Q2_LEVEL_STRINGS);
        for (size_t i = 0; i < QA_Q2_SAVE_AREA_PORTALS; ++i)
            qa_store_u32le(bytes.data + Q2_LEVEL_STRINGS + i * 4, (uint32_t)level->portal_open[i]);
    } else {
        size_t cursor = 0;
        uint32_t rows = qa_q2_save_configstring_count(level), width = qa_q2_save_configstring_width(level);
        for (uint32_t i = 0; i < rows; ++i) {
            const uint8_t *row = table.data + (size_t)i * width;
            if (!*row) continue;
            const uint8_t *end = memchr(row, 0, width);
            size_t length = end ? (size_t)(end - row) : width;
            qa_store_u32le(bytes.data + cursor, i); bytes.data[cursor + 4] = (uint8_t)length; cursor += 5;
            memcpy(bytes.data + cursor, row, length); cursor += length;
        }
        qa_store_u32le(bytes.data + cursor, UINT32_MAX); cursor += 4;
        qa_store_u16le(bytes.data + cursor, level->portal_count); cursor += 2;
        for (uint16_t i = 0; i < level->portal_count; ++i) bytes.data[cursor + i] = (uint8_t)level->portal_open[i];
    }
    qa_buffer_free(&table); *out = bytes; return true;
}

void qa_q2_save_destroy(qa_q2_save_data *save)
{
    if (!save) return;
    qa_q2_save_server_dispose(&save->server);
    qa_buffer_free(&save->game);
    for (size_t i = 0; i < save->level_count; ++i) qa_q2_save_level_dispose(save->levels + i);
    free(save->levels); free(save);
}

static bool decode_files(const q2_save_files *files, qa_q2_save_data **out, qa_error *error)
{
    qa_q2_save_data *save = calloc(1, sizeof(*save));
    if (!save) return persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II saved files");
    qa_buffer server = {0};
    bool ok = read_member(files, "server.ssv", "", &server, error) &&
        qa_q2_save_server_decode((qa_bytes){server.data, server.size}, &save->server, error) &&
        read_member(files, "game.ssv", "", &save->game, error);
    qa_buffer_free(&server);
    if (ok && files->pack && !save->server.rerelease)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease PACK lacks its actual EVAS server");
    if (ok && !save->game.size)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II original save has an empty GAME file");
    size_t count = files->pack ? qa_archive_count(files->pack) : files->files.count;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_archive_entry *member = files->pack ? qa_archive_entry_at(files->pack, i) : NULL;
        const qa_fs_entry *entry = files->pack ? NULL : files->files.entries + i;
        const char *path = member ? member->path : entry->name;
        bool regular = member ? !member->is_directory : entry->kind == QA_FS_REGULAR;
        size_t length = strlen(path);
        if (!regular || length < 5 || strcmp(path + length - 4, ".sav")) continue;
        size_t size = length - 4;
        if (size >= QA_Q2_SAVE_MAP_BYTES) { ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved map exceeds MAX_QPATH"); break; }
        char name[QA_Q2_SAVE_MAP_BYTES]; memcpy(name, path, size); name[size] = 0;
        if (!level_name(name, error)) { ok = false; break; }
        if (save->level_count == SIZE_MAX / sizeof(*save->levels)) {
            ok = persistence_fail(error, QA_ERROR_MEMORY, "Quake II saved level roster exceeds memory"); break;
        }
        qa_q2_save_level *next = realloc(save->levels, (save->level_count + 1) * sizeof(*next));
        if (!next) { ok = persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II saved level"); break; }
        save->levels = next;
        qa_q2_save_level *level = next + save->level_count++;
        *level = (qa_q2_save_level){.rerelease = save->server.rerelease}; memcpy(level->name, name, size + 1);
        qa_buffer engine = {0};
        ok = read_member(files, name, ".sav", &level->game, error) &&
            read_member(files, name, ".sv2", &engine, error) &&
            qa_q2_save_level_decode((qa_bytes){engine.data, engine.size}, level, error);
        qa_buffer_free(&engine);
        if (ok && !level->game.size)
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved LEVEL file is empty");
    }
    if (!ok) { qa_q2_save_destroy(save); return false; }
    *out = save; return true;
}

bool qa_q2_save_pack_decode(qa_bytes bytes, qa_q2_save_data **out, qa_error *error)
{
    if (!out || *out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II PACK read requires an empty output");
    q2_save_files files = {0};
    bool ok = open_pack(&files, bytes, error) && decode_files(&files, out, error);
    close_files(&files); return ok;
}

bool qa_q2_save_directory_read(qa_fs_root *root, const char *path,
    qa_q2_save_data **out, qa_error *error)
{
    if (!root || !out || *out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II original read requires an empty output");
    if (!qa_save_slot_name(path, error)) return false;
    q2_save_files files = {0};
    bool ok = open_files(root, path, &files, error) && decode_files(&files, out, error);
    close_files(&files); return ok;
}

static bool deflate_bytes(z_stream *stream, qa_bytes bytes, qa_error *error)
{
    stream->next_in = (Bytef *)(uintptr_t)bytes.data; stream->avail_in = (uInt)bytes.size;
    int status = deflate(stream, Z_NO_FLUSH);
    return (status == Z_OK && !stream->avail_in) || persistence_fail(error,
        status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_IO, "Compressing original Quake II saved state");
}

static void pack_member(uint8_t *row, const char *name, const char *suffix, uint32_t offset, uint32_t size)
{
    size_t a = strlen(name), b = strlen(suffix);
    memcpy(row, name, a); memcpy(row + a, suffix, b);
    qa_store_u32le(row + 56, offset); qa_store_u32le(row + 60, size);
}

static bool write_pack(qa_fs_root *root, const char *path, const qa_q2_save_data *save,
    qa_bytes server, uint64_t nonce, qa_error *error)
{
    if (save->level_count > (INT32_MAX / 64 - 2) / 2)
        return persistence_fail(error, QA_ERROR_MEMORY, "Quake II original PACK directory exceeds its signed extent");
    size_t members = 2 + save->level_count * 2;
    qa_buffer directory = {.data = calloc(members * 64 + 12, 1), .size = members * 64 + 12};
    qa_buffer *engine = save->level_count ? calloc(save->level_count, sizeof(*engine)) : NULL;
    if (!directory.data || (save->level_count && !engine)) {
        qa_buffer_free(&directory); free(engine);
        return persistence_fail(error, QA_ERROR_MEMORY, "Writing Quake II original PACK directory");
    }
    memcpy(directory.data, "PACK", 4); qa_store_u32le(directory.data + 4, 12);
    qa_store_u32le(directory.data + 8, (uint32_t)(members * 64));
    size_t extent = directory.size; bool ok = server.size <= INT32_MAX - directory.size &&
        save->game.size <= INT32_MAX - directory.size - server.size;
    if (ok) {
        pack_member(directory.data + 12, "server.ssv", "", (uint32_t)extent, (uint32_t)server.size); extent += server.size;
        pack_member(directory.data + 76, "game.ssv", "", (uint32_t)extent, (uint32_t)save->game.size); extent += save->game.size;
    }
    for (size_t i = 0; ok && i < save->level_count; ++i) {
        const qa_q2_save_level *level = save->levels + i;
        if (!level->rerelease || strlen(level->name) + 4 >= 56 || level->game.size > INT32_MAX) {
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II rerelease LEVEL has no representable PACK identity"); break;
        }
        ok = qa_q2_save_level_encode(level, engine + i, error);
        if (!ok) break;
        if (extent > INT32_MAX || level->game.size > INT32_MAX - extent ||
            engine[i].size > INT32_MAX - extent - level->game.size) {
            ok = persistence_fail(error, QA_ERROR_MEMORY, "Quake II original PACK payload exceeds signed offsets"); break;
        }
        uint8_t *row = directory.data + 12 + (2 + i * 2) * 64;
        pack_member(row, level->name, ".sav", (uint32_t)extent, (uint32_t)level->game.size); extent += level->game.size;
        pack_member(row + 64, level->name, ".sv2", (uint32_t)extent, (uint32_t)engine[i].size); extent += engine[i].size;
    }
    if (ok && extent > INT32_MAX) ok = persistence_fail(error, QA_ERROR_MEMORY, "Quake II original PACK exceeds signed offsets");
    if (!ok && (!error || error->code == QA_OK)) persistence_fail(error, QA_ERROR_MEMORY, "Quake II original PACK file is too large");
    z_stream stream = {0}; bool initialized = false; qa_buffer compressed = {0};
    if (ok) {
        int status = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 9, Z_DEFAULT_STRATEGY);
        initialized = status == Z_OK;
        if (!initialized) ok = persistence_fail(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_IO,
            "Opening Quake II original gzip state stream");
    }
    if (ok) {
        uLong bound = deflateBound(&stream, (uLong)extent);
        if (bound > UINT_MAX) ok = persistence_fail(error, QA_ERROR_MEMORY, "Quake II gzip output exceeds its stream extent");
        else {
            compressed = (qa_buffer){.data = malloc((size_t)bound), .size = (size_t)bound};
            if (!compressed.data) ok = persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II compressed state");
            else { stream.next_out = compressed.data; stream.avail_out = (uInt)bound; }
        }
    }
    if (ok) ok = deflate_bytes(&stream, (qa_bytes){directory.data, directory.size}, error) &&
        deflate_bytes(&stream, server, error) && deflate_bytes(&stream, (qa_bytes){save->game.data, save->game.size}, error);
    for (size_t i = 0; ok && i < save->level_count; ++i)
        ok = deflate_bytes(&stream, (qa_bytes){save->levels[i].game.data, save->levels[i].game.size}, error) &&
            deflate_bytes(&stream, (qa_bytes){engine[i].data, engine[i].size}, error);
    if (ok) {
        int status = deflate(&stream, Z_FINISH);
        ok = status == Z_STREAM_END || persistence_fail(error, QA_ERROR_IO, "Finishing Quake II original gzip state");
        compressed.size = stream.total_out;
    }
    if (initialized && deflateEnd(&stream) != Z_OK && ok)
        ok = persistence_fail(error, QA_ERROR_IO, "Closing Quake II original gzip state");
    if (ok) ok = qa_fs_root_replace(root, path, (qa_bytes){compressed.data, compressed.size}, nonce, error);
    for (size_t i = 0; i < save->level_count; ++i) qa_buffer_free(engine + i);
    free(engine); qa_buffer_free(&directory); qa_buffer_free(&compressed); return ok;
}

bool qa_q2_save_directory_write(qa_fs_root *root, const char *directory,
    const qa_q2_save_data *save, uint64_t nonce, qa_error *error)
{
    if (!root || !save) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II directory write requires its GAME state");
    if (!qa_save_slot_name(directory, error)) return false;
    if (!save->game.data || !save->game.size || (save->level_count && !save->levels))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II save requires its actual GAME and level files");
    qa_buffer server = {0};
    if (!qa_q2_save_server_encode(&save->server, &server, error)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < save->level_count; ++i) {
        const qa_q2_save_level *level = save->levels + i;
        ok = memchr(level->name, 0, sizeof(level->name)) && level_name(level->name, error) &&
            level->game.data && level->game.size;
        if (!ok && (!error || error->code == QA_OK))
            persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II saved level has no actual map/file owner");
        for (size_t prior = 0; ok && prior < i; ++prior)
            if (!strcmp(save->levels[prior].name, level->name))
                ok = persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II save repeats a level");
    }
    if (ok && save->server.rerelease) {
        ok = write_pack(root, directory, save, (qa_bytes){server.data, server.size}, nonce, error);
        qa_buffer_free(&server); return ok;
    }
    /* Publish the server entry last. Ordinary Source writes replace a named
     * slot's old per-level set; stale visited maps must not survive a new unit. */
    qa_fs_listing files = {0}; qa_error local = {0};
    if (ok && !qa_fs_root_list(root, directory, &files, &local) && local.code != QA_ERROR_NOT_FOUND) {
        ok = false; if (error) *error = local;
    }
    for (size_t i = 0; ok && i < files.count; ++i) {
        const qa_fs_entry *entry = files.entries + i;
        size_t length = strlen(entry->name);
        if (entry->kind != QA_FS_REGULAR) continue;
        bool server_file = !strcmp(entry->name, "server.ssv");
        bool level_file = length >= 4 &&
            (!strcmp(entry->name + length - 4, ".sav") || !strcmp(entry->name + length - 4, ".sv2"));
        if (!server_file && !level_file) continue;
        char *path = file_path(directory, entry->name, "", error);
        if (!path) { ok = false; break; }
        ok = qa_fs_root_remove(root, path, error); free(path);
    }
    qa_fs_listing_free(&files);
    if (ok) ok = write_file(root, directory, "game.ssv", "", (qa_bytes){save->game.data, save->game.size}, nonce, error);
    for (size_t i = 0; ok && i < save->level_count; ++i) {
        const qa_q2_save_level *level = save->levels + i;
        qa_buffer engine = {0};
        if (level->rerelease != save->server.rerelease)
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved level does not belong to its engine");
        if (ok) ok = qa_q2_save_level_encode(level, &engine, error) &&
            write_file(root, directory, level->name, ".sav", (qa_bytes){level->game.data, level->game.size}, nonce, error) &&
            write_file(root, directory, level->name, ".sv2", (qa_bytes){engine.data, engine.size}, nonce, error);
        qa_buffer_free(&engine);
    }
    if (ok) ok = write_file(root, directory, "server.ssv", "", (qa_bytes){server.data, server.size}, nonce, error);
    qa_buffer_free(&server);
    return ok;
}
