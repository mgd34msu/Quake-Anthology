#include "internal.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q2_bots.h"
#include <float.h>

typedef struct item_inspection {
    qa_q2_game *game;
    qa_actor_id pickup, recipient;
    qa_pickup_offer *offer;
    float *utility;
    bool *available;
} item_inspection;

static bool inventory(qa_q2_game *g, qa_actor_id actor,
                       const qa_q2_item_definition *definition,
                       qa_inventory_entry *out, qa_error *error)
{
    qa_error absent = {0};
    if (qa_inventory_entry_read(g->services.inventory, actor, definition->item, out, &absent))
        return true;
    if (absent.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = absent;
        return false;
    }
    *out = (qa_inventory_entry){.item = definition->item, .capacity = definition->capacity};
    return true;
}

static bool supply_preview(qa_supply *supply, qa_actor_id recipient,
                            const qa_supply_offer *offer, float *utility,
                            bool *available, qa_error *error)
{
    qa_supply_preview_result result = {0};
    if (!qa_supply_preview(supply, recipient, offer, false, &result, error)) return false;
    *available = result.accepted;
    double value = 0;
    for (size_t i = 0; i < result.weapon_count; ++i)
        value += result.weapons[i].before == 0 && result.weapons[i].given > 0 ? 10 : 0;
    for (size_t i = 0; i < result.ammo_count; ++i)
        value += fmax(0, result.ammo[i].given);
    *utility = (float)fmin(value, FLT_MAX);
    qa_supply_preview_free(&result);
    return true;
}

typedef struct bot_supply_inspection {
    qa_q2_game *game;
    qa_actor_id recipient;
    qa_supply_preview_result *preview;
    bool *eligible,*found;
} bot_supply_inspection;

static bool bot_supply_actor(void *opaque,qa_actor_id pickup,qa_error *error) {
    bot_supply_inspection *call=opaque;
    qa_q2_game *g=call->game;
    q2_actor *actor=q2_actor_get(g,pickup,false,NULL);
    if(!actor || !actor->item || !actor->item->definition || actor->item->companion) return true;
    q2_item_state *item=actor->item;
    const qa_q2_item_definition *d=item->definition;
    bool weapon=d->kind==QA_Q2_ITEM_WEAPON;
    if(!weapon && d->kind!=QA_Q2_ITEM_AMMO) return true;
    *call->found=true;
    if(!q2_item_eligible(g,actor,call->recipient,call->eligible,error)) return false;
    qa_supply *selected;
    if(!q2_item_supply(g,call->recipient,&selected,error)) return false;
    if(!q2_actor_live(g,pickup) || !q2_actor_live(g,call->recipient)) goto stale;
    if(weapon) {
        bool owned;
        bool mapped=selected && qa_supply_maps(selected,d->item,true);
        if(mapped) {
            if(!qa_supply_owns(selected,call->recipient,d->item,&owned,error)) return false;
        } else {
            qa_inventory_entry entry;
            if(!inventory(g,call->recipient,d,&entry,error)) return false;
            owned=entry.count>0;
        }
        bool stays=g->options.cooperative?!g->item_runtime->options.instanced_coop:
            g->options.deathmatch && (g->options.deathmatch_flags&4);
        if(stays && owned && !(item->spawn.spawnflags&0x30000)) *call->eligible=false;
        if(mapped && d->weapon==QA_Q2_BLASTER) *call->eligible=false;
    }
    const qa_q2_item_definition *ammo=weapon && d->ammo?q2_item_by_id(g,d->ammo):d;
    bool with_ammo=!weapon || (ammo && d->ammo && !(item->spawn.spawnflags&0x10000));
    int quantity=weapon?ammo && d->ammo?(g->options.deathmatch_flags&8192)?1000:ammo->quantity:0:
        d->weapon!=QA_Q2_WEAPON_NONE && d->infinite_quantity && (g->options.deathmatch_flags&8192)?
        1000:item->spawn.count?item->spawn.count:d->quantity;
    qa_pickup_grant grant={.item=ammo?ammo->item:0,.amount=quantity};
    if(selected) {
        if(weapon && d->weapon==QA_Q2_BLASTER) return true;
        qa_supply_offer offer={.kind=weapon?QA_SUPPLY_WEAPON:
            d->weapon==QA_Q2_WEAPON_NONE?QA_SUPPLY_AMMO:QA_SUPPLY_AMMO_WEAPON,
            .item=d->item,.weapon=d->item,.ammo=with_ammo?&grant:NULL,.ammo_count=with_ammo?1:0};
        if(!qa_supply_preview(selected,call->recipient,&offer,false,call->preview,error)) return false;
    } else {
        qa_inventory_entry entries[2];size_t count=0;
        if(weapon) {
            if(!inventory(g,call->recipient,d,entries+count,error)) return false;
            ++count;
        }
        if(with_ammo) {
            if(!inventory(g,call->recipient,ammo,entries+count,error)) return false;
            ++count;
        }
        qa_pickup_grant owned={.item=d->item,.amount=1};
        qa_pickup_grant_plan plan={.weapon_offer=weapon,.accept_nonzero=true,
            .shared_weapon_ammo=!weapon && d->weapon!=QA_Q2_WEAPON_NONE,
            .weapons=&owned,.weapon_count=weapon || d->weapon!=QA_Q2_WEAPON_NONE?1:0,
            .ammo=with_ammo?&grant:NULL,.ammo_count=with_ammo?1:0};
        if(!qa_pickup_preview_grants(entries,count,&plan,call->preview,error)) return false;
    }
    if(q2_actor_live(g,pickup) && q2_actor_live(g,call->recipient)) return true;
stale:
    qa_supply_preview_free(call->preview);*call->eligible=false;*call->found=false;return true;
}

bool qa_q2_bot_supply_preview(qa_q2_game *g,qa_actor_id pickup,qa_actor_id recipient,
    qa_supply_preview_result *out,bool *eligible,bool *found,qa_error *error) {
    if(!g || !out || !eligible || !found || !g->item_runtime ||
       g->continuation_pending || g->continuation_failed) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 supply preview requires its live source owner and outputs");
        return false;
    }
    *out=(qa_supply_preview_result){0};*eligible=false;*found=false;
    if(!q2_actor_live(g,pickup) || !q2_actor_live(g,recipient)) return true;
    bot_supply_inspection call={g,recipient,out,eligible,found};
    bool ok=qa_q2_run_actor(g,pickup,bot_supply_actor,&call,error);
    if(!ok) qa_supply_preview_free(out);
    return ok;
}

static bool native_preview(item_inspection *call, q2_actor *actor, qa_error *error)
{
    qa_q2_game *g = call->game;
    q2_item_state *item = actor->item;
    const qa_q2_item_definition *d = item->definition;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, call->recipient, &combat, error)) return false;
    if (!q2_actor_live(g, call->recipient) || !q2_actor_live(g, call->pickup)) return true;
    q2_actor *player = q2_actor_get(g, call->recipient, false, NULL);
    const q2_power_state *powers = player ? player->powers : NULL;
    float maximum = powers ? powers->maximum_health : 100;
    bool instanced = g->options.cooperative && g->item_runtime->options.instanced_coop;
    switch (d->kind) {
    case QA_Q2_ITEM_HEALTH:
        *call->available = d->ignore_maximum || combat.health < maximum;
        *call->utility = fmaxf(0, (float)(item->spawn.count ? item->spawn.count : d->quantity));
        if (!d->ignore_maximum) *call->utility = fminf(*call->utility, fmaxf(0, maximum - combat.health));
        return true;
    case QA_Q2_ITEM_FOOD:
        *call->available = true; *call->utility = fmaxf(0, (float)item->spawn.count); return true;
    case QA_Q2_ITEM_ARMOR:
    case QA_Q2_ITEM_SHARD: {
        qa_regular_armor next;
        *call->available = q2_item_armor_result(g, d, &combat.armor.regular, &next);
        if (*call->available)
            *call->utility = fmaxf(1, (float)next.points - (float)combat.armor.regular.points);
        return true;
    }
    case QA_Q2_ITEM_MAX_HEALTH:
    case QA_Q2_ITEM_PACK:
        *call->available = true; *call->utility = fmaxf(1, (float)d->quantity); return true;
    default: break;
    }
    qa_inventory_entry entry;
    if (!inventory(g, call->recipient, d, &entry, error)) return false;
    switch (d->kind) {
    case QA_Q2_ITEM_KEY:
        *call->available = !g->options.cooperative || entry.count == 0;
        if (g->options.cooperative &&
            ((d->rule_flags & QA_Q2_ITEM_POWER_CUBE) || (d->rule_flags & QA_Q2_ITEM_EXPLOSIVE_CHARGES)))
            *call->available = !powers || !(powers->power_cubes & ((item->spawn.spawnflags & 0xff00u) >> 8));
        *call->utility = *call->available ? 1 : 0; return true;
    case QA_Q2_ITEM_AMMO:
    case QA_Q2_ITEM_WEAPON: {
        bool weapon = d->kind == QA_Q2_ITEM_WEAPON;
        qa_supply *supply;
        if (!q2_item_supply(g, call->recipient, &supply, error)) return false;
        if (!q2_actor_live(g, call->recipient) || !q2_actor_live(g, call->pickup)) return true;
        bool mapped = supply && qa_supply_maps(supply, d->item, weapon);
        bool owned = entry.count > 0;
        if (weapon && mapped && !qa_supply_owns(supply, call->recipient, d->item, &owned, error)) return false;
        bool stays = g->options.cooperative ? !instanced
                     : g->options.deathmatch && (g->options.deathmatch_flags & 4);
        if (weapon && ((mapped && d->weapon == QA_Q2_BLASTER) ||
            (stays && owned && !call->offer->dropped))) return true;
        const qa_q2_item_definition *ammo = weapon && d->ammo ? q2_item_by_id(g, d->ammo) : d;
        bool with_ammo = !weapon || (ammo && !(item->spawn.spawnflags & 0x10000));
        int quantity = weapon ? ammo ? (g->options.deathmatch_flags & 8192) ? 1000 : ammo->quantity : 0
            : d->weapon != QA_Q2_WEAPON_NONE && d->infinite_quantity && (g->options.deathmatch_flags & 8192)
                ? 1000 : item->spawn.count ? item->spawn.count : d->quantity;
        qa_pickup_grant grant = {.item = ammo ? ammo->item : 0, .amount = quantity};
        if (mapped) {
            qa_supply_offer offer = {.kind = weapon ? QA_SUPPLY_WEAPON
                : d->weapon == QA_Q2_WEAPON_NONE ? QA_SUPPLY_AMMO : QA_SUPPLY_AMMO_WEAPON,
                .item = d->item, .weapon = d->item,
                .ammo = with_ammo ? &grant : NULL, .ammo_count = with_ammo ? 1 : 0};
            return supply_preview(supply, call->recipient, &offer, call->utility, call->available, error);
        }
        *call->available = weapon || (quantity > 0 ? entry.count < entry.capacity
                                                 : quantity < 0 && entry.count > 0);
        *call->utility = weapon ? owned ? 1 : 10
            : fmaxf(0, (float)fmin(quantity, entry.capacity - entry.count));
        return true;
    }
    case QA_Q2_ITEM_POWER:
    case QA_Q2_ITEM_SPHERE:
    case QA_Q2_ITEM_COMPASS:
        if (d->console_give == QA_Q2_GIVE_INVENTORY_ONLY ||
            (g->options.edition == QA_Q2_RERELEASE && g->options.skill == 0 && entry.count >= 3) ||
            (g->options.skill == 1 && entry.count >= 2) || (g->options.skill >= 2 && entry.count >= 1) ||
            (g->options.cooperative && !instanced && d->coop_stay && entry.count > 0) ||
            (d->kind == QA_Q2_ITEM_SPHERE && powers && q2_actor_live(g, powers->sphere)))
            return true;
        break;
    case QA_Q2_ITEM_POWER_ARMOR:
        *call->available = true; *call->utility = entry.count > 0 ? 1 : 10; return true;
    case QA_Q2_ITEM_DECOY:
        if (!g->options.deathmatch) return true;
        break;
    default: break;
    }
    *call->available = entry.count < entry.capacity;
    *call->utility = *call->available ? 1 : 0;
    return true;
}

static bool inspect_actor(void *context, qa_actor_id id, qa_error *error)
{
    item_inspection *call = context;
    qa_q2_game *g = call->game;
    q2_actor *actor = q2_actor_get(g, id, false, NULL);
    if (!actor || !actor->item || !actor->item->definition || actor->item->companion) return true;
    *call->offer = q2_item_offer(g, actor, call->recipient);
    if (!q2_item_eligible(g, actor, call->recipient, call->available, error)) return false;
    if (!*call->available) return true;
    *call->available = false;
    bool handled;
    bool ok = qa_pickups_preview(g->services.pickups, call->offer, call->utility,
                                  call->available, &handled, error);
    if (ok && !handled && q2_actor_live(g, id) && q2_actor_live(g, call->recipient))
        ok = native_preview(call, actor, error);
    if (!q2_actor_live(g, id) || !q2_actor_live(g, call->recipient)) {
        *call->utility = 0; *call->available = false;
    }
    return ok;
}

static bool inspect(void *context, qa_actor_id pickup, qa_actor_id recipient,
                     qa_pickup_offer *offer, float *utility, bool *available, qa_error *error)
{
    qa_q2_game *g = context;
    *utility = 0; *available = false;
    *offer = (qa_pickup_offer){.pickup = pickup, .recipient = recipient, .source = g->options.owner};
    if (!q2_actor_live(g, pickup) || !q2_actor_live(g, recipient)) return true;
    item_inspection call = {.game = g, .pickup = pickup, .recipient = recipient,
        .offer = offer, .utility = utility, .available = available};
    return qa_q2_run_actor(g, pickup, inspect_actor, &call, error);
}

bool q2_item_observe(qa_q2_game *g, q2_actor *actor, qa_error *error)
{
    if (!g->services.pickups || !actor->item || !actor->item->definition || actor->item->companion)
        return true;
    qa_pickup_observer observer = {.context = g, .inspect = inspect};
    if (!qa_pickups_observe(g->services.pickups, actor->id, g->options.owner,
                            &observer, &actor->item->observation, error)) return false;
    actor->item->observations = g->services.pickups;
    return true;
}
bool qa_q2_game_pickup_observer(qa_q2_game *g, qa_actor_id id, qa_actor_owner owner,
                                 uint64_t saved_serial, qa_pickup_observer *out, qa_error *error)
{
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!out || !a || !a->item || !a->item->definition || a->item->companion ||
        owner != g->options.owner || !saved_serial ||
        !qa_actor_id_equal(a->item->observation.actor, id) ||
        a->item->observation.serial != saved_serial) {
        qa_error_set(error, QA_ERROR_FORMAT, id.slot, "Q2 pickup observer has no native offer owner");
        return false;
    }
    a->item->observation = (qa_pickup_lease){.actor = id, .serial = saved_serial};
    *out = (qa_pickup_observer){.context = g, .inspect = inspect};
    return true;
}

bool qa_q2_pickups_rebind(qa_q2_game *g, qa_error *error)
{
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 pickup rebinding needs a provider");
        return false;
    }
    if (!q2_checkpoint_idle(g, error)) return false;
    for (uint32_t i = 0; i < g->capacity; ++i)
        if (g->actors[i] && q2_actor_live(g, g->actors[i]->id) && !q2_item_observe(g, g->actors[i], error))
            return false;
    return true;
}
