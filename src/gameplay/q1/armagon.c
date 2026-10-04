#include "internal.h"
#include <stdio.h>

static bool sound(qa_q1_game *g, q1_actor *entity, const char *path, int32_t channel, float volume,
                  float attenuation, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = entity->id,
                              .time_ns = g->time_ns,
                              .channel = channel,
                              .volume = volume,
                              .attenuation = attenuation};
    return qa_builtin_resource(&g->services, path, &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
static bool sync_body(qa_q1_game *g, q1_actor *entity, bool walking, qa_error *error) {
    q1_actor *part = q1_entity(g, entity->state.monster.source.armagon.body);
    if (!part) {
        qa_error_set(error, QA_ERROR_FORMAT, entity->id.slot,
                     "Armagon lost its source body entity");
        return false;
    }
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, part->id, &target, error))
        return false;
    target.origin = body.origin;
    target.angles = body.angles;
    target.angles.y += entity->state.monster.source.armagon.torso_yaw;
    if (walking && g->options.edition == QA_Q1_RERELEASE)
        part->physics.motion = QA_PHYSICS_STEP;
    part->frame = entity->frame;
    return qa_world_body_write(g->services.world, part->id, &target, error);
}
static void turn(q1_actor *entity, float difference, float target) {
    float *yaw = &entity->state.monster.source.armagon.torso_yaw;
    *yaw = fabsf(difference) < 10 ? target
           : difference > 5       ? *yaw + 9
           : difference < -5      ? *yaw - 9
                                  : target;
}
static qa_vec3 enemy_origin(qa_q1_game *g, q1_actor *entity, bool eyes) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->state.monster.enemy, &body, NULL))
        return qa_v3(0, 0, 0);
    qa_q1_target traits;
    if (eyes)
        body.origin.z +=
            q1_target(g, entity->state.monster.enemy, &traits) ? traits.view_height : 25;
    return body.origin;
}
static bool idle(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (q1_health(g, entity->id) < 0 || g->time <= entity->state.monster.source.armagon.idle_at)
        return true;
    entity->state.monster.source.armagon.idle_at = g->time + 3;
    if (q1_random(g) >= 0.5f)
        return true;
    float r = q1_random(g);
    char path[32];
    snprintf(path, sizeof(path), "armagon/idle%u.wav",
             r < 0.25f   ? 1
             : r < 0.5f  ? 2
             : r < 0.75f ? 3
                         : 4);
    return sound(g, entity, path, 2, 1, 0.5f, error);
}
static bool think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!sync_body(g, entity, false, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 delta = qa_vec_sub(enemy_origin(g, entity, false), body.origin);
    entity->physics.ideal_yaw = qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
    float offset = entity->physics.ideal_yaw - body.angles.y;
    if (offset > 180)
        offset -= 360;
    if (offset < -180)
        offset += 360;
    entity->state.monster.source.armagon.behind = fabsf(offset) > 90;
    if (entity->state.monster.source.armagon.behind)
        offset = 0;
    turn(entity, offset - entity->state.monster.source.armagon.torso_yaw, offset);
    if (q1_health(g, entity->id) < 0)
        return true;
    if (!idle(g, entity, error))
        return false;
    q1_actor *part = q1_entity(g, entity->state.monster.source.armagon.body);
    if (part && g->options.edition == QA_Q1_RERELEASE &&
        qa_vec_length(qa_vec_sub(entity->state.monster.source.armagon.old_origin, body.origin)) >
            50)
        part->physics.motion = QA_PHYSICS_STEP;
    return true;
}
static bool walk_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!sync_body(g, entity, true, error) ||
        !qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error))
        return false;
    entity->state.monster.source.armagon.behind = false;
    turn(entity, -entity->state.monster.source.armagon.torso_yaw, 0);
    if (q1_health(g, entity->id) < 0)
        return true;
    if (!idle(g, entity, error))
        return false;
    qa_actor_id client = {0};
    bool visible;
    if (!g->host.check_client) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "Armagon requires source check-client admission");
        return false;
    }
    if (!g->host.check_client(g->host.context, entity->id, &client) || !client.registry)
        return true;
    return q1_monster_visible(g, entity, client, &visible, error) &&
           (!visible || q1_monster_found(g, entity, client, error));
}
static bool launch(qa_q1_game *g, q1_actor *entity, float offset, unsigned turn_direction,
                   bool laser, qa_error *error) {
    qa_body_state body, enemy = {0};
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 angles = body.angles;
    angles.y += entity->state.monster.source.armagon.torso_yaw + (turn_direction == 1   ? 165
                                                                  : turn_direction == 2 ? -165
                                                                                        : 0);
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward;
    qa_vec3 origin = qa_vec_add(
        body.origin, qa_vec_add(qa_v3(0, 0, 66), qa_vec_add(qa_vec_scale(g->right, offset),
                                                            qa_vec_scale(forward, 84))));
    qa_vec3 target = enemy_origin(g, entity, true);
    if (g->options.skill) {
        (void)qa_world_body_read(g->services.world, entity->state.monster.enemy, &enemy, NULL);
        target = qa_vec_add(
            target, qa_vec_scale(enemy.velocity, qa_vec_length(qa_vec_sub(target, origin)) / 1000));
    }
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target, origin));
    if (qa_vec_dot(direction, forward) < entity->state.monster.source.armagon.aim_threshold)
        direction = forward;
    entity->effects |= 2;
    if (laser)
        return q1_hipnotic_launch_laser(g, entity->id, QA_Q1_WEAPON_COUNT, origin, direction, false,
                                        error);
    q1_actor *shot;
    return q1_sound(g, entity->id, "weapons/sgun1.wav", 1, 1, error) &&
           q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ROCKET, origin,
                               qa_vec_scale(direction, 1000), &shot, error);
}
static bool walking_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    bool moved;
    return qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error) &&
           qa_world_body_read(g->services.world, entity->id, &body, error) &&
           qa_physics_walk_move(g->services.physics, entity->id, body.angles.y, 14,
                                (float)g->elapsed, true, true, &moved, error) &&
           (!q1_alive(g, entity->id) || think(g, entity, error));
}
static bool walk(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id goal = q1_monster_route(g, entity);
    if (!goal.registry)
        goal = g->services.physics->world_actor;
    return !goal.registry ||
           qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal, 14, false, error);
}
static bool run(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error))
        return false;
    g->run_straight = true;
    return q1_monster_ai(g, entity, Q1_AI_RUN, 14, error) &&
           (!q1_alive(g, entity->id) || think(g, entity, error));
}
static bool over_think(qa_q1_game *g, q1_actor *entity, bool left, qa_error *error) {
    qa_body_state body;
    bool moved;
    if (!qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error) ||
        !qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_physics_walk_move(g->services.physics, entity->id, body.angles.y, 14, (float)g->elapsed,
                              true, true, &moved, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!sync_body(g, entity, false, error) ||
        !qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float delta = 0;
    if (entity->count == 0) {
        qa_vec3 direction = qa_vec_sub(enemy_origin(g, entity, false), body.origin);
        entity->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f);
        delta = entity->physics.ideal_yaw - body.angles.y + (left ? -165 : 165);
        if (delta > 180)
            delta -= 360;
        if (delta < -180)
            delta += 360;
    } else if (entity->count == 1)
        return launch(g, entity, left ? 40 : -40, left ? 1 : 2, false, error);
    turn(entity, delta - entity->state.monster.source.armagon.torso_yaw, delta);
    return true;
}
static bool clear_shot(qa_q1_game *g, q1_actor *entity, bool *clear, bool *water, float *distance,
                       qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 25)), end = enemy_origin(g, entity, true);
    qa_actor_id enemy = entity->state.monster.enemy.registry ? entity->state.monster.enemy
                                                             : g->services.physics->world_actor;
    qa_trace_result trace;
    if (!q1_trace(g, start, end, entity->id, true, &trace, error))
        return false;
    *clear = trace.hit != QA_TRACE_HIT_NONE && qa_actor_id_equal(trace.actor, enemy);
    *water = trace.in_open && trace.in_water;
    *distance = qa_vec_length(qa_vec_sub(end, start));
    return true;
}
static void attack_finished(qa_q1_game *g, q1_actor *entity, double delay) {
    if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
        entity->state.monster.attack_finished = g->time + delay;
    entity->state.monster.refired = false;
}
static bool repulse(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!think(g, entity, error))
        return false;
    if (!entity->state.monster.source.armagon.repulse_state) {
        attack_finished(g, entity, 0.5);
        entity->state.monster.source.armagon.repulse_state = 1;
        return q1_sound(g, entity->id, "armagon/repel.wav", 4, 1, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, body.origin, 300, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id actor = snapshot->snapshot.ids[i - 1];
        qa_q1_target traits;
        bool visible;
        if (!q1_target(g, actor, &traits) || !traits.player || traits.notarget ||
            q1_health(g, actor) <= 0)
            continue;
        if (!q1_monster_visible(g, entity, actor, &visible, error)) {
            ok = false;
            break;
        }
        if (!visible)
            continue;
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, actor, &target, NULL))
            continue;
        qa_vec3 direction =
            qa_vec_normalize(qa_vec_sub(target.origin, qa_vec_sub(body.origin, qa_v3(0, 0, 24))));
        target.velocity = qa_vec_add(target.velocity, qa_vec_scale(direction, 1500));
        if (!g->services.motion_changed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Armagon repulse needs selected movement continuation");
            ok = false;
            break;
        }
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = target};
        if (!qa_world_body_write(g->services.world, actor, &target, error) ||
            !g->services.motion_changed(g->services.context, actor, &change, error)) {
            ok = false;
            break;
        }
        if (!q1_alive(g, entity->id))
            break;
    }
    qa_builtin_snapshot_release(snapshot);
    if (!ok || !q1_alive(g, entity->id))
        return ok;
    if (!q1_radius(g, entity->id, entity->id, 60, entity->id, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (q1_alive(g, entity->id)) {
        entity->state.monster.source.armagon.repulse_state = 0;
        attack_finished(g, entity, 0.1);
    }
    return true;
}
static bool stand_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    bool clear, water;
    float distance;
    if (!clear_shot(g, entity, &clear, &water, &distance, error))
        return false;
    if (!clear || water)
        return q1_monster_play(g, entity, "armagon_run1", error);
    if (g->time < entity->state.monster.attack_finished)
        return true;
    qa_q1_target traits;
    if (distance < 200 && q1_target(g, entity->state.monster.enemy, &traits) && traits.player)
        return repulse(g, entity, error);
    entity->state.monster.source.armagon.repulse_state = 0;
    if (distance > 450)
        return q1_monster_play(g, entity, "armagon_run1", error);
    if (!q1_monster_play(g, entity, q1_random(g) < 0.5f ? "armagon_satk1" : "armagon_slaser1",
                         error))
        return false;
    return !q1_alive(g, entity->id) || !entity->state.monster.source.armagon.behind ||
           q1_monster_play(g, entity, "armagon_run1", error);
}
bool q1_armagon_attack(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    entity->state.monster.lefty = false;
    *out = false;
    bool clear, water;
    float distance;
    if (!clear_shot(g, entity, &clear, &water, &distance, error))
        return false;
    if ((!clear && !entity->state.monster.charmer.registry) || water ||
        g->time < entity->state.monster.attack_finished)
        return true;
    qa_body_state body;
    qa_q1_target traits;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float delta = entity->physics.ideal_yaw -
                  (body.angles.y + entity->state.monster.source.armagon.torso_yaw);
    if ((fabsf(delta) > 10 && distance > 200) ||
        !q1_target(g, entity->state.monster.enemy, &traits) || !traits.player)
        return true;
    if (distance < 400) {
        *out = true;
        return q1_monster_play(g, entity, "armagon_stop1", error);
    }
    entity->state.monster.lefty = true;
    return true;
}
bool q1_armagon_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state source;
    if (!qa_world_body_read(g->services.world, entity->id, &source, error))
        return false;
    q1_actor *part;
    if (!q1_create(g, "armagon_body", Q1_TIMER, (qa_actor_id){0}, &part, error))
        return false;
    qa_body_state body = {.origin = qa_vec_sub(source.origin, qa_v3(0, 0, 64)),
                          .bounds = {{-16, -16, -16}, {16, 16, 16}}};
    part->physics.solid = QA_PHYSICS_NOT_SOLID;
    if (g->options.edition == QA_Q1_CLASSIC)
        part->physics.motion = QA_PHYSICS_STEP;
    entity->state.monster.source.armagon.body = part->id;
    entity->state.monster.source.armagon.old_origin = source.origin;
    entity->state.monster.source.armagon.aim_threshold = g->options.skill == 0   ? 0.9f
                                                         : g->options.skill == 1 ? 0.85f
                                                                                 : 0.75f;
    entity->physics.yaw_speed = g->options.skill == 0 ? 5 : g->options.skill == 1 ? 9 : 12;
    entity->max_health = g->options.skill == 0 ? 2000 : g->options.skill == 1 ? 2500 : 3500;
    return q1_model(g, part, "progs/armabody.mdl", error) &&
           qa_world_body_write(g->services.world, part->id, &body, error) &&
           q1_link(g, part, error) &&
           qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error);
}
static bool final_death(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!think(g, entity, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_q1_spawn_multi_explosion(g, qa_vec_add(body.origin, qa_v3(0, 0, 80)), 20, 10, 3, 0.1f,
                                     0.5f, NULL, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
    entity->think = Q1_THINK_NONE;
    entity->next_think = 0;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.flags = 0;
    entity->consumed_corpse = true;
    body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, 32}};
    entity->wait = (float)(g->time + 5);
    q1_actor *part = q1_entity(g, entity->state.monster.source.armagon.body);
    if (!part) {
        qa_error_set(error, QA_ERROR_FORMAT, entity->id.slot, "Armagon lost its body during death");
        return false;
    }
    part->physics.solid = QA_PHYSICS_NOT_SOLID;
    part->consumed_corpse = true;
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error) && q1_link(g, part, error) &&
           q1_schedule(g, part, 0.1, Q1_THINK_ARMAGON_BODY, error);
}
bool q1_armagon_think(qa_q1_game *g, q1_actor *part, q1_think_kind kind, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, part->id, &body, error))
        return false;
    if (kind == Q1_THINK_ARMAGON_BODY) {
        if (!q1_schedule(g, part, 0.1, kind, error))
            return false;
        uint32_t count = part->state.effect.count;
        if (!count)
            part->count = 0;
        if (count < 25) {
            if ((float)count > part->count) {
                const char *models[] = {"gib1", "gib2", "gib3"};
                for (unsigned i = 0; i < 3; ++i)
                    if (!q1_gib_at(g, part->id, body.origin, -100, models[i], error))
                        return false;
                part->count = (float)(count + 1);
            }
            part->state.effect.count = count + 1;
            return true;
        }
        part->state.effect.count = 0;
        return q1_schedule(g, part, 0.1, Q1_THINK_ARMAGON_EXPLOSION, error);
    }
    if (!q1_sound(g, part->id, "misc/longexpl.wav", 0, 0.5f, error))
        return false;
    const char *models[] = {"gib1", "gib2", "gib3"};
    for (unsigned i = 0; i < 9; ++i)
        if (!q1_gib_at(g, part->id, body.origin, -200, models[i % 3], error))
            return false;
    part->physics.motion = QA_PHYSICS_STATIONARY;
    part->physics.solid = QA_PHYSICS_NOT_SOLID;
    part->frame = 0;
    return q1_model(g, part, "progs/s_explod.spr", error) &&
           q1_schedule(g, part, 0.1, Q1_THINK_SPRITE, error);
}

bool q1_armagon_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    bool servo = action == Q1_ACTION_HIPARMA_ARMAGON_WALK3 ||
                 action == Q1_ACTION_HIPARMA_ARMAGON_RUN3 ||
                 action == Q1_ACTION_HIPARMA_ARMAGON_WATK2 ||
                 action == Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT3 ||
                 action == Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT3;
    bool foot =
        action == Q1_ACTION_HIPARMA_ARMAGON_WALK5 || action == Q1_ACTION_HIPARMA_ARMAGON_RUN5 ||
        action == Q1_ACTION_HIPARMA_ARMAGON_WATK4 || action == Q1_ACTION_HIPARMA_ARMAGON_STOP2 ||
        action == Q1_ACTION_HIPARMA_ARMAGON_SATK6 ||
        action == Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT5 ||
        action == Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT5 ||
        action == Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT10;
    if (servo && !sound(g, entity, "armagon/servo.wav", 7, 0.5f, 0.5f, error))
        return false;
    if (foot && !sound(g, entity, "armagon/footfall.wav", 6, 1, 0.5f, error))
        return false;
    switch (action) {
    case Q1_ACTION_ARMAGON_THINK:
    case Q1_ACTION_HIPARMA_ARMAGON_STOP2:
    case Q1_ACTION_HIPARMA_ARMAGON_SATK6:
        return think(g, entity, error);
    case Q1_ACTION_ARMAGON_WALKTHINK:
        return walk_think(g, entity, error);
    case Q1_ACTION_ARMAGON_STAND_ATTACK:
        return stand_attack(g, entity, error);
    case Q1_ACTION_ARMAGON_MISSILE_ATTACK:
        return q1_monster_play(g, entity,
                               q1_random(g) < 0.5f ? "armagon_watk1" : "armagon_wlaseratk1", error);
    case Q1_ACTION_MOVETOGOAL_14:
        return walk(g, entity, error);
    case Q1_ACTION_HIPARMA_ARMAGON_STAND1:
        if (!q1_monster_ai(g, entity, Q1_AI_STAND, 0, error))
            return false;
        return !q1_alive(g, entity->id) ||
               (think(g, entity, error) &&
                q1_schedule(g, entity, 0.2, Q1_THINK_MONSTER_FRAME, error));
    case Q1_ACTION_HIPARMA_ARMAGON_STAND2:
        return think(g, entity, error) &&
               q1_schedule(g, entity, 0.2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPARMA_ARMAGON_WALK3:
    case Q1_ACTION_HIPARMA_ARMAGON_WALK5:
        return walk(g, entity, error) && (!q1_alive(g, entity->id) || walk_think(g, entity, error));
    case Q1_ACTION_HIPARMA_ARMAGON_RUN1:
    case Q1_ACTION_HIPARMA_ARMAGON_RUN3:
    case Q1_ACTION_HIPARMA_ARMAGON_RUN5:
        return run(g, entity, error);
    case Q1_ACTION_HIPARMA_ARMAGON_RUN12: {
        if (!run(g, entity, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        q1_monster *m = &entity->state.monster;
        if (m->source.armagon.behind && g->time > m->attack_finished) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, entity->id, &body, error))
                return false;
            float delta = entity->physics.ideal_yaw - body.angles.y;
            if (delta > 180)
                delta -= 360;
            if (delta < -180)
                delta += 360;
            m->next_frame = q1_frame_index(delta > 0 ? "armagon_overleft1" : "armagon_overright1");
        } else if (m->lefty) {
            m->lefty = false;
            m->next_frame = q1_frame_index("armagon_missile_attack");
        }
        return true;
    }
    case Q1_ACTION_HIPARMA_ARMAGON_WATK1:
    case Q1_ACTION_HIPARMA_ARMAGON_WATK2:
    case Q1_ACTION_HIPARMA_ARMAGON_WATK4:
        return walking_attack(g, entity, error);
    case Q1_ACTION_HIPARMA_ARMAGON_WATK6:
    case Q1_ACTION_HIPARMA_ARMAGON_WATK11:
    case Q1_ACTION_HIPARMA_ARMAGON_WLASERATK6:
    case Q1_ACTION_HIPARMA_ARMAGON_WLASERATK11:
        if (!walking_attack(g, entity, error))
            return false;
        return !q1_alive(g, entity->id) ||
               launch(g, entity,
                      action == Q1_ACTION_HIPARMA_ARMAGON_WATK6 ||
                              action == Q1_ACTION_HIPARMA_ARMAGON_WLASERATK6
                          ? 40
                          : -40,
                      0,
                      action == Q1_ACTION_HIPARMA_ARMAGON_WLASERATK6 ||
                          action == Q1_ACTION_HIPARMA_ARMAGON_WLASERATK11,
                      error);
    case Q1_ACTION_HIPARMA_ARMAGON_WATK13:
        if (!walking_attack(g, entity, error))
            return false;
        attack_finished(g, entity, 1);
        return true;
    case Q1_ACTION_SUB_ATTACKFINISHED_1_0:
        attack_finished(g, entity, 1);
        return true;
    case Q1_ACTION_SUB_ATTACKFINISHED_0_3:
        attack_finished(g, entity, 0.3);
        return true;
    case Q1_ACTION_HIPARMA_ARMAGON_SATK9:
        return think(g, entity, error) && launch(g, entity, 40, 0, false, error) &&
               (!q1_alive(g, entity->id) || launch(g, entity, -40, 0, false, error));
    case Q1_ACTION_ARMAGON_LAUNCH_LASER_40:
        return launch(g, entity, 40, 0, true, error);
    case Q1_ACTION_ARMAGON_LAUNCH_LASER_NEG_40:
        return launch(g, entity, -40, 0, true, error);
    case Q1_ACTION_HIPARMA_ARMAGON_DIE4: {
        qa_body_state body;
        return think(g, entity, error) &&
               qa_world_body_read(g->services.world, entity->id, &body, error) &&
               qa_q1_spawn_multi_explosion(g, qa_vec_add(body.origin, qa_v3(0, 0, 48)), 48, 10, 6,
                                           0.3f, 0.3f, NULL, error) &&
               sound(g, entity, "armagon/death.wav", 0, 1, 0, error) &&
               q1_schedule(g, entity, 0.2, Q1_THINK_MONSTER_FRAME, error);
    }
    case Q1_ACTION_HIPARMA_ARMAGON_DIE8:
        return think(g, entity, error) && q1_schedule(g, entity, 2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPARMA_ARMAGON_DIE14:
        return final_death(g, entity, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT1:
        entity->count = 0;
        return over_think(g, entity, true, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT1:
        entity->count = 0;
        return over_think(g, entity, false, error);
    case Q1_ACTION_ARMAGON_OVERLEFT_THINK:
    case Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT3:
    case Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT5:
        return over_think(g, entity, true, error);
    case Q1_ACTION_ARMAGON_OVERRIGHT_THINK:
    case Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT3:
    case Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT10:
        return over_think(g, entity, false, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT11:
        entity->count = 1;
        return over_think(g, entity, true, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERLEFT12:
        entity->count = 2;
        return over_think(g, entity, true, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT5:
        entity->count = 1;
        return over_think(g, entity, false, error);
    case Q1_ACTION_HIPARMA_ARMAGON_OVERRIGHT6:
        entity->count = 2;
        return over_think(g, entity, false, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown Armagon frame action");
        return false;
    }
}
