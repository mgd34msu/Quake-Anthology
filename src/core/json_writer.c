#include "qa/json_writer.h"
#include "qa/json.h"
#include "qa/text.h"

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void fail(qa_json_writer *w, qa_status code, const char *message) {
    if (!w->failed)
        qa_error_set(&w->failure, code, w->bytes.size, "%s", message);
    w->failed = true;
}
static void append(qa_json_writer *w, const char *text, size_t length) {
    if (w->failed)
        return;
    if (length >= SIZE_MAX - w->bytes.size) {
        fail(w, QA_ERROR_MEMORY, "JSON output size overflow");
        return;
    }
    size_t required = w->bytes.size + length + 1;
    if (required > w->capacity) {
        size_t capacity = w->capacity ? w->capacity : 256;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        uint8_t *data = realloc(w->bytes.data, capacity);
        if (!data) {
            fail(w, QA_ERROR_MEMORY, "Allocating JSON output");
            return;
        }
        w->bytes.data = data;
        w->capacity = capacity;
    }
    if (length)
        memcpy(w->bytes.data + w->bytes.size, text, length);
    w->bytes.size += length;
    w->bytes.data[w->bytes.size] = 0;
}
static bool value(qa_json_writer *w) {
    if (w->failed)
        return false;
    if (!w->depth) {
        if (w->root) {
            fail(w, QA_ERROR_ARGUMENT, "JSON document already has a root");
            return false;
        }
        w->root = true;
    } else {
        qa_json_writer_scope *scope = &w->scopes[w->depth - 1];
        if (scope->object) {
            if (!scope->pending_key) {
                fail(w, QA_ERROR_ARGUMENT, "JSON object value requires a key");
                return false;
            }
            scope->pending_key = false;
        } else if (scope->nonempty)
            append(w, ",", 1);
        scope->nonempty = true;
    }
    return !w->failed;
}
static void begin(qa_json_writer *w, bool object) {
    if (!value(w))
        return;
    if (w->depth == sizeof(w->scopes) / sizeof(w->scopes[0])) {
        fail(w, QA_ERROR_ARGUMENT, "JSON writer nesting limit");
        return;
    }
    append(w, object ? "{" : "[", 1);
    w->scopes[w->depth++] = (qa_json_writer_scope){.object = object};
}
void qa_json_writer_object(qa_json_writer *w) { begin(w, true); }
void qa_json_writer_array(qa_json_writer *w) { begin(w, false); }
void qa_json_writer_end(qa_json_writer *w) {
    if (w->failed)
        return;
    if (!w->depth || w->scopes[w->depth - 1].pending_key) {
        fail(w, QA_ERROR_ARGUMENT, "Incomplete JSON container");
        return;
    }
    append(w, w->scopes[--w->depth].object ? "}" : "]", 1);
}
static void quoted(qa_json_writer *w, qa_bytes bytes) {
    if (w->failed)
        return;
    qa_buffer quote;
    if (!qa_json_quote(bytes, &quote, &w->failure)) {
        w->failed = true;
        return;
    }
    append(w, (const char *)quote.data, quote.size);
    qa_buffer_free(&quote);
}
void qa_json_writer_key(qa_json_writer *w, const char *key) {
    if (w->failed)
        return;
    if (!key || !w->depth || !w->scopes[w->depth - 1].object ||
        w->scopes[w->depth - 1].pending_key) {
        fail(w, QA_ERROR_ARGUMENT, "JSON key outside an object or missing previous value");
        return;
    }
    qa_json_writer_scope *scope = &w->scopes[w->depth - 1];
    if (scope->nonempty)
        append(w, ",", 1);
    quoted(w, (qa_bytes){(const uint8_t *)key, strlen(key)});
    append(w, ":", 1);
    scope->pending_key = true;
}
void qa_json_writer_bytes(qa_json_writer *w, qa_bytes bytes) {
    if (value(w))
        quoted(w, bytes);
}
void qa_json_writer_string(qa_json_writer *w, const char *text) {
    if (!text) {
        fail(w, QA_ERROR_ARGUMENT, "Missing JSON string");
        return;
    }
    qa_json_writer_bytes(w, (qa_bytes){(const uint8_t *)text, strlen(text)});
}
void qa_json_writer_number(qa_json_writer *w, double number) {
    if (!value(w))
        return;
    char text[32];
    if (!qa_format_number(number, text, &w->failure)) {
        w->failed = true;
        return;
    }
    append(w, text, strlen(text));
}
void qa_json_writer_u64(qa_json_writer *w, uint64_t number) {
    if (!value(w))
        return;
    char text[21];
    int length = snprintf(text, sizeof(text), "%" PRIu64, number);
    if (length < 1 || (size_t)length >= sizeof(text)) {
        fail(w, QA_ERROR_ARGUMENT, "Formatting JSON unsigned integer");
        return;
    }
    append(w, text, (size_t)length);
}
void qa_json_writer_bool(qa_json_writer *w, bool flag) {
    if (value(w))
        append(w, flag ? "true" : "false", flag ? 4 : 5);
}
void qa_json_writer_null(qa_json_writer *w) {
    if (value(w))
        append(w, "null", 4);
}
void qa_json_writer_destroy(qa_json_writer *w) {
    if (w) {
        qa_buffer_free(&w->bytes);
        *w = (qa_json_writer){0};
    }
}
bool qa_json_writer_finish(qa_json_writer *w, qa_buffer *out, qa_error *e) {
    if (!w->root || w->depth || !out)
        fail(w, QA_ERROR_ARGUMENT, "JSON document is incomplete");
    if (w->failed) {
        if (e)
            *e = w->failure;
        return false;
    }
    append(w, "\n", 1);
    if (w->failed) {
        if (e)
            *e = w->failure;
        return false;
    }
    *out = w->bytes;
    *w = (qa_json_writer){0};
    return true;
}
