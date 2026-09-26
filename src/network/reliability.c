#include "qa/network.h"

#include <stdlib.h>
#include <string.h>

struct qa_net_toggle {
    uint8_t *pending, *reliable, *output;
    size_t capacity, pending_size, reliable_size;
    uint32_t incoming, outgoing, incoming_ack, last_reliable;
    bool reliable_sequence, incoming_reliable, incoming_reliable_ack;
};

static bool invalid(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

static bool exhausted(qa_error *error)
{
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate network channel storage");
    return false;
}

static bool valid_bytes(qa_bytes bytes) { return bytes.size == 0 || bytes.data != NULL; }

bool qa_net_toggle_create(size_t capacity, uint32_t first_sequence,
                           qa_net_toggle **out, qa_error *error)
{
    if (out == NULL || capacity == 0 || capacity > SIZE_MAX / 3 || first_sequence > INT32_MAX)
        return invalid(error, "Invalid reliable channel capacity or sequence");
    qa_net_toggle *channel = calloc(1, sizeof(*channel));
    if (channel == NULL) return exhausted(error);
    channel->pending = malloc(capacity * 3);
    if (channel->pending == NULL) { free(channel); return exhausted(error); }
    channel->capacity = capacity;
    channel->reliable = channel->pending + capacity;
    channel->output = channel->reliable + capacity;
    channel->outgoing = first_sequence;
    *out = channel;
    return true;
}

void qa_net_toggle_destroy(qa_net_toggle *channel)
{
    if (channel != NULL) { free(channel->pending); free(channel); }
}

bool qa_net_toggle_queue(qa_net_toggle *channel, qa_bytes bytes, qa_error *error)
{
    if (channel == NULL || !valid_bytes(bytes)) return invalid(error, "Invalid reliable payload");
    if (bytes.size > channel->capacity - channel->pending_size)
        return invalid(error, "Reliable pending message overflow");
    if (bytes.size != 0) memmove(channel->pending + channel->pending_size, bytes.data, bytes.size);
    channel->pending_size += bytes.size;
    return true;
}

bool qa_net_toggle_transmit(qa_net_toggle *channel, qa_bytes unreliable, size_t capacity,
                             qa_net_toggle_packet *out, qa_error *error)
{
    if (channel == NULL || out == NULL || !valid_bytes(unreliable) || capacity > channel->capacity)
        return invalid(error, "Invalid reliable transmit request");
    if (channel->outgoing > INT32_MAX)
        return invalid(error, "Reliable channel sequence exhausted; reconnect required");
    bool load = channel->reliable_size == 0 && channel->pending_size != 0;
    bool send = load || (channel->incoming_ack > channel->last_reliable &&
                         channel->incoming_reliable_ack != channel->reliable_sequence);
    size_t reliable_size = send ? (load ? channel->pending_size : channel->reliable_size) : 0;
    if (reliable_size > capacity) return invalid(error, "Reliable payload does not fit packet");
    if (load) {
        memcpy(channel->reliable, channel->pending, channel->pending_size);
        channel->reliable_size = channel->pending_size;
        channel->pending_size = 0;
        channel->reliable_sequence = !channel->reliable_sequence;
    }
    if (reliable_size != 0) memcpy(channel->output, channel->reliable, reliable_size);
    size_t size = reliable_size;
    if (unreliable.size <= capacity - size) {
        if (unreliable.size != 0) memmove(channel->output + size, unreliable.data, unreliable.size);
        size += unreliable.size;
    }
    *out = (qa_net_toggle_packet){
        .sequence = channel->outgoing++, .acknowledged = channel->incoming,
        .reliable = send, .reliable_acknowledged = channel->incoming_reliable,
        .payload = { channel->output, size }
    };
    if (send) channel->last_reliable = channel->outgoing;
    return true;
}

bool qa_net_toggle_receive(qa_net_toggle *channel, const qa_net_toggle_packet *packet,
                            bool *accepted, uint32_t *dropped, qa_error *error)
{
    if (channel == NULL || packet == NULL || accepted == NULL || dropped == NULL ||
        packet->sequence > INT32_MAX || packet->acknowledged > INT32_MAX ||
        !valid_bytes(packet->payload) || packet->payload.size > channel->capacity)
        return invalid(error, "Invalid reliable packet");
    *accepted = false;
    *dropped = 0;
    if (packet->sequence <= channel->incoming) return true;
    *dropped = packet->sequence - channel->incoming - 1;
    if (packet->reliable_acknowledged == channel->reliable_sequence) channel->reliable_size = 0;
    channel->incoming = packet->sequence;
    channel->incoming_ack = packet->acknowledged;
    channel->incoming_reliable_ack = packet->reliable_acknowledged;
    if (packet->reliable) channel->incoming_reliable = !channel->incoming_reliable;
    *accepted = true;
    return true;
}

bool qa_net_toggle_pending(const qa_net_toggle *channel)
{
    return channel != NULL && (channel->pending_size != 0 || channel->reliable_size != 0);
}
uint32_t qa_net_toggle_incoming(const qa_net_toggle *channel) { return channel == NULL ? 0 : channel->incoming; }
uint32_t qa_net_toggle_outgoing(const qa_net_toggle *channel) { return channel == NULL ? 0 : channel->outgoing; }
bool qa_net_toggle_advance(qa_net_toggle *channel, uint32_t sequence, qa_error *error)
{
    if (channel == NULL || sequence < channel->outgoing || sequence > INT32_MAX)
        return invalid(error, "Invalid outgoing sequence advancement");
    channel->outgoing = sequence;
    return true;
}

struct qa_net_stopwait {
    uint8_t *send, *receive;
    size_t capacity, fragment_bytes, send_size, send_offset, receive_size;
    uint32_t outgoing, incoming;
    uint64_t retry_ns, sent_ns;
    bool sending, sent;
};

bool qa_net_stopwait_create(size_t message_bytes, size_t fragment_bytes,
                             uint64_t retry_ns, qa_net_stopwait **out, qa_error *error)
{
    if (out == NULL || message_bytes == 0 || message_bytes > SIZE_MAX / 2 ||
        fragment_bytes == 0 || fragment_bytes > message_bytes)
        return invalid(error, "Invalid stop-and-wait channel capacity");
    qa_net_stopwait *channel = calloc(1, sizeof(*channel));
    if (channel == NULL) return exhausted(error);
    channel->send = malloc(message_bytes * 2);
    if (channel->send == NULL) { free(channel); return exhausted(error); }
    channel->receive = channel->send + message_bytes;
    channel->capacity = message_bytes;
    channel->fragment_bytes = fragment_bytes;
    channel->retry_ns = retry_ns;
    *out = channel;
    return true;
}

void qa_net_stopwait_destroy(qa_net_stopwait *channel)
{
    if (channel != NULL) { free(channel->send); free(channel); }
}

bool qa_net_stopwait_begin(qa_net_stopwait *channel, qa_bytes bytes, qa_error *error)
{
    if (channel == NULL || !valid_bytes(bytes) || bytes.size > channel->capacity)
        return invalid(error, "Invalid stop-and-wait message");
    if (channel->sending) return invalid(error, "Reliable message remains unacknowledged");
    if (bytes.size != 0) memmove(channel->send, bytes.data, bytes.size);
    channel->send_size = bytes.size;
    channel->send_offset = 0;
    channel->sending = true;
    channel->sent = false;
    return true;
}

bool qa_net_stopwait_next(qa_net_stopwait *channel, uint64_t now_ns,
                          bool *present, qa_net_reliable_fragment *out, qa_error *error)
{
    if (channel == NULL || present == NULL || out == NULL)
        return invalid(error, "Invalid stop-and-wait output");
    *present = false;
    if (!channel->sending || (channel->sent &&
        (now_ns < channel->sent_ns || now_ns - channel->sent_ns <= channel->retry_ns))) return true;
    size_t size = channel->send_size - channel->send_offset;
    if (size > channel->fragment_bytes) size = channel->fragment_bytes;
    *out = (qa_net_reliable_fragment){ .sequence = channel->outgoing,
        .final = size == channel->send_size - channel->send_offset,
        .payload = { channel->send + channel->send_offset, size } };
    channel->sent = true;
    channel->sent_ns = now_ns;
    *present = true;
    return true;
}

bool qa_net_stopwait_acknowledge(qa_net_stopwait *channel, uint32_t sequence)
{
    if (channel == NULL || !channel->sending || !channel->sent || sequence != channel->outgoing) return false;
    ++channel->outgoing;
    size_t remaining = channel->send_size - channel->send_offset;
    if (remaining <= channel->fragment_bytes) {
        channel->sending = false;
        channel->send_offset = channel->send_size;
    } else channel->send_offset += channel->fragment_bytes;
    channel->sent = false;
    return true;
}

bool qa_net_stopwait_receive(qa_net_stopwait *channel, const qa_net_reliable_fragment *fragment,
                             qa_net_fragment_result *result, qa_bytes *out, qa_error *error)
{
    if (channel == NULL || fragment == NULL || result == NULL || out == NULL || !valid_bytes(fragment->payload))
        return invalid(error, "Invalid stop-and-wait fragment");
    *out = (qa_bytes){0};
    *result = QA_NET_FRAGMENT_DUPLICATE;
    if (fragment->sequence != channel->incoming) return true;
    if (fragment->payload.size > channel->capacity - channel->receive_size)
        return invalid(error, "Reliable receive overflow");
    if (fragment->payload.size != 0)
        memmove(channel->receive + channel->receive_size, fragment->payload.data, fragment->payload.size);
    channel->receive_size += fragment->payload.size;
    ++channel->incoming;
    *result = fragment->final ? QA_NET_FRAGMENT_COMPLETE : QA_NET_FRAGMENT_PENDING;
    if (fragment->final) {
        *out = (qa_bytes){ channel->receive, channel->receive_size };
        channel->receive_size = 0;
    }
    return true;
}

bool qa_net_stopwait_ready(const qa_net_stopwait *channel) { return channel != NULL && !channel->sending; }

struct qa_net_fragments {
    uint8_t *send, *receive;
    size_t capacity, fragment_bytes, send_size, send_offset, receive_size;
    uint32_t send_sequence, current, accepted;
    bool terminal_empty, sending, has_current;
};

bool qa_net_fragments_create(size_t message_bytes, size_t fragment_bytes,
                              bool terminal_empty, qa_net_fragments **out, qa_error *error)
{
    if (out == NULL || message_bytes == 0 || message_bytes > SIZE_MAX / 2 ||
        fragment_bytes == 0 || fragment_bytes > message_bytes)
        return invalid(error, "Invalid fragment channel capacity");
    qa_net_fragments *channel = calloc(1, sizeof(*channel));
    if (channel == NULL) return exhausted(error);
    channel->send = malloc(message_bytes * 2);
    if (channel->send == NULL) { free(channel); return exhausted(error); }
    channel->receive = channel->send + message_bytes;
    channel->capacity = message_bytes;
    channel->fragment_bytes = fragment_bytes;
    channel->terminal_empty = terminal_empty;
    *out = channel;
    return true;
}

void qa_net_fragments_destroy(qa_net_fragments *channel)
{
    if (channel != NULL) { free(channel->send); free(channel); }
}

bool qa_net_fragments_begin(qa_net_fragments *channel, uint32_t sequence, qa_bytes bytes, qa_error *error)
{
    if (channel == NULL || !valid_bytes(bytes) || bytes.size > channel->capacity || sequence == 0)
        return invalid(error, "Invalid fragmented message");
    if (channel->sending) return invalid(error, "Fragmented message remains pending");
    if (bytes.size != 0) memmove(channel->send, bytes.data, bytes.size);
    channel->send_size = bytes.size;
    channel->send_offset = 0;
    channel->send_sequence = sequence;
    channel->sending = true;
    return true;
}

bool qa_net_fragments_next(qa_net_fragments *channel, bool *present, qa_net_fragment *out, qa_error *error)
{
    if (channel == NULL || present == NULL || out == NULL) return invalid(error, "Invalid fragment output");
    *present = false;
    if (!channel->sending) return true;
    size_t remaining = channel->send_size - channel->send_offset;
    size_t size = remaining < channel->fragment_bytes ? remaining : channel->fragment_bytes;
    bool final = size == remaining && (!channel->terminal_empty || size < channel->fragment_bytes);
    *out = (qa_net_fragment){ .sequence = channel->send_sequence, .offset = channel->send_offset,
        .final = final, .payload = { channel->send + channel->send_offset, size } };
    channel->send_offset += size;
    channel->sending = !final;
    *present = true;
    return true;
}

bool qa_net_fragments_pending(const qa_net_fragments *channel) { return channel != NULL && channel->sending; }

bool qa_net_fragments_receive(qa_net_fragments *channel, const qa_net_fragment *fragment,
                               qa_net_fragment_result *result, qa_bytes *out, qa_error *error)
{
    if (channel == NULL || fragment == NULL || result == NULL || out == NULL || !valid_bytes(fragment->payload))
        return invalid(error, "Invalid receive fragment");
    *result = QA_NET_FRAGMENT_DUPLICATE;
    *out = (qa_bytes){0};
    if (fragment->sequence <= channel->accepted) return true;
    if (!channel->has_current || fragment->sequence != channel->current) {
        channel->current = fragment->sequence;
        channel->receive_size = 0;
        channel->has_current = true;
    }
    if (fragment->offset != channel->receive_size ||
        fragment->payload.size > channel->capacity - channel->receive_size) return true;
    if (fragment->payload.size != 0)
        memmove(channel->receive + channel->receive_size, fragment->payload.data, fragment->payload.size);
    channel->receive_size += fragment->payload.size;
    *result = fragment->final ? QA_NET_FRAGMENT_COMPLETE : QA_NET_FRAGMENT_PENDING;
    if (fragment->final) {
        channel->accepted = fragment->sequence;
        *out = (qa_bytes){ channel->receive, channel->receive_size };
        channel->receive_size = 0;
    }
    return true;
}

bool qa_net_fragments_accept(qa_net_fragments *channel, uint32_t sequence)
{
    if (channel == NULL || sequence <= channel->accepted) return false;
    channel->accepted = sequence;
    return true;
}

bool qa_net_rate_ready(const qa_net_rate *rate, uint64_t now_ns, bool paused)
{
    return rate != NULL && rate->bytes_per_second != 0 &&
        (paused || rate->clear_ns < (double)now_ns + (double)rate->backup_bytes * 1e9 / rate->bytes_per_second);
}

void qa_net_rate_sent(qa_net_rate *rate, size_t bytes, uint64_t now_ns, bool paused)
{
    if (rate == NULL || rate->bytes_per_second == 0) return;
    if (paused) rate->clear_ns = (double)now_ns;
    else {
        if (rate->clear_ns < (double)now_ns) rate->clear_ns = (double)now_ns;
        rate->clear_ns += (double)bytes * 1e9 / rate->bytes_per_second;
    }
}
