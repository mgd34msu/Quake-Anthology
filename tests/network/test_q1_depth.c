/* Port of tests/network/q1/network.test.ts wire cases, with bytes captured from the
 * TS Q1 writers: each captured stream is decoded and rewritten, so writers and decoders
 * are both held to the TS bytes. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/common/endpoint.h"
#include "qa/network/q1/handshake.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/profile.h"
#include "q1_depth_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint8_t *unhex(const char *hex, size_t *length) {
  size_t n = strlen(hex) / 2;
  uint8_t *bytes = malloc(n ? n : 1);
  for (size_t i = 0; i < n; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); bytes[i] = (uint8_t)v; }
  *length = n;
  return bytes;
}
static bool same_hex(const uint8_t *bytes, size_t length, const char *hex) {
  size_t n; uint8_t *want = unhex(hex, &n);
  bool equal = n == length && memcmp(want, bytes, n) == 0;
  free(want);
  return equal;
}
static void show(const char *label, const uint8_t *bytes, size_t length, const char *hex) {
  fprintf(stderr, "%s\n got ", label);
  for (size_t i = 0; i < length; i++) fprintf(stderr, "%02x", bytes[i]);
  fprintf(stderr, "\n exp %s\n", hex);
}
#define CHECK_HEX(bytes, length, hex, label) do { checks++; if (!same_hex(bytes, length, hex)) { \
  failures++; fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); show(label, bytes, length, hex); } } while (0)

/* The TS rows use a literal identity: flags are not checked against netQuakeProfile. */
static qa_q1_protocol_identity identity_of(double version, double flags) {
  qa_q1_protocol_identity id;
  memset(&id, 0, sizeof id);
  id.kind = version == 15 ? "q1-netquake" : version == 666 ? "q1-fitzquake" : "q1-rmq";
  id.version = (int64_t)version;
  id.flags = (int64_t)flags;
  return id;
}

static void test_netquake_rows(void) {
  for (size_t i = 0; i < sizeof q1_rows / sizeof *q1_rows; i++) {
    const q1_row *row = &q1_rows[i];
    qa_q1_protocol_identity profile = identity_of(row->version, row->flags);
    size_t length; uint8_t *bytes = unhex(row->hex, &length);
    qa_netquake_decoder decoder;
    qa_netquake_decoder_init(&decoder, profile, row->mode, row->standard != 0);
    size_t count = 0;
    qa_netquake_message *messages = qa_netquake_decoder_decode(&decoder, bytes, length, &count);
    CHECK(count == 1);
    qa_size_buf sb;
    qa_size_buf_init(&sb, 65535, false);
    for (size_t k = 0; k < count; k++) qa_write_netquake_message(&sb, profile, &messages[k], row->mode, row->standard != 0);
    size_t out_length = 0;
    uint8_t *out = qa_size_buf_bytes(&sb, &out_length);
    char label[96];
    snprintf(label, sizeof label, "%s protocol %g flags %g", row->kind, row->version, row->flags);
    CHECK_HEX(out, out_length, row->rewritten, label); /* TS write(decode(hex)) */
    free(out);
    qa_size_buf_destroy(&sb);
    for (size_t k = 0; k < count; k++) qa_netquake_message_destroy(&messages[k]);
    free(messages);
    qa_netquake_decoder_destroy(&decoder);
    free(bytes);
  }
}

/* writeNetQuakeMove then decodeNetQuakeClient: distinct values show any field reorder. */
static void test_moves(void) {
  for (size_t i = 0; i < sizeof q1_moves / sizeof *q1_moves; i++) {
    qa_q1_protocol_identity profile = identity_of(q1_moves[i].version, q1_moves[i].flags);
    qa_q1_user_command command;
    memset(&command, 0, sizeof command);
    command.acknowledged_server_time_seconds = 3.5;
    command.view_angles = (qa_vec3){11.25, -90, 45};
    command.forward_move = 200; command.side_move = -50; command.up_move = 7;
    command.buttons = 3; command.impulse = 9;
    qa_size_buf sb;
    qa_size_buf_init(&sb, 65535, false);
    qa_write_netquake_move(&sb, &command, profile, 0, false);
    size_t length; uint8_t *bytes = qa_size_buf_bytes(&sb, &length);
    CHECK_HEX(bytes, length, q1_moves[i].hex, "netquake move");
    size_t count = 0;
    qa_netquake_client_message *decoded = qa_decode_netquake_client(bytes, length, profile, &count);
    CHECK(count == 1 && strcmp(decoded[0].kind, "move") == 0);
    const qa_q1_user_command *got = &decoded[0].command;
    CHECK(got->acknowledged_server_time_seconds == 3.5);
    CHECK(got->forward_move == 200 && got->side_move == -50 && got->up_move == 7);
    CHECK(got->buttons == 3 && got->impulse == 9);
    for (size_t k = 0; k < count; k++) qa_netquake_client_message_destroy(&decoded[k]);
    free(decoded);
    free(bytes);
    qa_size_buf_destroy(&sb);
  }
}

static void test_controls(void) {
  for (size_t i = 0; i < sizeof q1_controls / sizeof *q1_controls; i++) {
    size_t length; uint8_t *bytes = unhex(q1_controls[i], &length);
    qa_netquake_control message;
    qa_decode_netquake_control(bytes, length, &message);
    size_t out_length = 0;
    uint8_t *out = qa_encode_netquake_control(&message, &out_length);
    CHECK_HEX(out, out_length, q1_controls[i], "netquake control");
    free(out);
    qa_netquake_control_destroy(&message);
    free(bytes);
  }
}

static const double random_values[] = {100, 200, 300, 400, 0x12345, 0x7fff};
static double next_random(void *ctx) { size_t *at = ctx; return random_values[(*at)++ % 6]; }
static bool not_blocked(void *ctx, const qa_net_address *from) { (void)ctx; (void)from; return false; }
static size_t admitted_count = 0;
static qa_qw_host_result admit(void *ctx, const qa_qw_connect_request *request, double now_ms) {
  (void)ctx; (void)now_ms;
  size_t at = admitted_count++;
  CHECK(at < sizeof q1_admitted / sizeof *q1_admitted);
  if (at < sizeof q1_admitted / sizeof *q1_admitted) {
    CHECK(request->qport == q1_admitted[at].qport && request->challenge == q1_admitted[at].challenge);
    CHECK(strcmp(request->userinfo, q1_admitted[at].userinfo) == 0);
    CHECK(request->spectator == (q1_admitted[at].spectator != 0) && request->donor_wide == (q1_admitted[at].donor != 0));
  }
  return (qa_qw_host_result){0, NULL};
}
static char *dup_text(const char *text) { char *p = malloc(strlen(text) + 1); strcpy(p, text); return p; }
static char *status_text(void *ctx) { (void)ctx; return dup_text("stat\n"); }
static char *log_text(void *ctx, double sequence) {
  (void)ctx;
  if (sequence < 0) return NULL;
  char text[32]; snprintf(text, sizeof text, "L%.0f", sequence);
  return dup_text(text);
}
static void execute_admin(void *ctx, const char *command, qa_qw_write_fn write, void *write_ctx) {
  (void)ctx;
  write(write_ctx, "ran:", 4);
  write(write_ctx, command, strlen(command));
}

static void test_quakeworld_server(void) {
  size_t at = 0;
  qa_qw_challenges challenges;
  qa_qw_challenges_init(&challenges, next_random, &at, 1024);
  qa_qw_connectionless_server server;
  memset(&server, 0, sizeof server);
  server.host.password = ""; server.host.spectator_password = ""; server.host.rcon_password = "pw";
  server.host.high_characters = true;
  server.host.blocked = not_blocked; server.host.connect = admit;
  server.host.status = status_text; server.host.log = log_text; server.host.execute_admin = execute_admin;
  server.challenges = &challenges;
  qa_net_address from;
  memset(&from, 0, sizeof from);
  from.kind = QA_NET_IPV4; from.host = (char *)"10.0.0.1"; from.port = 27001;
  for (size_t i = 0; i < sizeof q1_server / sizeof *q1_server; i++) {
    size_t length = 0;
    uint8_t *packet = qa_quakeworld_out_of_band(q1_server[i].text, false, &length);
    size_t *lens = NULL, count = 0;
    uint8_t **replies = qa_qw_connectionless_receive(&server, packet, length, &from, 1000, &lens, &count);
    CHECK((int)count == q1_server[i].count);
    if (count > 0 && q1_server[i].count > 0) CHECK_HEX(replies[0], lens[0], q1_server[i].reply, q1_server[i].text);
    for (size_t k = 0; k < count; k++) free(replies[k]);
    free(replies); free(lens); free(packet);
  }
  CHECK(admitted_count == sizeof q1_admitted / sizeof *q1_admitted);
  /* TS issue(): (random() & 0x7fffffff) || 1, reused for 30 s: the first random value. */
  CHECK(qa_qw_challenges_issue(&challenges, &from, 1000) == Q1_CHALLENGE);
  qa_qw_challenges_destroy(&challenges);
  qa_qw_connectionless_server_destroy(&server);

  qa_qw_connect_client client;
  qa_qw_connect_client_init(&client, 1234, "\\name\\x");
  size_t length = 0;
  uint8_t *first = qa_qw_connect_client_next(&client, 0, &length);
  CHECK_HEX(first, length, q1_client_flow[0], "getchallenge");
  free(first);
  char text[32];
  snprintf(text, sizeof text, "c%d", Q1_CHALLENGE);
  uint8_t *reply = qa_quakeworld_out_of_band(text, false, &length);
  qa_qw_connect_client_receive(&client, reply, length);
  free(reply);
  uint8_t *second = qa_qw_connect_client_next(&client, 1, &length);
  CHECK_HEX(second, length, q1_client_flow[1], "connect");
  free(second);
  CHECK(qa_qw_connect_client_next(&client, 2, &length) == NULL);
  qa_qw_connect_client_destroy(&client);
}

static void test_heartbeats(void) {
  qa_qw_master_heartbeat beat;
  qa_qw_master_heartbeat_init(&beat);
  const double times[] = {0, 1000, 300000, 300001}, active[] = {3, 4, 5, 6};
  const bool force[] = {false, false, false, true};
  for (size_t i = 0; i < 4; i++) {
    size_t length = 0;
    uint8_t *bytes = qa_qw_master_heartbeat_next(&beat, times[i], active[i], force[i], &length);
    if (strcmp(q1_beats[i], "null") == 0) CHECK(bytes == NULL);
    else if (!bytes) CHECK(bytes != NULL);
    else CHECK_HEX(bytes, length, q1_beats[i], "heartbeat");
    free(bytes);
  }
  size_t length = 0;
  uint8_t *big = qa_quakeworld_heartbeat(1234567, 3, &length);
  CHECK_HEX(big, length, q1_big_heartbeat, "large sequence stays decimal");
  free(big);

  qa_nq_connect_client nq;
  qa_nq_connect_client_init(&nq);
  const double steps[] = {0, 100, 2500, 5000, 7500, 10000};
  for (size_t i = 0; i < 6; i++) {
    uint8_t *bytes = qa_nq_connect_client_next(&nq, steps[i], &length);
    if (strcmp(q1_nq_flow[i], "null") == 0) CHECK(bytes == NULL);
    else if (!bytes) CHECK(bytes != NULL);
    else CHECK_HEX(bytes, length, q1_nq_flow[i], "nq connect");
    free(bytes);
  }
  CHECK(nq.kind == 2 && strcmp(nq.reason, "No response") == 0);
  qa_nq_connect_client_destroy(&nq);
}

int main(void) {
  test_netquake_rows();
  test_moves();
  test_controls();
  test_quakeworld_server();
  test_heartbeats();
  printf("q1_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
