#include "internal.h"

static q1_actor *hazard(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && q1_map_is_rogue_hazard(entity->map->kind) ? entity : NULL;
}
bool qa_q1_game_rogue_earthquake(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g || !g->maps || g->options.program != QA_Q1_ROGUE || !q1_alive(g, g->maps->world_actor) ||
        !g->maps->rogue_quake_active)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = q1_map_rogue_shake(g, actor, g->maps->rogue_quake_intensity, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 source retired during Rogue earthquake");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_map_rogue_shake(qa_q1_game *g, qa_actor_id actor, float intensity, qa_error *error) {
    if (!q1_alive(g, actor) || !qa_world_body_storage_serial(g->services.world, actor))
        return true;
    qa_actor_id world = g->maps->world_actor;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_physics_properties ground_flags;
    if (!g->services.physics || !g->services.physics->services.read(
        g->services.physics->services.context, actor, &ground_flags)) return true;
    if (!q1_alive(g, world) || !q1_alive(g, actor) || !(ground_flags.flags & QA_PHYSICS_ONGROUND))
        return true;
    float x = q1_random(g) * intensity * 2 - intensity;
    float y = q1_random(g) * intensity * 2 - intensity;
    float z = q1_random(g) * intensity * 2 - intensity;
    qa_vec3 impulse = qa_v3(x, y, z);
    body.velocity = qa_vec_add(body.velocity, impulse);
    return qa_world_body_write(g->services.world, actor, &body, error);
}
static bool quake_stop(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (q1_alive(g, g->maps->world_actor))
        g->maps->rogue_quake_active = false;
    double delay = entity->spawnflags & 1 ? q1_random(g) * entity->wait : entity->wait;
    return q1_map_schedule(g, entity, delay, Q1_MAP_ROGUE_QUAKE_START, error);
}
static bool quake_rumble(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->map->active_until < g->time)
        return quake_stop(g, entity, error);
    qa_actor_id id = entity->id;
    if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_EQUAKE_RUMBLE_WAV], 2, 0, 1, error))
        return false;
    entity = hazard(g, id);
    return !entity || q1_map_schedule(g, entity, 1, Q1_MAP_ROGUE_QUAKE_RUMBLE, error);
}
static bool saw_start(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    entity->map->touch_enabled = true;
    entity->map->use_enabled = false;
    qa_actor_id target = {0};
    (void)qa_targets_first(g->maps->options.targets, entity->target, &target);
    entity = hazard(g, id);
    if (!entity)
        return true;
    entity->physics.goal = entity->map->pending.follower.move_target = q1_ref_from(g, target);
    return q1_map_schedule(
        g, entity, .1, q1_map_text(g, entity->target) ? Q1_MAP_SAW_FLY : Q1_MAP_SAW_STAND, error);
}
static qa_actor_id goal_or_world(qa_q1_game *g, qa_actor_id goal) {
    return q1_alive(g, goal) ? goal : g->maps->world_actor;
}
static bool saw_frame(qa_q1_game *g, q1_actor *entity, bool flying, qa_error *error) {
    qa_actor_id id = entity->id;
    entity->frame = flying ? 1 : 0;
    if (entity->map->cooldown < g->time) {
        if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_BUZZ_BUZZ1_WAV], 2, 1, .2f, error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        entity->map->cooldown = g->time + 1;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    qa_vec3 angles = body.angles;
    if (flying) {
        entity = hazard(g, id);
        if (!entity)
            return true;
        qa_actor_id goal = goal_or_world(g, q1_ref_actor(g, entity->physics.goal));
        if (!q1_alive(g, goal))
            return q1_map_fail(error, "Buzzsaw requires worldspawn");
        qa_body_state destination;
        if (!qa_world_body_read(g->services.world, goal, &destination, error))
            return false;
        entity = hazard(g, id);
        if (!entity || !q1_alive(g, goal))
            return true;
        qa_vec3 origin = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_normalize(qa_vec_sub(destination.origin, body.origin)),
                                      entity->speed));
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!hazard(g, id))
            return true;
        body.origin = origin;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = hazard(g, id);
        if (!entity || !q1_link(g, entity, error))
            return entity == NULL;
    }
    if (!hazard(g, id))
        return true;
    if (flying && !qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!hazard(g, id))
        return true;
    angles.x -= 60;
    body.angles = angles;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = hazard(g, id);
    if (!entity)
        return true;
    entity->physics.angular_velocity.x = 60;
    return q1_map_schedule(g, entity, .1, flying ? Q1_MAP_SAW_FLY : Q1_MAP_SAW_STAND, error);
}
static bool saw_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_builtin_actor_traits traits;
    if (!g->services.actor_traits(g->services.context, other, &traits) ||
        (!traits.player && !traits.monster))
        return true;
    entity = hazard(g, id);
    if (!entity || !q1_alive(g, other))
        return true;
    if (entity->map->active_until < g->time) {
        if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_BUZZ_BUZZ_WAV], 1, 1, 1, error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        entity->map->active_until = g->time + 2;
    }
    if (!q1_alive(g, other))
        return true;
    if (!q1_damage(g, other, id, id, entity->map->current_ammo, QA_Q1_WEAPON_COUNT, error))
        return false;
    entity = hazard(g, id);
    if (!entity || !q1_alive(g, other) || !qa_world_body_storage_serial(g->services.world, other))
        return true;
    qa_actor_id goal = goal_or_world(g, q1_ref_actor(g, entity->physics.goal));
    if (!q1_alive(g, goal))
        return true;
    qa_body_state body, destination, self;
    if (!qa_world_body_read(g->services.world, other, &body, error))
        return false;
    if (!hazard(g, id) || !q1_alive(g, other) || !q1_alive(g, goal))
        return true;
    if (!qa_world_body_read(g->services.world, goal, &destination, error))
        return false;
    if (!hazard(g, id) || !q1_alive(g, other) || !q1_alive(g, goal))
        return true;
    if (!qa_world_body_read(g->services.world, id, &self, error))
        return false;
    if (!hazard(g, id) || !q1_alive(g, other))
        return true;
    qa_vec3 velocity =
        qa_vec_scale(qa_vec_normalize(qa_vec_sub(destination.origin, self.origin)), 200);
    velocity.z = 200;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = other,
                              .time_ns = g->time_ns,
                              .origin = self.origin,
                              .value = 1,
                              .count = 1};
    event.resource=g->runtime_names[Q1_NAME_RESOURCE_MEAT_SPRAY];
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    if (!hazard(g, id) || !q1_alive(g, other))
        return true;
    body.velocity = velocity;
    return qa_world_body_write(g->services.world, other, &body, error);
}
static bool trail_fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (entity->map->kind != Q1_MAP_LTRAIL_END) {
        if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_LHIT_WAV], 2, 1, 1, error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        qa_actor_id target = {0};
        (void)qa_targets_first(g->maps->options.targets, entity->target, &target);
        if (!hazard(g, id))
            return true;
        target = goal_or_world(g, target);
        if (!q1_alive(g, target))
            return q1_map_fail(error, "Lightning trail requires worldspawn");
        qa_body_state self, destination;
        if (!qa_world_body_read(g->services.world, id, &self, error))
            return false;
        if (!hazard(g, id) || !q1_alive(g, target))
            return true;
        if (!qa_world_body_read(g->services.world, target, &destination, error))
            return false;
        if (!hazard(g, id) || !q1_alive(g, target))
            return true;
        qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                                 .family = QA_GAME_Q1,
                                 .provider = g->options.provider,
                                 .actor = id,
                                 .time_ns = g->time_ns,
                                 .origin = self.origin,
                                 .end = destination.origin,
                                 .code = 2};
        if (!qa_builtin_emit(&g->services, &beam, error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        float damage = entity->map->current_ammo;
        if (!q1_lightning_rays(g, id, id, self.origin, destination.origin, damage, damage * 4, 225,
                               qa_v3(0, 0, 100), Q1_LIGHTNING_REMEMBER_ALL | Q1_LIGHTNING_PARTICLES,
                               QA_Q1_WEAPON_COUNT, NULL, error))
            return false;
    }
    entity = hazard(g, id);
    if (!entity)
        return true;
    bool chain = entity->map->active_until < g->time;
    return q1_map_schedule(g, entity, chain ? entity->map->frags : .05,
                           chain ? Q1_MAP_LTRAIL_CHAIN : Q1_MAP_LTRAIL_FIRE, error);
}
bool q1_map_rogue_hazard_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                             qa_actor_id activator, qa_error *error) {
    qa_actor_id id = entity->id;
    if (entity->map->kind == Q1_MAP_ROGUE_QUAKE_FIELD) {
        entity->delay = entity->delay == 0 ? 1 : 0;
        return true;
    }
    if (entity->map->kind == Q1_MAP_BUZZSAW)
        return saw_start(g, entity, error);
    entity->activator = q1_ref_from(g, activator);
    if (entity->spawnflags & 1) {
        bool end = q1_classnamed(g, other, g->runtime_names[Q1_NAME_LTRAIL_END]);
        entity = hazard(g, id);
        if (!entity)
            return true;
        if (!end) {
            if (entity->spawnflags & 2) {
                entity->spawnflags -= 2;
                return true;
            }
            entity->spawnflags += 2;
        } else if (!(entity->spawnflags & 2))
            return true;
    }
    if (entity->map->kind == Q1_MAP_LTRAIL_END)
        return q1_map_schedule(g, entity, entity->map->frags, Q1_MAP_LTRAIL_CHAIN, error);
    entity->map->active_until = g->time + entity->map->weapon;
    if (!trail_fire(g, entity, error))
        return false;
    entity = hazard(g, id);
    if (entity && entity->map->kind == Q1_MAP_LTRAIL_START)
        entity->map->cooldown = g->time;
    return true;
}
bool q1_map_rogue_hazard_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                               qa_error *error) {
    qa_actor_id id = entity->id;
    if (entity->map->kind == Q1_MAP_BUZZSAW)
        return saw_touch(g, entity, other, error);
    if (entity->map->kind == Q1_MAP_ROGUE_QUAKE_FIELD) {
        if (entity->delay == 0)
            return true;
        if (entity->map->active_until < g->time) {
            if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_EQUAKE_RUMBLE_WAV], 2, 1, 1, error))
                return false;
            entity = hazard(g, id);
            if (!entity)
                return true;
            entity->map->active_until = g->time + 1;
        }
        bool player = q1_map_player(g, other);
        entity = hazard(g, id);
        return !player || !entity || q1_map_rogue_shake(g, other, entity->map->weapon, error);
    }
    if (entity->map->kind != Q1_MAP_ROGUE_QUAKE_KILL || !q1_map_player(g, other) ||
        !hazard(g, id) || !q1_alive(g, other))
        return true;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    qa_actor_id quake = {0};
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *candidate = hazard(g, snapshot->snapshot.ids[i]);
        if (candidate && candidate->map->kind == Q1_MAP_ROGUE_QUAKE) {
            quake = candidate->id;
            break;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    if (!quake.registry)
        return true;
    if (q1_alive(g, g->maps->world_actor))
        g->maps->rogue_quake_active = false;
    return qa_session_release(g->services.session, quake, error);
}
bool q1_map_rogue_hazard_think(qa_q1_game *g, q1_actor *entity, q1_map_action action,
                               qa_error *error) {
    switch (action) {
    case Q1_MAP_ROGUE_QUAKE_START:
        if (q1_alive(g, g->maps->world_actor))
            g->maps->rogue_quake_active = true;
        entity->map->active_until =
            g->time + (entity->spawnflags & 1 ? q1_random(g) * entity->delay : entity->delay);
        return quake_rumble(g, entity, error);
    case Q1_MAP_ROGUE_QUAKE_STOP:
        return quake_stop(g, entity, error);
    case Q1_MAP_ROGUE_QUAKE_RUMBLE:
        return quake_rumble(g, entity, error);
    case Q1_MAP_SAW_START:
        return saw_start(g, entity, error);
    case Q1_MAP_SAW_FLY:
    case Q1_MAP_SAW_STAND:
        return saw_frame(g, entity, action == Q1_MAP_SAW_FLY, error);
    case Q1_MAP_LTRAIL_FIRE:
        return trail_fire(g, entity, error);
    case Q1_MAP_LTRAIL_CHAIN: {
        qa_actor_id id = entity->id;
        if (!q1_map_targets(g, entity, q1_ref_actor(g, entity->activator), error))
            return false;
        entity = hazard(g, id);
        if (entity) {
            entity->think = Q1_THINK_MAP;
            entity->map->action = Q1_MAP_IDLE;
        }
        return true;
    }
    default:
        return q1_map_fail(error, "invalid Rogue hazard continuation");
    }
}
bool q1_map_rogue_hazard_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_state *state = entity->map;
    if (state->kind == Q1_MAP_ROGUE_QUAKE) {
        entity->delay = entity->delay != 0 ? entity->delay : 20;
        entity->wait = entity->wait != 0 ? entity->wait : 60;
        state->weapon = state->weapon != 0 ? state->weapon : 40;
        if (q1_alive(g, g->maps->world_actor)) {
            g->maps->rogue_quake_active = false;
            g->maps->rogue_quake_intensity = state->weapon * .5f;
        }
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!hazard(g, id))
            return true;
        body.bounds = (qa_bounds){0};
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = hazard(g, id);
        if (!entity || !q1_link(g, entity, error))
            return entity == NULL;
        entity = hazard(g, id);
        return !entity || q1_map_schedule(g, entity, 1, Q1_MAP_ROGUE_QUAKE_STOP, error);
    }
    if (state->kind == Q1_MAP_ROGUE_QUAKE_FIELD || state->kind == Q1_MAP_ROGUE_QUAKE_KILL) {
        if (state->kind == Q1_MAP_ROGUE_QUAKE_FIELD) {
            state->weapon = (state->weapon != 0 ? state->weapon : 40) * .5f;
            state->use_enabled = q1_map_text(g, entity->targetname);
            entity->delay = state->use_enabled ? 0 : 1;
        }
        state->touch_enabled = true;
        return q1_map_trigger_init(g, entity, true, error);
    }
    if (state->kind == Q1_MAP_BUZZSAW) {
        if (!q1_model(g, entity, g->runtime_names[Q1_NAME_RESOURCE_PROGS_BUZZSAW_MDL], error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        if (!q1_map_damageable(g, entity, false, error))
            return false;
        entity = hazard(g, id);
        if (!entity)
            return true;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->physics.motion = QA_PHYSICS_FLY;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!hazard(g, id))
            return true;
        float yaw = body.angles.y;
        if (yaw == 0 || yaw == 180)
            body.bounds = (qa_bounds){{-18, 0, -18}, {18, 0, 18}};
        else if (yaw == 90 || yaw == 270)
            body.bounds = (qa_bounds){{0, -18, -18}, {0, 18, 18}};
        else
            return q1_map_fail(error, "Buzzsaw: Not at 90 degree angle!");
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = hazard(g, id);
        if (!entity || !q1_link(g, entity, error))
            return entity == NULL;
        entity = hazard(g, id);
        if (!entity)
            return true;
        state = entity->map;
        entity->speed = entity->speed != 0 ? entity->speed : 10;
        state->current_ammo = state->current_ammo != 0 ? state->current_ammo : 10;
        state->cooldown = g->time + q1_random(g) * 2;
        state->use_enabled = q1_map_text(g, entity->targetname);
        return state->use_enabled || q1_map_schedule(g, entity, .2, Q1_MAP_SAW_START, error);
    }
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    entity->physics.solid = QA_PHYSICS_BOX;
    state->use_enabled = true;
    state->current_ammo = state->current_ammo != 0 ? state->current_ammo : 25;
    state->weapon = state->weapon != 0 ? state->weapon : .3f;
    state->frags = state->frags != 0 ? state->frags : .3f;
    if (state->kind == Q1_MAP_LTRAIL_START) {
        state->cooldown = g->time;
        if (entity->spawnflags & 2) {
            state->active_until = g->time + 99999999;
            return q1_map_schedule(g, entity, .1, Q1_MAP_LTRAIL_FIRE, error);
        }
    }
    return true;
}
