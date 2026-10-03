#include "qa/network_q2_unicast.h"
#include <stdlib.h>
#include <string.h>

struct qa_q2_unicast_cache { qa_q2_unicast_claim *rows; size_t count, capacity; };
static bool fail(qa_error *e, qa_status code, const char *text)
{ qa_error_set(e, code, 0, "%s", text); return false; }
static bool valid(const qa_q2_unicast_claim *r)
{ return r && r->client.owner && r->client.generation && r->connection_epoch && r->source && r->map_revision && r->key; }
static bool domain(const qa_q2_unicast_claim *a, const qa_q2_unicast_claim *b)
{
    return a->source == b->source && a->map_revision == b->map_revision && qa_sha256_equal(&a->map, &b->map) &&
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
    *out = calloc(1, sizeof(**out));
    return *out != NULL || fail(e, QA_ERROR_MEMORY, "Retaining Q2 client-group unicast keys");
}
void qa_q2_unicast_cache_destroy(qa_q2_unicast_cache *cache)
{ if (cache) { free(cache->rows); free(cache); } }
bool qa_q2_unicast_check(const qa_q2_unicast_cache *cache, const qa_q2_unicast_claim *claim,
    bool *duplicate, qa_error *e)
{
    if (!cache || !duplicate || !valid(claim))
        return fail(e, QA_ERROR_ARGUMENT, "Q2 duplicate check has no actual client/Source claim");
    *duplicate = false;
    for (size_t i = 0; i < cache->count; ++i)
        if (same(cache->rows + i, claim)) { *duplicate = true; break; }
    return true;
}
bool qa_q2_unicast_reserve(qa_q2_unicast_cache *cache, qa_error *e)
{
    if (!cache) return fail(e, QA_ERROR_ARGUMENT, "Missing Q2 unicast reservation owner");
    if (cache->count < cache->capacity) return true;
    size_t capacity = cache->capacity ? cache->capacity * 2 : 32;
    if (capacity <= cache->capacity || capacity > SIZE_MAX / sizeof(*cache->rows))
        return fail(e, QA_ERROR_MEMORY, "Q2 unicast cache extent overflows");
    void *rows = realloc(cache->rows, capacity * sizeof(*cache->rows));
    if (!rows) return fail(e, QA_ERROR_MEMORY, "Retaining Q2 per-frame unicast key");
    cache->rows = rows; cache->capacity = capacity; return true;
}
bool qa_q2_unicast_remember(qa_q2_unicast_cache *cache, const qa_q2_unicast_claim *claim, qa_error *e)
{
    bool duplicate = false;
    if (!qa_q2_unicast_check(cache, claim, &duplicate, e)) return false;
    if (duplicate) return true;
    size_t kept = 0;
    for (size_t i = 0; i < cache->count; ++i) {
        const qa_q2_unicast_claim *row = cache->rows + i;
        if (row->source == claim->source && !domain(row, claim)) continue;
        if (qa_net_client_id_equal(row->client, claim->client) && row->connection_epoch != claim->connection_epoch) continue;
        cache->rows[kept++] = *row;
    }
    cache->count = kept;
    if (!qa_q2_unicast_reserve(cache, e)) return false;
    cache->rows[cache->count++] = *claim; return true;
}
void qa_q2_unicast_remove_client(qa_q2_unicast_cache *cache, qa_net_client_id client)
{
    if (!cache) return;
    size_t kept = 0;
    for (size_t i = 0; i < cache->count; ++i)
        if (!qa_net_client_id_equal(cache->rows[i].client, client)) cache->rows[kept++] = cache->rows[i];
    cache->count = kept;
}
void qa_q2_unicast_remove_source(qa_q2_unicast_cache *cache, qa_actor_owner source)
{
    if (!cache) return;
    size_t kept = 0;
    for (size_t i = 0; i < cache->count; ++i)
        if (cache->rows[i].source != source) cache->rows[kept++] = cache->rows[i];
    cache->count = kept;
}
static bool row_codec(qa_source_save_io *io, const qa_q2_unicast_refs *refs, qa_q2_unicast_claim *row)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t client = 0, source = 0;
    if (!reading && (!refs->client_encode(refs->context, row->client, &client, io->error) ||
        !refs->source_encode(refs->context, row->source, &source, io->error))) return false;
    if (!qa_source_save_u64(io, &client) || !qa_source_save_u64(io, &source) || !client || !source ||
        !qa_source_save_u64(io, &row->connection_epoch) ||
        !qa_source_save_bytes(io, row->map.bytes, sizeof(row->map.bytes)) ||
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
    for (size_t i = 0; ok && i < cache->count; ++i) {
        bool present = false;
        ok = refs->current(refs->context, cache->rows + i, &present, e);
        if (ok && present) rows[count++] = cache->rows[i];
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
        count <= (io.input.size - io.offset) / 84;
    qa_q2_unicast_cache *cache = NULL;
    if (ok) ok = qa_q2_unicast_cache_create(&cache, e);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_q2_unicast_claim row = {0}; bool present = false, duplicate = false;
        ok = row_codec(&io, refs, &row) && refs->current(refs->context, &row, &present, e) &&
            present && qa_q2_unicast_check(cache, &row, &duplicate, e) && !duplicate;
        for (size_t j = 0; ok && j < cache->count; ++j)
            if (cache->rows[j].source == row.source && !domain(cache->rows + j, &row)) ok = false;
        if (ok) ok = qa_q2_unicast_remember(cache, &row, e);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (!ok && (!e || e->code == QA_OK)) fail(e, QA_ERROR_FORMAT, "Saved Q2 unicast cache has no current canonical Source group");
    if (ok) *out = cache; else qa_q2_unicast_cache_destroy(cache);
    qa_source_save_dispose(&io); return ok;
}
