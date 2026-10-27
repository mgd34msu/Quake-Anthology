/* Port of src/content/archive/loose.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/archive/loose.h"

#include <errno.h>
#include <limits.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/content/archive/types.h"
#include "qa/foundation/support.h"

static noreturn void fs_error(const char *path) {
  fprintf(stderr, "Error: %s: %s\n", path, strerror(errno));
  abort();
}

/* node path resolve() for the simple joined shapes used here:
 * absolute normalization of "." and ".." segments, lexically. */
static char *resolve_joined(const char *first, const char *second) {
  char joined[PATH_MAX * 2];
  if (second[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s", second);
  } else if (first[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s/%s", first, second);
  } else {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) fs_error(".");
    (void)snprintf(joined, sizeof joined, "%s/%s/%s", cwd, first, second);
  }
  /* Normalize lexically into a fresh buffer. */
  size_t length = strlen(joined);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, joined, length + 1);
  char **parts = (char **)malloc((length / 2 + 2) * sizeof *parts);
  if (parts == NULL) { free(copy); return NULL; }
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    size_t part_length = (size_t)(cursor - start);
    if (part_length == 1 && start[0] == '.') continue;
    if (part_length == 2 && start[0] == '.' && start[1] == '.') {
      if (count > 0) count--;
      continue;
    }
    if (*cursor == '/') *cursor++ = '\0';
    parts[count++] = start;
  }
  char *out = (char *)malloc(length + 2);
  if (out == NULL) { free(parts); free(copy); return NULL; }
  size_t at = 0;
  if (count == 0) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    out[at++] = '/';
    size_t part_length = strlen(parts[i]);
    memcpy(out + at, parts[i], part_length);
    at += part_length;
  }
  out[at] = '\0';
  free(parts);
  free(copy);
  return out;
}

/* node path relative(from, to) for absolute paths. */
static bool within_root(const char *root, const char *path) {
  size_t root_length = strlen(root);
  if (strcmp(root, "/") == 0) return true;
  return strncmp(path, root, root_length) == 0 &&
         (path[root_length] == '/' || path[root_length] == '\0');
}

bool qa_open_loose_entry(const char *root_path, const char *member_path,
                         qa_loose_entry_handle *out) {
  memset(out, 0, sizeof *out);
  char *path = qa_normalize_entry_path(member_path);
  if (path == NULL) return false;
  size_t path_length = strlen(path);
  if (path_length > 0 && path[path_length - 1] == '/') {
    free(path);
    qa_range_error("Expected a loose file path");
  }
  char *root = realpath(root_path, NULL);
  if (root == NULL) { free(path); fs_error(root_path); }
  char *resolved = resolve_joined(root, path);
  if (resolved == NULL) { free(path); free(root); return false; }
  char *source = realpath(resolved, NULL);
  free(resolved);
  if (source == NULL) { free(path); free(root); fs_error(member_path); }
  if (!within_root(root, source)) {
    free(path); free(root); free(source);
    qa_range_error("Loose entry escapes root: %s", member_path);
  }
  free(root);
  qa_archive_source storage;
  if (!qa_file_source_init(&storage, source)) {
    free(path); free(source);
    return false;
  }
  out->path = path;
  out->source = source;
  out->byte_length = storage.byte_length;
  out->storage = storage;
  return true;
}

uint8_t *qa_loose_entry_read(qa_loose_entry_handle *entry, size_t *out_length) {
  return qa_archive_source_read(&entry->storage, 0, entry->storage.byte_length,
                                out_length);
}

void qa_loose_entry_close(qa_loose_entry_handle *entry) {
  qa_archive_source_close(&entry->storage);
}

void qa_loose_entry_destroy(qa_loose_entry_handle *entry) {
  qa_archive_source_destroy(&entry->storage);
  free(entry->path);
  entry->path = NULL;
  free(entry->source);
  entry->source = NULL;
}

uint8_t *qa_read_loose_entry(const char *root_path, const char *member_path,
                             size_t *out_length) {
  qa_loose_entry_handle entry;
  if (!qa_open_loose_entry(root_path, member_path, &entry)) return NULL;
  uint8_t *bytes = qa_loose_entry_read(&entry, out_length);
  qa_loose_entry_destroy(&entry);
  return bytes;
}
