/* Port of src/network/q3/netchan.ts. GPL-2.0-or-later. */
#include "qa/network/q3/netchan.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct qa_q3_netchannel {
  qa_q3_channel_role role; int64_t qport;
  qa_q3_transmit_qport_fn transmit_qport; void *tq_ctx;
  int64_t incoming, outgoing, dropped, fragment_sequence, fragment_length;
  uint8_t fragments[QA_Q3_MAX_MESSAGE_LENGTH];
  uint8_t unsent_buffer[QA_Q3_MAX_MESSAGE_LENGTH];
  int64_t unsent_length, unsent_start; bool unsent_fragments, delivering;
};
bool qa_q3_netchannel_init(qa_q3_netchannel **out, qa_q3_channel_role role, int64_t qport,
                           qa_q3_transmit_qport_fn transmit_qport, void *ctx) {
  if (qport < 0 || qport > 65535) qa_range_error("qport must be an unsigned short");
  qa_q3_netchannel *c = calloc(1, sizeof *c);
  if (!c) return false;
  c->role = role; c->qport = qport; c->transmit_qport = transmit_qport; c->tq_ctx = ctx;
  c->outgoing = 1;
  *out = c; return true;
}
bool qa_q3_netchannel_source_zero(qa_q3_netchannel **out) {
  if (!qa_q3_netchannel_init(out, QA_Q3_CHANNEL_CLIENT, 0, NULL, NULL)) return false;
  (*out)->outgoing = 0; return true;
}
void qa_q3_netchannel_destroy(qa_q3_netchannel *c) { free(c); }
int64_t qa_q3_netchannel_incoming_sequence(const qa_q3_netchannel *c) { return c->incoming; }
int64_t qa_q3_netchannel_outgoing_sequence(const qa_q3_netchannel *c) { return c->outgoing; }
bool qa_q3_netchannel_has_unsent_fragments(const qa_q3_netchannel *c) { return c->unsent_fragments; }
int64_t qa_q3_netchannel_remaining_unsent_bytes(const qa_q3_netchannel *c) { return c->unsent_length - c->unsent_start; }
qa_q3_channel_role qa_q3_netchannel_get_role(const qa_q3_netchannel *c) { return c->role; }
int64_t qa_q3_netchannel_get_qport(const qa_q3_netchannel *c) { return c->qport; }
static const char *role_name(qa_q3_channel_role r) { return r == QA_Q3_CHANNEL_CLIENT ? "client" : "server"; }
static bool make_packet(qa_q3_netchannel *c, const uint8_t *payload, size_t len, bool fragmented, int64_t start,
                        qa_q3_source_message_state *ss, uint8_t **out, size_t *out_len) {
  qa_q3_message_writer *m = NULL;
  if (!qa_q3_message_writer_init(&m, QA_Q3_MESSAGE_OOB, QA_Q3_MAX_PACKET_LENGTH, ss)) return false;
  qa_q3_message_writer_write_long(m, (int32_t)((uint32_t)c->outgoing | (fragmented ? 0x80000000u : 0u)));
  if (c->role == QA_Q3_CHANNEL_CLIENT)
    qa_q3_message_writer_write_short(m, c->transmit_qport ? c->transmit_qport(c->tq_ctx) : c->qport);
  if (fragmented) { qa_q3_message_writer_write_short(m, start); qa_q3_message_writer_write_short(m, (int64_t)len); }
  qa_q3_message_writer_write_data(m, payload, len);
  bool ok = qa_q3_message_writer_to_bytes(m, out, out_len);
  qa_q3_message_writer_destroy(m);
  return ok;
}
static void assert_entry(qa_q3_netchannel *c) { if (c->delivering) qa_range_error("Cannot reenter channel delivery"); }
void qa_q3_netchannel_begin_transmit(qa_q3_netchannel *c, const uint8_t *payload, size_t len, const qa_q3_channel_delivery *delivery) {
  assert_entry(c);
  if (len > QA_Q3_MAX_MESSAGE_LENGTH) qa_range_error("Netchannel message too large");
  if (c->outgoing > 0x7fffffff) qa_range_error("Netchannel sequence exhausted; reconnect required");
  c->unsent_start = 0;
  if (len >= QA_Q3_FRAGMENT_SIZE) {
    c->unsent_fragments = true; c->unsent_length = (int64_t)len;
    memcpy(c->unsent_buffer, payload, len);
    qa_q3_netchannel_transmit_next_fragment(c, delivery);
    return;
  }
  uint8_t *packet = NULL; size_t plen = 0;
  if (!make_packet(c, payload, len, false, 0, delivery->source_state, &packet, &plen)) qa_range_error("Netchannel allocation failed");
  c->outgoing++;
  c->delivering = true;
  delivery->send(delivery->send_ctx, packet, plen);
  if (delivery->trace) {
    char buf[128]; snprintf(buf, sizeof buf, "%s send %4zu : s=%lld ack=%lld\n",
      role_name(c->role), plen, (long long)(c->outgoing - 1), (long long)c->incoming);
    delivery->trace(delivery->trace_ctx, buf);
  }
  c->delivering = false;
  free(packet);
}
bool qa_q3_netchannel_transmit_next_fragment(qa_q3_netchannel *c, const qa_q3_channel_delivery *delivery) {
  assert_entry(c);
  int64_t length = c->unsent_length - c->unsent_start;
  if (length > QA_Q3_FRAGMENT_SIZE) length = QA_Q3_FRAGMENT_SIZE;
  uint8_t *packet = NULL; size_t plen = 0;
  if (!make_packet(c, c->unsent_buffer + c->unsent_start, (size_t)length, true, c->unsent_start, delivery->source_state, &packet, &plen))
    qa_range_error("Netchannel allocation failed");
  c->delivering = true;
  delivery->send(delivery->send_ctx, packet, plen);
  if (delivery->trace) {
    char buf[160]; snprintf(buf, sizeof buf, "%s send %4zu : s=%lld fragment=%lld,%lld\n",
      role_name(c->role), plen, (long long)c->outgoing, (long long)c->unsent_start, (long long)length);
    delivery->trace(delivery->trace_ctx, buf);
  }
  c->delivering = false;
  free(packet);
  c->unsent_start += length;
  if (c->unsent_start == c->unsent_length && length != QA_Q3_FRAGMENT_SIZE) {
    c->outgoing++; c->unsent_fragments = false;
  }
  return true;
}
typedef struct { uint8_t **pkts; size_t *lens; size_t count, cap; } collect_t;
static void collect_send(void *ctx, const uint8_t *p, size_t n) {
  collect_t *c = ctx;
  if (c->count == c->cap) {
    size_t nc = c->cap ? c->cap * 2 : 8;
    c->pkts = realloc(c->pkts, nc * sizeof *c->pkts);
    c->lens = realloc(c->lens, nc * sizeof *c->lens);
    c->cap = nc;
  }
  c->pkts[c->count] = malloc(n ? n : 1);
  memcpy(c->pkts[c->count], p, n); c->lens[c->count] = n; c->count++;
}
static void noop_trace(void *ctx, const char *t) { (void)ctx; (void)t; }
bool qa_q3_netchannel_transmit(qa_q3_netchannel *c, const uint8_t *payload, size_t len,
                               uint8_t ***out_packets, size_t **out_lengths, size_t *out_count) {
  collect_t col = {0};
  qa_q3_channel_delivery d = { NULL, collect_send, &col, noop_trace, NULL };
  qa_q3_netchannel_begin_transmit(c, payload, len, &d);
  while (c->unsent_fragments) qa_q3_netchannel_transmit_next_fragment(c, &d);
  *out_packets = col.pkts; *out_lengths = col.lens; *out_count = col.count;
  return true;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void diag_print(const qa_q3_channel_diagnostics *d, const char *text) { if (d && d->print) d->print(d->print_ctx, text); }
bool qa_q3_netchannel_receive(qa_q3_netchannel *c, const uint8_t *packet, size_t len,
                              const qa_q3_channel_diagnostics *diag, qa_q3_channel_result *out) {
  memset(out, 0, sizeof *out); out->qport = -1;
  int64_t base = c->role == QA_Q3_CHANNEL_SERVER ? 6 : 4;
  if ((int64_t)len < base || len > QA_Q3_MAX_MESSAGE_LENGTH) { out->kind = QA_Q3_CHANNEL_REJECTED; out->reason = QA_Q3_REJECT_MALFORMED; return true; }
  uint32_t wire = rd32(packet);
  bool fragmented = (wire & 0x80000000u) != 0;
  int64_t sequence = wire & 0x7fffffffu;
  int64_t qport = c->role == QA_Q3_CHANNEL_SERVER ? rd16(packet + 4) : -1;
  if (fragmented && (int64_t)len < base + 4) { out->kind = QA_Q3_CHANNEL_REJECTED; out->reason = QA_Q3_REJECT_MALFORMED; return true; }
  int64_t start = fragmented ? (int16_t)rd16(packet + base) : 0;
  int64_t length = fragmented ? (int16_t)rd16(packet + base + 2) : 0;
  if (diag && diag->show_packets) {
    char buf[160];
    if (fragmented) snprintf(buf, sizeof buf, "%s recv %4zu : s=%lld fragment=%lld,%lld\n", role_name(c->role), len, (long long)sequence, (long long)start, (long long)length);
    else snprintf(buf, sizeof buf, "%s recv %4zu : s=%lld\n", role_name(c->role), len, (long long)sequence);
    diag_print(diag, buf);
  }
  if (sequence <= c->incoming) {
    if (diag && (diag->show_drop || diag->show_packets)) {
      char buf[256]; snprintf(buf, sizeof buf, "%s:Out of order packet %lld at %lld\n", diag->remote_address, (long long)sequence, (long long)c->incoming);
      diag_print(diag, buf);
    }
    out->kind = QA_Q3_CHANNEL_REJECTED; out->reason = QA_Q3_REJECT_SEQUENCE; return true;
  }
  c->dropped = sequence - (c->incoming + 1);
  if (c->dropped > 0 && diag && (diag->show_drop || diag->show_packets)) {
    char buf[256]; snprintf(buf, sizeof buf, "%s:Dropped %lld packets at %lld\n", diag->remote_address, (long long)c->dropped, (long long)sequence);
    diag_print(diag, buf);
  }
  if (!fragmented) {
    c->incoming = sequence;
    out->kind = QA_Q3_CHANNEL_ACCEPTED; out->sequence = sequence; out->qport = qport; out->dropped = c->dropped;
    out->payload_len = len - (size_t)base;
    out->payload = malloc(out->payload_len ? out->payload_len : 1);
    if (!out->payload) return false;
    memcpy(out->payload, packet + base, out->payload_len);
    return true;
  }
  if (sequence != c->fragment_sequence) { c->fragment_sequence = sequence; c->fragment_length = 0; }
  if (start != c->fragment_length) {
    if (diag && (diag->show_drop || diag->show_packets)) {
      char buf[256]; snprintf(buf, sizeof buf, "%s:Dropped a message fragment\n", diag->remote_address); diag_print(diag, buf);
    }
    out->kind = QA_Q3_CHANNEL_REJECTED; out->reason = QA_Q3_REJECT_FRAGMENT_ORDER; return true;
  }
  int64_t header = base + 4;
  if (length < 0 || length > (int64_t)len - header || c->fragment_length + length > QA_Q3_MAX_MESSAGE_LENGTH) {
    if (diag && (diag->show_drop || diag->show_packets)) {
      char buf[256]; snprintf(buf, sizeof buf, "%s:illegal fragment length\n", diag->remote_address); diag_print(diag, buf);
    }
    out->kind = QA_Q3_CHANNEL_REJECTED; out->reason = QA_Q3_REJECT_FRAGMENT_LENGTH; return true;
  }
  memcpy(c->fragments + c->fragment_length, packet + header, (size_t)length);
  c->fragment_length += length;
  if (length == QA_Q3_FRAGMENT_SIZE) { out->kind = QA_Q3_CHANNEL_FRAGMENT; out->sequence = sequence; out->received = c->fragment_length; return true; }
  out->payload_len = (size_t)c->fragment_length;
  out->payload = malloc(out->payload_len ? out->payload_len : 1);
  if (!out->payload) return false;
  memcpy(out->payload, c->fragments, out->payload_len);
  c->fragment_length = 0; c->incoming = sequence;
  out->kind = QA_Q3_CHANNEL_ACCEPTED; out->sequence = sequence; out->qport = qport; out->dropped = c->dropped;
  return true;
}
static bool xor_payload(const uint8_t *payload, size_t len, size_t start, int64_t initial_key, const char *command, uint8_t **out) {
  *out = malloc(len ? len : 1);
  if (!*out) return false;
  memcpy(*out, payload, len);
  size_t clen = command ? strlen(command) : 0;
  int key = (int)(initial_key & 255);
  size_t index = 0;
  for (size_t i = start; i < len; i++) {
    if (index >= clen) index = 0;
    int ch = clen == 0 ? 0 : (unsigned char)command[index];
    key ^= ((ch > 127 || ch == 37 ? 46 : ch) << (i & 1)) & 255;
    index++;
    (*out)[i] = (uint8_t)((*out)[i] ^ key);
  }
  return true;
}
bool qa_q3_xor_client_message(const uint8_t *payload, size_t len, int64_t challenge,
                              qa_q3_server_command_fn server_command, void *ctx, uint8_t **out) {
  if (len <= 12) { *out = malloc(len ? len : 1); if (!*out) return false; memcpy(*out, payload, len); return true; }
  qa_q3_message_reader *r = NULL;
  if (!qa_q3_message_reader_init(&r, payload, len, QA_Q3_MESSAGE_BITSTREAM, NULL)) return false;
  int64_t server_id = qa_q3_message_reader_read_long(r);
  int64_t ack = qa_q3_message_reader_read_long(r);
  int64_t reliable = qa_q3_message_reader_read_long(r);
  qa_q3_message_reader_destroy(r);
  const char *cmd = server_command(ctx, reliable);
  return xor_payload(payload, len, 12, (int32_t)((uint32_t)challenge ^ (uint32_t)server_id ^ (uint32_t)ack), cmd ? cmd : "", out);
}
bool qa_q3_xor_server_message(const uint8_t *payload, size_t len, int64_t challenge, int64_t sequence,
                              const char *client_command, uint8_t **out) {
  return xor_payload(payload, len, 4, (int32_t)((uint32_t)challenge ^ (uint32_t)sequence), client_command ? client_command : "", out);
}
