/* Port of src/formats/images/tga.ts. GPL-2.0-or-later. */
#include "qa/formats/images/tga.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/formats/images/mip.h"

void qa_tga_image_destroy(qa_tga_image *image) {
  qa_image_level_destroy(&image->level);
  free(image->indices);
  free(image->palette_rgba);
  image->indices = NULL;
  image->palette_rgba = NULL;
  image->indices_length = 0;
  image->palette_rgba_length = 0;
  image->has_indexed = 0;
}

struct tga_state {
  qa_binary_reader *reader;
  const char *source;
  int64_t descriptor, depth, map_first, map_length;
  int indexed, gray;
  const uint8_t *palette; /* RGBA quads, map_length entries */
  int64_t current_index;
  uint8_t *pixels;
  uint16_t *indices;
  int64_t width, height, pixel;
};

static void tga_color(struct tga_state *state, int64_t bits, uint8_t rgba[4]) {
  qa_binary_reader *reader = state->reader;
  if (bits == 15 || bits == 16) {
    int64_t word = qa_binary_reader_u16(reader);
    int64_t r = (word >> 10) & 31, g = (word >> 5) & 31, b = word & 31;
    rgba[0] = (uint8_t)((r << 3) | (r >> 2));
    rgba[1] = (uint8_t)((g << 3) | (g >> 2));
    rgba[2] = (uint8_t)((b << 3) | (b >> 2));
    rgba[3] = bits == 16 && (state->descriptor & 15) != 0 && (word & 32768) == 0
                  ? 0
                  : 255;
    return;
  }
  if (bits != 24 && bits != 32) {
    qa_binary_error(state->source, (int64_t)qa_binary_reader_offset(reader),
                    "Unsupported TGA color depth %lld", (long long)bits);
  }
  uint8_t b = qa_binary_reader_u8(reader);
  uint8_t g = qa_binary_reader_u8(reader);
  uint8_t r = qa_binary_reader_u8(reader);
  rgba[0] = r;
  rgba[1] = g;
  rgba[2] = b;
  rgba[3] = bits == 32 ? qa_binary_reader_u8(reader) : 255;
}

static void tga_read(struct tga_state *state, uint8_t rgba[4]) {
  qa_binary_reader *reader = state->reader;
  if (state->indexed) {
    int64_t index = state->depth == 8 ? qa_binary_reader_u8(reader)
                                      : qa_binary_reader_u16(reader);
    int64_t at = index - state->map_first;
    if (at < 0 || at >= state->map_length) {
      qa_binary_error(state->source, (int64_t)qa_binary_reader_offset(reader),
                      "TGA palette index out of range");
    }
    state->current_index = index;
    memcpy(rgba, state->palette + at * 4, 4);
    return;
  }
  if (state->gray) {
    uint8_t value = qa_binary_reader_u8(reader);
    rgba[0] = value;
    rgba[1] = value;
    rgba[2] = value;
    rgba[3] = state->depth == 16 ? qa_binary_reader_u8(reader) : 255;
    return;
  }
  tga_color(state, state->depth, rgba);
}

static void tga_write(struct tga_state *state, const uint8_t rgba[4]) {
  int64_t x = state->pixel % state->width;
  int64_t y = state->pixel / state->width;
  int64_t destination_x = (state->descriptor & 16) == 0 ? x : state->width - 1 - x;
  int64_t destination_y =
      (state->descriptor & 32) == 0 ? state->height - 1 - y : y;
  int64_t at = destination_y * state->width + destination_x;
  memcpy(state->pixels + at * 4, rgba, 4);
  if (state->indices != NULL) state->indices[at] = (uint16_t)state->current_index;
  state->pixel++;
}

int qa_decode_tga(const uint8_t *bytes, size_t length, const char *source,
                  qa_tga_image *out) {
  const char *name = source != NULL ? source : "<tga>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name)) return 0;
  memset(out, 0, sizeof *out);
  int64_t id_length = qa_binary_reader_u8(&reader);
  int64_t map_type = qa_binary_reader_u8(&reader);
  int64_t type = qa_binary_reader_u8(&reader);
  int64_t map_first = qa_binary_reader_u16(&reader);
  int64_t map_length = qa_binary_reader_u16(&reader);
  int64_t map_depth = qa_binary_reader_u8(&reader);
  qa_binary_reader_skip(&reader, 4);
  int64_t width = qa_binary_reader_u16(&reader);
  int64_t height = qa_binary_reader_u16(&reader);
  int64_t depth = qa_binary_reader_u8(&reader);
  int64_t descriptor = qa_binary_reader_u8(&reader);
  int indexed = type == 1 || type == 9;
  int gray = type == 3 || type == 11;
  int rle = type >= 9;
  int type_ok = type == 1 || type == 2 || type == 3 || type == 9 || type == 10 ||
                type == 11;
  int depth_ok = indexed ? depth == 8 || depth == 16
                 : gray  ? depth == 8 || depth == 16
                         : depth == 16 || depth == 24 || depth == 32;
  if (!type_ok || width == 0 || height == 0 ||
      (double)width * (double)height * 4.0 > 2147483647.0 ||
      (descriptor & 192) != 0 || map_type != (indexed ? 1 : 0) || !depth_ok) {
    qa_binary_error(name, 0, "Unsupported TGA header");
  }
  qa_binary_reader_skip(&reader, id_length);
  struct tga_state state;
  memset(&state, 0, sizeof state);
  state.reader = &reader;
  state.source = name;
  state.descriptor = descriptor;
  state.depth = depth;
  state.map_first = map_first;
  state.map_length = map_length;
  state.indexed = indexed;
  state.gray = gray;
  state.width = width;
  state.height = height;
  uint8_t *palette = NULL;
  if (indexed) {
    palette = map_length == 0 ? NULL
                              : (uint8_t *)malloc((size_t)map_length * 4);
    if (map_length != 0 && palette == NULL) {
      qa_binary_reader_destroy(&reader);
      return 0;
    }
    for (int64_t entry = 0; entry < map_length; entry++) {
      tga_color(&state, map_depth, palette + entry * 4);
    }
  }
  state.palette = palette;
  size_t count = (size_t)(width * height);
  uint8_t *pixels = (uint8_t *)malloc(count * 4);
  uint16_t *indices = indexed ? (uint16_t *)calloc(count, sizeof(uint16_t)) : NULL;
  if (pixels == NULL || (indexed && indices == NULL)) {
    free(pixels);
    free(indices);
    free(palette);
    qa_binary_reader_destroy(&reader);
    return 0;
  }
  state.pixels = pixels;
  state.indices = indices;
  int64_t total = width * height;
  while (state.pixel < total) {
    int64_t packet = rle ? qa_binary_reader_u8(&reader) : 0;
    int64_t run = rle ? (packet & 127) + 1 : 1;
    if (run > total - state.pixel) {
      qa_binary_error(name, (int64_t)qa_binary_reader_offset(&reader),
                      "TGA packet exceeds pixel count");
    }
    if ((packet & 128) != 0) {
      uint8_t rgba[4];
      tga_read(&state, rgba);
      for (int64_t i = 0; i < run; i++) tga_write(&state, rgba);
    } else {
      for (int64_t i = 0; i < run; i++) {
        uint8_t rgba[4];
        tga_read(&state, rgba);
        tga_write(&state, rgba);
      }
    }
  }
  qa_binary_reader_destroy(&reader);
  out->level.width = (double)width;
  out->level.height = (double)height;
  out->level.pixels.bytes = pixels;
  out->level.pixels.length = count * 4;
  out->descriptor = descriptor;
  out->has_indexed = indexed;
  out->indices = indices;
  out->indices_length = indexed ? count : 0;
  out->first = map_first;
  out->palette_rgba = palette;
  out->palette_rgba_length = indexed ? (size_t)map_length * 4 : 0;
  return 1;
}

uint8_t *qa_encode_tga(const qa_image_level *image, size_t *out_length) {
  if (!isfinite(image->width) || !isfinite(image->height) ||
      trunc(image->width) != image->width || trunc(image->height) != image->height ||
      image->width <= 0 || image->width > 65535 || image->height <= 0 ||
      image->height > 65535 ||
      (double)image->pixels.length != image->width * image->height * 4.0) {
    qa_range_error("Invalid TGA output");
  }
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)(18 + image->pixels.length))) {
    return NULL;
  }
  static const uint8_t zeroes9[9] = {0};
  qa_binary_writer_u8(&writer, 0);
  qa_binary_writer_u8(&writer, 0);
  qa_binary_writer_u8(&writer, 2);
  qa_binary_writer_bytes(&writer, zeroes9, 9);
  qa_binary_writer_u16(&writer, (int64_t)image->width);
  qa_binary_writer_u16(&writer, (int64_t)image->height);
  qa_binary_writer_u8(&writer, 32);
  qa_binary_writer_u8(&writer, 40);
  for (size_t i = 0; i < image->pixels.length; i += 4) {
    uint8_t r = image->pixels.bytes[i], g = image->pixels.bytes[i + 1];
    uint8_t b = image->pixels.bytes[i + 2], a = image->pixels.bytes[i + 3];
    qa_binary_writer_u8(&writer, b);
    qa_binary_writer_u8(&writer, g);
    qa_binary_writer_u8(&writer, r);
    qa_binary_writer_u8(&writer, a);
  }
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
