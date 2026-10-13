#include "internal.h"
#include "qa/console.h"
#include "qa/text.h"
#include <stdio.h>

typedef enum menu_action {
    MENU_NONE, MENU_RED, MENU_BLUE, MENU_CHASE, MENU_CREDITS, MENU_BACK, MENU_MATCH
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
#undef MENU_ENTRY
#undef MENU_BLANK

static const char *menu_text(qa_modes *m, mode_name name) {
    return name == MODE_NAME_COUNT ? NULL :
        qa_strings_cstr(qa_session_strings(m->options.services.session), m->runtime_names[name]);
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
                         menu_row rows[18], char level[33], char counts[2][32]) {
    const menu_entry *entries = NULL;
    size_t count = 0;
    switch (page) {
    case QA_MODE_MENU_JOIN:
        entries = join_entries; count = sizeof(join_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_CREDITS:
        entries = credits_entries; count = sizeof(credits_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_NO_CHASE:
        entries = no_chase_entries; count = sizeof(no_chase_entries) / sizeof(*entries); break;
    case QA_MODE_MENU_NONE: return 0;
    }
    for (size_t i = 0; i < count; ++i)
        rows[i] = (menu_row){menu_text(m, entries[i].name), entries[i].align, entries[i].action};
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
static bool menu_publish(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          bool opened, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    menu_row rows[18]; char level[33], counts[2][32];
    size_t count = menu_rows(m, v, member, member->menu.page, rows, level, counts);
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    if (opened && (member->menu.cursor < 0 || (size_t)member->menu.cursor >= count ||
                    rows[member->menu.cursor].action == MENU_NONE))
        member->menu.cursor = menu_first(rows, count);
    char layout[QA_MODE_MENU_LAYOUT_BYTES] = "xv 32 yv 8 picn inventory ";
    size_t length = strlen(layout);
    for (size_t i = 0; i < count; ++i) {
        const char *text = rows[i].text;
        if (!text || !*text)
            continue;
        bool alternate = *text == '*';
        if (alternate) ++text;
        int x = rows[i].align == MENU_CENTER ? 162 - (int)strlen(text) * 4 :
                rows[i].align == MENU_RIGHT ? 260 - (int)strlen(text) * 8 : 64;
        bool selected = member->menu.cursor == (int8_t)i;
        int written = snprintf(layout + length, sizeof(layout) - length,
            "yv %zu xv %d %s \"%s%s\" ", 32 + i * 8, x - (selected ? 8 : 0),
            selected || alternate ? "string2" : "string", selected ? "\x0d" : "", text);
        if (written < 0 || (size_t)written >= sizeof(layout) - length) {
            layout[length] = 0;
            break;
        }
        length += (size_t)written;
    }
    if (!opened && !strcmp(member->menu.layout, layout))
        return true;
    memcpy(member->menu.layout, layout, length + 1);
    ++member->menu.revision;
    return !m->options.hooks.layout || MODE_CALLBACK(m,
        m->options.hooks.layout(m->options.hooks.context, v->id, actor,
                               member->menu.layout, true, e));
}
bool mode_ui_close(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || member->menu.page == QA_MODE_MENU_NONE)
        return true;
    member->menu.page = QA_MODE_MENU_NONE;
    member->menu.cursor = -1;
    member->menu.layout[0] = 0;
    ++member->menu.revision;
    return !m->options.hooks.layout || MODE_CALLBACK(m,
        m->options.hooks.layout(m->options.hooks.context, v->id, actor,
                               member->menu.layout, false, e));
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
static bool menu_select(qa_modes *m, mode_instance *v, qa_actor_id actor,
                         menu_action action, qa_error *e) {
    bool accepted;
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
    case MENU_NONE: return true;
    }
    return true;
}
static bool menu_named(const char *text, const char *name) {
    return qa_text_bytes_equal((qa_bytes){(const uint8_t *)text, strlen(text)},
                               (qa_bytes){(const uint8_t *)name, strlen(name)}, true);
}
bool mode_ui_command(qa_modes *m, mode_instance *v, qa_actor_id actor,
                       const qa_command_invocation *command, bool *handled, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || v->value.rules.source != QA_MODE_Q2_CTF || !v->value.rules.enabled)
        return true;
    const char *name = command->argv[0];
    if (menu_named(name, "score") || menu_named(name, "+scores") || menu_named(name, "-scores"))
        return mode_ui_close(m, v, actor, e);
    if (menu_named(name, "putaway")) {
        member->player.scoreboard = false;
        if (member->menu.page != QA_MODE_MENU_NONE) {
            *handled = true;
            return mode_ui_close(m, v, actor, e);
        }
        return true;
    }
    if (menu_named(name, "inven")) {
        member->player.scoreboard = false;
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
        menu_row rows[18]; char level[33], counts[2][32];
        size_t count = menu_rows(m, v, member, QA_MODE_MENU_JOIN, rows, level, counts);
        if (!mode_member_get(m, v, actor))
            return true;
        for (size_t i = 0; i < count; ++i)
            if (rows[i].action == wanted)
                return menu_select(m, v, actor, wanted, e);
        return true;
    }
    if (member->menu.page == QA_MODE_MENU_NONE)
        return true;
    bool next = menu_named(name, "invnext"), previous = menu_named(name, "invprev");
    bool select = menu_named(name, "invuse");
    if (!next && !previous && !select)
        return true;
    *handled = true;
    menu_row rows[18]; char level[33], counts[2][32];
    size_t count = menu_rows(m, v, member, member->menu.page, rows, level, counts);
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
bool qa_modes_layout_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                           const char **text, bool *visible, uint64_t *revision) {
    mode_member *member = mode_member_get(m, mode_get(m, id), actor);
    if (!member || !text || !visible || !revision)
        return false;
    *text = member->menu.layout;
    *visible = member->menu.page != QA_MODE_MENU_NONE;
    *revision = member->menu.revision;
    return true;
}
