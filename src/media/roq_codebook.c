/* Port of src/media/roq-codebook.ts. GPL-2.0-or-later. */
#include "qa/media/roq_codebook.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define BOOK2_BYTES (256 * 16 * 4 * 2)
#define BOOK4_BYTES (256 * 64 * 4 * 2)
#define BOOK8_BYTES (256 * 256 * 4 * 2)

static int chroma(double coefficient, int value, int bias) {
  double factor = (float)((float)((float)coefficient / 2.0) * 64.0 + 0.5);
  return (int)trunc((float)((double)(float)(factor * value) + bias));
}

static void validate_yuv(int64_t y, int64_t u, int64_t v) {
  if (y < 0 || y > 255 || u < 0 || u > 255 || v < 0 || v > 255) {
    qa_range_error("RoQ YUV values must be bytes");
  }
}

static int clamp_int(int value, int low, int high) {
  return value < low ? low : value > high ? high : value;
}

int64_t qa_roq_yuv_to_rgb565(int64_t y, int64_t u, int64_t v) {
  validate_yuv(y, u, v);
  int yy = ((int)y << 6) | ((int)y >> 2);
  int xu = 2 * (int)u - 255;
  int xv = 2 * (int)v - 255;
  int r = clamp_int((yy + chroma(1.402, xv, 32)) >> 9, 0, 31);
  int g = clamp_int((yy + chroma(0.34414, -xu, 0) +
                     chroma(0.71414, -xv, 32)) >> 8, 0, 63);
  int b = clamp_int((yy + chroma(1.772, xu, 32)) >> 9, 0, 31);
  return (r << 11) | (g << 5) | b;
}

uint32_t qa_roq_yuv_to_rgba(int64_t y, int64_t u, int64_t v) {
  validate_yuv(y, u, v);
  int yy = ((int)y << 6) | ((int)y >> 2);
  int xu = 2 * (int)u - 255;
  int xv = 2 * (int)v - 255;
  uint32_t r = (uint32_t)clamp_int((yy + chroma(1.402, xv, 32)) >> 6, 0, 255);
  uint32_t g = (uint32_t)clamp_int(
      (yy + chroma(0.34414, -xu, 0) + chroma(0.71414, -xv, 32)) >> 6, 0, 255);
  uint32_t b = (uint32_t)clamp_int((yy + chroma(1.772, xu, 32)) >> 6, 0, 255);
  return r | (g << 8) | (b << 16) | 0xff000000u;
}

bool qa_source_roq_codebooks_init(qa_source_roq_codebooks *books) {
  memset(books, 0, sizeof *books);
  books->vq2 = (uint16_t *)calloc(256 * 16 * 4, sizeof(uint16_t));
  books->vq4 = (uint16_t *)calloc(256 * 64 * 4, sizeof(uint16_t));
  books->vq8 = (uint16_t *)calloc(256 * 256 * 4, sizeof(uint16_t));
  if (books->vq2 == NULL || books->vq4 == NULL || books->vq8 == NULL) {
    qa_source_roq_codebooks_destroy(books);
    return false;
  }
  books->book2 = (uint8_t *)books->vq2;
  books->book4 = (uint8_t *)books->vq4;
  books->book8 = (uint8_t *)books->vq8;
  return true;
}

void qa_source_roq_codebooks_destroy(qa_source_roq_codebooks *books) {
  free(books->vq2);
  free(books->vq4);
  free(books->vq8);
  memset(books, 0, sizeof *books);
}

void qa_source_roq_codebooks_clear(qa_source_roq_codebooks *books) {
  memset(books->vq2, 0, BOOK2_BYTES);
  memset(books->vq4, 0, BOOK4_BYTES);
  memset(books->vq8, 0, BOOK8_BYTES);
}

typedef struct write_ctx {
  qa_source_roq_codebooks *books;
  size_t cursor;
  int64_t width;
  const uint8_t *gray; /* NULL when not gray */
} write_ctx;

static void write_pixel(write_ctx *ctx, int y, int u, int v) {
  uint8_t *pixels = ctx->books->book2; /* TS DataView over vq2 only */
  if (ctx->gray != NULL) {
    pixels[ctx->cursor] = ctx->gray[y];
  } else if (ctx->width == 2) {
    uint16_t value = (uint16_t)qa_roq_yuv_to_rgb565(y, u, v);
    pixels[ctx->cursor] = (uint8_t)(value & 0xff);
    pixels[ctx->cursor + 1] = (uint8_t)(value >> 8);
  } else {
    uint32_t value = qa_roq_yuv_to_rgba(y, u, v);
    pixels[ctx->cursor] = (uint8_t)(value & 0xff);
    pixels[ctx->cursor + 1] = (uint8_t)((value >> 8) & 0xff);
    pixels[ctx->cursor + 2] = (uint8_t)((value >> 16) & 0xff);
    pixels[ctx->cursor + 3] = (uint8_t)(value >> 24);
  }
  ctx->cursor += (size_t)ctx->width;
}

void qa_source_roq_codebooks_decode(qa_source_roq_codebooks *books,
                                    qa_binary_reader *reader, int64_t flags,
                                    qa_roq_codebook_mode mode,
                                    qa_roq_codebook_format format,
                                    qa_roq_codebook_profile profile) {
  if (flags < 0 || flags > 65535) {
    qa_binary_error(reader->source, (int64_t)reader->cursor,
                    "invalid RoQ codebook flags");
  }
  if (format.samples_per_pixel == 1 && format.gray_length < 256) {
    qa_binary_error(reader->source, (int64_t)reader->cursor,
                    "RoQ gray lookup requires at least 256 bytes");
  }
  uint32_t uflags = (uint32_t)flags;
  int64_t count2 = (uflags >> 8) == 0 ? 256 : (int64_t)(uflags >> 8);
  int64_t count4 = flags == 0 ? 256 : (int64_t)(uflags & 255);
  int64_t width = format.samples_per_pixel;
  int64_t cell_pixels = mode == QA_ROQ_CODEBOOK_HALF ? 2
                        : mode == QA_ROQ_CODEBOOK_NORMAL ? 4 : 8;
  write_ctx out;
  out.books = books;
  out.cursor = 0;
  out.width = width;
  out.gray = format.samples_per_pixel == 1 ? format.gray : NULL;
  for (int64_t index = 0; index < count2; index++) {
    if (out.gray != NULL && mode != QA_ROQ_CODEBOOK_SMOOTHED_DOUBLE) {
      write_pixel(&out, qa_binary_reader_u8(reader), 0, 0);
      if (mode == QA_ROQ_CODEBOOK_HALF) {
        qa_binary_reader_skip(reader, 1);
        write_pixel(&out, qa_binary_reader_u8(reader), 0, 0);
        qa_binary_reader_skip(reader, 3);
      } else {
        write_pixel(&out, qa_binary_reader_u8(reader), 0, 0);
        write_pixel(&out, qa_binary_reader_u8(reader), 0, 0);
        write_pixel(&out, qa_binary_reader_u8(reader), 0, 0);
        qa_binary_reader_skip(reader, 2);
      }
      continue;
    }
    int y0 = qa_binary_reader_u8(reader);
    if (mode == QA_ROQ_CODEBOOK_HALF) {
      qa_binary_reader_skip(reader, 1);
      int y2 = qa_binary_reader_u8(reader);
      qa_binary_reader_skip(reader, 1);
      int u = qa_binary_reader_u8(reader);
      int v = qa_binary_reader_u8(reader);
      write_pixel(&out, y0, u, v);
      write_pixel(&out, y2, u, v);
      continue;
    }
    int y1 = qa_binary_reader_u8(reader);
    int y2 = qa_binary_reader_u8(reader);
    int y3 = qa_binary_reader_u8(reader);
    int u = 0;
    int v = 0;
    if (out.gray != NULL) {
      qa_binary_reader_skip(reader, 2);
    } else {
      u = qa_binary_reader_u8(reader);
      v = qa_binary_reader_u8(reader);
    }
    write_pixel(&out, y0, u, v);
    write_pixel(&out, y1, u, v);
    if (mode == QA_ROQ_CODEBOOK_SMOOTHED_DOUBLE) {
      write_pixel(&out, (y0 * 3 + y2) / 4, u, v);
      write_pixel(&out, (y1 * 3 + y3) / 4, u, v);
      write_pixel(&out, (y0 + y2 * 3) / 4, u, v);
      write_pixel(&out, (y1 + y3 * 3) / 4, u, v);
    }
    write_pixel(&out, y2, u, v);
    write_pixel(&out, y3, u, v);
  }
  if (profile == QA_ROQ_CODEBOOK_DIAGNOSTIC_2X2 && flags == 0 &&
      qa_binary_reader_remaining(reader) == 0) {
    count4 = 0;
  }
  int64_t row_pixels = mode == QA_ROQ_CODEBOOK_HALF ? 1 : 2;
  int64_t rows = cell_pixels / row_pixels;
  size_t destination4 = 0;
  size_t destination8 = 0;
  for (int64_t half = 0; half < count4 * 2; half++) {
    int64_t left = qa_binary_reader_u8(reader) * cell_pixels * width;
    int64_t right = qa_binary_reader_u8(reader) * cell_pixels * width;
    for (int64_t row = 0; row < rows; row++) {
      int64_t starts[2] = {left, right};
      for (int s = 0; s < 2; s++) {
        for (int64_t column = 0; column < row_pixels; column++) {
          size_t source = (size_t)(starts[s] + column * width);
          memcpy(books->book4 + destination4, books->book2 + source,
                 (size_t)width);
          destination4 += (size_t)width;
          memcpy(books->book8 + destination8, books->book2 + source,
                 (size_t)width);
          memcpy(books->book8 + destination8 + (size_t)width,
                 books->book2 + source, (size_t)width);
          destination8 += (size_t)(width * 2);
        }
      }
      size_t row_bytes = (size_t)(row_pixels * width * 4);
      memmove(books->book8 + destination8,
              books->book8 + destination8 - row_bytes, row_bytes);
      destination8 += row_bytes;
      left += row_pixels * width;
      right += row_pixels * width;
    }
  }
}
