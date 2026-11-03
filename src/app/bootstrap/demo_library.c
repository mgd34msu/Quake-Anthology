/* Port of src/app/bootstrap/demo-library.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/demo_library.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
struct qa_app_demo_library { char *root; qa_app_demo_library_mounts mounts; };
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_demo_library *qa_app_demo_library_create(const char *root, qa_app_demo_library_mounts mounts) {
  qa_app_demo_library *library = calloc(1, sizeof *library);
  if (!library) return NULL;
  library->root = dup(root);
  library->mounts = mounts;
  return library;
}
void qa_app_demo_library_destroy(qa_app_demo_library *library) {
  if (!library) return;
  free(library->root);
  free(library);
}
const char *qa_app_demo_library_root(const qa_app_demo_library *library) { return library->root; }
typedef struct { char **names; size_t count, cap; } name_set;
static bool demo_extension(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot) return false;
  char ext[16]; size_t len = strlen(dot);
  if (len >= sizeof ext) return false;
  for (size_t i = 0; i <= len; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
  if (strcmp(ext, ".dem") == 0 || strcmp(ext, ".qwd") == 0 || strcmp(ext, ".dm2") == 0 || strcmp(ext, ".mvd") == 0) return true;
  if (strncmp(ext, ".dm_", 4) == 0) {
    for (size_t i = 4; ext[i]; i++) if (!isdigit((unsigned char)ext[i])) return false;
    return ext[4] != '\0';
  }
  return false;
}
static void add_name(name_set *set, const char *path) {
  if (!demo_extension(path)) return;
  char *normalized = qa_normalize_resource_path(path);
  /* TS names.set(normalized.toLowerCase(), normalized): Unicode case-insensitive key. */
  for (size_t i = 0; i < set->count; i++) {
    if (qa_js_equals_ignore_case(set->names[i], normalized)) { free(set->names[i]); set->names[i] = normalized; return; }
  }
  if (set->count == set->cap) { set->cap = set->cap ? set->cap * 2 : 16; set->names = realloc(set->names, set->cap * sizeof *set->names); }
  set->names[set->count++] = normalized;
}
static const char *scan(qa_app_demo_library *library, const char *directory, name_set *set) {
  char full[4096];
  snprintf(full, sizeof full, "%s%s%s", library->root, directory[0] ? "/" : "", directory);
  DIR *dir = opendir(full);
  if (!dir) return qa_is_missing_file(errno) ? NULL : "could not scan demo directory";
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    char path[4096];
    snprintf(path, sizeof path, "%s%s%s", directory, directory[0] ? "/" : "", entry->d_name);
    char disk[8192];
    snprintf(disk, sizeof disk, "%s/%s", library->root, path);
    struct stat st;
    if (stat(disk, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) { const char *error = scan(library, path, set); if (error) { closedir(dir); return error; } }
    else if (S_ISREG(st.st_mode)) add_name(set, path);
  }
  closedir(dir);
  return NULL;
}
static int by_name(const void *a, const void *b) { return qa_js_locale_compare(*(char *const *)a, *(char *const *)b); }
qa_app_demo_library_entry *qa_app_demo_library_list(qa_app_demo_library *library, size_t *out_count, const char **out_error) {
  *out_count = 0;
  *out_error = NULL;
  name_set set = {0};
  static const char *const queries[][2] = {{"", ".dem"}, {"", ".qwd"}, {"demos", ".dm2"}, {"demos", ".mvd"},
      {"demos", ".dm_66"}, {"demos", ".dm_67"}, {"demos", ".dm_68"}};
  for (size_t q = 0; q < 7; q++) {
    size_t count = 0;
    char **names = library->mounts.list_files(library->mounts.ctx, queries[q][0], queries[q][1], &count);
    for (size_t i = 0; i < count; i++) {
      char path[4096];
      snprintf(path, sizeof path, "%s%s%s", queries[q][0], queries[q][0][0] ? "/" : "", names[i]);
      add_name(&set, path);
      free(names[i]);
    }
    free(names);
  }
  const char *error = scan(library, "", &set);
  if (error) {
    for (size_t i = 0; i < set.count; i++) free(set.names[i]);
    free(set.names);
    *out_error = error;
    return NULL;
  }
  qa_js_sort(set.names, set.count, sizeof *set.names, by_name); /* localeCompare ~ byte order for ASCII names */
  qa_app_demo_library_entry *entries = calloc(set.count ? set.count : 1, sizeof *entries);
  for (size_t i = 0; i < set.count; i++) {
    entries[i].id = set.names[i];
    entries[i].label = dup(set.names[i]);
    entries[i].family = qa_app_demo_family_of(set.names[i], QA_APP_DEMO_Q1);
  }
  free(set.names);
  *out_count = set.count;
  return entries;
}
void qa_app_demo_library_entries_free(qa_app_demo_library_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) { free(entries[i].id); free(entries[i].label); }
  free(entries);
}
bool qa_app_demo_library_read(qa_app_demo_library *library, const char *path, uint8_t **out_bytes, size_t *out_count, const char **out_error) {
  *out_error = NULL;
  char *normalized = qa_normalize_resource_path(path);
  char disk[8192];
  snprintf(disk, sizeof disk, "%s/%s", library->root, normalized);
  FILE *file = fopen(disk, "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *bytes = malloc(size > 0 ? (size_t)size : 1);
    size_t read = size > 0 ? fread(bytes, 1, (size_t)size, file) : 0;
    fclose(file);
    free(normalized);
    *out_bytes = bytes;
    *out_count = read;
    return true;
  }
  if (!qa_is_missing_file(errno)) { free(normalized); *out_error = "could not read demo file"; return false; }
  bool found = library->mounts.read(library->mounts.ctx, normalized, out_bytes, out_count);
  free(normalized);
  return found;
}
