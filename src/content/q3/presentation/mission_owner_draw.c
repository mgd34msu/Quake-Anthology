/* Port of src/content/q3/presentation/mission-owner-draw.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mission_owner_draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/events.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
const char *qa_q3_pres_mission_owner_draw_id_name(qa_q3_pres_mission_owner_draw_id id) {
  switch (id) {
    case QA_Q3_PRES_OD_PLAYER_ARMOR_ICON: return "CG_PLAYER_ARMOR_ICON";
    case QA_Q3_PRES_OD_PLAYER_HEALTH: return "CG_PLAYER_HEALTH";
    case QA_Q3_PRES_OD_PLAYER_SCORE: return "CG_PLAYER_SCORE";
    case QA_Q3_PRES_OD_GAME_TYPE: return "CG_GAME_TYPE";
    case QA_Q3_PRES_OD_GAME_STATUS: return "CG_GAME_STATUS";
    case QA_Q3_PRES_OD_KILLER: return "CG_KILLER";
    default: return "CG_OWNERDRAW";
  }
}
void qa_q3_pres_mission_owner_draw_init(qa_q3_pres_mission_owner_draw *draw,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_mission_owner_draw_host host) {
  draw->state = state;
  draw->static_state = static_state;
  draw->host = host;
}
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_mission_owner_draw *draw) {
  return draw->host.icons->tools;
}
static qa_q3_base_source_player_state *snapshot_ps(qa_q3_pres_mission_owner_draw *draw) {
  return draw->state->snap ? &draw->state->snap->player_state : NULL;
}
static double player_team(qa_q3_pres_mission_owner_draw *draw) {
  qa_q3_base_source_player_state *ps = snapshot_ps(draw);
  return ps ? qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) : -1;
}
static bool team_has_flag(qa_q3_pres_mission_owner_draw *draw, bool other) {
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  if (cgs->game_type != QA_Q3_BASE_GT_1FCTF && cgs->game_type != QA_Q3_BASE_GT_CTF) return false;
  double team = player_team(draw);
  if (cgs->game_type == QA_Q3_BASE_GT_1FCTF)
    return other ? ((team == QA_Q3_BASE_TEAM_RED && cgs->flag_status == 3) ||
                    (team == QA_Q3_BASE_TEAM_BLUE && cgs->flag_status == 2))
                 : ((team == QA_Q3_BASE_TEAM_RED && cgs->flag_status == 2) ||
                    (team == QA_Q3_BASE_TEAM_BLUE && cgs->flag_status == 3));
  return other ? ((team == QA_Q3_BASE_TEAM_RED && cgs->redflag == 1) ||
                  (team == QA_Q3_BASE_TEAM_BLUE && cgs->blueflag == 1))
               : ((team == QA_Q3_BASE_TEAM_RED && cgs->blueflag == 1) ||
                  (team == QA_Q3_BASE_TEAM_BLUE && cgs->redflag == 1));
}
bool qa_q3_pres_mission_owner_draw_other_team_has_flag(qa_q3_pres_mission_owner_draw *draw) {
  return team_has_flag(draw, true);
}
bool qa_q3_pres_mission_owner_draw_your_team_has_flag(qa_q3_pres_mission_owner_draw *draw) {
  return team_has_flag(draw, false);
}
static double owner_cvar(qa_q3_pres_mission_owner_draw *draw, const char *name) {
  qa_cvar_snapshot snapshot = draw->host.read_vm_cvar(draw->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static bool visible(qa_q3_pres_mission_owner_draw *draw, double flags) {
  int64_t bits = (int64_t)flags;
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  qa_q3_base_game_type type = cgs->game_type;
  if (bits & QA_Q3_PRES_OD_SHOW_TEAMINFO)
    return owner_cvar(draw, "cg_currentSelectedPlayer") == draw->state->num_sorted_team_players;
  if (bits & QA_Q3_PRES_OD_SHOW_NOTEAMINFO)
    return owner_cvar(draw, "cg_currentSelectedPlayer") != draw->state->num_sorted_team_players;
  if (bits & QA_Q3_PRES_OD_SHOW_OTHERTEAMHASFLAG) return qa_q3_pres_mission_owner_draw_other_team_has_flag(draw);
  if (bits & QA_Q3_PRES_OD_SHOW_YOURTEAMHASENEMYFLAG)
    return qa_q3_pres_mission_owner_draw_your_team_has_flag(draw);
  if (bits & (QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG | QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG))
    return ((bits & QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG) != 0 && (cgs->redflag == 1 || cgs->flag_status == 2)) ||
           ((bits & QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG) != 0 && (cgs->blueflag == 1 || cgs->flag_status == 3));
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYTEAMGAME) && type >= QA_Q3_BASE_GT_TEAM) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYNONTEAMGAME) && type < QA_Q3_BASE_GT_TEAM) return true;
  if (bits & QA_Q3_PRES_OD_SHOW_HARVESTER) return type == QA_Q3_BASE_GT_HARVESTER;
  if (bits & QA_Q3_PRES_OD_SHOW_ONEFLAG) return type == QA_Q3_BASE_GT_1FCTF;
  if ((bits & QA_Q3_PRES_OD_SHOW_CTF) && type == QA_Q3_BASE_GT_CTF) return true;
  if (bits & QA_Q3_PRES_OD_SHOW_OBELISK) return type == QA_Q3_BASE_GT_OBELISK;
  qa_q3_base_source_player_state *ps = snapshot_ps(draw);
  const qa_q3_base_stat_schema *mission = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  if (ps && (bits & QA_Q3_PRES_OD_SHOW_HEALTHCRITICAL) &&
      qa_q3_base_player_state_slots_get(&ps->stats, mission->health) < 25) return true;
  if (ps && (bits & QA_Q3_PRES_OD_SHOW_HEALTHOK) &&
      qa_q3_base_player_state_slots_get(&ps->stats, mission->health) >= 25) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_SINGLEPLAYER) && type == QA_Q3_BASE_GT_SINGLE_PLAYER) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_TOURNAMENT) && type == QA_Q3_BASE_GT_TOURNAMENT) return true;
  if (ps && (bits & QA_Q3_PRES_OD_SHOW_IF_PLAYER_HAS_FLAG))
    return qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_REDFLAG) != 0 ||
           qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_BLUEFLAG) != 0 ||
           qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_NEUTRALFLAG) != 0;
  return false;
}
static double selected_index(qa_q3_pres_mission_owner_draw *draw) {
  double slot = draw->host.selected_player(draw->host.ctx);
  if (slot != trunc(slot) || slot < 0 || slot >= 8)
    qa_range_error("Invalid owner-draw slot %lld", (long long)qa_int32(slot));
  return draw->state->sorted_team_players[(size_t)slot];
}
static qa_q3_pres_client_info *client_at(qa_q3_pres_mission_owner_draw *draw, double index) {
  if (index != trunc(index) || index < 0 || index >= 64)
    qa_range_error("Invalid owner-draw slot %lld", (long long)qa_int32(index));
  return &draw->static_state->client_info[(size_t)index];
}
static const char *location_text(qa_q3_pres_mission_owner_draw *draw, double index) {
  const char *text = draw->host.config_string(draw->host.ctx, 608 + index);
  return text && text[0] != '\0' ? text : "unknown";
}
static const qa_q3_pres_scene_shader *status_handle(qa_q3_pres_mission_owner_draw *draw, double task) {
  const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
  if (!g) return NULL;
  switch ((int)task) {
    case 2: return g->defend_shader;
    case 3: return g->patrol_shader;
    case 4: return g->follow_shader;
    case 5: return g->retrieve_shader;
    case 6: return g->escort_shader;
    case 7: return g->camp_shader;
    default: return g->assault_shader;
  }
}
static void paint_text(qa_q3_pres_mission_owner_draw *draw, const qa_ui_owner_draw_paint_request *request,
                       const char *text) {
  const qa_q3_font_set *fonts = draw->host.fonts(draw->host.ctx);
  double width = qa_q3_text_width(fonts, text, request->text_scale, 0);
  double x = request->text_x;
  if (request->alignment == 1) x -= width / 2;
  else if (request->alignment == 2) x -= width;
  qa_q3_text_paint_options options = {x, request->text_y, request->text_scale, request->color, text, 0, 0,
                                      (int32_t)request->text_style};
  qa_q3_text_paint(&tools_of(draw)->draw, fonts, &options);
}
static void paint_number(qa_q3_pres_mission_owner_draw *draw, const qa_ui_owner_draw_paint_request *request,
                         double value) {
  char text[32];
  snprintf(text, sizeof text, "%lld", (long long)qa_int32(value));
  paint_text(draw, request, text);
}
static const char *game_type_text(qa_q3_base_game_type type) {
  switch (type) {
    case QA_Q3_BASE_GT_FFA: return "Free For All";
    case QA_Q3_BASE_GT_TEAM: return "Team Deathmatch";
    case QA_Q3_BASE_GT_CTF: return "Capture the Flag";
    case QA_Q3_BASE_GT_1FCTF: return "One Flag CTF";
    case QA_Q3_BASE_GT_OBELISK: return "Overload";
    case QA_Q3_BASE_GT_HARVESTER: return "Harvester";
    default: return "";
  }
}
static void game_status_text(qa_q3_pres_mission_owner_draw *draw, char *out, size_t size) {
  qa_q3_base_source_player_state *ps = snapshot_ps(draw);
  if (draw->static_state->game_type < QA_Q3_BASE_GT_TEAM) {
    if (!ps || player_team(draw) == QA_Q3_BASE_TEAM_SPECTATOR) {
      snprintf(out, size, "%s", "");
      return;
    }
    char *place = qa_q3_pres_place_string(
        (double)qa_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_RANK) + 1));
    /* TS template interpolates the raw PERS_SCORE number. */
    double score = qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE);
    if (score == trunc(score))
      snprintf(out, size, "%s place with %lld", place ? place : "", (long long)score);
    else
      snprintf(out, size, "%s place with %g", place ? place : "", score);
    free(place);
    return;
  }
  double red = draw->state->team_scores[0], blue = draw->state->team_scores[1];
  if (red == blue) snprintf(out, size, "Teams are tied at %lld", (long long)red);
  else if (red >= blue) snprintf(out, size, "Red leads Blue, %lld to %lld", (long long)red, (long long)blue);
  else snprintf(out, size, "Blue leads Red, %lld to %lld", (long long)blue, (long long)red);
}
void qa_q3_pres_mission_owner_draw_paint(qa_q3_pres_mission_owner_draw *draw,
                                         const qa_ui_owner_draw_paint_request *request) {
  if (request->draw != NULL && request->draw != &tools_of(draw)->draw)
    qa_range_error("Mission owner drawing must share the ordered HUD recorder");
  if (owner_cvar(draw, "cg_drawStatus") == 0) return;
  if (request->owner_draw_flags != 0 && !visible(draw, request->owner_draw_flags)) return;
  qa_q3_base_source_player_state *ps = snapshot_ps(draw);
  qa_q3_pres_client_game_state *state = draw->state;
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  int id = (int)request->owner_draw;
  const qa_q3_base_stat_schema *schema = ps
      ? qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps)) : NULL;
  switch (id) {
    case QA_Q3_PRES_OD_PLAYER_ARMOR_ICON:
    case QA_Q3_PRES_OD_PLAYER_ARMOR_ICON2D:
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect, draw->host.armor_icon);
      return;
    case QA_Q3_PRES_OD_PLAYER_ARMOR_VALUE:
      if (ps && schema) paint_number(draw, request, qa_q3_base_player_state_slots_get(&ps->stats, schema->armor));
      return;
    case QA_Q3_PRES_OD_PLAYER_HEALTH:
      if (ps && schema) paint_number(draw, request, qa_q3_base_player_state_slots_get(&ps->stats, schema->health));
      return;
    case QA_Q3_PRES_OD_PLAYER_AMMO_ICON:
    case QA_Q3_PRES_OD_PLAYER_AMMO_ICON2D:
      if (ps)
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
                                              draw->host.weapon_icon(draw->host.ctx, ps->weapon));
      return;
    case QA_Q3_PRES_OD_PLAYER_AMMO_VALUE:
      if (ps) {
        double ammo = qa_q3_base_player_state_slots_get(&ps->ammo, ps->weapon);
        if (ammo >= 0) paint_number(draw, request, ammo);
      }
      return;
    case QA_Q3_PRES_OD_PLAYER_HEAD: {
      if (!ps || !draw->host.crandom || !draw->host.random_value) return;
      double x = request->rect.x;
      if (state->damage_time != 0 && (float)((float)state->time - (float)state->damage_time) < 500) {
        double frac = (float)((float)((float)state->time - (float)state->damage_time) / 500);
        double size = (float)((float)(request->rect.width * 1.25) * (float)(1.5 - (float)(frac * 0.5)));
        double stretch = (float)(size - (float)(request->rect.width * 1.25));
        x = (float)(x - (float)((float)(stretch * 0.5) + (float)((float)(state->damage_x * stretch) * 0.5)));
        state->head_start_yaw = (float)(180 + (float)(state->damage_x * 45));
        state->head_end_yaw = (float)(180 + (float)(20 * (float)cos((float)(draw->host.crandom(draw->host.ctx) * (float)3.141592653589793))));
        state->head_end_pitch = (float)(5 * (float)cos((float)(draw->host.crandom(draw->host.ctx) * (float)3.141592653589793)));
        state->head_start_time = state->time;
        state->head_end_time = (double)qa_int32(trunc((float)((float)qa_int32(state->time + 100) +
                                                              (float)(draw->host.random_value(draw->host.ctx) * 2000))));
      } else if (state->time >= state->head_end_time) {
        state->head_start_yaw = state->head_end_yaw;
        state->head_start_pitch = state->head_end_pitch;
        state->head_start_time = state->head_end_time;
        state->head_end_time = (double)qa_int32(trunc((float)((float)qa_int32(state->time + 100) +
                                                              (float)(draw->host.random_value(draw->host.ctx) * 2000))));
        state->head_end_yaw = (float)(180 + (float)(20 * (float)cos((float)(draw->host.crandom(draw->host.ctx) * (float)3.141592653589793))));
        state->head_end_pitch = (float)(5 * (float)cos((float)(draw->host.crandom(draw->host.ctx) * (float)3.141592653589793)));
      }
      if (state->head_start_time > state->time) state->head_start_time = state->time;
      double frac = (float)((float)qa_int32(state->time - state->head_start_time) /
                            (float)qa_int32(state->head_end_time - state->head_start_time));
      frac = (float)((float)(frac * frac) * (float)(3 - (float)(2 * frac)));
      qa_rect rect = request->rect;
      rect.x = x;
      qa_vec3 angles = {(float)(state->head_start_pitch +
                                (float)((float)(state->head_end_pitch - state->head_start_pitch) * frac)),
                        (float)(state->head_start_yaw +
                                (float)((float)(state->head_end_yaw - state->head_start_yaw) * frac)), 0};
      qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, rect, ps->client_num, angles);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEAD:
    case QA_Q3_PRES_OD_VOICE_HEAD: {
      qa_vec3 angles = {0, 180, 0};
      qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, request->rect,
          id == QA_Q3_PRES_OD_VOICE_HEAD ? cgs->current_voice_client : selected_index(draw), angles);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_NAME:
    case QA_Q3_PRES_OD_VOICE_NAME: {
      qa_q3_pres_client_info *ci = client_at(draw,
          id == QA_Q3_PRES_OD_VOICE_NAME ? cgs->current_voice_client : selected_index(draw));
      paint_text(draw, request, ci->name ? ci->name : "");
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_LOCATION:
      paint_text(draw, request, location_text(draw, client_at(draw, selected_index(draw))->location));
      return;
    case QA_Q3_PRES_OD_PLAYER_LOCATION:
      if (ps) paint_text(draw, request, location_text(draw, client_at(draw, ps->client_num)->location));
      return;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEALTH:
      paint_number(draw, request, client_at(draw, selected_index(draw))->health);
      return;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_ARMOR: {
      qa_q3_pres_client_info *ci = client_at(draw, selected_index(draw));
      if (ci->armor > 0) paint_number(draw, request, ci->armor);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_STATUS: {
      qa_q3_pres_client_info *ci = client_at(draw, selected_index(draw));
      if (!(cgs->order_pending && state->time > (double)qa_int32(cgs->order_time - 2500) &&
            (((int64_t)state->time >> 9) & 1)))
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
                                              status_handle(draw, cgs->order_pending ? cgs->current_order
                                                                                       : ci->team_task));
      return;
    }
    case QA_Q3_PRES_OD_PLAYER_STATUS:
      if (ps)
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
                                              status_handle(draw, client_at(draw, ps->client_num)->team_task));
      return;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_WEAPON: {
      qa_q3_pres_client_info *ci = client_at(draw, selected_index(draw));
      const qa_q3_pres_scene_shader *icon = draw->host.weapon_icon
          ? draw->host.weapon_icon(draw->host.ctx, ci->cur_weapon) : NULL;
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
          icon ? icon : draw->host.graphics ? draw->host.graphics->defer_shader : NULL);
      return;
    }
    case QA_Q3_PRES_OD_PLAYER_SCORE:
      if (ps) paint_number(draw, request,
                           qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE));
      return;
    case QA_Q3_PRES_OD_BLUE_SCORE:
    case QA_Q3_PRES_OD_RED_SCORE: {
      double value = id == QA_Q3_PRES_OD_RED_SCORE ? cgs->scores1 : cgs->scores2;
      if (value == -9999) paint_text(draw, request, "-");
      else paint_number(draw, request, value);
      return;
    }
    case QA_Q3_PRES_OD_RED_NAME: paint_text(draw, request, (cgs->red_team ? cgs->red_team : "")); return;
    case QA_Q3_PRES_OD_BLUE_NAME: paint_text(draw, request, (cgs->blue_team ? cgs->blue_team : "")); return;
    case QA_Q3_PRES_OD_GAME_TYPE: paint_text(draw, request, game_type_text(cgs->game_type)); return;
    case QA_Q3_PRES_OD_GAME_STATUS: {
      char text[320];
      game_status_text(draw, text, sizeof text);
      paint_text(draw, request, text);
      return;
    }
    case QA_Q3_PRES_OD_KILLER: {
      if (state->killer_name && state->killer_name[0] != '\0') {
        char text[1088];
        snprintf(text, sizeof text, "Fragged by %s", state->killer_name);
        paint_text(draw, request, text);
      }
      return;
    }
    case QA_Q3_PRES_OD_CAPFRAGLIMIT: {
      char text[32];
      snprintf(text, sizeof text, "%2lld",
               (long long)qa_int32(cgs->game_type >= QA_Q3_BASE_GT_CTF ? cgs->capturelimit : cgs->fraglimit));
      paint_text(draw, request, text);
      return;
    }
    case QA_Q3_PRES_OD_1STPLACE:
    case QA_Q3_PRES_OD_2NDPLACE: {
      double value = id == QA_Q3_PRES_OD_1STPLACE ? cgs->scores1 : cgs->scores2;
      if (value != -9999) {
        char text[32];
        snprintf(text, sizeof text, "%2lld", (long long)qa_int32(value));
        paint_text(draw, request, text);
      }
      return;
    }
    case QA_Q3_PRES_OD_TEAM_COLOR:
      qa_q3_pres_client_draw_icons_draw_team_background(draw->host.icons, request->rect, request->color.w,
                                                        player_team(draw));
      return;
    case QA_Q3_PRES_OD_PLAYER_HASFLAG:
    case QA_Q3_PRES_OD_PLAYER_HASFLAG2D: {
      bool force_2d = id == QA_Q3_PRES_OD_PLAYER_HASFLAG2D;
      double adj = force_2d ? 0 : 2;
      qa_rect rect = {(float)(request->rect.x + adj), (float)(request->rect.y + adj),
                      (float)(request->rect.width - adj), (float)(request->rect.height - adj)};
      qa_q3_base_source_player_state *predicted = &state->predicted_player_state;
      if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_REDFLAG) != 0)
        qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, rect, QA_Q3_BASE_TEAM_RED, force_2d);
      else if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_BLUEFLAG) != 0)
        qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, rect, QA_Q3_BASE_TEAM_BLUE, force_2d);
      else if (qa_q3_base_player_state_slots_get(&predicted->powerups, QA_Q3_PW_NEUTRALFLAG) != 0)
        qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, rect, QA_Q3_BASE_TEAM_FREE, force_2d);
      return;
    }
    case QA_Q3_PRES_OD_BLUE_FLAGHEAD:
    case QA_Q3_PRES_OD_RED_FLAGHEAD:
    case QA_Q3_PRES_OD_BLUE_FLAGNAME:
    case QA_Q3_PRES_OD_RED_FLAGNAME: {
      bool blue = id == QA_Q3_PRES_OD_BLUE_FLAGHEAD || id == QA_Q3_PRES_OD_BLUE_FLAGNAME;
      double carrier = -1;
      for (double i = 0; i < cgs->maxclients && i < 64; i++) {
        qa_q3_pres_client_info *ci = client_at(draw, i);
        if (ci->info_valid && ci->team == (blue ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE) &&
            (((int64_t)ci->powerups & (1LL << (blue ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG))) != 0)) {
          carrier = i;
          break;
        }
      }
      if (carrier >= 0) {
        if (id == QA_Q3_PRES_OD_BLUE_FLAGNAME || id == QA_Q3_PRES_OD_RED_FLAGNAME)
          paint_text(draw, request, client_at(draw, carrier)->name ? client_at(draw, carrier)->name : "");
        else {
          qa_vec3 angles = {0, (float)(180 + (float)(20 * (float)sin((float)((float)state->time / 650)))), 0};
          qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, request->rect, 0, angles);
        }
      }
      return;
    }
    case QA_Q3_PRES_OD_HARVESTER_SKULLS:
    case QA_Q3_PRES_OD_HARVESTER_SKULLS2D: {
      if (cgs->game_type != QA_Q3_BASE_GT_HARVESTER || !ps) return;
      char text[32];
      snprintf(text, sizeof text, "%lld", (long long)qa_int32(fmin(ps->generic1, 99)));
      paint_text(draw, request, text);
      if (owner_cvar(draw, "cg_drawIcons") == 0 || !draw->host.graphics) return;
      bool red = player_team(draw) == QA_Q3_BASE_TEAM_BLUE;
      bool force_2d = id == QA_Q3_PRES_OD_HARVESTER_SKULLS2D;
      if (!force_2d && owner_cvar(draw, "cg_draw3dIcons") != 0) {
        qa_rect rect = {request->rect.x, request->rect.y, 35, 35};
        qa_vec3 origin = {90, 0, -10};
        qa_vec3 angles = {0, (float)(((int64_t)state->time & 2047) * 360.0 / 2048), 0};
        qa_q3_pres_client_draw_icons_draw_3d_model(draw->host.icons, rect,
            red ? draw->host.graphics->red_cube_model : draw->host.graphics->blue_cube_model, NULL, origin,
            angles);
      } else {
        qa_rect rect = {(float)(request->rect.x + 3), (float)(request->rect.y + 16), 20, 20};
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect,
            red ? draw->host.graphics->red_cube_icon : draw->host.graphics->blue_cube_icon);
      }
      return;
    }
    case QA_Q3_PRES_OD_AREA_SYSTEMCHAT:
    case QA_Q3_PRES_OD_AREA_TEAMCHAT:
    case QA_Q3_PRES_OD_AREA_CHAT: {
      const char *system = "", *team1 = "", *team2 = "";
      draw->host.chat(draw->host.ctx, &system, &team1, &team2);
      paint_text(draw, request, id == QA_Q3_PRES_OD_AREA_SYSTEMCHAT ? system
                                : id == QA_Q3_PRES_OD_AREA_TEAMCHAT ? team1 : team2);
      return;
    }
    case QA_Q3_PRES_OD_SPECTATORS: paint_text(draw, request, (state->spectator_list ? state->spectator_list : "")); return;
    case QA_Q3_PRES_OD_BLUE_FLAGSTATUS:
    case QA_Q3_PRES_OD_RED_FLAGSTATUS: {
      bool blue = id == QA_Q3_PRES_OD_BLUE_FLAGSTATUS;
      const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
      if (cgs->game_type != QA_Q3_BASE_GT_CTF && cgs->game_type != QA_Q3_BASE_GT_1FCTF) {
        if (cgs->game_type == QA_Q3_BASE_GT_HARVESTER && g) {
          qa_vec4 color = blue ? (qa_vec4){0, 0, 1, 1} : (qa_vec4){1, 0, 0, 1};
          qa_draw2d_set_color(&tools_of(draw)->draw, &color);
          qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
                                                blue ? g->blue_cube_icon : g->red_cube_icon);
          qa_draw2d_set_color(&tools_of(draw)->draw, NULL);
        }
        return;
      }
      if (g && qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK,
                                                blue ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG) != NULL) {
        double status = blue ? cgs->blueflag : cgs->redflag;
        qa_vec4 color = blue ? (qa_vec4){0, 0, 1, 1} : (qa_vec4){1, 0, 0, 1};
        qa_draw2d_set_color(&tools_of(draw)->draw, &color);
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
            g->flag_shaders[status >= 0 && status <= 2 ? (size_t)status : 0]);
        qa_draw2d_set_color(&tools_of(draw)->draw, NULL);
      }
      return;
    }
    case QA_Q3_PRES_OD_ONEFLAG_STATUS: {
      double status = cgs->flag_status;
      const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
      if (cgs->game_type != QA_Q3_BASE_GT_1FCTF || !g ||
          qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, QA_Q3_PW_NEUTRALFLAG) == NULL ||
          status < 0 || status > 4) return;
      qa_vec4 color = status == 2 ? (qa_vec4){1, 0, 0, 1} : status == 3 ? (qa_vec4){0, 0, 1, 1}
                                                                 : (qa_vec4){1, 1, 1, 1};
      qa_draw2d_set_color(&tools_of(draw)->draw, &color);
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), request->rect,
          g->flag_shaders[status == 2 || status == 3 ? 1 : status == 4 ? 2 : 0]);
      /* The source intentionally leaves this color set for the next draw. */
      return;
    }
    case QA_Q3_PRES_OD_ACCURACY:
    case QA_Q3_PRES_OD_ASSISTS:
    case QA_Q3_PRES_OD_DEFEND:
    case QA_Q3_PRES_OD_EXCELLENT:
    case QA_Q3_PRES_OD_IMPRESSIVE:
    case QA_Q3_PRES_OD_PERFECT:
    case QA_Q3_PRES_OD_GAUNTLET:
    case QA_Q3_PRES_OD_CAPTURES: {
      /* TS medal(): the selected score row, fround-staged value. */
      if (state->selected_score != trunc(state->selected_score) || state->selected_score < 0 ||
          state->selected_score >= 64)
        qa_range_error("Invalid owner-draw slot %lld", (long long)qa_int32(state->selected_score));
      qa_q3_pres_client_score *score = &state->scores[(size_t)state->selected_score];
      double value = (float)(id == QA_Q3_PRES_OD_ACCURACY ? score->accuracy
          : id == QA_Q3_PRES_OD_ASSISTS ? score->assist_count
          : id == QA_Q3_PRES_OD_DEFEND ? score->defend_count
          : id == QA_Q3_PRES_OD_EXCELLENT ? score->excellent_count
          : id == QA_Q3_PRES_OD_IMPRESSIVE ? score->impressive_count
          : id == QA_Q3_PRES_OD_PERFECT ? score->perfect
          : id == QA_Q3_PRES_OD_GAUNTLET ? score->guantlet_count : score->captures);
      if (value > 0) {
        if (id == QA_Q3_PRES_OD_PERFECT) paint_text(draw, request, "Wow");
        else if (id == QA_Q3_PRES_OD_ACCURACY) {
          char text[32];
          snprintf(text, sizeof text, "%lld%%", (long long)trunc(value));
          paint_text(draw, request, text);
        } else paint_number(draw, request, trunc(value));
      }
      return;
    }
    default:
      /* Remaining TS ids need host surfaces this port's host does not
       * carry yet (playerHead random is wired, but item/area-powerup
       * item visuals, spectators textPaintLimit scrolling and teamInfo
       * rows ride media.weaponRegistry/resources picks): callable
       * no-ops here, listed as unresolved by the slice. */
      return;
  }
}
