#include "haptics_private.h"
#include "qa/input_haptic_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct pattern_row { qa_haptic_pattern *pattern; size_t holders; };
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool pattern_valid(const qa_haptic_pattern *p)
{
    return p && p->rate && p->count <= UINT32_MAX / 4 &&
        (p->loop ? p->start < p->end && p->end <= p->count :
                   !p->start && !p->end && !p->interval);
}
static bool players_valid(qa_haptic_player *const *players, size_t count, qa_error *error)
{
    if (count && !players) return fail(error, QA_ERROR_ARGUMENT, "missing tactile player holders");
    for (size_t i = 0; i < count; ++i) {
        if (!players[i]) return fail(error, QA_ERROR_ARGUMENT, "missing tactile player owner");
        for (size_t j = 0; j < i; ++j) if (players[i] == players[j])
            return fail(error, QA_ERROR_ARGUMENT, "duplicate tactile player owner");
    }
    return true;
}
static bool player_valid(const qa_haptic_player *p)
{
    return isfinite(p->start_ms) && p->start_ms >= 0 &&
        ((isfinite(p->last_ms) && p->last_ms >= 0) || p->last_ms == -INFINITY) &&
        isfinite(p->strength) && p->strength >= 0 && p->strength <= 1 &&
        (p->pattern ? (p->last_index >= -2 &&
            (p->last_index != -2 || (p->pattern->loop && p->pattern->interval)) &&
            (p->last_index < 0 || (uint64_t)p->last_index < p->pattern->count)) :
            p->last_index == -1);
}
static size_t find_pattern(const struct pattern_row *rows, size_t count, const qa_haptic_pattern *p)
{
    for (size_t i = 0; i < count; ++i) if (rows[i].pattern == p) return i;
    return SIZE_MAX;
}
static bool add_pattern(struct pattern_row **rows, size_t *count, qa_haptic_pattern *p, qa_error *error)
{
    if (!p) return true;
    size_t index = find_pattern(*rows, *count, p);
    if (index == SIZE_MAX) {
        if (*count == SIZE_MAX / sizeof(**rows)) return fail(error, QA_ERROR_MEMORY, "tactile pattern inventory overflow");
        struct pattern_row *grown = realloc(*rows, (*count + 1) * sizeof(**rows));
        if (!grown) return fail(error, QA_ERROR_MEMORY, "allocating tactile pattern inventory");
        *rows = grown; index = (*count)++;
        grown[index] = (struct pattern_row){.pattern = p};
    }
    if ((*rows)[index].holders == SIZE_MAX) return fail(error, QA_ERROR_MEMORY, "tactile pattern holder overflow");
    ++(*rows)[index].holders;
    return true;
}
static bool pattern_fields(qa_source_save_io *io, qa_haptic_pattern **pointer)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_haptic_pattern saved = {0};
    if (!reading) saved = **pointer;
    if (!qa_source_save_count(io, &saved.count, UINT32_MAX / 4) ||
        !qa_source_save_u16(io, &saved.rate) || !qa_source_save_bool(io, &saved.loop) ||
        !qa_source_save_u32(io, &saved.start) || !qa_source_save_u32(io, &saved.end) ||
        !qa_source_save_u32(io, &saved.interval) || !pattern_valid(&saved)) return false;
    if (reading) {
        if (io->offset > io->input.size || saved.count > (io->input.size - io->offset) / 4)
            return fail(io->error, QA_ERROR_FORMAT, "truncated tactile pattern samples");
        if (saved.count > (SIZE_MAX - sizeof(saved)) / 4)
            return fail(io->error, QA_ERROR_MEMORY, "tactile pattern allocation overflow");
        qa_haptic_pattern *p = malloc(sizeof(*p) + saved.count * 4);
        if (!p) return fail(io->error, QA_ERROR_MEMORY, "restoring tactile pattern");
        *p = saved; p->references = 1; *pointer = p;
    }
    return qa_source_save_bytes(io, (*pointer)->samples, saved.count * 4);
}
static bool player_fields(qa_source_save_io *io, qa_haptic_player *p, struct pattern_row *rows, size_t count)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t index = p->pattern ? find_pattern(rows, count, p->pattern) : UINT64_MAX;
    if (!qa_source_save_u64(io, &index) || (index != UINT64_MAX && index >= count)) return false;
    if (reading && index != UINT64_MAX) {
        p->pattern = rows[index].pattern;
        qa_haptic_pattern_retain(p->pattern);
        ++rows[index].holders;
    }
    return qa_source_save_f64(io, &p->start_ms) && qa_source_save_f64(io, &p->last_ms) &&
        qa_source_save_i64(io, &p->last_index) && qa_source_save_f32(io, &p->strength) &&
        qa_source_save_bool(io, &p->enabled) && qa_source_save_bool(io, &p->active) && player_valid(p);
}
static bool header(qa_source_save_io *io, size_t *patterns, size_t *entries, size_t *players)
{
    uint8_t magic[4] = {'Q','H','A','P'};
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QHAP", sizeof(magic)) &&
        qa_source_save_count(io, patterns, SIZE_MAX / sizeof(struct pattern_row)) &&
        qa_source_save_count(io, entries, SIZE_MAX / sizeof(struct haptic_entry)) &&
        qa_source_save_count(io, players, SIZE_MAX / sizeof(qa_haptic_player));
}
bool qa_haptic_checkpoint(const qa_haptic_cache *cache, qa_haptic_player *const *players, size_t count,
    const qa_haptic_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!cache || !refs || !refs->resource_encode || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "tactile capture requires actual holders and empty output");
    if (!players_valid(players, count, error)) return false;
    struct pattern_row *rows = NULL;
    size_t patterns = 0, entries = 0;
    bool success = true;
    for (const struct haptic_entry *e = cache->entries; success && e; e = e->next) {
        if (entries == SIZE_MAX) { success = false; break; }
        ++entries;
        success = e->source && pattern_valid(e->pattern) && add_pattern(&rows, &patterns, e->pattern, error);
        for (const struct haptic_entry *other = cache->entries; success && other != e; other = other->next)
            if (other->source == e->source) success = false;
    }
    for (size_t i = 0; success && i < count; ++i)
        success = player_valid(players[i]) && add_pattern(&rows, &patterns, players[i]->pattern, error);
    for (size_t i = 0; success && i < patterns; ++i)
        success = pattern_valid(rows[i].pattern) && rows[i].pattern->references == rows[i].holders;
    qa_source_save_io io = {0};
    success = success && qa_source_save_writer(&io, NULL, error) && header(&io, &patterns, &entries, &count);
    for (size_t i = 0; success && i < patterns; ++i) success = pattern_fields(&io, &rows[i].pattern);
    for (const struct haptic_entry *e = cache->entries; success && e; e = e->next) {
        uint64_t pool = 0, resource = 0, index = find_pattern(rows, patterns, e->pattern);
        success = refs->resource_encode(refs->context, e->source, &pool, &resource, error) &&
            qa_source_save_u64(&io, &pool) && qa_source_save_u64(&io, &resource) &&
            qa_source_save_u64(&io, &index);
    }
    for (size_t i = 0; success && i < count; ++i) {
        qa_haptic_player saved = *players[i];
        success = player_fields(&io, &saved, rows, patterns);
    }
    success = success && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(rows);
    if (!success && error && error->code == QA_OK)
        fail(error, QA_ERROR_UNSUPPORTED, "tactile holder graph is not completely qualified");
    return success;
}
bool qa_haptic_restore(qa_haptic_cache *cache, qa_haptic_player *const *players, size_t count,
    const qa_haptic_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!cache || !refs || !refs->resource_decode)
        return fail(error, QA_ERROR_ARGUMENT, "tactile restore requires actual owners and content resolver");
    if (!players_valid(players, count, error)) return false;
    struct pattern_row *rows = NULL;
    qa_haptic_player *saved = NULL;
    qa_haptic_cache *candidate = qa_haptic_cache_create(error);
    if (!candidate) return false;
    size_t patterns = 0, entries = 0, player_count = 0, decoded = 0;
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && header(&io, &patterns, &entries, &player_count) &&
        player_count == count;
    if (success && (io.offset > bytes.size || patterns > (bytes.size - io.offset) / 23 ||
        entries > (bytes.size - io.offset) / 24 || count > (bytes.size - io.offset) / 38)) success = false;
    if (success) {
        rows = calloc(patterns ? patterns : 1, sizeof(*rows));
        saved = calloc(count ? count : 1, sizeof(*saved));
        if (!rows || !saved) success = fail(error, QA_ERROR_MEMORY, "allocating tactile continuation graph");
    }
    for (size_t i = 0; success && i < patterns; ++i) success = pattern_fields(&io, &rows[i].pattern);
    struct haptic_entry **tail = &candidate->entries;
    for (size_t i = 0; success && i < entries; ++i) {
        uint64_t pool = 0, resource = 0, index = 0;
        const qa_resource *source = NULL;
        success = qa_source_save_u64(&io, &pool) && qa_source_save_u64(&io, &resource) &&
            qa_source_save_u64(&io, &index) &&
            index < patterns && refs->resource_decode(refs->context, pool, resource, &source, error) &&
            source != NULL;
        for (struct haptic_entry *e = candidate->entries; success && e; e = e->next)
            if (e->source == source) success = false;
        if (!success) break;
        struct haptic_entry *e = calloc(1, sizeof(*e));
        if (!e) { success = fail(error, QA_ERROR_MEMORY, "restoring tactile cache entry"); break; }
        e->pattern = rows[index].pattern; e->source = (qa_resource *)source;
        qa_haptic_pattern_retain(e->pattern); qa_resource_retain(e->source);
        ++rows[index].holders;
        *tail = e; tail = &e->next;
    }
    for (size_t i = 0; success && i < count; ++i) {
        saved[i].output = players[i]->output; saved[i].user = players[i]->user;
        decoded = i + 1;
        success = player_fields(&io, &saved[i], rows, patterns);
    }
    for (size_t i = 0; success && i < patterns; ++i) if (!rows[i].holders) success = false;
    success = success && qa_source_save_finish(&io, NULL);
    if (success) {
        struct haptic_entry *old = cache->entries;
        cache->entries = candidate->entries; candidate->entries = old;
        for (size_t i = 0; i < count; ++i) {
            qa_haptic_player old_player = *players[i];
            *players[i] = saved[i]; saved[i] = old_player;
        }
    }
    for (size_t i = 0; i < decoded; ++i) qa_haptic_pattern_release(saved[i].pattern);
    if (rows) for (size_t i = 0; i < patterns; ++i) qa_haptic_pattern_release(rows[i].pattern);
    qa_haptic_cache_destroy(candidate); free(saved); free(rows); qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "invalid tactile continuation graph");
    return success;
}
