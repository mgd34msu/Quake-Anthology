#include "internal.h"

static bool is_player(qa_q1_game *g, qa_actor_id actor) {
    qa_q1_target target;
    return q1_target(g, actor, &target) && target.player;
}
static bool is_monster(qa_q1_game *g, qa_actor_id actor) {
    q1_actor *entity = q1_entity(g, actor);
    if (entity && entity->kind == Q1_MONSTER)
        return true;
    qa_builtin_actor_traits traits;
    return g->services.actor_traits &&
           g->services.actor_traits(g->services.context, actor, &traits) && traits.monster;
}
static bool finish(qa_q1_game *g, q1_player *player, float delay, bool repeating, qa_error *error) {
    if (!q1_weapon_attack_delay(g, player, &delay, error))
        return false;
    player->attack_finished = g->time + delay;
    player->next_weapon_frame = g->time + (repeating ? 0.1 : delay);
    player->continuous = repeating;
    player->weapon_frame = repeating                        ? player->weapon_frame % 8 + 1
                           : player->weapon == QA_Q1_PLASMA ? 0
                                                            : 1;
    player->animation_at = repeating || player->weapon == QA_Q1_PLASMA ? -1 : g->time;
    player->animation_base = 1;
    player->hostile_until = g->time + 1;
    player->punch.x = -2;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    return q1_weapon_event(g, player, -2, (int32_t)player->weapon, error) &&
           q1_effect(g, QA_BUILTIN_MUZZLE, player->id, body.origin, 0, 0, error);
}
static bool grenade_explode(qa_q1_game *g, q1_actor *grenade, bool mini, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, grenade->id, &body, error))
        return false;
    float amount = mini ? is_player(g, q1_ref_actor(g, grenade->owner)) ? 90 : 60
                         : q1_weapon_shape(QA_Q1_MULTI_GRENADE)->blast_damage;
    if (!q1_radius(g, grenade->id, q1_ref_actor(g, grenade->owner), amount, (qa_actor_id){0}, QA_Q1_MULTI_GRENADE,
                   error))
        return false;
    return !q1_alive(g, grenade->id) ||
           (q1_effect(g, QA_BUILTIN_EXPLOSION, grenade->id, body.origin, 0, 0, error) &&
            q1_remove(g, grenade, error));
}
static bool rocket_explode(qa_q1_game *g, q1_actor *rocket, qa_actor_id direct, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, rocket->id, &body, error))
        return false;
    if (direct.registry && q1_health(g, direct) != 0) {
        float damage = (q1_weapon_shape(QA_Q1_MULTI_ROCKET)->damage-7.5f) + q1_random(g)*15;
        if (q1_classnamed(g, direct, "monster_shambler") ||
            q1_classnamed(g, direct, "monster_dragon"))
            damage *= 0.5f;
        if (!q1_damage(g, direct, rocket->id, q1_ref_actor(g, rocket->owner), damage, QA_Q1_MULTI_ROCKET, error))
            return false;
        if (!q1_alive(g, rocket->id))
            return true;
    }
    if (!q1_radius(g, rocket->id, q1_ref_actor(g, rocket->owner),
                   q1_weapon_shape(QA_Q1_MULTI_ROCKET)->blast_damage, direct, QA_Q1_MULTI_ROCKET, error))
        return false;
    if (!q1_alive(g, rocket->id))
        return true;
    qa_vec3 origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
    return q1_effect(g, QA_BUILTIN_EXPLOSION, rocket->id, origin, 0, 0, error) &&
           q1_remove(g, rocket, error);
}
static bool rocket_home(qa_q1_game *g, q1_actor *rocket, qa_error *error) {
    qa_body_state self, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, rocket->state.projectile.enemy), &target, NULL) ||
        q1_health(g, q1_ref_actor(g, rocket->state.projectile.enemy)) < 1)
        return q1_remove(g, rocket, error);
    if (!qa_world_body_read(g->services.world, rocket->id, &self, error))
        return false;
    if (q1_native_trajectory(g, rocket->id) &&
        !q1_missile_velocity(
            g, rocket, qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, self.origin)), 1000),
            error))
        return false;
    return q1_schedule(g, rocket, 0.1, Q1_THINK_MULTI_HOME, error);
}
static bool rocket_acquire(qa_q1_game *g, q1_actor *rocket, qa_error *error) {
    if (rocket->state.projectile.expires < g->time)
        return rocket_explode(g, rocket, (qa_actor_id){0}, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, rocket->id, &body, error))
        return false;
    qa_vec3 direction = qa_vec_normalize(body.velocity);
    if (q1_alive(g, q1_ref_actor(g, rocket->owner))) {
        qa_builtin_angle_vectors(rocket->state.projectile.launch_angles, &g->forward, &g->right,
                                 &g->up);
        if (!q1_aim(g, rocket->id, g->forward, &direction, error))
            return false;
    }
    qa_trace_result trace;
    if (!q1_trace(g, body.origin, qa_vec_add(body.origin, qa_vec_scale(direction, 1000)),
                  rocket->id, true, &trace, error))
        return false;
    if (trace.hit == QA_TRACE_HIT_ACTOR && is_monster(g, trace.actor)) {
        rocket->state.projectile.enemy = q1_ref_from(g, trace.actor);
        return rocket_home(g, rocket, error);
    }
    rocket->state.projectile.launch_angles = qa_v3(
        qa_builtin_angle_mod(atan2f(body.velocity.z, hypotf(body.velocity.x, body.velocity.y)) *
                             57.29577951308232f),
        qa_builtin_angle_mod(atan2f(body.velocity.y, body.velocity.x) * 57.29577951308232f), 0);
    return q1_schedule(g, rocket, 0.2, Q1_THINK_MULTI_ACQUIRE, error);
}
static bool split_grenade(qa_q1_game *g, q1_actor *grenade, qa_error *error) {
    if (!q1_ref_present(grenade->owner))
        return q1_remove(g, grenade, error);
    for (unsigned i = 0; i < 5; ++i) {
        qa_vec3 angles = grenade->state.projectile.launch_angles;
        angles.y += (float)i * 72;
        qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
        qa_vec3 velocity = qa_vec_add(qa_vec_scale(g->forward, 100), qa_vec_scale(g->up, 400));
        velocity = qa_vec_add(velocity, qa_vec_scale(g->forward, (q1_random(g) * 2 - 1) * 60 - 30));
        velocity = qa_vec_add(velocity, qa_vec_scale(g->right, (q1_random(g) * 2 - 1) * 40 - 20));
        velocity = qa_vec_add(velocity, qa_vec_scale(g->up, (q1_random(g) * 2 - 1) * 60 - 30));
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, grenade->id, &body, error))
            return false;
        q1_actor *mini;
        if (!q1_projectile_spawn(g, q1_ref_actor(g, grenade->owner), QA_Q1_MULTI_GRENADE, Q1_MULTI_GRENADE,
                                 body.origin, velocity, &mini, error))
            return false;
        if (!qa_builtin_resource(&g->services, "MiniGrenade", &mini->classname, error))
            return false;
        mini->physics.motion = QA_PHYSICS_BOUNCE;
        mini->physics.angular_velocity = qa_v3(300, 300, 300);
        mini->state.projectile.mini = true;
        mini->state.projectile.launch_angles = angles;
        if (!q1_launch_behavior(g, mini, QA_BUILTIN_GRENADE, error) ||
            !q1_schedule(g, mini, 1 + (q1_random(g) * 2 - 1) * 0.5, Q1_THINK_MINI_EXPLODE, error))
            return false;
        if (!q1_alive(g, grenade->id))
            return true;
    }
    return q1_remove(g, grenade, error);
}
static bool plasma_damage(qa_q1_game *g, q1_actor *plasma, qa_vec3 end, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, plasma->id, &body, error))
        return false;
    return q1_lightning_rays(g, q1_ref_actor(g, plasma->owner), plasma->id, body.origin, end, 50, 200, 225,
                             qa_v3(0, 0, 100), Q1_LIGHTNING_REMEMBER_ALL | Q1_LIGHTNING_PARTICLES,
                             QA_Q1_PLASMA, NULL, error);
}
static bool plasma_explode(qa_q1_game *g, q1_actor *plasma, qa_actor_id other, qa_error *error) {
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, plasma->id, &self, error))
        return false;
    float damage = (q1_weapon_shape(QA_Q1_PLASMA)->damage-10) + q1_random(g)*20;
    if (!q1_sound(g, plasma->id, "plasma/explode.wav", 1, 1, error))
        return false;
    if (q1_health(g, other) != 0) {
        if (q1_classnamed(g, other, "monster_shambler"))
            damage *= 0.5f;
        if (!q1_damage(g, other, plasma->id, q1_ref_actor(g, plasma->owner), damage, QA_Q1_PLASMA, error))
            return false;
        if (!q1_alive(g, plasma->id))
            return true;
    }
    if (!q1_radius(g, plasma->id, q1_ref_actor(g, plasma->owner),
                   q1_weapon_shape(QA_Q1_PLASMA)->blast_damage, other, QA_Q1_PLASMA, error))
        return false;
    if (!q1_alive(g, plasma->id))
        return true;
    if (!q1_effect(g, QA_BUILTIN_EXPLOSION, plasma->id, self.origin, 0, 0, error))
        return false;
    unsigned count = 0;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, self.origin, 320, &snapshot, error))
        return false;
    bool result = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id target = snapshot->snapshot.ids[i - 1];
        if (q1_ref_equal(q1_ref_from(g, target), plasma->owner) ||
            (!is_player(g, target) && !is_monster(g, target)))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, target, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(center, self.origin)) > 320)
            continue;
        qa_trace_result trace;
        if (!q1_trace(g, self.origin, body.origin, (qa_actor_id){0}, false, &trace, error)) {
            result = false;
            break;
        }
        if (trace.fraction != 1)
            continue;
        qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                                 .family = QA_GAME_Q1,
                                 .provider = g->options.provider,
                                 .actor = target,
                                 .time_ns = g->time_ns,
                                 .origin = body.origin,
                                 .end = self.origin,
                                 .code = 2};
        if (!qa_builtin_emit(&g->services, &beam, error) ||
            !q1_sound(g, plasma->id, "weapons/lhit.wav", 2, 1, error) ||
            !plasma_damage(g, plasma, body.origin, error)) {
            result = false;
            break;
        }
        if (!q1_alive(g, plasma->id))
            break;
        if (++count == 5)
            break;
    }
    qa_builtin_snapshot_release(snapshot);
    return result && (!q1_alive(g, plasma->id) || q1_remove(g, plasma, error));
}
bool q1_rogue_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    q1_projectile *p = &entity->state.projectile;
    if (q1_ref_equal(entity->owner, q1_ref_from(g, other)))
        return true;
    q1_actor *native = q1_entity(g, other);
    if (p->kind == Q1_LAVA_SPIKE && native && native->physics.solid == QA_PHYSICS_TRIGGER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (p->kind == Q1_MULTI_GRENADE) {
        qa_q1_target target;
        if (q1_target(g, other, &target) && (target.player || target.aimed_damage))
            return grenade_explode(g, entity, p->mini || !is_player(g, q1_ref_actor(g, entity->owner)), error);
        if (!q1_sound(g, entity->id, "weapons/bounce.wav", 1, 1, error))
            return false;
        if (qa_vec_length(body.velocity) == 0)
            entity->physics.angular_velocity = qa_v3(0, 0, 0);
        return true;
    }
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_COLLISION_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, entity, error);
    if (p->kind == Q1_MULTI_ROCKET)
        return rocket_explode(g, entity, other, error);
    if (p->kind == Q1_PLASMA)
        return plasma_explode(g, entity, other, error);
    if (p->kind == Q1_LAVA_SPIKE) {
        bool powered = p->count == 1;
        if (q1_damageable(g, other)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, other, body.origin, powered ? 18 : 9, 1, error))
                return false;
            if (!q1_classnamed(g, other, "monster_lava_man")) {
                bool player = is_player(g, other);
                float damage = player ? powered ? 18 : 9 : powered ? 30 : 15;
                qa_string_id cause;
                if (!qa_builtin_resource(&g->services, powered ? "rogue:super-lava" : "rogue:lava",
                                         &cause, error) ||
                    !q1_damage_typed(g, other, entity->id, q1_ref_actor(g, entity->owner), damage, p->weapon,
                                     player ? powered ? QA_Q1_ARMOR_HALF : QA_Q1_ARMOR_BYPASS
                                            : QA_Q1_ARMOR_NORMAL,
                                     cause, error))
                    return false;
            }
        } else if (!q1_effect(g, QA_BUILTIN_IMPACT, entity->id, body.origin, 0, powered ? 4 : 3,
                              error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    qa_error_set(error, QA_ERROR_FORMAT, p->kind, "invalid Rogue projectile impact");
    return false;
}
bool q1_rogue_launch_plasma(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction,
                            q1_actor **out, qa_error *error) {
    q1_actor *projectile;
    if (!q1_projectile_spawn(g, owner, QA_Q1_PLASMA, Q1_PLASMA, origin,
                             qa_vec_scale(direction, 0.01f), &projectile, error))
        return false;
    projectile->physics.angular_velocity = qa_v3(300, 300, 300);
    if (!g->options.coop && !g->options.deathmatch)
        projectile->effects = 4;
    if (!q1_sound(g, projectile->id, "plasma/flight.wav", 1, 1, error) ||
        !q1_schedule(g, projectile, q1_weapon_shape(QA_Q1_PLASMA)->launch_delay,
                      Q1_THINK_PLASMA_LAUNCH, error) ||
        !q1_launch_behavior(g, projectile, QA_BUILTIN_PLASMA, error))
        return false;
    if (out)
        *out = projectile;
    return true;
}
bool q1_rogue_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 direction;
    q1_actor *projectile;
    switch (player->weapon) {
    case QA_Q1_LAVA_NAILGUN:
    case QA_Q1_LAVA_SUPER_NAILGUN: {
        bool powered = player->weapon == QA_Q1_LAVA_SUPER_NAILGUN &&
                       q1_ammo_count(g, player->id, QA_Q1_LAVA_NAILS) >= 2;
        if (!q1_consume(g, player->id, QA_Q1_LAVA_NAILS, powered ? 2 : 1, error))
            return false;
        qa_vec3 origin =
            qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 16)),
                       qa_vec_scale(g->right, powered ? 0 : (float)player->nail_side * 4));
        if (!q1_sound(g, player->id, powered ? "weapons/spike2.wav" : "weapons/rocket1i.wav", 1, 1,
                      error) ||
            !q1_aim(g, player->id, g->forward, &direction, error) ||
            !q1_projectile_spawn(
                g, player->id, powered ? QA_Q1_LAVA_SUPER_NAILGUN : QA_Q1_LAVA_NAILGUN,
                Q1_LAVA_SPIKE, origin, qa_vec_scale(direction,q1_weapon_shape(player->weapon)->speed),
                &projectile, error))
            return false;
        projectile->state.projectile.count = powered ? 1 : 0;
        if (!q1_launch_behavior(g, projectile, QA_BUILTIN_NAIL, error))
            return false;
        player->nail_side = -player->nail_side;
        return finish(g, player, q1_weapon_interval(player->weapon), true, error);
    }
    case QA_Q1_MULTI_GRENADE: {
        qa_vec3 velocity;
        if (!q1_consume(g, player->id, QA_Q1_MULTI_ROCKETS, 1, error) ||
            !q1_grenade_velocity(g, player, &velocity, error) ||
            !q1_projectile_spawn(g, player->id, QA_Q1_MULTI_GRENADE, Q1_MULTI_GRENADE, body.origin,
                                 velocity, &projectile, error))
            return false;
        projectile->physics.motion = QA_PHYSICS_BOUNCE;
        projectile->physics.angular_velocity = qa_v3(300, 300, 300);
        if (!q1_schedule(g, projectile, q1_weapon_shape(QA_Q1_MULTI_GRENADE)->lifetime, Q1_THINK_MULTI_SPLIT, error) ||
            !q1_launch_behavior(g, projectile, QA_BUILTIN_GRENADE, error) ||
            !q1_sound(g, player->id, "weapons/grenade.wav", 1, 1, error))
            return false;
        return finish(g, player, q1_weapon_interval(player->weapon), false, error);
    }
    case QA_Q1_MULTI_ROCKET: {
        if (!q1_consume(g, player->id, QA_Q1_MULTI_ROCKETS, 1, error))
            return false;
        bool multiplayer = g->options.coop || g->options.deathmatch;
        const qa_q1_weapon_view *shape=q1_weapon_shape(QA_Q1_MULTI_ROCKET);
        static const float offsets[] = {-10, -5, 5, 10};
        static const int frames[] = {2, 3, 0, 1};
        for (unsigned i = 0; i < 4; ++i) {
            qa_vec3 origin =
                qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(g->forward, 8)), qa_v3(0, 0, 16));
            qa_vec3 angles = player->input.view_angles;
            if (multiplayer)
                angles.y += offsets[i] * 0.66f;
            qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
            qa_vec3 velocity;
            if (multiplayer) {
                if (!q1_aim(g, player->id, g->forward, &direction, error))
                    return false;
                velocity = qa_vec_scale(direction, shape->speed);
            } else
                velocity = qa_vec_sub(qa_vec_scale(g->forward, shape->speed),
                                      qa_vec_scale(g->right, offsets[i] * 8));
            if (!q1_projectile_spawn(g, player->id, QA_Q1_MULTI_ROCKET, Q1_MULTI_ROCKET, origin,
                                     velocity, &projectile, error))
                return false;
            projectile->frame = frames[i];
            projectile->state.projectile.expires = g->time + shape->lifetime;
            projectile->state.projectile.launch_angles = player->input.view_angles;
            if (multiplayer && !q1_model(g, projectile, "progs/rockup_d.mdl", error))
                return false;
            if (!q1_launch_behavior(g, projectile, QA_BUILTIN_ROCKET, error))
                return false;
            if (!q1_alive(g, projectile->id))
                continue;
            if (multiplayer) {
                if (!q1_schedule(g, projectile, shape->lifetime, Q1_THINK_MULTI_EXPLODE, error))
                    return false;
            } else {
                qa_trace_result trace;
                if (!q1_trace(g, origin, qa_vec_add(origin, velocity), player->id, true, &trace,
                              error))
                    return false;
                if (trace.hit == QA_TRACE_HIT_ACTOR && is_monster(g, trace.actor)) {
                    projectile->state.projectile.enemy = q1_ref_from(g, trace.actor);
                    qa_scheduler_cancel(qa_session_scheduler(g->services.session), projectile->id);
                    projectile->think = Q1_THINK_MULTI_HOME;
                    projectile->next_think = 0;
                } else if (!q1_schedule(g, projectile, 0.1, Q1_THINK_MULTI_ACQUIRE, error))
                    return false;
            }
        }
        return q1_sound(g, player->id, "weapons/sgun1.wav", 1, 1, error) &&
               finish(g, player, q1_weapon_interval(player->weapon), false, error);
    }
    case QA_Q1_PLASMA: {
        float ammo = (float)q1_ammo_count(g, player->id, QA_Q1_PLASMA_CELLS);
        if (player->input.water_level > 1) {
            if (!q1_consume(g, player->id, QA_Q1_PLASMA_CELLS, ammo, error) ||
                !q1_radius(g, player->id, player->id, 35 * ammo, (qa_actor_id){0}, QA_Q1_PLASMA,
                           error))
                return false;
        } else {
            if (!q1_consume(g, player->id, QA_Q1_PLASMA_CELLS, 1, error))
                return false;
            qa_vec3 origin =
                qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(g->forward, 24)), qa_v3(0, 0, 16));
            if (!q1_aim(g, player->id, g->forward, &direction, error) ||
                !q1_rogue_launch_plasma(g, player->id, origin, direction, &projectile, error))
                return false;
            qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = player->id,
                                      .time_ns = g->time_ns,
                                      .channel = 1,
                                      .volume = 0.5f,
                                      .attenuation = 1};
            if (!qa_builtin_resource(&g->services, "plasma/fire.wav", &sound.resource, error) ||
                !qa_builtin_emit(&g->services, &sound, error))
                return false;
        }
        return finish(g, player, q1_weapon_interval(player->weapon), false, error);
    }
    default:
        qa_error_set(error, QA_ERROR_ARGUMENT, player->weapon, "invalid Rogue arsenal weapon");
        return false;
    }
}
bool q1_rogue_think(qa_q1_game *g, q1_actor *entity, q1_think_kind kind, qa_error *error) {
    switch (kind) {
    case Q1_THINK_MULTI_SPLIT:
        return split_grenade(g, entity, error);
    case Q1_THINK_MINI_EXPLODE:
        return grenade_explode(g, entity, true, error);
    case Q1_THINK_MULTI_EXPLODE:
        return rocket_explode(g, entity, (qa_actor_id){0}, error);
    case Q1_THINK_MULTI_ACQUIRE:
        return rocket_acquire(g, entity, error);
    case Q1_THINK_MULTI_HOME:
        return rocket_home(g, entity, error);
    case Q1_THINK_PLASMA_LAUNCH: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        if (q1_native_trajectory(g, entity->id) &&
            !q1_missile_velocity(g, entity, qa_vec_scale(qa_vec_normalize(body.velocity),
                                                        q1_weapon_shape(QA_Q1_PLASMA)->speed),
                                 error))
            return false;
        return q1_schedule(g, entity, 5, Q1_THINK_REMOVE, error);
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, kind, "invalid Rogue weapon continuation");
        return false;
    }
}
