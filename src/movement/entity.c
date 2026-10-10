#include "entity/internal.h"
#include "qa/movement.h"

qa_actor_id qa_physics_actor_reference(const qa_physics *physics,
    qa_actor_reference reference) {
    if (physics->services.resolve_reference)
        return physics->services.resolve_reference(physics->services.context, reference);
    return qa_actor_reference_resolve(qa_world_actors(physics->world), reference);
}

qa_physics_properties qa_physics_properties_default(qa_game_family family) {
    qa_physics_properties p = {0};
    p.family = family;
    p.motion = QA_PHYSICS_STATIONARY;
    p.solid = QA_PHYSICS_NOT_SOLID;
    p.gravity_direction = qa_v3(0, 0, -1);
    p.gravity_scale = 1;
    p.yaw_speed = 20;
    p.water_type = family == QA_GAME_Q1 ? -1 : 0;
    return p;
}

bool qa_physics_init(qa_physics *p, qa_world *world, qa_actor_id world_actor,
                     const qa_physics_services *services, qa_error *error) {
    if (!p || !world || !services || !services->read || !services->write) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Physics needs a world and authoritative property access");
        return false;
    }
    *p = (qa_physics){.world = world, .world_actor = world_actor,
                     .services = *services, .gravity = 800,
                     .max_velocity = 2000, .stop_speed = 100};
    size_t capacity = qa_actors_capacity(qa_world_actors(world));
    if (capacity < QA_PHYSICS_SOURCE_PUSH_LIMIT) capacity = QA_PHYSICS_SOURCE_PUSH_LIMIT;
    return qa_physics_prepare_push_frames(p, QA_PHYSICS_DEFAULT_PUSH_FRAMES,
        capacity, capacity, error);
}

bool ph_live(const qa_physics *p, qa_actor_id actor) {
    return p && p->world && qa_actors_get(qa_world_actors(p->world), actor) != NULL;
}

int ph_read(qa_physics *p, qa_actor_id actor, qa_body_state *body,
             qa_physics_properties *properties, qa_error *error) {
    if (!ph_live(p, actor)) return 0;
    if (!p->services.read(p->services.context, actor, properties)) return 0;
    if (!ph_live(p, actor)) return 0;
    if (!qa_world_body_read(p->world, actor, body, error))
        return ph_live(p, actor) ? -1 : 0;
    return ph_live(p, actor) ? 1 : 0;
}

bool ph_write(qa_physics *p, qa_actor_id actor, const qa_body_state *body, qa_error *error) {
    if (!ph_live(p, actor)) return true;
    return qa_world_body_write(p->world, actor, body, error);
}

bool ph_properties(qa_physics *p, qa_actor_id actor,
                    const qa_physics_properties *properties, qa_error *error) {
    if (!ph_live(p, actor)) return true;
    return p->services.write(p->services.context, actor, properties, error);
}

bool ph_link(qa_physics *p, qa_actor_id actor, bool triggers, qa_error *error) {
    if (!ph_live(p, actor)) return true;
    if (!qa_world_link(p->world, actor, NULL, error)) return false;
    return !triggers || qa_physics_touch_triggers(p, actor, error);
}

static qa_trace_policy ph_policy(const qa_physics_properties *props, uint32_t mask,
                                 qa_q1_move_kind move) {
    qa_trace_policy policy = qa_collision_default_policy(props->family);
    policy.contents_mask = qa_collision_contents_mask(mask,props->family);
    policy.q1_move = move;
    policy.q1_hull = -1;
    policy.q2_merged_contents = props->q2_rerelease;
    policy.curves = policy.player_curve_clip = true;
    return policy;
}

bool ph_trace(qa_physics *p, qa_actor_id actor, const qa_physics_properties *props,
               qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, uint32_t mask,
               qa_q1_move_kind move, const qa_actor_id *exclude, size_t count,
               qa_trace_result *trace, qa_error *error) {
    qa_trace_query query = {.start = start, .end = end,
        .shape = {.kind = bounds ? QA_SHAPE_BOX : QA_SHAPE_POINT},
        .policy = ph_policy(props, mask, move), .pass_actor = actor};
    if (bounds) query.shape.bounds = *bounds;
    return count ? qa_world_trace_excluding(p->world, &query, exclude, count, trace, error) :
                   qa_world_trace(p->world, &query, trace, error);
}

bool ph_body_trace(qa_physics *p, qa_actor_id actor, const qa_body_state *body,
                    const qa_physics_properties *props, qa_vec3 start, qa_vec3 end,
                    bool exact, const qa_actor_id *exclude, size_t count,
                    qa_trace_result *trace, qa_error *error) {
    qa_q1_move_kind move = QA_Q1_MOVE_NORMAL;
    if (props->motion == QA_PHYSICS_FLY_MISSILE) move = QA_Q1_MOVE_MISSILE;
    else if (props->solid == QA_PHYSICS_NOT_SOLID || props->solid == QA_PHYSICS_TRIGGER)
        move = QA_Q1_MOVE_NO_MONSTERS;
    uint32_t mask = exact || props->clip_mask ? props->clip_mask : 3;
    return ph_trace(p, actor, props, start, end, &body->bounds, mask, move,
                    exclude, count, trace, error);
}

bool ph_contents(qa_physics *p, qa_actor_id actor, const qa_physics_properties *props,
                  qa_vec3 point, int32_t *out, qa_error *error) {
    qa_point_query query = {.point = point, .pass_actor = actor,
        .policy = ph_policy(props, UINT32_MAX, QA_Q1_MOVE_NORMAL)};
    qa_point_contents contents;
    if (!qa_world_point_contents(p->world, &query, &contents, error)) return false;
    *out = qa_collision_point_contents_export(contents.contents,props->family,contents.q1_opaque_token);
    return true;
}

qa_actor_id ph_hit(const qa_physics *p, const qa_trace_result *trace) {
    return trace->hit == QA_TRACE_HIT_ACTOR ? trace->actor :
           trace->hit == QA_TRACE_HIT_WORLD ? p->world_actor : ph_none();
}

qa_actor_reference ph_reference(const qa_physics *p, qa_actor_id actor, qa_actor_id target) {
    const qa_actor_registry *registry = qa_world_actors(p->world);
    const qa_actor_record *self = qa_actors_get(registry, actor);
    const qa_actor_record *other = qa_actors_get(registry, target);
    if (self && other && self->owner == other->owner && other->has_source)
        return qa_actor_reference_source(other->owner, other->source_slot);
    return qa_actor_reference_lifetime(target);
}

qa_vec3 ph_normal(const qa_trace_result *trace) {
    return trace->contact ? trace->contact_plane.normal : trace->plane.normal;
}

void ph_axes(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up) {
    const float radians = 0.01745329251994329577f;
    float sy = sinf(angles.y*radians), cy = cosf(angles.y*radians);
    float sp = sinf(angles.x*radians), cp = cosf(angles.x*radians);
    float sr = sinf(angles.z*radians), cr = cosf(angles.z*radians);
    *forward = qa_v3(cp*cy, cp*sy, -sp);
    *right = qa_v3(-sr*sp*cy+cr*sy, -sr*sp*sy-cr*cy, -sr*cp);
    *up = qa_v3(cr*sp*cy+sr*sy, cr*sp*sy-sr*cy, cr*cp);
}

bool ph_event(qa_physics *p, qa_actor_id actor, qa_physics_event_kind kind,
               qa_vec3 origin, qa_error *error) {
    if (!p->services.event) return true;
    qa_physics_event event = {.actor = actor, .kind = kind, .origin = origin};
    return p->services.event(p->services.context, &event, error);
}

bool ph_ground(qa_physics *p, qa_actor_id actor, qa_actor_id ground, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    if (ground.registry || props.family != QA_GAME_Q1)
        body.ground = ph_reference(p, actor, ground);
    if (!ph_write(p, actor, &body, error)) return false;
    if (!ph_live(p, actor) || !p->services.read(p->services.context, actor, &props)) return true;
    if (ground.registry) props.flags |= QA_PHYSICS_ONGROUND;
    else props.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
    return ph_properties(p, actor, &props, error);
}

bool ph_test_position(qa_physics *p, qa_actor_id actor, bool *blocked, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    *blocked = false;
    if (read <= 0) return read == 0;
    qa_trace_result trace;
    if (!ph_body_trace(p, actor, &body, &props, body.origin, body.origin, false,
                       NULL, 0, &trace, error)) return false;
    *blocked = trace.start_solid;
    return true;
}

typedef struct ph_trigger_context { qa_physics *physics; qa_error *error; bool ok; } ph_trigger_context;

static bool trigger_active(void *context, qa_actor_id actor) {
    ph_trigger_context *call = context;
    qa_physics_properties props;
    if (!call->ok || !ph_live(call->physics, actor)) return false;
    if (call->physics->services.read(call->physics->services.context, actor, &props))
        return ph_live(call->physics, actor) && props.solid == QA_PHYSICS_TRIGGER;
    qa_actor_collision collision;
    qa_error local = {0};
    bool found = qa_world_get_collision(call->physics->world, actor, &collision, &local);
    if (local.code != QA_OK) {
        if (call->error) *call->error = local;
        call->ok = false;
        return false;
    }
    return found &&
           (collision.role == QA_COLLISION_TRIGGER || collision.role == QA_COLLISION_BOTH);
}

static void trigger_touch(void *context, qa_world *world, const qa_touch_contact *contact) {
    ph_trigger_context *call = context;
    (void)world;
    if (!call->ok || !call->physics->services.touch) return;
    call->ok = call->physics->services.touch(call->physics->services.context, contact, call->error);
}

static bool touch_triggers(qa_physics *p, qa_actor_id actor,
                           qa_game_family family, bool source,
                           qa_error *error) {
    qa_physics_properties props;
    if (!ph_live(p, actor)) return true;
    if (!p->services.read(p->services.context, actor, &props)) {
        qa_actor_collision collision;
        qa_error local = {0};
        if (!qa_world_get_collision(p->world, actor, &collision, &local)) {
            if (local.code != QA_OK) { if (error) *error = local; return false; }
            return true;
        }
        props = qa_physics_properties_default(collision.family);
    }
    if (!source) family = props.family;
    if (family != QA_GAME_Q1 && (props.flags & QA_PHYSICS_DEAD) &&
        (props.flags & (QA_PHYSICS_PLAYER | QA_PHYSICS_MONSTER))) return true;
    ph_trigger_context call = {.physics = p, .error = error, .ok = true};
    return qa_world_touch_triggers(p->world, actor, family, trigger_active,
                                  trigger_touch, &call, error) && call.ok;
}

bool qa_physics_touch_triggers(qa_physics *p, qa_actor_id actor, qa_error *error) {
    return touch_triggers(p, actor, QA_GAME_Q1, false, error);
}

bool qa_physics_touch_triggers_source(qa_physics *p, qa_actor_id actor,
                                       qa_game_family family, qa_error *error) {
    if (!p || family < QA_GAME_Q1 || family > QA_GAME_Q3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid source trigger traversal");
        return false;
    }
    return touch_triggers(p, actor, family, true, error);
}

bool qa_physics_impact(qa_physics *p, qa_actor_id actor,
                       const qa_trace_result *trace, qa_error *error) {
    if (!p || !trace) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid physics impact");
        return false;
    }
    if (!p->services.touch || !ph_live(p, actor)) return true;
    qa_actor_id other = ph_hit(p, trace);
    if (!other.registry) return true;
    qa_physics_properties self_props;
    if (!p->services.read(p->services.context, actor, &self_props)) return true;
    bool rerelease = self_props.family == QA_GAME_Q2 && self_props.q2_rerelease &&
                     trace->family == QA_GAME_Q2;
    qa_touch_contact contact = {.self = actor, .other = other, .has_plane = true,
        .plane = trace->contact ? trace->contact_plane : trace->plane,
        .has_surface = trace->has_surface, .surface = trace->surface,
        .has_source_trace = rerelease, .source_trace = *trace};
    if (trace->family == QA_GAME_Q1 &&
        (trace->has_surface || !qa_collision_bits_equal(trace->surface_flags,(qa_collision_bits){0}))) {
        contact.has_surface = true;
        memset(&contact.surface, 0, sizeof(contact.surface));
        const qa_collision_bits touch_flags = {
            (UINT64_C(1) << QA_SURFACE_SLICK) | (UINT64_C(1) << QA_SURFACE_NODRAW) |
            (UINT64_C(1) << QA_SURFACE_SKY_NOIMPACT) | (UINT64_C(1) << QA_SURFACE_SKY), 0};
        contact.surface.flags = qa_collision_bits_intersection(trace->surface_flags,touch_flags);
    }
    if (self_props.solid != QA_PHYSICS_NOT_SOLID ||
        (rerelease && (self_props.flags & QA_PHYSICS_ALWAYS_TOUCH)))
        if (!p->services.touch(p->services.context, &contact, error)) return false;
    /* Rerelease deliberately dispatches the second callback after self removal. */
    if ((!rerelease && !ph_live(p, actor)) || !ph_live(p, other)) return true;
    qa_physics_properties other_props;
    bool supplied = p->services.read(p->services.context, other, &other_props);
    if (!ph_live(p, other)) return true;
    if (!supplied) {
        qa_actor_collision collision;
        qa_error local = {0};
        if (!qa_world_get_collision(p->world, other, &collision, &local)) {
            if (local.code != QA_OK) { if (error) *error = local; return false; }
            return true;
        }
        other_props = qa_physics_properties_default(collision.family);
        other_props.solid = collision.role == QA_COLLISION_TRIGGER ? QA_PHYSICS_TRIGGER : QA_PHYSICS_BOX;
    }
    if (other_props.solid == QA_PHYSICS_NOT_SOLID &&
        !(rerelease && (other_props.flags & QA_PHYSICS_ALWAYS_TOUCH))) return true;
    contact.self = other;
    contact.other = actor;
    contact.inverted = rerelease;
    if (!rerelease) { contact.has_plane = false; contact.has_surface = false; }
    return p->services.touch(p->services.context, &contact, error);
}

bool qa_physics_push_entity(qa_physics *p, qa_actor_id actor, qa_vec3 displacement,
                            const qa_actor_id *exclude, size_t count,
                            qa_trace_result *out, qa_error *error) {
    if (!p || !out || !qa_vec_finite(displacement) || (count && !exclude)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid entity push");
        return false;
    }
    qa_body_state initial;
    qa_physics_properties props;
    int read = ph_read(p, actor, &initial, &props, error);
    if (read <= 0) {
        if (read == 0) *out = (qa_trace_result){.fraction = 1};
        return read == 0;
    }
    qa_vec3 end = qa_vec_add(initial.origin, displacement);
    for (;;) {
        qa_body_state current;
        read = ph_read(p, actor, &current, &props, error);
        if (read < 0) return false;
        if (!read) return true;
        if (!ph_body_trace(p, actor, &current, &props, initial.origin, end, false,
                           exclude, count, out, error)) return false;
        current.origin = out->end;
        if (!ph_write(p, actor, &current, error) || !ph_link(p, actor, false, error)) return false;
        if (out->fraction != 1) {
            qa_actor_id hit = ph_hit(p, out);
            if (!qa_physics_impact(p, actor, out, error)) return false;
            if (props.family != QA_GAME_Q1 && hit.registry && !ph_live(p, hit) && ph_live(p, actor)) {
                read = ph_read(p, actor, &current, &props, error);
                if (read < 0) return false;
                if (!read) return true;
                current.origin = initial.origin;
                if (!ph_write(p, actor, &current, error) || !ph_link(p, actor, false, error)) return false;
                continue;
            }
        }
        return qa_physics_touch_triggers(p, actor, error);
    }
}

qa_vec3 qa_physics_clip_velocity(qa_vec3 velocity, qa_vec3 normal, float overbounce) {
    qa_vec3 out = qa_vec_sub(velocity, qa_vec_scale(normal, qa_vec_dot(velocity, normal)*overbounce));
    if (out.x > -0.1f && out.x < 0.1f) out.x = 0;
    if (out.y > -0.1f && out.y < 0.1f) out.y = 0;
    if (out.z > -0.1f && out.z < 0.1f) out.z = 0;
    return out;
}

bool qa_physics_water_transition(qa_physics *p, qa_actor_id actor,
                                 qa_vec3 previous_origin, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    int32_t value;
    if (!ph_contents(p, actor, &props, body.origin, &value, error)) return false;
    bool wet = ph_wet(props.family, value), was_wet = props.water_level != 0;
    props.water_level = wet ? 1 : 0;
    props.water_type = value;
    if (!ph_properties(p, actor, &props, error)) return false;
    return wet == was_wet || !ph_live(p, actor) ||
        ph_event(p, actor, wet ? QA_PHYSICS_WATER_ENTER : QA_PHYSICS_WATER_LEAVE,
                 wet ? previous_origin : body.origin, error);
}

static bool ph_stop_velocity(qa_physics *p, qa_actor_id actor, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    body.velocity = qa_v3(0, 0, 0);
    return ph_write(p, actor, &body, error);
}

typedef struct ph_q2r_slide_context {
    qa_physics *physics;
    qa_actor_id actor;
    qa_vec3 *origin, *velocity;
    bool exact;
} ph_q2r_slide_context;

static bool ph_q2r_trace(void *context, qa_vec3 start, qa_vec3 end,
                         qa_bounds bounds, qa_trace_result *trace, qa_error *error) {
    ph_q2r_slide_context *call = context;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(call->physics, call->actor, &body, &props, error);
    if (read <= 0) {
        if (read == 0) *trace = (qa_trace_result){.family = QA_GAME_Q2, .fraction = 1, .end = end};
        return read == 0;
    }
    body.origin = *call->origin;
    body.velocity = *call->velocity;
    if (!ph_write(call->physics, call->actor, &body, error)) return false;
    if (!ph_live(call->physics, call->actor)) {
        *trace = (qa_trace_result){.family = QA_GAME_Q2, .fraction = 1, .end = end};
        return true;
    }
    body.bounds = bounds;
    return ph_body_trace(call->physics, call->actor, &body, &props, start, end,
                         call->exact, NULL, 0, trace, error);
}

static bool ph_q2r_fly(qa_physics *p, qa_actor_id actor, qa_body_state body,
                        float seconds, bool exact, qa_physics_result *result, qa_error *error) {
    qa_vec3 origin = body.origin, velocity = body.velocity;
    ph_q2r_slide_context call = {.physics = p, .actor = actor, .origin = &origin,
                                 .velocity = &velocity, .exact = exact};
    qa_q2r_slide slide = {.context = &call, .trace = ph_q2r_trace,
        .origin = &origin, .velocity = &velocity,
        .pml_origin = p->q2r_pml_origin ? p->q2r_pml_origin : &p->q2r_local_origin,
        .bounds = body.bounds, .elapsed = seconds};
    if (!qa_move_q2r_slide(&slide, error)) return false;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    body.origin = origin;
    body.velocity = velocity;
    if (!ph_write(p, actor, &body, error)) return false;
    for (size_t i = 0; i < slide.contact_count; ++i) {
        if (!ph_live(p, actor)) { result->status = QA_PHYSICS_REMOVED; return true; }
        qa_trace_result *trace = &slide.contacts[i];
        qa_actor_id other = ph_hit(p, trace);
        if (!ph_live(p, other)) continue;
        ++result->collisions;
        if (trace->plane.normal.z > 0.7f && !ph_ground(p, actor, other, error)) return false;
        if (!qa_physics_impact(p, actor, trace, error)) return false;
        read = ph_read(p, actor, &body, &props, error);
        if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
        if (props.flags & QA_PHYSICS_KILL_VELOCITY) {
            props.flags &= ~(uint32_t)QA_PHYSICS_KILL_VELOCITY;
            if (!ph_properties(p, actor, &props, error) || !ph_stop_velocity(p, actor, error)) return false;
        }
    }
    return true;
}

bool qa_physics_fly_move(qa_physics *p, qa_actor_id actor, float seconds,
                         bool exact, qa_physics_result *result, qa_error *error) {
    if (!p || !result || !isfinite(seconds) || seconds < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid fly-move interval");
        return false;
    }
    *result = (qa_physics_result){.status = QA_PHYSICS_MOVED};
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read < 0) return false;
    if (!read) { result->status = ph_live(p, actor) ? QA_PHYSICS_UNMANAGED : QA_PHYSICS_REMOVED; return true; }
    if (!ph_ground(p, actor, ph_none(), error)) return false;
    if (props.family == QA_GAME_Q2 && props.q2_rerelease)
        return ph_q2r_fly(p, actor, body, seconds, exact, result, error);
    qa_vec3 primal = body.velocity, original = body.velocity, planes[5];
    size_t plane_count = 0;
    float remaining = seconds;
    for (unsigned bump = 0; bump < 4; ++bump) {
        read = ph_read(p, actor, &body, &props, error);
        if (read < 0) return false;
        if (!read) { result->status = QA_PHYSICS_REMOVED; return true; }
        qa_trace_result trace;
        if (!ph_body_trace(p, actor, &body, &props, body.origin,
                           qa_vec_add(body.origin, qa_vec_scale(body.velocity, remaining)),
                           exact, NULL, 0, &trace, error)) return false;
        if (trace.all_solid) {
            result->status = QA_PHYSICS_STOPPED;
            return ph_stop_velocity(p, actor, error);
        }
        if (trace.fraction > 0) {
            body.origin = trace.end;
            original = body.velocity;
            plane_count = 0;
            if (!ph_write(p, actor, &body, error)) return false;
        }
        if (trace.fraction == 1) break;
        ++result->collisions;
        qa_vec3 normal = ph_normal(&trace);
        qa_actor_id hit = ph_hit(p, &trace);
        qa_actor_collision other_collision;
        qa_error local = {0};
        bool brush = trace.hit == QA_TRACE_HIT_WORLD ||
            (qa_world_get_collision(p->world, hit, &other_collision, &local) && other_collision.inline_model);
        if (local.code != QA_OK) { if (error) *error = local; return false; }
        if ((exact ? normal.z > 0.7f : qa_vec_dot(normal, props.gravity_direction) < -0.7f) && hit.registry && brush)
            if (!ph_ground(p, actor, hit, error)) return false;
        if (!qa_physics_impact(p, actor, &trace, error)) return false;
        read = ph_read(p, actor, &body, &props, error);
        if (read < 0) return false;
        if (!read) { result->status = QA_PHYSICS_REMOVED; return true; }
        remaining -= remaining*trace.fraction;
        if (plane_count == 5) { result->status = QA_PHYSICS_STOPPED; return ph_stop_velocity(p, actor, error); }
        planes[plane_count++] = normal;
        qa_vec3 velocity = body.velocity;
        bool accepted = false;
        for (size_t i = 0; i < plane_count; ++i) {
            qa_vec3 candidate = qa_physics_clip_velocity(original, planes[i], 1);
            bool good = true;
            for (size_t j = 0; j < plane_count; ++j) {
                bool same = i == j || (props.family != QA_GAME_Q1 && ph_equal_vec(planes[i], planes[j]));
                if (!same && qa_vec_dot(candidate, planes[j]) < 0) { good = false; break; }
            }
            if (good) { velocity = candidate; accepted = true; break; }
        }
        if (!accepted) {
            if (plane_count != 2) { result->status = QA_PHYSICS_STOPPED; return ph_stop_velocity(p, actor, error); }
            qa_vec3 direction = qa_vec_cross(planes[0], planes[1]);
            velocity = qa_vec_scale(direction, qa_vec_dot(direction, body.velocity));
        }
        if (qa_vec_dot(velocity, primal) <= 0) {
            result->status = QA_PHYSICS_STOPPED;
            return ph_stop_velocity(p, actor, error);
        }
        body.velocity = velocity;
        if (!ph_write(p, actor, &body, error)) return false;
    }
    if (!ph_live(p, actor)) result->status = QA_PHYSICS_REMOVED;
    return true;
}

static float ph_clamp(float value, float maximum) {
    return isfinite(value) ? fmaxf(-maximum, fminf(maximum, value)) : 0;
}

static qa_vec3 ph_limit(qa_vec3 velocity, float maximum) {
    return qa_v3(ph_clamp(velocity.x, maximum), ph_clamp(velocity.y, maximum), ph_clamp(velocity.z, maximum));
}

static float ph_angular_friction(float value, float adjustment) {
    return value > 0 ? fmaxf(0, value-adjustment) : fminf(0, value+adjustment);
}

static bool ph_angular_step(qa_physics *p, qa_actor_id actor, float seconds,
                             float friction, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    body.angles = qa_vec_add(body.angles, qa_vec_scale(props.angular_velocity, seconds));
    if (!ph_write(p, actor, &body, error)) return false;
    if (!ph_live(p, actor) || !p->services.read(p->services.context, actor, &props)) return true;
    if (friction != 0 && ph_moving(props.angular_velocity)) {
        float loss = seconds*friction;
        props.angular_velocity = qa_v3(ph_angular_friction(props.angular_velocity.x, loss),
            ph_angular_friction(props.angular_velocity.y, loss), ph_angular_friction(props.angular_velocity.z, loss));
        return ph_properties(p, actor, &props, error);
    }
    return true;
}

static bool ph_shared_bottom(qa_physics *p, qa_actor_id actor, const qa_body_state *body,
                               const qa_physics_properties *props, bool *supported, qa_error *error) {
    qa_bounds box = qa_bounds_translate(body->bounds, body->origin);
    float direction = props->gravity_direction.z;
    float floor = direction > 0 ? box.maxs.z : box.mins.z;
    qa_vec3 start = qa_v3((box.mins.x+box.maxs.x)*0.5f, (box.mins.y+box.maxs.y)*0.5f, floor);
    qa_vec3 end = start; end.z += direction*36;
    qa_trace_result middle;
    *supported = false;
    if (!ph_trace(p, actor, props, start, end, NULL, PH_MONSTER_MASK,
                   QA_Q1_MOVE_NO_MONSTERS, NULL, 0, &middle, error)) return false;
    if (middle.fraction == 1) return true;
    float xs[2] = {box.mins.x, box.maxs.x}, ys[2] = {box.mins.y, box.maxs.y};
    for (unsigned x = 0; x < 2; ++x) for (unsigned y = 0; y < 2; ++y) {
        start = qa_v3(xs[x], ys[y], floor);
        end = start; end.z += direction*36;
        qa_trace_result corner;
        if (!ph_trace(p, actor, props, start, end, NULL, PH_MONSTER_MASK,
                       QA_Q1_MOVE_NO_MONSTERS, NULL, 0, &corner, error)) return false;
        if (corner.fraction == 1 || (corner.end.z-middle.end.z)*direction > 18) return true;
    }
    *supported = true;
    return true;
}

static bool ph_new_toss(qa_physics *p, qa_actor_id actor, float seconds,
                         qa_physics_result *result, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    if (props.flags & QA_PHYSICS_TEAM_SLAVE) { result->status = QA_PHYSICS_SLAVE; return true; }
    qa_trace_result trace;
    qa_vec3 below = body.origin;
    below.z -= 0.25f;
    if (!ph_body_trace(p, actor, &body, &props, body.origin, below, true, NULL, 0, &trace, error)) return false;
    qa_actor_id ground = ph_live(p, qa_physics_actor_reference(p, body.ground)) ?
        (trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : p->world_actor) : ph_none();
    if (!ph_ground(p, actor, ground, error)) return false;
    if (ground.registry && ph_normal(&trace).z == 1 && !ph_moving(body.velocity)) {
        result->status = QA_PHYSICS_STOPPED;
        return true;
    }
    qa_vec3 old_origin = body.origin, velocity = body.velocity;
    float speed = qa_vec_length(velocity);
    if (props.q2_rerelease) {
        if (speed > p->max_velocity) velocity = qa_vec_scale(qa_vec_scale(velocity, 1/speed), p->max_velocity);
    } else velocity = ph_limit(velocity, p->max_velocity);
    float gravity = props.gravity_scale*p->gravity*seconds;
    if (props.q2_rerelease || props.gravity_direction.z > 0)
        velocity = qa_vec_add(velocity, qa_vec_scale(props.gravity_direction, gravity));
    else velocity.z -= gravity;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    body.velocity = velocity;
    if (!ph_write(p, actor, &body, error) || !ph_angular_step(p, actor, seconds, p->stop_speed*6, error)) return false;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    speed = qa_vec_length(velocity);
    float loss = props.water_level ? 6*(float)props.water_level : ground.registry ? 36 : 6;
    body.velocity = speed > 0 ? qa_vec_scale(velocity, fmaxf(0, speed-loss)/speed) : qa_v3(0, 0, 0);
    if (!ph_write(p, actor, &body, error) || !qa_physics_fly_move(p, actor, seconds, true, result, error) ||
        !ph_link(p, actor, true, error)) return false;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    int32_t contents;
    if (!ph_contents(p, actor, &props, body.origin, &contents, error)) return false;
    bool was_wet = (props.water_type & 56) != 0, wet = (contents & 56) != 0;
    props.water_level = wet ? 1 : 0;
    props.water_type = contents;
    if (!ph_properties(p, actor, &props, error)) return false;
    if (wet != was_wet && !ph_event(p, p->world_actor.registry ? p->world_actor : actor,
        QA_PHYSICS_WATER_ENTER, wet ? old_origin : body.origin, error)) return false;
    result->status = ph_live(p, actor) ? QA_PHYSICS_MOVED : QA_PHYSICS_REMOVED;
    return true;
}

static bool physics_step(qa_physics *p, qa_actor_id actor, const qa_source_frame *frame,
    const qa_physics_motion *selected, qa_physics_result *result, qa_error *error) {
    if (!p || !p->world || !frame || !result || !isfinite(p->gravity) ||
        !isfinite(p->max_velocity) || p->max_velocity < 0 || !isfinite(p->stop_speed)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid entity physics step");
        return false;
    }
    *result = (qa_physics_result){.status = QA_PHYSICS_STOPPED};
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read < 0) return false;
    if (!read) { result->status = ph_live(p, actor) ? QA_PHYSICS_UNMANAGED : QA_PHYSICS_REMOVED; return true; }
    qa_physics_motion motion = selected ? *selected : props.motion;
    if ((!frame->elapsed_ns && (!selected || motion != QA_PHYSICS_NOCLIP)) ||
        motion == QA_PHYSICS_STATIONARY) return true;
    float seconds = (float)((double)frame->elapsed_ns*0.000000001);
    result->status = QA_PHYSICS_MOVED;
    if (selected && motion == QA_PHYSICS_NOCLIP) {
        double elapsed = (double)frame->elapsed_ns / 1000000000.0;
        body.origin = qa_v3((float)((double)body.origin.x + (double)body.velocity.x * elapsed),
            (float)((double)body.origin.y + (double)body.velocity.y * elapsed),
            (float)((double)body.origin.z + (double)body.velocity.z * elapsed));
        body.angles = qa_v3((float)((double)body.angles.x + (double)props.angular_velocity.x * elapsed),
            (float)((double)body.angles.y + (double)props.angular_velocity.y * elapsed),
            (float)((double)body.angles.z + (double)props.angular_velocity.z * elapsed));
        return ph_write(p, actor, &body, error) && ph_link(p, actor, false, error);
    }
    qa_body_attachment attachment;
    if (qa_world_attachment(p->world, actor, &attachment)) {
        qa_vec3 previous = body.origin;
        if ((motion == QA_PHYSICS_FLY || motion == QA_PHYSICS_FLY_MISSILE) &&
            !ph_angular_step(p, actor, seconds, 0, error)) return false;
        qa_trace_result trace;
        return qa_physics_push_entity(p, actor, qa_v3(0, 0, 0), NULL, 0, &trace, error) &&
               qa_physics_water_transition(p, actor, previous, error);
    }
    if (motion == QA_PHYSICS_PUSH || motion == QA_PHYSICS_STOP) {
        qa_physics_push push = {.actor = actor, .displacement = qa_vec_scale(body.velocity, seconds),
            .angular_displacement = qa_vec_scale(props.angular_velocity, seconds)};
        return qa_physics_push_pusher(p, &push, result, error);
    }
    if (motion == QA_PHYSICS_NOCLIP) {
        body.origin = qa_vec_add(body.origin, qa_vec_scale(body.velocity, seconds));
        body.angles = qa_vec_add(body.angles, qa_vec_scale(props.angular_velocity, seconds));
        return ph_write(p, actor, &body, error) && ph_link(p, actor, false, error);
    }
    if (motion == QA_PHYSICS_NEW_TOSS) return ph_new_toss(p, actor, seconds, result, error);
    if (props.family != QA_GAME_Q1 && motion != QA_PHYSICS_STEP &&
        ph_grounded(&body, &props) && (!ph_live(p, qa_physics_actor_reference(p, body.ground)) ||
        qa_vec_dot(body.velocity, props.gravity_direction) < 0)) {
        if (!ph_ground(p, actor, ph_none(), error)) return false;
        body.ground = (qa_actor_reference){0};
    }
    qa_vec3 velocity = ph_limit(body.velocity, p->max_velocity);
    if (motion == QA_PHYSICS_STEP) {
        if (props.family == QA_GAME_Q1) {
            if (ph_grounded(&body, &props) || (props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) return true;
            bool sound = body.velocity.z < -p->gravity*0.1f;
            body.velocity = ph_limit(qa_vec_add(body.velocity,
                qa_vec_scale(props.gravity_direction, props.gravity_scale*p->gravity*seconds)), p->max_velocity);
            if (!ph_write(p, actor, &body, error) || !qa_physics_fly_move(p, actor, seconds, false, result, error) ||
                !ph_link(p, actor, true, error)) return false;
            read = ph_read(p, actor, &body, &props, error);
            if (read < 0) return false;
            if (!read) { result->status = QA_PHYSICS_REMOVED; return true; }
            return !sound || !ph_grounded(&body, &props) || ph_event(p, actor, QA_PHYSICS_LAND, body.origin, error);
        }
        if (props.family != QA_GAME_Q2 && !ph_grounded(&body, &props) &&
            qa_vec_dot(velocity, props.gravity_direction) >= -100) {
            qa_trace_result floor;
            if (!ph_body_trace(p, actor, &body, &props, body.origin,
                qa_vec_add(body.origin, qa_vec_scale(props.gravity_direction, 0.25f)), false, NULL, 0, &floor, error)) return false;
            if (floor.fraction < 1 && !floor.start_solid && qa_vec_dot(floor.plane.normal, props.gravity_direction) <= -0.7f) {
                body.ground = ph_reference(p, actor, ph_hit(p, &floor));
                if (!ph_ground(p, actor, ph_hit(p, &floor), error)) return false;
            }
        }
        bool was_grounded = ph_grounded(&body, &props);
        bool falling_fast = (props.family != QA_GAME_Q2 ||
            (!was_grounded && !(props.flags & QA_PHYSICS_FLYING) &&
             !((props.flags & QA_PHYSICS_SWIMMING) && props.water_level > 2))) &&
            qa_vec_dot(body.velocity, props.gravity_direction) > p->gravity*0.1f;
        if (!ph_angular_step(p, actor, seconds, 600, error)) return false;
        read = ph_read(p, actor, &body, &props, error);
        if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
        if (!ph_grounded(&body, &props) && !(props.flags & QA_PHYSICS_FLYING) &&
            !((props.flags & QA_PHYSICS_SWIMMING) && props.water_level > 2) && props.water_level == 0)
            velocity = qa_vec_add(velocity, qa_vec_scale(props.gravity_direction, props.gravity_scale*p->gravity*seconds));
        if ((props.flags & QA_PHYSICS_FLYING) && velocity.z != 0) {
            float speed = fabsf(velocity.z);
            velocity.z *= fmaxf(0, speed-seconds*fmaxf(speed, 100)*2)/speed;
        }
        if ((props.flags & QA_PHYSICS_SWIMMING) && velocity.z != 0) {
            float speed = fabsf(velocity.z);
            velocity.z *= fmaxf(0, speed-seconds*fmaxf(speed, 100)*(float)props.water_level)/speed;
        }
        if (ph_moving(velocity)) {
            if (ph_grounded(&body, &props) || (props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) {
                float speed = hypotf(velocity.x, velocity.y);
                bool bottom = true;
                if (speed > 0 && (props.flags & QA_PHYSICS_DEAD) &&
                    !ph_shared_bottom(p, actor, &body, &props, &bottom, error)) return false;
                if (speed > 0 && bottom) {
                    float fraction = fmaxf(0, speed-seconds*fmaxf(speed, 100)*6)/speed;
                    velocity.x *= fraction; velocity.y *= fraction;
                }
            }
            body.velocity = velocity;
            if (!ph_write(p, actor, &body, error) || !qa_physics_fly_move(p, actor, seconds, false, result, error) ||
                !ph_link(p, actor, true, error)) return false;
            read = ph_read(p, actor, &body, &props, error);
            if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
            return was_grounded || !falling_fast || !ph_grounded(&body, &props) ||
                ph_event(p, actor, QA_PHYSICS_LAND, body.origin, error);
        }
        body.velocity = velocity;
        return ph_write(p, actor, &body, error);
    }
    if (ph_grounded(&body, &props)) { result->status = QA_PHYSICS_STOPPED; return true; }
    if (props.motion != QA_PHYSICS_FLY && props.motion != QA_PHYSICS_FLY_MISSILE && props.motion != QA_PHYSICS_WALL_BOUNCE)
        velocity = qa_vec_add(velocity, qa_vec_scale(props.gravity_direction, props.gravity_scale*p->gravity*seconds));
    qa_vec3 old_origin = body.origin;
    qa_game_family family = props.family;
    body.velocity = velocity;
    body.angles = qa_vec_add(body.angles, qa_vec_scale(props.angular_velocity, seconds));
    if (!ph_write(p, actor, &body, error)) return false;
    qa_trace_result trace;
    if (!qa_physics_push_entity(p, actor, qa_vec_scale(velocity, seconds), NULL, 0, &trace, error)) return false;
    if (family != QA_GAME_Q1 && !qa_physics_water_transition(p, actor, old_origin, error)) return false;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    if (trace.fraction == 1) return true;
    result->collisions = 1;
    qa_vec3 normal = ph_normal(&trace);
    velocity = qa_physics_clip_velocity(body.velocity, normal,
        props.motion == QA_PHYSICS_WALL_BOUNCE ? 2 : props.motion == QA_PHYSICS_BOUNCE ? 1.5f : 1);
    if (props.motion != QA_PHYSICS_WALL_BOUNCE && qa_vec_dot(normal, props.gravity_direction) < -0.7f &&
        (qa_vec_dot(velocity, props.gravity_direction) > -60 || props.motion != QA_PHYSICS_BOUNCE)) {
        body.velocity = qa_v3(0, 0, 0);
        if (!ph_write(p, actor, &body, error) || !ph_ground(p, actor, ph_hit(p, &trace), error)) return false;
        if (p->services.read(p->services.context, actor, &props)) {
            props.angular_velocity = qa_v3(0, 0, 0);
            if (!ph_properties(p, actor, &props, error)) return false;
        }
        result->status = QA_PHYSICS_STOPPED;
    } else { body.velocity = velocity; if (!ph_write(p, actor, &body, error)) return false; }
    if (family == QA_GAME_Q1 && ph_live(p, actor)) {
        if (p->services.q1_water_transition) return p->services.q1_water_transition(p->services.context, actor, error);
        return qa_physics_water_transition(p, actor, old_origin, error);
    }
    return true;
}

bool qa_physics_step(qa_physics *p, qa_actor_id actor, const qa_source_frame *frame,
    qa_physics_result *result, qa_error *error)
{ return physics_step(p, actor, frame, NULL, result, error); }

bool qa_physics_step_source_motion(qa_physics *p, qa_actor_id actor, const qa_source_frame *frame,
    qa_physics_motion motion, qa_physics_result *result, qa_error *error)
{
    if ((unsigned)motion > QA_PHYSICS_STEP) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid captured source physics procedure");
        return false;
    }
    return physics_step(p, actor, frame, &motion, result, error);
}
