#include "internal.h"

static q1_actor *ending_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *actor = q1_entity(g, id);
    return actor && actor->map && actor->map->kind == Q1_MAP_ENDING_ACTOR ? actor : NULL;
}
static qa_vec3 velocity_angles(qa_vec3 direction) {
    float yaw = 0, pitch;
    if (direction.x == 0 && direction.y == 0)
        pitch = direction.z > 0 ? 90 : 270;
    else {
        yaw = atan2f(direction.y, direction.x) * 57.29577951308232f;
        pitch = atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f;
    }
    return qa_v3(pitch < 0 ? pitch + 360 : pitch, yaw < 0 ? yaw + 360 : yaw, 0);
}
static bool first_target(qa_q1_game *g, const char *name, qa_actor_id *out, qa_error *error) {
    qa_string_id target;
    if (!qa_builtin_resource(&g->services, name, &target, error))
        return false;
    *out = (qa_actor_id){0};
    (void)qa_targets_first(g->maps->options.targets, target, out);
    return true;
}
static q1_actor *machine(qa_q1_game *g, qa_error *error) {
    q1_actor *entity =
        q1_alive(g, g->maps->world_actor) ? q1_entity(g, q1_ref_actor(g, g->maps->time_machine)) : NULL;
    if (!entity || !entity->map || entity->map->kind != Q1_MAP_TIME_MACHINE) {
        q1_map_fail(error, "End sequence time machine is missing");
        return NULL;
    }
    return entity;
}
static bool control_player(qa_q1_game *g, qa_actor_id player, qa_vec3 origin, qa_vec3 angles,
                           qa_error *error) {
    if (!g->maps->options.control_player)
        return q1_map_fail(error, "Q1 cinematic requires the selected player control owner");
    return g->maps->options.control_player(g->maps->options.context, player, origin, angles,
                                           qa_v3(0, 0, 0), error);
}
static bool remove_stuff(qa_q1_game *g, qa_error *error) {
    qa_string_id trail, core_name;
    if (!qa_builtin_resource(&g->services, "ltrail_start", &trail, error) ||
        !qa_builtin_resource(&g->services, "item_time_core", &core_name, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *entity = q1_entity(g, snapshot->snapshot.ids[i]);
        if (entity && entity->native && entity->classname == trail &&
            !q1_remove(g, entity, error)) {
            qa_builtin_snapshot_release(snapshot);
            return false;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    qa_actor_id core = {0};
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *entity = q1_entity(g, snapshot->snapshot.ids[i]);
        if (entity && entity->native && entity->classname == core_name) {
            core = entity->id;
            break;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    qa_body_state body = {0};
    if (core.registry && !qa_world_body_read(g->services.world, core, &body, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .time_ns = g->time_ns,
                              .origin = body.origin,
                              .code = 230,
                              .count = 5};
    if (!qa_builtin_resource(&g->services, "colored-explosion", &event.resource, error) ||
        !qa_builtin_emit(&g->services, &event, error))
        return false;
    q1_actor *entity = q1_entity(g, core);
    return !entity || q1_schedule(g, entity, .1, Q1_THINK_REMOVE, error);
}
static bool escape_lava(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q1, contents.q1_opaque_token) != -5 || !ending_actor(g, id))
        return true;
    qa_actor_id point;
    if (!first_target(g, "point1", &point, error))
        return false;
    if (!point.registry)
        return true;
    qa_body_state destination;
    if (!qa_world_body_read(g->services.world, point, &destination, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    body.origin = destination.origin;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    q1_actor *actor = ending_actor(g, id);
    return !actor || q1_link(g, actor, error);
}
static bool set_goal(qa_q1_game *g, qa_actor_id id, const char *target, qa_error *error) {
    q1_actor *actor = ending_actor(g, id);
    if (!actor)
        return true;
    qa_string_id name;
    if (!qa_builtin_resource(&g->services, target, &name, error))
        return false;
    actor->target = name;
    qa_actor_id next = {0};
    (void)qa_targets_first(g->maps->options.targets, name, &next);
    if (!next.registry) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "End sequence %s placing screwed up!", target);
        return false;
    }
    actor = ending_actor(g, id);
    if (actor)
        actor->physics.goal = actor->map->pending.follower.move_target = q1_ref_from(g, next);
    return true;
}
static bool control(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    if (!q1_alive(g, g->maps->world_actor))
        return q1_map_fail(error, "Ending requires worldspawn");
    uint8_t stage = g->maps->rogue_actor_stage;
    q1_actor *actor = ending_actor(g, id);
    if (!actor)
        return true;
    if (stage == 4) {
        actor->frame = 12;
        return q1_map_schedule(g, actor, 2, Q1_MAP_ENDING_TELEPORT, error);
    }
    if (stage == 3) {
        if (!qa_builtin_resource(&g->services, "timepod", &actor->target, error) ||
            !q1_map_targets(g, actor, q1_ref_actor(g, actor->activator), error))
            return false;
        if (!ending_actor(g, id))
            return true;
    }
    if (stage != 0 && stage != 2 && stage != 3)
        return true;
    if (!set_goal(g, id, stage == 2 ? "machine" : stage == 3 ? "point2" : "point1", error))
        return false;
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    if (stage == 2) {
        g->maps->rogue_actor_stage = 5;
        actor->map->pending.follower.fire_stage = 1;
        return q1_map_schedule(g, actor, .1, Q1_MAP_ENDING_FIRE, error);
    }
    actor->frame = 6;
    if (stage == 0)
        g->maps->rogue_actor_stage = 1;
    return q1_map_schedule(g, actor, .1, Q1_MAP_ENDING_RUN, error);
}
bool qa_q1_game_rogue_path_touch(qa_q1_game *g, qa_actor_id corner, qa_actor_id follower,
                                 bool *handled, qa_error *error) {
    if (!g || !g->maps || !handled)
        return q1_map_fail(error, "invalid Rogue path contact");
    *handled = false;
    q1_actor *actor = q1_entity(g, follower);
    if (!actor || !actor->map ||
        (actor->map->kind != Q1_MAP_ENDING_ACTOR && actor->map->kind != Q1_MAP_BUZZSAW) ||
        !q1_ref_equal(actor->map->pending.follower.move_target, q1_ref_from(g, corner)))
        return true;
    q1_map_kind kind = actor->map->kind;
    qa_authored_target fields;
    if (!qa_targets_read(g->maps->options.targets, corner, &fields))
        return true;
    qa_actor_id next = {0};
    (void)qa_targets_first(g->maps->options.targets, fields.target, &next);
    actor = q1_entity(g, follower);
    if (!actor || !actor->map || actor->map->kind != kind || !q1_alive(g, corner) ||
        !q1_ref_equal(actor->map->pending.follower.move_target, q1_ref_from(g, corner)))
        return true;
    actor->physics.goal = actor->map->pending.follower.move_target = q1_ref_from(g, next);
    qa_body_state to = {0}, from;
    if (next.registry && !qa_world_body_read(g->services.world, next, &to, error))
        return false;
    actor = q1_entity(g, follower);
    if (!actor || !actor->map || actor->map->kind != kind || (next.registry && !q1_alive(g, next)))
        return true;
    if (!qa_world_body_read(g->services.world, follower, &from, error))
        return false;
    actor = q1_entity(g, follower);
    if (!actor || !actor->map || actor->map->kind != kind || (next.registry && !q1_alive(g, next)))
        return true;
    qa_vec3 delta = qa_vec_sub(to.origin, from.origin);
    float yaw = atan2f(delta.y, delta.x) * 57.29577951308232f;
    actor->physics.ideal_yaw = yaw < 0 ? yaw + 360 : yaw;
    if (!next.registry)
        actor->map->pause_time = (float)(g->time + 999999);
    *handled = true;
    return next.registry || kind != Q1_MAP_BUZZSAW ||
           q1_map_schedule(g, actor, .1, Q1_MAP_SAW_STAND, error);
}
static bool run(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    if (!escape_lava(g, id, error))
        return false;
    q1_actor *actor = ending_actor(g, id);
    if (!actor)
        return true;
    qa_actor_id goal = q1_ref_actor(g, actor->physics.goal);
    qa_authored_target fields;
    if (qa_targets_read(g->maps->options.targets, goal, &fields)) {
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), fields.targetname);
        if (name && (!strcmp(name, "endpoint1") || !strcmp(name, "endpoint2"))) {
            actor = ending_actor(g, id);
            if (!actor)
                return true;
            if (q1_alive(g, g->maps->world_actor))
                g->maps->rogue_actor_stage = name[8] == '1' ? 2 : 4;
            return q1_map_schedule(g, actor, .1, Q1_MAP_ENDING_CONTROL, error);
        }
    }
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    if (++actor->frame > 11)
        actor->frame = 6;
    if (q1_alive(g, goal) &&
        !qa_physics_q1_move_to_goal(g->services.physics, id, goal, 15, false, error))
        return false;
    actor = ending_actor(g, id);
    return !actor || q1_map_schedule(g, actor, .1, Q1_MAP_ENDING_RUN, error);
}
static bool fire_rocket(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *target = machine(g, error);
    if (!target)
        return false;
    qa_actor_id target_id = target->id;
    q1_actor *actor = ending_actor(g, id);
    if (!actor)
        return true;
    actor->physics.goal = q1_ref_from(g, target_id);
    target->map->pending.time_reaction = Q1_TIME_CRASH;
    if (!qa_combat_set_health(g->services.combat, target_id, 1, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    qa_body_state target_body, body;
    if (!qa_world_body_read(g->services.world, target_id, &target_body, error) ||
        !qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    qa_vec3 angles = velocity_angles(qa_vec_sub(target_body.origin, body.origin));
    body.angles = angles;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    actor->effects = 2;
    angles.x = -angles.x;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward;
    actor->map->pending.follower.view_angles = angles;
    actor->map->pending.follower.rockets -= 1;
    actor->map->current_ammo = actor->map->pending.follower.rockets;
    if (!q1_sound_resource(g, id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_SGUN1_WAV], 1, 1, 1, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    qa_vec3 direction;
    if (!q1_aim(g, id, forward, &direction, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    qa_vec3 origin = qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(forward, 8)), qa_v3(0, 0, 16));
    q1_actor *missile;
    if (!q1_projectile_spawn(g, id, QA_Q1_WEAPON_COUNT, Q1_ROCKET, origin,
                             qa_vec_scale(direction, 1000), &missile, error) ||
        !q1_map_finale_emit(
            g, 4, qa_q1_finale_text(g->options.edition == QA_Q1_RERELEASE, "$qc_finale_rogue_end"),
            error))
        return false;
    return g->options.edition != QA_Q1_RERELEASE ||
           qa_q1_campaign_source_rogue_end(g->maps->options.campaign_source, error);
}
static bool fire(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *actor = ending_actor(g, id);
    if (!actor)
        return true;
    unsigned stage = actor->map->pending.follower.fire_stage;
    if (stage < 1 || stage > 21)
        return q1_map_fail(error, "invalid Rogue ending fire stage");
    actor->frame = (int32_t)(stage <= 6 ? 106 + stage : 12 + (stage - 7) % 5);
    if (stage == 1) {
        if (!fire_rocket(g, id, error))
            return false;
    } else if (stage == 2) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!ending_actor(g, id))
            return true;
        body.angles.x = 0;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        actor = ending_actor(g, id);
        if (actor)
            actor->map->pending.follower.view_angles.x = 0;
    } else if (stage == 5) {
        if (!remove_stuff(g, error))
            return false;
        q1_actor *target = machine(g, error);
        if (!target)
            return false;
        qa_actor_id target_id = target->id;
        qa_combat_state combat;
        qa_error observed = {0};
        if (!qa_combat_read(g->services.combat, target_id, &combat, &observed)) {
            if (!q1_alive(g, target_id))
                return true;
            if (error)
                *error = observed;
            return false;
        }
        target = q1_entity(g, target_id);
        if (target && combat.health > 0 &&
            !q1_map_schedule(g, target, .1, Q1_MAP_TIME_CRASH_THINK, error))
            return false;
    } else if (stage == 6)
        actor->effects = 0;
    else if (stage == 21 && q1_alive(g, g->maps->world_actor))
        g->maps->rogue_actor_stage = 3;
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    if (stage != 21)
        actor->map->pending.follower.fire_stage = (uint8_t)(stage + 1);
    return q1_map_schedule(g, actor, stage == 1 ? .1 : .15,
                           stage == 21 ? Q1_MAP_ENDING_CONTROL : Q1_MAP_ENDING_FIRE, error);
}
bool q1_map_ending_think(qa_q1_game *g, q1_actor *actor, q1_map_action action, qa_error *error) {
    qa_actor_id id = actor->id;
    switch (action) {
    case Q1_MAP_ENDING_CONTROL:
        return control(g, id, error);
    case Q1_MAP_ENDING_RUN:
        return run(g, id, error);
    case Q1_MAP_ENDING_FIRE:
        return fire(g, id, error);
    case Q1_MAP_ENDING_TELEPORT: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error) ||
            !qa_q1_spawn_teleport_fog(g, body.origin, NULL, error))
            return false;
        actor = ending_actor(g, id);
        if (!actor)
            return true;
        actor->model = QA_STRING_NONE;
        return q1_map_schedule(g, actor, 999999, Q1_MAP_IDLE, error);
    }
    case Q1_MAP_CAMERA_TRACK: {
        qa_actor_id player = q1_ref_actor(g, actor->owner), tracked = q1_ref_actor(g, g->maps->ending_actor);
        if (!ending_actor(g, tracked) || !q1_alive(g, player) ||
            !qa_world_body_storage_serial(g->services.world, player))
            return q1_remove(g, actor, error);
        qa_body_state player_body, actor_body;
        if (!qa_world_body_read(g->services.world, player, &player_body, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!ending_actor(g, tracked))
            return qa_session_release(g->services.session, id, error);
        if (!qa_world_body_read(g->services.world, tracked, &actor_body, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!ending_actor(g, tracked) || !q1_alive(g, player))
            return qa_session_release(g->services.session, id, error);
        qa_vec3 delta = qa_vec_sub(actor_body.origin, player_body.origin);
        delta.z = -delta.z;
        if (!control_player(g, player, player_body.origin, velocity_angles(delta), error))
            return false;
        actor = q1_entity(g, id);
        return !actor || q1_map_schedule(g, actor, .1, Q1_MAP_CAMERA_TRACK, error);
    }
    default:
        return q1_map_fail(error, "invalid Rogue ending continuation");
    }
}
bool q1_map_rogue_ending(qa_q1_game *g, qa_actor_id player, qa_error *error) {
    q1_map_runtime *maps = g->maps;
    if (!maps->rogue_cutscene || maps->rogue_ending_started || !q1_alive(g, maps->world_actor))
        return true;
    if (!qa_world_body_storage_serial(g->services.world, player))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player, &body, error))
        return false;
    if (maps->rogue_ending_started || !q1_alive(g, maps->world_actor) || !q1_alive(g, player))
        return true;
    maps->rogue_ending_started = true;
    qa_actor_id camera;
    if (!first_target(g, "cameraview", &camera, error))
        return false;
    bool short_ending = g->options.coop || !camera.registry;
    double exit_after = g->time + (short_ending ? 3 : 10000000);
    if (!qa_q1_level_cutscene(maps->options.level, maps->options.current_map, player, exit_after,
                              error))
        return false;
    maps->finale =
        (qa_q1_map_finale_view){.map = maps->options.current_map, .exit_after = exit_after};
    if (short_ending) {
        if (!control_player(g, player, qa_vec_add(body.origin, qa_v3(0, 0, 48)), body.angles,
                            error) ||
            !q1_map_finale_emit(
                g, 4, qa_q1_finale_text(g->options.edition == QA_Q1_RERELEASE, "$qc_finale_coop"),
                error) ||
            !remove_stuff(g, error))
            return false;
        q1_actor *target = machine(g, error);
        return target && q1_map_schedule(g, target, .1, Q1_MAP_TIME_CRASH_THINK, error);
    }
    if (!q1_map_finale_emit(g, 1, "", error))
        return false;
    if (!q1_alive(g, player))
        return true;
    q1_actor *actor;
    if (!q1_map_timer(g, g->runtime_names[Q1_NAME_CLASS_ACTOR], &actor, error))
        return false;
    qa_actor_id id = actor->id;
    actor->map->kind = Q1_MAP_ENDING_ACTOR;
    actor->owner = q1_ref_from(g, player);
    actor->max_health = 100;
    actor->physics.solid = QA_PHYSICS_BOX;
    actor->physics.motion = QA_PHYSICS_STEP;
    qa_q1_character_view view;
    qa_q1_character_pose pose = {0};
    if (qa_q1_character_read(g, player, &view))
        actor->frame = view.frame;
    else if (g->host.character_pose) {
        bool observed = g->host.character_pose(g->host.context, player, &pose);
        actor = ending_actor(g, id);
        if (!actor)
            return true;
        if (observed)
            actor->frame = pose.frame;
    }
    if (!q1_model(g, actor, g->runtime_names[Q1_NAME_RESOURCE_PROGS_PLAYER_MDL], error) ||
        !qa_combat_set_health(g->services.combat, id, 100, error))
        return false;
    if (!ending_actor(g, id))
        return true;
    qa_body_state actor_body = {
        .origin = body.origin, .angles = body.angles, .bounds = {{-16, -16, -24}, {16, 16, 32}}};
    if (!qa_world_body_write(g->services.world, id, &actor_body, error))
        return false;
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    actor->map->view_offset = qa_v3(0, 0, 25);
    actor->map->has_view_offset = true;
    actor->physics.flags |= QA_PHYSICS_MONSTER;
    actor->physics.ideal_yaw = body.angles.y;
    actor->physics.yaw_speed = 20;
    maps->ending_actor = q1_ref_from(g, id);
    if (!escape_lava(g, id, error))
        return false;
    actor = ending_actor(g, id);
    if (!actor)
        return true;
    if (!q1_map_schedule(g, actor, .1, Q1_MAP_ENDING_CONTROL, error) || !q1_link(g, actor, error))
        return false;
    qa_body_state camera_body;
    if (!qa_world_body_read(g->services.world, camera, &camera_body, error) ||
        !qa_world_body_read(g->services.world, id, &actor_body, error) ||
        !control_player(g, player, camera_body.origin,
                        velocity_angles(qa_vec_sub(actor_body.origin, camera_body.origin)), error))
        return false;
    if (!q1_alive(g, player))
        return true;
    q1_actor *tracker;
    if (!q1_map_timer(g, g->runtime_names[Q1_NAME_CLASS_ROGUE_CAMERA_TRACKER], &tracker, error))
        return false;
    tracker->map->kind = Q1_MAP_CAMERA_TRACKER;
    tracker->owner = q1_ref_from(g, player);
    return q1_map_schedule(g, tracker, .05, Q1_MAP_CAMERA_TRACK, error);
}
