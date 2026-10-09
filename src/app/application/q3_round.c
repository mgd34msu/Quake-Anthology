#include "q3_round.h"
#include "qa/modes_q3_session.h"
#include "bots_round.h"
#include "guest_q3_restart.h"
#include "guest_projection_private.h"
#include "match_intents.h"
#include "native_q3_console.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "native_q3_settings.h"
#include "native_q3_ipfilters.h"
#include "native_maps.h"
#include "native_q3_clients.h"
#include "native_q3_session.h"
#include "portals.h"
#include "rankings.h"
#include "control_frame.h"
#include "qa/application_players.h"

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>

#define ROUND_FRAME_NS UINT64_C(100000000)

typedef struct application_round {
    qa_application *application;
    application_provider *provider;
    const qa_application_q3_round_services *services;
    qa_application_q3_round_cut *frontend;
    application_q3_round_players *players;
    application_bots_round *bots;
    qa_entities entities;
    qa_world *world;
    qa_collision_geometry *geometry;
    qa_resource *map;
    const qa_launch_snapshot *launch;
    uint64_t map_revision, configuration_generation, outer_frame, epoch_modification;
    float previous_epoch;
    uint32_t next_epoch;
    bool mutated, network_owned, epoch_present;
} application_round;

static bool source_epoch_prepare(application_round *, qa_error *);

static bool same_mode(qa_mode_id left, qa_mode_id right)
{ return left.slot == right.slot && left.generation == right.generation; }

static bool boundary(qa_application *app, bool owned_round, qa_error *error)
{
    if (!app || app->operation != APPLICATION_IDLE || app->q3_round_active != owned_round || !app->session ||
        !qa_session_safe(app->session) || !qa_session_destroy_ready(app->session) ||
        qa_session_faulted(app->session) || !app->world || !qa_world_idle(app->world) ||
        !qa_combat_idle(app->combat) || !qa_console_idle(app->console) ||
        !application_guests_idle(app) || !application_bots_can_destroy(app) ||
        !application_control_frames_idle(app) || !application_rankings_idle(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        app->publication_started || app->destroy_requested || app->finalizing ||
        app->pending_close || app->routing_snapshot || app->routing_providers ||
        app->routing_provider_count ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round requires completed source and service owners");
    return true;
}

static qa_cvars *source_cvars(application_provider *provider)
{
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return application_native_q3_console_registry(provider);
    qa_q3_host *host = provider->kind == APPLICATION_PROVIDER_QVM
        ? provider->state.qvm.host : provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q3_host : NULL;
    qa_cvars *cvars = NULL;
    if (host) (void)qa_q3_host_console(host, &cvars, NULL);
    return cvars;
}

bool qa_application_q3_round_callback_ready(qa_application *app,
    qa_actor_owner source_owner, qa_error *error)
{
    application_provider *provider = app
        ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!app || app->operation != APPLICATION_IDLE || !provider ||
        provider->owner != source_owner || !provider->constructed || !provider->attached ||
        provider->close_pending || provider->component.clock.kind != QA_RULESET_Q3 ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !app->world || !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
        !application_guests_idle(app) || !qa_console_idle(app->console) ||
        app->publication_started || app->destroy_requested || app->finalizing ||
        app->pending_close || app->routing_snapshot || app->routing_providers ||
        app->routing_provider_count ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 round frontend callback requires its actual completed source boundary");
    return true;
}

static bool source_compatible(qa_application *app,
    application_provider *provider, qa_mode_id mode, bool *out,
    bool owned_round, qa_error *error)
{
    if (!out || !boundary(app, owned_round, error)) return false;
    *out = false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(app));
    if (!choices || !provider || provider->application != app || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q3 ||
        provider->component.clock.kind != QA_RULESET_Q3 ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        provider != application_native_q3_mode_source_provider(app, mode) ||
        !app->primary_mode_ready || !same_mode(mode, app->primary_mode))
        return true;
    if (choices->monster_count || app->mode_count != 1) return true;
    for (size_t i = 0; i < choices->behavior_count; ++i)
        if (choices->behaviors[i].enabled) return true;
    for (size_t i = 0; i < choices->equipment_count; ++i)
        if (choices->equipment[i].selection.grapple != QA_GRAPPLE_DISABLED ||
            choices->equipment[i].selection.grenades.enabled) return true;
    for (size_t i = 0; i < app->mode_count; ++i)
        if (application_native_q3_mode_source_provider(app, app->mode_ids[i]) != provider) return true;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, provider->owner, &clock) || clock.paused ||
        clock.frame.kind != QA_RULESET_Q3 ||
        (clock.frame_number && clock.frame.phase != QA_FRAME_EXIT) ||
        clock.frame_number > UINT64_MAX - 4 ||
        clock.elapsed_ns > UINT64_MAX - 4 * ROUND_FRAME_NS ||
        clock.frame.time_ns > UINT64_MAX - 4 * ROUND_FRAME_NS ||
        qa_session_elapsed(app->session) > UINT64_MAX - 4 * ROUND_FRAME_NS)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round source clock is unavailable or exhausted");
    qa_cvars *cvars = source_cvars(provider);
    const qa_cvar_view *game_type = qa_cvars_find(cvars, "g_gametype");
    const qa_cvar_view *clients = qa_cvars_find(cvars, "sv_maxclients");
    if (!game_type || !clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round has no actual source compatibility cvars");
    if (provider->kind == APPLICATION_PROVIDER_Q3 &&
        (game_type->owner != provider->owner || clients->owner != provider->owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 compatibility settings belong to another source");
    if (game_type->modified || clients->modified) return true;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source source;
        if (!qa_q3_round_read(provider->state.q3, &source, error) ||
            !application_native_q3_wire_round_ready(provider, error)) return false;
        if (game_type->number != (float)source.game_type ||
            clients->number != (float)source.max_clients) return true;
    } else {
        qa_error qualification = {0};
        if (!application_q3_guest_round_ready(provider, &qualification)) {
            if (qualification.code == QA_ERROR_UNSUPPORTED) return true;
            if (error) *error = qualification;
            return false;
        }
    }
    uint32_t cursor = 0;
    const qa_actor_record *actor;
    static const qa_launch_role roles[] = {QA_ROLE_CHARACTER, QA_ROLE_MOVEMENT,
        QA_ROLE_ARSENAL, QA_ROLE_INVENTORY, QA_ROLE_COMBAT, QA_ROLE_EFFECTS,
        QA_ROLE_EQUIPMENT};
    while (qa_actors_next(qa_session_actors(app->session), &cursor, &actor)) {
        if (actor->owner != provider->owner) return true;
        uint32_t seat;
        if (!qa_application_player_seat(app, actor->id, &seat)) continue;
        for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
            application_provider *selected = application_provider_for(app, actor->id, roles[i], "");
            if (selected && selected != provider) return true;
        }
    }
    const qa_application_q3_round_services *services = app->q3_round_services;
    if (!services || !services->prepare || !services->deliver || !services->begin ||
        !services->network_owned ||
        !services->bind || !services->queue_client || !services->admit_client ||
        !services->reject_client || !services->finish || !services->dispose)
        return true;
    *out = true;
    return true;
}

bool application_q3_round_compatible(qa_application *app,
    application_provider *provider, qa_mode_id mode, bool *out, qa_error *error)
{
    return source_compatible(app, provider, mode, out, false, error);
}

static void mark_mutated(void *opaque)
{
    application_round *cut = opaque;
    if (cut->mutated) return;
    cut->mutated = true;
    application_match_intents_restart_mutated(cut->application->match_intents, cut->application);
}

static bool retained(application_round *cut, qa_error *error)
{
    qa_application *app = cut->application;
    if (app->world != cut->world || app->geometry != cut->geometry ||
        app->map_resource != cut->map || app->map_revision != cut->map_revision ||
        qa_application_launch(app) != cut->launch ||
        qa_application_configuration_generation(app) != cut->configuration_generation ||
        application_frame_revision(app) != cut->outer_frame ||
        cut->provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        app->state == QA_APPLICATION_FAULTED || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round lost a retained world or source owner");
    return true;
}

static bool capture(application_round *cut, qa_error *error)
{
    if (!application_q3_round_players_prepare(cut->application, cut->provider,
            &cut->players, error)) return false;
    const qa_application_q3_round_client *clients;
    size_t count = application_q3_round_players_clients(cut->players, &clients);
    return cut->services->prepare(cut->application->guest_context, cut->application,
        cut->provider->owner, clients, count, &cut->frontend, error) &&
        cut->services->network_owned(cut->frontend, &cut->network_owned, error) &&
        source_epoch_prepare(cut, error);
}

static bool source_epoch_prepare(application_round *cut, qa_error *error)
{
    if (cut->network_owned) return true;
    application_provider *provider = cut->provider;
    qa_cvars *cvars = source_cvars(provider);
    const qa_cvar_view *epoch = qa_cvars_find(cvars, "sv_serverid");
    double value = epoch ? (double)epoch->number : 0;
    if (!cvars || !isfinite(value) || value < 0 || value >= INT32_MAX || trunc(value) != value)
        return application_fail(error, QA_ERROR_ARGUMENT, "local Q3 source epoch is unavailable or exhausted");
    cut->epoch_present = epoch != NULL;
    cut->previous_epoch = epoch ? epoch->number : 0;
    cut->epoch_modification = epoch ? epoch->modification_count : 0;
    cut->next_epoch = (uint32_t)value + 1u;
    return true;
}

static bool source_epoch(application_round *cut, qa_error *error)
{
    if (cut->network_owned) return true;
    qa_cvars *cvars = source_cvars(cut->provider);
    const qa_cvar_view *epoch = qa_cvars_find(cvars, "sv_serverid");
    if ((epoch != NULL) != cut->epoch_present ||
        (epoch && (epoch->number != cut->previous_epoch ||
                   epoch->modification_count != cut->epoch_modification)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source epoch changed during the prepared frontend cut");
    char text[32];
    snprintf(text, sizeof(text), "%u", cut->next_epoch);
    return qa_cvars_set(cvars, "sv_serverid", text, true, error);
}

static bool deliver(application_round *cut, qa_error *error)
{
    return cut->services->deliver(cut->frontend, error) && retained(cut, error);
}

bool qa_application_q3_source_client_slot(qa_application *app, qa_actor_owner owner,
    qa_actor_id actor, uint32_t *out, qa_error *error)
{
    if (!app || !out || !app->session || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 source client has no live canonical generation");
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner) { provider = app->providers[i]; break; }
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 source client has no admitted GAME owner");
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_native_client_slot(provider->state.q3, actor, out, error);
    if (application_q3_guest_actor_client(provider, actor, out)) return true;
    return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 GAME has no physical binding for this canonical generation");
}

static bool guest_frame(void *opaque, qa_session *session,
    const qa_source_frame *frame, qa_error *error)
{
    application_provider *provider = opaque;
    if (session != provider->application->session || frame->provider != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round frame changed its admitted owner");
    return application_q3_guest_round_frame(provider, error);
}

static bool settle(application_round *cut, qa_error *error)
{
    qa_application *app = cut->application;
    app->operation = APPLICATION_ADVANCING;
    bool native = cut->provider->kind == APPLICATION_PROVIDER_Q3;
    bool okay = qa_session_round_step(app->session, cut->provider->owner,
        ROUND_FRAME_NS, native ? NULL : guest_frame, cut->provider, error);
    if (okay && native) {
        int32_t source_time;
        qa_clock_state clock;
        okay = qa_q3_source_clock(cut->provider->state.q3, &source_time, error) &&
            qa_session_clock(app->session, cut->provider->owner, &clock);
        if (okay && (clock.frame.phase != QA_FRAME_EXIT ||
            (uint32_t)(clock.frame.time_ns / UINT64_C(1000000)) != (uint32_t)source_time))
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                "Q3 settlement mode frame differs from its actual source entry");
    }
    app->operation = APPLICATION_IDLE;
    return okay && deliver(cut, error);
}

bool application_q3_round_restart(qa_application *app, application_provider *provider,
    qa_mode_id mode, bool *mutated, qa_error *error)
{
    if (!mutated) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires a mutation result");
    *mutated = false;
    bool compatible;
    if (!application_q3_round_compatible(app, provider, mode, &compatible, error)) return false;
    if (!compatible)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Q3 source composition requires full world replacement");
    application_round cut = {.application = app, .provider = provider,
        .services = app->q3_round_services, .world = app->world,
        .geometry = app->geometry, .map = app->map_resource,
        .launch = qa_application_launch(app), .map_revision = app->map_revision,
        .configuration_generation = qa_application_configuration_generation(app),
        .outer_frame = application_frame_revision(app)};
    app->q3_round_active = true;
    bool okay = capture(&cut, error) && deliver(&cut, error);
    if (okay) {
        /* Event delivery can change connected clients and their transport
         * histories. Capture the actual inventory again at the final cut. */
        cut.services->dispose(cut.frontend); cut.frontend = NULL;
        application_q3_round_players_dispose(cut.players); cut.players = NULL;
        okay = source_compatible(app, provider, mode, &compatible, true, error);
        if (okay && !compatible)
            okay = application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "Q3 source composition requires full world replacement");
        if (okay) okay = retained(&cut, error) && capture(&cut, error);
    }
    const qa_application_q3_round_client *clients = NULL;
    size_t count = application_q3_round_players_clients(cut.players, &clients);
    bool native = provider->kind == APPLICATION_PROVIDER_Q3;
    if (okay) okay = application_q3_round_map_prepare(app, provider, &cut.entities, error) &&
        application_bots_round_prepare(app, provider, clients, count, &cut.bots, error);
    if (okay && native)
        okay = application_native_q3_session_capture_carry(provider, error);
    if (okay) okay = cut.services->begin(cut.frontend, mark_mutated, &cut, error);
    if (okay) okay = source_epoch(&cut, error);
    if (okay && !native) {
        mark_mutated(&cut);
        okay = application_q3_guest_round_begin(provider, error) &&
            application_q3_guest_round_shutdown(provider, error);
    }
    if (okay) {
        mark_mutated(&cut);
        okay = application_rankings_round_close(app, provider, error) &&
            application_bots_round_begin(cut.bots, error);
    }
    if (okay && native)
        okay = application_native_q3_wire_round_begin(provider, error);
    if (okay) {
        app->operation = APPLICATION_CONFIGURING;
        okay = qa_session_retire_actors(app->session, error);
        if (okay) {
            app->physics->world_actor = (qa_actor_id){0};
            app->shader_remap_count = 0;
            if (native) {
                qa_clock_state clock = {0};
                int32_t warmup = 0, effective_restarted = 0;
                okay = application_native_q3_settings_reset_cache(provider, __DATE__, error) &&
                    application_native_q3_settings_source_modes(provider, error) &&
                    application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_DO_WARMUP, &warmup, error) &&
                    application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_RESTARTED, &effective_restarted, error);
                if (okay && !qa_session_clock(app->session, provider->owner, &clock))
                    okay = application_fail(error, QA_ERROR_ARGUMENT, "Q3 reset lost its source settings or clock");
                bool effective_warmup = warmup != 0;
                int32_t time = (int32_t)(uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
                for (size_t i = 0; okay && i < app->mode_count; ++i)
                    okay = qa_modes_q3_round_reset(app->modes, app->mode_ids[i], time,
                        clock.frame.time_ns, effective_restarted, error);
                if (okay) okay = application_portals_close(app, provider->owner, error) &&
                    qa_q3_round_reset(provider->state.q3, time, effective_warmup,
                        effective_restarted, error) &&
                    application_native_q3_settings_source_init(provider, error) &&
                    application_native_q3_ipfilters_init(provider, error) &&
                    application_q3_round_map_spawn(provider, &cut.entities, error);
            } else okay = application_q3_guest_round_reset(provider, error);
        }
        if (okay) okay = application_q3_round_players_publish(app, provider, cut.players, error);
        app->operation = APPLICATION_IDLE;
    }
    if (okay) okay = retained(&cut, error) && application_bots_round_bind(cut.bots, error) &&
        cut.services->bind(cut.frontend, error) && deliver(&cut, error);
    for (size_t frame = 0; okay && frame < 3; ++frame) okay = settle(&cut, error);
    for (size_t i = 0; okay && i < count; ++i) {
        okay = cut.services->queue_client(cut.frontend, &clients[i], error);
        if (okay && native && !clients[i].remote)
            okay = application_native_q3_send_command(provider,
                (int32_t)clients[i].source_slot, "map_restart\n", error);
        qa_actor_id actor = {0};
        bool accepted = false;
        const char *reason = NULL;
        app->operation = APPLICATION_CONFIGURING;
        if (okay && !native && !clients[i].remote)
            okay = application_q3_guest_round_queue_client(provider, clients[i].source_slot, error);
        if (okay) okay = application_q3_round_player_admit(app, provider,
            cut.players, i, &actor, &accepted, &reason, error);
        app->operation = APPLICATION_IDLE;
        if (okay && accepted)
            okay = application_bots_round_reconnect(cut.bots, &clients[i], actor, error) &&
                cut.services->admit_client(cut.frontend, &clients[i], actor, error);
        else if (okay) {
            okay = (native ? reason != NULL : application_q3_guest_round_denial_read(provider,
                clients[i].source_slot, &reason, error)) &&
                application_bots_round_reject(cut.bots, &clients[i], error) &&
                cut.services->reject_client(cut.frontend, &clients[i], reason, error);
            qa_error first = error ? *error : (qa_error){0};
            qa_error cleanup = {0};
            app->operation = APPLICATION_CONFIGURING;
            bool drained = native || application_guest_clients_drain(provider, &cleanup);
            app->operation = APPLICATION_IDLE;
            if (!drained && okay) {
                okay = false;
                if (error) *error = cleanup;
            } else if (!okay && error) *error = first;
        }
        if (okay) okay = deliver(&cut, error);
    }
    if (okay) okay = application_q3_round_players_finish(app, provider, cut.players, error) &&
        settle(&cut, error);
    if (okay && !native) okay = application_q3_guest_round_finish(provider, error);
    if (okay) okay = application_bots_round_resume(cut.bots, error) &&
        application_rankings_frame(app, error) &&
        application_native_q3_clients_drain(app, error);
    if (okay && native) okay = application_native_q3_wire_round_finish(provider, error);
    if (okay) okay =
        application_q3_publish_local_snapshots(app, error) &&
        deliver(&cut, error) && cut.services->finish(cut.frontend, error) &&
        retained(&cut, error);
    app->operation = APPLICATION_IDLE;
    *mutated = cut.mutated;
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 round lost a qualified source phase");
    if (!okay && cut.mutated) application_fault(app, error);
    application_bots_round_dispose(cut.bots);
    if (cut.frontend) cut.services->dispose(cut.frontend);
    application_q3_round_players_dispose(cut.players);
    qa_entities_free(&cut.entities);
    app->q3_round_active = false;
    return okay;
}
