/* Port of src/network/unified/channel.ts. GPL-2.0-or-later. */
#include "qa/network/unified/channel.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/unified/packet.h"

typedef struct qa_unified_outgoing {
  int64_t sequence, required, fragments, next;
  uint8_t *payload; size_t length;
  /* TS sent Map: attempts[f] == 0 means absent; order keeps Map iteration order. */
  double *sent_at; int32_t *attempts; uint8_t *acknowledged; int32_t *order;
  int64_t sent_count, acknowledged_count;
} outgoing;
typedef struct qa_unified_assembly {
  int64_t sequence, required, fragment_bytes, fragments, received_count;
  double started;
  uint8_t *payload; size_t length;
  uint8_t *received;
} assembly;

static void *xalloc(size_t size) {
  void *p = calloc(size ? size : 1, 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static void outgoing_free(outgoing *m) {
  if (!m) return;
  free(m->payload); free(m->sent_at); free(m->attempts); free(m->acknowledged); free(m->order);
  free(m);
}
static void assembly_free(assembly *a) {
  if (!a) return;
  free(a->payload); free(a->received);
  free(a);
}
void qa_unified_deliveries_destroy(qa_unified_delivery *events, size_t count) {
  for (size_t i = 0; i < count; i++) free(events[i].payload);
  free(events);
}
qa_unified_channel_limits qa_unified_channel_default_limits(void) {
  return (qa_unified_channel_limits){1200, 4 * 1024 * 1024, 8 * 1024 * 1024, 256, 8, 8192, 32, 250, 40, 30000, 0};
}
void qa_unified_channel_init(qa_unified_channel *c, const char *token, const qa_unified_channel_limits *limits) {
  memset(c, 0, sizeof *c);
  qa_unified_token(token, c->token);
  c->limits = limits ? *limits : qa_unified_channel_default_limits();
  if (c->limits.inbound_message_bytes == 0) c->limits.inbound_message_bytes = c->limits.message_bytes;
  const qa_unified_channel_limits *l = &c->limits;
  const int64_t values[] = {l->datagram_bytes, l->message_bytes, l->inbound_message_bytes, l->queued_reliable_bytes, l->queued_reliable_messages,
      l->reliable_window_messages, l->fragments, l->packets_per_flush, l->retry_milliseconds, l->maximum_transmissions,
      l->assembly_milliseconds};
  for (size_t i = 0; i < sizeof values / sizeof *values; i++)
    if (values[i] > QA_MAX_SAFE_INTEGER || values[i] < 1) qa_range_error("Invalid unified channel limit");
  if (l->datagram_bytes <= QA_UNIFIED_PACKET_HEADER_BYTES || l->datagram_bytes > 65507
      || l->message_bytes > QA_UNIFIED_SEQUENCE_MAX || l->fragments > 65535 || l->reliable_window_messages > 64
      || l->queued_reliable_bytes < l->message_bytes)
    qa_range_error("Invalid unified channel capacity");
  c->next_reliable = 1;
  c->next_frame = 1;
}
int64_t qa_unified_channel_received_reliable_sequence(const qa_unified_channel *c) { return c->reliable_received; }
int64_t qa_unified_channel_acknowledged_reliable_sequence(const qa_unified_channel *c) { return c->reliable_acknowledged; }
bool qa_unified_channel_closed(const qa_unified_channel *c) { return c->ended; }
static int64_t window_messages(const qa_unified_channel *c) {
  return c->limits.reliable_window_messages < c->limits.queued_reliable_messages
      ? c->limits.reliable_window_messages : c->limits.queued_reliable_messages;
}
static void check_open(const qa_unified_channel *c) {
  if (c->ended) qa_raise("Error", 0, 0, "Unified channel is closed");
}
static void check_time(double now) {
  if (!isfinite(now) || now < 0) qa_range_error("Invalid unified channel time");
}
static int64_t fragments_for(const qa_unified_channel *c, size_t length) {
  int64_t bytes = c->limits.datagram_bytes - QA_UNIFIED_PACKET_HEADER_BYTES;
  int64_t count = ((int64_t)length + bytes - 1) / bytes;
  return count < 1 ? 1 : count;
}
static outgoing *outgoing_new(const qa_unified_channel *c, const uint8_t *payload, size_t length, int64_t sequence, int64_t required) {
  int64_t fragments = fragments_for(c, length);
  if ((int64_t)length > c->limits.message_bytes || fragments > c->limits.fragments)
    qa_range_error("Unified message exceeds channel capacity");
  if (sequence > QA_UNIFIED_SEQUENCE_MAX) qa_range_error("Unified sequence exhausted; reconnect required");
  outgoing *m = xalloc(sizeof *m);
  m->sequence = sequence; m->required = required; m->fragments = fragments; m->length = length;
  m->payload = xalloc(length);
  if (length) memcpy(m->payload, payload, length);
  m->sent_at = xalloc((size_t)fragments * sizeof *m->sent_at);
  m->attempts = xalloc((size_t)fragments * sizeof *m->attempts);
  m->acknowledged = xalloc((size_t)fragments);
  m->order = xalloc((size_t)fragments * sizeof *m->order);
  return m;
}
bool qa_unified_channel_can_queue_reliable(const qa_unified_channel *c, size_t length) {
  return !c->ended && (int64_t)c->reliable_count < c->limits.queued_reliable_messages
      && c->queued_bytes + (int64_t)length <= c->limits.queued_reliable_bytes
      && (int64_t)length <= c->limits.message_bytes && fragments_for(c, length) <= c->limits.fragments
      && c->next_reliable <= QA_UNIFIED_SEQUENCE_MAX;
}
int64_t qa_unified_channel_queue_reliable(qa_unified_channel *c, const uint8_t *payload, size_t length) {
  check_open(c);
  if ((int64_t)c->reliable_count >= c->limits.queued_reliable_messages
      || c->queued_bytes + (int64_t)length > c->limits.queued_reliable_bytes)
    qa_range_error("Unified reliable queue overflow");
  outgoing *message = outgoing_new(c, payload, length, c->next_reliable, 0);
  c->next_reliable++;
  c->queued_bytes += (int64_t)length;
  if (c->reliable_count == c->reliable_cap) {
    c->reliable_cap = c->reliable_cap ? c->reliable_cap * 2 : 16;
    c->reliable = realloc(c->reliable, c->reliable_cap * sizeof *c->reliable);
    if (!c->reliable) qa_range_error("out of memory");
  }
  c->reliable[c->reliable_count++] = message;
  return message->sequence;
}
void qa_unified_channel_queue_frame(qa_unified_channel *c, const uint8_t *payload, size_t length, double required) {
  check_open(c);
  if (!isfinite(required) || required != trunc(required) || required < 0 || required >= (double)c->next_reliable)
    qa_range_error("Frame depends on an unqueued reliable message");
  outgoing *frame = outgoing_new(c, payload, length, c->next_frame, (int64_t)required);
  c->next_frame++;
  if (!c->frame) c->frame = frame;
  else {
    outgoing_free(c->pending_frame); /* TS drops its reference to the replaced pending frame. */
    c->pending_frame = frame;
  }
}
void qa_unified_channel_close(qa_unified_channel *c) {
  c->ended = true;
  for (size_t i = 0; i < c->reliable_count; i++) outgoing_free(c->reliable[i]);
  c->reliable_count = 0;
  c->queued_bytes = 0;
  outgoing_free(c->frame); c->frame = NULL;
  outgoing_free(c->pending_frame); c->pending_frame = NULL;
  for (size_t i = 0; i < c->assembly_count; i++) assembly_free(c->assemblies[i]);
  c->assembly_count = 0;
  c->received_bytes = 0;
  assembly_free(c->frame_assembly); c->frame_assembly = NULL;
  if (c->has_waiting_frame) { free(c->waiting_frame.payload); c->has_waiting_frame = false; }
  c->ack_count = 0;
}
void qa_unified_channel_destroy(qa_unified_channel *c) {
  qa_unified_channel_close(c);
  free(c->reliable); free(c->assemblies); free(c->acks);
  c->reliable = NULL; c->assemblies = NULL; c->acks = NULL;
  c->reliable_cap = c->assembly_cap = c->ack_cap = 0;
}
/* True when a reliable assembly timed out: the channel is closed and failure is set. */
static bool expire(qa_unified_channel *c, double now) {
  for (size_t i = 0; i < c->assembly_count; i++) {
    if (now - c->assemblies[i]->started >= (double)c->limits.assembly_milliseconds) {
      qa_unified_channel_close(c);
      c->failure = "Unified reliable assembly timed out";
      return true;
    }
  }
  if (c->frame_assembly && now - c->frame_assembly->started >= (double)c->limits.assembly_milliseconds) {
    assembly_free(c->frame_assembly);
    c->frame_assembly = NULL;
  }
  return false;
}
/* A single cumulative ACK suffices for every already delivered duplicate. */
static void acknowledge(qa_unified_channel *c, int64_t sequence, int64_t fragment) {
  bool delivered = sequence <= c->reliable_received;
  for (size_t i = 0; i < c->ack_count; i++) {
    struct qa_unified_ack *ack = &c->acks[i];
    if (ack->delivered != delivered) continue;
    if (delivered || (ack->sequence == sequence && ack->fragment == fragment)) {
      /* Map.set on an existing key keeps its position and takes the new value. */
      ack->sequence = sequence; ack->fragment = fragment;
      return;
    }
  }
  if (c->ack_count == c->ack_cap) {
    c->ack_cap = c->ack_cap ? c->ack_cap * 2 : 32;
    c->acks = realloc(c->acks, c->ack_cap * sizeof *c->acks);
    if (!c->acks) qa_range_error("out of memory");
  }
  c->acks[c->ack_count++] = (struct qa_unified_ack){delivered, sequence, fragment};
}
static void accept_acknowledgment(qa_unified_channel *c, const qa_unified_packet *packet) {
  int64_t window = window_messages(c);
  int64_t end = -1;
  for (size_t i = 0; i < c->reliable_count; i++)
    if (c->reliable[i]->sequence == packet->acknowledged_reliable) { end = (int64_t)i; break; }
  if (end >= 0 && end < window) {
    bool complete = true;
    for (int64_t i = 0; i <= end; i++)
      if (c->reliable[i]->next != c->reliable[i]->fragments) { complete = false; break; }
    if (complete) {
      for (int64_t i = 0; i <= end; i++) {
        c->queued_bytes -= (int64_t)c->reliable[i]->length;
        outgoing_free(c->reliable[i]);
      }
      size_t removed = (size_t)end + 1;
      memmove(c->reliable, c->reliable + removed, (c->reliable_count - removed) * sizeof *c->reliable);
      c->reliable_count -= removed;
      c->reliable_acknowledged = packet->acknowledged_reliable;
    }
  }
  if (packet->kind == QA_UNIFIED_ACK) {
    for (size_t i = 0; i < c->reliable_count && (int64_t)i < window; i++) {
      outgoing *message = c->reliable[i];
      if (message->sequence != packet->sequence) continue;
      /* A fragment outside the array was never sent. */
      if (packet->fragment < message->fragments && message->attempts[packet->fragment] > 0
          && !message->acknowledged[packet->fragment]) {
        message->acknowledged[packet->fragment] = 1;
        message->acknowledged_count++;
      }
      break;
    }
  }
}
static assembly *assembly_new(const qa_unified_packet *packet, double now) {
  assembly *a = xalloc(sizeof *a);
  a->sequence = packet->sequence; a->required = packet->required_reliable_sequence;
  a->fragment_bytes = packet->fragment_bytes; a->fragments = packet->fragments;
  a->started = now;
  a->length = (size_t)packet->total_bytes;
  a->payload = xalloc(a->length);
  a->received = xalloc((size_t)packet->fragments);
  return a;
}
static bool append(assembly *a, const qa_unified_packet *packet) {
  if (a->required != packet->required_reliable_sequence || (int64_t)a->length != packet->total_bytes
      || a->fragment_bytes != packet->fragment_bytes || a->fragments != packet->fragments)
    return false;
  if (!a->received[packet->fragment]) {
    if (packet->length) memcpy(a->payload + packet->fragment * packet->fragment_bytes, packet->payload, packet->length);
    a->received[packet->fragment] = 1;
    a->received_count++;
  }
  return true;
}
typedef struct deliveries { qa_unified_delivery *items; size_t count, cap; } deliveries;
static void deliver(deliveries *out, qa_unified_delivery delivery) {
  if (out->count == out->cap) {
    out->cap = out->cap ? out->cap * 2 : 4;
    out->items = realloc(out->items, out->cap * sizeof *out->items);
    if (!out->items) qa_range_error("out of memory");
  }
  out->items[out->count++] = delivery;
}
static void release_frame(qa_unified_channel *c, deliveries *out) {
  if (!c->has_waiting_frame || c->waiting_frame.required_reliable_sequence > c->reliable_received) return;
  c->has_waiting_frame = false;
  if (c->waiting_frame.sequence <= c->frame_received) { free(c->waiting_frame.payload); return; }
  c->frame_received = c->waiting_frame.sequence;
  deliver(out, c->waiting_frame);
}
static assembly *find_assembly(const qa_unified_channel *c, int64_t sequence) {
  for (size_t i = 0; i < c->assembly_count; i++)
    if (c->assemblies[i]->sequence == sequence) return c->assemblies[i];
  return NULL;
}
qa_unified_delivery *qa_unified_channel_receive(qa_unified_channel *c, const uint8_t *datagram, size_t length,
    double now, size_t *out_count) {
  *out_count = 0;
  check_open(c);
  check_time(now);
  if (expire(c, now)) return NULL;
  if ((int64_t)length > c->limits.datagram_bytes) return NULL;
  qa_unified_packet packet;
  if (!qa_decode_unified_packet(datagram, length, &packet) || strcmp(packet.token, c->token) != 0) return NULL;
  if (packet.kind != QA_UNIFIED_ACK
      && (packet.total_bytes > c->limits.inbound_message_bytes || packet.fragments > c->limits.fragments)) return NULL;
  accept_acknowledgment(c, &packet);
  deliveries delivered = {0};
  if (packet.kind == QA_UNIFIED_ACK) return NULL;
  if (packet.kind == QA_UNIFIED_RELIABLE) {
    if (packet.sequence <= c->reliable_received) { acknowledge(c, packet.sequence, packet.fragment); return NULL; }
    if (packet.sequence > c->reliable_received + window_messages(c)) return NULL;
    assembly *a = find_assembly(c, packet.sequence);
    if (!a) {
      /* Future assemblies cannot occupy the space needed to close the first gap. */
      int64_t reserve = 0;
      for (int64_t sequence = c->reliable_received + 1; sequence < packet.sequence; sequence++)
        if (!find_assembly(c, sequence)) { reserve = c->limits.inbound_message_bytes; break; }
      if (c->received_bytes + packet.total_bytes + reserve > c->limits.queued_reliable_bytes) return NULL;
      a = assembly_new(&packet, now);
      if (c->assembly_count == c->assembly_cap) {
        c->assembly_cap = c->assembly_cap ? c->assembly_cap * 2 : 8;
        c->assemblies = realloc(c->assemblies, c->assembly_cap * sizeof *c->assemblies);
        if (!c->assemblies) qa_range_error("out of memory");
      }
      c->assemblies[c->assembly_count++] = a;
      c->received_bytes += packet.total_bytes;
    }
    if (!append(a, &packet)) return NULL;
    acknowledge(c, packet.sequence, packet.fragment);
    for (;;) {
      assembly *ready = find_assembly(c, c->reliable_received + 1);
      if (!ready || ready->received_count != ready->fragments) break;
      c->reliable_received = ready->sequence;
      for (size_t i = 0; i < c->assembly_count; i++)
        if (c->assemblies[i] == ready) {
          memmove(&c->assemblies[i], &c->assemblies[i + 1], (c->assembly_count - i - 1) * sizeof *c->assemblies);
          c->assembly_count--;
          break;
        }
      c->received_bytes -= (int64_t)ready->length;
      deliver(&delivered, (qa_unified_delivery){"reliable", ready->payload, ready->length, ready->sequence, 0});
      ready->payload = NULL;
      assembly_free(ready);
    }
    if (delivered.count != 0) {
      size_t kept = 0;
      for (size_t i = 0; i < c->ack_count; i++)
        if (c->acks[i].sequence > c->reliable_received) c->acks[kept++] = c->acks[i];
      c->ack_count = kept;
      acknowledge(c, packet.sequence, packet.fragment);
      release_frame(c, &delivered);
    }
  } else {
    if (packet.sequence <= c->frame_received || packet.sequence < c->newest_frame) return NULL;
    if (packet.sequence > c->newest_frame) {
      c->newest_frame = packet.sequence;
      assembly_free(c->frame_assembly);
      c->frame_assembly = NULL;
    }
    if (c->has_waiting_frame && c->waiting_frame.sequence == packet.sequence) return NULL;
    if (!c->frame_assembly) c->frame_assembly = assembly_new(&packet, now);
    assembly *a = c->frame_assembly;
    if (!append(a, &packet)) return NULL;
    if (a->received_count == a->fragments) {
      if (c->has_waiting_frame) free(c->waiting_frame.payload); /* A newer frame replaces the one still waiting. */
      c->waiting_frame = (qa_unified_delivery){"frame", a->payload, a->length, packet.sequence, a->required};
      c->has_waiting_frame = true;
      a->payload = NULL;
      assembly_free(a);
      c->frame_assembly = NULL;
      release_frame(c, &delivered);
    }
  }
  *out_count = delivered.count;
  return delivered.items;
}
static uint8_t *make_packet(const qa_unified_channel *c, const outgoing *message, qa_unified_packet_kind kind, int64_t fragment, size_t *length) {
  int64_t fragment_bytes = c->limits.datagram_bytes - QA_UNIFIED_PACKET_HEADER_BYTES;
  qa_unified_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = kind;
  memcpy(packet.token, c->token, sizeof packet.token);
  packet.sequence = message->sequence;
  packet.acknowledged_reliable = c->reliable_received;
  packet.required_reliable_sequence = message->required;
  packet.total_bytes = (int64_t)message->length;
  packet.fragment_bytes = fragment_bytes;
  packet.fragment = fragment;
  packet.fragments = message->fragments;
  int64_t start = fragment * fragment_bytes;
  int64_t end = start + fragment_bytes;
  if (end > (int64_t)message->length) end = (int64_t)message->length;
  if (start > end) start = end;
  packet.payload = message->payload + start;
  packet.length = (size_t)(end - start);
  uint8_t *bytes = qa_encode_unified_packet(&packet, length);
  if (!bytes) qa_range_error("out of memory");
  return bytes;
}
/* NULL with c->failure set when the retry limit closed the channel. */
static uint8_t *send_reliable(qa_unified_channel *c, double now, size_t *length) {
  size_t count = c->reliable_count < (size_t)window_messages(c) ? c->reliable_count : (size_t)window_messages(c);
  for (size_t checked = 0; checked < count; checked++) {
    size_t index = c->reliable_cursor % count;
    c->reliable_cursor = (index + 1) % count;
    outgoing *message = c->reliable[index];
    int64_t fragment = message->next;
    if (fragment == message->fragments) {
      int64_t retry = -1;
      for (int64_t i = 0; i < message->sent_count; i++) {
        int32_t candidate = message->order[i];
        if (!message->acknowledged[candidate] && now - message->sent_at[candidate] >= (double)c->limits.retry_milliseconds) {
          retry = candidate;
          break;
        }
      }
      if (retry >= 0) fragment = retry;
      else if (index == 0 && message->acknowledged_count == message->fragments) {
        /* Probe only the head for a lost cumulative ACK; buffered successors wait. */
        fragment = message->fragments - 1;
        if (message->attempts[fragment] == 0 || now - message->sent_at[fragment] < (double)c->limits.retry_milliseconds) continue;
      } else continue;
    } else message->next++;
    int32_t attempts = message->attempts[fragment] + 1;
    if (attempts > c->limits.maximum_transmissions) {
      qa_unified_channel_close(c);
      c->failure = "Unified reliable retry limit exceeded";
      return NULL;
    }
    if (message->attempts[fragment] == 0) message->order[message->sent_count++] = (int32_t)fragment;
    message->sent_at[fragment] = now;
    message->attempts[fragment] = attempts;
    return make_packet(c, message, QA_UNIFIED_RELIABLE, fragment, length);
  }
  return NULL;
}
static uint8_t *send_frame(qa_unified_channel *c, size_t *length) {
  outgoing *message = c->frame;
  if (!message) return NULL;
  uint8_t *packet = make_packet(c, message, QA_UNIFIED_FRAME, message->next++, length);
  if (message->next == message->fragments) {
    outgoing_free(message);
    c->frame = c->pending_frame;
    c->pending_frame = NULL;
  }
  return packet;
}
static uint8_t *send_ack(qa_unified_channel *c, size_t *length) {
  if (c->ack_count == 0) return NULL;
  struct qa_unified_ack ack = c->acks[0];
  memmove(c->acks, c->acks + 1, (c->ack_count - 1) * sizeof *c->acks);
  c->ack_count--;
  qa_unified_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_UNIFIED_ACK;
  memcpy(packet.token, c->token, sizeof packet.token);
  packet.sequence = ack.sequence;
  packet.acknowledged_reliable = c->reliable_received;
  packet.fragment = ack.fragment;
  uint8_t *bytes = qa_encode_unified_packet(&packet, length);
  if (!bytes) qa_range_error("out of memory");
  return bytes;
}
uint8_t **qa_unified_channel_flush(qa_unified_channel *c, double now, size_t **out_lengths, size_t *out_count) {
  *out_lengths = NULL;
  *out_count = 0;
  check_open(c);
  check_time(now);
  if (expire(c, now)) return NULL;
  size_t limit = (size_t)c->limits.packets_per_flush;
  uint8_t **packets = xalloc(limit * sizeof *packets);
  size_t *lengths = xalloc(limit * sizeof *lengths);
  size_t count = 0;
  while (count < limit) {
    bool progress = false;
    for (int turn = 0; turn < 3; turn++) {
      if (count == limit) break;
      int lane = c->lane;
      c->lane = (c->lane + 1) % 3;
      size_t length = 0;
      uint8_t *packet = lane == 0 ? send_ack(c, &length) : lane == 1 ? send_reliable(c, now, &length) : send_frame(c, &length);
      if (c->failure) {
        /* The TS throw discards what this flush had built. */
        for (size_t i = 0; i < count; i++) free(packets[i]);
        free(packets); free(lengths);
        return NULL;
      }
      if (packet) { packets[count] = packet; lengths[count] = length; count++; progress = true; }
    }
    if (!progress) break;
  }
  if (count == 0) { free(packets); free(lengths); return NULL; }
  *out_lengths = lengths;
  *out_count = count;
  return packets;
}
