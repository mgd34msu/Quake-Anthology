/* Port of src/app/bootstrap/server-master-list.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_master_list.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/foundation/support.h"
#include "qa/network/common/transport.h"

void qa_app_server_master_list_free(char **servers, size_t count) {
  if (servers == NULL) return;
  for (size_t i = 0; i < count; i++) free(servers[i]);
  free(servers);
}

/* TextDecoder("utf-8", { fatal: true }): the WHATWG decoder rejects overlong forms,
 * surrogates and code points past U+10FFFF; a leading BOM is dropped. */
static size_t utf8_sequence(const uint8_t *bytes, size_t length, size_t at) {
  uint8_t c = bytes[at];
  if (c < 0x80) return 1;
  size_t need; uint8_t lower = 0x80, upper = 0xbf;
  if (c >= 0xc2 && c <= 0xdf) need = 1;
  else if (c >= 0xe0 && c <= 0xef) { need = 2; if (c == 0xe0) lower = 0xa0; if (c == 0xed) upper = 0x9f; }
  else if (c >= 0xf0 && c <= 0xf4) { need = 3; if (c == 0xf0) lower = 0x90; if (c == 0xf4) upper = 0x8f; }
  else return 0;
  if (at + need >= length) return 0;
  for (size_t i = 1; i <= need; i++) {
    uint8_t b = bytes[at + i];
    if (i == 1 ? (b < lower || b > upper) : (b & 0xc0) != 0x80) return 0;
  }
  return need + 1;
}
/* String.prototype.trim: WhiteSpace and LineTerminator code points, decoded from UTF-8. */
size_t qa_app_js_space_at(const char *text, size_t length, size_t at) {
  const uint8_t *b = (const uint8_t *)text + at;
  size_t left = length - at;
  if (left >= 1 && (b[0] == 0x09 || b[0] == 0x0a || b[0] == 0x0b || b[0] == 0x0c || b[0] == 0x0d || b[0] == 0x20)) return 1;
  if (left >= 2 && b[0] == 0xc2 && b[1] == 0xa0) return 2;
  if (left >= 3 && b[0] == 0xe1 && b[1] == 0x9a && b[2] == 0x80) return 3;
  if (left >= 3 && b[0] == 0xe2 && b[1] == 0x80 && ((b[2] >= 0x80 && b[2] <= 0x8a) || b[2] == 0xa8 || b[2] == 0xa9 || b[2] == 0xaf)) return 3;
  if (left >= 3 && b[0] == 0xe2 && b[1] == 0x81 && b[2] == 0x9f) return 3;
  if (left >= 3 && b[0] == 0xe3 && b[1] == 0x80 && b[2] == 0x80) return 3;
  if (left >= 3 && b[0] == 0xef && b[1] == 0xbb && b[2] == 0xbf) return 3;
  return 0;
}
static size_t js_space_before(const char *text, size_t end) {
  for (size_t width = 1; width <= 3 && width <= end; width++)
    if (qa_app_js_space_at(text, end, end - width) == width) return width;
  return 0;
}
char *qa_app_js_trim_range(const char *text, size_t length) {
  size_t start = 0, end = length, step;
  while (start < end && (step = qa_app_js_space_at(text, end, start)) != 0) start += step;
  while (end > start && (step = js_space_before(text, end)) != 0) end -= step;
  char *out = malloc(end - start + 1);
  if (out == NULL) return NULL;
  memcpy(out, text + start, end - start);
  out[end - start] = '\0';
  return out;
}
char *qa_app_js_trim(const char *text) { return qa_app_js_trim_range(text, strlen(text)); }

/* Owned state of one parse; released on a raise so a catching caller leaks nothing. */
typedef struct parse_scope {
  char *text;
  char **servers; size_t count, capacity;
  char **valid; size_t valid_count;
} parse_scope;
static void parse_scope_release(void *arg) {
  parse_scope *scope = arg;
  free(scope->text); scope->text = NULL;
  qa_app_server_master_list_free(scope->servers, scope->count); scope->servers = NULL; scope->count = 0;
  free(scope->valid); scope->valid = NULL;
}
typedef struct direct_scope { const char *text; } direct_scope;
static void direct_text_body(void *ctx) { (void)qa_app_direct_server_text(((direct_scope *)ctx)->text); }

char **qa_app_parse_server_master_list(const uint8_t *bytes, size_t length, qa_app_master_list_skipped_fn skipped,
                                       void *skipped_ctx, size_t *out_count) {
  *out_count = 0;
  for (size_t i = 0; i < length;) {
    size_t width = utf8_sequence(bytes, length, i);
    if (width == 0) qa_raise("TypeError", 0, 0, "Invalid byte sequence");
    i += width;
  }
  size_t start = length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
  parse_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, parse_scope_release, &scope);
  scope.text = malloc(length - start + 1);
  if (scope.text == NULL) qa_range_error("Out of memory");
  memcpy(scope.text, bytes + start, length - start);
  scope.text[length - start] = '\0';
  /* .split(/\r?\n/).map(trim).filter(non-empty, not '#') into a Set (first-seen order).
   * A decoded NUL is kept by JS; the C text ends there (string-model limit). */
  size_t text_length = length - start;
  for (size_t cursor = 0; cursor <= text_length;) {
    const char *nl = memchr(scope.text + cursor, '\n', text_length - cursor);
    size_t line_end = nl != NULL ? (size_t)(nl - scope.text) : text_length;
    size_t line_length = line_end - cursor;
    if (nl != NULL && line_length > 0 && scope.text[line_end - 1] == '\r') line_length--;
    char *line = qa_app_js_trim_range(scope.text + cursor, line_length);
    if (line == NULL) qa_range_error("Out of memory");
    bool keep = line[0] != '\0' && line[0] != '#';
    for (size_t i = 0; keep && i < scope.count; i++) if (strcmp(scope.servers[i], line) == 0) keep = false;
    if (!keep) free(line);
    else {
      if (scope.count == scope.capacity) {
        size_t capacity = scope.capacity ? scope.capacity * 2 : 16;
        char **grown = realloc(scope.servers, capacity * sizeof *grown);
        if (grown == NULL) { free(line); qa_range_error("Out of memory"); }
        scope.servers = grown; scope.capacity = capacity;
      }
      scope.servers[scope.count++] = line;
    }
    if (nl == NULL) break;
    cursor = line_end + 1;
  }
  free(scope.text); scope.text = NULL;
  if (scope.count > 4096) qa_raise("Error", 0, 0, "Master list exceeds 4096 servers");
  scope.valid = calloc(scope.count ? scope.count : 1, sizeof *scope.valid);
  if (scope.valid == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < scope.count; i++) {
    direct_scope check = {scope.servers[i]};
    qa_caught_error caught;
    if (qa_try(direct_text_body, &check, &caught)) scope.valid[scope.valid_count++] = scope.servers[i];
    else { free(scope.servers[i]); scope.servers[i] = NULL; }
  }
  size_t total = scope.count;
  free(scope.servers);
  scope.servers = scope.valid; scope.count = scope.valid_count; scope.valid = NULL;
  /* skipped is a caller callback: the guard still owns the valid list if it raises. */
  if (scope.count != total && skipped != NULL) skipped(skipped_ctx, (double)(total - scope.count));
  qa_cleanup_pop(&guard);
  *out_count = scope.count;
  return scope.servers;
}

/* One GET: the body is collected up to the 1 MiB bound. */
typedef struct fetch_scope {
  const qa_app_master_list_signal *signal;
  uint8_t *body; size_t length, capacity;
  bool oversize, aborted;
  CURL *easy;
} fetch_scope;
static void fetch_scope_release(void *arg) {
  fetch_scope *scope = arg;
  if (scope->easy != NULL) curl_easy_cleanup(scope->easy);
  scope->easy = NULL;
  free(scope->body); scope->body = NULL;
}
static size_t fetch_body(char *data, size_t size, size_t count, void *ctx) {
  fetch_scope *scope = ctx;
  size_t n = size * count;
  if (scope->length + n > 1024 * 1024) { scope->oversize = true; return 0; }
  if (scope->length + n > scope->capacity) {
    size_t capacity = scope->capacity ? scope->capacity : 4096;
    while (capacity < scope->length + n) capacity *= 2;
    uint8_t *grown = realloc(scope->body, capacity);
    if (grown == NULL) return 0;
    scope->body = grown; scope->capacity = capacity;
  }
  memcpy(scope->body + scope->length, data, n);
  scope->length += n;
  return n;
}
static int fetch_progress(void *ctx, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
  fetch_scope *scope = ctx;
  (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
  if (scope->signal != NULL && (scope->signal->aborted ||
      (scope->signal->deadline_ms > 0 && qa_net_default_now(NULL) >= scope->signal->deadline_ms))) {
    scope->aborted = true;
    return 1;
  }
  return 0;
}
static void throw_if_aborted(const qa_app_master_list_signal *signal) {
  if (signal != NULL && (signal->aborted || (signal->deadline_ms > 0 && qa_net_default_now(NULL) >= signal->deadline_ms)))
    qa_raise("AbortError", 0, 0, "The operation was aborted.");
}

char **qa_app_fetch_server_master_list(const char *url, const qa_app_master_list_signal *signal,
                                       qa_app_master_list_skipped_fn skipped, void *skipped_ctx, size_t *out_count) {
  *out_count = 0;
  /* new URL(url).protocol: the scheme is ASCII case-insensitive. */
  const char *colon = strchr(url, ':');
  if (colon == NULL) qa_raise("TypeError", 0, 0, "\"%s\" cannot be parsed as a URL.", url);
  size_t scheme = (size_t)(colon - url);
  if (!((scheme == 4 && strncasecmp(url, "http", 4) == 0) || (scheme == 5 && strncasecmp(url, "https", 5) == 0)))
    qa_raise("Error", 0, 0, "Master lists require HTTP or HTTPS");
  throw_if_aborted(signal);
  static bool initialized = false;
  if (!initialized) { curl_global_init(CURL_GLOBAL_DEFAULT); initialized = true; }
  fetch_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.signal = signal;
  qa_cleanup guard;
  qa_cleanup_push(&guard, fetch_scope_release, &scope);
  scope.easy = curl_easy_init();
  if (scope.easy == NULL) qa_range_error("Out of memory");
  curl_easy_setopt(scope.easy, CURLOPT_URL, url);
  curl_easy_setopt(scope.easy, CURLOPT_NOSIGNAL, 1L);
  /* fetch's default redirect mode "follow" with its limit of 20. */
  curl_easy_setopt(scope.easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(scope.easy, CURLOPT_MAXREDIRS, 20L);
  curl_easy_setopt(scope.easy, CURLOPT_WRITEFUNCTION, fetch_body);
  curl_easy_setopt(scope.easy, CURLOPT_WRITEDATA, &scope);
  curl_easy_setopt(scope.easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(scope.easy, CURLOPT_XFERINFOFUNCTION, fetch_progress);
  curl_easy_setopt(scope.easy, CURLOPT_XFERINFODATA, &scope);
  if (signal != NULL && signal->deadline_ms > 0) {
    double left = signal->deadline_ms - qa_net_default_now(NULL);
    curl_easy_setopt(scope.easy, CURLOPT_TIMEOUT_MS, (long)(left < 1 ? 1 : left));
  }
  CURLcode code = curl_easy_perform(scope.easy);
  long status = 0;
  curl_easy_getinfo(scope.easy, CURLINFO_RESPONSE_CODE, &status);
  if (scope.aborted || code == CURLE_OPERATION_TIMEDOUT) qa_raise("AbortError", 0, 0, "The operation was aborted.");
  /* response.ok is checked once the headers arrive, before the body is read. */
  if (status != 0 && (status < 200 || status > 299)) qa_raise("Error", 0, 0, "Master list returned HTTP %ld", status);
  if (scope.oversize) qa_raise("Error", 0, 0, "Master list exceeds 1 MiB");
  if (code == CURLE_URL_MALFORMAT) qa_raise("TypeError", 0, 0, "\"%s\" cannot be parsed as a URL.", url);
  if (code != CURLE_OK) qa_raise("Error", 0, 0, "%s", curl_easy_strerror(code));
  curl_easy_cleanup(scope.easy);
  scope.easy = NULL;
  /* Bun's fetch always exposes a byte ReadableStream body for a completed GET, so the
   * "no response body" and "not a byte stream" branches cannot arise here. */
  char **servers = qa_app_parse_server_master_list(scope.body != NULL ? scope.body : (const uint8_t *)"", scope.length,
                                                   skipped, skipped_ctx, out_count);
  qa_cleanup_pop(&guard);
  free(scope.body);
  return servers;
}
