#include "boss_internal.h"
#include <float.h>
#include <stdio.h>

bool q1_meat_spray(qa_q1_game *g, q1_actor *owner, qa_vec3 origin, qa_vec3 velocity,
                   qa_error *error) {
    qa_actor_id owner_id = owner->id;
    q1_actor *spray;
    qa_body_state body;
    if (!q1_create(g, "meat_spray", Q1_GIB, owner_id, &spray, error))
        return false;
    qa_actor_id child = spray->id;
    bool ok = qa_world_body_read(g->services.world, owner_id, &body, error);
    if (!ok)
        goto cleanup;
    spray = q1_entity(g, child);
    if (!spray || !q1_alive(g, owner_id))
        goto cleanup;
    spray->physics.motion = QA_PHYSICS_BOUNCE;
    spray->physics.angular_velocity = qa_v3(3000, 1000, 2000);
    if (g->options.edition == QA_Q1_RERELEASE)
        body.angles.x = -body.angles.x;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    velocity.z += 250 + 50 * q1_random(g);
    body = (qa_body_state){.origin = origin, .velocity = velocity};
    ok = q1_model(g, spray, "progs/zom_gib.mdl", error) &&
         qa_world_body_write(g->services.world, child, &body, error);
    if (!ok)
        goto cleanup;
    spray = q1_entity(g, child);
    if (!spray || !q1_alive(g, owner_id))
        goto cleanup;
    ok = q1_schedule(g, spray, 1, Q1_THINK_REMOVE, error);
    if (!ok)
        goto cleanup;
    spray = q1_entity(g, child);
    if (!spray || !q1_alive(g, owner_id))
        goto cleanup;
    ok = q1_link(g, spray, error);
    if (ok && q1_entity(g, child) && q1_alive(g, owner_id))
        return true;
cleanup:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return ok;
}

bool q1_projectile_spawn(qa_q1_game *g, qa_actor_id owner, qa_q1_weapon weapon,
                         q1_projectile_kind kind, qa_vec3 origin, qa_vec3 velocity, q1_actor **out,
                         qa_error *error) {
    static const char *const names[] = {
        "spike",           "superspike",     "missile",           "grenade",        "wizard_spike",
        "knight_spike",    "enforcer_laser", "ogre_grenade",      "zombie_grenade", "vore_ball",
        "chthon_lavaball", "hiplaser",       "proximity_grenade", "lava_spike",     "MultiGrenade",
        "MultiRocket",     "plasma"};
    static const char *const models[] = {
        "progs/spike.mdl",    "progs/s_spike.mdl", "progs/missile.mdl",  "progs/grenade.mdl",
        "progs/w_spike.mdl",  "progs/k_spike.mdl", "progs/laser.mdl",    "progs/grenade.mdl",
        "progs/zom_gib.mdl",  "progs/v_spike.mdl", "progs/lavaball.mdl", "progs/lasrspik.mdl",
        "progs/proxbomb.mdl", "progs/lspike.mdl",  "progs/mervup.mdl",   "progs/rockup.mdl",
        "progs/plasma.mdl"};
    if ((unsigned)kind >= sizeof(names) / sizeof(*names)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 projectile kind");
        return false;
    }
    q1_actor *entity;
    if (!q1_create(g, names[kind], Q1_PROJECTILE, owner, &entity, error))
        return false;
    entity->state.projectile = (q1_projectile){.kind = kind,
                                               .weapon = weapon,
                                               .activator = q1_ref_from(g, owner),
                                               .attack = q1_attack(g, owner, entity->id, weapon)};
    entity->state.projectile.attack.projectile = entity->id;
    if (!qa_attack_next(&g->attack_sequence, &entity->state.projectile.attack, error))
        goto fail;
    bool grenade = kind == Q1_GRENADE || kind == Q1_OGRE_GRENADE || kind == Q1_ZOMBIE_GRENADE;
    entity->physics.motion = grenade ? QA_PHYSICS_BOUNCE : QA_PHYSICS_FLY_MISSILE;
    entity->physics.solid = QA_PHYSICS_BOX;
    if (grenade)
        entity->physics.angular_velocity =
            kind == Q1_ZOMBIE_GRENADE ? qa_v3(3000, 1000, 2000) : qa_v3(300, 300, 300);
    if (kind == Q1_VORE_BALL)
        entity->physics.angular_velocity = qa_v3(300, 300, 300);
    if (kind == Q1_LAVA_BALL)
        entity->physics.angular_velocity = qa_v3(200, 100, 300);
    if (kind == Q1_ENFORCER_LASER)
        entity->effects = 8;
    qa_body_state body = {
        .origin = origin,
        .velocity = velocity,
        .angles = {atan2f(velocity.z, hypotf(velocity.x, velocity.y)) * 57.29577951308232f,
                   qa_builtin_angle_mod(atan2f(velocity.y, velocity.x) * 57.29577951308232f), 0}};
    if (!q1_model(g, entity, models[kind], error) ||
        !qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !q1_link(g, entity, error))
        goto fail;
    double lifetime = grenade ? q1_weapon_shape(QA_Q1_GRENADE)->lifetime
                      : kind == Q1_ROCKET || kind == Q1_ENFORCER_LASER || kind == Q1_VORE_BALL
                          ? q1_weapon_shape(QA_Q1_ROCKET)->lifetime
                          : q1_weapon_shape(QA_Q1_NAILGUN)->lifetime;
    q1_think_kind think =
        kind == Q1_GRENADE || kind == Q1_OGRE_GRENADE ? Q1_THINK_EXPLODE : Q1_THINK_REMOVE;
    if (!q1_schedule(g, entity, lifetime, think, error))
        goto fail;
    if (kind < Q1_HIP_LASER && weapon < QA_Q1_WEAPON_COUNT) {
        qa_builtin_projectile_role role =
            grenade                                                             ? QA_BUILTIN_GRENADE
            : kind == Q1_ROCKET || kind == Q1_LAVA_BALL || kind == Q1_VORE_BALL ? QA_BUILTIN_ROCKET
            : kind == Q1_ENFORCER_LASER                                         ? QA_BUILTIN_BOLT
                                                                                : QA_BUILTIN_NAIL;
        if (!q1_launch_behavior(g, entity, role, error))
            goto fail;
    }
    *out = entity;
    return true;
fail:
    (void)q1_remove(g, entity, NULL);
    return false;
}

bool q1_sprite_prepare(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    entity->touch_disabled = true;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    entity->frame = 0;
    if (!q1_model(g, entity, "progs/s_explod.spr", error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    body.velocity = qa_v3(0, 0, 0);
    return qa_world_body_write(g->services.world, id, &body, error);
}
bool q1_sprite_explosion(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error) ||
        !q1_effect(g, QA_BUILTIN_EXPLOSION, id, body.origin, 0, 0, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    entity->kind = Q1_TIMER;
    if (!q1_sprite_prepare(g, entity, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !q1_link(g, entity, error))
        return entity == NULL;
    entity = q1_entity(g, id);
    return !entity || q1_schedule(g, entity, .1, Q1_THINK_SPRITE, error);
}
bool q1_explode(qa_q1_game *g, q1_actor *entity, qa_actor_id direct, qa_error *error) {
    q1_projectile projectile = entity->state.projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    bool rocket = projectile.kind == Q1_ROCKET || projectile.kind == Q1_LAVA_BALL;
    if (rocket && direct.registry && q1_health(g, direct) != 0) {
        float amount = (q1_weapon_shape(QA_Q1_ROCKET)->damage-10) + q1_random(g)*20;
        q1_actor *target = q1_entity(g, direct);
        if (target && target->kind == Q1_MONSTER &&
            target->state.monster.species->species == QA_Q1_SHAMBLER)
            amount *= 0.5f;
        if (!q1_damage(g, direct, entity->id, q1_ref_actor(g, entity->owner), amount, projectile.weapon, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    float radius = projectile.kind == Q1_OGRE_GRENADE || projectile.kind == Q1_VORE_BALL
                       ? 40 : q1_weapon_shape(QA_Q1_ROCKET)->blast_damage;
    if (!q1_radius(g, entity->id, q1_ref_actor(g, entity->owner), radius, direct, projectile.weapon, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (projectile.kind == Q1_OGRE_GRENADE || projectile.kind == Q1_VORE_BALL)
        return q1_sound(g, entity->id, "weapons/r_exp3.wav", 1, 1, error) &&
               q1_sprite_explosion(g, entity, error);
    qa_vec3 origin = rocket
                         ? qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8))
                         : body.origin;
    return q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, origin, 0, 0, error) &&
           q1_remove(g, entity, error);
}

bool q1_projectile_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                         const qa_touch_contact *contact, qa_error *error) {
    if (entity->state.projectile.kind == Q1_DEMODOG_GRENADE)
        return q1_demodog_grenade_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_BOSS_SPHERE_SHOT)
        return q1_boss_sphere_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_BOSS_BLAST_SHOT)
        return q1_boss_blast_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_FINAL_ROCK)
        return q1_final_rock_touch(g, entity, other, contact, error);
    if (entity->state.projectile.kind == Q1_ORB_ROCK)
        return q1_orb_rock_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_SHUB_GRENADE)
        return q1_shub_grenade_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_HEAVY_SPIKE)
        return q1_heavy_spike_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_MG3_LAVAMAN_BALL)
        return q1_lavaman_touch(g, entity, other, error);
    if (entity->state.projectile.kind >= Q1_HIP_LASER)
        return q1_expansion_touch(g, entity, other, contact, error);
    if (q1_ref_equal(q1_ref_from(g, other), entity->owner))
        return true;
    q1_actor *target = q1_entity(g, other);
    if (target && target->physics.solid == QA_PHYSICS_TRIGGER)
        return true;
    if (entity->state.projectile.remove_touch)
        return q1_remove(g, entity, error);
    q1_projectile projectile = entity->state.projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_point_query point = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &point, &contents, error))
        return false;
    bool sky = contents.contents == -6 ||
               (contact && contact->has_surface && (contact->surface.flags & 4));
    if (projectile.kind != Q1_GRENADE && projectile.kind != Q1_OGRE_GRENADE &&
        projectile.kind != Q1_ZOMBIE_GRENADE && sky)
        return q1_remove(g, entity, error);
    switch (projectile.kind) {
    case Q1_ROCKET:
    case Q1_LAVA_BALL:
        return q1_explode(g, entity, other, error);
    case Q1_GRENADE:
    case Q1_OGRE_GRENADE: {
        qa_q1_target observation;
        if (q1_target(g, other, &observation) && (observation.player || observation.aimed_damage))
            return q1_explode(g, entity, (qa_actor_id){0}, error);
        if (projectile.kind == Q1_OGRE_GRENADE && qa_vec_dot(body.velocity, body.velocity) == 0)
            entity->physics.angular_velocity = qa_v3(0, 0, 0);
        return q1_sound(g, entity->id, "weapons/bounce.wav", 1, 1, error);
    }
    case Q1_SPIKE:
    case Q1_SUPERSPIKE:
    case Q1_WIZARD_SPIKE:
    case Q1_KNIGHT_SPIKE: {
        float amount = q1_weapon_shape(projectile.kind == Q1_SUPERSPIKE
                                          ? QA_Q1_SUPER_NAILGUN : QA_Q1_NAILGUN)->damage;
        if (q1_damageable(g, other)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, other, body.origin, amount, 1, error) ||
                !q1_damage(g, other, entity->id, q1_ref_actor(g, entity->owner), amount, projectile.weapon, error))
                return false;
        } else if (!q1_effect(g, QA_BUILTIN_IMPACT, entity->id, body.origin, 0,
                              projectile.kind == Q1_WIZARD_SPIKE   ? 7
                              : projectile.kind == Q1_KNIGHT_SPIKE ? 8
                              : projectile.kind == Q1_SUPERSPIKE   ? 4
                                                                   : 3,
                              error))
            return false;
        return q1_remove(g, entity, error);
    }
    case Q1_ENFORCER_LASER: {
        qa_vec3 hit = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
        if (!q1_sound(g, entity->id, "enforcer/enfstop.wav", 1, 3, error))
            return false;
        if (q1_health(g, other) != 0) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, other, hit, 15, 1, error) ||
                !q1_damage(g, other, entity->id, q1_ref_actor(g, entity->owner), 15, projectile.weapon, error))
                return false;
        } else if (!q1_effect(g, QA_BUILTIN_IMPACT, entity->id, hit, 1, 2, error))
            return false;
        return q1_remove(g, entity, error);
    }
    case Q1_ZOMBIE_GRENADE:
        if (q1_damageable(g, other))
            return q1_damage(g, other, entity->id, q1_ref_actor(g, entity->owner),
                             q1_classnamed(g, entity->id, "mummy_grenade") ? 15 + q1_random(g) * 15
                                                                           : 10,
                             projectile.weapon, error) &&
                   q1_sound(g, entity->id, "zombie/z_hit.wav", 1, 1, error) &&
                   q1_remove(g, entity, error);
        body.velocity = qa_v3(0, 0, 0);
        entity->physics.angular_velocity = qa_v3(0, 0, 0);
        entity->state.projectile.remove_touch = true;
        return q1_sound(g, entity->id, "zombie/z_miss.wav", 1, 1, error) &&
               qa_world_body_write(g->services.world, entity->id, &body, error);
    case Q1_VORE_BALL:
        if (target && target->kind == Q1_MONSTER &&
            target->state.monster.species->species == QA_Q1_ZOMBIE &&
            !q1_damage(g, other, entity->id, entity->id, 110, QA_Q1_WEAPON_COUNT, error))
            return false;
        return q1_explode(g, entity, (qa_actor_id){0}, error);
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, projectile.kind,
                     "Q1 expansion projectile impact is unimplemented");
        return false;
    }
}
bool q1_projectile_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_think_kind think = entity->think;
    entity->think = Q1_THINK_NONE;
    if (think == Q1_THINK_SPRITE) {
        ++entity->frame;
        return entity->frame >= 6 ? q1_remove(g, entity, error)
                                  : q1_schedule(g, entity, 0.1, Q1_THINK_SPRITE, error);
    }
    q1_projectile projectile = entity->state.projectile;
    qa_body_state target, body;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, projectile.enemy), &target, NULL) ||
        q1_health(g, q1_ref_actor(g, projectile.enemy)) < 1)
        return q1_remove(g, entity, error);
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (think == Q1_THINK_VORE) {
        float speed = g->options.edition == QA_Q1_CLASSIC && g->options.skill == 3 ? 350 : 250;
        if (q1_native_trajectory(g, entity->id)) {
            body.velocity =
                qa_vec_scale(qa_vec_normalize(qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, 10)),
                                                         body.origin)),
                             speed);
            if (!qa_world_body_write(g->services.world, entity->id, &body, error))
                return false;
        }
        return q1_schedule(g, entity, 0.2, Q1_THINK_VORE, error);
    }
    if (think == Q1_THINK_WIZARD) {
        if (q1_health(g, q1_ref_actor(g, entity->owner)) > 0) {
            qa_body_state owner;
            if (qa_world_body_read(g->services.world, q1_ref_actor(g, entity->owner), &owner, NULL) &&
                !q1_effect(g, QA_BUILTIN_MUZZLE, q1_ref_actor(g, entity->owner), owner.origin, 0, 0, error))
                return false;
            qa_vec3 angles = target.angles;
            if (g->options.edition == QA_Q1_RERELEASE)
                angles.x = -angles.x;
            qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
            qa_vec3 direction = qa_vec_normalize(qa_vec_sub(
                qa_vec_sub(target.origin, qa_vec_scale(projectile.right, 13)), body.origin));
            q1_actor *missile;
            if (!q1_sound(g, entity->id, "wizard/wattack.wav", 1, 1, error) ||
                !q1_projectile_spawn(g, q1_ref_actor(g, entity->owner), QA_Q1_WEAPON_COUNT, Q1_WIZARD_SPIKE,
                                     body.origin, qa_vec_scale(direction, 600), &missile, error))
                return false;
        }
        return q1_remove(g, entity, error);
    }
    return false;
}

bool q1_gib_at(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, float health, const char *model,
               qa_error *error) {
    q1_actor *gib;
    if (!q1_create(g, "gib", Q1_GIB, owner, &gib, error))
        return false;
    qa_actor_id child = gib->id;
    bool ok = true;
    gib = q1_entity(g, child);
    if (!gib || (owner.registry && !q1_alive(g, owner))) goto cleanup;
    char path[128];
    int length = snprintf(path, sizeof(path), "progs/%s.mdl", model);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 gib model name too long");
        ok = false;
        goto cleanup;
    }
    ok = q1_model(g, gib, path, error);
    if (!ok) goto cleanup;
    gib->physics.motion = QA_PHYSICS_BOUNCE;
    double scale = health > -50 ? .7 : health > -200 ? 2 : 10;
    double x = 100 * ((double)q1_random(g) * 2 - 1);
    double y = 100 * ((double)q1_random(g) * 2 - 1);
    double z = 200 + 100 * (double)q1_random(g);
    qa_vec3 velocity = qa_v3((float)(x * scale), (float)(y * scale), (float)(z * scale));
    qa_body_state body;
    ok = qa_world_body_read(g->services.world, child, &body, error);
    if (!ok) goto cleanup;
    gib = q1_entity(g, child);
    if (!gib || (owner.registry && !q1_alive(g, owner))) goto cleanup;
    body.origin = origin;
    body.velocity = velocity;
    body.bounds = (qa_bounds){0};
    ok = qa_world_body_write(g->services.world, child, &body, error);
    if (!ok) goto cleanup;
    gib = q1_entity(g, child);
    if (!gib || (owner.registry && !q1_alive(g, owner))) goto cleanup;
    float angular_x = (float)((double)q1_random(g) * 600);
    float angular_y = (float)((double)q1_random(g) * 600);
    float angular_z = (float)((double)q1_random(g) * 600);
    gib->physics.angular_velocity = qa_v3(angular_x, angular_y, angular_z);
    ok = q1_schedule(g, gib, 10 + (double)q1_random(g) * 10, Q1_THINK_REMOVE, error);
    if (!ok) goto cleanup;
    gib = q1_entity(g, child);
    if (!gib || (owner.registry && !q1_alive(g, owner))) goto cleanup;
    ok = q1_link(g, gib, error);
    if (ok && q1_entity(g, child) && (!owner.registry || q1_alive(g, owner))) return true;
cleanup:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return ok;
}
bool q1_gib(qa_q1_game *g, q1_actor *source, const char *model, bool head, qa_error *error) {
    qa_actor_id id = source->id;
    float health = q1_health(g, id);
    source = q1_entity(g, id);
    if (!source)
        return true;
    if (head)
        return q1_gib_head(g, source, model, health, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return !q1_entity(g, id);
    return !q1_entity(g, id) || q1_gib_at(g, id, body.origin, health, model, error);
}
bool q1_gib_head(qa_q1_game *g, q1_actor *source, const char *model, float health,
                 qa_error *error) {
    qa_actor_id id = source->id;
    source = q1_entity(g, id);
    if (!source)
        return true;
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), id);
    source->kind = Q1_GIB;
    source->think = Q1_THINK_NONE;
    source->next_think = 0;
    char path[128];
    int length = snprintf(path, sizeof(path), "progs/%s.mdl", model);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 gib model name too long");
        return false;
    }
    if (!q1_model(g, source, path, error))
        return false;
    source->frame = 0;
    source->physics.motion = QA_PHYSICS_BOUNCE;
    qa_combat_state state;
    if (!qa_combat_read_traits(g->services.combat, id, &state, error))
        return false;
    source = q1_entity(g, id);
    if (!source)
        return true;
    state.can_take_damage = false;
    if (!qa_combat_set_traits(g->services.combat, id, &state, error))
        return false;
    source = q1_entity(g, id);
    if (!source)
        return true;
    source->physics.solid = QA_PHYSICS_NOT_SOLID;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_entity(g, id))
        return true;
    qa_vec3 origin = qa_v3(body.origin.x + 0.0f, body.origin.y + 0.0f, 0);
    double shifted_z = (double)body.origin.z - 24;
    if (!isfinite(shifted_z) || fabs(shifted_z) >= 0x1.ffffffp127) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 head origin exceeds finite float range");
        return false;
    }
    origin.z = fabs(shifted_z) > FLT_MAX ? -FLT_MAX : (float)shifted_z;
    double scale = health > -50 ? .7 : health > -200 ? 2 : 10;
    double x = 100 * ((double)q1_random(g) * 2 - 1);
    double y = 100 * ((double)q1_random(g) * 2 - 1);
    double z = 200 + 100 * (double)q1_random(g);
    qa_vec3 velocity = qa_v3((float)(x * scale), (float)(y * scale), (float)(z * scale));
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_entity(g, id))
        return true;
    body.origin = origin;
    body.velocity = velocity;
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    body.ground = (qa_actor_id){0};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    source = q1_entity(g, id);
    if (!source)
        return true;
    source->physics.angular_velocity = qa_v3(0, (float)(((double)q1_random(g) * 2 - 1) * 600), 0);
    return q1_link(g, source, error);
}
