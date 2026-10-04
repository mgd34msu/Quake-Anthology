#include "shared.h"

static qa_collision_plane box_plane(qa_bounds bounds, unsigned index) {
    unsigned axis = index >> 1;
    bool negative = (index & 1u) != 0;
    qa_vec3 normal = qa_v3(0, 0, 0);
    qa_vec_set_component(&normal, axis, negative ? -1.0f : 1.0f);
    float distance = negative ? -qa_vec_component(bounds.mins, axis) : qa_vec_component(bounds.maxs, axis);
    return qa_collision_make_plane(normal, distance, (int32_t)(axis + (negative ? 3u : 0u)));
}

static void trace_box(qa_trace_result *result, qa_bounds target, qa_vec3 start, qa_vec3 end,
                      const qa_q3_shape *shape, bool stationary, uint32_t mask, int32_t contents) {
    if (((uint32_t)contents & mask) == 0) return;
    if (stationary) {
        if (qa_bounds_overlap(q3_shape_bounds(shape, start), target)) {
            result->start_solid = result->all_solid = true;
            result->fraction = 0;
            result->contents = contents;
        }
        return;
    }
    float enter = -1, leave = 1;
    bool start_out = false, get_out = false, has_lead = false;
    qa_collision_plane lead = {0};
    for (unsigned side = 0; side < 6; ++side) {
        qa_collision_plane plane = box_plane(target, side);
        float first = q3_shape_distance(shape, start, plane);
        float last = q3_shape_distance(shape, end, plane);
        if (first > 0) start_out = true;
        if (last > 0) get_out = true;
        if (first > 0 && (last >= 0.125f || last >= first)) return;
        if (first <= 0 && last <= 0) continue;
        if (first > last) {
            float fraction = fmaxf(0, (first - 0.125f) / (first - last));
            if (fraction > enter) { enter = fraction; lead = plane; has_lead = true; }
        } else leave = fminf(leave, fminf(1, (first + 0.125f) / (first - last)));
    }
    if (!start_out) {
        result->start_solid = true;
        if (!get_out) { result->all_solid = true; result->fraction = 0; result->contents = contents; }
    } else if (enter < leave && enter > -1 && enter < result->fraction && has_lead) {
        result->fraction = fmaxf(0, enter);
        result->plane = lead;
        result->contents = contents;
        result->surface_flags = 0;
    }
}

/* This approximation belongs to the source capsule sweep, including its
 * second Newton step. Ordinary vector lengths use sqrtf. */
static float capsule_root(float number) {
    uint32_t bits;
    memcpy(&bits, &number, sizeof(bits));
    bits = UINT32_C(0x5f3759df) - (bits >> 1);
    float y;
    memcpy(&y, &bits, sizeof(y));
    float half = number * 0.5f;
    y *= 1.5f - half * y * y;
    y *= 1.5f - half * y * y;
    return number * y;
}

static float line_distance_squared(qa_vec3 point, qa_vec3 start, qa_vec3 end, qa_vec3 direction) {
    qa_vec3 projection = qa_vec_add(start, qa_vec_scale(direction, qa_vec_dot(qa_vec_sub(point, start), direction)));
    for (unsigned axis = 0; axis < 3; ++axis) {
        float projected = qa_vec_component(projection, axis);
        float first = qa_vec_component(start, axis), last = qa_vec_component(end, axis);
        if ((projected > first && projected > last) || (projected < first && projected < last)) {
            qa_vec3 delta = qa_vec_sub(point, fabsf(projected - first) < fabsf(projected - last) ? start : end);
            return qa_vec_dot(delta, delta);
        }
    }
    qa_vec3 delta = qa_vec_sub(point, projection);
    return qa_vec_dot(delta, delta);
}

static void trace_rounded(qa_trace_result *result, qa_vec3 origin, float radius,
                          bool cylinder, float halfheight, qa_vec3 start, qa_vec3 end,
                          qa_vec3 model_origin, int32_t contents) {
    qa_vec3 first = start, last = end, center = origin;
    if (cylinder) { first.z = 0; last.z = 0; center.z = 0; }
    qa_vec3 delta = qa_vec_sub(first, center), end_delta = qa_vec_sub(last, center);
    float radius_squared = radius * radius;
    if ((!cylinder || (start.z <= origin.z + halfheight && start.z >= origin.z - halfheight))
        && qa_vec_dot(delta, delta) < radius_squared) {
        result->fraction = 0;
        result->start_solid = true;
        if (qa_vec_dot(end_delta, end_delta) < radius_squared) result->all_solid = true;
        return;
    }
    qa_vec3 movement = qa_vec_sub(last, first);
    float length = qa_vec_length(movement);
    qa_vec3 direction = length == 0 ? qa_v3(0, 0, 0) : qa_vec_scale(movement, 1.0f / length);
    float closest = line_distance_squared(center, first, last, direction);
    float near_radius = radius + 0.125f;
    if (closest >= radius_squared && qa_vec_dot(end_delta, end_delta) > near_radius * near_radius) return;
    float inflated = radius + 1;
    float b = 2 * qa_vec_dot(direction, delta);
    float c = qa_vec_dot(delta, delta) - inflated * inflated;
    float determinant = b * b - 4 * c;
    if (determinant <= 0) return;
    float fraction = (-b - capsule_root(determinant)) * 0.5f;
    fraction = fraction < 0 ? 0 : fraction / length;
    if (!(fraction < result->fraction)) return;
    qa_vec3 intersection = qa_vec_lerp(start, end, fraction);
    if (cylinder && (intersection.z > origin.z + halfheight || intersection.z < origin.z - halfheight)) return;
    qa_vec3 normal = qa_vec_sub(intersection, origin);
    if (cylinder) normal.z = 0;
    normal = qa_vec_scale(normal, 1.0f / inflated);
    result->fraction = fraction;
    result->plane.normal = normal;
    result->plane.distance = qa_vec_dot(normal, qa_vec_add(model_origin, intersection));
    result->contents = contents;
}

static void position_capsule(qa_trace_result *result, qa_vec3 center, const qa_q3_shape *target,
                             qa_vec3 start, const qa_q3_shape *shape) {
    qa_vec3 endpoints[2] = {qa_vec_add(start, shape->offset), qa_vec_sub(start, shape->offset)};
    qa_vec3 target_endpoints[2] = {qa_vec_add(center, target->offset), qa_vec_sub(center, target->offset)};
    float radius = shape->radius + target->radius, squared = radius * radius;
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < 2; ++j) {
        qa_vec3 delta = qa_vec_sub(target_endpoints[j], endpoints[i]);
        if (qa_vec_dot(delta, delta) < squared) {
            result->all_solid = result->start_solid = true;
            result->fraction = 0;
        }
    }
    /* CM_TestCapsuleInCapsule keeps this upper/lower comparison order. */
    if ((endpoints[0].z >= target_endpoints[0].z && endpoints[0].z <= target_endpoints[1].z)
        || (endpoints[1].z >= target_endpoints[0].z && endpoints[1].z <= target_endpoints[1].z)) {
        qa_vec3 delta = qa_vec_sub(endpoints[0], target_endpoints[0]);
        delta.z = 0;
        if (qa_vec_dot(delta, delta) < squared) {
            result->all_solid = result->start_solid = true;
            result->fraction = 0;
        }
    }
}

static bool trace_shape(const qa_trace_query *query, qa_shape_kind target_kind, qa_bounds target_bounds,
                         qa_vec3 origin, int32_t contents, bool transformed, void *replacement_map,
                         qa_trace_result *out, qa_error *error) {
    if (query == NULL || out == NULL || !qa_vec_finite(query->start) || !qa_vec_finite(query->end)
        || !qa_vec_finite(origin) || !qa_vec_finite(query->target.angles)
        || !qa_vec_finite(target_bounds.mins) || !qa_vec_finite(target_bounds.maxs)
        || (target_kind != QA_SHAPE_BOX && target_kind != QA_SHAPE_CAPSULE)
        || query->shape.kind < QA_SHAPE_POINT || query->shape.kind > QA_SHAPE_CAPSULE
        || (query->shape.kind != QA_SHAPE_POINT && !qa_bounds_valid(query->shape.bounds))
        || (target_kind == QA_SHAPE_CAPSULE && !qa_bounds_valid(target_bounds))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 temporary collision query"); return false;
    }
    qa_trace_result result = qa_collision_empty_trace(query, QA_COLLISION_Q3);
    qa_vec3 center, basis[3];
    qa_q3_shape shape = q3_prepare_shape(query->shape, &center);
    float moving_halfheight = shape.extents.z;
    qa_vec3 start = qa_vec_sub(qa_vec_add(query->start, center), origin);
    qa_vec3 end = qa_vec_sub(qa_vec_add(query->end, center), origin);
    bool rotated = target_kind == QA_SHAPE_CAPSULE && !q3_same_point(query->target.angles, qa_v3(0, 0, 0));
    if (rotated) {
        qa_collision_basis(query->target.angles, basis);
        start = qa_collision_to_local(start, basis); end = qa_collision_to_local(end, basis);
        q3_rotate_capsule(&shape, basis);
    }
    bool stationary = transformed ? q3_same_point(start, end) : q3_same_point(query->start, query->end);
    if (transformed) {
        center = qa_vec_scale(qa_vec_add(shape.mins, shape.extents), 0.5f);
        shape.mins = qa_vec_sub(shape.mins, center); shape.extents = qa_vec_sub(shape.extents, center);
        start = qa_vec_add(start, center); end = qa_vec_add(end, center);
    }
    uint32_t mask = qa_collision_geometry_mask(&query->policy, QA_COLLISION_Q3);
    if (target_kind == QA_SHAPE_BOX) trace_box(&result, target_bounds, start, end, &shape, stationary, mask, contents);
    else {
        qa_vec3 target_center;
        qa_q3_shape target = q3_prepare_shape((qa_trace_shape){QA_SHAPE_CAPSULE, target_bounds}, &target_center);
        if (shape.kind != QA_SHAPE_CAPSULE) {
            if (replacement_map != NULL) {
                qa_bounds original = {qa_vec_add(start, shape.mins), qa_vec_add(start, shape.extents)};
                bool point_trace = q3_same_point(shape.mins, qa_v3(0, 0, 0));
                shape.kind = QA_SHAPE_CAPSULE;
                shape.radius = target.radius;
                shape.offset = target.offset;
                if (!qa_q3_trace_capsule_replacement(replacement_map, query,
                        qa_vec_sub(start, target_center), qa_vec_sub(end, target_center),
                        shape, original, stationary, point_trace, &result, error)) return false;
            } else if (stationary) {
                /* The source swap retains original position bounds and tests
                 * them against the original moving box, then skips six sides. */
                qa_bounds original = {qa_vec_add(start, shape.mins), qa_vec_add(start, shape.extents)};
                qa_bounds box = {shape.mins, shape.extents};
                if (((uint32_t)contents & mask) != 0 && qa_bounds_overlap(original, box)) {
                    result.fraction = 0; result.all_solid = result.start_solid = true; result.contents = contents;
                }
            } else {
                trace_box(&result, (qa_bounds){shape.mins, shape.extents},
                    qa_vec_sub(start, target_center), qa_vec_sub(end, target_center), &target, false, mask, contents);
            }
        } else if (stationary) position_capsule(&result, target_center, &target, start, &shape);
        else {
            qa_bounds swept = qa_bounds_union(q3_shape_bounds(&shape, start), q3_shape_bounds(&shape, end));
            qa_bounds bounds = {qa_vec_sub(target_bounds.mins, qa_v3(1, 1, 1)), qa_vec_add(target_bounds.maxs, qa_v3(1, 1, 1))};
            if (qa_bounds_overlap(swept, bounds)) {
                float radius = target.radius + shape.radius;
                float halfheight = target.extents.z + moving_halfheight - radius;
                if ((start.x != end.x || start.y != end.y) && halfheight > 0)
                    trace_rounded(&result, target_center, radius, true, halfheight, start, end, origin, contents);
                trace_rounded(&result, qa_vec_add(target_center, target.offset), radius, false, 0,
                    qa_vec_sub(start, shape.offset), qa_vec_sub(end, shape.offset), origin, contents);
                trace_rounded(&result, qa_vec_sub(target_center, target.offset), radius, false, 0,
                    qa_vec_add(start, shape.offset), qa_vec_add(end, shape.offset), origin, contents);
            }
        }
    }
    if (rotated && result.fraction != 1)
        result.plane.normal = qa_collision_from_local(result.plane.normal, basis);
    q3_finish_trace(query, &result);
    *out = result;
    return true;
}

bool qa_q3_trace_shape(const qa_trace_query *query, qa_shape_kind target_kind, qa_bounds target_bounds,
                        qa_vec3 origin, int32_t contents, qa_trace_result *out, qa_error *error) {
    return trace_shape(query, target_kind, target_bounds, origin, contents, true, NULL, out, error);
}

bool qa_q3_trace_capsule_source(const qa_trace_query *query, qa_bounds bounds, bool transformed,
                                void *replacement_map, qa_trace_result *out, qa_error *error) {
    qa_trace_query local = *query;
    if (!transformed) local.target = (qa_collision_target){0};
    return trace_shape(&local, QA_SHAPE_CAPSULE, bounds, local.target.origin, 0x02000000,
                       transformed, replacement_map, out, error);
}

bool qa_q3_trace_box_source(const qa_trace_query *query, qa_bounds bounds, bool transformed,
                            qa_trace_result *out, qa_error *error) {
    qa_trace_query local = *query;
    if (!transformed) local.target = (qa_collision_target){0};
    return trace_shape(&local, QA_SHAPE_BOX, bounds, local.target.origin, 0x02000000,
                       transformed, NULL, out, error);
}
