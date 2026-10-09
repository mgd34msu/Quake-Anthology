#include "qa/actors.h"
#include "entity_internal.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SHIFT 8u
#define PAGE_SIZE (1u << PAGE_SHIFT)
#define NO_SAVED_SLOT UINT32_MAX

typedef struct actor_page {
    qa_actor_record records[PAGE_SIZE];
    uint64_t generations[PAGE_SIZE];
    uint32_t saved_slots[PAGE_SIZE];
    qa_world_body bodies[PAGE_SIZE];
} actor_page;

typedef struct source_entry {
    qa_actor_owner owner;
    uint32_t source_slot;
    uint32_t host_slot;
    unsigned char state;
} source_entry;

typedef struct restored_slot {
    qa_actor_slot_checkpoint saved;
    qa_actor_id reference;
    qa_actor_id live;
    bool pending_source;
} restored_slot;

struct qa_actor_registry {
    actor_page **pages;
    uint64_t *free_bits;
    source_entry *sources;
    restored_slot *history;
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

static atomic_uint_fast64_t next_identity = 1;

static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static qa_actor_record *record_at(const qa_actor_registry *registry, uint32_t index)
{
    actor_page *page = registry->pages[index >> PAGE_SHIFT];
    return page == NULL ? NULL : &page->records[index & (PAGE_SIZE - 1u)];
}

qa_world_body *qa_actors_body(const qa_actor_registry *registry, uint32_t slot)
{
    if (registry == NULL || slot >= registry->capacity) return NULL;
    return &registry->pages[slot >> PAGE_SHIFT]->bodies[slot & (PAGE_SIZE - 1u)];
}

static unsigned first_bit(uint64_t bits)
{
    unsigned bit = 0;
    while ((bits & UINT64_C(1)) == 0) { bits >>= 1u; ++bit; }
    return bit;
}

static void free_storage(qa_actor_registry *registry)
{
    if (registry == NULL) return;
    if (registry->pages != NULL) {
        for (uint32_t i = 0; i < registry->page_count; ++i) {
            actor_page *page = registry->pages[i];
            if (page != NULL)
                for (size_t slot = 0; slot < PAGE_SIZE; ++slot) free(page->bodies[slot].leaves);
            free(page);
        }
    }
    free(registry->pages);
    free(registry->free_bits);
    free(registry->sources);
    free(registry->history);
    free(registry);
}

bool qa_actor_id_equal(qa_actor_id left, qa_actor_id right)
{
    return left.registry == right.registry && left.generation == right.generation
        && left.slot == right.slot;
}

qa_actor_reference qa_actor_reference_lifetime(qa_actor_id actor)
{
    return actor.registry ? (qa_actor_reference){.kind = QA_ACTOR_REFERENCE_LIFETIME,
        .value.actor = actor} : (qa_actor_reference){0};
}

qa_actor_reference qa_actor_reference_source(qa_actor_owner owner, uint32_t slot)
{
    return (qa_actor_reference){.kind = QA_ACTOR_REFERENCE_SOURCE,
        .value.source = {.owner = owner, .slot = slot}};
}

qa_actor_reference qa_actor_reference_from_actor(const qa_actor_registry *registry,
                                                 qa_actor_owner owner, qa_actor_id actor)
{
    const qa_actor_record *record = qa_actors_get(registry, actor);
    return record && record->owner == owner && record->has_source
        ? qa_actor_reference_source(owner, record->source_slot)
        : qa_actor_reference_lifetime(actor);
}

bool qa_actor_reference_present(qa_actor_reference reference)
{
    return reference.kind == QA_ACTOR_REFERENCE_SOURCE ||
        (reference.kind == QA_ACTOR_REFERENCE_LIFETIME && reference.value.actor.registry);
}

bool qa_actor_reference_equal(qa_actor_reference left, qa_actor_reference right)
{
    if (left.kind != right.kind) return false;
    if (left.kind == QA_ACTOR_REFERENCE_NONE) return true;
    if (left.kind == QA_ACTOR_REFERENCE_SOURCE)
        return left.value.source.owner == right.value.source.owner &&
            left.value.source.slot == right.value.source.slot;
    return qa_actor_id_equal(left.value.actor, right.value.actor);
}

qa_actor_id qa_actor_reference_resolve(const qa_actor_registry *registry,
    qa_actor_reference reference)
{
    if (reference.kind == QA_ACTOR_REFERENCE_LIFETIME) return reference.value.actor;
    if (reference.kind == QA_ACTOR_REFERENCE_SOURCE) {
        const qa_actor_record *record = qa_actors_at_source(registry,
            reference.value.source.owner, reference.value.source.slot);
        if (record) return record->id;
    }
    return (qa_actor_id){0};
}

bool qa_actors_create(uint32_t capacity, qa_actor_release_fn release,
                      void *context, qa_actor_registry **out, qa_error *error)
{
    if (capacity == 0 || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Actor capacity and output are required");
    qa_actor_registry *registry = calloc(1, sizeof(*registry));
    if (registry == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor registry");
    registry->capacity = capacity;
    registry->page_count = (capacity - 1u) / PAGE_SIZE + 1u;
    registry->free_words = (capacity - 1u) / 64u + 1u;
    registry->pages = calloc(registry->page_count, sizeof(*registry->pages));
    registry->free_bits = calloc(registry->free_words, sizeof(*registry->free_bits));
    if (registry->pages == NULL || registry->free_bits == NULL) {
        free_storage(registry);
        return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor slot index");
    }
    for (uint32_t i = 0; i < registry->page_count; ++i) {
        registry->pages[i] = calloc(1, sizeof(actor_page));
        if (registry->pages[i] == NULL) {
            free_storage(registry);
            return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor page");
        }
    }
    size_t source_capacity = 16u;
    while (source_capacity / 2u < capacity) {
        if (source_capacity > SIZE_MAX / 2u) {
            free_storage(registry);
            return fail(error, QA_ERROR_MEMORY, "Actor source index exhausted");
        }
        source_capacity *= 2u;
    }
    registry->sources = calloc(source_capacity, sizeof(*registry->sources));
    if (registry->sources == NULL) {
        free_storage(registry);
        return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor source index");
    }
    registry->source_capacity = source_capacity;
    uint_fast64_t identity = atomic_load_explicit(&next_identity, memory_order_relaxed);
    do {
        if (identity == UINT64_MAX) {
            free_storage(registry);
            return fail(error, QA_ERROR_MEMORY, "Actor registry identities exhausted");
        }
    } while (!atomic_compare_exchange_weak_explicit(&next_identity, &identity,
               identity + 1u, memory_order_relaxed, memory_order_relaxed));
    registry->identity = (uint64_t)identity;
    registry->release = release;
    registry->context = context;
    for (uint32_t i = 0; i < registry->free_words; ++i) registry->free_bits[i] = UINT64_MAX;
    unsigned tail = capacity % 64u;
    if (tail != 0) registry->free_bits[registry->free_words - 1u] = (UINT64_C(1) << tail) - 1u;
    *out = registry;
    return true;
}

static size_t source_hash(qa_actor_owner owner, uint32_t slot)
{
    uint64_t key = ((uint64_t)owner << 32u) | slot;
    key = (key ^ (key >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
    key = (key ^ (key >> 27u)) * UINT64_C(0x94d049bb133111eb);
    return (size_t)(key ^ (key >> 31u));
}

static source_entry *source_find(const qa_actor_registry *registry,
                                 qa_actor_owner owner, uint32_t slot)
{
    if (registry->source_capacity == 0) return NULL;
    size_t mask = registry->source_capacity - 1u;
    size_t index = source_hash(owner, slot) & mask;
    for (;;) {
        source_entry *entry = &registry->sources[index];
        if (entry->state == 0) return NULL;
        if (entry->state == 1 && entry->owner == owner && entry->source_slot == slot) return entry;
        index = (index + 1u) & mask;
    }
}

static void source_insert(qa_actor_registry *registry, qa_actor_owner owner,
                           uint32_t source_slot, uint32_t host_slot)
{
    size_t mask = registry->source_capacity - 1u;
    size_t index = source_hash(owner, source_slot) & mask;
    while (registry->sources[index].state == 1) index = (index + 1u) & mask;
    registry->sources[index] = (source_entry){owner, source_slot, host_slot, 1};
}

static void source_remove(qa_actor_registry *registry, qa_actor_owner owner,
                          uint32_t slot)
{
    size_t mask = registry->source_capacity - 1u;
    size_t gap = (size_t)(source_find(registry, owner, slot) - registry->sources);
    for (size_t index = (gap + 1u) & mask; registry->sources[index].state != 0;
         index = (index + 1u) & mask) {
        source_entry entry = registry->sources[index];
        size_t home = source_hash(entry.owner, entry.source_slot) & mask;
        if (((gap - home) & mask) < ((index - home) & mask)) {
            registry->sources[gap] = entry;
            gap = index;
        }
    }
    registry->sources[gap] = (source_entry){0};
}

static bool allocate(qa_actor_registry *registry, qa_actor_owner owner,
                      qa_actor_definition definition, bool has_source,
                      uint32_t source_slot, qa_actor_id *out, qa_error *error)
{
    if (registry == NULL || out == NULL || registry->clearing)
        return fail(error, QA_ERROR_ARGUMENT, "Actor registry cannot allocate in this state");
    if (has_source && source_find(registry, owner, source_slot) != NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Actor source slot is occupied");
    uint32_t word = registry->first_free_word;
    while (word < registry->free_words && registry->free_bits[word] == 0) ++word;
    registry->first_free_word = word;
    if (word == registry->free_words) return fail(error, QA_ERROR_MEMORY, "Actor registry is full");
    unsigned bit = first_bit(registry->free_bits[word]);
    uint32_t index = word * 64u + bit;
    actor_page *page = registry->pages[index >> PAGE_SHIFT];
    uint32_t offset = index & (PAGE_SIZE - 1u);
    qa_actor_id id = {registry->identity, page->generations[offset], index};
    page->records[offset] = (qa_actor_record){id, owner, definition, source_slot, has_source};
    page->saved_slots[offset] = NO_SAVED_SLOT;
    registry->free_bits[word] &= ~(UINT64_C(1) << bit);
    if (has_source) source_insert(registry, owner, source_slot, index);
    if (registry->high_water <= index) registry->high_water = index + 1u;
    ++registry->live_count;
    ++registry->revision;
    *out = id;
    return true;
}

bool qa_actors_allocate(qa_actor_registry *registry, qa_actor_owner owner,
                        qa_actor_definition definition, qa_actor_id *out, qa_error *error)
{
    return allocate(registry, owner, definition, false, 0, out, error);
}

bool qa_actors_allocate_source(qa_actor_registry *registry, qa_actor_owner owner,
                               uint32_t source_slot, qa_actor_definition definition,
                               qa_actor_id *out, qa_error *error)
{
    return allocate(registry, owner, definition, true, source_slot, out, error);
}

const qa_actor_record *qa_actors_get(const qa_actor_registry *registry, qa_actor_id actor)
{
    if (registry == NULL || actor.registry != registry->identity || actor.slot >= registry->high_water) return NULL;
    const qa_actor_record *record = record_at(registry, actor.slot);
    return record != NULL && qa_actor_id_equal(record->id, actor) ? record : NULL;
}

const qa_actor_record *qa_actors_at_source(const qa_actor_registry *registry,
                                         qa_actor_owner owner, uint32_t source_slot)
{
    if (registry == NULL) return NULL;
    source_entry *entry = source_find(registry, owner, source_slot);
    return entry == NULL ? NULL : record_at(registry, entry->host_slot);
}

bool qa_actors_set_metadata(qa_actor_registry *registry, qa_actor_id actor,
                            qa_actor_owner owner, qa_actor_definition definition, qa_error *error)
{
    const qa_actor_record *record = qa_actors_get(registry, actor);
    if (!record || registry->callback_depth || registry->clearing)
        return fail(error, QA_ERROR_ARGUMENT, "Actor metadata requires its returned live identity");
    if (record->has_source && record->owner != owner)
        return fail(error, QA_ERROR_ARGUMENT, "Actor metadata cannot change its indexed Source owner");
    if (record->owner == owner && record->definition == definition) return true;
    if (registry->revision == UINT64_MAX)
        return fail(error, QA_ERROR_MEMORY, "Actor metadata revision exhausted");
    qa_actor_record *mutable = record_at(registry, actor.slot);
    mutable->owner = owner;
    mutable->definition = definition;
    ++registry->revision;
    return true;
}

bool qa_actors_bind_source(qa_actor_registry *registry, qa_actor_id actor,
                          uint32_t source_slot, qa_error *error)
{
    const qa_actor_record *record = qa_actors_get(registry, actor);
    if (!record || registry->clearing)
        return fail(error, QA_ERROR_ARGUMENT, "Source admission requires its live actor");
    if (record->has_source)
        return record->source_slot == source_slot ||
            fail(error, QA_ERROR_ARGUMENT, "Source admission cannot change a physical slot");
    if (source_find(registry, record->owner, source_slot))
        return fail(error, QA_ERROR_ARGUMENT, "Actor source slot is occupied");
    if (registry->revision == UINT64_MAX)
        return fail(error, QA_ERROR_MEMORY, "Actor metadata revision exhausted");
    qa_actor_record *mutable = record_at(registry, actor.slot);
    source_insert(registry, record->owner, source_slot, actor.slot);
    mutable->source_slot = source_slot;
    mutable->has_source = true;
    ++registry->revision;
    return true;
}

bool qa_actors_release(qa_actor_registry *registry, qa_actor_id actor, qa_error *error)
{
    if (qa_actors_get(registry, actor) == NULL)
        return fail(error, QA_ERROR_NOT_FOUND, "Actor handle is stale or foreign");
    actor_page *page = registry->pages[actor.slot >> PAGE_SHIFT];
    uint32_t offset = actor.slot & (PAGE_SIZE - 1u);
    qa_actor_record released = page->records[offset];
    if (released.has_source) {
        source_remove(registry, released.owner, released.source_slot);
    }
    if (page->saved_slots[offset] != NO_SAVED_SLOT) registry->history[page->saved_slots[offset]].live = (qa_actor_id){0};
    page->saved_slots[offset] = NO_SAVED_SLOT;
    page->records[offset] = (qa_actor_record){0};
    ++page->generations[offset];
    if (page->generations[offset] != UINT64_MAX) {
        registry->free_bits[actor.slot / 64u] |= UINT64_C(1) << (actor.slot % 64u);
        if (actor.slot / 64u < registry->first_free_word)
            registry->first_free_word = actor.slot / 64u;
    }
    --registry->live_count;
    ++registry->revision;
    if (registry->release != NULL) {
        ++registry->callback_depth;
        registry->release(registry->context, registry, released);
        --registry->callback_depth;
    }
    return true;
}

bool qa_actors_clear(qa_actor_registry *registry, qa_error *error)
{
    if (registry == NULL || registry->clearing || registry->callback_depth != 0)
        return fail(error, QA_ERROR_ARGUMENT, "Actor clear requires an idle registry");
    registry->clearing = true;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(registry, &cursor, &record))
        (void)qa_actors_release(registry, record->id, NULL);
    if (registry->sources != NULL) memset(registry->sources, 0, registry->source_capacity * sizeof(*registry->sources));
    free(registry->history);
    registry->history = NULL;
    registry->history_count = 0;
    registry->clearing = false;
    return true;
}

bool qa_actors_destroy(qa_actor_registry *registry, qa_error *error)
{
    if (registry == NULL) return true;
    if (!qa_actors_clear(registry, error)) return false;
    free_storage(registry);
    return true;
}

bool qa_actors_next(const qa_actor_registry *registry, uint32_t *cursor,
                    const qa_actor_record **out)
{
    if (registry == NULL || cursor == NULL || out == NULL) return false;
    while (*cursor < registry->high_water) {
        uint32_t word = *cursor / 64u;
        unsigned offset = *cursor % 64u;
        uint64_t occupied = ~registry->free_bits[word] & (UINT64_MAX << offset);
        if (occupied == 0) {
            uint32_t remaining = registry->high_water - *cursor;
            *cursor += remaining < 64u - offset ? remaining : 64u - offset;
            continue;
        }
        uint32_t index = word * 64u + first_bit(occupied);
        if (index >= registry->high_water) { *cursor = registry->high_water; break; }
        *cursor = index + 1u;
        const qa_actor_record *record = record_at(registry, index);
        if (record != NULL && record->id.registry) { *out = record; return true; }
    }
    return false;
}

uint32_t qa_actors_count(const qa_actor_registry *registry) { return registry == NULL ? 0 : registry->live_count; }
uint32_t qa_actors_capacity(const qa_actor_registry *registry) { return registry == NULL ? 0 : registry->capacity; }
uint64_t qa_actors_revision(const qa_actor_registry *registry) { return registry == NULL ? 0 : registry->revision; }
uint64_t qa_actors_identity(const qa_actor_registry *registry) { return registry == NULL ? 0 : registry->identity; }

bool qa_actors_set_release_observer(qa_actor_registry *registry, qa_actor_release_fn release, void *context)
{
    if (!registry || registry->callback_depth || registry->clearing) return false;
    registry->release = release;
    registry->context = context;
    return true;
}

bool qa_actors_checkpoint(const qa_actor_registry *registry,
                          qa_actor_checkpoint *out, qa_error *error)
{
    if (registry == NULL || out == NULL || registry->callback_depth != 0 || registry->clearing)
        return fail(error, QA_ERROR_ARGUMENT, "Actor checkpoint requires an idle registry");
    qa_actor_slot_checkpoint *slots = NULL;
    if (registry->high_water != 0) {
        slots = calloc(registry->high_water, sizeof(*slots));
        if (slots == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor checkpoint");
    }
    for (uint32_t i = 0; i < registry->high_water; ++i) {
        const actor_page *page = registry->pages[i >> PAGE_SHIFT];
        if (page == NULL) continue;
        uint32_t offset = i & (PAGE_SIZE - 1u);
        const qa_actor_record *record = &page->records[offset];
        slots[i] = (qa_actor_slot_checkpoint){page->generations[offset], record->owner,
            record->definition, record->source_slot, record->id.registry != 0, record->has_source};
    }
    *out = (qa_actor_checkpoint){slots, registry->high_water, registry->capacity};
    return true;
}

void qa_actor_checkpoint_free(qa_actor_checkpoint *checkpoint)
{
    if (checkpoint == NULL) return;
    free(checkpoint->slots);
    *checkpoint = (qa_actor_checkpoint){0};
}

bool qa_actors_restore(const qa_actor_checkpoint *checkpoint,
                       qa_actor_release_fn release, void *context,
                       qa_actor_registry **out, qa_error *error)
{
    if (checkpoint == NULL || out == NULL || checkpoint->count > checkpoint->capacity
        || (checkpoint->count != 0 && checkpoint->slots == NULL))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid actor checkpoint storage");
    qa_actor_registry *registry = NULL;
    if (!qa_actors_create(checkpoint->capacity, release, context, &registry, error)) return false;
    if (checkpoint->count != 0) {
        registry->history = calloc(checkpoint->count, sizeof(*registry->history));
        if (registry->history == NULL) {
            free_storage(registry);
            return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor history");
        }
    }
    registry->history_count = checkpoint->count;
    registry->high_water = checkpoint->count;
    for (uint32_t i = 0; i < checkpoint->count; ++i) {
        qa_actor_slot_checkpoint saved = checkpoint->slots[i];
        if ((!saved.active && saved.has_source) || (saved.active && saved.generation == UINT64_MAX)) {
            free_storage(registry);
            return fail(error, QA_ERROR_FORMAT, "Invalid actor checkpoint lifetime");
        }
        actor_page *page = registry->pages[i >> PAGE_SHIFT];
        uint32_t offset = i & (PAGE_SIZE - 1u);
        page->generations[offset] = saved.generation;
        page->saved_slots[offset] = NO_SAVED_SLOT;
        registry->history[i].saved = saved;
        registry->history[i].pending_source = saved.has_source;
        if (saved.active || saved.generation == UINT64_MAX)
            registry->free_bits[i / 64u] &= ~(UINT64_C(1) << (i % 64u));
        if (!saved.active) continue;
        qa_actor_id id = {registry->identity, saved.generation, i};
        page->records[offset] = (qa_actor_record){id, saved.owner, saved.definition, saved.source_slot, saved.has_source};
        page->saved_slots[offset] = i;
        registry->history[i].reference = id;
        registry->history[i].live = id;
        ++registry->live_count;
        ++registry->revision;
        if (saved.has_source) {
            if (source_find(registry, saved.owner, saved.source_slot) != NULL) {
                free_storage(registry);
                return fail(error, QA_ERROR_FORMAT, "Duplicate actor source checkpoint");
            }
            source_insert(registry, saved.owner, saved.source_slot, i);
        }
    }
    *out = registry;
    return true;
}

const qa_actor_record *qa_actors_resolve_saved(const qa_actor_registry *registry,
                                             qa_saved_actor_id saved)
{
    if (registry == NULL) return NULL;
    if (saved.slot < registry->history_count) {
        const restored_slot *history = &registry->history[saved.slot];
        if (!history->saved.active || history->saved.generation != saved.generation) return NULL;
        return qa_actors_get(registry, history->live);
    }
    qa_actor_id id = {registry->identity, saved.generation, saved.slot};
    return qa_actors_get(registry, id);
}

bool qa_actors_save_reference(const qa_actor_registry *registry, qa_actor_id actor,
                              qa_saved_actor_id *out, qa_error *error)
{
    if (registry == NULL || out == NULL || actor.registry != registry->identity)
        return fail(error, QA_ERROR_ARGUMENT, "Cannot save a foreign actor reference");
    qa_saved_actor_id saved = {actor.generation, actor.slot};
    qa_actor_id reference;
    if (!qa_actors_reference_saved(registry, saved, false, &reference, error)) return false;
    *out = saved;
    return true;
}

bool qa_actors_reference_saved(const qa_actor_registry *registry,
                               qa_saved_actor_id saved, bool checkpoint_domain,
                               qa_actor_id *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Actor history requires registry and output");
    if (checkpoint_domain) {
        if (saved.slot >= registry->history_count)
            return fail(error, QA_ERROR_NOT_FOUND, "Actor is outside checkpoint history");
        const restored_slot *history = &registry->history[saved.slot];
        if (saved.generation > history->saved.generation
            || (!history->saved.active && saved.generation == history->saved.generation))
            return fail(error, QA_ERROR_NOT_FOUND, "Invalid checkpoint actor history");
        if (history->saved.active && saved.generation == history->saved.generation) {
            *out = history->reference;
            return true;
        }
    } else {
        if (saved.slot >= registry->high_water)
            return fail(error, QA_ERROR_NOT_FOUND, "Actor is outside current history");
        const actor_page *page = registry->pages[saved.slot >> PAGE_SHIFT];
        uint32_t offset = saved.slot & (PAGE_SIZE - 1u);
        if (page == NULL || saved.generation > page->generations[offset]
            || (!page->records[offset].id.registry && saved.generation == page->generations[offset]))
            return fail(error, QA_ERROR_NOT_FOUND, "Invalid current actor history");
    }
    *out = (qa_actor_id){registry->identity, saved.generation, saved.slot};
    return true;
}

bool qa_actors_rebind_restored_source(qa_actor_registry *registry,
                                     qa_actor_owner owner, qa_error *error)
{
    if (registry == NULL || registry->callback_depth != 0 || registry->clearing)
        return fail(error, QA_ERROR_ARGUMENT, "Actor rebind requires an idle registry");
    bool found = false;
    for (uint32_t i = 0; i < registry->history_count; ++i) {
        const restored_slot *history = &registry->history[i];
        if (!history->pending_source || history->saved.owner != owner) continue;
        found = true;
        if (qa_actors_get(registry, history->live) != NULL)
            return fail(error, QA_ERROR_ARGUMENT, "Saved source actor is still live");
        const qa_actor_record *actor = qa_actors_at_source(registry, owner, history->saved.source_slot);
        if (actor == NULL || registry->pages[actor->id.slot >> PAGE_SHIFT]
            ->saved_slots[actor->id.slot & (PAGE_SIZE - 1u)] != NO_SAVED_SLOT)
            return fail(error, QA_ERROR_ARGUMENT, "Reconstructed source actor is missing or already bound");
    }
    if (!found) return fail(error, QA_ERROR_NOT_FOUND, "Source has no pending checkpoint reconstruction");
    for (uint32_t i = 0; i < registry->history_count; ++i) {
        restored_slot *history = &registry->history[i];
        if (!history->pending_source || history->saved.owner != owner) continue;
        const qa_actor_record *actor = qa_actors_at_source(registry, owner, history->saved.source_slot);
        history->live = actor->id;
        history->reference = actor->id;
        history->pending_source = false;
        registry->pages[actor->id.slot >> PAGE_SHIFT]
            ->saved_slots[actor->id.slot & (PAGE_SIZE - 1u)] = i;
    }
    return true;
}
