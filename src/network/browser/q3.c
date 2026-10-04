#include "internal.h"
#include "qa/network_q3.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const qa_net_protocol_id protocol = {QA_NET_Q3_68, 0, 0};
static uint64_t entry_order(const qa_server_browser *b, const qa_net_address *address)
{
    for (uint32_t i = 0; i < b->capacity; ++i) if (b->records[i].occupied &&
        b->records[i].entry.protocol.kind == QA_NET_Q3_68 && qa_net_address_equal(address, &b->records[i].entry.address, true))
        return b->records[i].q3_order;
    return 0;
}
uint32_t qa_browser_q3_source_bit(int32_t source)
{
    static const uint32_t bits[] = {QA_SERVER_LAN, QA_SERVER_SECONDARY_MASTER, QA_SERVER_MASTER, QA_SERVER_FAVORITE};
    return source >= 0 && source < 4 ? bits[source] : 0;
}
bool qa_browser_q3_membership(qa_server_browser *b, const qa_net_address *address,
    uint32_t before, uint32_t after, qa_error *e)
{
    for (int32_t s = 0; s < 4; ++s) {
        uint32_t bit = qa_browser_q3_source_bit(s);
        browser_q3_source *list = &b->q3->sources[s];
        if ((before & bit) == (after & bit)) continue;
        if (list->generation == UINT64_MAX || ((after & bit) && list->count == 8192))
            return qa_browser_fail(e, "Q3 browser source extent exhausted");
    }
    for (int32_t s = 0; s < 4; ++s) {
        uint32_t bit = qa_browser_q3_source_bit(s);
        browser_q3_source *list = &b->q3->sources[s];
        if ((before & bit) == (after & bit)) continue;
        if (after & bit) {
            uint64_t order = entry_order(b, address); uint32_t position = list->count;
            while (position && entry_order(b, &list->addresses[position - 1]) > order) {
                list->addresses[position] = list->addresses[position - 1]; --position;
            }
            list->addresses[position] = *address; ++list->count;
        }
        else for (uint32_t i = 0; i < list->count; ++i)
            if (qa_net_address_equal(address, &list->addresses[i], true)) {
                memmove(&list->addresses[i], &list->addresses[i + 1],
                    (list->count - i - 1) * sizeof(*list->addresses));
                --list->count; break;
            }
        ++list->generation;
    }
    return true;
}
bool qa_server_browser_q3_entry(const qa_server_browser *b, const qa_net_address *a, qa_server_entry *out)
{
    if (!b || !a || !out) return false;
    for (uint32_t i = 0; i < b->capacity; ++i) {
        const browser_record *r = &b->records[i];
        if (r->occupied && r->entry.protocol.kind == QA_NET_Q3_68 &&
            qa_net_address_equal(a, &r->entry.address, true)) { *out = r->entry; return true; }
    }
    return false;
}
static bool send(qa_server_browser *b, const qa_net_address *a, const char *text, qa_error *e)
{
    qa_buffer bytes = {0};
    if (!qa_q3_connectionless_encode(text, &bytes, e)) return false;
    b->callback = true;
    bool ok = b->hooks.send(b->hooks.context, a, (qa_bytes){bytes.data, bytes.size}, e);
    b->callback = false; qa_buffer_free(&bytes); return ok;
}
bool qa_server_browser_q3_request(qa_server_browser *b, const qa_net_address *a,
    qa_browser_q3_request_kind kind, uint64_t now, qa_browser_q3_request_id *out, qa_error *e)
{
    if (!b || b->callback || !a || (a->kind != QA_NET_IPV4 && a->kind != QA_NET_IPV6) || !a->port || !out ||
        kind > QA_BROWSER_Q3_STATUS || !b->next_query)
        return qa_browser_fail(e, "Invalid retained Q3 discovery request");
    browser_q3_request *slot = NULL;
    for (size_t i = 0; i < 48; ++i) if (!b->q3->requests[i].id) { slot = &b->q3->requests[i]; break; }
    if (!slot) return qa_browser_fail(e, "Q3 discovery request slots exhausted");
    uint64_t id = b->next_query++;
    *slot = (browser_q3_request){.id = id, .result = {.address = *a, .kind = kind, .sent_ns = now}};
    char text[64]; (void)snprintf(text, sizeof(text), "%s %" PRIu64,
        kind == QA_BROWSER_Q3_INFO ? "getinfo" : "getstatus", id);
    if (!send(b, a, text, e)) { memset(slot, 0, sizeof(*slot)); return false; }
    *out = id; return true;
}
bool qa_server_browser_q3_result(const qa_server_browser *b, qa_browser_q3_request_id id,
    const qa_browser_q3_result **out, qa_error *e)
{
    if (!b || !id || !out) return qa_browser_fail(e, "Invalid Q3 discovery result lookup");
    for (size_t i = 0; i < 48; ++i) if (b->q3->requests[i].id == id) {
        *out = &b->q3->requests[i].result; return true;
    }
    return qa_browser_fail(e, "Q3 discovery request was retired");
}
bool qa_server_browser_q3_release(qa_server_browser *b, qa_browser_q3_request_id id, qa_error *e)
{
    if (!b || b->callback) return qa_browser_fail(e, "Q3 discovery release inside callback");
    for (size_t i = 0; i < 48; ++i) if (id && b->q3->requests[i].id == id) {
        memset(&b->q3->requests[i], 0, sizeof(b->q3->requests[i])); break;
    }
    return true;
}
bool qa_server_browser_q3_list_read(const qa_server_browser *b, int32_t source,
    qa_browser_q3_list *out, qa_error *e)
{
    if (!b || !out || !qa_browser_q3_source_bit(source)) return qa_browser_fail(e, "Invalid Q3 browser source");
    const browser_q3_source *list = &b->q3->sources[source];
    *out = (qa_browser_q3_list){list->addresses, list->count, list->generation, list->reset_generation,
        b->master_pending && b->q3->master_source == source && !b->q3->master_received};
    return true;
}
bool qa_server_browser_q3_clear(qa_server_browser *b, int32_t source, qa_error *e)
{
    if (!b || b->callback || !qa_browser_q3_source_bit(source)) return qa_browser_fail(e, "Invalid Q3 list reset");
    browser_q3_source *list = &b->q3->sources[source];
    if (list->reset_generation == UINT64_MAX ||
        UINT64_MAX - list->generation < (uint64_t)list->count + 1)
        return qa_browser_fail(e, "Q3 browser reset generation exhausted");
    /* Clear through the shared entry owner, preserving other memberships. */
    while (list->count) if (!qa_server_browser_remove_source(b, &list->addresses[0], protocol,
        qa_browser_q3_source_bit(source), e)) return false;
    ++list->generation; ++list->reset_generation; return true;
}
bool qa_server_browser_q3_scan(qa_server_browser *b, uint64_t now, qa_error *e)
{
    if (!b || b->callback || !b->hooks.local || !b->next_query)
        return qa_browser_fail(e, "Missing genuine Q3 broadcast transport");
    if (!qa_server_browser_q3_clear(b, 0, e)) return false;
    uint64_t query = b->next_query++;
    if (b->q3->broadcast_count >= SIZE_MAX / sizeof(browser_q3_broadcast) - 1)
        return qa_browser_fail(e, "Q3 broadcast allocation extent exhausted");
    browser_q3_broadcast *broadcasts = realloc(b->q3->broadcasts,
        (b->q3->broadcast_count + 1) * sizeof(*broadcasts));
    if (!broadcasts) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q3 broadcast query"); return false; }
    b->q3->broadcasts = broadcasts;
    broadcasts[b->q3->broadcast_count++] = (browser_q3_broadcast){query, now};
    char text[64]; (void)snprintf(text, sizeof(text), "getinfo %" PRIu64, query);
    for (uint16_t i = 0; i < 4; ++i) {
        qa_net_address a = {.kind = QA_NET_IPV4, .port = (uint16_t)(27960 + i), .host.ipv4 = {255,255,255,255}};
        if (!send(b, &a, text, e)) return false;
    }
    return true;
}
void qa_browser_q3_expire(qa_server_browser *b, uint64_t now)
{
    size_t kept = 0;
    for (size_t i = 0; i < b->q3->broadcast_count; ++i) {
        browser_q3_broadcast v = b->q3->broadcasts[i];
        if (now < v.sent_ns || now - v.sent_ns < UINT64_C(3000000000)) b->q3->broadcasts[kept++] = v;
    }
    b->q3->broadcast_count = kept;
}
bool qa_browser_status_enqueue(qa_server_browser *b, const qa_net_address *address,
    qa_net_protocol_id dialect, qa_error *e)
{
    for (size_t i = 0; i < b->q3->status_count; ++i)
        if (b->q3->status_queue[i].protocol.kind == dialect.kind &&
            b->q3->status_queue[i].protocol.revision == dialect.revision &&
            b->q3->status_queue[i].protocol.flags == dialect.flags &&
            qa_net_address_equal(&b->q3->status_queue[i].address, address, true)) return true;
    if (b->q3->status_count >= SIZE_MAX / sizeof(browser_status_query) - 1)
        return qa_browser_fail(e, "Discovery queue extent exhausted");
    browser_status_query *queue = realloc(b->q3->status_queue, (b->q3->status_count + 1) * sizeof(*queue));
    if (!queue) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining ordered source status query"); return false; }
    b->q3->status_queue = queue;
    queue[b->q3->status_count++] = (browser_status_query){*address,dialect}; return true;
}
static unsigned status_family(qa_net_protocol_id dialect)
{
    return dialect.kind<=QA_NET_RMQ999?0:dialect.kind<=QA_NET_QW29?1:
        dialect.kind<=QA_NET_Q2PRIVATE_4038?2:3;
}
bool qa_server_browser_q3_pump(qa_server_browser *b, uint64_t now, qa_error *e)
{
    if (!b || b->callback) return qa_browser_fail(e, "Q3 discovery queue entered during callback");
    for (size_t at=0,sent=0; sent<4 && at<b->q3->status_count;) {
        browser_status_query query=b->q3->status_queue[at]; size_t pending=0;
        if(query.protocol.kind==QA_NET_Q3_68)
            for(size_t i=0;i<48;++i)
                if(b->q3->requests[i].id && !b->q3->requests[i].result.completed) ++pending;
        for(uint32_t i=0;i<b->capacity;++i)
            if(b->records[i].occupied && status_family(b->records[i].entry.protocol)==status_family(query.protocol) &&
                b->records[i].entry.pending) ++pending;
        if(pending>=16) { ++at; continue; }
        memmove(b->q3->status_queue+at,b->q3->status_queue+at+1,
            (--b->q3->status_count-at)*sizeof(*b->q3->status_queue));
        if(!qa_server_browser_query(b,&query.address,query.protocol,false,now,UINT64_C(3000000000),e)) return false;
        ++sent;
    }
    return true;
}
bool qa_server_browser_q3_master(qa_server_browser *b, int32_t source,
    const qa_net_address *a, int32_t version, const char *const *words, size_t count,
    uint64_t now, qa_error *e)
{
    if (!b || b->callback || (source != 1 && source != 2) || !a || a->kind != QA_NET_IPV4 ||
        !a->port || version <= 0 || (count && !words)) return qa_browser_fail(e, "Invalid Q3 master request");
    char text[QA_Q3_MESSAGE_BYTES - 4];
    int first = snprintf(text, sizeof(text), "getservers %" PRId32, version);
    size_t used = (size_t)first;
    for (size_t i = 0; i < count; ++i) {
        if (!words[i]) return qa_browser_fail(e, "Missing Q3 master keyword");
        for (const unsigned char *p = (const unsigned char *)words[i]; *p; ++p)
            if (*p == ' ' || (*p >= 9 && *p <= 13)) return qa_browser_fail(e, "Q3 master keyword contains whitespace");
        size_t n = strlen(words[i]);
        if (n + 1 >= sizeof(text) - used) return qa_browser_fail(e, "Q3 master command exceeds source message");
        text[used++] = ' '; memcpy(text + used, words[i], n + 1); used += n;
    }
    if (!qa_server_browser_q3_clear(b, source, e)) return false;
    qa_server_browser_cancel_master(b);
    b->q3->master_source = source; b->q3->master_received = false;
    b->master = *a; b->master_protocol = protocol; b->master_sent = now;
    b->master_timeout = UINT64_C(3000000000); b->master_pending = true;
    if (!send(b, a, text, e)) { b->master_pending = false; return false; }
    return true;
}
static int32_t atoi32(const char *text)
{
    errno = 0; char *end; long long n = strtoll(text, &end, 10);
    if (end == text) return 0;
    if (n > INT32_MAX) return INT32_MAX;
    if (n < INT32_MIN) return INT32_MIN;
    return (int32_t)n;
}
static void rules_map(const char *raw, char out[8193])
{
    out[0] = 0;
    const char *p = raw; if (*p == '\\') ++p;
    while (*p) {
        const char *key = p; while (*p && *p != '\\') ++p;
        size_t key_size = (size_t)(p - key); if (!*p) break;
        const char *value = ++p; while (*p && *p != '\\') ++p;
        size_t value_size = (size_t)(p - value);
        char *q = out, *old_value = NULL, *old_end = NULL;
        while (*q) {
            char *name = ++q; while (*q && *q != '\\') ++q;
            size_t name_size = (size_t)(q - name);
            char *entry_value = ++q; while (*q && *q != '\\') ++q;
            if (name_size == key_size && !memcmp(name, key, key_size)) { old_value = entry_value; old_end = q; break; }
        }
        if (old_value) {
            size_t tail = strlen(old_end);
            memmove(old_value + value_size, old_end, tail + 1);
            memcpy(old_value, value, value_size);
        } else {
            size_t used = strlen(out); out[used++] = '\\';
            memcpy(out + used, key, key_size); used += key_size; out[used++] = '\\';
            memcpy(out + used, value, value_size); out[used + value_size] = 0;
        }
        if (*p) ++p;
    }
}
bool qa_browser_q3_decode(qa_bytes bytes, qa_browser_q3_result *out, uint64_t *challenge, qa_error *e)
{
    qa_q3_connectionless *packet = malloc(sizeof(*packet));
    qa_q3_tokens *tokens = malloc(sizeof(*tokens));
    if (!packet || !tokens) { free(packet); free(tokens); qa_error_set(e, QA_ERROR_MEMORY, 0, "Decoding Q3 browser response"); return false; }
    bool ok = qa_q3_connectionless_decode(bytes, QA_Q3_CLIENT, packet, e);
    if (!ok) { free(packet); free(tokens); return false; }
    char command[32]; const char *original = qa_q3_token(&packet->tokens, 0);
    size_t command_size = strlen(original);
    if (command_size >= sizeof(command)) { free(packet); free(tokens); return false; }
    for (size_t i = 0; i <= command_size; ++i)
        command[i] = original[i] >= 'A' && original[i] <= 'Z' ? (char)(original[i] + ('a' - 'A')) : original[i];
    bool info = !strcmp(command, "inforesponse"), status = !strcmp(command, "statusresponse");
    if (!ok || (!info && !status)) { free(packet); free(tokens); return false; }
    out->kind = out->response_kind = info ? QA_BROWSER_Q3_INFO : QA_BROWSER_Q3_STATUS;
    char text[QA_Q3_MESSAGE_BYTES]; size_t n = 0;
    while (n < packet->payload_size && packet->payload[n]) {
        unsigned char ch = packet->payload[n]; text[n++] = (char)(ch == '%' || ch > 127 ? '.' : ch);
    }
    text[n] = 0;
    char *line = strchr(text, '\n'); if (line) *line++ = 0;
    if (strlen(text) >= 8192) ok = false;
    char value[8192]; qa_server_entry *entry = &out->entry; entry->protocol = protocol;
    if (ok) { strcpy(entry->rules, text); ok = qa_q3_info_value(text, "protocol", value, sizeof(value), e); }
    if (ok && info && atoi32(value) != 68) ok = false;
    if (ok) ok = qa_q3_info_value(text, "challenge", value, sizeof(value), e);
    if (ok) {
        *challenge = 0;
        for (const char *p = value; *p; ++p) {
            if (*p < '0' || *p > '9' || *challenge > (UINT64_MAX - (unsigned)(*p - '0')) / 10) { ok = false; break; }
            *challenge = *challenge * 10 + (unsigned)(*p - '0');
        }
    }
    if (ok) ok = qa_q3_info_value(text, "hostname", entry->name, sizeof(entry->name), e);
    if (ok && !*entry->name) ok = qa_q3_info_value(text, "sv_hostname", entry->name, sizeof(entry->name), e);
    if (ok) ok = qa_q3_info_value(text, "mapname", entry->map, sizeof(entry->map), e) &&
        qa_q3_info_value(text, "sv_maxclients", value, sizeof(value), e);
    int32_t maximum = ok ? atoi32(value) : 0, clients = 0;
    if (ok && info) { ok = qa_q3_info_value(text, "clients", value, sizeof(value), e); clients = atoi32(value); }
    if (ok) rules_map(text, entry->rules);
    size_t used = strlen(entry->rules);
    if (used >= sizeof(out->status)) used = sizeof(out->status) - 1;
    if (ok) {
        memcpy(out->status, entry->rules, used);
        if (used < sizeof(out->status) - 1) out->status[used++] = '\\';
        out->status[used] = 0;
    }
    while (ok && status && line) {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        if (*line && !qa_q3_tokenize(line, tokens, e)) { ok = false; break; }
        if (*line && tokens->count >= 3) {
            const char *name = qa_q3_token(tokens, 2); size_t name_size = strlen(name) + 1;
            if (out->player_count == 1024 || name_size > sizeof(out->names) - out->names_size) { ok = false; break; }
            qa_browser_q3_player *player = &out->players[out->player_count++];
            player->score = atoi32(qa_q3_token(tokens, 0)); player->ping = atoi32(qa_q3_token(tokens, 1));
            player->name = (uint16_t)out->names_size;
            memcpy(out->names + out->names_size, name, name_size); out->names_size += (uint32_t)name_size;
            ++clients;
            char row[QA_Q3_MESSAGE_BYTES]; (void)snprintf(row, sizeof(row), "\\%" PRId32 " %" PRId32 " \"%s\"",
                atoi32(qa_q3_token(tokens, 0)), atoi32(qa_q3_token(tokens, 1)), qa_q3_token(tokens, 2));
            size_t length = strlen(row), copy = length < sizeof(out->status) - used - 1 ? length : sizeof(out->status) - used - 1;
            memcpy(out->status + used, row, copy); used += copy; out->status[used] = 0;
        }
        line = next;
    }
    if (ok && used < sizeof(out->status) - 1) { out->status[used++] = '\\'; out->status[used] = 0; }
    if (clients < 0 || clients > 1024 || maximum < 0 || maximum > 1024) ok = false;
    entry->players = (uint32_t)clients; entry->maximum_players = (uint32_t)maximum;
    for (char *p = entry->name; *p; ++p) if ((unsigned char)*p < 32 || *p == 127) *p = ' ';
    for (char *p = entry->map; *p; ++p) if ((unsigned char)*p < 32 || *p == 127) *p = ' ';
    free(packet); free(tokens); return ok;
}
bool qa_browser_q3_store_response(browser_record *record, qa_bytes bytes, qa_error *e)
{
    if (!record || !bytes.data || !bytes.size || bytes.size > QA_Q3_MESSAGE_BYTES)
        return qa_browser_fail(e, "Invalid retained Q3 discovery datagram");
    uint8_t *data = malloc(bytes.size);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining complete Q3 discovery datagram"); return false; }
    memcpy(data, bytes.data, bytes.size);
    qa_buffer_free(&record->q3_response); record->q3_response = (qa_buffer){data, bytes.size}; return true;
}
bool qa_browser_q3_response_valid(const browser_record *record)
{
    if (!record->q3_response.size) return !record->q3_response.data &&
        (record->entry.protocol.kind != QA_NET_Q3_68 || !record->entry.available);
    if (!record->occupied || record->entry.protocol.kind != QA_NET_Q3_68 || !record->entry.available ||
        !record->q3_response.data || record->q3_response.size > QA_Q3_MESSAGE_BYTES) return false;
    qa_browser_q3_result *decoded = calloc(1, sizeof(*decoded));
    if (!decoded) return false;
    uint64_t challenge; qa_error ignored = {0};
    bool ok = qa_browser_q3_decode((qa_bytes){record->q3_response.data, record->q3_response.size}, decoded, &challenge, &ignored);
    const qa_server_entry *entry = &record->entry;
    if (ok) ok = entry->players == decoded->entry.players && entry->maximum_players == decoded->entry.maximum_players &&
        !strcmp(entry->name, decoded->entry.name) && !strcmp(entry->map, decoded->entry.map) && !strcmp(entry->rules, decoded->entry.rules);
    free(decoded); return ok;
}
bool qa_server_browser_q3_cached_entry_read(const qa_server_browser *b, const qa_net_address *address,
    qa_browser_q3_cached_entry *out, qa_error *e)
{
    if (!b || !address || !out) return qa_browser_fail(e, "Invalid Q3 cached entry observation");
    for (uint32_t i = 0; i < b->capacity; ++i) {
        const browser_record *record = &b->records[i];
        if (record->occupied && record->entry.protocol.kind == QA_NET_Q3_68 &&
            qa_net_address_equal(&record->entry.address, address, true)) {
            *out = (qa_browser_q3_cached_entry){record->entry,
                {record->q3_response.data, record->q3_response.size}, record->q3_order};
            return true;
        }
    }
    return qa_browser_fail(e, "Q3 cached entry is absent");
}
static bool restored_entry_valid(const qa_browser_q3_cached_entry *saved, qa_error *e)
{
    if (!saved) return qa_browser_fail(e, "Missing Q3 cache entry");
    const qa_server_entry *entry = &saved->entry;
    browser_record qualified = {.occupied = true, .entry = *entry,
        .q3_response = {(uint8_t *)saved->response.data, saved->response.size}};
    if (entry->protocol.kind != QA_NET_Q3_68 || entry->protocol.revision || entry->protocol.flags ||
        entry->address.kind != QA_NET_IPV4 || !entry->address.port || !entry->sources ||
        (entry->sources & ~(uint32_t)(QA_SERVER_MASTER | QA_SERVER_SECONDARY_MASTER | QA_SERVER_FAVORITE)) ||
        entry->pending || entry->timed_out || (entry->has_ping && !entry->available) ||
        (!entry->has_ping && entry->ping_ns) || entry->players > 1024 || entry->maximum_players > 1024 ||
        !memchr(entry->name, 0, sizeof(entry->name)) || !memchr(entry->map, 0, sizeof(entry->map)) ||
        !memchr(entry->rules, 0, sizeof(entry->rules)) || !qa_browser_q3_response_valid(&qualified))
        return qa_browser_fail(e, "Q3 cache entry differs from its genuine response");
    return true;
}
bool qa_server_browser_q3_cached_entry_validate(const qa_browser_q3_cached_entry *saved, qa_error *e)
{
    if (!restored_entry_valid(saved, e)) return false;
    if (saved->response.size) {
        qa_browser_q3_result *decoded = calloc(1, sizeof(*decoded));
        if (!decoded) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Qualifying cached Q3 player names"); return false; }
        uint64_t challenge; bool ok = qa_browser_q3_decode(saved->response, decoded, &challenge, e);
        const char *p = decoded->entry.rules; uint32_t pairs = 0;
        while (ok && *p) {
            const char *key = ++p; while (*p && *p != '\\') ++p;
            if (!*p || p - key > 1024 || ++pairs > 1024) { ok = false; break; }
            ++p; while (*p && *p != '\\') ++p;
        }
        for (uint32_t i = 0; ok && i < decoded->player_count; ++i)
            if (strlen(decoded->names + decoded->players[i].name) > 1024) ok = false;
        free(decoded);
        if (!ok) return qa_browser_fail(e, "Q3 cached rule or player name exceeds source cache limit");
    }
    return true;
}
bool qa_server_browser_q3_restore_entry(qa_server_browser *b, const qa_browser_q3_cached_entry *saved, qa_error *e)
{
    if (!b || b->callback || !restored_entry_valid(saved, e)) return false;
    const qa_server_entry *entry = &saved->entry;
    qa_server_entry live; bool retained = qa_server_browser_q3_entry(b, &entry->address, &live) && live.available;
    if (!qa_server_browser_add(b, &entry->address, protocol, entry->sources, e)) return false;
    if (retained) return true;
    for (uint32_t i = 0; i < b->capacity; ++i) {
        browser_record *record = &b->records[i];
        if (!record->occupied || record->entry.protocol.kind != QA_NET_Q3_68 ||
            !qa_net_address_equal(&record->entry.address, &entry->address, true)) continue;
        if (saved->response.size && !qa_browser_q3_store_response(record, saved->response, e)) return false;
        uint32_t sources = record->entry.sources;
        bool pending = record->entry.pending, timed_out = record->entry.timed_out;
        record->entry = *entry; record->entry.sources = sources;
        record->entry.pending = pending; record->entry.timed_out = timed_out;
        return true;
    }
    return qa_browser_fail(e, "Q3 cache import lost its physical entry");
}
bool qa_browser_q3_receive(qa_server_browser *b, const qa_net_datagram *packet, bool *recognized, qa_error *e)
{
    *recognized = false;
    bool possible = b->q3->broadcast_count != 0;
    for (size_t i = 0; i < 48; ++i) if (b->q3->requests[i].id && !b->q3->requests[i].result.completed &&
        qa_net_address_equal(&b->q3->requests[i].result.address, &packet->from, true)) possible = true;
    if (!possible) return true;
    qa_browser_q3_result *decoded = calloc(1, sizeof(*decoded));
    if (!decoded) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q3 browser response"); return false; }
    uint64_t challenge = 0; qa_error ignored = {0};
    if (!qa_browser_q3_decode(packet->payload, decoded, &challenge, &ignored)) { free(decoded); return true; }
    browser_q3_request *match = NULL;
    for (size_t i = 0; i < 48; ++i) {
        browser_q3_request *r = &b->q3->requests[i];
        if (r->id == challenge && r->id && !r->result.completed &&
            qa_net_address_equal(&r->result.address, &packet->from, true)) { match = r; break; }
    }
    uint64_t sent = match ? match->result.sent_ns : 0;
    bool broadcast = false;
    if (!match) for (size_t i = 0; i < b->q3->broadcast_count; ++i)
        if (challenge == b->q3->broadcasts[i].query) {
            broadcast = true; sent = b->q3->broadcasts[i].sent_ns; break;
        }
    if (packet->received_ns < sent || (!match && !broadcast)) { free(decoded); return true; }
    if (broadcast) {
        b->callback = true; bool local = b->hooks.local && b->hooks.local(b->hooks.context, &packet->from); b->callback = false;
        if (!local) { free(decoded); return true; }
    }
    qa_server_entry previous;
    bool existing = qa_server_browser_q3_entry(b, &packet->from, &previous);
    if (!existing || broadcast) if (!qa_server_browser_add(b, &packet->from, protocol,
        broadcast ? QA_SERVER_LAN : QA_SERVER_DIRECT, e)) { free(decoded); return false; }
    if (!qa_server_browser_q3_entry(b, &packet->from, &previous)) { free(decoded); return false; }
    decoded->address = packet->from; decoded->sent_ns = sent; decoded->completed_ns = packet->received_ns; decoded->completed = true;
    decoded->entry.address = packet->from; decoded->entry.sources = previous.sources;
    decoded->entry.updated_ns = packet->received_ns; decoded->entry.ping_ns = packet->received_ns - sent;
    decoded->entry.available = true; decoded->entry.has_ping = true;
    for (uint32_t i = 0; i < b->capacity; ++i) if (b->records[i].occupied &&
        b->records[i].entry.protocol.kind == QA_NET_Q3_68 && qa_net_address_equal(&b->records[i].entry.address, &packet->from, true)) {
        bool pending = b->records[i].entry.pending, timed_out = b->records[i].entry.timed_out;
        if (!qa_browser_q3_store_response(&b->records[i], packet->payload, e)) { free(decoded); return false; }
        b->records[i].entry = decoded->entry;
        b->records[i].entry.pending = pending; b->records[i].entry.timed_out = timed_out;
        break;
    }
    if (match) { decoded->kind = match->result.kind; match->result = *decoded; }
    if (b->hooks.changed) { b->callback = true; b->hooks.changed(b->hooks.context, &decoded->entry); b->callback = false; }
    free(decoded); *recognized = true; return true;
}
