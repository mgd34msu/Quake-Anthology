/*
 * Unit checks for src/app/bootstrap/network/{q2,q2_client_receiver,q2_downloads,remote}.c, ported
 * from ../quake-anthology-ts tests/network/q2/application-{close,control}.test.ts,
 * tests/first-playable/network/q2-demo.test.ts (receiver cases) and q2-downloads.test.ts (mounted
 * downloads, the model-list skip), plus malformed-input loops over the receiver and the server.
 * Every callback checks a sentinel ctx. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/app/bootstrap/network/q2.h"
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/core/cvars/index.h"
#include "qa/core/md4.h"
#include "qa/foundation/support.h"
#include "qa/network/common/loopback.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/server_write.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
static int SENTINEL;
#define SENTINEL_OK(ctx) CHECK((ctx) != NULL && *(int **)(ctx) == &SENTINEL)

static uint32_t rng_state = 0x2545f491u;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return rng_state;
}
static const qa_q2_protocol_identity CLASSIC = {"q2-classic", 34, 0, 0};
static qa_q2_server_message_options message_options(void) {
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 2080;
  options.inventory_slots = 256;
  return options;
}

/* ---- encoded server records (q2-demo.test.ts helpers) ---- */
typedef struct bytes { uint8_t *data; size_t length; } bytes;
static void bytes_add(bytes *out, uint8_t *data, size_t length) {
  uint8_t *grown = realloc(out->data, out->length + length + 1);
  if (!grown) abort();
  memcpy(grown + out->length, data, length);
  out->data = grown; out->length += length;
  free(data);
}
static void add_event(bytes *out, const qa_q2_server_event *event) {
  qa_q2_wire_codec wire;
  if (!qa_q2_wire_codec_init(&wire, CLASSIC)) abort();
  size_t length = 0;
  uint8_t *data = qa_q2_encode_server_event(&wire, event, &length);
  qa_q2_wire_codec_destroy(&wire);
  bytes_add(out, data, length);
}
static void add_server_data(bytes *out, double servercount) {
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_DATA;
  event.data.servercount = servercount;
  event.data.attractloop = true;
  snprintf(event.data.gamedir, sizeof event.data.gamedir, "baseq2");
  event.data.clientnum = 0;
  snprintf(event.data.levelname, sizeof event.data.levelname, "base1");
  event.data.server_state = 2;
  add_event(out, &event);
}
static void add_config(bytes *out, double index, const char *value) {
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_CONFIG_STRING;
  event.index = index;
  event.text = (char *)value;
  add_event(out, &event);
}
static void add_command(bytes *out, const char *text) {
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_COMMAND_TEXT;
  event.text = (char *)text;
  add_event(out, &event);
}
static bytes preamble(void) {
  bytes out = {0};
  add_server_data(&out, 42);
  add_config(&out, 33, "maps/base1.bsp");
  return out;
}

/* ---- Q2ClientReceiver ---- */
typedef struct rx_world {
  int *sentinel;
  qa_app_q2_client_receiver *receiver;
  char commands[8][64]; size_t command_count;
  int resets, ready, records, frames, prepares;
  bool downloading, closed, close_in_server_data, close_in_game_state, external_close;
} rx_world;
static void rx_server_data(void *ctx, const void *data, void (*assert_current)(void *), void *assert_ctx) {
  SENTINEL_OK(ctx);
  rx_world *w = ctx;
  (void)data;
  if (w->external_close) w->closed = true;
  if (w->close_in_server_data) qa_app_q2_client_receiver_close(w->receiver);
  assert_current(assert_ctx);
}
static void rx_game_state(void *ctx, qa_app_q2_game_state state) {
  SENTINEL_OK(ctx);
  rx_world *w = ctx;
  CHECK(state.data != NULL && state.config_strings != NULL && state.baselines != NULL);
  w->ready++;
  if (w->close_in_game_state) qa_app_q2_client_receiver_close(w->receiver);
}
static void rx_frame(void *ctx, const void *frame, const void *const *records, size_t count, double now) {
  SENTINEL_OK(ctx);
  (void)frame; (void)records; (void)count; (void)now;
  ((rx_world *)ctx)->frames++;
}
static void rx_records(void *ctx, const void *const *records, size_t count) {
  SENTINEL_OK(ctx);
  (void)records; (void)count;
  ((rx_world *)ctx)->records++;
}
static void rx_disconnected(void *ctx, const char *reason) { SENTINEL_OK(ctx); (void)reason; }
static void rx_print(void *ctx, const char *text) { SENTINEL_OK(ctx); (void)text; }
static void rx_command(void *ctx, const char *text) {
  SENTINEL_OK(ctx);
  rx_world *w = ctx;
  if (w->command_count < 8) snprintf(w->commands[w->command_count++], 64, "%s", text);
}
static void rx_reset(void *ctx) { SENTINEL_OK(ctx); ((rx_world *)ctx)->resets++; }
static bool rx_closed(void *ctx) { SENTINEL_OK(ctx); return ((rx_world *)ctx)->closed; }
static qa_app_q2_download_preparation rx_prepare(void *ctx, const qa_app_q2_game_state *state) {
  SENTINEL_OK(ctx);
  (void)state;
  rx_world *w = ctx;
  w->prepares++;
  return w->downloading ? QA_APP_Q2_PREP_WAITING : QA_APP_Q2_PREP_READY;
}
static qa_app_q2_download_receive rx_receive(void *ctx, const qa_q2_server_event *block) {
  SENTINEL_OK(ctx);
  (void)block;
  ((rx_world *)ctx)->downloading = false;
  return QA_APP_Q2_DL_COMPLETE;
}
static void rx_downloads_close(void *ctx) { SENTINEL_OK(ctx); }
static qa_q2_server_message_options rx_options;
static qa_app_q2_client_receiver *rx_create(rx_world *w, bool demo, bool downloads) {
  rx_options = message_options();
  qa_app_q2_client_host host;
  memset(&host, 0, sizeof host);
  host.ctx = w;
  host.protocol = CLASSIC;
  host.message_options = &rx_options;
  host.server_data = rx_server_data;
  host.game_state = rx_game_state;
  host.frame = rx_frame;
  host.records = rx_records;
  host.disconnected = rx_disconnected;
  host.print = rx_print;
  qa_app_q2_receiver_source source;
  memset(&source, 0, sizeof source);
  source.is_demo = demo;
  source.ctx = w;
  source.command = rx_command;
  source.reset_commands = rx_reset;
  source.closed = rx_closed;
  if (downloads) {
    source.has_downloads = true;
    source.downloads = (qa_app_q2_receiver_downloads){w, rx_prepare, rx_receive, rx_downloads_close};
  }
  w->receiver = qa_app_q2_client_receiver_create(host, source);
  return w->receiver;
}
typedef struct receive_call {
  qa_app_q2_client_receiver *receiver; const uint8_t *data; size_t length;
  qa_q2_server_record *records; size_t count;
} receive_call;
static void receive_body(void *arg) {
  receive_call *call = arg;
  call->records = qa_app_q2_client_receiver_receive(call->receiver, call->data, call->length, 1, &call->count);
}
/* receiver.receive under the TS await: false is a rejection, its message in `caught`. */
static bool rx_receive_bytes(qa_app_q2_client_receiver *receiver, bytes data, qa_caught_error *caught) {
  receive_call call = {receiver, data.data, data.length, NULL, 0};
  bool ok = qa_try(receive_body, &call, caught);
  if (ok) qa_q2_server_records_free(call.records, call.count);
  free(data.data);
  return ok;
}
static void test_receiver_live_precache(void) {
  rx_world w = {.sentinel = &SENTINEL};
  w.downloading = true;
  qa_app_q2_client_receiver *receiver = rx_create(&w, false, true);
  qa_caught_error caught;
  CHECK(rx_receive_bytes(receiver, preamble(), &caught));
  CHECK(w.resets == 1);
  bytes bare = {0}; add_command(&bare, "precache\n");
  CHECK(!rx_receive_bytes(receiver, bare, &caught));
  CHECK(strstr(caught.message, "signon number") != NULL && strcmp(caught.name, "Error") == 0);
  bytes other = {0}; add_command(&other, "precache 41\n");
  CHECK(!rx_receive_bytes(receiver, other, &caught));
  CHECK(strstr(caught.message, "another server generation") != NULL);
  bytes signon = {0}; add_command(&signon, "cmd configstrings 42 0\ncmd baselines 42 0\nprecache 42\n");
  CHECK(rx_receive_bytes(receiver, signon, &caught));
  CHECK(w.ready == 0);
  CHECK(strcmp(qa_app_q2_client_receiver_phase(receiver), "loading") == 0);
  bytes block = {0};
  {
    qa_q2_server_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2_SERVER_DOWNLOAD;
    event.percent = 100;
    event.has_bytes = true;
    uint8_t none[1] = {0};
    event.bytes = none;
    event.bytes_length = 0;
    add_event(&block, &event);
  }
  CHECK(rx_receive_bytes(receiver, block, &caught));
  CHECK(w.ready == 1);
  CHECK(w.command_count == 3);
  CHECK(strcmp(w.commands[0], "configstrings 42 0") == 0);
  CHECK(strcmp(w.commands[1], "baselines 42 0") == 0);
  CHECK(strcmp(w.commands[2], "begin 42") == 0);
  CHECK(strcmp(qa_app_q2_client_receiver_phase(receiver), "active") == 0);
  /* DemoRecordingSeed: server-data, the one config string, precache; no baselines or frames. */
  qa_app_q2_recording_seed seed;
  qa_app_q2_client_receiver_seed(receiver, &seed);
  CHECK(strcmp(seed.protocol.kind, "q2-classic") == 0 && seed.packet_count == 3);
  if (seed.packet_count == 3) CHECK(seed.packets[0].message[0] == 12); /* svc_serverdata */
  qa_app_q2_recording_seed_free(&seed);
  qa_app_q2_client_receiver_close(receiver);
  CHECK(strcmp(qa_app_q2_client_receiver_phase(receiver), "closed") == 0);
  size_t count = 1;
  bytes again = preamble();
  CHECK(qa_app_q2_client_receiver_receive(receiver, again.data, again.length, 5, &count) == NULL && count == 0);
  free(again.data);
  qa_app_q2_client_receiver_destroy(receiver);
}
static void test_receiver_external_close(void) {
  rx_world w = {.sentinel = &SENTINEL};
  w.external_close = true;
  qa_app_q2_client_receiver *receiver = rx_create(&w, false, false);
  qa_caught_error caught;
  CHECK(!rx_receive_bytes(receiver, preamble(), &caught));
  CHECK(strstr(caught.message, "retired") != NULL);
  qa_app_q2_client_receiver_destroy(receiver);
}
/* `closing during serverData|gameState rejects stale delivery`, on the receiver Q2DemoPlayback
 * drives: the close lands inside the host callback instead of across an await. */
static void test_receiver_close_during_boundary(bool server_data) {
  rx_world w = {.sentinel = &SENTINEL};
  w.close_in_server_data = server_data;
  w.close_in_game_state = !server_data;
  qa_app_q2_client_receiver *receiver = rx_create(&w, true, false);
  bytes data = preamble();
  add_command(&data, "precache\n");
  int before = w.records;
  qa_caught_error caught;
  CHECK(!rx_receive_bytes(receiver, data, &caught));
  CHECK(strstr(caught.message, "retired") != NULL);
  CHECK(w.records == before && w.frames == 0);
  qa_app_q2_client_receiver_destroy(receiver);
}
/* Truncated and mutated server packets either decode or raise; neither crashes nor leaks. */
static void test_receiver_malformed(void) {
  bytes base = preamble();
  add_command(&base, "cmd configstrings 42 0\nprecache 42\n");
  add_config(&base, 1520, "male/grunt");
  for (int iteration = 0; iteration < 400; iteration++) {
    rx_world w = {.sentinel = &SENTINEL};
    qa_app_q2_client_receiver *receiver = rx_create(&w, iteration & 1, false);
    bytes copy = {malloc(base.length + 1), base.length};
    memcpy(copy.data, base.data, base.length);
    if (iteration % 3 == 0) copy.length = rng() % (base.length + 1);
    int flips = 1 + (int)(rng() % 4);
    for (int i = 0; i < flips && copy.length; i++) copy.data[rng() % copy.length] = (uint8_t)rng();
    qa_caught_error caught;
    (void)rx_receive_bytes(receiver, copy, &caught);
    qa_app_q2_client_receiver_destroy(receiver);
  }
  free(base.data);
  CHECK(true);
}

/* ---- Q2ServerNetwork / Q2ClientNetwork over loopback (application-close, -control) ---- */
static double clock_now(void *ctx) { (void)ctx; return 0; }
typedef struct net_world {
  int *sentinel;
  qa_app_q2_player players[2];
  qa_q2_server_data_result data[2];
  qa_app_q2_config_snapshot configs;
  qa_q2_baseline_list baselines;
  int admitted;
  int disconnected[8]; size_t disconnect_count;
  bool disconnect_raises, reject;
  char executed[4][64]; size_t executed_count;
  const char *password;
  qa_net_address master;
  char prints[1024];
} net_world;
static qa_app_q2_admission srv_admit(void *ctx, const void *from, const void *request) {
  SENTINEL_OK(ctx);
  (void)from; (void)request;
  net_world *w = ctx;
  qa_app_q2_admission admission;
  memset(&admission, 0, sizeof admission);
  if (w->reject) { admission.reason = "No game clients in this control test"; return admission; }
  if (w->admitted >= 2) qa_raise("Error", 0, 0, "Unexpected admission");
  admission.accepted = true;
  admission.player = w->players[w->admitted++];
  return admission;
}
static void srv_disconnect(void *ctx, qa_app_q2_player player, const char *reason) {
  SENTINEL_OK(ctx);
  (void)reason;
  net_world *w = ctx;
  if (w->disconnect_count < 8) w->disconnected[w->disconnect_count++] = (int)player.client.slot;
  if (w->disconnect_raises)
    qa_raise("Error", 0, 0, player.client.slot == 0 ? "first disconnect failure" : "second disconnect failure");
}
static qa_app_q2_player srv_carried(void *ctx, qa_client_id client) {
  SENTINEL_OK(ctx);
  net_world *w = ctx;
  return w->players[client.slot & 1];
}
static qa_app_q2_game_state srv_game_state(void *ctx, qa_app_q2_player player, const qa_q2_protocol_identity *protocol) {
  SENTINEL_OK(ctx);
  (void)protocol;
  net_world *w = ctx;
  qa_app_q2_game_state state = {&w->data[player.client.slot & 1], &w->configs, &w->baselines};
  return state;
}
static const void *srv_frame(void *ctx, qa_app_q2_player player, const void *output, const qa_q2_protocol_identity *protocol) {
  SENTINEL_OK(ctx);
  (void)player; (void)output; (void)protocol;
  qa_raise("Error", 0, 0, "No frames in shutdown fixture");
}
static const qa_app_q2_server_event *srv_events(void *ctx, qa_app_q2_player player, const void *output,
    const void *const *events, size_t count, size_t *out_count) {
  SENTINEL_OK(ctx);
  (void)player; (void)output; (void)events; (void)count;
  *out_count = 0;
  return NULL;
}
static const void *srv_input(void *ctx, qa_app_q2_player player, const void *command, double sequence) {
  SENTINEL_OK(ctx);
  (void)player; (void)command; (void)sequence;
  return NULL;
}
static void srv_command(void *ctx, qa_app_q2_player player, const char *name, const char *const *args, size_t count) {
  SENTINEL_OK(ctx);
  (void)player; (void)name; (void)args; (void)count;
}
static void srv_userinfo(void *ctx, qa_app_q2_player player, const char *value) {
  SENTINEL_OK(ctx);
  (void)player; (void)value;
}
static void srv_print(void *ctx, const char *text) {
  SENTINEL_OK(ctx);
  net_world *w = ctx;
  strncat(w->prints, text, sizeof w->prints - strlen(w->prints) - 1);
}
static void srv_observe(void *ctx, const void *output, const void *const *events, size_t count) {
  SENTINEL_OK(ctx);
  (void)output; (void)events; (void)count;
}
static const void *const *srv_masters(void *ctx, size_t *out_count) {
  SENTINEL_OK(ctx);
  net_world *w = ctx;
  static const void *const unused = NULL;
  (void)unused;
  *out_count = 1;
  return (const void *const *)(const void *)&w->master;
}
static double fixed_random(void *ctx) { SENTINEL_OK(ctx); return 0.5; }
static qa_app_q2_server_host server_host(net_world *w) {
  static qa_q2_server_message_options options;
  options = message_options();
  qa_app_q2_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = w;
  host.protocol = CLASSIC;
  host.message_options = &options;
  host.max_clients = 2;
  host.admit = srv_admit;
  host.disconnect = srv_disconnect;
  host.carried_player = srv_carried;
  host.game_state = srv_game_state;
  host.frame = srv_frame;
  host.events = srv_events;
  host.input = srv_input;
  host.command = srv_command;
  host.userinfo = srv_userinfo;
  host.print = srv_print;
  host.observe = srv_observe;
  return host;
}
static void net_world_init(net_world *w) {
  memset(w, 0, sizeof *w);
  w->sentinel = &SENTINEL;
  for (int slot = 0; slot < 2; slot++) {
    w->players[slot].client.slot = slot;
    w->players[slot].actor.slot = slot + 1;
    w->players[slot].source_entity = slot + 1;
    w->data[slot].servercount = 1;
    snprintf(w->data[slot].gamedir, sizeof w->data[slot].gamedir, "baseq2");
    w->data[slot].clientnum = slot;
    snprintf(w->data[slot].levelname, sizeof w->data[slot].levelname, "fixture");
    w->data[slot].server_state = 2;
  }
}
typedef struct client_world { int *sentinel; int disconnects; } client_world;
static const char *cl_userinfo(void *ctx) { SENTINEL_OK(ctx); return "\\name\\fixture"; }
static void cl_game_state(void *ctx, qa_app_q2_game_state state) { SENTINEL_OK(ctx); (void)state; }
static void cl_frame(void *ctx, const void *frame, const void *const *records, size_t count, double now) {
  SENTINEL_OK(ctx);
  (void)frame; (void)records; (void)count; (void)now;
}
static void cl_records(void *ctx, const void *const *records, size_t count) {
  SENTINEL_OK(ctx);
  (void)records; (void)count;
}
static const void *cl_command(void *ctx, const void *command) {
  SENTINEL_OK(ctx);
  (void)command;
  static qa_q2_wire_usercmd usercmd;
  return &usercmd;
}
static void cl_disconnected(void *ctx, const char *reason) {
  SENTINEL_OK(ctx);
  (void)reason;
  ((client_world *)ctx)->disconnects++;
}
static void cl_print(void *ctx, const char *text) { SENTINEL_OK(ctx); (void)text; }
static qa_app_q2_client_host client_host(client_world *w) {
  static qa_q2_server_message_options options;
  options = message_options();
  qa_app_q2_client_host host;
  memset(&host, 0, sizeof host);
  host.ctx = w;
  host.protocol = CLASSIC;
  host.message_options = &options;
  host.userinfo = cl_userinfo;
  host.game_state = cl_game_state;
  host.frame = cl_frame;
  host.records = cl_records;
  host.command = cl_command;
  host.disconnected = cl_disconnected;
  host.print = cl_print;
  return host;
}
typedef struct close_call { qa_app_q2_server_network *server; } close_call;
static void close_body(void *arg) { qa_app_q2_server_network_close(((close_call *)arg)->server); }
typedef struct loop_fixture {
  qa_loopback_hub hub;
  qa_loopback_transport *server_endpoint, *client_endpoints[2];
  qa_datagram_transport server_transport, client_transports[2];
  qa_app_q2_server_network *server;
  qa_app_q2_client_network *clients[2];
  client_world client_worlds[2];
} loop_fixture;
static void loop_open(loop_fixture *f, net_world *w) {
  memset(f, 0, sizeof *f);
  qa_loopback_hub_init(&f->hub, NULL, clock_now, NULL);
  f->server_endpoint = qa_loopback_hub_bind(&f->hub, "server");
  f->server_transport = qa_loopback_transport_as_transport(f->server_endpoint);
  qa_app_q2_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &f->server_transport;
  options.host = server_host(w);
  options.random = fixed_random;
  options.random_ctx = w;
  f->server = qa_app_q2_server_network_create(options);
  for (int slot = 0; slot < 2; slot++) {
    char id[32];
    snprintf(id, sizeof id, "client%d", slot);
    f->client_endpoints[slot] = qa_loopback_hub_bind(&f->hub, id);
    f->client_transports[slot] = qa_loopback_transport_as_transport(f->client_endpoints[slot]);
    f->client_worlds[slot].sentinel = &SENTINEL;
    qa_app_q2_client_network_options client;
    memset(&client, 0, sizeof client);
    client.transport = &f->client_transports[slot];
    client.remote = &f->server_endpoint->address;
    client.qport = 100 + slot;
    client.host = client_host(&f->client_worlds[slot]);
    f->clients[slot] = qa_app_q2_client_network_create(client);
  }
}
static bool loop_signon(loop_fixture *f) {
  for (int turn = 0; turn < 40; turn++) {
    bool all = true;
    for (int slot = 0; slot < 2; slot++)
      all &= qa_app_q2_client_network_phase(f->clients[slot]) == QA_APP_PHASE_ACTIVE;
    if (all) return true;
    size_t count = 0;
    for (int slot = 0; slot < 2; slot++) qa_app_q2_client_network_poll(f->clients[slot], turn * 100, &count);
    qa_app_q2_server_network_poll(f->server, turn * 100, &count);
  }
  return false;
}
static void loop_close(loop_fixture *f) {
  for (int slot = 0; slot < 2; slot++) {
    qa_app_q2_client_network_close(f->clients[slot]);
    qa_app_q2_client_network_destroy(f->clients[slot]);
  }
  qa_app_q2_server_network_destroy(f->server);
  qa_loopback_hub_close(&f->hub);
  qa_loopback_hub_destroy(&f->hub);
}
static void test_application_close(void) {
  net_world w;
  net_world_init(&w);
  loop_fixture f;
  loop_open(&f, &w);
  CHECK(loop_signon(&f));
  CHECK(qa_app_q2_client_network_phase(f.clients[0]) == QA_APP_PHASE_ACTIVE);
  CHECK(qa_app_q2_client_network_phase(f.clients[1]) == QA_APP_PHASE_ACTIVE);
  qa_app_q2_player listed[4];
  CHECK(qa_app_q2_server_network_clients(f.server, listed, 4) == 2);
  w.disconnect_raises = true;
  close_call call = {f.server};
  qa_caught_error caught;
  CHECK(!qa_try(close_body, &call, &caught));
  CHECK(strcmp(caught.name, "AggregateError") == 0);
  CHECK(strcmp(caught.message, "Q2 server shutdown failed") == 0);
  CHECK(w.disconnect_count == 2 && w.disconnected[0] == 0 && w.disconnected[1] == 1);
  CHECK(qa_loopback_transport_closed(f.server_endpoint));
  CHECK(qa_try(close_body, &call, &caught));
  CHECK(w.disconnect_count == 2);
  CHECK(qa_app_q2_server_network_phase(f.server) == QA_APP_PHASE_CLOSED);
  loop_close(&f);
}
/* A peer whose packets are garbage is dropped or survives; the server never crashes or leaks. */
static void test_server_malformed_packets(void) {
  net_world w;
  net_world_init(&w);
  loop_fixture f;
  loop_open(&f, &w);
  CHECK(loop_signon(&f));
  uint8_t packet[96];
  for (int iteration = 0; iteration < 600; iteration++) {
    size_t length = rng() % sizeof packet;
    for (size_t i = 0; i < length; i++) packet[i] = (uint8_t)rng();
    int mode = iteration % 4;
    if (mode == 0 && length >= 4) memset(packet, 0xff, 4);
    if (mode == 1 && length >= 10) { packet[8] = 100; packet[9] = 0; } /* client 0's qport */
    if (mode == 2) {
      static const char *texts[] = {"\xff\xff\xff\xff" "connect 34 100 1 \"\\name\\x\"",
          "\xff\xff\xff\xff" "getchallenge", "\xff\xff\xff\xff" "rcon x status",
          "\xff\xff\xff\xff" "info 34", "\xff\xff\xff\xff" "connect 34 7 99999 \"\\name\\x\" 1400 1"};
      const char *text = texts[rng() % 5];
      length = strlen(text);
      memcpy(packet, text, length);
      if (length) packet[rng() % length] ^= (uint8_t)(rng() & 0x1f);
    }
    qa_loopback_transport_send(f.client_endpoints[iteration & 1], &f.server_endpoint->address, packet, length);
    size_t count = 0;
    qa_app_q2_server_network_poll(f.server, 4000 + iteration, &count);
  }
  CHECK(true);
  loop_close(&f);
}
/* application-control: rcon authentication, master heartbeat and shutdown. The TS case also awaits
 * an asynchronous executeRcon; the C execute_rcon is synchronous, so the poll completes at once. */
static const char *rcon_password(void *ctx) { SENTINEL_OK(ctx); return ((net_world *)ctx)->password; }
static bool rcon_limited(void *ctx, const char **password, const char ***prefixes, size_t *count) {
  SENTINEL_OK(ctx);
  static const char **none = NULL;
  *password = ""; *prefixes = none; *count = 0;
  return true;
}
static bool rcon_rate(void *ctx, double now) { SENTINEL_OK(ctx); (void)now; return true; }
static void rcon_recharge(void *ctx) { SENTINEL_OK(ctx); }
static void rcon_execute(void *ctx, const char *command, bool limited,
    void (*output)(void *octx, const char *text), void *output_ctx) {
  SENTINEL_OK(ctx);
  (void)limited;
  net_world *w = ctx;
  if (w->executed_count < 4) snprintf(w->executed[w->executed_count++], 64, "%s", command);
  output(output_ctx, "finished\n");
}
static qa_q2_status discovery_status(void *ctx) {
  SENTINEL_OK(ctx);
  qa_q2_status status = {"\\hostname\\Control\\mapname\\base1", NULL, 0};
  return status;
}
static qa_q2_host_info discovery_info(void *ctx) {
  SENTINEL_OK(ctx);
  qa_q2_host_info info = {"Control", "base1", 0, 1};
  return info;
}
static bool poll_oob(qa_loopback_transport *endpoint, qa_q2_connectionless_message *out, char *raw, size_t raw_size) {
  qa_receive_event event;
  memset(&event, 0, sizeof event);
  if (!qa_loopback_transport_poll(endpoint, &event)) return false;
  bool ok = event.kind == QA_RECV_PACKET && qa_q2_read_out_of_band(event.payload, event.length, false, out);
  if (raw && event.kind == QA_RECV_PACKET) {
    size_t n = event.length < raw_size - 1 ? event.length : raw_size - 1;
    memcpy(raw, event.payload, n);
    for (size_t i = 0; i < n; i++) if (!raw[i]) raw[i] = ' ';
    raw[n] = '\0';
  }
  qa_receive_event_destroy(&event);
  return ok;
}
static void send_text(qa_loopback_transport *from, const qa_net_address *to, const char *text) {
  size_t length = 0;
  uint8_t *packet = qa_q2_out_of_band(text, false, &length);
  qa_loopback_transport_send(from, to, packet, length);
  free(packet);
}
static void test_application_control(void) {
  net_world w;
  net_world_init(&w);
  w.reject = true;
  w.password = "first";
  qa_loopback_hub hub;
  qa_loopback_hub_init(&hub, NULL, clock_now, NULL);
  qa_loopback_transport *endpoint = qa_loopback_hub_bind(&hub, "server");
  qa_loopback_transport *client = qa_loopback_hub_bind(&hub, "client");
  qa_loopback_transport *master = qa_loopback_hub_bind(&hub, "master");
  w.master = master->address;
  qa_datagram_transport transport = qa_loopback_transport_as_transport(endpoint);
  qa_q2_connectionless_host administration;
  memset(&administration, 0, sizeof administration);
  administration.ctx = &w;
  administration.rcon_password = rcon_password;
  administration.limited_rcon = rcon_limited;
  administration.rcon_rate_allowed = rcon_rate;
  administration.recharge_rcon_rate = rcon_recharge;
  administration.execute_rcon = rcon_execute;
  qa_app_q2_discovery discovery = {&w, discovery_status, discovery_info};
  qa_app_q2_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &transport;
  options.host = server_host(&w);
  options.host.max_clients = 1;
  options.host.masters = srv_masters;
  options.host.discovery = &discovery;
  options.host.administration = &administration;
  options.random = fixed_random;
  options.random_ctx = &w;
  qa_app_q2_server_network *server = qa_app_q2_server_network_create(options);
  {
    /* TS this.wire = { kind: "source", protocol: options.host.protocol } */
    qa_app_application_network as_network = qa_app_q2_server_network_as_network(server);
    qa_wire_selection wire = as_network.wire(as_network.ctx);
    CHECK(!wire.is_unified && strcmp(wire.protocol.kind, options.host.protocol.kind) == 0 &&
          wire.protocol.version == options.host.protocol.version);
  }
  send_text(client, &endpoint->address, "rcon wrong status");
  size_t count = 0;
  qa_app_q2_server_network_poll(server, 0, &count);
  CHECK(w.executed_count == 0);
  qa_q2_connectionless_message message;
  char raw[512];
  CHECK(poll_oob(client, &message, NULL, 0));
  CHECK(message.body && strstr(message.body, "Bad rcon_password") != NULL);
  qa_q2_connectionless_message_free(&message);
  CHECK(poll_oob(master, &message, raw, sizeof raw));
  CHECK(strcmp(message.command, "heartbeat") == 0);
  CHECK(strstr(raw, "\\hostname\\Control") != NULL);
  qa_q2_connectionless_message_free(&message);
  w.password = "second";
  send_text(client, &endpoint->address, "rcon second status");
  qa_app_q2_server_network_poll(server, 100, &count);
  CHECK(w.executed_count == 1 && strcmp(w.executed[0], "status ") == 0);
  CHECK(poll_oob(client, &message, NULL, 0));
  CHECK(message.body && strcmp(message.body, "finished\n") == 0);
  qa_q2_connectionless_message_free(&message);
  CHECK(!poll_oob(master, &message, NULL, 0));
  qa_app_q2_server_network_close(server);
  CHECK(poll_oob(master, &message, NULL, 0));
  CHECK(strcmp(message.command, "shutdown") == 0);
  qa_q2_connectionless_message_free(&message);
  qa_app_q2_server_network_destroy(server);
  qa_loopback_hub_close(&hub);
  qa_loopback_hub_destroy(&hub);
}

/* ---- createQ2ApplicationDownloads / Q2PeerDownload (q2-downloads.test.ts) ---- */
typedef struct mount_world {
  int *sentinel;
  qa_loose_mount loose;
  qa_resolved_resource_reference reference;
  const char *installed[8]; size_t installed_count; /* exists() */
  const uint8_t *map; size_t map_length; const char *map_path;
  const char *write_root;
} mount_world;
static bool same_ci(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (x != y) return false;
  }
  return *a == *b;
}
static bool mount_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length,
    const qa_resolved_resource_reference **out_reference) {
  SENTINEL_OK(ctx);
  mount_world *w = ctx;
  size_t length;
  const uint8_t *source = NULL;
  if (w->map_path && same_ci(path, w->map_path)) { source = w->map; length = w->map_length; }
  else if (same_ci(path, "maps/custom.bsp")) length = 2051;
  else if (same_ci(path, "sound/empty.wav")) length = 0;
  else return false;
  *out_bytes = calloc(1, length ? length : 1);
  if (source) memcpy(*out_bytes, source, length);
  *out_length = length;
  w->reference.requested_path = path;
  *out_reference = &w->reference;
  return true;
}
static bool mount_exists(void *ctx, const char *path) {
  SENTINEL_OK(ctx);
  mount_world *w = ctx;
  for (size_t i = 0; i < w->installed_count; i++) if (same_ci(path, w->installed[i])) return true;
  return false;
}
static void mount_invalidate(void *ctx) { SENTINEL_OK(ctx); }
static void mount_world_init(mount_world *w) {
  memset(w, 0, sizeof *w);
  w->sentinel = &SENTINEL;
  w->loose.identity.id = "mount:download:loose";
  w->loose.identity.content = "q2:classic:baseq2:installed";
  w->loose.root_path = "/nonexistent";
  w->reference.provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  w->reference.provenance.loose_mount = &w->loose;
}
static qa_app_q2_mounts mounts_of(mount_world *w) {
  qa_app_q2_mounts mounts = {w, mount_open, mount_exists, mount_invalidate};
  return mounts;
}
static qa_cvar_registry *cvars_create(qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void cvar_set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_set(cvars, name, value, false, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static char *upper(const char *text) {
  char *out = malloc(strlen(text) + 1);
  strcpy(out, text);
  for (char *p = out; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
  return out;
}
static void free_event(qa_q2_server_event *event) {
  if (!event) return;
  qa_q2_server_event_clear(event);
  free(event);
}
static void test_mounted_downloads(void) {
  mount_world w;
  mount_world_init(&w);
  qa_cvar_registry *cvars = cvars_create(QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_app_q2_downloads *downloads = qa_app_create_q2_application_downloads(mounts_of(&w), cvars, false);
  qa_app_q2_peer_download *peer = qa_app_q2_peer_download_create();
  cvar_set(cvars, "allow_download", "1");
  CHECK(!qa_app_q2_downloads_allowed(downloads, "players/male/grunt.pcx"));
  static const char *const toggles[][2] = {{"allow_download_maps", "maps/custom.bsp"},
      {"allow_download_models", "models/custom.md2"}, {"allow_download_sounds", "sound/empty.wav"}};
  for (size_t i = 0; i < 3; i++) {
    cvar_set(cvars, toggles[i][0], "0");
    char *name = upper(toggles[i][1]);
    CHECK(!qa_app_q2_downloads_allowed(downloads, name));
    free(name);
    cvar_set(cvars, toggles[i][0], "1");
  }
  static const char *const refused[] = {"../secret.cfg", "/sound/empty.wav", "sound\\empty.wav",
      ".private/file", "config.cfg", "sound/../empty.wav"};
  for (size_t i = 0; i < 6; i++) CHECK(!qa_app_q2_downloads_allowed(downloads, refused[i]));
  qa_q2_server_event *first = qa_app_q2_peer_download_begin(peer, downloads, "maps/custom.bsp", NULL);
  CHECK(first && first->has_bytes && first->bytes_length == 1024 && first->percent == 49);
  free_event(first);
  qa_q2_server_event *negative = qa_app_q2_peer_download_begin(peer, downloads, "maps/custom.bsp", "-1");
  CHECK(negative && negative->kind == QA_Q2_SERVER_DOWNLOAD && !negative->has_bytes && negative->percent == 0);
  free_event(negative);
  qa_q2_server_event *next = qa_app_q2_peer_download_next(peer);
  CHECK(next && next->percent == 99);
  free_event(next);
  qa_app_q2_peer_download_close(peer);
  CHECK(qa_app_q2_peer_download_next(peer) == NULL);
  qa_q2_server_event *empty = qa_app_q2_peer_download_begin(peer, downloads, "sound/empty.wav", NULL);
  CHECK(empty != NULL);
  if (empty) {
    qa_q2_wire_codec wire;
    qa_q2_wire_codec_init(&wire, CLASSIC);
    size_t length = 0;
    uint8_t *encoded = qa_q2_encode_server_event(&wire, empty, &length);
    CHECK(length == 4 && encoded[0] == 16 && encoded[1] == 0 && encoded[2] == 0 && encoded[3] == 0);
    free(encoded);
    qa_q2_wire_codec_destroy(&wire);
  }
  free_event(empty);
  CHECK(qa_app_q2_peer_download_next(peer) == NULL);
  qa_cvar_registry *rerelease = cvars_create(QA_COMMAND_DIALECT_Q2_RERELEASE);
  qa_app_q2_downloads *modern = qa_app_create_q2_application_downloads(mounts_of(&w), rerelease, true);
  char *allow = qa_cvar_registry_variable_string(rerelease, "allow_download");
  char *players = qa_cvar_registry_variable_string(rerelease, "allow_download_players");
  CHECK(allow && strcmp(allow, "1") == 0 && players && strcmp(players, "1") == 0);
  free(allow); free(players);
  qa_app_q2_downloads_destroy(modern);
  qa_cvar_registry_destroy(rerelease);
  qa_app_q2_peer_download_destroy(peer);
  qa_app_q2_downloads_destroy(downloads);
  qa_cvar_registry_destroy(cvars);
}

/* ---- Q2DownloadReceiver ---- */
typedef struct dl_world {
  int *sentinel;
  qa_app_q2_remote_mounts content;
  char requests[8][96]; size_t request_count;
  char prints[2048];
} dl_world;
static const qa_app_q2_remote_mounts *dl_content(void *ctx) { SENTINEL_OK(ctx); return &((dl_world *)ctx)->content; }
static void dl_command(void *ctx, const char *text) {
  SENTINEL_OK(ctx);
  dl_world *w = ctx;
  if (w->request_count < 8) snprintf(w->requests[w->request_count++], 96, "%s", text);
}
static void dl_print(void *ctx, const char *text) {
  SENTINEL_OK(ctx);
  dl_world *w = ctx;
  strncat(w->prints, text, sizeof w->prints - strlen(w->prints) - 1);
}
/* A small IBSP the Q2 reader accepts: version 38, one plane, node, leaf, model and area, no
 * textures. */
static void put32(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)value; at[1] = (uint8_t)(value >> 8); at[2] = (uint8_t)(value >> 16); at[3] = (uint8_t)(value >> 24);
}
static uint8_t *minimal_bsp(size_t *length) {
  enum { HEADER = 8 + 19 * 8, PLANE = 20, NODE = 28, LEAF = 28, MODEL = 48, AREA = 8 };
  *length = HEADER + PLANE + NODE + LEAF + MODEL + AREA;
  uint8_t *out = calloc(1, *length);
  memcpy(out, "IBSP", 4);
  put32(out + 4, 38);
  size_t at = HEADER;
  const struct { int lump; size_t size; } lumps[] = {{1, PLANE}, {4, NODE}, {8, LEAF}, {13, MODEL}, {17, AREA}};
  for (size_t i = 0; i < 5; i++) {
    put32(out + 8 + lumps[i].lump * 8, (uint32_t)at);
    put32(out + 12 + lumps[i].lump * 8, (uint32_t)lumps[i].size);
    if (lumps[i].lump == 4) { put32(out + at + 4, 0xffffffffu); put32(out + at + 8, 0xffffffffu); }
    if (lumps[i].lump == 8) { put32(out + at, 1); out[at + 4] = 0xff; out[at + 5] = 0xff; } /* solid, cluster -1 */
    at += lumps[i].size;
  }
  return out;
}
typedef struct prepare_call {
  qa_app_q2_download_receiver *receiver; const qa_app_q2_game_state *state;
  qa_app_q2_download_preparation result;
} prepare_call;
static void prepare_body(void *arg) {
  prepare_call *call = arg;
  call->result = qa_app_q2_download_receiver_prepare(call->receiver, call->state);
}
static void rm_tree(const char *root) {
  char command[512];
  snprintf(command, sizeof command, "rm -rf '%s'", root);
  if (system(command) != 0) fprintf(stderr, "cleanup failed: %s\n", root);
}
static void test_download_receiver(void) {
  char root[256], write_root[300];
  snprintf(root, sizeof root, "build/agents/network2-q2e/tmp/q2-units-%d", (int)getpid());
  snprintf(write_root, sizeof write_root, "%s/baseq2", root);
  mount_world mounts;
  mount_world_init(&mounts);
  size_t map_length = 0;
  uint8_t *map = minimal_bsp(&map_length);
  mounts.map = map; mounts.map_length = map_length; mounts.map_path = "maps/skip.bsp";
  mounts.installed[mounts.installed_count++] = "maps/skip.bsp";
  mounts.installed[mounts.installed_count++] = "sprites/s_bfg1.sp2";
  dl_world w;
  memset(&w, 0, sizeof w);
  w.sentinel = &SENTINEL;
  w.content.mounts = mounts_of(&mounts);
  w.content.selection_base = "q2-classic-baseq2";
  w.content.selection_directory = "baseq2";
  w.content.write_root = write_root;
  w.content.base_write_root = write_root;
  qa_app_q2_download_receiver *receiver = qa_app_q2_download_receiver_create(dl_content, &w,
      dl_command, &w, dl_print, &w, NULL, NULL, NULL);
  char checksum[32];
  snprintf(checksum, sizeof checksum, "%u", (unsigned)qa_block_checksum(map, map_length));
  /* a model list with a sprite and an unnameable resource is skipped, not fatal */
  qa_q2_server_data_result data;
  memset(&data, 0, sizeof data);
  data.servercount = 1;
  snprintf(data.gamedir, sizeof data.gamedir, "baseq2");
  qa_q2_config_string_entry entries[] = {{31, checksum}, {33, "maps/skip.bsp"},
      {34, "sprites/s_bfg1.sp2"}, {35, "models/bad name/tris.md2"}};
  qa_app_q2_config_snapshot configs = {entries, 4};
  qa_q2_baseline_list baselines;
  memset(&baselines, 0, sizeof baselines);
  qa_app_q2_game_state state = {&data, &configs, &baselines};
  prepare_call call = {receiver, &state, QA_APP_Q2_PREP_CANCELED};
  qa_caught_error caught;
  bool ok = qa_try(prepare_body, &call, &caught);
  CHECK(ok);
  if (!ok) fprintf(stderr, "prepare raised %s: %s\n", caught.name, caught.message);
  CHECK(call.result == QA_APP_Q2_PREP_READY);
  CHECK(w.request_count == 0);
  CHECK(strstr(w.prints, "bad name") != NULL);
  /* A missing asset requests a native download; a refused block retires it for this state. */
  qa_q2_config_string_entry wanted[] = {{31, checksum}, {33, "maps/skip.bsp"}, {35, "models/wanted/tris.md2"}};
  qa_app_q2_config_snapshot wanted_configs = {wanted, 3};
  qa_q2_server_data_result data2 = data;
  qa_app_q2_game_state second = {&data2, &wanted_configs, &baselines};
  call.state = &second;
  w.prints[0] = '\0';
  CHECK(qa_try(prepare_body, &call, &caught) && call.result == QA_APP_Q2_PREP_WAITING);
  CHECK(w.request_count == 1 && strcmp(w.requests[0], "download models/wanted/tris.md2") == 0);
  qa_app_client_download_progress progress[4];
  CHECK(qa_app_q2_download_receiver_progress(receiver, progress, 4) == 1);
  qa_q2_server_event block;
  memset(&block, 0, sizeof block);
  block.kind = QA_Q2_SERVER_DOWNLOAD;
  block.has_bytes = false;
  CHECK(qa_app_q2_download_receiver_receive(receiver, &block) == QA_APP_Q2_DL_COMPLETE);
  CHECK(strstr(w.prints, "Server download unavailable: models/wanted/tris.md2\n") != NULL);
  CHECK(qa_try(prepare_body, &call, &caught) && call.result == QA_APP_Q2_PREP_READY);
  CHECK(w.request_count == 1);
  CHECK(qa_app_q2_download_receiver_receive(receiver, &block) == QA_APP_Q2_DL_UNSOLICITED);
  /* A world whose checksum differs is Com_Error(ERR_DROP). */
  qa_q2_config_string_entry stale[] = {{31, "1"}, {33, "maps/skip.bsp"}};
  qa_app_q2_config_snapshot stale_configs = {stale, 2};
  qa_q2_server_data_result data3 = data;
  qa_app_q2_game_state third = {&data3, &stale_configs, &baselines};
  call.state = &third;
  CHECK(!qa_try(prepare_body, &call, &caught));
  CHECK(strcmp(caught.name, "Q2ServerDropError") == 0);
  CHECK(strncmp(caught.message, "Local map version differs from server: ", 39) == 0);
  qa_app_q2_download_receiver_destroy(receiver);
  free(map);
  rm_tree(root);
}

int main(void) {
  if (mkdir("build/agents/network2-q2e/tmp", 0777) != 0) { /* may already exist */ }
  test_receiver_live_precache();
  test_receiver_external_close();
  test_receiver_close_during_boundary(true);
  test_receiver_close_during_boundary(false);
  test_receiver_malformed();
  test_application_close();
  test_server_malformed_packets();
  test_application_control();
  test_mounted_downloads();
  test_download_receiver();
  printf("test_q2_app_units: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
