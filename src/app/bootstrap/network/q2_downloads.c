/* Port of src/app/bootstrap/network/q2-downloads.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_downloads.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/core/md4.h"
#include "qa/core/numeric.h"
#include "qa/formats/q12_model/md2.h"
#include "qa/formats/q12_model/sprite.h"
#include "qa/formats/q2_map/reader.h"
#include "qa/foundation/support.h"
#include "qa/materials/sky.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/services/http_downloads.h"
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1; char *out = malloc(n);
  if (out) memcpy(out, s, n);
  return out;
}
static bool starts_with_ci(const char *s, const char *prefix) {
  for (size_t i = 0; prefix[i]; i++)
    if (tolower((unsigned char)s[i]) != prefix[i]) return false;
  return true;
}
static bool ends_with_ci(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  if (n < m) return false;
  for (size_t i = 0; i < m; i++)
    if (tolower((unsigned char)s[n - m + i]) != suffix[i]) return false;
  return true;
}
static bool charset_ok(const char *path) {
  if (!*path) return false;
  for (const char *p = path; *p; p++) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '_' || c == '+' || c == '.' || c == '/' || c == '-';
    if (!ok) return false;
  }
  return true;
}
static void check_download_path(const char *path) {
  size_t count = 0;
  char **parts = qa_download_path(path, &count);
  qa_contained_file_parts_free(parts, count);
}
struct qa_app_q2_downloads { qa_app_q2_mounts mounts; qa_cvar_registry *cvars; };
qa_app_q2_downloads *qa_app_create_q2_application_downloads(
    qa_app_q2_mounts mounts, qa_cvar_registry *cvars, bool edition_rerelease) {
  qa_app_q2_downloads *d = malloc(sizeof *d);
  if (!d) return NULL;
  d->mounts = mounts; d->cvars = cvars;
  qa_cvar_snapshot registered;
  qa_cvar_registry_register(cvars, "sv_downloadserver", "", 0, &registered);
  const char *def = edition_rerelease ? "1" : "0";
  qa_cvar_registry_register(cvars, "allow_download", def, QA_Q2_CVAR_FLAG_ARCHIVE, &registered);
  qa_cvar_registry_register(cvars, "allow_download_players", def, QA_Q2_CVAR_FLAG_ARCHIVE, &registered);
  static const char *names[] = {"allow_download_models", "allow_download_sounds", "allow_download_maps"};
  for (size_t i = 0; i < 3; i++)
    qa_cvar_registry_register(cvars, names[i], "1", QA_Q2_CVAR_FLAG_ARCHIVE, &registered);
  return d;
}
void qa_app_q2_downloads_destroy(qa_app_q2_downloads *downloads) { free(downloads); }
char *qa_app_q2_downloads_http_server(qa_app_q2_downloads *downloads) {
  char *server = qa_cvar_registry_variable_string(downloads->cvars, "sv_downloadserver");
  if (!server) return NULL;
  size_t n = strlen(server) + 10;
  char *argument = malloc(n);
  if (!argument) { free(server); return NULL; }
  snprintf(argument, n, "dlserver=%s", server);
  free(server);
  char *args[] = {argument};
  char *result = qa_q2_read_download_server(args, 1);
  free(argument);
  return result;
}
bool qa_app_q2_downloads_allowed(qa_app_q2_downloads *downloads, const char *name) {
  if (strstr(name, "..") || name[0] == '.' || !strchr(name, '/')
      || qa_cvar_registry_variable_value(downloads->cvars, "allow_download") == 0) return false;
  /* TS try { downloadPath } catch -> false: the landed parts routine
   * aborts on unsafe names, which the checks above mostly preclude. */
  check_download_path(name);
  static const struct { const char *prefix, *setting; } categories[] = {
    {"players/", "allow_download_players"}, {"models/", "allow_download_models"},
    {"sound/", "allow_download_sounds"}, {"maps/", "allow_download_maps"}};
  for (size_t i = 0; i < 4; i++)
    if (starts_with_ci(name, categories[i].prefix)
        && qa_cvar_registry_variable_value(downloads->cvars, categories[i].setting) == 0)
      return false;
  return true;
}
typedef struct retained { uint8_t *bytes; size_t length; } retained;
static uint8_t *retained_read(void *ctx, double offset, double max_bytes, size_t *out_len) {
  retained *r = ctx;
  if (!r->bytes) qa_range_error("Q2 mounted download is closed");
  size_t start = offset < 0 ? 0 : (size_t)offset;
  if (start > r->length) start = r->length;
  size_t available = r->length - start;
  size_t want = max_bytes < 0 ? 0 : (size_t)max_bytes;
  if (want > available) want = available;
  uint8_t *out = malloc(want ? want : 1);
  if (!out) { *out_len = 0; return NULL; }
  memcpy(out, r->bytes + start, want);
  *out_len = want;
  return out;
}
static void retained_close(void *ctx) {
  retained *r = ctx;
  free(r->bytes); r->bytes = NULL; free(r);
}
bool qa_app_q2_downloads_open(qa_app_q2_downloads *downloads, const char *name,
    qa_download_source *out) {
  uint8_t *bytes = NULL; size_t length = 0;
  const qa_resolved_resource_reference *reference = NULL;
  if (!downloads->mounts.open(downloads->mounts.ctx, name, &bytes, &length, &reference))
    return false;
  if (length > 0x7fffffff) { free(bytes); return false; }
  bool archive_map = reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
      && reference->provenance.member_path
      && starts_with_ci(reference->provenance.member_path, "maps/");
  if (!qa_can_download_resource(reference) || archive_map) { free(bytes); return false; }
  retained *r = malloc(sizeof *r);
  if (!r) { free(bytes); return false; }
  r->bytes = bytes; r->length = length;
  out->ctx = r; out->byte_length = (double)length;
  out->read = retained_read; out->close = retained_close;
  return true;
}
struct qa_app_q2_peer_download {
  bool has_sender; qa_q2_download_sender sender; double generation;
};
qa_app_q2_peer_download *qa_app_q2_peer_download_create(void) {
  return calloc(1, sizeof(qa_app_q2_peer_download));
}
void qa_app_q2_peer_download_destroy(qa_app_q2_peer_download *peer) {
  if (!peer) return;
  qa_app_q2_peer_download_close(peer);
  free(peer);
}
double qa_app_q2_peer_download_revision(const qa_app_q2_peer_download *peer) {
  return peer->generation;
}
static qa_q2_server_event *refused_event(void) {
  qa_q2_server_event *event = calloc(1, sizeof *event);
  if (!event) qa_range_error("out of memory");
  event->kind = QA_Q2_SERVER_DOWNLOAD;
  event->has_bytes = false; event->percent = 0;
  return event;
}
void qa_app_q2_peer_download_close(qa_app_q2_peer_download *peer) {
  peer->generation++;
  if (peer->has_sender) { qa_q2_download_sender_close(&peer->sender); peer->has_sender = false; }
}
qa_q2_server_event *qa_app_q2_peer_download_begin(qa_app_q2_peer_download *peer,
    qa_app_q2_downloads *downloads, const char *name, const char *offset_text) {
  /* TS try { nativeAtoi } catch: landed atoi does not throw. */
  double offset = qa_native_atoi(offset_text ? offset_text : "0");
  if (offset < 0 || !qa_app_q2_downloads_allowed(downloads, name)) return refused_event();
  qa_app_q2_peer_download_close(peer);
  double generation = peer->generation;
  qa_download_source source;
  if (!qa_app_q2_downloads_open(downloads, name, &source)) return refused_event();
  if (generation != peer->generation) { source.close(source.ctx); return NULL; }
  qa_q2_download_sender_init(&peer->sender, source, true,
      fmin(offset, source.byte_length), false, 0);
  peer->has_sender = true;
  return qa_app_q2_peer_download_next(peer);
}
qa_q2_server_event *qa_app_q2_peer_download_next(qa_app_q2_peer_download *peer) {
  return peer->has_sender ? qa_q2_download_sender_next(&peer->sender) : NULL;
}
/* ---------------- Q2DownloadReceiver ---------------- */
typedef struct string_list { char **items; size_t count, capacity; } string_list;
static void list_push(string_list *list, const char *value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->items = realloc(list->items, list->capacity * sizeof *list->items);
    if (!list->items) qa_range_error("out of memory");
  }
  list->items[list->count++] = copy_text(value);
}
static bool list_contains(const string_list *list, const char *value) {
  for (size_t i = 0; i < list->count; i++) if (strcmp(list->items[i], value) == 0) return true;
  return false;
}
static void list_clear(string_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  list->count = 0;
}
static void list_destroy(string_list *list) { list_clear(list); free(list->items); }
typedef struct resource_iter {
  int phase; size_t index, sub_index;
  char *map; uint8_t *world; size_t world_length;
  string_list pending; size_t pending_index;
  string_list models, sounds, images;
  int sky_step; char *sky_tga, *sky_pcx;
} resource_iter;
struct qa_app_q2_download_receiver {
  const qa_app_q2_remote_mounts *(*content)(void *); void *content_ctx;
  void (*command)(void *, const char *); void *command_ctx;
  void (*print)(void *, const char *); void *print_ctx;
  void (*refresh_packages)(void *); void *refresh_ctx;
  const qa_app_client_download_permission *permission;
  qa_app_q2_state_access access;
  bool cancelled, retired_block, retry_requested;
  char *server;
  /* The landed queue type is opaque with caller storage and no
   * published size; this aligned buffer holds it (parent: landed
   * header should expose a create/size). */
  _Alignas(16) unsigned char http_storage[1024]; bool http_live;
  bool metadata_aborted;
  bool http_task, http_done; char *http_error;
  int task_kind; /* 0 none, 1 initial, 2 asset */ char *task_path;
  string_list attempted, refused;
  struct { char *dest, *path; } *http_paths; size_t http_path_count, http_path_cap;
  struct { char *path; bool game_local; } *asset_scopes; size_t scope_count, scope_cap;
  char *retry_path;
  double generation;
  const qa_app_q2_game_state *state;
  resource_iter *paths;
  struct { char *path; bool has_sink; qa_download_sink sink; double percent; } pending;
  bool has_pending;
};
static bool permitted(qa_app_q2_download_receiver *r, qa_app_download_transport transport,
    qa_app_client_download_category category) {
  return r->permission == NULL
      || qa_app_client_download_permitted(r->permission, transport, category);
}
static void print_text(qa_app_q2_download_receiver *r, const char *a, const char *b,
    const char *c, const char *d) {
  size_t n = strlen(a) + strlen(b) + strlen(c) + strlen(d) + 1;
  char *text = malloc(n);
  if (!text) qa_range_error("out of memory");
  snprintf(text, n, "%s%s%s%s", a, b, c, d);
  r->print(r->print_ctx, text);
  free(text);
}
static void mkdir_recursive(const char *path) {
  char *copy = copy_text(path);
  if (!copy) qa_range_error("out of memory");
  for (char *p = copy + 1; *p; p++) {
    if (*p == '/') { *p = '\0'; mkdir(copy, 0777); *p = '/'; }
  }
  mkdir(copy, 0777);
  free(copy);
}
static const char *path_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
static char *path_dirname(const char *path) {
  const char *slash = strrchr(path, '/');
  if (!slash) return copy_text(".");
  if (slash == path) return copy_text("/");
  size_t n = (size_t)(slash - path);
  char *out = malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, path, n); out[n] = '\0';
  return out;
}
static char *encode_component(const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  size_t length = strlen(text);
  char *out = malloc(length * 3 + 1);
  if (!out) return NULL;
  size_t used = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '.' || c == '!' || c == '~'
        || c == '*' || c == '\'' || c == '(' || c == ')';
    if (keep) out[used++] = (char)c;
    else { out[used++] = '%'; out[used++] = hex[c >> 4]; out[used++] = hex[c & 15]; }
  }
  out[used] = '\0';
  return out;
}
static char *encode_path_segments(const char *path) {
  /* path.split('/').map(encodeURIComponent).join('/') */
  size_t length = strlen(path);
  char *out = malloc(length * 3 + 1);
  if (!out) return NULL;
  size_t used = 0;
  const char *cursor = path;
  for (;;) {
    const char *slash = strchr(cursor, '/');
    size_t part = slash ? (size_t)(slash - cursor) : strlen(cursor);
    char *segment = malloc(part + 1);
    if (!segment) { free(out); return NULL; }
    memcpy(segment, cursor, part); segment[part] = '\0';
    char *encoded = encode_component(segment);
    free(segment);
    if (!encoded) { free(out); return NULL; }
    size_t elen = strlen(encoded);
    memcpy(out + used, encoded, elen); used += elen;
    free(encoded);
    if (!slash) break;
    out[used++] = '/';
    cursor = slash + 1;
  }
  out[used] = '\0';
  return out;
}
/* new URL(relative, server) for path-only relatives. */
static char *url_join(const char *server, const char *relative) {
  const char *authority = strstr(server, "://");
  const char *path_start = authority ? strchr(authority + 3, '/') : NULL;
  size_t keep;
  if (!path_start) keep = strlen(server);
  else {
    const char *last = strrchr(path_start, '/');
    keep = (size_t)(last - server) + 1;
  }
  size_t n = keep + strlen(relative) + 1;
  char *out = malloc(n);
  if (!out) return NULL;
  memcpy(out, server, keep);
  memcpy(out + keep, relative, strlen(relative) + 1);
  return out;
}
static const char *config_string(qa_app_q2_download_receiver *r,
    const qa_app_q2_game_state *state, double index) {
  return r->access.config_string(state->config_strings, index);
}
static void validate_path(const char *path) {
  check_download_path(path);
  bool ok = charset_ok(path);
  if (ok) {
    if (starts_with_ci(path, "maps/")) ok = ends_with_ci(path, ".bsp");
    else if (starts_with_ci(path, "models/") || starts_with_ci(path, "players/"))
      ok = ends_with_ci(path, ".md2") || ends_with_ci(path, ".sp2")
          || ends_with_ci(path, ".pcx") || ends_with_ci(path, ".wav");
    else if (starts_with_ci(path, "sound/")) ok = ends_with_ci(path, ".wav");
    else if (starts_with_ci(path, "pics/")) ok = ends_with_ci(path, ".pcx");
    else if (starts_with_ci(path, "env/")) ok = ends_with_ci(path, ".tga") || ends_with_ci(path, ".pcx");
    else if (starts_with_ci(path, "textures/")) ok = ends_with_ci(path, ".wal");
    else ok = false;
  }
  if (!ok) qa_range_error("Q2 server requested a non-asset download: %s", path);
}
static bool filelist_extension_ok(const char *path) {
  static const char *extensions[] = {".bsp", ".dm2", ".ent", ".jpg", ".loc", ".md2", ".md3",
    ".ogg", ".pcx", ".png", ".sp2", ".tga", ".txt", ".wal", ".wav", ".jpeg", ".bmp", ".gif"};
  for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++)
    if (ends_with_ci(path, extensions[i])) return true;
  return false;
}
static const char *root_for(qa_app_q2_download_receiver *r, const char *path) {
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  return starts_with_ci(path, "players/") ? content->base_write_root : content->write_root;
}
static char *http_destination(qa_app_q2_download_receiver *r, const char *path) {
  const char *base = path_basename(root_for(r, path));
  size_t n = strlen(base) + strlen(path) + 2;
  char *out = malloc(n);
  if (!out) qa_range_error("out of memory");
  snprintf(out, n, "%s/%s", base, path);
  return out;
}
static const char *http_resource(qa_app_q2_download_receiver *r, const char *destination) {
  for (size_t i = 0; i < r->http_path_count; i++)
    if (strcmp(r->http_paths[i].dest, destination) == 0) return r->http_paths[i].path;
  qa_range_error("Unknown Q2 HTTP download destination");
}
static void http_paths_set(qa_app_q2_download_receiver *r, const char *dest, const char *path) {
  for (size_t i = 0; i < r->http_path_count; i++)
    if (strcmp(r->http_paths[i].dest, dest) == 0) {
      free(r->http_paths[i].path); r->http_paths[i].path = copy_text(path); return;
    }
  if (r->http_path_count == r->http_path_cap) {
    r->http_path_cap = r->http_path_cap ? r->http_path_cap * 2 : 16;
    r->http_paths = realloc(r->http_paths, r->http_path_cap * sizeof *r->http_paths);
    if (!r->http_paths) qa_range_error("out of memory");
  }
  r->http_paths[r->http_path_count].dest = copy_text(dest);
  r->http_paths[r->http_path_count].path = copy_text(path);
  r->http_path_count++;
}
static void scope_set(qa_app_q2_download_receiver *r, const char *path, bool game_local) {
  for (size_t i = 0; i < r->scope_count; i++)
    if (strcmp(r->asset_scopes[i].path, path) == 0) {
      if (!r->asset_scopes[i].game_local) r->asset_scopes[i].game_local = game_local;
      return;
    }
  if (r->scope_count == r->scope_cap) {
    r->scope_cap = r->scope_cap ? r->scope_cap * 2 : 16;
    r->asset_scopes = realloc(r->asset_scopes, r->scope_cap * sizeof *r->asset_scopes);
    if (!r->asset_scopes) qa_range_error("out of memory");
  }
  r->asset_scopes[r->scope_count].path = copy_text(path);
  r->asset_scopes[r->scope_count].game_local = game_local;
  r->scope_count++;
}
static bool scope_game_local(qa_app_q2_download_receiver *r, const char *path) {
  for (size_t i = 0; i < r->scope_count; i++)
    if (strcmp(r->asset_scopes[i].path, path) == 0) return r->asset_scopes[i].game_local;
  return false;
}
typedef struct http_current { qa_app_q2_download_receiver *r; double generation; } http_current;
static void check_current(http_current *current) {
  qa_app_q2_download_receiver *r = current->r;
  if (current->generation != r->generation || r->metadata_aborted)
    qa_range_error("Q2 download generation retired");
  if (!permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA))
    qa_range_error("Q2 HTTP download permission changed");
}
static void queue_assert_current(void *ctx) { check_current(ctx); }
static bool queue_resolved(void *ctx, const char *destination) {
  http_current *current = ctx;
  qa_app_q2_download_receiver *r = current->r;
  check_current(current);
  const char *path = http_resource(r, destination);
  if (!permitted(r, QA_APP_DL_HTTP, qa_app_client_download_category_for(path))) return true;
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  bool found = content->open_scoped(content->mounts.ctx, path, scope_game_local(r, path));
  check_current(current);
  return found;
}
static void queue_refresh_package(void *ctx, const char *destination) {
  http_current *current = ctx;
  qa_app_q2_download_receiver *r = current->r;
  http_resource(r, destination);
  if (!r->refresh_packages) qa_range_error("Q2 package refresh is unavailable");
  r->refresh_packages(r->refresh_ctx);
  check_current(current);
}
static void queue_progress(void *ctx, const char *path, double received, bool has_total, double total) {
  (void)ctx; (void)path; (void)received; (void)has_total; (void)total;
}
typedef struct validate_ctx { qa_app_q2_download_receiver *r; char *path; } validate_ctx;
static void http_validate(void *ctx, const char *staged) {
  validate_ctx *v = ctx;
  if (!permitted(v->r, QA_APP_DL_HTTP, qa_app_client_download_category_for(v->path)))
    qa_range_error("Q2 HTTP download permission changed");
  (void)staged;
}
static void http_validate_package(void *ctx, const char *staged) {
  validate_ctx *v = ctx;
  http_validate(ctx, staged);
  qa_archive_handle *archive = qa_open_archive(staged, true,
      ends_with_ci(v->path, ".pak") ? QA_ARCHIVE_FORMAT_PAK : QA_ARCHIVE_FORMAT_ZIP);
  qa_archive_destroy(archive);
}
static void http_asset_run(qa_app_q2_download_receiver *r, const char *path, bool package) {
  if (!r->http_live || !r->server || !r->state || list_contains(&r->attempted, path)
      || !permitted(r, QA_APP_DL_HTTP, qa_app_client_download_category_for(path))) return;
  list_push(&r->attempted, path);
  const char *gamedir = r->access.gamedir(r->state->data);
  const char *game = gamedir && gamedir[0] ? gamedir : "baseq2";
  char *encoded_game = encode_component(game);
  char *encoded_path = encode_path_segments(path);
  if (!encoded_game || !encoded_path) qa_range_error("out of memory");
  size_t rel_n = strlen(encoded_game) + strlen(encoded_path) + 2;
  char *relative = malloc(rel_n);
  if (!relative) qa_range_error("out of memory");
  snprintf(relative, rel_n, "%s/%s", encoded_game, encoded_path);
  free(encoded_game); free(encoded_path);
  char *url = url_join(r->server, relative);
  free(relative);
  char *destination = http_destination(r, path);
  http_paths_set(r, destination, path);
  validate_ctx validate = {r, copy_text(path)};
  qa_download_expectation expected = {true, NULL, 0, 0x7fffffff};
  qa_http_download_request request = {destination, url, package ? "package" : "asset",
      expected, package ? http_validate_package : http_validate, &validate};
  qa_http_download_queue_enqueue((qa_http_download_queue *)r->http_storage, request);
  qa_http_download_queue_pump((qa_http_download_queue *)r->http_storage);
  qa_http_download_result result;
  int have = qa_http_download_queue_result((qa_http_download_queue *)r->http_storage, destination, &result);
  free(validate.path); free(url); free(destination);
  if (!have) return;
  if (strcmp(result.kind, "failed") == 0) {
    const char *reason = result.reason ? result.reason : "HTTP download failed";
    char *message = copy_text(reason);
    qa_http_download_result_destroy(&result);
    qa_range_error("%s", message);
  }
  bool fallback = strcmp(result.kind, "fallback") == 0;
  qa_http_download_result_destroy(&result);
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  content->mounts.invalidate_listings(content->mounts.ctx);
  if (fallback)
    print_text(r, "HTTP unavailable: ", path, "; ",
        package ? "continuing with individual files\n" : "using native download\n");
}
static void http_initial_run(qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state) {
  if (!r->server || !permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) return;
  http_current current = {r, r->generation};
  const char *gamedir = r->access.gamedir(state->data);
  const char *game = gamedir && gamedir[0] ? gamedir : "baseq2";
  const char *map = config_string(r, state, 33);
  const qa_app_q2_remote_mounts *owner = r->content(r->content_ctx);
  char *parent = path_dirname(owner->write_root);
  mkdir_recursive(parent);
  check_current(&current);
  string_list assets = {0}, packages = {0};
  qa_http_queue_options options = {0};
  options.root = parent; options.ctx = &current;
  options.assert_current = queue_assert_current;
  options.resolved = queue_resolved;
  options.refresh_package = queue_refresh_package;
  options.progress = queue_progress;
  if (r->http_live) qa_http_download_queue_destroy((qa_http_download_queue *)r->http_storage);
  qa_http_download_queue_init((qa_http_download_queue *)r->http_storage, options);
  r->http_live = true;
  string_list lists = {0};
  char *encoded_game = encode_component(game);
  if (!encoded_game) qa_range_error("out of memory");
  size_t first_n = strlen(encoded_game) + 10;
  char *first = malloc(first_n);
  if (!first) qa_range_error("out of memory");
  snprintf(first, first_n, "%s.filelist", encoded_game);
  list_push(&lists, first);
  free(first);
  if (map) {
    validate_path(map);
    size_t map_length = strlen(map);
    /* TS map.slice(0, -4) drops the final four units unconditionally. */
    size_t stem = map_length >= 4 ? map_length - 4 : 0;
    char *stem_text = malloc(stem + 1);
    if (!stem_text) qa_range_error("out of memory");
    memcpy(stem_text, map, stem); stem_text[stem] = '\0';
    char *encoded_stem = encode_path_segments(stem_text);
    free(stem_text);
    size_t n = strlen(encoded_game) + strlen(encoded_stem) + 11;
    char *entry = malloc(n);
    if (!entry) qa_range_error("out of memory");
    snprintf(entry, n, "%s/%s.filelist", encoded_game, encoded_stem);
    free(encoded_stem);
    list_push(&lists, entry);
    free(entry);
    if (!list_contains(&assets, map)) list_push(&assets, map);
    scope_set(r, map, false);
  }
  free(encoded_game);
  for (size_t li = 0; li < lists.count; li++) {
    if (!permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) {
      list_destroy(&lists); list_destroy(&assets); list_destroy(&packages); free(parent);
      return;
    }
    char *url = url_join(r->server, lists.items[li]);
    size_t length = 0;
    uint8_t *bytes = qa_fetch_http_download_metadata(url, 1 << 20, &r->metadata_aborted, &length);
    free(url);
    check_current(&current);
    if (!bytes) continue;
    char *text = malloc(length + 1);
    if (!text) qa_range_error("out of memory");
    memcpy(text, bytes, length); text[length] = '\0';
    free(bytes);
    char *cursor = text;
    while (cursor) {
      char *newline = strchr(cursor, '\n');
      char *line = cursor;
      if (newline) *newline = '\0';
      size_t line_length = strlen(line);
      if (line_length && line[line_length - 1] == '\r') line[line_length - 1] = '\0';
      if (line[0] != '\0') {
        /* TS per-entry try/catch prints and continues; validate
         * without the aborting helpers here. */
        bool package = ends_with_ci(line, ".pak") || ends_with_ci(line, ".pkz");
        bool valid = true;
        if (package) {
          size_t count = 0;
          valid = strchr(line, '/') == NULL && charset_ok(line);
          if (valid) {
            char **parts = qa_download_path(line, &count);
            qa_contained_file_parts_free(parts, count);
          }
          if (valid && r->refresh_packages && !list_contains(&packages, line))
            list_push(&packages, line);
        } else {
          const char *path = line[0] == '@' ? line + 1 : line;
          valid = charset_ok(path) && strchr(path, '/') != NULL && filelist_extension_ok(path);
          if (valid) {
            size_t count = 0;
            char **parts = qa_download_path(path, &count);
            qa_contained_file_parts_free(parts, count);
          }
          if (valid) {
            if (!list_contains(&assets, path)) list_push(&assets, path);
            scope_set(r, path, line[0] == '@');
          }
        }
        if (!valid) {
          char shown[129];
          size_t shown_length = strlen(line) > 128 ? 128 : strlen(line);
          memcpy(shown, line, shown_length); shown[shown_length] = '\0';
          print_text(r, "Ignoring invalid Q2 filelist entry: ", shown, "\n", "");
        }
      }
      cursor = newline ? newline + 1 : NULL;
      if (cursor && *cursor == '\0') break;
    }
    free(text);
  }
  list_destroy(&lists);
  for (size_t i = 0; i < packages.count; i++) {
    http_asset_run(r, packages.items[i], true);
    check_current(&current);
  }
  list_destroy(&packages);
  qa_app_q2_layout layout = qa_app_q2_application_layout((qa_q2_protocol_identity){"q2-classic", 34, 0, 0});
  for (double index = 1; index < layout.max_models; index++) {
    const char *path = config_string(r, state, layout.models + index);
    if (path && path[0] && path[0] != '*' && path[0] != '#') {
      validate_path(path);
      if (!list_contains(&assets, path)) list_push(&assets, path);
      scope_set(r, path, false);
    }
  }
  for (double index = 1; index < layout.max_sounds; index++) {
    const char *name = config_string(r, state, layout.sounds + index);
    if (name && name[0] && name[0] != '*') {
      char *path;
      if (name[0] == '#') path = copy_text(name + 1);
      else {
        size_t n = strlen(name) + 7;
        path = malloc(n);
        if (!path) qa_range_error("out of memory");
        snprintf(path, n, "sound/%s", name);
      }
      validate_path(path);
      if (!list_contains(&assets, path)) list_push(&assets, path);
      scope_set(r, path, false);
      free(path);
    }
  }
  for (size_t i = 0; i < assets.count; i++) http_asset_run(r, assets.items[i], false);
  check_current(&current);
  list_destroy(&assets);
  free(parent);
}
/* TS start(): the async task runs now; completion (or its error) is
 * observed on the next prepare(), keeping the waiting sequence. */
static void start_task(qa_app_q2_download_receiver *r, int kind, const char *path) {
  double generation = r->generation;
  r->http_task = true; r->http_done = false;
  r->task_kind = kind;
  free(r->task_path); r->task_path = path ? copy_text(path) : NULL;
  /* Errors abort in C (the TS catch stores httpError); generation
   * checks inside the task abort with the retired message. */
  if (kind == 1) http_initial_run(r, r->state);
  else if (kind == 2) http_asset_run(r, path, false);
  if (generation == r->generation) r->http_done = true;
}
static void iter_destroy(resource_iter *it) {
  if (!it) return;
  free(it->map); free(it->world);
  list_destroy(&it->pending); list_destroy(&it->models);
  list_destroy(&it->sounds); list_destroy(&it->images);
  free(it->sky_tga); free(it->sky_pcx);
  free(it);
}
static void collect_names(qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state,
    double first, double count, string_list *out) {
  for (double index = 1; index < count; index++) {
    const char *value = config_string(r, state, first + index);
    if (value && value[0]) list_push(out, value);
  }
}
/* TS resources() async generator as an explicit iterator. Returns a
 * malloc'd path or NULL when exhausted. */
static char *iter_next(qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state,
    const qa_app_q2_remote_mounts *content) {
  resource_iter *it = r->paths;
  qa_app_q2_layout layout = qa_app_q2_application_layout((qa_q2_protocol_identity){"q2-classic", 34, 0, 0});
  for (;;) {
    if (it->pending_index < it->pending.count)
      return copy_text(it->pending.items[it->pending_index++]);
    switch (it->phase) {
      case 0: {
        const char *map = config_string(r, state, layout.models + 1);
        if (!map) qa_range_error("Q2 server supplied no world model");
        it->map = copy_text(map);
        it->phase = 1;
        return copy_text(map);
      }
      case 1: {
        uint8_t *bytes = NULL; size_t length = 0;
        const qa_resolved_resource_reference *reference = NULL;
        if (!content->mounts.open(content->mounts.ctx, it->map, &bytes, &length, &reference))
          qa_range_error("Q2 server map is unavailable: %s", it->map);
        it->world = bytes; it->world_length = length;
        const char *checksum = config_string(r, state, layout.map_checksum);
        bool ok = checksum && (uint32_t)strtod(checksum, NULL)
            == qa_block_checksum(bytes, length);
        if (!ok) qa_range_error("Q2 server map checksum differs from mounted content");
        collect_names(r, state, layout.models, layout.max_models, &it->models);
        collect_names(r, state, layout.sounds, layout.max_sounds, &it->sounds);
        collect_names(r, state, layout.images, layout.max_images, &it->images);
        it->phase = 2; it->index = 0;
        break;
      }
      case 2: {
        if (it->index >= it->models.count) { it->phase = 3; it->index = 0; break; }
        const char *model = it->models.items[it->index++];
        if (strcmp(model, it->map) == 0 || model[0] == '*' || model[0] == '#') break;
        uint8_t *bytes = NULL; size_t length = 0;
        const qa_resolved_resource_reference *reference = NULL;
        list_clear(&it->pending); it->pending_index = 0;
        list_push(&it->pending, model);
        if (content->mounts.open(content->mounts.ctx, model, &bytes, &length, &reference)) {
          if (ends_with_ci(model, ".md2")) {
            qa_q2_alias_model parsed;
            qa_parse_md2(bytes, length, model, &parsed);
            for (size_t i = 0; i < parsed.skins.count; i++)
              if (parsed.skins.values[i][0]) list_push(&it->pending, parsed.skins.values[i]);
            qa_q2_alias_model_destroy(&parsed);
          } else if (ends_with_ci(model, ".sp2")) {
            qa_q2_sprite_model parsed;
            qa_parse_sp2(bytes, length, model, &parsed);
            for (size_t i = 0; i < parsed.frame_count; i++)
              list_push(&it->pending, parsed.frames[i].image);
            /* Landed sprite model storage is released with its
             * model owner; strings were copied above. */
          }
          free(bytes);
        }
        break;
      }
      case 3: {
        if (it->index >= it->sounds.count) { it->phase = 4; it->index = 0; break; }
        const char *sound = it->sounds.items[it->index++];
        if (sound[0] == '*') break;
        if (sound[0] == '#') return copy_text(sound + 1);
        size_t n = strlen(sound) + 7;
        char *out = malloc(n);
        if (!out) qa_range_error("out of memory");
        snprintf(out, n, "sound/%s", sound);
        return out;
      }
      case 4: {
        if (it->index >= it->images.count) { it->phase = 5; it->index = 0; break; }
        const char *image = it->images.items[it->index++];
        if (image[0] == '/') return copy_text(image + 1);
        size_t n = strlen(image) + 10;
        char *out = malloc(n);
        if (!out) qa_range_error("out of memory");
        snprintf(out, n, "pics/%s.pcx", image);
        return out;
      }
      case 5: {
        if (it->index >= 256) { it->phase = 6; it->index = 0; it->sky_step = 0; break; }
        const char *value = config_string(r, state, layout.player_skins + (double)it->index++);
        if (!value || !value[0]) break;
        const char *backslash = strchr(value, '\\');
        const char *skin = backslash ? backslash + 1 : value;
        const char *slash = strchr(skin, '/');
        if (!slash || slash == skin) break;
        size_t model_length = (size_t)(slash - skin);
        const char *name = slash + 1;
        static const char *fixed[] = {"tris.md2", "weapon.md2", "weapon.pcx"};
        list_clear(&it->pending); it->pending_index = 0;
        for (size_t i = 0; i < 5; i++) {
          char buffer[512];
          if (i < 3) snprintf(buffer, sizeof buffer, "players/%.*s/%s", (int)model_length, skin, fixed[i]);
          else if (i == 3) snprintf(buffer, sizeof buffer, "players/%.*s/%s.pcx", (int)model_length, skin, name);
          else snprintf(buffer, sizeof buffer, "players/%.*s/%s_i.pcx", (int)model_length, skin, name);
          list_push(&it->pending, buffer);
        }
        break;
      }
      case 6: {
        const char *sky = config_string(r, state, 2);
        if (!sky || !sky[0] || it->index >= 6) { it->phase = 7; break; }
        if (it->sky_step == 0) {
          const char *suffix = qa_sky_face_suffixes[it->index];
          size_t n = strlen(sky) + strlen(suffix) + 10;
          free(it->sky_tga); free(it->sky_pcx);
          it->sky_tga = malloc(n); it->sky_pcx = malloc(n);
          if (!it->sky_tga || !it->sky_pcx) qa_range_error("out of memory");
          snprintf(it->sky_tga, n, "env/%s%s.tga", sky, suffix);
          snprintf(it->sky_pcx, n, "env/%s%s.pcx", sky, suffix);
          it->index++;
          if (content->mounts.exists(content->mounts.ctx, it->sky_tga)
              || content->mounts.exists(content->mounts.ctx, it->sky_pcx)) break;
          it->sky_step = 1;
          return copy_text(it->sky_tga);
        }
        it->sky_step = 0;
        if (!content->mounts.exists(content->mounts.ctx, it->sky_tga))
          return copy_text(it->sky_pcx);
        break;
      }
      case 7: {
        qa_q2_bsp bsp;
        qa_read_q2_bsp(it->world, it->world_length, it->map, &bsp);
        list_clear(&it->pending); it->pending_index = 0;
        for (size_t i = 0; i < bsp.texture_info_count; i++) {
          size_t n = strlen(bsp.texture_info[i].name) + 15;
          char *out = malloc(n);
          if (!out) qa_range_error("out of memory");
          snprintf(out, n, "textures/%s.wal", bsp.texture_info[i].name);
          list_push(&it->pending, out);
          free(out);
        }
        qa_q2_bsp_destroy(&bsp);
        it->phase = 8;
        break;
      }
      default: return NULL;
    }
  }
}
static void receiver_close(qa_app_q2_download_receiver *r) {
  r->retired_block = false; r->retry_requested = false;
  r->generation++;
  if (r->http_live) { qa_http_download_queue_cancel((qa_http_download_queue *)r->http_storage); qa_http_download_queue_destroy((qa_http_download_queue *)r->http_storage); r->http_live = false; }
  r->metadata_aborted = true;
  r->metadata_aborted = false; /* TS installs a fresh AbortController */
  r->http_task = false; r->http_done = false;
  free(r->http_error); r->http_error = NULL;
  list_clear(&r->attempted);
  for (size_t i = 0; i < r->http_path_count; i++) {
    free(r->http_paths[i].dest); free(r->http_paths[i].path);
  }
  r->http_path_count = 0;
  free(r->retry_path); r->retry_path = NULL;
  free(r->task_path); r->task_path = NULL; r->task_kind = 0;
  if (r->has_pending) {
    if (r->pending.has_sink) {
      qa_download_sink_close(&r->pending.sink);
      qa_download_sink_destroy(&r->pending.sink);
    }
    free(r->pending.path); r->has_pending = false;
  }
  iter_destroy(r->paths); r->paths = NULL;
  r->state = NULL;
  list_clear(&r->refused);
}
qa_app_q2_download_receiver *qa_app_q2_download_receiver_create(
    const qa_app_q2_remote_mounts *(*content)(void *ctx), void *content_ctx,
    void (*command)(void *ctx, const char *text), void *command_ctx,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    void (*refresh_packages)(void *ctx), void *refresh_ctx,
    const qa_app_client_download_permission *permission,
    qa_app_q2_state_access state_access) {
  qa_app_q2_download_receiver *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->content = content; r->content_ctx = content_ctx;
  r->command = command; r->command_ctx = command_ctx;
  r->print = print; r->print_ctx = print_ctx;
  r->refresh_packages = refresh_packages; r->refresh_ctx = refresh_ctx;
  r->permission = permission; r->access = state_access;
  return r;
}
void qa_app_q2_download_receiver_destroy(qa_app_q2_download_receiver *receiver) {
  if (!receiver) return;
  receiver_close(receiver);
  free(receiver->server);
  list_destroy(&receiver->attempted); list_destroy(&receiver->refused);
  free(receiver->http_paths);
  for (size_t i = 0; i < receiver->scope_count; i++) free(receiver->asset_scopes[i].path);
  free(receiver->asset_scopes);
  free(receiver);
}
size_t qa_app_q2_download_receiver_progress(qa_app_q2_download_receiver *receiver,
    qa_app_client_download_progress *out, size_t capacity) {
  size_t total = 0;
  if (receiver->http_live) {
    size_t count = qa_http_download_queue_progress_count((qa_http_download_queue *)receiver->http_storage);
    for (size_t i = 0; i < count; i++) {
      qa_download_progress item = qa_http_download_queue_progress_at((qa_http_download_queue *)receiver->http_storage, i);
      if (total < capacity) {
        qa_app_client_download_progress *row = &out[total];
        row->path = http_resource(receiver, item.path);
        row->transport = QA_APP_DL_HTTP;
        row->received = item.received;
        row->has_total = item.has_total; row->total = item.total;
        row->has_percent = item.has_total && item.total != 0;
        row->percent = row->has_percent ? item.received * 100 / item.total : 0;
        row->phase = strcmp(item.phase, "running") == 0 ? QA_APP_DL_RUNNING
            : strcmp(item.phase, "done") == 0 ? QA_APP_DL_DONE : QA_APP_DL_PENDING;
      }
      total++;
    }
  }
  if (receiver->has_pending) {
    if (total < capacity) {
      qa_app_client_download_progress *row = &out[total];
      row->path = receiver->pending.path; row->transport = QA_APP_DL_NATIVE;
      row->received = qa_download_sink_byte_length(&receiver->pending.sink);
      row->has_total = false; row->has_percent = true;
      row->percent = receiver->pending.percent; row->phase = QA_APP_DL_RUNNING;
    }
    total++;
  }
  return total;
}
void qa_app_q2_download_receiver_cancel(qa_app_q2_download_receiver *receiver) {
  if (receiver->cancelled) return;
  bool waiting = receiver->has_pending;
  receiver_close(receiver);
  receiver->cancelled = true; receiver->retired_block = waiting;
}
void qa_app_q2_download_receiver_retry(qa_app_q2_download_receiver *receiver) {
  if (receiver->retired_block) { receiver->retry_requested = true; return; }
  receiver_close(receiver);
  receiver->cancelled = false;
}
double qa_app_q2_download_receiver_revision(const qa_app_q2_download_receiver *receiver) {
  return receiver->generation;
}
void qa_app_q2_download_receiver_set_http_server(qa_app_q2_download_receiver *receiver,
    const char *server_url) {
  receiver_close(receiver);
  receiver->cancelled = false;
  free(receiver->server);
  receiver->server = server_url ? copy_text(server_url) : NULL;
}
qa_app_q2_download_preparation qa_app_q2_download_receiver_prepare(
    qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state) {
  if (r->cancelled) return QA_APP_Q2_PREP_CANCELED;
  const char *gamedir = r->access.gamedir(state->data);
  qa_remote_content_selection selected;
  if (!qa_remote_content_selection_make("q2-classic-baseq2", gamedir ? gamedir : "", &selected))
    qa_range_error("out of memory");
  const qa_app_q2_remote_mounts *owner = r->content(r->content_ctx);
  bool same = strcmp(owner->selection_base, selected.base) == 0
      && strcmp(owner->selection_directory, selected.directory) == 0;
  free(selected.directory);
  if (!same) qa_range_error("Q2 server game differs from prepared content");
  if (r->http_task && r->http_done) r->http_task = false;
  if (r->state != state) {
    receiver_close(r);
    r->state = state;
    owner->mounts.invalidate_listings(owner->mounts.ctx);
    if (r->server && permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) start_task(r, 1, NULL);
  }
  if (r->http_task) return QA_APP_Q2_PREP_WAITING;
  if (r->http_error) qa_range_error("%s", r->http_error);
  if (!r->paths) {
    r->paths = calloc(1, sizeof(resource_iter));
    if (!r->paths) qa_range_error("out of memory");
  }
  if (r->has_pending) return QA_APP_Q2_PREP_WAITING;
  double generation = r->generation;
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  for (;;) {
    char *path;
    if (r->retry_path) { path = r->retry_path; r->retry_path = NULL; }
    else path = iter_next(r, state, content);
    if (generation != r->generation) { free(path); return QA_APP_Q2_PREP_CANCELED; }
    if (!path) return QA_APP_Q2_PREP_READY;
    validate_path(path);
    bool installed = content->mounts.exists(content->mounts.ctx, path);
    if (generation != r->generation) { free(path); return QA_APP_Q2_PREP_CANCELED; }
    if (installed || list_contains(&r->refused, path)) { free(path); continue; }
    qa_app_client_download_category category = qa_app_client_download_category_for(path);
    if (r->http_live && !list_contains(&r->attempted, path)
        && permitted(r, QA_APP_DL_HTTP, category)) {
      r->retry_path = path;
      start_task(r, 2, path);
      return QA_APP_Q2_PREP_WAITING;
    }
    if (!permitted(r, QA_APP_DL_NATIVE, category)) { free(path); continue; }
    mkdir_recursive(root_for(r, path));
    if (generation != r->generation) { free(path); return QA_APP_Q2_PREP_CANCELED; }
    if (!permitted(r, QA_APP_DL_NATIVE, category)) { free(path); continue; }
    qa_download_expectation expected = {true, NULL, 0, 0x7fffffff};
    if (!qa_download_sink_create(&r->pending.sink, root_for(r, path), path, expected))
      qa_range_error("out of memory");
    r->pending.has_sink = true;
    r->pending.path = path; r->pending.percent = 0; r->has_pending = true;
    size_t n = strlen(path) + 10;
    char *command = malloc(n);
    if (!command) qa_range_error("out of memory");
    snprintf(command, n, "download %s", path);
    r->command(r->command_ctx, command);
    free(command);
    return QA_APP_Q2_PREP_WAITING;
  }
}
qa_app_q2_download_receive qa_app_q2_download_receiver_receive(
    qa_app_q2_download_receiver *r, const qa_q2_server_event *block) {
  if (r->retired_block) {
    r->retired_block = false;
    if (r->retry_requested) { r->retry_requested = false; r->cancelled = false; }
    return QA_APP_Q2_DL_COMPLETE;
  }
  if (!r->has_pending) return QA_APP_Q2_DL_UNSOLICITED;
  if (!permitted(r, QA_APP_DL_NATIVE, qa_app_client_download_category_for(r->pending.path))) {
    qa_download_sink_close(&r->pending.sink);
    qa_download_sink_destroy(&r->pending.sink);
    free(r->pending.path); r->has_pending = false;
    return QA_APP_Q2_DL_COMPLETE;
  }
  if (!block->has_bytes) {
    list_push(&r->refused, r->pending.path);
    print_text(r, "Server download unavailable: ", r->pending.path, "\n", "");
    qa_download_sink_close(&r->pending.sink);
    qa_download_sink_destroy(&r->pending.sink);
    free(r->pending.path); r->has_pending = false;
    return QA_APP_Q2_DL_COMPLETE;
  }
  if (block->percent < r->pending.percent || block->percent > 100) {
    receiver_close(r);
    qa_range_error("Invalid Q2 download progress");
  }
  qa_download_sink_append(&r->pending.sink, block->bytes, block->bytes_length);
  r->pending.percent = block->percent;
  if (block->percent == 100
      || (block->bytes_length == 0 && qa_download_sink_byte_length(&r->pending.sink) == 0)) {
    char *digest = qa_download_sink_finish(&r->pending.sink);
    free(digest);
    qa_download_sink_destroy(&r->pending.sink);
    free(r->pending.path); r->has_pending = false;
    const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
    content->mounts.invalidate_listings(content->mounts.ctx);
    return QA_APP_Q2_DL_COMPLETE;
  }
  if (block->bytes_length == 0) {
    receiver_close(r);
    qa_range_error("Q2 download made no progress");
  }
  r->command(r->command_ctx, "nextdl");
  return QA_APP_Q2_DL_WAITING;
}
void qa_app_q2_download_receiver_close(qa_app_q2_download_receiver *receiver) {
  receiver_close(receiver);
}
