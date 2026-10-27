/* Port of src/content/mounts/paths.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/mounts/paths.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/support.h"

static noreturn void plain_error(const char *format, const char *argument) {
  fprintf(stderr, "Error: ");
  fprintf(stderr, format, argument);
  fputc('\n', stderr);
  abort();
}
static noreturn void fs_error(const char *path) {
  fprintf(stderr, "Error: %s: %s\n", path, strerror(errno));
  abort();
}

char *qa_normalize_resource_path(const char *path) {
  if (path == NULL) path = "";
  size_t length = strlen(path);
  char *normalized = (char *)malloc(length + 1);
  if (normalized == NULL) return NULL;
  for (size_t i = 0; i < length; i++)
    normalized[i] = path[i] == '\\' ? '/' : path[i];
  normalized[length] = '\0';
  bool bad = length == 0;
  if (!bad && length >= 2 && normalized[1] == ':') {
    char first = normalized[0];
    if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))
      bad = true;
  }
  size_t start = 0;
  for (size_t i = 0; i <= length && !bad; i++) {
    if (i != length && normalized[i] != '/') continue;
    size_t part_length = i - start;
    if (part_length == 0 || (part_length == 1 && normalized[start] == '.') ||
        (part_length == 2 && normalized[start] == '.' &&
         normalized[start + 1] == '.'))
      bad = true;
    start = i + 1;
  }
  if (bad) {
    free(normalized);
    qa_range_error("Invalid relative resource path: %s", path);
  }
  return normalized;
}

/* Lexical absolute normalization (node:path resolve) of `joined`. */
static char *normalize_absolute(const char *joined) {
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
    bool at_end = *cursor == '\0';
    if (!at_end) *cursor++ = '\0';
    if (part_length == 1 && start[0] == '.') continue;
    if (part_length == 2 && start[0] == '.' && start[1] == '.') {
      if (count > 0) count--;
      continue;
    }
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

static char *resolve_path(const char *first, const char *second) {
  char joined[PATH_MAX * 2];
  if (second != NULL && second[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s", second);
  } else if (first[0] == '/') {
    if (second != NULL)
      (void)snprintf(joined, sizeof joined, "%s/%s", first, second);
    else
      (void)snprintf(joined, sizeof joined, "%s", first);
  } else {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) fs_error(".");
    if (second != NULL)
      (void)snprintf(joined, sizeof joined, "%s/%s/%s", cwd, first, second);
    else
      (void)snprintf(joined, sizeof joined, "%s/%s", cwd, first);
  }
  return normalize_absolute(joined);
}

static bool within_root(const char *root, const char *path) {
  size_t root_length = strlen(root);
  if (strcmp(root, "/") == 0) return path[0] == '/';
  return strncmp(path, root, root_length) == 0 &&
         (path[root_length] == '/' || path[root_length] == '\0');
}

char *qa_path_within_root(const char *root, const char *path) {
  char *normalized = qa_normalize_resource_path(path);
  if (normalized == NULL) return NULL;
  char *destination = resolve_path(root, normalized);
  free(normalized);
  if (destination == NULL) return NULL;
  char *resolved_root = resolve_path(root, NULL);
  if (resolved_root == NULL) { free(destination); return NULL; }
  bool inside = within_root(resolved_root, destination);
  free(resolved_root);
  if (!inside) {
    free(destination);
    qa_range_error("Resource escapes content root: %s", path);
  }
  return destination;
}

bool qa_is_missing_file(int error_number) {
  return error_number == ENOENT || error_number == ENOTDIR;
}

static bool ascii_iequal_part(const char *left, const char *right,
                              size_t right_length) {
  if (strlen(left) != right_length) return false;
  for (size_t i = 0; i < right_length; i++) {
    char a = left[i], b = right[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

bool qa_find_content_path(const char *root, const char *path,
                          qa_content_path_comparison comparison,
                          char **out_path) {
  *out_path = NULL;
  char *normalized = qa_normalize_resource_path(path);
  if (normalized == NULL) qa_range_error("out of memory");
  char *current = resolve_path(root, NULL);
  if (current == NULL) { free(normalized); qa_range_error("out of memory"); }
  char *canonical_root = realpath(current, NULL);
  if (canonical_root == NULL) {
    int error_number = errno;
    free(normalized);
    free(current);
    if (qa_is_missing_file(error_number)) return false;
    errno = error_number;
    fs_error(root);
  }
  size_t start = 0, length = strlen(normalized);
  for (size_t i = 0; i <= length; i++) {
    if (i != length && normalized[i] != '/') continue;
    size_t part_length = i - start;
    char part[PATH_MAX];
    if (part_length >= sizeof part) part_length = sizeof part - 1;
    memcpy(part, normalized + start, part_length);
    part[part_length] = '\0';
    char *next = NULL;
    if (comparison == QA_CONTENT_PATH_EXACT) {
      next = resolve_path(current, part);
      if (next == NULL) goto oom;
    } else {
      DIR *directory = opendir(current);
      if (directory == NULL) {
        int error_number = errno;
        if (qa_is_missing_file(error_number)) {
          free(normalized); free(current); free(canonical_root);
          return false;
        }
        errno = error_number;
        fs_error(current);
      }
      char exact[PATH_MAX] = "";
      char match[PATH_MAX] = "";
      size_t matches = 0;
      struct dirent *entry;
      bool have_exact = false;
      while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, part) == 0) {
          (void)snprintf(exact, sizeof exact, "%s", entry->d_name);
          have_exact = true;
        }
        if (ascii_iequal_part(entry->d_name, part, strlen(part))) {
          if (matches == 0)
            (void)snprintf(match, sizeof match, "%s", entry->d_name);
          matches++;
        }
      }
      (void)closedir(directory);
      if (have_exact) {
        next = resolve_path(current, exact);
      } else if (matches > 1) {
        free(normalized); free(current); free(canonical_root);
        plain_error("Ambiguous content path: %s", path);
      } else if (matches == 0) {
        free(normalized); free(current); free(canonical_root);
        return false;
      } else {
        next = resolve_path(current, match);
      }
      if (next == NULL) goto oom;
    }
    free(current);
    current = next;
    start = i + 1;
  }
  free(normalized);
  {
    char *canonical = realpath(current, NULL);
    if (canonical == NULL) {
      int error_number = errno;
      free(current); free(canonical_root);
      if (qa_is_missing_file(error_number)) return false;
      errno = error_number;
      fs_error(path);
    }
    bool inside = within_root(canonical_root, canonical);
    free(canonical);
    if (!inside) {
      free(current); free(canonical_root);
      plain_error("Content symlink escapes root: %s", path);
    }
    struct stat info;
    if (stat(current, &info) != 0) {
      int error_number = errno;
      free(current); free(canonical_root);
      if (qa_is_missing_file(error_number)) return false;
      errno = error_number;
      fs_error(path);
    }
    free(canonical_root);
    *out_path = current;
    return true;
  }
oom:
  free(normalized);
  free(current);
  free(canonical_root);
  qa_range_error("out of memory");
}
