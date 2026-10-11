#include "internal.h"

static q1_actor *authored(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_horde(e->map->kind) ? e : NULL;
}
bool q1_map_horde_present(qa_q1_game *g) {
    if (!g->maps || g->destroy_pending)
        return false;
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    while (qa_targets_next_authored(g->maps->options.targets, g->runtime_names[Q1_NAME_HORDE_MANAGER], &cursor, &id)) {
        q1_actor *manager = authored(g, id);
        if (manager && manager->native && manager->map->kind == Q1_MAP_HORDE_MANAGER)
            return true;
    }
    return false;
}
bool qa_q1_game_map_horde_manager_read(qa_q1_game *g, qa_actor_id id,
                                      qa_string_id *target, qa_actor_id *activator,
                                      bool *found, qa_error *error) {
    if (!g || !target || !activator || !found)
        return q1_map_fail(error, "Invalid Q1 Horde live manager observation");
    *found = false;
    if (!g->maps || g->destroy_pending)
        return true;
    q1_actor *e = authored(g, id);
    if (!e || !e->native || e->map->kind != Q1_MAP_HORDE_MANAGER)
        return true;
    *target = e->target;
    *activator = q1_ref_actor(g, e->activator);
    *found = true;
    return true;
}
bool q1_map_horde_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    if (kind == Q1_MAP_HORDE_MANAGER) {
        e->map->pending.horde_start_flags = *g->maps->options.server_flags;
        e->wait = 1;
        e->delay = 9;
        e->map->use_enabled = true;
        if (!q1_map_text(g, e->target) &&
            !qa_builtin_resource(&g->services, "horde_event", &e->target, error))
            return false;
        e = authored(g, id);
        if (!e)
            return true;
        if (!qa_builtin_resource(&g->services, "horde_manager", &e->targetname, error))
            return false;
        qa_targets_changed(g->maps->options.targets, e->id);
        qa_string_id name;
        float enabled = 0;
        if (!q1_source_value(g, QA_Q1_SOURCE_HORDE, 0, &enabled, error))
            return false;
        if (!authored(g, id) || enabled != 0 || g->options.deathmatch != 0)
            return true;
        if (!g->maps->options.server_command)
            return q1_map_fail(error, "Q1 authored Horde requires its server cvar owner");
        return qa_builtin_resource(&g->services, "horde 1", &name, error) &&
               g->maps->options.server_command(g->maps->options.context, name, error);
    }
    e->wait = 0;
    if (kind <= Q1_MAP_HORDE_BOSS) {
        e->map->use_enabled = true;
        float width = kind == Q1_MAP_HORDE_RANGED || kind == Q1_MAP_HORDE_BOSS ? 44 : 80;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!authored(g, id))
            return true;
        body.bounds = (qa_bounds){{-width, -width, 0}, {width, width, 128}};
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
    }
    return true;
}
bool q1_map_horde_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!g->maps->options.horde_control)
        return q1_map_fail(error, "Q1 Horde authored control requires the selected mode owner");
    if (e->map->kind != Q1_MAP_HORDE_MANAGER)
        e->spawnflags ^= 1u;
    return g->maps->options.horde_control(g->maps->options.context, e->id,
                                         e->map->kind == Q1_MAP_HORDE_MANAGER, error);
}
static bool layout(qa_q1_game *g, qa_horde_options *out, qa_horde_point *points,
                    size_t capacity, size_t *count, bool *found, qa_error *error) {
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    q1_actor *manager = NULL;
    while (qa_targets_next_authored(g->maps->options.targets, g->runtime_names[Q1_NAME_HORDE_MANAGER], &cursor, &id)) {
        q1_actor *e = authored(g, id);
        if (e && e->map->kind == Q1_MAP_HORDE_MANAGER) {
            manager = e;
            break;
        }
    }
    if (!manager)
        return true;
    qa_actor_id manager_id = manager->id;
    qa_horde_options options = {.manager = manager_id, .target = manager->target,
                                .starting_campaign_flags = manager->map->pending.horde_start_flags,
                                .skill = g->options.skill, .world_type = g->options.world_type,
                                .cooperative = g->options.coop};
    cursor = (qa_target_cursor){0};
    while (qa_targets_next_authored(g->maps->options.targets, 0, &cursor, &id)) {
        q1_actor *e = authored(g, id);
        if (!e || e->map->kind == Q1_MAP_HORDE_MANAGER)
            continue;
        if (*count >= capacity)
            return q1_map_fail(error, "Q1 Horde point observation storage is too small");
        qa_horde_point point = {.actor = id, .kind = (qa_horde_point_kind)(e->map->kind - Q1_MAP_HORDE_NORMAL),
                                .flags = e->spawnflags, .target = e->target};
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!authored(g, manager_id) || g->destroy_pending)
            return true;
        if (!authored(g, id))
            continue;
        point.origin = body.origin;
        point.angles = body.angles;
        points[(*count)++] = point;
    }
    if (!authored(g, manager_id) || g->destroy_pending)
        return true;
    *out = options;
    *found = true;
    return true;
}
bool qa_q1_game_map_horde_read(qa_q1_game *g, qa_horde_options *out, qa_horde_point *points,
                              size_t capacity, size_t *count, bool *found, qa_error *error) {
    if (!g || !out || !count || !found || (capacity && !points))
        return q1_map_fail(error, "Invalid Q1 Horde authored observation");
    *count = 0;
    *found = false;
    if (!g->maps || g->destroy_pending)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = layout(g, out, points, capacity, count, found, error);
    if (!*found)
        *count = 0;
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_game_map_horde_point_read(qa_q1_game *g, qa_actor_id id, qa_horde_point *out,
                                    bool *found, qa_error *error) {
    if (!g || !out || !found)
        return q1_map_fail(error, "Invalid Q1 Horde live point observation");
    *found = false;
    q1_actor *e = authored(g, id);
    if (!e || e->map->kind == Q1_MAP_HORDE_MANAGER)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_body_state body;
    bool ok = qa_world_body_read(g->services.world, id, &body, error);
    e = authored(g, id);
    if (ok && e && e->map->kind != Q1_MAP_HORDE_MANAGER) {
        out->actor = id;
        out->kind = (qa_horde_point_kind)(e->map->kind - Q1_MAP_HORDE_NORMAL);
        out->origin = body.origin;
        out->angles = body.angles;
        out->target = e->target;
        out->flags = e->spawnflags;
        *found = true;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
