#include "maps/internal.h"

static bool toggle(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = q1_entity(g, id);
    if (!e)
        return true;
    if (!e->frozen.active) {
        qa_combat_state combat;
        if (!qa_combat_read_traits(g->services.combat, id, &combat, error))
            return false;
        e = q1_entity(g, id);
        if (!e)
            return true;
        e->frozen.think = e->think;
        e->frozen.next_think = e->next_think;
        e->frozen.physics_think = e->physics.next_think_ns;
        e->frozen.damageable = combat.can_take_damage;
        if (!q1_map_damageable(g, e, false, error))
            return false;
        e = q1_entity(g, id);
        if (!e)
            return true;
        qa_scheduler_cancel(qa_session_scheduler(g->services.session), id);
        e->think = Q1_THINK_NONE;
        e->next_think = -1;
        e->physics.next_think_ns = -1;
        e->frozen.active = true;
        return true;
    }
    q1_think_kind think = e->frozen.think;
    double due = e->frozen.next_think;
    bool damageable = e->frozen.damageable;
    if (e->physics.motion == QA_PHYSICS_PUSH) {
        e->think = think;
        e->next_think = due;
        e->physics.next_think_ns = e->frozen.physics_think;
    } else if (think != Q1_THINK_NONE && due >= 0) {
        if (!q1_schedule(g, e, due - g->time, think, error))
            return false;
    } else {
        e->think = think;
        e->next_think = due;
    }
    e = q1_entity(g, id);
    if (!e)
        return true;
    e->frozen.active = false;
    e->frozen.next_think = -1;
    return q1_map_damageable(g, e, damageable, error);
}
bool qa_q1_game_freeze(qa_q1_game *g, qa_actor_id id, bool *handled, qa_error *error) {
    if (!g || !handled)
        return q1_map_fail(error, "Invalid Q1 freeze continuation request");
    *handled = false;
    q1_actor *e = q1_entity(g, id);
    if (!e || !e->native)
        return true;
    *handled = true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = toggle(g, id, error);
    qa_q1_game_operation_end(&operation);
    return ok;
}
