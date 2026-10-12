#include "positions_private.h"
#include "qa/allocation_gate.h"

static size_t find(const qa_audio_positions *positions, uint64_t actor, uint64_t owner,
    uint64_t projection)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
    size_t row;
    while (qa_id_index_next(&positions->actors, &cursor, &row))
        if (positions->rows[row].owner == owner &&
            positions->rows[row].projection == projection) return row;
    return SIZE_MAX;
}

static bool reserve_rows(qa_audio_positions *positions, size_t needed, qa_error *error)
{
    if (needed > positions->capacity) {
        if (positions->fixed) {
            qa_allocation_gate_capacity_exhausted();
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Reserved audio workspace exhausted");
            return false;
        }
        size_t maximum = SIZE_MAX / sizeof(*positions->rows);
        if (needed > maximum) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio position count overflow");
            return false;
        }
        size_t capacity = positions->capacity ? positions->capacity : 8;
        while (capacity < needed) {
            if (capacity > maximum / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        qa_audio_position *rows = qa_arena_grow(positions->storage, positions->rows,
            positions->count * sizeof(*rows), capacity * sizeof(*rows),
            _Alignof(qa_audio_position), error);
        if (!rows) return false;
        positions->rows = rows;
        positions->capacity = capacity;
    }
    return qa_id_index_reserve(&positions->actors, needed - positions->count, error);
}

bool qa_audio_positions_prepare(qa_audio_positions *positions, qa_arena *storage,
    size_t capacity, bool fixed, qa_error *error)
{
    if (storage) positions->storage = storage;
    else {
        qa_arena_init(&positions->owned_storage, 0);
        positions->storage = &positions->owned_storage;
    }
    positions->actors.storage = positions->storage;
    if (capacity > SIZE_MAX / sizeof(*positions->rows)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio allocation size overflow");
        qa_audio_positions_destroy(positions);
        return false;
    }
    if (capacity) positions->rows = qa_arena_alloc(positions->storage,
        capacity * sizeof(*positions->rows), _Alignof(qa_audio_position), error);
    if ((capacity && !positions->rows) ||
        !qa_id_index_reserve(&positions->actors, capacity, error)) {
        qa_audio_positions_destroy(positions);
        return false;
    }
    positions->capacity = capacity;
    positions->fixed = fixed;
    return true;
}

void qa_audio_positions_project(qa_audio_positions *positions,
    qa_audio_position_projection *projection)
{
    *projection = (qa_audio_position_projection){.id = ++positions->next_projection};
}

bool qa_audio_positions_set(qa_audio_positions *positions,
    const qa_audio_position_projection *projection, uint64_t actor,
    uint64_t owner, qa_vec3 origin, bool all_owners, qa_error *error)
{
    uint64_t scope = projection ? projection->id : 0;
    size_t row = find(positions, actor, owner, scope);
    if (row == SIZE_MAX) {
        if (positions->count == SIZE_MAX) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio position count overflow");
            return false;
        }
        if (!reserve_rows(positions, positions->count + 1, error)) return false;
        row = positions->count++;
        positions->rows[row] = (qa_audio_position){.actor = actor, .owner = owner,
            .projection = scope};
        qa_id_index_insert(&positions->actors, 0, actor, row);
    }
    uint64_t revision = ++positions->revision;
    positions->rows[row].origin = origin;
    positions->rows[row].revision = revision;
    if (all_owners) {
        qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
        while (qa_id_index_next(&positions->actors, &cursor, &row)) {
            qa_audio_position *position = &positions->rows[row];
            if (!projection || position->projection == scope) {
                position->origin = origin;
                position->revision = revision;
            }
        }
    }
    return true;
}

qa_vec3 qa_audio_positions_read(const qa_audio_positions *positions,
    const qa_audio_position_projection *projection, uint64_t actor,
    uint64_t owner, bool scoped)
{
    qa_vec3 fallback = qa_v3(0, 0, 0), projected = fallback;
    bool has_projected = false;
    qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
    size_t row;
    while (qa_id_index_next(&positions->actors, &cursor, &row)) {
        const qa_audio_position *position = &positions->rows[row];
        if (position->revision < projection->reset_revision) continue;
        if (position->projection == projection->id) {
            if (position->owner == QA_AUDIO_NO_OWNER) {
                projected = position->origin;
                has_projected = true;
            } else if (scoped && position->owner == owner) return position->origin;
        } else if (!position->projection && position->owner == QA_AUDIO_NO_OWNER)
            fallback = position->origin;
    }
    return has_projected ? projected : fallback;
}

void qa_audio_positions_projection_clear(qa_audio_positions *positions,
    qa_audio_position_projection *projection)
{
    if (!projection->id) {
        qa_audio_positions_clear(positions);
        return;
    }
    size_t retained = 0;
    for (size_t row = 0; row < positions->count; ++row)
        if (positions->rows[row].projection != projection->id)
            positions->rows[retained++] = positions->rows[row];
    if (retained != positions->count) {
        positions->count = retained;
        qa_id_index_clear(&positions->actors);
        for (size_t row = 0; row < retained; ++row)
            qa_id_index_insert(&positions->actors, 0, positions->rows[row].actor, row);
    }
    /* Global positions persist after this mixer stops, but it must not hear
     * them again until the next update. Newly created mixers inherit them. */
    projection->reset_revision = ++positions->revision;
}

void qa_audio_positions_clear(qa_audio_positions *positions)
{
    positions->count = 0;
    qa_id_index_clear(&positions->actors);
    ++positions->revision;
}

void qa_audio_positions_destroy(qa_audio_positions *positions)
{
    qa_id_index_destroy(&positions->actors);
    if (positions->storage == &positions->owned_storage)
        qa_arena_destroy(&positions->owned_storage);
    *positions = (qa_audio_positions){0};
}
