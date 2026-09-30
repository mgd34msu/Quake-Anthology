#include "internal.h"
#include "qa/console.h"
#include <ctype.h>

static bool named(const char *left, const char *right) {
    while (*left && *right)
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++))
            return false;
    return *left == *right;
}
static bool integer_argument(const char *text, int32_t *out) {
    char *end;
    long value = strtol(text, &end, 10);
    if (end == text || *end || value < INT32_MIN || value > INT32_MAX)
        return false;
    *out = (int32_t)value;
    return true;
}
bool qa_modes_console_command(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                               const qa_command_invocation *command, bool *handled,
                               qa_error *e) {
    if (!m || !command || !handled || !command->argc || !command->argv || !command->argv[0])
        return mode_fail(e, "invalid mode console invocation");
    *handled = false;
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p)
        return true;
    const char *name = command->argv[0];
    const char *arg = command->argc > 1 ? command->argv[1] : "";
    if (!arg)
        return mode_fail(e, "missing mode command argument");
    bool accepted;
    if (named(name, "team") || named(name, "join")) {
        *handled = true;
        if (!*arg && named(name, "team")) {
            qa_team_id current;
            if (!qa_modes_team(m, id, actor, &current, e))
                return false;
            return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0},
                                current, 0, 0, e);
        }
        bool spectator = named(arg, "spectator") || named(arg, "s") || named(arg, "scoreboard");
        bool automatic = !*arg || named(arg, "auto");
        qa_team_id team = named(arg, "red") || named(arg, "r") || named(arg, "1")
                             ? v->value.rules.teams[0]
                         : named(arg, "blue") || named(arg, "b") || named(arg, "2")
                             ? v->value.rules.teams[1] : 0;
        if (!spectator && !automatic && !team && !named(arg, "free") && !named(arg, "f"))
            return true;
        bool ok = qa_modes_request_team(m, id, actor, team, spectator, automatic, &accepted, e);
        if (ok && accepted && named(arg, "scoreboard"))
            ok = qa_modes_scoreboard(m, id, actor, true, e);
        return ok;
    }
    if (named(name, "spectator") || named(name, "observe")) {
        *handled = true;
        return qa_modes_observe(m, id, actor, 0, &accepted, e);
    }
    if (named(name, "ready") || named(name, "notready")) {
        *handled = true;
        return qa_modes_ready(m, id, actor, named(name, "ready"), e);
    }
    if (named(name, "score") || named(name, "+scores") || named(name, "-scores")) {
        *handled = true;
        return qa_modes_scoreboard(m, id, actor, !named(name, "-scores"), e);
    }
    if (named(name, "kill"))
        return qa_modes_suicide(m, id, actor, handled, e);
    if (named(name, "follownext") || named(name, "followprev")) {
        *handled = true;
        return qa_modes_follow(m, id, actor, (qa_actor_id){0}, 0,
                                 named(name, "follownext") ? 1 : -1, &accepted, e);
    }
    if (named(name, "vote") && (named(arg, "yes") || named(arg, "y") ||
                                 named(arg, "no") || named(arg, "n"))) {
        *handled = true;
        return qa_modes_vote_cast(m, id, actor, 0, named(arg, "yes") || named(arg, "y"), e);
    }
    if (named(name, "ghost")) {
        int32_t code;
        *handled = true;
        return !integer_argument(arg, &code) || code < 0 ||
               qa_modes_ghost_rejoin(m, id, actor, (uint32_t)code, e);
    }
    if (named(name, "impulse") && v->value.rules.source == QA_MODE_THREEWAVE) {
        int32_t impulse;
        if (!integer_argument(arg, &impulse))
            return true;
        if (impulse != 20 && impulse != 21 && impulse != 22 && impulse != 25 &&
            impulse != 100 && impulse != 101 && impulse != 102 && impulse != 103 &&
            impulse != 104 && !(p->player.spectator && impulse >= 1 && impulse <= 3))
            return true;
        if (p->player.spectator && impulse >= 20 && impulse <= 22) {
            *handled = true;
            return true;
        }
        qa_mode_controls controls = {.impulse = impulse};
        if (m->options.hooks.player_view)
            (void)m->options.hooks.player_view(m->options.hooks.context, actor, &controls.view_angles);
        bool consumed;
        bool ok = qa_modes_player_controls(m, id, actor, &controls, &consumed, e);
        *handled = consumed || (impulse >= 20 && impulse <= 22);
        return ok;
    }
    return true;
}

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
