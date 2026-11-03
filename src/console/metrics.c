/* Port of src/console/metrics.ts. GPL-2.0-or-later. */
#include "qa/console/metrics.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Math.max / Math.min, NaN and -0 included. */
static double max2(double a, double b) { return qa_js_max(a, b); }
static double min2(double a, double b) { return qa_js_min(a, b); }

double qa_console_cell_width(const qa_text_font_selection *font, double line_height) {
  qa_resolved_text_glyph glyph = qa_resolve_text_glyph(font, 77, false);
  double normalization = glyph.atlas->has_cap_ink ? 6 / glyph.atlas->cap_height : 1;
  return ceil(glyph.glyph.advance * line_height / glyph.atlas->line_height * normalization);
}

qa_console_metrics qa_console_metrics_for(const qa_console_metrics_options *options) {
  double ratio = max2(1, options->pixel_ratio);
  double automatic = max2(2, floor(options->height / ratio / 300));
  bool explicit = isfinite(options->requested_scale) && options->requested_scale > 0;
  double requested = explicit
                         ? max2(1, min2(4, qa_js_round(options->requested_scale)))
                         : automatic;
  double fit = explicit
                   ? max2(1, floor(min2(options->width / 40, options->height / 16)))
                   : max2(1, floor(min2(options->width / 256, options->height / 96)));
  double scale = max2(1, min2(fit, qa_js_round(requested * ratio)));
  double line_height = 8 * scale;
  double cell_width = qa_console_cell_width(options->font, line_height);
  qa_console_metrics out;
  out.scale = scale;
  out.line_height = line_height;
  out.cell_width = cell_width;
  out.columns = max2(1, floor((options->width - 16 * scale) / cell_width));
  return out;
}

/* /[\p{L}\p{N}\p{M}]/u for one code point, from the table bun generates. */
static bool is_letter_number_mark(uint32_t code) {
  static const struct {
    uint32_t first, last;
  } ranges[] = {
#include "metrics_letters.inc"
  };
  size_t lo = 0, hi = sizeof ranges / sizeof *ranges;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (code < ranges[mid].first) hi = mid;
    else if (code > ranges[mid].last) lo = mid + 1;
    else return true;
  }
  return false;
}

/* Decodes one UTF-8 code point; an invalid lead byte is its own
 * code point (as in the console buffer port). */
static uint32_t decode_codepoint(const unsigned char *text, size_t *advance) {
  unsigned char lead = text[0];
  if (lead < 0x80) {
    *advance = 1;
    return lead;
  }
  int extra = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : -1;
  if (extra < 0) {
    *advance = 1;
    return lead;
  }
  uint32_t code = lead & (extra == 1 ? 0x1fu : extra == 2 ? 0x0fu : 0x07u);
  for (int i = 1; i <= extra; i++) {
    if ((text[i] & 0xc0) != 0x80) {
      *advance = 1;
      return lead;
    }
    code = (code << 6) | (text[i] & 0x3fu);
  }
  *advance = (size_t)extra + 1;
  return code;
}

qa_console_glyph_metrics qa_console_glyph_metrics_for(const qa_text_font_selection *font,
                                                      const char *character,
                                                      double line_height, double cell_width) {
  const unsigned char *text = (const unsigned char *)character;
  size_t advance = 0;
  uint32_t code = text[0] == 0 ? 32 : decode_codepoint(text, &advance);
  bool any_mark = is_letter_number_mark(code);
  for (const unsigned char *p = text + advance; *p != 0 && !any_mark;) {
    size_t step = 0;
    any_mark = is_letter_number_mark(decode_codepoint(p, &step));
    p += step;
  }
  qa_resolved_text_glyph glyph = qa_resolve_text_glyph(font, (double)code, false);
  bool is_text = (code >= 32 && code <= 126) || any_mark;
  bool has_cap = is_text && glyph.atlas->has_cap_ink;
  double normalization = has_cap ? 6 / glyph.atlas->cap_height : 1;
  double natural_width = glyph.glyph.width * line_height / glyph.atlas->line_height *
                         normalization;
  double fit = min2(1, cell_width / max2(1, natural_width));
  qa_console_glyph_metrics out;
  out.line_height = line_height * normalization * fit;
  out.top = (has_cap ? glyph.atlas->cap_top : 0) * line_height / 8 * normalization * fit;
  return out;
}
