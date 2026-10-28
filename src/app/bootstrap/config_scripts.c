/* Port of src/app/bootstrap/config-scripts.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/config_scripts.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static char *join_path(const char *a, const char *b) {
  size_t cap = strlen(a) + strlen(b) + 2;
  char *path = malloc(cap);
  if (path) snprintf(path, cap, "%s/%s", a, b);
  return path;
}
char *qa_app_console_config_root(const char *user_content_root) {
  char *root = user_content_root ? dup(user_content_root) : qa_default_user_content_root();
  char *path = join_path(root, "console");
  free(root);
  return path;
}
bool qa_app_seat_console_config(const char *root, qa_seat_id seat, qa_config_store *out) {
  char seat_part[64];
  snprintf(seat_part, sizeof seat_part, "settings/seat-%lld", (long long)seat.index);
  char *path = join_path(root, seat_part);
  bool ok = qa_config_store_init(out, path);
  free(path);
  return ok;
}
static const qa_command_origin *caller_origin(const qa_command_origin *origin) {
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  return origin;
}
static char *read_file_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *text = malloc((size_t)size + 1);
  size_t read = size > 0 ? fread(text, 1, (size_t)size, file) : 0;
  text[read] = '\0'; /* latin1 bytes preserved except embedded NUL bounds */
  fclose(file);
  return text;
}
static void config_names(const char *root, const char *prefix, char ***names, size_t *count, size_t *cap) {
  char full[8192];
  snprintf(full, sizeof full, "%s%s%s", root, prefix[0] ? "/" : "", prefix);
  DIR *dir = opendir(full);
  if (!dir) {
    if (qa_is_missing_file(errno)) return;
    qa_range_error("could not list configuration directory");
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char name[8192];
    snprintf(name, sizeof name, "%s%s%s", prefix, prefix[0] ? "/" : "", entry->d_name);
    char disk[16384];
    snprintf(disk, sizeof disk, "%s/%s", root, name);
    struct stat st;
    if (stat(disk, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) config_names(root, name, names, count, cap);
    else if (S_ISREG(st.st_mode)) {
      size_t len = strlen(name);
      bool cfg = len >= 4 && name[len-4] == '.' && tolower((unsigned char)name[len-3]) == 'c' &&
          tolower((unsigned char)name[len-2]) == 'f' && tolower((unsigned char)name[len-1]) == 'g';
      if (cfg) {
        if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; *names = realloc(*names, *cap * sizeof **names); }
        (*names)[(*count)++] = dup(name);
      }
    }
  }
  closedir(dir);
}
static char *legacy_config_path(qa_app_legacy_console_config_sources sources) {
  char *shared = NULL;
  if (qa_find_content_path(sources.shared_root, "config.cfg", QA_CONTENT_PATH_CASE_INSENSITIVE, &shared)) {
    struct stat st;
    if (stat(shared, &st) == 0 && S_ISREG(st.st_mode)) return shared;
    free(shared);
  }
  char *newest = NULL;
  double modified = -1;
  for (size_t i = 0; i < sources.game_root_count; i++) {
    char *path = NULL;
    if (!qa_find_content_path(sources.game_roots[i], "config.cfg", QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) continue;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      double mtime = (double)st.st_mtime * 1000.0 + (double)st.st_mtim.tv_nsec / 1e6;
      if (!newest || mtime > modified) { free(newest); newest = path; modified = mtime; continue; }
    }
    free(path);
  }
  return newest;
}
char *qa_app_read_console_script(qa_app_read_console_script_options options) {
  char *name = qa_normalize_resource_path(options.name);
  const qa_command_origin *origin = caller_origin(&options.source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)
    qa_range_error("Remote clients cannot read local configuration scripts");
  char *result = NULL;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    char relative[8192];
    snprintf(relative, sizeof relative, "settings/seat-%lld/%s", (long long)origin->seat.index, name);
    char *path = NULL;
    if (qa_find_content_path(options.console_root, relative, QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) {
      result = read_file_text(path);
      free(path);
      if (result) { free(name); return result; }
    }
  }
  bool is_config = true;
  const char *want = "config.cfg";
  if (strlen(name) != strlen(want)) is_config = false;
  else for (size_t i = 0; want[i]; i++) if (tolower((unsigned char)name[i]) != want[i]) is_config = false;
  if (!result && options.has_legacy_config && is_config) {
    char *legacy = legacy_config_path(options.legacy_config);
    if (legacy) { result = read_file_text(legacy); free(legacy); if (result) { free(name); return result; } }
  }
  if (!result) {
    char *path = NULL;
    if (qa_find_content_path(options.settings->root, name, QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) {
      result = read_file_text(path);
      free(path);
      if (result) { free(name); return result; }
    }
  }
  qa_app_source_script_reader reader = options.has_mounted_script ? options.mounted_script : options.mounted;
  bool has_reader = options.has_mounted_script || options.has_mounted;
  if (has_reader) {
    uint8_t *bytes = NULL; size_t count = 0;
    if (reader.open(reader.ctx, name, &bytes, &count)) {
      char *text = malloc(count + 1);
      memcpy(text, bytes, count);
      text[count] = '\0';
      free(bytes);
      free(name);
      return text;
    }
  }
  free(name);
  return NULL;
}
struct qa_app_console_script_files {
  qa_app_console_script_files_options options;
  long reads;
  bool retiring, closed;
};
qa_app_console_script_files *qa_app_console_script_files_create(qa_app_console_script_files_options options) {
  qa_app_console_script_files *files = calloc(1, sizeof *files);
  if (!files) return NULL;
  files->options = options;
  return files;
}
void qa_app_console_script_files_destroy(qa_app_console_script_files *files) { free(files); }
const char *qa_app_console_script_files_root(const qa_app_console_script_files *files) { return files->options.settings->root; }
static void acquire(qa_app_console_script_files *files) {
  if (files->retiring) qa_range_error("Configuration reader is retired");
  files->reads++;
}
static void release_read(qa_app_console_script_files *files) { files->reads--; }
static int by_entry_name(const void *a, const void *b) {
  return strcmp(((const qa_app_console_script_entry *)a)->name, ((const qa_app_console_script_entry *)b)->name);
}
qa_app_console_script_entry *qa_app_console_script_files_list(qa_app_console_script_files *files, qa_command_context source, size_t *out_count) {
  acquire(files);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    release_read(files);
    qa_range_error("Remote clients cannot list local configuration scripts");
  }
  qa_app_console_script_entry *entries = NULL;
  size_t count = 0;
  char **names = NULL; size_t name_count = 0, name_cap = 0;
  config_names(files->options.settings->root, "", &names, &name_count, &name_cap);
  for (size_t i = 0; i < name_count; i++) {
    entries = realloc(entries, (count + 1) * sizeof *entries);
    entries[count++] = (qa_app_console_script_entry){names[i], "product"};
  }
  free(names);
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    qa_config_store seat_store;
    qa_app_seat_console_config(files->options.console_root, origin->seat, &seat_store);
    names = NULL; name_count = 0; name_cap = 0;
    config_names(seat_store.root, "", &names, &name_count, &name_cap);
    for (size_t i = 0; i < name_count; i++) {
      bool replaced = false;
      for (size_t j = 0; j < count; j++) {
        bool same = strlen(entries[j].name) == strlen(names[i]);
        if (same) for (size_t k = 0; names[i][k]; k++) if (tolower((unsigned char)entries[j].name[k]) != tolower((unsigned char)names[i][k])) same = false;
        if (same) { free(entries[j].name); entries[j] = (qa_app_console_script_entry){names[i], "seat"}; replaced = true; break; }
      }
      if (!replaced) { entries = realloc(entries, (count + 1) * sizeof *entries); entries[count++] = (qa_app_console_script_entry){names[i], "seat"}; }
    }
    free(names);
    qa_config_store_destroy(&seat_store);
  }
  qsort(entries, count, sizeof *entries, by_entry_name);
  release_read(files);
  *out_count = count;
  return entries;
}
void qa_app_console_script_entries_free(qa_app_console_script_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) free(entries[i].name);
  free(entries);
}
char *qa_app_console_script_files_read(qa_app_console_script_files *files, const char *name, qa_command_context source) {
  acquire(files);
  qa_app_read_console_script_options options = {0};
  options.name = name;
  options.source = source;
  options.console_root = files->options.console_root;
  options.settings = files->options.settings;
  options.mounted = files->options.mounted; options.has_mounted = files->options.has_mounted;
  options.mounted_script = files->options.mounted_script; options.has_mounted_script = files->options.has_mounted_script;
  options.has_legacy_config = files->options.has_legacy_config; options.legacy_config = files->options.legacy_config;
  char *text = qa_app_read_console_script(options);
  release_read(files);
  return text;
}
bool qa_app_console_script_files_read_mounted_script(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  qa_app_source_script_reader reader = files->options.has_mounted_script ? files->options.mounted_script : files->options.mounted;
  bool has = files->options.has_mounted_script || files->options.has_mounted;
  bool found = has && reader.open(reader.ctx, name, out_bytes, out_count);
  release_read(files);
  return found;
}
bool qa_app_console_script_files_read_mounted(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  bool found = files->options.has_mounted && files->options.mounted.open(files->options.mounted.ctx, name, out_bytes, out_count);
  release_read(files);
  return found;
}
char **qa_app_console_script_files_list_mounted(qa_app_console_script_files *files, const char *directory, const char *extension, qa_command_context source, size_t *out_count) {
  acquire(files);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    release_read(files);
    qa_range_error("Remote clients cannot list local mounted files");
  }
  *out_count = 0;
  char **names = files->options.mounted_files ? files->options.mounted_files(files->options.mounted_ctx, directory, extension, out_count) : NULL;
  release_read(files);
  return names;
}
bool qa_app_console_script_files_open_mounted(qa_app_console_script_files *files, const char *path, qa_command_context source, uint8_t **out_bytes, size_t *out_count) {
  acquire(files);
  const qa_command_origin *origin = caller_origin(&source.origin);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    release_read(files);
    qa_range_error("Remote clients cannot open local mounted files");
  }
  bool found = files->options.mounted_resource && files->options.mounted_resource(files->options.mounted_ctx, path, out_bytes, out_count);
  release_read(files);
  return found;
}
void qa_app_console_script_files_close(qa_app_console_script_files *files) {
  if (files->closed) return;
  files->closed = true;
  files->retiring = true;
  /* Synchronous port: no reads can be in flight across calls. */
  if (files->options.retire_mounted) files->options.retire_mounted(files->options.mounted.ctx);
}
const char *qa_app_console_script_files_write(qa_app_console_script_files *files, const char *(*operation)(void *ctx), void *ctx) {
  if (files->retiring) return "Configuration reader is retired";
  return operation(ctx);
}
