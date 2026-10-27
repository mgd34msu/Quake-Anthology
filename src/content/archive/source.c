/* Port of src/content/archive/source.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "qa/content/archive/source.h"

#include <errno.h>
#include <fcntl.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/archive/types.h"
#include "qa/foundation/support.h"

/* TS fs failures surface as plain Node Errors; the C port aborts with
 * the strerror text under the Error class name. */
static noreturn void fs_error(const char *source) {
  fprintf(stderr, "Error: %s: %s\n", source, strerror(errno));
  abort();
}

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static int64_t timespec_ns(struct timespec value) {
  return (int64_t)value.tv_sec * 1000000000LL + (int64_t)value.tv_nsec;
}

bool qa_memory_source_init(qa_archive_source *out, const uint8_t *bytes,
                           size_t length, const char *source) {
  memset(out, 0, sizeof *out);
  out->kind = QA_ARCHIVE_SOURCE_MEMORY;
  out->descriptor = -1;
  out->source = copy_text(source != NULL ? source : "<memory>");
  if (out->source == NULL) return false;
  out->bytes = (uint8_t *)malloc(length > 0 ? length : 1);
  if (out->bytes == NULL) { free(out->source); out->source = NULL; return false; }
  if (length > 0) memcpy(out->bytes, bytes, length);
  out->byte_length = (int64_t)length;
  return true;
}

bool qa_file_source_init(qa_archive_source *out, const char *source) {
  memset(out, 0, sizeof *out);
  out->kind = QA_ARCHIVE_SOURCE_FILE;
  out->descriptor = -1;
  out->source = copy_text(source);
  if (out->source == NULL) return false;
  int descriptor = open(source, O_RDONLY);
  if (descriptor < 0) fs_error(source);
  struct stat info;
  if (fstat(descriptor, &info) != 0) {
    int saved = errno;
    (void)close(descriptor);
    errno = saved;
    fs_error(source);
  }
  int64_t byte_length = (int64_t)info.st_size;
  if (!S_ISREG(info.st_mode) || byte_length < 0 ||
      byte_length > QA_MAX_SAFE_INTEGER) {
    (void)close(descriptor);
    qa_archive_error(source, 0,
                     "expected a regular file with a safe byte length");
  }
  out->byte_length = byte_length;
  out->modified_ns = timespec_ns(info.st_mtim);
  out->changed_ns = timespec_ns(info.st_ctim);
  out->descriptor = descriptor;
  return true;
}

static void verify_unchanged(const qa_archive_source *storage, int64_t offset) {
  struct stat info;
  if (fstat(storage->descriptor, &info) != 0) fs_error(storage->source);
  if ((int64_t)info.st_size != storage->byte_length ||
      timespec_ns(info.st_mtim) != storage->modified_ns ||
      timespec_ns(info.st_ctim) != storage->changed_ns) {
    qa_archive_error(storage->source, offset,
                     "source changed after it was opened");
  }
}

uint8_t *qa_archive_source_read_sync(qa_archive_source *storage,
                                     int64_t offset, int64_t length,
                                     size_t *out_length) {
  if (storage->closed)
    qa_archive_error(storage->source, offset, "archive is closed");
  qa_archive_check_range(storage->source, storage->byte_length, offset, length);
  if (out_length != NULL) *out_length = (size_t)length;
  if (storage->kind == QA_ARCHIVE_SOURCE_MEMORY) {
    if (length == 0) return NULL;
    uint8_t *out = (uint8_t *)malloc((size_t)length);
    if (out == NULL) return NULL;
    memcpy(out, storage->bytes + offset, (size_t)length);
    return out;
  }
  verify_unchanged(storage, offset);
  storage->active_reads++;
  uint8_t *bytes = (uint8_t *)malloc(length > 0 ? (size_t)length : 1);
  if (bytes == NULL) { storage->active_reads--; return NULL; }
  int64_t total = 0;
  while (total < length) {
    ssize_t count = pread(storage->descriptor, bytes + total,
                          (size_t)(length - total),
                          (off_t)(offset + total));
    if (count < 0) {
      if (errno == EINTR) continue;
      storage->active_reads--;
      free(bytes);
      fs_error(storage->source);
    }
    if (count == 0) {
      storage->active_reads--;
      free(bytes);
      qa_archive_error(storage->source, offset,
                       "short read: expected %lld, got %lld",
                       (long long)length, (long long)total);
    }
    total += count;
  }
  storage->active_reads--;
  /* The TS closes the descriptor here when close() raced the read;
   * synchronous reads cannot race, but keep the same shape. */
  if (storage->closed && storage->active_reads == 0 &&
      storage->descriptor >= 0) {
    (void)close(storage->descriptor);
    storage->descriptor = -1;
  }
  verify_unchanged(storage, offset);
  if (length == 0) { free(bytes); return NULL; }
  return bytes;
}

uint8_t *qa_archive_source_read(qa_archive_source *storage, int64_t offset,
                                int64_t length, size_t *out_length) {
  return qa_archive_source_read_sync(storage, offset, length, out_length);
}

void qa_archive_source_close(qa_archive_source *storage) {
  if (storage->closed) return;
  storage->closed = true;
  if (storage->kind == QA_ARCHIVE_SOURCE_FILE && storage->active_reads == 0 &&
      storage->descriptor >= 0) {
    (void)close(storage->descriptor);
    storage->descriptor = -1;
  }
}

void qa_archive_source_destroy(qa_archive_source *storage) {
  qa_archive_source_close(storage);
  if (storage->descriptor >= 0) {
    (void)close(storage->descriptor);
    storage->descriptor = -1;
  }
  free(storage->bytes);
  storage->bytes = NULL;
  free(storage->source);
  storage->source = NULL;
  storage->byte_length = 0;
}
