#include "internal.h"
#include "qa/server_browser_kex.h"
#include "qa/network_q3.h"
#include "qa/text.h"

#include <string.h>

static void display_text(qa_bytes text, char *out, size_t capacity)
{
    size_t cursor = 0, used = 0;
    uint32_t scalar;
    while (qa_utf8_next(text, &cursor, &scalar)) {
        char encoded[4];
        size_t count = qa_utf8_encode(scalar ? scalar : 0xfffd, encoded);
        if (count >= capacity - used) break;
        memcpy(out + used, encoded, count);
        used += count;
    }
    out[used] = 0;
}

bool qa_browser_kex_decode(qa_bytes bytes, qa_server_entry *out, qa_error *e)
{
    if (!out) return qa_browser_fail(e, "Missing retail status projection output");
    qa_kex_status_view view;
    if (!qa_kex_status_read(bytes, &view, e)) return false;
    qa_server_entry entry = {.protocol = {.kind = QA_NET_Q2KEX_2023},
        .players = view.players, .maximum_players = view.max_players};
    display_text(view.name, entry.name, sizeof(entry.name));
    display_text(qa_kex_status_value(&view, "map"), entry.map, sizeof(entry.map));
    for (size_t i = 0; i < view.attribute_count; ++i) {
        const qa_kex_status_attribute *a = &view.attributes[i];
        if (!a->key.size || a->key.size >= 1024 || a->value.size >= 4096 ||
            memchr(a->key.data, 0, a->key.size) || (a->value.size && memchr(a->value.data, 0, a->value.size))) continue;
        char key[1024], value[4096];
        memcpy(key, a->key.data, a->key.size); key[a->key.size] = 0;
        if (a->value.size) memcpy(value, a->value.data, a->value.size);
        value[a->value.size] = 0;
        qa_error projection = {0};
        (void)qa_q3_info_set(entry.rules, sizeof(entry.rules) - 1, key, value, &projection);
    }
    *out = entry;
    return true;
}

bool qa_server_browser_kex_status(const qa_server_browser *b, const qa_net_address *address,
    qa_kex_status_view *out, qa_error *e)
{
    if (!b || !address || !out) return qa_browser_fail(e, "Missing retained retail status owner");
    for (uint32_t i = 0; i < b->capacity; ++i) {
        const browser_record *r = &b->records[i];
        if (!r->occupied || r->entry.protocol.kind != QA_NET_Q2KEX_2023 ||
            r->entry.protocol.flags || r->entry.protocol.revision ||
            !qa_net_address_equal(address, &r->entry.address, true)) continue;
        return qa_kex_status_read((qa_bytes){r->status_response.data, r->status_response.size}, out, e);
    }
    return qa_browser_fail(e, "Retail discovery address has no shared retained status");
}

bool qa_server_browser_receive_kex_discovery(qa_server_browser *b,
    const qa_net_datagram *packet, uint64_t sent_ns, bool *recognized, qa_error *e)
{
    if (!b || b->callback || !packet || !recognized)
        return qa_browser_fail(e, "Missing idle retail discovery owner");
    *recognized = false;
    if (packet->kind != QA_NET_POLL_PACKET || packet->received_ns < sent_ns) return true;
    const qa_net_protocol_id protocol = {.kind = QA_NET_Q2KEX_2023};
    qa_server_entry decoded;
    qa_error malformed = {0};
    if (!qa_browser_kex_decode(packet->payload, &decoded, &malformed)) {
        if (malformed.code == QA_ERROR_MEMORY) { if (e) *e = malformed; return false; }
        return true;
    }
    if (!qa_server_browser_add(b, &packet->from, protocol, QA_SERVER_LAN, e)) return false;
    browser_record *record = qa_browser_find(b, &packet->from, protocol);
    if (!record) return qa_browser_fail(e, "Retail discovery lost its retained shared entry");
    if (!qa_browser_status_store_response(record, packet->payload, e)) return false;
    decoded.address = record->entry.address;
    decoded.sources = record->entry.sources;
    decoded.available = true;
    decoded.updated_ns = packet->received_ns;
    decoded.ping_ns = packet->received_ns - sent_ns;
    decoded.has_ping = true;
    record->entry = decoded;
    qa_browser_changed(b, &record->entry);
    *recognized = true;
    return true;
}
