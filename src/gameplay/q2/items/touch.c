#include "internal.h"

typedef struct item_touch {
    qa_q2_game *game;
    q2_actor *actor;
    qa_actor_id player;
    bool original;
} item_touch;
static bool live(const item_touch *call) {
    return q2_actor_live(call->game, call->actor->id) && q2_actor_live(call->game, call->player);
}
static bool instanced(const qa_q2_game *g) {
    return g->options.cooperative && g->item_runtime->options.instanced_coop;
}
static bool slot(qa_q2_game *g, qa_actor_id player, uint32_t *out, qa_error *e) {
    qa_q2_item_options *options = &g->item_runtime->options;
    if (options->player_slot && options->player_slot(options->context, player, out))
        return true;
    if (q2_client_slot(g, player, out))
        return true;
    qa_error_set(e, QA_ERROR_ARGUMENT, player.slot,
                 "Instanced Q2 pickup requires a stable player slot");
    return false;
}
static bool was_picked(const q2_item_state *item, uint32_t player) {
    for (size_t i = 0; i < item->picked_count; ++i)
        if (item->picked_slots[i] == player)
            return true;
    return false;
}
bool qa_q2_item_visible_to(qa_q2_game *g, qa_actor_id pickup, qa_actor_id player,
    bool *out, qa_error *e) {
    if (!g || !out || !q2_actor_live(g, pickup)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 pickup visibility observation");
        return false;
    }
    const q2_item_state *item = g->actors[pickup.slot]->item;
    *out = !item || item->visible;
    if (!*out || !item || !instanced(g)) return true;
    uint32_t index;
    if (!slot(g, player, &index, e)) return false;
    *out = q2_actor_live(g, pickup) && q2_actor_live(g, player) && !was_picked(item, index);
    return true;
}
static bool mark_picked(q2_item_state *item, uint32_t player, qa_error *e) {
    if (was_picked(item, player))
        return true;
    if (item->picked_count == item->picked_capacity) {
        size_t capacity = item->picked_capacity ? item->picked_capacity * 2 : 4;
        if (capacity < item->picked_capacity || capacity > SIZE_MAX / sizeof(*item->picked_slots)) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Q2 instanced pickup capacity overflow");
            return false;
        }
        uint32_t *slots = realloc(item->picked_slots, capacity * sizeof(*slots));
        if (!slots) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 pickup seat ownership");
            return false;
        }
        item->picked_slots = slots;
        item->picked_capacity = capacity;
    }
    item->picked_slots[item->picked_count++] = player;
    return true;
}
bool q2_item_eligible(qa_q2_game *g, q2_actor *actor, qa_actor_id recipient,
                       bool *allowed, qa_error *e) {
    item_touch value = {.game = g, .actor = actor, .player = recipient};
    const item_touch *call = &value;
    q2_item_state *item = actor->item;
    *allowed = false;
    if (!live(call) || !item->visible || !item->touchable ||
        (item->temporary && qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(g->services.session), item->owner), call->player)))
        return true;
    qa_builtin_actor_traits traits;
    if (!g->services.actor_traits ||
        !g->services.actor_traits(g->services.context, call->player, &traits) || !traits.player ||
        traits.spectator || !live(call))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, call->player, &combat, e))
        return false;
    if (!live(call) || combat.health < 1)
        return true;
    return qa_q2_item_visible_to(g, actor->id, recipient, allowed, e);
}
static bool eligible(void *context, const qa_pickup_offer *offer, bool *allowed, qa_error *e) {
    (void)offer;
    item_touch *call = context;
    return q2_item_eligible(call->game, call->actor, call->player, allowed, e);
}
static bool original(void *context, const qa_pickup_offer *offer, bool *accepted, qa_error *e) {
    (void)offer;
    item_touch *call = context;
    call->original = true;
    call->actor->item->retained = false;
    return q2_item_grant(call->game, call->actor, call->player, accepted, e);
}
static bool complete(void *context, const qa_pickup_offer *offer, bool accepted, qa_error *e) {
    (void)offer;
    item_touch *call = context;
    qa_q2_game *g = call->game;
    q2_actor *a = call->actor;
    q2_item_state *item = a->item;
    const qa_q2_item_definition *d = item->definition;
    if (!live(call))
        return true;
    if (accepted) {
        if (!call->original)
            item->retained = false;
        if (!q2_item_finish(g, a, call->player, e))
            return false;
        if (!live(call))
            return true;
        qa_string_id icon = d->icon_id, name = d->name_id;
        q2_actor *recipient = q2_actor_get(g, call->player, false, NULL);
        if (recipient && recipient->client) {
            recipient->pickup_icon = icon;
            recipient->pickup_text = name;
            recipient->pickup_until_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
        }
        if (!qa_builtin_emit(&g->services,
                             &(qa_builtin_event){.kind = QA_BUILTIN_ITEM,
                                                 .family = QA_GAME_Q2,
                                                 .provider = g->options.owner,
                                                 .actor = call->player,
                                                 .other = a->id,
                                                 .resource = icon,
                                                 .text = name,
                                                 .item = d->item,
                                                 .code = 0,
                                                 .time_ns = g->now_ns},
                             e))
            return false;
        if (!live(call))
            return true;
        if (!q2_item_sound(g, call->player, d->sound_id, e))
            return false;
        if (!live(call))
            return true;
        if (!q2_client_item_received(g, call->player, d, e))
            return false;
        if (!live(call))
            return true;
        recipient = q2_actor_get(g, call->player, false, NULL);
        if (recipient && recipient->client && recipient->client->info.selected_item == d->item) {
            recipient->selected_item_name = 0;
            recipient->selected_item_name_until_ns = 0;
        }
        if (instanced(g)) {
            uint32_t player;
            if (!slot(g, call->player, &player, e) || !mark_picked(item, player, e))
                return false;
            if (!live(call))
                return true;
            qa_q2_item_options *options = &g->item_runtime->options;
            if (!options->visibility) {
                qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                             "Instanced Q2 pickups require player visibility service");
                return false;
            }
            if (!options->visibility(options->context, call->player, a->id, false, e))
                return false;
            if (!live(call))
                return true;
            if (item->spawn.message &&
                !qa_builtin_emit(&g->services,
                                 &(qa_builtin_event){.kind = QA_BUILTIN_CENTERPRINT,
                                                     .family = QA_GAME_Q2,
                                                     .provider = g->options.owner,
                                                     .actor = call->player,
                                                     .text = item->spawn.message,
                                                     .time_ns = g->now_ns},
                                 e))
                return false;
        }
    }
    if (!live(call))
        return true;
    if (!item->targets_used) {
        /* Mark before entering the graph: recursively touching the same item
         * cannot fire an unbounded second copy of this target chain. */
        item->targets_used = true;
        qa_string_id message = item->spawn.message;
        if (instanced(g) || (g->options.edition == QA_Q2_RERELEASE && g->options.deathmatch))
            item->spawn.message = 0;
        bool okay = qa_q2_entity_use_targets(g, a->id, call->player, false, e);
        if (q2_actor_live(g, a->id))
            item->spawn.message = message;
        if (!okay)
            return false;
    }
    if (!accepted || !live(call))
        return true;
    bool dropped = (item->spawn.spawnflags & 0x30000) != 0;
    bool stays = g->options.cooperative && d->coop_stay;
    bool instance_retained = instanced(g) && !(item->spawn.spawnflags & 0x20000);
    return ((!stays || dropped) && !item->retained && !instance_retained)
               ? qa_session_release(g->services.session, a->id, e)
               : true;
}
qa_pickup_offer q2_item_offer(qa_q2_game *g, const q2_actor *a, qa_actor_id recipient) {
    const qa_q2_item_definition *d = a->item->definition;
    qa_pickup_resource resource = {0};
    if (d->kind == QA_Q2_ITEM_ARMOR || d->kind == QA_Q2_ITEM_SHARD)
        resource =
            (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION, .channel = QA_PROTECTION_REGULAR};
    else if (d->kind == QA_Q2_ITEM_POWER_ARMOR)
        resource =
            (qa_pickup_resource){.kind = QA_PICKUP_PROTECTION, .channel = QA_PROTECTION_POWERED};
    else if (d->kind == QA_Q2_ITEM_AMMO || d->kind == QA_Q2_ITEM_WEAPON ||
             d->kind == QA_Q2_ITEM_KEY)
        resource = (qa_pickup_resource){.kind = QA_PICKUP_INVENTORY, .item = d->item};
    return (qa_pickup_offer){.recipient = recipient,
                             .pickup = a->id,
                             .source = g->options.owner,
                             .item = d->item,
                             .default_resource = resource,
                             .override_count = a->item->spawn.count != 0,
                             .count = a->item->spawn.count,
                             .dropped = (a->item->spawn.spawnflags & 0x30000) != 0,
                             .time_ns = g->now_ns};
}
bool q2_item_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = contact->self.slot < g->capacity ? g->actors[contact->self.slot] : NULL;
    if (!a || !qa_actor_id_equal(a->id, contact->self) || !a->item)
        return true;
    if (a->item->companion)
        return q2_companion_touch(g, contact, e);
    if (a->item->dispatching)
        return true;
    item_touch call = {.game = g, .actor = a, .player = contact->other};
    qa_pickup_offer offer = q2_item_offer(g, a, contact->other);
    qa_pickup_continuation continuation = {
        .context = &call, .eligible = eligible, .original = original, .complete = complete};
    a->item->dispatching = true;
    qa_pickup_outcome outcome;
    bool ok = qa_pickups_touch(g->services.pickups, &offer, &continuation, &outcome, e);
    a->item->dispatching = false;
    return ok;
}
bool q2_item_console_pickup(qa_q2_game *g, qa_actor_id player,
                            const qa_q2_item_definition *definition, qa_error *e) {
    if (!g || !definition || !q2_actor_live(g, player)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 console pickup");
        return false;
    }
    qa_builtin_spawn spawn = {.owner = g->options.owner,
                              .definition = q2_item_classname(g, definition)};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    bool handled = false;
    qa_q2_item_spawn item = {.classname = definition->classname};
    bool ok = qa_q2_item_spawn_actor(g, id, &item, &handled, e);
    if (ok && handled && q2_actor_live(g, id) && q2_actor_live(g, player)) {
        q2_actor *actor = q2_actor_get(g, id, false, e);
        if (!actor || !actor->item) {
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Missing admitted Q2 console pickup state");
            ok = false;
        } else {
            actor->item->think = Q2_ITEM_IDLE;
            actor->item->due_ns = 0;
            actor->item->visible = true;
            actor->item->touchable = true;
            qa_touch_contact contact = {.self = id, .other = player};
            ok = q2_item_touch(g, &contact, e);
        }
    }
    if (q2_actor_live(g, id)) {
        qa_error ignored = {0};
        bool removed = qa_session_release(g->services.session, id, ok ? e : &ignored);
        ok = ok && removed;
    }
    return ok;
}
bool qa_q2_items_publish_visibility(qa_q2_game *g, qa_actor_id player, qa_error *e) {
    if (!g || !q2_actor_live(g, player)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item visibility recipient");
        return false;
    }
    if (!instanced(g))
        return true;
    uint32_t index;
    if (!slot(g, player, &index, e))
        return false;
    qa_q2_item_options *options = &g->item_runtime->options;
    if (!options->visibility) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                     "Instanced Q2 pickups require player visibility service");
        return false;
    }
    for (q2_actor *a = g->all_actors; a; a = a->all_next) {
        if (q2_actor_live(g, a->id) && a->item && was_picked(a->item, index) &&
            !options->visibility(options->context, player, a->id, false, e))
            return false;
        if (!q2_actor_live(g, player))
            return true;
    }
    return true;
}
