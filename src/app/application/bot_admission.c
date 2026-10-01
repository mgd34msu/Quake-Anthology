#include "bot_admission.h"
#include "map_players_private.h"
#include "bots_private.h"
#include "bot_world.h"
#include "native_q3_clients.h"
#include "native_q3_wire_state.h"
#include "rankings.h"
#include "qa/game_q3_client.h"
#include "qa/game_q3_source.h"

#include <stdlib.h>
#include <string.h>

static bool admission(qa_application *app, uint32_t physical,
    application_player_record **out, qa_error *error)
{
    if (!app || !app->players || !app->session || !app->bots ||
        app->destroy_requested || !qa_session_safe(app->session) ||
        !application_rankings_idle(app) || app->q3_round_active || app->q3_world_restart ||
        app->frame_preparing ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_CONFIGURING) ||
        app->state == QA_APPLICATION_FAULTED || app->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT, "bot admission requires its actual safe local source client");
    application_provider *source = app->players->map_provider;
    if (!source || source != application_bot_source(app->bots) || !source->constructed ||
        !source->attached || source->close_pending ||
        (!app->bots->shared_world && source->kind != APPLICATION_PROVIDER_Q3))
        return application_fail(error, QA_ERROR_ARGUMENT, "bot admission lost its actual native or shared GAME owner");
    for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record *record = &app->players->records[i];
        if (record->retiring || record->remote || !record->bot || record->client_slot != physical) continue;
        const qa_actor_record *actor = qa_actors_get(qa_session_actors(app->session), record->actor);
        if (!actor || !record->character || actor->owner != record->character->owner ||
            !actor->has_source || actor->source_slot != record->source_slot)
            return application_fail(error, QA_ERROR_ARGUMENT, "bot admission lost its actual actor generation or source binding");
        if (source->kind == APPLICATION_PROVIDER_Q3) {
            uint32_t actual;
            if (!qa_q3_native_client_slot(source->state.q3, record->actor, &actual, error)) return false;
            if (actual != physical)
                return application_fail(error, QA_ERROR_ARGUMENT, "bot canonical client differs from its physical GAME slot");
        }
        *out = record;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "bot source client has no actual local roster record");
}

static char *retain(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "cannot retain bot source userinfo"); return NULL; }
    memcpy(copy, text, length + 1);
    return copy;
}

bool application_players_bot_userinfo(qa_application *app, uint32_t physical,
    const char *text, qa_error *error)
{
    application_player_record *record;
    if (!text || strlen(text) >= 1024)
        return application_fail(error, QA_ERROR_ARGUMENT, "bot raw source userinfo exceeds its fixed source extent");
    if (!admission(app, physical, &record, error)) return false;
    char name[1024], team[1024], skin[1024];
    qa_q3_client_info_value(text, "name", name, sizeof(name));
    qa_q3_client_info_value(text, "team", team, sizeof(team));
    qa_q3_client_info_value(text, "model", skin, sizeof(skin));
    char *raw = retain(text, error), *new_name = raw ? retain(name, error) : NULL;
    char *new_team = new_name ? retain(team, error) : NULL;
    char *new_skin = new_team ? retain(skin, error) : NULL;
    if (!new_skin) { free(raw); free(new_name); free(new_team); return false; }
    application_provider *source = app->players->map_provider;
    bool okay = app->bots->shared_world
        ? application_bot_world_userinfo_set(app->bots->shared_world, physical, text, error)
        : application_native_q3_wire_userinfo(source, physical, text, error);
    if (!okay) { free(raw); free(new_name); free(new_team); free(new_skin); return false; }
    free(record->userinfo); free(record->name); free(record->team); free(record->skin);
    record->userinfo = raw; record->name = new_name; record->team = new_team; record->skin = new_skin;
    return true;
}

bool application_players_bot_connect(qa_application *app, uint32_t physical,
    bool first_time, bool bot, bool *accepted, qa_error *error)
{
    application_player_record *record;
    if (!accepted || !bot)
        return application_fail(error, QA_ERROR_ARGUMENT, "bot catalogue Connect requires its actual bot admission");
    *accepted = false;
    if (!admission(app, physical, &record, error)) return false;
    if (!record->source_begin_pending || !record->userinfo)
        return application_fail(error, QA_ERROR_ARGUMENT, "bot Connect lost its prepared source admission or raw userinfo");
    qa_mode_player_view member;
    if (app->primary_mode_ready &&
        (!qa_modes_player_read(app->modes, app->primary_mode, record->actor, &member, error) ||
         member.connection.connected))
        return application_fail(error, QA_ERROR_ARGUMENT, "bot Connect requires its unconnected prepared source client");
    qa_actor_id actor = record->actor;
    application_provider *source = app->players->map_provider;
    application_operation previous = app->operation;
    app->operation = APPLICATION_CONFIGURING;
    const char *reason = NULL;
    bool okay;
    if (app->bots->shared_world) {
        okay = application_bot_world_connect_client(app->bots->shared_world, physical,
            first_time, true, &reason, error);
        if (okay && reason)
            okay = application_bot_world_drop(app->bots->shared_world, physical, reason, error);
        if (okay && !reason) {
            qa_string_id name;
            okay = qa_strings_intern_cstr(qa_session_strings(app->session), record->name, &name, error) &&
                qa_modes_player(app->modes, &(qa_match_player){.actor = actor, .name = name,
                    .connected = true, .connecting = true, .bot = true}, error);
            for (size_t i = 0; okay && i < app->mode_count; ++i) {
                qa_team_id team = 0;
                if (record->team[0]) okay = qa_strings_intern_cstr(
                    qa_session_strings(app->session), record->team, &team, error);
                if (okay) okay = qa_modes_join(app->modes, app->mode_ids[i], actor,
                    team, record->spectator, error);
            }
            if (okay) *accepted = true;
        }
    } else {
        okay = application_native_q3_client_connect(source, actor, UINT32_MAX,
            record->userinfo, first_time, true, accepted, &reason, error);
        if (okay && !*accepted) {
            application_native_q3_wire_client_view wire;
            bool present;
            okay = application_native_q3_wire_client_admission_read(source, physical, &wire, &present, error);
            if (okay && present)
                okay = qa_actor_id_equal(wire.actor, actor) &&
                    application_native_q3_wire_drop(source, physical, reason ? reason : "BotConnectfailed", error) &&
                    application_native_q3_clients_drain(app, error);
            else if (okay) okay = application_players_native_q3_retire(app, source, actor, error);
        }
    }
    if (okay && *accepted && record->character != source &&
        record->character->kind == APPLICATION_PROVIDER_Q2)
        okay = qa_q2_player_userinfo(record->character->state.q2, actor, record->userinfo, error);
    app->operation = previous;
    if (!okay) { application_fault(app, error); return false; }
    return true;
}
