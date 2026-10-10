#ifndef QA_ACTORS_INTERNAL_H
#define QA_ACTORS_INTERNAL_H

#include "entity_internal.h"

#define QA_ACTOR_PAGE_SHIFT 8u
#define QA_ACTOR_PAGE_SIZE (1u << QA_ACTOR_PAGE_SHIFT)

typedef struct qa_actor_page {
    qa_actor_record records[QA_ACTOR_PAGE_SIZE];
    uint64_t generations[QA_ACTOR_PAGE_SIZE];
    uint32_t saved_slots[QA_ACTOR_PAGE_SIZE];
    qa_world_body bodies[QA_ACTOR_PAGE_SIZE];
    qa_actor_player players[QA_ACTOR_PAGE_SIZE];
} qa_actor_page;

struct qa_actor_registry {
    qa_actor_page **pages;
    qa_spatial_link *links;
    uint64_t *free_bits;
    struct source_entry *sources;
    struct restored_slot *history;
    size_t source_capacity;
    uint64_t identity;
    uint64_t revision;
    uint32_t capacity;
    uint32_t page_count;
    uint32_t free_words;
    uint32_t first_free_word;
    uint32_t high_water;
    uint32_t live_count;
    uint32_t history_count;
    uint32_t callback_depth;
    bool clearing;
    qa_actor_release_fn release;
    void *context;
};

static inline qa_world_body *qa_actors_body(qa_actor_page *const *pages, uint32_t slot)
{
    return &pages[slot >> QA_ACTOR_PAGE_SHIFT]->bodies[slot & (QA_ACTOR_PAGE_SIZE - 1u)];
}

static inline qa_spatial_link *qa_actors_link(qa_spatial_link *links, uint32_t slot)
{
    return links + slot;
}

#endif
