#include "qa/command_lexer.h"

#include <string.h>

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}

bool qa_command_token_space(unsigned char byte, bool console_text)
{
    return byte <= 32 || (!console_text && byte >= 128);
}

static bool punctuation(char c)
{
    return c != '\0' && strchr("{}()':", c) != NULL;
}

bool qa_command_token_next(const char *text, size_t length, size_t start,
    qa_ruleset_id dialect, bool console_text, qa_command_token_span *out, qa_error *error)
{
    size_t offset = start;
    *out = (qa_command_token_span){0};
    for (;;) {
        while (offset < length && qa_command_token_space((unsigned char)text[offset], console_text)) ++offset;
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
        while (offset < length && !qa_command_token_space((unsigned char)text[offset], console_text)) {
            if (dialect == QA_RULESET_NETQUAKE && punctuation(text[offset])) break;
            if (dialect == QA_RULESET_Q3 && (text[offset] == '"' ||
                (text[offset] == '/' && offset + 1 < length &&
                 (text[offset + 1] == '/' || text[offset + 1] == '*')))) break;
            ++offset;
        }
    }
    size_t size = offset - token_start;
    if (size >= 128) {
        const qa_ruleset_descriptor *rules = qa_ruleset_read(dialect);
        if (rules && rules->family == QA_GAME_Q2) {
            if (quoted) return fail(error, "quoted Q2 command token exceeds 127 bytes");
            size = 0;
        } else if (rules && rules->family == QA_GAME_Q1 && size >= 1024) {
            return fail(error, "Q1 command token exceeds 1023 bytes");
        }
    }
    if (quoted && offset < length && text[offset] == '"') ++offset;
    *out = (qa_command_token_span){token_start, size, offset, true};
    return true;
}
