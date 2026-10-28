/* Port of src/content/q3/presentation/hud.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/scoreboard.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
const char *qa_q3_pres_hud_cvar_name(qa_q3_pres_hud_cvar cvar) {
  switch (cvar) {
    case QA_Q3_PRES_HUD_CVAR_DRAW_2D: return "cg_draw2D";
    case QA_Q3_PRES_HUD_CVAR_DRAW_STATUS: return "cg_drawStatus";
    case QA_Q3_PRES_HUD_CVAR_DRAW_ICONS: return "cg_drawIcons";
    case QA_Q3_PRES_HUD_CVAR_DRAW_3D_ICONS: return "cg_draw3dIcons";
    case QA_Q3_PRES_HUD_CVAR_DRAW_REWARDS: return "cg_drawRewards";
    case QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR: return "cg_drawCrosshair";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_HEALTH: return "cg_crosshairHealth";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_SIZE: return "cg_crosshairSize";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_X: return "cg_crosshairX";
    case QA_Q3_PRES_HUD_CVAR_CROSSHAIR_Y: return "cg_crosshairY";
    case QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR_NAMES: return "cg_drawCrosshairNames";
    case QA_Q3_PRES_HUD_CVAR_DRAW_AMMO_WARNING: return "cg_drawAmmoWarning";
    case QA_Q3_PRES_HUD_CVAR_PAUSED: return "cg_paused";
  }
  return "cg_draw2D";
}
static qa_cvar_snapshot hud_snapshot(qa_q3_pres_client_hud *hud, qa_q3_pres_hud_cvar name, double *integer, double *numeric) {
  qa_cvar_snapshot snapshot = hud->host.read_vm_cvar(hud->host.ctx, name);
  *integer = snapshot.integer_value;
  *numeric = snapshot.numeric_value;
  return snapshot;
}
static double hud_cvar_int(qa_q3_pres_client_hud *hud, qa_q3_pres_hud_cvar name) {
  double integer = 0, numeric = 0;
  qa_cvar_snapshot snapshot = hud_snapshot(hud, name, &integer, &numeric);
  qa_cvar_read_free(&snapshot);
  return integer;
}
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_client_hud *hud) { return hud->host.icons->tools; }
void qa_q3_pres_client_hud_init(qa_q3_pres_client_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_client_hud_host host,
    qa_q3_pres_client_hud_variant variant) {
  if (state->product != static_state->product || variant.kind != state->product || host.icons->state != state ||
      host.icons->media.static_state != static_state || host.status->state != state ||
      host.status->tools != host.icons->tools || host.corners->state != state ||
      host.corners->static_state != static_state || host.corners->icons != host.icons ||
      host.prediction->state != state || host.weapons->selection.state != state)
    qa_range_error("HUD services must share canonical cgame state, drawing and weapon media");
  memset(hud, 0, sizeof *hud);
  hud->state = state;
  hud->static_state = static_state;
  hud->host = host;
  hud->variant = variant;
}
static void draw_reward(qa_q3_pres_client_hud *hud) {
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_REWARDS) == 0) return;
  qa_q3_pres_client_game_state *state = hud->state;
  for (int i = 0; i < (int)state->reward_stack && i < QA_Q3_PRES_REWARD_COUNT; i++) {
    qa_rect rect = {320 - 24 + (i - (int)state->reward_stack) * 56, 40, 48, 48};
    qa_q3_pres_client_draw_tools_draw_pic(tools_of(hud), rect, state->rewards[i].shader);
    if (state->rewards[i].count > 1 && i == 0) {
      char text[16];
      snprintf(text, sizeof text, "%lld", (long long)state->rewards[i].count);
      qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), rect.x + 40, rect.y + 32, text, 1);
    }
  }
}
void qa_q3_pres_client_hud_draw_status_bar(qa_q3_pres_client_hud *hud) {
  if (!hud->state->snap) return;
  const qa_q3_base_stat_schema *schema =
      qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(&hud->state->snap->player_state));
  double health = qa_q3_base_player_state_slots_get(&hud->state->snap->player_state.stats, schema->health);
  double armor = qa_q3_base_player_state_slots_get(&hud->state->snap->player_state.stats, schema->armor);
  qa_vec4 color = qa_q3_pres_color_for_health(hud->state);
  qa_draw2d_set_color(&tools_of(hud)->draw, &color);
  qa_q3_pres_client_hud_corners_draw_field(hud->host.corners, 200, 432, 32, health, 3);
  qa_draw2d_set_color(&tools_of(hud)->draw, NULL);
  if (armor > 0) qa_q3_pres_client_hud_corners_draw_field(hud->host.corners, 380, 432, 32, armor, 3);
}
void qa_q3_pres_client_hud_draw_crosshair(qa_q3_pres_client_hud *hud) {
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR) == 0) return;
  if (!hud->state->snap) return;
  if (qa_q3_base_player_state_record_health(&hud->state->snap->player_state) <= 0) return;
  double size = hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_SIZE);
  double index = hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR) - 1;
  if (index < 0) index = 0;
  if (index > 9) index = 9;
  if (size <= 0) size = 24;
  double cx = hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_X);
  double cy = hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_Y);
  qa_rect rect = {320 - size / 2 + cx, 240 - size / 2 + cy, size, size};
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_CROSSHAIR_HEALTH) != 0) {
    qa_vec4 color = qa_q3_pres_color_for_health(hud->state);
    qa_draw2d_set_color(&tools_of(hud)->draw, &color);
  }
  qa_q3_pres_client_draw_tools_draw_pic(tools_of(hud), rect, hud->host.crosshair_shader(hud->host.ctx, index));
  qa_draw2d_set_color(&tools_of(hud)->draw, NULL);
}
void qa_q3_pres_client_hud_draw_crosshair_names(qa_q3_pres_client_hud *hud) {
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_CROSSHAIR_NAMES) == 0) return;
  qa_vec4 color;
  if (!qa_q3_pres_fade_color(hud->state->time, hud->state->crosshair_client_time, 1000, &color)) return;
  double client_index = hud->state->crosshair_client_num;
  if (client_index < 0 || client_index >= 64) return;
  const char *name = hud->static_state->client_info[(size_t)client_index].name;
  if (!name) name = "";
  double width = qa_q3_pres_draw_strlen(name) * 8;
  qa_q3_pres_client_draw_tools_draw_small_string_color(tools_of(hud), 320 - width / 2, 200, name, color);
}
void qa_q3_pres_client_hud_draw_spectator(qa_q3_pres_client_hud *hud) {
  if (!hud->state->snap) return;
  if (hud->state->snap->player_state.pm_type != 2) return; /* PM_SPECTATOR */
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 240, 440, "SPECTATOR", 1);
}
void qa_q3_pres_client_hud_draw_vote(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  if (cgs->vote_time == 0) return;
  char text[192];
  snprintf(text, sizeof text, "VOTE: %.100s  YES:%lld NO:%lld", cgs->vote_string ? cgs->vote_string : "",
           (long long)cgs->vote_yes, (long long)cgs->vote_no);
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(hud), 4, 400, text, 1);
}
void qa_q3_pres_client_hud_draw_team_vote(qa_q3_pres_client_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  for (int team = 0; team < 2; team++) {
    if (cgs->team_vote_time[team] == 0) continue;
    char text[192];
    snprintf(text, sizeof text, "TEAM VOTE: %.100s  YES:%lld NO:%lld",
             cgs->team_vote_string[team] ? cgs->team_vote_string[team] : "",
             (long long)cgs->team_vote_yes[team], (long long)cgs->team_vote_no[team]);
    qa_q3_pres_client_draw_tools_draw_small_string(tools_of(hud), 4, 416 + team * 16, text, 1);
  }
}
void qa_q3_pres_client_hud_draw_follow(qa_q3_pres_client_hud *hud) {
  if (!hud->state->snap) return;
  if (((int64_t)hud->state->snap->player_state.pm_flags & 8) == 0) return; /* PMF_FOLLOW */
  double client_index = hud->state->snap->player_state.client_num;
  (void)client_index;
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(hud), 280, 460, "following", 1);
}
void qa_q3_pres_client_hud_draw_ammo_warning(qa_q3_pres_client_hud *hud) {
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_AMMO_WARNING) == 0) return;
  if (hud->state->low_ammo_warning == 0) return;
  const char *text = hud->state->low_ammo_warning == 2 ? "OUT OF AMMO" : "LOW AMMO WARNING";
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 240, 380, text, 1);
}
void qa_q3_pres_client_hud_draw_warmup(qa_q3_pres_client_hud *hud) {
  if (hud->static_state->game_type == QA_Q3_BASE_GT_SINGLE_PLAYER) return;
  if (hud->state->warmup <= 0) return;
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(hud), 260, 120, "Warmup", 1);
}
bool qa_q3_pres_client_hud_draw_scoreboard(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3)
    return qa_q3_pres_base_scoreboard_draw(hud->variant.scoreboard);
  return hud->variant.draw_scoreboard(hud->variant.menus_ctx);
}
bool qa_q3_pres_client_hud_draw_tourney_scoreboard(qa_q3_pres_client_hud *hud) {
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3)
    return qa_q3_pres_base_scoreboard_draw_tourney(hud->variant.scoreboard);
  return qa_q3_pres_client_hud_draw_scoreboard(hud);
}
void qa_q3_pres_client_hud_draw_2d(qa_q3_pres_client_hud *hud) {
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_2D) == 0) return;
  bool scoreboard = qa_q3_pres_client_hud_draw_scoreboard(hud);
  (void)scoreboard;
  qa_q3_pres_client_draw_status_draw_center_string(hud->host.status);
  if (hud_cvar_int(hud, QA_Q3_PRES_HUD_CVAR_DRAW_STATUS) != 0 && !hud->state->intermission_started) {
    if (hud->variant.kind == QA_Q3_BASE_PRODUCT_BASEQ3) qa_q3_pres_client_hud_draw_status_bar(hud);
  }
  qa_q3_pres_client_hud_corners_draw_upper_right(hud->host.corners);
  qa_q3_pres_client_hud_corners_draw_lower_right(hud->host.corners);
  qa_q3_pres_client_hud_corners_draw_lower_left(hud->host.corners);
  qa_q3_pres_client_hud_corners_draw_team_info(hud->host.corners);
  qa_q3_pres_client_hud_draw_crosshair_names(hud);
  qa_q3_pres_client_hud_draw_crosshair(hud);
  draw_reward(hud);
  qa_q3_pres_client_hud_draw_spectator(hud);
  qa_q3_pres_client_hud_draw_vote(hud);
  qa_q3_pres_client_hud_draw_team_vote(hud);
  qa_q3_pres_client_hud_draw_follow(hud);
  qa_q3_pres_client_hud_draw_ammo_warning(hud);
  qa_q3_pres_client_hud_draw_warmup(hud);
  if (hud->variant.kind == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    hud->variant.draw_timed_menus(hud->variant.menus_ctx);
}
