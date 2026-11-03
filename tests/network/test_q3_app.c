/*
 * Ports of application-network cases from ../quake-anthology-ts/tests/network/q3/:
 * client-print.test.ts, client-authorization.test.ts ("Q3 admission authorizes
 * before WAN challenge..."), and network.test.ts (IPX application admission,
 * callback drops, malformed packet recovery, rcon endpoint). In-memory datagram
 * transports stand in for UdpTransport/LoopbackHub/PacketQueue; TS async gates
 * become synchronous callbacks. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/q3.h"
#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/core/q3_cd_key.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/network/q3.h"
#include "qa/network/services/admin.h"

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
#define MAGIC 0x6b3e19d5u
#define FIX(type, ctx) ((type *)sentinel((ctx)))
static void *sentinel(void *ctx) {
  CHECK(ctx != NULL && *(unsigned *)ctx == MAGIC);
  return ctx;
}

/* ---------------- in-memory datagram transport ---------------- */
typedef struct fake_event { qa_net_address from; uint8_t *payload; size_t length; } fake_event;
typedef struct fake_transport {
  unsigned magic;
  qa_net_address address;
  struct fake_transport *peer;
  fake_event queue[64]; size_t head, count;
  qa_net_address sent_to[64]; uint8_t *sent[64]; size_t sent_len[64]; size_t sent_count;
  bool closed;
  qa_datagram_transport handle; /* stable pointer for owners holding qa_datagram_transport * */
} fake_transport;
static qa_datagram_transport ft_transport(fake_transport *t);
static void ft_init(fake_transport *t, const qa_net_address *address) {
  memset(t, 0, sizeof *t);
  t->magic = MAGIC;
  CHECK(qa_net_address_clone(address, &t->address));
  t->handle = ft_transport(t);
}
static void ft_accept(fake_transport *t, const qa_net_address *from, const uint8_t *payload, size_t length) {
  if (t->count >= 64) { CHECK(!"transport queue overflow"); return; }
  fake_event *e = &t->queue[(t->head + t->count++) % 64];
  CHECK(qa_net_address_clone(from, &e->from));
  e->payload = malloc(length ? length : 1);
  memcpy(e->payload, payload, length);
  e->length = length;
}
static int ft_get_address(void *ctx, qa_net_address *out) { return qa_net_address_clone(&FIX(fake_transport, ctx)->address, out); }
static bool ft_is_closed(void *ctx) { return FIX(fake_transport, ctx)->closed; }
static int64_t ft_max(void *ctx) { sentinel(ctx); return -1; }
static bool ft_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  fake_transport *t = FIX(fake_transport, ctx);
  if (t->sent_count < 64) {
    CHECK(qa_net_address_clone(to, &t->sent_to[t->sent_count]));
    t->sent[t->sent_count] = malloc(length ? length : 1);
    memcpy(t->sent[t->sent_count], payload, length);
    t->sent_len[t->sent_count] = length;
    t->sent_count++;
  }
  if (t->peer) {
    CHECK(qa_net_same_address(to, &t->peer->address, true));
    ft_accept(t->peer, &t->address, payload, length);
  }
  return true;
}
static int ft_poll(void *ctx, qa_receive_event *out) {
  fake_transport *t = FIX(fake_transport, ctx);
  if (t->count == 0) return 0;
  fake_event *e = &t->queue[t->head];
  t->head = (t->head + 1) % 64; t->count--;
  memset(out, 0, sizeof *out);
  out->kind = QA_RECV_PACKET;
  out->from = e->from;
  out->payload = e->payload;
  out->length = e->length;
  return 1;
}
static long long ft_subscribe(void *ctx, qa_net_listener_fn fn, void *listener_ctx) { sentinel(ctx); (void)fn; (void)listener_ctx; return 1; }
static void ft_unsubscribe(void *ctx, long long id) { sentinel(ctx); (void)id; }
static void ft_close(void *ctx) { FIX(fake_transport, ctx)->closed = true; }
static qa_datagram_transport ft_transport(fake_transport *t) {
  qa_datagram_transport d = { t, ft_get_address, ft_is_closed, ft_max, ft_send, ft_poll, ft_subscribe, ft_unsubscribe, ft_close };
  return d;
}
static void ft_clear_sent(fake_transport *t) {
  for (size_t i = 0; i < t->sent_count; i++) { qa_net_address_destroy(&t->sent_to[i]); free(t->sent[i]); }
  t->sent_count = 0;
}
static void ft_destroy(fake_transport *t) {
  while (t->count) {
    fake_event *e = &t->queue[t->head];
    qa_net_address_destroy(&e->from); free(e->payload);
    t->head = (t->head + 1) % 64; t->count--;
  }
  ft_clear_sent(t);
  qa_net_address_destroy(&t->address);
}
static qa_net_address net_ipv4(int64_t a, int64_t b, int64_t c, int64_t d, int64_t port) {
  qa_net_address out;
  const int64_t host[4] = { a, b, c, d };
  CHECK(qa_net_make_ipv4(&out, host, port, false));
  return out;
}
static qa_net_address net_ipx(int64_t network, const int64_t node[6], int64_t port) {
  qa_net_address out;
  CHECK(qa_net_make_ipx(&out, network, node, port));
  return out;
}
static qa_net_address net_loopback(const char *id) {
  qa_net_address out;
  CHECK(qa_net_make_loopback(&out, id));
  return out;
}
static void push_text(fake_transport *to, const qa_net_address *from, const char *text) {
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_connectionless_text(text, &bytes, &len));
  ft_accept(to, from, bytes, len);
  free(bytes);
}
static void push_raw(fake_transport *to, const qa_net_address *from, const char *body, size_t body_len) {
  uint8_t packet[2048];
  memset(packet, 255, 4);
  memcpy(packet + 4, body, body_len);
  ft_accept(to, from, packet, body_len + 4);
}
static char *command_of(const uint8_t *bytes, size_t len) {
  qa_q3_connectionless_packet decoded;
  if (!qa_q3_decode_connectionless(bytes, len, QA_Q3_CONNLESS_SERVER, NULL, &decoded)) return NULL;
  size_t n = strlen(decoded.command) + 1;
  char *out = malloc(n);
  memcpy(out, decoded.command, n);
  qa_q3_connectionless_packet_destroy(&decoded);
  return out;
}
static bool sent_command_is(fake_transport *t, size_t index, const char *command) {
  if (index >= t->sent_count) return false;
  char *got = command_of(t->sent[index], t->sent_len[index]);
  bool same = got && strcmp(got, command) == 0;
  free(got);
  return same;
}

/* ---------------- Q3 application client host ---------------- */
typedef struct client_host_fixture {
  unsigned magic;
  char prints[8][1200]; size_t print_count;
  bool loaded;
  const char *userinfo;
  double now;
  char disconnected[256];
} client_host_fixture;
static bool ch_downloading(void *ctx) { sentinel(ctx); return false; }
static const char *ch_userinfo(void *ctx) { return FIX(client_host_fixture, ctx)->userinfo; }
static void ch_system_info(void *ctx, const char *info) { sentinel(ctx); (void)info; }
static void ch_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping) { sentinel(ctx); (void)snapshot; (void)ping; }
static void ch_thunk(void *ctx) { sentinel(ctx); }
static void ch_print(void *ctx, const char *text) {
  client_host_fixture *f = FIX(client_host_fixture, ctx);
  if (f->print_count < 8) snprintf(f->prints[f->print_count++], 1200, "%s", text);
}
static double ch_download_size(void *ctx, double size) { sentinel(ctx); return size; }
static void ch_download(void *ctx, const qa_q3_download_block *block) { sentinel(ctx); (void)block; CHECK(!"unexpected download"); }
static void ch_clear(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx) { sentinel(ctx); (void)assert_current; (void)assert_ctx; }
static void ch_gamestate(void *ctx, const qa_q3_gamestate *state, double generation, qa_app_q3_assert_fn assert_current, void *assert_ctx) {
  (void)state; (void)generation; (void)assert_current; (void)assert_ctx;
  FIX(client_host_fixture, ctx)->loaded = true;
}
static void ch_attach(void *ctx, qa_q3_client_connection *connection) { sentinel(ctx); (void)connection; }
static qa_q3_wire_user_command ch_command(void *ctx, const void *command) {
  sentinel(ctx); (void)command;
  CHECK(!"No fixture gameplay");
  qa_q3_wire_user_command none;
  memset(&none, 0, sizeof none);
  return none;
}
static void ch_disconnected(void *ctx, const char *reason) { snprintf(FIX(client_host_fixture, ctx)->disconnected, 256, "%s", reason); }
static double ch_now(void *ctx) { return FIX(client_host_fixture, ctx)->now; }
static qa_app_q3_client_host client_host(client_host_fixture *f, qa_client_id client) {
  qa_app_q3_client_host h;
  memset(&h, 0, sizeof h);
  h.ctx = f; h.identity.client = client;
  h.downloading = ch_downloading; h.userinfo = ch_userinfo; h.system_info = ch_system_info; h.snapshot = ch_snapshot;
  h.map_restart = ch_thunk; h.print = ch_print; h.download_size = ch_download_size; h.download = ch_download;
  h.clear_active = ch_clear; h.gamestate = ch_gamestate; h.attach = ch_attach; h.command = ch_command; h.disconnected = ch_disconnected;
  return h;
}
static qa_q3_address q3_of(const qa_net_address *address) {
  qa_q3_address out;
  CHECK(qa_q3_address_from_net(address, &out));
  return out;
}

/* ================= client-print.test.ts ================= */
static void test_client_print(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-print"));
  qa_net_address server_address = net_ipv4(127, 0, 0, 1, 27960), client_address = net_ipv4(127, 0, 0, 1, 31001);
  fake_transport transport;
  ft_init(&transport, &client_address);
  client_host_fixture f;
  memset(&f, 0, sizeof f); f.magic = MAGIC; f.userinfo = "";
  qa_app_q3_client_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = ft_transport(&transport);
  options.remote = q3_of(&server_address);
  options.host = client_host(&f, qa_identity_owner_client(&identity, 0, 0));
  options.qport = 196;
  options.now = ch_now; options.now_ctx = &f;
  qa_app_q3_client_network *client = qa_app_q3_client_network_create(options);
  CHECK(client != NULL);
  push_raw(&transport, &server_address, "print\nFirst line\n  second  line\n100%\xe9", 37);
  qa_app_q3_client_network_poll(client, 1);
  CHECK(f.print_count == 1 && strcmp(f.prints[0], "First line\n  second  line\n100..") == 0);
  push_raw(&transport, &server_address, "print\nkept\0discarded\n", 21);
  qa_app_q3_client_network_poll(client, 2);
  CHECK(f.print_count == 2 && strcmp(f.prints[1], "kept") == 0);
  push_text(&transport, &server_address, "print ignored same-line text");
  qa_app_q3_client_network_poll(client, 3);
  CHECK(f.print_count == 3 && strcmp(f.prints[2], "") == 0);
  char long_text[1200];
  memcpy(long_text, "print\n", 6);
  memset(long_text + 6, 'x', 1100);
  long_text[1106] = 0;
  push_text(&transport, &server_address, long_text);
  qa_app_q3_client_network_poll(client, 4);
  CHECK(f.print_count == 4 && strlen(f.prints[3]) == 1023 && strspn(f.prints[3], "x") == 1023);
  CHECK(qa_app_q3_client_network_phase(client) == QA_APP_PHASE_CONNECTING);
  qa_app_q3_client_network_close(client);
  qa_app_q3_client_network_destroy(client);
  qa_q3_mirror_address_destroy(&options.remote);
  ft_destroy(&transport);
  qa_net_address_destroy(&server_address);
  qa_net_address_destroy(&client_address);
  qa_identity_owner_destroy(&identity);
}

/* ================= client-authorization.test.ts: admission authorizes before WAN challenge ================= */
typedef struct auth_owner_fixture {
  unsigned magic;
  qa_q3_client_authorization *owner;
  int lookups;
  qa_q3_address authority;
} auth_owner_fixture;
static bool ao_demo(void *ctx) { sentinel(ctx); return false; }
static bool ao_resolve(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  auth_owner_fixture *f = FIX(auth_owner_fixture, ctx);
  (void)err; (void)err_len;
  f->lookups++;
  return qa_q3_mirror_address_copy(out, &f->authority);
}
static void ao_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void ao_request(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx,
    void (*send)(void *send_ctx, const qa_q3_address *address, const uint8_t *payload, size_t length), void *send_ctx) {
  auth_owner_fixture *f = FIX(auth_owner_fixture, ctx);
  qa_q3_client_authorization_request(f->owner, assert_current, assert_ctx, send, send_ctx);
}
static void test_client_admission_authorizes_wan(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-auth-admission"));
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context.session = identity.session;
  cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(cvars, "cl_authorizeKey", "1", QA_CVAR_FLAG_NONE, &snapshot));
  qa_cvar_read_free(&snapshot);
  qa_q3_cd_key_state keys;
  qa_q3_cd_key_state_init(&keys, NULL, NULL, QA_Q3_KEY_BUILD_CLIENT);
  qa_q3_cd_key_state_write_ui(&keys, 0, "", (const uint8_t *)"2222222222222222", 16);
  auth_owner_fixture auth;
  memset(&auth, 0, sizeof auth); auth.magic = MAGIC;
  memset(&auth.authority, 0, sizeof auth.authority);
  auth.authority.kind = QA_Q3_ADDR_IPV4;
  auth.authority.host[0] = 192; auth.authority.host[1] = 0; auth.authority.host[2] = 2; auth.authority.host[3] = 1;
  auth.authority.port = 27952;
  qa_q3_client_authorization_options o;
  memset(&o, 0, sizeof o);
  o.cvars = cvars; o.keys = &keys; o.demo_restricted = ao_demo; o.resolve = ao_resolve; o.print = ao_print; o.print_ctx = &auth; o.ctx = &auth;
  CHECK(qa_q3_client_authorization_init(&auth.owner, &o));
  qa_net_address local = net_ipv4(127, 0, 0, 1, 30000);
  const int64_t node[6] = { 1, 2, 3, 4, 5, 6 };
  qa_net_address remotes[3] = { net_ipv4(198, 51, 100, 1, 27960), net_ipv4(192, 168, 1, 2, 27960), net_ipx(0x12345678, node, 27960) };
  for (int which = 0; which < 3; which++) {
    fake_transport transport;
    ft_init(&transport, &local);
    client_host_fixture f;
    memset(&f, 0, sizeof f); f.magic = MAGIC; f.userinfo = "";
    qa_app_q3_client_network_options options;
    memset(&options, 0, sizeof options);
    options.transport = ft_transport(&transport);
    options.remote = q3_of(&remotes[which]);
    options.host = client_host(&f, qa_identity_owner_client(&identity, 0, 0));
    options.qport = 77;
    options.has_authorization = true;
    options.authorization.ctx = &auth; options.authorization.request = ao_request;
    options.now = ch_now; options.now_ctx = &f;
    qa_app_q3_client_network *client = qa_app_q3_client_network_create(options);
    CHECK(client != NULL);
    qa_app_q3_client_network_poll(client, 0);
    if (which == 0) {
      CHECK(transport.sent_count == 2);
      CHECK(sent_command_is(&transport, 0, "getKeyAuthorize"));
      CHECK(sent_command_is(&transport, 1, "getchallenge"));
      qa_q3_address to;
      memset(&to, 0, sizeof to);
      if (transport.sent_count >= 1) {
        to = q3_of(&transport.sent_to[0]);
        CHECK(qa_q3_mirror_same_address(&to, &auth.authority, true));
        qa_q3_mirror_address_destroy(&to);
      }
      qa_app_q3_client_network_poll(client, 2999);
      CHECK(transport.sent_count == 2);
      qa_app_q3_client_network_poll(client, 3000);
      CHECK(transport.sent_count == 4);
      CHECK(auth.lookups == 1);
    } else {
      CHECK(transport.sent_count == 1);
      CHECK(sent_command_is(&transport, 0, "getchallenge"));
      if (which == 2) CHECK(transport.sent_count >= 1 && transport.sent_to[0].kind == QA_NET_IPX);
      CHECK(auth.lookups == 1);
    }
    qa_app_q3_client_network_close(client);
    qa_app_q3_client_network_destroy(client);
    qa_q3_mirror_address_destroy(&options.remote);
    ft_destroy(&transport);
  }
  for (int i = 0; i < 3; i++) qa_net_address_destroy(&remotes[i]);
  qa_net_address_destroy(&local);
  qa_q3_client_authorization_destroy(auth.owner);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

/* ---------------- Q3 application server host ---------------- */
typedef enum drop_at { DROP_NONE, DROP_BEGIN, DROP_INPUT, DROP_USERINFO, DROP_COMMAND } drop_at;
typedef struct server_host_fixture {
  unsigned magic;
  qa_app_q3_player player;
  qa_q3_product product;
  qa_q3_pure_server pure;
  qa_q3_server_rate rate;
  qa_q3_gamestate gamestate;
  qa_q3_gamestate_entry entries[1];
  qa_q3_player_state_record player_state;
  double time;
  char calls[16][64]; size_t call_count;
  char printed[32][300]; size_t print_count;
  char userinfo[4][512]; size_t userinfo_count;
  char commands[8][64]; size_t command_count;
  drop_at drop;
  bool begin_fails;
  qa_app_q3_server_network *network;
  /* administration */
  qa_server_administration administration;
  bool blocked;
  char rcon[4][128]; size_t rcon_count;
  qa_net_address master;
  bool has_master;
} server_host_fixture;
static void sh_call(server_host_fixture *f, const char *text) {
  if (f->call_count < 16) snprintf(f->calls[f->call_count++], 64, "%s", text);
}
static void sh_prepare(void *ctx, double feed, double server_id, void (*cs)(void *, double, const char *), void *cs_ctx) {
  sentinel(ctx); (void)feed; (void)server_id; (void)cs; (void)cs_ctx;
}
static const void *sh_pure(void *ctx, double server_id, const double *feed_id) { (void)server_id; (void)feed_id; return &FIX(server_host_fixture, ctx)->pure; }
static bool sh_false(void *ctx) { sentinel(ctx); return false; }
static const void *sh_open(void *ctx, const char *name) { sentinel(ctx); (void)name; return NULL; }
static const void *sh_rate(void *ctx, qa_app_q3_player player) { (void)player; return &FIX(server_host_fixture, ctx)->rate; }
static const void *sh_supports(void *ctx) { sentinel(ctx); return NULL; }
static double sh_time(void *ctx) { return FIX(server_host_fixture, ctx)->time; }
static const double *sh_occupied(void *ctx, size_t *count) { sentinel(ctx); *count = 0; return NULL; }
static qa_app_q3_admission sh_admit(void *ctx, const void *request) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  const qa_q3_accepted_connect *connect = request;
  if (f->userinfo_count < 4) snprintf(f->userinfo[f->userinfo_count++], 512, "%.511s", connect->userinfo ? connect->userinfo : "");
  qa_app_q3_admission admitted = { true, f->player, NULL };
  return admitted;
}
static qa_app_q3_player sh_carried(void *ctx, qa_client_id client) { (void)client; return FIX(server_host_fixture, ctx)->player; }
static void sh_disconnect(void *ctx, qa_app_q3_player player, const char *reason) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  (void)player; (void)reason;
  if (!f->begin_fails) sh_call(f, "disconnect"); /* the recovery fixture's disconnect() records nothing */
}
static const void *sh_game_state(void *ctx, qa_app_q3_player player, double server_id) {
  (void)player; (void)server_id;
  return &FIX(server_host_fixture, ctx)->gamestate;
}
static qa_app_q3_snapshot sh_snapshot(void *ctx, qa_app_q3_player player) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  (void)player;
  qa_app_q3_snapshot snapshot = { &f->player_state, NULL, 0, NULL, 0 };
  return snapshot;
}
static void guest_drop(server_host_fixture *f) {
  qa_app_q3_guest_output output = qa_app_q3_server_network_game_output(f->network);
  output.drop_client(output.ctx, 0, "guest drop");
}
static void sh_begin(void *ctx, qa_app_q3_player player, const void *command) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  const qa_q3_wire_user_command *wire = command;
  (void)player;
  char text[64];
  if (f->begin_fails) { sh_call(f, "begin"); qa_range_error("guest memory access failed"); }
  snprintf(text, sizeof text, "begin:%d", wire->server_time);
  sh_call(f, text);
  if (f->drop == DROP_BEGIN) guest_drop(f);
}
static const void *sh_input(void *ctx, qa_app_q3_player player, const void *command, double sequence) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  const qa_q3_wire_user_command *wire = command;
  (void)player; (void)sequence;
  char text[64];
  if (f->begin_fails) sh_call(f, "input");
  else {
    snprintf(text, sizeof text, "input:%d", wire->server_time);
    sh_call(f, text);
  }
  if (f->drop == DROP_INPUT) guest_drop(f);
  return NULL;
}
static void sh_command(void *ctx, qa_app_q3_player player, const char *name, const char *const *args, size_t count) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  (void)player; (void)args; (void)count;
  if (f->command_count < 8) snprintf(f->commands[f->command_count++], 64, "%.63s", name);
  if (f->drop != DROP_NONE) {
    char text[64];
    snprintf(text, sizeof text, "command:%.40s", name);
    sh_call(f, text);
  }
  if (f->drop == DROP_COMMAND) guest_drop(f);
}
static void sh_userinfo(void *ctx, qa_app_q3_player player, const char *value) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  (void)player; (void)value;
  if (f->drop != DROP_NONE) sh_call(f, "userinfo");
  if (f->drop == DROP_USERINFO) guest_drop(f);
}
static const char *sh_status(void *ctx, const char *challenge, bool detailed) {
  (void)challenge; (void)detailed;
  sh_call(FIX(server_host_fixture, ctx), "status");
  return "infoResponse\n";
}
static void sh_print(void *ctx, const char *text) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  if (f->print_count < 32) snprintf(f->printed[f->print_count++], 300, "%.299s", text);
}
static const char *ad_password(void *ctx) { sentinel(ctx); return "secret"; }
static void ad_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  if (f->rcon_count < 4) snprintf(f->rcon[f->rcon_count++], 128, "%.127s", command);
  output(output_ctx, "executed\n", 9);
}
static bool ad_rejects(void *ctx, const qa_net_address *address) { (void)address; return FIX(server_host_fixture, ctx)->blocked; }
static const qa_net_address *ad_masters(void *ctx, size_t *count) {
  server_host_fixture *f = FIX(server_host_fixture, ctx);
  *count = f->has_master ? 1 : 0;
  return f->has_master ? &f->master : NULL;
}
static void ad_record(void *ctx, const qa_net_address *address, const char *result) { sentinel(ctx); (void)address; (void)result; }
static void server_host_init(server_host_fixture *f, qa_identity_owner *owner) {
  memset(f, 0, sizeof *f);
  f->magic = MAGIC;
  f->player.client = qa_identity_owner_client(owner, 0, 0);
  f->player.actor = qa_identity_owner_actor(owner, 0, 0);
  f->player.source_entity = 0;
  f->product = QA_Q3_PRODUCT_BASEQ3;
  f->pure.enabled = false; f->pure.checksum_feed_server_id = 1;
  qa_q3_server_rate rate = { 10000, 0, 50, true, false, true };
  f->rate = rate;
  qa_q3_player_state_record_init(&f->player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  f->administration.ctx = f;
  f->administration.rcon_password = ad_password; f->administration.execute = ad_execute;
  f->administration.rejects = ad_rejects; f->administration.masters = ad_masters; f->administration.record = ad_record;
}
static qa_app_q3_server_host server_host(server_host_fixture *f, bool with_begin, bool with_administration) {
  qa_app_q3_server_host h;
  memset(&h, 0, sizeof h);
  h.ctx = f;
  h.administration = with_administration ? &f->administration : NULL;
  h.product = &f->product; h.max_clients = 1;
  h.prepare = sh_prepare; h.pure = sh_pure; h.downloads_enabled = sh_false; h.open_download = sh_open;
  h.rate = sh_rate; h.supports_source_wire = sh_supports; h.time = sh_time; h.occupied_slots = sh_occupied;
  h.admit = sh_admit; h.carried_player = sh_carried; h.disconnect = sh_disconnect; h.game_state = sh_game_state;
  h.snapshot = sh_snapshot; h.begin = with_begin ? sh_begin : NULL; h.input = sh_input; h.command = sh_command;
  h.userinfo = sh_userinfo; h.status = sh_status; h.print = sh_print;
  return h;
}
static double zero_random(void *ctx) { sentinel(ctx); return 0; }
static bool must_not_resolve(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  sentinel(ctx); (void)out; (void)err; (void)err_len;
  CHECK(!"IPX must not resolve IPv4 authorization");
  return false;
}

/* Wire client: TS `new Netchannel("client", 77)` plus encodeClientMessage/xorClientMessage. */
static unsigned empty_key_sentinel = MAGIC;
static const char *empty_command(void *ctx, int64_t sequence) { sentinel(ctx); (void)sequence; return ""; }
static void wire_send(qa_q3_netchannel *channel, fake_transport *server, const qa_net_address *from,
                      const int32_t *times, size_t time_count, const char *const *commands, size_t command_count) {
  qa_q3_reliable_command reliable[4];
  memset(reliable, 0, sizeof reliable);
  for (size_t i = 0; i < command_count; i++) { reliable[i].sequence = (int64_t)i + 1; snprintf(reliable[i].text, sizeof reliable[i].text, "%s", commands[i]); }
  qa_q3_client_movement movement;
  memset(&movement, 0, sizeof movement);
  movement.kind = QA_Q3_MOVE_NO_DELTA;
  for (size_t i = 0; i < time_count; i++) { movement.commands[i].server_time = times[i]; movement.commands[i].weapon = 2; }
  movement.command_count = time_count;
  qa_q3_client_message message = { { 1, 0, 0 }, reliable, command_count, time_count ? &movement : NULL };
  qa_q3_client_key_context key = { 0, empty_command, &empty_key_sentinel };
  uint8_t *bytes = NULL, *encoded = NULL; size_t len = 0;
  CHECK(qa_q3_encode_client_message(&message, &key, NULL, &bytes, &len));
  CHECK(qa_q3_xor_client_message(bytes, len, 0, empty_command, &empty_key_sentinel, &encoded));
  uint8_t **packets = NULL; size_t *lengths = NULL, count = 0;
  CHECK(qa_q3_netchannel_transmit(channel, encoded, len, &packets, &lengths, &count));
  for (size_t i = 0; i < count; i++) { ft_accept(server, from, packets[i], lengths[i]); free(packets[i]); }
  free(packets); free(lengths); free(bytes); free(encoded);
}
static void push_connect(fake_transport *server, const qa_net_address *from, size_t truncate) {
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_connect("\\protocol\\68\\qport\\77\\challenge\\0", &bytes, &len));
  ft_accept(server, from, bytes, truncate && truncate < len ? truncate : len);
  free(bytes);
}
static bool printed_contains(const server_host_fixture *f, const char *needle) {
  for (size_t i = 0; i < f->print_count; i++) if (strstr(f->printed[i], needle)) return true;
  return false;
}
static qa_app_q3_server_network *server_network(fake_transport *transport, server_host_fixture *f, bool with_begin, bool with_admin) {
  qa_app_q3_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &transport->handle;
  options.host = server_host(f, with_begin, with_admin);
  options.random = zero_random; options.random_ctx = f;
  qa_app_q3_server_network *network = qa_app_q3_server_network_create(options);
  CHECK(network != NULL);
  {
    /* TS readonly wire = { kind: "source", protocol: { kind: "q3", version: 68 } } */
    qa_app_application_network as_network = qa_app_q3_server_network_as_network(network);
    qa_wire_selection wire = as_network.wire(as_network.ctx);
    CHECK(!wire.is_unified && strcmp(wire.protocol.kind, "q3") == 0 && wire.protocol.version == 68);
  }
  f->network = network;
  return network;
}

/* ================= network.test.ts: callback drops ================= */
static void test_callback_drops(void) {
  static const char *const names[4] = { "begin", "input", "userinfo", "command" };
  for (int which = 0; which < 4; which++) {
    drop_at at = (drop_at)(DROP_BEGIN + which);
    qa_identity_owner owner;
    char owner_name[64];
    snprintf(owner_name, sizeof owner_name, "q3-drop-%s", names[which]);
    CHECK(qa_identity_owner_create(&owner, owner_name));
    server_host_fixture *f = malloc(sizeof *f);
    server_host_init(f, &owner);
    f->drop = at;
    char server_id[64], client_id[64];
    snprintf(server_id, sizeof server_id, "drop-server-%s", names[which]);
    snprintf(client_id, sizeof client_id, "drop-client-%s", names[which]);
    qa_net_address server_address = net_loopback(server_id), client_address = net_loopback(client_id);
    fake_transport transport;
    ft_init(&transport, &server_address);
    qa_app_q3_server_network *network = server_network(&transport, f, true, false);
    qa_q3_netchannel *channel = NULL;
    CHECK(qa_q3_netchannel_init(&channel, QA_Q3_CHANNEL_CLIENT, 77, NULL, NULL));
    size_t count = 0;
    push_connect(&transport, &client_address, 0);
    qa_app_q3_server_network_poll(network, 0, &count);
    wire_send(channel, &transport, &client_address, NULL, 0, NULL, 0);
    qa_app_q3_server_network_poll(network, 1, &count);
    if (at == DROP_COMMAND || at == DROP_USERINFO) {
      const int32_t ten[1] = { 10 };
      wire_send(channel, &transport, &client_address, ten, 1, NULL, 0);
      qa_app_q3_server_network_poll(network, 2, &count);
      const int32_t later[2] = { 20, 30 };
      const char *commands[2] = { at == DROP_USERINFO ? "userinfo \"\\name\\Changed\"" : "drop", "later" };
      wire_send(channel, &transport, &client_address, later, 2, commands, 2);
    } else {
      const int32_t times[3] = { 10, 20, 30 };
      wire_send(channel, &transport, &client_address, times, 3, NULL, 0);
    }
    qa_app_q3_server_network_poll(network, 3, &count);
    CHECK(count == 0);
    qa_app_q3_player clients[2];
    CHECK(qa_app_q3_server_network_clients(network, clients, 2) == 0);
    CHECK(qa_app_q3_server_network_phase(network) == QA_APP_PHASE_ACTIVE);
    const char *expected[3] = { "begin:10", NULL, "disconnect" };
    expected[1] = at == DROP_BEGIN ? NULL : at == DROP_INPUT ? "input:20" : at == DROP_USERINFO ? "userinfo" : "command:drop";
    size_t want = expected[1] ? 3 : 2;
    CHECK(f->call_count == want);
    if (f->call_count != want) {
      fprintf(stderr, "drop %s calls:", names[which]);
      for (size_t i = 0; i < f->call_count; i++) fprintf(stderr, " [%s]", f->calls[i]);
      for (size_t i = 0; i < f->print_count; i++) fprintf(stderr, " print<%s>", f->printed[i]);
      fprintf(stderr, "\n");
    }
    if (f->call_count == want) {
      CHECK(strcmp(f->calls[0], "begin:10") == 0);
      if (expected[1]) CHECK(strcmp(f->calls[1], expected[1]) == 0);
      CHECK(strcmp(f->calls[want - 1], "disconnect") == 0);
    }
    CHECK(!printed_contains(f, "callback belongs to a dropped client"));
    push_raw(&transport, &client_address, "getinfo", 7);
    qa_app_q3_server_network_poll(network, 4, &count);
    CHECK(f->call_count >= 1 && strcmp(f->calls[f->call_count - 1], "status") == 0);
    qa_app_q3_server_network_close(network);
    qa_q3_netchannel_destroy(channel);
    qa_app_q3_server_network_destroy(network);
    ft_destroy(&transport);
    qa_net_address_destroy(&server_address);
    qa_net_address_destroy(&client_address);
    free(f);
    qa_identity_owner_destroy(&owner);
  }
}

/* ================= network.test.ts: malformed packet recovery ================= */
static void test_packet_recovery(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "q3-failure"));
  server_host_fixture *f = malloc(sizeof *f);
  server_host_init(f, &owner);
  f->begin_fails = true;
  qa_net_address server_address = net_loopback("failure-server"), client_address = net_loopback("failure-client");
  fake_transport transport;
  ft_init(&transport, &server_address);
  qa_app_q3_server_network *network = server_network(&transport, f, true, false);
  qa_q3_netchannel *channel = NULL;
  CHECK(qa_q3_netchannel_init(&channel, QA_Q3_CHANNEL_CLIENT, 77, NULL, NULL));
  size_t count = 0;
  push_connect(&transport, &client_address, 0);
  qa_app_q3_server_network_poll(network, 0, &count);
  push_connect(&transport, &client_address, 13);
  qa_app_q3_server_network_poll(network, 1, &count);
  CHECK(count == 0);
  CHECK(f->print_count > 0);
  wire_send(channel, &transport, &client_address, NULL, 0, NULL, 0);
  qa_app_q3_server_network_poll(network, 2, &count);
  const int32_t times[2] = { 10, 20 };
  wire_send(channel, &transport, &client_address, times, 2, NULL, 0);
  push_raw(&transport, &client_address, "getinfo", 7);
  qa_app_q3_server_network_poll(network, 3, &count);
  qa_app_q3_player clients[2];
  CHECK(qa_app_q3_server_network_clients(network, clients, 2) == 0);
  CHECK(qa_app_q3_server_network_phase(network) == QA_APP_PHASE_ACTIVE);
  CHECK(f->call_count == 2 && strcmp(f->calls[0], "begin") == 0 && strcmp(f->calls[1], "status") == 0);
  if (f->call_count != 2) {
    fprintf(stderr, "recovery calls:");
    for (size_t i = 0; i < f->call_count; i++) fprintf(stderr, " [%s]", f->calls[i]);
    fprintf(stderr, "\n");
  }
  CHECK(printed_contains(f, "guest memory access failed"));
  qa_app_q3_server_network_close(network);
  qa_q3_netchannel_destroy(channel);
  qa_app_q3_server_network_destroy(network);
  ft_destroy(&transport);
  qa_net_address_destroy(&server_address);
  qa_net_address_destroy(&client_address);
  free(f);
  qa_identity_owner_destroy(&owner);
}

/* ================= network.test.ts: rcon endpoint ================= */
static bool reply_contains(fake_transport *client, const char *needle) {
  qa_receive_event event;
  if (!ft_poll(client, &event)) return false;
  char text[512];
  size_t n = event.length > 4 ? event.length - 4 : 0;
  if (n >= sizeof text) n = sizeof text - 1;
  memcpy(text, event.payload + 4, n); text[n] = 0;
  qa_receive_event_destroy(&event);
  return strstr(text, needle) != NULL;
}
static void test_rcon_endpoint(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "q3-admin-network"));
  server_host_fixture *f = malloc(sizeof *f);
  server_host_init(f, &owner);
  qa_net_address server_address = net_loopback("admin-server"), client_address = net_loopback("admin-client");
  fake_transport transport, client;
  ft_init(&transport, &server_address);
  ft_init(&client, &client_address);
  transport.peer = &client;
  qa_app_q3_server_network *network = server_network(&transport, f, false, true);
  size_t count = 0;
  push_text(&transport, &client_address, "rcon wrong echo \"two words\"");
  qa_app_q3_server_network_poll(network, 1000, &count);
  CHECK(reply_contains(&client, "Bad rconpassword"));
  CHECK(f->rcon_count == 0);
  push_text(&transport, &client_address, "rcon secret echo \"two words\"");
  qa_app_q3_server_network_poll(network, 2000, &count);
  CHECK(reply_contains(&client, "executed"));
  CHECK(f->rcon_count == 1 && strcmp(f->rcon[0], "echo \"two words\"") == 0);
  f->blocked = true;
  push_text(&transport, &client_address, "rcon secret echo blocked");
  qa_app_q3_server_network_poll(network, 3000, &count);
  CHECK(client.count == 0);
  CHECK(f->rcon_count == 1);
  qa_app_q3_server_network_close(network);
  qa_app_q3_server_network_destroy(network);
  ft_destroy(&transport);
  ft_destroy(&client);
  qa_net_address_destroy(&server_address);
  qa_net_address_destroy(&client_address);
  free(f);
  qa_identity_owner_destroy(&owner);
}

/* ================= network.test.ts: IPX application admission ================= */
static void test_ipx_application(void) {
  const int64_t server_node[6] = { 1, 2, 3, 4, 5, 6 }, client_node[6] = { 10, 11, 12, 13, 14, 15 };
  qa_net_address server_address = net_ipx(0x12345678, server_node, 27960), client_address = net_ipx(0x12345678, client_node, 31000);
  fake_transport server_transport, client_transport;
  ft_init(&server_transport, &server_address);
  ft_init(&client_transport, &client_address);
  server_transport.peer = &client_transport;
  client_transport.peer = &server_transport;
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "q3-ipx-application"));
  server_host_fixture *f = malloc(sizeof *f);
  server_host_init(f, &owner);
  qa_q3_server_rate rate = { 10000, 0, 50, false, true, true };
  f->rate = rate;
  f->time = 0;
  memset(f->entries, 0, sizeof f->entries);
  f->entries[0].kind = QA_Q3_GS_CONFIGSTRING; f->entries[0].index = 1; f->entries[0].value = (char *)"\\sv_serverid\\1";
  f->gamestate.entries = f->entries; f->gamestate.entry_count = 1;
  f->master = net_ipv4(192, 0, 2, 1, 27950); f->has_master = true;
  qa_app_q3_server_network_options server_options;
  memset(&server_options, 0, sizeof server_options);
  server_options.transport = &server_transport.handle;
  server_options.host = server_host(f, false, true);
  server_options.random = zero_random; server_options.random_ctx = f;
  server_options.resolve_authorization = must_not_resolve; server_options.resolve_ctx = f;
  qa_app_q3_server_network *server = qa_app_q3_server_network_create(server_options);
  CHECK(server != NULL);
  f->network = server;
  client_host_fixture c;
  memset(&c, 0, sizeof c); c.magic = MAGIC; c.userinfo = "\\name\\IPX";
  qa_app_q3_client_network_options client_options;
  memset(&client_options, 0, sizeof client_options);
  client_options.transport = client_transport.handle;
  client_options.remote = q3_of(&server_address);
  client_options.host = client_host(&c, f->player.client);
  client_options.qport = 77;
  client_options.now = ch_now; client_options.now_ctx = &c;
  qa_app_q3_client_network *client = qa_app_q3_client_network_create(client_options);
  CHECK(client != NULL);
  qa_q3_address client_q3 = q3_of(&client_address);
  CHECK(qa_q3_is_lan_address(&client_q3));
  qa_q3_mirror_address_destroy(&client_q3);
  size_t count = 0;
  qa_app_q3_client_network_poll(client, 0); qa_app_q3_server_network_poll(server, 0, &count);
  qa_app_q3_client_network_poll(client, 10); qa_app_q3_client_network_poll(client, 11);
  qa_app_q3_server_network_poll(server, 11, &count); qa_app_q3_client_network_poll(client, 20);
  c.now = 20; qa_app_q3_client_network_send_packet(client);
  qa_app_q3_server_network_poll(server, 20, &count); qa_app_q3_client_network_poll(client, 30);
  qa_app_q3_player clients[2];
  CHECK(qa_app_q3_server_network_clients(server, clients, 2) == 1);
  CHECK(c.loaded);
  CHECK(f->userinfo_count >= 1 && strstr(f->userinfo[0], "\\ip\\12345678.0a0b0c0d0e0f:31000") != NULL);
  c.now = 40; qa_app_q3_client_network_send_packet(client);
  qa_app_q3_server_network_poll(server, 40, &count);
  f->time = 1400;
  qa_app_q3_client_network_command(client, "say ipx");
  c.now = 1400; qa_app_q3_client_network_send_packet(client);
  qa_app_q3_server_network_poll(server, 1400, &count);
  size_t before_heartbeat = server_transport.sent_count;
  qa_app_q3_server_network_heartbeat(server, 1401);
  bool said = false;
  for (size_t i = 0; i < f->command_count; i++) if (strcmp(f->commands[i], "say") == 0) said = true;
  CHECK(said);
  CHECK(server_transport.sent_count > 2);
  CHECK(server_transport.sent_count == before_heartbeat); /* IPX never advertises to the IPv4 master */
  CHECK(printed_contains(f, "Q3 IPX transport supports LAN play; IPv4 master advertisement is unavailable.\n"));
  qa_app_q3_client_network_close(client);
  qa_app_q3_server_network_close(server);
  CHECK(client_transport.closed && server_transport.closed);
  qa_app_q3_client_network_destroy(client);
  qa_app_q3_server_network_destroy(server);
  qa_q3_mirror_address_destroy(&client_options.remote);
  ft_destroy(&server_transport);
  ft_destroy(&client_transport);
  qa_net_address_destroy(&f->master);
  qa_net_address_destroy(&server_address);
  qa_net_address_destroy(&client_address);
  free(f);
  qa_identity_owner_destroy(&owner);
}

int main(void) {
  test_client_print();
  test_client_admission_authorizes_wan();
  test_callback_drops();
  test_packet_recovery();
  test_rcon_endpoint();
  test_ipx_application();
  printf("test_q3_app: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
