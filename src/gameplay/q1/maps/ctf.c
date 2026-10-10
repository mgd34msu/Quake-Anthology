#include "internal.h"

static q1_actor *exit_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = g->destroy_pending ? NULL : q1_entity(g, id);
    return entity && entity->native && entity->map && q1_map_is_ctf(entity->map->kind)
               ? entity : NULL;
}
static bool state_read(qa_q1_game *g, qa_actor_id actor, qa_q1_ctf_map_state *out,
                         qa_error *error) {
    *out = (qa_q1_ctf_map_state){0};
    return g->maps->options.ctf_state
               ? g->maps->options.ctf_state(g->maps->options.context, actor, out, error)
               : q1_map_fail(error, "ThreeWave map requires its selected mode state owner");
}
static bool announce(qa_q1_game *g, qa_actor_id actor, const char *text,
                      qa_string_id map_name, qa_error *error) {
    qa_builtin_player_info info = {0};
    if (g->services.player_info)
        g->services.player_info(g->services.context, actor, &info);
    if (!q1_alive(g, actor))
        return true;
    qa_builtin_message_arg arguments[2] = {{.kind = QA_BUILTIN_MESSAGE_STRING},
                                           {.kind = QA_BUILTIN_MESSAGE_STRING,
                                            .value.text = map_name}};
    if (!qa_builtin_resource(&g->services, info.name ? info.name : "",
                               &arguments[0].value.text, error))
        return false;
    return q1_message_args(g, (qa_actor_id){0}, text, arguments,
                             q1_map_text(g, map_name) ? 2 : 1, error);
}
static bool vote_teleport(qa_q1_game *g, qa_actor_id trigger, qa_actor_id player,
                           qa_error *error) {
    q1_actor *entity = exit_actor(g, trigger);
    if (!entity || !q1_alive(g, player))
        return true;
    qa_actor_id target;
    if (!qa_targets_first(g->maps->options.targets, entity->target, &target))
        return q1_map_fail(error, "ThreeWave vote exit target is missing");
    if (!exit_actor(g, trigger) || !q1_alive(g, player) || !q1_alive(g, target))
        return true;
    qa_body_state destination, body;
    if (!qa_world_body_read(g->services.world, target, &destination, error) ||
        !qa_world_body_read(g->services.world, player, &body, error))
        return false;
    if (!exit_actor(g, trigger) || !q1_alive(g, player) || !q1_alive(g, target))
        return true;
    const q1_actor *native = q1_entity_const(g, target);
    qa_vec3 angles = native && native->map ? native->map->mangle : destination.angles;
    qa_vec3 forward;
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    if (!qa_q1_spawn_teleport_fog(g, body.origin, NULL, error))
        return false;
    if (!q1_alive(g, player) || !q1_alive(g, target))
        return true;
    if (!qa_q1_spawn_teleport_fog(g,
            qa_vec_add(destination.origin, qa_vec_scale(forward, 32)), NULL, error))
        return false;
    if (!q1_alive(g, player) || !q1_alive(g, target))
        return true;
    if (!qa_q1_spawn_teledeath(g, destination.origin, player, NULL, error))
        return false;
    if (!q1_alive(g, player))
        return true;
    if (!qa_world_body_read(g->services.world, player, &body, error))
        return false;
    if (!q1_alive(g, player))
        return true;
    body.origin = destination.origin;
    body.angles = angles;
    body.velocity = qa_vec_scale(forward, 300);
    body.ground = (qa_actor_reference){0};
    if (!qa_world_body_write(g->services.world, player, &body, error))
        return false;
    if (!q1_alive(g, player))
        return true;
    if (!qa_world_link(g->services.world, player, NULL, error))
        return false;
    if (!q1_alive(g, player) || !g->services.motion_changed)
        return true;
    qa_builtin_motion_change motion = {.reason = QA_BUILTIN_MOTION_TELEPORT,
        .body = body, .view_angles = angles, .force_view_angles = true,
        .hold_ns = UINT64_C(700000000)};
    return g->services.motion_changed(g->services.context, player, &motion, error);
}
static bool vote_touch(qa_q1_game *g, qa_actor_id id, qa_actor_id actor, qa_error *error) {
    q1_addon_contact *row = q1_map_addon_contact(g, actor, true, error);
    if (!row)
        return false;
    if (row->voted != 0) {
        bool message = row->voted < g->time;
        row->voted = g->time + 1;
        if (message && !q1_message(g, actor, "$qc_ctf_already_voted", error))
            return false;
        return vote_teleport(g, id, actor, error);
    }
    row->voted = g->time + 1;
    q1_actor *entity = exit_actor(g, id);
    if (!entity || !q1_map_targets(g, entity, actor, error))
        return entity == NULL;
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    if (!announce(g, actor, "$qc_ctf_has_voted", entity->message, error))
        return false;
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    ++entity->count;
    qa_actor_id leader = {0};
    float most = 0;
    qa_builtin_snapshot_frame *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    for (size_t i = 0; !g->destroy_pending && i < list->snapshot.count; ++i) {
        q1_actor *candidate = exit_actor(g, list->snapshot.ids[i]);
        if (candidate && candidate->map->kind == Q1_MAP_CTF_VOTE_EXIT &&
            !qa_actor_id_equal(candidate->id, id) && candidate->count > most) {
            most = candidate->count;
            leader = candidate->id;
        }
    }
    qa_builtin_snapshot_release(list);
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    if (entity->count > most || (entity->count == most && q1_random(g) > .5f))
        leader = id;
    g->maps->ctf_vote_leader = q1_ref_from(g, leader);
    if (leader.registry && g->maps->ctf_vote_exit_time == 0)
        g->maps->ctf_vote_exit_time = g->time + 60;
    return vote_teleport(g, id, actor, error);
}
bool q1_map_ctf_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (entity->map->kind == Q1_MAP_CTF_VOTE_EXIT) {
        if (!q1_map_text(g, entity->target))
            return q1_map_fail(error, "ThreeWave vote exit has no target");
        entity->count = 0;
    } else if (!q1_map_text(g, entity->map->map))
        return q1_map_fail(error, "ThreeWave changelevel trigger has no map");
    if (!q1_map_trigger_init(g, entity, true, error))
        return false;
    entity = exit_actor(g, id);
    if (!entity)
        return true;
    entity->map->touch_enabled = true;
    return q1_link(g, entity, error);
}
bool q1_map_ctf_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id actor, qa_error *error) {
    qa_actor_id id = entity->id;
    bool vote = entity->map->kind == Q1_MAP_CTF_VOTE_EXIT;
    if (!q1_map_player(g, actor) || (vote && q1_health(g, actor) <= 0))
        return true;
    qa_q1_ctf_map_state state;
    if (!state_read(g, actor, &state, error))
        return false;
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    if (vote)
        return state.observer || vote_touch(g, id, actor, error);
    float noexit = 0;
    if (!q1_source_value(g, QA_Q1_SOURCE_NOEXIT, 0, &noexit, error))
        return false;
    if (!isfinite(noexit))
        return q1_map_fail(error, "nonfinite ThreeWave noexit cvar");
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor) || noexit == 1 || (noexit == 2 && !state.start_map))
        return true;
    if (!announce(g, actor, "$qc_exited", QA_STRING_NONE, error))
        return false;
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    if (!q1_map_targets(g, entity, actor, error))
        return false;
    entity = exit_actor(g, id);
    if (!entity || !q1_alive(g, actor))
        return true;
    if ((entity->spawnflags & 1u) && !g->options.deathmatch)
        return qa_q1_level_travel(g->maps->options.level, entity->map->map, actor, error);
    entity->map->touch_enabled = false;
    return q1_map_schedule(g, entity, .1, Q1_MAP_CTF_NEXTLEVEL, error);
}
static bool nextlevel(qa_q1_game *g, qa_string_id map, qa_error *error) {
    if (!g->maps->options.level || !g->maps->options.ctf_pregame_end)
        return q1_map_fail(error, "ThreeWave nextlevel requires campaign and mode owners");
    if (!g->maps->options.ctf_pregame_end(g->maps->options.context, error))
        return false;
    if (!q1_alive(g, g->maps->world_actor))
        return true;
    q1_actor *helper;
    if (!q1_create(g, "ctf_nextlevel", Q1_MAP, (qa_actor_id){0}, &helper, error))
        return false;
    qa_actor_id id = helper->id;
    if (!q1_map_allocate(g, helper, error)) {
        (void)q1_remove(g, helper, NULL);
        return false;
    }
    helper->map->kind = Q1_MAP_DELAY;
    helper->map->map = map;
    if (q1_map_schedule(g, helper, .1, Q1_MAP_CTF_NEXTLEVEL, error))
        return true;
    helper = q1_entity(g, id);
    if (helper)
        (void)q1_remove(g, helper, NULL);
    return false;
}
bool qa_q1_game_map_ctf_nextlevel(qa_q1_game *g, qa_string_id map, qa_error *error) {
    if (!g || g->options.program != QA_Q1_CTF || !g->maps ||
        !q1_alive(g, g->maps->world_actor) || !q1_map_text(g, map))
        return q1_map_fail(error, "invalid ThreeWave nextlevel request");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = nextlevel(g, map, error);
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "ThreeWave source retired during nextlevel");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_map_ctf_frame(qa_q1_game *g, qa_error *error) {
    if (g->options.program != QA_Q1_CTF || !g->maps || !q1_alive(g, g->maps->world_actor))
        return true;
    const qa_q1_level_state *level = qa_q1_level_read(g->maps->options.level);
    if (level && level->intermission)
        return true;
    qa_q1_ctf_map_state state;
    if (!state_read(g, (qa_actor_id){0}, &state, error))
        return false;
    if (!q1_alive(g, g->maps->world_actor) || state.pregame_over || !state.start_map ||
        g->maps->ctf_vote_exit_time == 0 || g->time <= g->maps->ctf_vote_exit_time)
        return true;
    q1_actor *leader = exit_actor(g, q1_ref_actor(g, g->maps->ctf_vote_leader));
    return !leader || leader->map->kind != Q1_MAP_CTF_VOTE_EXIT ||
           nextlevel(g, leader->map->map, error);
}
bool q1_map_ctf_nextlevel_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (g->options.program != QA_Q1_CTF || !entity->map ||
        (entity->map->kind != Q1_MAP_DELAY && entity->map->kind != Q1_MAP_CTF_CHANGELEVEL) ||
        !g->maps->options.level)
        return q1_map_fail(error, "invalid ThreeWave nextlevel continuation");
    qa_actor_id id = entity->id;
    if (!qa_q1_level_begin(g->maps->options.level, entity->map->map, (qa_actor_id){0},
                             g->time, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || q1_remove(g, entity, error);
}
