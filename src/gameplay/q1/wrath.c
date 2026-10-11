#include "internal.h"

bool q1_wrath_launch(qa_q1_game *g, q1_actor *entity, unsigned attack, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, monster->enemy), &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, 10)), body.origin));
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    entity->effects |= 2;
    float forward = attack == 1 || attack == 4 ? 20 : attack == 2 ? 18 : 12;
    float up = attack == 4 ? 16 : attack == 2 ? 10 : 12;
    qa_vec3 origin = qa_vec_add(
        body.origin, qa_vec_add(qa_vec_scale(g->forward, forward),
                                qa_vec_add(qa_vec_scale(g->up, up),
                                           qa_vec_scale(g->right, attack == 3 ? 20 : 0))));
    q1_actor *shot;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_WRATH_MISSILE], Q1_PROJECTILE, entity->id, &shot, error) ||
        !q1_model(g, shot, "progs/w_ball.mdl", error))
        return false;
    shot->state.projectile =
        (q1_projectile){.kind = Q1_WRATH_MISSILE,
                        .weapon = QA_Q1_WEAPON_COUNT,
                        .enemy = monster->enemy,
                        .activator = q1_ref_from(g, entity->id),
                        .attack = q1_attack(g, entity->id, shot->id, QA_Q1_WEAPON_COUNT)};
    shot->state.projectile.attack.projectile = shot->id;
    shot->physics.motion = QA_PHYSICS_FLY_MISSILE;
    shot->physics.solid = QA_PHYSICS_BOX;
    shot->physics.angular_velocity = qa_v3(300, 300, 300);
    body = (qa_body_state){.origin = origin, .velocity = qa_vec_scale(direction, 400)};
    monster->attack_finished = g->time + 2;
    return qa_world_body_write(g->services.world, shot->id, &body, error) &&
           q1_link(g, shot, error) && q1_schedule(g, shot, 0.1, Q1_THINK_WRATH_HOME, error);
}
bool q1_wrath_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    switch (action) {
    case Q1_ACTION_WRATH_ATTACK: {
        float random = q1_random(g);
        return q1_monster_play(g, entity,
                               random < 0.25f   ? "wrath_at_a01"
                               : random < 0.65f ? "wrath_at_b01"
                                                : "wrath_at_c01",
                               error) &&
               q1_sound(g, entity->id, "wrath/watt.wav", 2, 1, error);
    }
    case Q1_ACTION_WRATHMISSILE_1:
        return q1_wrath_launch(g, entity, 1, error);
    case Q1_ACTION_WRATHMISSILE_2:
        return q1_wrath_launch(g, entity, 2, error);
    case Q1_ACTION_WRATHMISSILE_3:
        return q1_wrath_launch(g, entity, 3, error);
    case Q1_ACTION_WRATH_WRATH_DIE15: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        float health = q1_health(g, entity->id);
        const char *models[] = {"wrthgib1", "wrthgib2", "wrthgib3"};
        for (unsigned i = 0; i < 3; ++i)
            if (!q1_gib_at(g, entity->id, body.origin, health, models[i], error))
                return false;
        qa_actor_id world =
            g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
        if (!q1_radius(g, entity->id, entity->id, 80, world, QA_Q1_WEAPON_COUNT, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body.origin.z += 24;
        if (!qa_world_body_write(g->services.world, entity->id, &body, error) ||
            !q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, body.origin, 4, 0, error))
            return false;
        return q1_remove(g, entity, error);
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown wrath frame action");
        return false;
    }
}
bool q1_wrath_think(qa_q1_game *g, q1_actor *shot, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, shot->state.projectile.enemy);
    if (!q1_alive(g, enemy) || q1_health(g, enemy) < 1)
        return q1_remove(g, shot, error);
    qa_body_state body, target;
    qa_q1_target traits;
    if (!qa_world_body_read(g->services.world, enemy, &target, error) ||
        !qa_world_body_read(g->services.world, shot->id, &body, error))
        return false;
    if (q1_native_trajectory(g, shot->id)) {
        float height = q1_target(g, enemy, &traits) ? traits.view_height : 25;
        body.velocity =
            qa_vec_scale(qa_vec_normalize(qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, height)),
                                                     body.origin)),
                         g->options.skill == 3 ? 550 : 400);
        if (!qa_world_body_write(g->services.world, shot->id, &body, error))
            return false;
    }
    return q1_schedule(g, shot, 0.1, Q1_THINK_WRATH_HOME, error);
}
bool q1_wrath_touch(qa_q1_game *g, q1_actor *shot, qa_actor_id other, qa_error *error) {
    if (shot->physics.solid == QA_PHYSICS_NOT_SOLID)
        return true;
    if (q1_ref_equal(q1_ref_from(g, other), shot->owner) || q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_WRATH]) ||
        q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_SUPER_WRATH]))
        return q1_remove(g, shot, error);
    if (q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_ZOMBIE]) &&
        !q1_damage(g, other, shot->id, shot->id, 110, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    qa_actor_id world = g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
    if (!q1_radius(g, shot->id, q1_ref_actor(g, shot->owner), 20, world, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, shot->id, &body, error) ||
        !q1_sound(g, shot->id, "weapons/r_exp3.wav", 1, 1, error) ||
        !q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 0, 0, error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    body.velocity = qa_v3(0, 0, 0);
    shot->physics.solid = QA_PHYSICS_NOT_SOLID;
    shot->touch_disabled = true;
    shot->frame = 0;
    return q1_model(g, shot, "progs/s_explod.spr", error) &&
           qa_world_body_write(g->services.world, shot->id, &body, error) &&
           q1_link(g, shot, error) && q1_schedule(g, shot, 0.1, Q1_THINK_SPRITE, error);
}
