/* Port of src/media/source.ts. GPL-2.0-or-later. */
#include "qa/media/source.h"

#include <errno.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

/* TS plain `throw new Error(msg)` (and node:fs system errors). */
static noreturn void media_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static void range_check(int64_t offset) {
  if (offset < 0) {
    qa_range_error("Media offset must be a nonnegative integer");
  }
}

qa_media_input *qa_media_input_create(void *ctx, const char *source,
                                      int64_t byte_length,
                                      int64_t (*read_at)(void *, int64_t,
                                                         uint8_t *, size_t),
                                      void (*close_ctx)(void *)) {
  qa_media_input *input = (qa_media_input *)calloc(1, sizeof *input);
  if (input == NULL) return NULL;
  input->source = dup_text(source == NULL ? "<media>" : source);
  if (input->source == NULL) {
    free(input);
    return NULL;
  }
  input->ctx = ctx;
  input->byte_length = byte_length;
  input->read_at = read_at;
  input->close_ctx = close_ctx;
  return input;
}

typedef struct bytes_ctx {
  const uint8_t *bytes; /* borrowed */
  size_t length;
} bytes_ctx;

static int64_t bytes_read_at(void *raw, int64_t offset, uint8_t *destination,
                             size_t length) {
  bytes_ctx *ctx = (bytes_ctx *)raw;
  if (offset >= (int64_t)ctx->length) return 0;
  size_t available = ctx->length - (size_t)offset;
  size_t count = length < available ? length : available;
  memcpy(destination, ctx->bytes + (size_t)offset, count);
  return (int64_t)count;
}

qa_media_input *qa_media_bytes(const uint8_t *bytes, size_t length,
                               const char *source) {
  bytes_ctx *ctx = (bytes_ctx *)malloc(sizeof *ctx);
  if (ctx == NULL) return NULL;
  ctx->bytes = bytes;
  ctx->length = length;
  qa_media_input *input = qa_media_input_create(
      ctx, source, (int64_t)length, bytes_read_at, free);
  if (input == NULL) free(ctx);
  return input;
}

typedef struct file_ctx {
  FILE *file;
} file_ctx;

static int64_t file_read_at(void *raw, int64_t offset, uint8_t *destination,
                            size_t length) {
  file_ctx *ctx = (file_ctx *)raw;
  if (length == 0) return 0;
  if (fseek(ctx->file, (long)offset, SEEK_SET) != 0) {
    media_error("%s", strerror(errno));
  }
  size_t count = fread(destination, 1, length, ctx->file);
  if (count < length && ferror(ctx->file)) {
    media_error("%s", strerror(errno));
  }
  return (int64_t)count;
}

static void file_close_ctx(void *raw) {
  file_ctx *ctx = (file_ctx *)raw;
  fclose(ctx->file);
  free(ctx);
}

qa_media_input *qa_open_media_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) media_error("%s", strerror(errno));
  if (fseek(file, 0, SEEK_END) != 0) {
    int error = errno;
    fclose(file);
    media_error("%s", strerror(error));
  }
  long size = ftell(file);
  if (size < 0) {
    int error = errno;
    fclose(file);
    media_error("%s", strerror(error));
  }
  file_ctx *ctx = (file_ctx *)malloc(sizeof *ctx);
  if (ctx == NULL) {
    fclose(file);
    return NULL;
  }
  ctx->file = file;
  qa_media_input *input =
      qa_media_input_create(ctx, path, (int64_t)size, file_read_at,
                            file_close_ctx);
  if (input == NULL) file_close_ctx(ctx);
  return input;
}

int64_t qa_media_input_read_at(qa_media_input *input, int64_t offset,
                               uint8_t *destination, size_t length) {
  if (input->closed) media_error("Media input is closed");
  range_check(offset);
  return input->read_at(input->ctx, offset, destination, length);
}

void qa_media_input_close(qa_media_input *input) {
  if (input == NULL) return;
  if (!input->closed) {
    input->closed = true;
    if (input->close_ctx != NULL) input->close_ctx(input->ctx);
  }
  free(input->source);
  free(input);
}

uint8_t *qa_read_media(qa_media_input *input, int64_t offset, int64_t length) {
  range_check(offset);
  if (length < 0 || offset > input->byte_length - length) {
    qa_binary_error(input->source, offset,
                    "truncated media read of %lld bytes",
                    (long long)length);
  }
  if (length == 0) return NULL;
  uint8_t *bytes = (uint8_t *)malloc((size_t)length);
  if (bytes == NULL) return NULL;
  int64_t count = 0;
  while (count < length) {
    int64_t read = qa_media_input_read_at(
        input, offset + count, bytes + (size_t)count,
        (size_t)(length - count));
    if (read <= 0 || read > length - count) {
      free(bytes);
      qa_binary_error(input->source, offset + count, "short media read");
    }
    count += read;
  }
  return bytes;
}
