#include "internal.h"
#include "qa/console.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>

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
static bool q3_message(qa_modes *m, mode_instance *v, qa_actor_id actor,
                         const char *text, qa_error *e) {
    if (!mode_live(m, actor)) return true;
    qa_mode_event event = {.kind = QA_MODE_MESSAGE, .mode = v->id, .actor = actor,
                            .time_ns = v->value.time_ns};
    if (!qa_builtin_resource(&m->options.services, text, &event.text, e)) return false;
    return !m->options.hooks.event ||
           MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e));
}
static void q3_argument(const qa_command_invocation *command, size_t index, char out[1024]) {
    const char *value = index < command->argc ? command->argv[index] : "";
    size_t count = strlen(value);
    if (count > 1023) count = 1023;
    memcpy(out, value, count);
    out[count] = 0;
}
static int32_t q3_integer(const char *text) {
    while (*text && (unsigned char)*text <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + (uint32_t)(*text++ - '0');
    if (negative) value = 0u - value;
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static void q3_name(const char *text, bool team_vote, char out[1024]) {
    size_t used = 0, maximum = team_vote ? 35 : 1023;
    for (size_t i = 0; i < maximum && text[i]; ++i) {
        unsigned char value = (unsigned char)text[i];
        if ((!team_vote && value == 27) ||
            (team_vote && value == '^' && i + 1 < maximum && text[i + 1] && text[i + 1] != '^')) {
            if (i + 1 < maximum && text[i + 1]) ++i;
        } else if (value >= 32 && value < (team_vote ? 127 : 128))
            out[used++] = value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : (char)value;
    }
    out[used] = 0;
}
static qa_actor_id q3_client(qa_modes *m, mode_instance *v, const char *text,
                               bool numeric, bool team_vote, qa_team_id team) {
    int32_t slot = numeric ? q3_integer(text) : -1;
    qa_actor_id matched = {0};
    uint32_t matched_slot = UINT32_MAX;
    char wanted[1024];
    q3_name(text, team_vote, wanted);
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.ids[i];
        mode_player *player = mode_player_get(m, actor);
        if (!player || !mode_member_get(m, v, actor) ||
            (!player->value.connected && !(team_vote && player->value.connecting)) ||
            (!team_vote && player->value.connecting))
            continue;
        qa_builtin_player_info info;
        if (!m->options.services.player_info ||
            !m->options.services.player_info(m->options.services.context, actor, &info) ||
            !mode_player_get(m, actor) || !mode_member_get(m, v, actor))
            continue;
        if (numeric) {
            if (slot >= 0 && info.slot == (uint32_t)slot) return actor;
        } else {
            char name[1024];
            q3_name(info.name ? info.name : "", team_vote, name);
            qa_team_id own;
            if (strcmp(name, wanted) ||
                (team && (!qa_modes_team(m, v->id, actor, &own, NULL) || own != team))) continue;
            if (!matched.registry || info.slot < matched_slot) {
                matched = actor;
                matched_slot = info.slot;
            }
        }
    }
    return mode_member_get(m, v, matched) ? matched : (qa_actor_id){0};
}
static bool q3_ballot(qa_modes *m, mode_instance *v, qa_actor_id actor,
                        const char *argument, bool team_vote, qa_error *e) {
    qa_team_id team = 0;
    if (team_vote) {
        if (!qa_modes_team(m, v->id, actor, &team, e)) return false;
        if (mode_team_index(v, team) < 0) return true;
    }
    bool yes = argument[0] == 'y' ||
               (argument[0] && (argument[1] == 'Y' || argument[1] == '1'));
    return qa_modes_vote_cast(m, v->id, actor, team, yes, e) &&
           q3_message(m, v, actor, team_vote ? "Team vote cast.\n" : "Vote cast.\n", e);
}
static bool q3_call_vote(qa_modes *m, mode_instance *v, qa_actor_id actor,
                           const qa_command_invocation *command, bool team_vote, qa_error *e) {
    char key[1024], parameter[1024];
    q3_argument(command, 1, key);
    q3_argument(command, 2, parameter);
    if (team_vote) {
        size_t used = 0;
        parameter[0] = 0;
        for (size_t i = 2; i < command->argc; ++i) {
            if (i > 2 && used < 1023) parameter[used++] = ' ';
            const char *part = command->argv[i];
            while (*part && used < 1023) parameter[used++] = *part++;
            parameter[used] = 0;
            if (used == 1023) break;
        }
    }
    if (strchr(key, ';') || strchr(parameter, ';'))
        return q3_message(m, v, actor, "Invalid vote string.\n", e);
    qa_match_intent intent = {.mode = v->id};
    bool raw_command = false;
    int32_t game_type = 0;
    qa_team_id team = 0;
    if (team_vote) {
        if (!qa_modes_team(m, v->id, actor, &team, e)) return false;
        if (mode_team_index(v, team) < 0) return true;
        if (!named(key, "leader"))
            return q3_message(m, v, actor, "Team vote commands are: leader <player>.\n", e);
        intent.kind = QA_MATCH_TEAM_LEADER;
        intent.team = team;
        size_t digits = 0;
        while (digits < 3 && parameter[digits] >= '0' && parameter[digits] <= '9') ++digits;
        bool numeric = digits >= 3 || !parameter[digits];
        intent.actor = !*parameter ? actor : q3_client(m, v, parameter, numeric, true, numeric ? 0 : team);
        if (!intent.actor.registry)
            return q3_message(m, v, actor, "Invalid player for team vote.\n", e);
    } else if (named(key, "map_restart")) {
        intent.kind = QA_MATCH_RESTART_MAP;
        raw_command = true;
    }
    else if (named(key, "nextmap")) intent.kind = QA_MATCH_NEXT_MAP;
    else if (named(key, "map")) {
        intent.kind = QA_MATCH_SELECTED_MAP;
        if (!*parameter) return q3_message(m, v, actor, "Invalid vote map.\n", e);
        if (!qa_builtin_resource(&m->options.services, parameter, &intent.map, e)) return false;
    } else if (named(key, "g_gametype")) {
        static const qa_mode_kind kinds[] = {QA_MODE_FFA, QA_MODE_DUEL, QA_MODE_SINGLE_PLAYER,
            QA_MODE_TEAM_DEATHMATCH, QA_MODE_CTF, QA_MODE_ONE_FLAG, QA_MODE_OVERLOAD, QA_MODE_HARVESTER};
        int32_t type = q3_integer(parameter);
        if (type < 0 || type >= (int32_t)(sizeof(kinds) / sizeof(*kinds)) || type == 2)
            return q3_message(m, v, actor, "Invalid gametype.\n", e);
        intent.kind = QA_MATCH_GAME_TYPE;
        intent.game_type = kinds[type];
        game_type = type;
        raw_command = true;
    } else if (named(key, "kick") || named(key, "clientkick")) {
        intent.kind = QA_MATCH_KICK_PLAYER;
        intent.actor = q3_client(m, v, parameter,
            named(key, "clientkick") || (parameter[0] >= '0' && parameter[0] <= '9'), false, 0);
        if (!intent.actor.registry) return q3_message(m, v, actor, "Invalid player for kick vote.\n", e);
    } else if (named(key, "g_dowarmup") || named(key, "timelimit") || named(key, "fraglimit")) {
        intent.kind = named(key, "g_dowarmup") ? QA_MATCH_WARMUP
                      : named(key, "timelimit") ? QA_MATCH_TIME_LIMIT : QA_MATCH_FRAG_LIMIT;
        raw_command = true;
    } else return q3_message(m, v, actor,
        "Vote commands are: map_restart, nextmap, map <mapname>, g_gametype <n>, kick <player>, clientkick <clientnum>, g_doWarmup, timelimit <time>, fraglimit <frags>.\n", e);
    if (raw_command) {
        char script[sizeof(key) + sizeof(parameter) + 2];
        if (intent.kind == QA_MATCH_GAME_TYPE)
            snprintf(script, sizeof(script), "%s %" PRId32, key, game_type);
        else
            snprintf(script, sizeof(script), "%s \"%s\"", key, parameter);
        if (!qa_builtin_resource(&m->options.services, script, &intent.source_command, e)) return false;
    }
    return qa_modes_vote_start(m, v->id, actor, team, &intent, e);
}
static bool console_command(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                               const qa_command_invocation *command, bool *handled,
                               qa_error *e) {
    if (!m || !command || !handled || !command->argc || !command->argv || !command->argv[0])
        return mode_fail(e, "invalid mode console invocation");
    *handled = false;
    for (size_t i = 0; i < command->argc; ++i)
        if (!command->argv[i]) return mode_fail(e, "missing mode command token");
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    if (!v || !p)
        return true;
    const char *name = command->argv[0];
    const char *arg = command->argc > 1 ? command->argv[1] : "";
    if (!arg)
        return mode_fail(e, "missing mode command argument");
    bool accepted;
    if (v->value.rules.source >= QA_MODE_Q3) {
        if (named(name, "follow")) {
            *handled = true;
            if (command->argc != 2)
                return p->player.q3_spectator_state != QA_MODE_Q3_SPECTATOR_FOLLOW ||
                       qa_modes_follow(m, id, actor, (qa_actor_id){0}, 0, 0, &accepted, e);
            char text[1024];
            q3_argument(command, 1, text);
            qa_actor_id target = q3_client(m, v, text, text[0] >= '0' && text[0] <= '9', false, 0);
            return !target.registry ? q3_message(m, v, actor, "Invalid follow target.\n", e)
                                    : qa_modes_follow(m, id, actor, target, 0, 0, &accepted, e);
        }
        if (named(name, "callvote") || named(name, "callteamvote")) {
            *handled = true;
            return q3_call_vote(m, v, actor, command, named(name, "callteamvote"), e);
        }
        if (named(name, "vote") || named(name, "teamvote")) {
            *handled = true;
            return q3_ballot(m, v, actor, arg, named(name, "teamvote"), e);
        }
    }
    if (named(name, "team") || named(name, "join")) {
        *handled = true;
        if (!*arg && named(name, "team")) {
            qa_team_id current;
            if (!qa_modes_team(m, id, actor, &current, e))
                return false;
            return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0},
                                current, 0, 0, e);
        }
        bool q3 = v->value.rules.source >= QA_MODE_Q3;
        bool score_view = named(arg, "scoreboard") || (q3 && named(arg, "score"));
        int follow_slot = q3 && named(arg, "follow1") ? 1 : q3 && named(arg, "follow2") ? 2 : 0;
        bool spectator = named(arg, "spectator") || named(arg, "s") || score_view || follow_slot;
        bool automatic = !*arg || named(arg, "auto");
        qa_team_id team = named(arg, "red") || named(arg, "r") || named(arg, "1")
                             ? v->value.rules.teams[0]
                         : named(arg, "blue") || named(arg, "b") || named(arg, "2")
                             ? v->value.rules.teams[1] : 0;
        if (!spectator && !automatic && !team && !named(arg, "free") && !named(arg, "f"))
            return true;
        bool ok = qa_modes_request_team(m, id, actor, team, spectator, automatic, &accepted, e);
        if (ok && accepted && follow_slot)
            ok = qa_modes_follow(m, id, actor, (qa_actor_id){0}, follow_slot, 0, &accepted, e);
        if (ok && accepted && score_view) {
            mode_member *current = mode_member_get(m, v, actor);
            if (q3 && current) {
                current->player.q3_spectator_state = QA_MODE_Q3_SPECTATOR_SCOREBOARD;
            }
            ok = qa_modes_scoreboard(m, id, actor, true, e);
        }
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

bool qa_modes_console_command(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                               const qa_command_invocation *command, bool *handled, qa_error *e) {
    if (!m || m->callback_depth == UINT_MAX) return mode_fail(e, "invalid mode command boundary");
    return MODE_CALLBACK(m, console_command(m, id, actor, command, handled, e));
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
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, (int32_t)action,
                      QA_MATCH_ADMIN, e);
}
