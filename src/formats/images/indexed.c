/* Port of src/formats/images/indexed.ts. GPL-2.0-or-later. */
#include "qa/formats/images/indexed.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/formats/images/mip.h"

static int64_t dimensions(int64_t width, int64_t height, const char *source) {
  double count = (double)width * (double)height;
  if (width <= 0 || height <= 0 || count > 2147483647.0) {
    qa_binary_error(source, 0, "Invalid image dimensions %lldx%lld",
                    (long long)width, (long long)height);
  }
  return (int64_t)count;
}

static int mip_level(qa_binary_reader *reader, int64_t width, int64_t height,
                     int64_t offset, int64_t minimum, qa_image_level *out) {
  if (offset < minimum) {
    qa_binary_error(reader->source, offset, "Mip data overlaps the header");
  }
  int64_t count = dimensions(width, height, reader->source);
  qa_binary_reader section;
  if (!qa_binary_reader_section(reader, offset, count, &section)) return 0;
  uint8_t *pixels = qa_binary_reader_bytes(&section, width * height);
  qa_binary_reader_destroy(&section);
  if (pixels == NULL && count != 0) return 0;
  out->width = (double)width;
  out->height = (double)height;
  out->pixels.bytes = pixels;
  out->pixels.length = (size_t)count;
  return 1;
}

void qa_indexed_image_destroy(qa_indexed_image *image) {
  free(image->indices);
  image->indices = NULL;
  image->indices_length = 0;
}

void qa_pcx_image_destroy(qa_pcx_image *image) {
  free(image->indices);
  free(image->palette);
  image->indices = NULL;
  image->palette = NULL;
  image->indices_length = 0;
  image->palette_length = 0;
}

void qa_q1_miptex_destroy(qa_q1_miptex *texture) {
  free(texture->name);
  texture->name = NULL;
  if (texture->kind == QA_Q1_MIP_EMBEDDED) {
    for (int i = 0; i < 4; i++) qa_image_level_destroy(&texture->levels[i]);
  }
}

void qa_wal_image_destroy(qa_wal_image *image) {
  free(image->name);
  free(image->animation);
  image->name = NULL;
  image->animation = NULL;
  for (int i = 0; i < 4; i++) qa_image_level_destroy(&image->levels[i]);
}

int qa_decode_q1_mip_texture(const uint8_t *bytes, size_t length,
                             const char *source, qa_q1_miptex *out) {
  const char *name = source != NULL ? source : "<miptex>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  out->name = qa_binary_reader_fixed_byte_string(&reader, 16);
  if (out->name == NULL) { qa_binary_reader_destroy(&reader); return 0; }
  int64_t width = qa_binary_reader_u32(&reader);
  int64_t height = qa_binary_reader_u32(&reader);
  dimensions(width, height, name);
  if (width % 16 != 0 || height % 16 != 0) {
    qa_binary_error(name, 16, "Q1 mip dimensions must be multiples of 16");
  }
  int64_t a = qa_binary_reader_u32(&reader), b = qa_binary_reader_u32(&reader);
  int64_t c = qa_binary_reader_u32(&reader), d = qa_binary_reader_u32(&reader);
  out->width = width;
  out->height = height;
  if (a == 0 && b == 0 && c == 0 && d == 0) {
    out->kind = QA_Q1_MIP_EXTERNAL;
    qa_binary_reader_destroy(&reader);
    return 1;
  }
  out->kind = QA_Q1_MIP_EMBEDDED;
  int ok = mip_level(&reader, width, height, a, 40, &out->levels[0]) &&
           mip_level(&reader, width / 2, height / 2, b, 40, &out->levels[1]) &&
           mip_level(&reader, width / 4, height / 4, c, 40, &out->levels[2]) &&
           mip_level(&reader, width / 8, height / 8, d, 40, &out->levels[3]);
  qa_binary_reader_destroy(&reader);
  if (!ok) { qa_q1_miptex_destroy(out); return 0; }
  return 1;
}

static int64_t wal_half(int64_t value, int shift) {
  int64_t result = value >> shift;
  return result < 1 ? 1 : result;
}

int qa_decode_wal(const uint8_t *bytes, size_t length, const char *source,
                  qa_wal_image *out) {
  const char *name = source != NULL ? source : "<wal>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  out->name = qa_binary_reader_fixed_byte_string(&reader, 32);
  if (out->name == NULL) { qa_binary_reader_destroy(&reader); return 0; }
  int64_t width = qa_binary_reader_u32(&reader);
  int64_t height = qa_binary_reader_u32(&reader);
  dimensions(width, height, name);
  int64_t a = qa_binary_reader_u32(&reader), b = qa_binary_reader_u32(&reader);
  int64_t c = qa_binary_reader_u32(&reader), d = qa_binary_reader_u32(&reader);
  out->animation = qa_binary_reader_fixed_byte_string(&reader, 32);
  if (out->animation == NULL) {
    qa_binary_reader_destroy(&reader);
    qa_wal_image_destroy(out);
    return 0;
  }
  out->width = width;
  out->height = height;
  out->flags = qa_binary_reader_i32(&reader);
  out->contents = qa_binary_reader_i32(&reader);
  out->value = qa_binary_reader_i32(&reader);
  int ok = mip_level(&reader, width, height, a, 100, &out->levels[0]) &&
           mip_level(&reader, wal_half(width, 1), wal_half(height, 1), b, 100,
                     &out->levels[1]) &&
           mip_level(&reader, wal_half(width, 2), wal_half(height, 2), c, 100,
                     &out->levels[2]) &&
           mip_level(&reader, wal_half(width, 3), wal_half(height, 3), d, 100,
                     &out->levels[3]);
  qa_binary_reader_destroy(&reader);
  if (!ok) { qa_wal_image_destroy(out); return 0; }
  return 1;
}

int qa_decode_qpic(const uint8_t *bytes, size_t length, const char *source,
                   qa_indexed_image *out) {
  const char *name = source != NULL ? source : "<qpic>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  int64_t width = qa_binary_reader_i32(&reader);
  int64_t height = qa_binary_reader_i32(&reader);
  int64_t count = dimensions(width, height, name);
  uint8_t *indices = qa_binary_reader_bytes(&reader, count);
  qa_binary_reader_destroy(&reader);
  if (indices == NULL && count != 0) return 0;
  out->width = width;
  out->height = height;
  out->indices = indices;
  out->indices_length = (size_t)count;
  return 1;
}

int qa_decode_pcx(const uint8_t *bytes, size_t length, const char *source,
                  qa_pcx_image *out) {
  const char *name = source != NULL ? source : "<pcx>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  if (qa_binary_reader_u8(&reader) != 10 || qa_binary_reader_u8(&reader) != 5 ||
      qa_binary_reader_u8(&reader) != 1 || qa_binary_reader_u8(&reader) != 8) {
    qa_binary_error(name, 0, "Expected version 5, RLE, 8-bit PCX");
  }
  int64_t xmin = qa_binary_reader_u16(&reader), ymin = qa_binary_reader_u16(&reader);
  int64_t xmax = qa_binary_reader_u16(&reader), ymax = qa_binary_reader_u16(&reader);
  int64_t width = xmax - xmin + 1, height = ymax - ymin + 1;
  dimensions(width, height, name);
  qa_binary_reader_seek(&reader, 65);
  int64_t planes = qa_binary_reader_u8(&reader);
  int64_t bytes_per_line = qa_binary_reader_u16(&reader);
  if (planes != 1 || bytes_per_line < width) {
    qa_binary_error(name, 65, "Expected single-plane PCX with a complete scanline");
  }
  int64_t palette_offset = (int64_t)length - 769;
  int has_palette = palette_offset >= 128 && bytes[palette_offset] == 12;
  int64_t encoded_length = (has_palette ? palette_offset : (int64_t)length) - 128;
  qa_binary_reader encoded;
  if (!qa_binary_reader_section(&reader, 128, encoded_length, &encoded)) {
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  size_t count = (size_t)(width * height);
  uint8_t *indices = count == 0 ? NULL : (uint8_t *)malloc(count);
  if (count != 0 && indices == NULL) {
    qa_binary_reader_destroy(&encoded);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  for (int64_t y = 0; y < height; y++) {
    int64_t x = 0;
    while (x < bytes_per_line) {
      int64_t value = qa_binary_reader_u8(&encoded), run = 1;
      if ((value & 192) == 192) { run = value & 63; value = qa_binary_reader_u8(&encoded); }
      if (run == 0 || run > bytes_per_line - x) {
        int64_t at = (int64_t)qa_binary_reader_offset(&encoded) + 128;
        qa_binary_error(name, at, "PCX run exceeds its scanline");
      }
      if (x < width) {
        int64_t end = x + run < width ? x + run : width;
        for (int64_t i = x; i < end; i++) indices[y * width + i] = (uint8_t)value;
      }
      x += run;
    }
  }
  qa_binary_reader_destroy(&encoded);
  qa_binary_reader_destroy(&reader);
  out->width = width;
  out->height = height;
  out->indices = indices;
  out->indices_length = count;
  if (has_palette) {
    out->palette = (uint8_t *)malloc(768);
    if (out->palette == NULL) { qa_pcx_image_destroy(out); return 0; }
    memcpy(out->palette, bytes + palette_offset + 1, 768);
    out->palette_length = 768;
  }
  return 1;
}

uint8_t *qa_encode_pcx(const qa_indexed_image *image, const uint8_t *palette,
                       size_t palette_length, size_t *out_length) {
  dimensions(image->width, image->height, "PCX output");
  if (image->width > 65534 || image->height > 65536 ||
      image->indices_length != (size_t)(image->width * image->height) ||
      palette_length != 768) {
    qa_range_error("Invalid PCX output dimensions or palette");
  }
  int64_t stride = (image->width + 1) & ~1LL;
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, 128 + stride * image->height * 2 + 769)) {
    return NULL;
  }
  static const uint8_t magic[4] = {10, 5, 1, 8};
  static const uint8_t zeroes49[49] = {0};
  static const uint8_t zeroes58[58] = {0};
  qa_binary_writer_bytes(&writer, magic, 4);
  qa_binary_writer_u16(&writer, 0);
  qa_binary_writer_u16(&writer, 0);
  qa_binary_writer_u16(&writer, image->width - 1);
  qa_binary_writer_u16(&writer, image->height - 1);
  qa_binary_writer_u16(&writer, image->width);
  qa_binary_writer_u16(&writer, image->height < 65535 ? image->height : 65535);
  qa_binary_writer_bytes(&writer, zeroes49, 49);
  qa_binary_writer_u8(&writer, 1);
  qa_binary_writer_u16(&writer, stride);
  qa_binary_writer_u16(&writer, 1);
  qa_binary_writer_bytes(&writer, zeroes58, 58);
  for (int64_t y = 0; y < image->height; y++) {
    for (int64_t x = 0; x < stride; x++) {
      uint8_t value = x < image->width ? image->indices[y * image->width + x] : 0;
      if ((value & 192) == 192) qa_binary_writer_u8(&writer, 193);
      qa_binary_writer_u8(&writer, value);
    }
  }
  qa_binary_writer_u8(&writer, 12);
  qa_binary_writer_bytes(&writer, palette, 768);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}

void qa_lit_destroy(qa_lit *lit) {
  free(lit->samples);
  lit->samples = NULL;
  lit->samples_length = 0;
}

int qa_decode_lit(const uint8_t *bytes, size_t length, int has_expected,
                  int64_t expected_samples, const char *source, qa_lit *out) {
  const char *name = source != NULL ? source : "<lit>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  qa_binary_reader_expect_magic(&reader, "QLIT");
  if (qa_binary_reader_i32(&reader) != 1) {
    qa_binary_error(name, 4, "Only QLIT version 1 is supported");
  }
  size_t remaining = qa_binary_reader_remaining(&reader);
  if (remaining % 3 != 0 ||
      (has_expected && (int64_t)remaining != expected_samples * 3)) {
    qa_binary_error(name, 8, "QLIT sample count does not match lighting");
  }
  uint8_t *samples = qa_binary_reader_bytes(&reader, (int64_t)remaining);
  qa_binary_reader_destroy(&reader);
  if (samples == NULL && remaining != 0) return 0;
  out->samples = samples;
  out->samples_length = remaining;
  return 1;
}
