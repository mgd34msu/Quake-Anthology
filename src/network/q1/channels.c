/* Port of src/network/q1/channels.ts. GPL-2.0-or-later. */
#include "qa/network/q1/channels.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/message.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#define MAX_QUAKEWORLD_SEQUENCE_JUMP 0x10000
static uint8_t *nq_packet(uint32_t flags, uint32_t sequence, const uint8_t *payload, size_t length, size_t *out_len) {
  if (length > 65527) qa_range_error("NetQuake datagram exceeds 16-bit header");
  size_t n = length + 8;
  uint8_t *b = malloc(n); if (!b) return NULL;
  uint32_t w = flags | (uint32_t)n;
  b[0]=(uint8_t)(w>>24); b[1]=(uint8_t)(w>>16); b[2]=(uint8_t)(w>>8); b[3]=(uint8_t)w;
  b[4]=(uint8_t)(sequence>>24); b[5]=(uint8_t)(sequence>>16); b[6]=(uint8_t)(sequence>>8); b[7]=(uint8_t)sequence;
  memcpy(b + 8, payload, length); *out_len = n; return b;
}
/* Number.isSafeInteger before the cast: 1.5 or NaN must fail the TS check, not truncate.
 * A non-integer maps to -1, which the channel rejects with the TS message. */
static long long safe_or_invalid(double v) {
  return isfinite(v) && v == trunc(v) && fabs(v) <= 9007199254740991.0 ? (long long)v : -1;
}
int qa_netquake_channel_init(qa_netquake_channel *c, double max_message_bytes, double fragment_bytes) {
  memset(c, 0, sizeof *c); c->max_message_bytes = max_message_bytes;
  return qa_stop_and_wait_init(&c->reliable, safe_or_invalid(max_message_bytes), safe_or_invalid(fragment_bytes), 1000);
}
void qa_netquake_channel_destroy(qa_netquake_channel *c) { qa_stop_and_wait_destroy(&c->reliable); }
bool qa_netquake_channel_can_send_reliable(const qa_netquake_channel *c) { return qa_stop_and_wait_can_send(&c->reliable); }
void qa_netquake_channel_queue_reliable(qa_netquake_channel *c, const uint8_t *payload, size_t length) {
  qa_stop_and_wait_begin(&c->reliable, payload, length);
}
uint8_t *qa_netquake_channel_next(qa_netquake_channel *c, double now_ms, size_t *out_len) {
  qa_reliable_fragment p;
  if (!qa_stop_and_wait_next(&c->reliable, now_ms, &p)) return NULL;
  uint8_t *b = nq_packet(QA_NETFLAG_DATA | (p.final ? QA_NETFLAG_EOM : 0), p.sequence, p.payload, p.length, out_len);
  free(p.payload); return b;
}
uint8_t *qa_netquake_channel_unreliable(qa_netquake_channel *c, const uint8_t *payload, size_t length, size_t *out_len) {
  if ((double)length > c->max_message_bytes) qa_range_error("NetQuake unreliable message exceeds profile");
  /* DataView.setUint32 stores the counter modulo 2^32. */
  uint32_t sequence = (uint32_t)fmod(c->unreliable_send++, 4294967296.0);
  return nq_packet(QA_NETFLAG_UNRELIABLE, sequence, payload, length, out_len);
}
int qa_netquake_channel_receive(qa_netquake_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                qa_channel_delivery *delivery, uint8_t **reply, size_t *reply_len) {
  *reply = NULL; *reply_len = 0; memset(delivery, 0, sizeof *delivery);
  if (length < 8) qa_q1_packet_error("Short NetQuake header", 0);
  uint32_t word = ((uint32_t)bytes[0]<<24)|((uint32_t)bytes[1]<<16)|((uint32_t)bytes[2]<<8)|bytes[3];
  uint32_t flags = word & 0xffff0000u;
  uint32_t sequence = ((uint32_t)bytes[4]<<24)|((uint32_t)bytes[5]<<16)|((uint32_t)bytes[6]<<8)|bytes[7];
  if ((word & 65535u) != (uint32_t)length) qa_q1_packet_error("NetQuake length mismatch", 0);
  if (flags & QA_NETFLAG_CTL) return 0;
  const uint8_t *payload = bytes + 8; size_t plen = length - 8;
  if (flags & QA_NETFLAG_UNRELIABLE) {
    if ((double)sequence < c->unreliable_receive) return 0;
    delivery->dropped = (double)sequence - c->unreliable_receive;
    c->unreliable_receive = (double)sequence + 1;
    delivery->reliable = false; delivery->sequence = sequence; delivery->length = plen;
    delivery->payload = malloc(plen ? plen : 1); if (plen) memcpy(delivery->payload, payload, plen);
    return 1;
  }
  if (flags & QA_NETFLAG_ACK) {
    (void)qa_stop_and_wait_acknowledge(&c->reliable, sequence);
    *reply = qa_netquake_channel_next(c, now_ms, reply_len);
    return 0;
  }
  if (flags & QA_NETFLAG_DATA) {
    qa_reliable_fragment f; f.sequence = sequence; f.final = (flags & QA_NETFLAG_EOM) != 0;
    f.payload = (uint8_t *)payload; f.length = plen;
    qa_reliable_fragment_result res = qa_stop_and_wait_receive(&c->reliable, &f);
    static const uint8_t empty = 0;
    *reply = nq_packet(QA_NETFLAG_ACK, res.acknowledge, &empty, 0, reply_len);
    if (res.kind && strcmp(res.kind, "message") == 0) {
      delivery->reliable = true; delivery->payload = res.payload; delivery->length = res.length;
      delivery->sequence = sequence; delivery->dropped = 0; return 1;
    }
    return 0;
  }
  qa_q1_packet_error("Unknown NetQuake packet flags", 0);
}
int qa_quakeworld_channel_init(qa_quakeworld_channel *c, bool is_client, double qport, double max_message_bytes, double bytes_per_second) {
  if (qport != trunc(qport) || qport < 0 || qport > 65535) qa_range_error("Invalid qport");
  memset(c, 0, sizeof *c);
  c->is_client = is_client; c->qport = qport; c->max_message_bytes = max_message_bytes; c->bytes_per_second = bytes_per_second;
  qa_toggle_channel_init(&c->reliable, safe_or_invalid(max_message_bytes), 0);
  qa_packet_rate_init(&c->packet_rate, bytes_per_second, 200);
  return 1;
}
void qa_quakeworld_channel_destroy(qa_quakeworld_channel *c) { qa_toggle_channel_destroy(&c->reliable); }
double qa_quakeworld_channel_incoming_sequence(const qa_quakeworld_channel *c) { return (double)qa_toggle_channel_incoming_sequence(&c->reliable); }
double qa_quakeworld_channel_outgoing_sequence(const qa_quakeworld_channel *c) { return (double)qa_toggle_channel_outgoing_sequence(&c->reliable); }
bool qa_quakeworld_channel_has_reliable(const qa_quakeworld_channel *c) { return qa_toggle_channel_has_pending_reliable(&c->reliable); }
void qa_quakeworld_channel_queue_reliable(qa_quakeworld_channel *c, const uint8_t *bytes, size_t length) { qa_toggle_channel_queue(&c->reliable, bytes, length); }
bool qa_quakeworld_channel_can_packet(qa_quakeworld_channel *c, double now_ms) {
  c->packet_rate.bytes_per_second = c->bytes_per_second;
  return qa_packet_rate_can_send(&c->packet_rate, now_ms, false);
}
uint8_t *qa_quakeworld_channel_transmit(qa_quakeworld_channel *c, const uint8_t *unreliable, size_t length,
                                        double now_ms, bool server_paused, size_t *out_len) {
  qa_toggle_packet p;
  qa_toggle_channel_transmit(&c->reliable, unreliable, length, -1, &p);
  size_t header = c->is_client ? 10 : 8;
  size_t n = header + p.length;
  uint8_t *b = malloc(n); if (!b) { free(p.payload); return NULL; }
  uint32_t w0 = (uint32_t)p.sequence | (p.reliable ? 0x80000000u : 0);
  uint32_t w1 = (uint32_t)p.acknowledged | (p.reliable_acknowledged ? 0x80000000u : 0);
  b[0]=(uint8_t)w0; b[1]=(uint8_t)(w0>>8); b[2]=(uint8_t)(w0>>16); b[3]=(uint8_t)(w0>>24);
  b[4]=(uint8_t)w1; b[5]=(uint8_t)(w1>>8); b[6]=(uint8_t)(w1>>16); b[7]=(uint8_t)(w1>>24);
  if (c->is_client) { uint16_t qp = qa_js_to_uint16(c->qport); /* validated 0..65535 */ b[8]=(uint8_t)qp; b[9]=(uint8_t)(qp>>8); }
  memcpy(b + header, p.payload, p.length); free(p.payload);
  c->packet_rate.bytes_per_second = c->bytes_per_second;
  qa_packet_rate_sent(&c->packet_rate, (double)n, now_ms, !c->is_client && server_paused);
  *out_len = n; return b;
}
int qa_quakeworld_channel_receive(qa_quakeworld_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                  qa_quakeworld_delivery *out) {
  size_t header = !c->is_client ? 10 : 8;
  if (length < header) qa_q1_packet_error("Short QuakeWorld header", 0);
  if (!c->is_client) {
    uint16_t qp = (uint16_t)((uint16_t)bytes[8] | ((uint16_t)bytes[9] << 8));
    if ((double)qp != c->qport) return 0;
  }
  uint32_t word = (uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
  uint32_t ack = (uint32_t)bytes[4]|((uint32_t)bytes[5]<<8)|((uint32_t)bytes[6]<<16)|((uint32_t)bytes[7]<<24);
  if (word == 0xffffffffu) return 0;
  /* The server copies accepted sequences into its own outgoing counter, so a peer
   * must not be able to jump it to the end of the 31-bit space. */
  if (!c->is_client && (double)(word & 0x7fffffffu) - qa_quakeworld_channel_incoming_sequence(c) > MAX_QUAKEWORLD_SEQUENCE_JUMP)
    return 0;
  qa_toggle_packet p; memset(&p, 0, sizeof p);
  p.sequence = word & 0x7fffffffu; p.acknowledged = ack & 0x7fffffffu;
  p.reliable = (word >> 31) != 0; p.reliable_acknowledged = (ack >> 31) == 0 ? 0 : 1;
  p.payload = (uint8_t *)bytes + header; p.length = length - header;
  qa_toggle_receive_result res = qa_toggle_channel_receive(&c->reliable, &p);
  if (!res.accepted) return 0;
  double acknowledged = (double)p.acknowledged, sequence = (double)p.sequence;
  c->frame_latency = c->frame_latency * 0.99 + (qa_quakeworld_channel_outgoing_sequence(c) - acknowledged) * 0.01;
  c->frame_rate = c->frame_rate * 0.99 + (now_ms - c->last_received) * 0.01;
  c->last_received = now_ms;
  if (!c->is_client && sequence >= qa_quakeworld_channel_outgoing_sequence(c))
    qa_toggle_channel_advance_outgoing(&c->reliable, (long long)sequence);
  out->payload = res.payload; out->length = res.length;
  out->sequence = sequence; out->acknowledged = acknowledged; out->dropped = (double)res.dropped;
  return 1;
}
