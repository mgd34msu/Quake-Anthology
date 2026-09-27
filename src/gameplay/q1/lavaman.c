#include "internal.h"

bool q1_lavaman_attack(qa_q1_game *g, q1_actor *entity, bool *attacking, qa_error *error) {
    *attacking = false;
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state self, other;
    q1_monster *monster = &entity->state.monster;
    if (!qa_world_body_read(g->services.world, monster->enemy, &other, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &self, error))
        return false;
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 64)), other.origin, entity->id, true,
                  &trace, error))
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, monster->enemy) ||
        (trace.in_open && trace.in_water) || g->time < monster->attack_finished)
        return true;
    if (!q1_monster_play(g, entity, "lavaman_fire1", error))
        return false;
    float delay = 1 + q1_random(g);
    if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
        monster->attack_finished = g->time + delay;
    monster->refired = false;
    *attacking = true;
    return true;
}
static bool hunt(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (!q1_alive(g, monster->enemy) || q1_health(g, monster->enemy) <= 0) {
        qa_actor_id candidate = {0};
        bool next = !q1_alive(g, monster->enemy);
        uint32_t cursor = 0;
        const qa_actor_record *record;
        while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
            qa_q1_target target;
            if (!q1_target(g, record->id, &target) || !target.player)
                continue;
            if (next) {
                candidate = record->id;
                break;
            }
            if (qa_actor_id_equal(record->id, monster->enemy))
                next = true;
        }
        qa_body_state other, self;
        if (candidate.registry && qa_world_body_read(g->services.world, candidate, &other, NULL)) {
            if (!qa_world_body_read(g->services.world, entity->id, &self, error))
                return false;
            qa_trace_result trace;
            qa_actor_id world =
                g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
            if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 96)), other.origin, world, false,
                          &trace, error))
                return false;
            if (trace.fraction == 1) {
                monster->enemy = candidate;
                entity->physics.enemy = candidate;
            }
        }
    }
    return !monster->enemy.registry || q1_monster_face(g, entity, error);
}
static bool locomotion(qa_q1_game *g, q1_actor *entity, q1_ai ai, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    bool attacking;
    if (monster->enemy.registry) {
        if (!q1_lavaman_attack(g, entity, &attacking, error))
            return false;
    } else if (!hunt(g, entity, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (ai == Q1_AI_WALK && monster->enemy.registry) {
        /* The source calls FindTarget even while pursuing its current enemy. */
        bool found;
        if (!q1_monster_find_target(g, entity, &found, error))
            return false;
        return !q1_alive(g, entity->id) ||
               qa_physics_q1_move_to_goal(g->services.physics, entity->id, monster->enemy, 2, false,
                                          error);
    }
    return q1_monster_ai(g, entity, ai, ai == Q1_AI_STAND ? 0 : 2, error);
}
static bool fire(qa_q1_game *g, q1_actor *entity, bool first, qa_error *error) {
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, entity->state.monster.enemy, &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, 40),
                                           qa_vec_add(qa_vec_scale(g->right, first ? 65 : -75),
                                                      qa_vec_scale(g->up, first ? 130 : 125))));
    qa_vec3 delta = qa_vec_sub(target.origin, origin), direction = qa_vec_normalize(delta);
    float t = fmaxf(1, fminf(1.75f, qa_vec_length(delta) / 380));
    q1_actor *shot;
    if (!q1_create(g, "lavaman_ball", Q1_PROJECTILE, entity->id, &shot, error))
        return false;
    shot->state.projectile =
        (q1_projectile){.kind = Q1_LAVAMAN_BALL,
                        .weapon = QA_Q1_WEAPON_COUNT,
                        .activator = entity->id,
                        .attack = q1_attack(g, entity->id, shot->id, QA_Q1_WEAPON_COUNT)};
    shot->state.projectile.attack.projectile = shot->id;
    shot->physics.motion = QA_PHYSICS_BOUNCE;
    shot->physics.solid = QA_PHYSICS_BOX;
    shot->physics.angular_velocity = qa_v3(200, 100, 300);
    body = (qa_body_state){.origin = origin};
    if (!qa_world_body_write(g->services.world, shot->id, &body, error) ||
        !q1_missile_velocity(g, shot, direction, error) ||
        !qa_world_body_read(g->services.world, shot->id, &body, error))
        return false;
    body.velocity = qa_vec_add(qa_vec_scale(direction, 600 * t), qa_v3(0, 0, 200 * t));
    if (!q1_model(g, shot, "progs/lavaball.mdl", error) ||
        !qa_world_body_write(g->services.world, shot->id, &body, error) ||
        !q1_schedule(g, shot, 6, Q1_THINK_REMOVE, error) || !q1_link(g, shot, error) ||
        !q1_sound(g, entity->id, "boss1/throw.wav", 1, 1, error))
        return false;
    return q1_health(g, entity->state.monster.enemy) > 0 ||
           q1_monster_play(g, entity, "lavaman_idle1", error);
}
bool q1_lavaman_awake(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    entity->physics.yaw_speed = 20;
    entity->aimed_damage = true;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    entity->physics.ideal_yaw = body.angles.y;
    body.bounds = entity->state.monster.species->bounds;
    entity->max_health = 1250 + 250 * g->options.skill;
    combat.can_take_damage = true;
    if (!qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) ||
        !qa_combat_set_traits(g->services.combat, entity->id, &combat, error) ||
        !qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !q1_model(g, entity, "progs/lavaman.mdl", error) ||
        !q1_effect(g, QA_BUILTIN_IMPACT, entity->id, body.origin, 0, 10, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_q1_target target;
    if (q1_target(g, activator, &target) && target.player && !target.invisible &&
        !target.notarget) {
        entity->state.monster.enemy = activator;
        entity->physics.enemy = activator;
    }
    return q1_monster_drop_floor(g, entity, error) &&
           q1_monster_play(g, entity, "lavaman_rise1", error);
}
bool q1_lavaman_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    switch (action) {
    case Q1_ACTION_LAVAMAN_STAND:
        return locomotion(g, entity, Q1_AI_STAND, error);
    case Q1_ACTION_LAVAMAN_WALK:
        return locomotion(g, entity, Q1_AI_WALK, error);
    case Q1_ACTION_LAVAMAN_RUN:
        return locomotion(g, entity, Q1_AI_RUN, error);
    case Q1_ACTION_LAVAMAN_MISSILE_1:
        return fire(g, entity, true, error);
    case Q1_ACTION_LAVAMAN_MISSILE_2:
        return fire(g, entity, false, error);
    case Q1_ACTION_LAVAMAN_LAVAMAN_DEATH9: {
        qa_body_state body;
        return qa_world_body_read(g->services.world, entity->id, &body, error) &&
               q1_sound(g, entity->id, "boss1/out1.wav", 4, 1, error) &&
               q1_effect(g, QA_BUILTIN_IMPACT, entity->id, body.origin, 0, 10, error);
    }
    case Q1_ACTION_LAVAMAN_LAVAMAN_DEATH10:
        return q1_remove(g, entity, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown lava man frame action");
        return false;
    }
}
bool q1_lavaman_touch(qa_q1_game *g, q1_actor *shot, qa_actor_id other, qa_error *error) {
    if (qa_actor_id_equal(other, shot->owner))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, shot->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (contents.contents == -6)
        return q1_remove(g, shot, error);
    if (q1_health(g, other) != 0 &&
        !q1_damage(g, other, shot->id, shot->owner,
                   q1_classnamed(g, other, "monster_shambler") ? 20 : 40, QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    if (!q1_radius(g, shot->id, shot->owner, 40, other, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    body.origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
    return qa_world_body_write(g->services.world, shot->id, &body, error) &&
           q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 0, 0, error) &&
           q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 0, 0, error) &&
           q1_remove(g, shot, error);
}
