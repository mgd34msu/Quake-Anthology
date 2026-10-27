/* Port of src/network/q2/channel.ts. GPL-2.0-or-later. */
#include "qa/network/q2/channel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/zpacket.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

static bool is_integer(double v) { return v == trunc(v) && isfinite(v); }
static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}
static bool recording_q2pro(const qa_q2_channel *c) {
  return c->options.has_sequence_recording ? c->options.sequence_q2pro
                                            : c->options.protocol.version != 34;
}
static bool classic_id(const qa_q2_channel *c) {
  return !c->options.channel_new && !recording_q2pro(c);
}

void qa_q2_channel_receive_result_free(qa_q2_channel_receive_result *receive) {
  free(receive->bytes);
  memset(receive, 0, sizeof *receive);
}

void qa_q2_channel_init(qa_q2_channel *channel, const qa_q2_channel_options *options) {
  memset(channel, 0, sizeof *channel);
  channel->options = *options;
  channel->outgoing = 1;
  if (kind_is(&options->protocol, "q2-kex")) {
    channel->payload_bytes = 65527;
    channel->capacity = 65527;
    channel->receiving_capacity = 65527;
    return;
  }
  double datagram = options->has_max_datagram_bytes ? options->max_datagram_bytes : 65507;
  if (!is_integer(datagram) || datagram < 524 || datagram > 65507) {
    qa_range_error("Invalid Q2 transport datagram limit");
  }
  double payload = options->has_payload_bytes ? options->payload_bytes : 1390;
  double by_datagram = datagram - 12;
  channel->payload_bytes = payload < by_datagram ? payload : by_datagram;
  double capacity = options->has_message_bytes
                        ? options->message_bytes
                        : options->channel_new
                              ? 32768
                              : recording_q2pro(channel) ? channel->payload_bytes : 1384;
  channel->capacity = options->channel_new
                          ? capacity
                          : (capacity < datagram - 10 ? capacity : datagram - 10);
  if (!is_integer(channel->payload_bytes) || channel->payload_bytes < 512 ||
      channel->payload_bytes > 4086 || !is_integer(channel->capacity) ||
      channel->capacity < 1 || channel->capacity > 32768) {
    qa_range_error("Invalid Q2 channel limits");
  }
  if (!is_integer(options->qport) || options->qport < 0 || options->qport > 65535) {
    qa_range_error("Invalid Q2 qport");
  }
  channel->receiving_capacity = 32768;
}

void qa_q2_channel_destroy(qa_q2_channel *channel) {
  free(channel->queued);
  free(channel->reliable);
  free(channel->sending_bytes);
  memset(channel, 0, sizeof *channel);
}

int64_t qa_q2_channel_get_incoming_sequence(const qa_q2_channel *channel) {
  return channel->incoming;
}
int64_t qa_q2_channel_get_incoming_acknowledged(const qa_q2_channel *channel) {
  return channel->incoming_ack;
}
int64_t qa_q2_channel_get_outgoing_sequence(const qa_q2_channel *channel) {
  return channel->outgoing;
}
bool qa_q2_channel_get_fragment_pending(const qa_q2_channel *channel) {
  return channel->has_sending;
}
bool qa_q2_channel_get_reliable_pending(const qa_q2_channel *channel) {
  return channel->queued_length > 0 || channel->reliable_length > 0;
}
bool qa_q2_channel_get_acknowledgement_pending(const qa_q2_channel *channel) {
  return channel->ack_pending;
}
bool qa_q2_channel_get_can_reliable(const qa_q2_channel *channel) {
  return channel->reliable_length == 0;
}
bool qa_q2_channel_should_update(const qa_q2_channel *channel, double now_milliseconds) {
  return channel->queued_length > 0 || channel->ack_pending || channel->has_sending ||
         now_milliseconds - channel->last_sent_milliseconds > 1000;
}

void qa_q2_channel_queue_reliable(qa_q2_channel *channel, const uint8_t *bytes,
                                  size_t length) {
  if ((double)(channel->queued_length + length) > channel->capacity) {
    qa_range_error("Q2 reliable message overflow");
  }
  uint8_t *joined = (uint8_t *)malloc(channel->queued_length + length > 0
                                          ? channel->queued_length + length
                                          : 1);
  if (joined == NULL) qa_range_error("out of memory");
  if (channel->queued_length > 0) memcpy(joined, channel->queued, channel->queued_length);
  if (length > 0) memcpy(joined + channel->queued_length, bytes, length);
  free(channel->queued);
  channel->queued = joined;
  channel->queued_length += length;
}

static int qport_bytes(const qa_q2_channel *c) {
  return !c->options.channel_new && c->options.protocol.version < 35
             ? 2
             : c->options.qport == 0 ? 0 : 1;
}

static bool header_write(qa_q2_channel *c, bool reliable, bool fragmented,
                         qa_q2_sizebuf *message) {
  double cap = classic_id(c) ? 1400 : 4096;
  double limit = c->options.has_max_datagram_bytes ? c->options.max_datagram_bytes : 4096;
  double size = cap < limit ? cap : limit;
  if (!qa_q2_message_create(message, (int64_t)size)) return false;
  int64_t mask = c->options.channel_new ? 0x3fffffff : 0x7fffffff;
  uint32_t seq = (uint32_t)((uint64_t)c->outgoing & (uint64_t)mask) |
                 (reliable ? 0x80000000u : 0u) | (fragmented ? 0x40000000u : 0u);
  uint32_t ack = (uint32_t)((uint64_t)c->incoming & (uint64_t)mask) |
                 (c->incoming_reliable ? 0x80000000u : 0u);
  qa_q2_msg_write_long(message, (double)seq);
  qa_q2_msg_write_long(message, (double)ack);
  if (!c->options.side_server) {
    int width = qport_bytes(c);
    if (width == 2) qa_q2_msg_write_short(message, c->options.qport);
    else if (width == 1) qa_q2_msg_write_byte(message, c->options.qport);
  }
  return true;
}

uint8_t *qa_q2_channel_next_fragment(qa_q2_channel *channel, double now_milliseconds,
                                     size_t *out_length) {
  if (!channel->has_sending) return NULL;
  qa_q2_sizebuf packet;
  if (!header_write(channel, channel->sending_reliable, true, &packet)) return NULL;
  int64_t room = packet.maxsize - packet.cursize - 2;
  double writable_d = channel->payload_bytes < (double)room ? channel->payload_bytes
                                                             : (double)room;
  size_t writable = writable_d > 0 ? (size_t)writable_d : 0;
  size_t end = channel->sending_offset + writable;
  if (end > channel->sending_length) end = channel->sending_length;
  bool more = end < channel->sending_length;
  qa_q2_msg_write_short(&packet,
                        (double)((int64_t)channel->sending_offset | (more ? 0x8000 : 0)));
  qa_q2_sz_write(&packet, channel->sending_bytes + channel->sending_offset,
                 channel->sending_length - channel->sending_offset,
                 (int64_t)(end - channel->sending_offset));
  channel->sending_offset = end;
  if (!more) {
    channel->has_sending = false;
    free(channel->sending_bytes);
    channel->sending_bytes = NULL;
    channel->sending_length = 0;
    channel->outgoing++;
    channel->last_sent_milliseconds = now_milliseconds;
  }
  uint8_t *out = qa_q2_message_bytes(&packet, out_length);
  qa_q2_message_destroy(&packet);
  return out;
}

static uint8_t *kex_packet(qa_q2_channel *c, const uint8_t *payload, size_t length,
                           bool reliable, double now, size_t *out_length) {
  if ((double)length > c->capacity) qa_range_error("KEX game message overflow");
  uint8_t *bytes = (uint8_t *)malloc(length + 8);
  if (bytes == NULL) return NULL;
  uint32_t w0 = reliable ? 0x80000000u : (uint32_t)((uint64_t)(c->outgoing++) & 0x7fffffffu);
  uint32_t w1 = reliable ? 0x80000000u : (uint32_t)((uint64_t)c->incoming & 0x7fffffffu);
  for (int i = 0; i < 4; i++) bytes[i] = (uint8_t)((w0 >> (8 * i)) & 0xffu);
  for (int i = 0; i < 4; i++) bytes[4 + i] = (uint8_t)((w1 >> (8 * i)) & 0xffu);
  if (length > 0) memcpy(bytes + 8, payload, length);
  c->last_sent_milliseconds = now;
  if (out_length != NULL) *out_length = length + 8;
  return bytes;
}

uint8_t *qa_q2_channel_transmit(qa_q2_channel *channel, const uint8_t *unreliable,
                                size_t length, double now_milliseconds,
                                size_t *out_length) {
  if (kind_is(&channel->options.protocol, "q2-kex")) {
    return kex_packet(channel, unreliable, length, false, now_milliseconds, out_length);
  }
  uint8_t *next = qa_q2_channel_next_fragment(channel, now_milliseconds, out_length);
  if (next != NULL) return next;
  bool send_reliable = channel->incoming_ack > channel->last_reliable &&
                       channel->incoming_reliable_ack != channel->reliable_bit;
  if (channel->reliable_length == 0 && channel->queued_length > 0) {
    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    if (channel->options.side_server && channel->options.compress) {
      compressed = qa_q2_try_wrap_zpacket(channel->queued, channel->queued_length,
                                          (int32_t)channel->queued_length,
                                          (int32_t)channel->capacity, &compressed_len);
    }
    if (compressed != NULL && (kind_is(&channel->options.protocol, "q2-rerelease") ||
                               kind_is(&channel->options.protocol, "q2-private-classic"))) {
      compressed[0] = 34;
    }
    free(channel->reliable);
    if (compressed != NULL) {
      channel->reliable = compressed;
      channel->reliable_length = compressed_len;
      free(channel->queued);
    } else {
      channel->reliable = channel->queued;
      channel->reliable_length = channel->queued_length;
    }
    channel->queued = NULL;
    channel->queued_length = 0;
    channel->reliable_bit ^= 1;
    send_reliable = true;
  }
  const uint8_t *reliable = send_reliable ? channel->reliable : NULL;
  size_t reliable_len = send_reliable ? channel->reliable_length : 0;
  if (channel->options.channel_new && (double)(reliable_len + length) > channel->payload_bytes) {
    bool include = (double)(reliable_len + length) <= (double)channel->receiving_capacity;
    size_t total = reliable_len + (include ? length : 0);
    uint8_t *bytes = (uint8_t *)malloc(total > 0 ? total : 1);
    if (bytes == NULL) return NULL;
    if (reliable_len > 0) memcpy(bytes, reliable, reliable_len);
    if (include && length > 0) memcpy(bytes + reliable_len, unreliable, length);
    channel->has_sending = true;
    channel->sending_bytes = bytes;
    channel->sending_length = total;
    channel->sending_reliable = send_reliable;
    channel->sending_offset = 0;
    if (send_reliable) channel->last_reliable = channel->outgoing;
    uint8_t *fragment = qa_q2_channel_next_fragment(channel, now_milliseconds, out_length);
    if (fragment == NULL) qa_q2_throw_error("Q2 fragment disappeared");
    return fragment;
  }
  qa_q2_sizebuf packet;
  if (!header_write(channel, send_reliable, false, &packet)) return NULL;
  qa_q2_sz_write(&packet, reliable, reliable_len, (int64_t)reliable_len);
  if (packet.maxsize - packet.cursize >= (int64_t)length) {
    qa_q2_sz_write(&packet, unreliable, length, (int64_t)length);
  }
  if (send_reliable) {
    channel->last_reliable = channel->outgoing + (recording_q2pro(channel) ? 0 : 1);
  }
  channel->outgoing++;
  channel->ack_pending = false;
  channel->last_sent_milliseconds = now_milliseconds;
  uint8_t *out = qa_q2_message_bytes(&packet, out_length);
  qa_q2_message_destroy(&packet);
  return out;
}

static bool set_rejected(qa_q2_channel_receive_result *out, const char *reason) {
  out->kind = QA_Q2_CHANNEL_REJECTED;
  out->reason = reason;
  return true;
}

bool qa_q2_channel_receive(qa_q2_channel *channel, const uint8_t *bytes, size_t length,
                           double now_milliseconds, qa_q2_channel_receive_result *out) {
  memset(out, 0, sizeof *out);
  if (kind_is(&channel->options.protocol, "q2-kex")) {
    if (length < 8) return set_rejected(out, "short");
    uint32_t seq_word = 0;
    uint32_t ack_word = 0;
    for (int i = 0; i < 4; i++) seq_word |= (uint32_t)bytes[i] << (8 * i);
    for (int i = 0; i < 4; i++) ack_word |= (uint32_t)bytes[4 + i] << (8 * i);
    if (seq_word == 0x80000000u && ack_word == 0x80000000u) {
      channel->last_received_milliseconds = now_milliseconds;
      out->kind = QA_Q2_CHANNEL_MESSAGE;
      out->sequence = (double)channel->incoming;
      out->acknowledged = (double)channel->incoming_ack;
      out->dropped = 0;
      out->bytes_length = length - 8;
      out->bytes = (uint8_t *)malloc(out->bytes_length > 0 ? out->bytes_length : 1);
      if (out->bytes == NULL) return false;
      if (out->bytes_length > 0) memcpy(out->bytes, bytes + 8, out->bytes_length);
      return true;
    }
    int64_t sequence = (int64_t)(seq_word & 0x7fffffffu);
    int64_t acknowledged = (int64_t)(ack_word & 0x7fffffffu);
    if (sequence <= channel->incoming) return set_rejected(out, "sequence");
    int64_t dropped = sequence - channel->incoming - 1;
    channel->incoming = sequence;
    channel->incoming_ack = acknowledged;
    channel->last_received_milliseconds = now_milliseconds;
    out->kind = QA_Q2_CHANNEL_MESSAGE;
    out->sequence = (double)sequence;
    out->acknowledged = (double)acknowledged;
    out->dropped = (double)dropped;
    out->bytes_length = length - 8;
    out->bytes = (uint8_t *)malloc(out->bytes_length > 0 ? out->bytes_length : 1);
    if (out->bytes == NULL) return false;
    if (out->bytes_length > 0) memcpy(out->bytes, bytes + 8, out->bytes_length);
    return true;
  }
  qa_q2_sizebuf packet;
  qa_q2_sizebuf_init_empty(&packet);
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) return false;
  if (length > 0) memcpy(copy, bytes, length);
  qa_q2_message_load(&packet, copy, length);
  int32_t sequence_word = qa_q2_msg_read_long(&packet);
  int32_t ack_word = qa_q2_msg_read_long(&packet);
  if (channel->options.side_server) {
    int width = qport_bytes(channel);
    int32_t qport = width == 2 ? qa_q2_msg_read_word(&packet)
                    : width == 1 ? qa_q2_msg_read_byte(&packet)
                                 : 0;
    int32_t expected = width == 2 ? (int32_t)channel->options.qport
                                  : (int32_t)channel->options.qport & 255;
    if (packet.readcount > packet.cursize) {
      free(copy);
      return set_rejected(out, "short");
    }
    if (qport != expected) {
      free(copy);
      return set_rejected(out, "qport");
    }
  }
  int64_t mask = channel->options.channel_new ? 0x3fffffff : 0x7fffffff;
  int64_t sequence = (int64_t)(int32_t)sequence_word & mask;
  int64_t acknowledged = (int64_t)(int32_t)ack_word & mask;
  int reliable = (int)((uint32_t)sequence_word >> 31);
  int reliable_ack = (int)((uint32_t)ack_word >> 31);
  bool fragmented = channel->options.channel_new && ((uint32_t)sequence_word & 0x40000000u) != 0;
  int32_t fragment_word = fragmented ? qa_q2_msg_read_word(&packet) : 0;
  if (packet.readcount > packet.cursize) {
    free(copy);
    return set_rejected(out, "short");
  }
  if (sequence <= channel->incoming) {
    free(copy);
    return set_rejected(out, "sequence");
  }
  channel->incoming_reliable_ack = reliable_ack;
  if (reliable_ack == channel->reliable_bit) {
    free(channel->reliable);
    channel->reliable = NULL;
    channel->reliable_length = 0;
  }
  size_t payload_len = length > (size_t)packet.readcount ? length - (size_t)packet.readcount : 0;
  const uint8_t *payload = copy + (packet.readcount > 0 ? (size_t)packet.readcount : 0);
  uint8_t *result = NULL;
  size_t result_len = 0;
  if (fragmented) {
    if (sequence != channel->receive_sequence) {
      channel->receive_sequence = sequence;
      channel->receive_length = 0;
    }
    int32_t offset = fragment_word & 0x7fff;
    if ((size_t)offset != channel->receive_length) {
      free(copy);
      return set_rejected(out, "fragment-order");
    }
    if (channel->receive_length + payload_len > channel->receiving_capacity) {
      free(copy);
      return set_rejected(out, "fragment-size");
    }
    if (payload_len > 0) memcpy(channel->receiving + channel->receive_length, payload, payload_len);
    channel->receive_length += payload_len;
    if ((fragment_word & 0x8000) != 0) {
      out->kind = QA_Q2_CHANNEL_FRAGMENT;
      out->sequence = (double)sequence;
      out->received_bytes = (double)channel->receive_length;
      free(copy);
      return true;
    }
    result_len = channel->receive_length;
    result = (uint8_t *)malloc(result_len > 0 ? result_len : 1);
    if (result == NULL) {
      free(copy);
      return false;
    }
    if (result_len > 0) memcpy(result, channel->receiving, result_len);
    channel->receive_length = 0;
  } else {
    result_len = payload_len;
    result = (uint8_t *)malloc(result_len > 0 ? result_len : 1);
    if (result == NULL) {
      free(copy);
      return false;
    }
    if (result_len > 0) memcpy(result, payload, result_len);
  }
  int64_t dropped = sequence - channel->incoming - 1;
  channel->incoming = sequence;
  channel->incoming_ack = acknowledged;
  if (reliable) {
    channel->ack_pending = true;
    channel->incoming_reliable ^= 1;
  }
  channel->last_received_milliseconds = now_milliseconds;
  out->kind = QA_Q2_CHANNEL_MESSAGE;
  out->sequence = (double)sequence;
  out->acknowledged = (double)acknowledged;
  out->dropped = (double)dropped;
  out->bytes = result;
  out->bytes_length = result_len;
  free(copy);
  return true;
}

bool qa_q2_channel_send(qa_q2_channel *channel, qa_datagram_transport *transport,
                        const qa_net_address *to, const uint8_t *unreliable,
                        size_t length, double now_milliseconds) {
  if (kind_is(&channel->options.protocol, "q2-kex")) {
    bool sent = true;
    if (channel->queued_length != 0) {
      size_t packet_len = 0;
      uint8_t *packet = kex_packet(channel, channel->queued, channel->queued_length,
                                   true, now_milliseconds, &packet_len);
      if (packet == NULL) return false;
      sent = transport->send(transport->ctx, to, packet, packet_len);
      free(packet);
      if (sent) {
        free(channel->queued);
        channel->queued = NULL;
        channel->queued_length = 0;
      }
    }
    if (length != 0) {
      size_t packet_len = 0;
      uint8_t *packet = kex_packet(channel, unreliable, length, false,
                                   now_milliseconds, &packet_len);
      if (packet == NULL) return false;
      bool this_sent = transport->send(transport->ctx, to, packet, packet_len);
      free(packet);
      sent = this_sent && sent;
    }
    return sent;
  }
  size_t packet_len = 0;
  uint8_t *packet = qa_q2_channel_transmit(channel, unreliable, length,
                                           now_milliseconds, &packet_len);
  if (packet == NULL) return false;
  bool sent = transport->send(transport->ctx, to, packet, packet_len);
  free(packet);
  return sent;
}
