/* Port of src/content/q3/presentation/mission-owner-draw.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mission_owner_draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/content/q3/presentation/events.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"
enum { CS_LOCATIONS = 608, MAX_LOCATIONS = 64, SCORE_NOT_PRESENT = -9999, NUMBER_TEXT = 64 };
#define PI_D 3.141592653589793
static const qa_vec4 WHITE = {1, 1, 1, 1}, RED = {1, 0, 0, 1}, BLUE = {0, 0, 1, 1};
static const qa_text_texture_rect FULL_UV = {0, 0, 1, 1};
static double f(double value) { return (float)value; }
const char *qa_q3_pres_mission_owner_draw_id_name(qa_q3_pres_mission_owner_draw_id id) {
  switch (id) {
#define NAME(suffix) case QA_Q3_PRES_OD_##suffix: return "CG_" #suffix
    NAME(PLAYER_ARMOR_ICON); NAME(PLAYER_ARMOR_VALUE); NAME(PLAYER_HEAD); NAME(PLAYER_HEALTH);
    NAME(PLAYER_AMMO_ICON); NAME(PLAYER_AMMO_VALUE); NAME(SELECTEDPLAYER_HEAD); NAME(SELECTEDPLAYER_NAME);
    NAME(SELECTEDPLAYER_LOCATION); NAME(SELECTEDPLAYER_STATUS); NAME(SELECTEDPLAYER_WEAPON);
    NAME(SELECTEDPLAYER_POWERUP); NAME(PLAYER_ITEM); NAME(PLAYER_SCORE); NAME(BLUE_FLAGHEAD); NAME(BLUE_FLAGSTATUS);
    NAME(BLUE_FLAGNAME); NAME(RED_FLAGHEAD); NAME(RED_FLAGSTATUS); NAME(RED_FLAGNAME); NAME(BLUE_SCORE);
    NAME(RED_SCORE); NAME(RED_NAME); NAME(BLUE_NAME); NAME(HARVESTER_SKULLS); NAME(ONEFLAG_STATUS);
    NAME(PLAYER_LOCATION); NAME(TEAM_COLOR); NAME(CTF_POWERUP); NAME(AREA_POWERUP); NAME(PLAYER_HASFLAG);
    NAME(GAME_TYPE); NAME(SELECTEDPLAYER_ARMOR); NAME(SELECTEDPLAYER_HEALTH); NAME(PLAYER_STATUS);
    NAME(AREA_SYSTEMCHAT); NAME(AREA_TEAMCHAT); NAME(AREA_CHAT); NAME(GAME_STATUS); NAME(KILLER);
    NAME(PLAYER_ARMOR_ICON2D); NAME(PLAYER_AMMO_ICON2D); NAME(ACCURACY); NAME(ASSISTS); NAME(DEFEND);
    NAME(EXCELLENT); NAME(IMPRESSIVE); NAME(PERFECT); NAME(GAUNTLET); NAME(SPECTATORS); NAME(TEAMINFO);
    NAME(VOICE_HEAD); NAME(VOICE_NAME); NAME(PLAYER_HASFLAG2D); NAME(HARVESTER_SKULLS2D); NAME(CAPFRAGLIMIT);
    NAME(1STPLACE); NAME(2NDPLACE); NAME(CAPTURES);
#undef NAME
  }
  return NULL;
}
void qa_q3_pres_mission_owner_draw_init(qa_q3_pres_mission_owner_draw *draw,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_mission_owner_draw_host host) {
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK || static_state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK ||
      host.icons->state != state || host.icons->media.static_state != static_state)
    qa_range_error("Mission owner drawing requires canonical Team Arena state and media");
  draw->state = state;
  draw->static_state = static_state;
  draw->host = host;
}
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_mission_owner_draw *draw) {
  return draw->host.icons->tools;
}
static qa_q3_base_source_player_state *ps_of(qa_q3_pres_mission_owner_draw *draw) {
  if (!draw->state->snap) qa_range_error("Mission owner drawing requires a current snapshot");
  return &draw->state->snap->player_state;
}
static double owner_cvar(qa_q3_pres_mission_owner_draw *draw, const char *name) {
  qa_cvar_snapshot snapshot = draw->host.read_vm_cvar(draw->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static size_t slot_index(double index, size_t length) {
  if (index != trunc(index) || index < 0 || index >= (double)length)
    qa_range_error("Invalid owner-draw slot %s", QA_JS_NUMBER(index));
  return (size_t)index;
}
static qa_q3_pres_client_info *client_at(qa_q3_pres_mission_owner_draw *draw, double index) {
  return &draw->static_state->client_info[slot_index(index, QA_Q3_PRES_MAX_CLIENTS)];
}
static double sorted_at(qa_q3_pres_mission_owner_draw *draw, double index) {
  size_t length = sizeof draw->state->sorted_team_players / sizeof draw->state->sorted_team_players[0];
  return draw->state->sorted_team_players[slot_index(index, length)];
}
static double selected_index(qa_q3_pres_mission_owner_draw *draw) {
  return sorted_at(draw, draw->host.selected_player(draw->host.ctx));
}
static qa_q3_pres_client_info *selected_client(qa_q3_pres_mission_owner_draw *draw) {
  return client_at(draw, selected_index(draw));
}
static double player_team(qa_q3_pres_mission_owner_draw *draw) {
  return qa_q3_base_player_state_slots_get(&ps_of(draw)->persistant, QA_Q3_BASE_PERS_TEAM);
}
static const char *location_text(qa_q3_pres_mission_owner_draw *draw, double index) {
  const char *text = draw->host.config_string(draw->host.ctx, CS_LOCATIONS + index);
  return text && text[0] != '\0' ? text : "unknown";
}
static const char *client_name(const qa_q3_pres_client_info *client) { return client->name ? client->name : ""; }
static double width_text(qa_q3_pres_mission_owner_draw *draw, const char *text, double scale) {
  return qa_q3_text_width(draw->host.fonts(draw->host.ctx), text, scale, 0);
}
static void paint_text_at(qa_q3_pres_mission_owner_draw *draw, double scale, qa_vec4 color, const char *text,
                          double style, double x, double y) {
  qa_q3_text_paint_options options = {x, y, scale, color, text, 0, 0, qa_js_to_int32(style)};
  const qa_draw2d * draw2 = &tools_of(draw)->draw;
  const qa_q3_font_set * fonts2 = draw->host.fonts(draw->host.ctx);
  qa_q3_text_paint(draw2, fonts2, &options);
}
/* TS text() with its default position: the rect's left edge and bottom. */
static void paint_text(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double scale, qa_vec4 color,
                       const char *text, double style) {
  paint_text_at(draw, scale, color, text, style, rect.x, f(rect.y + rect.height));
}
static double paint_limited(qa_q3_pres_mission_owner_draw *draw, const char *text, double x, double y, double scale,
                            qa_vec4 color, double maximum, double limit) {
  qa_q3_text_paint_options options = {x, y, scale, color, text, 0, limit, 0};
  const qa_draw2d * draw2 = &tools_of(draw)->draw;
  const qa_q3_font_set * fonts2 = draw->host.fonts(draw->host.ctx);
  return qa_q3_text_paint_limit(draw2, fonts2, &options, maximum);
}
static void stretch_picture(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, const qa_ui_picture_or_lazy *picture) {
  qa_draw2d *target = &tools_of(draw)->draw;
  if (picture->kind == QA_UI_PICTURE_LAZY) qa_draw2d_stretch_pic_lazy(target, rect, FULL_UV, picture->produce, picture->produce_ctx);
  else qa_draw2d_stretch_pic(target, rect, FULL_UV, &picture->picture);
}
static void paint_number(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double scale, qa_vec4 color, double value,
                         const qa_ui_picture_or_lazy *picture, double style) {
  if (picture->kind != QA_UI_PICTURE_NONE) {
    qa_draw2d_set_color(&tools_of(draw)->draw, &color);
    stretch_picture(draw, rect, picture);
    qa_draw2d_set_color(&tools_of(draw)->draw, NULL);
  } else {
    char text[NUMBER_TEXT];
    snprintf(text, sizeof text, "%d", qa_int32(value));
    double width = width_text(draw, text, scale);
    paint_text_at(draw, scale, color, text, style, f(rect.x + f(f(rect.width - width) / 2)), f(rect.y + rect.height));
  }
}
const qa_q3_pres_scene_shader *qa_q3_pres_mission_owner_draw_status_handle(qa_q3_pres_mission_owner_draw *draw, double task) {
  const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
  if (task == 2) return g->defend_shader;
  if (task == 3) return g->patrol_shader;
  if (task == 4) return g->follow_shader;
  if (task == 5) return g->retrieve_shader;
  if (task == 6) return g->escort_shader;
  if (task == 7) return g->camp_shader;
  return g->assault_shader;
}
static double raw_value(qa_q3_pres_mission_owner_draw *draw, double id) {
  qa_q3_base_source_player_state *ps = ps_of(draw);
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  if (id == QA_Q3_PRES_OD_SELECTEDPLAYER_ARMOR) return selected_client(draw)->armor;
  if (id == QA_Q3_PRES_OD_SELECTEDPLAYER_HEALTH) return selected_client(draw)->health;
  if (id == QA_Q3_PRES_OD_PLAYER_ARMOR_VALUE) return qa_q3_base_player_state_slots_get(&ps->stats, stats->armor);
  if (id == QA_Q3_PRES_OD_PLAYER_AMMO_VALUE) {
    if (draw->host.weapon_hud) {
      qa_q3_pres_weapon_hud_reading reading = draw->host.weapon_hud(draw->host.weapon_hud_ctx);
      return reading.has_status && reading.status.ammo_is_finite ? reading.status.ammo_count : -1;
    }
    double weapon = qa_q3_pres_client_game_state_entity_at(draw->state, ps->client_num)->current_state.weapon;
    return weapon != 0 ? qa_q3_base_player_state_slots_get(&ps->ammo, weapon) : -1;
  }
  if (id == QA_Q3_PRES_OD_PLAYER_SCORE) return qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE);
  if (id == QA_Q3_PRES_OD_PLAYER_HEALTH) return qa_q3_base_player_state_slots_get(&ps->stats, stats->health);
  if (id == QA_Q3_PRES_OD_RED_SCORE) return draw->static_state->scores1;
  if (id == QA_Q3_PRES_OD_BLUE_SCORE) return draw->static_state->scores2;
  return -1;
}
double qa_q3_pres_mission_owner_draw_value(qa_q3_pres_mission_owner_draw *draw, double id) {
  return f(raw_value(draw, id));
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
bool qa_q3_pres_mission_owner_draw_visible(qa_q3_pres_mission_owner_draw *draw, double flags) {
  int32_t bits = qa_int32(flags);
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  qa_q3_base_game_type type = cgs->game_type;
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  if (bits & QA_Q3_PRES_OD_SHOW_TEAMINFO) return owner_cvar(draw, "cg_currentSelectedPlayer") == draw->state->num_sorted_team_players;
  if (bits & QA_Q3_PRES_OD_SHOW_NOTEAMINFO) return owner_cvar(draw, "cg_currentSelectedPlayer") != draw->state->num_sorted_team_players;
  if (bits & QA_Q3_PRES_OD_SHOW_OTHERTEAMHASFLAG) return qa_q3_pres_mission_owner_draw_other_team_has_flag(draw);
  if (bits & QA_Q3_PRES_OD_SHOW_YOURTEAMHASENEMYFLAG) return qa_q3_pres_mission_owner_draw_your_team_has_flag(draw);
  if (bits & (QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG | QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG))
    return ((bits & QA_Q3_PRES_OD_SHOW_BLUE_TEAM_HAS_REDFLAG) != 0 && (cgs->redflag == 1 || cgs->flag_status == 2)) ||
           ((bits & QA_Q3_PRES_OD_SHOW_RED_TEAM_HAS_BLUEFLAG) != 0 && (cgs->blueflag == 1 || cgs->flag_status == 3));
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYTEAMGAME) && type >= QA_Q3_BASE_GT_TEAM) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_ANYNONTEAMGAME) && type < QA_Q3_BASE_GT_TEAM) return true;
  if (bits & QA_Q3_PRES_OD_SHOW_HARVESTER) return type == QA_Q3_BASE_GT_HARVESTER;
  if (bits & QA_Q3_PRES_OD_SHOW_ONEFLAG) return type == QA_Q3_BASE_GT_1FCTF;
  if ((bits & QA_Q3_PRES_OD_SHOW_CTF) && type == QA_Q3_BASE_GT_CTF) return true;
  if (bits & QA_Q3_PRES_OD_SHOW_OBELISK) return type == QA_Q3_BASE_GT_OBELISK;
  if ((bits & QA_Q3_PRES_OD_SHOW_HEALTHCRITICAL) && qa_q3_base_player_state_slots_get(&ps_of(draw)->stats, stats->health) < 25) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_HEALTHOK) && qa_q3_base_player_state_slots_get(&ps_of(draw)->stats, stats->health) >= 25) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_SINGLEPLAYER) && type == QA_Q3_BASE_GT_SINGLE_PLAYER) return true;
  if ((bits & QA_Q3_PRES_OD_SHOW_TOURNAMENT) && type == QA_Q3_BASE_GT_TOURNAMENT) return true;
  if (bits & QA_Q3_PRES_OD_SHOW_IF_PLAYER_HAS_FLAG) {
    qa_q3_base_source_player_state *ps = ps_of(draw);
    return qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_REDFLAG) != 0 ||
           qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_BLUEFLAG) != 0 ||
           qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_NEUTRALFLAG) != 0;
  }
  return false;
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
  char first[NUMBER_TEXT], second[NUMBER_TEXT];
  if (draw->static_state->game_type < QA_Q3_BASE_GT_TEAM) {
    if (player_team(draw) == QA_Q3_BASE_TEAM_SPECTATOR) {
      out[0] = '\0';
      return;
    }
    qa_q3_base_source_player_state *ps = ps_of(draw);
    char *place = qa_q3_pres_place_string(qa_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_RANK) + 1));
    qa_net_js_number(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE), first, sizeof first);
    snprintf(out, size, "%s place with %s", place ? place : "", first);
    free(place);
    return;
  }
  double red = draw->state->team_scores[0], blue = draw->state->team_scores[1];
  qa_net_js_number(red, first, sizeof first);
  qa_net_js_number(blue, second, sizeof second);
  if (red == blue) snprintf(out, size, "Teams are tied at %s", first);
  else if (red >= blue) snprintf(out, size, "Red leads Blue, %s to %s", first, second);
  else snprintf(out, size, "Blue leads Red, %s to %s", second, first);
}
/* Owned; TS killerText(). */
static char *killer_text(qa_q3_pres_mission_owner_draw *draw) {
  const char *killer = draw->state->killer_name;
  if (!killer || !killer[0]) {
    char *empty = calloc(1, 1);
    if (!empty) qa_range_error("Mission owner drawing allocation failed");
    return empty;
  }
  static const char prefix[] = "Fragged by ";
  char *text = malloc(sizeof prefix + strlen(killer));
  if (!text) qa_range_error("Mission owner drawing allocation failed");
  strcpy(text, prefix);
  strcat(text, killer);
  return text;
}
double qa_q3_pres_mission_owner_draw_width(qa_q3_pres_mission_owner_draw *draw, double id, double scale) {
  if (id == QA_Q3_PRES_OD_GAME_TYPE) return width_text(draw, game_type_text(draw->static_state->game_type), scale);
  if (id == QA_Q3_PRES_OD_GAME_STATUS) {
    char text[320];
    game_status_text(draw, text, sizeof text);
    return width_text(draw, text, scale);
  }
  if (id == QA_Q3_PRES_OD_KILLER) {
    char *text = killer_text(draw);
    double width = width_text(draw, text, scale);
    free(text);
    return width;
  }
  if (id == QA_Q3_PRES_OD_RED_NAME || id == QA_Q3_PRES_OD_BLUE_NAME) {
    qa_cvar_snapshot snapshot = draw->host.read_vm_cvar(draw->host.ctx, id == QA_Q3_PRES_OD_RED_NAME ? "g_redteam" : "g_blueteam");
    double width = width_text(draw, snapshot.value, scale);
    qa_cvar_read_free(&snapshot);
    return width;
  }
  return 0;
}
static double spin_yaw(double time) { return f((qa_int32(time) & 2047) * 360.0 / 2048); }
static void paint_armor_icon(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool force_2d) {
  if (owner_cvar(draw, "cg_drawStatus") == 0) return;
  if (force_2d || (owner_cvar(draw, "cg_draw3dIcons") == 0 && owner_cvar(draw, "cg_drawIcons") != 0)) {
    qa_rect icon = rect;
    icon.y = (float)f(f(rect.y + f(rect.height / 2)) + 1);
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), icon, draw->host.graphics->armor_icon);
  } else if (owner_cvar(draw, "cg_draw3dIcons") != 0) {
    qa_vec3 origin = {90, 0, -10}, angles = {0, (float)spin_yaw(draw->state->time), 0};
    qa_q3_pres_client_draw_icons_draw_3d_model(draw->host.icons, rect, draw->host.graphics->armor_model, NULL, origin, angles);
  }
}
static void paint_ammo_icon(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool force_2d) {
  if (draw->host.weapon_hud) return;
  if (force_2d || (owner_cvar(draw, "cg_draw3dIcons") == 0 && owner_cvar(draw, "cg_drawIcons") != 0)) {
    const qa_q3_pres_scene_shader *icon = draw->host.ammo_icon(draw->host.ctx, draw->state->predicted_player_state.weapon);
    if (icon) qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect, icon);
  } else if (owner_cvar(draw, "cg_draw3dIcons") != 0) {
    double weapon = qa_q3_pres_client_game_state_entity_at(draw->state, ps_of(draw)->client_num)->current_state.weapon;
    qa_q3_pres_scene_model model = draw->host.ammo_model(draw->host.ctx, weapon);
    if (weapon != 0 && model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
      qa_vec3 origin = {70, 0, 0};
      qa_vec3 angles = {0, (float)f(90 + f(20 * f(sin(f(f(draw->state->time) / 1000))))), 0};
      qa_q3_pres_client_draw_icons_draw_3d_model(draw->host.icons, rect, model, NULL, origin, angles);
    }
  }
}
static double head_end_time(qa_q3_pres_mission_owner_draw *draw) {
  double time = draw->state->time;
  return qa_int32(trunc(f(f(qa_int32(time + 100)) + f(draw->host.random_value(draw->host.ctx) * 2000))));
}
static double head_end_yaw(qa_q3_pres_mission_owner_draw *draw) {
  return f(180 + f(20 * f(cos(f(draw->host.crandom(draw->host.ctx) * f(PI_D))))));
}
static double head_end_pitch(qa_q3_pres_mission_owner_draw *draw) {
  return f(5 * f(cos(f(draw->host.crandom(draw->host.ctx) * f(PI_D)))));
}
static void paint_player_head(qa_q3_pres_mission_owner_draw *draw, qa_rect rect) {
  qa_q3_pres_client_game_state *cg = draw->state;
  double x = rect.x;
  if (cg->damage_time != 0 && f(f(cg->time) - cg->damage_time) < 500) {
    double frac = f(f(f(cg->time) - cg->damage_time) / 500);
    double size = f(f(rect.width * 1.25) * f(1.5 - f(frac * 0.5))), stretch = f(size - f(rect.width * 1.25));
    x = f(x - f(f(stretch * 0.5) + f(f(cg->damage_x * stretch) * 0.5)));
    cg->head_start_yaw = f(180 + f(cg->damage_x * 45));
    cg->head_end_yaw = head_end_yaw(draw);
    cg->head_end_pitch = head_end_pitch(draw);
    cg->head_start_time = cg->time;
    cg->head_end_time = head_end_time(draw);
  } else if (cg->time >= cg->head_end_time) {
    cg->head_start_yaw = cg->head_end_yaw;
    cg->head_start_pitch = cg->head_end_pitch;
    cg->head_start_time = cg->head_end_time;
    cg->head_end_time = head_end_time(draw);
    cg->head_end_yaw = head_end_yaw(draw);
    cg->head_end_pitch = head_end_pitch(draw);
  }
  if (cg->head_start_time > cg->time) cg->head_start_time = cg->time;
  double frac = f(f(qa_int32(cg->time - cg->head_start_time)) / f(qa_int32(cg->head_end_time - cg->head_start_time)));
  frac = f(f(frac * frac) * f(3 - f(2 * frac)));
  qa_rect head = rect;
  head.x = (float)x;
  qa_vec3 angles = {(float)f(cg->head_start_pitch + f(f(cg->head_end_pitch - cg->head_start_pitch) * frac)),
                    (float)f(cg->head_start_yaw + f(f(cg->head_end_yaw - cg->head_start_yaw) * frac)), 0};
  qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, head, ps_of(draw)->client_num, angles);
}
static bool order_blinked_off(qa_q3_pres_mission_owner_draw *draw) {
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  double time = draw->state->time;
  return cgs->order_pending && time > qa_int32(cgs->order_time - 2500) && ((qa_int32(time) >> 9) & 1);
}
static void paint_selected_status(qa_q3_pres_mission_owner_draw *draw, qa_rect rect) {
  qa_q3_pres_client_info *ci = selected_client(draw);
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  if (order_blinked_off(draw)) return;
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect,
      qa_q3_pres_mission_owner_draw_status_handle(draw, cgs->order_pending ? cgs->current_order : ci->team_task));
}
/* -1 is TS null. */
static double flag_carrier(qa_q3_pres_mission_owner_draw *draw, bool blue) {
  for (double i = 0; i < draw->static_state->maxclients; i++) {
    qa_q3_pres_client_info *ci = client_at(draw, i);
    if (ci->info_valid && ci->team == (blue ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE) &&
        (qa_int32(ci->powerups) & (1 << (blue ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG))) != 0)
      return i;
  }
  return -1;
}
static void paint_flag_head(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool blue) {
  if (flag_carrier(draw, blue) < 0) return;
  qa_vec3 angles = {0, (float)f(180 + f(20 * f(sin(f(f(draw->state->time) / 650))))), 0};
  qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, rect, 0, angles);
}
static void paint_flag_status(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool blue, const qa_ui_picture_or_lazy *picture) {
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
  qa_draw2d *target = &tools_of(draw)->draw;
  if (cgs->game_type != QA_Q3_BASE_GT_CTF && cgs->game_type != QA_Q3_BASE_GT_1FCTF) {
    if (cgs->game_type == QA_Q3_BASE_GT_HARVESTER) {
      qa_draw2d_set_color(target, blue ? &BLUE : &RED);
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect, blue ? g->blue_cube_icon : g->red_cube_icon);
      qa_draw2d_set_color(target, NULL);
    }
    return;
  }
  if (picture->kind != QA_UI_PICTURE_NONE) {
    stretch_picture(draw, rect, picture);
  } else if (qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, blue ? QA_Q3_PW_BLUEFLAG : QA_Q3_PW_REDFLAG) != NULL) {
    double status = blue ? cgs->blueflag : cgs->redflag;
    qa_draw2d_set_color(target, blue ? &BLUE : &RED);
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect, g->flag_shaders[status >= 0 && status <= 2 ? slot_index(status, 3) : 0]);
    qa_draw2d_set_color(target, NULL);
  }
}
static void paint_one_flag_status(qa_q3_pres_mission_owner_draw *draw, qa_rect rect) {
  double status = draw->static_state->flag_status;
  if (draw->static_state->game_type != QA_Q3_BASE_GT_1FCTF ||
      qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, QA_Q3_PW_NEUTRALFLAG) == NULL ||
      status < 0 || status > 4) return;
  qa_draw2d_set_color(&tools_of(draw)->draw, status == 2 ? &RED : status == 3 ? &BLUE : &WHITE);
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect,
      draw->host.graphics->flag_shaders[status == 2 || status == 3 ? 1 : status == 4 ? 2 : 0]);
  /* The source intentionally leaves this color set for the next draw. */
}
static void paint_skulls(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double scale, qa_vec4 color, bool force_2d, double style) {
  if (draw->static_state->game_type != QA_Q3_BASE_GT_HARVESTER) return;
  char text[NUMBER_TEXT];
  qa_net_js_number(qa_js_min(ps_of(draw)->generic1, 99), text, sizeof text);
  paint_text_at(draw, scale, color, text, style, f(rect.x + f(rect.width - width_text(draw, text, scale))), f(rect.y + rect.height));
  if (owner_cvar(draw, "cg_drawIcons") == 0) return;
  bool red = player_team(draw) == QA_Q3_BASE_TEAM_BLUE;
  const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
  if (!force_2d && owner_cvar(draw, "cg_draw3dIcons") != 0) {
    qa_rect model = rect;
    model.width = model.height = 35;
    qa_vec3 origin = {90, 0, -10}, angles = {0, (float)spin_yaw(draw->state->time), 0};
    qa_q3_pres_client_draw_icons_draw_3d_model(draw->host.icons, model, red ? g->red_cube_model : g->blue_cube_model, NULL, origin, angles);
  } else {
    qa_rect icon = {(float)f(rect.x + 3), (float)f(rect.y + 16), 20, 20};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), icon, red ? g->red_cube_icon : g->blue_cube_icon);
  }
}
static void paint_player_has_flag(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool force_2d) {
  qa_q3_base_source_player_state *ps = &draw->state->predicted_player_state;
  double adj = force_2d ? 0 : 2;
  qa_rect r = {(float)f(rect.x + adj), (float)f(rect.y + adj), (float)f(rect.width - adj), (float)f(rect.height - adj)};
  if (qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_REDFLAG) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, r, QA_Q3_BASE_TEAM_RED, force_2d);
  else if (qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_BLUEFLAG) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, r, QA_Q3_BASE_TEAM_BLUE, force_2d);
  else if (qa_q3_base_player_state_slots_get(&ps->powerups, QA_Q3_PW_NEUTRALFLAG) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(draw->host.icons, r, QA_Q3_BASE_TEAM_FREE, force_2d);
}
static void register_item(qa_q3_pres_mission_owner_draw *draw, double value) {
  if (!draw->host.register_item_visuals(draw->host.ctx, value)) qa_range_error("Mission owner drawing allocation failed");
}
static void paint_item(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, bool persistent) {
  if (persistent && draw->static_state->game_type < QA_Q3_BASE_GT_CTF) return;
  double value = qa_q3_base_player_state_slots_get(&ps_of(draw)->stats, persistent ? QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP : QA_Q3_BASE_MP_STAT_HOLDABLE_ITEM);
  if (value == 0) return;
  register_item(draw, value);
  if (!persistent) register_item(draw, value);
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect, draw->host.item_icon(draw->host.ctx, value));
}
static const qa_q3_pres_scene_shader *item_shader(qa_q3_pres_mission_owner_draw *draw, const qa_q3_base_item_definition *item) {
  return item->icon ? draw->host.register_shader(draw->host.ctx, item->icon) : NULL;
}
static void paint_selected_powerup(qa_q3_pres_mission_owner_draw *draw, qa_rect rect) {
  qa_q3_pres_client_info *ci = selected_client(draw);
  for (int j = 0; j < QA_Q3_PW_NUM_POWERUPS; j++) {
    if ((qa_int32(ci->powerups) & (1 << j)) == 0) continue;
    const qa_q3_base_item_definition *item = qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, (qa_q3_powerup)j);
    if (item) {
      qa_q3_pres_client_draw_tools * tools = tools_of(draw);
      const qa_q3_pres_scene_shader * shader = item_shader(draw, item);
      qa_q3_pres_client_draw_tools_draw_pic(tools, rect, shader);
      return;
    }
  }
}
static void paint_area_powerup(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double alignment, double special, double scale, qa_vec4 color) {
  qa_q3_base_source_player_state *ps = ps_of(draw);
  qa_draw2d *target = &tools_of(draw)->draw;
  if (qa_q3_base_player_state_slots_get(&ps->stats, qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK)->health) <= 0) return;
  struct { int powerup; int32_t remaining; } sorted[16];
  size_t count = 0;
  for (int i = 0; i < 16; i++) {
    double expiry = qa_q3_base_player_state_slots_get(&ps->powerups, i);
    int32_t remaining = qa_int32(expiry - draw->state->time);
    if (expiry == 0 || remaining <= 0 || remaining >= 999000) continue;
    size_t index = 0;
    while (index < count && sorted[index].remaining < remaining) index++;
    memmove(&sorted[index + 1], &sorted[index], (count - index) * sizeof sorted[0]);
    sorted[index].powerup = i;
    sorted[index].remaining = remaining;
    count++;
  }
  double x = rect.x, y = rect.y;
  for (size_t i = 0; i < count; i++) {
    const qa_q3_base_item_definition *item = qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, (qa_q3_powerup)sorted[i].powerup);
    if (!item) continue;
    int32_t remaining = sorted[i].remaining;
    if (remaining >= 5000) {
      qa_draw2d_set_color(target, NULL);
    } else {
      double phase = f(f(remaining) / 1000);
      float alpha = (float)f(phase - trunc(phase));
      qa_vec4 fade = {alpha, alpha, alpha, alpha};
      qa_draw2d_set_color(target, &fade);
    }
    qa_rect icon = {(float)x, (float)y, (float)f(rect.width * 0.75), rect.height};
    qa_q3_pres_client_draw_tools * tools = tools_of(draw);
    const qa_q3_pres_scene_shader * shader = item_shader(draw, item);
    qa_q3_pres_client_draw_tools_draw_pic(tools, icon, shader);
    char text[NUMBER_TEXT];
    snprintf(text, sizeof text, "%d", sorted[i].remaining / 1000);
    paint_text_at(draw, scale, color, text, 0, f(f(x + f(rect.width * 0.75)) + 3), f(y + rect.height));
    if (alignment == 0) y = f(y + f(rect.width + special));
    else x = f(x + f(rect.width + special));
  }
  qa_draw2d_set_color(target, NULL);
}
static void paint_team_info(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double text_y, double scale, qa_vec4 color) {
  qa_q3_pres_client_game_state *cg = draw->state;
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  qa_draw2d *target = &tools_of(draw)->draw;
  double count = qa_js_min(cg->num_sorted_team_players, 8), team = player_team(draw);
  /* The source measures these unused maxima before drawing; font/config lookups remain ordered. */
  for (double i = 0; i < count; i++) {
    qa_q3_pres_client_info *ci = client_at(draw, sorted_at(draw, i));
    if (ci->info_valid && ci->team == team) (void)width_text(draw, client_name(ci), scale);
  }
  for (int i = 1; i < MAX_LOCATIONS; i++) {
    const char *location = draw->host.config_string(draw->host.ctx, CS_LOCATIONS + i);
    if (location && location[0] != '\0') (void)width_text(draw, location, scale);
  }
  double y = rect.y;
  for (double i = 0; i < count; i++) {
    qa_q3_pres_client_info *ci = client_at(draw, sorted_at(draw, i));
    if (!ci->info_valid || ci->team != team) continue;
    double x = trunc(f(rect.x + 1));
    for (int j = 0; j <= QA_Q3_PW_NUM_POWERUPS; j++) {
      if ((qa_int32(ci->powerups) & (1 << j)) == 0) continue;
      const qa_q3_base_item_definition *item = qa_q3_base_find_item_for_powerup(QA_Q3_BASE_PRODUCT_MISSIONPACK, (qa_q3_powerup)j);
      if (item) {
        qa_rect icon = {(float)x, (float)y, 12, 12};
        qa_q3_pres_client_draw_tools * tools = tools_of(draw);
        const qa_q3_pres_scene_shader * shader = item_shader(draw, item);
        qa_q3_pres_client_draw_tools_draw_pic(tools, icon, shader);
        x += 12;
      }
    }
    x = trunc(f(rect.x + 38));
    qa_vec4 health = qa_q3_pres_get_color_for_health(ci->health, ci->armor);
    qa_draw2d_set_color(target, &health);
    qa_rect heart = {(float)x, (float)f(y + 1), 10, 10};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), heart, draw->host.graphics->heart_shader);
    x += 13;
    qa_draw2d_set_color(target, NULL);
    const qa_q3_pres_scene_shader *handle = order_blinked_off(draw) ? NULL
        : qa_q3_pres_mission_owner_draw_status_handle(draw, cgs->order_pending ? cgs->current_order : ci->team_task);
    if (handle) {
      qa_rect status = {(float)x, (float)y, 12, 12};
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), status, handle);
    }
    x += 13;
    double left_over = f(rect.width - x), maximum = f(x + f(left_over / 3));
    (void)paint_limited(draw, client_name(ci), x, f(y + text_y), scale, color, maximum, 0);
    const char *location = location_text(draw, ci->location);
    x = trunc(f(x + f(f(left_over / 3) + 2)));
    (void)paint_limited(draw, location, x, f(y + text_y), scale, color, f(rect.width - 4), 0);
    y = f(y + f(text_y + 2));
    if (f(f(y + text_y) + 2) > f(rect.y + rect.height)) break;
  }
}
/* TS String.slice(start) over the spectator bytes. */
static const char *slice_from(const char *text, double start) {
  double length = (double)strlen(text);
  if (start < 0) start = qa_js_max(length + start, 0);
  return text + (size_t)qa_js_min(start, length);
}
static void paint_spectators(qa_q3_pres_mission_owner_draw *draw, qa_rect rect, double scale, qa_vec4 color) {
  qa_q3_pres_client_game_state *cg = draw->state;
  if (cg->spectator_len == 0) return;
  const char *list = cg->spectator_list ? cg->spectator_list : "";
  if (cg->spectator_width == -1) {
    cg->spectator_width = 0;
    cg->spectator_paint_x = trunc(f(rect.x + 1));
    cg->spectator_paint_x2 = -1;
  }
  if (cg->spectator_offset > cg->spectator_len) {
    cg->spectator_offset = 0;
    cg->spectator_paint_x = trunc(f(rect.x + 1));
    cg->spectator_paint_x2 = -1;
  }
  if (cg->time > cg->spectator_time) {
    cg->spectator_time = qa_int32(cg->time + 10);
    if (cg->spectator_paint_x <= f(rect.x + 2)) {
      if (cg->spectator_offset < cg->spectator_len) {
        const qa_q3_font_set * fonts2 = draw->host.fonts(draw->host.ctx);
        const char * text = slice_from(list, cg->spectator_offset);
        double width = qa_q3_text_width(fonts2, text, scale, 1);
        cg->spectator_paint_x = qa_int32(cg->spectator_paint_x + width - 1);
        cg->spectator_offset++;
      } else {
        cg->spectator_offset = 0;
        cg->spectator_paint_x = cg->spectator_paint_x2 >= 0 ? cg->spectator_paint_x2 : trunc(f(f(rect.x + rect.width) - 2));
        cg->spectator_paint_x2 = -1;
      }
    } else {
      cg->spectator_paint_x = qa_int32(cg->spectator_paint_x - 1);
      if (cg->spectator_paint_x2 >= 0) cg->spectator_paint_x2 = qa_int32(cg->spectator_paint_x2 - 1);
    }
  }
  double maximum = f(f(rect.x + rect.width) - 2), baseline = f(f(rect.y + rect.height) - 3);
  double painted = paint_limited(draw, slice_from(list, cg->spectator_offset), cg->spectator_paint_x, baseline, scale, color, maximum, 0);
  if (cg->spectator_paint_x2 >= 0) (void)paint_limited(draw, list, cg->spectator_paint_x2, baseline, scale, color, maximum, cg->spectator_offset);
  if (cg->spectator_offset != 0 && painted > 0) {
    if (cg->spectator_paint_x2 == -1) cg->spectator_paint_x2 = trunc(maximum);
  } else {
    cg->spectator_paint_x2 = -1;
  }
}
static void paint_medal(qa_q3_pres_mission_owner_draw *draw, int id, qa_rect rect, double scale, qa_vec4 color, const qa_ui_picture_or_lazy *picture) {
  qa_q3_pres_client_game_state *cg = draw->state;
  size_t length = sizeof cg->scores / sizeof cg->scores[0];
  const qa_q3_pres_client_score *score = &cg->scores[slot_index(cg->selected_score, length)];
  qa_draw2d *target = &tools_of(draw)->draw;
  double value = 0;
  switch (id) {
    case QA_Q3_PRES_OD_ACCURACY: value = f(score->accuracy); break;
    case QA_Q3_PRES_OD_ASSISTS: value = f(score->assist_count); break;
    case QA_Q3_PRES_OD_DEFEND: value = f(score->defend_count); break;
    case QA_Q3_PRES_OD_EXCELLENT: value = f(score->excellent_count); break;
    case QA_Q3_PRES_OD_IMPRESSIVE: value = f(score->impressive_count); break;
    case QA_Q3_PRES_OD_PERFECT: value = f(score->perfect); break;
    case QA_Q3_PRES_OD_GAUNTLET: value = f(score->guantlet_count); break;
    default: value = f(score->captures); break;
  }
  char text[NUMBER_TEXT + 1];
  bool has_text = false;
  color.w = 0.25;
  if (value > 0) {
    has_text = true;
    if (id == QA_Q3_PRES_OD_PERFECT) {
      color.w = 1;
      strcpy(text, "Wow");
    } else {
      qa_net_js_number(trunc(value), text, NUMBER_TEXT);
      if (id == QA_Q3_PRES_OD_ACCURACY) {
        strcat(text, "%");
        if (value > 50) color.w = 1;
      } else {
        color.w = 1;
      }
    }
  }
  qa_draw2d_set_color(target, &color);
  if (picture->kind != QA_UI_PICTURE_NONE) {
    stretch_picture(draw, rect, picture);
  } else {
    const qa_q3_pres_renderer_resources *resources = &draw->host.icons->media.resources;
    qa_picture_asset zero;
    memset(&zero, 0, sizeof zero);
    zero.kind = QA_PICTURE_MATERIAL;
    zero.material = resources->picture(resources->ctx, NULL);
    qa_draw2d_stretch_pic(target, rect, FULL_UV, &zero);
  }
  if (has_text) {
    color.w = 1;
    double width = width_text(draw, text, scale);
    paint_text_at(draw, scale, color, text, 0, f(rect.x + f(f(rect.width - width) / 2)), f(f(rect.y + rect.height) + 10));
  }
  qa_draw2d_set_color(target, NULL);
}
void qa_q3_pres_mission_owner_draw_paint(qa_q3_pres_mission_owner_draw *draw,
                                         const qa_ui_owner_draw_paint_request *request) {
  if (request->draw != &tools_of(draw)->draw)
    qa_range_error("Mission owner drawing must share the ordered HUD recorder");
  if (owner_cvar(draw, "cg_drawStatus") == 0) return;
  qa_rect rect = request->rect;
  double scale = request->text_scale, style = request->text_style;
  qa_vec4 color = request->color;
  const qa_ui_picture_or_lazy *picture = &request->background;
  bool force_2d = (qa_int32(request->owner_draw_flags) & QA_Q3_PRES_OD_SHOW_2DONLY) != 0;
  qa_q3_pres_client_game_state *state = draw->state;
  qa_q3_pres_client_game_static_state *cgs = draw->static_state;
  const qa_q3_pres_client_media_graphics *g = draw->host.graphics;
  int id = qa_int32(request->owner_draw);
  if ((double)id != request->owner_draw) return;
  char text[NUMBER_TEXT + 2];
  switch (id) {
    case QA_Q3_PRES_OD_PLAYER_ARMOR_ICON: paint_armor_icon(draw, rect, force_2d); break;
    case QA_Q3_PRES_OD_PLAYER_ARMOR_ICON2D: paint_armor_icon(draw, rect, true); break;
    case QA_Q3_PRES_OD_PLAYER_AMMO_ICON: paint_ammo_icon(draw, rect, force_2d); break;
    case QA_Q3_PRES_OD_PLAYER_AMMO_ICON2D: paint_ammo_icon(draw, rect, true); break;
    case QA_Q3_PRES_OD_PLAYER_AMMO_VALUE:
      if (!draw->host.weapon_hud &&
          qa_q3_pres_client_game_state_entity_at(state, ps_of(draw)->client_num)->current_state.weapon != 0 &&
          raw_value(draw, id) > -1)
        paint_number(draw, rect, scale, color, raw_value(draw, id), picture, style);
      break;
    case QA_Q3_PRES_OD_PLAYER_ARMOR_VALUE:
    case QA_Q3_PRES_OD_PLAYER_HEALTH:
    case QA_Q3_PRES_OD_PLAYER_SCORE:
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEALTH:
      paint_number(draw, rect, scale, color, raw_value(draw, id), picture, style);
      break;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_ARMOR:
      if (selected_client(draw)->armor > 0) paint_number(draw, rect, scale, color, selected_client(draw)->armor, picture, style);
      break;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_HEAD:
    case QA_Q3_PRES_OD_VOICE_HEAD: {
      qa_vec3 angles = {0, 180, 0};
      qa_q3_pres_client_draw_icons_draw_head(draw->host.icons, rect,
          id == QA_Q3_PRES_OD_VOICE_HEAD ? cgs->current_voice_client : selected_index(draw), angles);
      break;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_NAME:
    case QA_Q3_PRES_OD_VOICE_NAME:
      paint_text(draw, rect, scale, color,
                 client_name(client_at(draw, id == QA_Q3_PRES_OD_VOICE_NAME ? cgs->current_voice_client : selected_index(draw))), style);
      break;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_LOCATION:
      paint_text(draw, rect, scale, color, location_text(draw, selected_client(draw)->location), style);
      break;
    case QA_Q3_PRES_OD_PLAYER_LOCATION:
      paint_text(draw, rect, scale, color, location_text(draw, client_at(draw, ps_of(draw)->client_num)->location), style);
      break;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_STATUS: paint_selected_status(draw, rect); break;
    case QA_Q3_PRES_OD_PLAYER_STATUS:
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect,
          qa_q3_pres_mission_owner_draw_status_handle(draw, client_at(draw, ps_of(draw)->client_num)->team_task));
      break;
    case QA_Q3_PRES_OD_SELECTEDPLAYER_WEAPON: {
      const qa_q3_pres_scene_shader *icon = draw->host.weapon_icon(draw->host.ctx, selected_client(draw)->cur_weapon);
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(draw), rect, icon ? icon : g->defer_shader);
      break;
    }
    case QA_Q3_PRES_OD_SELECTEDPLAYER_POWERUP: paint_selected_powerup(draw, rect); break;
    case QA_Q3_PRES_OD_PLAYER_HEAD: paint_player_head(draw, rect); break;
    case QA_Q3_PRES_OD_PLAYER_ITEM: paint_item(draw, rect, false); break;
    case QA_Q3_PRES_OD_CTF_POWERUP: paint_item(draw, rect, true); break;
    case QA_Q3_PRES_OD_RED_SCORE:
    case QA_Q3_PRES_OD_BLUE_SCORE: {
      double value = id == QA_Q3_PRES_OD_RED_SCORE ? cgs->scores1 : cgs->scores2;
      if (value == SCORE_NOT_PRESENT) strcpy(text, "-");
      else qa_net_js_number(value, text, sizeof text);
      paint_text_at(draw, scale, color, text, style, f(f(rect.x + rect.width) - width_text(draw, text, scale)), f(rect.y + rect.height));
      break;
    }
    case QA_Q3_PRES_OD_RED_NAME:
    case QA_Q3_PRES_OD_BLUE_NAME: {
      qa_cvar_snapshot snapshot = draw->host.read_vm_cvar(draw->host.ctx, id == QA_Q3_PRES_OD_RED_NAME ? "g_redteam" : "g_blueteam");
      paint_text(draw, rect, scale, color, snapshot.value, style);
      qa_cvar_read_free(&snapshot);
      break;
    }
    case QA_Q3_PRES_OD_BLUE_FLAGHEAD: paint_flag_head(draw, rect, true); break;
    case QA_Q3_PRES_OD_RED_FLAGHEAD: paint_flag_head(draw, rect, false); break;
    case QA_Q3_PRES_OD_BLUE_FLAGSTATUS: paint_flag_status(draw, rect, true, picture); break;
    case QA_Q3_PRES_OD_RED_FLAGSTATUS: paint_flag_status(draw, rect, false, picture); break;
    case QA_Q3_PRES_OD_BLUE_FLAGNAME:
    case QA_Q3_PRES_OD_RED_FLAGNAME: {
      double carrier = flag_carrier(draw, id == QA_Q3_PRES_OD_BLUE_FLAGNAME);
      if (carrier >= 0) paint_text(draw, rect, scale, color, client_name(client_at(draw, carrier)), style);
      break;
    }
    case QA_Q3_PRES_OD_HARVESTER_SKULLS: paint_skulls(draw, rect, scale, color, false, style); break;
    case QA_Q3_PRES_OD_HARVESTER_SKULLS2D: paint_skulls(draw, rect, scale, color, true, style); break;
    case QA_Q3_PRES_OD_ONEFLAG_STATUS: paint_one_flag_status(draw, rect); break;
    case QA_Q3_PRES_OD_TEAM_COLOR:
      qa_q3_pres_client_draw_icons_draw_team_background(draw->host.icons, rect, color.w, player_team(draw));
      break;
    case QA_Q3_PRES_OD_AREA_POWERUP: paint_area_powerup(draw, rect, request->alignment, request->special, scale, color); break;
    case QA_Q3_PRES_OD_PLAYER_HASFLAG: paint_player_has_flag(draw, rect, false); break;
    case QA_Q3_PRES_OD_PLAYER_HASFLAG2D: paint_player_has_flag(draw, rect, true); break;
    case QA_Q3_PRES_OD_AREA_SYSTEMCHAT:
    case QA_Q3_PRES_OD_AREA_TEAMCHAT:
    case QA_Q3_PRES_OD_AREA_CHAT: {
      const char *system = "", *team1 = "", *team2 = "";
      draw->host.chat(draw->host.ctx, &system, &team1, &team2);
      paint_text(draw, rect, scale, color, id == QA_Q3_PRES_OD_AREA_SYSTEMCHAT ? system : id == QA_Q3_PRES_OD_AREA_TEAMCHAT ? team1 : team2, 0);
      break;
    }
    case QA_Q3_PRES_OD_GAME_TYPE: paint_text(draw, rect, scale, color, game_type_text(cgs->game_type), style); break;
    case QA_Q3_PRES_OD_GAME_STATUS: {
      char status[320];
      game_status_text(draw, status, sizeof status);
      paint_text(draw, rect, scale, color, status, style);
      break;
    }
    case QA_Q3_PRES_OD_KILLER:
      if (state->killer_name && state->killer_name[0] != '\0') {
        char *killer = killer_text(draw);
        double x = trunc(f(rect.x + f(rect.width / 2))) - trunc(width_text(draw, killer, scale) / 2);
        paint_text_at(draw, scale, color, killer, style, x, f(rect.y + rect.height));
        free(killer);
      }
      break;
    case QA_Q3_PRES_OD_ACCURACY:
    case QA_Q3_PRES_OD_ASSISTS:
    case QA_Q3_PRES_OD_DEFEND:
    case QA_Q3_PRES_OD_EXCELLENT:
    case QA_Q3_PRES_OD_IMPRESSIVE:
    case QA_Q3_PRES_OD_PERFECT:
    case QA_Q3_PRES_OD_GAUNTLET:
    case QA_Q3_PRES_OD_CAPTURES:
      paint_medal(draw, id, rect, scale, color, picture);
      break;
    case QA_Q3_PRES_OD_SPECTATORS: paint_spectators(draw, rect, scale, color); break;
    case QA_Q3_PRES_OD_TEAMINFO:
      if (owner_cvar(draw, "cg_currentSelectedPlayer") == state->num_sorted_team_players)
        paint_team_info(draw, rect, request->text_y, scale, color);
      break;
    case QA_Q3_PRES_OD_CAPFRAGLIMIT:
    case QA_Q3_PRES_OD_1STPLACE:
    case QA_Q3_PRES_OD_2NDPLACE: {
      double value = id == QA_Q3_PRES_OD_CAPFRAGLIMIT ? (cgs->game_type >= QA_Q3_BASE_GT_CTF ? cgs->capturelimit : cgs->fraglimit)
                     : id == QA_Q3_PRES_OD_1STPLACE ? cgs->scores1 : cgs->scores2;
      if (id == QA_Q3_PRES_OD_CAPFRAGLIMIT || value != SCORE_NOT_PRESENT) {
        char number[NUMBER_TEXT];
        qa_net_js_number(value, number, sizeof number);
        snprintf(text, sizeof text, "%2s", number);
        paint_text_at(draw, scale, color, text, style, rect.x, rect.y);
      }
      break;
    }
    default: break;
  }
}
