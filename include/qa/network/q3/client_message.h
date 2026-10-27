/* Port of src/network/q3/client-message.ts (CL_WritePacket / SV_UserMove).
 * GPL-2.0-or-later. InvalidClientOpcodeError/InvalidClientCommandCountError
 * abort via qa_binary_error with the TS message. */
#ifndef QA_NETWORK_Q3_CLIENT_MESSAGE_H
#define QA_NETWORK_Q3_CLIENT_MESSAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/reliable.h"
typedef enum qa_q3_client_opcode { QA_Q3_CLC_NOP = 1, QA_Q3_CLC_MOVE = 2, QA_Q3_CLC_MOVE_NO_DELTA = 3, QA_Q3_CLC_COMMAND = 4, QA_Q3_CLC_EOF = 5 } qa_q3_client_opcode;
#define QA_Q3_MAX_PACKET_USER_COMMANDS 32
typedef struct qa_q3_client_header { int64_t server_id, message_acknowledge, reliable_acknowledge; } qa_q3_client_header;
typedef enum qa_q3_client_movement_kind { QA_Q3_MOVE, QA_Q3_MOVE_NO_DELTA } qa_q3_client_movement_kind;
typedef struct qa_q3_client_movement {
  qa_q3_client_movement_kind kind;
  qa_q3_wire_user_command commands[QA_Q3_MAX_PACKET_USER_COMMANDS]; size_t command_count;
} qa_q3_client_movement;
typedef struct qa_q3_client_message {
  qa_q3_client_header header;
  const qa_q3_reliable_command *commands; size_t command_count;
  const qa_q3_client_movement *movement; /* NULL */
} qa_q3_client_message;
typedef const char *(*qa_q3_client_server_command_fn)(void *ctx, int64_t reliable_acknowledge);
/* TS ClientKeyContext. */
typedef struct qa_q3_client_key_context {
  int64_t checksum_feed;
  qa_q3_client_server_command_fn server_command; void *server_command_ctx;
} qa_q3_client_key_context;
typedef struct qa_q3_client_decode_context {
  qa_q3_client_key_context key;
  int64_t reliable_sequence, last_client_command, last_user_command_time;
} qa_q3_client_decode_context;
typedef struct qa_q3_unfiltered_client_movement {
  qa_q3_client_movement base; int64_t delta_message;
} qa_q3_unfiltered_client_movement;
typedef struct qa_q3_decoded_client_movement {
  qa_q3_client_movement base; int64_t delta_message;
  qa_q3_wire_user_command executable_commands[QA_Q3_MAX_PACKET_USER_COMMANDS]; size_t executable_count;
  int64_t last_user_command_time;
} qa_q3_decoded_client_movement;
typedef enum qa_q3_client_message_part_kind { QA_Q3_PART_COMMAND, QA_Q3_PART_MOVEMENT, QA_Q3_PART_EOF } qa_q3_client_message_part_kind;
typedef struct qa_q3_client_message_part {
  qa_q3_client_message_part_kind kind;
  qa_q3_reliable_command command;
  qa_q3_client_movement_kind movement_kind; int64_t delta_message;
} qa_q3_client_message_part;
typedef enum qa_q3_decoded_client_message_kind { QA_Q3_CLIENT_ACCEPTED, QA_Q3_CLIENT_REJECTED } qa_q3_decoded_client_message_kind;
typedef enum qa_q3_client_reject_reason {
  QA_Q3_CLIENT_REJECT_NONE, QA_Q3_CLIENT_NEGATIVE_MESSAGE_ACK, QA_Q3_CLIENT_NEGATIVE_RELIABLE_ACK,
  QA_Q3_CLIENT_STALE_RELIABLE_ACK, QA_Q3_CLIENT_FUTURE_RELIABLE_ACK, QA_Q3_CLIENT_LOST_RELIABLE_COMMAND
} qa_q3_client_reject_reason;
typedef struct qa_q3_decoded_client_message {
  qa_q3_decoded_client_message_kind kind;
  qa_q3_client_header header;
  qa_q3_reliable_command *commands; size_t command_count;
  int64_t last_client_command;
  qa_q3_client_reject_reason reason;
  bool has_movement; qa_q3_decoded_client_movement movement;
} qa_q3_decoded_client_message;
int64_t qa_q3_command_hash(const char *text, int64_t max_length);
int64_t qa_q3_command_hash_default(const char *text);
bool qa_q3_begin_client_message(const qa_q3_client_header *header, const qa_q3_reliable_command *commands, size_t count,
                                qa_q3_source_message_state *source_state, qa_q3_message_writer **out);
void qa_q3_write_client_movement(qa_q3_message_writer *w, const qa_q3_client_movement *movement,
                                 const qa_q3_client_header *header, const qa_q3_client_key_context *context);
bool qa_q3_finish_client_message(qa_q3_message_writer *w, uint8_t **out, size_t *out_len);
bool qa_q3_encode_client_message(const qa_q3_client_message *message, const qa_q3_client_key_context *context,
                                 qa_q3_source_message_state *source_state, uint8_t **out, size_t *out_len);
typedef struct qa_q3_client_message_reader qa_q3_client_message_reader;
bool qa_q3_client_message_reader_init(qa_q3_client_message_reader **out, const uint8_t *bytes, size_t len, const char *source);
void qa_q3_client_message_reader_destroy(qa_q3_client_message_reader *r);
/* TS prefix (serverId/messageAcknowledge read in constructor). */
void qa_q3_client_message_reader_prefix(const qa_q3_client_message_reader *r, int64_t *server_id, int64_t *message_acknowledge);
int64_t qa_q3_client_message_reader_read_count(const qa_q3_client_message_reader *r);
qa_q3_client_header qa_q3_client_message_reader_read_header(qa_q3_client_message_reader *r);
bool qa_q3_client_message_reader_next(qa_q3_client_message_reader *r, qa_q3_client_message_part *out);
bool qa_q3_client_message_reader_read_movement(qa_q3_client_message_reader *r, const qa_q3_client_key_context *context, qa_q3_unfiltered_client_movement *out);
void qa_q3_client_message_reader_validate_terminal(qa_q3_client_message_reader *r);
void qa_q3_filter_client_movement(const qa_q3_unfiltered_client_movement *movement, int64_t last_user_command_time, qa_q3_decoded_client_movement *out);
bool qa_q3_decode_client_message(const uint8_t *bytes, size_t len, const qa_q3_client_decode_context *context,
                                 const char *source, qa_q3_decoded_client_message *out);
void qa_q3_decoded_client_message_destroy(qa_q3_decoded_client_message *m);
#endif
