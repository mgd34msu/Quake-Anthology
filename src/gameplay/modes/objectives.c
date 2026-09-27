#include "internal.h"

static mode_objective *objective_find(qa_modes *m, qa_string_id id) {
    if (!m || !id)
        return NULL;
    for (uint32_t i = 0; i < m->objective_capacity; ++i)
        if (m->objectives[i].active && m->objectives[i].binding.id == id)
            return &m->objectives[i];
    return NULL;
}
bool qa_modes_bind_objective(qa_modes *m, const qa_objective_binding *binding,
                             qa_objective_lease *out, qa_error *e) {
    if (!m || !binding || !binding->owner || !binding->id || !binding->read || !out ||
        objective_find(m, binding->id) || m->next_serial == UINT64_MAX)
        return mode_fail(e, "invalid or duplicate objective");
    for (uint32_t i = 0; i < m->objective_capacity; ++i)
        if (!m->objectives[i].active) {
            uint64_t serial = m->next_serial++;
            m->objectives[i] =
                (mode_objective){.binding = *binding, .serial = serial, .active = true};
            *out = (qa_objective_lease){i, serial};
            return true;
        }
    return mode_fail(e, "objective capacity exhausted");
}
bool qa_modes_unbind_objective(qa_modes *m, qa_objective_lease lease, qa_error *e) {
    (void)e;
    if (m && lease.slot < m->objective_capacity && m->objectives[lease.slot].serial == lease.serial)
        m->objectives[lease.slot] = (mode_objective){0};
    return true;
}
static bool objective_read(qa_modes *m, mode_objective *o, qa_objective_state *out, qa_error *e) {
    uint64_t serial = o->serial;
    qa_objective_binding binding = o->binding;
    qa_objective_state state;
    if (!MODE_CALLBACK(m, binding.read(binding.context, &state, e)))
        return false;
    if (!o->active || o->serial != serial)
        return mode_fail(e, "objective owner changed during read");
    if (state.phase > QA_OBJECTIVE_COMPLETE || state.phase < QA_OBJECTIVE_HOME)
        return mode_fail(e, "invalid objective phase");
    if (state.actor.registry && !mode_live(m, state.actor))
        state.actor = (qa_actor_id){0};
    if (state.carrier.registry && !mode_live(m, state.carrier))
        state.carrier = (qa_actor_id){0};
    if (state.target.registry && !mode_live(m, state.target))
        state.target = (qa_actor_id){0};
    *out = state;
    return true;
}
bool qa_modes_objective(qa_modes *m, qa_string_id id, qa_objective_state *out, qa_error *e) {
    mode_objective *o = objective_find(m, id);
    if (!o || !out)
        return mode_fail(e, "unknown objective");
    return objective_read(m, o, out, e);
}
bool qa_modes_change_objective(qa_modes *m, qa_string_id id, const qa_objective_state *state,
                               qa_error *e) {
    mode_objective *o = objective_find(m, id);
    if (!o || !state || !o->binding.change || state->phase < QA_OBJECTIVE_HOME ||
        state->phase > QA_OBJECTIVE_COMPLETE ||
        (state->actor.registry && !mode_live(m, state->actor)) ||
        (state->carrier.registry && !mode_live(m, state->carrier)) ||
        (state->target.registry && !mode_live(m, state->target)))
        return mode_fail(e, "invalid objective change");
    uint64_t serial = o->serial;
    if (!MODE_CALLBACK(m, o->binding.change(o->binding.context, state, e)))
        return false;
    if (!o->active || o->serial != serial)
        return mode_fail(e, "objective owner changed during update");
    qa_objective_state actual;
    return objective_read(m, o, &actual, e);
}
bool qa_modes_campaign_gates(qa_modes *m, bool *complete, qa_error *e) {
    if (!m || !complete)
        return mode_fail(e, "invalid objective gate query");
    *complete = true;
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *o = &m->objectives[i];
        if (!o->active || !o->binding.campaign_gate)
            continue;
        qa_objective_state state;
        if (!objective_read(m, o, &state, e))
            return false;
        if (!state.complete)
            *complete = false;
    }
    return true;
}
bool qa_modes_objective_at(qa_modes *m, size_t index, qa_objective_binding *binding,
                           qa_objective_state *state, qa_error *e) {
    if (!m || !binding || !state)
        return mode_fail(e, "invalid objective enumeration");
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *o = &m->objectives[i];
        if (!o->active)
            continue;
        if (index--)
            continue;
        *binding = o->binding;
        return objective_read(m, o, state, e);
    }
    qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "objective index outside live set");
    return false;
}
