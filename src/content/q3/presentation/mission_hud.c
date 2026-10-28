/* Port of src/content/q3/presentation/mission-hud.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mission_hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/core/numeric.h"
void qa_q3_pres_mission_hud_init(qa_q3_pres_mission_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_mission_owner_draw *owner_draw,
    qa_q3_pres_mission_hud_host host) {
  memset(hud, 0, sizeof *hud);
  hud->state = state;
  hud->static_state = static_state;
  hud->owner_draw = owner_draw;
  hud->host = host;
}
void qa_q3_pres_mission_hud_reset_strings(qa_q3_pres_mission_hud *hud) {
  /* TS resets the UI string buffer through the host runtime reset. */
  hud->host.reset_menus(hud->host.ctx);
}
void qa_q3_pres_mission_hud_reset_menus(qa_q3_pres_mission_hud *hud) {
  hud->menus_loaded = false;
  hud->host.reset_menus(hud->host.ctx);
}
bool qa_q3_pres_mission_hud_load_menus(qa_q3_pres_mission_hud *hud, const char *path) {
  if (!hud->host.load_menus(hud->host.ctx, path)) return false;
  hud->menus_loaded = true;
  return true;
}
void qa_q3_pres_mission_hud_dispose(qa_q3_pres_mission_hud *hud) { hud->menus_loaded = false; }
void qa_q3_pres_mission_hud_init_team_chat(qa_q3_pres_mission_hud *hud) {
  hud->static_state->team_chat_pos = 0;
  hud->static_state->team_last_chat_pos = 0;
}
bool qa_q3_pres_mission_hud_chat(qa_q3_pres_mission_hud *hud, const char *text) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  size_t slot = (size_t)fmod(cgs->team_chat_pos, QA_Q3_PRES_TEAM_CHAT_COUNT);
  if (slot >= QA_Q3_PRES_TEAM_CHAT_COUNT) slot = 0;
  char **cell = &cgs->team_chat_msgs[slot];
  char truncated[160];
  snprintf(truncated, sizeof truncated, "%.150s", text ? text : "");
  if (!qa_q3_pres_set_string(cell, truncated)) return false;
  cgs->team_chat_msg_times[slot] = hud->state->time;
  cgs->team_chat_pos++;
  return true;
}
void qa_q3_pres_mission_hud_check_order_pending(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  if (!cgs->order_pending) return;
  if (hud->state->time >= cgs->order_time) cgs->order_pending = false;
}
double qa_q3_pres_mission_hud_get_selected_player(qa_q3_pres_mission_hud *hud) {
  (void)hud;
  /* cg_currentSelectedPlayer indexes the sorted team list in the TS. */
  return hud->state->selected_score >= 0 && hud->state->selected_score < 64 ? hud->state->selected_score : 0;
}
static void cycle_selected(qa_q3_pres_mission_hud *hud, double direction) {
  qa_q3_pres_client_game_state *state = hud->state;
  if (state->num_sorted_team_players <= 0) return;
  double current = 0;
  for (int i = 0; i < (int)state->num_sorted_team_players && i < 8; i++) {
    if (state->sorted_team_players[i] == qa_q3_pres_mission_hud_get_selected_player(hud)) current = i;
  }
  double next = fmod(current + direction + state->num_sorted_team_players, state->num_sorted_team_players);
  if (next < 0 || next >= 8) return;
  state->selected_score = state->sorted_team_players[(size_t)next];
}
void qa_q3_pres_mission_hud_select_next_player(qa_q3_pres_mission_hud *hud) { cycle_selected(hud, 1); }
void qa_q3_pres_mission_hud_select_previous_player(qa_q3_pres_mission_hud *hud) { cycle_selected(hud, -1); }
static int feeder_slot(double feeder) {
  return feeder == QA_Q3_PRES_FEEDER_RED ? 0 : feeder == QA_Q3_PRES_FEEDER_BLUE ? 1 : 2;
}
static bool score_on_team(qa_q3_pres_client_score *score, double feeder) {
  if (feeder == QA_Q3_PRES_FEEDER_SCOREBOARD) return true;
  double team = feeder == QA_Q3_PRES_FEEDER_RED ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE;
  return score->team == team;
}
double qa_q3_pres_mission_hud_feeder_count(qa_q3_pres_mission_hud *hud, double feeder) {
  double count = 0;
  for (int i = 0; i < (int)hud->state->num_scores; i++)
    if (score_on_team(&hud->state->scores[i], feeder)) count++;
  return count;
}
bool qa_q3_pres_mission_hud_feeder_item(qa_q3_pres_mission_hud *hud, double feeder, double index,
                                        char *text, size_t text_size) {
  double seen = 0;
  for (int i = 0; i < (int)hud->state->num_scores; i++) {
    qa_q3_pres_client_score *score = &hud->state->scores[i];
    if (!score_on_team(score, feeder)) continue;
    if (seen++ != index) continue;
    double client_index = score->client;
    const char *stored_name = client_index >= 0 && client_index < 64
        ? hud->static_state->client_info[(size_t)client_index].name : NULL;
    const char *name = stored_name ? stored_name : "";
    snprintf(text, text_size, "%.40s %lld %lld", name, (long long)score->score, (long long)score->ping);
    return true;
  }
  return false;
}
double qa_q3_pres_mission_hud_feeder_selection(qa_q3_pres_mission_hud *hud, double feeder) {
  return hud->score_selection[feeder_slot(feeder)];
}
void qa_q3_pres_mission_hud_set_score_selection(qa_q3_pres_mission_hud *hud, double feeder, double index) {
  double count = qa_q3_pres_mission_hud_feeder_count(hud, feeder);
  if (index < 0) index = 0;
  if (count > 0 && index >= count) index = count - 1;
  hud->score_selection[feeder_slot(feeder)] = index;
}
void qa_q3_pres_mission_hud_clear_scoreboard(qa_q3_pres_mission_hud *hud) {
  hud->state->score_board_showing = false;
  hud->score_selection[0] = hud->score_selection[1] = hud->score_selection[2] = 0;
}
bool qa_q3_pres_mission_hud_menu_scoreboard(qa_q3_pres_mission_hud *hud) {
  return hud->host.menu_scoreboard_open(hud->host.ctx);
}
bool qa_q3_pres_mission_hud_scroll_feeder(qa_q3_pres_mission_hud *hud, double feeder, bool down) {
  double selection = qa_q3_pres_mission_hud_feeder_selection(hud, feeder) + (down ? 1 : -1);
  qa_q3_pres_mission_hud_set_score_selection(hud, feeder, selection);
  return true;
}
void qa_q3_pres_mission_hud_frame(qa_q3_pres_mission_hud *hud, double time) {
  (void)time;
  qa_q3_pres_mission_hud_check_order_pending(hud);
}
void qa_q3_pres_mission_hud_paint_all(qa_q3_pres_mission_hud *hud) {
  hud->host.paint_all(hud->host.ctx, hud->state->time);
}
bool qa_q3_pres_mission_hud_draw_scoreboard(qa_q3_pres_mission_hud *hud) {
  if (!hud->state->show_scores && !hud->state->intermission_started) return false;
  hud->state->score_board_showing = true;
  hud->host.show_menu_by_name(hud->host.ctx, "score_menu");
  return true;
}
void qa_q3_pres_mission_hud_draw_timed_menus(qa_q3_pres_mission_hud *hud) {
  hud->host.paint_all(hud->host.ctx, hud->state->time);
}
double qa_q3_pres_mission_hud_client_num_from_name(qa_q3_pres_mission_hud *hud, const char *name) {
  for (int i = 0; i < 64; i++) {
    const char *candidate = hud->static_state->client_info[i].name;
    if (candidate && name && strcmp(candidate, name) == 0) return i;
  }
  return -1;
}
void qa_q3_pres_mission_hud_hide_team_menu(qa_q3_pres_mission_hud *hud) {
  hud->host.close_by_name(hud->host.ctx, "teamMenu");
  hud->static_state->event_handling = 0;
}
void qa_q3_pres_mission_hud_show_team_menu(qa_q3_pres_mission_hud *hud) {
  hud->host.show_menu_by_name(hud->host.ctx, "teamMenu");
  hud->static_state->event_handling = 1;
}
bool qa_q3_pres_mission_hud_other_team_has_flag(qa_q3_pres_mission_hud *hud) {
  return qa_q3_pres_mission_owner_draw_other_team_has_flag(hud->owner_draw);
}
bool qa_q3_pres_mission_hud_your_team_has_flag(qa_q3_pres_mission_hud *hud) {
  return qa_q3_pres_mission_owner_draw_your_team_has_flag(hud->owner_draw);
}
