/*
 * Port of src/text/q3-font.ts: Q3 fontInfo_t records, proportional and
 * banner atlas metrics, and registered-font text measurement.
 * C accommodations:
 *  - TS `number` glyph fields read with i32/f32 are int32_t / double
 *    (the f32 value widened, as in JS). Fixed strings become bounded
 *    char arrays. FontData/RegisteredFont are by-value records with
 *    the TS 256 glyph slots; glyph_count preserves the TS
 *    "Missing font glyph" check for shorter glyph arrays.
 *  - PictureAsset values use include/qa/text/draw2d.h; glyph
 *    pictures are borrowed pointers, NULL is TS null. The async
 *    UiAssetRegistry methods are synchronous, returning false only
 *    on OOM (the draw2d services convention); its register_font
 *    returns the borrowed RegisteredFont behind the services'
 *    opaque pointer. TS Omit<...> option shapes reuse the full
 *    options struct with the omitted field ignored.
 *  - Text inputs are NUL-terminated bytes, so the TS 8-bit check in
 *    byteText is inherent; embedded NUL truncation matches.
 *  - read/parse return false only on OOM; malformed records abort
 *    with the TS BinaryError text via qa_binary_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_Q3_FONT_H
#define QA_TEXT_Q3_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/text/draw2d.h"

#define QA_Q3_FONT_RECORD_BYTES 20548
#define QA_Q3_FONT_GLYPHS 256

#define QA_UI_LEFT 0
#define QA_UI_CENTER 1
#define QA_UI_RIGHT 2
#define QA_UI_SMALLFONT 0x10
#define QA_UI_GIANTFONT 0x40
#define QA_UI_DROPSHADOW 0x800
#define QA_UI_BLINK 0x1000
#define QA_UI_INVERSE 0x2000
#define QA_UI_PULSE 0x4000

/* TS GlyphMetrics extends TextureRect (s, t, s2, t2 inlined). */
typedef struct qa_q3_glyph_metrics {
  int32_t height;
  int32_t top;
  int32_t bottom;
  int32_t pitch;
  int32_t x_skip;
  int32_t image_width;
  int32_t image_height;
  double s;
  double t;
  double s2;
  double t2;
  char shader_name[33];
} qa_q3_glyph_metrics;

typedef struct qa_q3_font_data {
  char name[65];
  double glyph_scale;
  qa_q3_glyph_metrics glyphs[QA_Q3_FONT_GLYPHS];
} qa_q3_font_data;

/* TS RegisteredGlyph: glyph metrics plus its registered picture. */
typedef struct qa_q3_registered_glyph {
  qa_q3_glyph_metrics metrics;
  const qa_picture_asset *picture; /* TS PictureAsset | null, borrowed */
} qa_q3_registered_glyph;

typedef struct qa_q3_registered_font {
  char name[65];
  double glyph_scale;
  qa_q3_registered_glyph glyphs[QA_Q3_FONT_GLYPHS];
  size_t glyph_count;
} qa_q3_registered_font;

/* TS LegacyFonts; assets by value (their name strings are borrowed). */
typedef struct qa_q3_legacy_fonts {
  qa_picture_asset charset;
  qa_picture_asset proportional;
  qa_picture_asset glow;
  qa_picture_asset banner;
} qa_q3_legacy_fonts;

typedef enum qa_q3_font_profile {
  QA_Q3_FONT_UI = 0,
  QA_Q3_FONT_CGAME = 1
} qa_q3_font_profile;

/* TS FontSet; the three fonts are borrowed. */
typedef struct qa_q3_font_set {
  const qa_q3_registered_font *small;
  const qa_q3_registered_font *normal;
  const qa_q3_registered_font *big;
  qa_q3_font_profile profile;
  double small_threshold;
  double big_threshold;
} qa_q3_font_set;

/* TS AtlasMetric tuple [x, y, width]; width -1 is the invalid metric. */
typedef struct qa_q3_atlas_metric {
  int32_t values[3];
} qa_q3_atlas_metric;

/* `source` NULL means "<font>". False only on OOM. */
bool qa_q3_read_font_data(const uint8_t *bytes, size_t length, const char *source,
                          qa_q3_font_data *out);
bool qa_q3_parse_font_data(const uint8_t *bytes, size_t length, const char *source,
                           qa_q3_font_data *out);

qa_q3_atlas_metric qa_q3_prop_metric(int32_t code);
int64_t qa_q3_proportional_string_width(const char *input);
int64_t qa_q3_banner_string_width(const char *input);

/* TS limit defaults to 0 (unlimited). Results are TS numbers. */
double qa_q3_text_width(const qa_q3_font_set *fonts, const char *text, double scale,
                        double limit);
double qa_q3_text_height(const qa_q3_font_set *fonts, const char *text, double scale,
                         double limit);

/* TS UiAssetRegistry over FontAssetServices plus its print closure. */
typedef struct qa_q3_ui_asset_registry {
  qa_font_asset_services resources;
  qa_font_print_fn print;
  void *print_ctx;
} qa_q3_ui_asset_registry;

void qa_q3_ui_asset_registry_init(qa_q3_ui_asset_registry *registry,
                                  qa_font_asset_services resources,
                                  qa_font_print_fn print, void *print_ctx);
/* `mode` NULL is TS "no-mip". False only on OOM. */
bool qa_q3_ui_asset_registry_register_picture(qa_q3_ui_asset_registry *registry,
                                              const char *path, const char *mode,
                                              qa_picture_asset *out);
/* Borrowed font, or NULL for TS null. `path` NULL is TS null. */
const qa_q3_registered_font *qa_q3_ui_asset_registry_register_font(
    qa_q3_ui_asset_registry *registry, const char *path, double point_size);
bool qa_q3_ui_asset_registry_load_legacy_fonts(qa_q3_ui_asset_registry *registry,
                                               qa_q3_legacy_fonts *out);

void qa_q3_draw_char(const qa_draw2d *draw, const qa_picture_asset *charset, double x,
                     double y, double width, double height, int32_t code);

/* TS FixedTextOptions. */
typedef struct qa_q3_fixed_text_options {
  double x;
  double y;
  const char *text;
  qa_vec4 color;
  double char_width;
  double char_height;
  double max_chars;
  bool force_color;
  bool shadow;
} qa_q3_fixed_text_options;

void qa_q3_draw_cg_string(const qa_draw2d *draw, const qa_picture_asset *charset,
                          const qa_q3_fixed_text_options *options);

/* TS UiTextOptions. */
typedef struct qa_q3_ui_text_options {
  double x;
  double y;
  const char *text;
  qa_vec4 color;
  int32_t style;
  double time;
} qa_q3_ui_text_options;

void qa_q3_draw_ui_string(const qa_draw2d *draw, const qa_picture_asset *charset,
                          const qa_q3_ui_text_options *options);
void qa_q3_draw_proportional_string(const qa_draw2d *draw,
                                    const qa_q3_legacy_fonts *fonts,
                                    const qa_q3_ui_text_options *options);
void qa_q3_draw_cg_proportional_string(const qa_draw2d *draw,
                                       const qa_q3_legacy_fonts *fonts,
                                       const qa_q3_ui_text_options *options);
void qa_q3_draw_banner_string(const qa_draw2d *draw, const qa_q3_legacy_fonts *fonts,
                              const qa_q3_ui_text_options *options);
void qa_q3_draw_cg_banner_string(const qa_draw2d *draw,
                                 const qa_q3_legacy_fonts *fonts,
                                 const qa_q3_ui_text_options *options);

/* TS TextPaintOptions. */
typedef struct qa_q3_text_paint_options {
  double x;
  double y;
  double scale;
  qa_vec4 color;
  const char *text;
  double adjust;
  double limit;
  int32_t style;
} qa_q3_text_paint_options;

/* TS TextCursor. */
typedef struct qa_q3_text_cursor {
  double position;
  double character;
  double time;
} qa_q3_text_cursor;

void qa_q3_text_paint(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                      const qa_q3_text_paint_options *options);
/* The options' adjust field is ignored (TS Omit<..., "adjust">). */
void qa_q3_text_paint_with_cursor(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                                  const qa_q3_text_paint_options *options,
                                  qa_q3_text_cursor cursor);
/* The options' style field is ignored (TS Omit<..., "style">). */
double qa_q3_text_paint_limit(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                              const qa_q3_text_paint_options *options, double max_x);

#endif
