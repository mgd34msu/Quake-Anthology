/* Port of src/content/archive/types.ts. GPL-2.0-or-later. */
#include "qa/content/archive/types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

noreturn void qa_archive_error(const char *source, int64_t offset,
                               const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "ArchiveError: %s:%lld: ", source ? source : "",
          (long long)offset);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

void qa_archive_check_range(const char *source, int64_t size, int64_t offset,
                            int64_t length) {
  /* int64_t inputs are already integers; the TS safe-integer bounds
   * still apply where observable. */
  if (offset < -QA_MAX_SAFE_INTEGER || offset > QA_MAX_SAFE_INTEGER ||
      length < -QA_MAX_SAFE_INTEGER || length > QA_MAX_SAFE_INTEGER ||
      offset < 0 || length < 0 || offset > size - length) {
    qa_archive_error(source, offset,
                     "range of %lld bytes exceeds %lld-byte source",
                     (long long)length, (long long)size);
  }
}

/* JSON.stringify of a byte string, for the RangeError message. */
static char *json_stringify(const char *value) {
  size_t length = strlen(value);
  char *out = (char *)malloc(length * 6 + 3);
  if (out == NULL) return NULL;
  size_t at = 0;
  out[at++] = '"';
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)value[i];
    switch (c) {
      case '"': out[at++] = '\\'; out[at++] = '"'; break;
      case '\\': out[at++] = '\\'; out[at++] = '\\'; break;
      case '\b': out[at++] = '\\'; out[at++] = 'b'; break;
      case '\f': out[at++] = '\\'; out[at++] = 'f'; break;
      case '\n': out[at++] = '\\'; out[at++] = 'n'; break;
      case '\r': out[at++] = '\\'; out[at++] = 'r'; break;
      case '\t': out[at++] = '\\'; out[at++] = 't'; break;
      default:
        if (c < 0x20) {
          at += (size_t)snprintf(out + at, 7, "\\u%04x", c);
        } else {
          out[at++] = (char)c;
        }
        break;
    }
  }
  out[at++] = '"';
  out[at] = '\0';
  return out;
}

static noreturn void unsafe_path(const char *path) {
  char *json = json_stringify(path != NULL ? path : "");
  qa_range_error("Unsafe archive member path: %s", json != NULL ? json : "\"\"");
}

char *qa_normalize_entry_path(const char *path) {
  if (path == NULL) path = "";
  size_t length = strlen(path);
  char *separated = (char *)malloc(length + 1);
  if (separated == NULL) return NULL;
  for (size_t i = 0; i < length; i++)
    separated[i] = path[i] == '\\' ? '/' : path[i];
  separated[length] = '\0';
  bool trailing = length > 0 && separated[length - 1] == '/';
  size_t candidate_length = trailing ? length - 1 : length;

  bool bad = candidate_length == 0;
  for (size_t i = 0; i < candidate_length && !bad; i++)
    if (separated[i] == ':') bad = true;
  /* A NUL byte cannot occur in a C string; the TS includes("\0")
   * clause is unreachable here (byte-string inputs lose it earlier). */

  /* Split candidate into parts, validating ""/"." and folding "..". */
  size_t part_capacity = candidate_length / 2 + 2;
  char **parts = (char **)malloc(part_capacity * sizeof *parts);
  size_t *part_lengths = (size_t *)malloc(part_capacity * sizeof *part_lengths);
  if (parts == NULL || part_lengths == NULL) {
    free(parts); free(part_lengths); free(separated);
    return NULL;
  }
  size_t part_count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= candidate_length && !bad; i++) {
    if (i != candidate_length && separated[i] != '/') continue;
    size_t part_length = i - start;
    const char *part = separated + start;
    if (part_length == 0 || (part_length == 1 && part[0] == '.')) {
      bad = true;
      break;
    }
    if (part_length == 2 && part[0] == '.' && part[1] == '.') {
      if (part_count == 0) { bad = true; break; }
      part_count--;
    } else {
      parts[part_count] = separated + start;
      part_lengths[part_count] = part_length;
      part_count++;
    }
    start = i + 1;
  }
  if (!bad && part_count == 0) bad = true;
  if (bad) {
    free(parts); free(part_lengths); free(separated);
    unsafe_path(path);
  }
  size_t out_length = trailing ? 1 : 0;
  for (size_t i = 0; i < part_count; i++)
    out_length += part_lengths[i] + (i > 0 ? 1 : 0);
  char *out = (char *)malloc(out_length + 1);
  if (out == NULL) {
    free(parts); free(part_lengths); free(separated);
    return NULL;
  }
  size_t at = 0;
  for (size_t i = 0; i < part_count; i++) {
    if (i > 0) out[at++] = '/';
    memcpy(out + at, parts[i], part_lengths[i]);
    at += part_lengths[i];
  }
  if (trailing) out[at++] = '/';
  out[at] = '\0';
  free(parts); free(part_lengths); free(separated);
  return out;
}

char *qa_compare_entry_path(const char *path,
                            qa_archive_path_comparison comparison) {
  char *normalized = qa_normalize_entry_path(path);
  if (normalized == NULL) return NULL;
  if (comparison == QA_ARCHIVE_PATH_EXACT) return normalized;
  for (size_t i = 0; normalized[i] != '\0'; i++) {
    unsigned char c = (unsigned char)normalized[i];
    if (c >= 'A' && c <= 'Z') {
      normalized[i] = (char)(c - 'A' + 'a');
    } else if (comparison == QA_ARCHIVE_PATH_CASE_INSENSITIVE &&
               c >= 0xc0 && c <= 0xde && c != 0xd7) {
      normalized[i] = (char)(c + 0x20); /* Latin-1 toLowerCase */
    }
  }
  return normalized;
}
