/*
 * Port of ../quake-anthology-ts/tests/network/q3/round-restart.test.ts over
 * qa_app_q3_server_network_restart_source_round with a native Q3 client
 * connection on an in-memory loopback. One TS check is not reproduced: calling
 * the captured round's bindSource after restartSourceRound resolved ("scope has
 * ended"); the C round handle lives on the restart call's stack, so it cannot be
 * used after that call returns. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/q3.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
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
#define MAGIC 0x7c15a2f3u
static void *sentinel(void *ctx) {
  CHECK(ctx != NULL && *(unsigned *)ctx == MAGIC);
  return ctx;
}

/* ---- loopback endpoint pair ---- */
typedef struct packet { qa_net_address from; uint8_t *bytes; size_t len; } packet;
typedef struct endpoint {
  unsigned magic;
  qa_net_address address;
  struct endpoint *peer;
  packet queue[128]; size_t head, count;
  bool closed;
  qa_datagram_transport handle;
} endpoint;
static void ep_accept(endpoint *e, const qa_net_address *from, const uint8_t *bytes, size_t len) {
  if (e->count >= 128 || e->closed) return;
  packet *p = &e->queue[(e->head + e->count++) % 128];
  CHECK(qa_net_address_clone(from, &p->from));
  p->bytes = malloc(len ? len : 1);
  memcpy(p->bytes, bytes, len);
  p->len = len;
}
static bool ep_shift(endpoint *e, packet *out) {
  if (e->count == 0) return false;
  *out = e->queue[e->head];
  e->head = (e->head + 1) % 128; e->count--;
  return true;
}
static int ep_get_address(void *ctx, qa_net_address *out) { return qa_net_address_clone(&((endpoint *)sentinel(ctx))->address, out); }
static bool ep_is_closed(void *ctx) { return ((endpoint *)sentinel(ctx))->closed; }
static int64_t ep_max(void *ctx) { sentinel(ctx); return -1; }
static bool ep_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t len) {
  endpoint *e = sentinel(ctx);
  CHECK(e->peer && qa_net_same_address(to, &e->peer->address, true));
  if (e->peer) ep_accept(e->peer, &e->address, bytes, len);
  return true;
}
static int ep_poll(void *ctx, qa_receive_event *out) {
  endpoint *e = sentinel(ctx);
  packet p;
  if (!ep_shift(e, &p)) return 0;
  memset(out, 0, sizeof *out);
  out->kind = QA_RECV_PACKET; out->from = p.from; out->payload = p.bytes; out->length = p.len;
  return 1;
}
static long long ep_subscribe(void *ctx, qa_net_listener_fn fn, void *listener_ctx) { sentinel(ctx); (void)fn; (void)listener_ctx; return 1; }
static void ep_unsubscribe(void *ctx, long long id) { sentinel(ctx); (void)id; }
static void ep_close(void *ctx) { ((endpoint *)sentinel(ctx))->closed = true; }
static void ep_init(endpoint *e, const char *id) {
  memset(e, 0, sizeof *e);
  e->magic = MAGIC;
  CHECK(qa_net_make_loopback(&e->address, id));
  qa_datagram_transport handle = { e, ep_get_address, ep_is_closed, ep_max, ep_send, ep_poll, ep_subscribe, ep_unsubscribe, ep_close };
  e->handle = handle;
}
static void ep_destroy(endpoint *e) {
  packet p;
  while (ep_shift(e, &p)) { qa_net_address_destroy(&p.from); free(p.bytes); }
  qa_net_address_destroy(&e->address);
}

/* ---- fixture ---- */
typedef struct fixture {
  unsigned magic;
  endpoint server_end, client_end;
  qa_identity_owner identity;
  qa_client_id client_id;
  int64_t actor_generation, source_generation;
  double now;
  bool eligible, prepare_failure, pure;
  qa_app_q3_player player;
  char calls[32][48]; size_t call_count;
  double feeds[32]; size_t feed_count;
  struct { int64_t generation, time; } moves[32]; size_t move_count;
  qa_actor_command commands_pool[64]; size_t command_pool_count;
  int64_t snapshot_flags[64]; size_t snapshot_count;
  char server_commands[32][96]; size_t server_command_count;
  double pure_epochs[64]; size_t pure_epoch_count;
  int gamestates, restarts, random_draws;
  qa_q3_product product;
  qa_q3_pure_server pure_server;
  qa_q3_server_rate rate;
  qa_q3_gamestate gamestate; qa_q3_gamestate_entry entry; char serverid_text[48];
  qa_q3_player_state_record player_state;
  qa_app_q3_server_network *network;
  qa_q3_client_connection *client;
  qa_q3_channel_delivery_storage delivery_storage;
  const qa_q3_channel_delivery *delivery;
  qa_q3_address server_q3;
  int notifications, runs;
} fixture;
static void call(fixture *f, const char *text) { if (f->call_count < 32) snprintf(f->calls[f->call_count++], 48, "%s", text); }
static fixture *F(void *ctx) { return sentinel(ctx); }
static void h_prepare(void *ctx, double feed, double server_id, void (*cs)(void *, double, const char *), void *cs_ctx) {
  fixture *f = F(ctx);
  (void)server_id; (void)cs; (void)cs_ctx;
  if (f->prepare_failure) qa_raise("Error", 0, 0, "prepare failed");
  if (f->feed_count < 32) f->feeds[f->feed_count++] = feed;
}
static const void *h_pure(void *ctx, double server_id, const double *feed_server_id) {
  fixture *f = F(ctx);
  double epoch = feed_server_id ? *feed_server_id : server_id;
  if (f->pure_epoch_count < 64) f->pure_epochs[f->pure_epoch_count++] = epoch;
  f->pure_server.enabled = f->pure; f->pure_server.checksum_feed = 0; f->pure_server.checksum_feed_server_id = (int64_t)epoch;
  f->pure_server.has_cgame_checksum = true; f->pure_server.cgame_checksum = 7;
  f->pure_server.has_ui_checksum = true; f->pure_server.ui_checksum = 8;
  return &f->pure_server;
}
static bool h_false(void *ctx) { sentinel(ctx); return false; }
static const void *h_open(void *ctx, const char *name) { sentinel(ctx); (void)name; return NULL; }
static const void *h_rate(void *ctx, qa_app_q3_player player) { (void)player; return &F(ctx)->rate; }
static const void *h_supports(void *ctx) { sentinel(ctx); return NULL; }
static double h_time(void *ctx) { return F(ctx)->now; }
static const double *h_occupied(void *ctx, size_t *count) { sentinel(ctx); *count = 0; return NULL; }
static qa_app_q3_admission h_admit(void *ctx, const void *request) {
  fixture *f = F(ctx);
  (void)request;
  call(f, "admit");
  qa_app_q3_admission a = { true, f->player, NULL };
  return a;
}
static qa_app_q3_player h_carried(void *ctx, qa_client_id client) { (void)client; return F(ctx)->player; }
static void h_disconnect(void *ctx, qa_app_q3_player player, const char *reason) { (void)player; (void)reason; call(F(ctx), "disconnect"); }
static const void *h_game_state(void *ctx, qa_app_q3_player player, double server_id) {
  fixture *f = F(ctx);
  (void)player;
  snprintf(f->serverid_text, sizeof f->serverid_text, "\\sv_serverid\\%.0f", server_id);
  memset(&f->entry, 0, sizeof f->entry);
  f->entry.kind = QA_Q3_GS_CONFIGSTRING; f->entry.index = 1; f->entry.value = f->serverid_text;
  memset(&f->gamestate, 0, sizeof f->gamestate);
  f->gamestate.entries = &f->entry; f->gamestate.entry_count = 1;
  return &f->gamestate;
}
static qa_app_q3_snapshot h_snapshot(void *ctx, qa_app_q3_player player) {
  fixture *f = F(ctx);
  (void)player;
  qa_q3_player_state_record_init(&f->player_state, QA_Q3_PRODUCT_MISSIONPACK, 0, 5, 0);
  f->player_state.client_num = 0; f->player_state.command_time = (int32_t)f->now;
  qa_app_q3_snapshot s = { &f->player_state, NULL, 0, NULL, 0 };
  return s;
}
static const void *h_input(void *ctx, qa_app_q3_player player, const void *command, double sequence) {
  fixture *f = F(ctx);
  const qa_q3_wire_user_command *wire = command;
  if (f->move_count < 32) { f->moves[f->move_count].generation = player.actor.generation; f->moves[f->move_count].time = wire->server_time; f->move_count++; }
  if (f->command_pool_count >= 64) return NULL;
  qa_actor_command *out = &f->commands_pool[f->command_pool_count++];
  memset(out, 0, sizeof *out);
  out->actor = player.actor; out->sequence = sequence;
  return out;
}
static void h_command(void *ctx, qa_app_q3_player player, const char *name, const char *const *args, size_t count) {
  sentinel(ctx); (void)player; (void)name; (void)args; (void)count;
}
static void h_userinfo(void *ctx, qa_app_q3_player player, const char *value) { sentinel(ctx); (void)player; (void)value; }
static const char *h_status(void *ctx, const char *challenge, bool detailed) { sentinel(ctx); (void)challenge; (void)detailed; return ""; }
static void h_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void r_preflight(void *ctx) {
  fixture *f = F(ctx);
  if (!f->eligible) qa_raise("Error", 0, 0, "incompatible source round");
  call(f, "preflight");
}
static void r_rebind(void *ctx) {
  fixture *f = F(ctx);
  CHECK(f->actor_generation == f->source_generation + 1);
  f->source_generation = f->actor_generation;
  call(f, "bind");
}
static qa_app_q3_admission r_reconnect(void *ctx, qa_client_id client, const char *userinfo, const void *last_command) {
  fixture *f = F(ctx);
  const qa_q3_wire_user_command *command = last_command;
  CHECK(client.slot == f->client_id.slot && client.token == f->client_id.token);
  CHECK(strstr(userinfo, "Retained") != NULL);
  char text[48];
  snprintf(text, sizeof text, "reconnect:%d", command->server_time);
  call(f, text);
  f->player.client = client;
  f->player.actor = qa_identity_owner_actor(&f->identity, 0, f->actor_generation);
  qa_app_q3_admission a = { true, f->player, NULL };
  return a;
}
static double h_random(void *ctx) { F(ctx)->random_draws++; return 0; }
static qa_app_q3_server_host make_host(fixture *f) {
  qa_app_q3_server_host h;
  memset(&h, 0, sizeof h);
  h.ctx = f;
  h.has_source_round = true;
  h.source_round.ctx = f; h.source_round.preflight = r_preflight; h.source_round.rebind = r_rebind; h.source_round.reconnect = r_reconnect;
  h.product = &f->product; h.max_clients = 1;
  h.prepare = h_prepare; h.pure = h_pure; h.downloads_enabled = h_false; h.open_download = h_open; h.rate = h_rate;
  h.supports_source_wire = h_supports; h.time = h_time; h.occupied_slots = h_occupied; h.admit = h_admit;
  h.carried_player = h_carried; h.disconnect = h_disconnect; h.game_state = h_game_state; h.snapshot = h_snapshot;
  h.input = h_input; h.command = h_command; h.userinfo = h_userinfo; h.status = h_status; h.print = h_print;
  return h;
}
static void c_thunk(void *ctx) { sentinel(ctx); }
static void c_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static void c_system_info(void *ctx, const char *info) { sentinel(ctx); (void)info; }
static void c_gamestate(void *ctx, const qa_q3_gamestate *state, int64_t generation) { (void)state; (void)generation; F(ctx)->gamestates++; }
static void c_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, int64_t ping) {
  fixture *f = F(ctx);
  (void)ping;
  if (f->snapshot_count < 64) f->snapshot_flags[f->snapshot_count++] = snapshot->flags;
}
static int64_t c_download_size(void *ctx, int64_t size) { sentinel(ctx); return size; }
static void c_download(void *ctx, const qa_q3_download_block *block) { sentinel(ctx); (void)block; }
static void c_map_restart(void *ctx) { F(ctx)->restarts++; }
static bool c_local(void *ctx) { sentinel(ctx); return true; }
static void client_send(void *ctx, const qa_q3_address *to, const uint8_t *bytes, size_t len) {
  fixture *f = F(ctx);
  qa_net_address net;
  CHECK(qa_q3_address_to_net(to, &net));
  ep_send(&f->client_end, &net, bytes, len);
  qa_net_address_destroy(&net);
}
static void client_remote(void *ctx, qa_q3_address *out) { CHECK(qa_q3_mirror_address_copy(out, &F(ctx)->server_q3)); }
static fixture *make_fixture(bool pure) {
  fixture *f = calloc(1, sizeof *f);
  f->magic = MAGIC;
  ep_init(&f->server_end, "round-server");
  ep_init(&f->client_end, "round-client");
  f->server_end.peer = &f->client_end; f->client_end.peer = &f->server_end;
  CHECK(qa_identity_owner_create(&f->identity, "wire-round"));
  f->client_id = qa_identity_owner_client(&f->identity, 0, 0);
  f->now = 1000; f->eligible = true; f->pure = pure;
  f->player.client = f->client_id; f->player.actor = qa_identity_owner_actor(&f->identity, 0, 0); f->player.source_entity = 0;
  f->product = QA_Q3_PRODUCT_MISSIONPACK;
  qa_q3_server_rate rate = { 10000, 0, 50, true, false, true };
  f->rate = rate;
  qa_app_q3_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &f->server_end.handle;
  options.host = make_host(f);
  options.random = h_random; options.random_ctx = f;
  f->network = qa_app_q3_server_network_create(options);
  CHECK(f->network != NULL);
  qa_q3_client_bindings b;
  memset(&b, 0, sizeof b);
  b.assert_current = c_thunk; b.print = c_print; b.print_ctx = f; b.clear_active = c_thunk; b.system_info = c_system_info;
  b.gamestate = c_gamestate; b.snapshot = c_snapshot; b.download_size = c_download_size; b.download = c_download;
  b.map_restart = c_map_restart; b.level_shot = c_thunk; b.local_server_running = c_local; b.ctx = f;
  qa_q3_connection_identity who = { f->client_id, true, qa_identity_owner_seat(&f->identity, 0) };
  qa_q3_client_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = QA_Q3_CLIENT_MODE_NETWORK; mode.challenge = 0; mode.qport = 77;
  CHECK(qa_q3_client_connection_init(&f->client, who, QA_Q3_PRODUCT_MISSIONPACK, &mode, &b));
  CHECK(qa_q3_address_from_net(&f->server_end.address, &f->server_q3));
  f->delivery = qa_q3_channel_delivery_create(&f->delivery_storage, client_send, f, client_remote, f,
      qa_q3_client_connection_source_state(f->client), c_print, f);
  return f;
}
static void destroy_fixture(fixture *f) {
  qa_app_q3_server_network_close(f->network);
  qa_app_q3_server_network_destroy(f->network);
  qa_q3_client_connection_destroy(f->client);
  qa_q3_mirror_address_destroy(&f->server_q3);
  ep_destroy(&f->server_end);
  ep_destroy(&f->client_end);
  qa_identity_owner_destroy(&f->identity);
  free(f);
}
static void read_client(fixture *f) {
  packet p;
  while (ep_shift(&f->client_end, &p)) {
    if (!(p.len >= 4 && p.bytes[0] == 255 && p.bytes[1] == 255 && p.bytes[2] == 255 && p.bytes[3] == 255)) {
      qa_q3_client_packet_result result;
      CHECK(qa_q3_client_connection_receive_datagram(f->client, p.bytes, p.len, f->now, &result));
      qa_q3_client_packet_result_destroy(&result);
    }
    qa_net_address_destroy(&p.from); free(p.bytes);
  }
  for (int64_t sequence = qa_q3_client_connection_last_executed_server_command(f->client) + 1;
       sequence <= qa_q3_client_connection_server_command_sequence(f->client); sequence++) {
    char **argv = NULL; size_t argc = 0;
    if (!qa_q3_client_connection_get_server_command(f->client, sequence, &argv, &argc)) continue;
    char joined[96] = "";
    size_t used = 0;
    for (size_t i = 0; i < argc; i++) {
      used += (size_t)snprintf(joined + used, used < sizeof joined ? sizeof joined - used : 0, "%s%s", i ? " " : "", argv[i]);
      free(argv[i]);
    }
    free(argv);
    if (f->server_command_count < 32) snprintf(f->server_commands[f->server_command_count++], 96, "%s", joined);
  }
}
static size_t poll_server(fixture *f, double now, const void *const **out) {
  size_t count = 0;
  const void *const *commands = qa_app_q3_server_network_poll(f->network, now, &count);
  if (out) *out = commands;
  return count;
}
static size_t send_move(fixture *f, int32_t time, const void *const **out) {
  qa_q3_wire_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = time; command.weapon = 5; command.forwardmove = 127;
  qa_q3_wire_command_history_append(qa_q3_client_connection_commands(f->client), &command);
  qa_q3_client_send_options options = { f->now, 0, false };
  qa_q3_client_connection_transmit(f->client, &options, f->delivery);
  return poll_server(f, f->now, out);
}
static void publish(fixture *f) { qa_app_q3_server_network_publish(f->network, NULL, NULL, 0, f->now); read_client(f); }
static void add_reliable(fixture *f, const char *text) { CHECK(qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(f->client), text, NULL)); }
static void connect(fixture *f) {
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_encode_connect("\\protocol\\68\\qport\\77\\challenge\\0\\name\\Retained", &bytes, &len));
  ep_accept(&f->server_end, &f->client_end.address, bytes, len);
  free(bytes);
  poll_server(f, f->now, NULL); read_client(f);
  if (f->pure) add_reliable(f, "cp 1 7 8 @ 0");
  send_move(f, 10, NULL); read_client(f);
  send_move(f, 20, NULL); read_client(f);
  publish(f);
}
static size_t clients(fixture *f, qa_app_q3_player *out) { return qa_app_q3_server_network_clients(f->network, out, 2); }
static size_t count_calls(fixture *f, const char *prefix, bool exact) {
  size_t n = 0;
  for (size_t i = 0; i < f->call_count; i++)
    if (exact ? strcmp(f->calls[i], prefix) == 0 : strncmp(f->calls[i], prefix, strlen(prefix)) == 0) n++;
  return n;
}
static void configstring_events(qa_app_q3_network_round_restart *round, const double *indices, const char *const *values, size_t count) {
  qa_app_q3_source_event events[2];
  qa_app_sim_presentation_event items[2];
  const void *pointers[2];
  for (size_t i = 0; i < count; i++) {
    memset(&events[i], 0, sizeof events[i]);
    events[i].kind = QA_APP_Q3_EVENT_CONFIGSTRING; events[i].index = indices[i]; events[i].value = values[i];
    memset(&items[i], 0, sizeof items[i]);
    items[i].kind = "q3-source"; items[i].sequence = (double)i + 1; items[i].seconds = 1.3; items[i].q3_source = &events[i];
    pointers[i] = &items[i];
  }
  round->receive_events(round->ctx, pointers, count);
}
typedef struct poll_scope { fixture *f; double now; } poll_scope;
static void poll_body(void *ctx) { poll_scope *s = ctx; poll_server(s->f, s->now, NULL); }
typedef struct reconnect_scope { qa_app_q3_network_round_restart *round; qa_client_id client; } reconnect_scope;
static void reconnect_body(void *ctx) { reconnect_scope *s = ctx; s->round->reconnect_client(s->round->ctx, s->client); }

/* ================= case 1: fast restart keeps the wire ================= */
typedef struct first_round { fixture *f; size_t prior_snapshots; } first_round;
static void run_first(void *ctx, qa_app_q3_network_round_restart *round) {
  first_round *r = ctx;
  fixture *f = r->f;
  CHECK(round->client_count == 1 && round->clients[0].slot == f->client_id.slot && round->clients[0].token == f->client_id.token);
  CHECK(round->snapshot_server_bit == 4);
  poll_scope ps = { f, 1001 };
  qa_caught_error caught;
  CHECK(!qa_try(poll_body, &ps, &caught) && strstr(caught.message, "already in progress") != NULL);
  f->actor_generation++;
  round->bind_source(round->ctx);
  for (int settle = 0; settle < 3; settle++) f->now += 100;
  const double indices[2] = { 1, 5 };
  const char *values[2] = { "\\sv_serverid\\2", "0" };
  configstring_events(round, indices, values, 2);
  CHECK(round->reconnect_client(round->ctx, f->client_id));
  reconnect_scope rs = { round, f->client_id };
  CHECK(!qa_try(reconnect_body, &rs, &caught) && strstr(caught.message, "already reconnected") != NULL);
  f->now += 100; read_client(f);
  CHECK(f->snapshot_count == r->prior_snapshots);
  CHECK(f->restarts == 0);
}
static void run_second(void *ctx, qa_app_q3_network_round_restart *round) {
  fixture *f = sentinel(ctx);
  CHECK(round->snapshot_server_bit == 0);
  f->actor_generation++;
  round->bind_source(round->ctx);
  for (int settle = 0; settle < 3; settle++) f->now += 100;
  const double indices[1] = { 1 };
  const char *values[1] = { "\\sv_serverid\\3" };
  configstring_events(round, indices, values, 1);
  CHECK(round->reconnect_client(round->ctx, f->client_id));
  f->now += 100;
}
static void test_fast_restart(void) {
  fixture *f = make_fixture(false);
  connect(f);
  qa_q3_netchannel *channel = qa_q3_client_connection_channel(f->client);
  qa_q3_client_reliable_commands *reliable = qa_q3_client_connection_reliable(f->client);
  int initial_gamestates = f->gamestates;
  qa_app_q3_player old[2];
  CHECK(clients(f, old) == 1);
  int draws_before = f->random_draws;
  first_round r = { f, f->snapshot_count };
  qa_app_q3_server_network_restart_source_round(f->network, run_first, &r, NULL, NULL);
  CHECK(send_move(f, 30, NULL) == 0);
  CHECK(f->gamestates == initial_gamestates);
  publish(f);
  CHECK(f->random_draws == draws_before);
  CHECK(qa_q3_client_connection_server_id(f->client) == 2);
  CHECK(qa_q3_client_connection_checksum_feed(f->client) == 0);
  CHECK(qa_q3_client_connection_channel(f->client) == channel);
  CHECK(qa_q3_client_connection_reliable(f->client) == reliable);
  CHECK(f->gamestates == initial_gamestates);
  CHECK(f->restarts == 1);
  CHECK(f->server_command_count >= 3);
  if (f->server_command_count >= 3) {
    CHECK(strcmp(f->server_commands[f->server_command_count - 3], "cs 1 \\sv_serverid\\2") == 0);
    CHECK(strcmp(f->server_commands[f->server_command_count - 2], "cs 5 0") == 0);
    CHECK(strcmp(f->server_commands[f->server_command_count - 1], "map_restart") == 0);
  }
  CHECK(f->snapshot_count >= 1 && f->snapshot_flags[f->snapshot_count - 1] == 4);
  qa_app_q3_player now_clients[2];
  CHECK(clients(f, now_clients) == 1);
  CHECK(now_clients[0].client.token == old[0].client.token && now_clients[0].client.slot == old[0].client.slot);
  CHECK(now_clients[0].actor.generation == 1);
  CHECK(count_calls(f, "reconnect:", false) == 1 && count_calls(f, "reconnect:20", true) == 1);
  CHECK(count_calls(f, "admit", true) == 1);
  f->now += 100;
  const void *const *commands = NULL;
  size_t count = send_move(f, 40, &commands);
  CHECK(count > 0);
  for (size_t i = 0; i < count; i++) CHECK(((const qa_actor_command *)commands[i])->actor.generation == 1);
  CHECK(f->move_count >= 1 && f->moves[f->move_count - 1].time == 40);
  for (size_t i = 0; i < f->feed_count; i++) CHECK(f->feeds[i] == 0);
  CHECK(f->feed_count > 0);
  qa_app_q3_server_network_restart_source_round(f->network, run_second, f, NULL, NULL);
  qa_q3_client_connection_set_server_id(f->client, 1);
  CHECK(send_move(f, 50, NULL) == 0);
  publish(f);
  CHECK(qa_q3_client_connection_server_id(f->client) == 3);
  CHECK(f->gamestates == initial_gamestates);
  CHECK(f->restarts == 2);
  CHECK(f->snapshot_count >= 1 && f->snapshot_flags[f->snapshot_count - 1] == 0);
  CHECK(count_calls(f, "reconnect:", false) == 2 && count_calls(f, "reconnect:40", true) == 1);
  qa_app_q3_server_network_change_world(f->network, make_host(f), NULL, 0);
  poll_server(f, 2000, NULL); read_client(f);
  CHECK(f->gamestates == initial_gamestates + 1);
  CHECK(f->random_draws == draws_before + 2);
  destroy_fixture(f);
}

/* ================= case 2: preflight and interrupted round ================= */
static void run_must_not(void *ctx, qa_app_q3_network_round_restart *round) { sentinel(ctx); (void)round; CHECK(!"must not run"); }
static void count_notification(void *ctx) { F(ctx)->notifications++; }
static void run_failing(void *ctx, qa_app_q3_network_round_restart *round) {
  fixture *f = sentinel(ctx);
  f->actor_generation++;
  round->bind_source(round->ctx);
  qa_raise("Error", 0, 0, "source round failed");
}
typedef struct restart_scope {
  fixture *f; void (*run)(void *, qa_app_q3_network_round_restart *); void (*mutation)(void *);
} restart_scope;
static void restart_body(void *ctx) {
  restart_scope *s = ctx;
  qa_app_q3_server_network_restart_source_round(s->f->network, s->run, s->f, s->mutation, s->f);
}
static void test_preflight_and_interrupted(void) {
  fixture *f = make_fixture(false);
  connect(f);
  f->eligible = false;
  restart_scope scope = { f, run_must_not, count_notification };
  qa_caught_error caught;
  CHECK(!qa_try(restart_body, &scope, &caught) && strstr(caught.message, "incompatible") != NULL);
  CHECK(f->notifications == 0);
  CHECK(qa_app_q3_server_network_phase(f->network) == QA_APP_PHASE_ACTIVE);
  qa_app_q3_player out[2];
  CHECK(clients(f, out) == 1);
  f->now += 100; publish(f);
  CHECK(f->snapshot_count >= 1 && f->snapshot_flags[f->snapshot_count - 1] == 0);
  destroy_fixture(f);
  fixture *failed = make_fixture(false);
  connect(failed);
  restart_scope failing = { failed, run_failing, NULL };
  CHECK(!qa_try(restart_body, &failing, &caught) && strstr(caught.message, "source round failed") != NULL);
  CHECK(qa_app_q3_server_network_phase(failed->network) == QA_APP_PHASE_CLOSED);
  CHECK(failed->server_end.closed);
  CHECK(clients(failed, out) == 0);
  CHECK(poll_server(failed, 2000, NULL) == 0);
  destroy_fixture(failed);
}

/* ================= case 3: delayed pure commands ================= */
static void run_pure(void *ctx, qa_app_q3_network_round_restart *round) {
  fixture *f = sentinel(ctx);
  f->actor_generation++;
  round->bind_source(round->ctx);
  for (int settle = 0; settle < 3; settle++) f->now += 100;
  const double indices[1] = { 1 };
  const char *values[1] = { "\\sv_serverid\\2" };
  configstring_events(round, indices, values, 1);
  round->reconnect_client(round->ctx, f->client_id);
  f->now += 100;
}
static void test_pure_epochs(void) {
  fixture *fast = make_fixture(true);
  connect(fast);
  qa_app_q3_server_network_restart_source_round(fast->network, run_pure, fast, NULL, NULL);
  publish(fast);
  CHECK(qa_q3_client_connection_server_id(fast->client) == 2);
  add_reliable(fast, "cp 1 7 8 @ 0"); send_move(fast, 30, NULL);
  qa_app_q3_player out[2];
  CHECK(clients(fast, out) == 1);
  CHECK(fast->pure_epoch_count >= 1 && fast->pure_epochs[fast->pure_epoch_count - 1] == 1);
  fast->now += 100;
  add_reliable(fast, "cp 1 7 8 @ 999"); send_move(fast, 40, NULL);
  CHECK(clients(fast, out) == 0);
  CHECK(fast->call_count >= 1 && strcmp(fast->calls[fast->call_count - 1], "disconnect") == 0);
  destroy_fixture(fast);
  fixture *full = make_fixture(true);
  connect(full);
  qa_app_q3_server_network_change_world(full->network, make_host(full), NULL, 0);
  poll_server(full, 2000, NULL); read_client(full);
  CHECK(qa_q3_client_connection_server_id(full->client) == 2);
  add_reliable(full, "cp 1 7 8 @ 999"); send_move(full, 30, NULL);
  CHECK(clients(full, out) == 1);
  CHECK(full->pure_epoch_count >= 1 && full->pure_epochs[full->pure_epoch_count - 1] == 2);
  add_reliable(full, "cp 2 7 8 @ 0"); send_move(full, 40, NULL);
  CHECK(clients(full, out) == 1);
  destroy_fixture(full);
}

/* ================= case 4: mutation notification precedes prepare/notification failure ================= */
static void run_counting(void *ctx, qa_app_q3_network_round_restart *round) { (void)round; F(ctx)->runs++; }
static void failing_notification(void *ctx) {
  fixture *f = F(ctx);
  f->notifications++;
  if (!f->prepare_failure) qa_raise("Error", 0, 0, "notification failed");
}
static void test_notification_precedes_failure(void) {
  for (int prepare = 1; prepare >= 0; prepare--) {
    fixture *f = make_fixture(false);
    connect(f);
    if (prepare) f->prepare_failure = true;
    restart_scope scope = { f, run_counting, failing_notification };
    qa_caught_error caught;
    CHECK(!qa_try(restart_body, &scope, &caught) && strstr(caught.message, prepare ? "prepare failed" : "notification failed") != NULL);
    CHECK(f->notifications == 1);
    CHECK(f->runs == 0);
    CHECK(qa_app_q3_server_network_phase(f->network) == QA_APP_PHASE_CLOSED);
    CHECK(f->server_end.closed);
    qa_app_q3_player out[2];
    CHECK(clients(f, out) == 0);
    CHECK(poll_server(f, 2000, NULL) == 0);
    destroy_fixture(f);
  }
}

int main(void) {
  test_fast_restart();
  test_preflight_and_interrupted();
  test_pure_epochs();
  test_notification_precedes_failure();
  printf("test_q3_round: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
