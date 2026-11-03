/*
 * Q3ServerNetwork.pollPackets per-packet catch (src/app/bootstrap/network/q3.ts
 * pollPackets): a malformed datagram prints its error and the server keeps
 * running; a game callback failure raised by one peer's packet disconnects that
 * peer with "Server game error". Drives the server through a fake transport
 * and host, with real Q3 admission and wire encoding. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/q3.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static int SENTINEL;
#define MAX_QUEUE 16
typedef struct world {
  int *sentinel;
  /* transport */
  qa_receive_event queue[MAX_QUEUE]; size_t head, tail;
  uint8_t last_sent[2048]; size_t last_sent_len; int sends; bool closed;
  /* host */
  char prints[8192];
  char disconnect_reason[256]; int disconnects, admits, commands;
  bool command_raises;
  qa_q3_gamestate gamestate;
  qa_q3_server_rate rate;
  qa_q3_pure_server pure;
  qa_q3_product product;
  qa_q3_player_state_record player;
  double now;
} world;
static world *W(void *ctx) {
  world *w = ctx;
  if (!w || w->sentinel != &SENTINEL) { fprintf(stderr, "bad ctx\n"); abort(); }
  return w;
}
/* transport */
static int t_address(void *ctx, qa_net_address *out) {
  (void)W(ctx);
  memset(out, 0, sizeof *out);
  out->kind = QA_NET_IPV4; out->ipv4[0] = 10; out->ipv4[3] = 1; out->port = 27960;
  return 1;
}
static bool t_send(void *ctx, const qa_net_address *to, const uint8_t *payload, size_t length) {
  world *w = W(ctx);
  (void)to;
  if (length <= sizeof w->last_sent) { memcpy(w->last_sent, payload, length); w->last_sent_len = length; }
  w->sends++;
  return true;
}
static int t_poll(void *ctx, qa_receive_event *out) {
  world *w = W(ctx);
  if (w->head == w->tail) return 0;
  *out = w->queue[w->head++ % MAX_QUEUE];
  return 1;
}
static void t_close(void *ctx) { W(ctx)->closed = true; }
static void push_packet(world *w, const uint8_t *bytes, size_t len) {
  qa_receive_event *e = &w->queue[w->tail++ % MAX_QUEUE];
  memset(e, 0, sizeof *e);
  e->kind = QA_RECV_PACKET;
  e->from.kind = QA_NET_IPV4; e->from.ipv4[0] = 10; e->from.ipv4[3] = 2; e->from.port = 27961;
  e->payload = malloc(len ? len : 1);
  memcpy(e->payload, bytes, len);
  e->length = len;
}
/* host */
static void h_prepare(void *ctx, double feed, double id, void (*cs)(void *, double, const char *), void *cs_ctx) {
  (void)W(ctx); (void)feed; (void)id; (void)cs; (void)cs_ctx;
}
static const void *h_pure(void *ctx, double id, const double *feed_id) { (void)id; (void)feed_id; return &W(ctx)->pure; }
static bool h_downloads(void *ctx) { (void)W(ctx); return false; }
static const void *h_open(void *ctx, const char *name) { (void)W(ctx); (void)name; return NULL; }
static const void *h_rate(void *ctx, qa_app_q3_player p) { (void)p; return &W(ctx)->rate; }
static double h_time(void *ctx) { return W(ctx)->now; }
static const double *h_occupied(void *ctx, size_t *count) { (void)W(ctx); *count = 0; return NULL; }
static qa_app_q3_admission h_admit(void *ctx, const void *request) {
  world *w = W(ctx);
  const qa_q3_accepted_connect *accepted = request;
  w->admits++;
  qa_app_q3_admission a;
  memset(&a, 0, sizeof a);
  a.accepted = true;
  a.player.client.slot = accepted->slot; a.player.client.generation = 1; a.player.client.token = &SENTINEL;
  a.player.source_entity = (double)accepted->slot;
  return a;
}
static qa_app_q3_player h_carried(void *ctx, qa_client_id client) {
  (void)W(ctx);
  qa_app_q3_player p; memset(&p, 0, sizeof p); p.client = client; return p;
}
static void h_disconnect(void *ctx, qa_app_q3_player p, const char *reason) {
  world *w = W(ctx);
  (void)p;
  w->disconnects++;
  snprintf(w->disconnect_reason, sizeof w->disconnect_reason, "%s", reason);
}
static const void *h_game_state(void *ctx, qa_app_q3_player p, double id) { (void)p; (void)id; return &W(ctx)->gamestate; }
static qa_app_q3_snapshot h_snapshot(void *ctx, qa_app_q3_player p) {
  world *w = W(ctx);
  (void)p;
  qa_app_q3_snapshot s; memset(&s, 0, sizeof s);
  s.player = &w->player;
  return s;
}
static const void *h_input(void *ctx, qa_app_q3_player p, const void *command, double sequence) {
  (void)W(ctx); (void)p; (void)command; (void)sequence; return NULL;
}
static void h_command(void *ctx, qa_app_q3_player p, const char *name, const char *const *args, size_t count) {
  world *w = W(ctx);
  (void)p; (void)args; (void)count;
  w->commands++;
  if (w->command_raises) qa_range_error("%s failed", name);
}
static void h_userinfo(void *ctx, qa_app_q3_player p, const char *value) { (void)W(ctx); (void)p; (void)value; }
static const char *h_status(void *ctx, const char *challenge, bool detailed) { (void)W(ctx); (void)challenge; (void)detailed; return NULL; }
static void h_print(void *ctx, const char *text) {
  world *w = W(ctx);
  size_t used = strlen(w->prints);
  snprintf(w->prints + used, sizeof w->prints - used, "%s", text);
}
static double random_value(void *ctx) { (void)W(ctx); return 3; }
static const char *empty_command(void *ctx, int64_t sequence) { (void)ctx; (void)sequence; return ""; }

/* Client side: challenge negotiation then reliable command datagrams. */
static int64_t parse_challenge(const world *w) {
  /* "\xff\xff\xff\xffchallengeResponse N" */
  char text[128] = "";
  size_t n = w->last_sent_len > 4 ? w->last_sent_len - 4 : 0;
  if (n >= sizeof text) n = sizeof text - 1;
  memcpy(text, w->last_sent + 4, n);
  const char *space = strchr(text, ' ');
  return space ? qa_native_atoi(space + 1) : -1;
}
static void push_sequenced(world *w, qa_q3_netchannel *channel, int64_t challenge, const uint8_t *plain, size_t plain_len) {
  uint8_t *encoded = NULL;
  CHECK(qa_q3_xor_client_message(plain, plain_len, challenge, empty_command, NULL, &encoded));
  uint8_t **packets = NULL; size_t *lengths = NULL, count = 0;
  CHECK(qa_q3_netchannel_transmit(channel, encoded, plain_len, &packets, &lengths, &count));
  for (size_t i = 0; i < count; i++) { push_packet(w, packets[i], lengths[i]); free(packets[i]); }
  free(packets); free(lengths); free(encoded);
}

int main(void) {
  world w;
  memset(&w, 0, sizeof w);
  w.sentinel = &SENTINEL;
  w.rate.rate = 25000; w.rate.snapshot_msec = 50; w.rate.lan = true;
  w.product = QA_Q3_PRODUCT_BASEQ3;
  qa_q3_player_state_record_init(&w.player, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
  qa_datagram_transport transport;
  memset(&transport, 0, sizeof transport);
  transport.ctx = &w; transport.get_address = t_address; transport.send = t_send;
  transport.poll = t_poll; transport.close = t_close;
  qa_app_q3_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &w; host.product = &w.product; host.max_clients = 4;
  host.prepare = h_prepare; host.pure = h_pure; host.downloads_enabled = h_downloads; host.open_download = h_open;
  host.rate = h_rate; host.time = h_time; host.occupied_slots = h_occupied; host.admit = h_admit;
  host.carried_player = h_carried; host.disconnect = h_disconnect; host.game_state = h_game_state;
  host.snapshot = h_snapshot; host.input = h_input; host.command = h_command; host.userinfo = h_userinfo;
  host.status = h_status; host.print = h_print;
  qa_app_q3_server_network_options options;
  memset(&options, 0, sizeof options);
  options.transport = &transport; options.host = host; options.random = random_value; options.random_ctx = &w;
  qa_app_q3_server_network *server = qa_app_q3_server_network_create(options);
  CHECK(server != NULL);

  /* 1. A connect whose compressed payload is cut short: BinaryError printed, poll returns. */
  uint8_t *connect = NULL; size_t connect_len = 0;
  CHECK(qa_q3_encode_connect("\\protocol\\68\\qport\\5\\challenge\\0", &connect, &connect_len));
  push_packet(&w, connect, 15);
  free(connect);
  size_t polled = 0;
  qa_app_q3_server_network_poll(server, 1000, &polled);
  CHECK(strstr(w.prints, "<connectionless>:12: Malformed compressed connectionless payload: Truncated Huffman symbol") != NULL);
  CHECK(qa_app_q3_server_network_phase(server) == QA_APP_PHASE_ACTIVE);

  /* 2. getchallenge then connect from a LAN address. */
  uint8_t *packet = NULL; size_t packet_len = 0;
  CHECK(qa_q3_encode_connectionless_text("getchallenge", &packet, &packet_len));
  push_packet(&w, packet, packet_len);
  free(packet);
  w.now = 2000;
  qa_app_q3_server_network_poll(server, 2000, &polled);
  int64_t challenge = parse_challenge(&w);
  CHECK(challenge != -1);
  char userinfo[128];
  snprintf(userinfo, sizeof userinfo, "\\protocol\\68\\qport\\5\\challenge\\%lld\\name\\probe", (long long)challenge);
  CHECK(qa_q3_encode_connect(userinfo, &connect, &connect_len));
  push_packet(&w, connect, connect_len);
  free(connect);
  qa_app_q3_server_network_poll(server, 2100, &polled);
  CHECK(w.admits == 1);
  CHECK(w.last_sent_len >= 4 && memcmp(w.last_sent + 4, "connectResponse", 15) == 0);
  qa_app_q3_player clients[4];
  CHECK(qa_app_q3_server_network_clients(server, clients, 4) == 1);

  qa_q3_netchannel *channel = NULL;
  CHECK(qa_q3_netchannel_init(&channel, QA_Q3_CHANNEL_CLIENT, 5, NULL, NULL));
  /* 3. A sequenced datagram too short for the client-message prefix: printed, peer kept. */
  w.prints[0] = '\0';
  const uint8_t short_plain[2] = {1, 2};
  push_sequenced(&w, channel, challenge, short_plain, sizeof short_plain);
  qa_app_q3_server_network_poll(server, 2200, &polled);
  CHECK(strstr(w.prints, "<client-message>:") != NULL);
  CHECK(qa_app_q3_server_network_clients(server, clients, 4) == 1);
  CHECK(w.disconnects == 0);

  /* 4. A bad command byte: the TS warning, peer kept. */
  w.prints[0] = '\0';
  qa_q3_message_writer *writer = NULL;
  CHECK(qa_q3_message_writer_init_default(&writer));
  qa_q3_message_writer_write_long(writer, 1);
  qa_q3_message_writer_write_long(writer, 0);
  qa_q3_message_writer_write_long(writer, 0);
  qa_q3_message_writer_write_byte(writer, 9);
  uint8_t *plain = NULL; size_t plain_len = 0;
  CHECK(qa_q3_message_writer_to_bytes(writer, &plain, &plain_len));
  qa_q3_message_writer_destroy(writer);
  push_sequenced(&w, channel, challenge, plain, plain_len);
  free(plain);
  qa_app_q3_server_network_poll(server, 2300, &polled);
  CHECK(strstr(w.prints, "WARNING: bad command byte for client\n") != NULL);
  CHECK(qa_app_q3_server_network_clients(server, clients, 4) == 1);

  /* 5. A game command that raises: Q3GameCallbackError costs the sender its connection. */
  w.prints[0] = '\0';
  w.command_raises = true;
  qa_q3_reliable_command commands[1] = { {1, "boom now"} };
  qa_q3_client_message message;
  memset(&message, 0, sizeof message);
  message.header.server_id = 1; message.header.message_acknowledge = 0; message.header.reliable_acknowledge = 0;
  message.commands = commands; message.command_count = 1;
  qa_q3_client_key_context key = { 0, empty_command, NULL };
  CHECK(qa_q3_encode_client_message(&message, &key, NULL, &plain, &plain_len));
  push_sequenced(&w, channel, challenge, plain, plain_len);
  free(plain);
  qa_app_q3_server_network_poll(server, 2400, &polled);
  CHECK(w.commands == 1);
  CHECK(strstr(w.prints, "Q3 game callback failed: boom failed") != NULL);
  CHECK(w.disconnects == 1 && strcmp(w.disconnect_reason, "Server game error") == 0);
  CHECK(qa_app_q3_server_network_clients(server, clients, 4) == 0);
  CHECK(qa_app_q3_server_network_phase(server) == QA_APP_PHASE_ACTIVE);

  qa_app_q3_server_network_close(server);
  CHECK(w.closed);
  qa_app_q3_server_network_destroy(server);
  qa_q3_netchannel_destroy(channel);
  while (w.head != w.tail) qa_receive_event_destroy(&w.queue[w.head++ % MAX_QUEUE]);
  printf("test_q3_server_network_catch: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
