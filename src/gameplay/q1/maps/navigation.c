#include "internal.h"
#include <float.h>

static q1_actor *mapped(qa_q1_game *g, qa_actor_id actor) {
    q1_actor *e = q1_entity(g, actor);
    return e && e->map ? e : NULL;
}
static bool mover_kind(q1_map_kind kind, qa_q1_map_mover_kind *out) {
    switch (kind) {
    case Q1_MAP_DOOR: case Q1_MAP_BUTTON: case Q1_MAP_SECRET_DOOR:
    case Q1_MAP_ELEVATOR_BUTTON: case Q1_MAP_ROTATE_DOOR:
        *out = QA_Q1_MOVER_DOOR; return true;
    case Q1_MAP_PLAT: case Q1_MAP_ROGUE_PLAT:
        *out = QA_Q1_MOVER_ELEVATOR; return true;
    case Q1_MAP_TRAIN: case Q1_MAP_TRAIN2: case Q1_MAP_ROTATE_TRAIN:
        *out = QA_Q1_MOVER_TRAIN; return true;
    case Q1_MAP_BOBBING_WATER: case Q1_MAP_ADDON_BOB:
        *out = QA_Q1_MOVER_BOBBING; return true;
    case Q1_MAP_WALL: case Q1_MAP_GATE: case Q1_MAP_TOGGLE_WALL:
    case Q1_MAP_BREAKAWAY: case Q1_MAP_PUSHABLE: case Q1_MAP_MOVEWALL:
    case Q1_MAP_ROTATE_OBJECT: case Q1_MAP_ADDON_TOSS: case Q1_MAP_ADDON_SHATTER:
    case Q1_MAP_ADDON_DEBRIS: case Q1_MAP_ADDON_EXPLODE: case Q1_MAP_ADDON_HURT:
    case Q1_MAP_ADDON_FADE: case Q1_MAP_ADDON_ROTATE: case Q1_MAP_ADDON_BREAKABLE:
        *out = QA_Q1_MOVER_STATIC; return true;
    default: return false;
    }
}
static bool activation_traits(qa_q1_game *g, qa_actor_id actor,
                              qa_q1_map_mover_view *view, qa_error *error) {
    q1_actor *e = mapped(g, actor);
    if (!e)
        return true;
    bool useable = e->map->use_enabled;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
        return false;
    if (!mapped(g, actor))
        return true;
    view->activation = actor;
    view->useable = useable;
    view->shootable = combat.can_take_damage;
    return true;
}
static bool activator(qa_q1_game *g, qa_actor_id actor, qa_q1_map_mover_view *view,
                       qa_error *error) {
    q1_actor *e = mapped(g, actor);
    if (!e)
        return true;
    if (e->map->kind == Q1_MAP_DOOR && e->map->pending.mover.group) {
        actor = q1_ref_actor(g, e->map->pending.mover.group->members[0]);
        e = mapped(g, actor);
        if (!e)
            return true;
    } else if (e->map->kind == Q1_MAP_MOVEWALL || e->map->kind == Q1_MAP_ROTATE_OBJECT) {
        actor = q1_ref_actor(g, e->owner);
        e = mapped(g, actor);
        if (!e)
            return true;
    }
    qa_string_id name = e->targetname;
    if (!activation_traits(g, actor, view, error))
        return false;
    if (!q1_map_text(g, name))
        return true;
    qa_builtin_snapshot_frame *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    qa_builtin_snapshot_frame *queue;
    if (!q1_snapshot_actors(g, &queue, error)) {
        qa_builtin_snapshot_release(list);
        return false;
    }
    queue->snapshot.count = 0;
    bool ok = true;
    for (;;) {
        for (size_t i = 0; i < list->snapshot.count; ++i) {
            q1_actor *candidate = mapped(g, list->snapshot.ids[i]);
            if (!candidate || candidate->target != name)
                continue;
            q1_map_kind kind = candidate->map->kind;
            if (kind == Q1_MAP_BUTTON || kind == Q1_MAP_ELEVATOR_BUTTON) {
                qa_actor_id id = candidate->id;
                qa_q1_map_mover_view selected = *view;
                ok = activation_traits(g, id, &selected, error);
                if (!ok)
                    goto done;
                if (selected.useable || selected.shootable) {
                    *view = selected;
                    goto done;
                }
            } else if ((kind == Q1_MAP_RELAY || kind == Q1_MAP_DELAY) &&
                       q1_map_text(g, candidate->targetname)) {
                queue->snapshot.ids[queue->snapshot.count++] = candidate->id;
                /* Removing queued relays bounds branching paths and cycles. */
                list->snapshot.ids[i] = list->snapshot.ids[--list->snapshot.count];
                --i;
            }
        }
        if (!queue->snapshot.count)
            break;
        e = mapped(g, queue->snapshot.ids[--queue->snapshot.count]);
        name = e ? e->targetname : QA_STRING_NONE;
    }
done:
    qa_builtin_snapshot_release(queue);
    qa_builtin_snapshot_release(list);
    return ok;
}
static bool train_stops(qa_q1_game *g, qa_actor_id actor, q1_map_kind kind,
                         qa_vec3 offset, qa_nav_train_stop *stops, size_t capacity,
                         size_t *count, qa_error *error) {
    q1_actor *e = mapped(g, actor);
    if (!e)
        return true;
    if (!stops || capacity < g->capacity)
        return q1_map_fail(error, "Q1 train observation needs registry-sized stop storage");
    qa_string_id next_name = kind == Q1_MAP_ROTATE_TRAIN ? e->map->path : e->target;
    qa_actor_id node = q1_ref_actor(g, kind == Q1_MAP_ROTATE_TRAIN ? e->map->pending.rotation.goal
                       : kind == Q1_MAP_TRAIN2 ? e->map->pending.mover.goal : (q1_ref){0});
    if (!q1_alive(g, node) && !qa_targets_first(g->maps->options.targets, next_name, &node))
        return true;
    while (q1_alive(g, node) && *count < capacity) {
        for (size_t i = 0; i < *count; ++i)
            if (stops[i].id == node.slot)
                return true;
        qa_authored_target fields;
        if (!qa_targets_read(g->maps->options.targets, node, &fields))
            return q1_map_fail(error, "Q1 train observation lost its authored path");
        if (!mapped(g, actor))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, node, &body, error))
            return false;
        if (!mapped(g, actor))
            return true;
        if (!q1_alive(g, node))
            return true;
        double speed = 0, flags = 0;
        qa_targets_number(g->maps->options.targets, node, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_SPEED], &speed);
        qa_targets_number(g->maps->options.targets, node, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_SPAWNFLAGS], &flags);
        if (!mapped(g, actor))
            return true;
        if (!isfinite(speed) || fabs(speed) > FLT_MAX || !isfinite(fields.wait_seconds) ||
            !isfinite(flags) || flags < 0 || flags > UINT32_MAX || floor(flags) != flags)
            return q1_map_fail(error, "Invalid Q1 observed train path fields");
        qa_actor_id next = {0};
        qa_targets_first(g->maps->options.targets, fields.target, &next);
        if (!mapped(g, actor))
            return true;
        float wait = kind == Q1_MAP_ROTATE_TRAIN && ((uint32_t)flags & 4)
                         ? -1 : fields.wait_seconds;
        stops[(*count)++] = (qa_nav_train_stop){.id = node.slot,
            .next = q1_alive(g, next) ? next.slot : QA_NAV_NO_INDEX,
            .origin = qa_vec_sub(body.origin, offset), .wait = wait,
            .teleport = kind != Q1_MAP_TRAIN && speed == -1};
        node = next;
    }
    return true;
}
static bool mover_read(qa_q1_game *g, qa_actor_id actor, qa_q1_map_mover_view *out,
                        qa_nav_train_stop *stops, size_t capacity, size_t *count,
                        bool *found, qa_error *error) {
    q1_actor *e = mapped(g, actor);
    qa_q1_map_mover_kind kind;
    if (!e || !mover_kind(e->map->kind, &kind))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    e = mapped(g, actor);
    if (!e)
        return true;
    q1_map_state *state = e->map;
    qa_actor_id source_actor = actor;
    qa_vec3 stop_offset = body.bounds.mins;
    uint32_t inline_model = state->inline_model;
    bool has_inline_model = state->has_inline_model;
    bool enabled = !state->dormant && e->physics.solid != QA_PHYSICS_NOT_SOLID;
    if (state->kind == Q1_MAP_MOVEWALL || state->kind == Q1_MAP_ROTATE_OBJECT) {
        q1_actor *controller = mapped(g, q1_ref_actor(g, e->owner));
        if (controller && controller->map->kind == Q1_MAP_ROTATE_TRAIN) {
            source_actor = controller->id;
            qa_body_state controller_body;
            if (!qa_world_body_read(g->services.world, source_actor, &controller_body, error))
                return false;
            e = mapped(g, source_actor);
            if (!e || !mapped(g, actor))
                return true;
            stop_offset = qa_vec_sub(controller_body.origin, body.origin);
            state = e->map;
            kind = QA_Q1_MOVER_TRAIN;
        }
    }
    q1_map_kind source_kind = state->kind;
    qa_linked_body linked;
    qa_bounds bounds = qa_world_linked(g->services.world, actor, &linked)
                           ? linked.absolute_bounds : qa_bounds_translate(body.bounds, body.origin);
    qa_q1_map_mover_view view = {.actor = actor, .kind = kind,
        .inline_model = inline_model, .has_inline_model = has_inline_model,
        .navigation = {.actor = actor, .bounds = bounds, .velocity = body.velocity,
            .enabled = enabled,
            .kind = QA_NAV_ENTITY_GENERIC}};
    if (kind == QA_Q1_MOVER_ELEVATOR) {
        q1_map_movement motion = state->pending.mover;
        view.navigation.kind = QA_NAV_ENTITY_ELEVATOR;
        view.navigation.locked = source_kind == Q1_MAP_ROGUE_PLAT && motion.rogue.disabled;
        view.navigation.has_destination = motion.moving;
        view.navigation.destination = motion.destination;
        view.navigation.data.elevator.origin = body.origin;
        view.navigation.data.elevator.bottom = motion.pos2;
        view.navigation.data.elevator.top = motion.pos1;
        static const qa_nav_mover_phase phases[] = {
            QA_NAV_MOVER_BOTTOM, QA_NAV_MOVER_UP, QA_NAV_MOVER_TOP, QA_NAV_MOVER_DOWN};
        view.navigation.data.elevator.phase = phases[motion.position];
    } else if (kind == QA_Q1_MOVER_TRAIN) {
        bool rotating = source_kind == Q1_MAP_ROTATE_TRAIN;
        if (rotating && qa_actor_id_equal(source_actor, actor))
            stop_offset = qa_v3(0, 0, 0);
        view.navigation.kind = QA_NAV_ENTITY_TRAIN;
        view.navigation.has_destination = rotating ? state->pending.rotation.phase == 1
                                                   : state->pending.mover.moving;
        view.navigation.destination = rotating ? qa_vec_sub(state->pending.rotation.final_destination,
                                                             stop_offset)
                                               : state->pending.mover.destination;
        bool scheduled = rotating ? state->pending.rotation.end_time != 0
            : e->next_think >= 0 && state->action != Q1_MAP_IDLE;
        view.navigation.data.train.origin = body.origin;
        view.navigation.data.train.running = view.navigation.has_destination || scheduled;
        if (!train_stops(g, source_actor, source_kind, stop_offset,
                           stops, capacity, count, error))
            return false;
        view.navigation.data.train.stops = stops;
        view.navigation.data.train.count = *count;
    } else if (source_kind == Q1_MAP_DOOR) {
        view.navigation.locked = (e->spawnflags & 24) != 0;
        view.navigation.has_destination = state->pending.mover.moving;
        view.navigation.destination = state->pending.mover.destination;
    }
    if (!mapped(g, actor))
        return true;
    if (!activator(g, actor, &view, error))
        return false;
    if (!mapped(g, actor) || !mapped(g, source_actor) || g->destroy_pending)
        return true;
    *out = view;
    *found = true;
    return true;
}
bool qa_q1_game_map_mover_read(qa_q1_game *g, qa_actor_id actor, qa_q1_map_mover_view *out,
                              qa_nav_train_stop *stops, size_t capacity, size_t *count,
                              bool *found, qa_error *error) {
    if (!g || !out || !count || !found || (capacity && !stops))
        return q1_map_fail(error, "Invalid Q1 mover observation");
    *found = false;
    *count = 0;
    if (g->destroy_pending || !g->maps)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = mover_read(g, actor, out, stops, capacity, count, found, error);
    if (!*found)
        *count = 0;
    qa_q1_game_operation_end(&operation);
    return ok;
}
