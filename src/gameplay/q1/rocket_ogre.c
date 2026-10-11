#include "internal.h"
#include <stdio.h>

bool q1_rocket_ogre_override(const char *name) {
    static const char *const names[] = {"ogre_stand5", "ogre_walk3", "ogre_run1", "ogre_nail1",
                                        "ogre_nail4",  "ogre_nail5", "ogre_die3", "ogre_bdie3"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}
static bool fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    qa_body_state self, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, monster->enemy), &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &self, error))
        return false;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(self.angles, &forward, &right, &up);
    float offset = (float)(monster->addon.projectile_max - monster->addon.projectiles) *
                   (q1_random(g) > 0.5f ? -64 : 64);
    qa_vec3 lead = qa_vec_scale(qa_v3(target.velocity.x, target.velocity.y, 0),
                                qa_vec_length(qa_vec_sub(target.origin, self.origin)) / 1200);
    qa_vec3 direction =
        qa_vec_add(qa_vec_sub(qa_vec_add(qa_vec_add(target.origin, qa_v3(0, 0, -8)), lead),
                              qa_vec_add(self.origin, qa_v3(0, 0, 16))),
                   qa_vec_scale(right, offset));
    entity->effects |= 2;
    if (!q1_sound(g, entity->id, "weapons/sgun1.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    q1_actor *missile;
    if (!q1_projectile_spawn(
            g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ROCKET,
            qa_vec_add(qa_vec_add(self.origin, qa_vec_scale(forward, 8)), qa_v3(0, 0, 16)),
            qa_vec_scale(qa_vec_normalize(direction), 600), &missile, error))
        return false;
    missile->state.projectile.kind = Q1_MG3_OGRE_ROCKET;
    missile->state.projectile.expires = g->time + 0.2;
    return qa_builtin_resource(&g->services, "ogre_missile", &missile->classname, error);
}
bool q1_rocket_ogre_frame(qa_q1_game *g, q1_actor *entity, const char *name, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (!strcmp(name, "ogre_nail1") || !strcmp(name, "ogre_nail5"))
        monster->addon.projectiles = monster->addon.projectile_max;
    if (!strcmp(name, "ogre_nail4")) {
        if (!fire(g, entity, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (monster->addon.projectiles)
            --monster->addon.projectiles;
        if (monster->addon.projectile_max && monster->addon.projectiles)
            monster->next_frame = q1_frame_index("ogre_nail2");
    }
    if ((!strcmp(name, "ogre_stand5") || !strcmp(name, "ogre_walk3") ||
         !strcmp(name, "ogre_run1")) &&
        q1_random(g) < 0.2f) {
        unsigned index = (!strcmp(name, "ogre_walk3") ? 3u : 1u) + (q1_random(g) < 0.5f ? 0u : 1u);
        char sound[32];
        snprintf(sound, sizeof(sound), "armagon/idle%u.wav", index);
        return q1_sound(g, entity->id, sound, 2, 2, error);
    }
    return true;
}
bool q1_rocket_ogre_touch(qa_q1_game *g, q1_actor *missile, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(missile->owner, q1_ref_from(g, other)))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, missile->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, missile, error);
    if (missile->state.projectile.expires > g->time) {
        if (!q1_effect(g, QA_BUILTIN_IMPACT, other, body.origin, 18, 1, error) ||
            !q1_damage(g, other, missile->id, q1_ref_actor(g, missile->owner), 20, QA_Q1_WEAPON_COUNT, error))
            return false;
        if (!q1_alive(g, missile->id))
            return true;
        return q1_effect(g, QA_BUILTIN_IMPACT, missile->id, body.origin, 0, 8, error) &&
               q1_remove(g, missile, error);
    }
    if (q1_health(g, other) != 0) {
        float damage = q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_SHAMBLER]) ? 20
                       : q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_ZOMBIE]) ? 60
                                                                   : 40;
        if (!q1_damage(g, other, missile->id, q1_ref_actor(g, missile->owner), damage, QA_Q1_WEAPON_COUNT, error))
            return false;
        if (!q1_alive(g, missile->id))
            return true;
    }
    if (!q1_radius(g, missile->id, q1_ref_actor(g, missile->owner), 40, other, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, missile->id))
        return true;
    body.origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
    return qa_world_body_write(g->services.world, missile->id, &body, error) &&
           q1_sprite_explosion(g, missile, error);
}
