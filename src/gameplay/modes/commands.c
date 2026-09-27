#include "internal.h"

bool qa_modes_can_move(qa_modes *m, qa_mode_id id, qa_actor_id actor) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    return !v || !v->value.rules.paused || (p && (p->extra_flags & 2));
}
bool qa_modes_referee(qa_modes *m, qa_mode_id id, qa_actor_id actor, unsigned level, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || level > 2)
        return mode_fail(e, "invalid referee admission");
    p->extra_flags = (p->extra_flags & ~6) | (level == 2 ? 6 : level == 1 ? 2 : 0);
    p->admin = level != 0;
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0,
                      (int32_t)level, QA_MATCH_ADMIN, e);
}
bool qa_modes_admin(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_mode_admin_action action,
                    qa_string_id map, bool *accepted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p || !accepted || action < QA_MODE_ADMIN_PAUSE || action > QA_MODE_ADMIN_MATCH_MAP)
        return mode_fail(e, "invalid match command");
    *accepted = false;
    bool admin = v->value.rules.source == QA_MODE_Q2_CTF ? p->admin : (p->extra_flags & 2) != 0;
    if (action != QA_MODE_ADMIN_PAUSE_ANY && !admin)
        return true;
    if (action == QA_MODE_ADMIN_MAP || action == QA_MODE_ADMIN_MATCH_MAP) {
        if (!map || !m->options.hooks.map_allowed ||
            !m->options.hooks.map_allowed(m->options.hooks.context, id, map))
            return true;
        *accepted = true;
        return mode_intent(
            m, v, action == QA_MODE_ADMIN_MATCH_MAP ? QA_MATCH_START : QA_MATCH_SELECTED_MAP, actor,
            0, map, e);
    }
    *accepted = true;
    switch (action) {
    case QA_MODE_ADMIN_PAUSE:
    case QA_MODE_ADMIN_PAUSE_ANY:
        v->value.rules.paused = !v->value.rules.paused;
        if (v->value.rules.auto_lock)
            v->value.rules.match_lock = !v->value.rules.paused;
        break;
    case QA_MODE_ADMIN_LOCK:
        v->value.rules.match_lock = !v->value.rules.match_lock;
        break;
    case QA_MODE_ADMIN_START:
        if (v->value.phase != QA_MODE_WAITING && v->value.phase != QA_MODE_SETUP) {
            *accepted = false;
            return true;
        }
        return qa_modes_start(m, id, e);
    case QA_MODE_ADMIN_STOP:
        if (v->value.phase == QA_MODE_WAITING) {
            *accepted = false;
            return true;
        }
        return qa_modes_cancel(m, id, e);
    case QA_MODE_ADMIN_MAP:
    case QA_MODE_ADMIN_MATCH_MAP:
        break;
    }
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, action,
                      QA_MATCH_ADMIN, e);
}
