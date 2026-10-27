/* Port of src/network/common/reliability.ts. GPL-2.0-or-later. */
#include "qa/network/common/reliability.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static int is_safe(long long v) { return v >= -QA_MAX_SAFE_INTEGER && v <= QA_MAX_SAFE_INTEGER; }
static uint8_t *dupbytes(const uint8_t *b, size_t n) { uint8_t *p = malloc(n ? n : 1); if (n) memcpy(p, b, n); return p; }

int qa_toggle_channel_init(qa_toggle_channel *c, long long capacity, long long first_sequence) {
  if (!is_safe(capacity) || capacity <= 0) qa_range_error("Invalid reliable capacity");
  memset(c, 0, sizeof *c); c->capacity = capacity; c->outgoing = first_sequence; return 1;
}
void qa_toggle_channel_destroy(qa_toggle_channel *c) { free(c->pending); free(c->reliable); memset(c, 0, sizeof *c); }
long long qa_toggle_channel_incoming_sequence(const qa_toggle_channel *c) { return c->incoming; }
long long qa_toggle_channel_outgoing_sequence(const qa_toggle_channel *c) { return c->outgoing; }
void qa_toggle_channel_advance_outgoing(qa_toggle_channel *c, long long sequence) {
  if (!is_safe(sequence) || sequence < c->outgoing || sequence > 0x7fffffffLL)
    qa_range_error("Invalid outgoing sequence advancement");
  c->outgoing = sequence;
}
bool qa_toggle_channel_has_pending_reliable(const qa_toggle_channel *c) { return c->pending_len > 0 || c->reliable_len > 0; }
void qa_toggle_channel_queue(qa_toggle_channel *c, const uint8_t *payload, size_t length) {
  if ((long long)(c->pending_len + length) > c->capacity) qa_range_error("Reliable message overflow");
  uint8_t *j = realloc(c->pending, c->pending_len + length ? c->pending_len + length : 1);
  c->pending = j; memcpy(c->pending + c->pending_len, payload, length); c->pending_len += length;
}
void qa_toggle_channel_transmit(qa_toggle_channel *c, const uint8_t *unreliable, size_t length,
                                long long payload_capacity, qa_toggle_packet *out) {
  long long cap = payload_capacity < 0 ? c->capacity : payload_capacity;
  if (!is_safe(cap) || cap < 0 || cap > c->capacity) qa_range_error("Invalid packet payload capacity");
  if (c->outgoing > 0x7fffffffLL) qa_range_error("Channel sequence exhausted; reconnect required");
  bool send_reliable = c->incoming_ack > c->last_reliable && c->incoming_reliable_ack != c->reliable_sequence;
  if (c->reliable_len == 0 && c->pending_len > 0) {
    free(c->reliable); c->reliable = c->pending; c->reliable_len = c->pending_len;
    c->pending = NULL; c->pending_len = 0;
    c->reliable_sequence = c->reliable_sequence == 0 ? 1 : 0; send_reliable = true;
  }
  size_t rlen = send_reliable ? c->reliable_len : 0;
  if ((long long)rlen > cap) qa_range_error("Reliable payload does not fit packet");
  bool include = (long long)(rlen + length) <= cap;
  size_t plen = rlen + (include ? length : 0);
  out->payload = malloc(plen ? plen : 1); out->length = plen;
  if (rlen) memcpy(out->payload, c->reliable, rlen);
  if (include && length) memcpy(out->payload + rlen, unreliable, length);
  out->sequence = c->outgoing++;
  if (send_reliable) c->last_reliable = c->outgoing;
  out->acknowledged = c->incoming; out->reliable = send_reliable;
  out->reliable_acknowledged = c->incoming_reliable;
}
qa_toggle_receive_result qa_toggle_channel_receive(qa_toggle_channel *c, const qa_toggle_packet *p) {
  qa_toggle_receive_result out; memset(&out, 0, sizeof out);
  if (p->sequence <= c->incoming) { out.accepted = false; out.reason = "sequence"; return out; }
  out.dropped = p->sequence - c->incoming - 1;
  if (p->reliable_acknowledged == c->reliable_sequence) { free(c->reliable); c->reliable = NULL; c->reliable_len = 0; }
  c->incoming = p->sequence; c->incoming_ack = p->acknowledged; c->incoming_reliable_ack = p->reliable_acknowledged;
  if (p->reliable) c->incoming_reliable = c->incoming_reliable == 0 ? 1 : 0;
  out.accepted = true; out.payload = dupbytes(p->payload, p->length); out.length = p->length;
  return out;
}

int qa_stop_and_wait_init(qa_stop_and_wait *c, long long mm, long long fb, double retry_ms) {
  if (!is_safe(mm) || mm <= 0 || !is_safe(fb) || fb <= 0 || fb > mm)
    qa_range_error("Invalid NetQuake fragment limits");
  memset(c, 0, sizeof *c); c->max_message_bytes = mm; c->fragment_bytes = fb; c->retry_ms = retry_ms;
  c->received = malloc((size_t)mm); return c->received != NULL;
}
void qa_stop_and_wait_destroy(qa_stop_and_wait *c) { free(c->sending); free(c->received); memset(c, 0, sizeof *c); }
bool qa_stop_and_wait_can_send(const qa_stop_and_wait *c) { return !c->has_sending; }
void qa_stop_and_wait_begin(qa_stop_and_wait *c, const uint8_t *bytes, size_t length) {
  if (c->has_sending) qa_range_error("Reliable message remains unacknowledged");
  if ((long long)length > c->max_message_bytes) qa_range_error("Reliable message exceeds capacity");
  c->sending = dupbytes(bytes, length); c->sending_len = length; c->offset = 0; c->sent = false; c->has_sending = true;
}
int qa_stop_and_wait_next(qa_stop_and_wait *c, double now, qa_reliable_fragment *out) {
  if (!c->has_sending || (c->sent && now - c->sent_at <= c->retry_ms)) return 0;
  size_t end = c->offset + (size_t)c->fragment_bytes; if (end > c->sending_len) end = c->sending_len;
  c->sent_at = now; c->sent = true;
  out->sequence = c->outgoing; out->final = end == c->sending_len;
  out->length = end - c->offset; out->payload = dupbytes(c->sending + c->offset, out->length);
  return 1;
}
bool qa_stop_and_wait_acknowledge(qa_stop_and_wait *c, uint32_t sequence) {
  if (!c->has_sending || !c->sent || sequence != c->outgoing) return false;
  c->outgoing = c->outgoing + 1u;
  c->offset += (size_t)c->fragment_bytes;
  if (c->offset >= c->sending_len) { free(c->sending); c->sending = NULL; c->has_sending = false; }
  else c->sent = false;
  return true;
}
qa_reliable_fragment_result qa_stop_and_wait_receive(qa_stop_and_wait *c, const qa_reliable_fragment *f) {
  qa_reliable_fragment_result out; memset(&out, 0, sizeof out); out.acknowledge = f->sequence;
  if (f->sequence != c->incoming) { out.kind = "duplicate"; return out; }
  if ((long long)(c->received_len + f->length) > c->max_message_bytes) qa_range_error("Reliable receive overflow");
  memcpy(c->received + c->received_len, f->payload, f->length); c->received_len += f->length;
  c->incoming = c->incoming + 1u;
  if (!f->final) { out.kind = "fragment"; return out; }
  out.kind = "message"; out.payload = dupbytes(c->received, c->received_len); out.length = c->received_len;
  c->received_len = 0; return out;
}
