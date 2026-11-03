/*
 * Port of src/compat/qvm/client-state.ts (types only).
 * qa_qvmx_wire_user_command and qa_qvmx_source_game_state_record are the
 * guest-record forms of network/q3/message.ts WireUserCommand and
 * network/q3/game-state.ts SourceGameStateRecord: TS numbers (doubles,
 * since a guest record can hold any word) and borrowed sized views. The
 * qa_qvmx_*_from_network converters below are the only crossing between
 * them. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CLIENT_STATE_H
#define QA_COMPAT_QVM_CLIENT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q3/game_state.h"
#include "qa/network/q3/message.h"

typedef struct qa_qvmx_wire_user_command {
  double server_time;
  double angles[3];
  double forwardmove, rightmove, upmove, buttons, weapon;
} qa_qvmx_wire_user_command;

typedef struct qa_qvmx_source_game_state_record {
  const int32_t *string_offsets;
  size_t string_offset_count;
  const uint8_t *string_data;
  size_t string_data_length;
  double data_count;
} qa_qvmx_source_game_state_record;

static inline qa_qvmx_wire_user_command qa_qvmx_wire_user_command_from_network(
    const qa_q3_wire_user_command *command) {
  qa_qvmx_wire_user_command out;
  out.server_time = command->server_time;
  for (int i = 0; i < 3; i++) out.angles[i] = command->angles[i];
  out.forwardmove = command->forwardmove;
  out.rightmove = command->rightmove;
  out.upmove = command->upmove;
  out.buttons = command->buttons;
  out.weapon = command->weapon;
  return out;
}

/* Borrows the record's owned arrays (1024 offsets, 16000 data bytes). */
static inline qa_qvmx_source_game_state_record qa_qvmx_source_game_state_record_from_network(
    const qa_q3_source_game_state_record *record) {
  qa_qvmx_source_game_state_record out;
  out.string_offsets = record->string_offsets;
  out.string_offset_count = 1024;
  out.string_data = record->string_data;
  out.string_data_length = 16000;
  out.data_count = (double)record->data_count;
  return out;
}

typedef struct qa_q3_client_game_state_view {
  void *ctx;
  /* TS get(index): string|null. Borrowed string, NULL for TS null. */
  const char *(*get)(void *ctx, double index);
  qa_qvmx_source_game_state_record (*copy_source_record)(void *ctx);
} qa_q3_client_game_state_view;

typedef struct qa_q3_client_commands_view {
  void *ctx;
  double (*get_current_number)(void *ctx);
  /* TS read(number): WireUserCommand|null. */
  bool (*read)(void *ctx, double number, qa_qvmx_wire_user_command *out);
} qa_q3_client_commands_view;

typedef struct qa_q3_client_state {
  void *ctx;
  double (*get_generation)(void *ctx);
  double (*get_server_message_sequence)(void *ctx);
  double (*get_last_executed_server_command)(void *ctx);
  double (*get_client_number)(void *ctx);
  /* TS snapshotPing(number): number|null. */
  bool (*snapshot_ping)(void *ctx, double number, double *out);
  /* TS getServerCommand(sequence): Promise<readonly string[]|null>,
   * synchronous in C. Borrowed token list, present=false for null. */
  bool (*get_server_command)(void *ctx, double sequence, const char *const **out_tokens,
                             size_t *out_count);
  qa_q3_client_game_state_view game_state;
  qa_q3_client_commands_view commands;
} qa_q3_client_state;

#endif
