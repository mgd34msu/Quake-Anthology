/* Remote-input hardening for the Q2 network package: every wire decoder a peer reaches is run
 * over truncated and mutated packets under qa_try (no crash; under ASan, no leak), the zlib bomb
 * from tests/network/hardening/q2-inflate-limit.test.ts, and readQ2DownloadServer against
 * bytes captured from the TypeScript. GPL-2.0-or-later. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/q2/client_messages.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/zpacket.h"
#include "qa/network/q2/gtv.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/kex/channel.h"
#include "qa/network/q2/kex/discovery.h"
#include "qa/network/q2/kex/packet.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_recording.h"
#include "qa/network/q2/server_messages.h"
#include "q2_depth_corpus.h"
#include "q2_dlserver_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint32_t rng_state = 0x2545f491u;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
static uint8_t *from_hex(const char *hex, size_t *length) {
  size_t n = strlen(hex) / 2;
  uint8_t *out = malloc(n ? n : 1);
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  *length = n;
  return out;
}

/* One decode attempt over `bytes`; raises are the TS throws a caller catches. */
typedef void (*decode_fn)(void *ctx, const uint8_t *bytes, size_t length);
typedef struct attempt { decode_fn fn; void *ctx; const uint8_t *bytes; size_t length; } attempt;
static void attempt_body(void *arg) {
  attempt *a = arg;
  a->fn(a->ctx, a->bytes, a->length);
}
static size_t raised_total = 0;
static bool decode_once(decode_fn fn, void *ctx, const uint8_t *bytes, size_t length) {
  attempt a = {fn, ctx, bytes, length};
  qa_caught_error caught;
  bool ok = qa_try(attempt_body, &a, &caught);
  if (!ok) raised_total++;
  return ok;
}
/* Truncate at every length, then mutate with a fixed-seed PRNG. */
static void fuzz(decode_fn fn, void *ctx, const uint8_t *seed, size_t length, int rounds) {
  uint8_t *work = malloc(length + 16);
  for (size_t cut = 0; cut <= length; cut++) {
    memcpy(work, seed, cut);
    decode_once(fn, ctx, work, cut);
  }
  for (int round = 0; round < rounds; round++) {
    size_t n = length;
    memcpy(work, seed, length);
    int edits = 1 + (int)(rng() % 4);
    for (int e = 0; e < edits && n > 0; e++) {
      uint32_t r = rng();
      switch (r % 4) {
        case 0: work[r / 4 % n] = (uint8_t)rng(); break;
        case 1: work[r / 4 % n] ^= (uint8_t)(1u << (rng() % 8)); break;
        case 2: work[r / 4 % n] = (rng() & 1) ? 0xff : 0x00; break;
        default: n = (size_t)(r / 4 % n); break;
      }
    }
    decode_once(fn, ctx, work, n);
  }
  free(work);
  checks++;
}

/* --- Server messages (client side of a remote server) --- */
static void server_read(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q2_server_message_reader *reader = ctx;
  qa_q2_server_record *records = NULL;
  size_t count = 0;
  if (qa_q2_server_message_reader_read(reader, bytes, length, &records, &count))
    qa_q2_server_records_free(records, count);
}
static qa_q2_protocol_identity protocol_of(const char *kind, double version, double revision) {
  qa_q2_protocol_identity protocol;
  memset(&protocol, 0, sizeof protocol);
  protocol.kind = kind;
  protocol.version = (int64_t)version;
  protocol.revision = (int64_t)revision;
  return protocol;
}
static void test_server_messages(void) {
  for (size_t row = 0; row < sizeof q2_rows / sizeof *q2_rows; row++) {
    qa_q2_protocol_identity protocol = protocol_of(q2_rows[row].kind, q2_rows[row].version,
                                                   q2_rows[row].revision);
    qa_q2_server_message_options options;
    memset(&options, 0, sizeof options);
    options.max_config_strings = 32767;
    options.inventory_slots = 256;
    qa_q2_server_message_reader reader;
    CHECK(qa_q2_server_message_reader_init(&reader, protocol, options));
    const char *seeds[] = {q2_rows[row].first, q2_rows[row].rich, q2_rows[row].rich2};
    for (size_t i = 0; i < 3; i++) {
      if (seeds[i] == NULL) continue;
      size_t length = 0;
      uint8_t *bytes = from_hex(seeds[i], &length);
      fuzz(server_read, &reader, bytes, length, 300);
      free(bytes);
    }
    /* print, configstring, sound, download text: classic opcodes shared by every profile. */
    static const uint8_t mixed[] = {10, 2, 'h', 'i', 0, 13, 5, 0, 'x', 0, 9, 0x1f, 3, 200, 32, 10, 9, 0,
                                    0, 1, 0, 2, 0, 3, 0, 16, 3, 0, 50, 1, 2, 3};
    fuzz(server_read, &reader, mixed, sizeof mixed, 300);
    qa_q2_server_message_reader_destroy(&reader);
  }
  /* A zpacket envelope (r1q2/q2pro opcode 21) around a print. */
  qa_q2_protocol_identity r1 = protocol_of("q2-r1q2", 35, 1905);
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 2048;
  options.inventory_slots = 256;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_server_message_reader_init(&reader, r1, options));
  uint8_t inner[64];
  inner[0] = 10;
  inner[1] = 2;
  for (int i = 0; i < 40; i++) inner[2 + i] = 'a';
  inner[42] = 0;
  size_t wrapped_length = 0;
  uint8_t *wrapped = qa_q2_try_wrap_zpacket(inner, 43, 43, 1400, &wrapped_length);
  CHECK(wrapped != NULL);
  if (wrapped) {
    qa_q2_server_record *records = NULL;
    size_t count = 0;
    CHECK(qa_q2_server_message_reader_read(&reader, wrapped, wrapped_length, &records, &count));
    CHECK(count == 1 && records[0].event.kind == QA_Q2_SERVER_PRINT);
    qa_q2_server_records_free(records, count);
    fuzz(server_read, &reader, wrapped, wrapped_length, 400);
  }
  free(wrapped);
  qa_q2_server_message_reader_destroy(&reader);
}

/* --- Zpacket payload on its own: the ComError text follows the Node message. --- */
typedef struct zcase { qa_caught_error caught; } zcase;
static void zpacket_read(void *ctx, const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_q2_sizebuf msg;
  qa_q2_sizebuf_init_empty(&msg);
  uint8_t *copy = malloc(length ? length : 1);
  memcpy(copy, bytes, length);
  qa_q2_sz_init(&msg, copy, length, (int64_t)length);
  msg.cursize = (int64_t)length;
  qa_cleanup node;
  qa_cleanup_push(&node, free, copy);
  size_t out_length = 0;
  free(qa_q2_read_zpacket_payload(&msg, &out_length));
  qa_cleanup_pop(&node);
  free(copy);
}
static const char *zpacket_error(const uint8_t *bytes, size_t length, qa_caught_error *caught) {
  attempt a = {zpacket_read, NULL, bytes, length};
  if (qa_try(attempt_body, &a, caught)) return NULL;
  return caught->message;
}
static void test_zpacket(void) {
  uint8_t plain[200];
  for (int i = 0; i < 200; i++) plain[i] = (uint8_t)(i % 7);
  uLongf bound = compressBound(200);
  uint8_t *deflated = malloc(bound);
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  stream.next_in = plain;
  stream.avail_in = 200;
  stream.next_out = deflated;
  stream.avail_out = (uInt)bound;
  deflate(&stream, Z_FINISH);
  size_t deflated_length = stream.total_out;
  deflateEnd(&stream);
  uint8_t *packet = malloc(deflated_length + 4);
  packet[0] = (uint8_t)deflated_length;
  packet[1] = (uint8_t)(deflated_length >> 8);
  packet[2] = 200;
  packet[3] = 0;
  memcpy(packet + 4, deflated, deflated_length);
  qa_caught_error caught;
  CHECK(zpacket_error(packet, deflated_length + 4, &caught) == NULL);
  /* expected length smaller than the stream: Node's maxOutputLength RangeError text */
  packet[2] = 50;
  const char *message = zpacket_error(packet, deflated_length + 4, &caught);
  CHECK(message && strcmp(message, "zpacket: raw inflate failed: Cannot create a Buffer larger than 50 bytes") == 0);
  CHECK(strcmp(caught.name, "ComError") == 0 && caught.code == 1);
  /* truncated stream */
  packet[2] = 200;
  packet[0] = 3;
  packet[1] = 0;
  message = zpacket_error(packet, 7, &caught);
  CHECK(message && strcmp(message, "zpacket: raw inflate failed: unexpected end of file") == 0);
  /* garbage stream */
  uint8_t garbage[] = {3, 0, 100, 0, 0xff, 0xff, 0xff};
  message = zpacket_error(garbage, sizeof garbage, &caught);
  CHECK(message && strcmp(message, "zpacket: raw inflate failed: invalid block type") == 0);
  /* header past the end */
  uint8_t short_header[] = {3};
  message = zpacket_error(short_header, 1, &caught);
  CHECK(message && strcmp(message, "Truncated Quake II message") == 0);
  packet[0] = (uint8_t)deflated_length;
  packet[1] = (uint8_t)(deflated_length >> 8);
  fuzz(zpacket_read, NULL, packet, deflated_length + 4, 500);
  free(packet);
  free(deflated);
}

/* tests/network/hardening/q2-inflate-limit.test.ts */
static uint8_t *download_block(const uint8_t *data, size_t length, uint8_t percent, size_t *out) {
  uint8_t *message = malloc(4 + length);
  message[0] = 35;
  message[1] = (uint8_t)(length & 255);
  message[2] = (uint8_t)(length >> 8);
  message[3] = percent;
  memcpy(message + 4, data, length);
  *out = 4 + length;
  return message;
}
typedef struct bomb_scope { qa_q2_server_message_reader *reader; uint8_t **chunks; size_t *lengths; size_t count; } bomb_scope;
static void bomb_body(void *arg) {
  bomb_scope *scope = arg;
  for (size_t i = 0; i < scope->count; i++) {
    qa_q2_server_record *records = NULL;
    size_t count = 0;
    if (qa_q2_server_message_reader_read(scope->reader, scope->chunks[i], scope->lengths[i], &records, &count))
      qa_q2_server_records_free(records, count);
  }
}
static void test_inflate_limit(void) {
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 2048;
  options.inventory_slots = 256;
  qa_q2_server_message_reader reader;
  CHECK(qa_q2_server_message_reader_init(&reader, protocol_of("q2-rerelease", 1038, 0), options));
  size_t plain_length = 128u * 1024u * 1024u;
  uint8_t *zeros = calloc(1, 1 << 20);
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  size_t capacity = 1 << 20, used = 0;
  uint8_t *bomb = malloc(capacity);
  for (size_t fed = 0; fed < plain_length; fed += 1 << 20) {
    stream.next_in = zeros;
    stream.avail_in = 1 << 20;
    int flush = fed + (1 << 20) >= plain_length ? Z_FINISH : Z_NO_FLUSH;
    do {
      if (capacity - used < 65536) { capacity *= 2; bomb = realloc(bomb, capacity); }
      stream.next_out = bomb + used;
      stream.avail_out = (uInt)(capacity - used);
      deflate(&stream, flush);
      used = stream.total_out;
    } while (stream.avail_in != 0 || (flush == Z_FINISH && stream.avail_out == 0));
  }
  deflateEnd(&stream);
  free(zeros);
  size_t count = (used + 29999) / 30000;
  CHECK(count > 2);
  uint8_t **chunks = malloc(count * sizeof *chunks);
  size_t *lengths = malloc(count * sizeof *lengths);
  for (size_t i = 0; i < count; i++) {
    size_t start = i * 30000, n = used - start < 30000 ? used - start : 30000;
    chunks[i] = download_block(bomb + start, n, i == count - 1 ? 100 : 50, &lengths[i]);
  }
  bomb_scope scope = {&reader, chunks, lengths, count};
  qa_caught_error caught;
  bool ok = qa_try(bomb_body, &scope, &caught);
  CHECK(!ok);
  if (!ok) {
    fprintf(stderr, "inflate limit: %s: %s\n", caught.name, caught.message);
    CHECK(strstr(caught.message, "larger") || strstr(caught.message, "limit") ||
          strstr(caught.message, "exceeds") || strstr(caught.message, "Cannot create"));
  }
  for (size_t i = 0; i < count; i++) free(chunks[i]);
  free(chunks);
  free(lengths);
  free(bomb);
  qa_q2_server_message_reader_destroy(&reader);
}

/* --- Client messages (server side: a remote client's move, userinfo, stringcmd) --- */
typedef struct client_ctx { qa_q2_wire_codec *wire; double seats; } client_ctx;
static void client_read(void *ctx, const uint8_t *bytes, size_t length) {
  client_ctx *c = ctx;
  qa_q2_client_record *records = NULL;
  size_t count = 0;
  if (qa_q2_read_client_messages(c->wire, bytes, length, 7, c->seats, &records, &count))
    qa_q2_client_records_free(records, count);
}
static void test_client_messages(void) {
  for (size_t row = 0; row < sizeof q2_rows / sizeof *q2_rows; row++) {
    qa_q2_protocol_identity protocol = protocol_of(q2_rows[row].kind, q2_rows[row].version,
                                                   q2_rows[row].revision);
    qa_q2_wire_codec wire;
    CHECK(qa_q2_wire_codec_init(&wire, protocol));
    bool kex = strcmp(protocol.kind, "q2-kex") == 0;
    qa_q2_wire_usercmd commands[3];
    memset(commands, 0, sizeof commands);
    for (int i = 0; i < 3; i++) {
      commands[i].msec = 16 + i;
      commands[i].forwardmove = 200;
      commands[i].angles[1] = (int16_t)(1000 * i);
      commands[i].buttons = 1;
    }
    size_t move_length = 0;
    uint8_t *move = qa_q2_encode_move(&wire, 7, 3, commands, &move_length);
    qa_q2_client_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2_CLIENT_USERINFO;
    event.text = "\\name\\player\\skin\\male/grunt";
    size_t userinfo_length = 0;
    uint8_t *userinfo = qa_q2_encode_client_control(&event, kex, &userinfo_length);
    event.kind = QA_Q2_CLIENT_COMMAND;
    event.text = "download maps/q2dm1.bsp 0";
    size_t command_length = 0;
    uint8_t *command = qa_q2_encode_client_control(&event, kex, &command_length);
    size_t total = move_length + userinfo_length + command_length;
    uint8_t *joined = malloc(total);
    memcpy(joined, move, move_length);
    memcpy(joined + move_length, userinfo, userinfo_length);
    memcpy(joined + move_length + userinfo_length, command, command_length);
    client_ctx ctx = {&wire, 1};
    qa_q2_client_record *records = NULL;
    size_t count = 0;
    attempt a = {client_read, &ctx, joined, total};
    qa_caught_error caught;
    CHECK(qa_try(attempt_body, &a, &caught));
    (void)records;
    (void)count;
    fuzz(client_read, &ctx, joined, total, 600);
    ctx.seats = 4;
    fuzz(client_read, &ctx, joined, total, 200);
    if (strcmp(protocol.kind, "q2-q2pro") == 0) {
      qa_q2_clc_batch_move_frame frames[2] = {{commands, 2}, {commands + 1, 2}};
      size_t batch_length = 0;
      uint8_t *batch = qa_q2_encode_batch_move(&wire, true, 3, frames, 2, &batch_length);
      ctx.seats = 1;
      fuzz(client_read, &ctx, batch, batch_length, 600);
      free(batch);
    }
    free(joined);
    free(move);
    free(userinfo);
    free(command);
    qa_q2_wire_codec_destroy(&wire);
  }
}

/* --- GTV: client stream from a remote server, request from a remote viewer --- */
typedef struct sent { uint8_t bytes[4096]; size_t length; } sent;
static void capture_send(void *ctx, const uint8_t *bytes, size_t length) {
  sent *s = ctx;
  if (s->length + length <= sizeof s->bytes) {
    memcpy(s->bytes + s->length, bytes, length);
    s->length += length;
  }
}
static void gtv_client_read(void *ctx, const uint8_t *bytes, size_t length) {
  (void)ctx;
  sent outgoing = {{0}, 0};
  qa_q2_gtv_identity identity = {"viewer", "secret", "q2 test"};
  qa_q2_gtv_client client;
  if (!qa_q2_gtv_client_init(&client, capture_send, &outgoing, identity, -1)) return;
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))qa_q2_gtv_client_destroy, &client);
  qa_q2_gtv_client_start(&client);
  qa_q2_gtv_event *events = NULL;
  size_t count = 0;
  if (qa_q2_gtv_client_receive(&client, bytes, length, &events, &count))
    qa_q2_gtv_events_free(events, count);
  qa_cleanup_pop(&node);
  qa_q2_gtv_client_destroy(&client);
}
static void gtv_request_read(void *ctx, const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_q2_gtv_request request;
  (void)qa_q2_read_gtv_request(bytes, length, &request);
}
static void test_gtv(void) {
  size_t magic_length = 0;
  uint8_t *magic = qa_q2_mvd_magic(&magic_length);
  for (int deflate_flag = 0; deflate_flag < 2; deflate_flag++) {
    qa_q2_gtv_server_stream stream;
    qa_q2_gtv_server_stream_init(&stream);
    size_t hello_length = 0;
    uint8_t *hello = qa_q2_gtv_server_stream_hello(&stream, deflate_flag ? 1 : 0, &hello_length);
    size_t total = magic_length + hello_length;
    uint8_t *seed = malloc(total);
    memcpy(seed, magic, magic_length);
    memcpy(seed + magic_length, hello, hello_length);
    fuzz(gtv_client_read, NULL, seed, total, 600);
    free(seed);
    free(hello);
    qa_q2_gtv_server_stream_close(&stream);
  }
  /* The client's own hello as a remote request (frame length prefix stripped). */
  sent outgoing = {{0}, 0};
  qa_q2_gtv_identity identity = {"viewer", "secret", "q2 test"};
  qa_q2_gtv_client client;
  CHECK(qa_q2_gtv_client_init(&client, capture_send, &outgoing, identity, -1));
  qa_q2_gtv_client_start(&client);
  qa_q2_gtv_event *events = NULL;
  size_t count = 0;
  CHECK(qa_q2_gtv_client_receive(&client, magic, magic_length, &events, &count));
  qa_q2_gtv_events_free(events, count);
  CHECK(outgoing.length > magic_length + 2);
  if (outgoing.length > magic_length + 2)
    fuzz(gtv_request_read, NULL, outgoing.bytes + magic_length + 2, outgoing.length - magic_length - 2, 600);
  qa_q2_gtv_client_destroy(&client);
  free(magic);
}

/* --- KEX: channel datagrams and LAN mDNS --- */
static bool drop_emit(void *ctx, const uint8_t *bytes, size_t length) {
  (void)ctx; (void)bytes; (void)length;
  return true;
}
static void kex_channel_read(void *ctx, const uint8_t *bytes, size_t length) {
  qa_kex_channel *channel = ctx;
  qa_kex_message message;
  memset(&message, 0, sizeof message);
  if (qa_kex_channel_receive(channel, bytes, length, 0, &message)) qa_kex_message_free(&message);
}
static void test_kex(void) {
  qa_kex_channel channel;
  CHECK(qa_kex_channel_init(&channel, drop_emit, NULL));
  uint8_t payload[] = {'C', 'R', 'A', 'N', 'T', 'I', 'M', 'E', 0, 'Q', 'u', 'a', 'k', 'e', 'I', 'I', 0, 1};
  for (int kind = 0; kind < 3; kind++) {
    qa_kex_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.kind = kind == 0 ? 128 : kind == 1 ? 0 : 127;
    packet.flags = kind == 1 ? 1 : 0;
    packet.sequence = kind == 1 ? 1 : 0;
    packet.reliable = kind == 1 ? 1 : 0;
    uint8_t compressed[64];
    if (kind == 2) {
      compressed[0] = 3;
      uLongf n = sizeof compressed - 1;
      compress2(compressed + 1, &n, payload, sizeof payload, 6);
      packet.payload = compressed;
      packet.payload_length = n + 1;
    } else {
      packet.payload = payload;
      packet.payload_length = sizeof payload;
    }
    size_t length = 0;
    uint8_t *bytes = qa_kex_write_packet(&packet, &length);
    CHECK(bytes != NULL);
    if (bytes) fuzz(kex_channel_read, &channel, bytes, length, 600);
    free(bytes);
  }
  qa_kex_channel_destroy(&channel);
}
static void mdns_found(void *ctx, const qa_net_address *address) { (*(int *)ctx)++; (void)address; }
static void mdns_read(void *ctx, const uint8_t *bytes, size_t length) {
  qa_kex_mdns *mdns = ctx;
  (void)qa_kex_mdns_message(mdns, bytes, length);
}
static void test_mdns(void) {
  static const uint8_t response[] = {
    0, 0, 0x84, 0, 0, 0, 0, 2, 0, 0, 0, 0,
    /* Quake II._game._udp.local SRV 0 0 27910 host.local */
    8, 'Q', 'u', 'a', 'k', 'e', ' ', 'I', 'I', 5, '_', 'g', 'a', 'm', 'e', 4, '_', 'u', 'd', 'p',
    5, 'l', 'o', 'c', 'a', 'l', 0, 0, 33, 0x80, 1, 0, 0, 0, 120, 0, 18,
    0, 0, 0, 0, 0x6d, 0x06, 4, 'h', 'o', 's', 't', 5, 'l', 'o', 'c', 'a', 'l', 0,
    /* host.local A 10.0.0.9 via a compression pointer to "local" */
    4, 'h', 'o', 's', 't', 0xc0, 32, 0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4, 10, 0, 0, 9};
  int found = 0;
  qa_kex_mdns mdns;
  memset(&mdns, 0, sizeof mdns);
  mdns.socket_fd = -1;
  mdns.found = mdns_found;
  mdns.found_ctx = &found;
  CHECK(qa_kex_mdns_message(&mdns, response, sizeof response));
  CHECK(found == 1);
  uint8_t loop[] = {0, 0, 0x84, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xc0, 12};
  CHECK(!qa_kex_mdns_message(&mdns, loop, sizeof loop));
  fuzz(mdns_read, &mdns, response, sizeof response, 1500);
  for (size_t i = 0; i < mdns.endpoint_count; i++) {
    free(mdns.endpoints[i].name);
    free(mdns.endpoints[i].target);
  }
  free(mdns.endpoints);
  for (size_t i = 0; i < mdns.address_count; i++) {
    free(mdns.addresses[i].name);
    qa_net_address_destroy(&mdns.addresses[i].address);
  }
  free(mdns.addresses);
}

/* --- readQ2DownloadServer, TS bytes --- */
static void test_download_server(void) {
  qa_net_address public_v4, private_v4, loopback, v6;
  int64_t a[4] = {8, 8, 8, 8}, b[4] = {192, 168, 0, 2};
  qa_net_make_ipv4(&public_v4, a, 27910, false);
  qa_net_make_ipv4(&private_v4, b, 27910, false);
  qa_net_make_loopback(&loopback, "x");
  qa_net_make_ip(&v6, "fe80::1", 27910, false);
  for (size_t i = 0; i < sizeof q2_dlserver_rows / sizeof *q2_dlserver_rows; i++) {
    const char *server = q2_dlserver_rows[i].server;
    const qa_net_address *address = strcmp(server, "public") == 0 ? &public_v4
        : strcmp(server, "private") == 0 ? &private_v4
        : strcmp(server, "loopback") == 0 ? &loopback
        : strcmp(server, "v6") == 0 ? &v6 : NULL;
    size_t n = strlen(q2_dlserver_rows[i].input) + 10;
    char *argument = malloc(n);
    snprintf(argument, n, "dlserver=%s", q2_dlserver_rows[i].input);
    char *arguments[] = {"p=34", argument};
    char *href = qa_q2_read_download_server(arguments, 2, address);
    const char *want = q2_dlserver_rows[i].href;
    bool same = (href == NULL && want == NULL) || (href && want && strcmp(href, want) == 0);
    checks++;
    if (!same) {
      failures++;
      fprintf(stderr, "FAIL dlserver %s [%s]: got %s want %s\n", q2_dlserver_rows[i].input, server,
              href ? href : "null", want ? want : "null");
    }
    free(href);
    free(argument);
  }
  qa_net_address_destroy(&public_v4);
  qa_net_address_destroy(&private_v4);
  qa_net_address_destroy(&loopback);
  qa_net_address_destroy(&v6);
}

int main(void) {
  test_server_messages();
  test_zpacket();
  test_inflate_limit();
  test_client_messages();
  test_gtv();
  test_kex();
  test_mdns();
  test_download_server();
  printf("q2 remote input: %d checks, %d failures (%zu decodes raised)\n", checks, failures, raised_total);
  return failures == 0 ? 0 : 1;
}
