/* Port of src/network/q3/server-message.ts (cl_parse.c / sv_snapshot.c).
 * GPL-2.0-or-later. Owned payloads freed by the *_destroy helpers. */
#ifndef QA_NETWORK_Q3_SERVER_MESSAGE_H
#define QA_NETWORK_Q3_SERVER_MESSAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/parse_entities.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
#include "qa/network/q3/state_delta.h"
typedef enum qa_q3_server_opcode {
  QA_Q3_SVC_NOP = 1, QA_Q3_SVC_GAMESTATE = 2, QA_Q3_SVC_CONFIGSTRING = 3, QA_Q3_SVC_BASELINE = 4,
  QA_Q3_SVC_COMMAND = 5, QA_Q3_SVC_DOWNLOAD = 6, QA_Q3_SVC_SNAPSHOT = 7, QA_Q3_SVC_EOF = 8
} qa_q3_server_opcode;
typedef enum qa_q3_gamestate_entry_kind { QA_Q3_GS_CONFIGSTRING, QA_Q3_GS_BASELINE } qa_q3_gamestate_entry_kind;
typedef struct qa_q3_gamestate_entry {
  qa_q3_gamestate_entry_kind kind;
  int64_t index; char *value; /* configstring */
  int64_t number; qa_q3_entity_state_record entity; /* baseline */
} qa_q3_gamestate_entry;
typedef struct qa_q3_gamestate {
  int64_t command_sequence;
  qa_q3_gamestate_entry *entries; size_t entry_count;
  int64_t client_number, checksum_feed;
} qa_q3_gamestate;
typedef struct qa_q3_wire_snapshot {
  int64_t message_number, server_time, delta_number, flags, server_command_number, parse_entities_number;
  uint8_t area_mask[32]; size_t area_mask_len;
  qa_q3_player_state_record player_state;
  qa_q3_entity_state_record *entities; size_t entity_count;
} qa_q3_wire_snapshot;
typedef struct qa_q3_snapshot_history_entry {
  bool valid; /* TS status "valid" */
  qa_q3_wire_snapshot snapshot;
} qa_q3_snapshot_history_entry;
typedef enum qa_q3_snapshot_validity_kind { QA_Q3_SNAP_VALID, QA_Q3_SNAP_INVALID } qa_q3_snapshot_validity_kind;
typedef enum qa_q3_snapshot_invalid_reason {
  QA_Q3_SNAP_REASON_NONE, QA_Q3_SNAP_MISSING_DELTA, QA_Q3_SNAP_INVALID_DELTA, QA_Q3_SNAP_STALE_DELTA, QA_Q3_SNAP_STALE_ENTITIES
} qa_q3_snapshot_invalid_reason;
typedef struct qa_q3_snapshot_validity { qa_q3_snapshot_validity_kind kind; qa_q3_snapshot_invalid_reason reason; } qa_q3_snapshot_validity;
typedef enum qa_q3_download_kind { QA_Q3_DOWNLOAD_START, QA_Q3_DOWNLOAD_CHUNK, QA_Q3_DOWNLOAD_ERROR } qa_q3_download_kind;
typedef struct qa_q3_download_block {
  qa_q3_download_kind kind;
  int64_t file_size, number;
  uint8_t *data; size_t data_len;
  char *message;
} qa_q3_download_block;
typedef enum qa_q3_server_operation_kind {
  QA_Q3_OP_NOP, QA_Q3_OP_COMMAND, QA_Q3_OP_GAMESTATE, QA_Q3_OP_DOWNLOAD, QA_Q3_OP_SNAPSHOT
} qa_q3_server_operation_kind;
typedef struct qa_q3_server_operation {
  qa_q3_server_operation_kind kind;
  int64_t sequence; char *text; /* command */
  qa_q3_gamestate gamestate;
  qa_q3_download_block download;
  qa_q3_snapshot_validity validity; qa_q3_wire_snapshot snapshot;
} qa_q3_server_operation;
/* TS ServerMessageContext baseline/history closures -> fn+ctx. baseline returns
 * the live mutable record or NULL (TS object identity preserved). */
typedef qa_q3_entity_state_record *(*qa_q3_baseline_fn)(void *ctx, int64_t number);
typedef const qa_q3_snapshot_history_entry *(*qa_q3_history_fn)(void *ctx, int64_t delta_number);
typedef struct qa_q3_server_message_context {
  qa_q3_product product;
  int64_t message_number, reliable_sequence, server_command_sequence, parse_entities_number;
  qa_q3_baseline_fn baseline; void *baseline_ctx;
  qa_q3_history_fn history; void *history_ctx;
} qa_q3_server_message_context;
typedef enum qa_q3_server_message_terminal { QA_Q3_TERMINAL_EOF, QA_Q3_TERMINAL_DOWNLOAD_ERROR } qa_q3_server_message_terminal;
typedef struct qa_q3_server_message {
  int64_t reliable_acknowledge, server_command_sequence, parse_entities_number;
  qa_q3_server_operation *operations; size_t operation_count;
  qa_q3_server_message_terminal terminal;
} qa_q3_server_message;
/* TS ServerMessageDiagnostics = DeltaMessageDiagnostics minus offset. */
typedef struct qa_q3_server_message_diagnostics {
  qa_q3_delta_shownet_fn shownet; void *shownet_ctx;
  qa_q3_message_print_fn print; void *print_ctx;
} qa_q3_server_message_diagnostics;
typedef enum qa_q3_server_message_step_kind {
  QA_Q3_STEP_ACKNOWLEDGE, QA_Q3_STEP_GAMESTATE_START, QA_Q3_STEP_GAMESTATE_SEQUENCE,
  QA_Q3_STEP_GAMESTATE_ENTRY, QA_Q3_STEP_GAMESTATE_CLIENT, QA_Q3_STEP_GAMESTATE_CHECKSUM,
  QA_Q3_STEP_GAMESTATE_END, QA_Q3_STEP_SNAPSHOT_HEADER, QA_Q3_STEP_OPERATION, QA_Q3_STEP_END
} qa_q3_server_message_step_kind;
typedef struct qa_q3_server_message_step {
  qa_q3_server_message_step_kind kind;
  int64_t sequence, number, checksum, delta_number;
  qa_q3_gamestate_entry entry;
  qa_q3_server_operation operation;
  qa_q3_server_message_terminal terminal;
} qa_q3_server_message_step;
typedef int64_t (*qa_q3_publish_download_size_fn)(void *ctx, int64_t file_size);
typedef struct qa_q3_server_message_cursor qa_q3_server_message_cursor;
bool qa_q3_server_message_cursor_init(qa_q3_server_message_cursor **out, const uint8_t *bytes, size_t len,
                                      const char *source, const qa_q3_server_message_diagnostics *diagnostics,
                                      int64_t read_offset, qa_q3_source_parse_entities *parse_entities,
                                      qa_q3_publish_download_size_fn publish_download_size, void *publish_ctx);
void qa_q3_server_message_cursor_destroy(qa_q3_server_message_cursor *c);
bool qa_q3_server_message_cursor_next(qa_q3_server_message_cursor *c, const qa_q3_server_message_context *context, qa_q3_server_message_step *out);
bool qa_q3_decode_server_message(const uint8_t *bytes, size_t len, const qa_q3_server_message_context *context,
                                 const char *source, qa_q3_server_message *out);
void qa_q3_write_server_message(qa_q3_message_writer *w, int64_t reliable_acknowledge,
                                const qa_q3_server_operation *operations, size_t operation_count,
                                const qa_q3_server_message_context *context);
bool qa_q3_encode_server_message(int64_t reliable_acknowledge, const qa_q3_server_operation *operations, size_t operation_count,
                                 const qa_q3_server_message_context *context, qa_q3_source_message_state *source_state,
                                 uint8_t **out, size_t *out_len);
void qa_q3_wire_snapshot_destroy(qa_q3_wire_snapshot *s);
void qa_q3_gamestate_destroy(qa_q3_gamestate *g);
void qa_q3_gamestate_entry_destroy(qa_q3_gamestate_entry *e);
void qa_q3_download_block_destroy(qa_q3_download_block *d);
void qa_q3_server_operation_destroy(qa_q3_server_operation *o);
void qa_q3_server_message_destroy(qa_q3_server_message *m);
void qa_q3_server_message_step_destroy(qa_q3_server_message_step *s);
bool qa_q3_wire_snapshot_copy(qa_q3_wire_snapshot *dst, const qa_q3_wire_snapshot *src);
#endif
