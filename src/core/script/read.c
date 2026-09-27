#include "internal.h"

static bool processed(qa_script *s, qa_script_token *out, bool *found, qa_error *e) {
    for (;;) {
        script_queued_token item;
        if (!script_raw(s, &item, found, e))
            return false;
        if (!*found)
            return true;
        qa_script_token *t = &item.token;
        if (item.processed) {
            *out = *t;
            return true;
        }
        if (t->kind == QA_SCRIPT_PUNCTUATION &&
            (t->subtype == QA_SCRIPT_HASH || t->subtype == QA_SCRIPT_DOLLAR)) {
            if (!script_directive(s, item, e))
                return false;
            continue;
        }
        if (s->skipping != 0)
            continue;
        script_macro *macro =
            t->kind == QA_SCRIPT_NAME ? script_macro_find(&s->macros, t->text) : NULL;
        if (macro != NULL) {
            if (!script_expand(s, item, macro, e))
                return false;
            if (s->empty_expansion) {
                *found = false;
                return true;
            }
            continue;
        }
        *out = *t;
        return true;
    }
}
bool qa_script_next(qa_script *s, qa_script_token *out, bool *found, qa_error *e) {
    if (s == NULL || out == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script read output");
        return false;
    }
    qa_script_token token;
    if (!processed(s, &token, found, e))
        return false;
    if (!*found)
        return true;
    if (token.kind == QA_SCRIPT_STRING) {
        for (;;) {
            qa_script_token next;
            bool more;
            if (!processed(s, &next, &more, e))
                return false;
            if (!more)
                break;
            if (next.kind != QA_SCRIPT_STRING) {
                if (!script_push(s, (script_queued_token){next, NULL, true}, e))
                    return false;
                break;
            }
            if (token.text.size < 2 || next.text.size < 2 ||
                next.text.size > SIZE_MAX - token.text.size)
                return script_fail(s, token.location, "Invalid adjacent string", e);
            size_t size = token.text.size + next.text.size - 2;
            if (size + 1 >= s->options.token_limit)
                return script_fail(s, token.location, "Adjacent strings exceed token limit", e);
            char *text = qa_arena_alloc(&s->arena, size + 1, 1, e);
            if (text == NULL)
                return false;
            memcpy(text, token.text.data, token.text.size - 1);
            memcpy(text + token.text.size - 1, next.text.data + 1, next.text.size - 1);
            text[size] = 0;
            token.text = (qa_bytes){(const uint8_t *)text, size};
            token.subtype = (uint32_t)size;
        }
    }
    if (s->outputs >= s->options.maximum_output_tokens)
        return script_fail(s, token.location, "Script output exceeds token limit", e);
    ++s->outputs;
    *out = token;
    *found = true;
    return true;
}
bool qa_script_unread(qa_script *s, const qa_script_token *token, qa_error *e) {
    if (s == NULL || token == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing unread script token/source");
        return false;
    }
    return script_push(s, (script_queued_token){*token, NULL, true}, e);
}
bool qa_script_expect(qa_script *s, const char *text, qa_error *e) {
    if (text == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing expected script token");
        return false;
    }
    qa_script_token token;
    bool found;
    if (!qa_script_next(s, &token, &found, e))
        return false;
    if (!found || !qa_script_token_is(&token, text)) {
        qa_error_set(e, QA_ERROR_FORMAT, qa_script_position(s).offset, "Expected script token %s",
                     text);
        return false;
    }
    return true;
}
bool qa_script_check(qa_script *s, const char *text, bool *matched, qa_error *e) {
    if (matched == NULL || text == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script check input/output");
        return false;
    }
    qa_script_token token;
    bool found;
    if (!qa_script_next(s, &token, &found, e))
        return false;
    *matched = found && qa_script_token_is(&token, text);
    return !found || *matched || qa_script_unread(s, &token, e);
}
bool qa_script_skip_until(qa_script *s, const char *text, bool *found, qa_error *e) {
    if (text == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing script skip input/output");
        return false;
    }
    qa_script_token token;
    while (qa_script_next(s, &token, found, e)) {
        if (!*found || qa_script_token_is(&token, text))
            return true;
    }
    return false;
}
bool qa_script_read_line(qa_script *s, qa_script_token *out, bool *found, qa_error *e) {
    if (s == NULL || out == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script line output");
        return false;
    }
    script_queued_token token;
    if (!script_line_token(s, &token, found, e))
        return false;
    if (*found)
        *out = token.token;
    return true;
}
