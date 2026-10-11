#include "internal.h"

static q1_actor *time_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && q1_map_is_time_actor(entity->map->kind) ? entity : NULL;
}
static bool create_time_actor(qa_q1_game *g, qa_string_id name, q1_map_kind kind, qa_actor_id owner,
                              q1_actor **out, qa_error *error) {
    q1_actor *entity;
    if (!q1_create(g, name, Q1_MAP, owner, &entity, error))
        return false;
    if (!q1_map_allocate(g, entity, error)) {
        (void)q1_remove(g, entity, NULL);
        return false;
    }
    entity->map->kind = kind;
    *out = entity;
    return true;
}
static bool chunk(qa_q1_game *g, qa_actor_id explosion, qa_actor_id machine, qa_error *error) {
    qa_body_state source;
    if (!qa_world_body_read(g->services.world, machine, &source, error))
        return false;
    qa_vec3 forward, up;
    qa_builtin_angle_vectors(source.angles, &forward, NULL, &up);
    q1_actor *gib;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_TIME_MACHINE_GIB], Q1_GIB, (qa_actor_id){0}, &gib, error))
        return false;
    qa_actor_id gib_id = gib->id;
    gib->physics.solid = QA_PHYSICS_NOT_SOLID;
    gib->physics.motion = QA_PHYSICS_TOSS;
    if (!q1_model(g, gib, g->runtime_names[Q1_NAME_RESOURCE_PROGS_TIMEGIB_MDL], error) ||
        !qa_world_body_read(g->services.world, machine, &source, error))
        goto fail;
    qa_body_state body = {.origin = qa_vec_sub(qa_vec_add(source.origin, qa_vec_scale(forward, 84)),
                                               qa_vec_scale(up, 136)),
                          .velocity = qa_vec_scale(up, -50),
                          .angles = source.angles};
    if (!qa_world_body_write(g->services.world, gib_id, &body, error))
        goto fail;
    gib = q1_entity(g, gib_id);
    if (!gib)
        return true;
    gib->physics.angular_velocity = qa_v3(300, 300, 300);
    if (!q1_sound_resource(g, explosion, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_R_EXP3_WAV], 1, 0, 1, error) ||
        !qa_world_body_read(g->services.world, gib_id, &body, error) ||
        !q1_effect(g, QA_BUILTIN_EXPLOSION, (qa_actor_id){0}, body.origin, 1, 0, error))
        goto fail;
    q1_actor *entity = time_actor(g, machine);
    if (entity)
        entity->frame = 1;
    gib = q1_entity(g, gib_id);
    if (!gib)
        return true;
    if (!q1_schedule(g, gib, 5, Q1_THINK_REMOVE, error))
        goto fail;
    gib = q1_entity(g, gib_id);
    if (!gib || q1_link(g, gib, error))
        return true;
fail:
    (void)qa_session_release(g->services.session, gib_id, NULL);
    return false;
}
static bool pain(qa_q1_game *g, q1_actor *machine, qa_error *error) {
    qa_actor_id id = machine->id;
    float health = q1_health(g, id);
    machine = time_actor(g, id);
    if (!machine || (health > 1100 && machine->map->cooldown > g->time))
        return true;
    if (q1_random(g) < .4f) {
        machine->map->cooldown = g->time + 2;
        float selection = q1_random(g);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        machine = time_actor(g, id);
        if (!machine)
            return true;
        qa_vec3 forward, right, up;
        qa_builtin_angle_vectors(body.angles, &forward, &right, &up);
        q1_actor *explosion;
        if (!create_time_actor(g, g->runtime_names[Q1_NAME_CLASS_TIME_MACHINE_PAIN], Q1_MAP_TIME_BOOM, id, &explosion, error))
            return false;
        qa_actor_id boom_id = explosion->id;
        machine = time_actor(g, id);
        if (!machine) {
            (void)qa_session_release(g->services.session, boom_id, NULL);
            return true;
        }
        explosion->target = machine->target;
        qa_vec3 offset =
            selection < .33f ? qa_vec_sub(qa_vec_scale(forward, 80), qa_vec_scale(up, 64))
            : selection < .66f
                ? qa_vec_sub(qa_vec_scale(right, 80), qa_vec_scale(up, 24))
                : qa_vec_sub(qa_vec_sub(qa_vec_scale(forward, 64), qa_vec_scale(up, 48)),
                             qa_vec_scale(right, 48));
        if (!qa_world_body_read(g->services.world, id, &body, error)) {
            (void)qa_session_release(g->services.session, boom_id, NULL);
            return false;
        }
        qa_body_state boom_body = {.origin = qa_vec_add(body.origin, offset)};
        if (!qa_world_body_write(g->services.world, boom_id, &boom_body, error)) {
            (void)qa_session_release(g->services.session, boom_id, NULL);
            return false;
        }
        explosion = time_actor(g, boom_id);
        if (explosion && !q1_link(g, explosion, error)) {
            (void)qa_session_release(g->services.session, boom_id, NULL);
            return false;
        }
        explosion = time_actor(g, boom_id);
        if (explosion &&
            !q1_map_schedule(g, explosion, .2 + q1_random(g) * .3, Q1_MAP_TIME_BOOM_THINK, error)) {
            (void)qa_session_release(g->services.session, boom_id, NULL);
            return false;
        }
    }
    machine = time_actor(g, id);
    if (machine && health < 1000) {
        machine->map->cooldown = 0;
        machine->map->pending.time_reaction = Q1_TIME_NO_REACTION;
        if (q1_alive(g, g->maps->world_actor))
            g->maps->rogue_cutscene = true;
    }
    return true;
}
bool qa_q1_game_time_machine_crash(qa_q1_game *g, qa_error *error) {
    q1_actor *machine = g && g->maps && q1_alive(g, g->maps->world_actor)
                            ? time_actor(g, q1_ref_actor(g, g->maps->time_machine))
                            : NULL;
    if (!machine)
        return q1_map_fail(error, "Rogue time_crash requires item_time_machine");
    qa_actor_id id = machine->id;
    if (!q1_map_damageable(g, machine, false, error))
        return false;
    machine = time_actor(g, id);
    if (!machine)
        return true;
    machine->physics.motion = QA_PHYSICS_FLY;
    machine->physics.solid = QA_PHYSICS_NOT_SOLID;
    machine->physics.angular_velocity = qa_v3(15, 0, 5);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!time_actor(g, id))
        return true;
    body.velocity = qa_v3(0, 0, -50);
    body.bounds = (qa_bounds){0};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    machine = time_actor(g, id);
    if (!machine)
        return true;
    if (!q1_map_schedule(g, machine, .1, Q1_MAP_TIME_FALL, error) ||
        !qa_builtin_resource(&g->services, "timeramp", &machine->target, error))
        return false;
    return q1_map_targets(g, machine, q1_ref_actor(g, machine->activator), error);
}
static bool boom(qa_q1_game *g, q1_actor *explosion, qa_error *error) {
    qa_actor_id id = explosion->id;
    if (!q1_map_targets(g, explosion, q1_ref_actor(g, explosion->activator), error))
        return false;
    explosion = time_actor(g, id);
    if (!explosion)
        return true;
    qa_actor_id machine_id = q1_ref_actor(g, explosion->owner);
    q1_actor *machine = time_actor(g, machine_id);
    if (!machine)
        return q1_map_fail(error, "Time machine explosion lost its machine");
    float health = q1_health(g, machine_id);
    machine = time_actor(g, machine_id);
    if (!machine)
        return q1_map_fail(error, "Time machine explosion lost its machine");
    if (health < 1250 && machine->frame > 0) {
        if (machine->skin < 2) {
            machine->frame = 2;
            machine->skin = 2;
        }
    } else if (q1_health(g, machine_id) < 1500) {
        machine = time_actor(g, machine_id);
        if (machine && machine->frame == 0) {
            if (!chunk(g, id, machine_id, error))
                return false;
            machine = time_actor(g, machine_id);
            if (machine) {
                machine->frame = 1;
                machine->skin = 1;
            }
        }
    }
    if (!time_actor(g, id))
        return true;
    if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_R_EXP3_WAV], 1, 0, 1, error))
        return false;
    bool ordinary = q1_random(g) < .5f;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (ordinary) {
        if (!q1_effect(g, QA_BUILTIN_EXPLOSION, (qa_actor_id){0}, body.origin, 1, 0, error))
            return false;
    } else {
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                                  .family = QA_GAME_Q1,
                                  .provider = g->options.provider,
                                  .time_ns = g->time_ns,
                                  .origin = body.origin,
                                  .code = 244,
                                  .count = 3};
        if (!qa_builtin_resource(&g->services, "colored-explosion", &event.resource, error) ||
            !qa_builtin_emit(&g->services, &event, error))
            return false;
    }
    explosion = time_actor(g, id);
    if (!explosion || !q1_sprite_prepare(g, explosion, error))
        return explosion == NULL;
    explosion = time_actor(g, id);
    if (!explosion)
        return true;
    if (!q1_schedule(g, explosion, .1, Q1_THINK_SPRITE, error) || !q1_link(g, explosion, error))
        return false;
    explosion = time_actor(g, id);
    if (!explosion)
        return true;
    qa_string_id target = explosion->target;
    q1_actor *stop;
    if (!create_time_actor(g, g->runtime_names[Q1_NAME_CLASS_TIME_STOP_SHAKE], Q1_MAP_TIME_STOP, (qa_actor_id){0}, &stop, error))
        return false;
    stop->target = target;
    if (q1_map_schedule(g, stop, .7, Q1_MAP_TIME_STOP_SHAKE, error))
        return true;
    (void)q1_remove(g, stop, NULL);
    return false;
}
bool q1_map_time_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (g->options.deathmatch != 0)
        return q1_remove(g, entity, error);
    bool machine = entity->map->kind == Q1_MAP_TIME_MACHINE;
    if (!q1_model(g, entity, machine ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_TIMEMACH_MDL] : g->runtime_names[Q1_NAME_RESOURCE_PROGS_TIMECORE_MDL], error))
        return false;
    entity->physics.motion = QA_PHYSICS_FLY;
    entity->physics.solid = machine ? QA_PHYSICS_BOX : QA_PHYSICS_NOT_SOLID;
    entity->physics.angular_velocity = machine ? qa_v3(0, 60, 0) : qa_v3(60, 60, 60);
    if (machine) {
        entity->max_health = 1600;
        if (!q1_map_damageable(g, entity, true, error))
            return false;
        entity = time_actor(g, id);
        if (!entity)
            return true;
        entity->physics.flags |= QA_PHYSICS_MONSTER;
        if (!qa_combat_set_health(g->services.combat, id, 1600, error))
            return false;
        entity = time_actor(g, id);
        if (!entity)
            return true;
        entity->map->pending.time_reaction = Q1_TIME_PAIN;
        if (q1_alive(g, g->maps->world_actor))
            g->maps->time_machine = q1_ref_from(g, id);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!time_actor(g, id))
            return true;
        body.bounds = (qa_bounds){{-64, -64, -144}, {64, 64, 0}};
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = time_actor(g, id);
    }
    return !entity || q1_link(g, entity, error);
}
bool q1_map_time_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                          qa_error *error) {
    if (entity->map->kind != Q1_MAP_TIME_MACHINE || (outcome->result.reaction != QA_REACTION_PAIN &&
                                                     outcome->result.reaction != QA_REACTION_DEATH))
        return true;
    switch (entity->map->pending.time_reaction) {
    case Q1_TIME_NO_REACTION:
        return true;
    case Q1_TIME_PAIN:
        return pain(g, entity, error);
    case Q1_TIME_CRASH:
        return qa_q1_game_time_machine_crash(g, error);
    }
    return q1_map_fail(error, "invalid Rogue time-machine reaction");
}
bool q1_map_time_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    qa_actor_id id = entity->id;
    if (action == Q1_MAP_TIME_BOOM_THINK)
        return boom(g, entity, error);
    if (action == Q1_MAP_TIME_CRASH_THINK)
        return qa_q1_game_time_machine_crash(g, error);
    if (action == Q1_MAP_TIME_STOP_SHAKE) {
        if (!q1_map_targets(g, entity, q1_ref_actor(g, entity->activator), error))
            return false;
        return !q1_alive(g, id) || qa_session_release(g->services.session, id, error);
    }
    if (action != Q1_MAP_TIME_FALL)
        return q1_map_fail(error, "invalid Rogue time-machine continuation");
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = time_actor(g, id);
    if (!entity)
        return true;
    if (entity->map->cooldown == 0) {
        if (body.origin.z < -20) {
            qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .time_ns = g->time_ns,
                                      .origin = qa_vec_add(body.origin, qa_v3(0, 0, -80)),
                                      .value = 1,
                                      .count = 1};
            if (!qa_builtin_resource(&g->services, "lava-splash", &event.resource, error) ||
                !qa_builtin_emit(&g->services, &event, error))
                return false;
            entity = time_actor(g, id);
            if (!entity)
                return true;
            entity->map->cooldown = 1;
        }
    } else if (q1_random(g) < .3f &&
               !q1_effect(g, QA_BUILTIN_EXPLOSION, (qa_actor_id){0}, body.origin, 1, 0, error))
        return false;
    if (!time_actor(g, id))
        return true;
    qa_vec3 velocity = body.velocity;
    velocity.z -= 5;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!time_actor(g, id))
        return true;
    body.velocity = velocity;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = time_actor(g, id);
    return !entity || q1_map_schedule(g, entity, .1, Q1_MAP_TIME_FALL, error);
}
