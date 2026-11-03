/* Port of src/app/bootstrap/match-results.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/match_results.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/llm/errors.h"
#include "qa/content/q2/base/player/index.h"
#include "simulation/shared_simulation_internal.h"

#define MAX_SLOTS 256
#define RESULT_CAPACITY 64

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = checked(malloc(length + 1));
  memcpy(copy, text, length + 1);
  return copy;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool starts_with(const char *text, const char *prefix) {
  return text != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}

void qa_app_match_result_free(qa_app_match_result *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->player_count; i++) free(result->players[i].identity);
  free(result->players); free(result->rules); free(result->match); free(result->map);
  memset(result, 0, sizeof *result);
}

/* TS ended(simulation, transition). */
static bool ended(qa_app_shared_simulation *simulation, const qa_app_native_q2_map_transition *transition) {
  if (!is(simulation->options.mode, "deathmatch")) return false;
  qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
  if (q3 != NULL) return qa_app_q3_source_runtime_intermission_time(q3) != 0;
  if (qa_app_shared_sim_q2_source(simulation) != NULL || qa_app_shared_sim_q2_native(simulation, NULL, NULL)) {
    qa_app_q2_match_simulation access = qa_app_shared_sim_q2_match_simulation(simulation);
    return qa_app_q2_match_completed(&access, transition);
  }
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(simulation, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, players[i]);
    if (player != NULL && player->intermission) return true;
  }
  return false;
}

/* String.prototype.trim over UTF-8: a malloc'd copy. */
static char *js_trim(const char *text) {
  const char *start = text;
  for (size_t width; (width = qa_js_white_space_length(start)) != 0;) start += width;
  const char *end = start;
  for (const char *at = start; *at != '\0';) {
    size_t width = qa_js_white_space_length(at);
    if (width != 0) at += width;
    else { at++; end = at; }
  }
  size_t length = (size_t)(end - start);
  char *copy = checked(malloc(length + 1));
  memcpy(copy, start, length);
  copy[length] = '\0';
  return copy;
}
/* The pieces of userinfo.split("\\"): malloc'd array of malloc'd strings. */
static char **split_backslash(const char *text, size_t *out_count) {
  size_t count = 1;
  for (const char *at = text; *at != '\0'; at++) if (*at == '\\') count++;
  char **parts = checked(calloc(count, sizeof *parts));
  size_t index = 0;
  const char *start = text;
  for (const char *at = text;; at++) {
    if (*at == '\\' || *at == '\0') {
      size_t length = (size_t)(at - start);
      parts[index] = checked(malloc(length + 1));
      memcpy(parts[index], start, length);
      parts[index][length] = '\0';
      index++;
      if (*at == '\0') break;
      start = at + 1;
    }
  }
  *out_count = count;
  return parts;
}
static const char *const identity_keys[] = { "social_id", "cl_guid", "name" };
char *qa_app_match_userinfo_identity(const char *userinfo) {
  if (userinfo == NULL) return NULL;
  size_t part_count = 0;
  char **parts = split_backslash(userinfo, &part_count);
  char *result = NULL;
  for (size_t k = 0; k < sizeof identity_keys / sizeof identity_keys[0] && result == NULL; k++) {
    /* values.set over the pairs: the last pair with the key wins. */
    const char *value = NULL;
    for (size_t index = 1; index + 1 < part_count; index += 2)
      if (strcmp(parts[index], identity_keys[k]) == 0) value = parts[index + 1];
    if (value == NULL) continue;
    char *trimmed = js_trim(value);
    if (trimmed[0] == '\0') { free(trimmed); continue; }
    if (strcmp(identity_keys[k], "name") == 0) result = trimmed;
    else {
      size_t length = strlen(identity_keys[k]) + 1 + strlen(trimmed) + 1;
      result = checked(malloc(length));
      (void)snprintf(result, length, "%s:%s", identity_keys[k], trimmed);
      free(trimmed);
    }
  }
  for (size_t i = 0; i < part_count; i++) free(parts[i]);
  free(parts);
  return result;
}

/* TS native.playerState(client.slot + 1) scored by the edition's rule. */
static bool native_score(qa_app_shared_simulation *simulation, bool classic_rule, double slot, double *out) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  (void)qa_app_shared_sim_q2_native(simulation, &classic, &rerelease);
  bool classic_world = is(simulation->source.edition, "classic");
  if (classic_rule) {
    if (classic_world) {
      qa_q2_player_state state = qa_app_sim_classic_guest_world_player_state(classic, slot + 1);
      return qa_app_classic_match_score(&state, out);
    }
    qa_q2_rerelease_player_state state = qa_app_sim_rerelease_guest_world_player_state(rerelease, slot + 1);
    /* classicMatchScore reads only stats, which the rerelease state's view carries too. */
    qa_q2_player_state view;
    memset(&view, 0, sizeof view);
    view.view.stats = state.view.stats;
    return qa_app_classic_match_score(&view, out);
  }
  if (classic_world) {
    qa_q2_player_state state = qa_app_sim_classic_guest_world_player_state(classic, slot + 1);
    qa_q2_rerelease_player_state view;
    memset(&view, 0, sizeof view);
    view.view.stats = state.view.stats;
    return qa_app_rerelease_match_score(&view, out);
  }
  qa_q2_rerelease_player_state state = qa_app_sim_rerelease_guest_world_player_state(rerelease, slot + 1);
  return qa_app_rerelease_match_score(&state, out);
}

bool qa_app_match_result_of(qa_app_shared_simulation *simulation, const char *match,
    const qa_app_native_q2_map_transition *transition, qa_app_match_result *out) {
  memset(out, 0, sizeof *out);
  if (!ended(simulation, transition)) return false;
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(simulation);
  bool native = qa_app_shared_sim_q2_native(simulation, NULL, NULL);
  size_t count = 0;
  const qa_actor_id *listed = qa_app_shared_sim_players(simulation, &count);
  /* players() storage is reused by later simulation calls: copy the list first. */
  qa_actor_id *players = checked(malloc((count == 0 ? 1 : count) * sizeof *players));
  if (count != 0) memcpy(players, listed, count * sizeof *players);
  qa_app_match_result_player *scored = checked(calloc(count == 0 ? 1 : count, sizeof *scored));
  size_t scored_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = players[i];
    qa_client_id client;
    bool has_client = qa_app_shared_sim_player_client(simulation, actor, &client);
    double score = 0;
    bool has_score;
    if (!native) {
      qa_source_match_player_result player = qa_app_shared_sim_primary_match_player(simulation, actor);
      has_score = player.has_value;
      if (has_score) score = player.value.score(player.value.ctx);
    } else if (!has_client) has_score = false;
    else has_score = native_score(simulation, is(simulation->source.edition, "classic") || transition != NULL, (double)client.slot, &score);
    if (!has_client || !has_score) continue;
    if (q2 != NULL) {
      qa_q2_base_player_state *state = qa_q2_players_get_state(q2->players, actor);
      if (state != NULL && state->spectator) continue;
    }
    qa_app_match_result_player *row = &scored[scored_count++];
    row->slot = (double)client.slot;
    row->score = score;
    row->identity = qa_app_sim_bot_services_is_bot(&simulation->bot_services, actor)
        ? NULL : qa_app_match_userinfo_identity(qa_app_shared_sim_source_player_userinfo(simulation, actor));
  }
  free(players);
  if (scored_count == 0) { free(scored); return false; }
  double best = -INFINITY;
  for (size_t i = 0; i < scored_count; i++) best = qa_js_max(best, scored[i].score);
  for (size_t i = 0; i < scored_count; i++) scored[i].won = scored[i].score == best;
  const char *map = simulation->recipe.map.geometry.requested_path;
  const char *content = simulation->recipe.map.entities.content;
  out->family = starts_with(content, "q3") ? "q3" : starts_with(content, "q2") ? "q2" : "q1";
  out->rules = dup_text(simulation->recipe.match.provider);
  size_t length = strlen(match) + 1 + strlen(map) + 1;
  out->match = checked(malloc(length));
  (void)snprintf(out->match, length, "%s:%s", match, map);
  out->map = dup_text(map);
  out->players = scored;
  out->player_count = scored_count;
  return true;
}

/* ---- ApplicationMatchRankings ---- */
typedef struct account_record { char *name; qa_online_account account; } account_record;
struct qa_app_match_rankings {
  qa_local_authorization *authorization;
  qa_local_ranking_service *ranking;
  qa_event_ring results; /* qa_app_match_result records; strings owned until settle takes them */
  qa_event_cursor cursor;
  const char *slots[MAX_SLOTS]; /* AccountId (borrowed from accounts) or NULL */
  account_record **accounts; size_t account_count, account_capacity; /* accounts and names maps */
  qa_app_player_progress_store *store;
  qa_app_shared_simulation *published;
  double dropped;
};

qa_app_match_rankings *qa_app_match_rankings_create(void) {
  qa_app_match_rankings *rankings = checked(calloc(1, sizeof *rankings));
  rankings->authorization = checked(qa_local_authorization_create());
  rankings->ranking = checked(qa_local_ranking_service_create());
  if (!qa_event_ring_init(&rankings->results, RESULT_CAPACITY, sizeof(qa_app_match_result))) qa_range_error("Out of memory");
  if (!qa_event_ring_open_cursor(&rankings->results, &rankings->cursor, false)) qa_range_error("Out of memory");
  return rankings;
}
void qa_app_match_rankings_destroy(qa_app_match_rankings *rankings) {
  if (rankings == NULL) return;
  /* Records the cursor has not taken still own their strings; taken ones were freed by settle. */
  size_t pending = 0;
  qa_app_match_result *records = qa_event_ring_take(&rankings->results, &rankings->cursor, &pending);
  for (size_t i = 0; i < pending; i++) qa_app_match_result_free(&records[i]);
  free(records);
  qa_event_ring_close_cursor(&rankings->results, &rankings->cursor);
  qa_event_ring_destroy(&rankings->results);
  for (size_t i = 0; i < rankings->account_count; i++) {
    free(rankings->accounts[i]->name);
    qa_online_account_destroy(&rankings->accounts[i]->account);
    free(rankings->accounts[i]);
  }
  free(rankings->accounts);
  qa_local_ranking_service_free(rankings->ranking);
  qa_local_authorization_free(rankings->authorization);
  free(rankings);
}

/* TS private account(name). */
static const qa_online_account *account_of(qa_app_match_rankings *rankings, const char *name) {
  for (size_t i = 0; i < rankings->account_count; i++)
    if (strcmp(rankings->accounts[i]->name, name) == 0) return &rankings->accounts[i]->account;
  uint8_t bytes[24];
  qa_llm_random_bytes(bytes, sizeof bytes);
  char password[sizeof bytes * 2 + 1];
  for (size_t i = 0; i < sizeof bytes; i++) (void)snprintf(password + i * 2, 3, "%02x", bytes[i]);
  qa_online_account registered;
  memset(&registered, 0, sizeof registered);
  qa_local_authorization_register(rankings->authorization, name, password, &registered);
  qa_online_account_destroy(&registered);
  qa_authorization authorization;
  memset(&authorization, 0, sizeof authorization);
  qa_local_authorization_authorize(rankings->authorization, name, password, &authorization);
  if (!is(authorization.kind, "authorized")) {
    qa_authorization_destroy(&authorization);
    qa_raise("Error", 0, 0, "Local ranking account could not be authorized");
  }
  account_record *record = checked(calloc(1, sizeof *record));
  record->name = dup_text(name);
  record->account.id = dup_text(authorization.account.id);
  record->account.name = dup_text(authorization.account.name);
  qa_authorization_destroy(&authorization);
  if (rankings->account_count == rankings->account_capacity) {
    size_t capacity = rankings->account_capacity == 0 ? 8 : rankings->account_capacity * 2;
    rankings->accounts = checked(realloc(rankings->accounts, capacity * sizeof *rankings->accounts));
    rankings->account_capacity = capacity;
  }
  rankings->accounts[rankings->account_count++] = record;
  return &record->account;
}
/* TS names.get(account). */
static const char *name_of(qa_app_match_rankings *rankings, const char *account) {
  for (size_t i = 0; i < rankings->account_count; i++)
    if (strcmp(rankings->accounts[i]->account.id, account) == 0) return rankings->accounts[i]->name;
  return NULL;
}

/* One report of attach's grouping (Map<string, report> in insertion order). */
typedef struct attach_report {
  const char *rules, *match;
  qa_ranking_report_player *players; size_t player_count;
  qa_stat_entry *statistics; /* one "score" entry per player */
} attach_report;
typedef struct attach_state { attach_report *reports; size_t count; } attach_state;
static void attach_state_free(void *arg) {
  attach_state *state = arg;
  for (size_t i = 0; i < state->count; i++) { free(state->reports[i].players); free(state->reports[i].statistics); }
  free(state->reports);
  state->reports = NULL; state->count = 0;
}
void qa_app_match_rankings_attach(qa_app_match_rankings *rankings, qa_app_player_progress_store *store) {
  if (rankings->store == store) return;
  rankings->store = store;
  size_t count = 0;
  const qa_app_player_progress_event *const *events = qa_app_player_progress_store_all(store, &count);
  attach_state state = { checked(calloc(count == 0 ? 1 : count, sizeof *state.reports)), 0 };
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, attach_state_free, &state);
  for (size_t i = 0; i < count; i++) {
    const qa_app_player_progress_event *event = events[i];
    if (!is(event->kind, "ranked-match")) continue;
    const qa_online_account *account = account_of(rankings, event->participant);
    /* key JSON.stringify([event.rules, event.event]): equal exactly when both strings are. */
    attach_report *report = NULL;
    for (size_t r = 0; r < state.count && report == NULL; r++)
      if (strcmp(state.reports[r].rules, event->rules) == 0 && strcmp(state.reports[r].match, event->event) == 0) report = &state.reports[r];
    if (report == NULL) {
      report = &state.reports[state.count++];
      report->rules = event->rules; report->match = event->event;
      report->players = checked(calloc(count, sizeof *report->players));
      report->statistics = checked(calloc(count, sizeof *report->statistics));
    }
    size_t at = report->player_count++;
    report->statistics[at] = (qa_stat_entry){ "score", event->score };
    report->players[at] = (qa_ranking_report_player){ account->id, event->score, event->won, &report->statistics[at], 1 };
  }
  for (size_t r = 0; r < state.count; r++) {
    qa_online_ranking_report report = { state.reports[r].match, state.reports[r].rules, state.reports[r].players,
      state.reports[r].player_count };
    (void)qa_local_ranking_service_submit(rankings->ranking, &report);
  }
  qa_cleanup_pop(&cleanup);
  attach_state_free(&state);
}

static bool slot_in_range(double slot) {
  return slot == trunc(slot) && fabs(slot) <= (double)QA_MAX_SAFE_INTEGER && slot >= 0 && slot < MAX_SLOTS;
}
const qa_online_account *qa_app_match_rankings_bind(qa_app_match_rankings *rankings, double slot, const char *name) {
  if (!slot_in_range(slot)) qa_range_error("Ranking slot is out of range");
  const qa_online_account *account = account_of(rankings, name);
  rankings->slots[(size_t)slot] = account->id;
  return account;
}
void qa_app_match_rankings_unbind(qa_app_match_rankings *rankings, double slot) {
  if (slot_in_range(slot)) rankings->slots[(size_t)slot] = NULL;
}
double qa_app_match_rankings_dropped_results(const qa_app_match_rankings *rankings) { return rankings->dropped; }

const qa_app_match_result *qa_app_match_rankings_observe(qa_app_match_rankings *rankings,
    qa_app_shared_simulation *simulation, const qa_app_native_q2_map_transition *transition) {
  if (rankings->published == simulation) return NULL;
  char uuid[37];
  qa_llm_random_uuid(uuid);
  qa_app_match_result result;
  if (!qa_app_match_result_of(simulation, uuid, transition, &result)) return NULL;
  rankings->published = simulation;
  int64_t id = qa_event_ring_append(&rankings->results, &result);
  if (id == 0) {
    qa_app_match_result_free(&result);
    rankings->dropped += 1;
    return NULL;
  }
  return qa_event_ring_at(&rankings->results, id);
}

/* settle's state: the taken records (freed as each is consumed, the rest on a
 * throw), the merged players of the current record, and the touched accounts and
 * rules sets (rules copied, accounts borrowed from the account records). */
typedef struct settle_state {
  qa_app_match_result *taken; size_t taken_count, next;
  qa_ranking_report_player *merged; qa_stat_entry *statistics;
  const char **touched; size_t touched_count, touched_capacity;
  char **rules; size_t rules_count, rules_capacity;
} settle_state;
static void settle_state_free(void *arg) {
  settle_state *state = arg;
  for (size_t i = state->next; i < state->taken_count; i++) qa_app_match_result_free(&state->taken[i]);
  for (size_t i = 0; i < state->rules_count; i++) free(state->rules[i]);
  free(state->taken); free(state->merged); free(state->statistics); free(state->touched); free(state->rules);
  memset(state, 0, sizeof *state);
}
static void ranking_entry_release(qa_ranking_entry *entry) {
  free(entry->account);
  for (size_t j = 0; j < entry->statistic_count; j++) free(entry->stat_keys[j]);
  free(entry->stat_keys); free(entry->statistics);
}
/* One record of results.take(cursor): merge by account, submit, record. */
static void settle_record(qa_app_match_rankings *rankings, settle_state *state, const qa_app_match_result *result) {
  free(state->merged); free(state->statistics);
  state->merged = checked(calloc(result->player_count + 1, sizeof *state->merged));
  state->statistics = checked(calloc(result->player_count + 1, sizeof *state->statistics));
  size_t merged_count = 0;
  for (size_t p = 0; p < result->player_count; p++) {
    const qa_app_match_result_player *player = &result->players[p];
    const char *account = slot_in_range(player->slot) ? rankings->slots[(size_t)player->slot] : NULL;
    if (account == NULL) continue;
    size_t found = merged_count;
    for (size_t m = 0; m < merged_count && found == merged_count; m++) if (strcmp(state->merged[m].account, account) == 0) found = m;
    if (found == merged_count) {
      state->statistics[merged_count] = (qa_stat_entry){ "score", player->score };
      state->merged[merged_count] = (qa_ranking_report_player){ account, player->score, player->won, &state->statistics[merged_count], 1 };
      merged_count++;
    } else {
      double score = qa_js_max(state->merged[found].score, player->score);
      state->merged[found].score = score;
      state->merged[found].won = state->merged[found].won || player->won;
      state->statistics[found].value = score;
    }
  }
  if (merged_count == 0) return;
  qa_online_ranking_report report = { result->match, result->rules, state->merged, merged_count };
  (void)qa_local_ranking_service_submit(rankings->ranking, &report);
  bool seen = false;
  for (size_t r = 0; r < state->rules_count && !seen; r++) seen = strcmp(state->rules[r], result->rules) == 0;
  if (!seen) {
    if (state->rules_count == state->rules_capacity) {
      state->rules_capacity = state->rules_capacity == 0 ? 4 : state->rules_capacity * 2;
      state->rules = checked(realloc(state->rules, state->rules_capacity * sizeof *state->rules));
    }
    state->rules[state->rules_count++] = dup_text(result->rules);
  }
  for (size_t m = 0; m < merged_count; m++) {
    bool touched = false;
    for (size_t t = 0; t < state->touched_count && !touched; t++) touched = strcmp(state->touched[t], state->merged[m].account) == 0;
    if (!touched) {
      if (state->touched_count == state->touched_capacity) {
        state->touched_capacity = state->touched_capacity == 0 ? 8 : state->touched_capacity * 2;
        state->touched = checked(realloc(state->touched, state->touched_capacity * sizeof *state->touched));
      }
      state->touched[state->touched_count++] = state->merged[m].account;
    }
    const char *name = name_of(rankings, state->merged[m].account);
    if (rankings->store != NULL && name != NULL) {
      qa_app_player_progress_event event;
      memset(&event, 0, sizeof event);
      event.kind = "ranked-match"; event.source = result->family; event.participant = name;
      event.event = result->match; event.map = result->map; event.score = state->merged[m].score;
      event.won = state->merged[m].won; event.rules = result->rules;
      (void)qa_app_player_progress_store_record(rankings->store, event);
    }
  }
}
qa_ranking_entry *qa_app_match_rankings_settle(qa_app_match_rankings *rankings, size_t *out_count) {
  settle_state state;
  memset(&state, 0, sizeof state);
  state.taken = qa_event_ring_take(&rankings->results, &rankings->cursor, &state.taken_count);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, settle_state_free, &state);
  for (; state.next < state.taken_count; state.next++) {
    settle_record(rankings, &state, &state.taken[state.next]);
    qa_app_match_result_free(&state.taken[state.next]);
  }
  qa_event_ring_retire(&rankings->results);
  qa_ranking_entry *entries = NULL;
  size_t count = 0, capacity = 0;
  for (size_t r = 0; r < state.rules_count; r++) {
    size_t standing_count = 0;
    qa_ranking_entry *standings = qa_local_ranking_service_standings(rankings->ranking, state.rules[r], &standing_count);
    for (size_t e = 0; e < standing_count; e++) {
      bool touched = false;
      for (size_t t = 0; t < state.touched_count && !touched; t++) touched = strcmp(state.touched[t], standings[e].account) == 0;
      if (!touched) { ranking_entry_release(&standings[e]); continue; }
      if (count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        entries = checked(realloc(entries, capacity * sizeof *entries));
      }
      entries[count++] = standings[e];
    }
    free(standings);
  }
  qa_cleanup_pop(&cleanup);
  settle_state_free(&state);
  *out_count = count;
  return entries != NULL ? entries : checked(calloc(1, sizeof *entries));
}
qa_ranking_entry *qa_app_match_rankings_standings(qa_app_match_rankings *rankings, qa_provider_id rules, size_t *out_count) {
  return qa_local_ranking_service_standings(rankings->ranking, rules, out_count);
}
