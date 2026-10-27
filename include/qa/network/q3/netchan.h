/* Port of src/network/q3/netchan.ts (net_chan.c). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_NETCHAN_H
#define QA_NETWORK_Q3_NETCHAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#define QA_Q3_FRAGMENT_SIZE 1300
#define QA_Q3_MAX_PACKET_LENGTH 1400
typedef enum qa_q3_channel_role { QA_Q3_CHANNEL_CLIENT, QA_Q3_CHANNEL_SERVER } qa_q3_channel_role;
typedef void (*qa_q3_channel_send_fn)(void *ctx, const uint8_t *packet, size_t len);
typedef int64_t (*qa_q3_transmit_qport_fn)(void *ctx);
/* TS ChannelDelivery. */
typedef struct qa_q3_channel_delivery {
  qa_q3_source_message_state *source_state; /* may be NULL */
  qa_q3_channel_send_fn send; void *send_ctx;
  qa_q3_message_print_fn trace; void *trace_ctx;
} qa_q3_channel_delivery;
/* TS ChannelDiagnostics. */
typedef struct qa_q3_channel_diagnostics {
  bool show_packets, show_drop;
  const char *remote_address;
  qa_q3_message_print_fn print; void *print_ctx;
} qa_q3_channel_diagnostics;
typedef enum qa_q3_channel_result_kind { QA_Q3_CHANNEL_ACCEPTED, QA_Q3_CHANNEL_FRAGMENT, QA_Q3_CHANNEL_REJECTED } qa_q3_channel_result_kind;
typedef enum qa_q3_channel_reject_reason { QA_Q3_REJECT_NONE, QA_Q3_REJECT_MALFORMED, QA_Q3_REJECT_SEQUENCE, QA_Q3_REJECT_FRAGMENT_ORDER, QA_Q3_REJECT_FRAGMENT_LENGTH } qa_q3_channel_reject_reason;
/* TS ChannelResult. payload owned (free) when accepted. */
typedef struct qa_q3_channel_result {
  qa_q3_channel_result_kind kind;
  qa_q3_channel_reject_reason reason;
  int64_t sequence, qport, dropped, received; /* qport -1 = null */
  uint8_t *payload; size_t payload_len;
} qa_q3_channel_result;
typedef struct qa_q3_netchannel qa_q3_netchannel;
bool qa_q3_netchannel_init(qa_q3_netchannel **out, qa_q3_channel_role role, int64_t qport,
                           qa_q3_transmit_qport_fn transmit_qport, void *transmit_qport_ctx);
bool qa_q3_netchannel_source_zero(qa_q3_netchannel **out);
void qa_q3_netchannel_destroy(qa_q3_netchannel *c);
int64_t qa_q3_netchannel_incoming_sequence(const qa_q3_netchannel *c);
int64_t qa_q3_netchannel_outgoing_sequence(const qa_q3_netchannel *c);
bool qa_q3_netchannel_has_unsent_fragments(const qa_q3_netchannel *c);
int64_t qa_q3_netchannel_remaining_unsent_bytes(const qa_q3_netchannel *c);
qa_q3_channel_role qa_q3_netchannel_get_role(const qa_q3_netchannel *c);
int64_t qa_q3_netchannel_get_qport(const qa_q3_netchannel *c);
void qa_q3_netchannel_begin_transmit(qa_q3_netchannel *c, const uint8_t *payload, size_t len, const qa_q3_channel_delivery *delivery);
bool qa_q3_netchannel_transmit_next_fragment(qa_q3_netchannel *c, const qa_q3_channel_delivery *delivery);
/* TS transmit(): collects packets. out_packets is array of owned buffers + lengths (caller frees each + arrays). */
bool qa_q3_netchannel_transmit(qa_q3_netchannel *c, const uint8_t *payload, size_t len,
                               uint8_t ***out_packets, size_t **out_lengths, size_t *out_count);
bool qa_q3_netchannel_receive(qa_q3_netchannel *c, const uint8_t *packet, size_t len,
                              const qa_q3_channel_diagnostics *diagnostics, qa_q3_channel_result *out);
typedef const char *(*qa_q3_server_command_fn)(void *ctx, int64_t reliable_acknowledge);
bool qa_q3_xor_client_message(const uint8_t *payload, size_t len, int64_t challenge,
                              qa_q3_server_command_fn server_command, void *ctx, uint8_t **out);
bool qa_q3_xor_server_message(const uint8_t *payload, size_t len, int64_t challenge, int64_t sequence,
                              const char *client_command, uint8_t **out);
#endif
