/* Port of src/formats/images/q3-pcx.ts. GPL-2.0-or-later. */
#include "qa/formats/images/q3_pcx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"

void qa_q3_pcx_indexed_destroy(qa_q3_pcx_indexed *image) {
  free(image->indices);
  free(image->palette);
  image->indices = NULL;
  image->palette = NULL;
  image->indices_length = 0;
  image->palette_length = 0;
}

int qa_decode_q3_pcx_indexed(const uint8_t *bytes, size_t length,
                             const char *source, qa_q3_pcx_indexed *out,
                             char **rejection_message) {
  const char *name = source != NULL ? source : "<buffer>";
  if (rejection_message != NULL) *rejection_message = NULL;
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  if (qa_binary_reader_length(&reader) < 12) {
    qa_binary_error(name, 0, "truncated PCX header");
  }
  int64_t manufacturer = qa_binary_reader_u8(&reader);
  int64_t version = qa_binary_reader_u8(&reader);
  int64_t encoding = qa_binary_reader_u8(&reader);
  int64_t bits_per_pixel = qa_binary_reader_u8(&reader);
  qa_binary_reader_skip(&reader, 4);
  int64_t xmax = qa_binary_reader_u16(&reader);
  int64_t ymax = qa_binary_reader_u16(&reader);
  int64_t width = xmax + 1, height = ymax + 1;
  if (manufacturer != 0x0a || version != 5 || encoding != 1 ||
      bits_per_pixel != 8 || xmax >= 1024 || ymax >= 1024) {
    qa_binary_reader_destroy(&reader);
    if (rejection_message != NULL) {
      size_t capacity = strlen(name) + 96;
      char *message = (char *)malloc(capacity);
      if (message == NULL) return 0;
      snprintf(message, capacity, "Bad pcx file %s (%lld x %lld) (%lld x %lld)\n",
               name, (long long)width, (long long)height, (long long)xmax,
               (long long)ymax);
      *rejection_message = message;
    }
    return -1;
  }
  if (qa_binary_reader_length(&reader) < 768) {
    qa_binary_error(name, (int64_t)qa_binary_reader_length(&reader) - 768,
                    "PCX palette precedes input");
  }
  size_t count = (size_t)(width * height);
  uint8_t *indices = count == 0 ? NULL : (uint8_t *)calloc(count, 1);
  if (count != 0 && indices == NULL) { qa_binary_reader_destroy(&reader); return 0; }
  qa_binary_reader palette_section;
  if (!qa_binary_reader_section(&reader,
                                (int64_t)qa_binary_reader_length(&reader) - 768,
                                768, &palette_section)) {
    free(indices);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  uint8_t *palette = qa_binary_reader_bytes(&palette_section, 768);
  qa_binary_reader_destroy(&palette_section);
  if (palette == NULL) {
    free(indices);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  qa_binary_reader_seek(&reader, 128);
  for (int64_t y = 0; y < height; y++) {
    int64_t x = 0;
    while (x < width) {
      int64_t packet_offset = (int64_t)qa_binary_reader_offset(&reader);
      int64_t data_byte = qa_binary_reader_u8(&reader);
      int64_t run_length = 1;
      if ((data_byte & 0xc0) == 0xc0) {
        run_length = data_byte & 0x3f;
        data_byte = qa_binary_reader_u8(&reader);
      }
      int64_t destination = y * width + x;
      if (run_length > (int64_t)count - destination) {
        qa_binary_error(name, packet_offset,
                        "PCX RLE packet overruns image allocation");
      }
      for (int64_t i = 0; i < run_length; i++) {
        indices[destination + i] = (uint8_t)data_byte;
      }
      x += run_length;
    }
  }
  qa_binary_reader_destroy(&reader);
  out->width = width;
  out->height = height;
  out->indices = indices;
  out->indices_length = count;
  out->palette = palette;
  out->palette_length = 768;
  return 1;
}

int qa_expand_q3_pcx(const qa_q3_pcx_indexed *image, qa_image_data *out) {
  qa_binary_reader palette;
  if (!qa_binary_reader_init(&palette, image->palette, image->palette_length,
                             NULL)) {
    return 0;
  }
  size_t out_length = image->indices_length * 4;
  uint8_t *pixels = out_length == 0 ? NULL : (uint8_t *)malloc(out_length);
  if (out_length != 0 && pixels == NULL) {
    qa_binary_reader_destroy(&palette);
    return 0;
  }
  size_t destination = 0;
  for (size_t i = 0; i < image->indices_length; i++) {
    qa_binary_reader_seek(&palette, (int64_t)image->indices[i] * 3);
    pixels[destination++] = qa_binary_reader_u8(&palette);
    pixels[destination++] = qa_binary_reader_u8(&palette);
    pixels[destination++] = qa_binary_reader_u8(&palette);
    pixels[destination++] = 255;
  }
  qa_binary_reader_destroy(&palette);
  out->width = image->width;
  out->height = image->height;
  out->pixels = pixels;
  out->pixels_length = out_length;
  return 1;
}

int qa_decode_q3_pcx(const uint8_t *bytes, size_t length, const char *source,
                     qa_image_data *out, char **rejection_message) {
  qa_q3_pcx_indexed decoded;
  int result = qa_decode_q3_pcx_indexed(bytes, length, source, &decoded,
                                        rejection_message);
  if (result != 1) return result;
  result = qa_expand_q3_pcx(&decoded, out);
  qa_q3_pcx_indexed_destroy(&decoded);
  return result;
}
