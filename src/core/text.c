#include "qa/text.h"

#include <stdlib.h>
#include <string.h>
#include <unicode/ucasemap.h>
#include <unicode/utf8.h>

static UChar32 next_code(qa_bytes input, size_t *cursor) {
    size_t remaining = input.size - *cursor;
    int32_t at = 0, length = (int32_t)(remaining < 4 ? remaining : 4);
    UChar32 code;
    U8_NEXT(input.data + *cursor, at, length, code);
    *cursor += (size_t)at;
    return code;
}
bool qa_utf8_valid(qa_bytes input) {
    if ((!input.data && input.size) || input.size > PTRDIFF_MAX)
        return false;
    size_t cursor = 0;
    while (cursor < input.size)
        if (next_code(input, &cursor) < 0)
            return false;
    return true;
}
bool qa_utf8_next(qa_bytes input, size_t *cursor, uint32_t *scalar) {
    if (*cursor >= input.size)
        return false;
    UChar32 code = next_code(input, cursor);
    *scalar = code < 0 ? UINT32_C(0xfffd) : (uint32_t)code;
    return true;
}
size_t qa_utf8_encode(uint32_t code, char out[4]) {
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        code = 0xfffd;
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 63));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 63));
        out[2] = (char)(0x80 | (code & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 63));
    out[2] = (char)(0x80 | ((code >> 6) & 63));
    out[3] = (char)(0x80 | (code & 63));
    return 4;
}
bool qa_unicode_whitespace(uint32_t c) {
    return (c >= 9 && c <= 13) || c == 0x20 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000 || c == 0xfeff;
}

bool qa_utf8_repair(qa_bytes input, qa_buffer *out, qa_error *error) {
    if (!out || (!input.data && input.size) || input.size > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid UTF-8 resource input");
        return false;
    }
    int32_t length = (int32_t)input.size, at = 0;
    size_t needed = 0;
    while (at < length) {
        int32_t begin = at;
        UChar32 code;
        U8_NEXT(input.data, at, length, code);
        size_t count = code < 0 ? 3 : (size_t)(at - begin);
        if (count >= SIZE_MAX - needed) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "UTF-8 resource size overflow");
            return false;
        }
        needed += count;
    }
    uint8_t *data = malloc(needed + 1);
    if (!data) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UTF-8 resource text");
        return false;
    }
    at = 0;
    size_t written = 0;
    while (at < length) {
        int32_t begin = at;
        UChar32 code;
        U8_NEXT(input.data, at, length, code);
        size_t count = code < 0 ? 3 : (size_t)(at - begin);
        if (code < 0)
            memcpy(data + written, "\xef\xbf\xbd", 3);
        else
            memcpy(data + written, input.data + begin, count);
        written += count;
    }
    data[written] = 0;
    *out = (qa_buffer){data, written};
    return true;
}

bool qa_utf8_lower(qa_bytes input, qa_buffer *out, qa_error *error) {
    if (!out || (!input.data && input.size) || input.size > INT32_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Unicode lowercase input");
        return false;
    }
    UErrorCode status = U_ZERO_ERROR;
    UCaseMap *map = ucasemap_open("", 0, &status);
    if (U_FAILURE(status) || !map) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "opening Unicode case map: %s",
                     u_errorName(status));
        return false;
    }
    const char *source = input.size ? (const char *)input.data : "";
    int32_t needed = ucasemap_utf8ToLower(map, NULL, 0, source, (int32_t)input.size, &status);
    if ((U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) || needed < 0 ||
        needed == INT32_MAX) {
        ucasemap_close(map);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "lowercasing UTF-8: %s", u_errorName(status));
        return false;
    }
    uint8_t *data = malloc((size_t)needed + 1);
    if (!data) {
        ucasemap_close(map);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating lowercase text");
        return false;
    }
    status = U_ZERO_ERROR;
    int32_t written =
        ucasemap_utf8ToLower(map, (char *)data, needed + 1, source, (int32_t)input.size, &status);
    ucasemap_close(map);
    if (U_FAILURE(status) || written < 0 || written > needed) {
        free(data);
        qa_error_set(error, QA_ERROR_FORMAT, 0, "lowercasing UTF-8: %s", u_errorName(status));
        return false;
    }
    data[written] = 0;
    *out = (qa_buffer){data, (size_t)written};
    return true;
}
