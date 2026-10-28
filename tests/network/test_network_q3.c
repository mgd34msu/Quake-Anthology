/*
 * Port of key cases from tests/network/q3/network.test.ts (msg.c fixture,
 * fragment boundaries, download window, pure verification) plus slice
 * roundtrips. TypeScript `toThrow(RangeError)` cases run in a child
 * process and must abort. GPL-2.0-or-later.
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

static void hex_of(const uint8_t *data, size_t len, char *out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) { out[i*2] = digits[data[i] >> 4]; out[i*2+1] = digits[data[i] & 15]; }
  out[len*2] = 0;
}

static void test_message_fixture(void) {
  qa_q3_message_writer *w = NULL;
  CHECK(qa_q3_message_writer_init_default(&w));
  qa_q3_message_writer_write_bits(w, 5, 3);
  qa_q3_message_writer_write_byte(w, 0);
  qa_q3_message_writer_write_byte(w, 255);
  qa_q3_message_writer_write_short(w, -1234);
  qa_q3_message_writer_write_long(w, 0x12345678);
  qa_q3_message_writer_write_float(w, 1.5);
  qa_q3_message_writer_write_string(w, "quake%");
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_message_writer_to_bytes(w, &bytes, &len));
  char hex[128]; hex_of(bytes, len, hex);
  CHECK(strcmp(hex, "95a48fdad27853f0956a3e46200f3158fe02") == 0);
  CHECK(qa_q3_message_writer_bit_position(w) == 138);
  qa_q3_message_reader *r = NULL;
  CHECK(qa_q3_message_reader_init(&r, bytes, len, QA_Q3_MESSAGE_BITSTREAM, NULL));
  CHECK(qa_q3_message_reader_read_bits(r, 3) == 5);
  CHECK(qa_q3_message_reader_read_byte(r) == 0);
  CHECK(qa_q3_message_reader_read_byte(r) == 255);
  CHECK(qa_q3_message_reader_read_short(r) == -1234);
  CHECK(qa_q3_message_reader_read_long(r) == 0x12345678);
  CHECK(qa_q3_message_reader_read_float(r) == 1.5);
  char *text = NULL;
  CHECK(qa_q3_message_reader_read_string(r, &text));
  CHECK(text && strcmp(text, "quake.") == 0);
  free(text); free(bytes);
  qa_q3_message_reader_destroy(r);
  qa_q3_message_writer_destroy(w);
}

static void test_netchan_fragments(void) {
  qa_q3_netchannel *sender = NULL, *receiver = NULL;
  CHECK(qa_q3_netchannel_init(&sender, QA_Q3_CHANNEL_CLIENT, 27961, NULL, NULL));
  CHECK(qa_q3_netchannel_init(&receiver, QA_Q3_CHANNEL_SERVER, 27961, NULL, NULL));
  uint8_t bytes[2600];
  for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t)i;
  uint8_t **packets = NULL; size_t *lengths = NULL, count = 0;
  CHECK(qa_q3_netchannel_transmit(sender, bytes, sizeof bytes, &packets, &lengths, &count));
  CHECK(count == 3);
  CHECK(lengths[0] == 1310 && lengths[1] == 1310 && lengths[2] == 10);
  qa_q3_channel_result result;
  CHECK(qa_q3_netchannel_receive(receiver, packets[0], lengths[0], NULL, &result));
  CHECK(result.kind == QA_Q3_CHANNEL_FRAGMENT);
  CHECK(qa_q3_netchannel_receive(receiver, packets[1], lengths[1], NULL, &result));
  CHECK(result.kind == QA_Q3_CHANNEL_FRAGMENT);
  CHECK(qa_q3_netchannel_receive(receiver, packets[2], lengths[2], NULL, &result));
  CHECK(result.kind == QA_Q3_CHANNEL_ACCEPTED);
  CHECK(result.qport == 27961);
  CHECK(result.payload_len == sizeof bytes && memcmp(result.payload, bytes, sizeof bytes) == 0);
  free(result.payload);
  CHECK(qa_q3_netchannel_receive(receiver, packets[2], lengths[2], NULL, &result));
  CHECK(result.kind == QA_Q3_CHANNEL_REJECTED && result.reason == QA_Q3_REJECT_SEQUENCE);
  for (size_t i = 0; i < count; i++) free(packets[i]);
  free(packets); free(lengths);
  qa_q3_netchannel_destroy(sender);
  qa_q3_netchannel_destroy(receiver);
}

static void test_huffman_and_hash(void) {
  const char *input = "hello hello";
  uint8_t *compressed = NULL; size_t clen = 0;
  CHECK(qa_q3_compress_adaptive((const uint8_t *)input, strlen(input), &compressed, &clen));
  char hex[64]; hex_of(compressed, clen, hex);
  CHECK(strcmp(hex, "000b164cb169ec87e07c01") == 0);
  uint8_t *plain = NULL; size_t plen = 0;
  CHECK(qa_q3_decompress_adaptive_default(compressed, clen, &plain, &plen));
  CHECK(plen == strlen(input) && memcmp(plain, input, plen) == 0);
  free(compressed); free(plain);
  CHECK(qa_q3_command_hash_default("") == 0);
  CHECK(qa_q3_command_hash_default("disconnect") == 131811);
  CHECK(qa_q3_command_hash_default("cs 1 \"x\"") == 62894);
}

static void test_pure_verify(void) {
  int64_t loaded[2] = { (int64_t)0xffffffffLL, 7 };
  qa_q3_pure_server server = { true, 99, 42, true, (int64_t)0xffffffffLL, true, 7, loaded, 2 };
  int64_t checksum = (int32_t)(99 ^ -1 ^ 7 ^ 2);
  char checksum_text[24]; snprintf(checksum_text, sizeof checksum_text, "%lld", (long long)checksum);
  const char *argv1[] = { "cp", "42", "-1", "7", "@", "-1", "7", checksum_text };
  CHECK(qa_q3_verify_pure_command(&server, argv1, 8).kind == QA_Q3_PURE_AUTHENTIC);
  const char *argv2[] = { "cp", "42", "-1", "7", "@", "7", "7", "97" };
  CHECK(qa_q3_verify_pure_command(&server, argv2, 8).kind == QA_Q3_PURE_REJECTED);
  const char *argv3[] = { "cp", "41" };
  qa_q3_pure_result r = qa_q3_verify_pure_command(&server, argv3, 2);
  CHECK(r.kind == QA_Q3_PURE_IGNORED && r.ignored_reason == QA_Q3_PURE_OUTDATED);
}

typedef struct { const uint8_t *data; size_t len, offset; } test_file_t;
static int64_t test_read(void *ctx, uint8_t *target, size_t capacity) {
  test_file_t *f = ctx;
  size_t count = f->len - f->offset < capacity ? f->len - f->offset : capacity;
  memcpy(target, f->data + f->offset, count);
  f->offset += count;
  return (int64_t)count;
}
static void test_close_file(void *ctx) { (void)ctx; }
static test_file_t g_file;
static bool test_open(void *ctx, const char *name, qa_q3_download_read_file *out) {
  (void)ctx; (void)name;
  g_file.offset = 0;
  out->size = (int64_t)g_file.len; out->read = test_read; out->close = test_close_file; out->ctx = &g_file;
  return true;
}
static bool test_true(void *ctx) { (void)ctx; return true; }
static bool test_false(void *ctx) { (void)ctx; return false; }
static void test_drop_fail(void *ctx, const char *reason) { (void)ctx; fprintf(stderr, "unexpected drop: %s\n", reason); failures++; }
static uint8_t g_received[4096]; static size_t g_received_len;
static char g_commands[8][64]; static size_t g_command_count;
static bool g_published, g_completed;
static void test_write(void *ctx, const uint8_t *bytes, size_t len) {
  (void)ctx;
  memcpy(g_received + g_received_len, bytes, len); g_received_len += len;
}
static bool test_open_temp(void *ctx, const char *path, qa_q3_download_write_file *out) {
  (void)ctx; (void)path;
  out->write_bytes = test_write; out->close = test_close_file; out->ctx = NULL;
  return true;
}
static void test_publish_temp(void *ctx, const char *t, const char *d) { (void)ctx; (void)t; (void)d; g_published = true; }
static void test_reliable(void *ctx, const char *text) { (void)ctx; snprintf(g_commands[g_command_count++], 64, "%s", text); }
static void test_noop(void *ctx) { (void)ctx; }
static void test_completed(void *ctx) { (void)ctx; g_completed = true; }
static void test_progress(void *ctx, const char *n, int64_t c, int64_t s) { (void)ctx; (void)n; (void)c; (void)s; }
static qa_q3_entity_state_record *null_baseline(void *ctx, int64_t n) { (void)ctx; (void)n; return NULL; }
static const qa_q3_snapshot_history_entry *null_history(void *ctx, int64_t n) { (void)ctx; (void)n; return NULL; }

static void test_download_window(void) {
  static uint8_t data[4096];
  for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)i;
  g_file.data = data; g_file.len = sizeof data; g_file.offset = 0;
  g_received_len = 0; g_command_count = 0; g_published = false; g_completed = false;
  qa_q3_download_server_bindings sb = { test_open, test_true, test_false, test_drop_fail, NULL, NULL, NULL };
  qa_q3_server_download *server = NULL;
  CHECK(qa_q3_server_download_init(&server, &sb));
  qa_q3_download_client_bindings cb = { test_noop, test_open_temp, test_publish_temp, test_reliable, test_noop, test_progress, test_completed, NULL };
  qa_q3_client_download *client = NULL;
  CHECK(qa_q3_client_download_init(&client, &cb));
  qa_q3_server_download_begin(server, "mods/example.pk3");
  qa_q3_client_download_begin(client, "mods/example.pk3", "mods/example.pk3");
  qa_q3_message_writer *writer = NULL;
  CHECK(qa_q3_message_writer_init_default(&writer));
  qa_q3_message_writer_write_long(writer, 0);
  qa_q3_download_rate rate = { 100000, 0, 50 };
  qa_q3_server_download_write(server, writer, 2000, &rate);
  qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_EOF);
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_message_writer_to_bytes(writer, &bytes, &len));
  qa_q3_server_message_context ctx = { QA_Q3_PRODUCT_BASEQ3, 1, 0, 0, 0, null_baseline, NULL, null_history, NULL };
  qa_q3_server_message decoded;
  CHECK(qa_q3_decode_server_message(bytes, len, &ctx, NULL, &decoded));
  for (size_t i = 0; i < decoded.operation_count; i++) {
    qa_q3_server_operation *op = &decoded.operations[i];
    if (op->kind == QA_Q3_OP_DOWNLOAD) {
      if (op->download.kind == QA_Q3_DOWNLOAD_START) qa_q3_client_download_publish_size(client, op->download.file_size);
      qa_q3_client_download_receive(client, &op->download);
    }
  }
  CHECK(g_received_len == sizeof data && memcmp(g_received, data, sizeof data) == 0);
  CHECK(g_published && g_completed);
  CHECK(g_command_count == 4);
  CHECK(strcmp(g_commands[0], "download mods/example.pk3") == 0);
  CHECK(strcmp(g_commands[1], "nextdl 0") == 0);
  CHECK(strcmp(g_commands[2], "nextdl 1") == 0);
  CHECK(strcmp(g_commands[3], "nextdl 2") == 0);
  for (int block = 0; block < 3; block++) qa_q3_server_download_acknowledge(server, block, 2100);
  CHECK(qa_q3_server_download_name(server)[0] == 0);
  qa_q3_server_message_destroy(&decoded);
  free(bytes);
  qa_q3_message_writer_destroy(writer);
  qa_q3_client_download_destroy(client);
  qa_q3_server_download_destroy(server);
}

static void test_delta_and_reliable(void) {
  /* Entity/player delta roundtrip through the bitstream. */
  qa_q3_entity_state_record from, to, back;
  qa_q3_entity_state_record_init(&from, 0);
  qa_q3_entity_state_record_init(&to, 0);
  to.number = 7; to.e_type = 3; to.origin.x = 100.5; to.origin.y = -20; to.event = 9;
  to.pos.base.z = 42;
  qa_q3_message_writer *w = NULL;
  CHECK(qa_q3_message_writer_init_default(&w));
  qa_q3_write_delta_entity(w, &from, &to, false);
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_message_writer_to_bytes(w, &bytes, &len));
  qa_q3_message_reader *r = NULL;
  CHECK(qa_q3_message_reader_init(&r, bytes, len, QA_Q3_MESSAGE_BITSTREAM, NULL));
  CHECK(qa_q3_message_reader_read_bits(r, 10) == 7);
  qa_q3_read_delta_entity(r, &from, 7, NULL, &back);
  CHECK(back.number == 7 && back.e_type == 3 && back.event == 9);
  CHECK(back.origin.x == 100.5 && back.origin.y == -20 && back.pos.base.z == 42);
  qa_q3_message_reader_destroy(r);
  qa_q3_message_writer_destroy(w);
  free(bytes);
  /* Reliable ring: command 65 overwrites the oldest slot for the client ring. */
  qa_q3_client_reliable_commands *ring = NULL;
  CHECK(qa_q3_client_reliable_commands_init(&ring));
  qa_q3_reliable_command cmd;
  for (int i = 0; i < 65; i++) {
    char text[24]; snprintf(text, sizeof text, "cmd%d", i + 1);
    CHECK(qa_q3_client_reliable_commands_add(ring, text, &cmd));
  }
  CHECK(strcmp(qa_q3_reliable_ring_lookup(ring, 1), "cmd65") == 0);
  qa_q3_reliable_ring_destroy(ring);
  /* Connectionless connect compression roundtrip. */
  uint8_t *packet = NULL; size_t plen = 0;
  CHECK(qa_q3_encode_connect("\\protocol\\68\\qport\\27001", &packet, &plen));
  qa_q3_connectionless_packet decoded_packet;
  CHECK(qa_q3_decode_connectionless(packet, plen, QA_Q3_CONNLESS_SERVER, NULL, &decoded_packet));
  CHECK(strcmp(decoded_packet.command, "connect") == 0);
  CHECK(decoded_packet.compression == QA_Q3_COMPRESSION_ADAPTIVE);
  CHECK(decoded_packet.argument_count == 1 && strstr(decoded_packet.arguments[0], "protocol") != NULL);
  qa_q3_connectionless_packet_destroy(&decoded_packet);
  free(packet);
}

static void test_aborts(void) {
  CHECK_ABORTS(qa_q3_message_writer *w; qa_q3_message_writer_init(&w, QA_Q3_MESSAGE_BITSTREAM, 99999, NULL));
  CHECK_ABORTS(qa_q3_player_state_record s; qa_q3_player_state_record_init(&s, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0); qa_q3_player_state_slot_set(&s, 2, 16, 1));
  CHECK_ABORTS(qa_q3_check_download_name("../evil.pk3"));
}

static void test_address_bridge(void) {
  qa_q3_address v4; memset(&v4, 0, sizeof v4);
  v4.kind = QA_Q3_ADDR_IPV4; v4.host[0] = 192; v4.host[1] = 168; v4.host[2] = 0; v4.host[3] = 7;
  v4.port = 27960;
  qa_net_address net; memset(&net, 0, sizeof net);
  CHECK(qa_q3_address_to_net(&v4, &net));
  CHECK(net.kind == QA_NET_IPV4 && net.ipv4[0] == 192 && net.ipv4[3] == 7 && net.port == 27960);
  char *key = qa_q3_mirror_address_key(&v4, true);
  CHECK(key && strcmp(key, "192.168.0.7:27960") == 0);
  qa_q3_address back; memset(&back, 0, sizeof back);
  CHECK(qa_q3_address_from_net(&net, &back));
  CHECK(qa_q3_mirror_same_address(&v4, &back, true));
  back.port = 1;
  CHECK(!qa_q3_mirror_same_address(&v4, &back, true));
  CHECK(qa_q3_mirror_same_address(&v4, &back, false));
  free(key);
  qa_net_address_destroy(&net);
  qa_q3_mirror_address_destroy(&back);
  /* Loopback id roundtrips through the owner host string. */
  qa_net_address loop; memset(&loop, 0, sizeof loop);
  CHECK(qa_net_make_loopback(&loop, "seat-2") == 1);
  qa_q3_address q3loop; memset(&q3loop, 0, sizeof q3loop);
  CHECK(qa_q3_address_from_net(&loop, &q3loop));
  CHECK(q3loop.kind == QA_Q3_ADDR_LOOPBACK && q3loop.loopback_id &&
        strcmp(q3loop.loopback_id, "seat-2") == 0);
  char *lkey = qa_q3_mirror_address_key(&q3loop, true);
  CHECK(lkey && strcmp(lkey, "loopback:seat-2") == 0);
  free(lkey);
  qa_q3_mirror_address_destroy(&q3loop);
  qa_net_address_destroy(&loop);
}

int main(void) {
  test_address_bridge();
  test_message_fixture();
  test_netchan_fragments();
  test_huffman_and_hash();
  test_pure_verify();
  test_download_window();
  test_delta_and_reliable();
  test_aborts();
  printf("test_network_q3: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
