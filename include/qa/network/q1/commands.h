/* Port of src/network/q1/commands.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_COMMANDS_H
#define QA_NETWORK_Q1_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/profile.h"
typedef struct qa_quakeworld_move {
  qa_qw_user_command oldest, previous, current; double loss_percent;
} qa_quakeworld_move;
typedef struct qa_quakeworld_client_message {
  const char *kind; char *text; double sequence; qa_vec3 origin;
  double percent; uint8_t *bytes; size_t length; qa_quakeworld_move bundle;
} qa_quakeworld_client_message;
void qa_quakeworld_client_message_destroy(qa_quakeworld_client_message *m);
void qa_write_quakeworld_move(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_quakeworld_move *bundle, double sequence);
qa_quakeworld_client_message *qa_decode_quakeworld_client(const uint8_t *bytes, size_t length,
                                                          qa_quakeworld_profile profile, double sequence, size_t *out_count);
void qa_write_quakeworld_upload(qa_size_buf *sb, const uint8_t *bytes, size_t length, double percent);
typedef void (*qa_qw_apply_fn)(void *ctx, const qa_qw_user_command *command);
typedef struct qa_quakeworld_command_replay { qa_qw_user_command last; } qa_quakeworld_command_replay;
void qa_quakeworld_command_replay_init(qa_quakeworld_command_replay *r);
void qa_quakeworld_command_replay_run(qa_quakeworld_command_replay *r, const qa_quakeworld_move *bundle,
                                      double dropped, bool paused, qa_qw_apply_fn apply, void *ctx);
void qa_split_quakeworld_command(const qa_qw_user_command *command, qa_qw_apply_fn apply, void *ctx);
#endif
