#include "internal.h"

static bool monster_trace(qa_physics *p, qa_actor_id actor,
                           const qa_physics_properties *props, qa_vec3 start,
                           qa_vec3 end, const qa_bounds *bounds,
                           qa_trace_result *trace, qa_error *error) {
    uint32_t mask = PH_MONSTER_MASK | (props->family == QA_COLLISION_Q2 && props->q2_rerelease ? UINT32_C(0x40000000) : 0);
    return ph_trace(p, actor, props, start, end, bounds, mask,
                     bounds ? QA_Q1_MOVE_NORMAL : QA_Q1_MOVE_NO_MONSTERS,
                     NULL, 0, trace, error);
}

static bool monster_contents(qa_physics *p, qa_actor_id actor,
                              const qa_physics_properties *props, qa_vec3 point,
                              int32_t *value, qa_error *error) {
    qa_point_contents contents;
    if (!ph_contents(p, actor, props, point, &contents, error)) return false;
    *value = contents.contents;
    return true;
}

static bool monster_bottom_state(qa_physics *p, qa_actor_id actor,
    qa_body_state body, qa_physics_properties props, qa_vec3 origin,
    bool *supported, qa_error *error) {
    *supported = false;
    qa_bounds box = qa_bounds_translate(body.bounds, origin);
    bool q1 = props.family == QA_COLLISION_Q1;
    float direction = !q1 && props.gravity_direction.z > 0 ? 1 : -1;
    float support = direction > 0 ? box.maxs.z : box.mins.z;
    float xs[2] = {box.mins.x, box.maxs.x}, ys[2] = {box.mins.y, box.maxs.y};
    bool easy = true;
    for (unsigned x = 0; x < 2 && easy; ++x) for (unsigned y = 0; y < 2; ++y) {
        int32_t contents;
        if (!monster_contents(p, actor, &props, qa_v3(xs[x], ys[y], support+direction), &contents, error)) return false;
        if (contents != (q1 ? -2 : 1)) { easy = false; break; }
    }
    if (easy) { *supported = true; return true; }
    qa_vec3 center = qa_v3((box.mins.x+box.maxs.x)*0.5f, (box.mins.y+box.maxs.y)*0.5f, support);
    bool rerelease = !q1 && props.q2_rerelease;
    qa_vec3 start = rerelease ? qa_v3(origin.x, origin.y, support) : center;
    qa_vec3 end = start; end.z += direction*36;
    qa_bounds flat = body.bounds;
    flat.mins.z = flat.maxs.z = 0;
    qa_trace_result middle;
    if (!monster_trace(p, actor, &props, start, end, rerelease ? &flat : NULL, &middle, error)) return false;
    if (middle.fraction == 1) return true;
    if (rerelease && (props.flags & QA_PHYSICS_SUPER_STEP)) { *supported = true; return true; }
    qa_vec3 quarter = qa_v3((box.maxs.x-box.mins.x)*0.25f, (box.maxs.y-box.mins.y)*0.25f, 0);
    qa_bounds quadrant = {qa_vec_scale(quarter, -1), quarter};
    for (unsigned x = 0; x < 2; ++x) for (unsigned y = 0; y < 2; ++y) {
        start = rerelease ? qa_v3(center.x+(x ? quarter.x : -quarter.x),
                                  center.y+(y ? quarter.y : -quarter.y), support) :
                            qa_v3(xs[x], ys[y], support);
        end = start; end.z += direction*36;
        qa_trace_result corner;
        if (!monster_trace(p, actor, &props, start, end, rerelease ? &quadrant : NULL, &corner, error)) return false;
        if (corner.fraction == 1 || (corner.end.z-middle.end.z)*direction > 18) return true;
    }
    *supported = true;
    return true;
}

bool qa_physics_check_bottom(qa_physics *p, qa_actor_id actor, qa_vec3 origin,
                             bool *supported, qa_error *error) {
    if (!p || !supported || !qa_vec_finite(origin)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid monster bottom query"); return false;
    }
    *supported = false;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    return monster_bottom_state(p, actor, body, props, origin, supported, error);
}

bool qa_physics_check_ground(qa_physics *p, qa_actor_id actor, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    if (props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)) return true;
    if (body.velocity.z*props.gravity_direction.z < -100)
        return ph_ground(p, actor, ph_none(), error);
    qa_trace_result trace;
    qa_vec3 destination = body.origin;
    destination.z += 0.25f*props.gravity_direction.z;
    if (!monster_trace(p, actor, &props, body.origin,
        destination, &body.bounds, &trace, error)) return false;
    bool steep = !trace.contact || (props.gravity_direction.z < 0 ? ph_normal(&trace).z < 0.7f : ph_normal(&trace).z > -0.7f);
    if (steep && !trace.start_solid)
        return ph_ground(p, actor, ph_none(), error);
    if (!trace.start_solid && !trace.all_solid) {
        body.origin = trace.end;
        body.velocity.z = 0;
        if (!ph_write(p, actor, &body, error)) return false;
        return ph_ground(p, actor, ph_hit(p, &trace), error);
    }
    return true;
}

bool qa_physics_categorize_water(qa_physics *p, qa_actor_id actor, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    qa_vec3 feet = body.origin;
    feet.z += props.gravity_direction.z > 0 ? body.bounds.maxs.z-1 : body.bounds.mins.z+1;
    int32_t contents;
    if (!monster_contents(p, actor, &props, feet, &contents, error)) return false;
    int32_t level = 0;
    if (ph_wet(props.family, contents)) {
        level = 1;
        qa_vec3 point = feet; point.z += 26;
        int32_t deeper;
        if (!monster_contents(p, actor, &props, point, &deeper, error)) return false;
        if (ph_wet(props.family, deeper)) {
            level = 2;
            point.z = feet.z+48;
            if (!monster_contents(p, actor, &props, point, &deeper, error)) return false;
            if (ph_wet(props.family, deeper)) level = 3;
        }
    }
    props.water_level = level;
    props.water_type = level ? contents : props.family == QA_COLLISION_Q1 ? -1 : 0;
    return ph_properties(p, actor, &props, error);
}

bool qa_physics_drop_to_floor(qa_physics *p, qa_actor_id actor, float distance,
                              bool *dropped, qa_error *error) {
    if (!p || !dropped || !isfinite(distance) || distance < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid drop-to-floor distance"); return false;
    }
    *dropped = false;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    qa_vec3 start = body.origin;
    bool q1 = props.family == QA_COLLISION_Q1;
    float direction = !q1 && props.gravity_direction.z > 0 ? 1 : -1;
    if (!q1) {
        bool offset = !props.q2_rerelease;
        if (!offset) {
            qa_trace_result position;
            if (!monster_trace(p, actor, &props, start, start, &body.bounds, &position, error)) return false;
            offset = position.start_solid;
        }
        if (offset) start.z -= direction;
        body.origin = start;
        if (!ph_write(p, actor, &body, error)) return false;
    }
    qa_trace_result trace;
    qa_vec3 end = start; end.z += direction*distance;
    if (!monster_trace(p, actor, &props, start,
        end, &body.bounds, &trace, error)) return false;
    if (trace.fraction == 1 || trace.all_solid) return true;
    body.origin = trace.end;
    if (!ph_write(p, actor, &body, error) || !ph_link(p, actor, false, error)) return false;
    if (q1) {
        if (!ph_ground(p, actor, ph_hit(p, &trace), error)) return false;
    } else if (!qa_physics_check_ground(p, actor, error) || !qa_physics_categorize_water(p, actor, error)) return false;
    *dropped = ph_live(p, actor);
    return true;
}

static bool commit_monster(qa_physics *p, qa_actor_id actor, qa_vec3 origin,
                            bool change_ground, qa_actor_id ground, bool clear_partial,
                            bool relink, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    body.origin = origin;
    if (change_ground && props.family == QA_COLLISION_Q1) body.ground = ground;
    if (!ph_write(p, actor, &body, error)) return false;
    if (change_ground && props.family != QA_COLLISION_Q1 && !ph_ground(p, actor, ground, error)) return false;
    if (clear_partial && ph_live(p, actor) && p->services.read(p->services.context, actor, &props)) {
        props.flags &= ~(uint32_t)QA_PHYSICS_PARTIAL_GROUND;
        if (!ph_properties(p, actor, &props, error)) return false;
    }
    return !relink || ph_link(p, actor, true, error);
}

static bool monster_step_state(qa_physics *p, qa_actor_id actor, qa_vec3 move,
    float elapsed, bool commit, bool relink, qa_body_state body,
    qa_physics_properties props, qa_body_state *detached_body,
    qa_physics_properties *detached_props, bool *moved, qa_error *error) {
    bool q1 = props.family == QA_COLLISION_Q1;
    if (!q1 && p->services.before_monster_step) {
        bool handled;
        if (!p->services.before_monster_step(p->services.context, actor, &move, &handled, error)) return false;
        if (handled) { *moved = true; return true; }
        if (!ph_live(p, actor)) return true;
        if (!qa_vec_finite(move)) {
            qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "Monster step policy returned invalid displacement");
            return false;
        }
        if (!p->services.read(p->services.context, actor, &props) || !ph_live(p, actor)) return true;
    }
    qa_vec3 destination = qa_vec_add(body.origin, move);
    if (props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)) {
        bool flying = (props.flags & QA_PHYSICS_FLYING) != 0;
        bool swimming = (props.flags & QA_PHYSICS_SWIMMING) != 0;
        bool wet = false, deep = false;
        if (!q1) {
            int32_t contents;
            qa_vec3 feet = body.origin; feet.z += body.bounds.mins.z+1;
            if (!monster_contents(p, actor, &props, feet, &contents, error)) return false;
            wet = (contents & 56) != 0;
            feet.z += 26;
            if (!monster_contents(p, actor, &props, feet, &contents, error)) return false;
            deep = (contents & 56) != 0;
        }
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            destination = qa_vec_add(body.origin, move);
            bool enemy = ph_live(p, props.enemy);
            if (attempt == 0 && enemy) {
                qa_actor_id goal = q1 ? props.enemy : props.goal.registry ? props.goal : props.enemy;
                if (!q1 && !props.goal.registry) {
                    props.goal = props.enemy;
                    if (!ph_properties(p, actor, &props, error)) return false;
                }
                qa_body_state target;
                if (ph_live(p, goal)) {
                    if (!qa_world_body_read(p->world, goal, &target, error)) return false;
                    float dz = body.origin.z-target.origin.z;
                    float stride = !q1 && props.q2_rerelease ? elapsed*80 : 8;
                    qa_physics_properties target_props;
                    bool player = !detached_body && p->services.read(p->services.context, goal, &target_props) &&
                                  (target_props.flags & QA_PHYSICS_PLAYER);
                    if (q1 || player) {
                        if (dz > 40) destination.z -= stride;
                        if (dz < 30 && (q1 || !(swimming && !deep))) destination.z += stride;
                    } else destination.z += dz > stride ? -stride : dz > 0 ? -dz : dz < -stride ? stride : dz;
                }
            }
            qa_trace_result trace;
            if (!monster_trace(p, actor, &props, body.origin, destination, &body.bounds, &trace, error)) return false;
            int32_t contents = 0;
            qa_vec3 feet = trace.end;
            if (!q1) feet.z += body.bounds.mins.z+1;
            if ((!q1 || (trace.fraction == 1 && swimming)) &&
                !monster_contents(p, actor, &props, feet, &contents, error)) return false;
            if (q1) {
                if (trace.fraction == 1 && swimming && contents == -1) return true;
            } else {
                bool enters_water = (contents & 56) != 0;
                if ((flying && !wet && enters_water) || (swimming && !deep && !enters_water)) return true;
            }
            if (trace.fraction == 1 && (q1 || (!trace.start_solid && !trace.all_solid))) {
                if (commit && !commit_monster(p, actor, trace.end, false, ph_none(), false, relink, error)) return false;
                if (detached_body) detached_body->origin = trace.end;
                *moved = true;
                return true;
            }
            if (!enemy) break;
        }
        return true;
    }
    qa_vec3 gravity = q1 ? qa_v3(0, 0, -1) : props.gravity_direction;
    qa_vec3 start = qa_vec_sub(destination, qa_vec_scale(gravity, 18));
    qa_vec3 end = qa_vec_add(start, qa_vec_scale(gravity, 36));
    qa_trace_result trace;
    if (!monster_trace(p, actor, &props, start, end, &body.bounds, &trace, error)) return false;
    if (trace.all_solid) return true;
    if (trace.start_solid) {
        qa_vec3 retry = destination;
        if (!q1 && !props.q2_rerelease) { retry = start; retry.z -= 18; }
        if (!monster_trace(p, actor, &props, retry, end, &body.bounds, &trace, error)) return false;
        if (trace.all_solid || trace.start_solid) return true;
    }
    if (!q1) {
        float offset = gravity.z > 0 ? body.bounds.maxs.z-1 : body.bounds.mins.z+1;
        qa_vec3 feet = body.origin; feet.z += offset;
        int32_t before, after;
        if (!monster_contents(p, actor, &props, feet, &before, error)) return false;
        feet = trace.end; feet.z += offset;
        if (!monster_contents(p, actor, &props, feet, &after, error)) return false;
        if (!(before & 56) && (after & 56)) return true;
    }
    if (trace.fraction == 1) {
        if (!(props.flags & QA_PHYSICS_PARTIAL_GROUND)) return true;
        /* Q1 clears onground after the synchronous link/touch callback. */
        if (commit && !commit_monster(p, actor, destination, !q1, ph_none(), false, relink, error)) return false;
        if (commit && q1 && ph_live(p, actor) && p->services.read(p->services.context, actor, &props)) {
            props.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
            if (!ph_properties(p, actor, &props, error)) return false;
        }
        if (detached_body) {
            detached_body->origin = destination;
            detached_props->flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        }
        *moved = true;
        return true;
    }
    if (!q1 && p->services.accept_ground) {
        bool accepted;
        if (!p->services.accept_ground(p->services.context, actor, trace.end, &accepted, error)) return false;
        if (!accepted) return true;
    }
    if (!ph_live(p, actor)) return true;
    if (q1 && commit) {
        body.origin = trace.end;
        if (!ph_write(p, actor, &body, error)) return false;
    }
    bool bottom;
    if (detached_body) {
        if (!monster_bottom_state(p, actor, body, props, trace.end, &bottom, error)) return false;
    } else if (!qa_physics_check_bottom(p, actor, trace.end, &bottom, error)) return false;
    if (!bottom) {
        if (!(props.flags & QA_PHYSICS_PARTIAL_GROUND)) {
            if (q1 && commit) {
                body.origin = qa_vec_sub(destination, move);
                if (!ph_write(p, actor, &body, error)) return false;
            }
            return true;
        }
        if (commit && !commit_monster(p, actor, trace.end, false, ph_none(), false, relink, error)) return false;
        if (detached_body) detached_body->origin = trace.end;
        *moved = true;
        return true;
    }
    if (trace.hit == QA_TRACE_HIT_NONE) {
        qa_error_set(error, QA_ERROR_FORMAT, actor.slot, "Monster landed without a ground hit"); return false;
    }
    if (commit && !commit_monster(p, actor, trace.end, true, ph_hit(p, &trace), true, relink, error)) return false;
    if (detached_body) {
        detached_body->origin = trace.end;
        detached_body->ground = ph_hit(p, &trace);
        detached_props->flags &= ~(uint32_t)QA_PHYSICS_PARTIAL_GROUND;
    }
    *moved = true;
    return true;
}

bool qa_physics_monster_step(qa_physics *p, qa_actor_id actor, qa_vec3 move,
                             float elapsed, bool commit, bool relink,
                             bool *moved, qa_error *error) {
    if (!p || !moved || !qa_vec_finite(move) || !isfinite(elapsed) || elapsed < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid monster step"); return false;
    }
    *moved = false;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    return monster_step_state(p, actor, move, elapsed, commit, relink,
        body, props, NULL, NULL, moved, error);
}

bool qa_physics_monster_walk_detached(qa_physics *p, qa_actor_id actor,
    const qa_body_state *initial, const qa_physics_properties *initial_source,
    float yaw, float distance, qa_body_state *out,
    qa_physics_properties *out_source, bool *moved, qa_error *error) {
    if (!p || !p->world || !initial || !initial_source || !out || !out_source || !moved ||
        initial_source->family != QA_COLLISION_Q1 || !isfinite(yaw) || !isfinite(distance) ||
        !qa_vec_finite(initial->origin) || !qa_vec_finite(initial->angles) ||
        !qa_vec_finite(initial->velocity) || !qa_vec_finite(initial->bounds.mins) ||
        !qa_vec_finite(initial->bounds.maxs) || initial->bounds.mins.x > initial->bounds.maxs.x ||
        initial->bounds.mins.y > initial->bounds.maxs.y || initial->bounds.mins.z > initial->bounds.maxs.z) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached monster walk requires actual Q1 state and finite bounds");
        return false;
    }
    qa_body_state body = *initial;
    qa_physics_properties props = *initial_source;
    *out = body; *out_source = props; *moved = false;
    if (!ph_live(p, actor) || !(props.flags & (QA_PHYSICS_ONGROUND | QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)))
        return true;
    float radians = yaw*0.01745329251994329577f;
    qa_vec3 move = qa_v3(cosf(radians)*distance, sinf(radians)*distance, 0);
    if (!qa_vec_finite(move)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Detached monster walk displacement overflowed"); return false;
    }
    if (!monster_step_state(p, actor, move, 0, false, false, body, props, out, out_source, moved, error)) return false;
    if (!ph_live(p, actor)) { *out = body; *out_source = props; *moved = false; }
    return true;
}

bool qa_physics_walk_move(qa_physics *p, qa_actor_id actor, float yaw, float distance,
                          float elapsed, bool commit, bool relink,
                          bool *moved, qa_error *error) {
    if (!p || !moved || !isfinite(yaw) || !isfinite(distance)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid monster walk move"); return false;
    }
    *moved = false;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    if (props.family == QA_COLLISION_Q1) {
        if (!(props.flags & (QA_PHYSICS_ONGROUND | QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) return true;
    } else if (props.motion == QA_PHYSICS_STATIONARY ||
        (!body.ground.registry && !(props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)))) return true;
    float radians = yaw*0.01745329251994329577f;
    return qa_physics_monster_step(p, actor, qa_v3(cosf(radians)*distance, sinf(radians)*distance, 0),
                                    elapsed, commit, relink, moved, error);
}

static float anglemod(float angle) {
    float result = fmodf(angle, 360);
    return result < 0 ? result+360 : result;
}

bool qa_physics_change_yaw(qa_physics *p, qa_actor_id actor, float elapsed, qa_error *error) {
    if (!p || !isfinite(elapsed) || elapsed < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid monster yaw interval"); return false;
    }
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    bool q1 = props.family == QA_COLLISION_Q1;
    float current = q1 ? qa_angle_mod(body.angles.y) : anglemod(body.angles.y);
    float move = props.ideal_yaw-current;
    if (q1) {
        if (props.ideal_yaw > current) { if (move >= 180) move -= 360; }
        else if (move <= -180) move += 360;
    } else {
        if (move > 180) move -= 360;
        if (move < -180) move += 360;
    }
    float speed = props.yaw_speed*(!q1 && props.q2_rerelease ? elapsed*10 : 1);
    move = fmaxf(-speed, fminf(speed, move));
    body.angles.y = q1 ? qa_angle_mod(current+move) : anglemod(current+move);
    return ph_write(p, actor, &body, error);
}

bool qa_physics_step_direction(qa_physics *p, qa_actor_id actor, float yaw,
                               float distance, float elapsed, bool *moved, qa_error *error) {
    if (!p || !moved || !isfinite(yaw) || !isfinite(distance)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid monster step direction"); return false;
    }
    *moved = false;
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    props.ideal_yaw = yaw;
    if (!ph_properties(p, actor, &props, error) || !qa_physics_change_yaw(p, actor, elapsed, error)) return false;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    bool q1 = props.family == QA_COLLISION_Q1;
    qa_vec3 original = body.origin;
    float radians = yaw*0.01745329251994329577f;
    if (q1) {
        if (!qa_physics_monster_step(p, actor, qa_v3(cosf(radians)*distance, sinf(radians)*distance, 0),
                                      elapsed, true, false, moved, error)) return false;
    } else if (!qa_physics_walk_move(p, actor, yaw, distance, elapsed, true, false, moved, error)) return false;
    read = ph_read(p, actor, &body, &props, error);
    if (read < 0) return false;
    if (!read) { if (!q1) *moved = true; return true; }
    if (*moved) {
        float delta = q1 ? body.angles.y-props.ideal_yaw : anglemod(body.angles.y-props.ideal_yaw);
        if (delta > 45 && delta < 315 && !(props.flags & QA_PHYSICS_KEEP_MOVE_WHILE_TURNING)) {
            body.origin = original;
            if (!ph_write(p, actor, &body, error)) return false;
        }
    }
    if (!ph_link(p, actor, true, error)) return false;
    if (!q1 && !ph_live(p, actor)) *moved = true;
    return true;
}

bool qa_physics_close_enough(qa_physics *p, qa_actor_id actor, qa_actor_id goal, float distance) {
    if (!p || !isfinite(distance)) return false;
    qa_linked_body a, b;
    if (!ph_live(p, actor) || !ph_live(p, goal) ||
        !qa_world_linked(p->world, actor, &a) || !qa_world_linked(p->world, goal, &b)) return false;
    qa_vec3 extra = qa_v3(distance, distance, distance);
    return qa_bounds_overlap((qa_bounds){qa_vec_sub(a.absolute_bounds.mins, extra),
                                        qa_vec_add(a.absolute_bounds.maxs, extra)}, b.absolute_bounds);
}

static bool chase_step(qa_physics *p, qa_actor_id actor, float yaw, float distance,
                        bool *stop, qa_error *error) {
    if (!ph_live(p, actor)) { *stop = true; return true; }
    return qa_physics_step_direction(p, actor, yaw, distance, 0.1f, stop, error);
}

bool qa_physics_q1_chase_direction(qa_physics *p, qa_actor_id actor, qa_actor_id goal,
                                   float distance, qa_error *error) {
    if (!p || !isfinite(distance)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q1 chase move"); return false;
    }
    qa_body_state body, enemy;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    if (!ph_live(p, goal)) return true;
    if (!qa_world_body_read(p->world, goal, &enemy, error)) return false;
    if (!p->services.random_integer) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 chase requires the game's random source"); return false;
    }
    float old = qa_angle_mod(truncf(props.ideal_yaw/45)*45), turnaround = qa_angle_mod(old-180);
    qa_vec3 delta = qa_vec_sub(enemy.origin, body.origin);
    float first = delta.x > 10 ? 0 : delta.x < -10 ? 180 : -1;
    float second = delta.y < -10 ? 270 : delta.y > 10 ? 90 : -1;
    bool stop = false;
    if (first != -1 && second != -1) {
        float diagonal = first == 0 ? (second == 90 ? 45 : 315) : (second == 90 ? 135 : 215);
        if (diagonal != turnaround) {
            if (!chase_step(p, actor, diagonal, distance, &stop, error)) return false;
            if (stop) return true;
        }
    }
    if ((p->services.random_integer(p->services.context) & 1) || fabsf(delta.y) > fabsf(delta.x)) {
        float temporary = first; first = second; second = temporary;
    }
    if (first != -1 && first != turnaround) {
        if (!chase_step(p, actor, first, distance, &stop, error)) return false;
        if (stop) return true;
    }
    if (second != -1 && second != turnaround) {
        if (!chase_step(p, actor, second, distance, &stop, error)) return false;
        if (stop) return true;
    }
    if (!chase_step(p, actor, old, distance, &stop, error)) return false;
    if (stop) return true;
    bool ascending = (p->services.random_integer(p->services.context) & 1) != 0;
    for (unsigned i = 0; i < 8; ++i) {
        float direction = (float)(ascending ? i*45 : 315-i*45);
        if (direction == turnaround) continue;
        if (!chase_step(p, actor, direction, distance, &stop, error)) return false;
        if (stop) return true;
    }
    if (!chase_step(p, actor, turnaround, distance, &stop, error)) return false;
    if (stop) return true;
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    props.ideal_yaw = old;
    if (!ph_properties(p, actor, &props, error)) return false;
    bool supported;
    if (!qa_physics_check_bottom(p, actor, body.origin, &supported, error)) return false;
    if (!supported && ph_live(p, actor) && p->services.read(p->services.context, actor, &props)) {
        props.flags |= QA_PHYSICS_PARTIAL_GROUND;
        return ph_properties(p, actor, &props, error);
    }
    return true;
}

bool qa_physics_q1_move_to_goal(qa_physics *p, qa_actor_id actor, qa_actor_id goal,
                               float distance, bool contact, qa_error *error) {
    if (!p || !isfinite(distance)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q1 goal move"); return false;
    }
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) return read == 0;
    if (!(props.flags & (QA_PHYSICS_ONGROUND | QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) return true;
    if (!contact && props.enemy.registry && qa_physics_close_enough(p, actor, goal, distance)) return true;
    if (!p->services.random_integer) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 goal move requires the game's random source"); return false;
    }
    bool moved = false;
    if ((p->services.random_integer(p->services.context) & 3) != 1 &&
        !qa_physics_step_direction(p, actor, props.ideal_yaw, distance, 0.1f, &moved, error)) return false;
    return moved || !ph_live(p, actor) || qa_physics_q1_chase_direction(p, actor, goal, distance, error);
}
