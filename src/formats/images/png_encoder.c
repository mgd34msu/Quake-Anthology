/* Port of src/formats/images/png-encoder.ts. GPL-2.0-or-later. */
#include "qa/formats/images/png_encoder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/foundation/support.h"

static uint32_t png_crc32(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : 0xedb88320u);
    }
  }
  return crc ^ 0xffffffffu;
}

static void put_u32_be(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)(value >> 24);
  at[1] = (uint8_t)(value >> 16);
  at[2] = (uint8_t)(value >> 8);
  at[3] = (uint8_t)value;
}

static int safe_integer(double value) {
  return isfinite(value) && trunc(value) == value &&
         value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

uint8_t *qa_encode_png(double width, double height, const uint8_t *rgba,
                       size_t rgba_length, size_t *out_length) {
  if (!safe_integer(width) || !safe_integer(height) || width <= 0 || height <= 0 ||
      width > 16384 || height > 16384 ||
      (double)rgba_length != width * height * 4.0) {
    qa_range_error("PNG dimensions do not match the RGBA framebuffer");
  }
  int64_t w = (int64_t)width, h = (int64_t)height;
  uint8_t header[13] = {0};
  put_u32_be(header, (uint32_t)w);
  put_u32_be(header + 4, (uint32_t)h);
  header[8] = 8;
  header[9] = 6;
  size_t stride = (size_t)w * 4;
  size_t scanline_length = (size_t)h * (stride + 1);
  uint8_t *scanlines = (uint8_t *)calloc(scanline_length, 1);
  if (scanlines == NULL) return NULL;
  for (int64_t y = 0; y < h; y++) {
    memcpy(scanlines + (size_t)y * (stride + 1) + 1, rgba + (size_t)y * stride,
           stride);
  }
  /* node:zlib deflateSync defaults (level -1, memLevel 8). */
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
    free(scanlines);
    return NULL;
  }
  uLong bound = deflateBound(&stream, (uLong)scanline_length);
  uint8_t *compressed = (uint8_t *)malloc(bound);
  if (compressed == NULL) {
    deflateEnd(&stream);
    free(scanlines);
    return NULL;
  }
  stream.next_in = scanlines;
  stream.avail_in = (uInt)scanline_length;
  stream.next_out = compressed;
  stream.avail_out = (uInt)bound;
  int status = deflate(&stream, Z_FINISH);
  size_t compressed_length = stream.total_out;
  deflateEnd(&stream);
  free(scanlines);
  if (status != Z_STREAM_END) {
    free(compressed);
    return NULL;
  }
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  size_t total = 8 + (13 + 12) + (compressed_length + 12) + 12;
  uint8_t *output = (uint8_t *)malloc(total);
  if (output == NULL) {
    free(compressed);
    return NULL;
  }
  size_t at = 0;
  memcpy(output + at, signature, 8);
  at += 8;
  const struct { const char *type; const uint8_t *data; size_t length; } chunks[3] = {
      {"IHDR", header, 13},
      {"IDAT", compressed, compressed_length},
      {"IEND", NULL, 0},
  };
  for (int i = 0; i < 3; i++) {
    put_u32_be(output + at, (uint32_t)chunks[i].length);
    memcpy(output + at + 4, chunks[i].type, 4);
    if (chunks[i].length != 0) memcpy(output + at + 8, chunks[i].data, chunks[i].length);
    put_u32_be(output + at + 8 + chunks[i].length,
               png_crc32(output + at + 4, 4 + chunks[i].length));
    at += 12 + chunks[i].length;
  }
  free(compressed);
  *out_length = total;
  return output;
}
