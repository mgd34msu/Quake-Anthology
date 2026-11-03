/* Port of the single-transfer cases of tests/network/common/http-downloads.test.ts against a
 * scripted local HTTP server; the concurrent cases are in test_http_concurrency.c.
 * GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/network/services/http_downloads.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define FILE_SIZE (2 * 1024 * 1024 + 5)
static uint8_t *file_bytes;

typedef enum mode {
  MODE_RANGES, MODE_RANGE_200, MODE_RANGE_416, MODE_NO_ETAG, MODE_WEAK_ETAG, MODE_NO_ACCEPT, MODE_BAD_CONTENT_RANGE,
  MODE_INTERRUPTED, MODE_CHANGED_AFTER_REJECT, MODE_TRUNCATED_WHOLE, MODE_REDIRECT_SAME, MODE_REDIRECT_AWAY, MODE_MISSING,
} mode;

static void respond(int fd, const char *head, const uint8_t *body, size_t length) {
  if (write(fd, head, strlen(head)) < 0) return;
  size_t sent = 0;
  while (sent < length) {
    ssize_t n = write(fd, body + sent, length - sent);
    if (n <= 0) return;
    sent += (size_t)n;
  }
}
static void serve(int listener, mode m, int log_fd) {
  int interrupted_once = 0;
  for (;;) {
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) _exit(0);
    char request[8192];
    size_t used = 0;
    while (used + 1 < sizeof request) {
      ssize_t n = read(fd, request + used, sizeof request - 1 - used);
      if (n <= 0) break;
      used += (size_t)n;
      request[used] = '\0';
      if (strstr(request, "\r\n\r\n")) break;
    }
    request[used] = '\0';
    char line[512];
    size_t line_length = strcspn(request, "\r\n");
    snprintf(line, sizeof line, "%.*s\n", (int)line_length, request);
    /* One line per request, with the Range and If-Match it carried. */
    const char *range = strcasestr(request, "\r\nRange: bytes=");
    const char *if_match = strcasestr(request, "\r\nIf-Match:");
    char log_line[640];
    snprintf(log_line, sizeof log_line, "%.*s range=%.*s match=%d\n", (int)line_length, request,
        range ? (int)strcspn(range + 15, "\r\n") : 0, range ? range + 15 : "", if_match != NULL);
    if (write(log_fd, log_line, strlen(log_line)) < 0) _exit(1);
    bool head = strncmp(request, "HEAD", 4) == 0;
    char text[512];
    const char *etag = m == MODE_NO_ETAG ? "" : m == MODE_WEAK_ETAG ? "ETag: W/\"weak\"\r\n" : "ETag: \"v1\"\r\n";
    const char *accept_ranges = m == MODE_NO_ACCEPT || m == MODE_TRUNCATED_WHOLE ? "" : "Accept-Ranges: bytes\r\n";
    if (m == MODE_MISSING) {
      respond(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", NULL, 0);
    } else if ((m == MODE_REDIRECT_SAME || m == MODE_REDIRECT_AWAY) && strstr(request, "GET /start")) {
      respond(fd, m == MODE_REDIRECT_SAME
          ? "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
          : "HTTP/1.1 302 Found\r\nLocation: http://192.0.2.1:9/away\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", NULL, 0);
    } else if (m == MODE_REDIRECT_SAME || m == MODE_REDIRECT_AWAY) {
      respond(fd, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n", (const uint8_t *)"small", 5);
    } else if (head) {
      snprintf(text, sizeof text, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n%s%sConnection: close\r\n\r\n", FILE_SIZE, etag, accept_ranges);
      respond(fd, text, NULL, 0);
    } else if (range && m != MODE_NO_ETAG && m != MODE_WEAK_ETAG && m != MODE_NO_ACCEPT) {
      long start = 0, end = 0;
      sscanf(range + 15, "%ld-%ld", &start, &end);
      size_t length = (size_t)(end - start + 1);
      if (m == MODE_RANGE_200 || m == MODE_CHANGED_AFTER_REJECT) {
        snprintf(text, sizeof text, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n%sConnection: close\r\n\r\n", FILE_SIZE, etag);
        respond(fd, text, file_bytes, FILE_SIZE);
      } else if (m == MODE_RANGE_416) {
        respond(fd, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", NULL, 0);
      } else if (m == MODE_BAD_CONTENT_RANGE) {
        snprintf(text, sizeof text, "HTTP/1.1 206 Partial Content\r\nContent-Length: %zu\r\n%sContent-Range: bytes %ld-%ld/9\r\nConnection: close\r\n\r\n", length, etag, start, end);
        respond(fd, text, file_bytes + start, length);
      } else if (m == MODE_INTERRUPTED && !interrupted_once) {
        /* Advertise the whole span, deliver half, and drop the connection. */
        interrupted_once = 1;
        snprintf(text, sizeof text, "HTTP/1.1 206 Partial Content\r\nContent-Length: %zu\r\n%sContent-Range: bytes %ld-%ld/%d\r\nConnection: close\r\n\r\n", length, etag, start, end, FILE_SIZE);
        respond(fd, text, file_bytes + start, length / 2);
      } else {
        snprintf(text, sizeof text, "HTTP/1.1 206 Partial Content\r\nContent-Length: %zu\r\n%sContent-Range: bytes %ld-%ld/%d\r\nConnection: close\r\n\r\n", length, etag, start, end, FILE_SIZE);
        respond(fd, text, file_bytes + start, length);
      }
    } else if (m == MODE_CHANGED_AFTER_REJECT) {
      respond(fd, "HTTP/1.1 412 Precondition Failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", NULL, 0);
    } else if (m == MODE_TRUNCATED_WHOLE) {
      snprintf(text, sizeof text, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", FILE_SIZE);
      respond(fd, text, file_bytes, 100);
    } else {
      snprintf(text, sizeof text, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n%s%sConnection: close\r\n\r\n", FILE_SIZE, etag, accept_ranges);
      respond(fd, text, file_bytes, FILE_SIZE);
    }
    close(fd);
  }
}

typedef struct host_ctx { int sentinel; int progress_calls; } host_ctx;
static host_ctx host = {0x5a5a, 0};
static void assert_current(void *ctx) { CHECK(ctx == &host); }
static bool resolved(void *ctx, const char *path) { (void)path; CHECK(ctx == &host); return false; }
static void progress(void *ctx, const char *path, double received, bool has_total, double total) {
  (void)path; (void)received; (void)has_total; (void)total;
  CHECK(ctx == &host);
  host.progress_calls++;
}
static void refresh(void *ctx, const char *path) { (void)path; CHECK(ctx == &host); }

typedef struct outcome { char kind[16]; char reason[128]; bool published, identical; size_t length; char log[4096]; } outcome;

static outcome run(mode m, const char *url_path, bool small_limit) {
  outcome result;
  memset(&result, 0, sizeof result);
  /* Build-local scratch (run from the repository root); removed below on every outcome. */
  mkdir("build", 0755); mkdir("build/agents", 0755); mkdir("build/agents/network2", 0755);
  mkdir("build/agents/network2/tmp", 0755);
  char root[] = "build/agents/network2/tmp/qa-http-XXXXXX";
  if (!mkdtemp(root)) abort();
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(listener, 64) != 0) abort();
  socklen_t size = sizeof addr;
  getsockname(listener, (struct sockaddr *)&addr, &size);
  int log_pipe[2];
  if (pipe(log_pipe) != 0) abort();
  pid_t server = fork();
  if (server == 0) { close(log_pipe[0]); serve(listener, m, log_pipe[1]); _exit(0); }
  close(log_pipe[1]);
  qa_http_queue_options options;
  memset(&options, 0, sizeof options);
  options.root = root;
  options.ctx = &host;
  options.assert_current = assert_current;
  options.resolved = resolved;
  options.progress = progress;
  options.refresh_package = refresh;
  qa_http_download_queue *queue = qa_http_download_queue_create(options);
  char url[256];
  snprintf(url, sizeof url, "http://127.0.0.1:%d%s", ntohs(addr.sin_port), url_path);
  qa_http_download_request request;
  memset(&request, 0, sizeof request);
  request.path = "file.bin"; request.url = url; request.kind = "asset";
  request.expected.is_protocol = true;
  request.expected.maximum_bytes = small_limit ? 100 : 8 * 1024 * 1024;
  qa_http_download_queue_enqueue(queue, request);
  qa_http_download_queue_pump(queue);
  qa_http_download_result outcome_value;
  CHECK(qa_http_download_queue_result(queue, "file.bin", &outcome_value) == 1);
  snprintf(result.kind, sizeof result.kind, "%s", outcome_value.kind);
  if (outcome_value.reason) snprintf(result.reason, sizeof result.reason, "%s", outcome_value.reason);
  qa_http_download_result_destroy(&outcome_value);
  qa_http_download_queue_free(queue);
  kill(server, SIGKILL);
  waitpid(server, NULL, 0);
  close(listener);
  ssize_t got = read(log_pipe[0], result.log, sizeof result.log - 1);
  if (got > 0) result.log[got] = '\0';
  close(log_pipe[0]);
  char path[256];
  snprintf(path, sizeof path, "%s/file.bin", root);
  FILE *f = fopen(path, "rb");
  if (f) {
    result.published = true;
    uint8_t *bytes = malloc(FILE_SIZE + 16);
    result.length = fread(bytes, 1, FILE_SIZE + 16, f);
    result.identical = result.length == FILE_SIZE && memcmp(bytes, file_bytes, FILE_SIZE) == 0;
    free(bytes);
    fclose(f);
    unlink(path);
  }
  /* Staging files must be gone. */
  DIR *dir = opendir(root);
  int stray = 0;
  for (struct dirent *entry; dir && (entry = readdir(dir));)
    if (entry->d_name[0] != '.' || (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) stray++;
  if (dir) closedir(dir);
  CHECK(stray == 0);
  rmdir(root);
  return result;
}
static int count_lines(const char *text, const char *needle) {
  int count = 0;
  for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) count++;
  return count;
}

int main(void) {
  signal(SIGPIPE, SIG_IGN);
  file_bytes = malloc(FILE_SIZE);
  for (size_t i = 0; i < FILE_SIZE; i++) file_bytes[i] = (uint8_t)(i * 31 + (i >> 8));

  outcome r = run(MODE_RANGES, "/file", false);
  CHECK(strcmp(r.kind, "downloaded") == 0 && r.identical);
  CHECK(count_lines(r.log, "HEAD ") == 1 && count_lines(r.log, "GET ") == 4); /* Four spans. */

  r = run(MODE_RANGE_200, "/file", false);
  CHECK(strcmp(r.kind, "downloaded") == 0 && r.identical);
  CHECK(count_lines(r.log, "match=1") == 1); /* Rejection retries one identity-bound whole file. */

  r = run(MODE_RANGE_416, "/file", false);
  CHECK(strcmp(r.kind, "downloaded") == 0 && r.identical);
  CHECK(count_lines(r.log, "match=1") == 1);

  /* Unusable probes take the ordinary whole-file path with no identity. */
  const mode whole[] = {MODE_NO_ETAG, MODE_WEAK_ETAG, MODE_NO_ACCEPT};
  for (size_t i = 0; i < 3; i++) {
    r = run(whole[i], "/file", false);
    CHECK(strcmp(r.kind, "downloaded") == 0 && r.identical);
    CHECK(count_lines(r.log, "GET ") == 1 && count_lines(r.log, "match=1") == 0);
  }

  r = run(MODE_BAD_CONTENT_RANGE, "/file", false);
  CHECK(strcmp(r.kind, "fallback") == 0 && !r.published);
  CHECK(strcmp(r.reason, "Invalid HTTP byte range response") == 0);

  /* Interrupted byte range resumes at the retained offset. */
  r = run(MODE_INTERRUPTED, "/file", false);
  CHECK(strcmp(r.kind, "downloaded") == 0 && r.identical);
  CHECK(count_lines(r.log, "GET ") == 5);
  char resume[64];
  snprintf(resume, sizeof resume, "range=%d-", (FILE_SIZE / 4 + 1) / 2);
  CHECK(strstr(r.log, resume) != NULL); /* The retry starts after the retained half of span one. */

  /* Whole-file degradation rejects a representation changed after range rejection. */
  r = run(MODE_CHANGED_AFTER_REJECT, "/file", false);
  CHECK(strcmp(r.kind, "fallback") == 0 && !r.published);
  CHECK(strcmp(r.reason, "HTTP download status 412") == 0);

  r = run(MODE_TRUNCATED_WHOLE, "/file", false);
  CHECK(strcmp(r.kind, "fallback") == 0 && !r.published);

  r = run(MODE_REDIRECT_SAME, "/start", true);
  CHECK(strcmp(r.kind, "downloaded") == 0 && r.published && r.length == 5);
  r = run(MODE_REDIRECT_AWAY, "/start", true);
  CHECK(strcmp(r.kind, "fallback") == 0 && !r.published);

  r = run(MODE_MISSING, "/file", true);
  CHECK(strcmp(r.kind, "fallback") == 0 && strcmp(r.reason, "HTTP download status 404") == 0);

  /* An over-limit body falls back instead of crashing the client. */
  r = run(MODE_NO_ACCEPT, "/file", true);
  CHECK(strcmp(r.kind, "fallback") == 0 && !r.published);

  CHECK(host.progress_calls > 0);
  free(file_bytes);
  printf("http_downloads_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
