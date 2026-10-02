/*
 * Source behavior from id Software's code/game/g_cmds.c and g_main.c and the
 * TypeScript GameCommandRuntime and MatchRuntime translations.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#include "native_q3_votes.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "unified_q3_events.h"
#include "qa/game_q3_configstrings.h"
#include "qa/source_save.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VOTE_BYTES = 1024, VOTE_FLAG = 0x4000, TEAM_VOTE_FLAG = 0x80000 };

typedef struct source_vote {
    int32_t time, yes, no;
    char text[VOTE_BYTES];
} source_vote;

typedef struct vote_state {
    source_vote global, teams[2];
    int32_t execute_time;
    char display[VOTE_BYTES];
    bool initialized;
} vote_state;

typedef enum vote_operation {
    VOTE_IDLE, VOTE_COMMAND, VOTE_FRAME, VOTE_LEADER, VOTE_CAPTURE, VOTE_IMPORT
} vote_operation;

struct application_native_q3_votes {
    application_provider *provider;
    vote_state state;
    vote_operation operation;
};

typedef struct vote_scope {
    struct application_native_q3_votes *owner;
    qa_application *application;
    qa_q3_game *game;
    qa_actor_id actor;
    qa_actor_owner source_owner;
    uint32_t slot, maximum;
} vote_scope;

static struct application_native_q3_votes *owner_at(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 ? provider->native_q3_votes : NULL;
}

static bool named(const char *text, const char *name)
{
    while (*text && *name) {
        unsigned char byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        if (byte != (unsigned char)*name++) return false;
    }
    return !*text && !*name;
}

static int32_t signed_bits(uint32_t bits)
{
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

/* bg_lib AddInt negates through int32, including its INT32_MIN byte output. */
static void decimal(int32_t value, char text[32])
{
    unsigned char reversed[16];
    size_t count = 0;
    int32_t remaining = value < 0 ? signed_bits(0u - (uint32_t)value) : value;
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0) reversed[count++] = '-';
    for (size_t index = 0; index < count; ++index) text[index] = (char)reversed[count - index - 1];
    text[count] = 0;
}

static void store_text(char text[VOTE_BYTES], const char *source)
{
    size_t length = strlen(source);
    if (length >= VOTE_BYTES) length = VOTE_BYTES - 1;
    memcpy(text, source, length);
    text[length] = 0;
}

static int32_t integer(const char *text)
{
    while (*text) {
        unsigned char byte = (unsigned char)*text;
        if ((byte < 128 ? (int)byte : (int)byte - 256) > 32) break;
        ++text;
    }
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t bits = 0;
    while (*text >= '0' && *text <= '9') bits = bits * 10u + (uint32_t)(*text++ - '0');
    return signed_bits(negative ? 0u - bits : bits);
}

static void argument(const qa_command_invocation *command, size_t index,
    char *out, size_t capacity)
{
    const char *text = index < command->argc ? command->argv[index] : "";
    size_t length = strlen(text);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, text, length);
    out[length] = 0;
}

static bool scope_live(const vote_scope *scope, qa_error *error)
{
    application_provider *provider = scope->owner->provider;
    qa_application *app = scope->application;
    if (provider->native_q3_votes != scope->owner || provider->application != app ||
        provider->owner != scope->source_owner || provider->state.q3 != scope->game ||
        !application_native_q3_votes_bound(provider))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 vote source has retired");
    if (scope->actor.registry) {
        uint32_t slot;
        if (!qa_actors_get(qa_session_actors(app->session), scope->actor) ||
            !qa_q3_native_client_slot(scope->game, scope->actor, &slot, error) || slot != scope->slot)
            return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 vote client has retired");
    }
    return true;
}

static bool scope_begin(application_provider *provider, qa_actor_id actor,
    vote_operation operation, vote_scope *scope, qa_error *error)
{
    struct application_native_q3_votes *owner = owner_at(provider);
    qa_application *app = provider ? provider->application : NULL;
    if (!owner || !app || !owner->state.initialized || owner->operation != VOTE_IDLE ||
        !provider->state.q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes lack an idle source level");
    *scope = (vote_scope){.owner = owner, .application = app, .game = provider->state.q3,
        .source_owner = provider->owner};
    if (!scope_live(scope, error) || !qa_q3_source_max_clients(scope->game, &scope->maximum, error) ||
        (actor.registry && !qa_q3_native_client_slot(scope->game, actor, &scope->slot, error))) return false;
    scope->actor = actor;
    if (!scope_live(scope, error) || !application_native_q3_console_borrow(provider, error)) return false;
    owner->operation = operation;
    return true;
}

static void scope_end(vote_scope *scope)
{
    scope->owner->operation = VOTE_IDLE;
    application_native_q3_console_release(scope->owner->provider);
}

static bool send(const vote_scope *scope, int32_t slot, const char *text, qa_error *error)
{
    return application_native_q3_send_command(scope->owner->provider, slot, text, error) &&
        scope_live(scope, error);
}

static bool print_client(const vote_scope *scope, const char *text, qa_error *error)
{
    char command[VOTE_BYTES + 128];
    snprintf(command, sizeof(command), "print \"%s\"", text);
    return send(scope, (int32_t)scope->slot, command, error);
}

static bool append(const vote_scope *scope, const char *text, qa_error *error)
{
    qa_console *console;
    qa_command_context command = {.owner = scope->source_owner, .dialect = QA_CONSOLE_Q3,
                                  .origin = QA_COMMAND_SERVER};
    char line[VOTE_BYTES + 1];
    snprintf(line, sizeof(line), "%s\n", text);
    if (!application_native_q3_console_at(scope->owner->provider, &console, NULL, NULL))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 vote has no source console");
    return application_unified_q3_console(scope->owner->provider, false, line, error) &&
        qa_console_append(console, &command, line, error) && scope_live(scope, error);
}

static bool config(const vote_scope *scope, uint32_t index, const char *text, qa_error *error)
{
    uint64_t before, after;
    const char *current;
    if (!qa_q3_configstring_revision(scope->game, index, &before, error) ||
        !qa_q3_configstring_write(scope->game, index, text, error) || !scope_live(scope, error) ||
        !qa_q3_configstring_revision(scope->game, index, &after, error) ||
        !qa_q3_configstring_read(scope->game, index, &current, error)) return false;
    if (after < before || after - before > 1 || strcmp(current, text))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 vote configstring was superseded");
    return true;
}

static bool config_integer(const vote_scope *scope, uint32_t index, int32_t value, qa_error *error)
{
    char text[32];
    decimal(value, text);
    return config(scope, index, text, error);
}

static bool client(const vote_scope *scope, uint32_t slot, qa_q3_native_client *out,
    qa_q3_client_session *sess, qa_error *error)
{
    if (!qa_q3_client_slot_read(scope->game, slot, out, error)) return false;
    *sess = out->session;
    return true;
}

static bool flag(const vote_scope *scope, uint32_t slot, uint32_t set, uint32_t clear,
    qa_error *error)
{
    return qa_q3_client_player_flags_update(scope->game, slot, set, clear, error);
}

static bool live_cvar(const vote_scope *scope, const char *name, char text[VOTE_BYTES], qa_error *error)
{
    qa_cvars *cvars = application_native_q3_cvar_owner(scope->owner->provider, name);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    if (!value)
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 vote has no actual source cvar");
    size_t length = strlen(value->value);
    if (length >= VOTE_BYTES) length = VOTE_BYTES - 1;
    memcpy(text, value->value, length);
    text[length] = 0;
    return true;
}

static bool call_vote(vote_scope *scope, const qa_command_invocation *command, qa_error *error)
{
    vote_state *state = &scope->owner->state;
    source_vote *vote = &state->global;
    qa_q3_native_client caller;
    qa_q3_client_session sess;
    int32_t allowed;
    if (!application_native_q3_settings_integer(scope->owner->provider, "g_allowVote", &allowed, error) ||
        !client(scope, scope->slot, &caller, &sess, error)) return false;
    if (!allowed) return print_client(scope, "Voting not allowed here.\n", error);
    if (vote->time) return print_client(scope, "A vote is already in progress.\n", error);
    if (caller.vote_count >= 3) return print_client(scope, "You have called the maximum number of votes.\n", error);
    if (sess.team == 3) return print_client(scope, "Not allowed to call a vote as spectator.\n", error);
    char key[VOTE_BYTES], parameter[VOTE_BYTES];
    argument(command, 1, key, sizeof(key));
    argument(command, 2, parameter, sizeof(parameter));
    if (strchr(key, ';') || strchr(parameter, ';')) return print_client(scope, "Invalid vote string.\n", error);
    if (!named(key, "map_restart") && !named(key, "nextmap") && !named(key, "map") &&
        !named(key, "g_gametype") && !named(key, "kick") && !named(key, "clientkick") &&
        !named(key, "g_dowarmup") && !named(key, "timelimit") && !named(key, "fraglimit"))
        return print_client(scope, "Invalid vote string.\n", error) &&
            print_client(scope, "Vote commands are: map_restart, nextmap, map <mapname>, g_gametype <n>, kick <player>, clientkick <clientnum>, g_doWarmup, timelimit <time>, fraglimit <frags>.\n", error);
    if (state->execute_time) {
        state->execute_time = 0;
        if (!append(scope, vote->text, error)) return false;
    }
    char formatted[3 * VOTE_BYTES + 64];
    if (named(key, "g_gametype")) {
        static const char *const names[] = {"Free For All", "Tournament", "Single Player",
            "Team Deathmatch", "Capture the Flag", "One Flag CTF", "Overload", "Harvester"};
        int32_t type = integer(parameter);
        if (type < 0 || type == 2 || type >= 8) return print_client(scope, "Invalid gametype.\n", error);
        snprintf(formatted, sizeof(formatted), "%s %" PRId32, key, type);
        store_text(vote->text, formatted);
        snprintf(formatted, sizeof(formatted), "%s %s", key, names[type]);
        store_text(state->display, formatted);
    } else if (named(key, "map")) {
        char nextmap[VOTE_BYTES];
        if (!live_cvar(scope, "nextmap", nextmap, error)) return false;
        if (*nextmap) snprintf(formatted, sizeof(formatted), "%s %s; set nextmap \"%s\"", key, parameter, nextmap);
        else snprintf(formatted, sizeof(formatted), "%s %s", key, parameter);
        store_text(vote->text, formatted);
        memcpy(state->display, vote->text, sizeof(state->display));
    } else if (named(key, "nextmap")) {
        char nextmap[VOTE_BYTES];
        if (!live_cvar(scope, "nextmap", nextmap, error)) return false;
        if (!*nextmap) return print_client(scope, "nextmap not set.\n", error);
        memcpy(vote->text, "vstr nextmap", sizeof("vstr nextmap"));
        memcpy(state->display, vote->text, sizeof(state->display));
    } else {
        snprintf(formatted, sizeof(formatted), "%s \"%s\"", key, parameter);
        store_text(vote->text, formatted);
        memcpy(state->display, vote->text, sizeof(state->display));
    }
    char announcement[128];
    snprintf(announcement, sizeof(announcement), "print \"%s called a vote.\n\"", caller.netname);
    if (!send(scope, -1, announcement, error) || !qa_q3_source_clock(scope->game, &vote->time, error)) return false;
    vote->yes = 1;
    vote->no = 0;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot)
        if (!flag(scope, slot, 0, VOTE_FLAG, error)) return false;
    return flag(scope, scope->slot, VOTE_FLAG, 0, error) &&
        config_integer(scope, 8, vote->time, error) && config(scope, 9, state->display, error) &&
        config_integer(scope, 10, vote->yes, error) && config_integer(scope, 11, vote->no, error);
}

static void clean_name(const char *text, char out[36])
{
    size_t used = 0;
    for (size_t index = 0; index < 35 && text[index]; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (byte == '^' && index + 1 < 35 && text[index + 1] && text[index + 1] != '^') ++index;
        else if (byte >= 32 && byte <= 126) {
            if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
            out[used++] = (char)byte;
        }
    }
    out[used] = 0;
}

static bool team_parameter(const qa_command_invocation *command, char text[VOTE_BYTES], qa_error *error)
{
    size_t used = 0;
    text[0] = 0;
    for (size_t index = 2; index < command->argc; ++index) {
        if (index > 2) {
            if (used == VOTE_BYTES - 1)
                return application_fail(error, QA_ERROR_ARGUMENT, "team vote arguments overflow the source string buffer");
            text[used++] = ' ';
        }
        if (used >= VOTE_BYTES)
            return application_fail(error, QA_ERROR_ARGUMENT, "team vote arguments overflow the source string buffer");
        char part[VOTE_BYTES];
        argument(command, index, part, VOTE_BYTES - used);
        size_t length = strlen(part);
        memcpy(text + used, part, length);
        used += length;
        text[used] = 0;
    }
    return true;
}

static bool call_team_vote(vote_scope *scope, const qa_command_invocation *command, qa_error *error)
{
    qa_q3_native_client caller;
    qa_q3_client_session sess;
    if (!client(scope, scope->slot, &caller, &sess, error)) return false;
    if (sess.team != 1 && sess.team != 2) return true;
    uint32_t offset = (uint32_t)sess.team - 1;
    source_vote *vote = &scope->owner->state.teams[offset];
    int32_t allowed;
    if (!application_native_q3_settings_integer(scope->owner->provider, "g_allowVote", &allowed, error)) return false;
    if (!allowed) return print_client(scope, "Voting not allowed here.\n", error);
    if (vote->time) return print_client(scope, "A team vote is already in progress.\n", error);
    if (caller.team_vote_count >= 3)
        return print_client(scope, "You have called the maximum number of team votes.\n", error);
    char key[VOTE_BYTES], parameter[VOTE_BYTES];
    argument(command, 1, key, sizeof(key));
    if (!team_parameter(command, parameter, error)) return false;
    if (strchr(key, ';') || strchr(parameter, ';')) return print_client(scope, "Invalid vote string.\n", error);
    if (!named(key, "leader")) return print_client(scope, "Invalid vote string.\n", error) &&
        print_client(scope, "Team vote commands are: leader <player>.\n", error);
    qa_q3_player_state player;
    if (!qa_q3_player_read(scope->game, scope->actor, &player))
        return application_fail(error, QA_ERROR_NOT_FOUND, "team vote has no actual source player state");
    int32_t target = player.client_number;
    if (*parameter) {
        size_t digits = 0;
        while (digits < 3 && parameter[digits] >= '0' && parameter[digits] <= '9') ++digits;
        if (digits >= 3 || !parameter[digits]) {
            target = integer(parameter);
            char message[128], number[32];
            decimal(target, number);
            if (target < 0 || (uint32_t)target >= scope->maximum) {
                snprintf(message, sizeof(message), "Bad client slot: %s\n", number);
                return print_client(scope, message, error);
            }
            qa_q3_source_binding row;
            if (!qa_q3_source_binding_read(scope->game, (uint32_t)target, &row, error)) return false;
            if (!row.in_use) {
                snprintf(message, sizeof(message), "Client %s is not active\n", number);
                return print_client(scope, message, error);
            }
        } else {
            char wanted[36];
            clean_name(parameter, wanted);
            for (target = 0; (uint32_t)target < scope->maximum; ++target) {
                qa_q3_native_client other;
                qa_q3_client_session other_session;
                char cleaned[36];
                if (!client(scope, (uint32_t)target, &other, &other_session, error)) return false;
                if (other.connected == QA_Q3_CLIENT_DISCONNECTED || other_session.team != sess.team) continue;
                clean_name(other.netname, cleaned);
                if (!strcmp(wanted, cleaned)) break;
            }
            if ((uint32_t)target >= scope->maximum) {
                char message[VOTE_BYTES + 64];
                snprintf(message, sizeof(message), "%s is not a valid player on your team.\n", parameter);
                return print_client(scope, message, error);
            }
        }
    }
    char formatted[VOTE_BYTES + 64], number[32];
    decimal(target, number);
    snprintf(formatted, sizeof(formatted), "%s %s", key, number);
    store_text(vote->text, formatted);
    char announcement[128];
    snprintf(announcement, sizeof(announcement), "print \"%s called a team vote.\n\"", caller.netname);
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client other;
        qa_q3_client_session other_session;
        if (!client(scope, slot, &other, &other_session, error)) return false;
        if (other.connected != QA_Q3_CLIENT_DISCONNECTED && other_session.team == sess.team &&
            !send(scope, (int32_t)slot, announcement, error)) return false;
    }
    if (!qa_q3_source_clock(scope->game, &vote->time, error)) return false;
    vote->yes = 1;
    vote->no = 0;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client other;
        qa_q3_client_session other_session;
        if (!client(scope, slot, &other, &other_session, error)) return false;
        if (other_session.team == sess.team && !flag(scope, slot, 0, TEAM_VOTE_FLAG, error)) return false;
    }
    return flag(scope, scope->slot, TEAM_VOTE_FLAG, 0, error) &&
        config_integer(scope, 12 + offset, vote->time, error) && config(scope, 14 + offset, vote->text, error) &&
        config_integer(scope, 16 + offset, vote->yes, error) && config_integer(scope, 18 + offset, vote->no, error);
}

static bool ballot(vote_scope *scope, const qa_command_invocation *command,
    bool team_vote, qa_error *error)
{
    qa_q3_native_client caller;
    qa_q3_client_session sess;
    qa_q3_player_state player;
    if (!client(scope, scope->slot, &caller, &sess, error)) return false;
    if (team_vote && sess.team != 1 && sess.team != 2) return true;
    uint32_t offset = team_vote ? (uint32_t)sess.team - 1 : 0;
    source_vote *vote = team_vote ? &scope->owner->state.teams[offset] : &scope->owner->state.global;
    uint32_t voted = team_vote ? TEAM_VOTE_FLAG : VOTE_FLAG;
    if (!vote->time) return print_client(scope,
        team_vote ? "No team vote in progress.\n" : "No vote in progress.\n", error);
    if (!qa_q3_player_read(scope->game, scope->actor, &player))
        return application_fail(error, QA_ERROR_NOT_FOUND, "vote ballot has no actual source player state");
    if (player.flags & voted) return print_client(scope,
        team_vote ? "Team vote already cast.\n" : "Vote already cast.\n", error);
    if (!team_vote && sess.team == 3) return print_client(scope, "Not allowed to vote as spectator.\n", error);
    if (!print_client(scope, team_vote ? "Team vote cast.\n" : "Vote cast.\n", error) ||
        !flag(scope, scope->slot, voted, 0, error)) return false;
    char text[64];
    argument(command, 1, text, sizeof(text));
    bool yes = text[0] == 'y' || (text[0] && (text[1] == 'Y' || text[1] == '1'));
    int32_t *count = yes ? &vote->yes : &vote->no;
    *count = signed_bits((uint32_t)*count + 1u);
    return config_integer(scope, team_vote ? (yes ? 16 : 18) + offset : yes ? 10 : 11, *count, error);
}

bool application_native_q3_votes_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!command || !handled || command->argc > 1024 || (command->argc && !command->argv))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 vote requires source command arguments");
    *handled = false;
    for (size_t index = 0; index < command->argc; ++index)
        if (!command->argv[index]) return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 vote argument is absent");
    if (!command->argc) return true;
    char name[VOTE_BYTES];
    argument(command, 0, name, sizeof(name));
    bool global_call = named(name, "callvote"), team_call = named(name, "callteamvote");
    bool global_ballot = named(name, "vote"), team_ballot = named(name, "teamvote");
    if (!global_call && !team_call && !global_ballot && !team_ballot) return true;
    if (!actor.registry || !actor.generation)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 vote requires its physical source client");
    vote_scope scope;
    if (!scope_begin(provider, actor, VOTE_COMMAND, &scope, error)) return false;
    *handled = true;
    bool okay = global_call ? call_vote(&scope, command, error) : team_call ? call_team_vote(&scope, command, error) :
        ballot(&scope, command, team_ballot, error);
    if (okay) okay = scope_live(&scope, error);
    scope_end(&scope);
    return okay;
}

static bool print_team(const vote_scope *scope, int32_t team, const char *text, qa_error *error)
{
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client other;
        qa_q3_client_session sess;
        if (!client(scope, slot, &other, &sess, error)) return false;
        if (sess.team == team && !send(scope, (int32_t)slot, text, error)) return false;
    }
    return true;
}

static bool userinfo_changed(const vote_scope *scope, uint32_t slot, qa_error *error)
{
    return application_native_q3_client_userinfo_changed_slot(scope->owner->provider, slot, error) &&
        scope_live(scope, error);
}

static bool leader(const vote_scope *scope, int32_t team, int32_t target, qa_error *error)
{
    if (target < 0 || (uint32_t)target >= scope->maximum)
        return application_fail(error, QA_ERROR_ARGUMENT, "team vote leader is outside the actual source client table");
    qa_q3_native_client selected;
    qa_q3_client_session selected_session;
    char message[VOTE_BYTES];
    if (!client(scope, (uint32_t)target, &selected, &selected_session, error)) return false;
    if (selected.connected == QA_Q3_CLIENT_DISCONNECTED) {
        snprintf(message, sizeof(message), "print \"%s is not connected\n\"", selected.netname);
        return print_team(scope, team, message, error);
    }
    if (selected_session.team != team) {
        snprintf(message, sizeof(message), "print \"%s is not on the team anymore\n\"", selected.netname);
        return print_team(scope, team, message, error);
    }
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client other;
        qa_q3_client_session sess;
        if (!client(scope, slot, &other, &sess, error)) return false;
        if (sess.team == team && sess.team_leader) {
            sess.team_leader = 0;
            if (!qa_q3_client_session_slot_write(scope->game, slot,
                    QA_Q3_CLIENT_SESSION_LEADER, &sess, error) || !userinfo_changed(scope, slot, error)) return false;
        }
    }
    selected_session.team_leader = 1;
    if (!qa_q3_client_session_slot_write(scope->game, (uint32_t)target,
            QA_Q3_CLIENT_SESSION_LEADER, &selected_session, error) || !userinfo_changed(scope, (uint32_t)target, error) ||
        !client(scope, (uint32_t)target, &selected, &selected_session, error)) return false;
    snprintf(message, sizeof(message), "print \"%s is the new team leader\n\"", selected.netname);
    return print_team(scope, team, message, error);
}

bool application_native_q3_votes_set_leader(application_provider *provider,
    int32_t team, int32_t source_client, qa_error *error)
{
    vote_scope scope;
    if (!scope_begin(provider, (qa_actor_id){0}, VOTE_LEADER, &scope, error)) return false;
    bool okay = leader(&scope, team, source_client, error) && scope_live(&scope, error);
    scope_end(&scope);
    return okay;
}

bool application_native_q3_votes_check_team_leader(application_provider *provider,
    int32_t team, qa_error *error)
{
    vote_scope scope;
    if (!scope_begin(provider, (qa_actor_id){0}, VOTE_LEADER, &scope, error)) return false;
    bool okay = true, found = false;
    for (uint32_t slot = 0; okay && slot < scope.maximum; ++slot) {
        qa_q3_native_client persistent;
        qa_q3_client_session sess;
        okay = client(&scope, slot, &persistent, &sess, error);
        if (okay && sess.team == team && sess.team_leader) { found = true; break; }
    }
    if (okay && !found) {
        for (uint32_t slot = 0; okay && slot < scope.maximum; ++slot) {
            qa_q3_native_client persistent;
            qa_q3_client_session sess;
            uint32_t server_flags;
            okay = client(&scope, slot, &persistent, &sess, error);
            if (!okay || sess.team != team) continue;
            okay = qa_q3_client_server_flags(scope.game, slot, &server_flags, error);
            if (!okay || (server_flags & 8u)) continue;
            sess.team_leader = 1;
            okay = qa_q3_client_session_slot_write(scope.game, slot,
                QA_Q3_CLIENT_SESSION_LEADER, &sess, error);
            break;
        }
        /* The second source loop runs even when the first selected a human. */
        for (uint32_t slot = 0; okay && slot < scope.maximum; ++slot) {
            qa_q3_native_client persistent;
            qa_q3_client_session sess;
            okay = client(&scope, slot, &persistent, &sess, error);
            if (!okay || sess.team != team) continue;
            sess.team_leader = 1;
            okay = qa_q3_client_session_slot_write(scope.game, slot,
                QA_Q3_CLIENT_SESSION_LEADER, &sess, error);
            break;
        }
    }
    if (okay) okay = scope_live(&scope, error);
    scope_end(&scope);
    return okay;
}

static bool check_global(vote_scope *scope, int32_t now, int32_t voting, qa_error *error)
{
    vote_state *state = &scope->owner->state;
    source_vote *vote = &state->global;
    if (state->execute_time && state->execute_time < now) {
        state->execute_time = 0;
        if (!append(scope, vote->text, error)) return false;
    }
    if (!vote->time) return true;
    if (signed_bits((uint32_t)now - (uint32_t)vote->time) >= 30000) {
        if (!send(scope, -1, "print \"Vote failed.\n\"", error)) return false;
    } else if (vote->yes > voting / 2) {
        if (!send(scope, -1, "print \"Vote passed.\n\"", error)) return false;
        state->execute_time = signed_bits((uint32_t)now + 3000u);
    } else if (vote->no >= voting / 2) {
        if (!send(scope, -1, "print \"Vote failed.\n\"", error)) return false;
    } else return true;
    vote->time = 0;
    return config(scope, 8, "", error);
}

static bool check_team(vote_scope *scope, uint32_t offset, int32_t now, int32_t voting, qa_error *error)
{
    source_vote *vote = &scope->owner->state.teams[offset];
    if (!vote->time) return true;
    if (signed_bits((uint32_t)now - (uint32_t)vote->time) >= 30000) {
        if (!send(scope, -1, "print \"Team vote failed.\n\"", error)) return false;
    } else if (vote->yes > voting / 2) {
        if (!send(scope, -1, "print \"Team vote passed.\n\"", error)) return false;
        if (!strncmp(vote->text, "leader", 6)) {
            size_t length = strlen(vote->text);
            if (!leader(scope, (int32_t)offset + 1, integer(vote->text + (length < 7 ? length : 7)), error)) return false;
        } else if (!append(scope, vote->text, error)) return false;
    } else if (vote->no >= voting / 2) {
        if (!send(scope, -1, "print \"Team vote failed.\n\"", error)) return false;
    } else return true;
    vote->time = 0;
    return config(scope, 12 + offset, "", error);
}

bool application_native_q3_votes_frame(application_provider *provider, qa_error *error)
{
    vote_scope scope;
    if (!scope_begin(provider, (qa_actor_id){0}, VOTE_FRAME, &scope, error)) return false;
    int32_t now;
    qa_q3_source_client_counts counts;
    bool okay = qa_q3_source_clock(scope.game, &now, error) &&
        qa_q3_source_client_counts_read(scope.game, &counts, error) &&
        check_global(&scope, now, counts.num_voting, error) &&
        check_team(&scope, 0, now, counts.num_team_voting[0], error) &&
        check_team(&scope, 1, now, counts.num_team_voting[1], error) && scope_live(&scope, error);
    scope_end(&scope);
    return okay;
}

bool application_native_q3_votes_create(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->application ||
        !provider->product || provider->native_q3_votes)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes require an empty source holder");
    struct application_native_q3_votes *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 source votes");
    owner->provider = provider;
    provider->native_q3_votes = owner;
    return true;
}

bool application_native_q3_votes_idle(const application_provider *provider)
{
    const struct application_native_q3_votes *owner = owner_at(provider);
    return !owner || owner->operation == VOTE_IDLE;
}

bool application_native_q3_votes_bound(const application_provider *provider)
{
    const struct application_native_q3_votes *owner = owner_at(provider);
    qa_application *app = provider ? provider->application : NULL;
    return owner && owner->provider == provider && owner->state.initialized && app &&
        provider->constructed && provider->attached && !provider->close_pending && provider->state.q3 &&
        !app->destroy_requested && (application_world_provider(app, QA_ROLE_ENTITIES, "") == provider ||
            application_native_q3_source_command_entered(provider)) &&
        application_native_q3_settings_initialized(provider) &&
        application_native_q3_console_settings_bound(provider);
}

bool application_native_q3_votes_destroy(application_provider *provider, qa_error *error)
{
    struct application_native_q3_votes *owner = owner_at(provider);
    if (!provider || !application_native_q3_votes_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes are borrowed");
    free(owner);
    provider->native_q3_votes = NULL;
    return true;
}

bool application_native_q3_votes_reset(application_provider *provider, qa_error *error)
{
    struct application_native_q3_votes *owner = owner_at(provider);
    if (!owner || owner->operation != VOTE_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes are not at source Init");
    owner->state = (vote_state){.initialized = true};
    return true;
}

static bool text_field(qa_source_save_io *io, char text[VOTE_BYTES])
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = reading ? 0 : strlen(text);
    if (!qa_source_save_count(io, &length, VOTE_BYTES - 1) ||
        !qa_source_save_bytes(io, text, length) || memchr(text, 0, length)) return false;
    if (reading) text[length] = 0;
    return true;
}

static bool state_fields(qa_source_save_io *io, vote_state *state)
{
    if (!qa_source_save_bool(io, &state->initialized) ||
        !qa_source_save_i32(io, &state->execute_time) || !text_field(io, state->display)) return false;
    source_vote *votes[3] = {&state->global, &state->teams[0], &state->teams[1]};
    for (size_t row = 0; row < 3; ++row) {
        source_vote *vote = votes[row];
        if (!qa_source_save_i32(io, &vote->time) || !qa_source_save_i32(io, &vote->yes) ||
            !qa_source_save_i32(io, &vote->no) || !text_field(io, vote->text)) return false;
        if (!state->initialized && (vote->time || vote->yes || vote->no || *vote->text))
            return application_fail(io->error, QA_ERROR_FORMAT, "uninitialized native Q3 votes contain source state");
    }
    if (!state->initialized && (state->execute_time || *state->display))
        return application_fail(io->error, QA_ERROR_FORMAT, "uninitialized native Q3 votes contain source state");
    return true;
}

static bool header(qa_source_save_io *io, const application_provider *provider)
{
    uint8_t magic[4] = {'Q', 'A', 'G', 'V'};
    uint32_t version = 1;
    uint32_t product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    uint32_t expected_product = product;
    uint64_t owner = provider->owner;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAGV", 4) &&
        qa_source_save_u32(io, &version) && version == 1 &&
        qa_source_save_u32(io, &product) && product == expected_product &&
        qa_source_save_u64(io, &owner) && owner == provider->owner;
}

bool application_native_q3_votes_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    struct application_native_q3_votes *owner = owner_at(provider);
    if (!owner || !out || owner->operation != VOTE_IDLE)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes are not at a continuation boundary");
    owner->operation = VOTE_CAPTURE;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && header(&io, provider) &&
        state_fields(&io, &owner->state) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    owner->operation = VOTE_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 vote continuation");
    return okay;
}

bool application_native_q3_votes_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    struct application_native_q3_votes *owner = owner_at(provider);
    if (!owner || owner->operation != VOTE_IDLE || owner->state.initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 votes require an empty candidate holder");
    owner->operation = VOTE_IMPORT;
    qa_source_save_io io = {0};
    vote_state candidate = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && header(&io, provider) &&
        state_fields(&io, &candidate) && qa_source_save_finish(&io, NULL);
    if (okay) owner->state = candidate;
    qa_source_save_dispose(&io);
    owner->operation = VOTE_IDLE;
    if (!okay && error && error->code == QA_OK)
        application_fail(error, QA_ERROR_FORMAT, "invalid native Q3 vote continuation");
    return okay;
}
