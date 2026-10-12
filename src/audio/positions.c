#include "positions_private.h"
#include "qa/allocation_gate.h"

static size_t find(const qa_audio_positions *positions, uint64_t actor, uint64_t owner)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
    size_t row;
    while (qa_id_index_next(&positions->actors, &cursor, &row))
        if (positions->rows[row].owner == owner) return row;
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

bool qa_audio_positions_reserve(qa_audio_positions *positions, uint64_t actor,
    uint64_t owner, qa_error *error)
{
    if (find(positions, actor, owner) != SIZE_MAX) return true;
    if (positions->count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Audio position count overflow");
        return false;
    }
    return reserve_rows(positions, positions->count + 1, error);
}

bool qa_audio_positions_set(qa_audio_positions *positions, uint64_t actor,
    uint64_t owner, qa_vec3 origin, bool all_owners, qa_error *error)
{
    size_t row = find(positions, actor, owner);
    if (row == SIZE_MAX) {
        if (!qa_audio_positions_reserve(positions, actor, owner, error)) return false;
        row = positions->count++;
        positions->rows[row] = (qa_audio_position){actor, owner, origin};
        qa_id_index_insert(&positions->actors, 0, actor, row);
    } else positions->rows[row].origin = origin;
    if (all_owners) {
        qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
        while (qa_id_index_next(&positions->actors, &cursor, &row))
            positions->rows[row].origin = origin;
    }
    return true;
}

qa_vec3 qa_audio_positions_read(const qa_audio_positions *positions, uint64_t actor,
    uint64_t owner, bool scoped)
{
    qa_vec3 fallback = qa_v3(0, 0, 0);
    qa_id_index_cursor cursor = qa_id_index_begin(&positions->actors, 0, actor);
    size_t row;
    while (qa_id_index_next(&positions->actors, &cursor, &row)) {
        const qa_audio_position *position = &positions->rows[row];
        if (position->owner == QA_AUDIO_NO_OWNER) fallback = position->origin;
        else if (scoped && position->owner == owner) return position->origin;
    }
    return fallback;
}

void qa_audio_positions_clear(qa_audio_positions *positions)
{
    positions->count = 0;
    qa_id_index_clear(&positions->actors);
}

void qa_audio_positions_destroy(qa_audio_positions *positions)
{
    qa_id_index_destroy(&positions->actors);
    if (positions->storage == &positions->owned_storage)
        qa_arena_destroy(&positions->owned_storage);
    *positions = (qa_audio_positions){0};
}
