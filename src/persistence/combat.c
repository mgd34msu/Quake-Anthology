#include "qa/persistence_gameplay.h"
#include "../gameplay/combat_internal.h"
#include "../gameplay/inventory_internal.h"
#include "lease_serials.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *text)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }

bool qa_persistence_combat_admission(const qa_combat *combat, qa_actor_id actor, qa_combat_admission *out)
{
    if (!combat || !out || !qa_actors_get(combat->actors, actor)) return false;
    const qa_combat_record *entry = combat->records + actor.slot;
    if (!entry->active || !qa_actor_id_equal(entry->actor, actor) || !entry->admission.admit) return false;
    *out = entry->admission; return true;
}

static bool armor(qa_source_save_io *io, qa_armor *value)
{
    uint32_t regular = value->regular.kind, powered = value->powered.kind,
        edition = value->powered.source_edition, source_kind = value->powered.source_kind;
    if (!qa_source_save_u32(io, &regular) || regular > QA_ARMOR_SOURCE ||
        !qa_source_save_f32(io, &value->regular.points) ||
        !qa_source_save_string(io, &value->regular.item)) return false;
    value->regular.kind = (qa_regular_armor_kind)regular;
    switch (value->regular.kind) {
    case QA_ARMOR_Q1:
        if (!qa_source_save_f32(io, &value->regular.protection.q1_absorption)) return false;
        break;
    case QA_ARMOR_Q2:
        if (!qa_source_save_f32(io, &value->regular.protection.q2.normal) ||
            !qa_source_save_f32(io, &value->regular.protection.q2.energy)) return false;
        break;
    case QA_ARMOR_Q3:
        if (!qa_source_save_f32(io, &value->regular.protection.q3_protection)) return false;
        break;
    default: break;
    }
    if (!qa_source_save_u32(io, &powered) || powered > QA_POWER_SHIELD ||
        !qa_source_save_f32(io, &value->powered.cells) ||
        !qa_source_save_string(io, &value->powered.source_owner) ||
        !qa_source_save_u32(io, &edition) || !qa_source_save_u32(io, &source_kind)) return false;
    value->powered.kind = (qa_power_kind)powered;
    value->powered.source_edition = (qa_q2_power_armor_edition)edition;
    value->powered.source_kind = (qa_power_armor_source)source_kind;
    return qa_armor_validate(value, io->error);
}

static bool state(qa_source_save_io *io, qa_combat_state *value)
{
    return qa_source_save_f32(io, &value->health) && isfinite(value->health) &&
        qa_source_save_f32(io, &value->mass) && isfinite(value->mass) && armor(io, &value->armor) &&
        qa_source_save_bool(io, &value->can_take_damage) && qa_source_save_bool(io, &value->invulnerable) &&
        qa_source_save_bool(io, &value->no_knockback) && qa_source_save_string(io, &value->team);
}

static bool attack(qa_source_save_io *io, qa_attack *value)
{
    uint32_t kind = value->cause.kind;
    if (!qa_source_save_u64(io, &value->sequence) || !qa_source_save_u64(io, &value->time_ns) ||
        !qa_source_save_actor(io, &value->attacker) || !qa_source_save_actor(io, &value->inflictor) ||
        !qa_source_save_actor(io, &value->projectile) || !qa_source_save_string(io, &value->weapon) ||
        !qa_source_save_string(io, &value->weapon_provider) ||
        !qa_source_save_string(io, &value->combat_provider) ||
        !qa_source_save_string(io, &value->inventory_provider) ||
        !qa_source_save_string(io, &value->movement_provider) ||
        !qa_source_save_bool(io, &value->powerup_applied) ||
        !qa_source_save_string(io, &value->powerup_owner) || !qa_source_save_u32(io, &kind)) return false;
    if (kind > QA_CAUSE_ENVIRONMENT) return fail(io->error, "Invalid saved last-attack cause");
    value->cause.kind = (qa_cause_kind)kind;
    switch (value->cause.kind) {
    case QA_CAUSE_Q1: {
        uint32_t policy = value->cause.source.q1.armor;
        if (!qa_source_save_string(io, &value->cause.source.q1.death_type) ||
            !qa_source_save_u32(io, &policy)) return false;
        if (policy > QA_Q1_ARMOR_HALF) return fail(io->error, "Invalid saved last-attack Q1 armor policy");
        value->cause.source.q1.armor = (qa_q1_armor_effect)policy;
        return true;
    }
    case QA_CAUSE_Q2: {
        uint32_t edition = value->cause.source.q2.native;
        if (!qa_source_save_i32(io, &value->cause.source.q2.means_of_death) ||
            !qa_source_save_u32(io, &value->cause.source.q2.flags) ||
            !qa_source_save_u32(io, &edition) ||
            !qa_source_save_i32(io, &value->cause.source.q2.native_value) ||
            !qa_source_save_u32(io, &value->cause.source.q2.classic_product) ||
            !qa_source_save_bool(io, &value->cause.source.q2.friendly_fire) ||
            !qa_source_save_bool(io, &value->cause.source.q2.no_point_loss)) return false;
        if (edition > QA_Q2_CAUSE_RERELEASE) return fail(io->error, "Invalid saved last-attack Q2 edition");
        value->cause.source.q2.native = (qa_q2_native_edition)edition;
        return true;
    }
    case QA_CAUSE_Q3:
        return qa_source_save_i32(io, &value->cause.source.q3.means_of_death) &&
            qa_source_save_u32(io, &value->cause.source.q3.flags);
    case QA_CAUSE_ENVIRONMENT: {
        uint32_t hazard = value->cause.source.hazard;
        if (!qa_source_save_u32(io, &hazard)) return false;
        if (hazard > QA_HAZARD_TRIGGER) return fail(io->error, "Invalid saved last-attack hazard");
        value->cause.source.hazard = (qa_hazard)hazard;
        return true;
    }
    }
    return fail(io->error, "Invalid saved last-attack cause");
}

static bool same_state(const qa_combat_state *a, const qa_combat_state *b)
{
    return a->health == b->health && a->mass == b->mass && qa_armor_equal(a->armor, b->armor) &&
        a->can_take_damage == b->can_take_damage && a->invulnerable == b->invulnerable &&
        a->no_knockback == b->no_knockback && a->team == b->team;
}

static bool same_attack(const qa_attack *a, const qa_attack *b)
{
    if (a->sequence != b->sequence || a->time_ns != b->time_ns ||
        !qa_actor_id_equal(a->attacker, b->attacker) || !qa_actor_id_equal(a->inflictor, b->inflictor) ||
        !qa_actor_id_equal(a->projectile, b->projectile) || a->weapon != b->weapon ||
        a->weapon_provider != b->weapon_provider || a->combat_provider != b->combat_provider ||
        a->inventory_provider != b->inventory_provider || a->movement_provider != b->movement_provider ||
        a->powerup_applied != b->powerup_applied || a->powerup_owner != b->powerup_owner ||
        a->cause.kind != b->cause.kind) return false;
    switch (a->cause.kind) {
    case QA_CAUSE_Q1:
        return a->cause.source.q1.death_type == b->cause.source.q1.death_type &&
            a->cause.source.q1.armor == b->cause.source.q1.armor;
    case QA_CAUSE_Q2:
        return a->cause.source.q2.means_of_death == b->cause.source.q2.means_of_death &&
            a->cause.source.q2.flags == b->cause.source.q2.flags &&
            a->cause.source.q2.native == b->cause.source.q2.native &&
            a->cause.source.q2.native_value == b->cause.source.q2.native_value &&
            a->cause.source.q2.classic_product == b->cause.source.q2.classic_product &&
            a->cause.source.q2.friendly_fire == b->cause.source.q2.friendly_fire &&
            a->cause.source.q2.no_point_loss == b->cause.source.q2.no_point_loss;
    case QA_CAUSE_Q3:
        return a->cause.source.q3.means_of_death == b->cause.source.q3.means_of_death &&
            a->cause.source.q3.flags == b->cause.source.q3.flags;
    case QA_CAUSE_ENVIRONMENT: return a->cause.source.hazard == b->cause.source.hazard;
    }
    return false;
}

static uint32_t binding_mask(const qa_combat_binding *b)
{
    return (b->read ? 1u : 0u) | (b->write_health ? 2u : 0u) | (b->write_armor ? 4u : 0u) |
        (b->validate_armor ? 8u : 0u) | (b->write_traits ? 16u : 0u) |
        (b->empty_regular_armor ? 32u : 0u) | (b->normalize_legacy_armor ? 64u : 0u) |
        (b->admit ? 128u : 0u) | (b->adjust ? 256u : 0u) | (b->source_damage ? 512u : 0u);
}

static uint32_t protection_mask(const qa_protection_binding *b)
{ return (b->read ? 1u : 0u) | (b->validate_write ? 2u : 0u) | (b->write ? 4u : 0u) | (b->absorb ? 8u : 0u); }

static bool source_primary(qa_combat *combat, const qa_combat_binding *binding,
                             qa_combat_state *out, qa_error *error)
{
    ++combat->active_calls;
    bool ok = binding->read(binding->context, out, error);
    --combat->active_calls; return ok;
}

static bool source_protection(qa_combat *combat, const qa_protection_binding *binding,
                                qa_armor *out, qa_error *error)
{
    ++combat->active_calls;
    bool ok = binding->read(binding->context, out, error);
    --combat->active_calls; return ok;
}

static bool signature(qa_source_save_io *io)
{
    unsigned char actual[8] = {'Q','A','C','O','M','B','A','T'};
    static const unsigned char expected[8] = {'Q','A','C','O','M','B','A','T'};
    uint32_t version = 4;
    return qa_source_save_bytes(io, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual)) &&
        qa_source_save_u32(io, &version) && version == 4;
}

static bool policies(qa_source_save_io *io, qa_combat *combat)
{
    size_t count = combat->policy_count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(qa_combat_policy)) || count != combat->policy_count)
        return fail(io->error, "Saved combat policy inventory differs from prepared providers");
    for (size_t i = 0; i < count; ++i) {
        qa_actor_owner owner = combat->policies[i].provider;
        uint32_t family = combat->policies[i].family;
        if (!qa_source_save_string(io, &owner) || !qa_source_save_u32(io, &family) ||
            owner != combat->policies[i].provider || family != (uint32_t)combat->policies[i].family)
            return fail(io->error, "Saved combat policy ordering differs from prepared providers");
    }
    return true;
}

static bool record(qa_source_save_io *io, qa_combat *combat, qa_inventory *inventory,
                   qa_combat_record *entry, const qa_persistence_gameplay_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool admission = entry->admission.admit != NULL;
    bool fuel = entry->power_inventory != NULL;
    qa_combat_state primary = entry->state;
    uint32_t mask = binding_mask(&entry->binding);
    if (!reading && !qa_combat_read_traits(combat, entry->actor, &primary, io->error)) return false;
    if (!qa_source_save_actor(io, &entry->actor) || !qa_source_save_u64(io, &entry->serial) ||
        !entry->serial || !qa_actors_get(combat->actors, entry->actor) ||
        !qa_source_save_bool(io, &entry->external) || !state(io, &primary) ||
        !qa_source_save_bool(io, &entry->has_last_attack) ||
        (entry->has_last_attack && !attack(io, &entry->last_attack)) ||
        !qa_source_save_bool(io, &admission) || !qa_source_save_bool(io, &fuel) ||
        !qa_source_save_string(io, &entry->power_item)) return false;
    if ((!fuel && entry->power_item) || (fuel && (!entry->power_item || (!reading && entry->power_inventory != inventory))) ||
        (entry->external && admission)) return fail(io->error, "Invalid saved combat storage ownership");
    if (reading) { entry->active = true; entry->state = primary; entry->power_inventory = fuel ? inventory : NULL; }
    if (entry->external) {
        bool source_stages[2] = {entry->binding.source_armor_stages[0], entry->binding.source_armor_stages[1]};
        bool has_primary[2] = {entry->binding.has_primary_protection[0], entry->binding.has_primary_protection[1]};
        qa_actor_owner owners[2] = {entry->binding.primary_protection[0], entry->binding.primary_protection[1]};
        if (!qa_source_save_u32(io, &mask)) return false;
        for (size_t i = 0; i < 2; ++i)
            if (!qa_source_save_bool(io, &source_stages[i]) || !qa_source_save_bool(io, &has_primary[i]) ||
                !qa_source_save_string(io, &owners[i])) return false;
        if (reading) {
            qa_combat_state observed;
            if (!resolve || !resolve->combat || !resolve->combat(resolve->context, entry->actor, entry->serial, &entry->binding, io->error) ||
                binding_mask(&entry->binding) != mask || (mask & 7u) != 7u ||
                !source_primary(combat, &entry->binding, &observed, io->error) || !same_state(&primary, &observed))
                return fail(io->error, "Restored source combat authority differs from saved primary");
            for (size_t i = 0; i < 2; ++i)
                if (entry->binding.source_armor_stages[i] != source_stages[i] ||
                    entry->binding.has_primary_protection[i] != has_primary[i] || entry->binding.primary_protection[i] != owners[i])
                    return fail(io->error, "Restored combat primary protection ownership differs");
        }
    } else if (primary.armor.regular.kind == QA_ARMOR_SOURCE)
        return fail(io->error, "Local combat cannot own source armor");
    if (reading && admission && (!resolve || !resolve->admission ||
        !resolve->admission(resolve->context, entry->actor, &entry->admission, io->error) || !entry->admission.admit))
        return fail(io->error, "Saved combat damage admission has no restored source callback");
    for (size_t channel = 0; channel < 2; ++channel) {
        qa_combat_protection *slot = entry->protection + channel;
        if (!qa_source_save_bool(io, &slot->reserved)) return false;
        if (!slot->reserved) continue;
        uint32_t mode = slot->claim.admission, callbacks = protection_mask(&slot->binding);
        qa_armor reservoir = primary.armor;
        if (!reading && slot->bound && !source_protection(combat, &slot->binding, &reservoir, io->error)) return false;
        if (!qa_source_save_u64(io, &slot->serial) || !slot->serial ||
            !qa_source_save_string(io, &slot->claim.owner) || !slot->claim.owner ||
            !qa_source_save_string(io, &slot->claim.expected_owner) || !qa_source_save_u32(io, &slot->claim.rule) ||
            !qa_source_save_u32(io, &mode) || mode > QA_PROTECTION_REPLACE_CURRENT ||
            !qa_source_save_bool(io, &slot->bound)) return false;
        slot->claim.admission = (qa_protection_admission)mode;
        if (!slot->bound) continue;
        if (!qa_source_save_u32(io, &callbacks) || !armor(io, &reservoir)) return false;
        if (channel == QA_PROTECTION_POWERED && reservoir.powered.kind != QA_POWER_NONE &&
            reservoir.powered.source_kind == QA_POWER_SOURCE_GENERIC && reservoir.powered.source_owner != slot->claim.owner)
            return fail(io->error, "Generic powered reservoir differs from its actual absorption owner");
        if (reading) {
            qa_armor observed = primary.armor;
            if (!resolve || !resolve->protection ||
                !resolve->protection(resolve->context, entry->actor, (qa_protection_channel)channel,
                                     &slot->claim, &slot->binding, io->error) ||
                protection_mask(&slot->binding) != callbacks || callbacks != 15u ||
                !source_protection(combat, &slot->binding, &observed, io->error) ||
                (channel == QA_PROTECTION_REGULAR ? !qa_regular_armor_equal(reservoir.regular, observed.regular) :
                    !qa_powered_armor_equal(reservoir.powered, observed.powered)))
                return fail(io->error, "Restored protection reservoir differs from saved source authority");
        }
    }
    return true;
}

static bool safe(qa_session *session, qa_combat *combat, qa_inventory *inventory, qa_error *error)
{
    return (session && combat && inventory && combat->actors == qa_session_actors(session) &&
        inventory->actors == combat->actors && !inventory->calls &&
        qa_combat_idle(combat) && !combat->admissions && !combat->admission_count &&
        !combat->current && !combat->pickups) || fail(error, "Combat persistence requires idle candidate-owned stores");
}

bool qa_persistence_combat_capture(qa_session *session, qa_combat *combat, qa_inventory *inventory,
                                   qa_buffer *out, qa_error *error)
{
    if (!out || !safe(session, combat, inventory, error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, session, error)) return false;
    uint32_t capacity = qa_actors_capacity(combat->actors);
    size_t count = 0;
    for (uint32_t i = 0; i < capacity; ++i) if (combat->records[i].active) ++count;
    uint64_t next = combat->next_serial;
    bool ok = signature(&io) && qa_source_save_u64(&io, &next) && policies(&io, combat) &&
        qa_source_save_count(&io, &count, capacity);
    for (uint32_t i = 0; ok && i < capacity; ++i) if (combat->records[i].active) {
        qa_combat_record copy = combat->records[i];
        if (copy.active_admissions || copy.power_admitting) ok = fail(error, "Combat actor is admitting a mutation");
        else ok = record(&io, combat, inventory, &copy, NULL);
        const qa_combat_record *current = combat->records + i;
        if (ok && (!current->active || !qa_actor_id_equal(copy.actor, current->actor) ||
            copy.serial != current->serial || copy.external != current->external ||
            copy.power_inventory != current->power_inventory || copy.power_item != current->power_item ||
            copy.has_last_attack != current->has_last_attack ||
            (copy.has_last_attack && !same_attack(&copy.last_attack, &current->last_attack)) ||
            !same_state(&copy.state, &current->state))) ok = fail(error, "Combat storage changed during capture");
        for (size_t channel = 0; ok && channel < 2; ++channel)
            if (copy.protection[channel].serial != current->protection[channel].serial ||
                copy.protection[channel].reserved != current->protection[channel].reserved ||
                copy.protection[channel].bound != current->protection[channel].bound)
                ok = fail(error, "Protection ownership changed during capture");
    }
    if (ok) ok = safe(session, combat, inventory, error) && next == combat->next_serial && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_persistence_combat_restore(qa_session *session, qa_combat *combat, qa_inventory *inventory,
    const qa_persistence_gameplay_resolvers *resolve, qa_bytes bytes, qa_error *error)
{
    if (!safe(session, combat, inventory, error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, session, bytes, error)) return false;
    uint32_t capacity = qa_actors_capacity(combat->actors);
    qa_combat_record *records = calloc(capacity, sizeof(*records));
    if (!records) { qa_source_save_dispose(&io); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating saved combat records"); return false; }
    uint64_t next = 0;
    uint64_t *serials = NULL; size_t serial_count = 0, serial_capacity = 0;
    size_t count = 0;
    bool ok = signature(&io) && qa_source_save_u64(&io, &next) && next && policies(&io, combat) &&
        qa_source_save_count(&io, &count, capacity);
    uint32_t previous = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_combat_record entry = {0};
        ok = record(&io, combat, inventory, &entry, resolve);
        if (ok && ((i && entry.actor.slot <= previous) || entry.serial >= next))
            ok = fail(error, "Saved combat records are unordered or have invalid ownership serials");
        if (ok) ok = persistence_serial_append(&serials, &serial_count, &serial_capacity, entry.serial, next - 1, error);
        for (size_t channel = 0; ok && channel < 2; ++channel)
            if (entry.protection[channel].reserved)
                ok = persistence_serial_append(&serials, &serial_count, &serial_capacity,
                    entry.protection[channel].serial, next - 1, error);
        if (ok) { previous = entry.actor.slot; records[entry.actor.slot] = entry; }
    }
    if (ok) ok = persistence_serial_unique(serials, serial_count, error) &&
        qa_source_save_finish(&io, NULL) && safe(session, combat, inventory, error);
    if (ok) { free(combat->records); combat->records = records; combat->next_serial = next; records = NULL; }
    else if (!error || error->code == QA_OK) fail(error, "Invalid saved combat continuation");
    free(serials); free(records); qa_source_save_dispose(&io);
    return ok;
}

bool qa_persistence_combat_validate(qa_combat *combat, qa_inventory *inventory, qa_error *error)
{
    if (!combat || !inventory || !qa_combat_idle(combat)) return fail(error, "Combat validation requires idle stores");
    for (uint32_t i = 0; i < qa_actors_capacity(combat->actors); ++i) {
        qa_combat_record *entry = combat->records + i;
        if (!entry->active) continue;
        if (!qa_actors_get(combat->actors, entry->actor)) return fail(error, "Saved combat actor is not live");
        if (entry->power_inventory) {
            qa_inventory_entry fuel;
            if (entry->power_inventory != inventory ||
                !qa_inventory_entry_read(inventory, entry->actor, entry->power_item, &fuel, error) || !isfinite((float)fuel.count))
                return fail(error, "Saved combat power inventory has no compatible restored reservoir");
        }
    }
    return true;
}
