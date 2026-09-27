#include "qa/common_parse.h"
#include "qa/text.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, size_t offset, const char *message) {
    qa_error_set(error, code, offset, "%s", message);
    return false;
}
static size_t c_length(qa_bytes bytes) {
    const uint8_t *nul = bytes.size ? memchr(bytes.data, 0, bytes.size) : NULL;
    return nul ? (size_t)(nul - bytes.data) : bytes.size;
}
bool qa_common_cursor_init(qa_common_cursor *cursor, qa_bytes source, qa_common_end end,
                           qa_error *error) {
    if (!cursor || (source.size && !source.data) || source.size == SIZE_MAX ||
        (end != QA_COMMON_TERMINATED && end != QA_COMMON_UNINITIALIZED))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid COM_Parse source");
    *cursor = (qa_common_cursor){.source = source, .terminator = c_length(source), .end = end};
    return true;
}
qa_common_cursor_state qa_common_cursor_capture(const qa_common_cursor *cursor) {
    return (qa_common_cursor_state){.offset = cursor->ended ? 0 : cursor->offset,
                                    .ended = cursor->ended};
}
bool qa_common_cursor_restore(qa_common_cursor *cursor, qa_common_cursor_state state,
                              qa_error *error) {
    if (!cursor || (!state.ended && state.offset > cursor->terminator))
        return fail(error, QA_ERROR_ARGUMENT, state.offset,
                    "COM_Parse cursor is outside its C byte string");
    cursor->offset = state.ended ? 0 : state.offset;
    cursor->ended = state.ended;
    return true;
}
qa_common_parser_state qa_common_parser_capture(const qa_common_parser *parser) {
    return (qa_common_parser_state){.token = {(const uint8_t *)parser->token, parser->token_length},
                                    .name = {(const uint8_t *)parser->name, parser->name_length},
                                    .line = parser->line};
}
bool qa_common_parser_restore(qa_common_parser *parser, const qa_common_parser_state *state,
                              qa_error *error) {
    if (!parser || !state || state->token.size >= QA_COMMON_TOKEN_CAPACITY ||
        state->name.size >= QA_COMMON_TOKEN_CAPACITY || state->line < 0 ||
        (state->token.size && !state->token.data) || (state->name.size && !state->name.data))
        return fail(error, QA_ERROR_FORMAT, 0, "invalid COM_Parse saved continuation");
    qa_common_parser restored = {
        .token_length = state->token.size, .name_length = state->name.size, .line = state->line};
    if (state->token.size)
        memcpy(restored.token, state->token.data, state->token.size);
    if (state->name.size)
        memcpy(restored.name, state->name.data, state->name.size);
    *parser = restored;
    return true;
}
void qa_common_parser_reset(qa_common_parser *parser) {
    if (parser)
        *parser = (qa_common_parser){0};
}
bool qa_common_parser_begin(qa_common_parser *parser, const char *name, qa_common_print print,
                            void *context, qa_error *error) {
    if (!parser || !name)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid COM_BeginParseSession");
    parser->line = 0;
    size_t length = strlen(name);
    if (length >= 32000)
        return fail(error, QA_ERROR_FORMAT, length, "Com_sprintf: overflowed bigbuffer");
    size_t copied = length < QA_COMMON_TOKEN_CAPACITY ? length : QA_COMMON_TOKEN_CAPACITY - 1;
    char saved[QA_COMMON_TOKEN_CAPACITY];
    memcpy(saved, name, copied);
    if (length >= QA_COMMON_TOKEN_CAPACITY) {
        char message[96];
        int count = snprintf(message, sizeof(message), "Com_sprintf: overflow of %zu in %d\n",
                             length, QA_COMMON_TOKEN_CAPACITY);
        if (print && !print(context, (qa_bytes){(const uint8_t *)message, (size_t)count}, error))
            return false;
    }
    memcpy(parser->name, saved, copied);
    parser->name[copied] = 0;
    parser->name_length = copied;
    return true;
}
bool qa_common_parser_diagnostic(const qa_common_parser *parser, bool warning, const char *message,
                                 qa_common_print print, void *context, qa_error *error) {
    if (!parser || !message || !print)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid COM_Parse diagnostic");
    size_t length = strlen(message), name_length = parser->name_length;
    if (length > SIZE_MAX - name_length - 64)
        return fail(error, QA_ERROR_MEMORY, 0, "COM_Parse diagnostic size overflow");
    size_t capacity = length + name_length + 64;
    char *text = malloc(capacity);
    if (!text)
        return fail(error, QA_ERROR_MEMORY, 0, "allocating COM_Parse diagnostic");
    const char *kind = warning ? "WARNING: " : "ERROR: ";
    size_t written = strlen(kind);
    memcpy(text, kind, written);
    memcpy(text + written, parser->name, name_length);
    written += name_length;
    int count = snprintf(text + written, capacity - written, ", line %" PRId32 ": ", parser->line);
    written += (size_t)count;
    memcpy(text + written, message, length);
    written += length;
    text[written++] = '\n';
    text[written] = 0;
    bool ok = print(context, (qa_bytes){(const uint8_t *)text, written}, error);
    free(text);
    return ok;
}
bool qa_common_overwrite_token(qa_common_parser *parser, qa_bytes text, qa_error *error) {
    if (!parser || (text.size && !text.data))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid COM_Parse token write");
    size_t length = c_length(text);
    if (length >= QA_COMMON_TOKEN_CAPACITY)
        return fail(error, QA_ERROR_ARGUMENT, length,
                    "COM_Parse token write exceeds its source buffer");
    if (length)
        memmove(parser->token, text.data, length);
    parser->token[length] = 0;
    parser->token_length = length;
    return true;
}
static bool byte_at(const qa_common_cursor *cursor, size_t offset, int *out, qa_error *error) {
    if (offset < cursor->source.size) {
        unsigned byte = cursor->source.data[offset];
        *out = byte < 128 ? (int)byte : (int)byte - 256;
        return true;
    }
    if (offset == cursor->source.size && cursor->end == QA_COMMON_TERMINATED) {
        *out = 0;
        return true;
    }
    return fail(error, QA_ERROR_FORMAT, offset,
                "COM_Parse reached an uninitialized short-read tail");
}
static bool skip_comment(const qa_common_cursor *cursor, size_t *offset, bool block,
                         qa_error *error) {
    for (;;) {
        int byte, next;
        if (!byte_at(cursor, *offset, &byte, error))
            return false;
        if (!byte || (!block && byte == '\n'))
            return true;
        if (block && byte == '*') {
            if (!byte_at(cursor, *offset + 1, &next, error))
                return false;
            if (next == '/') {
                *offset += 2;
                return true;
            }
        }
        ++*offset;
    }
}
static void next_line(qa_common_parser *parser) {
    parser->line = parser->line == INT32_MAX ? INT32_MIN : parser->line + 1;
}
static void append(qa_common_parser *parser, uint8_t byte) {
    if (parser->token_length < QA_COMMON_TOKEN_CAPACITY) {
        parser->token[parser->token_length++] = (char)byte;
        parser->token[parser->token_length] = 0;
    }
}
bool qa_common_parse(qa_common_parser *parser, qa_common_cursor *cursor, bool allow_line_breaks,
                     qa_error *error) {
    if (!parser || !cursor)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid COM_Parse operation");
    parser->token_length = 0;
    parser->token[0] = 0;
    if (cursor->ended)
        return true;
    size_t offset = cursor->offset;
    bool crossed_line = false;
    int byte, lookahead;
    for (;;) {
        if (!byte_at(cursor, offset, &byte, error))
            return false;
        while (byte <= 32) {
            if (!byte) {
                cursor->ended = true;
                cursor->offset = 0;
                return true;
            }
            if (byte == '\n') {
                next_line(parser);
                crossed_line = true;
            }
            if (!byte_at(cursor, ++offset, &byte, error))
                return false;
        }
        if (crossed_line && !allow_line_breaks) {
            cursor->offset = offset;
            return true;
        }
        if (byte != '/')
            break;
        if (!byte_at(cursor, offset + 1, &lookahead, error))
            return false;
        if (lookahead == '/' || lookahead == '*') {
            offset += 2;
            if (!skip_comment(cursor, &offset, lookahead == '*', error))
                return false;
        } else
            break;
    }
    if (byte == '"') {
        ++offset;
        for (;;) {
            if (!byte_at(cursor, offset++, &byte, error))
                return false;
            if (byte == '"' || !byte) {
                if (parser->token_length == QA_COMMON_TOKEN_CAPACITY)
                    return fail(error, QA_ERROR_FORMAT, offset - 1,
                                "COM_Parse quoted token terminator exceeds 1024-byte storage");
                cursor->ended = byte == 0;
                cursor->offset = cursor->ended ? 0 : offset;
                return true;
            }
            append(parser, cursor->source.data[offset - 1]);
        }
    }
    do {
        append(parser, cursor->source.data[offset]);
        if (!byte_at(cursor, ++offset, &byte, error))
            return false;
        if (byte == '\n')
            next_line(parser);
    } while (byte > 32);
    if (parser->token_length == QA_COMMON_TOKEN_CAPACITY) {
        parser->token_length = 0;
        parser->token[0] = 0;
    }
    cursor->offset = offset;
    return true;
}
bool qa_common_skip_line(qa_common_parser *parser, qa_common_cursor *cursor, qa_error *error) {
    if (!parser || !cursor || cursor->ended)
        return fail(error, QA_ERROR_ARGUMENT, 0, "SkipRestOfLine requires a live source cursor");
    size_t offset = cursor->offset;
    for (;;) {
        int byte;
        if (!byte_at(cursor, offset++, &byte, error))
            return false;
        if (!byte) {
            cursor->ended = true;
            cursor->offset = 0;
            return true;
        }
        if (byte == '\n') {
            next_line(parser);
            cursor->offset = offset;
            return true;
        }
    }
}
bool qa_common_match(qa_common_parser *parser, qa_common_cursor *cursor, const char *expected,
                     qa_error *error) {
    if (!expected)
        return fail(error, QA_ERROR_ARGUMENT, 0, "missing MatchToken argument");
    if (!qa_common_parse(parser, cursor, true, error))
        return false;
    size_t length = strlen(expected);
    if (length == parser->token_length && !memcmp(parser->token, expected, length))
        return true;
    qa_error_set(error, QA_ERROR_FORMAT, cursor->offset, "MatchToken: %s != %s", parser->token,
                 expected);
    return false;
}
bool qa_common_skip_braced(qa_common_parser *parser, qa_common_cursor *cursor, qa_error *error) {
    size_t depth = 0;
    do {
        if (!qa_common_parse(parser, cursor, true, error))
            return false;
        if (!strcmp(parser->token, "{"))
            ++depth;
        else if (!strcmp(parser->token, "}"))
            --depth;
    } while (depth && !cursor->ended);
    return true;
}
static bool matrix_read(qa_common_parser *parser, qa_common_cursor *cursor,
                        const int32_t *dimensions, unsigned rank, float *matrix, size_t capacity,
                        size_t *offset, qa_error *error) {
    if (!qa_common_match(parser, cursor, "(", error))
        return false;
    for (int32_t i = 0; i < dimensions[0]; ++i) {
        if (rank > 1) {
            if (!matrix_read(parser, cursor, dimensions + 1, rank - 1, matrix, capacity, offset,
                             error))
                return false;
        } else {
            if (!qa_common_parse(parser, cursor, true, error))
                return false;
            if (*offset >= capacity || !matrix)
                return fail(error, QA_ERROR_ARGUMENT, *offset,
                            "Parse1DMatrix write exceeds its destination");
            double value;
            if (!qa_parse_atof(parser->token, &value, error))
                return false;
            matrix[(*offset)++] = (float)value;
        }
    }
    return qa_common_match(parser, cursor, ")", error);
}
bool qa_common_matrix_1d(qa_common_parser *parser, qa_common_cursor *cursor, int32_t x,
                         float *matrix, size_t capacity, size_t offset, qa_error *error) {
    return matrix_read(parser, cursor, &x, 1, matrix, capacity, &offset, error);
}
bool qa_common_matrix_2d(qa_common_parser *parser, qa_common_cursor *cursor, int32_t y, int32_t x,
                         float *matrix, size_t capacity, size_t offset, qa_error *error) {
    int32_t dimensions[] = {y, x};
    return matrix_read(parser, cursor, dimensions, 2, matrix, capacity, &offset, error);
}
bool qa_common_matrix_3d(qa_common_parser *parser, qa_common_cursor *cursor, int32_t z, int32_t y,
                         int32_t x, float *matrix, size_t capacity, size_t offset,
                         qa_error *error) {
    int32_t dimensions[] = {z, y, x};
    return matrix_read(parser, cursor, dimensions, 3, matrix, capacity, &offset, error);
}
bool qa_common_compress(qa_bytes source, qa_common_end end, qa_buffer *out, qa_error *error) {
    qa_common_cursor cursor;
    if (!out)
        return fail(error, QA_ERROR_ARGUMENT, 0, "missing COM_Compress output");
    if (!qa_common_cursor_init(&cursor, source, end, error))
        return false;
    uint8_t *text = malloc(cursor.terminator + 1);
    if (!text)
        return fail(error, QA_ERROR_MEMORY, 0, "allocating COM_Compress output");
    size_t offset = 0, length = 0;
    bool newline = false, whitespace = false;
    for (;;) {
        int byte, next = 0;
        if (!byte_at(&cursor, offset, &byte, error))
            goto failed;
        if (!byte)
            break;
        if (byte == '/' && !byte_at(&cursor, offset + 1, &next, error))
            goto failed;
        if (byte == '/' && (next == '/' || next == '*')) {
            if (!skip_comment(&cursor, &offset, next == '*', error))
                goto failed;
        } else if (byte == '\n' || byte == '\r') {
            newline = true;
            ++offset;
        } else if (byte == ' ' || byte == '\t') {
            whitespace = true;
            ++offset;
        } else {
            if (newline) {
                text[length++] = '\n';
                newline = whitespace = false;
            }
            if (whitespace) {
                text[length++] = ' ';
                whitespace = false;
            }
            text[length++] = source.data[offset++];
            if (byte == '"') {
                for (;;) {
                    if (!byte_at(&cursor, offset, &byte, error))
                        goto failed;
                    if (!byte)
                        break;
                    text[length++] = source.data[offset++];
                    if (byte == '"')
                        break;
                }
            }
        }
    }
    text[length] = 0;
    *out = (qa_buffer){.data = text, .size = length};
    return true;
failed:
    free(text);
    return false;
}
