/* Port of src/content/q3/presentation/hud-corners.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/hud_corners.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/core/info_string.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
enum { CS_LOCATIONS = 608, TEAMCHAT_HEIGHT = 8 };
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
void qa_q3_pres_client_hud_corners_init(qa_q3_pres_client_hud_corners *corners,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_draw_icons *icons, qa_q3_pres_hud_corners_host host) {
  memset(corners, 0, sizeof *corners);
  corners->state = state;
  corners->static_state = static_state;
  corners->icons = icons;
  corners->host = host;
}
void qa_q3_pres_client_hud_corners_draw_field(qa_q3_pres_client_hud_corners *corners, double x, double y,
                                              double width, double value, double digits) {
  /* CG_DrawField: blank leading cells, '-' for negatives, digits. */
  char text[32];
  snprintf(text, sizeof text, "%lld", (long long)qa_int32(value));
  size_t length = strlen(text);
  double cell = width;
  double cursor = x + (digits - (double)length) * cell;
  if (length > (size_t)digits) {
    cursor = x;
  }
  for (size_t i = 0; i < length; i++) {
    qa_rect rect = {cursor, y, cell, cell * 1.5};
    if (text[i] >= '0' && text[i] <= '9')
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect,
                                            corners->host.number_shader(corners->host.ctx, text[i] - '0'));
    else if (text[i] == '-')
      qa_q3_pres_client_draw_tools_draw_pic(tools_of(corners), rect,
                                            corners->host.number_shader(corners->host.ctx, 10));
    cursor += cell;
  }
}
static qa_q3_base_source_player_state *active_player_state(qa_q3_pres_client_hud_corners *corners) {
  if (!corners->state->snap) qa_range_error("HUD corners require a current snapshot");
  return &corners->state->snap->player_state;
}
static double draw_attacker(qa_q3_pres_client_hud_corners *corners, double y) {
  qa_q3_pres_client_game_state *state = corners->state;
  if (state->attacker_time == 0) return y;
  qa_vec4 color;
  if (!qa_q3_pres_fade_color(state->time, state->attacker_time, 10000, &color)) return y;
  qa_q3_base_source_player_state *ps = active_player_state(corners);
  const qa_q3_base_stat_schema *schema =
      qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
  (void)schema;
  double attacker = qa_q3_base_player_state_slots_get(&ps->persistant, 9); /* PERS_ATTACKER */
  if (attacker < 0 || attacker >= 64) return y;
  qa_rect head = {640 - 48 - 4, y, 48, 48};
  qa_vec3 angles = {state->head_start_pitch, state->head_start_yaw, 0};
  qa_draw2d_set_color(&tools_of(corners)->draw, &color);
  qa_q3_pres_client_draw_icons_draw_head(corners->icons, head, attacker, angles);
  qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  return y + 48;
}
static double draw_snapshot_value(qa_q3_pres_client_hud_corners *corners, double y) {
  char text[64];
  snprintf(text, sizeof text, "time %lld snap %lld", (long long)corners->state->time,
           (long long)corners->state->latest_snapshot_num);
  double x = 640 - qa_q3_pres_draw_strlen(text) * 8 - 4;
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), x, y, text, 1);
  return y + 16;
}
static double draw_fps(qa_q3_pres_client_hud_corners *corners, double y) {
  double now = corners->host.milliseconds(corners->host.ctx);
  corners->fps_frames[(size_t)corners->fps_index] = now - corners->fps_previous;
  corners->fps_previous = now;
  corners->fps_index = fmod(corners->fps_index + 1, 4);
  double total = 0;
  for (int i = 0; i < 4; i++) total += corners->fps_frames[i];
  double fps = total > 0 ? 4000.0 / total : 0;
  char text[32];
  snprintf(text, sizeof text, "%lld fps", (long long)qa_int32(fps));
  double x = 640 - qa_q3_pres_draw_strlen(text) * 8 - 4;
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), x, y, text, 1);
  return y + 16;
}
static double draw_timer(qa_q3_pres_client_hud_corners *corners, double y) {
  double seconds = floor(corners->state->time / 1000);
  char text[32];
  snprintf(text, sizeof text, "%lld:%02lld", (long long)floor(seconds / 60), (long long)fmod(seconds, 60));
  double x = 640 - qa_q3_pres_draw_strlen(text) * 8 - 4;
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), x, y, text, 1);
  return y + 16;
}
static void draw_team_overlay(qa_q3_pres_client_hud_corners *corners, bool upper) {
  /* Sorted team players with location/health/armor/powerup columns. */
  qa_q3_pres_client_game_state *state = corners->state;
  double y = upper ? 0 : 480 - 8 * 13;
  for (int slot = 0; slot < 8 && slot < (int)state->num_sorted_team_players; slot++) {
    double client_index = state->sorted_team_players[slot];
    if (client_index < 0 || client_index >= 64) continue;
    qa_q3_pres_client_info *info = &corners->static_state->client_info[(size_t)client_index];
    if (!info->info_valid) continue;
    char location_buffer[256];
    const char *config = corners->host.config_string(corners->host.ctx, CS_LOCATIONS + info->location);
    snprintf(location_buffer, sizeof location_buffer, "%.200s", config ? config : "");
    qa_vec4 health_color = qa_q3_pres_get_color_for_health(info->health, info->armor);
    double row_y = upper ? y + slot * 13 : y + slot * 13;
    qa_draw2d_set_color(&tools_of(corners)->draw, &health_color);
    qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), 640 - 8 * 30, row_y,
                                                   info->name ? info->name : "", 1);
    qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), 640 - 8 * 17, row_y, location_buffer, 1);
    qa_draw2d_set_color(&tools_of(corners)->draw, NULL);
  }
}
void qa_q3_pres_client_hud_corners_draw_upper_right(qa_q3_pres_client_hud_corners *corners) {
  double y = 0;
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 1) {
    draw_team_overlay(corners, true);
    y += 8 * 13;
  }
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_SNAPSHOT) != 0) y = draw_snapshot_value(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_FPS) != 0) y = draw_fps(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TIMER) != 0) y = draw_timer(corners, y);
  if (corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_ATTACKER) != 0) (void)draw_attacker(corners, y);
}
void qa_q3_pres_client_hud_corners_draw_lower_right(qa_q3_pres_client_hud_corners *corners) {
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 2)
    draw_team_overlay(corners, false);
}
void qa_q3_pres_client_hud_corners_draw_lower_left(qa_q3_pres_client_hud_corners *corners) {
  if (corners->static_state->game_type >= QA_Q3_BASE_GT_TEAM &&
      corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_OVERLAY) == 3) {
    /* Left overlay shares the row layout in the TS. */
    draw_team_overlay(corners, false);
  }
}
void qa_q3_pres_client_hud_corners_draw_team_info(qa_q3_pres_client_hud_corners *corners) {
  qa_q3_pres_client_game_static_state *cgs = corners->static_state;
  if (cgs->game_type < QA_Q3_BASE_GT_TEAM) return;
  double chat_height = fmin(corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_HEIGHT), TEAMCHAT_HEIGHT);
  if (chat_height <= 0) return;
  if (cgs->team_last_chat_pos == cgs->team_chat_pos) return;
  /* Expire the oldest message exactly as the TS cursor advance. */
  double oldest = cgs->team_chat_msg_times[(size_t)fmod(cgs->team_last_chat_pos, QA_Q3_PRES_TEAM_CHAT_COUNT)];
  if (qa_int32(corners->state->time - oldest) > corner_cvar(corners, QA_Q3_PRES_CORNER_CVAR_TEAM_CHAT_TIME))
    cgs->team_last_chat_pos++;
  double y = 480 - 60 - chat_height * 10;
  double count = 0;
  for (double pos = cgs->team_last_chat_pos; pos < cgs->team_chat_pos && count < chat_height; pos++, count++) {
    const char *message = cgs->team_chat_msgs[(size_t)fmod(pos, QA_Q3_PRES_TEAM_CHAT_COUNT)];
    qa_q3_pres_client_draw_tools_draw_small_string(tools_of(corners), 4, y + count * 10,
                                                   message ? message : "", 1);
  }
}
