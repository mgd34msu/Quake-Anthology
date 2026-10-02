#include "q3_world_restart.h"
#include "control_frame.h"
#include "guest_q3_restart.h"
#include "map_players_private.h"
#include "match_intents.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_wire_state.h"
#include "native_q3_session.h"
#include "native_q3_settings.h"
#include "rankings.h"
#include "bots_round.h"
#include "startup_flow.h"
#include "startup_program.h"
#include "qa/game_q3_round.h"
#include "qa/game_q3_source.h"
#include "qa/cvars_save.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct restart_cvar {
    char *name, *value, *reset, *latched, *description;
    uint32_t flags;
} restart_cvar;
typedef struct restart_client {
    qa_actor_id actor;
    qa_q3_usercmd command;
    char *userinfo;
    uint32_t slot;
} restart_client;

struct application_q3_world_restart {
    qa_application *application;
    application_provider *source;
    const qa_launch_snapshot *launch;
    const qa_launch_snapshot *candidate;
    application_publication *publication;
    application_native_q3_wire_carry *wire;
    application_bots_original *bots;
    qa_buffer guest_cvars;
    qa_buffer native_cvars;
    uint64_t guest_cvar_owner;
    application_q3_world_startup startup;
    restart_cvar *cvars;
    size_t cvar_count;
    restart_client clients[64];
    size_t client_count;
    uint32_t retained_capacity;
    uint64_t configuration_generation, map_revision, actor_revision, outer_frame;
    bool native, prepared, mutated, admitted, published, guest_handed_off, native_handed_off;
    bool configuration_finished;
};

static char *copy_text(const char *text, qa_error *error)
{
    if (!text) return NULL;
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "Q3 restart text is exhausted");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        application_fail(error, QA_ERROR_MEMORY, "cannot retain Q3 replacement source text");
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static bool capture_cvars(application_q3_world_restart_state *, qa_error *);

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

static bool same_mode(qa_mode_id left, qa_mode_id right)
{ return left.slot == right.slot && left.generation == right.generation; }

static bool boundary(qa_application *app, qa_error *error)
{
    if (!app || app->operation != APPLICATION_IDLE || app->q3_round_active ||
        app->q3_world_restart || !app->session || !qa_session_safe(app->session) ||
        !qa_session_destroy_ready(app->session) || qa_session_faulted(app->session) ||
        !app->world || !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
        !qa_console_idle(app->console) || !application_guests_idle(app) ||
        !application_bots_can_destroy(app) || !application_control_frames_idle(app) ||
        !application_rankings_idle(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        app->publication_started || app->pending_close || app->routing_snapshot ||
        app->routing_providers || app->routing_provider_count ||
        app->destroy_requested || app->finalizing ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 world replacement requires its completed live authorities");
    return true;
}

static bool retained(application_q3_world_restart_state *state, qa_error *error)
{
    qa_application *app = state->application;
    bool committing = app->publication_started && state->candidate &&
        qa_application_launch(app) == state->candidate;
    if ((!committing && qa_application_launch(app) != state->launch) ||
        qa_application_configuration_generation(app) != state->configuration_generation + (committing ? 1u : 0u) ||
        app->map_revision != state->map_revision ||
        application_frame_revision(app) != state->outer_frame ||
        qa_actors_revision(qa_session_actors(app->session)) != state->actor_revision ||
        state->source != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        !state->source->constructed || !state->source->attached ||
        state->source->close_pending || app->destroy_requested ||
        app->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 replacement source changed during candidate preparation");
    return true;
}

static bool same_program(const application_q3_world_restart_state *state,
    const application_provider *provider)
{
    const qa_launch_instance *old = state->source->launch;
    const qa_launch_instance *next = provider ? provider->launch : NULL;
    if (!old || !next || provider == state->source || provider->application != state->application ||
        strcmp(old->selection.instance, next->selection.instance) ||
        old->selection.runtime != next->selection.runtime ||
        old->selection.product != next->selection.product ||
        strcmp(old->selection.implementation, next->selection.implementation) ||
        strcmp(old->selection.artifact, next->selection.artifact) ||
        strcmp(old->selection.component, next->selection.component) ||
        old->selection.options.size != next->selection.options.size ||
        (old->selection.options.size && memcmp(old->selection.options.data,
            next->selection.options.data, old->selection.options.size)) ||
        (old->artifact != NULL) != (next->artifact != NULL) ||
        (old->declaration != NULL) != (next->declaration != NULL)) return false;
    return (!old->artifact || qa_sha256_equal(qa_resource_digest(old->artifact),
        qa_resource_digest(next->artifact))) &&
        (!old->declaration || qa_sha256_equal(qa_resource_digest(old->declaration),
        qa_resource_digest(next->declaration)));
}

bool application_q3_world_restart_active(const qa_application *app)
{ return app && app->q3_world_restart; }

bool application_q3_world_restart_guest_shutdown(const qa_application *app,
    const application_provider *provider)
{
    const application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    return state && app->operation == APPLICATION_CONFIGURING &&
        state->mutated && state->source == provider && !state->native &&
        provider->constructed && provider->attached && !provider->close_pending;
}

bool application_q3_world_restart_source(const qa_application *app,
    const application_provider *provider, application_q3_world_startup *out)
{
    const application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state || !out || app->operation != APPLICATION_CONFIGURING ||
        !same_program(state, provider)) return false;
    *out = state->startup;
    return true;
}

bool application_q3_world_restart_cvars(qa_application *app,
    application_provider *provider, qa_cvars *cvars, qa_error *error)
{
    application_q3_world_startup startup;
    if (!application_q3_world_restart_source(app, provider, &startup)) return true;
    application_q3_world_restart_state *state = app->q3_world_restart;
    /* Original GAME service construction can share the installed registry.
     * The actual old Shutdown owns its final carry, before new GAME Init. */
    if (!state->native) return true;
    if (!cvars || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3 || provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 startup cvars require their fresh candidate registry");
    for (size_t i = 0; i < state->cvar_count; ++i) {
        const restart_cvar *saved = &state->cvars[i];
        if (!strcmp(saved->name, "mapname") || !strcmp(saved->name, "sv_mapname")) continue;
        if (!qa_cvars_register(cvars, saved->name, saved->reset, saved->flags,
                provider->owner, saved->description, error) ||
            !qa_cvars_set(cvars, saved->name,
                saved->latched ? saved->latched : saved->value, true, error)) return false;
    }
    char text[32];
    snprintf(text, sizeof(text), "%u", startup.max_clients);
    if (!qa_cvars_set(cvars, "sv_maxclients", text, true, error)) return false;
    snprintf(text, sizeof(text), "%d", startup.game_type);
    return qa_cvars_set(cvars, "g_gametype", text, true, error);
}

bool application_q3_world_restart_client(const qa_application *app, qa_actor_id actor,
    qa_q3_usercmd *command, const char **userinfo, qa_error *error)
{
    const application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state || !command || !userinfo ||
        app->operation != APPLICATION_CONFIGURING)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 replacement client requires its actual captured source transaction");
    for (size_t i = 0; i < state->client_count; ++i) {
        const restart_client *client = &state->clients[i];
        if (!qa_actor_id_equal(actor, client->actor)) continue;
        *command = client->command;
        *userinfo = client->userinfo;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND,
        "Q3 replacement has no captured source client for this actor generation");
}

static void dispose(application_q3_world_restart_state *state)
{
    application_bots_original_dispose(state->bots);
    for (size_t i = 0; i < state->cvar_count; ++i) {
        restart_cvar *cvar = &state->cvars[i];
        free(cvar->name); free(cvar->value); free(cvar->reset);
        free(cvar->latched); free(cvar->description);
    }
    free(state->cvars);
    application_native_q3_wire_carry_dispose(state->wire);
    qa_buffer_free(&state->guest_cvars);
    qa_buffer_free(&state->native_cvars);
    for (size_t i = 0; i < state->client_count; ++i) free(state->clients[i].userinfo);
    qa_launch_snapshot_release(state->launch);
    qa_launch_snapshot_release(state->candidate);
}

void application_q3_world_restart_configuration_finish(qa_application *app,
    const qa_launch_snapshot *candidate, bool published)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state || !candidate || state->candidate != candidate || state->configuration_finished)
        return;
    state->configuration_finished = true;
    application_startup_publication_finish(app, candidate, published);
}

bool application_q3_world_restart_guest_handoff(qa_application *app,
    application_provider *provider, qa_cvars *cvars, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!application_q3_world_restart_guest_shutdown(app, provider) ||
        !state->prepared || state->guest_handed_off || !state->publication ||
        !state->publication->players || cvars != source_cvars(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 handoff has no actual old GAME shutdown");
    if (!application_bots_original_validate_source(state->bots, provider, cvars, error) ||
        !application_q3_guest_cvar_owner(provider, &state->guest_cvar_owner, error) ||
        !qa_cvars_save_capture(cvars, &state->guest_cvars, error)) return false;
    application_player_travel *travel = state->publication->players;
    state->retained_capacity = 0;
    for (size_t i = 0; i < travel->count; ++i) {
        application_player_carry *carry = &travel->carry[i];
        application_player_record *record = &travel->roster->records[i];
        if (!carry->q3_client) continue;
        qa_actor_id actor;
        qa_q3_usercmd command;
        bool bot;
        uint64_t entered_ns;
        const char *userinfo;
        qa_error current = {0};
        if (!application_q3_guest_handoff_client_read(provider, record->client_slot,
                &actor, &command, &bot, &entered_ns, &userinfo, &current)) {
            if (current.code == QA_ERROR_NOT_FOUND) {
                record->retiring = true;
                continue;
            }
            if (error) *error = current;
            return false;
        }
        if (!qa_actor_id_equal(actor, carry->q3_previous_actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 shutdown changed a retained physical client generation");
        char *text = copy_text(userinfo, error);
        if (!text) return false;
        free(record->userinfo);
        record->userinfo = text;
        record->bot = travel->seats[i].bot = bot;
        if (state->retained_capacity <= record->client_slot)
            state->retained_capacity = record->client_slot + 1;
        (void)entered_ns;
    }
    for (uint32_t slot = 0; slot < 64; ++slot) {
        qa_actor_id actor;
        qa_q3_usercmd command;
        bool bot;
        uint64_t entered_ns;
        const char *userinfo;
        qa_error current = {0};
        if (!application_q3_guest_handoff_client_read(provider, slot, &actor, &command,
                &bot, &entered_ns, &userinfo, &current)) {
            if (current.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = current;
            return false;
        }
        bool retained = false;
        for (size_t i = 0; i < travel->count; ++i)
            retained |= !travel->roster->records[i].retiring &&
                travel->roster->records[i].client_slot == slot &&
                qa_actor_id_equal(travel->carry[i].q3_previous_actor, actor);
        if (!retained)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Q3 Shutdown admitted a new client outside its prepared canonical roster");
    }
    for (size_t i = 0; i < state->cvar_count; ++i) {
        restart_cvar *saved = &state->cvars[i];
        free(saved->name); free(saved->value); free(saved->reset);
        free(saved->latched); free(saved->description);
    }
    free(state->cvars);
    state->cvars = NULL;
    state->cvar_count = 0;
    uint32_t capacity = state->startup.max_clients;
    if (!capture_cvars(state, error)) return false;
    state->startup.max_clients = capacity;
    static const qa_mode_kind kinds[] = {QA_MODE_FFA, QA_MODE_DUEL,
        QA_MODE_SINGLE_PLAYER, QA_MODE_TEAM_DEATHMATCH, QA_MODE_CTF,
        QA_MODE_ONE_FLAG, QA_MODE_OVERLOAD, QA_MODE_HARVESTER};
    const qa_launch_choices *choices = qa_launch_snapshot_choices(state->candidate);
    for (size_t i = 0; i < state->publication->mode_count; ++i) {
        if (strcmp(choices->modes[i].instance, provider->launch->selection.instance)) continue;
        qa_mode_view mode;
        if (!qa_modes_read(state->publication->modes, state->publication->mode_ids[i], &mode, error)) return false;
        mode.rules.kind = kinds[state->startup.game_type];
        if (!qa_modes_configure(state->publication->modes, state->publication->mode_ids[i], &mode.rules, error)) return false;
    }
    state->guest_handed_off = true;
    return true;
}

static bool import_guest_handoff(application_q3_world_restart_state *state,
    application_provider *provider, qa_error *error)
{
    if (!state->guest_handed_off)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement did not capture its actual Shutdown continuation");
    qa_cvars *cvars = source_cvars(provider);
    uint64_t owner;
    qa_cvars_restore *ticket = NULL;
    if (!cvars || !application_q3_guest_cvar_owner(provider, &owner, error) ||
        !qa_cvars_save_prepare(cvars, (qa_bytes){state->guest_cvars.data, state->guest_cvars.size},
            &ticket, error)) return false;
    if (!qa_cvars_save_commit(ticket, error)) {
        qa_cvars_save_abort(ticket);
        return false;
    }
    size_t count = qa_cvars_count(cvars);
    if (count > SIZE_MAX / sizeof(qa_cvar_record_state))
        return application_fail(error, QA_ERROR_MEMORY, "Q3 carried cvar metadata is exhausted");
    qa_cvar_record_state *rows = count ? calloc(count, sizeof(*rows)) : NULL;
    if (count && !rows)
        return application_fail(error, QA_ERROR_MEMORY, "cannot qualify Q3 carried cvar ownership");
    qa_cvar_registry_state registry;
    bool okay = qa_cvars_capture_metadata(cvars, &registry, rows, count, error);
    for (size_t i = 0; okay && i < count; ++i) {
        if (rows[i].owner == state->source->owner) rows[i].owner = provider->owner;
        else if (rows[i].owner == state->guest_cvar_owner) rows[i].owner = owner;
    }
    if (okay) okay = qa_cvars_restore_metadata(cvars, &registry, rows, count, error);
    free(rows);
    for (size_t i = 0; okay && i < count; ++i) {
        const qa_cvar_view *view = qa_cvars_at(cvars, i);
        if (view->latched_value) okay = qa_cvars_apply_latched(cvars, view->name, error);
    }
    char text[32];
    snprintf(text, sizeof(text), "%u", state->startup.max_clients);
    if (okay) okay = qa_cvars_set(cvars, "sv_maxclients", text, true, error);
    const char *map = qa_strings_cstr(qa_session_strings(state->application->session),
        state->application->current_map);
    if (okay) okay = map && qa_cvars_set(cvars, "mapname", map, true, error) &&
        qa_cvars_set(cvars, "sv_mapname", map, true, error);
    return okay && application_bots_original_validate_source(state->bots, provider, cvars, error);
}

static bool capture_cvars(application_q3_world_restart_state *state, qa_error *error)
{
    qa_cvars *source = source_cvars(state->source);
    if (!source || qa_cvars_dialect(source) != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement source has no actual cvar registry");
    size_t count = qa_cvars_count(source);
    if (count > SIZE_MAX / sizeof(*state->cvars))
        return application_fail(error, QA_ERROR_MEMORY, "Q3 replacement cvar inventory is exhausted");
    state->cvars = count ? calloc(count, sizeof(*state->cvars)) : NULL;
    if (count && !state->cvars)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain Q3 replacement source cvars");
    qa_cvar_options options = {.dialect = QA_CONSOLE_Q3};
    qa_cvars *resolved = qa_cvars_create(&options, error);
    if (!resolved) return false;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        const qa_cvar_view *view = qa_cvars_at(source, i);
        if (!view || !view->name || !view->value || !view->reset_value ||
            (state->native && view->owner != state->source->owner && view->owner != 0)) {
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                "Q3 replacement cvar belongs to another source owner");
            break;
        }
        restart_cvar *saved = &state->cvars[state->cvar_count++];
        saved->flags = view->flags;
        saved->name = copy_text(view->name, error);
        saved->value = copy_text(view->value, error);
        saved->reset = copy_text(view->reset_value, error);
        saved->latched = copy_text(view->latched_value, error);
        saved->description = copy_text(view->description ? view->description : "", error);
        okay = saved->name && saved->value && saved->reset && saved->description &&
            (!view->latched_value || saved->latched) &&
            qa_cvars_register(resolved, saved->name,
                saved->latched ? saved->latched : saved->value, 0, 0, NULL, error);
    }
    const qa_cvar_view *capacity = okay ? qa_cvars_find(resolved, "sv_maxclients") : NULL;
    const qa_cvar_view *game_type = okay ? qa_cvars_find(resolved, "g_gametype") : NULL;
    const qa_cvar_view *warmup = okay ? qa_cvars_find(resolved, "g_doWarmup") : NULL;
    const qa_cvar_view *restarted = okay ? qa_cvars_find(resolved, "g_restarted") : NULL;
    const qa_cvar_view *dedicated = okay ? qa_cvars_find(resolved, "dedicated") : NULL;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(state->launch);
    if (okay && (!capacity || !game_type || !warmup || !restarted ||
                 !dedicated || !choices || choices->seat_count > 64))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement has no complete source startup settings");
    if (okay) {
        float number = truncf(capacity->number);
        float minimum = dedicated->number != 0 ? 1.0f
            : (float)(choices->seat_count ? choices->seat_count : 1);
        if (minimum < (float)state->retained_capacity)
            minimum = (float)state->retained_capacity;
        if (number < minimum) number = minimum;
        if (number > 64) number = 64;
        if (!isfinite(number) || number < 1 || number > 64)
            okay = application_fail(error, QA_ERROR_FORMAT,
                "Q3 replacement client capacity must be between 1 and 64");
        else {
            state->startup.max_clients = (uint32_t)number;
            state->startup.game_type = game_type->integer;
            state->startup.warmup = warmup->integer != 0;
            state->startup.restarted = restarted->integer;
            if (state->startup.game_type < 0 || state->startup.game_type > 7)
                state->startup.game_type = 0;
        }
    }
    qa_cvars_destroy(resolved);
    return okay;
}

static bool capture_capacity(application_q3_world_restart_state *state, qa_error *error)
{
    uint32_t maximum = 64;
    if (state->native && !qa_q3_source_max_clients(state->source->state.q3,
            &maximum, error)) return false;
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        if (state->native) {
            application_native_q3_wire_client_view client;
            bool present;
            if (!application_native_q3_wire_client_read(state->source, slot,
                    &client, &present, error)) return false;
            if (present) state->retained_capacity = slot + 1;
        } else {
            qa_actor_id actor;
            qa_q3_usercmd command;
            bool bot;
            uint64_t entered;
            qa_error absent = {0};
            if (application_q3_guest_world_client_read(state->source, slot,
                    &actor, &command, &bot, &entered, &absent))
                state->retained_capacity = slot + 1;
            else if (absent.code != QA_ERROR_NOT_FOUND) {
                if (error) *error = absent;
                return false;
            }
        }
    }
    return true;
}

static bool capture_clients(application_q3_world_restart_state *state,
    qa_error *error)
{
    qa_application *app = state->application;
    if (!app->players || app->players->map_provider != state->source)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 replacement has no actual source client roster");
    application_player_record *ordered[64] = {0};
    for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record *record = &app->players->records[i];
        if (record->retiring || !qa_actors_get(qa_session_actors(app->session), record->actor)) continue;
        if (record->client_slot >= 64 || ordered[record->client_slot])
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement client slots collide");
        ordered[record->client_slot] = record;
    }
    for (uint32_t slot = 0; slot < 64; ++slot) {
        application_player_record *record = ordered[slot];
        if (!record) continue;
        if (slot >= state->startup.max_clients)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q3 replacement capacity excludes a retained source client slot");
        restart_client *client = &state->clients[state->client_count++];
        client->actor = record->actor;
        client->slot = slot;
        const char *userinfo = record->userinfo;
        if (state->native) {
            uint32_t native_slot;
            application_native_q3_wire_client_view wire_client;
            bool present;
            if (!qa_q3_native_client_slot(state->source->state.q3, record->actor, &native_slot, error) ||
                native_slot != slot ||
                !application_native_q3_wire_client_read(state->source, slot,
                    &wire_client, &present, error)) return false;
            if (!present || !qa_actor_id_equal(wire_client.actor, record->actor) ||
                wire_client.bot != record->bot)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q3 replacement source has another wire client admission");
            userinfo = wire_client.userinfo;
            client->command = wire_client.command;
        } else {
            qa_actor_id actor; bool bot; uint64_t entered;
            if (!application_q3_guest_world_client_read(state->source, slot, &actor,
                    &client->command, &bot, &entered, error) ||
                !qa_actor_id_equal(actor, record->actor) || bot != record->bot ||
                !application_q3_guest_world_userinfo(state->source, slot, &userinfo, error)) return false;
        }
        if (!userinfo)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement client has no genuine source userinfo");
        client->userinfo = copy_text(userinfo, error);
        if (!client->userinfo) return false;
    }
    if (state->native) {
        uint32_t maximum;
        if (!qa_q3_source_max_clients(state->source->state.q3, &maximum, error)) return false;
        for (uint32_t slot = 0; slot < maximum; ++slot) {
            application_native_q3_wire_client_view client;
            bool present;
            if (!application_native_q3_wire_client_read(state->source, slot,
                    &client, &present, error)) return false;
            if (present != (ordered[slot] != NULL) ||
                (present && !qa_actor_id_equal(client.actor, ordered[slot]->actor)))
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q3 replacement wire inventory differs from its canonical roster");
        }
    } else
        for (uint32_t slot = 0; slot < 64; ++slot) {
            qa_actor_id actor; qa_q3_usercmd command; bool bot; uint64_t entered;
            qa_error absent = {0};
            bool present = application_q3_guest_world_client_read(state->source, slot,
                &actor, &command, &bot, &entered, &absent);
            if (!present && absent.code == QA_ERROR_NOT_FOUND && !ordered[slot]) continue;
            if (!present || !ordered[slot] || !qa_actor_id_equal(actor, ordered[slot]->actor)) {
                if (!present && error) *error = absent;
                return present ? application_fail(error, QA_ERROR_ARGUMENT,
                    "Q3 source client inventory differs from its retained roster") : false;
            }
        }
    return true;
}

static bool make_draft(application_q3_world_restart_state *state,
    qa_mode_id mode, qa_launch_draft **out, qa_error *error)
{
    if (!qa_launch_snapshot_draft_copy(state->launch, out, error)) return false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(state->launch);
    static const qa_mode_kind kinds[] = {QA_MODE_FFA, QA_MODE_DUEL,
        QA_MODE_SINGLE_PLAYER, QA_MODE_TEAM_DEATHMATCH, QA_MODE_CTF,
        QA_MODE_ONE_FLAG, QA_MODE_OVERLOAD, QA_MODE_HARVESTER};
    size_t index = 0;
    while (index < state->application->mode_count &&
        !same_mode(state->application->mode_ids[index], mode)) ++index;
    if (index >= choices->mode_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement lost its selected source mode");
    qa_launch_mode selection = choices->modes[index];
    selection.rules.kind = kinds[state->startup.game_type];
    if (!qa_launch_set_mode(*out, &selection, error)) return false;
    qa_launch_provider provider = state->source->launch->selection;
    provider.clock.initial_time_ns = state->startup.initial_time_ns;
    provider.clock.initial_lead_ns = 0;
    return qa_launch_set_provider(*out, &provider, error);
}

bool application_q3_world_restart_prepared(qa_application *app,
    application_publication *publication, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state) return true;
    if (state->prepared || !publication || !publication->travel ||
        publication->previous != state->launch || !same_program(state, publication->map_provider) ||
        !publication->map_resource || !app->map_resource ||
        !qa_sha256_equal(qa_resource_digest(publication->map_resource), qa_resource_digest(app->map_resource)) ||
        publication->next_count != app->provider_count ||
        publication->removed_count != app->provider_count || !retained(state, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 replacement publication differs from its prepared source world");
    for (size_t i = 0; i < publication->next_count; ++i)
        for (size_t j = 0; j < app->provider_count; ++j)
            if (publication->next[i] == app->providers[j])
                return application_fail(error, QA_ERROR_ARGUMENT, "Q3 full replacement retained a gameplay instance");
    if (state->native &&
        !application_native_q3_wire_carry_import(publication->map_provider, state->wire, error))
        return false;
    state->publication = publication;
    state->candidate = publication->candidate;
    if (!state->native &&
        !application_bots_original_prepare(app, publication, &state->bots, error)) return false;
    state->prepared = true;
    return true;
}

bool application_q3_world_restart_begin(qa_application *app,
    application_publication *publication, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state) return true;
    if (!state->prepared || state->publication != publication || state->mutated ||
        !retained(state, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement retirement lost its prepared ownership");
    state->mutated = true;
    application_match_intents_restart_mutated(app->match_intents, app);
    return true;
}

static bool reconcile_native_clients(application_provider *source,
    application_player_travel *travel, qa_error *error)
{
    uint32_t maximum;
    if (!qa_q3_source_max_clients(source->state.q3, &maximum, error)) return false;
    for (size_t index = 0; index < travel->count; ++index) {
        if (!travel->carry[index].q3_client) continue;
        uint32_t slot = travel->roster->records[index].client_slot;
        if (slot >= maximum)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "native Q3 prepared client exceeds its actual old source capacity");
        for (size_t previous = 0; previous < index; ++previous)
            if (travel->carry[previous].q3_client &&
                travel->roster->records[previous].client_slot == slot)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "native Q3 prepared carry repeats a physical source client");
    }
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        application_native_q3_wire_client_view client;
        bool present;
        if (!application_native_q3_wire_client_read(source, slot, &client, &present, error)) return false;
        size_t index = 0;
        while (index < travel->count && (!travel->carry[index].q3_client ||
            travel->roster->records[index].client_slot != slot)) ++index;
        if (index == travel->count) {
            if (!present) continue;
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "native Q3 shutdown admitted a client outside its prepared canonical roster");
        }
        application_player_record *record = &travel->roster->records[index];
        if (!present) {
            record->retiring = true;
            continue;
        }
        if (record->retiring || !qa_actor_id_equal(client.actor, travel->carry[index].q3_previous_actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 shutdown changed a retained physical client generation");
        char *userinfo = copy_text(client.userinfo, error);
        if (!userinfo) return false;
        free(record->userinfo);
        record->userinfo = userinfo;
        record->bot = travel->seats[index].bot = client.bot;
        travel->carry[index].q3_command = client.command;
    }
    return true;
}

bool application_q3_map_shutdown(qa_application *app,
    application_publication *publication, bool *cut, qa_error *error)
{
    if (!cut || *cut || !app || !publication)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 map cut requires its empty publication result");
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (app->q3_world_restart || !app->world || !source ||
        source->kind != APPLICATION_PROVIDER_Q3 || source != publication->map_provider)
        return true;
    application_player_travel *travel = publication->players;
    bool retained_source = false;
    for (size_t i = 0; i < publication->next_count; ++i)
        retained_source |= publication->next[i] == source;
    for (size_t i = 0; i < publication->removed_count; ++i)
        if (publication->removed[i] == source) retained_source = false;
    if (!retained_source || !publication->travel ||
        app->operation != APPLICATION_CONFIGURING || !app->publication_started ||
        !source->state.q3 || !source->constructed || !source->attached ||
        source->close_pending || !travel || !travel->roster ||
        (travel->count && (!travel->carry || !travel->seats)) ||
        !qa_session_safe(app->session) || !qa_world_idle(app->world))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 ordinary map lost its retained source publication");
    if (!reconcile_native_clients(source, travel, error) ||
        !application_native_q3_session_capture_carry(source, error) ||
        !application_native_q3_wire_map_begin(source, error)) return false;
    *cut = true;
    return true;
}

bool application_q3_map_published(qa_application *app,
    application_publication *publication, bool cut, qa_error *error)
{
    if (!cut) return true;
    application_provider *source = publication ? publication->map_provider : NULL;
    if (!app || !source || app->q3_world_restart ||
        app->operation != APPLICATION_CONFIGURING || !app->publication_started ||
        !publication->published || qa_application_launch(app) != publication->candidate ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        !app->players || app->players->map_provider != source)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 ordinary map did not publish its retained source");
    return application_native_q3_wire_map_finish(source, error);
}

bool application_q3_world_restart_shutdown(qa_application *app,
    application_publication *publication, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state || !state->native) return true;
    application_provider *source = state->source;
    application_player_travel *travel = publication ? publication->players : NULL;
    if (!state->prepared || !state->mutated || state->native_handed_off ||
        state->publication != publication || app->operation != APPLICATION_CONFIGURING ||
        !app->publication_started || !travel || !travel->roster ||
        (travel->count && (!travel->carry || !travel->seats)) ||
        !source->constructed || !source->attached || source->close_pending ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 carry has no actual old-source shutdown cut");
    if (!reconcile_native_clients(source, travel, error) ||
        !application_native_q3_session_capture_carry(source, error)) return false;
    qa_cvars *cvars = source_cvars(source);
    if (!cvars || !qa_cvars_save_capture(cvars, &state->native_cvars, error)) return false;
    application_native_q3_wire_carry *final = NULL;
    if (!application_native_q3_wire_carry_capture(source, &final, error)) return false;
    if (!application_native_q3_wire_carry_refresh(publication->map_provider, final, error)) {
        application_native_q3_wire_carry_dispose(final);
        return false;
    }
    application_native_q3_wire_carry_dispose(state->wire);
    state->wire = final;
    state->native_handed_off = true;
    return true;
}

static bool import_native_handoff(application_q3_world_restart_state *state,
    application_provider *provider, qa_error *error)
{
    qa_cvars *cvars = source_cvars(provider);
    const qa_cvar_view *map = qa_cvars_find(cvars, "mapname");
    const qa_cvar_view *server_map = qa_cvars_find(cvars, "sv_mapname");
    if (!state->native_handed_off || !cvars || provider->owner != state->source->owner ||
        application_native_q3_settings_initialized(provider) || !map || !server_map)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 replacement has no final source registry handoff");
    char *map_name = copy_text(map->value, error);
    char *server_map_name = copy_text(server_map->value, error);
    qa_cvars_restore *ticket = NULL;
    bool okay = map_name && server_map_name && qa_cvars_save_prepare(cvars,
        (qa_bytes){state->native_cvars.data, state->native_cvars.size}, &ticket, error);
    if (okay) {
        okay = qa_cvars_save_commit(ticket, error);
        if (okay) ticket = NULL;
    }
    if (ticket) qa_cvars_save_abort(ticket);
    size_t count = qa_cvars_count(cvars);
    for (size_t i = 0; okay && i < count; ++i) {
        const qa_cvar_view *view = qa_cvars_at(cvars, i);
        if (view->latched_value) okay = qa_cvars_apply_latched(cvars, view->name, error);
    }
    char capacity[16];
    snprintf(capacity, sizeof(capacity), "%u", state->startup.max_clients);
    if (okay) okay = qa_cvars_set(cvars, "sv_maxclients", capacity, true, error) &&
        qa_cvars_set(cvars, "mapname", map_name, true, error) &&
        qa_cvars_set(cvars, "sv_mapname", server_map_name, true, error);
    free(map_name);
    free(server_map_name);
    return okay;
}

bool application_q3_world_restart_retired(qa_application *app,
    application_publication *publication, bool *retain_bots, qa_error *error)
{
    if (!retain_bots)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement requires its actual bot retirement disposition");
    *retain_bots = false;
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state) return true;
    if (!state->mutated || state->publication != publication || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement lost its genuine source retirement");
    for (size_t i = 0; i < publication->removed_count; ++i)
        if (publication->removed[i]->constructed)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement still owns an unretired source executor");
    if (!application_bots_original_rebind(state->bots, error)) return false;
    *retain_bots = state->bots != NULL;
    return true;
}

bool application_q3_world_restart_admitted(qa_application *app,
    application_publication *publication, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state) return true;
    if (!state->mutated || state->admitted || state->publication != publication ||
        !same_program(state, publication->map_provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement admission lost its source transaction");
    qa_clock_state clock;
    application_provider *provider = publication->map_provider;
    if (!(state->native ? import_native_handoff : import_guest_handoff)(state, provider, error)) return false;
    if (!qa_session_clock(app->session, provider->owner, &clock) ||
        clock.frame.kind != QA_CLOCK_Q3 || clock.frame.provider != provider->owner ||
        clock.frame.phase != QA_FRAME_EXIT || clock.frame_number || clock.elapsed_ns || clock.debt_ns ||
        clock.frame.time_ns != state->startup.initial_time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement has no genuine fresh source clock");
    state->admitted = true;
    return true;
}

bool application_q3_world_restart_published(qa_application *app,
    application_publication *publication, qa_error *error)
{
    application_q3_world_restart_state *state = app ? app->q3_world_restart : NULL;
    if (!state) return true;
    if (!state->admitted || state->published || state->publication != publication ||
        qa_application_launch(app) != state->candidate ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != publication->map_provider ||
        app->map_revision != state->map_revision + 1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement did not publish its actual source world");
    if (state->native &&
        !application_native_q3_wire_carry_finish(publication->map_provider, error)) return false;
    state->published = true;
    return true;
}

bool application_q3_world_restart(qa_application *app, application_provider *provider,
    qa_mode_id mode, bool *mutated, qa_error *error)
{
    if (!mutated) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement requires its mutation result");
    *mutated = false;
    if (!boundary(app, error)) return false;
    if (!provider || provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending || !provider->launch || !provider->product ||
        provider->product->family != QA_GAME_Q3 || provider->component.clock.kind != QA_CLOCK_Q3 ||
        provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        provider != application_native_q3_mode_source_provider(app, mode) ||
        !app->primary_mode_ready || !same_mode(mode, app->primary_mode))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement has no actual selected GAME source");
    application_q3_world_restart_state state = {.application = app, .source = provider,
        .launch = qa_application_launch(app), .native = provider->kind == APPLICATION_PROVIDER_Q3,
        .configuration_generation = qa_application_configuration_generation(app),
        .map_revision = app->map_revision, .outer_frame = application_frame_revision(app),
        .actor_revision = qa_actors_revision(qa_session_actors(app->session))};
    state.startup.restart = !state.native;
    qa_launch_snapshot_retain(state.launch);
    int32_t time, loaded_type;
    bool okay;
    if (state.native) {
        qa_q3_round_source source;
        okay = qa_q3_round_read(provider->state.q3, &source, error);
        if (okay) {
            time = source.current_time_ms;
            state.startup.random_seed = source.random_seed;
        }
    } else okay = application_q3_guest_world_source(provider, &time,
        &loaded_type, &state.startup.random_seed, error);
    qa_clock_state clock;
    if (okay && (!qa_session_clock(app->session, provider->owner, &clock) ||
        clock.frame.kind != QA_CLOCK_Q3 || clock.frame.provider != provider->owner ||
        clock.frame.phase != QA_FRAME_EXIT || clock.paused))
        okay = application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 replacement has no completed source EXIT clock");
    if (okay) state.startup.initial_time_ns = clock.frame.time_ns;
    if (okay && state.native)
        okay = application_native_q3_session_capture_carry(provider, error);
    if (okay) okay = capture_capacity(&state, error) && capture_cvars(&state, error) &&
        capture_clients(&state, error) && retained(&state, error);
    if (okay && state.native)
        okay = application_native_q3_wire_carry_capture(provider, &state.wire, error);
    qa_launch_draft *draft = NULL;
    if (okay) okay = make_draft(&state, mode, &draft, error);
    qa_configuration_transaction *transaction = NULL;
    if (okay) {
        app->q3_world_restart = &state;
        app->operation = APPLICATION_CONFIGURING;
        okay = qa_configuration_prepare_replacing(app->configuration, draft, &transaction, error);
        if (okay) {
            state.candidate = qa_configuration_candidate(transaction);
            qa_launch_snapshot_retain(state.candidate);
            okay = qa_configuration_validate(transaction, error);
        }
        if (okay)
            okay = application_startup_publication_prepare(app, state.publication, error);
        if (okay)
            okay = application_startup_program_publication_prepare(app, state.publication,
                &state.publication->programs, error) &&
                application_startup_program_publication_seal(state.publication->programs, error);
        if (okay) okay = qa_configuration_commit(transaction, error);
        if (okay) transaction = NULL;
        if (transaction) {
            qa_error cleanup = {0};
            qa_error first = error ? *error : (qa_error){0};
            bool aborted = qa_configuration_abort(transaction, &cleanup);
            if (!aborted && first.code == QA_OK) first = cleanup;
            if (error) *error = first;
        }
        if (okay && app->state == QA_APPLICATION_FAULTED) {
            okay = false;
            if (error) *error = app->publication_error;
        }
        if (okay && (!state.mutated || !state.published))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Q3 replacement omitted its actual publication phase");
        application_q3_world_restart_configuration_finish(app, state.candidate, okay);
        app->q3_world_restart = NULL;
        app->operation = APPLICATION_IDLE;
    }
    *mutated = state.mutated;
    if (!okay && state.mutated) application_fault(app, error);
    qa_launch_draft_destroy(draft);
    dispose(&state);
    return okay;
}
