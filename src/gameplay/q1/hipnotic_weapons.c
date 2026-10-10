#include "internal.h"

static bool creature(qa_q1_game *g, qa_actor_id actor) {
    q1_actor *entity = q1_entity(g, actor);
    if (entity && entity->kind == Q1_MONSTER)
        return true;
    qa_builtin_actor_traits traits;
    if (g->services.actor_traits && g->services.actor_traits(g->services.context, actor, &traits))
        return traits.player || traits.monster;
    qa_q1_target target;
    return q1_target(g, actor, &target) && target.player;
}
static bool finish(qa_q1_game *g, q1_player *player, float delay, int32_t frame, float punch,
                   qa_error *error) {
    qa_q1_weapon_parameters parameters = {.interval = delay, .nail_speed = 1000};
    if (!q1_weapon_parameters(g, player->id, player->weapon, &parameters, error))
        return false;
    delay = parameters.interval;
    if (!q1_weapon_attack_delay(g, player, &delay, error))
        return false;
    player->attack_finished = g->time + delay;
    player->next_weapon_frame = player->attack_finished;
    player->weapon_frame = frame;
    player->hostile_until = g->time + 1;
    if (punch != 0)
        player->punch.x = punch;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    return q1_weapon_event(g, player, punch, (int32_t)player->weapon, error) &&
           q1_effect(g, QA_BUILTIN_MUZZLE, player->id, body.origin, 0, 0, error);
}
bool q1_hipnotic_launch_laser(qa_q1_game *g, qa_actor_id owner, qa_q1_weapon weapon, qa_vec3 origin,
                              qa_vec3 direction, bool light, qa_error *error) {
    q1_actor *laser;
    const qa_q1_weapon_view *shape = q1_weapon_shape(weapon == QA_Q1_MG3_LASER
                                                      ? weapon : QA_Q1_LASER);
    qa_vec3 velocity = qa_vec_scale(qa_vec_normalize(direction), shape->speed);
    if (!q1_projectile_spawn(g, owner, weapon, Q1_HIP_LASER, origin, velocity, &laser, error))
        return false;
    laser->effects = light ? 8 : 0;
    laser->speed = shape->speed;
    laser->physics.angular_velocity = qa_v3(0, 0, 400);
    laser->state.projectile.damage =
        light ? (weapon == QA_Q1_MG3_LASER ? 20 : 25) : shape->damage;
    laser->state.projectile.movedir = velocity;
    laser->state.projectile.expires = g->time + shape->lifetime;
    return q1_schedule(g, laser, 0, Q1_THINK_HIP_LASER, error) &&
           q1_sound(g, owner, "hipweap/laserg.wav", 1, 1, error) &&
           q1_launch_behavior(g, laser, QA_BUILTIN_BOLT, error);
}
bool q1_hipnotic_launch_proximity(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin,
                                  qa_vec3 velocity, qa_error *error) {
    q1_actor *mine;
    if (!q1_projectile_spawn(g, owner, QA_Q1_PROXIMITY, Q1_PROXIMITY, origin, velocity, &mine,
                             error))
        return false;
    mine->physics.motion = QA_PHYSICS_TOSS;
    mine->physics.angular_velocity = qa_v3(100, 600, 100);
    const qa_q1_weapon_view *shape=q1_weapon_shape(QA_Q1_PROXIMITY);
    mine->state.projectile.expires = g->time + shape->lifetime + shape->lifetime_variation * q1_random(g);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, mine->id, &body, error))
        return false;
    body.bounds = (qa_bounds){{-1, -1, -1}, {1, 1, 1}};
    return qa_combat_set_health(g->services.combat, mine->id, 5, error) &&
           qa_world_body_write(g->services.world, mine->id, &body, error) &&
           q1_link(g, mine, error) && q1_schedule(g, mine, 2, Q1_THINK_PROX_WATCH, error) &&
           q1_launch_behavior(g, mine, QA_BUILTIN_GRENADE, error);
}
bool q1_hipnotic_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    if (player->weapon == QA_Q1_LASER || player->weapon == QA_Q1_MG3_LASER) {
        if (!q1_consume(g, player->id, QA_Q1_CELLS, 1, error))
            return false;
        qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
        qa_vec3 forward = g->forward, right = g->right, up = g->up, direction;
        qa_vec3 outward = qa_vec_normalize(qa_v3(forward.x, forward.y, 0));
        qa_vec3 origin =
            qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(up, 6)), qa_vec_scale(outward, 12));
        if (!q1_aim(g, player->id, forward, &direction, error))
            return false;
        bool paired = !player->continuous || player->weapon_frame == 4;
        if (paired) {
            float offset = 6 * 0.707f;
            origin = qa_vec_sub(qa_vec_add(origin, qa_vec_scale(right, offset)),
                                qa_vec_scale(up, offset));
            if (!q1_hipnotic_launch_laser(g, player->id, player->weapon, origin, direction, false,
                                          error) ||
                !q1_hipnotic_launch_laser(g, player->id, player->weapon,
                                          qa_vec_sub(origin, qa_vec_scale(right, offset * 2)),
                                          direction, false, error))
                return false;
        } else if (!q1_hipnotic_launch_laser(g, player->id, player->weapon,
                                             qa_vec_add(origin, qa_vec_scale(up, 6)), direction,
                                             q1_random(g) < 0.1f, error))
            return false;
        player->continuous = true;
        player->animation_at = -1;
        return finish(g, player, q1_weapon_interval(player->weapon), paired ? 1 : 4, -1, error);
    }
    if (player->weapon == QA_Q1_PROXIMITY) {
        qa_vec3 velocity;
        if (!q1_consume(g, player->id, QA_Q1_ROCKETS, 1, error) ||
            !q1_grenade_velocity(g, player, &velocity, error) ||
            !q1_hipnotic_launch_proximity(g, player->id, body.origin, velocity, error) ||
            !q1_sound(g, player->id, "hipweap/proxbomb.wav", 1, 1, error))
            return false;
        player->continuous = false;
        player->animation_at = g->time;
        player->animation_base = 1;
        return finish(g, player, q1_weapon_interval(player->weapon), 1, -2, error);
    }
    q1_actor *strike;
    if (!q1_create(g, "hipnotic_hammer_strike", Q1_TIMER, player->id, &strike, error) ||
        !q1_schedule(g, strike, q1_weapon_shape(player->weapon)->launch_delay,
                      Q1_THINK_HAMMER_STRIKE, error))
        return false;
    strike->state.projectile.weapon = player->weapon;
    player->continuous = false;
    player->animation_at = g->time;
    player->animation_base = q1_ammo_count(g, player->id, QA_Q1_CELLS) < 30 ? 32 : 38;
    return finish(g, player, q1_weapon_interval(player->weapon), 1, 0, error);
}
static bool proximity_explode(qa_q1_game *g, q1_actor *mine, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, mine->id, &body, error))
        return false;
    if (!q1_radius(g, mine->id, q1_ref_actor(g, mine->state.projectile.activator),
                   q1_weapon_shape(QA_Q1_PROXIMITY)->blast_damage, (qa_actor_id){0},
                   QA_Q1_PROXIMITY, error))
        return false;
    if (!q1_alive(g, mine->id))
        return true;
    return q1_effect(g, QA_BUILTIN_EXPLOSION, mine->id, body.origin, 0, 0, error) &&
           q1_remove(g, mine, error);
}
bool q1_proximity_arm(qa_q1_game *g, q1_actor *mine, double delay, qa_error *error) {
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, mine->id, &combat, error))
        return false;
    combat.can_take_damage = false;
    mine->state.projectile.detonating = true;
    mine->owner = mine->state.projectile.activator;
    return qa_combat_set_traits(g->services.combat, mine->id, &combat, error) &&
           q1_schedule(g, mine, delay, Q1_THINK_PROX_EXPLODE, error);
}
bool q1_hipnotic_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                       const qa_touch_contact *contact, qa_error *error) {
    q1_projectile *p = &entity->state.projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (p->kind == Q1_PROXIMITY) {
        if (qa_actor_id_equal(other, entity->id) || q1_classnamed(g, other, "proximity_grenade"))
            return true;
        entity->physics.motion = QA_PHYSICS_TOSS;
        if (p->count == 1)
            return true;
        qa_body_state target_body;
        qa_q1_target target;
        bool moving = qa_world_body_read(g->services.world, other, &target_body, NULL) &&
                      qa_vec_length(target_body.velocity) > 0;
        bool aimed = q1_target(g, other, &target) && (target.player || target.aimed_damage);
        if (moving || aimed)
            return q1_proximity_arm(g, entity, 0.1, error) && proximity_explode(g, entity, error);
        if (!q1_sound(g, entity->id, "weapons/bounce.wav", 1, 1, error))
            return false;
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        p->count = 1;
        p->surface = q1_ref_from(g, other);
        body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_link(g, entity, error);
    }
    entity->owner = (q1_ref){0};
    ++p->count;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, entity, error);
    qa_vec3 old_direction = qa_vec_normalize(p->movedir);
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_sub(body.origin, qa_vec_scale(old_direction, 16)),
                  qa_vec_add(body.origin, qa_vec_scale(old_direction, 16)), entity->id, true,
                  &trace, error))
        return false;
    body.origin = trace.end;
    if (!qa_world_body_write(g->services.world, entity->id, &body, error))
        return false;
    if (q1_health(g, other) != 0) {
        if (q1_ref_equal(p->activator, q1_ref_from(g, other)))
            p->damage *= 0.5f;
        if (!q1_effect(g, QA_BUILTIN_IMPACT, other, trace.end, p->damage, 1, error) ||
            !q1_damage(g, other, entity->id, q1_ref_actor(g, p->activator), p->damage, p->weapon, error))
            return false;
    } else if (p->count == 3 || q1_random(g) < 0.15f) {
        if (!q1_effect(g, QA_BUILTIN_IMPACT, entity->id, trace.end, 0, 0, error))
            return false;
    } else {
        p->damage *= 0.9f;
        qa_vec3 normal = trace.fraction < 1              ? trace.plane.normal
                         : contact && contact->has_plane ? contact->plane.normal
                                                         : qa_v3(0, 0, 0);
        p->movedir = qa_vec_scale(
            qa_vec_normalize(qa_vec_add(old_direction, qa_vec_scale(normal, 2))), entity->speed);
        if (!q1_missile_velocity(g, entity, p->movedir, error))
            return false;
        (void)q1_random(g);
        return q1_sound(g, entity->id, "hipweap/laserric.wav", 1, 3, error);
    }
    if (!q1_alive(g, entity->id))
        return true;
    return q1_sound(g, entity->id, "enforcer/enfstop.wav", 1, 3, error) &&
           q1_remove(g, entity, error);
}
static bool proximity_watch(qa_q1_game *g, q1_actor *mine, qa_error *error) {
    size_t mines = 0;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *entity = g->actors[i];
        if (entity && entity->kind == Q1_PROJECTILE &&
            entity->state.projectile.kind == Q1_PROXIMITY && !entity->state.projectile.detonating)
            ++mines;
    }
    qa_body_state surface, body;
    bool moving =
        qa_world_body_read(g->services.world, q1_ref_actor(g, mine->state.projectile.surface), &surface, NULL) &&
        qa_vec_length(surface.velocity) > 0;
    if (g->time > mine->state.projectile.expires || mines > 15 || moving)
        return proximity_explode(g, mine, error);
    mine->owner = (q1_ref){0};
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, mine->id, &combat, error) ||
        !qa_world_body_read(g->services.world, mine->id, &body, error))
        return false;
    combat.can_take_damage = true;
    if (!qa_combat_set_traits(g->services.combat, mine->id, &combat, error) ||
        !q1_link(g, mine, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, body.origin, 140, &snapshot, error))
        return false;
    bool found = false, result = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id actor = snapshot->snapshot.ids[i - 1];
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, actor, &target, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            target.origin, qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(body.origin, center)) > 140)
            continue;
        q1_actor *native = q1_entity(g, actor);
        bool loose = native && native->kind == Q1_PROJECTILE &&
                     native->state.projectile.kind == Q1_PROXIMITY &&
                     native->state.projectile.count == 0;
        bool live = !qa_actor_id_equal(actor, mine->id) && q1_health(g, actor) > 0 &&
                    creature(g, actor) && !q1_classnamed(g, actor, "proximity_grenade");
        if (!loose && !live)
            continue;
        qa_trace_result trace;
        if (!q1_trace(g, body.origin, target.origin, mine->id, false, &trace, error)) {
            result = false;
            break;
        }
        if (trace.fraction != 1)
            continue;
        found = true;
        break;
    }
    qa_builtin_snapshot_release(snapshot);
    if (!result)
        return false;
    if (found)
        return q1_sound(g, mine->id, "hipweap/proxwarn.wav", 1, 1, error) &&
               (!q1_alive(g, mine->id) || q1_proximity_arm(g, mine, 0.5, error));
    return q1_schedule(g, mine, 0.25, Q1_THINK_PROX_WATCH, error);
}
static bool hammer_damage(qa_q1_game *g, qa_actor_id from, qa_vec3 start, qa_vec3 end, float damage,
                          qa_q1_weapon weapon, qa_error *error) {
    return q1_electric_rays(g, from, from, from, start, end, damage, damage * 4, 225,
                            qa_v3(0, 0, 100), Q1_LIGHTNING_PARTICLES |
                            Q1_LIGHTNING_REMEMBER_ALL | Q1_LIGHTNING_WETSUIT,
                            weapon, "electric", error);
}
bool q1_hipnotic_hammer_base(qa_q1_game *g, q1_player *player, qa_vec3 origin, qa_q1_weapon weapon,
                             qa_error *error) {
    q1_actor *base;
    if (!q1_create(g, "hipnotic_mjolnir_base", Q1_TIMER, player->id, &base, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    base->state.projectile.movedir = g->forward;
    base->state.projectile.weapon = weapon;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, base->id, &body, error))
        return false;
    body.origin = origin;
    if (!qa_world_body_write(g->services.world, base->id, &body, error) ||
        !q1_schedule(g, base, 1, Q1_THINK_REMOVE, error) ||
        !q1_sound(g, base->id, "hipweap/mjolslap.wav", 0, 1, error) ||
        !q1_sound(g, base->id, "hipweap/mjolhit.wav", 1, 1, error))
        return false;
    for (unsigned i = 0; i < 4; ++i) {
        q1_actor *bolt;
        if (!q1_create(g, "hipnotic_mjolnir_lightning", Q1_TIMER, base->id, &bolt, error))
            return false;
        bolt->state.projectile.activator = q1_ref_from(g, player->id);
        bolt->state.projectile.weapon = weapon;
        bolt->state.projectile.expires = g->time + 0.8;
        bolt->state.projectile.launch_angles = qa_v3(0, player->input.view_angles.y, 0);
        if (!qa_world_body_read(g->services.world, bolt->id, &body, error))
            return false;
        body.origin = origin;
        if (!qa_world_body_write(g->services.world, bolt->id, &body, error) ||
            !q1_schedule(g, bolt, 0, Q1_THINK_HAMMER_BOLT, error))
            return false;
    }
    return true;
}
static bool hammer_strike(qa_q1_game *g, q1_actor *strike, qa_error *error) {
    q1_player *player = q1_player_get(g, q1_ref_actor(g, strike->owner));
    if (!player)
        return q1_remove(g, strike, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward, up = g->up, source = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result trace;
    if (!q1_trace(g, source, qa_vec_add(source, qa_vec_scale(forward, 32)), player->id, true,
                  &trace, error))
        return false;
    float cells = (float)q1_ammo_count(g, player->id, QA_Q1_CELLS);
    float delay = .4f;
    if (!q1_weapon_attack_delay(g, player, &delay, error))
        return false;
    player->attack_finished = g->time + delay;
    if (trace.fraction == 1 && cells >= 15) {
        qa_vec3 start = qa_vec_add(source, qa_vec_scale(forward, 32));
        if (!q1_trace(g, start, qa_vec_sub(start, qa_vec_scale(up, 50)), player->id, true, &trace,
                      error))
            return false;
        if (trace.fraction > 0.3f && trace.fraction < 1) {
            if (player->input.water_level > 1) {
                if (!q1_consume(g, player->id, QA_Q1_CELLS, cells, error) ||
                    !q1_radius_typed(g, player->id, player->id, 35 * cells, (qa_actor_id){0},
                                     strike->state.projectile.weapon, "discharge", error))
                    return false;
            } else if (!q1_consume(g, player->id, QA_Q1_CELLS, 15, error) ||
                       !q1_hipnotic_hammer_base(g, player, trace.end,
                                                strike->state.projectile.weapon, error))
                return false;
            delay = 1.5f;
            if (!q1_weapon_attack_delay(g, player, &delay, error))
                return false;
            player->attack_finished = g->time + delay;
            return q1_remove(g, strike, error);
        }
    }
    qa_vec3 origin = qa_vec_sub(trace.end, qa_vec_scale(forward, 4));
    if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
        float damage = q1_classnamed(g, trace.actor, "monster_zombie") ? 70 : 50;
        if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, origin, damage, 1, error) ||
            !q1_damage(g, trace.actor, player->id, player->id, damage,
                       strike->state.projectile.weapon, error))
            return false;
    } else if (trace.fraction != 1) {
        if (!q1_sound(g, player->id, "hipweap/mjoltink.wav", 1, 1, error) ||
            !q1_effect(g, QA_BUILTIN_IMPACT, strike->id, origin, 0, 0, error))
            return false;
    } else if (!q1_sound(g, player->id, "knight/sword1.wav", 1, 1, error))
        return false;
    return !q1_alive(g, strike->id) || q1_remove(g, strike, error);
}
static bool hammer_bolt(qa_q1_game *g, q1_actor *bolt, qa_error *error) {
    q1_projectile *p = &bolt->state.projectile;
    q1_actor *base = q1_entity(g, q1_ref_actor(g, bolt->owner));
    if (g->time > p->expires || !base || !q1_alive(g, q1_ref_actor(g, p->activator)))
        return q1_remove(g, bolt, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, base->id, &body, error))
        return false;
    qa_vec3 origin = body.origin;
    uint32_t old_state = p->count;
    if (!p->count) {
        p->enemy = (q1_ref){0};
        float best = 350;
        qa_builtin_snapshot_frame *snapshot;
        if (!q1_radius_snapshot(g, origin, 350, &snapshot, error))
            return false;
        bool result = true;
        for (size_t i = snapshot->snapshot.count; i > 0; --i) {
            qa_actor_id actor = snapshot->snapshot.ids[i - 1];
            if (q1_ref_equal(q1_ref_from(g, actor), p->activator) || q1_health(g, actor) <= 0 ||
                !creature(g, actor))
                continue;
            q1_actor *native = q1_entity(g, actor);
            if (native && (native->physics.flags & QA_PHYSICS_TEAM_SLAVE))
                continue;
            if (q1_hipnotic_lightning_claimed(g, actor) ||
                !qa_world_body_read(g->services.world, actor, &body, NULL))
                continue;
            float distance = qa_vec_length(qa_vec_sub(body.origin, origin));
            if (distance >= best)
                continue;
            qa_trace_result trace;
            if (!q1_trace(g, origin, body.origin, bolt->id, false, &trace, error)) {
                result = false;
                break;
            }
            if (trace.fraction != 1 || (trace.in_open && trace.in_water))
                continue;
            best = distance;
            p->enemy = q1_ref_from(g, actor);
        }
        qa_builtin_snapshot_release(snapshot);
        if (!result)
            return false;
        if (!q1_ref_present(p->enemy)) {
            qa_builtin_angle_vectors(p->launch_angles, &g->forward, &g->right, &g->up);
            qa_vec3 end = qa_vec_add(qa_vec_add(origin, qa_vec_scale(g->forward, 200)),
                                     qa_vec_scale(g->right, 400 * q1_random(g) - 200));
            qa_trace_result trace;
            if (!q1_trace(g, origin, end, bolt->id, false, &trace, error))
                return false;
            qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                                     .family = QA_GAME_Q1,
                                     .provider = g->options.provider,
                                     .actor = bolt->id,
                                     .time_ns = g->time_ns,
                                     .origin = origin,
                                     .end = trace.end,
                                     .code = 2};
            return qa_builtin_emit(&g->services, &beam, error) &&
                   q1_schedule(g, bolt, 0.1, Q1_THINK_HAMMER_BOLT, error);
        }
        p->count = 1;
    }
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, p->enemy), &body, NULL)) {
        p->count = 0;
        return q1_schedule(g, bolt, 0.1, Q1_THINK_HAMMER_BOLT, error);
    }
    qa_vec3 end = qa_vec_add(
        qa_vec_add(body.origin, body.bounds.mins),
        qa_vec_scale(qa_vec_sub(body.bounds.maxs, body.bounds.mins), 0.25f + q1_random(g) * 0.5f));
    qa_trace_result trace;
    if (!q1_trace(g, origin, end, q1_ref_actor(g, p->activator), false, &trace, error))
        return false;
    if (trace.fraction != 1 || q1_health(g, q1_ref_actor(g, p->enemy)) <= 0) {
        p->count = 0;
        return q1_schedule(g, bolt, 0.1, Q1_THINK_HAMMER_BOLT, error);
    }
    qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                             .family = QA_GAME_Q1,
                             .provider = g->options.provider,
                             .actor = bolt->id,
                             .time_ns = g->time_ns,
                             .origin = origin,
                             .end = trace.end,
                             .code = 2};
    float damage = old_state == 0 ? 80 : 30;
    float facing = qa_vec_dot(qa_vec_normalize(qa_vec_sub(body.origin, origin)),
                              base->state.projectile.movedir);
    if (!qa_builtin_emit(&g->services, &beam, error) ||
        !hammer_damage(g, q1_ref_actor(g, p->activator), origin, trace.end, facing > 0.3f ? damage : damage * 0.5f,
                       p->weapon, error))
        return false;
    return !q1_alive(g, bolt->id) || q1_schedule(g, bolt, 0.2, Q1_THINK_HAMMER_BOLT, error);
}
bool q1_hipnotic_think(qa_q1_game *g, q1_actor *entity, q1_think_kind kind, qa_error *error) {
    switch (kind) {
    case Q1_THINK_HIP_LASER:
        if (g->time > entity->state.projectile.expires)
            return q1_remove(g, entity, error);
        if (q1_native_trajectory(g, entity->id) &&
            !q1_missile_velocity(g, entity, entity->state.projectile.movedir, error))
            return false;
        return q1_schedule(g, entity, 0.1, Q1_THINK_HIP_LASER, error);
    case Q1_THINK_PROX_WATCH:
        return proximity_watch(g, entity, error);
    case Q1_THINK_PROX_EXPLODE:
        return proximity_explode(g, entity, error);
    case Q1_THINK_HAMMER_STRIKE:
        return hammer_strike(g, entity, error);
    case Q1_THINK_HAMMER_BOLT:
        return hammer_bolt(g, entity, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, kind, "invalid Hipnotic weapon continuation");
        return false;
    }
}
