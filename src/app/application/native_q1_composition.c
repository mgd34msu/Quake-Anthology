#include "native_q1_composition.h"
#include "native_q1_console.h"
#include "map_players_private.h"
#include "qa/game_q1_bots.h"
#include "qa/application_q1_composition.h"

#include <string.h>

static bool source_options(application_provider *source, qa_q1_options *options,
    qa_error *error)
{
    double seconds;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1 ||
        !source->constructed || source->close_pending ||
        !qa_q1_source_respawn_options_read(source->state.q1, options, &seconds, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition has no constructed native GAME owner");
    return options->provider == source->owner || application_fail(error, QA_ERROR_ARGUMENT,
        "Q1 composition differs from its physical GAME identity");
}

bool application_native_q1_composition_current(void *opaque, qa_actor_owner owner,
    qa_mode_source kind, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *source = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    qa_q1_options options;
    qa_clock_state clock;
    if (!app || app->destroy_requested || app->finalizing || !app->session || !app->world ||
        !source || source->owner != owner || !source->attached ||
        !source_options(source, &options, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition lost its actual physical source publication");
    if ((kind != QA_MODE_THREEWAVE || options.program != QA_Q1_CTF) &&
        (kind != QA_MODE_ROGUE || options.program != QA_Q1_ROGUE))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition does not match the actual source program");
    if (!qa_session_clock(app->session, owner, &clock) || clock.frame.provider != owner ||
        clock.frame.kind != source->component.clock.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition lost its genuine source clock");
    qa_cvars *cvars = application_native_q1_console_registry(source);
    const qa_cvar_view *teamplay = cvars ? qa_cvars_find(cvars, "teamplay") : NULL;
    const qa_cvar_view *gamecfg = cvars ? qa_cvars_find(cvars, "gamecfg") : NULL;
    return (teamplay && gamecfg && teamplay->owner == owner && gamecfg->owner == owner) ||
        application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition lost its actual GAME cvar namespace");
}

static bool chosen_modes_current(qa_application *app, qa_error *error)
{
    const qa_launch_snapshot *snapshot = app->routing_snapshot;
    if (!snapshot && app->configuration) snapshot = qa_configuration_current(app->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!app->modes || !choices || application_mode_choice(app, app->mode_count) ||
        (app->mode_count && !app->mode_ids))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition lost its actual chosen-rule roster");
    bool primary = !app->primary_mode_ready;
    for (size_t i = 0; i < app->mode_count; ++i) {
        qa_mode_id id = app->mode_ids[i];
        qa_mode_view view;
        const qa_launch_mode *choice = application_mode_choice(app, i);
        if (!qa_modes_read(app->modes, id, &view, error)) return false;
        if (!choice || view.origin != QA_MODE_CHOSEN_RULE || view.source_owner ||
            view.rules.source != choice->rules.source ||
            view.rules.kind != choice->rules.kind)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "chosen-rule identity names a different mode continuation");
        for (size_t j = 0; j < i; ++j)
            if (app->mode_ids[j].slot == id.slot && app->mode_ids[j].generation == id.generation)
                return application_fail(error, QA_ERROR_ARGUMENT, "chosen-rule identity is duplicated");
        if (id.slot == app->primary_mode.slot && id.generation == app->primary_mode.generation)
            primary = true;
    }
    size_t chosen = 0;
    for (size_t i = 0;; ++i) {
        qa_mode_id id;
        qa_mode_view view;
        qa_error current = {0};
        if (!qa_modes_at(app->modes, i, &id, &view, &current)) {
            if (current.code == QA_ERROR_NOT_FOUND) break;
            if (error) *error = current;
            return false;
        }
        if (view.origin == QA_MODE_CHOSEN_RULE) ++chosen;
    }
    return (primary && chosen == app->mode_count) ||
        application_fail(error, QA_ERROR_ARGUMENT,
            "chosen primary or mode count differs from the actual rule roster");
}

bool application_native_q1_composition_expected(void *opaque, qa_modes *modes, bool *present,
    qa_actor_owner *owner, qa_mode_source *kind, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *source = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!app || app->destroy_requested || app->finalizing || !app->session || !app->world ||
        !source || !source->constructed || !source->attached || source->close_pending ||
        !modes || app->modes != modes || !present || !owner || !kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition has no current physical GAME publication");
    if (!chosen_modes_current(app, error)) return false;
    *present = false;
    *owner = 0;
    *kind = QA_MODE_Q1;
    if (source->kind != APPLICATION_PROVIDER_Q1) return true;
    qa_q1_options options;
    if (!source_options(source, &options, error)) return false;
    if (options.program != QA_Q1_CTF && options.program != QA_Q1_ROGUE) return true;
    qa_mode_source actual = options.program == QA_Q1_CTF ? QA_MODE_THREEWAVE : QA_MODE_ROGUE;
    if (!application_native_q1_composition_current(app, source->owner, actual, error))
        return false;
    *present = true;
    *owner = source->owner;
    *kind = actual;
    return true;
}

bool application_native_q1_composition_prepare(qa_application *app,
    application_publication *publication, qa_error *error)
{
    if (!app || !publication || !publication->modes || !publication->map_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 composition lost its candidate owner");
    if (publication->restoring || publication->map_provider->kind != APPLICATION_PROVIDER_Q1)
        return true;
    application_provider *source = publication->map_provider;
    qa_q1_options options;
    if (!source_options(source, &options, error)) return false;
    if (options.program != QA_Q1_CTF && options.program != QA_Q1_ROGUE) return true;
    qa_mode_source kind = options.program == QA_Q1_CTF ? QA_MODE_THREEWAVE : QA_MODE_ROGUE;
    qa_mode_kind game = kind == QA_MODE_THREEWAVE || options.teamplay == 4 || options.teamplay == 6
        ? QA_MODE_CTF : options.teamplay == 5 ? QA_MODE_TAG : QA_MODE_TEAM_DEATHMATCH;
    qa_mode_rules rules = qa_mode_defaults(kind, game);
    rules.teamplay = options.teamplay;
    rules.flags = options.gamecfg;
    rules.rogue_deathmatch = options.deathmatch != 0;
    rules.relics = kind == QA_MODE_THREEWAVE ||
        (rules.rogue_deathmatch && (options.gamecfg & 1) != 0);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(publication->candidate);
    const char *map = choices ? choices->world.map : NULL;
    if (!map) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 composition has no actual candidate map");
    const char *name = strrchr(map, '/');
    name = name ? name + 1 : map;
    rules.start_map = !strcmp(name, "start") || !strcmp(name, "start.bsp");
    static const char *const teams[] = {"red", "blue", "grey"};
    for (size_t i = 0; i < 3; ++i)
        if (!qa_strings_intern_cstr(qa_session_strings(app->session), teams[i],
            &rules.teams[i], error)) return false;
    qa_mode_id id;
    return qa_modes_add_q1_composition(publication->modes, &rules, source->owner, &id, error);
}

bool application_native_q1_composition_player_current(void *opaque, qa_actor_owner owner,
    qa_mode_source kind, qa_actor_id actor, bool *observer, qa_error *error)
{
    qa_application *app = opaque;
    if (!observer || !application_native_q1_composition_current(app, owner, kind, error))
        return false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    uint32_t slot;
    qa_q1_source_client_view client;
    if (!app->players || app->players->map_provider != source ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        !qa_q1_native_client_slot(source->state.q1, actor, &slot, error) ||
        !qa_q1_source_client_read(source->state.q1, actor, &client) ||
        !qa_actor_id_equal(client.actor, actor) || client.slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 composition lost its physical source client");
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *record = app->players->records + i;
        if (!record->retiring && record->client_slot == slot &&
            qa_actor_id_equal(record->actor, actor)) {
            *observer = client.observer;
            return true;
        }
    }
    return application_fail(error, QA_ERROR_ARGUMENT,
        "Q1 composition client differs from its actual published roster");
}

bool application_native_q1_composition_mode(qa_application *app, application_provider *source,
    qa_mode_id *out, qa_error *error)
{
    if (!app || !app->modes || !source || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 composition lookup lost its actual owner");
    for (size_t i = 0;; ++i) {
        qa_mode_view view;
        qa_mode_id id;
        qa_error current = {0};
        if (!qa_modes_at(app->modes, i, &id, &view, &current)) {
            if (current.code == QA_ERROR_NOT_FOUND) break;
            if (error) *error = current;
            return false;
        }
        if (view.origin == QA_MODE_NATIVE_Q1_COMPOSITION && view.source_owner == source->owner) {
            if (!application_native_q1_composition_current(app, source->owner,
                view.rules.source, error)) return false;
            *out = id;
            return true;
        }
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 source has no genuine composition controller");
}

bool qa_application_q1_ctf_recipient_read(qa_application *app, qa_actor_owner owner,
    qa_actor_id actor, uint64_t *source_time_ns, bool *found, qa_error *error)
{
    if (!app || !app->session || !app->world || !source_time_ns || !found ||
        app->destroy_requested || app->finalizing)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF recipient has no actual application owner");
    *found = false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->owner != owner || source->kind != APPLICATION_PROVIDER_Q1)
        return true;
    qa_q1_options options;
    if (!source_options(source, &options, error)) return false;
    if (options.program != QA_Q1_CTF) return true;
    if (!application_native_q1_composition_current(app, owner, QA_MODE_THREEWAVE, error))
        return false;
    qa_q1_source_client_view client;
    if (!qa_actors_get(qa_session_actors(app->session), actor) ||
        !qa_q1_source_client_read(source->state.q1, actor, &client)) return true;
    bool observer;
    uint64_t time_ns;
    double seconds;
    if (!application_native_q1_composition_player_current(app, owner, QA_MODE_THREEWAVE,
            actor, &observer, error)) return false;
    if (!qa_q1_game_clock_read(source->state.q1, &time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF recipient lost its actual source client clock");
    *source_time_ns = time_ns;
    *found = true;
    return true;
}
