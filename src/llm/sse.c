#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/sse.h. GPL-2.0-or-later. */
#include "qa/llm/sse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void response_format(const qa_llm_http_response *response, char *buffer, size_t capacity) {
  char type_buffer[256];
  const char *type = qa_llm_response_content_type_base(response, type_buffer, sizeof type_buffer);
  const char *format = type == NULL ? "missing content type"
      : strcmp(type, "text/event-stream") == 0 ? "event stream"
      : strcmp(type, "application/json") == 0 ? "JSON"
      : strcmp(type, "text/html") == 0 ? "HTML"
      : strcmp(type, "text/plain") == 0 ? "plain text" : "other content type";
  snprintf(buffer, capacity, "HTTP %d, %s", response->status, format);
}

typedef struct parser {
  qa_llm_abort_signal *signal;
  qa_llm_sse_on_event_fn on_event;
  void *ctx;
  qa_llm_error *error;
  qa_llm_strbuf buffer; /* decoded-but-unprocessed text */
  qa_llm_strbuf event; /* current event name */
  qa_llm_strbuf joined; /* data lines joined with '\n' */
  size_t data_lines;
  bool stopped;
  bool failed;
  bool received_event;
} parser;

static void parser_line(parser *state, const char *line, size_t length) {
  if (state->stopped || state->failed) return;
  if (length == 0) {
    if (state->data_lines > 0) {
      if (!qa_llm_check_request_abort(state->signal, state->error)) { state->failed = true; return; }
      state->received_event = true;
      qa_llm_sse_event event = { qa_llm_strbuf_text(&state->event), qa_llm_strbuf_text(&state->joined) };
      qa_llm_sse_action action = state->on_event(state->ctx, &event, state->error);
      if (action == QA_LLM_SSE_STOP) state->stopped = true;
      if (action == QA_LLM_SSE_FAIL) state->failed = true;
    }
    state->event.length = 0;
    if (state->event.data != NULL) state->event.data[0] = '\0';
    qa_llm_strbuf_append(&state->event, "message");
    state->joined.length = 0;
    if (state->joined.data != NULL) state->joined.data[0] = '\0';
    state->data_lines = 0;
    return;
  }
  if (line[0] == ':') return;
  const char *colon = memchr(line, ':', length);
  size_t field_len = colon != NULL ? (size_t)(colon - line) : length;
  const char *content = colon != NULL ? colon + 1 : line + length;
  size_t content_len = (size_t)((line + length) - content);
  if (content_len > 0 && content[0] == ' ') { content++; content_len--; }
  if (field_len == 4 && memcmp(line, "data", 4) == 0) {
    if (state->data_lines > 0) qa_llm_strbuf_append_char(&state->joined, '\n');
    qa_llm_strbuf_append_n(&state->joined, content, content_len);
    state->data_lines++;
  } else if (field_len == 5 && memcmp(line, "event", 5) == 0) {
    state->event.length = 0;
    if (state->event.data != NULL) state->event.data[0] = '\0';
    qa_llm_strbuf_append_n(&state->event, content, content_len);
  }
}

static void parser_drain(parser *state, bool final) {
  while (!state->stopped && !state->failed) {
    const char *text = qa_llm_strbuf_text(&state->buffer);
    size_t length = state->buffer.length;
    size_t end = 0;
    while (end < length && text[end] != '\r' && text[end] != '\n') end++;
    if (end == length) break;
    if (!final && end == length - 1 && text[end] == '\r') break;
    size_t width = text[end] == '\r' && end + 1 < length && text[end + 1] == '\n' ? 2 : 1;
    char *line = qa_llm_strndup(text, end);
    memmove(state->buffer.data, state->buffer.data + end + width, length - end - width);
    state->buffer.length = length - end - width;
    state->buffer.data[state->buffer.length] = '\0';
    parser_line(state, line, end);
    free(line);
  }
  if (final && !state->stopped && !state->failed && state->buffer.length > 0) {
    char *line = qa_llm_strndup(state->buffer.data, state->buffer.length);
    size_t length = state->buffer.length;
    state->buffer.length = 0;
    state->buffer.data[0] = '\0';
    parser_line(state, line, length);
    free(line);
  }
  if (final && !state->stopped && !state->failed) parser_line(state, "", 0);
}

bool qa_llm_consume_sse(qa_llm_http_response *response, qa_llm_abort_signal *signal,
                        qa_llm_sse_on_event_fn on_event, void *ctx, bool require_content_type, qa_llm_error *error) {
  bool has_body = false;
  if (!qa_llm_response_body(response, &has_body, error)) {
    qa_llm_cancel_response_body(response);
    return false;
  }
  char type_buffer[256];
  const char *content_type = qa_llm_response_content_type_base(response, type_buffer, sizeof type_buffer);
  bool is_stream = content_type != NULL && strcmp(content_type, "text/event-stream") == 0;
  /* TS precedence: (requireContentType && !is_stream) || body === null. */
  if ((require_content_type && !is_stream) || !has_body) {
    char format[128];
    response_format(response, format, sizeof format);
    qa_llm_cancel_response_body(response);
    qa_llm_error_setf(error, "LLM service did not return an event stream (%s%s).", format, has_body ? "" : ", no body");
    return false;
  }

  parser state;
  memset(&state, 0, sizeof state);
  state.signal = signal;
  state.on_event = on_event;
  state.ctx = ctx;
  state.error = error;
  qa_llm_strbuf_init(&state.buffer);
  qa_llm_strbuf_init(&state.event);
  qa_llm_strbuf_init(&state.joined);
  qa_llm_strbuf_append(&state.event, "message");

  qa_llm_bytes raw;
  qa_llm_bytes_init(&raw); /* undecoded tail (split UTF-8 sequences) */
  bool ok = true;
  bool stream_error = false; /* non-LlmSettingsError failure class */
  size_t bytes = 0;
  uint8_t chunk[8192];

  if (!qa_llm_check_request_abort(signal, error)) ok = false;
  while (ok && !state.stopped && !state.failed) {
    size_t length = 0;
    qa_llm_read_result result = response->read(response->body_ctx, chunk, sizeof chunk, &length);
    if (!qa_llm_check_request_abort(signal, error)) { ok = false; break; }
    if (result == QA_LLM_READ_ERROR) { stream_error = true; ok = false; break; }
    if (result == QA_LLM_READ_DONE) {
      /* decoder.decode() flush: a dangling partial sequence is fatal. */
      if (raw.length > 0) { stream_error = true; ok = false; break; }
      parser_drain(&state, true);
      if (state.failed) ok = false;
      break;
    }
    bytes += length;
    if (bytes > QA_LLM_MAX_STREAM_BYTES) {
      qa_llm_error_set(error, "LLM response exceeded the size limit.");
      ok = false;
      break;
    }
    qa_llm_bytes_append(&raw, chunk, length);
    size_t valid = 0;
    if (!qa_llm_utf8_valid(raw.data, raw.length, &valid)) {
      /* Invalid only when the bad byte is not a split sequence tail:
       * re-check the prefix; a partial tail waits for more bytes. */
      bool partial_tail = valid < raw.length && qa_llm_utf8_incomplete_tail(raw.data + valid, raw.length - valid);
      if (!partial_tail) { stream_error = true; ok = false; break; }
    } else {
      valid = raw.length;
    }
    if (valid > 0) {
      qa_llm_strbuf_append_n(&state.buffer, raw.data, valid);
      memmove(raw.data, raw.data + valid, raw.length - valid);
      raw.length -= valid;
    }
    parser_drain(&state, false);
    if (state.failed) ok = false;
  }
  if (state.failed) ok = false;
  if (ok && !state.received_event) {
    char format[128];
    response_format(response, format, sizeof format);
    qa_llm_error_setf(error, "LLM service returned no event data (%s).", format);
    ok = false;
  }
  if (!ok) {
    /* catch: abort wins; LlmSettingsError rethrows; else generic. */
    qa_llm_error abort_error;
    if (!qa_llm_check_request_abort(signal, &abort_error)) {
      *error = abort_error;
    } else if (stream_error) {
      qa_llm_error_set(error, "Could not read the LLM response stream.");
    }
  }
  /* finally: reader.cancel() */
  qa_llm_cancel_response_body(response);
  qa_llm_bytes_free(&raw);
  qa_llm_strbuf_free(&state.buffer);
  qa_llm_strbuf_free(&state.event);
  qa_llm_strbuf_free(&state.joined);
  return ok;
}
