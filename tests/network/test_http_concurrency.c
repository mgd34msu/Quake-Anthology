/* Ports the concurrent cases of tests/network/common/http-downloads.test.ts: two files stream at
 * once and deduplicate, a large file streams four overlapping ranges, cancellation of paused range
 * bodies drains siblings and removes staging, and range rejection retries one identity-pinned
 * whole GET. A threaded in-process HTTP server stands in for Bun.serve. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <ftw.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "qa/foundation/support.h"
#include "qa/network/services/http_downloads.h"
static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum mode { MODE_OVERLAP, MODE_RANGES, MODE_PAUSE, MODE_RANGE_200, MODE_RANGE_416,
  MODE_PACKAGE, MODE_FALLBACK, MODE_FILE_CANCEL, MODE_REDIRECT, MODE_GZIP };
#define RANGE_LENGTH (1024 * 1024 + 123)
static uint8_t range_bytes[RANGE_LENGTH];
static struct server {
  enum mode mode; int listener, port; pthread_t thread;
  pthread_mutex_t lock; pthread_cond_t changed;
  int active, peak, requests, heads, wholes, spans;
  bool released, stopping, header_mismatch;
  bool started, block, ready;
  char paths[16][64]; int path_count;
} server;

static void send_all(int fd, const void *data, size_t length) {
  const char *p = data;
  while (length > 0) {
    ssize_t n = send(fd, p, length, MSG_NOSIGNAL);
    if (n <= 0) return;
    p += n; length -= (size_t)n;
  }
}
static bool header_value(const char *request, const char *name, char *out, size_t size) {
  size_t n = strlen(name);
  for (const char *line = strstr(request, "\r\n"); line && line[2]; line = strstr(line + 2, "\r\n")) {
    const char *start = line + 2;
    if (strncasecmp(start, name, n) == 0 && start[n] == ':') {
      start += n + 1;
      while (*start == ' ') start++;
      const char *end = strstr(start, "\r\n");
      size_t length = end ? (size_t)(end - start) : strlen(start);
      if (length >= size) length = size - 1;
      memcpy(out, start, length);
      out[length] = '\0';
      return true;
    }
  }
  return false;
}
static void wait_release(void) {
  pthread_mutex_lock(&server.lock);
  while (!server.released && !server.stopping) pthread_cond_wait(&server.changed, &server.lock);
  pthread_mutex_unlock(&server.lock);
}
static void range_headers(char *out, size_t size, int status, long start, long end) {
  if (status == 206)
    snprintf(out, size, "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nETag: \"version-one\"\r\n"
        "Content-Range: bytes %ld-%ld/%d\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
        start, end, RANGE_LENGTH, end - start + 1);
  else
    snprintf(out, size, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nETag: \"version-one\"\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n", RANGE_LENGTH);
}
static uint8_t gzip_payload[2048], gzip_body[2048];
static size_t gzip_payload_length, gzip_body_length;
static void finish_connection(int fd) {
  shutdown(fd, SHUT_WR);
  char drain[256];
  while (recv(fd, drain, sizeof drain, 0) > 0) {}
  close(fd);
}
static void send_text(int fd, int status, const char *reason, const char *extra, const char *body) {
  char head[512];
  snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\n%sContent-Length: %zu\r\nConnection: close\r\n\r\n",
      status, reason, extra, strlen(body));
  send_all(fd, head, strlen(head));
  send_all(fd, body, strlen(body));
}
/* One chunk now, then the rest only after release (TS ReadableStream awaiting a deferred). */
static void send_held(int fd) {
  const char *head = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n1\r\n\x01\r\n";
  send_all(fd, head, strlen(head));
  pthread_mutex_lock(&server.lock);
  server.started = true;
  pthread_cond_broadcast(&server.changed);
  pthread_mutex_unlock(&server.lock);
  wait_release();
  send_all(fd, "0\r\n\r\n", 5);
}
static void serve_scripted(int fd, const char *request) {
  char path[64] = "", value[256];
  sscanf(request, "%*s %63s", path);
  pthread_mutex_lock(&server.lock);
  if (server.path_count < 16) snprintf(server.paths[server.path_count++], 64, "%s", path);
  pthread_mutex_unlock(&server.lock);
  switch (server.mode) {
    case MODE_PACKAGE: send_text(fd, 200, "OK", "", "package"); break;
    case MODE_FALLBACK:
      if (strcmp(path, "/missing") == 0) send_text(fd, 404, "Not Found", "", "missing");
      else send_held(fd);
      break;
    case MODE_FILE_CANCEL:
      if (strcmp(path, "/first") == 0 && server.block) send_held(fd);
      else send_text(fd, 200, "OK", "", "ok");
      break;
    case MODE_REDIRECT:
      if (strcmp(path, "/redirect") == 0) send_text(fd, 302, "Found", "Location: /asset\r\n", "");
      else if (strcmp(path, "/escape") == 0) send_text(fd, 302, "Found", "Location: http://127.0.0.1:1/asset\r\n", "");
      else if (server.ready) send_text(fd, 200, "OK", "", "complete");
      else send_text(fd, 503, "Unavailable", "", "");
      break;
    case MODE_GZIP: {
      /* Deliberately ignore the identity request, as a misconfigured mirror can. */
      if (!header_value(request, "Accept-Encoding", value, sizeof value) || strcmp(value, "identity") != 0)
        server.header_mismatch = true;
      char head[256];
      snprintf(head, sizeof head, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
          gzip_body_length);
      send_all(fd, head, strlen(head));
      send_all(fd, gzip_body, gzip_body_length);
      break;
    }
    default: break;
  }
  finish_connection(fd);
}
static void *serve_one(void *arg) {
  int fd = (int)(intptr_t)arg;
  char request[8192];
  size_t used = 0;
  while (used + 1 < sizeof request) {
    ssize_t n = recv(fd, request + used, sizeof request - 1 - used, 0);
    if (n <= 0) break;
    used += (size_t)n;
    request[used] = '\0';
    if (strstr(request, "\r\n\r\n")) break;
  }
  request[used] = '\0';
  bool head = strncmp(request, "HEAD ", 5) == 0;
  if (server.mode >= MODE_PACKAGE) { serve_scripted(fd, request); return NULL; }
  char value[256], headers[512];
  pthread_mutex_lock(&server.lock);
  server.requests++;
  if (head) server.heads++;
  pthread_mutex_unlock(&server.lock);
  if (server.mode == MODE_OVERLAP) {
    pthread_mutex_lock(&server.lock);
    server.active++;
    if (server.active > server.peak) server.peak = server.active;
    pthread_cond_broadcast(&server.changed);
    pthread_mutex_unlock(&server.lock);
    wait_release();
    pthread_mutex_lock(&server.lock);
    server.active--;
    pthread_mutex_unlock(&server.lock);
    const char *reply = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "2\r\n\x01\x02\r\n1\r\n\x03\r\n0\r\n\r\n";
    send_all(fd, reply, strlen(reply));
  } else if (head) {
    range_headers(headers, sizeof headers, 200, 0, 0);
    send_all(fd, headers, strlen(headers));
  } else {
    long start = -1, end = -1;
    bool ranged = header_value(request, "Range", value, sizeof value) && sscanf(value, "bytes=%ld-%ld", &start, &end) == 2;
    if (!header_value(request, "Accept-Encoding", value, sizeof value) || strcmp(value, "identity") != 0)
      server.header_mismatch = true;
    if (ranged && (!header_value(request, "If-Range", value, sizeof value) || strcmp(value, "\"version-one\"") != 0))
      server.header_mismatch = true;
    if (server.mode == MODE_RANGE_200 || server.mode == MODE_RANGE_416) {
      if (ranged && server.mode == MODE_RANGE_200) {
        range_headers(headers, sizeof headers, 200, 0, 0);
        send_all(fd, headers, strlen(headers));
        send_all(fd, range_bytes, RANGE_LENGTH);
      } else if (ranged) {
        const char *reply = "HTTP/1.1 416 Range Not Satisfiable\r\nETag: \"version-one\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(fd, reply, strlen(reply));
      } else {
        pthread_mutex_lock(&server.lock);
        server.wholes++;
        pthread_mutex_unlock(&server.lock);
        if (!header_value(request, "If-Match", value, sizeof value) || strcmp(value, "\"version-one\"") != 0)
          server.header_mismatch = true;
        range_headers(headers, sizeof headers, 200, 0, 0);
        send_all(fd, headers, strlen(headers));
        send_all(fd, range_bytes, RANGE_LENGTH);
      }
    } else if (ranged) {
      range_headers(headers, sizeof headers, 206, start, end);
      send_all(fd, headers, strlen(headers));
      pthread_mutex_lock(&server.lock);
      server.spans++;
      server.active++;
      if (server.active > server.peak) server.peak = server.active;
      pthread_cond_broadcast(&server.changed);
      pthread_mutex_unlock(&server.lock);
      send_all(fd, range_bytes + start, 32768);
      wait_release();
      if (!server.stopping) send_all(fd, range_bytes + start + 32768, (size_t)(end - start + 1 - 32768));
      pthread_mutex_lock(&server.lock);
      server.active--;
      pthread_mutex_unlock(&server.lock);
    }
  }
  shutdown(fd, SHUT_WR);
  char drain[256];
  while (recv(fd, drain, sizeof drain, 0) > 0) {}
  close(fd);
  return NULL;
}
static void *accept_loop(void *arg) {
  (void)arg;
  for (;;) {
    int fd = accept(server.listener, NULL, NULL);
    if (fd < 0) return NULL;
    if (server.stopping) { close(fd); return NULL; }
    pthread_t thread;
    pthread_create(&thread, NULL, serve_one, (void *)(intptr_t)fd);
    pthread_detach(thread);
  }
}
static void server_start(enum mode mode) {
  pthread_mutex_destroy(&server.lock);
  memset(&server, 0, sizeof server);
  server.mode = mode;
  pthread_mutex_init(&server.lock, NULL);
  pthread_cond_init(&server.changed, NULL);
  server.listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(server.listener, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(server.listener, 64) != 0) abort();
  socklen_t size = sizeof addr;
  getsockname(server.listener, (struct sockaddr *)&addr, &size);
  server.port = ntohs(addr.sin_port);
  pthread_create(&server.thread, NULL, accept_loop, NULL);
}
static void server_release(void) {
  pthread_mutex_lock(&server.lock);
  server.released = true;
  pthread_cond_broadcast(&server.changed);
  pthread_mutex_unlock(&server.lock);
}
static void server_stop(void) {
  pthread_mutex_lock(&server.lock);
  server.stopping = true;
  pthread_cond_broadcast(&server.changed);
  pthread_mutex_unlock(&server.lock);
  shutdown(server.listener, SHUT_RDWR);
  close(server.listener);
  pthread_join(server.thread, NULL);
  /* Detached connection threads see `stopping` and finish their sends. */
  struct timespec pause = {0, 50 * 1000 * 1000};
  nanosleep(&pause, NULL);
}
static int server_value(const int *field) {
  pthread_mutex_lock(&server.lock);
  int value = *field;
  pthread_mutex_unlock(&server.lock);
  return value;
}

/* --- queue host --- */
static struct host {
  bool retired;
  double progress[4096]; size_t progress_count;
  bool total_mismatch; double expected_total;
} host;
static void assert_current(void *ctx) {
  CHECK(ctx == &host);
  if (host.retired) qa_range_error("Retired epoch");
}
static bool mounted;
static int paths_at_refresh = -1;
static bool resolved(void *ctx, const char *path) {
  CHECK(ctx == &host);
  return mounted && strcmp(path, "covered") == 0;
}
static void refresh(void *ctx, const char *path) {
  (void)path;
  CHECK(ctx == &host);
  paths_at_refresh = server.path_count;
  mounted = true;
}
static void progress(void *ctx, const char *path, double received, bool has_total, double total) {
  (void)path;
  CHECK(ctx == &host);
  if (host.expected_total > 0 && (!has_total || total != host.expected_total)) host.total_mismatch = true;
  if (host.progress_count < 4096) host.progress[host.progress_count++] = received;
}
static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
  (void)sb; (void)flag; (void)ftw;
  return remove(path);
}
static char root[128];
static double budget_bytes, queue_concurrency;
static qa_http_download_queue *open_queue(void) {
  memset(&host, 0, sizeof host);
  mkdir("build", 0755); mkdir("build/agents", 0755); mkdir("build/agents/network2", 0755);
  mkdir("build/agents/network2/tmp", 0755);
  snprintf(root, sizeof root, "build/agents/network2/tmp/qa-httpc-XXXXXX");
  if (!mkdtemp(root)) abort();
  qa_http_queue_options options;
  memset(&options, 0, sizeof options);
  options.root = root;
  options.ctx = &host;
  options.assert_current = assert_current;
  options.resolved = resolved;
  options.refresh_package = refresh;
  options.progress = progress;
  if (budget_bytes > 0) { options.has_total_budget_bytes = true; options.total_budget_bytes = budget_bytes; }
  if (queue_concurrency > 0) { options.has_concurrency = true; options.concurrency = queue_concurrency; }
  return qa_http_download_queue_create(options);
}
static void close_queue(qa_http_download_queue *queue) {
  qa_http_download_queue_cancel(queue);
  qa_http_download_queue_free(queue);
  nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}
static qa_http_download_request request(const char *path, char *url, size_t url_size, double maximum) {
  snprintf(url, url_size, "http://127.0.0.1:%d/%s", server.port, path);
  qa_http_download_request r;
  memset(&r, 0, sizeof r);
  r.path = path; r.url = url; r.kind = "asset";
  r.expected.is_protocol = true;
  r.expected.maximum_bytes = maximum;
  return r;
}
static size_t list_root(char names[8][64]) {
  size_t count = 0;
  DIR *dir = opendir(root);
  for (struct dirent *entry; dir && (entry = readdir(dir));) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (count < 8) snprintf(names[count], 64, "%.63s", entry->d_name);
    count++;
  }
  if (dir) closedir(dir);
  return count;
}
static bool file_equals(const char *name, const uint8_t *bytes, size_t length) {
  char path[256];
  snprintf(path, sizeof path, "%s/%s", root, name);
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  uint8_t *data = malloc(length + 1);
  size_t got = fread(data, 1, length + 1, file);
  fclose(file);
  bool same = got == length && memcmp(data, bytes, length) == 0;
  free(data);
  return same;
}
static const char *result_kind(qa_http_download_queue *queue, const char *path) {
  qa_http_download_result result;
  if (!qa_http_download_queue_result(queue, path, &result)) return "pending";
  const char *kind = result.kind;
  qa_http_download_result_destroy(&result);
  return kind;
}
/* Polls until `done` or ~20 s. */
static bool poll_until(qa_http_download_queue *queue, bool (*done)(void *), void *ctx) {
  for (int i = 0; i < 1000; i++) {
    if (done(ctx)) return true;
    qa_http_download_queue_poll(queue, 20);
  }
  return done(ctx);
}
static bool two_active(void *ctx) { (void)ctx; return server_value(&server.active) == 2; }
static bool four_active(void *ctx) { (void)ctx; return server_value(&server.active) == 4; }
static bool streamed(void *ctx) { (void)ctx; for (size_t i = 0; i < host.progress_count; i++) if (host.progress[i] > 0) return true; return false; }

static void test_overlap_and_dedupe(void) {
  server_start(MODE_OVERLAP);
  qa_http_download_queue *queue = open_queue();
  char url_a[128], url_b[128];
  qa_http_download_request a = request("a", url_a, sizeof url_a, 100), b = request("b", url_b, sizeof url_b, 100);
  qa_http_download_queue_enqueue(queue, a);
  qa_http_download_queue_enqueue(queue, a);
  qa_http_download_queue_enqueue(queue, b);
  CHECK(poll_until(queue, two_active, NULL));
  CHECK(server_value(&server.peak) == 2);
  char names[8][64];
  size_t count = list_root(names);
  for (size_t i = 0; i < count && i < 8; i++) CHECK(strcmp(names[i], "a") != 0 && strcmp(names[i], "b") != 0);
  server_release();
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "a"), "downloaded") == 0);
  CHECK(strcmp(result_kind(queue, "b"), "downloaded") == 0);
  CHECK(server_value(&server.requests) == 2);
  const uint8_t expected[] = {1, 2, 3};
  CHECK(file_equals("a", expected, 3));
  bool saw_three = false;
  for (size_t i = 0; i < host.progress_count; i++) if (host.progress[i] == 3) saw_three = true;
  CHECK(saw_three);
  close_queue(queue);
  server_stop();
}
static void test_four_overlapping_ranges(void) {
  server_start(MODE_RANGES);
  qa_http_download_queue *queue = open_queue();
  host.expected_total = RANGE_LENGTH;
  char url[128];
  qa_http_download_queue_enqueue(queue, request("large", url, sizeof url, RANGE_LENGTH));
  CHECK(poll_until(queue, four_active, NULL));
  CHECK(server_value(&server.peak) == 4);
  char names[8][64];
  size_t count = list_root(names);
  for (size_t i = 0; i < count && i < 8; i++) CHECK(strncmp(names[i], ".download-", 10) == 0);
  server_release();
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "large"), "downloaded") == 0);
  CHECK(server_value(&server.heads) == 1);
  CHECK(server_value(&server.spans) == 4);
  CHECK(!server.header_mismatch);
  CHECK(file_equals("large", range_bytes, RANGE_LENGTH));
  CHECK(host.progress_count > 1 && host.progress[0] == 0);
  CHECK(host.progress[host.progress_count - 1] == RANGE_LENGTH);
  for (size_t i = 1; i < host.progress_count; i++) CHECK(host.progress[i] >= host.progress[i - 1]);
  CHECK(!host.total_mismatch);
  count = list_root(names);
  CHECK(count == 1 && strcmp(names[0], "large") == 0);
  close_queue(queue);
  server_stop();
}
static void test_cancel_paused_ranges(void) {
  server_start(MODE_PAUSE);
  qa_http_download_queue *queue = open_queue();
  char url[128];
  qa_http_download_queue_enqueue(queue, request("large", url, sizeof url, RANGE_LENGTH));
  CHECK(poll_until(queue, streamed, NULL));
  host.retired = true;
  qa_http_download_queue_cancel(queue);
  qa_http_download_result result;
  CHECK(qa_http_download_queue_result(queue, "large", &result) == 1);
  CHECK(strcmp(result.kind, "cancelled") == 0 && result.reason == NULL);
  qa_http_download_result_destroy(&result);
  char names[8][64];
  CHECK(list_root(names) == 0);
  server_release();
  close_queue(queue);
  server_stop();
}
static void test_range_rejection_retries_whole(enum mode mode) {
  server_start(mode);
  qa_http_download_queue *queue = open_queue();
  char url[128];
  qa_http_download_queue_enqueue(queue, request("large", url, sizeof url, RANGE_LENGTH));
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "large"), "downloaded") == 0);
  CHECK(server_value(&server.wholes) == 1);
  CHECK(!server.header_mismatch);
  CHECK(file_equals("large", range_bytes, RANGE_LENGTH));
  char names[8][64];
  size_t count = list_root(names);
  CHECK(count == 1 && strcmp(names[0], "large") == 0);
  close_queue(queue);
  server_stop();
}
/* TS totalBudgetBytes: the queue-wide byte budget fails the transfer that crosses it. */
static void test_total_budget(void) {
  server_start(MODE_OVERLAP);
  budget_bytes = 4;
  qa_http_download_queue *queue = open_queue();
  budget_bytes = 0;
  char url_a[128], url_b[128];
  qa_http_download_queue_enqueue(queue, request("a", url_a, sizeof url_a, 100));
  qa_http_download_queue_enqueue(queue, request("b", url_b, sizeof url_b, 100));
  CHECK(poll_until(queue, two_active, NULL));
  server_release();
  qa_http_download_queue_pump(queue);
  int downloaded = 0, quota = 0;
  for (int i = 0; i < 2; i++) {
    qa_http_download_result result;
    CHECK(qa_http_download_queue_result(queue, i ? "b" : "a", &result) == 1);
    if (strcmp(result.kind, "downloaded") == 0) downloaded++;
    if (strcmp(result.kind, "fallback") == 0 && result.reason && strcmp(result.reason, "HTTP download quota exceeded") == 0) quota++;
    qa_http_download_result_destroy(&result);
  }
  CHECK(downloaded == 1 && quota == 1);
  close_queue(queue);
  server_stop();
}
static bool server_started(void *ctx) { (void)ctx; pthread_mutex_lock(&server.lock); bool v = server.started; pthread_mutex_unlock(&server.lock); return v; }
static void test_package_barrier(void) {
  server_start(MODE_PACKAGE);
  mounted = false; paths_at_refresh = -1;
  qa_http_download_queue *queue = open_queue();
  char url_a[128], url_b[128];
  qa_http_download_queue_enqueue(queue, request("covered", url_a, sizeof url_a, 100));
  qa_http_download_request pack = request("pak.pk3", url_b, sizeof url_b, 100);
  pack.kind = "package";
  qa_http_download_queue_enqueue(queue, pack);
  qa_http_download_queue_pump(queue);
  CHECK(paths_at_refresh == 1);
  CHECK(strcmp(server.paths[0], "/pak.pk3") == 0);
  CHECK(strcmp(result_kind(queue, "pak.pk3"), "downloaded") == 0);
  CHECK(strcmp(result_kind(queue, "covered"), "resolved") == 0);
  CHECK(server.path_count == 1);
  close_queue(queue);
  mounted = false;
  server_stop();
}
static void test_fallback_and_cancel(void) {
  server_start(MODE_FALLBACK);
  queue_concurrency = 1;
  qa_http_download_queue *queue = open_queue();
  queue_concurrency = 0;
  char url[3][128];
  qa_http_download_queue_enqueue(queue, request("missing", url[0], sizeof url[0], 100));
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "missing"), "fallback") == 0);
  qa_http_download_queue_enqueue(queue, request("active", url[1], sizeof url[1], 100));
  qa_http_download_queue_enqueue(queue, request("pending", url[2], sizeof url[2], 100));
  CHECK(poll_until(queue, server_started, NULL));
  qa_http_download_queue_cancel(queue);
  server_release();
  CHECK(strcmp(result_kind(queue, "active"), "cancelled") == 0);
  CHECK(strcmp(result_kind(queue, "pending"), "cancelled") == 0);
  char names[8][64];
  CHECK(list_root(names) == 0);
  close_queue(queue);
  server_stop();
}
static void test_file_cancel_and_retry(void) {
  server_start(MODE_FILE_CANCEL);
  server.block = true;
  queue_concurrency = 1;
  qa_http_download_queue *queue = open_queue();
  queue_concurrency = 0;
  char url[2][128];
  qa_http_download_queue_enqueue(queue, request("first", url[0], sizeof url[0], 100));
  qa_http_download_queue_enqueue(queue, request("second", url[1], sizeof url[1], 100));
  CHECK(poll_until(queue, server_started, NULL));
  qa_http_download_queue_cancel_file(queue, "first");
  server_release();
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "first"), "cancelled") == 0);
  CHECK(strcmp(result_kind(queue, "second"), "downloaded") == 0);
  server.block = false;
  qa_http_download_queue_retry(queue, "first");
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "first"), "downloaded") == 0);
  char names[8][64];
  size_t count = list_root(names);
  CHECK(count == 2);
  close_queue(queue);
  server_stop();
}
static void test_redirect_and_retry(void) {
  server_start(MODE_REDIRECT);
  qa_http_download_queue *queue = open_queue();
  char url[2][128];
  qa_http_download_queue_enqueue(queue, request("redirect", url[0], sizeof url[0], 100));
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "redirect"), "fallback") == 0);
  server.ready = true;
  qa_http_download_queue_retry(queue, "redirect");
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "redirect"), "downloaded") == 0);
  CHECK(file_equals("redirect", (const uint8_t *)"complete", 8));
  qa_http_download_queue_enqueue(queue, request("escape", url[1], sizeof url[1], 100));
  qa_http_download_queue_pump(queue);
  qa_http_download_result result;
  CHECK(qa_http_download_queue_result(queue, "escape", &result) == 1);
  CHECK(strcmp(result.kind, "fallback") == 0 && result.reason
      && strcmp(result.reason, "HTTP download redirect left advertised origin") == 0);
  qa_http_download_result_destroy(&result);
  bool found = false;
  for (size_t i = 0; i < qa_http_download_queue_progress_count(queue); i++) {
    qa_download_progress item = qa_http_download_queue_progress_at(queue, i);
    if (strcmp(item.path, "redirect") == 0) {
      found = true;
      CHECK(strcmp(item.phase, "done") == 0 && item.received == 8 && strcmp(item.result, "downloaded") == 0);
    }
  }
  CHECK(found);
  close_queue(queue);
  server_stop();
}
static void test_gzip_whole_file(void) {
  gzip_payload_length = 0;
  for (int i = 0; i < 85; i++) { memcpy(gzip_payload + gzip_payload_length, "quake asset payload ", 20); gzip_payload_length += 20; }
  z_stream z;
  memset(&z, 0, sizeof z);
  deflateInit2(&z, 9, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
  z.next_in = gzip_payload; z.avail_in = (uInt)gzip_payload_length;
  z.next_out = gzip_body; z.avail_out = sizeof gzip_body;
  deflate(&z, Z_FINISH);
  gzip_body_length = z.total_out;
  deflateEnd(&z);
  CHECK(gzip_body_length < gzip_payload_length);
  server_start(MODE_GZIP);
  qa_http_download_queue *queue = open_queue();
  char url[128];
  qa_http_download_queue_enqueue(queue, request("asset", url, sizeof url, 4096));
  qa_http_download_queue_pump(queue);
  CHECK(strcmp(result_kind(queue, "asset"), "downloaded") == 0);
  CHECK(!server.header_mismatch);
  CHECK(file_equals("asset", gzip_payload, gzip_payload_length));
  close_queue(queue);
  server_stop();
}
int main(void) {
  for (size_t i = 0; i < RANGE_LENGTH; i++) range_bytes[i] = (uint8_t)(i * 17 % 251);
  pthread_mutex_init(&server.lock, NULL);
  test_overlap_and_dedupe();
  test_four_overlapping_ranges();
  test_cancel_paused_ranges();
  test_range_rejection_retries_whole(MODE_RANGE_200);
  test_range_rejection_retries_whole(MODE_RANGE_416);
  test_total_budget();
  test_package_barrier();
  test_fallback_and_cancel();
  test_file_cancel_and_retry();
  test_redirect_and_retry();
  test_gzip_whole_file();
  rmdir("build/agents/network2/tmp");
  if (failures) { fprintf(stderr, "%d failures of %d checks\n", failures, checks); return 1; }
  printf("test_http_concurrency: %d checks passed\n", checks);
  return 0;
}
