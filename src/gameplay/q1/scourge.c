#include "internal.h"

static bool locomotion_sound(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (!monster->source.scourge.initialized) {
        q1_actor *trigger;
        if (!q1_create(g, g->runtime_names[Q1_NAME_SCOURGE_TRIGGER], Q1_TIMER, (qa_actor_id){0}, &trigger, error))
            return false;
        trigger->activator = q1_ref_from(g, entity->id);
        monster->source.scourge.trigger = q1_ref_from(g, trigger->id);
        monster->source.scourge.initialized = true;
        trigger->physics.solid = QA_PHYSICS_TRIGGER;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body = (qa_body_state){.origin = body.origin, .bounds = {{-64, -64, -24}, {64, 64, 64}}};
        if (!qa_world_body_write(g->services.world, trigger->id, &body, error) ||
            !q1_schedule(g, trigger, 0.1 + q1_random(g), Q1_THINK_SCOURGE_TRIGGER, error))
            return false;
    }
    bool silent = monster->source.scourge.silent,
         previous = monster->source.scourge.previous_silent;
    monster->source.scourge.previous_silent = silent;
    if (!silent && previous)
        return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_MISC_NULL_WAV], 4, 2, 1, error);
    if (silent && !previous)
        return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SCOURGE_WALK_WAV], 4, 2, 1, error);
    return true;
}
static bool side(qa_q1_game *g, q1_actor *entity, bool right, float distance, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    bool moved;
    return qa_physics_walk_move(g->services.physics, entity->id, body.angles.y + (right ? 90 : 270),
                                distance, (float)g->elapsed, true, true, &moved, error);
}
static bool shoot(qa_q1_game *g, q1_actor *entity, float offset, qa_error *error) {
    entity->effects |= 2;
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, -19)),
                   qa_vec_add(qa_vec_scale(g->right, offset), qa_vec_scale(g->forward, 14)));
    qa_vec3 velocity =
        qa_vec_scale(qa_vec_normalize(qa_vec_sub(
                         qa_vec_add(target.origin, qa_vec_scale(g->forward, 200)), origin)),
                     1000);
    q1_actor *shot;
    if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_ROCKET1I_WAV], 1, 1, 1, error) ||
        !q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_SPIKE, origin, velocity, &shot,
                             error))
        return false;
    entity->state.monster.attack_finished = g->time + 0.2;
    return true;
}
static bool turn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
    float yaw = qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
    if (fabsf(body.angles.y - yaw) > 10)
        return q1_monster_face(g, entity, error);
    entity->state.monster.next_frame = q1_frame_index("scourge_run1");
    return true;
}
static bool tail(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state body, target;
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!qa_world_body_read(g->services.world, enemy, &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100)
        return true;
    bool visible;
    if (!q1_can_damage(g, enemy, entity->id, &visible, error))
        return false;
    if (!visible)
        return true;
    float a = q1_random(g), b = q1_random(g), c = q1_random(g);
    if (!q1_damage(g, enemy, entity->id, entity->id, (a + b + c) * 40, QA_Q1_WEAPON_COUNT, error) ||
        !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SMACK_WAV], 1, 1, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_vec3 velocity = qa_vec_scale(g->right, (q1_random(g) * 2 - 1) * 50);
    qa_vec3 origin = qa_vec_add(body.origin, qa_vec_scale(g->forward, 16));
    return q1_meat_spray(g, entity, origin, velocity, error);
}
bool q1_scourge_trigger(qa_q1_game *g, q1_actor *trigger, qa_actor_id actor, qa_error *error) {
    q1_actor *owner = q1_entity(g, q1_ref_actor(g, trigger->activator));
    if (!owner || owner->kind != Q1_MONSTER || q1_health(g, owner->id) <= 0)
        return q1_remove(g, trigger, error);
    qa_body_state body, source;
    if (!qa_world_body_read(g->services.world, owner->id, &source, error) ||
        !qa_world_body_read(g->services.world, trigger->id, &body, error))
        return false;
    if (!actor.registry) {
        qa_builtin_angle_vectors(source.angles, &g->forward, &g->right, &g->up);
        body.origin = qa_vec_add(source.origin, qa_vec_scale(g->forward, 300));
        return qa_world_body_write(g->services.world, trigger->id, &body, error) &&
               q1_link(g, trigger, error) &&
               q1_schedule(g, trigger, 0.1, Q1_THINK_SCOURGE_TRIGGER, error);
    }
    qa_q1_target traits;
    qa_physics_properties physics;
    if (!g->services.physics->services.read ||
        !g->services.physics->services.read(g->services.physics->services.context, actor,
                                            &physics) ||
        physics.motion != QA_PHYSICS_FLY_MISSILE || (physics.flags & QA_PHYSICS_MONSTER) ||
        (q1_target(g, actor, &traits) && traits.player))
        return true;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (qa_vec_dot(qa_vec_normalize(qa_vec_sub(source.origin, body.origin)),
                   qa_vec_normalize(body.velocity)) < 0.8f)
        return true;
    if (g->time > trigger->delay) {
        const char *animation =
            q1_random(g) < 0.5f ? "scourge_strafeleft1" : "scourge_straferight1";
        if (!q1_monster_play(g, owner, animation, error))
            return false;
        owner->state.monster.source.scourge.dodge_until = g->time + 1.5;
    }
    return true;
}
bool q1_scourge_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    switch (action) {
    case Q1_ACTION_SCOURGE_THINK:
        return locomotion_sound(g, entity, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_STAND1:
        monster->source.scourge.silent = false;
        return q1_monster_ai(g, entity, Q1_AI_STAND, 0, error) &&
               locomotion_sound(g, entity, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_WALK1:
    case Q1_ACTION_HIPSCRGE_SCOURGE_RUN1:
        if (q1_random(g) < 0.1f && !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SCOURGE_IDLE_WAV], 2, 2, 1, error))
            return false;
        monster->source.scourge.silent = true;
        return locomotion_sound(g, entity, error) &&
               q1_monster_ai(g, entity,
                             action == Q1_ACTION_HIPSCRGE_SCOURGE_WALK1 ? Q1_AI_WALK : Q1_AI_RUN,
                             action == Q1_ACTION_HIPSCRGE_SCOURGE_WALK1 ? 8 : 18, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_STRAFELEFT1:
    case Q1_ACTION_HIPSCRGE_SCOURGE_STRAFERIGHT1:
        monster->source.scourge.silent = true;
        return locomotion_sound(g, entity, error) &&
               side(g, entity, action == Q1_ACTION_HIPSCRGE_SCOURGE_STRAFERIGHT1, 20, error);
    case Q1_ACTION_AI_LEFT_20:
        return side(g, entity, false, 20, error);
    case Q1_ACTION_AI_LEFT_14:
        return side(g, entity, false, 14, error);
    case Q1_ACTION_AI_RIGHT_20:
        return side(g, entity, true, 20, error);
    case Q1_ACTION_AI_RIGHT_14:
        return side(g, entity, true, 14, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_TURN1:
        monster->source.scourge.silent = true;
        return locomotion_sound(g, entity, error) && turn(g, entity, error);
    case Q1_ACTION_AI_TURN_IN_PLACE:
        return turn(g, entity, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK1:
        monster->source.scourge.silent = false;
        return locomotion_sound(g, entity, error) && shoot(g, entity, 40, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK2:
        return shoot(g, entity, -56, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK3:
        return shoot(g, entity, -40, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK4:
        return shoot(g, entity, 56, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK5:
        return shoot(g, entity, 40, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_ATK8:
        if (!shoot(g, entity, 56, error))
            return false;
        if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
            monster->attack_finished = g->time + 4 * q1_random(g);
        else
            (void)q1_random(g);
        monster->counter = 0;
        return true;
    case Q1_ACTION_HIPSCRGE_SCOURGE_MELEE1:
        monster->source.scourge.silent = false;
        return locomotion_sound(g, entity, error) &&
               q1_monster_ai(g, entity, Q1_AI_CHARGE, 3, error);
    case Q1_ACTION_HIPSCRGE_SCOURGE_MELEE11: {
        if (!q1_monster_face(g, entity, error))
            return false;
        bool visible;
        if (g->options.skill == 3 && monster->counter != 1) {
            if (!q1_monster_visible(g, entity, q1_ref_actor(g, monster->enemy), &visible, error))
                return false;
            if (visible) {
                monster->counter = 1;
                monster->next_frame = q1_frame_index("scourge_melee1");
            }
        }
        return true;
    }
    case Q1_ACTION_HIPSCRGE_SCOURGE_PAIN1:
        monster->source.scourge.silent = false;
        return locomotion_sound(g, entity, error);
    case Q1_ACTION_ATTACK_WITH_TAIL:
        return tail(g, entity, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown scourge frame action");
        return false;
    }
}
