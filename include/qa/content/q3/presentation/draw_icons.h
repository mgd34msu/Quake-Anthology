/* Port of src/content/q3/presentation/draw-icons.ts. GPL-2.0-or-later.
 * CG_Draw3DModel/DrawHead/DrawFlagModel/DrawTeamBackground from
 * id Software's cg_draw.c. Media is the TS-used pick of ClientMedia. */
#ifndef QA_CONTENT_Q3_PRESENTATION_DRAW_ICONS_H
#define QA_CONTENT_Q3_PRESENTATION_DRAW_ICONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
typedef struct qa_q3_pres_client_draw_icon_settings { bool draw_icons, draw_3d_icons; } qa_q3_pres_client_draw_icon_settings;
typedef struct qa_q3_pres_draw_icons_media {
  qa_q3_base_product product;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_renderer_resources resources;
  qa_q3_pres_scene_model red_flag_model, blue_flag_model, neutral_flag_model;
  const qa_q3_pres_scene_shader *defer_shader, *team_status_bar; /* nullable */
  void *items_ctx;
  const qa_q3_pres_packet_item_visual *(*items)(void *ctx, size_t *out_count);
} qa_q3_pres_draw_icons_media;
typedef struct qa_q3_pres_client_draw_icons {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_draw_tools *tools;
  qa_q3_pres_draw_icons_media media;
  void *settings_ctx;
  qa_q3_pres_client_draw_icon_settings (*settings)(void *ctx);
  const void *commands_token; /* TS commands identity vs tools.draw */
} qa_q3_pres_client_draw_icons;
/* Queue/product mismatches abort with the TS Error texts.
 * commands_token must equal tools->draw.commands.ctx. */
void qa_q3_pres_client_draw_icons_init(qa_q3_pres_client_draw_icons *icons, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_draw_tools *tools, qa_q3_pres_draw_icons_media media,
    qa_q3_pres_client_draw_icon_settings (*settings)(void *ctx), void *settings_ctx, const void *commands_token);
void qa_q3_pres_client_draw_icons_draw_3d_model(qa_q3_pres_client_draw_icons *icons, qa_rect rect,
    qa_q3_pres_scene_model model, const qa_q3_pres_scene_skin *skin, qa_vec3 origin, qa_vec3 angles);
void qa_q3_pres_client_draw_icons_draw_head(qa_q3_pres_client_draw_icons *icons, qa_rect rect, double client_num,
                                            qa_vec3 head_angles);
void qa_q3_pres_client_draw_icons_draw_flag_model(qa_q3_pres_client_draw_icons *icons, qa_rect rect, double team,
                                                  bool force_2d);
void qa_q3_pres_client_draw_icons_draw_team_background(qa_q3_pres_client_draw_icons *icons, qa_rect rect,
                                                       double alpha, double team);
#endif
