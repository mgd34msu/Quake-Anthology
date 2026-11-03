/*
 * Ports of unit-layer cases from ../quake-anthology-ts/tests/network/q3/:
 * authorization.test.ts, client-authorization.test.ts (owner cases),
 * recording.test.ts, and network.test.ts (loopback wire exchange, compressed
 * connect admission, wire-ordered server execution, retired packet stop,
 * configstring overflow drop, deferred admission decision). TS async gates
 * become synchronous callbacks that perform the same mutation at the point the
 * TS test resumes. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/core/q3_cd_key.h"
#include "qa/foundation/support.h"
#include "qa/network/q3.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
/* Every fixture starts with this sentinel; callbacks check the ctx they receive. */
#define MAGIC 0x51a3c7e9u
#define FIX(type, ctx) ((type *)sentinel((ctx)))
static void *sentinel(void *ctx) {
  CHECK(ctx != NULL && *(unsigned *)ctx == MAGIC);
  return ctx;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = malloc(n);
  memcpy(out, s, n);
  return out;
}

static qa_q3_address ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, int64_t port) {
  qa_q3_address out;
  memset(&out, 0, sizeof out);
  out.kind = QA_Q3_ADDR_IPV4;
  out.host[0] = a; out.host[1] = b; out.host[2] = c; out.host[3] = d;
  out.port = port;
  return out;
}
static uint8_t *text_packet(const char *text, size_t *len) {
  uint8_t *bytes = NULL;
  CHECK(qa_q3_encode_connectionless_text(text, &bytes, len));
  return bytes;
}

/* ---- datagram capture shared by admission and authorization fixtures ---- */
typedef struct sent_packet { qa_q3_address to; uint8_t *payload; size_t len; } sent_packet;
typedef struct outbox { sent_packet packets[32]; size_t count; } outbox;
static void outbox_push(outbox *box, const qa_q3_address *to, const uint8_t *payload, size_t len) {
  if (box->count >= 32) { CHECK(!"outbox overflow"); return; }
  sent_packet *p = &box->packets[box->count++];
  CHECK(qa_q3_mirror_address_copy(&p->to, to));
  p->payload = malloc(len ? len : 1);
  memcpy(p->payload, payload, len);
  p->len = len;
}
static void outbox_clear(outbox *box) {
  for (size_t i = 0; i < box->count; i++) { qa_q3_mirror_address_destroy(&box->packets[i].to); free(box->packets[i].payload); }
  box->count = 0;
}

/* ================= authorization.test.ts ================= */
typedef struct server_auth_fixture {
  unsigned magic;
  int lookups, prints;
  bool enabled, fail_on_use;
  qa_q3_address authority;
  qa_q3_challenge *mutate; /* the challenge the pending lookup observes changing */
  outbox sent;
  const char *game;
} server_auth_fixture;
static bool sa_resolve(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  server_auth_fixture *f = FIX(server_auth_fixture, ctx);
  (void)err; (void)err_len;
  CHECK(!f->fail_on_use);
  f->lookups++;
  if (f->mutate) f->mutate->challenge = 18; /* TS: challenge.challenge = 18 while the lookup is pending */
  return qa_q3_mirror_address_copy(out, &f->authority);
}
static bool sa_enabled(void *ctx) { return FIX(server_auth_fixture, ctx)->enabled; }
static const char *sa_game(void *ctx) { return FIX(server_auth_fixture, ctx)->game; }
static const char *sa_strict(void *ctx) { sentinel(ctx); return "1"; }
static void sa_send(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t len) {
  server_auth_fixture *f = FIX(server_auth_fixture, ctx);
  CHECK(!f->fail_on_use);
  outbox_push(&f->sent, to, packet, len);
}
static void sa_print(void *ctx, const char *text) {
  server_auth_fixture *f = FIX(server_auth_fixture, ctx);
  (void)text;
  CHECK(!f->fail_on_use);
  f->prints++;
}
static qa_q3_server_authorization *server_authorization(server_auth_fixture *f) {
  qa_q3_server_authorization_options o;
  memset(&o, 0, sizeof o);
  o.resolve = sa_resolve; o.enabled = sa_enabled; o.game_directory = sa_game; o.strict_auth = sa_strict;
  o.send = sa_send; o.print = sa_print; o.print_ctx = f; o.ctx = f;
  qa_q3_server_authorization *a = NULL;
  CHECK(qa_q3_server_authorization_init(&a, &o));
  return a;
}

static void test_authorization_ipx(void) {
  server_auth_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.enabled = true; f.fail_on_use = true; f.game = "";
  qa_q3_server_authorization *owner = server_authorization(&f);
  qa_q3_challenge challenge;
  memset(&challenge, 0, sizeof challenge);
  challenge.has_address = true;
  challenge.address.kind = QA_Q3_ADDR_IPX;
  challenge.address.network = 1;
  for (int i = 0; i < 6; i++) challenge.address.node[i] = (uint8_t)(i + 1);
  challenge.address.port = 27960;
  challenge.challenge = 17;
  qa_q3_server_authorization_request(owner, &challenge);
  qa_q3_mirror_ipv4_address address;
  CHECK(!qa_q3_server_authorization_get_address(owner, &address));
  CHECK(f.lookups == 0 && f.sent.count == 0 && f.prints == 0);
  qa_q3_server_authorization_destroy(owner);
}

/* Admission fixture used by the WAN challenge exchange. */
typedef struct admission_fixture {
  unsigned magic;
  qa_q3_server_authorization *authorization;
  outbox sent;
  bool enabled, lan, close_on_admit;
  int admits;
  qa_q3_accepted_connect admitted;
  bool has_admitted;
  qa_q3_admission_slot slot;
  size_t slot_count;
  double random;
} admission_fixture;
static bool af_enabled(void *ctx) { return FIX(admission_fixture, ctx)->enabled; }
static const qa_q3_admission_slot *af_slots(void *ctx, size_t *count) {
  admission_fixture *f = FIX(admission_fixture, ctx);
  *count = f->slot_count;
  return &f->slot;
}
static int64_t af_private_clients(void *ctx) { sentinel(ctx); return 0; }
static const char *af_private_password(void *ctx) { sentinel(ctx); return ""; }
static double af_reconnect(void *ctx) { sentinel(ctx); return 3; }
static double af_zero(void *ctx) { sentinel(ctx); return 0; }
static bool af_authorize_address(void *ctx, qa_q3_address *out) {
  admission_fixture *f = FIX(admission_fixture, ctx);
  return f->authorization && qa_q3_server_authorization_get_address(f->authorization, out);
}
static bool af_false(void *ctx) { sentinel(ctx); return false; }
static bool af_is_lan(void *ctx, const qa_q3_address *address) { (void)address; return FIX(admission_fixture, ctx)->lan; }
static double af_random(void *ctx) { return FIX(admission_fixture, ctx)->random; }
static void af_authorize(void *ctx, const qa_q3_challenge *challenge) {
  admission_fixture *f = FIX(admission_fixture, ctx);
  CHECK(f->authorization != NULL); /* "LAN should not authorize" */
  if (f->authorization) qa_q3_server_authorization_request(f->authorization, challenge);
}
static void af_send(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t len) { outbox_push(&FIX(admission_fixture, ctx)->sent, to, packet, len); }
static char *af_admit(void *ctx, const qa_q3_accepted_connect *connection) {
  admission_fixture *f = FIX(admission_fixture, ctx);
  f->admits++;
  if (!f->has_admitted) {
    f->has_admitted = true;
    f->admitted = *connection;
    qa_q3_mirror_address_copy(&f->admitted.address, &connection->address);
    f->admitted.userinfo = copy_text(connection->userinfo ? connection->userinfo : "");
  }
  if (f->close_on_admit) f->enabled = false; /* TS: owner closes while the decision is awaited */
  return NULL;
}
static void af_drop_bot(void *ctx, int64_t slot) { sentinel(ctx); (void)slot; }
static void af_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void af_query(void *ctx, const qa_q3_address *from, qa_q3_connectionless_packet *packet) { sentinel(ctx); (void)from; (void)packet; }
static qa_q3_server_admission *admission(admission_fixture *f) {
  qa_q3_server_admission_bindings b;
  memset(&b, 0, sizeof b);
  b.enabled = af_enabled; b.slots = af_slots; b.private_clients = af_private_clients; b.private_password = af_private_password;
  b.reconnect_limit_seconds = af_reconnect; b.minimum_ping = af_zero; b.maximum_ping = af_zero;
  b.authorize_address = af_authorize_address; b.demo_restricted = af_false; b.is_lan = af_is_lan; b.random = af_random;
  b.authorize = af_authorize; b.send = af_send; b.admit = af_admit; b.drop_bot = af_drop_bot; b.print = af_print;
  b.query = af_query; b.ctx = f; b.print_ctx = f;
  qa_q3_server_admission *a = NULL;
  CHECK(qa_q3_server_admission_init(&a, &b));
  return a;
}
static void admission_fixture_destroy(admission_fixture *f) {
  outbox_clear(&f->sent);
  if (f->has_admitted) { qa_q3_mirror_address_destroy(&f->admitted.address); free(f->admitted.userinfo); }
}

static void test_authorization_wan(void) {
  server_auth_fixture auth;
  memset(&auth, 0, sizeof auth); auth.magic = MAGIC;
  auth.enabled = true; auth.game = "missionpack"; auth.authority = ipv4(192, 0, 2, 1, 27952);
  admission_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.enabled = true; f.random = 123;
  f.authorization = server_authorization(&auth);
  qa_q3_server_admission *server = admission(&f);
  qa_q3_address client = ipv4(198, 51, 100, 2, 30000);
  size_t len = 0;
  uint8_t *packet = text_packet("getchallenge", &len);
  qa_q3_server_admission_receive(server, &client, packet, len, 100);
  free(packet);
  CHECK(auth.sent.count == 1);
  CHECK(auth.sent.count == 1 && qa_q3_mirror_same_address(&auth.sent.packets[0].to, &auth.authority, true));
  char nonce[64] = "";
  if (auth.sent.count == 1) {
    qa_q3_connectionless_packet decoded;
    CHECK(qa_q3_decode_connectionless(auth.sent.packets[0].payload, auth.sent.packets[0].len, QA_Q3_CONNLESS_SERVER, NULL, &decoded));
    CHECK(strcmp(decoded.command, "getIpAuthorize") == 0);
    CHECK(decoded.argument_count == 5);
    if (decoded.argument_count == 5) {
      CHECK(strcmp(decoded.arguments[1], "198.51.100.2") == 0);
      CHECK(strcmp(decoded.arguments[2], "missionpack") == 0);
      CHECK(strcmp(decoded.arguments[3], "0") == 0);
      CHECK(strcmp(decoded.arguments[4], "1") == 0);
      snprintf(nonce, sizeof nonce, "%s", decoded.arguments[0]);
    }
    qa_q3_connectionless_packet_destroy(&decoded);
  }
  char text[128];
  snprintf(text, sizeof text, "ipAuthorize %s accept", nonce);
  packet = text_packet(text, &len);
  qa_q3_server_admission_receive(server, &client, packet, len, 110); /* forged: from the client */
  CHECK(f.sent.count == 0 && auth.sent.count == 1);
  qa_q3_server_admission_receive(server, &auth.authority, packet, len, 120);
  free(packet);
  CHECK(f.sent.count == 1);
  if (f.sent.count == 1) {
    CHECK(qa_q3_mirror_same_address(&f.sent.packets[0].to, &client, true));
    qa_q3_connectionless_packet decoded;
    CHECK(qa_q3_decode_connectionless(f.sent.packets[0].payload, f.sent.packets[0].len, QA_Q3_CONNLESS_CLIENT, NULL, &decoded));
    CHECK(strcmp(decoded.command, "challengeResponse") == 0);
    qa_q3_connectionless_packet_destroy(&decoded);
  }
  packet = text_packet("getchallenge", &len);
  qa_q3_server_admission_receive(server, &client, packet, len, 130);
  free(packet);
  CHECK(auth.lookups == 1);
  qa_q3_server_admission_destroy(server);
  qa_q3_server_authorization_destroy(f.authorization);
  admission_fixture_destroy(&f);
  outbox_clear(&auth.sent);
}

static void test_authorization_retired_lookup(void) {
  server_auth_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.enabled = true; f.game = ""; f.authority = ipv4(192, 0, 2, 1, 27952);
  qa_q3_server_authorization *owner = server_authorization(&f);
  qa_q3_challenge challenge;
  memset(&challenge, 0, sizeof challenge);
  challenge.has_address = true;
  challenge.address = ipv4(198, 51, 100, 2, 30000);
  challenge.challenge = 17;
  f.mutate = &challenge;
  qa_q3_server_authorization_request(owner, &challenge);
  CHECK(f.sent.count == 0);
  f.mutate = NULL;
  f.enabled = false;
  qa_q3_server_authorization_request(owner, &challenge);
  CHECK(f.sent.count == 0);
  qa_q3_server_authorization_destroy(owner);
  outbox_clear(&f.sent);
}

/* ================= client-authorization.test.ts ================= */
static const char FIXTURE_BASE[] = "2222222222222222";
typedef struct client_auth_fixture {
  unsigned magic;
  bool demo, current, retire_on_lookup;
  int lookups, attempts, sent, null_until;
  qa_q3_address authority;
  uint8_t packets[4][128]; size_t packet_len[4];
} client_auth_fixture;
static bool ca_demo(void *ctx) { return FIX(client_auth_fixture, ctx)->demo; }
static bool ca_resolve(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  client_auth_fixture *f = FIX(client_auth_fixture, ctx);
  (void)err; (void)err_len;
  f->lookups++;
  f->attempts++;
  if (f->retire_on_lookup) f->current = false; /* the owner retires while the lookup is pending */
  if (f->attempts <= f->null_until) return false;
  return qa_q3_mirror_address_copy(out, &f->authority);
}
static void ca_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void ca_assert(void *ctx) {
  client_auth_fixture *f = FIX(client_auth_fixture, ctx);
  if (!f->current) qa_raise("Error", 0, 0, "Retired fixture");
}
static void ca_send(void *ctx, const qa_q3_mirror_ipv4_address *address, const uint8_t *packet, size_t len) {
  client_auth_fixture *f = FIX(client_auth_fixture, ctx);
  CHECK(qa_q3_mirror_same_address(address, &f->authority, true));
  if (f->sent < 4 && len <= 128) { memcpy(f->packets[f->sent], packet, len); f->packet_len[f->sent] = len; }
  f->sent++;
}
static qa_cvar_registry *make_cvars(qa_identity_owner *owner) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(cvars, name, value, QA_CVAR_FLAG_NONE, &snapshot));
  qa_cvar_read_free(&snapshot);
}
static qa_q3_client_authorization *client_authorization(client_auth_fixture *f, qa_cvar_registry *cvars, const qa_q3_cd_key_state *keys) {
  qa_q3_client_authorization_options o;
  memset(&o, 0, sizeof o);
  o.cvars = cvars; o.keys = keys; o.demo_restricted = ca_demo; o.resolve = ca_resolve;
  o.print = ca_print; o.print_ctx = f; o.ctx = f;
  qa_q3_client_authorization *a = NULL;
  CHECK(qa_q3_client_authorization_init(&a, &o));
  return a;
}
typedef struct request_scope { qa_q3_client_authorization *owner; client_auth_fixture *f; } request_scope;
static void request_body(void *ctx) {
  request_scope *scope = ctx;
  qa_q3_client_authorization_request(scope->owner, ca_assert, scope->f, ca_send, scope->f);
}

static void test_client_authorization_packet(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-key-test"));
  qa_cvar_registry *cvars = make_cvars(&identity);
  qa_q3_cd_key_state keys;
  qa_q3_cd_key_state_init(&keys, NULL, NULL, QA_Q3_KEY_BUILD_CLIENT);
  register_cvar(cvars, "cl_authorizeKey", "1");
  qa_q3_cd_key_state_write_ui(&keys, 0, "", (const uint8_t *)FIXTURE_BASE, 16);
  register_cvar(cvars, "cl_anonymous", "1");
  client_auth_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.current = true; f.authority = ipv4(192, 0, 2, 1, 27952);
  qa_q3_client_authorization *owner = client_authorization(&f, cvars, &keys);
  qa_q3_client_authorization_request(owner, ca_assert, &f, ca_send, &f);
  CHECK(f.sent == 1);
  if (f.sent >= 1) {
    qa_q3_connectionless_packet decoded;
    CHECK(qa_q3_decode_connectionless(f.packets[0], f.packet_len[0], QA_Q3_CONNLESS_SERVER, NULL, &decoded));
    CHECK(strcmp(decoded.command, "getKeyAuthorize") == 0);
    CHECK(decoded.argument_count >= 2 && strcmp(decoded.arguments[0], "1") == 0);
    CHECK(decoded.argument_count >= 2 && strcmp(decoded.arguments[1], FIXTURE_BASE) == 0);
    qa_q3_connectionless_packet_destroy(&decoded);
  }
  f.demo = true;
  qa_q3_client_authorization_request(owner, ca_assert, &f, ca_send, &f);
  CHECK(f.sent == 2);
  if (f.sent >= 2) {
    qa_q3_connectionless_packet decoded;
    CHECK(qa_q3_decode_connectionless(f.packets[1], f.packet_len[1], QA_Q3_CONNLESS_SERVER, NULL, &decoded));
    CHECK(decoded.argument_count >= 2 && strcmp(decoded.arguments[1], "demota") == 0);
    qa_q3_connectionless_packet_destroy(&decoded);
  }
  CHECK(f.lookups == 1);
  qa_q3_client_authorization_destroy(owner);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

static void test_client_authorization_retired_and_retry(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-key-test"));
  qa_cvar_registry *cvars = make_cvars(&identity);
  qa_q3_cd_key_state keys;
  qa_q3_cd_key_state_init(&keys, NULL, NULL, QA_Q3_KEY_BUILD_CLIENT);
  register_cvar(cvars, "cl_authorizeKey", "1");
  client_auth_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.current = true; f.retire_on_lookup = true; f.authority = ipv4(192, 0, 2, 1, 27952);
  qa_q3_client_authorization *owner = client_authorization(&f, cvars, &keys);
  request_scope scope = { owner, &f };
  qa_caught_error caught;
  CHECK(!qa_try(request_body, &scope, &caught));
  CHECK(strcmp(caught.message, "Retired fixture") == 0);
  CHECK(f.sent == 0);
  qa_q3_client_authorization_destroy(owner);
  client_auth_fixture retry;
  memset(&retry, 0, sizeof retry); retry.magic = MAGIC;
  retry.current = true; retry.null_until = 1; retry.authority = ipv4(192, 0, 2, 1, 27952);
  owner = client_authorization(&retry, cvars, &keys);
  qa_q3_client_authorization_request(owner, ca_assert, &retry, ca_send, &retry);
  CHECK(retry.sent == 0);
  qa_q3_client_authorization_request(owner, ca_assert, &retry, ca_send, &retry);
  CHECK(retry.sent == 1);
  CHECK(retry.attempts == 2);
  qa_q3_client_authorization_destroy(owner);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

static void test_client_authorization_opt_in(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-key-test"));
  qa_cvar_registry *cvars = make_cvars(&identity);
  qa_q3_cd_key_state keys;
  qa_q3_cd_key_state_init(&keys, NULL, NULL, QA_Q3_KEY_BUILD_CLIENT);
  client_auth_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.current = true; f.authority = ipv4(192, 0, 2, 1, 27952);
  qa_q3_client_authorization *owner = client_authorization(&f, cvars, &keys);
  qa_q3_client_authorization_request(owner, ca_assert, &f, ca_send, &f);
  CHECK(f.sent == 0);
  CHECK(f.lookups == 0);
  qa_q3_client_authorization_destroy(owner);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

/* Key-owner case: the core test (tests/core/test_core_leaves.c) covers storage and
 * selection; the non-ASCII key rejections are added here. */
static void test_cd_key_non_ascii(void) {
  CHECK(!qa_validate_q3_cd_key("\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f"
                               "\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f\xc3\x9f", NULL));
  CHECK(!qa_validate_q3_cd_key("\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c"
                               "\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c"
                               "\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c", NULL));
  CHECK(!qa_validate_q3_cd_key("IIIIIIIIIIIIIIII", NULL));
}

/* ================= client bindings shared by recording and loopback ================= */
typedef struct client_fixture {
  unsigned magic;
  int gamestates;
  struct { int64_t delta; double origin_x, origin_y, origin_z; size_t entity_count; double entity_base_x; } snapshots[8];
  size_t snapshot_count;
} client_fixture;
static void cf_thunk(void *ctx) { sentinel(ctx); }
static void cf_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void cf_system_info(void *ctx, const char *info) { sentinel(ctx); (void)info; }
static void cf_gamestate(void *ctx, const qa_q3_gamestate *state, int64_t generation) {
  (void)state; (void)generation;
  FIX(client_fixture, ctx)->gamestates++;
}
static void cf_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, int64_t ping) {
  client_fixture *f = FIX(client_fixture, ctx);
  (void)ping;
  if (f->snapshot_count >= 8) return;
  f->snapshots[f->snapshot_count].delta = snapshot->delta_number;
  f->snapshots[f->snapshot_count].origin_x = snapshot->player_state.origin.x;
  f->snapshots[f->snapshot_count].origin_y = snapshot->player_state.origin.y;
  f->snapshots[f->snapshot_count].origin_z = snapshot->player_state.origin.z;
  f->snapshots[f->snapshot_count].entity_count = snapshot->entity_count;
  f->snapshots[f->snapshot_count].entity_base_x = snapshot->entity_count ? snapshot->entities[0].pos.base.x : 0;
  f->snapshot_count++;
}
static int64_t cf_download_size(void *ctx, int64_t size) { sentinel(ctx); return size; }
static void cf_download(void *ctx, const qa_q3_download_block *block) { sentinel(ctx); (void)block; CHECK(!"unexpected download"); }
static bool cf_local(void *ctx) { sentinel(ctx); return true; }
static qa_q3_client_bindings client_bindings(client_fixture *f) {
  qa_q3_client_bindings b;
  memset(&b, 0, sizeof b);
  b.assert_current = cf_thunk; b.print = cf_print; b.print_ctx = f; b.clear_active = cf_thunk;
  b.system_info = cf_system_info; b.gamestate = cf_gamestate; b.snapshot = cf_snapshot;
  b.download_size = cf_download_size; b.download = cf_download; b.map_restart = cf_thunk; b.level_shot = cf_thunk;
  b.local_server_running = cf_local; b.ctx = f;
  return b;
}

/* ================= recording.test.ts ================= */
static void test_recording_seed(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "recording"));
  client_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  qa_q3_client_bindings bindings = client_bindings(&f);
  qa_q3_connection_identity who = { qa_identity_owner_client(&identity, 2, 0), true, qa_identity_owner_seat(&identity, 2) };
  qa_q3_client_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = QA_Q3_CLIENT_MODE_NETWORK; mode.challenge = 9; mode.qport = 195;
  qa_q3_client_connection *source = NULL;
  CHECK(qa_q3_client_connection_init(&source, who, QA_Q3_PRODUCT_MISSIONPACK, &mode, &bindings));
  qa_q3_gamestate_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.kind = QA_Q3_GS_CONFIGSTRING; entry.index = 0; entry.value = (char *)"\\mapname\\mpteam1";
  qa_q3_server_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_Q3_OP_GAMESTATE;
  op.gamestate.command_sequence = 8; op.gamestate.client_number = 2; op.gamestate.checksum_feed = 123;
  op.gamestate.entries = &entry; op.gamestate.entry_count = 1;
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = QA_Q3_PRODUCT_MISSIONPACK; context.message_number = 20; context.server_command_sequence = 8;
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_server_message(0, &op, 1, &context, NULL, &bytes, &len));
  qa_q3_server_message message;
  CHECK(qa_q3_client_connection_receive_message(source, 20, bytes, len, 500, &message));
  qa_q3_server_message_destroy(&message);
  free(bytes);
  qa_q3_client_connection_set_demo_waiting(source, false);
  int64_t sequence = 0; uint8_t *seed = NULL; size_t seed_len = 0;
  CHECK(qa_q3_demo_gamestate(source, &sequence, &seed, &seed_len));
  CHECK(sequence == 19);
  CHECK(qa_q3_client_connection_server_message_sequence(source) == 20);
  CHECK(!qa_q3_client_connection_demo_waiting(source));
  qa_q3_client_connection *replay = NULL;
  CHECK(qa_q3_client_connection_init(&replay, qa_q3_client_connection_identity(source), qa_q3_client_connection_get_product(source), &mode, &bindings));
  CHECK(qa_q3_client_connection_receive_message(replay, sequence, seed, seed_len, 0, &message));
  qa_q3_server_message_destroy(&message);
  free(seed);
  qa_q3_gamestate a, b;
  CHECK(qa_q3_client_connection_copy_gamestate(replay, &a));
  CHECK(qa_q3_client_connection_copy_gamestate(source, &b));
  CHECK(a.entry_count == b.entry_count && a.command_sequence == b.command_sequence
        && a.client_number == b.client_number && a.checksum_feed == b.checksum_feed);
  for (size_t i = 0; i < a.entry_count && i < b.entry_count; i++) {
    CHECK(a.entries[i].kind == b.entries[i].kind && a.entries[i].index == b.entries[i].index);
    CHECK(a.entries[i].value && b.entries[i].value && strcmp(a.entries[i].value, b.entries[i].value) == 0);
  }
  qa_q3_gamestate_destroy(&a);
  qa_q3_gamestate_destroy(&b);
  CHECK(qa_q3_client_connection_client_number(replay) == 2);
  CHECK(qa_q3_client_connection_server_command_sequence(replay) == 8);
  CHECK(qa_q3_client_connection_checksum_feed(replay) == 123);
  CHECK(f.gamestates == 2);
  qa_q3_client_connection_destroy(replay);
  qa_q3_client_connection_destroy(source);
  qa_identity_owner_destroy(&identity);
}

/* ================= network.test.ts: loopback wire exchange ================= */
typedef struct wire_queue { uint8_t *packets[64]; size_t lens[64]; size_t head, count; } wire_queue;
static void wq_push(wire_queue *q, const uint8_t *packet, size_t len) {
  if (q->count >= 64) { CHECK(!"wire queue overflow"); return; }
  size_t at = (q->head + q->count++) % 64;
  q->packets[at] = malloc(len ? len : 1);
  memcpy(q->packets[at], packet, len);
  q->lens[at] = len;
}
static bool wq_shift(wire_queue *q, uint8_t **packet, size_t *len) {
  if (q->count == 0) return false;
  *packet = q->packets[q->head]; *len = q->lens[q->head];
  q->head = (q->head + 1) % 64; q->count--;
  return true;
}
static void wq_clear(wire_queue *q) { uint8_t *p; size_t n; while (wq_shift(q, &p, &n)) free(p); }
typedef struct loop_fixture {
  unsigned magic;
  wire_queue to_client, to_server;
  double now;
  int64_t entered[8]; size_t entered_count;
  int64_t moves[8]; size_t move_count;
  char executed[8][128]; size_t executed_count;
  qa_q3_entity_state_record baseline;
  qa_q3_address server_address, client_address;
  int64_t server_id;
  /* ordered-wire fixtures */
  char calls[16][64]; size_t call_count;
  int retirement; /* 0 none, 1 peer, 2 world */
  qa_q3_server_connection *connection;
} loop_fixture;
static void lf_send_client(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t len) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  CHECK(qa_q3_mirror_same_address(to, &f->client_address, true));
  wq_push(&f->to_client, packet, len);
}
static void lf_send_server(void *ctx, const qa_q3_address *to, const uint8_t *packet, size_t len) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  CHECK(qa_q3_mirror_same_address(to, &f->server_address, true));
  wq_push(&f->to_server, packet, len);
}
static void lf_client_address(void *ctx, qa_q3_address *out) { *out = FIX(loop_fixture, ctx)->client_address; }
static void lf_server_address(void *ctx, qa_q3_address *out) { *out = FIX(loop_fixture, ctx)->server_address; }
static void lf_trace(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void sb_assert(void *ctx) { sentinel(ctx); }
static int64_t sb_server_id(void *ctx) { return FIX(loop_fixture, ctx)->server_id; }
static int64_t sb_checksum(void *ctx) { sentinel(ctx); return 123; }
static int64_t sb_zero(void *ctx) { sentinel(ctx); return 0; }
static bool sb_false(void *ctx) { sentinel(ctx); return false; }
static bool sb_true(void *ctx) { sentinel(ctx); return true; }
static double sb_time(void *ctx) { return FIX(loop_fixture, ctx)->now; }
static const char *sb_download_name(void *ctx) { sentinel(ctx); return ""; }
static bool sb_command(void *ctx, const qa_q3_reliable_command *command, bool client_ok) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  (void)client_ok;
  if (f->executed_count < 8) snprintf(f->executed[f->executed_count++], 128, "%.127s", command->text);
  return true;
}
static void sb_enter(void *ctx, const qa_q3_wire_user_command *command) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  if (f->entered_count < 8) f->entered[f->entered_count++] = command->server_time;
}
static void sb_think(void *ctx, const qa_q3_wire_user_command *command) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  if (f->move_count < 8) f->moves[f->move_count++] = command->server_time;
}
static void sb_resend(void *ctx) { sentinel(ctx); CHECK(!"Unexpected gamestate resend"); }
static void sb_drop(void *ctx, const char *reason) { sentinel(ctx); fprintf(stderr, "unexpected drop: %s\n", reason); CHECK(!"unexpected drop"); }
static void sb_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void lf_baseline(void *ctx, int64_t number, qa_q3_entity_state_record *out) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  if (number == 2) *out = f->baseline;
  else qa_q3_entity_state_record_init(out, 0);
}
static qa_q3_server_bindings loop_server_bindings(loop_fixture *f) {
  qa_q3_server_bindings b;
  memset(&b, 0, sizeof b);
  b.assert_current = sb_assert; b.server_id = sb_server_id; b.restarted_server_id = sb_server_id; b.checksum_feed = sb_checksum;
  b.pure = sb_false; b.debug_build = false; b.time = sb_time; b.client_running = sb_true; b.flood_protect = sb_true;
  b.download_name = sb_download_name; b.command = sb_command; b.enter_world = sb_enter; b.think = sb_think;
  b.resend_gamestate = sb_resend; b.drop = sb_drop; b.print = sb_print; b.print_ctx = f; b.ctx = f;
  return b;
}
static void read_client(loop_fixture *f, qa_q3_client_connection *client) {
  uint8_t *packet; size_t len;
  while (wq_shift(&f->to_client, &packet, &len)) {
    qa_q3_client_packet_result result;
    CHECK(qa_q3_client_connection_receive_datagram(client, packet, len, f->now, &result));
    qa_q3_client_packet_result_destroy(&result);
    free(packet);
  }
}
static void read_server(loop_fixture *f, qa_q3_server_connection *server) {
  uint8_t *packet; size_t len;
  while (wq_shift(&f->to_server, &packet, &len)) {
    qa_q3_channel_result result;
    CHECK(qa_q3_server_connection_receive_datagram(server, packet, len, &result));
    free(result.payload);
    free(packet);
  }
}
static qa_q3_wire_user_command wire_command(int32_t time, int32_t buttons, int32_t forward) {
  qa_q3_wire_user_command c;
  memset(&c, 0, sizeof c);
  c.server_time = time; c.angles[1] = 16384; c.buttons = buttons; c.weapon = 5; c.forwardmove = forward;
  return c;
}

static void test_loopback_exchange(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-wire-smoke"));
  qa_q3_connection_identity who = { qa_identity_owner_client(&identity, 1, 0), true, qa_identity_owner_seat(&identity, 1) };
  loop_fixture *f = calloc(1, sizeof *f); f->magic = MAGIC;
  f->now = 1000; f->server_id = 42;
  f->server_address = ipv4(127, 0, 0, 1, 27960); f->client_address = ipv4(127, 0, 0, 1, 31000);
  qa_q3_entity_state_record_init(&f->baseline, 0);
  f->baseline.number = 2; f->baseline.modelindex = 1;
  qa_q3_snapshot_entities *entities = NULL;
  CHECK(qa_q3_snapshot_entities_init(&entities, 2048));
  qa_q3_server_snapshot_history *snapshots = NULL;
  CHECK(qa_q3_server_snapshot_history_init(&snapshots, entities, QA_Q3_PRODUCT_MISSIONPACK, lf_baseline, f));
  qa_q3_server_bindings sb = loop_server_bindings(f);
  qa_q3_server_connection *server = NULL;
  CHECK(qa_q3_server_connection_init(&server, who, 51, 27961, snapshots, &sb));
  client_fixture cf;
  memset(&cf, 0, sizeof cf); cf.magic = MAGIC;
  qa_q3_client_bindings cb = client_bindings(&cf);
  qa_q3_client_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = QA_Q3_CLIENT_MODE_NETWORK; mode.challenge = 51; mode.qport = 27961;
  qa_q3_client_connection *client = NULL;
  CHECK(qa_q3_client_connection_init(&client, who, QA_Q3_PRODUCT_MISSIONPACK, &mode, &cb));
  qa_q3_channel_delivery_storage server_storage, client_storage;
  const qa_q3_channel_delivery *server_delivery = qa_q3_channel_delivery_create(&server_storage, lf_send_client, f,
      lf_client_address, f, qa_q3_server_connection_source_state(server), lf_trace, f);
  const qa_q3_channel_delivery *client_delivery = qa_q3_channel_delivery_create(&client_storage, lf_send_server, f,
      lf_server_address, f, qa_q3_client_connection_source_state(client), lf_trace, f);
  qa_q3_server_rate rate = { 25000, 0, 50, true, true, true };
  qa_q3_gamestate_entry gs_entries[2];
  memset(gs_entries, 0, sizeof gs_entries);
  gs_entries[0].kind = QA_Q3_GS_CONFIGSTRING; gs_entries[0].index = 1; gs_entries[0].value = (char *)"\\sv_serverid\\42";
  gs_entries[1].kind = QA_Q3_GS_BASELINE; gs_entries[1].number = 2; gs_entries[1].entity = f->baseline;
  qa_q3_gamestate state = { 0, gs_entries, 2, 1, 123 };
  qa_q3_server_connection_send_gamestate(server, &state, &rate, server_delivery);
  read_client(f, client);
  CHECK(qa_q3_client_connection_server_id(client) == 42);
  CHECK(qa_q3_client_connection_client_number(client) == 1);
  CHECK(qa_q3_client_connection_identity(client).has_seat && qa_q3_client_connection_identity(client).seat.index == 1);
  CHECK(qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(client), "userinfo \"\\name\\Ranger\"", NULL));
  qa_q3_wire_user_command c100 = wire_command(100, 1, 127), c200 = wire_command(200, 1, 127);
  qa_q3_wire_command_history_append(qa_q3_client_connection_commands(client), &c100);
  qa_q3_wire_command_history_append(qa_q3_client_connection_commands(client), &c200);
  qa_q3_client_send_options send_options = { f->now, 1, false };
  qa_q3_client_connection_transmit(client, &send_options, client_delivery);
  read_server(f, server);
  CHECK(f->entered_count == 1 && f->entered[0] == 100);
  CHECK(f->move_count == 1 && f->moves[0] == 200);
  CHECK(f->executed_count == 1 && strcmp(f->executed[0], "userinfo \"\\name\\Ranger\"") == 0);
  qa_q3_player_state_record player;
  qa_q3_player_state_record_init(&player, QA_Q3_PRODUCT_MISSIONPACK, 0, 5, 0);
  player.client_num = 1; player.command_time = 200;
  player.origin.x = 10; player.origin.y = 20; player.origin.z = 30;
  player.stats[0] = 100;
  qa_q3_entity_state_record entity = f->baseline;
  entity.pos.base.x = 64; entity.pos.base.y = 8; entity.pos.base.z = 16;
  uint8_t mask[1] = { 254 };
  qa_q3_server_snapshot_history_capture(snapshots, qa_q3_netchannel_outgoing_sequence(qa_q3_server_connection_channel(server)),
      &player, mask, 1, &entity, 1);
  qa_q3_server_connection_send_snapshot(server, 0, &rate, server_delivery, NULL, NULL);
  read_client(f, client);
  CHECK(cf.snapshot_count == 1);
  CHECK(cf.snapshot_count >= 1 && cf.snapshots[0].origin_x == 10 && cf.snapshots[0].origin_y == 20 && cf.snapshots[0].origin_z == 30);
  CHECK(qa_q3_reliable_ring_acknowledge(qa_q3_client_connection_reliable(client)) == 1);
  f->now = 1100;
  qa_q3_wire_user_command c300 = wire_command(300, 0, 64);
  qa_q3_wire_command_history_append(qa_q3_client_connection_commands(client), &c300);
  send_options.real_time = f->now;
  qa_q3_client_connection_transmit(client, &send_options, client_delivery);
  read_server(f, server);
  CHECK(qa_q3_server_connection_delta_message(server) == 2);
  CHECK(f->move_count == 2 && f->moves[1] == 300);
  CHECK(f->executed_count == 1);
  player.origin.x = 11; player.command_time = 300;
  qa_q3_server_snapshot_history_capture(snapshots, qa_q3_netchannel_outgoing_sequence(qa_q3_server_connection_channel(server)),
      &player, mask, 1, &entity, 1);
  qa_q3_server_connection_send_snapshot(server, 0, &rate, server_delivery, NULL, NULL);
  read_client(f, client);
  CHECK(cf.snapshot_count == 2);
  CHECK(cf.snapshot_count >= 2 && cf.snapshots[1].delta == 2);
  CHECK(cf.snapshot_count >= 2 && cf.snapshots[1].origin_x == 11);
  CHECK(cf.snapshot_count >= 2 && cf.snapshots[1].entity_count >= 1 && cf.snapshots[1].entity_base_x == 64);
  wq_clear(&f->to_client); wq_clear(&f->to_server);
  qa_q3_client_connection_destroy(client);
  qa_q3_server_connection_destroy(server);
  qa_q3_server_snapshot_history_destroy(snapshots);
  qa_q3_snapshot_entities_destroy(entities);
  free(f);
  qa_identity_owner_destroy(&identity);
}

/* ================= network.test.ts: compressed connect admission ================= */
static void test_compressed_connect_admission(void) {
  admission_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  f.enabled = true; f.lan = true; f.random = 123; f.slot_count = 1;
  f.slot.slot = 0; f.slot.phase = QA_Q3_SLOT_FREE;
  qa_q3_server_admission *server = admission(&f);
  qa_q3_address server_address = ipv4(127, 0, 0, 1, 27960), client_address = ipv4(127, 0, 0, 1, 31000);
  qa_q3_client_admission *client = NULL;
  CHECK(qa_q3_client_admission_init(&client, 4711, af_print, &f));
  CHECK(qa_q3_client_admission_begin(client, &server_address));
  qa_q3_outgoing_datagram challenge;
  CHECK(qa_q3_client_admission_resend(client, 100, "\\name\\Ranger", &challenge));
  qa_q3_server_admission_receive(server, &client_address, challenge.payload, challenge.payload_len, 100);
  qa_q3_outgoing_datagram_destroy(&challenge);
  CHECK(f.sent.count == 1);
  qa_q3_client_admission_result result;
  if (f.sent.count >= 1) {
    CHECK(qa_q3_client_admission_receive(client, &server_address, f.sent.packets[0].payload, f.sent.packets[0].len, 110, &result));
    qa_q3_client_admission_result_destroy(&result);
  }
  qa_q3_outgoing_datagram connect;
  CHECK(qa_q3_client_admission_resend(client, 110, "\\name\\Ranger", &connect));
  qa_q3_server_admission_receive(server, &client_address, connect.payload, connect.payload_len, 120);
  qa_q3_outgoing_datagram_destroy(&connect);
  CHECK(f.sent.count == 2);
  if (f.sent.count >= 2) {
    CHECK(qa_q3_client_admission_receive(client, &server_address, f.sent.packets[1].payload, f.sent.packets[1].len, 125, &result));
    CHECK(result.kind == QA_Q3_ADMIT_ADMITTED);
    qa_q3_client_admission_result_destroy(&result);
  }
  CHECK(f.has_admitted && f.admitted.qport == 4711);
  CHECK(f.has_admitted && strstr(f.admitted.userinfo, "\\ip\\127.0.0.1:31000") != NULL);
  qa_q3_client_admission_destroy(client);
  qa_q3_server_admission_destroy(server);
  admission_fixture_destroy(&f);
}

/* ================= network.test.ts: deferred admission decision ================= */
static void test_admission_owner_closes_during_decision(void) {
  for (int close_while_waiting = 0; close_while_waiting < 2; close_while_waiting++) {
    admission_fixture f;
    memset(&f, 0, sizeof f); f.magic = MAGIC;
    f.enabled = true; f.lan = true; f.slot_count = 1; f.close_on_admit = close_while_waiting != 0;
    f.slot.slot = 0; f.slot.phase = QA_Q3_SLOT_FREE;
    qa_q3_server_admission *server = admission(&f);
    qa_q3_address address;
    memset(&address, 0, sizeof address);
    address.kind = QA_Q3_ADDR_LOOPBACK;
    address.loopback_id = (char *)"deferred-admission";
    uint8_t *bytes = NULL; size_t len = 0;
    CHECK(qa_q3_encode_connect("\\protocol\\68\\qport\\77\\challenge\\0", &bytes, &len));
    qa_q3_server_admission_receive(server, &address, bytes, len, 0);
    free(bytes);
    CHECK(f.admits == 1);
    CHECK(f.sent.count == (close_while_waiting ? 0u : 1u));
    qa_q3_server_admission_destroy(server);
    admission_fixture_destroy(&f);
  }
}

/* ================= network.test.ts: wire-ordered server execution ================= */
static void ow_call(loop_fixture *f, const char *format, long long value, const char *text) {
  if (f->call_count >= 16) return;
  if (text) snprintf(f->calls[f->call_count++], 64, "%.63s", text);
  else snprintf(f->calls[f->call_count++], 64, format, value);
}
static bool ow_command(void *ctx, const qa_q3_reliable_command *command, bool client_ok) {
  loop_fixture *f = FIX(loop_fixture, ctx);
  (void)client_ok;
  ow_call(f, NULL, 0, command->text);
  if (f->retirement == 1) qa_q3_server_connection_set_phase(f->connection, QA_Q3_SERVER_PHASE_ZOMBIE);
  else if (f->retirement == 2) f->server_id++;
  return true;
}
static void ow_enter(void *ctx, const qa_q3_wire_user_command *command) { ow_call(ctx, "begin:%lld", command->server_time, NULL); }
static void ow_think(void *ctx, const qa_q3_wire_user_command *command) { ow_call(ctx, "think:%lld", command->server_time, NULL); }
static void ow_resend(void *ctx) { sentinel(ctx); }
static void ow_drop(void *ctx, const char *reason) { sentinel(ctx); (void)reason; }
static unsigned key_sentinel = MAGIC;
static const char *empty_command(void *ctx, int64_t sequence) { sentinel(ctx); (void)sequence; return ""; }
static void ordered_baseline(void *ctx, int64_t number, qa_q3_entity_state_record *out) { sentinel(ctx); (void)number; qa_q3_entity_state_record_init(out, 0); }
typedef struct ordered { qa_q3_snapshot_entities *entities; qa_q3_server_snapshot_history *history; qa_q3_server_connection *server; qa_identity_owner identity; } ordered;
static void ordered_connection(ordered *o, loop_fixture *f) {
  CHECK(qa_identity_owner_create(&o->identity, "q3-await-wire"));
  qa_q3_connection_identity who;
  memset(&who, 0, sizeof who);
  who.client = qa_identity_owner_client(&o->identity, 0, 0);
  CHECK(qa_q3_snapshot_entities_init(&o->entities, 2048));
  CHECK(qa_q3_server_snapshot_history_init(&o->history, o->entities, QA_Q3_PRODUCT_BASEQ3, ordered_baseline, f));
  qa_q3_server_bindings b;
  memset(&b, 0, sizeof b);
  b.assert_current = sb_assert; b.server_id = sb_server_id; b.restarted_server_id = sb_server_id; b.checksum_feed = sb_zero;
  b.pure = sb_false; b.time = sb_time; b.client_running = sb_true; b.flood_protect = sb_false; b.download_name = sb_download_name;
  b.command = ow_command; b.enter_world = ow_enter; b.think = ow_think; b.resend_gamestate = ow_resend; b.drop = ow_drop;
  b.print = sb_print; b.print_ctx = f; b.ctx = f;
  CHECK(qa_q3_server_connection_init(&o->server, who, 0, 77, o->history, &b));
  f->connection = o->server;
}
static void ordered_destroy(ordered *o) {
  qa_q3_server_connection_destroy(o->server);
  qa_q3_server_snapshot_history_destroy(o->history);
  qa_q3_snapshot_entities_destroy(o->entities);
  qa_identity_owner_destroy(&o->identity);
}
static qa_q3_client_message_reader *ordered_message(const char *const *commands, size_t command_count,
                                                    const int32_t *times, size_t time_count) {
  qa_q3_reliable_command reliable[4];
  memset(reliable, 0, sizeof reliable);
  for (size_t i = 0; i < command_count; i++) { reliable[i].sequence = (int64_t)i + 1; snprintf(reliable[i].text, sizeof reliable[i].text, "%s", commands[i]); }
  qa_q3_client_movement movement;
  memset(&movement, 0, sizeof movement);
  movement.kind = QA_Q3_MOVE_NO_DELTA;
  for (size_t i = 0; i < time_count; i++) { movement.commands[i].server_time = times[i]; movement.commands[i].weapon = 2; }
  movement.command_count = time_count;
  qa_q3_client_message message = { { 42, 0, 0 }, reliable, command_count, time_count ? &movement : NULL };
  qa_q3_client_key_context key = { 0, empty_command, &key_sentinel };
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_client_message(&message, &key, NULL, &bytes, &len));
  qa_q3_client_message_reader *reader = NULL;
  CHECK(qa_q3_client_message_reader_init(&reader, bytes, len, NULL));
  free(bytes);
  return reader;
}
static void test_wire_order(void) {
  loop_fixture *f = calloc(1, sizeof *f); f->magic = MAGIC;
  f->server_id = 42;
  ordered o;
  ordered_connection(&o, f);
  qa_q3_server_connection_set_phase(o.server, QA_Q3_SERVER_PHASE_PRIMED);
  const char *commands[2] = { "one", "two" };
  const int32_t times[3] = { 10, 20, 30 };
  qa_q3_client_message_reader *reader = ordered_message(commands, 2, times, 3);
  qa_q3_server_connection_execute_message(o.server, reader);
  qa_q3_client_message_reader_destroy(reader);
  static const char *const expected[5] = { "one", "two", "begin:10", "think:20", "think:30" };
  CHECK(f->call_count == 5);
  for (size_t i = 0; i < 5 && i < f->call_count; i++) CHECK(strcmp(f->calls[i], expected[i]) == 0);
  CHECK(qa_q3_server_connection_last_client_command(o.server) == 2);
  ordered_destroy(&o);
  free(f);
}
typedef struct execute_scope { qa_q3_server_connection *server; qa_q3_client_message_reader *reader; } execute_scope;
static void execute_body(void *ctx) {
  execute_scope *scope = ctx;
  qa_q3_server_connection_execute_message(scope->server, scope->reader);
}
static void test_retired_packet_stops(void) {
  for (int retirement = 1; retirement <= 2; retirement++) {
    loop_fixture *f = calloc(1, sizeof *f); f->magic = MAGIC;
    f->server_id = 42; f->retirement = retirement;
    ordered o;
    ordered_connection(&o, f);
    const char *commands[2] = { "one", "two" };
    qa_q3_client_message_reader *reader = ordered_message(commands, 2, NULL, 0);
    execute_scope scope = { o.server, reader };
    qa_caught_error caught;
    bool ok = qa_try(execute_body, &scope, &caught);
    if (retirement == 2) CHECK(!ok && strstr(caught.message, "retired server world") != NULL);
    else CHECK(ok);
    qa_q3_client_message_reader_destroy(reader);
    CHECK(f->call_count == 1 && strcmp(f->calls[0], "one") == 0);
    CHECK(qa_q3_server_connection_last_client_command(o.server) == 0);
    ordered_destroy(&o);
    free(f);
  }
}

/* ================= network.test.ts: configstring overflow and download drop ================= */
typedef struct overflow_fixture {
  unsigned magic;
  qa_q3_configstring_client client;
  bool dropped;
  int download_drops;
} overflow_fixture;
static bool of_true(void *ctx) { sentinel(ctx); return true; }
static bool of_false(void *ctx) { sentinel(ctx); return false; }
static const qa_q3_configstring_client *of_clients(void *ctx, size_t *count) {
  overflow_fixture *f = FIX(overflow_fixture, ctx);
  *count = 1;
  return &f->client;
}
static void of_drop(void *ctx, const char *message) {
  overflow_fixture *f = FIX(overflow_fixture, ctx);
  (void)message;
  f->client.phase = QA_Q3_CS_PHASE_OTHER; /* TS: server.phase = "zombie" */
  f->dropped = true;
}
static void of_assert(void *ctx) { sentinel(ctx); }
static void of_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static bool dl_open(void *ctx, const char *name, qa_q3_download_read_file *out) { sentinel(ctx); (void)name; (void)out; return false; }
static bool dl_true(void *ctx) { sentinel(ctx); return true; }
static bool dl_false(void *ctx) { sentinel(ctx); return false; }
static void dl_drop(void *ctx, const char *reason) { (void)reason; FIX(overflow_fixture, ctx)->download_drops++; }
static void test_configstring_overflow_drop(void) {
  overflow_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC;
  CHECK(qa_q3_server_reliable_commands_init(&f.client.reliable));
  f.client.phase = QA_Q3_CS_PHASE_ACTIVE;
  f.client.print = of_print; f.client.print_ctx = &f;
  f.client.drop = of_drop; f.client.drop_ctx = &f;
  f.client.assert_current = of_assert; f.client.assert_ctx = &f;
  char text[32];
  for (int i = 0; i < 64; i++) {
    snprintf(text, sizeof text, "pending %d", i);
    qa_q3_server_reliable_commands_add(f.client.reliable, text);
  }
  qa_q3_configstring_bindings b = { of_true, of_false, of_clients, &f };
  qa_q3_server_config_strings *strings = NULL;
  CHECK(qa_q3_server_config_strings_init(&strings, &b));
  char *value = malloc(3001);
  memset(value, 'x', 3000); value[3000] = 0;
  qa_q3_server_config_strings_set(strings, 2, value);
  free(value);
  CHECK(f.dropped);
  CHECK(qa_q3_reliable_ring_sequence(f.client.reliable) == 65);
  qa_q3_server_config_strings_destroy(strings);
  qa_q3_reliable_ring_destroy(f.client.reliable);
  qa_q3_download_server_bindings db;
  memset(&db, 0, sizeof db);
  db.open = dl_open; db.enabled = dl_true; db.pure = dl_false; db.drop = dl_drop; db.print = of_print; db.print_ctx = &f; db.ctx = &f;
  qa_q3_server_download *download = NULL;
  CHECK(qa_q3_server_download_init(&download, &db));
  qa_q3_server_download_acknowledge(download, 1, 0);
  CHECK(f.download_drops == 1);
  qa_q3_server_download_destroy(download);
}

int main(void) {
  test_authorization_ipx();
  test_authorization_wan();
  test_authorization_retired_lookup();
  test_client_authorization_packet();
  test_client_authorization_retired_and_retry();
  test_client_authorization_opt_in();
  test_cd_key_non_ascii();
  test_recording_seed();
  test_loopback_exchange();
  test_compressed_connect_admission();
  test_admission_owner_closes_during_decision();
  test_wire_order();
  test_retired_packet_stops();
  test_configstring_overflow_drop();
  printf("test_q3_units: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
