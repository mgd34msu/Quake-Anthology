#include "internal.h"

static bool direction(bot_travel *t, const qa_bot_vector_source *direction, float speed, uint32_t type, bool *moved,
                      qa_error *e) {
    qa_bot_move_input *s = &t->state->input;
    uint32_t caps = t->graph->profile.capabilities;
    *moved = false;
    bool swimming;
    if (!qa_bot_navigation_swimming(t->navigation, s->origin, &swimming, e))
        return false;
    if (swimming) {
        if (!(caps & QA_NAV_CAPABILITY(QA_NAV_SWIM)))
            return true;
        qa_vec3 vector;
        if (!qa_bot_vector_read(direction, &vector, e) ||
            !bot_move_action(t, qa_vec_normalize(vector), speed, e))
            return false;
        *moved = true;
        return true;
    }
    bool grounded;
    if (!bot_on_ground(t, &grounded, e))
        return false;
    if (grounded)
        s->flags |= QA_BOT_MOVE_ON_GROUND;
    if (s->flags & QA_BOT_MOVE_ON_GROUND) {
        bool barrier;
        if (!bot_barrier_jump_from(t, direction, speed, &barrier, e))
            return false;
        if (barrier) {
            *moved = true;
            return true;
        }
        s->flags &= ~QA_BOT_MOVE_BARRIER_JUMP;
        uint32_t presence =
            (type & QA_BOT_DIRECTION_CROUCH) && !(type & QA_BOT_DIRECTION_JUMP) ? 4 : 2;
        qa_vec3 horizontal = {0};
        if (!qa_bot_vector_component(direction, 0, &horizontal.x, e) ||
            !qa_bot_vector_component(direction, 1, &horizontal.y, e)) return false;
        horizontal = qa_vec_normalize(horizontal);
        float gap;
        if (!(type & QA_BOT_DIRECTION_JUMP)) {
            if (!bot_gap_distance(t, s->origin, horizontal, &gap, e))
                return false;
            if (gap > 0)
                type |= QA_BOT_DIRECTION_JUMP;
        }
        bool jumping = (type & QA_BOT_DIRECTION_JUMP) != 0;
        bool crouching = (type & QA_BOT_DIRECTION_CROUCH) != 0;
        if ((jumping && !(caps & QA_NAV_CAPABILITY(QA_NAV_JUMP))) ||
            (crouching && !(caps & QA_NAV_CAPABILITY(QA_NAV_CROUCH))))
            return true;
        qa_nav_prediction_query q = {.origin = s->origin,
                                     .velocity = s->velocity,
                                     .presence = presence,
                                     .on_ground = true,
                                     .command_move = qa_vec_scale(horizontal, speed),
                                     .command_frames = jumping ? 1 : 2,
                                     .maximum_frames = jumping ? 30 : 2,
                                     .frame_ms = 100,
                                     .stop_events = (jumping ? 1 : 0) | 32 | 4 | 8 | 16};
        q.origin.z += .5f;
        if (jumping)
            q.command_move.z = 400;
        if (!bot_predict(t, &q, e))
            return false;
        const qa_nav_prediction_result *prediction = &t->moves->prediction;
        if ((jumping && prediction->frames >= q.maximum_frames) ||
            (prediction->stop_event & (8 | 16 | 32)))
            return true;
        if (prediction->stop_event & 1) {
            if (!bot_gap_distance(t, prediction->end, qa_vec_normalize(prediction->velocity), &gap,
                                  e))
                return false;
            if (gap > 0)
                return true;
            if (!bot_gap_distance(t, prediction->end, horizontal, &gap, e))
                return false;
            if (gap > 0)
                return true;
        }
        if (qa_vec_length(bot_horizontal(s->origin, prediction->end)) <
            (speed * s->think_time) * .5f)
            return true;
        if (jumping && !bot_jump_action(t, false, e))
            return false;
        if (crouching && !bot_flag_action(t, QA_BOT_CROUCH, e))
            return false;
        if (!bot_move_action(t, horizontal, speed, e))
            return false;
    } else if ((s->flags & QA_BOT_MOVE_BARRIER_JUMP) && s->velocity.z < 50) {
        qa_vec3 vector;
        if (!qa_bot_vector_read(direction, &vector, e) || !bot_move_action(t, vector, speed, e))
            return false;
    }
    *moved = true;
    return true;
}
bool qa_bot_moves_direction(qa_bot_moves *m, uint32_t handle, qa_vec3 vector, float speed,
                            uint32_t type, bool *moved, qa_error *e) {
    if (!qa_vec_finite(vector) || !isfinite(speed))
        return bot_move_fail(e, "invalid bot movement direction");
    qa_bot_vector_source source = {.value = &vector};
    return qa_bot_moves_direction_from(m, handle, &source, speed, type, moved, e);
}
bool qa_bot_moves_direction_from(qa_bot_moves *m, uint32_t handle,
                                 const qa_bot_vector_source *source, float speed,
                                 uint32_t type, bool *moved, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *state = bot_move_state(m, handle, e);
    if (!state)
        return false;
    if (!moved || !source || (!source->value && !source->read))
        return bot_move_fail(e, "missing bot movement direction fields");
    m->busy = true;
    bot_travel t;
    bool ok = bot_travel_ready(m, e) && bot_travel_begin(m, state, &t, e) &&
        direction(&t, source, speed, type, moved, e);
    m->busy = false;
    return ok;
}
