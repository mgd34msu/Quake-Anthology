#include "internal.h"

static bool append(qa_script_lexer *l, uint8_t **bytes, size_t *count, size_t *capacity,
                   uint8_t value, qa_error *e) {
    if (*count + 1 >= l->options.token_limit)
        return script_error(l, "String exceeds script token limit", e);
    if (!script_grow((void **)bytes, capacity, *count + 1, 1, e))
        return false;
    (*bytes)[(*count)++] = value;
    return true;
}
static bool escape(qa_script_lexer *l, uint8_t *out, qa_error *e) {
    script_advance(l, 1);
    uint8_t c = script_peek(l, 0);
    switch (c) {
    case '\\':
    case '\'':
    case '"':
    case '?':
        *out = c;
        script_advance(l, 1);
        return true;
    case 'n':
        *out = '\n';
        break;
    case 'r':
        *out = '\r';
        break;
    case 't':
        *out = '\t';
        break;
    case 'v':
        *out = '\v';
        break;
    case 'b':
        *out = '\b';
        break;
    case 'f':
        *out = '\f';
        break;
    case 'a':
        *out = 7;
        break;
    default: {
        unsigned radix = c == 'x' ? 16 : 10;
        uint32_t value = 0;
        if (c == 'x')
            script_advance(l, 1);
        else if (!script_digit(c))
            return script_error(l, "Unknown string escape", e);
        for (;;) {
            c = script_peek(l, 0);
            bool hex = (l->options.flags & QA_SCRIPT_STRICT_NUMBERS) != 0
                           ? script_hex(c)
                           : script_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (radix == 16 ? !hex : !script_digit(c))
                break;
            uint32_t digit = script_digit(c) ? c - '0' : c >= 'a' ? c - 'a' + 10 : c - 'A' + 10;
            if (value > ((uint32_t)INT32_MAX - digit) / radix)
                return script_error(l, "String escape exceeds source integer range", e);
            value = value * radix + digit;
            script_advance(l, 1);
        }
        if (value > 255) {
            script_warning(l, "Too large value in escape character");
            value = 255;
        }
        *out = (uint8_t)value;
        return true;
    }
    }
    script_advance(l, 1);
    return true;
}
bool script_quoted(qa_script_lexer *l, qa_script_token *out, qa_error *e) {
    uint8_t quote = script_peek(l, 0), *bytes = l->scratch;
    size_t count = 0, capacity = l->scratch_capacity;
    size_t start = l->state.offset;
    bool transformed = false;
    bool ok = append(l, &bytes, &count, &capacity, quote, e);
    script_advance(l, 1);
    while (ok) {
        uint8_t c = script_peek(l, 0);
        if (c == 0 || c == '\n') {
            ok = script_error(l, c == 0 ? "Missing trailing quote" : "Newline inside string", e);
            break;
        }
        if (c == quote) {
            script_advance(l, 1);
            if ((l->options.flags & QA_SCRIPT_NO_STRING_CONCAT) != 0)
                break;
            qa_script_lexer_state saved = l->state;
            script_whitespace(l);
            if (script_peek(l, 0) != quote) {
                l->state = saved;
                break;
            }
            transformed = true;
            script_advance(l, 1);
            continue;
        }
        if (c == '\\' && (l->options.flags & QA_SCRIPT_NO_STRING_ESCAPES) == 0) {
            transformed = true;
            ok = escape(l, &c, e);
        } else
            script_advance(l, 1);
        if (ok)
            ok = append(l, &bytes, &count, &capacity, c, e);
    }
    if (ok)
        ok = append(l, &bytes, &count, &capacity, quote, e);
    if (ok) {
        const uint8_t *text = transformed
                                  ? (const uint8_t *)script_string(&l->arena, bytes, count, e)
                                  : l->input.data + start;
        if (text == NULL)
            ok = false;
        else {
            out->kind = quote == '"' ? QA_SCRIPT_STRING : QA_SCRIPT_LITERAL;
            out->text = (qa_bytes){(const uint8_t *)text, count};
            out->subtype = (uint32_t)count;
        }
    }
    l->scratch = bytes;
    l->scratch_capacity = capacity;
    return ok;
}
