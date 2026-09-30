#include "map/internal.h"

static char *copy_text(const char *text, qa_error *error) {
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        q3_fail(error, "Q3 configstring exceeds addressable source storage");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 source configstring");
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}
bool qa_q3_configstring_read(const qa_q3_game *game, uint32_t index,
                            const char **out, qa_error *error) {
    if (!game || !out || index >= QA_Q3_NATIVE_CONFIGSTRINGS)
        return q3_fail(error, "invalid Q3 source configstring query");
    *out = game->configstrings[index] ? game->configstrings[index] : "";
    return true;
}
static bool write_value(qa_q3_game *game, uint32_t index, const char *text,
                         const qa_q3_map_event *event, qa_error *error) {
    const char *prior = game->configstrings[index] ? game->configstrings[index] : "";
    if (!strcmp(prior, text)) return true;
    char *copy = *text ? copy_text(text, error) : NULL;
    if (*text && !copy) return false;
    free(game->configstrings[index]);
    game->configstrings[index] = copy;
    return !event || q3_map_emit(game, event, error);
}
bool q3_configstring_event(qa_q3_game *game, const qa_q3_map_event *event, qa_error *error) {
    if (!game || !game->map || !event || event->kind != QA_Q3_MAP_CONFIGSTRING ||
        event->index < 0 || (uint32_t)event->index >= QA_Q3_NATIVE_CONFIGSTRINGS ||
        game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 source configstring producer");
    qa_q3_map_event captured = *event;
    const char *text = captured.text ? q3_map_cstr(game, captured.text) : "";
    if (!text) return q3_fail(error, "Q3 configstring has no actual source text");
    ++game->observation_depth;
    bool result = write_value(game, (uint32_t)captured.index, text, &captured, error);
    --game->observation_depth;
    return result;
}
bool qa_q3_configstring_write(qa_q3_game *game, uint32_t index,
                             const char *text, qa_error *error) {
    if (!game || index >= QA_Q3_NATIVE_CONFIGSTRINGS || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 source configstring write boundary");
    if (!text) text = "";
    const char *prior = game->configstrings[index] ? game->configstrings[index] : "";
    if (!strcmp(prior, text)) return true;
    qa_q3_map_event event = {.kind = QA_Q3_MAP_CONFIGSTRING, .index = (int32_t)index};
    if (game->map && !q3_map_intern_cstr(game, text, &event.text, error)) return false;
    ++game->observation_depth;
    bool result = write_value(game, index, text, game->map ? &event : NULL, error);
    --game->observation_depth;
    return result;
}
void q3_configstrings_clear(qa_q3_game *game) {
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CONFIGSTRINGS; ++i) {
        free(game->configstrings[i]);
        game->configstrings[i] = NULL;
    }
}
bool q3_configstrings_capture(const qa_q3_game *game, qa_q3_checkpoint *saved, qa_error *error) {
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CONFIGSTRINGS; ++i)
        if (game->configstrings[i]) ++saved->configstring_count;
    if (!saved->configstring_count) return true;
    saved->configstrings = calloc(saved->configstring_count, sizeof(*saved->configstrings));
    if (!saved->configstrings) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 configstring checkpoint slots");
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CONFIGSTRINGS; ++i) {
        if (!game->configstrings[i]) continue;
        char *copy = copy_text(game->configstrings[i], error);
        if (!copy) return false;
        saved->configstrings[at++] = (qa_q3_saved_configstring){i, copy};
    }
    return true;
}
void q3_configstrings_discard(char **table) {
    if (!table) return;
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CONFIGSTRINGS; ++i) free(table[i]);
    free(table);
}
bool q3_configstrings_prepare(const qa_q3_checkpoint *saved, char ***out, qa_error *error) {
    if (saved->configstring_count > QA_Q3_NATIVE_CONFIGSTRINGS ||
        (saved->configstring_count && !saved->configstrings))
        return q3_fail(error, "invalid Q3 configstring checkpoint count");
    char **table = calloc(QA_Q3_NATIVE_CONFIGSTRINGS, sizeof(*table));
    if (!table) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 configstring restore slots");
        return false;
    }
    for (size_t i = 0; i < saved->configstring_count; ++i) {
        const qa_q3_saved_configstring *entry = &saved->configstrings[i];
        if (entry->index >= QA_Q3_NATIVE_CONFIGSTRINGS || !entry->text || !*entry->text ||
            (i && entry->index <= saved->configstrings[i - 1].index)) {
            q3_configstrings_discard(table);
            return q3_fail(error, "invalid Q3 source configstring checkpoint slot");
        }
        table[entry->index] = copy_text(entry->text, error);
        if (!table[entry->index]) { q3_configstrings_discard(table); return false; }
    }
    *out = table;
    return true;
}
void q3_configstrings_commit(qa_q3_game *game, char **table) {
    q3_configstrings_clear(game);
    memcpy(game->configstrings, table, sizeof(game->configstrings));
    free(table);
}
