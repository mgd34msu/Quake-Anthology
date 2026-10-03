#include "qa/actors.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SHIFT 8u
#define PAGE_SIZE (1u << PAGE_SHIFT)
#define NO_SAVED_SLOT UINT32_MAX

typedef struct actor_slot {
    qa_actor_record record;
    uint64_t generation;
    uint32_t saved_slot;
    bool live;
} actor_slot;

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
    actor_slot **pages;
    uint64_t *free_bits;
    source_entry *sources;
    restored_slot *history;
    size_t source_capacity;
    size_t source_count;
    size_t source_tombs;
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

static actor_slot *slot_at(const qa_actor_registry *registry, uint32_t index)
{
    actor_slot *page = registry->pages[index >> PAGE_SHIFT];
    return page == NULL ? NULL : &page[index & (PAGE_SIZE - 1u)];
}

static actor_slot *ensure_slot(qa_actor_registry *registry, uint32_t index,
                               qa_error *error)
{
    uint32_t page = index >> PAGE_SHIFT;
    if (registry->pages[page] == NULL) {
        registry->pages[page] = calloc(PAGE_SIZE, sizeof(actor_slot));
        if (registry->pages[page] == NULL) {
            fail(error, QA_ERROR_MEMORY, "Cannot allocate actor page");
            return NULL;
        }
    }
    return slot_at(registry, index);
}

static void free_storage(qa_actor_registry *registry)
{
    if (registry == NULL) return;
    if (registry->pages != NULL) {
        for (uint32_t i = 0; i < registry->page_count; ++i) free(registry->pages[i]);
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
    if (registry->sources[index].state == 2) --registry->source_tombs;
    registry->sources[index] = (source_entry){owner, source_slot, host_slot, 1};
    ++registry->source_count;
}

static bool source_reserve(qa_actor_registry *registry, qa_error *error)
{
    size_t capacity = registry->source_capacity;
    if (capacity != 0 && registry->source_count + registry->source_tombs + 1u <= capacity - capacity / 4u)
        return true;
    size_t next = capacity == 0 ? 16u : capacity;
    if (registry->source_count + 1u > next - next / 4u) {
        if (next > SIZE_MAX / 2u) return fail(error, QA_ERROR_MEMORY, "Actor source index exhausted");
        next *= 2u;
    }
    source_entry *entries = calloc(next, sizeof(*entries));
    if (entries == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate actor source index");
    source_entry *old = registry->sources;
    registry->sources = entries;
    registry->source_capacity = next;
    registry->source_count = 0;
    registry->source_tombs = 0;
    for (size_t i = 0; i < capacity; ++i) {
        if (old[i].state == 1) source_insert(registry, old[i].owner, old[i].source_slot, old[i].host_slot);
    }
    free(old);
    return true;
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
    uint64_t bits = registry->free_bits[word];
    unsigned bit = 0;
    while ((bits & UINT64_C(1)) == 0) { bits >>= 1u; ++bit; }
    uint32_t index = word * 64u + bit;
    actor_slot *slot = ensure_slot(registry, index, error);
    if (slot == NULL || (has_source && !source_reserve(registry, error))) return false;
    qa_actor_id id = {registry->identity, slot->generation, index};
    slot->record = (qa_actor_record){id, owner, definition, source_slot, has_source};
    slot->saved_slot = NO_SAVED_SLOT;
    slot->live = true;
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
    actor_slot *slot = slot_at(registry, actor.slot);
    return slot != NULL && slot->live && qa_actor_id_equal(slot->record.id, actor) ? &slot->record : NULL;
}

const qa_actor_record *qa_actors_at_source(const qa_actor_registry *registry,
                                         qa_actor_owner owner, uint32_t source_slot)
{
    if (registry == NULL) return NULL;
    source_entry *entry = source_find(registry, owner, source_slot);
    return entry == NULL ? NULL : &slot_at(registry, entry->host_slot)->record;
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
    actor_slot *slot = slot_at(registry, actor.slot);
    slot->record.owner = owner;
    slot->record.definition = definition;
    ++registry->revision;
    return true;
}

bool qa_actors_release(qa_actor_registry *registry, qa_actor_id actor, qa_error *error)
{
    if (qa_actors_get(registry, actor) == NULL)
        return fail(error, QA_ERROR_NOT_FOUND, "Actor handle is stale or foreign");
    actor_slot *slot = slot_at(registry, actor.slot);
    qa_actor_record released = slot->record;
    if (released.has_source) {
        source_find(registry, released.owner, released.source_slot)->state = 2;
        --registry->source_count;
        ++registry->source_tombs;
    }
    if (slot->saved_slot != NO_SAVED_SLOT) registry->history[slot->saved_slot].live = (qa_actor_id){0};
    slot->saved_slot = NO_SAVED_SLOT;
    slot->live = false;
    slot->record = (qa_actor_record){0};
    ++slot->generation;
    if (slot->generation != UINT64_MAX) {
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
    for (uint32_t i = 0; i < registry->high_water; ++i) {
        actor_slot *slot = slot_at(registry, i);
        if (slot != NULL && slot->live) (void)qa_actors_release(registry, slot->record.id, NULL);
    }
    if (registry->sources != NULL) memset(registry->sources, 0, registry->source_capacity * sizeof(*registry->sources));
    registry->source_tombs = 0;
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
        actor_slot *slot = slot_at(registry, (*cursor)++);
        if (slot != NULL && slot->live) { *out = &slot->record; return true; }
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
        const actor_slot *slot = slot_at(registry, i);
        if (slot == NULL) continue;
        slots[i] = (qa_actor_slot_checkpoint){slot->generation, slot->record.owner,
            slot->record.definition, slot->record.source_slot, slot->live, slot->record.has_source};
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
        actor_slot *slot = ensure_slot(registry, i, error);
        if (slot == NULL) { free_storage(registry); return false; }
        slot->generation = saved.generation;
        slot->saved_slot = NO_SAVED_SLOT;
        registry->history[i].saved = saved;
        registry->history[i].pending_source = saved.has_source;
        if (saved.active || saved.generation == UINT64_MAX)
            registry->free_bits[i / 64u] &= ~(UINT64_C(1) << (i % 64u));
        if (!saved.active) continue;
        qa_actor_id id = {registry->identity, saved.generation, i};
        slot->record = (qa_actor_record){id, saved.owner, saved.definition, saved.source_slot, saved.has_source};
        slot->live = true;
        slot->saved_slot = i;
        registry->history[i].reference = id;
        registry->history[i].live = id;
        ++registry->live_count;
        ++registry->revision;
        if (saved.has_source) {
            if (source_find(registry, saved.owner, saved.source_slot) != NULL) {
                free_storage(registry);
                return fail(error, QA_ERROR_FORMAT, "Duplicate actor source checkpoint");
            }
            if (!source_reserve(registry, error)) { free_storage(registry); return false; }
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
        const actor_slot *slot = slot_at(registry, saved.slot);
        if (slot == NULL || saved.generation > slot->generation
            || (!slot->live && saved.generation == slot->generation))
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
        if (actor == NULL || slot_at(registry, actor->id.slot)->saved_slot != NO_SAVED_SLOT)
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
        slot_at(registry, actor->id.slot)->saved_slot = i;
    }
    return true;
}
