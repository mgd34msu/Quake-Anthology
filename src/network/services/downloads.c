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
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
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
  if (f->ended) qa_raise("Error", 0, 0, "Download source is closed");
  if (!is_safe_d(offset) || offset < 0 || offset > f->byte_length || !is_safe_d(max_bytes) || max_bytes < 0)
    qa_range_error("Invalid download range");
  struct stat info;
  if (fstat(f->fd, &info) != 0 || (double)info.st_size != f->byte_length)
    qa_raise("Error", 0, 0, "Download changed size while open");
  size_t want = (size_t)qa_js_min(max_bytes, f->byte_length - offset);
  uint8_t *bytes = malloc(want ? want : 1);
  if (!bytes) qa_range_error("Out of memory");
  size_t done = 0;
  while (done < want) {
    ssize_t n = pread(f->fd, bytes + done, want - done, (off_t)(offset + (double)done));
    if (n == 0) { free(bytes); qa_raise("Error", 0, 0, "Unexpected download EOF"); }
    if (n < 0) { int code = errno; if (code == EINTR) continue; free(bytes); qa_raise("Error", code, 0, "%s, read", strerror(code)); }
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
  if (fd < 0) { int code = errno; close(parent); free(leaf); qa_raise("Error", code, 0, "%s, open", strerror(code)); }
  s->parent_fd = parent; s->fd = fd;
  size_t tn = strlen(tmpname) + 1; s->temporary = malloc(tn); memcpy(s->temporary, tmpname, tn);
  s->target = leaf; s->expected = expected; s->ranged = ranged;
  qa_net_sha256_init(&s->hash);
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
  if (span_count) memcpy(sorted, spans, span_count * sizeof *sorted);
  qa_js_sort(sorted, span_count, sizeof *sorted, cmp_span);
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
    if (n == 0) qa_raise("Error", 0, 0, "Download write made no progress");
    if (n < 0) { int code = errno; if (code == EINTR) continue; qa_raise("Error", code, 0, "%s, write", strerror(code)); }
    written += (size_t)n; if (progress) *progress = written;
  }
}
void qa_download_sink_append(qa_download_sink *s, const uint8_t *bytes, size_t length) {
  if (s->ranged) qa_raise("Error", 0, 0, "Ranged downloads require appendRange");
  if (s->ended) qa_raise("Error", 0, 0, "Download sink is closed");
  if (s->inspecting) qa_raise("Error", 0, 0, "Download inspection is in progress");
  if (s->count + (double)length > sink_limit(s->expected)) qa_range_error("Download exceeds expected size");
  write_all_at(s->fd, bytes, length, false, 0, NULL);
  qa_net_sha256_update(&s->hash, bytes, length); s->count += (double)length;
}
void qa_download_sink_append_range(qa_download_sink *s, double span_start, const uint8_t *bytes, size_t length) {
  if (!s->ranged) qa_raise("Error", 0, 0, "Sequential downloads require append");
  if (s->ended) qa_raise("Error", 0, 0, "Download sink is closed");
  if (s->inspecting) qa_raise("Error", 0, 0, "Download inspection is in progress");
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
  if (bad) qa_raise("Error", 0, 0, "Ranged download is incomplete");
  struct stat info;
  if (fstat(s->fd, &info) != 0 || (double)info.st_size != s->total)
    qa_raise("Error", 0, 0, "Ranged download staged size changed");
}
/* Sequential appends were hashed as written; a ranged file is hashed from the staged
 * descriptor in 64 KiB reads, as TS does. */
static char *sink_digest(qa_download_sink *s) {
  if (s->ranged) {
    uint8_t bytes[64 * 1024];
    double offset = 0;
    while (offset < s->total) {
      size_t want = s->total - offset < (double)sizeof bytes ? (size_t)(s->total - offset) : sizeof bytes;
      ssize_t n = pread(s->fd, bytes, want, (off_t)offset);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) { int code = errno; qa_raise("Error", code, 0, "%s, read", strerror(code)); }
      if (n == 0) qa_raise("Error", 0, 0, "Unexpected ranged download EOF");
      qa_net_sha256_update(&s->hash, bytes, (size_t)n); offset += (double)n;
    }
  }
  char hex[65]; qa_net_sha256_hex_digest(&s->hash, hex);
  char *digest = qa_create_content_digest(hex);
  if (!digest) qa_range_error("Out of memory");
  return digest;
}
struct finish_scope { qa_download_sink *s; char *digest; };
static void finish_body(void *ctx) {
  struct finish_scope *scope = ctx;
  qa_download_sink *s = scope->s;
  require_complete_ranges(s);
  if (!s->expected.is_protocol && s->count != s->expected.byte_length) qa_raise("Error", 0, 0, "Download size differs from content identity");
  scope->digest = sink_digest(s);
  if (!s->expected.is_protocol && strcmp(scope->digest, s->expected.digest) != 0)
    qa_raise("Error", 0, 0, "Download checksum differs from content identity");
  /* link fails if the destination exists, avoiding replacement of installed game assets. */
  if (linkat(s->parent_fd, s->temporary, s->parent_fd, s->target, 0) != 0) {
    int code = errno;
    qa_raise("Error", code, 0, "%s, link", strerror(code));
  }
}
char *qa_download_sink_finish(qa_download_sink *s) {
  if (s->ended) qa_raise("Error", 0, 0, "Download sink is closed");
  if (s->inspecting) qa_raise("Error", 0, 0, "Download inspection is in progress");
  /* try { ... } finally { close() } */
  struct finish_scope scope = { s, NULL };
  qa_caught_error caught;
  bool ok = qa_try(finish_body, &scope, &caught);
  qa_download_sink_close(s);
  if (!ok) { free(scope.digest); qa_rethrow(&caught); }
  return scope.digest;
}
struct try_finish_scope { qa_download_sink *s; char *digest; };
static void try_finish_body(void *ctx) { struct try_finish_scope *scope = ctx; scope->digest = qa_download_sink_finish(scope->s); }
char *qa_download_sink_try_finish(qa_download_sink *s, const char **error) {
  *error = NULL;
  struct try_finish_scope scope = { s, NULL };
  qa_caught_error caught;
  if (qa_try(try_finish_body, &scope, &caught)) return scope.digest;
  snprintf(s->failure, sizeof s->failure, "%s", caught.message);
  *error = s->failure;
  return NULL;
}
struct inspect_scope { qa_download_sink *s; qa_download_inspect_fn inspect; void *ctx; const char *path; };
static void inspect_body(void *ctx) {
  struct inspect_scope *scope = ctx;
  scope->inspect(scope->ctx, scope->path);
  if (scope->s->ended) qa_raise("Error", 0, 0, "Download sink closed during inspection");
}
void qa_download_sink_inspect_staged(qa_download_sink *s, qa_download_inspect_fn inspect, void *ctx) {
  if (s->ended) qa_raise("Error", 0, 0, "Download sink is closed");
  if (s->inspecting) qa_raise("Error", 0, 0, "Download inspection is in progress");
  require_complete_ranges(s);
  char selfpath[128]; snprintf(selfpath, sizeof selfpath, "/proc/self/fd/%d", s->fd);
  int retained = open(selfpath, O_RDONLY);
  if (retained < 0) { int code = errno; qa_raise("Error", code, 0, "%s, open", strerror(code)); }
  s->inspecting = true;
  char rpath[128]; snprintf(rpath, sizeof rpath, "/proc/self/fd/%d", retained);
  /* try { await inspect(path); closed check } catch (error) { close(); throw error }
   * finally { inspecting = false; closeSync(retained) } */
  struct inspect_scope scope = { s, inspect, ctx, rpath };
  qa_caught_error caught;
  bool ok = qa_try(inspect_body, &scope, &caught);
  if (!ok) qa_download_sink_close(s);
  s->inspecting = false; close(retained);
  if (!ok) qa_rethrow(&caught);
}
static void unlink_at_if_present(int parent, const char *name, int *failure) {
  if (unlinkat(parent, name, 0) != 0 && errno != ENOENT && *failure == 0) *failure = errno;
}
void qa_download_sink_close(qa_download_sink *s) {
  if (s->ended) return;
  s->ended = true;
  /* try { closeSync(descriptor) } finally { try { unlink staged } finally { closeSync(parent) } }:
   * every step runs, then the first failure propagates. */
  int failure = 0; const char *call = NULL;
  if (s->fd >= 0 && close(s->fd) != 0) { failure = errno; call = "close"; }
  if (!s->suspended && s->parent_fd >= 0 && s->temporary) {
    int unlinked = 0;
    if (s->resumable) {
      unlink_at_if_present(s->parent_fd, s->temporary, &unlinked);
      char record[1024]; snprintf(record, sizeof record, "%s.sha256", s->temporary);
      unlink_at_if_present(s->parent_fd, record, &unlinked);
    } else if (unlinkat(s->parent_fd, s->temporary, 0) != 0) {
      /* A one-shot staged name must still exist: a missing one means something else removed
       * it, which callers report as a cleanup failure. */
      unlinked = errno;
    }
    if (unlinked != 0 && failure == 0) { failure = unlinked; call = "unlink"; }
  }
  if (s->parent_fd >= 0) close(s->parent_fd);
  s->fd = -1; s->parent_fd = -1;
  /* A closed sink rejects every later call before reading its names or spans, so close
   * releases them: callers that only close (as TS code only closes) leak nothing. */
  free(s->temporary); free(s->target); free(s->parent_path); free(s->spans);
  s->temporary = NULL; s->target = NULL; s->parent_path = NULL; s->spans = NULL; s->span_count = 0;
  if (failure != 0) qa_raise("Error", failure, 0, "%s, %s", strerror(failure), call);
}

/* Keep the staged bytes and their hash record for a later resumable sink; other sinks are discarded. */
typedef struct suspend_scope { qa_download_sink *s; } suspend_scope;
static void suspend_body(void *ctx) {
  qa_download_sink *s = ((suspend_scope *)ctx)->s;
  char record[1024]; snprintf(record, sizeof record, "%s.sha256", s->temporary);
  int fd = openat(s->parent_fd, record, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd < 0) { int code = errno; qa_raise("Error", code, 0, "%s, open", strerror(code)); }
  qa_net_sha256 copy = s->hash; /* hash.copy() */
  char hex[65]; qa_net_sha256_hex_digest(&copy, hex);
  char line[128]; int n = snprintf(line, sizeof line, "%s %s\n", QA_JS_NUMBER(s->count), hex);
  size_t written = 0; int code = 0;
  while (written < (size_t)n) {
    ssize_t w = write(fd, line + written, (size_t)n - written);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) { code = w < 0 ? errno : EIO; break; }
    written += (size_t)w;
  }
  close(fd);
  if (code != 0) qa_raise("Error", code, 0, "%s, write", strerror(code));
  s->suspended = true;
}
void qa_download_sink_suspend(qa_download_sink *s) {
  if (s->ended || s->inspecting || !s->resumable || s->count == 0) {
    qa_download_sink_close(s);
    return;
  }
  /* try { write record; suspended = true } finally { close() } */
  suspend_scope scope = { s };
  qa_caught_error caught;
  bool ok = qa_try(suspend_body, &scope, &caught);
  qa_download_sink_close(s);
  if (!ok) qa_rethrow(&caught);
}

/* reopenPartial: a descriptor positioned for appending, with the hash state of its verified
 * prefix, or -1 after discarding a bad partial. */
typedef struct reopen_scope { int parent; const char *partial; double limit; int fd; qa_net_sha256 hash; double count; bool reused; } reopen_scope;
static bool parse_record(const char *text, size_t length, double *size, char hex[65]) {
  /* /^(\d{1,15}) ([0-9a-f]{64})\n?$/ */
  size_t i = 0; double value = 0;
  while (i < length && i < 16 && text[i] >= '0' && text[i] <= '9') { value = value * 10 + (text[i] - '0'); i++; }
  if (i == 0 || i > 15 || i >= length || text[i] != ' ') return false;
  size_t start = ++i;
  while (i < length && i - start < 64 && ((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) i++;
  if (i - start != 64) return false;
  memcpy(hex, text + start, 64); hex[64] = 0;
  if (i < length && text[i] == '\n') i++;
  if (i != length) return false;
  *size = value;
  return true;
}
static void reopen_verify_body(void *ctx) {
  reopen_scope *r = ctx;
  struct stat info;
  if (fstat(r->fd, &info) != 0) { int code = errno; qa_raise("Error", code, 0, "%s, fstat", strerror(code)); }
  char record[1024]; snprintf(record, sizeof record, "%s.sha256", r->partial);
  bool claimed = false; double claimed_size = 0; char claimed_hex[65] = {0};
  int rfd = openat(r->parent, record, O_RDONLY);
  if (rfd < 0) {
    int code = errno;
    if (code != ENOENT) qa_raise("Error", code, 0, "%s, open", strerror(code));
  } else {
    char text[256]; size_t length = 0; int code = 0;
    for (;;) {
      ssize_t n = read(rfd, text + length, sizeof text - length);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) { code = errno; break; }
      if (n == 0) break;
      length += (size_t)n;
      if (length == sizeof text) break; /* longer than any valid record */
    }
    close(rfd);
    if (code != 0) qa_raise("Error", code, 0, "%s, read", strerror(code));
    claimed = length < sizeof text && parse_record(text, length, &claimed_size, claimed_hex);
  }
  qa_net_sha256_init(&r->hash);
  double size = (double)info.st_size;
  bool verified = claimed && S_ISREG(info.st_mode) && size == claimed_size && size > 0 && size <= r->limit;
  if (verified) {
    static uint8_t chunk[64 * 1024];
    for (double offset = 0; offset < size;) {
      size_t want = size - offset < (double)sizeof chunk ? (size_t)(size - offset) : sizeof chunk;
      ssize_t n = pread(r->fd, chunk, want, (off_t)offset);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) { int code = errno; qa_raise("Error", code, 0, "%s, read", strerror(code)); }
      if (n == 0) { verified = false; break; }
      qa_net_sha256_update(&r->hash, chunk, (size_t)n);
      offset += (double)n;
    }
    if (verified) {
      qa_net_sha256 copy = r->hash; char hex[65]; qa_net_sha256_hex_digest(&copy, hex);
      verified = strcmp(hex, claimed_hex) == 0;
    }
  }
  if (verified) { r->reused = true; r->count = size; return; }
  close(r->fd); r->fd = -1;
  int failure = 0;
  unlink_at_if_present(r->parent, r->partial, &failure);
  if (failure == 0) unlink_at_if_present(r->parent, record, &failure);
  if (failure != 0) qa_raise("Error", failure, 0, "%s, unlink", strerror(failure));
}
static void reopen_partial(reopen_scope *r) {
  r->fd = openat(r->parent, r->partial, O_RDWR | O_APPEND | O_NOFOLLOW);
  if (r->fd < 0) {
    int code = errno;
    if (code != ENOENT) qa_raise("Error", code, 0, "%s, open", strerror(code));
    char record[1024]; snprintf(record, sizeof record, "%s.sha256", r->partial);
    int failure = 0; unlink_at_if_present(r->parent, record, &failure);
    if (failure != 0) qa_raise("Error", failure, 0, "%s, unlink", strerror(failure));
    return;
  }
  qa_caught_error caught;
  if (!qa_try(reopen_verify_body, r, &caught)) {
    if (r->fd >= 0) close(r->fd);
    r->fd = -1;
    qa_rethrow(&caught);
  }
}
typedef struct resumable_scope { qa_download_sink *s; int parent; char *leaf; qa_download_expectation expected; } resumable_scope;
static void resumable_body(void *ctx) {
  resumable_scope *scope = ctx;
  qa_download_sink *s = scope->s;
  size_t ln = strlen(scope->leaf);
  char *partial = malloc(ln + 6);
  if (!partial) qa_range_error("Out of memory");
  memcpy(partial, scope->leaf, ln); memcpy(partial + ln, ".part", 6);
  reopen_scope r; memset(&r, 0, sizeof r);
  r.parent = scope->parent; r.partial = partial; r.limit = scope->expected.maximum_bytes; r.fd = -1;
  qa_cleanup partial_guard; qa_cleanup_push(&partial_guard, free, partial);
  reopen_partial(&r);
  int fd = r.fd;
  if (!r.reused) {
    fd = openat(scope->parent, partial, O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { int code = errno; qa_raise("Error", code, 0, "%s, open", strerror(code)); }
  }
  qa_cleanup_pop(&partial_guard);
  s->parent_fd = scope->parent; s->fd = fd;
  s->temporary = partial; s->target = scope->leaf; scope->leaf = NULL;
  s->expected = scope->expected; s->ranged = false; s->resumable = true;
  if (r.reused) { s->hash = r.hash; s->count = r.count; }
  else qa_net_sha256_init(&s->hash);
  s->resumed_from = s->count;
  char pbuf[512]; snprintf(pbuf, sizeof pbuf, "/proc/self/fd/%d", scope->parent);
  size_t pn = strlen(pbuf) + 1; s->parent_path = malloc(pn);
  if (!s->parent_path) qa_range_error("Out of memory");
  memcpy(s->parent_path, pbuf, pn);
}
int qa_download_sink_create_resumable(qa_download_sink *s, const char *root, const char *name, qa_download_expectation expected) {
  if (!expected.is_protocol || !is_safe_d(expected.maximum_bytes) || expected.maximum_bytes < 0) qa_range_error("Invalid download size");
  memset(s, 0, sizeof *s);
  s->fd = -1; s->parent_fd = -1;
  char *leaf = NULL;
  int parent = qa_open_contained_parent(root, name, true, &leaf);
  if (parent < 0) return 0;
  /* try { ... } catch (error) { closeSync(parent.descriptor); throw error } */
  resumable_scope scope = { s, parent, leaf, expected };
  qa_caught_error caught;
  if (!qa_try(resumable_body, &scope, &caught)) {
    free(scope.leaf);
    free(s->temporary); free(s->parent_path);
    if (s->fd >= 0) close(s->fd);
    memset(s, 0, sizeof *s); s->ended = true; s->fd = -1; s->parent_fd = -1;
    close(parent);
    qa_rethrow(&caught);
  }
  return 1;
}
