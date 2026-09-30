#include "combat_internal.h"
#include "qa/inventory.h"
#include <stdlib.h>
#include <string.h>

bool qa_combat_argument(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
static bool memory_error(qa_error *error) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating combat storage"); return false;
}
bool qa_combat_live(qa_combat *combat, qa_actor_id actor) {
    return combat && qa_actors_get(combat->actors, actor) != NULL;
}
static qa_combat_record *record(qa_combat *combat, qa_actor_id actor) {
    if (!qa_combat_live(combat, actor)) return NULL;
    qa_combat_record *entry = &combat->records[actor.slot];
    return entry->active && qa_actor_id_equal(entry->actor, actor) ? entry : NULL;
}
static bool require_record(qa_combat *combat, qa_actor_id actor, qa_combat_record **out, qa_error *error) {
    qa_combat_record *entry = record(combat, actor);
    if (!entry) { qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "actor has no current combat binding"); return false; }
    *out = entry; return true;
}
static bool state_valid(const qa_combat_state *state, qa_error *error) {
    return state && isfinite(state->health) && isfinite(state->mass)
        ? qa_armor_validate(&state->armor, error) : qa_combat_argument(error, "invalid combat state");
}
static qa_combat_cursor *cursor_for(qa_combat *, qa_actor_id);
static bool fuel_entry(qa_combat *combat, qa_combat_record *entry, qa_inventory_entry *out,
                        qa_error *error) {
    qa_actor_id actor = entry->actor;
    uint64_t serial = entry->serial;
    ++combat->active_calls;
    bool ok = qa_inventory_entry_read(entry->power_inventory, actor, entry->power_item, out, error);
    --combat->active_calls;
    if (!ok) return false;
    if (record(combat, actor) != entry || entry->serial != serial)
        return qa_combat_argument(error, "combat owner changed during power fuel read");
    if (!isfinite((float)out->count))
        return qa_combat_argument(error, "power fuel exceeds its float representation");
    return true;
}
static bool fuel_write(qa_combat *combat, qa_combat_record *entry, float count, qa_error *error) {
    qa_inventory_entry fuel;
    if (!fuel_entry(combat, entry, &fuel, error)) return false;
    if (fuel.count == count) return true;
    qa_actor_id actor = entry->actor;
    uint64_t serial = entry->serial;
    fuel.count = count;
    ++combat->active_calls;
    bool ok = qa_inventory_configure(entry->power_inventory, actor, &fuel, NULL, NULL, error);
    --combat->active_calls;
    if (!ok) return false;
    return (record(combat, actor) == entry && entry->serial == serial) ||
           qa_combat_argument(error, "combat owner changed during power fuel write");
}
static bool primary_read(qa_combat *combat, qa_combat_record *entry, qa_combat_state *out, qa_error *error) {
    qa_actor_id actor = entry->actor; uint64_t serial = entry->serial;
    bool ok = true;
    if (entry->external) {
        qa_combat_binding binding = entry->binding;
        ++combat->active_calls; ok = binding.read(binding.context, out, error); --combat->active_calls;
    } else *out = entry->state;
    if (!ok) return false;
    if (record(combat, actor) != entry || entry->serial != serial) return qa_combat_argument(error, "combat owner changed during state read");
    return state_valid(out, error);
}
static bool read_state(qa_combat *combat, qa_combat_record *entry, qa_combat_state *out, qa_error *error) {
    qa_actor_id actor = entry->actor; uint64_t serial = entry->serial;
    if (!primary_read(combat, entry, out, error)) return false;
    if (entry->power_inventory && !entry->protection[QA_PROTECTION_POWERED].reserved && out->armor.powered.kind != QA_POWER_NONE) {
        qa_inventory_entry fuel;
        if (!fuel_entry(combat, entry, &fuel, error)) return false;
        out->armor.powered.cells = (float)fuel.count;
    }
    for (unsigned channel = 0; channel != 2; ++channel) {
        qa_combat_protection *slot = &entry->protection[channel];
        if (!slot->reserved) continue;
        if (!slot->bound) return qa_combat_argument(error, "protection reservation has not been bound");
        qa_protection_binding binding = slot->binding; uint64_t owner_serial = slot->serial;
        qa_armor armor = out->armor;
        ++combat->active_calls; bool ok = binding.read(binding.context, &armor, error); --combat->active_calls;
        if (!ok) return false;
        if (record(combat, actor) != entry || entry->serial != serial || !slot->bound || slot->serial != owner_serial)
            return qa_combat_argument(error, "protection owner changed during state read");
        if (channel == QA_PROTECTION_REGULAR) out->armor.regular = armor.regular;
        else out->armor.powered = armor.powered;
    }
    if (combat->hooks.team) {
        ++combat->active_calls;
        out->team = combat->hooks.team(combat->hooks.context, actor, out->team);
        --combat->active_calls;
        if (record(combat, actor) != entry || entry->serial != serial) return qa_combat_argument(error, "combat owner changed during team resolution");
    }
    if (combat->hooks.invulnerable) {
        ++combat->active_calls;
        bool protected = combat->hooks.invulnerable(combat->hooks.context, actor);
        --combat->active_calls;
        if (record(combat, actor) != entry || entry->serial != serial)
            return qa_combat_argument(error, "combat owner changed during protection resolution");
        out->invulnerable = out->invulnerable || protected;
    }
    return state_valid(out, error);
}
bool qa_combat_read_traits(qa_combat *combat, qa_actor_id actor, qa_combat_state *out, qa_error *error) {
    qa_combat_record *entry; qa_combat_state state;
    if (!out || !require_record(combat, actor, &entry, error) || !primary_read(combat, entry, &state, error)) return false;
    *out = state; return true;
}
bool qa_combat_read(qa_combat *combat, qa_actor_id actor, qa_combat_state *out, qa_error *error) {
    qa_combat_record *entry; qa_combat_state state;
    if (!out || !require_record(combat, actor, &entry, error) || !read_state(combat, entry, &state, error)) return false;
    *out = state; return true;
}
bool qa_combat_primary_read(qa_combat *combat, qa_actor_id actor, qa_combat_state *out, bool *local, qa_error *error) {
    qa_combat_record *entry; qa_combat_state state;
    if (!out || !local || !qa_combat_idle(combat)) return qa_combat_argument(error, "checkpoint requires idle combat");
    if (!require_record(combat, actor, &entry, error) || !primary_read(combat, entry, &state, error)) return false;
    if (entry->power_inventory && state.armor.powered.kind != QA_POWER_NONE) {
        qa_inventory_entry fuel;
        if (!fuel_entry(combat, entry, &fuel, error)) return false;
        state.armor.powered.cells = (float)fuel.count;
    }
    if (!state_valid(&state, error)) return false;
    *out = state; *local = !entry->external; return true;
}
bool qa_combat_create(qa_actor_registry *actors, const qa_combat_hooks *hooks, qa_combat **out, qa_error *error) {
    if (!actors || !out) return qa_combat_argument(error, "combat requires an actor registry and output");
    size_t count = qa_actors_capacity(actors);
    if (count > SIZE_MAX / sizeof(qa_combat_record)) return memory_error(error);
    qa_combat *combat = calloc(1, sizeof(*combat));
    if (!combat) return memory_error(error);
    combat->records = calloc(count, sizeof(*combat->records));
    if (!combat->records || !qa_operation_create(sizeof(qa_damage_request), sizeof(qa_damage_outcome), &combat->damage, error)) {
        bool allocation_failed = !combat->records;
        free(combat->records); free(combat);
        return allocation_failed ? memory_error(error) : false;
    }
    combat->actors = actors; combat->next_serial = 1;
    if (hooks) combat->hooks = *hooks;
    *out = combat; return true;
}
bool qa_combat_idle(const qa_combat *combat) { return combat && !combat->active_calls && !combat->active_hits; }
bool qa_combat_destroy(qa_combat *combat, qa_error *error) {
    if (!combat) return true;
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot destroy active combat");
    if (combat->admissions) return qa_combat_argument(error, "abort policy admissions before combat destruction");
    if (!qa_operation_destroy(combat->damage, error)) return false;
    free(combat->records); free(combat->policies); free(combat); return true;
}
void qa_combat_actor_released(qa_combat *combat, qa_actor_record released) {
    if (!combat || released.id.slot >= qa_actors_capacity(combat->actors)) return;
    qa_combat_record *entry = &combat->records[released.id.slot];
    if (entry->active && qa_actor_id_equal(entry->actor, released.id)) memset(entry, 0, sizeof(*entry));
}
static bool next_serial(qa_combat *combat, uint64_t *out, qa_error *error) {
    if (combat->next_serial == UINT64_MAX) return qa_combat_argument(error, "combat ownership identity exhausted");
    *out = combat->next_serial++; return true;
}
struct qa_combat_policy_admission {
    qa_combat *combat;
    qa_combat_policy policy;
    qa_combat_policy_admission *previous, *next;
};

bool qa_combat_prepare_policy(qa_combat *combat, const qa_combat_policy *policy, bool replace_owner,
                               qa_combat_policy_admission **out, qa_error *error) {
    if (!combat || !policy || !out || !policy->describe || policy->family < QA_GAME_Q1 || policy->family > QA_GAME_Q3)
        return qa_combat_argument(error, "invalid combat policy");
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot change policies during combat");
    for (size_t i = 0; !replace_owner && i < combat->policy_count; ++i) if (combat->policies[i].provider == policy->provider)
        return qa_combat_argument(error, "combat policy provider already registered");
    for (qa_combat_policy_admission *pending = combat->admissions; pending; pending = pending->next)
        if (pending->policy.provider == policy->provider)
            return qa_combat_argument(error, "combat policy provider already reserved");
    if (combat->admission_count >= SIZE_MAX - combat->policy_count) return memory_error(error);
    size_t required = combat->policy_count + combat->admission_count + 1;
    if (required > combat->policy_capacity) {
        size_t limit = SIZE_MAX / sizeof(*combat->policies);
        if (required > limit) return memory_error(error);
        size_t capacity = combat->policy_capacity > limit / 2 ? limit : combat->policy_capacity * 2;
        if (capacity < required) capacity = required;
        if (capacity < 8 && limit >= 8) capacity = 8;
        qa_combat_policy *policies = realloc(combat->policies, capacity * sizeof(*policies));
        if (!policies) return memory_error(error);
        combat->policies = policies; combat->policy_capacity = capacity;
    }
    qa_combat_policy_admission *token = malloc(sizeof(*token));
    if (!token) return memory_error(error);
    *token = (qa_combat_policy_admission){.combat = combat, .policy = *policy, .next = combat->admissions};
    if (token->next) token->next->previous = token;
    combat->admissions = token;
    ++combat->admission_count;
    *out = token;
    return true;
}

bool qa_combat_policy_admission_validate(qa_combat_policy_admission *token, qa_error *error) {
    if (!token || !qa_combat_idle(token->combat)) return qa_combat_argument(error, "policy admission requires idle combat");
    for (size_t i = 0; i < token->combat->policy_count; ++i)
        if (token->combat->policies[i].provider == token->policy.provider)
            return qa_combat_argument(error, "remove existing policy before admission");
    return true;
}

void qa_combat_policy_admission_abort(qa_combat_policy_admission *token) {
    if (!token) return;
    if (token->previous) token->previous->next = token->next;
    else token->combat->admissions = token->next;
    if (token->next) token->next->previous = token->previous;
    --token->combat->admission_count;
    free(token);
}

bool qa_combat_policy_admission_commit(qa_combat_policy_admission *token, qa_error *error) {
    if (!qa_combat_policy_admission_validate(token, error)) return false;
    token->combat->policies[token->combat->policy_count++] = token->policy;
    qa_combat_policy_admission_abort(token);
    return true;
}

bool qa_combat_register_policy(qa_combat *combat, const qa_combat_policy *policy, qa_error *error) {
    qa_combat_policy_admission *token;
    if (!qa_combat_prepare_policy(combat, policy, false, &token, error)) return false;
    if (qa_combat_policy_admission_commit(token, error)) return true;
    qa_combat_policy_admission_abort(token);
    return false;
}
bool qa_combat_unregister_policy(qa_combat *combat, qa_actor_owner provider, qa_error *error) {
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot change policies during combat");
    for (size_t i = 0; i < combat->policy_count; ++i) if (combat->policies[i].provider == provider) {
        memmove(combat->policies + i, combat->policies + i + 1, (combat->policy_count - i - 1) * sizeof(*combat->policies));
        --combat->policy_count; return true;
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "combat policy not registered"); return false;
}
bool qa_combat_create_actor(qa_combat *combat, qa_actor_id actor, const qa_combat_state *state, qa_error *error) {
    if (!qa_combat_live(combat, actor) || record(combat, actor)) return qa_combat_argument(error, "actor is stale or already owns combat state");
    if (!state_valid(state, error) || state->armor.regular.kind == QA_ARMOR_SOURCE)
        return qa_combat_argument(error, "local combat requires native armor storage");
    uint64_t serial; if (!next_serial(combat, &serial, error)) return false;
    qa_combat_record *entry = &combat->records[actor.slot];
    *entry = (qa_combat_record){.actor = actor, .serial = serial, .active = true, .state = *state};
    return true;
}
uint64_t qa_combat_storage_serial(const qa_combat *combat, qa_actor_id actor) {
    if (!combat || !qa_actors_get(combat->actors, actor))
        return 0;
    const qa_combat_record *entry = &combat->records[actor.slot];
    return entry->active && qa_actor_id_equal(entry->actor, actor) ? entry->serial : 0;
}
bool qa_combat_set_admission(qa_combat *combat, qa_actor_id actor,
                              const qa_combat_admission *admission, qa_error *error) {
    qa_combat_record *entry;
    if (!require_record(combat, actor, &entry, error)) return false;
    if (entry->external || (admission && !admission->admit))
        return qa_combat_argument(error, "damage admission requires local combat storage and a callback");
    if (entry->active_admissions || entry->power_admitting || cursor_for(combat, actor))
        return qa_combat_argument(error, "cannot change an active actor's damage admission");
    uint64_t serial;
    if (!next_serial(combat, &serial, error)) return false;
    entry->admission = admission ? *admission : (qa_combat_admission){0};
    entry->serial = serial;
    return true;
}
bool qa_combat_bind(qa_combat *combat, qa_actor_id actor, const qa_combat_binding *binding, bool replace, qa_error *error) {
    if (!qa_combat_live(combat, actor) || !binding || !binding->read || !binding->write_health || !binding->write_armor)
        return qa_combat_argument(error, "invalid combat binding");
    qa_combat_record *entry = &combat->records[actor.slot];
    if (cursor_for(combat, actor) || entry->active_admissions)
        return qa_combat_argument(error, "cannot replace a live damage binding");
    if (entry->power_admitting) return qa_combat_argument(error, "cannot replace combat during power fuel admission");
    if (record(combat, actor) && !replace) return qa_combat_argument(error, "actor already has combat storage");
    if (record(combat, actor) && (entry->protection[0].reserved || entry->protection[1].reserved))
        return qa_combat_argument(error, "close protection leases before replacing combat storage");
    uint64_t previous_serial = entry->serial;
    bool previous_active = entry->active;
    qa_combat_state initial;
    ++combat->active_calls; bool ok = binding->read(binding->context, &initial, error); --combat->active_calls;
    if (!ok || !state_valid(&initial, error)) return false;
    if (!qa_combat_live(combat, actor) || entry->serial != previous_serial || entry->active != previous_active)
        return qa_combat_argument(error, "combat actor or storage changed during admission");
    uint64_t serial; if (!next_serial(combat, &serial, error)) return false;
    qa_inventory *power_inventory = previous_active ? entry->power_inventory : NULL;
    qa_item_id power_item = previous_active ? entry->power_item : 0;
    *entry = (qa_combat_record){.actor = actor, .serial = serial, .active = true, .external = true,
        .binding = *binding, .power_inventory = power_inventory, .power_item = power_item};
    return true;
}
bool qa_combat_bind_power_inventory(qa_combat *combat, qa_actor_id actor, qa_inventory *inventory,
                                    qa_item_id item, qa_error *error) {
    qa_combat_record *entry;
    if (!inventory || !item || !combat)
        return qa_combat_argument(error, "power cells require an inventory and item");
    if (!require_record(combat, actor, &entry, error)) return false;
    if (entry->power_inventory)
        return (entry->power_inventory == inventory && entry->power_item == item) ||
               qa_combat_argument(error, "actor already has a different power cell reservoir");
    if (entry->power_admitting || cursor_for(combat, actor))
        return qa_combat_argument(error, "power fuel admission requires an inactive combat binding");
    uint64_t serial = entry->serial;
    qa_inventory_entry fuel;
    entry->power_admitting = true;
    ++combat->active_calls;
    bool ok = qa_inventory_entry_read(inventory, actor, item, &fuel, error);
    --combat->active_calls;
    bool current_owner = record(combat, actor) == entry && entry->serial == serial;
    if (current_owner) entry->power_admitting = false;
    if (!ok) return false;
    if (!isfinite((float)fuel.count) || !current_owner)
        return qa_combat_argument(error, "invalid or retired power fuel binding");
    entry->power_inventory = inventory; entry->power_item = item; return true;
}
bool qa_combat_power_inventory(qa_combat *combat, qa_actor_id actor, qa_inventory **inventory,
                                qa_item_id *item) {
    qa_combat_record *entry = record(combat, actor);
    if (inventory) *inventory = entry ? entry->power_inventory : NULL;
    if (item) *item = entry ? entry->power_item : 0;
    return entry && entry->power_inventory;
}
static qa_combat_cursor *cursor_for(qa_combat *combat, qa_actor_id actor) {
    for (qa_combat_cursor *cursor = combat->current; cursor; cursor = cursor->previous)
        if (cursor->active && qa_actor_id_equal(cursor->outcome->request.target, actor)) return cursor;
    return NULL;
}
static bool journal_reserve(qa_combat_cursor *cursor, qa_error *error) {
    if (cursor->outcome->mutation_count < cursor->journal_capacity) return true;
    size_t capacity = cursor->journal_capacity ? cursor->journal_capacity * 2 : 8;
    if (capacity < cursor->journal_capacity || capacity > SIZE_MAX / sizeof(qa_damage_mutation)) return memory_error(error);
    qa_damage_mutation *mutations = realloc(cursor->outcome->mutations, capacity * sizeof(*mutations));
    if (!mutations) return memory_error(error);
    cursor->outcome->mutations = mutations; cursor->journal_capacity = capacity; return true;
}
static void advance_cursors(qa_combat *combat, qa_actor_id actor, const qa_damage_mutation *mutation) {
    for (qa_combat_cursor *cursor = combat->current; cursor; cursor = cursor->previous) {
        if (!cursor->active || !qa_actor_id_equal(cursor->outcome->request.target, actor)) continue;
        switch (mutation->kind) {
        case QA_MUTATION_HEALTH: cursor->observed.health = mutation->value.health.after; break;
        case QA_MUTATION_ARMOR: cursor->observed.armor = mutation->value.armor.after; break;
        case QA_MUTATION_SOURCE_VELOCITY: cursor->velocity = mutation->value.velocity.after; cursor->has_velocity = true; break;
        case QA_MUTATION_IMPULSE: cursor->has_velocity = false; break;
        }
    }
}
static bool vector_equal(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool observe_store(qa_combat *combat, qa_combat_cursor *cursor, const qa_damage_mutation *mutation, qa_error *error) {
    qa_combat_record *entry = record(combat, cursor->outcome->request.target);
    if (!cursor->active || !entry || entry->serial != cursor->binding_serial || cursor->reaction_seen)
        return qa_combat_argument(error, "damage observation is closed or its owner retired");
    qa_combat_state actual;
    if (!read_state(combat, entry, &actual, error)) return false;
    switch (mutation->kind) {
    case QA_MUTATION_HEALTH:
        if (!isfinite(mutation->value.health.after) || mutation->value.health.before != cursor->observed.health || actual.health != mutation->value.health.after)
            return qa_combat_argument(error, "observed health differs from authoritative store");
        break;
    case QA_MUTATION_ARMOR:
        if (!qa_armor_equal(mutation->value.armor.before, cursor->observed.armor) || !qa_armor_equal(mutation->value.armor.after, actual.armor))
            return qa_combat_argument(error, "observed armor differs from authoritative store");
        if (qa_armor_equal(mutation->value.armor.before, mutation->value.armor.after)) return true;
        break;
    case QA_MUTATION_SOURCE_VELOCITY:
        if (!qa_vec_finite(mutation->value.velocity.before) || !qa_vec_finite(mutation->value.velocity.after) ||
            (cursor->has_velocity && !vector_equal(cursor->velocity, mutation->value.velocity.before)))
            return qa_combat_argument(error, "invalid observed source velocity");
        break;
    default: return qa_combat_argument(error, "source observation cannot replay an impulse");
    }
    if (!journal_reserve(cursor, error)) return false;
    cursor->outcome->mutations[cursor->outcome->mutation_count++] = *mutation;
    advance_cursors(combat, entry->actor, mutation); return true;
}
static bool observe_public(qa_combat *combat, qa_actor_id actor, bool armor, qa_error *error) {
    qa_combat_cursor *cursor = cursor_for(combat, actor);
    if (!cursor || !qa_combat_live(combat, actor)) return true;
    qa_combat_state current; if (!qa_combat_read(combat, actor, &current, error)) return false;
    qa_damage_mutation mutation;
    if (armor) {
        if (qa_armor_equal(cursor->observed.armor, current.armor)) return true;
        mutation = (qa_damage_mutation){.kind = QA_MUTATION_ARMOR, .value.armor = {cursor->observed.armor, current.armor}};
    } else {
        if (cursor->observed.health == current.health) return true;
        mutation = (qa_damage_mutation){.kind = QA_MUTATION_HEALTH, .value.health = {cursor->observed.health, current.health}};
    }
    if (cursor->reaction_seen) { advance_cursors(combat, actor, &mutation); return true; }
    return observe_store(combat, cursor, &mutation, error);
}
bool qa_combat_set_health(qa_combat *combat, qa_actor_id actor, float health, qa_error *error) {
    qa_combat_record *entry;
    if (!isfinite(health)) return qa_combat_argument(error, "health must be finite");
    if (!require_record(combat, actor, &entry, error)) return false;
    qa_combat_cursor *cursor = cursor_for(combat, actor);
    if (cursor && !cursor->reaction_seen && !journal_reserve(cursor, error)) return false;
    uint64_t serial = entry->serial;
    if (entry->external) {
        qa_combat_binding binding = entry->binding;
        ++combat->active_calls; bool ok = binding.write_health(binding.context, health, error); --combat->active_calls;
        if (!ok) return false;
    } else entry->state.health = health;
    if (!qa_combat_live(combat, actor)) return true;
    if (record(combat, actor) != entry || entry->serial != serial) return qa_combat_argument(error, "health owner changed during store");
    return observe_public(combat, actor, false, error);
}
static bool ownership(qa_combat *combat, qa_combat_record *entry, qa_actor_id actor, uint64_t serial, const uint64_t owners[2], qa_error *error) {
    if (record(combat, actor) != entry || entry->serial != serial) return qa_combat_argument(error, "combat owner changed during armor store");
    for (unsigned c = 0; c != 2; ++c) if ((entry->protection[c].reserved ? entry->protection[c].serial : 0) != owners[c])
        return qa_combat_argument(error, "protection owner changed during armor store");
    return true;
}
bool qa_combat_set_armor(qa_combat *combat, qa_actor_id actor, const qa_armor *armor, qa_error *error) {
    qa_combat_record *entry;
    if (!qa_armor_validate(armor, error) || !require_record(combat, actor, &entry, error)) return false;
    qa_combat_state primary, effective;
    if (!primary_read(combat, entry, &primary, error) || !read_state(combat, entry, &effective, error)) return false;
    uint64_t serial = entry->serial, owners[2] = {entry->protection[0].reserved ? entry->protection[0].serial : 0, entry->protection[1].reserved ? entry->protection[1].serial : 0};
    qa_armor selected = *armor, original = *armor;
    if (owners[0]) original.regular = primary.armor.regular;
    if (owners[1]) original.powered = primary.armor.powered;
    if (!entry->external && original.regular.kind == QA_ARMOR_SOURCE) return qa_combat_argument(error, "local armor cannot store a source formula");
    ++combat->active_calls;
    bool ok = !entry->external || !entry->binding.validate_armor || entry->binding.validate_armor(entry->binding.context, &original, error);
    --combat->active_calls;
    if (!ok || !ownership(combat, entry, actor, serial, owners, error)) return false;
    for (unsigned channel = 0; channel != 2; ++channel) if (owners[channel]) {
        qa_protection_binding binding = entry->protection[channel].binding;
        ++combat->active_calls; ok = binding.validate_write(binding.context, &selected, error); --combat->active_calls;
        if (!ok || !ownership(combat, entry, actor, serial, owners, error)) return false;
    }
    qa_combat_cursor *cursor = cursor_for(combat, actor);
    if (cursor && !cursor->reaction_seen && !journal_reserve(cursor, error)) return false;
    bool changed[2] = {!qa_regular_armor_equal(effective.armor.regular, selected.regular), effective.armor.powered.kind != selected.powered.kind ||
        (selected.powered.kind != QA_POWER_NONE && effective.armor.powered.cells != selected.powered.cells)};
    for (unsigned channel = 0; channel != 2; ++channel) if (owners[channel] && changed[channel]) {
        qa_protection_binding binding = entry->protection[channel].binding;
        ++combat->active_calls; ok = binding.write(binding.context, &selected, error); --combat->active_calls;
        if (!ok || !ownership(combat, entry, actor, serial, owners, error)) return false;
        if (channel == QA_PROTECTION_REGULAR && changed[QA_PROTECTION_POWERED]) {
            qa_combat_state intermediate;
            if (!read_state(combat, entry, &intermediate, error)) return false;
            if (intermediate.armor.powered.kind != effective.armor.powered.kind ||
                (effective.armor.powered.kind != QA_POWER_NONE && intermediate.armor.powered.cells != effective.armor.powered.cells))
                return qa_combat_argument(error, "powered protection changed during regular armor store");
        }
    }
    if (!owners[QA_PROTECTION_POWERED] && changed[QA_PROTECTION_POWERED] && entry->power_inventory && selected.powered.kind != QA_POWER_NONE) {
        ok = fuel_write(combat, entry, selected.powered.cells, error);
        if (!ok || !ownership(combat, entry, actor, serial, owners, error)) return false;
    }
    qa_combat_state latest;
    if (!primary_read(combat, entry, &latest, error)) return false;
    if (!owners[0] && changed[0] && !qa_regular_armor_equal(primary.armor.regular, latest.armor.regular))
        return qa_combat_argument(error, "primary regular armor changed during delegated write");
    original = latest.armor;
    if (!owners[0] && changed[0]) original.regular = selected.regular;
    if (!owners[1] && changed[1]) original.powered = selected.powered;
    if (!qa_armor_equal(original, latest.armor)) {
        if (entry->external) {
            qa_combat_binding binding = entry->binding;
            ++combat->active_calls;
            ok = !binding.validate_armor || binding.validate_armor(binding.context, &original, error);
            --combat->active_calls;
            if (!ok || !ownership(combat, entry, actor, serial, owners, error)) return false;
            ++combat->active_calls;
            ok = binding.write_armor(binding.context, &original, error);
            --combat->active_calls;
            if (!ok) return false;
        } else entry->state.armor = original;
    }
    if (!ownership(combat, entry, actor, serial, owners, error)) return false;
    return observe_public(combat, actor, true, error);
}
bool qa_combat_set_traits(qa_combat *combat, qa_actor_id actor, const qa_combat_state *traits, qa_error *error) {
    qa_combat_record *entry;
    if (!state_valid(traits, error) || !require_record(combat, actor, &entry, error)) return false;
    if (entry->external) {
        qa_combat_binding binding = entry->binding;
        uint64_t serial = entry->serial;
        if (!binding.write_traits) return qa_combat_argument(error, "source combat traits are not mutable");
        ++combat->active_calls; bool ok = binding.write_traits(binding.context, traits, error); --combat->active_calls;
        return ok && (record(combat, actor) == entry && entry->serial == serial
            ? true : qa_combat_argument(error, "combat traits storage changed during write"));
    }
    entry->state.mass = traits->mass; entry->state.can_take_damage = traits->can_take_damage;
    entry->state.invulnerable = traits->invulnerable; entry->state.no_knockback = traits->no_knockback; entry->state.team = traits->team;
    return true;
}
bool qa_combat_set_regular_armor(qa_combat *combat, qa_actor_id actor, const qa_regular_armor *regular, qa_error *error) {
    qa_combat_state state;
    if (!regular) return qa_combat_argument(error, "missing regular armor selection");
    if (!qa_combat_read(combat, actor, &state, error)) return false;
    state.armor.regular = *regular;
    return qa_combat_set_armor(combat, actor, &state.armor, error);
}
bool qa_combat_set_powered_armor(qa_combat *combat, qa_actor_id actor, const qa_powered_armor *powered, qa_error *error) {
    qa_combat_state state;
    if (!powered) return qa_combat_argument(error, "missing powered armor selection");
    if (!qa_combat_read(combat, actor, &state, error)) return false;
    state.armor.powered = *powered;
    return qa_combat_set_armor(combat, actor, &state.armor, error);
}
bool qa_combat_set_regular_points(qa_combat *combat, qa_actor_id actor, float points, const qa_regular_armor *initial, qa_error *error) {
    qa_combat_record *entry; qa_combat_state state;
    if (!isfinite(points)) return qa_combat_argument(error, "armor points must be finite");
    if (!require_record(combat, actor, &entry, error) || !read_state(combat, entry, &state, error)) return false;
    if (state.armor.regular.kind == QA_ARMOR_NONE) {
        if (points == 0) return true;
        bool selected = false;
        if (entry->external && !entry->protection[QA_PROTECTION_REGULAR].reserved && entry->binding.empty_regular_armor) {
            qa_combat_binding binding = entry->binding; uint64_t serial = entry->serial;
            ++combat->active_calls;
            bool ok = binding.empty_regular_armor(binding.context, points, &state.armor.regular, &selected, error);
            --combat->active_calls;
            if (!ok) return false;
            if (record(combat, actor) != entry || entry->serial != serial || entry->protection[QA_PROTECTION_REGULAR].reserved)
                return qa_combat_argument(error, "regular armor owner changed during selection");
        }
        if (!selected && initial) { state.armor.regular = *initial; selected = true; }
        if (!selected || state.armor.regular.kind == QA_ARMOR_NONE) return qa_combat_argument(error, "armor points require a regular armor selection");
    }
    state.armor.regular.points = points;
    return qa_combat_set_armor(combat, actor, &state.armor, error);
}
bool qa_combat_normalize_legacy_armor(qa_combat *combat, qa_actor_id actor, const qa_armor *input, qa_armor *out, qa_error *error) {
    qa_combat_record *entry;
    if (!out || !qa_armor_validate(input, error) || !require_record(combat, actor, &entry, error)) return false;
    qa_armor normalized = *input;
    if (entry->external && entry->binding.normalize_legacy_armor) {
        qa_combat_binding binding = entry->binding; uint64_t serial = entry->serial;
        ++combat->active_calls;
        bool ok = binding.normalize_legacy_armor(binding.context, input, &normalized, error);
        --combat->active_calls;
        if (!ok) return false;
        if (record(combat, actor) != entry || entry->serial != serial) return qa_combat_argument(error, "armor owner changed during legacy normalization");
    }
    if (!qa_armor_validate(&normalized, error)) return false;
    *out = normalized; return true;
}
bool qa_combat_reserve_protection(qa_combat *combat, qa_actor_id actor, qa_protection_channel channel,
                                 const qa_protection_claim *claim, qa_protection_lease *out, qa_error *error) {
    qa_combat_record *entry;
    if (!claim || !out || (channel != QA_PROTECTION_REGULAR && channel != QA_PROTECTION_POWERED) ||
        claim->admission < QA_PROTECTION_CLAIM || claim->admission > QA_PROTECTION_REPLACE_CURRENT)
        return qa_combat_argument(error, "invalid protection claim");
    if (!qa_combat_idle(combat)) return qa_combat_argument(error, "cannot reserve protection during combat");
    if (!require_record(combat, actor, &entry, error)) return false;
    qa_combat_protection *slot = &entry->protection[channel];
    if (slot->reserved) return qa_combat_argument(error, "protection channel already reserved");
    const qa_actor_record *identity = qa_actors_get(combat->actors, actor);
    bool has_owner = entry->external ? entry->binding.has_primary_protection[channel] : channel == QA_PROTECTION_REGULAR || entry->power_inventory || entry->state.armor.powered.kind != QA_POWER_NONE;
    qa_actor_owner owner = entry->external ? entry->binding.primary_protection[channel] : identity->owner;
    if ((claim->admission == QA_PROTECTION_CLAIM && has_owner) || (claim->admission == QA_PROTECTION_REPLACE_PRIMARY && (!has_owner || owner != claim->expected_owner)))
        return qa_combat_argument(error, "protection claim does not match primary ownership");
    if (entry->external && entry->binding.source_damage && !entry->binding.source_armor_stages[channel])
        return qa_combat_argument(error, "original source has no declared armor stage");
    uint64_t serial; if (!next_serial(combat, &serial, error)) return false;
    *slot = (qa_combat_protection){.serial = serial, .claim = *claim, .reserved = true};
    *out = (qa_protection_lease){actor, serial, channel}; return true;
}
bool qa_combat_protection_current(qa_combat *combat, qa_protection_lease lease) {
    qa_combat_record *entry = record(combat, lease.actor);
    return entry && (lease.channel == QA_PROTECTION_REGULAR || lease.channel == QA_PROTECTION_POWERED) &&
        entry->protection[lease.channel].reserved && entry->protection[lease.channel].serial == lease.serial;
}
bool qa_combat_protection_bound(qa_combat *combat, qa_protection_lease lease) {
    return qa_combat_protection_current(combat, lease) && combat->records[lease.actor.slot].protection[lease.channel].bound;
}
bool qa_combat_protection_owner(qa_combat *combat, qa_actor_id actor, qa_protection_channel channel, qa_actor_owner *out, uint64_t *serial) {
    qa_combat_record *entry = record(combat, actor);
    if (!entry || (channel != QA_PROTECTION_REGULAR && channel != QA_PROTECTION_POWERED) || !entry->protection[channel].reserved) return false;
    if (out) *out = entry->protection[channel].claim.owner;
    if (serial) *serial = entry->protection[channel].serial;
    return true;
}
bool qa_combat_bind_protection(qa_combat *combat, qa_protection_lease lease, const qa_protection_binding *binding, qa_error *error) {
    if (!binding || !binding->read || !binding->write || !binding->validate_write || !binding->absorb)
        return qa_combat_argument(error, "invalid protection binding");
    if (!qa_combat_idle(combat) || !qa_combat_protection_current(combat, lease)) return qa_combat_argument(error, "protection reservation is unavailable");
    qa_combat_protection *slot = &combat->records[lease.actor.slot].protection[lease.channel];
    if (slot->bound) return qa_combat_argument(error, "protection reservation already bound");
    qa_armor armor = {0};
    ++combat->active_calls; bool ok = binding->read(binding->context, &armor, error); --combat->active_calls;
    if (!ok || !qa_armor_validate(&armor, error)) return false;
    if (!qa_combat_protection_current(combat, lease)) return qa_combat_argument(error, "protection retired during admission");
    slot->binding = *binding; slot->bound = true; return true;
}
bool qa_combat_close_protection(qa_combat *combat, qa_protection_lease lease, qa_error *error) {
    if (!qa_combat_protection_current(combat, lease)) return true;
    qa_combat_cursor *cursor = cursor_for(combat, lease.actor);
    qa_combat_protection *slot = &combat->records[lease.actor.slot].protection[lease.channel];
    if (cursor && slot->bound) {
        qa_combat_state current;
        if (!qa_combat_read(combat, lease.actor, &current, error)) return false;
        if (!cursor->reaction_seen && !qa_armor_equal(current.armor, cursor->observed.armor))
            return qa_combat_argument(error, "protection closed with an unobserved store");
        if (!cursor->reaction_seen && !journal_reserve(cursor, error)) return false;
    }
    memset(slot, 0, sizeof(*slot));
    return observe_public(combat, lease.actor, true, error);
}

static bool remember_failure(bool ok, bool *failed, qa_error *saved, qa_error *error) {
    if (ok) return true;
    if (!*failed) {
        *failed = true;
        if (error && error->code != QA_OK) *saved = *error;
        else qa_error_set(saved, QA_ERROR_ARGUMENT, 0, "source observation failed");
    }
    if (error) *error = *saved;
    return false;
}
static bool damage_observe_current(qa_damage_observer *observer, const qa_damage_mutation *mutation, qa_error *error) {
    if (!observer->open || !mutation) return qa_combat_argument(error, "source damage observer is closed");
    qa_damage_mutation captured = *mutation;
    if (captured.kind == QA_MUTATION_ARMOR) {
        qa_combat_record *entry = record(observer->combat, observer->cursor->outcome->request.target);
        if (!entry) return qa_combat_argument(error, "source damage actor retired");
        if (entry->protection[QA_PROTECTION_REGULAR].bound) {
            captured.value.armor.before.regular = observer->cursor->observed.armor.regular;
            captured.value.armor.after.regular = observer->cursor->observed.armor.regular;
        }
        if (entry->protection[QA_PROTECTION_POWERED].bound) {
            captured.value.armor.before.powered = observer->cursor->observed.armor.powered;
            captured.value.armor.after.powered = observer->cursor->observed.armor.powered;
        }
    }
    return observe_store(observer->combat, observer->cursor, &captured, error);
}
bool qa_damage_observe(qa_damage_observer *observer, const qa_damage_mutation *mutation, qa_error *error) {
    if (!observer) return qa_combat_argument(error, "missing source damage observer");
    if (observer->failed) { if (error) *error = observer->failure; return false; }
    bool ok = damage_observe_current(observer, mutation, error);
    return remember_failure(ok, &observer->failed, &observer->failure, error);
}
static bool result_valid(const qa_damage_result *result, qa_error *error) {
    if (!result || !isfinite(result->applied_damage) || result->reaction < QA_REACTION_NONE || result->reaction > QA_REACTION_DEATH)
        return qa_combat_argument(error, "invalid damage result");
    if (result->has_feedback && (result->feedback_family < QA_GAME_Q1 || result->feedback_family > QA_GAME_Q3 ||
        !isfinite(result->power_saved) || !isfinite(result->armor_saved) || !isfinite(result->blood) || !isfinite(result->knockback)))
        return qa_combat_argument(error, "invalid source damage feedback");
    return true;
}
static bool cursor_reconciled(qa_combat *combat, qa_combat_cursor *cursor, qa_error *error) {
    if (!qa_combat_live(combat, cursor->outcome->request.target)) return true;
    qa_combat_state current;
    if (!qa_combat_read(combat, cursor->outcome->request.target, &current, error)) return false;
    if (current.health != cursor->observed.health || !qa_armor_equal(current.armor, cursor->observed.armor))
        return qa_combat_argument(error, "source omitted a committed combat store");
    return true;
}
static bool before_reaction(qa_combat *combat, qa_combat_cursor *cursor, const qa_damage_result *result, qa_error *error) {
    if (!cursor->active || cursor->reaction_seen) return qa_combat_argument(error, "source reaction boundary already consumed");
    if (!result_valid(result, error) || !cursor_reconciled(combat, cursor, error)) return false;
    cursor->reaction_seen = true; cursor->reaction = *result; cursor->outcome->result = *result;
    if (!qa_combat_live(combat, cursor->outcome->request.target) || !combat->hooks.before_reaction) return true;
    ++combat->active_calls;
    bool ok = combat->hooks.before_reaction(combat->hooks.context, cursor->outcome, error);
    --combat->active_calls;
    return ok;
}
bool qa_damage_before_reaction(qa_damage_observer *observer, const qa_damage_result *result, qa_error *error) {
    if (!observer) return qa_combat_argument(error, "missing source damage observer");
    if (observer->failed) { if (error) *error = observer->failure; return false; }
    bool ok = observer->open ? before_reaction(observer->combat, observer->cursor, result, error)
        : qa_combat_argument(error, "source damage observer is closed");
    return remember_failure(ok, &observer->failed, &observer->failure, error);
}
static bool pickup_store_current(qa_combat *combat, qa_actor_id actor, qa_actor_owner owner,
                                  const qa_protection_store *change, qa_error *error) {
    qa_combat_record *entry;
    if (!change || (!change->regular && !change->powered)) return qa_combat_argument(error, "protection store has no channels");
    if (!require_record(combat, actor, &entry, error)) return false;
    for (unsigned c = 0; c != 2; ++c) if (c == QA_PROTECTION_REGULAR ? change->regular : change->powered) {
        qa_combat_protection *slot = &entry->protection[c];
        if (!slot->bound || slot->claim.owner != owner) return qa_combat_argument(error, "protection store exceeds component authority");
    }
    qa_combat_state actual;
    if (!read_state(combat, entry, &actual, error)) return false;
    if ((change->regular && !qa_regular_armor_equal(change->after.regular, actual.armor.regular)) ||
        (change->powered && (change->after.powered.kind != actual.armor.powered.kind ||
        (actual.armor.powered.kind != QA_POWER_NONE && change->after.powered.cells != actual.armor.powered.cells))))
        return qa_combat_argument(error, "reported protection store differs from source state");
    qa_combat_cursor *cursor = cursor_for(combat, actor);
    if (!cursor) return true;
    qa_armor previous = cursor->observed.armor;
    if (change->regular) previous.regular = change->before.regular;
    if (change->powered) previous.powered = change->before.powered;
    qa_damage_mutation mutation = {.kind = QA_MUTATION_ARMOR, .value.armor = {previous, actual.armor}};
    if (cursor->reaction_seen) { advance_cursors(combat, actor, &mutation); return true; }
    return observe_store(combat, cursor, &mutation, error);
}
bool qa_combat_pickup_store(qa_combat *combat, qa_actor_id actor, qa_actor_owner owner, const qa_protection_store *change, qa_error *error) {
    return pickup_store_current(combat, actor, owner, change, error);
}
bool qa_protection_observe(qa_protection_observer *observer, const qa_protection_store *change, qa_error *error) {
    if (!observer) return qa_combat_argument(error, "missing protection observer");
    if (observer->failed) { if (error) *error = observer->failure; return false; }
    bool ok = observer->open && qa_combat_protection_current(observer->combat, observer->lease)
        ? pickup_store_current(observer->combat, observer->lease.actor, observer->owner, change, error)
        : qa_combat_argument(error, "protection observer is closed or its owner retired");
    return remember_failure(ok, &observer->failed, &observer->failure, error);
}
bool qa_combat_absorb(qa_combat *combat, const qa_damage_request *request, qa_protection_channel channel,
                      const qa_damage_geometry *geometry, float amount, qa_damage_flags flags, const qa_armor_context *context,
                      float *saved, qa_error *error) {
    if (!request || !context || !saved || !isfinite(amount) || (channel != QA_PROTECTION_REGULAR && channel != QA_PROTECTION_POWERED))
        return qa_combat_argument(error, "invalid combat armor stage");
    qa_damage_geometry captured = geometry ? *geometry : (qa_damage_geometry){request->direction, request->point, request->normal};
    if (!qa_vec_finite(captured.direction) || !qa_vec_finite(captured.point) || !qa_vec_finite(captured.normal))
        return qa_combat_argument(error, "invalid armor stage geometry");
    qa_combat_record *entry;
    if (!require_record(combat, request->target, &entry, error)) return false;
    qa_combat_cursor *cursor = cursor_for(combat, request->target);
    if (!cursor || &cursor->outcome->request != request || cursor->reaction_seen)
        return qa_combat_argument(error, "armor stage requires its active damage request");
    qa_combat_protection *slot = &entry->protection[channel];
    if (slot->reserved) {
        if (amount <= 0 || flags.no_armor || (channel == QA_PROTECTION_REGULAR ? flags.no_regular_armor : flags.no_power_armor)) { *saved = 0; return true; }
        if (!slot->bound) return qa_combat_argument(error, "armor stage has an unbound protection owner");
        qa_protection_binding binding = slot->binding;
        qa_protection_observer observer = {.combat = combat, .cursor = cursor,
            .lease = {request->target, slot->serial, channel}, .owner = slot->claim.owner, .open = true};
        float result = 0;
        ++combat->active_calls;
        bool ok = binding.absorb(binding.context, request, &captured, amount, flags, &observer, &result, error);
        --combat->active_calls; observer.open = false;
        if (observer.failed) { if (error) *error = observer.failure; return false; }
        if (!ok) return false;
        if (!isfinite(result) || result < 0 || result > amount) return qa_combat_argument(error, "source armor savings exceed the incoming damage");
        if (!cursor_reconciled(combat, cursor, error)) return false;
        *saved = result; return true;
    }
    qa_combat_state current; qa_armor_result result;
    if (!read_state(combat, entry, &current, error) || !qa_armor_absorb(&current.armor, amount, flags, context, &channel, &result, error)) return false;
    if (!qa_armor_equal(current.armor, result.armor) && !qa_combat_set_armor(combat, request->target, &result.armor, error)) return false;
    *saved = channel == QA_PROTECTION_REGULAR ? result.regular_saved : result.power_saved;
    return true;
}
bool qa_combat_impulse(qa_combat *combat, const qa_damage_request *request, qa_vec3 direction, float amount, qa_error *error) {
    if (!qa_vec_finite(direction) || !isfinite(amount)) return qa_combat_argument(error, "invalid damage momentum");
    if (amount == 0 || !qa_combat_live(combat, request->target)) return true;
    qa_vec3 impulse = qa_vec_scale(qa_vec_normalize(direction), amount);
    if (!qa_vec_finite(impulse)) return qa_combat_argument(error, "damage momentum overflow");
    qa_combat_cursor *cursor = cursor_for(combat, request->target);
    if (!cursor || !journal_reserve(cursor, error)) return false;
    if (!combat->hooks.impulse) return qa_combat_argument(error, "movement impulse has no authoritative consumer");
    qa_damage_mutation mutation = {.kind = QA_MUTATION_IMPULSE, .value.impulse = {impulse, request->attack.movement_provider}};
    ++combat->active_calls;
    bool ok = combat->hooks.impulse(combat->hooks.context, request->target, impulse, request->attack.movement_provider, error);
    --combat->active_calls;
    if (!ok) return false;
    if (!journal_reserve(cursor, error)) return false;
    advance_cursors(combat, request->target, &mutation);
    cursor->outcome->mutations[cursor->outcome->mutation_count++] = mutation;
    return true;
}
void qa_damage_outcome_free(qa_damage_outcome *outcome) {
    if (!outcome) return;
    free(outcome->mutations); memset(outcome, 0, sizeof(*outcome));
}
typedef struct damage_dispatch {
    qa_combat *combat;
    qa_source_damage_fn source;
    void *source_context;
} damage_dispatch;

static bool damage_canonical(void *context, const void *input, void *output, qa_error *error) {
    damage_dispatch *dispatch = context;
    qa_combat *combat = dispatch->combat;
    const qa_damage_request *input_request = input;
    qa_damage_outcome *outcome = output;
    if (!qa_damage_request_validate(input_request, error)) return false;
    *outcome = (qa_damage_outcome){.request = *input_request};
    qa_damage_request *request = &outcome->request;
    qa_combat_record *entry = record(combat, request->target);
    if (!qa_combat_live(combat, request->target)) { outcome->stale = true; return true; }
    if (!entry) return qa_combat_argument(error, "damage target has no combat authority");
    uint64_t serial = entry->serial;
    qa_source_damage_fn source = dispatch->source ? dispatch->source : entry->external ? entry->binding.source_damage : NULL;
    void *source_context = dispatch->source ? dispatch->source_context : entry->binding.context;
    qa_combat_policy policy = {0};
    if (!source) {
        bool found = false;
        for (size_t i = 0; i < combat->policy_count; ++i) if (combat->policies[i].provider == request->attack.combat_provider) { policy = combat->policies[i]; found = true; break; }
        if (!found) return qa_combat_argument(error, "attack names an unregistered combat policy");
        qa_combat_admission admission = entry->external
            ? (qa_combat_admission){entry->binding.context, entry->binding.admit}
            : entry->admission;
        if (admission.admit) {
            if (entry->active_admissions == SIZE_MAX)
                return qa_combat_argument(error, "damage admission nesting exhausted");
            bool handled = false;
            ++entry->active_admissions;
            ++combat->active_calls;
            bool ok = admission.admit(admission.context, request, &handled, error);
            --combat->active_calls;
            if (record(combat, request->target) == entry && entry->serial == serial)
                --entry->active_admissions;
            if (!ok) return false;
            if (handled) goto confirm;
        }
        if (!qa_combat_live(combat, request->target)) { outcome->stale = true; return true; }
        if (record(combat, request->target) != entry || entry->serial != serial) return qa_combat_argument(error, "damage admission changed its owner");
        if (entry->external && entry->binding.adjust) {
            qa_combat_binding binding = entry->binding;
            ++combat->active_calls;
            bool ok = binding.adjust(binding.context, request, &request->amount, &request->knockback, error);
            --combat->active_calls;
            if (!ok || !qa_damage_request_validate(request, error)) return false;
        }
    }
    bool allowed = true;
    if (combat->hooks.damage_allowed) {
        ++combat->active_calls; allowed = combat->hooks.damage_allowed(combat->hooks.context, request); --combat->active_calls;
    }
    if (!qa_combat_live(combat, request->target)) { outcome->stale = true; return true; }
    if (record(combat, request->target) != entry || entry->serial != serial) return qa_combat_argument(error, "damage source changed its owner");
    if (!observe_public(combat, request->target, false, error) || !observe_public(combat, request->target, true, error)) return false;
    qa_combat_state initial; if (!read_state(combat, entry, &initial, error)) return false;
    qa_combat_cursor cursor = {.previous = combat->current, .outcome = outcome, .observed = initial, .binding_serial = serial, .active = true};
    combat->current = &cursor;
    qa_damage_result result = {0}; bool ok = true;
    if (allowed && source) {
        qa_damage_observer observer = {.combat = combat, .cursor = &cursor, .open = true};
        ++combat->active_calls;
        ok = source(source_context, combat, request, &observer, &result, error);
        --combat->active_calls; observer.open = false;
        if (observer.failed) { if (error) *error = observer.failure; ok = false; }
        if (ok && cursor.reaction_seen && (result.applied_damage != cursor.reaction.applied_damage || result.reaction != cursor.reaction.reaction))
            ok = qa_combat_argument(error, "source result differs from its reaction boundary");
        if (ok && !cursor.reaction_seen && result.reaction != QA_REACTION_NONE)
            ok = qa_combat_argument(error, "source damage omitted its reaction boundary");
    } else if (allowed) ok = qa_combat_policy_execute(combat, &policy, request, &result, error);
    if (ok) ok = result_valid(&result, error) && (cursor.reaction_seen || cursor_reconciled(combat, &cursor, error));
    if (ok && !cursor.reaction_seen) ok = before_reaction(combat, &cursor, &result, error);
    if (ok) outcome->result = result;
    if (ok && !source && result.reaction != QA_REACTION_NONE && qa_combat_live(combat, request->target)) {
        if (!combat->hooks.reaction) ok = qa_combat_argument(error, "damage reaction has no source callback owner");
        else {
            ++combat->active_calls; ok = combat->hooks.reaction(combat->hooks.context, outcome, error); --combat->active_calls;
        }
    }
    cursor.active = false; combat->current = cursor.previous;
    if (!ok) return false;
confirm:
    if (qa_combat_live(combat, request->target)) {
        qa_combat_state current;
        if (!qa_combat_read(combat, request->target, &current, error)) return false;
        outcome->survived = current.health > 0;
    }
    if (combat->hooks.confirmed) {
        ++combat->active_calls; bool confirmed = combat->hooks.confirmed(combat->hooks.context, outcome, error); --combat->active_calls;
        if (!confirmed) return false;
    }
    return true;
}
static bool dispatch_damage(qa_combat *combat, const qa_damage_request *request, qa_source_damage_fn source,
                            void *context, qa_damage_outcome *out, qa_error *error) {
    if (!combat || !out || !qa_damage_request_validate(request, error)) return false;
    damage_dispatch dispatch = {combat, source, context};
    qa_damage_outcome result = {0};
    ++combat->active_hits;
    bool ok = qa_operation_dispatch(combat->damage, request, &result, damage_canonical, &dispatch, NULL, NULL, error);
    --combat->active_hits;
    if (!ok) { qa_damage_outcome_free(&result); return false; }
    *out = result; return true;
}
bool qa_combat_apply(qa_combat *combat, const qa_damage_request *request, qa_damage_outcome *out, qa_error *error) {
    return dispatch_damage(combat, request, NULL, NULL, out, error);
}
bool qa_combat_run_source(qa_combat *combat, const qa_damage_request *request, qa_source_damage_fn source,
                          void *context, qa_damage_outcome *out, qa_error *error) {
    if (!source) return qa_combat_argument(error, "source damage requires an executor");
    return dispatch_damage(combat, request, source, context, out, error);
}
qa_operation *qa_combat_damage_operation(qa_combat *combat) { return combat ? combat->damage : NULL; }

bool qa_combat_source_reaction(qa_combat *combat, const qa_damage_request *request, const qa_damage_result *result, qa_error *error) {
    if (!combat || !qa_damage_request_validate(request, error) || !result_valid(result, error)) return false;
    if (result->reaction != QA_REACTION_PAIN && result->reaction != QA_REACTION_DEATH)
        return qa_combat_argument(error, "deferred source reaction requires pain or death");
    if (!qa_combat_live(combat, request->target) || !combat->hooks.before_reaction) return true;
    qa_damage_outcome outcome = {.request = *request, .result = *result};
    ++combat->active_calls;
    bool ok = combat->hooks.before_reaction(combat->hooks.context, &outcome, error);
    --combat->active_calls;
    return ok;
}

bool qa_combat_pickup_begin(qa_combat *combat, qa_actor_id actor, qa_combat_pickup_scope *out, qa_error *error) {
    if (!out || !combat) return qa_combat_argument(error, "invalid pickup combat scope");
    for (qa_combat_pickup_scope *scope = combat->pickups; scope; scope = scope->previous)
        if (scope == out) return qa_combat_argument(error, "pickup combat scope is already open");
    qa_combat_record *entry = record(combat, actor);
    *out = (qa_combat_pickup_scope){.combat = combat, .previous = combat->pickups, .actor = actor,
        .binding_serial = entry ? entry->serial : 0, .open = true};
    combat->pickups = out;
    ++combat->active_calls;
    return true;
}
bool qa_combat_pickup_end(qa_combat_pickup_scope *scope, bool reconcile, qa_error *error) {
    if (!scope || !scope->open || !scope->combat) return qa_combat_argument(error, "pickup combat scope is closed");
    qa_combat *combat = scope->combat;
    if (combat->pickups != scope) return qa_combat_argument(error, "pickup combat scope ended out of order or was copied");
    bool ok = true;
    if (reconcile && qa_combat_live(combat, scope->actor)) {
        qa_combat_record *entry = record(combat, scope->actor);
        if ((entry ? entry->serial : 0) != scope->binding_serial) ok = qa_combat_argument(error, "pickup changed primary combat storage");
        else {
            qa_combat_cursor *cursor = cursor_for(combat, scope->actor);
            if (cursor) {
                qa_combat_state current;
                ok = qa_combat_read(combat, scope->actor, &current, error);
                if (ok && !qa_armor_equal(current.armor, cursor->observed.armor))
                    ok = qa_combat_argument(error, "pickup omitted a committed protection store");
            }
        }
    }
    combat->pickups = scope->previous;
    scope->open = false; --combat->active_calls;
    return ok;
}
