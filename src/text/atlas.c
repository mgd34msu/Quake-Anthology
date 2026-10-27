/* Port of src/text/atlas.ts. GPL-2.0-or-later. */
#include "qa/text/atlas.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/images/png.h"
#include "qa/formats/images/tga.h"
#include "qa/foundation/support.h"
#include "qa/text/kfont.h"
#include "qa/text/truetype.h"

/* TS `throw new Error(msg)`, as in src/core/commands/index.c. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

/* JS ToInt32 (bitwise operators in resolveTextGlyph). */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  if (value < 0) bits = ~bits + 1u;
  return (int32_t)bits;
}

static bool is_integer(double value) {
  return isfinite(value) && trunc(value) == value;
}

static bool glyph_map_set(qa_text_atlas *atlas, double codepoint,
                          qa_atlas_glyph glyph) {
  for (size_t i = 0; i < atlas->glyph_count; i++) {
    if (atlas->glyphs[i].codepoint == codepoint) {
      atlas->glyphs[i].glyph = glyph;
      return true;
    }
  }
  if (atlas->glyph_count == atlas->glyph_capacity) {
    size_t grown = atlas->glyph_capacity == 0 ? 16 : atlas->glyph_capacity * 2;
    qa_text_glyph_entry *glyphs =
        (qa_text_glyph_entry *)realloc(atlas->glyphs, grown * sizeof *glyphs);
    if (glyphs == NULL) return false;
    atlas->glyphs = glyphs;
    atlas->glyph_capacity = grown;
  }
  atlas->glyphs[atlas->glyph_count].codepoint = codepoint;
  atlas->glyphs[atlas->glyph_count].glyph = glyph;
  atlas->glyph_count++;
  return true;
}

void qa_text_atlas_destroy(qa_text_atlas *atlas) {
  free(atlas->name);
  free(atlas->glyphs);
  memset(atlas, 0, sizeof *atlas);
}

const qa_atlas_glyph *qa_text_atlas_glyph(const qa_text_atlas *atlas,
                                          double codepoint) {
  for (size_t i = 0; i < atlas->glyph_count; i++) {
    if (atlas->glyphs[i].codepoint == codepoint) return &atlas->glyphs[i].glyph;
  }
  return NULL;
}

bool qa_classic_charset(qa_renderer_image image, const char *name,
                        bool color_baked, qa_text_atlas *out) {
  memset(out, 0, sizeof *out);
  if (fmod(image.width, 16) != 0 || fmod(image.height, 16) != 0) {
    qa_range_error("Charset needs 16 by 16 glyph cells");
  }
  const char *atlas_name = name != NULL ? name : "conchars";
  double width = image.width / 16;
  double height = image.height / 16;
  out->name = dup_text(atlas_name);
  if (out->name == NULL) return false;
  for (int code = 0; code < 256; code++) {
    qa_atlas_glyph glyph = {(code & 15) * width, (code >> 4) * height,
                            width,           height,
                            width,           color_baked};
    if (!glyph_map_set(out, code, glyph)) {
      qa_text_atlas_destroy(out);
      return false;
    }
  }
  out->kind = QA_TEXT_ATLAS_CLASSIC;
  out->picture.kind = QA_PICTURE_IMAGE;
  out->picture.image.name = out->name;
  out->picture.image.image = image;
  out->line_height = height;
  return true;
}

/* --- registry internals --- */

static qa_font_cache_entry *cache_find(qa_text_font_registry *registry,
                                       const char *key) {
  for (size_t i = 0; i < registry->cache_count; i++) {
    if (strcmp(registry->cache[i].key, key) == 0) return &registry->cache[i];
  }
  return NULL;
}

/* Takes ownership of key and font. False only on OOM. */
static bool cache_store(qa_text_font_registry *registry, char *key,
                        qa_text_atlas *font) {
  if (registry->cache_count == registry->cache_capacity) {
    size_t grown =
        registry->cache_capacity == 0 ? 8 : registry->cache_capacity * 2;
    qa_font_cache_entry *cache = (qa_font_cache_entry *)realloc(
        registry->cache, grown * sizeof *cache);
    if (cache == NULL) return false;
    registry->cache = cache;
    registry->cache_capacity = grown;
  }
  registry->cache[registry->cache_count].key = key;
  registry->cache[registry->cache_count].font = font;
  registry->cache[registry->cache_count].is_null = font == NULL;
  registry->cache_count++;
  return true;
}

static bool track_image(qa_text_font_registry *registry,
                        qa_renderer_image image) {
  if (registry->image_count == registry->image_capacity) {
    size_t grown =
        registry->image_capacity == 0 ? 8 : registry->image_capacity * 2;
    qa_renderer_image *images = (qa_renderer_image *)realloc(
        registry->images, grown * sizeof *images);
    if (images == NULL) return false;
    registry->images = images;
    registry->image_capacity = grown;
  }
  registry->images[registry->image_count++] = image;
  return true;
}

static void require_open(qa_text_font_registry *registry) {
  if (registry->closed) qa_error("Text font registry is closed");
}

/* TS register(): builds the TextAtlas into *out (caller-owned until
 * stored in the cache). False only on OOM. */
static bool register_atlas(qa_text_font_registry *registry,
                           qa_text_atlas_kind kind, const char *name,
                           const qa_image_level *level, double line_height,
                           qa_text_atlas *out) {
  require_open(registry);
  qa_render_image content = {0};
  content.kind = QA_RENDER_IMAGE_RGBA8;
  content.levels = level;
  content.level_count = 1;
  content.border_color = (qa_vec4){0, 0, 0, 0};
  qa_renderer_image image =
      registry->services.register_image(registry->services.ctx, name, &content);
  /* Loads are synchronous, so the TS closed-during-registration race
   * cannot occur; the closed checks above keep the same messages. */
  if (!track_image(registry, image)) {
    registry->services.release_image(registry->services.ctx, image);
    return false;
  }
  out->name = dup_text(name);
  if (out->name == NULL) return false;
  out->kind = kind;
  out->picture.kind = QA_PICTURE_IMAGE;
  out->picture.image.name = out->name;
  out->picture.image.image = image;
  out->line_height = line_height;
  static const double CAP_CODES[] = {72, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57};
  double top = line_height;
  double bottom = -1;
  for (size_t c = 0; c < sizeof CAP_CODES / sizeof CAP_CODES[0]; c++) {
    const qa_atlas_glyph *glyph = qa_text_atlas_glyph(out, CAP_CODES[c]);
    if (glyph == NULL) continue;
    for (double y = 0; y < glyph->height; y++) {
      for (double x = 0; x < glyph->width; x++) {
        double flat = ((glyph->y + y) * level->width + glyph->x + x) * 4 + 3;
        size_t index = flat >= 0 ? (size_t)flat : level->pixels.length;
        /* Out-of-range typed-array loads read as undefined (TS ?? 0). */
        int alpha = index < level->pixels.length ? level->pixels.bytes[index] : 0;
        if (alpha > 127) {
          if (y < top) top = y;
          if (y > bottom) bottom = y;
        }
      }
    }
  }
  if (!(bottom < top)) {
    out->has_cap_ink = true;
    out->cap_top = top * 8 / line_height;
    out->cap_height = (bottom - top + 1) * 8 / line_height;
  }
  return true;
}

static bool ends_with_png_lower(const char *token) {
  size_t length = strlen(token);
  if (length < 4) return false;
  const char *suffix = token + length - 4;
  return suffix[0] == '.' && tolower((unsigned char)suffix[1]) == 'p' &&
         tolower((unsigned char)suffix[2]) == 'n' &&
         tolower((unsigned char)suffix[3]) == 'g';
}

static char *join_key(const char *prefix, const char *path) {
  size_t need = strlen(prefix) + strlen(path) + 1;
  char *key = (char *)malloc(need);
  if (key != NULL) snprintf(key, need, "%s%s", prefix, path);
  return key;
}

/* number -> JS decimal string for the small integral values used in
 * keys and messages; falls back to %g. */
static void format_number(char *buffer, size_t size, double value) {
  if (is_integer(value) && fabs(value) <= 9007199254740991.0) {
    snprintf(buffer, size, "%lld", (long long)value);
  } else {
    snprintf(buffer, size, "%.17g", value);
  }
}

const qa_text_atlas *qa_text_font_registry_load_kfont(
    qa_text_font_registry *registry, const char *path, bool *oom) {
  *oom = false;
  require_open(registry);
  char *key = join_key("kfont:", path);
  if (key == NULL) {
    *oom = true;
    return NULL;
  }
  qa_font_cache_entry *existing = cache_find(registry, key);
  if (existing != NULL) {
    const qa_text_atlas *font = existing->font;
    free(key);
    return font;
  }
  qa_text_atlas *result = NULL;
  size_t length = 0;
  uint8_t *bytes = registry->services.read(registry->services.ctx, path, &length);
  if (bytes != NULL) {
    /* TextDecoder output: the kfont grammar is ASCII; pass bytes on a
     * NUL-terminated copy (invalid UTF-8 is not replaced, see header). */
    char *text = (char *)malloc(length + 1);
    if (text == NULL) {
      free(bytes);
      free(key);
      *oom = true;
      return NULL;
    }
    memcpy(text, bytes, length);
    text[length] = '\0';
    free(bytes);
    qa_parsed_kfont parsed;
    bool parsed_ok = qa_parse_kfont(text, &parsed);
    free(text);
    if (parsed_ok) {
      size_t bitmap_length = 0;
      uint8_t *bitmap = registry->services.read(
          registry->services.ctx, parsed.texture_token, &bitmap_length);
      if (bitmap != NULL) {
        qa_png_image png = {0};
        qa_tga_image tga = {0};
        qa_image_level level;
        int decoded;
        if (ends_with_png_lower(parsed.texture_token)) {
          decoded = qa_decode_png(bitmap, bitmap_length, parsed.texture_token, &png);
          level = png.level;
        } else {
          decoded = qa_decode_tga(bitmap, bitmap_length, parsed.texture_token, &tga);
          level = tga.level;
        }
        free(bitmap);
        if (decoded == 0) {
          qa_parsed_kfont_destroy(&parsed);
          free(key);
          *oom = true;
          return NULL;
        }
        result = (qa_text_atlas *)calloc(1, sizeof *result);
        if (result == NULL) {
          qa_png_image_destroy(&png);
          qa_tga_image_destroy(&tga);
          qa_parsed_kfont_destroy(&parsed);
          free(key);
          *oom = true;
          return NULL;
        }
        for (size_t i = 0; i < parsed.glyph_count; i++) {
          double code = parsed.glyphs[i].codepoint;
          const qa_kfont_char *glyph = &parsed.glyphs[i].glyph;
          if (glyph->x < 0 || glyph->y < 0 || glyph->w < 0 || glyph->h < 0 ||
              glyph->x + glyph->w > level.width ||
              glyph->y + glyph->h > level.height) {
            char code_text[32];
            format_number(code_text, sizeof code_text, code);
            qa_range_error("Kfont glyph %s exceeds %s", code_text,
                           parsed.texture_token);
          }
          qa_atlas_glyph entry = {glyph->x, glyph->y, glyph->w, glyph->h,
                                  glyph->w, false};
          if (!glyph_map_set(result, code, entry)) {
            qa_text_atlas_destroy(result);
            free(result);
            qa_png_image_destroy(&png);
            qa_tga_image_destroy(&tga);
            qa_parsed_kfont_destroy(&parsed);
            free(key);
            *oom = true;
            return NULL;
          }
        }
        qa_text_atlas staged = *result;
        memset(result, 0, sizeof *result);
        result->glyphs = staged.glyphs;
        result->glyph_count = staged.glyph_count;
        result->glyph_capacity = staged.glyph_capacity;
        if (!register_atlas(registry, QA_TEXT_ATLAS_KFONT, path, &level,
                            parsed.line_height, result)) {
          qa_text_atlas_destroy(result);
          free(result);
          result = NULL;
          qa_png_image_destroy(&png);
          qa_tga_image_destroy(&tga);
          qa_parsed_kfont_destroy(&parsed);
          free(key);
          *oom = true;
          return NULL;
        }
        qa_png_image_destroy(&png);
        qa_tga_image_destroy(&tga);
      }
      qa_parsed_kfont_destroy(&parsed);
    }
  }
  if (!cache_store(registry, key, result)) {
    free(key);
    if (result != NULL) {
      qa_text_atlas_destroy(result);
      free(result);
    }
    *oom = true;
    return NULL;
  }
  return result;
}

static void validate_pixel_size(double pixel_size) {
  if (!is_integer(pixel_size) || pixel_size <= 0 || pixel_size > 1024) {
    qa_range_error("Invalid TrueType pixel size");
  }
}

static void validate_scalar(double code) {
  if (!is_integer(code) || code < 0 || code > 0x10ffff ||
      (code >= 0xd800 && code <= 0xdfff)) {
    qa_range_error("Font coverage must contain Unicode scalar values");
  }
}

static int compare_doubles(const void *left, const void *right) {
  double a = *(const double *)left;
  double b = *(const double *)right;
  return (a > b) - (a < b);
}

/* Set semantics: distinct values in first-appearance order. */
static double *distinct_doubles(const double *values, size_t count,
                                size_t *out_count) {
  double *out = count != 0 ? (double *)malloc(count * sizeof *out) : NULL;
  if (count != 0 && out == NULL) return NULL;
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    bool seen = false;
    for (size_t j = 0; j < kept; j++) {
      if (out[j] == values[i]) seen = true;
    }
    if (!seen) out[kept++] = values[i];
  }
  *out_count = kept;
  return out;
}

static char *coverage_key(const char *prefix, const char *path,
                          double pixel_size, const double *coverage,
                          size_t count) {
  char size_text[32];
  format_number(size_text, sizeof size_text, pixel_size);
  size_t need = strlen(prefix) + strlen(path) + strlen(size_text) + 3;
  for (size_t i = 0; i < count; i++) {
    char code_text[32];
    format_number(code_text, sizeof code_text, coverage[i]);
    need += strlen(code_text) + 1;
  }
  char *key = (char *)malloc(need);
  if (key == NULL) return NULL;
  size_t used = (size_t)snprintf(key, need, "%s%s:%s:", prefix, path, size_text);
  for (size_t i = 0; i < count; i++) {
    char code_text[32];
    format_number(code_text, sizeof code_text, coverage[i]);
    used += (size_t)snprintf(key + used, need - used, "%s%s", i != 0 ? "," : "",
                             code_text);
  }
  return key;
}

static bool atlas_from_tt(qa_text_font_registry *registry, const char *name,
                          qa_parsed_font *font, const double *codes,
                          size_t count, double pixel_size,
                          qa_text_atlas **out_font) {
  *out_font = NULL;
  int64_t *coverage = count != 0 ? (int64_t *)malloc(count * sizeof *coverage)
                                 : NULL;
  if (count != 0 && coverage == NULL) return false;
  for (size_t i = 0; i < count; i++) coverage[i] = (int64_t)codes[i];
  qa_font_atlas built = {0};
  bool built_ok = qa_truetype_build_font_atlas(font, coverage, count, pixel_size,
                                               512, &built);
  free(coverage);
  if (!built_ok) return false;
  qa_text_atlas *result = (qa_text_atlas *)calloc(1, sizeof *result);
  if (result == NULL) {
    qa_font_atlas_destroy(&built);
    return false;
  }
  for (size_t i = 0; i < built.glyph_count; i++) {
    const qa_atlas_rect *glyph = &built.glyphs[i];
    qa_atlas_glyph entry = {(double)glyph->x, (double)glyph->y,
                            (double)glyph->w, (double)glyph->h,
                            (double)glyph->w, glyph->color};
    if (!glyph_map_set(result, (double)built.codepoints[i], entry)) {
      qa_text_atlas_destroy(result);
      free(result);
      qa_font_atlas_destroy(&built);
      return false;
    }
  }
  qa_image_level level = {(double)built.width, (double)built.height,
                          {built.pixels, built.pixels_length}};
  qa_text_glyph_entry *glyphs = result->glyphs;
  size_t glyph_count = result->glyph_count;
  size_t glyph_capacity = result->glyph_capacity;
  memset(result, 0, sizeof *result);
  result->glyphs = glyphs;
  result->glyph_count = glyph_count;
  result->glyph_capacity = glyph_capacity;
  bool registered = register_atlas(registry, QA_TEXT_ATLAS_TRUETYPE, name, &level,
                                   (double)built.line_height, result);
  qa_font_atlas_destroy(&built);
  if (!registered) {
    qa_text_atlas_destroy(result);
    free(result);
    return false;
  }
  *out_font = result;
  return true;
}

const qa_text_atlas *qa_text_font_registry_load_truetype(
    qa_text_font_registry *registry, const char *path, double pixel_size,
    const double *codepoints, size_t codepoint_count, bool *oom) {
  *oom = false;
  validate_pixel_size(pixel_size);
  double *wanted = (double *)malloc((codepoint_count + 2) * sizeof *wanted);
  if (wanted == NULL) {
    *oom = true;
    return NULL;
  }
  if (codepoint_count != 0) {
    memcpy(wanted, codepoints, codepoint_count * sizeof *wanted);
  }
  wanted[codepoint_count] = 32;
  wanted[codepoint_count + 1] = 63;
  size_t coverage_count = 0;
  double *coverage =
      distinct_doubles(wanted, codepoint_count + 2, &coverage_count);
  free(wanted);
  if (coverage == NULL) {
    *oom = true;
    return NULL;
  }
  qsort(coverage, coverage_count, sizeof *coverage, compare_doubles);
  for (size_t i = 0; i < coverage_count; i++) validate_scalar(coverage[i]);
  char *key = coverage_key("truetype:", path, pixel_size, coverage, coverage_count);
  if (key == NULL) {
    free(coverage);
    *oom = true;
    return NULL;
  }
  require_open(registry);
  qa_font_cache_entry *existing = cache_find(registry, key);
  if (existing != NULL) {
    const qa_text_atlas *font = existing->font;
    free(key);
    free(coverage);
    return font;
  }
  qa_text_atlas *result = NULL;
  size_t length = 0;
  uint8_t *bytes = registry->services.read(registry->services.ctx, path, &length);
  if (bytes != NULL) {
    qa_parse_font_result parsed = qa_truetype_parse_font(bytes, length);
    free(bytes);
    if (!parsed.ok) {
      qa_error("%s: %s", path, parsed.reason != NULL ? parsed.reason : "");
    }
    bool built = atlas_from_tt(registry, key, parsed.font, coverage,
                               coverage_count, pixel_size, &result);
    qa_parse_font_result_destroy(&parsed);
    if (!built) {
      free(key);
      free(coverage);
      *oom = true;
      return NULL;
    }
  }
  free(coverage);
  if (!cache_store(registry, key, result)) {
    free(key);
    if (result != NULL) {
      qa_text_atlas_destroy(result);
      free(result);
    }
    *oom = true;
    return NULL;
  }
  return result;
}

bool qa_text_font_registry_load_truetype_pages(
    qa_text_font_registry *registry, const char *path, double pixel_size,
    const double *codepoints, size_t codepoint_count,
    const qa_text_atlas ***out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  validate_pixel_size(pixel_size);
  size_t length = 0;
  uint8_t *bytes = registry->services.read(registry->services.ctx, path, &length);
  if (bytes == NULL) return true;
  qa_parse_font_result parsed = qa_truetype_parse_font(bytes, length);
  free(bytes);
  if (!parsed.ok) qa_error("%s: %s", path, parsed.reason != NULL ? parsed.reason : "");
  size_t distinct_count = 0;
  double *distinct = distinct_doubles(codepoints, codepoint_count, &distinct_count);
  if (codepoint_count != 0 && distinct == NULL) {
    qa_parse_font_result_destroy(&parsed);
    return false;
  }
  double *coverage = distinct_count != 0
                         ? (double *)malloc(distinct_count * sizeof *coverage)
                         : NULL;
  if (distinct_count != 0 && coverage == NULL) {
    free(distinct);
    qa_parse_font_result_destroy(&parsed);
    return false;
  }
  size_t coverage_count = 0;
  for (size_t i = 0; i < distinct_count; i++) {
    validate_scalar(distinct[i]);
    if (qa_parsed_font_cmap_lookup(parsed.font, (int64_t)distinct[i]) != 0) {
      coverage[coverage_count++] = distinct[i];
    }
  }
  free(distinct);
  size_t page_total = (coverage_count + 127) / 128;
  const qa_text_atlas **pages = page_total != 0
      ? (const qa_text_atlas **)malloc(page_total * sizeof *pages)
      : NULL;
  if (page_total != 0 && pages == NULL) {
    free(coverage);
    qa_parse_font_result_destroy(&parsed);
    return false;
  }
  size_t page_count = 0;
  for (size_t offset = 0; offset < coverage_count; offset += 128) {
    size_t slice = coverage_count - offset < 128 ? coverage_count - offset : 128;
    char *key = coverage_key("truetype-page:", path, pixel_size,
                             coverage + offset, slice);
    if (key == NULL) {
      free(pages);
      free(coverage);
      qa_parse_font_result_destroy(&parsed);
      return false;
    }
    require_open(registry);
    qa_font_cache_entry *existing = cache_find(registry, key);
    const qa_text_atlas *page = NULL;
    if (existing != NULL) {
      page = existing->font;
      free(key);
    } else {
      qa_text_atlas *built_font = NULL;
      if (!atlas_from_tt(registry, key, parsed.font, coverage + offset, slice,
                         pixel_size, &built_font)) {
        free(key);
        free(pages);
        free(coverage);
        qa_parse_font_result_destroy(&parsed);
        return false;
      }
      if (!cache_store(registry, key, built_font)) {
        free(key);
        if (built_font != NULL) {
          qa_text_atlas_destroy(built_font);
          free(built_font);
        }
        free(pages);
        free(coverage);
        qa_parse_font_result_destroy(&parsed);
        return false;
      }
      page = built_font;
    }
    if (page != NULL) pages[page_count++] = page;
  }
  free(coverage);
  qa_parse_font_result_destroy(&parsed);
  *out = pages;
  *out_count = page_count;
  return true;
}

void qa_text_font_registry_init(qa_text_font_registry *registry,
                                qa_font_image_services services) {
  memset(registry, 0, sizeof *registry);
  registry->services = services;
}

bool qa_text_font_registry_select(qa_text_font_registry *registry,
                                  const qa_text_font_request *request,
                                  const qa_text_atlas *classic,
                                  qa_text_font_selection *out) {
  memset(out, 0, sizeof *out);
  out->classic = classic;
  bool oom = false;
  if (request->kind == QA_FONT_REQUEST_CLASSIC) {
    out->is_atlas = false;
    out->unicode = request->unicode_font == NULL
                       ? NULL
                       : qa_text_font_registry_load_kfont(
                             registry, request->unicode_font, &oom);
    return !oom;
  }
  const qa_text_atlas *font =
      request->kind == QA_FONT_REQUEST_KFONT
          ? qa_text_font_registry_load_kfont(registry, request->path, &oom)
          : qa_text_font_registry_load_truetype(
                registry, request->path, request->pixel_size,
                request->codepoints, request->codepoint_count, &oom);
  if (oom) return false;
  if (font == NULL) {
    out->is_atlas = false;
    out->unicode = NULL;
  } else {
    out->is_atlas = true;
    out->font = font;
  }
  return true;
}

void qa_text_font_registry_close(qa_text_font_registry *registry) {
  if (registry->closed) return;
  registry->closed = true;
  for (size_t i = 0; i < registry->image_count; i++) {
    registry->services.release_image(registry->services.ctx, registry->images[i]);
  }
  free(registry->images);
  registry->images = NULL;
  registry->image_count = 0;
  registry->image_capacity = 0;
  for (size_t i = 0; i < registry->cache_count; i++) {
    free(registry->cache[i].key);
    if (registry->cache[i].font != NULL) {
      qa_text_atlas_destroy(registry->cache[i].font);
      free(registry->cache[i].font);
    }
  }
  free(registry->cache);
  registry->cache = NULL;
  registry->cache_count = 0;
  registry->cache_capacity = 0;
}

/* --- glyph resolution --- */

static bool atlas_glyph(const qa_text_atlas *atlas, double codepoint,
                        qa_resolved_text_glyph *out) {
  const qa_atlas_glyph *glyph = qa_text_atlas_glyph(atlas, codepoint);
  if (glyph == NULL || glyph->width == 0) return false;
  out->atlas = atlas;
  out->glyph = *glyph;
  out->codepoint = codepoint;
  out->visible = codepoint != 32;
  return true;
}

static qa_resolved_text_glyph classic_glyph(
    const qa_text_font_selection *selection, double code, bool alternate) {
  int32_t masked = (to_int32(code) & 255) | (alternate ? 128 : 0);
  qa_resolved_text_glyph result;
  if (!atlas_glyph(selection->classic, masked, &result)) {
    qa_error("Classic charset is missing a cell");
  }
  result.visible = (to_int32(code) & 127) != 32;
  return result;
}

qa_resolved_text_glyph qa_resolve_text_glyph(
    const qa_text_font_selection *selection, double codepoint,
    bool alternate) {
  qa_resolved_text_glyph result;
  if (!selection->is_atlas) {
    if (codepoint <= 255) return classic_glyph(selection, codepoint, alternate);
    if (selection->unicode != NULL) {
      if (atlas_glyph(selection->unicode, codepoint, &result)) return result;
      if (atlas_glyph(selection->unicode, 63, &result)) return result;
    }
    return classic_glyph(selection, 63, alternate);
  }
  if (atlas_glyph(selection->font, codepoint, &result)) return result;
  for (size_t i = 0; i < selection->fallback_count; i++) {
    if (atlas_glyph(selection->fallbacks[i], codepoint, &result)) return result;
  }
  if (codepoint <= 255) return classic_glyph(selection, codepoint, alternate);
  if (atlas_glyph(selection->font, 63, &result)) return result;
  return classic_glyph(selection, 63, alternate);
}

qa_text_texture_rect qa_glyph_uv(const qa_resolved_text_glyph *glyph) {
  double width = glyph->atlas->picture.image.image.width;
  double height = glyph->atlas->picture.image.image.height;
  qa_text_texture_rect uv;
  uv.s = glyph->glyph.x / width;
  uv.t = glyph->glyph.y / height;
  uv.s2 = (glyph->glyph.x + glyph->glyph.width) / width;
  uv.t2 = (glyph->glyph.y + glyph->glyph.height) / height;
  return uv;
}

/* Decodes one UTF-8 code point; invalid bytes yield U+FFFD and consume
 * one byte (the TS iterates a well-formed UTF-16 string). */
static int64_t decode_utf8(const unsigned char *text, size_t length,
                           size_t *advance) {
  unsigned char first = text[0];
  if (first < 0x80) {
    *advance = 1;
    return first;
  }
  int extra = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc0 ? 1 : -1;
  if (extra < 0 || length < (size_t)(extra + 1)) {
    *advance = 1;
    return 0xfffd;
  }
  int64_t code = first & (extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07);
  for (int i = 1; i <= extra; i++) {
    if ((text[i] & 0xc0) != 0x80) {
      *advance = 1;
      return 0xfffd;
    }
    code = (code << 6) | (text[i] & 0x3f);
  }
  *advance = (size_t)(extra + 1);
  return code;
}

int64_t *qa_text_codepoints(const char *text, size_t length,
                            size_t *out_count) {
  *out_count = 0;
  int64_t *points = NULL;
  size_t count = 0;
  size_t capacity = 0;
  size_t offset = 0;
  const unsigned char *bytes = (const unsigned char *)text;
  while (offset < length) {
    size_t advance = 1;
    int64_t code = decode_utf8(bytes + offset, length - offset, &advance);
    offset += advance;
    bool seen = false;
    for (size_t i = 0; i < count; i++) {
      if (points[i] == code) seen = true;
    }
    if (seen) continue;
    if (count == capacity) {
      size_t grown = capacity == 0 ? 16 : capacity * 2;
      int64_t *next = (int64_t *)realloc(points, grown * sizeof *next);
      if (next == NULL) {
        free(points);
        return NULL;
      }
      points = next;
      capacity = grown;
    }
    points[count++] = code;
  }
  *out_count = count;
  return points;
}
