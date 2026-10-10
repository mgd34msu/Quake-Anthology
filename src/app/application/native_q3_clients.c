#include "native_q3_clients.h"
#include "qa/game_type.h"
#include "qa/text.h"
#include "native_q3_console.h"
#include "native_q3_chat.h"
#include "native_q3_wire_state.h"
#include "native_q3_log.h"
#include "native_q3_settings.h"
#include "native_q3_ipfilters.h"
#include "native_q3_session.h"
#include "native_q3_votes.h"
#include "native_q3_rank.h"
#include "native_q3_match.h"
#include "native_q3_objectives.h"
#include "native_q3_control.h"
#include "guest_q3_restart.h"
#include "map_players_private.h"
#include "control_frame.h"
#include "rankings.h"
#include "bots_round.h"
#include "bots_catalog.h"
#include "supplies.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_client.h"
#include "qa/game_q3_wire.h"
#include "qa/modes_q3_clients.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool named(const char *text, const char *name)
{
    while (*text && *name) {
        unsigned char byte = (unsigned char)*text++;
        if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
        if (byte != (unsigned char)*name++) return false;
    }
    return !*text && !*name;
}

static int32_t difference(int32_t left, int32_t right)
{
    uint32_t bits = (uint32_t)left - (uint32_t)right;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
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
    if (negative) bits = 0u - bits;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool source(application_provider *provider, qa_actor_id actor,
    uint32_t *slot, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || app->destroy_requested || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        ((!app->primary_mode_ready || application_world_provider(app, QA_ROLE_ENTITIES, "") != provider)&&
         !application_native_q3_source_entered(provider)) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 client source has retired");
    return qa_q3_native_client_slot(provider->state.q3, actor, slot, error);
}

static bool client_mode(application_provider *provider,qa_mode_id *out,qa_error *error)
{
    bool found;
    return application_native_q3_source_mode(provider,out,&found,error)&&
        (found||application_fail(error,QA_ERROR_NOT_FOUND,"Native Q3 client has no actual associated mode"));
}

static bool native_rules(application_provider *provider,qa_mode_id mode,qa_mode_view *out,
    bool *native,qa_error *error)
{
    qa_application *app = provider->application;
    application_provider *chosen = application_mode_provider(app,mode);
    *native=false;
    if (!chosen || chosen->kind != APPLICATION_PROVIDER_Q3) return true;
    if(!qa_modes_read(app->modes,mode,out,error)) return false;
    *native=out->rules.source>=QA_MODE_Q3; return true;
}

application_provider *application_native_q3_mode_source_provider(qa_application *app, qa_mode_id mode)
{
    application_provider *chosen = app ? application_mode_provider(app, mode) : NULL;
    if (chosen && application_native_q3_source_entered(chosen))
        return application_native_q3_source_mode_current(chosen, mode, NULL) ? chosen : NULL;
    if (!chosen || chosen->kind != APPLICATION_PROVIDER_Q3 || !app->primary_mode_ready ||
        mode.slot != app->primary_mode.slot || mode.generation != app->primary_mode.generation)
        return chosen;
    application_provider *game = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_mode_view view;
    if (game && game->kind == APPLICATION_PROVIDER_Q3 &&
        qa_modes_read(app->modes, mode, &view, NULL) && view.rules.source >= QA_MODE_Q3)
        return game;
    return chosen;
}

bool application_native_q3_client_print(void *opaque, qa_actor_id actor,
    const char *text, qa_error *error)
{
    application_provider *provider = opaque;
    uint32_t slot;
    if (!text || !source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    char command[8192];
    snprintf(command, sizeof(command), "print \"%s\"", text);
    bool ok = application_native_q3_send_command(provider, (int32_t)slot, command, error) &&
        source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_mode_client_slot(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_actor_owner *owner, uint32_t *slot, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    if (!owner || !slot || !provider || !provider->constructed || provider->close_pending) return false;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        if (!provider->state.q3 || !qa_q3_native_client_slot(provider->state.q3, actor, slot, error)) return false;
    } else if ((provider->kind != APPLICATION_PROVIDER_QVM && provider->kind != APPLICATION_PROVIDER_NATIVE) ||
        provider->component.clock.kind != QA_RULESET_Q3 ||
        !application_q3_guest_actor_bound(provider, actor, slot)) return false;
    *owner = provider->owner;
    return true;
}

bool application_native_q3_mode_source(void *opaque, qa_mode_id mode, qa_actor_owner *owner)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    qa_mode_view view;
    bool associated=provider&&application_native_q3_source_entered(provider)&&
        application_native_q3_source_mode_current(provider,mode,NULL);
    if (!owner || !provider || ((!app->primary_mode_ready ||
        mode.slot != app->primary_mode.slot || mode.generation != app->primary_mode.generation)&&!associated) ||
        !qa_modes_read(app->modes, mode, &view, NULL) || view.rules.source < QA_MODE_Q3 ||
        provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || provider->close_pending ||
        (application_world_provider(app, QA_ROLE_ENTITIES, "") != provider&&!associated)) return false;
    *owner = provider->owner;
    return true;
}

bool application_native_q3_source_score_bound(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_actor_owner *owner)
{
    qa_application *app = opaque;
    if (!app || !owner || app->destroy_requested || !app->modes ||
        !qa_actors_get(qa_session_actors(app->session), actor)) return false;
    application_provider *candidate=application_native_q3_mode_source_provider(app,mode);
    bool associated=candidate&&application_native_q3_source_entered(candidate);
    if(associated&&!application_native_q3_source_mode_current(candidate,mode,NULL)) return false;
    application_provider *provider=associated?candidate:application_world_provider(app,QA_ROLE_ENTITIES,"");
    if(!associated&&(!app->primary_mode_ready||mode.slot!=app->primary_mode.slot||
        mode.generation!=app->primary_mode.generation||
        application_world_provider(app,QA_ROLE_ENTITIES,"")!=provider)) return false;
    uint32_t slot;
    qa_q3_source_binding binding;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, NULL) ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, NULL) ||
        !qa_actor_id_equal(binding.actor, actor) || !binding.body_attached ||
        binding.client_slot != (int32_t)slot) return false;
    *owner = provider->owner;
    return true;
}

bool application_native_q3_mode_rank_client(void *opaque, qa_mode_id mode,
    qa_actor_id actor, bool *connected, bool *connecting, bool *bot,
    int32_t *source_team, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    uint32_t slot, flags;
    qa_q3_native_client client;
    qa_q3_client_session sess;
    if (!connected || !connecting || !bot || !source_team ||
        !provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || provider->close_pending || app->destroy_requested ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !qa_q3_client_slot_read(provider->state.q3, slot, &client, error) ||
        !qa_q3_client_server_flags(provider->state.q3, slot, &flags, error) ||
        !qa_q3_client_session_slot_read(provider->state.q3, slot, &sess, error)) return false;
    *connected = client.connected != QA_Q3_CLIENT_DISCONNECTED;
    *connecting = client.connected == QA_Q3_CLIENT_CONNECTING;
    *bot = (flags & 8u) != 0;
    *source_team = sess.team;
    return true;
}

bool application_native_q3_mode_rank_counts(void *opaque, qa_mode_id mode,
    qa_mode_q3_rank_counts *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    uint32_t maximum;
    if (!out || !provider || !app || app->destroy_requested ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || provider->close_pending ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error)) return false;
    qa_mode_q3_rank_counts counts = {0};
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        qa_q3_native_client client;
        qa_q3_client_session sess;
        uint32_t flags;
        if (!qa_q3_client_slot_read(provider->state.q3, slot, &client, error)) return false;
        if (client.connected == QA_Q3_CLIENT_DISCONNECTED) continue;
        ++counts.connected;
        if (!qa_q3_client_session_slot_read(provider->state.q3, slot, &sess, error)) return false;
        if (sess.team == 3) continue;
        ++counts.non_spectator;
        if (client.connected != QA_Q3_CLIENT_CONNECTED) continue;
        ++counts.playing;
        if (!qa_q3_client_server_flags(provider->state.q3, slot, &flags, error)) return false;
        if (flags & 8u) continue;
        ++counts.voting;
        if (sess.team == 1 || sess.team == 2) ++counts.team_voting[sess.team - 1];
    }
    *out = counts;
    return true;
}

static bool team_counts(application_provider *provider, qa_mode_id mode,
    int32_t ignore_slot, int32_t counts[2], qa_error *error)
{
    (void)mode;
    qa_application *app = provider ? provider->application : NULL;
    uint32_t maximum;
    if (!app || app->destroy_requested || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || provider->close_pending ||
        (application_world_provider(app, QA_ROLE_ENTITIES, "") != provider&&
         !application_native_q3_source_entered(provider)) ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error)) return false;
    counts[0] = counts[1] = 0;
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        if ((int32_t)slot == ignore_slot) continue;
        qa_q3_native_client client;
        qa_q3_client_session row;
        if (!qa_q3_client_slot_read(provider->state.q3, slot, &client, error)) return false;
        if (client.connected == QA_Q3_CLIENT_DISCONNECTED) continue;
        if (!qa_q3_client_session_slot_read(provider->state.q3, slot, &row, error)) return false;
        if (row.team == 1) ++counts[0];
        else if (row.team == 2) ++counts[1];
    }
    return true;
}

static bool pick_team(application_provider *provider, qa_mode_id mode,
    int32_t ignore_slot, int32_t *out, qa_error *error)
{
    int32_t counts[2];
    qa_q3_source_team_state team;
    if (!out || !team_counts(provider, mode, ignore_slot, counts, error) ||
        !qa_q3_source_team_state_read(provider->state.q3, &team, error)) return false;
    *out = counts[1] > counts[0] ? 1 : counts[0] > counts[1] ? 2 :
        team.team_scores[2] > team.team_scores[1] ? 1 : 2;
    return true;
}

bool application_native_q3_client_pick_team(application_provider *provider,
    int32_t ignore_slot, int32_t *out, qa_error *error)
{
    qa_mode_id mode;
    return client_mode(provider,&mode,error)&&pick_team(provider,mode,ignore_slot,out,error);
}

bool application_native_q3_mode_choose_team(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_team_id *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    uint32_t slot;
    int32_t selected;
    qa_mode_view view;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !pick_team(provider, mode, (int32_t)slot, &selected, error) ||
        !qa_modes_read(app->modes, mode, &view, error)) return false;
    *out = view.rules.teams[selected - 1];
    return true;
}

bool application_native_q3_mode_vote_calls(void *opaque, qa_mode_id mode,
    qa_actor_id actor, bool team_vote, int32_t *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    qa_q3_native_client client;
    uint32_t slot;
    if (!out || !source(provider, actor, &slot, error) ||
        !qa_q3_client_read(provider->state.q3, actor, &client, error)) return false;
    *out = team_vote ? client.team_vote_count : client.vote_count;
    return true;
}

bool application_native_q3_mode_intermission_client(void *opaque, qa_mode_id mode,
    qa_actor_id actor, bool *eligible, bool *ready, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    qa_q3_native_client client;
    qa_q3_player_state player;
    qa_q3_source_binding row;
    uint32_t slot;
    if (!eligible || !ready || !source(provider, actor, &slot, error) ||
        !qa_q3_client_read(provider->state.q3, actor, &client, error) ||
        !qa_q3_player_read(provider->state.q3, actor, &player) || player.client_number < 0 ||
        !qa_q3_source_binding_read(provider->state.q3, (uint32_t)player.client_number, &row, error)) return false;
    *eligible = client.connected == QA_Q3_CLIENT_CONNECTED && !(row.server_flags & 8u);
    *ready = client.ready_to_exit;
    return true;
}

bool application_native_q3_mode_ready_publish(void *opaque, qa_mode_id mode,
    int32_t mask, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->constructed ||
        !provider->attached || provider->close_pending || app->destroy_requested ||
        !application_native_q3_console_borrow(provider, error)) return false;
    uint32_t maximum;
    bool ok = qa_q3_source_max_clients(provider->state.q3, &maximum, error);
    for (uint32_t slot = 0; ok && slot < maximum; ++slot) {
        qa_q3_native_client client;
        ok = qa_q3_client_slot_read(provider->state.q3, slot, &client, error);
        if (ok && client.connected == QA_Q3_CLIENT_CONNECTED)
            ok = qa_q3_wire_client_ready(provider->state.q3, slot, mask, error);
    }
    application_native_q3_console_release(provider);
    return ok;
}

static bool session(application_provider *provider, qa_actor_id actor,
    qa_q3_client_session *out, qa_error *error)
{
    return qa_q3_client_session_read(provider->state.q3, actor, out, error);
}

static bool connection(application_provider *provider, qa_actor_id actor,
    qa_q3_client_connection state, qa_error *error)
{
    qa_application *app = provider->application;
    qa_mode_player_view member;
    qa_q3_native_client client;
    qa_mode_id mode;
    uint32_t slot, flags;
    if (!source(provider, actor, &slot, error) ||
        !client_mode(provider,&mode,error)||!qa_modes_player_read(app->modes,mode,actor,&member,error)||
        !qa_q3_client_read(provider->state.q3, actor, &client, error) ||
        !qa_q3_client_server_flags(provider->state.q3, slot, &flags, error)) return false;
    member.connection.connected = state != QA_Q3_CLIENT_DISCONNECTED;
    member.connection.connecting = state == QA_Q3_CLIENT_CONNECTING;
    member.connection.bot = (flags & 8u) != 0;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), client.netname,
            &member.connection.name, error)) return false;
    return qa_modes_player(app->modes, &member.connection, error);
}

static bool slot_source(application_provider *provider, uint32_t slot,
    qa_actor_id *actor, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    uint32_t maximum;
    qa_q3_source_binding binding;
    if (!app || app->destroy_requested || !actor ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        ((!app->primary_mode_ready || application_world_provider(app, QA_ROLE_ENTITIES, "") != provider)&&
         !application_native_q3_source_entered(provider)) ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error) || slot >= maximum ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 fixed client source has retired");
    *actor = binding.actor;
    return true;
}

static bool slot_current(application_provider *provider, uint32_t slot,
    qa_actor_id expected, qa_error *error)
{
    qa_actor_id current;
    return slot_source(provider, slot, &current, error) &&
        (qa_actor_id_equal(current, expected) ||
         application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 fixed client changed during source output"));
}

static bool userinfo_changed(application_provider *provider,
    uint32_t slot, qa_error *error)
{
    uint32_t flags;
    qa_actor_id actor;
    qa_q3_client_session sess;
    qa_q3_native_client before, client;
    int32_t game_type;
    const char *retained;
    if (!slot_source(provider, slot, &actor, error) ||
        !qa_q3_client_session_slot_read(provider->state.q3, slot, &sess, error) ||
        !qa_q3_client_slot_read(provider->state.q3, slot, &before, error) ||
        !application_native_q3_wire_source_userinfo_read(provider, slot, &retained, error)) return false;
    application_snapshot_mutated(provider->application);
    char info[1024];
    size_t count = strlen(retained);
    if (count >= sizeof(info)) count = sizeof(info) - 1;
    memcpy(info, retained, count); info[count] = 0;
    if (strchr(info, '"') || strchr(info, ';'))
        memcpy(info, "\\name\\badinfo", sizeof("\\name\\badinfo"));
    if (!qa_q3_client_slot_userinfo(provider->state.q3, slot, info,
            sess.team == 3 && sess.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD, error) ||
        !qa_q3_client_slot_read(provider->state.q3, slot, &client, error)) return false;
    if (actor.registry && !connection(provider, actor, client.connected, error)) return false;
    if (client.connected == QA_Q3_CLIENT_CONNECTED && strcmp(before.netname, client.netname)) {
        char text[128];
        snprintf(text, sizeof(text), "print \"%s^7 renamed to %s\\n\"", before.netname, client.netname);
        if (!application_native_q3_send_command(provider, -1, text, error) ||
            !slot_current(provider, slot, actor, error)) return false;
    }
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error) ||
        !qa_q3_client_server_flags(provider->state.q3, slot, &flags, error)) return false;
    char model[64], head[64], color1[1024], color2[1024], red[1024], blue[1024];
    char task[1024], skill[1024], config[8192], log[8256];
    qa_q3_client_info_value(info, qa_game_type_is_team(game_type) ? "team_model" : "model", model, sizeof(model));
    qa_q3_client_info_value(info, qa_game_type_is_team(game_type) ? "team_headmodel" : "headmodel", head, sizeof(head));
    qa_q3_client_info_value(info, "color1", color1, sizeof(color1));
    qa_q3_client_info_value(info, "color2", color2, sizeof(color2));
    qa_q3_client_info_value(info, "g_redteam", red, sizeof(red));
    qa_q3_client_info_value(info, "g_blueteam", blue, sizeof(blue));
    qa_q3_client_info_value(info, "teamtask", task, sizeof(task));
    qa_q3_client_info_value(info, "skill", skill, sizeof(skill));
    int32_t team_task = integer(task);
    int32_t team = sess.team;
    if (qa_game_type_is_team(game_type) && (flags & 8u)) {
        char requested[1024];
        qa_q3_client_info_value(info, "team", requested, sizeof(requested));
        if (named(requested, "red") || named(requested, "r")) team = 1;
        else if (named(requested, "blue") || named(requested, "b")) team = 2;
        else {
            if (!application_native_q3_client_pick_team(provider, (int32_t)slot, &team, error)) return false;
        }
    }
    char fields[6][12];
    qa_format_q3_integer(team, fields[0]);
    qa_format_q3_integer(client.max_health, fields[1]);
    qa_format_q3_integer(sess.wins, fields[2]);
    qa_format_q3_integer(sess.losses, fields[3]);
    qa_format_q3_integer(team_task, fields[4]);
    qa_format_q3_integer(sess.team_leader, fields[5]);
    if (flags & 8u)
        snprintf(config, sizeof(config), "n\\%s\\t\\%s\\model\\%s\\hmodel\\%s\\c1\\%s\\c2\\%s\\hc\\%s\\w\\%s\\l\\%s\\skill\\%s\\tt\\%s\\tl\\%s",
            client.netname, fields[0], model, head, color1, color2, fields[1],
            fields[2], fields[3], skill, fields[4], fields[5]);
    else
        snprintf(config, sizeof(config), "n\\%s\\t\\%s\\model\\%s\\hmodel\\%s\\g_redteam\\%s\\g_blueteam\\%s\\c1\\%s\\c2\\%s\\hc\\%s\\w\\%s\\l\\%s\\tt\\%s\\tl\\%s",
            client.netname, fields[0], model, head, red, blue, color1, color2, fields[1],
            fields[2], fields[3], fields[4], fields[5]);
    if (!qa_q3_configstring_write(provider->state.q3, 544u + slot, config, error) ||
        !slot_current(provider, slot, actor, error)) return false;
    snprintf(log, sizeof(log), "ClientUserinfoChanged: %u %s\n", slot, config);
    return application_native_q3_log(provider, log, error) && slot_current(provider, slot, actor, error);
}

bool application_native_q3_client_userinfo_changed(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    uint32_t slot;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    bool ok = userinfo_changed(provider, slot, error) && source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_userinfo_changed_slot(application_provider *provider,
    uint32_t slot, qa_error *error)
{
    qa_actor_id actor;
    if (!slot_source(provider, slot, &actor, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    bool ok = userinfo_changed(provider, slot, error) && slot_current(provider, slot, actor, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_connect(application_provider *provider, qa_actor_id actor,
    uint32_t seat, const char *userinfo, bool first_time, bool bot,
    bool *accepted, const char **denial, qa_error *error)
{
    uint32_t slot, flags;
    if (!userinfo || !accepted || !denial || !source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    *accepted = false; *denial = NULL;
    char info[1024], address[1024], supplied[1024];
    size_t length = strlen(userinfo);
    if (length >= sizeof(info)) length = sizeof(info) - 1;
    memcpy(info, userinfo, length); info[length] = 0;
    qa_q3_client_info_value(info, "ip", address, sizeof(address));
    qa_q3_client_info_value(info, "password", supplied, sizeof(supplied));
    bool reject;
    const char *password;
    bool ok = application_native_q3_ipfilters_filter(provider, address, &reject, error);
    if (ok && reject) {
        *denial = "You are banned from this server.";
        application_native_q3_console_release(provider);
        return true;
    }
    if (ok) ok = qa_q3_client_server_flags(provider->state.q3, slot, &flags, error) &&
        application_native_q3_settings_string_at(provider, APPLICATION_Q3_SETTING_G_PASSWORD, &password, error);
    if (ok && !(flags & 8u) && strcmp(address, "localhost") && *password &&
        !named(password, "none") && strcmp(password, supplied)) *denial = "Invalid password";
    if (!ok || *denial) {
        application_native_q3_console_release(provider);
        return ok;
    }
    ok = application_native_q3_wire_connect(provider, slot, actor, seat, userinfo, bot, error) &&
        qa_q3_client_connect(provider->state.q3, actor, bot, error) &&
        qa_q3_client_server_flags(provider->state.q3, slot, &flags, error) &&
        qa_q3_client_set_server_flags(provider->state.q3, actor, bot ? flags | 8u : flags, error) &&
        connection(provider, actor, QA_Q3_CLIENT_CONNECTING, error);
    if (ok) ok = application_native_q3_session_client_connect(provider, actor, first_time, userinfo, error);
    if (ok && bot) {
        bool bot_accepted = false;
        ok = application_bots_native_q3_connect(provider, actor, !first_time, &bot_accepted, error);
        if (ok && !bot_accepted) {
            *denial = "BotConnectfailed";
            application_native_q3_console_release(provider);
            return true;
        }
        if (ok) ok = source(provider, actor, &slot, error);
    }
    char text[128];
    if (ok) {
        snprintf(text, sizeof(text), "ClientConnect: %u\n", slot);
        ok = application_native_q3_log(provider, text, error) &&
             source(provider, actor, &slot, error) &&
             application_native_q3_client_userinfo_changed(provider, actor, error);
    }
    qa_q3_native_client client;
    if (ok && first_time) {
        ok = qa_q3_client_read(provider->state.q3, actor, &client, error);
        if (ok) {
            snprintf(text, sizeof(text), "print \"%s^7 connected\\n\"", client.netname);
            ok = application_native_q3_send_command(provider, -1, text, error) &&
                 source(provider, actor, &slot, error);
        }
    }
    if (ok) ok = application_native_q3_rank(provider, error);
    if (ok) *accepted = true;
    application_native_q3_console_release(provider);
    return ok;
}

static bool client_spawn(application_provider *provider, qa_actor_id actor,
    const qa_body_state *spawn, const qa_q3_usercmd *command, qa_error *error)
{
    uint32_t slot;
    qa_mode_player_view member;
    qa_q3_client_session sess;
    qa_q3_usercmd accepted;
    qa_body_state body;
    qa_mode_id mode;
    qa_application *app = provider ? provider->application : NULL;
    if (!source(provider, actor, &slot, error) ||
        !client_mode(provider,&mode,error)||!qa_modes_player_read(app->modes,mode,actor,&member,error)||
        !session(provider, actor, &sess, error)) return false;
    bool spectator = sess.team == 3;
    if (spawn) body = *spawn;
    else {
        if (!qa_world_body_read(app->world, actor, &body, error)) return false;
        if (spectator) {
            if (!application_q3_find_intermission_pose(provider, &body.origin, &body.angles, error)) return false;
            body.velocity = qa_v3(0, 0, 0);
        } else {
            bool found;
            if (!application_q3_player_spawn_pose(app, actor, false, &body, &found, error)) return false;
            if (!found) return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 client has no actual spawn point");
        }
    }
    if (command) accepted = *command;
    else {
        application_native_q3_wire_client_view wire;
        bool present;
        if (!application_native_q3_wire_client_admission_read(provider, slot, &wire, &present, error) ||
            !present || !qa_actor_id_equal(wire.actor, actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 spawn has no retained engine command");
        accepted = wire.command;
    }
    if (!qa_q3_player_begin_command(provider->state.q3, actor, &accepted, error) ||
        !qa_q3_client_spectator(provider->state.q3, actor, spectator, error) ||
        !qa_q3_spawn_player(provider->state.q3, actor, &body, member.state.team, error) ||
        !application_supplies_spawn(app->supplies, provider, actor, error) ||
        !qa_q3_client_ready(provider->state.q3, actor, false, error) ||
        !source(provider, actor, &slot, error)) return false;
    if (!application_control_spawn_reset(app, actor, spectator, error)) return false;
    int32_t inactivity, now;
    if (!qa_q3_source_clock(provider->state.q3, &now, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_INACTIVITY, &inactivity, error)) return false;
    uint32_t deadline_bits = (uint32_t)now + (uint32_t)inactivity * 1000u;
    int32_t deadline;
    memcpy(&deadline, &deadline_bits, sizeof(deadline));
    qa_q3_usercmd received = accepted;
    if (!qa_q3_client_inactivity_write(provider->state.q3, actor,
            deadline, false, error) ||
        !qa_q3_source_clock(provider->state.q3, &accepted.serverTime, error) ||
        !qa_q3_client_command(provider->state.q3, actor, &accepted, error)) return false;
    return application_control_q3_client_think(provider, actor, &received, error) != APPLICATION_CONTROL_FAILED &&
           source(provider, actor, &slot, error);
}

bool application_native_q3_client_spawn(application_provider *provider, qa_actor_id actor,
    const qa_body_state *spawn, const qa_q3_usercmd *command, qa_error *error)
{
    uint32_t slot;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    bool ok = client_spawn(provider, actor, spawn, command, error) && source(provider, actor, &slot, error);
    if (ok) {
        application_native_q3_wire_client_view wire;
        bool present;
        ok = application_native_q3_wire_client_admission_read(provider, slot, &wire, &present, error);
        if (ok && present && wire.begun)
            ok = application_players_source_spawned(provider, actor, error) &&
                source(provider, actor, &slot, error);
    }
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_begin(application_provider *provider, qa_actor_id actor,
    const qa_body_state *spawn, const qa_q3_usercmd *command, qa_error *error)
{
    uint32_t slot;
    qa_mode_id mode;
    qa_mode_view rules;
    bool q3_rules;
    if (!source(provider, actor, &slot, error) ||
        !client_mode(provider,&mode,error)||!native_rules(provider,mode,&rules,&q3_rules,error)||
        !application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    qa_application *app = provider->application;
    bool ok = qa_world_unlink(app->world, actor, error) &&
        qa_q3_client_begin_state(provider->state.q3, actor, error) &&
        (!q3_rules || qa_modes_q3_client_begin_state(app->modes,mode,actor,error)) &&
        connection(provider, actor, QA_Q3_CLIENT_CONNECTED, error) &&
        application_native_q3_client_spawn(provider, actor, spawn, command, error);
    qa_q3_client_session sess;
    qa_q3_native_client client;
    int32_t game_type;
    if (ok) ok = session(provider, actor, &sess, error) &&
        qa_q3_client_read(provider->state.q3, actor, &client, error) &&
        application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error);
    char text[128];
    if (ok && sess.team != 3) {
        ok = qa_q3_client_teleport_event(provider->state.q3, actor, true, error) &&
             source(provider, actor, &slot, error);
        if (ok && game_type != 1) {
            snprintf(text, sizeof(text), "print \"%s^7 entered the game\\n\"", client.netname);
            ok = application_native_q3_send_command(provider, -1, text, error) &&
                 source(provider, actor, &slot, error);
        }
    }
    if (ok) {
        snprintf(text, sizeof(text), "ClientBegin: %u\n", slot);
        ok = application_native_q3_log(provider, text, error) &&
             source(provider, actor, &slot, error) &&
             application_native_q3_wire_begin(provider, slot, error) &&
             application_native_q3_rank(provider, error) &&
             application_players_source_spawned(provider, actor, error) &&
             source(provider, actor, &slot, error);
    }
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_respawn(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *provider = opaque;
    if (provider && provider->application &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider&&
        !application_native_q3_source_entered(provider))
        return application_players_selected_character_respawn(provider, actor, error);
    uint32_t slot;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    bool ok = qa_q3_client_copy_body_queue(provider->state.q3, actor, error) &&
        source(provider, actor, &slot, error) &&
        application_native_q3_client_spawn(provider, actor, NULL, NULL, error) &&
        qa_q3_client_teleport_event(provider->state.q3, actor, true, error) &&
        source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

static bool toss_cube(application_provider *provider, qa_actor_id actor,
    int32_t team, qa_error *error)
{
    uint32_t slot;
    int32_t timeout;
    qa_actor_id cube = {0};
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_CUBE_TIMEOUT, &timeout, error) ||
        !qa_q3_client_toss_cube(provider->state.q3, actor, team, timeout, &cube, error) ||
        !source(provider, actor, &slot, error)) return false;
    if (!cube.registry) return true;
    qa_q3_item_state item;
    return qa_q3_item_read(provider->state.q3, cube, &item) &&
        application_native_q3_objective_admitted(provider, cube, item.spawn.item_index, true, error) &&
        source(provider, actor, &slot, error);
}

bool application_native_q3_client_death_items(void *opaque, qa_actor_id actor,
    qa_error *error)
{
    application_provider *provider = opaque;
    if (provider && provider->application &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider&&
        !application_native_q3_source_entered(provider))
        return true;
    uint32_t slot;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    qa_q3_product product;
    int32_t start, game_type;
    bool okay = qa_q3_source_match_context_read(provider->state.q3, &product, &start, error) &&
        application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error);
    if (okay && product == QA_Q3_TEAM_ARENA && game_type == 7) {
        qa_q3_client_session client_session;
        okay = session(provider, actor, &client_session, error) &&
            toss_cube(provider, actor, client_session.team, error);
    }
    if (okay) okay = source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return okay;
}

static bool client_disconnect(application_provider *provider,
    qa_actor_id actor, bool drop_transport, qa_error *error)
{
    uint32_t slot, maximum;
    qa_q3_native_client client;
    qa_q3_client_session sess;
    qa_application *app = provider ? provider->application : NULL;
    bool primary = app && app->players && app->players->map_provider == provider &&
        application_world_provider(app, QA_ROLE_ENTITIES, "") == provider;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    bool ok = (!primary || application_bots_catalog_remove_begin(app, slot, error)) &&
        source(provider, actor, &slot, error) &&
        (!primary || application_rankings_disconnect(app, actor, error)) &&
        source(provider, actor, &slot, error) &&
        (!primary || application_bots_client_shutdown(app, actor, false, error)) &&
        source(provider, actor, &slot, error) &&
        (!drop_transport || application_native_q3_wire_drop_transport(provider, slot, error)) &&
        source(provider, actor, &slot, error) &&
        qa_q3_client_read(provider->state.q3, actor, &client, error) &&
        session(provider, actor, &sess, error) &&
        qa_q3_source_max_clients(provider->state.q3, &maximum, error);
    for (uint32_t follower = 0; ok && follower < maximum; ++follower) {
        qa_q3_client_session viewer;
        ok = qa_q3_client_session_slot_read(provider->state.q3, follower, &viewer, error);
        if (ok && viewer.team == 3 && viewer.spectator_state == QA_Q3_SPECTATOR_FOLLOW &&
            viewer.spectator_client == (int32_t)slot)
            ok = application_native_q3_client_stop_following_slot(provider, follower, error) &&
                 source(provider, actor, &slot, error);
    }
    if (ok && client.connected == QA_Q3_CLIENT_CONNECTED && sess.team != 3) {
        ok = qa_q3_client_teleport_event(provider->state.q3, actor, false, error) &&
            source(provider, actor, &slot, error) &&
            qa_q3_client_disconnect_items(provider->state.q3, actor, error) &&
            source(provider, actor, &slot, error);
        int32_t game_type;
        if (ok) ok = application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error);
        if (ok && game_type == 7 && provider->product &&
            !strcmp(provider->product->campaign, "missionpack"))
            ok = toss_cube(provider, actor, sess.team, error);
    }
    if (ok) {
        char text[64];
        snprintf(text, sizeof(text), "ClientDisconnect: %u\n", slot);
        ok = application_native_q3_log(provider, text, error) && source(provider, actor, &slot, error);
    }
    int32_t game_type, warmup, intermission;
    qa_q3_source_client_counts ranks;
    if (ok) ok = application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error) &&
        application_native_q3_match_warmup(provider, &warmup, error) &&
        application_native_q3_match_intermission(provider, &intermission, error) &&
        qa_q3_source_client_counts_read(provider->state.q3, &ranks, error);
    if (ok && game_type == 1 && !warmup && !intermission && ranks.sorted_clients[1] == slot) {
        uint32_t winner_slot = ranks.sorted_clients[0];
        qa_q3_client_session winner;
        ok = qa_q3_client_session_slot_read(provider->state.q3, winner_slot, &winner, error);
        if (ok) {
            winner.wins = difference(winner.wins, -1);
            ok = qa_q3_client_session_slot_write(provider->state.q3, winner_slot,
                     QA_Q3_CLIENT_SESSION_WINS, &winner, error) &&
                application_native_q3_client_userinfo_changed_slot(provider, winner_slot, error) &&
                source(provider, actor, &slot, error);
        }
    }
    sess.team = 0;
    if (ok) ok = qa_q3_client_disconnect(provider->state.q3, actor, error) &&
        qa_q3_client_session_slot_write(provider->state.q3, slot, QA_Q3_CLIENT_SESSION_TEAM, &sess, error) &&
        connection(provider, actor, QA_Q3_CLIENT_DISCONNECTED, error) &&
        qa_q3_configstring_write(provider->state.q3, 544u + slot, "", error) &&
        source(provider, actor, &slot, error) &&
        application_native_q3_rank(provider, error) &&
        application_native_q3_wire_disconnect(provider, slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_disconnect(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    return client_disconnect(provider, actor, false, error);
}

bool application_native_q3_clients_drain_provider(application_provider *provider, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || app->destroy_requested || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 DROP drain has no admitted provider");
    uint32_t maximum;
    bool ok = qa_q3_source_max_clients(provider->state.q3, &maximum, error);
    for (uint32_t slot = 0; ok && slot < maximum; ++slot) {
        qa_actor_id actor;
        const char *reason;
        bool pending;
        ok = application_native_q3_wire_drop_client_read(provider, slot, &actor, &reason, &pending, error);
        if (!ok || !pending) continue;
        if (!application_rankings_idle(app) ||
            (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING &&
             app->operation != APPLICATION_CONFIGURING) || !qa_session_safe(app->session) ||
            !qa_modes_idle(app->modes) || !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
            !application_native_q3_console_idle(provider) || !application_native_q3_wire_idle(provider))
            return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 DROP drain requires released source callbacks");
        application_native_q3_source_drop_scope scope;
        if (!application_native_q3_source_drop_begin(provider, slot, actor, &scope, error)) return false;
        uint32_t actual_slot;
        qa_mode_id mode;
        bool primary = app->players && app->players->map_provider == provider &&
            application_world_provider(app, QA_ROLE_ENTITIES, "") == provider;
        ok = client_mode(provider, &mode, error) &&
             source(provider, actor, &actual_slot, error) && actual_slot == slot &&
             client_disconnect(provider, actor, true, error) &&
             application_native_q3_source_drop_disconnected(&scope, error) &&
             (!primary || application_players_native_q3_retire(app, provider, actor, error));
        if (!ok && (!error || !error->code))
            application_fail(error, QA_ERROR_NOT_FOUND, "native Q3 DROP changed its actual client generation");
        qa_error close_error = {0};
        if (!application_native_q3_source_drop_end(&scope, &close_error) && ok) {
            if (error) *error = close_error;
            ok = false;
        }
        (void)reason;
    }
    return ok;
}

bool application_native_q3_clients_drain(qa_application *app, qa_error *error)
{
    if (!app || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 DROP drain has no admitted application");
    for (size_t index = 0; index < app->provider_count; ++index) {
        application_provider *provider = app->providers[index];
        if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->native_q3_wire || !provider->constructed ||
            !provider->attached || provider->close_pending) continue;
        if (!application_native_q3_clients_drain_provider(provider, error)) return false;
    }
    return true;
}

bool application_native_q3_client_scoreboard(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    uint32_t recipient;
    qa_q3_source_client_counts ranks;
    qa_q3_source_team_state team_state;
    int32_t time;
    if (!source(provider, actor, &recipient, error) ||
        !qa_q3_source_clock(provider->state.q3, &time, error) ||
        !qa_q3_source_client_counts_read(provider->state.q3, &ranks, error) ||
        !qa_q3_source_team_state_read(provider->state.q3, &team_state, error)) return false;
    char entries[1025] = {0}, row[1024], output[1152];
    size_t used = 0, included = 0;
    for (int32_t index = 0; index < ranks.num_connected; ++index) {
        uint32_t slot = ranks.sorted_clients[index];
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        qa_q3_player_state ps;
        qa_q3_entity entity;
        qa_q3_wire_visibility visibility;
        qa_q3_player published;
        if (!qa_q3_source_binding_read(provider->state.q3, slot, &binding, error) ||
            !qa_q3_client_slot_read(provider->state.q3, slot, &client, error) ||
            !qa_q3_player_read(provider->state.q3, binding.actor, &ps) ||
            !qa_q3_wire_entity_read(provider->state.q3, slot, &entity, &visibility, error) ||
            !qa_q3_wire_player_read(provider->state.q3, slot, &published, error)) return false;
        int32_t ping = client.connected == QA_Q3_CLIENT_CONNECTING ? -1 : published.ping < 999 ? published.ping : 999;
        uint32_t bits = (uint32_t)ps.accuracy_hits * 100u;
        int32_t numerator;
        memcpy(&numerator, &bits, sizeof(numerator));
        bits = ps.accuracy_shots ? (uint32_t)((int64_t)numerator / ps.accuracy_shots) : 0;
        int32_t accuracy;
        memcpy(&accuracy, &bits, sizeof(accuracy));
        const int32_t values[14] = {(int32_t)slot, published.persistant[0], ping,
            difference(time, client.enter_time_ms) / 60000, 0, entity.powerups,
            accuracy, published.persistant[9], published.persistant[10],
            published.persistant[13], published.persistant[11], published.persistant[12],
            published.persistant[2] == 0 && published.persistant[8] == 0, published.persistant[14]};
        char fields[14][12];
        for (size_t field = 0; field < 14; ++field) qa_format_q3_integer(values[field], fields[field]);
        int length = snprintf(row, sizeof(row), " %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
            fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6],
            fields[7], fields[8], fields[9], fields[10], fields[11], fields[12], fields[13]);
        if (length < 0 || (size_t)length >= sizeof(row))
            return application_fail(error, QA_ERROR_FORMAT, "native Q3 scoreboard row overflow");
        if (used + (size_t)length > 1024) break;
        memcpy(entries + used, row, (size_t)length + 1);
        used += (size_t)length;
        ++included;
    }
    char red_score[12], blue_score[12];
    qa_format_q3_integer(team_state.team_scores[1], red_score);
    qa_format_q3_integer(team_state.team_scores[2], blue_score);
    snprintf(output, sizeof(output), "scores %zu %s %s%s",
        included, red_score, blue_score, entries);
    return application_native_q3_send_command(provider, (int32_t)recipient, output, error) &&
           source(provider, actor, &recipient, error);
}

bool application_rankings_source_effect(qa_application *app, application_provider *provider,
    qa_actor_id actor, application_ranking_source_effect effect, qa_error *error)
{
    uint32_t slot;
    if (!app || !provider || provider->application != app || !source(provider, actor, &slot, error)) return false;
    switch (effect) {
    case APPLICATION_RANKING_SPECTATOR: {
        qa_q3_client_session sess;
        if (!session(provider, actor, &sess, error)) return false;
        sess.team = 3;
        sess.spectator_state = QA_Q3_SPECTATOR_FREE;
        return qa_q3_client_session_slot_write(provider->state.q3, slot,
                   QA_Q3_CLIENT_SESSION_TEAM | QA_Q3_CLIENT_SESSION_STATE, &sess, error) &&
               application_native_q3_client_spawn(provider, actor, NULL, NULL, error);
    }
    case APPLICATION_RANKING_ACTIVATE:
        return application_native_q3_client_set_team(provider, actor, "free", error);
    case APPLICATION_RANKING_SCOREBOARD:
        return application_native_q3_client_scoreboard(provider, actor, error);
    case APPLICATION_RANKING_DROP_BOT:
        return application_native_q3_wire_drop(provider, slot,
            "Bots cannot participate in ranked games", error);
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "unknown native Q3 ranking effect");
}

static bool set_team(application_provider *provider, qa_actor_id actor,
    const char *request, bool *accepted, bool *changed, qa_error *error)
{
    if (accepted) *accepted = false;
    if (changed) *changed = false;
    qa_application *app = provider->application;
    uint32_t slot, maximum;
    qa_q3_client_session sess;
    qa_q3_player_state player;
    qa_q3_source_client_counts ranks;
    qa_q3_native_client persistent;
    qa_mode_id mode;
    qa_mode_view rules;
    bool q3_rules;
    int32_t game_type, balance, max_players, time;
    if (!request || !source(provider, actor, &slot, error) ||
        !client_mode(provider,&mode,error)||!native_rules(provider,mode,&rules,&q3_rules,error)||
        !session(provider, actor, &sess, error) ||
        !qa_q3_player_read(provider->state.q3, actor, &player) ||
        !qa_q3_source_client_counts_read(provider->state.q3, &ranks, error) ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error) ||
        !qa_q3_source_clock(provider->state.q3, &time, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_TEAM_FORCE_BALANCE, &balance, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_MAX_GAME_CLIENTS, &max_players, error)) return false;
    int32_t team = 0, state = QA_Q3_SPECTATOR_NOT, client = 0;
    if (named(request, "scoreboard") || named(request, "score")) {
        team = 3; state = QA_Q3_SPECTATOR_SCOREBOARD;
    } else if (named(request, "follow1") || named(request, "follow2")) {
        team = 3; state = QA_Q3_SPECTATOR_FOLLOW;
        client = named(request, "follow1") ? -1 : -2;
    } else if (named(request, "spectator") || named(request, "s")) {
        team = 3; state = QA_Q3_SPECTATOR_FREE;
    } else if (qa_game_type_is_team(game_type)) {
        if (named(request, "red") || named(request, "r")) team = 1;
        else if (named(request, "blue") || named(request, "b")) team = 2;
        else if (!application_native_q3_client_pick_team(provider, (int32_t)slot, &team, error)) return false;
        if (balance) {
            int32_t counts[2];
            if (!team_counts(provider,mode,player.client_number,counts,error)) return false;
            if (counts[team - 1] - counts[2 - team] > 1) {
                const char *text = team == 1 ? "cp \"Red team has too many players.\\n\"" :
                    "cp \"Blue team has too many players.\\n\"";
                return application_native_q3_send_command(provider, player.client_number, text, error) &&
                    source(provider, actor, &slot, error);
            }
        }
    }
    if ((game_type == 1 && ranks.num_non_spectator >= 2) ||
        (max_players > 0 && ranks.num_non_spectator >= max_players)) team = 3;
    int32_t old_team = sess.team;
    if (team == old_team && team != 3) {
        if (accepted) *accepted = true;
        return true;
    }
    qa_combat_state combat;
    if (!qa_combat_read(app->combat, actor, &combat, error)) return false;
    if (combat.health <= 0 &&
        (!qa_q3_client_copy_body_queue(provider->state.q3, actor, error) ||
         !source(provider, actor, &slot, error))) return false;
    if (!qa_q3_client_team_state(provider->state.q3, actor, 0, error)) return false;
    if (old_team != 3) {
        qa_damage_request death = {.target = actor, .amount = 100000,
            .attack = {.attacker = actor, .inflictor = actor, .weapon_provider = provider->owner,
                .time_ns = (uint64_t)(uint32_t)time * UINT64_C(1000000),
                .cause = {.kind = QA_CAUSE_Q3, .source.q3 = {.means_of_death = 20, .flags = 32}}}};
        if (!application_source_force_death(app, &death, 0, error) ||
            !source(provider, actor, &slot, error)) return false;
    }
    if (team == 3) sess.spectator_time_ms = time;
    sess.team = team; sess.spectator_state = state; sess.spectator_client = client;
    sess.team_leader = 0;
    uint32_t fields = QA_Q3_CLIENT_SESSION_TEAM | QA_Q3_CLIENT_SESSION_STATE |
        QA_Q3_CLIENT_SESSION_CLIENT | QA_Q3_CLIENT_SESSION_LEADER |
        (team == 3 ? QA_Q3_CLIENT_SESSION_TIME : 0);
    if (!qa_q3_client_session_slot_write(provider->state.q3, slot, fields, &sess, error)) return false;
    if (q3_rules &&
        (!qa_modes_join(app->modes,mode,actor,
             team == 1 ? rules.rules.teams[0] : team == 2 ? rules.rules.teams[1] : 0, team == 3, error) ||
         !source(provider, actor, &slot, error))) return false;
    if (team == 1 || team == 2) {
        int32_t leader = -1;
        uint32_t own_flags, leader_flags = 0;
        for (uint32_t candidate = 0; candidate < maximum; ++candidate) {
            qa_q3_native_client other;
            if (!qa_q3_client_slot_read(provider->state.q3, candidate, &other, error)) return false;
            if (other.connected != QA_Q3_CLIENT_DISCONNECTED && other.session.team == team &&
                other.session.team_leader) { leader = (int32_t)candidate; break; }
        }
        if (!qa_q3_client_server_flags(provider->state.q3, slot, &own_flags, error) ||
            (leader >= 0 && !qa_q3_client_server_flags(provider->state.q3, (uint32_t)leader, &leader_flags, error))) return false;
        if ((leader < 0 || (!(own_flags & 8u) && (leader_flags & 8u))) &&
            (!application_native_q3_votes_set_leader(provider, team, (int32_t)slot, error) ||
             !source(provider, actor, &slot, error))) return false;
    }
    if ((old_team == 1 || old_team == 2) &&
        (!application_native_q3_votes_check_team_leader(provider, old_team, error) ||
         !source(provider, actor, &slot, error))) return false;
    if (!qa_q3_client_read(provider->state.q3, actor, &persistent, error)) return false;
    const char *announcement = team == 1 ? "joined the red team." : team == 2 ? "joined the blue team." :
        team == 3 && old_team != 3 ? "joined the spectators." : team == 0 ? "joined the battle." : NULL;
    if (announcement) {
        char text[128];
        snprintf(text, sizeof(text), "cp \"%s^7 %s\n\"", persistent.netname, announcement);
        if (!application_native_q3_send_command(provider, -1, text, error) ||
            !source(provider, actor, &slot, error)) return false;
    }
    bool ok = application_native_q3_client_userinfo_changed(provider, actor, error) &&
        application_native_q3_client_begin(provider, actor, NULL, NULL, error);
    if (ok && accepted) *accepted = true;
    if (ok && changed) *changed = true;
    return ok;
}

bool application_native_q3_client_set_team(application_provider *provider, qa_actor_id actor,
    const char *request, qa_error *error)
{
    uint32_t slot;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    bool ok = set_team(provider, actor, request, NULL, NULL, error) && source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_stop_following_slot(application_provider *provider,
    uint32_t slot, qa_error *error)
{
    qa_actor_id actor;
    qa_q3_client_session sess;
    if (!slot_source(provider, slot, &actor, error) ||
        !qa_q3_client_session_slot_read(provider->state.q3, slot, &sess, error)) return false;
    application_snapshot_mutated(provider->application);
    sess.team = 3;
    sess.spectator_state = QA_Q3_SPECTATOR_FREE;
    return qa_q3_client_persistent_team(provider->state.q3, slot, 3, error) &&
        qa_q3_client_session_slot_write(provider->state.q3, slot,
            QA_Q3_CLIENT_SESSION_TEAM | QA_Q3_CLIENT_SESSION_STATE, &sess, error) &&
        qa_q3_wire_client_stop_following(provider->state.q3, slot, error) &&
        qa_q3_client_slot_stop_following_state(provider->state.q3, slot, error) &&
        slot_current(provider, slot, actor, error);
}

bool application_native_q3_mode_team_request(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_team_id team, bool spectator, bool automatic,
    int32_t spectator_state, int32_t spectator_client, bool *accepted,
    bool *changed, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    uint32_t slot;
    qa_mode_view view;
    if (!provider || !accepted || !changed || !source(provider, actor, &slot, error) ||
        !qa_modes_read(app->modes, mode, &view, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    const char *request = spectator ? spectator_state == QA_Q3_SPECTATOR_SCOREBOARD ? "scoreboard" :
        spectator_state == QA_Q3_SPECTATOR_FOLLOW && spectator_client == -1 ? "follow1" :
        spectator_state == QA_Q3_SPECTATOR_FOLLOW && spectator_client == -2 ? "follow2" : "spectator" :
        automatic ? "free" : team == view.rules.teams[0] && team ? "red" :
        team == view.rules.teams[1] && team ? "blue" : "free";
    bool ok = set_team(provider, actor, request, accepted, changed, error) &&
        source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

static bool print_client(application_provider *provider, uint32_t slot,
    const char *text, qa_error *error)
{
    char output[1152];
    int written = snprintf(output, sizeof(output), "print \"%s\"", text);
    if (written < 0 || (size_t)written >= sizeof(output))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 client print command exceeds its output buffer");
    return application_native_q3_send_command(provider, (int32_t)slot, output, error);
}

static bool stop_following(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    uint32_t slot;
    qa_q3_native_client client;
    return source(provider, actor, &slot, error) &&
        application_native_q3_client_stop_following_slot(provider, slot, error) &&
        qa_q3_client_read(provider->state.q3, actor, &client, error) &&
        connection(provider, actor, client.connected, error);
}

bool application_native_q3_mode_stop_following(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_native_q3_mode_source_provider(app, mode);
    return provider && stop_following(provider, actor, error);
}

static bool sanitized(const char *input, char out[1024], qa_error *error)
{
    size_t used = 0;
    for (size_t index = 0; index < 1023 && input[index]; ++index) {
        unsigned char byte = (unsigned char)input[index];
        if (byte == 27) {
            if (index == 1022 || !input[index + 1])
                return application_fail(error, QA_ERROR_FORMAT, "Q3 SanitizeString ESC pair crosses its source buffer");
            ++index;
        } else if (byte >= 32 && byte < 128) {
            if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
            out[used++] = (char)byte;
        }
    }
    out[used] = 0;
    return true;
}

static bool follow_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, int cycle, qa_error *error)
{
    uint32_t slot, maximum;
    qa_q3_client_session sess;
    int32_t game_type, target = -1;
    if (!source(provider, actor, &slot, error) || !session(provider, actor, &sess, error) ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error)) return false;
    if (!cycle && command->argc != 2)
        return sess.spectator_state != QA_Q3_SPECTATOR_FOLLOW || stop_following(provider, actor, error);
    if (!cycle) {
        char text[1024], name[1024], output[1152];
        size_t length = strlen(command->argv[1]);
        if (length >= sizeof(text)) length = sizeof(text) - 1;
        memcpy(text, command->argv[1], length); text[length] = 0;
        if (text[0] >= '0' && text[0] <= '9') {
            target = integer(text);
            if (target < 0 || (uint32_t)target >= maximum) {
                char value[12];
                qa_format_q3_integer(target, value);
                snprintf(output, sizeof(output), "Bad client slot: %s\n", value);
                return print_client(provider, slot, output, error);
            }
            qa_q3_native_client other;
            if (!qa_q3_client_slot_read(provider->state.q3, (uint32_t)target, &other, error)) return false;
            if (other.connected != QA_Q3_CLIENT_CONNECTED) {
                char value[12];
                qa_format_q3_integer(target, value);
                snprintf(output, sizeof(output), "Client %s is not active\n", value);
                return print_client(provider, slot, output, error);
            }
        } else {
            if (!sanitized(text, name, error)) return false;
            for (uint32_t candidate = 0; candidate < maximum; ++candidate) {
                qa_q3_native_client other;
                char cleaned[1024];
                if (!qa_q3_client_slot_read(provider->state.q3, candidate, &other, error)) return false;
                if (other.connected != QA_Q3_CLIENT_CONNECTED) continue;
                if (!sanitized(other.netname, cleaned, error)) return false;
                if (!strcmp(name, cleaned)) { target = (int32_t)candidate; break; }
            }
            if (target < 0) {
                snprintf(output, sizeof(output), "User %s is not on the server\n", text);
                return print_client(provider, slot, output, error);
            }
        }
        qa_q3_client_session other;
        if (!qa_q3_client_session_slot_read(provider->state.q3, (uint32_t)target, &other, error)) return false;
        if (target == (int32_t)slot || other.team == 3) return true;
    }
    if (game_type == 1 && sess.team == 0) {
        sess.losses = difference(sess.losses, -1);
        if (!qa_q3_client_session_slot_write(provider->state.q3, slot,
                QA_Q3_CLIENT_SESSION_LOSSES, &sess, error)) return false;
    }
    if ((!cycle && sess.team != 3) || (cycle && sess.spectator_state == QA_Q3_SPECTATOR_NOT)) {
        if (!application_native_q3_client_set_team(provider, actor, "spectator", error) ||
            !source(provider, actor, &slot, error)) return false;
    }
    if (!session(provider, actor, &sess, error)) return false;
    if (cycle) {
        int32_t original = sess.spectator_client;
        target = original;
        bool found = false;
        for (uint32_t visited = 0; visited < maximum; ++visited) {
            int64_t next = (int64_t)target + cycle;
            target = next >= maximum ? 0 : next < 0 ? (int32_t)maximum - 1 : (int32_t)next;
            qa_q3_native_client other;
            qa_q3_client_session other_sess;
            if (!qa_q3_client_slot_read(provider->state.q3, (uint32_t)target, &other, error) ||
                !qa_q3_client_session_slot_read(provider->state.q3, (uint32_t)target, &other_sess, error)) return false;
            if (other.connected == QA_Q3_CLIENT_CONNECTED && other_sess.team != 3) {
                found = true; break;
            }
            if (target == original) return true;
        }
        if (!found) return application_fail(error, QA_ERROR_FORMAT,
            "Q3 FollowCycle cannot terminate from its automatic source sentinel");
    }
    sess.spectator_state = QA_Q3_SPECTATOR_FOLLOW;
    sess.spectator_client = target;
    return qa_q3_client_session_slot_write(provider->state.q3, slot,
        QA_Q3_CLIENT_SESSION_STATE | QA_Q3_CLIENT_SESSION_CLIENT, &sess, error);
}

static bool team_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, qa_error *error)
{
    uint32_t slot;
    qa_q3_client_session sess;
    qa_q3_native_client client;
    int32_t time, game_type;
    if (!source(provider, actor, &slot, error) || !session(provider, actor, &sess, error) ||
        !qa_q3_client_read(provider->state.q3, actor, &client, error) ||
        !qa_q3_source_clock(provider->state.q3, &time, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error)) return false;
    if (command->argc != 2)
        return print_client(provider, slot, sess.team == 1 ? "Red team\n" :
            sess.team == 2 ? "Blue team\n" : sess.team == 3 ? "Spectator team\n" : "Free team\n", error);
    if (client.switch_team_time_ms > time)
        return print_client(provider, slot, "May not switch teams more than once per 5 seconds.\n", error);
    if (game_type == 1 && sess.team == 0) {
        sess.losses = difference(sess.losses, -1);
        if (!qa_q3_client_session_slot_write(provider->state.q3, slot,
                QA_Q3_CLIENT_SESSION_LOSSES, &sess, error)) return false;
    }
    char request[1024];
    size_t length = strlen(command->argv[1]);
    if (length >= sizeof(request)) length = sizeof(request) - 1;
    memcpy(request, command->argv[1], length); request[length] = 0;
    if (!application_native_q3_client_set_team(provider, actor, request, error) ||
        !source(provider, actor, &slot, error)) return false;
    return qa_q3_client_team_switch_time(provider->state.q3, actor, difference(time, -5000), error);
}

static bool team_task(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, qa_error *error)
{
    if (command->argc != 2) return true;
    uint32_t slot;
    const char *current;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_wire_userinfo_read(provider, slot, &current, error)) return false;
    char info[1024], value[16];
    size_t length = strlen(current);
    if (length >= sizeof(info)) length = sizeof(info) - 1;
    memcpy(info, current, length); info[length] = 0;
    qa_format_q3_integer(integer(command->argv[1]), value);
    if (!qa_q3_info_set(info, sizeof(info), "teamtask", value, error) ||
        !application_native_q3_wire_userinfo(provider, slot, info, error)) return false;
    return application_native_q3_client_userinfo_changed(provider, actor, error);
}

static bool level_shot(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    uint32_t slot;
    int32_t cheats, game_type;
    qa_combat_state combat;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_SV_CHEATS, &cheats, error)) return false;
    if (!cheats) return print_client(provider, slot, "Cheats are not enabled on this server.\n", error);
    if (!qa_combat_read(provider->application->combat, actor, &combat, error)) return false;
    if (combat.health <= 0) return print_client(provider, slot, "You must be alive to use this command.\n", error);
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_GAMETYPE, &game_type, error)) return false;
    if (game_type != 0) return print_client(provider, slot, "Must be in g_gametype 0 for levelshot\n", error);
    return application_native_q3_match_begin_intermission(provider, error) &&
        source(provider, actor, &slot, error) &&
        application_native_q3_send_command(provider, (int32_t)slot, "clientLevelShot", error);
}

bool application_native_q3_client_command(application_provider *provider, qa_actor_id actor,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    uint32_t slot;
    if (!handled || !command || !command->argc || !command->argv ||
        !source(provider, actor, &slot, error)) return false;
    for (size_t i = 0; i < command->argc; ++i)
        if (!command->argv[i]) return application_fail(error, QA_ERROR_ARGUMENT, "missing Q3 client command argument");
    *handled = true;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    application_snapshot_mutated(provider->application);
    qa_application *app = provider->application;
    bool chat_handled = false;
    bool ok = application_native_q3_chat_command(app, provider, actor, command, &chat_handled, error);
    int32_t intermission;
    if (ok && !chat_handled) {
        const char *name = command->argv[0];
        if (named(name, "score")) ok = application_native_q3_client_scoreboard(provider, actor, error);
        else if (!application_native_q3_match_intermission(provider, &intermission, error)) ok = false;
        else if (intermission)
            ok = application_native_q3_chat_intermission(app, provider, actor, command, error);
        else if (named(name, "team")) ok = team_command(provider, actor, command, error);
        else if (named(name, "levelshot")) ok = level_shot(provider, actor, error);
        else if (named(name, "follow") || named(name, "follownext") || named(name, "followprev"))
            ok = follow_command(provider, actor, command,
                named(name, "follownext") ? 1 : named(name, "followprev") ? -1 : 0, error);
        else if (named(name, "teamtask")) ok = team_task(provider, actor, command, error);
        else if (named(name, "gc")) ok = application_native_q3_game_command(app, provider, actor, command, error);
        else if (named(name, "callvote") || named(name, "vote") ||
            named(name, "callteamvote") || named(name, "teamvote")) {
            bool vote_handled;
            ok = application_native_q3_votes_command(provider, actor, command, &vote_handled, error) &&
                (vote_handled || application_fail(error, QA_ERROR_ARGUMENT,
                    "native Q3 vote command lost its source dispatcher"));
        }
        else {
            bool source_handled = false;
            ok = qa_q3_game_console_command(provider->state.q3, actor, command, &source_handled, error);
            if (ok && !source_handled) {
                qa_mode_id mode;
                bool found;
                ok=application_native_q3_source_mode(provider,&mode,&found,error);
                if(ok&&found) ok=qa_modes_console_command(app->modes,mode,actor,command,&source_handled,error);
            }
            if (ok && !source_handled) {
                char text[1056];
                snprintf(text, sizeof(text), "unknown cmd %.1023s\n", name);
                ok = print_client(provider, slot, text, error);
            }
        }
    }
    if (ok) ok = source(provider, actor, &slot, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_source_client_command(application_provider *provider,qa_actor_id actor,
    const qa_command_invocation *command,bool *handled,qa_error *error)
{
    if(!handled) return application_fail(error,QA_ERROR_ARGUMENT,"Source Q3 command requires its dispatch result");
    *handled=false;
    application_native_q3_source_command_scope scope={0};
    if(!application_native_q3_source_command_begin(provider,actor,command,&scope,error)) return false;
    bool ok=application_native_q3_client_command(provider,actor,command,handled,error);
    qa_error cleanup={0};
    bool returned=application_native_q3_source_command_end(&scope,&cleanup);
    if(ok&&!returned&&error) *error=cleanup;
    return ok&&returned;
}

bool application_native_q3_client_text(application_provider *provider, qa_actor_id actor,
    const char *text, qa_error *error)
{
    uint32_t slot;
    qa_console *console;
    qa_command_context context;
    if (!text || !source(provider, actor, &slot, error) ||
        !application_native_q3_console_at(provider, &console, NULL, &context) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    context.origin=QA_COMMAND_REMOTE; context.actor=actor; context.seat=UINT32_MAX;
    bool ok = qa_application_capture_command_context(provider->application,&context,&context,error) &&
        qa_console_execute_now(console, &context, text, error);
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_think_policy(application_provider *provider, qa_actor_id actor,
    const qa_q3_usercmd *received, qa_q3_usercmd *accepted, int32_t *msec, bool *run, qa_error *error)
{
    uint32_t slot;
    qa_q3_native_client client;
    qa_q3_client_session sess;
    int32_t time, previous;
    if (!received || !accepted || !msec || !run || !source(provider, actor, &slot, error) ||
        !qa_q3_client_read(provider->state.q3, actor, &client, error) ||
        !session(provider, actor, &sess, error) ||
        !qa_q3_source_clock(provider->state.q3, &time, error) ||
        !qa_q3_client_command_time(provider->state.q3, actor, &previous, error)) return false;
    *accepted = *received;
    *run = false; *msec = 0;
    if (client.connected != QA_Q3_CLIENT_CONNECTED) return true;
    int32_t maximum = difference(time, -200), minimum = difference(time, 1000);
    if (accepted->serverTime > maximum) accepted->serverTime = maximum;
    if (accepted->serverTime < minimum) accepted->serverTime = minimum;
    if (!qa_q3_client_command(provider->state.q3, actor, accepted, error)) return false;
    *msec = difference(accepted->serverTime, previous);
    if (*msec < 1 && sess.spectator_state != QA_Q3_SPECTATOR_FOLLOW) return true;
    if (*msec > 200) *msec = 200;
    int32_t cached, fixed;
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_PMOVE_MSEC, &cached, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_PMOVE_FIXED, &fixed, error)) return false;
    bool use_fixed = fixed != 0 || client.pmove_fixed;
    if ((cached < 8 || cached > 33) &&
        !application_native_q3_settings_force_set(provider, APPLICATION_Q3_SETTING_PMOVE_MSEC, cached < 8 ? "8" : "33", error)) return false;
    if (use_fixed) {
        if (cached <= 0) return application_fail(error, QA_ERROR_ARGUMENT, "fixed source pmove requires a positive cached step");
        int32_t sum = difference(accepted->serverTime, difference(1, cached));
        uint32_t bits = (uint32_t)(sum / cached) * (uint32_t)cached;
        memcpy(&accepted->serverTime, &bits, sizeof(bits));
        if (!qa_q3_client_command(provider->state.q3, actor, accepted, error)) return false;
    }
    *run = true;
    return true;
}

bool application_native_q3_client_spectator_buttons(application_provider *provider,
    qa_actor_id actor, const qa_q3_usercmd *command, qa_error *error)
{
    uint32_t slot, old_buttons;
    if (!command || !source(provider, actor, &slot, error) ||
        !qa_q3_client_buttons(provider->state.q3, actor, (uint32_t)command->buttons, false, &old_buttons, error)) return false;
    return !((uint32_t)command->buttons & 1u) || (old_buttons & 1u) ||
        follow_command(provider, actor, NULL, 1, error);
}

bool application_native_q3_client_movement_parameters(application_provider *provider,
    qa_actor_id actor, bool source_client, int32_t *pm_type, int32_t *gravity, int32_t *speed,
    bool *spectator, qa_error *error)
{
    uint32_t slot;
    qa_combat_state combat;
    qa_application *app = provider ? provider->application : NULL;
    bool selected = app && application_provider_for(app, actor, QA_ROLE_MOVEMENT, "") == provider;
    if (!pm_type || !gravity || !speed || !spectator || !app || app->destroy_requested ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        (!source_client && !selected) ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        actor.slot >= app->control_capacity ||
        !app->controls[actor.slot].active || app->controls[actor.slot].retired ||
        !qa_actor_id_equal(app->controls[actor.slot].player.actor, actor) ||
        !qa_combat_read(app->combat, actor, &combat, error)) return false;
    application_control_record *control = &app->controls[actor.slot];
    bool foreign = control->player.state.kind != QA_RULESET_Q3;
    bool noclip;
    qa_q3_wire_policy policy = {0};
    if (source_client) {
        qa_q3_client_session sess;
        qa_q3_player_state player;
        if (!source(provider, actor, &slot, error) ||
            !session(provider, actor, &sess, error) ||
            !qa_q3_player_read(provider->state.q3, actor, &player) ||
            (foreign && !qa_q3_wire_player_policy_read(provider->state.q3, actor, &policy, error)))
            return false;
        *spectator = sess.team == 3;
        noclip = player.noclip;
    } else {
        if (foreign) return application_fail(error, QA_ERROR_ARGUMENT,
            "Selected Q3 movement requires its actual Q3 control state");
        *spectator = control->player.state.data.q3.movement_type == 2;
        noclip = control->player.state.data.q3.movement_type == 1;
    }
    if (*spectator) {
        *pm_type = 2;
        *speed = 400;
        *gravity = foreign ? policy.gravity : control->player.state.data.q3.gravity;
    } else {
        float gravity_value, speed_value;
        if (!application_native_q3_settings_number_at(provider, APPLICATION_Q3_SETTING_G_GRAVITY, &gravity_value, error) ||
            !application_native_q3_settings_number_at(provider, APPLICATION_Q3_SETTING_G_SPEED, &speed_value, error)) return false;
        *pm_type = noclip ? 1 : combat.health <= 0 ? 3 : 0;
        *gravity = qa_source_float_to_i32(gravity_value);
        *speed = qa_source_float_to_i32(speed_value);
        application_provider *equipment = application_provider_for(app, actor, QA_ROLE_EQUIPMENT, "");
        if (!equipment || !equipment->constructed || !equipment->attached || equipment->close_pending)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 movement lost its selected equipment owner");
        if (equipment->kind == APPLICATION_PROVIDER_Q3) {
            qa_q3_player_state speed_player;
            if (!equipment->state.q3 ||
                !qa_q3_player_read(equipment->state.q3, actor, &speed_player) ||
                !(speed_player.selections & QA_Q3_EQUIPMENT))
                return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 movement lost its selected equipment state");
            double multiplier = equipment->product && !strcmp(equipment->product->campaign, "missionpack") &&
                speed_player.persistent == QA_Q3_P_SCOUT ? 1.5 : speed_player.powerups[QA_Q3_P_HASTE] ? 1.3 : 1.0;
            if (multiplier != 1.0) {
                double scaled = *speed * multiplier;
                *speed = scaled >= -2147483648.0 && scaled < 2147483648.0 ? (int32_t)scaled : INT32_MIN;
            }
        } else if ((equipment->kind == APPLICATION_PROVIDER_QVM ||
                    equipment->kind == APPLICATION_PROVIDER_NATIVE) &&
                   equipment->component.clock.kind == QA_RULESET_Q3) {
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "selected guest Q3 equipment has no typed movement speed capability");
        }
    }
    if (!foreign) return true;
    policy.pm_type = *pm_type;
    policy.gravity = *gravity;
    policy.speed = *speed;
    return qa_q3_wire_player_policy(provider->state.q3, actor, &policy, error);
}

bool application_native_q3_client_think_special(application_provider *provider,
    qa_actor_id actor, const qa_q3_usercmd *command, int32_t msec,
    bool *handled, qa_error *error)
{
    (void)msec;
    uint32_t slot, old_buttons;
    int32_t intermission;
    qa_q3_client_session sess;
    if (!command || !handled || !source(provider, actor, &slot, error) ||
        !session(provider, actor, &sess, error) ||
        !application_native_q3_match_intermission(provider, &intermission, error)) return false;
    *handled = false;
    if (intermission) {
        *handled = true;
        if (!qa_q3_client_player_flags_update(provider->state.q3, slot, 0, 0x1100u, error) ||
            !qa_q3_client_buttons(provider->state.q3, actor, (uint32_t)command->buttons, false, &old_buttons, error)) return false;
        return !((uint32_t)command->buttons & 5u & (old_buttons ^ (uint32_t)command->buttons)) ||
            qa_q3_client_ready(provider->state.q3, actor, true, error);
    }
    if (sess.team == 3 && sess.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD) {
        *handled = true;
        return true;
    }
    if (sess.team == 3 && sess.spectator_state == QA_Q3_SPECTATOR_FOLLOW) {
        *handled = true;
        return application_native_q3_client_spectator_buttons(provider, actor, command, error);
    }
    return true;
}

bool application_native_q3_source_client_run(void *opaque, qa_actor_id actor,
    const qa_source_frame *frame, qa_error *error)
{
    application_provider *provider = opaque;
    qa_application *app = provider ? provider->application : NULL;
    qa_source_frame active;
    qa_q3_native_client client;
    uint32_t slot;
    int32_t time;
    if (!app || !frame || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending || app->destroy_requested ||
        app->operation != APPLICATION_ADVANCING || frame->provider != provider->owner ||
        frame->kind != QA_RULESET_Q3 || frame->phase != QA_ENTITY_PHYSICS ||
        !qa_session_active_frame(app->session, provider->owner, &active) ||
        active.kind != frame->kind || active.phase != frame->phase || active.number != frame->number ||
        active.start_ns != frame->start_ns || active.time_ns != frame->time_ns ||
        active.elapsed_ns != frame->elapsed_ns ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !qa_q3_client_slot_read(provider->state.q3, slot, &client, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 G_RunClient has no actual source frame");
    if (client.connected != QA_Q3_CLIENT_CONNECTED) return true;
    if (!source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    bool deferred;
    bool ok = application_native_q3_client_deferred(provider, actor, &deferred, error);
    if (ok && deferred) {
        qa_q3_usercmd command = client.command;
        ok = qa_q3_source_clock(provider->state.q3, &time, error) &&
            ((uint32_t)time == (uint32_t)(frame->time_ns / UINT64_C(1000000)) ||
             application_fail(error, QA_ERROR_ARGUMENT, "native Q3 G_RunClient clock differs from its ENTRY"));
        if (ok) {
            command.serverTime = time;
            ok = qa_q3_client_command(provider->state.q3, actor, &command, error) &&
                application_control_q3_client_think(provider, actor, &command, error) != APPLICATION_CONTROL_FAILED &&
                source(provider, actor, &slot, error);
        }
    }
    application_native_q3_console_release(provider);
    return ok;
}

bool application_native_q3_client_deferred(application_provider *provider,
    qa_actor_id actor, bool *out, qa_error *error)
{
    uint32_t slot, flags;
    int32_t synchronous;
    if (!out || !source(provider, actor, &slot, error) ||
        !qa_q3_client_server_flags(provider->state.q3, slot, &flags, error) ||
        !application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_SYNCHRONOUS_CLIENTS, &synchronous, error)) return false;
    *out = (flags & 8u) != 0 || synchronous != 0;
    return true;
}
