/* Port of src/app/bootstrap/addon-library.ts (see addon_library.h for the C
 * accommodations). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/addon_library.h"
#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/content/archive.h"
#include "qa/content/catalog/addons.h"
#include "qa/contracts/content.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/common/session.h"
#include "qa/network/services/http_downloads.h"
#include "qa/persistence/value.h"
#include "qa/platform/files/contained.h"
#include "qa/platform/files/writable.h"

const char qa_app_addon_library_stop_label[] = "Cancel download";
const char qa_app_addon_library_create_label[] = "Import Quake ZIP";

/* TS InstalledAddon (start NULL is TS null). */
typedef struct installed_addon { char *directory, *group, *sha256, *title, *start; } installed_addon;
typedef struct installed_list { installed_addon *items; size_t count; } installed_list;

struct qa_app_addon_library {
  qa_app_addon_library_options options;
  char *root;
  qa_ui_library_menu_service service;
  qa_addon_package_list catalog;
  installed_list installed;
  /* Replaced catalogs and installed lists stay alive: selected/selectedLocal keep
   * the objects they were taken from, as the TS references do. */
  qa_addon_package_list *old_catalogs; size_t old_catalog_count;
  installed_list *old_installed; size_t old_installed_count;
  const qa_addon_package *selected;      /* NULL is TS null */
  const installed_addon *selected_local; /* NULL is TS null */
  char *message;
  bool pending, closed, cancelled, catalog_aborted, purged;
  size_t skipped_rows;
  qa_http_download_queue *queue;
  /* scope()/entries() results */
  char *scope;
  qa_ui_library_entry *entries; size_t entry_count;
  char **entry_strings; size_t entry_string_count;
};

/* ---- helpers ------------------------------------------------------------------------ */
static char *dup_text(const char *s) {
  char *copy = strdup(s ? s : "");
  if (!copy) qa_range_error("Out of memory");
  return copy;
}
static char *format(const char *fmt, ...) {
  va_list args; va_start(args, fmt);
  va_list copy; va_copy(copy, args);
  int length = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  char *out = length < 0 ? NULL : malloc((size_t)length + 1);
  if (!out) qa_range_error("Out of memory");
  vsnprintf(out, (size_t)length + 1, fmt, args);
  va_end(args);
  return out;
}
static char *join(const char *a, const char *b) { return format("%s/%s", a, b); }
static void set_message(qa_app_addon_library *library, const char *text) {
  char *next = dup_text(text);
  free(library->message);
  library->message = next;
}
static bool streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static bool lower_hex(const char *text, size_t length) {
  if (!text || strlen(text) != length) return false;
  for (const char *p = text; *p; p++) if (!((*p >= 'a' && *p <= 'f') || (*p >= '0' && *p <= '9'))) return false;
  return true;
}
/* /^qd_[a-f0-9]{20}_[a-f0-9]{20}$/ */
static bool managed_name(const char *name) {
  if (strlen(name) != 3 + 20 + 1 + 20 || strncmp(name, "qd_", 3) != 0 || name[23] != '_') return false;
  for (size_t i = 3; i < 44; i++) {
    if (i == 23) continue;
    char c = name[i];
    if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}
/* readFile: NULL with errno set on failure. */
static uint8_t *read_file(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  uint8_t *bytes = NULL;
  size_t length = 0, capacity = 0;
  for (;;) {
    if (length == capacity) {
      capacity = capacity ? capacity * 2 : 65536;
      uint8_t *grown = realloc(bytes, capacity);
      if (!grown) { free(bytes); fclose(file); qa_range_error("Out of memory"); }
      bytes = grown;
    }
    size_t read = fread(bytes + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  int failed = ferror(file);
  fclose(file);
  if (failed) { free(bytes); errno = EIO; return NULL; }
  *out_length = length;
  return bytes;
}
/* Node fs error text: "<CODE>: <description>, <syscall> '<path>'". */
static noreturn void fs_raise(int code, const char *syscall, const char *path) {
  const char *name = code == ENOENT ? "ENOENT" : code == EEXIST ? "EEXIST" : code == ENOTEMPTY ? "ENOTEMPTY"
      : code == EACCES ? "EACCES" : code == ENOTDIR ? "ENOTDIR" : code == EISDIR ? "EISDIR" : "EIO";
  const char *text = code == ENOENT ? "no such file or directory" : strerror(code);
  qa_raise("Error", code, 0, "%s: %s, %s '%s'", name, text, syscall, path);
}
static uint8_t *read_file_or_raise(const char *path, size_t *out_length) {
  uint8_t *bytes = read_file(path, out_length);
  if (!bytes) fs_raise(errno, "open", path);
  return bytes;
}
/* JSON.parse of a file; NULL when it cannot be read or parsed. */
static qa_save_value *read_json(const char *path) {
  size_t length = 0;
  uint8_t *bytes = read_file(path, &length);
  if (!bytes) return NULL;
  qa_save_value *value = qa_parse_json_value(bytes, length, true);
  free(bytes);
  return value;
}
/* rm(path, { force: true }) for a file. */
static void rm_force(const char *path) {
  if (unlink(path) != 0 && errno != ENOENT) fs_raise(errno, "rm", path);
}
/* rm(path, { recursive: true, force: true }). */
static void rm_tree(const char *path) {
  struct stat info;
  if (lstat(path, &info) != 0) { if (errno == ENOENT) return; fs_raise(errno, "rm", path); }
  if (S_ISDIR(info.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir) fs_raise(errno, "rm", path);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char *child = join(path, entry->d_name);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, child);
      rm_tree(child);
      qa_cleanup_pop(&cleanup);
      free(child);
    }
    closedir(dir);
    if (rmdir(path) != 0 && errno != ENOENT) fs_raise(errno, "rm", path);
  } else rm_force(path);
}
static void mkdir_recursive(const char *path) {
  char *copy = dup_text(path);
  for (char *p = copy + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(copy, 0777) != 0 && errno != EEXIST) { int code = errno; free(copy); fs_raise(code, "mkdir", path); }
    *p = '/';
  }
  if (mkdir(copy, 0777) != 0 && errno != EEXIST) { int code = errno; free(copy); fs_raise(code, "mkdir", path); }
  free(copy);
}
/* A growable text buffer for JSON.stringify output. */
typedef struct text_buffer { char *data; size_t length, capacity; } text_buffer;
static void buffer_append(text_buffer *buffer, const char *text, size_t n) {
  if (buffer->length + n + 1 > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity * 2 : 256;
    while (capacity < buffer->length + n + 1) capacity *= 2;
    char *grown = realloc(buffer->data, capacity);
    if (!grown) qa_range_error("Out of memory");
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, text, n);
  buffer->length += n;
  buffer->data[buffer->length] = '\0';
}
static void buffer_text(text_buffer *buffer, const char *text) { buffer_append(buffer, text, strlen(text)); }
/* JSON.stringify of a string (UTF-8 passes through; control characters escape). */
static void buffer_quote(text_buffer *buffer, const char *text) {
  buffer_text(buffer, "\"");
  for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
    char escape[8];
    unsigned char c = *s;
    if (c == '"') buffer_text(buffer, "\\\"");
    else if (c == '\\') buffer_text(buffer, "\\\\");
    else if (c == '\b') buffer_text(buffer, "\\b");
    else if (c == '\f') buffer_text(buffer, "\\f");
    else if (c == '\n') buffer_text(buffer, "\\n");
    else if (c == '\r') buffer_text(buffer, "\\r");
    else if (c == '\t') buffer_text(buffer, "\\t");
    else if (c < 0x20) { snprintf(escape, sizeof escape, "\\u%04x", c); buffer_text(buffer, escape); }
    else buffer_append(buffer, (const char *)s, 1);
  }
  buffer_text(buffer, "\"");
}
static void free_buffer(void *ctx) { free(((text_buffer *)ctx)->data); }
static void free_pointer(void *ctx) { free(*(char **)ctx); }
static void sha256_hex(const uint8_t *bytes, size_t length, char out[65]) { qa_net_sha256_hex(bytes, length, out); }

/* ---- lists -------------------------------------------------------------------------- */
static void installed_free(installed_list *list) {
  for (size_t i = 0; i < list->count; i++) {
    installed_addon *item = &list->items[i];
    free(item->directory); free(item->group); free(item->sha256); free(item->title); free(item->start);
  }
  free(list->items);
  memset(list, 0, sizeof *list);
}
static void retire_installed(qa_app_addon_library *library, installed_list next) {
  installed_list *grown = realloc(library->old_installed, (library->old_installed_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  library->old_installed = grown;
  library->old_installed[library->old_installed_count++] = library->installed;
  library->installed = next;
}
static void retire_catalog(qa_app_addon_library *library, qa_addon_package_list next) {
  qa_addon_package_list *grown = realloc(library->old_catalogs, (library->old_catalog_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  library->old_catalogs = grown;
  library->old_catalogs[library->old_catalog_count++] = library->catalog;
  library->catalog = next;
}

/* ---- public names ------------------------------------------------------------------- */
const char *qa_app_addon_content_parent(const char *edition) {
  return streq(edition, "rerelease") ? "q1/rerelease" : "q1";
}
char *qa_app_addon_product(const char *edition, const char *directory) {
  return format("q1-%s-%s", edition ? edition : "classic", directory);
}
static const char *content_parent(const qa_app_addon_library *library) { return qa_app_addon_content_parent(library->options.edition); }

/* ---- ensureDirectory / hide --------------------------------------------------------- */
static char *ensure_directory(qa_app_addon_library *library, const char *name) {
  mkdir_recursive(library->root);
  char *path = dup_text(library->root);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  struct stat info;
  if (lstat(path, &info) != 0) fs_raise(errno, "lstat", path);
  if (!S_ISDIR(info.st_mode)) qa_raise("Error", 0, 0, "Add-on root must not be a symlink");
  size_t part_count = 0;
  char **parts = qa_contained_file_parts(name, &part_count);
  if (!parts) qa_range_error("Out of memory");
  for (size_t i = 0; i < part_count; i++) {
    char *next = join(path, parts[i]);
    qa_cleanup_pop(&cleanup);
    free(path);
    path = next;
    qa_cleanup_push(&cleanup, free, path);
    if (mkdir(path, 0777) != 0 && errno != EEXIST) { qa_contained_file_parts_free(parts, part_count); fs_raise(errno, "mkdir", path); }
    if (lstat(path, &info) != 0) { qa_contained_file_parts_free(parts, part_count); fs_raise(errno, "lstat", path); }
    if (!S_ISDIR(info.st_mode)) {
      qa_contained_file_parts_free(parts, part_count);
      qa_raise("Error", 0, 0, "Add-on storage directory must not be a symlink");
    }
  }
  qa_contained_file_parts_free(parts, part_count);
  qa_cleanup_pop(&cleanup);
  return path;
}
static void hide(qa_app_addon_library *library, const char *directory) {
  qa_user_file_store store;
  if (!qa_user_file_store_init(&store, library->root)) qa_range_error("Out of memory");
  char *name = format(".addons/removed/%s/%s", content_parent(library), directory);
  qa_writable_binary_file file;
  bool opened = qa_user_file_store_open(&store, name, QA_WRITABLE_WRITE, NULL, NULL, &file);
  free(name);
  qa_user_file_store_destroy(&store);
  if (!opened) qa_raise("Error", 0, 0, "Cannot update installed add-on catalog");
  qa_writable_binary_file_close(&file);
}

/* ---- readInstalled / purgeHidden ---------------------------------------------------- */
typedef struct purge_call { qa_app_addon_library *library; const char *root, *name; } purge_call;
static void purge_body(void *ctx) {
  purge_call *call = ctx;
  char *directory = join(call->root, call->name);
  char *record = join(directory, ".quaddicted.json");
  qa_save_value *value = read_json(record); /* an unreadable record only leaves the cached package alone */
  free(record);
  char sha256[65] = "";
  if (value && value->kind == QA_SAVE_OBJECT) {
    const qa_save_value *sha = qa_save_value_get(value, "sha256");
    if (sha && sha->kind == QA_SAVE_STRING && lower_hex(sha->text, 64)) snprintf(sha256, sizeof sha256, "%s", sha->text);
  }
  qa_save_value_free(value);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, directory);
  rm_tree(directory);
  qa_cleanup_pop(&cleanup);
  free(directory);
  if (sha256[0] != '\0') {
    char *zip = format("%s/.addons/%s.zip", call->library->root, sha256);
    qa_cleanup_push(&cleanup, free, zip);
    rm_force(zip);
    qa_cleanup_pop(&cleanup);
    free(zip);
  }
  char *tombstone = format("%s/.addons/removed/%s/%s", call->library->root, content_parent(call->library), call->name);
  qa_cleanup_push(&cleanup, free, tombstone);
  rm_force(tombstone);
  qa_cleanup_pop(&cleanup);
  free(tombstone);
}
/* Removal only hides a copy so running worlds keep reading it; hidden copies are deleted at the next
 * start, when nothing has them mounted. */
static void purge_hidden(qa_app_addon_library *library, const char *root, const char *name) {
  purge_call call = {library, root, name};
  qa_caught_error caught;
  (void)qa_try(purge_body, &call, &caught); /* the copy stays hidden by its tombstone */
}
static bool entry_is_directory(const char *root, const struct dirent *entry) {
#ifdef DT_DIR
  if (entry->d_type == DT_DIR) return true;
  if (entry->d_type != DT_UNKNOWN) return false;
#endif
  char *path = join(root, entry->d_name);
  struct stat info;
  bool directory = lstat(path, &info) == 0 && S_ISDIR(info.st_mode);
  free(path);
  return directory;
}
typedef struct names { char **items; size_t count; } names;
static void names_free(void *ctx) {
  names *list = ctx;
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
}
void qa_app_addon_library_read_installed(qa_app_addon_library *library) {
  const char *parent = content_parent(library);
  char *root = join(library->root, parent);
  qa_cleanup cleanup_root;
  qa_cleanup_push(&cleanup_root, free, root);
  free(ensure_directory(library, parent));
  /* readdir(root, { withFileTypes: true }) */
  names directories = {0};
  DIR *dir = opendir(root);
  if (!dir) fs_raise(errno, "scandir", root);
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (!entry_is_directory(root, entry) || !managed_name(entry->d_name)) continue;
    char **grown = realloc(directories.items, (directories.count + 1) * sizeof *grown);
    if (!grown) { closedir(dir); qa_range_error("Out of memory"); }
    directories.items = grown;
    directories.items[directories.count++] = dup_text(entry->d_name);
  }
  closedir(dir);
  qa_cleanup cleanup_names;
  qa_cleanup_push(&cleanup_names, names_free, &directories);
  installed_list installed = {0};
  for (size_t i = 0; i < directories.count; i++) {
    const char *name = directories.items[i];
    char *relative = format("%s/%s", parent, name);
    bool hidden = qa_managed_addon_hidden(library->root, relative);
    free(relative);
    if (hidden) {
      if (!library->purged) purge_hidden(library, root, name);
      continue;
    }
    char *record = format("%s/%s/.quaddicted.json", root, name);
    qa_save_value *value = read_json(record); /* unmanaged directories are not offered for removal */
    free(record);
    if (!value) continue;
    const qa_save_value *sha = qa_save_value_get(value, "sha256"), *title = qa_save_value_get(value, "title"),
        *group = qa_save_value_get(value, "group"), *start = qa_save_value_get(value, "start");
    if (value->kind == QA_SAVE_OBJECT && sha && sha->kind == QA_SAVE_STRING && lower_hex(sha->text, 64) && title
        && title->kind == QA_SAVE_STRING && group && group->kind == QA_SAVE_STRING && start
        && (start->kind == QA_SAVE_NULL || start->kind == QA_SAVE_STRING)) {
      installed_addon *items = realloc(installed.items, (installed.count + 1) * sizeof *items);
      if (!items) qa_range_error("Out of memory");
      installed.items = items;
      installed.items[installed.count++] = (installed_addon){dup_text(name), dup_text(group->text), dup_text(sha->text),
          dup_text(title->text), start->kind == QA_SAVE_STRING ? dup_text(start->text) : NULL};
    }
    qa_save_value_free(value);
  }
  qa_cleanup_pop(&cleanup_names);
  names_free(&directories);
  qa_cleanup_pop(&cleanup_root);
  free(root);
  retire_installed(library, installed);
  library->purged = true;
}

/* ---- run() -------------------------------------------------------------------------- */
typedef void (*operation_fn)(qa_app_addon_library *library, void *ctx);
typedef struct operation_call { qa_app_addon_library *library; operation_fn operation; void *ctx; } operation_call;
static void operation_body(void *ctx) {
  operation_call *call = ctx;
  call->operation(call->library, call->ctx);
}
static void run(qa_app_addon_library *library, operation_fn operation, void *ctx) {
  if (library->closed || library->pending) return;
  library->cancelled = false;
  library->pending = true;
  operation_call call = {library, operation, ctx};
  qa_caught_error caught;
  if (!qa_try(operation_body, &call, &caught) && !library->closed) set_message(library, caught.message);
  library->pending = false;
}
static void cancel_queue(qa_app_addon_library *library) {
  if (library->queue) qa_http_download_queue_cancel(library->queue);
}

/* ---- refresh() ---------------------------------------------------------------------- */
typedef struct fetch_state { qa_app_addon_library *library; uint8_t *data; size_t length, capacity; bool oom; } fetch_state;
static size_t fetch_write(char *bytes, size_t size, size_t count, void *ctx) {
  fetch_state *state = ctx;
  size_t n = size * count;
  if (state->length + n > state->capacity) {
    size_t capacity = state->capacity ? state->capacity * 2 : 1 << 20;
    while (capacity < state->length + n) capacity *= 2;
    uint8_t *grown = realloc(state->data, capacity);
    if (!grown) { state->oom = true; return 0; }
    state->data = grown;
    state->capacity = capacity;
  }
  memcpy(state->data + state->length, bytes, n);
  state->length += n;
  return n;
}
static int fetch_progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
  (void)a; (void)b; (void)c; (void)d;
  fetch_state *state = ctx;
  return state->library->closed || state->library->catalog_aborted ? 1 : 0; /* AbortSignal.any([abort, request]) */
}
/* fetch(quaddictedCatalogUrl, { signal }) + response.ok + response.text(). */
static uint8_t *fetch_catalog(qa_app_addon_library *library, size_t *out_length) {
  CURL *curl = curl_easy_init();
  if (!curl) qa_range_error("Out of memory");
  fetch_state state = {library, NULL, 0, 0, false};
  curl_easy_setopt(curl, CURLOPT_URL, qa_quaddicted_catalog_url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L); /* AbortSignal.timeout(20000) */
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, fetch_progress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  if (state.oom) { free(state.data); qa_range_error("Out of memory"); }
  if (code == CURLE_ABORTED_BY_CALLBACK) { free(state.data); qa_raise("AbortError", 0, 0, "The operation was aborted."); }
  if (code == CURLE_OPERATION_TIMEDOUT) { free(state.data); qa_raise("TimeoutError", 0, 0, "The operation timed out."); }
  if (code != CURLE_OK) { free(state.data); qa_raise("Error", 0, 0, "%s", curl_easy_strerror(code)); }
  if (status < 200 || status > 299) { free(state.data); qa_raise("Error", 0, 0, "Quaddicted returned HTTP %ld", status); }
  *out_length = state.length;
  return state.data ? state.data : calloc(1, 1);
}
static void count_skipped(void *ctx, const char *reason) { (void)reason; ((qa_app_addon_library *)ctx)->skipped_rows++; }
typedef struct refresh_call { qa_app_addon_library *library; } refresh_call;
static void refresh_try_body(void *ctx) {
  qa_app_addon_library *library = ((refresh_call *)ctx)->library;
  library->catalog_aborted = false; /* this.catalogAbort = new AbortController() */
  size_t length = 0;
  uint8_t *text = fetch_catalog(library, &length);
  qa_cleanup cleanup_text;
  qa_cleanup_push(&cleanup_text, free, text);
  qa_save_value *data = qa_parse_json_value(text, length, true);
  if (!data) qa_raise("SyntaxError", 0, 0, "JSON Parse error: Unable to parse JSON string");
  qa_cleanup cleanup_data;
  qa_cleanup_push(&cleanup_data, (void (*)(void *))qa_save_value_free, data);
  qa_addon_package_list catalog = {0};
  if (!qa_parse_addon_catalog_reporting(data, count_skipped, library, &catalog)) qa_range_error("Out of memory");
  qa_cleanup cleanup_catalog;
  qa_cleanup_push(&cleanup_catalog, (void (*)(void *))qa_addon_package_list_free, &catalog);
  free(ensure_directory(library, ".addons"));
  qa_user_file_store store;
  if (!qa_user_file_store_init(&store, library->root)) qa_range_error("Out of memory");
  qa_writable_binary_file cached;
  bool opened = qa_user_file_store_open(&store, ".addons/quaddicted.json", QA_WRITABLE_WRITE, NULL, NULL, &cached);
  qa_user_file_store_destroy(&store);
  if (!opened) qa_raise("Error", 0, 0, "Cannot save add-on catalog");
  (void)qa_writable_binary_file_write(&cached, text, length);
  qa_writable_binary_file_close(&cached);
  qa_cleanup_pop(&cleanup_catalog);
  retire_catalog(library, catalog);
  qa_cleanup_pop(&cleanup_data);
  qa_save_value_free(data);
  qa_cleanup_pop(&cleanup_text);
  free(text);
}
static void cached_catalog_body(void *ctx) {
  qa_app_addon_library *library = ((refresh_call *)ctx)->library;
  char *path = format("%s/.addons/quaddicted.json", library->root);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  size_t length = 0;
  uint8_t *bytes = read_file_or_raise(path, &length);
  qa_save_value *data = qa_parse_json_value(bytes, length, true);
  free(bytes);
  if (!data) qa_raise("SyntaxError", 0, 0, "JSON Parse error");
  qa_cleanup cleanup_data;
  qa_cleanup_push(&cleanup_data, (void (*)(void *))qa_save_value_free, data);
  qa_addon_package_list catalog = {0};
  if (!qa_parse_addon_catalog(data, &catalog)) qa_range_error("Out of memory");
  retire_catalog(library, catalog);
  qa_cleanup_pop(&cleanup_data);
  qa_save_value_free(data);
  qa_cleanup_pop(&cleanup);
  free(path);
}
static void refresh_operation(qa_app_addon_library *library, void *unused) {
  (void)unused; /* run(async () => { ... }) captures only this */
  qa_app_addon_library_read_installed(library);
  set_message(library, "Reading Quaddicted catalog\xe2\x80\xa6");
  library->skipped_rows = 0;
  refresh_call call = {library};
  qa_caught_error error;
  if (!qa_try(refresh_try_body, &call, &error)) {
    if (library->closed) return;
    qa_caught_error ignored;
    if (!qa_try(cached_catalog_body, &call, &ignored)) qa_rethrow(&error);
    set_message(library, "Quaddicted unavailable \xe2\x80\x94 showing cached catalog");
    return;
  }
  char suffix[96] = "";
  if (library->skipped_rows != 0) snprintf(suffix, sizeof suffix, " (%zu invalid entries skipped)", library->skipped_rows);
  char *message = format("Quaddicted \xe2\x80\x94 %zu community packages%s", library->catalog.count, suffix);
  set_message(library, message);
  free(message);
}
void qa_app_addon_library_refresh(qa_app_addon_library *library) { run(library, refresh_operation, NULL); }

/* ---- install(selected) -------------------------------------------------------------- */
/* TS directory(item): qd_<sha256(group)[0..20)>_<item.sha256[0..20)>. */
static char *addon_directory(const qa_addon_package *item) {
  char group[65];
  sha256_hex((const uint8_t *)item->group, strlen(item->group), group);
  return format("qd_%.20s_%.20s", group, item->sha256);
}
typedef struct install_state {
  qa_app_addon_library *library;
  const qa_addon_package *selected;
  char *cache, *stage;
  qa_http_download_queue *queue;
  char *directory, *destination;
  const qa_addon_package **packages;
} install_state;
static void queue_assert_current(void *ctx) {
  install_state *state = ctx;
  if (state->library->closed || state->library->cancelled) qa_raise("Error", 0, 0, "Add-on browser closed or cancelled");
}
static bool queue_resolved(void *ctx, const char *path) {
  install_state *state = ctx;
  char *full = join(state->cache, path);
  size_t length = 0;
  uint8_t *bytes = read_file(full, &length);
  free(full);
  if (!bytes) return false;
  char hex[65];
  sha256_hex(bytes, length, hex);
  free(bytes);
  return strncmp(hex, path, 64) == 0;
}
static void queue_refresh_package(void *ctx, const char *path) { (void)ctx; (void)path; /* refreshPackage: async () => {} */ }
static void queue_progress(void *ctx, const char *path, double received, bool has_total, double total) {
  (void)path; /* progress: (_path, received, total) */
  install_state *state = ctx;
  char received_text[32], total_text[32];
  snprintf(received_text, sizeof received_text, "%s", QA_JS_NUMBER(received));
  snprintf(total_text, sizeof total_text, "%s", has_total ? QA_JS_NUMBER(total) : "?");
  char *message = format("Downloading %s: %s/%s bytes", state->selected->title, received_text, total_text);
  set_message(state->library, message);
  free(message);
}
static void rm_stage_body(void *ctx) { rm_tree(ctx); }
static void install_cleanup(void *ctx) {
  install_state *state = ctx;
  if (state->queue) {
    qa_http_download_queue_cancel(state->queue);
    qa_http_download_queue_free(state->queue);
    state->queue = NULL;
  }
  state->library->queue = NULL;
  if (state->stage) {
    qa_caught_error ignored; /* a cleanup runs while unwinding and must not raise */
    (void)qa_try(rm_stage_body, state->stage, &ignored);
  }
  free(state->cache); free(state->stage); free(state->directory); free(state->destination); free(state->packages);
  state->cache = state->stage = state->directory = state->destination = NULL;
  state->packages = NULL;
}
static void cancelled_check(qa_app_addon_library *library) {
  if (library->closed || library->cancelled) qa_raise("Error", 0, 0, "Add-on installation cancelled");
}
static void stage_package(install_state *state, const qa_addon_package *item, qa_user_file_store *files) {
  char *path = format("%s.zip", item->sha256);
  qa_cleanup cleanup_path;
  qa_cleanup_push(&cleanup_path, free, path);
  bool file_url = strncmp(item->url, "file:", 5) == 0;
  if (!file_url) {
    qa_http_download_request request;
    memset(&request, 0, sizeof request);
    request.path = path; request.url = item->url; request.kind = "asset";
    request.expected.digest = item->digest; request.expected.byte_length = (double)item->bytes;
    qa_http_download_queue_enqueue(state->queue, request);
    qa_http_download_queue_pump(state->queue);
    qa_http_download_result result;
    memset(&result, 0, sizeof result);
    (void)qa_http_download_queue_result(state->queue, path, &result);
    const char *kind = result.kind ? result.kind : "failed";
    if (strcmp(kind, "failed") == 0 || strcmp(kind, "fallback") == 0) {
      char reason[1024];
      snprintf(reason, sizeof reason, "%s", result.reason ? result.reason : "Download failed");
      qa_http_download_result_destroy(&result);
      qa_raise("Error", 0, 0, "%s", reason);
    }
    bool cancelled = strcmp(kind, "cancelled") == 0;
    qa_http_download_result_destroy(&result);
    if (cancelled) qa_raise("Error", 0, 0, "Add-on installation cancelled");
  }
  cancelled_check(state->library);
  char *archive_path = join(state->cache, path);
  qa_archive_handle *archive = qa_open_archive(archive_path, false, QA_ARCHIVE_FORMAT_ZIP);
  if (!archive) { int code = errno; qa_cleanup guard; qa_cleanup_push(&guard, free, archive_path); fs_raise(code ? code : ENOENT, "open", archive_path); }
  free(archive_path);
  qa_cleanup cleanup_archive;
  qa_cleanup_push(&cleanup_archive, (void (*)(void *))qa_archive_destroy, archive);
  for (size_t i = 0; i < archive->entry_count; i++) {
    const qa_archive_entry *entry = &archive->entries[i];
    cancelled_check(state->library);
    if (entry->is_directory) continue;
    bool found = false;
    char *target = qa_addon_install_path(item, entry->path, &found);
    if (!target) { if (found) qa_range_error("Out of memory"); continue; }
    qa_cleanup cleanup_target;
    qa_cleanup_push(&cleanup_target, free, target);
    size_t length = 0;
    uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
    qa_cleanup cleanup_bytes;
    qa_cleanup_push(&cleanup_bytes, free, bytes);
    qa_writable_binary_file file;
    if (!qa_user_file_store_open(files, target, QA_WRITABLE_WRITE, NULL, NULL, &file))
      qa_raise("Error", 0, 0, "Cannot stage %s", entry->path);
    int64_t written = qa_writable_binary_file_write(&file, bytes, length);
    qa_writable_binary_file_close(&file);
    if (written != (int64_t)length) qa_raise("Error", 0, 0, "Incomplete add-on write: %s", entry->path);
    qa_cleanup_pop(&cleanup_bytes);
    free(bytes);
    qa_cleanup_pop(&cleanup_target);
    free(target);
  }
  qa_cleanup_pop(&cleanup_archive);
  qa_archive_close(archive);
  qa_archive_destroy(archive);
  qa_cleanup_pop(&cleanup_path);
  free(path);
}
typedef struct changed_call { qa_app_addon_library *library; } changed_call;
static void changed_body(void *ctx) {
  qa_app_addon_library *library = ((changed_call *)ctx)->library;
  const char *error = library->options.changed(library->options.ctx);
  if (error) qa_raise("Error", 0, 0, "%s", error);
}
static void install(qa_app_addon_library *library, const qa_addon_package *selected) {
  qa_app_addon_library_read_installed(library);
  install_state state;
  memset(&state, 0, sizeof state);
  state.library = library;
  state.selected = selected;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, install_cleanup, &state);
  size_t package_count = 0;
  state.packages = qa_resolve_addon_packages(library->catalog.values, library->catalog.count, selected, &package_count);
  state.cache = ensure_directory(library, ".addons");
  char *template = format("%s/install-XXXXXX", state.cache);
  if (!mkdtemp(template)) { int code = errno; free(template); fs_raise(code, "mkdtemp", state.cache); }
  state.stage = template;
  qa_user_file_store files;
  if (!qa_user_file_store_init(&files, state.stage)) qa_range_error("Out of memory");
  qa_cleanup cleanup_files;
  qa_cleanup_push(&cleanup_files, (void (*)(void *))qa_user_file_store_destroy, &files);
  qa_http_queue_options queue_options;
  memset(&queue_options, 0, sizeof queue_options);
  queue_options.root = state.cache;
  queue_options.concurrency = 1; queue_options.has_concurrency = true;
  queue_options.ctx = &state;
  queue_options.assert_current = queue_assert_current;
  queue_options.resolved = queue_resolved;
  queue_options.refresh_package = queue_refresh_package;
  queue_options.progress = queue_progress;
  state.queue = qa_http_download_queue_create(queue_options);
  if (!state.queue) qa_range_error("Out of memory");
  library->queue = state.queue;
  for (size_t i = 0; i < package_count; i++) stage_package(&state, state.packages[i], &files);
  char *game_root = join(state.stage, selected->game_directory);
  qa_cleanup cleanup_root;
  qa_cleanup_push(&cleanup_root, free, game_root);
  state.directory = addon_directory(selected);
  char *parent = join(library->root, content_parent(library));
  state.destination = join(parent, state.directory);
  free(parent);
  const char *start = selected->start_count > 0 ? selected->starts[0] : NULL;
  /* writeFile(gameRoot/.quaddicted.json, JSON.stringify({ sha256, title, group, start })) */
  text_buffer json = {0};
  qa_cleanup cleanup_json;
  qa_cleanup_push(&cleanup_json, free_buffer, &json);
  buffer_text(&json, "{\"sha256\":"); buffer_quote(&json, selected->sha256);
  buffer_text(&json, ",\"title\":"); buffer_quote(&json, selected->title);
  buffer_text(&json, ",\"group\":"); buffer_quote(&json, selected->group);
  buffer_text(&json, ",\"start\":");
  if (start) buffer_quote(&json, start); else buffer_text(&json, "null");
  buffer_text(&json, "}");
  char *record = join(game_root, ".quaddicted.json");
  FILE *file = fopen(record, "wb");
  if (!file) { int code = errno; qa_cleanup guard; qa_cleanup_push(&guard, free, record); fs_raise(code, "open", record); }
  free(record);
  fwrite(json.data, 1, json.length, file);
  fclose(file);
  qa_cleanup_pop(&cleanup_json);
  free(json.data);
  cancelled_check(library);
  /* previous = installed.filter(item => item.group === selected.group && item.directory !== directory) */
  installed_list previous = {calloc(library->installed.count + 1, sizeof(installed_addon)), 0};
  if (!previous.items) qa_range_error("Out of memory");
  qa_cleanup cleanup_previous;
  qa_cleanup_push(&cleanup_previous, free, previous.items);
  for (size_t i = 0; i < library->installed.count; i++)
    if (streq(library->installed.items[i].group, selected->group) && !streq(library->installed.items[i].directory, state.directory))
      previous.items[previous.count++] = library->installed.items[i];
  if (rename(game_root, state.destination) != 0) {
    int code = errno;
    if (code != EEXIST && code != ENOTEMPTY) fs_raise(code, "rename", game_root);
    char *saved_path = join(state.destination, ".quaddicted.json");
    size_t saved_length = 0;
    uint8_t *saved_bytes = read_file(saved_path, &saved_length);
    if (!saved_bytes) { int read_code = errno; qa_cleanup guard; qa_cleanup_push(&guard, free, saved_path); fs_raise(read_code, "open", saved_path); }
    free(saved_path);
    qa_save_value *saved = qa_parse_json_value(saved_bytes, saved_length, true);
    free(saved_bytes);
    if (!saved) qa_raise("SyntaxError", 0, 0, "JSON Parse error");
    const qa_save_value *sha = saved->kind == QA_SAVE_OBJECT ? qa_save_value_get(saved, "sha256") : NULL;
    bool same = sha && sha->kind == QA_SAVE_STRING && streq(sha->text, selected->sha256);
    qa_save_value_free(saved);
    if (!same) qa_raise("Error", 0, 0, "Managed add-on destination conflicts with existing content");
  }
  char *removed = format("%s/removed/%s/%s", state.cache, content_parent(library), state.directory);
  qa_cleanup cleanup_removed;
  qa_cleanup_push(&cleanup_removed, free, removed);
  rm_force(removed);
  qa_cleanup_pop(&cleanup_removed);
  free(removed);
  for (size_t i = 0; i < previous.count; i++) hide(library, previous.items[i].directory);
  changed_call changed = {library};
  qa_caught_error error;
  if (!qa_try(changed_body, &changed, &error)) {
    hide(library, state.directory);
    for (size_t i = 0; i < previous.count; i++) {
      char *old = format("%s/removed/%s/%s", state.cache, content_parent(library), previous.items[i].directory);
      qa_cleanup_push(&cleanup_removed, free, old);
      rm_force(old);
      qa_cleanup_pop(&cleanup_removed);
      free(old);
    }
    qa_rethrow(&error);
  }
  qa_cleanup_pop(&cleanup_previous);
  free(previous.items);
  qa_cleanup_pop(&cleanup_root);
  free(game_root);
  qa_app_addon_library_read_installed(library);
  char *message = format("Installed %s \xe2\x80\x94 ready to play", selected->title);
  set_message(library, message);
  free(message);
  qa_cleanup_pop(&cleanup_files);
  qa_user_file_store_destroy(&files);
  qa_cleanup_pop(&cleanup);
  install_cleanup(&state);
}
static void install_operation(qa_app_addon_library *library, void *ctx) { install(library, ctx); }

/* ---- importLocal(path) -------------------------------------------------------------- */
static bool plain_directory_name(const char *name) {
  if (!name || !*name) return false;
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-')) return false;
  }
  return true;
}
static void package_cleanup(void *ctx) { qa_addon_package_free(ctx); }
static void import_operation(qa_app_addon_library *library, void *ctx) {
  const char *path = ctx;
  size_t length = 0;
  uint8_t *bytes = read_file_or_raise(path, &length);
  qa_cleanup cleanup_bytes;
  qa_cleanup_push(&cleanup_bytes, free, bytes);
  char sha256[65];
  sha256_hex(bytes, length, sha256);
  qa_archive_handle *archive = qa_open_archive(path, false, QA_ARCHIVE_FORMAT_ZIP);
  if (!archive) fs_raise(errno ? errno : ENOENT, "open", path);
  char *game_directory = dup_text("id1");
  qa_cleanup cleanup_directory;
  qa_cleanup_push(&cleanup_directory, free_pointer, &game_directory);
  qa_cleanup cleanup_archive;
  qa_cleanup_push(&cleanup_archive, (void (*)(void *))qa_archive_destroy, archive);
  if (archive->format != QA_ARCHIVE_FORMAT_ZIP) qa_raise("Error", 0, 0, "Select a Quake ZIP package");
  /* roots = new Set(files' first path segments) */
  const char *only = NULL;
  size_t only_length = 0, root_count = 0;
  for (size_t i = 0; i < archive->entry_count; i++) {
    if (archive->entries[i].is_directory) continue;
    const char *entry = archive->entries[i].path;
    const char *separator = strchr(entry, '/');
    size_t n = separator ? (size_t)(separator - entry) : strlen(entry);
    if (root_count == 0) { only = entry; only_length = n; root_count = 1; }
    else if (root_count == 1 && (n != only_length || strncmp(entry, only, n) != 0)) root_count = 2;
  }
  if (root_count == 1) {
    char *root = format("%.*s", (int)only_length, only);
    static const char *const reserved[] = {"maps", "progs", "gfx", "sound", "music", "textures"};
    bool is_reserved = false;
    for (size_t i = 0; i < 6; i++) if (strcmp(root, reserved[i]) == 0) is_reserved = true;
    if (!is_reserved && plain_directory_name(root)) { free(game_directory); game_directory = root; }
    else free(root);
  }
  qa_cleanup_pop(&cleanup_archive);
  qa_archive_close(archive);
  qa_archive_destroy(archive);
  const char *slash = strrchr(path, '/');
  const char *filename = slash ? slash + 1 : path;
  qa_addon_package item;
  memset(&item, 0, sizeof item);
  qa_cleanup_pop(&cleanup_directory);
  item.game_directory = game_directory;
  game_directory = NULL;
  qa_cleanup cleanup_item;
  qa_cleanup_push(&cleanup_item, package_cleanup, &item);
  item.digest = qa_create_content_digest(sha256);
  if (!item.digest) qa_range_error("Out of memory");
  item.sha256 = dup_text(sha256);
  size_t name_length = strlen(filename);
  bool zip = name_length >= 4 && strcasecmp(filename + name_length - 4, ".zip") == 0;
  item.title = format("%.*s", (int)(zip ? name_length - 4 : name_length), filename);
  item.filename = dup_text(filename);
  item.group = format("local:%s", filename);
  for (char *p = item.group + 6; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a'); /* toLowerCase (ASCII) */
  item.bytes = (int64_t)length;
  item.url = format("file://%s", path); /* pathToFileURL(path): only its protocol is read */
  item.mappings = calloc(1, sizeof *item.mappings);
  if (!item.mappings) qa_range_error("Out of memory");
  item.mappings[0].from = dup_text("");
  item.mappings[0].to = dup_text("");
  item.mapping_count = 1;
  char *cache = ensure_directory(library, ".addons");
  qa_user_file_store store;
  bool initialized = qa_user_file_store_init(&store, cache);
  free(cache);
  if (!initialized) qa_range_error("Out of memory");
  char *name = format("%s.zip", sha256);
  qa_writable_binary_file file;
  bool opened = qa_user_file_store_open(&store, name, QA_WRITABLE_WRITE, NULL, NULL, &file);
  free(name);
  qa_user_file_store_destroy(&store);
  if (!opened) qa_raise("Error", 0, 0, "Cannot cache local add-on");
  int64_t written = qa_writable_binary_file_write(&file, bytes, length);
  qa_writable_binary_file_close(&file);
  if (written != (int64_t)length) qa_raise("Error", 0, 0, "Incomplete local add-on copy");
  qa_cleanup_pop(&cleanup_item);
  qa_cleanup_pop(&cleanup_bytes);
  free(bytes);
  /* The imported item is the install's `selected`; it stays alive with the replaced catalogs. */
  qa_addon_package_list kept = {calloc(1, sizeof(qa_addon_package)), 1};
  if (!kept.values) qa_range_error("Out of memory");
  kept.values[0] = item;
  qa_addon_package_list *grown = realloc(library->old_catalogs, (library->old_catalog_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  library->old_catalogs = grown;
  library->old_catalogs[library->old_catalog_count++] = kept;
  install(library, &kept.values[0]);
}
void qa_app_addon_library_create_submit(qa_app_addon_library *library, const char *path) {
  char *copy = dup_text(path);
  run(library, import_operation, copy);
  free(copy);
}

/* ---- removeInstalled / launch ------------------------------------------------------- */
static void remove_installed(qa_app_addon_library *library, const installed_addon *item) {
  hide(library, item->directory);
  changed_call changed = {library};
  qa_caught_error error;
  if (!qa_try(changed_body, &changed, &error)) {
    char *removed = format("%s/.addons/removed/%s/%s", library->root, content_parent(library), item->directory);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, removed);
    rm_force(removed);
    qa_cleanup_pop(&cleanup);
    free(removed);
    qa_rethrow(&error);
  }
  qa_app_addon_library_read_installed(library);
}
static void local_remove_operation(qa_app_addon_library *library, void *ctx) {
  const installed_addon *local = ctx;
  remove_installed(library, local);
  library->selected_local = NULL;
  char *message = format("Removed %s", local->title);
  set_message(library, message);
  free(message);
}
static void remove_operation(qa_app_addon_library *library, void *ctx) {
  const installed_addon *installed = ctx;
  remove_installed(library, installed);
  char *message = format("Removed %s", installed->title);
  set_message(library, message);
  free(message);
}
static void launch_operation(qa_app_addon_library *library, void *ctx) {
  const installed_addon *local = ctx;
  char *product = qa_app_addon_product(library->options.edition, local->directory);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, product);
  library->options.launch(library->options.ctx, product, local->start);
  qa_cleanup_pop(&cleanup);
  free(product);
}
static const installed_addon *installed_by_group(const qa_app_addon_library *library, const char *group) {
  for (size_t i = 0; i < library->installed.count; i++)
    if (streq(library->installed.items[i].group, group)) return &library->installed.items[i];
  return NULL;
}
void qa_app_addon_library_activate(qa_app_addon_library *library, const char *id) {
  if (library->pending) return;
  if (streq(id, "back")) { library->selected = NULL; library->selected_local = NULL; return; }
  if (streq(id, "local-play") && library->selected_local) { run(library, launch_operation, (void *)library->selected_local); return; }
  if (streq(id, "local-remove") && library->selected_local) { run(library, local_remove_operation, (void *)library->selected_local); return; }
  for (size_t i = 0; i < library->installed.count; i++) {
    char *local_id = format("local:%s", library->installed.items[i].directory);
    bool match = streq(local_id, id);
    free(local_id);
    if (match) { library->selected_local = &library->installed.items[i]; return; }
  }
  const qa_addon_package *selected = library->selected;
  if (!selected) {
    library->selected = NULL;
    for (size_t i = 0; i < library->catalog.count && !library->selected; i++)
      if (streq(library->catalog.values[i].sha256, id)) library->selected = &library->catalog.values[i];
    return;
  }
  const installed_addon *installed = installed_by_group(library, selected->group);
  if (streq(id, "play") && installed) run(library, launch_operation, (void *)installed);
  else if (streq(id, "install")) run(library, install_operation, (void *)selected);
  else if (streq(id, "remove") && installed) run(library, remove_operation, (void *)installed);
}

/* ---- scope / status / entries ------------------------------------------------------- */
const char *qa_app_addon_library_scope(qa_app_addon_library *library) {
  char *scope = library->selected_local ? format("installed:%s", library->selected_local->directory)
      : library->selected ? format("package:%s", library->selected->sha256) : dup_text("catalog");
  free(library->scope);
  library->scope = scope;
  return scope;
}
const char *qa_app_addon_library_status(const qa_app_addon_library *library) { return library->message; }
static void entries_clear(qa_app_addon_library *library) {
  for (size_t i = 0; i < library->entry_string_count; i++) free(library->entry_strings[i]);
  free(library->entry_strings); library->entry_strings = NULL; library->entry_string_count = 0;
  free(library->entries); library->entries = NULL; library->entry_count = 0;
}
static const char *keep(qa_app_addon_library *library, char *text) {
  char **grown = realloc(library->entry_strings, (library->entry_string_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  library->entry_strings = grown;
  library->entry_strings[library->entry_string_count++] = text;
  return text;
}
static void push(qa_app_addon_library *library, size_t at, const char *id, const char *label, const char *detail, const char *unavailable) {
  qa_ui_library_entry *grown = realloc(library->entries, (library->entry_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  library->entries = grown;
  memmove(&library->entries[at + 1], &library->entries[at], (library->entry_count - at) * sizeof *grown);
  library->entries[at] = (qa_ui_library_entry){id, label, detail, unavailable};
  library->entry_count++;
}
const qa_ui_library_entry *qa_app_addon_library_entries(qa_app_addon_library *library, size_t *out_count) {
  entries_clear(library);
  const installed_addon *local = library->selected_local;
  if (local) {
    push(library, library->entry_count, "local-play", "Play", local->title, NULL);
    push(library, library->entry_count, "local-remove", "Remove installed add-on", local->title, NULL);
    push(library, library->entry_count, "back", "Back to add-ons", NULL, NULL);
  } else if (library->selected) {
    const qa_addon_package *selected = library->selected;
    const installed_addon *installed = installed_by_group(library, selected->group);
    if (installed) {
      push(library, library->entry_count, "play", "Play", installed->title, NULL);
      push(library, library->entry_count, "remove", "Remove installed add-on", "Only this managed copy", NULL);
    }
    if (!installed || !streq(installed->sha256, selected->sha256)) {
      const char *detail = keep(library, format("%s \xe2\x80\x94 %s KiB", selected->title, QA_JS_NUMBER(ceil((double)selected->bytes / 1024))));
      push(library, library->entry_count, "install", installed ? "Update" : "Install", detail, selected->unavailable);
    }
    push(library, library->entry_count, "back", "Back to add-ons", NULL, NULL);
  } else {
    for (size_t i = 0; i < library->catalog.count; i++) {
      const qa_addon_package *item = &library->catalog.values[i];
      push(library, library->entry_count, item->sha256, item->title, installed_by_group(library, item->group) ? "Installed" : "Quaddicted", NULL);
    }
    for (size_t i = 0; i < library->installed.count; i++) {
      const installed_addon *item = &library->installed.items[i];
      bool listed = false;
      for (size_t j = 0; j < library->entry_count && !listed; j++) listed = streq(library->entries[j].id, item->sha256);
      if (listed) continue;
      const char *id = keep(library, format("local:%s", item->directory));
      push(library, 0, id, item->title, "Installed \xe2\x80\x94 play", NULL);
    }
  }
  *out_count = library->entry_count;
  return library->entries;
}

/* ---- stop / settle / suspend / close ------------------------------------------------ */
void qa_app_addon_library_stop(qa_app_addon_library *library) {
  library->cancelled = true;
  library->catalog_aborted = true;
  cancel_queue(library);
}
void qa_app_addon_library_settle(qa_app_addon_library *library) { (void)library; /* operations finish inside run() */ }
void qa_app_addon_library_suspend(qa_app_addon_library *library) {
  library->cancelled = true;
  library->catalog_aborted = true;
  cancel_queue(library);
}
void qa_app_addon_library_close(qa_app_addon_library *library) {
  if (!library) return;
  library->closed = true;
  cancel_queue(library);
}

/* ---- LibraryMenuService view -------------------------------------------------------- */
static const char *service_scope(void *ctx) { return qa_app_addon_library_scope(ctx); }
static const qa_ui_library_entry *service_entries(void *ctx, size_t *out_count) { return qa_app_addon_library_entries(ctx, out_count); }
static const char *service_status(void *ctx) { return qa_app_addon_library_status(ctx); }
static void service_refresh(void *ctx) { qa_app_addon_library_refresh(ctx); }
static void service_activate(void *ctx, const char *id) { qa_app_addon_library_activate(ctx, id); }
static void service_create(void *ctx, const char *path) { qa_app_addon_library_create_submit(ctx, path); }
static void service_stop(void *ctx) { qa_app_addon_library_stop(ctx); }
const qa_ui_library_menu_service *qa_app_addon_library_service(qa_app_addon_library *library) { return &library->service; }

qa_app_addon_library *qa_app_addon_library_create(qa_app_addon_library_options options) {
  qa_app_addon_library *library = calloc(1, sizeof *library);
  if (!library) return NULL;
  library->options = options;
  library->root = strdup(options.root ? options.root : "");
  library->message = strdup("Quaddicted \xe2\x80\x94 community Quake add-ons");
  if (!library->root || !library->message) { free(library->root); free(library->message); free(library); return NULL; }
  if (options.edition) {
    library->options.edition = streq(options.edition, "rerelease") ? "rerelease" : "classic";
  }
  library->options.root = library->root;
  qa_ui_library_menu_service *service = &library->service;
  service->ctx = library;
  service->scope = service_scope;
  service->entries = service_entries;
  service->status = service_status;
  service->refresh = service_refresh;
  service->activate = service_activate;
  service->has_create = true;
  service->create_label = qa_app_addon_library_create_label;
  service->create_submit = service_create;
  service->has_stop = true;
  service->stop_label = qa_app_addon_library_stop_label;
  service->stop_activate = service_stop;
  return library;
}
void qa_app_addon_library_destroy(qa_app_addon_library *library) {
  if (!library) return;
  if (library->queue) qa_http_download_queue_free(library->queue);
  installed_free(&library->installed);
  for (size_t i = 0; i < library->old_installed_count; i++) installed_free(&library->old_installed[i]);
  free(library->old_installed);
  qa_addon_package_list_free(&library->catalog);
  for (size_t i = 0; i < library->old_catalog_count; i++) qa_addon_package_list_free(&library->old_catalogs[i]);
  free(library->old_catalogs);
  entries_clear(library);
  free(library->scope);
  free(library->message);
  free(library->root);
  free(library);
}
