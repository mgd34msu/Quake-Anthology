#include "internal.h"

static int32_t signed_integer(uint32_t value) {
    int32_t out;
    memcpy(&out, &value, sizeof(out));
    return out;
}
bool script_number(qa_script_lexer *l, qa_script_token *out, uint8_t *raw, qa_error *e) {
    size_t start = script_lexer_offset(l);
    out->kind = QA_SCRIPT_NUMBER;qa_store_u32le(raw+1024,QA_SCRIPT_NUMBER);
    out->text = (qa_bytes){l->input.data + start, 0};
    unsigned radix = 10;
    uint32_t flags = 0;
    uint8_t first = script_peek(l, 0), next = script_peek(l, 1);
    if (first == '0' && (next == 'x' || next == 'X')) {
        radix = 16;
        flags = QA_SCRIPT_HEX;
        raw[0]=first;raw[1]=next;script_advance(l, 2);
        out->text.size = 2;
        for (;;) {
            uint8_t c = script_peek(l, 0);
            bool hex = (script_lexer_flags(l) & QA_SCRIPT_STRICT_NUMBERS) != 0
                           ? script_hex(c)
                           : script_digit(c) || (c >= 'a' && c <= 'f') || c == 'A';
            if (!hex)
                break;
            if(!script_token_byte(l,raw,script_lexer_offset(l)-start,script_peek(l,0),e)) return false;
            script_advance(l, 1);
            out->text.size = script_lexer_offset(l) - start;
            if (out->text.size >= l->options.token_limit)
                return script_error(l, "Number exceeds script token limit", e);
        }
    } else if (first == '0' && (next == 'b' || next == 'B') &&
               (script_lexer_flags(l) & QA_SCRIPT_NO_BINARY) == 0) {
        radix = 2;
        flags = QA_SCRIPT_BINARY;
        raw[0]=first;raw[1]=next;script_advance(l, 2);
        out->text.size = 2;
        while (script_peek(l, 0) == '0' || script_peek(l, 0) == '1') {
            if(!script_token_byte(l,raw,script_lexer_offset(l)-start,script_peek(l,0),e)) return false;
            script_advance(l, 1);
            out->text.size = script_lexer_offset(l) - start;
            if (out->text.size >= l->options.token_limit)
                return script_error(l, "Number exceeds script token limit", e);
        }
    } else {
        bool octal = first == '0';
        unsigned dots = 0;
        for (;;) {
            uint8_t c = script_peek(l, 0);
            if (c == '.')
                ++dots;
            else if (!script_digit(c))
                break;
            if (c == '8' || c == '9')
                octal = false;
            if(!script_token_byte(l,raw,script_lexer_offset(l)-start,script_peek(l,0),e)) return false;
            script_advance(l, 1);
            out->text.size = script_lexer_offset(l) - start;
            if (out->text.size >= l->options.token_limit - 1)
                return script_error(l, "Number exceeds script token limit", e);
        }
        flags = octal ? QA_SCRIPT_OCTAL : QA_SCRIPT_DECIMAL;
        radix = octal ? 8 : 10;
        if (dots != 0)
            flags |= QA_SCRIPT_FLOAT;
        out->subtype = flags;qa_store_u32le(raw+1028,flags);
        if (dots > 1 && (script_lexer_flags(l) & QA_SCRIPT_STRICT_NUMBERS) != 0)
            return script_error(l, "Numeric token contains multiple decimal points", e);
    }
    size_t end = script_lexer_offset(l);
    out->subtype = flags;qa_store_u32le(raw+1028,flags);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t c = script_peek(l, 0);
        if ((c == 'l' || c == 'L') && (flags & QA_SCRIPT_LONG) == 0) {
            flags |= QA_SCRIPT_LONG;
            script_advance(l, 1);
            out->subtype = flags;qa_store_u32le(raw+1028,flags);
        } else if ((c == 'u' || c == 'U') &&
                   (flags & (QA_SCRIPT_UNSIGNED | QA_SCRIPT_FLOAT)) == 0) {
            flags |= QA_SCRIPT_UNSIGNED;
            script_advance(l, 1);
            out->subtype = flags;qa_store_u32le(raw+1028,flags);
        }
    }
    out->text = (qa_bytes){l->input.data + start, end - start};
    out->subtype = flags;qa_store_u32le(raw+1028,flags);
    if(!script_token_byte(l,raw,out->text.size,0,e)) return false;
    if ((flags & QA_SCRIPT_FLOAT) != 0) {
        double value = 0;
        if ((script_lexer_flags(l) & QA_SCRIPT_STRICT_NUMBERS) != 0) {
            if (!qa_parse_number(out->text, &value, e))
                return script_error(l, "Invalid floating script number", e);
        } else {
            uint32_t divisor = 0;
            for (size_t i = 0; i < out->text.size; ++i) {
                if (out->text.data[i] == '.') {
                    if (divisor != 0) {
                        out->number = value;
                        out->integer = 0;
                        script_token_float(raw+1036,out->number);return true;
                    }
                    divisor = 10;
                    ++i;
                }
                int digit = (i < out->text.size ? out->text.data[i] : 0) - '0';
                if (divisor != 0) {
                    value += (double)digit / divisor;
                    divisor *= 10;
                } else
                    value = value * 10 + digit;
            }
        }
        double integral = trunc(value);
        if (!isfinite(integral) || integral < 0 || integral > 4294967295.0)
            return script_unsupported(
                l, "Floating script number exceeds source unsigned integer range", e);
        out->number = value;
        out->integer = signed_integer((uint32_t)value);
    } else {
        uint32_t value = 0;
        size_t offset = radix == 16 || radix == 2 ? 2 : radix == 8 ? 1 : 0;
        for (; offset < out->text.size; ++offset) {
            uint8_t c = out->text.data[offset];
            uint32_t digit = script_digit(c) ? c - '0' : c >= 'a' ? c - 'a' + 10 : c - 'A' + 10;
            value = value * radix + digit;
        }
        out->subtype |= QA_SCRIPT_INTEGER;
        out->integer = signed_integer(value);
        out->number = value;
    }
    qa_store_u32le(raw+1032,(uint32_t)out->integer);script_token_float(raw+1036,out->number);
    qa_store_u32le(raw+1028,out->subtype);return true;
}
