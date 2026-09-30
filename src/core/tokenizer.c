#include "qa/tokenizer.h"
#include "qa/text.h"
#include <string.h>

bool qa_tokenizer_init(qa_tokenizer *parser, qa_bytes source, qa_error *error) {
    const qa_tokenizer_options options = {.maximum_units = 1023};
    return qa_tokenizer_init_options(parser, source, &options, error);
}
bool qa_tokenizer_init_options(qa_tokenizer *parser, qa_bytes source,
                               const qa_tokenizer_options *options, qa_error *error) {
    if (!parser || !options || (source.size && !source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid text tokenizer");
        return false;
    }
    *parser = (qa_tokenizer){.source = source, .options = *options};
    return true;
}
static size_t whitespace(const qa_tokenizer *parser, size_t at) {
    if (at >= parser->source.size) return 0;
    if (!parser->options.unicode_whitespace) return parser->source.data[at] <= 32 ? 1 : 0;
    size_t next = at; uint32_t scalar;
    if (qa_utf8_next(parser->source, &next, &scalar) && qa_unicode_whitespace(scalar)) return next - at;
    return 0;
}
static bool punctuation(const qa_tokenizer *parser, uint8_t value) {
    return value && value < 128 && parser->options.punctuation &&
        strchr(parser->options.punctuation, value) != NULL;
}
bool qa_tokenizer_next(qa_tokenizer *parser, qa_token *token, bool *found, qa_error *error) {
    if (!parser || !token || !found || parser->offset > parser->source.size ||
        (parser->source.size && !parser->source.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid text tokenizer read");
        return false;
    }
    const uint8_t *p = parser->source.data;
    size_t n = parser->source.size, at = parser->offset;
    for (;;) {
        size_t skip;
        while ((skip = whitespace(parser, at)) != 0) at += skip;
        if (n - at >= 2 && p[at] == '/' && p[at + 1] == '/') {
            at += 2;
            while (at < n && p[at] != '\n' && p[at] != '\r') ++at;
        } else if (n - at >= 2 && p[at] == '/' && p[at + 1] == '*') {
            at += 2;
            while (n - at >= 2 && !(p[at] == '*' && p[at + 1] == '/')) ++at;
            at = n - at >= 2 ? at + 2 : n;
        } else break;
    }
    if (at == n) { parser->offset = at; *found = false; return true; }
    qa_token result = {.offset = at, .quoted = p[at] == '"'};
    size_t begin = at + (result.quoted ? 1 : 0);
    at = begin;
    if (!result.quoted && punctuation(parser, p[at])) ++at;
    else while (at < n && (result.quoted ? p[at] != '"' :
                           !whitespace(parser, at) && !punctuation(parser, p[at]))) {
        if (result.quoted && parser->options.reject_quoted_newlines && (p[at] == '\r' || p[at] == '\n')) {
            qa_error_set(error, QA_ERROR_FORMAT, at, "newline in quoted text token"); return false;
        }
        ++at;
    }
    if (result.quoted && parser->options.reject_quoted_newlines && at == n) {
        qa_error_set(error, QA_ERROR_FORMAT, result.offset, "unterminated quoted text token"); return false;
    }
    result.text = (qa_bytes){p + begin, at - begin};
    size_t units = 0, cursor = 0;
    uint32_t scalar;
    while (qa_utf8_next(result.text, &cursor, &scalar)) {
        units += scalar > 0xffff ? 2 : 1;
        if (result.quoted && scalar == '\r' && cursor < result.text.size && result.text.data[cursor] == '\n') {
            ++cursor;
            result.normalize_crlf = true;
        }
        if (parser->options.maximum_units && units > parser->options.maximum_units) {
            qa_error_set(error, QA_ERROR_FORMAT, result.offset, "text token exceeds %zu characters", parser->options.maximum_units);
            return false;
        }
    }
    parser->offset = at + (result.quoted && at < n ? 1 : 0);
    *token = result;
    *found = true;
    return true;
}
bool qa_token_copy(const qa_token *token, char *out, size_t capacity, qa_error *error) {
    if (!token || !out || !capacity || (token->text.size && !token->text.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid text token copy");
        return false;
    }
    size_t written = 0;
    for (size_t i = 0; i < token->text.size; ++i) {
        if (written == capacity - 1) {
            out[0] = 0;
            qa_error_set(error, QA_ERROR_ARGUMENT, token->offset, "text token copy is too small");
            return false;
        }
        out[written++] = (char)token->text.data[i];
        if (token->normalize_crlf && token->text.data[i] == '\r' && i + 1 < token->text.size &&
            token->text.data[i + 1] == '\n') ++i;
    }
    out[written] = 0;
    return true;
}
