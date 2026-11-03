/*
 * Remote-input recovery for the Q3 wire decoders: every TS catch on a decode
 * path (server.ts InvalidClientOpcodeError / InvalidClientCommandCountError,
 * server-message.ts sourceLong exhaustion, connectionless.ts decompression
 * rewrap) and fixed-seed truncate/mutate loops over each decoder family, run
 * under qa_try. A raise is acceptable where TS throws; a crash or (under ASan)
 * a leak is not. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t rng_state = 0x9e3779b9u;
static uint32_t rng(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return rng_state;
}
/* Truncate (even iterations) or flip 1-4 bytes (odd iterations) of a valid input. */
static size_t mutate(const uint8_t *src, size_t len, uint8_t *dst, int iteration) {
  memcpy(dst, src, len);
  if (iteration % 2 == 0) return len ? rng() % len : 0;
  int flips = 1 + (int)(rng() % 4);
  for (int i = 0; i < flips && len; i++) dst[rng() % len] ^= (uint8_t)(1u + rng() % 255u);
  return len;
}

/* ---- Server connection fixture ---- */
static int SENTINEL;
typedef struct server_fixture {
  int *sentinel;
  char prints[4096];
  char dropped[256];
  int drops, resends, commands, enters, thinks;
  double now;
} server_fixture;
static server_fixture *fx_of(void *ctx) {
  server_fixture *f = ctx;
  if (f->sentinel != &SENTINEL) { fprintf(stderr, "bad ctx\n"); abort(); }
  return f;
}
static void fx_assert(void *ctx) { (void)fx_of(ctx); }
static int64_t fx_server_id(void *ctx) { (void)fx_of(ctx); return 1; }
static int64_t fx_checksum(void *ctx) { (void)fx_of(ctx); return 0x1234; }
static bool fx_false(void *ctx) { (void)fx_of(ctx); return false; }
static double fx_time(void *ctx) { return fx_of(ctx)->now; }
static const char *fx_download(void *ctx) { (void)fx_of(ctx); return ""; }
static bool fx_command(void *ctx, const qa_q3_reliable_command *command, bool client_ok) {
  (void)command; (void)client_ok; fx_of(ctx)->commands++; return true;
}
static void fx_enter(void *ctx, const qa_q3_wire_user_command *command) { (void)command; fx_of(ctx)->enters++; }
static void fx_think(void *ctx, const qa_q3_wire_user_command *command) { (void)command; fx_of(ctx)->thinks++; }
static void fx_resend(void *ctx) { fx_of(ctx)->resends++; }
static void fx_drop(void *ctx, const char *reason) {
  server_fixture *f = fx_of(ctx);
  f->drops++;
  snprintf(f->dropped, sizeof f->dropped, "%s", reason);
}
static void fx_print(void *ctx, const char *text) {
  server_fixture *f = fx_of(ctx);
  size_t used = strlen(f->prints);
  snprintf(f->prints + used, sizeof f->prints - used, "%s", text);
}
static void fx_baseline(void *ctx, int64_t number, qa_q3_entity_state_record *out) {
  (void)fx_of(ctx);
  qa_q3_entity_state_record_init(out, 0);
  (void)number;
}
typedef struct server_rig {
  server_fixture fixture;
  qa_q3_snapshot_entities *entities;
  qa_q3_server_snapshot_history *history;
  qa_q3_server_connection *connection;
} server_rig;
static void rig_init(server_rig *rig) {
  memset(rig, 0, sizeof *rig);
  rig->fixture.sentinel = &SENTINEL;
  CHECK(qa_q3_snapshot_entities_init(&rig->entities, 4096));
  CHECK(qa_q3_server_snapshot_history_init(&rig->history, rig->entities, QA_Q3_PRODUCT_BASEQ3, fx_baseline, &rig->fixture));
  qa_q3_server_bindings b;
  memset(&b, 0, sizeof b);
  b.assert_current = fx_assert; b.server_id = fx_server_id; b.restarted_server_id = fx_server_id;
  b.checksum_feed = fx_checksum; b.pure = fx_false; b.debug_build = false; b.time = fx_time;
  b.client_running = fx_false; b.flood_protect = fx_false; b.download_name = fx_download;
  b.command = fx_command; b.enter_world = fx_enter; b.think = fx_think; b.resend_gamestate = fx_resend;
  b.drop = fx_drop; b.print = fx_print; b.print_ctx = &rig->fixture; b.ctx = &rig->fixture;
  qa_q3_connection_identity identity;
  memset(&identity, 0, sizeof identity);
  CHECK(qa_q3_server_connection_init(&rig->connection, identity, 77, 27960, rig->history, &b));
}
static void rig_destroy(server_rig *rig) {
  qa_q3_server_connection_destroy(rig->connection);
  qa_q3_server_snapshot_history_destroy(rig->history);
  qa_q3_snapshot_entities_destroy(rig->entities);
}
/* Plaintext client message: header longs then raw tail bytes written as message bytes. */
static size_t client_plaintext(int64_t server_id, int64_t ack, int64_t reliable_ack, const int *tail, size_t tail_count, uint8_t *out) {
  qa_q3_message_writer *w = NULL;
  CHECK(qa_q3_message_writer_init_default(&w));
  qa_q3_message_writer_write_long(w, server_id);
  qa_q3_message_writer_write_long(w, ack);
  qa_q3_message_writer_write_long(w, reliable_ack);
  for (size_t i = 0; i < tail_count; i++) qa_q3_message_writer_write_byte(w, tail[i]);
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_message_writer_to_bytes(w, &bytes, &len));
  memcpy(out, bytes, len);
  free(bytes);
  qa_q3_message_writer_destroy(w);
  return len;
}
static void reader_cleanup(void *reader) { qa_q3_client_message_reader_destroy(reader); }
typedef struct execute_scope { server_rig *rig; const uint8_t *bytes; size_t len; } execute_scope;
static void execute_body(void *ctx) {
  execute_scope *scope = ctx;
  qa_q3_client_message_reader *reader = NULL;
  if (!qa_q3_client_message_reader_init(&reader, scope->bytes, scope->len, NULL)) return;
  /* The reader is owned here; free it if execution raises. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, reader_cleanup, reader);
  qa_q3_server_connection_execute_message(scope->rig->connection, reader);
  qa_cleanup_pop(&guard);
  qa_q3_client_message_reader_destroy(reader);
}

static void test_server_catches(void) {
  server_rig rig;
  rig_init(&rig);
  uint8_t bytes[256];
  qa_caught_error caught;
  /* server.ts:98 InvalidClientOpcodeError -> warning print, connection kept. */
  int bad_opcode[] = {9, 5};
  size_t len = client_plaintext(1, 0, 0, bad_opcode, 2, bytes);
  execute_scope scope = { &rig, bytes, len };
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(strcmp(rig.fixture.prints, "WARNING: bad command byte for client\n") == 0);
  CHECK(rig.fixture.drops == 0);
  /* server.ts:124 InvalidClientCommandCountError, both texts. */
  rig.fixture.prints[0] = '\0';
  int zero_count[] = {QA_Q3_CLC_MOVE, 0};
  len = client_plaintext(1, 0, 0, zero_count, 2, bytes);
  scope.len = len;
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(strcmp(rig.fixture.prints, "cmdCount < 1\n") == 0);
  rig.fixture.prints[0] = '\0';
  int big_count[] = {QA_Q3_CLC_MOVE_NO_DELTA, 33};
  len = client_plaintext(1, 0, 0, big_count, 2, bytes);
  scope.len = len;
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(strcmp(rig.fixture.prints, "cmdCount > MAX_PACKET_USERCMDS\n") == 0);
  CHECK(rig.fixture.thinks == 0 && rig.fixture.drops == 0);
  /* Other BinaryErrors propagate: a datagram too short for the prefix. */
  scope.len = 3;
  CHECK(!qa_try(execute_body, &scope, &caught));
  CHECK(strcmp(caught.name, "BinaryError") == 0);
  CHECK(strncmp(caught.message, "<client-message>:", 17) == 0);
  /* Wrong-server gamestate resend is limited to once per second. */
  int eof[] = {QA_Q3_CLC_EOF};
  len = client_plaintext(9, 3, 0, eof, 1, bytes);
  scope.len = len;
  rig.fixture.now = 5000;
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(rig.fixture.resends == 1);
  rig.fixture.now = 5999;
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(rig.fixture.resends == 1);
  rig.fixture.now = 6000;
  CHECK(qa_try(execute_body, &scope, &caught));
  CHECK(rig.fixture.resends == 2);
  rig_destroy(&rig);
}

/* ---- Client message fuzz through the decoder and the server connection ---- */
static const char *fixed_command(void *ctx, int64_t sequence) { (void)ctx; (void)sequence; return "cs 1 x"; }
typedef struct decode_client_scope { const uint8_t *bytes; size_t len; } decode_client_scope;
static void decode_client_body(void *ctx) {
  decode_client_scope *scope = ctx;
  qa_q3_client_decode_context context;
  memset(&context, 0, sizeof context);
  context.key.checksum_feed = 0x1234; context.key.server_command = fixed_command;
  context.reliable_sequence = 0; context.last_client_command = 0; context.last_user_command_time = 0;
  qa_q3_decoded_client_message message;
  if (qa_q3_decode_client_message(scope->bytes, scope->len, &context, NULL, &message))
    qa_q3_decoded_client_message_destroy(&message);
}
static void test_client_message_fuzz(void) {
  qa_q3_reliable_command commands[2] = { {1, "say hello"}, {2, "userinfo \"\\name\\x\""} };
  qa_q3_client_movement movement;
  memset(&movement, 0, sizeof movement);
  movement.kind = QA_Q3_MOVE; movement.command_count = 3;
  for (int i = 0; i < 3; i++) {
    movement.commands[i].server_time = 1000 + i * 16;
    movement.commands[i].angles[1] = 300 * i; movement.commands[i].forwardmove = 127;
    movement.commands[i].buttons = i;
  }
  qa_q3_client_message message = { {1, 0, 0}, commands, 2, &movement };
  qa_q3_client_key_context key = { 0x1234, fixed_command, NULL };
  uint8_t *valid = NULL; size_t valid_len = 0;
  CHECK(qa_q3_encode_client_message(&message, &key, NULL, &valid, &valid_len));
  decode_client_scope scope = { valid, valid_len };
  qa_caught_error caught;
  CHECK(qa_try(decode_client_body, &scope, &caught));
  uint8_t buffer[512];
  int raised = 0;
  server_rig rig;
  rig_init(&rig);
  for (int i = 0; i < 4000; i++) {
    size_t len = mutate(valid, valid_len, buffer, i);
    scope.bytes = buffer; scope.len = len;
    if (!qa_try(decode_client_body, &scope, &caught)) raised++;
    rig.fixture.prints[0] = '\0';
    execute_scope exec = { &rig, buffer, len };
    if (!qa_try(execute_body, &exec, &caught)) raised++;
  }
  CHECK(raised > 0);
  rig_destroy(&rig);
  free(valid);
}

/* ---- Server message fuzz (decoder and cursor exhaustion) ---- */
typedef struct decode_server_scope { const uint8_t *bytes; size_t len; } decode_server_scope;
static qa_q3_entity_state_record *no_baseline(void *ctx, int64_t number) { (void)ctx; (void)number; return NULL; }
static const qa_q3_snapshot_history_entry *no_history(void *ctx, int64_t number) { (void)ctx; (void)number; return NULL; }
static void decode_server_body(void *ctx) {
  decode_server_scope *scope = ctx;
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = QA_Q3_PRODUCT_BASEQ3; context.message_number = 5;
  context.baseline = no_baseline; context.history = no_history;
  qa_q3_server_message message;
  if (qa_q3_decode_server_message(scope->bytes, scope->len, &context, NULL, &message))
    qa_q3_server_message_destroy(&message);
}
static void cursor_next_body(void *ctx) {
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = QA_Q3_PRODUCT_BASEQ3; context.message_number = 5;
  context.baseline = no_baseline; context.history = no_history;
  qa_q3_server_message_step step;
  if (qa_q3_server_message_cursor_next(ctx, &context, &step)) qa_q3_server_message_step_destroy(&step);
}
static void test_server_message_fuzz(void) {
  qa_q3_gamestate_entry entries[3];
  memset(entries, 0, sizeof entries);
  entries[0].kind = QA_Q3_GS_CONFIGSTRING; entries[0].index = 0; entries[0].value = "\\sv_hostname\\fuzz\\g_gametype\\0";
  entries[1].kind = QA_Q3_GS_CONFIGSTRING; entries[1].index = 1; entries[1].value = "\\sv_serverid\\7";
  entries[2].kind = QA_Q3_GS_BASELINE; entries[2].number = 12;
  qa_q3_entity_state_record_init(&entries[2].entity, 0);
  entries[2].entity.number = 12;
  qa_q3_server_operation ops[3];
  memset(ops, 0, sizeof ops);
  ops[0].kind = QA_Q3_OP_COMMAND; ops[0].sequence = 1; ops[0].text = "print \"hi\"";
  ops[1].kind = QA_Q3_OP_GAMESTATE; ops[1].gamestate.command_sequence = 1;
  ops[1].gamestate.entries = entries; ops[1].gamestate.entry_count = 3;
  ops[1].gamestate.client_number = 0; ops[1].gamestate.checksum_feed = 99;
  ops[2].kind = QA_Q3_OP_NOP;
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = QA_Q3_PRODUCT_BASEQ3; context.message_number = 5;
  context.baseline = no_baseline; context.history = no_history;
  uint8_t *valid = NULL; size_t valid_len = 0;
  CHECK(qa_q3_encode_server_message(0, ops, 3, &context, NULL, &valid, &valid_len));
  decode_server_scope scope = { valid, valid_len };
  qa_caught_error caught;
  CHECK(qa_try(decode_server_body, &scope, &caught));
  uint8_t buffer[1024];
  int raised = 0;
  for (int i = 0; i < 4000; i++) {
    size_t len = mutate(valid, valid_len, buffer, i);
    scope.bytes = buffer; scope.len = len;
    if (!qa_try(decode_server_body, &scope, &caught)) raised++;
  }
  CHECK(raised > 0);
  /* sourceLong: a header long cut short is MSG_ReadLong's -1, not a raise
   * (server-message.ts:276, including mid-symbol truncation). */
  qa_q3_message_writer *rare = NULL;
  CHECK(qa_q3_message_writer_init_default(&rare));
  qa_q3_message_writer_write_long(rare, (int64_t)(int32_t)0x9d9d9d9du); /* four long Huffman codes */
  uint8_t *long_bytes = NULL; size_t long_len = 0;
  CHECK(qa_q3_message_writer_to_bytes(rare, &long_bytes, &long_len));
  qa_q3_message_writer_destroy(rare);
  CHECK(long_len >= 5);
  for (size_t cut = 0; cut < long_len - 1; cut++) {
    qa_q3_server_message_cursor *cursor = NULL;
    CHECK(qa_q3_server_message_cursor_init(&cursor, long_bytes, cut, NULL, NULL, 0, NULL, NULL, NULL));
    qa_q3_server_message_step step;
    CHECK(qa_q3_server_message_cursor_next(cursor, &context, &step));
    CHECK(step.kind == QA_Q3_STEP_ACKNOWLEDGE && step.sequence == -1);
    qa_q3_server_message_step_destroy(&step);
    /* The cursor is exhausted: the next opcode read fails as CL_ParseServerMessage does. */
    qa_caught_error exhausted;
    CHECK(!qa_try(cursor_next_body, cursor, &exhausted));
    CHECK(strcmp(exhausted.name, "BinaryError") == 0
          && strstr(exhausted.message, "CL_ParseServerMessage: read past end of server message") != NULL);
    qa_q3_server_message_cursor_destroy(cursor);
  }
  free(long_bytes);
  free(valid);
}

/* ---- Connectionless and Huffman ---- */
typedef struct connectionless_scope { const uint8_t *bytes; size_t len; qa_q3_connectionless_receiver receiver; } connectionless_scope;
static void connectionless_body(void *ctx) {
  connectionless_scope *scope = ctx;
  qa_q3_connectionless_packet packet;
  if (qa_q3_decode_connectionless(scope->bytes, scope->len, scope->receiver, NULL, &packet))
    qa_q3_connectionless_packet_destroy(&packet);
}
typedef struct huffman_scope { const uint8_t *bytes; size_t len; } huffman_scope;
static void huffman_body(void *ctx) {
  huffman_scope *scope = ctx;
  uint8_t *out = NULL; size_t out_len = 0;
  if (qa_q3_decompress_adaptive_default(scope->bytes, scope->len, &out, &out_len)) free(out);
}
static void test_connectionless_fuzz(void) {
  uint8_t *valid = NULL; size_t valid_len = 0;
  CHECK(qa_q3_encode_connect("\\challenge\\5\\qport\\27960\\protocol\\68\\name\\fuzz", &valid, &valid_len));
  connectionless_scope scope = { valid, valid_len, QA_Q3_CONNLESS_SERVER };
  qa_caught_error caught;
  CHECK(qa_try(connectionless_body, &scope, &caught));
  /* connectionless.ts:103: decompression errors become BinaryError at offset 12. */
  scope.len = 13;
  CHECK(!qa_try(connectionless_body, &scope, &caught));
  CHECK(strcmp(caught.name, "BinaryError") == 0 && caught.detail == 12);
  CHECK(strcmp(caught.message, "<connectionless>:12: Malformed compressed connectionless payload: Truncated Huffman length") == 0);
  scope.len = 15;
  CHECK(!qa_try(connectionless_body, &scope, &caught));
  CHECK(strcmp(caught.message, "<connectionless>:12: Malformed compressed connectionless payload: Truncated Huffman symbol") == 0);
  uint8_t buffer[512];
  int raised = 0;
  for (int i = 0; i < 4000; i++) {
    size_t len = mutate(valid, valid_len, buffer, i);
    scope.bytes = buffer; scope.len = len;
    scope.receiver = (i & 2) ? QA_Q3_CONNLESS_CLIENT : QA_Q3_CONNLESS_SERVER;
    if (!qa_try(connectionless_body, &scope, &caught)) raised++;
    huffman_scope huff = { buffer + (len > 12 ? 12 : len), len > 12 ? len - 12 : 0 };
    if (!qa_try(huffman_body, &huff, &caught)) {
      raised++;
      CHECK(strcmp(caught.name, "RangeError") == 0 || strcmp(caught.name, "Error") == 0);
    }
  }
  CHECK(raised > 0);
  free(valid);
}

/* ---- Netchan receive: hostile datagrams are rejected by result, never raised ---- */
static void test_netchan_fuzz(void) {
  qa_q3_netchannel *server = NULL, *client = NULL;
  CHECK(qa_q3_netchannel_init(&server, QA_Q3_CHANNEL_SERVER, 27960, NULL, NULL));
  CHECK(qa_q3_netchannel_init(&client, QA_Q3_CHANNEL_CLIENT, 27960, NULL, NULL));
  uint8_t buffer[1600];
  int accepted = 0;
  for (int i = 0; i < 4000; i++) {
    size_t len = rng() % sizeof buffer;
    for (size_t j = 0; j < len; j++) buffer[j] = (uint8_t)rng();
    if (i % 3 == 0 && len >= 10) { buffer[3] |= 0x80; buffer[1] = 0; buffer[2] = 0; }
    qa_q3_channel_result result;
    qa_q3_netchannel *channel = (i & 1) ? server : client;
    CHECK(qa_q3_netchannel_receive(channel, buffer, len, NULL, &result));
    if (result.kind == QA_Q3_CHANNEL_ACCEPTED) accepted++;
    free(result.payload);
  }
  (void)accepted;
  qa_q3_netchannel_destroy(server);
  qa_q3_netchannel_destroy(client);
}

int main(void) {
  test_server_catches();
  test_client_message_fuzz();
  test_server_message_fuzz();
  test_connectionless_fuzz();
  test_netchan_fuzz();
  printf("test_q3_remote_abort: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
