/* Port of src/network/services/rankings.ts. GPL-2.0-or-later. */
#include "qa/network/services/rankings.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct player_entry { double slot; qa_ranking_player_state state; char *reason; };
struct qa_ranking_lifecycle {
  const qa_ranking_provider *provider;
  qa_ranking_changed_fn changed; void *changed_ctx;
  qa_ranking_service_changed_fn service_changed; void *service_ctx;
  qa_ranking_service_state current; char *reason;
  bool has_match; qa_ranking_match match;
  struct player_entry *players; size_t count, capacity;
};
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
void qa_ranking_lifecycle_init(qa_ranking_lifecycle *l, const qa_ranking_provider *provider,
                               qa_ranking_changed_fn changed, void *changed_ctx,
                               qa_ranking_service_changed_fn service_changed, void *service_ctx) {
  memset(l, 0, sizeof *l);
  l->provider = provider; l->changed = changed; l->changed_ctx = changed_ctx;
  l->service_changed = service_changed; l->service_ctx = service_ctx;
  l->current.kind = "disabled";
}
qa_ranking_lifecycle *qa_ranking_lifecycle_create(const qa_ranking_provider *provider,
                                                  qa_ranking_changed_fn changed, void *changed_ctx,
                                                  qa_ranking_service_changed_fn service_changed, void *service_ctx) {
  qa_ranking_lifecycle *l = malloc(sizeof *l);
  if (l) qa_ranking_lifecycle_init(l, provider, changed, changed_ctx, service_changed, service_ctx);
  return l;
}
void qa_ranking_lifecycle_free(qa_ranking_lifecycle *l) { if (l) { qa_ranking_lifecycle_destroy(l); free(l); } }
void qa_ranking_lifecycle_destroy(qa_ranking_lifecycle *l) {
  for (size_t i = 0; i < l->count; i++) free(l->players[i].reason);
  free(l->players); free(l->reason); memset(l, 0, sizeof *l);
}
static void set_state(qa_ranking_lifecycle *l, const char *kind, const char *reason, uint64_t game_id) {
  free(l->reason); l->reason = reason ? dupstr(reason) : NULL;
  l->current.kind = kind; l->current.reason = l->reason; l->current.game_id = game_id;
  if (l->service_changed) l->service_changed(l->service_ctx, l->current);
}
void qa_ranking_lifecycle_unavailable(qa_ranking_lifecycle *l, const char *reason) { set_state(l, "unavailable", reason, 0); }
qa_ranking_service_state qa_ranking_lifecycle_get_state(const qa_ranking_lifecycle *l) { return l->current; }
static struct player_entry *find_player(qa_ranking_lifecycle *l, double slot) {
  for (size_t i = 0; i < l->count; i++) if (l->players[i].slot == slot) return &l->players[i];
  return NULL;
}
qa_ranking_player_state qa_ranking_lifecycle_player(const qa_ranking_lifecycle *l, double slot) {
  for (size_t i = 0; i < l->count; i++) if (l->players[i].slot == slot) return l->players[i].state;
  qa_ranking_player_state s; memset(&s, 0, sizeof s); s.kind = "new"; return s;
}
static void set_player(qa_ranking_lifecycle *l, double slot, qa_ranking_player_state state) {
  struct player_entry *e = find_player(l, slot);
  if (!e) {
    if (l->count == l->capacity) { size_t nc = l->capacity ? l->capacity * 2 : 8; l->players = realloc(l->players, nc * sizeof *l->players); l->capacity = nc; }
    e = &l->players[l->count++]; e->slot = slot; e->reason = NULL;
  }
  free(e->reason); e->reason = state.reason ? dupstr(state.reason) : NULL;
  e->state = state; e->state.reason = e->reason;
  l->changed(l->changed_ctx, slot, e->state);
}
static qa_ranking_player_state player_state(const char *kind, const char *reason) {
  qa_ranking_player_state s; memset(&s, 0, sizeof s); s.kind = kind; s.reason = reason; return s;
}
/* TS enqueue().catch: failures mark service unavailable, then re-abort. */
static void fail_named(qa_ranking_lifecycle *l, const char *name, const char *message) {
  const char *text = message ? message : "Ranking service failed";
  char copy[QA_CAUGHT_MESSAGE_CAPACITY];
  snprintf(copy, sizeof copy, "%s", text); /* the provider's borrowed text may not outlive set_state */
  set_state(l, "unavailable", copy, 0);
  qa_raise(name, 0, 0, "%s", copy);
}
static void fail(qa_ranking_lifecycle *l, const char *message) { fail_named(l, "Error", message); }
void qa_ranking_lifecycle_begin(qa_ranking_lifecycle *l, bool enabled, bool single_player, const char *game_key) {
  if (l->has_match) fail(l, "Ranking match is already active");
  if (!enabled || single_player) { set_state(l, "disabled", NULL, 0); return; }
  if (!l->provider) {
    set_state(l, "unavailable", "No ranking service is configured. Local progress and match records remain available.", 0);
    return;
  }
  set_state(l, "starting", NULL, 0);
  const char *error = NULL;
  qa_ranking_match m;
  if (!l->provider->begin(l->provider->ctx, game_key, &m, &error)) fail(l, error);
  l->match = m; l->has_match = true;
  set_state(l, "active", NULL, m.game_id);
}
void qa_ranking_lifecycle_account(qa_ranking_lifecycle *l, double slot, const qa_ranking_account_request *request) {
  if (!l->provider || !l->has_match || strcmp(l->current.kind, "active") != 0) fail(l, "Ranking service is not active");
  if (strcmp(qa_ranking_lifecycle_player(l, slot).kind, "active") == 0) fail(l, "Ranking player is already active");
  set_player(l, slot, player_state("pending", NULL));
  const char *error = NULL;
  qa_ranking_login_result result; memset(&result, 0, sizeof result);
  if (!l->provider->login(l->provider->ctx, l->match, request, &result, &error)) {
    set_player(l, slot, player_state("denied", error ? error : "Ranking login failed"));
    fail(l, error ? error : "Ranking login failed");
  }
  if (!result.active) { set_player(l, slot, player_state("denied", result.reason)); return; }
  for (size_t i = 0; i < l->count; i++)
    if (strcmp(l->players[i].state.kind, "active") == 0 && l->players[i].state.account.player_id == result.account.player_id) {
      set_player(l, slot, player_state("denied", "Ranking account is already joined"));
      fail(l, "Ranking account is already joined");
    }
  if (!l->provider->join(l->provider->ctx, l->match, result.account, &error)) {
    set_player(l, slot, player_state("denied", error ? error : "Ranking login failed"));
    fail(l, error ? error : "Ranking login failed");
  }
  qa_ranking_player_state active; memset(&active, 0, sizeof active);
  active.kind = "active"; active.account = result.account;
  set_player(l, slot, active);
}
static int player_id(qa_ranking_lifecycle *l, double slot, uint64_t *out) {
  if (slot == -1) { *out = 0; return 1; }
  qa_ranking_player_state s = qa_ranking_lifecycle_player(l, slot);
  if (strcmp(s.kind, "active") != 0) return 0;
  *out = s.account.player_id; return 1;
}
static void do_report(qa_ranking_lifecycle *l, double self_slot, double other_slot, qa_ranking_report *report) {
  if (strcmp(l->current.kind, "active") != 0 || !l->has_match || !l->provider) return;
  uint64_t first, second;
  if (!player_id(l, self_slot, &first) || !player_id(l, other_slot, &second)) return;
  report->self_id = first; report->other_id = second;
  const char *error = NULL;
  if (!l->provider->report(l->provider->ctx, l->match, report, &error)) fail(l, error);
}
void qa_ranking_lifecycle_report_int(qa_ranking_lifecycle *l, double self_slot, double other_slot, double key, double value, bool accumulate) {
  qa_ranking_report r; memset(&r, 0, sizeof r);
  r.key = key; r.value = value; r.accumulate = accumulate;
  do_report(l, self_slot, other_slot, &r);
}
void qa_ranking_lifecycle_report_string(qa_ranking_lifecycle *l, double self_slot, double other_slot, double key, const char *value) {
  qa_ranking_report r; memset(&r, 0, sizeof r);
  r.is_string = true; r.key = key; r.text = value;
  do_report(l, self_slot, other_slot, &r);
}
void qa_ranking_lifecycle_frame(qa_ranking_lifecycle *l) {
  if (strcmp(l->current.kind, "active") != 0 || !l->provider) return;
  const char *error = NULL;
  if (!l->provider->poll(l->provider->ctx, &error)) fail(l, error);
}
void qa_ranking_lifecycle_reset(qa_ranking_lifecycle *l, double slot) {
  qa_ranking_player_state s = qa_ranking_lifecycle_player(l, slot);
  if (strcmp(l->current.kind, "active") == 0 && (strcmp(s.kind, "denied") == 0 || strcmp(s.kind, "spectator") == 0))
    set_player(l, slot, player_state("new", NULL));
}
void qa_ranking_lifecycle_spectate(qa_ranking_lifecycle *l, double slot) {
  if (strcmp(l->current.kind, "active") != 0) return;
  qa_ranking_player_state s = qa_ranking_lifecycle_player(l, slot);
  if (strcmp(s.kind, "active") == 0 && l->has_match && l->provider) {
    const char *error = NULL;
    if (!l->provider->logout(l->provider->ctx, l->match, s.account, &error)) fail(l, error);
  }
  set_player(l, slot, player_state("spectator", NULL));
}
void qa_ranking_lifecycle_disconnect(qa_ranking_lifecycle *l, double slot) {
  qa_ranking_player_state s = qa_ranking_lifecycle_player(l, slot);
  const char *error = NULL; bool failed = false;
  if (strcmp(s.kind, "active") == 0 && l->has_match && l->provider)
    failed = !l->provider->logout(l->provider->ctx, l->match, s.account, &error);
  struct player_entry *e = find_player(l, slot);
  if (e) {
    size_t idx = (size_t)(e - l->players);
    free(e->reason);
    memmove(&l->players[idx], &l->players[idx+1], (l->count - idx - 1) * sizeof *l->players);
    l->count--;
  }
  l->changed(l->changed_ctx, slot, player_state("new", NULL));
  if (failed) fail(l, error);
}
void qa_ranking_lifecycle_end(qa_ranking_lifecycle *l) {
  if (!l->has_match) return;
  qa_ranking_match match = l->match;
  set_state(l, "ending", NULL, 0);
  bool any_failure = false;
  for (size_t i = 0; i < l->count; i++) {
    struct player_entry *e = &l->players[i];
    if (strcmp(e->state.kind, "active") == 0 && l->provider) {
      const char *error = NULL;
      if (!l->provider->logout(l->provider->ctx, match, e->state.account, &error)) any_failure = true;
    }
    l->changed(l->changed_ctx, e->slot, player_state("new", NULL));
  }
  if (l->provider) {
    const char *error = NULL;
    if (!l->provider->finish(l->provider->ctx, match, &error)) any_failure = true;
  }
  l->has_match = false;
  for (size_t i = 0; i < l->count; i++) free(l->players[i].reason);
  l->count = 0;
  set_state(l, "disabled", NULL, 0);
  if (any_failure) fail_named(l, "AggregateError", "Ranking report submission or cleanup failed");
}
