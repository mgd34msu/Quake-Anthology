/* Port of src/text/layout.ts. GPL-2.0-or-later. */
#include "qa/text/layout.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static const qa_vec4 COLORS[8] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1},
                                  {1, 1, 0, 1}, {0, 0, 1, 1}, {0, 1, 1, 1},
                                  {1, 0, 1, 1}, {1, 1, 1, 1}};

/* Decodes one UTF-8 code point; invalid bytes yield U+FFFD and consume
 * one byte (the TS iterates a well-formed UTF-16 string). *unit is the
 * first UTF-16 code unit, for the Q3 color-code escape. */
static int64_t decode_utf8(const unsigned char *text, size_t length,
                           size_t *advance, int *unit) {
  unsigned char first = text[0];
  int64_t code;
  if (first < 0x80) {
    *advance = 1;
    code = first;
  } else {
    int extra = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc0 ? 1 : -1;
    if (extra < 0 || length < (size_t)(extra + 1)) {
      *advance = 1;
      code = 0xfffd;
    } else {
      code = first & (extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07);
      bool bad = false;
      for (int i = 1; i <= extra; i++) {
        if ((text[i] & 0xc0) != 0x80) bad = true;
        code = (code << 6) | (text[i] & 0x3f);
      }
      if (bad) {
        *advance = 1;
        code = 0xfffd;
      } else {
        *advance = (size_t)(extra + 1);
      }
    }
  }
  *unit = code > 0xffff ? 0xd800 + (int)((code - 0x10000) >> 10) : (int)code;
  return code;
}

typedef struct text_cell {
  qa_resolved_text_glyph glyph;
  qa_vec4 color;
  int64_t source_offset;
  int64_t character; /* code point; 9 is "\t", 32 is " " */
  double advance;
  double scale;
} text_cell;

typedef struct row_list {
  text_cell **rows;
  size_t *row_counts;
  size_t count;
  size_t capacity;
} row_list;

typedef struct layout_state {
  text_cell *current;
  size_t current_count;
  size_t current_capacity;
  row_list rows;
  double width;
  double maximum;
} layout_state;

static bool cells_push(text_cell **cells, size_t *count, size_t *capacity,
                       text_cell cell) {
  if (*count == *capacity) {
    size_t grown = *capacity == 0 ? 16 : *capacity * 2;
    text_cell *next = (text_cell *)realloc(*cells, grown * sizeof *next);
    if (next == NULL) return false;
    *cells = next;
    *capacity = grown;
  }
  (*cells)[(*count)++] = cell;
  return true;
}

static bool state_flush(layout_state *state) {
  if (state->rows.count == state->rows.capacity) {
    size_t grown = state->rows.capacity == 0 ? 8 : state->rows.capacity * 2;
    text_cell **rows =
        (text_cell **)realloc(state->rows.rows, grown * sizeof *rows);
    if (rows == NULL) return false;
    size_t *counts = (size_t *)realloc(state->rows.row_counts,
                                       grown * sizeof *counts);
    if (counts == NULL) return false;
    state->rows.rows = rows;
    state->rows.row_counts = counts;
    state->rows.capacity = grown;
  }
  state->rows.rows[state->rows.count] = state->current;
  state->rows.row_counts[state->rows.count] = state->current_count;
  state->rows.count++;
  state->current = NULL;
  state->current_count = 0;
  state->current_capacity = 0;
  state->width = 0;
  return true;
}

static bool state_push(layout_state *state, text_cell cell) {
  if (state->width + cell.advance > state->maximum &&
      state->current_count > 0) {
    int64_t break_at = -1;
    for (int64_t i = (int64_t)state->current_count - 1; i >= 0; i--) {
      if (state->current[i].character == ' ' ||
          state->current[i].character == '\t') {
        break_at = i;
        break;
      }
    }
    if (break_at >= 0) {
      size_t remainder_count = state->current_count - (size_t)break_at - 1;
      text_cell *remainder = NULL;
      if (remainder_count != 0) {
        remainder = (text_cell *)malloc(remainder_count * sizeof *remainder);
        if (remainder == NULL) return false;
        memcpy(remainder, state->current + break_at + 1,
               remainder_count * sizeof *remainder);
      }
      state->current_count = (size_t)break_at; /* drops the space cell */
      if (!state_flush(state)) {
        free(remainder);
        return false;
      }
      double width = 0;
      for (size_t i = 0; i < remainder_count; i++) {
        width += remainder[i].advance;
        if (!cells_push(&state->current, &state->current_count,
                        &state->current_capacity, remainder[i])) {
          free(remainder);
          return false;
        }
      }
      free(remainder);
      state->width = width;
      if (state->width + cell.advance > state->maximum &&
          state->current_count > 0) {
        if (!state_flush(state)) return false;
      }
    } else {
      if (!state_flush(state)) return false;
    }
  }
  if (!cells_push(&state->current, &state->current_count,
                  &state->current_capacity, cell)) {
    return false;
  }
  state->width += cell.advance;
  return true;
}

static void state_free(layout_state *state) {
  for (size_t i = 0; i < state->rows.count; i++) free(state->rows.rows[i]);
  free(state->rows.rows);
  free(state->rows.row_counts);
  free(state->current);
  memset(state, 0, sizeof *state);
}

void qa_text_layout_destroy(qa_text_layout *layout) {
  for (size_t i = 0; i < layout->line_count; i++) free(layout->lines[i].glyphs);
  free(layout->lines);
  memset(layout, 0, sizeof *layout);
}

bool qa_layout_text(const qa_text_layout_options *options,
                    qa_text_layout *out) {
  memset(out, 0, sizeof *out);
  if (!isfinite(options->scale) || options->scale <= 0) {
    qa_range_error("Text scale must be positive");
  }
  double line_height =
      options->has_line_height ? options->line_height : 8 * options->scale;
  double maximum = options->has_max_width ? options->max_width : INFINITY;
  if (!isfinite(line_height) || line_height <= 0 || maximum <= 0 ||
      isnan(maximum)) {
    qa_range_error("Invalid text layout dimensions");
  }
  double tab_columns = options->has_tab_columns ? options->tab_columns : 4;
  if (!(isfinite(tab_columns) && trunc(tab_columns) == tab_columns) ||
      tab_columns <= 0) {
    qa_range_error("Invalid tab width");
  }
  layout_state state = {0};
  state.maximum = maximum;
  qa_vec4 color = options->color;
  int64_t count = 0;
  bool skip_next = false;
  const unsigned char *bytes = (const unsigned char *)options->text;
  size_t offset = 0;
  while (offset < options->text_length) {
    int64_t source_offset = (int64_t)offset;
    size_t advance = 1;
    int unit = 0;
    int64_t character =
        decode_utf8(bytes + offset, options->text_length - offset, &advance,
                    &unit);
    offset += advance;
    if (skip_next) {
      skip_next = false;
      continue;
    }
    if (character == 0) break;
    /* options.text.charAt(offset): first unit of the next character. */
    bool has_next = offset < options->text_length;
    int next_unit = 0;
    int64_t next_character = -1;
    if (has_next) {
      size_t next_advance = 1;
      next_character = decode_utf8(bytes + offset,
                                   options->text_length - offset,
                                   &next_advance, &next_unit);
      (void)next_advance;
    }
    if (options->color_codes == QA_COLOR_CODES_Q3 && character == '^' &&
        has_next && next_character != '^') {
      const qa_vec4 *palette = &COLORS[(next_unit - 48) & 7];
      if (!options->force_color) {
        color = *palette;
        color.w = options->color.w;
      }
      skip_next = true;
      continue;
    }
    if (character == '\r') continue;
    if (character == '\n') {
      if (!state_flush(&state)) goto oom;
      continue;
    }
    if (options->has_max_glyphs && (double)count >= options->max_glyphs) break;
    qa_resolved_text_glyph glyph = qa_resolve_text_glyph(
        &options->font, character == '\t' ? 32 : (double)character,
        options->alternate);
    double scale = line_height / fmax(1, glyph.atlas->line_height);
    double cell_advance =
        character == '\t'
            ? line_height * tab_columns -
                  fmod(state.width, line_height * tab_columns)
            : glyph.glyph.advance * scale;
    qa_vec4 tint;
    if (glyph.glyph.color) {
      tint = (qa_vec4){1, 1, 1, color.w};
    } else if (options->alternate) {
      tint = (qa_vec4){0.85, 0.65, 0.12, color.w};
    } else {
      tint = color;
    }
    text_cell cell = {glyph, tint, source_offset, character, cell_advance,
                      scale};
    if (!state_push(&state, cell)) goto oom;
    count++;
  }
  if (state.current_count > 0 || state.rows.count == 0 ||
      (options->text_length > 0 &&
       options->text[options->text_length - 1] == '\n')) {
    if (!state_flush(&state)) goto oom;
  }
  out->lines = state.rows.count != 0
                   ? (qa_text_line *)calloc(state.rows.count, sizeof *out->lines)
                   : NULL;
  if (state.rows.count != 0 && out->lines == NULL) goto oom;
  out->line_count = state.rows.count;
  double widest = 0;
  for (size_t line_number = 0; line_number < state.rows.count; line_number++) {
    text_cell *row = state.rows.rows[line_number];
    size_t row_count = state.rows.row_counts[line_number];
    double row_width = 0;
    for (size_t i = 0; i < row_count; i++) row_width += row[i].advance;
    double y = (double)line_number * line_height;
    double available = isfinite(maximum) ? maximum : row_width;
    double x = options->align == QA_TEXT_ALIGN_CENTER
                   ? (available - row_width) / 2
                   : options->align == QA_TEXT_ALIGN_RIGHT
                         ? available - row_width
                         : 0;
    qa_text_line *line = &out->lines[line_number];
    line->width = row_width;
    line->y = y;
    line->glyph_count = row_count;
    line->glyphs = row_count != 0
                       ? (qa_positioned_glyph *)malloc(row_count *
                                                        sizeof *line->glyphs)
                       : NULL;
    if (row_count != 0 && line->glyphs == NULL) {
      state_free(&state);
      qa_text_layout_destroy(out);
      return false;
    }
    for (size_t i = 0; i < row_count; i++) {
      qa_positioned_glyph *positioned = &line->glyphs[i];
      positioned->source_offset = row[i].source_offset;
      positioned->glyph = row[i].glyph;
      positioned->rect.x = x;
      positioned->rect.y = y;
      positioned->rect.width = row[i].glyph.glyph.width * row[i].scale;
      positioned->rect.height = row[i].glyph.glyph.height * row[i].scale;
      positioned->uv = qa_glyph_uv(&row[i].glyph);
      positioned->color = row[i].color;
      x += row[i].advance;
    }
    widest = fmax(widest, row_width);
  }
  state_free(&state);
  out->width = widest;
  out->height = (double)out->line_count * line_height;
  out->line_height = line_height;
  out->glyph_count = count;
  return true;
oom:
  state_free(&state);
  return false;
}

static void draw_pass(const qa_draw2d *draw, const qa_text_layout *layout,
                      qa_vec2 origin, double shadow_offset, bool shadow) {
  for (size_t i = 0; i < layout->line_count; i++) {
    const qa_text_line *line = &layout->lines[i];
    for (size_t j = 0; j < line->glyph_count; j++) {
      const qa_positioned_glyph *glyph = &line->glyphs[j];
      if (!glyph->glyph.visible) continue;
      qa_vec4 shadow_color = {0, 0, 0, glyph->color.w};
      qa_draw2d_set_color(draw, shadow ? &shadow_color : &glyph->color);
      qa_rect rect = glyph->rect;
      rect.x = origin.x + glyph->rect.x + (shadow ? shadow_offset : 0);
      rect.y = origin.y + glyph->rect.y + (shadow ? shadow_offset : 0);
      qa_draw2d_stretch_pic(draw, rect, glyph->uv, &glyph->glyph.atlas->picture);
    }
  }
}

void qa_draw_text_layout(const qa_draw2d *draw, const qa_text_layout *layout,
                         qa_vec2 origin, double shadow_offset) {
  if (shadow_offset > 0) draw_pass(draw, layout, origin, shadow_offset, true);
  draw_pass(draw, layout, origin, shadow_offset, false);
  qa_draw2d_set_color(draw, NULL);
}

/* JS ToInt32 for the `& ~7` in seatTextScale. */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  if (value < 0) bits = ~bits + 1u;
  return (int32_t)bits;
}

qa_seat_text_scale qa_seat_text_scale_of(qa_rect viewport, double console_scale,
                                         double status_bar_scale,
                                         double crosshair_scale) {
  double automatic = fmax(1, floor(viewport.height / 300));
  double requested = console_scale > 0 ? console_scale : automatic;
  double raw_width = floor(fmax(320, fmin(viewport.width / requested,
                                         fmax(320, viewport.width))));
  double console_width = fmax(8, (double)(to_int32(raw_width) & ~7));
  double fit = fmax(1, fmin(floor(viewport.width / 320),
                            floor(viewport.height / 144)));
  qa_seat_text_scale out;
  out.console = viewport.width / console_width;
  out.console_width = console_width;
  out.console_height = viewport.width <= 0
                           ? viewport.height
                           : floor(console_width * viewport.height /
                                       viewport.width +
                                   0.5); /* Math.round */
  out.status_bar = status_bar_scale > 0
                       ? fmax(1, fmin(status_bar_scale, fit))
                       : fit;
  out.crosshair = fmax(1, fmin(crosshair_scale, 10));
  return out;
}

void qa_seat_text_presentation_init(qa_seat_text_presentation *presentation,
                                    qa_seat_id seat,
                                    qa_text_font_selection font) {
  presentation->seat = seat;
  presentation->font = font;
}

bool qa_seat_text_presentation_layout(
    const qa_seat_text_presentation *presentation,
    const qa_text_layout_options *options, qa_text_layout *out) {
  qa_text_layout_options with_font = *options;
  with_font.font = presentation->font;
  return qa_layout_text(&with_font, out);
}

bool qa_seat_text_presentation_draw(
    const qa_seat_text_presentation *presentation, const qa_draw2d *draw,
    const qa_text_layout_options *options, qa_vec2 origin,
    double shadow_offset, qa_text_layout *out) {
  if (!qa_same_seat(presentation->seat, draw->commands.seat)) {
    qa_error("Text presentation belongs to a different seat");
  }
  if (!qa_seat_text_presentation_layout(presentation, options, out)) {
    return false;
  }
  qa_draw_text_layout(draw, out, origin, shadow_offset);
  return true;
}
