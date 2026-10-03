#include "qa/persistence_gameplay.h"
#include "../gameplay/inventory_internal.h"
#include "lease_serials.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }

static bool entry_fields(qa_source_save_io *io, qa_inventory_entry *entry)
{
    uint32_t policy = entry->policy;
    qa_inventory_entry normalized;
    if (!qa_source_save_string(io, &entry->item) || !entry->item ||
        !qa_source_save_f64(io, &entry->count) || !qa_source_save_f64(io, &entry->capacity) ||
        !qa_source_save_u32(io, &policy) || policy > QA_COUNT_SOURCE_DOUBLE) return false;
    entry->policy = (qa_inventory_count_policy)policy;
    return qa_inventory_validate_entry(entry, &normalized, io->error) &&
        normalized.count == entry->count && normalized.capacity == entry->capacity;
}

static bool entry_equal(qa_inventory_entry a, qa_inventory_entry b)
{ return a.item == b.item && a.count == b.count && a.capacity == b.capacity && a.policy == b.policy; }

static uint32_t mask(const qa_inventory_binding *binding)
{
    return (binding->count ? 1u : 0u) | (binding->at ? 2u : 0u) |
        (binding->write ? 4u : 0u) | (binding->mutable_capacity ? 8u : 0u) |
        (binding->checked_count ? 16u : 0u) | (binding->acquire ? 32u : 0u);
}

static bool binding_count(const qa_inventory_binding *binding, size_t *out, qa_error *error)
{
    if (binding->checked_count) return binding->checked_count(binding->context, out, error);
    if (!binding->count) return fail(error, "Source inventory has no count reader");
    *out = binding->count(binding->context); return true;
}

static bool binding_fields(qa_source_save_io *io, qa_inventory_binding *binding)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t callbacks = mask(binding);
    size_t count = 0;
    if (!reading) {
        if ((callbacks & 7u) != 7u) return fail(io->error, "Incomplete source inventory binding");
        if (!binding_count(binding, &count, io->error)) return false;
    }
    if (!qa_source_save_u32(io, &callbacks) || mask(binding) != callbacks || (callbacks & 7u) != 7u ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(qa_inventory_entry)))
        return fail(io->error, "Restored source inventory callbacks differ");
    if (reading) {
        size_t actual;
        if (!binding_count(binding, &actual, io->error)) return false;
        if (actual != count) return fail(io->error, "Restored source inventory extent differs");
    }
    qa_inventory_entry *seen = count ? calloc(count, sizeof(*seen)) : NULL;
    if (count && !seen) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating inventory validation entries"); return false; }
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_inventory_entry observed = {0};
        ok = binding->at(binding->context, i, &observed, io->error);
        if (ok) seen[i] = observed;
        if (ok) ok = entry_fields(io, seen + i);
        if (ok && reading && !entry_equal(observed, seen[i]))
            ok = fail(io->error, "Restored source inventory value differs from saved authority");
        for (size_t j = 0; ok && j < i; ++j) if (seen[j].item == seen[i].item)
            ok = fail(io->error, "Duplicate source inventory item");
        bool mutable_capacity = binding->mutable_capacity && binding->mutable_capacity(binding->context, seen[i].item);
        bool expected = mutable_capacity;
        if (ok) ok = qa_source_save_bool(io, &expected) && expected == mutable_capacity;
    }
    free(seen);
    return ok;
}

static bool definition_fields(qa_source_save_io *io, qa_item_admission *item, bool definitions_only)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = !reading && item->definition.label != NULL;
    size_t length = present ? strlen(item->definition.label) : 0;
    if (!qa_source_save_string(io, &item->definition.item) || !item->definition.item ||
        !qa_source_save_string(io, &item->definition.ammo) || !qa_source_save_string(io, &item->definition.owner) ||
        !qa_source_save_bool(io, &present) || !present ||
        !qa_source_save_count(io, &length, SIZE_MAX - 1) || !length) return false;
    if (reading) {
        if (io->offset > io->input.size || length > io->input.size - io->offset)
            return fail(io->error, "Truncated inventory label");
        char *label = malloc(length + 1);
        if (!label) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Copying restored inventory label"); return false; }
        item->definition.label = label;
        if (!qa_source_save_bytes(io, label, length)) return false;
        label[length] = 0;
        if (memchr(label, 0, length)) return fail(io->error, "Inventory label contains embedded NUL");
    } else if (!qa_source_save_bytes(io, (void *)item->definition.label, length)) return false;
    return
        qa_source_save_bool(io, &item->definition.weapon) && qa_source_save_u32(io, &item->definition.actions) &&
        !(item->definition.actions & ~(uint32_t)(QA_ITEM_USE | QA_ITEM_DROP)) &&
        !(item->definition.weapon && !definitions_only && (item->definition.actions & QA_ITEM_USE)) &&
        qa_source_save_bool(io, &item->replace_primary) && !(definitions_only && item->replace_primary);
}

static bool definition_equal(const qa_item_admission *a, const qa_item_admission *b)
{
    return a->definition.item == b->definition.item && a->definition.ammo == b->definition.ammo &&
        a->definition.owner == b->definition.owner && a->definition.weapon == b->definition.weapon &&
        a->definition.actions == b->definition.actions && a->replace_primary == b->replace_primary &&
        a->definition.label && b->definition.label && !strcmp(a->definition.label, b->definition.label);
}

static bool group_fields(qa_source_save_io *io, qa_actor_id actor, item_group *group,
                          const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool invoke = group->invoke != NULL;
    if (!qa_source_save_u64(io, &group->serial) || !group->serial ||
        !qa_source_save_string(io, &group->owner) || !qa_source_save_bool(io, &group->definitions_only) ||
        !qa_source_save_bool(io, &invoke) ||
        !qa_source_save_count(io, &group->count, SIZE_MAX / sizeof(*group->items)) || !group->count) return false;
    qa_inventory_items source = {0};
    if (reading) {
        group->items = calloc(group->count, sizeof(*group->items));
        if (!group->items) {
            group->count = 0;
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating restored source inventory definitions"); return false;
        }
        group->active = true;
    }
    for (size_t i = 0; i < group->count; ++i) {
        qa_item_admission value = reading ? (qa_item_admission){0} : group->items[i];
        qa_item_admission *decoded = reading ? group->items + i : &value;
        if (!definition_fields(io, decoded, group->definitions_only) || decoded->definition.owner != group->owner ||
            (decoded->definition.actions && !invoke)) return false;
        value = *decoded;
        for (size_t j = 0; j < i; ++j) if (group->items[j].definition.item == value.definition.item)
            return fail(io->error, "Duplicate inventory group item");
    }
    if (reading) {
        qa_inventory_source_group saved = {group->owner, group->items, group->count, group->definitions_only};
        if (!resolve || !resolve->inventory_group ||
            !resolve->inventory_group(resolve->context, actor, group->serial, &saved, &source, io->error) ||
            source.owner != group->owner || source.count != group->count || !source.items ||
            (source.invoke != NULL) != invoke)
            return fail(io->error, "Saved inventory group has no matching restored source owner");
        for (size_t i = 0; i < group->count; ++i)
            if (!definition_equal(group->items + i, source.items + i))
                return fail(io->error, "Restored inventory item declaration differs");
        group->binding = source.state; group->action_context = source.action_context; group->invoke = source.invoke;
    }
    return group->definitions_only || binding_fields(io, &group->binding);
}

static bool store_fields(qa_source_save_io *io, qa_inventory *table, inventory_store *store,
                          const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_actor(io, &store->actor) || !qa_actors_get(table->actors, store->actor) ||
        !qa_source_save_u64(io, &store->serial) || !store->serial ||
        !qa_source_save_u64(io, &store->revision) || !qa_source_save_bool(io, &store->local) ||
        !qa_source_save_count(io, &store->count, SIZE_MAX / sizeof(*store->entries))) return false;
    if (reading) {
        store->entries = store->count ? calloc(store->count, sizeof(*store->entries)) : NULL;
        if (store->count && !store->entries) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating native inventory counts"); return false; }
        store->capacity = store->count; store->references = 1; store->attached = true;
    }
    for (size_t i = 0; i < store->count; ++i) {
        if (!entry_fields(io, store->entries + i)) return false;
        for (size_t j = 0; j < i; ++j) if (store->entries[j].item == store->entries[i].item)
            return fail(io->error, "Duplicate native inventory item");
    }
    if (!store->local) {
        if (reading && (!resolve || !resolve->inventory_primary ||
            !resolve->inventory_primary(resolve->context, store->actor, store->serial, &store->primary, io->error))) return false;
        if (!binding_fields(io, &store->primary)) return false;
        size_t source_count;
        if (!binding_count(&store->primary, &source_count, io->error)) return false;
        for (size_t i = 0; i < source_count; ++i) {
            qa_inventory_entry entry;
            if (!store->primary.at(store->primary.context, i, &entry, io->error)) return false;
            for (size_t j = 0; j < store->count; ++j) if (entry.item == store->entries[j].item)
                return fail(io->error, "Native inventory storage overlaps source primary");
        }
    }
    size_t count = 0;
    if (!reading) for (item_group *group = store->groups; group; group = group->next) {
        if (!group->active) return fail(io->error, "Inventory group is pending retirement");
        ++count;
    }
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(item_group))) return false;
    item_group **link = &store->groups;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *link = calloc(1, sizeof(**link));
            if (!*link) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating restored inventory group"); return false; }
        }
        if (!group_fields(io, store->actor, *link, resolve)) return false;
        link = &(*link)->next;
    }
    return true;
}

static bool safe(qa_session *session, qa_inventory *table, qa_error *error)
{
    if (!session || !table || table->actors != qa_session_actors(session) || table->calls)
        return fail(error, "Inventory persistence requires idle candidate-owned stores");
    for (size_t i = 0; i < 4; ++i)
        if (!qa_operation_destroy_validate(table->operations[i], error)) return false;
    for (uint32_t i = 0; i < table->capacity; ++i)
        if (table->stores[i] && (!table->stores[i]->attached || table->stores[i]->references != 1))
            return fail(error, "Inventory has a pending admission or detached store");
    return true;
}

static bool signature(qa_source_save_io *io)
{
    unsigned char actual[8] = {'Q','A','I','N','V','E','N','T'};
    static const unsigned char expected[8] = {'Q','A','I','N','V','E','N','T'};
    return qa_source_save_bytes(io, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual));
}

bool qa_persistence_inventory_capture(qa_session *session, qa_inventory *table, qa_buffer *out, qa_error *error)
{
    if (!out || !safe(session, table, error)) return false;
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, session, error)) return false;
    size_t count = 0;
    for (uint32_t i = 0; i < table->capacity; ++i) if (table->stores[i]) ++count;
    uint64_t serial = table->serial;
    bool ok = signature(&io) && qa_source_save_u64(&io, &serial) && qa_source_save_count(&io, &count, table->capacity);
    for (uint32_t i = 0; ok && i < table->capacity; ++i) if (table->stores[i]) {
        inventory_store *held = qa_inventory_checkpoint_acquire(table, table->stores[i]->actor);
        if (!held) { ok = fail(error, "Inventory store retired during capture"); break; }
        inventory_store copy = *held;
        ok = store_fields(&io, table, &copy, NULL) && table->stores[i] == held && held->attached &&
            held->serial == copy.serial && held->revision == copy.revision;
        qa_inventory_checkpoint_release(table, held);
    }
    if (ok) ok = safe(session, table, error) && serial == table->serial && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid inventory continuation");
    qa_source_save_dispose(&io); return ok;
}

bool qa_persistence_inventory_restore(qa_session *session, qa_inventory *table,
    const qa_persistence_gameplay_resolvers *resolve, qa_bytes bytes, qa_error *error)
{
    if (!safe(session, table, error)) return false;
    qa_inventory *scratch = NULL;
    if (!qa_inventory_create(table->actors, &scratch, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error);
    size_t count = 0;
    uint64_t *serials = NULL; size_t serial_count = 0, serial_capacity = 0;
    if (ok) ok = signature(&io) && qa_source_save_u64(&io, &scratch->serial) &&
        qa_source_save_count(&io, &count, table->capacity);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        inventory_store *store = calloc(1, sizeof(*store));
        if (!store) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating restored inventory store"); ok = false; break; }
        /* Install before decoding so every partially owned allocation is
         * reachable by the scratch service's ordinary destructor. */
        store->references = 1;
        size_t record_offset = io.offset;
        ok = qa_source_save_actor(&io, &store->actor);
        if (!ok || !qa_actors_get(table->actors, store->actor) || (i && store->actor.slot <= previous)) {
            free(store); ok = fail(error, "Saved inventory actor ordering is invalid"); break;
        }
        previous = store->actor.slot; scratch->stores[store->actor.slot] = store;
        io.offset = record_offset;
        qa_inventory_hold(table);
        ok = store_fields(&io, table, store, resolve);
        qa_inventory_unhold(table);
        if (ok) ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, store->serial, scratch->serial, error);
        for (item_group *group = store->groups; ok && group; group = group->next)
            ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, group->serial, scratch->serial, error);
    }
    if (ok) ok = persistence_serial_unique(serials, serial_count, error) && qa_source_save_finish(&io, NULL) && safe(session, table, error);
    if (ok) {
        inventory_store **old = table->stores;
        table->stores = scratch->stores; table->serial = scratch->serial;
        scratch->stores = old;
    }
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid saved inventory continuation");
    qa_source_save_dispose(&io);
    free(serials);
    (void)qa_inventory_destroy(scratch, NULL);
    return ok;
}
