#include "internal.h"

bool q1_launch_behavior(qa_q1_game *g, q1_actor *entity, qa_builtin_projectile_role role,
                        qa_error *error) {
    qa_q1_target shooter;
    if (!q1_target(g, q1_ref_actor(g, entity->state.projectile.activator), &shooter) || !shooter.player)
        return true;
    qa_builtin_weapon_launch launch = {.projectile = entity->id,
                                       .shooter = q1_ref_actor(g, entity->state.projectile.activator),
                                       .weapon = entity->state.projectile.attack.weapon,
                                       .provider = g->options.provider,
                                       .role = role,
                                       .time_ns = g->time_ns};
    if (!qa_world_body_read(g->services.world, entity->id, &launch.body, error))
        return false;
    bool changed;
    if (!qa_builtin_launch_projectile(&g->services, &launch, &changed, error))
        return false;
    if (!changed || !q1_alive(g, entity->id))
        return true;
    if (entity->state.projectile.kind == Q1_HIP_LASER) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        entity->state.projectile.movedir = body.velocity;
        entity->speed = qa_vec_length(body.velocity);
    }
    return q1_link(g, entity, error);
}
bool q1_native_trajectory(qa_q1_game *g, qa_actor_id actor) {
    return !g->services.controls_trajectory ||
           !g->services.controls_trajectory(g->services.context, actor);
}
bool q1_missile_velocity(qa_q1_game *g, q1_actor *entity, qa_vec3 velocity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    body.velocity = velocity;
    body.angles =
        qa_v3(velocity.x == 0 && velocity.y == 0
                  ? (velocity.z > 0 ? 90 : 270)
                  : qa_builtin_angle_mod(atan2f(velocity.z, hypotf(velocity.x, velocity.y)) *
                                         57.29577951308232f),
              qa_builtin_angle_mod(atan2f(velocity.y, velocity.x) * 57.29577951308232f), 0);
    body.ground = (qa_actor_reference){0};
    return qa_world_body_write(g->services.world, entity->id, &body, error);
}
bool q1_grenade_velocity(qa_q1_game *g, q1_player *player, qa_vec3 *out, qa_error *error) {
    const qa_q1_weapon_view *shape = q1_weapon_shape(QA_Q1_GRENADE);
    qa_vec3 direction;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    if (!q1_aim(g, player->id, g->forward, &direction, error))
        return false;
    if (player->input.view_angles.x == 0) {
        *out = q1_grenade_launch_velocity(shape,true,direction,g->forward,g->right,g->up,0,0);
        return true;
    }
    float x = (q1_random(g) * 2 - 1) * shape->velocity_spread;
    float y = (q1_random(g) * 2 - 1) * shape->velocity_spread;
    *out = q1_grenade_launch_velocity(shape,false,direction,g->forward,g->right,g->up,x,y);
    return true;
}
bool q1_expansion_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    if (player->weapon == QA_Q1_ROGUE_GRAPPLE || player->weapon == QA_Q1_CTF_GRAPPLE)
        return qa_q1_grapple_fire(g, player->id, player->weapon == QA_Q1_CTF_GRAPPLE,
                                  &player->input, error);
    if (player->weapon == QA_Q1_MG3_LASER)
        return q1_hipnotic_fire(g, player, error);
    if (player->weapon == QA_Q1_MG3_MJOLNIR)
        return q1_mg3_hammer_fire(g, player, error);
    if (player->weapon >= QA_Q1_LASER && player->weapon <= QA_Q1_PROXIMITY)
        return q1_hipnotic_fire(g, player, error);
    if (player->weapon >= QA_Q1_LAVA_NAILGUN && player->weapon <= QA_Q1_PLASMA)
        return q1_rogue_fire(g, player, error);
    qa_error_set(error, QA_ERROR_UNSUPPORTED, player->weapon,
                 "Q1 addon arsenal controller not admitted");
    return false;
}
bool q1_expansion_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                        const qa_touch_contact *contact, qa_error *error) {
    if (entity->state.projectile.kind == Q1_VENGEANCE)
        return q1_sphere_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_ROGUE_HOOK ||
        entity->state.projectile.kind == Q1_CTF_HOOK)
        return q1_grapple_touch(g, entity, other, contact, error);
    if (entity->state.projectile.kind == Q1_WRATH_MISSILE)
        return q1_wrath_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_DRAGON_FIREBALL)
        return q1_dragon_fireball_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_MG3_OGRE_ROCKET)
        return q1_rocket_ogre_touch(g, entity, other, error);
    if (entity->state.projectile.kind == Q1_LAVAMAN_BALL)
        return q1_lavaman_touch(g, entity, other, error);
    return entity->state.projectile.kind == Q1_HIP_LASER ||
                   entity->state.projectile.kind == Q1_PROXIMITY
               ? q1_hipnotic_touch(g, entity, other, contact, error)
               : q1_rogue_touch(g, entity, other, error);
}
bool q1_expansion_think(qa_q1_game *g, q1_actor *entity, q1_think_kind kind, qa_error *error) {
    if (kind >= Q1_THINK_HIP_LASER && kind <= Q1_THINK_HAMMER_BOLT)
        return q1_hipnotic_think(g, entity, kind, error);
    return q1_rogue_think(g, entity, kind, error);
}
