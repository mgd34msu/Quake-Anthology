/* Port of src/network/unified/channel.ts. GPL-2.0-or-later. */
#include "qa/network/unified/channel.h"
#include "qa/foundation/support.h"
#include "qa/network/unified/packet.h"
#include <stdlib.h>
#include <string.h>
static bool is_safe_ll(int64_t v) { return v >= -QA_MAX_SAFE_INTEGER && v <= QA_MAX_SAFE_INTEGER; }
static uint8_t *dupbytes(const uint8_t *b, size_t n) { uint8_t *p = malloc(n ? n : 1); if (n) memcpy(p, b, n); return p; }
void qa_unified_deliveries_destroy(qa_unified_delivery *events, size_t count) {
  for (size_t i = 0; i < count; i++) free(events[i].payload);
  free(events);
}
void qa_unified_receive_result_destroy(qa_unified_receive_result *r) {
  qa_unified_deliveries_destroy(r->deliveries, r->delivery_count);
  for (size_t i = 0; i < r->reply_count; i++) free(r->replies[i]);
  free(r->replies); free(r->reply_lens);
  memset(r, 0, sizeof *r);
}
static void assembly_free(struct qa_unified_assembly *a) {
  if (!a) return;
  for (int64_t i = 0; i < a->key.fragments; i++) free(a->chunks[i]);
  free(a->chunks); free(a->chunk_lens); free(a);
}
static int packet_parts(int64_t total, int64_t fragment_bytes) {
  return total == 0 ? 1 : (int)((total + fragment_bytes - 1) / fragment_bytes);
}
static int64_t next_seq(qa_unified_channel *c) {
  if (c->next_packet > QA_UNIFIED_SEQUENCE_MAX) qa_range_error("Unified sequence exhausted; reconnect required");
  c->next_packet++;
  return c->next_packet - 1;
}
static int64_t reliable_seq(qa_unified_channel *c) {
  if (c->next_reliable > QA_UNIFIED_SEQUENCE_MAX) qa_range_error("Unified sequence exhausted; reconnect required");
  c->next_reliable++;
  return c->next_reliable - 1;
}
void qa_unified_channel_init(qa_unified_channel *c, const char *token, int64_t fragment_bytes,
                             int64_t max_message_bytes, double retry_ms, int64_t first_sequence, bool has_first) {
  int64_t first = has_first ? first_sequence : 1;
  if (!is_safe_ll(first) || first < 1 || first > QA_UNIFIED_SEQUENCE_MAX)
    qa_range_error("Invalid unified sequence");
  memset(c, 0, sizeof *c);
  qa_unified_token(token, c->token);
  c->fragment_bytes = fragment_bytes; c->max_message_bytes = max_message_bytes; c->retry_ms = retry_ms;
  c->next_packet = first; c->next_reliable = first;
  c->last_packet = first - 1;
}
void qa_unified_channel_destroy(qa_unified_channel *c) {
  free(c->reliable);
  assembly_free(c->reliable_assembly); assembly_free(c->frame_assembly);
  for (size_t i = 0; i < c->frame_count; i++) free(c->frames[i].payload);
  free(c->frames); free(c->frame_sequences);
  memset(c, 0, sizeof *c);
}
bool qa_unified_channel_can_send_reliable(const qa_unified_channel *c) { return !c->has_reliable; }
void qa_unified_channel_queue_reliable(qa_unified_channel *c, const uint8_t *bytes, size_t length) {
  if (c->has_reliable) qa_range_error("Unified reliable message is unacknowledged");
  if ((int64_t)length > c->max_message_bytes) qa_range_error("Unified message exceeds capacity");
  if (packet_parts((int64_t)length, c->fragment_bytes) > 65535) qa_range_error("Unified message has too many fragments");
  c->reliable = dupbytes(bytes, length); c->reliable_len = length; c->has_reliable = true;
  c->reliable_sequence = reliable_seq(c); c->reliable_sent = false;
}
static void push_reply_arr(uint8_t ***arr, size_t **lens, size_t *count, size_t *cap, uint8_t *bytes, size_t len) {
  if (*count == *cap) { size_t nc = *cap ? *cap * 2 : 4; *arr = realloc(*arr, nc * sizeof **arr); *lens = realloc(*lens, nc * sizeof **lens); *cap = nc; }
  (*arr)[*count] = bytes; (*lens)[*count] = len; (*count)++;
}
uint8_t **qa_unified_channel_transmit_reliable(qa_unified_channel *c, double now_ms, size_t **out_lens, size_t *out_count) {
  uint8_t **out = NULL; size_t *lens = NULL, count = 0, cap = 0;
  if (!c->has_reliable || (c->reliable_sent && now_ms - c->reliable_sent_at <= c->retry_ms)) {
    if (c->ack_dirty) {
      qa_unified_packet ack; memset(&ack, 0, sizeof ack);
      ack.kind = QA_UNIFIED_ACK;
      memcpy(ack.token, c->token, sizeof ack.token);
      ack.sequence = next_seq(c); ack.acknowledged_reliable = c->acknowledged_reliable; ack.fragment = 0;
      size_t n = 0; uint8_t *b = qa_encode_unified_packet(&ack, &n);
      push_reply_arr(&out, &lens, &count, &cap, b, n);
      c->ack_sent = true; c->ack_dirty = false;
    }
    *out_lens = lens; *out_count = count; return out;
  }
  int64_t fragments = packet_parts((int64_t)c->reliable_len, c->fragment_bytes);
  for (int64_t fragment = 0; fragment < fragments; fragment++) {
    int64_t offset = fragment * c->fragment_bytes;
    int64_t end = offset + c->fragment_bytes;
    if (end > (int64_t)c->reliable_len) end = (int64_t)c->reliable_len;
    qa_unified_packet p; memset(&p, 0, sizeof p);
    p.kind = QA_UNIFIED_RELIABLE;
    memcpy(p.token, c->token, sizeof p.token);
    p.sequence = next_seq(c); p.acknowledged_reliable = c->acknowledged_reliable;
    p.fragment = fragment; p.required_reliable_sequence = 0;
    p.total_bytes = (int64_t)c->reliable_len; p.fragment_bytes = c->fragment_bytes; p.fragments = fragments;
    p.payload = c->reliable + offset; p.length = (size_t)(end - offset);
    size_t n = 0; uint8_t *b = qa_encode_unified_packet(&p, &n);
    push_reply_arr(&out, &lens, &count, &cap, b, n);
  }
  c->reliable_sent = true; c->reliable_sent_at = now_ms;
  c->ack_sent = c->acknowledged_reliable != 0; c->ack_dirty = false;
  *out_lens = lens; *out_count = count; return out;
}
uint8_t *qa_unified_channel_transmit_frame(qa_unified_channel *c, const uint8_t *bytes, size_t length,
                                           bool has_required, int64_t required, size_t *out_len) {
  int64_t req = has_required ? required : (c->reliable_sent ? c->reliable_sequence : c->acknowledged_reliable);
  if (!is_safe_ll(req) || req < 0 || req > QA_UNIFIED_SEQUENCE_MAX
      || (has_required && req != 0 && (!c->reliable_sent || req != c->reliable_sequence) && req > c->acknowledged_reliable))
    qa_range_error("Frame references an unknown reliable message");
  if ((int64_t)length > c->max_message_bytes) qa_range_error("Unified message exceeds capacity");
  if (packet_parts((int64_t)length, c->fragment_bytes) != 1) qa_range_error("Frame exceeds one fragment; sender chunking is required");
  qa_unified_packet p; memset(&p, 0, sizeof p);
  p.kind = QA_UNIFIED_FRAME;
  memcpy(p.token, c->token, sizeof p.token);
  p.sequence = next_seq(c); p.acknowledged_reliable = c->acknowledged_reliable; p.fragment = 0;
  p.required_reliable_sequence = req;
  p.total_bytes = (int64_t)length; p.fragment_bytes = c->fragment_bytes; p.fragments = 1;
  p.payload = (uint8_t *)bytes; p.length = length;
  uint8_t *b = qa_encode_unified_packet(&p, out_len);
  c->ack_sent = c->acknowledged_reliable != 0; c->ack_dirty = false;
  return b;
}
struct delivery_list { qa_unified_delivery *items; size_t count, cap; };
static void push_delivery(struct delivery_list *l, qa_unified_delivery d) {
  if (l->count == l->cap) { size_t nc = l->cap ? l->cap * 2 : 4; l->items = realloc(l->items, nc * sizeof *l->items); l->cap = nc; }
  l->items[l->count++] = d;
}
static void add_frame_sequence(qa_unified_channel *c, int64_t required) {
  if (c->seq_count == c->seq_cap) { size_t nc = c->seq_cap ? c->seq_cap * 2 : 8; c->frame_sequences = realloc(c->frame_sequences, nc * sizeof *c->frame_sequences); c->seq_cap = nc; }
  c->frame_sequences[c->seq_count++] = required;
}
static void hold_or_emit_frame(qa_unified_channel *c, struct delivery_list *deliveries, int64_t required, uint8_t *payload, size_t length) {
  if (required <= c->delivered_reliable) {
    qa_unified_delivery d; memset(&d, 0, sizeof d);
    d.kind = "frame"; d.payload = payload; d.length = length; d.required_reliable_sequence = required;
    push_delivery(deliveries, d);
    return;
  }
  if (c->frame_count == c->frame_cap) { size_t nc = c->frame_cap ? c->frame_cap * 2 : 8; c->frames = realloc(c->frames, nc * sizeof *c->frames); c->frame_cap = nc; }
  c->frames[c->frame_count].required = required;
  c->frames[c->frame_count].payload = payload;
  c->frames[c->frame_count].length = length;
  c->frame_count++;
}
static void flush_frames(qa_unified_channel *c, struct delivery_list *deliveries) {
  size_t w = 0;
  for (size_t i = 0; i < c->frame_count; i++) {
    struct qa_unified_pending_frame f = c->frames[i];
    if (f.required <= c->delivered_reliable) {
      qa_unified_delivery d; memset(&d, 0, sizeof d);
      d.kind = "frame"; d.payload = f.payload; d.length = f.length; d.required_reliable_sequence = f.required;
      push_delivery(deliveries, d);
    } else c->frames[w++] = f;
  }
  c->frame_count = w;
}
static bool key_equal(const struct qa_unified_assembly_key *a, const struct qa_unified_assembly_key *b) {
  return a->sequence == b->sequence && a->fragments == b->fragments
    && a->fragment_bytes == b->fragment_bytes && a->total_bytes == b->total_bytes;
}
/* Returns 1 complete (payload in *out_bytes), 0 pending, -1 rejected (*reason). */
static int assemble(qa_unified_channel *c, bool reliable, const qa_unified_packet *p,
                    uint8_t **out_bytes, size_t *out_len, const char **reason) {
  struct qa_unified_assembly **slot = reliable ? &c->reliable_assembly : &c->frame_assembly;
  struct qa_unified_assembly_key key = { p->sequence, p->fragments, p->fragment_bytes, p->total_bytes };
  (void)c;
  if (!*slot || !key_equal(&(*slot)->key, &key)) {
    if (p->fragment == 0) assembly_free(*slot);
    else if (*slot && (*slot)->chunks[p->fragment]) { *reason = "sequence"; return -1; }
    if (!*slot || !key_equal(&(*slot)->key, &key)) {
      struct qa_unified_assembly *a = calloc(1, sizeof *a);
      a->key = key;
      a->chunks = calloc((size_t)(p->fragments > 0 ? p->fragments : 1), sizeof *a->chunks);
      a->chunk_lens = calloc((size_t)(p->fragments > 0 ? p->fragments : 1), sizeof *a->chunk_lens);
      *slot = a;
    }
  }
  struct qa_unified_assembly *a = *slot;
  if (!key_equal(&a->key, &key)) { *reason = "sequence"; return -1; }
  if (a->chunks[p->fragment]) return 0;
  a->chunks[p->fragment] = dupbytes(p->payload, p->length);
  a->chunk_lens[p->fragment] = p->length;
  a->count++; a->size += (int64_t)p->length;
  if (a->count != key.fragments) return 0;
  if (a->size != key.total_bytes) { *reason = "length"; return -1; }
  uint8_t *joined = malloc(key.total_bytes > 0 ? (size_t)key.total_bytes : 1);
  size_t at = 0;
  for (int64_t i = 0; i < key.fragments; i++) {
    memcpy(joined + at, a->chunks[i], a->chunk_lens[i]);
    at += a->chunk_lens[i];
  }
  assembly_free(a); *slot = NULL;
  *out_bytes = joined; *out_len = (size_t)key.total_bytes;
  return 1;
}
uint8_t **qa_unified_channel_receive(qa_unified_channel *c, const uint8_t *bytes, size_t length, double now_ms,
                                     qa_unified_receive_result *out) {
  memset(out, 0, sizeof *out);
  struct delivery_list deliveries = {0};
  qa_unified_packet p;
  if (!qa_decode_unified_packet(bytes, length, &p) || strcmp(p.token, c->token) != 0) {
    qa_unified_delivery d; memset(&d, 0, sizeof d);
    d.kind = "invalid-frame"; d.reason = "packet";
    push_delivery(&deliveries, d);
    out->deliveries = deliveries.items; out->delivery_count = deliveries.count;
    return out->replies;
  }
  if (p.kind != QA_UNIFIED_ACK && p.total_bytes > c->max_message_bytes) {
    qa_unified_delivery d; memset(&d, 0, sizeof d);
    d.kind = "rejected-fragment"; d.sequence = p.sequence; d.reason = "length";
    push_delivery(&deliveries, d);
    out->deliveries = deliveries.items; out->delivery_count = deliveries.count;
    return out->replies;
  }
  bool fresh = p.sequence > c->last_packet;
  int64_t last = c->last_packet;
  if (fresh) c->last_packet = p.sequence;
  bool pending_before = c->has_reliable;
  if (c->has_reliable && p.acknowledged_reliable >= c->reliable_sequence) {
    free(c->reliable); c->reliable = NULL; c->has_reliable = false;
  }
  c->last_received_at = now_ms;
  if (p.kind == QA_UNIFIED_RELIABLE) {
    bool due_before = c->reliable_sent && now_ms - c->reliable_sent_at > c->retry_ms;
    uint8_t *joined = NULL; size_t joined_len = 0; const char *reason = NULL;
    int ar = assemble(c, true, &p, &joined, &joined_len, &reason);
    if (ar < 0) {
      qa_unified_delivery d; memset(&d, 0, sizeof d);
      d.kind = "rejected-fragment"; d.sequence = p.sequence; d.reason = reason;
      push_delivery(&deliveries, d);
    } else if (ar == 1) {
      if (p.sequence > c->acknowledged_reliable) {
        c->acknowledged_reliable = p.sequence;
        if (!c->ack_sent) c->ack_sent = false;
        c->ack_dirty = true;
      }
      if (p.sequence > c->delivered_reliable) {
        c->delivered_reliable = p.sequence;
        qa_unified_delivery d; memset(&d, 0, sizeof d);
        d.kind = "reliable"; d.payload = joined; d.length = joined_len; d.sequence = p.sequence;
        push_delivery(&deliveries, d);
        flush_frames(c, &deliveries);
      } else free(joined);
    }
    (void)due_before;
  } else if (p.kind == QA_UNIFIED_FRAME) {
    bool seen = false;
    for (size_t i = 0; i < c->seq_count; i++) if (c->frame_sequences[i] == p.sequence) seen = true;
    if (!seen) {
      uint8_t *joined = NULL; size_t joined_len = 0; const char *reason = NULL;
      int ar = assemble(c, false, &p, &joined, &joined_len, &reason);
      if (ar < 0) {
        qa_unified_delivery d; memset(&d, 0, sizeof d);
        d.kind = "rejected-fragment"; d.sequence = p.sequence; d.reason = reason;
        push_delivery(&deliveries, d);
      } else if (ar == 1) {
        add_frame_sequence(c, p.sequence);
        hold_or_emit_frame(c, &deliveries, p.required_reliable_sequence, joined, joined_len);
      }
    }
  }
  (void)last;
  /* TS sends replies only for fresh packets; an ack that cleared the
   * pending reliable (ackNotified) or an overdue burst forces the send. */
  size_t *lens = NULL; size_t count = 0;
  uint8_t **replies = NULL;
  if (fresh) {
    bool ack_notified = pending_before && !c->has_reliable;
    double when = now_ms;
    if (ack_notified && c->reliable_sent) when = c->reliable_sent_at + c->retry_ms + 1;
    replies = qa_unified_channel_transmit_reliable(c, when, &lens, &count);
  }
  out->deliveries = deliveries.items; out->delivery_count = deliveries.count;
  out->replies = replies; out->reply_lens = lens; out->reply_count = count;
  return replies;
}
