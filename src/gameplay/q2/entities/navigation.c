#include "internal.h"

typedef struct mover_query {
    qa_q2_game *game;
    qa_q2_map_mover_view *out;
    qa_nav_train_stop *stops;
    size_t capacity, *count;
    bool *found;
} mover_query;

static bool observation_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

static bool mover_kind(const q2_entity_state *s, qa_q2_map_mover_kind *out) {
    switch (s->kind) {
    case Q2E_DOOR: case Q2E_BUTTON: case Q2E_SECRET_DOOR: case Q2E_WATER:
        *out = QA_Q2_MOVER_DOOR; return true;
    case Q2E_PLAT: *out = QA_Q2_MOVER_ELEVATOR; return true;
    case Q2E_TRAIN: *out = QA_Q2_MOVER_TRAIN; return true;
    case Q2E_ROTATING: case Q2E_CONVEYOR: case Q2E_OBJECT: case Q2E_FORCEWALL:
        *out = QA_Q2_MOVER_STATIC; return true;
    case Q2E_SCENERY:
        if (s->scenery != Q2S_WALL && s->scenery != Q2S_EXPLOSIVE) return false;
        *out = QA_Q2_MOVER_STATIC; return true;
    default: return false;
    }
}

static bool activation_traits(qa_q2_game *g, qa_actor_id id,
                                qa_q2_map_mover_view *view, qa_error *error) {
    q2_actor *a = q2_ent(g, id);
    if (!a) return true;
    bool usable = a->entity->usable;
    qa_combat_state combat = {0};
    qa_error absent = {0};
    if (!qa_combat_read_traits(g->services.combat, id, &combat, &absent) &&
        absent.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = absent;
        return false;
    }
    if (!q2_ent(g, id)) return true;
    view->activation = id;
    view->useable = usable;
    view->shootable = combat.can_take_damage;
    return true;
}

static bool activation(qa_q2_game *g, qa_actor_id controller,
                         qa_q2_map_mover_view *view, qa_error *error) {
    q2_actor *a = q2_ent(g, controller);
    if (!a) return true;
    qa_string_id name = a->entity->targetname;
    if (!activation_traits(g, controller, view, error)) return false;
    if (!name || !q2_ent(g, controller)) return true;
    qa_builtin_snapshot_frame *list = q2_scratch_acquire(g, error);
    if (!list) return false;
    qa_builtin_snapshot_frame *queue = q2_scratch_acquire(g, error);
    if (!queue) { qa_builtin_snapshot_release(list); return false; }
    bool ok = qa_builtin_observations(&g->services, &list->snapshot, error) &&
        qa_builtin_snapshot_reserve(&queue->snapshot, list->snapshot.count + 1, error);
    queue->snapshot.count = ok ? 1 : 0;
    if (ok) { queue->snapshot.ids[0] = controller; queue->snapshot.sort[0] = (qa_actor_id){0}; }
    size_t position = 0;
    while (ok && position < queue->snapshot.count && q2_ent(g, controller)) {
        qa_actor_id parent = queue->snapshot.ids[position++];
        a = q2_ent(g, parent);
        name = a ? a->entity->targetname : 0;
        if (!name) continue;
        for (size_t i = 0; i < list->snapshot.count; ++i) {
            qa_actor_id id = list->snapshot.ids[i];
            q2_actor *candidate = q2_ent(g, id);
            if (!candidate || candidate->entity->target != name) continue;
            q2_entity_state *s = candidate->entity;
            if (s->kind == Q2E_BUTTON) {
                qa_q2_map_mover_view chosen = *view;
                ok = activation_traits(g, id, &chosen, error);
                if (!ok) break;
                if (chosen.useable || chosen.shootable) {
                    qa_actor_id path = parent;
                    for (size_t step = 0; path.registry && step < queue->snapshot.count; ++step) {
                        q2_actor *part = q2_ent(g, path);
                        if (part && part->entity->kind == Q2E_KEY && part->entity->usable)
                            chosen.navigation.locked = true;
                        size_t index = 0;
                        while (index < queue->snapshot.count &&
                               !qa_actor_id_equal(queue->snapshot.ids[index], path)) ++index;
                        path = index < queue->snapshot.count ? queue->snapshot.sort[index] : (qa_actor_id){0};
                    }
                    *view = chosen; position = queue->snapshot.count; name = 0; break;
                }
            } else if ((s->kind == Q2E_RELAY || s->kind == Q2E_COUNTER || s->kind == Q2E_KEY ||
                        s->kind == Q2E_ELEVATOR || s->kind == Q2E_COOP_RELAY) && s->targetname) {
                queue->snapshot.sort[queue->snapshot.count] = parent;
                queue->snapshot.ids[queue->snapshot.count++] = id;
                list->snapshot.ids[i] = list->snapshot.ids[--list->snapshot.count];
                --i;
            }
        }
    }
    qa_builtin_snapshot_release(queue);
    qa_builtin_snapshot_release(list);
    return ok;
}

static bool unique_target(qa_q2_game *g, qa_string_id name, qa_actor_id *out,
                            bool *ambiguous) {
    qa_targets *targets = g->entity_runtime->services.targets;
    qa_target_cursor cursor = {0};
    if (!targets || !name || !qa_targets_next(targets, name, &cursor, out)) return false;
    qa_actor_id other;
    if (qa_targets_next(targets, name, &cursor, &other)) { *ambiguous = true; return false; }
    return true;
}

static bool route(mover_query *query, qa_actor_id controller, qa_vec3 offset,
                     qa_q2_map_mover_view *view, qa_error *error) {
    qa_q2_game *g = query->game;
    q2_actor *a = q2_ent(g, controller);
    qa_actor_id node = a->entity->mover->destination;
    if (!q2_actor_live(g, node) && !unique_target(g, a->entity->target, &node, &view->route_ambiguous)) return true;
    while (q2_actor_live(g, node) && *query->count < query->capacity) {
        for (size_t i = 0; i < *query->count; ++i)
            if (query->stops[i].id == node.slot) return true;
        qa_authored_target fields;
        if (!qa_targets_read(g->entity_runtime->services.targets, node, &fields)) return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, node, &body, error)) return false;
        if (!q2_ent(g, controller) || !q2_actor_live(g, node)) return true;
        uint32_t flags = q2_actor_field_flags(g, node, g->field_keys[QA_TARGET_KEY_SPAWNFLAGS]);
        if (!q2_ent(g, controller) || !q2_actor_live(g, node)) return true;
        qa_actor_id next = {0};
        bool has_next = unique_target(g, fields.target, &next, &view->route_ambiguous);
        if (!q2_ent(g, controller) || !q2_actor_live(g, node)) return true;
        query->stops[(*query->count)++] = (qa_nav_train_stop){.id = node.slot,
            .next = has_next ? next.slot : QA_NAV_NO_INDEX, .wait = fields.wait_seconds,
            .origin = qa_vec_add(body.origin, offset), .teleport = (flags & 1u) != 0};
        if (!has_next) return true;
        node = next;
    }
    if (q2_actor_live(g, node)) view->route_ambiguous = true;
    return true;
}

static bool inspect(void *opaque, qa_actor_id actor, qa_error *error) {
    mover_query *query = opaque;
    qa_q2_game *g = query->game;
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_actor *a = q2_ent(g, actor);
    qa_q2_map_mover_kind kind;
    if (!a || !mover_kind(a->entity, &kind)) return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error)) return false;
    a = q2_ent(g, actor);
    if (!a) return true;
    qa_q2_map_mover_view view = {.actor = actor, .controller = actor, .kind = kind,
        .inline_model = a->entity->collision.model, .has_inline_model = a->entity->has_inline,
        .navigation = {.actor = actor, .enabled = a->physics.solid != QA_PHYSICS_NOT_SOLID,
            .bounds = qa_bounds_translate(body.bounds, body.origin), .velocity = body.velocity}};
    qa_linked_body linked;
    if (qa_world_linked(g->services.world, actor, &linked)) view.navigation.bounds = linked.absolute_bounds;
    if (qa_actor_reference_present(a->entity->team_master) && q2_ent(g, qa_actor_reference_resolve(actors, a->entity->team_master)))
        view.controller = qa_actor_reference_resolve(actors, a->entity->team_master);
    qa_body_state source = body;
    if (!qa_actor_id_equal(view.controller, actor) &&
        !qa_world_body_read(g->services.world, view.controller, &source, error)) return false;
    a = q2_ent(g, view.controller);
    if (!a || !q2_ent(g, actor)) return true;
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (m) {
        view.navigation.has_destination = m->moving && !m->angular;
        view.navigation.destination = qa_vec_add(m->motion.destination, qa_vec_sub(body.origin, source.origin));
    }
    if (kind == QA_Q2_MOVER_ELEVATOR && m) {
        const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
        bool second = name && !strcmp(name, "func_plat2");
        view.navigation.kind = QA_NAV_ENTITY_ELEVATOR;
        view.navigation.locked = second ? !m->activated : s->targetname && m->phase == 1 && !m->moving;
        view.navigation.data.elevator.origin = body.origin;
        qa_vec3 part = qa_vec_sub(body.origin, source.origin);
        view.navigation.data.elevator.bottom = qa_vec_add(m->start, part);
        view.navigation.data.elevator.top = qa_vec_add(m->end, part);
        static const qa_nav_mover_phase phases[] = {
            QA_NAV_MOVER_BOTTOM, QA_NAV_MOVER_UP, QA_NAV_MOVER_TOP, QA_NAV_MOVER_DOWN};
        if (m->phase < 0 || m->phase > 3)
            return observation_fail(error, "Q2 elevator continuation has an invalid phase");
        view.navigation.data.elevator.phase = phases[m->phase];
    } else if (kind == QA_Q2_MOVER_TRAIN && m) {
        view.navigation.kind = QA_NAV_ENTITY_TRAIN;
        view.navigation.data.train.origin = body.origin;
        view.navigation.data.train.running = (s->spawnflags & 1u) != 0;
        qa_vec3 offset = g->options.edition == QA_Q2_RERELEASE && (s->spawnflags & 32u)
            ? qa_v3(0, 0, 0) : qa_vec_scale(source.bounds.mins, -1);
        if (g->options.edition == QA_Q2_RERELEASE && !(s->spawnflags & 32u) && (s->spawnflags & 16u))
            offset = qa_vec_sub(offset, qa_v3(1, 1, 1));
        offset = qa_vec_add(offset, qa_vec_sub(body.origin, source.origin));
        if (!route(query, view.controller, offset, &view, error)) return false;
        view.navigation.data.train.stops = query->stops;
        view.navigation.data.train.count = *query->count;
    }
    if (!q2_ent(g, actor) || !q2_ent(g, view.controller)) return true;
    if (!activation(g, view.controller, &view, error)) return false;
    if (!q2_ent(g, actor) || !q2_ent(g, view.controller)) return true;
    *query->out = view; *query->found = true;
    return true;
}

bool qa_q2_entity_mover_read(qa_q2_game *g, qa_actor_id actor, qa_q2_map_mover_view *out,
                             qa_nav_train_stop *stops, size_t capacity, size_t *count,
                             bool *found, qa_error *error) {
    if (!g || !out || !count || !found || (capacity && !stops))
        return observation_fail(error, "Invalid Q2 mover observation");
    *count = 0; *found = false;
    if (!q2_ent(g, actor)) return true;
    mover_query query = {.game = g, .out = out, .stops = stops, .capacity = capacity,
        .count = count, .found = found};
    bool ok = qa_q2_run_actor(g, actor, inspect, &query, error);
    if (!*found) *count = 0;
    return ok;
}
