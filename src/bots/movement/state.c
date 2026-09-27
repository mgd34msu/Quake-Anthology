#include "internal.h"

bool bot_move_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_move_mutable(qa_bot_moves *m, qa_error *e) {
    return m && !m->busy ? true : bot_move_fail(e, "bot movement owner is absent or active");
}
bool qa_bot_moves_has_handle(const qa_bot_moves *m, uint32_t id) {
    return m && id && id <= m->maximum && m->slots[id - 1].used;
}
qa_bot_move_state *bot_move_state(const qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!m || !id || id > m->maximum || !m->slots[id - 1].used) {
        bot_move_fail(e, "invalid bot movement state handle");
        return NULL;
    }
    return &m->slots[id - 1].state;
}
bool qa_bot_moves_create(uint32_t maximum, qa_bot_library *library, qa_bot_actions *actions,
                         const qa_bot_move_services *services, qa_bot_moves **out, qa_error *e) {
    if (!maximum || maximum > SIZE_MAX / sizeof(bot_move_slot) || !library || !actions ||
        !services || !services->navigation || !services->random.next || !out)
        return bot_move_fail(e, "invalid bot movement services/capacity");
    qa_bot_moves *m = calloc(1, sizeof(*m));
    if (!m || !(m->slots = calloc(maximum, sizeof(*m->slots)))) {
        free(m);
        qa_error_set(e, QA_ERROR_MEMORY, maximum, "allocating native bot movement states");
        return false;
    }
    m->maximum = maximum;
    m->library = library;
    m->actions = actions;
    m->services = *services;
    if (!qa_nav_workspace_create(&m->workspace, e)) {
        qa_bot_moves_destroy(m);
        return false;
    }
    *out = m;
    return true;
}
void qa_bot_moves_destroy(qa_bot_moves *m) {
    if (!m)
        return;
    qa_nav_prediction_result_free(&m->prediction);
    qa_nav_route_free(&m->trajectory);
    qa_nav_workspace_destroy(m->workspace);
    free(m->candidates);
    free(m->points);
    free(m->visited);
    free(m->slots);
    free(m);
}
bool qa_bot_moves_setup(qa_bot_moves *m, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    static const char *const names[BOT_MOVE_VARIABLE_COUNT] = {
        "sv_step",          "sv_maxbarrier",     "sv_gravity",        "weapindex_rocketlauncher",
        "weapindex_bfg10k", "weapindex_grapple", "entitytypemissile", "offhandgrapple",
        "cmd_grappleon",    "cmd_grappleoff"};
    static const char *const defaults[BOT_MOVE_VARIABLE_COUNT] = {
        "18", "32", "800", "5", "9", "10", "3", "0", "grappleon", "grappleoff"};
    for (size_t i = 0; i < BOT_MOVE_VARIABLE_COUNT; ++i)
        if (!qa_bot_library_variable_default(m->library, names[i], defaults[i], &m->variables[i],
                                             e))
            return false;
    return true;
}
bool qa_bot_moves_time(qa_bot_moves *m, float time, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!isfinite(time))
        return bot_move_fail(e, "invalid movement observation time");
    m->time = time;
    return true;
}
bool qa_bot_moves_allocate(qa_bot_moves *m, uint32_t *out, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!out)
        return bot_move_fail(e, "missing move state handle output");
    *out = 0;
    for (uint32_t i = 0; i < m->maximum; ++i)
        if (!m->slots[i].used) {
            m->slots[i] = (bot_move_slot){.used = true};
            *out = i + 1;
            break;
        }
    return true;
}
bool qa_bot_moves_free(qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!bot_move_mutable(m, e) || !bot_move_state(m, id, e))
        return false;
    m->slots[id - 1] = (bot_move_slot){0};
    return true;
}
bool qa_bot_moves_initialize(qa_bot_moves *m, uint32_t id, const qa_bot_move_input *input,
                             qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    if (!input || !qa_vec_finite(input->origin) || !qa_vec_finite(input->velocity) ||
        !qa_vec_finite(input->view_offset) || !qa_vec_finite(input->view_angles) ||
        !isfinite(input->think_time))
        return bot_move_fail(e, "invalid bot movement input");
    if (input->flags & QA_BOT_MOVE_TELEPORTED)
        s->walk_progress = false;
    uint32_t mask = QA_BOT_MOVE_ON_GROUND | QA_BOT_MOVE_TELEPORTED | QA_BOT_MOVE_WATER_JUMP |
                    QA_BOT_MOVE_WALK | QA_BOT_MOVE_GRAPPLE_PULL;
    uint32_t flags = (s->input.flags & ~mask) | (input->flags & mask);
    s->input = *input;
    s->input.flags = flags;
    return true;
}
bool qa_bot_moves_reset(qa_bot_moves *m, uint32_t id, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    *s = (qa_bot_move_state){0};
    return true;
}
bool qa_bot_moves_reset_avoid(qa_bot_moves *m, uint32_t id, bool last, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    if (!last) {
        s->avoid_reachability = 0;
        s->avoid_time = 0;
        s->avoid_tries = 0;
    } else if (s->avoid_time > 0) {
        s->avoid_time = 0;
        /* The donor defines the release32 probe into the first spot's x word.
         * Express that value deliberately without an out-of-bounds C access. */
        int32_t probe;
        memcpy(&probe, &s->avoid_spots[0].origin.x, sizeof(probe));
        if (probe > 0) {
            uint32_t word = (uint32_t)s->avoid_tries - 1;
            memcpy(&s->avoid_tries, &word, sizeof(word));
        }
    }
    return true;
}
void bot_move_avoid(qa_bot_moves *m, qa_bot_move_state *s, uint32_t reach, float duration) {
    if (s->avoid_reachability == reach) {
        uint32_t tries = s->avoid_time > m->time ? (uint32_t)s->avoid_tries + 1 : 1;
        memcpy(&s->avoid_tries, &tries, sizeof(tries));
        s->avoid_time = m->time + duration;
    } else if (s->avoid_time < m->time) {
        s->avoid_reachability = reach;
        s->avoid_time = m->time + duration;
        s->avoid_tries = 1;
    }
}
void bot_move_set_reach(qa_bot_move_state *s, uint32_t reach) {
    if (s->last_reachability != reach)
        s->walk_progress = false;
    s->last_reachability = reach;
}
bool qa_bot_moves_avoid_spot(qa_bot_moves *m, uint32_t id, const qa_bot_avoid_spot *spot,
                             qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    if (!spot || !qa_vec_finite(spot->origin) || !isfinite(spot->radius))
        return bot_move_fail(e, "invalid bot avoid spot");
    if (!spot->type)
        s->avoid_count = 0;
    else if (s->avoid_count < QA_BOT_AVOID_SPOTS)
        s->avoid_spots[s->avoid_count++] = *spot;
    return true;
}
bool qa_bot_moves_capture(const qa_bot_moves *m, uint32_t id, qa_bot_move_state *out, qa_error *e) {
    qa_bot_move_state *s = bot_move_state(m, id, e);
    if (!s)
        return false;
    if (!out)
        return bot_move_fail(e, "missing move checkpoint output");
    *out = *s;
    return true;
}
bool qa_bot_moves_restore(qa_bot_moves *m, uint32_t id, const qa_bot_move_state *state,
                          qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!id || id > m->maximum || !state || state->avoid_count > QA_BOT_AVOID_SPOTS ||
        !qa_vec_finite(state->input.origin) || !qa_vec_finite(state->input.velocity) ||
        !qa_vec_finite(state->input.view_offset) || !qa_vec_finite(state->input.view_angles) ||
        !qa_vec_finite(state->last_origin) || !isfinite(state->input.think_time) ||
        !isfinite(state->grapple_visible_time) || !isfinite(state->last_grapple_distance) ||
        !isfinite(state->reachability_time) || !isfinite(state->avoid_time))
        return bot_move_fail(e, "invalid bot movement checkpoint");
    for (size_t i = 0; i < state->avoid_count; ++i)
        if (!qa_vec_finite(state->avoid_spots[i].origin) || !isfinite(state->avoid_spots[i].radius))
            return bot_move_fail(e, "invalid saved avoid spot");
    if (state->walk_progress) {
        m->busy = true;
        qa_bot_navigation *n = m->services.navigation(m->services.context, state->input.client);
        m->busy = false;
        if (!n || !qa_navigation_edge(qa_bot_navigation_runtime(n), state->walk_edge))
            return bot_move_fail(e, "saved movement edge is absent from selected navigation");
    }
    m->slots[id - 1] = (bot_move_slot){.used = true, .state = *state};
    return true;
}
