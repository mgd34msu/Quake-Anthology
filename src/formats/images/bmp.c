/* Port of src/formats/images/bmp.ts. GPL-2.0-or-later. */
#include "qa/formats/images/bmp.h"
#include "qa/formats/images/limits.h"
#include "qa/foundation/support.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/formats/images/mip.h"

void qa_bmp_image_destroy(qa_bmp_image *image) {
  qa_image_level_destroy(&image->level);
  free(image->indices);
  free(image->palette);
  image->indices = NULL;
  image->palette = NULL;
  image->indices_length = 0;
  image->palette_length = 0;
  image->has_indexed = 0;
}

/* Owned decode state released when a BinaryError unwinds the decoder. */
typedef struct decode_guard {
  qa_binary_reader *reader;
  void **owned[3];
} decode_guard;

static void decode_guard_release(void *raw) {
  decode_guard *guard = (decode_guard *)raw;
  for (int i = 0; i < 3; i++)
    if (guard->owned[i] != NULL) free(*guard->owned[i]);
  qa_binary_reader_destroy(guard->reader);
}

int qa_decode_bmp(const uint8_t *bytes, size_t length, const char *source,
                  qa_bmp_image *out) {
  const char *name = source != NULL ? source : "<bmp>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  uint8_t *palette = NULL, *pixels = NULL, *indices = NULL;
  decode_guard guard_state = {&reader, {(void **)&palette, (void **)&pixels, (void **)&indices}};
  qa_cleanup guard;
  qa_cleanup_push(&guard, decode_guard_release, &guard_state);
  qa_binary_reader_expect_magic(&reader, "BM");
  qa_binary_reader_skip(&reader, 8);
  int64_t offset = qa_binary_reader_u32(&reader);
  int64_t header = qa_binary_reader_u32(&reader);
  int64_t width = qa_binary_reader_i32(&reader);
  int64_t signed_height = qa_binary_reader_i32(&reader);
  int64_t planes = qa_binary_reader_u16(&reader);
  int64_t depth = qa_binary_reader_u16(&reader);
  uint32_t compression = qa_binary_reader_u32(&reader);
  qa_binary_reader_skip(&reader, 12);
  int64_t color_count = qa_binary_reader_u32(&reader);
  qa_binary_reader_skip(&reader, 4);
  int64_t height = signed_height < 0 ? -signed_height : signed_height;
  if (signed_height == -2147483648LL) height = 2147483648LL;
  if (header != 40 || planes != 1 || compression != 0 ||
      (depth != 8 && depth != 24 && depth != 32)) {
    qa_binary_error(name, 14,
                    "BMP requires BITMAPINFOHEADER, BI_RGB and 8/24/32-bit pixels");
  }
  if (!qa_image_size_allowed((double)width, (double)height)) {
    qa_binary_error(name, 18, "Invalid BMP dimensions");
  }
  if (depth == 8) {
    if (color_count == 0) color_count = 256;
    if (color_count > 256) {
      qa_binary_error(name, 46, "BMP palette has too many entries");
    }
    palette = (uint8_t *)malloc((size_t)(color_count * 3));
    if (palette == NULL) { qa_binary_reader_destroy(&reader); qa_cleanup_pop(&guard); return 0; }
    for (int64_t entry = 0; entry < color_count; entry++) {
      uint8_t b = qa_binary_reader_u8(&reader);
      uint8_t g = qa_binary_reader_u8(&reader);
      uint8_t r = qa_binary_reader_u8(&reader);
      qa_binary_reader_skip(&reader, 1);
      palette[entry * 3] = r;
      palette[entry * 3 + 1] = g;
      palette[entry * 3 + 2] = b;
    }
  }
  if (offset < (int64_t)qa_binary_reader_offset(&reader)) {
    qa_binary_error(name, offset, "BMP pixels overlap the header or palette");
  }
  int64_t stride = (width * depth + 31) / 32 * 4;
  uint8_t *data = qa_binary_reader_data_view(&reader, offset, stride * height);
  size_t count = (size_t)(width * height);
  pixels = (uint8_t *)malloc(count * 4);
  indices = palette == NULL ? NULL : (uint8_t *)malloc(count);
  if (pixels == NULL || (palette != NULL && indices == NULL)) {
    free(pixels);
    free(indices);
    free(palette);
    qa_binary_reader_destroy(&reader);
    qa_cleanup_pop(&guard); return 0;
  }
  for (int64_t row = 0; row < height; row++) {
    for (int64_t x = 0; x < width; x++) {
      int64_t index = (signed_height < 0 ? row : height - row - 1) * width + x;
      int64_t at = row * stride + x * depth / 8;
      if (indices != NULL) {
        uint8_t color = data[at];
        if ((int64_t)color >= color_count) {
          qa_binary_error(name, offset + at, "BMP palette index out of range");
        }
        indices[index] = color;
        pixels[index * 4] = palette[color * 3];
        pixels[index * 4 + 1] = palette[color * 3 + 1];
        pixels[index * 4 + 2] = palette[color * 3 + 2];
        pixels[index * 4 + 3] = 255;
      } else {
        pixels[index * 4] = data[at + 2];
        pixels[index * 4 + 1] = data[at + 1];
        pixels[index * 4 + 2] = data[at];
        pixels[index * 4 + 3] = depth == 32 ? data[at + 3] : 255;
      }
    }
  }
  qa_binary_reader_destroy(&reader);
  out->level.width = (double)width;
  out->level.height = (double)height;
  out->level.pixels.bytes = pixels;
  out->level.pixels.length = count * 4;
  out->has_indexed = palette != NULL;
  out->indices = indices;
  out->indices_length = palette == NULL ? 0 : count;
  out->palette = palette;
  out->palette_length = palette == NULL ? 0 : (size_t)(color_count * 3);
  qa_cleanup_pop(&guard); return 1;
}
