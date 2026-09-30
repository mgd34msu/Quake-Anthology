#include "inventory_internal.h"
#include "qa/operation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_inventory_hold(qa_inventory *table) { ++table->calls; }
void qa_inventory_unhold(qa_inventory *table) { --table->calls; }

static bool fail(qa_error *e, qa_status status, const char *message)
{ qa_error_set(e, status, 0, "%s", message); return false; }

static void store_changed(inventory_store *store)
{ if (store->revision != UINT64_MAX) ++store->revision; }

static size_t local_index(const inventory_store *store, qa_item_id item)
{
    for (size_t i = 0; i < store->count; ++i)
        if (store->entries[i].item == item) return i;
    return store->count;
}

static bool reserve_entries(inventory_store *store, size_t needed, qa_error *e)
{
    if (needed <= store->capacity) return true;
    if (needed > SIZE_MAX / sizeof(*store->entries))
        return fail(e, QA_ERROR_MEMORY, "Inventory size overflow");
    size_t capacity = store->capacity > SIZE_MAX / 2 ? needed : store->capacity * 2;
    if (capacity < 8) capacity = 8;
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*store->entries)) capacity = needed;
    qa_inventory_entry *entries = realloc(store->entries, capacity * sizeof(*entries));
    if (!entries) return fail(e, QA_ERROR_MEMORY, "Cannot grow inventory");
    store->entries = entries;
    store->capacity = capacity;
    return true;
}

static void free_group(item_group *group)
{
    for (size_t i = 0; i < group->count; ++i) free((void *)group->items[i].definition.label);
    free(group->items);
    free(group);
}

static void drop_store(inventory_store *store)
{
    if (--store->references == 0) {
        item_group *group = store->groups;
        while (group) { item_group *next = group->next; free_group(group); group = next; }
        pickup_claim *claim = store->pickups;
        while (claim) { pickup_claim *next = claim->next; free(claim); claim = next; }
        free(store->entries);
        free(store);
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
{ drop_store(store); --table->calls; }

inventory_store *qa_inventory_checkpoint_acquire(qa_inventory *table, qa_actor_id actor)
{ return acquire(table, actor); }
void qa_inventory_checkpoint_release(qa_inventory *table, inventory_store *store)
{ release_store(table, store); }

static item_group *group_for(inventory_store *store, qa_item_id item)
{
    for (item_group *g = store->groups; g; g = g->next)
        if (g->active && !g->definitions_only) for (size_t i = 0; i < g->count; ++i)
            if (g->items[i].definition.item == item) return g;
    return NULL;
}

static item_group *definition_for(inventory_store *store, qa_item_id item)
{
    item_group *storage = group_for(store, item);
    if (storage) return storage;
    for (item_group *g = store->groups; g; g = g->next)
        if (g->active && g->definitions_only) for (size_t i = 0; i < g->count; ++i)
            if (g->items[i].definition.item == item) return g;
    return NULL;
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
        double wrapped = fmod(trunc(value), 4294967296.0);
        if (wrapped < 0) wrapped += 4294967296.0;
        *out = wrapped >= 2147483648.0 ? wrapped - 4294967296.0 : wrapped;
        return true;
    }
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
    *out = binding.count(binding.context);
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

static bool binding_at(qa_inventory *table, inventory_store *store, item_group *group,
                       size_t index, qa_inventory_entry *out, qa_error *e)
{
    if (!require_current(table, store, group, e)) return false;
    qa_inventory_entry entry;
    if (!group && (store->local || index < store->count)) {
        if (index >= store->count) return fail(e, QA_ERROR_NOT_FOUND, "Inventory index out of range");
        entry = store->entries[index];
    } else {
        qa_inventory_binding binding = group ? group->binding : store->primary;
        uint64_t revision = store->revision;
        if (!group) index -= store->count;
        if (!binding.at(binding.context, index, &entry, e)) return false;
        if (!require_current(table, store, group, e)) return false;
        if (store->revision != revision)
            return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during entry callback");
    }
    return qa_inventory_validate_entry(&entry, out, e);
}

static bool find_in(qa_inventory *table, inventory_store *store, item_group *group,
                    qa_item_id item, qa_inventory_entry *out, bool *found, qa_error *e)
{
    size_t count;
    uint64_t revision = store->revision;
    *found = false;
    if (!binding_count(table, store, group, &count, e)) return false;
    if (store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during item lookup");
    for (size_t i = 0; i < count; ++i) {
        qa_inventory_entry entry;
        if (!binding_at(table, store, group, i, &entry, e)) return false;
        if (store->revision != revision)
            return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during item lookup");
        if (entry.item == item) {
            if (*found) return fail(e, QA_ERROR_ARGUMENT, "Duplicate inventory item in storage");
            *out = entry; *found = true;
        }
    }
    return true;
}

void qa_inventory_admission_abort(qa_inventory_admission *admission)
{
    if (!admission) return;
    release_store(admission->table, admission->store);
    free(admission);
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
    if (!reserve_entries(store, store->count + admission->missing, e)) return false;
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
    qa_inventory_admission *admission = calloc(1, sizeof(*admission) + count * sizeof(admission_entry));
    if (!admission) {
        release_store(table, store);
        return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory admission");
    }
    admission->table = table;
    admission->store = store;
    admission->count = count;
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
        if (admission->entries[i].missing)
            store->entries[store->count++] = admission->entries[i].initial;
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
            (store->count == SIZE_MAX || !reserve_entries(store, store->count + 1, e)))
            return false;
        store->entries[i] = *entry;
        if (i == store->count) ++store->count;
        store_changed(store);
        return true;
    }
    qa_inventory_binding binding = group ? group->binding : store->primary;
    store_changed(store);
    if (!binding.write(binding.context, entry, e)) return false;
    return require_current(table, store, group, e);
}

static bool group_validate(qa_inventory *table, inventory_store *store, item_group *group, qa_error *e)
{
    if (group->definitions_only)
        return fail(e, QA_ERROR_ARGUMENT, "Native definitions have no external storage");
    size_t count;
    if (!binding_count(table, store, group, &count, e)) return false;
    if (count != group->count) return fail(e, QA_ERROR_ARGUMENT, "Source item storage differs from admitted definitions");
    for (size_t i = 0; i < group->count; ++i) {
        qa_inventory_entry entry; bool found;
        if (!find_in(table, store, group, group->items[i].definition.item, &entry, &found, e)) return false;
        if (!found) return fail(e, QA_ERROR_ARGUMENT, "Source item storage omitted an admitted item");
    }
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
    for (size_t i = 0; i < 4; ++i) {
        if (!qa_operation_create(sizeof(qa_inventory_request), sizeof(qa_inventory_result), &table->operations[i], e)) {
            for (size_t j = 0; j < i; ++j) qa_operation_destroy(table->operations[j], NULL);
            free(table->stores); free(table); return false;
        }
    }
    *out = table; return true;
}

bool qa_inventory_destroy(qa_inventory *table, qa_error *e)
{
    if (!table) return true;
    if (table->calls) return fail(e, QA_ERROR_ARGUMENT, "Cannot destroy inventory during a callback");
    for (size_t i = 0; i < 4; ++i)
        if (!qa_operation_destroy_validate(table->operations[i], e)) return false;
    for (size_t i = 0; i < 4; ++i) qa_operation_destroy(table->operations[i], NULL);
    for (uint32_t i = 0; i < table->capacity; ++i) if (table->stores[i]) drop_store(table->stores[i]);
    free(table->stores); free(table); return true;
}

void qa_inventory_actor_released(qa_inventory *table, qa_actor_record actor)
{
    if (!table || actor.id.slot >= table->capacity) return;
    inventory_store *store = table->stores[actor.id.slot];
    if (!store || !qa_actor_id_equal(store->actor, actor.id)) return;
    table->stores[actor.id.slot] = NULL;
    store->attached = false;
    drop_store(store);
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
        drop_store(previous);
    }
    if (table->serial == UINT64_MAX) return fail(e, QA_ERROR_ARGUMENT, "Inventory lease serial exhausted");
    store->actor = actor; store->serial = ++table->serial; store->references = 1; store->attached = true;
    table->stores[actor.slot] = store;
    return true;
}

bool qa_inventory_bind(qa_inventory *table, qa_actor_id actor, const qa_inventory_binding *binding, qa_error *e)
{
    if (!binding_valid(binding)) return fail(e, QA_ERROR_ARGUMENT, "Incomplete inventory binding");
    inventory_store *store = calloc(1, sizeof(*store));
    if (!store) return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory storage");
    store->primary = *binding;
    if (!install(table, actor, store, e)) { free(store); return false; }
    return true;
}

static bool primary_snapshot(qa_inventory *table, inventory_store *store,
                             const qa_inventory_binding *binding,
                             qa_inventory_entry **out, size_t *out_count, qa_error *e)
{
    uint64_t revision = store->revision;
    size_t count = binding->count(binding->context);
    if (!require_current(table, store, NULL, e)) return false;
    if (store->revision != revision)
        return fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary count callback");
    if (count > SIZE_MAX / sizeof(qa_inventory_entry))
        return fail(e, QA_ERROR_MEMORY, "Primary inventory snapshot size overflow");
    qa_inventory_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) return fail(e, QA_ERROR_MEMORY, "Cannot snapshot primary inventory");
    for (size_t i = 0; i < count; ++i) {
        qa_inventory_entry entry;
        if (!binding->at(binding->context, i, &entry, e) ||
            !require_current(table, store, NULL, e) ||
            !qa_inventory_validate_entry(&entry, &entries[i], e)) goto failed;
        if (store->revision != revision) {
            fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during primary entry callback");
            goto failed;
        }
        for (size_t j = 0; j < i; ++j) if (entries[j].item == entries[i].item) {
            fail(e, QA_ERROR_FORMAT, "Duplicate primary inventory item"); goto failed;
        }
    }
    *out = entries; *out_count = count; return true;
failed:
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
    size_t count = 0, written = 0;
    if (store->references != 2) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory admission requires idle callbacks"); goto done;
    }
    if (!store->local) {
        if (store->primary.context != binding->context || store->primary.count != binding->count ||
            store->primary.at != binding->at || store->primary.write != binding->write ||
            store->primary.mutable_capacity != binding->mutable_capacity) {
            fail(e, QA_ERROR_ARGUMENT, "Primary inventory already belongs to another source"); goto done;
        }
        *out = (qa_inventory_lease){actor, store->serial}; ok = true; goto done;
    }
    if (!primary_snapshot(table, store, binding, &source, &count, e)) goto done;
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
    size_t retained = 0;
    for (size_t i = 0; i < store->count; ++i) {
        bool external = false;
        for (size_t j = 0; j < count; ++j) if (store->entries[i].item == source[j].item) { external = true; break; }
        if (!external) store->entries[retained++] = store->entries[i];
    }
    store->count = retained; store->local = false; store->primary = *binding;
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
    size_t count = 0;
    if (store->local || store->serial != lease.serial || store->primary.context != context) {
        fail(e, QA_ERROR_NOT_FOUND, "Primary inventory detach binding changed"); goto done;
    }
    if (store->references != 2 || store->revision == UINT64_MAX || table->serial == UINT64_MAX) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory detach requires idle callbacks and available identity"); goto done;
    }
    if (!primary_snapshot(table, store, &store->primary, &source, &count, e)) goto done;
    if (store->references != 2 || table->serial == UINT64_MAX || store->revision == UINT64_MAX) {
        fail(e, QA_ERROR_ARGUMENT, "Primary inventory changed while preparing detach"); goto done;
    }
    for (size_t i = 0; i < count; ++i) if (local_index(store, source[i].item) < store->count) {
        fail(e, QA_ERROR_FORMAT, "Source primary duplicates native inventory storage"); goto done;
    }
    if (count > SIZE_MAX - store->count) {
        fail(e, QA_ERROR_MEMORY, "Primary inventory detach size overflow"); goto done;
    }
    if (!reserve_entries(store, store->count + count, e)) goto done;
    if (count) memcpy(store->entries + store->count, source, count * sizeof(*source));
    store->count += count; store->local = true; store->primary = (qa_inventory_binding){0};
    store->serial = ++table->serial; store_changed(store); ok = true;
done:
    free(source); release_store(table, store); return ok;
}

bool qa_inventory_create_actor(qa_inventory *table, qa_actor_id actor,
                               const qa_inventory_entry *entries, size_t count, qa_error *e)
{
    if ((count && !entries) || count > SIZE_MAX / sizeof(*entries)) return fail(e, QA_ERROR_ARGUMENT, "Invalid initial inventory");
    inventory_store *store = calloc(1, sizeof(*store));
    if (!store) return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory storage");
    store->local = true; store->references = 1;
    if (count) {
        store->entries = malloc(count * sizeof(*entries));
        if (!store->entries) { drop_store(store); return fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory entries"); }
    }
    store->capacity = store->count = count;
    for (size_t i = 0; i < count; ++i) {
        if (!qa_inventory_validate_entry(&entries[i], &store->entries[i], e)) { drop_store(store); return false; }
        for (size_t j = 0; j < i; ++j) if (entries[j].item == entries[i].item) {
            drop_store(store); return fail(e, QA_ERROR_ARGUMENT, "Duplicate initial inventory item");
        }
    }
    if (!install(table, actor, store, e)) { drop_store(store); return false; }
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
        for (pickup_claim *claim = store->pickups; claim; claim = claim->next)
            if (claim->item == definition.item) { fail(e, QA_ERROR_ARGUMENT, "Source item already has a pickup delegate"); goto failed; }
        if (!find_in(table, store, NULL, definition.item, &entry, &found, e)) goto failed;
        if (found != admission.replace_primary) { fail(e, QA_ERROR_ARGUMENT, "Source item admission conflicts with primary inventory"); goto failed; }
        size_t length = strlen(definition.label);
        char *label = malloc(length + 1);
        if (!label) { fail(e, QA_ERROR_MEMORY, "Cannot copy source item label"); goto failed; }
        memcpy(label, definition.label, length + 1);
        admission.definition.label = label; group->items[i] = admission;
    }
    if (!group_validate(table, store, group, e)) goto failed;
    for (size_t i = 0; i < group->count; ++i) {
        qa_item_id item = group->items[i].definition.item;
        qa_inventory_entry primary_entry; bool primary_found;
        if (!find_in(table, store, NULL, item, &primary_entry, &primary_found, e)) goto failed;
        if (primary_found != group->items[i].replace_primary) {
            fail(e, QA_ERROR_ARGUMENT, "Primary inventory changed during source item admission"); goto failed;
        }
        if (group_for(store, item)) { fail(e, QA_ERROR_ARGUMENT, "Source item ownership changed during admission"); goto failed; }
        for (pickup_claim *claim = store->pickups; claim; claim = claim->next)
            if (claim->item == item) { fail(e, QA_ERROR_ARGUMENT, "Source item acquired a pickup delegate during admission"); goto failed; }
    }
    if (table->serial == UINT64_MAX) { fail(e, QA_ERROR_ARGUMENT, "Inventory lease serial exhausted"); goto failed; }
    group->serial = ++table->serial;
    item_group **link = &store->groups;
    while (*link) link = &(*link)->next;
    *link = group;
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
        if (old) { old->active = false; store_changed(store); }
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
    group->serial = ++table->serial; group->active = true;
    if (old) old->active = false;
    item_group **link = &store->groups;
    while (*link) link = &(*link)->next;
    *link = group;
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
    for (item_group *group = store->groups; group; group = group->next)
        if (group->active && group->serial == serial) return group;
    return NULL;
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
        for (size_t i = 0; !group->definitions_only && i < group->count; ++i) {
            pickup_claim *claim = store->pickups;
            while (claim && claim->item != group->items[i].definition.item) claim = claim->next;
            if (claim) {
                const void *token = claim->token;
                pickup_claim **link = &store->pickups;
                while (*link) {
                    pickup_claim *candidate = *link;
                    if (candidate->token == token) { *link = candidate->next; free(candidate); }
                    else link = &candidate->next;
                }
            }
        }
    }
    release_store(table, store); return true;
}

bool qa_inventory_has(const qa_inventory *table, qa_actor_id actor)
{
    return table && actor.slot < table->capacity && qa_actors_get(table->actors, actor) &&
        table->stores[actor.slot] && qa_actor_id_equal(table->stores[actor.slot]->actor, actor);
}

bool qa_inventory_entry_read(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                             qa_inventory_entry *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing inventory output");
    inventory_store *store = acquire(table, actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Actor has no current inventory");
    qa_inventory_entry entry; bool found;
    item_group *group = group_for(store, item);
    bool ok = find_in(table, store, group, item, &entry, &found, e);
    if (ok && group_for(store, item) != group) ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory item owner changed during read");
    if (ok && !found) ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory item not admitted");
    if (ok) *out = entry;
    release_store(table, store); return ok;
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
    item_group *group = definition_for(store, item);
    bool declared = false;
    if (group) for (size_t i = 0; i < group->count; ++i)
        if (group->items[i].definition.item == item) declared = (group->items[i].definition.actions & action) != 0;
    bool ok = declared ? group->invoke(group->action_context, item, action, e)
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
        bool writes;
        ok = qa_inventory_preview_give(&before, input->amount, &after, &output->amount, &writes, e);
        if (ok && writes) ok = write_entry(call->table, store, group, &after, e);
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
} source_publication;

static bool publish_source(void *context, const void *request, void *result, qa_error *e)
{
    source_publication *publication = context;
    const qa_inventory_request *input = request;
    if (!require_current(publication->table, publication->store, publication->group, e)) return false;
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
    if ((count && !changes) || count > SIZE_MAX / sizeof(*changes)) return fail(e, QA_ERROR_ARGUMENT, "Invalid source inventory changes");
    inventory_store *store = acquire(table, lease.actor);
    if (!store) return fail(e, QA_ERROR_NOT_FOUND, "Committed inventory source retired");
    store_changed(store);
    item_group *group = lease_group(store, lease.serial);
    bool ok = group ? group_validate(table, store, group, e) : fail(e, QA_ERROR_NOT_FOUND, "Committed inventory source retired");
    qa_inventory_change *snapshots = count ? calloc(count, sizeof(*snapshots)) : NULL;
    if (count && !snapshots) ok = fail(e, QA_ERROR_MEMORY, "Cannot snapshot source inventory publication");
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
        source_publication publication = {table, store, group, snapshots[i].after, false};
        qa_inventory_request request = {.actor = lease.actor, .item = snapshots[i].after.item, .entry = snapshots[i].after};
        qa_inventory_result result;
        ok = require_current(table, store, group, e) && qa_operation_dispatch(table->operations[QA_INVENTORY_CONFIGURE],
            &request, &result, publish_source, &publication, source_published, &publication, e) && require_current(table, store, group, e);
    }
    free(snapshots); release_store(table, store); return ok;
}

bool qa_inventory_pickup_claim(qa_inventory *table, qa_actor_id actor, qa_item_id item,
                               const void *token, qa_error *e)
{
    inventory_store *store = acquire(table, actor);
    if (!store || !token) {
        if (store) release_store(table, store);
        return fail(e, QA_ERROR_ARGUMENT, "Invalid inventory pickup claim");
    }
    bool ok = true;
    for (pickup_claim *claim = store->pickups; claim; claim = claim->next)
        if (claim->item == item) {
            ok = claim->token == token || fail(e, QA_ERROR_ARGUMENT, "Inventory item already has a pickup delegate");
            release_store(table, store); return ok;
        }
    pickup_claim *claim = malloc(sizeof(*claim));
    if (!claim) ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate inventory pickup claim");
    else { *claim = (pickup_claim){store->pickups, item, token}; store->pickups = claim; }
    release_store(table, store); return ok;
}

bool qa_inventory_pickup_current(qa_inventory *table, qa_actor_id actor, qa_item_id item, const void *token)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return false;
    bool found = false;
    for (pickup_claim *claim = store->pickups; claim; claim = claim->next)
        if (claim->item == item && claim->token == token) { found = true; break; }
    release_store(table, store); return found;
}

void qa_inventory_pickup_release(qa_inventory *table, qa_actor_id actor, const void *token)
{
    inventory_store *store = acquire(table, actor);
    if (!store) return;
    pickup_claim **link = &store->pickups;
    while (*link) {
        pickup_claim *claim = *link;
        if (claim->token == token) { *link = claim->next; free(claim); }
        else link = &claim->next;
    }
    release_store(table, store);
}

void qa_inventory_source_snapshot_free(qa_inventory_source_snapshot *snapshot)
{
    if (!snapshot) return;
    for (size_t i = 0; i < snapshot->group_count; ++i) {
        qa_inventory_source_group *group = &snapshot->groups[i];
        for (size_t j = 0; j < group->count; ++j) free((void *)group->items[j].definition.label);
        free(group->items);
    }
    free(snapshot->groups); free(snapshot->primary);
    *snapshot = (qa_inventory_source_snapshot){0};
}

bool qa_inventory_source_items(qa_inventory *table, qa_actor_id actor,
                                qa_inventory_source_snapshot *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing source inventory snapshot output");
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
    result.primary = result.primary_count ? malloc(result.primary_count * sizeof(*result.primary)) : NULL;
    result.groups = groups ? calloc(groups, sizeof(*result.groups)) : NULL;
    if ((!result.primary && result.primary_count) || (groups && !result.groups)) {
        ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate source inventory snapshot"); goto done;
    }
    for (size_t i = 0; ok && i < result.primary_count; ++i) ok = binding_at(table, store, NULL, i, &result.primary[i], e);
    for (item_group *g = store->groups; ok && g; g = g->next) if (g->active) {
        if (result.group_count >= groups) { ok = fail(e, QA_ERROR_ARGUMENT, "Source inventory changed during snapshot"); break; }
        qa_inventory_source_group *group = &result.groups[result.group_count++];
        group->owner = g->owner;
        group->definitions_only = g->definitions_only;
        group->items = calloc(g->count, sizeof(*group->items));
        if (!group->items) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy source inventory group"); break; }
        for (size_t i = 0; i < g->count; ++i) {
            size_t length = strlen(g->items[i].definition.label);
            char *label = malloc(length + 1);
            if (!label) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy source item label"); break; }
            memcpy(label, g->items[i].definition.label, length + 1);
            group->items[i] = g->items[i]; group->items[i].definition.label = label; ++group->count;
        }
    }
    if (ok && result.group_count != groups) ok = fail(e, QA_ERROR_ARGUMENT, "Source inventory changed during snapshot");
done:
    if (ok && (!current(table, store, NULL) || store->revision != revision))
        ok = fail(e, QA_ERROR_NOT_FOUND, "Inventory changed during source snapshot");
    release_store(table, store);
    if (ok) *out = result;
    else qa_inventory_source_snapshot_free(&result);
    return ok;
}
