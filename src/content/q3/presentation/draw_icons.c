/* Port of src/content/q3/presentation/draw-icons.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/draw_icons.h"
#include <math.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
static float f32(double v) { return (float)v; }
static qa_vec3 icon_origin(qa_bounds bounds, double fraction) {
  double length = f32(f32(fraction) * f32(bounds.max.z - bounds.min.z));
  qa_vec3 origin = {f32(length / f32(0.268)), f32(0.5 * f32(bounds.min.y + bounds.max.y)),
                    f32(-0.5 * f32(bounds.min.z + bounds.max.z))};
  return origin;
}
void qa_q3_pres_client_draw_icons_init(qa_q3_pres_client_draw_icons *icons, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_draw_tools *tools, qa_q3_pres_draw_icons_media media,
    qa_q3_pres_client_draw_icon_settings (*settings)(void *ctx), void *settings_ctx, const void *commands_token) {
  if (tools->draw.commands.ctx != commands_token)
    qa_range_error("Draw icons and command buffer must share the engine drawing queue");
  if (state->product != media.product) qa_range_error("Draw icons and media products differ");
  icons->state = state;
  icons->tools = tools;
  icons->media = media;
  icons->settings = settings;
  icons->settings_ctx = settings_ctx;
  icons->commands_token = commands_token;
}
void qa_q3_pres_client_draw_icons_draw_3d_model(qa_q3_pres_client_draw_icons *icons, qa_rect rect,
    qa_q3_pres_scene_model model, const qa_q3_pres_scene_skin *skin, qa_vec3 origin, qa_vec3 angles) {
  qa_q3_pres_client_draw_icon_settings settings = icons->settings(icons->settings_ctx);
  if (!settings.draw_3d_icons || !settings.draw_icons) return;
  qa_rect viewport = qa_q3_pres_client_draw_tools_adjust_from_640(icons->tools, rect);
  qa_q3_pres_refdef refdef = qa_q3_pres_create_refdef();
  qa_q3_pres_ref_model_entity entity = qa_q3_pres_create_model_entity(model);
  entity.axis = qa_qvm_angles_to_axis(angles);
  entity.origin = origin;
  entity.custom_skin = skin;
  entity.shading.render_flags = QA_Q3_PRES_RF_NOSHADOW;
  refdef.render_flags = QA_Q3_PRES_RDF_NOWORLDMODEL;
  refdef.view_axis = (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  refdef.fov_x = 30;
  refdef.fov_y = 30;
  refdef.time = icons->state->time;
  refdef.x = trunc(viewport.x);
  refdef.y = trunc(viewport.y);
  refdef.width = trunc(viewport.width);
  refdef.height = trunc(viewport.height);
  qa_q3_pres_renderer_resources *scene = &icons->media.resources;
  scene->clear_scene(scene->ctx);
  qa_q3_pres_admitted_ref_entity admitted;
  admitted.is_poly_record = false;
  admitted.entity = qa_q3_pres_ref_entity_from_model(entity);
  scene->add_ref_entity(scene->ctx, &admitted);
  scene->render_scene(scene->ctx, &refdef);
}
void qa_q3_pres_client_draw_icons_draw_head(qa_q3_pres_client_draw_icons *icons, qa_rect rect, double client_num,
                                            qa_vec3 head_angles) {
  if (client_num != trunc(client_num) || client_num < 0 || client_num >= QA_Q3_PRES_MAX_CLIENTS)
    qa_range_error("CG_DrawHead: invalid client number");
  qa_q3_pres_client_info *client = &icons->media.static_state->client_info[(size_t)client_num];
  qa_q3_pres_client_draw_icon_settings settings = icons->settings(icons->settings_ctx);
  if (settings.draw_3d_icons) {
    if (client->head_model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
      qa_vec3 origin = qa_add3(icon_origin(qa_q3_pres_model_bounds(&client->head_model), 0.7), client->head_offset);
      qa_q3_pres_client_draw_icons_draw_3d_model(icons, rect, client->head_model, client->head_skin, origin,
                                                 head_angles);
    } else {
      /* TS returns here only when the 3D branch runs; default model returns. */
      if (client->deferred)
        qa_q3_pres_client_draw_tools_draw_pic(icons->tools, rect, icons->media.defer_shader);
      return;
    }
  } else if (settings.draw_icons) {
    qa_q3_pres_client_draw_tools_draw_pic(icons->tools, rect, client->model_icon);
  }
  if (client->deferred) qa_q3_pres_client_draw_tools_draw_pic(icons->tools, rect, icons->media.defer_shader);
}
void qa_q3_pres_client_draw_icons_draw_flag_model(qa_q3_pres_client_draw_icons *icons, qa_rect rect, double team,
                                                  bool force_2d) {
  qa_q3_pres_client_draw_icon_settings settings = icons->settings(icons->settings_ctx);
  if (!force_2d && settings.draw_3d_icons) {
    qa_vec3 origin = icon_origin(qa_q3_pres_model_bounds(&icons->media.red_flag_model), 0.5);
    qa_vec3 angles = {0, f32(60 * f32(sin(f32(f32(icons->state->time) / 2000)))), 0};
    qa_q3_pres_scene_model model;
    if (team == QA_Q3_BASE_TEAM_RED) model = icons->media.red_flag_model;
    else if (team == QA_Q3_BASE_TEAM_BLUE) model = icons->media.blue_flag_model;
    else if (team == QA_Q3_BASE_TEAM_FREE) model = icons->media.neutral_flag_model;
    else return;
    qa_q3_pres_client_draw_icons_draw_3d_model(icons, rect, model, NULL, origin, angles);
  } else if (settings.draw_icons) {
    qa_q3_powerup powerup;
    if (team == QA_Q3_BASE_TEAM_RED) powerup = QA_Q3_PW_REDFLAG;
    else if (team == QA_Q3_BASE_TEAM_BLUE) powerup = QA_Q3_PW_BLUEFLAG;
    else if (team == QA_Q3_BASE_TEAM_FREE) powerup = QA_Q3_PW_NEUTRALFLAG;
    else return;
    const qa_q3_base_item_definition *item =
        qa_q3_base_find_item_for_powerup(icons->media.product, powerup);
    if (item) {
      size_t count = 0;
      const qa_q3_base_item_definition *items = qa_q3_base_item_list(icons->media.product, &count);
      size_t index = (size_t)(item - items);
      size_t visual_count = 0;
      const qa_q3_pres_packet_item_visual *visuals = icons->media.items(icons->media.items_ctx, &visual_count);
      if (index >= visual_count) qa_range_error("Flag item has no cgame visual slot");
      (void)visuals;
      qa_q3_pres_client_draw_tools_draw_pic(icons->tools, rect, visuals[index].icon);
    }
  }
}
void qa_q3_pres_client_draw_icons_draw_team_background(qa_q3_pres_client_draw_icons *icons, qa_rect rect,
                                                       double alpha, double team) {
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return;
  qa_vec4 color = {team == QA_Q3_BASE_TEAM_RED ? 1 : 0, 0, team == QA_Q3_BASE_TEAM_BLUE ? 1 : 0, f32(alpha)};
  qa_draw2d_set_color(&icons->tools->draw, &color);
  qa_rect whole = {trunc(rect.x), trunc(rect.y), trunc(rect.width), trunc(rect.height)};
  qa_q3_pres_client_draw_tools_draw_pic(icons->tools, whole, icons->media.team_status_bar);
  qa_draw2d_set_color(&icons->tools->draw, NULL);
}
