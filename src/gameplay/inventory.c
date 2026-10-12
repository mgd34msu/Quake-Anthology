#include "inventory_internal.h"
#include "qa/operation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_inventory_hold(qa_inventory *table) { ++table->calls; }
void qa_inventory_unhold(qa_inventory *table) { --table->calls; }
bool qa_inventory_idle(const qa_inventory *table) { return !table || !table->calls; }

static bool fail(qa_error *e, qa_status status, const char *message)
{ qa_error_set(e, status, 0, "%s", message); return false; }

static void counts_changed(inventory_store *store)
{ if (store->revision != UINT64_MAX) ++store->revision; }
static void store_changed(inventory_store *store)
{
    counts_changed(store);
    if (store->definition_revision != UINT64_MAX) ++store->definition_revision;
}

static const uint64_t all_owners = UINT64_MAX;

static bool index_value(const qa_id_index *index, qa_item_id item,
    uint64_t owner, size_t *value)
{
    qa_id_index_cursor cursor = qa_id_index_begin(index, item, owner);
    return qa_id_index_next(index, &cursor, value);
}

static pickup_claim *pickup_for(const inventory_store *store, qa_item_id item)
{
    size_t value;
    return index_value(&store->pickup_items, item, 0, &value) ?
        (pickup_claim *)(uintptr_t)value : NULL;
}

static void pickup_token_release(inventory_store *store, const void *token)
{
    size_t value;
    uint64_t token_id = (uint64_t)(uintptr_t)token;
    while (index_value(&store->pickup_items, 0, token_id, &value)) {
        pickup_claim *claim = (pickup_claim *)(uintptr_t)value;
        *claim->link = claim->next;
        if (claim->next) claim->next->link = claim->link;
        qa_id_index_remove_value(&store->pickup_items, claim->item, 0, value);
        qa_id_index_remove_value(&store->pickup_items, 0, token_id, value);
        qa_pool_release(&store->storage_owner->pickup_records, claim->storage_slot);
    }
}

static size_t local_index(const inventory_store *store, qa_item_id item)
{
    size_t ordinal;
    return index_value(&store->local_items, item, 0, &ordinal) ? ordinal : store->count;
}

static void local_reindex(inventory_store *store)
{
    qa_id_index_clear(&store->local_items);
    for (size_t i = 0; i < store->count; ++i)
        qa_id_index_insert(&store->local_items, store->entries[i].item, 0, i);
}

static bool group_index_prepare(item_group *group, qa_error *e)
{
    group->item_index.storage = &group->index_storage;
    if (!qa_id_index_reserve(&group->item_index, group->count, e)) return false;
    qa_id_index_clear(&group->item_index);
    for (size_t i = 0; i < group->count; ++i)
        qa_id_index_insert(&group->item_index, group->items[i].definition.item, 0, i);
    return true;
}

static void groups_reindex(inventory_store *store)
{
    qa_id_index_clear(&store->definitions);
    size_t order = 0;
    for (item_group *group = store->groups; group; group = group->next) if (group->active) {
        group->order = order++;
        qa_id_index_insert(&store->definitions, 0, group->serial, (size_t)(uintptr_t)group);
        for (size_t i = 0; i < group->count; ++i) {
            qa_item_id item = group->items[i].definition.item;
            qa_id_index_insert(&store->definitions, item, all_owners, (size_t)(uintptr_t)group);
            qa_id_index_insert(&store->definitions, item, group->owner, (size_t)(uintptr_t)group);
        }
    }
}

static inventory_source_index *source_index_take(inventory_store *store, qa_error *e)
{
    qa_inventory *owner = store->storage_owner;
    qa_pool *pool = &owner->source_records;
    if (pool->head == SIZE_MAX) {
        size_t previous = pool->capacity;
        if (!qa_pool_extend(pool, &owner->record_storage, previous, e)) return NULL;
        for (size_t i = previous; i < pool->capacity; ++i)
            memset(qa_pool_at(pool, i), 0, sizeof(inventory_source_index));
    }
    size_t slot;
    inventory_source_index *index = qa_pool_take(pool, &slot);
    if (!index) { fail(e, QA_ERROR_MEMORY, "Cannot prepare inventory source index"); return NULL; }
    index->items.storage = &index->storage;
    qa_id_index_clear(&index->items);
    index->owner = owner; index->storage_slot = slot;
    index->revision = 0; index->ready = index->validated = false;
    return index;
}

static void source_index_dispose(inventory_source_index *index)
{
    if (!index) return;
    qa_id_index_clear(&index->items);
    index->ready = index->validated = false;
    qa_pool_release(&index->owner->source_records, index->storage_slot);
}

static inventory_source_index *source_candidate_take(inventory_store *store,
    item_group *group, qa_error *e)
{
    inventory_source_index **slot = group ? &group->source_candidate : &store->primary_candidate;
    inventory_source_index *candidate = *slot;
    *slot = NULL;
    return candidate ? candidate : source_index_take(store, e);
}

static void source_candidate_return(inventory_store *store, item_group *group,
    inventory_source_index *candidate)
{
    if (!candidate) return;
    qa_id_index_clear(&candidate->items);
    candidate->ready = candidate->validated = false;
    inventory_source_index **slot = group ? &group->source_candidate : &store->primary_candidate;
    if (!*slot) *slot = candidate;
    else source_index_dispose(candidate);
}

static bool source_candidate_prepare(inventory_store *store, item_group *group,
    size_t count, qa_error *e)
{
    inventory_source_index **slot = group ? &group->source_candidate : &store->primary_candidate;
    inventory_source_index *candidate = *slot;
    if (!candidate) candidate = group ? group->source_index : store->primary_index;
    if (candidate) {
        size_t additional = count > candidate->items.count ? count - candidate->items.count : 0;
        return qa_id_index_reserve(&candidate->items, additional, e);
    }
    candidate = source_index_take(store, e);
    if (!candidate) return false;
    if (!qa_id_index_reserve(&candidate->items, count, e)) {
        source_index_dispose(candidate); return false;
    }
    *slot = candidate;
    return true;
}

bool qa_inventory_source_prepare(inventory_store *store, item_group *group,
    size_t count, qa_error *e)
{
    inventory_source_index **slots[] = {
        group ? &group->source_index : &store->primary_index,
        group ? &group->source_candidate : &store->primary_candidate,
    };
    for (size_t i = 0; i < 2; ++i) {
        if (!*slots[i]) {
            *slots[i] = source_index_take(store, e);
            if (!*slots[i]) return false;
        }
        inventory_source_index *index = *slots[i];
        size_t additional = count > index->items.count ? count - index->items.count : 0;
        if (!qa_id_index_reserve(&index->items, additional, e)) return false;
    }
    return true;
}

static void *record_take(qa_inventory *table, qa_pool *pool, size_t pages,
    size_t *slot, qa_error *e)
{
    if (!qa_pool_run_available(pool, pages)) {
        size_t additional = pool->capacity > pages ? pool->capacity : pages;
        if (!qa_pool_extend(pool, &table->record_storage, additional, e)) return NULL;
    }
    return qa_pool_take_run(pool, pages, slot);
}

inventory_store *qa_inventory_store_create(qa_inventory *table, qa_error *e)
{
    size_t slot;
    inventory_store *store = record_take(table, &table->store_records, 1, &slot, e);
    if (!store) return NULL;
    *store = (inventory_store){.references = 1, .storage_owner = table, .storage_slot = slot};
    store->local_items.storage = &store->index_storage;
    store->definitions.storage = &store->index_storage;
    store->pickup_items.storage = &store->index_storage;
    ++table->storage_leases;
    return store;
}

bool qa_inventory_store_reserve_entries(qa_inventory *table, inventory_store *store,
    size_t needed, qa_error *e)
{
    if (needed > store->local_items.count &&
        !qa_id_index_reserve(&store->local_items, needed - store->local_items.count, e)) return false;
    if (needed <= store->capacity) return true;
    if (needed > SIZE_MAX / sizeof(*store->entries))
        return fail(e, QA_ERROR_MEMORY, "Inventory size overflow");
    size_t capacity = store->capacity > SIZE_MAX / 2 ? needed : store->capacity * 2;
    if (capacity < 8) capacity = 8;
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*store->entries)) capacity = needed;
    qa_inventory *owner = store->entries_owner ? store->entries_owner : table;
    qa_pool *pool = &owner->scratch_pages;
    size_t bytes = capacity * sizeof(*store->entries);
    size_t pages = bytes / pool->stride + (bytes % pool->stride != 0);
    size_t slot = store->entries_slot;
    qa_inventory_entry *entries = store->entries_pages ?
        qa_pool_grow_run(pool, &slot, store->entries_pages, pages) : NULL;
    bool relocated = entries == NULL;
    if (relocated) entries = qa_pool_take_run(pool, pages, &slot);
    if (!entries) return fail(e, QA_ERROR_MEMORY, "Cannot grow inventory");
    if (store->count) memmove(entries, store->entries, store->count * sizeof(*entries));
    if (relocated && store->entries_pages)
        qa_pool_release_run(pool, store->entries_slot, store->entries_pages);
    if (!store->entries_owner) { ++owner->storage_leases; store->entries_owner = owner; }
    store->entries = entries; store->entries_slot = slot; store->entries_pages = pages;
    store->capacity = pages * pool->stride / sizeof(*entries);
    return true;
}

static void storage_free(qa_inventory *table)
{
    for (size_t i = 0; i < table->source_records.capacity; ++i)
        qa_arena_destroy(&((inventory_source_index *)qa_pool_at(&table->source_records, i))->storage);
    qa_arena_destroy(&table->scratch_storage);
    qa_arena_destroy(&table->record_storage);
    free(table);
}

static void storage_release(qa_inventory *table)
{ if (--table->storage_leases == 0 && table->closed) storage_free(table); }

static void free_group(item_group *group)
{
    source_index_dispose(group->source_index);
    source_index_dispose(group->source_candidate);
    qa_arena_destroy(&group->index_storage);
    for (size_t i = 0; i < group->count; ++i) free((void *)group->items[i].definition.label);
    free(group->items);
    free(group);
}

void qa_inventory_store_drop(inventory_store *store)
{
    if (--store->references == 0) {
        item_group *group = store->groups;
        while (group) { item_group *next = group->next; free_group(group); group = next; }
        source_index_dispose(store->primary_index);
        source_index_dispose(store->primary_candidate);
        store->primary_index = NULL;
        store->primary_candidate = NULL;
        qa_arena_destroy(&store->index_storage);
        pickup_claim *claim = store->pickups;
        while (claim) {
            pickup_claim *next = claim->next;
            qa_pool_release(&store->storage_owner->pickup_records, claim->storage_slot);
            claim = next;
        }
        qa_inventory *owner = store->entries_owner;
        qa_inventory *storage_owner = store->storage_owner;
        storage_owner->pickup_reservations -= store->pickup_reservation;
        if (owner) qa_pool_release_run(&owner->scratch_pages, store->entries_slot, store->entries_pages);
        qa_pool_release(&storage_owner->store_records, store->storage_slot);
        if (owner) storage_release(owner);
        storage_release(storage_owner);
    } else if (store->references == 1 && store->attached) {
        item_group **link = &store->groups;
        while (*link) {
            item_group *group = *link;
            if (!group->active) { *link = group->next; free_group(group); }
            else link = &group->next;
        }
    }
}

static bool current(qa_inventory *table, inventory_store *store, item_group *group)
{
    return qa_actors_get(table->actors, store->actor) != NULL &&
        table->stores[store->actor.slot] == store && (!group || group->active);
}

static bool require_current(qa_inventory *table, inventory_store *store, item_group *group, qa_error *e)
{ return current(table, store, group) || fail(e, QA_ERROR_NOT_FOUND, "Inventory storage lease retired"); }

static inventory_store *acquire(qa_inventory *table, qa_actor_id actor)
{
    if (!table || actor.slot >= table->capacity || !qa_actors_get(table->actors, actor)) return NULL;
    inventory_store *store = table->stores[actor.slot];
    if (!store || !qa_actor_id_equal(store->actor, actor)) return NULL;
    ++store->references;
    ++table->calls;
    return store;
}

static void release_store(qa_inventory *table, inventory_store *store)
{ qa_inventory_store_drop(store); --table->calls; }

inventory_store *qa_inventory_checkpoint_acquire(qa_inventory *table, qa_actor_id actor)
{ return acquire(table, actor); }
void qa_inventory_checkpoint_release(qa_inventory *table, inventory_store *store)
{ release_store(table, store); }

static item_group *group_for(inventory_store *store, qa_item_id item)
{
    item_group *found = NULL;
    if (!item) return NULL;
    qa_id_index_cursor cursor = qa_id_index_begin(&store->definitions, item, all_owners);
    size_t value;
    while (qa_id_index_next(&store->definitions, &cursor, &value)) {
        item_group *group = (item_group *)(uintptr_t)value;
        if (!group->definitions_only && (!found || group->order < found->order)) found = group;
    }
    return found;
}

typedef struct inventory_definition {
    item_group *group;
    const qa_item_definition *value;
    bool ambiguous;
} inventory_definition;

static inventory_definition definition_lookup(const inventory_store *store, qa_item_id item,
    const qa_actor_owner *owner)
{
    inventory_definition found = {0};
    if (!item) return found;
    item_group *actual = NULL, *declared = NULL;
    qa_id_index_cursor cursor = qa_id_index_begin(&store->definitions, item,
        owner ? *owner : all_owners);
    size_t value, actual_count = 0;
    while (qa_id_index_next(&store->definitions, &cursor, &value)) {
        item_group *group = (item_group *)(uintptr_t)value;
        item_group **first = group->definitions_only ? &declared : &actual;
        if (!*first || group->order < (*first)->order) *first = group;
        actual_count += !group->definitions_only;
    }
    found.group = actual ? actual : declared;
    if (!found.group) return found;
    size_t ordinal;
    if (index_value(&found.group->item_index, item, 0, &ordinal))
        found.value = &found.group->items[ordinal].definition;
    if (owner) {
        size_t preceding_declarations = 0;
        cursor = qa_id_index_begin(&store->definitions, item, *owner);
        while (qa_id_index_next(&store->definitions, &cursor, &value)) {
            item_group *group = (item_group *)(uintptr_t)value;
            preceding_declarations += group->definitions_only && (!actual || group->order < actual->order);
        }
        found.ambiguous = actual_count > 1 || preceding_declarations > 1;
    }
    return found;
}

static item_group *definition_for(inventory_store *store, qa_item_id item)
{
    return definition_lookup(store, item, NULL).group;
}

static bool quantity(double value, qa_error *e)
{ return (isfinite(value) && value >= 0) || fail(e, QA_ERROR_ARGUMENT, "Inventory quantity must be finite and nonnegative"); }

static bool counter(qa_inventory_count_policy policy, double value, double *out, qa_error *e)
{
    if (!isfinite(value)) return fail(e, QA_ERROR_ARGUMENT, "Inventory counter must be finite");
    switch (policy) {
    case QA_COUNT_STACK:
        if (!quantity(value, e)) return false;
        *out = value; return true;
    case QA_COUNT_SOURCE_FLOAT: {
        float rounded = (float)value;
        if (!isfinite(rounded)) return fail(e, QA_ERROR_ARGUMENT, "Inventory counter exceeds float range");
        *out = rounded; return true;
    }
    case QA_COUNT_SOURCE_INT32: {
        if (value <= -2147483649.0 || value >= 2147483648.0)
            return fail(e, QA_ERROR_ARGUMENT, "Inventory counter exceeds its native integer field");
        *out = (int32_t)value;
        return true;
    }
    case QA_COUNT_SOURCE_DOUBLE:
        *out=value; return true;
    }
    return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory count policy");
}

bool qa_inventory_validate_entry(const qa_inventory_entry *entry, qa_inventory_entry *out, qa_error *e)
{
    double count;
    if (!entry || !out || !entry->item) return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory entry");
    if (!quantity(entry->capacity, e) || !counter(entry->policy, entry->count, &count, e)) return false;
    *out = *entry;
    out->count = count;
    return true;
}

bool qa_inventory_preview_give(const qa_inventory_entry *entry, double amount,
    qa_inventory_entry *out, double *given, bool *writes, qa_error *e)
{
    qa_inventory_entry next;
    double delta;
    if (!out || !given || !writes) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory preview output");
    if (!qa_inventory_validate_entry(entry, &next, e) || !quantity(amount, e)) return false;
    double possible = fmin(amount, fmax(0, next.capacity - next.count));
    if (possible == 0) { *out = next; *given = 0; *writes = false; return true; }
    if (!counter(next.policy, possible, &delta, e) || !counter(next.policy, next.count + delta, &delta, e)) return false;
    next.count = delta;
    *given = next.count - entry->count;
    *writes = true;
    *out = next;
    return true;
}

static bool binding_valid(const qa_inventory_binding *binding)
{ return binding && binding->count && binding->at && binding->write; }

static bool binding_count(qa_inventory *table, inventory_store *store, item_group *group,
                          size_t *out, qa_error *e)
{
    if (!require_current(table, store, group, e)) return false;
    if (!group && store->local) { *out = store->count; return true; }
    qa_inventory_binding binding = group ? group->binding : store->primary;
    uint64_t revision = store->revision;
    if (binding.checked_count) {
        if (!binding.checked_count(binding.context, out, e)) return false;
    } else *out = binding.count(binding.context);
    if (!require_current(table, store, group, e)) return false;
    if (store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during count callback");
    if (!group) {
        if (*out > SIZE_MAX - store->count)
            return fail(e, QA_ERROR_FORMAT, "Inventory entry count overflow");
        *out += store->count;
    }
    return true;
}

static bool source_entry(qa_inventory *table, inventory_store *store, item_group *group,
    const qa_inventory_binding *binding, size_t ordinal, qa_inventory_entry *out, qa_error *e)
{
    uint64_t revision = store->revision;
    qa_inventory_entry entry;
    if (!require_current(table, store, group, e) ||
        !binding->at(binding->context, ordinal, &entry, e) ||
        !require_current(table, store, group, e)) return false;
    if (store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during entry callback");
    return qa_inventory_validate_entry(&entry, out, e);
}

static bool binding_at(qa_inventory *table, inventory_store *store, item_group *group,
                       size_t index, qa_inventory_entry *out, qa_error *e)
{
    if (!require_current(table, store, group, e)) return false;
    if (!group && (store->local || index < store->count)) {
        if (index >= store->count) return fail(e, QA_ERROR_NOT_FOUND, "Inventory index out of range");
        return qa_inventory_validate_entry(&store->entries[index], out, e);
    }
    qa_inventory_binding binding = group ? group->binding : store->primary;
    return source_entry(table, store, group, &binding,
        index - (group ? 0 : store->count), out, e);
}

static bool source_capture(qa_inventory *table, inventory_store *store, item_group *group,
    const qa_inventory_binding *binding, inventory_source_index *index, size_t count,
    qa_inventory_entry *entries, qa_item_id selected, qa_inventory_entry *sample,
    bool *sampled, qa_error *e)
{
    *sampled = false;
    if (!qa_id_index_reserve(&index->items, count, e)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_inventory_entry entry;
        if (!source_entry(table, store, group, binding, i, &entry, e)) return false;
        size_t ordinal;
        if (entries && index_value(&index->items, entry.item, 0, &ordinal))
            return fail(e, QA_ERROR_FORMAT, "Duplicate primary inventory item");
        qa_id_index_insert(&index->items, entry.item, 0, i);
        if (entries) entries[i] = entry;
        if (selected && entry.item == selected) {
            if (*sampled) return fail(e, QA_ERROR_ARGUMENT, "Duplicate inventory item in storage");
            *sample = entry; *sampled = true;
        }
    }
    return true;
}

static bool source_revision(qa_inventory *table, inventory_store *store,
    item_group *group, uint64_t *out, qa_error *e)
{
    qa_inventory_binding binding = group ? group->binding : store->primary;
    uint64_t revision = store->revision;
    if (!require_current(table, store, group, e)) return false;
    if (!binding.items_revision(binding.context, out, e)) return false;
    return require_current(table, store, group, e) &&
        (store->revision == revision ||
         fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during topology callback"));
}

static bool source_refresh(qa_inventory *table, inventory_store *store, item_group *group,
    qa_item_id selected, qa_inventory_entry *sample, bool *sampled, qa_error *e)
{
    qa_inventory_binding binding = group ? group->binding : store->primary;
    inventory_source_index *source = group ? group->source_index : store->primary_index;
    uint64_t revision = store->revision, topology = 0;
    *sampled = false;
    if (binding.items_revision) {
        if (!source_revision(table, store, group, &topology, e)) return false;
        source = group ? group->source_index : store->primary_index;
        if (source && source->ready && source->revision == topology) return true;
    }
    size_t count;
    if (!binding_count(table, store, group, &count, e)) return false;
    if (!group) count -= store->count;
    inventory_source_index *candidate = source_candidate_take(store, group, e);
    if (!candidate) return false;
    if (!source_capture(table, store, group, &binding, candidate, count,
        NULL, selected, sample, sampled, e)) goto failed;
    if (store->revision != revision) {
        fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during item lookup"); goto failed;
    }
    if (binding.items_revision) {
        uint64_t completed;
        if (!source_revision(table, store, group, &completed, e)) goto failed;
        if (completed != topology) {
            fail(e, QA_ERROR_NOT_FOUND, "Inventory topology changed during enumeration"); goto failed;
        }
    }
    if (!source_candidate_prepare(store, group, count, e)) goto failed;
    source = group ? group->source_index : store->primary_index;
    if (group) group->source_index = candidate;
    else store->primary_index = candidate;
    candidate->revision = topology; candidate->ready = true;
    source_candidate_return(store, group, source);
    return true;
failed:
    source_candidate_return(store, group, candidate);
    return false;
}

static bool find_in(qa_inventory *table, inventory_store *store, item_group *group,
                    qa_item_id item, qa_inventory_entry *out, bool *found, qa_error *e)
{
    uint64_t revision = store->revision;
    *found = false;
    if (!require_current(table, store, group, e)) return false;
    if (!group) {
        size_t ordinal = local_index(store, item);
        if (ordinal < store->count) { *out = store->entries[ordinal]; *found = true; }
        if (store->local) return true;
    }
    qa_inventory_entry sampled_entry;
    bool sampled;
    if (!source_refresh(table, store, group, item, &sampled_entry, &sampled, e)) return false;
    inventory_source_index *source = group ? group->source_index : store->primary_index;
    qa_id_index_cursor cursor = qa_id_index_begin(&source->items, item, 0);
    size_t ordinal, duplicate;
    if (qa_id_index_next(&source->items, &cursor, &ordinal)) {
        if (*found || qa_id_index_next(&source->items, &cursor, &duplicate))
            return fail(e, QA_ERROR_ARGUMENT, "Duplicate inventory item in storage");
        if (sampled) *out = sampled_entry;
        else if (!binding_at(table, store, group, ordinal + (group ? 0 : store->count), out, e)) return false;
        if (out->item != item)
            return fail(e, QA_ERROR_NOT_FOUND, "Inventory topology changed during item lookup");
        *found = true;
    }
    return require_current(table, store, group, e) &&
        (store->revision == revision || fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during item lookup"));
}

void qa_inventory_admission_abort(qa_inventory_admission *admission)
{
    if (!admission) return;
    qa_inventory *table = admission->table;
    inventory_store *store = admission->store;
    qa_pool_release_run(&table->admission_pages, admission->storage_slot, admission->storage_pages);
    release_store(table, store);
}

bool qa_inventory_admission_validate(qa_inventory_admission *admission, qa_error *e)
{
    if (!admission) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory admission");
    admission->validated = false;
    inventory_store *store = admission->store;
    qa_inventory *table = admission->table;
    if (!require_current(table, store, NULL, e)) return false;
    uint64_t revision = store->revision;
    if (revision == UINT64_MAX)
        return fail(e, QA_ERROR_ARGUMENT, "Inventory admission revision exhausted");
    admission->missing = 0;
    for (size_t i = 0; i < admission->count; ++i) {
        admission_entry *entry = &admission->entries[i];
        entry->owner = group_for(store, entry->initial.item);
        qa_inventory_entry existing;
        bool found;
        if (!find_in(table, store, entry->owner, entry->initial.item, &existing, &found, e))
            return false;
        if (store->revision != revision || group_for(store, entry->initial.item) != entry->owner)
            return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during admission");
        if (entry->owner && !found)
            return fail(e, QA_ERROR_FORMAT, "Item owner omitted an admitted inventory entry");
        entry->missing = !found;
        admission->missing += !found;
    }
    if (admission->missing > SIZE_MAX - store->count)
        return fail(e, QA_ERROR_MEMORY, "Inventory admission size overflow");
    if (!qa_inventory_store_reserve_entries(table, store, store->count + admission->missing, e)) return false;
    admission->revision = revision;
    admission->validated = true;
    return true;
}

bool qa_inventory_prepare_entries(qa_inventory *table, qa_actor_id actor,
                                  const qa_inventory_entry *entries, size_t count,
                                  qa_inventory_admission **out, qa_error *e)
{
    if (!out || (count && !entries) ||
        count > (SIZE_MAX - sizeof(qa_inventory_admission)) / sizeof(admission_entry))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory admission");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Inventory admission actor is unavailable");
    size_t bytes = sizeof(qa_inventory_admission) + count * sizeof(admission_entry);
    size_t pages = bytes / table->admission_pages.stride + (bytes % table->admission_pages.stride != 0);
    size_t slot;
    qa_inventory_admission *admission = record_take(table, &table->admission_pages, pages, &slot, e);
    if (!admission) {
        release_store(table, store);
        return false;
    }
    memset(admission, 0, bytes);
    admission->table = table;
    admission->store = store;
    admission->count = count;
    admission->storage_slot = slot;
    admission->storage_pages = pages;
    for (size_t i = 0; i < count; ++i) {
        if (!qa_inventory_validate_entry(&entries[i], &admission->entries[i].initial, e))
            goto failed;
        for (size_t j = 0; j < i; ++j) {
            if (entries[j].item == entries[i].item) {
                fail(e, QA_ERROR_ARGUMENT, "Duplicate inventory admission item");
                goto failed;
            }
        }
    }
    if (!qa_inventory_admission_validate(admission, e)) goto failed;
    *out = admission;
    return true;
failed:
    qa_inventory_admission_abort(admission);
    return false;
}

bool qa_inventory_admission_commit(qa_inventory_admission *admission, qa_error *e)
{
    if (!admission || !admission->validated)
        return fail(e, QA_ERROR_ARGUMENT, "Inventory admission has not been validated");
    inventory_store *store = admission->store;
    qa_inventory *table = admission->table;
    if (!require_current(table, store, NULL, e)) return false;
    if (store->revision != admission->revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed after admission validation");
    for (size_t i = 0; i < admission->count; ++i) {
        admission_entry *entry = &admission->entries[i];
        if (group_for(store, entry->initial.item) != entry->owner)
            return fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed after validation");
    }
    for (size_t i = 0; i < admission->count; ++i)
        if (admission->entries[i].missing) {
            qa_id_index_insert(&store->local_items, admission->entries[i].initial.item, 0, store->count);
            store->entries[store->count++] = admission->entries[i].initial;
        }
    if (admission->missing) store_changed(store);
    qa_inventory_admission_abort(admission);
    return true;
}

static bool write_entry(qa_inventory *table, inventory_store *store, item_group *group,
                        const qa_inventory_entry *entry, qa_error *e)
{
    if (!require_current(table, store, group, e)) return false;
    if (group_for(store, entry->item) != group) return fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed before its store");
    size_t i = local_index(store, entry->item);
    if (!group && (store->local || i < store->count)) {
        if (i == store->count &&
            (store->count == SIZE_MAX || !qa_inventory_store_reserve_entries(table, store, store->count + 1, e)))
            return false;
        store->entries[i] = *entry;
        if (i == store->count) {
            qa_id_index_insert(&store->local_items, entry->item, 0, i);
            ++store->count; store_changed(store);
        }
        else counts_changed(store);
        return true;
    }
    qa_inventory_binding binding = group ? group->binding : store->primary;
    counts_changed(store);
    if (!binding.write(binding.context, entry, e)) return false;
    return require_current(table, store, group, e);
}

static bool source_acquisition(qa_inventory *table, inventory_store *store, item_group *group,
    const qa_inventory_entry *before, double amount, bool publish,
    qa_inventory_entry *after, bool *handled, bool *writes, qa_error *e)
{
    *handled = false; *writes = false;
    if (!require_current(table, store, group, e)) return false;
    if (group_for(store, before->item) != group)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed before acquisition");
    if (!group && (store->local || local_index(store, before->item) < store->count)) return true;
    qa_inventory_binding binding = group ? group->binding : store->primary;
    if (!binding.acquire) return true;
    uint64_t revision = store->revision;
    bool ok = binding.acquire(binding.context, before, amount, publish, after, handled, writes, e);
    if (publish && *handled && *writes) counts_changed(store);
    if (!ok) return false;
    if (!require_current(table, store, group, e)) return false;
    if (group_for(store, before->item) != group)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed during acquisition");
    if (!publish && store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during acquisition preview");
    if (!*handled) return !*writes || fail(e, QA_ERROR_ARGUMENT, "Unhandled Source acquisition reported writes");
    qa_inventory_entry normalized;
    if (!qa_inventory_validate_entry(after, &normalized, e)) return false;
    if (normalized.item != before->item || normalized.policy != before->policy ||
        normalized.capacity != before->capacity)
        return fail(e, QA_ERROR_ARGUMENT, "Source acquisition changed its admitted entry identity");
    if (!*writes && normalized.count != before->count)
        return fail(e, QA_ERROR_ARGUMENT, "Source acquisition changed a counter without its write receipt");
    *after = normalized;
    if (publish) {
        qa_inventory_entry actual; bool found;
        if (!find_in(table, store, group, before->item, &actual, &found, e)) return false;
        if (!found || actual.count != after->count || actual.capacity != after->capacity ||
            actual.policy != after->policy)
            return fail(e, QA_ERROR_NOT_FOUND, "Source acquisition result differs from its current storage");
    }
    return true;
}

bool qa_inventory_preview_acquire(qa_inventory *table, qa_actor_id actor,
    const qa_inventory_entry *before, double amount, qa_inventory_entry *after,
    double *given, bool *writes, qa_error *e)
{
    qa_inventory_entry normalized;
    if (!after || !given || !writes)
        return fail(e, QA_ERROR_ARGUMENT, "Missing acquisition preview output");
    if (!qa_inventory_validate_entry(before, &normalized, e) || !quantity(amount, e)) return false;
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Acquisition preview requires its actual inventory actor");
    item_group *group = group_for(store, normalized.item);
    qa_inventory_entry actual; bool found, handled = false;
    bool ok = find_in(table, store, group, normalized.item, &actual, &found, e);
    if (ok && (!found || actual.policy != normalized.policy || actual.capacity != normalized.capacity))
        ok = fail(e, QA_ERROR_NOT_FOUND, "Acquisition preview entry differs from its admitted Source");
    if (ok) ok = source_acquisition(table, store, group, &normalized, amount, false, after, &handled, writes, e);
    if (ok && handled) *given = after->count - normalized.count;
    else if (ok) ok = qa_inventory_preview_give(&normalized, amount, after, given, writes, e);
    release_store(table, store);
    return ok;
}

static bool group_validate(qa_inventory *table, inventory_store *store, item_group *group, qa_error *e)
{
    if (group->definitions_only)
        return fail(e, QA_ERROR_ARGUMENT, "Native definitions have no external storage");
    bool sampled;
    if (!source_refresh(table, store, group, 0, NULL, &sampled, e)) return false;
    inventory_source_index *source = group->source_index;
    if (source->validated) return true;
    if (source->items.count != group->count)
        return fail(e, QA_ERROR_ARGUMENT, "Source item storage differs from admitted definitions");
    for (size_t i = 0; i < group->count; ++i) {
        qa_id_index_cursor cursor = qa_id_index_begin(&source->items, group->items[i].definition.item, 0);
        size_t ordinal;
        if (!qa_id_index_next(&source->items, &cursor, &ordinal))
            return fail(e, QA_ERROR_ARGUMENT, "Source item storage omitted an admitted item");
        if (qa_id_index_next(&source->items, &cursor, &ordinal))
            return fail(e, QA_ERROR_ARGUMENT, "Duplicate inventory item in storage");
    }
    source->validated = true;
    return true;
}

bool qa_inventory_store_index_restore(qa_inventory *table, inventory_store *store, qa_error *e)
{
    if (!qa_id_index_reserve(&store->local_items, store->count, e)) return false;
    local_reindex(store);
    size_t definitions = 0;
    for (item_group *group = store->groups; group; group = group->next) {
        if (definitions == SIZE_MAX || group->count > (SIZE_MAX - definitions - 1) / 2)
            return fail(e, QA_ERROR_MEMORY, "Inventory definition index overflows");
        definitions += group->count * 2 + 1;
        if (!group_index_prepare(group, e) || (!group->definitions_only &&
            !qa_inventory_source_prepare(store, group, group->count, e))) return false;
    }
    if (!qa_id_index_reserve(&store->definitions, definitions, e)) return false;
    groups_reindex(store);
    if (!store->local && store->primary.items_revision) {
        bool sampled;
        if (!source_refresh(table, store, NULL, 0, NULL, &sampled, e)) return false;
    }
    for (item_group *group = store->groups; group; group = group->next)
        if (group->active && !group->definitions_only && group->binding.items_revision &&
            !group_validate(table, store, group, e)) return false;
    return true;
}

bool qa_inventory_create(qa_actor_registry *actors, qa_inventory **out, qa_error *e)
{
    if (!actors || !out) return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory service arguments");
    qa_inventory *table = calloc(1, sizeof(*table));
    if (!table) return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory service");
    table->actors = actors; table->capacity = qa_actors_capacity(actors);
    table->stores = calloc(table->capacity, sizeof(*table->stores));
    if (!table->stores && table->capacity) { free(table); return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory slots"); }
    qa_arena_init(&table->record_storage, 0);
    if (!qa_pool_prepare(&table->store_records, &table->record_storage, table->capacity,
            sizeof(inventory_store), _Alignof(inventory_store), e) ||
        !qa_pool_prepare(&table->source_records, &table->record_storage, 1,
            sizeof(inventory_source_index), _Alignof(inventory_source_index), e) ||
        !qa_pool_prepare(&table->pickup_records, &table->record_storage, 0,
            sizeof(pickup_claim), _Alignof(pickup_claim), e) ||
        !qa_pool_prepare(&table->admission_pages, &table->record_storage, 1,
            sizeof(qa_inventory_admission) + sizeof(admission_entry), _Alignof(qa_inventory_admission), e)) {
        qa_arena_destroy(&table->record_storage);
        free(table->stores); free(table); return false;
    }
    memset(qa_pool_at(&table->source_records, 0), 0, sizeof(inventory_source_index));
    if (!qa_pool_reserve_pages(&table->scratch_pages, &table->scratch_storage,
            (size_t)table->capacity + 64, 512, _Alignof(max_align_t), e)) {
        qa_arena_destroy(&table->scratch_storage);
        qa_arena_destroy(&table->record_storage);
        free(table->stores); free(table); return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!qa_operation_create(sizeof(qa_inventory_request), sizeof(qa_inventory_result), &table->operations[i], e)) {
            for (size_t j = 0; j < i; ++j) qa_operation_destroy(table->operations[j], NULL);
            qa_arena_destroy(&table->scratch_storage);
            qa_arena_destroy(&table->record_storage);
            free(table->stores); free(table); return false;
        }
    }
    *out = table; return true;
}

bool qa_inventory_destroy(qa_inventory *table, qa_error *e)
{
    if (!table || table->closed) return true;
    if (table->calls) return fail(e, QA_ERROR_ARGUMENT, "Cannot destroy inventory during a callback");
    for (size_t i = 0; i < 4; ++i)
        if (!qa_operation_destroy_validate(table->operations[i], e)) return false;
    for (size_t i = 0; i < 4; ++i) qa_operation_destroy(table->operations[i], NULL);
    for (uint32_t i = 0; i < table->capacity; ++i) if (table->stores[i]) qa_inventory_store_drop(table->stores[i]);
    free(table->stores); table->stores = NULL; table->closed = true;
    if (!table->storage_leases) storage_free(table);
    return true;
}

void qa_inventory_actor_released(qa_inventory *table, qa_actor_record actor)
{
    if (!table || actor.id.slot >= table->capacity) return;
    inventory_store *store = table->stores[actor.id.slot];
    if (!store || !qa_actor_id_equal(store->actor, actor.id)) return;
    table->stores[actor.id.slot] = NULL;
    store->attached = false;
    qa_inventory_store_drop(store);
}

static bool install(qa_inventory *table, qa_actor_id actor, inventory_store *store, qa_error *e)
{
    if (!table || actor.slot >= table->capacity || !qa_actors_get(table->actors, actor))
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory actor is not current");
    inventory_store *previous = table->stores[actor.slot];
    if (previous) {
        if (qa_actors_get(table->actors, previous->actor)) return fail(e, QA_ERROR_ARGUMENT, "Actor already has inventory storage");
        table->stores[actor.slot] = NULL;
        previous->attached = false;
        qa_inventory_store_drop(previous);
    }
    if (table->serial == UINT64_MAX) return fail(e, QA_ERROR_ARGUMENT, "Inventory lease serial exhausted");
    store->actor = actor; store->serial = ++table->serial; store->references = 1; store->attached = true;
    table->stores[actor.slot] = store;
    return true;
}

bool qa_inventory_bind(qa_inventory *table, qa_actor_id actor, const qa_inventory_binding *binding, qa_error *e)
{
    if (!binding_valid(binding)) return fail(e, QA_ERROR_ARGUMENT, "Incomplete inventory binding");
    if (!table) return fail(e, QA_ERROR_NOT_FOUND, "Inventory actor is not current");
    inventory_store *store = qa_inventory_store_create(table, e);
    if (!store) return false;
    store->primary = *binding;
    if (!install(table, actor, store, e)) { qa_inventory_store_drop(store); return false; }
    if (binding->items_revision) {
        ++store->references; ++table->calls;
        bool sampled;
        bool ok = source_refresh(table, store, NULL, 0, NULL, &sampled, e);
        if (!ok && table->stores[actor.slot] == store) {
            table->stores[actor.slot] = NULL; store->attached = false;
            qa_inventory_store_drop(store);
        }
        release_store(table, store);
        return ok;
    }
    return true;
}

static bool primary_snapshot(qa_inventory *table, inventory_store *store,
                             const qa_inventory_binding *binding,
                             qa_inventory_entry **out, size_t *out_count,
                             inventory_source_index **out_index, qa_error *e)
{
    uint64_t revision = store->revision, topology = 0;
    if (binding->items_revision &&
        (!binding->items_revision(binding->context, &topology, e) ||
         !require_current(table, store, NULL, e))) return false;
    size_t count;
    if (binding->checked_count) {
        if (!binding->checked_count(binding->context, &count, e)) return false;
    } else count = binding->count(binding->context);
    if (!require_current(table, store, NULL, e)) return false;
    if (store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary count callback");
    if (count > SIZE_MAX / sizeof(qa_inventory_entry))
        return fail(e, QA_ERROR_MEMORY, "Primary inventory snapshot size overflow");
    qa_inventory_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) return fail(e, QA_ERROR_MEMORY, "Cannot snapshot primary inventory");
    inventory_source_index *index = source_candidate_take(store, NULL, e);
    if (!index) { free(entries); return false; }
    bool sampled;
    if (!source_capture(table, store, NULL, binding, index, count,
        entries, 0, NULL, &sampled, e)) goto failed;
    if (store->revision != revision) {
        fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary entry callback"); goto failed;
    }
    if (binding->items_revision) {
        uint64_t completed;
        if (!binding->items_revision(binding->context, &completed, e) ||
            !require_current(table, store, NULL, e)) goto failed;
        if (store->revision != revision || completed != topology) {
            fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary entry callback"); goto failed;
        }
    }
    if (!source_candidate_prepare(store, NULL, count, e)) goto failed;
    index->revision = topology; index->ready = true;
    *out = entries; *out_count = count; *out_index = index; return true;
failed:
    source_candidate_return(store, NULL, index);
    free(entries); return false;
}

bool qa_inventory_adopt_primary(qa_inventory *table, qa_actor_id actor,
                                const qa_inventory_binding *binding,
                                qa_inventory_lease *out, qa_error *e)
{
    if (!out || !binding_valid(binding))
        return fail(e, QA_ERROR_ARGUMENT, "Incomplete primary inventory admission");
    if (!qa_inventory_has(table, actor)) {
        if (!qa_inventory_bind(table, actor, binding, e)) return false;
        *out = (qa_inventory_lease){actor, table->stores[actor.slot]->serial}; return true;
    }
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Primary inventory actor retired");
    bool ok = false;
    qa_inventory_entry *source = NULL, *adopted = NULL;
    inventory_source_index *prepared = NULL;
    size_t count = 0, written = 0;
    if (store->references != 2) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory admission requires idle callbacks"); goto done;
    }
    if (!store->local) {
        if (store->primary.context != binding->context || store->primary.count != binding->count ||
            store->primary.at != binding->at || store->primary.write != binding->write ||
            store->primary.mutable_capacity != binding->mutable_capacity ||
            store->primary.checked_count != binding->checked_count ||
            store->primary.acquire != binding->acquire ||
            store->primary.items_revision != binding->items_revision) {
            fail(e, QA_ERROR_ARGUMENT, "Primary inventory already belongs to another source"); goto done;
        }
        *out = (qa_inventory_lease){actor, store->serial}; ok = true; goto done;
    }
    if (!primary_snapshot(table, store, binding, &source, &count, &prepared, e)) goto done;
    if (store->references != 2) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory callback retained an admission lease"); goto done;
    }
    adopted = count ? malloc(count * sizeof(*adopted)) : NULL;
    if (count && !adopted) { fail(e, QA_ERROR_MEMORY, "Cannot prepare primary inventory adoption"); goto done; }
    for (size_t i = 0; i < count; ++i) {
        adopted[i] = source[i];
        size_t local = local_index(store, source[i].item);
        if (local < store->count) {
            adopted[i].count = store->entries[local].count;
            qa_inventory_entry normalized;
            if (!qa_inventory_validate_entry(&adopted[i], &normalized, e)) goto done;
            if (normalized.count != adopted[i].count) {
                fail(e, QA_ERROR_ARGUMENT, "Canonical inventory count cannot be preserved by source representation"); goto done;
            }
            adopted[i] = normalized;
        }
    }
    if (table->serial == UINT64_MAX || store->revision == UINT64_MAX) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory admission identity exhausted"); goto done;
    }
    uint64_t serial = ++table->serial, revision = store->revision;
    for (size_t i = 0; i < count; ++i) {
        written = i + 1;
        if (adopted[i].count != source[i].count &&
            !binding->write(binding->context, &adopted[i], e)) goto rollback;
        if (!require_current(table, store, NULL, e)) goto rollback;
        if (store->revision != revision) {
            fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary adoption write"); goto rollback;
        }
    }
    if (binding->items_revision) {
        uint64_t completed;
        if (!binding->items_revision(binding->context, &completed, e) ||
            !require_current(table, store, NULL, e)) goto rollback;
        if (store->revision != revision || completed != prepared->revision) {
            fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary adoption write"); goto rollback;
        }
    }
    size_t retained = 0;
    for (size_t i = 0; i < store->count; ++i) {
        size_t ordinal;
        if (!index_value(&prepared->items, store->entries[i].item, 0, &ordinal))
            store->entries[retained++] = store->entries[i];
    }
    store->count = retained; local_reindex(store);
    inventory_source_index *previous_index = store->primary_index;
    store->primary_index = prepared; prepared = NULL;
    source_candidate_return(store, NULL, previous_index);
    store->local = false; store->primary = *binding;
    store->serial = serial; store_changed(store);
    *out = (qa_inventory_lease){actor, serial}; ok = true; goto done;
rollback:
    if (current(table, store, NULL) && store->revision == revision) {
        qa_error original = e ? *e : (qa_error){0};
        while (written) {
            size_t i = --written;
            if (adopted[i].count != source[i].count) {
                qa_error rollback_error = {0};
                if (!binding->write(binding->context, &source[i], &rollback_error)) {
                    if (e) *e = rollback_error;
                    goto done;
                }
                if (!current(table, store, NULL) || store->revision != revision) goto done;
            }
        }
        if (e) *e = original;
    }
done:
    source_candidate_return(store, NULL, prepared);
    free(adopted); free(source); release_store(table, store); return ok;
}

bool qa_inventory_primary_current(qa_inventory *table, qa_inventory_lease lease,
                                  const void *context)
{
    if (!table || !lease.serial || lease.actor.slot >= table->capacity ||
        !qa_actors_get(table->actors, lease.actor)) return false;
    const inventory_store *store = table->stores[lease.actor.slot];
    return store && qa_actor_id_equal(store->actor, lease.actor) && !store->local &&
        store->serial == lease.serial && store->primary.context == context;
}

bool qa_inventory_detach_primary(qa_inventory *table, qa_inventory_lease lease,
                                 void *context, qa_error *e)
{
    inventory_store *store = acquire(table, lease.actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Primary inventory detach actor retired");
    bool ok = false;
    qa_inventory_entry *source = NULL;
    inventory_source_index *prepared = NULL;
    size_t count = 0;
    if (store->local || store->serial != lease.serial || store->primary.context != context) {
        fail(e, QA_ERROR_NOT_FOUND, "Primary inventory detach binding changed"); goto done;
    }
    if (store->references != 2 || store->revision == UINT64_MAX || table->serial == UINT64_MAX) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory detach requires idle callbacks and available identity"); goto done;
    }
    if (!primary_snapshot(table, store, &store->primary, &source, &count, &prepared, e)) goto done;
    if (store->references != 2 || table->serial == UINT64_MAX || store->revision == UINT64_MAX) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory changed while preparing detach"); goto done;
    }
    for (size_t i = 0; i < count; ++i) if (local_index(store, source[i].item) < store->count) {
        fail(e, QA_ERROR_FORMAT, "Source primary duplicates native inventory storage"); goto done;
    }
    if (count > SIZE_MAX - store->count) {
        fail(e, QA_ERROR_MEMORY, "Primary inventory detach size overflow"); goto done;
    }
    if (!qa_inventory_store_reserve_entries(table, store, store->count + count, e)) goto done;
    if (count) memcpy(store->entries + store->count, source, count * sizeof(*source));
    store->count += count; local_reindex(store);
    source_index_dispose(store->primary_index);
    store->primary_index = NULL;
    store->local = true; store->primary = (qa_inventory_binding){0};
    store->serial = ++table->serial; store_changed(store); ok = true;
done:
    source_candidate_return(store, NULL, prepared);
    free(source); release_store(table, store); return ok;
}

bool qa_inventory_create_actor(qa_inventory *table, qa_actor_id actor,
                               const qa_inventory_entry *entries, size_t count, qa_error *e)
{
    if (!table || (count && !entries) || count > SIZE_MAX / sizeof(*entries)) return fail(e, QA_ERROR_ARGUMENT, "Invalid initial inventory");
    inventory_store *store = qa_inventory_store_create(table, e);
    if (!store) return false;
    store->local = true;
    if (!qa_inventory_store_reserve_entries(table, store, count, e)) {
        qa_inventory_store_drop(store); return false;
    }
    store->count = count;
    for (size_t i = 0; i < count; ++i) {
        if (!qa_inventory_validate_entry(&entries[i], &store->entries[i], e)) { qa_inventory_store_drop(store); return false; }
        for (size_t j = 0; j < i; ++j) if (entries[j].item == entries[i].item) {
            qa_inventory_store_drop(store); return fail(e, QA_ERROR_ARGUMENT, "Duplicate initial inventory item");
        }
    }
    local_reindex(store);
    if (!install(table, actor, store, e)) { qa_inventory_store_drop(store); return false; }
    return true;
}

bool qa_inventory_bind_items(qa_inventory *table, qa_actor_id actor,
                             const qa_inventory_items *requested, qa_inventory_lease *out, qa_error *e)
{
    if (!requested || !out || !requested->count || !requested->items || !binding_valid(&requested->state) ||
        requested->count > SIZE_MAX / sizeof(qa_item_admission)) return fail(e, QA_ERROR_ARGUMENT, "Invalid source item group");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Source items require primary inventory");
    bool ok = false;
    item_group *group = calloc(1, sizeof(*group));
    if (!group) { fail(e, QA_ERROR_MEMORY, "Cannot allocate source item group"); goto done; }
    group->items = calloc(requested->count, sizeof(*group->items));
    if (!group->items) { fail(e, QA_ERROR_MEMORY, "Cannot allocate source item definitions"); goto failed; }
    group->count = requested->count; group->owner = requested->owner; group->binding = requested->state;
    group->action_context = requested->action_context; group->invoke = requested->invoke; group->active = true;
    for (size_t i = 0; i < group->count; ++i) {
        qa_item_admission admission = requested->items[i];
        qa_item_definition definition = admission.definition;
        qa_inventory_entry entry; bool found;
        if (!definition.item || definition.owner != requested->owner || !definition.label || !*definition.label ||
            (definition.actions & ~(uint32_t)(QA_ITEM_USE | QA_ITEM_DROP)) ||
            (definition.actions && !requested->invoke) || (definition.weapon && (definition.actions & QA_ITEM_USE)) ||
            group_for(store, definition.item)) { fail(e, QA_ERROR_ARGUMENT, "Source item conflicts with owner or actions"); goto failed; }
        for (size_t j = 0; j < i; ++j) if (group->items[j].definition.item == definition.item) {
            fail(e, QA_ERROR_ARGUMENT, "Duplicate source item definition"); goto failed;
        }
        if (pickup_for(store, definition.item)) { fail(e, QA_ERROR_ARGUMENT, "Source item already has a pickup delegate"); goto failed; }
        if (!find_in(table, store, NULL, definition.item, &entry, &found, e)) goto failed;
        if (found != admission.replace_primary) { fail(e, QA_ERROR_ARGUMENT, "Source item admission conflicts with primary inventory"); goto failed; }
        size_t length = strlen(definition.label);
        char *label = malloc(length + 1);
        if (!label) { fail(e, QA_ERROR_MEMORY, "Cannot copy source item label"); goto failed; }
        memcpy(label, definition.label, length + 1);
        admission.definition.label = label; group->items[i] = admission;
    }
    if (!group_index_prepare(group, e) || !group_validate(table, store, group, e)) goto failed;
    for (size_t i = 0; i < group->count; ++i) {
        qa_item_id item = group->items[i].definition.item;
        qa_inventory_entry primary_entry; bool primary_found;
        if (!find_in(table, store, NULL, item, &primary_entry, &primary_found, e)) goto failed;
        if (primary_found != group->items[i].replace_primary) {
            fail(e, QA_ERROR_ARGUMENT, "Primary inventory changed during source item admission"); goto failed;
        }
        if (group_for(store, item)) { fail(e, QA_ERROR_ARGUMENT, "Source item ownership changed during admission"); goto failed; }
        if (pickup_for(store, item)) { fail(e, QA_ERROR_ARGUMENT, "Source item acquired a pickup delegate during admission"); goto failed; }
    }
    if (table->serial == UINT64_MAX) { fail(e, QA_ERROR_ARGUMENT, "Inventory lease serial exhausted"); goto failed; }
    if (group->count > (SIZE_MAX - 1) / 2 ||
        !qa_id_index_reserve(&store->definitions, group->count * 2 + 1, e)) goto failed;
    group->serial = ++table->serial;
    item_group **link = &store->groups;
    while (*link) link = &(*link)->next;
    *link = group;
    groups_reindex(store);
    store_changed(store);
    *out = (qa_inventory_lease){actor, group->serial}; ok = true; goto done;
failed:
    free_group(group);
done:
    release_store(table, store); return ok;
}

static item_group *lease_group(inventory_store *, uint64_t);

bool qa_inventory_replace_definitions(qa_inventory *table, qa_actor_id actor,
    qa_actor_owner owner, const qa_item_definition *definitions, size_t count,
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *), void *context,
    qa_inventory_lease previous, qa_inventory_lease *out, qa_error *e)
{
    if (!out || (count && !definitions) || count > SIZE_MAX / sizeof(qa_item_admission))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid native item definitions");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Native definitions require primary inventory");
    item_group *old = previous.serial ? lease_group(store, previous.serial) : NULL;
    if (previous.serial && (!qa_actor_id_equal(previous.actor, actor) || !old ||
                            !old->definitions_only || old->owner != owner)) {
        release_store(table, store);
        return fail(e, QA_ERROR_ARGUMENT, "Native definition replacement lease is not owned");
    }
    if (!count) {
        if (old) { old->active = false; groups_reindex(store); store_changed(store); }
        *out = (qa_inventory_lease){0};
        release_store(table, store);
        return true;
    }
    item_group *group = calloc(1, sizeof(*group));
    bool ok = false;
    if (!group) { fail(e, QA_ERROR_MEMORY, "Cannot allocate native definitions"); goto done; }
    group->items = calloc(count, sizeof(*group->items));
    if (!group->items) { fail(e, QA_ERROR_MEMORY, "Cannot allocate native item catalog"); goto failed; }
    group->count = count; group->owner = owner; group->definitions_only = true;
    group->action_context = context; group->invoke = invoke;
    for (size_t i = 0; i < count; ++i) {
        const qa_item_definition *definition = &definitions[i];
        if (!definition->item || definition->owner != owner || !definition->label || !*definition->label ||
            (definition->actions & ~(uint32_t)(QA_ITEM_USE | QA_ITEM_DROP)) ||
            (definition->actions && !invoke)) {
            fail(e, QA_ERROR_ARGUMENT, "Native item definition conflicts with owner or actions"); goto failed;
        }
        bool replacing_item = false;
        for (size_t j = 0; old && j < old->count; ++j)
            if (old->items[j].definition.item == definition->item) replacing_item = true;
        for (item_group *other = store->groups; other; other = other->next) {
            if (!other->active || other == old) continue;
            for (size_t j = 0; j < other->count; ++j) {
                if (other->items[j].definition.item == definition->item) {
                    if (replacing_item && !other->definitions_only) continue;
                    fail(e, QA_ERROR_ARGUMENT, "Native item definition already has an owner");
                    goto failed;
                }
            }
        }
        for (size_t j = 0; j < i; ++j) if (definitions[j].item == definition->item) {
            fail(e, QA_ERROR_ARGUMENT, "Duplicate native item definition"); goto failed;
        }
        size_t length = strlen(definition->label);
        char *label = malloc(length + 1);
        if (!label) { fail(e, QA_ERROR_MEMORY, "Cannot copy native item label"); goto failed; }
        memcpy(label, definition->label, length + 1);
        group->items[i].definition = *definition;
        group->items[i].definition.label = label;
    }
    if (table->serial == UINT64_MAX) { fail(e, QA_ERROR_ARGUMENT, "Inventory lease serial exhausted"); goto failed; }
    if (!group_index_prepare(group, e) || group->count > (SIZE_MAX - 1) / 2 ||
        !qa_id_index_reserve(&store->definitions, group->count * 2 + 1, e)) goto failed;
    group->serial = ++table->serial; group->active = true;
    if (old) old->active = false;
    item_group **link = &store->groups;
    while (*link) link = &(*link)->next;
    *link = group;
    groups_reindex(store);
    store_changed(store);
    *out = (qa_inventory_lease){actor, group->serial}; ok = true; goto done;
failed:
    free_group(group);
done:
    release_store(table, store); return ok;
}

bool qa_inventory_bind_definitions(qa_inventory *table, qa_actor_id actor,
    qa_actor_owner owner, const qa_item_definition *definitions, size_t count,
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *), void *context,
    qa_inventory_lease *out, qa_error *e)
{
    if (!count) return fail(e, QA_ERROR_ARGUMENT, "Invalid native item definitions");
    return qa_inventory_replace_definitions(table, actor, owner, definitions, count,
                                            invoke, context, (qa_inventory_lease){0}, out, e);
}

static item_group *lease_group(inventory_store *store, uint64_t serial)
{
    size_t group;
    return index_value(&store->definitions, 0, serial, &group) ?
        (item_group *)(uintptr_t)group : NULL;
}

bool qa_inventory_lease_current(qa_inventory *table, qa_inventory_lease lease)
{
    inventory_store *store = acquire(table, lease.actor);
    if (!store) return false;
    bool found = lease_group(store, lease.serial) != NULL;
    release_store(table, store); return found;
}

bool qa_inventory_close_items(qa_inventory *table, qa_inventory_lease lease, qa_error *e)
{
    (void)e;
    inventory_store *store = acquire(table, lease.actor);
    if (!store) return true;
    item_group *group = lease_group(store, lease.serial);
    if (group) {
        store_changed(store);
        group->active = false;
        groups_reindex(store);
        for (size_t i = 0; !group->definitions_only && i < group->count; ++i) {
            pickup_claim *claim = pickup_for(store, group->items[i].definition.item);
            if (claim) pickup_token_release(store, claim->token);
        }
    }
    release_store(table, store); return true;
}

uint64_t qa_inventory_revision(const qa_inventory *table,qa_actor_id actor)
{
    if (!qa_inventory_has(table,actor)) return 0;
    return table->stores[actor.slot]->revision;
}
uint64_t qa_inventory_definition_revision(const qa_inventory *table, qa_actor_id actor)
{
    return qa_inventory_has(table, actor) ? table->stores[actor.slot]->definition_revision : 0;
}

bool qa_inventory_entries_snapshot(qa_inventory *table, qa_actor_id actor,
    qa_arena *storage, qa_inventory_entry **out, size_t *count,
    bool (*between_reads)(void *, qa_error *), void *context, qa_error *e)
{
    if (!table || !storage || !out || !count)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory snapshot arguments");
    qa_inventory_scratch_acquire(table, storage);
    size_t needed = 0;
    bool ok = qa_inventory_entries(table, actor, NULL, 0, &needed, e);
    if (ok && between_reads) ok = between_reads(context, e);
    qa_inventory_entry *entries = NULL;
    if (ok && needed) {
        if (needed > SIZE_MAX / sizeof(*entries))
            ok = fail(e, QA_ERROR_MEMORY, "Inventory snapshot size overflow");
        else {
            entries = qa_arena_alloc(storage, needed * sizeof(*entries),
                _Alignof(qa_inventory_entry), e);
            ok = entries != NULL;
        }
    }
    size_t written = 0;
    if (ok) ok = qa_inventory_entries(table, actor, entries, needed, &written, e);
    if (ok && written > needed)
        ok = fail(e, QA_ERROR_ARGUMENT, "Inventory output buffer is too small");
    if (!ok) { qa_inventory_snapshot_release(table, storage); return false; }
    *out = entries; *count = written; return true;
}

void qa_inventory_snapshot_release(qa_inventory *table, qa_arena *storage)
{
    qa_arena_destroy(storage);
    storage_release(table);
}

bool qa_inventory_has(const qa_inventory *table, qa_actor_id actor)
{
    return table && actor.slot < table->capacity && qa_actors_get(table->actors, actor) &&
        table->stores[actor.slot] && qa_actor_id_equal(table->stores[actor.slot]->actor, actor);
}

bool qa_inventory_entry_find(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                             qa_inventory_entry *out, bool *found, qa_error *e)
{
    if (!out || !found) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory output");
    *found = false;
    if (!table || actor.slot >= table->capacity || !qa_actors_get(table->actors, actor))
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory actor is not current");
    if (!table->stores[actor.slot]) return true;
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Actor has no current inventory");
    qa_inventory_entry entry;
    item_group *group = group_for(store, item);
    bool ok = find_in(table, store, group, item, &entry, found, e);
    if (ok && group_for(store, item) != group) ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed during read");
    if (ok && *found) *out = entry;
    release_store(table, store); return ok;
}
bool qa_inventory_entry_read(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                             qa_inventory_entry *out, qa_error *e)
{
    bool found;
    return qa_inventory_entry_find(table, actor, item, out, &found, e) &&
        (found || fail(e, QA_ERROR_NOT_FOUND, "Inventory item not admitted"));
}
bool qa_inventory_count_read(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                             double *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory count output");
    qa_inventory_entry entry; bool found;
    if (!qa_inventory_entry_find(table, actor, item, &entry, &found, e)) return false;
    *out = found ? entry.count : 0;
    return true;
}

bool qa_inventory_entries(qa_inventory *table, qa_actor_id actor, qa_inventory_entry *out,
                          size_t capacity, size_t *count, qa_error *e)
{
    if (!count || (capacity && !out)) return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory output buffer");
    inventory_store *store = acquire(table, actor);
    if (!store) { *count = 0; return true; }
    uint64_t revision = store->revision;
    size_t total = 0, primary_count;
    bool ok = binding_count(table, store, NULL, &primary_count, e);
    for (size_t i = 0; ok && i < primary_count; ++i) {
        qa_inventory_entry entry;
        ok = binding_at(table, store, NULL, i, &entry, e);
        if (ok && !group_for(store, entry.item)) { if (total < capacity) out[total] = entry; ++total; }
    }
    for (item_group *g = store->groups; ok && g; g = g->next) if (g->active && !g->definitions_only) {
        ok = group_validate(table, store, g, e);
        for (size_t i = 0; ok && i < g->count; ++i) {
            qa_inventory_entry entry;
            ok = binding_at(table, store, g, i, &entry, e);
            if (ok) { if (total < capacity) out[total] = entry; ++total; }
        }
    }
    if (ok && store->revision != revision)
        ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during enumeration");
    if (ok) { *count = total; if (out && total > capacity) ok = fail(e, QA_ERROR_ARGUMENT, "Inventory output buffer is too small"); }
    release_store(table, store); return ok;
}

bool qa_inventory_mutable_capacity(qa_inventory *table, qa_actor_id actor, qa_item_id item)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return false;
    item_group *group = group_for(store, item);
    qa_inventory_entry entry; bool found;
    bool ok = find_in(table, store, group, item, &entry, &found, NULL) && found;
    if (ok && (group || (!store->local && local_index(store, item) == store->count))) {
        qa_inventory_binding binding = group ? group->binding : store->primary;
        ok = binding.mutable_capacity && binding.mutable_capacity(binding.context, item) && current(table, store, group);
    }
    release_store(table, store); return ok;
}

bool qa_inventory_item_owner(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                             qa_actor_owner *owner, uint64_t *serial)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return false;
    item_group *group = group_for(store, item);
    if (group) { if (owner) *owner = group->owner; if (serial) *serial = group->serial; }
    bool found = group != NULL;
    release_store(table, store); return found;
}

bool qa_inventory_storage_token(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                                uint64_t *serial, qa_error *e)
{
    if (!serial) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory storage token");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Inventory storage retired");
    item_group *group = group_for(store, item);
    qa_inventory_entry entry; bool found;
    bool ok = find_in(table, store, group, item, &entry, &found, e);
    if (ok && group_for(store, item) != group) ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed during storage lookup");
    if (ok && !found) ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory item not admitted");
    if (ok) *serial = group ? group->serial : store->serial;
    release_store(table, store); return ok;
}

bool qa_inventory_item_action(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                              qa_item_action action, qa_error *e)
{
    if (action != QA_ITEM_USE && action != QA_ITEM_DROP) return fail(e, QA_ERROR_ARGUMENT, "Invalid item action");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Item action storage retired");
    inventory_definition definition = definition_lookup(store, item, NULL);
    bool declared = definition.value && (definition.value->actions & action) != 0;
    bool ok = declared ? definition.group->invoke(definition.group->action_context, item, action, e)
                       : fail(e, QA_ERROR_NOT_FOUND, "Item action not declared");
    release_store(table, store); return ok;
}

bool qa_inventory_item_definitions(qa_inventory *table, qa_actor_id actor,
                                   qa_item_definition *out, size_t capacity, size_t *count, qa_error *e)
{
    if (!count || (capacity && !out)) return fail(e, QA_ERROR_ARGUMENT, "Invalid item definition output");
    inventory_store *store = acquire(table, actor);
    size_t total = 0;
    if (store) {
        for (item_group *g = store->groups; g; g = g->next) if (g->active)
            for (size_t i = 0; i < g->count; ++i) {
                if (definition_for(store, g->items[i].definition.item) != g) continue;
                if (total < capacity) out[total] = g->items[i].definition;
                ++total;
            }
        release_store(table, store);
    }
    *count = total;
    return !out || total <= capacity || fail(e, QA_ERROR_ARGUMENT, "Item definition output buffer is too small");
}

bool qa_inventory_item_definition_find(const qa_inventory *table, qa_actor_id actor,
    const qa_actor_owner *owner, qa_item_id item, qa_item_definition *out, bool *present, qa_error *e)
{
    if (!out || !present || !item)
        return fail(e, QA_ERROR_ARGUMENT, "Missing source definition lookup output or item");
    *present = false;
    if (!qa_inventory_has(table, actor)) return true;
    inventory_definition found = definition_lookup(table->stores[actor.slot], item, owner);
    if (owner && found.ambiguous)
        return fail(e, QA_ERROR_FORMAT, "Source definition has ambiguous admitted groups");
    if (found.value) { *out = *found.value; *present = true; }
    return true;
}

bool qa_inventory_source_definition_read(const qa_inventory *table, qa_actor_id actor,
    qa_actor_owner owner, qa_item_id item, qa_item_definition *out, qa_error *e)
{
    if (!out || !item || !qa_inventory_has(table, actor))
        return fail(e, QA_ERROR_NOT_FOUND, "Source definition requires its actual inventory actor");
    bool found;
    return qa_inventory_item_definition_find(table, actor, &owner, item, out, &found, e) &&
        (found || fail(e, QA_ERROR_NOT_FOUND, "Source item has no admitted definition for this owner"));
}

typedef struct inventory_call {
    qa_inventory *table;
    qa_inventory_operation_kind kind;
    qa_actor_id actor;
    qa_item_id item;
    qa_inventory_committed_fn committed;
    void *context;
    bool stored;
} inventory_call;

static bool canonical(void *context, const void *request, void *result, qa_error *e)
{
    inventory_call *call = context;
    const qa_inventory_request *input = request;
    qa_inventory_result *output = result;
    *output = (qa_inventory_result){0};
    if (call->committed && (!qa_actor_id_equal(call->actor, input->actor) || input->entry.item != call->item))
        return fail(e, QA_ERROR_ARGUMENT, "Observed inventory store changed actor or item");
    if (!qa_actors_get(call->table->actors, input->actor)) return fail(e, QA_ERROR_NOT_FOUND, "Inventory actor retired");
    inventory_store *store = acquire(call->table, input->actor);
    bool amount_ok = call->kind == QA_INVENTORY_CONFIGURE || call->kind == QA_INVENTORY_ADJUST || quantity(input->amount, e);
    if (!amount_ok) { if (store) release_store(call->table, store); return false; }
    if (!store) {
        if (call->kind == QA_INVENTORY_GIVE) return true;
        if (call->kind == QA_INVENTORY_CONSUME) { output->consumed = input->amount == 0; return true; }
        return fail(e, QA_ERROR_NOT_FOUND, "Actor has no inventory binding");
    }
    qa_item_id item = call->kind == QA_INVENTORY_CONFIGURE ? input->entry.item : input->item;
    item_group *group = group_for(store, item);
    qa_inventory_entry before, after;
    bool found, ok = find_in(call->table, store, group, item, &before, &found, e);
    if (!ok) goto done;
    if (call->kind == QA_INVENTORY_CONFIGURE) {
        ok = qa_inventory_validate_entry(&input->entry, &after, e) && write_entry(call->table, store, group, &after, e);
        if (ok && call->committed) {
            bool exists;
            ok = group_for(store, item) == group;
            if (!ok) fail(e, QA_ERROR_NOT_FOUND, "Inventory owner changed during observed store");
            if (ok) ok = find_in(call->table, store, group, item, &after, &exists, e);
            if (ok && !exists) ok = fail(e, QA_ERROR_NOT_FOUND, "Observed inventory entry disappeared");
            if (ok) {
                qa_inventory_change change = { .had_before = found, .actor = input->actor, .after = after };
                if (found) change.before = before;
                ok = call->committed(call->context, &change, e);
                if (ok) call->stored = true;
            }
        }
    } else if (!found) {
        if (call->kind == QA_INVENTORY_CONSUME) output->consumed = input->amount == 0;
        if (call->kind == QA_INVENTORY_ADJUST) ok = fail(e, QA_ERROR_ARGUMENT, "Item is not a signed source counter");
    } else if (call->kind == QA_INVENTORY_GIVE) {
        bool writes, handled;
        ok = source_acquisition(call->table, store, group, &before, input->amount, true, &after, &handled, &writes, e);
        if (ok && handled) output->amount = after.count - before.count;
        else if (ok) {
            ok = qa_inventory_preview_give(&before, input->amount, &after, &output->amount, &writes, e);
            if (ok && writes) ok = write_entry(call->table, store, group, &after, e);
        }
    } else if (call->kind == QA_INVENTORY_CONSUME) {
        if (before.count >= input->amount) {
            double delta;
            after = before;
            ok = counter(before.policy, input->amount, &delta, e) && counter(before.policy, before.count - delta, &delta, e);
            if (ok) { after.count = delta; ok = write_entry(call->table, store, group, &after, e); }
            output->consumed = ok;
        }
    } else {
        if (before.policy == QA_COUNT_STACK) ok = fail(e, QA_ERROR_ARGUMENT, "Item is not a signed source counter");
        else {
            double delta;
            after = before;
            ok = counter(before.policy, input->amount, &delta, e) && counter(before.policy, before.count + delta, &delta, e);
            if (ok) { after.count = delta; ok = write_entry(call->table, store, group, &after, e); output->amount = after.count; }
        }
    }
done:
    release_store(call->table, store); return ok;
}

static bool require_stored(void *context, qa_error *e)
{ return ((inventory_call *)context)->stored || fail(e, QA_ERROR_ARGUMENT, "Observed source inventory requires canonical storage before observers"); }

static bool dispatch(inventory_call *call, const qa_inventory_request *request, qa_inventory_result *result, qa_error *e)
{
    if (!call->table || !qa_actors_get(call->table->actors, request->actor)) return fail(e, QA_ERROR_NOT_FOUND, "Inventory actor retired");
    ++call->table->calls;
    bool ok = qa_operation_dispatch(call->table->operations[call->kind], request, result, canonical, call,
        call->committed ? require_stored : NULL, call, e);
    --call->table->calls;
    return ok;
}

bool qa_inventory_give(qa_inventory *table, qa_actor_id actor, qa_item_id item, double amount, double *given, qa_error *e)
{
    if (!given) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory give result");
    inventory_call call = {.table = table, .kind = QA_INVENTORY_GIVE};
    qa_inventory_request request = {.actor = actor, .item = item, .amount = amount};
    qa_inventory_result result;
    if (!dispatch(&call, &request, &result, e)) return false;
    *given = result.amount; return true;
}

bool qa_inventory_consume(qa_inventory *table, qa_actor_id actor, qa_item_id item, double amount, bool *consumed, qa_error *e)
{
    if (!consumed) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory consume result");
    inventory_call call = {.table = table, .kind = QA_INVENTORY_CONSUME};
    qa_inventory_request request = {.actor = actor, .item = item, .amount = amount};
    qa_inventory_result result;
    if (!dispatch(&call, &request, &result, e)) return false;
    *consumed = result.consumed; return true;
}

bool qa_inventory_configure(qa_inventory *table, qa_actor_id actor, const qa_inventory_entry *entry,
    qa_inventory_committed_fn committed, void *context, qa_error *e)
{
    if (!entry) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory entry");
    inventory_call call = {.table = table, .kind = QA_INVENTORY_CONFIGURE, .actor = actor,
        .item = entry->item, .committed = committed, .context = context};
    qa_inventory_request request = {.actor = actor, .item = entry->item, .entry = *entry};
    qa_inventory_result result;
    return dispatch(&call, &request, &result, e);
}

bool qa_inventory_adjust(qa_inventory *table, qa_actor_id actor, qa_item_id item, double delta, double *count, qa_error *e)
{
    if (!count) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory adjustment result");
    inventory_call call = {.table = table, .kind = QA_INVENTORY_ADJUST};
    qa_inventory_request request = {.actor = actor, .item = item, .amount = delta};
    qa_inventory_result result;
    if (!dispatch(&call, &request, &result, e)) return false;
    *count = result.amount; return true;
}

qa_operation *qa_inventory_operation(qa_inventory *table, qa_inventory_operation_kind kind)
{ return table && kind >= QA_INVENTORY_GIVE && kind <= QA_INVENTORY_ADJUST ? table->operations[kind] : NULL; }

static bool entry_equal(qa_inventory_entry a, qa_inventory_entry b)
{ return a.item == b.item && a.count == b.count && a.capacity == b.capacity && a.policy == b.policy; }

typedef struct source_publication {
    qa_inventory *table;
    inventory_store *store;
    item_group *group;
    qa_inventory_entry after;
    bool published;
    bool *retired_token;
} source_publication;

static bool source_current(source_publication *publication, qa_error *e)
{
    if (current(publication->table, publication->store, publication->group)) return true;
    *publication->retired_token = true;
    return fail(e, QA_ERROR_NOT_FOUND, "Inventory storage lease retired");
}

static bool publish_source(void *context, const void *request, void *result, qa_error *e)
{
    source_publication *publication = context;
    const qa_inventory_request *input = request;
    if (!source_current(publication, e)) return false;
    if (!qa_actor_id_equal(input->actor, publication->store->actor) || !entry_equal(input->entry, publication->after))
        return fail(e, QA_ERROR_ARGUMENT, "Committed source inventory publication was changed");
    publication->published = true;
    *(qa_inventory_result *)result = (qa_inventory_result){0};
    return true;
}

static bool source_published(void *context, qa_error *e)
{ return ((source_publication *)context)->published || fail(e, QA_ERROR_ARGUMENT, "Committed source inventory requires publication before observers"); }

bool qa_inventory_source_stored(qa_inventory *table, qa_inventory_lease lease,
    const qa_inventory_change *changes, size_t count, qa_error *e)
{
    bool retired_token;
    return qa_inventory_source_stored_ex(table, lease, changes, count, &retired_token, e);
}

bool qa_inventory_source_stored_ex(qa_inventory *table, qa_inventory_lease lease,
    const qa_inventory_change *changes, size_t count, bool *retired_token, qa_error *e)
{
    if (!retired_token) return fail(e, QA_ERROR_ARGUMENT, "Missing source inventory retirement result");
    *retired_token = false;
    if ((count && !changes) || count > SIZE_MAX / sizeof(*changes)) return fail(e, QA_ERROR_ARGUMENT, "Invalid source inventory changes");
    inventory_store *store = acquire(table, lease.actor);
    if (!store) {
        *retired_token = table != NULL;
        return fail(e, QA_ERROR_NOT_FOUND, "Committed inventory source retired");
    }
    counts_changed(store);
    item_group *group = lease_group(store, lease.serial);
    if (!group) *retired_token = true;
    bool ok = group ? group_validate(table, store, group, e) : fail(e, QA_ERROR_NOT_FOUND, "Committed inventory source retired");
    qa_arena storage;
    qa_inventory_scratch_begin(table, &storage);
    qa_inventory_change *snapshots = ok && count ?
        qa_arena_alloc(&storage, count * sizeof(*snapshots), _Alignof(qa_inventory_change), e) : NULL;
    if (ok && count && !snapshots) ok = fail(e, QA_ERROR_MEMORY, "Cannot snapshot source inventory publication");
    for (size_t i = 0; ok && i < count; ++i) {
        qa_inventory_entry actual; bool found;
        snapshots[i] = changes[i];
        ok = changes[i].had_before && qa_actor_id_equal(changes[i].actor, lease.actor) &&
            qa_inventory_validate_entry(&changes[i].before, &snapshots[i].before, e) &&
            qa_inventory_validate_entry(&changes[i].after, &snapshots[i].after, e);
        if (!ok) { fail(e, QA_ERROR_ARGUMENT, "Invalid committed source inventory change"); break; }
        qa_item_id item = snapshots[i].after.item;
        if (snapshots[i].before.item != item || group_for(store, item) != group) {
            ok = fail(e, QA_ERROR_ARGUMENT, "Committed source inventory changed unowned item"); break;
        }
        for (size_t j = 0; j < i; ++j) if (snapshots[j].after.item == item) ok = false;
        if (!ok) { fail(e, QA_ERROR_ARGUMENT, "Duplicate committed source inventory change"); break; }
        ok = find_in(table, store, group, item, &actual, &found, e);
        if (ok && (!found || !entry_equal(actual, snapshots[i].after))) ok = fail(e, QA_ERROR_ARGUMENT, "Committed inventory differs from source storage");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        source_publication publication = {.table = table, .store = store, .group = group,
            .after = snapshots[i].after, .retired_token = retired_token};
        qa_inventory_request request = {.actor = lease.actor, .item = snapshots[i].after.item, .entry = snapshots[i].after};
        qa_inventory_result result;
        ok = source_current(&publication, e) && qa_operation_dispatch(table->operations[QA_INVENTORY_CONFIGURE],
            &request, &result, publish_source, &publication, source_published, &publication, e) && source_current(&publication, e);
    }
    qa_arena_destroy(&storage); release_store(table, store); return ok;
}

static bool pickup_storage_prepare(inventory_store *store, size_t additional, qa_error *e)
{
    size_t existing = store->pickup_items.count / 2;
    if (additional > SIZE_MAX / 2 - existing)
        return fail(e, QA_ERROR_MEMORY, "Inventory pickup storage overflow");
    size_t required = existing + additional;
    if (!qa_id_index_reserve(&store->pickup_items, additional * 2, e)) return false;
    if (required <= store->pickup_reservation) return true;
    qa_inventory *owner = store->storage_owner;
    size_t increase = required - store->pickup_reservation;
    if (increase > SIZE_MAX - owner->pickup_reservations)
        return fail(e, QA_ERROR_MEMORY, "Inventory pickup storage overflow");
    size_t reservations = owner->pickup_reservations + increase;
    if (reservations > owner->pickup_records.capacity &&
        !qa_pool_extend(&owner->pickup_records, &owner->record_storage,
            reservations - owner->pickup_records.capacity, e)) return false;
    store->pickup_reservation = required;
    owner->pickup_reservations = reservations;
    return true;
}

bool qa_inventory_pickup_prepare(qa_inventory *table, qa_actor_id actor,
    size_t additional, qa_error *e)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Inventory pickup admission actor is unavailable");
    bool ok = pickup_storage_prepare(store, additional, e);
    release_store(table, store);
    return ok;
}

bool qa_inventory_pickup_claim(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                               const void *token, qa_error *e)
{
    inventory_store *store = acquire(table, actor);
    if (!store || !token) {
        if (store) release_store(table, store);
        return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory pickup claim");
    }
    pickup_claim *claim = pickup_for(store, item);
    if (claim) {
        bool ok = claim->token == token || fail(e, QA_ERROR_ARGUMENT, "Inventory item already has a pickup delegate");
        release_store(table, store); return ok;
    }
    bool ok = pickup_storage_prepare(store, 1, e);
    size_t slot;
    if (ok) {
        qa_inventory *owner = store->storage_owner;
        claim = qa_pool_take(&owner->pickup_records, &slot);
        if (!claim) ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory pickup claim");
        else {
            *claim = (pickup_claim){.next = store->pickups, .item = item, .token = token,
                .link = &store->pickups, .storage_slot = slot};
            if (claim->next) claim->next->link = &claim->next;
            store->pickups = claim;
            qa_id_index_insert(&store->pickup_items, item, 0, (size_t)(uintptr_t)claim);
            qa_id_index_insert(&store->pickup_items, 0, (uint64_t)(uintptr_t)token,
                (size_t)(uintptr_t)claim);
        }
    }
    release_store(table, store); return ok;
}

bool qa_inventory_pickup_current(qa_inventory *table, qa_actor_id actor, qa_item_id item, const void *token)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return false;
    pickup_claim *claim = pickup_for(store, item);
    bool found = claim && claim->token == token;
    release_store(table, store); return found;
}

bool qa_inventory_pickup_storage_current(const qa_inventory *table,qa_actor_id actor,
    qa_item_id item,const void *token,uint64_t serial,qa_actor_owner owner)
{
    if (!table || actor.slot >= table->capacity || !qa_actors_get(table->actors, actor)) return false;
    inventory_store *store = table->stores[actor.slot];
    if (!store || !store->attached || !qa_actor_id_equal(store->actor, actor)) return false;
    const pickup_claim *claim = pickup_for(store, item);
    if (!claim || claim->token != token) return false;
    const item_group *group = group_for(store, item);
    return group ? group->serial == serial && group->owner == owner : store->serial == serial;
}

void qa_inventory_pickup_release(qa_inventory *table, qa_actor_id actor, const void *token)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return;
    if (token) pickup_token_release(store, token);
    release_store(table, store);
}

bool qa_inventory_source_items(qa_inventory *table, qa_actor_id actor, qa_arena *storage,
                                qa_inventory_source_snapshot *out, qa_error *e)
{
    if (!out || !storage) return fail(e, QA_ERROR_ARGUMENT, "Source inventory requires caller storage and output");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Source inventory actor retired");
    uint64_t revision = store->revision;
    qa_inventory_source_snapshot result = {.primary_native_count = store->count,
                                           .primary_external = !store->local};
    size_t groups = 0;
    for (item_group *g = store->groups; g; g = g->next) if (g->active) ++groups;
    bool ok = true;
    ok = binding_count(table, store, NULL, &result.primary_count, e);
    if (!ok) goto done;
    if (result.primary_count > SIZE_MAX / sizeof(*result.primary) || groups > SIZE_MAX / sizeof(*result.groups)) {
        ok = fail(e, QA_ERROR_MEMORY, "Source inventory snapshot size overflow"); goto done;
    }
    result.primary = result.primary_count ? qa_arena_alloc(storage,result.primary_count*sizeof(*result.primary),_Alignof(qa_inventory_entry),e) : NULL;
    result.groups = groups ? qa_arena_alloc(storage,groups*sizeof(*result.groups),_Alignof(qa_inventory_source_group),e) : NULL;
    if (result.groups) memset(result.groups,0,groups*sizeof(*result.groups));
    if ((!result.primary && result.primary_count) || (groups && !result.groups)) {
        ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate source inventory snapshot"); goto done;
    }
    for (size_t i = 0; ok && i < result.primary_count; ++i) ok = binding_at(table, store, NULL, i, &result.primary[i], e);
    for (item_group *g = store->groups; ok && g; g = g->next) if (g->active) {
        if (result.group_count >= groups) { ok = fail(e, QA_ERROR_ARGUMENT, "Source inventory changed during snapshot"); break; }
        qa_inventory_source_group *group = &result.groups[result.group_count++];
        group->owner = g->owner;
        group->definitions_only = g->definitions_only;
        group->items = g->count?qa_arena_alloc(storage,g->count*sizeof(*group->items),_Alignof(qa_item_admission),e):NULL;
        if (g->count && !group->items) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy source inventory group"); break; }
        for (size_t i = 0; i < g->count; ++i) {
            char *label = qa_arena_text(storage, g->items[i].definition.label, e);
            if (!label) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy source item label"); break; }
            group->items[i] = g->items[i]; group->items[i].definition.label = label; ++group->count;
        }
    }
    if (ok && result.group_count != groups) ok = fail(e, QA_ERROR_ARGUMENT, "Source inventory changed during snapshot");
done:
    if (ok && (!current(table, store, NULL) || store->revision != revision))
        ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during source snapshot");
    release_store(table, store);
    if (ok) *out = result;
    return ok;
}
