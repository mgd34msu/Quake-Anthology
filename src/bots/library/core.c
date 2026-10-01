#include "internal.h"

bool bot_grow(void **data, size_t *capacity, size_t need, size_t stride, qa_error *e) {
    if (need <= *capacity)
        return true;
    size_t count = *capacity == 0 ? 16 : *capacity;
    while (count < need && count <= SIZE_MAX / 2)
        count *= 2;
    if (count < need || count > SIZE_MAX / stride) {
        qa_error_set(e, QA_ERROR_MEMORY, need, "Bot resource capacity overflow");
        return false;
    }
    void *next = realloc(*data, count * stride);
    if (next == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, need, "Allocating bot resource storage");
        return false;
    }
    *data = next;
    *capacity = count;
    return true;
}
char *bot_string(qa_arena *arena, qa_bytes bytes, qa_error *e) {
    if (bytes.size == SIZE_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot string size overflow");
        return NULL;
    }
    char *out = qa_arena_alloc(arena, bytes.size + 1, 1, e);
    if (out == NULL)
        return NULL;
    if (bytes.size != 0)
        memcpy(out, bytes.data, bytes.size);
    out[bytes.size] = 0;
    return out;
}
bool bot_fail(qa_script *s, const char *message, qa_error *e) {
    qa_script_location location = qa_script_position(s);
    qa_error_set(e, QA_ERROR_FORMAT, location.offset, "%s:%u: %s",
                 location.path == NULL ? "<bot>" : location.path, location.line, message);
    return false;
}
void bot_warning(qa_bot_library *library, qa_script *s, const char *message) {
    const qa_script_services *services = &library->options.scripts;
    if (services->diagnostic != NULL) {
        qa_script_diagnostic d = {QA_SCRIPT_WARNING, qa_script_position(s), message};
        services->diagnostic(services->context, &d);
    }
}
bool bot_token(qa_script *s, qa_script_token *out, qa_error *e) {
    bool found;
    if (!qa_script_next(s, out, &found, e))
        return false;
    return found || bot_fail(s, "Unexpected end of bot resource", e);
}
bool bot_number(qa_script *s, float *out, bool signed_value, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    bool negative = qa_script_token_is(&token, "-");
    if (negative && !bot_token(s, &token, e))
        return false;
    if (token.kind != QA_SCRIPT_NUMBER || !isfinite(token.number) || !isfinite((float)token.number))
        return bot_fail(s, "Expected finite bot numeric value", e);
    *out = (float)token.number;
    if (negative && signed_value)
        *out = -*out;
    return true;
}
bool bot_integer(qa_script *s, int32_t *out, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    if (token.kind != QA_SCRIPT_NUMBER || (token.subtype & QA_SCRIPT_INTEGER) == 0)
        return bot_fail(s, "Expected integer bot resource value", e);
    *out = token.integer;
    return true;
}
float bot_random(const qa_bot_random_source *source) {
    return (float)(source->next(source->context) & 32767) / 32767.0f;
}
bool qa_bot_library_create(const qa_bot_library_options *options, qa_bot_library **out,
                           qa_error *e) {
    if (options == NULL || out == NULL || options->scripts.read == NULL ||
        options->scripts.release == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot resource services/output");
        return false;
    }
    qa_bot_library *library = calloc(1, sizeof(*library));
    if (library == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared bot resource library");
        return false;
    }
    library->options = *options;
    if (options->preprocessor.globals) {
        qa_script_defines_retain((qa_script_defines *)options->preprocessor.globals);
    } else {
        qa_script_defines *globals = NULL;
        if (!qa_script_defines_create(&globals, e)) {
            free(library);
            return false;
        }
        library->options.preprocessor.globals = globals;
    }
    if (options->preprocessor.include_path != NULL) {
        library->options.preprocessor.include_path =
            bot_string(&library->arena,
                       (qa_bytes){(const uint8_t *)options->preprocessor.include_path,
                                  strlen(options->preprocessor.include_path)},
                       e);
        if (library->options.preprocessor.include_path == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    if (options->scripts.date != NULL) {
        library->options.scripts.date = bot_string(
            &library->arena,
            (qa_bytes){(const uint8_t *)options->scripts.date, strlen(options->scripts.date)}, e);
        if (library->options.scripts.date == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    if (options->scripts.time != NULL) {
        library->options.scripts.time = bot_string(
            &library->arena,
            (qa_bytes){(const uint8_t *)options->scripts.time, strlen(options->scripts.time)}, e);
        if (library->options.scripts.time == NULL) {
            qa_bot_library_destroy(library);
            return false;
        }
    }
    *out = library;
    return true;
}
const qa_script_defines *qa_bot_library_global_defines(const qa_bot_library *library) {
    return library ? library->options.preprocessor.globals : NULL;
}
bool qa_bot_library_global_define(qa_bot_library *library, const char *definition, qa_error *e) {
    if (!library) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Bot global definition needs its actual library owner");
        return false;
    }
    return qa_script_defines_add((qa_script_defines *)library->options.preprocessor.globals, definition, e);
}
void qa_bot_library_destroy(qa_bot_library *library) {
    if (library == NULL)
        return;
    for (qa_bot_weights *c = library->weights; c != NULL;) {
        qa_bot_weights *next = c->next;
        qa_bot_weights_release(c);
        c = next;
    }
    for (qa_bot_character *c = library->characters; c != NULL;) {
        qa_bot_character *next = c->next;
        qa_bot_character_release(c);
        c = next;
    }
    for (qa_bot_weapons *c = library->weapon_configs; c != NULL;) {
        qa_bot_weapons *next = c->next;
        qa_bot_weapons_release(c);
        c = next;
    }
    for (qa_bot_items *c = library->item_configs; c != NULL;) {
        qa_bot_items *next = c->next;
        qa_bot_items_release(c);
        c = next;
    }
    bot_chat_assets_close(library);
    qa_bot_library_variables_clear(library);
    qa_script_defines_release((qa_script_defines *)library->options.preprocessor.globals);
    qa_arena_destroy(&library->arena);
    free(library);
}
void qa_bot_library_reload(qa_bot_library *library, bool reload) {
    if (library != NULL)
        library->options.reload_characters = reload;
}
bool bot_reload_characters(const qa_bot_library *library) {
    const qa_bot_variable *variable = qa_bot_library_variable(library, "bot_reloadcharacters");
    return variable ? variable->value != 0 : library->options.reload_characters;
}
