/*
 * Port of src/console/draw.ts (drawConsole): console rows, the edit
 * field with its blinking caret, and up to three help lines, laid
 * out in fixed character cells above the bottom of the console area.
 * C accommodations:
 *  - SeatTextPresentation (src/text/layout.ts), consoleGlyphMetrics
 *    (src/console/metrics.ts) and clipPicture (src/render/commands/
 *    frame.ts) have no ported C headers yet, so the operations this
 *    file calls on them are decomposed into the callbacks below,
 *    and the Draw2D (ported as qa_draw2d) is reached only through
 *    the draw target because the TS creates a clipped Draw2D
 *    wrapper first. No other module is stubbed: draw_character/
 *    draw_pic receive the TS console clip bounds and must draw
 *    through that clipped wrapper exactly as the TS does;
 *    layout_width is SeatTextPresentation.layout(...).width for
 *    one character.
 *  - ConsoleField (ported as qa_console_field, byte-counted) is
 *    read through a borrowed view the caller fills from it (text
 *    from qa_console_field_text()); this file counts code points
 *    as the TS does, so cursor/scroll in the view are code-point
 *    indices and can differ from the field's byte counts for
 *    non-ASCII text. Characters reach the callbacks as code
 *    points decoded from UTF-8 (invalid bytes are one each).
 *  - PictureAsset is an opaque borrowed pointer (NULL = null).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_DRAW_H
#define QA_CONSOLE_DRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/console/buffer.h"
#include "qa/console/discovery.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

/** TS Draw2D over its TextDrawSink, reduced to what drawConsole
 * reads: seat/width/height and drawPic for the background. */
typedef struct qa_console_draw_target {
  void *ctx;
  qa_seat_id seat;
  double width;
  double height;
  /* Draw2D.drawPic(rect, picture) through the clipped wrapper. */
  void (*draw_pic)(void *ctx, qa_rect rect, const void *picture,
                   qa_rect clip);
} qa_console_draw_target;

/** TS SeatTextPresentation + consoleGlyphMetrics(font, ...). */
typedef struct qa_console_text_presentation {
  void *ctx;
  qa_seat_id seat;
  const void *font; /* opaque TS TextFontSelection, borrowed */
  /* consoleGlyphMetrics: per-character line height and top. */
  void (*glyph_metrics)(void *ctx, const void *font, uint32_t character,
                        double line_height, double cell_width,
                        double *out_line_height, double *out_top);
  /* layout({text: character, scale, color, alternate,
   * lineHeight}).width */
  double (*layout_width)(void *ctx, uint32_t character, double scale,
                         qa_vec4 color, bool alternate, double line_height);
  /* draw(draw, {text: character, scale, color, alternate,
   * lineHeight}, {x, y}) through the clipped Draw2D. */
  void (*draw_character)(void *ctx, uint32_t character, double scale,
                         qa_vec4 color, bool alternate, double line_height,
                         double x, double y, qa_rect clip);
} qa_console_text_presentation;

/** Borrowed read view of TS ConsoleField. */
typedef struct qa_console_field_view {
  const char *text;
  int64_t cursor;
  int64_t scroll;
  bool overstrike;
} qa_console_field_view;

/** TS ConsoleDrawOptions. field NULL = null; has_cell_width false
 * is the TS omitted cellWidth; selected_entry NULL = undefined. */
typedef struct qa_console_draw_options {
  const qa_console_draw_target *draw;
  const qa_console_text_presentation *text;
  const qa_console_row *rows;
  size_t row_count;
  const qa_console_field_view *field;
  double height;
  double scale;
  bool has_cell_width;
  double cell_width;
  double now_ms;
  const void *background;
  const qa_console_discovery_entry *selected_entry;
} qa_console_draw_options;

/* Aborts when the text belongs to another seat (TS throws). */
void qa_draw_console(const qa_console_draw_options *options);

#endif
