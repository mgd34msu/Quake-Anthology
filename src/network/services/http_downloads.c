/* Port of src/network/services/http-downloads.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/services/http_downloads.h"
#include "qa/foundation/support.h"
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = '\0'; } return p; }
static bool is_safe_d(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }

/* --- Minimal URL handling: scheme://host[:port]/path --- */
#define RANGE_THRESHOLD (1024.0 * 1024.0)
struct url_parts { char *scheme, *host, *path, *origin; int port; bool has_userinfo; };
static void url_free(struct url_parts *u) { free(u->scheme); free(u->host); free(u->path); free(u->origin); memset(u, 0, sizeof *u); }
static int url_parse(const char *url, struct url_parts *out) {
  memset(out, 0, sizeof *out);
  const char *sep = strstr(url, "://");
  if (!sep) return 0;
  out->scheme = dupn(url, (size_t)(sep - url));
  const char *rest = sep + 3;
  const char *slash = strchr(rest, '/');
  const char *auth_end = slash ? slash : url + strlen(url);
  const char *at = NULL;
  for (const char *p = rest; p < auth_end; p++) if (*p == '@') at = p;
  const char *hostport = at ? at + 1 : rest;
  out->has_userinfo = at != NULL;
  const char *colon = NULL;
  for (const char *p = hostport; p < auth_end; p++) if (*p == ':') colon = p;
  /* IPv6 literals keep brackets in host. */
  if (hostport[0] == '[') {
    const char *closeb = strchr(hostport, ']');
    if (closeb && closeb < auth_end) {
      out->host = dupn(hostport + 1, (size_t)(closeb - hostport - 1));
      out->port = (closeb + 1 < auth_end && closeb[1] == ':') ? atoi(closeb + 2) : 0;
    }
  } else if (colon) {
    out->host = dupn(hostport, (size_t)(colon - hostport));
    out->port = atoi(colon + 1);
  } else out->host = dupn(hostport, (size_t)(auth_end - hostport));
  if (!out->port) out->port = strcmp(out->scheme, "https") == 0 ? 443 : 80;
  out->path = dupstr(slash ? slash : "/");
  size_t need = strlen(out->scheme) + strlen(out->host) + 32;
  out->origin = malloc(need);
  snprintf(out->origin, need, "%s://%s:%d", out->scheme, out->host, out->port);
  return 1;
}
static char *url_resolve(const char *base, const char *location) {
  if (strstr(location, "://")) return dupstr(location);
  struct url_parts b;
  if (!url_parse(base, &b)) return dupstr(location);
  char *out = NULL;
  if (location[0] == '/') {
    size_t need = strlen(b.origin) + strlen(location) + 1;
    out = malloc(need); snprintf(out, need, "%s%s", b.origin, location);
  } else {
    /* Relative to base directory. */
    const char *last = strrchr(b.path, '/');
    size_t dirlen = last ? (size_t)(last - b.path) + 1 : 0;
    size_t need = strlen(b.origin) + dirlen + strlen(location) + 1;
    out = malloc(need);
    snprintf(out, need, "%s%.*s%s", b.origin, (int)dirlen, b.path, location);
  }
  url_free(&b);
  return out;
}

/* --- HTTP response --- */
struct http_response {
  int status; char *headers; uint8_t *body; size_t body_len, body_cap;
  int fd; bool headers_done; size_t content_length; bool has_content_length; size_t received;
};
static const char *header_get(const char *headers, const char *name, char *buf, size_t bufsz) {
  size_t nl = strlen(name);
  const char *p = headers;
  while (p && *p) {
    const char *eol = strstr(p, "\r\n");
    size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
    if (linelen > nl && p[nl] == ':') {
      bool match = true;
      for (size_t i = 0; i < nl; i++) if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) match = false;
      if (match) {
        const char *v = p + nl + 1;
        while (v < p + linelen && (*v == ' ' || *v == '\t')) v++;
        size_t vn = (size_t)((p + linelen) - v);
        if (vn >= bufsz) vn = bufsz - 1;
        memcpy(buf, v, vn); buf[vn] = '\0'; return buf;
      }
    }
    if (!eol) break;
    p = eol + 2;
  }
  return NULL;
}
/* One request, no redirect following, through libcurl (fetch with redirect: 'manual').
 * Returns the status, -1 on a transport error before a status, -3 when aborted. A failure
 * after the headers keeps the status, sets *truncated, and leaves delivered bytes delivered,
 * as a TS body reader that throws after earlier chunks. headers_out takes the header block. */
typedef void (*http_body_fn)(void *ctx, const uint8_t *chunk, size_t length);
struct http_transfer {
  http_body_fn on_body; void *body_ctx;
  char *headers; size_t header_length;
  const bool *aborted;
};
static size_t http_on_header(char *data, size_t size, size_t count, void *ctx) {
  struct http_transfer *t = ctx;
  size_t n = size * count;
  /* A status line starts a new header block. */
  if (n >= 5 && strncmp(data, "HTTP/", 5) == 0) t->header_length = 0;
  char *grown = realloc(t->headers, t->header_length + n + 1);
  if (!grown) return 0;
  t->headers = grown;
  memcpy(t->headers + t->header_length, data, n);
  t->header_length += n;
  t->headers[t->header_length] = '\0';
  return n;
}
static size_t http_on_body(char *data, size_t size, size_t count, void *ctx) {
  struct http_transfer *t = ctx;
  if (t->on_body) t->on_body(t->body_ctx, (const uint8_t *)data, size * count);
  return size * count;
}
static int http_on_progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
  (void)a; (void)b; (void)c; (void)d;
  struct http_transfer *t = ctx;
  return t->aborted && *t->aborted ? 1 : 0;
}
static int http_once(const char *url, const char *method, const char *extra_headers, http_body_fn on_body,
                     void *body_ctx, char **headers_out, const bool *aborted, bool *truncated) {
  static bool initialized = false;
  if (!initialized) { curl_global_init(CURL_GLOBAL_DEFAULT); initialized = true; }
  *truncated = false;
  CURL *easy = curl_easy_init();
  if (!easy) return -1;
  struct http_transfer t = {on_body, body_ctx, NULL, 0, aborted};
  struct curl_slist *list = NULL;
  /* extra_headers is CRLF-terminated lines. Accept-Encoding goes through curl so a server
   * that ignores identity still yields decoded bytes, as fetch does. */
  bool identity = false;
  for (const char *line = extra_headers ? extra_headers : ""; *line;) {
    const char *end = strstr(line, "\r\n");
    size_t n = end ? (size_t)(end - line) : strlen(line);
    char *text = dupn(line, n);
    if (strncasecmp(text, "Accept-Encoding:", 16) == 0) identity = true;
    else list = curl_slist_append(list, text);
    free(text);
    line += n + (end ? 2 : 0);
  }
  curl_easy_setopt(easy, CURLOPT_URL, url);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 30000L);
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, identity ? "identity" : "");
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, http_on_header);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA, &t);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, http_on_body);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &t);
  curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, http_on_progress);
  curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &t);
  if (strcmp(method, "HEAD") == 0) curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
  CURLcode code = curl_easy_perform(easy);
  long status = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(list);
  curl_easy_cleanup(easy);
  if (code == CURLE_ABORTED_BY_CALLBACK) { free(t.headers); return -3; }
  if (status <= 0 || !t.headers) { free(t.headers); return -1; }
  *truncated = code != CURLE_OK;
  *headers_out = t.headers;
  return (int)status;
}
static bool url_ok(const char *url) {
  struct url_parts u;
  if (!url_parse(url, &u)) return false;
  bool ok = (strcmp(u.scheme, "http") == 0 || strcmp(u.scheme, "https") == 0) && !u.has_userinfo;
  url_free(&u); return ok;
}
/* Buffered follow: collects full body (downloads are bounded by sink limits at queue level). */
struct collect { uint8_t *data; size_t len, cap; bool oom, truncated; bool bounded, over; size_t limit; };
static void collect_body(void *ctx, const uint8_t *chunk, size_t length) {
  struct collect *c = ctx;
  /* A bounded read stops keeping bytes once a chunk would pass its limit (TS cancels the
   * reader there); the rest of the body is drained unread. */
  if (c->over) return;
  if (c->bounded && length > c->limit - c->len) { c->over = true; return; }
  if (c->len + length > c->cap) {
    size_t nc = c->cap ? c->cap : 16384;
    while (nc < c->len + length) nc *= 2;
    uint8_t *nd = realloc(c->data, nc);
    if (!nd) { c->oom = true; return; }
    c->data = nd; c->cap = nc;
  }
  memcpy(c->data + c->len, chunk, length); c->len += length;
}
static int http_fetch_buffered(const char *url, const char *method, const char *extra_headers,
                               struct collect *out_body, char **headers_out, const bool *aborted,
                               bool bounded, size_t limit) {
  char *current = dupstr(url);
  struct url_parts orig; url_parse(url, &orig);
  int result = -1;
  for (int redirects = 0;; redirects++) {
    if (!url_ok(current)) { result = -4; break; }
    struct collect body; memset(&body, 0, sizeof body);
    body.bounded = bounded; body.limit = limit;
    char *headers = NULL;
    bool truncated = false;
    int status = http_once(current, method, extra_headers, collect_body, &body, &headers, aborted, &truncated);
    body.truncated = truncated;
    if (status < 0) { free(body.data); free(headers); result = status; break; }
    if (!(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
      *out_body = body; *headers_out = headers; result = status; break;
    }
    free(body.data);
    if (aborted && *aborted) { free(headers); result = -3; break; }
    char loc[2048];
    const char *location = headers ? header_get(headers, "location", loc, sizeof loc) : NULL;
    if (redirects == 4 || !location) { free(headers); result = -4; break; }
    char *next = url_resolve(current, location);
    struct url_parts np; url_parse(next, &np);
    bool same_origin = np.origin && orig.origin && strcmp(np.origin, orig.origin) == 0;
    if (!same_origin || np.has_userinfo) { url_free(&np); free(next); free(headers); result = -4; break; }
    url_free(&np); free(headers); free(current); current = next;
  }
  free(current); url_free(&orig);
  return result;
}
/* /^bytes (\d+)-(\d+)\/(\d+)$/ with each capture compared by Number(): a string
 * compare against "%.0f" text rejected "bytes 07-9/10", which the TS accepts. */
static bool content_range_digits(const char **cursor, char stop, double *out) {
  const char *p = *cursor, *start = p;
  while (*p >= '0' && *p <= '9') p++;
  if (p == start || *p != stop) return false;
  *out = qa_js_parse_int(start, 10);
  *cursor = stop == '\0' ? p : p + 1;
  return true;
}
static bool content_range_matches(const char *range, double start, double end, double total) {
  if (strncmp(range, "bytes ", 6) != 0) return false;
  const char *p = range + 6;
  double first, last, size;
  return content_range_digits(&p, '-', &first) && content_range_digits(&p, '/', &last) &&
         content_range_digits(&p, '\0', &size) && first == start && last == end && size == total;
}
static bool content_length_of(const char *headers, double *out) {
  char buf[128];
  if (!header_get(headers, "content-length", buf, sizeof buf)) return false;
  for (const char *p = buf; *p; p++) if (!isdigit((unsigned char)*p)) return false;
  double v = strtod(buf, NULL);
  if (!is_safe_d(v)) return false;
  *out = v; return true;
}
static bool identity_encoding(const char *headers) {
  char buf[128];
  if (!header_get(headers, "content-encoding", buf, sizeof buf)) return true;
  for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
  return strcmp(buf, "identity") == 0;
}
uint8_t *qa_fetch_http_download_metadata(const char *url, double maximum_bytes, const bool *aborted, size_t *out_len) {
  if (!is_safe_d(maximum_bytes) || maximum_bytes < 0) qa_range_error("Invalid HTTP metadata size limit");
  *out_len = 0;
  struct url_parts u; url_parse(url, &u);
  bool proto_ok = u.scheme && (strcmp(u.scheme, "http") == 0 || strcmp(u.scheme, "https") == 0);
  url_free(&u);
  if (!proto_ok) return NULL;
  struct collect body; memset(&body, 0, sizeof body);
  char *headers = NULL;
  int status = http_fetch_buffered(url, "GET", "Accept-Encoding: identity\r\n", &body, &headers, aborted,
                                   true, maximum_bytes > (double)SIZE_MAX ? SIZE_MAX : (size_t)maximum_bytes);
  free(headers);
  /* signal.throwIfAborted() after a read, a status other than 200, an oversized body or a
   * truncated transfer all settle as null. */
  if (status != 200 || body.over || body.oom || body.truncated || (aborted && *aborted)) { free(body.data); return NULL; }
  *out_len = body.len;
  return body.data ? body.data : malloc(1);
}

/* --- Queue ---
 * The TS queue runs transfers as concurrent promises that stream response bodies into the
 * staging sink. Here every request is a curl easy handle on one multi handle per queue, and
 * the async flow is an explicitly pumped state machine: qa_http_download_queue_poll() runs the
 * TS schedule()/pump() step, drives the multi handle once, and advances each transfer whose
 * request completed. Bodies stream into the sink from the write callback; nothing is buffered.
 * Range spans of one file run concurrently; up to `concurrency` files run at once. */
enum { ENTRY_PENDING, ENTRY_RUNNING, ENTRY_DONE };
enum { PHASE_PROBE, PHASE_RANGES, PHASE_WHOLE };
enum { OP_HEAD, OP_RANGE, OP_WHOLE };
struct http_transfer_state;
struct http_op {
  struct http_transfer_state *owner; int kind; size_t span;
  qa_http_download_queue *queue; bool dying; /* curl may call back while a handle is removed */
  CURL *easy; struct curl_slist *list;
  char *url; int redirects;
  char *headers; size_t header_length;
  long status; bool started, discard;
  double start; /* range: first requested byte */
  char error[QA_CAUGHT_MESSAGE_CAPACITY]; bool has_error, unsupported, transport;
};
struct http_entry;
struct http_transfer_state {
  struct http_entry *entry; long long generation;
  int phase;
  struct http_op *ops[8]; size_t op_count;
  qa_download_sink sink; bool have_sink, published;
  double probe_total; char etag[512]; bool retry_identity;
  qa_download_span *spans; size_t span_count;
  double *span_received; int *span_attempt; bool *span_done;
  bool has_primary, primary_unsupported; char primary[QA_CAUGHT_MESSAGE_CAPACITY];
  bool has_total; double total;
};
struct http_entry {
  char *path, *url, *kind;
  qa_download_expectation expected; char *digest_copy;
  qa_http_validate_fn validate; void *validate_ctx;
  int state; bool checked;
  double received; bool has_total; double total;
  const char *result_kind; char *digest, *reason;
  bool aborted;
  struct http_transfer_state *transfer;
};
struct qa_http_download_queue {
  qa_http_queue_options options;
  double concurrency, range_streams;
  struct http_entry **entries; size_t count, capacity;
  double active; bool package_active, closed; long long generation;
  double total_received; /* TS totalReceived, against options.total_budget_bytes */
  CURLM *multi;
};
void qa_http_download_result_destroy(qa_http_download_result *r) { free(r->digest); free(r->reason); memset(r, 0, sizeof *r); }
void qa_http_download_queue_init(qa_http_download_queue *q, qa_http_queue_options options) {
  double concurrency = options.has_concurrency ? options.concurrency : 2;
  if (concurrency != trunc(concurrency) || concurrency < 1 || concurrency > 4)
    qa_range_error("HTTP download concurrency must be between 1 and 4");
  double streams = options.has_range_streams ? options.range_streams : 4;
  if (!isfinite(streams)) qa_range_error("Invalid HTTP range stream count");
  memset(q, 0, sizeof *q);
  q->options = options; q->concurrency = concurrency;
  double t = trunc(streams); if (t < 1) t = 1; if (t > 8) t = 8;
  q->range_streams = t;
}
static void op_destroy(qa_http_download_queue *q, struct http_op *op) {
  if (!op) return;
  op->dying = true;
  if (op->easy) { if (q->multi) curl_multi_remove_handle(q->multi, op->easy); curl_easy_cleanup(op->easy); }
  curl_slist_free_all(op->list);
  free(op->url); free(op->headers); free(op);
}
static void transfer_free(qa_http_download_queue *q, struct http_transfer_state *t) {
  for (size_t i = 0; i < t->op_count; i++) op_destroy(q, t->ops[i]);
  t->op_count = 0;
  free(t->spans); free(t->span_received); free(t->span_attempt); free(t->span_done);
  free(t);
}
static void entry_free(struct http_entry *e) {
  free(e->path); free(e->url); free(e->kind);
  free(e->digest_copy); free(e->digest); free(e->reason); free(e);
}
static void sink_close_quiet(struct http_transfer_state *t);
void qa_http_download_queue_destroy(qa_http_download_queue *q) {
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    if (e->transfer) { sink_close_quiet(e->transfer); transfer_free(q, e->transfer); }
    entry_free(e);
  }
  free(q->entries);
  if (q->multi) curl_multi_cleanup(q->multi);
  memset(q, 0, sizeof *q);
}
static bool same_expectation(qa_download_expectation a, qa_download_expectation b) {
  if (a.is_protocol) return b.is_protocol && a.maximum_bytes == b.maximum_bytes;
  return !b.is_protocol && a.byte_length == b.byte_length && strcmp(a.digest, b.digest) == 0;
}
static struct http_entry *queue_find(qa_http_download_queue *q, const char *path) {
  for (size_t i = 0; i < q->count; i++) if (strcmp(q->entries[i]->path, path) == 0) return q->entries[i];
  return NULL;
}
void qa_http_download_queue_enqueue(qa_http_download_queue *q, qa_http_download_request request) {
  if (q->closed) return;
  q->options.assert_current(q->options.ctx);
  size_t pc = 0; char **parts = qa_contained_file_parts(request.path, &pc);
  qa_contained_file_parts_free(parts, pc);
  struct url_parts u; url_parse(request.url, &u);
  bool proto_ok = u.scheme && (strcmp(u.scheme, "http") == 0 || strcmp(u.scheme, "https") == 0);
  url_free(&u);
  if (!proto_ok) qa_range_error("Download URL requires HTTP or HTTPS");
  struct http_entry *existing = queue_find(q, request.path);
  if (existing) {
    if (strcmp(existing->url, request.url) != 0 || strcmp(existing->kind, request.kind) != 0
        || !same_expectation(existing->expected, request.expected) || existing->validate != request.validate)
      qa_range_error("Conflicting HTTP download identity for one destination");
    return;
  }
  if (q->count == q->capacity) {
    size_t nc = q->capacity ? q->capacity * 2 : 8;
    struct http_entry **grown = realloc(q->entries, nc * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    q->entries = grown; q->capacity = nc;
  }
  struct http_entry *e = calloc(1, sizeof *e);
  if (!e) qa_range_error("out of memory");
  e->path = dupstr(request.path); e->url = dupstr(request.url); e->kind = dupstr(request.kind);
  e->expected = request.expected;
  if (!request.expected.is_protocol && request.expected.digest) {
    e->digest_copy = dupstr(request.expected.digest); e->expected.digest = e->digest_copy;
  }
  e->validate = request.validate; e->validate_ctx = request.validate_ctx;
  e->state = ENTRY_PENDING;
  q->entries[q->count++] = e;
}
static void queue_settle(struct http_entry *e, const char *kind, char *digest, const char *reason) {
  e->state = ENTRY_DONE; e->result_kind = kind; e->digest = digest;
  e->reason = reason ? dupstr(reason) : NULL;
}
/* TS current(): throws when the epoch retired, the host is not current, or the entry aborted. */
typedef struct current_scope { qa_http_download_queue *q; struct http_entry *e; long long generation; } current_scope;
static void current_body(void *ctx) {
  current_scope *scope = ctx;
  if (scope->q->closed || scope->generation != scope->q->generation) qa_range_error("HTTP download epoch retired");
  scope->q->options.assert_current(scope->q->options.ctx);
  if (scope->e->aborted) qa_raise("AbortError", 0, 0, "This operation was aborted");
}
/* NULL when current, else the message TS current() throws. */
static const char *transfer_current(qa_http_download_queue *q, struct http_transfer_state *t, qa_caught_error *caught) {
  current_scope scope = {q, t->entry, t->generation};
  return qa_try(current_body, &scope, caught) ? NULL : caught->message;
}
typedef struct report_scope { qa_http_download_queue *q; struct http_entry *e; double received; bool has_total; double total; } report_scope;
static void report_body(void *ctx) {
  report_scope *scope = ctx;
  qa_http_download_queue *q = scope->q;
  q->total_received += qa_js_max(0, scope->received - scope->e->received);
  if (q->options.has_total_budget_bytes && q->total_received > q->options.total_budget_bytes)
    qa_range_error("HTTP download quota exceeded");
  scope->e->received = scope->received; scope->e->has_total = scope->has_total; scope->e->total = scope->total;
  scope->q->options.progress(scope->q->options.ctx, scope->e->path, scope->received, scope->has_total, scope->total);
}
static bool queue_report(qa_http_download_queue *q, struct http_entry *e, double received, bool has_total, double total,
    qa_caught_error *caught) {
  report_scope scope = {q, e, received, has_total, total};
  return qa_try(report_body, &scope, caught);
}
/* /^"[\x21\x23-\x7e\x80-\xff]*"$/ */
static bool strong_etag(const char *etag) {
  size_t n = strlen(etag);
  if (n < 2 || etag[0] != '"' || etag[n - 1] != '"') return false;
  for (size_t i = 1; i + 1 < n; i++) {
    unsigned char c = (unsigned char)etag[i];
    if (c < 0x21 || c == 0x22 || c == 0x7f) return false;
  }
  return true;
}
static bool is_redirect(long status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }
static void op_fail(struct http_op *op, const char *message) {
  if (op->has_error) return;
  op->has_error = true;
  snprintf(op->error, sizeof op->error, "%s", message);
}
static size_t op_on_header(char *data, size_t size, size_t count, void *ctx) {
  struct http_op *op = ctx;
  size_t n = size * count;
  if (n >= 5 && strncmp(data, "HTTP/", 5) == 0) op->header_length = 0;
  char *grown = realloc(op->headers, op->header_length + n + 1);
  if (!grown) return 0;
  op->headers = grown;
  memcpy(op->headers + op->header_length, data, n);
  op->header_length += n;
  op->headers[op->header_length] = '\0';
  return n;
}
static bool op_validate(qa_http_download_queue *q, struct http_op *op);
static bool op_chunk(qa_http_download_queue *q, struct http_op *op, const uint8_t *data, size_t length);
static size_t op_on_body(char *data, size_t size, size_t count, void *ctx) {
  struct http_op *op = ctx;
  size_t n = size * count;
  if (op->dying) return 0;
  qa_http_download_queue *q = op->queue;
  if (!op->started) {
    op->started = true;
    curl_easy_getinfo(op->easy, CURLINFO_RESPONSE_CODE, &op->status);
    if (is_redirect(op->status)) op->discard = true;
    else if (!op_validate(q, op)) return 0;
  }
  if (op->discard) return n;
  return op_chunk(q, op, (const uint8_t *)data, n) ? n : 0;
}
static int op_on_progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
  (void)a; (void)b; (void)c; (void)d;
  struct http_op *op = ctx;
  if (op->dying) return 1;
  qa_http_download_queue *q = op->queue;
  struct http_transfer_state *t = op->owner;
  return q->closed || t->generation != q->generation || t->entry->aborted ? 1 : 0;
}
static void op_start(qa_http_download_queue *q, struct http_op *op) {
  static bool initialized = false;
  if (!initialized) { curl_global_init(CURL_GLOBAL_DEFAULT); initialized = true; }
  if (!q->multi) { q->multi = curl_multi_init(); if (!q->multi) qa_range_error("out of memory"); }
  if (op->easy) { curl_multi_remove_handle(q->multi, op->easy); curl_easy_cleanup(op->easy); }
  curl_slist_free_all(op->list); op->list = NULL;
  free(op->headers); op->headers = NULL; op->header_length = 0;
  op->status = 0; op->started = false; op->discard = false;
  op->has_error = false; op->unsupported = false; op->transport = false; op->error[0] = '\0';
  op->easy = curl_easy_init();
  if (!op->easy) qa_range_error("out of memory");
  struct http_transfer_state *t = op->owner;
  char line[700];
  /* Accept-Encoding: identity on every request but metadata; If-Range / If-Match pin the probe's ETag. */
  if (op->kind == OP_RANGE) {
    snprintf(line, sizeof line, "Range: bytes=%s-%s", QA_JS_NUMBER(op->start), QA_JS_NUMBER(t->spans[op->span].end));
    op->list = curl_slist_append(op->list, line);
    snprintf(line, sizeof line, "If-Range: %s", t->etag);
    op->list = curl_slist_append(op->list, line);
  } else if (op->kind == OP_WHOLE && t->retry_identity) {
    snprintf(line, sizeof line, "If-Match: %s", t->etag);
    op->list = curl_slist_append(op->list, line);
  }
  curl_easy_setopt(op->easy, CURLOPT_URL, op->url);
  curl_easy_setopt(op->easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(op->easy, CURLOPT_FOLLOWLOCATION, 0L);
  /* IDLE_TIMEOUT_MILLISECONDS covers headers and each wait for the next chunk, not the transfer. */
  curl_easy_setopt(op->easy, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
  curl_easy_setopt(op->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(op->easy, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(op->easy, CURLOPT_HTTPHEADER, op->list);
  curl_easy_setopt(op->easy, CURLOPT_ACCEPT_ENCODING, "identity");
  curl_easy_setopt(op->easy, CURLOPT_HEADERFUNCTION, op_on_header);
  curl_easy_setopt(op->easy, CURLOPT_HEADERDATA, op);
  curl_easy_setopt(op->easy, CURLOPT_WRITEFUNCTION, op_on_body);
  curl_easy_setopt(op->easy, CURLOPT_WRITEDATA, op);
  curl_easy_setopt(op->easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(op->easy, CURLOPT_XFERINFOFUNCTION, op_on_progress);
  curl_easy_setopt(op->easy, CURLOPT_XFERINFODATA, op);
  curl_easy_setopt(op->easy, CURLOPT_PRIVATE, op);
  if (op->kind == OP_HEAD) curl_easy_setopt(op->easy, CURLOPT_NOBODY, 1L);
  curl_multi_add_handle(q->multi, op->easy);
}
static struct http_op *op_new(qa_http_download_queue *q, struct http_transfer_state *t, int kind, size_t span) {
  struct http_op *op = calloc(1, sizeof *op);
  if (!op) qa_range_error("out of memory");
  op->owner = t; op->kind = kind; op->span = span; op->queue = q;
  op->url = dupstr(t->entry->url);
  if (kind == OP_RANGE) op->start = t->spans[span].start;
  t->ops[t->op_count++] = op;
  op_start(q, op);
  return op;
}
static void op_remove(qa_http_download_queue *q, struct http_transfer_state *t, struct http_op *op) {
  for (size_t i = 0; i < t->op_count; i++)
    if (t->ops[i] == op) {
      memmove(t->ops + i, t->ops + i + 1, (t->op_count - i - 1) * sizeof *t->ops);
      t->op_count--;
      break;
    }
  op_destroy(q, op);
}
/* DownloadSink.create / createRanged raise inside the TS transfer try. */
typedef struct sink_create_scope { qa_http_download_queue *q; struct http_transfer_state *t; bool ranged; } sink_create_scope;
static void sink_create_body(void *ctx) {
  sink_create_scope *scope = ctx;
  struct http_transfer_state *t = scope->t;
  int ok = scope->ranged
      ? qa_download_sink_create_ranged(&t->sink, scope->q->options.root, t->entry->path, t->entry->expected,
          t->probe_total, t->spans, t->span_count)
      : qa_download_sink_create(&t->sink, scope->q->options.root, t->entry->path, t->entry->expected);
  if (!ok) qa_range_error("out of memory");
  t->have_sink = true;
}
/* Header checks TS makes before reading a body: receiveRange's and openResponse's. */
static bool op_validate(qa_http_download_queue *q, struct http_op *op) {
  struct http_transfer_state *t = op->owner;
  qa_caught_error caught;
  if (op->kind == OP_HEAD) return true;
  const char *retired = transfer_current(q, t, &caught);
  if (retired) { op_fail(op, retired); return false; }
  const char *headers = op->headers ? op->headers : "";
  char etag_text[512];
  const char *etag = header_get(headers, "etag", etag_text, sizeof etag_text);
  if (op->kind == OP_RANGE) {
    const qa_download_span *span = &t->spans[op->span];
    if (etag && strcmp(etag, t->etag) != 0) { op_fail(op, "HTTP ranged representation changed"); return false; }
    if (op->status == 200 || op->status == 416) {
      op->unsupported = true;
      op_fail(op, "HTTP server rejected byte ranges");
      return false;
    }
    char range_text[256], length_text[64];
    const char *range = header_get(headers, "content-range", range_text, sizeof range_text);
    double length = 0;
    bool has_length = header_get(headers, "content-length", length_text, sizeof length_text) != NULL;
    bool length_ok = content_length_of(headers, &length);
    if (op->status != 206 || !etag || !identity_encoding(headers) || !range
        || !content_range_matches(range, op->start, span->end, t->probe_total)
        || (has_length && (!length_ok || length != span->end - op->start + 1))) {
      op_fail(op, "Invalid HTTP byte range response");
      return false;
    }
    return true;
  }
  /* openResponse */
  if (op->status != 200 || (t->retry_identity && (!etag || strcmp(etag, t->etag) != 0 || !identity_encoding(headers)))) {
    char text[64];
    snprintf(text, sizeof text, "HTTP download status %ld", op->status);
    op_fail(op, text);
    return false;
  }
  double total = 0;
  t->has_total = identity_encoding(headers) && content_length_of(headers, &total);
  t->total = total;
  sink_create_scope create = {q, t, false};
  if (!qa_try(sink_create_body, &create, &caught)) { op_fail(op, caught.message); return false; }
  if (!queue_report(q, t->entry, 0, t->has_total, t->total, &caught)) { op_fail(op, caught.message); return false; }
  return true;
}
typedef struct append_scope { struct http_transfer_state *t; struct http_op *op; const uint8_t *data; size_t length; } append_scope;
static void append_body(void *ctx) {
  append_scope *scope = ctx;
  struct http_transfer_state *t = scope->t;
  if (scope->op->kind == OP_RANGE) {
    const qa_download_span *span = &t->spans[scope->op->span];
    double width = span->end - span->start + 1;
    if ((double)scope->length > width - t->span_received[scope->op->span]) qa_range_error("HTTP byte range overflow");
    qa_download_sink_append_range(&t->sink, span->start, scope->data, scope->length);
    t->span_received[scope->op->span] += (double)scope->length;
  } else qa_download_sink_append(&t->sink, scope->data, scope->length);
}
static bool op_chunk(qa_http_download_queue *q, struct http_op *op, const uint8_t *data, size_t length) {
  struct http_transfer_state *t = op->owner;
  qa_caught_error caught;
  if (op->kind == OP_HEAD) return true;
  const char *retired = transfer_current(q, t, &caught);
  if (retired) { op_fail(op, retired); return false; }
  append_scope scope = {t, op, data, length};
  if (!qa_try(append_body, &scope, &caught)) { op_fail(op, caught.message); return false; }
  double total = op->kind == OP_RANGE ? t->probe_total : t->total;
  bool has_total = op->kind == OP_RANGE ? true : t->has_total;
  if (!queue_report(q, t->entry, qa_download_sink_byte_length(&t->sink), has_total, total, &caught)) {
    op_fail(op, caught.message);
    return false;
  }
  return true;
}
/* downloadResponse's redirect step after a 3xx; false with an error when it throws. */
static bool op_redirect(qa_http_download_queue *q, struct http_op *op) {
  char location_text[2048];
  const char *location = header_get(op->headers ? op->headers : "", "location", location_text, sizeof location_text);
  if (op->redirects == 4 || !location) { op_fail(op, "HTTP download redirect limit or missing location"); return false; }
  char *next = url_resolve(op->url, location);
  struct url_parts np, origin;
  url_parse(next ? next : "", &np);
  url_parse(op->owner->entry->url, &origin);
  bool same = np.origin && origin.origin && strcmp(np.origin, origin.origin) == 0 && !np.has_userinfo;
  url_free(&np); url_free(&origin);
  if (!same) { free(next); op_fail(op, "HTTP download redirect left advertised origin"); return false; }
  if (!url_ok(next)) { free(next); op_fail(op, "Invalid download URL"); return false; }
  free(op->url);
  op->url = next;
  op->redirects++;
  op_start(q, op);
  return true;
}
static void sink_close_quiet(struct http_transfer_state *t) {
  if (!t->have_sink) return;
  t->have_sink = false;
  qa_download_sink_destroy(&t->sink);
}
typedef struct sink_scope { struct http_transfer_state *t; } sink_scope;
static void sink_close_body(void *ctx) { sink_close_quiet(((sink_scope *)ctx)->t); }
/* TS transfer's catch: close the sink, then cancelled, failed or fallback. */
static void transfer_fail(qa_http_download_queue *q, struct http_transfer_state *t, const char *reason, bool fatal) {
  struct http_entry *e = t->entry;
  char text[QA_CAUGHT_MESSAGE_CAPACITY];
  snprintf(text, sizeof text, "%s", reason);
  for (size_t i = 0; i < t->op_count; i++) op_destroy(q, t->ops[i]);
  t->op_count = 0;
  sink_scope scope = {t};
  qa_caught_error caught;
  if (!qa_try(sink_close_body, &scope, &caught)) queue_settle(e, "failed", NULL, "HTTP download and staged cleanup failed");
  else if (q->closed || t->generation != q->generation || e->aborted) queue_settle(e, "cancelled", NULL, NULL);
  else queue_settle(e, t->published || fatal ? "failed" : "fallback", NULL, text);
}
typedef struct inspect_scope { struct http_transfer_state *t; } inspect_scope;
static void inspect_body(void *ctx) {
  struct http_transfer_state *t = ((inspect_scope *)ctx)->t;
  qa_download_sink_inspect_staged(&t->sink, t->entry->validate, t->entry->validate_ctx);
}
typedef struct refresh_scope { qa_http_download_queue *q; struct http_entry *e; } refresh_scope;
static void refresh_body(void *ctx) {
  refresh_scope *scope = ctx;
  scope->q->options.refresh_package(scope->q->options.ctx, scope->e->path);
}
static void transfer_publish(qa_http_download_queue *q, struct http_transfer_state *t) {
  struct http_entry *e = t->entry;
  qa_caught_error caught;
  const char *failure = transfer_current(q, t, &caught);
  if (failure) { transfer_fail(q, t, failure, false); return; }
  if (e->validate) {
    inspect_scope scope = {t};
    if (!qa_try(inspect_body, &scope, &caught)) { transfer_fail(q, t, caught.message, false); return; }
    if ((failure = transfer_current(q, t, &caught))) { transfer_fail(q, t, failure, false); return; }
  }
  const char *error = NULL;
  char *digest = qa_download_sink_try_finish(&t->sink, &error);
  if (!digest) { transfer_fail(q, t, error ? error : "HTTP download failed", false); return; }
  t->published = true;
  sink_close_quiet(t);
  if (strcmp(e->kind, "package") == 0) {
    refresh_scope scope = {q, e};
    if (!qa_try(refresh_body, &scope, &caught)) { free(digest); transfer_fail(q, t, caught.message, false); return; }
    if ((failure = transfer_current(q, t, &caught))) { free(digest); transfer_fail(q, t, failure, false); return; }
  }
  queue_settle(e, "downloaded", digest, NULL);
}
static void transfer_whole(qa_http_download_queue *q, struct http_transfer_state *t) {
  t->phase = PHASE_WHOLE;
  op_new(q, t, OP_WHOLE, 0);
}
/* After the HEAD: probeRanges' checks, then byteSpans and every range at once. */
static void transfer_probed(qa_http_download_queue *q, struct http_transfer_state *t, struct http_op *op, bool ok) {
  struct http_entry *e = t->entry;
  qa_caught_error caught;
  const char *headers = op->headers ? op->headers : "";
  double total = 0;
  bool has_total = ok && content_length_of(headers, &total);
  char accept[64], stored[512];
  const char *accept_value = ok ? header_get(headers, "accept-ranges", accept, sizeof accept) : NULL;
  const char *etag_value = ok ? header_get(headers, "etag", stored, sizeof stored) : NULL;
  bool ranges = false;
  if (accept_value) {
    char lowered[64];
    size_t i = 0;
    for (; accept_value[i] && i + 1 < sizeof lowered; i++) lowered[i] = (char)tolower((unsigned char)accept_value[i]);
    lowered[i] = '\0';
    ranges = strcmp(lowered, "bytes") == 0;
  }
  bool usable = ok && op->status == 200 && identity_encoding(headers) && has_total && total > RANGE_THRESHOLD && ranges
      && etag_value && strong_etag(etag_value);
  if (usable) snprintf(t->etag, sizeof t->etag, "%s", etag_value);
  op_remove(q, t, op);
  const char *failure = transfer_current(q, t, &caught);
  if (failure) { transfer_fail(q, t, failure, false); return; }
  if (!usable) { transfer_whole(q, t); return; }
  double limit = e->expected.is_protocol ? e->expected.maximum_bytes : e->expected.byte_length;
  if (total > limit || (!e->expected.is_protocol && total != e->expected.byte_length)) {
    transfer_fail(q, t, "HTTP range size differs from expected bounds", false);
    return;
  }
  t->probe_total = total;
  double width = ceil(total / q->range_streams);
  size_t count = 0;
  for (double start = 0; start < total; start += width) count++;
  t->spans = malloc(count * sizeof *t->spans);
  t->span_received = calloc(count, sizeof *t->span_received);
  t->span_attempt = calloc(count, sizeof *t->span_attempt);
  t->span_done = calloc(count, sizeof *t->span_done);
  if (!t->spans || !t->span_received || !t->span_attempt || !t->span_done) qa_range_error("out of memory");
  size_t at = 0;
  for (double start = 0; start < total; start += width) {
    t->spans[at].start = start;
    t->spans[at].end = qa_js_min(start + width, total) - 1;
    at++;
  }
  t->span_count = count;
  sink_create_scope create = {q, t, true};
  if (!qa_try(sink_create_body, &create, &caught)) { transfer_fail(q, t, caught.message, false); return; }
  t->phase = PHASE_RANGES;
  if (!queue_report(q, e, 0, true, total, &caught)) { transfer_fail(q, t, caught.message, false); return; }
  for (size_t i = 0; i < count; i++) op_new(q, t, OP_RANGE, i);
}
/* receiveRanges settled: every span finished, or the primary failure drained its siblings. */
static void transfer_ranges_settled(qa_http_download_queue *q, struct http_transfer_state *t) {
  qa_caught_error caught;
  if (t->has_primary) {
    if (!t->primary_unsupported) { transfer_fail(q, t, t->primary, false); return; }
    sink_scope scope = {t};
    if (!qa_try(sink_close_body, &scope, &caught)) { transfer_fail(q, t, "HTTP ranged staging cleanup failed", true); return; }
    const char *failure = transfer_current(q, t, &caught);
    if (failure) { transfer_fail(q, t, failure, false); return; }
    t->retry_identity = true;
    transfer_whole(q, t);
    return;
  }
  const char *failure = transfer_current(q, t, &caught);
  if (failure) { transfer_fail(q, t, failure, false); return; }
  transfer_publish(q, t);
}
static void range_finished(qa_http_download_queue *q, struct http_transfer_state *t, struct http_op *op) {
  qa_caught_error caught;
  size_t span = op->span;
  const qa_download_span *bounds = &t->spans[span];
  double width = bounds->end - bounds->start + 1;
  if (!op->has_error && t->span_received[span] != width) op_fail(op, "Incomplete HTTP byte range");
  if (!op->has_error) { t->span_done[span] = true; op_remove(q, t, op); }
  else {
    /* receiveRange's catch: current() and abort first, then retry from the retained offset. */
    const char *failure = transfer_current(q, t, &caught);
    bool retry = !failure && !op->unsupported && t->span_attempt[span] < 2 && t->span_received[span] != width;
    if (retry) {
      t->span_attempt[span]++;
      op->start = bounds->start + t->span_received[span];
      op->redirects = 0;
      free(op->url);
      op->url = dupstr(t->entry->url);
      op_start(q, op);
      return;
    }
    if (!t->has_primary) {
      t->has_primary = true;
      t->primary_unsupported = !failure && op->unsupported;
      snprintf(t->primary, sizeof t->primary, "%s", failure ? failure : op->error);
    }
    op_remove(q, t, op);
    /* siblings.abort(): the other spans stop and settle at once. */
    while (t->op_count > 0) op_remove(q, t, t->ops[0]);
  }
  if (t->op_count == 0) transfer_ranges_settled(q, t);
}
static void whole_finished(qa_http_download_queue *q, struct http_transfer_state *t, struct http_op *op) {
  if (!op->has_error) {
    if (!t->have_sink) { if (!op_validate(q, op)) { transfer_fail(q, t, op->error, false); return; } }
    double length = qa_download_sink_byte_length(&t->sink);
    if (t->has_total && length != t->total) op_fail(op, "Incomplete HTTP response body");
    else if (t->retry_identity && length != t->probe_total) op_fail(op, "HTTP whole-file retry differs from probed size");
  }
  char error[QA_CAUGHT_MESSAGE_CAPACITY];
  bool failed = op->has_error;
  snprintf(error, sizeof error, "%s", op->error);
  op_remove(q, t, op);
  if (failed) { transfer_fail(q, t, error, false); return; }
  transfer_publish(q, t);
}
/* One curl request ended. Maps transport results to the TS fetch/reader rejections. */
static void op_finished(qa_http_download_queue *q, struct http_op *op, CURLcode code) {
  struct http_transfer_state *t = op->owner;
  if (!op->started && code == CURLE_OK) {
    curl_easy_getinfo(op->easy, CURLINFO_RESPONSE_CODE, &op->status);
    if (is_redirect(op->status)) op->discard = true;
    else if (op->kind != OP_HEAD && op->kind != OP_WHOLE && !op_validate(q, op)) code = CURLE_WRITE_ERROR;
    op->started = op->kind != OP_WHOLE;
  }
  if (code != CURLE_OK && !op->has_error) {
    if (code == CURLE_ABORTED_BY_CALLBACK) op_fail(op, "This operation was aborted");
    else if (code == CURLE_OPERATION_TIMEDOUT) op_fail(op, "HTTP download stalled");
    else if (op->status > 0) op_fail(op, "HTTP response body failed");
    else { op->transport = true; op_fail(op, "HTTP download request failed"); }
  }
  if (!op->has_error && op->discard) {
    qa_caught_error caught;
    const char *failure = transfer_current(q, t, &caught);
    if (failure) op_fail(op, failure);
    else if (op_redirect(q, op)) return;
  }
  if (op->kind == OP_HEAD) {
    /* probeRanges catches request failures as "no ranges", unless the signal aborted. */
    qa_caught_error caught;
    const char *failure = transfer_current(q, t, &caught);
    if (failure) { op_remove(q, t, op); transfer_fail(q, t, failure, false); return; }
    transfer_probed(q, t, op, !op->has_error);
    return;
  }
  if (op->kind == OP_RANGE) range_finished(q, t, op);
  else whole_finished(q, t, op);
}
/* TS start()/complete(). */
static void queue_complete(qa_http_download_queue *q, struct http_entry *e) {
  q->active--;
  if (strcmp(e->kind, "package") == 0) {
    q->package_active = false;
    for (size_t i = 0; i < q->count; i++) q->entries[i]->checked = false;
  }
  if (e->result_kind && strcmp(e->result_kind, "failed") == 0) qa_http_download_queue_cancel(q);
}
static void queue_start(qa_http_download_queue *q, struct http_entry *e) {
  e->state = ENTRY_RUNNING; q->active++;
  if (strcmp(e->kind, "package") == 0) q->package_active = true;
  struct http_transfer_state *t = calloc(1, sizeof *t);
  if (!t) qa_range_error("out of memory");
  t->entry = e; t->generation = q->generation;
  e->transfer = t;
  qa_caught_error caught;
  const char *failure = transfer_current(q, t, &caught);
  if (failure) transfer_fail(q, t, failure, false);
  else if (q->range_streams > 1 && (e->expected.is_protocol ? e->expected.maximum_bytes : e->expected.byte_length) > RANGE_THRESHOLD) {
    t->phase = PHASE_PROBE;
    op_new(q, t, OP_HEAD, 0);
  } else transfer_whole(q, t);
}
typedef struct pump_scope { qa_http_download_queue *q; } pump_scope;
static void pump_body(void *ctx) {
  qa_http_download_queue *q = ((pump_scope *)ctx)->q;
  q->options.assert_current(q->options.ctx);
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    if (e->state != ENTRY_PENDING || e->checked) continue;
    e->checked = true;
    bool resolved = q->options.resolved(q->options.ctx, e->path);
    if (q->closed) return;
    q->options.assert_current(q->options.ctx);
    if (resolved) queue_settle(e, "resolved", NULL, NULL);
  }
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    if (e->state == ENTRY_PENDING && strcmp(e->kind, "package") == 0) {
      if (q->active == 0) queue_start(q, e);
      return;
    }
  }
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    if (e->state != ENTRY_PENDING) continue;
    if (q->active >= q->concurrency) break;
    queue_start(q, e);
  }
}
/* TS pump(): a host failure settles every pending entry as failed. */
static void queue_schedule(qa_http_download_queue *q) {
  if (q->closed || q->package_active) return;
  pump_scope scope = {q};
  qa_caught_error caught;
  if (qa_try(pump_body, &scope, &caught)) return;
  for (size_t i = 0; i < q->count; i++)
    if (q->entries[i]->state == ENTRY_PENDING) queue_settle(q->entries[i], "failed", NULL, caught.message);
}
/* Retire transfers whose entry aborted or epoch retired: their fetches reject at once. */
static void queue_reap(qa_http_download_queue *q) {
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    struct http_transfer_state *t = e->transfer;
    if (!t) continue;
    if (e->state == ENTRY_RUNNING && (q->closed || t->generation != q->generation || e->aborted))
      transfer_fail(q, t, "This operation was aborted", false);
    if (e->state == ENTRY_DONE) {
      transfer_free(q, t);
      e->transfer = NULL;
      queue_complete(q, e);
    }
  }
}
int qa_http_download_queue_poll(qa_http_download_queue *q, int timeout_milliseconds) {
  queue_reap(q);
  queue_schedule(q);
  queue_reap(q);
  if (q->multi && q->active > 0) {
    int running = 0, ready = 0;
    curl_multi_perform(q->multi, &running);
    if (timeout_milliseconds > 0) curl_multi_poll(q->multi, NULL, 0, timeout_milliseconds, &ready);
    curl_multi_perform(q->multi, &running);
    for (;;) {
      int left = 0;
      CURLMsg *message = curl_multi_info_read(q->multi, &left);
      if (!message) break;
      if (message->msg != CURLMSG_DONE) continue;
      struct http_op *op = NULL;
      curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, (char **)&op);
      CURLcode code = message->data.result;
      if (op && op->owner->entry->state == ENTRY_RUNNING) op_finished(q, op, code);
    }
    queue_reap(q);
    queue_schedule(q);
    queue_reap(q);
  }
  return (int)q->active;
}
qa_http_download_queue *qa_http_download_queue_create(qa_http_queue_options options) {
  qa_http_download_queue *q = malloc(sizeof *q);
  if (!q) qa_range_error("out of memory");
  qa_http_download_queue_init(q, options);
  return q;
}
void qa_http_download_queue_free(qa_http_download_queue *q) {
  qa_http_download_queue_destroy(q);
  free(q);
}
bool qa_http_download_queue_idle(qa_http_download_queue *q) {
  if (q->active > 0) return false;
  if (q->closed || q->package_active) return true;
  for (size_t i = 0; i < q->count; i++)
    if (q->entries[i]->state == ENTRY_PENDING && !q->entries[i]->checked) return false;
  for (size_t i = 0; i < q->count; i++)
    if (q->entries[i]->state == ENTRY_PENDING) return false;
  return true;
}
void qa_http_download_queue_pump(qa_http_download_queue *q) {
  do qa_http_download_queue_poll(q, 50); while (!qa_http_download_queue_idle(q));
}
int qa_http_download_queue_result(qa_http_download_queue *q, const char *path, qa_http_download_result *out) {
  struct http_entry *e = queue_find(q, path);
  if (!e || e->state != ENTRY_DONE) return 0;
  memset(out, 0, sizeof *out);
  out->kind = e->result_kind;
  out->digest = e->digest ? dupstr(e->digest) : NULL;
  out->reason = e->reason ? dupstr(e->reason) : NULL;
  return 1;
}
size_t qa_http_download_queue_progress_count(qa_http_download_queue *q) { return q->count; }
qa_download_progress qa_http_download_queue_progress_at(qa_http_download_queue *q, size_t index) {
  struct http_entry *e = q->entries[index];
  qa_download_progress p;
  p.path = e->path;
  p.phase = e->state == ENTRY_PENDING ? "pending" : e->state == ENTRY_RUNNING ? "running" : "done";
  p.received = e->received; p.has_total = e->has_total; p.total = e->total; p.result = e->result_kind;
  return p;
}
void qa_http_download_queue_retry(qa_http_download_queue *q, const char *path) {
  struct http_entry *e = queue_find(q, path);
  if (!e || e->state != ENTRY_DONE || e->transfer
      || !(e->result_kind && (strcmp(e->result_kind, "fallback") == 0 || strcmp(e->result_kind, "cancelled") == 0)))
    qa_range_error("Only a settled HTTP fallback or cancellation can be retried");
  qa_http_download_request req;
  req.path = e->path; req.url = e->url; req.kind = e->kind; req.expected = e->expected;
  req.validate = e->validate; req.validate_ctx = e->validate_ctx;
  size_t idx = 0;
  while (q->entries[idx] != e) idx++;
  memmove(&q->entries[idx], &q->entries[idx + 1], (q->count - idx - 1) * sizeof *q->entries);
  q->count--;
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))entry_free, e);
  qa_http_download_queue_enqueue(q, req);
  qa_cleanup_pop(&guard);
  entry_free(e);
  queue_schedule(q);
}
void qa_http_download_queue_cancel_file(qa_http_download_queue *q, const char *path) {
  struct http_entry *e = queue_find(q, path);
  if (e && e->state == ENTRY_PENDING) queue_settle(e, "cancelled", NULL, NULL);
  else if (e && e->state == ENTRY_RUNNING) e->aborted = true;
  queue_reap(q);
  queue_schedule(q);
}
void qa_http_download_queue_cancel(qa_http_download_queue *q) {
  if (q->closed) return;
  q->closed = true; q->generation++;
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = q->entries[i];
    if (e->state == ENTRY_PENDING) queue_settle(e, "cancelled", NULL, NULL);
    else if (e->state == ENTRY_RUNNING) e->aborted = true;
  }
  queue_reap(q);
}
