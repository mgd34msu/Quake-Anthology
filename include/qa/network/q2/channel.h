/*
 * Port of src/network/q2/channel.ts (Q2Channel). Transport send uses
 * the landed common transport vtable; returned packets are malloc'd.
 * Options absence is expressed with has_* flags (TS ?? defaults).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_CHANNEL_H
#define QA_NETWORK_Q2_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/common/transport.h"

/* TS Q2ChannelOptions. side_server false is "client";
 * sequence_q2pro false is "id". */
typedef struct qa_q2_channel_options {
  bool side_server;
  qa_q2_protocol_identity protocol;
  bool channel_new;
  double qport;
  bool has_payload_bytes;
  double payload_bytes;
  bool has_message_bytes;
  double message_bytes;
  bool has_max_datagram_bytes;
  double max_datagram_bytes;
  bool compress;
  bool has_sequence_recording;
  bool sequence_q2pro;
} qa_q2_channel_options;

/* TS Q2ChannelReceive. reason is the exact TS literal. */
typedef enum qa_q2_channel_receive_kind {
  QA_Q2_CHANNEL_MESSAGE, QA_Q2_CHANNEL_FRAGMENT, QA_Q2_CHANNEL_REJECTED
} qa_q2_channel_receive_kind;
typedef struct qa_q2_channel_receive {
  qa_q2_channel_receive_kind kind;
  double sequence;
  double acknowledged;
  double dropped;
  double received_bytes;
  const char *reason;
  uint8_t *bytes; /* owned */
  size_t bytes_length;
} qa_q2_channel_receive_result;

void qa_q2_channel_receive_result_free(qa_q2_channel_receive_result *receive);

/* TS Q2Channel. */
typedef struct qa_q2_channel {
  qa_q2_channel_options options;
  double payload_bytes;
  double capacity;
  double last_sent_milliseconds;
  double last_received_milliseconds;
  int64_t incoming, outgoing, incoming_ack, incoming_reliable;
  int64_t incoming_reliable_ack, reliable_bit, last_reliable;
  bool ack_pending;
  uint8_t *queued;
  size_t queued_length;
  uint8_t *reliable;
  size_t reliable_length;
  bool has_sending;
  uint8_t *sending_bytes;
  size_t sending_length;
  bool sending_reliable;
  size_t sending_offset;
  uint8_t receiving[65527 > 0 ? 65527 : 1];
  int64_t receive_sequence;
  size_t receive_length;
  size_t receiving_capacity;
} qa_q2_channel;

/* RangeError cases abort with the TS messages. */
void qa_q2_channel_init(qa_q2_channel *channel, const qa_q2_channel_options *options);
void qa_q2_channel_destroy(qa_q2_channel *channel);
/* TS getters (get_ infix: names must differ from types). */
int64_t qa_q2_channel_get_incoming_sequence(const qa_q2_channel *channel);
int64_t qa_q2_channel_get_incoming_acknowledged(const qa_q2_channel *channel);
int64_t qa_q2_channel_get_outgoing_sequence(const qa_q2_channel *channel);
bool qa_q2_channel_get_fragment_pending(const qa_q2_channel *channel);
bool qa_q2_channel_get_reliable_pending(const qa_q2_channel *channel);
bool qa_q2_channel_get_acknowledgement_pending(const qa_q2_channel *channel);
bool qa_q2_channel_get_can_reliable(const qa_q2_channel *channel);
bool qa_q2_channel_should_update(const qa_q2_channel *channel, double now_milliseconds);
void qa_q2_channel_queue_reliable(qa_q2_channel *channel, const uint8_t *bytes,
                                  size_t length);
/* TS nextFragment / transmit: malloc'd packet or NULL (nextFragment
 * with nothing pending). receive fills a result; false only on OOM. */
uint8_t *qa_q2_channel_next_fragment(qa_q2_channel *channel, double now_milliseconds,
                                     size_t *out_length);
uint8_t *qa_q2_channel_transmit(qa_q2_channel *channel, const uint8_t *unreliable,
                                size_t length, double now_milliseconds,
                                size_t *out_length);
bool qa_q2_channel_receive(qa_q2_channel *channel, const uint8_t *bytes, size_t length,
                           double now_milliseconds, qa_q2_channel_receive_result *out);
/* TS send(transport, to, unreliable, now). */
bool qa_q2_channel_send(qa_q2_channel *channel, qa_datagram_transport *transport,
                        const qa_net_address *to, const uint8_t *unreliable,
                        size_t length, double now_milliseconds);

#endif
