#include "boss_internal.h"

q1_ref q1_boss_enemy(const q1_actor *e) {
    return e->kind == Q1_MONSTER ? e->state.monster.enemy : e->state.boss_child.enemy;
}
qa_vec3 q1_boss_target(qa_q1_game *g, const q1_actor *e) {
    qa_body_state value;
    return qa_world_body_read(g->services.world, q1_ref_actor(g, q1_boss_enemy(e)), &value, NULL) ? value.origin
                                                                                 : qa_v3(0, 0, 0);
}
bool q1_boss_child_create(qa_q1_game *g, const char *classname, q1_boss_child_kind kind,
                          qa_actor_id owner, q1_actor **out, qa_error *error) {
    if (!q1_create(g, classname, Q1_BOSS_CHILD, owner, out, error))
        return false;
    (*out)->state.boss_child.kind = kind;
    return true;
}
bool q1_boss_child_schedule(qa_q1_game *g, q1_actor *e, q1_boss_child_kind kind, double seconds,
                            qa_error *error) {
    e->state.boss_child.kind = kind;
    return q1_schedule(g, e, seconds, Q1_THINK_BOSS_CHILD, error);
}
bool q1_boss_shot(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction,
                  qa_vec3 velocity, const char *model, q1_projectile_kind kind, q1_actor **out,
                  qa_error *error) {
    q1_actor *shot;
    if (!q1_projectile_spawn(g, owner, QA_Q1_WEAPON_COUNT, Q1_SPIKE, origin, direction, &shot,
                             error))
        return false;
    shot->state.projectile.kind = kind;
    qa_body_state body;
    if (!q1_model(g, shot, model, error) ||
        !qa_world_body_read(g->services.world, shot->id, &body, error))
        goto fail;
    body.velocity = velocity;
    body.bounds = (qa_bounds){0};
    if (!qa_world_body_write(g->services.world, shot->id, &body, error))
        goto fail;
    *out = shot;
    return true;
fail:
    q1_remove(g, shot, NULL);
    return false;
}
bool q1_boss_sphere_manager(qa_q1_game *g, q1_actor *source, int32_t maximum, bool chunk,
                            qa_error *error) {
    qa_body_state owner;
    if (!qa_world_body_read(g->services.world, source->id, &owner, error))
        return false;
    q1_actor *manager;
    if (!q1_boss_child_create(g, "", chunk ? Q1_CHILD_SPHERE_CHUNK : Q1_CHILD_SPHERE, source->id,
                              &manager, error))
        return false;
    qa_body_state body = {.origin = owner.origin};
    q1_boss_child *state = &manager->state.boss_child;
    if (chunk) {
        manager->wait = 22.5f;
        manager->delay = .8f;
        state->sign = 1;
        state->maximum = maximum;
        state->enemy = q1_boss_enemy(source);
        if (!q1_ref_present(state->enemy) && !q1_boss_first_player(g, &state->enemy, error))
            goto fail;
        body.angles = owner.angles;
    } else {
        if (!q1_ref_present(q1_boss_enemy(source))) {
            q1_ref player;
            if (!q1_boss_first_player(g, &player, error))
                goto fail;
            if (source->kind == Q1_MONSTER)
                source->state.monster.enemy = player;
            else
                source->state.boss_child.enemy = player;
        }
        qa_vec3 direction = qa_vec_sub(q1_boss_target(g, source), owner.origin);
        direction.z = 0;
        direction = qa_vec_normalize(direction);
        qa_builtin_angle_vectors(owner.angles, &g->forward, &g->right, &g->up);
        state->sign = q1_boss_flat_dot(g->right, qa_vec_cross(direction, qa_v3(0, 0, 1))) > 1;
        body.angles = q1_boss_angles(direction);
        manager->count = (float)maximum;
    }
    if (!qa_world_body_write(g->services.world, manager->id, &body, error) ||
        !q1_link(g, manager, error) || !q1_schedule(g, manager, .1, Q1_THINK_BOSS_CHILD, error))
        goto fail;
    return true;
fail:
    q1_remove(g, manager, NULL);
    return false;
}
bool q1_boss_autogun(qa_q1_game *g, q1_actor *source, qa_vec3 origin, float offset,
                     qa_error *error) {
    q1_ref player;
    qa_body_state target = {0};
    if (!q1_boss_first_player(g, &player, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, player), &target, NULL);
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target.origin, origin));
    if (offset != 0)
        direction = qa_vec_add(qa_vec_scale(qa_vec_cross(direction, qa_v3(0, 0, 1)), offset),
                               qa_vec_scale(direction, 1 - fabsf(offset)));
    q1_actor *shot;
    if (!q1_boss_shot(g, source->id, origin, direction,
                      qa_vec_scale(direction, offset == 0 ? 600 : 800), "progs/rogue/sphere.mdl",
                      Q1_BOSS_SPHERE_SHOT, &shot, error))
        return false;
    shot->effects = 64;
    return true;
}
bool q1_boss_sphere_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(e->owner, q1_ref_from(g, other)))
        return true;
    if (q1_classnamed(g, other, "oldnew_child") || q1_classnamed(g, other, "oldnew_eye") ||
        q1_classnamed(g, other, "monster_szombie"))
        return q1_remove(g, e, error);
    qa_physics_properties p;
    if (g->services.physics->services.read(g->services.physics->services.context, other, &p) &&
        p.solid == QA_PHYSICS_TRIGGER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_contents_export(contents.contents, QA_COLLISION_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, e, error);
    return (!q1_damageable(g, other) ||
            q1_damage(g, other, e->id, q1_ref_actor(g, e->owner), 18, QA_Q1_WEAPON_COUNT, error)) &&
           (!q1_alive(g, e->id) || q1_remove(g, e, error));
}
static bool actual_sphere(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!q1_ref_present(e->state.boss_child.enemy) &&
        !q1_boss_first_player(g, &e->state.boss_child.enemy, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 direction = g->forward;
    for (unsigned i = 0; i < 100; ++i) {
        if (!q1_alive(g, e->id))
            return true;
        qa_vec3 point = q1_boss_sphere_points[i];
        qa_vec3 aim =
            qa_vec_add(qa_vec_scale(direction, .65f), qa_vec_scale(qa_vec_normalize(point), .35f));
        qa_vec3 origin =
            qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 32)), qa_vec_scale(point, 32));
        q1_actor *shot;
        if (!q1_boss_shot(g, q1_ref_actor(g, e->owner), origin, aim, qa_vec_scale(aim, 800),
                          "progs/rogue/sphere.mdl", Q1_BOSS_SPHERE_SHOT, &shot, error))
            return false;
        shot->damage = 18;
        if (i % 5 == 0)
            shot->effects = 64;
    }
    if (--e->count <= 0)
        return q1_remove(g, e, error);
    if (!q1_schedule(g, e, .8, Q1_THINK_BOSS_CHILD, error))
        return false;
    body.angles.y += e->state.boss_child.sign != 0 ? 20 : -20;
    return qa_world_body_write(g->services.world, e->id, &body, error);
}
bool q1_boss_sphere_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_boss_child *state = &e->state.boss_child;
    if (state->kind == Q1_CHILD_SPHERE)
        return actual_sphere(g, e, error);
    bool chunk = state->kind == Q1_CHILD_SPHERE_CHUNK;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    qa_vec3 attack = {0}, excluded, excluded2;
    if (chunk) {
        attack = qa_vec_sub(q1_boss_target(g, e), body.origin);
        attack.z = 0;
        attack = qa_vec_normalize(attack);
        body.angles = q1_boss_angles(attack);
        if (!qa_world_body_write(g->services.world, e->id, &body, error))
            return false;
        qa_builtin_angle_vectors(qa_vec_add(body.angles, qa_v3(0, 22.5f, 0)), &g->forward,
                                 &g->right, &g->up);
        excluded = g->forward;
        qa_builtin_angle_vectors(qa_vec_add(body.angles, qa_v3(0, -22.5f, 0)), &g->forward,
                                 &g->right, &g->up);
        excluded2 = g->forward;
    } else {
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        excluded = g->forward;
        excluded2 = g->right;
    }
    if (!q1_sound(g, e->id, "weapons/spike2.wav", 1, 1, error))
        return false;
    if (!chunk && !q1_ref_present(state->enemy) && !q1_boss_first_player(g, &state->enemy, error))
        return false;
    for (int y = -1; y < (chunk ? 1 : 2); ++y) {
        for (int x = 0; x < (!chunk && y == 0 ? 44 : 45); ++x) {
            if (!q1_alive(g, e->id))
                return true;
            qa_builtin_angle_vectors(qa_vec_add(body.angles, qa_v3(0, (float)x * 8, 0)),
                                     &g->forward, &g->right, &g->up);
            float first = q1_boss_flat_dot(g->forward, excluded),
                  second = q1_boss_flat_dot(g->forward, excluded2);
            if (chunk
                    ? q1_boss_flat_dot(g->forward, attack) <= .5f || first > .985f || second > .985f
                    : fabsf(first) >= .9f || fabsf(second) >= .9f)
                continue;
            qa_vec3 origin = qa_vec_add(
                qa_vec_add(body.origin, qa_vec_scale(g->forward, 64)),
                qa_vec_add(qa_vec_scale(g->up, (float)y * 16), qa_v3(0, 0, chunk ? 8 : 24)));
            q1_actor *shot;
            if (!q1_boss_shot(g, q1_ref_actor(g, e->owner), origin, qa_vec_scale(g->forward, 200),
                              qa_vec_scale(qa_vec_normalize(g->forward), 400), "progs/diamond.mdl",
                              Q1_BOSS_SPHERE_SHOT, &shot, error))
                return false;
            if (y == (chunk ? 0 : -1) && x % 2 == 0)
                shot->effects = 64;
            shot->physics.angular_velocity = qa_v3(0, 0, -100);
        }
        body.angles.y += 4;
        if (!qa_world_body_write(g->services.world, e->id, &body, error))
            return false;
    }
    if (!chunk) {
        body.angles.y += state->sign * e->wait;
        if (!qa_world_body_write(g->services.world, e->id, &body, error) ||
            !q1_schedule(g, e, e->delay, Q1_THINK_BOSS_CHILD, error))
            return false;
    }
    if (++e->count > (float)state->maximum) {
        q1_actor *owner = q1_entity(g, q1_ref_actor(g, e->owner));
        if (!chunk && owner && owner->kind == Q1_MONSTER)
            owner->state.monster.source.boss.immune = false;
        return q1_remove(g, e, error);
    }
    return true;
}
bool q1_boss_teledeath(qa_q1_game *g, q1_actor *owner, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner->id, &body, error))
        return false;
    q1_actor *death;
    if (!q1_boss_child_create(g, "teledeath", Q1_CHILD_TELEDEATH, owner->id, &death, error))
        return false;
    death->physics.solid = QA_PHYSICS_TRIGGER;
    body.velocity = body.angles = qa_v3(0, 0, 0);
    body.ground = (qa_actor_reference){0};
    body.bounds.mins = qa_vec_sub(body.bounds.mins, qa_v3(1, 1, 1));
    body.bounds.maxs = qa_vec_add(body.bounds.maxs, qa_v3(1, 1, 1));
    if (!qa_world_body_write(g->services.world, death->id, &body, error) ||
        !q1_schedule(g, death, .2, Q1_THINK_REMOVE, error) || !q1_link(g, death, error))
        goto fail;
    if (!g->host.force_retouch) {
        qa_error_set(error, QA_ERROR_ARGUMENT, owner->id.slot,
                     "MG3 boss teledeath requires source retouch");
        goto fail;
    }
    return g->host.force_retouch(g->host.context, 2, error);
fail:
    q1_remove(g, death, NULL);
    return false;
}
bool q1_boss_teledeath_touch(qa_q1_game *g, q1_actor *death, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(death->owner, q1_ref_from(g, other)))
        return true;
    qa_q1_target traits, owner;
    bool player = q1_target(g, other, &traits) && traits.player;
    if (player || q1_classnamed(g, other, "monster_oldone_new")) {
        if (qa_q1_game_invulnerable(g, other) &&
            !qa_builtin_resource(&g->services, "teledeath2", &death->classname, error))
            return false;
        if (!q1_target(g, q1_ref_actor(g, death->owner), &owner) || !owner.player)
            return !q1_ref_present(death->owner) || q1_damage(g, q1_ref_actor(g, death->owner), death->id, death->id, 50000,
                                                       QA_Q1_WEAPON_COUNT, error);
    }
    return q1_health(g, other) == 0 ||
           q1_damage(g, other, death->id, death->id, 50000, QA_Q1_WEAPON_COUNT, error);
}
bool q1_boss_gib_vectors(qa_q1_game *g, q1_actor *source, qa_error *error) {
    qa_body_state owner;
    if (!qa_world_body_read(g->services.world, source->id, &owner, error))
        return false;
    for (unsigned i = 0; i < 100; ++i) {
        qa_vec3 point = q1_boss_sphere_points[i];
        if (point.z <= 0)
            continue;
        q1_actor *gib;
        if (!q1_create(g, "", Q1_GIB, (qa_actor_id){0}, &gib, error))
            return false;
        float choice = q1_random(g), speed = 800 + (2 * q1_random(g) - 1) * 200;
        qa_body_state body = {
            .origin =
                qa_vec_add(qa_vec_add(owner.origin, qa_vec_scale(point, 64)), qa_v3(0, 0, 48)),
            .velocity = qa_vec_scale(point, speed)};
        gib->physics.motion = QA_PHYSICS_BOUNCE;
        float x = q1_random(g) * 600, y = q1_random(g) * 600, z = q1_random(g) * 600;
        gib->physics.angular_velocity = qa_v3(x, y, z);
        if (!q1_think_deadline(g->time, 0, &gib->physics.q1_pusher.local_seconds, error) ||
            !q1_model(g, gib,
                      choice < .3f   ? "progs/gib1.mdl"
                      : choice < .6f ? "progs/gib2.mdl"
                                     : "progs/gib3.mdl",
                      error) ||
            !qa_world_body_write(g->services.world, gib->id, &body, error) ||
            !q1_schedule(g, gib, 10 + q1_random(g) * 10, Q1_THINK_REMOVE, error) ||
            !q1_link(g, gib, error)) {
            q1_remove(g, gib, NULL);
            return false;
        }
    }
    return true;
}
