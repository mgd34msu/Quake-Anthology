#include "internal.h"
#include "qa/text.h"

bool chat_append(void **data, size_t *capacity, size_t *count, size_t stride, const void *value,
                 qa_error *e) {
    if (*count >= UINT32_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, *count, "Bot chat resource index overflow");
        return false;
    }
    if (!bot_grow(data, capacity, *count + 1, stride, e))
        return false;
    memcpy((uint8_t *)*data + (*count)++ * stride, value, stride);
    return true;
}
bool chat_string(qa_bot_chat_asset *a, qa_script *s, const char **out, qa_error *e) {
    qa_script_token t;
    if (!bot_token(s, &t, e))
        return false;
    if (t.kind != QA_SCRIPT_STRING)
        return bot_fail(s, "Expected bot chat string", e);
    qa_bytes text = qa_script_token_value(&t);
    const void *end = memchr(text.data, 0, text.size);
    if (end != NULL)
        text.size = (const uint8_t *)end - text.data;
    *out = bot_string(&a->arena, text, e);
    return *out != NULL;
}
bool chat_message_parse(qa_bot_chat_asset *a, qa_script *s, const char **out, qa_error *e) {
    char *text=NULL;
    size_t length=0,capacity=0;bool ok=false;
    for (;;) {
        qa_script_token t;
        if (!bot_token(s, &t, e))
            goto finish;
        qa_bytes part;
        char integer[32];
        char marker = 0;
        if (t.kind == QA_SCRIPT_STRING) {
            part = qa_script_token_value(&t);
            const void *end = memchr(part.data, 0, part.size);
            if (end != NULL)
                part.size = (const uint8_t *)end - part.data;
        } else if (t.kind == QA_SCRIPT_NUMBER && (t.subtype & QA_SCRIPT_INTEGER) != 0) {
            if (!qa_format_ecmascript_number(t.integer, integer, e))
                goto finish;
            part = (qa_bytes){(const uint8_t *)integer, strlen(integer)};
            marker = 'v';
        } else if (t.kind == QA_SCRIPT_NAME) {
            part = t.text;
            marker = 'r';
        } else {
            (void)bot_fail(s, "Unknown bot chat message component", e);goto finish;
        }
        size_t extra = marker != 0 ? 3 : 0;
        if(marker && length>249) {
            (void)bot_fail(s,"chat message too long\n",e);goto finish;
        }
        if(part.size>SIZE_MAX-length || extra>SIZE_MAX-length-part.size) {
            qa_error_set(e,QA_ERROR_MEMORY,0,"Chat expression exceeds native address range");goto finish;
        }
        if(length+part.size+extra>=256) {
            if(!marker) (void)bot_fail(s,"chat message too long\n",e);
            else qa_error_set(e,QA_ERROR_ARGUMENT,0,"Encoded chat message exceeds the source 256-byte buffer");
            goto finish;
        }
        if(!bot_grow((void **)&text,&capacity,length+part.size+extra,1,e)) goto finish;
        if (marker != 0) {
            text[length++] = 1;
            text[length++] = marker;
        }
        if (part.size != 0)
            memcpy(text + length, part.data, part.size);
        length += part.size;
        if (marker != 0)
            text[length++] = 1;
        bool end;
        if (!qa_script_check(s, ";", &end, e))
            goto finish;
        if (end)
            break;
        if (!qa_script_expect(s, ",", e))
            goto finish;
    }
    *out = bot_string(&a->arena, (qa_bytes){(const uint8_t *)text, length}, e);
    ok=*out!=NULL;
finish:
    free(text);return ok;
}
bool chat_messages_parse(qa_bot_chat_asset *a, qa_script *s, qa_bot_chat_range *range,
                         qa_error *e) {
    range->first = (uint32_t)a->view.message_count;
    for (;;) {
        bool end;
        if (!qa_script_check(s, "}", &end, e))
            return false;
        if (end)
            break;
        const char *text;
        if (!chat_message_parse(a, s, &text, e) ||
            !chat_append((void **)&a->messages, &a->message_capacity, &a->view.message_count,
                         sizeof(text), &text, e))
            return false;
    }
    range->count = (uint32_t)a->view.message_count - range->first;
    for (uint32_t i = 0; i < range->count / 2; ++i) {
        const char *text = a->messages[range->first + i];
        a->messages[range->first + i] = a->messages[range->first + range->count - 1 - i];
        a->messages[range->first + range->count - 1 - i] = text;
    }
    return true;
}
bool chat_pieces_parse(qa_bot_chat_asset *a, qa_script *s, const char *end,
                       qa_bot_chat_range *range, qa_error *e) {
    range->first = (uint32_t)a->view.piece_count;
    bool previous_variable = false;
    for (;;) {
        qa_script_token t;
        if (!bot_token(s, &t, e))
            return false;
        qa_bot_chat_piece piece = {0};
        if (t.kind == QA_SCRIPT_NUMBER && (t.subtype & QA_SCRIPT_INTEGER) != 0) {
            if (t.integer < 0 || t.integer >= 8 || previous_variable)
                return bot_fail(s, "Invalid or adjacent bot chat match variables", e);
            piece.kind = QA_BOT_CHAT_VARIABLE;
            piece.data.variable = (uint32_t)t.integer;
            previous_variable = true;
        } else if (t.kind == QA_SCRIPT_STRING) {
            piece.kind = QA_BOT_CHAT_ALTERNATIVES;
            piece.data.alternatives.first = (uint32_t)a->view.alternative_count;
            if (!qa_script_unread(s, &t, e))
                return false;
            bool empty = false;
            for (;;) {
                const char *text;
                if (!chat_string(a, s, &text, e))
                    return false;
                empty |= text[0] == 0;
                if (!chat_append((void **)&a->alternatives, &a->alternative_capacity,
                                 &a->view.alternative_count, sizeof(text), &text, e))
                    return false;
                bool more;
                if (!qa_script_check(s, "|", &more, e))
                    return false;
                if (!more)
                    break;
            }
            piece.data.alternatives.count =
                (uint32_t)a->view.alternative_count - piece.data.alternatives.first;
            if (!empty)
                previous_variable = false;
        } else
            return bot_fail(s, "Invalid bot chat match piece", e);
        if (!chat_append((void **)&a->pieces, &a->piece_capacity, &a->view.piece_count,
                         sizeof(piece), &piece, e))
            return false;
        bool done;
        if (!qa_script_check(s, end, &done, e))
            return false;
        if (done)
            break;
        if (!qa_script_expect(s, ",", e))
            return false;
    }
    range->count = (uint32_t)a->view.piece_count - range->first;
    return true;
}
bool chat_parse_randoms(qa_bot_chat_asset *a, qa_script *s, qa_error *e) {
    for (;;) {
        qa_script_token t;
        bool found;
        if (!qa_script_next(s, &t, &found, e))
            return false;
        if (!found)
            return true;
        if (t.kind != QA_SCRIPT_NAME)
            return bot_fail(s, "Expected random bot chat name", e);
        qa_bot_chat_list list = {0};
        list.name = bot_string(&a->arena, t.text, e);
        if (list.name == NULL || !qa_script_expect(s, "=", e) || !qa_script_expect(s, "{", e) ||
            !chat_messages_parse(a, s, &list.messages, e))
            return false;
        if (!chat_append((void **)&a->lists, &a->list_capacity, &a->view.list_count, sizeof(list),
                         &list, e))
            return false;
    }
}
bool chat_parse_matches(qa_bot_chat_asset *a, qa_script *s, qa_error *e) {
    for (;;) {
        qa_script_token t;
        bool found;
        if (!qa_script_next(s, &t, &found, e))
            return false;
        if (!found)
            return true;
        if (t.kind != QA_SCRIPT_NUMBER || (t.subtype & QA_SCRIPT_INTEGER) == 0)
            return bot_fail(s, "Expected bot chat match context", e);
        uint32_t context = (uint32_t)t.integer;
        if (!qa_script_expect(s, "{", e))
            return false;
        for (;;) {
            bool end;
            if (!qa_script_check(s, "}", &end, e))
                return false;
            if (end)
                break;
            qa_bot_chat_template match = {.context = context};
            if (!chat_pieces_parse(a, s, "=", &match.pieces, e) || !qa_script_expect(s, "(", e) ||
                !bot_integer(s, &match.type, e) || !qa_script_expect(s, ",", e) ||
                !bot_integer(s, &match.subtype, e) || !qa_script_expect(s, ")", e) ||
                !qa_script_expect(s, ";", e))
                return false;
            if (!chat_append((void **)&a->templates, &a->template_capacity, &a->view.template_count,
                             sizeof(match), &match, e))
                return false;
        }
    }
}
bool chat_parse_synonyms(qa_bot_chat_asset *a, qa_script *s, qa_error *e) {
    uint32_t contexts[31], context = 0;
    size_t depth = 0;
    for (;;) {
        qa_script_token t;
        bool found;
        if (!qa_script_next(s, &t, &found, e))
            return false;
        if (!found)
            break;
        if (t.kind == QA_SCRIPT_NUMBER) {
            if (depth == 31)
                return bot_fail(s, "Too many bot synonym context levels", e);
            contexts[depth++] = (uint32_t)t.integer;
            context |= (uint32_t)t.integer;
            if (!qa_script_expect(s, "{", e))
                return false;
        } else if (qa_script_token_is(&t, "}")) {
            if (depth == 0)
                return bot_fail(s, "Unexpected bot synonym context close", e);
            context &= ~contexts[--depth];
        } else if (qa_script_token_is(&t, "[")) {
            qa_bot_chat_synonyms group = {.context = context,
                                          .entries.first = (uint32_t)a->view.synonym_count};
            for (;;) {
                qa_bot_chat_synonym entry;
                if (!qa_script_expect(s, "(", e) || !chat_string(a, s, &entry.text, e) ||
                    !qa_script_expect(s, ",", e) || !bot_number(s, &entry.weight, true, e) ||
                    !qa_script_expect(s, ")", e))
                    return false;
                if (entry.text[0] == 0)
                    return bot_fail(s, "Empty bot synonym", e);
                group.total_weight += entry.weight;
                if (!chat_append((void **)&a->synonyms, &a->synonym_capacity,
                                 &a->view.synonym_count, sizeof(entry), &entry, e))
                    return false;
                bool end;
                if (!qa_script_check(s, "]", &end, e))
                    return false;
                if (end)
                    break;
                if (!qa_script_expect(s, ",", e))
                    return false;
            }
            group.entries.count = (uint32_t)a->view.synonym_count - group.entries.first;
            if (group.entries.count < 2)
                return bot_fail(s, "A bot synonym group needs two entries", e);
            if (!chat_append((void **)&a->groups, &a->group_capacity, &a->view.group_count,
                             sizeof(group), &group, e))
                return false;
        } else if (t.kind == QA_SCRIPT_PUNCTUATION)
            return bot_fail(s, "Unexpected bot synonym punctuation", e);
    }
    return depth == 0 || bot_fail(s, "Unclosed bot synonym context", e);
}
