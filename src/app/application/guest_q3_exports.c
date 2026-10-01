#include "guest_q3_private.h"
#include "guest_q3_client_console.h"
#include "guest_q3_console.h"
#include "guest_q3_factory.h"
#include "guest_native_q2_private.h"
#include "guest_projection_private.h"
#include "guest_qc_internal.h"
#include "native_q3_console.h"
#include "native_q3_remote_role.h"
#include "native_q1_console.h"
#include "native_q2_console.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "qa/application_q3_client.h"
#include "qa/game_q3_source.h"

bool application_guest_frontend_rebind_ready(application_provider *provider,
    const qa_scene_frame *current_frame, void *current_context, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    if (engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 frontend exchange requires idle source roles");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (!qa_q3_host_frontend_rebind_ready(role->host, current_frame,
                                             current_context, error))
            return false;
    return true;
}

void application_guest_frontend_rebind(application_provider *provider,
    qa_scene_frame *destination_frame, void *current_context, void *destination_context)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return;
    for (q3g_role *role = engine->roles; role; role = role->next)
        qa_q3_host_frontend_rebind(role->host, destination_frame,
                                  current_context, destination_context);
}

bool application_guest_console_at(application_provider *provider, size_t index,
                                    qa_console **console, qa_cvars **cvars,
                                    qa_command_context *context)
{
    if (!provider || !provider->constructed || !console) return false;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return !index && application_native_q1_console_at(provider, console, cvars, context);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return !index && application_native_q2_console_at(provider, console, cvars, context);
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_console *game = NULL;
        if (application_native_q3_console_at(provider, &game, NULL, NULL)) {
            if (!index) return application_native_q3_console_at(provider, console, cvars, context);
            --index;
        }
        qa_application_startup_source source; bool found;
        if (!application_native_q3_remote_role_source_at(provider, index, &source, &found, NULL) || !found ||
            !application_native_q3_remote_role_configuration(provider, source.scope.seat, &source, NULL)) return false;
        *console = source.console;
        if (cvars) *cvars = source.cvars;
        if (context) *context = source.command;
        return true;
    }
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (index || !engine || !engine->console) return false;
        *console = engine->console;
        if (cvars) *cvars = engine->cvars;
        if (context) *context = engine->command_context;
        return true;
    }
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (index || !engine->console) return false;
        *console = engine->console;
        if (cvars) *cvars = engine->cvars;
        if (context) *context = engine->command_context;
        return true;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return false;
    qa_console *game = application_guest_q3_console_owner(provider);
    if (game) {
        if (!index) {
            if (engine->game && engine->game->host)
                *console = qa_q3_host_console(engine->game->host, cvars, context);
            else {
                *console = game;
                if (cvars) *cvars = application_guest_q3_console_registry(provider);
                if (context) *context = (qa_command_context){.owner = provider->owner,
                    .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER};
            }
            return true;
        }
        --index;
    }
    qa_application_startup_source source;
    if (application_guest_q3_client_console_source(engine, index, &source) &&
        application_guest_q3_client_console_at(engine, source.scope.seat, console, cvars)) {
        if (context) *context = source.command;
        for (q3g_role *role = engine->roles; role; role = role->next)
            if (qa_q3_host_console(role->host, NULL, NULL) == *console) {
                *console = qa_q3_host_console(role->host, cvars, context);
                break;
            }
        return true;
    }
    return false;
}

bool application_guest_console_scope(application_provider *provider,
    const qa_console *console, qa_application_console_scope *out)
{
    if (!provider || !provider->constructed || !console || !out)
        return false;
    qa_application_console_scope scope = {.provider = provider->owner};
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_console *source = NULL;
        if (!application_native_q1_console_at(provider, &source, NULL, NULL) || source != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_Q1_GAME;
    } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_console *source = NULL;
        if (!application_native_q2_console_at(provider, &source, NULL, NULL) || source != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_Q2_GAME;
    } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_console *source = NULL;
        if (application_native_q3_console_at(provider, &source, NULL, NULL) && source == console)
            scope.kind = QA_APPLICATION_CONSOLE_Q3_GAME;
        else {
            qa_application_startup_source client;
            bool matched = false;
            for (size_t index = 0;; ++index) {
                bool found;
                if (!application_native_q3_remote_role_source_at(provider, index, &client, &found, NULL) || !found) break;
                if (client.console == console && application_native_q3_remote_role_configuration(provider,
                    client.scope.seat, &client, NULL)) { scope = client.scope; matched = true; break; }
            }
            if (!matched) return false;
        }
    } else if (provider->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = provider->state.qc.engine;
        if (!engine || engine->console != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_QC;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine) {
        if (provider->state.native.q2_engine->console != console)
            return false;
        scope.kind = QA_APPLICATION_CONSOLE_NATIVE_Q2;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        if (!engine) return false;
        if (application_guest_q3_console_owner(provider) == console)
            scope.kind = QA_APPLICATION_CONSOLE_Q3_GAME;
        else {
            qa_application_startup_source client;
            bool matched = false;
            for (size_t index = 0; application_guest_q3_client_console_source(engine, index, &client); ++index)
                if (client.console == console && application_guest_q3_client_console_at(engine,
                    client.scope.seat, NULL, NULL)) { scope = client.scope; matched = true; break; }
            if (!matched) return false;
        }
    }
    *out = scope;
    return true;
}

bool application_q3_guest_role_loading(const application_provider *provider,
    qa_qvm_role kind, uint32_t seat)
{
    struct application_q3_guest *engine = q3g_engine((application_provider *)provider);
    if (engine) for (const q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && role->seat == seat && role->ready && !role->retired)
            return role->source_cleared;
    return false;
}

static q3g_role *find_role(application_provider *provider, qa_qvm_role kind,
                            uint32_t seat, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (engine) for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && (kind == QA_QVM_GAME || role->seat == seat)) return role;
    application_fail(error, QA_ERROR_NOT_FOUND, "Q3 guest role is not installed for this seat");
    return NULL;
}

static bool client_context_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, bool read_clock, qa_application_q3_client_context *out, qa_error *error)
{
    if (!app || !out || !receiver || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client context requires its live receiver owner");
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    application_provider *provider = NULL;
    for (size_t i = 0; i < count; ++i)
        if (providers[i] && providers[i]->owner == receiver) {
            if (provider)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client receiver has ambiguous source ownership");
            provider = providers[i];
        }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!provider || provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending || !engine || engine->restore_pending || engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 client receiver is not admitted");
    q3g_role *role = NULL;
    for (q3g_role *current = engine->roles; current; current = current->next)
        if (current->kind == QA_QVM_CGAME && current->seat == seat && current->ready &&
            !current->retired && current->host) {
            if (role)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client seat has ambiguous CGAME hosts");
            role = current;
        }
    if (!role || !role->local_client || role->client >= 64)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 client context has no actual local GAME source");
    qa_q3_host_client_context host;
    if (!qa_q3_host_client_context_read(role->host, &host) || host.role != QA_QVM_CGAME ||
        host.session != app->session || host.owner != receiver || host.service_owner != role->service_owner ||
        host.command_context.owner != receiver || host.command_context.seat != seat ||
        host.command_context.dialect != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client context differs from its actual CGAME host");
    qa_application_q3_client_context view = {.session = host.session, .receiver = receiver,
        .seat = seat, .source_client = role->client, .service_owner = host.service_owner,
        .frontend_lifetime = host.frontend_lifetime, .console = host.console, .cvars = host.cvars,
        .client_time_cvars = host.client_time_cvars, .client_time_owner = host.client_time_owner,
        .command_context = host.command_context, .native_source = role->native_client != NULL,
        .initialized = role->init_succeeded};
    if (role->native_client) {
        application_native_q3_wire_client_topology topology;
        if (!application_native_q3_wire_client_topology_read(role->native_client, &topology, error)) return false;
        if (topology.source != role->client_source || topology.source_owner != role->source_owner ||
            topology.receiver != receiver || topology.seat != seat || topology.source_slot != role->client ||
            topology.source->application != app || !topology.source->constructed ||
            !topology.source->attached || topology.source->close_pending)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client context lost its actual native GAME lease");
        view.source_owner = topology.source_owner;
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(topology.source->state.q3, topology.source_slot, &binding, error)) return false;
        view.source_actor = binding.actor;
        view.source_cvars = application_native_q3_console_registry(topology.source);
        if (!view.source_cvars)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 client context lacks its native GAME registry");
        if (read_clock && !application_native_q3_wire_client_time(role->native_client, &view.source_milliseconds, error)) return false;
    } else {
        struct application_q3_guest *source = role->client_engine;
        q3g_role *game = source ? source->game : NULL;
        q3g_client *client = source ? &source->clients[role->client] : NULL;
        uint32_t source_slot;
        if (!source || role->client_source != source->provider || role->source_owner != source->provider->owner || !source->map_ready ||
            !game || !game->initialized || game->retired || !game->host || !client->allocated ||
            !client->begun || client->bot || client->pending_retirement || !client->actor.registry ||
            !qa_actors_get(qa_session_actors(app->session), client->actor) ||
            !qa_q3_host_actor_slot(game->host, client->actor, &source_slot, error) || source_slot != role->client)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 client context lost its original GAME client binding");
        view.source_owner = source->provider->owner;
        view.source_actor = client->actor;
        if (!qa_q3_host_console(game->host, &view.source_cvars, NULL) || !view.source_cvars)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 client context lacks its original GAME registry");
        if (read_clock && !application_q3_wire_time(source->provider, &view.source_milliseconds, error)) return false;
    }
    if (!role->client_services.gamestate || !role->client_services.gamestate(role->client_services.context))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 client context requires its source gamestate after Begin");
    if (view.client_time_cvars && (view.client_time_cvars != view.source_cvars ||
        view.client_time_owner != view.source_owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 local client timing authority differs from its actual GAME source");
    if (read_clock) {
        qa_clock_state clock;
        if (!qa_session_clock(app->session, view.source_owner, &clock) ||
            clock.frame.provider != view.source_owner || clock.frame.kind != QA_CLOCK_Q3 ||
            (clock.frame.number && clock.frame.phase != QA_FRAME_EXIT))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client context lost its actual GAME clock");
        view.source_frame = clock.frame;
    }
    *out = view;
    return true;
}

bool qa_application_q3_client_context_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *out, qa_error *error)
{
    return client_context_read(app, receiver, seat, true, out, error);
}

bool qa_application_q3_client_context_current(qa_application *app,
    const qa_application_q3_client_context *retained)
{
    qa_application_q3_client_context actual;
    return retained && client_context_read(app, retained->receiver, retained->seat, false, &actual, NULL) &&
        actual.session == retained->session && actual.source_owner == retained->source_owner &&
        actual.source_client == retained->source_client && qa_actor_id_equal(actual.source_actor, retained->source_actor) &&
        actual.service_owner == retained->service_owner && actual.frontend_lifetime == retained->frontend_lifetime &&
        actual.console == retained->console && actual.cvars == retained->cvars && actual.source_cvars == retained->source_cvars &&
        actual.client_time_cvars == retained->client_time_cvars && actual.client_time_owner == retained->client_time_owner &&
        actual.native_source == retained->native_source;
}

bool qa_application_q3_client_retire(qa_application *app,
    const qa_application_q3_client_context *retained, qa_error *error)
{
    if (!app || !retained || app->operation != APPLICATION_IDLE ||
        app->frame_preparing || app->q3_round_active || app->q3_world_restart ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) || !qa_application_q3_client_context_current(app, retained))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 client retirement requires its exact receiver after source callbacks unwind");
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    application_provider *provider = NULL;
    for (size_t i = 0; i < count; ++i)
        if (providers[i] && providers[i]->owner == retained->receiver) provider = providers[i];
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || engine->draining_clients || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client retirement has an active source owner");
    q3g_role **position = &engine->roles;
    while (*position && ((*position)->kind != QA_QVM_CGAME || (*position)->seat != retained->seat))
        position = &(*position)->next;
    q3g_role *role = *position;
    if (!role || !role->initialized || role->retired || role->service_owner != retained->service_owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client retirement lost its initialized CGAME role");
    q3g_role *next = role->next;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = q3g_role_shutdown(role, false, error);
    if (ok) ok = q3g_role_destroy(role, error);
    if (ok) *position = next;
    app->operation = APPLICATION_IDLE;
    return ok;
}

static bool remote_context_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, bool published, qa_application_q3_client_context *out, qa_error *error)
{
    if (!app || !out || !receiver || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context requires its live actual-seat receiver");
    const qa_launch_snapshot *snapshot = !published && app->routing_snapshot ?
        app->routing_snapshot : qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!choices || choices->seat_count != 1 || choices->seats[0].id != seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context leaves its single actual launch seat");
    application_provider **providers = !published && app->routing_providers ? app->routing_providers : app->providers;
    size_t count = !published && app->routing_providers ? app->routing_provider_count : app->provider_count;
    application_provider *provider = NULL;
    for (size_t i = 0; i < count; ++i)
        if (providers[i] && providers[i]->owner == receiver) {
            if (provider)
                return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context has ambiguous receiver ownership");
            provider = providers[i];
        }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!provider || provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending ||
        !q3g_selected_client_seat(provider, choices, QA_QVM_CGAME, 0))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Remote Q3 context has no admitted selected HUD receiver");
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return application_native_q3_remote_role_context(provider, seat, out, error);
    if (!engine || engine->restore_pending || engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Remote Q3 context has no admitted original source owner");
    q3g_role *role = NULL;
    for (q3g_role *current = engine->roles; current; current = current->next)
        if (current->kind == QA_QVM_CGAME && current->seat == seat && current->ready &&
            !current->retired && current->host) {
            if (role)
                return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context has ambiguous CGAME hosts");
            role = current;
        }
    qa_q3_host_client_context host;
    if (!role || role->local_client || role->native_client || !role->client_services.gamestate ||
        !qa_q3_host_client_context_read(role->host, &host) || host.role != QA_QVM_CGAME ||
        host.session != app->session || host.owner != receiver || host.service_owner != role->service_owner ||
        host.command_context.owner != receiver || host.command_context.seat != seat ||
        host.command_context.dialect != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 context differs from its actual external-service host");
    *out = (qa_application_q3_client_context){.session = host.session, .receiver = receiver,
        .seat = seat, .source_client = UINT32_MAX, .service_owner = host.service_owner,
        .frontend_lifetime = host.frontend_lifetime, .console = host.console, .cvars = host.cvars,
        .client_time_cvars = host.client_time_cvars, .client_time_owner = host.client_time_owner,
        .command_context = host.command_context, .initialized = role->init_succeeded};
    return true;
}

bool qa_application_q3_remote_context_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *out, qa_error *error)
{ return remote_context_read(app, receiver, seat, false, out, error); }

bool qa_application_q3_remote_published_context_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *out, qa_error *error)
{ return remote_context_read(app, receiver, seat, true, out, error); }

static bool remote_context_current(qa_application *app,
    const qa_application_q3_client_context *retained, bool published)
{
    qa_application_q3_client_context actual;
    if (!retained || !remote_context_read(app, retained->receiver,
        retained->seat, published, &actual, NULL)) return false;
    application_provider **providers = !published && app->routing_providers ? app->routing_providers : app->providers;
    size_t count = !published && app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t index = 0; index < count; ++index)
        if (providers[index] && providers[index]->owner == retained->receiver &&
            providers[index]->kind == APPLICATION_PROVIDER_Q3)
            return application_native_q3_remote_role_current(providers[index], retained);
    return actual.session == retained->session &&
        actual.service_owner == retained->service_owner && actual.frontend_lifetime == retained->frontend_lifetime &&
        actual.console == retained->console && actual.cvars == retained->cvars &&
        actual.client_time_cvars == retained->client_time_cvars && actual.client_time_owner == retained->client_time_owner &&
        actual.command_context.owner == retained->command_context.owner &&
        actual.command_context.seat == retained->command_context.seat &&
        actual.command_context.dialect == retained->command_context.dialect;
}

bool qa_application_q3_remote_context_current(qa_application *app,
    const qa_application_q3_client_context *retained)
{ return remote_context_current(app, retained, false); }

bool qa_application_q3_remote_published_context_current(qa_application *app,
    const qa_application_q3_client_context *retained)
{ return remote_context_current(app, retained, true); }

bool application_q3_guest_role_add(application_provider *provider, qa_qvm_role kind,
                                     uint32_t seat, const char *path, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || engine->round.phase != Q3G_ROUND_NONE ||
        kind == QA_QVM_GAME || kind > QA_QVM_UI || !path)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 companion creation requires an idle owner and client role");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && role->seat == seat)
            return (role->ready && !role->retired && !strcmp(role->path, path)) || application_fail(error, QA_ERROR_ARGUMENT, "Q3 role seat already has another artifact");
    q3g_role *role = NULL;
    if (!q3g_role_create(engine, kind, seat, path, false, &role, error)) return false;
    role->next = engine->roles; engine->roles = role; return true;
}

bool application_q3_guest_role_initialize(application_provider *provider, qa_qvm_role kind,
                                            uint32_t seat, int32_t server_message,
                                            int32_t server_command, int32_t client_number,
                                            bool connecting, qa_error *error)
{
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    if (kind == QA_QVM_GAME || role->engine->calls || role->initialized ||
        (role->source_cleared && role->engine->initializing_role != role) ||
        role->engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client initialization requires an idle fresh role");
    if (role->native_client && (!role->client_services.gamestate ||
        !role->client_services.gamestate(role->client_services.context)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client initialization requires its admitted actual native GAME client");
    if (!q3g_role_activate(role, error)) return false;
    int32_t result;
    if (kind == QA_QVM_UI) {
        if (role->vm) {
            ++role->engine->calls;
            bool ok = qa_qvm_validate_ui(role->vm, &result, error);
            --role->engine->calls;
            if (!ok) return false;
        } else if (!q3g_call(role, 0, NULL, 0, &result, error)) return false;
        if (result != 4 && !(role->abi == QA_QVM_Q3_MODERN && result == 6))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 UI returned an unsupported API version");
        int32_t argument = connecting ? 1 : 0;
        if (!q3g_call(role, 1, &argument, 1, &result, error)) return false;
    } else {
        if (server_message < 0 || client_number < 0 || client_number >= 64 ||
            !role->client_services.gamestate || !role->client_services.current_snapshot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame requires admitted client and gamestate services");
        int32_t message, time;
        ++role->engine->calls;
        bool ready = role->client_services.current_snapshot(role->client_services.context, &message, &time, error);
        const qa_q3_gamestate *state = ready ? role->client_services.gamestate(role->client_services.context) : NULL;
        bool matches = state && (!role->local_client ||
            (message == server_message && server_command == state->command_sequence)) &&
            client_number == state->client_number;
        --role->engine->calls;
        if (!ready) return false;
        if (!matches)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame initialization differs from the active client gamestate");
        if (!role->native_client && role->client < 64 &&
            (!role->client_engine || !role->client_engine->clients[role->client].connected))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 local cgame has no connected source client");
        if (role->client < 64 && role->client != (uint32_t)client_number)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 cgame source client ordinal differs from its seat");
        if (role->local_client && !q3g_client_effect(role, QA_APPLICATION_Q3_SYSTEM_INFO,
                qa_q3_configstring(state, 1), error)) return false;
        q3g_role *ui = NULL;
        if (role->local_client && !application_guest_q3_source_ui_received(role, state, &ui, error)) return false;
        for (q3g_role *candidate = role->engine->roles; candidate; candidate = candidate->next)
            if (candidate->kind == QA_QVM_UI && candidate->seat == seat && candidate->ready && !candidate->retired) {
                if (ui && ui != candidate) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous actual CGAME source UI");
                ui = candidate;
            }
        if (!ui || ui->descriptor->storage != role->descriptor->storage)
            return application_fail(error, QA_ERROR_ARGUMENT, "CGAME initialization lost its actual source UI helper");
        if (!ui->initialized && !application_q3_guest_role_initialize(provider, QA_QVM_UI, seat,
                server_message, server_command, client_number, true, error)) return false;
        if (!ui->init_succeeded)
            return application_fail(error, QA_ERROR_ARGUMENT, "CGAME initialization requires successful source UI Init");
        int32_t arguments[] = {server_message, server_command, client_number};
        q3g_role *previous = role->engine->initializing_role;
        role->engine->initializing_role = role;
        bool initialized = q3g_call(role, 0, arguments, 3, &result, error);
        role->engine->initializing_role = previous;
        if (!initialized) return false;
    }
    role->initialized = true; return true;
}

bool application_q3_guest_role_call(application_provider *provider, qa_qvm_role kind,
                                      uint32_t seat, int32_t command, const int32_t *arguments,
                                      size_t count, int32_t *result, qa_error *error)
{
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    int32_t first = kind == QA_QVM_UI ? 3 : kind == QA_QVM_CGAME ? 2 : 8;
    int32_t last = kind == QA_QVM_UI ? 10 : kind == QA_QVM_CGAME ? 8 : 10;
    if (!role->initialized || role->engine->round.phase != Q3G_ROUND_NONE || command < first || command > last)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role export is unavailable in its current lifecycle/profile");
    return q3g_call(role, command, arguments, count, result, error);
}

bool application_q3_guest_console_command(application_provider *provider, const char *text,
                                            bool *handled, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !engine->game->initialized ||
        engine->round.phase != Q3G_ROUND_NONE || !text || !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 console command requires initialized game");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
    qa_command_tokens prior = engine->arguments; engine->arguments = next;
    int32_t result;
    bool ok = q3g_call(engine->game, 9, NULL, 0, &result, error);
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = result != 0;
    if (ok) ok = application_guest_bots_admit(provider, error);
    if (ok) ok = application_guest_clients_drain(provider, error);
    return ok;
}

bool application_q3_guest_role_command(application_provider *provider, qa_qvm_role kind,
                                         uint32_t seat, int32_t time, const char *text,
                                         bool *handled, qa_error *error)
{
    if (kind == QA_QVM_GAME) return application_q3_guest_console_command(provider, text, handled, error);
    q3g_role *role = find_role(provider, kind, seat, error);
    if (!role) return false;
    if (!text || !handled || !role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 console export requires initialized client role");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
    qa_command_tokens prior = role->arguments; role->arguments = next;
    bool prior_scope = role->arguments_scoped; role->arguments_scoped = true;
    int32_t result;
    bool ok = q3g_call(role, kind == QA_QVM_UI ? 8 : 2,
                       kind == QA_QVM_UI ? &time : NULL, kind == QA_QVM_UI ? 1 : 0,
                       &result, error);
    qa_command_tokens_free(&role->arguments); role->arguments = prior;
    role->arguments_scoped = prior_scope;
    if (ok) *handled = result != 0;
    return ok;
}
