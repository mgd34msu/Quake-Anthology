#include "guest_q3_restart.h"
#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "guest_input_private.h"
#include "qa/application_players.h"
#include "q3_world_restart.h"
#include "bots_private.h"
#include "guest_q3_client_console.h"

bool q3g_round_fail(struct application_q3_guest *engine, const qa_error *failure,
    qa_error *error)
{
    if (engine->round.phase != Q3G_ROUND_FAILED) {
        engine->round.failure = failure ? *failure : (qa_error){0};
        if (engine->round.failure.code == QA_OK)
            qa_error_set(&engine->round.failure, QA_ERROR_ARGUMENT, 0, "Q3 round source operation failed");
        engine->round.phase = Q3G_ROUND_FAILED;
    }
    if (error) *error = engine->round.failure;
    return false;
}

bool q3g_round_call(q3g_role *role, int32_t command, const int32_t *arguments,
    size_t count, int32_t *result, qa_error *error)
{
    bool prior = role->engine->round.source_entry;
    role->engine->round.source_entry = true;
    bool ok = q3g_call(role, command, arguments, count, result, error);
    role->engine->round.source_entry = prior;
    return ok;
}

static bool owner_idle(application_provider *provider, application_operation operation,
    struct application_q3_guest **out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !provider->constructed || !provider->attached || engine->restore_pending ||
        !engine->map_ready || !engine->game || !engine->game->host || engine->game->retired ||
        !engine->game->ready || !engine->game->committed || engine->calls || engine->draining_clients ||
        provider->application->operation != operation ||
        !qa_session_safe(provider->application->session) ||
        qa_session_faulted(provider->application->session) || !qa_world_idle(engine->world) ||
        !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires its qualified idle GAME owner");
    *out = engine;
    return true;
}

static bool owner_publication(application_provider *provider,
    struct application_q3_guest **out, qa_error *error)
{
    if (!provider) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires its source provider");
    application_operation operation = provider->application->operation;
    if (operation != APPLICATION_IDLE && operation != APPLICATION_CONFIGURING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round client publication requires an idle or configuring owner");
    return owner_idle(provider, operation, out, error);
}

bool application_q3_guest_round_ready(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!owner_idle(provider, APPLICATION_IDLE, &engine, error)) return false;
    if (engine->round.phase != Q3G_ROUND_NONE || !engine->game->initialized || !engine->entity_text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires a running source map");
    qa_cvars *cvars = NULL;
    qa_q3_host_console(engine->game->host, &cvars, NULL);
    const qa_cvar_view *game_type = cvars ? qa_cvars_find(cvars, "g_gametype") : NULL;
    const qa_cvar_view *clients = cvars ? qa_cvars_find(cvars, "sv_maxclients") : NULL;
    if (!engine->loaded_compatibility || !game_type || !clients ||
        game_type->modified || clients->modified ||
        game_type->integer != engine->loaded_game_type ||
        clients->integer != engine->loaded_max_clients)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Q3 round game type or client capacity differs from the admitted source map");
    if (engine->game->native && qa_native_host_profile(engine->game->native) != QA_NATIVE_Q3_VMMAIN)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 round requires its established GAME vmMain profile");
    if (engine->game->native &&
        !qa_native_restart_ready(qa_native_host_instance(engine->game->native), error)) return false;
    if (!qa_q3_host_round_ready(engine->game->host, error)) return false;
    qa_clock_state clock;
    if (!qa_session_clock(provider->application->session, provider->owner, &clock) ||
        clock.frame.provider != provider->owner || clock.frame.kind != QA_CLOCK_Q3 ||
        clock.frame.time_ns > UINT64_MAX - UINT64_C(400000000) || clock.frame_number > UINT64_MAX - 4)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round source clock cannot admit four settlement frames");
    for (uint32_t slot = 0; slot < 64; ++slot) {
        const q3g_client *client = &engine->clients[slot];
        if (client->pending_bot || client->pending_retirement || client->disconnect_pending || client->carry_pending || client->reserved ||
            (client->allocated && !client->connected))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round has an unfinished source client operation");
        if (!client->connected) continue;
        uint32_t actual_slot;
        if (!client->allocated || !client->actor.registry ||
            !application_guest_input_actor_idle(provider->application, client->actor) ||
            !qa_q3_host_actor_slot(engine->game->host, client->actor, &actual_slot, error) ||
            actual_slot != slot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round client/source binding is incomplete");
    }
    return true;
}

static bool readable(application_provider *provider, struct application_q3_guest **out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !provider->constructed || !provider->attached || engine->restore_pending ||
        !engine->map_ready || !engine->game || !engine->game->host || engine->game->retired ||
        !engine->game->initialized || !engine->game->committed ||
        (engine->round.phase != Q3G_ROUND_NONE && engine->round.phase != Q3G_ROUND_SETTLING) ||
        (provider->application->operation != APPLICATION_IDLE &&
         provider->application->operation != APPLICATION_CONFIGURING &&
         provider->application->operation != APPLICATION_ADVANCING))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source read requires a running GAME owner");
    *out = engine;
    return true;
}

bool application_q3_guest_world_seed(application_provider *provider, uint32_t *out,
    qa_error *error)
{
    struct application_q3_guest *engine;
    if (!out || !owner_idle(provider, APPLICATION_IDLE, &engine, error)) return false;
    if (!engine->game->initialized || engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 seed requires its running original source map");
    memcpy(out, &engine->random_seed, sizeof(*out));
    return true;
}

static bool world_readable(application_provider *provider,
    struct application_q3_guest **out, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!owner_idle(provider, APPLICATION_IDLE, &engine, error)) return false;
    if (!engine->game->initialized || engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 world read requires its running original source map");
    if (!qa_q3_host_round_ready(engine->game->host, error)) return false;
    *out = engine;
    return true;
}

bool application_q3_guest_retire_executors(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    if (!provider->constructed || engine->calls || engine->draining_clients ||
        engine->restore_pending || provider->application->operation != APPLICATION_CONFIGURING ||
        !qa_session_safe(provider->application->session) || !qa_world_idle(engine->world) ||
        qa_actors_count(qa_session_actors(provider->application->session)) ||
        !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 executor retirement requires its actual completed actor cut");
    qa_bot_runtime *runtime = application_bots_runtime(provider->application);
    for (q3g_role *role = engine->roles; role; role = role->next) {
        if (role->kind == QA_QVM_UI && !role->retired) {
            if (qa_q3_host_borrows_bots(role->host, runtime))
                return application_fail(error, QA_ERROR_ARGUMENT, "Live Q3 UI still borrows the retiring bot runtime");
            continue;
        }
        if (role->initialized || !role->retired)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 map executor has not completed its genuine Shutdown");
        if (!q3g_role_consume(role, error)) return false;
    }
    return true;
}

bool application_q3_guest_client_sources_retire(application_provider *provider,
    application_provider *old_game, application_provider *next_game,
    const qa_launch_choices *choices, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    qa_application *application = provider->application;
    if (!provider->constructed || engine->calls || engine->restore_pending || engine->draining_clients ||
        (application->operation != APPLICATION_CONFIGURING && application->operation != APPLICATION_DESTROYING) ||
        !qa_session_safe(application->session) || !qa_world_idle(engine->world) ||
        !application_q3_guest_idle(provider) ||
        (old_game && old_game->application != application) ||
        (next_game && (next_game->application != application || !choices)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client source retirement requires its actual world cut");
    if (!application_guest_q3_client_consoles_retarget(engine, old_game, next_game, choices, error)) return false;
    for (q3g_role *role = engine->roles; role; role = role->next) {
        if (role->kind != QA_QVM_UI || !role->local_client || role->retired) continue;
        if (!role->client_source || (application->operation == APPLICATION_CONFIGURING &&
            old_game && role->client_source != old_game))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 UI lease differs from the actual retiring GAME source");
        bool retained = false;
        if (role->client_source == next_game)
            for (size_t i = 0; choices && i < choices->seat_count; ++i)
                if (choices->seats[i].id == role->seat && i == role->client &&
                    q3g_selected_client_seat(provider, choices, role->kind, i)) retained = true;
        if (!retained && !q3g_role_shutdown(role, false, error)) return false;
    }
    return true;
}

bool application_q3_guest_bots_borrowed(const qa_application *application,
    const qa_bot_runtime *runtime)
{
    if (!application || !runtime) return false;
    for (application_provider *provider = application->live_providers; provider; provider = provider->next_live) {
        const struct application_q3_guest *engine = q3g_engine(provider);
        if (!engine) continue;
        for (const q3g_role *role = engine->roles; role; role = role->next)
            if (qa_q3_host_borrows_bots(role->host, runtime)) return true;
    }
    return false;
}

bool application_q3_guest_client_carry(application_provider *provider, uint32_t slot,
    qa_actor_id actor, const qa_q3_usercmd *command, qa_error *error)
{
    struct application_q3_guest *engine;
    application_q3_world_startup startup;
    if (!command || slot >= 64 || !owner_idle(provider, APPLICATION_CONFIGURING, &engine, error) ||
        !engine->game->initialized || engine->round.phase != Q3G_ROUND_NONE ||
        !application_q3_world_restart_source(provider->application, provider, &startup))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client command carry requires its actual fresh replacement");
    q3g_client *client = &engine->clients[slot];
    uint32_t actual;
    if (!client->carry_pending || !client->connected || !client->allocated || client->begun ||
        client->pending_retirement || !qa_actor_id_equal(client->actor, actor) ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor) ||
        !qa_q3_host_actor_slot(engine->game->host, actor, &actual, error) || actual != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 carried command has no genuine connected source slot");
    client->command = *command;
    client->carry_pending = false;
    return true;
}

bool application_q3_guest_cvar_owner(application_provider *provider, uint64_t *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!out || !engine || !provider->constructed || !provider->attached || provider->close_pending ||
        engine->restore_pending || engine->calls || engine->draining_clients ||
        !engine->game || !engine->game->host || !engine->game->ready ||
        !engine->game->service_owner || engine->round.phase != Q3G_ROUND_NONE ||
        !qa_world_idle(engine->world) || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cvar owner requires its idle actual GAME role");
    bool live = provider->attached && provider->application->operation == APPLICATION_IDLE &&
        engine->map_ready && engine->game->initialized && !engine->game->retired && engine->game->committed;
    bool handoff = engine->handoff_ready && !engine->game->initialized && engine->game->retired &&
        application_q3_world_restart_guest_shutdown(provider->application, provider);
    application_q3_world_startup startup;
    bool fresh = !engine->game->initialized && !engine->game->retired &&
        application_q3_world_restart_source(provider->application, provider, &startup);
    if (!live && !handoff && !fresh)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cvar owner is outside its actual source handoff");
    *out = engine->game->service_owner;
    return true;
}

bool application_q3_guest_world_source(application_provider *provider,
    int32_t *milliseconds, int32_t *game_type, uint32_t *seed, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!milliseconds || !game_type || !seed || !world_readable(provider, &engine, error)) return false;
    if (!engine->loaded_compatibility)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 world has no admitted source game type");
    int32_t clock;
    if (!application_q3_guest_round_clock(provider, &clock, error)) return false;
    *milliseconds = clock;
    *game_type = engine->loaded_game_type;
    memcpy(seed, &engine->random_seed, sizeof(*seed));
    return true;
}

static bool world_client(application_provider *provider, uint32_t slot,
    const q3g_client **out, qa_error *error)
{
    struct application_q3_guest *engine;
    if (slot >= 64 || !world_readable(provider, &engine, error)) return false;
    const q3g_client *client = &engine->clients[slot];
    if (!client->connected)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 source client is not connected");
    uint32_t actual_slot;
    if (!client->allocated || client->pending_retirement || client->pending_bot ||
        !client->actor.registry || !application_guest_input_actor_idle(provider->application, client->actor) ||
        !qa_q3_host_actor_slot(engine->game->host, client->actor, &actual_slot, error) || actual_slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 world client/source binding is incomplete");
    *out = client;
    return true;
}

bool application_q3_guest_world_userinfo(application_provider *provider, uint32_t slot,
    const char **out, qa_error *error)
{
    const q3g_client *client;
    if (!out || !world_client(provider, slot, &client, error)) return false;
    *out = client->userinfo ? client->userinfo : "";
    return true;
}

bool application_q3_guest_world_client_read(application_provider *provider, uint32_t slot,
    qa_actor_id *actor, qa_q3_usercmd *command, bool *bot, uint64_t *entered_ns, qa_error *error)
{
    const q3g_client *client;
    if (!actor || !command || !bot || !entered_ns || !world_client(provider, slot, &client, error)) return false;
    *actor = client->actor; *command = client->command;
    *bot = client->bot; *entered_ns = client->entered_ns;
    return true;
}

bool application_q3_guest_handoff_client_read(application_provider *provider, uint32_t slot,
    qa_actor_id *actor, qa_q3_usercmd *command, bool *bot, uint64_t *entered_ns,
    const char **userinfo, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!actor || !command || !bot || !entered_ns || !userinfo || slot >= 64 ||
        !engine || !engine->handoff_ready || engine->calls || engine->draining_clients ||
        engine->restore_pending || !engine->game || !engine->game->host ||
        !engine->game->retired || engine->game->initialized ||
        engine->round.phase != Q3G_ROUND_NONE || !qa_world_idle(engine->world) ||
        !application_q3_guest_idle(provider) ||
        !application_q3_world_restart_guest_shutdown(provider->application, provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client read is outside its actual shutdown handoff");
    const q3g_client *client = &engine->clients[slot];
    if (!client->connected || client->pending_retirement)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 shutdown did not retain this source client");
    uint32_t actual_slot;
    if (!client->allocated || client->pending_bot || !client->actor.registry ||
        !qa_actors_get(qa_session_actors(provider->application->session), client->actor) ||
        !qa_q3_host_actor_slot(engine->game->host, client->actor, &actual_slot, error) || actual_slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 shutdown client has no retained physical source binding");
    *actor = client->actor; *command = client->command; *bot = client->bot;
    *entered_ns = client->entered_ns; *userinfo = client->userinfo ? client->userinfo : "";
    return true;
}

bool application_q3_guest_snapshot_bit(application_provider *provider, uint8_t *out,
    qa_error *error)
{
    struct application_q3_guest *engine;
    if (!out || !readable(provider, &engine, error)) return false;
    *out = engine->local_snapshot_server_bit;
    return true;
}

bool application_q3_guest_round_clock(application_provider *provider, int32_t *out, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!out || !readable(provider, &engine, error)) return false;
    qa_clock_state clock;
    if (!qa_session_clock(provider->application->session, provider->owner, &clock) ||
        clock.frame.provider != provider->owner || clock.frame.kind != QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 clock is outside its installed source owner");
    *out = (int32_t)(uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    return true;
}

bool application_q3_guest_round_gamestate_read(application_provider *provider,
    const qa_q3_gamestate **out, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!out || !readable(provider, &engine, error)) return false;
    *out = &engine->gamestate;
    return true;
}

bool application_q3_guest_round_configstring_read(application_provider *provider,
    uint32_t index, const char **out, qa_error *error)
{
    const qa_q3_gamestate *state;
    if (!out || index >= QA_Q3_CONFIGSTRINGS ||
        !application_q3_guest_round_gamestate_read(provider, &state, error)) return false;
    *out = qa_q3_configstring(state, index);
    return true;
}

bool application_q3_guest_round_userinfo(application_provider *provider, uint32_t slot,
    const char **out, qa_error *error)
{
    if (!out || slot >= 64 || !application_q3_guest_round_ready(provider, error)) return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    const q3g_client *client = &engine->clients[slot];
    if (!client->connected)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 userinfo requires a connected source client");
    *out = client->userinfo ? client->userinfo : "";
    return true;
}

bool application_q3_guest_round_client_read(application_provider *provider, uint32_t slot,
    qa_actor_id *actor, qa_q3_usercmd *command, bool *bot, uint64_t *entered_ns, qa_error *error)
{
    if (!actor || !command || !bot || !entered_ns || slot >= 64)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client observation requires source slot and outputs");
    if (!application_q3_guest_round_ready(provider, error)) return false;
    const q3g_client *client = &q3g_engine(provider)->clients[slot];
    if (!client->connected)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 source client is not connected");
    *actor = client->actor; *command = client->command;
    *bot = client->bot; *entered_ns = client->entered_ns;
    return true;
}

bool application_q3_guest_round_configstring(application_provider *provider, uint32_t index,
    const char *text, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!text || !provider ||
        !owner_idle(provider, provider->application->operation == APPLICATION_CONFIGURING ?
            APPLICATION_CONFIGURING : APPLICATION_IDLE, &engine, error)) return false;
    if ((engine->round.phase != Q3G_ROUND_NONE && engine->round.phase != Q3G_ROUND_SETTLING) ||
        !engine->game->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 configstring requires a running GAME owner");
    ++engine->calls;
    bool ok = q3g_set_configstring(engine->game, index, text, error);
    --engine->calls;
    return !ok && engine->round.phase == Q3G_ROUND_SETTLING ? q3g_round_fail(engine, error, error) : ok;
}

bool application_q3_guest_round_begin(application_provider *provider, qa_error *error)
{
    if (!application_q3_guest_round_ready(provider, error)) return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    uint64_t carried = 0, roster_carried = 0;
    for (uint32_t slot = 0; slot < 64; ++slot) {
        if (engine->clients[slot].connected) carried |= UINT64_C(1) << slot;
        if (engine->clients[slot].roster_attached) roster_carried |= UINT64_C(1) << slot;
    }
    engine->local_snapshot_server_bit ^= 4u;
    engine->round = (q3g_round){.phase = Q3G_ROUND_RETIRING,
        .carried = carried, .roster_carried = roster_carried};
    return true;
}

bool application_q3_guest_round_shutdown(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!owner_idle(provider, APPLICATION_IDLE, &engine, error)) return false;
    if (engine->round.phase != Q3G_ROUND_RETIRING || engine->round.shutdown_completed ||
        !engine->game->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round shutdown requires its running source cut");
    if (!q3g_role_shutdown_source(engine->game, true, error)) return q3g_round_fail(engine, error, error);
    engine->round.shutdown_completed = true;
    return true;
}

bool application_q3_guest_round_reset(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!owner_idle(provider, APPLICATION_CONFIGURING, &engine, error)) return false;
    if (engine->round.phase != Q3G_ROUND_RETIRING || engine->game->initialized ||
        !engine->round.shutdown_completed ||
        qa_actors_count(qa_session_actors(provider->application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Retire all canonical actors before Q3 round reset");
    if (!qa_q3_host_round_ready(engine->game->host, error)) return false;
    for (size_t i = 0; i < 64; ++i)
        if (engine->clients[i].actor.registry || engine->clients[i].pending_retirement ||
            engine->clients[i].connected || engine->clients[i].roster_attached)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client bindings have not retired for round reset");
    qa_clock_state clock;
    if (!qa_session_clock(provider->application->session, provider->owner, &clock) ||
        clock.frame.provider != provider->owner || clock.frame.kind != QA_CLOCK_Q3 ||
        clock.frame.time_ns > UINT64_MAX - UINT64_C(400000000) || clock.frame_number > UINT64_MAX - 4)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round source clock cannot admit four settlement frames");
    engine->round.phase = Q3G_ROUND_RESETTING;
    engine->round.start_ns = clock.frame.time_ns;
    engine->round.last_frame = clock.frame_number;
    engine->milliseconds = (int32_t)(uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    q3g_role *game = engine->game;
    int32_t result;
    bool ok;
    if (game->projection) {
        while (game->projection->actors) {
            guest_projection_actor *context = game->projection->actors;
            if (!application_guest_projection_detach(game, context->actor, error))
                return q3g_round_fail(engine, error, error);
            game->projection->actors = context->next;
            free(context);
        }
    }
    if (!qa_q3_host_round_reset(game->host,
            (qa_bytes){(const uint8_t *)engine->entity_text, strlen(engine->entity_text)}, error))
        return q3g_round_fail(engine, error, error);
    if (game->vm && !qa_qvm_restart_original(game->vm, error))
        return q3g_round_fail(engine, error, error);
    if (game->native && !qa_native_restart_original(qa_native_host_instance(game->native), error))
        return q3g_round_fail(engine, error, error);
    qa_bytes primary = game->artifact->image ?
        (qa_bytes){game->artifact->primary.data, game->artifact->primary.size} :
        qa_native_declaration_primary(game->artifact->declaration);
    if (!application_guest_input_attach(game, primary, error)) return q3g_round_fail(engine, error, error);
    if (game->native) {
        game->init_succeeded = false;
        ++engine->calls;
        ok = qa_native_host_initialize(game->native, engine->milliseconds, engine->random_seed, true, error);
        --engine->calls;
        game->initialized = qa_native_get_lifecycle(qa_native_host_instance(game->native)) == QA_NATIVE_INITIALIZED;
        game->init_succeeded = ok && game->initialized;
    } else {
        int32_t arguments[] = {engine->milliseconds, engine->random_seed, 1};
        ok = q3g_round_call(game, 0, arguments, 3, &result, error);
    }
    if (!ok) return q3g_round_fail(engine, error, error);
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(game->host, &data) || !data.entities_address ||
        !data.clients_address || !data.entity_stride || !data.client_stride)
        return q3g_round_fail(engine, NULL, error);
    engine->round.phase = Q3G_ROUND_SETTLING;
    return true;
}

bool application_q3_guest_round_frame(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_source_frame frame;
    if (!engine || !provider->constructed || !provider->attached || engine->restore_pending ||
        provider->application->operation != APPLICATION_ADVANCING || engine->calls ||
        engine->draining_clients || !engine->game || !engine->game->initialized ||
        engine->round.phase != Q3G_ROUND_SETTLING || engine->round.completed_frames >= 4 ||
        !qa_world_idle(engine->world) || !application_q3_guest_idle(provider) ||
        !qa_session_active_frame(provider->application->session, provider->owner, &frame) ||
        frame.kind != QA_CLOCK_Q3 || frame.phase != QA_FRAME_ENTRY ||
        frame.elapsed_ns != UINT64_C(100000000) || frame.time_ns != frame.start_ns ||
        frame.start_ns != engine->round.start_ns + UINT64_C(100000000) * engine->round.completed_frames ||
        frame.number <= engine->round.last_frame ||
        (engine->round.completed_frames == 3 && engine->round.carried != engine->round.reconnected))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 settlement requires its actual admitted 100 ms source frame");
    engine->milliseconds = (int32_t)(uint32_t)(frame.time_ns / UINT64_C(1000000));
    engine->round.last_frame = frame.number;
    int32_t result;
    if (!q3g_round_call(engine->game, 8, &engine->milliseconds, 1, &result, error))
        return q3g_round_fail(engine, error, error);
    bool prior = engine->round.source_entry;
    engine->round.source_entry = true;
    bool completed = application_guest_bots_admit(provider, error) &&
        application_guest_clients_drain(provider, error);
    engine->round.source_entry = prior;
    if (!completed) return q3g_round_fail(engine, error, error);
    ++engine->round.completed_frames;
    return true;
}

bool application_q3_guest_round_reconnect(application_provider *provider, uint32_t slot,
    qa_actor_id actor, bool *accepted, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!accepted || slot >= 64 || !owner_publication(provider, &engine, error)) return false;
    uint64_t bit = UINT64_C(1) << slot;
    uint64_t pending = engine->round.carried & ~engine->round.reconnected;
    if (engine->round.phase != Q3G_ROUND_SETTLING || engine->round.completed_frames != 3 ||
        !(pending & bit) || (pending & (bit - 1)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round reconnect requires three frames and ordered carried slots");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(provider->application->session), actor);
    q3g_client *client = &engine->clients[slot];
    bool borrowed = record && record->owner != provider->owner;
    if (!record || (client->actor.registry &&
            (!client->reserved || !qa_actor_id_equal(client->actor, actor))) ||
        client->connected || client->pending_retirement ||
        (!borrowed && (!record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round actor differs from its carried source slot");
    if (!client->bot) {
        uint32_t seat;
        if (!qa_application_player_seat(provider->application, actor, &seat) || engine->seats[slot] != seat)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round human seat differs from its carried source slot");
    }
    if (!qa_q3_host_bind_actor(engine->game->host, slot, actor, borrowed, error)) return false;
    client->actor = actor;
    client->allocated = true;
    int32_t arguments[] = {(int32_t)slot, 0, client->bot ? 1 : 0}, result;
    bool admitted = false, ok;
    char *denial_text = NULL;
    if (engine->game->native) {
        qa_buffer denial = {0};
        ++engine->calls;
        ok = qa_native_host_q3_client_connect(engine->game->native, slot, false, client->bot, &denial, error);
        --engine->calls;
        admitted = ok && !denial.data;
        if (ok && denial.data) {
            denial_text = q3g_copy_text((const char *)denial.data, error);
            ok = denial_text != NULL;
        }
        qa_buffer_free(&denial);
    } else {
        ok = q3g_round_call(engine->game, 2, arguments, 3, &result, error);
        if (ok && result) {
            qa_bytes denial;
            ok = qa_qvm_read_string(engine->game->vm, result, &denial, error);
            if (ok) { denial_text = q3g_copy_text((const char *)denial.data, error); ok = denial_text != NULL; }
        }
        admitted = ok && !result;
    }
    if (!ok) { free(denial_text); return q3g_round_fail(engine, error, error); }
    client->reserved = false;
    if (denial_text) {
        free(client->retirement_reason);
        client->retirement_reason = denial_text;
    }
    admitted = admitted && !client->pending_retirement;
    client->connected = admitted;
    if (admitted) {
        int32_t argument = (int32_t)slot;
        if (!q3g_round_call(engine->game, 3, &argument, 1, &result, error))
            return q3g_round_fail(engine, error, error);
        if (!client->connected || client->pending_retirement ||
            !qa_actors_get(qa_session_actors(provider->application->session), actor)) admitted = false;
        else {
            client->begun = true;
            if (!application_guest_actor_admit(provider, actor, error))
                return q3g_round_fail(engine, error, error);
            if (engine->round.roster_carried & bit) {
                qa_combat_state state;
                qa_q3_player player;
                char name[QA_Q3_BIG_INFO_CHARS], skin[QA_Q3_BIG_INFO_CHARS];
                if (!application_guest_player_state(provider, actor, &state, error) ||
                    !qa_q3_host_source_player(engine->game->host, slot, &player, error) ||
                    !qa_q3_info_value(client->userinfo ? client->userinfo : "", "name", name, sizeof(name), error) ||
                    !qa_q3_info_value(client->userinfo ? client->userinfo : "", "model", skin, sizeof(skin), error))
                    return q3g_round_fail(engine, error, error);
                qa_builtin_player_info info = {.name = name, .skin = skin, .slot = slot,
                    .ping = player.ping, .entered_ns = client->entered_ns,
                    .view_height = (float)player.viewheight, .connected = true,
                    .spectator = player.pmType == 2, .dead = state.health <= 0};
                if (!application_players_guest_attach(provider->application, provider, slot, actor, &info, error))
                    return q3g_round_fail(engine, error, error);
                client->roster_attached = true;
            }
            if (!client->gamestate) {
                client->gamestate = malloc(sizeof(*client->gamestate));
                if (!client->gamestate) {
                    application_fail(error, QA_ERROR_MEMORY, "retaining connecting Q3 client gamestate");
                    return q3g_round_fail(engine, error, error);
                }
                *client->gamestate = engine->gamestate;
                client->gamestate->client_number = (int32_t)slot;
                client->gamestate->command_sequence = client->consumed_server_command;
            }
        }
    }
    if (!admitted) {
        /* This is the carried original connection, even when Connect rejects
         * its replacement actor before publishing connected=true. */
        if (!client->disconnect_started) {
            int32_t argument = (int32_t)slot;
            client->disconnect_started = true;
            client->disconnect_pending = false;
            if (!q3g_round_call(engine->game, 5, &argument, 1, &result, error))
                return q3g_round_fail(engine, error, error);
        }
        if (!application_guest_client_drop(provider, slot,
                client->retirement_reason ? client->retirement_reason : "", error))
            return q3g_round_fail(engine, error, error);
    }
    engine->round.reconnected |= bit;
    *accepted = admitted;
    return true;
}

bool application_q3_guest_round_queue_client(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_q3_guest *engine;
    if (slot >= 64 || !owner_publication(provider, &engine, error)) return false;
    uint64_t bit = UINT64_C(1) << slot;
    if (engine->round.phase != Q3G_ROUND_SETTLING || engine->round.completed_frames != 3 ||
        !(engine->round.carried & bit) || ((engine->round.queued | engine->round.reconnected) & bit) ||
        engine->clients[slot].pending_retirement)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restart command requires its waiting carried source client");
    if (!qa_q3_reliable_add(&engine->clients[slot].reliable, QA_Q3_SERVER, "map_restart\n", error))
        return q3g_round_fail(engine, error, error);
    engine->round.queued |= bit;
    return true;
}

bool application_q3_guest_round_denial_read(application_provider *provider, uint32_t slot,
    const char **out, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!out || slot >= 64 || !owner_publication(provider, &engine, error)) return false;
    uint64_t bit = UINT64_C(1) << slot;
    if (engine->round.phase != Q3G_ROUND_SETTLING || !(engine->round.reconnected & bit) ||
        engine->clients[slot].connected)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 denial requires its completed carried client rejection");
    *out = engine->clients[slot].retirement_reason ? engine->clients[slot].retirement_reason : "";
    return true;
}

bool application_q3_guest_round_finish(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine;
    if (!owner_publication(provider, &engine, error)) return false;
    if (engine->round.phase != Q3G_ROUND_SETTLING || engine->round.completed_frames != 4 ||
        engine->round.carried != engine->round.reconnected)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires four frames and completed carried clients");
    engine->round = (q3g_round){0};
    return true;
}
