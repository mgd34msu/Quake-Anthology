#include "internal.h"

static bool widow(const struct qa_q2_monster *monster) {
    return monster && monster->definition &&
           (monster->definition->species == Q2M_WIDOW ||
            monster->definition->species == Q2M_WIDOW2);
}

bool q2_monster_timed_invulnerability(const q2_actor *actor, uint64_t now_ns) {
    return widow(actor->monster) &&
           actor->monster->widow_powers.invulnerability_until_ns > now_ns;
}

static bool shown(uint64_t until, uint64_t now) {
    if (until <= now)
        return false;
    uint64_t remaining = until - now;
    uint64_t ticks = remaining / Q2M_TENTH + (remaining % Q2M_TENTH >= Q2M_TENTH / 2);
    return ticks && (ticks > 30 || (ticks & 4));
}

void q2m_widow_clear_powerups(q2m_context *context) {
    context->monster->widow_powers = (qa_builtin_powerups){0};
    context->actor->extra_effects &= ~UINT64_C(0x08018000);
}

void q2m_widow_power_think(q2m_context *context) {
    if (!widow(context->monster))
        return;
    context->actor->extra_effects &= ~UINT64_C(0x08018000);
    if (context->combat.health <= 0)
        return;
    const qa_builtin_powerups *power = &context->monster->widow_powers;
    uint64_t now = context->game->now_ns;
    if (shown(power->quad_until_ns, now))
        context->actor->extra_effects |= UINT64_C(32768);
    if (shown(power->double_until_ns, now))
        context->actor->extra_effects |= UINT64_C(134217728);
    if (shown(power->invulnerability_until_ns, now))
        context->actor->extra_effects |= UINT64_C(65536);
}

static bool armor(q2m_context *context, qa_error *error) {
    qa_q2_game *game = context->game;
    qa_actor_id id = context->actor->id;
    qa_inventory *inventory = game->services.inventory;
    qa_item_id item;
    if (!qa_combat_power_inventory(game->services.combat, id, &inventory, &item) &&
        !qa_builtin_resource(&game->services, "q2:monster-power", &item, error))
        return false;
    qa_inventory_entry fuel;
    qa_error observed = {0};
    bool present = qa_inventory_entry_read(inventory, id, item, &fuel, &observed);
    if (!q2m_alive(context))
        return true;
    if (!present && observed.code != QA_OK && observed.code != QA_ERROR_NOT_FOUND) {
        if (error)
            *error = observed;
        return false;
    }
    if (present && fuel.count > 0)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(game->services.combat, id, &combat, error))
        return false;
    if (!q2m_alive(context))
        return true;
    float cells = 250.0f * game->options.skill;
    combat.armor.powered = (qa_power_armor){.kind = QA_POWER_SHIELD, .cells = cells};
    if (!qa_combat_set_armor(game->services.combat, id, &combat.armor, error))
        return false;
    return !q2m_alive(context) || q2m_set_power_cells(context, QA_POWER_SHIELD, cells, error);
}

static bool powers(q2m_context *context, qa_actor_id actor, qa_builtin_powerups *out,
                   qa_error *error) {
    *out = (qa_builtin_powerups){0};
    if (!q2_actor_live(context->game, actor))
        return true;
    if (!context->game->services.powerups) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, actor.slot,
                     "Widow power copying requires selected effects projection");
        return false;
    }
    return context->game->services.powerups(context->game->services.context,
                                            context->game->options.owner, actor, out, error);
}

static bool respond(q2m_context *context, qa_actor_id actor, qa_error *error) {
    qa_builtin_powerups source;
    if (!powers(context, actor, &source, error))
        return false;
    if (!q2m_alive(context) || !q2_actor_live(context->game, actor))
        return true;
    qa_q2_game *game = context->game;
    qa_builtin_powerups *own = &context->monster->widow_powers;
    if (shown(source.quad_until_ns, game->now_ns)) {
        if (game->options.skill == 1) {
            own->double_until_ns = source.quad_until_ns;
            game->widow_damage_multiplier = 2;
        } else if (game->options.skill >= 2) {
            own->quad_until_ns = source.quad_until_ns;
            game->widow_damage_multiplier = 4;
            if (game->options.skill == 3 && !armor(context, error))
                return false;
        }
    } else if (shown(source.double_until_ns, game->now_ns)) {
        if (game->options.skill >= 2) {
            own->double_until_ns = source.double_until_ns;
            game->widow_damage_multiplier = 2;
            if (game->options.skill == 3 && !armor(context, error))
                return false;
        }
    } else {
        game->widow_damage_multiplier = 1;
    }
    if (!q2m_alive(context))
        return true;
    if (shown(source.invulnerability_until_ns, game->now_ns)) {
        if (game->options.skill == 1)
            return armor(context, error);
        if (game->options.skill >= 2) {
            own->invulnerability_until_ns = source.invulnerability_until_ns;
            if (game->options.skill == 3)
                return armor(context, error);
        }
    }
    return true;
}

bool q2m_widow_powerups(q2m_context *context, qa_error *error) {
    if (!widow(context->monster))
        return true;
    if (!context->game->options.cooperative)
        return !context->monster->enemy.registry ||
               respond(context, context->monster->enemy, error);
    q2_trace_frame *roster = q2_player_roster(context->game, error);
    if (!roster)
        return false;
    size_t count = 0;
    for (size_t i = 0; i < roster->snapshot.count && q2m_alive(context); ++i) {
        qa_actor_id actor = roster->snapshot.ids[i];
        qa_builtin_actor_traits traits;
        if (q2_actor_live(context->game, actor) && context->game->services.actor_traits &&
            context->game->services.actor_traits(context->game->services.context, actor, &traits) &&
            traits.player && q2_actor_live(context->game, actor))
            roster->snapshot.ids[count++] = actor;
    }
    bool ok = true, found = false;
    for (unsigned priority = 0; priority != 3 && !found && q2m_alive(context); ++priority) {
        for (size_t i = 0; i < count && q2m_alive(context); ++i) {
            qa_actor_id actor = roster->snapshot.ids[i];
            qa_builtin_powerups source;
            if (!powers(context, actor, &source, error)) {
                ok = false;
                break;
            }
            if (!q2m_alive(context) || !q2_actor_live(context->game, actor))
                continue;
            uint64_t until = priority == 0 ? source.invulnerability_until_ns
                             : priority == 1 ? source.quad_until_ns : source.double_until_ns;
            if (shown(until, context->game->now_ns)) {
                ok = respond(context, actor, error);
                found = true;
                break;
            }
        }
        if (!ok)
            break;
    }
    roster->active = false;
    return ok;
}
