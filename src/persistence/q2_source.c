#include "internal.h"
#include "qa/q2_save.h"
#include "qa/binary.h"

#define Q2_SERVER_HEADER (QA_Q2_SAVE_COMMENT_BYTES + QA_Q2_SAVE_MAP_COMMAND_BYTES)
#define Q2_SERVER_CVAR (QA_Q2_SAVE_CVAR_BYTES * 2)
#define Q2_LEVEL_STRINGS (QA_Q2_SAVE_CONFIGSTRINGS * QA_Q2_SAVE_CONFIGSTRING_BYTES)
#define Q2_LEVEL_BYTES (Q2_LEVEL_STRINGS + QA_Q2_SAVE_AREA_PORTALS * 4)

void qa_q2_save_server_dispose(qa_q2_save_server *server)
{
    if (!server) return;
    free(server->cvars);
    *server = (qa_q2_save_server){0};
}

static bool server_text(const qa_q2_save_server *server, qa_error *error)
{
    if (!memchr(server->comment, 0, sizeof(server->comment)) ||
        !memchr(server->map_command, 0, sizeof(server->map_command)) || !server->map_command[0] ||
        (server->cvar_count && !server->cvars))
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server save fields are unterminated or absent");
    for (size_t i = 0; i < server->cvar_count; ++i) {
        const qa_q2_save_cvar *cvar = server->cvars + i;
        if (!cvar->name[0] || !memchr(cvar->name, 0, sizeof(cvar->name)) ||
            !memchr(cvar->value, 0, sizeof(cvar->value)))
            return persistence_fail(error, QA_ERROR_FORMAT, "Quake II latched cvar field is unterminated or absent");
    }
    return true;
}

bool qa_q2_save_server_decode(qa_bytes bytes, qa_q2_save_server *out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < Q2_SERVER_HEADER ||
        (bytes.size - Q2_SERVER_HEADER) % Q2_SERVER_CVAR)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II server.ssv has a truncated fixed record");
    if (!memcmp(bytes.data, "SSV2", 4))
        return persistence_fail(error, QA_ERROR_UNSUPPORTED, "This Quake II engine save container requires its original dialect");
    qa_q2_save_server server = {0};
    memcpy(server.comment, bytes.data, sizeof(server.comment));
    memcpy(server.map_command, bytes.data + sizeof(server.comment), sizeof(server.map_command));
    server.cvar_count = (bytes.size - Q2_SERVER_HEADER) / Q2_SERVER_CVAR;
    if (server.cvar_count) {
        server.cvars = malloc(server.cvar_count * sizeof(*server.cvars));
        if (!server.cvars)
            return persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II latched cvars");
        for (size_t i = 0; i < server.cvar_count; ++i) {
            memcpy(server.cvars[i].name, bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR,
                sizeof(server.cvars[i].name));
            memcpy(server.cvars[i].value, bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR + QA_Q2_SAVE_CVAR_BYTES,
                sizeof(server.cvars[i].value));
        }
    }
    if (!server_text(&server, error)) { qa_q2_save_server_dispose(&server); return false; }
    *out = server;
    return true;
}

bool qa_q2_save_server_encode(const qa_q2_save_server *server, qa_buffer *out, qa_error *error)
{
    if (!server || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server save owner/output are required");
    if (!server_text(server, error)) return false;
    if (server->cvar_count > (SIZE_MAX - Q2_SERVER_HEADER) / Q2_SERVER_CVAR)
        return persistence_fail(error, QA_ERROR_MEMORY, "Quake II latched cvars exceed their file extent");
    qa_buffer bytes = {.size = Q2_SERVER_HEADER + server->cvar_count * Q2_SERVER_CVAR};
    bytes.data = malloc(bytes.size);
    if (!bytes.data) return persistence_fail(error, QA_ERROR_MEMORY, "Writing Quake II server.ssv");
    memcpy(bytes.data, server->comment, sizeof(server->comment));
    memcpy(bytes.data + sizeof(server->comment), server->map_command, sizeof(server->map_command));
    for (size_t i = 0; i < server->cvar_count; ++i) {
        memcpy(bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR, server->cvars[i].name,
            sizeof(server->cvars[i].name));
        memcpy(bytes.data + Q2_SERVER_HEADER + i * Q2_SERVER_CVAR + QA_Q2_SAVE_CVAR_BYTES, server->cvars[i].value,
            sizeof(server->cvars[i].value));
    }
    *out = bytes;
    return true;
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

bool qa_q2_save_server_read(qa_fs_root *root, const char *directory,
    qa_q2_save_server *out, qa_error *error)
{
    if (!root || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II server directory/output are required");
    if (!qa_save_slot_name(directory, error)) return false;
    qa_buffer bytes = {0};
    bool ok = read_file(root, directory, "server.ssv", "", &bytes, error) &&
        qa_q2_save_server_decode((qa_bytes){bytes.data, bytes.size}, out, error);
    qa_buffer_free(&bytes);
    return ok;
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

static bool level_decode(qa_bytes bytes, qa_q2_save_level *level, qa_error *error)
{
    if (bytes.size != Q2_LEVEL_BYTES)
        return persistence_fail(error, QA_ERROR_FORMAT, "Quake II .sv2 has an invalid configstring/portal extent");
    memcpy(level->configstrings, bytes.data, Q2_LEVEL_STRINGS);
    for (size_t i = 0; i < QA_Q2_SAVE_AREA_PORTALS; ++i)
        level->portal_open[i] = qa_load_i32le(bytes.data + Q2_LEVEL_STRINGS + i * 4);
    return true;
}

void qa_q2_save_destroy(qa_q2_save_data *save)
{
    if (!save) return;
    qa_q2_save_server_dispose(&save->server);
    qa_buffer_free(&save->game);
    for (size_t i = 0; i < save->level_count; ++i) qa_buffer_free(&save->levels[i].game);
    free(save->levels); free(save);
}

bool qa_q2_save_directory_read(qa_fs_root *root, const char *directory,
    qa_q2_save_data **out, qa_error *error)
{
    if (!root || !out || *out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Quake II directory read requires an empty output");
    if (!qa_save_slot_name(directory, error)) return false;
    qa_q2_save_data *save = calloc(1, sizeof(*save));
    if (!save) return persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II save directory");
    qa_fs_listing files = {0};
    bool ok = qa_q2_save_server_read(root, directory, &save->server, error) &&
        read_file(root, directory, "game.ssv", "", &save->game, error) &&
        qa_fs_root_list(root, directory, &files, error);
    if (ok && !save->game.size)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II save directory has an empty GAME file");
    for (size_t i = 0; ok && i < files.count; ++i) {
        const qa_fs_entry *entry = files.entries + i;
        size_t length = strlen(entry->name);
        if (entry->kind != QA_FS_REGULAR || length < 5 || strcmp(entry->name + length - 4, ".sav")) continue;
        size_t size = length - 4;
        if (size >= QA_Q2_SAVE_MAP_BYTES) { ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved map exceeds MAX_QPATH"); break; }
        char name[QA_Q2_SAVE_MAP_BYTES]; memcpy(name, entry->name, size); name[size] = 0;
        if (!level_name(name, error)) { ok = false; break; }
        if (save->level_count == SIZE_MAX / sizeof(*save->levels)) {
            ok = persistence_fail(error, QA_ERROR_MEMORY, "Quake II saved level roster exceeds memory"); break;
        }
        qa_q2_save_level *next = realloc(save->levels, (save->level_count + 1) * sizeof(*next));
        if (!next) { ok = persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II saved level"); break; }
        save->levels = next;
        qa_q2_save_level *level = next + save->level_count++;
        *level = (qa_q2_save_level){0}; memcpy(level->name, name, size + 1);
        qa_buffer engine = {0};
        ok = read_file(root, directory, name, ".sav", &level->game, error) &&
            read_file(root, directory, name, ".sv2", &engine, error) &&
            level_decode((qa_bytes){engine.data, engine.size}, level, error);
        qa_buffer_free(&engine);
        if (ok && !level->game.size)
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Quake II saved LEVEL file is empty");
    }
    qa_fs_listing_free(&files);
    if (!ok) { qa_q2_save_destroy(save); return false; }
    *out = save;
    return true;
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
    qa_buffer engine = {0};
    if (ok && save->level_count) {
        engine = (qa_buffer){.data = malloc(Q2_LEVEL_BYTES), .size = Q2_LEVEL_BYTES};
        if (!engine.data) ok = persistence_fail(error, QA_ERROR_MEMORY, "Writing Quake II configstrings and portals");
    }
    for (size_t i = 0; ok && i < save->level_count; ++i) {
        const qa_q2_save_level *level = save->levels + i;
        memcpy(engine.data, level->configstrings, Q2_LEVEL_STRINGS);
        for (size_t portal = 0; portal < QA_Q2_SAVE_AREA_PORTALS; ++portal)
            qa_store_u32le(engine.data + Q2_LEVEL_STRINGS + portal * 4, (uint32_t)level->portal_open[portal]);
        ok = write_file(root, directory, level->name, ".sav", (qa_bytes){level->game.data, level->game.size}, nonce, error) &&
            write_file(root, directory, level->name, ".sv2", (qa_bytes){engine.data, engine.size}, nonce, error);
    }
    if (ok) ok = write_file(root, directory, "server.ssv", "", (qa_bytes){server.data, server.size}, nonce, error);
    qa_buffer_free(&engine); qa_buffer_free(&server);
    return ok;
}
