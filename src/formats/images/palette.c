/* Port of src/formats/images/palette.ts. GPL-2.0-or-later. */
#include "qa/formats/images/palette.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static int safe_integer(double value) {
  return isfinite(value) && trunc(value) == value &&
         value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

int qa_decode_palette(const uint8_t *bytes, size_t length,
                      qa_resolved_resource_reference source, qa_palette *out) {
  if (length != 768) qa_range_error("A Quake palette requires 256 RGB entries");
  uint8_t *colors = (uint8_t *)malloc(768);
  if (colors == NULL) return 0;
  memcpy(colors, bytes, 768);
  out->colors.bytes = colors;
  out->colors.length = 768;
  out->source = source;
  return 1;
}

void qa_palette_destroy(qa_palette *palette) {
  free((void *)palette->colors.bytes);
  palette->colors.bytes = NULL;
  palette->colors.length = 0;
}

int qa_decode_q1_colormap(const uint8_t *bytes, size_t length,
                          qa_q1_colormap *out) {
  if (length < 16385) {
    qa_range_error("Q1 colormap requires 64 lighting rows and a fullbright count");
  }
  uint8_t *levels = (uint8_t *)malloc(16384);
  if (levels == NULL) return 0;
  memcpy(levels, bytes, 16384);
  out->levels = levels;
  out->fullbright_first = 256 - (int64_t)bytes[16384];
  out->fullbright_last = 255;
  return 1;
}

void qa_q1_colormap_destroy(qa_q1_colormap *colormap) {
  free(colormap->levels);
  colormap->levels = NULL;
}

qa_indexed_render_image qa_indexed_render_image_make(
    const qa_image_level *levels, size_t level_count, qa_palette palette,
    qa_palette_transparency transparency,
    const qa_fullbright_range *fullbright, const qa_byte_view *translation) {
  if (palette.colors.length != 768) {
    qa_range_error("A Quake palette requires 256 RGB entries");
  }
  if (translation != NULL && translation->length != 256) {
    qa_range_error("Palette translation requires 256 entries");
  }
  for (size_t i = 0; i < level_count; i++) {
    const qa_image_level *level = &levels[i];
    if (!safe_integer(level->width) || !safe_integer(level->height) ||
        level->width <= 0 || level->height <= 0 ||
        (double)level->pixels.length != level->width * level->height) {
      qa_range_error("Indexed image dimensions do not match its indices");
    }
  }
  qa_indexed_render_image image;
  memset(&image, 0, sizeof image);
  image.kind = QA_RENDER_IMAGE_INDEXED8;
  image.levels = levels;
  image.level_count = level_count;
  image.palette = palette;
  image.transparency = transparency;
  image.fullbright = fullbright;
  image.translation = translation;
  return image;
}

uint8_t *qa_q1_player_translation(int64_t top_color, int64_t bottom_color) {
  if (top_color < 0 || top_color > 13 || bottom_color < 0 || bottom_color > 13) {
    qa_range_error("Q1 player colors must be in 0..13");
  }
  uint8_t *translation = (uint8_t *)malloc(256);
  if (translation == NULL) return NULL;
  for (int i = 0; i < 256; i++) translation[i] = (uint8_t)i;
  int64_t top = top_color * 16, bottom = bottom_color * 16;
  for (int64_t index = 0; index < 16; index++) {
    translation[16 + index] = (uint8_t)(top < 128 ? top + index : top + 15 - index);
    translation[96 + index] =
        (uint8_t)(bottom < 128 ? bottom + index : bottom + 15 - index);
  }
  return translation;
}

int qa_expand_indexed_image(const qa_indexed_render_image *image, int64_t level,
                            qa_palette_layer layer, qa_image_level *out) {
  if (level < 0 || (uint64_t)level >= (uint64_t)image->level_count) {
    qa_range_error("Missing image mip %lld", (long long)level);
  }
  const qa_image_level *indexed = &image->levels[level];
  size_t count = indexed->pixels.length;
  uint8_t *pixels = count == 0 ? NULL : (uint8_t *)malloc(count * 4);
  if (count != 0 && pixels == NULL) return 0;
  const uint8_t *colors = image->palette.colors.bytes;
  size_t color_length = image->palette.colors.length;
  for (size_t offset = 0; offset < count; offset++) {
    uint8_t original = indexed->pixels.bytes[offset];
    int64_t index = original;
    if (image->translation != NULL) {
      if ((size_t)original >= image->translation->length) {
        free(pixels);
        qa_range_error("Incomplete palette translation");
      }
      index = image->translation->bytes[original];
    }
    if ((size_t)(index * 3 + 2) >= color_length) {
      free(pixels);
      qa_range_error("Offset is outside the bounds of the DataView");
    }
    int transparent = image->transparency.kind != NULL &&
                      strcmp(image->transparency.kind, "opaque") != 0 &&
                      (double)original == image->transparency.index;
    int fullbright = image->fullbright != NULL &&
                     (double)index >= image->fullbright->first &&
                     (double)index <= image->fullbright->last;
    int visible = !transparent &&
                  (layer == QA_PALETTE_LAYER_COMBINED ||
                   (layer == QA_PALETTE_LAYER_FULLBRIGHT ? fullbright : !fullbright));
    pixels[offset * 4] = colors[index * 3];
    pixels[offset * 4 + 1] = colors[index * 3 + 1];
    pixels[offset * 4 + 2] = colors[index * 3 + 2];
    pixels[offset * 4 + 3] = visible ? 255 : 0;
  }
  out->width = indexed->width;
  out->height = indexed->height;
  out->pixels.bytes = pixels;
  out->pixels.length = count * 4;
  return 1;
}

void qa_build_gamma_table(const qa_gamma_profile *profile, uint8_t table[256]) {
  if (!isfinite(profile->gamma) || profile->gamma <= 0) {
    qa_range_error("Gamma must be positive and finite");
  }
  if (profile->kind == QA_GAMMA_Q3 &&
      (!isfinite(profile->overbright_bits) ||
       trunc(profile->overbright_bits) != profile->overbright_bits ||
       profile->overbright_bits < 0 || profile->overbright_bits > 2)) {
    qa_range_error("Q3 overbright bits must be in 0..2");
  }
  for (int index = 0; index < 256; index++) {
    double value;
    if (profile->kind == QA_GAMMA_Q1_GL) {
      float g = (float)profile->gamma;
      float p = (float)qa_js_pow((index + 1) / 256.0, (double)g);
      float m = (float)((double)p * 255.0);
      value = (double)(float)((double)m + 0.5);
    } else if (profile->gamma == 1) {
      value = index;
    } else if (profile->kind == QA_GAMMA_Q3) {
      float base = (float)(index / 255.0);
      float inv = (float)(1.0 / (double)(float)profile->gamma);
      value = 255.0 * qa_js_pow((double)base, (double)inv) + 0.5;
    } else {
      float g = (float)profile->gamma;
      value = (double)(float)(255.0 * qa_js_pow((index + 0.5) / 255.5, (double)g) + 0.5);
    }
    if (profile->kind == QA_GAMMA_Q3) {
      value = trunc(value) * qa_js_pow(2.0, profile->overbright_bits);
    }
    double clamped = trunc(value);
    if (!(clamped >= 0)) clamped = 0;
    if (clamped > 255) clamped = 255;
    table[index] = (uint8_t)clamped;
  }
}

/* TS WeakMap keyed by profile object. A C profile is a value that callers
 * reuse and mutate in place, so the cache is keyed by every field the
 * table reads: equal values always compose the same table, and a changed
 * profile at the same address is never served a stale table. */
struct gamma_cache_entry {
  qa_gamma_profile key;
  uint8_t table[256];
  int identity;
};
static struct gamma_cache_entry *gamma_cache;
static size_t gamma_cache_count, gamma_cache_capacity;

int qa_apply_image_gamma(const qa_image_level *level,
                         const qa_gamma_profile *profile, qa_image_level *out) {
  if ((double)level->pixels.length != level->width * level->height * 4.0) {
    qa_range_error("Gamma input must be RGBA8");
  }
  size_t length = level->pixels.length;
  uint8_t *pixels = length == 0 ? NULL : (uint8_t *)malloc(length);
  if (length != 0 && pixels == NULL) return 0;
  if (length != 0) memcpy(pixels, level->pixels.bytes, length);
  struct gamma_cache_entry *cached = NULL;
  for (size_t i = 0; i < gamma_cache_count; i++) {
    const qa_gamma_profile *key = &gamma_cache[i].key;
    if (key->kind == profile->kind && key->gamma == profile->gamma &&
        key->intensity == profile->intensity && key->overbright_bits == profile->overbright_bits &&
        key->only_gamma == profile->only_gamma)
      cached = &gamma_cache[i];
  }
  uint8_t local_table[256];
  int local_identity = 1;
  const uint8_t *composed = local_table;
  int identity;
  if (cached != NULL) {
    composed = cached->table;
    identity = cached->identity;
  } else {
    uint8_t table[256];
    qa_build_gamma_table(profile, table);
    /* Math.max propagates NaN, unlike fmax. */
    double intensity = profile->kind == QA_GAMMA_Q1_GL || profile->only_gamma
                           ? 1.0
                           : profile->intensity > 1.0 ? profile->intensity
                               : profile->intensity == profile->intensity ? 1.0
                                                                           : NAN;
    if (!isfinite(intensity)) {
      free(pixels);
      qa_range_error("Intensity must be finite");
    }
    float fi = (float)intensity;
    identity = 1;
    for (int value = 0; value < 256; value++) {
      float scaled = (float)((double)value * (double)fi);
      double t = trunc((double)scaled);
      int64_t at = t >= 255.0 ? 255 : t <= 0.0 ? 0 : (int64_t)t;
      uint8_t corrected = table[at];
      local_table[value] = corrected;
      if (corrected != value) identity = 0;
    }
    local_identity = identity;
    if (gamma_cache_count == gamma_cache_capacity) {
      size_t capacity = gamma_cache_capacity == 0 ? 8 : gamma_cache_capacity * 2;
      struct gamma_cache_entry *grown =
          (struct gamma_cache_entry *)realloc(gamma_cache, capacity * sizeof *grown);
      if (grown != NULL) {
        gamma_cache = grown;
        gamma_cache_capacity = capacity;
      }
    }
    if (gamma_cache_count < gamma_cache_capacity) {
      struct gamma_cache_entry *entry = &gamma_cache[gamma_cache_count++];
      entry->key = *profile;
      memcpy(entry->table, local_table, sizeof entry->table);
      entry->identity = local_identity;
    }
  }
  if (!identity) {
    for (size_t offset = 0; offset < length; offset += 4) {
      pixels[offset] = composed[pixels[offset]];
      pixels[offset + 1] = composed[pixels[offset + 1]];
      pixels[offset + 2] = composed[pixels[offset + 2]];
    }
  }
  out->width = level->width;
  out->height = level->height;
  out->pixels.bytes = pixels;
  out->pixels.length = length;
  return 1;
}
