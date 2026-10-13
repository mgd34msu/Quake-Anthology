#include "internal.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q2.h"
#include "qa/network_q2_kex.h"
#include "qa/network_q3.h"
#include "qa/server_browser_kex.h"
#include "qa/server_browser_details.h"
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
bool qa_browser_status_decode(const qa_server_browser *browser, qa_bytes bytes, qa_net_protocol_id protocol,
                               qa_server_entry *out, uint64_t *challenge, qa_error *error) {
    qa_server_entry entry = {.protocol = protocol}; *challenge = 0;
    if (protocol.kind <= QA_NET_QW29) {
        size_t slot;
        qa_arena *scratch = qa_browser_decode_acquire(browser, &slot, error);
        if (!scratch) return false;
        qa_q1_discovery status = {0};
        bool ok = qa_q1_discovery_read(bytes, qa_q1_is_qw(protocol), scratch, &status, error);
        if (ok) {
            ok = text_copy(entry.name, sizeof(entry.name), status.name, error) &&
                text_copy(entry.map, sizeof(entry.map), status.map, error);
            entry.players = status.players; entry.maximum_players = status.max_players;
        }
        if (ok && status.rules.count) {
            size_t used=0;
            for (size_t i = 0; i < status.rules.count; ++i) {
                const char *key=status.rules.rules[i].name,*value=status.rules.rules[i].value;
                size_t key_size=strlen(key),value_size=strlen(value);
                if(key_size>sizeof(entry.rules)-used-1 || value_size>sizeof(entry.rules)-used-key_size-1 ||
                    key_size+value_size+2>=sizeof(entry.rules)-used) break;
                entry.rules[used++]='\\'; memcpy(entry.rules+used,key,key_size); used+=key_size;
                entry.rules[used++]='\\'; memcpy(entry.rules+used,value,value_size); used+=value_size;
                entry.rules[used]=0;
            }
        }
        qa_q1_discovery_free(&status); qa_browser_decode_release(browser, slot);
        if (!ok) return false;
    } else if (protocol.kind == QA_NET_Q2KEX_2023) {
        if (!qa_browser_kex_decode(bytes, &entry, error)) return false;
        entry.protocol = protocol;
    } else if (protocol.kind == QA_NET_Q3_68) {
        qa_browser_q3_result result_storage = {0}; qa_browser_q3_result *result = &result_storage;
        bool ok = qa_browser_q3_decode(bytes, result, challenge, error);
        if (ok) entry = result->entry;
        if (!ok) return false;
    } else {
        qa_q2_oob message_storage; qa_q2_oob *message = &message_storage; bool recognized = false;
        char rules[8193];
        bool ok = qa_q2_oob_read(bytes, protocol.kind == QA_NET_Q2REPRO_1038, message, &recognized, error);
        if (ok && recognized) {
            size_t slot;
            qa_arena *scratch = qa_browser_decode_acquire(browser, &slot, error);
            ok = scratch && qa_q2_status_read(message, rules, sizeof(rules), scratch,
                q2_player, &entry, &recognized, error);
            if (scratch) qa_browser_decode_release(browser, slot);
        }
        if (ok && recognized) ok = info(&entry, rules, error);
        if (!ok || !recognized) return qa_browser_fail(error, "Not a Q2 status response");
    }
    *out = entry; return true;
}
bool qa_browser_status_response_valid(const qa_server_browser *browser, const browser_record *record) {
    if (!record->status_response.size) return !record->status_response.data &&
        (record->entry.protocol.kind == QA_NET_Q3_68 || !record->entry.available);
    if (!record->occupied || !record->entry.available || record->entry.protocol.kind == QA_NET_Q3_68 ||
        !record->status_response.data || record->status_response.size > 65535) return false;
    qa_server_entry decoded; uint64_t challenge; qa_error ignored = {0};
    return qa_browser_status_decode(browser, (qa_bytes){record->status_response.data, record->status_response.size},
        record->entry.protocol, &decoded, &challenge, &ignored) &&
        decoded.players == record->entry.players && decoded.maximum_players == record->entry.maximum_players &&
        !strcmp(decoded.name, record->entry.name) && !strcmp(decoded.map, record->entry.map) &&
        !strcmp(decoded.rules, record->entry.rules);
}
typedef struct detail_builder {
    qa_server_browser_details *out;
    qa_arena *storage;
    size_t player_capacity, rule_capacity;
} detail_builder;
static bool detail_text(qa_arena *storage, qa_buffer *out, qa_bytes text, qa_error *error) {
    if (text.size == SIZE_MAX || (text.size && !text.data))
        return qa_browser_fail(error, "Invalid discovery detail text");
    out->data = storage ? qa_arena_alloc(storage, text.size + 1, 1, error) : malloc(text.size + 1);
    if (!out->data) {
        if (!storage) qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying received discovery detail text");
        return false;
    }
    if (text.size) memcpy(out->data, text.data, text.size);
    out->data[text.size] = 0; out->size = text.size;
    return true;
}
static void *detail_rows(detail_builder *builder, void *rows, size_t count,
    size_t *capacity, size_t stride, size_t alignment, qa_error *error) {
    if (count < *capacity) return rows;
    size_t needed = count + 1;
    size_t extent = *capacity <= SIZE_MAX / 2 ? *capacity * 2 : needed;
    if (extent < needed || extent > SIZE_MAX / stride) extent = needed;
    void *next = builder->storage ? qa_arena_grow(builder->storage, rows,
        count * stride, extent * stride, alignment, error) : realloc(rows, extent * stride);
    if (!next) {
        if (!builder->storage) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining received discovery detail rows");
        return NULL;
    }
    *capacity = extent; return next;
}
static bool detail_player(void *context, int32_t score, int32_t ping, const char *name, qa_error *error) {
    detail_builder *builder = context; qa_server_browser_details *out = builder->out;
    if (out->player_count >= SIZE_MAX / sizeof(*out->players))
        return qa_browser_fail(error, "Discovery detail row extent exhausted");
    qa_server_browser_player *rows = detail_rows(builder, out->players, out->player_count,
        &builder->player_capacity, sizeof(*rows), _Alignof(qa_server_browser_player), error);
    if (!rows) return false;
    out->players = rows;
    qa_server_browser_player *row = rows + out->player_count++;
    *row = (qa_server_browser_player){.score = score, .ping = ping};
    return detail_text(builder->storage, &row->name, (qa_bytes){(const uint8_t *)name, strlen(name)}, error);
}
static bool detail_rule(detail_builder *builder, qa_bytes name, qa_bytes value, qa_error *error) {
    qa_server_browser_details *out = builder->out;
    if (out->rule_count >= SIZE_MAX / sizeof(*out->rules))
        return qa_browser_fail(error, "Discovery rule extent exhausted");
    qa_server_browser_rule *rows = detail_rows(builder, out->rules, out->rule_count,
        &builder->rule_capacity, sizeof(*rows), _Alignof(qa_server_browser_rule), error);
    if (!rows) return false;
    out->rules = rows;
    qa_server_browser_rule *row = rows + out->rule_count++;
    *row = (qa_server_browser_rule){0};
    return detail_text(builder->storage, &row->name, name, error) &&
        detail_text(builder->storage, &row->value, value, error);
}
static bool detail_info(detail_builder *builder, const char *text, qa_error *error) {
    const char *p = text;
    while (*p) {
        if (*p++ != '\\') return qa_browser_fail(error, "Received discovery rules lack an info delimiter");
        const char *name = p;
        while (*p && *p != '\\') ++p;
        if (!*p) return qa_browser_fail(error, "Received discovery rule lacks a value");
        size_t name_size = (size_t)(p++ - name);
        const char *value = p;
        while (*p && *p != '\\') ++p;
        if (!detail_rule(builder, (qa_bytes){(const uint8_t *)name,name_size},
            (qa_bytes){(const uint8_t *)value,(size_t)(p-value)},error)) return false;
    }
    return true;
}
void qa_server_browser_details_free(qa_server_browser_details *out) {
    if (!out) return;
    for (size_t i = 0; i < out->player_count; ++i) qa_buffer_free(&out->players[i].name);
    for (size_t i = 0; i < out->rule_count; ++i) {
        qa_buffer_free(&out->rules[i].name); qa_buffer_free(&out->rules[i].value);
    }
    free(out->players); free(out->rules); qa_buffer_free(&out->response);
    *out = (qa_server_browser_details){0};
}
static const browser_record *detail_record(const qa_server_browser *browser,
    const qa_net_address *address, qa_net_protocol_id protocol) {
    if (!browser || !address) return NULL;
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        const browser_record *r = browser->records + i;
        if (r->occupied && r->entry.protocol.kind == protocol.kind && r->entry.protocol.revision == protocol.revision &&
            r->entry.protocol.flags == protocol.flags && qa_net_address_equal(&r->entry.address,address,true)) return r;
    }
    return NULL;
}
static bool details_read(const qa_server_browser *browser, const qa_net_address *address,
    qa_net_protocol_id protocol, qa_arena *storage, qa_server_browser_details *out, qa_error *error) {
    const browser_record *r = detail_record(browser,address,protocol);
    if (!out || out->response.data || out->response.size || out->players || out->player_count ||
        out->rules || out->rule_count || !r)
        return qa_browser_fail(error,"Discovery details require an empty output and actual retained entry");
    qa_server_browser_details candidate = {.entry = r->entry};
    detail_builder builder = {.out = &candidate, .storage = storage};
    qa_bytes bytes = protocol.kind == QA_NET_Q3_68 ?
        (qa_bytes){r->q3_response.data,r->q3_response.size} :
        (qa_bytes){r->status_response.data,r->status_response.size};
    if (!r->entry.available) { *out = candidate; return true; }
    if (!bytes.data || !bytes.size)
        return qa_browser_fail(error,"Available discovery entry lost its received detail datagram");
    bool ok = detail_text(storage,&candidate.response,bytes,error);
    if (ok && protocol.kind <= QA_NET_QW29) {
        size_t slot;
        qa_arena *scratch = qa_browser_decode_acquire(browser, &slot, error);
        qa_q1_discovery status = {0};
        ok = scratch && qa_q1_discovery_read(bytes,qa_q1_is_qw(protocol),scratch,&status,error);
        for (size_t i = 0; ok && i < status.player_count; ++i)
            ok = detail_player(&builder,status.player_details[i].score,status.player_details[i].ping,status.player_details[i].name,error);
        for (size_t i = 0; ok && i < status.rules.count; ++i) {
            const char *name=status.rules.rules[i].name,*value=status.rules.rules[i].value;
            ok = detail_rule(&builder,(qa_bytes){(const uint8_t *)name,strlen(name)},
                (qa_bytes){(const uint8_t *)value,strlen(value)},error);
        }
        qa_q1_discovery_free(&status);
        if (scratch) qa_browser_decode_release(browser, slot);
    } else if (ok && protocol.kind == QA_NET_Q2KEX_2023) {
        qa_kex_status_view status;
        ok = qa_kex_status_read(bytes,&status,error);
        for (size_t i = 0; ok && i < status.attribute_count; ++i)
            ok = detail_rule(&builder,status.attributes[i].key,status.attributes[i].value,error);
    } else if (ok && protocol.kind == QA_NET_Q3_68) {
        qa_browser_q3_result status_storage = {0}; qa_browser_q3_result *status = &status_storage; uint64_t challenge;
        ok = qa_browser_q3_decode(bytes,status,&challenge,error);
        for (uint32_t i=0;ok && i<status->player_count;++i)
            ok=detail_player(&builder,status->players[i].score,status->players[i].ping,
                status->names+status->players[i].name,error);
        if(ok) ok=detail_info(&builder,status->entry.rules,error);
    } else if (ok) {
        qa_q2_oob message_storage; qa_q2_oob *message = &message_storage; bool recognized=false;
        char rules[8193];
        ok=qa_q2_oob_read(bytes,protocol.kind==QA_NET_Q2REPRO_1038,message,&recognized,error) && recognized;
        if (ok) {
            size_t slot;
            qa_arena *scratch = qa_browser_decode_acquire(browser, &slot, error);
            ok = scratch && qa_q2_status_read(message,rules,sizeof(rules),scratch,
                detail_player,&builder,&recognized,error) && recognized &&
                detail_info(&builder,rules,error);
            if (scratch) qa_browser_decode_release(browser, slot);
        }
    }
    if(!ok) {
        if (!storage) qa_server_browser_details_free(&candidate);
        return false;
    }
    *out=candidate;
    return true;
}
bool qa_server_browser_details_read(const qa_server_browser *browser, const qa_net_address *address,
    qa_net_protocol_id protocol, qa_server_browser_details *out, qa_error *error) {
    return details_read(browser,address,protocol,NULL,out,error);
}
bool qa_server_browser_details_read_arena(const qa_server_browser *browser, const qa_net_address *address,
    qa_net_protocol_id protocol, qa_arena *storage, qa_server_browser_details *out, qa_error *error) {
    if (!storage) return qa_browser_fail(error,"Discovery detail rows require caller storage");
    return details_read(browser,address,protocol,storage,out,error);
}
bool qa_server_browser_details_current(const qa_server_browser *browser, const qa_server_browser_details *view) {
    const browser_record *r=view?detail_record(browser,&view->entry.address,view->entry.protocol):NULL;
    if(!r) return false;
    const qa_server_entry *a=&r->entry,*b=&view->entry;
    qa_bytes bytes=a->protocol.kind==QA_NET_Q3_68?(qa_bytes){r->q3_response.data,r->q3_response.size}:
        (qa_bytes){r->status_response.data,r->status_response.size};
    return a->sources==b->sources && a->players==b->players && a->maximum_players==b->maximum_players &&
        a->updated_ns==b->updated_ns && a->ping_ns==b->ping_ns && a->available==b->available &&
        a->pending==b->pending && a->timed_out==b->timed_out && a->has_ping==b->has_ping &&
        !strcmp(a->name,b->name) && !strcmp(a->map,b->map) && !strcmp(a->rules,b->rules) &&
        bytes.size==view->response.size && (!bytes.size ||
            (view->response.data && !memcmp(bytes.data,view->response.data,bytes.size)));
}
bool qa_browser_master_decode(qa_bytes bytes, qa_net_protocol_id protocol, qa_net_address *addresses,
                               size_t capacity, size_t *count, bool *complete, qa_error *error) {
    *complete = false; *count = 0;
    if (protocol.kind == QA_NET_Q3_68) {
        static const uint8_t prefix[] = {255,255,255,255,'g','e','t','s','e','r','v','e','r','s','R','e','s','p','o','n','s','e'};
        if (bytes.size < sizeof(prefix) || memcmp(bytes.data, prefix, sizeof(prefix))) return qa_browser_fail(error, "Not a Q3 master response");
        size_t offset = sizeof(prefix);
        while (offset < bytes.size && bytes.data[offset] != '\\') ++offset;
        while (offset < bytes.size && bytes.data[offset] == '\\') {
            ++offset;
            if (bytes.size - offset >= 3 && !memcmp(bytes.data + offset, "EOT", 3)) { *complete = true; break; }
            if (bytes.size - offset < 7 || bytes.data[offset + 6] != '\\' || *count >= capacity) break;
            qa_net_address address = {.kind = QA_NET_IPV4, .port = (uint16_t)((bytes.data[offset + 4] << 8) | bytes.data[offset + 5])};
            memcpy(address.host.ipv4, bytes.data + offset, 4);
            addresses[(*count)++] = address;
            offset += 6;
        }
        return true;
    }
    bool recognized = false;
    if (!qa_q2_master_read(bytes, addresses, capacity, count, &recognized, error) || !recognized)
        return qa_browser_fail(error, "Not a Q2 master response");
    *complete = true; return true;
}
