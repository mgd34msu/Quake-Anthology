#ifndef QA_Q3_COLLISION_SHARED_H
#define QA_Q3_COLLISION_SHARED_H

#include "patch.h"

static inline bool q3_same_point(qa_vec3 a, qa_vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static inline qa_q3_shape q3_prepare_shape(qa_trace_shape input, qa_vec3 *center) {
    qa_q3_shape shape = {0};
    shape.kind = input.kind;
    *center = qa_v3(0, 0, 0);
    if (input.kind == QA_SHAPE_POINT) return shape;
    *center = qa_vec_scale(qa_vec_add(input.bounds.mins, input.bounds.maxs), 0.5f);
    shape.mins = qa_vec_sub(input.bounds.mins, *center);
    shape.extents = qa_vec_sub(input.bounds.maxs, *center);
    shape.radius = fminf(shape.extents.x, shape.extents.z);
    shape.offset = qa_v3(0, 0, shape.extents.z - shape.radius);
    return shape;
}
static inline void q3_rotate_capsule(qa_q3_shape *shape, const qa_vec3 basis[3]) {
    float height = shape->offset.z;
    shape->offset = qa_v3(basis[0].z * height, -basis[1].z * height, basis[2].z * height);
}
static inline float q3_shape_expansion(const qa_q3_shape *shape, qa_collision_plane plane) {
    if (shape->kind == QA_SHAPE_CAPSULE) return shape->radius;
    uint8_t signs = plane.signbits;
    qa_vec3 corner = qa_v3((signs & 1u) != 0 ? shape->extents.x : shape->mins.x,
                          (signs & 2u) != 0 ? shape->extents.y : shape->mins.y,
                          (signs & 4u) != 0 ? shape->extents.z : shape->mins.z);
    return -qa_vec_dot(plane.normal, corner);
}
static inline float q3_shape_distance(const qa_q3_shape *shape, qa_vec3 point, qa_collision_plane plane) {
    float distance = plane.distance + q3_shape_expansion(shape, plane);
    if (shape->kind == QA_SHAPE_CAPSULE) {
        point = qa_vec_dot(plane.normal, shape->offset) > 0
            ? qa_vec_sub(point, shape->offset) : qa_vec_add(point, shape->offset);
    }
    return qa_vec_dot(point, plane.normal) - distance;
}
static inline qa_bounds q3_shape_bounds(const qa_q3_shape *shape, qa_vec3 point) {
    if (shape->kind != QA_SHAPE_CAPSULE)
        return (qa_bounds){qa_vec_add(point, shape->mins), qa_vec_add(point, shape->extents)};
    qa_vec3 extent = qa_v3(fabsf(shape->offset.x) + shape->radius,
                          fabsf(shape->offset.y) + shape->radius,
                          fabsf(shape->offset.z) + shape->radius);
    return (qa_bounds){qa_vec_sub(point, extent), qa_vec_add(point, extent)};
}
static inline void q3_finish_trace(const qa_trace_query *query, qa_trace_result *result) {
    result->end = result->fraction == 1.0f ? query->end : qa_vec_lerp(query->start, query->end, result->fraction);
    result->contact = !result->all_solid && result->fraction != 1.0f
        && !q3_same_point(result->plane.normal, qa_v3(0, 0, 0));
    if (result->contact) result->contact_plane = result->plane;
    result->hit = result->fraction < 1.0f || result->start_solid ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
}

#endif
