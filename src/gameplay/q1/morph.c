#include "internal.h"

static bool setup(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_actor *owner = q1_entity(g, q1_ref_actor(g, entity->owner));
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    entity->physics.ideal_yaw = body.angles.y;
    if (entity->physics.yaw_speed == 0)
        entity->physics.yaw_speed = 20;
    entity->frame = q1_frames[q1_frame_index("morph_wake1")].frame;
    entity->skin = 2;
    body.bounds = entity->state.monster.species->bounds;
    combat.can_take_damage = false;
    entity->max_health = owner ? 200 : 2000;
    if (owner) {
        entity->effects = 0;
        entity->spawnflags = owner->spawnflags;
    } else
        entity->effects |= 8;
    return qa_combat_set_traits(g->services.combat, entity->id, &combat, error) &&
           qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) &&
           qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error);
}
static bool wake(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    bool empty = q1_spawnpoint_empty(g, entity->id, body.origin);
    if (empty && !setup(g, entity, error))
        return false;
    entity->state.monster.next_frame = q1_frame_index(empty ? "morph_wake1" : "morph_wake");
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_morph_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_string_id model = entity->spawnflags & 2   ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_MORPH_AZ_MDL]
                        : entity->spawnflags & 4 ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_MORPH_EG_MDL]
                        : entity->spawnflags & 8 ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_MORPH_GR_MDL]
                                                 : 0;
    if (!model) {
        qa_error_set(error, QA_ERROR_FORMAT, entity->id.slot,
                     "monster_morph has no source skin selection");
        return false;
    }
    if (!q1_model(g, entity, model, error))
        return false;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->aimed_damage = false;
    return qa_strings_text(qa_session_strings(g->services.session), entity->targetname).size ||
           wake(g, entity, error);
}
static bool child(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (q1_ref_present(entity->owner) ||
        entity->state.monster.source.morph.children > 1u + g->options.skill)
        return true;
    qa_actor_id marker;
    if (!q1_overlord_destination(g, &marker, error))
        return false;
    if (!marker.registry)
        return true;
    qa_body_state target;
    if (!qa_world_body_read(g->services.world, marker, &target, error))
        return false;
    q1_actor *point = q1_entity(g, marker), *next;
    if (!q1_create(g, g->runtime_names[Q1_NAME_MONSTER_MORPH], Q1_MONSTER, entity->id, &next, error))
        return false;
    next->model = entity->model;
    next->target = entity->target;
    next->initial_angles = point ? point->initial_angles : target.angles;
    next->state.monster = (q1_monster){.species = entity->state.monster.species,
                                       .birth_epoch = 1,
                                       .enemy = entity->state.monster.enemy};
    next->physics.enemy = entity->state.monster.enemy;
    next->physics.goal = entity->physics.goal;
    qa_body_state body = {.angles = next->initial_angles};
    if (!qa_world_body_write(g->services.world, next->id, &body, error) || !setup(g, next, error) ||
        !qa_world_body_read(g->services.world, next->id, &body, error))
        return false;
    body.origin = target.origin;
    if (!qa_world_body_write(g->services.world, next->id, &body, error) ||
        !q1_monster_drop_floor(g, entity, error))
        return false;
    next->state.monster.next_frame = q1_frame_index("morph_wake1");
    return q1_schedule(g, next, 0.3, Q1_THINK_MONSTER_FRAME, error);
}
static bool laser(qa_q1_game *g, q1_actor *entity, qa_vec3 origin, qa_vec3 direction,
                  qa_error *error) {
    q1_actor *shot;
    return q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ENFORCER_LASER, origin,
                               qa_vec_scale(qa_vec_normalize(direction), 600), &shot, error);
}
static bool attack(qa_q1_game *g, q1_actor *entity, bool stab, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (stab) {
        bool visible;
        if (!q1_alive(g, enemy))
            return true;
        if (!q1_can_damage(g, enemy, entity->id, &visible, error))
            return false;
        if (!visible)
            return true;
    }
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, enemy, &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
    if (stab) {
        qa_q1_target traits;
        delta.z += q1_target(g, enemy, &traits) ? traits.view_height : 25;
    }
    float distance = qa_vec_length(delta);
    qa_vec3 direction = qa_vec_normalize(delta);
    if (stab && distance <= 90) {
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ENFORCER_ENFSTOP_WAV], 1, 3, 1, error) ||
            !q1_damage(g, enemy, entity->id, entity->id, q1_random(g) * 10 + 20, QA_Q1_WEAPON_COUNT,
                       error))
            return false;
        qa_builtin_event particles = {.kind = QA_BUILTIN_IMPACT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = enemy,
                                      .origin = target.origin,
                                      .direction = qa_vec_scale(g->forward, 150),
                                      .time_ns = g->time_ns,
                                      .code = 73,
                                      .count = 14};
        return qa_builtin_emit(&g->services, &particles, error);
    }
    entity->effects |= 2;
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, stab ? 80 : 30),
                                           qa_vec_add(qa_vec_scale(g->right, stab ? 4 : 8.5f),
                                                      qa_v3(0, 0, stab ? 4 : 16))));
    float spread = stab ? (distance != 0 ? 0.04f : 0.1f) : (distance > 400 ? 0.04f : 0.1f);
    if (!laser(g, entity, origin, direction, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!laser(g, entity, origin, qa_vec_add(direction, qa_vec_scale(g->right, spread)), error))
        return false;
    return !q1_alive(g, entity->id) ||
           laser(g, entity, origin, qa_vec_sub(direction, qa_vec_scale(g->right, spread)), error);
}
static bool smack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!q1_alive(g, enemy))
        return true;
    bool visible;
    if (!q1_can_damage(g, enemy, entity->id, &visible, error))
        return false;
    if (!visible)
        return true;
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, enemy, &target, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100)
        return true;
    if (!q1_damage(g, enemy, entity->id, entity->id, q1_random(g) * 10 + 10, QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    if (!q1_alive(g, enemy) || !q1_alive(g, entity->id))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, enemy, &target, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    target.velocity = qa_vec_add(qa_vec_scale(g->forward, 100), qa_v3(0, 0, 100));
    if (!qa_world_body_write(g->services.world, enemy, &target, error))
        return false;
    qa_q1_target traits;
    if (q1_target(g, enemy, &traits) && traits.player) {
        if (!g->services.motion_changed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, enemy.slot,
                         "morph knockback needs selected movement continuation");
            return false;
        }
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = target};
        return g->services.motion_changed(g->services.context, enemy, &change, error);
    }
    return true;
}
bool q1_morph_melee(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    float random = q1_random(g);
    return q1_monster_play(g, entity,
                           random < 0.5f    ? "morph_bigattack01"
                           : random < 0.75f ? "morph_attack01"
                                            : "morph_knockback01",
                           error);
}
bool q1_morph_pain(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (g->options.skill == 3)
        return q1_random(g) <= 0.5f || child(g, entity, error);
    q1_monster *monster = &entity->state.monster;
    if (monster->pain_finished > g->time || q1_random(g) > 0.25f)
        return true;
    float random = q1_random(g);
    monster->pain_finished = g->time + 2;
    monster->next_frame = q1_frame_index(random > 0.6f ? "morph_painB1" : "morph_painA1");
    return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_GUARD_PAIN1_WAV], 2, 1, 1, error) &&
           q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_morph_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    switch (action) {
    case Q1_ACTION_MORPH_STAB2:
        return attack(g, entity, true, error);
    case Q1_ACTION_MORPH_FIRE:
        return attack(g, entity, false, error);
    case Q1_ACTION_MORPH_SMACK:
        return smack(g, entity, error);
    case Q1_ACTION_MORPH_TELEPORT:
        return child(g, entity, error);
    case Q1_ACTION_MORPH_WAKE:
        return wake(g, entity, error);
    case Q1_ACTION_MORPH_MORPH_DIE9:
        ++entity->skin;
        return true;
    case Q1_ACTION_MORPH_MORPH_DIE21:
        return q1_remove(g, entity, error);
    case Q1_ACTION_MORPH_MORPH_WAKE1: {
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_GUARD_SEE1_WAV], 2, 1, 1, error))
            return false;
        q1_actor *owner = q1_entity(g, q1_ref_actor(g, entity->owner));
        if (!owner || owner->kind != Q1_MONSTER)
            return true;
        ++g->total_monsters;
        ++owner->state.monster.source.morph.children;
        return q1_effect(g, QA_BUILTIN_TARGET, entity->id, qa_v3(0, 0, 0), (float)g->total_monsters,
                         1, error);
    }
    case Q1_ACTION_MORPH_MORPH_WAKE15:
        entity->skin = 1;
        return true;
    case Q1_ACTION_MORPH_MORPH_WAKE31: {
        entity->physics.solid = QA_PHYSICS_BOX;
        entity->aimed_damage = true;
        --entity->skin;
        qa_combat_state combat;
        if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
            return false;
        combat.can_take_damage = true;
        if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error))
            return false;
        if (q1_ref_present(entity->owner)) {
            entity->state.monster.next_frame = q1_frame_index("morph_run1");
            if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error))
                return false;
        }
        return q1_link(g, entity, error);
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown morph frame action");
        return false;
    }
}
