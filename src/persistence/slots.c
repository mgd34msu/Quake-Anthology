#include "internal.h"
#include "qa/persistence_slots.h"
#include "qa/q1_save.h"

void qa_q1_save_slot_metadata_dispose(qa_q1_save_slot_metadata *source)
{
    if (!source) return;
    free(source->comment); free(source->map); free(source->game_directories); free(source->world_message);
    free(source->killed_monsters); free(source->total_monsters);
    free(source->found_secrets); free(source->total_secrets);
    *source = (qa_q1_save_slot_metadata){0};
}
static bool source_value(const qa_q1_save_record *record, const char *key, char **out, qa_error *error)
{
    const char *value = NULL;
    for (size_t i = 0; i < record->count; ++i)
        if (!strcmp(record->pairs[i].key, key)) value = record->pairs[i].value;
    if (!value) return true;
    size_t size = strlen(value) + 1;
    *out = malloc(size);
    if (!*out) return persistence_fail(error, QA_ERROR_MEMORY, "Retaining original save metadata text");
    memcpy(*out, value, size);
    return true;
}

bool qa_save_slot_inspect(qa_fs_root *root, const char *name,
    qa_save_slot_format *format, qa_save_metadata *out, qa_q1_save_slot_metadata *original,
    qa_q2_save_slot_metadata *q2, qa_error *error)
{
    if (!format || !out || !original)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing save slot metadata output");
    if (!root || !qa_save_slot_name(name, error)) return false;
    qa_fs_entry_kind entry_kind;
    if (!qa_fs_root_status(root, name, &entry_kind, NULL, error)) return false;
    if (entry_kind == QA_FS_DIRECTORY) {
        qa_q2_save_server server = {0};
        if (!qa_q2_save_server_read(root, name, &server, error)) return false;
        size_t size = strlen(name);
        char *game_path = malloc(size + sizeof("/game.ssv"));
        bool ok = game_path != NULL;
        if (!ok) persistence_fail(error, QA_ERROR_MEMORY, "Retaining Quake II GAME save path");
        if (ok) {
            memcpy(game_path, name, size); memcpy(game_path + size, "/game.ssv", sizeof("/game.ssv"));
            ok = qa_fs_root_status(root, game_path, &entry_kind, NULL, error);
            if (ok && entry_kind != QA_FS_REGULAR)
                ok = persistence_fail(error, QA_ERROR_NOT_FOUND, "Quake II save directory has no GAME file");
        }
        free(game_path);
        qa_q2_save_slot_metadata metadata = {0};
        if (ok) {
            memcpy(metadata.comment, server.comment, sizeof(metadata.comment));
            memcpy(metadata.map_command, server.map_command, sizeof(metadata.map_command));
            memcpy(metadata.game_directory, "baseq2", sizeof("baseq2"));
            for (size_t i = 0; i < server.cvar_count; ++i)
                if (!strcmp(server.cvars[i].name, "game")) {
                    memset(metadata.game_directory, 0, sizeof(metadata.game_directory));
                    memcpy(metadata.game_directory, server.cvars[i].value, sizeof(metadata.game_directory));
                    if (!metadata.game_directory[0]) memcpy(metadata.game_directory, "baseq2", sizeof("baseq2"));
                }
            *format = QA_SAVE_SLOT_Q2_CLASSIC; *out = (qa_save_metadata){0};
            *original = (qa_q1_save_slot_metadata){0}; if (q2) *q2 = metadata;
        }
        qa_q2_save_server_dispose(&server);
        return ok;
    }
    qa_fs_file *file = NULL; qa_fs_identity identity; qa_save_metadata summary = {0}; bool is_shared = false;
    bool inspected = qa_fs_root_file_open(root, name, &file, &identity, error) &&
        qa_save_image_metadata_read(file, &identity, &summary, &is_shared, error);
    qa_fs_file_close(file);
    if (!inspected) return false;
    if (is_shared) {
        *format = QA_SAVE_SLOT_SHARED; *out = summary; *original = (qa_q1_save_slot_metadata){0};
        if (q2) *q2 = (qa_q2_save_slot_metadata){0};
        return true;
    }
    qa_save_image *image = NULL; qa_q1_save_data *save = NULL; qa_q2_save_data *directory = NULL;
    if (!qa_saved_game_read(root, name, &image, &save, &directory, error))
        return false;
    qa_save_slot_format kind = QA_SAVE_SLOT_SHARED;
    qa_save_metadata shared = {0}; qa_q1_save_slot_metadata source = {0};
    bool ok = true;
    if (image) shared = *qa_save_image_metadata(image);
    else {
        kind = save->version == 5 ? QA_SAVE_SLOT_Q1_V5 : QA_SAVE_SLOT_Q1_V6;
        source.comment = save->comment; save->comment = NULL;
        source.map = save->map; save->map = NULL;
        source.game_directories = save->game_directories; save->game_directories = NULL;
        source.time = save->time; source.skill = save->skill; source.entity_count = save->entity_count;
        source.player_record_present = save->entities[1].count != 0;
        ok = source_value(save->entities, "message", &source.world_message, error) &&
            source_value(&save->globals, "killed_monsters", &source.killed_monsters, error) &&
            source_value(&save->globals, "total_monsters", &source.total_monsters, error) &&
            source_value(&save->globals, "found_secrets", &source.found_secrets, error) &&
            source_value(&save->globals, "total_secrets", &source.total_secrets, error);
    }
    if (!qa_save_image_destroy_checked(&image, error)) ok = false;
    qa_q1_save_destroy(save);
    qa_q2_save_destroy(directory);
    if (!ok) { qa_q1_save_slot_metadata_dispose(&source); return false; }
    *format = kind; *out = shared; *original = source;
    if (q2) *q2 = (qa_q2_save_slot_metadata){0};
    return true;
}

static char *slot_path(const char *directory, const char *name, qa_error *error)
{
    size_t prefix = strlen(directory), length = strlen(name);
    size_t separator = prefix ? 1u : 0u;
    if (prefix > SIZE_MAX - separator - 1 || length > SIZE_MAX - prefix - separator - 1) {
        persistence_fail(error, QA_ERROR_MEMORY, "Save slot path exceeds memory extent");
        return NULL;
    }
    char *path = malloc(prefix + separator + length + 1);
    if (!path) {
        persistence_fail(error, QA_ERROR_MEMORY, "Allocating contained save slot path");
        return NULL;
    }
    memcpy(path, directory, prefix);
    if (separator)
        path[prefix] = '/';
    memcpy(path + prefix + separator, name, length + 1);
    return path;
}

static int slot_compare(const void *left, const void *right)
{
    const qa_save_slot_entry *a = left, *b = right;
    return strcmp(a->name, b->name);
}

void qa_save_slot_listing_free(qa_save_slot_listing *listing)
{
    if (!listing)
        return;
    for (size_t i = 0; i < listing->count; ++i) {
        free(listing->entries[i].name);
        qa_q1_save_slot_metadata_dispose(&listing->entries[i].source);
    }
    free(listing->entries);
    *listing = (qa_save_slot_listing){0};
}

bool qa_save_slots_list(qa_fs_root *root, const char *directory,
                        qa_save_slot_listing *out, qa_error *error)
{
    if (!root || !directory || !out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing save slot directory or output");
    char *probe = slot_path(directory, "listing.sav", error);
    if (!probe)
        return false;
    bool valid = qa_save_slot_name(probe, error);
    free(probe);
    if (!valid)
        return false;
    qa_fs_listing files = {0};
    qa_error local = {0};
    if (!qa_fs_root_list(root, directory, &files, &local)) {
        qa_fs_listing_free(&files);
        if (local.code == QA_ERROR_NOT_FOUND) {
            *out = (qa_save_slot_listing){0};
            return true;
        }
        if (error)
            *error = local;
        return false;
    }
    qa_save_slot_listing candidate = {0};
    if (files.count) {
        candidate.entries = calloc(files.count, sizeof(*candidate.entries));
        if (!candidate.entries) {
            qa_fs_listing_free(&files);
            return persistence_fail(error, QA_ERROR_MEMORY, "Allocating save slot inventory");
        }
    }
    bool ok = true;
    for (size_t i = 0; ok && i < files.count; ++i) {
        if (files.entries[i].kind != QA_FS_REGULAR && files.entries[i].kind != QA_FS_DIRECTORY)
            continue;
        size_t leaf_length = strlen(files.entries[i].name);
        if (files.entries[i].kind == QA_FS_REGULAR &&
            (leaf_length < 5 || strcmp(files.entries[i].name + leaf_length - 4, ".sav"))) continue;
        char *path = slot_path(directory, files.entries[i].name, error);
        if (!path) {
            ok = false;
            break;
        }
        if (!qa_save_slot_name(path, NULL)) {
            free(path);
            continue;
        }
        if (files.entries[i].kind == QA_FS_DIRECTORY) {
            char *server = slot_path(path, "server.ssv", error);
            if (!server) { free(path); ok = false; break; }
            qa_fs_entry_kind kind;
            qa_error status = {0};
            bool present = qa_fs_root_status(root, server, &kind, NULL, &status);
            free(server);
            if (!present) {
                free(path);
                if (status.code == QA_ERROR_NOT_FOUND) continue;
                if (error) *error = status;
                ok = false; break;
            }
            if (kind != QA_FS_REGULAR) { free(path); continue; }
        }
        qa_save_slot_entry *entry = candidate.entries + candidate.count++;
        entry->name = path;
        if (!qa_save_slot_inspect(root, path, &entry->format, &entry->metadata, &entry->source, &entry->q2, &entry->error)) {
            if (entry->error.code == QA_ERROR_MEMORY) {
                if (error)
                    *error = entry->error;
                ok = false;
            } else if (entry->error.code == QA_OK) {
                qa_error_set(&entry->error, QA_ERROR_FORMAT, 0, "Unable to verify save slot image");
            }
        }
    }
    qa_fs_listing_free(&files);
    if (!ok) {
        qa_save_slot_listing_free(&candidate);
        return false;
    }
    if (candidate.count > 1)
        qsort(candidate.entries, candidate.count, sizeof(*candidate.entries), slot_compare);
    *out = candidate;
    return true;
}
