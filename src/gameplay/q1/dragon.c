#include "internal.h"
#include <stdio.h>

static bool stop_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (!m->source.dragon.attacking)
        return true;
    m->attack_finished = g->time + q1_random(g) * 2 + 4 - g->options.skill;
    m->source.dragon.attacking = false;
    qa_body_state body, goal = {0};
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, m->move_target), &goal, NULL);
    qa_trace_result trace;
    if (!q1_trace(g, body.origin, goal.origin, g->services.physics->world_actor, false, &trace,
                  error))
        return false;
    if (trace.fraction == 1)
        return true;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        qa_q1_target traits;
        if (q1_target(g, record->id, &traits) && traits.player &&
            !q1_message(g, record->id, "Error: Dragon cannot get to next target!\n", error))
            return false;
    }
    return true;
}
static bool check_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->source.dragon.attacking || m->source.dragon.missile == UINT16_MAX ||
        m->attack_finished > g->time)
        return true;
    if (q1_ref_present(m->enemy) && q1_health(g, q1_ref_actor(g, m->enemy)) < 0)
        m->enemy = (q1_ref){0};
    qa_q1_target traits;
    if (q1_target(g, q1_ref_actor(g, m->enemy), &traits) && traits.notarget)
        return true;
    if (!q1_ref_present(m->enemy)) {
        bool found;
        return q1_monster_find_target(g, entity, &found, error);
    }
    qa_body_state body, enemy;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, m->enemy), &enemy, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 delta = qa_vec_sub(enemy.origin, body.origin);
    if (qa_vec_dot(qa_vec_normalize(delta), g->forward) <= 0.3f)
        return true;
    qa_trace_result trace;
    if (!q1_trace(g, body.origin, enemy.origin, g->services.physics->world_actor, false, &trace,
                  error))
        return false;
    if (trace.fraction != 1)
        return true;
    m->source.dragon.attacking = true;
    m->next_frame =
        qa_vec_length(delta) < 350 ? q1_frame_index("dragon_melee1") : m->source.dragon.missile;
    return true;
}
static bool move(qa_q1_game *g, q1_actor *entity, float distance, qa_error *error) {
    if (q1_health(g, entity->id) < 1)
        return q1_remove(g, entity, error);
    q1_monster *m = &entity->state.monster;
    if (!m->source.dragon.attacking && !check_attack(g, entity, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body, target = {0};
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_actor_id previous = q1_ref_actor(g, m->enemy);
    qa_actor_id goal = q1_ref_actor(g, m->source.dragon.attacking ? m->enemy : m->move_target);
    (void)qa_world_body_read(g->services.world, goal, &target, NULL);
    if (!m->source.dragon.attacking)
        m->enemy = q1_ref_from(g, goal);
    qa_vec3 direction = qa_vec_sub(target.origin, body.origin);
    float desired = qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f);
    float yaw = body.angles.y, roll = body.angles.z;
    if (yaw != desired) {
        float offset = 180 - yaw;
        float left = qa_builtin_angle_mod(desired + offset) - 180,
              right = 180 - qa_builtin_angle_mod(desired + offset);
        if (left < 0)
            left = 360;
        else if (right < 0)
            right = 360;
        entity->physics.yaw_speed = 10;
        if (right < 180) {
            yaw = 10 < right ? yaw - 10 : desired;
            if (right > 5)
                roll = fminf(30, roll + 5);
        } else {
            yaw = 10 < right ? yaw + 10 : desired;
            if (left > 5)
                roll = fmaxf(-30, roll - 5);
        }
    } else if (roll != 0) {
        if (roll < -5)
            roll += 5;
        else if (roll < 5)
            roll = 0;
        else if (roll > 5)
            roll -= 5;
    }
    body.angles.y = yaw;
    body.angles.z = roll;
    if (direction.z > 5)
        body.origin.z += 5;
    else if (direction.z < -5)
        body.origin.z -= 5;
    qa_vec3 before = body.origin;
    bool moved;
    if (!qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !qa_physics_walk_move(g->services.physics, entity->id, yaw, distance, (float)g->elapsed,
                              true, true, &moved, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (body.origin.x == before.x && body.origin.y == before.y && body.origin.z == before.z &&
        q1_alive(g, q1_ref_actor(g, entity->physics.goal)) &&
        !qa_physics_q1_move_to_goal(g->services.physics, entity->id, q1_ref_actor(g, entity->physics.goal), distance,
                                    false, error))
        return false;
    m->enemy = q1_ref_from(g, previous);
    return true;
}
bool q1_dragon_launch_fireball(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction,
                               qa_error *error) {
    q1_actor *source = q1_entity(g, owner), *shot;
    if (source)
        source->effects |= 2;
    if (!q1_create(g, g->runtime_names[Q1_NAME_FIREBALL], Q1_PROJECTILE, owner, &shot, error) ||
        !q1_model(g, shot, "progs/fireball.mdl", error))
        return false;
    shot->state.projectile =
        (q1_projectile){.kind = Q1_DRAGON_FIREBALL,
                        .weapon = QA_Q1_WEAPON_COUNT,
                        .enemy = source && source->kind == Q1_MONSTER ? source->state.monster.enemy
                                                                      : (q1_ref){0},
                        .attack = q1_attack(g, owner, shot->id, QA_Q1_WEAPON_COUNT)};
    shot->state.projectile.attack.projectile = shot->id;
    shot->physics.motion = QA_PHYSICS_FLY_MISSILE;
    shot->physics.solid = QA_PHYSICS_BOX;
    shot->physics.angular_velocity = qa_v3(0, 0, 300);
    qa_body_state body = {.origin = origin};
    return qa_world_body_write(g->services.world, shot->id, &body, error) &&
           q1_missile_velocity(g, shot, qa_vec_scale(direction, q1_random(g) * 300 + 900), error) &&
           q1_schedule(g, shot, 6, Q1_THINK_REMOVE, error) && q1_link(g, shot, error);
}
bool q1_dragon_fireball_touch(qa_q1_game *g, q1_actor *shot, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(q1_ref_from(g, other), shot->owner))
        return true;
    bool dragon = q1_classnamed(g, q1_ref_actor(g, shot->owner), g->runtime_names[Q1_NAME_MONSTER_DRAGON]);
    if (!q1_radius(g, shot->id, q1_ref_actor(g, shot->owner), dragon ? 90 : 30,
                   q1_ref_actor(g, dragon ? shot->owner : q1_ref_from(g, g->services.physics->world_actor)), QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    if (!q1_alive(g, shot->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, shot->id, &body, error))
        return false;
    return q1_sound_resource(g, shot->id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_R_EXP3_WAV], 1, 1, 1, error) &&
           q1_effect(g, QA_BUILTIN_EXPLOSION, shot->id, body.origin, 5, 228, error) &&
           q1_remove(g, shot, error);
}
static bool fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DRAGON_ATTACK_WAV], 2, 1, 1, error))
        return false;
    qa_body_state body, target = {0};
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, 112), qa_vec_scale(g->up, 32)));
    bool plasma = q1_random(g) > 0.66f;
    unsigned count = plasma ? g->options.skill > 1 ? 2 : 1
                            : (unsigned)floorf(q1_random(g) * g->options.skill + 0.5f) + 1;
    for (unsigned i = 0; i < count; ++i) {
        if (!q1_alive(g, entity->id))
            return true;
        float distortion = (q1_random(g) - 0.5f) * 0.25f;
        target = (qa_body_state){0};
        (void)qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL);
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target.origin, origin));
        qa_builtin_angle_vectors(direction, &g->forward, &g->right, &g->up);
        qa_vec3 aim = qa_vec_add(direction, qa_vec_scale(g->right, distortion));
        if (plasma ? !q1_rogue_launch_plasma(g, entity->id, origin, aim, NULL, error)
                   : !q1_dragon_launch_fireball(g, entity->id, origin, aim, error))
            return false;
    }
    return true;
}
static bool tail(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!q1_alive(g, enemy))
        return true;
    bool visible;
    if (!q1_can_damage(g, enemy, entity->id, &visible, error))
        return false;
    if (!visible)
        return true;
    if (!move(g, entity, 10, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, enemy, &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
    if (qa_vec_length(delta) < 250) {
        if (!q1_damage(g, enemy, entity->id, entity->id, 30, QA_Q1_WEAPON_COUNT, error))
            return false;
        if (q1_alive(g, enemy)) {
            if (!qa_world_body_read(g->services.world, enemy, &target, error))
                return false;
            target.velocity = qa_vec_scale(qa_vec_normalize(delta), 500);
            target.velocity.z = 350;
            if (!qa_world_body_write(g->services.world, enemy, &target, error))
                return false;
            qa_q1_target traits;
            if (q1_target(g, enemy, &traits) && traits.player) {
                if (!g->services.motion_changed) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, enemy.slot,
                                 "dragon knockback needs selected movement continuation");
                    return false;
                }
                qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH,
                                                   .body = target};
                if (!g->services.motion_changed(g->services.context, enemy, &change, error))
                    return false;
            }
        }
    }
    return !q1_alive(g, entity->id) || stop_attack(g, entity, error);
}
static bool finish_death(qa_q1_game *g, q1_actor *entity, unsigned count, qa_error *error) {
    for (unsigned i = 0; i < count; i += 3)
        for (unsigned model = 1; model <= 3; ++model) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, entity->id, &body, error))
                return false;
            qa_vec3 velocity = qa_vec_scale(body.velocity, -1.25f);
            qa_builtin_angle_vectors(velocity, &g->forward, &g->right, &g->up);
            float right = q1_random(g) * 300 - 150, up = q1_random(g) * 300 - 150;
            velocity = qa_vec_add(
                velocity, qa_vec_add(qa_vec_scale(g->right, right), qa_vec_scale(g->up, up)));
            q1_actor *gib;
            if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_GIB], Q1_GIB, (qa_actor_id){0}, &gib, error))
                return false;
            char path[32];
            snprintf(path, sizeof(path), "progs/gib%u.mdl", model);
            gib->physics.motion = QA_PHYSICS_BOUNCE;
            float x = q1_random(g) * 600, y = q1_random(g) * 600, z = q1_random(g) * 600;
            gib->physics.angular_velocity = qa_v3(x, y, z);
            body = (qa_body_state){
                .origin = body.origin, .velocity = velocity, .bounds = {{-8, -8, -8}, {8, 8, 8}}};
            if (!q1_model(g, gib, path, error) ||
                !qa_world_body_write(g->services.world, gib->id, &body, error) ||
                !q1_schedule(g, gib, 10 + q1_random(g) * 10, Q1_THINK_REMOVE, error) ||
                !q1_link(g, gib, error))
                return false;
        }
    if (!g->services.use_targets) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "dragon death needs source target dispatcher");
        return false;
    }
    if (!qa_builtin_resource(&g->services, "dragondoor", &entity->target, error) ||
        !g->services.use_targets(g->services.context, entity->id, q1_ref_actor(g, entity->activator), entity->target,
                                 entity->killtarget, entity->delay, error))
        return false;
    return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
}
static bool explode(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->source.dragon.death_state > 1)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (qa_vec_length(body.velocity) < 100 || (entity->physics.flags & QA_PHYSICS_PLAYER)) {
        m->source.dragon.death_state = 3;
        const char *models[] = {"drggib01", "drggib02", "drggib03"};
        for (unsigned i = 0; i < 3; ++i)
            if (!q1_gib_at(g, entity->id, body.origin, -100, models[i], error))
                return false;
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_TORNOFF2_WAV], 4, 0, 1, error))
            return false;
        m->next_frame = q1_frame_index("dragon_boom2");
        return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    body.velocity = qa_vec_sub(body.velocity, qa_vec_scale(g->up, 40));
    m->source.dragon.last_velocity = body.velocity;
    return qa_world_body_write(g->services.world, entity->id, &body, error);
}
bool q1_dragon_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (!entity->state.monster.source.dragon.death_state)
        return true;
    if (q1_classnamed(g, other, g->runtime_names[Q1_NAME_PLAYER])) {
        if (!qa_builtin_resource(&g->services, "monster_dragon_dead", &entity->classname, error) ||
            !q1_damage(g, other, entity->id, entity->id, 200, QA_Q1_WEAPON_COUNT, error))
            return false;
    }
    if (!q1_alive(g, entity->id) || !qa_actor_id_equal(other, g->services.physics->world_actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    body.velocity = qa_v3(0, 0, 0);
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           explode(g, entity, error);
}
bool q1_dragon_corner_touch(qa_q1_game *g, q1_actor *corner, qa_actor_id other, qa_error *error) {
    q1_actor *entity = q1_entity(g, other);
    if (!entity || entity->kind != Q1_MONSTER ||
        entity->state.monster.species->species != QA_Q1_DRAGON ||
        !q1_classnamed(g, other, g->runtime_names[Q1_NAME_MONSTER_DRAGON]) ||
        !q1_ref_equal(entity->state.monster.move_target, q1_ref_from(g, corner->id)))
        return true;
    qa_actor_id goal = q1_find_target(g, corner->target);
    entity->state.monster.move_target = q1_ref_from(g, goal);
    entity->physics.goal = q1_ref_from(g, goal);
    entity->target = corner->target;
    if (goal.registry)
        return true;
    qa_error_set(error, QA_ERROR_FORMAT, corner->id.slot, "dragon_corner: no target found");
    return false;
}
bool q1_dragon_use(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (q1_health(g, entity->id) < 1)
        return true;
    entity->state.monster.next_frame = q1_frame_index("dragon_walk1");
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_dragon_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    body.bounds = entity->state.monster.species->bounds;
    entity->max_health = 3000.0f + 1000.0f * (float)g->options.skill;
    entity->state.monster.source.dragon.pain_sequence = 1;
    entity->state.monster.source.dragon.missile = UINT16_MAX;
    entity->state.monster.next_frame = q1_frame_index("dragon_activate");
    return qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) &&
           qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_model(g, entity, "progs/dragon.mdl", error) &&
           q1_schedule(g, entity, 0.1 - g->time, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_dragon_pain(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->pain_finished > g->time || q1_random(g) >= 0.25f)
        return true;
    if (!stop_attack(g, entity, error) || !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DRAGON_PAIN_WAV], 2, 1, 1, error))
        return false;
    m->pain_finished = g->time + 2;
    static const char *frames[] = {"dragon_painA1", "dragon_painF1", "dragon_painE1",
                                   "dragon_painD1", "dragon_painC1", "dragon_painB1"};
    if (m->source.dragon.pain_sequence >= 1 && m->source.dragon.pain_sequence <= 6)
        m->next_frame = q1_frame_index(frames[m->source.dragon.pain_sequence - 1]);
    return true;
}
bool q1_dragon_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    switch (action) {
    case Q1_ACTION_DRAGON_STOP_ATTACK:
        return stop_attack(g, entity, error);
    case Q1_ACTION_DRAGON_FIREBALL:
        return fire(g, entity, error);
    case Q1_ACTION_DRAGON_TAIL:
        return tail(g, entity, error);
    case Q1_ACTION_DRAGON_EXPLODE:
        return explode(g, entity, error);
    case Q1_ACTION_DRAGON_MOVE_10:
        return move(g, entity, 10, error);
    case Q1_ACTION_DRAGON_MOVE_12:
        return move(g, entity, 12, error);
    case Q1_ACTION_DRAGON_MOVE_17:
        return move(g, entity, 17, error);
    case Q1_ACTION_DRAGON_BOOM2: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body.velocity = m->source.dragon.last_velocity;
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               finish_death(g, entity, 15, error);
    }
    case Q1_ACTION_DRAGON_ACTIVATE: {
        qa_combat_state combat;
        qa_body_state body;
        bool moved;
        if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error) ||
            !qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        combat.can_take_damage = true;
        entity->aimed_damage = true;
        entity->physics.ideal_yaw = body.angles.y;
        entity->physics.flags |= QA_PHYSICS_FLYING | QA_PHYSICS_MONSTER;
        if (entity->physics.yaw_speed == 0)
            entity->physics.yaw_speed = 10;
        if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error) ||
            !qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
        m->move_target = q1_ref_from(g, q1_find_target(g, entity->target));
        entity->physics.goal = m->move_target;
        return qa_strings_text(qa_session_strings(g->services.session), entity->targetname).size ||
               q1_dragon_use(g, entity, error);
    }
    case Q1_ACTION_DRAGON_DRAGON_WALK1: {
        if (!stop_attack(g, entity, error))
            return false;
        m->source.dragon.missile = q1_frame_index("dragon_atk_a1");
        m->source.dragon.pain_sequence = 1;
        if (!move(g, entity, 17, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (q1_random(g) >= 0.2f)
            return true;
        qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND,
                                  .family = QA_GAME_Q1,
                                  .provider = g->options.provider,
                                  .actor = entity->id,
                                  .time_ns = g->time_ns,
                                  .channel = 2,
                                  .volume = 0.6f,
                                  .attenuation = 2};
        return qa_builtin_resource(&g->services, "dragon/active.wav", &sound.resource, error) &&
               qa_builtin_emit(&g->services, &sound, error);
    }
    case Q1_ACTION_DRAGON_DRAGON_WALK2:
        m->source.dragon.missile = UINT16_MAX;
        return move(g, entity, 17, error);
    case Q1_ACTION_DRAGON_DRAGON_WALK13:
        m->source.dragon.missile = UINT16_MAX;
        if (!move(g, entity, 17, error))
            return false;
        m->source.dragon.pain_sequence = 1;
        return true;
    case Q1_ACTION_DRAGON_DRAGON_WALK3:
    case Q1_ACTION_DRAGON_DRAGON_WALK5:
    case Q1_ACTION_DRAGON_DRAGON_WALK7:
    case Q1_ACTION_DRAGON_DRAGON_WALK9:
    case Q1_ACTION_DRAGON_DRAGON_WALK11: {
        unsigned sequence = action == Q1_ACTION_DRAGON_DRAGON_WALK3   ? 2
                            : action == Q1_ACTION_DRAGON_DRAGON_WALK5 ? 3
                            : action == Q1_ACTION_DRAGON_DRAGON_WALK7 ? 4
                            : action == Q1_ACTION_DRAGON_DRAGON_WALK9 ? 5
                                                                      : 6;
        static const char *frames[] = {"dragon_atk_b1", "dragon_atk_c1", "dragon_atk_d1",
                                       "dragon_atk_e1", "dragon_atk_f1"};
        m->source.dragon.missile = q1_frame_index(frames[sequence - 2]);
        if (!move(g, entity, 17, error))
            return false;
        m->source.dragon.pain_sequence = (uint8_t)sequence;
        return true;
    }
    case Q1_ACTION_DRAGON_DRAGON_DEATH1: {
        if (m->source.dragon.death_state)
            return true;
        m->source.dragon.death_state = 1;
        m->source.dragon.attacking = false;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        body.velocity = qa_vec_sub(qa_vec_scale(g->forward, 300), qa_vec_scale(g->up, 40));
        body.ground = (qa_actor_reference){0};
        body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
        entity->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DRAGON_DEATH_WAV], 2, 0, 1, error);
    }
    case Q1_ACTION_DRAGON_DRAGON_DEATH21:
        return finish_death(g, entity, 39, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown dragon frame action");
        return false;
    }
}
