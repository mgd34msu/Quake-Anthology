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
#include "qa/network/common/js_number.h"
#include "qa/materials/sky.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/services/http_downloads.h"
#include "qa/network/services/download_types.h"
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
static void free_arg(void *arg) { free(arg); }
/* TS `throw new Q2ServerDropError(message)`: Com_Error(ERR_DROP), the client leaves the server and
 * shows the message instead of ending the process. */
#define q2_server_drop(...) qa_raise("Q2ServerDropError", 0, 0, __VA_ARGS__)
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
  qa_cvar_registry_register(cvars, "sv_downloadserver", "", 0, &registered); qa_cvar_read_free(&registered);
  const char *def = edition_rerelease ? "1" : "0";
  qa_cvar_registry_register(cvars, "allow_download", def, QA_Q2_CVAR_FLAG_ARCHIVE, &registered); qa_cvar_read_free(&registered);
  qa_cvar_registry_register(cvars, "allow_download_players", def, QA_Q2_CVAR_FLAG_ARCHIVE, &registered); qa_cvar_read_free(&registered);
  static const char *names[] = {"allow_download_models", "allow_download_sounds", "allow_download_maps"};
  for (size_t i = 0; i < 3; i++) {
    qa_cvar_registry_register(cvars, names[i], "1", QA_Q2_CVAR_FLAG_ARCHIVE, &registered);
    qa_cvar_read_free(&registered);
  }
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
  char *result = qa_q2_read_download_server(args, 1, NULL);
  free(argument);
  return result;
}
static void check_download_path_body(void *ctx) { check_download_path(ctx); }
bool qa_app_q2_downloads_allowed(qa_app_q2_downloads *downloads, const char *name) {
  if (strstr(name, "..") || name[0] == '.' || !strchr(name, '/') || !qa_is_served_download_type(name)
      || qa_cvar_registry_variable_value(downloads->cvars, "allow_download") == 0) return false;
  /* TS try { downloadPath(name) } catch { return false } */
  qa_caught_error caught;
  if (!qa_try(check_download_path_body, (void *)name, &caught)) return false;
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
  if (!r->bytes) qa_raise("Error", 0, 0, "Q2 mounted download is closed");
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
typedef struct open_scope {
  qa_app_q2_downloads *downloads;
  const char *name;
  qa_download_source source;
  bool opened;
} open_scope;
static void open_body(void *arg) {
  open_scope *scope = (open_scope *)arg;
  scope->opened = qa_app_q2_downloads_open(scope->downloads, scope->name, &scope->source);
}
qa_q2_server_event *qa_app_q2_peer_download_begin(qa_app_q2_peer_download *peer,
    qa_app_q2_downloads *downloads, const char *name, const char *offset_text) {
  /* TS try { nativeAtoi } catch: landed atoi does not throw. */
  double offset = qa_native_atoi(offset_text ? offset_text : "0");
  if (offset < 0 || !qa_app_q2_downloads_allowed(downloads, name)) return refused_event();
  qa_app_q2_peer_download_close(peer);
  double generation = peer->generation;
  /* TS try { source = await downloads.open(name) } catch { source = null } */
  open_scope scope = {downloads, name, {0}, false};
  qa_caught_error caught;
  if (!qa_try(open_body, &scope, &caught)) scope.opened = false;
  qa_download_source source = scope.source;
  if (generation != peer->generation) { if (scope.opened) source.close(source.ctx); return NULL; }
  if (!scope.opened) return refused_event();
  qa_q2_download_sender_init(&peer->sender, source, true,
      qa_js_min(offset, source.byte_length), false, 0);
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
typedef struct http_current { struct qa_app_q2_download_receiver *r; double generation; } http_current;
typedef struct resource_iter {
  int phase; size_t index, sub_index;
  char *map; uint8_t *world; size_t world_length;
  string_list pending; size_t pending_index;
  string_list models, sounds, images;
  int sky_step; char *sky_tga, *sky_pcx;
  char *yielded_model; /* TS opens a model after yielding it, i.e. after its own download */
} resource_iter;
struct qa_app_q2_download_receiver {
  const qa_app_q2_remote_mounts *(*content)(void *); void *content_ctx;
  void (*command)(void *, const char *); void *command_ctx;
  void (*print)(void *, const char *); void *print_ctx;
  void (*refresh_packages)(void *); void *refresh_ctx;
  const qa_app_client_download_permission *permission;
  bool cancelled, retired_block, retry_requested;
  char *server;
  qa_http_download_queue *http; /* TS http (null when absent) */
  char *http_root; http_current http_current; /* the queue's root and assertCurrent closure */
  bool metadata_aborted;
  bool http_task, http_done; char *http_error; const char *http_error_name;
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
/* Q2ApplicationGameState as the receiver builds it: data is a qa_q2_server_data_result,
 * configStrings a qa_app_q2_config_snapshot (q2_client_receiver.h). */
static const char *config_string(const qa_app_q2_game_state *state, double index) {
  const qa_app_q2_config_snapshot *snapshot = state->config_strings;
  for (size_t i = 0; i < snapshot->count; i++)
    if (snapshot->entries[i].index == index) return snapshot->entries[i].value;
  return NULL;
}
static const char *state_gamedir(const qa_app_q2_game_state *state) {
  return ((const qa_q2_server_data_result *)state->data)->gamedir;
}
static void validate_path(const char *path) {
  check_download_path(path);
  bool ok = charset_ok(path);
  if (ok) {
    if (starts_with_ci(path, "maps/")) ok = ends_with_ci(path, ".bsp");
    else if (starts_with_ci(path, "models/") || starts_with_ci(path, "players/")
             || starts_with_ci(path, "sprites/"))
      ok = ends_with_ci(path, ".md2") || ends_with_ci(path, ".sp2")
          || ends_with_ci(path, ".pcx") || ends_with_ci(path, ".wav");
    else if (starts_with_ci(path, "sound/")) ok = ends_with_ci(path, ".wav");
    else if (starts_with_ci(path, "pics/")) ok = ends_with_ci(path, ".pcx");
    else if (starts_with_ci(path, "env/")) ok = ends_with_ci(path, ".tga") || ends_with_ci(path, ".pcx");
    else if (starts_with_ci(path, "textures/")) ok = ends_with_ci(path, ".wal");
    else ok = false;
  }
  if (!ok) qa_raise("Error", 0, 0, "Q2 server requested a non-asset download: %s", path);
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
  qa_raise("Error", 0, 0, "Unknown Q2 HTTP download destination");
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

static void check_current(http_current *current) {
  qa_app_q2_download_receiver *r = current->r;
  if (current->generation != r->generation || r->metadata_aborted)
    qa_raise("Error", 0, 0, "Q2 download generation retired");
  if (!permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA))
    qa_raise("Error", 0, 0, "Q2 HTTP download permission changed");
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
  if (!r->refresh_packages) qa_raise("Error", 0, 0, "Q2 package refresh is unavailable");
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
    qa_raise("Error", 0, 0, "Q2 HTTP download permission changed");
  (void)staged;
}
static void http_validate_package(void *ctx, const char *staged) {
  validate_ctx *v = ctx;
  http_validate(ctx, staged);
  qa_archive_handle *archive = qa_open_archive(staged, true,
      qa_js_ends_with_ignore_case(v->path, ".pak") ? QA_ARCHIVE_FORMAT_PAK : QA_ARCHIVE_FORMAT_ZIP);
  qa_archive_destroy(archive);
}
static void http_asset_run(qa_app_q2_download_receiver *r, const char *path, bool package) {
  if (!r->http || !r->server || !r->state || list_contains(&r->attempted, path)
      || !permitted(r, QA_APP_DL_HTTP, qa_app_client_download_category_for(path))) return;
  list_push(&r->attempted, path);
  const char *gamedir = state_gamedir(r->state);
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
  qa_download_expectation expected = {true, NULL, 0,
      package ? 1024.0 * 1024 * 1024 : 128.0 * 1024 * 1024}; /* MAX_HTTP_PACKAGE/ASSET_BYTES */
  qa_http_download_request request = {destination, url, package ? "package" : "asset",
      expected, package ? http_validate_package : http_validate, &validate};
  qa_http_download_queue_enqueue(r->http, request);
  qa_http_download_queue_pump(r->http);
  qa_http_download_result result;
  int have = qa_http_download_queue_result(r->http, destination, &result);
  free(validate.path); free(url); free(destination);
  if (!have) return;
  if (strcmp(result.kind, "failed") == 0) {
    const char *reason = result.reason ? result.reason : "HTTP download failed";
    char *message = copy_text(reason);
    qa_http_download_result_destroy(&result);
    if (!message) qa_range_error("out of memory");
    qa_cleanup node;
    qa_cleanup_push(&node, free_arg, message);
    qa_raise("Error", 0, 0, "%s", message);
  }
  bool fallback = strcmp(result.kind, "fallback") == 0;
  qa_http_download_result_destroy(&result);
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  content->mounts.invalidate_listings(content->mounts.ctx);
  if (fallback)
    print_text(r, "HTTP unavailable: ", path, "; ",
        package ? "continuing with individual files\n" : "using native download\n");
}
typedef struct filelist_entry {
  qa_app_q2_download_receiver *r;
  const char *line;
  size_t length;
  string_list *assets, *packages;
} filelist_entry;
/* One TS filelist try block. The line is a byte span; a NUL inside it fails validation. */
static void filelist_entry_body(void *arg) {
  filelist_entry *entry = (filelist_entry *)arg;
  char *line = malloc(entry->length + 1);
  if (!line) qa_range_error("out of memory");
  memcpy(line, entry->line, entry->length);
  line[entry->length] = '\0';
  qa_cleanup node;
  qa_cleanup_push(&node, free_arg, line);
  bool embedded_nul = strlen(line) != entry->length;
  if (ends_with_ci(line, ".pak") || ends_with_ci(line, ".pkz")) {
    check_download_path(line);
    if (embedded_nul || strchr(line, '/') != NULL || !charset_ok(line))
      qa_raise("Error", 0, 0, "Invalid package path");
    if (entry->r->refresh_packages && !list_contains(entry->packages, line))
      list_push(entry->packages, line);
  } else {
    const char *path = line[0] == '@' ? line + 1 : line;
    check_download_path(path);
    /* Donor HTTP whitelist plus JPEG/BMP/GIF supported by the shared image reader. */
    if (embedded_nul || !charset_ok(path) || strchr(path, '/') == NULL || !filelist_extension_ok(path))
      qa_raise("Error", 0, 0, "Q2 filelist requested a non-asset download: %s", path);
    if (!list_contains(entry->assets, path)) list_push(entry->assets, path);
    scope_set(entry->r, path, line[0] == '@');
  }
  qa_cleanup_pop(&node);
  free(line);
}
/* httpInitial's locals, released when a validation or currency check raises out of it. */
typedef struct initial_held {
  char *parent; string_list assets, packages, lists;
  char *path; char *encoded_game; uint8_t *bytes;
} initial_held;
static void initial_held_release(void *arg) {
  initial_held *held = arg;
  free(held->parent); held->parent = NULL;
  list_destroy(&held->assets); list_destroy(&held->packages); list_destroy(&held->lists);
  memset(&held->assets, 0, sizeof held->assets);
  memset(&held->packages, 0, sizeof held->packages);
  memset(&held->lists, 0, sizeof held->lists);
  free(held->path); held->path = NULL;
  free(held->encoded_game); held->encoded_game = NULL;
  free(held->bytes); held->bytes = NULL;
}
static void http_initial_run(qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state) {
  if (!r->server || !permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) return;
  initial_held held = {0};
  qa_cleanup node;
  qa_cleanup_push(&node, initial_held_release, &held);
  /* The queue keeps calling assertCurrent/resolved after this run returns (each asset task pumps
   * it), so the closure state and root live on the receiver with the queue. */
  r->http_current = (http_current){r, r->generation};
  http_current *current = &r->http_current;
  const char *gamedir = state_gamedir(state);
  const char *game = gamedir && gamedir[0] ? gamedir : "baseq2";
  const char *map = config_string(state, 33);
  const qa_app_q2_remote_mounts *owner = r->content(r->content_ctx);
  held.parent = path_dirname(owner->write_root);
  if (!held.parent) qa_range_error("out of memory");
  mkdir_recursive(held.parent);
  check_current(current);
  qa_http_queue_options options = {0};
  options.ctx = current;
  options.has_total_budget_bytes = true;
  options.total_budget_bytes = 2.0 * 1024 * 1024 * 1024; /* MAX_HTTP_SESSION_BYTES */
  options.assert_current = queue_assert_current;
  options.resolved = queue_resolved;
  options.refresh_package = queue_refresh_package;
  options.progress = queue_progress;
  if (r->http) qa_http_download_queue_free(r->http);
  r->http = NULL;
  free(r->http_root);
  r->http_root = held.parent;
  held.parent = NULL;
  options.root = r->http_root;
  r->http = qa_http_download_queue_create(options);
  if (!r->http) qa_range_error("out of memory");
  size_t listed = 0;
  held.encoded_game = encode_component(game);
  if (!held.encoded_game) qa_range_error("out of memory");
  const char *encoded_game = held.encoded_game;
  size_t first_n = strlen(encoded_game) + 10;
  held.path = malloc(first_n);
  if (!held.path) qa_range_error("out of memory");
  snprintf(held.path, first_n, "%s.filelist", encoded_game);
  list_push(&held.lists, held.path);
  free(held.path); held.path = NULL;
  if (map) {
    validate_path(map);
    size_t map_length = strlen(map);
    /* TS map.slice(0, -4) drops the final four units unconditionally. */
    size_t stem = map_length >= 4 ? map_length - 4 : 0;
    held.path = malloc(stem + 1);
    if (!held.path) qa_range_error("out of memory");
    memcpy(held.path, map, stem); held.path[stem] = '\0';
    char *encoded_stem = encode_path_segments(held.path);
    free(held.path); held.path = NULL;
    if (!encoded_stem) qa_range_error("out of memory");
    held.path = encoded_stem;
    size_t n = strlen(encoded_game) + strlen(encoded_stem) + 11;
    char *entry = malloc(n);
    if (!entry) qa_range_error("out of memory");
    snprintf(entry, n, "%s/%s.filelist", encoded_game, encoded_stem);
    free(held.path); held.path = entry;
    list_push(&held.lists, entry);
    free(held.path); held.path = NULL;
    if (!list_contains(&held.assets, map)) list_push(&held.assets, map);
    scope_set(r, map, false);
  }
  for (size_t li = 0; li < held.lists.count; li++) {
    if (!permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) {
      qa_cleanup_pop(&node);
      initial_held_release(&held);
      return;
    }
    held.path = url_join(r->server, held.lists.items[li]);
    if (!held.path) qa_range_error("out of memory");
    size_t length = 0;
    held.bytes = qa_fetch_http_download_metadata(held.path, 1 << 20, &r->metadata_aborted, &length);
    free(held.path); held.path = NULL;
    check_current(current);
    if (!held.bytes) continue;
    const uint8_t *bytes = held.bytes;
    /* TS new TextDecoder().decode(bytes).split('\n'): every segment counts toward the entry
     * cap, including the empty one after a final newline. */
    size_t start = 0;
    while (start <= length) {
      const uint8_t *newline = memchr(bytes + start, '\n', length - start);
      size_t end = newline ? (size_t)(newline - bytes) : length;
      if (++listed > 4096) {
        print_text(r, "Ignoring the rest of an oversized Q2 filelist\n", "", "", "");
        break;
      }
      size_t line_length = end - start;
      if (line_length && bytes[start + line_length - 1] == '\r') line_length--;
      if (line_length) {
        filelist_entry entry = {r, (const char *)bytes + start, line_length, &held.assets, &held.packages};
        qa_caught_error caught;
        if (!qa_try(filelist_entry_body, &entry, &caught)) {
          char shown[129];
          size_t shown_length = line_length > 128 ? 128 : line_length;
          memcpy(shown, bytes + start, shown_length);
          shown[shown_length] = '\0';
          print_text(r, "Ignoring invalid Q2 filelist entry: ", shown, "\n", "");
        }
      }
      if (!newline) break;
      start = end + 1;
    }
    free(held.bytes); held.bytes = NULL;
  }
  /* Native Q2 cannot download flat packages. Settle package publication/reload before assets. */
  for (size_t i = 0; i < held.packages.count; i++) {
    http_asset_run(r, held.packages.items[i], true);
    check_current(current);
  }
  qa_app_q2_layout layout = qa_app_q2_application_layout((qa_q2_protocol_identity){"q2-classic", 34, 0, 0});
  for (double index = 1; index < layout.max_models; index++) {
    const char *path = config_string(state, layout.models + index);
    if (path && path[0] && path[0] != '*' && path[0] != '#') {
      validate_path(path);
      if (!list_contains(&held.assets, path)) list_push(&held.assets, path);
      scope_set(r, path, false);
    }
  }
  for (double index = 1; index < layout.max_sounds; index++) {
    const char *name = config_string(state, layout.sounds + index);
    if (name && name[0] && name[0] != '*') {
      if (name[0] == '#') held.path = copy_text(name + 1);
      else {
        size_t n = strlen(name) + 7;
        held.path = malloc(n);
        if (held.path) snprintf(held.path, n, "sound/%s", name);
      }
      if (!held.path) qa_range_error("out of memory");
      validate_path(held.path);
      if (!list_contains(&held.assets, held.path)) list_push(&held.assets, held.path);
      scope_set(r, held.path, false);
      free(held.path); held.path = NULL;
    }
  }
  for (size_t i = 0; i < held.assets.count; i++) http_asset_run(r, held.assets.items[i], false);
  check_current(current);
  qa_cleanup_pop(&node);
  initial_held_release(&held);
}
/* TS start(): the async task runs now; completion (or its error) is
 * observed on the next prepare(), keeping the waiting sequence. */
typedef struct task_scope {
  qa_app_q2_download_receiver *r;
  int kind;
  const char *path;
} task_scope;
static void task_body(void *arg) {
  task_scope *scope = (task_scope *)arg;
  if (scope->kind == 1) http_initial_run(scope->r, scope->r->state);
  else if (scope->kind == 2) http_asset_run(scope->r, scope->path, false);
}
static void start_task(qa_app_q2_download_receiver *r, int kind, const char *path) {
  double generation = r->generation;
  r->http_task = true; r->http_done = false;
  r->task_kind = kind;
  free(r->task_path); r->task_path = path ? copy_text(path) : NULL;
  /* TS task.catch(cause => { if (current) this.httpError = cause }) */
  task_scope scope = {r, kind, path};
  qa_caught_error caught;
  if (!qa_try(task_body, &scope, &caught) && generation == r->generation) {
    free(r->http_error);
    r->http_error = copy_text(caught.message);
    r->http_error_name = caught.name;
    if (!r->http_error) qa_range_error("out of memory");
  }
  if (generation == r->generation) r->http_done = true;
}
static void iter_destroy(resource_iter *it) {
  if (!it) return;
  free(it->map); free(it->world);
  list_destroy(&it->pending); list_destroy(&it->models);
  list_destroy(&it->sounds); list_destroy(&it->images);
  free(it->sky_tga); free(it->sky_pcx);
  free(it->yielded_model);
  free(it);
}
/* TS names(first, count): configStrings first+1 .. first+count-1, empty ones dropped. */
static void collect_names(const qa_app_q2_game_state *state, double first, double count,
    string_list *out) {
  for (double index = 1; index < count; index++) {
    const char *value = config_string(state, first + index);
    if (value && value[0]) list_push(out, value);
  }
}
static char *joined_text(const char *a, const char *b, const char *c, const char *d) {
  size_t n = strlen(a) + strlen(b) + strlen(c) + strlen(d) + 1;
  char *out = malloc(n);
  if (!out) qa_range_error("out of memory");
  snprintf(out, n, "%s%s%s%s", a, b, c, d);
  return out;
}
typedef struct model_parse {
  const char *model; const uint8_t *bytes; size_t length;
  string_list *dependencies;
  bool md2; qa_q2_alias_model alias; bool sp2; qa_q2_sprite_model sprite;
} model_parse;
static void model_parse_release(void *arg) {
  model_parse *parse = arg;
  if (parse->md2) qa_q2_alias_model_destroy(&parse->alias);
  if (parse->sp2) qa_q2_sprite_model_destroy(&parse->sprite);
  parse->md2 = parse->sp2 = false;
}
static void model_parse_body(void *arg) {
  model_parse *parse = arg;
  qa_cleanup node;
  qa_cleanup_push(&node, model_parse_release, parse);
  if (ends_with_ci(parse->model, ".md2")) {
    qa_parse_md2(parse->bytes, parse->length, parse->model, &parse->alias);
    parse->md2 = true;
    for (size_t i = 0; i < parse->alias.skins.count; i++)
      if (parse->alias.skins.values[i][0]) list_push(parse->dependencies, parse->alias.skins.values[i]);
  } else if (ends_with_ci(parse->model, ".sp2")) {
    qa_parse_sp2(parse->bytes, parse->length, parse->model, &parse->sprite);
    parse->sp2 = true;
    for (size_t i = 0; i < parse->sprite.frame_count; i++)
      list_push(parse->dependencies, parse->sprite.frames[i].image);
  }
  qa_cleanup_pop(&node);
  model_parse_release(parse);
}
/* TS resources() async generator as an explicit iterator. Returns a malloc'd path or NULL when
 * exhausted. Work the generator does after a yield (opening the world or a model) runs on the
 * following call, after the yielded file had its chance to download. */
static char *iter_next(qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state,
    const qa_app_q2_remote_mounts *content) {
  resource_iter *it = r->paths;
  qa_app_q2_layout layout = qa_app_q2_application_layout((qa_q2_protocol_identity){"q2-classic", 34, 0, 0});
  for (;;) {
    if (it->pending_index < it->pending.count)
      return copy_text(it->pending.items[it->pending_index++]);
    switch (it->phase) {
      case 0: {
        const char *map = config_string(state, layout.models + 1);
        if (!map) qa_raise("Error", 0, 0, "Q2 server supplied no world model");
        it->map = copy_text(map);
        if (!it->map) qa_range_error("out of memory");
        it->phase = 1;
        return copy_text(map);
      }
      case 1: {
        uint8_t *bytes = NULL; size_t length = 0;
        const qa_resolved_resource_reference *reference = NULL;
        if (!content->mounts.open(content->mounts.ctx, it->map, &bytes, &length, &reference))
          q2_server_drop("Couldn't load %s", it->map);
        it->world = bytes; it->world_length = length;
        const char *checksum = config_string(state, layout.map_checksum);
        uint32_t world_checksum = qa_block_checksum(bytes, length);
        /* (Number(checksum) >>> 0) !== blockChecksum(world.bytes) */
        if (!checksum || qa_js_to_uint32(qa_js_string_to_number(checksum)) != world_checksum)
          q2_server_drop("Local map version differs from server: %d != '%s'",
                         (int)(int32_t)world_checksum, checksum ? checksum : "");
        collect_names(state, layout.models, layout.max_models, &it->models);
        it->phase = 2; it->index = 0;
        break;
      }
      case 2: {
        if (it->yielded_model) {
          char *model = it->yielded_model;
          it->yielded_model = NULL;
          qa_cleanup model_node;
          qa_cleanup_push(&model_node, free_arg, model);
          list_clear(&it->pending); it->pending_index = 0;
          uint8_t *bytes = NULL; size_t length = 0;
          const qa_resolved_resource_reference *reference = NULL;
          if (content->mounts.open(content->mounts.ctx, model, &bytes, &length, &reference)) {
            qa_cleanup bytes_node;
            qa_cleanup_push(&bytes_node, free_arg, bytes);
            /* CL_RequestNextDownload skips a model it cannot load; the skin check is simply lost. */
            string_list dependencies = {0};
            model_parse parse = {0};
            parse.model = model; parse.bytes = bytes; parse.length = length;
            parse.dependencies = &dependencies;
            qa_caught_error caught;
            if (qa_try(model_parse_body, &parse, &caught)) {
              for (size_t i = 0; i < dependencies.count; i++) list_push(&it->pending, dependencies.items[i]);
            } else {
              char shown[129];
              size_t shown_length = strlen(model) > 128 ? 128 : strlen(model);
              memcpy(shown, model, shown_length);
              shown[shown_length] = '\0';
              char *text = joined_text("Ignoring unreadable Q2 model ", shown, ": ", caught.message);
              qa_cleanup text_node;
              qa_cleanup_push(&text_node, free_arg, text);
              print_text(r, text, "\n", "", "");
              qa_cleanup_pop(&text_node);
              free(text);
            }
            list_destroy(&dependencies);
            qa_cleanup_pop(&bytes_node);
            free(bytes);
          }
          qa_cleanup_pop(&model_node);
          free(model);
          break;
        }
        if (it->index >= it->models.count) {
          collect_names(state, layout.sounds, layout.max_sounds, &it->sounds);
          it->phase = 3; it->index = 0;
          break;
        }
        const char *model = it->models.items[it->index++];
        if (strcmp(model, it->map) == 0 || model[0] == '*' || model[0] == '#') break;
        it->yielded_model = copy_text(model);
        if (!it->yielded_model) qa_range_error("out of memory");
        return copy_text(model);
      }
      case 3: {
        if (it->index >= it->sounds.count) {
          collect_names(state, layout.images, layout.max_images, &it->images);
          it->phase = 4; it->index = 0;
          break;
        }
        const char *sound = it->sounds.items[it->index++];
        if (sound[0] == '*') break;
        if (sound[0] == '#') return copy_text(sound + 1);
        return joined_text("sound/", sound, "", "");
      }
      case 4: {
        if (it->index >= it->images.count) { it->phase = 5; it->index = 0; break; }
        const char *image = it->images.items[it->index++];
        if (image[0] == '/') return copy_text(image + 1);
        return joined_text("pics/", image, ".pcx", "");
      }
      case 5: {
        if (it->index >= 256) { it->phase = 6; it->index = 0; it->sky_step = 0; break; }
        const char *value = config_string(state, layout.player_skins + (double)it->index++);
        if (!value || !value[0]) break;
        /* value.slice(value.indexOf('\\') + 1) */
        const char *backslash = strchr(value, '\\');
        const char *skin = backslash ? backslash + 1 : value;
        const char *slash = strchr(skin, '/');
        if (!slash || slash == skin) break;
        size_t model_length = (size_t)(slash - skin);
        char *model = malloc(model_length + 1);
        if (!model) qa_range_error("out of memory");
        memcpy(model, skin, model_length); model[model_length] = '\0';
        qa_cleanup model_node;
        qa_cleanup_push(&model_node, free_arg, model);
        const char *name = slash + 1;
        list_clear(&it->pending); it->pending_index = 0;
        const char *suffixes[5][2] = {{"tris.md2", ""}, {"weapon.md2", ""}, {"weapon.pcx", ""},
            {name, ".pcx"}, {name, "_i.pcx"}};
        for (size_t i = 0; i < 5; i++) {
          char *path = joined_text("players/", model, "/", suffixes[i][0]);
          char *full = joined_text(path, suffixes[i][1], "", "");
          free(path);
          list_push(&it->pending, full);
          free(full);
        }
        qa_cleanup_pop(&model_node);
        free(model);
        break;
      }
      case 6: {
        const char *sky = config_string(state, 2);
        if (!sky || !sky[0] || it->index >= 6) { it->phase = 7; break; }
        if (it->sky_step == 0) {
          const char *suffix = qa_sky_face_suffixes[it->index];
          free(it->sky_tga); free(it->sky_pcx);
          it->sky_tga = NULL; it->sky_pcx = NULL;
          it->sky_tga = joined_text("env/", sky, suffix, ".tga");
          it->sky_pcx = joined_text("env/", sky, suffix, ".pcx");
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
          char *out = joined_text("textures/", bsp.texture_info[i].name, ".wal", "");
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
  if (r->http) { qa_http_download_queue_cancel(r->http); qa_http_download_queue_free(r->http); r->http = NULL; }
  free(r->http_root); r->http_root = NULL;
  r->metadata_aborted = true;
  r->metadata_aborted = false; /* TS installs a fresh AbortController */
  r->http_task = false; r->http_done = false;
  free(r->http_error); r->http_error = NULL; r->http_error_name = NULL;
  list_clear(&r->attempted);
  for (size_t i = 0; i < r->http_path_count; i++) {
    free(r->http_paths[i].dest); free(r->http_paths[i].path);
  }
  r->http_path_count = 0;
  free(r->retry_path); r->retry_path = NULL;
  free(r->task_path); r->task_path = NULL; r->task_kind = 0;
  if (r->has_pending) {
    /* TS this.pending?.sink.suspend(): an interrupted native download keeps its staged bytes for resume. */
    if (r->pending.has_sink) {
      qa_download_sink_suspend(&r->pending.sink);
      qa_download_sink_destroy(&r->pending.sink);
    }
    free(r->pending.path); r->has_pending = false;
  }
  iter_destroy(r->paths); r->paths = NULL;
  r->state = NULL;
  list_clear(&r->refused);
  /* httpInitial's asset scope map belongs to the queue this close cancelled. */
  for (size_t i = 0; i < r->scope_count; i++) free(r->asset_scopes[i].path);
  r->scope_count = 0;
}
qa_app_q2_download_receiver *qa_app_q2_download_receiver_create(
    const qa_app_q2_remote_mounts *(*content)(void *ctx), void *content_ctx,
    void (*command)(void *ctx, const char *text), void *command_ctx,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    void (*refresh_packages)(void *ctx), void *refresh_ctx,
    const qa_app_client_download_permission *permission) {
  qa_app_q2_download_receiver *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->content = content; r->content_ctx = content_ctx;
  r->command = command; r->command_ctx = command_ctx;
  r->print = print; r->print_ctx = print_ctx;
  r->refresh_packages = refresh_packages; r->refresh_ctx = refresh_ctx;
  r->permission = permission;
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
  if (receiver->http != NULL) {
    size_t count = qa_http_download_queue_progress_count(receiver->http);
    for (size_t i = 0; i < count; i++) {
      qa_download_progress item = qa_http_download_queue_progress_at(receiver->http, i);
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
typedef struct exists_scope {
  const qa_app_q2_remote_mounts *content;
  const char *path;
  bool installed;
} exists_scope;
static void exists_body(void *arg) {
  exists_scope *scope = (exists_scope *)arg;
  scope->installed = scope->content->mounts.exists(scope->content->mounts.ctx, scope->path);
}
static void validate_body(void *arg) { validate_path(arg); }
typedef struct prepare_scope {
  qa_app_q2_download_receiver *r;
  const qa_app_q2_game_state *state;
  double generation;
  char *path; /* owned while the loop holds it */
  qa_app_q2_download_preparation result;
} prepare_scope;
static void prepare_body(void *arg) {
  prepare_scope *scope = (prepare_scope *)arg;
  qa_app_q2_download_receiver *r = scope->r;
  const qa_app_q2_game_state *state = scope->state;
  double generation = scope->generation;
  const qa_app_q2_remote_mounts *content = r->content(r->content_ctx);
  for (;;) {
    char *path;
    if (r->retry_path) { path = r->retry_path; r->retry_path = NULL; }
    else path = iter_next(r, state, content);
    if (generation != r->generation) { free(path); scope->path = NULL; scope->result = QA_APP_Q2_PREP_CANCELED; return; }
    if (!path) { scope->result = QA_APP_Q2_PREP_READY; return; }
    scope->path = path;
    /* An unresolvable path is not installed; validation below refuses it. */
    exists_scope exists = {content, path, false};
    qa_caught_error exists_caught;
    if (!qa_try(exists_body, &exists, &exists_caught)) exists.installed = false;
    bool installed = exists.installed;
    if (generation != r->generation) { free(path); scope->path = NULL; scope->result = QA_APP_Q2_PREP_CANCELED; return; }
    if (installed || list_contains(&r->refused, path)) { free(path); scope->path = NULL; continue; }
    qa_caught_error invalid;
    if (!qa_try(validate_body, path, &invalid)) {
      /* cl_parse.c prints and goes on with the next resource. */
      char shown[129];
      size_t shown_length = strlen(path) > 128 ? 128 : strlen(path);
      memcpy(shown, path, shown_length);
      shown[shown_length] = '\0';
      print_text(r, "Refusing Q2 download of ", shown, "\n", "");
      if (!list_contains(&r->refused, path)) list_push(&r->refused, path);
      free(path);
      scope->path = NULL;
      continue;
    }
    qa_app_client_download_category category = qa_app_client_download_category_for(path);
    if (r->http != NULL && !list_contains(&r->attempted, path)
        && permitted(r, QA_APP_DL_HTTP, category)) {
      r->retry_path = path;
      scope->path = NULL;
      start_task(r, 2, path);
      scope->result = QA_APP_Q2_PREP_WAITING;
      return;
    }
    if (!permitted(r, QA_APP_DL_NATIVE, category)) { free(path); scope->path = NULL; continue; }
    mkdir_recursive(root_for(r, path));
    if (generation != r->generation) { free(path); scope->path = NULL; scope->result = QA_APP_Q2_PREP_CANCELED; return; }
    if (!permitted(r, QA_APP_DL_NATIVE, category)) { free(path); scope->path = NULL; continue; }
    qa_download_expectation expected = {true, NULL, 0, 0x7fffffff};
    if (!qa_download_sink_create_resumable(&r->pending.sink, root_for(r, path), path, expected))
      qa_range_error("out of memory");
    r->pending.has_sink = true;
    r->pending.path = path; r->pending.percent = 0; r->has_pending = true;
    scope->path = NULL;
    /* cl_parse.c continues from the length of the staged file. */
    double resumed = r->pending.sink.resumed_from;
    size_t n = strlen(path) + 40;
    char *command = malloc(n);
    if (!command) qa_range_error("out of memory");
    if (resumed > 0) snprintf(command, n, "download %s %s", path, QA_JS_NUMBER(resumed));
    else snprintf(command, n, "download %s", path);
    r->command(r->command_ctx, command);
    free(command);
    scope->result = QA_APP_Q2_PREP_WAITING;
    return;
  }
}
qa_app_q2_download_preparation qa_app_q2_download_receiver_prepare(
    qa_app_q2_download_receiver *r, const qa_app_q2_game_state *state) {
  if (r->cancelled) return QA_APP_Q2_PREP_CANCELED;
  const char *gamedir = state_gamedir(state);
  qa_remote_content_selection selected;
  if (!qa_remote_content_selection_make("q2-classic-baseq2", gamedir ? gamedir : "", &selected))
    qa_range_error("out of memory");
  const qa_app_q2_remote_mounts *owner = r->content(r->content_ctx);
  bool same = strcmp(owner->selection_base, selected.base) == 0
      && strcmp(owner->selection_directory, selected.directory) == 0;
  free(selected.directory);
  if (!same) qa_raise("Error", 0, 0, "Q2 server game differs from prepared content");
  if (r->http_task && r->http_done) r->http_task = false;
  if (r->state != state) {
    receiver_close(r);
    r->state = state;
    owner->mounts.invalidate_listings(owner->mounts.ctx);
    if (r->server && permitted(r, QA_APP_DL_HTTP, QA_APP_DL_METADATA)) start_task(r, 1, NULL);
  }
  if (r->http_task) return QA_APP_Q2_PREP_WAITING;
  /* TS `throw this.httpError`: the stored error, outside the try that closes the receiver. */
  if (r->http_error) qa_raise(r->http_error_name, 0, 0, "%s", r->http_error);
  if (!r->paths) {
    r->paths = calloc(1, sizeof(resource_iter));
    if (!r->paths) qa_range_error("out of memory");
  }
  if (r->has_pending) return QA_APP_Q2_PREP_WAITING;
  /* TS try { ... } catch (error) { if (generation !== this.generation) return 'canceled';
   * this.close(); throw error; } */
  prepare_scope scope = {r, state, r->generation, NULL, QA_APP_Q2_PREP_CANCELED};
  qa_caught_error caught;
  bool ok = qa_try(prepare_body, &scope, &caught);
  if (ok) return scope.result;
  free(scope.path);
  if (scope.generation != r->generation) return QA_APP_Q2_PREP_CANCELED;
  receiver_close(r);
  qa_rethrow(&caught);
}
typedef struct receive_scope {
  qa_app_q2_download_receiver *r;
  const qa_q2_server_event *block;
  qa_app_q2_download_receive result;
} receive_scope;
static void receive_body(void *arg);
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
  /* TS try { ... } catch (error) { this.close(); throw error; } */
  receive_scope scope = {r, block, QA_APP_Q2_DL_COMPLETE};
  qa_caught_error caught;
  if (qa_try(receive_body, &scope, &caught)) return scope.result;
  /* catch: pending.sink.close(); this.pending = null; this.close(); throw */
  if (r->has_pending) {
    if (r->pending.has_sink) {
      qa_download_sink_close(&r->pending.sink);
      qa_download_sink_destroy(&r->pending.sink);
    }
    free(r->pending.path); r->has_pending = false;
  }
  receiver_close(r);
  qa_rethrow(&caught);
}
static void receive_body(void *arg) {
  receive_scope *scope = (receive_scope *)arg;
  qa_app_q2_download_receiver *r = scope->r;
  const qa_q2_server_event *block = scope->block;
  if (!block->has_bytes) {
    if (!list_contains(&r->refused, r->pending.path)) list_push(&r->refused, r->pending.path);
    print_text(r, "Server download unavailable: ", r->pending.path, "\n", "");
    qa_download_sink_close(&r->pending.sink);
    qa_download_sink_destroy(&r->pending.sink);
    free(r->pending.path); r->has_pending = false;
    scope->result = QA_APP_Q2_DL_COMPLETE;
    return;
  }
  if (block->percent < r->pending.percent || block->percent > 100)
    qa_raise("Error", 0, 0, "Invalid Q2 download progress");
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
    scope->result = QA_APP_Q2_DL_COMPLETE;
    return;
  }
  if (block->bytes_length == 0) qa_raise("Error", 0, 0, "Q2 download made no progress");
  r->command(r->command_ctx, "nextdl");
  scope->result = QA_APP_Q2_DL_WAITING;
}
void qa_app_q2_download_receiver_close(qa_app_q2_download_receiver *receiver) {
  receiver_close(receiver);
}
