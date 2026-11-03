/* Port of src/app/bootstrap/q3-rankings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_rankings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/network/services/rank_codec.h"
typedef struct weapon_track { bool present; double weapon, since; } weapon_track;
struct qa_app_q3_rankings {
  qa_q3_entity_pool *pool;
  const qa_q3_game_level *level;
  qa_app_q3_ranking_host host;
  qa_ranking_lifecycle *lifecycle;
  qa_event_ring reports;
  qa_event_cursor cursor;
  qa_ranking_player_state *observed; bool *observed_present;
  weapon_track *weapons;
  size_t slots;
  bool closed;
  char *game_key; /* TS gameKey, "" until begin */
  bool ended;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void status_changed(void *ctx, double slot, qa_ranking_player_state state) {
  qa_app_q3_rankings *rankings = ctx;
  rankings->host.status(rankings->host.ctx, slot, state);
}
static void ignore_print(void *ctx, const char *text) { (void)ctx; (void)text; }
/* SV_RankBegin passes the encoded game ID and key to clients (sv_rankings.c CS_GRANK); the GPL
   release has no CS_GRANK slot, so they ride in serverinfo. */
static void publish_game(qa_app_q3_rankings *rankings, qa_ranking_service_state state) {
  char id[13] = "";
  bool active = is(state.kind, "active");
  if (active) {
    uint8_t text[12] = {0};
    qa_rank_encode_game_id(state.game_id, text, sizeof text, ignore_print, NULL);
    /* text.subarray(0, text.indexOf(0)): no NUL is indexOf -1, which keeps the first 11 bytes. */
    size_t end = 11;
    for (size_t i = 0; i < sizeof text; i++) if (text[i] == 0) { end = i; break; }
    memcpy(id, text, end);
    id[end] = '\0';
  }
  rankings->host.set_cvar(rankings->host.ctx, "sv_rankingsGameKey",
      active && rankings->game_key != NULL ? rankings->game_key : "");
  rankings->host.set_cvar(rankings->host.ctx, "sv_rankingsGameID", id);
}
static void service_changed(void *ctx, qa_ranking_service_state state) {
  qa_app_q3_rankings *rankings = ctx;
  publish_game(rankings, state);
  if (rankings->host.service_status != NULL) rankings->host.service_status(rankings->host.ctx, state);
}
static void report_sink(void *ctx, qa_q3_ranking_report report) {
  qa_app_q3_rankings *rankings = ctx;
  if (!is(qa_ranking_lifecycle_get_state(rankings->lifecycle).kind, "active") || rankings->ended) return;
  if (qa_event_ring_append(&rankings->reports, &report) == 0) {
    qa_event_ring_reset(&rankings->reports);
    qa_ranking_lifecycle_unavailable(rankings->lifecycle,
        "Ranking provider cannot keep up with source reports; this match cannot be submitted completely.");
  }
}
static bool warmup_of(void *ctx) {
  qa_app_q3_rankings *rankings = ctx;
  return rankings->level->base.warmup_time != 0;
}
qa_app_q3_rankings *qa_app_q3_rankings_create(qa_q3_entity_pool *pool, const qa_q3_game_level *level,
    const qa_ranking_provider *provider, qa_app_q3_ranking_host host) {
  qa_app_q3_rankings *rankings = calloc(1, sizeof *rankings);
  if (rankings == NULL) qa_range_error("Out of memory");
  rankings->pool = pool;
  rankings->level = level;
  rankings->host = host;
  rankings->lifecycle = host.create_lifecycle(host.ctx, provider, status_changed, rankings,
      service_changed, rankings);
  if (!qa_event_ring_init(&rankings->reports, 65536, sizeof(qa_q3_ranking_report)))
    qa_range_error("Out of memory");
  if (!qa_event_ring_open_cursor(&rankings->reports, &rankings->cursor, false))
    qa_range_error("Out of memory");
  rankings->slots = QA_Q3_MAX_CLIENTS;
  rankings->observed = calloc(rankings->slots, sizeof *rankings->observed);
  rankings->observed_present = calloc(rankings->slots, sizeof *rankings->observed_present);
  rankings->weapons = calloc(rankings->slots, sizeof *rankings->weapons);
  if (rankings->observed == NULL || rankings->observed_present == NULL || rankings->weapons == NULL)
    qa_range_error("Out of memory");
  qa_q3_ranking_reports_attach(&pool->rankings, rankings, report_sink, warmup_of);
  return rankings;
}
qa_ranking_lifecycle *qa_app_q3_rankings_lifecycle(qa_app_q3_rankings *rankings) { return rankings->lifecycle; }
static void assert_open(qa_app_q3_rankings *rankings) {
  if (rankings->closed) qa_range_error("Ranking match owner is closed");
}
static void flush_reports(qa_app_q3_rankings *rankings) {
  size_t count = 0;
  qa_q3_ranking_report *reports = qa_event_ring_take(&rankings->reports, &rankings->cursor, &count);
  qa_event_ring_retire(&rankings->reports);
  for (size_t i = 0; i < count; i++) {
    if (!is(qa_ranking_lifecycle_get_state(rankings->lifecycle).kind, "active")) break;
    if (reports[i].kind == QA_Q3_RANK_INTEGER)
      qa_ranking_lifecycle_report_int(rankings->lifecycle, reports[i].self, reports[i].other,
          reports[i].key, reports[i].value_number, reports[i].accumulate);
    else
      qa_ranking_lifecycle_report_string(rankings->lifecycle, reports[i].self, reports[i].other,
          reports[i].key, reports[i].value_string);
  }
  free(reports);
}
void qa_app_q3_rankings_begin(qa_app_q3_rankings *rankings, bool enabled, bool single_player, const char *game_key) {
  assert_open(rankings);
  char *key = strdup(game_key != NULL ? game_key : "");
  if (key == NULL) qa_range_error("Out of memory");
  free(rankings->game_key);
  rankings->game_key = key;
  qa_ranking_lifecycle_begin(rankings->lifecycle, enabled, single_player, game_key);
  qa_app_q3_rankings_frame(rankings);
}
static void assert_client(qa_app_q3_rankings *rankings, double slot) {
  assert_open(rankings);
  bool ok = slot == trunc(slot) && slot >= 0 && slot < rankings->pool->max_clients
      && rankings->host.entity_inuse(rankings->host.ctx, slot)
      && rankings->pool->clients[(size_t)slot] != NULL;
  if (!ok) qa_range_error("Ranking account requires a connected source client");
}
void qa_app_q3_rankings_account(qa_app_q3_rankings *rankings, double slot, const qa_ranking_account_request *request) {
  assert_client(rankings, slot);
  qa_ranking_lifecycle_account(rankings->lifecycle, slot, request);
}
typedef struct actions_ctx { qa_app_q3_rankings *rankings; double slot; } actions_ctx;
static actions_ctx slot_actions[64];
static qa_ranking_service_state actions_service(void *ctx) {
  return qa_ranking_lifecycle_get_state(((actions_ctx *)ctx)->rankings->lifecycle);
}
static qa_ranking_player_state actions_player(void *ctx) {
  actions_ctx *actions = ctx;
  return qa_ranking_lifecycle_player(actions->rankings->lifecycle, actions->slot);
}
static const char *actions_submit(void *ctx, qa_ranking_account_request request) {
  actions_ctx *actions = ctx;
  qa_app_q3_rankings_account(actions->rankings, actions->slot, &request);
  return NULL;
}
static const char *actions_reset(void *ctx) {
  actions_ctx *actions = ctx;
  qa_app_q3_rankings_reset(actions->rankings, actions->slot);
  return NULL;
}
static const char *actions_spectate(void *ctx) {
  actions_ctx *actions = ctx;
  qa_app_q3_rankings_spectate(actions->rankings, actions->slot);
  return NULL;
}
qa_ui_ranking_account_actions qa_app_q3_rankings_account_actions(qa_app_q3_rankings *rankings, double slot) {
  assert_client(rankings, slot);
  actions_ctx *ctx = &slot_actions[(size_t)slot % 64];
  ctx->rankings = rankings; ctx->slot = slot;
  qa_ui_ranking_account_actions actions = {ctx, actions_service, actions_player,
      actions_submit, actions_reset, actions_spectate};
  return actions;
}
void qa_app_q3_rankings_reset(qa_app_q3_rankings *rankings, double slot) {
  assert_client(rankings, slot);
  qa_ranking_lifecycle_reset(rankings->lifecycle, slot);
}
void qa_app_q3_rankings_spectate(qa_app_q3_rankings *rankings, double slot) {
  assert_client(rankings, slot);
  flush_reports(rankings);
  qa_ranking_lifecycle_spectate(rankings->lifecycle, slot);
}
void qa_app_q3_rankings_disconnect(qa_app_q3_rankings *rankings, double slot) {
  assert_open(rankings);
  flush_reports(rankings);
  qa_ranking_lifecycle_disconnect(rankings->lifecycle, slot);
  if (slot >= 0 && (size_t)slot < rankings->slots) {
    rankings->observed_present[(size_t)slot] = false;
    rankings->weapons[(size_t)slot].present = false;
  }
}
void qa_app_q3_rankings_frame(qa_app_q3_rankings *rankings) {
  assert_open(rankings);
  flush_reports(rankings);
  qa_ranking_lifecycle_frame(rankings->lifecycle);
  if (!is(qa_ranking_lifecycle_get_state(rankings->lifecycle).kind, "active") || rankings->ended) return;
  double max_clients = rankings->pool->max_clients;
  for (double slot = 0; slot < max_clients; slot++) {
    size_t index = (size_t)slot;
    if (!rankings->host.entity_inuse(rankings->host.ctx, slot)
        || rankings->pool->clients[index] == NULL) continue;
    if (((qa_js_to_int32(rankings->host.entity_sv_flags(rankings->host.ctx, slot))) & QA_Q3_BASE_SVF_BOT) != 0) {
      rankings->host.drop_bot(rankings->host.ctx, slot);
      continue;
    }
    weapon_track *weapon = &rankings->weapons[index];
    double current_weapon = rankings->host.entity_weapon(rankings->host.ctx, slot);
    if (!weapon->present) {
      weapon->present = true; weapon->weapon = current_weapon; weapon->since = rankings->level->base.time;
    } else if (weapon->weapon != current_weapon) {
      qa_q3_ranking_reports_weapon_time(&rankings->pool->rankings, slot, weapon->weapon,
          trunc((rankings->level->base.time - weapon->since) / 1000));
      weapon->weapon = current_weapon; weapon->since = rankings->level->base.time;
    }
    qa_ranking_player_state state = qa_ranking_lifecycle_player(rankings->lifecycle, slot);
    bool has_previous = rankings->observed_present[index];
    qa_ranking_player_state previous = rankings->observed[index];
    rankings->observed[index] = state;
    rankings->observed_present[index] = true;
    if (!has_previous || !is(previous.kind, state.kind)) rankings->host.status(rankings->host.ctx, slot, state);
    double team = rankings->host.entity_session_team(rankings->host.ctx, slot);
    if (is(state.kind, "new") || is(state.kind, "spectator")) {
      if (team != QA_Q3_BASE_TEAM_SPECTATOR) {
        rankings->host.spectator(rankings->host.ctx, slot);
        rankings->host.menu(rankings->host.ctx, slot);
      }
    } else if (is(state.kind, "denied")) {
      qa_ranking_lifecycle_reset(rankings->lifecycle, slot);
    } else if (is(state.kind, "active")) {
      if (team == QA_Q3_BASE_TEAM_SPECTATOR && rankings->host.game_type(rankings->host.ctx) < QA_Q3_BASE_GT_TEAM)
        rankings->host.activate(rankings->host.ctx, slot);
      if (!has_previous || !is(previous.kind, "active")) {
        for (double other = 0; other < max_clients; other++) {
          size_t other_index = (size_t)other;
          if (!rankings->host.entity_inuse(rankings->host.ctx, other)
              || rankings->pool->clients[other_index] == NULL
              || ((qa_js_to_int32(rankings->host.entity_sv_flags(rankings->host.ctx, other))) & QA_Q3_BASE_SVF_BOT) != 0)
            continue;
          if (other != slot && is(qa_ranking_lifecycle_player(rankings->lifecycle, other).kind, "active"))
            qa_ranking_lifecycle_report_int(rankings->lifecycle, slot, other, 1210000002, 1, false);
          rankings->host.scoreboard(rankings->host.ctx, other);
        }
      }
    }
  }
  double fraglimit = qa_js_string_to_number(rankings->host.cvar(rankings->host.ctx, "fraglimit"));
  double timelimit = qa_js_string_to_number(rankings->host.cvar(rankings->host.ctx, "timelimit"));
  if ((fraglimit == 0 || fraglimit > 100) && (timelimit == 0 || timelimit > 1000))
    rankings->host.set_cvar(rankings->host.ctx, "timelimit", "1000");
}
void qa_app_q3_rankings_game_over(qa_app_q3_rankings *rankings) {
  assert_open(rankings);
  if (rankings->ended || rankings->level->base.warmup_time != 0) return;
  flush_reports(rankings);
  rankings->ended = true;
  static const struct { const char *name; double key; } strings[] = {
      {"sv_hostname", 1000010000}, {"mapname", 1000010001}, {"fs_game", 1000010002}, {"version", 1000010011}};
  static const struct { const char *name; double key; } integers[] = {
      {"g_gametype", 1010010003}, {"fraglimit", 1010010004}, {"timelimit", 1010010005},
      {"sv_maxclients", 1010010006}, {"sv_maxRate", 1010010007}, {"sv_minPing", 1010010008},
      {"sv_maxPing", 1010010009}, {"dedicated", 1010010010}};
  for (size_t i = 0; i < sizeof strings / sizeof strings[0]; i++)
    qa_ranking_lifecycle_report_string(rankings->lifecycle, -1, -1, strings[i].key,
        rankings->host.cvar(rankings->host.ctx, strings[i].name));
  for (size_t i = 0; i < sizeof integers / sizeof integers[0]; i++) {
    double value = trunc(qa_js_string_to_number(rankings->host.cvar(rankings->host.ctx, integers[i].name)));
    if (value == 0 || value != value) value = 0; /* TS `|| 0` also maps NaN */
    qa_ranking_lifecycle_report_int(rankings->lifecycle, -1, -1, integers[i].key, value, false);
  }
}
void qa_app_q3_rankings_close(qa_app_q3_rankings *rankings) {
  if (rankings == NULL || rankings->closed) return;
  rankings->closed = true;
  qa_q3_ranking_reports_detach(&rankings->pool->rankings);
  flush_reports(rankings);
  qa_ranking_lifecycle_end(rankings->lifecycle);
  rankings->host.destroy_lifecycle(rankings->host.ctx, rankings->lifecycle);
  qa_event_ring_close_cursor(&rankings->reports, &rankings->cursor);
  qa_event_ring_destroy(&rankings->reports);
  free(rankings->observed); free(rankings->observed_present); free(rankings->weapons); free(rankings->game_key);
  free(rankings);
}
