/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <errno.h>

static bool whitespace(uint8_t c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static bool punctuation(uint8_t c) { return c == '{' || c == '}' || c == '(' || c == ')'; }
qa_bytes model_token_next(model_reader *r) {
    while (r->ok && r->pos < r->bytes.size) {
        uint8_t c = r->bytes.data[r->pos];
        if (whitespace(c)) {
            ++r->pos;
            continue;
        }
        if (r->bytes.size - r->pos >= 2 && c == '/' && r->bytes.data[r->pos + 1] == '/') {
            while (r->pos < r->bytes.size && r->bytes.data[r->pos] != '\n')
                ++r->pos;
            continue;
        }
        if (r->bytes.size - r->pos >= 2 && c == '/' && r->bytes.data[r->pos + 1] == '*') {
            r->pos += 2;
            while (r->bytes.size - r->pos >= 2 &&
                   !(r->bytes.data[r->pos] == '*' && r->bytes.data[r->pos + 1] == '/'))
                ++r->pos;
            if (r->bytes.size - r->pos < 2) {
                model_fail(r, "unterminated MD5 comment");
                return (qa_bytes){0};
            }
            r->pos += 2;
            continue;
        }
        break;
    }
    if (!r->ok || r->pos == r->bytes.size)
        return (qa_bytes){0};
    size_t start = r->pos++;
    uint8_t first = r->bytes.data[start];
    if (first == '"') {
        start = r->pos;
        while (r->pos < r->bytes.size && r->bytes.data[r->pos] != '"')
            ++r->pos;
        if (r->pos == r->bytes.size) {
            model_fail(r, "unterminated MD5 string");
            return (qa_bytes){0};
        }
        size_t length = r->pos++ - start;
        return (qa_bytes){r->bytes.data + start, length};
    }
    if (!punctuation(first))
        while (r->pos < r->bytes.size && !whitespace(r->bytes.data[r->pos]) &&
               !punctuation(r->bytes.data[r->pos]))
            ++r->pos;
    return (qa_bytes){r->bytes.data + start, r->pos - start};
}
bool model_expect(model_reader *r, const char *expected) {
    qa_bytes t = model_token_next(r);
    return (t.data && t.size == strlen(expected) && !memcmp(t.data, expected, t.size)) ||
           model_fail(r, "unexpected MD5 token");
}
double model_number(model_reader *r) {
    qa_bytes t = model_token_next(r);
    char local[128], *text = local;
    if (!t.data || !t.size || t.size == SIZE_MAX) {
        model_fail(r, "missing MD5 number");
        return 0;
    }
    if (t.size >= sizeof(local)) {
        text = model_alloc(r, t.size + 1, 1);
        if (!text)
            return 0;
    }
    memcpy(text, t.data, t.size);
    text[t.size] = '\0';
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    bool valid = end == text + t.size && isfinite(value);
    if (text != local)
        free(text);
    if (!valid) {
        model_fail(r, "invalid MD5 number");
        return 0;
    }
    return value;
}
int32_t model_integer(model_reader *r, int32_t minimum, int32_t maximum) {
    double n = model_number(r);
    if (n < minimum || n > maximum || n != trunc(n)) {
        model_fail(r, "MD5 integer outside range");
        return 0;
    }
    return (int32_t)n;
}
float model_scalar(model_reader *r) {
    float value = (float)model_number(r);
    if (!isfinite(value))
        model_fail(r, "MD5 number exceeds float range");
    return value;
}
void model_vector(model_reader *r, float out[3]) {
    model_expect(r, "(");
    for (unsigned i = 0; i < 3; ++i)
        out[i] = model_scalar(r);
    model_expect(r, ")");
}
