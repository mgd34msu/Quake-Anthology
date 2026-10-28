/* Port of src/app/bootstrap/startup-saves.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_saves.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static bool ends_ci(const char *s, const char *suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  if (a < b) return false;
  return strcasecmp(s + a - b, suffix) == 0;
}
static char *join_path(const char *dir, const char *name) {
  size_t a = strlen(dir), b = strlen(name);
  char *r = malloc(a + 1 + b + 1);
  if (!r) qa_range_error("Out of memory");
  snprintf(r, a + 1 + b + 1, "%s/%s", dir, name);
  return r;
}
char *qa_app_resolve_external_save_path(const char *const *directories, size_t directory_count, const char *name) {
  char filename_buf[1024];
  if (ends_ci(name, ".sav")) snprintf(filename_buf, sizeof filename_buf, "%s", name);
  else snprintf(filename_buf, sizeof filename_buf, "%s.sav", name);
  if (strchr(filename_buf, '/') || strchr(filename_buf, '\\') || strcmp(filename_buf, ".sav") == 0) return NULL;
  for (size_t i = 0; i < directory_count; i++) {
    char *direct = join_path(directories[i], filename_buf);
    if (access(direct, F_OK) == 0) return direct;
    free(direct);
    DIR *dir = opendir(directories[i]);
    if (dir == NULL) continue;
    struct dirent *entry;
    char *found = NULL;
    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_type != DT_DIR) continue;
      char *subdir = join_path(directories[i], entry->d_name);
      char *nested = join_path(subdir, filename_buf);
      free(subdir);
      if (access(nested, F_OK) == 0) { found = nested; break; }
      free(nested);
    }
    closedir(dir);
    if (found) return found;
  }
  return NULL;
}
typedef struct cached_row { char *path; double mtime_ms; double size; qa_app_startup_save_row row; } cached_row;
struct qa_app_startup_saves {
  qa_app_startup_saves_host host;
  char *directory;
  char **external; size_t external_count;
  qa_app_startup_save_list current;
  char **path_ids; char **paths; size_t path_count;
  cached_row *cache; size_t cache_count, cache_cap;
};
static void free_row(qa_app_startup_save_row *row) {
  free(row->id); free(row->label); free(row->map); free(row->game); free(row->unavailable);
}
static void free_list(qa_app_startup_save_list *list) {
  for (size_t i = 0; i < list->row_count; i++) free_row(&list->rows[i]);
  free(list->rows); free(list->error);
  memset(list, 0, sizeof *list);
}
qa_app_startup_saves *qa_app_startup_saves_create(qa_app_startup_saves_host host, const char *directory,
                                                  const char *const *external_directories, size_t external_count) {
  qa_app_startup_saves *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->host = host;
  s->directory = xdup(directory);
  s->external = calloc(external_count ? external_count : 1, sizeof *s->external);
  if (!s->external) qa_range_error("Out of memory");
  s->external_count = external_count;
  for (size_t i = 0; i < external_count; i++) s->external[i] = xdup(external_directories[i]);
  return s;
}
void qa_app_startup_saves_destroy(qa_app_startup_saves *s) {
  if (!s) return;
  free_list(&s->current);
  for (size_t i = 0; i < s->path_count; i++) { free(s->path_ids[i]); free(s->paths[i]); }
  free(s->path_ids); free(s->paths);
  for (size_t i = 0; i < s->cache_count; i++) { free(s->cache[i].path); free_row(&s->cache[i].row); }
  free(s->cache);
  for (size_t i = 0; i < s->external_count; i++) free(s->external[i]);
  free(s->external); free(s->directory); free(s);
}
const char *qa_app_startup_saves_directory(const qa_app_startup_saves *s) { return s->directory; }
const qa_app_startup_save_list *qa_app_startup_saves_list(const qa_app_startup_saves *s) { return &s->current; }
const char *qa_app_startup_saves_path(qa_app_startup_saves *s, const char *id) {
  const qa_app_startup_save_row *row = NULL;
  const char *path = NULL;
  for (size_t i = 0; i < s->current.row_count; i++) if (strcmp(s->current.rows[i].id, id) == 0) row = &s->current.rows[i];
  for (size_t i = 0; i < s->path_count; i++) if (strcmp(s->path_ids[i], id) == 0) path = s->paths[i];
  if (row == NULL || path == NULL) qa_range_error("This saved game is no longer listed. Refresh the saved games.");
  if (row->unavailable != NULL) qa_range_error("%s", row->unavailable);
  return path;
}
char *qa_app_startup_saves_named_path(qa_app_startup_saves *s, const char *name) {
  char *label = qa_input_js_trim(name);
  if (!label) qa_range_error("Out of memory");
  size_t len = strlen(label);
  bool ok = len >= 1 && len <= 48;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)label[i];
    /* TS \p{L}\p{N} is Unicode; bytes >= 0x80 pass here as letters. */
    if (!(isalnum(c) || c == ' ' || c == '-' || c == '_' || c >= 0x80)) ok = false;
  }
  if (!ok) qa_range_error("Use 1-48 letters, numbers, spaces, - or _.");
  /* mkdir -p */
  char *walk = xdup(s->directory);
  for (char *p = walk + 1; *p; p++) if (*p == '/') { *p = '\0'; mkdir(walk, 0777); *p = '/'; }
  mkdir(walk, 0777);
  free(walk);
  char file[1100];
  snprintf(file, sizeof file, "%s.sav", label);
  char *path = join_path(s->directory, file);
  free(label);
  if (access(path, F_OK) == 0) { free(path); qa_range_error("Name exists. Select its slot to overwrite."); }
  return path;
}
typedef struct found_file { char *path; char *id; } found_file;
static void scan_dir(qa_app_startup_saves *s, const char *directory, const char *root, const char *external_prefix,
                     found_file **files, size_t *count, size_t *cap, char ***seen, size_t *seen_count, size_t *seen_cap, bool *error) {
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    struct stat st;
    if (stat(directory, &st) != 0) return; /* ENOENT */
    *error = true;
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char *path = join_path(directory, entry->d_name);
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
      scan_dir(s, path, root, external_prefix, files, count, cap, seen, seen_count, seen_cap, error);
      free(path);
      continue;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || !(ends_ci(entry->d_name, ".sav") || ends_ci(entry->d_name, ".pak.gz"))) {
      free(path);
      continue;
    }
    bool dup = false;
    for (size_t i = 0; i < *seen_count; i++) if (strcmp((*seen)[i], path) == 0) dup = true;
    bool skip_pak = external_prefix == NULL && ends_ci(path, ".pak.gz");
    if (dup || skip_pak) { free(path); continue; }
    if (*seen_count == *seen_cap) { *seen_cap = *seen_cap ? *seen_cap * 2 : 16; *seen = realloc(*seen, *seen_cap * sizeof **seen); if (!*seen) qa_range_error("Out of memory"); }
    (*seen)[(*seen_count)++] = xdup(path);
    if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; *files = realloc(*files, *cap * sizeof **files); if (!*files) qa_range_error("Out of memory"); }
    const char *relative = path + strlen(root);
    if (*relative == '/') relative++;
    char *id;
    if (external_prefix != NULL) {
      size_t need = strlen(external_prefix) + strlen(relative) + 1;
      id = malloc(need);
      if (!id) qa_range_error("Out of memory");
      snprintf(id, need, "%s%s", external_prefix, relative);
    } else id = xdup(relative);
    (*files)[*count].path = path;
    (*files)[*count].id = id;
    (*count)++;
  }
  closedir(dir);
  (void)s;
}
static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}
static int row_compare(const void *left, const void *right) {
  const qa_app_startup_save_row *a = left, *b = right;
  if (a->saved_at_milliseconds != b->saved_at_milliseconds) return a->saved_at_milliseconds > b->saved_at_milliseconds ? -1 : 1;
  return strcmp(a->id, b->id);
}
void qa_app_startup_saves_refresh(qa_app_startup_saves *s) {
  found_file *files = NULL; size_t count = 0, cap = 0;
  char **seen = NULL; size_t seen_count = 0, seen_cap = 0;
  bool had_error = false;
  scan_dir(s, s->directory, s->directory, NULL, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
  for (size_t i = 0; i < s->external_count; i++) {
    char prefix[64];
    snprintf(prefix, sizeof prefix, "external/%zu/", i);
    scan_dir(s, s->external[i], s->external[i], prefix, &files, &count, &cap, &seen, &seen_count, &seen_cap, &had_error);
  }
  for (size_t i = 0; i < seen_count; i++) free(seen[i]);
  free(seen);
  qa_app_startup_save_row *rows = calloc(count ? count : 1, sizeof *rows);
  char **path_ids = calloc(count ? count : 1, sizeof *path_ids);
  char **paths = calloc(count ? count : 1, sizeof *paths);
  if (!rows || !path_ids || !paths) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const char *path = files[i].path;
    const char *id = files[i].id;
    path_ids[i] = xdup(id);
    paths[i] = xdup(path);
    struct stat st;
    bool has_identity = stat(path, &st) == 0;
    double mtime_ms = has_identity ? (double)st.st_mtime * 1000.0 + (double)st.st_mtim.tv_nsec / 1e6 : 0;
    double size = has_identity ? (double)st.st_size : 0;
    cached_row *cached = NULL;
    if (has_identity) for (size_t j = 0; j < s->cache_count; j++) if (strcmp(s->cache[j].path, path) == 0) cached = &s->cache[j];
    if (cached != NULL && cached->mtime_ms == mtime_ms && cached->size == size) {
      rows[i] = cached->row;
      rows[i].id = xdup(id);
      rows[i].label = xdup(cached->row.label);
      rows[i].map = xdup(cached->row.map);
      rows[i].game = xdup(cached->row.game);
      rows[i].unavailable = cached->row.unavailable ? xdup(cached->row.unavailable) : NULL;
      continue;
    }
    qa_app_startup_save_detail detail; memset(&detail, 0, sizeof detail);
    if (ends_ci(path, ".pak.gz")) {
      const char *base = base_name(path);
      size_t base_len = strlen(base) - 7; /* .pak.gz */
      detail.label = qa_input_dup_range(base, base_len);
      detail.map = xdup("Unknown map");
      detail.game = xdup("Quake II (rerelease)");
      detail.saved_at_milliseconds = mtime_ms;
      detail.unavailable = xdup("Retail Quake II saves with recorded client or level state cannot be restored by this engine yet.");
      s->host.inspect_pak(s->host.ctx, path, &detail);
    } else {
      const char *base = base_name(path);
      size_t name_len = strlen(base) - 4; /* .sav */
      char *name = qa_input_dup_range(base, name_len);
      if (!name) qa_range_error("Out of memory");
      char *lower = xdup(name);
      for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);
      if (strcmp(lower, "autosave") == 0) detail.label = xdup("Autosave");
      else if (strcmp(lower, "quicksave") == 0) detail.label = xdup("Quicksave");
      else {
        detail.label = xdup(name);
        for (char *p = detail.label; *p; p++) if (*p == '_') *p = ' ';
      }
      free(name); free(lower);
      detail.map = xdup("Unknown map");
      detail.game = xdup("Unknown game");
      detail.saved_at_milliseconds = mtime_ms;
      s->host.inspect_sav(s->host.ctx, path, &detail);
    }
    rows[i].id = xdup(id);
    rows[i].label = detail.label;
    rows[i].map = detail.map;
    rows[i].game = detail.game;
    rows[i].saved_at_milliseconds = detail.saved_at_milliseconds;
    rows[i].unavailable = detail.unavailable;
    if (has_identity) {
      cached_row *slot = cached;
      if (slot == NULL) {
        if (s->cache_count == s->cache_cap) { s->cache_cap = s->cache_cap ? s->cache_cap * 2 : 16; s->cache = realloc(s->cache, s->cache_cap * sizeof *s->cache); if (!s->cache) qa_range_error("Out of memory"); }
        slot = &s->cache[s->cache_count++];
        memset(slot, 0, sizeof *slot);
        slot->path = xdup(path);
      } else free_row(&slot->row);
      slot->mtime_ms = mtime_ms;
      slot->size = size;
      slot->row = rows[i];
      slot->row.id = xdup(rows[i].id);
      slot->row.label = xdup(rows[i].label);
      slot->row.map = xdup(rows[i].map);
      slot->row.game = xdup(rows[i].game);
      slot->row.unavailable = rows[i].unavailable ? xdup(rows[i].unavailable) : NULL;
    }
  }
  qsort(rows, count, sizeof *rows, row_compare);
  /* paths map order need not follow rows; ids are the keys. */
  for (size_t i = 0; i < s->path_count; i++) { free(s->path_ids[i]); free(s->paths[i]); }
  free(s->path_ids); free(s->paths);
  s->path_ids = path_ids; s->paths = paths; s->path_count = count;
  free_list(&s->current);
  s->current.rows = rows;
  s->current.row_count = count;
  s->current.error = had_error ? xdup("Some saved games could not be read. Check access and refresh.") : NULL;
  for (size_t i = 0; i < count; i++) { free(files[i].path); free(files[i].id); }
  free(files);
}
