/* Port of src/platform/files/writable.ts. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "qa/platform/files/writable.h"

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
#include "qa/platform/files/contained.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

void qa_writable_file_checkpoint_dispose(qa_writable_file_checkpoint *checkpoint) {
  free(checkpoint->path);
  checkpoint->path = NULL;
}

static void live(const qa_writable_binary_file *file) {
  if (file->ended) qa_error("Writable file is closed");
}

static int64_t file_size(int descriptor) {
  struct stat info;
  if (fstat(descriptor, &info) != 0) qa_error("%s", strerror(errno));
  return (int64_t)info.st_size;
}

bool qa_writable_binary_file_capture_checkpoint(qa_writable_binary_file *file,
                                                qa_writable_file_checkpoint *out) {
  live(file);
  out->path = dup_text(file->path);
  if (out->path == NULL) return false;
  out->mode = file->mode;
  out->mode_valid = true;
  out->position = file->position;
  return true;
}

int64_t qa_writable_binary_file_write(qa_writable_binary_file *file,
                                      const uint8_t *bytes, size_t length) {
  live(file);
  size_t offset = 0;
  bool retried = false;
  while (offset < length) {
    ssize_t written;
    if (file->mode == QA_WRITABLE_WRITE) {
      written = pwrite(file->descriptor, bytes + offset, length - offset,
                       (off_t)file->position);
    } else {
      written = write(file->descriptor, bytes + offset, length - offset);
    }
    /* Every C-level failure is a platform failure (errno), which the
     * TypeScript counts as zero bytes written. */
    if (written < 0) written = 0;
    if (written == 0) {
      if (retried) {
        if (file->print != NULL) file->print("FS_Write: 0 bytes written\n", file->print_ctx);
        live(file);
        return 0;
      }
      retried = true;
      continue;
    }
    offset += (size_t)written;
    file->position = file->mode == QA_WRITABLE_WRITE
                         ? file->position + (int64_t)written
                         : file_size(file->descriptor);
  }
  return (int64_t)length;
}

int qa_writable_binary_file_seek(qa_writable_binary_file *file, int64_t offset,
                                 qa_writable_seek_origin origin) {
  live(file);
  struct stat info;
  if (fstat(file->descriptor, &info) != 0) return -1;
  int64_t base = origin == QA_WRITABLE_SEEK_CURRENT
                     ? file->position
                     : origin == QA_WRITABLE_SEEK_END ? (int64_t)info.st_size : 0;
  if ((offset > 0 && base > QA_MAX_SAFE_INTEGER - offset) ||
      (offset < 0 && base < -QA_MAX_SAFE_INTEGER - offset))
    return -1;
  int64_t position = base + offset;
  if (position < 0 || position > QA_MAX_SAFE_INTEGER) return -1;
  file->position = position;
  return 0;
}

void qa_writable_binary_file_close(qa_writable_binary_file *file) {
  if (!file->ended) {
    file->ended = true;
    close(file->descriptor);
  }
  free(file->path);
  file->path = NULL;
}

/* mkdir -p with the 0700 leaf mode the TypeScript requests. */
static void make_directories(const char *root) {
  char *path = dup_text(root);
  if (path == NULL) qa_range_error("out of memory");
  for (char *p = path + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        int code = errno;
        free(path);
        qa_error("%s", strerror(code));
      }
      *p = '/';
    }
  }
  if (mkdir(path, 0700) != 0 && errno != EEXIST) {
    int code = errno;
    free(path);
    qa_error("%s", strerror(code));
  }
  free(path);
}

bool qa_user_file_store_init(qa_user_file_store *store, const char *root) {
  store->root = dup_text(root);
  return store->root != NULL;
}

void qa_user_file_store_destroy(qa_user_file_store *store) {
  free(store->root);
  store->root = NULL;
}

static bool finish_open(qa_writable_binary_file *out, int descriptor,
                        qa_writable_file_mode mode, qa_writable_print_fn print,
                        void *print_ctx, const char *name, bool resume,
                        int64_t resume_position) {
  out->path = dup_text(name);
  if (out->path == NULL) {
    close(descriptor);
    return false;
  }
  out->descriptor = descriptor;
  out->mode = mode;
  out->print = print;
  out->print_ctx = print_ctx;
  out->position = resume ? resume_position
                          : mode == QA_WRITABLE_WRITE ? 0 : file_size(descriptor);
  out->ended = false;
  return true;
}

bool qa_user_file_store_resume(qa_user_file_store *store,
                               const qa_writable_file_checkpoint *state,
                               qa_writable_print_fn print, void *print_ctx,
                               qa_writable_binary_file *out) {
  size_t count = 0;
  char **parts = qa_contained_file_parts(state->path, &count);
  if (parts == NULL) return false;
  qa_contained_file_parts_free(parts, count);
  if (state->position < 0 || state->position > QA_MAX_SAFE_INTEGER)
    qa_range_error("Invalid writable checkpoint cursor");
  if (!state->mode_valid) qa_range_error("Invalid writable checkpoint mode");
  bool append = state->mode != QA_WRITABLE_WRITE;
  if (append) make_directories(store->root);
  char *leaf = NULL;
  int parent = qa_open_contained_parent(store->root, state->path, append, &leaf);
  if (parent < 0) return false;
  int flags = O_WRONLY | O_NOFOLLOW | O_NONBLOCK | (append ? O_APPEND | O_CREAT : 0);
  int descriptor = openat(parent, leaf, flags, 0600);
  int code = errno;
  close(parent);
  free(leaf);
  if (descriptor < 0) qa_error("%s", strerror(code));
  struct stat info;
  if (fstat(descriptor, &info) != 0) {
    code = errno;
    close(descriptor);
    qa_error("%s", strerror(code));
  }
  if (!S_ISREG(info.st_mode)) {
    close(descriptor);
    qa_error("Writable source file is not a regular file");
  }
  return finish_open(out, descriptor, state->mode, print, print_ctx, state->path,
                     true, state->position);
}

bool qa_user_file_store_open(qa_user_file_store *store, const char *name,
                             qa_writable_file_mode mode,
                             qa_writable_print_fn print, void *print_ctx,
                             qa_writable_binary_file *out) {
  make_directories(store->root);
  char *leaf = NULL;
  int parent = qa_open_contained_parent(store->root, name, true, &leaf);
  if (parent < 0) return false;
  int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK |
              (mode == QA_WRITABLE_WRITE ? 0 : O_APPEND);
  int descriptor = openat(parent, leaf, flags, 0600);
  close(parent);
  free(leaf);
  if (descriptor < 0) return false; /* platform failure: TypeScript null */
  struct stat info;
  if (fstat(descriptor, &info) != 0) {
    int code = errno;
    close(descriptor);
    qa_error("%s", strerror(code));
  }
  if (!S_ISREG(info.st_mode)) {
    close(descriptor);
    qa_error("Writable source file is not a regular file");
  }
  if (mode == QA_WRITABLE_WRITE && ftruncate(descriptor, 0) != 0) {
    int code = errno;
    close(descriptor);
    qa_error("%s", strerror(code));
  }
  return finish_open(out, descriptor, mode, print, print_ctx, name, false, 0);
}
