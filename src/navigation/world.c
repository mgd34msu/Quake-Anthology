#include "internal.h"

bool nav_profile_valid(const qa_nav_profile *p, qa_error *e) {
    if (p == NULL || (unsigned)p->movement.kind > QA_MOVEMENT_Q3 || p->team > 2 ||
        !isfinite(p->maximum_step) || p->maximum_step < 0 || !isfinite(p->maximum_drop) ||
        p->maximum_drop < 0 || !isfinite(p->minimum_floor_normal) || p->minimum_floor_normal <= 0 ||
        p->minimum_floor_normal > 1)
        goto invalid;
    qa_collision_family family = p->movement.kind <= QA_MOVEMENT_QUAKEWORLD     ? QA_COLLISION_Q1
                                 : p->movement.kind <= QA_MOVEMENT_Q2_RERELEASE ? QA_COLLISION_Q2
                                                                                : QA_COLLISION_Q3;
    if (p->policy.family != family)
        goto invalid;
    const qa_trace_shape *shapes[] = {&p->shape, &p->crouched_shape};
    for (unsigned i = 0; i < (p->has_crouched_shape ? 2u : 1u); ++i) {
        qa_bounds b = shapes[i]->bounds;
        if ((shapes[i]->kind != QA_SHAPE_BOX && shapes[i]->kind != QA_SHAPE_CAPSULE) ||
            !qa_bounds_valid(b) || b.mins.x == b.maxs.x || b.mins.y == b.maxs.y ||
            b.mins.z == b.maxs.z)
            goto invalid;
    }
    return true;
invalid:
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid selected navigation profile");
    return false;
}
bool nav_services_valid(const qa_navigation_services *s, bool prediction, qa_error *e) {
    if (s && ((s->traversal_begin != NULL || s->traversal_admit != NULL ||
               s->traversal_end != NULL) &&
              (!s->traversal_begin || !s->traversal_admit || !s->traversal_end))) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Navigation source traversal needs paired ownership callbacks");
        return false;
    }
    if (s == NULL || s->world == NULL || (prediction && s->movement_input == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Navigation requires shared world and selected movement services");
        return false;
    }
    return true;
}
bool nav_trace(const qa_navigation_services *s, const qa_nav_profile *p, qa_actor_id actor,
               qa_vec3 start, qa_vec3 end, bool geometry, qa_trace_result *out, qa_error *e) {
    qa_trace_query q = {
        .start = start, .end = end, .shape = p->shape, .policy = p->policy, .pass_actor = actor};
    if (geometry || s->topology_geometry_only) {
        q.target.inline_model = true;
        q.target.model = 0;
        return qa_collision_trace(qa_world_geometry(s->world), &q, out, e);
    }
    return qa_world_trace(s->world, &q, out, e);
}
bool nav_clear(const qa_navigation_services *s, const qa_nav_profile *p, qa_actor_id actor,
               qa_vec3 start, qa_vec3 end, bool geometry, bool *clear, qa_error *e) {
    qa_trace_result trace;
    if (!nav_trace(s, p, actor, start, end, geometry, &trace, e))
        return false;
    *clear = !trace.start_solid && !trace.all_solid && trace.fraction == 1;
    return true;
}
bool nav_contents(const qa_navigation_services *s, const qa_nav_profile *p, qa_actor_id actor,
                  qa_vec3 point, bool geometry, uint32_t *out, qa_error *e) {
    geometry = geometry || s->topology_geometry_only;
    qa_point_query q = {.point = point, .policy = p->policy, .pass_actor = actor};
    qa_point_contents sample;
    if (geometry) {
        q.target.inline_model = true;
        q.target.model = 0;
        if (!qa_collision_point_contents(qa_world_geometry(s->world), &q, &sample, e))
            return false;
    } else if (!qa_world_point_contents(s->world, &q, &sample, e))
        return false;
    if (sample.family == QA_COLLISION_Q1) {
        *out = sample.contents == -3   ? QA_NAV_WATER
               : sample.contents == -4 ? QA_NAV_SLIME
               : sample.contents == -5 ? QA_NAV_LAVA
                                       : 0;
    } else {
        uint32_t value =
            (uint32_t)(sample.family == QA_COLLISION_Q2 ? sample.merged : sample.contents);
        *out = ((value & 32) != 0 ? QA_NAV_WATER : 0) | ((value & 16) != 0 ? QA_NAV_SLIME : 0) |
               ((value & 8) != 0 ? QA_NAV_LAVA : 0) |
               (sample.family == QA_COLLISION_Q2 && (value & UINT32_C(0x20000000)) != 0
                    ? QA_NAV_CONTENTS_LADDER
                    : 0);
    }
    return true;
}
bool nav_crouch_profile(const qa_nav_profile *p, qa_nav_profile *out) {
    if (!p->has_crouched_shape || (p->capabilities & QA_NAV_CAPABILITY(QA_NAV_CROUCH)) == 0)
        return false;
    *out = *p;
    out->shape = p->crouched_shape;
    return true;
}
bool nav_node_profile(const qa_nav_profile *p, const qa_nav_node *n, qa_nav_profile *out) {
    bool crouched = n->source.kind == QA_NAV_ORIGIN_AAS
                        ? (n->presence & 2) == 0 && (n->presence & 4) != 0
                    : n->source.kind == QA_NAV_ORIGIN_NAV3
                        ? (n->flags & 512) != 0
                        : n->source.kind == QA_NAV_ORIGIN_CONSTRUCTED && n->presence == 4;
    if (crouched)
        return nav_crouch_profile(p, out);
    *out = *p;
    return true;
}
