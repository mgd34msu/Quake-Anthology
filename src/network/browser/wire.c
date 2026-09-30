#include "internal.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q2.h"
#include "qa/network_q2_kex.h"
#include "qa/network_q3.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_browser_query_encode(qa_net_protocol_id protocol, uint64_t challenge, qa_net_writer *writer) {
    switch (protocol.kind) {
    case QA_NET_NQ15: case QA_NET_FITZ666: case QA_NET_RMQ999: return qa_nq_discovery_query(writer);
    case QA_NET_QW28: case QA_NET_QW29: return qa_qw_discovery_query(writer);
    case QA_NET_Q2_34: case QA_NET_R1Q2_35: case QA_NET_Q2PRO_36: case QA_NET_Q2REPRO_1038:
    case QA_NET_Q2PRIVATE_4038: return qa_q2_oob_write(writer, "status");
    case QA_NET_Q2KEX_2023: return qa_kex_discovery_query(writer);
    case QA_NET_Q3_68: {
        char query[64]; (void)snprintf(query, sizeof(query), "getstatus %" PRIu64, challenge);
        return qa_q2_oob_write(writer, query);
    }
    default: return qa_net_writer_fail(writer, "Protocol has no native server query dialect");
    }
}
static bool text_copy(char *out, size_t capacity, const char *text, qa_error *error) {
    size_t size = strlen(text);
    if (size >= capacity) return qa_browser_fail(error, "Server discovery text exceeds field limit");
    memcpy(out, text, size + 1); return true;
}
static bool number(const char *text, uint32_t *out) {
    if (!*text) { *out = 0; return true; }
    uint64_t result = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || result > (UINT32_MAX - (unsigned)(*text - '0')) / 10u) return false;
        result = result * 10 + (unsigned)(*text - '0');
    }
    *out = (uint32_t)result; return true;
}
static bool info(qa_server_entry *entry, const char *rules, qa_error *error) {
    char value[1025];
    if (!text_copy(entry->rules, sizeof(entry->rules), rules, error) ||
        !qa_q3_info_value(rules, "hostname", value, sizeof(value), error)) return false;
    if (!*value && !qa_q3_info_value(rules, "sv_hostname", value, sizeof(value), error)) return false;
    if (!text_copy(entry->name, sizeof(entry->name), value, error) ||
        !qa_q3_info_value(rules, "mapname", entry->map, sizeof(entry->map), error) ||
        !qa_q3_info_value(rules, "sv_maxclients", value, sizeof(value), error) || !number(value, &entry->maximum_players)) return false;
    return true;
}
static bool q2_player(void *context, int32_t score, int32_t ping, const char *name, qa_error *error) {
    qa_server_entry *entry = context; (void)score; (void)ping; (void)name;
    if (entry->players == UINT32_MAX) return qa_browser_fail(error, "Discovery player count overflow");
    ++entry->players; return true;
}
bool qa_browser_status_decode(qa_bytes bytes, qa_net_protocol_id protocol,
                               qa_server_entry *out, uint64_t *challenge, qa_error *error) {
    qa_server_entry entry = {.protocol = protocol}; *challenge = 0;
    if (protocol.kind <= QA_NET_QW29) {
        qa_q1_discovery status = {0};
        if (!qa_q1_discovery_read(bytes, qa_q1_is_qw(protocol), &status, error)) return false;
        bool ok = text_copy(entry.name, sizeof(entry.name), status.name, error) &&
            text_copy(entry.map, sizeof(entry.map), status.map, error);
        entry.players = status.players; entry.maximum_players = status.max_players;
        if (ok && status.rules.count) {
            for (size_t i = 0; i < status.rules.count; ++i)
                if (!qa_q3_info_set(entry.rules, sizeof(entry.rules), status.rules.rules[i].name,
                    status.rules.rules[i].value, error)) { ok = false; break; }
        }
        qa_q1_discovery_free(&status); if (!ok) return false;
    } else if (protocol.kind == QA_NET_Q2KEX_2023) {
        qa_kex_discovery *status = malloc(sizeof(*status));
        if (!status) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating KEX discovery scratch"); return false; }
        if (!qa_kex_discovery_read(bytes, status, error)) { free(status); return false; }
        bool ok = text_copy(entry.name, sizeof(entry.name), status->name, error);
        entry.players = status->players; entry.maximum_players = status->max_players;
        for (size_t i = 0; ok && i < status->attribute_count; ++i) {
            if (!strcmp(status->attributes[i].key, "mapname")) ok = text_copy(entry.map, sizeof(entry.map), status->attributes[i].value, error);
            if (ok) ok = qa_q3_info_set(entry.rules, sizeof(entry.rules), status->attributes[i].key, status->attributes[i].value, error);
        }
        free(status); if (!ok) return false;
    } else if (protocol.kind == QA_NET_Q3_68) {
        qa_q3_connectionless *message = malloc(sizeof(*message));
        if (!message) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 discovery scratch"); return false; }
        bool ok = qa_q3_connectionless_decode(bytes, QA_Q3_CLIENT, message, error);
        if (!ok || strcmp(qa_q3_token(&message->tokens, 0), "statusResponse")) { free(message); return qa_browser_fail(error, "Not a Q3 status response"); }
        size_t n = 0;
        while (n < message->payload_size && message->payload[n] && message->payload[n] != '\n' && message->payload[n] != '\r') ++n;
        if (n >= sizeof(entry.rules)) { free(message); return qa_browser_fail(error, "Q3 discovery rules exceed limit"); }
        char rules[8193]; memcpy(rules, message->payload, n); rules[n] = 0;
        ok = info(&entry, rules, error);
        char value[64];
        if (ok) ok = qa_q3_info_value(rules, "challenge", value, sizeof(value), error);
        if (ok) {
            uint64_t result = 0;
            for (const char *p = value; *p; ++p) {
                if (*p < '0' || *p > '9' || result > (UINT64_MAX - (unsigned)(*p - '0')) / 10u) { ok = false; break; }
                result = result * 10 + (unsigned)(*p - '0');
            }
            *challenge = result;
        }
        for (size_t i = n; ok && i < message->payload_size;) {
            while (i < message->payload_size && (message->payload[i] == '\n' || message->payload[i] == '\r')) ++i;
            size_t start = i;
            while (i < message->payload_size && message->payload[i] && message->payload[i] != '\n' && message->payload[i] != '\r') ++i;
            if (i > start) ++entry.players;
            if (i < message->payload_size && !message->payload[i]) break;
        }
        free(message); if (!ok) return qa_browser_fail(error, "Invalid Q3 discovery challenge/rules");
    } else {
        qa_q2_oob *message = malloc(sizeof(*message)); bool recognized = false;
        if (!message) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 discovery scratch"); return false; }
        char rules[8193];
        bool ok = qa_q2_oob_read(bytes, protocol.kind == QA_NET_Q2REPRO_1038, message, &recognized, error);
        if (ok && recognized) ok = qa_q2_status_read(message, rules, sizeof(rules), q2_player, &entry, &recognized, error);
        if (ok && recognized) ok = info(&entry, rules, error);
        free(message); if (!ok || !recognized) return qa_browser_fail(error, "Not a Q2 status response");
    }
    *out = entry; return true;
}
bool qa_browser_master_decode(qa_bytes bytes, qa_net_protocol_id protocol, qa_net_address *addresses,
                               size_t capacity, size_t *count, bool *complete, qa_error *error) {
    *complete = false; *count = 0;
    if (protocol.kind == QA_NET_Q3_68) {
        static const uint8_t prefix[] = {255,255,255,255,'g','e','t','s','e','r','v','e','r','s','R','e','s','p','o','n','s','e'};
        if (bytes.size < sizeof(prefix) || memcmp(bytes.data, prefix, sizeof(prefix))) return qa_browser_fail(error, "Not a Q3 master response");
        size_t offset = sizeof(prefix);
        while (offset < bytes.size && bytes.data[offset] != '\\') ++offset;
        while (offset < bytes.size) {
            if (bytes.data[offset++] != '\\') return qa_browser_fail(error, "Malformed Q3 master separator");
            if (bytes.size - offset >= 3 && !memcmp(bytes.data + offset, "EOT", 3)) { *complete = true; break; }
            if (bytes.size - offset < 7 || bytes.data[offset + 6] != '\\') return qa_browser_fail(error, "Truncated Q3 master record");
            if (*count >= capacity) return qa_browser_fail(error, "Master response exceeds endpoint capacity");
            qa_net_address address = {.kind = QA_NET_IPV4, .port = (uint16_t)((bytes.data[offset + 4] << 8) | bytes.data[offset + 5])};
            memcpy(address.host.ipv4, bytes.data + offset, 4);
            if (address.port) addresses[(*count)++] = address;
            offset += 6;
        }
        return true;
    }
    bool recognized = false;
    if (!qa_q2_master_read(bytes, addresses, capacity, count, &recognized, error) || !recognized)
        return qa_browser_fail(error, "Not a Q2 master response");
    *complete = true; return true;
}
