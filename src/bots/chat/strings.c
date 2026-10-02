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
static bool text_byte(char *text, const qa_bot_chat_text_io *io, size_t offset, uint8_t *out,
                      qa_error *e) {
    if (io != NULL)
        return io->read(io->context, offset, out, 1, e);
    *out = (uint8_t)text[offset];
    return true;
}
static bool unify_whitespace(char *text, const qa_bot_chat_text_io *io, qa_error *e) {
    size_t pointer = 0, old = 0;
    uint8_t byte;
    for (;;) {
        if (!text_byte(text, io, pointer, &byte, e))
            return false;
        if (byte == 0)
            return true;
        while (byte != 0 && white(byte)) {
            ++pointer;
            if (!text_byte(text, io, pointer, &byte, e))
                return false;
        }
        if (pointer > old) {
            if (old != 0 && byte != 0) {
                uint8_t space = ' ';
                if (io != NULL) {
                    if (!io->write(io->context, old, (qa_bytes){&space, 1}, e))
                        return false;
                } else
                    text[old] = ' ';
                ++old;
            }
            if (pointer > old) {
                if (io != NULL) {
                    size_t end = pointer;
                    do {
                        if (!text_byte(text, io, end, &byte, e))
                            return false;
                        if (byte == 0)
                            break;
                        if (end == SIZE_MAX)
                            return false;
                        ++end;
                    } while (true);
                    if (!io->copy(io->context, old, pointer, end + 1 - pointer, e))
                        return false;
                } else
                    memmove(text + old, text + pointer, strlen(text + pointer) + 1);
            }
        }
        if (!text_byte(text, io, pointer, &byte, e))
            return false;
        while (byte != 0 && !white(byte)) {
            ++pointer;
            if (!text_byte(text, io, pointer, &byte, e))
                return false;
        }
        old = pointer;
    }
}
void qa_bot_chat_unify_whitespace(char *text) {
    if (text != NULL)
        (void)unify_whitespace(text, NULL, NULL);
}
bool qa_bot_chat_unify_whitespace_into(const qa_bot_chat_text_io *io, qa_error *e) {
    if (io == NULL || io->read == NULL || io->write == NULL || io->copy == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing external bot text access");
        return false;
    }
    return unify_whitespace(NULL, io, e);
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
typedef struct match_access {
    qa_bot_chat_match *native;
    const qa_bot_chat_match_io *external;
    const qa_bot_chat_system *system;
    uint64_t revision;
} match_access;
static bool match_current(const match_access *match, qa_error *e) {
    if (match->system == NULL ||
        (!match->system->retired && match->system->revision == match->revision))
        return true;
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Bot match configuration changed during output");
    return false;
}
static bool match_text(const match_access *match, char storage[256], const char **out,
                       qa_error *e) {
    if (match->external == NULL) {
        *out = match->native->text;
        return true;
    }
    const qa_bot_chat_text_io *io = &match->external->text;
    if (!io->read(io->context, 0, storage, 256, e) || !match_current(match, e))
        return false;
    if (memchr(storage, 0, 256) == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Bot match string lacks its source terminator");
        return false;
    }
    *out = storage;
    return true;
}
static bool match_offset(match_access *match, uint32_t index, int32_t *value, bool write,
                         qa_error *e) {
    if (match->external != NULL) {
        const qa_bot_chat_match_io *io = match->external;
        bool ok = write ? io->write_offset(io->text.context, index, *value, e)
                        : io->read_offset(io->text.context, index, value, e);
        return ok && match_current(match, e);
    }
    if (write)
        match->native->variables[index].offset = (int16_t)*value;
    else
        *value = match->native->variables[index].offset;
    return true;
}
static bool match_length(match_access *match, uint32_t index, int32_t value, qa_error *e) {
    if (match->external != NULL)
        return match->external->write_length(match->external->text.context, index, value, e) &&
               match_current(match, e);
    match->native->variables[index].length = (uint16_t)value;
    return true;
}
static bool match_pieces(const qa_bot_chat_asset *a, qa_bot_chat_range range, match_access *match,
                         bool *matched, qa_error *e) {
    int32_t last = -1;
    size_t pointer = 0;
    size_t native_length = match->native != NULL ? strlen(match->native->text) : 0;
    *matched = false;
    for (uint32_t i = 0; i < range.count; ++i) {
        const qa_bot_chat_piece *piece = a->pieces + range.first + i;
        if (piece->kind == QA_BOT_CHAT_VARIABLE) {
            int32_t offset = (int32_t)pointer;
            if (!match_offset(match, piece->data.variable, &offset, true, e))
                return false;
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
            char storage[256];
            const char *text;
            if (!match_text(match, storage, &text, e))
                return false;
            size_t length = match->native != NULL ? native_length : strlen(text);
            int32_t offset = qa_bot_chat_contains(text + (pointer < length ? pointer : length),
                                                  alternative, false);
            if (offset < 0)
                continue;
            if (last >= 0) {
                int32_t beginning;
                if (!match_offset(match, (uint32_t)last, &beginning, false, e) ||
                    !match_length(match, (uint32_t)last, (int32_t)pointer + offset - beginning, e))
                    return false;
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
            return true;
    }
    int32_t beginning = 0;
    if (last >= 0 && !match_offset(match, (uint32_t)last, &beginning, false, e))
        return false;
    if (beginning < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Source signed-char match offset overflow");
        return false;
    }
    char storage[256];
    const char *text;
    if (!match_text(match, storage, &text, e))
        return false;
    size_t length = match->native != NULL ? native_length : strlen(text);
    if (last < 0) {
        *matched = pointer == length;
        return true;
    }
    if (!match_length(match, (uint32_t)last, (int32_t)length - beginning, e))
        return false;
    *matched = true;
    return true;
}
bool chat_match_pieces(const qa_bot_chat_asset *a, qa_bot_chat_range range,
                       qa_bot_chat_match *match) {
    match_access access = {.native = match};
    bool found;
    return match_pieces(a, range, &access, &found, NULL) && found;
}
static bool copy_external_text(const qa_bot_chat_text_io *io, size_t offset, const char *text,
                               size_t count, const match_access *guard, qa_error *e) {
    if (!io->admit(io->context, offset, count, e) || !match_current(guard, e))
        return false;
    size_t copied = strlen(text);
    if (copied > count)
        copied = count;
    for (size_t i = 0; i < copied; ++i) {
        uint8_t byte = (uint8_t)text[i];
        if (!io->write(io->context, offset + i, (qa_bytes){&byte, 1}, e) ||
            !match_current(guard, e))
            return false;
    }
    return io->clear(io->context, offset + copied, count - copied, e) && match_current(guard, e);
}
static bool find_match(const qa_bot_chat_system *system, const char *text, uint32_t context,
                       match_access *match, bool *found, qa_error *e) {
    if (match->external != NULL) {
        if (!copy_external_text(&match->external->text, 0, text, 256, match, e))
            return false;
    } else
        chat_match_clear(match->native, text);
    char storage[256];
    const char *current;
    if (!match_text(match, storage, &current, e))
        return false;
    size_t length = strlen(current);
    while (length != 0) {
        uint8_t byte;
        if (!text_byte(match->native != NULL ? match->native->text : NULL,
                       match->external != NULL ? &match->external->text : NULL, length - 1, &byte,
                       e))
            return false;
        if (byte != '\n')
            break;
        --length;
        byte = 0;
        if (match->external != NULL) {
            const qa_bot_chat_text_io *io = &match->external->text;
            if (!io->write(io->context, length, (qa_bytes){&byte, 1}, e) ||
                !match_current(match, e))
                return false;
        } else
            match->native->text[length] = 0;
    }
    *found = false;
    const qa_bot_chat_asset *a = system != NULL ? system->options.matches : NULL;
    if (a == NULL)
        return true;
    for (size_t i = 0; i < a->view.template_count; ++i) {
        const qa_bot_chat_template *t = a->templates + i;
        if ((t->context & context) == 0)
            continue;
        for (uint32_t j = 0; j < 8; ++j) {
            int32_t absent = -1;
            if (!match_offset(match, j, &absent, true, e))
                return false;
        }
        bool matched;
        if (!match_pieces(a, t->pieces, match, &matched, e))
            return false;
        if (matched) {
            if (match->external != NULL) {
                const qa_bot_chat_match_io *io = match->external;
                if (!io->write_type(io->text.context, false, t->type, e) ||
                    !match_current(match, e) ||
                    !io->write_type(io->text.context, true, t->subtype, e) ||
                    !match_current(match, e))
                    return false;
            } else {
                match->native->type = t->type;
                match->native->subtype = t->subtype;
            }
            *found = true;
            break;
        }
    }
    return true;
}
bool qa_bot_chat_find_match(const qa_bot_chat_system *system, const char *text, uint32_t context,
                            qa_bot_chat_match *match, bool *found, qa_error *e) {
    if (system == NULL || text == NULL || match == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat match request");
        return false;
    }
    match_access access = {.native = match};
    return find_match(system, text, context, &access, found, e);
}
bool qa_bot_chat_find_match_into(const qa_bot_chat_system *system, const char *text,
                                 uint32_t context, const qa_bot_chat_match_io *io, bool *found,
                                 qa_error *e) {
    if ((system != NULL && system->retired) || text == NULL || found == NULL || io == NULL ||
        io->text.admit == NULL || io->text.read == NULL || io->text.write == NULL ||
        io->text.clear == NULL || io->read_offset == NULL || io->write_offset == NULL ||
        io->write_length == NULL || io->write_type == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid external bot match access");
        return false;
    }
    qa_bot_chat_system *retained = (qa_bot_chat_system *)system;
    if (retained != NULL)
        ++retained->references;
    qa_bot_chat_asset *asset = system != NULL ? system->options.matches : NULL;
    qa_bot_chat_asset_retain(asset);
    match_access access = {
        .external = io, .system = system, .revision = system != NULL ? system->revision : 0};
    bool ok = find_match(system, text, context, &access, found, e);
    qa_bot_chat_asset_release(asset);
    if (retained != NULL)
        chat_system_release(retained);
    return ok;
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
static bool replacement_capacity(char **text, size_t *capacity, size_t needed, qa_error *e) {
    if (needed <= *capacity)
        return true;
    size_t size = *capacity != 0 ? *capacity : 256;
    while (size < needed) {
        if (size > SIZE_MAX / 2) {
            size = needed;
            break;
        }
        size *= 2;
    }
    char *grown = realloc(*text, size);
    if (grown == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, needed, "Growing external bot text snapshot");
        return false;
    }
    *text = grown;
    *capacity = size;
    return true;
}
static bool replace_words_value(char **storage, size_t *capacity, const char *word,
                          const char *replacement, const qa_bot_chat_text_io *io,
                          const match_access *guard,bool growing, qa_error *e) {
    char *text = *storage;
    size_t size = strlen(replacement);
    int32_t found = chat_word(text, word, 0);
    while (found >= 0) {
        int32_t prior = chat_word(text, replacement, 0);
        while (prior >= 0 && !(prior <= found && (uint32_t)found < (uint32_t)prior + size))
            prior = chat_word(text, replacement, (size_t)prior + 1);
        if (prior < 0) {
            size_t removed = strlen(word), length = strlen(text);
            if (io != NULL || growing) {
                if (size >= SIZE_MAX - (length - removed)) {
                    qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot replacement size overflow");
                    return false;
                }
                if (!replacement_capacity(storage, capacity, length - removed + size + 1, e))
                    return false;
                text = *storage;
                const char *tail = text + (uint32_t)found + removed;
                if (io && (!copy_external_text(io, (uint32_t)found + size, tail, strlen(tail) + 1, guard,
                                        e) ||
                    !copy_external_text(io, (uint32_t)found, replacement, size, guard, e)))
                    return false;
            }
            if (!replace_at(text, *capacity, (uint32_t)found, removed, replacement, e))
                return false;
        }
        found = chat_word(text, word, (size_t)found + size);
    }
    return true;
}
static bool replace_words(char **storage,size_t *capacity,const char *word,const char *replacement,
    const qa_bot_chat_text_io *io,const match_access *guard,bool growing,qa_error *error) {
    size_t word_size=strlen(word)+1,replacement_size=strlen(replacement)+1;
    char *word_value=malloc(word_size),*replacement_value=malloc(replacement_size);
    if(!word_value || !replacement_value) {
        free(word_value);free(replacement_value);
        qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining reached synonym string values");return false;
    }
    memcpy(word_value,word,word_size);memcpy(replacement_value,replacement,replacement_size);
    bool ok=replace_words_value(storage,capacity,word_value,replacement_value,io,guard,growing,error);
    free(word_value);free(replacement_value);return ok;
}
static bool replace_synonyms(qa_bot_chat_system *system, char *text, size_t capacity,
                             uint32_t context, bool weighted, bool reply,
                             const qa_bot_chat_text_io *io, const match_access *guard,
                             char **growing,qa_error *e) {
    if (system == NULL ||
        (io == NULL && (text == NULL || capacity == 0 || memchr(text, 0, capacity) == NULL)) ||
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
                qa_bot_chat_synonyms group;
                if(!bot_chat_packed_group(a,(uint32_t)i,&group,e)) return false;
                if ((group.context & context) == 0)
                    continue;
                for (uint32_t j = 1; j < group.entries.count; ++j) {
                    qa_bot_chat_synonym entry,first;
                    if(!bot_chat_packed_entry(a,group.entries.first+j,&entry,e)) return false;
                    const char *word=entry.text;
                    if(chat_word(text,word,pointer)!=(int64_t)pointer) continue;
                    if(!bot_chat_packed_entry(a,group.entries.first,&first,e)) return false;
                    const char *replacement=first.text;
                    if (chat_word(text, word, pointer) != (int64_t)pointer ||
                        chat_word(text, replacement, pointer) == (int64_t)pointer)
                        continue;
                    if(growing) {
                        size_t length=strlen(text),removed=strlen(word),size=strlen(replacement);
                        if(size>=SIZE_MAX-(length-removed)) {
                            qa_error_set(e,QA_ERROR_MEMORY,0,"Reply synonym exceeds native address range");return false;
                        }
                        bool ready=replacement_capacity(&text,&capacity,length-removed+size+1,e);
                        *growing=text;if(!ready) return false;
                    }
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
    char *snapshot = NULL;
    size_t snapshot_capacity = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < a->view.group_count; ++i) {
        qa_bot_chat_synonyms group;
        if(!bot_chat_packed_group(a,(uint32_t)i,&group,e)) {ok=false;break;}
        if ((group.context & context) == 0)
            continue;
        uint32_t chosen = 0;
        if (weighted) {
            float random=bot_random(&system->services.random);
            if(!bot_chat_packed_group(a,(uint32_t)i,&group,e)) {ok=false;break;}
            float value=(float)(random*group.total_weight);
            if (value == 0)
                continue;
            float total = 0;
            for (chosen = 0; chosen < group.entries.count; ++chosen) {
                float weight;
                if(!bot_chat_packed_weight(a,group.entries.first+chosen,&weight,e)) {ok=false;break;}
                total=(float)(total+weight);
                if (value < total)
                    break;
            }
            if(!ok) break;
            if (chosen == group.entries.count)
                continue;
        }
        for (uint32_t j = weighted ? 0 : 1; ok && j < group.entries.count; ++j) {
            if (j == chosen)
                continue;
            if (io != NULL) {
                qa_bytes source;
                ok = io->snapshot(io->context, &source, e) && match_current(guard, e);
                if (!ok)
                    break;
                if (source.size == SIZE_MAX || (source.size != 0 && source.data == NULL)) {
                    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid external bot text snapshot");
                    ok = false;
                    break;
                }
                ok = replacement_capacity(&snapshot, &snapshot_capacity, source.size + 1, e);
                if (!ok)
                    break;
                if (source.size != 0)
                    memcpy(snapshot, source.data, source.size);
                snapshot[source.size] = 0;
                qa_bot_chat_synonym entry,replacement;
                ok=bot_chat_packed_entry(a,group.entries.first+j,&entry,e) &&
                    bot_chat_packed_entry(a,group.entries.first+chosen,&replacement,e);
                if(ok) ok = replace_words(&snapshot, &snapshot_capacity,
                                   entry.text, replacement.text, io,guard,false, e);
            } else {
                qa_bot_chat_synonym entry,replacement;
                ok=bot_chat_packed_entry(a,group.entries.first+j,&entry,e) &&
                    bot_chat_packed_entry(a,group.entries.first+chosen,&replacement,e);
                if(ok) ok = replace_words(&text, &capacity, entry.text,
                                   replacement.text, NULL, NULL,growing!=NULL, e);
                if(growing) *growing=text;
            }
        }
    }
    free(snapshot);
    return ok;
}
bool qa_bot_chat_replace_synonyms(qa_bot_chat_system *system, char *text, size_t capacity,
                                  uint32_t context, bool weighted, bool reply, qa_error *e) {
    return replace_synonyms(system, text, capacity, context, weighted, reply, NULL, NULL,NULL, e);
}
bool chat_replace_source(qa_bot_chat_system *system,const char *source,uint32_t context,
    bool weighted,bool reply,char **out,qa_error *error) {
    if(!out || *out || !source || !system) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source synonym expansion requires its actual string/system/output");return false;
    }
    size_t size=strlen(source)+1;char *text=malloc(size);
    if(!text) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining source synonym result");return false;}
    memcpy(text,source,size);qa_bot_chat_asset *asset=system->options.synonyms;qa_bot_chat_asset_retain(asset);
    bool ok=replace_synonyms(system,text,size,context,weighted,reply,NULL,NULL,&text,error);
    qa_bot_chat_asset_release(asset);
    if(!ok) {free(text);return false;}
    *out=text;return true;
}
bool qa_bot_chat_replace_synonyms_into(qa_bot_chat_system *system, const qa_bot_chat_text_io *io,
                                       uint32_t context, qa_error *e) {
    if (system == NULL)
        return true;
    if (system->retired || io == NULL || io->admit == NULL || io->snapshot == NULL ||
        io->write == NULL || io->clear == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid external bot synonym access");
        return false;
    }
    ++system->references;
    qa_bot_chat_asset *asset = system->options.synonyms;
    qa_bot_chat_asset_retain(asset);
    match_access guard = {.system = system, .revision = system->revision};
    bool ok = replace_synonyms(system, NULL, 0, context, false, false, io, &guard,NULL, e);
    qa_bot_chat_asset_release(asset);
    chat_system_release(system);
    return ok;
}
