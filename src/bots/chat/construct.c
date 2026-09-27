#include "internal.h"
#include "qa/text.h"

const char *chat_random_string(qa_bot_chat_system *system, const char *name) {
    const qa_bot_chat_asset *a = system->options.randoms;
    if (a == NULL)
        return NULL;
    for (size_t i = 0; i < a->view.list_count; ++i) {
        const qa_bot_chat_list *list = a->lists + i;
        if (strcmp(list->name, name) != 0)
            continue;
        uint64_t index =
            (uint64_t)(float)(bot_random(&system->services.random) * (float)list->messages.count);
        if (index < list->messages.count)
            return a->messages[list->messages.first + (uint32_t)index];
    }
    return NULL;
}
static bool expand(qa_bot_chat *state, const char *source, uint32_t context,
                   const qa_bot_chat_match *match, uint32_t variable_context, bool reply,
                   bool *expanded, qa_error *e) {
    char output[256] = {0};
    size_t length = 0, pointer = 0;
    *expanded = false;
    while (source[pointer] != 0) {
        if (source[pointer] != 1) {
            if (length == 255)
                goto overflow;
            output[length++] = source[pointer++];
            output[length] = 0;
            continue;
        }
        char kind = source[++pointer];
        if (kind != 'v' && kind != 'r') {
            qa_error_set(e, QA_ERROR_FORMAT, pointer, "Invalid bot chat expansion escape");
            return false;
        }
        ++pointer;
        char key[256];
        size_t size = 0;
        while (source[pointer] != 0 && source[pointer] != 1) {
            if (size == 255)
                goto overflow;
            key[size++] = source[pointer++];
        }
        key[size] = 0;
        if (source[pointer] == 1)
            ++pointer;
        const char *value;
        char variable[256];
        if (kind == 'v') {
            uint32_t index = 0;
            if (size == 0)
                goto invalid_variable;
            for (size_t i = 0; i < size; ++i) {
                if (key[i] < '0' || key[i] > '9' || index >= 8)
                    goto invalid_variable;
                index = index * 10 + (uint32_t)(key[i] - '0');
            }
            if (index >= 8)
                goto invalid_variable;
            if (!qa_bot_chat_match_variable(match, index, variable, sizeof(variable), e) ||
                !qa_bot_chat_replace_synonyms(state->system, variable, sizeof(variable),
                                              variable_context, false, reply, e))
                return false;
            value = variable;
        } else {
            value = chat_random_string(state->system, key);
            if (value == NULL) {
                qa_error_set(e, QA_ERROR_NOT_FOUND, pointer,
                             "Unknown or unselected random bot chat string %s", key);
                return false;
            }
            *expanded = true;
        }
        size = strlen(value);
        if (size >= sizeof(output) - length)
            goto overflow;
        memcpy(output + length, value, size);
        length += size;
        output[length] = 0;
    }
    if (!qa_bot_chat_replace_synonyms(state->system, output, sizeof(output), context, true, false,
                                      e))
        return false;
    memcpy(state->message, output, strlen(output) + 1);
    return true;
invalid_variable:
    qa_error_set(e, QA_ERROR_FORMAT, pointer, "Bot chat variable is outside 0..7");
    return false;
overflow:
    qa_error_set(e, QA_ERROR_FORMAT, pointer, "Expanded bot chat exceeds 255 bytes");
    return false;
}
bool chat_construct(qa_bot_chat *state, const char *text, uint32_t context,
                    qa_bot_chat_match *match, uint32_t variable_context, bool reply, qa_error *e) {
    char source[256];
    chat_copy(source, sizeof(source), text);
    for (unsigned i = 0; i < 10; ++i) {
        bool expanded;
        if (!expand(state, source, context, match, variable_context, reply, &expanded, e))
            return false;
        if (!expanded)
            return true;
        memcpy(source, state->message, strlen(state->message) + 1);
    }
    chat_report(state->system, QA_SCRIPT_WARNING, "Bot chat random expansion exceeded ten passes");
    return true;
}
static void append_variables(qa_bot_chat_match *match, const char *const variables[8]) {
    if (variables == NULL)
        return;
    for (size_t i = 0; i < 8; ++i) {
        if (variables[i] == NULL)
            continue;
        size_t length = strlen(match->text), size = strlen(variables[i]);
        if (size > 255 - length)
            size = 255 - length;
        match->variables[i] = (qa_bot_chat_capture){(int16_t)length, (uint16_t)size};
        memcpy(match->text + length, variables[i], size);
        match->text[length + size] = 0;
    }
}
static const qa_bot_chat_list *initial_type(const qa_bot_chat *state, const char *name) {
    if (state == NULL || state->initial == NULL || name == NULL)
        return NULL;
    for (size_t i = 0; i < state->initial->view.list_count; ++i)
        if (chat_equal(state->initial->lists[i].name, name))
            return state->initial->lists + i;
    return NULL;
}
size_t qa_bot_chat_initial_count(const qa_bot_chat *state, const char *name) {
    const qa_bot_chat_list *list = initial_type(state, name);
    if (list == NULL)
        return 0;
    size_t count = list->messages.count;
    qa_bot_chat_services services = state->system->services;
    if (services.test_initial != NULL && services.test_initial(services.context)) {
        char text[352], number[32];
        qa_error ignored = {0};
        if (qa_format_number((double)count, number, &ignored)) {
            size_t size = strlen(name);
            if (size > 255)
                size = 255;
            memcpy(text, name, size);
            memcpy(text + size, " has ", 5);
            size += 5;
            size_t digits = strlen(number);
            memcpy(text + size, number, digits);
            size += digits;
            memcpy(text + size, " chat lines", 12);
            qa_bot_chat *retained = (qa_bot_chat *)state;
            chat_retain(retained);
            chat_report(state->system, QA_SCRIPT_INFO, text);
            if (!retained->retired)
                chat_report(state->system, QA_SCRIPT_INFO, "-------------------");
            chat_release(retained);
        }
    }
    return count;
}
static bool missing_initial(qa_bot_chat *state, const char *name, qa_error *e) {
    if (state->initial == NULL || !state->system->options.debug)
        return true;
    if (name == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "BotInitialChat: DEBUG print reads a null source type string");
        return false;
    }
    static const char prefix[] = "no chat messages of type ";
    size_t length = strlen(name);
    if (length > SIZE_MAX - sizeof(prefix)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot chat diagnostic size overflow");
        return false;
    }
    char *message = malloc(length + sizeof(prefix));
    if (message == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, length, "Retaining bot chat diagnostic");
        return false;
    }
    memcpy(message, prefix, sizeof(prefix) - 1);
    memcpy(message + sizeof(prefix) - 1, name, length + 1);
    chat_report(state->system, QA_SCRIPT_INFO, message);
    free(message);
    return true;
}
bool qa_bot_chat_initial(qa_bot_chat *state, const char *name, uint32_t context,
                         const char *const variables[8], float time, bool *found, qa_error *e) {
    if (state == NULL || state->retired || found == NULL || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial bot chat request");
        return false;
    }
    *found = false;
    const qa_bot_chat_list *type = initial_type(state, name);
    if (type == NULL)
        return missing_initial(state, name, e);
    qa_bot_chat_asset *asset = state->initial;
    uint32_t eligible = 0, selected = QA_BOT_NO_INDEX;
    for (uint32_t i = 0; i < type->messages.count; ++i)
        if (asset->cooldowns[type->messages.first + i] <= time)
            ++eligible;
    if (eligible == 0) {
        float best_time = 0;
        for (uint32_t i = 0; i < type->messages.count; ++i) {
            uint32_t index = type->messages.first + i;
            if (best_time == 0 || asset->cooldowns[index] < best_time) {
                selected = index;
                best_time = asset->cooldowns[index];
            }
        }
    } else {
        uint64_t draw =
            (uint64_t)(float)(bot_random(&state->system->services.random) * (float)eligible);
        for (uint32_t i = 0; i < type->messages.count; ++i) {
            uint32_t index = type->messages.first + i;
            if (asset->cooldowns[index] > time)
                continue;
            if (draw == 0) {
                selected = index;
                asset->cooldowns[index] = time + 20;
                break;
            }
            --draw;
        }
    }
    if (selected == QA_BOT_NO_INDEX)
        return missing_initial(state, name, e);
    qa_bot_chat_match match;
    chat_match_clear(&match, "");
    append_variables(&match, variables);
    if (!chat_construct(state, asset->messages[selected], context, &match, 0, false, e))
        return false;
    *found = true;
    return true;
}
static bool reply_key(const qa_bot_chat_asset *a, const qa_bot_chat_key *key,
                      const qa_bot_chat *state, const char *input, qa_bot_chat_match *match) {
    switch (key->kind) {
    case QA_BOT_CHAT_NAME:
        return qa_bot_chat_contains(input, state->name, false) >= 0;
    case QA_BOT_CHAT_GENDER:
        return state->gender == key->data.gender;
    case QA_BOT_CHAT_BOT_NAMES:
        return qa_bot_chat_contains(key->data.text, state->name, false) >= 0;
    case QA_BOT_CHAT_WORD:
        return chat_word(input, key->data.text, 0) >= 0;
    case QA_BOT_CHAT_PATTERN:
        return chat_match_pieces(a, key->data.pieces, match);
    }
    return false;
}
bool qa_bot_chat_reply_message(qa_bot_chat *state, const char *input, uint32_t context,
                               uint32_t variable_context, const char *const variables[8],
                               float time, bool *found, qa_error *e) {
    if (state == NULL || input == NULL || found == NULL || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot reply request");
        return false;
    }
    *found = false;
    qa_bot_chat_asset *asset = state->system->options.replies;
    if (asset == NULL)
        return true;
    qa_bot_chat_match match, best_match;
    chat_match_clear(&match, input);
    float priority = -1;
    uint32_t selected = QA_BOT_NO_INDEX;
    qa_bot_chat_range selected_messages = {0};
    for (size_t i = 0; i < asset->view.reply_count; ++i) {
        const qa_bot_chat_reply *reply = asset->replies + i;
        bool matches = false;
        for (uint32_t j = 0; j < reply->keys.count; ++j) {
            const qa_bot_chat_key *key = asset->keys + reply->keys.first + j;
            bool result = reply_key(asset, key, state, input, &match);
            if (key->mode == QA_BOT_CHAT_AND) {
                if (!result) {
                    matches = false;
                    break;
                }
            } else if (key->mode == QA_BOT_CHAT_NOT) {
                if (result) {
                    matches = false;
                    break;
                }
            } else if (result)
                matches = true;
        }
        if (!matches || reply->priority <= priority)
            continue;
        uint32_t eligible = 0;
        for (uint32_t j = 0; j < reply->messages.count; ++j)
            if (asset->cooldowns[reply->messages.first + j] <= time)
                ++eligible;
        uint64_t draw =
            (uint64_t)(float)(bot_random(&state->system->services.random) * (float)eligible);
        for (uint32_t j = 0; j < reply->messages.count; ++j) {
            /* The source decrements before checking cooldown, including when
             * eligible is zero; preserve its observable message selection. */
            if (draw == 0) {
                selected = reply->messages.first + j;
                selected_messages = reply->messages;
                best_match = match;
                priority = truncf(reply->priority);
                break;
            }
            --draw;
        }
    }
    if (selected == QA_BOT_NO_INDEX)
        return true;
    append_variables(&best_match, variables);
    qa_bot_chat_services services = state->system->services;
    if (services.test_reply != NULL && services.test_reply(services.context)) {
        chat_retain(state);
        qa_bot_chat_asset_retain(asset);
        uint64_t revision = state->system->revision;
        bool ok = true;
        for (uint32_t i = 0; ok && !state->retired && state->system->revision == revision &&
                             i < selected_messages.count;
             ++i) {
            ok = chat_construct(state, asset->messages[selected_messages.first + i], context,
                                &best_match, variable_context, true, e);
            if (ok && !state->retired) {
                chat_strip_tildes(state->message);
                chat_report(state->system, QA_SCRIPT_INFO, state->message);
            }
        }
        qa_bot_chat_asset_release(asset);
        chat_release(state);
        if (!ok)
            return false;
    } else {
        asset->cooldowns[selected] = time + 20;
        if (!chat_construct(state, asset->messages[selected], context, &best_match,
                            variable_context, true, e))
            return false;
    }
    *found = true;
    return true;
}
