/* Port of src/content/q3/presentation/hud-corners.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/hud_corners.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/info_string.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
enum {
  ICON_SIZE = 48, CHAR_WIDTH = 32, CHAR_HEIGHT = 48, BIGCHAR_WIDTH = 16, BIGCHAR_HEIGHT = 16,
  TINYCHAR_WIDTH = 8, TINYCHAR_HEIGHT = 8, MAX_TEAM_OVERLAY_PLAYERS = 8, TEAM_OVERLAY_MAXNAME_WIDTH = 12,
  TEAM_OVERLAY_MAXLOCATION_WIDTH = 16, MAX_LOCATIONS = 64, TEAMCHAT_HEIGHT = 8, SCORE_NOT_PRESENT = -9999,
  CS_PLAYERS = 544, CS_LOCATIONS = 608, ATTACKER_HEAD_TIME = 10000, POWERUP_BLINKS = 5,
  POWERUP_BLINK_TIME = 1000, PULSE_TIME = 200, FPS_FRAMES = 4
};
static const double PULSE_SCALE = 1.5;
static float f32(double v) { return (float)v; }
static const qa_q3_powerup POWERUPS[16] = {QA_Q3_PW_NONE, QA_Q3_PW_QUAD, QA_Q3_PW_BATTLESUIT, QA_Q3_PW_HASTE,
    QA_Q3_PW_INVIS, QA_Q3_PW_REGEN, QA_Q3_PW_FLIGHT, QA_Q3_PW_REDFLAG, QA_Q3_PW_BLUEFLAG, QA_Q3_PW_NEUTRALFLAG,
    QA_Q3_PW_SCOUT, QA_Q3_PW_GUARD, QA_Q3_PW_DOUBLER, QA_Q3_PW_AMMOREGEN, QA_Q3_PW_INVULNERABILITY,
    QA_Q3_PW_NUM_POWERUPS};
static double integer_division(double numerator, double denominator) {
  return (double)qa_int32(trunc(numerator / denominator));
}
const char *qa_q3_pres_hud_corner_cvar_name(qa_q3_pres_hud_corner_cvar cvar) {
  switch (cvar) {
    case QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY: return "cg_drawTeamOverlay";
    case QA_Q3_PRES_CORNER_CVAR_SNAPSHOT: return "cg_drawSnapshot";
    case QA_Q3_PRES_CORNER_CVAR_FPS: return "cg_drawFPS";
    case QA_Q3_PRES_CORNER_CVAR_TIMER: return "cg_drawTimer";
    case QA_Q3_PRES_CORNER_CVAR_ATTACKER: return "cg_drawAttacker";
    case QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_HEIGHT: return "cg_teamChatHeight";
    case QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_TIME: return "cg_teamChatTime";
  }
  return "cg_drawTeamOverlay";
}
static double corner_cvar(qa_q3_pres_client_hud_corners *corners, qa_q3_pres_hud_corner_cvar name) {
  qa_cvar_snapshot snapshot = corners->host.read_vm_cvar(corners->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_client_hud_corners *corners) {
  return corners->icons->tools;
}
static qa_q3_base_source_player_state *active_player_state(qa_q3_pres_client_hud_corners *corners) {
  if (!corners->state->snap) qa_range_error("HUD corners require a current snapshot");
  return &corners->state->snap->player_state;
}
static void require_base(qa_q3_pres_client_hud_corners *corners, const char *source_name) {
  if (corners->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    qa_range_error("%s is not compiled in missionpack", source_name);
}
void qa_q3_pres_client_hud_corners_init(qa_q3_pres_client_hud_corners *corners,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_draw_icons *icons, qa_q3_pres_hud_corners_host host) {
  if (icons->state != state) qa_range_error("HUD corners and icons must share client state");
  if (icons->media.static_state != static_state)
    qa_range_error("HUD corners and media must share static state");
  if (state->product != static_state->product) qa_range_error("HUD corner products differ");
  memset(corners, 0, sizeof *corners);
  corners->state = state;
  corners->static_state = static_state;
  corners->icons = icons;
  corners->host = host;
}
void qa_q3_pres_client_hud_corners_draw_field(qa_q3_pres_client_hud_corners *corners, double x, double y,
                                              double width, double value) {
  if (corners->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    qa_range_error("CG_DrawField is not compiled in missionpack");
  if (width < 1) return;
  if (width > 5) width = 5;
  int32_t v = qa_int32(value);
  if (width == 1) v = v < 0 ? 0 : v > 9 ? 9 : v;
  else if (width == 2) v = v < -9 ? -9 : v > 99 ? 99 : v;
  else if (width == 3) v = v < -99 ? -99 : v > 999 ? 999 : v;
  else if (width == 4) v = v < -999 ? -999 : v > 9999 ? 9999 : v;
  char text[32];
  snprintf(text, sizeof text, "%lld", (long long)v);
  double length = fmin((double)strlen(text), width);
  double draw_x = x + 2 + CHAR_WIDTH * (width - length);
  for (int index = 0; index < (int)length; index++) {
    int frame = text[index] == '-' ? 10 : text[index] - '0';
    if (frame < 0 || frame > 10 || !corners->host.graphics)
      qa_range_error("Invalid field digit frame");
    qa_rect rect = {draw_x, y, CHAR_WIDTH, CHAR_HEIGHT};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect,
                                          corners->host.graphics->number_shaders[frame]);
    draw_x += CHAR_WIDTH;
  }
}
static double draw_attacker(qa_q3_pres_client_hud_corners *corners, double y) {
  qa_q3_pres_client_game_state *state = corners->state;
  if (!state->snap) qa_range_error("CG_DrawAttacker requires a current snapshot");
  qa_q3_base_source_player_state *predicted = &state->predicted_player_state;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
  if (qa_q3_base_player_state_slots_get(&predicted->stats, schema->health) <= 0 || state->attacker_time == 0)
    return y;
  double client_num = qa_q3_base_player_state_slots_get(&predicted->persistant, QA_Q3_BASE_PERS_ATTACKER);
  if (client_num < 0 || client_num >= 64 || client_num == state->snap->player_state.client_num) return y;
  if (qa_int32(state->time - state->attacker_time) > ATTACKER_HEAD_TIME) {
    state->attacker_time = 0;
    return y;
  }
  double size = f32(ICON_SIZE * f32(1.25));
  qa_rect head = {f32(640 - size), y, size, size};
  qa_vec3 angles = {0, 180, 0};
  qa_q3_pres_client_draw_icons_draw_head(corners->icons, head, client_num, angles);
  const char *config = corners->host.config_string(corners->host.ctx, CS_PLAYERS + client_num);
  char *name = qa_info_value_for_key(config ? config : "", "n");
  y = f32(y + size);
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners),
      trunc(640 - qa_q3_pres_draw_strlen(name ? name : "") * BIGCHAR_WIDTH), trunc(y), name ? name : "", 0.5);
  free(name);
  return f32(y + BIGCHAR_HEIGHT + 2);
}
static double draw_snapshot_value(qa_q3_pres_client_hud_corners *corners, double y) {
  if (!corners->state->snap) qa_range_error("CG_DrawSnapshot requires a current snapshot");
  char text[96];
  snprintf(text, sizeof text, "time:%lld snap:%lld cmd:%lld",
           (long long)corners->state->snap->server_time, (long long)qa_int32(corners->state->latest_snapshot_num),
           (long long)qa_int32(corners->static_state->server_command_sequence));
  double width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH;
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), 635 - width, trunc(y + 2), text, 1);
  return f32(y + BIGCHAR_HEIGHT + 4);
}
static double draw_fps(qa_q3_pres_client_hud_corners *corners, double y) {
  int32_t time = qa_int32(corners->host.milliseconds(corners->host.ctx));
  int32_t frame_time = qa_int32((double)time - corners->fps_previous);
  corners->fps_previous = time;
  corners->fps_previous_times[(size_t)fmod(corners->fps_index, FPS_FRAMES)] = frame_time;
  corners->fps_index++;
  if (corners->fps_index > FPS_FRAMES) {
    int32_t total = 0;
    for (int i = 0; i < FPS_FRAMES; i++) total = qa_int32((double)total + corners->fps_previous_times[i]);
    if (total == 0) total = 1;
    double fps = integer_division(1000 * FPS_FRAMES, total);
    char text[32];
    snprintf(text, sizeof text, "%lldfps", (long long)qa_int32(fps));
    qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners),
        635 - qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH, trunc(y + 2), text, 1);
  }
  return f32(y + BIGCHAR_HEIGHT + 4);
}
static double draw_timer(qa_q3_pres_client_hud_corners *corners, double y) {
  int32_t milliseconds = qa_int32(corners->state->time - corners->static_state->level_start_time);
  double seconds = integer_division(milliseconds, 1000);
  double minutes = integer_division(seconds, 60);
  seconds = (double)qa_int32(seconds - minutes * 60);
  double tens = integer_division(seconds, 10);
  seconds = (double)qa_int32(seconds - tens * 10);
  char text[48];
  snprintf(text, sizeof text, "%lld:%lld%lld", (long long)qa_int32(minutes), (long long)qa_int32(tens),
           (long long)qa_int32(seconds));
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners),
      635 - qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH, trunc(y + 2), text, 1);
  return f32(y + BIGCHAR_HEIGHT + 4);
}
static qa_q3_pres_client_info *team_client(qa_q3_pres_client_hud_corners *corners, double sorted_index) {
  if (sorted_index != trunc(sorted_index) || sorted_index < 0 || sorted_index >= 8)
    qa_range_error("Invalid sorted team player index");
  double client_number = corners->state->sorted_team_players[(size_t)sorted_index];
  if (client_number != trunc(client_number) || client_number < 0 || client_number >= 64)
    qa_range_error("Invalid sorted team client number");
  return &corners->static_state->client_info[(size_t)client_number];
}
static void tiny_text(qa_q3_pres_client_hud_corners *corners, double x, double y, const char *text,
                      qa_vec4 color, double max_chars) {
  qa_q3_fixed_text_options options = {x, y, text, color, 8, 8, max_chars, false, false};
  qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(corners), options);
}
static double draw_team_overlay(qa_q3_pres_client_hud_corners *corners, double y, bool right, bool upper) {
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 0) return y;
  qa_q3_base_source_player_state *ps = active_player_state(corners);
  double team = qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM);
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return y;
  double count = fmin(corners->state->num_sorted_team_players, MAX_TEAM_OVERLAY_PLAYERS);
  double players = 0, player_width = 0;
  for (double index = 0; index < count; index++) {
    qa_q3_pres_client_info *client = team_client(corners, index);
    if (client->info_valid && client->team == (qa_q3_base_team)team) {
      players++;
      player_width = fmax(player_width, qa_q3_pres_draw_strlen(client->name ? client->name : ""));
    }
  }
  if (players == 0) return y;
  player_width = fmin(player_width, TEAM_OVERLAY_MAXNAME_WIDTH);
  double location_width = 0;
  for (double index = 1; index < MAX_LOCATIONS; index++) {
    const char *location = corners->host.config_string(corners->host.ctx, CS_LOCATIONS + index);
    if (location && location[0] != '\0')
      location_width = fmax(location_width, qa_q3_pres_draw_strlen(location));
  }
  location_width = fmin(location_width, TEAM_OVERLAY_MAXLOCATION_WIDTH);
  double width = (player_width + location_width + 11) * TINYCHAR_WIDTH;
  double x = right ? 640 - width : 0;
  double height = players * TINYCHAR_HEIGHT;
  double return_y = upper ? y + height : y - height;
  if (!upper) y -= height;
  qa_vec4 bar_color = team == QA_Q3_BASE_TEAM_RED ? (qa_vec4){1, 0, 0, f32(0.33)}
                                                  : (qa_vec4){0, 0, 1, f32(0.33)};
  qa_draw2d_set_color(&tools_of(corners)->draw, &bar_color);
  if (corners->host.graphics) {
    qa_rect bar = {x, y, width, height};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), bar, corners->host.graphics->team_status_bar);
  }
  qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  qa_vec4 white = {1, 1, 1, 1};
  for (double index = 0; index < count; index++) {
    qa_q3_pres_client_info *client = team_client(corners, index);
    if (!client->info_valid || client->team != (qa_q3_base_team)team) continue;
    double xx = x + TINYCHAR_WIDTH;
    tiny_text(corners, xx, y, client->name ? client->name : "", white, TEAM_OVERLAY_MAXNAME_WIDTH);
    if (location_width != 0) {
      const char *location = corners->host.config_string(corners->host.ctx, CS_LOCATIONS + client->location);
      if (!location || location[0] == '\0') location = "unknown";
      xx = x + TINYCHAR_WIDTH * 2 + TINYCHAR_WIDTH * player_width;
      tiny_text(corners, xx, y, location, white, TEAM_OVERLAY_MAXLOCATION_WIDTH);
    }
    xx = x + TINYCHAR_WIDTH * 3 + TINYCHAR_WIDTH * player_width + TINYCHAR_WIDTH * location_width;
    char health_text[32];
    snprintf(health_text, sizeof health_text, "%3lld %3lld", (long long)qa_int32(client->health),
             (long long)qa_int32(client->armor));
    tiny_text(corners, xx, y, health_text, qa_q3_pres_get_color_for_health(client->health, client->armor), 0);
    xx += TINYCHAR_WIDTH * 3;
    const qa_q3_pres_scene_shader *weapon_icon = corners->host.weapon_icon
        ? corners->host.weapon_icon(corners->host.ctx, client->cur_weapon) : NULL;
    qa_rect weapon_rect = {xx, y, 8, 8};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), weapon_rect,
        weapon_icon ? weapon_icon : corners->host.graphics ? corners->host.graphics->defer_shader : NULL);
    xx = right ? x : x + width - TINYCHAR_WIDTH;
    for (int p = 0; p < 16; p++) {
      if (((int64_t)client->powerups & (1LL << POWERUPS[p])) == 0) continue;
      const qa_q3_base_item_definition *item =
          qa_q3_base_find_item_for_powerup(corners->state->product, POWERUPS[p]);
      if (!item) continue;
      const qa_q3_pres_scene_shader *shader = item->icon == NULL || !corners->host.register_shader
          ? NULL : corners->host.register_shader(corners->host.ctx, item->icon);
      qa_rect powerup_rect = {xx, y, 8, 8};
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), powerup_rect, shader);
      xx += right ? -TINYCHAR_WIDTH : TINYCHAR_WIDTH;
    }
    y += TINYCHAR_HEIGHT;
  }
  return f32(return_y);
}
static double draw_free_score(qa_q3_pres_client_hud_corners *corners, double x, double y, double score,
                              bool selected, bool first) {
  char text[32];
  snprintf(text, sizeof text, "%2lld", (long long)qa_int32(score));
  double width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH + 8;
  x -= width;
  qa_vec4 color = selected ? (first ? (qa_vec4){0, 0, 1, f32(0.33)} : (qa_vec4){1, 0, 0, f32(0.33)})
                            : (qa_vec4){f32(0.5), f32(0.5), f32(0.5), f32(0.33)};
  qa_rect rect = {x, y - 4, width, BIGCHAR_HEIGHT + 8};
  qa_q3_pres_client_draw_tools_fill_rect(tools_of(corners), rect, &color);
  if (selected && corners->host.graphics)
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect, corners->host.graphics->select_shader);
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), x + 4, trunc(y), text, 1);
  return x;
}
static double draw_scores(qa_q3_pres_client_hud_corners *corners, double y) {
  require_base(corners, "CG_DrawScores");
  qa_q3_base_source_player_state *ps = active_player_state(corners);
  qa_q3_pres_client_game_static_state *cgs = corners->static_state;
  int32_t score1 = qa_int32(cgs->scores1), score2 = qa_int32(cgs->scores2);
  y = f32(y - BIGCHAR_HEIGHT - 8);
  double y1 = y, x = 640;
  const qa_q3_pres_client_media_graphics *g = corners->host.graphics;
  if (cgs->game_type >= QA_Q3_BASE_GT_TEAM) {
    char text[32];
    snprintf(text, sizeof text, "%2lld", (long long)score2);
    double width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH + 8;
    x -= width;
    qa_rect blue_rect = {x, y - 4, width, BIGCHAR_HEIGHT + 8};
    qa_vec4 blue = {0, 0, 1, f32(0.33)};
    qa_q3_pres_client_draw_tools_fill_rect(tools_of(corners), blue_rect, &blue);
    if (qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) == QA_Q3_BASE_TEAM_BLUE && g)
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), blue_rect, g->select_shader);
    qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), x + 4, trunc(y), text, 1);
    if (cgs->game_type == QA_Q3_BASE_GT_CTF &&
        qa_q3_base_find_item_for_powerup(corners->state->product, QA_Q3_PW_BLUEFLAG) != NULL) {
      y1 = f32(y - BIGCHAR_HEIGHT - 8);
      if (cgs->blueflag >= 0 && cgs->blueflag <= 2) {
        if (!g) qa_range_error("Invalid blue flag status");
        qa_rect flag_rect = {x, y1 - 4, width, BIGCHAR_HEIGHT + 8};
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), flag_rect,
                                              g->blue_flag_shader[(size_t)cgs->blueflag]);
      }
    }
    snprintf(text, sizeof text, "%2lld", (long long)score1);
    width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH + 8;
    x -= width;
    qa_rect red_rect = {x, y - 4, width, BIGCHAR_HEIGHT + 8};
    qa_vec4 red = {1, 0, 0, f32(0.33)};
    qa_q3_pres_client_draw_tools_fill_rect(tools_of(corners), red_rect, &red);
    if (qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) == QA_Q3_BASE_TEAM_RED && g)
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), red_rect, g->select_shader);
    qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), x + 4, trunc(y), text, 1);
    if (cgs->game_type == QA_Q3_BASE_GT_CTF &&
        qa_q3_base_find_item_for_powerup(corners->state->product, QA_Q3_PW_REDFLAG) != NULL) {
      y1 = f32(y - BIGCHAR_HEIGHT - 8);
      if (cgs->redflag >= 0 && cgs->redflag <= 2) {
        if (!g) qa_range_error("Invalid red flag status");
        qa_rect flag_rect = {x, y1 - 4, width, BIGCHAR_HEIGHT + 8};
        qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), flag_rect,
                                              g->red_flag_shader[(size_t)cgs->redflag]);
      }
    }
    double limit = cgs->game_type >= QA_Q3_BASE_GT_CTF ? cgs->capturelimit : cgs->fraglimit;
    if (limit != 0) {
      snprintf(text, sizeof text, "%2lld", (long long)qa_int32(limit));
      double limit_width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH + 8;
      x -= limit_width;
      qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), x + 4, trunc(y), text, 1);
    }
  } else {
    double score = qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE);
    bool spectator = qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) ==
                     QA_Q3_BASE_TEAM_SPECTATOR;
    if (score1 != score) score2 = (int32_t)score;
    if (score2 != SCORE_NOT_PRESENT)
      x = draw_free_score(corners, x, y, score2, !spectator && score == score2 && score != score1, false);
    if (score1 != SCORE_NOT_PRESENT)
      x = draw_free_score(corners, x, y, score1, !spectator && score == score1, true);
    if (cgs->fraglimit != 0) {
      char text[32];
      snprintf(text, sizeof text, "%2lld", (long long)qa_int32(cgs->fraglimit));
      double width = qa_q3_pres_draw_strlen(text) * BIGCHAR_WIDTH + 8;
      x -= width;
      qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), x + 4, trunc(y), text, 1);
    }
  }
  return f32(y1 - 8);
}
static double draw_powerups(qa_q3_pres_client_hud_corners *corners, double y) {
  require_base(corners, "CG_DrawPowerups");
  qa_q3_base_source_player_state *ps = active_player_state(corners);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(corners->state->product);
  if (qa_q3_base_player_state_slots_get(&ps->stats, schema->health) <= 0) return y;
  int sorted[16];
  int32_t sorted_times[16];
  int sorted_count = 0;
  for (int powerup = 0; powerup < (int)ps->powerups.length && powerup < 16; powerup++) {
    double expiration = qa_q3_base_player_state_slots_get(&ps->powerups, powerup);
    if (expiration == 0) continue;
    int32_t remaining = qa_int32(expiration - corners->state->time);
    if (remaining < 0 || remaining > 999000) continue;
    int insertion = 0;
    while (insertion < sorted_count && sorted_times[insertion] < remaining) insertion++;
    for (int i = sorted_count; i > insertion; i--) {
      sorted[i] = sorted[i - 1];
      sorted_times[i] = sorted_times[i - 1];
    }
    sorted[insertion] = powerup;
    sorted_times[insertion] = remaining;
    sorted_count++;
  }
  double x = 640 - ICON_SIZE - CHAR_WIDTH * 2;
  for (int index = 0; index < sorted_count; index++) {
    int powerup = sorted[index];
    int32_t remaining = sorted_times[index];
    const qa_q3_base_item_definition *item =
        qa_q3_base_find_item_for_powerup(corners->state->product, POWERUPS[powerup]);
    if (!item) continue;
    y = f32(y - ICON_SIZE);
    qa_vec4 field_color = {1, f32(0.2), f32(0.2), 1};
    qa_draw2d_set_color(&tools_of(corners)->draw, &field_color);
    qa_q3_pres_client_hud_corners_draw_field(corners, x, y, 2, integer_division(remaining, 1000));
    qa_vec4 modulation;
    const qa_vec4 *modulation_ptr = NULL;
    if (remaining < POWERUP_BLINKS * POWERUP_BLINK_TIME) {
      double fraction = f32(f32((double)remaining) / f32(POWERUP_BLINK_TIME));
      fraction = f32(fraction - trunc(fraction));
      modulation = (qa_vec4){fraction, fraction, fraction, fraction};
      modulation_ptr = &modulation;
    }
    qa_draw2d_set_color(&tools_of(corners)->draw, modulation_ptr);
    double size = ICON_SIZE;
    if (corners->state->powerup_active == powerup &&
        qa_int32(corners->state->time - corners->state->powerup_time) < PULSE_TIME) {
      double pulse = f32(1 - f32(f32(f32(corners->state->time) - f32(corners->state->powerup_time)) /
                                 f32(PULSE_TIME)));
      size = f32(f32(ICON_SIZE) * f32(1 + f32(f32(PULSE_SCALE - 1) * pulse)));
    }
    const qa_q3_pres_scene_shader *shader = item->icon == NULL || !corners->host.register_shader
        ? NULL : corners->host.register_shader(corners->host.ctx, item->icon);
    qa_rect rect = {f32(640 - size), f32(f32(y + ICON_SIZE / 2) - f32(size / 2)), size, size};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect, shader);
  }
  qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  return y;
}
static double draw_pickup_item(qa_q3_pres_client_hud_corners *corners, double y) {
  require_base(corners, "CG_DrawPickupItem");
  qa_q3_base_source_player_state *ps = active_player_state(corners);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(corners->state->product);
  if (qa_q3_base_player_state_slots_get(&ps->stats, schema->health) <= 0) return y;
  y = (double)qa_int32(y - ICON_SIZE);
  int32_t value = qa_int32(corners->state->item_pickup);
  if (value == 0) return y;
  qa_vec4 fade;
  if (!qa_q3_pres_fade_color(corners->state->time, corners->state->item_pickup_time, 3000, &fade)) return y;
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(corners->state->product, value);
  const qa_q3_pres_scene_shader *icon = NULL;
  if (!corners->host.pickup_visual || !corners->host.pickup_visual(corners->host.ctx, value, &icon))
    qa_range_error("Pickup item has no cgame visual slot");
  qa_draw2d_set_color(&tools_of(corners)->draw, &fade);
  qa_rect rect = {8, y, ICON_SIZE, ICON_SIZE};
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect, icon);
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(corners), ICON_SIZE + 16,
      y + ICON_SIZE / 2 - BIGCHAR_HEIGHT / 2, item && item->pickup_name ? item->pickup_name : "", fade.x);
  qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  return y;
}
void qa_q3_pres_client_hud_corners_draw_upper_right(qa_q3_pres_client_hud_corners *corners) {
  double y = 0;
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 1)
    y = draw_team_overlay(corners, y, true, true);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_SNAPSHOT) != 0) y = draw_snapshot_value(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_FPS) != 0) y = draw_fps(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TIMER) != 0) y = draw_timer(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_ATTACKER) != 0) (void)draw_attacker(corners, y);
}
void qa_q3_pres_client_hud_corners_draw_lower_right(qa_q3_pres_client_hud_corners *corners) {
  require_base(corners, "CG_DrawLowerRight");
  double y = 480 - ICON_SIZE;
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 2)
    y = draw_team_overlay(corners, y, true, false);
  y = draw_scores(corners, y);
  (void)draw_powerups(corners, y);
}
void qa_q3_pres_client_hud_corners_draw_lower_left(qa_q3_pres_client_hud_corners *corners) {
  require_base(corners, "CG_DrawLowerLeft");
  double y = 480 - ICON_SIZE;
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 3)
    y = draw_team_overlay(corners, y, false, false);
  (void)draw_pickup_item(corners, trunc(y));
}
static const char *team_chat_msg(qa_q3_pres_client_game_static_state *cgs, double index, double chat_height) {
  double slot = fmod(index, chat_height);
  if (slot < 0 || slot >= QA_Q3_PRES_TEAM_CHAT_COUNT) qa_range_error("Invalid team chat ring index");
  return cgs->team_chat_msgs[(size_t)slot];
}
void qa_q3_pres_client_hud_corners_draw_team_info(qa_q3_pres_client_hud_corners *corners) {
  require_base(corners, "CG_DrawTeamInfo");
  double chat_height = fmin(corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_HEIGHT), TEAMCHAT_HEIGHT);
  if (chat_height <= 0) return;
  qa_q3_pres_client_game_static_state *cgs = corners->static_state;
  if (cgs->team_last_chat_pos == cgs->team_chat_pos) return;
  double oldest_slot = fmod(cgs->team_last_chat_pos, chat_height);
  if (oldest_slot < 0 || oldest_slot >= QA_Q3_PRES_TEAM_CHAT_COUNT)
    qa_range_error("Invalid team chat ring index");
  double oldest = cgs->team_chat_msg_times[(size_t)oldest_slot];
  if (qa_int32(corners->state->time - oldest) >
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_TIME))
    cgs->team_last_chat_pos++;
  double height = (cgs->team_chat_pos - cgs->team_last_chat_pos) * TINYCHAR_HEIGHT;
  double width = 0;
  for (double index = cgs->team_last_chat_pos; index < cgs->team_chat_pos; index++) {
    const char *message = team_chat_msg(cgs, index, chat_height);
    width = fmax(width, qa_q3_pres_draw_strlen(message ? message : ""));
  }
  width = width * TINYCHAR_WIDTH + TINYCHAR_WIDTH * 2;
  (void)width;
  double team = qa_q3_base_player_state_slots_get(&active_player_state(corners)->persistant,
                                                  QA_Q3_BASE_PERS_TEAM);
  qa_vec4 color = team == QA_Q3_BASE_TEAM_RED ? (qa_vec4){1, 0, 0, f32(0.33)}
                  : team == QA_Q3_BASE_TEAM_BLUE ? (qa_vec4){0, 0, 1, f32(0.33)}
                                                 : (qa_vec4){0, 1, 0, f32(0.33)};
  qa_draw2d_set_color(&tools_of(corners)->draw, &color);
  if (corners->host.graphics) {
    qa_rect bar = {0, 420 - height, 640, height};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), bar, corners->host.graphics->team_status_bar);
  }
  qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  qa_vec4 white = {1, 1, 1, 1};
  for (double index = cgs->team_chat_pos - 1; index >= cgs->team_last_chat_pos; index--) {
    const char *message = team_chat_msg(cgs, index, chat_height);
    qa_q3_fixed_text_options options = {8, 420 - (cgs->team_chat_pos - index) * 8,
                                        message ? message : "", white, TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 0,
                                        false, false};
    qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(corners), options);
  }
}
