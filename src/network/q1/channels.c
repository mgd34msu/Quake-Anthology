#include "qa/network_q1_channel.h"
#include "qa/network_q1_save.h"
#include "qa/network_q1_peer_save.h"
#include "qa/network_reliability_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_nq_channel {
    qa_net_stopwait *reliable;
    size_t message_bytes, wire_capacity;
    uint32_t unreliable_send;
    uint64_t unreliable_receive;
    uint8_t *wire;
};
struct qa_qw_channel {
    qa_net_toggle *reliable;
    qa_net_rate rate;
    qa_q1_channel_side side;
    uint16_t qport;
    size_t capacity;
    uint8_t *wire;
    uint64_t last_received_ns;
    double frame_latency, frame_interval_ms;
};

static bool invalid(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static uint32_t read_be32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}
static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24); bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8); bytes[3] = (uint8_t)value;
}
static bool nq_packet(qa_nq_channel *channel, uint32_t flags, uint32_t sequence,
                       qa_bytes payload, qa_bytes *out, qa_error *error) {
    if (!out || payload.size > channel->wire_capacity - 8 || (payload.size && !payload.data))
        return invalid(error, "NetQuake datagram exceeds channel capacity");
    if (payload.size) memmove(channel->wire + 8, payload.data, payload.size);
    write_be32(channel->wire, flags | (uint32_t)(payload.size + 8));
    write_be32(channel->wire + 4, sequence);
    *out = (qa_bytes){channel->wire, payload.size + 8};
    return true;
}

bool qa_nq_channel_create(size_t message_bytes, size_t fragment_bytes,
                           qa_nq_channel **out, qa_error *error) {
    if (!out || !message_bytes || !fragment_bytes || fragment_bytes > 65527 || fragment_bytes > message_bytes)
        return invalid(error, "Invalid NetQuake channel capacity");
    qa_nq_channel *channel = calloc(1, sizeof(*channel));
    if (!channel) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate NetQuake channel"); return false; }
    channel->wire_capacity = (message_bytes < 65527 ? message_bytes : 65527) + 8;
    channel->message_bytes = message_bytes;
    channel->wire = malloc(channel->wire_capacity);
    if (!channel->wire) {
        free(channel); qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate NetQuake wire buffer"); return false;
    }
    if (!qa_net_stopwait_create(message_bytes, fragment_bytes, UINT64_C(1000000000), &channel->reliable, error)) {
        free(channel->wire); free(channel); return false;
    }
    *out = channel;
    return true;
}
void qa_nq_channel_destroy(qa_nq_channel *channel) {
    if (!channel) return;
    qa_net_stopwait_destroy(channel->reliable); free(channel->wire); free(channel);
}
bool qa_nq_channel_ready(const qa_nq_channel *channel) {
    return channel && qa_net_stopwait_ready(channel->reliable);
}
uint32_t qa_nq_channel_unreliable_sequence(const qa_nq_channel *channel) {
    return channel ? channel->unreliable_send : 0;
}
bool qa_nq_channel_queue(qa_nq_channel *channel, qa_bytes payload, qa_error *error) {
    return channel ? qa_net_stopwait_begin(channel->reliable, payload, error) : invalid(error, "Missing NetQuake channel");
}
bool qa_nq_channel_next(qa_nq_channel *channel, uint64_t now_ns, bool *present,
                         qa_bytes *out, qa_error *error) {
    if (!channel || !present || !out) return invalid(error, "Invalid NetQuake send arguments");
    *out = (qa_bytes){0};
    qa_net_reliable_fragment fragment;
    if (!qa_net_stopwait_next(channel->reliable, now_ns, present, &fragment, error)) return false;
    return !*present || nq_packet(channel, QA_NQ_FLAG_DATA | (fragment.final ? QA_NQ_FLAG_EOM : 0),
                                 fragment.sequence, fragment.payload, out, error);
}
bool qa_nq_channel_unreliable(qa_nq_channel *channel, qa_bytes payload, qa_bytes *out, qa_error *error) {
    if (!channel || payload.size > channel->message_bytes) return invalid(error, "Invalid NetQuake unreliable payload");
    if (!nq_packet(channel, QA_NQ_FLAG_UNRELIABLE, channel->unreliable_send, payload, out, error)) return false;
    ++channel->unreliable_send;
    return true;
}
bool qa_nq_channel_receive(qa_nq_channel *channel, qa_bytes bytes, uint64_t now_ns,
                            qa_q1_delivery *out, qa_bytes *reply, qa_error *error) {
    if (!channel || !out || !reply || !bytes.data) return invalid(error, "Invalid NetQuake receive arguments");
    *out = (qa_q1_delivery){0}; *reply = (qa_bytes){0};
    if (bytes.size < 8 || bytes.size > UINT16_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid NetQuake datagram length"); return false;
    }
    uint32_t word = read_be32(bytes.data), flags = word & UINT32_C(0xffff0000);
    uint32_t sequence = read_be32(bytes.data + 4);
    if ((word & UINT32_C(65535)) != bytes.size) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "NetQuake length mismatch"); return false;
    }
    if (flags & QA_NQ_FLAG_CONTROL) return true;
    qa_bytes payload = {bytes.data + 8, bytes.size - 8};
    if (flags == QA_NQ_FLAG_UNRELIABLE) {
        if (payload.size > channel->message_bytes) {
            qa_error_set(error, QA_ERROR_FORMAT, 8, "NetQuake unreliable payload exceeds capacity"); return false;
        }
        if (sequence < channel->unreliable_receive) return true;
        *out = (qa_q1_delivery){.present = true, .sequence = sequence,
            .dropped = (uint32_t)((uint64_t)sequence - channel->unreliable_receive), .payload = payload};
        channel->unreliable_receive = (uint64_t)sequence + 1;
        return true;
    }
    if (flags == QA_NQ_FLAG_ACK && !payload.size) {
        (void)qa_net_stopwait_acknowledge(channel->reliable, sequence);
        bool present;
        return qa_nq_channel_next(channel, now_ns, &present, reply, error);
    }
    if (flags == QA_NQ_FLAG_DATA || flags == (QA_NQ_FLAG_DATA | QA_NQ_FLAG_EOM)) {
        qa_net_reliable_fragment fragment = {sequence, (flags & QA_NQ_FLAG_EOM) != 0, payload};
        qa_net_fragment_result result;
        qa_bytes message;
        if (!qa_net_stopwait_receive(channel->reliable, &fragment, &result, &message, error)) return false;
        if (!nq_packet(channel, QA_NQ_FLAG_ACK, sequence, (qa_bytes){0}, reply, error)) return false;
        if (result == QA_NET_FRAGMENT_COMPLETE)
            *out = (qa_q1_delivery){.present = true, .reliable = true, .sequence = sequence, .payload = message};
        return true;
    }
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid NetQuake packet flags or ACK payload");
    return false;
}

bool qa_qw_channel_create(qa_q1_channel_side side, uint16_t qport, size_t message_bytes,
                           uint32_t rate, qa_qw_channel **out, qa_error *error) {
    if (!out || (side != QA_Q1_CHANNEL_CLIENT && side != QA_Q1_CHANNEL_SERVER) ||
        !message_bytes || message_bytes > 65525 || !rate) return invalid(error, "Invalid QuakeWorld channel options");
    qa_qw_channel *channel = calloc(1, sizeof(*channel));
    if (!channel) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeWorld channel"); return false; }
    channel->wire = malloc(message_bytes + 10);
    if (!channel->wire) { free(channel); qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeWorld wire buffer"); return false; }
    if (!qa_net_toggle_create(message_bytes, 0, &channel->reliable, error)) {
        free(channel->wire); free(channel); return false;
    }
    channel->capacity = message_bytes; channel->side = side; channel->qport = qport;
    channel->rate = (qa_net_rate){.bytes_per_second = rate, .backup_bytes = 200};
    *out = channel; return true;
}
void qa_qw_channel_destroy(qa_qw_channel *channel) {
    if (!channel) return;
    qa_net_toggle_destroy(channel->reliable); free(channel->wire); free(channel);
}
bool qa_qw_channel_queue(qa_qw_channel *channel, qa_bytes bytes, qa_error *error) {
    return channel ? qa_net_toggle_queue(channel->reliable, bytes, error) : invalid(error, "Missing QuakeWorld channel");
}
bool qa_qw_channel_pending(const qa_qw_channel *channel) { return channel && qa_net_toggle_pending(channel->reliable); }
bool qa_qw_channel_rate(qa_qw_channel *channel, uint32_t rate, qa_error *error) {
    if (!channel || !rate) return invalid(error, "QuakeWorld packet rate must be positive");
    channel->rate.bytes_per_second = rate; return true;
}
bool qa_qw_channel_can_send(const qa_qw_channel *channel, uint64_t now_ns) {
    return channel && qa_net_rate_ready(&channel->rate, now_ns, false);
}
bool qa_qw_channel_transmit(qa_qw_channel *channel, qa_bytes bytes, uint64_t now_ns,
                             bool paused, qa_bytes *out, qa_error *error) {
    if (!channel || !out) return invalid(error, "Invalid QuakeWorld transmit arguments");
    qa_net_toggle_packet packet;
    if (!qa_net_toggle_transmit(channel->reliable, bytes, channel->capacity, &packet, error)) return false;
    qa_net_writer writer;
    qa_net_writer_init(&writer, channel->wire, channel->capacity + 10, error);
    qa_net_write_u32(&writer, packet.sequence | (packet.reliable ? UINT32_C(0x80000000) : 0));
    qa_net_write_u32(&writer, packet.acknowledged | (packet.reliable_acknowledged ? UINT32_C(0x80000000) : 0));
    if (channel->side == QA_Q1_CHANNEL_CLIENT) qa_net_write_u16(&writer, channel->qport);
    if (!qa_net_write_data(&writer, packet.payload.data, packet.payload.size)) return false;
    *out = (qa_bytes){channel->wire, qa_net_writer_size(&writer)};
    qa_net_rate_sent(&channel->rate, out->size, now_ns, channel->side == QA_Q1_CHANNEL_SERVER && paused);
    return true;
}
bool qa_qw_channel_receive(qa_qw_channel *channel, qa_bytes bytes, uint64_t now_ns,
                            qa_q1_delivery *out, qa_error *error) {
    if (!channel || !out || !bytes.data) return invalid(error, "Invalid QuakeWorld receive arguments");
    *out = (qa_q1_delivery){0};
    size_t header = channel->side == QA_Q1_CHANNEL_SERVER ? 10U : 8U;
    if (bytes.size < header || bytes.size - header > channel->capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid QuakeWorld datagram size"); return false;
    }
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error);
    uint32_t word = qa_net_read_u32(&reader), ack = qa_net_read_u32(&reader);
    if (channel->side == QA_Q1_CHANNEL_SERVER && qa_net_read_u16(&reader) != channel->qport) return true;
    if (word == UINT32_MAX) return true;
    qa_net_toggle_packet packet = {
        .sequence = word & INT32_MAX, .acknowledged = ack & INT32_MAX,
        .reliable = (word >> 31) != 0, .reliable_acknowledged = (ack >> 31) != 0,
        .payload = {bytes.data + header, bytes.size - header}
    };
    bool accepted; uint32_t dropped;
    if (!qa_net_toggle_receive(channel->reliable, &packet, &accepted, &dropped, error)) return false;
    if (!accepted) return true;
    double sequence_delta = (double)qa_net_toggle_outgoing(channel->reliable) - packet.acknowledged;
    double interval_ms = ((double)now_ns - (double)channel->last_received_ns) / 1e6;
    channel->frame_latency = channel->frame_latency * 0.99 + sequence_delta * 0.01;
    channel->frame_interval_ms = channel->frame_interval_ms * 0.99 + interval_ms * 0.01;
    channel->last_received_ns = now_ns;
    if (channel->side == QA_Q1_CHANNEL_SERVER && packet.sequence >= qa_net_toggle_outgoing(channel->reliable))
        if (!qa_net_toggle_advance(channel->reliable, packet.sequence, error)) return false;
    *out = (qa_q1_delivery){.present = true, .sequence = packet.sequence,
        .acknowledged = packet.acknowledged, .dropped = dropped, .payload = packet.payload};
    return true;
}
qa_qw_channel_stats qa_qw_channel_get_stats(const qa_qw_channel *channel) {
    if (!channel) return (qa_qw_channel_stats){0};
    return (qa_qw_channel_stats){qa_net_toggle_incoming(channel->reliable), qa_net_toggle_outgoing(channel->reliable),
        channel->last_received_ns, channel->frame_latency, channel->frame_interval_ms,
        channel->rate.bytes_per_second};
}

bool qa_q1_peer_send(qa_q1_peer *peer, qa_bytes bytes, qa_error *error) {
    return peer ? qa_net_transport_send(peer->transport, &peer->remote, bytes, error) : invalid(error, "Missing Quake peer");
}
bool qa_q1_peer_receive(qa_q1_peer *peer, const qa_net_address *from, qa_bytes bytes,
                         uint64_t now_ns, qa_q1_delivery *out, qa_error *error) {
    if (!peer || !from || !out) return invalid(error, "Invalid Quake peer receive arguments");
    *out = (qa_q1_delivery){0};
    peer->reply_send_failed = false;
    switch (peer->kind) {
    case QA_Q1_PEER_NETQUAKE: {
        if (!peer->channel.nq) return invalid(error, "Missing NetQuake peer channel");
        if (!qa_net_address_equal(from, &peer->remote, true)) return true;
        qa_bytes reply;
        if (!qa_nq_channel_receive(peer->channel.nq, bytes, now_ns, out, &reply, error)) return false;
        if (reply.size) peer->reply_send_failed = !qa_q1_peer_send(peer, reply, NULL);
        return true;
    }
    case QA_Q1_PEER_QUAKEWORLD:
        if (!peer->channel.qw) return invalid(error, "Missing QuakeWorld peer channel");
        if (!qa_net_address_equal(from, &peer->remote, peer->channel.qw->side == QA_Q1_CHANNEL_CLIENT)) return true;
        if (!qa_qw_channel_receive(peer->channel.qw, bytes, now_ns, out, error)) return false;
        if (out->present) peer->remote = *from;
        return true;
    }
    return invalid(error, "Unknown Quake peer channel kind");
}

bool qa_nq_channel_checkpoint(const qa_nq_channel *channel, qa_buffer *out, qa_error *error)
{
    size_t message = 0, fragment = 0; uint64_t retry = 0;
    if (!channel || !out || !channel->wire ||
        !qa_net_stopwait_limits(channel->reliable, &message, &fragment, &retry) || message != channel->message_bytes ||
        retry != UINT64_C(1000000000) || fragment > 65527 ||
        channel->wire_capacity != (message < 65527 ? message : 65527) + 8 ||
        channel->unreliable_receive > (uint64_t)UINT32_MAX + 1)
        return invalid(error, "Invalid NetQuake continuation owner policy");
    qa_buffer reliable = {0};
    if (!qa_net_stopwait_checkpoint(channel->reliable, &reliable, error)) return false;
    if (reliable.size > SIZE_MAX - 44) { qa_buffer_free(&reliable); return invalid(error, "NetQuake continuation extent exceeds memory"); }
    uint8_t *data = malloc(44 + reliable.size);
    if (!data) { qa_buffer_free(&reliable); qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding NetQuake continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, 44 + reliable.size, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x434e4151)) &&
        qa_net_write_u64(&w, message) && qa_net_write_u64(&w, fragment) &&
        qa_net_write_u32(&w, channel->unreliable_send) && qa_net_write_u64(&w, channel->unreliable_receive) &&
        qa_net_write_u64(&w, reliable.size) && qa_net_write_data(&w, reliable.data, reliable.size);
    qa_buffer_free(&reliable);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
bool qa_nq_channel_restore_checkpoint(qa_bytes bytes, size_t message, size_t fragment, qa_nq_channel **out, qa_error *error)
{
    if (!out || *out || (bytes.size && !bytes.data)) return invalid(error, "NetQuake restore requires an empty candidate output");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    if (qa_net_read_u32(&r) != UINT32_C(0x434e4151) ||
        qa_net_read_u64(&r) != message || qa_net_read_u64(&r) != fragment)
        return invalid(error, "NetQuake continuation schema/policy differs");
    uint32_t send = qa_net_read_u32(&r); uint64_t received = qa_net_read_u64(&r), size = qa_net_read_u64(&r);
    qa_bytes reliable = {0};
    if (r.failed || received > (uint64_t)UINT32_MAX + 1 || size > SIZE_MAX ||
        !qa_net_read_bytes(&r, (size_t)size, &reliable) || !qa_net_reader_finish(&r))
        return invalid(error, "Invalid NetQuake retained continuation extent");
    qa_nq_channel *channel = NULL; qa_net_stopwait *restored = NULL;
    if (!qa_nq_channel_create(message, fragment, &channel, error)) return false;
    if (!qa_net_stopwait_restore_checkpoint(reliable, message, fragment, UINT64_C(1000000000), &restored, error)) {
        qa_nq_channel_destroy(channel); return false;
    }
    qa_net_stopwait_destroy(channel->reliable); channel->reliable = restored;
    channel->unreliable_send = send; channel->unreliable_receive = received;
    *out = channel; return true;
}
bool qa_qw_channel_checkpoint(const qa_qw_channel *channel, qa_buffer *out, qa_error *error)
{
    if (!channel || !out || !channel->wire || !channel->capacity || channel->capacity > 65525 ||
        (channel->side != QA_Q1_CHANNEL_CLIENT && channel->side != QA_Q1_CHANNEL_SERVER) ||
        !channel->rate.bytes_per_second || channel->rate.backup_bytes != 200 || !isfinite(channel->rate.clear_ns) ||
        channel->rate.clear_ns < 0 || !isfinite(channel->frame_latency) || !isfinite(channel->frame_interval_ms))
        return invalid(error, "Invalid QuakeWorld continuation owner policy");
    qa_buffer reliable = {0};
    if (!qa_net_toggle_checkpoint(channel->reliable, &reliable, error)) return false;
    if (reliable.size > SIZE_MAX - 70) { qa_buffer_free(&reliable); return invalid(error, "QuakeWorld continuation extent exceeds memory"); }
    uint8_t *data = malloc(70 + reliable.size);
    if (!data) { qa_buffer_free(&reliable); qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding QuakeWorld continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, 70 + reliable.size, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x43574151)) &&
        qa_net_write_u32(&w, channel->side) && qa_net_write_u16(&w, channel->qport) &&
        qa_net_write_u64(&w, channel->capacity) && qa_net_write_f64(&w, channel->rate.clear_ns) &&
        qa_net_write_u32(&w, channel->rate.bytes_per_second) && qa_net_write_u32(&w, channel->rate.backup_bytes) &&
        qa_net_write_u64(&w, channel->last_received_ns) && qa_net_write_f64(&w, channel->frame_latency) &&
        qa_net_write_f64(&w, channel->frame_interval_ms) && qa_net_write_u64(&w, reliable.size) &&
        qa_net_write_data(&w, reliable.data, reliable.size);
    qa_buffer_free(&reliable);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
bool qa_qw_channel_restore_checkpoint(qa_bytes bytes, qa_q1_channel_side side, uint16_t qport,
    size_t message, qa_qw_channel **out, qa_error *error)
{
    if (!out || *out || (bytes.size && !bytes.data)) return invalid(error, "QuakeWorld restore requires an empty candidate output");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    if (qa_net_read_u32(&r) != UINT32_C(0x43574151) ||
        qa_net_read_u32(&r) != (uint32_t)side || qa_net_read_u16(&r) != qport || qa_net_read_u64(&r) != message)
        return invalid(error, "QuakeWorld continuation schema/connection policy differs");
    qa_net_rate rate; rate.clear_ns = qa_net_read_f64(&r); rate.bytes_per_second = qa_net_read_u32(&r);
    rate.backup_bytes = qa_net_read_u32(&r); uint64_t received = qa_net_read_u64(&r);
    double latency = qa_net_read_f64(&r), interval = qa_net_read_f64(&r); uint64_t size = qa_net_read_u64(&r);
    qa_bytes reliable = {0};
    if (r.failed || !rate.bytes_per_second || rate.backup_bytes != 200 || !isfinite(rate.clear_ns) || rate.clear_ns < 0 ||
        !isfinite(latency) || !isfinite(interval) || size > SIZE_MAX ||
        !qa_net_read_bytes(&r, (size_t)size, &reliable) || !qa_net_reader_finish(&r))
        return invalid(error, "Invalid QuakeWorld scheduling/reliability continuation");
    qa_qw_channel *channel = NULL; qa_net_toggle *restored = NULL;
    if (!qa_qw_channel_create(side, qport, message, rate.bytes_per_second, &channel, error)) return false;
    if (!qa_net_toggle_restore_checkpoint(reliable, message, &restored, error)) { qa_qw_channel_destroy(channel); return false; }
    qa_net_toggle_destroy(channel->reliable); channel->reliable = restored;
    channel->rate = rate; channel->last_received_ns = received;
    channel->frame_latency = latency; channel->frame_interval_ms = interval;
    *out = channel; return true;
}

bool qa_nq_channel_save_policy(const qa_nq_channel *channel, size_t *message, size_t *fragment)
{
    size_t capacity, retained_fragment; uint64_t retry;
    if (!channel || !message || !fragment ||
        !qa_net_stopwait_limits(channel->reliable, &capacity, &retained_fragment, &retry) ||
        capacity != channel->message_bytes || retry != UINT64_C(1000000000)) return false;
    *message = channel->message_bytes; *fragment = retained_fragment;
    return true;
}

bool qa_qw_channel_save_policy(const qa_qw_channel *channel, size_t *message,
    qa_q1_channel_side *side, uint16_t *qport)
{
    if (!channel || !message || !side || !qport) return false;
    *message = channel->capacity; *side = channel->side; *qport = channel->qport;
    return true;
}
