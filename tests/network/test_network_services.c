/* Ports tests/network/common/discovery.test.ts (with a scripted discovery wire that carries
 * the challenge as text in place of the Q3 codec), tests/network/services/rankings.test.ts,
 * and the services' malformed-input paths: favorites restore over arbitrary JSON, the
 * unsolicited LAN bound, a throwing transport, and the ranking text codecs.
 * GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/services/discovery.h"
#include "qa/network/services/download_types.h"
#include "qa/network/services/online.h"
#include "qa/network/services/rank_codec.h"
#include "qa/network/services/rankings.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int sentinel;
#define SENTINEL ((void *)&sentinel)

/* --- discovery --- */
typedef struct fixture {
  void *sentinel;
  char sent[32][48]; int sent_count;
  bool fail_send, throw_send;
  qa_server_browser *browser;
} fixture;
static uint8_t *wire_query(void *ctx, const char *kind, const char *challenge, size_t *out_len) {
  CHECK(ctx == SENTINEL);
  char text[64]; int n = snprintf(text, sizeof text, "%s %s", kind, challenge);
  uint8_t *bytes = malloc((size_t)n); memcpy(bytes, text, (size_t)n); *out_len = (size_t)n; return bytes;
}
static uint8_t *wire_master(void *ctx, size_t *out_len) { CHECK(ctx == SENTINEL); uint8_t *b = malloc(1); b[0] = 'm'; *out_len = 1; return b; }
static uint8_t *wire_heartbeat(void *ctx, bool active, size_t *out_len) { CHECK(ctx == SENTINEL); uint8_t *b = malloc(1); b[0] = active ? 'a' : 'i'; *out_len = 1; return b; }
static bool sender_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  fixture *f = ctx; CHECK(f->sentinel == SENTINEL); (void)to;
  if (f->throw_send) qa_raise("Error", 0, 0, "send failed");
  if (f->fail_send) return false;
  int i = f->sent_count++ % 32;
  memcpy(f->sent[i], bytes, length < 47 ? length : 47); f->sent[i][length < 47 ? length : 47] = '\0';
  return true;
}
static void fixture_init(fixture *f) {
  memset(f, 0, sizeof *f); f->sentinel = SENTINEL;
  qa_discovery_wire wire = {SENTINEL, wire_query, wire_master, wire_heartbeat};
  qa_packet_sender sender = {f, sender_send};
  f->browser = qa_server_browser_create(wire, sender);
}
static const char *challenge(fixture *f, int index) { return strchr(f->sent[index], ' ') + 1; }
static qa_net_address address, other;
static void make_status(qa_server_status *status, const char *name, bool players) {
  memset(status, 0, sizeof *status);
  status->name = strdup(name); status->map = strdup("q3dm1"); status->players = 1; status->max_players = 8;
  qa_qw_info_init(&status->rules);
  if (players) {
    status->player_count = 1; status->player_details = calloc(1, sizeof *status->player_details);
    status->player_details[0].name = strdup("Player"); status->player_details[0].score = 7; status->player_details[0].ping = 42;
  }
}
static long long request(fixture *f, double now, const char *kind) {
  long long handle = qa_server_browser_request(f->browser, &address, now, kind, false, false, 0);
  CHECK(handle > 0); return handle;
}
static const char *result_kind(fixture *f, long long handle, double *ping, qa_discovery_result *keep) {
  static char kind[16];
  qa_discovery_result r;
  if (!qa_server_browser_request_result(f->browser, handle, &r)) return NULL;
  snprintf(kind, sizeof kind, "%s", r.kind);
  if (ping) *ping = r.ping_ms;
  if (keep) *keep = r;
  else { qa_net_address_destroy(&r.address); if (r.has_status) qa_server_status_destroy(&r.status); }
  return kind;
}
static bool sources_are(const qa_browser_entry *e, const char *const *expected, size_t count) {
  if (!e || e->source_count != count) return false;
  for (size_t i = 0; i < count; i++) if (strcmp(e->sources[i], expected[i]) != 0) return false;
  return true;
}
static void test_lan_and_cache(void) {
  fixture f; fixture_init(&f);
  qa_server_browser_add(f.browser, &address, "favorite", 0);
  int64_t all[4] = {255, 255, 255, 255};
  qa_net_address broadcast; qa_net_make_ipv4(&broadcast, all, 27960, false);
  qa_server_browser_broadcast(f.browser, &broadcast, 1, 10);
  qa_server_status reply; make_status(&reply, "Arena", false);
  CHECK(qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 0), 20, NULL));
  static const char *fav_lan[] = {"favorite", "lan"};
  CHECK(sources_are(qa_server_browser_get_entry(f.browser, &address), fav_lan, 2));
  qa_browser_entry cached; memset(&cached, 0, sizeof cached);
  qa_net_address_clone(&address, &cached.address);
  static const char *secondary[] = {"secondary-master"};
  cached.sources = (const char **)secondary; cached.source_count = 1;
  cached.has_status = true; make_status(&cached.status, "Stale cache", false); cached.has_ping = true; cached.ping_ms = 100;
  qa_server_browser_restore_entry(f.browser, &cached);
  CHECK(strcmp(qa_server_browser_get_entry(f.browser, &address)->status.name, "Arena") == 0);
  static const char *three[] = {"favorite", "lan", "secondary-master"};
  CHECK(sources_are(qa_server_browser_get_entry(f.browser, &address), three, 3));
  qa_server_browser_remove_source(f.browser, &address, "secondary-master");
  qa_server_browser_remove_favorite(f.browser, &address);
  static const char *lan[] = {"lan"};
  CHECK(sources_are(qa_server_browser_get_entry(f.browser, &address), lan, 1));
  /* An entry without status takes every restored field (TS spreads the restored entry). */
  qa_server_browser_add(f.browser, &other, "favorite", 3);
  qa_net_address_destroy(&cached.address); qa_net_address_clone(&other, &cached.address);
  cached.updated_at = 77;
  qa_server_browser_restore_entry(f.browser, &cached);
  const qa_browser_entry *restored = qa_server_browser_get_entry(f.browser, &other);
  CHECK(restored->has_status && strcmp(restored->status.name, "Stale cache") == 0 && restored->updated_at == 77 && restored->ping_ms == 100);
  qa_server_status_destroy(&cached.status); qa_net_address_destroy(&cached.address);
  qa_server_status_destroy(&reply);
  qa_server_browser_free(f.browser);
}
static void test_owner_requests(void) {
  fixture f; fixture_init(&f);
  long long retained = qa_server_browser_request(f.browser, &address, 0, "info", true, true, 0);
  long long timed = qa_server_browser_request(f.browser, &other, 0, "status", true, false, 100);
  CHECK(retained > 0 && timed > 0);
  size_t n = 0; free(qa_server_browser_expire_queries(f.browser, 3000, 3000, &n));
  CHECK(strcmp(result_kind(&f, retained, NULL, NULL), "pending") == 0);
  CHECK(strcmp(result_kind(&f, timed, NULL, NULL), "expired") == 0);
  qa_server_status reply; make_status(&reply, "Arena", false);
  CHECK(qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 0), 4000, NULL));
  double ping = 0;
  CHECK(strcmp(result_kind(&f, retained, &ping, NULL), "completed") == 0 && ping == 4000);
  qa_server_browser_release_request(f.browser, retained);
  CHECK(result_kind(&f, retained, NULL, NULL) == NULL);
  long long released = qa_server_browser_request(f.browser, &address, 5000, "info", true, true, 0);
  char late[16]; snprintf(late, sizeof late, "%s", challenge(&f, 2));
  qa_server_browser_release_request(f.browser, released);
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, late, 9000, NULL));
  qa_server_status_destroy(&reply);
  qa_server_browser_free(f.browser);
}
static void test_info_status_order(void) {
  for (int status_first = 1; status_first >= 0; status_first--) {
    fixture f; fixture_init(&f);
    long long info = request(&f, 10, "info"), status = request(&f, 20, "status");
    int order[2] = {status_first ? 1 : 0, status_first ? 0 : 1};
    for (int i = 0; i < 2; i++) {
      qa_server_status reply; make_status(&reply, "Arena", order[i] == 1);
      CHECK(qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, order[i]), 50 + order[i], NULL));
      qa_server_status_destroy(&reply);
    }
    double ping = 0; qa_discovery_result r;
    CHECK(strcmp(result_kind(&f, info, &ping, NULL), "completed") == 0 && ping == 40);
    CHECK(strcmp(result_kind(&f, status, &ping, &r), "completed") == 0 && ping == 31 && strcmp(r.request_kind, "status") == 0
      && r.status.player_count == 1 && strcmp(r.status.player_details[0].name, "Player") == 0 && r.status.player_details[0].ping == 42);
    qa_net_address_destroy(&r.address); qa_server_status_destroy(&r.status);
    CHECK(qa_server_browser_size(f.browser) == 1);
    size_t n = 7; free(qa_server_browser_expire_queries(f.browser, 5000, 3000, &n)); CHECK(n == 0);
    CHECK(strcmp(result_kind(&f, info, NULL, NULL), "completed") == 0);
    qa_server_browser_free(f.browser);
  }
}
static void test_wrong_replies(void) {
  fixture f; fixture_init(&f);
  long long handle = request(&f, 0, "info");
  qa_server_status reply; make_status(&reply, "Arena", false);
  CHECK(!qa_server_browser_receive(f.browser, &other, &reply, challenge(&f, 0), 10, NULL));
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, "unknown", 10, NULL));
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 0), 10, "status"));
  CHECK(strcmp(result_kind(&f, handle, NULL, NULL), "pending") == 0);
  CHECK(qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 0), 10, NULL));
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 0), 20, NULL));
  /* Challenge-less replies need an unambiguous request and can use explicit kind. */
  fixture g; fixture_init(&g);
  long long info = request(&g, 0, "info"), status = request(&g, 1, "status");
  CHECK(!qa_server_browser_receive(g.browser, &address, &reply, NULL, 10, NULL));
  CHECK(qa_server_browser_receive(g.browser, &address, &reply, NULL, 10, "status"));
  CHECK(strcmp(result_kind(&g, status, NULL, NULL), "completed") == 0 && strcmp(result_kind(&g, info, NULL, NULL), "pending") == 0);
  CHECK(qa_server_browser_receive(g.browser, &address, &reply, NULL, 11, NULL));
  /* Same-kind explicit requests are independently challenge-correlated. */
  fixture h; fixture_init(&h);
  long long first = request(&h, 0, "info"), second = request(&h, 1, "info");
  CHECK(!qa_server_browser_receive(h.browser, &address, &reply, NULL, 10, "info"));
  CHECK(qa_server_browser_receive(h.browser, &address, &reply, challenge(&h, 0), 10, NULL));
  CHECK(strcmp(result_kind(&h, first, NULL, NULL), "completed") == 0 && strcmp(result_kind(&h, second, NULL, NULL), "pending") == 0);
  qa_server_status_destroy(&reply);
  qa_server_browser_free(f.browser); qa_server_browser_free(g.browser); qa_server_browser_free(h.browser);
}
static void test_expiry_cancel_release(void) {
  fixture f; fixture_init(&f);
  long long expired = request(&f, 0, "info"), cancelled = request(&f, 1, "status");
  qa_server_browser_add(f.browser, &address, "favorite", 0);
  CHECK(qa_server_browser_cancel_request(f.browser, cancelled)); CHECK(!qa_server_browser_cancel_request(f.browser, cancelled));
  CHECK(strcmp(result_kind(&f, cancelled, NULL, NULL), "cancelled") == 0);
  size_t n = 9; free(qa_server_browser_expire_queries(f.browser, 2999, 3000, &n)); CHECK(n == 0);
  qa_net_address *list = qa_server_browser_expire_queries(f.browser, 3000, 3000, &n);
  CHECK(n == 1 && qa_net_same_address(&list[0], &address, true));
  for (size_t i = 0; i < n; i++) qa_net_address_destroy(&list[i]);
  free(list);
  CHECK(strcmp(result_kind(&f, expired, NULL, NULL), "expired") == 0);
  free(qa_server_browser_expire_queries(f.browser, 4000, 3000, &n)); CHECK(n == 0);
  qa_server_status reply; make_status(&reply, "Arena", false);
  for (int index = 0; index < 2; index++) CHECK(!qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, index), 4000, NULL));
  qa_server_browser_release_request(f.browser, expired); qa_server_browser_release_request(f.browser, cancelled);
  CHECK(result_kind(&f, expired, NULL, NULL) == NULL && result_kind(&f, cancelled, NULL, NULL) == NULL);
  long long released = request(&f, 5000, "info");
  qa_server_browser_release_request(f.browser, released);
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 2), 5010, NULL));
  qa_net_address *favorites = qa_server_browser_favorite_addresses(f.browser, &n);
  CHECK(n == 1 && qa_net_same_address(&favorites[0], &address, true));
  for (size_t i = 0; i < n; i++) qa_net_address_destroy(&favorites[i]);
  free(favorites);
  /* Expiration deduplicates addresses and retains requests without a browser row. */
  fixture g; fixture_init(&g);
  long long info = request(&g, 0, "info"), status = request(&g, 0, "status");
  free(qa_server_browser_expire_queries(g.browser, 10, 10, &n)); CHECK(n == 0);
  CHECK(strcmp(result_kind(&g, info, NULL, NULL), "expired") == 0 && strcmp(result_kind(&g, status, NULL, NULL), "expired") == 0);
  qa_server_browser_add(g.browser, &address, "direct", 0); request(&g, 20, "info"); request(&g, 20, "status");
  list = qa_server_browser_expire_queries(g.browser, 30, 10, &n);
  CHECK(n == 1 && qa_net_same_address(&list[0], &address, true));
  for (size_t i = 0; i < n; i++) qa_net_address_destroy(&list[i]);
  free(list);
  qa_server_status_destroy(&reply);
  qa_server_browser_free(f.browser); qa_server_browser_free(g.browser);
}
static void test_menu_queries(void) {
  fixture f; fixture_init(&f);
  long long retained = request(&f, 0, "info");
  CHECK(qa_server_browser_query(f.browser, &address, 1, NULL));
  CHECK(qa_server_browser_query(f.browser, &address, 2, "status"));
  CHECK(qa_server_browser_query(f.browser, &address, 3, NULL));
  qa_server_status reply; make_status(&reply, "Arena", false);
  CHECK(!qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, 1), 10, NULL));
  int order[3] = {2, 3, 0};
  for (int i = 0; i < 3; i++) CHECK(qa_server_browser_receive(f.browser, &address, &reply, challenge(&f, order[i]), 10, NULL));
  CHECK(strcmp(result_kind(&f, retained, NULL, NULL), "completed") == 0);
  size_t n = 1; free(qa_server_browser_expire_queries(f.browser, 5000, 3000, &n)); CHECK(n == 0);
  /* Legacy challenge-less refresh retains the latest query time. */
  fixture g; fixture_init(&g);
  qa_server_browser_query(g.browser, &address, 0, NULL); qa_server_browser_query(g.browser, &address, 5, NULL);
  CHECK(qa_server_browser_receive(g.browser, &address, &reply, NULL, 10, NULL));
  CHECK(qa_server_browser_entry_at(g.browser, 0)->ping_ms == 5);
  CHECK(!qa_server_browser_receive(g.browser, &address, &reply, NULL, 11, NULL));
  qa_server_status_destroy(&reply);
  qa_server_browser_free(f.browser); qa_server_browser_free(g.browser);
}
static void test_broadcast_and_bound(void) {
  fixture f; fixture_init(&f);
  long long status = request(&f, 0, "status");
  int64_t all[4] = {255, 255, 255, 255};
  qa_net_address broadcast; qa_net_make_ipv4(&broadcast, all, 27960, false);
  CHECK(qa_server_browser_broadcast(f.browser, &broadcast, 1, 5) == 1);
  qa_server_status lan; make_status(&lan, "Arena", false);
  char lan_challenge[16]; snprintf(lan_challenge, sizeof lan_challenge, "%s", challenge(&f, 1));
  CHECK(qa_server_browser_receive(f.browser, &address, &lan, lan_challenge, 10, NULL));
  CHECK(qa_server_browser_receive(f.browser, &other, &lan, lan_challenge, 11, NULL));
  static const char *lan_only[] = {"lan"};
  CHECK(qa_server_browser_size(f.browser) == 2 && sources_are(qa_server_browser_entry_at(f.browser, 0), lan_only, 1)
    && sources_are(qa_server_browser_entry_at(f.browser, 1), lan_only, 1));
  CHECK(strcmp(result_kind(&f, status, NULL, NULL), "pending") == 0);
  CHECK(qa_server_browser_receive(f.browser, &address, &lan, challenge(&f, 0), 12, NULL));
  CHECK(strcmp(result_kind(&f, status, NULL, NULL), "completed") == 0);
  size_t n; free(qa_server_browser_expire_queries(f.browser, 3005, 3000, &n));
  CHECK(!qa_server_browser_receive(f.browser, &other, &lan, lan_challenge, 3010, NULL));
  /* Unsolicited LAN answers stop adding rows at 4096 entries. */
  CHECK(qa_server_browser_broadcast(f.browser, &broadcast, 1, 4000) == 1);
  snprintf(lan_challenge, sizeof lan_challenge, "%s", challenge(&f, f.sent_count - 1));
  for (int i = 0; i < 4200; i++) {
    int64_t host[4] = {10, (i >> 16) & 255, (i >> 8) & 255, i & 255};
    qa_net_address spoof; qa_net_make_ipv4(&spoof, host, 27960, false);
    (void)qa_server_browser_receive(f.browser, &spoof, &lan, lan_challenge, 4001, NULL);
  }
  CHECK(qa_server_browser_size(f.browser) == 4096);
  CHECK(qa_server_browser_receive(f.browser, &address, &lan, lan_challenge, 4002, NULL)); /* existing rows still update */
  qa_server_status_destroy(&lan);
  qa_server_browser_free(f.browser);
}
struct request_scope { qa_server_browser *b; long long handle; };
static void request_body(void *ctx) { struct request_scope *s = ctx; s->handle = qa_server_browser_request(s->b, &address, 0, "info", false, false, 0); }
static void test_failed_sends(void) {
  qa_server_status reply; make_status(&reply, "Arena", false);
  for (int throws = 0; throws < 2; throws++) {
    fixture f; fixture_init(&f);
    if (throws) f.throw_send = true; else f.fail_send = true;
    struct request_scope scope = {f.browser, -1};
    qa_caught_error caught;
    if (throws) { CHECK(!qa_try(request_body, &scope, &caught) && strcmp(caught.message, "send failed") == 0); }
    else { CHECK(qa_try(request_body, &scope, &caught) && scope.handle == 0); }
    CHECK(!qa_server_browser_receive(f.browser, &address, &reply, "1", 10, NULL));
    size_t n = 1; free(qa_server_browser_expire_queries(f.browser, 5000, 3000, &n)); CHECK(n == 0);
    CHECK(qa_server_browser_pending_requests(f.browser) == 0);
    qa_server_browser_free(f.browser);
  }
  qa_server_status_destroy(&reply);
}
struct restore_scope { qa_server_browser *b; const char *text; };
static void restore_body(void *ctx) { struct restore_scope *s = ctx; qa_server_browser_restore_favorites(s->b, s->text); }
static void test_favorites(void) {
  fixture f; fixture_init(&f);
  qa_net_address loop; qa_net_make_loopback(&loop, "say \"hi\"\n");
  qa_server_browser_add(f.browser, &address, "favorite", 0);
  qa_server_browser_add(f.browser, &loop, "favorite", 0);
  char *saved = qa_server_browser_save_favorites(f.browser);
  CHECK(strcmp(saved, "[{\"kind\":\"ipv4\",\"host\":[127,0,0,1],\"port\":27960},{\"kind\":\"loopback\",\"id\":\"say \\\"hi\\\"\\n\"}]") == 0);
  fixture g; fixture_init(&g);
  qa_server_browser_add(g.browser, &other, "favorite", 0);
  struct restore_scope scope = {g.browser, saved};
  qa_caught_error caught;
  CHECK(qa_try(restore_body, &scope, &caught));
  size_t n; qa_net_address *favorites = qa_server_browser_favorite_addresses(g.browser, &n);
  CHECK(n == 2 && qa_net_same_address(&favorites[0], &address, true) && qa_net_same_address(&favorites[1], &loop, true));
  for (size_t i = 0; i < n; i++) qa_net_address_destroy(&favorites[i]);
  free(favorites);
  static const struct { const char *text, *name, *message; } bad[] = {
    {"{}", "Error", "Invalid favorites save"},
    {"[1]", "RangeError", "Invalid network address record"},
    {"[{\"kind\":\"ipv4\",\"host\":[1,2,3,4]}]", "RangeError", "Network address has no port"},
    {"[{\"kind\":\"ipv4\",\"host\":[1,2,3,4.5],\"port\":1}]", "RangeError", "Invalid IPv4 octet"},
    {"[{\"kind\":\"ipv4\",\"host\":[1,2,3,4],\"port\":1.5}]", "RangeError", "Invalid network port"},
    {"[{\"kind\":\"ipv6\",\"host\":\"1.2.3.4\",\"port\":1}]", "RangeError", "IPv6 record contains another address family"},
    {"[{\"kind\":\"ipx\",\"network\":4294967296,\"node\":[1,2,3,4,5,6],\"port\":1}]", "RangeError", "Invalid IPX address"},
    {"[{\"kind\":\"tcp\",\"port\":1}]", "RangeError", "Invalid network address fields"},
  };
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
    scope.text = bad[i].text;
    CHECK(!qa_try(restore_body, &scope, &caught));
    CHECK(strcmp(caught.name, bad[i].name) == 0 && strcmp(caught.message, bad[i].message) == 0);
  }
  scope.text = "[{\"kind\":";
  CHECK(!qa_try(restore_body, &scope, &caught));
  /* A failed restore changes nothing. */
  favorites = qa_server_browser_favorite_addresses(g.browser, &n);
  CHECK(n == 2);
  for (size_t i = 0; i < n; i++) qa_net_address_destroy(&favorites[i]);
  free(favorites);
  free(saved); qa_net_address_destroy(&loop);
  qa_server_browser_free(f.browser); qa_server_browser_free(g.browser);
}
static void test_heartbeat(void) {
  fixture f; fixture_init(&f);
  qa_discovery_wire wire = {SENTINEL, wire_query, wire_master, wire_heartbeat};
  qa_packet_sender sender = {&f, sender_send};
  qa_master_heartbeat_service heartbeat; qa_master_heartbeat_service_init(&heartbeat, wire, sender, 300000);
  CHECK(qa_master_heartbeat_service_send(&heartbeat, &address, 1, 0, true, false) == 1);
  CHECK(qa_master_heartbeat_service_send(&heartbeat, &address, 1, 1000, true, false) == 0);
  CHECK(qa_master_heartbeat_service_send(&heartbeat, &address, 1, 1000, false, true) == 1);
  CHECK(strcmp(f.sent[1], "i") == 0);
  CHECK(qa_server_browser_query_master(f.browser, &address) && strcmp(f.sent[2], "m") == 0);
  qa_server_browser_receive_master(f.browser, &other, 1, 9);
  static const char *master[] = {"master"};
  CHECK(sources_are(qa_server_browser_get_entry(f.browser, &other), master, 1));
  qa_server_browser_free(f.browser);
}

/* --- rankings.test.ts --- */
typedef struct rank_fixture {
  void *sentinel;
  char events[16][40]; int event_count;
  qa_ranking_report reports[8]; char report_text[8][16]; int report_count;
  char changed[16][24]; int changed_count;
  bool logout_fails;
} rank_fixture;
static void event(rank_fixture *f, const char *text) { snprintf(f->events[f->event_count++ & 15], 40, "%s", text); }
static int p_begin(void *ctx, const char *key, qa_ranking_match *out, const char **error) {
  rank_fixture *f = ctx; (void)error; char text[40]; snprintf(text, sizeof text, "begin:%s", key); event(f, text);
  out->game_id = 17; return 1;
}
static int p_login(void *ctx, qa_ranking_match match, const qa_ranking_account_request *request, qa_ranking_login_result *out, const char **error) {
  rank_fixture *f = ctx; (void)match; (void)error; event(f, request->kind);
  if (strcmp(request->username, "denied") == 0) { out->active = false; out->reason = "No membership"; }
  else { out->active = true; out->account.player_id = 81; out->account.rank = 9; }
  return 1;
}
static int p_join(void *ctx, qa_ranking_match match, qa_ranking_account account, const char **error) {
  (void)match; (void)account; (void)error; event(ctx, "join"); return 1;
}
static int p_report(void *ctx, qa_ranking_match match, const qa_ranking_report *report, const char **error) {
  rank_fixture *f = ctx; (void)match; (void)error;
  int i = f->report_count++ & 7; f->reports[i] = *report;
  if (report->is_string) { snprintf(f->report_text[i], 16, "%s", report->text); f->reports[i].text = f->report_text[i]; }
  return 1;
}
static int p_poll(void *ctx, const char **error) { (void)error; event(ctx, "poll"); return 1; }
static int p_logout(void *ctx, qa_ranking_match match, qa_ranking_account account, const char **error) {
  rank_fixture *f = ctx; (void)match; (void)account;
  if (f->logout_fails) { event(f, "logout-failed"); *error = "Provider unavailable"; return 0; }
  event(f, "logout"); return 1;
}
static int p_finish(void *ctx, qa_ranking_match match, const char **error) { (void)match; (void)error; event(ctx, "finish"); return 1; }
static void p_changed(void *ctx, double slot, qa_ranking_player_state state) {
  rank_fixture *f = ctx; CHECK(f->sentinel == SENTINEL);
  snprintf(f->changed[f->changed_count++ & 15], 24, "%g:%s", slot, state.kind);
}
static qa_ranking_provider provider_for(rank_fixture *f) {
  qa_ranking_provider p = {f, "https://rankings.invalid", p_begin, p_login, p_join, p_report, p_poll, p_logout, p_finish};
  return p;
}
static void rank_init(rank_fixture *f) { memset(f, 0, sizeof *f); f->sentinel = SENTINEL; }
static void test_ranking_lifecycle(void) {
  rank_fixture f; rank_init(&f);
  qa_ranking_provider provider = provider_for(&f);
  qa_ranking_lifecycle *owner = qa_ranking_lifecycle_create(&provider, p_changed, &f, NULL, NULL);
  qa_ranking_lifecycle_begin(owner, true, false, "source-game-key");
  qa_ranking_account_request login = {"login", "user", "not-stored", NULL};
  qa_ranking_lifecycle_account(owner, 0, &login);
  qa_ranking_lifecycle_report_int(owner, 0, -1, 7, 3, true);
  qa_ranking_lifecycle_report_string(owner, -1, 0, 8, "map");
  qa_ranking_lifecycle_report_int(owner, 1, -1, 7, 99, true);
  qa_ranking_lifecycle_frame(owner); qa_ranking_lifecycle_end(owner);
  static const char *expected[] = {"begin:source-game-key", "login", "join", "poll", "logout", "finish"};
  CHECK(f.event_count == 6);
  for (int i = 0; i < 6 && i < f.event_count; i++) CHECK(strcmp(f.events[i], expected[i]) == 0);
  CHECK(f.report_count == 2);
  CHECK(!f.reports[0].is_string && f.reports[0].self_id == 81 && f.reports[0].other_id == 0 && f.reports[0].key == 7 && f.reports[0].value == 3 && f.reports[0].accumulate);
  CHECK(f.reports[1].is_string && f.reports[1].self_id == 0 && f.reports[1].other_id == 81 && f.reports[1].key == 8 && strcmp(f.reports[1].text, "map") == 0);
  CHECK(f.changed_count == 3 && strcmp(f.changed[0], "0:pending") == 0 && strcmp(f.changed[1], "0:active") == 0 && strcmp(f.changed[2], "0:new") == 0);
  CHECK(strcmp(qa_ranking_lifecycle_get_state(owner).kind, "disabled") == 0);
  qa_ranking_lifecycle_free(owner);

  qa_ranking_lifecycle *unavailable = qa_ranking_lifecycle_create(NULL, p_changed, &f, NULL, NULL);
  qa_ranking_lifecycle_begin(unavailable, true, false, "source-game-key");
  CHECK(strcmp(qa_ranking_lifecycle_get_state(unavailable).kind, "unavailable") == 0);
  qa_ranking_lifecycle_free(unavailable);
  rank_fixture g; rank_init(&g); provider = provider_for(&g);
  owner = qa_ranking_lifecycle_create(&provider, p_changed, &g, NULL, NULL);
  qa_ranking_lifecycle_begin(owner, true, true, "source-game-key");
  CHECK(strcmp(qa_ranking_lifecycle_get_state(owner).kind, "disabled") == 0 && g.event_count == 0);
  qa_ranking_lifecycle_free(owner);
}
static void test_ranking_denied_and_cleanup(void) {
  rank_fixture f; rank_init(&f);
  qa_ranking_provider provider = provider_for(&f);
  qa_ranking_lifecycle *owner = qa_ranking_lifecycle_create(&provider, p_changed, &f, NULL, NULL);
  qa_ranking_lifecycle_begin(owner, true, false, "key");
  qa_ranking_account_request create = {"create", "denied", "not-stored", "unused@example.invalid"};
  qa_ranking_lifecycle_account(owner, 0, &create);
  CHECK(strcmp(qa_ranking_lifecycle_player(owner, 0).kind, "denied") == 0);
  qa_ranking_lifecycle_reset(owner, 0);
  CHECK(strcmp(qa_ranking_lifecycle_player(owner, 0).kind, "new") == 0);
  qa_ranking_account_request login = {"login", "user", "not-stored", NULL};
  qa_ranking_lifecycle_account(owner, 0, &login);
  qa_ranking_lifecycle_spectate(owner, 0);
  CHECK(strcmp(qa_ranking_lifecycle_player(owner, 0).kind, "spectator") == 0);
  qa_ranking_lifecycle_disconnect(owner, 0); qa_ranking_lifecycle_end(owner);
  int logouts = 0; for (int i = 0; i < f.event_count; i++) if (strcmp(f.events[i], "logout") == 0) logouts++;
  CHECK(logouts == 1);
  qa_ranking_lifecycle_free(owner);
}
static void end_body(void *ctx) { qa_ranking_lifecycle_end(ctx); }
static void test_ranking_provider_failure(void) {
  rank_fixture f; rank_init(&f); f.logout_fails = true;
  qa_ranking_provider provider = provider_for(&f);
  qa_ranking_lifecycle *owner = qa_ranking_lifecycle_create(&provider, p_changed, &f, NULL, NULL);
  qa_ranking_lifecycle_begin(owner, true, false, "key");
  qa_ranking_account_request login = {"login", "user", "not-stored", NULL};
  qa_ranking_lifecycle_account(owner, 0, &login);
  qa_caught_error caught;
  CHECK(!qa_try(end_body, owner, &caught));
  CHECK(strcmp(caught.name, "AggregateError") == 0 && strcmp(caught.message, "Ranking report submission or cleanup failed") == 0);
  CHECK(strcmp(f.events[(f.event_count - 1) & 15], "finish") == 0);
  CHECK(strcmp(qa_ranking_lifecycle_get_state(owner).kind, "unavailable") == 0);
  CHECK(strcmp(qa_ranking_lifecycle_player(owner, 0).kind, "new") == 0);
  qa_ranking_lifecycle_free(owner);
}

/* Ranking codec: TS keeps only the encoder (the decoders read client text the provider never
 * carries). Expected bytes from bun rankEncodeGameId. */
static int rank_prints;
static void quiet(void *ctx, const char *text) {
  CHECK(ctx == SENTINEL);
  CHECK(strcmp(text, "SV_RankEncodeGameID: result buffer too small\n") == 0);
  rank_prints++;
}
static void test_rank_codec(void) {
  uint8_t encoded[16];
  memset(encoded, 0, sizeof encoded);
  qa_rank_encode_game_id(0x0123456789abcdefULL, encoded, sizeof encoded, quiet, SENTINEL);
  static const uint8_t expected[16] = {88, 89, 83, 72, 121, 109, 116, 53, 56, 77, 52, 0, 0, 0, 0, 0};
  CHECK(memcmp(encoded, expected, sizeof expected) == 0 && rank_prints == 0);
  uint8_t small[4] = {9, 9, 9, 9};
  qa_rank_encode_game_id(1, small, sizeof small, quiet, SENTINEL);
  CHECK(rank_prints == 1 && small[0] == 0 && small[1] == 9);
}

/* network.test.ts "local account persistence and ranking reports are usable", plus the
 * JSON restore boundary for malformed saves. */
struct restore_text { void *target; const char *text; bool rankings; };
static void restore_text_body(void *ctx) {
  struct restore_text *r = ctx;
  if (r->rankings) qa_local_ranking_service_restore(r->target, r->text);
  else qa_local_authorization_restore(r->target, r->text);
}
static void test_online(void) {
  qa_local_authorization *accounts = qa_local_authorization_create();
  qa_online_account account; qa_local_authorization_register(accounts, "player", "secret", &account);
  char *saved = qa_local_authorization_save(accounts);
  qa_local_authorization *restored = qa_local_authorization_create();
  qa_local_authorization_restore(restored, saved);
  qa_authorization denied; qa_local_authorization_authorize(restored, "player", "wrong", &denied);
  CHECK(strcmp(denied.kind, "denied") == 0); qa_authorization_destroy(&denied);
  qa_authorization authorized; qa_local_authorization_authorize(restored, "player", "secret", &authorized);
  CHECK(strcmp(authorized.kind, "authorized") == 0);
  qa_online_account found;
  CHECK(qa_local_authorization_get_account(restored, authorized.token, &found) && strcmp(found.id, account.id) == 0 && strcmp(found.name, "player") == 0);
  qa_online_account_destroy(&found);
  qa_local_authorization_revoke(restored, authorized.token);
  CHECK(!qa_local_authorization_get_account(restored, authorized.token, &found));
  qa_authorization_destroy(&authorized);
  static const struct { const char *text, *message; } bad[] = {
    {"[]", "Invalid local account save"}, {"{\"version\":2,\"accounts\":[]}", "Invalid local account save"},
    {"{\"version\":1,\"accounts\":[7]}", "Invalid account record"},
    {"{\"version\":1,\"accounts\":[{\"account\":{\"id\":\"account:00\",\"name\":\"x\"},\"salt\":\"\",\"passwordHash\":\"\"}]}", "Invalid account fields"},
    /* A "version" key nested in a string is not the save's version. */
    {"{\"note\":\"\\\"version\\\":1\",\"accounts\":[]}", "Invalid local account save"},
  };
  qa_caught_error caught;
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
    struct restore_text r = {restored, bad[i].text, false};
    CHECK(!qa_try(restore_text_body, &r, &caught) && strcmp(caught.message, bad[i].message) == 0 && strcmp(caught.name, "Error") == 0);
  }
  qa_authorization still; qa_local_authorization_authorize(restored, "player", "secret", &still);
  CHECK(strcmp(still.kind, "authorized") == 0); qa_authorization_destroy(&still);

  qa_local_ranking_service *rankings = qa_local_ranking_service_create();
  qa_stat_entry frags = {"frags", 12};
  qa_ranking_report_player player = {account.id, 12, true, &frags, 1};
  qa_online_ranking_report report = {"one", "q3:ffa", &player, 1};
  CHECK(qa_local_ranking_service_submit(rankings, &report)); CHECK(!qa_local_ranking_service_submit(rankings, &report));
  size_t count = 0; qa_ranking_entry *standings = qa_local_ranking_service_standings(rankings, "q3:ffa", &count);
  CHECK(count == 1 && standings[0].matches == 1 && standings[0].wins == 1 && standings[0].score == 12);
  char *ranks = qa_local_ranking_service_save(rankings);
  qa_local_ranking_service *restored_ranks = qa_local_ranking_service_create();
  qa_local_ranking_service_restore(restored_ranks, ranks);
  size_t count2 = 0; qa_ranking_entry *again = qa_local_ranking_service_standings(restored_ranks, "q3:ffa", &count2);
  CHECK(count2 == 1 && strcmp(again[0].account, standings[0].account) == 0 && again[0].score == 12 && again[0].statistic_count == 1
    && strcmp(again[0].statistics[0].key, "frags") == 0 && again[0].statistics[0].value == 12);
  qa_ranking_entries_destroy(standings, count); qa_ranking_entries_destroy(again, count2);
  struct restore_text rr = {restored_ranks, "[{\"match\":\"m\",\"rules\":\"nocolon\",\"players\":[]}]", true};
  CHECK(!qa_try(restore_text_body, &rr, &caught) && strcmp(caught.message, "Invalid ranking report") == 0);
  rr.text = "[{\"match\":\"m\",\"rules\":\"q3:x\",\"players\":[{\"account\":\"bad\",\"score\":1,\"won\":true,\"statistics\":[]}]}]";
  CHECK(!qa_try(restore_text_body, &rr, &caught) && strcmp(caught.message, "Invalid ranked player") == 0);
  rr.text = "[{\"match\":\"m\",\"rules\":\"q3:x\",\"players\":[]},{\"match\":\"m\",\"rules\":\"q3:x\",\"players\":[]}]";
  CHECK(!qa_try(restore_text_body, &rr, &caught) && strcmp(caught.message, "Duplicate saved match") == 0);
  rr.text = "[{";
  CHECK(!qa_try(restore_text_body, &rr, &caught));
  free(ranks); free(saved);
  qa_online_account_destroy(&account);
  qa_local_ranking_service_free(rankings); qa_local_ranking_service_free(restored_ranks);
  qa_local_authorization_free(accounts); qa_local_authorization_free(restored);
}

/* download-types.ts */
static void test_download_types(void) {
  CHECK(qa_is_served_download_type("maps/q2dm1.bsp")); CHECK(qa_is_served_download_type("sound/X.WAV"));
  CHECK(qa_is_served_download_type("pics/a.JPEG")); CHECK(qa_is_served_download_type("demos/one.dm2"));
  CHECK(!qa_is_served_download_type("server.cfg")); CHECK(!qa_is_served_download_type("autoexec.cfg"));
  CHECK(!qa_is_served_download_type("pak0.pak")); CHECK(!qa_is_served_download_type("bsp"));
  CHECK(!qa_is_served_download_type("maps/x.bsp.cfg")); CHECK(!qa_is_served_download_type("maps/xbsp"));
  CHECK(qa_is_served_download_type(".txt"));
}

int main(void) {
  int64_t a[4] = {127, 0, 0, 1}, b[4] = {127, 0, 0, 2};
  qa_net_make_ipv4(&address, a, 27960, false); qa_net_make_ipv4(&other, b, 27960, false);
  test_lan_and_cache();
  test_owner_requests();
  test_info_status_order();
  test_wrong_replies();
  test_expiry_cancel_release();
  test_menu_queries();
  test_broadcast_and_bound();
  test_failed_sends();
  test_favorites();
  test_heartbeat();
  test_ranking_lifecycle();
  test_ranking_denied_and_cleanup();
  test_ranking_provider_failure();
  test_rank_codec();
  test_online();
  test_download_types();
  printf("test_network_services: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
