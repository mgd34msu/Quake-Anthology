#include "internal.h"

enum { Q3_MOVER_LIMIT = QA_PHYSICS_SOURCE_PUSH_LIMIT };

typedef struct q3_mover_record {
    qa_body_state body;
    qa_physics_properties properties;
    qa_q3_mover_state source;
} q3_mover_record;
typedef struct q3_mover_transaction {
    qa_physics *physics;
    const qa_q3_mover_services *services;
    int32_t now, previous;
    ph_pushed *pushed;
    struct qa_physics_push_frame *frame;
    size_t count, capacity;
    qa_physics_result *result;
    qa_error *error;
} q3_mover_transaction;

static int32_t q3_signed_word(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static int32_t q3_integer(float value) {
    return isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f
        ? (int32_t)truncf(value) : INT32_MIN;
}
static uint32_t q3_angle_word(float angle) {
    return (uint32_t)q3_integer((angle * 65536) / 360) & UINT32_C(65535);
}
static bool q3_native(qa_q3_mover_actor_kind kind) {
    return kind >= QA_Q3_MOVER_NATIVE_FIXED && kind <= QA_Q3_MOVER_PROXIMITY_MINE;
}
static int q3_read(q3_mover_transaction *transaction, qa_actor_id actor, q3_mover_record *record) {
    int read = ph_read(transaction->physics, actor, &record->body, &record->properties, transaction->error);
    if (read <= 0) return read;
    if (!transaction->services->read(transaction->services->context, actor, &record->source) ||
        !ph_live(transaction->physics, actor)) return 0;
    if (record->source.kind < QA_Q3_MOVER_IGNORE || record->source.kind > QA_Q3_MOVER_PROXIMITY_MINE) {
        qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 mover actor classification");
        return -1;
    }
    record->source.write_client_motion = false;
    return 1;
}
static bool q3_source_write(q3_mover_transaction *transaction, qa_actor_id actor, const qa_q3_mover_state *source) {
    return !ph_live(transaction->physics, actor) ||
        transaction->services->write(transaction->services->context, actor, source, transaction->error);
}
static bool q3_body_position(q3_mover_transaction *transaction, qa_actor_id actor, qa_vec3 origin, bool clear_ground) {
    q3_mover_record current;
    int read = q3_read(transaction, actor, &current);
    if (read <= 0) return read == 0;
    current.body.origin = origin;
    if (clear_ground) current.body.ground = (qa_actor_reference){0};
    return ph_write(transaction->physics, actor, &current.body, transaction->error);
}
static bool q3_lose_ground(q3_mover_transaction *transaction, qa_actor_id actor) {
    q3_mover_record current;
    int read = q3_read(transaction, actor, &current);
    if (read <= 0) return read == 0;
    if (q3_native(current.source.kind)) {
        current.source.ground_entity_number = -1;
        if (!q3_source_write(transaction, actor, &current.source)) return false;
    }
    read = q3_read(transaction, actor, &current);
    if (read <= 0) return read == 0;
    current.body.ground = (qa_actor_reference){0};
    return ph_write(transaction->physics, actor, &current.body, transaction->error);
}
static bool q3_action(q3_mover_transaction *transaction, qa_q3_mover_action action,
                       qa_actor_id actor, qa_actor_id other) {
    if (!ph_live(transaction->physics, actor)) return true;
    return transaction->services->action(transaction->services->context, action, actor,
                                         other, transaction->now, transaction->error);
}
static bool q3_trace_query(q3_mover_transaction *transaction, const qa_trace_query *query, qa_trace_result *trace) {
    bool success = transaction->services->trace
        ? transaction->services->trace(transaction->services->context, query, trace, transaction->error)
        : qa_world_trace(transaction->physics->world, query, trace, transaction->error);
    if (!success) return false;
    if (trace->family != QA_GAME_Q3 || !isfinite(trace->fraction) ||
        trace->fraction < 0 || trace->fraction > 1 || !qa_vec_finite(trace->end)) {
        qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 mover trace result");
        return false;
    }
    return true;
}
static qa_vec3 q3_test_origin(const q3_mover_record *record) {
    if (!q3_native(record->source.kind)) return record->body.origin;
    return record->source.has_client ? record->source.client_origin : record->source.position.base;
}
static bool q3_position_blocked(q3_mover_transaction *transaction, qa_actor_id actor, bool *blocked) {
    *blocked = false;
    q3_mover_record record;
    int read = q3_read(transaction, actor, &record);
    if (read <= 0) return read == 0;
    qa_vec3 start = q3_test_origin(&record);
    qa_trace_query query = {.start = start, .end = start,
        .shape = {QA_SHAPE_BOX, record.body.bounds}, .pass_actor = actor,
        .policy = qa_collision_default_policy(QA_GAME_Q3)};
    query.policy.contents_mask = qa_collision_contents_mask(
        record.properties.clip_mask ? record.properties.clip_mask : 1,QA_GAME_Q3);
    qa_trace_result trace;
    if (!q3_trace_query(transaction, &query, &trace)) return false;
    *blocked = ph_live(transaction->physics, actor) && (trace.start_solid || trace.all_solid);
    return true;
}
static qa_vec3 q3_rotation(qa_vec3 origin, qa_vec3 pusher_origin, qa_vec3 angular) {
    qa_vec3 forward, right, up;
    ph_axes(angular, &forward, &right, &up);
    right = qa_vec_scale(right, -1);
    qa_vec3 relative = qa_vec_sub(origin, pusher_origin);
    qa_vec3 rotated = qa_v3(
        qa_vec_dot(relative, qa_v3(forward.x, right.x, up.x)),
        qa_vec_dot(relative, qa_v3(forward.y, right.y, up.y)),
        qa_vec_dot(relative, qa_v3(forward.z, right.z, up.z)));
    return qa_vec_sub(rotated, relative);
}
static bool q3_save(q3_mover_transaction *transaction, qa_actor_id actor,
                     const q3_mover_record *record, ph_pushed *saved) {
    if (transaction->count == Q3_MOVER_LIMIT) {
        if (transaction->physics->push_stats.rollback_overflows != SIZE_MAX)
            ++transaction->physics->push_stats.rollback_overflows;
        qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Q3 pushed stack exceeds MAX_GENTITIES");
        return false;
    }
    if (transaction->count == transaction->capacity)
        return ph_push_overflow(&transaction->physics->push_stats.rollback_overflows, transaction->error,
            "Reserved pusher rollback capacity exhausted");
    *saved = (ph_pushed){.actor = actor, .kind = record->source.kind,
        .origin = q3_test_origin(record), .body_origin = record->body.origin,
        .angles = record->source.angular.base, .delta_yaw = (float)record->source.delta_yaw_word,
        .has_client = record->source.has_client};
    transaction->pushed[transaction->count++] = *saved;
    if (transaction->count > transaction->physics->push_stats.peak_rollback)
        transaction->physics->push_stats.peak_rollback = transaction->count;
    return true;
}

static bool q3_restore(q3_mover_transaction *transaction) {
    for (size_t i = transaction->count; i > 0; --i) {
        const ph_pushed *saved = &transaction->pushed[i - 1];
        q3_mover_record current;
        int read = q3_read(transaction, saved->actor, &current);
        if (read < 0) return false;
        if (!read) continue;
        if (q3_native(saved->kind)) {
            if (!q3_native(current.source.kind) ||
                (current.source.has_client && !saved->has_client)) {
                qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Q3 pushed actor changed its native body owner");
                return false;
            }
            current.source.position.base = saved->origin;
            current.source.angular.base = saved->angles;
            if (current.source.has_client) {
                current.source.client_origin = saved->origin;
                current.source.delta_yaw_word = q3_integer(saved->delta_yaw);
                current.source.write_client_motion = true;
            }
            if (!q3_source_write(transaction, saved->actor, &current.source)) return false;
        }
        if (!q3_native(saved->kind) &&
            !q3_body_position(transaction, saved->actor, saved->body_origin, false)) return false;
        /* Native rollback restores S and the actual PS, then links the current
         * physical body. A borrowed model's r.currentOrigin is independent. */
        if (!ph_link(transaction->physics, saved->actor, false, transaction->error)) return false;
    }
    return true;
}

static bool q3_try_push(q3_mover_transaction *transaction, qa_actor_id actor,
                         qa_actor_id pusher, qa_vec3 move, qa_vec3 angular,
                         bool *pushed) {
    *pushed = true;
    q3_mover_record check, support;
    int read = q3_read(transaction, actor, &check);
    if (read <= 0) return read == 0;
    read = q3_read(transaction, pusher, &support);
    if (read <= 0) return read == 0;
    bool rider = qa_actor_id_equal(qa_physics_actor_reference(transaction->physics, check.body.ground), pusher);
    if (support.source.stop && !rider) {
        *pushed = false;
        return true;
    }
    ph_pushed saved;
    if (!q3_save(transaction, actor, &check, &saved)) return false;
    qa_vec3 rotation = q3_rotation(saved.origin, support.body.origin, angular);
    qa_vec3 destination = qa_vec_add(qa_vec_add(saved.origin, move), rotation);
    bool native = q3_native(check.source.kind);
    if (native) {
        check.source.position.base = qa_vec_add(qa_vec_add(check.source.position.base, move), rotation);
        if (check.source.has_client) {
            check.source.client_origin = qa_vec_add(qa_vec_add(check.source.client_origin, move), rotation);
            check.source.delta_yaw_word = q3_signed_word((uint32_t)check.source.delta_yaw_word + q3_angle_word(angular.y));
            check.source.write_client_motion = true;
        }
        if (!q3_source_write(transaction, actor, &check.source)) return false;
    }
    if (!native) {
        if (!q3_body_position(transaction, actor, destination, !rider)) return false;
    }
    if (!rider && !q3_lose_ground(transaction, actor)) return false;
    bool blocked;
    if (!q3_position_blocked(transaction, actor, &blocked)) return false;
    if (!ph_live(transaction->physics, actor) || !ph_live(transaction->physics, pusher)) return true;
    if (!blocked) {
        read = q3_read(transaction, actor, &check);
        if (read <= 0) return read == 0;
        if (native && !q3_body_position(transaction, actor, q3_test_origin(&check), false)) return false;
        return ph_link(transaction->physics, actor, false, transaction->error);
    }
    read = q3_read(transaction, actor, &check);
    if (read <= 0) return read == 0;
    if (native) {
        check.source.position.base = saved.origin;
        check.source.angular.base = saved.angles;
        if (check.source.has_client) {
            check.source.client_origin = saved.origin;
            check.source.write_client_motion = true;
        }
        if (!q3_source_write(transaction, actor, &check.source)) return false;
    }
    /* Source fallback keeps the already adjusted player yaw. It restores the
     * original position, never merely subtracts translation from a rotation. */
    if (!native &&
        !q3_body_position(transaction, actor, saved.origin, !rider)) return false;
    if (!q3_position_blocked(transaction, actor, &blocked)) return false;
    if (!ph_live(transaction->physics, actor) || !ph_live(transaction->physics, pusher)) return true;
    if (!blocked) {
        if (!q3_lose_ground(transaction, actor)) return false;
        --transaction->count;
        return true;
    }
    *pushed = false;
    return true;
}

static bool q3_proximity_clear(q3_mover_transaction *transaction, qa_actor_id actor, bool *clear) {
    *clear = true;
    q3_mover_record mine;
    int read = q3_read(transaction, actor, &mine);
    if (read <= 0) return read == 0;
    qa_trace_query query = {
        .start = qa_vec_add(mine.source.position.base, qa_vec_scale(mine.source.proximity_direction, 0.125f)),
        .end = qa_vec_add(mine.source.position.base, qa_vec_scale(mine.source.proximity_direction, 2)),
        .shape = {.kind = QA_SHAPE_POINT}, .pass_actor = actor,
        .policy = qa_collision_default_policy(QA_GAME_Q3)};
    query.policy.contents_mask = qa_collision_contents_mask(1,QA_GAME_Q3);
    qa_trace_result trace;
    if (!q3_trace_query(transaction, &query, &trace)) return false;
    *clear = !ph_live(transaction->physics, actor) ||
        (!trace.start_solid && !trace.all_solid && trace.fraction == 1);
    return true;
}
static bool q3_proximity_push(q3_mover_transaction *transaction, qa_actor_id actor,
                               qa_actor_id pusher, qa_vec3 move, qa_vec3 angular) {
    q3_mover_record mine, support;
    int read = q3_read(transaction, actor, &mine);
    if (read <= 0) return read == 0;
    bool attached = qa_actor_id_equal(mine.source.proximity_pusher, pusher);
    if (attached) {
        read = q3_read(transaction, pusher, &support);
        if (read <= 0) return read == 0;
        qa_vec3 forward, right, up;
        ph_axes(qa_vec_scale(angular, -1), &forward, &right, &up);
        qa_vec3 translated = qa_vec_add(mine.source.position.base, move);
        qa_vec3 relative = qa_vec_sub(translated, support.body.origin);
        qa_vec3 rotated = qa_v3(qa_vec_dot(relative, forward), -qa_vec_dot(relative, right), qa_vec_dot(relative, up));
        mine.source.position.base = qa_vec_add(translated, qa_vec_sub(rotated, relative));
        if (!q3_source_write(transaction, actor, &mine.source)) return false;
    }
    bool clear;
    if (!q3_proximity_clear(transaction, actor, &clear)) return false;
    if (!ph_live(transaction->physics, actor) || !ph_live(transaction->physics, pusher)) return true;
    if (!clear) return q3_action(transaction, QA_Q3_MOVER_PROXIMITY_TRIGGER, actor, pusher);
    if (!attached) return true;
    read = q3_read(transaction, actor, &mine);
    if (read <= 0) return read == 0;
    return q3_body_position(transaction, actor, mine.source.position.base, false) &&
        ph_link(transaction->physics, actor, false, transaction->error);
}

static bool q3_overlap(qa_bounds first, qa_bounds second) {
    return first.mins.x < second.maxs.x && first.mins.y < second.maxs.y && first.mins.z < second.maxs.z &&
           first.maxs.x > second.mins.x && first.maxs.y > second.mins.y && first.maxs.z > second.mins.z;
}
static qa_bounds q3_absolute(q3_mover_transaction *transaction, qa_actor_id actor, const qa_body_state *body) {
    qa_linked_body linked;
    return qa_world_linked(transaction->physics->world, actor, &linked)
        ? linked.absolute_bounds : qa_bounds_translate(body->bounds, body->origin);
}
static void q3_push_bounds(q3_mover_transaction *transaction, qa_actor_id actor,
                            const qa_body_state *body, qa_vec3 move, qa_vec3 angular,
                            qa_bounds *destination, qa_bounds *total) {
    if (ph_moving(body->angles) || ph_moving(angular)) {
        qa_vec3 corner = qa_v3(fmaxf(fabsf(body->bounds.mins.x), fabsf(body->bounds.maxs.x)),
                                fmaxf(fabsf(body->bounds.mins.y), fabsf(body->bounds.maxs.y)),
                                fmaxf(fabsf(body->bounds.mins.z), fabsf(body->bounds.maxs.z)));
        float radius = qa_vec_length(corner);
        qa_vec3 extent = qa_v3(radius, radius, radius), position = qa_vec_add(body->origin, move);
        *destination = (qa_bounds){qa_vec_sub(position, extent), qa_vec_add(position, extent)};
        *total = (qa_bounds){qa_vec_sub(destination->mins, move), qa_vec_sub(destination->maxs, move)};
    } else {
        qa_bounds bounds = q3_absolute(transaction, actor, body);
        *destination = qa_bounds_translate(bounds, move);
        *total = (qa_bounds){qa_vec_add(bounds.mins, qa_v3(fminf(move.x, 0), fminf(move.y, 0), fminf(move.z, 0))),
                            qa_vec_add(bounds.maxs, qa_v3(fmaxf(move.x, 0), fmaxf(move.y, 0), fmaxf(move.z, 0)))};
    }
}
static bool q3_query(q3_mover_transaction *transaction, qa_bounds bounds, qa_actor_id *actors, size_t *count) {
    *count = 0;
    if (transaction->services->query) {
        if (!transaction->services->query(transaction->services->context, bounds, actors,
                                           Q3_MOVER_LIMIT, count, transaction->error)) return false;
        if (*count > Q3_MOVER_LIMIT) {
            if (transaction->physics->push_stats.candidate_overflows != SIZE_MAX)
                ++transaction->physics->push_stats.candidate_overflows;
            qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Q3 mover query exceeded its result capacity");
            return false;
        }
        if (*count > transaction->physics->push_stats.peak_candidates)
            transaction->physics->push_stats.peak_candidates = *count;
        return true;
    }
    bool overflow;
    bool ok = qa_world_query(transaction->physics->world, bounds, QA_COLLISION_BOTH,
                          actors, Q3_MOVER_LIMIT, count, &overflow, transaction->error);
    if (ok && *count > transaction->physics->push_stats.peak_candidates)
        transaction->physics->push_stats.peak_candidates = *count;
    return ok;
}

static bool q3_push_part(q3_mover_transaction *transaction, qa_actor_id pusher,
                          qa_vec3 move, qa_vec3 angular, qa_actor_id *obstacle) {
    *obstacle = ph_none();
    q3_mover_record support;
    int read = q3_read(transaction, pusher, &support);
    if (read <= 0) return read == 0;
    qa_bounds destination, total;
    q3_push_bounds(transaction, pusher, &support.body, move, angular, &destination, &total);
    qa_actor_id *actors = transaction->frame->candidates;
    qa_body_link_state old_link;
    bool linked = qa_world_link_state(transaction->physics->world, pusher, &old_link);
    bool ok = qa_world_unlink(transaction->physics->world, pusher, transaction->error);
    size_t count = 0;
    if (ok) ok = q3_query(transaction, total, actors, &count);
    if (!ok) {
        if (linked && ph_live(transaction->physics, pusher)) {
            qa_error ignored;
            (void)qa_world_restore_link_state(transaction->physics->world, pusher, &old_link, &ignored);
        }
        return false;
    }
    read = q3_read(transaction, pusher, &support);
    if (read <= 0) return read == 0;
    support.body.origin = qa_vec_add(support.body.origin, move);
    support.body.angles = qa_vec_add(support.body.angles, angular);
    ok = ph_write(transaction->physics, pusher, &support.body, transaction->error) &&
         ph_link(transaction->physics, pusher, false, transaction->error);
    for (size_t i = 0; i < count && ok && ph_live(transaction->physics, pusher); ++i) {
        qa_actor_id actor = actors[i];
        qa_body_attachment attachment;
        qa_linked_body linked_actor;
        if (qa_actor_id_equal(actor, pusher) ||
            !qa_world_linked(transaction->physics->world, actor, &linked_actor) ||
            qa_world_attachment(transaction->physics->world, actor, &attachment)) continue;
        q3_mover_record check;
        read = q3_read(transaction, actor, &check);
        if (read < 0) { ok = false; break; }
        if (!read || check.source.kind == QA_Q3_MOVER_IGNORE || check.source.kind == QA_Q3_MOVER_NATIVE_FIXED) continue;
        if (check.source.kind == QA_Q3_MOVER_PROXIMITY_MINE) {
            ok = q3_proximity_push(transaction, actor, pusher, move, angular);
            continue;
        }
        if (!qa_actor_id_equal(qa_physics_actor_reference(transaction->physics, check.body.ground), pusher)) {
            if (!q3_overlap(q3_absolute(transaction, actor, &check.body), destination)) continue;
            bool blocked;
            if (!q3_position_blocked(transaction, actor, &blocked)) { ok = false; break; }
            if (!blocked) continue;
        }
        bool pushed;
        if (!q3_try_push(transaction, actor, pusher, move, angular, &pushed)) { ok = false; break; }
        if (pushed || !ph_live(transaction->physics, actor) || !ph_live(transaction->physics, pusher)) continue;
        ++transaction->result->collisions;
        read = q3_read(transaction, pusher, &support);
        if (read < 0) { ok = false; break; }
        if (!read) break;
        if (support.source.position.type == QA_TRAJECTORY_SINE || support.source.angular.type == QA_TRAJECTORY_SINE) {
            ok = q3_action(transaction, QA_Q3_MOVER_CRUSH, pusher, actor);
            continue;
        }
        if (!q3_restore(transaction)) { ok = false; break; }
        *obstacle = actor;
        break;
    }
    return ok;
}

static bool q3_team_limit(q3_mover_transaction *transaction, size_t count) {
    if (count < Q3_MOVER_LIMIT) return true;
    qa_error_set(transaction->error, QA_ERROR_ARGUMENT, count, "Q3 mover team chain exceeds MAX_GENTITIES");
    return false;
}
static bool q3_team_part(q3_mover_transaction *transaction, const q3_mover_record *record) {
    if (q3_native(record->source.kind)) return true;
    qa_error_set(transaction->error, QA_ERROR_ARGUMENT, 0, "Q3 mover team part has no native source trajectories");
    return false;
}
static bool q3_next(q3_mover_transaction *transaction, qa_actor_id part, qa_actor_id prior_next, qa_actor_id *next) {
    q3_mover_record live;
    int read = q3_read(transaction, part, &live);
    if (read < 0) return false;
    *next = read ? live.source.team_next : prior_next;
    return true;
}
static bool q3_rewind_team(q3_mover_transaction *transaction, qa_actor_id leader) {
    int32_t elapsed = q3_signed_word((uint32_t)transaction->now - (uint32_t)transaction->previous);
    qa_actor_id part = leader;
    for (size_t count = 0; part.registry; ++count) {
        if (!q3_team_limit(transaction, count)) return false;
        q3_mover_record record;
        int read = q3_read(transaction, part, &record);
        if (read <= 0) return read == 0;
        if (!q3_team_part(transaction, &record)) return false;
        qa_actor_id next = record.source.team_next;
        record.source.position.time_ms = q3_signed_word((uint32_t)record.source.position.time_ms + (uint32_t)elapsed);
        record.source.angular.time_ms = q3_signed_word((uint32_t)record.source.angular.time_ms + (uint32_t)elapsed);
        if (!q3_source_write(transaction, part, &record.source)) return false;
        read = q3_read(transaction, part, &record);
        if (read < 0) return false;
        if (read) {
            if (!qa_trajectory_position(&record.source.position, transaction->now, 800, &record.body.origin, transaction->error) ||
                !qa_trajectory_position(&record.source.angular, transaction->now, 800, &record.body.angles, transaction->error) ||
                !ph_write(transaction->physics, part, &record.body, transaction->error) ||
                !ph_link(transaction->physics, part, false, transaction->error)) return false;
        }
        if (!ph_live(transaction->physics, leader)) return true;
        if (!q3_next(transaction, part, next, &part)) return false;
    }
    return true;
}
static bool q3_run_team(q3_mover_transaction *transaction, qa_actor_id leader) {
    qa_actor_id part = leader, obstacle = ph_none();
    for (size_t count = 0; part.registry; ++count) {
        if (!q3_team_limit(transaction, count)) return false;
        q3_mover_record record;
        int read = q3_read(transaction, part, &record);
        if (read <= 0) return read == 0;
        if (!q3_team_part(transaction, &record)) return false;
        qa_actor_id next = record.source.team_next;
        qa_vec3 destination, angles;
        if (!qa_trajectory_position(&record.source.position, transaction->now, 800, &destination, transaction->error) ||
            !qa_trajectory_position(&record.source.angular, transaction->now, 800, &angles, transaction->error)) return false;
        if (!q3_push_part(transaction, part, qa_vec_sub(destination, record.body.origin),
                           qa_vec_sub(angles, record.body.angles), &obstacle)) return false;
        if (obstacle.registry) break;
        if (!ph_live(transaction->physics, leader)) return true;
        if (!q3_next(transaction, part, next, &part)) return false;
    }
    if (obstacle.registry) {
        transaction->result->status = QA_PHYSICS_BLOCKED;
        transaction->result->obstacle = obstacle;
        if (!q3_rewind_team(transaction, leader)) return false;
        return q3_action(transaction, QA_Q3_MOVER_BLOCKED, leader, obstacle);
    }
    part = leader;
    for (size_t count = 0; part.registry; ++count) {
        if (!q3_team_limit(transaction, count)) return false;
        q3_mover_record record;
        int read = q3_read(transaction, part, &record);
        if (read <= 0) return read == 0;
        if (!q3_team_part(transaction, &record)) return false;
        qa_actor_id next = record.source.team_next;
        int32_t end = q3_signed_word((uint32_t)record.source.position.time_ms + (uint32_t)record.source.position.duration_ms);
        if (record.source.position.type == QA_TRAJECTORY_LINEAR_STOP && transaction->now >= end &&
            !q3_action(transaction, QA_Q3_MOVER_REACHED, part, ph_none())) return false;
        if (!ph_live(transaction->physics, leader)) return true;
        if (!q3_next(transaction, part, next, &part)) return false;
    }
    return true;
}

static bool q3_mover_call(qa_physics *physics, const qa_q3_mover_services *services,
                          qa_actor_id leader, int32_t previous, int32_t now,
                          bool run_think, qa_physics_result *result, qa_error *error) {
    if (!physics || !physics->world || !services || !services->read || !services->write || !services->action || !result) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native Q3 mover services");
        return false;
    }
    *result = (qa_physics_result){.status = QA_PHYSICS_MOVED};
    q3_mover_transaction transaction = {.physics = physics, .services = services,
        .now = now, .previous = previous, .result = result, .error = error};
    q3_mover_record record;
    int read = q3_read(&transaction, leader, &record);
    if (read <= 0) {
        result->status = ph_live(physics, leader) ? QA_PHYSICS_UNMANAGED : QA_PHYSICS_REMOVED;
        return read == 0;
    }
    if (!q3_native(record.source.kind)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 mover leader has no native source trajectories");
        return false;
    }
    if (run_think && record.source.team_slave) {
        result->status = QA_PHYSICS_SLAVE;
        return true;
    }
    transaction.frame = ph_push_frame(physics, error);
    if (!transaction.frame) return false;
    if (transaction.frame->candidate_capacity < Q3_MOVER_LIMIT) {
        ph_push_frame_release(physics, transaction.frame);
        return ph_push_overflow(&physics->push_stats.candidate_overflows, error,
            "Reserved Q3 mover candidate capacity exhausted");
    }
    transaction.pushed = transaction.frame->pushed.entries;
    transaction.capacity = transaction.frame->pushed.capacity;
    bool move = !run_think || record.source.position.type != QA_TRAJECTORY_STATIONARY ||
                                record.source.angular.type != QA_TRAJECTORY_STATIONARY;
    bool ok = !move || q3_run_team(&transaction, leader);
    if (ok && run_think) ok = q3_action(&transaction, QA_Q3_MOVER_THINK, leader, ph_none());
    if (!ph_live(physics, leader)) result->status = QA_PHYSICS_REMOVED;
    ph_push_frame_release(physics, transaction.frame);
    return ok;
}

bool qa_physics_q3_run_team(qa_physics *physics, const qa_q3_mover_services *services,
                            qa_actor_id leader, int32_t previous, int32_t now,
                            qa_physics_result *result, qa_error *error) {
    return q3_mover_call(physics, services, leader, previous, now, false, result, error);
}
bool qa_physics_q3_run_mover(qa_physics *physics, const qa_q3_mover_services *services,
                             qa_actor_id leader, int32_t previous, int32_t now,
                             qa_physics_result *result, qa_error *error) {
    return q3_mover_call(physics, services, leader, previous, now, true, result, error);
}
