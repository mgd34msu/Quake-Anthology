#ifndef QA_RENDER_RESOURCE_INDEX_PRIVATE_H
#define QA_RENDER_RESOURCE_INDEX_PRIVATE_H

#include "qa/id_index.h"
#include "qa/pool.h"
#include <stdint.h>

typedef struct qa_render_resource_slot {
    uint64_t identity, revision;
    const void *owner;
    void *value;
    size_t slot;
} qa_render_resource_slot;
typedef struct qa_render_resource_index {
    qa_arena storage;
    qa_pool slots;
    qa_id_index identities;
    size_t count;
} qa_render_resource_index;

static inline uint64_t render_resource_key(uint64_t identity, const void *owner,
    qa_string_id *space)
{
    *space = identity ? 0 : 1;
    return identity ? identity : (uint64_t)(uintptr_t)owner;
}

static inline qa_render_resource_slot *render_resource_slot(
    const qa_render_resource_index *index, uint64_t identity, uint64_t revision,
    const void *owner)
{
    qa_string_id space;
    uint64_t key = render_resource_key(identity, owner, &space);
    qa_id_index_cursor cursor = qa_id_index_begin(&index->identities, space, key);
    size_t value;
    while (qa_id_index_next(&index->identities, &cursor, &value)) {
        qa_render_resource_slot *slot = qa_pool_at(&index->slots, value);
        if (slot->identity == identity && slot->revision == revision &&
            slot->owner == owner) return slot;
    }
    return NULL;
}
static inline void *render_resource_get(const qa_render_resource_index *index,
    uint64_t identity, uint64_t revision, const void *owner)
{
    qa_render_resource_slot *slot = render_resource_slot(index, identity, revision, owner);
    return slot ? slot->value : NULL;
}
/* Admission reserves first, so publication and draw lookup never allocate. */
static inline void render_resource_put(qa_render_resource_index *index,
    uint64_t identity, uint64_t revision, const void *owner, void *value)
{
    qa_render_resource_slot *slot = render_resource_slot(index, identity, revision, owner);
    if (!slot) {
        size_t ordinal;
        slot = qa_pool_take(&index->slots, &ordinal);
        *slot = (qa_render_resource_slot){identity, revision, owner, value, ordinal};
        qa_string_id space;
        uint64_t key = render_resource_key(identity, owner, &space);
        qa_id_index_insert(&index->identities, space, key, ordinal);
        ++index->count;
    } else slot->value = value;
}
static inline bool render_resource_reserve(qa_render_resource_index *index,
                                           size_t required, qa_error *error)
{
    if (required <= index->count) return true;
    if (!index->slots.stride) {
        qa_render_resource_index next = {0};
        next.identities.storage = &next.storage;
        if (!qa_pool_prepare(&next.slots, &next.storage, required,
                sizeof(qa_render_resource_slot), _Alignof(qa_render_resource_slot), error) ||
            !qa_id_index_reserve(&next.identities, required, error)) {
            qa_id_index_destroy(&next.identities);
            qa_arena_destroy(&next.storage);
            return false;
        }
        *index = next;
        index->identities.storage = &index->storage;
        return true;
    }
    if (!qa_id_index_reserve(&index->identities, required - index->count, error)) return false;
    if (required > index->slots.capacity) {
        size_t growth = required - index->slots.capacity;
        if (growth < index->slots.capacity) growth = index->slots.capacity;
        if (!qa_pool_extend(&index->slots, &index->storage, growth, error)) return false;
    }
    return true;
}
static inline void render_resource_remove(qa_render_resource_index *index,
    uint64_t identity, uint64_t revision, const void *owner)
{
    qa_render_resource_slot *slot = render_resource_slot(index, identity, revision, owner);
    if (!slot) return;
    qa_string_id space;
    uint64_t key = render_resource_key(identity, owner, &space);
    qa_id_index_remove_value(&index->identities, space, key, slot->slot);
    qa_pool_release(&index->slots, slot->slot);
    --index->count;
}
static inline void render_resource_clear(qa_render_resource_index *index)
{
    qa_id_index_clear(&index->identities);
    if (index->slots.stride) qa_pool_reset(&index->slots);
    index->count = 0;
}
static inline void render_resource_destroy(qa_render_resource_index *index)
{
    qa_id_index_destroy(&index->identities);
    qa_arena_destroy(&index->storage);
    *index = (qa_render_resource_index){0};
}

#endif
