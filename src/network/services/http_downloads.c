/* Port of src/network/services/http-downloads.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/services/http_downloads.h"
#include "qa/foundation/support.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static char *dupn(const char *s, size_t n) { char *p = malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = '\0'; } return p; }
static bool is_safe_d(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }

/* --- Minimal URL handling: scheme://host[:port]/path --- */
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
static int http_connect(const struct url_parts *u) {
  char portbuf[16]; snprintf(portbuf, sizeof portbuf, "%d", u->port);
  struct addrinfo hints; memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(u->host, portbuf, &hints, &res) != 0 || !res) return -1;
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) { if (fd >= 0) close(fd); freeaddrinfo(res); return -1; }
  freeaddrinfo(res);
  struct timeval tv = { 30, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  return fd;
}
/* Performs one request (no redirect following); body streamed via callback
 * (NULL = collect nothing / HEAD). Returns status or -1 on transport error.
 * headers_out receives malloc'd header block. */
typedef void (*http_body_fn)(void *ctx, const uint8_t *chunk, size_t length);
static int http_once(const char *url, const char *method, const char *extra_headers,
                     http_body_fn on_body, void *body_ctx, char **headers_out, const bool *aborted) {
  struct url_parts u;
  if (!url_parse(url, &u)) return -1;
  if (strcmp(u.scheme, "https") == 0) { url_free(&u); return -2; } /* no TLS */
  int fd = http_connect(&u);
  if (fd < 0) { url_free(&u); return -1; }
  char req[4096];
  int rn = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n%s\r\n",
                    method, u.path, u.host, u.port, extra_headers ? extra_headers : "");
  url_free(&u);
  size_t sent = 0;
  while (sent < (size_t)rn) { ssize_t w = send(fd, req + sent, (size_t)rn - sent, 0); if (w <= 0) { close(fd); return -1; } sent += (size_t)w; }
  /* Read until header terminator. */
  size_t hcap = 8192, hlen = 0; char *hbuf = malloc(hcap);
  size_t body_at = 0; bool found = false;
  while (!found) {
    if (aborted && *aborted) { free(hbuf); close(fd); return -3; }
    if (hlen == hcap) { hcap *= 2; hbuf = realloc(hbuf, hcap); }
    ssize_t n = recv(fd, hbuf + hlen, hcap - hlen, 0);
    if (n <= 0) { free(hbuf); close(fd); return -1; }
    hlen += (size_t)n;
    if (hlen >= 4) for (size_t i = 0; i + 3 < hlen; i++)
      if (hbuf[i]=='\r' && hbuf[i+1]=='\n' && hbuf[i+2]=='\r' && hbuf[i+3]=='\n') { body_at = i + 4; found = true; break; }
    if (hlen > 65536 && !found) { free(hbuf); close(fd); return -1; }
  }
  int status = 0;
  { char *sp = strchr(hbuf, ' '); if (sp) status = atoi(sp + 1); }
  *headers_out = dupn(hbuf, body_at >= 2 ? body_at - 2 : body_at);
  if (on_body && hlen > body_at) on_body(body_ctx, (uint8_t *)hbuf + body_at, hlen - body_at);
  if (on_body) {
    uint8_t chunk[16384];
    for (;;) {
      if (aborted && *aborted) { free(hbuf); close(fd); return -3; }
      ssize_t n = recv(fd, chunk, sizeof chunk, 0);
      if (n == 0) break;
      if (n < 0) { if (errno == EINTR) continue; break; }
      on_body(body_ctx, chunk, (size_t)n);
    }
  }
  free(hbuf); close(fd);
  return status;
}
static bool url_ok(const char *url) {
  struct url_parts u;
  if (!url_parse(url, &u)) return false;
  bool ok = (strcmp(u.scheme, "http") == 0 || strcmp(u.scheme, "https") == 0) && !u.has_userinfo;
  url_free(&u); return ok;
}
/* Buffered follow: collects full body (downloads are bounded by sink limits at queue level). */
struct collect { uint8_t *data; size_t len, cap; bool oom; };
static void collect_body(void *ctx, const uint8_t *chunk, size_t length) {
  struct collect *c = ctx;
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
                               struct collect *out_body, char **headers_out, const bool *aborted) {
  char *current = dupstr(url);
  struct url_parts orig; url_parse(url, &orig);
  int result = -1;
  for (int redirects = 0;; redirects++) {
    if (!url_ok(current)) { result = -4; break; }
    struct collect body; memset(&body, 0, sizeof body);
    char *headers = NULL;
    int status = http_once(current, method, extra_headers, collect_body, &body, &headers, aborted);
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
char *qa_http_download_to_sink(const char *url, qa_download_sink *sink) {
  struct url_parts u; url_parse(url, &u);
  bool proto_ok = u.scheme && (strcmp(u.scheme, "https") == 0 || strcmp(u.scheme, "http") == 0);
  url_free(&u);
  if (!proto_ok) { qa_download_sink_close(sink); qa_range_error("Downloads require HTTP or HTTPS"); }
  struct collect body; memset(&body, 0, sizeof body);
  char *headers = NULL; bool not_aborted = false;
  int status = http_fetch_buffered(url, "GET", "", &body, &headers, &not_aborted);
  free(headers);
  if (status != 200) {
    free(body.data); qa_download_sink_close(sink);
    qa_range_error("Download HTTP status %d", status < 0 ? 0 : status);
  }
  if (body.len) qa_download_sink_append(sink, body.data, body.len);
  free(body.data);
  return qa_download_sink_finish(sink);
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
  int status = http_fetch_buffered(url, "GET", "", &body, &headers, aborted);
  free(headers);
  if (status != 200) { free(body.data); return NULL; }
  if ((double)body.len > maximum_bytes) { free(body.data); return NULL; }
  *out_len = body.len;
  return body.data ? body.data : malloc(1);
}

/* --- Queue --- */
struct http_entry {
  char *path, *url, *kind;
  qa_download_expectation expected; char *digest_copy;
  qa_http_validate_fn validate; void *validate_ctx;
  int state; /* 0 pending 1 running 2 done */
  double received; bool has_total; double total;
  const char *result_kind; char *digest, *reason;
  bool aborted;
};
struct qa_http_download_queue {
  qa_http_queue_options options;
  double concurrency, range_streams;
  struct http_entry *entries; size_t count, capacity;
  double active; bool package_active, closed; long long generation;
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
void qa_http_download_queue_destroy(qa_http_download_queue *q) {
  for (size_t i = 0; i < q->count; i++) {
    free(q->entries[i].path); free(q->entries[i].url); free(q->entries[i].kind);
    free(q->entries[i].digest_copy); free(q->entries[i].digest); free(q->entries[i].reason);
  }
  free(q->entries); memset(q, 0, sizeof *q);
}
static bool same_expectation(qa_download_expectation a, qa_download_expectation b) {
  if (a.is_protocol) return b.is_protocol && a.maximum_bytes == b.maximum_bytes;
  return !b.is_protocol && a.byte_length == b.byte_length && strcmp(a.digest, b.digest) == 0;
}
static struct http_entry *queue_find(qa_http_download_queue *q, const char *path) {
  for (size_t i = 0; i < q->count; i++) if (strcmp(q->entries[i].path, path) == 0) return &q->entries[i];
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
  if (q->count == q->capacity) { size_t nc = q->capacity ? q->capacity * 2 : 8; q->entries = realloc(q->entries, nc * sizeof *q->entries); q->capacity = nc; }
  struct http_entry *e = &q->entries[q->count++];
  memset(e, 0, sizeof *e);
  e->path = dupstr(request.path); e->url = dupstr(request.url); e->kind = dupstr(request.kind);
  e->expected = request.expected;
  if (!request.expected.is_protocol && request.expected.digest) {
    e->digest_copy = dupstr(request.expected.digest); e->expected.digest = e->digest_copy;
  }
  e->validate = request.validate; e->validate_ctx = request.validate_ctx;
  e->state = 0;
}
static void queue_settle(struct http_entry *e, const char *kind, char *digest, const char *reason) {
  e->state = 2; e->result_kind = kind; e->digest = digest;
  e->reason = reason ? dupstr(reason) : NULL;
}
static void queue_report(qa_http_download_queue *q, struct http_entry *e, double received, bool has_total, double total) {
  e->received = received; e->has_total = has_total; e->total = total;
  q->options.progress(q->options.ctx, e->path, received, has_total, total);
}
static bool entry_current(qa_http_download_queue *q, struct http_entry *e, long long generation) {
  if (q->closed || generation != q->generation) return false;
  q->options.assert_current(q->options.ctx);
  return !e->aborted;
}
static void queue_transfer(qa_http_download_queue *q, struct http_entry *e) {
  long long generation = q->generation;
  qa_download_sink sink; bool have_sink = false; bool published = false;
  char *digest = NULL; const char *fail_reason = NULL; char failbuf[256] = {0};
  if (!entry_current(q, e, generation)) { queue_settle(e, "cancelled", NULL, NULL); return; }
  /* Range probe (HEAD) when streams > 1 and limit > 1MiB. */
  double limit = e->expected.is_protocol ? e->expected.maximum_bytes : e->expected.byte_length;
  bool use_ranges = false; double probe_total = 0; char etag[512] = {0}; bool has_etag = false;
  if (q->range_streams > 1 && limit > 1048576) {
    struct collect body; memset(&body, 0, sizeof body);
    char *headers = NULL;
    int status = http_fetch_buffered(e->url, "HEAD", "Accept-Encoding: identity\r\n", &body, &headers, &e->aborted);
    free(body.data);
    if (status == 200 && headers) {
      double total = 0; char ar[64];
      const char *etagv = header_get(headers, "etag", etag, sizeof etag);
      has_etag = etagv != NULL;
      if (has_etag) memmove(etag, etagv, strlen(etagv) + 1);
      bool ranges_ok = header_get(headers, "accept-ranges", ar, sizeof ar) && strcmp(ar, "bytes") == 0;
      if (identity_encoding(headers) && content_length_of(headers, &total) && total > 1048576 && ranges_ok && has_etag) {
        if (total > limit || (!e->expected.is_protocol && total != e->expected.byte_length)) {
          free(headers); queue_settle(e, "fallback", NULL, "HTTP range size differs from expected bounds"); return;
        }
        use_ranges = true; probe_total = total;
      }
    }
    free(headers);
  }
  if (!entry_current(q, e, generation)) { queue_settle(e, "cancelled", NULL, NULL); return; }
  if (use_ranges) {
    double streams = q->range_streams;
    double width = ceil(probe_total / streams);
    size_t nspans = 0; for (double st = 0; st < probe_total; st += width) nspans++;
    qa_download_span *spans = malloc(nspans * sizeof *spans);
    size_t si = 0;
    for (double st = 0; st < probe_total; st += width) { spans[si].start = st; spans[si].end = fmin(st + width, probe_total) - 1; si++; }
    qa_download_sink_create_ranged(&sink, q->options.root, e->path, e->expected, probe_total, spans, nspans);
    have_sink = true;
    queue_report(q, e, 0, true, probe_total);
    bool ok = true;
    for (size_t i = 0; i < nspans && ok; i++) {
      char range_hdr[1024];
      snprintf(range_hdr, sizeof range_hdr, "Range: bytes=%lld-%lld\r\nIf-Range: %s\r\nAccept-Encoding: identity\r\n",
               (long long)spans[i].start, (long long)spans[i].end, etag);
      struct collect body; memset(&body, 0, sizeof body);
      char *headers = NULL;
      int status = http_fetch_buffered(e->url, "GET", range_hdr, &body, &headers, &e->aborted);
      char cr[256] = {0}; char exp_cr[256];
      snprintf(exp_cr, sizeof exp_cr, "bytes %lld-%lld/%lld", (long long)spans[i].start, (long long)spans[i].end, (long long)probe_total);
      bool cr_ok = headers && header_get(headers, "content-range", cr, sizeof cr) && strcmp(cr, exp_cr) == 0;
      size_t want = (size_t)(spans[i].end - spans[i].start + 1);
      if (status != 206 || !cr_ok || body.len != want) ok = false;
      else {
        qa_download_sink_append_range(&sink, spans[i].start, body.data, body.len);
        queue_report(q, e, qa_download_sink_byte_length(&sink), true, probe_total);
      }
      free(body.data); free(headers);
      if (!entry_current(q, e, generation)) ok = false;
    }
    free(spans);
    if (!ok) {
      qa_download_sink_destroy(&sink); have_sink = false;
      use_ranges = false; /* fall through to whole-file with If-Match identity */
    }
  }
  if (!have_sink) {
    char extra[600] = "";
    if (has_etag) snprintf(extra, sizeof extra, "Accept-Encoding: identity\r\nIf-Match: %s\r\n", etag);
    struct collect body; memset(&body, 0, sizeof body);
    char *headers = NULL;
    int status = http_fetch_buffered(e->url, "GET", extra, &body, &headers, &e->aborted);
    double total = 0; bool has_total = headers && content_length_of(headers, &total);
    if (!entry_current(q, e, generation)) { free(body.data); free(headers); queue_settle(e, "cancelled", NULL, NULL); return; }
    if (status != 200) {
      snprintf(failbuf, sizeof failbuf, "HTTP download status %d", status < 0 ? 0 : status);
      fail_reason = failbuf; free(body.data); free(headers);
      queue_settle(e, "fallback", NULL, fail_reason); return;
    }
    qa_download_sink_create(&sink, q->options.root, e->path, e->expected);
    have_sink = true;
    queue_report(q, e, 0, has_total, total);
    if (body.len) qa_download_sink_append(&sink, body.data, body.len);
    queue_report(q, e, qa_download_sink_byte_length(&sink), has_total, total);
    if (has_total && qa_download_sink_byte_length(&sink) != total) {
      free(body.data); free(headers); qa_download_sink_destroy(&sink);
      queue_settle(e, "fallback", NULL, "Incomplete HTTP response body"); return;
    }
    if (has_etag && use_ranges == false && probe_total > 0 && qa_download_sink_byte_length(&sink) != probe_total) {
      free(body.data); free(headers); qa_download_sink_destroy(&sink);
      queue_settle(e, "fallback", NULL, "HTTP whole-file retry differs from probed size"); return;
    }
    free(body.data); free(headers);
  }
  if (!entry_current(q, e, generation)) { qa_download_sink_destroy(&sink); queue_settle(e, "cancelled", NULL, NULL); return; }
  if (e->validate) {
    qa_download_sink_inspect_staged(&sink, e->validate, e->validate_ctx);
    if (!entry_current(q, e, generation)) { qa_download_sink_destroy(&sink); queue_settle(e, "cancelled", NULL, NULL); return; }
  }
  digest = qa_download_sink_finish(&sink); published = true;
  qa_download_sink_destroy(&sink);
  if (strcmp(e->kind, "package") == 0) {
    q->options.refresh_package(q->options.ctx, e->path);
    if (!entry_current(q, e, generation)) { queue_settle(e, "cancelled", NULL, NULL); return; }
  }
  (void)published;
  queue_settle(e, "downloaded", digest, NULL);
}
void qa_http_download_queue_pump(qa_http_download_queue *q) {
  if (q->closed || q->package_active) return;
  q->options.assert_current(q->options.ctx);
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = &q->entries[i];
    if (e->state != 0) continue;
    bool resolved = q->options.resolved(q->options.ctx, e->path);
    if (q->closed) return;
    q->options.assert_current(q->options.ctx);
    if (resolved) queue_settle(e, "resolved", NULL, NULL);
  }
  /* Packages run exclusively and first; then assets in order. */
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = &q->entries[i];
    if (e->state != 0 || strcmp(e->kind, "package") != 0) continue;
    e->state = 1; q->package_active = true;
    queue_transfer(q, e);
    q->package_active = false;
    if (e->result_kind && strcmp(e->result_kind, "failed") == 0) { qa_http_download_queue_cancel(q); return; }
  }
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = &q->entries[i];
    if (e->state != 0) continue;
    e->state = 1;
    queue_transfer(q, e);
    if (e->result_kind && strcmp(e->result_kind, "failed") == 0) { qa_http_download_queue_cancel(q); return; }
    if (q->closed) return;
  }
}
int qa_http_download_queue_result(qa_http_download_queue *q, const char *path, qa_http_download_result *out) {
  struct http_entry *e = queue_find(q, path);
  if (!e || e->state != 2) return 0;
  memset(out, 0, sizeof *out);
  out->kind = e->result_kind;
  out->digest = e->digest ? dupstr(e->digest) : NULL;
  out->reason = e->reason ? dupstr(e->reason) : NULL;
  return 1;
}
size_t qa_http_download_queue_progress_count(qa_http_download_queue *q) { return q->count; }
qa_download_progress qa_http_download_queue_progress_at(qa_http_download_queue *q, size_t index) {
  struct http_entry *e = &q->entries[index];
  qa_download_progress p;
  p.path = e->path;
  p.phase = e->state == 0 ? "pending" : e->state == 1 ? "running" : "done";
  p.received = e->received; p.has_total = e->has_total; p.total = e->total; p.result = e->result_kind;
  return p;
}
void qa_http_download_queue_retry(qa_http_download_queue *q, const char *path) {
  struct http_entry *e = queue_find(q, path);
  if (!e || e->state != 2 || !(e->result_kind && (strcmp(e->result_kind, "fallback") == 0 || strcmp(e->result_kind, "cancelled") == 0)))
    qa_range_error("Only a settled HTTP fallback or cancellation can be retried");
  qa_http_download_request req;
  req.path = e->path; req.url = e->url; req.kind = e->kind; req.expected = e->expected;
  req.validate = e->validate; req.validate_ctx = e->validate_ctx;
  size_t idx = (size_t)(e - q->entries);
  char *p = dupstr(e->path), *u = dupstr(e->url), *k = dupstr(e->kind);
  req.path = p; req.url = u; req.kind = k;
  free(e->path); free(e->url); free(e->kind); free(e->digest_copy); free(e->digest); free(e->reason);
  memmove(&q->entries[idx], &q->entries[idx+1], (q->count - idx - 1) * sizeof *q->entries);
  q->count--;
  qa_http_download_queue_enqueue(q, req);
  free(p); free(u); free(k);
}
void qa_http_download_queue_cancel_file(qa_http_download_queue *q, const char *path) {
  struct http_entry *e = queue_find(q, path);
  if (e && e->state == 0) queue_settle(e, "cancelled", NULL, NULL);
  else if (e && e->state == 1) e->aborted = true;
}
void qa_http_download_queue_cancel(qa_http_download_queue *q) {
  if (q->closed) return;
  q->closed = true; q->generation++;
  for (size_t i = 0; i < q->count; i++) {
    struct http_entry *e = &q->entries[i];
    if (e->state == 0) queue_settle(e, "cancelled", NULL, NULL);
    else if (e->state == 1) e->aborted = true;
  }
}
