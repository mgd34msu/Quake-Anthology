/* Port of src/formats/images/png.ts. GPL-2.0-or-later. */
#include "qa/formats/images/png.h"
#include "qa/formats/images/limits.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/core/binary/index.h"
#include "qa/formats/images/mip.h"

static uint32_t get_u32_be(const uint8_t *bytes, size_t offset) {
  return (uint32_t)bytes[offset] << 24 | (uint32_t)bytes[offset + 1] << 16 |
         (uint32_t)bytes[offset + 2] << 8 | (uint32_t)bytes[offset + 3];
}
static uint32_t get_u16_be(const uint8_t *bytes, size_t offset) {
  return (uint32_t)bytes[offset] << 8 | (uint32_t)bytes[offset + 1];
}

static int paeth(int a, int b, int c) {
  int p = a + b - c;
  int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static uint32_t crc_table[256];
static int crc_table_ready;
static uint32_t png_crc32(const uint8_t *bytes, size_t length) {
  if (!crc_table_ready) {
    for (int value = 0; value < 256; value++) {
      uint32_t crc = (uint32_t)value;
      for (int bit = 0; bit < 8; bit++) {
        crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : 0xedb88320u);
      }
      crc_table[value] = crc;
    }
    crc_table_ready = 1;
  }
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc = crc_table[(crc ^ bytes[i]) & 0xff] ^ (crc >> 8);
  }
  return crc ^ 0xffffffffu;
}

void qa_png_image_destroy(qa_png_image *image) {
  qa_image_level_destroy(&image->level);
  free(image->indices);
  free(image->palette);
  free(image->alpha);
  image->indices = NULL;
  image->palette = NULL;
  image->alpha = NULL;
  image->indices_length = 0;
  image->palette_length = 0;
  image->alpha_length = 0;
  image->has_indexed = 0;
}

struct idat_part { size_t offset, length; };

static int64_t pass_size(int64_t total, int64_t start, int64_t step) {
  if (total <= start) return 0;
  return (total - start + step - 1) / step;
}

static int set_indexed(const uint8_t *transparency, size_t transparency_length,
                       uint8_t *palette, size_t palette_length, uint8_t *indices,
                       size_t indices_length, qa_png_image *out) {
  size_t alpha_length = palette_length / 3;
  uint8_t *alpha = alpha_length == 0 ? NULL : (uint8_t *)malloc(alpha_length);
  if (alpha_length != 0 && alpha == NULL) return 0;
  for (size_t i = 0; i < alpha_length; i++) alpha[i] = 255;
  if (transparency != NULL && transparency_length != 0) {
    memcpy(alpha, transparency, transparency_length);
  }
  out->has_indexed = 1;
  out->indices = indices;
  out->indices_length = indices_length;
  out->palette = palette;
  out->palette_length = palette_length;
  out->alpha = alpha;
  out->alpha_length = alpha_length;
  return 1;
}

static int png_fast(const char *name, uint8_t *raw, int64_t width, int64_t height,
                    int64_t color_type, int64_t channels, int has_transparency,
                    const uint8_t *transparency, size_t transparency_length,
                    uint8_t *palette, size_t palette_length, uint8_t *pixels,
                    uint8_t *indices, qa_png_image *out) {
  int64_t stride = channels, row_length = width * stride, row_stride = row_length + 1;
  for (int64_t py = 0; py < height; py++) {
    int64_t row_start = py * row_stride, offset = row_start + 1;
    int filter = raw[row_start];
    if (filter > 4) {
      qa_binary_error(name, 8, "PNG: unknown scanline filter %d", filter);
    }
    if (filter == 1) {
      for (int64_t byte = stride; byte < row_length; byte++) {
        raw[offset + byte] =
            (uint8_t)(raw[offset + byte] + raw[offset + byte - stride]);
      }
    } else if (filter == 2 && py > 0) {
      int64_t up = offset - row_stride;
      for (int64_t byte = 0; byte < row_length; byte++) {
        raw[offset + byte] = (uint8_t)(raw[offset + byte] + raw[up + byte]);
      }
    } else if (filter == 3) {
      int64_t up = offset - row_stride;
      for (int64_t byte = 0; byte < row_length; byte++) {
        int a = byte >= stride ? raw[offset + byte - stride] : 0;
        int b = py > 0 ? raw[up + byte] : 0;
        raw[offset + byte] = (uint8_t)(raw[offset + byte] + ((a + b) >> 1));
      }
    } else if (filter == 4) {
      int64_t up = offset - row_stride;
      for (int64_t byte = 0; byte < row_length; byte++) {
        int a = byte >= stride ? raw[offset + byte - stride] : 0;
        int b = py > 0 ? raw[up + byte] : 0;
        int c = py > 0 && byte >= stride ? raw[up + byte - stride] : 0;
        raw[offset + byte] = (uint8_t)(raw[offset + byte] + paeth(a, b, c));
      }
    }
  }
  if (color_type == 6) {
    for (int64_t py = 0; py < height; py++) {
      memcpy(pixels + py * row_length, raw + py * row_stride + 1, (size_t)row_length);
    }
  } else if (color_type == 2) {
    int64_t tr = has_transparency ? (int64_t)get_u16_be(transparency, 0) : -1;
    int64_t tg = has_transparency ? (int64_t)get_u16_be(transparency, 2) : -1;
    int64_t tb = has_transparency ? (int64_t)get_u16_be(transparency, 4) : -1;
    int64_t produced = 0;
    for (int64_t py = 0; py < height; py++) {
      int64_t at = py * row_stride + 1;
      for (int64_t px = 0; px < width; px++) {
        int r = raw[at], g = raw[at + 1], b = raw[at + 2];
        pixels[produced] = (uint8_t)r;
        pixels[produced + 1] = (uint8_t)g;
        pixels[produced + 2] = (uint8_t)b;
        pixels[produced + 3] = r == tr && g == tg && b == tb ? 0 : 255;
        at += 3;
        produced += 4;
      }
    }
  } else if (color_type == 0 || color_type == 4) {
    int64_t transparent_gray =
        has_transparency && color_type == 0 ? (int64_t)get_u16_be(transparency, 0) : -1;
    int64_t produced = 0;
    for (int64_t py = 0; py < height; py++) {
      int64_t at = py * row_stride + 1;
      for (int64_t px = 0; px < width; px++) {
        int value = raw[at];
        pixels[produced] = (uint8_t)value;
        pixels[produced + 1] = (uint8_t)value;
        pixels[produced + 2] = (uint8_t)value;
        pixels[produced + 3] =
            color_type == 4 ? raw[at + 1] : value == transparent_gray ? 0 : 255;
        at += stride;
        produced += 4;
      }
    }
  } else if (indices != NULL && palette != NULL) {
    int64_t produced = 0, index = 0;
    for (int64_t py = 0; py < height; py++) {
      int64_t at = py * row_stride + 1;
      for (int64_t px = 0; px < width; px++) {
        int value = raw[at++];
        if ((size_t)value * 3 + 3 > palette_length) {
          qa_binary_error(name, 8, "PNG: palette index out of range");
        }
        indices[index++] = (uint8_t)value;
        pixels[produced] = palette[value * 3];
        pixels[produced + 1] = palette[value * 3 + 1];
        pixels[produced + 2] = palette[value * 3 + 2];
        pixels[produced + 3] =
            has_transparency && (size_t)value < transparency_length
                ? transparency[value]
                : 255;
        produced += 4;
      }
    }
  }
  if (indices != NULL && palette != NULL) {
    return set_indexed(has_transparency ? transparency : NULL,
                       has_transparency ? transparency_length : 0, palette,
                       palette_length, indices, (size_t)(width * height), out);
  }
  return 1;
}

static int64_t png_sample(const uint8_t *rows, int64_t row, int64_t px,
                          int64_t channel, int64_t channels, int64_t bit_depth) {
  if (bit_depth == 8) return rows[row + px * channels + channel];
  int64_t bit = (px * channels + channel) * bit_depth;
  int64_t offset = row + bit / 8;
  if (bit_depth == 16) return get_u16_be(rows, (size_t)offset);
  return (rows[offset] >> (8 - bit_depth - bit % 8)) & ((1 << bit_depth) - 1);
}

static int png_general(const char *name, const uint8_t *raw,
                       const int64_t passes[][4], int pass_count, int64_t width,
                       int64_t height, int64_t bit_depth, int64_t color_type,
                       int64_t channels, int64_t bits_per_pixel,
                       int64_t byte_stride, int has_transparency,
                       const uint8_t *transparency, size_t transparency_length,
                       int64_t transparent_gray_unused, uint8_t *palette,
                       size_t palette_length, uint8_t *pixels, uint8_t *indices,
                       qa_png_image *out) {
  (void)transparent_gray_unused;
  int64_t raw_offset = 0;
  int64_t tg_gray = has_transparency && (color_type == 0 || color_type == 2)
                        ? (int64_t)get_u16_be(transparency, 0)
                        : -1;
  int64_t tg_green = has_transparency && color_type == 2
                         ? (int64_t)get_u16_be(transparency, 2)
                         : -1;
  int64_t tg_blue = has_transparency && color_type == 2
                        ? (int64_t)get_u16_be(transparency, 4)
                        : -1;
  for (int p = 0; p < pass_count; p++) {
    int64_t x0 = passes[p][0], y0 = passes[p][1];
    int64_t dx = passes[p][2], dy = passes[p][3];
    int64_t w = pass_size(width, x0, dx), h = pass_size(height, y0, dy);
    if (w == 0 || h == 0) continue;
    int64_t row_length = (w * bits_per_pixel + 7) / 8;
    uint8_t *rows = (uint8_t *)malloc((size_t)(row_length * h));
    if (rows == NULL) return 0;
    for (int64_t py = 0; py < h; py++) {
      int filter = raw[raw_offset++];
      int64_t row = py * row_length;
      if (filter > 4) {
        free(rows);
        qa_binary_error(name, 8, "PNG: unknown scanline filter %d", filter);
      }
      for (int64_t byte = 0; byte < row_length; byte++) {
        int a = byte >= byte_stride ? rows[row + byte - byte_stride] : 0;
        int b = py > 0 ? rows[row + byte - row_length] : 0;
        int c = py > 0 && byte >= byte_stride
                    ? rows[row + byte - row_length - byte_stride]
                    : 0;
        int prediction = filter == 0 ? 0
                         : filter == 1 ? a
                         : filter == 2 ? b
                         : filter == 3 ? (a + b) >> 1
                                       : paeth(a, b, c);
        rows[row + byte] = (uint8_t)(raw[raw_offset++] + prediction);
      }
      for (int64_t px = 0; px < w; px++) {
        int64_t index = (y0 + py * dy) * width + x0 + px * dx;
        int64_t produced = index * 4;
        if (indices != NULL && palette != NULL) {
          int64_t value = png_sample(rows, row, px, 0, channels, bit_depth);
          if ((size_t)value * 3 + 3 > palette_length) {
            free(rows);
            qa_binary_error(name, 8, "PNG: palette index out of range");
          }
          indices[index] = (uint8_t)(value & 0xff);
          if (value > 255) indices[index] = (uint8_t)value; /* Uint8Array store */
          pixels[produced] = palette[value * 3];
          pixels[produced + 1] = palette[value * 3 + 1];
          pixels[produced + 2] = palette[value * 3 + 2];
          pixels[produced + 3] =
              has_transparency && (size_t)value < transparency_length
                  ? transparency[value]
                  : 255;
        } else if (color_type == 0 || color_type == 4) {
          int64_t value = png_sample(rows, row, px, 0, channels, bit_depth);
          int64_t gray = bit_depth == 16 ? value >> 8
                         : bit_depth == 8 ? value
                                          : (int64_t)qa_js_round((double)value * 255.0 / ((1 << bit_depth) - 1));
          pixels[produced] = (uint8_t)gray;
          pixels[produced + 1] = (uint8_t)gray;
          pixels[produced + 2] = (uint8_t)gray;
          if (color_type == 4) {
            int64_t alpha_sample = png_sample(rows, row, px, 1, channels, bit_depth);
            int64_t alpha = bit_depth == 16 ? alpha_sample >> 8
                            : bit_depth == 8
                                ? alpha_sample
                                : (int64_t)qa_js_round((double)alpha_sample * 255.0 / ((1 << bit_depth) - 1));
            pixels[produced + 3] = (uint8_t)alpha;
          } else {
            pixels[produced + 3] =
                has_transparency && value == tg_gray ? 0 : 255;
          }
        } else {
          int64_t samples[4];
          for (int64_t ch = 0; ch < channels; ch++) {
            samples[ch] = png_sample(rows, row, px, ch, channels, bit_depth);
          }
          for (int ch = 0; ch < 3; ch++) {
            int64_t value = samples[ch];
            int64_t converted = bit_depth == 16 ? value >> 8
                                : bit_depth == 8
                                    ? value
                                    : (int64_t)qa_js_round((double)value * 255.0 / ((1 << bit_depth) - 1));
            pixels[produced + ch] = (uint8_t)converted;
          }
          if (color_type == 6) {
            int64_t value = samples[3];
            int64_t converted = bit_depth == 16 ? value >> 8
                                : bit_depth == 8
                                    ? value
                                    : (int64_t)qa_js_round((double)value * 255.0 / ((1 << bit_depth) - 1));
            pixels[produced + 3] = (uint8_t)converted;
          } else {
            pixels[produced + 3] = has_transparency && samples[0] == tg_gray &&
                                           samples[1] == tg_green &&
                                           samples[2] == tg_blue
                                       ? 0
                                       : 255;
          }
        }
      }
    }
    free(rows);
  }
  if (indices != NULL && palette != NULL) {
    return set_indexed(color_type == 3 && has_transparency ? transparency : NULL,
                       color_type == 3 && has_transparency ? transparency_length : 0,
                       palette, palette_length, indices,
                       (size_t)(width * height), out);
  }
  return 1;
}

static void free_owned3(void *raw) {
  uint8_t ***slots = (uint8_t ***)raw;
  for (int i = 0; i < 3; i++) free(*slots[i]);
}

typedef struct png_decode_guard {
  struct idat_part **idat;
  uint8_t **palette;
  uint8_t **transparency;
} png_decode_guard;

static void png_decode_guard_release(void *raw) {
  png_decode_guard *guard = (png_decode_guard *)raw;
  free(*guard->idat);
  free(*guard->palette);
  free(*guard->transparency);
}

static int png_expand(const char *name, const uint8_t *bytes,
                      const struct idat_part *idat, size_t idat_count,
                      size_t compressed_length, int64_t width, int64_t height,
                      int64_t bit_depth, int64_t color_type, int64_t interlace,
                      uint8_t *palette, size_t palette_length,
                      int has_transparency, const uint8_t *transparency,
                      size_t transparency_length, qa_png_image *out) {
  int64_t channels = color_type == 0 || color_type == 3 ? 1
                     : color_type == 2                   ? 3
                     : color_type == 4                   ? 2
                                                         : 4;
  int64_t bits_per_pixel = channels * bit_depth;
  int64_t byte_stride = (bits_per_pixel + 7) / 8;
  if (byte_stride < 1) byte_stride = 1;
  static const int64_t single_pass[1][4] = {{0, 0, 1, 1}};
  static const int64_t adam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8},
                                      {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2},
                                      {0, 1, 1, 2}};
  const int64_t(*passes)[4] = interlace == 0 ? single_pass : adam7;
  int pass_count = interlace == 0 ? 1 : 7;
  int64_t raw_length = 0;
  for (int p = 0; p < pass_count; p++) {
    int64_t w = pass_size(width, passes[p][0], passes[p][2]);
    int64_t h = pass_size(height, passes[p][1], passes[p][3]);
    if (w != 0 && h != 0) raw_length += ((w * bits_per_pixel + 7) / 8 + 1) * h;
  }
  uint8_t *compressed =
      compressed_length == 0 ? NULL : (uint8_t *)malloc(compressed_length);
  if (compressed_length != 0 && compressed == NULL) return 0;
  size_t destination = 0;
  for (size_t i = 0; i < idat_count; i++) {
    if (idat[i].length != 0) {
      memcpy(compressed + destination, bytes + idat[i].offset, idat[i].length);
      destination += idat[i].length;
    }
  }
  uint8_t *raw = raw_length == 0 ? NULL : (uint8_t *)malloc((size_t)raw_length);
  if (raw_length != 0 && raw == NULL) { free(compressed); return 0; }
  {
    z_stream stream;
    memset(&stream, 0, sizeof stream);
    stream.next_in = compressed;
    stream.avail_in = (uInt)compressed_length;
    stream.next_out = raw;
    stream.avail_out = (uInt)raw_length;
    int status = inflateInit(&stream);
    if (status == Z_OK) status = inflate(&stream, Z_NO_FLUSH);
    /* node inflateSync({ maxOutputLength }): one more byte of output past
     * the expected size is "Cannot create a Buffer larger than N bytes". */
    bool too_large = false;
    if (status == Z_OK && stream.avail_out == 0) {
      uint8_t extra = 0;
      stream.next_out = &extra;
      stream.avail_out = 1;
      status = inflate(&stream, Z_NO_FLUSH);
      if (stream.avail_out == 0) too_large = true;
    }
    uLong produced = stream.total_out;
    char message[160];
    /* Node's messages: zlib's own text, except an incomplete stream
     * ("unexpected end of file") and a preset dictionary. */
    if (too_large)
      snprintf(message, sizeof message, "Cannot create a Buffer larger than %lld bytes",
               (long long)raw_length);
    else if (status == Z_BUF_ERROR || status == Z_OK)
      snprintf(message, sizeof message, "unexpected end of file");
    else if (status == Z_NEED_DICT)
      snprintf(message, sizeof message, "Missing dictionary");
    else
      snprintf(message, sizeof message, "%s", stream.msg != NULL ? stream.msg : zError(status));
    if (status != Z_MEM_ERROR || produced != 0) inflateEnd(&stream);
    if (status != Z_STREAM_END || too_large) {
      free(compressed);
      free(raw);
      if (status == Z_MEM_ERROR) return 0;
      qa_binary_error(name, 8, "PNG inflate: %s", message);
    }
    if (produced != (uLong)raw_length) {
      free(compressed);
      free(raw);
      qa_binary_error(name, 8, "PNG: unexpected decompressed size");
    }
  }
  free(compressed);
  if (has_transparency &&
      ((color_type == 0 && transparency_length != 2) ||
       (color_type == 2 && transparency_length != 6) ||
       (color_type == 3 && transparency_length > palette_length / 3) ||
       color_type == 4 || color_type == 6)) {
    free(raw);
    qa_binary_error(name, 8, "PNG: invalid transparency chunk");
  }
  size_t pixel_count = (size_t)(width * height);
  uint8_t *pixels = (uint8_t *)malloc(pixel_count * 4);
  uint8_t *indices =
      color_type == 3 ? (uint8_t *)malloc(pixel_count) : NULL;
  if (pixels == NULL || (color_type == 3 && indices == NULL)) {
    free(pixels);
    free(indices);
    free(raw);
    return 0;
  }
  int64_t transparent_gray =
      has_transparency && color_type == 0 ? (int64_t)get_u16_be(transparency, 0) : -1;
  /* A BinaryError from the scanline loops must not leak the buffers. */
  uint8_t **owned_buffers[3] = {&raw, &pixels, &indices};
  qa_cleanup expand_guard;
  qa_cleanup_push(&expand_guard, free_owned3, owned_buffers);
  if (bit_depth == 8 && interlace == 0) {
    int result = png_fast(name, raw, width, height, color_type, channels,
                          has_transparency, transparency, transparency_length,
                          palette, palette_length, pixels, indices, out);
    qa_cleanup_pop(&expand_guard);
    free(raw);
    if (!result) { free(pixels); free(indices); return 0; }
    out->level.width = (double)width;
    out->level.height = (double)height;
    out->level.pixels.bytes = pixels;
    out->level.pixels.length = pixel_count * 4;
    return 1;
  }
  int result = png_general(name, raw, passes, pass_count, width, height,
                           bit_depth, color_type, channels, bits_per_pixel,
                           byte_stride, has_transparency, transparency,
                           transparency_length, transparent_gray, palette,
                           palette_length, pixels, indices, out);
  qa_cleanup_pop(&expand_guard);
  free(raw);
  if (!result) { free(pixels); free(indices); return 0; }
  out->level.width = (double)width;
  out->level.height = (double)height;
  out->level.pixels.bytes = pixels;
  out->level.pixels.length = pixel_count * 4;
  return 1;
}

int qa_decode_png(const uint8_t *bytes, size_t length, const char *source,
                  qa_png_image *out) {
  const char *name = source != NULL ? source : "<png>";
  memset(out, 0, sizeof *out);
  if (length < 33 || get_u32_be(bytes, 0) != 0x89504e47u ||
      get_u32_be(bytes, 4) != 0x0d0a1a0au) {
    qa_binary_error(name, 0, "PNG: bad signature");
  }
  int64_t width = 0, height = 0, bit_depth = 0, color_type = -1, interlace = 0;
  int has_gamma = 0, has_srgb = 0;
  double gamma = 0;
  int64_t srgb_intent = 0;
  uint8_t *palette = NULL;
  size_t palette_length = 0;
  uint8_t *transparency = NULL;
  size_t transparency_length = 0;
  int has_transparency = 0;
  int ended = 0;
  size_t compressed_length = 0;
  struct idat_part *idat = NULL;
  size_t idat_count = 0, idat_capacity = 0;
  png_decode_guard guard_state = {&idat, &palette, &transparency};
  qa_cleanup guard;
  qa_cleanup_push(&guard, png_decode_guard_release, &guard_state);
  for (size_t offset = 8; offset + 12 <= length;) {
    uint32_t chunk_length = get_u32_be(bytes, offset);
    size_t start = offset + 8;
    uint64_t end = (uint64_t)start + chunk_length;
    if (end + 4 > (uint64_t)length) {
      qa_binary_error(name, (int64_t)offset, "PNG: truncated chunk");
    }
    char type[5];
    memcpy(type, bytes + offset + 4, 4);
    type[4] = '\0';
    if (get_u32_be(bytes, (size_t)end) !=
        png_crc32(bytes + offset + 4, (size_t)(end - (offset + 4)))) {
      qa_binary_error(name, (int64_t)offset, "PNG: bad %s CRC", type);
    }
    if (strcmp(type, "IHDR") == 0) {
      if (offset != 8 || chunk_length != 13) {
        qa_binary_error(name, (int64_t)offset, "PNG: misplaced or malformed IHDR");
      }
      width = get_u32_be(bytes, start);
      height = get_u32_be(bytes, start + 4);
      bit_depth = bytes[start + 8];
      color_type = bytes[start + 9];
      interlace = bytes[start + 12];
      if (bytes[start + 10] != 0 || bytes[start + 11] != 0 || interlace > 1) {
        qa_binary_error(name, (int64_t)(start + 10),
                        "PNG: unsupported compression, filter or interlace method");
      }
      int valid = 0;
      if (color_type == 0) {
        valid = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
                bit_depth == 8 || bit_depth == 16;
      } else if (color_type == 3) {
        valid = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
      } else if (color_type == 2 || color_type == 4 || color_type == 6) {
        valid = bit_depth == 8 || bit_depth == 16;
      }
      if (!valid) {
        qa_binary_error(name, (int64_t)(start + 8),
                        "PNG: unsupported color type %lld at depth %lld",
                        (long long)color_type, (long long)bit_depth);
      }
    } else if (strcmp(type, "PLTE") == 0) {
      if (chunk_length == 0 || chunk_length > 768 || chunk_length % 3 != 0) {
        qa_binary_error(name, (int64_t)start, "PNG: invalid palette");
      }
      free(palette);
      palette = (uint8_t *)malloc(chunk_length);
      if (palette == NULL) goto oom;
      memcpy(palette, bytes + start, chunk_length);
      palette_length = chunk_length;
    } else if (strcmp(type, "tRNS") == 0) {
      free(transparency);
      transparency = chunk_length == 0 ? NULL : (uint8_t *)malloc(chunk_length);
      if (chunk_length != 0 && transparency == NULL) goto oom;
      if (chunk_length != 0) memcpy(transparency, bytes + start, chunk_length);
      transparency_length = chunk_length;
      has_transparency = 1;
    } else if (strcmp(type, "gAMA") == 0) {
      if (chunk_length != 4 || get_u32_be(bytes, start) == 0) {
        qa_binary_error(name, (int64_t)start, "PNG: invalid gamma");
      }
      gamma = (double)get_u32_be(bytes, start) / 100000.0;
      has_gamma = 1;
    } else if (strcmp(type, "sRGB") == 0) {
      if (chunk_length != 1 || bytes[start] > 3) {
        qa_binary_error(name, (int64_t)start, "PNG: invalid sRGB intent");
      }
      srgb_intent = bytes[start];
      has_srgb = 1;
    } else if (strcmp(type, "IDAT") == 0) {
      if (idat_count == idat_capacity) {
        size_t capacity = idat_capacity == 0 ? 8 : idat_capacity * 2;
        struct idat_part *grown =
            (struct idat_part *)realloc(idat, capacity * sizeof *grown);
        if (grown == NULL) goto oom;
        idat = grown;
        idat_capacity = capacity;
      }
      idat[idat_count].offset = start;
      idat[idat_count].length = chunk_length;
      idat_count++;
      compressed_length += chunk_length;
    } else if (strcmp(type, "IEND") == 0) {
      if (chunk_length != 0) {
        qa_binary_error(name, (int64_t)start, "PNG: invalid IEND");
      }
      ended = 1;
      break;
    } else if ((bytes[offset + 4] & 32) == 0) {
      qa_binary_error(name, (int64_t)offset, "PNG: unsupported critical chunk %s",
                      type);
    }
    offset = (size_t)end + 4;
  }
  if (!ended || width <= 0 || height <= 0 ||
      !qa_image_size_allowed((double)width, (double)height) || idat_count == 0) {
    qa_binary_error(name, 8, "PNG: missing chunks or invalid dimensions");
  }
  if (color_type == 3 && palette == NULL) {
    qa_binary_error(name, 8, "PNG: indexed image has no palette");
  }
  {
    int result = png_expand(name, bytes, idat, idat_count, compressed_length,
                            width, height, bit_depth, color_type, interlace,
                            palette, palette_length, has_transparency,
                            transparency, transparency_length, out);
    qa_cleanup_pop(&guard);
    free(idat);
    free(transparency);
    if (result) {
      out->bit_depth = bit_depth;
      out->color_type = color_type;
      out->has_gamma = has_gamma;
      out->gamma = gamma;
      out->has_srgb_intent = has_srgb;
      out->srgb_intent = srgb_intent;
      /* palette ownership moved into out when indexed; png_expand notes it */
      if (!out->has_indexed) free(palette);
    } else {
      free(palette);
    }
    return result;
  }
oom:
  qa_cleanup_pop(&guard);
  free(idat);
  free(palette);
  free(transparency);
  return 0;
}
