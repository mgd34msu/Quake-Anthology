#include "internal.h"

bool ui_search(const char *title, const char *key, const char *source,
                      qa_bytes query, bool *out, qa_error *error) {
    *out = true;
    if (!query.size) return true;
    title = title ? title : ""; key = key ? key : ""; source = source ? source : "";
    size_t a = strlen(title), b = strlen(key), c = strlen(source);
    if (a > SIZE_MAX - 3 || b > SIZE_MAX - a - 3 || c > SIZE_MAX - a - b - 3)
        return ui_fail(error, "UI search text overflow");
    char *joined = malloc(a + b + c + 3);
    if (!joined) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating UI search text"); return false; }
    memcpy(joined, title, a); joined[a] = ' ';
    memcpy(joined + a + 1, key, b); joined[a + b + 1] = ' ';
    memcpy(joined + a + b + 2, source, c); joined[a + b + c + 2] = 0;
    qa_buffer lower = {0};
    bool ok = qa_utf8_lower((qa_bytes){(const uint8_t *)joined, a + b + c + 2}, &lower, error);
    free(joined);
    if (!ok) return false;
    size_t offset = 0, begin = 0;
    uint32_t scalar;
    while (offset < query.size) {
        begin = offset;
        if (!qa_utf8_next(query, &offset, &scalar)) break;
        if (qa_unicode_whitespace(scalar)) continue;
        size_t end = offset;
        while (offset < query.size) {
            size_t before = offset;
            if (!qa_utf8_next(query, &offset, &scalar)) break;
            if (qa_unicode_whitespace(scalar)) break;
            end = offset;
            if (offset <= before) break;
        }
        size_t length = end - begin;
        bool found = false;
        for (size_t i = 0; i <= lower.size && length <= lower.size - i; ++i)
            if (!memcmp(lower.data + i, query.data + begin, length)) { found = true; break; }
        if (!found) { *out = false; break; }
    }
    qa_buffer_free(&lower);
    return true;
}
