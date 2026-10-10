#include "internal.h"
#include "qa/text.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

size_t qa_command_separator(const char *text, size_t length, qa_ruleset_id dialect)
{
    bool quoted = false;
    for (size_t offset = 0; offset < length; ++offset) {
        char c = text[offset];
        if (c == '"') quoted = !quoted;
        if ((!quoted && c == ';') || c == '\n' || (dialect == QA_RULESET_Q3 && c == '\r'))
            return offset;
    }
    return length;
}

bool qac_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

bool qac_dialect_valid(qa_ruleset_id dialect)
{
    return dialect >= QA_RULESET_NETQUAKE && dialect <= QA_RULESET_Q3;
}

bool qac_q1(qa_ruleset_id dialect)
{
    return dialect == QA_RULESET_NETQUAKE || dialect == QA_RULESET_QUAKEWORLD;
}

bool qac_q2(qa_ruleset_id dialect)
{
    return dialect == QA_RULESET_Q2_CLASSIC || dialect == QA_RULESET_Q2_RERELEASE;
}

bool qac_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *left == *right;
}

char *qac_copy_n(const char *text, size_t length, qa_error *error)
{
    if (length == SIZE_MAX) {
        qac_fail(error, QA_ERROR_MEMORY, "console string exceeds address space");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating console string");
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *qac_copy(const char *text, qa_error *error)
{
    return qac_copy_n(text, strlen(text), error);
}

bool qac_text_add(qac_text *text, const char *data, size_t length, qa_error *error)
{
    if (length >= SIZE_MAX - text->size)
        return qac_fail(error, QA_ERROR_MEMORY, "console text exceeds address space");
    size_t needed = text->size + length + 1;
    if (needed > text->capacity) {
        size_t capacity = text->capacity == 0 ? 64 : text->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        char *grown = realloc(text->data, capacity);
        if (grown == NULL) return qac_fail(error, QA_ERROR_MEMORY, "growing console text");
        text->data = grown;
        text->capacity = capacity;
    }
    if (length != 0) memcpy(text->data + text->size, data, length);
    text->size += length;
    text->data[text->size] = '\0';
    return true;
}

bool qac_text_string(qac_text *text, const char *value, qa_error *error)
{
    return qac_text_add(text, value, strlen(value), error);
}

bool qac_text_finish(qac_text *text, qa_buffer *out, qa_error *error)
{
    if (!qac_text_add(text, "", 0, error)) return false;
    *out = (qa_buffer){(uint8_t *)text->data, text->size};
    *text = (qac_text){0};
    return true;
}

bool qac_space(unsigned char byte, bool console_text)
{
    return byte <= 32 || (!console_text && byte >= 128);
}

static bool punctuation(char c)
{
    return c != '\0' && strchr("{}()':", c) != NULL;
}

bool qac_parse_token(const char *text, size_t length, size_t start,
                      qa_ruleset_id dialect, bool console_text,
                      qac_token *out, qa_error *error)
{
    size_t offset = start;
    *out = (qac_token){0};
    for (;;) {
        while (offset < length && qac_space((unsigned char)text[offset], console_text)) ++offset;
        if (offset + 1 < length && text[offset] == '/' && text[offset + 1] == '/') {
            if (dialect == QA_RULESET_Q3) return true;
            while (offset < length && text[offset] != '\n') ++offset;
            continue;
        }
        if (dialect == QA_RULESET_Q3 && offset + 1 < length &&
            text[offset] == '/' && text[offset + 1] == '*') {
            offset += 2;
            while (offset + 1 < length && !(text[offset] == '*' && text[offset + 1] == '/')) ++offset;
            offset = offset + 1 < length ? offset + 2 : length;
            continue;
        }
        break;
    }
    if (offset >= length) return true;
    bool quoted = text[offset] == '"';
    if (quoted) ++offset;
    size_t token_start = offset;
    if (quoted) {
        while (offset < length && text[offset] != '"') ++offset;
    } else if (dialect == QA_RULESET_NETQUAKE && punctuation(text[offset])) {
        ++offset;
    } else {
        while (offset < length && !qac_space((unsigned char)text[offset], console_text)) {
            if (dialect == QA_RULESET_NETQUAKE && punctuation(text[offset])) break;
            if (dialect == QA_RULESET_Q3 && (text[offset] == '"' ||
                (text[offset] == '/' && offset + 1 < length &&
                 (text[offset + 1] == '/' || text[offset + 1] == '*')))) break;
            ++offset;
        }
    }
    size_t size = offset - token_start;
    if (qac_q2(dialect) && size >= 128) {
        if (quoted) return qac_fail(error, QA_ERROR_FORMAT, "quoted Q2 command token exceeds 127 bytes");
        size = 0;
    } else if (qac_q1(dialect) && size >= 1024) {
        return qac_fail(error, QA_ERROR_FORMAT, "Q1 command token exceeds 1023 bytes");
    }
    if (quoted && offset < length && text[offset] == '"') ++offset;
    *out = (qac_token){token_start, size, offset, true};
    return true;
}

bool qa_command_tokenize(const char *text, qa_ruleset_id dialect,
    bool console_text, qa_command_tokens *out,
    void *(*allocate)(void *, size_t, size_t, qa_error *), void *context, qa_error *error)
{
    if (text == NULL || out == NULL || !qac_dialect_valid(dialect))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command tokenizer arguments");
    *out = (qa_command_tokens){.borrowed = allocate != NULL};
    size_t length = strlen(text);
    if (length > (SIZE_MAX - 1) / 2)
        return qac_fail(error, QA_ERROR_MEMORY, "command tokenizer input is too large");
    size_t maximum = dialect == QA_RULESET_Q3 ? 1024 : 80;
    size_t capacity = length < maximum ? length + 1 : maximum;
    out->values = allocate ? allocate(context, capacity * sizeof(*out->values), _Alignof(char *), error) :
        calloc(capacity, sizeof(*out->values));
    out->storage = allocate ? allocate(context, length * 2 + 1, 1, error) : malloc(length * 2 + 1);
    out->args_text = allocate ? allocate(context, length + 1, 1, error) : malloc(length + 1);
    if (out->values == NULL || out->storage == NULL || out->args_text == NULL) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating command tokens");
        goto fail;
    }
    size_t offset = 0;
    size_t used = 0;
    size_t args_start = length;
    while (offset < length) {
        while (offset < length && qac_space((unsigned char)text[offset], console_text) &&
               (dialect == QA_RULESET_Q3 || text[offset] != '\n')) ++offset;
        if (dialect != QA_RULESET_Q3 && offset < length && text[offset] == '\n') break;
        if (out->count == 1) args_start = offset;
        qac_token token;
        if (!qac_parse_token(text, length, offset, dialect, console_text, &token, error)) goto fail;
        if (!token.found) break;
        offset = token.end;
        if (out->count < maximum) {
            if (dialect == QA_RULESET_Q3 && used + token.size + 1 > 9216) {
                qac_fail(error, QA_ERROR_FORMAT, "Q3 tokenized command exceeds 9216 bytes");
                goto fail;
            }
            out->values[out->count++] = out->storage + used;
            memcpy(out->storage + used, text + token.start, token.size);
            used += token.size;
            out->storage[used++] = '\0';
        }
        if (dialect == QA_RULESET_Q3 && out->count == maximum) break;
    }
    if (dialect == QA_RULESET_Q3) {
        size_t args_used = 0;
        for (size_t i = 1; i < out->count; ++i) {
            if (i > 1) out->args_text[args_used++] = ' ';
            size_t size = strlen(out->values[i]);
            memcpy(out->args_text + args_used, out->values[i], size); args_used += size;
        }
        out->args_text[args_used] = 0;
    } else {
        size_t end = length;
        if (qac_q2(dialect)) while (end > args_start && (unsigned char)text[end - 1] <= 32) --end;
        memcpy(out->args_text, text + args_start, end - args_start);
        out->args_text[end - args_start] = 0;
    }
    return true;
fail:
    qa_command_tokens_free(out);
    return false;
}

void qa_command_tokens_free(qa_command_tokens *tokens)
{
    if (tokens == NULL) return;
    if (!tokens->borrowed) {
        free(tokens->values);
        free(tokens->args_text);
        free(tokens->storage);
    }
    *tokens = (qa_command_tokens){0};
}

bool qa_command_tokens_copy(const qa_command_tokens *from, qa_command_tokens *out,
    void *(*allocate)(void *, size_t, size_t, qa_error *), void *context, qa_error *error)
{
    if (!from || !out || (from->count && !from->values)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid literal command token copy");
        return false;
    }
    size_t size = 0;
    for (size_t i = 0; i < from->count; ++i) {
        size_t length = strlen(from->values[i]) + 1;
        if (length > SIZE_MAX - size) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Literal command storage overflows");
            return false;
        }
        size += length;
    }
    qa_command_tokens copy = {.count = from->count, .borrowed = allocate != NULL};
    copy.values = !from->count ? NULL : allocate
        ? allocate(context, from->count * sizeof(*copy.values), _Alignof(char *), error)
        : calloc(from->count, sizeof(*copy.values));
    copy.storage = !size ? NULL : allocate ? allocate(context, size, 1, error) : malloc(size);
    const char *args = from->args_text ? from->args_text : "";
    copy.args_text = allocate ? allocate(context, strlen(args) + 1, 1, error) : malloc(strlen(args) + 1);
    if ((from->count && !copy.values) || (size && !copy.storage) || !copy.args_text) {
        qa_command_tokens_free(&copy);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining literal command tokens");
        return false;
    }
    char *at = copy.storage;
    for (size_t i = 0; i < from->count; ++i) {
        size_t length = strlen(from->values[i]) + 1;
        copy.values[i] = at; memcpy(at, from->values[i], length); at += length;
    }
    strcpy(copy.args_text, args); *out = copy; return true;
}

int32_t qac_integer(const char *text)
{
    while ((unsigned char)*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0;
    uint32_t limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*text >= '0' && *text <= '9') {
        unsigned digit = (unsigned)(*text++ - '0');
        if (value > (limit - digit) / 10)
            value = limit;
        else value = value * 10 + digit;
    }
    uint32_t bits = value;
    if (negative) bits = 0u - bits;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static int filter_byte(unsigned char value, bool sensitive)
{
    if (sensitive) return value > 127 ? (int)value - 256 : value;
    if (value == 255) return -1;
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

bool qa_command_filter(const char *pattern, const char *name, bool case_sensitive)
{
    if (pattern == NULL || name == NULL) return false;
    size_t p = 0, n = 0;
    size_t plen = strlen(pattern), nlen = strlen(name);
    while (p < plen) {
        if (pattern[p] == '*') {
            size_t start = ++p;
            while (p < plen && pattern[p] != '*' && pattern[p] != '?') ++p;
            size_t count = p - start;
            if (count == 0) continue;
            bool found = false;
            while (n <= nlen && count <= nlen - n) {
                size_t i = 0;
                while (i < count && filter_byte((unsigned char)pattern[start + i], case_sensitive) ==
                       filter_byte((unsigned char)name[n + i], case_sensitive)) ++i;
                if (i == count) { n += count; found = true; break; }
                ++n;
            }
            if (!found) return false;
        } else if (pattern[p] == '?') {
            if (n > nlen) return false;
            ++p; ++n;
        } else if (pattern[p] == '[' && p + 1 < plen && pattern[p + 1] == '[') {
            ++p;
            if (n > nlen || name[n] != '[') return false;
            ++p; ++n;
        } else if (pattern[p] == '[') {
            if (n > nlen) return false;
            int current = filter_byte((unsigned char)name[n], case_sensitive);
            ++p;
            bool found = false;
            while (p < plen) {
                if (pattern[p] == ']' && (p + 1 == plen || pattern[p + 1] != ']')) break;
                if (p + 2 < plen && pattern[p + 1] == '-' &&
                    (pattern[p + 2] != ']' || (p + 3 < plen && pattern[p + 3] == ']'))) {
                    int first = filter_byte((unsigned char)pattern[p], case_sensitive);
                    int last = filter_byte((unsigned char)pattern[p + 2], case_sensitive);
                    found |= current >= first && current <= last;
                    p += 3;
                } else {
                    found |= current == filter_byte((unsigned char)pattern[p], case_sensitive);
                    ++p;
                }
            }
            if (!found || p == plen) return false;
            ++p; ++n;
        } else {
            if (n > nlen || filter_byte((unsigned char)pattern[p], case_sensitive) !=
                filter_byte((unsigned char)name[n], case_sensitive)) return false;
            ++p; ++n;
        }
    }
    return true;
}

float qac_number(const char *text, qa_ruleset_id dialect)
{
    if (qac_q1(dialect))
        return (float)qa_parse_quake_number(text,QA_QUAKE_NUMBER_SIGNED_QUOTE);
    double value=0;
    (void)qa_parse_atof(text,&value,NULL);
    return (float)value;
}
