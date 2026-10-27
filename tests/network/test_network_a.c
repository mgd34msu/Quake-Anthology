/*
 * Port of key cases from tests/network (common primitives, Q1 wire,
 * unified packets): checksum2 lump skipping (qw-client.test.ts),
 * NetQuake print/stat round trips, movement checksum verification,
 * fragment sender/receiver completion, IPX header round trip, unified
 * reliable delivery. TypeScript toThrow cases run in a child process
 * and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/network/common/endpoint.h"
#include "qa/network/common/fragments.h"
#include "qa/network/common/ipx.h"
#include "qa/network/q1/checksum.h"
#include "qa/network/q1/commands.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/profile.h"
#include "qa/network/services/rank_codec.h"
#include "qa/network/unified/channel.h"
#include "qa/network/unified/packet.h"

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

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static void put_i32le(uint8_t *b, size_t o, int32_t v) {
  uint32_t u = (uint32_t)v;
  b[o] = (uint8_t)u; b[o+1] = (uint8_t)(u >> 8); b[o+2] = (uint8_t)(u >> 16); b[o+3] = (uint8_t)(u >> 24);
}

static void test_checksum2(void) {
  uint8_t bytes[139];
  memset(bytes, 0, sizeof bytes);
  put_i32le(bytes, 0, 29);
  for (int i = 0; i < 15; i++) {
    put_i32le(bytes, 4 + (size_t)i * 8, 124 + i);
    put_i32le(bytes, 8 + (size_t)i * 8, 1);
    bytes[124 + i] = (uint8_t)i;
  }
  int32_t original = qa_quake_world_map_checksum2(bytes, sizeof bytes);
  const int skipped[4] = { 0, 4, 5, 10 };
  for (int i = 0; i < 4; i++) bytes[124 + skipped[i]] = 100;
  CHECK(qa_quake_world_map_checksum2(bytes, sizeof bytes) == original);
  bytes[125] = 100;
  CHECK(qa_quake_world_map_checksum2(bytes, sizeof bytes) != original);
  CHECK_ABORTS((void)qa_quake_world_map_checksum2(bytes, 100));
}

static void test_message_and_netquake(void) {
  qa_size_buf sb;
  CHECK(qa_size_buf_init(&sb, 64, false));
  qa_msg_write_short(&sb, -2);
  qa_msg_write_coord(&sb, 1.5);
  size_t n = 0;
  uint8_t *bytes = qa_size_buf_bytes(&sb, &n);
  CHECK(n == 4 && bytes[0] == 0xfe && bytes[1] == 0xff && bytes[2] == 12 && bytes[3] == 0);
  qa_message_reader r;
  qa_message_reader_init(&r, bytes, n);
  CHECK(qa_message_reader_short(&r) == -2);
  CHECK(qa_message_reader_coord(&r) == 1.5);
  free(bytes);
  qa_size_buf_clear(&sb);

  qa_q1_protocol_identity profile = qa_netquake_profile(15, 0);
  qa_netquake_decoder decoder;
  qa_netquake_decoder_init(&decoder, profile, "known-retail", true);
  qa_netquake_message m;
  memset(&m, 0, sizeof m);
  m.kind = "print"; m.text = (char *)"hello\n";
  qa_write_netquake_message(&sb, profile, &m, "known-retail", true);
  qa_netquake_message stat;
  memset(&stat, 0, sizeof stat);
  stat.kind = "stat"; stat.index = 3; stat.value = -7;
  qa_write_netquake_message(&sb, profile, &stat, "known-retail", true);
  bytes = qa_size_buf_bytes(&sb, &n);
  size_t count = 0;
  qa_netquake_message *decoded = qa_netquake_decoder_decode(&decoder, bytes, n, &count);
  CHECK(count == 2);
  CHECK(decoded[0].kind && strcmp(decoded[0].kind, "print") == 0);
  CHECK(decoded[0].text && strcmp(decoded[0].text, "hello\n") == 0);
  CHECK(decoded[1].kind && strcmp(decoded[1].kind, "stat") == 0);
  CHECK(decoded[1].index == 3 && decoded[1].value == -7);
  for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&decoded[i]);
  free(decoded);
  free(bytes);
  qa_netquake_decoder_destroy(&decoder);
  qa_size_buf_destroy(&sb);
  CHECK_ABORTS(qa_net_port_number(0, false));
}

static void test_fragments_ipx(void) {
  qa_fragment_sender sender;
  CHECK(qa_fragment_sender_init(&sender, 4, 16, false));
  qa_fragment_receiver receiver;
  CHECK(qa_fragment_receiver_init(&receiver, 16));
  const uint8_t payload[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  qa_fragment_sender_begin(&sender, 7, payload, sizeof payload);
  int completed = 0;
  qa_message_fragment frag;
  while (qa_fragment_sender_next(&sender, &frag)) {
    qa_fragment_result result = qa_fragment_receiver_receive(&receiver, &frag);
    free(frag.bytes);
    if (result.kind == QA_FRAGMENT_COMPLETE) {
      completed = 1;
      CHECK(result.sequence == 7 && result.length == 9);
      CHECK(memcmp(result.bytes, payload, 9) == 0);
      free(result.bytes);
    }
  }
  CHECK(completed == 1);
  qa_fragment_sender_destroy(&sender);
  qa_fragment_receiver_destroy(&receiver);

  int64_t node[6] = { 1, 2, 3, 4, 5, 6 };
  qa_ipx_packet packet;
  memset(&packet, 0, sizeof packet);
  (void)qa_net_make_ipx(&packet.from, 0x11223344, node, 0x4002);
  int64_t node2[6] = { 6, 5, 4, 3, 2, 1 };
  (void)qa_net_make_ipx(&packet.to, 0x11223344, node2, 0x4003);
  packet.packet_type = 4; packet.hops = 0;
  uint8_t body[3] = { 9, 8, 7 };
  packet.payload = body; packet.length = 3;
  size_t wire_len = 0;
  uint8_t *wire = qa_encode_ipx_packet(&packet, &wire_len);
  CHECK(wire_len == 33 && wire[2] == 0 && wire[3] == 33);
  qa_ipx_packet decoded;
  CHECK(qa_decode_ipx_packet(wire, wire_len, &decoded) == 1);
  CHECK(decoded.packet_type == 4 && decoded.length == 3);
  CHECK(memcmp(decoded.payload, body, 3) == 0);
  CHECK(qa_net_same_address(&decoded.from, &packet.from, true));
  qa_ipx_packet_destroy(&decoded);
  packet.payload = NULL;
  qa_ipx_packet_destroy(&packet);
  free(wire);
}

static void test_unified(void) {
  qa_unified_channel a, b;
  qa_unified_channel_init(&a, "0123456789abcdef0123456789abcdef", 1024, 8192, 1000, 1, true);
  qa_unified_channel_init(&b, "0123456789abcdef0123456789abcdef", 1024, 8192, 1000, 1, true);
  const uint8_t payload[5] = { 'h', 'e', 'l', 'l', 'o' };
  qa_unified_channel_queue_reliable(&a, payload, sizeof payload);
  size_t *lens = NULL, count = 0;
  uint8_t **packets = qa_unified_channel_transmit_reliable(&a, 0, &lens, &count);
  CHECK(count == 1 && lens[0] == 53);
  qa_unified_packet peek;
  CHECK(qa_decode_unified_packet(packets[0], lens[0], &peek) == 1);
  CHECK(peek.kind == QA_UNIFIED_RELIABLE && peek.sequence == 1);
  qa_unified_receive_result result;
  (void)qa_unified_channel_receive(&b, packets[0], lens[0], 10, &result);
  CHECK(result.delivery_count == 1);
  CHECK(result.deliveries[0].kind && strcmp(result.deliveries[0].kind, "reliable") == 0);
  CHECK(result.deliveries[0].length == 5 && memcmp(result.deliveries[0].payload, payload, 5) == 0);
  CHECK(result.reply_count == 1);
  qa_unified_receive_result ack_result;
  (void)qa_unified_channel_receive(&a, result.replies[0], result.reply_lens[0], 20, &ack_result);
  CHECK(qa_unified_channel_can_send_reliable(&a));
  qa_unified_receive_result_destroy(&ack_result);
  qa_unified_receive_result_destroy(&result);
  for (size_t i = 0; i < count; i++) free(packets[i]);
  free(packets); free(lens);
  qa_unified_channel_destroy(&a);
  qa_unified_channel_destroy(&b);
}

static void test_move_checksum_and_rank(void) {
  qa_quakeworld_profile profile = qa_quakeworld_profile_make(28, 0);
  qa_qw_user_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.milliseconds = 16; cmd.forward_move = 200; cmd.buttons = 1;
  qa_quakeworld_move bundle;
  bundle.oldest = cmd; bundle.previous = cmd; bundle.current = cmd; bundle.loss_percent = 0;
  qa_size_buf sb;
  CHECK(qa_size_buf_init(&sb, 256, false));
  qa_write_quakeworld_move(&sb, profile, &bundle, 123);
  size_t n = 0;
  uint8_t *bytes = qa_size_buf_bytes(&sb, &n);
  size_t count = 0;
  qa_quakeworld_client_message *decoded = qa_decode_quakeworld_client(bytes, n, profile, 123, &count);
  CHECK(count == 1 && decoded[0].kind && strcmp(decoded[0].kind, "move") == 0);
  CHECK(decoded[0].bundle.current.forward_move == 200);
  for (size_t i = 0; i < count; i++) qa_quakeworld_client_message_destroy(&decoded[i]);
  free(decoded);
  CHECK_ABORTS(free(qa_decode_quakeworld_client(bytes, n, profile, 124, &count)));
  free(bytes);
  qa_size_buf_destroy(&sb);

  uint8_t source[3] = { 0xff, 0x00, 0x80 };
  uint8_t encoded[8] = {0};
  size_t written = qa_rank_ascii_encode(encoded, sizeof encoded, source, 3);
  uint8_t roundtrip[3] = {0};
  size_t read = qa_rank_ascii_decode(roundtrip, sizeof roundtrip, (const char *)encoded, written);
  CHECK(read == 3 && memcmp(roundtrip, source, 3) == 0);
}

int main(void) {
  test_checksum2();
  test_message_and_netquake();
  test_fragments_ipx();
  test_unified();
  test_move_checksum_and_rank();
  printf("test_network_a: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
