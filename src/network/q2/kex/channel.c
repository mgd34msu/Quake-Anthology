/* Port of src/network/q2/kex/channel.ts. GPL-2.0-or-later. */
#include "qa/network/q2/kex/channel.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/network/q2/codecs/zpacket.h"
#include "qa/foundation/support.h"
#include <zlib.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/kex/packet.h"

void qa_kex_message_free(qa_kex_message *message) {
  free(message->payload);
  memset(message, 0, sizeof *message);
}

static const qa_kex_pending_packet *pending_head(qa_kex_channel *channel) {
  return (const qa_kex_pending_packet *)qa_event_ring_peek(&channel->pending,
                                                           &channel->pending_cursor);
}

bool qa_kex_channel_init(qa_kex_channel *channel, qa_kex_emit_fn emit, void *ctx) {
  memset(channel, 0, sizeof *channel);
  channel->emit = emit;
  channel->emit_ctx = ctx;
  if (!qa_event_ring_init(&channel->pending, 8192, sizeof(qa_kex_pending_packet))) {
    return false;
  }
  if (!qa_event_ring_open_cursor(&channel->pending, &channel->pending_cursor, false)) {
    qa_event_ring_destroy(&channel->pending);
    memset(channel, 0, sizeof *channel);
    return false;
  }
  channel->pending_ready = true;
  return true;
}

void qa_kex_channel_destroy(qa_kex_channel *channel) {
  if (channel->pending_ready) {
    const qa_kex_pending_packet *head;
    while ((head = pending_head(channel)) != NULL) {
      uint8_t *bytes = head->bytes;
      (void)qa_event_ring_read(&channel->pending, &channel->pending_cursor);
      free(bytes);
    }
    qa_event_ring_close_cursor(&channel->pending, &channel->pending_cursor);
    qa_event_ring_destroy(&channel->pending);
  }
  free(channel->fragment_bytes);
  memset(channel, 0, sizeof *channel);
}

static uint8_t *deflate_all(const uint8_t *payload, size_t length, size_t *out_length) {
  uLongf bound = compressBound((uLong)length);
  uint8_t *out = (uint8_t *)malloc(bound > 0 ? bound : 1);
  if (out == NULL) return NULL;
  if (compress2(out, &bound, payload, (uLong)length, Z_DEFAULT_COMPRESSION) != Z_OK) {
    free(out);
    qa_q2_throw_error("KEX compression failed");
  }
  *out_length = (size_t)bound;
  return out;
}

/* TS inflateSync(bytes, { maxOutputLength: KEX_MESSAGE_BYTES }); its errors propagate as is. */
static uint8_t *inflate_all(const uint8_t *bytes, size_t length, size_t *out_length) {
  return qa_q2_inflate_sync(bytes, length, false, QA_KEX_MESSAGE_BYTES, Z_FINISH, out_length);
}

bool qa_kex_channel_send(qa_kex_channel *channel, double kind, const uint8_t *payload,
                         size_t length, double mode, double now) {
  if ((double)length > QA_KEX_MESSAGE_BYTES || kind != trunc(kind) || kind < 0 || kind > 255) {
    qa_q2_throw_error("Invalid KEX message");
  }
  uint8_t *data = (uint8_t *)malloc(length > 0 ? length : 1);
  if (data == NULL) return false;
  if (length > 0) memcpy(data, payload, length);
  size_t data_len = length;
  double message_kind = kind;
  if (kind < 127 && length >= 128) {
    size_t compressed_len = 0;
    uint8_t *compressed = deflate_all(payload, length, &compressed_len);
    if (compressed == NULL) {
      free(data);
      return false;
    }
    if (compressed_len + 1 < length) {
      free(data);
      data = (uint8_t *)malloc(compressed_len + 1);
      if (data == NULL) {
        free(compressed);
        return false;
      }
      data[0] = (uint8_t)kind;
      memcpy(data + 1, compressed, compressed_len);
      data_len = compressed_len + 1;
      message_kind = 127;
    }
    free(compressed);
  }
  bool fragmented = (double)data_len + (mode == 0 ? 3 : 7) > QA_KEX_DATAGRAM_BYTES;
  if (fragmented && mode == 0) {
    free(data);
    qa_q2_throw_error("Unsequenced KEX message cannot be fragmented");
  }
  if (mode == 3 && channel->pending_bytes + (double)data_len +
                       ceil((double)data_len / 1393.0) * 7.0 >
                       (double)QA_KEX_MESSAGE_BYTES * 2.0) {
    free(data);
    qa_q2_throw_error("KEX reliable queue is full");
  }
  size_t position = 0;
  bool first = true;
  bool accepted = true;
  do {
    size_t capacity = (size_t)(QA_KEX_DATAGRAM_BYTES - (mode == 0 ? 2 : 6) - (first ? 1 : 0));
    size_t end = position + capacity;
    if (end > data_len) end = data_len;
    bool final = end == data_len;
    int32_t fragment_flags = !fragmented ? 0 : first ? 4 : final ? 12 : 8;
    if (mode != 0) channel->sequence = (channel->sequence + 1) & 65535;
    if (mode == 3) channel->reliable = (channel->reliable + 1) & 65535;
    qa_kex_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.flags = mode + (double)fragment_flags;
    packet.sequence = (double)channel->sequence;
    packet.reliable = (double)channel->reliable;
    packet.kind = first ? (int32_t)message_kind : -1;
    packet.payload = data + position;
    packet.payload_length = end - position;
    size_t bytes_len = 0;
    uint8_t *bytes = qa_kex_write_packet(&packet, &bytes_len);
    if (bytes == NULL) {
      free(data);
      return false;
    }
    if (mode == 3) {
      if (pending_head(channel) == NULL) {
        channel->retry_at = now;
        channel->retries = 0;
      }
      uint8_t *kept = (uint8_t *)malloc(bytes_len > 0 ? bytes_len : 1);
      if (kept == NULL) {
        free(bytes);
        free(data);
        return false;
      }
      if (bytes_len > 0) memcpy(kept, bytes, bytes_len);
      qa_kex_pending_packet entry = {(double)channel->reliable, kept, bytes_len};
      (void)qa_event_ring_admit(&channel->pending, &entry);
      channel->pending_bytes += (double)bytes_len;
    }
    bool this_accepted = channel->emit(channel->emit_ctx, bytes, bytes_len);
    accepted = this_accepted && accepted;
    free(bytes);
    position = end;
    first = false;
  } while (position < data_len);
  free(data);
  return accepted;
}

static bool expand_message(double kind, uint8_t *bytes, size_t length,
                           qa_kex_message *out) {
  if (kind != 127) {
    out->kind = kind;
    out->payload = bytes;
    out->payload_length = length;
    return true;
  }
  if (length < 1) {
    free(bytes);
    qa_q2_throw_error("Invalid compressed KEX game kind");
  }
  int32_t original = bytes[0];
  if (original >= 127) {
    free(bytes);
    qa_q2_throw_error("Invalid compressed KEX game kind");
  }
  size_t inflated_len = 0;
  qa_cleanup node;
  qa_cleanup_push(&node, free, bytes);
  uint8_t *inflated = inflate_all(bytes + 1, length - 1, &inflated_len);
  qa_cleanup_pop(&node);
  free(bytes);
  if (inflated == NULL) return false;
  out->kind = (double)original;
  out->payload = inflated;
  out->payload_length = inflated_len;
  return true;
}

static bool channel_receive(qa_kex_channel *channel, qa_kex_packet *held, double now,
                            qa_kex_message *out);
static void packet_release(void *arg) { qa_kex_packet_free((qa_kex_packet *)arg); }
bool qa_kex_channel_receive(qa_kex_channel *channel, const uint8_t *bytes, size_t length,
                            double now, qa_kex_message *out) {
  qa_kex_packet packet;
  if (!qa_kex_read_packet(bytes, length, &packet)) return false;
  /* The packet's payload stays owned here until the body hands it on (and clears it). */
  qa_cleanup node;
  qa_cleanup_push(&node, packet_release, &packet);
  bool result = channel_receive(channel, &packet, now, out);
  qa_cleanup_pop(&node);
  qa_kex_packet_free(&packet);
  return result;
}
static bool channel_receive(qa_kex_channel *channel, qa_kex_packet *held, double now,
                            qa_kex_message *out) {
#define packet (*held)
  bool reliable = (qa_js_to_int32(packet.flags) & 1) != 0;
  bool sequential = (qa_js_to_int32(packet.flags) & 2) != 0;
  if (packet.kind == 130 && packet.flags == 0) {
    if (packet.payload_length != 3) {
      qa_kex_packet_free(&packet);
      qa_q2_throw_error("Invalid KEX acknowledgment");
    }
    int64_t acknowledgment =
        (int64_t)(((uint32_t)packet.payload[1] << 8) | packet.payload[2]);
    const qa_kex_pending_packet *head = pending_head(channel);
    while (head != NULL && (int64_t)head->reliable <= acknowledgment) {
      uint8_t *kept = head->bytes;
      double kept_len = (double)head->bytes_length;
      (void)qa_event_ring_read(&channel->pending, &channel->pending_cursor);
      free(kept);
      channel->pending_bytes -= kept_len;
      head = pending_head(channel);
      channel->retry_at = now;
      channel->retries = 0;
    }
    qa_event_ring_retire(&channel->pending);
    channel->received_at = now;
    qa_kex_packet_free(&packet);
    return false;
  }
  if (reliable) {
    channel->acknowledgment |= 4;
    if ((int64_t)packet.reliable != ((channel->incoming_reliable + 1) & 65535)) {
      channel->acknowledgment |= 2;
      qa_kex_packet_free(&packet);
      return false;
    }
    channel->incoming_reliable = (int64_t)packet.reliable;
    channel->acknowledgment &= ~2;
  } else if (sequential) {
    int64_t sequence = (int64_t)packet.sequence;
    if (sequence == channel->incoming_sequence) {
      qa_kex_packet_free(&packet);
      return false;
    }
    uint32_t comparison = (uint32_t)channel->incoming_sequence;
    if (((sequence ^ comparison) & 32768) != 0 && (sequence & 32768) != 0) {
      comparison = (comparison << 16) & 0xffffffffu;
    } else if (sequence >= (int64_t)comparison) {
      comparison = (uint32_t)sequence + 16384u;
    }
    if (((comparison - (uint32_t)sequence) & 0xffffffffu) < 16384u ||
        (int64_t)packet.reliable != channel->incoming_reliable) {
      qa_kex_packet_free(&packet);
      return false;
    }
  }
  if (sequential) channel->incoming_sequence = (int64_t)packet.sequence;
  channel->received_at = now;
  int32_t fragment = qa_js_to_int32(packet.flags) & 12;
  if (fragment == 0) {
    if (packet.kind < 0) {
      qa_kex_packet_free(&packet);
      qa_q2_throw_error("Missing KEX kind");
    }
    double kind = (double)packet.kind;
    uint8_t *payload = packet.payload;
    size_t payload_len = packet.payload_length;
    packet.payload = NULL;
    qa_kex_packet_free(&packet);
    memset(out, 0, sizeof *out);
    return expand_message(kind, payload, payload_len, out);
  }
  if (fragment == 4) {
    if (packet.kind < 0) {
      qa_kex_packet_free(&packet);
      qa_q2_throw_error("Missing KEX fragment kind");
    }
    free(channel->fragment_bytes);
    channel->has_fragments = true;
    channel->fragment_kind = (double)packet.kind;
    channel->fragment_sequence = (int64_t)packet.sequence;
    channel->fragment_bytes = packet.payload;
    channel->fragment_length = packet.payload_length;
    packet.payload = NULL;
    qa_kex_packet_free(&packet);
    return false;
  }
  if (!channel->has_fragments ||
      (int64_t)packet.sequence != ((channel->fragment_sequence + 1) & 65535)) {
    channel->has_fragments = false;
    free(channel->fragment_bytes);
    channel->fragment_bytes = NULL;
    channel->fragment_length = 0;
    qa_kex_packet_free(&packet);
    return false;
  }
  size_t joined_len = channel->fragment_length + packet.payload_length;
  if (joined_len > QA_KEX_MESSAGE_BYTES) {
    channel->has_fragments = false;
    free(channel->fragment_bytes);
    channel->fragment_bytes = NULL;
    channel->fragment_length = 0;
    qa_kex_packet_free(&packet);
    qa_q2_throw_error("KEX fragmented message exceeds limit");
  }
  uint8_t *joined = (uint8_t *)malloc(joined_len > 0 ? joined_len : 1);
  if (joined == NULL) {
    qa_kex_packet_free(&packet);
    return false;
  }
  if (channel->fragment_length > 0) memcpy(joined, channel->fragment_bytes, channel->fragment_length);
  if (packet.payload_length > 0) {
    memcpy(joined + channel->fragment_length, packet.payload, packet.payload_length);
  }
  double kind = channel->fragment_kind;
  if (fragment == 8) {
    free(channel->fragment_bytes);
    channel->fragment_bytes = joined;
    channel->fragment_length = joined_len;
    channel->fragment_sequence = (int64_t)packet.sequence;
    qa_kex_packet_free(&packet);
    return false;
  }
  channel->has_fragments = false;
  free(channel->fragment_bytes);
  channel->fragment_bytes = NULL;
  channel->fragment_length = 0;
  qa_kex_packet_free(&packet);
  memset(out, 0, sizeof *out);
  return expand_message(kind, joined, joined_len, out);
#undef packet
}

bool qa_kex_channel_tick(qa_kex_channel *channel, double now) {
  if (channel->acknowledgment != 0) {
    uint8_t ack_bytes[3];
    ack_bytes[0] = (channel->acknowledgment & 2) != 0 ? 1 : 0;
    ack_bytes[1] = (uint8_t)((channel->incoming_reliable >> 8) & 0xff);
    ack_bytes[2] = (uint8_t)(channel->incoming_reliable & 0xff);
    qa_kex_packet packet;
    memset(&packet, 0, sizeof packet);
    packet.flags = 0;
    packet.kind = 130;
    packet.payload = ack_bytes;
    packet.payload_length = 3;
    size_t bytes_len = 0;
    uint8_t *bytes = qa_kex_write_packet(&packet, &bytes_len);
    if (bytes != NULL) {
      (void)channel->emit(channel->emit_ctx, bytes, bytes_len);
      free(bytes);
    }
    channel->acknowledgment = 0;
  }
  const qa_kex_pending_packet *first = pending_head(channel);
  if (first != NULL && now - channel->retry_at >= 500) {
    if (++channel->retries >= 40) return false; /* TS timeout throw */
    channel->retry_at = now;
    (void)channel->emit(channel->emit_ctx, first->bytes, first->bytes_length);
    first = pending_head(channel);
  }
  if (now - channel->received_at >= 5000 && first == NULL) {
    (void)qa_kex_channel_send(channel, 129, NULL, 0, 3, now);
    channel->received_at = now;
  }
  return true;
}
