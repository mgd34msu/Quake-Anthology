#include "qa/persistence_gameplay.h"
#include "../gameplay/pickups_internal.h"
#include "lease_serials.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }

static bool signature(qa_source_save_io *io)
{
    unsigned char actual[8] = {'Q','A','P','I','C','K','U','P'};
    static const unsigned char expected[8] = {'Q','A','P','I','C','K','U','P'};
    uint32_t version = 1;
    return qa_source_save_bytes(io, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual)) &&
        qa_source_save_u32(io, &version) && version == 1;
}

static bool write_fields(qa_source_save_io *io, qa_pickup_write *write)
{
    uint32_t kind = write->resource.kind, fields = write->fields;
    if (!qa_source_save_u32(io, &kind) || kind < QA_PICKUP_INVENTORY || kind > QA_PICKUP_PROTECTION ||
        !qa_source_save_u32(io, &fields) || fields > QA_PICKUP_COUNT_CAPACITY) return false;
    write->resource.kind = (qa_pickup_resource_kind)kind; write->fields = (qa_pickup_fields)fields;
    if (kind == QA_PICKUP_INVENTORY)
        return qa_source_save_string(io, &write->resource.item) && write->resource.item;
    uint32_t channel = write->resource.channel;
    if (!qa_source_save_u32(io, &channel) || channel > QA_PROTECTION_POWERED) return false;
    write->resource.channel = (qa_protection_channel)channel; return true;
}

static bool same_write(qa_pickup_write a, qa_pickup_write b)
{
    return a.resource.kind == b.resource.kind && a.fields == b.fields &&
        (a.resource.kind == QA_PICKUP_INVENTORY ? a.resource.item == b.resource.item : a.resource.channel == b.resource.channel);
}

static bool rule_fields(qa_source_save_io *io, qa_pickups *service, pickup_registration *registration,
                         pickup_rule *owned, const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_pickup_rule *rule = &owned->rule, source = {0};
    bool preview = rule->preview != NULL;
    if (!qa_source_save_u32(io, &rule->id) || !rule->id || !qa_source_save_bool(io, &preview) ||
        !qa_source_save_count(io, &rule->offered_count, SIZE_MAX / sizeof(qa_item_id)) || !rule->offered_count ||
        !qa_source_save_count(io, &rule->write_count, SIZE_MAX / sizeof(qa_pickup_write)) || !rule->write_count) return false;
    if (reading) {
        qa_item_id *offered = calloc(rule->offered_count, sizeof(*offered));
        qa_pickup_write *writes = calloc(rule->write_count, sizeof(*writes));
        owned->storage = calloc(rule->write_count, sizeof(*owned->storage));
        rule->offered = offered; rule->writes = writes;
        if (!offered || !writes || !owned->storage) {
            qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating restored pickup rule arrays"); return false;
        }
        if (!resolve || !resolve->pickup_rule ||
            !resolve->pickup_rule(resolve->context, registration->actor, registration->owner,
                                   registration->serial, rule->id, &source, io->error) ||
            source.id != rule->id || !source.take || (source.preview != NULL) != preview ||
            source.offered_count != rule->offered_count || source.write_count != rule->write_count ||
            !source.offered || !source.writes)
            return fail(io->error, "Restored pickup rule has no matching source descriptor");
        rule->context = source.context; rule->take = source.take; rule->preview = source.preview;
    } else if (!rule->take) return fail(io->error, "Pickup rule lacks its source callback");
    for (size_t i = 0; i < rule->offered_count; ++i) {
        qa_item_id value = reading ? 0 : rule->offered[i];
        if (!qa_source_save_string(io, &value) || !value || (reading && value != source.offered[i])) return false;
        if (reading) ((qa_item_id *)rule->offered)[i] = value;
    }
    for (size_t i = 0; i < rule->write_count; ++i) {
        qa_pickup_write value = reading ? (qa_pickup_write){0} : rule->writes[i];
        bool claimed = !reading && value.resource.kind == QA_PICKUP_INVENTORY &&
            qa_inventory_pickup_current(service->inventory, registration->actor, value.resource.item, registration);
        bool current = !reading && qa_pickups_checkpoint_write_current(service, registration, &value, owned->storage[i]);
        if (!write_fields(io, &value) || (reading && !same_write(value, source.writes[i])) ||
            !qa_source_save_u64(io, &owned->storage[i]) || !owned->storage[i] ||
            !qa_source_save_bool(io, &claimed) || !qa_source_save_bool(io, &current)) return false;
        if (reading) {
            ((qa_pickup_write *)rule->writes)[i] = value;
            if (claimed && (value.resource.kind != QA_PICKUP_INVENTORY ||
                !qa_inventory_pickup_claim(service->inventory, registration->actor, value.resource.item, registration, io->error)))
                return fail(io->error, "Saved pickup inventory claim conflicts with restored owner");
            if (current != qa_pickups_checkpoint_write_current(service, registration, &value, owned->storage[i]))
                return fail(io->error, "Saved pickup resource lease differs from restored storage");
        }
    }
    return true;
}

static bool registration_fields(qa_source_save_io *io, qa_pickups *service, pickup_registration *registration,
                                  const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_actor(io, &registration->actor) || !qa_actors_get(service->actors, registration->actor) ||
        !qa_source_save_string(io, &registration->owner) || !qa_source_save_u64(io, &registration->serial) ||
        !registration->serial || !qa_source_save_count(io, &registration->count, SIZE_MAX / sizeof(*registration->rules)) ||
        !registration->count) return false;
    if (reading) {
        registration->rules = calloc(registration->count, sizeof(*registration->rules));
        if (!registration->rules) {
            registration->count = 0; qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating restored pickup registration"); return false;
        }
        registration->active = true;
    }
    for (size_t i = 0; i < registration->count; ++i)
        if (!rule_fields(io, service, registration, registration->rules + i, resolve)) return false;
    return true;
}

static bool observation_fields(qa_source_save_io *io, qa_pickups *service, pickup_observation_owner *owner,
                                const qa_persistence_gameplay_resolvers *resolve)
{
    if (!qa_source_save_actor(io, &owner->actor) || !qa_actors_get(service->actors, owner->actor) ||
        !qa_source_save_string(io, &owner->owner) || !owner->owner ||
        !qa_source_save_u64(io, &owner->serial) || !owner->serial) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!resolve || !resolve->pickup_observer ||
            !resolve->pickup_observer(resolve->context, owner->actor, owner->owner, owner->serial, &owner->observer, io->error) ||
            !owner->observer.inspect)
            return fail(io->error, "Saved pickup observation has no restored source descriptor");
        owner->active = true;
    }
    return owner->active && owner->observer.inspect;
}

static bool safe(qa_session *session, qa_pickups *service, qa_error *error)
{
    return (session && service && service->actors == qa_session_actors(session) && qa_pickups_idle(service) &&
        !service->observation_calls && !service->retired_observations) ||
        fail(error, "Pickup persistence requires idle candidate-owned registrations");
}

static bool claims_owned(qa_pickups *service, qa_error *error)
{
    for (uint32_t i = 0; i < service->inventory->capacity; ++i) {
        inventory_store *store = service->inventory->stores[i];
        if (!store) continue;
        for (pickup_claim *claim = store->pickups; claim; claim = claim->next) {
            bool found = false;
            for (pickup_registration *r = service->registrations; r && !found; r = r->next) {
                if (!r->active || r != claim->token || !qa_actor_id_equal(r->actor, store->actor)) continue;
                for (size_t j = 0; j < r->count; ++j)
                    for (size_t k = 0; k < r->rules[j].rule.write_count; ++k) {
                        qa_pickup_resource resource = r->rules[j].rule.writes[k].resource;
                        if (resource.kind == QA_PICKUP_INVENTORY && resource.item == claim->item) found = true;
                    }
            }
            if (!found) return fail(error, "Inventory pickup claim has no saved registration owner");
        }
    }
    return true;
}

bool qa_persistence_pickups_capture(qa_session *session, qa_pickups *service, qa_buffer *out, qa_error *error)
{
    if (!out || !safe(session, service, error) || !claims_owned(service, error)) return false;
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, session, error)) return false;
    size_t registrations = 0, observations = 0;
    for (pickup_registration *r = service->registrations; r; r = r->next) ++registrations;
    for (uint32_t i = 0; i < service->observation_capacity; ++i) if (service->observations[i]) ++observations;
    uint64_t serial = service->serial; bool eligible = service->eligible != NULL;
    bool ok = signature(&io) && qa_source_save_u64(&io, &serial) && qa_source_save_bool(&io, &eligible) &&
        qa_source_save_count(&io, &registrations, SIZE_MAX / sizeof(pickup_registration));
    for (pickup_registration *r = service->registrations; ok && r; r = r->next) {
        /* Claim tokens refer to the actual registration's address. */
        if (!r->active) ok = fail(error, "Pickup registration is pending retirement");
        else ok = registration_fields(&io, service, r, NULL);
    }
    if (ok) ok = qa_source_save_count(&io, &observations, qa_actors_capacity(service->actors));
    for (uint32_t i = 0; ok && i < service->observation_capacity; ++i) if (service->observations[i]) {
        pickup_observation_owner copy = *service->observations[i];
        ok = observation_fields(&io, service, &copy, NULL);
    }
    if (ok) ok = safe(session, service, error) && serial == service->serial && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid pickup continuation");
    qa_source_save_dispose(&io); return ok;
}

bool qa_persistence_pickups_restore(qa_session *session, qa_pickups *service,
    const qa_persistence_gameplay_resolvers *resolve, qa_bytes bytes, qa_error *error)
{
    if (!safe(session, service, error)) return false;
    qa_pickups *scratch = NULL;
    if (!qa_pickups_create(service->actors, service->combat, service->inventory, &scratch, error)) return false;
    qa_source_save_io io = {0}; size_t count = 0; bool eligible = false;
    uint64_t *serials = NULL; size_t serial_count = 0, serial_capacity = 0;
    bool ok = qa_source_save_reader(&io, session, bytes, error) && signature(&io) &&
        qa_source_save_u64(&io, &scratch->serial) && qa_source_save_bool(&io, &eligible) &&
        eligible == (service->eligible != NULL) && qa_source_save_count(&io, &count, SIZE_MAX / sizeof(pickup_registration));
    pickup_registration **link = &scratch->registrations;
    for (size_t i = 0; ok && i < count; ++i) {
        *link = calloc(1, sizeof(**link));
        if (!*link) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating restored pickup owner"); ok = false; break; }
        ok = registration_fields(&io, scratch, *link, resolve) && (*link)->serial <= scratch->serial;
        if (ok) ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, (*link)->serial, scratch->serial, error);
        for (pickup_registration *r = scratch->registrations; ok && r != *link; r = r->next)
            if (r->serial <= (*link)->serial) ok = fail(error, "Saved pickup registration order is invalid");
        link = &(*link)->next;
    }
    if (ok) ok = qa_source_save_count(&io, &count, qa_actors_capacity(service->actors));
    scratch->observation_capacity = qa_actors_capacity(service->actors);
    scratch->observations = calloc(scratch->observation_capacity, sizeof(*scratch->observations));
    if (!scratch->observations) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating restored pickup observations"); ok = false; scratch->observation_capacity = 0; }
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        pickup_observation_owner *owner = calloc(1, sizeof(*owner));
        if (!owner) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating restored pickup observer"); ok = false; break; }
        ok = observation_fields(&io, scratch, owner, resolve) && owner->serial <= scratch->serial &&
            (!i || owner->actor.slot > previous);
        if (ok) ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, owner->serial, scratch->serial, error);
        if (ok) { previous = owner->actor.slot; scratch->observations[owner->actor.slot] = owner; }
        else free(owner);
    }
    if (ok) ok = persistence_serial_unique(serials, serial_count, error) && qa_source_save_finish(&io, NULL) && safe(session, service, error);
    if (ok) {
        pickup_registration *old = service->registrations;
        pickup_observation_owner **old_observers = service->observations;
        uint32_t old_capacity = service->observation_capacity;
        service->registrations = scratch->registrations; service->observations = scratch->observations;
        service->observation_capacity = scratch->observation_capacity; service->serial = scratch->serial;
        scratch->registrations = old; scratch->observations = old_observers; scratch->observation_capacity = old_capacity;
    }
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid saved pickup continuation");
    free(serials); qa_source_save_dispose(&io); (void)qa_pickups_destroy(scratch, NULL); return ok;
}
