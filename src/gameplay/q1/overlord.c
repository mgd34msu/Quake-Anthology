#include "internal.h"
#include <stdio.h>

bool q1_spawnpoint_empty(qa_q1_game *g, qa_actor_id marker, qa_vec3 origin) {
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        if (qa_actor_id_equal(record->id, marker))
            continue;
        q1_actor *native = q1_entity(g, record->id);
        if (native && native->physics.solid == QA_PHYSICS_NOT_SOLID)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, record->id, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(center, origin)) > 64)
            continue;
        qa_q1_target traits;
        qa_physics_properties physics;
        bool monster = g->services.physics && g->services.physics->services.read &&
                       g->services.physics->services.read(g->services.physics->services.context,
                                                          record->id, &physics) &&
                       (physics.flags & QA_PHYSICS_MONSTER);
        if (monster || (q1_target(g, record->id, &traits) && traits.player) ||
            qa_scheduler_pending(qa_session_scheduler(g->services.session), record->id))
            return false;
    }
    return true;
}
qa_actor_id q1_overlord_destination(qa_q1_game *g) {
    qa_body_state player = {0};
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        qa_q1_target traits;
        if (q1_target(g, record->id, &traits) && traits.player) {
            (void)qa_world_body_read(g->services.world, record->id, &player, NULL);
            break;
        }
    }
    qa_builtin_angle_vectors(player.angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward;
    qa_actor_id best = {0}, farthest = {0};
    float distance = 0;
    cursor = 0;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        if (!q1_classnamed(g, record->id, "info_overlord_destination"))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, record->id, &body, NULL) ||
            !q1_spawnpoint_empty(g, record->id, body.origin))
            continue;
        qa_vec3 delta = qa_vec_sub(body.origin, player.origin);
        float current = qa_vec_length(delta);
        if (qa_vec_dot(qa_vec_normalize(delta), forward) > 0.6f && current > 150)
            best = record->id;
        if (current > distance) {
            distance = current;
            farthest = record->id;
        }
    }
    return best.registry ? best : farthest;
}
static bool teleport(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!(entity->spawnflags & 2) || q1_random(g) > 0.75f)
        return true;
    qa_actor_id marker = q1_overlord_destination(g);
    if (!marker.registry)
        return true;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, marker, &target, error))
        return false;
    if (!qa_q1_spawn_teleport_fog(g, body.origin, NULL, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    if (!qa_q1_spawn_teleport_fog(g, qa_vec_add(target.origin, qa_vec_scale(g->forward, 32)), NULL,
                                  error) ||
        !qa_q1_spawn_teledeath(g, target.origin, entity->id, NULL, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    body.origin = target.origin;
    entity->physics.flags &= ~QA_PHYSICS_ONGROUND;
    return qa_world_body_write(g->services.world, entity->id, &body, error);
}
static bool toss(qa_q1_game *g, q1_actor *entity, const char *model, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    float z = q1_random(g) * 100 - 50, side = q1_random(g) * 200 - 100;
    qa_vec3 velocity =
        qa_vec_add(qa_vec_add(qa_vec_scale(g->forward, 250), qa_vec_scale(g->up, 300 + z)),
                   qa_vec_scale(g->right, side));
    q1_actor *gib;
    if (!q1_create(g, "gib", Q1_GIB, (qa_actor_id){0}, &gib, error))
        return false;
    gib->physics.motion = QA_PHYSICS_BOUNCE;
    char path[64];
    snprintf(path, sizeof(path), "progs/%s.mdl", model);
    body = (qa_body_state){.origin = body.origin, .velocity = velocity};
    return q1_model(g, gib, path, error) &&
           qa_world_body_write(g->services.world, gib->id, &body, error) &&
           q1_schedule(g, gib, 10 + q1_random(g) * 10, Q1_THINK_REMOVE, error) &&
           q1_link(g, gib, error);
}
static bool burst(qa_q1_game *g, q1_actor *entity, const char *const *models, unsigned count,
                  qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, body.origin, 4, 0, error))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!q1_alive(g, entity->id))
            return true;
        if (!toss(g, entity, models[i], error))
            return false;
    }
    return true;
}
static bool smash(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id enemy = entity->state.monster.enemy;
    if (!q1_alive(g, enemy))
        return true;
    bool visible;
    if (!q1_can_damage(g, enemy, entity->id, &visible, error))
        return false;
    if (!visible)
        return true;
    if (!q1_monster_ai(g, entity, Q1_AI_CHARGE, 10, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, enemy, &target, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100)
        return true;
    float damage = 20 + q1_random(g) * 10;
    if (!q1_sound(g, entity->id, "s_wrath/smash.wav", 1, 1, error) ||
        !q1_damage(g, enemy, entity->id, entity->id, damage, QA_Q1_WEAPON_COUNT, error))
        return false;
    qa_q1_target traits;
    float height = q1_target(g, enemy, &traits) ? traits.view_height : 25;
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, height)), body.origin));
    qa_builtin_event event = {.kind = QA_BUILTIN_IMPACT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = enemy,
                              .time_ns = g->time_ns,
                              .origin = qa_vec_sub(target.origin, qa_vec_scale(direction, 30)),
                              .direction = qa_vec_scale(direction, -100),
                              .code = 73,
                              .value = damage};
    return qa_builtin_emit(&g->services, &event, error);
}
bool q1_overlord_melee(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    float random = q1_random(g);
    return q1_monster_play(g, entity,
                           random < 0.33f   ? "overlord_at_a01"
                           : random < 0.66f ? "overlord_at_b01"
                                            : "overlord_at_c01",
                           error);
}
bool q1_overlord_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    switch (action) {
    case Q1_ACTION_OVERLORD_SMASH:
        return smash(g, entity, error);
    case Q1_ACTION_OVERLORD_TELEPORT:
        return teleport(g, entity, error);
    case Q1_ACTION_OVERLORD_MISSILE:
        (void)q1_random(g);
        return q1_monster_play(g, entity, "overlord_msl_a01", error);
    case Q1_ACTION_WRATHMISSILE_4:
        return q1_wrath_launch(g, entity, 4, error);
    case Q1_ACTION_S_WRATH_OVERLORD_DIE01:
        return q1_schedule(g, entity, 0.05, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_S_WRATH_OVERLORD_DIE02:
        entity->physics.flags |= QA_PHYSICS_FLYING;
        return q1_schedule(g, entity, 0.05, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_S_WRATH_OVERLORD_DIE17: {
        const char *models[] = {"s_wrtgb2", "s_wrtgb3", "wrthgib1", "wrthgib2", "wrthgib3"};
        entity->model = 0;
        return burst(g, entity, models, 5, error) &&
               (!q1_alive(g, entity->id) ||
                q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error));
    }
    case Q1_ACTION_S_WRATH_OVERLORD_DIE18:
    case Q1_ACTION_S_WRATH_OVERLORD_DIE19: {
        const char *models[] = {"gib1", "gib2", "gib3", "gib1", "gib2", "gib3"};
        if (!burst(g, entity, models, 6, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        return action == Q1_ACTION_S_WRATH_OVERLORD_DIE19
                   ? q1_remove(g, entity, error)
                   : q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown overlord frame action");
        return false;
    }
}
