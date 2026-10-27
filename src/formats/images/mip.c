/* Port of src/formats/images/mip.ts. GPL-2.0-or-later. */
#include "qa/formats/images/mip.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_image_level_destroy(qa_image_level *level) {
  free((void *)level->pixels.bytes);
  level->pixels.bytes = NULL;
  level->pixels.length = 0;
}

static int safe_integer(double value) {
  return isfinite(value) && trunc(value) == value &&
         value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

static void image_bytes(const qa_image_level *image) {
  if (!safe_integer(image->width) || !safe_integer(image->height) ||
      image->width <= 0 || image->height <= 0 ||
      (double)image->pixels.length != image->width * image->height * 4.0) {
    qa_range_error("Expected a complete RGBA8 image");
  }
}

/* JS `bytes[i] ?? 0`: out-of-range typed-array loads yield 0 here. */
static uint8_t at0(const qa_image_level *image, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)image->pixels.length) return 0;
  return image->pixels.bytes[index];
}

/* JS ToUint32 then >>> shift. */
static uint32_t shr_u32(double value, int shift) {
  double m = fmod(trunc(value), 4294967296.0);
  if (!(m >= 0)) m += 4294967296.0;
  if (!(m >= 0)) m = 0;
  return (uint32_t)m >> shift;
}

static int make_level(int64_t width, int64_t height, qa_image_level *out,
                      uint8_t **pixels) {
  size_t length = (size_t)(width * height * 4);
  *pixels = length == 0 ? NULL : (uint8_t *)malloc(length);
  if (length != 0 && *pixels == NULL) return 0;
  out->width = (double)width;
  out->height = (double)height;
  out->pixels.bytes = *pixels;
  out->pixels.length = length;
  return 1;
}

int qa_resample_image(const qa_image_level *input, double width, double height,
                      qa_resample_profile profile, qa_image_level *out) {
  image_bytes(input);
  if (!safe_integer(width) || !safe_integer(height) || width <= 0 || height <= 0 ||
      width * height * 4.0 > 2147483647.0) {
    qa_range_error("Invalid resampled dimensions");
  }
  int64_t w = (int64_t)width, h = (int64_t)height;
  int64_t iw = (int64_t)input->width, ih = (int64_t)input->height;
  uint8_t *pixels;
  if (!make_level(w, h, out, &pixels)) return 0;
  double step = trunc(input->width * 65536.0 / width);
  for (int64_t y = 0; y < h; y++) {
    for (int64_t x = 0; x < w; x++) {
      int64_t o = (y * w + x) * 4;
      if (profile == QA_RESAMPLE_Q1) {
        int64_t sx = (int64_t)shr_u32(floor(step / 2.0) + (double)x * step, 16);
        if (sx > iw - 1) sx = iw - 1;
        int64_t sy = (int64_t)floor((double)y * (double)ih / (double)h);
        int64_t s = (sy * iw + sx) * 4;
        for (int c = 0; c < 4; c++) pixels[o + c] = at0(input, s + c);
      } else {
        int64_t x1 = (int64_t)shr_u32(floor(step / 4.0) + (double)x * step, 16);
        int64_t x2 =
            (int64_t)shr_u32(3.0 * floor(step / 4.0) + (double)x * step, 16);
        int64_t y1 = (int64_t)floor(((double)y + 0.25) * (double)ih / (double)h);
        int64_t y2 = (int64_t)floor(((double)y + 0.75) * (double)ih / (double)h);
        int64_t s11 = (y1 * iw + x1) * 4, s12 = (y1 * iw + x2) * 4;
        int64_t s21 = (y2 * iw + x1) * 4, s22 = (y2 * iw + x2) * 4;
        for (int c = 0; c < 4; c++) {
          pixels[o + c] = (uint8_t)(((int)at0(input, s11 + c) +
                                     (int)at0(input, s12 + c) +
                                     (int)at0(input, s21 + c) +
                                     (int)at0(input, s22 + c)) >>
                                    2);
        }
      }
    }
  }
  return 1;
}

int qa_mip_image(const qa_image_level *input, qa_mip_profile profile,
                 qa_image_level *out) {
  image_bytes(input);
  int64_t iw = (int64_t)input->width, ih = (int64_t)input->height;
  int64_t w = iw >> 1, h = ih >> 1;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (iw == 1 && ih == 1) {
    uint8_t *pixels;
    if (!make_level(w, h, out, &pixels)) return 0;
    memcpy(pixels, input->pixels.bytes, input->pixels.length);
    return 1;
  }
  if (profile == QA_MIP_Q3_WEIGHTED) {
    if ((iw & (iw - 1)) != 0 || (ih & (ih - 1)) != 0) {
      qa_range_error("Q3 weighted mipmaps require power-of-two dimensions");
    }
    if (iw == 1 || ih == 1) {
      uint8_t *pixels;
      if (!make_level(w, h, out, &pixels)) return 0;
      memcpy(pixels, input->pixels.bytes, (size_t)(w * h * 4));
      return 1;
    }
  }
  uint8_t *pixels;
  if (!make_level(w, h, out, &pixels)) return 0;
  if (profile == QA_MIP_Q3_WEIGHTED) {
    int64_t wrap_x = iw - 1, wrap_y = ih - 1;
    for (int64_t y = 0; y < h; y++) {
      for (int64_t x = 0; x < w; x++) {
        int64_t o = (y * w + x) * 4;
        for (int c = 0; c < 4; c++) {
          int value = 0;
          for (int dy = -1; dy <= 2; dy++) {
            for (int dx = -1; dx <= 2; dx++) {
              int weight = (dy == -1 || dy == 2 ? 1 : 2) * (dx == -1 || dx == 2 ? 1 : 2);
              int64_t s = (((y * 2 + dy) & wrap_y) * iw + ((x * 2 + dx) & wrap_x)) * 4 + c;
              value += weight * (int)at0(input, s);
            }
          }
          pixels[o + c] = (uint8_t)(value / 36);
        }
      }
    }
  } else {
    for (int64_t y = 0; y < h; y++) {
      int64_t row2_y = y * 2 + 1 < ih - 1 ? y * 2 + 1 : ih - 1;
      int64_t row1 = y * 2 * iw * 4, row2 = row2_y * iw * 4, out_row = y * w * 4;
      for (int64_t x = 0; x < w; x++) {
        int64_t o = out_row + x * 4, s1 = row1 + x * 8, s2 = row2 + x * 8;
        int64_t step = x * 2 + 1 < iw ? 4 : 0;
        for (int c = 0; c < 4; c++) {
          pixels[o + c] = (uint8_t)(((int)at0(input, s1 + c) +
                                     (int)at0(input, s1 + step + c) +
                                     (int)at0(input, s2 + c) +
                                     (int)at0(input, s2 + step + c)) >>
                                    2);
        }
      }
    }
  }
  return 1;
}

int qa_generate_mip_chain(const qa_image_level *input, qa_mip_profile profile,
                          qa_mip_chain *out) {
  size_t capacity = 8, count = 0;
  qa_image_level *levels = (qa_image_level *)malloc(capacity * sizeof *levels);
  if (levels == NULL) return 0;
  levels[count++] = *input;
  const qa_image_level *level = input;
  while (level->width > 1 || level->height > 1) {
    qa_image_level next;
    if (!qa_mip_image(level, profile, &next)) {
      for (size_t i = 1; i < count; i++) qa_image_level_destroy(&levels[i]);
      free(levels);
      return 0;
    }
    if (count == capacity) {
      capacity *= 2;
      qa_image_level *grown =
          (qa_image_level *)realloc(levels, capacity * sizeof *grown);
      if (grown == NULL) {
        qa_image_level_destroy(&next);
        for (size_t i = 1; i < count; i++) qa_image_level_destroy(&levels[i]);
        free(levels);
        return 0;
      }
      levels = grown;
    }
    levels[count++] = next;
    level = &levels[count - 1];
  }
  out->levels = levels;
  out->count = count;
  return 1;
}

void qa_mip_chain_destroy(qa_mip_chain *chain) {
  for (size_t i = 1; i < chain->count; i++) qa_image_level_destroy(&chain->levels[i]);
  free(chain->levels);
  chain->levels = NULL;
  chain->count = 0;
}
