#include "internal.h"
#include <stdio.h>

static q1_actor *campaign_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map ? e : NULL;
}
static const float Q1_COOP_SPAWN_ACTIVE = 73;
bool qa_q1_game_map_spawn_eligible(qa_q1_game *g, qa_actor_id actor) {
    q1_actor *point = q1_entity(g, actor);
    return point && (!point->map || point->map->kind != Q1_MAP_COOP_POINT ||
                     point->map->field_state == Q1_COOP_SPAWN_ACTIVE);
}
bool qa_q1_game_map_coop_spawn_grant(qa_q1_game *g, qa_actor_id actor, qa_actor_id point,
                                     qa_error *error) {
    if (!g->options.coop ||
        (g->options.program != QA_Q1_MG1 && g->options.program != QA_Q1_MG3))
        return true;
    q1_actor *source = q1_entity(g, point);
    if (!source || !source->map)
        return q1_map_fail(error, "Q1 cooperative spawn lost its actual authored point");
    uint32_t weapons = source->map->kind == Q1_MAP_COOP_POINT ? source->map->coop_weapons : 0;
    return qa_q1_game_coop_weapons_grant(g, actor, weapons, error);
}
static bool activate_coop_spawns(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_string_id target = e->target;
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    uint32_t weapons = 0;
    bool okay = true;
    for (size_t i = 0; okay && i < players->snapshot.count && q1_alive(g, id); ++i) {
        qa_actor_id player = players->snapshot.ids[i];
        if (!q1_alive(g, player))
            continue;
        uint32_t owned;
        okay = qa_q1_game_coop_weapons_read(g, player, &owned, error);
        if (okay)
            weapons |= owned;
    }
    qa_builtin_snapshot_release(players);
    if (!okay || !q1_alive(g, id))
        return okay;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next_authored(g->maps->options.targets, g->runtime_names[Q1_NAME_INFO_PLAYER_COOP], &cursor, &actor)) {
        q1_actor *point = campaign_actor(g, actor);
        if (!point || point->map->kind != Q1_MAP_COOP_POINT)
            continue;
        bool active = point->targetname == target;
        point->map->field_state = active ? Q1_COOP_SPAWN_ACTIVE : 0;
        point->map->coop_weapons = active ? weapons : 0;
    }
    return true;
}
bool q1_map_addon_sigil_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    bool mg3 = g->options.program == QA_Q1_MG3;
    uint32_t spawned = mg3 ? e->spawnflags & 128u : 0;
    uint32_t bits = e->spawnflags ? e->spawnflags : 1;
    unsigned index = 0, count = mg3 ? 4 : 6;
    while (index < count && !(bits & (1u << index)))
        ++index;
    if (index == count)
        return q1_map_fail(error, "Q1 addon sigil has no source rune model");
    qa_actor_id id = e->id;
    e->spawnflags = (1u << index) | spawned;
    e->map->style = (int32_t)(1u << index);
    char message[40];
    snprintf(message, sizeof(message), mg3 ? "$mg3_qc_rune%u" : "$qc_mg1_pickup_rune%u",
             index + 1);
    if (!q1_model(g, e, g->runtime_names[mg3?q1_rune_models[index]:q1_mg1_rune_models[index]], error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    if (!qa_builtin_resource(&g->services, message, &e->map->netname, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    e->physics.solid = QA_PHYSICS_TRIGGER;
    e->physics.motion = QA_PHYSICS_TOSS;
    e->map->touch_enabled = true;
    body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    e = campaign_actor(g, id);
    return !e || (q1_map_schedule(g, e, .2, Q1_MAP_SIGIL_PLACE, error) && q1_link(g, e, error));
}
bool q1_map_addon_sigil_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other,
                              qa_error *error) {
    qa_actor_id id = e->id;
    if (e->physics.solid != QA_PHYSICS_TRIGGER || !q1_map_player(g, other) ||
        !q1_alive(g, other) || q1_health(g, other) <= 0)
        return true;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    const char *message = qa_strings_cstr(qa_session_strings(g->services.session), e->map->netname);
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < players->snapshot.count && q1_alive(g, id); ++i)
        if (q1_alive(g, players->snapshot.ids[i]))
            ok = q1_message(g, players->snapshot.ids[i], message ? message : "", error);
    qa_builtin_snapshot_release(players);
    if (!ok)
        return false;
    e = campaign_actor(g, id);
    if (!e || !q1_alive(g, other))
        return true;
    if (!q1_sound_resource(g, other, g->runtime_names[Q1_NAME_RESOURCE_MISC_RUNEKEY_WAV], 3, 1, 1, error))
        return false;
    if (!campaign_actor(g, id) || !q1_alive(g, other))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!campaign_actor(g, id))
        return true;
    if (!q1_effect(g, QA_BUILTIN_ITEM, other, body.origin, 1, 0, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    e->physics.solid = QA_PHYSICS_NOT_SOLID;
    e->model = QA_STRING_NONE;
    if (!q1_link(g, e, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    bool mg3 = g->options.program == QA_Q1_MG3;
    uint32_t bits = mg3 ? (uint32_t)e->map->style : e->spawnflags & 31;
    *g->maps->options.server_flags |= bits | (mg3 ? 0 : bits << 6);
    if (!mg3 && (bits & 2)) {
        float horde = 0;
        if (!q1_source_value(g, QA_Q1_SOURCE_HORDE, 0, &horde, error))
            return false;
        if (!campaign_actor(g, id))
            return true;
        if (horde != 0) {
            if (!q1_snapshot_players(g, &players, error))
                return false;
            for (size_t i = 0; ok && i < players->snapshot.count && q1_alive(g, id); ++i)
                if (q1_alive(g, players->snapshot.ids[i]))
                    ok = q1_map_addon_hunger(g, players->snapshot.ids[i], (float)(g->time + 10), error);
            qa_builtin_snapshot_release(players);
            if (!ok)
                return false;
        }
    }
    if (!campaign_actor(g, id))
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = other,
                              .code = (int32_t)bits,
                              .flags = (uint32_t)g->options.program,
                              .time_ns = g->time_ns};
    if (!qa_builtin_resource(&g->services, "rune-collected", &event.resource, error) ||
        !qa_builtin_emit(&g->services, &event, error))
        return false;
    e = campaign_actor(g, id);
    return !e || q1_map_targets(g, e, other, error);
}
static bool indicator_model(qa_q1_game *g, q1_actor *e, qa_error *error) {
    uint32_t bits = e->spawnflags ? e->spawnflags : 1;
    unsigned index = 0;
    while (index < 6 && !(bits & (1u << index)))
        ++index;
    if (index == 6)
        return q1_map_fail(error, "Q1 rune indicator has no source rune model");
    e->spawnflags = 1u << index;
    return q1_model(g, e, g->runtime_names[q1_mg1_rune_models[index]], error);
}
bool q1_map_addon_campaign_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    switch (e->map->kind) {
    case Q1_MAP_COOP_POINT:
        if (!g->options.coop)
            return q1_remove(g, e, error);
        if (!qa_builtin_resource(&g->services, "info_player_coop", &e->map->netname, error))
            return false;
        if (!e->targetname || (e->spawnflags & 1u))
            e->map->field_state = Q1_COOP_SPAWN_ACTIVE;
        return true;
    case Q1_MAP_COOP_ACTIVATE:
        if (!g->options.coop)
            return q1_remove(g, e, error);
        e->map->use_enabled = true;
        return true;
    case Q1_MAP_RUNE_INDICATOR: {
        bool active = (e->spawnflags & 64) != 0;
        e->spawnflags &= ~64u;
        if (!indicator_model(g, e, error))
            return false;
        e = campaign_actor(g, id);
        if (!e)
            return true;
        e->map->use_enabled = true;
        if (active || (*g->maps->options.server_flags & e->spawnflags))
            return q1_map_schedule(g, e, .2, Q1_MAP_CAMPAIGN_USE_TARGETS, error);
        e->alpha = .2f;
        return true;
    }
    case Q1_MAP_SIGIL_FIXER:
        e->map->use_enabled = true;
        return q1_map_schedule(g, e, .8, Q1_MAP_SIGIL_FIX, error);
    case Q1_MAP_ELECTRODE_TARGET:
        return true;
    case Q1_MAP_EGG_OPENER:
        e->map->use_enabled = true;
        return true;
    default:
        return q1_map_fail(error, "unknown Q1 addon campaign actor");
    }
}
bool q1_map_addon_campaign_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator,
                               qa_error *error) {
    if (e->map->kind == Q1_MAP_COOP_ACTIVATE)
        return activate_coop_spawns(g, e, error);
    if (e->map->kind == Q1_MAP_EGG_OPENER) {
        qa_actor_id id = e->id;
        qa_builtin_snapshot_frame *targets;
        if (!q1_snapshot_targets(g, g->maps->options.targets, e->target, &targets, error))
            return false;
        bool ok = true;
        for (size_t i = 0; ok && i < targets->snapshot.count && campaign_actor(g, id); ++i) {
            qa_actor_id actor = targets->snapshot.ids[i];
            qa_authored_target fields;
            if (!qa_targets_read(g->maps->options.targets, actor, &fields))
                continue;
            const char *classname = qa_strings_cstr(qa_session_strings(g->services.session),
                                                    fields.classname);
            if (!campaign_actor(g, id) || !q1_alive(g, actor) || !classname ||
                strcmp(classname, "func_door"))
                continue;
            q1_actor *door = campaign_actor(g, actor);
            if (door && door->native && door->map->kind == Q1_MAP_DOOR)
                ok = q1_map_addon_egg_mover(g, door, error);
            else if (g->maps->options.egg_mover)
                ok = g->maps->options.egg_mover(g->maps->options.context, actor, error);
            else
                ok = q1_map_fail(error, "Q1 egg opener requires the selected mover owner");
        }
        qa_builtin_snapshot_release(targets);
        return ok;
    }
    if (e->map->kind == Q1_MAP_RUNE_INDICATOR) {
        e->alpha = 1;
        return q1_map_targets(g, e, activator, error);
    }
    if (e->map->kind != Q1_MAP_SIGIL_FIXER)
        return q1_map_fail(error, "invalid Q1 addon campaign use");
    bool enable = e->map->effect_active;
    e->map->effect_active = true;
    qa_actor_id id = e->id;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next_authored(g->maps->options.targets, 0, &cursor, &actor)) {
        if (!campaign_actor(g, id))
            return true;
        q1_actor *rune = campaign_actor(g, actor);
        if (rune && rune->map->kind == Q1_MAP_SIGIL)
            rune->map->touch_enabled = enable;
    }
    return true;
}
bool q1_map_addon_electrode_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other,
                                  qa_error *error) {
    qa_actor_id id = e->id;
    if (!q1_map_player(g, other) || !q1_alive(g, other) || q1_health(g, other) <= 0)
        return true;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    if (!q1_map_mover_touch(g, e, other, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    e->map->touch_enabled = false;
    float count = e->map->counter_value;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next_authored(g->maps->options.targets, g->runtime_names[Q1_NAME_MGE2M2_ELECTRODE_TARGET],
                                    &cursor, &actor)) {
        double target_count;
        if (!campaign_actor(g, id))
            return true;
        if (!qa_targets_number(g->maps->options.targets, actor, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_CNT], &target_count) ||
            target_count != count)
            continue;
        if (!campaign_actor(g, id))
            return true;
        if (!q1_alive(g, actor))
            continue;
        q1_actor *target = campaign_actor(g, actor);
        if (target && target->native && target->map->kind == Q1_MAP_ELECTRODE_TARGET) {
            if (!q1_map_schedule(g, target, .1, Q1_MAP_REMOVE, error))
                return false;
        } else if (g->maps->options.schedule_remove) {
            if (!g->maps->options.schedule_remove(g->maps->options.context, actor, .1, error))
                return false;
        } else
            return q1_map_fail(error, "Q1 electrode requires the selected removal owner");
    }
    return true;
}
bool q1_map_addon_campaign_think(qa_q1_game *g, q1_actor *e, q1_map_action action,
                                 qa_error *error) {
    if (action == Q1_MAP_CAMPAIGN_USE_TARGETS)
        return q1_map_targets(g, e, q1_ref_actor(g, e->activator), error);
    if (action == Q1_MAP_SIGIL_FIX)
        return q1_map_addon_campaign_use(g, e, q1_ref_actor(g, e->activator), error);
    return q1_map_fail(error, "invalid Q1 addon campaign action");
}

bool q1_map_addon_bossgate_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    bool inverse = (e->spawnflags & 64) != 0;
    e->spawnflags &= ~64u;
    if (!e->spawnflags)
        e->spawnflags = 15;
    e->spawnflags &= 31;
    bool complete = (*g->maps->options.server_flags & e->spawnflags) == e->spawnflags;
    if (complete != inverse)
        return q1_remove(g, e, error);
    if (!e->map->has_inline_model)
        return q1_map_fail(error, "Q1 addon boss gate has no brush model");
    qa_actor_id id = e->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    e->physics.solid = QA_PHYSICS_BRUSH;
    e->physics.motion = QA_PHYSICS_PUSH;
    e->map->use_enabled = true;
    body.angles = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    e = campaign_actor(g, id);
    if (!e)
        return true;
    if (!q1_link(g, e, error))
        return false;
    e = campaign_actor(g, id);
    return !e || (!e->target && !e->killtarget) ||
           q1_map_schedule(g, e, .2, Q1_MAP_CAMPAIGN_USE_TARGETS, error);
}

bool q1_map_addon_changelevel_begin(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_string_id map = e->map->map;
    bool finale = (e->spawnflags & 1) != 0;
    if (!qa_q1_level_begin(g->maps->options.level, map, q1_ref_actor(g, e->activator), g->time, error))
        return false;
    if (!finale || !campaign_actor(g, id))
        return true;
    qa_q1_intermission_result result;
    if (!qa_q1_level_client_connected(g->maps->options.level, g->time, false, &result, error))
        return false;
    if (!campaign_actor(g, id))
        return true;
    if (result.kind == QA_Q1_INTERMISSION_FINALE) {
        qa_q1_game_finale_reset(g);
        g->maps->finale.map = map;
    }
    return q1_map_present_intermission(g, id, 2, &result, error);
}
