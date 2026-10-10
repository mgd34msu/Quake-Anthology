#include "native_q3_session.h"
#include "qa/game_type.h"
#include "qa/text.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_clients.h"

#include <stdio.h>
#include <string.h>

enum { SESSION_BUFFER_SIZE = 1024 };

typedef struct session_source {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_cvars *cvars;
    qa_actor_owner owner;
    uint64_t publication_generation, command_generation, map_revision;
} session_source;

static bool source_live(const session_source *source, qa_error *error)
{
    const application_provider *provider = source->provider;
    const qa_application *app = source->application;
    if (!app || provider->application != app || !provider->constructed ||
        !provider->attached || provider->close_pending || app->destroy_requested ||
        provider->state.q3 != source->game || provider->owner != source->owner ||
        application_native_q3_console_registry(provider) != source->cvars ||
        app->publication_generation != source->publication_generation ||
        app->command_generation != source->command_generation ||
        app->map_revision != source->map_revision)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 session source retired during its operation");
    return true;
}

static bool source_begin(application_provider *provider, session_source *out,
    qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->application || !provider->state.q3 || !provider->owner || !out)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 session requires its actual source owner");
    qa_application *app = provider->application;
    *out = (session_source){.provider = provider, .application = app,
        .game = provider->state.q3, .owner = provider->owner,
        .cvars = application_native_q3_console_registry(provider),
        .publication_generation = app->publication_generation,
        .command_generation = app->command_generation, .map_revision = app->map_revision};
    if (!out->cvars)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 session has no retained source cvar registry");
    return source_live(out, error) &&
        application_native_q3_console_borrow(provider, error);
}

static bool client_live(const session_source *source, qa_actor_id actor,
    uint32_t slot, qa_error *error)
{
    uint32_t actual;
    qa_application *app = source->application;
    if (!source_live(source, error)) return false;
    if (!qa_actors_get(qa_session_actors(app->session), actor) ||
        !qa_q3_native_client_slot(source->game, actor, &actual, error) || actual != slot)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 session client lost its physical source binding");
    return true;
}

static void cvar_read(const session_source *source, const char *name,
    char buffer[SESSION_BUFFER_SIZE])
{
    const qa_cvar_view *value = qa_cvars_find(source->cvars, name);
    const char *text = value ? value->value : "";
    size_t count = 0;
    while (count < SESSION_BUFFER_SIZE - 1 && text[count]) ++count;
    memcpy(buffer, text, count);
    buffer[count] = 0;
}

static bool cvar_write(const session_source *source, const char *name,
    const char *value, qa_error *error)
{
    return source_live(source, error) &&
        qa_cvars_set(source->cvars, name, value, true, error) && source_live(source, error);
}

static int signed_byte(unsigned char byte)
{
    return byte < 128 ? byte : (int)byte - 256;
}

static int32_t integer(const char *text)
{
    while (*text && signed_byte((unsigned char)*text) <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-') ++text;
    uint32_t bits = 0;
    while (*text >= '0' && *text <= '9')
        bits = bits * 10u + (uint32_t)(*text++ - '0');
    if (negative) bits = 0u - bits;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

/* bg_lib _atoi consumes a delimiter, including the terminating NUL. Empty
 * input and trailing whitespace leave the cursor at NUL instead. */
static bool scan_integer(const char *buffer, size_t length, size_t *cursor,
    int32_t *value, qa_error *error)
{
    if (*cursor > length)
        return application_fail(error, QA_ERROR_FORMAT,
            "truncated native Q3 session scan exceeds its terminating NUL");
    *value = integer(buffer + *cursor);
    while (*cursor < length && signed_byte((unsigned char)buffer[*cursor]) <= 32)
        ++*cursor;
    if (*cursor == length) return true;
    if (buffer[*cursor] == '+' || buffer[*cursor] == '-') ++*cursor;
    for (;;) {
        if (*cursor == length) {
            ++*cursor;
            return true;
        }
        unsigned char byte = (unsigned char)buffer[(*cursor)++];
        if (byte < '0' || byte > '9') return true;
    }
}

static bool session_write(const session_source *source, uint32_t slot, qa_error *error)
{
    qa_q3_client_session row;
    qa_q3_source_binding before, after;
    uint32_t maximum;
    if (!source_live(source, error)) return false;
    if (!qa_q3_source_max_clients(source->game, &maximum, error)) return false;
    if (slot >= maximum)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 session write exceeds configured source clients");
    if (!qa_q3_client_session_slot_read(source->game, slot, &row, error) ||
        !qa_q3_source_binding_read(source->game, slot, &before, error)) return false;
    char name[32], value[SESSION_BUFFER_SIZE], fields[7][12];
    qa_format_q3_integer(row.team, fields[0]);
    qa_format_q3_integer(row.spectator_time_ms, fields[1]);
    qa_format_q3_integer(row.spectator_state, fields[2]);
    qa_format_q3_integer(row.spectator_client, fields[3]);
    qa_format_q3_integer(row.wins, fields[4]);
    qa_format_q3_integer(row.losses, fields[5]);
    qa_format_q3_integer(row.team_leader, fields[6]);
    snprintf(name, sizeof(name), "session%u", slot);
    snprintf(value, sizeof(value), "%s %s %s %s %s %s %s",
        fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]);
    if (!cvar_write(source, name, value, error) ||
        !qa_q3_source_binding_read(source->game, slot, &after, error)) return false;
    return qa_actor_id_equal(before.actor, after.actor) ||
        application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 session cvar callback replaced its physical source client");
}

static bool session_commit(const session_source *source, qa_actor_id actor,
    uint32_t slot, uint32_t mask, const qa_q3_client_session *row, qa_error *error)
{
    return client_live(source, actor, slot, error) &&
        qa_q3_client_session_slot_write(source->game, slot, mask, row, error) &&
        client_live(source, actor, slot, error);
}

static bool session_read(const session_source *source, qa_actor_id actor,
    uint32_t slot, qa_error *error)
{
    char name[32], buffer[SESSION_BUFFER_SIZE];
    snprintf(name, sizeof(name), "session%u", slot);
    cvar_read(source, name, buffer);
    size_t length = strlen(buffer), cursor = 0;
    qa_q3_client_session row = {0};
    if (!scan_integer(buffer, length, &cursor, &row.team, error) ||
        !scan_integer(buffer, length, &cursor, &row.spectator_time_ms, error) ||
        !session_commit(source, actor, slot, QA_Q3_CLIENT_SESSION_TIME, &row, error) ||
        !scan_integer(buffer, length, &cursor, &row.spectator_state, error) ||
        !scan_integer(buffer, length, &cursor, &row.spectator_client, error) ||
        !session_commit(source, actor, slot, QA_Q3_CLIENT_SESSION_CLIENT, &row, error) ||
        !scan_integer(buffer, length, &cursor, &row.wins, error) ||
        !session_commit(source, actor, slot, QA_Q3_CLIENT_SESSION_WINS, &row, error) ||
        !scan_integer(buffer, length, &cursor, &row.losses, error) ||
        !session_commit(source, actor, slot, QA_Q3_CLIENT_SESSION_LOSSES, &row, error) ||
        !scan_integer(buffer, length, &cursor, &row.team_leader, error)) return false;
    return session_commit(source, actor, slot,
        QA_Q3_CLIENT_SESSION_TEAM | QA_Q3_CLIENT_SESSION_STATE | QA_Q3_CLIENT_SESSION_LEADER, &row, error);
}

static bool initialize_client(const session_source *source, qa_actor_id actor,
    uint32_t slot, const char *userinfo, qa_error *error)
{
    int32_t game_type, auto_join = 0, maximum, time;
    qa_q3_client_session row = {0};
    if (!application_native_q3_settings_integer_at(source->provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error))
        return false;
    if (qa_game_type_is_team(game_type)) {
        if (!application_native_q3_settings_integer_at(source->provider, APPLICATION_Q3_SETTING_G_TEAM_AUTO_JOIN, &auto_join, error))
            return false;
        if (auto_join) {
            if (!application_native_q3_client_pick_team(source->provider, -1, &row.team, error))
                return false;
        } else row.team = 3;
    } else {
        char team[SESSION_BUFFER_SIZE];
        qa_q3_client_info_value(userinfo, "team", team, sizeof(team));
        if (team[0] == 's') row.team = 3;
        else {
            qa_q3_source_client_counts counts;
            if (!qa_q3_source_client_counts_read(source->game, &counts, error))
                return false;
            if (game_type == 1) row.team = counts.num_non_spectator >= 2 ? 3 : 0;
            else {
                if (!application_native_q3_settings_integer_at(source->provider,
                        APPLICATION_Q3_SETTING_G_MAX_GAME_CLIENTS, &maximum, error)) return false;
                row.team = maximum > 0 && counts.num_non_spectator >= maximum ? 3 : 0;
            }
        }
    }
    if (!session_commit(source, actor, slot, QA_Q3_CLIENT_SESSION_TEAM, &row, error)) return false;
    if (qa_game_type_is_team(game_type) && auto_join) {
        qa_q3_native_client client;
        char text[128];
        if (!qa_q3_client_read(source->game, actor, &client, error)) return false;
        snprintf(text, sizeof(text), "cp \"%s^7 %s\n\"", client.netname,
            row.team == 1 ? "joined the red team." : "joined the blue team.");
        if (!application_native_q3_send_command(source->provider, -1, text, error) ||
            !client_live(source, actor, slot, error)) return false;
    }
    if (!qa_q3_source_clock(source->game, &time, error)) return false;
    row.spectator_state = QA_Q3_SPECTATOR_FREE;
    row.spectator_time_ms = time;
    return session_commit(source, actor, slot,
        QA_Q3_CLIENT_SESSION_STATE | QA_Q3_CLIENT_SESSION_TIME, &row, error) &&
        session_write(source, slot, error) && client_live(source, actor, slot, error);
}

bool application_native_q3_session_initialize(void *opaque, qa_error *error)
{
    application_provider *provider = opaque;
    session_source source;
    if (!source_begin(provider, &source, error)) return false;
    char previous[SESSION_BUFFER_SIZE];
    cvar_read(&source, "session", previous);
    int32_t game_type;
    bool ok = application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error);
    if (ok && game_type != integer(previous))
        ok = qa_q3_source_new_session_set(source.game, true, error) &&
            application_native_q3_console_print(provider,
                "Gametype changed, clearing session data.\n", error) && source_live(&source, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_session_client_connect(application_provider *provider,
    qa_actor_id actor, bool first_time, const char *userinfo, qa_error *error)
{
    session_source source;
    if (!userinfo)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 session admission requires actual userinfo");
    if (!source_begin(provider, &source, error)) return false;
    uint32_t slot;
    bool new_session;
    bool ok = qa_q3_native_client_slot(source.game, actor, &slot, error) &&
        client_live(&source, actor, slot, error) &&
        qa_q3_source_new_session_read(source.game, &new_session, error);
    if (ok && (first_time || new_session))
        ok = initialize_client(&source, actor, slot, userinfo, error);
    if (ok) ok = session_read(&source, actor, slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_session_read_client(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    session_source source;
    if (!source_begin(provider, &source, error)) return false;
    uint32_t slot;
    bool ok = qa_q3_native_client_slot(source.game, actor, &slot, error) &&
        client_live(&source, actor, slot, error) && session_read(&source, actor, slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_session_write_client(application_provider *provider,
    uint32_t slot, qa_error *error)
{
    session_source source;
    if (!source_begin(provider, &source, error)) return false;
    bool ok = session_write(&source, slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

static bool world_write(const session_source *source, qa_error *error)
{
    int32_t game_type;
    uint32_t maximum;
    bool ok = application_native_q3_settings_integer_at(source->provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error);
    char value[SESSION_BUFFER_SIZE];
    if (ok) {
        qa_format_q3_integer(game_type, value);
        ok = cvar_write(source, "session", value, error) &&
            qa_q3_source_max_clients(source->game, &maximum, error);
    }
    for (uint32_t slot = 0; ok && slot < maximum; ++slot) {
        qa_q3_native_client client;
        ok = qa_q3_client_slot_read(source->game, slot, &client, error);
        if (ok && client.rule.connected == QA_Q3_CLIENT_CONNECTED)
            ok = session_write(source, slot, error);
    }
    return ok;
}

bool application_native_q3_session_write_world(application_provider *provider, qa_error *error)
{
    session_source source;
    if (!source_begin(provider, &source, error)) return false;
    bool ok = world_write(&source, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_session_capture_carry(application_provider *provider, qa_error *error)
{
    session_source source;
    if (!source_begin(provider, &source, error)) return false;
    uint32_t maximum;
    bool ok = world_write(&source, error) &&
        qa_q3_source_max_clients(source.game, &maximum, error);
    for (uint32_t slot = 0; ok && slot < maximum; ++slot) {
        qa_q3_native_client client;
        ok = qa_q3_client_slot_read(source.game, slot, &client, error);
        if (ok && client.rule.connected != QA_Q3_CLIENT_DISCONNECTED)
            ok = session_write(&source, slot, error);
    }
    application_native_q3_console_release(provider);
    return ok;
}
