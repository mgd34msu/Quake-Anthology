#include "internal.h"

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
            (items[i].tag == before || items[i].tag == after)) {
            qa_inventory_entry old = {.item = game->item_ids[i], .capacity = 1,
                .count = items[i].tag == before ? 1 : 0, .policy = QA_COUNT_SOURCE_INT32};
            qa_inventory_entry next = old;
            next.count = items[i].tag == after ? 1 : 0;
            changes[used++] = (qa_inventory_change){.actor = actor, .had_before = true,
                                                   .before = old, .after = next};
        }
    return qa_inventory_source_stored(game->options.services.inventory, owner->holdables,
                                       changes, used, error);
}
static size_t holdable_count(void *opaque) {
    q3_inventory_owner *owner = opaque;
    size_t count, result = 0;
    const qa_q3_item *items = qa_q3_items(owner->game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == QA_Q3_ITEM_HOLDABLE)
            ++result;
    return result;
}
static const qa_q3_item *holdable_at(q3_inventory_owner *owner, size_t ordinal, uint32_t *index) {
    size_t count;
    const qa_q3_item *items = qa_q3_items(owner->game->options.product, &count);
    for (size_t i = 1; i < count; ++i)
        if (items[i].kind == QA_Q3_ITEM_HOLDABLE) {
            if (!ordinal) {
                *index = (uint32_t)i;
                return &items[i];
            }
            --ordinal;
        }
    return NULL;
}
static bool holdable_read(void *opaque, size_t ordinal, qa_inventory_entry *out, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    uint32_t index;
    const qa_q3_item *item = holdable_at(owner, ordinal, &index);
    q3_actor *actor = q3_actor_get(owner->game, owner->actor);
    if (!item || !actor || actor->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "missing Q3 holdable inventory owner");
    *out = (qa_inventory_entry){.item = owner->game->item_ids[index], .capacity = 1,
                                .count = actor->state.player.holdable == item->tag ? 1 : 0,
                                .policy = QA_COUNT_SOURCE_INT32};
    return true;
}
static bool holdable_write(void *opaque, const qa_inventory_entry *entry, qa_error *error) {
    q3_inventory_owner *owner = opaque;
    q3_actor *actor = q3_actor_get(owner->game, owner->actor);
    if (!actor || actor->kind != Q3_ACTOR_PLAYER || entry->count < 0 || entry->count > 1 ||
        entry->count != trunc(entry->count))
        return q3_fail(error, "invalid Q3 holdable inventory mutation");
    for (size_t ordinal = 0; ordinal < holdable_count(owner); ++ordinal) {
        uint32_t index;
        const qa_q3_item *item = holdable_at(owner, ordinal, &index);
        if (item && owner->game->item_ids[index] == entry->item) {
            qa_q3_player_state *player = &actor->state.player;
            if (entry->count && player->holdable != QA_Q3_H_NONE && player->holdable != item->tag)
                return q3_fail(error, "Q3 already holds another holdable");
            if (entry->count)
                player->holdable = (qa_q3_holdable)item->tag;
            else if (player->holdable == item->tag)
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
bool qa_q3_inventory_admit(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 inventory admission needs an actual player owner");
    qa_inventory *inventory = game->options.services.inventory;
    if (!qa_inventory_has(inventory, actor) &&
        !qa_inventory_create_actor(inventory, actor, NULL, 0, error))
        return false;
    q3_inventory_owner *owner = &game->inventory_owners[actor.slot];
    if (!owner->actor.registry)
        *owner = (q3_inventory_owner){.game = game, .actor = actor};
    if (!qa_actor_id_equal(owner->actor, actor))
        return q3_fail(error, "Q3 inventory admission generation changed");
    uint32_t selections = entry->state.player.selections;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if ((selections & QA_Q3_ARSENAL) && !qa_inventory_lease_current(inventory, owner->weapons)) {
        qa_item_definition definitions[QA_Q3_WEAPON_COUNT];
        size_t used = 0;
        for (size_t i = 1; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_WEAPON)
                definitions[used++] = (qa_item_definition){.item = game->item_ids[i],
                    .ammo = game->ammo_items[items[i].tag], .owner = game->options.owner,
                    .label = items[i].name, .weapon = true, .actions = QA_ITEM_USE};
        if (!qa_inventory_bind_definitions(inventory, actor, game->options.owner,
                                            definitions, used, invoke, owner, &owner->weapons, error))
            return false;
    }
    if ((selections & QA_Q3_EQUIPMENT) && !qa_inventory_lease_current(inventory, owner->holdables)) {
        qa_item_admission definitions[5];
        size_t used = 0;
        for (size_t i = 1; i < count; ++i)
            if (items[i].kind == QA_Q3_ITEM_HOLDABLE) {
                qa_inventory_entry previous;
                qa_error observed = {0};
                bool found = qa_inventory_entry_read(inventory, actor, game->item_ids[i],
                                                       &previous, &observed);
                if (!found && observed.code != QA_ERROR_NOT_FOUND) {
                    if (error) *error = observed;
                    return false;
                }
                definitions[used++] = (qa_item_admission){.replace_primary = found, .definition = {
                    .item = game->item_ids[i], .owner = game->options.owner,
                    .label = items[i].name, .actions = QA_ITEM_USE}};
            }
        qa_inventory_items group = {.owner = game->options.owner, .items = definitions,
            .count = used, .state = {.context = owner, .count = holdable_count,
                                    .at = holdable_read, .write = holdable_write},
            .action_context = owner, .invoke = invoke};
        if (!qa_inventory_bind_items(inventory, actor, &group, &owner->holdables, error))
            return false;
    }
    owner->selections = selections;
    return true;
}
bool qa_q3_inventory_rebind(qa_q3_game *game, qa_error *error) {
    if (!game || game->observation_depth || !qa_session_safe(game->options.services.session))
        return q3_fail(error, "Q3 inventory rebind requires a safe source boundary");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->actors[i].kind == Q3_ACTOR_PLAYER &&
            q3_actor_get(game, game->actors[i].actor) &&
            !qa_q3_inventory_admit(game, game->actors[i].actor, error))
            return false;
    return true;
}
