#include "kex_lan_internal.h"
#include "kex_channel_internal.h"
#include "kex_save_internal.h"

#include <stdlib.h>
#include <string.h>

static bool attributes_valid(const struct attributes *a)
{
    if (a->count > 256 || (a->count && !a->data)) return false;
    for (size_t i = 0; i < a->count; ++i) {
        const qa_kex_attribute *v = &a->data[i];
        if (!v->key[0] || !v->value[0] || !memchr(v->key, 0, sizeof(v->key)) ||
            !memchr(v->value, 0, sizeof(v->value)) || strchr(v->key, '\\') ||
            !qa_kex_text_valid((qa_bytes){(const uint8_t *)v->key, strlen(v->key)}) ||
            !qa_kex_text_valid((qa_bytes){(const uint8_t *)v->value, strlen(v->value)})) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp(v->key, a->data[j].key)) return false;
    }
    return true;
}

static bool roster_contains(const qa_kex_lan *l, uint64_t id)
{
    for (size_t i = 0; i < l->player_count; ++i) if (l->players[i].id == id) return true;
    return false;
}

static bool queued_valid(const struct queued *q)
{
    return q && q->size <= 65535 && q->kind >= QA_NET_POLL_PACKET && q->kind <= QA_NET_POLL_DROPPED &&
        (q->kind == QA_NET_POLL_PACKET || q->size == 0) &&
        qa_kex_save_address_valid(&q->address, q->kind == QA_NET_POLL_PACKET);
}

bool qa_kex_lan_valid(const qa_kex_lan *l)
{
    if (!l || l->entered || l->peer_count > 256 || l->player_count > 255 || l->queued_count > 256 ||
        l->options.name != l->name || !memchr(l->name, 0, sizeof(l->name)) ||
        !qa_kex_text_valid((qa_bytes){(const uint8_t *)l->name, strlen(l->name)}) ||
        !qa_kex_save_address_valid(&l->local_address, true) || l->options.local_players > 8 ||
        (l->options.host && l->options.max_players < l->options.local_players) ||
        (l->options.host && (!l->joined || l->player_count > l->options.max_players)) ||
        (!l->options.host && (!l->options.local_players || !qa_kex_save_address_valid(&l->options.server, true))) ||
        !attributes_valid(&l->attributes) || !qa_kex_save_address_valid(&l->dropped_from, false)) return false;
    if (l->transport && !qa_net_address_equal(&l->local_address, qa_net_transport_address(l->transport), true)) return false;
    for (size_t i = 0; i < l->player_count; ++i) {
        if (!l->players[i].id || !attributes_valid(&l->players[i].attributes)) return false;
        if(l->options.host&&l->next_id&&l->players[i].id>=l->next_id)return false;
        for (size_t j = 0; j < i; ++j) if (l->players[i].id == l->players[j].id) return false;
    }
    size_t hosted_players=l->options.local_players;
    for (size_t i = 0; i < l->peer_count; ++i) {
        const struct peer *p = l->peers[i];
        if (!p || p->owner != l || !qa_kex_save_address_valid(&p->address, true) || p->count > 8 ||
            !qa_kex_channel_valid(p->channel) || p->channel->emit != qa_kex_lan_emit || p->channel->user != p) return false;
        for (size_t j = 0; j < i; ++j) if (qa_net_address_equal(&p->address, &l->peers[j]->address, true)) return false;
        if (!l->options.host && (p->count || !qa_net_address_equal(&p->address, &l->options.server, true))) return false;
        hosted_players+=p->count;
        for (size_t j = 0; j < p->count; ++j) {
            if (!p->players[j] || !roster_contains(l, p->players[j])) return false;
            for (size_t k = 0; k < j; ++k) if (p->players[k] == p->players[j]) return false;
            for (size_t k = 0; k < i; ++k)
                for (size_t n = 0; n < l->peers[k]->count; ++n)
                    if (p->players[j] == l->peers[k]->players[n]) return false;
            for (unsigned k = 0; k < l->options.local_players; ++k)
                if (p->players[j] == l->local_ids[k]) return false;
        }
    }
    if(l->options.host&&hosted_players!=l->player_count)return false;
    if (l->joined) {
        for (unsigned i = 0; i < l->options.local_players; ++i) {
            if (!l->local_ids[i] || !roster_contains(l, l->local_ids[i])) return false;
            for (unsigned j = 0; j < i; ++j) if (l->local_ids[i] == l->local_ids[j]) return false;
        }
    }
    size_t count = 0;
    const struct queued *last = NULL;
    for (const struct queued *q = l->head; q; q = q->next) {
        if (++count > l->queued_count || !queued_valid(q)) return false;
        last = q;
    }
    return count == l->queued_count && last == l->tail &&
        (!l->borrowed || queued_valid(l->borrowed));
}

void qa_kex_lan_destroy_detached(qa_kex_lan *l)
{
    if (!l || l->entered || l->transport) return;
    for (size_t i = 0; i < l->peer_count; ++i) {
        qa_kex_channel_destroy(l->peers[i]->channel);
        free(l->peers[i]);
    }
    for (size_t i = 0; i < l->player_count; ++i) free(l->players[i].attributes.data);
    free(l->attributes.data);
    while (l->head) { struct queued *q = l->head; l->head = q->next; free(q); }
    free(l->borrowed);
    free(l);
}

bool qa_kex_lan_bind(qa_kex_lan *l, qa_net_transport *transport, qa_error *e)
{
    const qa_net_address *bound = transport ? qa_net_transport_address(transport) : NULL;
    if (!qa_kex_lan_valid(l) || l->transport || !bound || !qa_net_address_equal(bound, &l->local_address, true)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX LAN binding requires its genuine saved native endpoint"); return false;
    }
    l->transport = transport;
    return true;
}

static bool attributes_write(qa_net_writer *w, const struct attributes *a)
{
    bool ok = qa_net_write_u16(w, (uint16_t)a->count);
    for (size_t i = 0; ok && i < a->count; ++i)
        ok = qa_net_write_string(w, a->data[i].key) && qa_net_write_string(w, a->data[i].value);
    return ok;
}

static bool attributes_read(qa_net_reader *r, struct attributes *a, qa_error *e)
{
    a->count = qa_net_read_u16(r);
    if (r->failed || a->count > 256 || a->count > qa_net_reader_remaining(r) / 4)
        return qa_net_reader_fail(r, "Invalid KEX retained attribute extent");
    if (a->count) {
        a->data = calloc(a->count, sizeof(*a->data));
        if (!a->data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX lobby attributes"); return false; }
    }
    for (size_t i = 0; i < a->count; ++i)
        if (!qa_net_read_string(r, a->data[i].key, sizeof(a->data[i].key)) ||
            !qa_net_read_string(r, a->data[i].value, sizeof(a->data[i].value))) return false;
    return attributes_valid(a) || qa_net_reader_fail(r, "Invalid KEX retained attributes");
}

static bool queued_write(qa_net_writer *w, const struct queued *q)
{
    return qa_kex_save_address_write(w, &q->address) && qa_net_write_u64(w, q->received) &&
        qa_net_write_u8(w, (uint8_t)q->kind) && qa_net_write_u32(w, (uint32_t)q->size) &&
        qa_net_write_data(w, q->bytes, q->size);
}

static bool queued_read(qa_net_reader *r, struct queued **out, qa_error *e)
{
    qa_net_address address = {0};
    bool ok = qa_kex_save_address_read(r, &address);
    uint64_t received = qa_net_read_u64(r);
    qa_net_poll_kind kind = (qa_net_poll_kind)qa_net_read_u8(r);
    uint32_t size = qa_net_read_u32(r);
    if (!ok || r->failed || size > 65535 || size > qa_net_reader_remaining(r))
        return qa_net_reader_fail(r, "Invalid KEX queued game extent");
    struct queued *q = malloc(sizeof(*q) + size);
    if (!q) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX queued game record"); return false; }
    *q = (struct queued){.address = address, .received = received, .kind = kind, .size = size};
    if (!qa_net_read_data(r, q->bytes, size) || !queued_valid(q)) {
        free(q); return qa_net_reader_fail(r, "Invalid KEX queued game record");
    }
    *out = q;
    return true;
}

bool qa_kex_lan_checkpoint(const qa_kex_lan *l, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_kex_lan_valid(l)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX LAN continuation requires an idle valid owner"); return false;
    }
    qa_buffer channels[256] = {{0}};
    size_t capacity = 2048 + 2 + l->attributes.count * 5120;
    bool ok = true;
    for (size_t i = 0; i < l->player_count; ++i) capacity += 10 + l->players[i].attributes.count * 5120;
    for (size_t i = 0; ok && i < l->peer_count; ++i) {
        ok = qa_kex_channel_checkpoint(l->peers[i]->channel, &channels[i], e);
        size_t addition = 256 + channels[i].size;
        if (ok && capacity > SIZE_MAX - addition) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "KEX LAN continuation extent overflow"); ok = false;
        }
        if (ok) capacity += addition;
    }
    for (const struct queued *q = l->head; ok && q; q = q->next) {
        size_t addition = 256 + q->size;
        if (capacity > SIZE_MAX - addition) { qa_error_set(e, QA_ERROR_MEMORY, 0, "KEX game continuation extent overflow"); ok = false; }
        else capacity += addition;
    }
    if (ok && l->borrowed) {
        size_t addition = 256 + l->borrowed->size;
        if (capacity > SIZE_MAX - addition) { qa_error_set(e, QA_ERROR_MEMORY, 0, "KEX borrowed game extent overflow"); ok = false; }
        else capacity += addition;
    }
    uint8_t *bytes = ok ? malloc(capacity) : NULL;
    if (ok && !bytes) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding KEX LAN continuation"); ok = false; }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, ok ? capacity : 0, e);
    ok = ok && qa_net_write_data(&w, "QAKL", 4) && qa_net_write_u32(&w, 1) &&
        qa_kex_save_address_write(&w, &l->local_address) && qa_net_write_u8(&w, l->options.host) &&
        qa_net_write_u8(&w, l->options.max_players) && qa_net_write_u8(&w, l->options.local_players) &&
        qa_net_write_string(&w, l->name) && qa_kex_save_address_write(&w, &l->options.server) &&
        qa_net_write_u64(&w, l->next_id) && qa_net_write_u64(&w, l->clock) && qa_net_write_u64(&w, l->retry_at) &&
        qa_net_write_u8(&w, l->joined) && qa_net_write_u8(&w, l->retried) && qa_net_write_u8(&w, l->local_first);
    for (unsigned i = 0; ok && i < 8; ++i) ok = qa_net_write_u64(&w, l->local_ids[i]);
    ok = ok && qa_net_write_u16(&w, (uint16_t)l->player_count) && attributes_write(&w, &l->attributes);
    for (size_t i = 0; ok && i < l->player_count; ++i)
        ok = qa_net_write_u64(&w, l->players[i].id) && attributes_write(&w, &l->players[i].attributes);
    ok = ok && qa_net_write_u16(&w, (uint16_t)l->peer_count);
    for (size_t i = 0; ok && i < l->peer_count; ++i) {
        const struct peer *p = l->peers[i];
        ok = qa_kex_save_address_write(&w, &p->address) && qa_net_write_u8(&w, (uint8_t)p->count);
        for (unsigned j = 0; ok && j < 8; ++j) ok = qa_net_write_u64(&w, p->players[j]);
        ok = ok && qa_net_write_u32(&w, (uint32_t)channels[i].size) && qa_net_write_data(&w, channels[i].data, channels[i].size);
    }
    ok = ok && qa_net_write_u16(&w, (uint16_t)l->queued_count);
    for (const struct queued *q = l->head; ok && q; q = q->next) ok = queued_write(&w, q);
    ok = ok && qa_net_write_u8(&w, l->borrowed != NULL) && (!l->borrowed || queued_write(&w, l->borrowed)) &&
        qa_net_write_u8(&w, l->dropped) && qa_kex_save_address_write(&w, &l->dropped_from);
    for (size_t i = 0; i < l->peer_count; ++i) qa_buffer_free(&channels[i]);
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool qa_kex_lan_restore(qa_bytes bytes, qa_kex_lan **out, qa_error *e)
{
    if (!out || !bytes.data || bytes.size < 32 || memcmp(bytes.data, "QAKL", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX LAN continuation envelope"); return false;
    }
    qa_kex_lan *l = calloc(1, sizeof(*l));
    if (!l) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX LAN continuation"); return false; }
    l->options.name = l->name;
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = qa_net_read_u32(&r) == 1 && qa_kex_save_address_read(&r, &l->local_address);
    uint8_t host = qa_net_read_u8(&r);
    l->options.host = host != 0;
    l->options.max_players = qa_net_read_u8(&r);
    l->options.local_players = qa_net_read_u8(&r);
    ok = ok && host <= 1 && qa_net_read_string(&r, l->name, sizeof(l->name)) && qa_kex_save_address_read(&r, &l->options.server);
    l->next_id = qa_net_read_u64(&r);
    l->clock = qa_net_read_u64(&r);
    l->retry_at = qa_net_read_u64(&r);
    uint8_t joined = qa_net_read_u8(&r), retried = qa_net_read_u8(&r);
    l->joined = joined != 0;
    l->retried = retried != 0;
    l->local_first = qa_net_read_u8(&r);
    for (unsigned i = 0; i < 8; ++i) l->local_ids[i] = qa_net_read_u64(&r);
    uint16_t player_count = qa_net_read_u16(&r);
    ok = ok && !r.failed && joined <= 1 && retried <= 1 && player_count <= 255 && attributes_read(&r, &l->attributes, e);
    for (size_t i = 0; ok && i < player_count; ++i) {
        ++l->player_count;
        l->players[i].id = qa_net_read_u64(&r);
        ok = !r.failed && attributes_read(&r, &l->players[i].attributes, e);
    }
    uint16_t peer_count = qa_net_read_u16(&r);
    ok = ok && !r.failed && peer_count <= 256 && peer_count <= qa_net_reader_remaining(&r) / 70;
    for (unsigned i = 0; ok && i < peer_count; ++i) {
        struct peer *p = calloc(1, sizeof(*p));
        if (!p) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX lobby peer"); ok = false; break; }
        p->owner = l;
        l->peers[l->peer_count++] = p;
        ok = qa_kex_save_address_read(&r, &p->address);
        p->count = qa_net_read_u8(&r);
        for (unsigned j = 0; j < 8; ++j) p->players[j] = qa_net_read_u64(&r);
        uint32_t extent = qa_net_read_u32(&r);
        qa_bytes channel;
        ok = ok && !r.failed && qa_net_read_bytes(&r, extent, &channel) &&
            qa_kex_channel_restore(channel, qa_kex_lan_emit, p, &p->channel, e);
    }
    uint16_t queued_count = qa_net_read_u16(&r);
    ok = ok && !r.failed && queued_count <= 256 && queued_count <= qa_net_reader_remaining(&r) / 18;
    for (unsigned i = 0; ok && i < queued_count; ++i) {
        struct queued *q = NULL;
        ok = queued_read(&r, &q, e);
        if (ok) {
            if (l->tail) l->tail->next = q;
            else l->head = q;
            l->tail = q;
            ++l->queued_count;
        }
    }
    uint8_t borrowed = qa_net_read_u8(&r);
    ok = ok && borrowed <= 1 && (!borrowed || queued_read(&r, &l->borrowed, e));
    uint8_t dropped = qa_net_read_u8(&r);
    l->dropped = dropped != 0;
    ok = ok && dropped <= 1 && qa_kex_save_address_read(&r, &l->dropped_from);
    if (!ok || !qa_net_reader_finish(&r) || !qa_kex_lan_valid(l)) {
        qa_kex_lan_destroy_detached(l);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX LAN retained state");
        return false;
    }
    *out = l;
    return true;
}
