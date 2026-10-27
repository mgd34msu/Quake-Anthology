/* Port of src/network/q3/client.ts. GPL-2.0-or-later. */
#include "qa/network/q3/client.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/client_message.h"
#include "qa/network/q3/client_server_command.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct qa_q3_wire_command_history { qa_q3_wire_user_command commands[64]; int32_t number; };
bool qa_q3_wire_command_history_init(qa_q3_wire_command_history **out) { *out = calloc(1, sizeof **out); return *out != NULL; }
void qa_q3_wire_command_history_destroy(qa_q3_wire_command_history *h) { free(h); }
int64_t qa_q3_wire_command_history_current_number(const qa_q3_wire_command_history *h) { return h->number; }
int64_t qa_q3_wire_command_history_append(qa_q3_wire_command_history *h, const qa_q3_wire_user_command *command) {
  h->number = (int32_t)((uint32_t)h->number + 1u);
  h->commands[h->number & 63] = *command;
  return h->number;
}
bool qa_q3_wire_command_history_read(const qa_q3_wire_command_history *h, int64_t number, qa_q3_wire_user_command *out) {
  if (number > h->number) qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetUserCmd: requested future command");
  if (number <= (int32_t)(h->number - 64)) return false;
  *out = h->commands[((int32_t)number) & 63];
  return true;
}
void qa_q3_wire_command_history_restart(qa_q3_wire_command_history *h) { memset(h->commands, 0, sizeof h->commands); }
void qa_q3_wire_command_history_clear(qa_q3_wire_command_history *h) { qa_q3_wire_command_history_restart(h); h->number = 0; }

typedef struct { int64_t command_number; int64_t server_time; double real_time; } sent_packet_t;
typedef struct { int32_t number; int64_t ping; } ping_entry_t;
struct qa_q3_client_connection {
  qa_q3_connection_identity identity;
  qa_q3_product product;
  qa_q3_client_mode mode;
  qa_q3_client_bindings bindings;
  qa_q3_client_reliable_commands *reliable;
  qa_q3_source_parse_entities *parse_entities;
  qa_q3_snapshot_history *history;
  qa_q3_client_game_state_storage *game_state;
  qa_q3_wire_command_history *commands;
  qa_q3_source_message_state source_state;
  qa_q3_entity_state_record baselines[1024];
  qa_q3_netchannel *channel;
  char server_commands[64][1024];
  sent_packet_t out_packets[32];
  ping_entry_t *snapshot_pings; size_t ping_count, ping_cap;
  qa_q3_server_command_executor *executor;
  int64_t server_message_sequence, server_command_sequence, last_executed_server_command;
  int64_t server_id, checksum_feed, client_number, generation;
  bool demo_waiting;
  double last_packet_sent_time;
};
static void client_print(void *ctx, const char *text) {
  qa_q3_client_connection *c = ctx;
  if (c->bindings.print) c->bindings.print(c->bindings.print_ctx, text);
}
static void game_state_drop(void *ctx, const char *message) {
  (void)ctx;
  qa_common_error(QA_COMMON_ERROR_DROP, message);
}
bool qa_q3_client_connection_init(qa_q3_client_connection **out, qa_q3_connection_identity identity,
                                  qa_q3_product product, const qa_q3_client_mode *mode, const qa_q3_client_bindings *bindings) {
  qa_q3_client_connection *c = calloc(1, sizeof *c);
  if (!c) return false;
  c->identity = identity; c->product = product; c->mode = *mode; c->bindings = *bindings;
  bool ok = qa_q3_client_reliable_commands_init(&c->reliable)
    && qa_q3_source_parse_entities_init(&c->parse_entities)
    && qa_q3_snapshot_history_init(&c->history, c->parse_entities)
    && qa_q3_client_game_state_storage_init(&c->game_state, game_state_drop, NULL)
    && qa_q3_wire_command_history_init(&c->commands)
    && qa_q3_server_command_executor_init(&c->executor);
  if (!ok) { qa_q3_client_connection_destroy(c); return false; }
  qa_q3_source_message_state_init(&c->source_state, client_print, c);
  for (int i = 0; i < 1024; i++) qa_q3_entity_state_record_init(&c->baselines[i], 0);
  c->demo_waiting = true;
  if (mode->kind == QA_Q3_CLIENT_MODE_NETWORK) {
    if (!qa_q3_netchannel_init(&c->channel, QA_Q3_CHANNEL_CLIENT, mode->qport, NULL, NULL)) {
      qa_q3_client_connection_destroy(c); return false;
    }
  }
  *out = c; return true;
}
void qa_q3_client_connection_destroy(qa_q3_client_connection *c) {
  if (!c) return;
  qa_q3_reliable_ring_destroy(c->reliable);
  qa_q3_source_parse_entities_destroy(c->parse_entities);
  qa_q3_snapshot_history_destroy(c->history);
  qa_q3_client_game_state_storage_destroy(c->game_state);
  qa_q3_wire_command_history_destroy(c->commands);
  qa_q3_server_command_executor_destroy(c->executor);
  qa_q3_netchannel_destroy(c->channel);
  free(c->snapshot_pings);
  free(c);
}
qa_q3_client_reliable_commands *qa_q3_client_connection_reliable(const qa_q3_client_connection *c) { return c->reliable; }
qa_q3_source_parse_entities *qa_q3_client_connection_parse_entities(const qa_q3_client_connection *c) { return c->parse_entities; }
qa_q3_snapshot_history *qa_q3_client_connection_history(const qa_q3_client_connection *c) { return c->history; }
qa_q3_client_game_state_storage *qa_q3_client_connection_game_state(const qa_q3_client_connection *c) { return c->game_state; }
qa_q3_wire_command_history *qa_q3_client_connection_commands(const qa_q3_client_connection *c) { return c->commands; }
qa_q3_source_message_state *qa_q3_client_connection_source_state(const qa_q3_client_connection *c) { return (qa_q3_source_message_state *)&c->source_state; }
qa_q3_entity_state_record *qa_q3_client_connection_baselines(const qa_q3_client_connection *c, size_t *count) { *count = 1024; return (qa_q3_entity_state_record *)c->baselines; }
qa_q3_netchannel *qa_q3_client_connection_channel(const qa_q3_client_connection *c) { return c->channel; }
qa_q3_connection_identity qa_q3_client_connection_identity(const qa_q3_client_connection *c) { return c->identity; }
qa_q3_product qa_q3_client_connection_get_product(const qa_q3_client_connection *c) { return c->product; }
const qa_q3_client_mode *qa_q3_client_connection_get_mode(const qa_q3_client_connection *c) { return &c->mode; }
const qa_q3_client_bindings *qa_q3_client_connection_get_bindings(const qa_q3_client_connection *c) { return &c->bindings; }
int64_t qa_q3_client_connection_server_message_sequence(const qa_q3_client_connection *c) { return c->server_message_sequence; }
void qa_q3_client_connection_set_server_message_sequence(qa_q3_client_connection *c, int64_t v) { c->server_message_sequence = v; }
int64_t qa_q3_client_connection_server_command_sequence(const qa_q3_client_connection *c) { return c->server_command_sequence; }
void qa_q3_client_connection_set_server_command_sequence(qa_q3_client_connection *c, int64_t v) { c->server_command_sequence = v; }
int64_t qa_q3_client_connection_last_executed_server_command(const qa_q3_client_connection *c) { return c->last_executed_server_command; }
void qa_q3_client_connection_set_last_executed_server_command(qa_q3_client_connection *c, int64_t v) { c->last_executed_server_command = v; }
int64_t qa_q3_client_connection_server_id(const qa_q3_client_connection *c) { return c->server_id; }
void qa_q3_client_connection_set_server_id(qa_q3_client_connection *c, int64_t v) { c->server_id = v; }
int64_t qa_q3_client_connection_checksum_feed(const qa_q3_client_connection *c) { return c->checksum_feed; }
void qa_q3_client_connection_set_checksum_feed(qa_q3_client_connection *c, int64_t v) { c->checksum_feed = v; }
int64_t qa_q3_client_connection_client_number(const qa_q3_client_connection *c) { return c->client_number; }
void qa_q3_client_connection_set_client_number(qa_q3_client_connection *c, int64_t v) { c->client_number = v; }
int64_t qa_q3_client_connection_generation(const qa_q3_client_connection *c) { return c->generation; }
void qa_q3_client_connection_set_generation(qa_q3_client_connection *c, int64_t v) { c->generation = v; }
bool qa_q3_client_connection_demo_waiting(const qa_q3_client_connection *c) { return c->demo_waiting; }
void qa_q3_client_connection_set_demo_waiting(qa_q3_client_connection *c, bool v) { c->demo_waiting = v; }
double qa_q3_client_connection_last_packet_sent_time(const qa_q3_client_connection *c) { return c->last_packet_sent_time; }
void qa_q3_client_connection_set_last_packet_sent_time(qa_q3_client_connection *c, double v) { c->last_packet_sent_time = v; }
static const char *server_command_text(qa_q3_client_connection *c, int64_t sequence) {
  return c->server_commands[((int32_t)sequence) & 63];
}
static const char *server_command_cb(void *ctx, int64_t sequence) { return server_command_text(ctx, sequence); }
static void clear_active(qa_q3_client_connection *c) {
  c->bindings.clear_active(c->bindings.ctx);
  c->bindings.assert_current(c->bindings.ctx);
  qa_q3_snapshot_history_clear(c->history);
  c->ping_count = 0;
  qa_q3_source_parse_entities_clear(c->parse_entities);
  qa_q3_client_game_state_storage_clear(c->game_state);
  qa_q3_wire_command_history_clear(c->commands);
  c->server_id = 0;
  for (int i = 0; i < 1024; i++) qa_q3_entity_state_record_init(&c->baselines[i], 0);
  memset(c->out_packets, 0, sizeof c->out_packets);
  c->generation++;
}
bool qa_q3_client_connection_snapshot_ping(qa_q3_client_connection *c, int64_t number, int64_t *out) {
  bool valid = false;
  qa_q3_wire_snapshot snap;
  if (!qa_q3_snapshot_history_read_slot(c->history, number, &valid, &snap)) return false;
  bool match = valid && snap.message_number == number;
  qa_q3_wire_snapshot_destroy(&snap);
  if (!match) return false;
  for (size_t i = 0; i < c->ping_count; i++) if (c->snapshot_pings[i].number == number) { *out = c->snapshot_pings[i].ping; return true; }
  return false;
}
static int64_t compute_ping(qa_q3_client_connection *c, const qa_q3_wire_snapshot *snapshot, double real_time) {
  int64_t outgoing = c->channel ? qa_q3_netchannel_outgoing_sequence(c->channel) : 1;
  for (int index = 0; index < 32; index++) {
    const sent_packet_t *packet = &c->out_packets[((int32_t)(outgoing - 1 - index)) & 31];
    if (snapshot->player_state.command_time >= packet->server_time) return (int32_t)(real_time - packet->real_time);
  }
  return 999;
}
static void ping_set(qa_q3_client_connection *c, int32_t number, int64_t ping) {
  for (size_t i = 0; i < c->ping_count; i++) if (c->snapshot_pings[i].number == number) { c->snapshot_pings[i].ping = ping; return; }
  if (c->ping_count == c->ping_cap) {
    size_t nc = c->ping_cap ? c->ping_cap * 2 : 16;
    ping_entry_t *np = realloc(c->snapshot_pings, nc * sizeof *np);
    if (!np) qa_range_error("snapshot ping allocation failed");
    c->snapshot_pings = np; c->ping_cap = nc;
  }
  c->snapshot_pings[c->ping_count].number = number;
  c->snapshot_pings[c->ping_count].ping = ping;
  c->ping_count++;
}
static void apply_system_info(qa_q3_client_connection *c) {
  char *info = NULL;
  qa_q3_client_game_state_storage_get(c->game_state, 1, &info);
  c->server_id = 0;
  char *value = qa_q3_info_value(info ? info : "", "sv_serverid");
  if (value) {
    /* info_value returns "" when missing; TS only sets when key present (atoi("")=0 anyway). */
    c->server_id = qa_native_atoi(value);
    free(value);
  }
  c->bindings.system_info(c->bindings.ctx, info ? info : "");
  free(info);
}
static qa_q3_entity_state_record *baseline_cb(void *ctx, int64_t number) {
  qa_q3_client_connection *c = ctx;
  if (number < 0 || number >= 1024) return NULL;
  return &c->baselines[number];
}
static const qa_q3_snapshot_history_entry *history_cb(void *ctx, int64_t delta_number) {
  qa_q3_client_connection *c = ctx;
  return qa_q3_snapshot_history_borrow_slot(c->history, delta_number, c->product);
}
static int64_t download_size_cb(void *ctx, int64_t size) {
  qa_q3_client_connection *c = ctx;
  return c->bindings.download_size(c->bindings.ctx, size);
}
bool qa_q3_client_connection_receive_message(qa_q3_client_connection *c, int64_t sequence, const uint8_t *bytes, size_t len, double real_time, qa_q3_server_message *out) {
  memset(out, 0, sizeof *out);
  c->bindings.assert_current(c->bindings.ctx);
  if (c->mode.kind == QA_Q3_CLIENT_MODE_NETWORK && (sequence <= c->server_message_sequence || sequence < 1))
    qa_range_error("Server message sequence must advance");
  c->server_message_sequence = sequence;
  qa_q3_server_message_cursor *cursor = NULL;
  if (!qa_q3_server_message_cursor_init(&cursor, bytes, len, "<q3-server-message>", NULL,
      c->mode.kind == QA_Q3_CLIENT_MODE_NETWORK ? 4 : 0, c->parse_entities, download_size_cb, c)) return false;
  int64_t acknowledge = 0, command_sequence = 0, client_number = 0, checksum_feed = 0;
  qa_q3_gamestate pending; memset(&pending, 0, sizeof pending);
  size_t pending_cap = 0;
  bool ok = true, done = false;
  while (!done) {
    c->bindings.assert_current(c->bindings.ctx);
    qa_q3_server_message_context ctx;
    ctx.product = c->product;
    ctx.message_number = c->server_message_sequence;
    ctx.reliable_sequence = qa_q3_reliable_ring_sequence(c->reliable);
    ctx.server_command_sequence = c->server_command_sequence;
    ctx.parse_entities_number = qa_q3_source_parse_entities_number(c->parse_entities);
    ctx.baseline = baseline_cb; ctx.baseline_ctx = c;
    ctx.history = history_cb; ctx.history_ctx = c;
    qa_q3_server_message_step step;
    if (!qa_q3_server_message_cursor_next(cursor, &ctx, &step)) { ok = false; break; }
    switch (step.kind) {
      case QA_Q3_STEP_ACKNOWLEDGE:
        acknowledge = step.sequence;
        qa_q3_reliable_ring_assign_acknowledgement(c->reliable, step.sequence);
        break;
      case QA_Q3_STEP_GAMESTATE_START:
        clear_active(c);
        qa_q3_gamestate_destroy(&pending); memset(&pending, 0, sizeof pending); pending_cap = 0;
        break;
      case QA_Q3_STEP_GAMESTATE_SEQUENCE:
        command_sequence = step.sequence; pending.command_sequence = step.sequence;
        c->server_command_sequence = step.sequence;
        qa_q3_client_game_state_storage_begin_entries(c->game_state);
        break;
      case QA_Q3_STEP_GAMESTATE_ENTRY: {
        if (pending.entry_count == pending_cap) {
          size_t nc = pending_cap ? pending_cap * 2 : 16;
          qa_q3_gamestate_entry *ne = realloc(pending.entries, nc * sizeof *ne);
          if (!ne) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
          pending.entries = ne; pending_cap = nc;
        }
        pending.entries[pending.entry_count] = step.entry;
        memset(&step.entry, 0, sizeof step.entry);
        const qa_q3_gamestate_entry *entry = &pending.entries[pending.entry_count++];
        if (entry->kind == QA_Q3_GS_CONFIGSTRING) {
          qa_q3_client_game_state_storage_append(c->game_state, entry->index, entry->value ? entry->value : "");
        } else {
          if (entry->number < 0 || entry->number >= 1024) {
            qa_q3_server_message_step_destroy(&step);
            qa_range_error("Invalid source baseline number");
          }
          c->baselines[entry->number] = entry->entity;
        }
        break;
      }
      case QA_Q3_STEP_GAMESTATE_CLIENT:
        client_number = step.number; pending.client_number = step.number; c->client_number = step.number;
        break;
      case QA_Q3_STEP_GAMESTATE_CHECKSUM:
        checksum_feed = step.checksum; pending.checksum_feed = step.checksum; c->checksum_feed = step.checksum;
        break;
      case QA_Q3_STEP_GAMESTATE_END: {
        pending.command_sequence = command_sequence;
        pending.client_number = client_number;
        pending.checksum_feed = checksum_feed;
        qa_q3_server_operation *ops = realloc(out->operations, (out->operation_count + 1) * sizeof *ops);
        if (!ops) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
        out->operations = ops;
        qa_q3_server_operation *op = &out->operations[out->operation_count++];
        memset(op, 0, sizeof *op); op->kind = QA_Q3_OP_GAMESTATE; op->gamestate = pending;
        memset(&pending, 0, sizeof pending); pending_cap = 0;
        apply_system_info(c);
        c->bindings.assert_current(c->bindings.ctx);
        c->bindings.gamestate(c->bindings.ctx, &op->gamestate, c->generation);
        break;
      }
      case QA_Q3_STEP_SNAPSHOT_HEADER:
        if (step.delta_number <= 0) c->demo_waiting = false;
        break;
      case QA_Q3_STEP_OPERATION: {
        qa_q3_server_operation *ops = realloc(out->operations, (out->operation_count + 1) * sizeof *ops);
        if (!ops) { qa_q3_server_message_step_destroy(&step); ok = false; done = true; break; }
        out->operations = ops;
        out->operations[out->operation_count] = step.operation;
        memset(&step.operation, 0, sizeof step.operation);
        qa_q3_server_operation *op = &out->operations[out->operation_count++];
        switch (op->kind) {
          case QA_Q3_OP_NOP: break;
          case QA_Q3_OP_COMMAND:
            c->server_command_sequence = op->sequence;
            {
              char *slot = c->server_commands[((int32_t)op->sequence) & 63];
              size_t n = op->text ? strlen(op->text) : 0; if (n > 1023) n = 1023;
              memcpy(slot, op->text ? op->text : "", n); slot[n] = 0;
            }
            break;
          case QA_Q3_OP_SNAPSHOT:
            if (qa_q3_snapshot_history_publish(c->history, op)) {
              int64_t ping = compute_ping(c, &op->snapshot, real_time);
              for (size_t i = 0; i < c->ping_count;) {
                if ((int32_t)(op->snapshot.message_number - c->snapshot_pings[i].number) >= QA_Q3_SNAPSHOT_BACKUP) {
                  memmove(c->snapshot_pings + i, c->snapshot_pings + i + 1, (c->ping_count - i - 1) * sizeof *c->snapshot_pings);
                  c->ping_count--;
                } else i++;
              }
              ping_set(c, (int32_t)op->snapshot.message_number, ping);
              c->bindings.snapshot(c->bindings.ctx, &op->snapshot, ping);
            }
            break;
          case QA_Q3_OP_DOWNLOAD:
            c->bindings.download(c->bindings.ctx, &op->download);
            break;
          case QA_Q3_OP_GAMESTATE: break;
        }
        break;
      }
      case QA_Q3_STEP_END:
        out->reliable_acknowledge = acknowledge;
        out->server_command_sequence = c->server_command_sequence;
        out->parse_entities_number = qa_q3_source_parse_entities_number(c->parse_entities);
        out->terminal = step.terminal;
        done = true;
        break;
    }
    qa_q3_server_message_step_destroy(&step);
  }
  qa_q3_gamestate_destroy(&pending);
  qa_q3_server_message_cursor_destroy(cursor);
  if (!ok) qa_q3_server_message_destroy(out);
  return ok;
}
bool qa_q3_client_connection_copy_gamestate(qa_q3_client_connection *c, qa_q3_gamestate *out) {
  memset(out, 0, sizeof *out);
  size_t cap = 0;
  for (int index = 0; index < 1024; index++) {
    char *value = NULL;
    if (!qa_q3_client_game_state_storage_get(c->game_state, index, &value)) return false;
    if (!value) continue;
    if (out->entry_count == cap) {
      size_t nc = cap ? cap * 2 : 16;
      qa_q3_gamestate_entry *ne = realloc(out->entries, nc * sizeof *ne);
      if (!ne) { free(value); qa_q3_gamestate_destroy(out); return false; }
      out->entries = ne; cap = nc;
    }
    qa_q3_gamestate_entry *e = &out->entries[out->entry_count++];
    memset(e, 0, sizeof *e);
    e->kind = QA_Q3_GS_CONFIGSTRING; e->index = index; e->value = value;
  }
  for (int number = 0; number < 1024; number++) {
    if (c->baselines[number].number == 0) continue;
    if (out->entry_count == cap) {
      size_t nc = cap ? cap * 2 : 16;
      qa_q3_gamestate_entry *ne = realloc(out->entries, nc * sizeof *ne);
      if (!ne) { qa_q3_gamestate_destroy(out); return false; }
      out->entries = ne; cap = nc;
    }
    qa_q3_gamestate_entry *e = &out->entries[out->entry_count++];
    memset(e, 0, sizeof *e);
    e->kind = QA_Q3_GS_BASELINE; e->number = number; e->entity = c->baselines[number];
  }
  out->command_sequence = c->server_command_sequence;
  out->client_number = c->client_number;
  out->checksum_feed = c->checksum_feed;
  return true;
}
void qa_q3_client_packet_result_destroy(qa_q3_client_packet_result *r) {
  free(r->channel.payload); r->channel.payload = NULL;
  qa_q3_server_message_destroy(&r->message);
  free(r->plaintext); r->plaintext = NULL;
}
bool qa_q3_client_connection_receive_datagram(qa_q3_client_connection *c, const uint8_t *packet, size_t len, double real_time, qa_q3_client_packet_result *out) {
  memset(out, 0, sizeof *out);
  c->bindings.assert_current(c->bindings.ctx);
  if (!c->channel || c->mode.kind != QA_Q3_CLIENT_MODE_NETWORK) qa_range_error("Demo connection cannot receive datagrams");
  if (!qa_q3_netchannel_receive(c->channel, packet, len, NULL, &out->channel)) return false;
  if (out->channel.kind != QA_Q3_CHANNEL_ACCEPTED) return true;
  qa_q3_message_reader *reader = NULL;
  if (!qa_q3_message_reader_init(&reader, out->channel.payload, out->channel.payload_len, QA_Q3_MESSAGE_BITSTREAM, NULL)) return false;
  int64_t ack = qa_q3_message_reader_read_long(reader);
  qa_q3_message_reader_destroy(reader);
  if (!qa_q3_xor_server_message(out->channel.payload, out->channel.payload_len, c->mode.challenge, out->channel.sequence,
        qa_q3_reliable_ring_lookup_masked(c->reliable, ack), &out->plaintext)) return false;
  out->plaintext_len = out->channel.payload_len;
  return qa_q3_client_connection_receive_message(c, out->channel.sequence, out->plaintext, out->plaintext_len, real_time, &out->message);
}
typedef struct { qa_q3_client_connection *c; } demo_seq_t;
static void demo_seq(void *ctx, int64_t sequence) { ((demo_seq_t *)ctx)->c->server_message_sequence = sequence; }
bool qa_q3_client_connection_read_demo(qa_q3_client_connection *c, double real_time, bool *is_end, qa_q3_demo_result *end_out, qa_q3_server_message *message_out) {
  if (c->mode.kind != QA_Q3_CLIENT_MODE_DEMO) qa_range_error("Network connection cannot read demo messages");
  demo_seq_t ds = { c };
  qa_q3_demo_result record;
  if (!c->mode.reader.next(c->mode.reader.ctx, demo_seq, &ds, &record)) return false;
  if (record.kind == QA_Q3_DEMO_END) { *is_end = true; if (end_out) *end_out = record; return true; }
  *is_end = false;
  bool ok = qa_q3_client_connection_receive_message(c, record.sequence, record.payload, record.payload_len, real_time, message_out);
  free(record.payload);
  return ok;
}
static void exec_assert(void *ctx) { qa_q3_client_connection *c = ctx; c->bindings.assert_current(c->bindings.ctx); }
static void exec_system_info(void *ctx) { apply_system_info(ctx); }
static void exec_map_restart(void *ctx) {
  qa_q3_client_connection *c = ctx;
  qa_q3_wire_command_history_restart(c->commands);
  c->bindings.map_restart(c->bindings.ctx);
}
static bool exec_local(void *ctx) { qa_q3_client_connection *c = ctx; return c->bindings.local_server_running(c->bindings.ctx); }
static void exec_level_shot(void *ctx) { qa_q3_client_connection *c = ctx; c->bindings.level_shot(c->bindings.ctx); }
bool qa_q3_client_connection_get_server_command(qa_q3_client_connection *c, int64_t sequence, char ***argv_out, size_t *argc_out) {
  c->bindings.assert_current(c->bindings.ctx);
  if (sequence <= c->server_command_sequence - 64) {
    if (c->mode.kind == QA_Q3_CLIENT_MODE_DEMO) { *argv_out = NULL; *argc_out = 0; return false; }
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetServerCommand: a reliable command was cycled out");
  }
  if (sequence > c->server_command_sequence)
    qa_common_error(QA_COMMON_ERROR_DROP, "CL_GetServerCommand: requested a command not received");
  c->last_executed_server_command = sequence;
  qa_q3_server_command_bindings bindings;
  bindings.assert_current = exec_assert;
  bindings.system_info = exec_system_info;
  bindings.map_restart = exec_map_restart;
  bindings.level_shot = exec_level_shot;
  bindings.local_server_running = exec_local;
  bindings.ctx = c;
  return qa_q3_server_command_executor_execute(c->executor, server_command_text(c, sequence), c->game_state, &bindings, argv_out, argc_out);
}
void qa_q3_client_connection_transmit(qa_q3_client_connection *c, const qa_q3_client_send_options *options, const qa_q3_channel_delivery *delivery) {
  c->bindings.assert_current(c->bindings.ctx);
  if (!c->channel || c->mode.kind != QA_Q3_CLIENT_MODE_NETWORK) qa_range_error("Demo connection cannot send packets");
  int64_t packet_dup = (int64_t)trunc(options->packet_dup);
  if (packet_dup < 0) packet_dup = 0;
  if (packet_dup > 5) packet_dup = 5;
  int64_t outgoing = qa_q3_netchannel_outgoing_sequence(c->channel);
  const sent_packet_t *old = &c->out_packets[((int32_t)(outgoing - 1 - packet_dup)) & 31];
  int64_t count = qa_q3_wire_command_history_current_number(c->commands) - old->command_number;
  if (count > 32) count = 32;
  if (count < 0) count = 0;
  qa_q3_client_movement movement; memset(&movement, 0, sizeof movement);
  movement.command_count = (size_t)count;
  int64_t current = qa_q3_wire_command_history_current_number(c->commands);
  for (int64_t index = 0; index < count; index++) {
    if (!qa_q3_wire_command_history_read(c->commands, current - count + index + 1, &movement.commands[index]))
      qa_range_error("Outgoing user command was overwritten");
  }
  qa_q3_client_header header = { c->server_id, c->server_message_sequence, c->server_command_sequence };
  int64_t outstanding = qa_q3_reliable_ring_outstanding(c->reliable);
  size_t pending_cap = outstanding > 0 ? (size_t)outstanding : 1;
  qa_q3_reliable_command *pending = calloc(pending_cap, sizeof *pending);
  if (!pending) qa_range_error("client transmit allocation failed");
  size_t pending_count = qa_q3_reliable_ring_pending(c->reliable, pending, pending_cap);
  qa_q3_message_writer *writer = NULL;
  if (!qa_q3_begin_client_message(&header, pending, pending_count, &c->source_state, &writer)) { free(pending); qa_range_error("client transmit allocation failed"); }
  free(pending);
  if (count != 0) {
    qa_q3_wire_snapshot latest;
    bool has_latest = qa_q3_snapshot_history_latest(c->history, &latest);
    bool no_delta = options->no_delta || !has_latest || c->demo_waiting
      || (has_latest && latest.message_number != c->server_message_sequence);
    if (has_latest) qa_q3_wire_snapshot_destroy(&latest);
    movement.kind = no_delta ? QA_Q3_MOVE_NO_DELTA : QA_Q3_MOVE;
    qa_q3_client_key_context key_ctx = { c->checksum_feed, server_command_cb, c };
    qa_q3_write_client_movement(writer, &movement, &header, &key_ctx);
  }
  sent_packet_t *slot = &c->out_packets[((int32_t)outgoing) & 31];
  slot->command_number = current;
  slot->server_time = count ? movement.commands[count - 1].server_time : 0;
  slot->real_time = options->real_time;
  c->last_packet_sent_time = options->real_time;
  uint8_t *finished = NULL; size_t finished_len = 0;
  if (!qa_q3_finish_client_message(writer, &finished, &finished_len)) { qa_q3_message_writer_destroy(writer); qa_range_error("client transmit allocation failed"); }
  qa_q3_message_writer_destroy(writer);
  uint8_t *bytes = NULL;
  if (!qa_q3_xor_client_message(finished, finished_len, c->mode.challenge, server_command_cb, c, &bytes)) { free(finished); qa_range_error("client transmit allocation failed"); }
  free(finished);
  qa_q3_netchannel_begin_transmit(c->channel, bytes, finished_len, delivery);
  free(bytes);
  while (qa_q3_netchannel_has_unsent_fragments(c->channel)) qa_q3_netchannel_transmit_next_fragment(c->channel, delivery);
}
bool qa_q3_client_connection_ready_to_send(const qa_q3_client_connection *c, const qa_q3_client_send_readiness *options) {
  if (c->mode.kind == QA_Q3_CLIENT_MODE_DEMO || !c->channel || options->cinematic) return false;
  if (options->downloading && options->real_time - c->last_packet_sent_time < 50) return false;
  if (!options->active && !options->primed && !options->downloading && options->real_time - c->last_packet_sent_time < 1000) return false;
  if (options->local || options->lan) return true;
  int64_t maximum = (int64_t)trunc(options->maximum_packets);
  if (maximum < 15) maximum = 15;
  if (maximum > 125) maximum = 125;
  int64_t outgoing = qa_q3_netchannel_outgoing_sequence(c->channel);
  const sent_packet_t *previous = &c->out_packets[((int32_t)(outgoing - 1)) & 31];
  return options->real_time - previous->real_time >= trunc(1000.0 / (double)maximum);
}
void qa_q3_client_connection_disconnect_packets(qa_q3_client_connection *c, const qa_q3_client_send_options *options, const qa_q3_channel_delivery *delivery) {
  if (c->mode.kind != QA_Q3_CLIENT_MODE_NETWORK) return;
  qa_q3_reliable_command cmd;
  if (!qa_q3_client_reliable_commands_add(c->reliable, "disconnect", &cmd))
    qa_range_error("Client command overflow");
  for (int pass = 0; pass < 3; pass++) qa_q3_client_connection_transmit(c, options, delivery);
}
