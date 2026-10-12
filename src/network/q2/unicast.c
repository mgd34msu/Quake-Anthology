#include "qa/network_q2_unicast.h"
#include "qa/id_index.h"
#include "qa/pool.h"
#include <stdlib.h>

typedef struct unicast_row {
    struct unicast_row *next;
    qa_q2_unicast_claim claim;
    size_t storage_slot;
} unicast_row;
struct qa_q2_unicast_cache {
    qa_arena storage;
    qa_pool rows;
    qa_id_index keys;
    unicast_row *first, *last;
    size_t count;
};
static bool fail(qa_error *e, qa_status code, const char *text)
{ qa_error_set(e, code, 0, "%s", text); return false; }
static bool valid(const qa_q2_unicast_claim *r)
{ return r && r->client.owner && r->client.generation && r->connection_epoch && r->source && r->map_revision && r->key; }
static bool domain(const qa_q2_unicast_claim *a, const qa_q2_unicast_claim *b)
{
    return a->source == b->source && a->map_revision == b->map_revision &&
        a->source_frame == b->source_frame && a->source_time_ns == b->source_time_ns;
}
static bool same(const qa_q2_unicast_claim *a, const qa_q2_unicast_claim *b)
{
    return domain(a, b) && qa_net_client_id_equal(a->client, b->client) &&
        a->connection_epoch == b->connection_epoch && a->key == b->key;
}
bool qa_q2_unicast_cache_create(qa_q2_unicast_cache **out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing Q2 unicast cache destination");
    qa_q2_unicast_cache *cache = calloc(1, sizeof(*cache));
    if (!cache) { *out = NULL; return fail(e, QA_ERROR_MEMORY, "Retaining Q2 client-group unicast keys"); }
    cache->keys.storage = &cache->storage;
    if (!qa_pool_prepare(&cache->rows, &cache->storage, 32, sizeof(unicast_row),
            _Alignof(unicast_row), e) || !qa_id_index_reserve(&cache->keys, 32, e)) {
        qa_q2_unicast_cache_destroy(cache); *out = NULL; return false;
    }
    *out = cache; return true;
}
void qa_q2_unicast_cache_destroy(qa_q2_unicast_cache *cache)
{ if (cache) { qa_id_index_destroy(&cache->keys); qa_arena_destroy(&cache->storage); free(cache); } }
bool qa_q2_unicast_check(const qa_q2_unicast_cache *cache, const qa_q2_unicast_claim *claim,
    bool *duplicate, qa_error *e)
{
    if (!cache || !duplicate || !valid(claim))
        return fail(e, QA_ERROR_ARGUMENT, "Q2 duplicate check has no actual client/Source claim");
    *duplicate = false;
    qa_id_index_cursor cursor = qa_id_index_begin(&cache->keys, claim->source, claim->key);
    size_t slot;
    while (qa_id_index_next(&cache->keys, &cursor, &slot)) {
        const unicast_row *row = qa_pool_at(&cache->rows, slot);
        if (same(&row->claim, claim)) { *duplicate = true; break; }
    }
    return true;
}
static bool reserve(qa_q2_unicast_cache *cache, size_t additional, qa_error *e)
{
    if (!qa_id_index_reserve(&cache->keys, additional, e)) return false;
    size_t required = cache->count + additional;
    if (required <= cache->rows.capacity) return true;
    size_t slots = required - cache->rows.capacity;
    if (slots < cache->rows.capacity) slots = cache->rows.capacity;
    return qa_pool_extend(&cache->rows, &cache->storage, slots, e);
}
bool qa_q2_unicast_reserve(qa_q2_unicast_cache *cache, qa_error *e)
{
    if (!cache) return fail(e, QA_ERROR_ARGUMENT, "Missing Q2 unicast reservation owner");
    return reserve(cache, 1, e);
}
static void prune(qa_q2_unicast_cache *cache, const qa_q2_unicast_claim *claim,
    const qa_net_client_id *client, qa_actor_owner source)
{
    unicast_row **link = &cache->first, *previous = NULL;
    bool changed = false;
    while (*link) {
        unicast_row *row = *link;
        bool remove = claim ?
            (row->claim.source == claim->source && !domain(&row->claim, claim)) ||
            (qa_net_client_id_equal(row->claim.client, claim->client) &&
                row->claim.connection_epoch != claim->connection_epoch) :
            client ? qa_net_client_id_equal(row->claim.client, *client) : row->claim.source == source;
        if (remove) {
            *link = row->next;
            if (cache->last == row) cache->last = previous;
            qa_pool_release(&cache->rows, row->storage_slot);
            --cache->count; changed = true;
        } else { previous = row; link = &row->next; }
    }
    if (changed) {
        qa_id_index_clear(&cache->keys);
        for (unicast_row *row = cache->first; row; row = row->next)
            qa_id_index_insert(&cache->keys, row->claim.source, row->claim.key, row->storage_slot);
    }
}
bool qa_q2_unicast_remember(qa_q2_unicast_cache *cache, const qa_q2_unicast_claim *claim, qa_error *e)
{
    bool duplicate = false;
    if (!qa_q2_unicast_check(cache, claim, &duplicate, e)) return false;
    if (duplicate) return true;
    if (!qa_q2_unicast_reserve(cache, e)) return false;
    prune(cache, claim, NULL, 0);
    size_t slot;
    unicast_row *row = qa_pool_take(&cache->rows, &slot);
    *row = (unicast_row){.claim = *claim, .storage_slot = slot};
    if (cache->last) cache->last->next = row; else cache->first = row;
    cache->last = row; ++cache->count;
    qa_id_index_insert(&cache->keys, claim->source, claim->key, slot);
    return true;
}
void qa_q2_unicast_remove_client(qa_q2_unicast_cache *cache, qa_net_client_id client)
{
    if (!cache) return;
    prune(cache, NULL, &client, 0);
}
void qa_q2_unicast_remove_source(qa_q2_unicast_cache *cache, qa_actor_owner source)
{
    if (!cache) return;
    prune(cache, NULL, NULL, source);
}
static bool row_codec(qa_source_save_io *io, const qa_q2_unicast_refs *refs, qa_q2_unicast_claim *row)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t client = 0, source = 0;
    if (!reading && (!refs->client_encode(refs->context, row->client, &client, io->error) ||
        !refs->source_encode(refs->context, row->source, &source, io->error))) return false;
    if (!qa_source_save_u64(io, &client) || !qa_source_save_u64(io, &source) || !client || !source ||
        !qa_source_save_u64(io, &row->connection_epoch) ||
        !qa_source_save_u64(io, &row->map_revision) ||
        !qa_source_save_u64(io, &row->source_frame) || !qa_source_save_u64(io, &row->source_time_ns) ||
        !qa_source_save_u32(io, &row->key)) return false;
    if (reading && (!refs->client_decode(refs->context, client, &row->client, io->error) ||
        !refs->source_decode(refs->context, source, &row->source, &row->map_revision, io->error))) return false;
    return valid(row) || fail(io->error, QA_ERROR_FORMAT, "Saved Q2 unicast claim is incomplete");
}
bool qa_q2_unicast_capture(const qa_q2_unicast_cache *cache, const qa_q2_unicast_refs *refs,
    qa_buffer *out, qa_error *e)
{
    if (!cache || !refs || !refs->current || !refs->client_encode || !refs->source_encode || !out)
        return fail(e, QA_ERROR_ARGUMENT, "Q2 unicast capture requires its actual HOST reference graph");
    qa_q2_unicast_claim *rows = cache->count ? malloc(cache->count * sizeof(*rows)) : NULL;
    if (cache->count && !rows) return fail(e, QA_ERROR_MEMORY, "Retaining Q2 unicast capture view");
    size_t count = 0; bool ok = true;
    for (const unicast_row *row = cache->first; ok && row; row = row->next) {
        bool present = false;
        ok = refs->current(refs->context, &row->claim, &present, e);
        if (ok && present) rows[count++] = row->claim;
    }
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, NULL, e);
    uint32_t magic = UINT32_C(0x5532514e);
    if (ok) ok = qa_source_save_u32(&io, &magic) &&
        qa_source_save_count(&io, &count, SIZE_MAX / sizeof(*rows));
    for (size_t i = 0; ok && i < count; ++i) ok = row_codec(&io, refs, rows + i);
    free(rows);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q2_unicast_restore(qa_bytes bytes, const qa_q2_unicast_refs *refs,
    qa_q2_unicast_cache **out, qa_error *e)
{
    if (!out || *out || !refs || !refs->current || !refs->client_decode || !refs->source_decode)
        return fail(e, QA_ERROR_ARGUMENT, "Q2 unicast restore requires its empty candidate HOST graph");
    qa_source_save_io io = {0};
    uint32_t magic = 0; size_t count = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, e) &&
        qa_source_save_u32(&io, &magic) &&
        magic == UINT32_C(0x5532514e) &&
        qa_source_save_count(&io, &count, SIZE_MAX / sizeof(qa_q2_unicast_claim)) &&
        count <= (io.input.size - io.offset) / 52;
    qa_q2_unicast_cache *cache = NULL;
    if (ok) ok = qa_q2_unicast_cache_create(&cache, e);
    if (ok) ok = reserve(cache, count, e);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_q2_unicast_claim row = {0}; bool present = false, duplicate = false;
        ok = row_codec(&io, refs, &row) && refs->current(refs->context, &row, &present, e) &&
            present && qa_q2_unicast_check(cache, &row, &duplicate, e) && !duplicate;
        for (const unicast_row *previous = cache->first; ok && previous; previous = previous->next)
            if (previous->claim.source == row.source && !domain(&previous->claim, &row)) ok = false;
        if (ok) ok = qa_q2_unicast_remember(cache, &row, e);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (!ok && (!e || e->code == QA_OK)) fail(e, QA_ERROR_FORMAT, "Saved Q2 unicast cache has no current canonical Source group");
    if (ok) *out = cache; else qa_q2_unicast_cache_destroy(cache);
    qa_source_save_dispose(&io); return ok;
}
