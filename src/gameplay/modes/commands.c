#include "internal.h"
#include "qa/text.h"
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
    int32_t slot = numeric ? qa_parse_decimal_i32(text,QA_DECIMAL_CONTROL_WRAP) : -1;
    qa_actor_id matched = {0};
    uint32_t matched_slot = UINT32_MAX;
    char wanted[1024];
    q3_name(text, team_vote, wanted);
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.actors[i];
        mode_player *player = mode_player_get(m, actor);
        if (!player || !mode_member_get(m, v, actor) ||
            (!player->connected && !(team_vote && player->connecting)) ||
            (!team_vote && player->connecting))
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
        int32_t type = qa_parse_decimal_i32(parameter,QA_DECIMAL_CONTROL_WRAP);
        if (type < 0 || type > QA_GAME_TYPE_HARVESTER || type == QA_GAME_TYPE_ARENA_SINGLE_PLAYER)
            return q3_message(m, v, actor, "Invalid gametype.\n", e);
        intent.kind = QA_MATCH_GAME_TYPE;
        intent.game_type = qa_mode_from_game_type(type);
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
bool mode_ctf_named_message(qa_modes *m, mode_instance *v, qa_actor_id subject,
    mode_name text, qa_string_id map, int32_t level, qa_error *e) {
    mode_player *player = mode_player_get(m, subject);
    if (!player)
        return true;
    qa_builtin_message_arg args[] = {
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = player->identity->name},
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = map}};
    return mode_message_level(m, v, (qa_actor_id){0}, m->runtime_names[text],
                               args, map ? 2 : 1, level, e);
}
bool mode_ctf_admin_grant(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || member->admin)
        return true;
    member->admin = true;
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0},
                       0, 1, QA_MATCH_ADMIN, e) &&
           mode_ctf_named_message(m, v, actor, MODE_NAME_CTF_ADMIN_GRANTED, 0, 2, e) &&
           mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_ADMIN_HELP], NULL, 0, e);
}
static bool ctf_warp_list(qa_modes *m, mode_instance *v, qa_actor_id actor,
                            const char *list, qa_error *e) {
    qa_string_id text;
    if (!qa_builtin_resource(&m->options.services, list, &text, e))
        return false;
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = text};
    return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_WARP_LEVELS],
                         &argument, 1, e);
}
static bool ctf_warp_allowed(const char *list, const char *map) {
    qa_bytes wanted = {(const uint8_t *)map, strlen(map)};
    while (*list) {
        list += strspn(list, " \t\n\r");
        size_t size = strcspn(list, " \t\n\r");
        if (size && qa_text_bytes_equal((qa_bytes){(const uint8_t *)list, size}, wanted, true))
            return true;
        list += size;
    }
    return false;
}
static qa_actor_id ctf_report_player_next(qa_modes *m, mode_instance *v,
    uint64_t *cursor, qa_builtin_player_info *out) {
    qa_actor_id selected = {0};
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id candidate = m->players_order.actors[ordinal];
        mode_player *player = mode_player_get(m, candidate);
        qa_builtin_player_info info;
        if (!player || !player->connected || !mode_member_get(m, v, candidate) ||
            !m->options.services.player_info ||
            !m->options.services.player_info(m->options.services.context, candidate, &info) ||
            !info.connected || info.slot < *cursor ||
            (selected.registry && info.slot >= out->slot))
            continue;
        selected = candidate;
        *out = info;
    }
    if (selected.registry)
        *cursor = (uint64_t)out->slot + 1;
    return selected;
}
static bool ctf_report(qa_modes *m, mode_instance *v, qa_actor_id actor,
    const char *text, qa_error *e) {
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_TEXT,
        .value.literal = text};
    return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_REPORT], &argument, 1, e);
}
static bool ctf_stats(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    char text[1400] = "";
    size_t length = 0;
    if (v->value.phase == QA_MODE_SETUP) {
        uint64_t cursor = 0;
        qa_builtin_player_info info;
        qa_actor_id candidate;
        while ((candidate = ctf_report_player_next(m, v, &cursor, &info)).registry) {
            mode_member *member = mode_member_get(m, v, candidate);
            qa_team_id team;
            if (!member || member->player.ready)
                continue;
            if (!qa_modes_team(m, v->id, candidate, &team, e))
                return false;
            if (mode_team_index(v, team) < 0)
                continue;
            int written = snprintf(text + length, sizeof(text) - length,
                "%s is not ready.\n", info.name ? info.name : "");
            if (written >= 0 && (size_t)written < sizeof(text) - 50 - length)
                length += (size_t)written;
            else
                text[length] = 0;
        }
    }
    if (!mode_update_ghosts(m, v, e))
        return false;
    bool available = false;
    for (uint32_t i = 0; i < m->actor_capacity; ++i)
        if (v->ghosts[i].code && v->ghosts[i].actor.registry) {
            available = true;
            break;
        }
    if (!available) {
        snprintf(text + length, sizeof(text) - length, "No statistics available.\n");
        return ctf_report(m, v, actor, text, e);
    }
    static const char heading[] = "  #|Name            |Score|Kills|Death|BasDf|CarDf|Effcy|\n";
    if (length + sizeof(heading) - 1 > sizeof(text) - 50) {
        snprintf(text + length, sizeof(text) - length, "And more...\n");
        return ctf_report(m, v, actor, text, e);
    }
    memcpy(text + length, heading, sizeof(heading));
    length += sizeof(heading) - 1;
    qa_strings *strings = qa_session_strings(m->options.services.session);
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        const mode_ghost *ghost = &v->ghosts[i];
        const char *name = qa_strings_cstr(strings, ghost->name);
        if (!name || !*name)
            continue;
        int64_t total = (int64_t)ghost->stats.kills + ghost->stats.deaths;
        int32_t efficiency = total ? (int32_t)((int64_t)ghost->stats.kills * 100 / total) : 50;
        int written = snprintf(text + length, sizeof(text) - length,
            "%3" PRIu32 "|%-16.16s|%5" PRId32 "|%5" PRId32 "|%5" PRId32
            "|%5" PRId32 "|%5" PRId32 "|%4" PRId32 "%%|\n",
            ghost->number, name, ghost->score, ghost->stats.kills, ghost->stats.deaths,
            ghost->stats.defenses, ghost->stats.carrier_defenses, efficiency);
        if (written < 0 || (size_t)written > sizeof(text) - 50 - length) {
            snprintf(text + length, sizeof(text) - length, "And more...\n");
            break;
        }
        length += (size_t)written;
    }
    return ctf_report(m, v, actor, text, e);
}
static bool ctf_player_list(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    char text[1400] = "";
    size_t length = 0;
    uint64_t cursor = 0;
    qa_builtin_player_info info;
    qa_actor_id candidate;
    while ((candidate = ctf_report_player_next(m, v, &cursor, &info)).registry) {
        mode_member *member = mode_member_get(m, v, candidate);
        int32_t score;
        if (!member)
            continue;
        if (!qa_modes_score(m, v->id, candidate, &score, e))
            return false;
        uint64_t seconds = v->value.time_ns > info.entered_ns
            ? (v->value.time_ns - info.entered_ns) / MODE_SECOND : 0;
        bool readiness = v->value.phase == QA_MODE_SETUP || v->value.phase == QA_MODE_COUNTDOWN;
        int written = snprintf(text + length, sizeof(text) - length,
            "%3" PRIu64 " %-16.16s %02" PRIu64 ":%02" PRIu64 " %4" PRId32 " %3" PRId32 "%s%s\n",
            (uint64_t)info.slot + 1, info.name ? info.name : "", seconds / 60, seconds % 60,
            info.ping, score, readiness ? (member->player.ready ? " (ready)" : " (notready)") : "",
            member->admin ? " (admin)" : "");
        if (written < 0 || (size_t)written > sizeof(text) - 50 - length) {
            snprintf(text + length, sizeof(text) - length, "And more...\n");
            break;
        }
        length += (size_t)written;
    }
    return ctf_report(m, v, actor, text, e);
}
static bool ctf_boot(qa_modes *m, mode_instance *v, qa_actor_id actor,
    const qa_command_invocation *command, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    mode_name message;
    if (!member || !member->admin)
        message = MODE_NAME_CTF_BOOT_NOT_ADMIN;
    else if (command->argc < 2)
        message = MODE_NAME_CTF_BOOT_USAGE;
    else {
        qa_mode_source_settings settings = {0};
        if (!m->options.hooks.source_settings ||
            !m->options.hooks.source_settings(m->options.hooks.context, v->id, &settings))
            return true;
        int32_t number = qa_parse_decimal_i32(command->argv[1], QA_DECIMAL_CONTROL_WRAP);
        if (number < 1 || (uint32_t)number > settings.max_clients)
            message = MODE_NAME_CTF_BOOT_INVALID;
        else {
            uint64_t cursor = (uint32_t)number - 1;
            qa_builtin_player_info info;
            qa_actor_id target = ctf_report_player_next(m, v, &cursor, &info);
            if (!target.registry || info.slot != (uint32_t)number - 1)
                message = MODE_NAME_CTF_BOOT_DISCONNECTED;
            else
                return !m->options.hooks.disconnect ||
                    MODE_CALLBACK(m, m->options.hooks.disconnect(m->options.hooks.context, target, e));
        }
    }
    return mode_message(m, v, actor, m->runtime_names[message], NULL, 0, e);
}
static bool ctf_command(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          const qa_command_invocation *command, bool *handled, qa_error *e) {
    const char *name = command->argv[0];
    if (named(name, "stats") || named(name, "playerlist") || named(name, "boot")) {
        *handled = true;
        return named(name, "stats") ? ctf_stats(m, v, actor, e) :
            named(name, "playerlist") ? ctf_player_list(m, v, actor, e) :
            ctf_boot(m, v, actor, command, e);
    }
    if (named(name, "id") && v->value.rules.enabled) {
        mode_member *member = mode_member_get(m, v, actor);
        if (!member)
            return true;
        *handled = true;
        member->player.ctf_id_view = !member->player.ctf_id_view;
        mode_name message = member->player.ctf_id_view
            ? MODE_NAME_CTF_ID_ENABLED : MODE_NAME_CTF_ID_DISABLED;
        return mode_message(m, v, actor, m->runtime_names[message], NULL, 0, e);
    }
    if (named(name, "drop") && command->argc == 2 && named(command->argv[1], "tech") &&
        v->value.rules.enabled) {
        bool rerelease = v->value.rules.ruleset == QA_RULESET_Q2_RERELEASE;
        if (rerelease && !mode_alive(m, actor)) {
            *handled = true;
            return true;
        }
        mode_member *member = mode_member_get(m, v, actor);
        mode_object *tech = member ? mode_object_get(m, member->relic) : NULL;
        if (!tech) {
            *handled = rerelease;
            return true;
        }
        qa_item_id item;
        if (!mode_inventory_item(m, v, tech->spec.item, &item, e))
            return false;
        *handled = true;
        return qa_inventory_item_action(m->options.services.inventory, actor, item, QA_ITEM_DROP, e);
    }
    if (named(name, "yes") || named(name, "no")) {
        *handled = true;
        return qa_modes_vote_cast(m, v->id, actor, 0, named(name, "yes"), e);
    }
    if (named(name, "ctf-menu") && command->argc > 1 && named(command->argv[1], "match")) {
        *handled = true;
        qa_match_intent intent = {.kind = QA_MATCH_CANCEL, .actor = actor};
        return qa_modes_vote_start(m, v->id, actor, 0, &intent, e);
    }
    if (!named(name, "admin") && !named(name, "warp"))
        return true;
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    *handled = true;
    if (named(name, "admin") && member->admin)
        return mode_ui_open_admin(m, v, actor, e);
    qa_mode_source_settings settings = {0};
    if (!m->options.hooks.source_settings ||
        !m->options.hooks.source_settings(m->options.hooks.context, v->id, &settings))
        return true;
    if (named(name, "admin")) {
        if (command->argc > 1 && settings.admin_password && *settings.admin_password &&
            !strcmp(settings.admin_password, command->argv[1]))
            return mode_ctf_admin_grant(m, v, actor, e) && mode_ui_open_admin(m, v, actor, e);
        qa_match_intent intent = {.kind = QA_MATCH_ADMIN, .actor = actor};
        return qa_modes_vote_start(m, v->id, actor, 0, &intent, e);
    }
    const char *list = settings.warp_list ? settings.warp_list : "";
    if (command->argc < 2)
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_WARP_USAGE], NULL, 0, e) &&
               ctf_warp_list(m, v, actor, list, e);
    qa_string_id map;
    if (!ctf_warp_allowed(list, command->argv[1]))
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_WARP_UNKNOWN], NULL, 0, e) &&
               ctf_warp_list(m, v, actor, list, e);
    if (!qa_builtin_resource(&m->options.services, command->argv[1], &map, e))
        return false;
    if (!m->options.hooks.map_allowed ||
        !MODE_CALLBACK(m, m->options.hooks.map_allowed(m->options.hooks.context, v->id, map)))
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_WARP_UNKNOWN], NULL, 0, e) &&
               ctf_warp_list(m, v, actor, list, e);
    if (member->admin)
        return mode_ctf_named_message(m, v, actor, MODE_NAME_CTF_WARPING, map, 2, e) &&
               mode_intent(m, v, QA_MATCH_SELECTED_MAP, actor, 0, map, e);
    qa_match_intent intent = {.kind = QA_MATCH_SELECTED_MAP, .actor = actor, .map = map};
    return qa_modes_vote_start(m, v->id, actor, 0, &intent, e);
}
bool mode_radio_set(qa_modes *m, mode_instance *v, qa_actor_id actor,
                     unsigned selection, qa_error *e) {
    static const int32_t flags[] = {0, 32, 16, 48};
    static const mode_name messages[] = {MODE_NAME_LMCTF_RADIO_OFF,
        MODE_NAME_LMCTF_RADIO_SOUND, MODE_NAME_LMCTF_RADIO_TEXT, MODE_NAME_LMCTF_RADIO_BOTH};
    if (selection >= sizeof(flags) / sizeof(*flags))
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_RADIO_USAGE],
                            NULL, 0, e);
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    member->extra_flags = (member->extra_flags & ~48) | flags[selection];
    return mode_message(m, v, actor, m->runtime_names[messages[selection]], NULL, 0, e);
}
static bool lmctf_command(qa_modes *m, mode_instance *v, qa_actor_id actor,
                           const qa_command_invocation *command, bool *handled, qa_error *e) {
    const char *name = command->argv[0];
    if (named(name, "radio")) {
        *handled = true;
        const char *argument = command->args_text ? command->args_text : "";
        unsigned selection = 4;
        if (*argument == '0' || !strcmp(argument, "off")) selection = 0;
        else if (*argument == '1' || !strcmp(argument, "on")) selection = 1;
        else if (*argument == '2' || !strcmp(argument, "text")) selection = 2;
        else if (*argument == '3' || !strcmp(argument, "both")) selection = 3;
        return mode_radio_set(m, v, actor, selection, e);
    }
    if (named(name, "gotomap") || named(name, "match")) {
        *handled = true;
        bool match = named(name, "match");
        mode_member *member = mode_member_get(m, v, actor);
        if (!member || !(member->extra_flags & 2))
            return mode_message(m, v, actor, m->runtime_names[match ?
                MODE_NAME_LMCTF_MATCH_NOT_REFEREE : MODE_NAME_LMCTF_GOTOMAP_NOT_REFEREE],
                NULL, 0, e);
        const char *argument = command->args_text ? command->args_text : "";
        if (!*argument)
            return !match || mode_message(m, v, actor,
                m->runtime_names[MODE_NAME_LMCTF_MATCH_USAGE], NULL, 0, e);
        qa_strings *strings = qa_session_strings(m->options.services.session);
        qa_bytes text = {(const uint8_t *)argument, strlen(argument)};
        qa_string_id map = match ? qa_strings_find(strings, text) :
            qa_strings_find_ascii_lower(strings, text);
        bool accepted;
        if (!qa_modes_admin(m, v->id, actor, match ? QA_MODE_ADMIN_MATCH_MAP :
                            QA_MODE_ADMIN_MAP, map, &accepted, e))
            return false;
        if (accepted)
            return true;
        char lowercase[1024];
        if (!match) {
            size_t length = text.size < sizeof(lowercase) - 1 ?
                text.size : sizeof(lowercase) - 1;
            for (size_t i = 0; i < length; ++i)
                lowercase[i] = (char)qa_text_ascii_lower(text.data[i]);
            lowercase[length] = 0;
            argument = lowercase;
        }
        qa_builtin_message_arg value = {.kind = QA_BUILTIN_MESSAGE_TEXT,
            .value.literal = argument};
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_MAP_NOT_LISTED],
                             &value, 1, e);
    }
    if (named(name, "observe_red") || named(name, "observe_blue")) {
        *handled = true;
        bool accepted;
        return qa_modes_observe(m, v->id, actor,
            v->value.rules.teams[named(name, "observe_blue") ? 1 : 0], &accepted, e);
    }
    if (named(name, "referee")) {
        qa_mode_source_settings settings;
        if (!m->options.hooks.source_settings ||
            !m->options.hooks.source_settings(m->options.hooks.context, v->id, &settings))
            return true;
        *handled = true;
        const char *password = command->args_text ? command->args_text : "";
        unsigned level;
        mode_name message;
        if (!strcmp(settings.rcon_password, password)) {
            if (!*settings.rcon_password)
                return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_RCON_OFF],
                                    NULL, 0, e);
            level = 2;
            message = MODE_NAME_LMCTF_RCON_GRANTED;
        } else if (!strcmp(settings.referee_password, password)) {
            if (!*settings.referee_password)
                return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_REFEREE_OFF],
                                    NULL, 0, e);
            level = 1;
            message = MODE_NAME_LMCTF_REFEREE_GRANTED;
        } else {
            level = 0;
            message = MODE_NAME_LMCTF_REFEREE_BAD_PASSWORD;
        }
        return qa_modes_referee(m, v->id, actor, level, e) &&
               mode_message(m, v, actor, m->runtime_names[message], NULL, 0, e);
    }
    qa_mode_admin_action action;
    if (named(name, "pausematch") || named(name, "unpausematch"))
        action = QA_MODE_ADMIN_PAUSE_ANY;
    else if (named(name, "pause_match"))
        action = QA_MODE_ADMIN_PAUSE;
    else if (named(name, "lock") || named(name, "unlock"))
        action = QA_MODE_ADMIN_LOCK;
    else if (named(name, "startmatch"))
        action = QA_MODE_ADMIN_START;
    else if (named(name, "stopmatch"))
        action = QA_MODE_ADMIN_STOP;
    else
        return true;
    *handled = true;
    bool accepted;
    return qa_modes_admin(m, v->id, actor, action, 0, &accepted, e);
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
    if (v->value.rules.source == QA_MODE_Q2_CTF || v->value.rules.source == QA_MODE_LMCTF) {
        if (!mode_ui_command(m, v, actor, command, handled, e))
            return false;
        if (*handled || !mode_member_get(m, v, actor))
            return true;
    }
    if (v->value.rules.source == QA_MODE_Q2_CTF) {
        if (!ctf_command(m, v, actor, command, handled, e))
            return false;
        if (*handled)
            return true;
    }
    if (v->value.rules.source == QA_MODE_LMCTF) {
        if (!lmctf_command(m, v, actor, command, handled, e))
            return false;
        if (*handled)
            return true;
    }
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
            if (v->value.rules.source == QA_MODE_LMCTF) {
                int index = mode_team_index(v, current);
                qa_builtin_message_arg argument = index >= 0 && index < 2 ?
                    (qa_builtin_message_arg){.kind = QA_BUILTIN_MESSAGE_STRING,
                        .value.text = m->runtime_names[index ? MODE_NAME_LMCTF_BLUE :
                            MODE_NAME_LMCTF_RED]} :
                    (qa_builtin_message_arg){.kind = QA_BUILTIN_MESSAGE_NUMBER,
                        .value.number = current};
                return mode_message(m, v, actor, m->runtime_names[index >= 0 && index < 2 ?
                    MODE_NAME_LMCTF_CURRENT_TEAM : MODE_NAME_LMCTF_CURRENT_TEAM_NUMBER],
                    &argument, 1, e) && mode_message(m, v, actor,
                        m->runtime_names[MODE_NAME_LMCTF_TEAM_USAGE], NULL, 0, e);
            }
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
    if (named(name, "spectator") || named(name, "observe") || named(name, "observer")) {
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
    if (named(name, "ghost") && v->value.rules.source == QA_MODE_Q2_CTF) {
        *handled = true;
        if (command->argc < 2)
            return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_GHOST_USAGE],
                                NULL, 0, e);
        return qa_modes_ghost_rejoin(m, id, actor,
            (uint32_t)qa_parse_decimal_i32(arg, QA_DECIMAL_CONTROL_WRAP), e);
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
    if (action != QA_MODE_ADMIN_PAUSE_ANY && !admin) {
        if (v->value.rules.source == QA_MODE_LMCTF &&
            (action == QA_MODE_ADMIN_LOCK || action == QA_MODE_ADMIN_START ||
             action == QA_MODE_ADMIN_STOP))
            return mode_message(m, v, actor, m->runtime_names[action == QA_MODE_ADMIN_LOCK ?
                MODE_NAME_LMCTF_LOCK_DENIED : MODE_NAME_LMCTF_ADMIN_DENIED], NULL, 0, e);
        return true;
    }
    if (action == QA_MODE_ADMIN_MAP || action == QA_MODE_ADMIN_MATCH_MAP) {
        if (!map || !m->options.hooks.map_allowed ||
            !m->options.hooks.map_allowed(m->options.hooks.context, id, map))
            return true;
        *accepted = true;
        if (v->value.rules.source == QA_MODE_LMCTF && action == QA_MODE_ADMIN_MATCH_MAP &&
            !mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_MATCH_STARTING],
                           NULL, 0, e))
            return false;
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
        if (v->value.rules.source == QA_MODE_LMCTF) {
            qa_builtin_event message = {.kind = QA_BUILTIN_CENTERPRINT, .flags = 1,
                .text = m->runtime_names[v->value.rules.paused ? MODE_NAME_LMCTF_GAME_PAUSED :
                    MODE_NAME_LMCTF_GAME_UNPAUSED]};
            if (!mode_builtin_emit(m, v, &message, e))
                return false;
        }
        break;
    case QA_MODE_ADMIN_LOCK:
        v->value.rules.match_lock = !v->value.rules.match_lock;
        if (v->value.rules.source == QA_MODE_LMCTF &&
            !mode_message(m, v, (qa_actor_id){0}, m->runtime_names[v->value.rules.match_lock ?
                MODE_NAME_LMCTF_TEAMS_LOCKED : MODE_NAME_LMCTF_TEAMS_UNLOCKED], NULL, 0, e))
            return false;
        break;
    case QA_MODE_ADMIN_START:
        if (v->value.phase != QA_MODE_WAITING && v->value.phase != QA_MODE_SETUP) {
            *accepted = false;
            if (v->value.rules.source == QA_MODE_LMCTF)
                return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_MATCH_RUNNING],
                                    NULL, 0, e);
            return true;
        }
        return qa_modes_start(m, id, e);
    case QA_MODE_ADMIN_STOP:
        if (v->value.phase == QA_MODE_WAITING) {
            *accepted = false;
            if (v->value.rules.source == QA_MODE_LMCTF)
                return mode_message(m, v, actor, m->runtime_names[MODE_NAME_LMCTF_MATCH_NOT_RUNNING],
                                    NULL, 0, e);
            return true;
        }
        if (!qa_modes_cancel(m, id, e))
            return false;
        if (v->value.rules.source == QA_MODE_LMCTF) {
            mode_player *player = mode_player_get(m, actor);
            if (!player)
                return true;
            qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING,
                .value.text = player->identity->name};
            return mode_message(m, v, (qa_actor_id){0}, m->runtime_names[MODE_NAME_LMCTF_MATCH_STOPPED],
                                &argument, 1, e);
        }
        return true;
    case QA_MODE_ADMIN_MAP:
    case QA_MODE_ADMIN_MATCH_MAP:
        break;
    }
    return mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, 0, (int32_t)action,
                      QA_MATCH_ADMIN, e);
}
