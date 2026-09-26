#include "qa/network_unified.h"

#include <string.h>

static bool invalid(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}

bool qa_unified_token_parse(const char *text, qa_unified_token *out, qa_error *error) {
    qa_unified_token token = {{0}};
    if (!text || !out || strlen(text) != 32)
        return invalid(error, "unified token requires 32 hexadecimal digits");
    for (size_t i = 0; i < 32; ++i) {
        unsigned char c = (unsigned char)text[i];
        unsigned n;
        if (c >= '0' && c <= '9') n = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') n = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') n = (unsigned)(c - 'A') + 10u;
        else return invalid(error, "invalid unified token digit");
        token.bytes[i / 2] |= (uint8_t)(n << ((i & 1u) ? 0u : 4u));
    }
    *out = token;
    return true;
}

void qa_unified_token_format(qa_unified_token token, char out[33]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        out[i * 2] = hex[token.bytes[i] >> 4];
        out[i * 2 + 1] = hex[token.bytes[i] & 15u];
    }
    out[32] = '\0';
}

static bool valid(const qa_unified_packet *p, qa_error *error) {
    if (!p || p->sequence == 0 || (unsigned)p->kind > (unsigned)QA_UNIFIED_FRAME)
        return invalid(error, "invalid unified packet kind or sequence");
    if (p->kind == QA_UNIFIED_ACK) {
        if (p->required_reliable || p->total_bytes || p->fragment_bytes || p->fragments || p->payload.size)
            return invalid(error, "unified ACK contains data fields");
        return true;
    }
    if (!p->fragment_bytes || p->fragment_bytes > QA_UNIFIED_MAX_DATAGRAM - QA_UNIFIED_HEADER_BYTES)
        return invalid(error, "invalid unified fragment size");
    uint32_t fragments = p->total_bytes ? (p->total_bytes - 1u) / p->fragment_bytes + 1u : 1u;
    if (fragments > UINT16_MAX || p->fragments != fragments || p->fragment >= fragments)
        return invalid(error, "invalid unified fragment count or index");
    uint64_t offset = (uint64_t)p->fragment * p->fragment_bytes;
    uint32_t bytes = p->total_bytes - (uint32_t)offset;
    if (bytes > p->fragment_bytes) bytes = p->fragment_bytes;
    if (p->payload.size != bytes || (bytes && !p->payload.data) ||
        (p->kind == QA_UNIFIED_RELIABLE && p->required_reliable != 0))
        return invalid(error, "invalid unified fragment payload or dependency");
    return true;
}

bool qa_unified_packet_decode(qa_bytes bytes, qa_unified_packet *out, qa_error *error) {
    if (!out || !bytes.data || bytes.size < QA_UNIFIED_HEADER_BYTES || bytes.size > QA_UNIFIED_MAX_DATAGRAM)
        return invalid(error, "invalid unified datagram length");
    const uint8_t *b = bytes.data;
    if (memcmp(b, "QTUC", 4) || b[4] != 1 || b[5] > 2 || b[6] || b[7])
        return invalid(error, "invalid unified packet header");
    qa_unified_packet p = {0};
    p.kind = (qa_unified_packet_kind)b[5];
    memcpy(p.token.bytes, b + 8, 16);
    p.sequence = qa_load_u32le(b + 24);
    p.acknowledged_reliable = qa_load_u32le(b + 28);
    p.required_reliable = qa_load_u32le(b + 32);
    p.total_bytes = qa_load_u32le(b + 36);
    p.fragment_bytes = qa_load_u32le(b + 40);
    p.fragment = qa_load_u16le(b + 44);
    p.fragments = qa_load_u16le(b + 46);
    p.payload = (qa_bytes){b + QA_UNIFIED_HEADER_BYTES, bytes.size - QA_UNIFIED_HEADER_BYTES};
    if (!valid(&p, error)) return false;
    *out = p;
    return true;
}

bool qa_unified_packet_encode(const qa_unified_packet *p, void *buffer, size_t capacity,
                               size_t *written, qa_error *error) {
    if (!valid(p, error)) return false;
    if (!buffer || !written || capacity < QA_UNIFIED_HEADER_BYTES + p->payload.size)
        return invalid(error, "unified packet destination is too small");
    uint8_t *b = buffer;
    /* Permit payload already placed in the destination. */
    if (p->payload.size) memmove(b + QA_UNIFIED_HEADER_BYTES, p->payload.data, p->payload.size);
    memset(b, 0, QA_UNIFIED_HEADER_BYTES);
    memcpy(b, "QTUC", 4); b[4] = 1; b[5] = (uint8_t)p->kind;
    memcpy(b + 8, p->token.bytes, 16);
    qa_store_u32le(b + 24, p->sequence);
    qa_store_u32le(b + 28, p->acknowledged_reliable);
    qa_store_u32le(b + 32, p->required_reliable);
    qa_store_u32le(b + 36, p->total_bytes);
    qa_store_u32le(b + 40, p->fragment_bytes);
    qa_store_u16le(b + 44, p->fragment);
    qa_store_u16le(b + 46, p->fragments);
    *written = QA_UNIFIED_HEADER_BYTES + p->payload.size;
    return true;
}
