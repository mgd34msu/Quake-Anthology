#include "internal.h"
#include <float.h>
#include <stdalign.h>

static bool q1_store(double value, float *out, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 pusher value exceeds finite binary32");
        return false;
    }
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}

bool ph_push_overflow(size_t *counter, qa_error *error, const char *message) {
    if (*counter != SIZE_MAX) ++*counter;
    qa_error_set(error, QA_ERROR_MEMORY, 0, "%s", message);
    return false;
}

struct qa_physics_push_frame *ph_push_frame(qa_physics *p, qa_error *error) {
    size_t slot;
    struct qa_physics_push_frame *frame = qa_pool_take(&p->push_frames, &slot);
    if (!frame) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Reserved pusher nesting capacity exhausted");
        return NULL;
    }
    frame->pushed.count = 0;
    return frame;
}

void ph_push_frame_release(qa_physics *p, struct qa_physics_push_frame *frame) {
    qa_pool_release(&p->push_frames, frame->slot);
}

bool qa_physics_dispose(qa_physics *p, qa_error *error) {
    if (!p) return true;
    if (p->push_transaction) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Physics disposal requires an idle pusher transaction"); return false;
    }
    if (p->push_frames.active) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Physics disposal requires idle pusher frames"); return false;
    }
    qa_arena_destroy(&p->push_storage);
    p->push_frames = (qa_pool){0};
    p->push_stats = (qa_physics_push_stats){0};
    return true;
}

bool qa_physics_prepare_push_frames(qa_physics *p, size_t count, size_t candidates,
                                    size_t rollback, qa_error *error) {
    size_t padding = alignof(struct qa_physics_push_frame)-1 +
        alignof(size_t)-1 + alignof(qa_actor_id)-1 + alignof(ph_pushed)-1;
    if (!p || !count || !candidates || !rollback ||
        candidates > SIZE_MAX/(2*sizeof(qa_actor_id)) || rollback > SIZE_MAX/sizeof(ph_pushed) ||
        count > SIZE_MAX/sizeof(struct qa_physics_push_frame)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Pusher reservation exceeds addressable storage"); return false;
    }
    size_t candidate_bytes = 2*candidates*sizeof(qa_actor_id);
    size_t rollback_bytes = rollback*sizeof(ph_pushed);
    size_t frame_bytes = sizeof(struct qa_physics_push_frame)+sizeof(size_t);
    if (candidate_bytes > SIZE_MAX-rollback_bytes ||
        candidate_bytes+rollback_bytes > SIZE_MAX-frame_bytes ||
        count > (SIZE_MAX-padding)/(candidate_bytes+rollback_bytes+frame_bytes)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Pusher reservation exceeds addressable storage"); return false;
    }
    size_t bytes = count*(candidate_bytes+rollback_bytes+frame_bytes)+padding;
    qa_arena storage = {0};
    if (!qa_arena_reserve(&storage, bytes, error)) return false;
    qa_pool frames = {0};
    if (!qa_pool_prepare(&frames, &storage, count, sizeof(struct qa_physics_push_frame),
                        alignof(struct qa_physics_push_frame), error)) {
        qa_arena_destroy(&storage);
        return false;
    }
    qa_actor_id *ids = qa_arena_alloc(&storage, count*candidate_bytes, alignof(qa_actor_id), error);
    ph_pushed *entries = qa_arena_alloc(&storage, count*rollback_bytes, alignof(ph_pushed), error);
    if (!ids || !entries || !qa_physics_dispose(p, error)) {
        qa_arena_destroy(&storage);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        struct qa_physics_push_frame *frame = qa_pool_at(&frames, i);
        *frame = (struct qa_physics_push_frame){.slot=i,
            .candidates=ids+2*i*candidates, .candidate_capacity=candidates,
            .pushed={.entries=entries+i*rollback, .capacity=rollback, .owner=p}};
    }
    qa_arena_seal(&storage);
    p->push_storage = storage;
    p->push_frames = frames;
    p->push_stats = (qa_physics_push_stats){.candidate_capacity=candidates, .rollback_capacity=rollback};
    return true;
}

static int candidate_order(qa_physics *p, qa_actor_id a, qa_actor_id b) {
    if (p->services.source_order) return p->services.source_order(p->services.context, a, b);
    const qa_actor_record *ar = qa_actors_get(qa_world_actors(p->world), a);
    const qa_actor_record *br = qa_actors_get(qa_world_actors(p->world), b);
    uint32_t aslot = ar && ar->has_source ? ar->source_slot : a.slot;
    uint32_t bslot = br && br->has_source ? br->source_slot : b.slot;
    if (aslot != bslot) return aslot < bslot ? -1 : 1;
    if (ar && br && ar->owner != br->owner) return ar->owner < br->owner ? -1 : 1;
    return a.slot < b.slot ? -1 : a.slot > b.slot;
}

static bool candidates(qa_physics *p, struct qa_physics_push_frame *frame,
                         qa_actor_id **out, size_t *count, qa_error *error) {
    size_t capacity = qa_actors_count(qa_world_actors(p->world));
    *count = 0;
    *out = NULL;
    if (!capacity) return true;
    if (capacity > frame->candidate_capacity)
        return ph_push_overflow(&p->push_stats.candidate_overflows, error,
            "Reserved pusher candidate capacity exhausted");
    if (capacity > p->push_stats.peak_candidates) p->push_stats.peak_candidates = capacity;
    qa_actor_id *list = frame->candidates;
    const qa_actor_record *record;
    uint32_t cursor = 0;
    while (*count < capacity && qa_actors_next(qa_world_actors(p->world), &cursor, &record)) list[(*count)++] = record->id;
    bool ordered = true;
    for (size_t i = 1; i < *count; ++i)
        if (candidate_order(p, list[i-1], list[i]) > 0) { ordered = false; break; }
    qa_actor_id *temporary = list+frame->candidate_capacity;
    for (size_t width = 1; !ordered && width < *count; width *= 2) {
        for (size_t start = 0; start < *count; start += 2*width) {
            size_t middle = start+width < *count ? start+width : *count;
            size_t end = middle+width < *count ? middle+width : *count;
            size_t left = start, right = middle, target = start;
            while (left < middle && right < end)
                temporary[target++] = candidate_order(p, list[left], list[right]) <= 0 ? list[left++] : list[right++];
            while (left < middle) temporary[target++] = list[left++];
            while (right < end) temporary[target++] = list[right++];
        }
        memcpy(list, temporary, *count*sizeof(*list));
    }
    *out = list;
    return true;
}

static bool save_push(struct qa_physics_transaction *transaction, qa_actor_id actor,
                       const qa_body_state *body, const qa_physics_properties *props,
                       qa_error *error) {
    if (transaction->count == transaction->capacity)
        return ph_push_overflow(&transaction->owner->push_stats.rollback_overflows, error,
            "Reserved pusher rollback capacity exhausted");
    transaction->entries[transaction->count++] = (ph_pushed){.actor = actor,
        .origin = body->origin, .angles = body->angles, .delta_yaw = props->delta_yaw};
    if (transaction->count > transaction->owner->push_stats.peak_rollback)
        transaction->owner->push_stats.peak_rollback = transaction->count;
    return true;
}

static bool restore_q2(qa_physics *p, ph_pushed saved, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, saved.actor, &body, &props, error);
    if (read <= 0) return read == 0;
    body.origin = saved.origin;
    body.angles = saved.angles;
    if (!ph_write(p, saved.actor, &body, error)) return false;
    if ((props.flags & QA_PHYSICS_PLAYER) && ph_live(p, saved.actor)) {
        if (p->services.read(p->services.context, saved.actor, &props)) {
            props.delta_yaw = saved.delta_yaw;
            if (!ph_properties(p, saved.actor, &props, error)) return false;
        }
    }
    return ph_link(p, saved.actor, false, error);
}

static bool overlaps_strict(qa_bounds a, qa_bounds b) {
    return a.mins.x < b.maxs.x && a.mins.y < b.maxs.y && a.mins.z < b.maxs.z &&
           a.maxs.x > b.mins.x && a.maxs.y > b.mins.y && a.maxs.z > b.mins.z;
}

static qa_vec3 rotated_delta(qa_vec3 origin, qa_vec3 pusher_origin, qa_vec3 move,
                              qa_vec3 forward, qa_vec3 right, qa_vec3 up) {
    qa_vec3 offset = qa_vec_sub(qa_vec_add(origin, move), pusher_origin);
    qa_vec3 rotated = qa_v3(qa_vec_dot(offset, forward), -qa_vec_dot(offset, right), qa_vec_dot(offset, up));
    return qa_vec_add(move, qa_vec_sub(rotated, offset));
}

static bool q1_push(qa_physics *p, const qa_physics_push *input,
                     struct qa_physics_push_frame *frame, qa_physics_result *result, qa_error *error) {
    qa_body_state original, body;
    qa_physics_properties props;
    int read = ph_read(p, input->actor, &original, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    bool rotating = ph_moving(input->angular_displacement);
    qa_linked_body linked;
    qa_bounds original_bounds = qa_world_linked(p->world, input->actor, &linked) ?
        linked.absolute_bounds : qa_bounds_translate(original.bounds, original.origin);
    float local;
    if (!q1_store(props.q1_pusher.local_seconds + input->q1_elapsed_seconds, &local, error)) return false;
    props.q1_pusher.local_seconds = local;
    if (!ph_properties(p, input->actor, &props, error)) return false;
    if (!ph_moving(input->displacement) && !rotating) return true;
    read = ph_read(p, input->actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    body.origin = qa_vec_add(body.origin, input->displacement);
    body.angles = qa_vec_add(body.angles, input->angular_displacement);
    if (!ph_write(p, input->actor, &body, error) || !ph_link(p, input->actor, false, error)) return false;
    if (!ph_live(p, input->actor) || !qa_world_linked(p->world, input->actor, &linked)) {
        result->status = QA_PHYSICS_REMOVED; return true;
    }
    qa_bounds bounds = rotating ? linked.absolute_bounds : qa_bounds_translate(original_bounds, input->displacement);
    qa_vec3 pusher_origin = body.origin, forward, right, up;
    ph_axes(qa_vec_scale(input->angular_displacement, -1), &forward, &right, &up);
    qa_actor_id *list;
    size_t count;
    if (!candidates(p, frame, &list, &count, error)) return false;
    struct qa_physics_transaction *saved = &frame->pushed;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        qa_actor_id actor = list[i];
        if (qa_actor_id_equal(actor, input->actor)) continue;
        read = ph_read(p, actor, &body, &props, error);
        if (read < 0) { ok = false; break; }
        if (!read || props.motion == QA_PHYSICS_PUSH || props.motion == QA_PHYSICS_STOP ||
            props.motion == QA_PHYSICS_STATIONARY || props.motion == QA_PHYSICS_NOCLIP) continue;
        bool rider = qa_actor_id_equal(qa_physics_actor_reference(p, body.ground), input->actor) &&
                     (props.family != QA_COLLISION_Q1 || (props.flags & QA_PHYSICS_ONGROUND));
        if (!rider) {
            if (!qa_world_linked(p->world, actor, &linked) || !overlaps_strict(linked.absolute_bounds, bounds)) continue;
            bool blocked;
            if (!ph_test_position(p, actor, &blocked, error)) { ok = false; break; }
            if (!blocked) continue;
        }
        if (!(props.flags & QA_PHYSICS_PLAYER)) {
            props.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
            if (props.family != QA_COLLISION_Q1) body.ground = (qa_actor_reference){0};
            if (!ph_properties(p, actor, &props, error) || !ph_write(p, actor, &body, error)) { ok = false; break; }
        }
        if (!save_push(saved, actor, &body, &props, error)) { ok = false; break; }
        qa_vec3 original_position = body.origin;
        qa_vec3 delta = rotating ? rotated_delta(body.origin, pusher_origin, input->displacement, forward, right, up) : input->displacement;
        /* Source disables this brush during push, including nested touches. */
        qa_body_link_state pusher_link;
        bool had_link = qa_world_link_state(p->world, input->actor, &pusher_link);
        if (!qa_world_suspend_collision(p->world, input->actor, error)) { ok = false; break; }
        qa_trace_result trace;
        ok = qa_physics_push_entity(p, actor, delta, NULL, 0, &trace, error);
        if (ph_live(p, input->actor)) {
            if (ok) ok = ph_link(p, input->actor, false, error);
            else if (had_link) {
                qa_error ignored;
                (void)qa_world_restore_link_state(p->world, input->actor, &pusher_link, &ignored);
            }
        }
        if (!ok) break;
        if (!ph_live(p, input->actor)) { result->status = QA_PHYSICS_REMOVED; break; }
        qa_body_state live_pusher;
        qa_physics_properties live_properties;
        read = ph_read(p, input->actor, &live_pusher, &live_properties, error);
        if (read < 0) { ok = false; break; }
        if (!read) { result->status = QA_PHYSICS_REMOVED; break; }
        pusher_origin = live_pusher.origin;
        read = ph_read(p, actor, &body, &props, error);
        if (read < 0) { ok = false; break; }
        if (!read) continue;
        bool blocked;
        if (!ph_test_position(p, actor, &blocked, error)) { ok = false; break; }
        if (!blocked) {
            if (rotating) { body.angles = qa_vec_add(body.angles, input->angular_displacement); ok = ph_write(p, actor, &body, error); }
            continue;
        }
        if (body.bounds.mins.x == body.bounds.maxs.x) continue;
        if (props.solid == QA_PHYSICS_NOT_SOLID || props.solid == QA_PHYSICS_TRIGGER || props.solid == QA_PHYSICS_CORPSE) {
            body.bounds.mins = qa_v3(0, 0, body.bounds.mins.z);
            body.bounds.maxs = body.bounds.mins;
            ok = ph_write(p, actor, &body, error);
            continue;
        }
        body.origin = original_position;
        if (!ph_write(p, actor, &body, error) || !ph_link(p, actor, true, error)) { ok = false; break; }
        read = ph_read(p, input->actor, &body, &props, error);
        if (read < 0) { ok = false; break; }
        if (!read) { result->status = QA_PHYSICS_REMOVED; break; }
        body.origin = original.origin;
        body.angles = original.angles;
        if (!q1_store(props.q1_pusher.local_seconds - input->q1_elapsed_seconds, &local, error)) { ok = false; break; }
        props.q1_pusher.local_seconds = local;
        if (!ph_write(p, input->actor, &body, error) || !ph_properties(p, input->actor, &props, error) ||
            !ph_link(p, input->actor, false, error)) { ok = false; break; }
        result->status = QA_PHYSICS_BLOCKED;
        result->obstacle = actor;
        if (p->services.blocked && !p->services.blocked(p->services.context, input->actor, actor, error)) { ok = false; break; }
        for (size_t j = 0; j < saved->count; ++j) {
            ph_pushed entry = saved->entries[j];
            read = ph_read(p, entry.actor, &body, &props, error);
            if (read < 0) { ok = false; break; }
            if (!read) continue;
            body.origin = entry.origin;
            if (rotating) body.angles = qa_vec_sub(body.angles, input->angular_displacement);
            if (!ph_write(p, entry.actor, &body, error) || !ph_link(p, entry.actor, false, error)) { ok = false; break; }
        }
        if (!ph_live(p, input->actor)) result->status = QA_PHYSICS_REMOVED;
        break;
    }
    return ok;
}

static float snap_eighth(float v) { return truncf(v*8+(v > 0 ? 0.5f : -0.5f))*0.125f; }

static bool q2_push(qa_physics *p, const qa_physics_push *input,
                     struct qa_physics_push_frame *frame, struct qa_physics_transaction *saved, bool touch,
                     qa_physics_result *result, qa_error *error) {
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, input->actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    qa_vec3 move = qa_v3(snap_eighth(input->displacement.x), snap_eighth(input->displacement.y), snap_eighth(input->displacement.z));
    if (!ph_moving(move) && !ph_moving(input->angular_displacement)) return true;
    if (!save_push(saved, input->actor, &body, &props, error)) return false;
    body.origin = qa_vec_add(body.origin, move);
    body.angles = qa_vec_add(body.angles, input->angular_displacement);
    if (!ph_write(p, input->actor, &body, error) || !ph_link(p, input->actor, false, error)) return false;
    qa_linked_body linked;
    if (!ph_live(p, input->actor) || !qa_world_linked(p->world, input->actor, &linked)) {
        result->status = QA_PHYSICS_REMOVED; return true;
    }
    qa_bounds bounds = linked.absolute_bounds;
    qa_vec3 forward, right, up;
    ph_axes(qa_vec_scale(input->angular_displacement, -1), &forward, &right, &up);
    qa_actor_id *list;
    size_t count;
    if (!candidates(p, frame, &list, &count, error)) return false;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        qa_actor_id actor = list[i];
        if (qa_actor_id_equal(actor, input->actor) || !ph_live(p, input->actor)) continue;
        read = ph_read(p, actor, &body, &props, error);
        if (read < 0) { ok = false; break; }
        if (!read || !qa_world_linked(p->world, actor, &linked) || props.motion == QA_PHYSICS_PUSH ||
            props.motion == QA_PHYSICS_STOP || props.motion == QA_PHYSICS_STATIONARY || props.motion == QA_PHYSICS_NOCLIP) continue;
        bool rider = qa_actor_id_equal(qa_physics_actor_reference(p, body.ground), input->actor), blocked = false;
        if (!rider) {
            if (!qa_bounds_overlap(linked.absolute_bounds, bounds)) continue;
            if (!ph_test_position(p, actor, &blocked, error)) { ok = false; break; }
            if (!blocked) continue;
        }
        qa_body_state pusher;
        qa_physics_properties pusher_props;
        read = ph_read(p, input->actor, &pusher, &pusher_props, error);
        if (read < 0) { ok = false; break; }
        if (!read) { result->status = QA_PHYSICS_REMOVED; break; }
        blocked = pusher_props.motion == QA_PHYSICS_STOP && !rider;
        if (!blocked) {
            if (!save_push(saved, actor, &body, &props, error)) { ok = false; break; }
            body.origin = qa_vec_add(body.origin, rotated_delta(body.origin, pusher.origin, move, forward, right, up));
            if (!rider) body.ground = (qa_actor_reference){0};
            if (!ph_write(p, actor, &body, error)) { ok = false; break; }
            if (props.flags & QA_PHYSICS_PLAYER) {
                if (p->services.read(p->services.context, actor, &props)) {
                    props.delta_yaw += input->angular_displacement.y;
                    if (!ph_properties(p, actor, &props, error)) { ok = false; break; }
                }
            }
            if (!ph_live(p, input->actor)) { result->status = QA_PHYSICS_REMOVED; break; }
            if (!ph_live(p, actor)) continue;
            if (!ph_test_position(p, actor, &blocked, error)) { ok = false; break; }
            if (!blocked) { ok = ph_link(p, actor, false, error); continue; }
            read = ph_read(p, actor, &body, &props, error);
            if (read < 0) { ok = false; break; }
            if (!read) continue;
            body.origin = qa_vec_sub(body.origin, move);
            if (!ph_write(p, actor, &body, error) || !ph_test_position(p, actor, &blocked, error)) { ok = false; break; }
            if (!blocked) { --saved->count; continue; }
        }
        for (size_t j = saved->count; j > 0; --j)
            if (!restore_q2(p, saved->entries[j-1], error)) { ok = false; break; }
        if (!ok) break;
        result->status = QA_PHYSICS_BLOCKED;
        result->obstacle = actor;
        if (ph_live(p, input->actor) && p->services.blocked)
            ok = p->services.blocked(p->services.context, input->actor, actor, error);
        break;
    }
    if (ok && touch && result->status == QA_PHYSICS_MOVED)
        for (size_t j = saved->count; j > 0; --j)
            if (!qa_physics_touch_triggers(p, saved->entries[j-1].actor, error)) return false;
    return ok;
}

bool qa_physics_push_pusher(qa_physics *p, const qa_physics_push *input,
                            qa_physics_result *result, qa_error *error) {
    if (!p || !input || !result || !qa_vec_finite(input->displacement) || !qa_vec_finite(input->angular_displacement) ||
        !isfinite(input->q1_elapsed_seconds) || input->q1_elapsed_seconds < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid pusher displacement"); return false;
    }
    *result = (qa_physics_result){.status = QA_PHYSICS_MOVED};
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, input->actor, &body, &props, error);
    if (read <= 0) { result->status = ph_live(p, input->actor) ? QA_PHYSICS_UNMANAGED : QA_PHYSICS_REMOVED; return read == 0; }
    struct qa_physics_push_frame *frame = ph_push_frame(p, error);
    if (!frame) return false;
    bool ok;
    if (props.family == QA_COLLISION_Q1) ok = q1_push(p, input, frame, result, error);
    else {
        struct qa_physics_transaction *saved = p->push_transaction ? p->push_transaction : &frame->pushed;
        ok = q2_push(p, input, frame, saved, !p->push_transaction, result, error);
    }
    ph_push_frame_release(p, frame);
    return ok;
}

bool qa_physics_push_team(qa_physics *p, const qa_physics_push *parts, size_t count,
                          qa_physics_result *result, qa_error *error) {
    if (!p || !result || (count && !parts) || p->push_transaction) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid or nested pusher team transaction"); return false;
    }
    for (size_t i = 0; i < count; ++i)
        if (!qa_vec_finite(parts[i].displacement) || !qa_vec_finite(parts[i].angular_displacement) ||
            !isfinite(parts[i].q1_elapsed_seconds) || parts[i].q1_elapsed_seconds < 0) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Invalid pusher team displacement"); return false;
        }
    *result = (qa_physics_result){.status = QA_PHYSICS_MOVED};
    if (!count) return true;
    struct qa_physics_push_frame *frame = ph_push_frame(p, error);
    if (!frame) return false;
    struct qa_physics_transaction *saved = &frame->pushed;
    p->push_transaction = saved;
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        if (!ph_live(p, parts[i].actor)) continue;
        qa_physics_result part;
        if (!qa_physics_push_pusher(p, &parts[i], &part, error)) { ok = false; break; }
        if (part.status == QA_PHYSICS_BLOCKED) { *result = part; break; }
    }
    p->push_transaction = NULL;
    if (ok && result->status != QA_PHYSICS_BLOCKED)
        for (size_t i = saved->count; i > 0; --i)
            if (!qa_physics_touch_triggers(p, saved->entries[i-1].actor, error)) { ok = false; break; }
    ph_push_frame_release(p, frame);
    return ok;
}

bool qa_physics_step_q1_pusher(qa_physics *p, qa_actor_id actor,
                              const qa_source_frame *frame, bool rotate,
                              qa_physics_result *result, qa_error *error) {
    if (!p || !frame || !result) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q1 pusher frame"); return false;
    }
    *result = (qa_physics_result){.status = QA_PHYSICS_MOVED};
    qa_body_state body;
    qa_physics_properties props;
    int read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    double old_time = props.q1_pusher.local_seconds, think_time = props.q1_pusher.next_think_seconds;
    double seconds = (double)frame->elapsed_ns / 1e9;
    double elapsed = think_time < old_time + seconds ? fmax(0, think_time - old_time) : seconds;
    if (elapsed != 0) {
        qa_physics_push push = {.actor = actor, .q1_elapsed_seconds = elapsed};
        qa_vec3 velocity = rotate ? props.angular_velocity : body.velocity;
        qa_vec3 displacement;
        if (!q1_store((double)velocity.x * elapsed, &displacement.x, error) ||
            !q1_store((double)velocity.y * elapsed, &displacement.y, error) ||
            !q1_store((double)velocity.z * elapsed, &displacement.z, error)) return false;
        if (rotate) push.angular_displacement = displacement;
        else push.displacement = displacement;
        if (!qa_physics_push_pusher(p, &push, result, error)) return false;
    }
    read = ph_read(p, actor, &body, &props, error);
    if (read <= 0) { result->status = QA_PHYSICS_REMOVED; return read == 0; }
    if (think_time > old_time && think_time <= props.q1_pusher.local_seconds) {
        props.q1_pusher.next_think_seconds = 0;
        if (!ph_properties(p, actor, &props, error)) return false;
        if (ph_live(p, actor) && p->services.pusher_think &&
            !p->services.pusher_think(p->services.context, actor, frame, error)) return false;
    }
    if (!ph_live(p, actor)) result->status = QA_PHYSICS_REMOVED;
    return true;
}
