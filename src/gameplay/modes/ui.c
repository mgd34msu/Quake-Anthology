#include "internal.h"
#include "qa/console.h"
#include "qa/text.h"
#include <inttypes.h>
#include <stdio.h>

typedef enum menu_action {
    MENU_NONE, MENU_RED, MENU_BLUE, MENU_CHASE, MENU_CREDITS, MENU_BACK, MENU_MATCH,
    MENU_VOTE_SKIP, MENU_VOTE_YES, MENU_VOTE_NO,
    MENU_LM_MAIN, MENU_LM_OBSERVER_MENU, MENU_LM_VOTE, MENU_LM_CHANGE_TEAM, MENU_LM_PLAY,
    MENU_LM_OBSERVE_RED, MENU_LM_OBSERVE_BLUE, MENU_LM_OBSERVE_ALL,
    MENU_ADMIN_SETTINGS, MENU_ADMIN_MATCH, MENU_ADMIN_CANCEL,
    MENU_SETTING_MATCH, MENU_SETTING_SETUP, MENU_SETTING_START, MENU_SETTING_WEAPONS_STAY,
    MENU_SETTING_INSTANT_ITEMS, MENU_SETTING_QUAD_DROP, MENU_SETTING_INSTANT_WEAPONS,
    MENU_SETTING_MATCH_LOCK, MENU_SETTINGS_APPLY, MENU_SETTINGS_CANCEL
} menu_action;
typedef enum menu_align { MENU_LEFT, MENU_CENTER, MENU_RIGHT } menu_align;
typedef struct menu_entry {
    mode_name name;
    menu_align align;
    menu_action action;
} menu_entry;
typedef struct menu_row {
    const char *text;
    menu_align align;
    menu_action action;
} menu_row;
typedef struct score_row {
    qa_actor_id actor;
    qa_string_id name;
    uint32_t slot;
    uint64_t minutes;
    int32_t score, ping, captures;
    uint8_t observer;
    bool carrier;
} score_row;
enum { SCORE_CTF_TEAM_ROWS = 16, SCORE_TEAM_ROWS = 21, SCORE_BYTES = 1000,
       SCORE_LMCTF_BYTES = 1024, SCORE_SPECTATORS = SCORE_BYTES / 12 };

#define MENU_ENTRY(name, align, action) {MODE_NAME_CTF_MENU_##name, MENU_##align, MENU_##action}
#define MENU_BLANK {MODE_NAME_COUNT, MENU_CENTER, MENU_NONE}
static const menu_entry join_entries[] = {
    MENU_ENTRY(GAME, CENTER, NONE), MENU_ENTRY(TITLE, CENTER, NONE),
    MENU_BLANK, MENU_BLANK, MENU_BLANK,
    MENU_ENTRY(RED, LEFT, RED), MENU_BLANK,
    MENU_ENTRY(BLUE, LEFT, BLUE), MENU_BLANK,
    MENU_ENTRY(CHASE, LEFT, CHASE), MENU_ENTRY(CREDITS, LEFT, CREDITS),
    MENU_BLANK, MENU_BLANK,
    MENU_ENTRY(CURSOR, LEFT, NONE), MENU_ENTRY(SELECT, LEFT, NONE),
    MENU_ENTRY(EXIT, LEFT, NONE), MENU_ENTRY(TAB, LEFT, NONE),
    MENU_ENTRY(VERSION, RIGHT, NONE)
};
static const menu_entry credits_entries[] = {
    MENU_ENTRY(GAME, CENTER, NONE), MENU_ENTRY(TITLE, CENTER, NONE), MENU_BLANK,
    MENU_ENTRY(PROGRAMMING, CENTER, NONE), MENU_ENTRY(ZOID, CENTER, NONE),
    MENU_ENTRY(LEVEL_DESIGN, CENTER, NONE), MENU_ENTRY(ANTKOW, CENTER, NONE),
    MENU_ENTRY(WILLITS, CENTER, NONE), MENU_ENTRY(ZOID, CENTER, NONE),
    MENU_ENTRY(ART, CENTER, NONE), MENU_ENTRY(CARMACK_STEED, CENTER, NONE),
    MENU_ENTRY(CLOUD, CENTER, NONE), MENU_ENTRY(SOUND, CENTER, NONE),
    MENU_ENTRY(KLOK, CENTER, NONE), MENU_ENTRY(ORIGINAL_ART, CENTER, NONE),
    MENU_ENTRY(COZZENS, CENTER, NONE), MENU_BLANK, MENU_ENTRY(BACK, LEFT, BACK)
};
static const menu_entry no_chase_entries[] = {
    MENU_ENTRY(GAME, CENTER, NONE), MENU_ENTRY(TITLE, CENTER, NONE),
    MENU_BLANK, MENU_BLANK, MENU_ENTRY(NO_CHASE, LEFT, NONE), MENU_BLANK,
    MENU_ENTRY(BACK, LEFT, BACK)
};
static const menu_entry admin_entries[] = {
    MENU_ENTRY(ADMIN_TITLE, CENTER, NONE), MENU_BLANK,
    MENU_ENTRY(ADMIN_SETTINGS, LEFT, ADMIN_SETTINGS), MENU_BLANK, MENU_BLANK,
    MENU_ENTRY(CANCEL, LEFT, ADMIN_CANCEL), MENU_BLANK
};
static const menu_entry settings_entries[] = {
    MENU_ENTRY(SETTINGS_TITLE, CENTER, NONE), MENU_BLANK,
    MENU_ENTRY(SETTINGS_MATCH_LENGTH, LEFT, SETTING_MATCH),
    MENU_ENTRY(SETTINGS_SETUP_LENGTH, LEFT, SETTING_SETUP),
    MENU_ENTRY(SETTINGS_START_LENGTH, LEFT, SETTING_START),
    MENU_ENTRY(SETTINGS_WEAPONS_STAY, LEFT, SETTING_WEAPONS_STAY),
    MENU_ENTRY(SETTINGS_INSTANT_ITEMS, LEFT, SETTING_INSTANT_ITEMS),
    MENU_ENTRY(SETTINGS_QUAD_DROP, LEFT, SETTING_QUAD_DROP),
    MENU_ENTRY(SETTINGS_INSTANT_WEAPONS, LEFT, SETTING_INSTANT_WEAPONS),
    MENU_ENTRY(SETTINGS_MATCH_LOCK, LEFT, SETTING_MATCH_LOCK), MENU_BLANK,
    MENU_ENTRY(APPLY, LEFT, SETTINGS_APPLY), MENU_ENTRY(CANCEL, LEFT, SETTINGS_CANCEL)
};
static const menu_entry lmctf_vote_entries[] = {
    {MODE_NAME_LMCTF_VOTE_MENU_TITLE, MENU_LEFT, MENU_LM_MAIN},
    {MODE_NAME_LMCTF_VOTE_MENU_SEPARATOR, MENU_LEFT, MENU_NONE},
    {MODE_NAME_LMCTF_VOTE_MENU_SKIP, MENU_LEFT, MENU_VOTE_SKIP},
    MENU_BLANK, MENU_BLANK, MENU_BLANK,
    {MODE_NAME_LMCTF_VOTE_MENU_IDLE, MENU_LEFT, MENU_NONE},
    MENU_BLANK, MENU_BLANK
};
static const menu_entry lmctf_main_entries[] = {
    {MODE_NAME_LMCTF_MENU_TITLE, MENU_LEFT, MENU_NONE},
    {MODE_NAME_LMCTF_MENU_SEPARATOR, MENU_LEFT, MENU_NONE},
    MENU_BLANK,
    {MODE_NAME_LMCTF_MENU_BECOME_OBSERVER, MENU_LEFT, MENU_LM_OBSERVE_ALL},
    {MODE_NAME_LMCTF_MENU_CHANGE_TEAM, MENU_LEFT, MENU_LM_CHANGE_TEAM},
    MENU_BLANK, MENU_BLANK, MENU_BLANK,
    {MODE_NAME_LMCTF_MENU_VOTING, MENU_LEFT, MENU_LM_VOTE}
};
static const menu_entry lmctf_observer_entries[] = {
    MENU_BLANK,
    {MODE_NAME_LMCTF_OBSERVER_MENU_TITLE, MENU_LEFT, MENU_NONE},
    {MODE_NAME_LMCTF_OBSERVER_MENU_SEPARATOR, MENU_LEFT, MENU_NONE},
    MENU_BLANK,
    {MODE_NAME_LMCTF_OBSERVER_MENU_PLAY, MENU_LEFT, MENU_LM_PLAY},
    MENU_BLANK,
    {MODE_NAME_LMCTF_OBSERVER_MENU_RED, MENU_LEFT, MENU_LM_OBSERVE_RED},
    {MODE_NAME_LMCTF_OBSERVER_MENU_BLUE, MENU_LEFT, MENU_LM_OBSERVE_BLUE},
    {MODE_NAME_LMCTF_OBSERVER_MENU_ALL, MENU_LEFT, MENU_LM_OBSERVE_ALL}
};
#undef MENU_ENTRY
#undef MENU_BLANK

static const char *menu_text(qa_modes *m, mode_name name) {
    return name == MODE_NAME_COUNT ? NULL :
        qa_strings_cstr(qa_session_strings(m->options.services.session), m->runtime_names[name]);
}
static bool layout_enabled(const mode_instance *v) {
    return v && v->value.rules.enabled &&
        (v->value.rules.source == QA_MODE_Q2_CTF || v->value.rules.source == QA_MODE_LMCTF);
}
static bool menu_match(const mode_instance *v) {
    return v->value.phase == QA_MODE_COUNTDOWN ||
           (v->value.phase == QA_MODE_PLAYING && v->value.deadline_ns) ||
           v->value.phase >= QA_MODE_EXIT_PENDING;
}
static void menu_team_counts(qa_modes *m, mode_instance *v, size_t counts[2]) {
    counts[0] = counts[1] = 0;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.actors[i];
        mode_player *player = mode_player_get(m, actor);
        qa_team_id team;
        if (!player || !player->connected || !mode_member_get(m, v, actor) ||
            !qa_modes_team(m, v->id, actor, &team, NULL))
            continue;
        int index = mode_team_index(v, team);
        if (index >= 0 && index < 2)
            ++counts[index];
    }
}
static size_t menu_rows(qa_modes *m, mode_instance *v, mode_member *member, qa_mode_menu_page page,
                         menu_row rows[18], char level[33], char counts[2][32],
                         char fields[8][64]) {
    const menu_entry *entries = NULL;
    size_t count = 0;
    switch (page) {
    case QA_MODE_MENU_JOIN:
        entries = join_entries; count = sizeof(join_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_CREDITS:
        entries = credits_entries; count = sizeof(credits_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_NO_CHASE:
        entries = no_chase_entries; count = sizeof(no_chase_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_ADMIN:
        entries = admin_entries; count = sizeof(admin_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_SETTINGS:
        entries = settings_entries; count = sizeof(settings_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_LMCTF_VOTE:
        entries = lmctf_vote_entries;
        count = sizeof(lmctf_vote_entries) / sizeof(*entries);
        break;
    case QA_MODE_MENU_LMCTF_MAIN:
        entries = lmctf_main_entries;
        count = sizeof(lmctf_main_entries) / sizeof(*entries);
        break;
    case QA_MODE_MENU_LMCTF_OBSERVER:
        entries = lmctf_observer_entries;
        count = sizeof(lmctf_observer_entries) / sizeof(*entries);
        break;
    case QA_MODE_MENU_NONE: case QA_MODE_MENU_SCOREBOARD: return 0;
    }
    for (size_t i = 0; i < count; ++i)
        rows[i] = (menu_row){menu_text(m, entries[i].name), entries[i].align, entries[i].action};
    if (page == QA_MODE_MENU_LMCTF_MAIN) {
        if (member->player.spectator) {
            rows[3] = (menu_row){menu_text(m, MODE_NAME_LMCTF_MENU_OBSERVER_OPTIONS),
                                 MENU_LEFT, MENU_LM_OBSERVER_MENU};
            rows[4] = (menu_row){NULL, MENU_LEFT, MENU_NONE};
        }
        if (v->value.rules.flags & 32768u)
            rows[8] = (menu_row){NULL, MENU_LEFT, MENU_NONE};
        return count;
    }
    if (page == QA_MODE_MENU_LMCTF_OBSERVER)
        return count;
    if (page == QA_MODE_MENU_LMCTF_VOTE) {
        if (v->votes[0].active) {
            rows[2] = (menu_row){NULL, MENU_LEFT, MENU_NONE};
            rows[3] = (menu_row){menu_text(m, MODE_NAME_LMCTF_VOTE_MENU_YES),
                                 MENU_LEFT, MENU_VOTE_YES};
            rows[4] = (menu_row){menu_text(m, MODE_NAME_LMCTF_VOTE_MENU_NO),
                                 MENU_LEFT, MENU_VOTE_NO};
            rows[6].text = menu_text(m, MODE_NAME_LMCTF_VOTE_MENU_STARTED);
            rows[8].text = menu_text(m, member->ballots[0] > 0
                ? MODE_NAME_LMCTF_VOTE_MENU_BALLOT_YES : member->ballots[0] < 0
                ? MODE_NAME_LMCTF_VOTE_MENU_BALLOT_NO : MODE_NAME_LMCTF_VOTE_MENU_BALLOT_NONE);
        }
        return count;
    }
    if (page == QA_MODE_MENU_ADMIN) {
        mode_name match = MODE_NAME_COUNT;
        if (v->value.phase == QA_MODE_SETUP)
            match = MODE_NAME_CTF_MENU_ADMIN_FORCE_START;
        else if (v->value.phase == QA_MODE_PLAYING && v->value.deadline_ns)
            match = MODE_NAME_CTF_MENU_ADMIN_CANCEL_MATCH;
        else if (v->value.phase == QA_MODE_PLAYING && v->value.rules.competition)
            match = MODE_NAME_CTF_MENU_ADMIN_SWITCH_MATCH;
        if (match != MODE_NAME_COUNT)
            rows[3] = (menu_row){menu_text(m, match), MENU_LEFT, MENU_ADMIN_MATCH};
        return count;
    }
    if (page == QA_MODE_MENU_SETTINGS) {
        const qa_mode_ctf_settings *settings = &member->menu.settings;
        const int32_t lengths[3] = {settings->match_minutes, settings->setup_minutes,
                                   settings->start_seconds};
        const bool toggles[5] = {settings->weapons_stay, settings->instant_items,
            settings->quad_drop, settings->instant_weapons, settings->match_lock};
        for (size_t i = 0; i < 8; ++i) {
            if (i < 3)
                snprintf(fields[i], 64, rows[2 + i].text, lengths[i]);
            else
                snprintf(fields[i], 64, rows[2 + i].text,
                         menu_text(m, toggles[i - 3] ? MODE_NAME_CTF_MENU_YES : MODE_NAME_CTF_MENU_NO));
            rows[2 + i].text = fields[i];
        }
        return count;
    }
    if (page == QA_MODE_MENU_CREDITS)
        return count;
    bool following = member->player.follow_target.registry != 0;
    qa_mode_source_settings settings = {0};
    if (m->options.hooks.source_settings)
        m->options.hooks.source_settings(m->options.hooks.context, v->id, &settings);
    snprintf(level, 33, "*%.31s", settings.level_name ? settings.level_name : "");
    rows[2].text = level;
    if (page == QA_MODE_MENU_NO_CHASE)
        return count;
    bool match = menu_match(v);
    if (match && v->value.rules.match_lock) {
        rows[5].text = menu_text(m, MODE_NAME_CTF_MENU_LOCKED);
        rows[7].text = menu_text(m, MODE_NAME_CTF_MENU_LOCK_NOTE);
        rows[5].action = rows[7].action = MENU_NONE;
    } else if (match) {
        rows[5].text = menu_text(m, MODE_NAME_CTF_MENU_RED_MATCH);
        rows[7].text = menu_text(m, MODE_NAME_CTF_MENU_BLUE_MATCH);
    }
    if (v->value.rules.forced_team) {
        size_t row = v->value.rules.forced_team == v->value.rules.teams[0] ? 7 : 5;
        rows[row].text = NULL;
        rows[row].action = MENU_NONE;
    }
    size_t players[2];
    menu_team_counts(m, v, players);
    const char *format = menu_text(m, MODE_NAME_CTF_MENU_COUNT);
    for (size_t i = 0; i < 2; ++i) {
        snprintf(counts[i], 32, format ? format : "", players[i]);
        rows[6 + i * 2] = (menu_row){rows[5 + i * 2].text ? counts[i] : NULL,
                                    MENU_LEFT, MENU_NONE};
    }
    rows[9].text = menu_text(m, following
                                ? MODE_NAME_CTF_MENU_LEAVE_CHASE : MODE_NAME_CTF_MENU_CHASE);
    if (v->value.phase == QA_MODE_SETUP)
        rows[3].text = menu_text(m, MODE_NAME_CTF_MENU_SETUP);
    else if (v->value.phase == QA_MODE_COUNTDOWN)
        rows[3].text = menu_text(m, MODE_NAME_CTF_MENU_STARTING);
    else if (match)
        rows[3].text = menu_text(m, MODE_NAME_CTF_MENU_PLAYING);
    if (v->value.rules.competition && !match && v->value.phase != QA_MODE_SETUP)
        rows[11] = (menu_row){menu_text(m, MODE_NAME_CTF_MENU_REQUEST_MATCH), MENU_LEFT, MENU_MATCH};
    return count;
}
static int8_t menu_first(const menu_row *rows, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (rows[i].action != MENU_NONE)
            return (int8_t)i;
    return -1;
}
static bool layout_publish(qa_modes *m, mode_instance *v, qa_actor_id actor,
                           const char *layout, bool force, bool reliable, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || (!force && !strcmp(member->menu.layout, layout)))
        return true;
    memcpy(member->menu.layout, layout, strlen(layout) + 1);
    ++member->menu.revision;
    return !m->options.hooks.layout || MODE_CALLBACK(m,
        m->options.hooks.layout(m->options.hooks.context, v->id, actor,
                               member->menu.layout, member->menu.page != QA_MODE_MENU_NONE,
                               reliable, e));
}
static bool menu_publish(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          bool opened, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    menu_row rows[18]; char level[33], counts[2][32], fields[8][64];
    size_t count = menu_rows(m, v, member, member->menu.page, rows, level, counts, fields);
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    bool lmctf = v->value.rules.source == QA_MODE_LMCTF;
    if ((opened || lmctf) && (member->menu.cursor < 0 || (size_t)member->menu.cursor >= count ||
                    rows[member->menu.cursor].action == MENU_NONE))
        member->menu.cursor = menu_first(rows, count);
    char layout[QA_MODE_MENU_LAYOUT_BYTES] = "xv 32 yv 8 picn inventory ";
    size_t length = strlen(layout);
    for (size_t i = 0; i < count; ++i) {
        const char *text = rows[i].text;
        if (!text || !*text)
            continue;
        bool alternate = !lmctf && *text == '*';
        if (alternate) ++text;
        int x = lmctf ? 55 : rows[i].align == MENU_CENTER ? 162 - (int)strlen(text) * 4 :
                rows[i].align == MENU_RIGHT ? 260 - (int)strlen(text) * 8 : 64;
        bool selected = member->menu.cursor == (int8_t)i;
        int written = snprintf(layout + length, sizeof(layout) - length,
            "yv %zu xv %d %s \"%s%s\" ", 32 + i * 8, x - (!lmctf && selected ? 8 : 0),
            lmctf ? (selected ? "string" : "string2") :
                     (selected || alternate ? "string2" : "string"),
            selected ? "\x0d" : lmctf ? " " : "", text);
        if (written < 0 || (size_t)written >= sizeof(layout) - length) {
            layout[length] = 0;
            break;
        }
        length += (size_t)written;
    }
    return layout_publish(m, v, actor, layout, opened, true, e);
}
bool mode_ui_close(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || member->menu.page == QA_MODE_MENU_NONE)
        return true;
    if (member->menu.page == QA_MODE_MENU_SCOREBOARD)
        member->player.scoreboard = false;
    member->menu.page = QA_MODE_MENU_NONE;
    member->menu.cursor = -1;
    return layout_publish(m, v, actor, "", true, true, e);
}
static void score_insert(score_row *rows, size_t capacity, size_t *count, score_row row,
                         bool ranked) {
    size_t at = 0;
    while (at < *count &&
           ((ranked && rows[at].score > row.score) ||
            ((!ranked || rows[at].score == row.score) && rows[at].slot <= row.slot)))
        ++at;
    if (at == capacity)
        return;
    size_t end = *count < capacity ? (*count)++ : capacity - 1;
    while (end > at) {
        rows[end] = rows[end - 1];
        --end;
    }
    rows[at] = row;
}
static bool score_rows(qa_modes *m, mode_instance *v,
                       score_row teams[2][SCORE_TEAM_ROWS], size_t used[2],
                       int32_t counts[2], int32_t scores[2],
                       int32_t captures[2], int64_t pings[2],
                       score_row spectators[SCORE_SPECTATORS], size_t *spectator_count,
                       int32_t observer_counts[3],
                       qa_error *e) {
    bool lmctf = v->value.rules.source == QA_MODE_LMCTF;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.actors[i];
        mode_member *member = mode_member_get(m, v, actor);
        mode_player *player = member ? mode_player_get(m, actor) : NULL;
        qa_builtin_player_info info;
        if (!player || !player->connected || !m->options.services.player_info ||
            !m->options.services.player_info(m->options.services.context, actor, &info))
            continue;
        qa_team_id team;
        int32_t score;
        if (!qa_modes_team(m, v->id, actor, &team, e))
            return false;
        if (lmctf)
            score = member->stats.score;
        else if (!qa_modes_score(m, v->id, actor, &score, e))
            return false;
        member = mode_member_get(m, v, actor);
        if (!member)
            continue;
        score_row row = {.actor = actor, .name = player->identity->name,
                         .slot = info.slot, .score = score, .captures = member->stats.captures,
                         .minutes = v->value.time_ns > info.entered_ns
                             ? (v->value.time_ns - info.entered_ns) / MODE_SECOND / 60 : 0,
                         .ping = !lmctf && info.ping > 999 ? 999 : info.ping};
        int index = mode_team_index(v, team);
        if (index >= 0 && index < 2) {
            mode_object *flag = mode_object_get(m, member->flag);
            row.carrier = flag && flag->spec.team == v->value.rules.teams[index ^ 1];
            counts[index] = mode_add_i32(counts[index], 1);
            scores[index] = mode_add_i32(scores[index], score);
            captures[index] = mode_add_i32(captures[index], row.captures);
            pings[index] += info.ping;
            score_insert(teams[index], lmctf ? SCORE_TEAM_ROWS : SCORE_CTF_TEAM_ROWS,
                         &used[index], row, true);
        } else if (lmctf) {
            int observer = mode_team_index(v, member->player.observer_team);
            row.observer = observer >= 0 && observer < 2 ? (uint8_t)observer : 2;
            observer_counts[row.observer] = mode_add_i32(observer_counts[row.observer], 1);
            score_insert(spectators, SCORE_SPECTATORS, spectator_count, row, false);
        } else if (!team) {
            qa_physics *physics = m->options.services.physics;
            qa_physics_properties properties;
            if (physics && physics->services.read &&
                physics->services.read(physics->services.context, actor, &properties) &&
                properties.solid == QA_PHYSICS_NOT_SOLID)
                score_insert(spectators, SCORE_SPECTATORS, spectator_count, row, false);
        }
    }
    return true;
}
static bool score_admit(char *layout, size_t *length, const char *entry, size_t entry_length,
                        size_t maximum) {
    if (*length > maximum || entry_length > maximum - *length)
        return false;
    memcpy(layout + *length, entry, entry_length + 1);
    *length += entry_length;
    return true;
}
static bool score_string(char *layout, size_t *length, int x, int y, const char *text) {
    int written = snprintf(layout + *length, SCORE_LMCTF_BYTES + 1 - *length,
                           "xv %d yv %d string2 \"%s\" ", x, y, text ? text : "");
    if (written < 0 || (size_t)written > SCORE_LMCTF_BYTES - *length) {
        layout[*length] = 0;
        return false;
    }
    *length += (size_t)written;
    return true;
}
static void score_lmctf_layout(qa_modes *m, mode_instance *v, qa_actor_id actor,
    score_row teams[2][SCORE_TEAM_ROWS], const size_t used[2], const int32_t counts[2],
    const int32_t captures[2], const int64_t pings[2], const score_row *spectators,
    size_t spectator_count, const int32_t observer_counts[3], char *layout) {
    size_t length = 0;
    layout[0] = 0;
    bool small = counts[0] > 6 || counts[1] > 6;
    if (small) {
        const char *header = menu_text(m, MODE_NAME_LMCTF_SCORE_HEADER);
        score_admit(layout, &length, header, strlen(header), SCORE_LMCTF_BYTES);
    }
    mode_member *member = mode_member_get(m, v, actor);
    qa_actor_id killer = member ? member->menu.killer : (qa_actor_id){0};
    char entry[SCORE_LMCTF_BYTES + 1];
    for (unsigned team = 0; team < 2; ++team) {
        int x = (int)team * 160;
        for (size_t i = 0; i < used[team]; ++i) {
            const score_row *row = &teams[team][i];
            int y = (int)(small ? 48 + i * 8 : 32 + i * 32);
            int written;
            if (small)
                written = snprintf(entry, sizeof(entry), "ctf %d %d %u %d %d ", x, y,
                    row->slot, row->score, row->ping > 999 ? 999 : row->ping);
            else {
                mode_name tag = qa_actor_id_equal(row->actor, actor) ? MODE_NAME_PIC_TAG1 :
                                qa_actor_id_equal(row->actor, killer) ? MODE_NAME_PIC_TAG2 :
                                MODE_NAME_COUNT;
                if (tag != MODE_NAME_COUNT) {
                    written = snprintf(entry, sizeof(entry), "xv %d yv %d picn %s ",
                                       x + 32, y, menu_text(m, tag));
                    if (written < 0 || (size_t)written >= sizeof(entry) ||
                        !score_admit(layout, &length, entry, (size_t)written, SCORE_LMCTF_BYTES))
                        break;
                }
                written = snprintf(entry, sizeof(entry), "client %d %d %u %d %d %" PRIu64 " ",
                                   x, y, row->slot, row->score, row->ping, row->minutes);
            }
            if (written < 0 || (size_t)written >= sizeof(entry) ||
                !score_admit(layout, &length, entry, (size_t)written, SCORE_LMCTF_BYTES))
                break;
            if (!small && row->captures) {
                written = snprintf(entry, sizeof(entry), "xv %d yv %d string2 \"C:%d\" ",
                                   x + 112, y + 24, row->captures);
                if (written < 0 || (size_t)written >= sizeof(entry) ||
                    !score_admit(layout, &length, entry, (size_t)written, SCORE_LMCTF_BYTES))
                    break;
            }
        }
    }
    qa_strings *strings = qa_session_strings(m->options.services.session);
    int y = 256;
    for (unsigned group = 0; group < 3; ++group) {
        if (!observer_counts[group])
            continue;
        int x = group < 2 ? (int)group * 160 : 80;
        mode_name title = group == 0 ? MODE_NAME_LMCTF_SCORE_RED_OBSERVERS :
                          group == 1 ? MODE_NAME_LMCTF_SCORE_BLUE_OBSERVERS :
                                       MODE_NAME_LMCTF_SCORE_OBSERVERS;
        size_t entry_length = 0, ordinal = 0;
        bool fits = score_string(entry, &entry_length, x, y, menu_text(m, title));
        y += 8;
        bool columns = group == 2 && !observer_counts[0] && !observer_counts[1];
        for (size_t i = 0; i < spectator_count; ++i) {
            const score_row *row = &spectators[i];
            if (row->observer != group)
                continue;
            int name_x = columns ? (int)(ordinal % 3) * 130 : x;
            if (fits)
                fits = score_string(entry, &entry_length, name_x, y,
                                    qa_strings_cstr(strings, row->name));
            ++ordinal;
            if (!columns || ordinal % 3 == 0)
                y += 8;
        }
        if (ordinal < (size_t)observer_counts[group]) {
            fits = false;
            size_t remaining = (size_t)observer_counts[group] - ordinal;
            y += (int)(columns ? ((ordinal + remaining) / 3 - ordinal / 3) * 8 : remaining * 8);
        }
        if (fits)
            score_admit(layout, &length, entry, entry_length, SCORE_LMCTF_BYTES);
    }
    int64_t average[2] = {used[0] ? pings[0] / (int64_t)used[0] : 0,
                          used[1] ? pings[1] / (int64_t)used[1] : 0};
    int written = snprintf(entry, sizeof(entry),
        "xv 0 yv 0 picn %s xv 160 yv 0 picn %s xv 32 yv 0 picn %s xv 192 yv 0 picn %s "
        "xv 36 yv 4 string2 \"C:%3d\" xv 36 yv 20 string2 \"AP:%3" PRId64 "\" "
        "xv 196 yv 4 string2 \"C:%3d\" xv 196 yv 20 string2 \"AP:%3" PRId64 "\" "
        "xv 90 yv 4 num 4 19 xv 250 yv 4 num 4 20 ",
        menu_text(m, MODE_NAME_PIC_REDLION_I), menu_text(m, MODE_NAME_PIC_BLUEWOLF_I),
        menu_text(m, MODE_NAME_PIC_REDTAG), menu_text(m, MODE_NAME_PIC_BLUETAG),
        captures[0], average[0], captures[1], average[1]);
    if (written >= 0 && (size_t)written < sizeof(entry) && length + (size_t)written < SCORE_LMCTF_BYTES)
        score_admit(layout, &length, entry, (size_t)written, SCORE_LMCTF_BYTES);
}
static bool scoreboard_publish(qa_modes *m, mode_instance *v, qa_actor_id actor,
                              bool force, bool reliable, qa_error *e) {
    score_row teams[2][SCORE_TEAM_ROWS], spectators[SCORE_SPECTATORS];
    size_t used[2] = {0}, spectator_count = 0;
    int32_t counts[2] = {0}, scores[2] = {0};
    int32_t captures[2] = {0}, observer_counts[3] = {0};
    int64_t pings[2] = {0};
    if (!score_rows(m, v, teams, used, counts, scores, captures, pings,
                    spectators, &spectator_count, observer_counts, e))
        return false;
    char layout[QA_MODE_MENU_LAYOUT_BYTES], entry[1024];
    if (v->value.rules.source == QA_MODE_LMCTF) {
        score_lmctf_layout(m, v, actor, teams, used, counts, captures, pings,
                           spectators, spectator_count, observer_counts, layout);
        return layout_publish(m, v, actor, layout, force, reliable, e);
    }
    int written = snprintf(layout, sizeof(layout), menu_text(m, MODE_NAME_CTF_SCORE_HEADER),
                           scores[0], counts[0], scores[1], counts[1]);
    if (written < 0 || (size_t)written >= sizeof(layout))
        return true;
    size_t length = (size_t)written;
    int last[2] = {0};
    for (size_t i = 0; i < SCORE_CTF_TEAM_ROWS; ++i) {
        if (i >= used[0] && i >= used[1])
            break;
        size_t entry_length = 0;
        entry[0] = 0;
        for (unsigned team = 0; team < 2; ++team) {
            if (i >= used[team])
                continue;
            const score_row *row = &teams[team][i];
            written = snprintf(entry + entry_length, sizeof(entry) - entry_length,
                "ctf %u %zu %u %d %d ", team * 160, 42 + i * 8,
                row->slot, row->score, row->ping);
            if (written < 0 || (size_t)written >= sizeof(entry) - entry_length)
                break;
            entry_length += (size_t)written;
            if (row->carrier) {
                written = snprintf(entry + entry_length, sizeof(entry) - entry_length,
                    "xv %u yv %zu picn %s ", 56 + team * 160, 42 + i * 8,
                    menu_text(m, team ? MODE_NAME_PIC_SBFCTF1 : MODE_NAME_PIC_SBFCTF2));
                if (written < 0 || (size_t)written >= sizeof(entry) - entry_length)
                    break;
                entry_length += (size_t)written;
            }
            size_t previous = length;
            score_admit(layout, &length, entry, entry_length, SCORE_BYTES - 1);
            if (length != previous)
                last[team] = (int)i;
        }
    }
    int y = ((last[0] > last[1] ? last[0] : last[1]) + 2) * 8 + 42;
    if (length + 50 < SCORE_BYTES && spectator_count) {
        written = snprintf(entry, sizeof(entry), "xv 0 yv %d string2 \"%s\" ", y,
                           menu_text(m, MODE_NAME_CTF_SCORE_SPECTATORS));
        if (written >= 0 && (size_t)written < sizeof(entry)) {
            size_t entry_length = (size_t)written;
            memcpy(layout + length, entry, entry_length + 1);
            length += entry_length;
            y += 8;
            for (size_t i = 0; i < spectator_count; ++i) {
                const score_row *row = &spectators[i];
                written = snprintf(entry + entry_length, sizeof(entry) - entry_length,
                    "ctf %u %d %u %d %d ", (i & 1u) ? 160 : 0, y,
                    row->slot, row->score, row->ping);
                if (written < 0 || (size_t)written >= sizeof(entry) - entry_length)
                    break;
                entry_length += (size_t)written;
                score_admit(layout, &length, entry, entry_length, SCORE_BYTES - 1);
                if (i & 1u) y += 8;
            }
        }
    }
    for (unsigned team = 0; team < 2; ++team) {
        if (counts[team] - last[team] <= 1)
            continue;
        char more[64];
        snprintf(more, sizeof(more), menu_text(m, MODE_NAME_CTF_SCORE_MORE),
                 counts[team] - last[team] - 1);
        written = snprintf(layout + length, sizeof(layout) - length,
            "xv %u yv %d string \"%s\" ", 8 + team * 160,
            42 + (last[team] + 1) * 8, more);
        if (written < 0 || (size_t)written >= sizeof(layout) - length) {
            layout[length] = 0;
            break;
        }
        length += (size_t)written;
    }
    return layout_publish(m, v, actor, layout, force, reliable, e);
}
bool mode_ui_scoreboard(qa_modes *m, mode_instance *v, qa_actor_id actor,
                        bool visible, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !layout_enabled(v))
        return true;
    if (!visible)
        return mode_ui_close(m, v, actor, e);
    member->menu.page = QA_MODE_MENU_SCOREBOARD;
    member->menu.cursor = -1;
    member->menu.refresh_frame = UINT64_MAX;
    if (m->options.hooks.source_frame)
        m->options.hooks.source_frame(m->options.hooks.context, v->id,
                                      &member->menu.refresh_frame);
    return scoreboard_publish(m, v, actor, true, true, e);
}
bool mode_ui_frame(qa_modes *m, mode_instance *v, qa_error *e) {
    uint64_t frame;
    if (!layout_enabled(v) ||
        !m->options.hooks.source_frame ||
        !m->options.hooks.source_frame(m->options.hooks.context, v->id, &frame) || (frame & 31u))
        return true;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.actors[i];
        mode_member *member = mode_member_get(m, v, actor);
        if (!member || !member->player.scoreboard ||
            member->menu.page != QA_MODE_MENU_SCOREBOARD || member->menu.refresh_frame == frame)
            continue;
        member->menu.refresh_frame = frame;
        if (!scoreboard_publish(m, v, actor, true, false, e))
            return false;
    }
    return true;
}
static bool menu_open(qa_modes *m, mode_instance *v, qa_actor_id actor,
                       qa_mode_menu_page page, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    member->menu.page = page;
    member->menu.cursor = -1;
    member->player.scoreboard = false;
    return menu_publish(m, v, actor, true, e);
}
static bool lmctf_main_open(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    return menu_open(m, v, actor, QA_MODE_MENU_LMCTF_MAIN, e);
}
bool mode_ui_open_join(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    if (v->value.rules.source != QA_MODE_Q2_CTF || !v->value.rules.enabled)
        return true;
    size_t counts[2];
    menu_team_counts(m, v, counts);
    if (counts[0] == counts[1]) {
        if (m->options.hooks.source_random)
            m->options.hooks.source_random(m->options.hooks.context, v->id);
        else
            mode_random(m);
    }
    return menu_open(m, v, actor, QA_MODE_MENU_JOIN, e);
}
bool mode_ui_open_admin(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !member->admin || v->value.rules.source != QA_MODE_Q2_CTF ||
        !v->value.rules.enabled)
        return true;
    return menu_open(m, v, actor, QA_MODE_MENU_ADMIN, e);
}
bool mode_ui_open_lmctf_vote(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    if (v->value.rules.source != QA_MODE_LMCTF || !v->value.rules.enabled)
        return true;
    return menu_open(m, v, actor, QA_MODE_MENU_LMCTF_VOTE, e);
}
static bool menu_settings_open(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    qa_mode_source_settings settings = {0};
    if (!m->options.hooks.source_settings ||
        !m->options.hooks.source_settings(m->options.hooks.context, v->id, &settings))
        return true;
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !member->admin)
        return true;
    member->menu.settings = settings.ctf;
    return menu_open(m, v, actor, QA_MODE_MENU_SETTINGS, e);
}
static bool menu_select(qa_modes *m, mode_instance *v, qa_actor_id actor,
                         menu_action action, qa_error *e) {
    bool accepted;
    mode_member *admin_member = mode_member_get(m, v, actor);
    if (action >= MENU_ADMIN_SETTINGS && (!admin_member || !admin_member->admin))
        return true;
    switch (action) {
    case MENU_RED: case MENU_BLUE:
        return mode_ui_close(m, v, actor, e) &&
               (!mode_member_get(m, v, actor) || mode_ui_request_team(m, v, actor,
                    v->value.rules.teams[action == MENU_BLUE], &accepted, e));
    case MENU_CHASE: {
        mode_member *member = mode_member_get(m, v, actor);
        if (!member)
            return true;
        bool leave = member->player.follow_target.registry != 0;
        if (!qa_modes_follow(m, v->id, actor, (qa_actor_id){0}, 0, leave ? 0 : 1,
                             &accepted, e))
            return false;
        if (accepted)
            return mode_ui_close(m, v, actor, e);
        return menu_open(m, v, actor, QA_MODE_MENU_NO_CHASE, e);
    }
    case MENU_CREDITS: return menu_open(m, v, actor, QA_MODE_MENU_CREDITS, e);
    case MENU_BACK: return mode_ui_open_join(m, v, actor, e);
    case MENU_MATCH: {
        qa_match_intent intent = {.kind = QA_MATCH_CANCEL, .actor = actor};
        return mode_ui_close(m, v, actor, e) &&
               (!mode_member_get(m, v, actor) || qa_modes_vote_start(m, v->id, actor, 0, &intent, e));
    }
    case MENU_VOTE_SKIP: {
        qa_match_intent intent = {.kind = QA_MATCH_NEXT_MAP};
        return qa_modes_vote_start(m, v->id, actor, 0, &intent, e) &&
               mode_ui_open_lmctf_vote(m, v, actor, e);
    }
    case MENU_VOTE_YES: case MENU_VOTE_NO:
        return qa_modes_vote_cast(m, v->id, actor, 0, action == MENU_VOTE_YES, e) &&
               mode_ui_open_lmctf_vote(m, v, actor, e);
    case MENU_LM_MAIN:
        return lmctf_main_open(m, v, actor, e);
    case MENU_LM_OBSERVER_MENU:
        return menu_open(m, v, actor, QA_MODE_MENU_LMCTF_OBSERVER, e);
    case MENU_LM_VOTE:
        return mode_ui_open_lmctf_vote(m, v, actor, e);
    case MENU_LM_CHANGE_TEAM: case MENU_LM_PLAY: {
        qa_team_id team;
        if (action == MENU_LM_PLAY) {
            if (!qa_modes_choose_team(m, v->id, actor, &team, e))
                return false;
        } else {
            if (!qa_modes_team(m, v->id, actor, &team, e))
                return false;
            team = v->value.rules.teams[team == v->value.rules.teams[1] ? 0 : 1];
        }
        return mode_ui_request_team(m, v, actor, team, &accepted, e) &&
               mode_ui_close(m, v, actor, e);
    }
    case MENU_LM_OBSERVE_RED: case MENU_LM_OBSERVE_BLUE: case MENU_LM_OBSERVE_ALL: {
        bool close = admin_member && admin_member->menu.page == QA_MODE_MENU_LMCTF_MAIN;
        qa_team_id filter = action == MENU_LM_OBSERVE_ALL ? 0
            : v->value.rules.teams[action == MENU_LM_OBSERVE_BLUE ? 1 : 0];
        if (!qa_modes_observe(m, v->id, actor, filter, &accepted, e))
            return false;
        if (!accepted || !mode_member_get(m, v, actor))
            return close ? mode_ui_close(m, v, actor, e) : true;
        return qa_modes_follow(m, v->id, actor, (qa_actor_id){0}, 0, filter ? 1 : 0,
                                &accepted, e) && (close ? mode_ui_close(m, v, actor, e) : true);
    }
    case MENU_ADMIN_SETTINGS: return menu_settings_open(m, v, actor, e);
    case MENU_ADMIN_MATCH:
        return mode_ui_close(m, v, actor, e) &&
               (!mode_member_get(m, v, actor) || mode_ui_ctf_match(m, v, actor, e));
    case MENU_ADMIN_CANCEL: return mode_ui_close(m, v, actor, e);
    case MENU_SETTINGS_APPLY: {
        qa_mode_ctf_settings settings = admin_member->menu.settings;
        return mode_ui_ctf_settings_apply(m, v, actor, &settings, e) &&
               mode_ui_open_admin(m, v, actor, e);
    }
    case MENU_SETTINGS_CANCEL: return mode_ui_open_admin(m, v, actor, e);
    case MENU_SETTING_MATCH:
        admin_member->menu.settings.match_minutes =
            (admin_member->menu.settings.match_minutes % 60) + 5;
        if (admin_member->menu.settings.match_minutes < 5)
            admin_member->menu.settings.match_minutes = 5;
        break;
    case MENU_SETTING_SETUP:
        admin_member->menu.settings.setup_minutes =
            (admin_member->menu.settings.setup_minutes % 60) + 5;
        if (admin_member->menu.settings.setup_minutes < 5)
            admin_member->menu.settings.setup_minutes = 5;
        break;
    case MENU_SETTING_START:
        admin_member->menu.settings.start_seconds =
            (admin_member->menu.settings.start_seconds % 600) + 10;
        if (admin_member->menu.settings.start_seconds < 20)
            admin_member->menu.settings.start_seconds = 20;
        break;
    case MENU_SETTING_WEAPONS_STAY:
        admin_member->menu.settings.weapons_stay = !admin_member->menu.settings.weapons_stay;
        break;
    case MENU_SETTING_INSTANT_ITEMS:
        admin_member->menu.settings.instant_items = !admin_member->menu.settings.instant_items;
        break;
    case MENU_SETTING_QUAD_DROP:
        admin_member->menu.settings.quad_drop = !admin_member->menu.settings.quad_drop;
        break;
    case MENU_SETTING_INSTANT_WEAPONS:
        admin_member->menu.settings.instant_weapons = !admin_member->menu.settings.instant_weapons;
        break;
    case MENU_SETTING_MATCH_LOCK:
        admin_member->menu.settings.match_lock = !admin_member->menu.settings.match_lock;
        break;
    case MENU_NONE: return true;
    }
    return menu_publish(m, v, actor, false, e);
}
static bool menu_named(const char *text, const char *name) {
    return qa_text_bytes_equal((qa_bytes){(const uint8_t *)text, strlen(text)},
                               (qa_bytes){(const uint8_t *)name, strlen(name)}, true);
}
static bool menu_navigate(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          mode_member *member, const char *name, bool *handled, qa_error *e) {
    if (member->menu.page == QA_MODE_MENU_NONE || member->menu.page == QA_MODE_MENU_SCOREBOARD)
        return true;
    bool next = menu_named(name, "invnext"), previous = menu_named(name, "invprev");
    bool select = menu_named(name, "invuse");
    if (!next && !previous && !select)
        return true;
    *handled = true;
    menu_row rows[18]; char level[33], counts[2][32], fields[8][64];
    size_t count = menu_rows(m, v, member, member->menu.page, rows, level, counts, fields);
    member = mode_member_get(m, v, actor);
    if (!member || member->menu.cursor < 0 || !count)
        return true;
    if (select)
        return (size_t)member->menu.cursor >= count ? true :
               menu_select(m, v, actor, rows[member->menu.cursor].action, e);
    size_t cursor = (size_t)member->menu.cursor;
    for (size_t step = 0; step < count; ++step) {
        cursor = next ? (cursor + 1) % count : (cursor + count - 1) % count;
        if (rows[cursor].action != MENU_NONE) {
            member->menu.cursor = (int8_t)cursor;
            break;
        }
    }
    return menu_publish(m, v, actor, false, e);
}
bool mode_ui_command(qa_modes *m, mode_instance *v, qa_actor_id actor,
                       const qa_command_invocation *command, bool *handled, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !layout_enabled(v))
        return true;
    bool lmctf = v->value.rules.source == QA_MODE_LMCTF;
    const char *name = command->argv[0];
    if (menu_named(name, "score") || menu_named(name, "+scores") || menu_named(name, "-scores")) {
        *handled = true;
        bool visible = menu_named(name, "score") ? !member->player.scoreboard
                       : !menu_named(name, "-scores");
        return qa_modes_scoreboard(m, v->id, actor, visible, e);
    }
    if (menu_named(name, "putaway")) {
        member->player.scoreboard = false;
        if (member->menu.page != QA_MODE_MENU_NONE) {
            *handled = true;
            return mode_ui_close(m, v, actor, e);
        }
        return true;
    }
    if (lmctf) {
        if (menu_named(name, "ctfmenu") || menu_named(name, "lmctf-menu")) {
            *handled = true;
            return member->menu.page != QA_MODE_MENU_NONE &&
                   member->menu.page != QA_MODE_MENU_SCOREBOARD
                ? mode_ui_close(m, v, actor, e) : lmctf_main_open(m, v, actor, e);
        }
        if (menu_named(name, "inven")) {
            member->player.scoreboard = false;
            return mode_ui_close(m, v, actor, e);
        }
        if (menu_named(name, "lmctf-vote")) {
            if (command->argc == 1) {
                *handled = true;
                return mode_ui_open_lmctf_vote(m, v, actor, e);
            }
            if (menu_named(command->argv[1], "skip")) {
                *handled = true;
                return menu_select(m, v, actor, MENU_VOTE_SKIP, e);
            }
        } else if (menu_named(name, "voteyes") || menu_named(name, "voteno")) {
            *handled = true;
            return menu_select(m, v, actor, menu_named(name, "voteyes")
                ? MENU_VOTE_YES : MENU_VOTE_NO, e);
        }
        return menu_navigate(m, v, actor, member, name, handled, e);
    }
    if (menu_named(name, "inven")) {
        member->player.scoreboard = false;
        if (member->menu.page == QA_MODE_MENU_SCOREBOARD) {
            if (!mode_ui_close(m, v, actor, e))
                return false;
            member = mode_member_get(m, v, actor);
            if (!member)
                return true;
        }
        if (member->menu.page != QA_MODE_MENU_NONE) {
            *handled = true;
            return mode_ui_close(m, v, actor, e);
        }
        qa_team_id team;
        if (!qa_modes_team(m, v->id, actor, &team, e))
            return false;
        if (!team) {
            *handled = true;
            return mode_ui_open_join(m, v, actor, e);
        }
        return true;
    }
    if (menu_named(name, "ctf-menu")) {
        const char *action = command->argc > 1 ? command->argv[1] : "join";
        if (menu_named(action, "ready") || menu_named(action, "notready")) {
            *handled = true;
            return qa_modes_ready(m, v->id, actor, menu_named(action, "ready"), e);
        }
        if (menu_named(action, "observer")) {
            *handled = true;
            bool accepted;
            return qa_modes_observe(m, v->id, actor, 0, &accepted, e);
        }
        menu_action admin = menu_named(action, "admin-settings") ? MENU_ADMIN_SETTINGS :
                            menu_named(action, "admin-start") ? MENU_ADMIN_MATCH :
                            menu_named(action, "admin-cancel") ? MENU_ADMIN_CANCEL :
                            menu_named(action, "settings-apply") ? MENU_SETTINGS_APPLY :
                            menu_named(action, "settings-cancel") ? MENU_SETTINGS_CANCEL : MENU_NONE;
        if (admin != MENU_NONE) {
            *handled = true;
            if ((admin == MENU_SETTINGS_APPLY || admin == MENU_SETTINGS_CANCEL) &&
                member->menu.page != QA_MODE_MENU_SETTINGS)
                return true;
            return menu_select(m, v, actor, admin, e);
        }
        if (menu_named(action, "match"))
            return mode_ui_close(m, v, actor, e);
        if (menu_named(action, "join")) {
            *handled = true;
            return mode_ui_open_join(m, v, actor, e);
        }
        if (menu_named(action, "credits")) {
            *handled = true;
            return menu_open(m, v, actor, QA_MODE_MENU_CREDITS, e);
        }
        if (menu_named(action, "close")) {
            *handled = true;
            return mode_ui_close(m, v, actor, e);
        }
        menu_action wanted = menu_named(action, "join-red") ? MENU_RED :
                             menu_named(action, "join-blue") ? MENU_BLUE :
                             menu_named(action, "chase") ? MENU_CHASE : MENU_NONE;
        if (wanted == MENU_NONE)
            return true;
        *handled = true;
        menu_row rows[18]; char level[33], counts[2][32], fields[8][64];
        size_t count = menu_rows(m, v, member, QA_MODE_MENU_JOIN, rows, level, counts, fields);
        if (!mode_member_get(m, v, actor))
            return true;
        for (size_t i = 0; i < count; ++i)
            if (rows[i].action == wanted)
                return menu_select(m, v, actor, wanted, e);
        return true;
    }
    return menu_navigate(m, v, actor, member, name, handled, e);
}
bool qa_modes_layout_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                           const char **text, bool *visible, uint64_t *revision,
                           qa_mode_menu_page *page) {
    mode_instance *v = mode_get(m, id);
    if (!layout_enabled(v))
        return false;
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return false;
    if (text) *text = member->menu.layout;
    if (visible) *visible = member->menu.page != QA_MODE_MENU_NONE;
    if (revision) *revision = member->menu.revision;
    if (page) *page = member->menu.page;
    return true;
}
