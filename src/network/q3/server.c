/* Port of src/network/q3/server.ts. GPL-2.0-or-later. */
#include "qa/network/q3/server.h"
#include "qa/foundation/event_ring.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
int64_t qa_q3_rate_milliseconds(double message_size, double rate, double max_rate) {
  if (message_size > 1500) message_size = 1500;
  if (max_rate != 0) {
    double cap = max_rate > 1000 ? max_rate : 1000;
    rate = rate < cap ? rate : cap;
  }
  if (rate == 0) qa_range_error("Source snapshot rate division by zero");
  /* Math.trunc(Math.imul((messageSize+48)|0, 1000) / rate) | 0 */
  int32_t imul = (int32_t)((uint32_t)qa_int32(message_size + 48) * 1000u);
  return qa_int32(trunc(imul / rate));
}
typedef struct { uint8_t *data; size_t len; } queued_t;
struct qa_q3_server_connection {
  qa_q3_connection_identity identity;
  int64_t challenge;
  qa_q3_server_snapshot_history *snapshots;
  qa_q3_server_bindings bindings;
  qa_q3_netchannel *channel;
  qa_q3_server_reliable_commands *reliable;
  qa_q3_source_message_state source_state;
  qa_event_ring queued_messages;
  qa_q3_server_connection_phase phase;
  int64_t last_client_command;
  char *last_client_command_string;
  int64_t message_acknowledge, delta_message, gamestate_message_number;
  qa_q3_wire_user_command last_user_command;
  bool pure_authentic, got_pure_command;
  int32_t next_snapshot_time;
  bool rate_delayed;
  int64_t reliable_sent;
  int32_t next_reliable_time;
  bool has_last_resend; int32_t last_resend;
};
static void server_print(void *ctx, const char *text) {
  qa_q3_server_connection *s = ctx;
  if (s->bindings.print) s->bindings.print(s->bindings.print_ctx, text);
}
bool qa_q3_server_connection_init(qa_q3_server_connection **out, qa_q3_connection_identity identity, int64_t challenge,
                                  int64_t qport, qa_q3_server_snapshot_history *snapshots, const qa_q3_server_bindings *bindings) {
  qa_q3_server_connection *s = calloc(1, sizeof *s);
  if (!s) return false;
  s->identity = identity; s->challenge = challenge; s->snapshots = snapshots; s->bindings = *bindings;
  s->last_client_command_string = xstrdup("");
  s->delta_message = -1; s->gamestate_message_number = -1;
  s->phase = QA_Q3_SERVER_PHASE_CONNECTED;
  bool ok = s->last_client_command_string
    && qa_q3_netchannel_init(&s->channel, QA_Q3_CHANNEL_SERVER, qport, NULL, NULL)
    && qa_q3_server_reliable_commands_init(&s->reliable)
    && qa_event_ring_init(&s->queued_messages, 4096, sizeof(queued_t));
  if (!ok) { qa_q3_server_connection_destroy(s); return false; }
  qa_q3_source_message_state_init(&s->source_state, server_print, s);
  *out = s; return true;
}
void qa_q3_server_connection_destroy(qa_q3_server_connection *s) {
  if (!s) return;
  queued_t q;
  while (qa_event_ring_shift(&s->queued_messages, &q)) free(q.data);
  qa_event_ring_destroy(&s->queued_messages);
  qa_q3_netchannel_destroy(s->channel);
  qa_q3_reliable_ring_destroy(s->reliable);
  free(s->last_client_command_string);
  free(s);
}
qa_q3_netchannel *qa_q3_server_connection_channel(const qa_q3_server_connection *s) { return s->channel; }
qa_q3_server_reliable_commands *qa_q3_server_connection_reliable(const qa_q3_server_connection *s) { return s->reliable; }
qa_q3_source_message_state *qa_q3_server_connection_source_state(const qa_q3_server_connection *s) { return (qa_q3_source_message_state *)&s->source_state; }
qa_q3_connection_identity qa_q3_server_connection_identity(const qa_q3_server_connection *s) { return s->identity; }
int64_t qa_q3_server_connection_challenge(const qa_q3_server_connection *s) { return s->challenge; }
qa_q3_server_snapshot_history *qa_q3_server_connection_snapshots(const qa_q3_server_connection *s) { return s->snapshots; }
const qa_q3_server_bindings *qa_q3_server_connection_get_bindings(const qa_q3_server_connection *s) { return &s->bindings; }
qa_q3_server_connection_phase qa_q3_server_connection_get_phase(const qa_q3_server_connection *s) { return s->phase; }
void qa_q3_server_connection_set_phase(qa_q3_server_connection *s, qa_q3_server_connection_phase p) { s->phase = p; }
int64_t qa_q3_server_connection_last_client_command(const qa_q3_server_connection *s) { return s->last_client_command; }
void qa_q3_server_connection_set_last_client_command(qa_q3_server_connection *s, int64_t v) { s->last_client_command = v; }
const char *qa_q3_server_connection_last_client_command_string(const qa_q3_server_connection *s) { return s->last_client_command_string; }
void qa_q3_server_connection_set_last_client_command_string(qa_q3_server_connection *s, const char *v) { free(s->last_client_command_string); s->last_client_command_string = xstrdup(v); }
int64_t qa_q3_server_connection_message_acknowledge(const qa_q3_server_connection *s) { return s->message_acknowledge; }
void qa_q3_server_connection_set_message_acknowledge(qa_q3_server_connection *s, int64_t v) { s->message_acknowledge = v; }
int64_t qa_q3_server_connection_delta_message(const qa_q3_server_connection *s) { return s->delta_message; }
void qa_q3_server_connection_set_delta_message(qa_q3_server_connection *s, int64_t v) { s->delta_message = v; }
int64_t qa_q3_server_connection_gamestate_message_number(const qa_q3_server_connection *s) { return s->gamestate_message_number; }
void qa_q3_server_connection_set_gamestate_message_number(qa_q3_server_connection *s, int64_t v) { s->gamestate_message_number = v; }
qa_q3_wire_user_command qa_q3_server_connection_last_user_command(const qa_q3_server_connection *s) { return s->last_user_command; }
void qa_q3_server_connection_set_last_user_command(qa_q3_server_connection *s, const qa_q3_wire_user_command *v) { s->last_user_command = *v; }
bool qa_q3_server_connection_pure_authentic(const qa_q3_server_connection *s) { return s->pure_authentic; }
void qa_q3_server_connection_set_pure_authentic(qa_q3_server_connection *s, bool v) { s->pure_authentic = v; }
bool qa_q3_server_connection_got_pure_command(const qa_q3_server_connection *s) { return s->got_pure_command; }
void qa_q3_server_connection_set_got_pure_command(qa_q3_server_connection *s, bool v) { s->got_pure_command = v; }
int64_t qa_q3_server_connection_next_snapshot_time(const qa_q3_server_connection *s) { return s->next_snapshot_time; }
void qa_q3_server_connection_set_next_snapshot_time(qa_q3_server_connection *s, int64_t v) { s->next_snapshot_time = (int32_t)v; }
bool qa_q3_server_connection_rate_delayed(const qa_q3_server_connection *s) { return s->rate_delayed; }
void qa_q3_server_connection_set_rate_delayed(qa_q3_server_connection *s, bool v) { s->rate_delayed = v; }
int64_t qa_q3_server_connection_reliable_sent(const qa_q3_server_connection *s) { return s->reliable_sent; }
void qa_q3_server_connection_set_reliable_sent(qa_q3_server_connection *s, int64_t v) { s->reliable_sent = v; }
int64_t qa_q3_server_connection_next_reliable_time(const qa_q3_server_connection *s) { return s->next_reliable_time; }
void qa_q3_server_connection_set_next_reliable_time(qa_q3_server_connection *s, int64_t v) { s->next_reliable_time = (int32_t)v; }
static const char *lookup_masked_cb(void *ctx, int64_t sequence) {
  return qa_q3_reliable_ring_lookup_masked(((qa_q3_server_connection *)ctx)->reliable, sequence);
}
static bool connection_current(qa_q3_server_connection *s, int64_t server_id) {
  if (s->phase == QA_Q3_SERVER_PHASE_ZOMBIE) return false;
  s->bindings.assert_current(s->bindings.ctx);
  if (server_id != s->bindings.server_id(s->bindings.ctx))
    qa_raise("Error", 0, 0, "Q3 client callback belongs to a retired server world");
  return true;
}
typedef struct read_movement_scope {
  qa_q3_client_message_reader *reader; const qa_q3_client_key_context *key; qa_q3_unfiltered_client_movement *movement; bool ok;
} read_movement_scope;
static void read_movement_body(void *ctx) {
  read_movement_scope *scope = ctx;
  scope->ok = qa_q3_client_message_reader_read_movement(scope->reader, scope->key, scope->movement);
}
typedef struct next_part_scope { qa_q3_client_message_reader *reader; qa_q3_client_message_part *part; bool ok; } next_part_scope;
static void next_part_body(void *ctx) {
  next_part_scope *scope = ctx;
  scope->ok = qa_q3_client_message_reader_next(scope->reader, scope->part);
}
static void user_move(qa_q3_server_connection *s, qa_q3_client_message_reader *reader) {
  qa_q3_server_bindings *host = &s->bindings;
  int64_t server_id = host->server_id(host->ctx);
  qa_q3_client_key_context key_ctx = { host->checksum_feed(host->ctx), lookup_masked_cb, s };
  qa_q3_unfiltered_client_movement movement;
  read_movement_scope scope = { reader, &key_ctx, &movement, false };
  qa_caught_error caught;
  if (!qa_try(read_movement_body, &scope, &caught)) {
    if (strcmp(caught.name, QA_Q3_INVALID_CLIENT_COMMAND_COUNT_ERROR) != 0) qa_rethrow(&caught);
    if (s->bindings.print) s->bindings.print(s->bindings.print_ctx, caught.code < 1 ? "cmdCount < 1\n" : "cmdCount > MAX_PACKET_USERCMDS\n");
    return;
  }
  if (!scope.ok) return;
  qa_q3_server_frame *frame = qa_q3_server_snapshot_history_frame(s->snapshots, s->message_acknowledge);
  frame->message_acked = (int64_t)host->time(host->ctx);
  if (host->pure(host->ctx) && !s->pure_authentic && !s->got_pure_command) {
    if (s->phase == QA_Q3_SERVER_PHASE_ACTIVE) host->resend_gamestate(host->ctx);
    return;
  }
  if (s->phase == QA_Q3_SERVER_PHASE_PRIMED) {
    s->last_user_command = movement.base.commands[0];
    s->phase = QA_Q3_SERVER_PHASE_ACTIVE;
    host->enter_world(host->ctx, &s->last_user_command);
    if (!connection_current(s, server_id)) return;
  }
  if (host->pure(host->ctx) && !s->pure_authentic) { host->drop(host->ctx, "Cannot validate pure client!"); return; }
  if (s->phase != QA_Q3_SERVER_PHASE_ACTIVE) { s->delta_message = -1; return; }
  if (movement.base.command_count == 0) qa_range_error("Missing decoded user command");
  int64_t latest = movement.base.commands[movement.base.command_count - 1].server_time;
  for (size_t i = 0; i < movement.base.command_count; i++) {
    const qa_q3_wire_user_command *command = &movement.base.commands[i];
    if (command->server_time > latest || command->server_time <= s->last_user_command.server_time) continue;
    s->last_user_command = *command;
    host->think(host->ctx, command);
    if (!connection_current(s, server_id)) return;
  }
}
void qa_q3_server_connection_execute_message(qa_q3_server_connection *s, qa_q3_client_message_reader *reader) {
  qa_q3_server_bindings *host = &s->bindings;
  int64_t server_id = host->server_id(host->ctx);
  host->assert_current(host->ctx);
  int64_t prefix_id, prefix_ack;
  qa_q3_client_message_reader_prefix(reader, &prefix_id, &prefix_ack);
  (void)prefix_id;
  s->message_acknowledge = prefix_ack;
  if (s->message_acknowledge < 0) {
    if (host->debug_build) host->drop(host->ctx, "DEBUG: illegible client message");
    return;
  }
  qa_q3_client_header header = qa_q3_client_message_reader_read_header(reader);
  qa_q3_reliable_ring_assign_acknowledgement(s->reliable, header.reliable_acknowledge);
  if (qa_q3_reliable_ring_acknowledge(s->reliable) < qa_q3_reliable_ring_sequence(s->reliable) - 64) {
    if (host->debug_build) host->drop(host->ctx, "DEBUG: illegible client message");
    qa_q3_reliable_ring_assign_acknowledgement(s->reliable, qa_q3_reliable_ring_sequence(s->reliable));
    return;
  }
  if (header.server_id != host->server_id(host->ctx)
      && strlen(host->download_name(host->ctx)) == 0
      && !strstr(s->last_client_command_string, "nextdl")) {
    if (header.server_id >= host->restarted_server_id(host->ctx) && header.server_id < host->server_id(host->ctx)) return;
    /* The client picks both fields, so rebuilding the gamestate is limited to once per second. */
    int32_t now = (int32_t)host->time(host->ctx);
    if (s->message_acknowledge > s->gamestate_message_number
        && (!s->has_last_resend || (int32_t)((uint32_t)now - (uint32_t)s->last_resend) >= 1000)) {
      s->has_last_resend = true; s->last_resend = now;
      host->resend_gamestate(host->ctx);
    }
    return;
  }
  for (;;) {
    host->assert_current(host->ctx);
    qa_q3_client_message_part part;
    next_part_scope scope = { reader, &part, false };
    qa_caught_error caught;
    if (!qa_try(next_part_body, &scope, &caught)) {
      if (strcmp(caught.name, QA_Q3_INVALID_CLIENT_OPCODE_ERROR) != 0) qa_rethrow(&caught);
      if (host->print) host->print(host->print_ctx, "WARNING: bad command byte for client\n");
      return;
    }
    if (!scope.ok) return;
    if (part.kind == QA_Q3_PART_EOF) return;
    if (part.kind == QA_Q3_PART_COMMAND) {
      if (s->last_client_command >= part.command.sequence) continue;
      if (part.command.sequence > (int32_t)(s->last_client_command + 1)) { host->drop(host->ctx, "Lost reliable commands"); return; }
      bool client_ok = host->client_running(host->ctx) || s->phase != QA_Q3_SERVER_PHASE_ACTIVE
        || !host->flood_protect(host->ctx) || host->time(host->ctx) >= s->next_reliable_time;
      s->next_reliable_time = qa_int32(host->time(host->ctx) + 1000);
      if (!host->command(host->ctx, &part.command, client_ok) || !connection_current(s, server_id)) return;
      s->last_client_command = part.command.sequence;
      free(s->last_client_command_string);
      s->last_client_command_string = xstrdup(part.command.text); /* ring text already capped at 1023 */
      if (s->phase == QA_Q3_SERVER_PHASE_ZOMBIE) return;
    } else {
      s->delta_message = part.delta_message;
      user_move(s, reader);
      return;
    }
  }
}
static void free_slot(void *slot) { free(*(void **)slot); *(void **)slot = NULL; }
static void reader_slot_cleanup(void *slot) { qa_q3_client_message_reader_destroy(*(qa_q3_client_message_reader **)slot); }
/* Decode raises (BinaryError from a truncated or malformed client message, game
 * callback errors) propagate to the caller as in TS; the cleanup nodes free the
 * payload, plaintext and reader when a caller's qa_try catches them. */
bool qa_q3_server_connection_receive_datagram(qa_q3_server_connection *s, const uint8_t *packet, size_t len, qa_q3_channel_result *out) {
  s->bindings.assert_current(s->bindings.ctx);
  if (!qa_q3_netchannel_receive(s->channel, packet, len, NULL, out)) return false;
  if (out->kind != QA_Q3_CHANNEL_ACCEPTED || s->phase == QA_Q3_SERVER_PHASE_ZOMBIE) return true;
  qa_cleanup payload_guard, plain_guard, reader_guard;
  qa_cleanup_push(&payload_guard, free_slot, &out->payload);
  uint8_t *plain = NULL;
  qa_q3_client_message_reader *reader = NULL;
  qa_cleanup_push(&plain_guard, free_slot, &plain);
  qa_cleanup_push(&reader_guard, reader_slot_cleanup, &reader);
  bool ok = qa_q3_xor_client_message(out->payload, out->payload_len, s->challenge, lookup_masked_cb, s, &plain)
    && qa_q3_client_message_reader_init(&reader, plain, out->payload_len, NULL);
  if (ok) qa_q3_server_connection_execute_message(s, reader);
  qa_cleanup_pop(&reader_guard);
  qa_cleanup_pop(&plain_guard);
  qa_cleanup_pop(&payload_guard);
  qa_q3_client_message_reader_destroy(reader);
  free(plain);
  return ok;
}
static void transmit(qa_q3_server_connection *s, qa_q3_message_writer *writer, const qa_q3_server_rate *rate, const qa_q3_channel_delivery *delivery) {
  double now_d = s->bindings.time(s->bindings.ctx);
  int64_t now = (int64_t)now_d;
  qa_q3_server_frame *frame = qa_q3_server_snapshot_history_frame(s->snapshots, qa_q3_netchannel_outgoing_sequence(s->channel));
  frame->message_size = qa_q3_message_writer_byte_length(writer);
  frame->message_sent = now;
  frame->message_acked = -1;
  qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_EOF);
  uint8_t *plaintext = NULL; size_t plain_len = 0;
  if (!qa_q3_message_writer_to_bytes(writer, &plaintext, &plain_len)) qa_range_error("server transmit allocation failed");
  if (qa_q3_netchannel_has_unsent_fragments(s->channel)) {
    queued_t q = { plaintext, plain_len };
    qa_event_ring_admit(&s->queued_messages, &q);
    qa_q3_netchannel_transmit_next_fragment(s->channel, delivery);
  } else {
    uint8_t *encoded = NULL;
    if (!qa_q3_xor_server_message(plaintext, plain_len, s->challenge, qa_q3_netchannel_outgoing_sequence(s->channel), s->last_client_command_string, &encoded))
      qa_range_error("server transmit allocation failed");
    free(plaintext);
    qa_q3_netchannel_begin_transmit(s->channel, encoded, plain_len, delivery);
    free(encoded);
  }
  if (rate->local || (rate->force_lan && rate->lan)) { s->next_snapshot_time = qa_int32(now_d - 1); return; }
  double interval = (double)qa_q3_rate_milliseconds((double)qa_q3_message_writer_byte_length(writer), rate->rate, rate->max_rate);
  if (interval < rate->snapshot_msec) { interval = rate->snapshot_msec; s->rate_delayed = false; }
  else s->rate_delayed = true;
  s->next_snapshot_time = qa_int32(now_d + interval);
  if (s->phase != QA_Q3_SERVER_PHASE_ACTIVE && strlen(s->bindings.download_name(s->bindings.ctx)) == 0
      && s->next_snapshot_time < qa_int32(now_d + 1000))
    s->next_snapshot_time = qa_int32(now_d + 1000);
}
typedef struct { qa_q3_server_connection *s; qa_q3_entity_state_record scratch; } baseline_adapter_t;
static qa_q3_entity_state_record *baseline_adapter(void *ctx, int64_t number) {
  baseline_adapter_t *a = ctx;
  qa_q3_server_snapshot_history_baseline(a->s->snapshots, number, &a->scratch);
  return &a->scratch;
}
void qa_q3_server_connection_send_gamestate(qa_q3_server_connection *s, const qa_q3_gamestate *state,
                                            const qa_q3_server_rate *rate, const qa_q3_channel_delivery *delivery) {
  s->phase = QA_Q3_SERVER_PHASE_PRIMED;
  s->pure_authentic = false; s->got_pure_command = false;
  s->gamestate_message_number = qa_q3_netchannel_outgoing_sequence(s->channel);
  qa_q3_message_writer *writer = NULL;
  if (!qa_q3_message_writer_init(&writer, QA_Q3_MESSAGE_BITSTREAM, 16384, &s->source_state)) qa_range_error("gamestate writer allocation failed");
  int64_t outstanding = qa_q3_reliable_ring_outstanding(s->reliable);
  size_t pending_cap = outstanding > 0 ? (size_t)outstanding : 1;
  qa_q3_reliable_command *pending = calloc(pending_cap, sizeof *pending);
  if (!pending) qa_range_error("gamestate allocation failed");
  size_t pending_count = qa_q3_reliable_ring_pending(s->reliable, pending, pending_cap);
  qa_q3_server_operation *ops = calloc(pending_count + 1, sizeof *ops);
  if (!ops) qa_range_error("gamestate allocation failed");
  for (size_t i = 0; i < pending_count; i++) {
    ops[i].kind = QA_Q3_OP_COMMAND; ops[i].sequence = pending[i].sequence; ops[i].text = pending[i].text;
  }
  ops[pending_count].kind = QA_Q3_OP_GAMESTATE;
  ops[pending_count].gamestate = *state;
  ops[pending_count].gamestate.command_sequence = qa_q3_reliable_ring_sequence(s->reliable);
  ops[pending_count].gamestate.checksum_feed = s->bindings.checksum_feed(s->bindings.ctx);
  baseline_adapter_t adapter;
  memset(&adapter, 0, sizeof adapter);
  adapter.s = s;
  qa_q3_server_message_context ctx;
  ctx.product = qa_q3_server_snapshot_history_get_product(s->snapshots);
  ctx.message_number = qa_q3_netchannel_outgoing_sequence(s->channel);
  ctx.reliable_sequence = s->last_client_command;
  ctx.server_command_sequence = qa_q3_reliable_ring_sequence(s->reliable);
  ctx.parse_entities_number = 0;
  ctx.baseline = baseline_adapter; ctx.baseline_ctx = &adapter;
  ctx.history = NULL; ctx.history_ctx = NULL;
  qa_q3_write_server_message(writer, s->last_client_command, ops, pending_count + 1, &ctx);
  free(ops); free(pending); /* gamestate/text borrowed; not destroyed */
  s->reliable_sent = qa_q3_reliable_ring_sequence(s->reliable);
  transmit(s, writer, rate, delivery);
  qa_q3_message_writer_destroy(writer);
}
void qa_q3_server_connection_send_snapshot(qa_q3_server_connection *s, int64_t server_flags, const qa_q3_server_rate *rate,
                                           const qa_q3_channel_delivery *delivery, qa_q3_append_download_fn append_download, void *append_ctx) {
  if (qa_q3_netchannel_has_unsent_fragments(s->channel)) {
    double left = s->bindings.time(s->bindings.ctx);
    double right = (double)qa_q3_rate_milliseconds((double)qa_q3_netchannel_remaining_unsent_bytes(s->channel), rate->rate, rate->max_rate);
    s->next_snapshot_time = qa_int32(left
      + right);
    qa_q3_server_connection_transmit_next_fragment(s, delivery);
    return;
  }
  qa_q3_message_writer *writer = NULL;
  if (!qa_q3_message_writer_init(&writer, QA_Q3_MESSAGE_BITSTREAM, 16384, &s->source_state)) qa_range_error("snapshot writer allocation failed");
  qa_q3_message_writer_write_long(writer, s->last_client_command);
  int64_t outstanding = qa_q3_reliable_ring_outstanding(s->reliable);
  size_t pending_cap = outstanding > 0 ? (size_t)outstanding : 1;
  qa_q3_reliable_command *pending = calloc(pending_cap, sizeof *pending);
  if (!pending) qa_range_error("snapshot allocation failed");
  size_t pending_count = qa_q3_reliable_ring_pending(s->reliable, pending, pending_cap);
  for (size_t i = 0; i < pending_count; i++) {
    qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_COMMAND);
    qa_q3_message_writer_write_long(writer, pending[i].sequence);
    qa_q3_message_writer_write_string(writer, pending[i].text);
  }
  free(pending);
  s->reliable_sent = qa_q3_reliable_ring_sequence(s->reliable);
  qa_q3_server_snapshot_history * h = s->snapshots;
  qa_q3_message_writer * writer2 = writer;
  int64_t sequence2 = qa_q3_netchannel_outgoing_sequence(s->channel);
  int64_t requested = s->delta_message;
  bool active = s->phase == QA_Q3_SERVER_PHASE_ACTIVE;
  int64_t server_time = (int64_t)s->bindings.time(s->bindings.ctx);
  qa_q3_server_snapshot_history_write(h, writer2, sequence2,
    requested, active, server_time,
    server_flags | (s->rate_delayed ? 1 : 0) | (s->phase == QA_Q3_SERVER_PHASE_ACTIVE ? 0 : 2));
  if (append_download) append_download(append_ctx, writer);
  if (qa_q3_message_writer_overflowed(writer)) {
    if (s->bindings.print) s->bindings.print(s->bindings.print_ctx, "WARNING: msg overflowed\n");
    qa_q3_message_writer_clear(writer);
  }
  transmit(s, writer, rate, delivery);
  qa_q3_message_writer_destroy(writer);
}
void qa_q3_server_connection_transmit_next_fragment(qa_q3_server_connection *s, const qa_q3_channel_delivery *delivery) {
  if (!qa_q3_netchannel_has_unsent_fragments(s->channel)) qa_range_error("No pending server fragments");
  qa_q3_netchannel_transmit_next_fragment(s->channel, delivery);
  if (!qa_q3_netchannel_has_unsent_fragments(s->channel)) {
    queued_t next;
    if (qa_event_ring_shift(&s->queued_messages, &next)) {
      uint8_t *encoded = NULL;
      if (!qa_q3_xor_server_message(next.data, next.len, s->challenge, qa_q3_netchannel_outgoing_sequence(s->channel), s->last_client_command_string, &encoded))
        qa_range_error("server transmit allocation failed");
      qa_q3_netchannel_begin_transmit(s->channel, encoded, next.len, delivery);
      free(encoded);
      free(next.data);
    }
  }
}
qa_q3_pure_result qa_q3_server_connection_verify_pure(qa_q3_server_connection *s, const qa_q3_pure_server *server,
                                                      const char *const *argv, size_t argc,
                                                      qa_q3_send_rejected_snapshot_fn send_rejected_snapshot, void *send_ctx) {
  qa_q3_pure_result result = qa_q3_verify_pure_command(server, argv, argc);
  if (result.kind == QA_Q3_PURE_IGNORED) return result;
  s->got_pure_command = true;
  s->pure_authentic = result.kind == QA_Q3_PURE_AUTHENTIC;
  if (result.kind == QA_Q3_PURE_REJECTED) {
    s->next_snapshot_time = -1;
    s->phase = QA_Q3_SERVER_PHASE_ACTIVE;
    send_rejected_snapshot(send_ctx);
    s->bindings.drop(s->bindings.ctx, result.reason);
  }
  return result;
}
