/*
 * Port of src/llm/request.ts. C accommodations (noted per PORTING.md):
 * async/Promise become synchronous calls; AbortSignal becomes a caller
 * owned flag struct that callbacks and deadlines can set; fetch/Response
 * become an injected function pointer returning a pull-reader response,
 * exactly where the TypeScript injects its fetch. The default fetch is
 * libcurl (multi interface, so SSE consumers pull chunks incrementally).
 * withRequestAbort's race collapses to abort checks around the call.
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_REQUEST_H
#define QA_LLM_REQUEST_H

#include "qa/llm/errors.h"

typedef struct qa_llm_abort_signal {
  bool aborted;
  bool timed_out; /* set when the deadline, not the caller, aborted it */
  int64_t deadline_mono_ms; /* 0: no deadline */
  /* Abort forwarding, mirroring caller.addEventListener("abort", stop)
   * wired into a fresh AbortController. Borrowed, may be NULL. */
  struct qa_llm_abort_signal *parent;
} qa_llm_abort_signal;

void qa_llm_signal_init(qa_llm_abort_signal *signal);
void qa_llm_signal_abort(qa_llm_abort_signal *signal);
void qa_llm_signal_set_timeout(qa_llm_abort_signal *signal, double timeout_ms);
/* signal.aborted, also tripping (and latching) an expired deadline. */
bool qa_llm_signal_is_aborted(qa_llm_abort_signal *signal);

typedef void (*qa_llm_on_text_fn)(void *ctx, const char *text);

/* LlmRequestInput; NULL string fields mirror undefined. */
typedef struct qa_llm_request_input {
  const char *prompt;
  const char *instructions;
  const char *session_instructions;
  const char *session_id;
  qa_llm_abort_signal *signal;
  qa_llm_on_text_fn on_text;
  void *on_text_ctx;
} qa_llm_request_input;

/* TransportRequest: input.signal is required (non-NULL). */
typedef struct qa_llm_transport_request {
  qa_llm_request_input input;
  const char *model;
  const char *reasoning_effort; /* NULL: undefined */
} qa_llm_transport_request;

typedef struct qa_llm_header {
  const char *name;
  const char *value;
} qa_llm_header;

/* RequestInit as built by the transports. */
typedef struct qa_llm_http_init {
  const char *method;
  const qa_llm_header *headers;
  size_t header_count;
  const char *body; /* NULL: no body */
  size_t body_length;
  qa_llm_abort_signal *signal;
  bool redirect_error; /* redirect: "error" once fetchLlmResponse ran */
} qa_llm_http_init;

typedef enum qa_llm_read_result {
  QA_LLM_READ_OK,
  QA_LLM_READ_DONE,
  QA_LLM_READ_ERROR,
} qa_llm_read_result;

typedef qa_llm_read_result (*qa_llm_read_fn)(void *ctx, uint8_t *buffer, size_t capacity, size_t *out_length);
typedef void (*qa_llm_body_cancel_fn)(void *ctx);
typedef void (*qa_llm_body_free_fn)(void *ctx);

/* Response: status/headers plus a ReadableStream-like pull body. */
typedef struct qa_llm_http_response {
  int status;
  char *content_type; /* owned; NULL mirrors a missing header */
  bool has_body; /* response.body !== null */
  bool invalid_body; /* body present but not a ReadableStream */
  bool cancelled; /* cancelResponseBody / reader.cancel observed */
  qa_llm_read_fn read;
  qa_llm_body_cancel_fn cancel;
  qa_llm_body_free_fn free_ctx;
  void *body_ctx;
} qa_llm_http_response;

void qa_llm_response_init(qa_llm_http_response *response);
/* In-memory body (copied); chunk_size > 0 caps each read, mirroring the
 * tests' byte-at-a-time streams. content_type NULL omits the header. */
void qa_llm_response_init_memory(qa_llm_http_response *response, int status, const char *content_type,
                                 const void *bytes, size_t length, size_t chunk_size);
void qa_llm_response_dispose(qa_llm_http_response *response);
bool qa_llm_response_ok(const qa_llm_http_response *response);
/* headers.get("content-type")?.split(";")[0]?.trim().toLowerCase();
 * returns NULL when absent, else writes into buffer and returns it. */
const char *qa_llm_response_content_type_base(const qa_llm_http_response *response, char *buffer, size_t capacity);
/* Reads the whole body; false on reader error or abort. */
bool qa_llm_response_read_all(qa_llm_http_response *response, qa_llm_abort_signal *signal, qa_llm_bytes *out);

/* LlmFetch. On false the fetch itself failed (network class). */
typedef bool (*qa_llm_fetch_fn)(void *ctx, const char *url, const qa_llm_http_init *init,
                                qa_llm_http_response *out, qa_llm_error *error);
typedef struct qa_llm_fetch_ref {
  qa_llm_fetch_fn fn; /* NULL: the default libcurl fetch */
  void *ctx;
} qa_llm_fetch_ref;

/* LlmRequestOptions. */
typedef struct qa_llm_request_options {
  qa_llm_fetch_ref fetch;
  bool has_timeout;
  double timeout_ms;
} qa_llm_request_options;

bool qa_llm_curl_fetch(void *ctx, const char *url, const qa_llm_http_init *init,
                       qa_llm_http_response *out, qa_llm_error *error);
bool qa_llm_fetch_invoke(qa_llm_fetch_ref fetcher, const char *url, const qa_llm_http_init *init,
                         qa_llm_http_response *out, qa_llm_error *error);

/* checkRequestAbort: false with "LLM request cancelled." when aborted. */
bool qa_llm_check_request_abort(qa_llm_abort_signal *signal, qa_llm_error *error);
/* fetchLlmResponse: forces signal + redirect "error", maps failures. */
bool qa_llm_fetch_llm_response(const char *url, const qa_llm_http_init *init, qa_llm_fetch_ref fetcher,
                               qa_llm_abort_signal *signal, qa_llm_http_response *out, qa_llm_error *error);
/* responseBody: false with error when the body is not a stream;
 * *out_has_body mirrors the returned stream being non-null. */
bool qa_llm_response_body(qa_llm_http_response *response, bool *out_has_body, qa_llm_error *error);
/* cancelResponseBody (invalid bodies keep TS behavior: also an error
 * there, but callers in a throw path never observe it; here it cancels
 * what it can and records cancelled). */
void qa_llm_cancel_response_body(qa_llm_http_response *response);

#endif
