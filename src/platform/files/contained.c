/* Port of src/platform/files/contained.ts. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "qa/platform/files/contained.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

char **qa_contained_file_parts(const char *name, size_t *out_count) {
  size_t count = 1;
  for (const char *p = name; *p != '\0'; p++) {
    if (*p == '/') count++;
  }
  bool invalid = name[0] == '\0' || strchr(name, '\\') != NULL ||
                 strchr(name, ':') != NULL;
  char **parts = (char **)calloc(count, sizeof *parts);
  if (parts == NULL) return NULL;
  const char *start = name;
  for (size_t i = 0; i < count; i++) {
    const char *slash = strchr(start, '/');
    size_t length = slash == NULL ? strlen(start) : (size_t)(slash - start);
    parts[i] = (char *)malloc(length + 1);
    if (parts[i] == NULL) {
      qa_contained_file_parts_free(parts, count);
      return NULL;
    }
    memcpy(parts[i], start, length);
    parts[i][length] = '\0';
    if (length == 0 || strcmp(parts[i], ".") == 0 || strcmp(parts[i], "..") == 0)
      invalid = true;
    start = slash == NULL ? start + length : slash + 1;
  }
  if (invalid) {
    qa_contained_file_parts_free(parts, count);
    qa_range_error("File path must be relative and stay inside its storage directory");
  }
  *out_count = count;
  return parts;
}

void qa_contained_file_parts_free(char **parts, size_t count) {
  if (parts == NULL) return;
  for (size_t i = 0; i < count; i++) free(parts[i]);
  free(parts);
}

/* Node/Bun fs error text: "<CODE>: <libuv description>, <syscall> '<path>'".
   Unlisted errno values fall back to strerror. */
static noreturn void fs_error(int code, const char *syscall, const char *path) {
  static const struct { int code; const char *name, *text; } known[] = {
      {ENOENT, "ENOENT", "no such file or directory"},
      {ENOTDIR, "ENOTDIR", "not a directory"},
      {EACCES, "EACCES", "permission denied"},
      {EPERM, "EPERM", "operation not permitted"},
      {ELOOP, "ELOOP", "too many symbolic links encountered"},
      {EEXIST, "EEXIST", "file already exists"},
      {EMFILE, "EMFILE", "too many open files"},
      {ENAMETOOLONG, "ENAMETOOLONG", "name too long"},
      {EROFS, "EROFS", "read-only file system"},
      {ENOSPC, "ENOSPC", "no space left on device"},
      {EISDIR, "EISDIR", "illegal operation on a directory"},
  };
  for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
    if (known[i].code == code)
      qa_error("%s: %s, %s '%s'", known[i].name, known[i].text, syscall, path);
  qa_error("%s", strerror(code));
}

int qa_open_contained_parent(const char *root, const char *name, bool create,
                             char **out_leaf) {
#ifndef __linux__
  (void)root;
  (void)name;
  (void)create;
  (void)out_leaf;
  qa_error("Contained file storage currently requires Linux directory descriptors");
#else
  size_t count = 0;
  char **parts = qa_contained_file_parts(name, &count);
  if (parts == NULL) return -1;
  if (count == 0) qa_range_error("File path has no filename");
  int descriptor = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (descriptor < 0) {
    int code = errno;
    qa_contained_file_parts_free(parts, count);
    fs_error(code, "open", root);
  }
  for (size_t i = 0; i + 1 < count; i++) {
    /* TS opens `/proc/self/fd/${descriptor}/${part}`; the path names it. */
    char path[4096];
    snprintf(path, sizeof path, "/proc/self/fd/%d/%s", descriptor, parts[i]);
    if (create && mkdirat(descriptor, parts[i], 0777) != 0 && errno != EEXIST) {
      int code = errno;
      close(descriptor);
      qa_contained_file_parts_free(parts, count);
      fs_error(code, "mkdir", path);
    }
    int next = openat(descriptor, parts[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (next < 0) {
      int code = errno;
      close(descriptor);
      qa_contained_file_parts_free(parts, count);
      fs_error(code, "open", path);
    }
    close(descriptor);
    descriptor = next;
  }
  size_t leaf_length = strlen(parts[count - 1]);
  char *leaf = (char *)malloc(leaf_length + 1);
  if (leaf == NULL) {
    close(descriptor);
    qa_contained_file_parts_free(parts, count);
    return -1;
  }
  memcpy(leaf, parts[count - 1], leaf_length + 1);
  qa_contained_file_parts_free(parts, count);
  *out_leaf = leaf;
  return descriptor;
#endif
}
