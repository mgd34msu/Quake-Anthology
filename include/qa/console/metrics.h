/*
 * Port of src/console/metrics.ts: console character-grid sizing
 * from the selected text font. C accommodations:
 *  - All TS numbers stay double; Math.ceil/floor/round map to
 *    libm (round agrees with JS for the positive values here).
 *  - consoleGlyphMetrics' character is a UTF-8 byte string; its
 *    first decoded code point is the TS codePointAt(0). The TS
 *    /[\p{L}\p{N}\p{M}]/u test is the full range table bun reports
 *    for that class (src/console/metrics_letters.inc).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_METRICS_H
#define QA_CONSOLE_METRICS_H

#include "qa/text/atlas.h"

/** TS ConsoleMetrics. */
typedef struct qa_console_metrics {
  double scale;
  double line_height;
  double cell_width;
  double columns;
} qa_console_metrics;

typedef struct qa_console_metrics_options {
  double width;
  double height;
  double pixel_ratio;
  double requested_scale;
  const qa_text_font_selection *font;
} qa_console_metrics_options;

/* Keep the source glyph aspect ratio inside a fixed grid. */
double qa_console_cell_width(const qa_text_font_selection *font, double line_height);
qa_console_metrics qa_console_metrics_for(const qa_console_metrics_options *options);

/** TS consoleGlyphMetrics result. */
typedef struct qa_console_glyph_metrics {
  double line_height;
  double top;
} qa_console_glyph_metrics;

/* Text cap ink occupies six of eight source console pixels. */
qa_console_glyph_metrics qa_console_glyph_metrics_for(const qa_text_font_selection *font,
                                                      const char *character,
                                                      double line_height, double cell_width);

#endif
