/* Port of src/text/q3-font.ts. GPL-2.0-or-later. */
#include "qa/text/q3_font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static double fr(double value) { return (float)value; }

static bool font_fixed_string(qa_binary_reader *reader, int64_t length, char *out) {
  uint8_t *bytes = qa_binary_reader_bytes(reader, length);
  if (bytes == NULL && length != 0) return false;
  size_t count = 0;
  for (int64_t i = 0; i < length; i++) {
    if (bytes[i] == 0) break;
    out[count++] = (char)bytes[i];
  }
  out[count] = '\0';
  free(bytes);
  return true;
}

bool qa_q3_read_font_data(const uint8_t *bytes, size_t length, const char *source,
                          qa_q3_font_data *out) {
  const char *name = source == NULL ? "<font>" : source;
  if (length != QA_Q3_FONT_RECORD_BYTES)
    qa_binary_error(name, 0, "fontInfo_t must contain exactly 20548 bytes");
  qa_binary_reader reader;
  /* The reader borrows mutable storage; it never writes through it. */
  if (qa_binary_reader_init(&reader, (uint8_t *)bytes, length, name) == 0) return false;
  bool ok = true;
  for (int i = 0; i < QA_Q3_FONT_GLYPHS && ok; i++) {
    qa_q3_glyph_metrics *glyph = &out->glyphs[i];
    glyph->height = qa_binary_reader_i32(&reader);
    glyph->top = qa_binary_reader_i32(&reader);
    glyph->bottom = qa_binary_reader_i32(&reader);
    glyph->pitch = qa_binary_reader_i32(&reader);
    glyph->x_skip = qa_binary_reader_i32(&reader);
    glyph->image_width = qa_binary_reader_i32(&reader);
    glyph->image_height = qa_binary_reader_i32(&reader);
    glyph->s = qa_binary_reader_f32(&reader);
    glyph->t = qa_binary_reader_f32(&reader);
    glyph->s2 = qa_binary_reader_f32(&reader);
    glyph->t2 = qa_binary_reader_f32(&reader);
    qa_binary_reader_i32(&reader);
    ok = font_fixed_string(&reader, 32, glyph->shader_name);
  }
  if (ok) {
    out->glyph_scale = qa_binary_reader_f32(&reader);
    ok = font_fixed_string(&reader, 64, out->name);
  }
  qa_binary_reader_destroy(&reader);
  return ok;
}

bool qa_q3_parse_font_data(const uint8_t *bytes, size_t length, const char *source,
                           qa_q3_font_data *out) {
  const char *name = source == NULL ? "<font>" : source;
  if (!qa_q3_read_font_data(bytes, length, source, out)) return false;
  for (int index = 0; index < QA_Q3_FONT_GLYPHS; index++) {
    const qa_q3_glyph_metrics *glyph = &out->glyphs[index];
    const double components[4] = {glyph->s, glyph->t, glyph->s2, glyph->t2};
    for (int component = 0; component < 4; component++)
      if (!isfinite(components[component]))
        qa_binary_error(name, index * 80 + 28 + component * 4, "non-finite float");
    if (glyph->height < 0 || glyph->pitch < 0 || glyph->image_width < 0 ||
        glyph->image_height < 0)
      qa_binary_error(name, index * 80, "Negative glyph bitmap dimension");
  }
  if (!isfinite(out->glyph_scale)) qa_binary_error(name, 20480, "non-finite float");
  if (out->glyph_scale <= 0) qa_binary_error(name, 20480, "Font scale must be positive");
  return true;
}

typedef int32_t atlas_metric[3];
static const atlas_metric PROP_ASCII[] = {
    {0, 0, 8},     {11, 122, 7},  {154, 181, 14}, {55, 122, 17},
    {79, 122, 18}, {101, 122, 23}, {153, 122, 18}, {9, 93, 7},
    {207, 122, 8}, {230, 122, 9}, {177, 122, 18}, {30, 152, 18},
    {85, 181, 7},  {34, 93, 11},  {110, 181, 6},  {130, 152, 14},
    {22, 64, 17},  {41, 64, 12},  {58, 64, 17},   {78, 64, 18},
    {98, 64, 19},  {120, 64, 18}, {141, 64, 18},  {204, 64, 16},
    {162, 64, 17}, {182, 64, 18}, {59, 181, 7},   {35, 181, 7},
    {203, 152, 14}, {56, 93, 14}, {228, 152, 14}, {177, 181, 18},
    {28, 122, 22}, {5, 4, 18},    {27, 4, 18},    {48, 4, 18},
    {69, 4, 17},   {90, 4, 13},   {106, 4, 13},   {121, 4, 18},
    {143, 4, 17},  {164, 4, 8},   {175, 4, 16},   {195, 4, 18},
    {216, 4, 12},  {230, 4, 23},  {6, 34, 18},    {27, 34, 18},
    {48, 34, 18},  {68, 34, 18},  {90, 34, 17},   {110, 34, 18},
    {130, 34, 14}, {146, 34, 18}, {166, 34, 19},  {185, 34, 29},
    {215, 34, 18}, {234, 34, 18}, {5, 64, 14},    {60, 152, 7},
    {106, 151, 13}, {83, 152, 7}, {128, 122, 17}, {4, 152, 21},
    {134, 181, 5},
};
static const atlas_metric PROP_END[] = {
    {153, 152, 13}, {11, 181, 5}, {180, 152, 13}, {79, 93, 17}};
static const atlas_metric BANNER[] = {
    {11, 12, 33}, {49, 12, 31},  {85, 12, 31},  {120, 12, 30},
    {156, 12, 21}, {183, 12, 21}, {207, 12, 32}, {13, 55, 30},
    {49, 55, 13}, {66, 55, 29},  {101, 55, 31}, {135, 55, 21},
    {158, 55, 40}, {204, 55, 32}, {12, 97, 31},  {48, 97, 31},
    {82, 97, 30}, {118, 97, 30}, {153, 97, 30}, {185, 97, 25},
    {213, 97, 30}, {11, 139, 32}, {42, 139, 51}, {93, 139, 32},
    {126, 139, 31}, {158, 139, 25},
};

qa_q3_atlas_metric qa_q3_prop_metric(int32_t code) {
  qa_q3_atlas_metric out = {{0, 0, -1}};
  int32_t ch = code & 127;
  if (ch < 32 || ch == 127) return out;
  if (ch >= 97 && ch <= 122) ch -= 32;
  const int32_t *metric;
  if (ch >= 123) {
    if (ch - 123 >= (int32_t)(sizeof PROP_END / sizeof PROP_END[0]))
      qa_range_error("Invalid proportional glyph index");
    metric = PROP_END[ch - 123];
  } else {
    if (ch - 32 >= (int32_t)(sizeof PROP_ASCII / sizeof PROP_ASCII[0]))
      qa_range_error("Invalid proportional glyph index");
    metric = PROP_ASCII[ch - 32];
  }
  memcpy(out.values, metric, sizeof out.values);
  return out;
}

static qa_q3_atlas_metric banner_metric(int32_t code) {
  if (code - 65 < 0 || code - 65 >= (int32_t)(sizeof BANNER / sizeof BANNER[0]))
    qa_range_error("Invalid banner glyph index");
  qa_q3_atlas_metric out;
  memcpy(out.values, BANNER[code - 65], sizeof out.values);
  return out;
}

int64_t qa_q3_proportional_string_width(const char *input) {
  int64_t width = 0;
  for (const unsigned char *p = (const unsigned char *)input; *p != '\0'; p++) {
    qa_q3_atlas_metric metric = qa_q3_prop_metric(*p);
    if (metric.values[2] != -1) width += metric.values[2] + 3;
  }
  return width - 3;
}

int64_t qa_q3_banner_string_width(const char *input) {
  int64_t width = 0;
  for (const unsigned char *p = (const unsigned char *)input; *p != '\0'; p++) {
    if (*p == 32)
      width += 12;
    else if (*p >= 65 && *p <= 90)
      width += banner_metric(*p).values[2] + 4;
  }
  return width - 4;
}

static bool escape_at(const unsigned char *text, size_t length, size_t index) {
  return text[index] == 94 && index + 1 < length && text[index + 1] != 94;
}

static const qa_q3_registered_font *select_font(const qa_q3_font_set *fonts,
                                                double scale) {
  if (!isfinite(scale)) qa_range_error("Non-finite text scale");
  if (fr(scale) <= fr(fonts->small_threshold)) return fonts->small;
  bool big = fonts->profile == QA_Q3_FONT_UI ? fr(scale) >= fr(fonts->big_threshold)
                                             : fr(scale) > fr(fonts->big_threshold);
  return big ? fonts->big : fonts->normal;
}

static const qa_q3_registered_glyph *glyph_at(const qa_q3_registered_font *font,
                                              int code) {
  if (code < 0 || (size_t)code >= font->glyph_count)
    qa_range_error("Missing font glyph %d", code);
  return &font->glyphs[code];
}

static double text_metric(const qa_q3_font_set *fonts, const char *input, double scale,
                          double limit, bool height) {
  const unsigned char *text = (const unsigned char *)input;
  size_t length = strlen(input);
  const qa_q3_registered_font *font = select_font(fonts, scale);
  double use_scale = fr(fr(scale) * font->glyph_scale);
  size_t maximum = length;
  if (limit > 0) {
    double truncated = trunc(limit);
    if (truncated < (double)length) maximum = (size_t)truncated;
  }
  double value = 0;
  size_t count = 0;
  for (size_t i = 0; i < length && count < maximum; i++) {
    if (escape_at(text, length, i)) {
      i++;
      continue;
    }
    const qa_q3_registered_glyph *glyph = glyph_at(font, text[i]);
    value = height ? qa_js_max(value, glyph->metrics.height)
                   : fr(value + glyph->metrics.x_skip);
    count++;
  }
  return trunc(fr(value * use_scale));
}

double qa_q3_text_width(const qa_q3_font_set *fonts, const char *text, double scale,
                        double limit) {
  return text_metric(fonts, text, scale, limit, false);
}
double qa_q3_text_height(const qa_q3_font_set *fonts, const char *text, double scale,
                         double limit) {
  return text_metric(fonts, text, scale, limit, true);
}

static const qa_vec4 COLORS[8] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1},
                                  {1, 1, 0, 1}, {0, 0, 1, 1}, {0, 1, 1, 1},
                                  {1, 0, 1, 1}, {1, 1, 1, 1}};

static qa_vec4 escape_color(int code, double alpha) {
  qa_vec4 color = COLORS[(code - 48) & 7];
  color.w = alpha;
  return color;
}
static qa_vec4 black(double alpha) {
  qa_vec4 color = {0, 0, 0, alpha};
  return color;
}
static bool trunc_is_odd(double value) {
  return fmod(fabs(trunc(value)), 2.0) == 1.0;
}
static double pulse(double time) {
  if (!isfinite(time) || trunc(time) != time || time < -2147483648.0 ||
      time > 2147483647.0)
    qa_range_error("Font pulse time requires a signed int32");
  return fr(0.5 + fr(0.5 * fr(sin(fr(trunc(time / 75))))));
}
static double aligned_x(double x, double width, int32_t style) {
  double subtract = (style & 7) == QA_UI_CENTER ? trunc(width / 2)
                    : (style & 7) == QA_UI_RIGHT ? width
                                                 : 0;
  return trunc(x) - subtract;
}

void qa_q3_ui_asset_registry_init(qa_q3_ui_asset_registry *registry,
                                  qa_font_asset_services resources,
                                  qa_font_print_fn print, void *print_ctx) {
  registry->resources = resources;
  registry->print = print;
  registry->print_ctx = print_ctx;
}
bool qa_q3_ui_asset_registry_register_picture(qa_q3_ui_asset_registry *registry,
                                              const char *path, const char *mode,
                                              qa_picture_asset *out) {
  return registry->resources.register_picture(registry->resources.ctx, path,
                                              mode == NULL ? "no-mip" : mode, out);
}
const qa_q3_registered_font *qa_q3_ui_asset_registry_register_font(
    qa_q3_ui_asset_registry *registry, const char *path, double point_size) {
  return (const qa_q3_registered_font *)registry->resources.register_font(
      registry->resources.ctx, path, point_size, registry->print,
      registry->print_ctx);
}
bool qa_q3_ui_asset_registry_load_legacy_fonts(qa_q3_ui_asset_registry *registry,
                                               qa_q3_legacy_fonts *out) {
  if (!qa_q3_ui_asset_registry_register_picture(registry, "gfx/2d/bigchars", NULL,
                                                &out->charset))
    return false;
  if (!qa_q3_ui_asset_registry_register_picture(registry, "menu/art/font1_prop.tga",
                                                NULL, &out->proportional))
    return false;
  if (!qa_q3_ui_asset_registry_register_picture(registry, "menu/art/font1_prop_glo.tga",
                                                NULL, &out->glow))
    return false;
  return qa_q3_ui_asset_registry_register_picture(registry, "menu/art/font2_prop.tga",
                                                  NULL, &out->banner);
}

void qa_q3_draw_char(const qa_draw2d *draw, const qa_picture_asset *charset, double x,
                     double y, double width, double height, int32_t code) {
  int32_t ch = code & 255;
  if (ch == 32) return;
  double s = (ch & 15) / 16.0, t = (ch >> 4) / 16.0;
  qa_rect rect = {x, y, width, height};
  qa_text_texture_rect uv = {s, t, s + 1.0 / 16.0, t + 1.0 / 16.0};
  qa_draw2d_stretch_pic(draw, rect, uv, charset);
}

static void cg_pass(const qa_draw2d *draw, const qa_picture_asset *charset,
                    const qa_q3_fixed_text_options *options, bool shadow,
                    double maximum) {
  const unsigned char *text = (const unsigned char *)options->text;
  size_t length = strlen(options->text);
  double x = trunc(options->x);
  double count = 0;
  qa_vec4 color = shadow ? black(options->color.w) : options->color;
  qa_draw2d_set_color(draw, &color);
  for (size_t i = 0; i < length && count < maximum; i++) {
    if (escape_at(text, length, i)) {
      if (!shadow && !options->force_color) {
        qa_vec4 escaped = escape_color(text[i + 1], options->color.w);
        qa_draw2d_set_color(draw, &escaped);
      }
      i++;
      continue;
    }
    qa_q3_draw_char(draw, charset, x + (shadow ? 2 : 0),
                    trunc(options->y) + (shadow ? 2 : 0), trunc(options->char_width),
                    trunc(options->char_height), text[i]);
    x += trunc(options->char_width);
    count++;
  }
}

void qa_q3_draw_cg_string(const qa_draw2d *draw, const qa_picture_asset *charset,
                          const qa_q3_fixed_text_options *options) {
  double maximum = options->max_chars <= 0 ? 32767 : trunc(options->max_chars);
  if (options->shadow) cg_pass(draw, charset, options, true, maximum);
  cg_pass(draw, charset, options, false, maximum);
  qa_draw2d_set_color(draw, NULL);
}

static double pulse_channel(double value, double t) {
  value = fr(value);
  return fr(qa_js_max(0, qa_js_min(1, fr(value + fr(t * fr(fr(fr(0.8) * value) - value))))));
}

static void ui_pass(const qa_draw2d *draw, const qa_picture_asset *charset,
                    const qa_q3_ui_text_options *options, double start, double width,
                    double height, double offset, qa_vec4 pass_color) {
  if (trunc(options->y) + offset < -height) return;
  const unsigned char *text = (const unsigned char *)options->text;
  size_t length = strlen(options->text);
  qa_draw2d_set_color(draw, &pass_color);
  qa_rect base = {start + offset, trunc(options->y) + offset, width, height};
  qa_rect rect = qa_draw2d_adjust(draw, base);
  double x = rect.x;
  for (size_t i = 0; i < length; i++) {
    if (escape_at(text, length, i)) {
      qa_vec4 escaped = escape_color(text[++i], pass_color.w);
      qa_draw2d_set_color(draw, &escaped);
      continue;
    }
    int32_t ch = (int8_t)text[i];
    double s = (ch & 15) / 16.0, t = (ch >> 4) / 16.0;
    if (ch != 32) {
      qa_rect at = rect;
      at.x = x;
      qa_text_texture_rect uv = {s, t, s + 1.0 / 16.0, t + 1.0 / 16.0};
      qa_draw2d_stretch_pixels(draw, at, uv, charset);
    }
    x = fr(x + rect.width);
  }
  qa_draw2d_set_color(draw, NULL);
}

void qa_q3_draw_ui_string(const qa_draw2d *draw, const qa_picture_asset *charset,
                          const qa_q3_ui_text_options *options) {
  if ((options->style & QA_UI_BLINK) != 0 && trunc_is_odd(options->time / 200)) return;
  size_t length = strlen(options->text);
  double width = (options->style & QA_UI_SMALLFONT) != 0 ? 8
                 : (options->style & QA_UI_GIANTFONT) != 0 ? 32
                                                           : 16;
  double height = (options->style & QA_UI_GIANTFONT) != 0 &&
                          (options->style & QA_UI_SMALLFONT) == 0
                      ? 48
                      : 16;
  qa_vec4 color = options->color;
  if ((options->style & QA_UI_PULSE) != 0) {
    double t = pulse(options->time);
    color.x = pulse_channel(color.x, t);
    color.y = pulse_channel(color.y, t);
    color.z = pulse_channel(color.z, t);
    color.w = pulse_channel(color.w, t);
  }
  double start = aligned_x(options->x, (double)length * width, options->style);
  if ((options->style & QA_UI_DROPSHADOW) != 0)
    ui_pass(draw, charset, options, start, width, height, 2, black(color.w));
  ui_pass(draw, charset, options, start, width, height, 0, color);
}

static void atlas_pass(const qa_draw2d *draw, const qa_picture_asset *picture,
                       const char *input, double x, double y, qa_vec4 color,
                       double size, bool banner, qa_q3_font_profile profile) {
  const unsigned char *text = (const unsigned char *)input;
  size_t length = strlen(input);
  qa_draw2d_set_color(draw, &color);
  qa_rect origin = {x, y, 0, 0};
  qa_rect position = qa_draw2d_adjust(draw, origin);
  double scale_x = qa_draw2d_scale_x(draw);
  double vertical_scale =
      profile == QA_Q3_FONT_CGAME ? scale_x : qa_draw2d_scale_y(draw);
  double top = profile == QA_Q3_FONT_CGAME ? fr(y * scale_x) : position.y;
  double ax = position.x, aw = 0;
  double gap = fr(fr((banner ? 4 : 3) * scale_x) * size);
  double height = fr(fr((banner ? 36 : 27) * vertical_scale) * size);
  for (size_t i = 0; i < length; i++) {
    int32_t code = text[i] & 127;
    if (banner && code != 32 && (code < 65 || code > 90)) continue;
    qa_q3_atlas_metric metric =
        banner && code != 32 ? banner_metric(code) : qa_q3_prop_metric(code);
    if (code == 32) {
      aw = fr(fr((banner ? 12 : 8) * scale_x) * size);
    } else if (metric.values[2] != -1) {
      aw = fr(fr(metric.values[2] * scale_x) * size);
      qa_rect rect = {ax, top, aw, height};
      qa_text_texture_rect uv = {metric.values[0] / 256.0, metric.values[1] / 256.0,
                                 (metric.values[0] + metric.values[2]) / 256.0,
                                 (metric.values[1] + (banner ? 36 : 27)) / 256.0};
      qa_draw2d_stretch_pixels(draw, rect, uv, picture);
    } else if (profile == QA_Q3_FONT_CGAME) {
      aw = 0;
    }
    ax = fr(ax + fr(aw + gap));
  }
  qa_draw2d_set_color(draw, NULL);
}

static void proportional_text(const qa_draw2d *draw, const qa_q3_legacy_fonts *fonts,
                              const qa_q3_ui_text_options *options,
                              qa_q3_font_profile profile) {
  double size = (options->style & QA_UI_SMALLFONT) != 0 ? 0.75 : 1;
  double x = aligned_x(options->x,
                       trunc((double)qa_q3_proportional_string_width(options->text) *
                             size),
                       options->style);
  double y = trunc(options->y);
  if ((options->style & QA_UI_DROPSHADOW) != 0)
    atlas_pass(draw, &fonts->proportional, options->text, x + 2, y + 2,
               black(options->color.w), size, false, profile);
  if ((options->style & QA_UI_INVERSE) != 0) {
    double inverse = fr(profile == QA_Q3_FONT_CGAME ? 0.8 : 0.7);
    qa_vec4 dark = {fr(fr(options->color.x) * inverse), fr(fr(options->color.y) * inverse),
                    fr(fr(options->color.z) * inverse), options->color.w};
    atlas_pass(draw, &fonts->proportional, options->text, x, y, dark, size, false,
               profile);
    return;
  }
  atlas_pass(draw, &fonts->proportional, options->text, x, y, options->color, size,
             false, profile);
  if ((options->style & QA_UI_PULSE) != 0) {
    qa_vec4 glow = options->color;
    glow.w = pulse(options->time);
    atlas_pass(draw, &fonts->glow, options->text, x, y, glow, size, false, profile);
  }
}

void qa_q3_draw_proportional_string(const qa_draw2d *draw,
                                    const qa_q3_legacy_fonts *fonts,
                                    const qa_q3_ui_text_options *options) {
  proportional_text(draw, fonts, options, QA_Q3_FONT_UI);
}
void qa_q3_draw_cg_proportional_string(const qa_draw2d *draw,
                                       const qa_q3_legacy_fonts *fonts,
                                       const qa_q3_ui_text_options *options) {
  proportional_text(draw, fonts, options, QA_Q3_FONT_CGAME);
}

static void banner_text(const qa_draw2d *draw, const qa_q3_legacy_fonts *fonts,
                        const qa_q3_ui_text_options *options,
                        qa_q3_font_profile profile) {
  double x = aligned_x(options->x, (double)qa_q3_banner_string_width(options->text),
                       options->style);
  double y = trunc(options->y);
  if ((options->style & QA_UI_DROPSHADOW) != 0)
    atlas_pass(draw, &fonts->banner, options->text, x + 2, y + 2,
               black(options->color.w), 1, true, profile);
  atlas_pass(draw, &fonts->banner, options->text, x, y, options->color, 1, true,
             profile);
}

void qa_q3_draw_banner_string(const qa_draw2d *draw, const qa_q3_legacy_fonts *fonts,
                              const qa_q3_ui_text_options *options) {
  banner_text(draw, fonts, options, QA_Q3_FONT_UI);
}
void qa_q3_draw_cg_banner_string(const qa_draw2d *draw,
                                 const qa_q3_legacy_fonts *fonts,
                                 const qa_q3_ui_text_options *options) {
  banner_text(draw, fonts, options, QA_Q3_FONT_CGAME);
}

static qa_text_texture_rect glyph_uv(const qa_q3_registered_glyph *glyph) {
  qa_text_texture_rect uv = {glyph->metrics.s, glyph->metrics.t, glyph->metrics.s2,
                             glyph->metrics.t2};
  return uv;
}
static void paint_glyph(const qa_draw2d *draw, const qa_q3_registered_glyph *glyph,
                        double x, double baseline, double scale) {
  if (glyph->picture == NULL) return;
  qa_rect rect = {x, fr(baseline - fr(scale * glyph->metrics.top)),
                  fr(glyph->metrics.image_width * scale),
                  fr(glyph->metrics.image_height * scale)};
  qa_draw2d_stretch_pic(draw, rect, glyph_uv(glyph), glyph->picture);
}
static double paint_maximum(size_t length, double limit) {
  return limit > 0 ? qa_js_min((double)length, trunc(limit)) : (double)length;
}

static void paint_text(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                       const qa_q3_text_paint_options *options,
                       const qa_q3_text_cursor *cursor) {
  const unsigned char *text = (const unsigned char *)options->text;
  size_t length = strlen(options->text);
  const qa_q3_registered_font *font = select_font(fonts, options->scale);
  double scale = fr(fr(options->scale) * font->glyph_scale);
  double maximum = paint_maximum(length, options->limit);
  double x = fr(options->x);
  qa_vec4 color = options->color;
  double count = 0;
  const qa_q3_registered_glyph *cursor_glyph =
      cursor == NULL ? NULL : glyph_at(font, qa_js_to_int32(cursor->character) & 255);
  bool cursor_visible = cursor != NULL && !trunc_is_odd(cursor->time / 200);
  qa_draw2d_set_color(draw, &color);
  for (size_t i = 0; i < length && count < maximum; i++) {
    if (escape_at(text, length, i)) {
      color = escape_color(text[++i], options->color.w);
      qa_draw2d_set_color(draw, &color);
      continue;
    }
    const qa_q3_registered_glyph *glyph = glyph_at(font, text[i]);
    double baseline = fr(options->y);
    if ((options->style == 3 || options->style == 6) && glyph->picture != NULL) {
      double offset = options->style == 3 ? 1 : 2;
      qa_vec4 shadow = black(color.w);
      qa_draw2d_set_color(draw, &shadow);
      qa_rect rect = {fr(x + offset),
                      fr(fr(baseline - fr(scale * glyph->metrics.top)) + offset),
                      fr(glyph->metrics.image_width * scale),
                      fr(glyph->metrics.image_height * scale)};
      qa_draw2d_stretch_pic(draw, rect, glyph_uv(glyph), glyph->picture);
      qa_draw2d_set_color(draw, &color);
    }
    paint_glyph(draw, glyph, x, baseline, scale);
    if (cursor_visible && cursor->position == count)
      paint_glyph(draw, cursor_glyph, x, baseline, scale);
    x = fr(x + fr(fr(glyph->metrics.x_skip * scale) +
                  (cursor == NULL ? fr(options->adjust) : 0)));
    count++;
  }
  if (cursor_visible && cursor->position == maximum)
    paint_glyph(draw, cursor_glyph, x, fr(options->y), scale);
  qa_draw2d_set_color(draw, NULL);
}

void qa_q3_text_paint(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                      const qa_q3_text_paint_options *options) {
  paint_text(draw, fonts, options, NULL);
}
void qa_q3_text_paint_with_cursor(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                                  const qa_q3_text_paint_options *options,
                                  qa_q3_text_cursor cursor) {
  qa_q3_text_paint_options adjusted = *options;
  adjusted.adjust = 0;
  paint_text(draw, fonts, &adjusted, &cursor);
}

double qa_q3_text_paint_limit(const qa_draw2d *draw, const qa_q3_font_set *fonts,
                              const qa_q3_text_paint_options *options, double max_x) {
  const unsigned char *text = (const unsigned char *)options->text;
  size_t length = strlen(options->text);
  qa_q3_font_set cgame = *fonts;
  cgame.profile = QA_Q3_FONT_CGAME;
  const qa_q3_registered_font *font = select_font(&cgame, options->scale);
  double scale = fr(fr(options->scale) * font->glyph_scale);
  double maximum = paint_maximum(length, options->limit);
  double x = fr(options->x), result = fr(max_x), count = 0;
  qa_vec4 color = options->color;
  qa_draw2d_set_color(draw, &color);
  for (size_t i = 0; i < length && count < maximum; i++) {
    if (escape_at(text, length, i)) {
      qa_vec4 escaped = escape_color(text[++i], options->color.w);
      qa_draw2d_set_color(draw, &escaped);
      continue;
    }
    if (fr(qa_q3_text_width(fonts, options->text + i, scale, 1) + x) > fr(max_x)) {
      result = 0;
      break;
    }
    const qa_q3_registered_glyph *glyph = glyph_at(font, text[i]);
    paint_glyph(draw, glyph, x, fr(options->y), scale);
    x = fr(x + fr(fr(glyph->metrics.x_skip * scale) + fr(options->adjust)));
    result = x;
    count++;
  }
  qa_draw2d_set_color(draw, NULL);
  return result;
}
