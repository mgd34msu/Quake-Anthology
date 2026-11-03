/* Port of src/network/q3/client-message.ts. GPL-2.0-or-later. */
#include "qa/network/q3/client_message.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int64_t qa_q3_command_hash(const char *text, int64_t max_length) {
  if (max_length < 0) qa_range_error("Invalid command hash length");
  int32_t hash = 0;
  size_t n = strlen(text);
  for (int64_t i = 0; i < max_length && i < (int64_t)n; i++) {
    int32_t byte = (int8_t)(unsigned char)text[i]; /* (byte<<24)>>24 */
    if (byte == 0) break;
    hash = (int32_t)((uint32_t)hash + (uint32_t)(byte * (int32_t)(119 + i)));
  }
  return (int32_t)((uint32_t)hash ^ (uint32_t)(hash >> 10) ^ (uint32_t)(hash >> 20));
}
int64_t qa_q3_command_hash_default(const char *text) { return qa_q3_command_hash(text, 32); }
static int64_t move_key(const qa_q3_client_header *h, const qa_q3_client_key_context *ctx) {
  const char *cmd = ctx->server_command(ctx->server_command_ctx, h->reliable_acknowledge);
  return (int32_t)((uint32_t)ctx->checksum_feed ^ (uint32_t)h->message_acknowledge ^ (uint32_t)qa_q3_command_hash(cmd ? cmd : "", 32));
}
bool qa_q3_begin_client_message(const qa_q3_client_header *header, const qa_q3_reliable_command *commands, size_t count,
                                qa_q3_source_message_state *source_state, qa_q3_message_writer **out) {
  if (header->server_id < -((int64_t)0x80000000) || header->server_id > 0x7fffffff)
    qa_range_error("Client serverId must be int32");
  if (header->message_acknowledge < 0 || header->message_acknowledge > 0x7fffffff
      || header->reliable_acknowledge < 0 || header->reliable_acknowledge > 0x7fffffff)
    qa_range_error("Client acknowledgements must be nonnegative int32 values");
  if (!qa_q3_message_writer_init(out, QA_Q3_MESSAGE_BITSTREAM, QA_Q3_MAX_MESSAGE_LENGTH, source_state)) return false;
  qa_q3_message_writer *w = *out;
  qa_q3_message_writer_write_long(w, header->server_id);
  qa_q3_message_writer_write_long(w, header->message_acknowledge);
  qa_q3_message_writer_write_long(w, header->reliable_acknowledge);
  bool has_prev = false; int64_t prev = 0;
  for (size_t i = 0; i < count; i++) {
    int64_t seq = commands[i].sequence;
    if (seq < -((int64_t)0x80000000) || seq > 0x7fffffff || (has_prev && seq <= prev))
      qa_range_error("Client reliable commands must have increasing int32 sequences");
    qa_q3_message_writer_write_byte(w, QA_Q3_CLC_COMMAND);
    qa_q3_message_writer_write_long(w, seq);
    qa_q3_message_writer_write_string(w, commands[i].text);
    has_prev = true; prev = seq;
  }
  return true;
}
void qa_q3_write_client_movement(qa_q3_message_writer *w, const qa_q3_client_movement *movement,
                                 const qa_q3_client_header *header, const qa_q3_client_key_context *context) {
  if (movement->command_count < 1 || movement->command_count > QA_Q3_MAX_PACKET_USER_COMMANDS)
    qa_range_error("Movement needs 1 through 32 backup commands");
  qa_q3_message_writer_write_byte(w, movement->kind == QA_Q3_MOVE ? QA_Q3_CLC_MOVE : QA_Q3_CLC_MOVE_NO_DELTA);
  qa_q3_message_writer_write_byte(w, (int64_t)movement->command_count);
  int64_t key = move_key(header, context);
  qa_q3_wire_user_command old; memset(&old, 0, sizeof old);
  for (size_t i = 0; i < movement->command_count; i++) {
    qa_q3_write_delta_user_command(w, &old, &movement->commands[i], true, key);
    old = movement->commands[i];
  }
}
bool qa_q3_finish_client_message(qa_q3_message_writer *w, uint8_t **out, size_t *out_len) {
  qa_q3_message_writer_write_byte(w, QA_Q3_CLC_EOF);
  if (qa_q3_message_writer_overflowed(w)) qa_range_error("Client message exceeds MAX_MSGLEN");
  return qa_q3_message_writer_to_bytes(w, out, out_len);
}
bool qa_q3_encode_client_message(const qa_q3_client_message *message, const qa_q3_client_key_context *context,
                                 qa_q3_source_message_state *source_state, uint8_t **out, size_t *out_len) {
  qa_q3_message_writer *w = NULL;
  if (!qa_q3_begin_client_message(&message->header, message->commands, message->command_count, source_state, &w)) return false;
  if (message->movement) qa_q3_write_client_movement(w, message->movement, &message->header, context);
  bool ok = qa_q3_finish_client_message(w, out, out_len);
  qa_q3_message_writer_destroy(w);
  return ok;
}
enum read_phase { RP_HEADER, RP_COMMANDS, RP_MOVEMENT, RP_TERMINAL, RP_DONE, RP_FAILED };
static const char *phase_kind(enum read_phase p) {
  switch (p) { case RP_HEADER: return "header"; case RP_COMMANDS: return "commands"; case RP_MOVEMENT: return "movement";
    case RP_TERMINAL: return "terminal"; case RP_DONE: return "done"; default: return "failed"; }
}
struct qa_q3_client_message_reader {
  qa_q3_message_reader *reader; uint8_t *owned;
  int64_t prefix_server_id, prefix_msg_ack;
  enum read_phase phase;
  qa_q3_client_header header; qa_q3_client_movement_kind movement_kind;
};
static void reader_cleanup(void *r) { qa_q3_client_message_reader_destroy(r); }
bool qa_q3_client_message_reader_init(qa_q3_client_message_reader **out, const uint8_t *bytes, size_t len, const char *source) {
  const char *src = source ? source : "<client-message>";
  /* Validate the source cap before copying a caller-owned datagram (MessageReader throws over cap). */
  qa_q3_message_reader *validated = NULL;
  if (!qa_q3_message_reader_init(&validated, bytes, len, QA_Q3_MESSAGE_BITSTREAM, src)) return false;
  qa_q3_message_reader_destroy(validated);
  qa_q3_client_message_reader *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->owned = malloc(len ? len : 1);
  if (!r->owned) { free(r); return false; }
  memcpy(r->owned, bytes, len);
  if (!qa_q3_message_reader_init(&r->reader, r->owned, len, QA_Q3_MESSAGE_BITSTREAM, src)) { free(r->owned); free(r); return false; }
  /* The prefix longs raise BinaryError on a short datagram; the reader is not yet the caller's. */
  qa_cleanup guard;
  qa_cleanup_push(&guard, reader_cleanup, r);
  r->prefix_server_id = qa_q3_message_reader_read_long(r->reader);
  r->prefix_msg_ack = qa_q3_message_reader_read_long(r->reader);
  qa_cleanup_pop(&guard);
  r->phase = RP_HEADER;
  *out = r; return true;
}
void qa_q3_client_message_reader_destroy(qa_q3_client_message_reader *r) {
  if (!r) return;
  qa_q3_message_reader_destroy(r->reader); free(r->owned); free(r);
}
void qa_q3_client_message_reader_prefix(const qa_q3_client_message_reader *r, int64_t *server_id, int64_t *message_acknowledge) {
  *server_id = r->prefix_server_id; *message_acknowledge = r->prefix_msg_ack;
}
int64_t qa_q3_client_message_reader_read_count(const qa_q3_client_message_reader *r) { return qa_q3_message_reader_read_count(r->reader); }
qa_q3_client_header qa_q3_client_message_reader_read_header(qa_q3_client_message_reader *r) {
  if (r->phase != RP_HEADER) qa_raise("Error", 0, 0, "Cannot read client header during %s", phase_kind(r->phase));
  r->phase = RP_FAILED;
  qa_q3_client_header h = { r->prefix_server_id, r->prefix_msg_ack, qa_q3_message_reader_read_long(r->reader) };
  r->phase = RP_COMMANDS; r->header = h;
  return h;
}
bool qa_q3_client_message_reader_next(qa_q3_client_message_reader *r, qa_q3_client_message_part *out) {
  memset(out, 0, sizeof *out);
  if (r->phase != RP_COMMANDS) qa_raise("Error", 0, 0, "Cannot read next client part during %s", phase_kind(r->phase));
  enum read_phase phase = r->phase;
  r->phase = RP_FAILED;
  int64_t opcode = qa_q3_message_reader_read_byte(r->reader);
  if (opcode == QA_Q3_CLC_COMMAND) {
    int64_t seq = qa_q3_message_reader_read_long(r->reader);
    char *text = NULL;
    if (!qa_q3_message_reader_read_string(r->reader, &text)) return false;
    out->kind = QA_Q3_PART_COMMAND; out->command.sequence = seq;
    snprintf(out->command.text, sizeof out->command.text, "%s", text);
    free(text);
    r->phase = phase;
    return true;
  }
  if (opcode == QA_Q3_CLC_EOF) { r->phase = RP_DONE; out->kind = QA_Q3_PART_EOF; return true; }
  if (opcode != QA_Q3_CLC_MOVE && opcode != QA_Q3_CLC_MOVE_NO_DELTA)
    qa_raise(QA_Q3_INVALID_CLIENT_OPCODE_ERROR, (int)opcode, qa_q3_message_reader_read_count(r->reader), "%s:%lld: Invalid client opcode %lld",
             qa_q3_message_reader_source(r->reader), (long long)qa_q3_message_reader_read_count(r->reader), (long long)opcode);
  qa_q3_client_movement_kind kind = opcode == QA_Q3_CLC_MOVE ? QA_Q3_MOVE : QA_Q3_MOVE_NO_DELTA;
  r->phase = RP_MOVEMENT; r->movement_kind = kind;
  out->kind = QA_Q3_PART_MOVEMENT; out->movement_kind = kind;
  out->delta_message = kind == QA_Q3_MOVE ? r->header.message_acknowledge : -1;
  return true;
}
bool qa_q3_client_message_reader_read_movement(qa_q3_client_message_reader *r, const qa_q3_client_key_context *context, qa_q3_unfiltered_client_movement *out) {
  memset(out, 0, sizeof *out);
  if (r->phase != RP_MOVEMENT) qa_raise("Error", 0, 0, "Cannot read client movement during %s", phase_kind(r->phase));
  qa_q3_client_movement_kind kind = r->movement_kind;
  qa_q3_client_header header = r->header;
  r->phase = RP_FAILED;
  int64_t count = qa_q3_message_reader_read_byte(r->reader);
  if (count < 1 || count > QA_Q3_MAX_PACKET_USER_COMMANDS)
    qa_raise(QA_Q3_INVALID_CLIENT_COMMAND_COUNT_ERROR, (int)count, qa_q3_message_reader_read_count(r->reader), "%s:%lld: Invalid user command count %lld",
             qa_q3_message_reader_source(r->reader), (long long)qa_q3_message_reader_read_count(r->reader), (long long)count);
  int64_t key = move_key(&header, context);
  qa_q3_wire_user_command zero; memset(&zero, 0, sizeof zero);
  qa_q3_wire_user_command old;
  qa_q3_read_delta_user_command(r->reader, &zero, true, key, &old);
  out->base.commands[0] = old;
  for (int64_t i = 1; i < count; i++) {
    qa_q3_read_delta_user_command(r->reader, &old, true, key, &old);
    out->base.commands[i] = old;
  }
  out->base.kind = kind; out->base.command_count = (size_t)count;
  out->delta_message = kind == QA_Q3_MOVE ? header.message_acknowledge : -1;
  r->phase = RP_TERMINAL;
  return true;
}
void qa_q3_client_message_reader_validate_terminal(qa_q3_client_message_reader *r) {
  if (r->phase != RP_TERMINAL) qa_raise("Error", 0, 0, "Cannot validate client terminal during %s", phase_kind(r->phase));
  r->phase = RP_FAILED;
  if (qa_q3_message_reader_read_byte(r->reader) != QA_Q3_CLC_EOF)
    qa_binary_error(qa_q3_message_reader_source(r->reader), qa_q3_message_reader_read_count(r->reader), "Missing terminal clc_EOF after movement");
  r->phase = RP_DONE;
}
void qa_q3_filter_client_movement(const qa_q3_unfiltered_client_movement *movement, int64_t last_user_command_time, qa_q3_decoded_client_movement *out) {
  memset(out, 0, sizeof *out);
  if (movement->base.command_count < 1 || movement->base.command_count > QA_Q3_MAX_PACKET_USER_COMMANDS)
    qa_range_error("Movement needs 1 through 32 backup commands");
  int64_t latest = movement->base.commands[movement->base.command_count - 1].server_time;
  size_t n = 0;
  for (size_t i = 0; i < movement->base.command_count; i++) {
    const qa_q3_wire_user_command *cmd = &movement->base.commands[i];
    if (cmd->server_time > latest || cmd->server_time <= last_user_command_time) continue;
    out->executable_commands[n++] = *cmd;
    last_user_command_time = cmd->server_time;
  }
  out->base = movement->base; out->delta_message = movement->delta_message;
  out->executable_count = n; out->last_user_command_time = last_user_command_time;
}
static void free_commands(void *slot) { free(*(qa_q3_reliable_command **)slot); }
void qa_q3_decoded_client_message_destroy(qa_q3_decoded_client_message *m) { free(m->commands); m->commands = NULL; m->command_count = 0; }
bool qa_q3_decode_client_message(const uint8_t *bytes, size_t len, const qa_q3_client_decode_context *context,
                                 const char *source, qa_q3_decoded_client_message *out) {
  memset(out, 0, sizeof *out);
  qa_q3_client_message_reader *reader = NULL;
  if (!qa_q3_client_message_reader_init(&reader, bytes, len, source)) return false;
  /* Any decode raise below abandons this frame; the nodes free the reader and the
   * command list (the TS objects are collected). */
  qa_q3_reliable_command *commands = NULL; size_t count = 0, cap = 0;
  qa_cleanup reader_guard, commands_guard;
  qa_cleanup_push(&reader_guard, reader_cleanup, reader);
  qa_cleanup_push(&commands_guard, free_commands, &commands);
  qa_q3_client_header header = qa_q3_client_message_reader_read_header(reader);
  int64_t last = context->last_client_command;
  out->header = header; out->last_client_command = last;
  #define RELEASE() do { qa_cleanup_pop(&commands_guard); qa_cleanup_pop(&reader_guard); } while (0)
  #define REJECT(rsn, hdr) do { RELEASE(); out->kind = QA_Q3_CLIENT_REJECTED; out->reason = (rsn); out->header = (hdr); \
    out->commands = commands; out->command_count = count; out->last_client_command = last; \
    qa_q3_client_message_reader_destroy(reader); return true; } while (0)
  if (header.message_acknowledge < 0) REJECT(QA_Q3_CLIENT_NEGATIVE_MESSAGE_ACK, header);
  if (header.reliable_acknowledge < 0) REJECT(QA_Q3_CLIENT_NEGATIVE_RELIABLE_ACK, header);
  if (header.reliable_acknowledge < context->reliable_sequence - 64) {
    qa_q3_client_header h = header; h.reliable_acknowledge = context->reliable_sequence;
    REJECT(QA_Q3_CLIENT_STALE_RELIABLE_ACK, h);
  }
  if (header.reliable_acknowledge > context->reliable_sequence) REJECT(QA_Q3_CLIENT_FUTURE_RELIABLE_ACK, header);
  for (;;) {
    qa_q3_client_message_part part;
    if (!qa_q3_client_message_reader_next(reader, &part)) { RELEASE(); free(commands); qa_q3_client_message_reader_destroy(reader); return false; }
    if (part.kind == QA_Q3_PART_COMMAND) {
      if (part.command.sequence > last) {
        if (part.command.sequence > last + 1) REJECT(QA_Q3_CLIENT_LOST_RELIABLE_COMMAND, header);
        if (count == cap) {
          size_t nc = cap ? cap * 2 : 8;
          qa_q3_reliable_command *nn = realloc(commands, nc * sizeof *nn);
          if (!nn) { RELEASE(); free(commands); qa_q3_client_message_reader_destroy(reader); return false; }
          commands = nn; cap = nc;
        }
        commands[count++] = part.command;
        last = part.command.sequence;
      }
    } else if (part.kind == QA_Q3_PART_EOF) {
      RELEASE();
      out->kind = QA_Q3_CLIENT_ACCEPTED; out->commands = commands; out->command_count = count; out->last_client_command = last;
      qa_q3_client_message_reader_destroy(reader); return true;
    } else {
      qa_q3_unfiltered_client_movement movement;
      if (!qa_q3_client_message_reader_read_movement(reader, &context->key, &movement)) { RELEASE(); free(commands); qa_q3_client_message_reader_destroy(reader); return false; }
      qa_q3_client_message_reader_validate_terminal(reader);
      RELEASE();
      out->kind = QA_Q3_CLIENT_ACCEPTED; out->commands = commands; out->command_count = count; out->last_client_command = last;
      out->has_movement = true;
      qa_q3_filter_client_movement(&movement, context->last_user_command_time, &out->movement);
      qa_q3_client_message_reader_destroy(reader); return true;
    }
  }
  #undef REJECT
  #undef RELEASE
}
