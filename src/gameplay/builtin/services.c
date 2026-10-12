#include "qa/builtin.h"
#include <stdlib.h>
#include <string.h>

bool qa_builtin_services_validate(const qa_builtin_services *s, qa_error *error) {
    if (!s || !s->session || !s->world || !s->combat || !s->inventory || !s->emit ||
        qa_world_actors(s->world) != qa_session_actor_registry(s->session)) {
        qa_error_set(
            error, QA_ERROR_ARGUMENT, 0,
            "native gameplay needs one shared session, world, combat, inventory and event sink");
        return false;
    }
    return true;
}

static bool check_client_cluster(qa_world *world, qa_vec3 eye, int32_t *out, qa_error *error) {
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(qa_world_geometry(world), eye, QA_LEAF_Q1, &leaf, error)) return false;
    if (leaf.cluster < -1 || leaf.cluster > INT32_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Check-client eye exceeds its Source leaf index");
        return false;
    }
    *out = (int32_t)leaf.cluster;
    return true;
}
bool qa_builtin_check_client(const qa_builtin_check_client_query *q, uint32_t *out,
    qa_error *error) {
    qa_clock_state clock;
    if (!q || !out || !q->world || !q->capacity || !q->slot || !q->time || !q->cluster ||
        !q->client || !q->client_eye || !q->observer_eye ||
        !qa_session_clock(q->session, q->provider, &clock)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Check-client requires its physical readers and Source server clock");
        return false;
    }
    *out = 0;
    double now = (double)clock.frame.time_ns / 1000000000.0;
    if (now - *q->time >= 0.1) {
        uint32_t previous = *q->slot;
        if (previous < 1) previous = 1;
        if (previous > q->capacity) previous = q->capacity;
        uint32_t slot = previous == q->capacity ? 1 : previous + 1;
        while (slot != previous) {
            qa_builtin_check_client_row row;
            if (!q->client(q->context, slot, true, &row, error)) return false;
            if (row.present && !(row.health <= 0) && !row.no_target) break;
            slot = slot == q->capacity ? 1 : slot + 1;
        }
        *q->slot = slot;
        *q->time = now;
        qa_vec3 eye;
        if (!q->client_eye(q->context, slot, &eye, error) ||
            !check_client_cluster(q->world, eye, q->cluster, error)) return false;
    }
    if (!*q->slot) return true;
    qa_builtin_check_client_row row;
    if (!q->client(q->context, *q->slot, false, &row, error)) return false;
    if (!row.present || row.health <= 0) return true;
    qa_vec3 eye;
    int32_t cluster;
    bool visible;
    if (!q->observer_eye(q->context, &eye, error) ||
        !check_client_cluster(q->world, eye, &cluster, error) ||
        !qa_collision_cluster_visible(qa_world_geometry(q->world), *q->cluster,
            cluster, false, &visible, error)) return false;
    if (visible) *out = *q->slot;
    return true;
}

bool qa_builtin_spawn_actor(const qa_builtin_services *s, const qa_builtin_spawn *spawn,
                            qa_actor_id *out, qa_error *error) {
    if (!s || !spawn || !out || (spawn->inventory_count && !spawn->inventory)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native actor spawn");
        return false;
    }
    qa_actor_id actor;
    if (!qa_session_allocate(s->session, spawn->owner, spawn->definition, spawn->has_source,
                             spawn->source_slot, &actor, error))
        return false;
    if (!qa_world_body_create(s->world, actor, &spawn->body, error) ||
        (spawn->collision && !qa_world_set_collision(s->world, actor, spawn->collision, error)) ||
        (spawn->combat && !qa_combat_create_actor(s->combat, actor, spawn->combat, error)) ||
        (spawn->inventory && !qa_inventory_create_actor(s->inventory, actor, spawn->inventory,
                                                        spawn->inventory_count, error)) ||
        (spawn->link && !qa_world_link(s->world, actor, NULL, error))) {
        qa_error cleanup = {0};
        (void)qa_session_release(s->session, actor, &cleanup);
        return false;
    }
    *out = actor;
    return true;
}

bool qa_builtin_emit(const qa_builtin_services *s, const qa_builtin_event *event, qa_error *error) {
    if (!s || !s->emit || !event) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native presentation has no event consumer");
        return false;
    }
    return s->emit(s->context, event, error);
}

bool qa_builtin_resource(const qa_builtin_services *s, const char *path, qa_string_id *out,
                         qa_error *error) {
    return qa_strings_intern_cstr(qa_session_strings(s->session), path, out, error);
}

bool qa_builtin_q1_lightstyle(const qa_builtin_services *s, qa_actor_owner provider,
                             qa_actor_id actor, uint64_t time_ns, int32_t style,
                             qa_string_id pattern, qa_error *error) {
    const qa_builtin_event event = {.kind = QA_BUILTIN_LIGHT, .family = QA_GAME_Q1,
        .provider = provider, .actor = actor, .time_ns = time_ns,
        .code = style, .resource = pattern};
    return qa_builtin_emit(s, &event, error);
}

int qa_builtin_source_order(const qa_builtin_services *s, qa_actor_id a, qa_actor_id b) {
    if (s->physics && s->physics->services.source_order)
        return s->physics->services.source_order(s->physics->services.context, a, b);
    const qa_actor_registry *actors = qa_session_actors(s->session);
    const qa_actor_record *left = qa_actors_get(actors, a), *right = qa_actors_get(actors, b);
    uint32_t first = left && left->has_source ? left->source_slot : a.slot;
    uint32_t second = right && right->has_source ? right->source_slot : b.slot;
    if (first != second)
        return first < second ? -1 : 1;
    if (left && right && left->owner != right->owner)
        return left->owner < right->owner ? -1 : 1;
    return a.slot < b.slot ? -1 : a.slot > b.slot;
}
static int snapshot_source_compare(void *context,qa_actor_id left,qa_actor_id right) {
    return qa_builtin_source_order(context,left,right);
}
void qa_builtin_sort_observations(const qa_builtin_services *s, qa_world_actor_snapshot *snapshot) {
    qa_world_snapshot_sort(snapshot, snapshot_source_compare, (void *)s);
}
static bool snapshot_prepare(const qa_builtin_services *s, qa_world_actor_snapshot *snapshot,
                             bool *acquired, qa_error *error) {
    *acquired = !snapshot->frame && !snapshot->borrowed;
    if (*acquired && !qa_world_snapshot_acquire(s->world, snapshot, error)) return false;
    snapshot->count = 0;
    return true;
}
bool qa_builtin_observations(const qa_builtin_services *s, qa_world_actor_snapshot *snapshot,
                             qa_error *error) {
    if (!s || !s->session || !s->world || !snapshot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native actor observation query");
        return false;
    }
    bool acquired;
    if (!snapshot_prepare(s, snapshot, &acquired, error)) return false;
    const qa_actor_registry *actors = qa_session_actors(s->session);
    const qa_actor_record *record;
    uint32_t cursor = 0;
    while (qa_actors_next(actors, &cursor, &record))
        snapshot->actors[snapshot->count++] = record->id;
    return true;
}
bool qa_builtin_nearby(const qa_builtin_services *s, qa_vec3 origin, float radius,
                       qa_world_actor_snapshot *snapshot, qa_error *error) {
    if (!s || !s->session || !s->world || !snapshot || !qa_vec_finite(origin) ||
        !isfinite(radius) || radius < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native nearby query");
        return false;
    }
    bool acquired;
    if (!snapshot_prepare(s, snapshot, &acquired, error)) return false;
    qa_world_actor_snapshot nearby;
    qa_world_radius_query query = {.origin = origin, .radius = radius,
                                   .metric = QA_WORLD_RADIUS_ORIGIN,
                                   .actor_storage = snapshot->actors,
                                   .actor_capacity = snapshot->capacity};
    if (!qa_world_radius_snapshot_capture(s->world, &query, &nearby, error)) {
        if (acquired) qa_world_snapshot_release(snapshot);
        return false;
    }
    snapshot->count = nearby.count;
    qa_world_snapshot_release(&nearby);
    qa_builtin_sort_observations(s, snapshot);
    return true;
}
bool qa_builtin_players(const qa_builtin_services *s, qa_world_actor_snapshot *snapshot,
                        qa_error *error) {
    if (!s || !s->session || !s->world || !s->players || !snapshot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native gameplay needs the ordered client roster");
        return false;
    }
    bool acquired;
    if (!snapshot_prepare(s, snapshot, &acquired, error)) return false;
    size_t count = 0;
    bool ok = s->players(s->context, snapshot->actors, snapshot->capacity, &count, error);
    if (ok && count > snapshot->capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, count, "native player roster exceeds actor capacity");
        ok = false;
    }
    if (!ok) {
        if (acquired) qa_world_snapshot_release(snapshot);
        return false;
    }
    const qa_actor_registry *actors = qa_session_actors(s->session);
    for (size_t i = 0; i < count; ++i)
        if (qa_actors_get(actors, snapshot->actors[i]))
            snapshot->actors[snapshot->count++] = snapshot->actors[i];
    return true;
}

static bool apply_trajectory(const qa_builtin_services *s, qa_actor_id actor,
                             const qa_builtin_trajectory_update *update, qa_error *error) {
    if (!qa_actors_get(qa_session_actors(s->session), actor))
        return true;
    if (!qa_vec_finite(update->origin) || !qa_vec_finite(update->velocity) ||
        !qa_vec_finite(update->angles)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "weapon trajectory contains nonfinite values");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(s->world, actor, &body, error))
        return false;
    body.origin = update->origin;
    body.velocity = update->velocity;
    body.angles = update->angles;
    return qa_world_body_write(s->world, actor, &body, error);
}
bool qa_builtin_launch_projectile(const qa_builtin_services *s,
                                  const qa_builtin_weapon_launch *launch, bool *did_change,
                                  qa_error *error) {
    if (did_change)
        *did_change = false;
    if (!s || !launch || launch->role > QA_BUILTIN_GRAPPLE) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid weapon behavior launch");
        return false;
    }
    if (!s->weapon_launch)
        return true;
    qa_builtin_trajectory_update update = {0};
    bool changed = false;
    if (!s->weapon_launch(s->context, launch, &update, &changed, error))
        return false;
    if (changed && !apply_trajectory(s, launch->projectile, &update, error))
        return false;
    if (did_change)
        *did_change = changed;
    return true;
}
bool qa_builtin_step_projectile(const qa_builtin_services *s, qa_actor_id actor, uint64_t time_ns,
                                bool *did_change, qa_error *error) {
    if (did_change)
        *did_change = false;
    if (!s || !s->weapon_trajectory || !s->controls_trajectory ||
        !s->controls_trajectory(s->context, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(s->world, actor, &body, error))
        return false;
    qa_builtin_trajectory_update update = {0};
    bool changed = false;
    if (!s->weapon_trajectory(s->context, actor, &body, time_ns, &update, &changed, error))
        return false;
    if (changed && !apply_trajectory(s, actor, &update, error))
        return false;
    if (did_change)
        *did_change = changed;
    return true;
}
