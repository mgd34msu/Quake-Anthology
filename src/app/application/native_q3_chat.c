#include "native_q3_chat.h"
#include "native_q3_console.h"
#include "native_q3_wire_state.h"
#include "native_q3_log.h"
#include "native_q3_settings.h"
#include "qa/game_q3_clients.h"
#include "native_q3_team_status.h"
#include "qa/game_q3_source.h"

#include <stdio.h>
#include <string.h>

enum { CHAT_ARGUMENT_BYTES = 1024, CHAT_TEXT_BYTES = 150,
       CHAT_NAME_BYTES = 64,
       CHAT_FORMAT_BYTES = CHAT_ARGUMENT_BYTES + 2 * QA_Q3_NATIVE_NETNAME + 64,
       CHAT_BOT_FLAG = 8 };

typedef enum chat_mode { CHAT_ALL, CHAT_TEAM, CHAT_TELL } chat_mode;
typedef struct chat_scope {
    qa_application *application;
    application_provider *provider;
    qa_q3_game *game;
    qa_actor_id actor;
    uint64_t publication_generation, command_generation, map_revision;
    uint32_t slot, max_clients;
    int32_t game_type, time;
    bool dedicated;
} chat_scope;
typedef struct chat_client {
    qa_q3_native_client persistent;
    uint32_t server_flags;
    bool inuse;
} chat_client;

static bool same_command(const char *text, const char *expected)
{
    while (*text && *expected) {
        unsigned char byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z') byte = (unsigned char)(byte + ('a' - 'A'));
        if (byte != (unsigned char)*expected++) return false;
    }
    return !*text && !*expected;
}

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length + 1 < capacity && text[length]) ++length;
    return length;
}

static void copy_bounded(char *out, size_t capacity, const char *text)
{
    size_t length = bounded_length(text, capacity);
    memcpy(out, text, length);
    out[length] = 0;
}

static const char *argument(const qa_command_invocation *command, size_t index)
{
    return index < command->argc ? command->argv[index] : "";
}

static void concat_arguments(const qa_command_invocation *command, size_t first,
    char out[CHAT_ARGUMENT_BYTES])
{
    size_t used = 0;
    for (size_t index = first; index < command->argc; ++index) {
        const char *value = command->argv[index];
        size_t length = bounded_length(value, CHAT_ARGUMENT_BYTES);
        if (used + length >= CHAT_ARGUMENT_BYTES - 1) break;
        memcpy(out + used, value, length);
        used += length;
        if (index != command->argc - 1) out[used++] = ' ';
    }
    out[used] = 0;
}

/* bg_lib treats high byte characters as signed when skipping whitespace and
 * wraps every decimal digit operation, including arbitrarily long prefixes. */
static int32_t command_integer(const char *text)
{
    char bounded[CHAT_ARGUMENT_BYTES];
    size_t length = bounded_length(text, sizeof(bounded));
    memcpy(bounded, text, length);
    bounded[length] = 0;
    const unsigned char *cursor = (const unsigned char *)bounded;
    while (*cursor && (*cursor < 128 ? (int)*cursor : (int)*cursor - 256) <= 32)
        ++cursor;
    bool negative = *cursor == '-';
    if (*cursor == '-' || *cursor == '+') ++cursor;
    uint32_t value = 0;
    while (*cursor >= '0' && *cursor <= '9')
        value = value * 10u + (uint32_t)(*cursor++ - '0');
    if (negative) value = 0u - value;
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static bool scope_live(const chat_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    qa_application *application = scope->application;
    uint32_t slot;
    if (application->destroy_requested || provider->application != application ||
        provider->kind != APPLICATION_PROVIDER_Q3 || provider->state.q3 != scope->game ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        application->publication_generation != scope->publication_generation ||
        application->command_generation != scope->command_generation ||
        application->map_revision != scope->map_revision ||
        application_world_provider(application, QA_ROLE_ENTITIES, "") != provider ||
        !qa_q3_native_client_slot(scope->game, scope->actor, &slot, error) ||
        slot != scope->slot)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 chat source client has retired");
    int32_t time;
    return qa_q3_source_clock(scope->game, &time, error) &&
        (time == scope->time || application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 chat callback advanced its source clock"));
}

static bool scope_begin(qa_application *application, application_provider *provider,
    qa_actor_id actor, const qa_command_invocation *command, chat_scope *scope,
    qa_error *error)
{
    if (!application || !provider || provider->application != application ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !command || command->argc > 1024 || (command->argc && !command->argv))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native Q3 chat requires its actual source invocation");
    for (size_t index = 0; index < command->argc; ++index)
        if (!command->argv[index])
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "native Q3 chat argument is missing");
    *scope = (chat_scope){.application = application, .provider = provider,
        .game = provider->state.q3, .actor = actor,
        .publication_generation = application->publication_generation,
        .command_generation = application->command_generation,
        .map_revision = application->map_revision};
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &scope->slot, error) ||
        !qa_q3_source_max_clients(provider->state.q3, &scope->max_clients, error) ||
        !qa_q3_source_clock(scope->game, &scope->time, error) ||
        !application_native_q3_settings_integer(provider, "g_gametype", &scope->game_type, error)) return false;
    qa_cvars *cvars = application_native_q3_cvar_owner(provider, "dedicated");
    const qa_cvar_view *dedicated = cvars ? qa_cvars_find(cvars, "dedicated") : NULL;
    if (!dedicated)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 chat has no source dedicated setting");
    scope->dedicated = dedicated->integer != 0;
    return scope_live(scope, error);
}

static bool client_read(const chat_scope *scope, uint32_t slot, chat_client *out,
    qa_error *error)
{
    qa_q3_game *game = scope->provider->state.q3;
    qa_q3_source_binding binding;
    *out = (chat_client){0};
    if (!qa_q3_source_binding_read(game, slot, &binding, error) ||
        !qa_q3_client_slot_read(game, slot, &out->persistent, error) ||
        !qa_q3_client_server_flags(game, slot, &out->server_flags, error)) return false;
    out->inuse = binding.in_use;
    return true;
}

static bool client_team(const chat_scope *scope, uint32_t slot, int32_t *team,
    qa_error *error)
{
    qa_q3_client_session sess;
    if (!qa_q3_client_session_slot_read(scope->provider->state.q3, slot, &sess, error)) return false;
    *team = sess.team;
    return true;
}

static bool source_log(const chat_scope *scope, const char *text, qa_error *error)
{
    return application_native_q3_log(scope->provider, text, error) &&
           scope_live(scope, error);
}

static bool source_print(const chat_scope *scope, const char *text, qa_error *error)
{
    return application_native_q3_console_print(scope->provider, text, error) &&
           scope_live(scope, error);
}

static bool send(const chat_scope *scope, uint32_t slot, const char *text,
    qa_error *error)
{
    return application_native_q3_send_command(scope->provider, (int32_t)slot, text,
                                             error) && scope_live(scope, error);
}

static bool location_message(const chat_scope *scope, char text[CHAT_NAME_BYTES],
    bool *found, qa_error *error)
{
    return application_native_q3_team_location(scope->provider,
        scope->actor, text, CHAT_NAME_BYTES,
        found, error) && scope_live(scope, error);
}

static bool say_to(const chat_scope *scope, uint32_t slot, chat_mode mode,
    char color, const char *name, const char *text, qa_error *error)
{
    chat_client target;
    if (!client_read(scope, slot, &target, error)) return false;
    if (!target.inuse || target.persistent.connected != QA_Q3_CLIENT_CONNECTED)
        return true;
    if (mode == CHAT_TEAM || scope->game_type == 1) {
        int32_t sender_team, target_team;
        if (!client_team(scope, scope->slot, &sender_team, error) ||
            !client_team(scope, slot, &target_team, error)) return false;
        if (mode == CHAT_TEAM && (scope->game_type < 3 || sender_team != target_team))
            return true;
        if (scope->game_type == 1 && target_team == 0 && sender_team != 0) return true;
    }
    char output[CHAT_FORMAT_BYTES];
    snprintf(output, sizeof(output), "%s \"%s^%c%s\"",
             mode == CHAT_TEAM ? "tchat" : "chat", name, color, text);
    return send(scope, slot, output, error);
}

static bool say(const chat_scope *scope, int32_t target_slot, chat_mode mode,
    const char *chat_text, qa_error *error)
{
    chat_client sender;
    if (!client_read(scope, scope->slot, &sender, error)) return false;
    if (scope->game_type < 3 && mode == CHAT_TEAM) mode = CHAT_ALL;
    char name[CHAT_NAME_BYTES], output[CHAT_FORMAT_BYTES];
    char color;
    if (mode == CHAT_ALL) {
        snprintf(output, sizeof(output), "say: %s: %s\n",
                 sender.persistent.netname, chat_text);
        if (!source_log(scope, output, error)) return false;
        snprintf(output, sizeof(output), "%s^7\031: ", sender.persistent.netname);
        copy_bounded(name, sizeof(name), output);
        color = '2';
    } else {
        char location[CHAT_NAME_BYTES];
        bool found = false;
        if (mode == CHAT_TEAM) {
            snprintf(output, sizeof(output), "sayteam: %s: %s\n",
                     sender.persistent.netname, chat_text);
            if (!source_log(scope, output, error) ||
                !location_message(scope, location, &found, error)) return false;
        } else if (target_slot >= 0 && scope->game_type >= 3) {
            int32_t sender_team, target_team;
            if (!client_team(scope, scope->slot, &sender_team, error) ||
                !client_team(scope, (uint32_t)target_slot, &target_team, error)) return false;
            if (sender_team == target_team &&
                !location_message(scope, location, &found, error)) return false;
        }
        const char *left = mode == CHAT_TEAM ? "(" : "[";
        const char *right = mode == CHAT_TEAM ? ")" : "]";
        if (found)
            snprintf(output, sizeof(output), "\031%s%s^7\031%s (%s)\031: ",
                     left, sender.persistent.netname, right, location);
        else
            snprintf(output, sizeof(output), "\031%s%s^7\031%s\031: ",
                     left, sender.persistent.netname, right);
        copy_bounded(name, sizeof(name), output);
        color = mode == CHAT_TEAM ? '5' : '6';
    }
    char text[CHAT_TEXT_BYTES];
    size_t length = bounded_length(chat_text, sizeof(text));
    memcpy(text, chat_text, length);
    text[length] = 0;
    if (target_slot >= 0)
        return say_to(scope, (uint32_t)target_slot, mode, color, name, text, error);
    if (scope->dedicated) {
        snprintf(output, sizeof(output), "%s%s\n", name, text);
        if (!source_print(scope, output, error)) return false;
    }
    for (uint32_t slot = 0; slot < scope->max_clients; ++slot)
        if (!say_to(scope, slot, mode, color, name, text, error)) return false;
    return true;
}

static bool say_command(const chat_scope *scope, const qa_command_invocation *command,
    chat_mode mode, bool include_command, qa_error *error)
{
    if (command->argc < 2 && !include_command) return true;
    char text[CHAT_ARGUMENT_BYTES];
    concat_arguments(command, include_command ? 0 : 1, text);
    return say(scope, -1, mode, text, error);
}

static bool tell(const chat_scope *scope, const qa_command_invocation *command,
    qa_error *error)
{
    if (command->argc < 2) return true;
    int32_t slot = command_integer(argument(command, 1));
    if (slot < 0 || (uint32_t)slot >= scope->max_clients) return true;
    chat_client sender, target;
    if (!client_read(scope, (uint32_t)slot, &target, error)) return false;
    if (!target.inuse) return true;
    if (!client_read(scope, scope->slot, &sender, error)) return false;
    char text[CHAT_ARGUMENT_BYTES], output[CHAT_FORMAT_BYTES];
    concat_arguments(command, 2, text);
    snprintf(output, sizeof(output), "tell: %s to %s: %s\n",
             sender.persistent.netname, target.persistent.netname, text);
    if (!source_log(scope, output, error) ||
        !say(scope, slot, CHAT_TELL, text, error)) return false;
    if ((uint32_t)slot != scope->slot && !(sender.server_flags & CHAT_BOT_FLAG))
        return say(scope, (int32_t)scope->slot, CHAT_TELL, text, error);
    return true;
}

static bool voice_to(const chat_scope *scope, uint32_t slot, chat_mode mode,
    const char *id, bool voice_only, qa_error *error)
{
    chat_client target;
    if (!client_read(scope, slot, &target, error)) return false;
    if (!target.inuse) return true;
    if (mode == CHAT_TEAM) {
        int32_t sender_team, target_team;
        if (!client_team(scope, scope->slot, &sender_team, error) ||
            !client_team(scope, slot, &target_team, error)) return false;
        if (scope->game_type < 3 || sender_team != target_team) return true;
    }
    if (scope->game_type == 1) return true;
    const char *name = mode == CHAT_TEAM ? "vtchat" : mode == CHAT_TELL ? "vtell" : "vchat";
    int color = mode == CHAT_TEAM ? 53 : mode == CHAT_TELL ? 54 : 50;
    char output[CHAT_FORMAT_BYTES];
    snprintf(output, sizeof(output), "%s %d %u %d %s", name, voice_only ? 1 : 0,
             scope->slot, color, id);
    return send(scope, slot, output, error);
}

static bool voice(const chat_scope *scope, int32_t target_slot, chat_mode mode,
    const char *id, bool voice_only, qa_error *error)
{
    if (scope->game_type < 3 && mode == CHAT_TEAM) mode = CHAT_ALL;
    if (target_slot >= 0)
        return voice_to(scope, (uint32_t)target_slot, mode, id, voice_only, error);
    if (scope->dedicated) {
        chat_client sender;
        if (!client_read(scope, scope->slot, &sender, error)) return false;
        char output[CHAT_FORMAT_BYTES];
        snprintf(output, sizeof(output), "voice: %s %s\n", sender.persistent.netname, id);
        if (!source_print(scope, output, error)) return false;
    }
    for (uint32_t slot = 0; slot < scope->max_clients; ++slot)
        if (!voice_to(scope, slot, mode, id, voice_only, error)) return false;
    return true;
}

static bool voice_command(const chat_scope *scope, const qa_command_invocation *command,
    chat_mode mode, bool voice_only, qa_error *error)
{
    if (command->argc < 2) return true;
    char id[CHAT_ARGUMENT_BYTES];
    concat_arguments(command, 1, id);
    return voice(scope, -1, mode, id, voice_only, error);
}

static bool voice_tell(const chat_scope *scope, const qa_command_invocation *command,
    bool voice_only, qa_error *error)
{
    if (command->argc < 2) return true;
    int32_t slot = command_integer(argument(command, 1));
    if (slot < 0 || (uint32_t)slot >= scope->max_clients) return true;
    chat_client sender, target;
    if (!client_read(scope, (uint32_t)slot, &target, error)) return false;
    if (!target.inuse) return true;
    if (!client_read(scope, scope->slot, &sender, error)) return false;
    char id[CHAT_ARGUMENT_BYTES], output[CHAT_FORMAT_BYTES];
    concat_arguments(command, 2, id);
    snprintf(output, sizeof(output), "vtell: %s to %s: %s\n",
             sender.persistent.netname, target.persistent.netname, id);
    if (!source_log(scope, output, error) ||
        !voice(scope, slot, CHAT_TELL, id, voice_only, error)) return false;
    if ((uint32_t)slot != scope->slot && !(sender.server_flags & CHAT_BOT_FLAG))
        return voice(scope, (int32_t)scope->slot, CHAT_TELL, id, voice_only, error);
    return true;
}

static bool taunt_pair(const chat_scope *scope, uint32_t slot, const char *id,
    qa_error *error)
{
    uint32_t flags;
    if (!qa_q3_client_server_flags(scope->provider->state.q3, slot, &flags, error))
        return false;
    if (!(flags & CHAT_BOT_FLAG) &&
        !voice(scope, (int32_t)slot, CHAT_TELL, id, false, error)) return false;
    if (!qa_q3_client_server_flags(scope->provider->state.q3, scope->slot, &flags, error))
        return false;
    return (flags & CHAT_BOT_FLAG) ||
           voice(scope, (int32_t)scope->slot, CHAT_TELL, id, false, error);
}

static bool voice_taunt(const chat_scope *scope, qa_error *error)
{
    qa_q3_game *game = scope->provider->state.q3;
    qa_q3_client_taunt sender;
    if (!qa_q3_client_taunt_read(game, scope->slot, &sender, error)) return false;
    if (sender.enemy_source_present) {
        uint32_t slot = (uint32_t)sender.enemy_source_slot;
        if (slot < 64) {
            qa_q3_client_taunt target;
            if (!qa_q3_client_taunt_read(game, slot, &target, error)) return false;
            if (target.last_killed_client == (int32_t)scope->slot)
                return taunt_pair(scope, slot, "death_insult", error) &&
                       qa_q3_client_taunt_enemy_clear(game, scope->actor, error);
        }
    }
    if (sender.last_killed_client >= 0 && sender.last_killed_client != (int32_t)scope->slot) {
        if ((uint32_t)sender.last_killed_client >= QA_Q3_SOURCE_ENTITIES)
            return application_fail(error, QA_ERROR_FORMAT,
                                    "Q3 voice taunt victim exceeds the source entity pool");
        if (sender.last_killed_client < 64) {
            qa_q3_client_taunt target;
            uint32_t slot = (uint32_t)sender.last_killed_client;
            if (!qa_q3_client_taunt_read(game, slot, &target, error)) return false;
            const char *id = target.last_hurt_mod == 2 ? "kill_gauntlet" : "kill_insult";
            return taunt_pair(scope, slot, id, error) &&
                   qa_q3_client_taunt_kill_clear(game, scope->actor, error);
        }
    }
    if (scope->game_type >= 3) {
        int32_t sender_team, now;
        if (!client_team(scope, scope->slot, &sender_team, error) ||
            !qa_q3_source_clock(game, &now, error)) return false;
        for (uint32_t slot = 0; slot < 64; ++slot) {
            if (slot == scope->slot) continue;
            int32_t target_team;
            qa_q3_client_taunt target;
            if (!client_team(scope, slot, &target_team, error) ||
                !qa_q3_client_taunt_read(game, slot, &target, error)) return false;
            if (target_team == sender_team && target.reward_time_ms > now)
                return taunt_pair(scope, slot, "praise", error);
        }
    }
    return voice(scope, -1, CHAT_ALL, "taunt", false, error);
}

bool application_native_q3_chat_command(qa_application *application,
    application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!handled || !command || (command->argc && (!command->argv || !command->argv[0])))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid native Q3 chat dispatch");
    *handled = false;
    const char *name = argument(command, 0);
    if (!same_command(name, "say") && !same_command(name, "say_team") &&
        !same_command(name, "tell") && !same_command(name, "vsay") &&
        !same_command(name, "vsay_team") && !same_command(name, "vtell") &&
        !same_command(name, "vosay") && !same_command(name, "vosay_team") &&
        !same_command(name, "votell") && !same_command(name, "vtaunt")) return true;
    *handled = true;
    chat_scope scope;
    if (!scope_begin(application, provider, actor, command, &scope, error)) return false;
    if (same_command(name, "say")) return say_command(&scope, command, CHAT_ALL, false, error);
    if (same_command(name, "say_team")) return say_command(&scope, command, CHAT_TEAM, false, error);
    if (same_command(name, "tell")) return tell(&scope, command, error);
    if (same_command(name, "vsay")) return voice_command(&scope, command, CHAT_ALL, false, error);
    if (same_command(name, "vsay_team")) return voice_command(&scope, command, CHAT_TEAM, false, error);
    if (same_command(name, "vtell")) return voice_tell(&scope, command, false, error);
    if (same_command(name, "vosay")) return voice_command(&scope, command, CHAT_ALL, true, error);
    if (same_command(name, "vosay_team")) return voice_command(&scope, command, CHAT_TEAM, true, error);
    if (same_command(name, "votell")) return voice_tell(&scope, command, true, error);
    return voice_taunt(&scope, error);
}

bool application_native_q3_chat_intermission(qa_application *application,
    application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, qa_error *error)
{
    chat_scope scope;
    return scope_begin(application, provider, actor, command, &scope, error) &&
           say_command(&scope, command, CHAT_ALL, true, error);
}

bool application_native_q3_game_command(qa_application *application,
    application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, qa_error *error)
{
    static const char *const orders[] = {"hold your position", "hold this position",
        "come here", "cover me", "guard location", "search and destroy", "report"};
    chat_scope scope;
    if (!scope_begin(application, provider, actor, command, &scope, error)) return false;
    int32_t slot = command_integer(argument(command, 1));
    int32_t order = command_integer(argument(command, 2));
    if (slot < 0 || slot >= 64 || order < 0 || order > 7) return true;
    if (order == 7)
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q3 gc order 7 exceeds the source order table");
    return say(&scope, slot, CHAT_TELL, orders[order], error) &&
           say(&scope, (int32_t)scope.slot, CHAT_TELL, orders[order], error);
}
