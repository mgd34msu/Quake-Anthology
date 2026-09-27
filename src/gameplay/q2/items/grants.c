#include "internal.h"

static bool give(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d, int amount,
                 double *given, qa_error *e) {
    return q2_item_ensure(g, id, d, e) &&
           qa_inventory_give(g->services.inventory, id, d->item, amount, given, e);
}
static bool select_picked(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d, bool first,
                          qa_error *e) {
    if (!first || d->weapon == QA_Q2_WEAPON_NONE || !q2_actor_live(g, id))
        return true;
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (!a)
        return false;
    if (!a->weapon_bound)
        return true;
    if (g->options.deathmatch && a->weapon.weapon != QA_Q2_BLASTER)
        return true;
    qa_q2_selection result;
    return qa_q2_weapon_select(g, id, d->weapon, false, &result, e);
}
static bool ammo_pack(qa_q2_game *g, qa_actor_id id, bool full, qa_error *e) {
    static const struct {
        const char *name;
        int band, pack;
        bool band_grant;
    } changes[] = {{"ammo_bullets", 250, 300, true},   {"ammo_shells", 150, 200, true},
                   {"ammo_cells", 250, 300, false},    {"ammo_slugs", 75, 100, false},
                   {"ammo_rockets", 50, 100, false},   {"ammo_grenades", 50, 100, false},
                   {"ammo_magslug", 75, 100, false},   {"ammo_flechettes", 250, 200, false},
                   {"ammo_disruptor", 150, 200, false}};
    for (size_t i = 0; i < sizeof(changes) / sizeof(*changes); ++i) {
        const qa_q2_item_definition *d = qa_q2_item_lookup(g, changes[i].name);
        if (!d)
            continue;
        if (!q2_item_ensure(g, id, d, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, id, d->item, &entry, e))
            return false;
        int capacity = full ? changes[i].pack : changes[i].band;
        if (entry.capacity < capacity) {
            entry.capacity = capacity;
            if (!qa_inventory_configure(g->services.inventory, id, &entry, NULL, NULL, e))
                return false;
        }
        if (!q2_actor_live(g, id))
            return true;
        double given;
        if ((full || changes[i].band_grant) &&
            !qa_inventory_give(g->services.inventory, id, d->item, d->quantity, &given, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    return true;
}
static bool armor(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d,
                  const qa_regular_armor *old, bool *accepted, qa_error *e) {
    *accepted = false;
    if (old->kind == QA_ARMOR_SOURCE)
        return true;
    qa_regular_armor next = *old;
    const qa_q2_item_definition *jacket = qa_q2_item_lookup(g, "item_armor_jacket");
    if (d->kind == QA_Q2_ITEM_SHARD) {
        if (old->kind == QA_ARMOR_Q2 && old->points > 0)
            next.points += 2;
        else
            next = (qa_regular_armor){.kind = QA_ARMOR_Q2,
                                      .points = 2,
                                      .item = jacket->item,
                                      .protection.q2 = {.normal = .3f, .energy = 0}};
    } else if (old->kind != QA_ARMOR_Q2 || old->points == 0 ||
               d->normal_protection > old->protection.q2.normal) {
        float salvage = old->kind == QA_ARMOR_Q2
                            ? truncf(old->points * old->protection.q2.normal / d->normal_protection)
                            : 0;
        next = (qa_regular_armor){
            .kind = QA_ARMOR_Q2,
            .item = d->item,
            .points = fminf((float)d->capacity, (float)d->quantity + salvage),
            .protection.q2 = {.normal = d->normal_protection, .energy = d->energy_protection}};
    } else {
        const qa_q2_item_definition *previous = q2_item_by_id(g, old->item);
        float maximum = previous ? previous->capacity : 200;
        float salvage =
            old->protection.q2.normal > 0
                ? truncf((float)d->quantity * d->normal_protection / old->protection.q2.normal)
                : 0;
        next.points = fminf(maximum, old->points + salvage);
        if (next.points <= old->points)
            return true;
    }
    if (!qa_combat_set_regular_armor(g->services.combat, id, &next, e))
        return false;
    *accepted = true;
    return true;
}
bool q2_item_grant(qa_q2_game *g, q2_actor *a, qa_actor_id recipient, bool *accepted, qa_error *e) {
    q2_item_state *item = a->item;
    const qa_q2_item_definition *d = item->definition;
    *accepted = false;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, recipient, &combat, e))
        return false;
    q2_power_state *powers = q2_powers(g, recipient, e);
    if (!powers)
        return false;
    bool dropped = (item->spawn.spawnflags & 0x30000) != 0;
    bool instanced = g->item_runtime->options.instanced_coop && g->options.cooperative;
    int previous = 0;
    double given;
    switch (d->kind) {
    case QA_Q2_ITEM_HEALTH:
        if (!d->ignore_maximum && combat.health >= powers->maximum_health)
            return true;
        if (!qa_combat_set_health(
                g->services.combat, recipient,
                d->ignore_maximum
                    ? combat.health + (item->spawn.count ? item->spawn.count : d->quantity)
                    : fminf(powers->maximum_health,
                            combat.health + (item->spawn.count ? item->spawn.count : d->quantity)),
                e))
            return false;
        break;
    case QA_Q2_ITEM_FOOD:
        if (!qa_combat_set_health(g->services.combat, recipient, combat.health + item->spawn.count,
                                  e))
            return false;
        break;
    case QA_Q2_ITEM_ARMOR:
    case QA_Q2_ITEM_SHARD:
        return armor(g, recipient, d, &combat.armor.regular, accepted, e);
    case QA_Q2_ITEM_MAX_HEALTH:
        powers->maximum_health += d->fill && g->options.deathmatch ? 0 : d->quantity;
        if (d->fill && combat.health < powers->maximum_health &&
            !qa_combat_set_health(g->services.combat, recipient, powers->maximum_health, e))
            return false;
        break;
    case QA_Q2_ITEM_PACK:
        if (!ammo_pack(g, recipient, d->full_pack, e))
            return false;
        break;
    case QA_Q2_ITEM_KEY:
        if (!q2_item_ensure(g, recipient, d, e) || !q2_count(g, recipient, d->item, &previous, e))
            return false;
        if (g->options.cooperative) {
            if (!strcmp(d->classname, "key_power_cube") ||
                !strcmp(d->classname, "key_explosive_charges")) {
                uint32_t cubes = (item->spawn.spawnflags & 0xff00u) >> 8;
                if (powers->power_cubes & cubes)
                    return true;
                powers->power_cubes |= cubes;
            } else if (previous)
                return true;
        }
        if (!qa_inventory_give(g->services.inventory, recipient, d->item, 1, &given, e))
            return false;
        break;
    case QA_Q2_ITEM_AMMO: {
        int quantity = d->weapon != QA_Q2_WEAPON_NONE && d->infinite_quantity &&
                               (g->options.deathmatch_flags & 8192)
                           ? 1000
                       : item->spawn.count ? item->spawn.count
                                           : d->quantity;
        qa_supply *supply = q2_item_supply(g, recipient);
        if (!q2_actor_live(g, recipient) || !q2_actor_live(g, a->id))
            return true;
        if (supply && qa_supply_maps(supply, d->item, false)) {
            qa_pickup_grant grant = {d->item, quantity};
            return d->weapon == QA_Q2_WEAPON_NONE
                       ? qa_supply_ammo(supply, recipient, grant, false, accepted, e)
                       : qa_supply_ammo_weapon(supply, recipient, d->item, grant,
                                               QA_PICKUP_SWITCH_ALWAYS, true, accepted, e);
        }
        if (!q2_item_ensure(g, recipient, d, e) || !q2_count(g, recipient, d->item, &previous, e) ||
            !qa_inventory_give(g->services.inventory, recipient, d->item, quantity, &given, e))
            return false;
        if (given == 0)
            return true;
        if (!select_picked(g, recipient, d, previous == 0, e))
            return false;
        break;
    }
    case QA_Q2_ITEM_WEAPON: {
        qa_supply *supply = q2_item_supply(g, recipient);
        if (!q2_actor_live(g, recipient) || !q2_actor_live(g, a->id))
            return true;
        bool mapped = supply && qa_supply_maps(supply, d->item, true);
        if (mapped && d->weapon == QA_Q2_BLASTER)
            return true;
        if (mapped) {
            bool owned;
            if (!qa_supply_owns(supply, recipient, d->item, &owned, e))
                return false;
            previous = owned ? 1 : 0;
        } else if (!q2_item_ensure(g, recipient, d, e) ||
                   !q2_count(g, recipient, d->item, &previous, e))
            return false;
        bool stays = g->options.cooperative
                         ? !instanced
                         : g->options.deathmatch && (g->options.deathmatch_flags & 4);
        if (stays && previous > 0 && !dropped)
            return true;
        const qa_q2_item_definition *ammo = d->ammo ? q2_item_by_id(g, d->ammo) : NULL;
        qa_pickup_grant grant = {
            .item = d->ammo,
            .amount = ammo ? ((g->options.deathmatch_flags & 8192) ? 1000 : ammo->quantity) : 0};
        bool with_ammo = ammo && !(item->spawn.spawnflags & 0x10000);
        if (mapped) {
            qa_supply_offer offer = {.kind = QA_SUPPLY_WEAPON,
                                     .item = d->item,
                                     .ammo = with_ammo ? &grant : NULL,
                                     .ammo_count = with_ammo ? 1 : 0};
            qa_supply_options options = {.selection = previous == 0 ? QA_PICKUP_SWITCH_ALWAYS
                                                                    : QA_PICKUP_SWITCH_NEVER};
            return qa_supply_apply(supply, recipient, &offer, &options, accepted, e);
        }
        if (!qa_inventory_give(g->services.inventory, recipient, d->item, 1, &given, e))
            return false;
        if (!q2_actor_live(g, recipient))
            return true;
        if (with_ammo && (!q2_item_ensure(g, recipient, ammo, e) ||
                          !qa_inventory_give(g->services.inventory, recipient, ammo->item,
                                             grant.amount, &given, e)))
            return false;
        if (!select_picked(g, recipient, d, previous == 0, e))
            return false;
        break;
    }
    case QA_Q2_ITEM_POWER:
    case QA_Q2_ITEM_SPHERE:
    case QA_Q2_ITEM_COMPASS: {
        if (d->inventory_only)
            return true;
        if (!q2_item_ensure(g, recipient, d, e) || !q2_count(g, recipient, d->item, &previous, e))
            return false;
        if ((g->options.edition == QA_Q2_RERELEASE && g->options.skill == 0 && previous >= 3) ||
            (g->options.skill == 1 && previous >= 2) || (g->options.skill >= 2 && previous >= 1) ||
            (g->options.cooperative && !instanced && d->coop_stay && previous > 0))
            return true;
        if (d->kind == QA_Q2_ITEM_SPHERE && q2_actor_live(g, powers->sphere))
            return true;
        if (!qa_inventory_give(g->services.inventory, recipient, d->item, 1, &given, e))
            return false;
        if (given == 0)
            return true;
        if (!q2_actor_live(g, recipient))
            return true;
        bool death_drop = (item->spawn.spawnflags & 0x20000) &&
                          (d->powerup == QA_Q2_POWER_QUAD || d->powerup == QA_Q2_POWER_QUADFIRE);
        bool mission_power = d->powerup == QA_Q2_POWER_QUADFIRE ||
                             d->powerup == QA_Q2_POWER_DOUBLE || d->powerup == QA_Q2_POWER_IR;
        bool instant =
            ((g->options.deathmatch || mission_power) && (g->options.deathmatch_flags & 16)) ||
            death_drop;
        if (instant && d->kind != QA_Q2_ITEM_COMPASS) {
            bool used;
            uint64_t duration = d->powerup == QA_Q2_POWER_IR ? 60 * Q2_NS : 30 * Q2_NS;
            if (death_drop && item->expires_ns)
                duration = item->expires_ns > g->now_ns ? item->expires_ns - g->now_ns : 0;
            if (!q2_item_use_duration(g, recipient, d, duration, &used, e))
                return false;
        }
        break;
    }
    case QA_Q2_ITEM_POWER_ARMOR:
        if (!q2_item_ensure(g, recipient, d, e) || !q2_count(g, recipient, d->item, &previous, e) ||
            !qa_inventory_give(g->services.inventory, recipient, d->item, 1, &given, e))
            return false;
        if (g->options.deathmatch && previous == 0 && q2_actor_live(g, recipient)) {
            bool used;
            if (!qa_q2_item_use(g, recipient, d->item, &used, e))
                return false;
        }
        break;
    case QA_Q2_ITEM_DECOY:
        if (!g->options.deathmatch)
            return true; /* same bounded inventory grant below */
        if (!give(g, recipient, d, 1, &given, e))
            return false;
        if (given == 0)
            return true;
        break;
    case QA_Q2_ITEM_NUKE:
    case QA_Q2_ITEM_FLASHLIGHT:
        if (!give(g, recipient, d, 1, &given, e))
            return false;
        if (given == 0)
            return true;
        break;
    }
    *accepted = true;
    return true;
}
