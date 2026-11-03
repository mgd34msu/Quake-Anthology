#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/request.h. GPL-2.0-or-later. */
#include "qa/llm/request.h"
#include "qa/foundation/js.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js_case.h"

void qa_llm_signal_init(qa_llm_abort_signal *signal) {
  signal->aborted = false;
  signal->timed_out = false;
  signal->deadline_mono_ms = 0;
  signal->parent = NULL;
}

void qa_llm_signal_abort(qa_llm_abort_signal *signal) {
  if (signal != NULL) signal->aborted = true;
}

void qa_llm_signal_set_timeout(qa_llm_abort_signal *signal, double timeout_ms) {
  /* setTimeout(fn, ms) under Bun: NaN, below 1, or above 2^31 - 1 (Infinity too) runs
   * after 1 ms (TimeoutOverflowWarning); no C cast of an out-of-range double. */
  if (!(timeout_ms >= 1 && timeout_ms <= 2147483647.0)) timeout_ms = 1;
  signal->deadline_mono_ms = qa_llm_monotonic_ms() + (int64_t)timeout_ms;
}

bool qa_llm_signal_is_aborted(qa_llm_abort_signal *signal) {
  if (signal == NULL) return false;
  if (!signal->aborted && signal->parent != NULL && qa_llm_signal_is_aborted(signal->parent)) signal->aborted = true;
  if (!signal->aborted && signal->deadline_mono_ms != 0 && qa_llm_monotonic_ms() >= signal->deadline_mono_ms) {
    signal->aborted = true;
    signal->timed_out = true;
  }
  return signal->aborted;
}

bool qa_llm_check_request_abort(qa_llm_abort_signal *signal, qa_llm_error *error) {
  if (qa_llm_signal_is_aborted(signal)) {
    qa_llm_error_set(error, "LLM request cancelled.");
    return false;
  }
  return true;
}

void qa_llm_response_init(qa_llm_http_response *response) {
  memset(response, 0, sizeof *response);
}

typedef struct memory_body {
  uint8_t *bytes;
  size_t length;
  size_t offset;
  size_t chunk_size;
} memory_body;

static qa_llm_read_result memory_read(void *ctx, uint8_t *buffer, size_t capacity, size_t *out_length) {
  memory_body *body = ctx;
  if (body->offset >= body->length) return QA_LLM_READ_DONE;
  size_t count = body->length - body->offset;
  if (count > capacity) count = capacity;
  if (body->chunk_size > 0 && count > body->chunk_size) count = body->chunk_size;
  memcpy(buffer, body->bytes + body->offset, count);
  body->offset += count;
  *out_length = count;
  return QA_LLM_READ_OK;
}

static void memory_cancel(void *ctx) {
  memory_body *body = ctx;
  body->offset = body->length;
}

static void memory_free(void *ctx) {
  memory_body *body = ctx;
  free(body->bytes);
  free(body);
}

void qa_llm_response_init_memory(qa_llm_http_response *response, int status, const char *content_type,
                                 const void *bytes, size_t length, size_t chunk_size) {
  qa_llm_response_init(response);
  response->status = status;
  response->content_type = content_type != NULL ? qa_llm_strdup(content_type) : NULL;
  response->has_body = true;
  memory_body *body = malloc(sizeof *body);
  if (body == NULL) qa_llm_oom();
  body->bytes = malloc(length == 0 ? 1 : length);
  if (body->bytes == NULL) qa_llm_oom();
  if (length > 0) memcpy(body->bytes, bytes, length);
  body->length = length;
  body->offset = 0;
  body->chunk_size = chunk_size;
  response->read = memory_read;
  response->cancel = memory_cancel;
  response->free_ctx = memory_free;
  response->body_ctx = body;
}

void qa_llm_response_dispose(qa_llm_http_response *response) {
  free(response->content_type);
  if (response->free_ctx != NULL) response->free_ctx(response->body_ctx);
  qa_llm_response_init(response);
}

bool qa_llm_response_ok(const qa_llm_http_response *response) {
  return response->status >= 200 && response->status <= 299;
}

const char *qa_llm_response_content_type_base(const qa_llm_http_response *response, char *buffer, size_t capacity) {
  if (response->content_type == NULL) return NULL;
  const char *begin = response->content_type;
  const char *end = strchr(begin, ';');
  if (end == NULL) end = begin + strlen(begin);
  /* TS headers.get("content-type").split(";")[0].trim().toLowerCase(): the header is a
   * ByteString (Latin-1), so trim also strips U+00A0 and lowering folds Latin-1. */
  while (begin < end && (isspace((uint8_t)*begin) || (uint8_t)*begin == 0xa0)) begin++;
  while (end > begin && (isspace((uint8_t)end[-1]) || (uint8_t)end[-1] == 0xa0)) end--;
  size_t length = (size_t)(end - begin);
  if (length + 1 > capacity) length = capacity - 1;
  for (size_t i = 0; i < length; i++) buffer[i] = (char)qa_js_latin1_lower((uint8_t)begin[i]);
  buffer[length] = '\0';
  return buffer;
}

bool qa_llm_response_read_all(qa_llm_http_response *response, qa_llm_abort_signal *signal, qa_llm_bytes *out) {
  uint8_t buffer[8192];
  for (;;) {
    if (qa_llm_signal_is_aborted(signal)) return false;
    size_t length = 0;
    qa_llm_read_result result = response->read(response->body_ctx, buffer, sizeof buffer, &length);
    if (result == QA_LLM_READ_DONE) return true;
    if (result == QA_LLM_READ_ERROR) return false;
    qa_llm_bytes_append(out, buffer, length);
  }
}

bool qa_llm_response_body(qa_llm_http_response *response, bool *out_has_body, qa_llm_error *error) {
  if (!response->has_body) {
    *out_has_body = false;
    return true;
  }
  if (response->invalid_body) {
    qa_llm_error_set(error, "LLM service returned an invalid response body.");
    return false;
  }
  *out_has_body = true;
  return true;
}

void qa_llm_cancel_response_body(qa_llm_http_response *response) {
  if (!response->has_body || response->invalid_body) return;
  response->cancelled = true;
  if (response->cancel != NULL) response->cancel(response->body_ctx);
}

bool qa_llm_fetch_llm_response(const char *url, const qa_llm_http_init *init, qa_llm_fetch_ref fetcher,
                               qa_llm_abort_signal *signal, qa_llm_http_response *out, qa_llm_error *error) {
  if (!qa_llm_check_request_abort(signal, error)) return false;
  qa_llm_http_init merged = *init;
  merged.signal = signal;
  merged.redirect_error = true;
  qa_llm_error fetch_error;
  qa_llm_error_clear(&fetch_error);
  if (!qa_llm_fetch_invoke(fetcher, url, &merged, out, &fetch_error)) {
    if (!qa_llm_check_request_abort(signal, error)) return false;
    qa_llm_error_set(error, "Could not reach the LLM service. Check its URL and connection.");
    return false;
  }
  if (qa_llm_signal_is_aborted(signal)) {
    /* withRequestAbort loses the race after resolve: body cancelled. */
    qa_llm_cancel_response_body(out);
    qa_llm_response_dispose(out);
    qa_llm_error_set(error, "LLM request cancelled.");
    return false;
  }
  return true;
}

bool qa_llm_fetch_invoke(qa_llm_fetch_ref fetcher, const char *url, const qa_llm_http_init *init,
                         qa_llm_http_response *out, qa_llm_error *error) {
  qa_llm_response_init(out);
  qa_llm_fetch_fn fn = fetcher.fn != NULL ? fetcher.fn : qa_llm_curl_fetch;
  return fn(fetcher.ctx, url, init, out, error);
}

/* ---- libcurl default fetch (multi interface, pull reader) ---- */

typedef struct curl_body {
  CURLM *multi;
  CURL *easy;
  struct curl_slist *headers;
  qa_llm_bytes pending;
  size_t offset;
  bool transfer_done;
  bool failed;
  bool headers_done;
  qa_llm_abort_signal *signal;
  qa_llm_http_response *response;
} curl_body;

static size_t curl_write(void *data, size_t size, size_t count, void *ctx) {
  curl_body *body = ctx;
  size_t length = size * count;
  /* Compact consumed prefix before appending. */
  if (body->offset > 0) {
    memmove(body->pending.data, body->pending.data + body->offset, body->pending.length - body->offset);
    body->pending.length -= body->offset;
    body->offset = 0;
  }
  qa_llm_bytes_append(&body->pending, data, length);
  return length;
}

static size_t curl_header(void *data, size_t size, size_t count, void *ctx) {
  curl_body *body = ctx;
  size_t length = size * count;
  const char *line = data;
  if (length == 2 && line[0] == '\r' && line[1] == '\n') body->headers_done = true;
  /* Header names are case-insensitive; final response headers win by
   * only recording before body arrival on the last header block is
   * approximated by overwriting (redirects are errors here). */
  const char *name = "content-type:";
  size_t name_len = strlen(name);
  if (length > name_len) {
    bool match = true;
    for (size_t i = 0; i < name_len; i++) {
      if (tolower((uint8_t)line[i]) != name[i]) { match = false; break; }
    }
    if (match) {
      const char *value = line + name_len;
      size_t value_len = length - name_len;
      while (value_len > 0 && (*value == ' ' || *value == '\t')) { value++; value_len--; }
      while (value_len > 0 && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n' || value[value_len - 1] == ' ')) value_len--;
      free(body->response->content_type);
      body->response->content_type = qa_llm_strndup(value, value_len);
    }
  }
  return length;
}

static void curl_pump(curl_body *body, int poll_ms) {
  if (body->transfer_done) return;
  int running = 0;
  CURLMcode code = curl_multi_perform(body->multi, &running);
  if (code != CURLM_OK) { body->failed = true; body->transfer_done = true; return; }
  int queued = 0;
  CURLMsg *message;
  while ((message = curl_multi_info_read(body->multi, &queued)) != NULL) {
    if (message->msg == CURLMSG_DONE) {
      body->transfer_done = true;
      if (message->data.result != CURLE_OK) body->failed = true;
      long status = 0;
      curl_easy_getinfo(body->easy, CURLINFO_RESPONSE_CODE, &status);
      body->response->status = (int)status;
    }
  }
  if (!body->transfer_done && poll_ms > 0) curl_multi_poll(body->multi, NULL, 0, poll_ms, NULL);
}

static qa_llm_read_result curl_read(void *ctx, uint8_t *buffer, size_t capacity, size_t *out_length) {
  curl_body *body = ctx;
  for (;;) {
    if (qa_llm_signal_is_aborted(body->signal)) return QA_LLM_READ_ERROR;
    if (body->offset < body->pending.length) {
      size_t count = body->pending.length - body->offset;
      if (count > capacity) count = capacity;
      memcpy(buffer, body->pending.data + body->offset, count);
      body->offset += count;
      *out_length = count;
      return QA_LLM_READ_OK;
    }
    body->pending.length = 0;
    body->offset = 0;
    if (body->transfer_done) return body->failed ? QA_LLM_READ_ERROR : QA_LLM_READ_DONE;
    curl_pump(body, 100);
  }
}

static void curl_cancel(void *ctx) {
  curl_body *body = ctx;
  body->transfer_done = true;
  body->failed = false;
  if (body->multi != NULL && body->easy != NULL) curl_multi_remove_handle(body->multi, body->easy);
}

static void curl_free_body(void *ctx) {
  curl_body *body = ctx;
  if (body->multi != NULL && body->easy != NULL && !body->transfer_done) curl_multi_remove_handle(body->multi, body->easy);
  if (body->easy != NULL) curl_easy_cleanup(body->easy);
  if (body->multi != NULL) curl_multi_cleanup(body->multi);
  curl_slist_free_all(body->headers);
  qa_llm_bytes_free(&body->pending);
  free(body);
}

bool qa_llm_curl_fetch(void *ctx, const char *url, const qa_llm_http_init *init,
                       qa_llm_http_response *out, qa_llm_error *error) {
  (void)ctx;
  static bool initialized = false;
  if (!initialized) { curl_global_init(CURL_GLOBAL_DEFAULT); initialized = true; }
  curl_body *body = calloc(1, sizeof *body);
  if (body == NULL) qa_llm_oom();
  body->signal = init->signal;
  body->response = out;
  qa_llm_bytes_init(&body->pending);
  body->easy = curl_easy_init();
  body->multi = curl_multi_init();
  if (body->easy == NULL || body->multi == NULL) {
    curl_free_body(body);
    qa_llm_error_set(error, "curl unavailable");
    return false;
  }
  CURL *easy = body->easy;
  curl_easy_setopt(easy, CURLOPT_URL, url);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_write);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, body);
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, curl_header);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA, body);
  if (init->redirect_error) {
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
  }
  const char *method = init->method != NULL ? init->method : "GET";
  if (strcmp(method, "POST") == 0) {
    curl_easy_setopt(easy, CURLOPT_POST, 1L);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, init->body != NULL ? init->body : "");
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)(init->body != NULL ? init->body_length : 0));
  } else if (strcmp(method, "GET") != 0) {
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method);
  }
  for (size_t i = 0; i < init->header_count; i++) {
    qa_llm_strbuf line;
    qa_llm_strbuf_init(&line);
    qa_llm_strbuf_appendf(&line, "%s: %s", init->headers[i].name, init->headers[i].value);
    body->headers = curl_slist_append(body->headers, qa_llm_strbuf_text(&line));
    qa_llm_strbuf_free(&line);
  }
  if (body->headers != NULL) curl_easy_setopt(easy, CURLOPT_HTTPHEADER, body->headers);
  curl_multi_add_handle(body->multi, easy);
  /* Run until response headers are known (fetch resolves then). */
  while (!body->headers_done && !body->transfer_done) {
    if (qa_llm_signal_is_aborted(body->signal)) break;
    curl_pump(body, 100);
  }
  long status = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
  /* fetch redirect: "error" rejects redirect responses as network errors. */
  if (init->redirect_error && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
    curl_free_body(body);
    qa_llm_error_set(error, "curl transfer failed");
    return false;
  }
  if (body->failed && status == 0) {
    curl_free_body(body);
    qa_llm_error_set(error, "curl transfer failed");
    return false;
  }
  out->status = (int)status;
  out->has_body = true;
  out->read = curl_read;
  out->cancel = curl_cancel;
  out->free_ctx = curl_free_body;
  out->body_ctx = body;
  return true;
}
