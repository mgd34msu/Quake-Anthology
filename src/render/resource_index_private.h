#ifndef QA_RENDER_RESOURCE_INDEX_PRIVATE_H
#define QA_RENDER_RESOURCE_INDEX_PRIVATE_H

#include "qa/common.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct qa_render_resource_slot {
    uint64_t identity, revision;
    const void *owner;
    void *value;
} qa_render_resource_slot;
typedef struct qa_render_resource_index {
    qa_render_resource_slot *slots;
    size_t capacity, count;
} qa_render_resource_index;

static inline size_t render_resource_bucket(uint64_t identity, uint64_t revision,
                                            const void *owner, size_t mask)
{
    uint64_t key = identity ^ (revision * UINT64_C(0x9e3779b97f4a7c15)) ^
                   (uint64_t)(uintptr_t)owner;
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    return (size_t)key & mask;
}
static inline qa_render_resource_slot *render_resource_slot(
    const qa_render_resource_index *index, uint64_t identity, uint64_t revision,
    const void *owner)
{
    if (!index->capacity) return NULL;
    size_t mask = index->capacity - 1;
    size_t at = render_resource_bucket(identity, revision, owner, mask);
    for (;;) {
        qa_render_resource_slot *slot = index->slots + at;
        if (!slot->value || (slot->identity == identity && slot->revision == revision &&
                             slot->owner == owner)) return slot;
        at = (at + 1) & mask;
    }
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
    if (!slot->value) ++index->count;
    *slot = (qa_render_resource_slot){identity, revision, owner, value};
}
static inline bool render_resource_reserve(qa_render_resource_index *index,
                                           size_t required, qa_error *error)
{
    if (required <= index->capacity / 2) return true;
    size_t capacity = index->capacity ? index->capacity : 128;
    while (required > capacity / 2) {
        if (capacity > SIZE_MAX / 2) goto failed;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*index->slots)) goto failed;
    qa_render_resource_index next = {.capacity = capacity};
    next.slots = calloc(capacity, sizeof(*next.slots));
    if (!next.slots) goto failed;
    for (size_t i = 0; i < index->capacity; ++i) {
        const qa_render_resource_slot *slot = index->slots + i;
        if (slot->value)
            render_resource_put(&next, slot->identity, slot->revision, slot->owner, slot->value);
    }
    free(index->slots);
    *index = next;
    return true;
failed:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating renderer resource index");
    return false;
}
static inline void render_resource_remove(qa_render_resource_index *index,
    uint64_t identity, uint64_t revision, const void *owner)
{
    qa_render_resource_slot *slot = render_resource_slot(index, identity, revision, owner);
    if (!slot || !slot->value) return;
    size_t mask = index->capacity - 1;
    size_t at = (size_t)(slot - index->slots);
    slot->value = NULL;
    --index->count;
    for (at = (at + 1) & mask; index->slots[at].value; at = (at + 1) & mask) {
        qa_render_resource_slot displaced = index->slots[at];
        index->slots[at].value = NULL;
        --index->count;
        render_resource_put(index, displaced.identity, displaced.revision,
                            displaced.owner, displaced.value);
    }
}
static inline void render_resource_clear(qa_render_resource_index *index)
{
    if (index->slots) memset(index->slots, 0, index->capacity * sizeof(*index->slots));
    index->count = 0;
}
static inline void render_resource_destroy(qa_render_resource_index *index)
{
    free(index->slots);
    *index = (qa_render_resource_index){0};
}

#endif
