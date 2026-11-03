/* Port of src/app/bootstrap/network/q3-client-downloads.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_client_downloads.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/content/archive.h"
#include "qa/foundation/event_ring.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/pure.h"
#include "qa/network/services/downloads.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/js.h"
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
/* node:path resolve() against cwd, lexically normalized. */
static char *resolve_path(const char *path) {
  char *joined;
  if (path[0] == '/') {
    joined = copy_text(path);
  } else {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return NULL;
    size_t n = strlen(cwd) + strlen(path) + 2;
    joined = malloc(n);
    if (joined) snprintf(joined, n, "%s/%s", cwd, path);
  }
  if (!joined) return NULL;
  size_t length = strlen(joined), out_length = 0;
  char *out = malloc(length + 2);
  if (!out) { free(joined); return NULL; }
  out[out_length++] = '/';
  char *cursor = joined;
  while (*cursor) {
    while (*cursor == '/') cursor++;
    if (!*cursor) break;
    char *end = strchr(cursor, '/');
    size_t part = end ? (size_t)(end - cursor) : strlen(cursor);
    if (part == 1 && cursor[0] == '.') { /* skip */ }
    else if (part == 2 && cursor[0] == '.' && cursor[1] == '.') {
      if (out_length > 1) {
        out_length--;
        while (out_length > 1 && out[out_length - 1] != '/') out_length--;
      }
    } else {
      if (out_length > 1) out[out_length++] = '/';
      memcpy(out + out_length, cursor, part); out_length += part;
    }
    cursor = end ? end : cursor + part;
  }
  out[out_length] = '\0';
  free(joined);
  return out;
}
static void dirname_in_place(char *path) {
  char *slash = strrchr(path, '/');
  if (slash == path) slash[1] = '\0';
  else if (slash) *slash = '\0';
}
static const char *basename_of(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
char *qa_app_q3_download_path(const char *path, const qa_app_q3_download_mounts *owner) {
  qa_q3_check_download_name(path);
  char *write_dir = resolve_path(owner->write_root);
  char *base_dir = resolve_path(owner->base_write_root);
  if (!write_dir || !base_dir) { free(write_dir); free(base_dir); return NULL; }
  char *write_parent = copy_text(write_dir), *base_parent = copy_text(base_dir);
  dirname_in_place(write_parent); dirname_in_place(base_parent);
  bool same_family = strcmp(owner->selection_base, "q3-baseq3") == 0
      && strcmp(write_parent, base_parent) == 0;
  free(write_parent); free(base_parent);
  if (!same_family) {
    free(write_dir); free(base_dir);
    qa_range_error("Q3 download roots must belong to the selected Q3 family");
  }
  const char *separator = strchr(path, '/');
  if (separator == NULL) { free(write_dir); free(base_dir); return copy_text(path); }
  size_t dir_length = (size_t)(separator - path);
  char *head = malloc(dir_length + 1);
  if (!head) { free(write_dir); free(base_dir); return NULL; }
  memcpy(head, path, dir_length);
  head[dir_length] = '\0';
  char *directory = qa_js_to_lower_case(head); /* TS toLowerCase (Unicode) */
  free(head);
  if (!directory) { free(write_dir); free(base_dir); return NULL; }
  const char *physical = strcmp(directory, owner->selection_directory) == 0 ? basename_of(write_dir)
      : strcmp(directory, "baseq3") == 0 ? basename_of(base_dir) : NULL;
  free(directory);
  if (physical == NULL) { free(write_dir); free(base_dir); return copy_text(path); }
  size_t n = strlen(physical) + strlen(separator) + 1;
  char *mapped = malloc(n);
  if (mapped) snprintf(mapped, n, "%s%s", physical, separator);
  free(write_dir); free(base_dir);
  if (!mapped) return NULL;
  qa_q3_check_download_name(mapped);
  return mapped;
}
typedef struct package_request { char *remote, *local; double checksum; } package_request;
struct qa_app_q3_client_downloads {
  char *root; qa_app_q3_download_bindings bindings;
  bool has_owner; qa_app_q3_download_mounts owner;
  qa_q3_client_download *source;
  qa_event_ring queue;
  bool has_current; package_request current;
  bool has_sink; qa_download_sink sink;
  double size, generation; bool receiving;
  package_request *paused; size_t paused_count; bool has_paused;
};
static bool permitted(qa_app_q3_client_downloads *d) {
  return d->bindings.permission == NULL
      || qa_app_client_download_permitted(d->bindings.permission, QA_APP_DL_NATIVE, QA_APP_DL_PACKAGE);
}
static char *destination(qa_app_q3_client_downloads *d, const char *path) {
  return d->has_owner ? qa_app_q3_download_path(path, &d->owner) : copy_text(path);
}
static void request_free(package_request *r) { free(r->remote); free(r->local); }
static bool queue_shift(qa_app_q3_client_downloads *d, package_request *out) {
  const package_request *head = qa_event_ring_head(&d->queue);
  if (!head) return false;
  *out = *head;
  qa_event_ring_remove_at_offset(&d->queue, 0);
  return true;
}
typedef struct snapshot_ctx { package_request *items; size_t count, capacity; bool oom; } snapshot_ctx;
static void snapshot_visit(const void *record, int64_t id, void *ctx) {
  (void)id;
  snapshot_ctx *s = ctx;
  if (s->count == s->capacity) {
    size_t next = s->capacity ? s->capacity * 2 : 8;
    package_request *items = realloc(s->items, next * sizeof *items);
    if (!items) { s->oom = true; return; }
    s->items = items; s->capacity = next;
  }
  const package_request *r = record;
  s->items[s->count].remote = copy_text(r->remote);
  s->items[s->count].local = copy_text(r->local);
  s->items[s->count].checksum = r->checksum;
  s->count++;
}
static package_request *queue_snapshot(qa_app_q3_client_downloads *d, size_t *out_count) {
  snapshot_ctx s = {0};
  qa_event_ring_for_each_live(&d->queue, snapshot_visit, &s);
  *out_count = s.count;
  return s.items;
}
static bool start_next(qa_app_q3_client_downloads *d) {
  package_request request;
  bool has = queue_shift(d, &request);
  if (d->has_current) request_free(&d->current);
  d->has_current = has;
  if (has) d->current = request;
  d->size = 0;
  if (!has) return false;
  qa_q3_client_download_begin(d->source, request.remote, request.local);
  return true;
}
static void sink_cleanup(qa_app_q3_client_downloads *d) {
  if (d->has_sink) { qa_download_sink_destroy(&d->sink); d->has_sink = false; }
}
static void downloads_close(qa_app_q3_client_downloads *d) {
  if (d->has_paused) {
    for (size_t i = 0; i < d->paused_count; i++) request_free(&d->paused[i]);
    free(d->paused); d->paused = NULL; d->paused_count = 0; d->has_paused = false;
  }
  d->generation++;
  qa_q3_client_download_close(d->source);
  if (d->has_sink) qa_download_sink_close(&d->sink);
  sink_cleanup(d);
  if (d->has_current) { request_free(&d->current); d->has_current = false; }
  /* Ring records own strings; snapshot-free then reset. */
  size_t count; package_request *items = queue_snapshot(d, &count);
  for (size_t i = 0; i < count; i++) request_free(&items[i]);
  free(items);
  qa_event_ring_reset(&d->queue);
  d->size = 0;
}
static void binding_assert_current(void *ctx) {
  qa_app_q3_client_downloads *d = ctx;
  d->bindings.assert_current(d->bindings.ctx);
  if (!permitted(d)) qa_range_error("Q3 package download permission denied");
}
static void write_bytes(void *ctx, const uint8_t *bytes, size_t length) {
  qa_app_q3_client_downloads *d = ctx;
  qa_download_sink_append(&d->sink, bytes, length);
}
static void write_close(void *ctx) { (void)ctx; }
static bool binding_open_temporary(void *ctx, const char *path, qa_q3_download_write_file *out) {
  qa_app_q3_client_downloads *d = ctx;
  if (!d->has_current || d->size <= 0) qa_range_error("Unexpected Q3 download temporary request");
  size_t n = strlen(d->current.local) + 5;
  char *expected = malloc(n);
  if (!expected) return false;
  snprintf(expected, n, "%s.tmp", d->current.local);
  bool match = strcmp(path, expected) == 0;
  free(expected);
  if (!match) qa_range_error("Unexpected Q3 download temporary request");
  char *dest = destination(d, d->current.local);
  if (!dest) return false;
  qa_download_expectation expectation = {true, NULL, 0, d->size};
  int created = qa_download_sink_create(&d->sink, d->root, dest, expectation);
  free(dest);
  if (!created) return false;
  d->has_sink = true;
  out->write_bytes = write_bytes; out->close = write_close; out->ctx = d;
  return true;
}
typedef struct inspect_ctx { double checksum; } inspect_ctx;
static void archive_cleanup(void *archive) { qa_archive_destroy(archive); }
static void inspect_staged(void *ctx, const char *path) {
  inspect_ctx *inspect = ctx;
  qa_archive_handle *archive = qa_open_archive(path, true, QA_ARCHIVE_FORMAT_PK3);
  /* try { checksum } finally { archive.close() }: the staged file came from the server. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, archive_cleanup, archive);
  int64_t checksum = 0, pure = 0;
  qa_q3_archive_checksums(archive, 0, &checksum, &pure);
  qa_cleanup_pop(&guard);
  qa_archive_destroy(archive);
  if ((uint32_t)checksum != qa_js_to_uint32(inspect->checksum))
    qa_raise("Error", 0, 0, "Downloaded Q3 package checksum differs from server references");
}
static void binding_publish_temporary(void *ctx, const char *temporary, const char *dest) {
  qa_app_q3_client_downloads *d = ctx;
  double generation = d->generation;
  bool match = false;
  if (d->has_current && d->has_sink && strcmp(dest, d->current.local) == 0) {
    size_t n = strlen(d->current.local) + 5;
    char *expected = malloc(n);
    if (expected) {
      snprintf(expected, n, "%s.tmp", d->current.local);
      match = strcmp(temporary, expected) == 0;
      free(expected);
    }
  }
  if (!match) qa_range_error("Q3 download publication does not match the current package");
  if (qa_download_sink_byte_length(&d->sink) != d->size)
    qa_range_error("Q3 download ended before its advertised size");
  inspect_ctx inspect = {d->current.checksum};
  qa_download_sink_inspect_staged(&d->sink, inspect_staged, &inspect);
  d->bindings.assert_current(d->bindings.ctx);
  if (!permitted(d)) qa_range_error("Q3 package download permission denied");
  if (generation != d->generation || !d->has_sink)
    qa_range_error("Q3 download was retired during validation");
  char *digest = qa_download_sink_finish(&d->sink);
  free(digest);
  sink_cleanup(d);
}
static void binding_reliable(void *ctx, const char *text) {
  qa_app_q3_client_downloads *d = ctx;
  d->bindings.reliable(d->bindings.ctx, text);
}
static void binding_send_packet(void *ctx) {
  qa_app_q3_client_downloads *d = ctx;
  d->bindings.send_packet(d->bindings.ctx);
}
static void binding_progress(void *ctx, const char *name, int64_t count, int64_t size) {
  qa_app_q3_client_downloads *d = ctx;
  d->bindings.progress(d->bindings.ctx, name, (double)count, (double)size);
}
static void binding_completed(void *ctx) {
  qa_app_q3_client_downloads *d = ctx;
  if (d->has_current) { request_free(&d->current); d->has_current = false; }
  if (start_next(d)) return;
  double generation = d->generation;
  d->bindings.reload_packages(d->bindings.ctx);
  d->bindings.assert_current(d->bindings.ctx);
  if (generation != d->generation)
    qa_range_error("Q3 downloads retired during filesystem refresh");
  d->bindings.reliable(d->bindings.ctx, "donedl");
}
qa_app_q3_client_downloads *qa_app_q3_client_downloads_create(const char *root,
    qa_app_q3_download_bindings bindings, const qa_app_q3_download_mounts *owner) {
  if (owner) {
    char *resolved_root = resolve_path(root);
    char *write_root = resolve_path(owner->write_root);
    if (write_root) dirname_in_place(write_root);
    bool match = resolved_root && write_root && strcmp(resolved_root, write_root) == 0;
    free(resolved_root); free(write_root);
    if (!match) qa_range_error("Q3 download root differs from the selected mount family");
  }
  qa_app_q3_client_downloads *d = calloc(1, sizeof *d);
  if (!d) return NULL;
  d->root = copy_text(root); d->bindings = bindings;
  if (owner) { d->has_owner = true; d->owner = *owner; }
  if (!qa_event_ring_init(&d->queue, 4096, sizeof(package_request))) {
    free(d->root); free(d); return NULL;
  }
  qa_q3_download_client_bindings client = {binding_assert_current, binding_open_temporary,
      binding_publish_temporary, binding_reliable, binding_send_packet, binding_progress,
      binding_completed, d};
  if (!qa_q3_client_download_init(&d->source, &client)) {
    qa_event_ring_destroy(&d->queue); free(d->root); free(d); return NULL;
  }
  return d;
}
void qa_app_q3_client_downloads_destroy(qa_app_q3_client_downloads *downloads) {
  if (!downloads) return;
  downloads_close(downloads);
  qa_q3_client_download_destroy(downloads->source);
  qa_event_ring_destroy(&downloads->queue);
  free(downloads->root); free(downloads);
}
typedef struct progress_ctx {
  qa_app_client_download_progress *out; size_t capacity, total;
} progress_ctx;
static void progress_visit(const void *record, int64_t id, void *ctx) {
  (void)id;
  progress_ctx *p = ctx;
  const package_request *r = record;
  if (p->total < p->capacity) {
    qa_app_client_download_progress *row = &p->out[p->total];
    row->path = r->local; row->transport = QA_APP_DL_NATIVE;
    row->received = 0; row->has_total = false; row->total = 0;
    row->has_percent = false; row->percent = 0; row->phase = QA_APP_DL_PENDING;
  }
  p->total++;
}
size_t qa_app_q3_client_downloads_progress(qa_app_q3_client_downloads *downloads,
    qa_app_client_download_progress *out, size_t capacity) {
  progress_ctx p = {out, capacity, 0};
  if (downloads->has_current) {
    if (p.total < capacity) {
      qa_app_client_download_progress *row = &out[p.total];
      double received = downloads->has_sink ? qa_download_sink_byte_length(&downloads->sink) : 0;
      row->path = downloads->current.local; row->transport = QA_APP_DL_NATIVE;
      row->received = received; row->has_total = true; row->total = downloads->size;
      row->has_percent = downloads->size > 0;
      row->percent = downloads->size > 0 ? received * 100 / downloads->size : 0;
      row->phase = QA_APP_DL_RUNNING;
    }
    p.total++;
  }
  if (downloads->has_paused) {
    for (size_t i = 0; i < downloads->paused_count; i++)
      progress_visit(&downloads->paused[i], 0, &p);
  } else {
    qa_event_ring_for_each_live(&downloads->queue, progress_visit, &p);
  }
  return p.total;
}
void qa_app_q3_client_downloads_cancel(qa_app_q3_client_downloads *downloads) {
  if (downloads->has_paused) return;
  downloads->bindings.assert_current(downloads->bindings.ctx);
  size_t count; package_request *queued = queue_snapshot(downloads, &count);
  package_request *remaining = queued; size_t remaining_count = count;
  if (downloads->has_current) {
    remaining = malloc((count + 1) * sizeof *remaining);
    if (!remaining) qa_range_error("out of memory");
    remaining[0].remote = copy_text(downloads->current.remote);
    remaining[0].local = copy_text(downloads->current.local);
    remaining[0].checksum = downloads->current.checksum;
    if (count) memcpy(remaining + 1, queued, count * sizeof *queued);
    free(queued);
    remaining_count = count + 1;
  }
  downloads_close(downloads);
  downloads->paused = remaining; downloads->paused_count = remaining_count;
  downloads->has_paused = true;
  downloads->bindings.reliable(downloads->bindings.ctx, "stopdl");
  downloads->bindings.send_packet(downloads->bindings.ctx);
}
bool qa_app_q3_client_downloads_retry(qa_app_q3_client_downloads *downloads) {
  downloads->bindings.assert_current(downloads->bindings.ctx);
  if (!downloads->has_paused || !permitted(downloads)) return false;
  package_request *resumed = downloads->paused; size_t count = downloads->paused_count;
  downloads->paused = NULL; downloads->paused_count = 0; downloads->has_paused = false;
  qa_event_ring_reset(&downloads->queue);
  if (count) qa_event_ring_admit_all(&downloads->queue, resumed, count);
  free(resumed);
  return start_next(downloads);
}
typedef struct exists_ctx {
  qa_app_q3_client_downloads *downloads;
  bool (*exists)(void *, const char *); void *ctx;
} exists_ctx;
static bool package_exists(void *ctx, const char *name) {
  exists_ctx *e = ctx;
  char *dest = destination(e->downloads, name);
  bool result = dest && e->exists(e->ctx, dest);
  free(dest);
  return result;
}
bool qa_app_q3_client_downloads_begin(qa_app_q3_client_downloads *downloads,
    const qa_q3_server_pak *referenced, size_t referenced_count,
    const double *loaded_checksums, size_t loaded_count,
    bool (*exists)(void *ctx, const char *path), void *exists_ctx_param) {
  downloads->bindings.assert_current(downloads->bindings.ctx);
  if (downloads->receiving) qa_raise("Error", 0, 0, "Cannot replace Q3 downloads during block processing");
  if (!permitted(downloads)) { downloads_close(downloads); return false; }
  downloads_close(downloads);
  int64_t *loaded = malloc((loaded_count ? loaded_count : 1) * sizeof *loaded);
  if (!loaded) qa_range_error("out of memory");
  for (size_t i = 0; i < loaded_count; i++) loaded[i] = (int64_t)qa_uint32(loaded_checksums[i]);
  exists_ctx e = {downloads, exists, exists_ctx_param};
  char *list = qa_q3_compare_packages(referenced, referenced_count, loaded, loaded_count,
      package_exists, &e, true, 1024);
  free(loaded);
  if (!list) qa_range_error("out of memory");
  /* Split on '@'; fields[0] must be '' and count odd. */
  size_t fields = 1;
  for (const char *p = list; *p; p++) if (*p == '@') fields++;
  char **parts = malloc(fields * sizeof *parts);
  if (!parts) { free(list); qa_range_error("out of memory"); }
  /* The server names these packages: a name check raising below must not leak the list. */
  qa_cleanup list_guard, parts_guard;
  qa_cleanup_push(&list_guard, free, list);
  qa_cleanup_push(&parts_guard, free, parts);
  size_t part_count = 0;
  parts[part_count++] = list;
  for (char *p = list; *p; p++) if (*p == '@') { *p = '\0'; parts[part_count++] = p + 1; }
  if (parts[0][0] != '\0' || fields % 2 != 1) qa_raise("Error", 0, 0, "Incomplete Q3 package download list");
  for (size_t index = 1; index < fields; index += 2) {
    const char *remote = parts[index], *local = parts[index + 1];
    qa_q3_check_download_name(remote);
    qa_q3_check_download_name(local);
    if (strlen(remote) >= 64) qa_range_error("Q3 download name exceeds source server MAX_QPATH");
    const qa_q3_server_pak *pack = NULL; bool conflict = false;
    for (size_t i = 0; i < referenced_count; i++) {
      const qa_q3_server_pak *value = &referenced[i];
      if (!value->name) continue;
      size_t n = strlen(value->name) + 5;
      char *candidate = malloc(n);
      if (!candidate) qa_range_error("out of memory");
      snprintf(candidate, n, "%s.pk3", value->name);
      bool name_match = strcmp(candidate, remote) == 0;
      free(candidate);
      if (!name_match) continue;
      bool is_loaded = false;
      for (size_t j = 0; j < loaded_count; j++)
        if ((uint32_t)value->checksum == qa_uint32(loaded_checksums[j])) is_loaded = true;
      if (is_loaded) continue;
      if (!pack) pack = value;
      else if ((uint32_t)value->checksum != (uint32_t)pack->checksum) conflict = true;
    }
    if (!pack) qa_raise("Error", 0, 0, "Q3 download pair has no server reference");
    if (conflict) qa_raise("Error", 0, 0, "Q3 server references conflicting checksums for one download name");
    package_request request = {copy_text(remote), copy_text(local), (double)pack->checksum};
    qa_event_ring_admit(&downloads->queue, &request);
  }
  qa_cleanup_pop(&parts_guard);
  qa_cleanup_pop(&list_guard);
  free(parts);
  free(list);
  return start_next(downloads);
}
double qa_app_q3_client_downloads_publish_size(qa_app_q3_client_downloads *downloads, double size) {
  downloads->bindings.assert_current(downloads->bindings.ctx);
  if (downloads->has_sink && size != downloads->size)
    qa_range_error("Q3 download size changed during transfer");
  downloads->size = size;
  return (double)qa_q3_client_download_publish_size(downloads->source, (int64_t)size);
}
typedef struct receive_scope { qa_app_q3_client_downloads *d; const qa_q3_download_block *download; } receive_scope;
static void receive_body(void *ctx) {
  receive_scope *scope = ctx;
  qa_q3_client_download_receive(scope->d->source, scope->download);
}
typedef struct close_scope { qa_app_q3_client_downloads *d; } close_scope;
static void close_body(void *ctx) { downloads_close(((close_scope *)ctx)->d); }
void qa_app_q3_client_downloads_receive(qa_app_q3_client_downloads *downloads,
    const qa_q3_download_block *download) {
  if (downloads->receiving) qa_range_error("Q3 download block processing is already active");
  if (!permitted(downloads)) { qa_app_q3_client_downloads_cancel(downloads); return; }
  downloads->receiving = true;
  double generation = downloads->generation;
  /* try { source.receive } catch { swallow when a pause retired this generation;
   * otherwise close and rethrow } finally { receiving = false } */
  receive_scope scope = { downloads, download };
  qa_caught_error caught;
  bool ok = qa_try(receive_body, &scope, &caught);
  if (!ok && !(downloads->has_paused && generation != downloads->generation)) {
    close_scope closing = { downloads };
    qa_caught_error close_caught;
    bool closed = qa_try(close_body, &closing, &close_caught);
    downloads->receiving = false;
    if (!closed) qa_rethrow(&close_caught);
    qa_rethrow(&caught);
  }
  downloads->receiving = false;
}
void qa_app_q3_client_downloads_close(qa_app_q3_client_downloads *downloads) {
  downloads_close(downloads);
}
