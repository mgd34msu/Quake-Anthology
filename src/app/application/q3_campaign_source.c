#include "q3_campaign_source.h"
#include "map_players_private.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "guest_q3_private.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"

static application_provider *primary(qa_application *app)
{
    if (!app || !app->session || !app->world || app->destroy_requested ||
        app->state != QA_APPLICATION_RUNNING || app->q3_round_active || app->q3_world_restart ||
        app->routing_snapshot || !app->map_view_ready) return NULL;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || provider->application != app ||
        !provider->owner || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->launch->content || !provider->product ||
        provider->product->family != QA_GAME_Q3) return NULL;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        if (!provider->state.q3) return NULL;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        if (!engine || engine->provider != provider || engine->world != app->world || !engine->map_ready || engine->restore_pending ||
            engine->draining_clients || engine->round.phase != Q3G_ROUND_NONE || !engine->loaded_compatibility ||
            engine->loaded_max_clients < 1 || engine->loaded_max_clients > 64 ||
            !engine->game || engine->game->engine != engine || engine->game->kind != QA_QVM_GAME ||
            !engine->game->primary || !engine->game->host || engine->game->retired || !engine->game->ready ||
            !engine->game->initialized || !engine->game->committed) return NULL;
    }
    return provider;
}
static bool source_context(application_provider *provider, qa_application_q3_campaign *view, qa_error *error)
{
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        view->native_source = true; view->source_game = provider->state.q3;
        return application_native_q3_console_at(provider, &view->console, &view->cvars, NULL) &&
            view->console && view->cvars && qa_q3_source_clock(view->source_game, &view->source_time, error) &&
            qa_q3_source_match_context_read(view->source_game, &view->product, &view->match_start_time, error) &&
            application_native_q3_settings_integer(provider, "g_gametype", &view->game_type, error);
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    const char *start = qa_q3_configstring(&engine->gamestate, 21);
    if (!start || !*start) return application_fail(error, QA_ERROR_NOT_FOUND, "Original campaign GAME has no published source start time");
    const unsigned char *p = (const unsigned char *)start;
    while (*p && (*p <= 32 || *p >= 128)) ++p;
    uint32_t time = 0; bool negative = *p == '-';
    if (*p == '+' || *p == '-') ++p;
    while (*p >= '0' && *p <= '9') time = time * 10u + (uint32_t)(*p++ - '0');
    time = negative ? 0u - time : time;
    memcpy(&view->match_start_time, &time, sizeof(time));
    view->original_host = engine->game->host;
    qa_command_context command;
    view->console = qa_q3_host_console(view->original_host, &view->cvars, &command);
    view->product = engine->product; view->game_type = engine->loaded_game_type;
    view->source_time = engine->milliseconds;
    return view->console && view->cvars && command.owner == provider->owner && command.dialect == QA_CONSOLE_Q3;
}
static qa_fs_root *write_root(qa_vfs *content, qa_mount_id *id)
{
    *id = 0;
    for (size_t i = 0; i < qa_vfs_mount_count(content); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(content, i, &mount) && mount.writable) {
            *id = mount.id;
            return qa_vfs_mount_root(content, mount.id);
        }
    }
    return NULL;
}
bool qa_application_q3_campaign_read(qa_application *app, qa_actor_owner owner,
    qa_application_q3_campaign *out, qa_error *error)
{
    application_provider *provider = primary(app);
    if (!provider || (owner && owner != provider->owner) || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign requires its published physical Q3 GAME");
    qa_application_map_view map;
    qa_application_q3_campaign view = {.session = app->session,
        .publication = qa_application_launch(app), .launch = provider->launch,
        .source_owner = provider->owner,
        .content_product = provider->launch->selection.product, .content = provider->launch->content,
        .profile_root = app->user_files, .profile = app->progress,
        .publication_generation = app->publication_generation,
        .command_generation = app->command_generation, .map_revision = app->map_revision};
    if (!view.publication || !qa_application_map_read(app, &map) || !source_context(provider, &view, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign has no genuine GAME content, registry or round context");
    view.map = map.name; view.map_resource = map.resource;
    view.config_root = write_root(view.content, &view.write_mount);
    if (view.write_mount && !view.config_root)
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign writable source mount has no retained directory authority");
    *out = view;
    return true;
}
bool qa_application_q3_campaign_current(qa_application *app, const qa_application_q3_campaign *saved)
{
    qa_application_q3_campaign actual;
    return saved && qa_application_q3_campaign_read(app, saved->source_owner, &actual, NULL) &&
        actual.source_owner == saved->source_owner && actual.session == saved->session && actual.publication == saved->publication &&
        actual.launch == saved->launch && actual.source_game == saved->source_game &&
        actual.original_host == saved->original_host && actual.native_source == saved->native_source &&
        actual.content_product == saved->content_product && actual.product == saved->product &&
        actual.console == saved->console && actual.cvars == saved->cvars && actual.content == saved->content &&
        actual.write_mount == saved->write_mount && actual.config_root == saved->config_root &&
        actual.profile_root == saved->profile_root && actual.profile == saved->profile &&
        actual.map == saved->map && actual.map_resource == saved->map_resource &&
        actual.publication_generation == saved->publication_generation &&
        actual.command_generation == saved->command_generation && actual.map_revision == saved->map_revision &&
        actual.source_time == saved->source_time && actual.match_start_time == saved->match_start_time &&
        actual.game_type == saved->game_type;
}
bool qa_application_q3_campaign_postgame_context(qa_application *app,
    const qa_application_q3_campaign *view, const qa_command_context *command, qa_error *error)
{
    return (qa_application_q3_campaign_current(app, view) && command &&
        command->origin == QA_COMMAND_SERVER && command->dialect == QA_CONSOLE_Q3 &&
        command->owner == view->source_owner && qa_application_command_context_active(app, command)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Postgame command has no current actual GAME source");
}
bool qa_application_q3_campaign_local_seat(qa_application *app,
    const qa_application_q3_campaign *view, uint32_t slot,
    bool *found, uint32_t *seat, qa_actor_id *actor, qa_error *error)
{
    if (!found || !seat || !actor || !qa_application_q3_campaign_current(app, view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign seat lookup lost its genuine GAME cut");
    *found = false;
    qa_actor_id source_actor = {0}; uint32_t actual_slot;
    if (view->native_source) {
        uint32_t maximum; qa_q3_source_binding binding; qa_q3_native_client client;
        if (!qa_q3_source_max_clients(view->source_game, &maximum, error) || slot >= maximum ||
            !qa_q3_source_binding_read(view->source_game, slot, &binding, error) ||
            !qa_q3_client_slot_read(view->source_game, slot, &client, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "Campaign result names an invalid physical client");
        if (!binding.in_use || !binding.actor.registry || (binding.server_flags & 8u) ||
            client.connected == QA_Q3_CLIENT_DISCONNECTED || !app->players) return true;
        source_actor = binding.actor;
        if (!qa_q3_native_client_slot(view->source_game, source_actor, &actual_slot, error) || actual_slot != slot)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign physical client lost its full actor binding");
    } else {
        struct application_q3_guest *engine = q3g_engine(primary(app));
        if (slot >= (uint32_t)engine->loaded_max_clients || slot >= 64)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original campaign result names an invalid physical client");
        const q3g_client *client = engine->clients + slot;
        if (!client->allocated || !client->connected || !client->begun || client->bot ||
            client->pending_retirement || client->disconnect_pending || !client->actor.registry || !app->players) return true;
        source_actor = client->actor;
        if (!qa_q3_host_actor_slot(view->original_host, source_actor, &actual_slot, error) || actual_slot != slot)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Original campaign client lost its actual GAME actor binding");
    }
    if (!qa_actors_get(qa_session_actors(app->session), source_actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign physical client lost its full actor binding");
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (row->retiring || row->remote || row->bot || !qa_actor_id_equal(row->actor, source_actor)) continue;
        if (row->client_slot != slot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Campaign local roster differs from its physical GAME client");
        if (*found) return application_fail(error, QA_ERROR_ARGUMENT, "Campaign physical client has ambiguous local seats");
        *found = true; *seat = row->seat; *actor = row->actor;
    }
    return true;
}
bool qa_application_q3_campaign_player_name(qa_application *app,
    const qa_application_q3_campaign *view, uint32_t slot, char out[80], qa_error *error)
{
    if (!out || !qa_application_q3_campaign_current(app, view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign name lookup lost its actual GAME cut");
    char name[80] = {0}; qa_actor_id actor = {0}; uint32_t actual_slot;
    if (view->native_source) {
        uint32_t maximum; qa_q3_source_binding binding; qa_q3_native_client client;
        if (!qa_q3_source_max_clients(view->source_game, &maximum, error) || slot >= maximum ||
            !qa_q3_source_binding_read(view->source_game, slot, &binding, error) ||
            !qa_q3_client_slot_read(view->source_game, slot, &client, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "Campaign name lookup names an invalid physical client");
        if (!binding.actor.registry || binding.client_slot != (int32_t)slot ||
            client.connected == QA_Q3_CLIENT_DISCONNECTED ||
            !qa_q3_native_client_slot(view->source_game, binding.actor, &actual_slot, error) || actual_slot != slot)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign name has no live physical native client binding");
        if (!memchr(client.netname, 0, sizeof(client.netname)))
            return application_fail(error, QA_ERROR_FORMAT, "Campaign native client name is unterminated");
        actor = binding.actor;
        memcpy(name, client.netname, strlen(client.netname));
    } else {
        struct application_q3_guest *engine = q3g_engine(primary(app));
        if (slot >= (uint32_t)engine->loaded_max_clients || slot >= 64)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original campaign name lookup names an invalid physical client");
        const q3g_client *client = engine->clients + slot;
        if (!client->allocated || !client->connected || client->pending_retirement ||
            client->disconnect_pending || !client->actor.registry ||
            !qa_q3_host_actor_slot(view->original_host, client->actor, &actual_slot, error) || actual_slot != slot)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign name has no live physical original client binding");
        actor = client->actor;
        char value[8192];
        const char *info = qa_q3_configstring(&engine->gamestate, 544u + slot);
        if (!info || !qa_q3_info_value(info, "n", value, sizeof(value), error)) return false;
        size_t length = strlen(value);
        if (length >= sizeof(name)) length = sizeof(name) - 1;
        memcpy(name, value, length);
    }
    if (!qa_actors_get(qa_session_actors(app->session), actor) ||
        !qa_application_q3_campaign_current(app, view))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign name lost its current source actor");
    memcpy(out, name, sizeof(name));
    return true;
}
bool qa_application_q3_campaign_menu_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_campaign *source,
    qa_application_q3_client_context *client, qa_error *error)
{
    if (!source || !client)
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign menu requires source and actual CGAME recipient outputs");
    qa_application_q3_campaign view; qa_application_q3_client_context recipient;
    bool found; uint32_t actual_seat; qa_actor_id actor;
    if (!qa_application_q3_campaign_read(app, 0, &view, error) ||
        !qa_application_q3_client_context_read(app, receiver, seat, &recipient, error) ||
        recipient.native_source != view.native_source || recipient.source_owner != view.source_owner || recipient.source_cvars != view.cvars ||
        !qa_application_q3_campaign_local_seat(app, &view, recipient.source_client, &found, &actual_seat, &actor, error) ||
        !found || actual_seat != seat || !qa_actor_id_equal(actor, recipient.source_actor) ||
        !qa_application_q3_client_context_current(app, &recipient) || !qa_application_q3_campaign_current(app, &view))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Campaign menu recipient differs from its actual local GAME client");
    *source = view; *client = recipient;
    return true;
}
