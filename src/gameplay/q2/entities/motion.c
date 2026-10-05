#include "internal.h"

static float acceleration_distance(float target, float rate) {
    return target * (target / rate + 1) * .5f;
}
static void calculate(q2_motion *m, const q2_entity_state *s, bool rerelease) {
    m->move_speed = s->speed;
    if (m->remaining < s->accel) {
        m->current_speed = m->remaining;
        return;
    }
    float accel = acceleration_distance(s->speed, s->accel),
          decel = acceleration_distance(s->speed, s->decel);
    if (m->remaining - accel - decel < 0) {
        float factor = (s->accel + s->decel) / (s->accel * s->decel);
        float discriminant = 4 - 4 * factor * (-2 * m->remaining);
        m->move_speed = rerelease ? (-2 + sqrtf(discriminant)) / (2 * factor) :
            (float)((-2 + sqrt(discriminant)) / (2 * factor));
        if (rerelease)
            m->current_speed = m->move_speed;
        decel = acceleration_distance(m->move_speed, s->decel);
    }
    m->decel_distance = decel;
}
static void accelerate(q2_motion *m, const q2_entity_state *s, bool rerelease) {
    if (m->remaining <= m->decel_distance) {
        if (m->remaining < m->decel_distance) {
            if (m->next_speed != 0) {
                m->current_speed = m->next_speed;
                m->next_speed = 0;
                return;
            }
            if (m->current_speed > s->decel) {
                m->current_speed -= s->decel;
                if (rerelease && fabsf(m->current_speed) < .01f)
                    m->current_speed = m->remaining + 1;
            }
        }
        return;
    }
    if (m->current_speed == m->move_speed && m->remaining - m->current_speed < m->decel_distance) {
        float first = m->remaining - m->decel_distance,
              second = m->move_speed * (1 - first / m->move_speed);
        m->next_speed = m->move_speed - s->decel * (second / (first + second));
        return;
    }
    if (m->current_speed < s->speed) {
        float old = m->current_speed;
        m->current_speed = fminf(m->current_speed + s->accel, s->speed);
        if (m->remaining - m->current_speed >= m->decel_distance)
            return;
        float first = m->remaining - m->decel_distance, first_speed = (old + m->move_speed) * .5f;
        float second = m->move_speed * (1 - first / first_speed), distance = first + second;
        m->current_speed = first_speed * (first / distance) + m->move_speed * (second / distance);
        m->next_speed = m->move_speed - s->decel * (second / distance);
    }
}
static bool velocity(qa_q2_game *g, q2_actor *a, qa_vec3 value, qa_error *e) {
    if (a->entity->mover->motion.angular) {
        a->physics.angular_velocity = value;
        return true;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    body.velocity = value;
    return qa_world_body_write(g->services.world, a->id, &body, e);
}
static bool schedule_ns(qa_q2_game *g, q2_actor *a, q2_entity_think think, uint64_t duration) {
    a->entity->think = think;
    a->entity->due_ns = q2_deadline(g->now_ns, duration);
    return true;
}
static bool finish(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_mover *m = a->entity->mover;
    q2_move_done done = m->motion.done;
    if (!velocity(g, a, qa_v3(0, 0, 0), e))
        return false;
    m->moving = false;
    m->motion = (q2_motion){0};
    return q2_move_finished(g, a, done, e);
}
static bool final(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_motion *m = &a->entity->mover->motion;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 delta;
    if (m->angular)
        delta = qa_vec_sub(m->destination, body.angles);
    else if (m->remaining == 0)
        return finish(g, a, e);
    else
        delta = g->options.edition == QA_Q2_RERELEASE ? qa_vec_sub(m->destination, body.origin)
                                                      : qa_vec_scale(m->direction, m->remaining);
    if (m->angular && qa_vec_dot(delta, delta) == 0)
        return finish(g, a, e);
    if (!velocity(g, a, qa_vec_scale(delta, (float)Q2_NS / (float)g->frame_ns), e))
        return false;
    return schedule_ns(g, a, Q2ET_MOVE_DONE, g->frame_ns);
}
static bool begin(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_motion *m = &s->mover->motion;
    float frame = (float)g->frame_ns / Q2_NS;
    if (m->angular) {
        if (m->current_speed < s->speed)
            m->current_speed = fminf(s->speed, m->current_speed + s->accel);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        qa_vec3 delta = qa_vec_sub(m->destination, body.angles);
        float seconds = qa_vec_length(delta) / m->current_speed;
        if (seconds < frame)
            return final(g, a, e);
        if (!velocity(g, a, qa_vec_scale(delta, 1 / seconds), e))
            return false;
        return schedule_ns(g, a, m->current_speed >= s->speed ? Q2ET_MOVE_FINAL : Q2ET_MOVE_BEGIN,
                           m->current_speed >= s->speed
                               ? q2_item_seconds(floorf(seconds / frame) * frame)
                               : g->frame_ns);
    }
    if (s->speed * frame >= m->remaining)
        return final(g, a, e);
    if (!velocity(g, a, qa_vec_scale(m->direction, s->speed), e))
        return false;
    float frames = floorf(m->remaining / s->speed / frame);
    m->remaining -= frames * s->speed * frame;
    return schedule_ns(g, a, Q2ET_MOVE_FINAL, q2_item_seconds(frames * frame));
}
static void curve_sample(q2_motion *m, const q2_entity_state *s) {
    if (m->current_speed == 0)
        calculate(m, s, true);
    accelerate(m, s, true);
    m->curve_from = m->curve_to;
    if (m->remaining <= m->current_speed) {
        m->curve_to = m->curve_distance;
        m->final_sample = true;
    } else {
        m->remaining -= m->current_speed;
        m->curve_to = m->curve_distance - m->remaining;
    }
}
static bool curve_parameters(const q2_entity_state *s, const q2_motion *m, qa_error *e) {
    if (s->speed <= 0 || s->accel <= 0 || s->decel <= 0 ||
        !isfinite(s->speed) || !isfinite(s->accel) || !isfinite(s->decel) ||
        !qa_vec_finite(m->destination) || !qa_vec_finite(m->reference) ||
        !isfinite(m->curve_distance) || m->curve_distance < 0) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Original Q2 acceleration curve has invalid parameters");
        return false;
    }
    return true;
}
bool q2_move_curve_samples(const q2_entity_state *s, const q2_motion *motion,
                           q2_move_sample_fn emit, void *context, qa_error *e) {
    if (!emit || !curve_parameters(s, motion, e)) return false;
    q2_motion m = {.remaining = motion->curve_distance, .curve_distance = motion->curve_distance};
    if (motion->curve_subframes && !emit(context, 0, e)) return false;
    for (;;) {
        float before = m.remaining;
        curve_sample(&m, s);
        if (m.final_sample)
            return !motion->curve_subframes || emit(context, m.curve_distance, e);
        if (!isfinite(m.curve_to) || m.remaining >= before) {
            qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 acceleration curve does not advance");
            return false;
        }
        if (!emit(context, m.curve_to, e)) return false;
    }
}
bool q2_move_curve_restore(q2_motion *motion, const q2_entity_state *s, qa_vec3 reference,
                           uint32_t frame, uint32_t subframe, uint32_t subframes,
                           uint64_t frames_done, qa_error *e) {
    if (subframes > UINT8_MAX || subframe > UINT8_MAX) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Original Q2 subframe exceeds its Source byte");
        return false;
    }
    q2_motion rebuilt = {.direction = motion->direction, .destination = motion->destination,
        .reference = reference, .curve_distance = qa_vec_length(qa_vec_sub(motion->destination, reference)),
        .done = motion->done, .accelerated = true, .curve = true, .curve_frame = frame,
        .curve_subframe = (uint8_t)subframe, .curve_subframes = (uint8_t)subframes,
        .curve_frames_done = frames_done};
    if (!curve_parameters(s, &rebuilt, e)) return false;
    if ((subframes && !frame) || subframe > subframes + 1 || (!subframes && subframe)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Original Q2 curve counters exceed their sample");
        return false;
    }
    rebuilt.remaining = rebuilt.curve_distance;
    uint32_t samples = frame ? frame : 1;
    for (uint32_t i = 0; i < samples; ++i) {
        if (rebuilt.final_sample) {
            qa_error_set(e, QA_ERROR_FORMAT, i, "Original Q2 curve frame exceeds its motion");
            return false;
        }
        float before = rebuilt.remaining;
        curve_sample(&rebuilt, s);
        if (!isfinite(rebuilt.curve_to) || (!rebuilt.final_sample && rebuilt.remaining >= before)) {
            qa_error_set(e, QA_ERROR_FORMAT, i, "Original Q2 acceleration curve does not advance");
            return false;
        }
    }
    *motion = rebuilt;
    return true;
}
static bool curve(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_motion *m = &s->mover->motion;
    float distance;
    if (m->curve_subframes) {
        if (m->curve_subframe == m->curve_subframes + 1) {
            m->curve_subframe = 0;
            ++m->curve_frame;
            if (m->final_sample)
                return final(g, a, e);
            curve_sample(m, s);
        }
        float fraction = (float)(m->curve_subframe + 1) / (float)(m->curve_subframes + 1);
        distance = m->curve_from + (m->curve_to - m->curve_from) * fraction;
        ++m->curve_subframe;
    } else {
        if (m->curve_frame)
            curve_sample(m, s);
        if (m->final_sample)
            return final(g, a, e);
        distance = m->curve_to;
        ++m->curve_frame;
    }
    ++m->curve_frames_done;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 target = qa_vec_add(m->reference, qa_vec_scale(m->direction, distance));
    if (!velocity(g, a,
                  qa_vec_scale(qa_vec_sub(target, body.origin), (float)Q2_NS / (float)g->frame_ns),
                  e))
        return false;
    return schedule_ns(g, a, Q2ET_MOVE_ACCEL, g->frame_ns);
}
q2_mover *q2_mover_state(q2_actor *a, qa_error *e) {
    if (!a->entity->mover) {
        a->entity->mover = calloc(1, sizeof(*a->entity->mover));
        if (!a->entity->mover)
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 team member motion");
    }
    return a->entity->mover;
}
bool q2_move_start(qa_q2_game *g, q2_actor *a, qa_vec3 destination, bool angular, q2_move_done done,
                   qa_error *e) {
    q2_entity_state *s = a->entity;
    if (!qa_vec_finite(destination) || s->speed <= 0 || s->accel <= 0 || s->decel <= 0 ||
        !g->frame_ns) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 mover motion parameters");
        return false;
    }
    if (!q2_mover_state(a, e))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 reference = angular ? body.angles : body.origin,
            delta = qa_vec_sub(destination, reference);
    float distance = qa_vec_length(delta);
    q2_motion *m = &s->mover->motion;
    *m = (q2_motion){.direction = distance != 0 ? qa_vec_scale(delta, 1 / distance) : qa_v3(0, 0, 0),
                     .destination = destination,
                     .reference = reference,
                     .remaining = distance,
                     .done = done,
                     .angular = angular};
    s->mover->moving = true;
    if (!velocity(g, a, qa_v3(0, 0, 0), e))
        return false;
    if (angular)
        m->current_speed =
            g->options.edition == QA_Q2_RERELEASE && s->accel != s->speed ? 0 : s->speed;
    else if (s->speed != s->accel || s->speed != s->decel) {
        m->accelerated = true;
        if (g->options.edition == QA_Q2_RERELEASE && g->frame_ns != 100 * Q2_MS) {
            m->curve = true;
            m->curve_distance = distance;
            float subframes = .1f / ((float)g->frame_ns / Q2_NS) - 1;
            m->curve_subframes = subframes > 0 ? (uint8_t)subframes : 0;
            m->curve_frame = m->curve_subframes ? 1 : 0;
            curve_sample(m, s);
        }
        return schedule_ns(g, a, Q2ET_MOVE_ACCEL, g->frame_ns);
    }
    qa_actor_id owner = s->team_master.registry ? s->team_master : a->id;
    return qa_actor_id_equal(qa_q2_current_actor(g), owner)
               ? begin(g, a, e)
               : schedule_ns(g, a, Q2ET_MOVE_BEGIN, g->frame_ns);
}
bool q2_move_tick(qa_q2_game *g, q2_actor *a, q2_entity_think think, qa_error *e) {
    if (!a->entity->mover || !a->entity->mover->moving) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 mover continuation has no motion");
        return false;
    }
    if (think == Q2ET_MOVE_DONE)
        return finish(g, a, e);
    if (think == Q2ET_MOVE_FINAL)
        return final(g, a, e);
    if (think == Q2ET_MOVE_BEGIN)
        return begin(g, a, e);
    q2_entity_state *s = a->entity;
    q2_motion *m = &s->mover->motion;
    if (m->curve)
        return curve(g, a, e);
    if (g->options.edition == QA_Q2_RERELEASE) {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        m->remaining = qa_vec_length(qa_vec_sub(m->destination, b.origin));
    } else
        m->remaining -= m->current_speed;
    if (m->current_speed == 0)
        calculate(m, s, g->options.edition == QA_Q2_RERELEASE);
    accelerate(m, s, g->options.edition == QA_Q2_RERELEASE);
    if (m->remaining <= m->current_speed)
        return final(g, a, e);
    if (!velocity(g, a,
                  qa_vec_scale(m->direction, m->current_speed * (float)Q2_NS / (float)g->frame_ns),
                  e))
        return false;
    return schedule_ns(g, a, Q2ET_MOVE_ACCEL, g->frame_ns);
}
