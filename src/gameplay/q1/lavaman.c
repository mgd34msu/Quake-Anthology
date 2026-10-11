#include "internal.h"

bool q1_lavaman_attack(qa_q1_game *g, q1_actor *entity, bool *attacking, qa_error *error) {
    *attacking = false;
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state self, other;
    q1_monster *monster = &entity->state.monster;
    bool mg3 = g->options.program == QA_Q1_MG3;
    other = (qa_body_state){0};
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, monster->enemy), &other, NULL) && !mg3)
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &self, error))
        return false;
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 64)), other.origin, entity->id, true,
                  &trace, error))
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR || !q1_ref_equal(q1_ref_from(g, trace.actor), monster->enemy) ||
        (trace.in_open && trace.in_water) || g->time < monster->attack_finished)
        return true;
    if (!q1_monster_play(g, entity, "lavaman_fire1", error))
        return false;
    float delay = 1 + q1_random(g);
    if (mg3 || g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
        monster->attack_finished = g->time + delay;
    monster->counter = 0;
    *attacking = true;
    return true;
}
static bool hunt(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (!q1_alive(g, q1_ref_actor(g, monster->enemy)) || q1_health(g, q1_ref_actor(g, monster->enemy)) <= 0) {
        bool mg3 = g->options.program == QA_Q1_MG3;
        qa_actor_id world = g->services.physics->world_actor;
        qa_actor_id candidate = mg3 ? world : (qa_actor_id){0};
        qa_builtin_snapshot_frame *snapshot;
        if (!(mg3 ? q1_snapshot_actors(g, &snapshot, error)
                  : q1_snapshot_players(g, &snapshot, error)))
            return false;
        qa_actor_id *actors = snapshot->snapshot.ids;
        size_t count = snapshot->snapshot.count, first = 0;
        for (size_t i = 0; i < count; ++i)
            if (q1_ref_equal(q1_ref_from(g, actors[i]), monster->enemy)) {
                first = i + 1;
                break;
            }
        for (size_t i = first; i < count; ++i) {
            qa_q1_target traits;
            if (q1_target(g, actors[i], &traits) && traits.player) {
                candidate = actors[i];
                break;
            }
        }
        qa_builtin_snapshot_release(snapshot);
        qa_body_state other = {0}, self;
        bool has_body = qa_world_body_read(g->services.world, candidate, &other, NULL);
        if (mg3 || has_body) {
            if (!qa_world_body_read(g->services.world, entity->id, &self, error))
                return false;
            qa_trace_result trace;
            if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 96)), other.origin, world, false,
                          &trace, error))
                return false;
            if (trace.fraction == 1) {
                monster->enemy = q1_ref_from(g, candidate);
                entity->physics.enemy = q1_ref_from(g, candidate);
            }
        }
    }
    if (g->options.program == QA_Q1_MG3 && q1_ref_present(monster->enemy)) {
        monster->move_target = monster->enemy;
        entity->physics.goal = monster->enemy;
    }
    return !q1_ref_present(monster->enemy) || q1_monster_face(g, entity, error);
}
static bool locomotion(qa_q1_game *g, q1_actor *entity, q1_ai ai, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    bool attacking;
    if (q1_ref_present(monster->enemy)) {
        if (!q1_lavaman_attack(g, entity, &attacking, error))
            return false;
    } else if (!hunt(g, entity, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (g->options.program != QA_Q1_MG3 && ai == Q1_AI_WALK && q1_ref_present(monster->enemy)) {
        /* The source calls FindTarget even while pursuing its current enemy. */
        bool found;
        if (!q1_monster_find_target(g, entity, &found, error))
            return false;
        return !q1_alive(g, entity->id) ||
               qa_physics_q1_move_to_goal(g->services.physics, entity->id, q1_ref_actor(g, monster->enemy), 2, false,
                                          error);
    }
    return q1_monster_ai(g, entity, ai, ai == Q1_AI_STAND ? 0 : 2, error);
}
static bool fire(qa_q1_game *g, q1_actor *entity, bool first, qa_error *error) {
    bool mg3 = g->options.program == QA_Q1_MG3;
    qa_body_state body, target = {0};
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL) && !mg3)
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, 40),
                                           qa_vec_add(qa_vec_scale(g->right, first ? 65
                                                                             : mg3 ? -65
                                                                                   : -75),
                                                      qa_vec_scale(g->up, mg3     ? 90
                                                                          : first ? 130
                                                                                  : 125))));
    qa_vec3 delta = qa_vec_sub(target.origin, origin), direction = qa_vec_normalize(delta);
    float t = fmaxf(1, fminf(1.75f, qa_vec_length(delta) / 380));
    q1_actor *shot;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_LAVAMAN_BALL], Q1_PROJECTILE, entity->id, &shot, error))
        return false;
    shot->state.projectile =
        (q1_projectile){.kind = mg3 ? Q1_MG3_LAVAMAN_BALL : Q1_LAVAMAN_BALL,
                        .weapon = QA_Q1_WEAPON_COUNT,
                        .activator = q1_ref_from(g, entity->id),
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
    return q1_health(g, q1_ref_actor(g, entity->state.monster.enemy)) > 0 ||
           q1_monster_play(g, entity, "lavaman_idle1", error);
}
bool q1_lavaman_awake(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    bool mg3 = g->options.program == QA_Q1_MG3;
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.motion = mg3 ? QA_PHYSICS_FLY : QA_PHYSICS_STEP;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    if (entity->physics.yaw_speed == 0)
        entity->physics.yaw_speed = 20;
    entity->state.monster.addon.started = true;
    entity->state.monster.path_end = mg3;
    entity->state.monster.addon.normal_use = false;
    entity->aimed_damage = true;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    entity->physics.ideal_yaw = body.angles.y;
    body.bounds = entity->state.monster.species->bounds;
    float skill = g->options.skill;
    if (mg3 && g->host.cvars) {
        if (!q1_source_value(g, QA_Q1_SOURCE_SKILL, 0, &skill, error))
            return false;
    }
    float health = 1250 + 250 * skill;
    if (!mg3)
        entity->max_health = health;
    combat.can_take_damage = true;
    if (!qa_combat_set_health(g->services.combat, entity->id, health, error) ||
        !qa_combat_set_traits(g->services.combat, entity->id, &combat, error) ||
        !qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !q1_model(g, entity, "progs/lavaman.mdl", error) ||
        !q1_effect(g, QA_BUILTIN_IMPACT, entity->id,
                   qa_vec_sub(body.origin, qa_v3(0, 0, mg3 ? 50 : 0)), 0, 10, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_q1_target target;
    if (q1_target(g, activator, &target) && target.player && !target.invisible &&
        !target.notarget) {
        entity->state.monster.enemy = q1_ref_from(g, activator);
        entity->physics.enemy = q1_ref_from(g, activator);
    }
    return (mg3 || q1_monster_drop_floor(g, entity, error)) &&
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
    case Q1_ACTION_LAVAMAN_LAVAMAN_DEATH9:
    case Q1_ACTION_MG3_LAVAMAN_LAVAMAN_DEATH9: {
        qa_body_state body;
        return qa_world_body_read(g->services.world, entity->id, &body, error) &&
               q1_sound(g, entity->id, "boss1/out1.wav", 4, 1, error) &&
               q1_effect(
                   g, QA_BUILTIN_IMPACT, entity->id,
                   qa_vec_sub(body.origin, qa_v3(0, 0, g->options.program == QA_Q1_MG3 ? 50 : 0)),
                   0, 10, error);
    }
    case Q1_ACTION_LAVAMAN_LAVAMAN_DEATH10:
    case Q1_ACTION_MG3_LAVAMAN_LAVAMAN_DEATH10:
        return q1_remove(g, entity, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown lava man frame action");
        return false;
    }
}
bool q1_lavaman_touch(qa_q1_game *g, q1_actor *shot, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(q1_ref_from(g, other), shot->owner))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, shot->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, shot, error);
    if (q1_health(g, other) != 0 &&
        !q1_damage(g, other, shot->id, q1_ref_actor(g, shot->owner),
                   q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_SHAMBLER]) ? 20 : 40, QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    if (!q1_radius(g, shot->id, q1_ref_actor(g, shot->owner), 40, other, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    body.origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
    if (shot->state.projectile.kind == Q1_MG3_LAVAMAN_BALL)
        return qa_world_body_write(g->services.world, shot->id, &body, error) &&
               q1_sprite_explosion(g, shot, error);
    return qa_world_body_write(g->services.world, shot->id, &body, error) &&
           q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 0, 0, error) &&
           q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 0, 0, error) &&
           q1_remove(g, shot, error);
}

bool q1_lavaman_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (!monster->addon.started)
        return q1_lavaman_awake(g, entity, activator, error);
    if (monster->counted_death)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error) ||
        !qa_combat_set_health(g->services.combat, entity->id, 0, error) ||
        !q1_monster_death_report(g, entity, activator, true, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!g->services.use_targets && (entity->target || entity->killtarget)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "lava man targets require shared target routing");
        return false;
    }
    if (g->services.use_targets &&
        !g->services.use_targets(g->services.context, entity->id, activator, entity->target,
                                 entity->killtarget, entity->delay, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    monster->counted_death = true;
    return q1_monster_play(g, entity, "lavaman_death1", error);
}
