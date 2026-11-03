/* Port of src/content/q3/team-arena/match.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/match.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/team_arena/session.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

static double s32(double v) { return qa_js_to_int32(v); }
static double imul(double a, double b) { return (int32_t)(qa_js_to_int32(a) * qa_js_to_int32(b)); }
static double slot_get(qa_q3_base_player_state_slots *s, double i) { return qa_q3_base_player_state_slots_get(s, (size_t)i); }
static void slot_set(qa_q3_base_player_state_slots *s, double i, double v) { qa_q3_base_player_state_slots_set(s, (size_t)i, v); }
static char *dup_or_empty(const char *v) {
  if (!v) v = "";
  size_t n = strlen(v) + 1;
  char *out = malloc(n);
  if (out) memcpy(out, v, n);
  return out;
}

void qa_q3_ta_vote_state_init(qa_q3_ta_vote_state *s) {
  memset(s, 0, sizeof *s); s->string = dup_or_empty(""); s->display_string = dup_or_empty("");
}
void qa_q3_ta_vote_state_destroy(qa_q3_ta_vote_state *s) { free(s->string); free(s->display_string); s->string = s->display_string = NULL; }
void qa_q3_ta_team_vote_state_init(qa_q3_ta_team_vote_state *s) { memset(s, 0, sizeof *s); s->string = dup_or_empty(""); }
void qa_q3_ta_team_vote_state_destroy(qa_q3_ta_team_vote_state *s) { free(s->string); s->string = NULL; }
void qa_q3_ta_match_state_init(qa_q3_ta_match_state *s) {
  memset(s, 0, sizeof *s);
  s->intermission_origin = qa_math_vec3(0, 0, 0);
  s->intermission_angle = qa_math_vec3(0, 0, 0);
  qa_q3_ta_vote_state_init(&s->vote);
  qa_q3_ta_team_vote_state_init(&s->team_votes[0]);
  qa_q3_ta_team_vote_state_init(&s->team_votes[1]);
}
void qa_q3_ta_match_state_destroy(qa_q3_ta_match_state *s) {
  free(s->changemap); s->changemap = NULL;
  qa_q3_ta_vote_state_destroy(&s->vote);
  qa_q3_ta_team_vote_state_destroy(&s->team_votes[0]);
  qa_q3_ta_team_vote_state_destroy(&s->team_votes[1]);
}
void qa_q3_ta_match_module_state_init(qa_q3_ta_match_module_state *s) { s->password_modification_count = -1; }

static double at_value(const double *values, size_t count, double index) {
  if (index != trunc(index) || index < 0 || index >= (double)count) qa_range_error("Match rank index has no source storage");
  return values[(size_t)index];
}

/* TS sortRankPrefix (bg_lib qsort translation). */
typedef struct rank_sort { qa_q3_ta_match_runtime *runtime; double *values; } rank_sort;
static double rs_at(rank_sort *s, double i) { return at_value(s->values, QA_Q3_MAX_CLIENTS, i); }
static double rs_cmp(rank_sort *s, double a, double b) {
  return qa_q3_ta_match_runtime_sort_ranks(s->runtime, rs_at(s, a), rs_at(s, b));
}
static void rs_swap(rank_sort *s, double a, double b) {
  double v = rs_at(s, a); s->values[(size_t)a] = rs_at(s, b); s->values[(size_t)b] = v;
}
static void rs_swap_range(rank_sort *s, double a, double b, double length) {
  for (double i = 0; i < length; i++) rs_swap(s, a + i, b + i);
}
static double rs_median(rank_sort *s, double a, double b, double c) {
  return rs_cmp(s, a, b) < 0 ? (rs_cmp(s, b, c) < 0 ? b : rs_cmp(s, a, c) < 0 ? c : a)
                             : (rs_cmp(s, b, c) > 0 ? b : rs_cmp(s, a, c) < 0 ? a : c);
}
static void rs_insertion(rank_sort *s, double start, double length) {
  for (double mid = start + 1; mid < start + length; mid++)
    for (double left = mid; left > start && rs_cmp(s, left - 1, left) > 0; left--) rs_swap(s, left, left - 1);
}
static void rs_sort(rank_sort *s, double start, double length) {
  for (;;) {
    if (length < 7) { rs_insertion(s, start, length); return; }
    double mid = start + trunc(length / 2);
    if (length > 7) {
      double left = start, end = start + length - 1;
      if (length > 40) {
        double distance = trunc(length / 8);
        left = rs_median(s, left, left + distance, left + 2 * distance);
        mid = rs_median(s, mid - distance, mid, mid + distance);
        end = rs_median(s, end - 2 * distance, end - distance, end);
      }
      mid = rs_median(s, left, mid, end);
    }
    rs_swap(s, start, mid);
    double a = start + 1, b = a, c = start + length - 1, d = c;
    bool swapped = false;
    for (;;) {
      while (b <= c) { double r = rs_cmp(s, b, start); if (r > 0) break;
        if (r == 0) { swapped = true; rs_swap(s, a, b); a++; } b++; }
      while (b <= c) { double r = rs_cmp(s, c, start); if (r < 0) break;
        if (r == 0) { swapped = true; rs_swap(s, c, d); d--; } c--; }
      if (b > c) break;
      rs_swap(s, b, c); swapped = true; b++; c--;
    }
    if (!swapped) { rs_insertion(s, start, length); return; }
    double end = start + length;
    double range = qa_js_min(a - start, b - a); rs_swap_range(s, start, b - range, range);
    range = qa_js_min(d - c, end - d - 1); rs_swap_range(s, b, end - range, range);
    if (b - a > 1) rs_sort(s, start, b - a);
    range = d - c;
    if (range <= 1) return;
    start = end - range; length = range;
  }
}

void qa_q3_ta_match_runtime_init(qa_q3_ta_match_runtime *runtime, qa_q3_ta_match_host host,
    qa_q3_ta_match_module_state *module_state) {
  if ((qa_q3_base_product)host.pool->options.product != host.product)
    qa_range_error("Match product differs from its entity pool");
  if (host.team_scores->length != 4) qa_range_error("Match needs the shared four-team score storage");
  runtime->host = host;
  qa_q3_ta_match_module_state_init(&runtime->owned_module_state);
  runtime->module_state = module_state ? module_state : &runtime->owned_module_state;
}
qa_q3_ta_match_save_state qa_q3_ta_match_runtime_capture_save_state(qa_q3_ta_match_runtime *r) {
  qa_q3_ta_match_save_state s = {r->module_state->password_modification_count}; return s;
}
void qa_q3_ta_match_runtime_restore_save_state(qa_q3_ta_match_runtime *r, const qa_save_value *value) {
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "q3.matchModule")) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, "passwordModificationCount", &field)) qa_range_error("out of memory");
  r->module_state->password_modification_count = qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}
static qa_q3_ta_match_state *level(qa_q3_ta_match_runtime *r) { return r->host.state; }
static qa_q3_game_client *owned_client(qa_q3_ta_match_runtime *r, qa_q3_game_entity *entity) {
  if (qa_q3_entity_pool_get(r->host.pool, entity->slot) != entity || entity->client == NULL)
    qa_range_error("Match requires an owned client entity");
  return entity->client;
}
static double sorted_at(qa_q3_ta_match_runtime *r, double index) {
  return at_value(level(r)->sorted_clients, QA_Q3_MAX_CLIENTS, index);
}
static double score_of(qa_q3_ta_match_runtime *r, double index) {
  return slot_get(&qa_q3_entity_pool_client_at(r->host.pool, index)->ps.persistant, QA_Q3_BASE_PERS_SCORE);
}
static char *fmt(const char *format, const qa_q3_game_format_arg *a, size_t n, double cap) {
  return qa_q3_game_format(format, a, n, cap);
}
static void config_fmt(qa_q3_ta_match_runtime *r, double index, double value) {
  qa_q3_game_format_arg a = qa_q3_game_format_number(value);
  char *t = fmt("%i", &a, 1, 32000);
  r->host.set_configstring(r->host.ctx, index, t);
  free(t);
}

void qa_q3_ta_match_runtime_add_tournament_player(qa_q3_ta_match_runtime *r) {
  if (level(r)->num_playing_clients >= 2 || level(r)->intermission_time != 0) return;
  bool has_selected = false;
  double selected = 0;
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->pers.connected != QA_Q3_CONN_CONNECTED || client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR ||
        client->sess.spectator_state == QA_Q3_SPEC_SCOREBOARD || client->sess.spectator_client < 0) continue;
    if (!has_selected || client->sess.spectator_time < qa_q3_entity_pool_client_at(r->host.pool, selected)->sess.spectator_time) {
      selected = index; has_selected = true; }
  }
  if (!has_selected) return;
  level(r)->warmup_time = -1;
  r->host.set_team(r->host.ctx, qa_q3_entity_pool_at(r->host.pool, selected), "f");
}
static void remove_tournament_player(qa_q3_ta_match_runtime *r, double rank) {
  if (level(r)->num_playing_clients != 2) return;
  double number = sorted_at(r, rank);
  if (qa_q3_entity_pool_client_at(r->host.pool, number)->pers.connected == QA_Q3_CONN_CONNECTED)
    r->host.set_team(r->host.ctx, qa_q3_entity_pool_at(r->host.pool, number), "s");
}
void qa_q3_ta_match_runtime_remove_tournament_loser(qa_q3_ta_match_runtime *r) { remove_tournament_player(r, 1); }
void qa_q3_ta_match_runtime_remove_tournament_winner(qa_q3_ta_match_runtime *r) { remove_tournament_player(r, 0); }
void qa_q3_ta_match_runtime_adjust_tournament_scores(qa_q3_ta_match_runtime *r) {
  double winner = sorted_at(r, 0);
  qa_q3_game_client *first = qa_q3_entity_pool_client_at(r->host.pool, winner);
  if (first->pers.connected == QA_Q3_CONN_CONNECTED) {
    first->sess.wins = s32(first->sess.wins + 1); r->host.client_userinfo_changed(r->host.ctx, winner); }
  double loser = sorted_at(r, 1);
  qa_q3_game_client *second = qa_q3_entity_pool_client_at(r->host.pool, loser);
  if (second->pers.connected == QA_Q3_CONN_CONNECTED) {
    second->sess.losses = s32(second->sess.losses + 1); r->host.client_userinfo_changed(r->host.ctx, loser); }
}
double qa_q3_ta_match_runtime_sort_ranks(qa_q3_ta_match_runtime *r, double first, double second) {
  double left;
  double right;
  qa_q3_game_client *a = qa_q3_entity_pool_client_at(r->host.pool, first);
  qa_q3_game_client *b = qa_q3_entity_pool_client_at(r->host.pool, second);
  if (a->sess.spectator_state == QA_Q3_SPEC_SCOREBOARD || a->sess.spectator_client < 0) return 1;
  if (b->sess.spectator_state == QA_Q3_SPEC_SCOREBOARD || b->sess.spectator_client < 0) return -1;
  if (a->pers.connected == QA_Q3_CONN_CONNECTING) return 1;
  if (b->pers.connected == QA_Q3_CONN_CONNECTING) return -1;
  if (a->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR && b->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR)
    return a->sess.spectator_time < b->sess.spectator_time ? -1 : a->sess.spectator_time > b->sess.spectator_time ? 1 : 0;
  if (a->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) return 1;
  if (b->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) return -1;
  double left2 = score_of(r, first);
  double right2 = score_of(r, second);
  return left2 > right2 ? -1 : (left = score_of(r, first), right = score_of(r, second), left < right) ? 1 : 0;
}

void qa_q3_ta_match_runtime_calculate_ranks(qa_q3_ta_match_runtime *r) {
  qa_q3_ta_match_state *lv = level(r);
  qa_q3_entity_pool *pool = r->host.pool;
  double game_type = r->host.settings(r->host.ctx).game_type;
  lv->follow1 = lv->follow2 = -1;
  lv->num_connected_clients = lv->num_non_spectator_clients = lv->num_playing_clients = lv->num_voting_clients = 0;
  lv->num_team_voting_clients[0] = lv->num_team_voting_clients[1] = 0;
  for (double index = 0; index < pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(pool, index);
    if (client->pers.connected == QA_Q3_CONN_DISCONNECTED) continue;
    lv->sorted_clients[(size_t)lv->num_connected_clients++] = index;
    if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) continue;
    lv->num_non_spectator_clients++;
    if (client->pers.connected != QA_Q3_CONN_CONNECTED) continue;
    lv->num_playing_clients++;
    if ((qa_js_to_int32(qa_q3_entity_pool_at(pool, index)->r.sv_flags) & QA_Q3_BASE_SVF_BOT) == 0) {
      lv->num_voting_clients++;
      if (client->sess.session_team == QA_Q3_BASE_TEAM_RED) lv->num_team_voting_clients[0]++;
      else if (client->sess.session_team == QA_Q3_BASE_TEAM_BLUE) lv->num_team_voting_clients[1]++;
    }
    if (lv->follow1 == -1) lv->follow1 = index; else if (lv->follow2 == -1) lv->follow2 = index;
  }
  rank_sort sort = {r, lv->sorted_clients};
  rs_sort(&sort, 0, lv->num_connected_clients);
  if (game_type >= QA_Q3_BASE_GT_TEAM) {
    double red = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED);
    double blue = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE);
    for (double index = 0; index < lv->num_connected_clients; index++)
      slot_set(&qa_q3_entity_pool_client_at(pool, sorted_at(r, index))->ps.persistant,
          QA_Q3_BASE_PERS_RANK, red == blue ? 2 : red > blue ? 0 : 1);
  } else {
    double rank = -1, score = 0;
    for (double index = 0; index < lv->num_playing_clients; index++) {
      qa_q3_game_client *client = qa_q3_entity_pool_client_at(pool, sorted_at(r, index));
      double new_score = slot_get(&client->ps.persistant, QA_Q3_BASE_PERS_SCORE);
      if (index == 0 || new_score != score) { rank = index; slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_RANK, rank); }
      else {
        slot_set(&qa_q3_entity_pool_client_at(pool, sorted_at(r, index - 1))->ps.persistant,
            QA_Q3_BASE_PERS_RANK, (double)(qa_js_to_int32(rank) | 0x4000));
        slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_RANK, (double)(qa_js_to_int32(rank) | 0x4000));
      }
      score = new_score;
      if (game_type == QA_Q3_BASE_GT_SINGLE_PLAYER && lv->num_playing_clients == 1)
        slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_RANK, (double)(qa_js_to_int32(rank) | 0x4000));
    }
  }
  if (game_type >= QA_Q3_BASE_GT_TEAM) {
    config_fmt(r, 6, slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED));
    config_fmt(r, 7, slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE));
  } else {
    config_fmt(r, 6, lv->num_connected_clients == 0 ? -9999 : score_of(r, sorted_at(r, 0)));
    config_fmt(r, 7, lv->num_connected_clients < 2 ? -9999 : score_of(r, sorted_at(r, 1)));
  }
  qa_q3_ta_match_runtime_check_exit_rules(r);
  if (lv->intermission_time != 0) qa_q3_ta_match_runtime_send_scoreboard_to_all(r);
}
void qa_q3_ta_match_runtime_send_scoreboard_to_all(qa_q3_ta_match_runtime *r) {
  for (double index = 0; index < r->host.pool->max_clients; index++)
    if (qa_q3_entity_pool_client_at(r->host.pool, index)->pers.connected == QA_Q3_CONN_CONNECTED)
      r->host.send_scoreboard(r->host.ctx, qa_q3_entity_pool_at(r->host.pool, index));
}
void qa_q3_ta_match_runtime_move_client_to_intermission(qa_q3_ta_match_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = owned_client(r, entity);
  if (client->sess.spectator_state == QA_Q3_SPEC_FOLLOW) r->host.stop_following(r->host.ctx, entity);
  client = owned_client(r, entity);
  entity->s.origin = level(r)->intermission_origin;
  qa_q3_base_player_state_record_set_origin(&client->ps, level(r)->intermission_origin);
  client->ps.viewangles = level(r)->intermission_angle;
  client->ps.pm_type = QA_Q3_PM_INTERMISSION;
  for (size_t index = 0; index < client->ps.powerups.length; index++) slot_set(&client->ps.powerups, (double)index, 0);
  client->ps.e_flags = entity->s.e_flags = 0;
  entity->s.e_type = QA_Q3_BASE_ET_GENERAL;
  entity->s.modelindex = entity->s.loop_sound = entity->s.event = entity->r.contents = 0;
}
typedef struct pick_ctx { qa_q3_ta_match_runtime *runtime; } pick_ctx;
static double match_rand(void *ctx) { return qa_game_random_rand(((pick_ctx *)ctx)->runtime->host.random); }
static void match_warn(void *ctx, const char *text) {
  qa_q3_ta_match_runtime *r = ((pick_ctx *)ctx)->runtime; r->host.warn(r->host.ctx, text);
}
qa_q3_ta_spawn_pose qa_q3_ta_match_runtime_find_intermission_point(qa_q3_ta_match_runtime *r) {
  qa_q3_game_entity *entity = qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_CLASSNAME, "info_player_intermission");
  if (entity == NULL) {
    qa_q3_ta_spawn_pose selected = r->host.select_spawn_point(r->host.ctx, qa_math_vec3(0, 0, 0));
    level(r)->intermission_origin = selected.origin;
    level(r)->intermission_angle = selected.angles;
  } else {
    level(r)->intermission_origin = entity->s.origin;
    level(r)->intermission_angle = entity->s.angles;
    if (entity->target != NULL) {
      pick_ctx pctx = {r};
      qa_q3_target_selection_context sel = {r->host.pool, &pctx, match_rand, match_warn};
      qa_q3_game_entity *target = qa_q3_pick_target(sel, entity->target);
      if (target != NULL)
        level(r)->intermission_angle = qa_vector_to_angles(qa_sub3(target->s.origin, level(r)->intermission_origin));
    }
  }
  qa_q3_ta_spawn_pose out = {level(r)->intermission_origin, level(r)->intermission_angle};
  return out;
}
void qa_q3_ta_match_runtime_begin_intermission(qa_q3_ta_match_runtime *r) {
  if (level(r)->intermission_time != 0) return;
  if (r->host.settings(r->host.ctx).game_type == QA_Q3_BASE_GT_TOURNAMENT)
    qa_q3_ta_match_runtime_adjust_tournament_scores(r);
  level(r)->intermission_time = level(r)->time;
  (void)qa_q3_ta_match_runtime_find_intermission_point(r);
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    if (r->host.single_player(r->host.ctx)) {
      r->host.set_cvar(r->host.ctx, "ui_singlePlayerActive", "0");
      r->host.update_tournament_info(r->host.ctx);
    }
  } else if (r->host.settings(r->host.ctx).game_type == QA_Q3_BASE_GT_SINGLE_PLAYER) {
    r->host.update_tournament_info(r->host.ctx);
    r->host.spawn_models_on_victory_pads(r->host.ctx);
  }
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(r->host.pool, index);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    if (qa_q3_game_entity_health(entity) <= 0) r->host.respawn(r->host.ctx, entity);
    qa_q3_ta_match_runtime_move_client_to_intermission(r, entity);
  }
  qa_q3_ta_match_runtime_send_scoreboard_to_all(r);
}
void qa_q3_ta_match_runtime_exit_level(qa_q3_ta_match_runtime *r) {
  r->host.bot_interbreed_end_match(r->host.ctx);
  if (r->host.settings(r->host.ctx).game_type == QA_Q3_BASE_GT_TOURNAMENT) {
    if (!level(r)->restarted) {
      qa_q3_ta_match_runtime_remove_tournament_loser(r);
      r->host.append_console_command(r->host.ctx, "map_restart 0\n");
      level(r)->restarted = true;
      free(level(r)->changemap); level(r)->changemap = NULL;
      level(r)->intermission_time = 0;
    }
    return;
  }
  r->host.append_console_command(r->host.ctx, "vstr nextmap\n");
  free(level(r)->changemap); level(r)->changemap = NULL;
  level(r)->intermission_time = 0;
  slot_set(r->host.team_scores, QA_Q3_BASE_TEAM_RED, 0);
  slot_set(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE, 0);
  for (double i = 0; i < r->host.pool->max_clients; i++) {
    qa_q3_game_client *c = qa_q3_entity_pool_client_at(r->host.pool, i);
    if (c->pers.connected == QA_Q3_CONN_CONNECTED) slot_set(&c->ps.persistant, QA_Q3_BASE_PERS_SCORE, 0);
  }
  r->host.write_session_data(r->host.ctx);
  for (double i = 0; i < r->host.pool->max_clients; i++) {
    qa_q3_game_client *c = qa_q3_entity_pool_client_at(r->host.pool, i);
    if (c->pers.connected == QA_Q3_CONN_CONNECTED) c->pers.connected = QA_Q3_CONN_CONNECTING;
  }
}
void qa_q3_ta_match_runtime_log_exit(qa_q3_ta_match_runtime *r, const char *reason) {
  qa_q3_ta_match_settings settings = r->host.settings(r->host.ctx);
  bool won = true;
  qa_q3_game_format_arg a1 = qa_q3_game_format_string(reason);
  char *t = fmt("Exit: %s\n", &a1, 1, 1024);
  r->host.log(r->host.ctx, t); free(t);
  level(r)->intermission_queued = level(r)->time;
  r->host.set_configstring(r->host.ctx, 22, "1");
  if (settings.game_type >= QA_Q3_BASE_GT_TEAM) {
    qa_q3_game_format_arg value = qa_q3_game_format_number(slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED));
    qa_q3_game_format_arg value2 = qa_q3_game_format_number(slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE));
    qa_q3_game_format_arg a[] = {value, value2};
    t = fmt("red:%i  blue:%i\n", a, 2, 1024);
    r->host.log(r->host.ctx, t); free(t);
  }
  double limit = qa_js_min(level(r)->num_connected_clients, 32);
  for (double index = 0; index < limit; index++) {
    double number = sorted_at(r, index);
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, number);
    if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR || client->pers.connected == QA_Q3_CONN_CONNECTING) continue;
    qa_q3_game_format_arg a[] = {qa_q3_game_format_number(score_of(r, number)),
        qa_q3_game_format_number(qa_js_min(client->ps.ping, 999)),
        qa_q3_game_format_number(number), qa_q3_game_format_string(client->pers.netname)};
    t = fmt("score: %i  ping: %i  client: %i %s\n", a, 4, 1024);
    r->host.log(r->host.ctx, t); free(t);
    if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && r->host.single_player(r->host.ctx) &&
        settings.game_type == QA_Q3_BASE_GT_TOURNAMENT &&
        (qa_js_to_int32(qa_q3_entity_pool_at(r->host.pool, number)->r.sv_flags) & QA_Q3_BASE_SVF_BOT) != 0 &&
        slot_get(&client->ps.persistant, QA_Q3_BASE_PERS_RANK) == 0) won = false;
  }
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && r->host.single_player(r->host.ctx)) {
    double left;
    double right;
    if (settings.game_type >= QA_Q3_BASE_GT_CTF)
      won = (left = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED), right = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE), left > right);
    r->host.append_console_command(r->host.ctx, won ? "spWin\n" : "spLose\n");
  }
}

void qa_q3_ta_match_runtime_check_intermission_exit(qa_q3_ta_match_runtime *r) {
  if (r->host.settings(r->host.ctx).game_type == QA_Q3_BASE_GT_SINGLE_PLAYER) return;
  double ready = 0, not_ready = 0, mask = 0;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(r->host.product);
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->pers.connected != QA_Q3_CONN_CONNECTED ||
        (qa_js_to_int32(qa_q3_entity_pool_at(r->host.pool, client->ps.client_num)->r.sv_flags) & QA_Q3_BASE_SVF_BOT) != 0) continue;
    if (client->ready_to_exit) { ready++; if (index < 16) mask = (double)(qa_js_to_int32(mask) | (1 << qa_js_to_int32(index))); }
    else not_ready++;
  }
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->pers.connected == QA_Q3_CONN_CONNECTED) slot_set(&client->ps.stats, schema->clients_ready, mask);
  }
  if (level(r)->time < s32(level(r)->intermission_time + 5000)) return;
  if (ready == 0) { level(r)->ready_to_exit = false; return; }
  if (not_ready == 0) { qa_q3_ta_match_runtime_exit_level(r); return; }
  if (!level(r)->ready_to_exit) { level(r)->ready_to_exit = true; level(r)->exit_time = level(r)->time; }
  if (level(r)->time >= s32(level(r)->exit_time + 10000)) qa_q3_ta_match_runtime_exit_level(r);
}
bool qa_q3_ta_match_runtime_score_is_tied(qa_q3_ta_match_runtime *r) {
  double left2;
  double right2;
  double left;
  double right;
  if (level(r)->num_playing_clients < 2) return false;
  return r->host.settings(r->host.ctx).game_type >= QA_Q3_BASE_GT_TEAM
      ? (left2 = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_RED), right2 = slot_get(r->host.team_scores, QA_Q3_BASE_TEAM_BLUE), left2 == right2)
      : (left = score_of(r, sorted_at(r, 0)), right = score_of(r, sorted_at(r, 1)), left == right);
}
void qa_q3_ta_match_runtime_check_exit_rules(qa_q3_ta_match_runtime *r) {
  if (level(r)->intermission_time != 0) { qa_q3_ta_match_runtime_check_intermission_exit(r); return; }
  if (level(r)->intermission_queued != 0) {
    double delay = (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && r->host.single_player(r->host.ctx)) ? 5000 : 1000;
    if (s32(level(r)->time - level(r)->intermission_queued) >= delay) {
      level(r)->intermission_queued = 0; qa_q3_ta_match_runtime_begin_intermission(r); }
    return;
  }
  if (qa_q3_ta_match_runtime_score_is_tied(r)) return;
  qa_q3_ta_match_settings settings = r->host.settings(r->host.ctx);
  if (settings.time_limit != 0 && level(r)->warmup_time == 0 &&
      s32(level(r)->time - level(r)->start_time) >= imul(settings.time_limit, 60000)) {
    r->host.send_server_command(r->host.ctx, -1, "print \"Timelimit hit.\n\"");
    qa_q3_ta_match_runtime_log_exit(r, "Timelimit hit.");
    return;
  }
  if (level(r)->num_playing_clients < 2) return;
  static const struct { qa_q3_base_team team; const char *name; } teams[] = {
    {QA_Q3_BASE_TEAM_RED, "Red"}, {QA_Q3_BASE_TEAM_BLUE, "Blue"}};
  if (settings.game_type < QA_Q3_BASE_GT_CTF && settings.frag_limit != 0) {
    for (int i = 0; i < 2; i++) {
      if (slot_get(r->host.team_scores, teams[i].team) >= settings.frag_limit) {
        qa_q3_game_format_arg a = qa_q3_game_format_string(teams[i].name);
        char *t = fmt("print \"%s hit the fraglimit.\n\"", &a, 1, 32000);
        r->host.send_server_command(r->host.ctx, -1, t); free(t);
        qa_q3_ta_match_runtime_log_exit(r, "Fraglimit hit.");
        return;
      }
    }
    for (double index = 0; index < r->host.pool->max_clients; index++) {
      qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
      if (client->pers.connected != QA_Q3_CONN_CONNECTED || client->sess.session_team != QA_Q3_BASE_TEAM_FREE) continue;
      if (score_of(r, index) >= settings.frag_limit) {
        qa_q3_ta_match_runtime_log_exit(r, "Fraglimit hit.");
        qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
        char *t = fmt("print \"%s^7 hit the fraglimit.\n\"", &a, 1, 1024);
        r->host.send_server_command(r->host.ctx, -1, t); free(t);
        return;
      }
    }
  }
  if (settings.game_type >= QA_Q3_BASE_GT_CTF && settings.capture_limit != 0) {
    for (int i = 0; i < 2; i++) {
      if (slot_get(r->host.team_scores, teams[i].team) >= settings.capture_limit) {
        qa_q3_game_format_arg a = qa_q3_game_format_string(teams[i].name);
        char *t = fmt("print \"%s hit the capturelimit.\n\"", &a, 1, 32000);
        r->host.send_server_command(r->host.ctx, -1, t); free(t);
        qa_q3_ta_match_runtime_log_exit(r, "Capturelimit hit.");
        return;
      }
    }
  }
}
static void wait_for_players(qa_q3_ta_match_runtime *r) {
  if (level(r)->warmup_time == -1) return;
  level(r)->warmup_time = -1;
  r->host.set_configstring(r->host.ctx, 5, "-1");
  r->host.log(r->host.ctx, "Warmup:\n");
}
void qa_q3_ta_match_runtime_check_tournament(qa_q3_ta_match_runtime *r) {
  if (level(r)->num_playing_clients == 0) return;
  qa_q3_ta_match_settings settings = r->host.settings(r->host.ctx);
  if (settings.game_type == QA_Q3_BASE_GT_TOURNAMENT) {
    if (level(r)->num_playing_clients < 2) qa_q3_ta_match_runtime_add_tournament_player(r);
    if (level(r)->num_playing_clients != 2) { wait_for_players(r); return; }
    if (level(r)->warmup_time == 0) return;
  } else {
    if (settings.game_type == QA_Q3_BASE_GT_SINGLE_PLAYER || level(r)->warmup_time == 0) return;
    bool not_enough;
    if (settings.game_type > QA_Q3_BASE_GT_TEAM) {
      qa_q3_ta_session_world_state world;
      memset(&world, 0, sizeof world);
      world.clients = r->host.pool->clients;
      world.client_count = QA_Q3_MAX_CLIENTS;
      world.max_clients = r->host.pool->max_clients;
      not_enough = qa_q3_ta_team_count(&world, -1, QA_Q3_BASE_TEAM_RED) < 1 ||
                   qa_q3_ta_team_count(&world, -1, QA_Q3_BASE_TEAM_BLUE) < 1;
    } else not_enough = level(r)->num_playing_clients < 2;
    if (not_enough) { wait_for_players(r); return; }
  }
  settings = r->host.settings(r->host.ctx);
  if (settings.warmup_modification_count != level(r)->warmup_modification_count) {
    level(r)->warmup_modification_count = settings.warmup_modification_count;
    level(r)->warmup_time = -1;
  }
  if (level(r)->warmup_time < 0) {
    level(r)->warmup_time = s32(level(r)->time + imul(s32(settings.warmup_seconds - 1), 1000));
    config_fmt(r, 5, level(r)->warmup_time);
    return;
  }
  if (level(r)->time > level(r)->warmup_time) {
    level(r)->warmup_time = s32(level(r)->warmup_time + 10000);
    r->host.set_cvar(r->host.ctx, "g_restarted", "1");
    r->host.append_console_command(r->host.ctx, "map_restart 0\n");
    level(r)->restarted = true;
  }
}
void qa_q3_ta_match_runtime_check_vote(qa_q3_ta_match_runtime *r) {
  qa_q3_ta_vote_state *vote = &level(r)->vote;
  if (vote->execute_time != 0 && vote->execute_time < level(r)->time) {
    vote->execute_time = 0;
    size_t n = strlen(vote->string) + 2;
    char *cmd = malloc(n);
    if (!cmd) qa_range_error("out of memory");
    (void)snprintf(cmd, n, "%s\n", vote->string);
    r->host.append_console_command(r->host.ctx, cmd);
    free(cmd);
  }
  if (vote->time == 0) return;
  if (s32(level(r)->time - vote->time) >= 30000) r->host.send_server_command(r->host.ctx, -1, "print \"Vote failed.\n\"");
  else if (vote->yes > trunc(level(r)->num_voting_clients / 2)) {
    r->host.send_server_command(r->host.ctx, -1, "print \"Vote passed.\n\"");
    vote->execute_time = s32(level(r)->time + 3000);
  } else if (vote->no >= trunc(level(r)->num_voting_clients / 2))
    r->host.send_server_command(r->host.ctx, -1, "print \"Vote failed.\n\"");
  else return;
  vote->time = 0;
  r->host.set_configstring(r->host.ctx, 8, "");
}
void qa_q3_ta_match_runtime_print_team(qa_q3_ta_match_runtime *r, qa_q3_base_team team, const char *message) {
  for (double index = 0; index < r->host.pool->max_clients; index++)
    if (qa_q3_entity_pool_client_at(r->host.pool, index)->sess.session_team == team)
      r->host.send_server_command(r->host.ctx, index, message);
}
void qa_q3_ta_match_runtime_set_leader(qa_q3_ta_match_runtime *r, qa_q3_base_team team, double client_num) {
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, client_num);
  qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
  if (client->pers.connected == QA_Q3_CONN_DISCONNECTED) {
    char *t = fmt("print \"%s is not connected\n\"", &a, 1, 1024);
    qa_q3_ta_match_runtime_print_team(r, team, t); free(t); return;
  }
  if (client->sess.session_team != team) {
    char *t = fmt("print \"%s is not on the team anymore\n\"", &a, 1, 1024);
    qa_q3_ta_match_runtime_print_team(r, team, t); free(t); return;
  }
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *other = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (other->sess.session_team == team && other->sess.team_leader != 0) {
      other->sess.team_leader = 0; r->host.client_userinfo_changed(r->host.ctx, index); }
  }
  client->sess.team_leader = 1;
  r->host.client_userinfo_changed(r->host.ctx, client_num);
  char *t = fmt("print \"%s is the new team leader\n\"", &a, 1, 1024);
  qa_q3_ta_match_runtime_print_team(r, team, t); free(t);
}
void qa_q3_ta_match_runtime_check_team_leader(qa_q3_ta_match_runtime *r, qa_q3_base_team team) {
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->sess.session_team == team && client->sess.team_leader != 0) return;
  }
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->sess.session_team == team &&
        (qa_js_to_int32(qa_q3_entity_pool_at(r->host.pool, index)->r.sv_flags) & QA_Q3_BASE_SVF_BOT) == 0) {
      client->sess.team_leader = 1; break; }
  }
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (client->sess.session_team == team) { client->sess.team_leader = 1; break; }
  }
}
void qa_q3_ta_match_runtime_check_team_vote(qa_q3_ta_match_runtime *r, qa_q3_base_team team) {
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE) return;
  int offset = team == QA_Q3_BASE_TEAM_RED ? 0 : 1;
  qa_q3_ta_team_vote_state *vote = &level(r)->team_votes[offset];
  if (vote->time == 0) return;
  double majority = trunc(level(r)->num_team_voting_clients[offset] / 2);
  if (s32(level(r)->time - vote->time) >= 30000)
    r->host.send_server_command(r->host.ctx, -1, "print \"Team vote failed.\n\"");
  else if (vote->yes > majority) {
    r->host.send_server_command(r->host.ctx, -1, "print \"Team vote passed.\n\"");
    if (strncmp(vote->string, "leader", 6) == 0)
      qa_q3_ta_match_runtime_set_leader(r, team, qa_game_atoi(vote->string + (strlen(vote->string) >= 7 ? 7 : strlen(vote->string))));
    else {
      size_t n = strlen(vote->string) + 2;
      char *cmd = malloc(n);
      if (!cmd) qa_range_error("out of memory");
      (void)snprintf(cmd, n, "%s\n", vote->string);
      r->host.append_console_command(r->host.ctx, cmd);
      free(cmd);
    }
  } else if (vote->no >= majority)
    r->host.send_server_command(r->host.ctx, -1, "print \"Team vote failed.\n\"");
  else return;
  vote->time = 0;
  r->host.set_configstring(r->host.ctx, 12 + offset, "");
}
void qa_q3_ta_match_runtime_check_cvars(qa_q3_ta_match_runtime *r) {
  qa_q3_ta_match_settings settings = r->host.settings(r->host.ctx);
  if (settings.password_modification_count == r->module_state->password_modification_count) return;
  r->module_state->password_modification_count = settings.password_modification_count;
  const char *p = settings.password ? settings.password : "";
  bool is_none = true;
  const char *none = "none";
  size_t i;
  for (i = 0; p[i] && none[i]; i++) if (tolower((unsigned char)p[i]) != none[i]) { is_none = false; break; }
  if (is_none && (p[i] || none[i])) is_none = false;
  r->host.set_cvar(r->host.ctx, "g_needpass", (p[0] != '\0' && !is_none) ? "1" : "0");
}
