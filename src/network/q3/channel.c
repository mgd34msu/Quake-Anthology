#include "qa/network_q3.h"
#include "qa/network_q3_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct qa_q3_channel {
    qa_q3_role role;
    uint16_t qport;
    uint32_t incoming, outgoing, fragment_sequence;
    bool pending, fragmented;
    size_t send_size, send_offset, receive_size;
    uint8_t send[QA_Q3_MESSAGE_BYTES], receive[QA_Q3_MESSAGE_BYTES], delivered[QA_Q3_MESSAGE_BYTES];
    uint8_t packet[QA_Q3_FRAGMENT_BYTES + 10];
};
static bool fail(qa_error *e, qa_status code, const char *s) { qa_error_set(e, code, 0, "%s", s); return false; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static void put32(uint8_t *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (i * 8)); }
bool qa_q3_channel_create(qa_q3_role role, uint16_t qport, qa_q3_channel **out, qa_error *error) {
    if (!out || (role != QA_Q3_CLIENT && role != QA_Q3_SERVER)) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 channel options");
    qa_q3_channel *c = calloc(1, sizeof(*c));
    if (!c) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 netchannel");
    c->role = role; c->qport = qport; c->outgoing = 1;
    *out = c; return true;
}
void qa_q3_channel_destroy(qa_q3_channel *c) { free(c); }
bool qa_q3_channel_create_source_zero(qa_q3_role role, uint16_t qport, qa_q3_channel **out, qa_error *error) {
    if (!qa_q3_channel_create(role, qport, out, error)) return false;
    (*out)->outgoing = 0; return true;
}
size_t qa_q3_channel_remaining(const qa_q3_channel *c) { return c && c->pending ? c->send_size - c->send_offset : 0; }
uint32_t qa_q3_channel_outgoing(const qa_q3_channel *c) { return c->outgoing; }
uint32_t qa_q3_channel_incoming(const qa_q3_channel *c) { return c->incoming; }
qa_q3_role qa_q3_channel_role(const qa_q3_channel *c) { return c->role; }
uint16_t qa_q3_channel_qport(const qa_q3_channel *c) { return c->qport; }
bool qa_q3_channel_transmit_matches(const qa_q3_channel *c, qa_bytes packet, qa_error *error)
{
    if (!c || c->role != QA_Q3_CLIENT || !packet.data || packet.size < 6 ||
        packet.size > sizeof(c->packet) || !c->pending)
        return fail(error, QA_ERROR_FORMAT, "Q3 retained datagram lacks its actual client channel");
    uint32_t wire = get32(packet.data), sequence = c->outgoing;
    if ((wire & UINT32_C(0x7fffffff)) != sequence ||
        ((wire & UINT32_C(0x80000000)) != 0) != c->fragmented || get16(packet.data + 4) != c->qport)
        return fail(error, QA_ERROR_FORMAT, "Q3 retained datagram differs from its produced channel identity");
    size_t start = 0, length = c->send_size, header = 6;
    if (c->fragmented) {
        if (packet.size < 10) return fail(error, QA_ERROR_FORMAT, "Q3 retained fragment header is truncated");
        start = get16(packet.data + 6); length = get16(packet.data + 8); header = 10;
        if (start > c->send_size || start % QA_Q3_FRAGMENT_BYTES || length > QA_Q3_FRAGMENT_BYTES ||
            length != (c->send_size - start > QA_Q3_FRAGMENT_BYTES ? QA_Q3_FRAGMENT_BYTES : c->send_size - start))
            return fail(error, QA_ERROR_FORMAT, "Q3 retained fragment differs from its true produced source span");
    }
    return (start == c->send_offset && packet.size == header + length &&
        (!length || !memcmp(packet.data + header, c->send + start, length))) ||
        fail(error, QA_ERROR_FORMAT, "Q3 retained datagram bytes differ from their advanced source channel");
}
bool qa_q3_channel_checkpoint(const qa_q3_channel *c, qa_net_writer *writer)
{
    if (!c || !writer) return writer && qa_net_writer_fail(writer, "Missing Q3 channel checkpoint owner");
    return qa_net_write_u32(writer, c->role) &&
        qa_net_write_u16(writer, c->qport) && qa_net_write_u32(writer, c->incoming) &&
        qa_net_write_u32(writer, c->outgoing) && qa_net_write_u32(writer, c->fragment_sequence) &&
        qa_net_write_u8(writer, c->pending) && qa_net_write_u8(writer, c->fragmented) &&
        qa_net_write_u32(writer, (uint32_t)c->send_size) && qa_net_write_u32(writer, (uint32_t)c->send_offset) &&
        qa_net_write_u32(writer, (uint32_t)c->receive_size) &&
        qa_net_write_data(writer, c->send, c->send_size) && qa_net_write_data(writer, c->receive, c->receive_size);
}
bool qa_q3_channel_restore(qa_net_reader *reader, qa_q3_channel **out)
{
    if (!reader || !out) return reader && qa_net_reader_fail(reader, "Missing Q3 channel restore owner");
    uint32_t role = qa_net_read_u32(reader);
    uint16_t qport = qa_net_read_u16(reader);
    uint32_t incoming = qa_net_read_u32(reader), outgoing = qa_net_read_u32(reader);
    uint32_t fragment = qa_net_read_u32(reader);
    uint8_t pending = qa_net_read_u8(reader), fragmented = qa_net_read_u8(reader);
    uint32_t send_size = qa_net_read_u32(reader), send_offset = qa_net_read_u32(reader);
    uint32_t receive_size = qa_net_read_u32(reader);
    if (reader->failed) return false;
    if (role > QA_Q3_SERVER || incoming > UINT32_C(0x7fffffff) ||
        outgoing > UINT32_C(0x80000000) || fragment > UINT32_C(0x7fffffff) || pending > 1 || fragmented > 1 ||
        send_size > QA_Q3_MESSAGE_BYTES || receive_size > QA_Q3_MESSAGE_BYTES || send_offset > send_size ||
        (pending && outgoing > UINT32_C(0x7fffffff)) ||
        (fragmented != (send_size >= QA_Q3_FRAGMENT_BYTES)) ||
        (pending && (!fragmented ? send_offset != 0 : send_offset % QA_Q3_FRAGMENT_BYTES != 0)) ||
        (!pending && send_offset != send_size) || receive_size % QA_Q3_FRAGMENT_BYTES != 0)
        return qa_net_reader_fail(reader, "Invalid Q3 channel continuation fields");
    qa_q3_channel *channel = NULL;
    if (!qa_q3_channel_create((qa_q3_role)role, qport, &channel, reader->error)) { reader->failed = true; return false; }
    channel->incoming = incoming; channel->outgoing = outgoing; channel->fragment_sequence = fragment;
    channel->pending = pending != 0; channel->fragmented = fragmented != 0;
    channel->send_size = send_size; channel->send_offset = send_offset; channel->receive_size = receive_size;
    if (!qa_net_read_data(reader, channel->send, send_size) || !qa_net_read_data(reader, channel->receive, receive_size)) {
        qa_q3_channel_destroy(channel); return false;
    }
    *out = channel; return true;
}
bool qa_q3_channel_pending(const qa_q3_channel *c) { return c && c->pending; }
bool qa_q3_channel_begin(qa_q3_channel *c, qa_bytes data, qa_error *error) {
    if (!c || data.size > QA_Q3_MESSAGE_BYTES || (data.size && !data.data)) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 message payload");
    if (c->pending) return fail(error, QA_ERROR_ARGUMENT, "Q3 message still has unsent fragments");
    if (c->outgoing > UINT32_C(0x7fffffff)) return fail(error, QA_ERROR_FORMAT, "Q3 sequence exhausted; reconnect required");
    if (data.size) memcpy(c->send, data.data, data.size);
    c->send_size = data.size; c->send_offset = 0; c->pending = true;
    c->fragmented = data.size >= QA_Q3_FRAGMENT_BYTES;
    return true;
}
bool qa_q3_channel_prepare(qa_q3_channel *c, bool *present, qa_bytes *out, qa_error *error) {
    if (!c || !present || !out) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 transmit output");
    *present = false; *out = (qa_bytes){0};
    if (!c->pending) return true;
    size_t header = c->role == QA_Q3_CLIENT ? 6 : 4;
    put32(c->packet, c->outgoing | (c->fragmented ? UINT32_C(0x80000000) : 0));
    if (c->role == QA_Q3_CLIENT) put16(c->packet + 4, c->qport);
    size_t length = c->send_size - c->send_offset;
    if (c->fragmented) {
        if (length > QA_Q3_FRAGMENT_BYTES) length = QA_Q3_FRAGMENT_BYTES;
        put16(c->packet + header, (uint16_t)c->send_offset);
        put16(c->packet + header + 2, (uint16_t)length);
        header += 4;
    }
    if (length) memcpy(c->packet + header, c->send + c->send_offset, length);
    *present = true; *out = (qa_bytes){c->packet, header + length};
    return true;
}
void qa_q3_channel_sent(qa_q3_channel *c, qa_bytes packet) {
    size_t header = (c->role == QA_Q3_CLIENT ? 6u : 4u) + (c->fragmented ? 4u : 0u);
    size_t length = packet.size - header;
    c->send_offset += length;
    if (!c->fragmented || length < QA_Q3_FRAGMENT_BYTES) { c->pending = false; ++c->outgoing; }
}
bool qa_q3_channel_receive(qa_q3_channel *c, qa_bytes data, qa_q3_packet *out, qa_error *error) {
    if (!c || !out || !data.data) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 receive arguments");
    size_t header = c->role == QA_Q3_SERVER ? 6 : 4;
    if (data.size < header || data.size > QA_Q3_MESSAGE_BYTES) return fail(error, QA_ERROR_FORMAT, "Invalid Q3 datagram size");
    uint32_t wire = get32(data.data), sequence = wire & UINT32_C(0x7fffffff);
    bool fragmented = (wire & UINT32_C(0x80000000)) != 0;
    *out = (qa_q3_packet){.kind = QA_Q3_PACKET_STALE, .sequence = sequence,
        .qport = c->role == QA_Q3_SERVER ? get16(data.data + 4) : 0};
    if (fragmented && data.size < header + 4) return fail(error, QA_ERROR_FORMAT, "Truncated Q3 fragment header");
    if (sequence <= c->incoming) return true;
    out->dropped = sequence - c->incoming - 1;
    if (!fragmented) {
        size_t length = data.size - header;
        memcpy(c->delivered, data.data + header, length);
        c->incoming = sequence;
        out->kind = QA_Q3_PACKET_MESSAGE; out->payload = (qa_bytes){c->delivered, length};
        return true;
    }
    size_t start = get16(data.data + header), length = get16(data.data + header + 2);
    header += 4;
    if (start > INT16_MAX || length > INT16_MAX || length > data.size - header)
        return fail(error, QA_ERROR_FORMAT, "Invalid Q3 fragment length");
    if (sequence != c->fragment_sequence) { c->fragment_sequence = sequence; c->receive_size = 0; }
    if (start != c->receive_size) return true;
    if (length > sizeof(c->receive) - c->receive_size) return fail(error, QA_ERROR_FORMAT, "Q3 fragment assembly exceeds message limit");
    if (length) memcpy(c->receive + c->receive_size, data.data + header, length);
    c->receive_size += length;
    if (length == QA_Q3_FRAGMENT_BYTES) { out->kind = QA_Q3_PACKET_FRAGMENT; return true; }
    c->incoming = sequence;
    out->kind = QA_Q3_PACKET_MESSAGE; out->payload = (qa_bytes){c->receive, c->receive_size};
    c->receive_size = 0;
    return true;
}
static void xor_payload(uint8_t *data, size_t size, size_t start, uint32_t initial, const char *text) {
    size_t length = strlen(text), index = 0;
    uint8_t key = (uint8_t)initial;
    for (size_t i = start; i < size; ++i) {
        if (index >= length) index = 0;
        unsigned ch = length ? (unsigned char)text[index++] : 0;
        if (ch > 127 || ch == '%') ch = '.';
        key ^= (uint8_t)(ch << (i & 1)); data[i] ^= key;
    }
}
bool qa_q3_xor_client(uint8_t *data, size_t size, int32_t challenge,
                       qa_q3_command_lookup lookup, void *user, qa_error *error) {
    if (!data || !lookup || size > QA_Q3_MESSAGE_BYTES) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 client XOR input");
    if (size <= 12) return true;
    qa_q3_reader reader;
    qa_q3_reader_init(&reader, (qa_bytes){data, size}, false, error);
    uint32_t server = qa_q3_read_bits(&reader, 32), ack = qa_q3_read_bits(&reader, 32);
    int32_t reliable = (int32_t)qa_q3_read_bits(&reader, 32);
    if (reader.raw.failed) return false;
    const char *command = lookup(user, reliable);
    if (!command) return fail(error, QA_ERROR_FORMAT, "Missing Q3 XOR reliable command slot");
    xor_payload(data, size, 12, (uint32_t)challenge ^ server ^ ack, command);
    return true;
}
void qa_q3_xor_server(uint8_t *data, size_t size, int32_t challenge, uint32_t sequence, const char *command) {
    xor_payload(data, size, 4, (uint32_t)challenge ^ sequence, command);
}
void qa_q3_reliable_init(qa_q3_reliable *r) { memset(r, 0, sizeof(*r)); }
const char *qa_q3_reliable_lookup(const qa_q3_reliable *r, int32_t sequence) { return r->text[(uint32_t)sequence & 63]; }
bool qa_q3_reliable_add(qa_q3_reliable *r, qa_q3_role role, const char *text, qa_error *error) {
    if (!r || !text) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 reliable command");
    int64_t outstanding = (int64_t)r->sequence - r->acknowledged;
    if (role == QA_Q3_CLIENT && outstanding > QA_Q3_RELIABLE) return fail(error, QA_ERROR_FORMAT, "Q3 client reliable command overflow");
    if (r->sequence == INT32_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 reliable sequence exhausted; reconnect required");
    ++r->sequence;
    if (role == QA_Q3_SERVER && outstanding == QA_Q3_RELIABLE) return fail(error, QA_ERROR_FORMAT, "Q3 server reliable command overflow");
    size_t n = strlen(text);
    if (n >= QA_Q3_COMMAND_CHARS) n = QA_Q3_COMMAND_CHARS - 1;
    char *target = r->text[(uint32_t)r->sequence & 63];
    memcpy(target, text, n); target[n] = 0;
    return true;
}
bool qa_q3_reliable_ack(qa_q3_reliable *r, qa_q3_role role, int32_t value, bool *clamped, qa_error *error) {
    if (!r || !clamped || value < 0 || value > r->sequence) return fail(error, QA_ERROR_FORMAT, "Invalid Q3 reliable acknowledgement");
    *clamped = (int64_t)value < (int64_t)r->sequence - QA_Q3_RELIABLE;
    r->acknowledged = *clamped ? r->sequence : value;
    if (*clamped && role == QA_Q3_SERVER) return fail(error, QA_ERROR_FORMAT, "Stale Q3 client reliable acknowledgement");
    return true;
}
