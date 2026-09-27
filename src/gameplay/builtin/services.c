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

bool qa_builtin_snapshot_reserve(qa_builtin_actor_snapshot *snapshot, size_t capacity,
                                 qa_error *error) {
    if (!snapshot || capacity > SIZE_MAX / (2 * sizeof(qa_actor_id))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native actor snapshot capacity");
        return false;
    }
    if (capacity <= snapshot->capacity)
        return true;
    qa_actor_id *ids = malloc(2 * capacity * sizeof(*ids));
    if (!ids) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot reserve native actor snapshot");
        return false;
    }
    if (snapshot->count)
        memcpy(ids, snapshot->ids, snapshot->count * sizeof(*ids));
    free(snapshot->ids);
    snapshot->ids = ids;
    snapshot->sort = ids + capacity;
    snapshot->capacity = capacity;
    return true;
}
void qa_builtin_snapshot_free(qa_builtin_actor_snapshot *snapshot) {
    if (!snapshot)
        return;
    free(snapshot->ids);
    *snapshot = (qa_builtin_actor_snapshot){0};
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
static void sort_snapshot(const qa_builtin_services *s, qa_builtin_actor_snapshot *snapshot) {
    size_t count = snapshot->count;
    qa_actor_id *ids = snapshot->ids, *temporary = snapshot->sort;
    for (size_t width = 1; width < count; width *= 2) {
        for (size_t start = 0; start < count; start += 2 * width) {
            size_t middle = start + width < count ? start + width : count;
            size_t end = middle + width < count ? middle + width : count;
            size_t left = start, right = middle, target = start;
            while (left < middle && right < end)
                temporary[target++] = qa_builtin_source_order(s, ids[left], ids[right]) <= 0
                                          ? ids[left++] : ids[right++];
            while (left < middle)
                temporary[target++] = ids[left++];
            while (right < end)
                temporary[target++] = ids[right++];
        }
        memcpy(ids, temporary, count * sizeof(*ids));
    }
}
void qa_builtin_sort_observations(const qa_builtin_services *s, qa_builtin_actor_snapshot *snapshot) {
    sort_snapshot(s, snapshot);
}
bool qa_builtin_observations(const qa_builtin_services *s, qa_builtin_actor_snapshot *snapshot,
                             qa_error *error) {
    if (!s || !s->session || !snapshot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native actor observation query");
        return false;
    }
    const qa_actor_registry *actors = qa_session_actors(s->session);
    if (!qa_builtin_snapshot_reserve(snapshot, qa_actors_capacity(actors), error))
        return false;
    snapshot->count = 0;
    const qa_actor_record *record;
    uint32_t cursor = 0;
    while (qa_actors_next(actors, &cursor, &record))
        snapshot->ids[snapshot->count++] = record->id;
    return true;
}
bool qa_builtin_nearby(const qa_builtin_services *s, qa_vec3 origin, float radius,
                       qa_builtin_actor_snapshot *snapshot, qa_error *error) {
    if (!s || !s->session || !s->world || !snapshot || !qa_vec_finite(origin) ||
        !isfinite(radius) || radius < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native nearby query");
        return false;
    }
    if (!qa_builtin_observations(s, snapshot, error))
        return false;
    const qa_actor_registry *actors = qa_session_actors(s->session);
    size_t total = snapshot->count, accepted = 0;
    for (size_t i = 0; i < total; ++i) {
        qa_actor_id id = snapshot->ids[i];
        if (!qa_actors_get(actors, id))
            continue;
        qa_body_state body;
        qa_error read_error = {0};
        if (!qa_world_body_read(s->world, id, &body, &read_error)) {
            if (read_error.code == QA_ERROR_NOT_FOUND)
                continue;
            if (error)
                *error = read_error;
            snapshot->count = 0;
            return false;
        }
        if (qa_vec_length(qa_vec_sub(body.origin, origin)) <= radius)
            snapshot->ids[accepted++] = id;
    }
    snapshot->count = accepted;
    sort_snapshot(s, snapshot);
    return true;
}
bool qa_builtin_players(const qa_builtin_services *s, qa_builtin_actor_snapshot *snapshot,
                        qa_error *error) {
    if (!s || !s->session || !s->players || !snapshot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native gameplay needs the ordered client roster");
        return false;
    }
    if (!qa_builtin_snapshot_reserve(snapshot, qa_actors_capacity(qa_session_actors(s->session)), error))
        return false;
    snapshot->count = 0;
    size_t count = 0;
    if (!s->players(s->context, snapshot->ids, snapshot->capacity, &count, error))
        return false;
    if (count > snapshot->capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, count, "native player roster exceeds actor capacity");
        return false;
    }
    for (size_t i = 0; i < count; ++i)
        if (qa_actors_get(qa_session_actors(s->session), snapshot->ids[i]))
            snapshot->ids[snapshot->count++] = snapshot->ids[i];
    return true;
}

void qa_builtin_angle_vectors(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up) {
    const float radians = 0.01745329251994329577f;
    float sy = sinf(angles.y * radians), cy = cosf(angles.y * radians);
    float sp = sinf(angles.x * radians), cp = cosf(angles.x * radians);
    float sr = sinf(angles.z * radians), cr = cosf(angles.z * radians);
    if (forward)
        *forward = qa_v3(cp * cy, cp * sy, -sp);
    if (right)
        *right = qa_v3(-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp);
    if (up)
        *up = qa_v3(cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp);
}

float qa_builtin_angle_mod(float angle) {
    float result = fmodf(angle, 360.0f);
    return result < 0 ? result + 360.0f : result;
}

float qa_builtin_angle_delta(float target, float current) {
    float delta = qa_builtin_angle_mod(target) - qa_builtin_angle_mod(current);
    if (delta > 180.0f)
        delta -= 360.0f;
    if (delta < -180.0f)
        delta += 360.0f;
    return delta;
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
