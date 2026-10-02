#include "internal.h"

static bool writable(qa_script_lexer *l, qa_script_token *out, uint8_t **bytes, qa_error *e) {
    if (*bytes)
        return true;
    uint8_t *storage = qa_arena_alloc(&l->arena, l->options.token_limit, 1, e);
    if (!storage)
        return false;
    memcpy(storage, out->text.data, out->text.size);
    storage[out->text.size] = 0;
    out->text.data = storage;
    *bytes = storage;
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
        else if (!script_digit(c)) {
            script_report_error(l, "Unknown string escape");
            *out = 0;
            return true;
        }
        size_t start = script_lexer_offset(l);
        for (;;) {
            c = script_peek(l, 0);
            bool hex = (script_lexer_flags(l) & QA_SCRIPT_STRICT_NUMBERS) != 0
                           ? script_hex(c)
                           : script_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (radix == 16 ? !hex : !script_digit(c))
                break;
            script_advance(l, 1);
        }
        for (size_t i = start; i < script_lexer_offset(l); ++i) {
            c = l->input.data[i];
            uint32_t digit = script_digit(c) ? c - '0' : c >= 'a' ? c - 'a' + 10 : c - 'A' + 10;
            if (value > ((uint32_t)INT32_MAX - digit) / radix)
                return script_unsupported(l, "String escape exceeds source integer range", e);
            value = value * radix + digit;
        }
        if (value > 255) {
            script_lexer_offset_set(l,script_lexer_offset(l)-1);
            --l->column;
            script_warning(l, "Too large value in escape character");
            script_lexer_offset_set(l,script_lexer_offset(l)+1);
            ++l->column;
            value = 255;
        }
        *out = (uint8_t)value;
        return true;
    }
    }
    script_advance(l, 1);
    return true;
}
bool script_quoted(qa_script_lexer *l, qa_script_token *out, uint8_t *raw, qa_error *e) {
    uint8_t quote = script_peek(l, 0), *bytes = NULL;
    size_t count = 1;
    size_t start = script_lexer_offset(l);
    out->kind = quote == '"' ? QA_SCRIPT_STRING : QA_SCRIPT_LITERAL;
    qa_store_u32le(raw+1024,(uint32_t)out->kind);raw[0]=quote;
    out->text = (qa_bytes){l->input.data + start, count};
    script_advance(l, 1);
    for (;;) {
        if (count >= l->options.token_limit - 2)
            return script_error(l, "String exceeds script token limit", e);
        uint8_t c = script_peek(l, 0);
        if (c == 0 || c == '\n') {
            if(!script_token_byte(l,raw,count,0,e)) return false;
            return script_error(l, c == 0 ? "Missing trailing quote" : "Newline inside string", e);
        }
        if (c == quote) {
            script_advance(l, 1);
            if ((script_lexer_flags(l) & QA_SCRIPT_NO_STRING_CONCAT) != 0)
                break;
            script_lexer_cursor saved=script_lexer_cursor_get(l);
            script_whitespace(l);
            if (script_peek(l, 0) != quote) {
                script_lexer_cursor_set(l,saved);
                break;
            }
            if (!writable(l, out, &bytes, e))
                return false;
            script_advance(l, 1);
            continue;
        }
        if (c == '\\' && (script_lexer_flags(l) & QA_SCRIPT_NO_STRING_ESCAPES) == 0) {
            if (!writable(l, out, &bytes, e) || !escape(l, &c, e) ||
                !script_token_byte(l,raw,count,c,e)) return false;
        } else {
            if(!script_token_byte(l,raw,count,c,e)) return false;
            script_advance(l, 1);
        }
        if (bytes) {
            bytes[count] = c;
            bytes[count + 1] = 0;
        }
        out->text.size = ++count;
    }
    if (bytes) {
        bytes[count] = quote;
        bytes[count + 1] = 0;
    }
    if(!script_token_byte(l,raw,count,quote,e) || !script_token_byte(l,raw,count+1,0,e)) return false;
    out->text.size = ++count;
    out->subtype = (uint32_t)count;qa_store_u32le(raw+1028,out->subtype);
    return true;
}
