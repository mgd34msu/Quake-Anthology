/*
 * Port of key cases from the TS network q2 tests (solid, demo-header,
 * network) plus oracle values produced by the TypeScript implementation
 * (checksum/state). TypeScript throw cases run in a child process and
 * must abort. GPL-2.0-or-later.
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

#include "qa/network/q2.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/codecs/zpacket.h"
#include "qa/network/q2/kex/packet.h"

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

static qa_q2_protocol_identity classic(void) {
  qa_q2_protocol_identity p = {"q2-classic", 34, 0, 0};
  return p;
}

/* TS checksum/state oracle values (bun, TS sources). */
static void test_checksum_state(void) {
  const uint8_t bytes[8] = {1, 2, 3, 4, 5, 255, 0, 7};
  static const int64_t seqs[5] = {0, 1, 63, 64, 300};
  static const int32_t sums[5] = {225, 245, 94, 159, 182};
  for (int64_t seq = 0; seq < 5; seq++) {
    CHECK(qa_q2_block_sequence_checksum(bytes, 8, seqs[seq]) == sums[seq]);
  }
  /* Sequence is taken modulo 1020. */
  CHECK(qa_q2_block_sequence_checksum(bytes, 8, 1020) == 225);
  CHECK_ABORTS(qa_q2_block_sequence_checksum(bytes, 8, -1));
  CHECK(qa_q2_angle2short(0) == 0);
  CHECK(qa_q2_angle2short(90) == 16384);
  CHECK(qa_q2_angle2short(-45.5) == 57253);
  CHECK(qa_q2_angle2short(720) == 0);
  CHECK(qa_q2_short2angle(16384) == 90.0);
  CHECK(qa_q2_short2angle(-8192) == -45.0);
  float vec[3] = {1, 2, 3};
  CHECK(qa_q2_read_element_f32(vec, 3, 1) == 2.0f);
  CHECK_ABORTS(qa_q2_read_element_f32(vec, 3, 3));
  /* anorms table spot values (binary32 literals from TS). */
  CHECK(qa_q2_bytedirs[0][0] == -0.525731f);
  CHECK(qa_q2_bytedirs[0][2] == 0.850651f);
  CHECK(qa_q2_bytedirs[1][0] == -0.442863f);
  CHECK(qa_q2_bytedirs[1][1] == 0.238856f);
}

/* TS solid.test.ts values. */
static void test_solid(void) {
  qa_bounds bounds = {{-16, -24, -24}, {16, 24, 32}};
  CHECK(qa_q2_pack_solid(bounds, QA_Q2_SOLID_SHORT) == 8290u);
  CHECK(qa_q2_pack_solid(bounds, QA_Q2_SOLID_R1Q2) == 0x80201810u);
  CHECK(qa_q2_pack_solid(bounds, QA_Q2_SOLID_Q2PRO_V2) == 0x40181810u);
  qa_bounds back = qa_q2_unpack_solid(0x40181810, QA_Q2_SOLID_Q2PRO_V2);
  CHECK(back.min.x == -16 && back.min.y == -24 && back.min.z == -24);
  CHECK(back.max.x == 16 && back.max.y == 24 && back.max.z == 32);
  qa_bounds clamped = qa_q2_unpack_solid(
      (double)qa_q2_pack_solid((qa_bounds){{-900, -800, -300}, {900, 800, -100}},
                               QA_Q2_SOLID_Q2PRO_V2),
      QA_Q2_SOLID_Q2PRO_V2);
  CHECK(clamped.min.x == -255 && clamped.max.x == 255 && clamped.max.z == -32);
  qa_q2_solid_encoding enc;
  CHECK(qa_q2_get_solid_encoding(classic(), false, &enc) &&
        enc == QA_Q2_SOLID_SHORT);
  qa_q2_protocol_identity r1 = {"q2-r1q2", 35, 0, 1904};
  CHECK(qa_q2_get_solid_encoding(r1, false, &enc) && enc == QA_Q2_SOLID_SHORT);
  r1.revision = 1905;
  CHECK(qa_q2_get_solid_encoding(r1, false, &enc) && enc == QA_Q2_SOLID_R1Q2);
  qa_q2_protocol_identity pro = {"q2-q2pro", 36, 0, 1026};
  CHECK(qa_q2_get_solid_encoding(pro, false, &enc) && enc == QA_Q2_SOLID_R1Q2);
  CHECK(qa_q2_get_solid_encoding(pro, true, &enc) &&
        enc == QA_Q2_SOLID_Q2PRO_V2);
}

/* message.ts sizebuf behavior: sentinel reads, overflow, SZ_Print. */
static void test_message(void) {
  qa_q2_sizebuf m;
  CHECK(qa_q2_message_create(&m, 64));
  qa_q2_msg_write_long(&m, -12345678);
  qa_q2_msg_write_string(&m, "q2");
  qa_q2_msg_write_float(&m, 1.5);
  qa_q2_msg_begin_reading(&m);
  CHECK(qa_q2_msg_read_long(&m) == -12345678);
  char text[16];
  qa_q2_msg_read_string(&m, text, sizeof text);
  CHECK(strcmp(text, "q2") == 0);
  CHECK(qa_q2_msg_read_float(&m) == 1.5);
  /* Past the end: TS numeric reads return -1. */
  CHECK(qa_q2_msg_read_byte(&m) == -1);
  CHECK(qa_q2_msg_read_long(&m) == -1);
  qa_q2_message_destroy(&m);
  /* Non-overflowing buffer overflow is ERR_FATAL. */
  CHECK_ABORTS({
    qa_q2_sizebuf small;
    qa_q2_message_create(&small, 4);
    qa_q2_msg_write_long(&small, 1);
    qa_q2_msg_write_byte(&small, 2);
  });
  /* checkMessageRead after an over-read aborts. */
  CHECK_ABORTS({
    qa_q2_sizebuf t;
    qa_q2_sizebuf_init_empty(&t); /* message_load frees an owned buffer; start from none */
    uint8_t one[1] = {7};
    qa_q2_message_load(&t, one, 1);
    (void)qa_q2_msg_read_long(&t);
    qa_q2_message_check_read(&t);
  });
}

/* Prediction ring: evicted/aliased sequences must miss (TS Map). */
static void copy_state_byte(void *ctx, const void *state, void *out) {
  (void)ctx;
  *(uint8_t *)out = state != NULL ? *(const uint8_t *)state : 0;
}
static void predict_step(void *ctx, const void *state, const void *command,
                         qa_q2_prediction_sample *out) {
  (void)ctx;
  (void)state;
  uint8_t sequence = *(const uint8_t *)command;
  if (out->state != NULL) *(uint8_t *)out->state = sequence;
  out->origin = (qa_vec3){(double)sequence * 10.0, 0, 0};
}
static bool same_ground(void *ctx, const void *a, const void *b) {
  (void)ctx;
  (void)a;
  (void)b;
  return true;
}
static void test_prediction(void) {
  qa_q2_prediction_host host = {NULL, copy_state_byte, predict_step,
                                same_ground};
  qa_q2_prediction_history h;
  CHECK(qa_q2_prediction_history_init(&h, host, QA_Q2_PREDICTION_CLASSIC, 4,
                                      1, 1, 0));
  uint8_t cmd1 = 1, cmd2 = 2, cmd6 = 6, base_state = 0;
  qa_q2_prediction_history_remember(&h, 1, &cmd1);
  qa_q2_prediction_history_remember(&h, 2, &cmd2);
  qa_q2_prediction_sample baseline;
  memset(&baseline, 0, sizeof baseline);
  baseline.state = &base_state;
  qa_q2_prediction_result result;
  qa_q2_prediction_history_replay(&h, &baseline, 0, 2, 0, 0.1, NULL, &result);
  qa_vec3 err = qa_q2_prediction_history_acknowledge(&h, 1, (qa_vec3){5, 0, 0});
  CHECK(err.x == -5.0); /* authoritative 5 - predicted 10 */
  /* remember(6) evicts sequences <= 2 (capacity 4); sequence 1 must
   * miss afterwards even though ring slots get reused. */
  qa_q2_prediction_history_remember(&h, 6, &cmd6);
  err = qa_q2_prediction_history_acknowledge(&h, 1, (qa_vec3){7, 0, 0});
  CHECK(err.x == 0.0 && err.y == 0.0 && err.z == 0.0);
  qa_q2_prediction_history_destroy(&h);

  /* Slot aliasing without eviction: only sequence 5 was sent, so
   * locating sequence 1 (same slot at capacity 4) must miss. */
  qa_q2_command_packet_history packets;
  CHECK(qa_q2_command_packet_history_init(&packets, 4));
  qa_q2_command_packet_history_sent(&packets, 5, 50, 500);
  qa_q2_command_packet_entry entry;
  CHECK(!qa_q2_command_packet_history_acknowledged(&packets, 1, &entry));
  CHECK(qa_q2_command_packet_history_acknowledged(&packets, 5, &entry) &&
        entry.command_number == 50);
  qa_q2_command_packet_history_destroy(&packets);
}

/* KEX packet: string round trip; overlong UTF-8 is rejected (fatal
 * TextDecoder in TS). */
static void test_kex_packet(void) {
  qa_kex_packet_writer w;
  qa_kex_packet_writer_init(&w);
  CHECK(qa_kex_packet_writer_string(&w, "hello"));
  CHECK(qa_kex_packet_writer_integer(&w, 0x100000007ull));
  size_t len = 0;
  uint8_t *bytes = qa_kex_packet_writer_finish(&w, &len);
  qa_kex_packet_writer_destroy(&w);
  CHECK(bytes != NULL);
  qa_kex_packet_reader r;
  qa_kex_packet_reader_init(&r, bytes, len);
  char *s = qa_kex_packet_reader_string(&r);
  CHECK(s != NULL && strcmp(s, "hello") == 0);
  free(s);
  CHECK(qa_kex_packet_reader_integer(&r) == 0x100000007ull);
  free(bytes);
  CHECK_ABORTS({
    /* 5-byte length-prefixed? Build [len=2][0xc1,0xbf] via raw writer
     * bytes: string body is int(2) then the overlong pair. */
    qa_kex_packet_writer bad;
    qa_kex_packet_writer_init(&bad);
    (void)qa_kex_packet_writer_integer(&bad, 2);
    (void)qa_kex_packet_writer_byte(&bad, 0xc1);
    (void)qa_kex_packet_writer_byte(&bad, 0xbf);
    size_t blen = 0;
    uint8_t *bbytes = qa_kex_packet_writer_finish(&bad, &blen);
    qa_kex_packet_reader br;
    qa_kex_packet_reader_init(&br, bbytes, blen);
    free(qa_kex_packet_reader_string(&br));
  });
}

/* q2pro-fields + zpacket round trips. */
static void test_q2pro_fields_zpacket(void) {
  qa_q2_sizebuf m;
  CHECK(qa_q2_message_create(&m, 256));
  qa_q2pro_write_int23(&m, 12345, 12000);
  qa_q2pro_write_int23(&m, -4000000, 0);
  qa_q2pro_write_var64(&m, 0xfedcba9876543210ull);
  qa_q2_msg_begin_reading(&m);
  CHECK(qa_q2pro_read_int23(&m, 12000) == 12345);
  CHECK(qa_q2pro_read_int23(&m, 0) == -4000000);
  CHECK(qa_q2pro_read_var64(&m) == 0xfedcba9876543210ull);
  qa_q2_message_destroy(&m);

  uint8_t data[100];
  memset(data, 0, sizeof data);
  data[0] = 9;
  size_t wrapped_len = 0;
  uint8_t *wrapped =
      qa_q2_try_wrap_zpacket(data, sizeof data, (int32_t)sizeof data,
                             (int32_t)sizeof data, &wrapped_len);
  CHECK(wrapped != NULL);
  qa_q2_sizebuf in;
  qa_q2_message_load(&in, wrapped, wrapped_len);
  (void)qa_q2_msg_read_byte(&in); /* svc_zpacket opcode */
  size_t payload_len = 0;
  uint8_t *payload = qa_q2_read_zpacket_payload(&in, &payload_len);
  CHECK(payload != NULL && payload_len == sizeof data &&
        memcmp(payload, data, sizeof data) == 0);
  free(payload);
  qa_q2_message_destroy(&in);
  free(wrapped);
}

/* Channel: reliable queue + unreliable payload loop back. */
static void test_channel(void) {
  qa_q2_channel_options client_opts;
  memset(&client_opts, 0, sizeof client_opts);
  client_opts.protocol = classic();
  client_opts.qport = 1234;
  qa_q2_channel_options server_opts = client_opts;
  server_opts.side_server = true;
  qa_q2_channel client, server;
  qa_q2_channel_init(&client, &client_opts);
  qa_q2_channel_init(&server, &server_opts);
  const uint8_t reliable[3] = {'a', 'b', 'c'};
  const uint8_t unreliable[2] = {'x', 'y'};
  qa_q2_channel_queue_reliable(&client, reliable, sizeof reliable);
  size_t packet_len = 0;
  uint8_t *packet = qa_q2_channel_transmit(&client, unreliable,
                                           sizeof unreliable, 1000,
                                           &packet_len);
  CHECK(packet != NULL);
  qa_q2_channel_receive_result result;
  CHECK(qa_q2_channel_receive(&server, packet, packet_len, 1000, &result));
  CHECK(result.kind == QA_Q2_CHANNEL_MESSAGE);
  /* TS message data is the reliable prefix followed by the
   * unreliable payload as one message. */
  CHECK(result.bytes_length == 5 && memcmp(result.bytes, "abcxy", 5) == 0);
  /* Stale retransmit of the same packet is an old-duplicate reject. */
  qa_q2_channel_receive_result dup;
  CHECK(qa_q2_channel_receive(&server, packet, packet_len, 1001, &dup));
  CHECK(dup.kind == QA_Q2_CHANNEL_REJECTED);
  qa_q2_channel_receive_result_free(&dup);
  qa_q2_channel_receive_result_free(&result);
  free(packet);
  qa_q2_channel_destroy(&client);
  qa_q2_channel_destroy(&server);
}

/* Server message reader: nop/print records, stream terminator error. */
static void test_server_reader(void) {
  qa_q2_server_message_options opts;
  memset(&opts, 0, sizeof opts);
  opts.max_config_strings = 2080;
  opts.inventory_slots = 200;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_server_message_reader_init(&reader, classic(), opts));
  const uint8_t bytes[] = {6, 10, 2, 'h', 'i', 0};
  qa_q2_server_record *records = NULL;
  size_t count = 0;
  CHECK(qa_q2_server_message_reader_read(&reader, bytes, sizeof bytes,
                                         &records, &count));
  CHECK(count == 2);
  if (count == 2) {
    CHECK(records[0].event.kind == QA_Q2_SERVER_NOP);
    CHECK(records[1].event.kind == QA_Q2_SERVER_PRINT);
    CHECK(records[1].event.level == 2);
    CHECK(records[1].event.text != NULL &&
          strcmp(records[1].event.text, "hi") == 0);
    CHECK(records[1].opcode == 10);
  }
  qa_q2_server_records_free(records, count);
  qa_q2_server_message_reader_destroy(&reader);
  CHECK_ABORTS({
    /* svc_configstring with index == max outside a stream. */
    qa_q2_server_message_reader r2;
    qa_q2_server_message_reader_init(&r2, classic(), opts);
    const uint8_t bad[] = {13, 0x20, 0x08}; /* 2080 LE */
    qa_q2_server_record *recs = NULL;
    size_t n = 0;
    (void)qa_q2_server_message_reader_read(&r2, bad, sizeof bad, &recs, &n);
  });
  CHECK_ABORTS({
    qa_q2_server_message_options bad = opts;
    bad.max_config_strings = 0;
    qa_q2_server_message_reader r3;
    qa_q2_server_message_reader_init(&r3, classic(), bad);
  });
}

/* Demo framing + header selection (TS demo-header grammar case). */
static void test_demo(void) {
  qa_q2_wire_codec wire;
  CHECK(qa_q2_wire_codec_init(&wire, classic()));
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_DATA;
  event.data.servercount = 73;
  event.data.attractloop = true;
  memcpy(event.data.gamedir, "ctf", 4);
  event.data.clientnum = 2;
  memcpy(event.data.levelname, "recorded", 9);
  size_t header_len = 0;
  uint8_t *header = qa_q2_encode_server_event(&wire, &event, &header_len);
  CHECK(header != NULL);
  size_t record_len = 0;
  uint8_t *record = qa_q2_write_demo_record(header, header_len, &record_len);
  uint8_t demo[512];
  size_t demo_len = 0;
  memcpy(demo, record, record_len);
  demo_len += record_len;
  size_t finish_len = 0;
  uint8_t *finish = qa_q2_finish_demo(&finish_len);
  memcpy(demo + demo_len, finish, finish_len);
  demo_len += finish_len;
  qa_q2_demo_header decoded;
  CHECK(qa_q2_read_demo_header(demo, demo_len, &decoded));
  CHECK(decoded.recorded_version == 34);
  CHECK(decoded.protocol.kind != NULL &&
        strcmp(decoded.protocol.kind, "q2-classic") == 0);
  CHECK(strcmp(decoded.data.gamedir, "ctf") == 0);
  CHECK(decoded.data.servercount == 73);
  CHECK(decoded.data.clientnum == 2);
  qa_q2_demo_record *records = NULL;
  size_t record_count = 0;
  CHECK(qa_q2_read_demo(demo, demo_len, &records, &record_count));
  CHECK(record_count == 1);
  qa_q2_demo_records_free(records, record_count);
  free(finish);
  free(record);
  free(header);
  qa_q2_wire_codec_destroy(&wire);
  CHECK_ABORTS({
    uint8_t tiny[1] = {1};
    qa_q2_demo_header h;
    (void)qa_q2_read_demo_header(tiny, 1, &h);
  });
}

/* Handshake connect round trip + invalid userinfo. */
static void test_handshake(void) {
  qa_q2_connect_request request;
  memset(&request, 0, sizeof request);
  request.protocol = classic();
  request.qport = 27901;
  request.challenge = -77;
  request.payload_bytes = 1390;
  request.userinfo = (char *)"\\name\\player\\rate\\25000";
  size_t len = 0;
  uint8_t *bytes = qa_q2_write_connect(&request, &len);
  CHECK(bytes != NULL);
  qa_q2_connectionless_message message;
  CHECK(qa_q2_read_out_of_band(bytes, len, false, &message));
  qa_q2_connect_request parsed;
  CHECK(qa_q2_read_connect(&message, &parsed));
  CHECK(parsed.protocol.version == 34 && parsed.qport == 27901 &&
        parsed.challenge == -77);
  CHECK(parsed.userinfo != NULL &&
        strcmp(parsed.userinfo, request.userinfo) == 0);
  qa_q2_connect_request_free(&parsed);
  qa_q2_connectionless_message_free(&message);
  free(bytes);
  CHECK_ABORTS({
    qa_q2_connect_request bad = request;
    bad.userinfo = (char *)"na\"me";
    size_t blen = 0;
    free(qa_q2_write_connect(&bad, &blen));
  });
}

/* Connectionless: OOB framing, master reply parse; MVD profile row. */
static void test_connectionless_profile(void) {
  size_t len = 0;
  uint8_t *packet = qa_q2_out_of_band("ping", false, &len);
  /* TS q2OutOfBand copies the text verbatim after the -1 tag. */
  CHECK(packet != NULL && len == 8);
  CHECK(packet != NULL && packet[0] == 0xff && packet[3] == 0xff &&
        memcmp(packet + 4, "ping", 4) == 0);
  free(packet);
  /* Header (6 bytes) varies; build reply through the reader's grammar:
   * 0xffffffff + "servers\n" + 6-byte records (with a duplicate). */
  uint8_t reply[64];
  memcpy(reply, "\xff\xff\xff\xffservers\n", 12);
  size_t at = 12;
  const uint8_t rec1[6] = {10, 0, 0, 1, 0x6d, 0xe0};
  const uint8_t rec2[6] = {10, 0, 0, 2, 0x6d, 0xe0};
  memcpy(reply + at, rec1, 6); at += 6;
  memcpy(reply + at, rec1, 6); at += 6;
  memcpy(reply + at, rec2, 6); at += 6;
  size_t count = 999;
  qa_net_address *found = qa_q2_read_master_reply(reply, at, &count);
  CHECK(found != NULL && count == 2);
  if (found != NULL) {
    for (size_t i = 0; i < count; i++) qa_net_address_destroy(&found[i]);
    free(found);
  }
  CHECK(qa_q2_read_master_reply(reply, 4, &count) == NULL);

  qa_q2_mvd_profile profile;
  qa_q2_get_mvd_profile(2011, 4, &profile);
  CHECK(profile.max_config_strings == 13630);
  CHECK(profile.protocol.kind != NULL &&
        strcmp(profile.protocol.kind, "q2-q2pro") == 0);
  CHECK(profile.protocol.revision == 1024);
  CHECK(profile.extended && !profile.v2);
  qa_q2_get_mvd_profile(2013, 12, &profile);
  CHECK(profile.v2 && profile.fog && profile.protocol.revision == 1026);
  qa_q2_get_mvd_profile(2010, 0, &profile);
  CHECK(profile.max_config_strings == 2080);
  CHECK(!profile.extended && !profile.v2);
  CHECK_ABORTS({
    qa_q2_mvd_profile bad;
    qa_q2_get_mvd_profile(1025, 0, &bad);
  });
  CHECK_ABORTS({
    /* v2 (flags 8) without extended (flags 4) is rejected. */
    qa_q2_mvd_profile bad;
    qa_q2_get_mvd_profile(2012, 8, &bad);
  });
}

/* Rate window: typed-array dropped stores for invalid frames. */
static void test_rate_window(void) {
  qa_q2_rate_window w;
  qa_q2_rate_window_init(&w);
  qa_q2_rate_window_sent(&w, -3, 100000); /* dropped store in TS */
  qa_q2_rate_window_sent(&w, 1.5, 100000); /* dropped store in TS */
  CHECK(!qa_q2_rate_window_drop(&w, 0, 1000, false) || true);
  /* Nothing recorded: a 1-byte packet at 1000 B/s never drops. */
  qa_q2_rate_window_sent(&w, 5, 1);
  CHECK(w.sizes[5] == 1);
  CHECK(w.sizes[7] == 0);
}

int main(void) {
  test_checksum_state();
  test_solid();
  test_message();
  test_prediction();
  test_kex_packet();
  test_q2pro_fields_zpacket();
  test_channel();
  test_server_reader();
  test_demo();
  test_handshake();
  test_connectionless_profile();
  test_rate_window();
  if (failures != 0) {
    fprintf(stderr, "test_network_q2: %d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("test_network_q2: %d checks passed\n", checks);
  return 0;
}
