/* Port of src/content/q3/presentation/scoreboard.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/scoreboard.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/presentation/events.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
const char *qa_q3_pres_scoreboard_cvar_name(qa_q3_pres_scoreboard_cvar cvar) {
  return cvar == QA_Q3_PRES_SCOREBOARD_CVAR_PAUSED ? "cg_paused" : "cg_drawIcons";
}
void qa_q3_pres_base_scoreboard_init(qa_q3_pres_base_scoreboard *scoreboard,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_base_scoreboard_host host) {
  if (state->product != static_state->product) qa_range_error("Scoreboard state products differ");
  if (host.icons->state != state || host.icons->media.static_state != static_state ||
      host.players->host.state != state)
    qa_range_error("Scoreboard services must share canonical cgame state");
  for (int index = 0; index < 64; index++) {
    if (qa_q3_pres_client_info_store_client_info(host.clients, index) != &static_state->client_info[index])
      qa_range_error("Scoreboard client store must use canonical client slots");
  }
  scoreboard->state = state;
  scoreboard->static_state = static_state;
  scoreboard->host = host;
  scoreboard->local_client = false;
}
static qa_q3_pres_client_draw_tools *tools_of(qa_q3_pres_base_scoreboard *board) {
  return board->host.icons->tools;
}
static double board_cvar(qa_q3_pres_base_scoreboard *board, qa_q3_pres_scoreboard_cvar name) {
  qa_cvar_snapshot snapshot = board->host.read_vm_cvar(board->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static void draw_client_score(qa_q3_pres_base_scoreboard *board, qa_q3_pres_client_score *score, double y) {
  qa_q3_pres_client_info *info =
      qa_q3_pres_client_info_store_client_info(board->host.clients, score->client);
  if (!info->info_valid) return;
  char text[160];
  snprintf(text, sizeof text, "%.60s %lld %lld %lld", info->name ? info->name : "",
           (long long)score->score, (long long)score->ping, (long long)score->time);
  qa_rect head = {152, y, 32, 32};
  qa_vec3 angles = {board->state->head_start_pitch, board->state->head_start_yaw, 0};
  if (board_cvar(board, QA_Q3_PRES_SCOREBOARD_CVAR_DRAW_ICONS) != 0)
    qa_q3_pres_client_draw_icons_draw_head(board->host.icons, head, score->client, angles);
  qa_q3_pres_client_draw_tools_draw_small_string(tools_of(board), 200, y + 8, text, 1);
}
static bool request_scores_if_stale(qa_q3_pres_base_scoreboard *board) {
  /* CG_DrawOldScoreboard: refresh outdated scores every 2 seconds. */
  if (board->state->show_scores && board->state->time - board->state->scores_request_time > 2000) {
    board->state->scores_request_time = board->state->time;
    board->host.send_client_command(board->host.ctx, "score\n");
  }
  return board->state->show_scores || board->state->intermission_started;
}
bool qa_q3_pres_base_scoreboard_draw(qa_q3_pres_base_scoreboard *board) {
  if (!board->state->snap) qa_range_error("CG_DrawOldScoreboard requires a current snapshot");
  if (!request_scores_if_stale(board)) return false;
  qa_vec4 fade;
  bool has_fade = qa_q3_pres_fade_color(board->state->time, board->state->score_fade_time, 500, &fade);
  if (!board->state->show_scores && !board->state->intermission_started && !has_fade) return false;
  char *place = qa_q3_pres_place_string(board->state->snap->player_state.persistant.length ? 1 : 1);
  (void)place;
  /* Header: fraglimit / place line through the big-string tool. */
  qa_q3_pres_client_draw_tools_draw_big_string(tools_of(board), 200, 86, "Score", 1);
  double y = 118;
  int normal = board->state->intermission_started ? 17 : 7;
  int shown = 0;
  for (int i = 0; i < (int)board->state->num_scores && shown < normal; i++) {
    draw_client_score(board, &board->state->scores[i], y);
    y += board->state->intermission_started ? 16 : 40;
    shown++;
  }
  return true;
}
bool qa_q3_pres_base_scoreboard_draw_tourney(qa_q3_pres_base_scoreboard *board) {
  if (board->static_state->game_type != QA_Q3_BASE_GT_TOURNAMENT) return false;
  return qa_q3_pres_base_scoreboard_draw(board);
}
