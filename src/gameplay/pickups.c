/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "inventory_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct pickup_rule {
    qa_pickup_rule rule;
    uint64_t *storage;
} pickup_rule;

typedef struct pickup_registration {
    struct pickup_registration *next;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint64_t serial;
    bool active;
    pickup_rule *rules;
    size_t count;
} pickup_registration;

typedef enum pickup_consumption { PICKUP_LIVE, PICKUP_REMOVING, PICKUP_CONSUMED } pickup_consumption;
struct qa_pickup_execution {
    struct qa_pickup_execution *previous;
    qa_pickups *service;
    qa_pickup_offer offer;
    pickup_registration *registration;
    pickup_rule *rule;
    qa_pickup_selection selection;
    qa_combat_pickup_scope combat_scope;
    pickup_consumption consumption;
    bool open, accepted, grant_used, observer_open, source_scope, failed;
    qa_error failure;
};

struct qa_pickups {
    qa_actor_registry *actors;
    qa_combat *combat;
    qa_inventory *inventory;
    pickup_registration *registrations;
    qa_pickup_execution *scopes;
    uint64_t serial;
    size_t calls;
    void *eligibility_context;
    bool (*eligible)(void *, const qa_pickup_offer *, bool *, qa_error *);
};

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

static void registration_free(pickup_registration *registration)
{
    for (size_t i = 0; i < registration->count; ++i) {
        free((void *)registration->rules[i].rule.offered);
        free((void *)registration->rules[i].rule.writes);
        free(registration->rules[i].storage);
    }
    free(registration->rules); free(registration);
}

static void sweep(qa_pickups *service)
{
    if (service->calls) return;
    pickup_registration **link = &service->registrations;
    while (*link) {
        pickup_registration *registration = *link;
        if (!registration->active) {
            *link = registration->next;
            qa_inventory_pickup_release(service->inventory, registration->actor, registration);
            registration_free(registration);
        } else link = &registration->next;
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
    if (qa_inventory_item_owner(service->inventory, registration->actor, write->resource.item, &owner, NULL) && owner != registration->owner) return false;
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
    sweep(service); free(service); return true;
}

void qa_pickups_actor_released(qa_pickups *service, qa_actor_record actor)
{
    if (!service) return;
    for (pickup_registration *r = service->registrations; r; r = r->next)
        if (qa_actor_id_equal(r->actor, actor.id)) r->active = false;
    sweep(service);
}

static bool capture_rules(pickup_registration *registration, const qa_pickup_rule *rules, size_t count, qa_error *e)
{
    if (!rules || !count || count > SIZE_MAX / sizeof(*registration->rules)) return fail(e, QA_ERROR_ARGUMENT, "Pickup rules require a nonempty set");
    registration->rules = calloc(count, sizeof(*registration->rules));
    if (!registration->rules) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup rules");
    registration->count = count;
    for (size_t i = 0; i < count; ++i) {
        const qa_pickup_rule *rule = &rules[i];
        pickup_rule *copy = &registration->rules[i];
        if (!rule->id || !rule->take || !rule->offered_count || !rule->offered || !rule->write_count || !rule->writes ||
            rule->offered_count > SIZE_MAX / sizeof(*rule->offered) || rule->write_count > SIZE_MAX / sizeof(*rule->writes) ||
            rule->write_count > SIZE_MAX / sizeof(*copy->storage)) return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup rule");
        for (size_t j = 0; j < i; ++j) if (registration->rules[j].rule.id == rule->id)
            return fail(e, QA_ERROR_ARGUMENT, "Duplicate original pickup rule ID");
        qa_item_id *offered = malloc(rule->offered_count * sizeof(*offered));
        qa_pickup_write *writes = malloc(rule->write_count * sizeof(*writes));
        copy->storage = calloc(rule->write_count, sizeof(*copy->storage));
        if (!offered || !writes || !copy->storage) { free(offered); free(writes); return fail(e, QA_ERROR_MEMORY, "Cannot copy pickup rule declarations"); }
        copy->rule = *rule; copy->rule.offered = offered; copy->rule.writes = writes;
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

static bool registration_conflicts(qa_pickups *service, pickup_registration *candidate, qa_error *e)
{
    for (pickup_registration *r = service->registrations; r; r = r->next) {
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
    pickup_registration *registration = calloc(1, sizeof(*registration));
    if (!registration) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup registration");
    registration->actor = actor; registration->owner = owner; registration->active = true;
    qa_combat_pickup_scope combat_scope;
    if (!qa_combat_pickup_begin(service->combat, actor, &combat_scope, e)) { free(registration); return false; }
    qa_inventory_hold(service->inventory);
    ++service->calls;
    bool ok = capture_rules(registration, rules, count, e) && registration_conflicts(service, registration, e);
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
                if (ok && qa_inventory_item_owner(service->inventory, actor, write->resource.item, &resource_owner, NULL) && resource_owner != owner)
                    ok = fail(e, QA_ERROR_ARGUMENT, "Original pickup item belongs to another source");
                if (ok && write->fields != QA_PICKUP_COUNT && !qa_inventory_mutable_capacity(service->inventory, actor, write->resource.item))
                    ok = fail(e, QA_ERROR_ARGUMENT, "Original pickup destination has no mutable capacity");
                if (ok) ok = qa_inventory_pickup_claim(service->inventory, actor, write->resource.item, registration, e);
            }
        }
    }
    if (ok && !registration_current(service, registration)) ok = fail(e, QA_ERROR_NOT_FOUND, "Pickup resources retired during registration");
    if (ok) ok = registration_conflicts(service, registration, e);
    if (ok && service->serial == UINT64_MAX) ok = fail(e, QA_ERROR_ARGUMENT, "Pickup registration identity exhausted");
    if (ok) {
        registration->serial = ++service->serial;
        registration->next = service->registrations; service->registrations = registration;
        *out = (qa_pickup_lease){actor, registration->serial};
    } else { qa_inventory_pickup_release(service->inventory, actor, registration); registration_free(registration); }
    qa_inventory_unhold(service->inventory);
    qa_combat_pickup_end(&combat_scope, false, NULL);
    --service->calls; sweep(service); return ok;
}

bool qa_pickups_close(qa_pickups *service, qa_pickup_lease lease, qa_error *e)
{
    if (!service) return fail(e, QA_ERROR_ARGUMENT, "Missing pickup service");
    for (pickup_registration *r = service->registrations; r; r = r->next)
        if (r->serial == lease.serial && qa_actor_id_equal(r->actor, lease.actor)) {
            r->active = false; qa_inventory_pickup_release(service->inventory, r->actor, r); break;
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
    if (!offer->item || (offer->override_count && !isfinite(offer->count)) || !resource_valid(offer->default_resource, true) ||
        offer->grant < QA_PICKUP_RESOURCE_GRANT || offer->grant > QA_PICKUP_SOURCE_EFFECT || !cargo_valid(offer->cargo, offer->cargo_count, e))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid original pickup offer");
    if (offer->cargo_count) {
        qa_pickup_cargo *cargo = malloc(offer->cargo_count * sizeof(*cargo));
        if (!cargo) return fail(e, QA_ERROR_MEMORY, "Cannot capture pickup cargo");
        memcpy(cargo, offer->cargo, offer->cargo_count * sizeof(*cargo)); execution->offer.cargo = cargo;
    } else execution->offer.cargo = NULL;
    if (!qa_combat_pickup_begin(service->combat, offer->recipient, &execution->combat_scope, e)) {
        free((void *)execution->offer.cargo); return false;
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
    free((void *)execution->offer.cargo);
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
    for (pickup_registration *r = service->registrations; r; r = r->next) {
        if (!r->active || !qa_actor_id_equal(r->actor, execution->offer.recipient)) continue;
        for (size_t i = 0; i < r->count; ++i) {
            pickup_rule *rule = &r->rules[i];
            bool offered = false;
            for (size_t j = 0; j < rule->rule.offered_count; ++j) if (rule->rule.offered[j] == execution->offer.item) { offered = true; break; }
            if (!offered) continue;
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
    if (!callback) return fail(e, QA_ERROR_ARGUMENT, "Missing original pickup source callback");
    qa_pickup_execution execution;
    if (!scope_open(service, offer, &execution, e)) return false;
    if (!execution.open) {
        qa_combat_pickup_scope combat_scope;
        if (!qa_combat_pickup_begin(service->combat, offer->recipient, &combat_scope, e)) return false;
        qa_inventory_hold(service->inventory);
        ++service->calls;
        bool ok = callback(context, offer, QA_PICKUP_SELECT_STALE, &execution, e);
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
    qa_inventory *inventory;
    qa_supply_profile profile;
    qa_supply_hooks hooks;
    size_t calls;
    bool closed;
};

typedef struct supply_grants {
    qa_item_id *weapons;
    size_t weapon_count, weapon_capacity;
    qa_pickup_grant *ammo;
    size_t ammo_count, ammo_capacity;
    bool *exact;
    bool acceptance_override, accepted;
} supply_grants;

static void mappings_free(const qa_supply_mapping *mappings, size_t count)
{
    if (!mappings) return;
    for (size_t i = 0; i < count; ++i) free((void *)mappings[i].destinations);
    free((void *)mappings);
}

static void supply_free(qa_supply *supply)
{
    mappings_free(supply->profile.weapons, supply->profile.weapon_count);
    mappings_free(supply->profile.ammo, supply->profile.ammo_count);
    free((void *)supply->profile.weapon_owners); free((void *)supply->profile.ammo_owners); free(supply);
}

static void supply_leave(qa_supply *supply)
{
    qa_inventory_unhold(supply->inventory);
    if (--supply->calls == 0 && supply->closed) supply_free(supply);
}

static void supply_enter(qa_supply *supply)
{ ++supply->calls; qa_inventory_hold(supply->inventory); }

static bool supply_current(qa_supply *supply, qa_error *e)
{ return (supply && !supply->closed) || fail(e, QA_ERROR_NOT_FOUND, "Pickup supply service retired"); }

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

static bool mappings_copy(const qa_supply_mapping *input, size_t count,
                           const qa_supply_mapping **out, qa_error *e)
{
    if ((count && !input) || count > SIZE_MAX / sizeof(*input)) return fail(e, QA_ERROR_ARGUMENT, "Invalid supply mappings");
    qa_supply_mapping *copy = count ? calloc(count, sizeof(*copy)) : NULL;
    if (count && !copy) return fail(e, QA_ERROR_MEMORY, "Cannot allocate supply mappings");
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
        qa_item_id *destinations = malloc(input[i].count * sizeof(*destinations));
        if (!destinations) { ok = fail(e, QA_ERROR_MEMORY, "Cannot copy supply destinations"); break; }
        memcpy(destinations, input[i].destinations, input[i].count * sizeof(*destinations));
        copy[i] = input[i]; copy[i].destinations = destinations;
    }
    if (!ok) mappings_free(copy, count);
    else *out = copy;
    return ok;
}

static bool owners_valid(const qa_supply_source_owner *owners, size_t count,
                          const qa_supply_mapping *mappings, size_t mapping_count, qa_error *e)
{
    if ((count && !owners) || count > SIZE_MAX / sizeof(*owners)) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup source owners");
    for (size_t i = 0; i < count; ++i) {
        if (!mapping_contains(mapping_find(mappings, mapping_count, owners[i].source), owners[i].item))
            return fail(e, QA_ERROR_ARGUMENT, "Selected item has an invalid original owner");
        for (size_t j = 0; j < i; ++j) if (owners[j].item == owners[i].item)
            return fail(e, QA_ERROR_ARGUMENT, "Selected item has duplicate original owners");
    }
    return true;
}

static bool owners_copy(const qa_supply_source_owner *input, size_t count,
                         const qa_supply_source_owner **out, qa_error *e)
{
    qa_supply_source_owner *copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) return fail(e, QA_ERROR_MEMORY, "Cannot allocate supply source owners");
    if (count) memcpy(copy, input, count * sizeof(*copy));
    *out = copy; return true;
}

bool qa_supply_create(qa_inventory *inventory, const qa_supply_profile *profile,
                       const qa_supply_hooks *hooks, qa_supply **out, qa_error *e)
{
    if (!inventory || !profile || !out) return fail(e, QA_ERROR_ARGUMENT, "Invalid supply service arguments");
    qa_supply *supply = calloc(1, sizeof(*supply));
    if (!supply) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup supply service");
    supply->inventory = inventory;
    if (hooks) supply->hooks = *hooks;
    supply->profile.weapon_count = profile->weapon_count; supply->profile.ammo_count = profile->ammo_count;
    supply->profile.weapon_owner_count = profile->weapon_owner_count; supply->profile.ammo_owner_count = profile->ammo_owner_count;
    bool ok = mappings_copy(profile->weapons, profile->weapon_count, &supply->profile.weapons, e) &&
        mappings_copy(profile->ammo, profile->ammo_count, &supply->profile.ammo, e) &&
        owners_valid(profile->weapon_owners, profile->weapon_owner_count, supply->profile.weapons, profile->weapon_count, e) &&
        owners_valid(profile->ammo_owners, profile->ammo_owner_count, supply->profile.ammo, profile->ammo_count, e) &&
        owners_copy(profile->weapon_owners, profile->weapon_owner_count, &supply->profile.weapon_owners, e) &&
        owners_copy(profile->ammo_owners, profile->ammo_owner_count, &supply->profile.ammo_owners, e);
    if (!ok) { supply_free(supply); return false; }
    *out = supply; return true;
}

void qa_supply_destroy(qa_supply *supply)
{
    if (!supply || supply->closed) return;
    supply->closed = true;
    if (!supply->calls) supply_free(supply);
}

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

static bool buffer_grow(void **buffer, size_t *capacity, size_t count, size_t element, qa_error *e)
{
    if (count <= *capacity) return true;
    size_t next = *capacity ? *capacity : 8;
    while (next < count && next <= SIZE_MAX / 2) next *= 2;
    if (next < count || next > SIZE_MAX / element) return fail(e, QA_ERROR_MEMORY, "Pickup grant size overflow");
    void *allocation = realloc(*buffer, next * element);
    if (!allocation) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup grants");
    *buffer = allocation; *capacity = next; return true;
}

static void grants_free(supply_grants *grants)
{ free(grants->weapons); free(grants->ammo); free(grants->exact); *grants = (supply_grants){0}; }

static bool append_weapon(supply_grants *grants, qa_item_id item, qa_error *e)
{
    if (!item) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup weapon");
    for (size_t i = 0; i < grants->weapon_count; ++i) if (grants->weapons[i] == item) return true;
    void *buffer = grants->weapons;
    if (!buffer_grow(&buffer, &grants->weapon_capacity, grants->weapon_count + 1, sizeof(*grants->weapons), e)) return false;
    grants->weapons = buffer; grants->weapons[grants->weapon_count++] = item; return true;
}

static bool append_ammo(supply_grants *grants, qa_pickup_grant grant, qa_error *e)
{
    if (!grant.item) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup ammunition");
    void *buffer = grants->ammo;
    if (!buffer_grow(&buffer, &grants->ammo_capacity, grants->ammo_count + 1, sizeof(*grants->ammo), e)) return false;
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
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->weapons[i], &entry, e) || !supply_current(supply, e)) return false;
    for (size_t i = 0; i < grants->ammo_count; ++i)
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->ammo[i].item, &entry, e) || !supply_current(supply, e)) return false;
    return true;
}

static bool resolve_quantities(qa_supply *supply, qa_actor_id actor, supply_grants *grants,
                                const qa_supply_options *options, qa_error *e)
{
    if (!options->quantity) return true;
    grants->exact = grants->ammo_count ? calloc(grants->ammo_count, sizeof(*grants->exact)) : NULL;
    if (grants->ammo_count && !grants->exact) return fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup quantity decisions");
    for (size_t i = 0; i < grants->ammo_count; ++i) {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(supply->inventory, actor, grants->ammo[i].item, &entry, e) || !supply_current(supply, e)) return false;
        qa_supply_quantity quantity = {0};
        if (!options->quantity(options->quantity_context, &entry, &quantity, e) || !supply_current(supply, e)) return false;
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
        if (!qa_inventory_entry_read(supply->inventory, actor, grant.item, &entry, e) || !supply_current(supply, e)) return false;
        receipts[i] = (qa_pickup_receipt){.item = grant.item, .before = entry.count};
        if (grants->exact && grants->exact[i]) {
            entry.count += grant.amount;
            if (!qa_inventory_configure(supply->inventory, actor, &entry, NULL, NULL, e) || !supply_current(supply, e) ||
                !qa_inventory_entry_read(supply->inventory, actor, grant.item, &entry, e) || !supply_current(supply, e)) return false;
            receipts[i].given = entry.count - receipts[i].before;
        } else if (!qa_inventory_give(supply->inventory, actor, grant.item, grant.amount, &receipts[i].given, e) || !supply_current(supply, e)) return false;
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
    qa_pickup_receipt *receipts = grants->ammo_count ? calloc(grants->ammo_count, sizeof(*receipts)) : NULL;
    if (grants->ammo_count && !receipts) ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup grant receipts");
    if (!ok) { free(receipts); return false; }
    if (weapon_offer) for (size_t i = 0; ok && i < grants->weapon_count; ++i) {
        double given;
        ok = qa_inventory_give(supply->inventory, actor, grants->weapons[i], 1, &given, e) && supply_current(supply, e);
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
                if (!shared) { double given; ok = qa_inventory_give(supply->inventory, actor, grants->weapons[i], 1, &given, e) && supply_current(supply, e); }
            }
        }
        if (ok && grants->weapon_count && supply->hooks.weapon_granted) {
            bool select = weapon_offer || !options->only_empty;
            for (size_t i = 0; i < grants->ammo_count; ++i) select |= receipts[i].before == 0 && receipts[i].given > 0;
            ok = supply->hooks.weapon_granted(supply->hooks.context, actor, grants->weapons, grants->weapon_count,
                select ? options->selection : QA_PICKUP_SWITCH_NEVER, e);
        } else if (ok && !weapon_offer && !grants->weapon_count && supply->hooks.ammo_granted)
            ok = supply->hooks.ammo_granted(supply->hooks.context, actor, receipts, grants->ammo_count, options->auto_switch, e);
    }
    free(receipts);
    if (ok) *accepted = taken;
    return ok;
}

bool qa_supply_apply(qa_supply *supply, qa_actor_id actor, const qa_supply_offer *offer,
                      const qa_supply_options *options, bool *accepted, qa_error *e)
{
    if (!options || !accepted) return fail(e, QA_ERROR_ARGUMENT, "Missing pickup supply options or result");
    if (!selection_valid(options->selection, e) || !supply_current(supply, e)) return false;
    qa_supply_options captured = *options;
    supply_enter(supply);
    supply_grants grants = {0};
    bool ok = resolve_offer(supply, offer, &captured, &grants, e) &&
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
                      qa_pickup_selection_mode selection, bool canonical, qa_error *e)
{
    if (!supply_current(supply, e) || !selection_valid(selection, e) || !cargo_valid(cargo, count, e)) return false;
    supply_enter(supply);
    supply_grants grants = {0}; bool ok = true;
    for (size_t i = 0; ok && i < count; ++i)
        if (cargo[i].weapon) ok = resolve_weapon(supply, &grants, cargo[i].item, canonical, e);
        else ok = resolve_ammo(supply, &grants, (qa_pickup_grant){cargo[i].item, cargo[i].count}, canonical, false, e);
    qa_supply_options options = {.selection = selection, .canonical = canonical}; bool accepted;
    if (ok) ok = grant_resolved(supply, actor, &grants, true, &options, &accepted, e);
    grants_free(&grants); supply_leave(supply); return ok;
}

bool qa_supply_owns(qa_supply *supply, qa_actor_id actor, qa_item_id item, bool *owned, qa_error *e)
{
    if (!owned) return fail(e, QA_ERROR_ARGUMENT, "Missing supply ownership result");
    if (!supply_current(supply, e)) return false;
    supply_enter(supply);
    const qa_supply_mapping *mapping = destinations(supply, item, true, e);
    bool ok = mapping != NULL, all = true;
    for (size_t i = 0; ok && i < mapping->count; ++i) {
        qa_inventory_entry entry;
        qa_error read_error = {0};
        if (!qa_inventory_entry_read(supply->inventory, actor, mapping->destinations[i], &entry, &read_error)) {
            if (read_error.code == QA_ERROR_NOT_FOUND) { all = false; break; }
            if (e) *e = read_error;
            ok = false; break;
        }
        ok = supply_current(supply, e);
        if (entry.count <= 0) { all = false; break; }
    }
    if (ok) *owned = all;
    supply_leave(supply); return ok;
}

bool qa_supply_select_weapon(qa_supply *supply, qa_actor_id actor, qa_item_id item,
                              qa_pickup_selection_mode selection, qa_error *e)
{
    if (!selection_valid(selection, e) || !supply_current(supply, e)) return false;
    supply_enter(supply);
    supply_grants grants = {0};
    bool ok = resolve_weapon(supply, &grants, item, false, e) && require_entries(supply, actor, &grants, e);
    if (ok && supply->hooks.weapon_granted) ok = supply->hooks.weapon_granted(supply->hooks.context, actor, grants.weapons, grants.weapon_count, selection, e);
    grants_free(&grants); supply_leave(supply); return ok;
}

void qa_supply_preview_free(qa_supply_preview_result *result)
{ if (result) { free(result->weapons); free(result->ammo); *result = (qa_supply_preview_result){0}; } }

static bool preview_grant(qa_inventory_entry *entries, size_t count, qa_pickup_grant grant,
                           qa_pickup_receipt *receipt, qa_error *e)
{
    for (size_t i = 0; i < count; ++i) if (entries[i].item == grant.item) {
        bool writes;
        qa_inventory_entry after;
        qa_pickup_receipt result = {.item = grant.item, .before = entries[i].count};
        if (!qa_inventory_preview_give(&entries[i], grant.amount, &after, &result.given, &writes, e)) return false;
        if (writes) entries[i] = after;
        *receipt = result; return true;
    }
    return fail(e, QA_ERROR_NOT_FOUND, "Pickup preview destination was not admitted");
}

bool qa_pickup_preview_grants(const qa_inventory_entry *inventory, size_t count,
    const qa_pickup_grant_plan *plan, qa_supply_preview_result *out, qa_error *e)
{
    if (!plan || !out || (count && !inventory) || (plan->weapon_count && !plan->weapons) ||
        (plan->ammo_count && !plan->ammo) || count > SIZE_MAX / sizeof(*inventory) ||
        plan->weapon_count > SIZE_MAX / sizeof(qa_pickup_receipt) || plan->ammo_count > SIZE_MAX / sizeof(qa_pickup_receipt) ||
        (plan->weapon_offer && plan->shared_weapon_ammo)) return fail(e, QA_ERROR_ARGUMENT, "Invalid pickup grant preview plan");
    qa_inventory_entry *entries = count ? malloc(count * sizeof(*entries)) : NULL;
    qa_supply_preview_result result = {0};
    size_t weapon_capacity = plan->shared_weapon_ammo ? plan->ammo_count : plan->weapon_count;
    result.weapons = weapon_capacity ? calloc(weapon_capacity, sizeof(*result.weapons)) : NULL;
    result.ammo = plan->ammo_count ? calloc(plan->ammo_count, sizeof(*result.ammo)) : NULL;
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
        ok = preview_grant(entries, count, plan->weapons[i], &result.weapons[result.weapon_count++], e);
    result.accepted = plan->weapon_offer;
    for (size_t i = 0; ok && i < plan->ammo_count; ++i) {
        ok = preview_grant(entries, count, plan->ammo[i], &result.ammo[result.ammo_count++], e);
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
            if (!shared) ok = preview_grant(entries, count, plan->weapons[i], &result.weapons[result.weapon_count++], e);
        }
    }
done:
    free(entries);
    if (ok) *out = result;
    else qa_supply_preview_free(&result);
    return ok;
}

bool qa_supply_preview(qa_supply *supply, qa_actor_id actor, const qa_supply_offer *offer,
                        bool canonical, qa_supply_preview_result *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing supply preview result");
    if (!supply_current(supply, e)) return false;
    supply_enter(supply);
    supply_grants grants = {0};
    qa_supply_preview_result result = {0};
    qa_inventory_entry *entries = NULL;
    qa_pickup_grant *weapons = NULL;
    qa_supply_options options = {.canonical = canonical};
    size_t count = 0;
    bool ok = resolve_offer(supply, offer, &options, &grants, e) && require_entries(supply, actor, &grants, e) &&
        qa_inventory_entries(supply->inventory, actor, NULL, 0, &count, e) && supply_current(supply, e);
    if (!ok) goto done;
    if (count > SIZE_MAX / sizeof(*entries) || grants.weapon_count > SIZE_MAX / sizeof(*weapons)) {
        ok = fail(e, QA_ERROR_MEMORY, "Pickup preview size overflow"); goto done;
    }
    entries = malloc((count ? count : 1) * sizeof(*entries));
    weapons = grants.weapon_count ? calloc(grants.weapon_count, sizeof(*weapons)) : NULL;
    if (!entries || (grants.weapon_count && !weapons)) {
        ok = fail(e, QA_ERROR_MEMORY, "Cannot allocate pickup preview"); goto done;
    }
    ok = qa_inventory_entries(supply->inventory, actor, entries, count, &count, e) && supply_current(supply, e);
    if (!ok) goto done;
    for (size_t i = 0; i < grants.weapon_count; ++i) weapons[i] = (qa_pickup_grant){grants.weapons[i], 1};
    qa_pickup_grant_plan plan = {.weapon_offer = offer->kind == QA_SUPPLY_WEAPON, .weapons = weapons,
        .weapon_count = grants.weapon_count, .ammo = grants.ammo, .ammo_count = grants.ammo_count};
    ok = qa_pickup_preview_grants(entries, count, &plan, &result, e);
done:
    free(entries); free(weapons); grants_free(&grants); supply_leave(supply);
    if (ok) *out = result;
    else qa_supply_preview_free(&result);
    return ok;
}

static qa_item_id source_owner(const qa_supply_source_owner *owners, size_t count, qa_item_id item)
{
    for (size_t i = 0; i < count; ++i) if (owners[i].item == item) return owners[i].source;
    return 0;
}

bool qa_supply_weapon_sources(const qa_supply_profile *profile,
    const qa_supply_weapon *selected, size_t selected_count, const qa_supply_weapon *original,
    size_t original_count, qa_item_id *out, qa_error *e)
{
    if (!profile || (selected_count && (!selected || !out)) || (original_count && !original) ||
        (profile->weapon_count && !profile->weapons) || (profile->ammo_count && !profile->ammo) ||
        selected_count > SIZE_MAX / sizeof(*out)) return fail(e, QA_ERROR_ARGUMENT, "Invalid selected weapon source mapping");
    for (size_t i = 0; i < profile->weapon_count; ++i)
        if (profile->weapons[i].count && !profile->weapons[i].destinations) return fail(e, QA_ERROR_ARGUMENT, "Missing weapon supply destinations");
    for (size_t i = 0; i < profile->ammo_count; ++i)
        if (profile->ammo[i].count && !profile->ammo[i].destinations) return fail(e, QA_ERROR_ARGUMENT, "Missing ammo supply destinations");
    if (!owners_valid(profile->weapon_owners, profile->weapon_owner_count, profile->weapons, profile->weapon_count, e) ||
        !owners_valid(profile->ammo_owners, profile->ammo_owner_count, profile->ammo, profile->ammo_count, e)) return false;
    qa_item_id *sources = selected_count ? calloc(selected_count, sizeof(*sources)) : NULL;
    if (selected_count && !sources) return fail(e, QA_ERROR_MEMORY, "Cannot allocate selected weapon source mapping");
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
    free(sources); return ok;
}
