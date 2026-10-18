#include "pickups_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e, qa_status status, const char *message)
{ qa_error_set(e, status, 0, "%s", message); return false; }

static bool execution_failed(qa_pickup_execution *execution, qa_error *e, const char *message)
{
    if (!execution) return fail(e, QA_ERROR_ARGUMENT, message);
    if (!execution->failed) {
        execution->failed = true;
        qa_error_set(&execution->failure, QA_ERROR_ARGUMENT, 0, "%s", message);
    }
    if (e) *e = execution->failure;
    return false;
}

static bool execution_error(qa_pickup_execution *execution, qa_error *e)
{
    if (!execution->failed) {
        execution->failed = true;
        if (e && e->code != QA_OK) execution->failure = *e;
        else qa_error_set(&execution->failure, QA_ERROR_ARGUMENT, 0, "Pickup callback failed");
    }
    if (e) *e = execution->failure;
    return false;
}

static bool resource_equal(qa_pickup_resource a, qa_pickup_resource b)
{
    return a.kind == b.kind && (a.kind == QA_PICKUP_INVENTORY ? a.item == b.item
        : a.kind == QA_PICKUP_PROTECTION ? a.channel == b.channel : true);
}

static bool resource_valid(qa_pickup_resource resource, bool allow_none)
{
    return (allow_none && resource.kind == QA_PICKUP_NO_RESOURCE) ||
        (resource.kind == QA_PICKUP_INVENTORY && resource.item != 0) ||
        (resource.kind == QA_PICKUP_PROTECTION && (resource.channel == QA_PROTECTION_REGULAR || resource.channel == QA_PROTECTION_POWERED));
}

static pickup_registration_storage *registration_storage(pickup_registration *registration)
{
    return (pickup_registration_storage *)((uint8_t *)registration -
        offsetof(pickup_registration_storage, registration));
}

static bool payload_extent(size_t *bytes, size_t count, size_t element,
    size_t alignment, qa_error *e)
{
    size_t padding = alignment - 1;
    if (count > SIZE_MAX / element || *bytes > SIZE_MAX - padding ||
        count * element > SIZE_MAX - padding - *bytes)
        return fail(e, QA_ERROR_MEMORY, "Pickup rule payload size overflow");
    *bytes += count * element + padding;
    return true;
}

static bool rule_payload_extent(const qa_pickup_rule *rule, size_t *bytes, qa_error *e)
{
    return payload_extent(bytes, rule->offered_count, sizeof(qa_item_id), _Alignof(qa_item_id), e) &&
        payload_extent(bytes, rule->write_count, sizeof(qa_pickup_write), _Alignof(qa_pickup_write), e) &&
        payload_extent(bytes, rule->write_count, sizeof(uint64_t), _Alignof(uint64_t), e);
}

static bool registration_payload_reserve(pickup_registration_storage *row,
    size_t bytes, qa_error *e)
{
    if (bytes <= row->payload_capacity) return true;
    size_t capacity = row->payload_capacity;
    if (capacity && capacity <= SIZE_MAX / 2) capacity *= 2;
    if (capacity < bytes) capacity = bytes;
    if (!qa_arena_reserve(&row->payload, capacity, e)) return false;
    row->payload_capacity = capacity;
    return true;
}

static void registration_release(pickup_registration *registration)
{
    pickup_registration_storage *storage = registration_storage(registration);
    qa_arena_reset(&storage->payload);
    qa_pool_release(&storage->owner->registrations, storage->slot);
}

bool qa_pickups_registration_rules_prepare(pickup_registration *registration,
    size_t count, qa_error *e)
{
    pickup_registration_storage *row = registration_storage(registration);
    qa_arena *storage = &row->payload;
    size_t bytes = 0;
    if (!payload_extent(&bytes, count, sizeof(*registration->rules), _Alignof(pickup_rule), e) ||
        !registration_payload_reserve(row, bytes, e)) { registration->count = 0; return false; }
    registration->rules = qa_arena_alloc(storage,
        count * sizeof(*registration->rules), _Alignof(pickup_rule), e);
    if (!registration->rules) { registration->count = 0; return false; }
    memset(registration->rules, 0, count * sizeof(*registration->rules));
    registration->count = count;
    row->payload_bytes = bytes;
    return true;
}

bool qa_pickups_registration_rule_prepare(pickup_registration *registration,
    pickup_rule *owned, qa_error *e)
{
    pickup_registration_storage *row = registration_storage(registration);
    qa_arena *storage = &row->payload;
    qa_pickup_rule *rule = &owned->rule;
    size_t bytes = row->payload_bytes;
    if (!rule_payload_extent(rule, &bytes, e) || !registration_payload_reserve(row, bytes, e)) return false;
    qa_item_id *offered = qa_arena_alloc(storage,
        rule->offered_count * sizeof(*offered), _Alignof(qa_item_id), e);
    if (!offered) return false;
    qa_pickup_write *writes = qa_arena_alloc(storage,
        rule->write_count * sizeof(*writes), _Alignof(qa_pickup_write), e);
    if (!writes) return false;
    owned->storage = qa_arena_alloc(storage,
        rule->write_count * sizeof(*owned->storage), _Alignof(uint64_t), e);
    if (!owned->storage) return false;
    memset(offered, 0, rule->offered_count * sizeof(*offered));
    memset(writes, 0, rule->write_count * sizeof(*writes));
    memset(owned->storage, 0, rule->write_count * sizeof(*owned->storage));
    rule->offered = offered;
    rule->writes = writes;
    row->payload_bytes = bytes;
    return true;
}

static pickup_registration *registration_find(const qa_pickups *service, qa_pickup_lease lease)
{
    if (!service || !lease.serial) return NULL;
    const qa_id_index *index = &service->observation_storage->registration_index;
    qa_id_index_cursor cursor = qa_id_index_begin(index, 0, lease.serial);
    size_t value;
    if (!qa_id_index_next(index, &cursor, &value)) return NULL;
    pickup_registration *registration = (pickup_registration *)(uintptr_t)value;
    return qa_actor_id_equal(registration->actor, lease.actor) ? registration : NULL;
}

static pickup_registration *registration_actor_find(const qa_pickups *service, qa_actor_id actor)
{
    size_t value;
    return qa_actor_index_find_retained(&service->observation_storage->registration_actors,
        actor, &value) ? (pickup_registration *)(uintptr_t)value : NULL;
}

bool qa_pickups_registration_index_reserve(qa_pickups *service,
    pickup_registration *registration, qa_error *e)
{
    size_t entries = 1;
    for (size_t i = 0; i < registration->count; ++i) {
        size_t offered = registration->rules[i].rule.offered_count;
        if (offered > SIZE_MAX - entries) { entries = SIZE_MAX; break; }
        entries += offered;
    }
    pickup_observation_storage *storage = service->observation_storage;
    if (!qa_id_index_reserve(&storage->registration_index, entries, e)) return false;
    bool ok = registration_actor_find(service, registration->actor) ||
        qa_id_index_reserve(&storage->registration_actors.retained, 1, e);
    if (ok) qa_arena_seal(&registration_storage(registration)->payload);
    return ok;
}

void qa_pickups_registration_index_insert(qa_pickups *service, pickup_registration *registration)
{
    pickup_registration *head = registration_actor_find(service, registration->actor);
    registration->actor_next = NULL;
    registration->actor_tail = registration;
    if (!head || registration->serial > head->serial) {
        registration->actor_next = head;
        if (head) registration->actor_tail = head->actor_tail;
        /* Admission reserves new heads; drained rebuilds reuse that capacity. */
        (void)qa_actor_index_remember(&service->observation_storage->registration_actors,
            registration->actor, (size_t)(uintptr_t)registration, NULL);
    } else {
        head->actor_tail->actor_next = registration;
        head->actor_tail = registration;
    }
    qa_id_index *index = &service->observation_storage->registration_index;
    qa_id_index_insert(index, 0, registration->serial, (size_t)(uintptr_t)registration);
    for (size_t i = registration->count; i; --i) {
        pickup_rule *rule = &registration->rules[i - 1];
        size_t value = (size_t)(uintptr_t)rule;
        for (size_t j = 0; j < rule->rule.offered_count; ++j) {
            qa_item_id item = rule->rule.offered[j];
            const qa_id_index_entry *previous = qa_id_index_find(index, item, registration->serial);
            if (!previous || previous->value != value)
                qa_id_index_insert(index, item, registration->serial, value);
        }
    }
}

static void sweep(qa_pickups *service)
{
    if (service->calls) return;
    bool reindex = false;
    pickup_registration **link = &service->registrations;
    while (*link) {
        pickup_registration *registration = *link;
        if (!registration->active) {
            *link = registration->next;
            if (!reindex) {
                qa_id_index_clear(&service->observation_storage->registration_index);
                qa_actor_index_clear(&service->observation_storage->registration_actors);
                reindex = true;
            }
            qa_inventory_pickup_release(service->inventory, registration->actor, registration);
            registration_release(registration);
        } else link = &registration->next;
    }
    if (reindex) for (pickup_registration *registration = service->registrations;
            registration; registration = registration->next)
        qa_pickups_registration_index_insert(service, registration);
    while (service->retired_observations) {
        pickup_observation_owner *retired=service->retired_observations;
        service->retired_observations=retired->retired_next;
        qa_pool_release(&service->observation_storage->records, retired->slot);
    }
}

static bool write_current(qa_pickups *service, pickup_registration *registration,
                           const qa_pickup_write *write, uint64_t serial)
{
    qa_actor_owner owner; uint64_t current_serial;
    if (!registration->active || !qa_actors_get(service->actors, registration->actor)) return false;
    if (write->resource.kind == QA_PICKUP_PROTECTION)
        return qa_combat_protection_owner(service->combat, registration->actor, write->resource.channel, &owner, &current_serial)
            && owner == registration->owner && current_serial == serial &&
            qa_combat_protection_bound(service->combat, (qa_protection_lease){registration->actor, serial, write->resource.channel});
    if (!qa_inventory_pickup_current(service->inventory, registration->actor, write->resource.item, registration) ||
        !qa_inventory_storage_token(service->inventory, registration->actor, write->resource.item, &current_serial, NULL) || current_serial != serial) return false;
    return write->fields == QA_PICKUP_COUNT || qa_inventory_mutable_capacity(service->inventory, registration->actor, write->resource.item);
}

static bool registration_current(qa_pickups *service, pickup_registration *registration)
{
    if (!registration->active || !qa_actors_get(service->actors, registration->actor)) return false;
    for (size_t i = 0; i < registration->count; ++i)
        for (size_t j = 0; j < registration->rules[i].rule.write_count; ++j)
            if (!write_current(service, registration, &registration->rules[i].rule.writes[j], registration->rules[i].storage[j])) return false;
    return registration->active && qa_actors_get(service->actors, registration->actor) != NULL;
}

static bool write_retained(qa_pickups *service,const pickup_registration *registration,
                           const qa_pickup_write *write,uint64_t serial)
{
    if (!registration->active || !qa_actors_get(service->actors, registration->actor)) return false;
    if (write->resource.kind == QA_PICKUP_INVENTORY)
        return qa_inventory_pickup_storage_current(service->inventory, registration->actor,
            write->resource.item, registration, serial);
    qa_actor_owner owner; uint64_t current_serial;
    return qa_combat_protection_owner(service->combat, registration->actor, write->resource.channel,
        &owner, &current_serial) && owner == registration->owner && current_serial == serial &&
        qa_combat_protection_bound(service->combat,
            (qa_protection_lease){registration->actor, serial, write->resource.channel});
}

static bool rule_retained(qa_pickups *service,const pickup_registration *registration,
                          const pickup_rule *rule)
{
    bool inventory = false;
    for (size_t i = 0; i < rule->rule.write_count; ++i) {
        if (!write_retained(service, registration, rule->rule.writes + i, rule->storage[i])) return false;
        inventory |= rule->rule.writes[i].resource.kind == QA_PICKUP_INVENTORY;
    }
    if (inventory) for (size_t i = 0; i < registration->count; ++i)
        for (size_t j = 0; j < registration->rules[i].rule.write_count; ++j) {
            const qa_pickup_write *write = registration->rules[i].rule.writes + j;
            if (write->resource.kind == QA_PICKUP_INVENTORY &&
                !write_retained(service, registration, write, registration->rules[i].storage[j])) return false;
        }
    return registration->active && qa_actors_get(service->actors, registration->actor) != NULL;
}

bool qa_pickups_checkpoint_write_current(qa_pickups *service, pickup_registration *registration,
                                         const qa_pickup_write *write, uint64_t serial)
{ return write_current(service, registration, write, serial); }

static bool write_bound(qa_pickups *service, pickup_registration *registration,
                         const qa_pickup_write *write, uint64_t serial)
{
    if (!registration->active || !qa_actors_get(service->actors, registration->actor)) return false;
    if (write->resource.kind == QA_PICKUP_INVENTORY)
        return qa_inventory_pickup_current(service->inventory, registration->actor, write->resource.item, registration);
    qa_actor_owner owner; uint64_t current_serial;
    return qa_combat_protection_owner(service->combat, registration->actor, write->resource.channel, &owner, &current_serial) &&
        owner == registration->owner && current_serial == serial;
}

static bool rule_current(qa_pickups *service, pickup_registration *registration, pickup_rule *rule)
{
    bool inventory = false;
    for (size_t i = 0; i < rule->rule.write_count; ++i) {
        if (!write_current(service, registration, &rule->rule.writes[i], rule->storage[i])) return false;
        inventory |= rule->rule.writes[i].resource.kind == QA_PICKUP_INVENTORY;
    }
    if (inventory) for (size_t i = 0; i < registration->count; ++i)
        for (size_t j = 0; j < registration->rules[i].rule.write_count; ++j) {
            const qa_pickup_write *write = &registration->rules[i].rule.writes[j];
            if (write->resource.kind == QA_PICKUP_INVENTORY &&
                !write_current(service, registration, write, registration->rules[i].storage[j])) return false;
        }
    return registration->active && qa_actors_get(service->actors, registration->actor) != NULL;
}

bool qa_pickups_create(qa_actor_registry *actors, qa_combat *combat, qa_inventory *inventory,
                       qa_pickups **out, qa_error *e)
{
    if (!actors || !combat || !inventory || !out) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup service arguments");
    qa_pickups *service = calloc(1, sizeof(*service));
    if (!service) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup service");
    service->actors = actors; service->combat = combat; service->inventory = inventory;
    service->observation_storage = calloc(1, sizeof(*service->observation_storage));
    if (!service->observation_storage) {
        free(service);
        return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup observation storage");
    }
    pickup_observation_storage *storage = service->observation_storage;
    qa_arena_init(&storage->arena, 0);
    storage->registration_index.storage = &storage->arena;
    if (!qa_actor_index_create(&storage->registration_actors, actors, &storage->arena, e)) {
        (void)qa_pickups_destroy(service, NULL); return false;
    }
    size_t capacity = qa_actors_capacity(actors);
    if (capacity > SIZE_MAX / sizeof(*service->observations)) {
        (void)qa_pickups_destroy(service, NULL);
        return fail(e, QA_ERROR_MEMORY, "pickup observation table overflow");
    }
    if (capacity) {
        service->observations = qa_arena_alloc(&storage->arena,
            capacity * sizeof(*service->observations), _Alignof(pickup_observation_owner *), e);
        if (!service->observations) { (void)qa_pickups_destroy(service, NULL); return false; }
        memset(service->observations, 0, capacity * sizeof(*service->observations));
    }
    service->observation_capacity = (uint32_t)capacity;
    if (!qa_pool_prepare(&storage->records, &storage->arena, capacity,
            sizeof(pickup_observation_owner), _Alignof(pickup_observation_owner), e)) {
        (void)qa_pickups_destroy(service, NULL); return false;
    }
    if (!qa_pool_prepare(&storage->registrations, &storage->arena, capacity,
            sizeof(pickup_registration_storage), _Alignof(pickup_registration_storage), e)) {
        (void)qa_pickups_destroy(service, NULL); return false;
    }
    for (size_t i = 0; i < storage->registrations.capacity; ++i) {
        pickup_registration_storage *row = qa_pool_at(&storage->registrations, i);
        memset(row, 0, sizeof(*row));
        qa_arena_init(&row->payload, sizeof(pickup_rule));
    }
    *out = service; return true;
}

bool qa_pickups_idle(const qa_pickups *service) { return service && service->calls == 0 && !service->scopes; }

bool qa_pickups_set_eligibility(qa_pickups *service,
    bool (*eligible)(void *, const qa_pickup_offer *, bool *, qa_error *), void *context, qa_error *e)
{
    if (!qa_pickups_idle(service)) return fail(e, QA_ERROR_ARGUMENT, "Cannot change pickup eligibility during execution");
    service->eligible = eligible; service->eligibility_context = context;
    return true;
}

bool qa_pickups_destroy(qa_pickups *service, qa_error *e)
{
    if (!service) return true;
    if (!qa_pickups_idle(service)) return fail(e, QA_ERROR_ARGUMENT, "Cannot destroy pickup service during callbacks");
    for (pickup_registration *r = service->registrations; r; r = r->next) r->active = false;
    sweep(service);
    for(uint32_t i=0;i<service->observation_capacity;++i)
        if (service->observations[i])
            qa_pool_release(&service->observation_storage->records, service->observations[i]->slot);
    qa_id_index_destroy(&service->observation_storage->registration_index);
    qa_actor_index_destroy(&service->observation_storage->registration_actors);
    for (size_t i = 0; i < service->observation_storage->registrations.capacity; ++i) {
        pickup_registration_storage *row = qa_pool_at(&service->observation_storage->registrations, i);
        qa_arena_destroy(&row->payload);
    }
    qa_arena_destroy(&service->observation_storage->arena);
    free(service->observation_storage);free(service);return true;
}

pickup_observation_owner *qa_pickups_observation_take(qa_pickups *service, qa_error *e)
{
    pickup_observation_storage *storage = service->observation_storage;
    if (storage->records.active == storage->records.capacity &&
        !qa_pool_extend(&storage->records, &storage->arena,
            storage->records.capacity ? storage->records.capacity : 1, e)) return NULL;
    size_t slot;
    pickup_observation_owner *owner = qa_pool_take(&storage->records, &slot);
    *owner = (pickup_observation_owner){.slot = slot};
    return owner;
}

pickup_registration *qa_pickups_registration_take(qa_pickups *service, qa_error *e)
{
    pickup_observation_storage *storage = service->observation_storage;
    qa_pool *pool = &storage->registrations;
    if (pool->active == pool->capacity) {
        size_t previous = pool->capacity;
        if (!qa_pool_extend(pool, &storage->arena, previous ? previous : 1, e)) return NULL;
        for (size_t i = previous; i < pool->capacity; ++i) {
            pickup_registration_storage *row = qa_pool_at(pool, i);
            memset(row, 0, sizeof(*row));
            qa_arena_init(&row->payload, sizeof(pickup_rule));
        }
    }
    size_t slot;
    pickup_registration_storage *row = qa_pool_take(pool, &slot);
    row->owner = storage;
    row->slot = slot;
    row->payload_bytes = 0;
    row->payload.sealed = false;
    row->registration = (pickup_registration){0};
    return &row->registration;
}

static bool cargo_valid(const qa_pickup_cargo *, size_t, qa_error *);

static pickup_observation_owner *observation_get(const qa_pickups *service,qa_actor_id actor) {
    if(actor.slot>=service->observation_capacity) return NULL;
    pickup_observation_owner *owner=service->observations[actor.slot];
    return owner && owner->active && qa_actor_id_equal(owner->actor,actor)?owner:NULL;
}
static void observation_retire(qa_pickups *service,pickup_observation_owner *owner) {
    if(!owner || !owner->active) return;
    owner->active=false;
    if(service->observations[owner->actor.slot]==owner) service->observations[owner->actor.slot]=NULL;
    owner->retired_next=service->retired_observations;service->retired_observations=owner;
}
bool qa_pickups_observe(qa_pickups *service,qa_actor_id actor,qa_actor_owner owner,
                         const qa_pickup_observer *observer,qa_pickup_lease *out,qa_error *e) {
    if(!service || !owner || !observer || !observer->inspect || !out ||
       !qa_actors_get(service->actors,actor)) return fail(e,QA_ERROR_ARGUMENT,"invalid pickup observation binding");
    if(service->serial==UINT64_MAX) return fail(e,QA_ERROR_ARGUMENT,"pickup observation identity exhausted");
    pickup_observation_owner *previous=actor.slot<service->observation_capacity?service->observations[actor.slot]:NULL;
    bool same_actor=previous && qa_actor_id_equal(previous->actor,actor);
    if(same_actor && previous->observer.inspect && previous->owner!=owner)
        return fail(e,QA_ERROR_ARGUMENT,"pickup observation belongs to another source");
    qa_pickup_cargo cargo=same_actor?previous->cargo:(qa_pickup_cargo){0};
    bool has_cargo=same_actor && previous->has_cargo;
    pickup_observation_owner *binding=previous && !service->calls ? previous :
        qa_pickups_observation_take(service,e);
    if(!binding) return false;
    if(binding!=previous) observation_retire(service,previous);
    *binding=(pickup_observation_owner){.slot=binding->slot,.actor=actor,.owner=owner,
        .serial=++service->serial,.observer=*observer,.cargo=cargo,.active=true,.has_cargo=has_cargo};
    service->observations[actor.slot]=binding;
    *out=(qa_pickup_lease){actor,binding->serial};sweep(service);return true;
}
bool qa_pickups_observation_close(qa_pickups *service,qa_pickup_lease lease,qa_error *e) {
    if(!service) return fail(e,QA_ERROR_ARGUMENT,"missing pickup observation service");
    pickup_observation_owner *owner=observation_get(service,lease.actor);
    if(owner && owner->observer.inspect && owner->serial==lease.serial) {
        owner->observer=(qa_pickup_observer){0};owner->serial=0;
        if(!owner->has_cargo) observation_retire(service,owner);
    }
    sweep(service);return true;
}
bool qa_pickups_observation_current(qa_pickups *service,qa_pickup_lease lease) {
    if(!service || !qa_actors_get(service->actors,lease.actor)) return false;
    pickup_observation_owner *owner=observation_get(service,lease.actor);
    return owner && owner->observer.inspect && owner->serial==lease.serial;
}
bool qa_pickups_cargo_set(qa_pickups *service,qa_actor_id actor,const qa_pickup_cargo *value,qa_error *e) {
    if(!service) return fail(e,QA_ERROR_ARGUMENT,"missing pickup cargo service");
    pickup_observation_owner *owner=observation_get(service,actor);
    if(!value) {
        if(owner) {
            owner->cargo=(qa_pickup_cargo){0};owner->has_cargo=false;
            if(!owner->observer.inspect) observation_retire(service,owner);
        }
        sweep(service);return true;
    }
    qa_pickup_cargo cargo=*value;
    const qa_actor_record *record=qa_actors_get(service->actors,actor);
    if(!record || !cargo_valid(&cargo,1,e) || cargo.count<=0)
        return fail(e,QA_ERROR_ARGUMENT,"invalid retained pickup cargo");
    if(!owner) {
        pickup_observation_owner *previous=service->observations[actor.slot];
        if(previous && !service->calls) owner=previous;
        else {
            qa_pool *pool=&service->observation_storage->records;
            if(pool->active==pool->capacity)
                return fail(e,QA_ERROR_MEMORY,"pickup cargo storage is full");
            owner=qa_pickups_observation_take(service,e);
            if(!owner) return false;
        }
        if(owner!=previous) observation_retire(service,previous);
        *owner=(pickup_observation_owner){.slot=owner->slot,.actor=actor,.owner=record->owner,.active=true};
        service->observations[actor.slot]=owner;
    }
    owner->cargo=cargo;owner->has_cargo=true;
    sweep(service);return true;
}
bool qa_pickups_cargo_read(const qa_pickups *service,qa_actor_id actor,qa_pickup_cargo *out) {
    if(!service || !out || !qa_actors_get(service->actors,actor)) return false;
    pickup_observation_owner *owner=observation_get(service,actor);
    if(!owner || !owner->has_cargo) return false;
    *out=owner->cargo;return true;
}
static bool observation_current(qa_pickups *service,pickup_observation_owner *owner,qa_actor_id recipient) {
    return owner->active && owner->observer.inspect && observation_get(service,owner->actor)==owner &&
        qa_actors_get(service->actors,owner->actor) && qa_actors_get(service->actors,recipient);
}
bool qa_pickups_inspect(qa_pickups *service,qa_actor_id pickup,qa_actor_id recipient,
                         qa_pickup_observation *out,bool *found,qa_error *e) {
    if(!service || !out || !found || !qa_actors_get(service->actors,recipient))
        return fail(e,QA_ERROR_ARGUMENT,"invalid pickup observation request");
    *found=false;
    pickup_observation_owner *owner=observation_get(service,pickup);
    if(!owner || !owner->observer.inspect || !qa_actors_get(service->actors,pickup)) return true;
    if(service->calls==SIZE_MAX || service->observation_calls==SIZE_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"pickup observation nesting exhausted");
    ++service->calls;++service->observation_calls;
    qa_pickup_offer offer={0};bool available=false;float utility=0;
    bool ok=owner->observer.inspect(owner->observer.context,pickup,recipient,&offer,&utility,&available,e);
    if(ok && observation_current(service,owner,recipient) && available) {
        if(!qa_actor_id_equal(offer.pickup,pickup) || !qa_actor_id_equal(offer.recipient,recipient) ||
           offer.source!=owner->owner || (offer.cargo_count && !offer.cargo))
            ok=fail(e,QA_ERROR_ARGUMENT,"pickup observation returned a different source or actor");
        if(ok && (!isfinite(utility) || utility<0)) ok=fail(e,QA_ERROR_ARGUMENT,"pickup observation utility must be finite and nonnegative");
    }
    if(ok && observation_current(service,owner,recipient)) {
        *out=(qa_pickup_observation){.pickup=pickup,.recipient=recipient,.source=owner->owner,
            .item=offer.item,.utility=utility,.available=available,.dropped=offer.dropped,
            .spawnflags=offer.spawnflags};
        *found=true;
    }
    --service->observation_calls;--service->calls;sweep(service);return ok;
}

void qa_pickups_actor_released(qa_pickups *service, qa_actor_record actor)
{
    if (!service) return;
    for (pickup_registration *r = registration_actor_find(service, actor.id); r; r = r->actor_next)
        if (qa_actor_id_equal(r->actor, actor.id)) r->active = false;
    observation_retire(service,observation_get(service,actor.id));
    sweep(service);
}

static bool capture_rules(pickup_registration *registration, const qa_pickup_rule *rules, size_t count, qa_error *e)
{
    if (!rules || !count || count > SIZE_MAX / sizeof(*registration->rules)) return fail(e, QA_ERROR_ARGUMENT, "Pickup rules require a nonempty set");
    size_t bytes = 0;
    if (!payload_extent(&bytes, count, sizeof(*registration->rules), _Alignof(pickup_rule), e)) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_pickup_rule *rule = &rules[i];
        if (!rule->id || !rule->take || !rule->offered_count || !rule->offered || !rule->write_count || !rule->writes ||
            rule->offered_count > SIZE_MAX / sizeof(*rule->offered) || rule->write_count > SIZE_MAX / sizeof(*rule->writes) ||
            rule->write_count > SIZE_MAX / sizeof(uint64_t)) return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup rule");
        if (!rule_payload_extent(rule, &bytes, e)) return false;
    }
    if (!registration_payload_reserve(registration_storage(registration), bytes, e)) return false;
    if (!qa_pickups_registration_rules_prepare(registration, count, e)) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_pickup_rule *rule = &rules[i];
        pickup_rule *copy = &registration->rules[i];
        for (size_t j = 0; j < i; ++j) if (registration->rules[j].rule.id == rule->id)
            return fail(e, QA_ERROR_ARGUMENT, "Duplicate original pickup rule ID");
        copy->rule = *rule;
        if (!qa_pickups_registration_rule_prepare(registration, copy, e)) return false;
        qa_item_id *offered = (qa_item_id *)copy->rule.offered;
        qa_pickup_write *writes = (qa_pickup_write *)copy->rule.writes;
        memcpy(offered, rule->offered, rule->offered_count * sizeof(*offered));
        memcpy(writes, rule->writes, rule->write_count * sizeof(*writes));
        for (size_t j = 0; j < rule->offered_count; ++j) {
            if (!offered[j]) return fail(e, QA_ERROR_ARGUMENT, "Pickup rule offers an invalid item");
            for (size_t a = 0; a <= i; ++a) {
                const qa_pickup_rule *previous = &registration->rules[a].rule;
                size_t end = a == i ? j : previous->offered_count;
                for (size_t b = 0; b < end; ++b) if (previous->offered[b] == offered[j])
                    return fail(e, QA_ERROR_ARGUMENT, "Ambiguous original pickup rule offer");
            }
        }
        for (size_t j = 0; j < rule->write_count; ++j) {
            if (!resource_valid(writes[j].resource, false) ||
                (writes[j].resource.kind == QA_PICKUP_INVENTORY &&
                 writes[j].fields != QA_PICKUP_COUNT && writes[j].fields != QA_PICKUP_CAPACITY && writes[j].fields != QA_PICKUP_COUNT_CAPACITY))
                return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup write declaration");
            for (size_t k = 0; k < j; ++k) if (resource_equal(writes[j].resource, writes[k].resource))
                return fail(e, QA_ERROR_ARGUMENT, "Duplicate original pickup resource write");
        }
    }
    return true;
}

size_t qa_pickups_inventory_write_count(const qa_pickup_write *writes, size_t count)
{
    size_t inventory = 0;
    for (size_t i = 0; i < count; ++i) inventory += writes[i].resource.kind == QA_PICKUP_INVENTORY;
    return inventory;
}

static bool registration_inventory_prepare(qa_pickups *service,
    const pickup_registration *registration, qa_error *e)
{
    size_t inventory = 0;
    for (size_t i = 0; i < registration->count; ++i) {
        const qa_pickup_rule *rule = &registration->rules[i].rule;
        inventory += qa_pickups_inventory_write_count(rule->writes, rule->write_count);
    }
    return !inventory || qa_inventory_pickup_prepare(service->inventory, registration->actor, inventory, e);
}

static bool registration_conflicts(qa_pickups *service, pickup_registration *candidate, qa_error *e)
{
    for (pickup_registration *r = registration_actor_find(service, candidate->actor); r; r = r->actor_next) {
        if (!r->active || !qa_actor_id_equal(r->actor, candidate->actor)) continue;
        bool held = false;
        for (size_t j = 0; j < r->count; ++j) {
            const qa_pickup_rule *existing = &r->rules[j].rule;
            bool rule_held = false;
            for (size_t y = 0; y < existing->write_count; ++y)
                rule_held |= write_bound(service, r, &existing->writes[y], r->rules[j].storage[y]);
            held |= rule_held;
            if (!rule_held) continue;
            for (size_t i = 0; i < candidate->count; ++i) {
                const qa_pickup_rule *rule = &candidate->rules[i].rule;
                for (size_t x = 0; x < rule->offered_count; ++x)
                    for (size_t y = 0; y < existing->offered_count; ++y)
                        if (rule->offered[x] == existing->offered[y]) return fail(e, QA_ERROR_ARGUMENT, "Multiple original pickup owners offer this item");
                for (size_t x = 0; x < rule->write_count; ++x)
                    for (size_t y = 0; y < existing->write_count; ++y)
                        if (resource_equal(rule->writes[x].resource, existing->writes[y].resource) &&
                            write_bound(service, r, &existing->writes[y], r->rules[j].storage[y]))
                            return fail(e, QA_ERROR_ARGUMENT, "Original pickup resource already has a grant owner");
            }
        }
        if (!held) { r->active = false; qa_inventory_pickup_release(service->inventory, r->actor, r); }
    }
    return true;
}

bool qa_pickups_bind(qa_pickups *service, qa_actor_id actor, qa_actor_owner owner,
                     const qa_pickup_rule *rules, size_t count, qa_pickup_lease *out, qa_error *e)
{
    if (!service || !out || !qa_actors_get(service->actors, actor)) return fail(e, QA_ERROR_NOT_FOUND, "Pickup recipient is not current");
    if (service->serial == UINT64_MAX) return fail(e, QA_ERROR_ARGUMENT, "Pickup registration identity exhausted");
    pickup_registration *registration = qa_pickups_registration_take(service, e);
    if (!registration) return false;
    registration->actor = actor; registration->owner = owner; registration->active = true;
    qa_combat_pickup_scope combat_scope;
    if (!qa_combat_pickup_begin(service->combat, actor, &combat_scope, e)) { registration_release(registration); return false; }
    qa_inventory_hold(service->inventory);
    ++service->calls;
    bool ok = capture_rules(registration, rules, count, e) &&
        registration_inventory_prepare(service, registration, e) &&
        registration_conflicts(service, registration, e);
    for (size_t i = 0; ok && i < registration->count; ++i) {
        pickup_rule *rule = &registration->rules[i];
        for (size_t j = 0; ok && j < rule->rule.write_count; ++j) {
            const qa_pickup_write *write = &rule->rule.writes[j];
            qa_actor_owner resource_owner;
            if (write->resource.kind == QA_PICKUP_PROTECTION) {
                ok = qa_combat_protection_owner(service->combat, actor, write->resource.channel, &resource_owner, &rule->storage[j]) && resource_owner == owner;
                if (ok) ok = qa_combat_protection_bound(service->combat, (qa_protection_lease){actor, rule->storage[j], write->resource.channel});
                if (!ok) fail(e, QA_ERROR_ARGUMENT, "Original pickup protection is not bound to its grant owner");
            } else {
                ok = qa_inventory_storage_token(service->inventory, actor, write->resource.item, &rule->storage[j], e);
                if (ok && write->fields != QA_PICKUP_COUNT && !qa_inventory_mutable_capacity(service->inventory, actor, write->resource.item))
                    ok = fail(e, QA_ERROR_ARGUMENT, "Original pickup destination has no mutable capacity");
                if (ok) ok = qa_inventory_pickup_claim(service->inventory, actor, write->resource.item, registration, e);
            }
        }
    }
    if (ok && !registration_current(service, registration)) ok = fail(e, QA_ERROR_NOT_FOUND, "Pickup resources retired during registration");
    if (ok) ok = registration_conflicts(service, registration, e);
    if (ok && service->serial == UINT64_MAX) ok = fail(e, QA_ERROR_ARGUMENT, "Pickup registration identity exhausted");
    if (ok) ok = qa_pickups_registration_index_reserve(service, registration, e);
    if (ok) {
        registration->serial = ++service->serial;
        registration->next = service->registrations; service->registrations = registration;
        qa_pickups_registration_index_insert(service, registration);
        *out = (qa_pickup_lease){actor, registration->serial};
    } else { qa_inventory_pickup_release(service->inventory, actor, registration); registration_release(registration); }
    qa_inventory_unhold(service->inventory);
    qa_combat_pickup_end(&combat_scope, false, NULL);
    --service->calls; sweep(service); return ok;
}

bool qa_pickups_registration_current(qa_pickups *service,qa_pickup_lease lease,qa_actor_owner owner)
{
    pickup_registration *registration = registration_find(service, lease);
    if (!registration || registration->owner != owner) return false;
    for (size_t i = 0; i < registration->count; ++i)
        for (size_t j = 0; j < registration->rules[i].rule.write_count; ++j)
            if (!write_retained(service, registration, registration->rules[i].rule.writes + j,
                registration->rules[i].storage[j])) return false;
    return registration->active && qa_actors_get(service->actors, registration->actor) != NULL;
}

bool qa_pickups_registration_owned(const qa_pickups *service,qa_pickup_lease lease,qa_actor_owner owner)
{
    const pickup_registration *registration = registration_find(service, lease);
    return registration && registration->active && registration->owner == owner;
}

bool qa_pickups_close(qa_pickups *service, qa_pickup_lease lease, qa_error *e)
{
    if (!service) return fail(e, QA_ERROR_ARGUMENT, "Missing pickup service");
    pickup_registration *registration = registration_find(service, lease);
    if (registration) {
        registration->active = false;
        qa_inventory_pickup_release(service->inventory, registration->actor, registration);
    }
    sweep(service); return true;
}

static bool scope_current(const qa_pickup_execution *execution)
{
    if (!execution || !execution->open || !qa_actors_get(execution->service->actors, execution->offer.recipient)) return false;
    bool live = qa_actors_get(execution->service->actors, execution->offer.pickup) != NULL;
    return execution->consumption == PICKUP_CONSUMED ? !live : live;
}

bool qa_pickup_current(const qa_pickup_execution *execution)
{
    return scope_current(execution) && (execution->selection != QA_PICKUP_SELECT_REPLACEMENT ||
        rule_current(execution->service, execution->registration, execution->rule)) && scope_current(execution);
}

bool qa_pickup_recipient_is(const qa_pickup_execution *execution,qa_actor_id actor)
{
    return execution&&!execution->failed&&scope_current(execution)&&
        qa_actor_id_equal(execution->offer.recipient,actor)&&
        (execution->selection!=QA_PICKUP_SELECT_REPLACEMENT||
         (execution->registration&&execution->rule&&
          rule_retained(execution->service,execution->registration,execution->rule)));
}

bool qa_pickups_execution_read(qa_pickups *service,qa_actor_id actor,qa_actor_owner owner,
                              const qa_pickup_execution **out,bool *found,qa_error *e)
{
    if (!service || !out || !found)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup execution read");
    *out = NULL; *found = false;
    for (qa_pickup_execution *execution = service->scopes; execution; execution = execution->previous) {
        if (!qa_actor_id_equal(execution->offer.recipient, actor)) continue;
        if (owner && (execution->selection != QA_PICKUP_SELECT_REPLACEMENT ||
            !execution->registration || !execution->rule || execution->registration->owner != owner)) continue;
        if (execution->failed) {
            if (e) *e = execution->failure;
            return false;
        }
        if (execution->service != service || !scope_current(execution) ||
            (execution->selection == QA_PICKUP_SELECT_REPLACEMENT &&
             (!execution->registration || !execution->rule ||
              !rule_retained(service, execution->registration, execution->rule))))
            return fail(e, QA_ERROR_ARGUMENT, "Pickup execution is no longer current");
        *out = execution; *found = true; return true;
    }
    return true;
}

const qa_pickup_write *qa_pickup_writes(const qa_pickup_execution *execution, size_t *count)
{
    if (count) *count = 0;
    if (!execution || !execution->open || !execution->rule) return NULL;
    if (count) *count = execution->rule->rule.write_count;
    return execution->rule->rule.writes;
}

static bool cargo_valid(const qa_pickup_cargo *cargo, size_t count, qa_error *e)
{
    if ((count && !cargo) || count > SIZE_MAX / sizeof(*cargo)) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup cargo");
    for (size_t i = 0; i < count; ++i) {
        if (!cargo[i].item || !isfinite(cargo[i].count) || (cargo[i].weapon && cargo[i].count != 1))
            return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup cargo count");
        for (size_t j = 0; j < i; ++j) if (cargo[i].item == cargo[j].item)
            return fail(e, QA_ERROR_ARGUMENT, "Duplicate pickup cargo item");
    }
    return true;
}

static bool scope_open(qa_pickups *service, const qa_pickup_offer *offer, qa_pickup_execution *execution, qa_error *e)
{
    *execution = (qa_pickup_execution){.service = service, .selection = QA_PICKUP_SELECT_STALE};
    if (!service || !offer) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup offer");
    execution->offer = *offer;
    if (!qa_actors_get(service->actors, offer->recipient) || !qa_actors_get(service->actors, offer->pickup)) return true;
    for (qa_pickup_execution *scope = service->scopes; scope; scope = scope->previous)
        if (qa_actor_id_equal(scope->offer.pickup, offer->pickup)) return true;
    qa_pickup_cargo retained;
    if (!execution->offer.cargo_count && qa_pickups_cargo_read(service, offer->pickup, &retained)) {
        execution->offer.cargo = &retained;
        execution->offer.cargo_count = 1;
    }
    offer = &execution->offer;
    if (!offer->item || (offer->override_count && !isfinite(offer->count)) || !resource_valid(offer->default_resource, true) ||
        offer->grant < QA_PICKUP_RESOURCE_GRANT || offer->grant > QA_PICKUP_SOURCE_EFFECT || !cargo_valid(offer->cargo, offer->cargo_count, e))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup offer");
    qa_inventory_scratch_begin(service->inventory, &execution->cargo_storage);
    if (offer->cargo_count) {
        qa_pickup_cargo *cargo = qa_arena_alloc(&execution->cargo_storage,
            offer->cargo_count * sizeof(*cargo), _Alignof(qa_pickup_cargo), e);
        if (!cargo) { qa_arena_destroy(&execution->cargo_storage); return false; }
        memcpy(cargo, offer->cargo, offer->cargo_count * sizeof(*cargo)); execution->offer.cargo = cargo;
    } else execution->offer.cargo = NULL;
    if (!qa_combat_pickup_begin(service->combat, offer->recipient, &execution->combat_scope, e)) {
        qa_arena_destroy(&execution->cargo_storage); return false;
    }
    qa_inventory_hold(service->inventory);
    execution->open = true; execution->previous = service->scopes; service->scopes = execution;
    ++service->calls;
    return true;
}

static void scope_close(qa_pickup_execution *execution)
{
    if (!execution->open) return;
    qa_pickups *service = execution->service;
    execution->open = false; service->scopes = execution->previous;
    qa_arena_destroy(&execution->cargo_storage);
    qa_inventory_unhold(service->inventory);
    qa_combat_pickup_end(&execution->combat_scope, false, NULL);
    --service->calls; sweep(service);
}

static bool select_pickup(qa_pickup_execution *execution, qa_error *e)
{
    qa_pickups *service = execution->service;
    if (!scope_current(execution)) { execution->selection = QA_PICKUP_SELECT_STALE; return true; }
    bool eligible = true;
    if (service->eligible && !service->eligible(service->eligibility_context, &execution->offer, &eligible, e)) return false;
    if (!scope_current(execution)) { execution->selection = QA_PICKUP_SELECT_STALE; return true; }
    if (!eligible) { execution->selection = QA_PICKUP_SELECT_BLOCKED; return true; }
    if (execution->offer.grant == QA_PICKUP_SOURCE_EFFECT) {
        const qa_actor_record *pickup = qa_actors_get(service->actors, execution->offer.pickup);
        if (pickup->owner != execution->offer.source) return fail(e, QA_ERROR_ARGUMENT, "Original pickup effect belongs to another source");
        execution->selection = QA_PICKUP_SELECT_ORIGINAL; return true;
    }
    for (pickup_registration *r = registration_actor_find(service, execution->offer.recipient);
            r; r = r->actor_next) {
        if (!r->active || !qa_actor_id_equal(r->actor, execution->offer.recipient)) continue;
        const qa_id_index *index = &service->observation_storage->registration_index;
        qa_id_index_cursor cursor = qa_id_index_begin(index, execution->offer.item, r->serial);
        size_t value;
        while (qa_id_index_next(index, &cursor, &value)) {
            pickup_rule *rule = (pickup_rule *)(uintptr_t)value;
            size_t bound = 0;
            for (size_t j = 0; j < rule->rule.write_count; ++j)
                if (write_bound(service, r, &rule->rule.writes[j], rule->storage[j])) ++bound;
            if (!bound) continue;
            if (bound != rule->rule.write_count) return fail(e, QA_ERROR_ARGUMENT, "Original pickup has an incomplete resource write set");
            if (execution->rule) return fail(e, QA_ERROR_ARGUMENT, "Multiple original pickup owners accept this offer");
            execution->rule = rule; execution->registration = r;
        }
    }
    if (!scope_current(execution)) { execution->selection = QA_PICKUP_SELECT_STALE; return true; }
    if (execution->rule) {
        if (execution->offer.grant == QA_PICKUP_MAP_COUPLED) return fail(e, QA_ERROR_ARGUMENT, "Original pickup replacement requires its map lifecycle");
        execution->selection = QA_PICKUP_SELECT_REPLACEMENT;
    } else {
        qa_pickup_resource resource = execution->offer.default_resource;
        bool blocks = resource.kind == QA_PICKUP_INVENTORY
            ? qa_inventory_item_owner(service->inventory, execution->offer.recipient, resource.item, NULL, NULL)
            : resource.kind == QA_PICKUP_PROTECTION && qa_combat_protection_owner(service->combat, execution->offer.recipient, resource.channel, NULL, NULL);
        execution->selection = blocks ? QA_PICKUP_SELECT_BLOCKED : QA_PICKUP_SELECT_ORIGINAL;
    }
    return true;
}

bool qa_pickups_preview(qa_pickups *service,const qa_pickup_offer *offer,float *utility,
                         bool *accepted,bool *handled,qa_error *e)
{
    if(!utility || !accepted || !handled)
        return fail(e,QA_ERROR_ARGUMENT,"missing pickup preview result");
    *utility=0;*accepted=false;*handled=true;
    if(service && service->observation_calls==SIZE_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"pickup observation nesting exhausted");
    qa_pickup_execution execution;
    if(!scope_open(service,offer,&execution,e)) return false;
    if(!execution.open) return true;
    ++service->observation_calls;
    bool ok=select_pickup(&execution,e);
    if(ok && execution.selection==QA_PICKUP_SELECT_ORIGINAL) *handled=false;
    else if(ok && execution.selection==QA_PICKUP_SELECT_REPLACEMENT) {
        if(!execution.rule->rule.preview)
            ok=fail(e,QA_ERROR_UNSUPPORTED,"selected pickup replacement has no read-only preview");
        else ok=execution.rule->rule.preview(execution.rule->rule.context,&execution.offer,utility,accepted,e);
        if(ok && (!isfinite(*utility) || *utility<0))
            ok=fail(e,QA_ERROR_ARGUMENT,"pickup preview utility must be finite and nonnegative");
        if(ok && !qa_pickup_current(&execution)) {*utility=0;*accepted=false;}
    }
    --service->observation_calls;scope_close(&execution);return ok;
}

bool qa_pickup_execute_grant(qa_pickup_execution *execution, qa_pickup_outcome *out, qa_error *e)
{
    if (!execution || !out || !execution->open || execution->selection != QA_PICKUP_SELECT_REPLACEMENT)
        return execution_failed(execution, e, "Pickup has no current replacement grant");
    if (execution->grant_used) return execution_failed(execution, e, "Original pickup grant already used");
    execution->grant_used = true;
    if (!qa_pickup_current(execution)) { *out = QA_PICKUP_STALE; return true; }
    execution->observer_open = true;
    qa_pickup_outcome outcome = QA_PICKUP_REFUSED;
    qa_combat_pickup_scope combat_scope;
    if (!qa_combat_pickup_begin(execution->service->combat, execution->offer.recipient, &combat_scope, e)) {
        execution->observer_open = false;
        return execution_error(execution, e);
    }
    bool ok = execution->rule->rule.take(execution->rule->rule.context, &execution->offer, execution, &outcome, e);
    execution->observer_open = false;
    qa_error end_error = {0};
    if (!qa_combat_pickup_end(&combat_scope, ok && !execution->failed, &end_error) && ok) {
        if (e) *e = end_error;
        ok = false;
    }
    if (!ok) return execution_error(execution, e);
    if (execution->failed) { if (e) *e = execution->failure; return false; }
    if (outcome != QA_PICKUP_ACCEPTED && outcome != QA_PICKUP_REFUSED && outcome != QA_PICKUP_STALE)
        return execution_failed(execution, e, "Invalid original pickup grant outcome");
    if (!qa_pickup_current(execution)) outcome = QA_PICKUP_STALE;
    execution->accepted = outcome == QA_PICKUP_ACCEPTED;
    *out = outcome; return true;
}

bool qa_pickup_store_protection(qa_pickup_execution *execution, const qa_protection_store *store, qa_error *e)
{
    if (!execution || !store || !execution->observer_open || !qa_pickup_current(execution))
        return execution_failed(execution, e, "Original pickup protection observer is closed or stale");
    bool regular = false, powered = false;
    for (size_t i = 0; i < execution->rule->rule.write_count; ++i) {
        qa_pickup_resource resource = execution->rule->rule.writes[i].resource;
        if (resource.kind == QA_PICKUP_PROTECTION) {
            if (resource.channel == QA_PROTECTION_REGULAR) regular = true;
            else powered = true;
        }
    }
    if ((store->regular && !regular) || (store->powered && !powered))
        return execution_failed(execution, e, "Original pickup changed undeclared protection");
    if (!qa_combat_pickup_store(execution->service->combat, execution->offer.recipient, execution->registration->owner, store, e))
        return execution_error(execution, e);
    return true;
}

bool qa_pickup_consume(qa_pickup_execution *execution, bool (*remove)(void *, qa_error *), void *context, qa_error *e)
{
    if (!execution || !remove || !execution->source_scope || !execution->accepted || execution->consumption != PICKUP_LIVE ||
        !qa_pickup_current(execution) || execution->selection == QA_PICKUP_SELECT_BLOCKED || execution->selection == QA_PICKUP_SELECT_STALE)
        return execution_failed(execution, e, "Original pickup consumption lost its source scope");
    execution->consumption = PICKUP_REMOVING;
    if (!remove(context, e)) return execution_error(execution, e);
    if (!execution->open || !qa_actors_get(execution->service->actors, execution->offer.recipient) ||
        qa_actors_get(execution->service->actors, execution->offer.pickup))
        return execution_failed(execution, e, "Original pickup consumption did not retire its exact pickup");
    execution->consumption = PICKUP_CONSUMED;
    if (!qa_pickup_current(execution)) return execution_failed(execution, e, "Original pickup consumption changed recipient storage");
    return true;
}

bool qa_pickups_touch(qa_pickups *service, const qa_pickup_offer *offer,
                      const qa_pickup_continuation *continuation, qa_pickup_outcome *out, qa_error *e)
{
    if(service && service->observation_calls) return fail(e,QA_ERROR_ARGUMENT,"pickup observations cannot take items");
    if (!continuation || !continuation->original || !continuation->complete || !out)
        return fail(e, QA_ERROR_ARGUMENT, "Incomplete pickup touch continuation");
    qa_pickup_execution execution;
    if (!scope_open(service, offer, &execution, e)) return false;
    qa_pickup_outcome outcome = QA_PICKUP_STALE;
    if (!execution.open) { *out = outcome; return true; }
    bool eligible = true, ok = true;
    if (continuation->eligible) ok = continuation->eligible(continuation->context, &execution.offer, &eligible, e);
    if (!ok) goto done;
    if (!eligible) { outcome = QA_PICKUP_REFUSED; goto done; }
    if (!scope_current(&execution)) goto done;
    ok = select_pickup(&execution, e);
    if (!ok) goto done;
    if (execution.selection == QA_PICKUP_SELECT_REPLACEMENT) ok = qa_pickup_execute_grant(&execution, &outcome, e);
    else if (execution.selection == QA_PICKUP_SELECT_ORIGINAL) {
        bool taken = false;
        ok = continuation->original(continuation->context, &execution.offer, &taken, e);
        outcome = taken ? QA_PICKUP_ACCEPTED : QA_PICKUP_REFUSED;
    } else outcome = QA_PICKUP_REFUSED;
    if (!ok) goto done;
    if (outcome == QA_PICKUP_STALE || !qa_pickup_current(&execution)) { outcome = QA_PICKUP_STALE; goto done; }
    ok = continuation->complete(continuation->context, &execution.offer, outcome == QA_PICKUP_ACCEPTED, e);
done:
    if (execution.failed) { if (e) *e = execution.failure; ok = false; }
    scope_close(&execution);
    if (ok) *out = outcome;
    return ok;
}

bool qa_pickups_run_source(qa_pickups *service, const qa_pickup_offer *offer,
                           qa_pickup_source_fn callback, void *context, qa_error *e)
{
    if(service && service->observation_calls) return fail(e,QA_ERROR_ARGUMENT,"pickup observations cannot execute grants");
    if (!callback) return fail(e, QA_ERROR_ARGUMENT, "Missing original pickup source callback");
    qa_pickup_execution execution;
    if (!scope_open(service, offer, &execution, e)) return false;
    if (!execution.open) {
        qa_combat_pickup_scope combat_scope;
        if (!qa_combat_pickup_begin(service->combat, offer->recipient, &combat_scope, e)) return false;
        qa_inventory_hold(service->inventory);
        ++service->calls;
        bool ok = callback(context, &execution.offer, QA_PICKUP_SELECT_STALE, &execution, e);
        if (execution.failed) { if (e) *e = execution.failure; ok = false; }
        qa_inventory_unhold(service->inventory);
        qa_combat_pickup_end(&combat_scope, false, NULL);
        --service->calls; sweep(service); return ok;
    }
    execution.source_scope = true;
    bool ok = select_pickup(&execution, e);
    if (ok) {
        execution.accepted = execution.selection == QA_PICKUP_SELECT_ORIGINAL;
        ok = callback(context, &execution.offer, execution.selection, &execution, e);
    }
    if (execution.failed) { if (e) *e = execution.failure; ok = false; }
    scope_close(&execution); return ok;
}

struct qa_supply {
    qa_arena storage;
    qa_inventory *inventory;
    qa_supply_profile profile;
    qa_supply_hooks hooks;
    size_t calls;
    bool closed;
};

typedef struct supply_grants {
    qa_arena storage;
    qa_item_id *weapons;
    size_t weapon_count, weapon_capacity;
    qa_pickup_grant *ammo;
    size_t ammo_count, ammo_capacity;
    bool *exact;
    bool acceptance_override, accepted;
} supply_grants;

static void supply_free(qa_supply *supply)
{
    /* The service itself is in this arena, so destruction uses its moved handle. */
    qa_arena storage = supply->storage;
    qa_arena_destroy(&storage);
}

static void supply_leave(qa_supply *supply)
{
    qa_inventory_unhold(supply->inventory);
    if (--supply->calls == 0 && supply->closed) supply_free(supply);
}

static void supply_enter(qa_supply *supply)
{ ++supply->calls; qa_inventory_hold(supply->inventory); }

static bool supply_available(qa_supply *supply, qa_error *e)
{ return (supply && !supply->closed) || fail(e, QA_ERROR_NOT_FOUND, "Pickup supply service retired"); }

static bool supply_current(qa_supply *supply, qa_actor_id actor, qa_error *e)
{
    if (!supply_available(supply, e)) return false;
    if (supply->hooks.current && !supply->hooks.current(supply->hooks.context, actor, e)) return false;
    return supply_available(supply, e);
}

static const qa_supply_mapping *mapping_find(const qa_supply_mapping *mappings, size_t count, qa_item_id item)
{
    for (size_t i = 0; i < count; ++i) if (mappings[i].source == item) return &mappings[i];
    return NULL;
}

static bool mapping_contains(const qa_supply_mapping *mapping, qa_item_id item)
{
    if (mapping) for (size_t i = 0; i < mapping->count; ++i) if (mapping->destinations[i] == item) return true;
    return false;
}

static bool mappings_copy(qa_arena *storage, const qa_supply_mapping *input, size_t count,
                           const qa_supply_mapping **out, qa_error *e)
{
    if ((count && !input) || count > SIZE_MAX / sizeof(*input)) return fail(e, QA_ERROR_ARGUMENT, "Invalid supply mappings");
    qa_supply_mapping *copy = count ? qa_arena_alloc(storage,
        count * sizeof(*copy), _Alignof(qa_supply_mapping), e) : NULL;
    if (count && !copy) return fail(e, QA_ERROR_MEMORY, "Cannot allocate supply mappings");
    if (count) memset(copy, 0, count * sizeof(*copy));
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        if (!input[i].source || !input[i].count || !input[i].destinations || input[i].count > SIZE_MAX / sizeof(qa_item_id)) {
            ok = fail(e, QA_ERROR_ARGUMENT, "Supply mapping requires source and destinations"); break;
        }
        for (size_t j = 0; j < i; ++j) if (input[j].source == input[i].source) ok = fail(e, QA_ERROR_ARGUMENT, "Duplicate supply mapping source");
        for (size_t j = 0; ok && j < input[i].count; ++j) {
            if (!input[i].destinations[j]) ok = fail(e, QA_ERROR_ARGUMENT, "Invalid supply destination item");
            for (size_t k = 0; k < j; ++k) if (input[i].destinations[k] == input[i].destinations[j])
                ok = fail(e, QA_ERROR_ARGUMENT, "Duplicate supply destination item");
        }
        if (!ok) break;
        qa_item_id *destinations = qa_arena_alloc(storage,
            input[i].count * sizeof(*destinations), _Alignof(qa_item_id), e);
        if (!destinations) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy supply destinations"); break; }
        memcpy(destinations, input[i].destinations, input[i].count * sizeof(*destinations));
        copy[i] = input[i]; copy[i].destinations = destinations;
    }
    if (ok) *out = copy;
    return ok;
}

static bool owners_unique(const qa_supply_source_owner *owners, size_t count, qa_error *e)
{
    if ((count && !owners) || count > SIZE_MAX / sizeof(*owners)) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup source owners");
    for (size_t i = 0; i < count; ++i) {
        if (!owners[i].item || !owners[i].source)
            return fail(e, QA_ERROR_ARGUMENT, "Selected item has an invalid original owner");
        for (size_t j = 0; j < i; ++j) if (owners[j].item == owners[i].item)
            return fail(e, QA_ERROR_ARGUMENT, "Selected item has duplicate original owners");
    }
    return true;
}

static bool owners_valid(const qa_supply_source_owner *owners, size_t count,
                          const qa_supply_mapping *mappings, size_t mapping_count, qa_error *e)
{
    if (!owners_unique(owners, count, e)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!mapping_contains(mapping_find(mappings, mapping_count, owners[i].source), owners[i].item))
            return fail(e, QA_ERROR_ARGUMENT, "Selected item has an invalid original owner");
    return true;
}

static bool owners_copy(qa_arena *storage, const qa_supply_source_owner *input, size_t count,
                         const qa_supply_source_owner **out, qa_error *e)
{
    qa_supply_source_owner *copy = count ? qa_arena_alloc(storage,
        count * sizeof(*copy), _Alignof(qa_supply_source_owner), e) : NULL;
    if (count && !copy) return fail(e, QA_ERROR_MEMORY, "Cannot allocate supply source owners");
    if (count) memcpy(copy, input, count * sizeof(*copy));
    *out = copy; return true;
}

bool qa_supply_create(qa_inventory *inventory, const qa_supply_profile *profile,
                       const qa_supply_hooks *hooks, qa_supply **out, qa_error *e)
{
    if (!inventory || !profile || !out) return fail(e, QA_ERROR_ARGUMENT, "Invalid supply service arguments");
    qa_arena storage;
    qa_arena_init(&storage, sizeof(qa_supply));
    qa_supply *supply = qa_arena_alloc(&storage, sizeof(*supply), _Alignof(qa_supply), e);
    if (!supply) { qa_arena_destroy(&storage); return false; }
    *supply = (qa_supply){.storage = storage, .inventory = inventory};
    storage = (qa_arena){0};
    if (hooks) supply->hooks = *hooks;
    supply->profile.weapon_count = profile->weapon_count; supply->profile.ammo_count = profile->ammo_count;
    supply->profile.weapon_owner_count = profile->weapon_owner_count; supply->profile.ammo_owner_count = profile->ammo_owner_count;
    bool ok = mappings_copy(&supply->storage, profile->weapons, profile->weapon_count, &supply->profile.weapons, e) &&
        mappings_copy(&supply->storage, profile->ammo, profile->ammo_count, &supply->profile.ammo, e) &&
        owners_valid(profile->weapon_owners, profile->weapon_owner_count, supply->profile.weapons, profile->weapon_count, e) &&
        owners_unique(profile->ammo_owners, profile->ammo_owner_count, e) &&
        owners_copy(&supply->storage, profile->weapon_owners, profile->weapon_owner_count, &supply->profile.weapon_owners, e) &&
        owners_copy(&supply->storage, profile->ammo_owners, profile->ammo_owner_count, &supply->profile.ammo_owners, e);
    if (!ok) { supply_free(supply); return false; }
    qa_arena_seal(&supply->storage);
    *out = supply; return true;
}

void qa_supply_destroy(qa_supply *supply)
{
    if (!supply || supply->closed) return;
    supply->closed = true;
    if (!supply->calls) supply_free(supply);
}

bool qa_supply_idle(const qa_supply *supply) { return !supply || supply->calls == 0; }

bool qa_supply_maps(const qa_supply *supply, qa_item_id item, bool weapon)
{
    return supply && !supply->closed && mapping_find(weapon ? supply->profile.weapons : supply->profile.ammo,
        weapon ? supply->profile.weapon_count : supply->profile.ammo_count, item) != NULL;
}

static const qa_supply_mapping *destinations(qa_supply *supply, qa_item_id item, bool weapon, qa_error *e)
{
    const qa_supply_mapping *mapping = mapping_find(weapon ? supply->profile.weapons : supply->profile.ammo,
        weapon ? supply->profile.weapon_count : supply->profile.ammo_count, item);
    if (!mapping) fail(e, QA_ERROR_NOT_FOUND, "Pickup supply has no mapping for authored item");
    return mapping;
}

static bool buffer_grow(qa_arena *storage, void **buffer, size_t *capacity, size_t count, size_t element, size_t alignment, qa_error *e)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity : 8;
    while (next < count && next <= SIZE_MAX / 2) next *= 2;
    if (next < count || next > SIZE_MAX / element) return fail(e, QA_ERROR_MEMORY, "Pickup grant size overflow");
    void *allocation = qa_arena_grow(storage, *buffer, *capacity * element,
        next * element, alignment, e);
    if (!allocation) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup grants");
    *buffer = allocation; *capacity = next; return true;
}

static void grants_free(supply_grants *grants)
{ qa_arena_destroy(&grants->storage); *grants = (supply_grants){0}; }

static void grants_begin(qa_supply *supply, supply_grants *grants)
{ qa_inventory_scratch_begin(supply->inventory, &grants->storage); }

static bool append_weapon(supply_grants *grants, qa_item_id item, qa_error *e)
{
    if (!item) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup weapon");
    for (size_t i = 0; i < grants->weapon_count; ++i) if (grants->weapons[i] == item) return true;
    void *buffer = grants->weapons;
    if (!buffer_grow(&grants->storage, &buffer, &grants->weapon_capacity,
            grants->weapon_count + 1, sizeof(*grants->weapons), _Alignof(qa_item_id), e)) return false;
    grants->weapons = buffer; grants->weapons[grants->weapon_count++] = item; return true;
}

static bool append_ammo(supply_grants *grants, qa_pickup_grant grant, qa_error *e)
{
    if (!grant.item) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup ammunition");
    void *buffer = grants->ammo;
    if (!buffer_grow(&grants->storage, &buffer, &grants->ammo_capacity,
            grants->ammo_count + 1, sizeof(*grants->ammo), _Alignof(qa_pickup_grant), e)) return false;
    grants->ammo = buffer; grants->ammo[grants->ammo_count++] = grant; return true;
}

static bool resolve_weapon(qa_supply *supply, supply_grants *grants, qa_item_id item, bool canonical, qa_error *e)
{
    if (canonical) return append_weapon(grants, item, e);
    const qa_supply_mapping *mapping = destinations(supply, item, true, e);
    if (!mapping) return false;
    for (size_t i = 0; i < mapping->count; ++i) if (!append_weapon(grants, mapping->destinations[i], e)) return false;
    return true;
}

static bool resolve_ammo(qa_supply *supply, supply_grants *grants, qa_pickup_grant grant,
                          bool canonical, bool source_quantity, qa_error *e)
{
    if (!isfinite(grant.amount) || (!source_quantity && grant.amount < 0)) return fail(e, QA_ERROR_ARGUMENT, "Pickup amount must be finite and nonnegative");
    if (canonical) return append_ammo(grants, grant, e);
    const qa_supply_mapping *mapping = destinations(supply, grant.item, false, e);
    if (!mapping) return false;
    for (size_t i = 0; i < mapping->count; ++i)
        if (!append_ammo(grants, (qa_pickup_grant){mapping->destinations[i], grant.amount}, e)) return false;
    return true;
}

static bool resolve_offer(qa_supply *supply, const qa_supply_offer *offer,
                           const qa_supply_options *options, supply_grants *grants, qa_error *e)
{
    if (!offer || (offer->ammo_count && !offer->ammo) || offer->ammo_count > SIZE_MAX / sizeof(*offer->ammo) ||
        offer->kind < QA_SUPPLY_AMMO || offer->kind > QA_SUPPLY_WEAPON)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup supply offer");
    if (offer->kind != QA_SUPPLY_AMMO && !resolve_weapon(supply, grants,
        offer->kind == QA_SUPPLY_WEAPON ? offer->item : offer->weapon, options->canonical, e)) return false;
    for (size_t i = 0; i < offer->ammo_count; ++i)
        if (!resolve_ammo(supply, grants, offer->ammo[i], options->canonical, options->quantity != NULL, e)) return false;
    return true;
}

static bool require_entries(qa_supply *supply, qa_actor_id actor, const supply_grants *grants, qa_error *e)
{
    qa_inventory_entry entry;
    for (size_t i = 0; i < grants->weapon_count; ++i)
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->weapons[i], &entry, e) || !supply_current(supply, actor, e)) return false;
    for (size_t i = 0; i < grants->ammo_count; ++i)
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->ammo[i].item, &entry, e) || !supply_current(supply, actor, e)) return false;
    return true;
}

static bool resolve_quantities(qa_supply *supply, qa_actor_id actor, supply_grants *grants,
                                const qa_supply_options *options, qa_error *e)
{
    if (!options->quantity) return true;
    grants->exact = grants->ammo_count ? qa_arena_alloc(&grants->storage,
        grants->ammo_count * sizeof(*grants->exact), _Alignof(bool), e) : NULL;
    if (grants->ammo_count && !grants->exact) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup quantity decisions");
    for (size_t i = 0; i < grants->ammo_count; ++i) {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->ammo[i].item, &entry, e) || !supply_current(supply, actor, e)) return false;
        qa_supply_quantity quantity = {0};
        if (!options->quantity(options->quantity_context, &entry, &quantity, e) || !supply_current(supply, actor, e)) return false;
        if (!isfinite(quantity.amount) || (!quantity.exact && quantity.amount < 0)) return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup quantity");
        grants->ammo[i].amount = quantity.amount;
        grants->exact[i] = quantity.exact;
        if (quantity.exact) { grants->acceptance_override = true; grants->accepted |= quantity.accepted; }
    }
    for (size_t i = 0; i < grants->ammo_count; ++i) if (grants->exact[i])
        for (size_t j = 0; j < grants->ammo_count; ++j)
            if (grants->ammo[i].item == grants->ammo[j].item) grants->exact[j] = true;
    return true;
}

static bool grant_ammo(qa_supply *supply, qa_actor_id actor, const supply_grants *grants,
                        qa_pickup_receipt *receipts, qa_error *e)
{
    for (size_t i = 0; i < grants->ammo_count; ++i) {
        qa_inventory_entry entry;
        qa_pickup_grant grant = grants->ammo[i];
        if (!qa_inventory_entry_read(supply->inventory, actor, grant.item, &entry, e) || !supply_current(supply, actor, e)) return false;
        receipts[i] = (qa_pickup_receipt){.item = grant.item, .before = entry.count};
        if (grants->exact && grants->exact[i]) {
            entry.count += grant.amount;
            if (!qa_inventory_configure(supply->inventory, actor, &entry, NULL, NULL, e) || !supply_current(supply, actor, e) ||
                !qa_inventory_entry_read(supply->inventory, actor, grant.item, &entry, e) || !supply_current(supply, actor, e)) return false;
            receipts[i].given = entry.count - receipts[i].before;
        } else if (!qa_inventory_give(supply->inventory, actor, grant.item, grant.amount, &receipts[i].given, e) || !supply_current(supply, actor, e)) return false;
    }
    return true;
}

static bool selection_valid(qa_pickup_selection_mode selection, qa_error *e)
{
    return (selection >= QA_PICKUP_SWITCH_NEVER && selection <= QA_PICKUP_SWITCH_IF_BETTER) ||
        fail(e, QA_ERROR_ARGUMENT, "Invalid pickup weapon selection mode");
}

static bool grant_resolved(qa_supply *supply, qa_actor_id actor, supply_grants *grants,
                            bool weapon_offer, const qa_supply_options *options, bool *accepted, qa_error *e)
{
    bool ok = require_entries(supply, actor, grants, e) && resolve_quantities(supply, actor, grants, options, e);
    qa_pickup_receipt *receipts = grants->ammo_count ? qa_arena_alloc(&grants->storage,
        grants->ammo_count * sizeof(*receipts), _Alignof(qa_pickup_receipt), e) : NULL;
    if (grants->ammo_count && !receipts) ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup grant receipts");
    if (!ok) return false;
    if (weapon_offer) for (size_t i = 0; ok && i < grants->weapon_count; ++i) {
        double given;
        ok = qa_inventory_give(supply->inventory, actor, grants->weapons[i], 1, &given, e) && supply_current(supply, actor, e);
    }
    if (ok) ok = grant_ammo(supply, actor, grants, receipts, e);
    bool taken = weapon_offer || (grants->acceptance_override && grants->accepted);
    if (!weapon_offer && !grants->acceptance_override)
        for (size_t i = 0; i < grants->ammo_count; ++i) taken |= receipts[i].given > 0;
    if (ok && taken) {
        if (!weapon_offer && grants->weapon_count) {
            for (size_t i = 0; ok && i < grants->weapon_count; ++i) {
                bool shared = false;
                for (size_t j = 0; j < grants->ammo_count; ++j) if (grants->ammo[j].item == grants->weapons[i]) { shared = true; break; }
                if (!shared) { double given; ok = qa_inventory_give(supply->inventory, actor, grants->weapons[i], 1, &given, e) && supply_current(supply, actor, e); }
            }
        }
        if (ok && grants->weapon_count && supply->hooks.weapon_granted) {
            bool select = weapon_offer || !options->only_empty;
            for (size_t i = 0; i < grants->ammo_count; ++i) select |= receipts[i].before == 0 && receipts[i].given > 0;
            ok = supply->hooks.weapon_granted(supply->hooks.context, actor, grants->weapons, grants->weapon_count,
                select ? options->selection : QA_PICKUP_SWITCH_NEVER, e) && supply_current(supply, actor, e);
        } else if (ok && !weapon_offer && !grants->weapon_count && supply->hooks.ammo_granted)
            ok = supply->hooks.ammo_granted(supply->hooks.context, actor, receipts, grants->ammo_count, options->auto_switch, e) && supply_current(supply, actor, e);
    }
    if (ok) *accepted = taken;
    return ok;
}

bool qa_supply_apply(qa_supply *supply, qa_actor_id actor, const qa_supply_offer *offer,
                      const qa_supply_options *options, bool *accepted, qa_error *e)
{
    if (!options || !accepted) return fail(e, QA_ERROR_ARGUMENT, "Missing pickup supply options or result");
    if (!selection_valid(options->selection, e) || !supply_available(supply, e)) return false;
    qa_supply_options captured = *options;
    supply_enter(supply);
    supply_grants grants = {0};
    grants_begin(supply, &grants);
    bool ok = supply_current(supply, actor, e) && resolve_offer(supply, offer, &captured, &grants, e) &&
        grant_resolved(supply, actor, &grants, offer->kind == QA_SUPPLY_WEAPON, &captured, accepted, e);
    grants_free(&grants); supply_leave(supply); return ok;
}

bool qa_supply_ammo(qa_supply *supply, qa_actor_id actor, qa_pickup_grant grant,
                     bool auto_switch, bool *accepted, qa_error *e)
{
    qa_supply_offer offer = {.kind = QA_SUPPLY_AMMO, .ammo = &grant, .ammo_count = 1};
    qa_supply_options options = {.auto_switch = auto_switch};
    return qa_supply_apply(supply, actor, &offer, &options, accepted, e);
}

bool qa_supply_ammo_weapon(qa_supply *supply, qa_actor_id actor, qa_item_id weapon, qa_pickup_grant grant,
    qa_pickup_selection_mode selection, bool only_empty, bool *accepted, qa_error *e)
{
    qa_supply_offer offer = {.kind = QA_SUPPLY_AMMO_WEAPON, .weapon = weapon, .ammo = &grant, .ammo_count = 1};
    qa_supply_options options = {.selection = selection, .only_empty = only_empty};
    return qa_supply_apply(supply, actor, &offer, &options, accepted, e);
}

bool qa_supply_cargo(qa_supply *supply, qa_actor_id actor, const qa_pickup_cargo *cargo, size_t count,
                      qa_pickup_selection_mode selection, bool canonical, bool *accepted, qa_error *e)
{
    if (!accepted) return fail(e, QA_ERROR_ARGUMENT, "Missing cargo acceptance result");
    if (!supply_available(supply, e) || !selection_valid(selection, e) || !cargo_valid(cargo, count, e)) return false;
    supply_enter(supply);
    supply_grants grants = {0};
    grants_begin(supply, &grants);
    bool ok = supply_current(supply, actor, e);
    for (size_t i = 0; ok && i < count; ++i)
        if (cargo[i].weapon) ok = resolve_weapon(supply, &grants, cargo[i].item, canonical, e);
        else ok = resolve_ammo(supply, &grants, (qa_pickup_grant){cargo[i].item, cargo[i].count}, canonical, false, e);
    qa_supply_options options = {.selection = selection, .canonical = canonical}; bool granted = false;
    if (ok) ok = grant_resolved(supply, actor, &grants, true, &options, &granted, e);
    if (ok) *accepted = granted;
    grants_free(&grants); supply_leave(supply); return ok;
}

bool qa_supply_owns(qa_supply *supply, qa_actor_id actor, qa_item_id item, bool *owned, qa_error *e)
{
    if (!owned) return fail(e, QA_ERROR_ARGUMENT, "Missing supply ownership result");
    if (!supply_available(supply, e)) return false;
    supply_enter(supply);
    const qa_supply_mapping *mapping = supply_current(supply, actor, e) ? destinations(supply, item, true, e) : NULL;
    bool ok = mapping != NULL, all = mapping && mapping->count != 0;
    for (size_t i = 0; ok && i < mapping->count; ++i) {
        qa_inventory_entry entry;
        qa_error read_error = {0};
        if (!qa_inventory_entry_read(supply->inventory, actor, mapping->destinations[i], &entry, &read_error)) {
            if (read_error.code == QA_ERROR_NOT_FOUND) { all = false; break; }
            if (e) *e = read_error;
            ok = false; break;
        }
        ok = supply_current(supply, actor, e);
        if (entry.count <= 0) { all = false; break; }
    }
    if (ok) ok = supply_current(supply, actor, e);
    if (ok) *owned = all;
    supply_leave(supply); return ok;
}

bool qa_supply_select_weapon(qa_supply *supply, qa_actor_id actor, qa_item_id item,
                              qa_pickup_selection_mode selection, qa_error *e)
{
    if (!selection_valid(selection, e) || !supply_available(supply, e)) return false;
    supply_enter(supply);
    supply_grants grants = {0};
    grants_begin(supply, &grants);
    bool ok = supply_current(supply, actor, e) && resolve_weapon(supply, &grants, item, false, e) && require_entries(supply, actor, &grants, e);
    if (ok && supply->hooks.weapon_granted) ok = supply->hooks.weapon_granted(supply->hooks.context, actor, grants.weapons, grants.weapon_count, selection, e) && supply_current(supply, actor, e);
    grants_free(&grants); supply_leave(supply); return ok;
}

void qa_supply_preview_free(qa_supply_preview_result *result)
{
    if (!result) return;
    qa_inventory *owner = result->storage_owner;
    if (owner) qa_inventory_snapshot_release(owner, &result->storage);
    *result = (qa_supply_preview_result){0};
}

static bool preview_grant(qa_inventory *inventory, qa_actor_id actor,
    qa_inventory_entry *entries, size_t count, qa_pickup_grant grant,
    qa_pickup_receipt *receipt, qa_error *e)
{
    for (size_t i = 0; i < count; ++i) if (entries[i].item == grant.item) {
        bool writes;
        qa_inventory_entry after;
        qa_pickup_receipt result = {.item = grant.item, .before = entries[i].count};
        bool ok = inventory ? qa_inventory_preview_acquire(inventory, actor, &entries[i], grant.amount,
            &after, &result.given, &writes, e) :
            qa_inventory_preview_give(&entries[i], grant.amount, &after, &result.given, &writes, e);
        if (!ok) return false;
        if (writes) entries[i] = after;
        *receipt = result; return true;
    }
    return fail(e, QA_ERROR_NOT_FOUND, "Pickup preview destination was not admitted");
}

static bool preview_grants(qa_inventory *storage_owner, qa_inventory *source, qa_actor_id actor,
    const qa_inventory_entry *inventory, size_t count,
    const qa_pickup_grant_plan *plan, qa_supply_preview_result *out, qa_error *e)
{
    if (!storage_owner || !plan || !out || (count && !inventory) || (plan->weapon_count && !plan->weapons) ||
        (plan->ammo_count && !plan->ammo) || count > SIZE_MAX / sizeof(*inventory) ||
        plan->weapon_count > SIZE_MAX / sizeof(qa_pickup_receipt) || plan->ammo_count > SIZE_MAX / sizeof(qa_pickup_receipt) ||
        (plan->weapon_offer && plan->shared_weapon_ammo)) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup grant preview plan");
    qa_arena scratch;
    qa_inventory_scratch_begin(storage_owner, &scratch);
    qa_inventory_entry *entries = count ? qa_arena_alloc(&scratch,
        count * sizeof(*entries), _Alignof(qa_inventory_entry), e) : NULL;
    qa_supply_preview_result result = {.storage_owner = storage_owner};
    qa_inventory_scratch_acquire(storage_owner, &result.storage);
    size_t weapon_capacity = plan->shared_weapon_ammo ? plan->ammo_count : plan->weapon_count;
    result.weapons = weapon_capacity ? qa_arena_alloc(&result.storage,
        weapon_capacity * sizeof(*result.weapons), _Alignof(qa_pickup_receipt), e) : NULL;
    result.ammo = plan->ammo_count ? qa_arena_alloc(&result.storage,
        plan->ammo_count * sizeof(*result.ammo), _Alignof(qa_pickup_receipt), e) : NULL;
    bool ok = true;
    if ((count && !entries) || (weapon_capacity && !result.weapons) || (plan->ammo_count && !result.ammo)) {
        ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup grant preview"); goto done;
    }
    for (size_t i = 0; ok && i < count; ++i) {
        ok = qa_inventory_validate_entry(&inventory[i], &entries[i], e);
        for (size_t j = 0; ok && j < i; ++j) if (entries[j].item == entries[i].item)
            ok = fail(e, QA_ERROR_ARGUMENT, "Duplicate pickup preview inventory item");
    }
    for (size_t group = 0; ok && group < 2; ++group) {
        const qa_pickup_grant *grants = group ? plan->ammo : plan->weapons;
        size_t grant_count = group ? plan->ammo_count : plan->weapon_count;
        for (size_t i = 0; ok && i < grant_count; ++i) {
            bool found = false;
            for (size_t j = 0; j < count; ++j) if (entries[j].item == grants[i].item) { found = true; break; }
            if (!found) ok = fail(e, QA_ERROR_NOT_FOUND, "Pickup preview destination was not admitted");
            if (!group && plan->shared_weapon_ammo) {
                bool shared = false;
                for (size_t j = 0; j < plan->ammo_count; ++j) if (plan->ammo[j].item == grants[i].item) { shared = true; break; }
                if (!shared) ok = fail(e, QA_ERROR_ARGUMENT, "Shared pickup weapon has no ammo grant");
            }
        }
    }
    if (plan->weapon_offer) for (size_t i = 0; ok && i < plan->weapon_count; ++i)
        ok = preview_grant(source, actor, entries, count, plan->weapons[i], &result.weapons[result.weapon_count++], e);
    result.accepted = plan->weapon_offer;
    for (size_t i = 0; ok && i < plan->ammo_count; ++i) {
        ok = preview_grant(source, actor, entries, count, plan->ammo[i], &result.ammo[result.ammo_count++], e);
        if (ok) result.accepted |= plan->accept_nonzero ? result.ammo[i].given != 0 : result.ammo[i].given > 0;
    }
    if (ok && !plan->weapon_offer && result.accepted) {
        if (plan->shared_weapon_ammo) {
            for (size_t i = 0; i < result.ammo_count; ++i)
                for (size_t j = 0; j < plan->weapon_count; ++j)
                    if (result.ammo[i].item == plan->weapons[j].item) { result.weapons[result.weapon_count++] = result.ammo[i]; break; }
        } else for (size_t i = 0; ok && i < plan->weapon_count; ++i) {
            bool shared = false;
            for (size_t j = 0; j < result.ammo_count; ++j) if (result.ammo[j].item == plan->weapons[i].item) {
                result.weapons[result.weapon_count++] = result.ammo[j]; shared = true; break;
            }
            if (!shared) ok = preview_grant(source, actor, entries, count, plan->weapons[i], &result.weapons[result.weapon_count++], e);
        }
    }
done:
    qa_arena_destroy(&scratch);
    if (ok) *out = result;
    else qa_supply_preview_free(&result);
    return ok;
}

bool qa_pickup_preview_grants(qa_inventory *storage_owner, const qa_inventory_entry *inventory, size_t count,
    const qa_pickup_grant_plan *plan, qa_supply_preview_result *out, qa_error *e)
{
    return preview_grants(storage_owner, NULL, (qa_actor_id){0}, inventory, count, plan, out, e);
}

static bool preview_resolved(qa_supply *supply, qa_actor_id actor, const supply_grants *grants,
                              bool weapon_offer, qa_supply_preview_result *out, qa_error *e)
{
    qa_supply_preview_result result = {0};
    qa_arena scratch;
    qa_inventory_scratch_begin(supply->inventory, &scratch);
    qa_inventory_entry *entries = NULL;
    qa_pickup_grant *weapons = NULL;
    size_t count = 0;
    bool ok = require_entries(supply, actor, grants, e) &&
        qa_inventory_entries(supply->inventory, actor, NULL, 0, &count, e) && supply_current(supply, actor, e);
    if (!ok) goto done;
    if (count > SIZE_MAX / sizeof(*entries) || grants->weapon_count > SIZE_MAX / sizeof(*weapons)) {
        ok = fail(e, QA_ERROR_MEMORY, "Pickup preview size overflow"); goto done;
    }
    entries = qa_arena_alloc(&scratch, (count ? count : 1) * sizeof(*entries),
        _Alignof(qa_inventory_entry), e);
    weapons = grants->weapon_count ? qa_arena_alloc(&scratch,
        grants->weapon_count * sizeof(*weapons), _Alignof(qa_pickup_grant), e) : NULL;
    if (!entries || (grants->weapon_count && !weapons)) {
        ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup preview"); goto done;
    }
    ok = qa_inventory_entries(supply->inventory, actor, entries, count, &count, e) && supply_current(supply, actor, e);
    if (!ok) goto done;
    for (size_t i = 0; i < grants->weapon_count; ++i) weapons[i] = (qa_pickup_grant){grants->weapons[i], 1};
    qa_pickup_grant_plan plan = {.weapon_offer = weapon_offer, .weapons = weapons,
        .weapon_count = grants->weapon_count, .ammo = grants->ammo, .ammo_count = grants->ammo_count};
    ok = preview_grants(supply->inventory, supply->inventory, actor, entries, count, &plan, &result, e) &&
        supply_current(supply, actor, e);
done:
    qa_arena_destroy(&scratch);
    if (ok) *out = result;
    else qa_supply_preview_free(&result);
    return ok;
}
bool qa_supply_preview(qa_supply *supply, qa_actor_id actor, const qa_supply_offer *offer,
                        bool canonical, qa_supply_preview_result *out, qa_error *e)
{
    if(!out) return fail(e,QA_ERROR_ARGUMENT,"Missing supply preview result");
    if(!supply_available(supply,e)) return false;
    supply_enter(supply);
    supply_grants grants={0};grants_begin(supply,&grants);
    qa_supply_options options={.canonical=canonical};
    bool ok=supply_current(supply,actor,e) && resolve_offer(supply,offer,&options,&grants,e) &&
        preview_resolved(supply,actor,&grants,offer->kind==QA_SUPPLY_WEAPON,out,e);
    grants_free(&grants);supply_leave(supply);return ok;
}
bool qa_supply_cargo_preview(qa_supply *supply,qa_actor_id actor,const qa_pickup_cargo *cargo,
                              size_t count,bool canonical,qa_supply_preview_result *out,qa_error *e)
{
    if(!out) return fail(e,QA_ERROR_ARGUMENT,"Missing cargo preview result");
    if(!supply_available(supply,e) || !cargo_valid(cargo,count,e)) return false;
    supply_enter(supply);supply_grants grants={0};grants_begin(supply,&grants);
    bool ok=supply_current(supply,actor,e);
    for(size_t i=0;ok && i<count;++i)
        if(cargo[i].weapon) ok=resolve_weapon(supply,&grants,cargo[i].item,canonical,e);
        else ok=resolve_ammo(supply,&grants,(qa_pickup_grant){cargo[i].item,cargo[i].count},canonical,false,e);
    if(ok) ok=preview_resolved(supply,actor,&grants,true,out,e);
    grants_free(&grants);supply_leave(supply);return ok;
}

static qa_item_id source_owner(const qa_supply_source_owner *owners, size_t count, qa_item_id item)
{
    for (size_t i = 0; i < count; ++i) if (owners[i].item == item) return owners[i].source;
    return 0;
}

bool qa_supply_weapon_sources(const qa_supply_profile *profile,
    const qa_supply_weapon *selected, size_t selected_count, const qa_supply_weapon *original,
    size_t original_count, qa_arena *storage, qa_item_id *out, qa_error *e)
{
    if (!profile || (selected_count && (!selected || !out)) || (original_count && !original) ||
        (profile->weapon_count && !profile->weapons) || (profile->ammo_count && !profile->ammo) ||
        selected_count > SIZE_MAX / sizeof(*out)) return fail(e, QA_ERROR_ARGUMENT, "Invalid selected weapon source mapping");
    for (size_t i = 0; i < profile->weapon_count; ++i)
        if (profile->weapons[i].count && !profile->weapons[i].destinations) return fail(e, QA_ERROR_ARGUMENT, "Missing weapon supply destinations");
    for (size_t i = 0; i < profile->ammo_count; ++i)
        if (profile->ammo[i].count && !profile->ammo[i].destinations) return fail(e, QA_ERROR_ARGUMENT, "Missing ammo supply destinations");
    if (!owners_valid(profile->weapon_owners, profile->weapon_owner_count, profile->weapons, profile->weapon_count, e) ||
        !owners_unique(profile->ammo_owners, profile->ammo_owner_count, e)) return false;
    qa_item_id *sources = selected_count ? qa_arena_alloc(storage,
        selected_count * sizeof(*sources), _Alignof(qa_item_id), e) : NULL;
    if (selected_count && !sources) return fail(e, QA_ERROR_MEMORY, "Cannot allocate selected weapon source mapping");
    if (selected_count) memset(sources, 0, selected_count * sizeof(*sources));
    bool ok = true;
    for (size_t i = 0; ok && i < selected_count; ++i) {
        qa_supply_weapon weapon = selected[i];
        if (!weapon.item) { ok = fail(e, QA_ERROR_ARGUMENT, "Invalid selected weapon item"); break; }
        for (size_t j = 0; j < i; ++j) if (selected[j].item == weapon.item) ok = fail(e, QA_ERROR_ARGUMENT, "Duplicate selected weapon item");
        if (!ok || !weapon.drop) continue;
        for (size_t j = 0; j < original_count; ++j) if (original[j].item == weapon.item) { sources[i] = weapon.item; break; }
        if (sources[i]) continue;
        qa_item_id owner = source_owner(profile->weapon_owners, profile->weapon_owner_count, weapon.item);
        if (owner) {
            for (size_t j = 0; j < original_count; ++j) if (original[j].item == owner) { sources[i] = owner; break; }
            if (!sources[i]) ok = fail(e, QA_ERROR_NOT_FOUND, "Selected weapon names an unavailable original owner");
            continue;
        }
        size_t candidates = 0;
        for (size_t j = 0; j < original_count; ++j)
            if (mapping_contains(mapping_find(profile->weapons, profile->weapon_count, original[j].item), weapon.item)) {
                sources[i] = original[j].item; ++candidates;
            }
        if (!candidates && weapon.ammo) for (size_t j = 0; j < original_count; ++j)
            if (mapping_contains(mapping_find(profile->ammo, profile->ammo_count, original[j].ammo), weapon.ammo)) {
                sources[i] = original[j].item; ++candidates;
            }
        if (candidates > 1 && weapon.ammo) {
            qa_item_id ammo_owner = source_owner(profile->ammo_owners, profile->ammo_owner_count, weapon.ammo);
            size_t canonical_count = 0; qa_item_id canonical = 0;
            for (size_t j = 0; ammo_owner && j < original_count; ++j)
                if (original[j].ammo == ammo_owner && mapping_contains(mapping_find(profile->ammo, profile->ammo_count, ammo_owner), weapon.ammo)) {
                    ++canonical_count; canonical = original[j].item;
                }
            if (canonical_count == 1) { candidates = 1; sources[i] = canonical; }
        }
        if (candidates != 1) ok = fail(e, QA_ERROR_ARGUMENT, candidates ? "Selected weapon has ambiguous original drop mapping" : "Selected weapon has no original drop mapping");
    }
    if (ok && selected_count) memcpy(out, sources, selected_count * sizeof(*out));
    return ok;
}
