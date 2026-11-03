/* Port of src/formats/images/q3-bmp.ts. GPL-2.0-or-later. */
#include "qa/formats/images/q3_bmp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

noreturn void qa_bmp_drop_error(const char *source, const char *format, ...) {
  (void)source; /* the TS keeps it as a field; the message embeds it */
  /* TS throws BmpDropError for its caller's ERR_DROP: a catchable raise. */
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("BmpDropError", 0, 0, "%s", message);
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

int qa_decode_q3_bmp(const uint8_t *bytes, size_t length, const char *source,
                     qa_image_data *out) {
  const char *name = source != NULL ? source : "<buffer>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  uint8_t *palette = NULL, *pixels = NULL;
  decode_guard guard_state = {&reader, {(void **)&palette, (void **)&pixels, NULL}};
  qa_cleanup guard;
  qa_cleanup_push(&guard, decode_guard_release, &guard_state);
  int64_t id0 = qa_binary_reader_u8(&reader);
  int64_t id1 = qa_binary_reader_u8(&reader);
  uint32_t file_size = qa_binary_reader_u32(&reader);
  qa_binary_reader_skip(&reader, 12);
  int64_t width = qa_binary_reader_i32(&reader);
  int64_t signed_height = qa_binary_reader_i32(&reader);
  qa_binary_reader_skip(&reader, 2);
  int64_t bits_per_pixel = qa_binary_reader_u16(&reader);
  uint32_t compression = qa_binary_reader_u32(&reader);
  qa_binary_reader_skip(&reader, 20);
  if (bits_per_pixel == 8) {
    palette = qa_binary_reader_bytes(&reader, 1024);
    if (palette == NULL) { qa_binary_reader_destroy(&reader); qa_cleanup_pop(&guard); return 0; }
  }
  /* The source check uses &&: a single wrong ID byte still passes. */
  if (id0 != 66 && id1 != 77) {
    qa_bmp_drop_error(name, "LoadBMP: only Windows-style BMP files supported (%s)\n",
                      name);
  }
  if ((uint64_t)file_size != (uint64_t)length) {
    qa_bmp_drop_error(name,
                      "LoadBMP: header size does not match file size (%lld vs. %zu) (%s)\n",
                      (long long)(int32_t)file_size, length, name);
  }
  if (compression != 0) {
    qa_bmp_drop_error(name, "LoadBMP: only uncompressed BMP files supported (%s)\n",
                      name);
  }
  if (bits_per_pixel < 8) {
    qa_bmp_drop_error(name,
                      "LoadBMP: monochrome and 4-bit BMP files not supported (%s)\n",
                      name);
  }
  if (width < 0) {
    qa_binary_error(name, 18, "negative BMP width %lld", (long long)width);
  }
  if (signed_height == -2147483648LL) {
    qa_binary_error(name, 22,
                    "BMP height negation overflows signed 32-bit source arithmetic");
  }
  int64_t height = signed_height < 0 ? -signed_height : signed_height;
  double output_length = (double)width * (double)height * 4.0;
  if (output_length > 2147483647.0) {
    qa_binary_error(name, 18,
                    "decoded BMP size %.0f overflows signed 32-bit source allocation arithmetic",
                    output_length);
  }
  if (output_length == 0) {
    free(palette);
    qa_binary_reader_destroy(&reader);
    out->width = width;
    out->height = height;
    out->pixels = NULL;
    out->pixels_length = 0;
    qa_cleanup_pop(&guard); return 1;
  }
  if (bits_per_pixel == 16) {
    qa_binary_error(name, 28,
                    "16-bit LoadBMP source path reads uninitialized output and writes beyond its allocation; unsupported source-indeterminate pixels");
  }
  if (bits_per_pixel != 8 && bits_per_pixel != 24 && bits_per_pixel != 32) {
    qa_bmp_drop_error(name, "LoadBMP: illegal pixel_size '%lld' in file '%s'\n",
                      (long long)bits_per_pixel, name);
  }
  double required = (double)width * (double)height * ((double)bits_per_pixel / 8.0);
  if ((double)qa_binary_reader_remaining(&reader) < required) {
    qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                    "truncated BMP pixel data");
  }
  size_t out_length = (size_t)(width * height * 4);
  pixels = (uint8_t *)malloc(out_length);
  if (pixels == NULL) {
    free(palette);
    qa_binary_reader_destroy(&reader);
    qa_cleanup_pop(&guard); return 0;
  }
  for (int64_t row = height - 1; row >= 0; row--) {
    size_t destination = (size_t)(row * width * 4);
    for (int64_t column = 0; column < width; column++) {
      if (palette != NULL) {
        size_t index = (size_t)qa_binary_reader_u8(&reader) * 4;
        pixels[destination++] = palette[index + 2];
        pixels[destination++] = palette[index + 1];
        pixels[destination++] = palette[index];
        pixels[destination++] = 255;
      } else {
        uint8_t blue = qa_binary_reader_u8(&reader);
        uint8_t green = qa_binary_reader_u8(&reader);
        uint8_t red = qa_binary_reader_u8(&reader);
        pixels[destination++] = red;
        pixels[destination++] = green;
        pixels[destination++] = blue;
        pixels[destination++] = bits_per_pixel == 32 ? qa_binary_reader_u8(&reader) : 255;
      }
    }
  }
  free(palette);
  qa_binary_reader_destroy(&reader);
  out->width = width;
  out->height = height;
  out->pixels = pixels;
  out->pixels_length = out_length;
  qa_cleanup_pop(&guard); return 1;
}
