#include "qa/builtin.h"

static bool damageable(const qa_builtin_services *s, qa_actor_id actor, bool *out,
                       qa_error *error) {
    qa_combat_state state;
    qa_error local = {0};
    if (!qa_combat_read(s->combat, actor, &state, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) {
            *out = false;
            return true;
        }
        if (error)
            *error = local;
        return false;
    }
    *out = state.can_take_damage;
    return true;
}

bool qa_builtin_fire_hitscan(const qa_builtin_services *s, const qa_builtin_hitscan *shot,
                             qa_trace_result *last, qa_error *error) {
    if (!s || !shot || (shot->excluded_capacity && !shot->excluded) || !isfinite(shot->damage) ||
        !isfinite(shot->knockback)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native hitscan");
        return false;
    }
    size_t count = 0;
    qa_trace_result trace;
    for (;;) {
        if (!qa_world_trace_excluding(s->world, &shot->trace, shot->excluded, count, &trace, error))
            return false;
        qa_damage_outcome outcome = {0};
        bool can_damage = false;
        if (trace.hit == QA_TRACE_HIT_ACTOR && !damageable(s, trace.actor, &can_damage, error))
            return false;
        if (can_damage) {
            qa_damage_request request = {.attack = shot->attack,
                                         .target = trace.actor,
                                         .amount = shot->damage,
                                         .knockback = shot->knockback,
                                         .direction =
                                             qa_vec_sub(shot->trace.end, shot->trace.start),
                                         .point = trace.end,
                                         .normal = trace.contact_plane.normal};
            bool apply_damage = true;
            if (shot->prepare &&
                !shot->prepare(shot->context, &trace, &request, &apply_damage, error))
                return false;
            can_damage = apply_damage;
            if (apply_damage && !qa_combat_apply(s->combat, &request, &outcome, error)) {
                qa_damage_outcome_free(&outcome);
                return false;
            }
        }
        bool again = false;
        bool ok = !shot->hit ||
                  shot->hit(shot->context, &trace, can_damage ? &outcome : NULL, &again, error);
        qa_damage_outcome_free(&outcome);
        if (!ok)
            return false;
        if (!again || trace.hit != QA_TRACE_HIT_ACTOR)
            break;
        if (count == shot->excluded_capacity) {
            qa_error_set(error, QA_ERROR_ARGUMENT, count,
                         "piercing hitscan exhausted caller exclusion storage");
            return false;
        }
        shot->excluded[count++] = trace.actor;
    }
    if (last)
        *last = trace;
    return true;
}

static bool visible_point(const qa_builtin_services *s, qa_vec3 start, qa_vec3 end,
                          qa_actor_id target, qa_actor_id pass, qa_trace_policy policy, bool *out,
                          qa_error *error) {
    qa_trace_query query = {.start = start,
                            .end = end,
                            .shape.kind = QA_SHAPE_POINT,
                            .policy = policy,
                            .pass_actor = pass};
    qa_trace_result trace;
    if (!qa_world_trace(s->world, &query, &trace, error))
        return false;
    *out = trace.fraction == 1.0f ||
           (trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, target));
    return true;
}

bool qa_builtin_can_damage(const qa_builtin_services *s, qa_vec3 origin, qa_actor_id target,
                           qa_actor_id pass, qa_trace_policy policy, bool corners, bool *out,
                           qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(s->world, target, &body, error))
        return false;
    qa_actor_collision collision = {0};
    qa_error local = {0};
    bool brush = qa_world_get_collision(s->world, target, &collision, &local) && collision.inline_model;
    if (local.code != QA_OK) { if (error) *error = local; return false; }
    qa_vec3 center =
        qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
    qa_vec3 destination = policy.family == QA_COLLISION_Q3 || brush ? center : body.origin;
    bool visible;
    if (!visible_point(s, origin, destination, target, pass, policy, &visible, error))
        return false;
    if (visible || (brush && policy.family != QA_COLLISION_Q3)) {
        *out = visible;
        return true;
    }
    for (unsigned i = 0; i < (corners ? 8u : 4u); ++i) {
        qa_vec3 offset = qa_v3((i & 1u) ? 15.0f : -15.0f, (i & 2u) ? 15.0f : -15.0f,
                               corners ? ((i & 4u) ? 15.0f : -15.0f) : 0.0f);
        qa_actor_id accepted_target = policy.family == QA_COLLISION_Q3 ? (qa_actor_id){0} : target;
        if (!visible_point(s, origin, qa_vec_add(destination, offset), accepted_target, pass,
                           policy, &visible, error))
            return false;
        if (visible) {
            *out = true;
            return true;
        }
    }
    *out = false;
    return true;
}

static float distance_to_bounds(qa_vec3 point, qa_bounds bounds) {
    qa_vec3 delta = qa_v3(fmaxf(bounds.mins.x - point.x, fmaxf(0, point.x - bounds.maxs.x)),
                          fmaxf(bounds.mins.y - point.y, fmaxf(0, point.y - bounds.maxs.y)),
                          fmaxf(bounds.mins.z - point.z, fmaxf(0, point.z - bounds.maxs.z)));
    return qa_vec_length(delta);
}

bool qa_builtin_radius_damage(const qa_builtin_services *s, const qa_builtin_radius *radius,
                              size_t *damaged, qa_error *error) {
    if (!s || !radius || !qa_vec_finite(radius->origin) || !isfinite(radius->radius) ||
        radius->radius < 0 || !isfinite(radius->damage) || !isfinite(radius->distance_scale) ||
        !isfinite(radius->self_scale) || !isfinite(radius->knockback_scale) ||
        !isfinite(radius->direction_z_bias) ||
        (radius->has_candidates && radius->candidate_count && !radius->candidates) ||
        (radius->candidate_radius_only && !radius->has_candidates)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native radius attack");
        return false;
    }
    size_t total = 0;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    size_t candidate = 0;
    for (;;) {
        qa_actor_id actor;
        if (radius->has_candidates) {
            if (candidate == radius->candidate_count)
                break;
            actor = radius->candidates[candidate++];
            if (!qa_actors_get(qa_session_actors(s->session), actor))
                continue;
        } else {
            if (!qa_actors_next(qa_session_actors(s->session), &cursor, &record))
                break;
            actor = record->id;
        }
        if (qa_actor_id_equal(actor, radius->ignore))
            continue;
        bool can_damage;
        if (!damageable(s, actor, &can_damage, error))
            return false;
        if (!can_damage)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(s->world, actor, &body, error))
            return false;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        float distance =
            radius->distance == QA_RADIUS_BOUNDS
                ? distance_to_bounds(radius->origin, qa_bounds_translate(body.bounds, body.origin))
                : qa_vec_length(qa_vec_sub(center, radius->origin));
        if (!radius->candidate_radius_only && distance > radius->radius)
            continue;
        float amount = radius->damage - radius->distance_scale * distance;
        if (qa_actor_id_equal(actor, radius->attack.attacker))
            amount *= radius->self_scale;
        float knockback = amount * radius->knockback_scale;
        bool allowed = amount > 0;
        if (radius->adjust &&
            !radius->adjust(radius->context, actor, &amount, &knockback, &allowed, error))
            return false;
        if (!allowed || !qa_actors_get(qa_session_actors(s->session), actor))
            continue;
        if (radius->check_visibility) {
            bool visible;
            if (!qa_builtin_can_damage(s, radius->origin, actor, radius->visibility_pass,
                                       radius->trace, radius->corner_visibility, &visible, error))
                return false;
            if (!visible)
                continue;
        }
        qa_damage_request request = {.attack = radius->attack,
                                     .target = actor,
                                     .amount = amount,
                                     .knockback = knockback,
                                     .direction = qa_vec_sub(body.origin, radius->origin),
                                     .point = radius->origin,
                                     .radius = true};
        request.direction.z += radius->direction_z_bias;
        if (radius->prepare && !radius->prepare(radius->context, &request, &allowed, error))
            return false;
        if (!allowed)
            continue;
        qa_damage_outcome outcome = {0};
        bool ok = qa_combat_apply(s->combat, &request, &outcome, error);
        if (ok && outcome.result.applied_damage > 0)
            ++total;
        if (ok && radius->after)
            ok = radius->after(radius->context, &outcome, error);
        qa_damage_outcome_free(&outcome);
        if (!ok)
            return false;
    }
    if (damaged)
        *damaged = total;
    return true;
}

bool qa_builtin_projectile_move(const qa_builtin_services *s,
                                const qa_builtin_projectile_step *step, qa_trace_result *out,
                                qa_error *error) {
    qa_body_state body;
    if (!s || !step || !qa_vec_finite(step->end)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid projectile movement");
        return false;
    }
    if (!qa_world_body_read(s->world, step->actor, &body, error))
        return false;
    qa_trace_query query = {
        .start = body.origin,
        .end = step->end,
        .shape = {.kind = step->point ? QA_SHAPE_POINT : QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = step->trace,
        .pass_actor = step->actor};
    qa_trace_result trace;
    size_t excluded_count = step->owner.registry ? 1u : 0u;
    if (!qa_world_trace_excluding(s->world, &query, &step->owner, excluded_count, &trace, error))
        return false;
    body.origin = trace.end;
    if (!qa_world_body_write(s->world, step->actor, &body, error) ||
        !qa_world_link(s->world, step->actor, NULL, error))
        return false;
    if (out)
        *out = trace;
    if ((trace.fraction < 1 || trace.start_solid) && step->impact)
        return step->impact(step->context, step->actor, &trace, error);
    return true;
}
