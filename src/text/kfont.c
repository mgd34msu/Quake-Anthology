/* Port of src/text/kfont.ts. GPL-2.0-or-later. */
#include "qa/text/kfont.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"

/* JS parseInt(text, 10): leading whitespace, a sign, then digits;
 * NaN when no digit is present. */
static double js_parse_int(const char *text) {
  const unsigned char *p = (const unsigned char *)(text != NULL ? text : "");
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '\f' ||
         *p == '\r') {
    p++;
  }
  bool negative = false;
  if (*p == '+' || *p == '-') {
    negative = *p == '-';
    p++;
  }
  if (*p < '0' || *p > '9') return NAN;
  double value = 0;
  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (double)(*p - '0');
    p++;
  }
  return negative ? -value : value;
}

static char *next_token(qa_legacy_parse_state *state) {
  char *token = qa_parse_q2_token(state, QA_Q2_TOKEN_MAX);
  return token; /* NULL only on OOM */
}

static bool map_set(qa_kfont_glyph_entry **entries, size_t *count,
                    size_t *capacity, double codepoint,
                    qa_kfont_char glyph) {
  for (size_t i = 0; i < *count; i++) {
    if ((*entries)[i].codepoint == codepoint) {
      (*entries)[i].glyph = glyph;
      return true;
    }
  }
  if (*count == *capacity) {
    size_t grown = *capacity == 0 ? 16 : *capacity * 2;
    qa_kfont_glyph_entry *next =
        (qa_kfont_glyph_entry *)realloc(*entries, grown * sizeof *next);
    if (next == NULL) return false;
    *entries = next;
    *capacity = grown;
  }
  (*entries)[*count].codepoint = codepoint;
  (*entries)[*count].glyph = glyph;
  (*count)++;
  return true;
}

static const qa_kfont_char *map_get(const qa_kfont_glyph_entry *entries,
                                    size_t count, double codepoint) {
  for (size_t i = 0; i < count; i++) {
    if (entries[i].codepoint == codepoint) return &entries[i].glyph;
  }
  return NULL;
}

void qa_parsed_kfont_destroy(qa_parsed_kfont *parsed) {
  free(parsed->texture_token);
  free(parsed->glyphs);
  memset(parsed, 0, sizeof *parsed);
}

bool qa_parse_kfont(const char *text, qa_parsed_kfont *out) {
  memset(out, 0, sizeof *out);
  qa_legacy_parse_state state;
  state.data = text != NULL ? text : "";
  state.length = strlen(state.data);
  state.index = 0;
  char *texture_token = NULL;
  double line_height = 0;
  for (;;) {
    char *token = next_token(&state);
    if (token == NULL) goto fail;
    if (token[0] == '\0') {
      free(token);
      break;
    }
    if (strcmp(token, "texture") == 0) {
      free(token);
      free(texture_token);
      texture_token = next_token(&state);
      if (texture_token == NULL) goto fail;
    } else if (strcmp(token, "unicode") == 0) {
      free(token);
    } else if (strcmp(token, "mapchar") == 0) {
      free(token);
      char *ignored = next_token(&state);
      if (ignored == NULL) goto fail;
      free(ignored);
      for (;;) {
        char *entry_token = next_token(&state);
        if (entry_token == NULL) goto fail;
        if (strcmp(entry_token, "}") == 0 || entry_token[0] == '\0') {
          free(entry_token);
          break;
        }
        double codepoint = js_parse_int(entry_token);
        free(entry_token);
        char *fields[4];
        double values[4];
        bool oom = false;
        for (int i = 0; i < 4; i++) {
          fields[i] = next_token(&state);
          if (fields[i] == NULL) oom = true;
          values[i] = fields[i] != NULL ? js_parse_int(fields[i]) : NAN;
        }
        char *extra = next_token(&state);
        if (extra == NULL) oom = true;
        free(extra);
        for (int i = 0; i < 4; i++) free(fields[i]);
        if (oom) goto fail;
        if (!isfinite(codepoint) || !isfinite(values[0]) ||
            !isfinite(values[1]) || !isfinite(values[2]) ||
            !isfinite(values[3])) {
          goto fail; /* TS parseError -> null */
        }
        qa_kfont_char glyph = {values[0], values[1], values[2], values[3],
                               false, false};
        if (!map_set(&out->glyphs, &out->glyph_count, &out->glyph_capacity,
                     codepoint, glyph)) {
          goto fail;
        }
        if (glyph.h > line_height) line_height = glyph.h;
        double index = codepoint - QA_KFONT_ASCII_MIN;
        if (index >= 0 && index < QA_KFONT_NUM_CHARS) {
          out->chars[(size_t)index] = glyph;
          out->present[(size_t)index] = true;
        }
      }
    } else {
      free(token);
    }
  }
  if (texture_token == NULL) goto fail;
  out->texture_token = texture_token;
  out->line_height = line_height;
  return true;
fail:
  free(texture_token);
  qa_parsed_kfont_destroy(out);
  return false;
}

const qa_kfont_char *qa_parsed_kfont_glyph(const qa_parsed_kfont *parsed,
                                           double codepoint) {
  return map_get(parsed->glyphs, parsed->glyph_count, codepoint);
}

qa_kfont qa_kfont_from_parsed(const qa_parsed_kfont *parsed, const char *pic) {
  qa_kfont font;
  font.pic = pic;
  font.chars = parsed->chars;
  font.present = parsed->present;
  font.glyphs = parsed->glyphs;
  font.glyph_count = parsed->glyph_count;
  font.line_height = parsed->line_height;
  return font;
}

const qa_kfont_char *qa_scr_kfont_lookup(const qa_kfont *font,
                                         double codepoint) {
  double index = codepoint - QA_KFONT_ASCII_MIN;
  if (!(index >= 0) || index >= QA_KFONT_NUM_CHARS) return NULL;
  if (!font->present[(size_t)index]) return NULL;
  const qa_kfont_char *ch = &font->chars[(size_t)index];
  if (ch->w == 0) return NULL; /* TS !ch.w also drops NaN widths */
  if (isnan(ch->w)) return NULL;
  return ch;
}

const qa_kfont_char *qa_kfont_glyph(const qa_kfont *font, double codepoint) {
  const qa_kfont_char *ch = map_get(font->glyphs, font->glyph_count, codepoint);
  if (ch == NULL || ch->w == 0 || isnan(ch->w)) return NULL;
  return ch;
}

bool qa_kfont_has_glyph(const qa_kfont *font, double codepoint) {
  return qa_kfont_glyph(font, codepoint) != NULL;
}

const qa_kfont_char *qa_ttf_kfont_lookup(const qa_ttf_kfont *font,
                                         double codepoint) {
  const qa_kfont_char *ch = map_get(font->chars, font->char_count, codepoint);
  if (ch == NULL || ch->w == 0 || isnan(ch->w)) return NULL;
  return ch;
}

bool qa_kfont_from_ttf(const qa_font_atlas *atlas, const char *pic,
                       qa_ttf_kfont *out) {
  memset(out, 0, sizeof *out);
  size_t capacity = 0;
  for (size_t i = 0; i < atlas->glyph_count; i++) {
    const qa_atlas_rect *rect = &atlas->glyphs[i];
    qa_kfont_char glyph = {(double)rect->x, (double)rect->y, (double)rect->w,
                           (double)rect->h, true, rect->color};
    if (!map_set(&out->chars, &out->char_count, &capacity,
                 (double)atlas->codepoints[i], glyph)) {
      qa_ttf_kfont_destroy(out);
      return false;
    }
  }
  if (pic != NULL) {
    size_t length = strlen(pic);
    out->pic = (char *)malloc(length + 1);
    if (out->pic == NULL) {
      qa_ttf_kfont_destroy(out);
      return false;
    }
    memcpy(out->pic, pic, length + 1);
  }
  out->line_height = (double)atlas->line_height;
  return true;
}

void qa_ttf_kfont_destroy(qa_ttf_kfont *font) {
  free(font->pic);
  free(font->chars);
  memset(font, 0, sizeof *font);
}
