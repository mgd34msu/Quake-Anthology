/*
 * Port of src/text/layout.ts: one text layout calculation supplying
 * both measurement and draw positions, seat text scaling, and the
 * per-seat SeatTextPresentation binder.
 * C accommodations:
 *  - Text is bytes + length (UTF-8; NUL bytes are layout terminators,
 *    as "\0" is in the TS). sourceOffset values are byte offsets,
 *    where the TS counts UTF-16 code units; they agree for ASCII.
 *  - Optional TextLayoutOptions fields use has_* flags (absent
 *    maxWidth is the TS Infinity default). colorCodes/align string
 *    literals become enums.
 *  - The layout result owns its line/glyph arrays; free with
 *    qa_text_layout_destroy. layout functions return false only on
 *    OOM; invalid dimensions abort with the TS RangeError messages.
 *  - SeatTextPresentation copies the caller's options and overrides
 *    the font, matching Omit<TextLayoutOptions, "font">.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_LAYOUT_H
#define QA_TEXT_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/text/atlas.h"
#include "qa/text/draw2d.h"

typedef enum qa_text_color_codes {
  QA_COLOR_CODES_LITERAL, /* TS undefined / "literal" */
  QA_COLOR_CODES_Q3       /* "q3" */
} qa_text_color_codes;

typedef enum qa_text_align {
  QA_TEXT_ALIGN_LEFT,
  QA_TEXT_ALIGN_CENTER,
  QA_TEXT_ALIGN_RIGHT
} qa_text_align;

/* TS TextLayoutOptions. */
typedef struct qa_text_layout_options {
  const char *text;
  size_t text_length;
  qa_text_font_selection font;
  double scale;
  qa_vec4 color;
  qa_text_color_codes color_codes;
  bool force_color;
  bool alternate;
  bool has_max_width;
  double max_width;
  qa_text_align align;
  bool has_line_height;
  double line_height;
  bool has_max_glyphs;
  double max_glyphs;
  bool has_tab_columns;
  double tab_columns;
} qa_text_layout_options;

/* TS PositionedGlyph. */
typedef struct qa_positioned_glyph {
  int64_t source_offset; /* byte offset; UTF-16 units in the TS */
  qa_resolved_text_glyph glyph;
  qa_rect rect;
  qa_text_texture_rect uv;
  qa_vec4 color;
} qa_positioned_glyph;

/* TS TextLine. Owns glyphs. */
typedef struct qa_text_line {
  double width;
  double y;
  qa_positioned_glyph *glyphs;
  size_t glyph_count;
} qa_text_line;

/* TS TextLayout. Owns lines. */
typedef struct qa_text_layout {
  double width;
  double height;
  double line_height;
  qa_text_line *lines;
  size_t line_count;
  int64_t glyph_count;
} qa_text_layout;

void qa_text_layout_destroy(qa_text_layout *layout);
/* TS layoutText. False only on OOM. */
bool qa_layout_text(const qa_text_layout_options *options,
                    qa_text_layout *out);
/* TS drawTextLayout. */
void qa_draw_text_layout(const qa_draw2d *draw, const qa_text_layout *layout,
                         qa_vec2 origin, double shadow_offset);

/* TS SeatTextPresentation. font is public, as in the TS. */
typedef struct qa_seat_text_presentation {
  qa_seat_id seat;
  qa_text_font_selection font;
} qa_seat_text_presentation;

void qa_seat_text_presentation_init(qa_seat_text_presentation *presentation,
                                    qa_seat_id seat,
                                    qa_text_font_selection font);
bool qa_seat_text_presentation_layout(
    const qa_seat_text_presentation *presentation,
    const qa_text_layout_options *options, qa_text_layout *out);
/* Aborts when the draw sink belongs to another seat (TS Error). */
bool qa_seat_text_presentation_draw(
    const qa_seat_text_presentation *presentation, const qa_draw2d *draw,
    const qa_text_layout_options *options, qa_vec2 origin,
    double shadow_offset, qa_text_layout *out);

#endif
