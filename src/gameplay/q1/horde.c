#include "internal.h"
#include "qa/game_q1_maps.h"

static bool horde_enabled(qa_q1_game *g, bool *enabled, qa_error *error) {
    if (g->host.horde) {
        *enabled = g->host.horde(g->host.context);
        return true;
    }
    float value = 0;
    if (g->services.cvar) {
        qa_string_id name;
        if (!qa_builtin_resource(&g->services, "horde", &name, error) ||
            !g->services.cvar(q1_cvar_context(g), name, &value, error))
            return false;
    }
    *enabled = value != 0;
    return true;
}

bool qa_q1_horde_after_death(qa_q1_game *g, qa_actor_id actor, bool enabled, qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing Q1 game");
        return false;
    }
    q1_actor *entity = q1_entity(g, actor);
    if (!enabled || !entity || entity->physics.motion != QA_PHYSICS_BOUNCE)
        return true;
    qa_bytes model = qa_strings_text(qa_session_strings(g->services.session), entity->model);
    if (model.size < 8 || memcmp(model.data, "progs/h_", 8))
        return true;
    return q1_schedule(g, entity, 1, Q1_THINK_HORDE_HEAD_WAIT, error);
}

bool q1_horde_head_think(qa_q1_game *g, q1_actor *entity, q1_think_kind kind, qa_error *error) {
    if (kind == Q1_THINK_HORDE_HEAD_WAIT) {
        bool enabled;
        if (!horde_enabled(g, &enabled, error))
            return false;
        if (!enabled)
            return true;
        if (entity->alpha == 0)
            entity->alpha = 1;
        return q1_schedule(g, entity, 10 + q1_random(g) * 5, Q1_THINK_HORDE_HEAD_STEP, error);
    }
    if (entity->alpha <= 0)
        return q1_remove(g, entity, error);
    entity->alpha -= (float)g->elapsed;
    return q1_schedule(g, entity, 0, Q1_THINK_HORDE_HEAD_STEP, error);
}

bool qa_q1_horde_axe_chain(qa_q1_game *g, qa_actor_id actor, uint32_t hits, double expires,
                           qa_error *error) {
    if (!g || !isfinite(expires)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 Horde axe chain");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->horde_axe_chain = hits;
    player->horde_axe_chain_until = expires;
    return true;
}

bool q1_horde_axe_interval(qa_q1_game *g, q1_player *player, float *interval, bool *enabled,
                           qa_error *error) {
    *enabled = false;
    if ((g->options.program != QA_Q1_DOPA && g->options.program != QA_Q1_MG1) ||
        !(qa_q1_game_campaign_flags(g) & 4))
        return true;
    if (!horde_enabled(g, enabled, error))
        return false;
    if (!q1_alive(g, player->id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, player->id.slot,
                     "Q1 axe observation retired during source policy");
        return false;
    }
    if (!*enabled)
        return true;
    bool chop = player->horde_axe_chain >= 2 && g->time < player->horde_axe_chain_until;
    *interval = chop ? 0.8f : player->horde_axe_chain > 1 ? 0.6f : 0.4f;
    return true;
}
bool q1_horde_axe_delay(qa_q1_game *g, q1_player *player, float *interval, qa_error *error) {
    if (player->weapon != QA_Q1_AXE)
        return true;
    bool enabled;
    if (!q1_horde_axe_interval(g, player, interval, &enabled, error))
        return false;
    if (enabled) {
        bool chop = player->horde_axe_chain >= 2 && g->time < player->horde_axe_chain_until;
        player->animation_base = chop ? 1 : 5;
    }
    return true;
}

bool qa_q1_horde_spawn(qa_q1_game *g, const char *classname, qa_vec3 origin, qa_vec3 angles,
                       qa_actor_id manager, qa_actor_id enemy, qa_actor_id *out, qa_error *error) {
    if (!g || !classname || !out || !qa_vec_finite(origin) || !qa_vec_finite(angles)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 Horde spawn");
        return false;
    }
    const q1_species *species = q1_species_find(classname);
    if (!species || species->species > QA_Q1_ZOMBIE || species->species == QA_Q1_TARBABY ||
        species->species == QA_Q1_FISH) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unsupported Horde monster %s", classname);
        return false;
    }
    qa_q1_spawn spawn = {.classname = classname, .origin = origin, .angles = angles};
    qa_actor_id actor;
    if (!qa_q1_game_spawn(g, &spawn, &actor, error))
        return false;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Horde native spawn was inhibited by source mode");
        return false;
    }
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), actor);
    entity->think = Q1_THINK_NONE;
    entity->next_think = 0;
    entity->state.monster.horde = true;
    entity->state.monster.addon.waiting = false;
    entity->state.monster.addon.started = true;
    entity->state.monster.enemy = enemy;
    entity->owner = manager;
    entity->aimed_damage = true;
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    entity->physics.yaw_speed = 20;
    entity->physics.enemy = enemy;
    entity->physics.goal = enemy;
    if (species->species == QA_Q1_WIZARD)
        entity->physics.flags |= QA_PHYSICS_FLYING;
    float offset = species->species == QA_Q1_DEMON ? 48
                   : species->species == QA_Q1_OGRE || species->species == QA_Q1_SHAMBLER ||
                           species->species == QA_Q1_SHALRATH || species->species == QA_Q1_WIZARD ||
                           species->species == QA_Q1_ZOMBIE
                       ? 32
                       : 24;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!entity->state.monster.addon.enabled)
        body.bounds = species->bounds;
    if (!q1_model(g, entity, species->model, error))
        return false;
    body.origin = qa_vec_add(origin, qa_v3(0, 0, offset + 1));
    if (species->species != QA_Q1_WIZARD) {
        qa_trace_query query = {.start = body.origin,
                                .end = qa_vec_sub(body.origin, qa_v3(0, 0, 256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = actor,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.fraction < 1 && !trace.all_solid) {
            body.origin = trace.end;
            body.ground = trace.actor;
            entity->physics.flags |= QA_PHYSICS_ONGROUND;
        }
    }
    qa_combat_state combat;
    bool moved;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
        return false;
    combat.can_take_damage = true;
    if (!qa_builtin_resource(&g->services, "q1:monsters", &combat.team, error))
        return false;
    if (!qa_combat_set_traits(g->services.combat, actor, &combat, error) ||
        !qa_world_body_write(g->services.world, actor, &body, error) ||
        !qa_physics_walk_move(g->services.physics, actor, 0, 0, (float)g->elapsed, true, true,
                              &moved, error))
        return false;
    if (species->species == QA_Q1_ZOMBIE)
        --g->total_monsters;
    if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FOUND, error))
        return false;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_actor_id death_id;
    if (!q1_spawn_teledeath(g, body.origin, actor, 0.01, false, &death_id, error))
        return false;
    q1_actor *death = q1_entity(g, death_id);
    qa_body_state death_body;
    if (!death || !qa_world_body_read(g->services.world, death_id, &death_body, error))
        return false;
    qa_bounds overlap = {qa_vec_add(death_body.origin, death_body.bounds.mins),
                         qa_vec_add(death_body.origin, death_body.bounds.maxs)};
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id victim = snapshot->actors[i];
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, victim, &target, NULL))
            continue;
        qa_bounds bounds = {qa_vec_add(target.origin, target.bounds.mins),
                            qa_vec_add(target.origin, target.bounds.maxs)};
        if (qa_bounds_overlap(overlap, bounds) && !q1_teledeath_touch(g, death, victim, error)) {
            ok = false;
            break;
        }
        if (!q1_alive(g, death_id))
            break;
    }
    snapshot->borrowed = false;
    if (ok)
        *out = actor;
    return ok;
}
