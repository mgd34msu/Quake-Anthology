#include "internal.h"
#include "qa/game_q3_save.h"
#include <ctype.h>

const qa_q3_item *qa_q3_game_items(const qa_q3_game *game, size_t *count) {
    if (!count) return NULL;
    if (!game) { *count = 0; return NULL; }
    return qa_q3_items(game->options.product, count);
}

size_t q3_inventory_arsenal_entries(const qa_q3_game *game, bool spawn,
                                    qa_inventory_entry *out) {
    size_t count = 0;
    int limit = game->options.product == QA_Q3_ARENA ? 11 : QA_Q3_WEAPON_COUNT;
    for (int weapon = 1; weapon < limit; ++weapon) {
        out[count++] = (qa_inventory_entry){
            .item = game->weapon_items[weapon], .capacity = 1,
            .count = spawn && (weapon == QA_Q3_W_GAUNTLET || weapon == QA_Q3_W_MACHINEGUN) ? 1 : 0,
            .policy = QA_COUNT_SOURCE_INT32};
        if (game->ammo_items[weapon])
            out[count++] = (qa_inventory_entry){
                .item = game->ammo_items[weapon], .capacity = 200,
                .count = spawn && weapon == QA_Q3_W_MACHINEGUN
                    ? (game->options.rules.game_type == 3 ? 50 : 100) : 0,
                .policy = QA_COUNT_SOURCE_INT32};
    }
    return count;
}

static bool invoke_source(void *opaque, qa_item_id item, qa_item_action action, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    qa_q3_game *game = owner->game;
    q3_actor *actor = q3_actor_get(game, owner->actor);
    if (!actor || actor->kind != Q3_ACTOR_PLAYER || action != QA_ITEM_USE)
        return q3_fail(error, "invalid Q3 inventory action");
    qa_q3_player_state *player = &actor->state.player;
    if (player->selections & QA_Q3_ARSENAL)
        for (int weapon = 1; weapon < QA_Q3_WEAPON_COUNT; ++weapon)
            if (game->weapon_items[weapon] == item) {
                if (game->options.hooks.inventory_weapon_request) {
                    bool handled = false;
                    if (!game->options.hooks.inventory_weapon_request(game->options.hooks.context,
                            owner->actor, item, &handled, error)) return false;
                    actor = q3_actor_get(game, owner->actor);
                    if (handled || !actor || actor->kind != Q3_ACTOR_PLAYER) return true;
                    if (!(actor->state.player.selections & QA_Q3_ARSENAL))
                        return q3_fail(error, "Q3 weapon request lost its actual arsenal selection");
                }
                bool owned = q3_owns_weapon(game, owner->actor, (qa_q3_weapon)weapon);
                actor = q3_actor_get(game, owner->actor);
                if (owned && actor && actor->kind == Q3_ACTOR_PLAYER)
                    actor->state.player.requested_weapon = (qa_q3_weapon)weapon;
                return true;
            }
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if (player->selections & QA_Q3_EQUIPMENT)
        for (size_t i = 1; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_HOLDABLE && game->item_ids[i] == item)
                return qa_q3_activate_holdable(game, owner->actor,
                                                (qa_q3_holdable)items[i].tag, false, error);
    return q3_fail(error, "Q3 inventory item is not admitted");
}
static bool invoke(void *opaque, qa_item_id item, qa_item_action action, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    qa_q3_game *game = owner->game;
    if (game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 inventory action nesting exhausted");
    ++game->observation_depth;
    bool okay = invoke_source(opaque, item, action, error);
    --game->observation_depth;
    return okay;
}
bool q3_inventory_holdable_changed(qa_q3_game *game, qa_actor_id actor,
                                    qa_q3_holdable before, qa_q3_holdable after,
                                    qa_error *error) {
    q3_inventory_owner *owner = &game->inventory_owners[actor.slot];
    if (before == after || !qa_actor_id_equal(owner->actor, actor) ||
        !qa_inventory_lease_current(game->options.services.inventory, owner->holdables))
        return true;
    qa_inventory_change changes[2];
    size_t count, used = 0;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == QA_Q3_ITEM_HOLDABLE &&
            (items[i].tag == (int32_t)before || items[i].tag == (int32_t)after)) {
            qa_inventory_entry old = {.item = game->item_ids[i], .capacity = 1,
                .count = items[i].tag == (int32_t)before ? 1 : 0, .policy = QA_COUNT_SOURCE_INT32};
            qa_inventory_entry next = old;
            next.count = items[i].tag == (int32_t)after ? 1 : 0;
            changes[used++] = (qa_inventory_change){.actor = actor, .had_before = true,
                                                   .before = old, .after = next};
        }
    return qa_inventory_source_stored(game->options.services.inventory, owner->holdables,
                                       changes, used, error);
}
static bool equipment_item(const qa_q3_item *item) {
    return item->kind == QA_Q3_ITEM_HOLDABLE || item->kind == QA_Q3_ITEM_PERSISTENT;
}
static qa_item_definition item_definition(const qa_q3_game *game, const qa_q3_item *item,
                                           size_t index) {
    bool weapon = item->kind == QA_Q3_ITEM_WEAPON;
    return (qa_item_definition){.item = game->item_ids[index], .owner = game->options.owner,
        .ammo = weapon ? game->ammo_items[item->tag] : 0, .label = item->name, .weapon = weapon,
        .actions = weapon || item->kind == QA_Q3_ITEM_HOLDABLE ? QA_ITEM_USE : 0};
}
static size_t equipment_count(void *opaque) {
    q3_inventory_owner *owner = opaque;
    size_t count, result = 0;
    const qa_q3_item *items = qa_q3_items(owner->game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (equipment_item(&items[i]))
            ++result;
    return result;
}
static const qa_q3_item *equipment_at(q3_inventory_owner *owner, size_t ordinal, uint32_t *index) {
    size_t count;
    const qa_q3_item *items = qa_q3_items(owner->game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (equipment_item(&items[i])) {
            if (!ordinal) {
                *index = (uint32_t)i;
                return &items[i];
            }
            --ordinal;
        }
    return NULL;
}
static bool equipment_read(void *opaque, size_t ordinal, qa_inventory_entry *out, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    uint32_t index;
    const qa_q3_item *item = equipment_at(owner, ordinal, &index);
    q3_actor *actor = q3_actor_get(owner->game, owner->actor);
    if (!item || !actor || actor->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "missing Q3 equipment inventory owner");
    int32_t selected = item->kind == QA_Q3_ITEM_HOLDABLE ? (int32_t)actor->state.player.holdable
                                                       : (int32_t)actor->state.player.persistent;
    *out = (qa_inventory_entry){.item = owner->game->item_ids[index], .capacity = 1,
                                .count = selected == item->tag ? 1 : 0,
                                .policy = QA_COUNT_SOURCE_INT32};
    return true;
}
static bool equipment_write(void *opaque, const qa_inventory_entry *entry, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    q3_actor *actor = q3_actor_get(owner->game, owner->actor);
    if (!actor || actor->kind != Q3_ACTOR_PLAYER || entry->count < 0 || entry->count > 1 ||
        entry->count != trunc(entry->count))
        return q3_fail(error, "invalid Q3 holdable inventory mutation");
    size_t count;
    const qa_q3_item *items = qa_q3_items(owner->game->options.product, &count);
    for (size_t index = 1; index < count; ++index) {
        const qa_q3_item *item = items + index;
        if (equipment_item(item) && owner->game->item_ids[index] == entry->item) {
            qa_q3_player_state *player = &actor->state.player;
            if (item->kind == QA_Q3_ITEM_PERSISTENT)
                return entry->count == ((int32_t)player->persistent == item->tag ? 1 : 0) ||
                    q3_fail(error, "Q3 persistent item changes require their actual pickup owner");
            if (entry->count != 0 && player->holdable != QA_Q3_H_NONE && (int32_t)player->holdable != item->tag)
                return q3_fail(error, "Q3 already holds another holdable");
            if (entry->count != 0)
                player->holdable = (qa_q3_holdable)item->tag;
            else if ((int32_t)player->holdable == item->tag)
                player->holdable = QA_Q3_H_NONE;
            if (player->holdable == QA_Q3_H_KAMIKAZE)
                player->flags |= 0x200u;
            else
                player->flags &= ~0x200u;
            return true;
        }
    }
    return q3_fail(error, "unknown Q3 holdable inventory item");
}
static bool inventory_admit(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || game->source_restored)
        return q3_fail(error, "Q3 inventory admission needs an actual player owner");
    qa_inventory *inventory = game->options.services.inventory;
    if (!qa_inventory_has(inventory, actor) &&
        !qa_inventory_create_actor(inventory, actor, NULL, 0, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    q3_inventory_owner *owner = &game->inventory_owners[actor.slot];
    if (!owner->actor.registry)
        *owner = (q3_inventory_owner){.game = game, .actor = actor};
    if (!qa_actor_id_equal(owner->actor, actor))
        return q3_fail(error, "Q3 inventory admission generation changed");
    uint32_t selections = entry->state.player.selections;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if ((selections & QA_Q3_ARSENAL) && !qa_inventory_lease_current(inventory, owner->weapons)) {
        qa_inventory_entry entries[2 * (QA_Q3_WEAPON_COUNT - 1)];
        size_t entry_count = q3_inventory_arsenal_entries(game, false, entries);
        qa_inventory_admission *admission = NULL;
        if (!qa_inventory_prepare_entries(inventory, actor, entries, entry_count,
                                            &admission, error)) return false;
        if (!qa_inventory_admission_commit(admission, error)) {
            qa_inventory_admission_abort(admission);
            return false;
        }
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        memcpy(entry->state.player.ammo_regeneration_items, game->ammo_items,
            sizeof(entry->state.player.ammo_regeneration_items));
        qa_item_definition definitions[QA_Q3_WEAPON_COUNT];
        size_t used = 0;
        for (size_t i = 1; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_WEAPON)
                definitions[used++] = item_definition(game, items + i, i);
        qa_inventory_lease lease = {0};
        if (!qa_inventory_bind_definitions(inventory, actor, game->options.owner,
                                            definitions, used, invoke, owner, &lease, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        owner = &game->inventory_owners[actor.slot];
        if (!qa_actor_id_equal(owner->actor, actor))
            return q3_fail(error, "Q3 weapon admission owner changed during publication");
        owner->weapons = lease;
    }
    if ((selections & QA_Q3_EQUIPMENT) && !qa_inventory_lease_current(inventory, owner->holdables)) {
        qa_item_admission definitions[9];
        size_t used = 0;
        for (size_t i = 1; i < count; ++i)
            if (equipment_item(&items[i])) {
                qa_inventory_entry previous;
                qa_error observed = {0};
                bool found = qa_inventory_entry_read(inventory, actor, game->item_ids[i],
                                                       &previous, &observed);
                if (!found && observed.code != QA_ERROR_NOT_FOUND) {
                    if (error) *error = observed;
                    return false;
                }
                entry = q3_actor_get(game, actor);
                if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
                owner = &game->inventory_owners[actor.slot];
                if (!qa_actor_id_equal(owner->actor, actor))
                    return q3_fail(error, "Q3 holdable admission owner changed during observation");
                definitions[used++] = (qa_item_admission){.replace_primary = found,
                    .definition = item_definition(game, items + i, i)};
            }
        qa_inventory_items group = {.owner = game->options.owner, .items = definitions,
            .count = used, .state = {.context = owner, .count = equipment_count,
                                    .at = equipment_read, .write = equipment_write},
            .action_context = owner, .invoke = invoke};
        qa_inventory_lease lease = {0};
        if (!qa_inventory_bind_items(inventory, actor, &group, &lease, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        owner = &game->inventory_owners[actor.slot];
        if (!qa_actor_id_equal(owner->actor, actor))
            return q3_fail(error, "Q3 holdable admission owner changed during publication");
        owner->holdables = lease;
    }
    owner->selections = selections;
    return true;
}
bool qa_q3_inventory_admit(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 inventory admission boundary");
    ++game->observation_depth;
    bool result = inventory_admit(game, actor, error);
    --game->observation_depth;
    return result;
}
bool qa_q3_inventory_equipment_current(const qa_q3_game *game, qa_actor_id actor,
    qa_actor_owner expected_owner) {
    const q3_actor *entry = game ? q3_actor_const(game, actor) : NULL;
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || game->options.owner != expected_owner ||
        !(entry->state.player.selections & QA_Q3_EQUIPMENT)) return false;
    const q3_inventory_owner *owner = &game->inventory_owners[actor.slot];
    return owner->game == game && qa_actor_id_equal(owner->actor, actor) &&
        (owner->selections & QA_Q3_EQUIPMENT) &&
        qa_inventory_lease_current(game->options.services.inventory, owner->holdables);
}
static bool holdable_name(const char *actual, const char *requested) {
    if (!actual) return false;
    while (*actual && *requested) {
        if (tolower((unsigned char)*actual) != tolower((unsigned char)*requested)) return false;
        ++actual; ++requested;
    }
    return !*actual && !*requested;
}
bool qa_q3_selected_holdable_give(qa_q3_game *game, qa_actor_id actor,
    const char *name, bool *handled, qa_error *error) {
    if (!game || !name || !handled || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid selected Q3 holdable grant boundary");
    *handled = false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "selected holdable grant lost its actual Q3 player");
    if (!(entry->state.player.selections & QA_Q3_EQUIPMENT)) return true;
    if (!qa_q3_inventory_equipment_current(game, actor, game->options.owner))
        return q3_fail(error, "selected holdable grant lost its actual equipment admission");
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    for (size_t i = 1; i < count; ++i) {
        const qa_q3_item *item = items + i;
        if (item->kind != QA_Q3_ITEM_HOLDABLE ||
            (!holdable_name(item->classname, name) && !holdable_name(item->name, name))) continue;
        /* Pickup_Holdable replaces the retained item; its direct give path
         * bypasses Touch_Item's already-held gate and pickup side effects. */
        qa_q3_holdable before = entry->state.player.holdable;
        qa_q3_holdable after = (qa_q3_holdable)item->tag;
        ++game->observation_depth;
        entry->state.player.holdable = after;
        if (after == QA_Q3_H_KAMIKAZE) entry->state.player.flags |= 0x200u;
        bool ok = q3_inventory_holdable_changed(game, actor, before, after, error);
        --game->observation_depth;
        if (ok) *handled = true;
        return ok;
    }
    return true;
}
bool qa_q3_inventory_rebind(qa_q3_game *game, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth || !qa_session_safe(game->options.services.session))
        return q3_fail(error, "Q3 inventory rebind requires a safe source boundary");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (q3_actor_at(game, i)->kind == Q3_ACTOR_PLAYER &&
            q3_actor_get(game, q3_actor_at(game, i)->actor) &&
            !qa_q3_inventory_admit(game, q3_actor_at(game, i)->actor, error))
            return false;
    return true;
}

static bool saved_definition(const qa_item_definition *expected,
                              const qa_item_definition *saved) {
    return expected->item == saved->item && expected->ammo == saved->ammo &&
        expected->owner == saved->owner && expected->weapon == saved->weapon &&
        expected->actions == saved->actions && expected->label && saved->label &&
        !strcmp(expected->label, saved->label);
}
bool qa_q3_game_inventory_group(qa_q3_game *game, qa_actor_id actor, uint64_t serial,
    const qa_inventory_source_group *saved, qa_inventory_items *out, qa_error *error) {
    q3_actor *entry = game ? q3_actor_get(game, actor) : NULL;
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !saved || !out || !serial ||
        saved->owner != game->options.owner || (saved->count && !saved->items) ||
        game->observation_depth || game->player_binding_tokens[actor.slot])
        return q3_fail(error, "invalid Q3 saved inventory group");
    uint32_t selection = saved->definitions_only ? QA_Q3_ARSENAL : QA_Q3_EQUIPMENT;
    if (!(entry->state.player.selections & selection))
        return q3_fail(error, "Q3 saved inventory role is not selected");
    q3_inventory_owner *owner = &game->inventory_owners[actor.slot];
    if (!qa_actor_id_equal(owner->actor, actor))
        return q3_fail(error, "Q3 saved inventory owner generation changed");
    size_t count, used = 0;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    for (size_t i = 1; i < count; ++i) {
        bool weapon = items[i].kind == QA_Q3_ITEM_WEAPON;
        if (saved->definitions_only ? !weapon : !equipment_item(&items[i]))
            continue;
        if (used == QA_Q3_WEAPON_COUNT || used >= saved->count)
            return q3_fail(error, "Q3 saved inventory definition count differs");
        qa_item_admission definition = {.definition = item_definition(game, items + i, i),
            .replace_primary = saved->items[used].replace_primary};
        if ((saved->definitions_only && definition.replace_primary) ||
            !saved_definition(&definition.definition, &saved->items[used].definition))
            return q3_fail(error, "Q3 saved inventory declarations differ from source");
        ++used;
    }
    if (used != saved->count || (!saved->definitions_only && used > 9))
        return q3_fail(error, "Q3 saved inventory definition count differs");
    qa_inventory_lease prior = saved->definitions_only ? owner->weapons : owner->holdables;
    if (!qa_actor_id_equal(prior.actor, actor) || prior.serial != serial)
        return q3_fail(error, "Q3 saved inventory role has no captured private lease");
    *out = (qa_inventory_items){.owner = game->options.owner, .items = saved->items,
        .count = used, .action_context = owner, .invoke = invoke};
    if (!saved->definitions_only)
        out->state = (qa_inventory_binding){.context = owner, .count = equipment_count,
                                            .at = equipment_read, .write = equipment_write};
    return true;
}
