/* Port of src/network/q1/channels.ts. Returned datagrams/payloads are
 * malloc'd, caller frees. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CHANNELS_H
#define QA_NETWORK_Q1_CHANNELS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/reliability.h"
#include "qa/network/common/scheduling.h"
#include "qa/network/common/transport.h"
#define QA_NETFLAG_DATA 0x10000u
#define QA_NETFLAG_ACK 0x20000u
#define QA_NETFLAG_EOM 0x80000u
#define QA_NETFLAG_UNRELIABLE 0x100000u
#define QA_NETFLAG_CTL 0x80000000u
typedef struct qa_channel_delivery {
  bool reliable; uint8_t *payload; size_t length; double sequence, dropped;
} qa_channel_delivery;
typedef struct qa_netquake_channel {
  qa_stop_and_wait reliable; double max_message_bytes;
  double unreliable_send, unreliable_receive; /* TS numbers: never wrap at 2^32 */
} qa_netquake_channel;
int qa_netquake_channel_init(qa_netquake_channel *c, double max_message_bytes, double fragment_bytes);
void qa_netquake_channel_destroy(qa_netquake_channel *c);
bool qa_netquake_channel_can_send_reliable(const qa_netquake_channel *c);
void qa_netquake_channel_queue_reliable(qa_netquake_channel *c, const uint8_t *payload, size_t length);
uint8_t *qa_netquake_channel_next(qa_netquake_channel *c, double now_ms, size_t *out_len);
uint8_t *qa_netquake_channel_unreliable(qa_netquake_channel *c, const uint8_t *payload, size_t length, size_t *out_len);
/* Replies: up to 1 datagram in replies[0] (malloc'd) with *reply_count. */
int qa_netquake_channel_receive(qa_netquake_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                qa_channel_delivery *delivery, uint8_t **reply, size_t *reply_len);

typedef struct qa_quakeworld_delivery {
  uint8_t *payload; size_t length; double sequence, acknowledged, dropped;
} qa_quakeworld_delivery;
typedef struct qa_quakeworld_channel {
  qa_toggle_channel reliable; qa_packet_rate packet_rate;
  bool is_client; double qport, max_message_bytes, bytes_per_second;
  double last_received, frame_latency, frame_rate;
} qa_quakeworld_channel;
int qa_quakeworld_channel_init(qa_quakeworld_channel *c, bool is_client, double qport, double max_message_bytes, double bytes_per_second);
void qa_quakeworld_channel_destroy(qa_quakeworld_channel *c);
double qa_quakeworld_channel_incoming_sequence(const qa_quakeworld_channel *c);
double qa_quakeworld_channel_outgoing_sequence(const qa_quakeworld_channel *c);
bool qa_quakeworld_channel_has_reliable(const qa_quakeworld_channel *c);
void qa_quakeworld_channel_queue_reliable(qa_quakeworld_channel *c, const uint8_t *bytes, size_t length);
bool qa_quakeworld_channel_can_packet(qa_quakeworld_channel *c, double now_ms);
uint8_t *qa_quakeworld_channel_transmit(qa_quakeworld_channel *c, const uint8_t *unreliable, size_t length,
                                        double now_ms, bool server_paused, size_t *out_len);
int qa_quakeworld_channel_receive(qa_quakeworld_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                  qa_quakeworld_delivery *out);

#endif
