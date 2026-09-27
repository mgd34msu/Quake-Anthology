#include "internal.h"

static unsigned fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
void chat_copy(char *out, size_t capacity, const char *text) {
    if (capacity == 0)
        return;
    size_t size = text == NULL ? 0 : strlen(text);
    if (size >= capacity)
        size = capacity - 1;
    if (size != 0)
        memmove(out, text, size);
    out[size] = 0;
}
bool chat_equal(const char *a, const char *b) {
    if (a == NULL || b == NULL)
        return false;
    while (fold((uint8_t)*a) == fold((uint8_t)*b)) {
        if (*a == 0)
            return true;
        ++a;
        ++b;
    }
    return false;
}
int32_t qa_bot_chat_contains(const char *text, const char *part, bool sensitive) {
    if (text == NULL || part == NULL)
        return -1;
    size_t length = strlen(text), size = strlen(part);
    if (size > length)
        return -1;
    for (size_t i = 0; i <= length - size && i <= INT32_MAX; ++i) {
        size_t j = 0;
        while (j < size && (sensitive ? (uint8_t)text[i + j] == (uint8_t)part[j]
                                      : fold((uint8_t)text[i + j]) == fold((uint8_t)part[j])))
            ++j;
        if (j == size)
            return (int32_t)i;
    }
    return -1;
}
static bool delimiter(unsigned c) { return c == ' ' || c == '.' || c == ',' || c == '!'; }
int32_t chat_word(const char *text, const char *word, size_t start) {
    size_t length = strlen(text), size = strlen(word);
    if (start > length || size > length - start)
        return -1;
    size_t end = length - start - size, pointer = start;
    for (size_t index = 0; index <= end && pointer <= length; ++index, ++pointer) {
        if (index != 0) {
            while (pointer < length && !delimiter((uint8_t)text[pointer]))
                ++pointer;
            if (pointer == length)
                break;
            ++pointer;
        }
        if (size > length - pointer)
            continue;
        size_t j = 0;
        while (j < size && fold((uint8_t)text[pointer + j]) == fold((uint8_t)word[j]))
            ++j;
        if (j == size && (pointer + size == length || delimiter((uint8_t)text[pointer + size])))
            return pointer <= INT32_MAX ? (int32_t)pointer : -1;
    }
    return -1;
}
static bool white(unsigned c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             (c != 0 && strchr("()?:'/,.[]-_+=", (int)c) != NULL));
}
void qa_bot_chat_unify_whitespace(char *text) {
    if (text == NULL)
        return;
    size_t pointer = 0, old = 0;
    while (text[pointer] != 0) {
        while (text[pointer] != 0 && white((uint8_t)text[pointer]))
            ++pointer;
        if (pointer > old) {
            if (old != 0 && text[pointer] != 0)
                text[old++] = ' ';
            if (pointer > old)
                memmove(text + old, text + pointer, strlen(text + pointer) + 1);
        }
        while (text[pointer] != 0 && !white((uint8_t)text[pointer]))
            ++pointer;
        old = pointer;
    }
}
void chat_strip_tildes(char *text) {
    for (size_t i = 0; text[i] != 0; ++i)
        if (text[i] == '~')
            memmove(text + i, text + i + 1, strlen(text + i + 1) + 1);
}
void chat_match_clear(qa_bot_chat_match *match, const char *text) {
    memset(match, 0, sizeof(*match));
    chat_copy(match->text, sizeof(match->text), text);
    for (size_t i = 0; i < 8; ++i)
        match->variables[i].offset = -1;
}
bool chat_match_pieces(const qa_bot_chat_asset *a, qa_bot_chat_range range,
                       qa_bot_chat_match *match) {
    int32_t last = -1;
    size_t pointer = 0, length = strlen(match->text);
    for (uint32_t i = 0; i < range.count; ++i) {
        const qa_bot_chat_piece *piece = a->pieces + range.first + i;
        if (piece->kind == QA_BOT_CHAT_VARIABLE) {
            match->variables[piece->data.variable].offset = (int16_t)pointer;
            last = (int32_t)piece->data.variable;
            continue;
        }
        bool found = false;
        for (uint32_t j = 0; j < piece->data.alternatives.count; ++j) {
            const char *alternative = a->alternatives[piece->data.alternatives.first + j];
            size_t size = strlen(alternative);
            if (size == 0) {
                found = true;
                break;
            }
            int32_t offset = qa_bot_chat_contains(match->text + pointer, alternative, false);
            if (offset < 0)
                continue;
            if (last >= 0) {
                qa_bot_chat_capture *capture = match->variables + last;
                capture->length =
                    (uint16_t)(pointer + (uint32_t)offset - (uint16_t)capture->offset);
                last = -1;
                pointer += (uint32_t)offset + size;
                found = true;
                break;
            }
            if (offset == 0) {
                pointer += size;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    if (last < 0)
        return pointer == length;
    match->variables[last].length = (uint16_t)(length - (uint16_t)match->variables[last].offset);
    return true;
}
bool qa_bot_chat_find_match(const qa_bot_chat_system *system, const char *text, uint32_t context,
                            qa_bot_chat_match *match, bool *found, qa_error *e) {
    if (system == NULL || text == NULL || match == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat match request");
        return false;
    }
    chat_match_clear(match, text);
    size_t length = strlen(match->text);
    while (length != 0 && match->text[length - 1] == '\n')
        match->text[--length] = 0;
    *found = false;
    const qa_bot_chat_asset *a = system->options.matches;
    if (a == NULL)
        return true;
    for (size_t i = 0; i < a->view.template_count; ++i) {
        const qa_bot_chat_template *t = a->templates + i;
        if ((t->context & context) == 0)
            continue;
        for (size_t j = 0; j < 8; ++j)
            match->variables[j].offset = -1;
        if (chat_match_pieces(a, t->pieces, match)) {
            match->type = t->type;
            match->subtype = t->subtype;
            *found = true;
            break;
        }
    }
    return true;
}
bool qa_bot_chat_match_variable(const qa_bot_chat_match *match, uint32_t index, char *out,
                                size_t size, qa_error *e) {
    if (match == NULL || index >= 8 || out == NULL || size == 0 ||
        memchr(match->text, 0, sizeof(match->text)) == NULL)
        goto invalid;
    qa_bot_chat_capture capture = match->variables[index];
    if (capture.offset < 0) {
        out[0] = 0;
        return true;
    }
    size_t length = strlen(match->text), start = (uint16_t)capture.offset;
    if (start >= length) {
        out[0] = 0;
        return true;
    }
    size_t count = capture.length;
    if (count > length - start)
        count = length - start;
    if (count >= size)
        count = size - 1;
    memcpy(out, match->text + start, count);
    out[count] = 0;
    return true;
invalid:
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat match capture/output");
    return false;
}
static bool replace_at(char *text, size_t capacity, size_t offset, size_t remove,
                       const char *replacement, qa_error *e) {
    size_t length = strlen(text), size = strlen(replacement);
    if (size >= capacity - (length - remove)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, offset,
                     "Bot synonym replacement exceeds output capacity");
        return false;
    }
    memmove(text + offset + size, text + offset + remove, length - offset - remove + 1);
    memcpy(text + offset, replacement, size);
    return true;
}
static bool replace_words(char *text, size_t capacity, const char *word, const char *replacement,
                          qa_error *e) {
    size_t size = strlen(replacement);
    int32_t found = chat_word(text, word, 0);
    while (found >= 0) {
        int32_t prior = chat_word(text, replacement, 0);
        while (prior >= 0 && !(prior <= found && (uint32_t)found < (uint32_t)prior + size))
            prior = chat_word(text, replacement, (size_t)prior + 1);
        if (prior < 0 && !replace_at(text, capacity, (uint32_t)found, strlen(word), replacement, e))
            return false;
        found = chat_word(text, word, (size_t)found + size);
    }
    return true;
}
bool qa_bot_chat_replace_synonyms(qa_bot_chat_system *system, char *text, size_t capacity,
                                  uint32_t context, bool weighted, bool reply, qa_error *e) {
    if (system == NULL || text == NULL || capacity == 0 || memchr(text, 0, capacity) == NULL ||
        (weighted && reply)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot synonym replacement request");
        return false;
    }
    const qa_bot_chat_asset *a = system->options.synonyms;
    if (a == NULL)
        return true;
    if (reply) {
        size_t pointer = 0;
        while (text[pointer] != 0) {
            while (text[pointer] != 0 &&
                   ((uint8_t)text[pointer] <= 32 || (uint8_t)text[pointer] >= 128))
                ++pointer;
            if (text[pointer] == 0)
                break;
            bool replaced = false;
            for (size_t i = 0; i < a->view.group_count && !replaced; ++i) {
                const qa_bot_chat_synonyms *group = a->groups + i;
                if ((group->context & context) == 0)
                    continue;
                const char *replacement = a->synonyms[group->entries.first].text;
                for (uint32_t j = 1; j < group->entries.count; ++j) {
                    const char *word = a->synonyms[group->entries.first + j].text;
                    if (chat_word(text, word, pointer) != (int64_t)pointer ||
                        chat_word(text, replacement, pointer) == (int64_t)pointer)
                        continue;
                    if (!replace_at(text, capacity, pointer, strlen(word), replacement, e))
                        return false;
                    replaced = true;
                    break;
                }
            }
            while (text[pointer] != 0 && (uint8_t)text[pointer] > 32 &&
                   (uint8_t)text[pointer] < 128)
                ++pointer;
        }
        return true;
    }
    for (size_t i = 0; i < a->view.group_count; ++i) {
        const qa_bot_chat_synonyms *group = a->groups + i;
        if ((group->context & context) == 0)
            continue;
        uint32_t chosen = 0;
        if (weighted) {
            float value = bot_random(&system->services.random) * group->total_weight;
            if (value == 0)
                continue;
            float total = 0;
            for (chosen = 0; chosen < group->entries.count; ++chosen) {
                total += a->synonyms[group->entries.first + chosen].weight;
                if (value < total)
                    break;
            }
            if (chosen == group->entries.count)
                continue;
        }
        const char *replacement = a->synonyms[group->entries.first + chosen].text;
        for (uint32_t j = weighted ? 0 : 1; j < group->entries.count; ++j)
            if (j != chosen &&
                !replace_words(text, capacity, a->synonyms[group->entries.first + j].text,
                               replacement, e))
                return false;
    }
    return true;
}
