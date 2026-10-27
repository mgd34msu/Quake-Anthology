/*
 * Port of src/text/truetype.ts: direct sfnt parsing with unhinted
 * glyf/CFF outlines, CID subroutines, legacy kern and COLR v0/CPAL,
 * plus scanline rasterization and shelf-packaged font atlases. GPOS,
 * COLR v1 paints, CFF arithmetic and seac composition are unsupported,
 * as in the TypeScript. No FreeType calls: the parser is ported
 * faithfully (the platform wave handles FreeType separately).
 * C accommodations:
 *  - ParsedFontT becomes qa_parsed_font. qa_truetype_parse_font copies
 *    the input bytes; the font owns the copy, its tables and its glyph
 *    caches, all freed by qa_parsed_font_destroy.
 *  - TypeScript parseFont catches BinaryError/RangeError into
 *    {ok:false, reason}. The C parser keeps that channel internally
 *    (it must not use the aborting qa_range_error/qa_binary_error):
 *    qa_parse_font_result.reason is a malloc'd copy of the same
 *    message, freed with the result. Lazy contours() errors propagate
 *    past parseFont in the TypeScript, so qa_parsed_font_contours
 *    converts them to qa_range_error aborts with the same message.
 *  - contours() returns a borrowed view of the font's cache, valid
 *    until qa_parsed_font_destroy. colorLayers malloc's its array
 *    (caller frees). Raster/atlas outputs are caller-owned and freed
 *    with their destroy functions. Atlas glyphs (a Map in TS) become
 *    parallel codepoints/rects arrays plus a lookup helper.
 *  - TypeScript `number` coordinates stay double. Float32Array
 *    coverage accumulates in float with double intermediates cast at
 *    the store points. No string logic is observable, so UTF-16 vs
 *    byte lengths never arise (table tags compare as 4 raw bytes).
 *  - Default arguments are explicit parameters: supersampleY=4,
 *    paletteId=0, maxWidth=512, foreground white (pass NULL).
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_TRUETYPE_H
#define QA_TEXT_TRUETYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* FlattenedContourT: one flattened outline contour. */
typedef struct qa_tt_point {
  double x;
  double y;
} qa_tt_point;

typedef struct qa_flattened_contour {
  qa_tt_point *points;
  size_t count;
} qa_flattened_contour;

/* FlattenedContourT[]; borrowed from the font cache when returned by
 * qa_parsed_font_contours. */
typedef struct qa_tt_contours {
  qa_flattened_contour *contours;
  size_t count;
} qa_tt_contours;

/* ColorLayerT. */
typedef struct qa_color_layer {
  int64_t gid;
  int64_t palette_index;
} qa_color_layer;

/* {r,g,b,a} colors; doubles as in the TypeScript (palette entries are
 * byte-valued, the foreground argument is any number). */
typedef struct qa_tt_color {
  double r;
  double g;
  double b;
  double a;
} qa_tt_color;

typedef enum qa_outline_format {
  QA_OUTLINE_GLYF,
  QA_OUTLINE_CFF,
} qa_outline_format;

typedef struct qa_parsed_font qa_parsed_font;

/* ParseFontResultT. On success font is owned by the result; on failure
 * reason is a malloc'd message. Either way free with
 * qa_parse_font_result_destroy (which destroys the font too). */
typedef struct qa_parse_font_result {
  bool ok;
  char *reason;
  qa_parsed_font *font;
} qa_parse_font_result;

/* False return from the raster/atlas constructors means OOM only. */
qa_parse_font_result qa_truetype_parse_font(const uint8_t *buf, size_t length);
void qa_parse_font_result_destroy(qa_parse_font_result *result);
void qa_parsed_font_destroy(qa_parsed_font *font);

int64_t qa_parsed_font_units_per_em(const qa_parsed_font *font);
int64_t qa_parsed_font_num_glyphs(const qa_parsed_font *font);
int64_t qa_parsed_font_ascent(const qa_parsed_font *font);
int64_t qa_parsed_font_descent(const qa_parsed_font *font);
int64_t qa_parsed_font_line_gap(const qa_parsed_font *font);
qa_outline_format qa_parsed_font_outline_format(const qa_parsed_font *font);
/* Borrowed "glyf"/"cff" tag string. */
const char *qa_parsed_font_outline_format_name(const qa_parsed_font *font);

int64_t qa_parsed_font_cmap_lookup(qa_parsed_font *font, int64_t codepoint);
int64_t qa_parsed_font_advance_width(const qa_parsed_font *font, int64_t gid);
qa_tt_contours qa_parsed_font_contours(qa_parsed_font *font, int64_t gid);
int64_t qa_parsed_font_kerning(const qa_parsed_font *font, int64_t left_gid,
                               int64_t right_gid);
/* False when the TypeScript returns null. On true *out is a malloc'd
 * array of *out_count layers the caller frees (NULL when count is 0,
 * or on OOM, which sets *out_count to 0 as well). */
bool qa_parsed_font_color_layers(const qa_parsed_font *font, int64_t gid,
                                 qa_color_layer **out, size_t *out_count);
/* palette_id stands in for the optional argument (default 0). False
 * when the TypeScript returns null. */
bool qa_parsed_font_palette_color(const qa_parsed_font *font,
                                  int64_t palette_index, int64_t palette_id,
                                  qa_tt_color *out);

/* RasterizedGlyphT; coverage is width*height bytes, caller destroys. */
typedef struct qa_rasterized_glyph {
  int64_t width;
  int64_t height;
  uint8_t *coverage;
  size_t coverage_length;
} qa_rasterized_glyph;

void qa_rasterized_glyph_destroy(qa_rasterized_glyph *glyph);
/* supersample_y stands in for the defaulted argument (TS default 4). */
bool qa_truetype_rasterize_contours(const qa_flattened_contour *contours,
                                    size_t contour_count, int64_t width,
                                    int64_t height, double supersample_y,
                                    qa_rasterized_glyph *out);
bool qa_truetype_rasterize_glyph(qa_parsed_font *font, int64_t gid, double px,
                                 qa_rasterized_glyph *out);

/* RasterizedColorGlyphT; pixels is width*height*4 RGBA bytes. */
typedef struct qa_rasterized_color_glyph {
  int64_t width;
  int64_t height;
  uint8_t *pixels;
  size_t pixels_length;
} qa_rasterized_color_glyph;

void qa_rasterized_color_glyph_destroy(qa_rasterized_color_glyph *glyph);
/* foreground NULL means the TS default (255,255,255,255). Returns
 * false with *out zeroed when the TypeScript returns null, and false
 * with *oom set on allocation failure. */
bool qa_truetype_rasterize_color_glyph(qa_parsed_font *font, int64_t base_gid,
                                       double px, int64_t palette_id,
                                       const qa_tt_color *foreground,
                                       qa_rasterized_color_glyph *out,
                                       bool *oom);

/* AtlasRectT. */
typedef struct qa_atlas_rect {
  int64_t x;
  int64_t y;
  int64_t w;
  int64_t h;
  bool color;
} qa_atlas_rect;

/* FontAtlasT; pixels is width*height*4 RGBA. glyphs[i] is the rect
 * for codepoints[i] (the TS Map, in first-insertion order). */
typedef struct qa_font_atlas {
  int64_t width;
  int64_t height;
  uint8_t *pixels;
  size_t pixels_length;
  int64_t *codepoints;
  qa_atlas_rect *glyphs;
  size_t glyph_count;
  int64_t line_height;
} qa_font_atlas;

void qa_font_atlas_destroy(qa_font_atlas *atlas);
/* max_width stands in for the defaulted argument (TS default 512).
 * Invalid px/max_width abort with the TS RangeError message. */
bool qa_truetype_build_font_atlas(qa_parsed_font *font,
                                  const int64_t *codepoints, size_t count,
                                  double px, int64_t max_width,
                                  qa_font_atlas *out);
bool qa_font_atlas_rect_for(const qa_font_atlas *atlas, int64_t codepoint,
                            qa_atlas_rect *out);

/* Malloc'd latin1 codepoint list; caller frees. NULL on OOM. */
int64_t *qa_truetype_latin1_codepoints(size_t *out_count);

#endif
