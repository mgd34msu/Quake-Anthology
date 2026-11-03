/* Port of src/content/q3/presentation/scoreboard.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/scoreboard.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/presentation/events.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
enum { HEADER_Y = 86, TOP_Y = 118, NORMAL_HEIGHT = 40, INTER_HEIGHT = 16, MAX_NORMAL = 7, MAX_INTER = 17 };
static float f32(double v) { return (float)v; }
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
static qa_q3_base_source_player_state *snapshot_ps(qa_q3_pres_base_scoreboard *board) {
  if (!board->state->snap) qa_range_error("CG_DrawOldScoreboard requires a current snapshot");
  return &board->state->snap->player_state;
}
static qa_q3_pres_client_score *score_at(qa_q3_pres_base_scoreboard *board, double index) {
  if (index != trunc(index) || index < 0 || index >= QA_Q3_PRES_MAX_CLIENTS)
    qa_range_error("Scoreboard score index outside source array: %lld", (long long)qa_int32(index));
  return &board->state->scores[(size_t)index];
}
static qa_q3_pres_client_info *client_at(qa_q3_pres_base_scoreboard *board, double index) {
  if (index != trunc(index) || index < 0 || index >= QA_Q3_PRES_MAX_CLIENTS)
    qa_range_error("Scoreboard client index outside source array: %lld", (long long)qa_int32(index));
  return &board->static_state->client_info[(size_t)index];
}
static void draw_client_score(qa_q3_pres_base_scoreboard *board, double y, qa_q3_pres_client_score *score,
                              qa_vec4 color, double fade, bool large) {
  if (score->client < 0 || score->client >= board->static_state->maxclients) {
    char text[64];
    snprintf(text, sizeof text, "Bad score->client: %lld\n", (long long)qa_int32(score->client));
    board->host.print(board->host.ctx, text);
    return;
  }
  qa_q3_pres_client_info *ci = client_at(board, score->client);
  qa_q3_pres_client_draw_tools *tools = tools_of(board);
  const double icon_x = 80, head_x = 112;
  qa_rect icon_rect = {icon_x, large ? y - 8 : y, large ? 32 : 16, large ? 32 : 16};
  int32_t powerups = qa_js_to_int32(ci->powerups); /* TS powerups & x: ToInt32 */
  if ((powerups & (1 << QA_Q3_PW_NEUTRALFLAG)) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(board->host.icons, icon_rect, QA_Q3_BASE_TEAM_FREE, false);
  else if ((powerups & (1 << QA_Q3_PW_REDFLAG)) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(board->host.icons, icon_rect, QA_Q3_BASE_TEAM_RED, false);
  else if ((powerups & (1 << QA_Q3_PW_BLUEFLAG)) != 0)
    qa_q3_pres_client_draw_icons_draw_flag_model(board->host.icons, icon_rect, QA_Q3_BASE_TEAM_BLUE, false);
  else {
    if (ci->bot_skill > 0 && ci->bot_skill <= 5) {
      if (board_cvar(board, QA_Q3_PRES_SCOREBOARD_CVAR_DRAW_ICONS) != 0) {
        if (!board->host.graphics) qa_range_error("Scoreboard bot skill outside source shader array");
        qa_q3_pres_client_draw_tools_draw_pic(tools, icon_rect,
                                              board->host.graphics->bot_skill_shaders[(size_t)ci->bot_skill - 1]);
      }
    } else if (ci->handicap < 100) {
      char text[32];
      snprintf(text, sizeof text, "%lld", (long long)qa_int32(ci->handicap));
      qa_q3_pres_client_draw_tools_draw_small_string_color(
          tools, icon_x, board->static_state->game_type == QA_Q3_BASE_GT_TOURNAMENT ? y - 8 : y, text, color);
    }
    if (board->static_state->game_type == QA_Q3_BASE_GT_TOURNAMENT) {
      char text[64];
      snprintf(text, sizeof text, "%lld/%lld", (long long)qa_int32(ci->wins), (long long)qa_int32(ci->losses));
      qa_q3_pres_client_draw_tools_draw_small_string_color(
          tools, icon_x, ci->handicap < 100 && ci->bot_skill == 0 ? y + 8 : y, text, color);
    }
  }
  qa_rect head = {head_x, large ? y - 16 : y, large ? 48 : 16, large ? 48 : 16};
  qa_vec3 angles = {0, 180, 0};
  qa_q3_pres_client_draw_icons_draw_head(board->host.icons, head, score->client, angles);
  if (board->state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && board->host.graphics) {
    qa_rect task = {head_x + 48, y, 16, 16};
    if (ci->team_task == 1)
      qa_q3_pres_client_draw_tools_draw_pic(tools, task, board->host.graphics->assault_shader);
    else if (ci->team_task == 2)
      qa_q3_pres_client_draw_tools_draw_pic(tools, task, board->host.graphics->defend_shader);
  }
  char text[1088];
  const char *name = ci->name ? ci->name : "";
  if (score->ping == -1) snprintf(text, sizeof text, " connecting    %s", name);
  else if (ci->team == QA_Q3_BASE_TEAM_SPECTATOR)
    snprintf(text, sizeof text, " SPECT %3lld %4lld %s", (long long)qa_int32(score->ping),
             (long long)qa_int32(score->time), name);
  else
    snprintf(text, sizeof text, "%5lld %4lld %4lld %s", (long long)qa_int32(score->score),
             (long long)qa_int32(score->ping), (long long)qa_int32(score->time), name);
  qa_q3_base_source_player_state *ps = snapshot_ps(board);
  if (score->client == ps->client_num) {
    board->local_client = true;
    double rank = qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) ==
                          QA_Q3_BASE_TEAM_SPECTATOR ||
                      board->static_state->game_type >= QA_Q3_BASE_GT_TEAM
                  ? -1
                  : (double)(qa_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_RANK)) &
                             ~0x4000);
    qa_vec3 rgb = rank == 0 ? (qa_vec3){0, 0, f32(0.7)} : rank == 1 ? (qa_vec3){f32(0.7), 0, 0}
                  : rank == 2 ? (qa_vec3){f32(0.7), f32(0.7), 0} : (qa_vec3){f32(0.7), f32(0.7), f32(0.7)};
    qa_rect fill = {176, y, 512, 17};
    qa_vec4 fill_color = {rgb.x, rgb.y, rgb.z, f32(fade * 0.7)};
    qa_q3_pres_client_draw_tools_fill_rect(tools, fill, &fill_color);
  }
  qa_q3_pres_client_draw_tools_draw_big_string(tools, 160, y, text, fade);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps));
  int32_t ready = qa_int32(qa_q3_base_player_state_slots_get(&ps->stats, schema->clients_ready));
  int32_t ready_bit = (int32_t)(1u << ((unsigned)qa_int32(score->client) & 31u));
  if ((ready & ready_bit) != 0)
    qa_q3_pres_client_draw_tools_draw_big_string_color(tools, icon_x, y, "READY", color);
}
static double team_scoreboard(qa_q3_pres_base_scoreboard *board, double y, qa_q3_base_team team, double fade,
                              double max_clients, double line_height) {
  double count = 0;
  qa_vec4 color = {1, 1, 1, fade};
  for (double index = 0; index < board->state->num_scores && count < max_clients; index++) {
    qa_q3_pres_client_score *score = score_at(board, index);
    if (client_at(board, score->client)->team != team) continue;
    draw_client_score(board, y + line_height * count, score, color, fade, line_height == NORMAL_HEIGHT);
    count++;
  }
  return count;
}
static void reset_via_presenter(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_player_presenter_reset_player_entity((qa_q3_pres_player_presenter *)ctx, entity);
}
bool qa_q3_pres_base_scoreboard_draw(qa_q3_pres_base_scoreboard *board) {
  qa_q3_pres_client_game_state *state = board->state;
  qa_q3_pres_client_game_static_state *cgs = board->static_state;
  if (board_cvar(board, QA_Q3_PRES_SCOREBOARD_CVAR_PAUSED) != 0) {
    state->deferred_player_loading = 0;
    return false;
  }
  if (cgs->game_type == QA_Q3_BASE_GT_SINGLE_PLAYER &&
      state->predicted_player_state.pm_type == QA_Q3_PM_INTERMISSION) {
    state->deferred_player_loading = 0;
    return false;
  }
  if (state->warmup != 0 && !state->show_scores) return false;
  qa_vec4 color = {1, 1, 1, 1};
  bool white = state->show_scores || state->predicted_player_state.pm_type == QA_Q3_PM_DEAD ||
               state->predicted_player_state.pm_type == QA_Q3_PM_INTERMISSION;
  if (!white && !qa_q3_pres_fade_color(state->time, state->score_fade_time, 200, &color)) {
    state->deferred_player_loading = 0;
    free(state->killer_name);
    state->killer_name = NULL;
    return false;
  }
  double fade = color.x;
  qa_q3_pres_client_draw_tools *tools = tools_of(board);
  qa_q3_base_source_player_state *ps = snapshot_ps(board);
  if (state->killer_name && state->killer_name[0] != '\0') {
    char text[1088];
    snprintf(text, sizeof text, "Fragged by %s", state->killer_name);
    qa_q3_pres_client_draw_tools_draw_big_string(tools, trunc((640 - qa_q3_pres_draw_strlen(text) * 16) / 2), 40,
                                                 text, fade);
  }
  char rank_text[1088];
  bool has_rank = false;
  if (cgs->game_type < QA_Q3_BASE_GT_TEAM) {
    if (qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) != QA_Q3_BASE_TEAM_SPECTATOR) {
      char *place = qa_q3_pres_place_string(
          (double)qa_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_RANK) + 1));
      snprintf(rank_text, sizeof rank_text, "%s place with %lld", place ? place : "",
               (long long)qa_int32(qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_SCORE)));
      free(place);
      has_rank = true;
    }
  } else {
    double red = state->team_scores[0], blue = state->team_scores[1];
    if (red == blue) snprintf(rank_text, sizeof rank_text, "Teams are tied at %lld", (long long)qa_int32(red));
    else if (red >= blue)
      snprintf(rank_text, sizeof rank_text, "Red leads %lld to %lld", (long long)qa_int32(red),
               (long long)qa_int32(blue));
    else
      snprintf(rank_text, sizeof rank_text, "Blue leads %lld to %lld", (long long)qa_int32(blue),
               (long long)qa_int32(red));
    has_rank = true;
  }
  if (has_rank)
    qa_q3_pres_client_draw_tools_draw_big_string(
        tools, trunc((640 - qa_q3_pres_draw_strlen(rank_text) * 16) / 2), 60, rank_text, fade);
  if (board->host.graphics) {
    const qa_q3_pres_client_media_graphics *g = board->host.graphics;
    qa_rect score_pic = {176, HEADER_Y, 64, 32}, ping_pic = {264, HEADER_Y, 64, 32};
    qa_rect time_pic = {344, HEADER_Y, 64, 32}, name_pic = {416, HEADER_Y, 64, 32};
    qa_q3_pres_client_draw_tools_draw_pic(tools, score_pic, g->scoreboard_score);
    qa_q3_pres_client_draw_tools_draw_pic(tools, ping_pic, g->scoreboard_ping);
    qa_q3_pres_client_draw_tools_draw_pic(tools, time_pic, g->scoreboard_time);
    qa_q3_pres_client_draw_tools_draw_pic(tools, name_pic, g->scoreboard_name);
  }
  bool compact = state->num_scores > MAX_NORMAL;
  double line_height = compact ? INTER_HEIGHT : NORMAL_HEIGHT, top_border = compact ? 8 : 16;
  double max_clients = compact ? MAX_INTER : MAX_NORMAL, y = TOP_Y;
  board->local_client = false;
  if (cgs->game_type >= QA_Q3_BASE_GT_TEAM) {
    y += line_height / 2;
    qa_q3_base_team first_team = state->team_scores[0] >= state->team_scores[1] ? QA_Q3_BASE_TEAM_RED
                                                                                 : QA_Q3_BASE_TEAM_BLUE;
    double first = team_scoreboard(board, y, first_team, fade, max_clients, line_height);
    qa_rect bg1 = {0, y - top_border, 640, first * line_height + 16};
    qa_q3_pres_client_draw_icons_draw_team_background(board->host.icons, bg1, f32(0.33), first_team);
    y += first * line_height + 16;
    max_clients -= first;
    qa_q3_base_team second_team = first_team == QA_Q3_BASE_TEAM_RED ? QA_Q3_BASE_TEAM_BLUE : QA_Q3_BASE_TEAM_RED;
    double second = team_scoreboard(board, y, second_team, fade, max_clients, line_height);
    qa_rect bg2 = {0, y - top_border, 640, second * line_height + 16};
    qa_q3_pres_client_draw_icons_draw_team_background(board->host.icons, bg2, f32(0.33), second_team);
    y += second * line_height + 16;
    max_clients -= second;
    y += team_scoreboard(board, y, QA_Q3_BASE_TEAM_SPECTATOR, fade, max_clients, line_height) * line_height + 16;
  } else {
    double count = team_scoreboard(board, y, QA_Q3_BASE_TEAM_FREE, fade, max_clients, line_height);
    y += count * line_height + 16;
    y += team_scoreboard(board, y, QA_Q3_BASE_TEAM_SPECTATOR, fade, max_clients - count, line_height) *
             line_height + 16;
  }
  if (!board->local_client) {
    for (double index = 0; index < state->num_scores; index++) {
      qa_q3_pres_client_score *score = score_at(board, index);
      if (score->client == ps->client_num) {
        draw_client_score(board, y, score, color, fade, line_height == NORMAL_HEIGHT);
        break;
      }
    }
  }
  state->deferred_player_loading = (double)qa_int32(state->deferred_player_loading + 1);
  if (state->deferred_player_loading > 10)
    (void)qa_q3_pres_client_info_store_load_deferred_players(board->host.clients, reset_via_presenter,
                                                             board->host.players);
  return true;
}
static void center_giant_line(qa_q3_pres_base_scoreboard *board, double y, const char *text) {
  qa_q3_fixed_text_options options = {trunc(0.5 * (640 - 32 * qa_q3_pres_draw_strlen(text))), y, text,
                                      {1, 1, 1, 1}, 32, 48, 0, true, true};
  qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(board), options);
}
bool qa_q3_pres_base_scoreboard_draw_tourney(qa_q3_pres_base_scoreboard *board) {
  qa_q3_pres_client_game_state *state = board->state;
  qa_q3_pres_client_game_static_state *cgs = board->static_state;
  if ((double)qa_int32(state->scores_request_time + 2000) < state->time) {
    state->scores_request_time = state->time;
    board->host.send_client_command(board->host.ctx, "score");
  }
  qa_vec4 black = {0, 0, 0, 1};
  qa_rect full = {0, 0, 640, 480};
  qa_q3_pres_client_draw_tools_fill_rect(tools_of(board), full, &black);
  const char *motd = board->host.config_string(board->host.ctx, 4);
  center_giant_line(board, 8, motd == NULL || motd[0] == '\0' ? "Scoreboard" : motd);
  double seconds = trunc(state->time / 1000);
  double minutes = trunc(seconds / 60);
  seconds = fmod(seconds, 60);
  char clock_text[64];
  snprintf(clock_text, sizeof clock_text, "%lld:%lld%lld", (long long)minutes,
           (long long)trunc(seconds / 10), (long long)fmod(seconds, 10));
  center_giant_line(board, 64, clock_text);
  if (cgs->game_type >= QA_Q3_BASE_GT_TEAM) {
    const char *names[2] = {"Red Team", "Blue Team"};
    for (int i = 0; i < 2; i++) {
      double y = i == 0 ? 160 : 224;
      qa_q3_fixed_text_options name_options = {8, y, names[i], black, 32, 48, 0, true, true};
      qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(board), name_options);
      char score_text[32];
      snprintf(score_text, sizeof score_text, "%lld", (long long)qa_int32(state->team_scores[i]));
      qa_q3_fixed_text_options score_options = {632 - 32 * (double)strlen(score_text), y, score_text, black,
                                                32, 48, 0, true, true};
      qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(board), score_options);
    }
  } else {
    double y = 160;
    for (double index = 0; index < 64; index++) {
      qa_q3_pres_client_info *ci = client_at(board, index);
      if (!ci->info_valid || ci->team != QA_Q3_BASE_TEAM_FREE) continue;
      qa_q3_fixed_text_options name_options = {8, y, ci->name ? ci->name : "", black, 32, 48, 0, true, true};
      qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(board), name_options);
      char score_text[32];
      snprintf(score_text, sizeof score_text, "%lld", (long long)qa_int32(ci->score));
      qa_q3_fixed_text_options score_options = {632 - 32 * (double)strlen(score_text), y, score_text, black,
                                                32, 48, 0, true, true};
      qa_q3_pres_client_draw_tools_draw_string_ext(tools_of(board), score_options);
      y += 64;
    }
  }
  return true;
}
