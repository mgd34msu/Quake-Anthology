#include "internal.h"

bool q1_demodog_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    if (action == Q1_ACTION_DEMODOG_BITE) {
        if (!q1_ref_present(entity->state.monster.enemy))
            return true;
        return q1_monster_ai(g, entity, Q1_AI_CHARGE, 10, error) &&
               (!q1_alive(g, entity->id) || q1_monster_melee(g, entity, 100, 8, 3, true, error));
    }
    if (action != Q1_ACTION_DEMODOG_JUMP) {
        qa_error_set(error, QA_ERROR_FORMAT, action, "invalid demodog continuation");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    entity->state.monster.jump_touch = true;
    entity->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
    body.origin.z += 1;
    body.velocity = qa_vec_add(qa_vec_scale(g->forward, 300), qa_v3(0, 0, 200));
    return qa_world_body_write(g->services.world, entity->id, &body, error);
}

bool q1_demodog_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (!m->jump_touch || q1_health(g, entity->id) <= 0)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (q1_damageable(g, other) && m->attack_finished < g->time &&
        qa_vec_length(body.velocity) > 300) {
        if (!q1_damage(g, other, entity->id, entity->id, 10 + 10 * q1_random(g), QA_Q1_WEAPON_COUNT,
                       error))
            return false;
        if (!q1_alive(g, entity->id) || entity->kind != Q1_MONSTER)
            return true;
        qa_q1_target target;
        if (q1_target(g, other, &target) && target.player)
            return q1_damage(g, entity->id, other, other, 200, QA_Q1_WEAPON_COUNT, error);
        m->attack_finished = g->time + 0.5;
    }
    bool grounded;
    if (!qa_physics_check_bottom(g->services.physics, entity->id, body.origin, &grounded, error))
        return false;
    if (!grounded) {
        if (!(entity->physics.flags & QA_PHYSICS_ONGROUND))
            return true;
        m->jump_touch = false;
        m->next_frame = q1_frame_index("demodog_leap1");
    } else {
        entity->wait = 0;
        m->jump_touch = false;
        m->next_frame = q1_frame_index(m->species->run);
    }
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}

static bool grenade(qa_q1_game *g, q1_actor *entity, bool vertical, qa_error *error) {
    float x = 100 * (2 * q1_random(g) - 1), y = 100 * (2 * q1_random(g) - 1),
          z = 200 + 100 * q1_random(g);
    qa_vec3 velocity = qa_vec_scale(qa_v3(x, y, z), 1.5f);
    if (vertical)
        velocity.x = velocity.y = 0;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    velocity = qa_vec_add(velocity, qa_vec_scale(g->forward, 100));
    if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_GRENADE_WAV], 1, 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    q1_actor *shot;
    if (!q1_create(g, g->runtime_names[Q1_NAME_GRENADE], Q1_PROJECTILE, entity->id, &shot, error))
        return false;
    shot->state.projectile =
        (q1_projectile){.kind = Q1_DEMODOG_GRENADE,
                        .weapon = QA_Q1_WEAPON_COUNT,
                        .damage = 60,
                        .movedir = velocity,
                        .attack = q1_attack(g, entity->id, shot->id, QA_Q1_WEAPON_COUNT)};
    shot->state.projectile.attack.projectile = shot->id;
    shot->physics.motion = QA_PHYSICS_BOUNCE;
    shot->physics.solid = QA_PHYSICS_BOX;
    shot->physics.angular_velocity = qa_v3(300, 300, 300);
    qa_body_state launch = {
        .origin = body.origin,
        .velocity = velocity,
        .angles = {atan2f(velocity.z, hypotf(velocity.x, velocity.y)) * 57.29577951308232f,
                   qa_builtin_angle_mod(atan2f(velocity.y, velocity.x) * 57.29577951308232f), 0}};
    if (!q1_model(g, shot, "progs/grenade.mdl", error) ||
        !qa_world_body_write(g->services.world, shot->id, &launch, error) ||
        !q1_link(g, shot, error) ||
        !q1_schedule(g, shot, 2.5 + 0.25 * (2 * q1_random(g) - 1), Q1_THINK_DEMODOG_EXPLODE,
                     error)) {
        q1_remove(g, shot, NULL);
        return false;
    }
    return true;
}
bool q1_demodog_die(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!qa_combat_set_health(g->services.combat, entity->id, -50, error))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!grenade(g, entity, false, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    if (g->options.skill > 2 && q1_random(g) > 0.7f && !grenade(g, entity, true, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_UDEATH_WAV], 2, 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!q1_gib_at(g, entity->id, body.origin, -50, "gib3", error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    return q1_gib_head(g, entity, "h_dog", -50, error);
}
bool q1_demodog_explode(qa_q1_game *g, q1_actor *entity, qa_actor_id ignore, qa_error *error) {
    if (!q1_radius(g, entity->id, q1_ref_actor(g, entity->owner), entity->state.projectile.damage, ignore,
                   QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, body.origin, 0, 0, error))
        return false;
    return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
}
bool q1_demodog_grenade_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(entity->owner, q1_ref_from(g, other)))
        return true;
    qa_q1_target target;
    if (q1_target(g, other, &target) && (target.aimed_damage || target.player)) {
        if (q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_BOSS]) ||
            q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_OLDONE_NEW])) {
            if (!q1_damage(g, other, entity->id, q1_ref_actor(g, entity->owner), entity->state.projectile.damage,
                           QA_Q1_WEAPON_COUNT, error))
                return false;
            return !q1_alive(g, entity->id) || q1_demodog_explode(g, entity, other, error);
        }
        return q1_demodog_explode(g, entity, (qa_actor_id){0}, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (qa_vec_length(body.velocity) == 0)
        entity->physics.angular_velocity = qa_v3(0, 0, 0);
    if (entity->state.projectile.expires < g->time &&
        !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_BOUNCE_WAV], 1, 1, 1, error))
        return false;
    entity->state.projectile.expires = g->time + 0.1;
    return true;
}
