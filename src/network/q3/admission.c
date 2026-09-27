/* Donor: network/q3/connectionless.ts, admission.ts and authorization modules. */
#include "qa/network_q3.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e, qa_status code, const char *s) { qa_error_set(e, code, 0, "%s", s); return false; }
static unsigned char info_fold(unsigned char byte) {
    return byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte + ('a' - 'A')) : byte;
}
static bool equal(const char *a, const char *b) {
    while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    return *a == *b;
}
static int32_t number(const char *s) {
    bool negative = false;
    while (isspace((unsigned char)*s)) ++s;
    if (*s == '-' || *s == '+') negative = *s++ == '-';
    uint32_t n = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    while (*s >= '0' && *s <= '9') {
        unsigned digit = (unsigned)(*s++ - '0');
        if (n > (limit - digit) / 10) return negative ? INT32_MIN : INT32_MAX;
        n = n * 10 + digit;
    }
    if (negative) return n == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)n;
    return (int32_t)n;
}
const char *qa_q3_token(const qa_q3_tokens *t, size_t i) { return i < t->count ? t->text + t->offsets[i] : ""; }
bool qa_q3_tokenize(const char *s, qa_q3_tokens *out, qa_error *error) {
    if (!s || !out) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 command tokens");
    out->count = 0; out->truncated = false; size_t used = 0;
    while (*s) {
        while (*s && ((unsigned char)*s <= 32 || (unsigned char)*s > 127)) ++s;
        if (!*s || (s[0] == '/' && s[1] == '/')) break;
        if (s[0] == '/' && s[1] == '*') {
            s += 2;
            while (*s && !(s[0] == '*' && s[1] == '/')) ++s;
            if (*s) s += 2;
            continue;
        }
        if (out->count >= 1024) { out->truncated = true; break; }
        if (used >= sizeof(out->text)) return fail(error, QA_ERROR_FORMAT, "Q3 command token storage exhausted");
        out->offsets[out->count++] = (uint16_t)used;
        bool quoted = *s == '"';
        if (quoted) ++s;
        while (*s) {
            if (quoted ? *s == '"' : ((unsigned char)*s <= 32 || (unsigned char)*s > 127 || *s == '"'
                || (s[0] == '/' && (s[1] == '/' || s[1] == '*')))) break;
            if (used >= sizeof(out->text) - 1) return fail(error, QA_ERROR_FORMAT, "Q3 command token storage exceeds 9215 bytes");
            out->text[used++] = *s++;
        }
        out->text[used++] = 0;
        if (quoted && *s == '"') ++s;
    }
    if (!out->count) out->text[0] = 0;
    return true;
}
bool qa_q3_info_value(const char *info, const char *key, char *out, size_t capacity, qa_error *error) {
    if (!info || !key || !out || !capacity) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 info lookup");
    if (strlen(info) >= 8192) return fail(error, QA_ERROR_FORMAT, "Q3 info exceeds 8191 bytes");
    out[0] = 0;
    const char *p = info;
    if (*p == '\\') ++p;
    while (*p) {
        const char *name = p;
        while (*p && *p != '\\') ++p;
        size_t namesize = (size_t)(p - name);
        if (!*p) break;
        const char *value = ++p;
        while (*p && *p != '\\') ++p;
        size_t length = (size_t)(p - value);
        bool match = strlen(key) == namesize;
        for (size_t i = 0; match && i < namesize; ++i)
            match = info_fold((unsigned char)name[i]) == info_fold((unsigned char)key[i]);
        if (match) {
            if (length >= capacity) return fail(error, QA_ERROR_FORMAT, "Q3 info lookup output too small");
            memcpy(out, value, length); out[length] = 0; return true;
        }
        if (*p) ++p;
    }
    return true;
}
bool qa_q3_info_set(char *info, size_t capacity, const char *key, const char *value, qa_error *error) {
    if (!info || !key || !value || !capacity || capacity > 8192)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 info assignment");
    if (strpbrk(key, "\\;\"") || strpbrk(value, "\\;\"")) return fail(error, QA_ERROR_FORMAT, "Illegal Q3 info key/value");
    size_t length = strlen(info), key_length = strlen(key);
    if (length >= capacity) return fail(error, QA_ERROR_FORMAT, "Q3 info exceeds capacity");
    size_t remove_begin = 0, remove_end = 0;
    const char *p = info;
    while (*p) {
        const char *begin = p;
        if (*p == '\\') ++p;
        const char *name = p;
        while (*p && *p != '\\') ++p;
        size_t namesize = (size_t)(p - name);
        if (!*p) break;
        ++p;
        while (*p && *p != '\\') ++p;
        if (namesize == key_length && !memcmp(name, key, key_length)) {
            remove_begin = (size_t)(begin - info);
            remove_end = (size_t)(p - info);
            break;
        }
    }
    char buffer[8192];
    size_t retained = length - (remove_end - remove_begin), pair_size = 0;
    if (*value) {
        size_t remaining = capacity - 1;
        const char *parts[] = {"\\", key, "\\", value};
        size_t sizes[] = {1, key_length, 1, strlen(value)};
        for (size_t i = 0; i < 4; ++i) {
            size_t amount = sizes[i] < remaining ? sizes[i] : remaining;
            pair_size += amount;
            remaining -= amount;
        }
        if (pair_size >= capacity - retained)
            return fail(error, QA_ERROR_FORMAT, "Q3 info storage exhausted");
        size_t offset = capacity == 8192 ? retained : 0;
        remaining = pair_size;
        for (size_t i = 0; i < 4; ++i) {
            size_t amount = sizes[i] < remaining ? sizes[i] : remaining;
            memcpy(buffer + offset, parts[i], amount);
            offset += amount;
            remaining -= amount;
        }
    }
    size_t offset = capacity == 8192 ? 0 : pair_size;
    memcpy(buffer + offset, info, remove_begin);
    memcpy(buffer + offset + remove_begin, info + remove_end, length - remove_end);
    buffer[retained + pair_size] = 0;
    memcpy(info, buffer, retained + pair_size + 1);
    return true;
}
bool qa_q3_is_lan(const qa_net_address *a) {
    if (a->kind == QA_NET_LOOPBACK || a->kind == QA_NET_IPX) return true;
    if (a->kind == QA_NET_IPV6) {
        const uint8_t *v = a->host.ipv6.bytes;
        bool loopback = v[15] == 1;
        for (unsigned i = 0; i < 15; ++i) loopback = loopback && v[i] == 0;
        return loopback || (v[0] & 0xfe) == 0xfc || (v[0] == 0xfe && (v[1] & 0xc0) == 0x80);
    }
    const uint8_t *v = a->host.ipv4;
    return v[0] == 127 || v[0] == 10 || (v[0] == 192 && v[1] == 168) || (v[0] == 172 && v[1] >= 16 && v[1] <= 31);
}
bool qa_q3_connectionless_encode(const char *text, qa_buffer *out, qa_error *error) {
    if (!text || !out) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 connectionless text");
    size_t length = strlen(text);
    if (length > QA_Q3_MESSAGE_BYTES - 5) return fail(error, QA_ERROR_FORMAT, "Q3 connectionless packet too large");
    uint8_t *data = malloc(length + 4);
    if (!data) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 connectionless packet");
    memset(data, 255, 4); memcpy(data + 4, text, length);
    *out = (qa_buffer){data, length + 4}; return true;
}
bool qa_q3_connect_encode(const char *info, qa_buffer *out, qa_error *error) {
    if (!info || !out || strlen(info) > 1013 || strpbrk(info, "\"\n\r")) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 connect userinfo");
    size_t length = strlen(info);
    uint8_t text[1016]; text[0] = '"'; memcpy(text + 1, info, length); text[length + 1] = '"';
    qa_buffer compressed = {0};
    if (!qa_q3_huffman_compress((qa_bytes){text, length + 2}, &compressed, error)) return false;
    uint8_t *data = malloc(12 + compressed.size);
    if (!data) { qa_buffer_free(&compressed); return fail(error, QA_ERROR_MEMORY, "Allocating Q3 connect packet"); }
    memset(data, 255, 4); memcpy(data + 4, "connect ", 8); memcpy(data + 12, compressed.data, compressed.size);
    *out = (qa_buffer){data, 12 + compressed.size}; qa_buffer_free(&compressed); return true;
}
bool qa_q3_connectionless_decode(qa_bytes data, qa_q3_role receiver, qa_q3_connectionless *out, qa_error *error) {
    if (!out || !data.data || data.size < 4 || data.size >= QA_Q3_MESSAGE_BYTES)
        return fail(error, QA_ERROR_FORMAT, "Invalid Q3 connectionless size");
    for (size_t i = 0; i < 4; ++i) if (data.data[i] != 255) return fail(error, QA_ERROR_FORMAT, "Missing Q3 connectionless marker");
    qa_buffer expanded = {0};
    uint8_t joined[QA_Q3_MESSAGE_BYTES];
    memset(out, 0, sizeof(*out));
    if (receiver == QA_Q3_SERVER && data.size > 12 && !memcmp(data.data + 4, "connect", 7)) {
        if (!qa_q3_huffman_decompress((qa_bytes){data.data + 12, data.size - 12}, sizeof(joined) - 12, &expanded, error)) return false;
        memcpy(joined, data.data, 12); if (expanded.size) memcpy(joined + 12, expanded.data, expanded.size);
        data = (qa_bytes){joined, 12 + expanded.size}; out->compressed = true;
    }
    size_t offset = 4, used = 0;
    while (offset < data.size && used < sizeof(out->line) - 1) {
        unsigned ch = data.data[offset++];
        if (!ch || ch == '\n') break;
        out->line[used++] = (char)(ch == '%' ? '.' : ch);
    }
    out->line[used] = 0;
    out->payload_size = data.size - offset;
    if (out->payload_size) memcpy(out->payload, data.data + offset, out->payload_size);
    qa_buffer_free(&expanded);
    return qa_q3_tokenize(out->line, &out->tokens, error);
}
static bool reply(qa_q3_send_fn send, void *user, const qa_net_address *to, const char *text, qa_error *error) {
    qa_buffer buffer = {0};
    if (!qa_q3_connectionless_encode(text, &buffer, error)) return false;
    bool ok = send(user, to, (qa_bytes){buffer.data, buffer.size}, error); qa_buffer_free(&buffer); return ok;
}
void qa_q3_client_admission_begin(qa_q3_client_admission *c, const qa_net_address *address, uint16_t qport) {
    memset(c, 0, sizeof(*c)); c->qport = qport; c->address = *address; c->connect_time = -99999;
    c->phase = address->kind == QA_NET_LOOPBACK ? QA_Q3_CHALLENGING : QA_Q3_CONNECTING;
}
bool qa_q3_client_admission_resend(qa_q3_client_admission *c, int64_t now, const char *userinfo,
                                   qa_q3_send_fn send, void *user, qa_error *error) {
    if (!c || !send || !userinfo) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 connect resend arguments");
    if ((c->phase != QA_Q3_CONNECTING && c->phase != QA_Q3_CHALLENGING) || now - c->connect_time < 3000) return true;
    c->connect_time = now; ++c->connect_packets;
    if (c->phase == QA_Q3_CONNECTING) return reply(send, user, &c->address, "getchallenge", error);
    char info[1024], text[32];
    if (strlen(userinfo) >= sizeof(info)) return fail(error, QA_ERROR_FORMAT, "Q3 connect userinfo too large");
    strcpy(info, userinfo);
    if (!qa_q3_info_set(info, sizeof(info), "protocol", "68", error)) return false;
    snprintf(text, sizeof(text), "%u", (unsigned)c->qport);
    if (!qa_q3_info_set(info, sizeof(info), "qport", text, error)) return false;
    snprintf(text, sizeof(text), "%d", c->challenge);
    if (!qa_q3_info_set(info, sizeof(info), "challenge", text, error)) return false;
    qa_buffer buffer = {0};
    if (!qa_q3_connect_encode(info, &buffer, error)) return false;
    bool ok = send(user, &c->address, (qa_bytes){buffer.data, buffer.size}, error); qa_buffer_free(&buffer); return ok;
}
bool qa_q3_client_admission_receive(qa_q3_client_admission *c, const qa_net_address *from, qa_bytes data,
                                    int64_t now, qa_q3_admission_result *result, qa_q3_connectionless *packet, qa_error *error) {
    if (!c || !from || !result || !packet || (data.size && !data.data)) return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 client admission input");
    *result = QA_Q3_ADMISSION_IGNORED; c->last_packet_time = now;
    if (data.size >= 4 && !memcmp(data.data, "\xff\xff\xff\xff", 4)) {
        if (!qa_q3_connectionless_decode(data, QA_Q3_CLIENT, packet, error)) return false;
        const char *command = qa_q3_token(&packet->tokens, 0);
        if (equal(command, "challengeResponse")) {
            if (c->phase == QA_Q3_CONNECTING) {
                c->challenge = number(qa_q3_token(&packet->tokens, 1)); c->phase = QA_Q3_CHALLENGING;
                c->connect_packets = 0; c->connect_time = -99999; c->address = *from;
            }
            *result = QA_Q3_ADMISSION_HANDLED;
        } else if (equal(command, "connectResponse")) {
            if (c->phase == QA_Q3_CHALLENGING && qa_net_address_equal(from, &c->address, false)) {
                c->phase = QA_Q3_ADMITTED; c->address = *from; *result = QA_Q3_ADMISSION_CONNECTED;
            }
        } else *result = QA_Q3_ADMISSION_QUERY;
    } else if (c->phase == QA_Q3_ADMITTED && qa_net_address_equal(from, &c->address, true)) *result = QA_Q3_ADMISSION_SEQUENCED;
    return true;
}

struct qa_q3_server_admission { qa_q3_admission_hooks hooks; qa_q3_challenge challenges[1024]; };
bool qa_q3_server_admission_create(const qa_q3_admission_hooks *hooks, qa_q3_server_admission **out, qa_error *error) {
    if (!hooks || !hooks->random || !hooks->send || !hooks->admit || !hooks->query || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 admission requires shared authority, transport and query callbacks");
    qa_q3_server_admission *s = calloc(1, sizeof(*s));
    if (!s) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 challenges");
    s->hooks = *hooks; *out = s; return true;
}
void qa_q3_server_admission_destroy(qa_q3_server_admission *s) { free(s); }
static bool server_reply(qa_q3_server_admission *s, const qa_net_address *to, const char *text, qa_error *e) {
    return reply(s->hooks.send, s->hooks.context, to, text, e);
}
static bool challenge_reply(qa_q3_server_admission *s, qa_q3_challenge *c, int64_t now, qa_error *e) {
    char text[64]; snprintf(text, sizeof(text), "challengeResponse %d", c->challenge); c->ping_time = now;
    return server_reply(s, &c->address, text, e);
}
static bool server_challenge(qa_q3_server_admission *s, const qa_net_address *from, int64_t now, qa_error *e) {
    qa_q3_challenge *found = NULL, *oldest = &s->challenges[0];
    for (size_t i = 0; i < 1024; ++i) {
        qa_q3_challenge *c = &s->challenges[i];
        if (c->present && !c->connected && qa_net_address_equal(from, &c->address, true)) { found = c; break; }
        if (c->time < oldest->time) oldest = c;
    }
    if (!found) {
        found = oldest; memset(found, 0, sizeof(*found)); found->present = true; found->address = *from;
        uint32_t a = s->hooks.random(s->hooks.context), b = s->hooks.random(s->hooks.context);
        uint32_t raw = (a << 16) ^ b ^ (uint32_t)now;
        memcpy(&found->challenge, &raw, sizeof(raw)); found->time = found->first_time = now;
    }
    if (qa_q3_is_lan(from) || now - found->first_time > 5000) return challenge_reply(s, found, now, e);
    return !s->hooks.authorize || s->hooks.authorize(s->hooks.context, found, e);
}
static bool server_authorize(qa_q3_server_admission *s, const qa_q3_admission_options *o,
                             const qa_net_address *from, const qa_q3_tokens *tokens, int64_t now, qa_error *e) {
    if (from->kind != QA_NET_IPV4 || !o->authorize_address || o->authorize_address->kind != QA_NET_IPV4
        || !qa_net_address_equal(from, o->authorize_address, false)) return true;
    int32_t n = number(qa_q3_token(tokens, 1));
    for (size_t i = 0; i < 1024; ++i) {
        qa_q3_challenge *c = &s->challenges[i];
        if (!c->present || c->challenge != n) continue;
        const char *result = qa_q3_token(tokens, 2);
        if (equal(result, "accept") || (equal(result, "demo") && o->demo_restricted)) return challenge_reply(s, c, now, e);
        char text[1100];
        snprintf(text, sizeof(text), "print\n%s\n", equal(result, "demo") ? "Server is not a demo server" : qa_q3_token(tokens, 3));
        bool ok = server_reply(s, &c->address, text, e); memset(c, 0, sizeof(*c)); return ok;
    }
    return true;
}
static bool server_connect(qa_q3_server_admission *s, const qa_q3_admission_options *o,
                           const qa_q3_admission_slot *slots, size_t count,
                           const qa_net_address *from, const char *input, int64_t now, qa_error *e) {
    qa_q3_accepted_connect request = {.address = *from};
    if (strlen(input) >= sizeof(request.userinfo)) return fail(e, QA_ERROR_FORMAT, "Q3 connect userinfo exceeds 1023 bytes");
    strcpy(request.userinfo, input); char value[1024];
    if (!qa_q3_info_value(input, "protocol", value, sizeof(value), e)) return false;
    if (number(value) != 68) return server_reply(s, from, "print\nServer uses protocol version 68.\n", e);
    if (!qa_q3_info_value(input, "qport", value, sizeof(value), e)) return false;
    int32_t port = number(value);
    if (port < 0 || port > UINT16_MAX) return server_reply(s, from, "print\nInvalid qport.\n", e);
    request.qport = (uint16_t)port;
    if (!qa_q3_info_value(input, "challenge", value, sizeof(value), e)) return false;
    request.challenge = number(value);
    const qa_q3_admission_slot *selected = NULL;
    for (size_t i = 0; i < count; ++i) {
        const qa_q3_admission_slot *slot = &slots[i];
        if (slot->phase != QA_Q3_FREE && qa_net_address_equal(from, &slot->address, false)
            && (slot->qport == request.qport || (from->kind != QA_NET_LOOPBACK && slot->address.port == from->port))) {
            if (now - slot->last_connect_time < (int64_t)o->reconnect_limit_seconds * 1000) return true;
            selected = slot; break;
        }
    }
    if (from->kind != QA_NET_LOOPBACK) {
        qa_q3_challenge *challenge = NULL;
        for (size_t i = 0; i < 1024; ++i) if (s->challenges[i].present
            && s->challenges[i].challenge == request.challenge && qa_net_address_equal(from, &s->challenges[i].address, true)) { challenge = &s->challenges[i]; break; }
        if (!challenge) return server_reply(s, from, "print\nNo or bad challenge for address.\n", e);
        if (!qa_net_address_format(from, value, sizeof(value), e) || !qa_q3_info_set(request.userinfo, sizeof(request.userinfo), "ip", value, e)) return false;
        int64_t ping = now - challenge->ping_time; challenge->connected = true;
        if (!qa_q3_is_lan(from)) {
            if (o->minimum_ping && ping < o->minimum_ping) {
                challenge->address.port = 0; return server_reply(s, from, "print\nServer is for high pings only\n", e);
            }
            if (o->maximum_ping && ping > o->maximum_ping) return server_reply(s, from, "print\nServer is for low pings only\n", e);
        }
    } else if (!qa_q3_info_set(request.userinfo, sizeof(request.userinfo), "ip", "localhost", e)) return false;
    if (!qa_q3_info_value(input, "password", value, sizeof(value), e)) return false;
    uint32_t start = !strcmp(value, o->private_password ? o->private_password : "") ? 0 : o->private_clients;
    if (!selected) for (size_t i = 0; i < count; ++i) if (slots[i].slot >= start && slots[i].phase == QA_Q3_FREE) { selected = &slots[i]; break; }
    if (!selected) {
        if (from->kind != QA_NET_LOOPBACK) return server_reply(s, from, "print\nServer is full.\n", e);
        for (size_t i = 0; i < count; ++i) if (slots[i].slot >= start) {
            if (!slots[i].bot) return fail(e, QA_ERROR_FORMAT, "Q3 server is full on local connect");
            selected = &slots[i];
        }
        if (!selected || !s->hooks.drop_bot) return fail(e, QA_ERROR_FORMAT, "Q3 local connect has no replaceable bot");
        if (!s->hooks.drop_bot(s->hooks.context, selected->slot, e)) return false;
    }
    request.slot = selected->slot;
    char rejection[1024] = {0};
    if (!s->hooks.admit(s->hooks.context, &request, rejection, e)) return false;
    if (*rejection) { char text[1040]; snprintf(text, sizeof(text), "print\n%s\n", rejection); return server_reply(s, from, text, e); }
    return server_reply(s, from, "connectResponse", e);
}
bool qa_q3_server_admission_receive(qa_q3_server_admission *s, const qa_q3_admission_options *o,
                                    const qa_q3_admission_slot *slots, size_t count,
                                    const qa_net_address *from, qa_bytes data, int64_t now, qa_error *e) {
    if (!s || !o || !from || (count && !slots)) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 server admission context");
    qa_q3_connectionless packet;
    if (!qa_q3_connectionless_decode(data, QA_Q3_SERVER, &packet, e)) return false;
    const char *command = qa_q3_token(&packet.tokens, 0);
    if (equal(command, "getchallenge")) return server_challenge(s, from, now, e);
    if (equal(command, "ipAuthorize")) return server_authorize(s, o, from, &packet.tokens, now, e);
    if (equal(command, "connect")) return server_connect(s, o, slots, count, from, qa_q3_token(&packet.tokens, 1), now, e);
    return s->hooks.query(s->hooks.context, from, &packet, e);
}
void qa_q3_server_admission_disconnect(qa_q3_server_admission *s, const qa_net_address *address) {
    for (size_t i = 0; i < 1024; ++i) if (s->challenges[i].present && qa_net_address_equal(address, &s->challenges[i].address, true)) {
        s->challenges[i].connected = false; return;
    }
}
const qa_q3_admission_slot *qa_q3_route(const qa_net_address *from, qa_bytes packet, const qa_q3_admission_slot *slots, size_t count) {
    if (!from || !packet.data || packet.size < 6 || (count && !slots)) return NULL;
    uint16_t qport = (uint16_t)(packet.data[4] | (uint16_t)packet.data[5] << 8);
    for (size_t i = 0; i < count; ++i) if (slots[i].phase != QA_Q3_FREE && slots[i].qport == qport
        && qa_net_address_equal(from, &slots[i].address, false)) return &slots[i];
    return NULL;
}
bool qa_q3_authorize_server_packet(const qa_q3_challenge *c, const char *game, const char *strict, qa_buffer *out, qa_error *e) {
    if (!c || !c->present || c->address.kind != QA_NET_IPV4 || !game || !strict || strlen(game) >= 1024
        || strpbrk(game, "\r\n ") || strpbrk(strict, "\r\n ") || strlen(strict) >= 128)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 IP authorization request");
    char text[1280]; const uint8_t *v = c->address.host.ipv4;
    snprintf(text, sizeof(text), "getIpAuthorize %d %u.%u.%u.%u %s 0 %s", c->challenge,
        v[0], v[1], v[2], v[3], *game ? game : "baseq3", strict);
    return qa_q3_connectionless_encode(text, out, e);
}
bool qa_q3_authorize_client_packet(const char *key, bool demo, int32_t anonymous, qa_buffer *out, qa_error *e) {
    if (!key || !out) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 key authorization request");
    char sanitized[33]; size_t n = 0;
    for (size_t i = 0; i < 32 && key[i]; ++i) if (isalnum((unsigned char)key[i]) && (unsigned char)key[i] < 128) sanitized[n++] = key[i];
    sanitized[n] = 0;
    char text[80]; snprintf(text, sizeof(text), "getKeyAuthorize %d %s", anonymous, demo ? "demota" : sanitized);
    return qa_q3_connectionless_encode(text, out, e);
}
