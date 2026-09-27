#include "qa/builtin.h"

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
