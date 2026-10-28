/* Port of src/content/q3/presentation/draw-tools.ts. GPL-2.0-or-later.
 * From id Software's code/cgame/cg_drawtools.c. The media dependency
 * is the TS-used pick: picture resolution plus the five graphics
 * shaders the tools read. */
#ifndef QA_CONTENT_Q3_PRESENTATION_DRAW_TOOLS_H
#define QA_CONTENT_Q3_PRESENTATION_DRAW_TOOLS_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/text/draw2d.h"
#include "qa/text/q3_font.h"
/* TS drawStrlen. C bytes are inherently <= 255. */
double qa_q3_pres_draw_strlen(const char *text);
/* TS re-exports from text/q3-font.ts. */
double qa_q3_pres_proportional_string_width(const char *text);
double qa_q3_pres_banner_string_width(const char *text);
double qa_q3_pres_proportional_size_scale(double style);
/* TS fadeColor: false is TS null. */
bool qa_q3_pres_fade_color(double time, double start_msec, double total_msec, qa_vec4 *out);
qa_vec4 qa_q3_pres_team_color(double team);
qa_vec4 qa_q3_pres_get_color_for_health(double health, double armor);
/* Missing snapshot aborts with the TS Error text. */
qa_vec4 qa_q3_pres_color_for_health(qa_q3_pres_client_game_state *state);
typedef struct qa_q3_pres_draw_tools_media {
  void *ctx;
  qa_material_picture (*picture)(void *ctx, const qa_q3_pres_scene_shader *shader);
  const qa_q3_pres_scene_shader *charset_shader, *white_shader, *charset_prop; /* nullable */
  const qa_q3_pres_scene_shader *charset_prop_glow, *charset_prop_b, *back_tile_shader; /* nullable */
} qa_q3_pres_draw_tools_media;
typedef struct qa_q3_pres_client_draw_tools {
  qa_draw2d draw;
  qa_q3_pres_draw_tools_media media;
} qa_q3_pres_client_draw_tools;
/* Non-stretch-640 draws abort with the TS Error text. */
void qa_q3_pres_client_draw_tools_init(qa_q3_pres_client_draw_tools *tools, qa_draw2d draw,
                                       qa_q3_pres_draw_tools_media media);
void qa_q3_pres_client_draw_tools_draw_proportional_string(qa_q3_pres_client_draw_tools *tools,
                                                           qa_q3_ui_text_options options);
void qa_q3_pres_client_draw_tools_draw_banner_string(qa_q3_pres_client_draw_tools *tools,
                                                     qa_q3_ui_text_options options);
qa_rect qa_q3_pres_client_draw_tools_adjust_from_640(qa_q3_pres_client_draw_tools *tools, qa_rect rect);
/* color NULL is TS null. */
void qa_q3_pres_client_draw_tools_fill_rect(qa_q3_pres_client_draw_tools *tools, qa_rect rect, const qa_vec4 *color);
void qa_q3_pres_client_draw_tools_draw_sides(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size);
void qa_q3_pres_client_draw_tools_draw_top_bottom(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size);
void qa_q3_pres_client_draw_tools_draw_rect(qa_q3_pres_client_draw_tools *tools, qa_rect rect, double size,
                                            const qa_vec4 *color);
void qa_q3_pres_client_draw_tools_draw_pic(qa_q3_pres_client_draw_tools *tools, qa_rect rect,
                                           const qa_q3_pres_scene_shader *shader);
void qa_q3_pres_client_draw_tools_draw_char(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                            double width, double height, double code);
void qa_q3_pres_client_draw_tools_draw_string_ext(qa_q3_pres_client_draw_tools *tools,
                                                  qa_q3_fixed_text_options options);
void qa_q3_pres_client_draw_tools_draw_big_string(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                  const char *text, double alpha);
void qa_q3_pres_client_draw_tools_draw_big_string_color(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                        const char *text, qa_vec4 color);
void qa_q3_pres_client_draw_tools_draw_small_string(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                    const char *text, double alpha);
void qa_q3_pres_client_draw_tools_draw_small_string_color(qa_q3_pres_client_draw_tools *tools, double x, double y,
                                                          const char *text, qa_vec4 color);
void qa_q3_pres_client_draw_tools_tile_clear(qa_q3_pres_client_draw_tools *tools, const qa_q3_pres_refdef *refdef);
#endif
