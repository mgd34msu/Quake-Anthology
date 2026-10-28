/* Port of src/content/q3/presentation/draw-tools.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/draw_tools.h"
#include <string.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static float f32(double v) { return (float)v; }
static qa_vec4 white(double alpha) { qa_vec4 c = {1, 1, 1, f32(alpha)}; return c; }
double qa_q3_pres_draw_strlen(const char *text) {
  size_t length = strlen(text);
  double count = 0;
  for (size_t i = 0; i < length; i++) {
    if (text[i] == '^' && i + 1 < length && text[i + 1] != '^') i++;
    else count++;
  }
  return count;
}
double qa_q3_pres_proportional_string_width(const char *text) { return (double)qa_q3_proportional_string_width(text); }
double qa_q3_pres_banner_string_width(const char *text) { return (double)qa_q3_banner_string_width(text); }
double qa_q3_pres_proportional_size_scale(double style) {
  return (qa_int32(style) & QA_UI_SMALLFONT) != 0 ? 0.75 : 1;
}
bool qa_q3_pres_fade_color(double time, double start_msec, double total_msec, qa_vec4 *out) {
  int32_t start = qa_int32(start_msec), total = qa_int32(total_msec);
  int32_t elapsed = qa_int32((double)qa_int32(time) - start);
  if (start == 0 || elapsed >= total) return false;
  int32_t remaining = qa_int32((double)total - elapsed);
  *out = white(remaining < 200 ? remaining / 200.0 : 1);
  return true;
}
qa_vec4 qa_q3_pres_team_color(double team) {
  qa_vec4 c;
  if (team == QA_Q3_BASE_TEAM_RED) { c = (qa_vec4){1, f32(0.2), f32(0.2), 1}; return c; }
  if (team == QA_Q3_BASE_TEAM_BLUE) { c = (qa_vec4){f32(0.2), f32(0.2), 1, 1}; return c; }
  if (team == QA_Q3_BASE_TEAM_SPECTATOR) { c = (qa_vec4){f32(0.7), f32(0.7), f32(0.7), 1}; return c; }
  return white(1);
}
qa_vec4 qa_q3_pres_get_color_for_health(double health_value, double armor_value) {
  int32_t health = qa_int32(health_value), armor = qa_int32(armor_value);
  if (health <= 0) { qa_vec4 c = {0, 0, 0, 1}; return c; }
  int32_t maximum = qa_int32(health * QA_Q3_BASE_ARMOR_PROTECTION / (1 - QA_Q3_BASE_ARMOR_PROTECTION));
  health = qa_int32((double)health + (armor < maximum ? armor : maximum));
  qa_vec4 c = {1,
               health > 60 ? 1 : health < 30 ? 0 : f32((health - 30) / 30.0),
               health >= 100 ? 1 : health < 66 ? 0 : f32((health - 66) / 33.0), 1};
  return c;
}
qa_vec4 qa_q3_pres_color_for_health(qa_q3_pres_client_game_state *state) {
  if (!state->snap) qa_range_error("CG_ColorForHealth requires a current snapshot");
  const qa_q3_base_player_state_slots *stats = &state->snap->player_state.stats;
  const qa_q3_base_stat_schema *schema =
      qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(&state->snap->player_state));
  double health = qa_q3_base_player_state_slots_get(stats, schema->health);
  double armor = qa_q3_base_player_state_slots_get(stats, schema->armor);
  return qa_q3_pres_get_color_for_health(health, armor);
}
void qa_q3_pres_client_draw_tools_init(qa_q3_pres_client_draw_tools *tools, qa_draw2d draw,
                                       qa_q3_pres_draw_tools_media media) {
  if (draw.space != QA_COORD_STRETCH_640) qa_range_error("Cgame drawing requires stretch-640 coordinates");
  tools->draw = draw;
  tools->media = media;
}
static qa_picture_asset picture_of(qa_q3_pres_client_draw_tools *tools, const qa_q3_pres_scene_shader *shader) {
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_MATERIAL;
  asset.material = tools->media.picture(tools->media.ctx, shader);
  return asset;
}
static qa_q3_legacy_fonts legacy_fonts(qa_q3_pres_client_draw_tools *tools) {
  qa_q3_legacy_fonts fonts;
  fonts.charset = picture_of(tools, tools->media.charset_shader);
  fonts.proportional = picture_of(tools, tools->media.charset_prop);
  fonts.glow = picture_of(tools, tools->media.charset_prop_glow);
  fonts.banner = picture_of(tools, tools->media.charset_prop_b);
  return fonts;
}
void qa_q3_pres_client_draw_tools_draw_proportional_string(qa_q3_pres_client_draw_tools *tools,
                                                           qa_q3_ui_text_options options) {
  qa_q3_legacy_fonts fonts = legacy_fonts(tools);
  qa_q3_draw_cg_proportional_string(&tools->draw, &fonts, &options);
}
void qa_q3_pres_client_draw_tools_draw_banner_string(qa_q3_pres_client_draw_tools *tools,
                                                     qa_q3_ui_text_options options) {
  qa_q3_legacy_fonts fonts = legacy_fonts(tools);
  qa_q3_draw_cg_banner_string(&tools->draw, &fonts, &options);
}
qa_rect qa_q3_pres_client_draw_tools_adjust_from_640(qa_q3_pres_client_draw_tools *tools, qa_rect rect) {
  return qa_draw2d_adjust(&tools->draw, rect);
}
static const qa_text_texture_rect ZERO_UV = {0, 0, 0, 0};
void qa_q3_pres_client_draw_tools_fill_rect(qa_q3_pres_client_draw_tools *tools, qa_rect rect, const qa_vec4 *color) {
  qa_picture_asset picture = picture_of(tools, tools->media.white_shader);
  qa_draw2d_set_color(&tools->draw, color);
  qa_draw2d_stretch_pic(&tools->draw, rect, ZERO_UV, &picture);
  qa_draw2d_set_color(&tools->draw, NULL);
}
void qa_q3_pres_client_draw_tools_draw_sides(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size) {
  qa_rect r = qa_q3_pres_client_draw_tools_adjust_from_640(tools, rect);
  double width = f32(f32(size) * qa_draw2d_scale_x(&tools->draw));
  qa_picture_asset picture = picture_of(tools, tools->media.white_shader);
  qa_rect left = r;
  left.width = width;
  qa_draw2d_stretch_pixels(&tools->draw, left, ZERO_UV, &picture);
  qa_rect right = r;
  right.x = f32(f32(r.x + r.width) - width);
  right.width = width;
  qa_draw2d_stretch_pixels(&tools->draw, right, ZERO_UV, &picture);
}
void qa_q3_pres_client_draw_tools_draw_top_bottom(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size) {
  qa_rect r = qa_q3_pres_client_draw_tools_adjust_from_640(tools, rect);
  double height = f32(f32(size) * qa_draw2d_scale_y(&tools->draw));
  qa_picture_asset picture = picture_of(tools, tools->media.white_shader);
  qa_rect top = r;
  top.height = height;
  qa_draw2d_stretch_pixels(&tools->draw, top, ZERO_UV, &picture);
  qa_rect bottom = r;
  bottom.y = f32(f32(r.y + r.height) - height);
  bottom.height = height;
  qa_draw2d_stretch_pixels(&tools->draw, bottom, ZERO_UV, &picture);
}
void qa_q3_pres_client_draw_tools_draw_rect(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size,
                                            const qa_vec4 *color) {
  qa_draw2d_set_color(&tools->draw, color);
  qa_q3_pres_client_draw_tools_draw_top_bottom(tools, rect, size);
  qa_q3_pres_client_draw_tools_draw_sides(tools, rect, size);
  qa_draw2d_set_color(&tools->draw, NULL);
}
void qa_q3_pres_client_draw_tools_draw_pic(qa_q3_pres_client_draw_tools *tools, qa_rect rect,
                                           const qa_q3_pres_scene_shader *shader) {
  qa_picture_asset picture = picture_of(tools, shader);
  qa_draw2d_draw_pic(&tools->draw, rect, &picture);
}
void qa_q3_pres_client_draw_tools_draw_char(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                            double width, double height, double code) {
  if ((qa_int32(code) & 255) == 32) return;
  qa_picture_asset picture = picture_of(tools, tools->media.charset_shader);
  qa_q3_draw_char(&tools->draw, &picture, qa_int32(x), qa_int32(y), qa_int32(width), qa_int32(height), qa_int32(code));
}
void qa_q3_pres_client_draw_tools_draw_string_ext(qa_q3_pres_client_draw_tools *tools,
                                                  qa_q3_fixed_text_options options) {
  qa_picture_asset picture = picture_of(tools, tools->media.charset_shader);
  qa_q3_draw_cg_string(&tools->draw, &picture, &options);
}
static void fixed_string(qa_q3_pres_client_draw_tools *tools, double x, double y, const char *text, qa_vec4 color,
                         bool force, bool shadow, double cw, double ch) {
  qa_q3_fixed_text_options options = {x, y, text, color, cw, ch, 0, force, shadow};
  qa_q3_pres_client_draw_tools_draw_string_ext(tools, options);
}
void qa_q3_pres_client_draw_tools_draw_big_string(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                  const char *text, double alpha) {
  fixed_string(tools, x, y, text, white(alpha), false, true, 16, 16);
}
void qa_q3_pres_client_draw_tools_draw_big_string_color(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                        const char *text, qa_vec4 color) {
  fixed_string(tools, x, y, text, color, true, true, 16, 16);
}
void qa_q3_pres_client_draw_tools_draw_small_string(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                    const char *text, double alpha) {
  fixed_string(tools, x, y, text, white(alpha), false, false, 8, 16);
}
void qa_q3_pres_client_draw_tools_draw_small_string_color(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                          const char *text, qa_vec4 color) {
  fixed_string(tools, x, y, text, color, true, false, 8, 16);
}
static void tile_box(qa_q3_pres_client_draw_tools *tools, double x, double y, double width, double height) {
  qa_picture_asset picture = picture_of(tools, tools->media.back_tile_shader);
  qa_rect rect = {x, y, width, height};
  qa_text_texture_rect uv = {x / 64, y / 64, (x + width) / 64, (y + height) / 64};
  qa_draw2d_stretch_pixels(&tools->draw, rect, uv, &picture);
}
void qa_q3_pres_client_draw_tools_tile_clear(qa_q3_pres_client_draw_tools *tools, const qa_q3_pres_refdef *refdef) {
  double width = qa_draw2d_width(&tools->draw), height = qa_draw2d_height(&tools->draw);
  if (refdef->x == 0 && refdef->y == 0 && refdef->width == width && refdef->height == height) return;
  double top = qa_int32(refdef->y);
  double bottom = qa_int32(top + qa_int32(refdef->height) - 1);
  double left = qa_int32(refdef->x);
  double right = qa_int32(left + qa_int32(refdef->width) - 1);
  tile_box(tools, 0, 0, width, top);
  tile_box(tools, 0, bottom, width, height - bottom);
  tile_box(tools, 0, top, left, bottom - top + 1);
  tile_box(tools, right, top, width - right, bottom - top + 1);
}
