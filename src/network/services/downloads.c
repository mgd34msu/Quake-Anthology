/* Port of src/network/services/downloads.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/services/downloads.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/network/services/http_downloads.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool is_safe_d(double v) { return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0; }
int qa_download_file_open(qa_download_file *f, const char *root, const char *name) {
  char *leaf = NULL;
  int parent = qa_open_contained_parent(root, name, false, &leaf);
  if (parent < 0) return 0;
  int fd = openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  free(leaf);
  if (fd < 0) { close(parent); qa_range_error("Download source is not a regular bounded file"); }
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) { close(fd); close(parent); qa_range_error("Download source is not a regular bounded file"); }
  close(parent);
  f->fd = fd; f->byte_length = (double)info.st_size; f->ended = false;
  return 1;
}
uint8_t *qa_download_file_read(qa_download_file *f, double offset, double max_bytes, size_t *out_len) {
  if (f->ended) qa_range_error("Download source is closed");
  if (!is_safe_d(offset) || offset < 0 || offset > f->byte_length || !is_safe_d(max_bytes) || max_bytes < 0)
    qa_range_error("Invalid download range");
  struct stat info;
  if (fstat(f->fd, &info) != 0 || (double)info.st_size != f->byte_length)
    qa_range_error("Download changed size while open");
  size_t want = (size_t)fmin(max_bytes, f->byte_length - offset);
  uint8_t *bytes = malloc(want ? want : 1);
  size_t done = 0;
  while (done < want) {
    ssize_t n = pread(f->fd, bytes + done, want - done, (off_t)(offset + (double)done));
    if (n == 0) { free(bytes); qa_range_error("Unexpected download EOF"); }
    if (n < 0) { if (errno == EINTR) continue; free(bytes); qa_range_error("Unexpected download EOF"); }
    done += (size_t)n;
  }
  *out_len = want; return bytes;
}
void qa_download_file_close(qa_download_file *f) {
  if (!f->ended) { f->ended = true; close(f->fd); }
}
static uint8_t *file_src_read(void *ctx, double off, double maxb, size_t *out) { return qa_download_file_read(ctx, off, maxb, out); }
static void file_src_close(void *ctx) { qa_download_file_close(ctx); }
qa_download_source qa_download_file_as_source(qa_download_file *f) {
  qa_download_source s; s.ctx = f; s.byte_length = f->byte_length; s.read = file_src_read; s.close = file_src_close; return s;
}

static double sink_limit(qa_download_expectation e) { return e.is_protocol ? e.maximum_bytes : e.byte_length; }
static int sink_open(qa_download_sink *s, const char *root, const char *name, qa_download_expectation expected, bool ranged) {
  double limit = sink_limit(expected);
  if (!is_safe_d(limit) || limit < 0) qa_range_error("Invalid download size");
  memset(s, 0, sizeof *s);
  char *leaf = NULL;
  int parent = qa_open_contained_parent(root, name, true, &leaf);
  if (parent < 0) return 0;
  /* Temporary name: randomUUID -> pid+counter (uniqueness only). */
  static unsigned counter = 0;
  char tmpname[96];
  snprintf(tmpname, sizeof tmpname, ".download-%ld-%u", (long)getpid(), ++counter);
  int fd = openat(parent, tmpname, (ranged ? O_RDWR : O_WRONLY) | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (fd < 0) { close(parent); free(leaf); qa_range_error("Download write made no progress"); }
  s->parent_fd = parent; s->fd = fd;
  size_t tn = strlen(tmpname) + 1; s->temporary = malloc(tn); memcpy(s->temporary, tmpname, tn);
  s->target = leaf; s->expected = expected; s->ranged = ranged;
  /* Full paths for link/unlink via /proc/self/fd, mirroring TS. */
  char pbuf[512]; snprintf(pbuf, sizeof pbuf, "/proc/self/fd/%d", parent);
  size_t pn = strlen(pbuf) + 1; s->parent_path = malloc(pn); memcpy(s->parent_path, pbuf, pn);
  return 1;
}
int qa_download_sink_create(qa_download_sink *s, const char *root, const char *name, qa_download_expectation expected) {
  return sink_open(s, root, name, expected, false);
}
static int cmp_span(const void *a, const void *b) {
  double x = ((const qa_download_span *)a)->start, y = ((const qa_download_span *)b)->start;
  return x < y ? -1 : x > y ? 1 : 0;
}
int qa_download_sink_create_ranged(qa_download_sink *s, const char *root, const char *name,
                                   qa_download_expectation expected, double total,
                                   const qa_download_span *spans, size_t span_count) {
  double limit = sink_limit(expected);
  if (!is_safe_d(limit) || limit < 0 || !is_safe_d(total) || total < 0 || total > limit
      || (!expected.is_protocol && total != expected.byte_length))
    qa_range_error("Invalid ranged download size");
  qa_download_span *sorted = malloc((span_count ? span_count : 1) * sizeof *sorted);
  memcpy(sorted, spans, span_count * sizeof *sorted);
  qsort(sorted, span_count, sizeof *sorted, cmp_span);
  double next = 0;
  for (size_t i = 0; i < span_count; i++) {
    if (!is_safe_d(sorted[i].start) || !is_safe_d(sorted[i].end) || sorted[i].start != next || sorted[i].end < sorted[i].start || sorted[i].end >= total) {
      free(sorted); qa_range_error("Download spans must partition the complete file");
    }
    next = sorted[i].end + 1;
  }
  if (next != total) { free(sorted); qa_range_error("Download spans must partition the complete file"); }
  if (!sink_open(s, root, name, expected, true)) { free(sorted); return 0; }
  s->total = total; s->span_count = span_count;
  s->spans = malloc((span_count ? span_count : 1) * sizeof *s->spans);
  for (size_t i = 0; i < span_count; i++) { s->spans[i].start = sorted[i].start; s->spans[i].end = sorted[i].end; s->spans[i].written = 0; }
  free(sorted);
  return 1;
}
void qa_download_sink_destroy(qa_download_sink *s) {
  qa_download_sink_close(s);
  free(s->temporary); free(s->target); free(s->parent_path); free(s->spans);
  memset(s, 0, sizeof *s);
}
double qa_download_sink_byte_length(const qa_download_sink *s) { return s->count; }
qa_download_expectation qa_download_sink_get_expected(const qa_download_sink *s) { return s->expected; }
static void write_all_at(int fd, const uint8_t *bytes, size_t length, bool at, off_t pos, size_t *progress) {
  size_t written = 0;
  while (written < length) {
    ssize_t n = at ? pwrite(fd, bytes + written, length - written, pos + (off_t)written)
                   : write(fd, bytes + written, length - written);
    if (n == 0) qa_range_error("Download write made no progress");
    if (n < 0) { if (errno == EINTR) continue; qa_range_error("Download write made no progress"); }
    written += (size_t)n; if (progress) *progress = written;
  }
}
void qa_download_sink_append(qa_download_sink *s, const uint8_t *bytes, size_t length) {
  if (s->ranged) qa_range_error("Ranged downloads require appendRange");
  if (s->ended) qa_range_error("Download sink is closed");
  if (s->inspecting) qa_range_error("Download inspection is in progress");
  if (s->count + (double)length > sink_limit(s->expected)) qa_range_error("Download exceeds expected size");
  write_all_at(s->fd, bytes, length, false, 0, NULL);
  s->count += (double)length;
}
void qa_download_sink_append_range(qa_download_sink *s, double span_start, const uint8_t *bytes, size_t length) {
  if (!s->ranged) qa_range_error("Sequential downloads require append");
  if (s->ended) qa_range_error("Download sink is closed");
  if (s->inspecting) qa_range_error("Download inspection is in progress");
  struct qa_download_span_state *span = NULL;
  for (size_t i = 0; i < s->span_count; i++) if (s->spans[i].start == span_start) span = &s->spans[i];
  if (!span) qa_range_error("Unknown download span");
  if ((double)length > span->end - span_start + 1 - span->written) qa_range_error("Download exceeds span size");
  size_t progress = 0;
  write_all_at(s->fd, bytes, length, true, (off_t)(span_start + span->written), &progress);
  span->written += (double)progress; s->count += (double)progress;
}
static void require_complete_ranges(qa_download_sink *s) {
  if (!s->ranged) return;
  bool bad = s->count != s->total;
  for (size_t i = 0; i < s->span_count; i++) if (s->spans[i].written != s->spans[i].end - s->spans[i].start + 1) bad = true;
  if (bad) qa_range_error("Ranged download is incomplete");
  struct stat info;
  if (fstat(s->fd, &info) != 0 || (double)info.st_size != s->total)
    qa_range_error("Ranged download staged size changed");
}
static char *sink_digest(qa_download_sink *s) {
  /* Hash staged file contents (sequential appends wrote them in order). */
  if (s->ranged || true) {
    off_t size = lseek(s->fd, 0, SEEK_END);
    if (size < 0) { /* sequential write-only fd: reopen via parent/tmp name */
      int rfd = openat(s->parent_fd, s->temporary, O_RDONLY);
      if (rfd < 0) qa_range_error("Unexpected ranged download EOF");
      struct stat info; fstat(rfd, &info); size = info.st_size;
      uint8_t *all = malloc(size > 0 ? (size_t)size : 1);
      size_t done = 0;
      while (done < (size_t)size) { ssize_t n = read(rfd, all + done, (size_t)size - done); if (n <= 0) break; done += (size_t)n; }
      close(rfd);
      char hex[65]; qa_net_sha256_hex(all, done, hex); free(all);
      return qa_create_content_digest(hex);
    }
    uint8_t *all = malloc(size > 0 ? (size_t)size : 1);
    size_t done = 0;
    while (done < (size_t)size) {
      ssize_t n = pread(s->fd, all + done, (size_t)size - done, (off_t)done);
      if (n == 0) qa_range_error("Unexpected ranged download EOF");
      if (n < 0) { if (errno == EINTR) continue; break; }
      done += (size_t)n;
    }
    char hex[65]; qa_net_sha256_hex(all, done, hex); free(all);
    return qa_create_content_digest(hex);
  }
}
char *qa_download_sink_finish(qa_download_sink *s) {
  if (s->ended) qa_range_error("Download sink is closed");
  if (s->inspecting) qa_range_error("Download inspection is in progress");
  char *digest = NULL;
  /* TS closes in finally; errors abort here, close still runs on success path. */
  require_complete_ranges(s);
  if (!s->expected.is_protocol && s->count != s->expected.byte_length) {
    qa_download_sink_close(s);
    qa_range_error("Download size differs from content identity");
  }
  digest = sink_digest(s);
  if (!s->expected.is_protocol && strcmp(digest, s->expected.digest) != 0) {
    free(digest); qa_download_sink_close(s);
    qa_range_error("Download checksum differs from content identity");
  }
  char from[1024], to[1024];
  snprintf(from, sizeof from, "%s/%s", s->parent_path, s->temporary);
  snprintf(to, sizeof to, "%s/%s", s->parent_path, s->target);
  if (link(from, to) != 0) { free(digest); qa_download_sink_close(s); qa_range_error("Download write made no progress"); }
  qa_download_sink_close(s);
  return digest;
}
void qa_download_sink_inspect_staged(qa_download_sink *s, qa_download_inspect_fn inspect, void *ctx) {
  if (s->ended) qa_range_error("Download sink is closed");
  if (s->inspecting) qa_range_error("Download inspection is in progress");
  require_complete_ranges(s);
  char selfpath[128]; snprintf(selfpath, sizeof selfpath, "/proc/self/fd/%d", s->fd);
  int retained = open(selfpath, O_RDONLY);
  if (retained < 0) qa_range_error("Download sink is closed");
  s->inspecting = true;
  char rpath[128]; snprintf(rpath, sizeof rpath, "/proc/self/fd/%d", retained);
  inspect(ctx, rpath);
  bool was_ended = s->ended;
  s->inspecting = false; close(retained);
  if (was_ended) qa_range_error("Download sink closed during inspection");
}
void qa_download_sink_close(qa_download_sink *s) {
  if (s->ended) return;
  s->ended = true;
  if (s->fd >= 0) close(s->fd);
  if (s->parent_path && s->temporary) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", s->parent_path, s->temporary);
    unlink(path);
  }
  if (s->parent_fd >= 0) close(s->parent_fd);
  s->fd = -1; s->parent_fd = -1;
}
struct qa_download_window {
  qa_download_source source; double block_bytes, window_blocks;
  struct { double index; uint8_t *bytes; size_t length; } *blocks; size_t block_count, block_cap;
  double read_offset, current_block, client_block, transmit_block, send_time;
  bool eof, ended;
};
int qa_download_window_init(qa_download_window *w, qa_download_source source, double block_bytes, double window_blocks) {
  if (!is_safe_d(block_bytes) || block_bytes < 1 || !is_safe_d(window_blocks) || window_blocks < 1)
    qa_range_error("Invalid download window");
  memset(w, 0, sizeof *w); w->source = source; w->block_bytes = block_bytes; w->window_blocks = window_blocks;
  return 1;
}
void qa_download_window_destroy(qa_download_window *w) {
  qa_download_window_close(w);
  free(w->blocks); memset(w, 0, sizeof *w);
}
bool qa_download_window_closed(const qa_download_window *w) { return w->ended; }
static void window_put(qa_download_window *w, double index, uint8_t *bytes, size_t length) {
  if (w->block_count == w->block_cap) { size_t nc = w->block_cap ? w->block_cap * 2 : 8; w->blocks = realloc(w->blocks, nc * sizeof *w->blocks); w->block_cap = nc; }
  w->blocks[w->block_count].index = index; w->blocks[w->block_count].bytes = bytes; w->blocks[w->block_count].length = length; w->block_count++;
}
static int window_find(qa_download_window *w, double index) {
  for (size_t i = 0; i < w->block_count; i++) if (w->blocks[i].index == index) return (int)i;
  return -1;
}
qa_download_block *qa_download_window_packets(qa_download_window *w, double now, double count, size_t *out_count) {
  if (w->ended) qa_range_error("Download window is closed");
  while (w->current_block - w->client_block < w->window_blocks && w->read_offset < w->source.byte_length) {
    size_t blen = 0;
    uint8_t *bytes = w->source.read(w->source.ctx, w->read_offset, w->block_bytes, &blen);
    if (blen == 0) { free(bytes); qa_range_error("Unexpected download EOF"); }
    window_put(w, w->current_block++, bytes, blen);
    w->read_offset += (double)blen;
  }
  if (w->read_offset == w->source.byte_length && !w->eof && w->current_block - w->client_block < w->window_blocks) {
    window_put(w, w->current_block++, malloc(1), 0); w->eof = true;
  }
  size_t cap = 4, n = 0;
  qa_download_block *packets = calloc(cap, sizeof *packets);
  double remaining = count;
  while (remaining-- > 0 && w->client_block != w->current_block) {
    if (w->transmit_block == w->current_block) {
      if (now - w->send_time <= 1000) break;
      w->transmit_block = w->client_block;
    }
    int at = window_find(w, w->transmit_block);
    if (at < 0) qa_range_error("Download block was not retained");
    if (n == cap) { cap *= 2; packets = realloc(packets, cap * sizeof *packets); }
    packets[n].index = w->transmit_block;
    packets[n].has_total = w->transmit_block == 0;
    packets[n].total_bytes = w->source.byte_length;
    packets[n].length = w->blocks[at].length;
    packets[n].bytes = malloc(packets[n].length ? packets[n].length : 1);
    memcpy(packets[n].bytes, w->blocks[at].bytes, packets[n].length);
    n++;
    w->transmit_block++; w->send_time = now;
  }
  *out_count = n; return packets;
}
const char *qa_download_window_acknowledge(qa_download_window *w, double block, double now) {
  if (w->ended || block != w->client_block || block >= w->transmit_block)
    qa_range_error("Broken download acknowledgement");
  int at = window_find(w, block);
  if (at < 0) qa_range_error("Missing download block");
  size_t blen = w->blocks[at].length;
  free(w->blocks[at].bytes);
  memmove(&w->blocks[at], &w->blocks[at+1], (w->block_count - (size_t)at - 1) * sizeof *w->blocks);
  w->block_count--;
  if (blen == 0) { qa_download_window_close(w); return "complete"; }
  w->client_block++; w->send_time = now; return "continue";
}
void qa_download_window_close(qa_download_window *w) {
  if (w->ended) return;
  w->ended = true;
  for (size_t i = 0; i < w->block_count; i++) free(w->blocks[i].bytes);
  w->block_count = 0;
  w->source.close(w->source.ctx);
}
char *qa_download_http(const char *url, qa_download_sink *sink) {
  return qa_http_download_to_sink(url, sink);
}
const char **qa_verify_pure_content(const char *const *required, size_t required_count,
                                    const char *const *available, size_t available_count, size_t *out_count) {
  const char **out = malloc((required_count ? required_count : 1) * sizeof *out);
  size_t n = 0;
  for (size_t i = 0; i < required_count; i++) {
    bool found = false;
    for (size_t j = 0; j < available_count; j++) if (strcmp(required[i], available[j]) == 0) found = true;
    if (!found) out[n++] = required[i];
  }
  *out_count = n; return out;
}
