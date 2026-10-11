#include "internal.h"

static bool classname(qa_q1_game *g, qa_string_id id, const char *name) {
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), id);
    return text.size == strlen(name) && !memcmp(text.data, name, text.size);
}
static const q1_addon_contact *contact(const qa_q1_game *g, qa_actor_id actor) {
    if (!g || g->destroy_pending || !g->maps || !g->maps->addon_contacts ||
        actor.slot >= g->capacity || !q1_alive((qa_q1_game *)g, actor))
        return NULL;
    const q1_addon_contact *row = &g->maps->addon_contacts[actor.slot];
    return qa_actor_id_equal(row->actor, actor) ? row : NULL;
}
bool q1_map_mg3_buddha(const qa_q1_game *g, qa_actor_id actor) {
    const q1_addon_contact *row = contact(g, actor);
    return g && g->options.program == QA_Q1_MG3 && row && row->buddha;
}
bool qa_q1_game_map_effects(const qa_q1_game *g, qa_actor_id actor, uint32_t *out) {
    if (!out || !g || g->destroy_pending || !q1_alive((qa_q1_game *)g, actor))
        return false;
    const q1_addon_contact *row = contact(g, actor);
    *out = row ? row->effects : 0;
    return true;
}
static bool cleanup_markers(qa_q1_game *g, const char *name, qa_error *error) {
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && !g->destroy_pending && i < snapshot->snapshot.count; ++i) {
        q1_actor *marker = q1_entity(g, snapshot->snapshot.ids[i]);
        if (marker && marker->native && classname(g, marker->classname, name))
            ok = q1_remove(g, marker, error);
    }
    qa_builtin_snapshot_release(snapshot);
    return ok;
}
bool q1_map_mg3_impulse(qa_q1_game *g, qa_actor_id actor, uint8_t impulse, bool *handled,
                       qa_error *error) {
    if (g->options.program != QA_Q1_MG3 ||
        (impulse != 116 && impulse != 117 && impulse != 119 && impulse != 121 && impulse != 220))
        return true;
    *handled = true;
    if (!g->maps)
        return q1_map_fail(error, "MG3 player commands require the authored map owner");
    q1_addon_contact *row = q1_map_addon_contact(g, actor, true, error);
    if (!row)
        return false;
    switch (impulse) {
    case 116:
        row->secret_hunter = !row->secret_hunter;
        return row->secret_hunter || cleanup_markers(g, "secret_marker", error);
    case 117:
        row->exit_hunter = !row->exit_hunter;
        return row->exit_hunter || cleanup_markers(g, "exit_marker", error);
    case 119:
        row->monster_hunter = !row->monster_hunter;
        return true;
    case 121:
        row->buddha = !row->buddha;
        return true;
    case 220: {
        row->effects |= 8u;
        q1_actor *native = q1_entity(g, actor);
        if (native)
            native->effects |= 8u;
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                                  .provider = g->options.provider, .actor = actor,
                                  .flags = native ? native->effects : row->effects,
                                  .time_ns = g->time_ns};
        return qa_builtin_resource(&g->services, "actor-effects", &event.resource, error) &&
               qa_builtin_emit(&g->services, &event, error);
    }
    }
    return true;
}
static bool target_class(qa_q1_game *g, qa_actor_id target, const char *name) {
    qa_authored_target fields;
    return q1_alive(g, target) && qa_targets_read(g->maps->options.targets, target, &fields) &&
           !g->destroy_pending && q1_alive(g, target) && classname(g, fields.classname, name);
}
static bool marker_update(qa_q1_game *g, qa_actor_id player, qa_actor_id target,
                           qa_vec3 eye, bool secret, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, target, &body, error))
        return false;
    if (!q1_alive(g, player) || !q1_alive(g, target))
        return true;
    qa_vec3 middle = qa_vec_add(body.origin,
                                qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
    qa_trace_result trace;
    if (!q1_trace(g, eye, middle, player, false, &trace, error))
        return false;
    if (!q1_alive(g, player) || !q1_alive(g, target))
        return true;
    q1_addon_contact *row = q1_map_addon_contact(g, target, true, error);
    if (!row)
        return false;
    qa_actor_id id = q1_ref_actor(g, secret ? row->secret_marker : row->exit_marker);
    qa_string_id name = secret ? g->runtime_names[Q1_NAME_CLASS_SECRET_MARKER] : g->runtime_names[Q1_NAME_CLASS_EXIT_MARKER];
    q1_actor *marker = q1_entity(g, id);
    if (marker && (!marker->native || !q1_ref_equal(marker->owner, q1_ref_from(g, target)) ||
                   marker->classname != name))
        return q1_map_fail(error, "MG3 marker continuation has a different owner");
    bool created = !marker;
    if (created) {
        if (!q1_create(g, name, Q1_MAP, target, &marker, error))
            return false;
        id = marker->id;
        if (!q1_map_allocate(g, marker, error)) {
            (void)q1_remove(g, marker, NULL);
            return false;
        }
        marker->map->kind = Q1_MAP_POINT;
        marker->physics.motion = QA_PHYSICS_STATIONARY;
        marker->physics.solid = QA_PHYSICS_NOT_SOLID;
        if (!q1_model(g, marker, g->runtime_names[Q1_NAME_RESOURCE_PROGS_S_BUBBLE_SPR], error)) {
            if (q1_alive(g, id))
                (void)q1_remove(g, marker, NULL);
            return false;
        }
    }
    marker = q1_entity(g, id);
    if (!q1_alive(g, player) || !q1_alive(g, target) || !marker) {
        if (marker && created)
            (void)q1_remove(g, marker, NULL);
        return true;
    }
    body = (qa_body_state){.origin = qa_vec_sub(trace.end,
        qa_vec_scale(qa_vec_normalize(qa_vec_sub(middle, eye)), 4))};
    if (!qa_world_body_write(g->services.world, id, &body, error)) {
        if (created && q1_alive(g, id))
            (void)q1_remove(g, marker, NULL);
        return false;
    }
    marker = q1_entity(g, id);
    if (!marker || !q1_alive(g, player) || !q1_alive(g, target)) {
        if (marker && created)
            (void)q1_remove(g, marker, NULL);
        return true;
    }
    if (!q1_link(g, marker, error)) {
        if (created && q1_alive(g, id))
            (void)q1_remove(g, marker, NULL);
        return false;
    }
    marker = q1_entity(g, id);
    if (marker && !q1_alive(g, target))
        return q1_remove(g, marker, error);
    row = q1_map_addon_contact(g, target, false, NULL);
    if (row && q1_alive(g, id)) {
        if (secret)
            row->secret_marker = q1_ref_from(g, id);
        else
            row->exit_marker = q1_ref_from(g, id);
    }
    return true;
}
static bool marker_frame(qa_q1_game *g, qa_actor_id player, qa_vec3 eye,
                          qa_builtin_snapshot_frame *snapshot, bool secret, qa_error *error) {
    const char *target_name = secret ? "trigger_secret" : "trigger_changelevel";
    const char *marker_name = secret ? "secret_marker" : "exit_marker";
    for (size_t i = 0; i < snapshot->snapshot.count && q1_alive(g, player); ++i) {
        qa_actor_id id = snapshot->snapshot.ids[i];
        q1_actor *marker = q1_entity(g, id);
        if (marker && marker->native && classname(g, marker->classname, marker_name) &&
            !target_class(g, q1_ref_actor(g, marker->owner), target_name)) {
            marker = q1_entity(g, id);
            if (marker && !q1_remove(g, marker, error))
                return false;
        }
        if (target_class(g, id, target_name) &&
            !marker_update(g, player, id, eye, secret, error))
            return false;
    }
    return true;
}
static bool monster_frame(qa_q1_game *g, qa_actor_id player, qa_builtin_snapshot_frame *snapshot,
                           qa_error *error) {
    qa_string_id resource;
    if (!qa_builtin_resource(&g->services, "debug-bounds", &resource, error))
        return false;
    for (size_t i = 0; i < snapshot->snapshot.count && q1_alive(g, player); ++i) {
        qa_actor_id id = snapshot->snapshot.ids[i];
        q1_actor *native = q1_entity(g, id);
        bool active = native && native->native && (native->physics.flags & QA_PHYSICS_MONSTER);
        bool waiting = native && native->native && native->kind == Q1_MONSTER &&
                       native->state.monster.addon.enabled && native->state.monster.addon.waiting;
        if ((!native || !native->native) && g->services.actor_traits) {
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits(g->services.context, id, &traits))
                active = traits.monster;
        }
        bool living = active && q1_health(g, id) > 0;
        if (!living && !waiting)
            continue;
        if (!q1_alive(g, player) || !q1_alive(g, id))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!q1_alive(g, player) || !q1_alive(g, id))
            continue;
        qa_bounds bounds = !living
                               ? (qa_bounds){qa_v3(-16, -16, -16), qa_v3(16, 16, 16)}
                               : body.bounds;
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                                  .provider = g->options.provider, .actor = id, .other = player,
                                  .resource = resource, .time_ns = g->time_ns,
                                  .origin = qa_vec_add(body.origin, bounds.mins),
                                  .end = qa_vec_add(body.origin, bounds.maxs),
                                  .code = !living ? 244 : 251};
        if (!qa_builtin_emit(&g->services, &event, error))
            return false;
    }
    return true;
}
bool qa_q1_game_map_addon_player_frame(qa_q1_game *g, qa_actor_id actor, qa_vec3 view_offset,
                                       qa_error *error) {
    if (!g || !qa_vec_finite(view_offset))
        return q1_map_fail(error, "invalid MG3 player frame");
    if (g->options.program != QA_Q1_MG3)
        return true;
    const q1_addon_contact *row = contact(g, actor);
    if (!row || (!row->secret_hunter && !row->exit_hunter && !row->monster_hunter))
        return true;
    bool secret = row->secret_hunter, exit = row->exit_hunter, monster = row->monster_hunter;
    qa_q1_game_operation operation;
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_body_state body = {0};
    qa_builtin_snapshot_frame *snapshot = NULL;
    bool ok = qa_world_body_read(g->services.world, actor, &body, error);
    if (ok && q1_alive(g, actor))
        ok = q1_snapshot_actors(g, &snapshot, error);
    qa_vec3 eye = qa_vec_add(body.origin, view_offset);
    if (ok && snapshot) {
        ok = (!secret || marker_frame(g, actor, eye, snapshot, true, error)) &&
             (!exit || marker_frame(g, actor, eye, snapshot, false, error)) &&
             (!monster || monster_frame(g, actor, snapshot, error));
        qa_builtin_snapshot_release(snapshot);
    }
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "MG3 map source retired during player frame");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
