/* Port of src/network/q3/reliable.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_RELIABLE_H
#define QA_NETWORK_Q3_RELIABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_Q3_MAX_RELIABLE_COMMANDS 64
/* TS ReliableCommand. text points into ring or out struct storage (valid until next mutation). */
typedef struct qa_q3_reliable_command { int64_t sequence; char text[1024]; } qa_q3_reliable_command;
typedef enum qa_q3_reliable_ack_kind { QA_Q3_ACK_ACKNOWLEDGED, QA_Q3_ACK_CLAMPED, QA_Q3_ACK_REJECTED_STALE } qa_q3_reliable_ack_kind;
typedef struct qa_q3_reliable_acknowledgement { qa_q3_reliable_ack_kind kind; int64_t sequence; } qa_q3_reliable_acknowledgement;
typedef enum qa_q3_server_command_append_kind { QA_Q3_APPEND_QUEUED, QA_Q3_APPEND_OVERFLOW } qa_q3_server_command_append_kind;
typedef struct qa_q3_server_command_append {
  qa_q3_server_command_append_kind kind;
  qa_q3_reliable_command command; int64_t sequence, acknowledge;
} qa_q3_server_command_append;
/* TS ReliableOverflowError outcome for ClientReliableCommands.add: returned, not aborted,
 * so callers can reproduce the throw site. overflow=true carries sequence/acknowledge. */
typedef struct qa_q3_reliable_ring qa_q3_reliable_ring;
typedef qa_q3_reliable_ring qa_q3_client_reliable_commands;
typedef qa_q3_reliable_ring qa_q3_server_reliable_commands;
bool qa_q3_client_reliable_commands_init(qa_q3_client_reliable_commands **out);
bool qa_q3_server_reliable_commands_init(qa_q3_server_reliable_commands **out);
void qa_q3_reliable_ring_destroy(qa_q3_reliable_ring *r);
int64_t qa_q3_reliable_ring_sequence(const qa_q3_reliable_ring *r);
int64_t qa_q3_reliable_ring_acknowledge(const qa_q3_reliable_ring *r);
int64_t qa_q3_reliable_ring_outstanding(const qa_q3_reliable_ring *r);
const char *qa_q3_reliable_ring_lookup(const qa_q3_reliable_ring *r, int64_t sequence);
const char *qa_q3_reliable_ring_lookup_masked(const qa_q3_reliable_ring *r, int64_t sequence);
void qa_q3_reliable_ring_assign_acknowledgement(qa_q3_reliable_ring *r, int64_t sequence);
/* pending(): fills caller array (max), returns count. */
size_t qa_q3_reliable_ring_pending(const qa_q3_reliable_ring *r, qa_q3_reliable_command *out, size_t max);
void qa_q3_client_reliable_commands_change_latest(qa_q3_client_reliable_commands *r);
/* Returns false on ReliableOverflowError (out gets sequence/acknowledge of ring). */
bool qa_q3_client_reliable_commands_add(qa_q3_client_reliable_commands *r, const char *text, qa_q3_reliable_command *out);
qa_q3_reliable_acknowledgement qa_q3_client_reliable_commands_acknowledge_through(qa_q3_client_reliable_commands *r, int64_t sequence);
bool qa_q3_server_reliable_commands_replace_pending(qa_q3_server_reliable_commands *r, int64_t sent, const char *text);
qa_q3_server_command_append qa_q3_server_reliable_commands_add(qa_q3_server_reliable_commands *r, const char *text);
qa_q3_reliable_acknowledgement qa_q3_server_reliable_commands_acknowledge_through(qa_q3_server_reliable_commands *r, int64_t sequence);
#endif
