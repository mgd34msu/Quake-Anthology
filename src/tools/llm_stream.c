#include "llm_stream.h"
#include "qa/json.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

#define LLM_STREAM_LIMIT ((size_t)1048576)
bool llm_fail(qa_error *error, const char *message) { qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false; }
bool llm_text_add(llm_text *text, qa_bytes bytes, qa_error *error) {
    if ((bytes.size && !bytes.data) || bytes.size > SIZE_MAX - text->buffer.size - 1) return llm_fail(error, "LLM text size overflow");
    size_t needed = text->buffer.size + bytes.size + 1;
    if (needed > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 256;
        while (capacity < needed) { if (capacity > SIZE_MAX / 2) { capacity = needed; break; } capacity *= 2; }
        void *copy = realloc(text->buffer.data, capacity);
        if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating LLM text"); return false; }
        text->buffer.data = copy; text->capacity = capacity;
    }
    if (bytes.size) memcpy(text->buffer.data + text->buffer.size, bytes.data, bytes.size);
    text->buffer.size += bytes.size; text->buffer.data[text->buffer.size] = 0; return true;
}
bool llm_text_string(llm_text *text, const char *value, qa_error *error) { return llm_text_add(text, (qa_bytes){(const uint8_t *)value, strlen(value)}, error); }
void llm_text_clear(llm_text *text) { text->buffer.size = 0; if (text->buffer.data) text->buffer.data[0] = 0; }
void llm_stream_destroy(llm_stream *s) {
    qa_buffer_free(&s->raw.buffer); qa_buffer_free(&s->data.buffer); qa_buffer_free(&s->event.buffer); qa_buffer_free(&s->text.buffer); *s = (llm_stream){0};
}
static bool append_output(llm_stream *s, qa_bytes text, qa_error *error) {
    if (text.size > LLM_STREAM_LIMIT - s->text.buffer.size) return llm_fail(error, "LLM response exceeded the size limit");
    if (!llm_text_add(&s->text, text, error)) return false;
    if (!text.size && s->provider == QA_LLM_OTHER_API) return true;
    return !s->emit || s->emit(s->context, text, error);
}
static bool object(const qa_json_document *d, qa_json_id id, qa_error *error) {
    return qa_json_type(d, id) == QA_JSON_OBJECT || llm_fail(error, "Invalid LLM response");
}
static bool output_item(const qa_json_document *d, qa_json_id id, llm_text *out, qa_error *error) {
    if (!object(d, id, error)) return false;
    qa_json_id type = qa_json_get(d, id, "type");
    bool message = qa_json_string_equal(d, type, "message");
    if (!message && !qa_json_string_equal(d, type, "reasoning")) return llm_fail(error, "LLM returned an unsupported output item");
    qa_json_id content = qa_json_get(d, id, "content");
    if (!message || content == QA_JSON_NONE) return true;
    if (qa_json_type(d, content) != QA_JSON_ARRAY) return llm_fail(error, "Invalid LLM response");
    for (size_t i = 0; i < qa_json_size(d, content); ++i) {
        qa_json_id part = qa_json_at(d, content, i);
        if (!object(d, part, error)) return false;
        qa_json_id kind = qa_json_get(d, part, "type");
        if (qa_json_string_equal(d, kind, "refusal")) return llm_fail(error, "LLM declined the request");
        if (!qa_json_string_equal(d, kind, "output_text")) return llm_fail(error, "LLM returned unsupported content");
        qa_buffer text = {0};
        if (!qa_json_string(d, qa_json_get(d, part, "text"), &text, error)) return llm_fail(error, "Invalid LLM response");
        bool ok = llm_text_add(out, (qa_bytes){text.data, text.size}, error); qa_buffer_free(&text); if (!ok) return false;
    }
    return true;
}
static bool name_is(qa_bytes name, const char *value) {
    size_t n = strlen(value); return name.size == n && (!n || !memcmp(name.data, value, n));
}
static bool name_starts(qa_bytes name, const char *value) {
    size_t n = strlen(value); return name.size >= n && !memcmp(name.data, value, n);
}
static bool responses(llm_stream *s, const qa_json_document *d, qa_json_id root, qa_error *error) {
    qa_buffer type = {0}; qa_json_id kind = qa_json_get(d, root, "type");
    if (kind == QA_JSON_NONE || qa_json_type(d, kind) == QA_JSON_NULL) {
        if (!llm_text_add(&s->event, (qa_bytes){NULL, 0}, error)) return false;
    } else if (qa_json_type(d, kind) == QA_JSON_STRING) {
        if (!qa_json_string(d, kind, &type, error)) return false;
    } else return true; /* Unknown nonstring types are ignored by the donor. */
    qa_bytes name = type.data ? (qa_bytes){type.data, type.size} : s->event.buffer.data ?
        (qa_bytes){s->event.buffer.data, s->event.buffer.size} : (qa_bytes){(const uint8_t *)"message", 7};
    bool ok = false;
    if (name_is(name, "response.failed") || name_is(name, "response.incomplete") || name_is(name, "error")) { llm_fail(error, "LLM response failed. Try again"); goto done; }
    if (name_starts(name, "response.refusal.")) { llm_fail(error, "LLM declined the request"); goto done; }
    if (name_starts(name, "response.function_call_arguments.")) { llm_fail(error, "LLM returned an unsupported tool call"); goto done; }
    if (name_is(name, "response.output_item.added") || name_is(name, "response.output_item.done")) {
        llm_text discarded = {0};
        ok = output_item(d, qa_json_get(d, root, "item"), &discarded, error); qa_buffer_free(&discarded.buffer); if (!ok) goto done;
    }
    if (name_is(name, "response.content_part.added") || name_is(name, "response.content_part.done")) {
        qa_json_id part = qa_json_get(d, root, "part");
        if (!object(d, part, error)) goto done;
        if (qa_json_string_equal(d, qa_json_get(d, part, "type"), "refusal")) { llm_fail(error, "LLM declined the request"); goto done; }
    }
    if (name_is(name, "response.output_text.delta")) {
        qa_buffer text = {0};
        if (s->completed || !qa_json_string(d, qa_json_get(d, root, "delta"), &text, error)) { llm_fail(error, "Invalid LLM response"); goto done; }
        ok = append_output(s, (qa_bytes){text.data, text.size}, error); qa_buffer_free(&text); goto done;
    }
    if (name_is(name, "response.completed")) {
        qa_json_id result = qa_json_get(d, root, "response");
        if (!object(d, result, error)) goto done;
        if (!qa_json_string_equal(d, qa_json_get(d, result, "status"), "completed")) { llm_fail(error, "LLM response did not complete. Try again"); goto done; }
        qa_json_id output = qa_json_get(d, result, "output"); llm_text final = {0};
        if (output != QA_JSON_NONE) {
            if (qa_json_type(d, output) != QA_JSON_ARRAY) { llm_fail(error, "Invalid LLM response"); goto final_done; }
            for (size_t i = 0; i < qa_json_size(d, output); ++i) if (!output_item(d, qa_json_at(d, output, i), &final, error)) goto final_done;
            if (qa_json_size(d, output) && s->text.buffer.size &&
                (final.buffer.size != s->text.buffer.size || memcmp(final.buffer.data, s->text.buffer.data, final.buffer.size))) {
                llm_fail(error, "LLM returned inconsistent response text"); goto final_done;
            }
            if (!s->text.buffer.size && final.buffer.size && !append_output(s, (qa_bytes){final.buffer.data, final.buffer.size}, error)) goto final_done;
        }
        s->completed = s->stopped = true; ok = true;
final_done:
        qa_buffer_free(&final.buffer); goto done;
    }
    ok = true;
done:
    qa_buffer_free(&type); return ok;
}
static bool completions(llm_stream *s, const qa_json_document *d, qa_json_id root, qa_error *error) {
    if (qa_json_get(d, root, "error") != QA_JSON_NONE ||
        name_is((qa_bytes){s->event.buffer.data, s->event.buffer.size}, "error")) return llm_fail(error, "LLM service reported a response error");
    qa_json_id choices = qa_json_get(d, root, "choices");
    if (qa_json_type(d, choices) != QA_JSON_ARRAY) return llm_fail(error, "LLM service returned invalid completion data");
    for (size_t i = 0; i < qa_json_size(d, choices); ++i) {
        qa_json_id choice = qa_json_at(d, choices, i); if (!object(d, choice, error)) return false;
        qa_json_id index = qa_json_get(d, choice, "index"); double number;
        if (qa_json_type(d, index) != QA_JSON_NUMBER || !qa_json_number(d, index, &number, error) || number != 0) continue;
        qa_json_id delta = qa_json_get(d, choice, "delta"); if (!object(d, delta, error)) return false;
        if (qa_json_get(d, delta, "tool_calls") != QA_JSON_NONE || qa_json_get(d, delta, "function_call") != QA_JSON_NONE)
            return llm_fail(error, "LLM service returned an unsupported tool call");
        qa_json_id refusal = qa_json_get(d, delta, "refusal");
        if (qa_json_type(d, refusal) == QA_JSON_STRING && !qa_json_string_equal(d, refusal, "")) return llm_fail(error, "LLM service declined the request");
        qa_json_id content = qa_json_get(d, delta, "content");
        if (content != QA_JSON_NONE && qa_json_type(d, content) != QA_JSON_NULL) {
            qa_buffer text = {0};
            if (s->finished || !qa_json_string(d, content, &text, error)) return llm_fail(error, "LLM service returned invalid completion content");
            bool ok = append_output(s, (qa_bytes){text.data, text.size}, error); qa_buffer_free(&text); if (!ok) return false;
        }
        qa_json_id finish = qa_json_get(d, choice, "finish_reason");
        if (finish != QA_JSON_NONE && qa_json_type(d, finish) != QA_JSON_NULL) {
            if (!qa_json_string_equal(d, finish, "stop")) return llm_fail(error, "LLM response was incomplete or declined. Try a shorter request");
            s->finished = true;
        }
    }
    return true;
}
static bool dispatch(llm_stream *s, qa_error *error) {
    if (!s->have_data) return true;
    s->received = true;
    if (s->data.buffer.size == 6 && !memcmp(s->data.buffer.data, "[DONE]", 6)) {
        if (s->provider == QA_LLM_OTHER_API) s->done = s->stopped = true;
        return true;
    }
    qa_json_document *d = NULL;
    if (!qa_json_parse((qa_bytes){s->data.buffer.data, s->data.buffer.size}, &d, error)) return llm_fail(error, "Invalid LLM response stream data");
    qa_json_id root = qa_json_root(d);
    bool ok = object(d, root, error) && (s->provider == QA_LLM_OTHER_API ? completions(s, d, root, error) : responses(s, d, root, error));
    qa_json_destroy(d); return ok;
}
static bool process_line(llm_stream *s, qa_bytes value, qa_error *error) {
    if (!value.size) {
        if (!dispatch(s, error)) return false;
        llm_text_clear(&s->data); llm_text_clear(&s->event); s->have_data = false; return true;
    }
    if (value.data[0] == ':') return true;
    size_t colon = 0; while (colon < value.size && value.data[colon] != ':') ++colon;
    size_t begin = colon < value.size ? colon + 1 : value.size;
    if (begin < value.size && value.data[begin] == ' ') ++begin;
    qa_bytes content = {value.data + begin, value.size - begin};
    if (colon == 4 && !memcmp(value.data, "data", 4)) {
        if (s->have_data && !llm_text_string(&s->data, "\n", error)) return false;
        s->have_data = true; return llm_text_add(&s->data, content, error);
    }
    if (colon == 5 && !memcmp(value.data, "event", 5)) { llm_text_clear(&s->event); return llm_text_add(&s->event, content, error); }
    return true;
}
static unsigned utf8_width(uint8_t c) { return c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0; }
static bool validate_utf8(llm_stream *s, qa_bytes bytes, qa_error *error) {
    size_t start = 0;
    if (s->utf8_count) {
        unsigned width = utf8_width(s->utf8_tail[0]);
        while (s->utf8_count < width && start < bytes.size) s->utf8_tail[s->utf8_count++] = bytes.data[start++];
        if (s->utf8_count < width) return true;
        if (!qa_utf8_valid((qa_bytes){s->utf8_tail, s->utf8_count})) return llm_fail(error, "Could not read the LLM UTF-8 stream");
        s->utf8_count = 0;
    }
    size_t end = bytes.size, lead = end;
    while (lead > start && end - lead < 3 && (bytes.data[lead - 1] & 0xc0) == 0x80) --lead;
    if (lead > start) {
        --lead; unsigned width = utf8_width(bytes.data[lead]);
        if (width && end - lead < width) {
            s->utf8_count = end - lead; memcpy(s->utf8_tail, bytes.data + lead, s->utf8_count); end = lead;
        }
    }
    return qa_utf8_valid((qa_bytes){bytes.data ? bytes.data + start : NULL, end - start}) || llm_fail(error, "Could not read the LLM UTF-8 stream");
}
static bool drain(llm_stream *s, bool final, qa_error *error) {
    while (!s->stopped) {
        size_t end = s->cursor;
        while (end < s->raw.buffer.size && s->raw.buffer.data[end] != '\r' && s->raw.buffer.data[end] != '\n') ++end;
        if (end == s->raw.buffer.size || (!final && end + 1 == s->raw.buffer.size && s->raw.buffer.data[end] == '\r')) break;
        size_t width = s->raw.buffer.data[end] == '\r' && end + 1 < s->raw.buffer.size && s->raw.buffer.data[end + 1] == '\n' ? 2 : 1;
        if (!process_line(s, (qa_bytes){s->raw.buffer.data + s->cursor, end - s->cursor}, error)) return false;
        s->cursor = end + width;
    }
    if (final && !s->stopped) {
        if (s->cursor < s->raw.buffer.size && !process_line(s, (qa_bytes){s->raw.buffer.data + s->cursor, s->raw.buffer.size - s->cursor}, error)) return false;
        s->cursor = s->raw.buffer.size;
        if (!s->stopped && !process_line(s, (qa_bytes){NULL, 0}, error)) return false;
    }
    if (s->cursor) {
        size_t remaining = s->raw.buffer.size - s->cursor;
        memmove(s->raw.buffer.data, s->raw.buffer.data + s->cursor, remaining);
        s->raw.buffer.size = remaining; s->raw.buffer.data[remaining] = 0; s->cursor = 0;
    }
    return true;
}
bool llm_stream_feed(llm_stream *s, qa_bytes bytes, qa_error *error) {
    if (!s || (bytes.size && !bytes.data)) return llm_fail(error, "invalid LLM stream bytes");
    if (s->stopped) return true;
    if (bytes.size > LLM_STREAM_LIMIT - s->total_bytes) return llm_fail(error, "LLM response exceeded the size limit");
    s->total_bytes += bytes.size;
    return validate_utf8(s, bytes, error) && llm_text_add(&s->raw, bytes, error) && drain(s, false, error);
}
bool llm_stream_finish(llm_stream *s, qa_error *error) {
    if (!s) return llm_fail(error, "invalid LLM stream completion");
    if (!s->stopped && s->utf8_count) return llm_fail(error, "Could not read the LLM response stream");
    if (!s->stopped && !drain(s, true, error)) return false;
    if (!s->received) return llm_fail(error, "LLM service returned no event data");
    if (s->provider == QA_LLM_OTHER_API ? !s->done || !s->finished : !s->completed) return llm_fail(error, "LLM response ended before completion. Try again");
    qa_bytes text = {s->text.buffer.data, s->text.buffer.size}; size_t cursor = 0; uint32_t scalar;
    while (qa_utf8_next(text, &cursor, &scalar)) if (!qa_unicode_whitespace(scalar)) return true;
    return llm_fail(error, "LLM service returned no text. Try again");
}
