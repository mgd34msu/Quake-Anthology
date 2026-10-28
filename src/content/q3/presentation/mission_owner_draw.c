/* Port of src/content/q3/presentation/mission-owner-draw.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mission_owner_draw.h"
#include <math.h>
#include <stdio.h>
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
static bool team_has_flag(qa_q3_pres_mission_owner_draw *draw, bool other) {
  qa_q3_base_source_player_state *ps = snapshot_ps(draw);
  if (!ps) return false;
  double team = qa_q3_base_player_state_slots_get(&ps->persistant, 1); /* PERS_TEAM */
  bool red_taken = draw->static_state->redflag == 1, blue_taken = draw->static_state->blueflag == 1;
  if (team == QA_Q3_BASE_TEAM_RED) return other ? red_taken : blue_taken;
  if (team == QA_Q3_BASE_TEAM_BLUE) return other ? blue_taken : red_taken;
  return false;
}
bool qa_q3_pres_mission_owner_draw_other_team_has_flag(qa_q3_pres_mission_owner_draw *draw) {
  return team_has_flag(draw, true);
}
bool qa_q3_pres_mission_owner_draw_your_team_has_flag(qa_q3_pres_mission_owner_draw *draw) {
  return team_has_flag(draw, false);
}
static bool visible(qa_q3_pres_mission_owner_draw *draw, double flags) {
  int64_t bits = (int64_t)flags;
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  bool team_game = cgs->game_type >= QA_Q3_BASE_GT_TEAM;
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYTEAMGAME) && !team_game) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYNONTEAMGAME) && team_game) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_CTF) && cgs->game_type != QA_Q3_BASE_GT_CTF) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_ONEFLAG) && cgs->game_type != QA_Q3_BASE_GT_1FCTF) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_OBELISK) && cgs->game_type != QA_Q3_BASE_GT_OBELISK) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_HARVESTER) && cgs->game_type != QA_Q3_BASE_GT_HARVESTER) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_TOURNAMENT) && cgs->game_type != QA_Q3_BASE_GT_TOURNAMENT) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_SINGLEPLAYER) && cgs->game_type != QA_Q3_BASE_GT_SINGLE_PLAYER) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_OTHERTEAMHASFLAG) && !qa_q3_pres_mission_owner_draw_other_team_has_flag(draw))
    return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_YOURTEAMHASENEMYFLAG) && !qa_q3_pres_mission_owner_draw_your_team_has_flag(draw))
    return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG) && cgs->redflag != 1) return false;
  if ((bits & QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG) && cgs->blueflag != 1) return false;
  return true;
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
    case QA_Q3_BASE_GT_TOURNAMENT: return "Tournament";
    case QA_Q3_BASE_GT_SINGLE_PLAYER: return "Single Player";
    case QA_Q3_BASE_GT_TEAM: return "Team Deathmatch";
    case QA_Q3_BASE_GT_CTF: return "Capture The Flag";
    case QA_Q3_BASE_GT_1FCTF: return "One Flag CTF";
    case QA_Q3_BASE_GT_OBELISK: return "Overload";
    case QA_Q3_BASE_GT_HARVESTER: return "Harvester";
    default: return "";
  }
}
void qa_q3_pres_mission_owner_draw_paint(qa_q3_pres_mission_owner_draw *draw,
                                         const qa_ui_owner_draw_paint_request *request) {
  if (!visible(draw, request->owner_draw_flags)) return;
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
      qa_vec3 angles = {state->head_start_pitch, state->head_start_yaw, 0};
      if (ps) qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, request->rect, ps->client_num, angles);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEAD: {
      qa_vec3 angles = {state->head_start_pitch, state->head_start_yaw, 0};
      qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, request->rect,
                                             draw->host.selected_player(draw->host.ctx), angles);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_NAME: {
      double index = draw->host.selected_player(draw->host.ctx);
      if (index >= 0 && index < 64) {
        const char *selected_name = cgs->client_info[(size_t)index].name;
        paint_text(draw, request, selected_name ? selected_name : "");
      }
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEALTH: {
      double index = draw->host.selected_player(draw->host.ctx);
      if (index >= 0 && index < 64) paint_number(draw, request, cgs->client_info[(size_t)index].health);
      return;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_ARMOR: {
      double index = draw->host.selected_player(draw->host.ctx);
      if (index >= 0 && index < 64) paint_number(draw, request, cgs->client_info[(size_t)index].armor);
      return;
    }
    case QA_Q3_PRES_OD_PLAYER_SCORE:
      if (ps) paint_number(draw, request, qa_q3_base_player_state_slots_get(&ps->persistant, 2)); /* PERS_SCORE */
      return;
    case QA_Q3_PRES_OD_BLUE_SCORE: paint_number(draw, request, cgs->scores2); return;
    case QA_Q3_PRES_OD_RED_SCORE: paint_number(draw, request, cgs->scores1); return;
    case QA_Q3_PRES_OD_RED_NAME: paint_text(draw, request, (cgs->red_team ? cgs->red_team : "")); return;
    case QA_Q3_PRES_OD_BLUE_NAME: paint_text(draw, request, (cgs->blue_team ? cgs->blue_team : "")); return;
    case QA_Q3_PRES_OD_GAME_TYPE: paint_text(draw, request, game_type_text(cgs->game_type)); return;
    case QA_Q3_PRES_OD_GAME_STATUS: {
      char *place = qa_q3_pres_place_string(1);
      char text[160];
      snprintf(text, sizeof text, "%.80s  %lld:%lld", place ? place : "", (long long)cgs->scores1,
               (long long)cgs->scores2);
      paint_text(draw, request, text);
      return;
    }
    case QA_Q3_PRES_OD_KILLER: paint_text(draw, request, (state->killer_name ? state->killer_name : "")); return;
    case QA_Q3_PRES_OD_CAPFRAGLIMIT:
      paint_number(draw, request, cgs->game_type >= QA_Q3_BASE_GT_CTF ? cgs->capturelimit : cgs->fraglimit);
      return;
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
    case QA_Q3_PRES_OD_RED_FLAGSTATUS:
    case QA_Q3_PRES_OD_ONEFLAG_STATUS: {
      double status = id == QA_Q3_PRES_OD_BLUE_FLAGSTATUS ? cgs->blueflag
                      : id == QA_Q3_PRES_OD_RED_FLAGSTATUS ? cgs->redflag : cgs->flag_status;
      paint_text(draw, request, status == 0 ? "At Base" : status == 1 ? "Taken" : "Dropped");
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
      /* Medal counters live on the local client's score row. */
      if (ps) {
        for (int i = 0; i < (int)state->num_scores; i++) {
          if (state->scores[i].client != ps->client_num) continue;
          qa_q3_pres_client_score *score = &state->scores[i];
          double value = id == QA_Q3_PRES_OD_ACCURACY ? score->accuracy
              : id == QA_Q3_PRES_OD_ASSISTS ? score->assist_count
              : id == QA_Q3_PRES_OD_DEFEND ? score->defend_count
              : id == QA_Q3_PRES_OD_EXCELLENT ? score->excellent_count
              : id == QA_Q3_PRES_OD_IMPRESSIVE ? score->impressive_count
              : id == QA_Q3_PRES_OD_PERFECT ? score->perfect
              : id == QA_Q3_PRES_OD_GAUNTLET ? score->guantlet_count : score->captures;
          paint_number(draw, request, value);
          break;
        }
      }
      return;
    }
    default:
      /* Remaining ids (heads, skulls, powerups, locations, status,
       * places) draw through the icon/text helpers in the TS; the
       * shared owner-draw surface keeps them callable no-ops here. */
      return;
  }
}
