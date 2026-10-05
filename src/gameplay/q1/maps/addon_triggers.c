#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

static q1_actor *trigger(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_addon_trigger(e->map->kind) ? e : NULL;
}
static bool targets(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    e->activator = q1_ref_from(g, activator);
    return q1_map_targets(g, e, activator, error);
}
static bool counter_message(qa_q1_game *g, q1_actor *e, qa_actor_id activator,
                            bool sacrifice, bool force, qa_error *error) {
    qa_actor_id id = e->id;
    float count = e->count;
    if ((!force && (e->spawnflags & 1)) || !q1_map_player(g, activator))
        return true;
    if (!q1_alive(g, id) || !q1_alive(g, activator))
        return true;
    const char *message;
    char text[96];
    if (sacrifice) {
        message = count == 0 ? "$mg3_qc_sacricie_count_complete"
                            : "$mg3_qc_sacricie_count_more";
        if (count >= 1 && count <= 8) {
            char number[32];
            if (!qa_format_number(count, number, error))
                return false;
            snprintf(text, sizeof(text), "$mg3_qc_sacricie_count_%s_more", number);
            message = text;
        }
    } else
        message = count == 0   ? "$qc_sequence_completed"
                  : count == 1 ? "$qc_one_more"
                  : count == 2 ? "$qc_two_more"
                  : count == 3 ? "$qc_three_more" : "$qc_more_go";
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < players->snapshot.count && q1_alive(g, id); ++i)
        if (q1_alive(g, players->snapshot.ids[i]))
            ok = q1_message(g, players->snapshot.ids[i], message, error);
    qa_builtin_snapshot_release(players);
    return ok;
}
static bool counter_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator,
                        qa_error *error) {
    qa_actor_id id = e->id;
    bool timed = e->map->kind == Q1_MAP_ADDON_COUNTER_TIMED;
    bool sacrifice = e->map->kind == Q1_MAP_ADDON_SACRIFICE_COUNTER;
    e->count -= 1;
    if (e->count < 0)
        return true;
    if (timed && !q1_map_schedule(g, e, e->delay, Q1_MAP_ADDON_COUNTER_RESET, error))
        return false;
    if (!counter_message(g, e, activator, sacrifice, false, error))
        return false;
    e = trigger(g, id);
    if (!e || e->count != 0)
        return true;
    if (timed) {
        e->delay = 0;
        q1_map_cancel(g, e);
    } else if (!sacrifice && (e->spawnflags & 2))
        e->count = e->wait;
    if (!q1_map_multi_fire(g, e, activator, error))
        return false;
    e = trigger(g, id);
    return !timed || !e || q1_remove(g, e, error);
}
static bool repeat(qa_q1_game *g, q1_actor *e, qa_error *error) {
    double delay = e->wait + e->map->pause_time * (double)q1_random(g);
    return q1_map_schedule(g, e, delay, Q1_MAP_ADDON_REPEAT_TICK, error);
}
static bool explosion(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    e->delay = 0;
    if (!targets(g, e, q1_ref_actor(g, e->activator), error))
        return false;
    e = trigger(g, id);
    if (!e)
        return true;
    if (!(e->spawnflags & 1) &&
        !q1_radius(g, id, q1_ref_actor(g, e->owner), 120, id, QA_Q1_WEAPON_COUNT, error))
        return false;
    e = trigger(g, id);
    if (!e)
        return true;
    bool colored = g->options.program == QA_Q1_MG3 && (e->spawnflags & 2);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!trigger(g, id))
        return true;
    if (colored) {
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                                   .family = QA_GAME_Q1,
                                   .provider = g->options.provider,
                                   .actor = id,
                                   .origin = body.origin,
                                   .code = 244,
                                   .count = 3,
                                   .time_ns = g->time_ns};
        if (!qa_builtin_resource(&g->services, "colored-explosion", &event.resource, error) ||
            !qa_builtin_emit(&g->services, &event, error))
            return false;
    } else if (!q1_effect(g, QA_BUILTIN_EXPLOSION, id, body.origin, 0, 0, error))
        return false;
    e = trigger(g, id);
    return !e || q1_remove(g, e, error);
}
static bool change_targets(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_string_id old = e->target, replacement = e->killtarget;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next_authored(g->maps->options.targets, NULL, &cursor, &actor)) {
        if (!trigger(g, id))
            return true;
        qa_authored_target fields;
        if (!qa_targets_read(g->maps->options.targets, actor, &fields))
            continue;
        if (!trigger(g, id))
            return true;
        if (fields.target == old &&
            !qa_targets_set_target(g->maps->options.targets, actor, replacement, error))
            return false;
    }
    return true;
}
static bool cleanup(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_builtin_snapshot_frame *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < list->snapshot.count && trigger(g, id); ++i) {
        qa_actor_id actor = list->snapshot.ids[i];
        qa_builtin_actor_traits traits = {0};
        bool found = g->services.actor_traits(g->services.context, actor, &traits);
        if (!trigger(g, id))
            break;
        if (found && traits.monster && q1_health(g, actor) <= 0 && trigger(g, id) &&
            q1_alive(g, actor))
            ok = qa_session_release(g->services.session, actor, error);
    }
    qa_builtin_snapshot_release(list);
    e = trigger(g, id);
    return !ok ? false : !e || q1_remove(g, e, error);
}
bool q1_map_addon_trigger_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    bool field = kind == Q1_MAP_ADDON_MULTITOUCH || kind == Q1_MAP_ADDON_CHECK_SACRIFICES;
    bool filter = kind != Q1_MAP_ADDON_BN_RELAY && kind != Q1_MAP_ADDON_CLEANUP;
    if (filter &&
        ((g->options.coop ? !field && (e->spawnflags & 131072u)
                         : (e->spawnflags & 32768u)) ||
         (field && g->options.program == QA_Q1_MG3 &&
          (e->spawnflags & (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags))))))
        return q1_remove(g, e, error);
    e->map->use_enabled = true;
    switch (kind) {
    case Q1_MAP_ADDON_COUNTER:
    case Q1_MAP_ADDON_SACRIFICE_COUNTER:
        if (e->count == 0) e->count = 2;
        if (e->wait == 0) e->wait = e->count;
        return true;
    case Q1_MAP_ADDON_COUNTER_TIMED:
        e->wait = -1;
        if (e->count == 0) e->count = 2;
        if (e->delay == 0) e->delay = 2;
        e->map->counter_value = e->count;
        return true;
    case Q1_MAP_ADDON_REPEATER:
        if (e->wait == 0) e->wait = 1;
        return !(e->spawnflags & 1) || repeat(g, e, error);
    case Q1_MAP_ADDON_MULTITOUCH:
    case Q1_MAP_ADDON_CHECK_SACRIFICES:
        if (!q1_map_trigger_init(g, e, true, error))
            return false;
        e = trigger(g, id);
        if (!e)
            return true;
        e->wait = 0;
        e->map->use_enabled = false;
        e->map->touch_enabled = true;
        return q1_link(g, e, error);
    case Q1_MAP_ADDON_CHANGE_TARGET:
        return (q1_map_text(g, e->target) && q1_map_text(g, e->killtarget)) ||
               q1_map_fail(error, "Addon target changer requires target and killtarget");
    case Q1_MAP_ADDON_CLEANUP:
        return g->options.coop || q1_remove(g, e, error);
    case Q1_MAP_ADDON_ALWAYS:
        e->map->use_enabled = false;
        return q1_map_schedule(g, e, .1, Q1_MAP_ADDON_TARGETS, error);
    case Q1_MAP_ADDON_RUNE_COUNTER:
        if (e->count == 0) e->count = 2;
        return true;
    case Q1_MAP_ADDON_KILL_MONSTER:
        return q1_map_text(g, e->target) && q1_map_text(g, e->targetname)
                   ? true : q1_remove(g, e, error);
    case Q1_MAP_ADDON_HEALTH_RELAY:
        if (q1_health(g, id) != 0 || !trigger(g, id))
            return true;
        return qa_combat_set_health(g->services.combat, id, .5f, error);
    case Q1_MAP_ADDON_EXPLOSION:
    case Q1_MAP_ADDON_RUNE_RELAY:
    case Q1_MAP_ADDON_BN_RELAY:
        return true;
    default:
        return q1_map_fail(error, "Unknown addon trigger spawn");
    }
}
bool q1_map_addon_trigger_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator,
                              qa_error *error) {
    qa_actor_id id = e->id;
    switch (e->map->kind) {
    case Q1_MAP_ADDON_COUNTER:
    case Q1_MAP_ADDON_COUNTER_TIMED:
    case Q1_MAP_ADDON_SACRIFICE_COUNTER:
        return counter_use(g, e, activator, error);
    case Q1_MAP_ADDON_REPEATER:
        e->activator = q1_ref_from(g, activator);
        e->spawnflags ^= 1;
        if (!(e->spawnflags & 1)) {
            q1_map_cancel(g, e);
            return true;
        }
        return repeat(g, e, error);
    case Q1_MAP_ADDON_EXPLOSION:
        e->activator = q1_ref_from(g, activator);
        return e->delay > 0 ? q1_map_schedule(g, e, e->delay, Q1_MAP_ADDON_EXPLOSION_FIRE, error)
                           : explosion(g, e, error);
    case Q1_MAP_ADDON_CHANGE_TARGET:
        return change_targets(g, e, error);
    case Q1_MAP_ADDON_CLEANUP:
        return cleanup(g, e, error);
    case Q1_MAP_ADDON_RUNE_RELAY: {
        uint32_t required = e->spawnflags & 15;
        if ((*g->maps->options.server_flags & required) != required)
            return true;
        e->activator = q1_ref_from(g, activator);
        return q1_map_schedule(g, e, .1, Q1_MAP_ADDON_TARGETS, error);
    }
    case Q1_MAP_ADDON_RUNE_COUNTER:
        return (float)qa_q1_mg3_rune_count(*g->maps->options.server_flags) < e->count ||
               targets(g, e, activator, error);
    case Q1_MAP_ADDON_BN_RELAY: {
        static const uint32_t bits[] = {QA_Q1_BLOODY_NIGHTMARE_ACTIVE,
                                        QA_Q1_BLOODY_NIGHTMARE_NEWGAME,
                                        QA_Q1_BLOODY_NIGHTMARE_DISCOVERED};
        uint32_t flags = *g->maps->options.server_flags;
        for (unsigned i = 0; i < sizeof(bits) / sizeof(*bits); ++i)
            if (((e->spawnflags & (1u << i)) && !(flags & bits[i])) ||
                ((e->spawnflags & (8u << i)) && (flags & bits[i])))
                return true;
        return targets(g, e, activator, error);
    }
    case Q1_MAP_ADDON_KILL_MONSTER: {
        qa_string_id name = e->target;
        qa_target_cursor cursor = {0};
        qa_actor_id actor;
        while (qa_targets_next(g->maps->options.targets, name, &cursor, &actor)) {
            if (!trigger(g, id))
                break;
            qa_builtin_actor_traits traits = {0};
            bool found = g->services.actor_traits(g->services.context, actor, &traits);
            if (!trigger(g, id))
                break;
            float health = q1_health(g, actor);
            if (!trigger(g, id))
                break;
            if (found && traits.monster && health > 0 && q1_alive(g, actor) &&
                !q1_damage(g, actor, activator.registry ? activator : id, activator,
                           health * 2, QA_Q1_WEAPON_COUNT, error))
                return false;
        }
        return true;
    }
    case Q1_MAP_ADDON_HEALTH_RELAY:
        return targets(g, e, activator, error);
    default:
        return true;
    }
}
bool q1_map_addon_trigger_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other,
                                qa_error *error) {
    qa_actor_id id = e->id;
    if (!q1_map_player(g, other) || !trigger(g, id) || q1_health(g, other) <= 0)
        return true;
    e = trigger(g, id);
    if (!e)
        return true;
    bool sacrifice = e->map->kind == Q1_MAP_ADDON_CHECK_SACRIFICES;
    if (e->wait == 0) {
        e->wait = 1;
        if (sacrifice) {
            qa_actor_id counter;
            if (qa_targets_first(g->maps->options.targets, e->target, &counter)) {
                q1_actor *target = q1_entity(g, counter);
                if (trigger(g, id) && target && target->map && target->count > 0 &&
                    !counter_message(g, target, other, true, true, error))
                    return false;
            }
        } else if (!(e->spawnflags & 16) && !targets(g, e, other, error))
            return false;
    }
    e = trigger(g, id);
    return !e || q1_map_schedule(g, e, .2, Q1_MAP_ADDON_MULTITOUCH_EMPTY, error);
}
bool q1_map_addon_trigger_think(qa_q1_game *g, q1_actor *e, q1_map_action action,
                                qa_error *error) {
    if (!q1_map_addon_trigger_action_matches(e->map->kind, action))
        return q1_map_fail(error, "Addon trigger callback belongs to a different source owner");
    qa_actor_id id = e->id;
    switch (action) {
    case Q1_MAP_ADDON_COUNTER_RESET:
        e->count = e->map->counter_value;
        return true;
    case Q1_MAP_ADDON_REPEAT_TICK:
        if (!targets(g, e, q1_ref_actor(g, e->activator), error))
            return false;
        e = trigger(g, id);
        return !e || repeat(g, e, error);
    case Q1_MAP_ADDON_MULTITOUCH_EMPTY:
        e->wait = 0;
        return e->map->kind == Q1_MAP_ADDON_CHECK_SACRIFICES || (e->spawnflags & 32) ||
               targets(g, e, q1_ref_actor(g, e->activator), error);
    case Q1_MAP_ADDON_TARGETS:
        return targets(g, e, q1_ref_actor(g, e->activator), error);
    case Q1_MAP_ADDON_EXPLOSION_FIRE:
        return explosion(g, e, error);
    default:
        return q1_map_fail(error, "Unknown addon trigger continuation");
    }
}
