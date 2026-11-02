/* Shared helpers for the content/catalog port (no TS counterpart).
 * normalize/contained delegate to their TS owners' ports
 * (src/content/mounts/paths.ts, src/platform/files/contained.ts)
 * under collision-proof catalog-private names. GPL-2.0-or-later. */
#ifndef QA_CONTENT_CATALOG_INTERNAL_H
#define QA_CONTENT_CATALOG_INTERNAL_H

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdnoreturn.h>

#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"

/* TS `throw new Error(msg)`: prints "Error: msg" and aborts. */
static inline noreturn void qa_catalog_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static inline char *qa_catalog_dup(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *out = (char *)malloc(length);
  if (out != NULL) memcpy(out, text, length);
  return out;
}

/* Malloc'd printf result; NULL only on OOM. */
static inline char *qa_catalog_format(const char *format, ...) {
  va_list args, copy;
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) { va_end(args); return NULL; }
  char *out = (char *)malloc((size_t)length + 1);
  if (out != NULL) (void)vsnprintf(out, (size_t)length + 1, format, args);
  va_end(args);
  return out;
}

static inline bool qa_catalog_starts_with(const char *text, const char *prefix) {
  return text != NULL && prefix != NULL &&
         strncmp(text, prefix, strlen(prefix)) == 0;
}

static inline bool qa_catalog_ends_with(const char *text, const char *suffix) {
  if (text == NULL || suffix == NULL) return false;
  size_t a = strlen(text), b = strlen(suffix);
  return a >= b && strcmp(text + (a - b), suffix) == 0;
}

static inline char *qa_catalog_lower_dup(const char *text) {
  char *out = qa_catalog_dup(text != NULL ? text : "");
  if (out != NULL)
    for (size_t i = 0; out[i] != '\0'; i++)
      out[i] = (char)tolower((unsigned char)out[i]);
  return out;
}

static inline bool qa_catalog_ieq(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  while (*left != '\0' && *right != '\0') {
    if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
      return false;
    left++; right++;
  }
  return *left == *right;
}

/* TS sourceCompare in catalog/index.ts. */
static inline int qa_catalog_source_compare(const char *left, const char *right) {
  int result = strcmp(left != NULL ? left : "", right != NULL ? right : "");
  return result < 0 ? -1 : result > 0 ? 1 : 0;
}

/* Malloc'd "a/b" join (no duplicate slash); NULL only on OOM. */
static inline char *qa_catalog_join(const char *left, const char *right) {
  if (left == NULL || left[0] == '\0') return qa_catalog_dup(right != NULL ? right : "");
  if (right == NULL || right[0] == '\0') return qa_catalog_dup(left);
  bool slash_left = left[strlen(left) - 1] == '/';
  bool slash_right = right[0] == '/';
  if (slash_left && slash_right) return qa_catalog_format("%s%s", left, right + 1);
  if (slash_left || slash_right) return qa_catalog_format("%s%s", left, right);
  return qa_catalog_format("%s/%s", left, right);
}

static inline const char *qa_catalog_basename(const char *path) {
  const char *slash = path != NULL ? strrchr(path, '/') : NULL;
  return slash != NULL ? slash + 1 : (path != NULL ? path : "");
}

/* Malloc'd dirname (Node path.posix-like for catalog paths). */
static inline char *qa_catalog_dirname(const char *path) {
  if (path == NULL || path[0] == '\0') return qa_catalog_dup(".");
  const char *slash = strrchr(path, '/');
  if (slash == NULL) return qa_catalog_dup(".");
  if (slash == path) return qa_catalog_dup("/");
  size_t length = (size_t)(slash - path);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) { memcpy(out, path, length); out[length] = '\0'; }
  return out;
}

/* normalizeResourcePath (TS owner: src/content/mounts/paths.ts).
 * Malloc'd; NULL only on OOM. Invalid paths abort with the exact
 * TS RangeError. */
static inline char *qa_catalog_normalize_resource_path(const char *path) {
  return qa_normalize_resource_path(path);
}

/* containedFileParts validation (TS owner:
 * src/platform/files/contained.ts): callers here only need the
 * validation, so the parts are freed immediately. Aborts exactly
 * as the owner does on unsafe names. */
static inline void qa_catalog_contained_check(const char *name) {
  if (name == NULL)
    qa_range_error("File path must be relative and stay inside its storage directory");
  size_t count = 0;
  char **parts = qa_contained_file_parts(name, &count);
  qa_contained_file_parts_free(parts, count);
}

/* Growable string list. */
typedef struct qa_catalog_string_list {
  char **values;
  size_t count;
  size_t capacity;
} qa_catalog_string_list;

static inline bool qa_catalog_string_push(qa_catalog_string_list *list, char *owned) {
  if (list->count == list->capacity) {
    size_t capacity = list->capacity != 0 ? list->capacity * 2 : 8;
    char **values = (char **)realloc(list->values, capacity * sizeof *values);
    if (values == NULL) return false;
    list->values = values;
    list->capacity = capacity;
  }
  list->values[list->count++] = owned;
  return true;
}

static inline void qa_catalog_string_list_free(qa_catalog_string_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  list->values = NULL; list->count = 0; list->capacity = 0;
}

static inline bool qa_catalog_string_contains(const char *const *values,
                                              size_t count, const char *text) {
  for (size_t i = 0; i < count; i++)
    if (values[i] != NULL && text != NULL && strcmp(values[i], text) == 0) return true;
  return false;
}

#endif
