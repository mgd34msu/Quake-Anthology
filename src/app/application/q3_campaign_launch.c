#include "q3_campaign_launch.h"
#include "rankings.h"
#include "q3_product.h"
#include "qa/cvars_save.h"

#include <stdlib.h>
#include <string.h>

struct application_q3_campaign_launch {
    const qa_launch_snapshot *previous;
    const char *instance;
    qa_product_id product;
    qa_program_kind runtime;
    const qa_cvars *previous_cvars;
    qa_actor_owner previous_owner;
    application_provider *candidate;
    qa_cvars *candidate_cvars;
    uint64_t candidate_cvar_owner, previous_cvar_owner;
    qa_buffer final_cvars;
    bool original;
    const qa_application_q3_setting *settings;
    size_t count;
};

static bool named(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++, b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return *left == *right;
}

bool application_q3_campaign_launch_cvars(application_provider *provider,
    qa_cvars *cvars, uint64_t cvar_owner, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state) return true;
    if (!provider->launch || strcmp(provider->launch->selection.instance, state->instance))
        return true;
    if (app->operation != APPLICATION_CONFIGURING || provider->attached ||
        provider->launch->selection.product != state->product ||
        provider->launch->selection.runtime != state->runtime ||
        !provider->product || provider->product->family != QA_GAME_Q3 ||
        !cvars || !cvar_owner || cvars == app->cvars ||
        cvars == state->previous_cvars || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign startup values require their fresh physical GAME registry");
    for (size_t i = 0; i < qa_cvars_count(state->previous_cvars); ++i) {
        const qa_cvar_view *value = qa_cvars_at(state->previous_cvars, i);
        if (named(value->name, "mapname") || named(value->name, "sv_mapname") ||
            named(value->name, "sv_cheats")) continue;
        uint64_t owner = value->owner == state->previous_owner ? provider->owner :
            value->owner ? cvar_owner : 0;
        if (!qa_cvars_register(cvars, value->name, value->reset_value, value->flags,
                owner, value->description, error) ||
            !qa_cvars_set(cvars, value->name,
                value->latched_value ? value->latched_value : value->value,
                true, error)) return false;
    }
    for (size_t i = 0; i < state->count; ++i) {
        if (named(state->settings[i].name, "sv_cheats")) continue;
        if (!qa_cvars_set(cvars, state->settings[i].name,
            state->settings[i].value, true, error)) return false;
    }
    state->candidate = provider;
    state->candidate_cvars = cvars;
    state->candidate_cvar_owner = cvar_owner;
    return true;
}

bool application_q3_campaign_launch_guest_handoff(application_provider *provider,
    qa_cvars *cvars, uint64_t cvar_owner, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state || provider->owner != state->previous_owner) return true;
    if (!state->original || app->operation != APPLICATION_CONFIGURING ||
        !provider->attached || !provider->constructed || provider->close_pending ||
        cvars != state->previous_cvars || !cvar_owner || state->final_cvars.data)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign Shutdown carry lost its actual previous GAME registry");
    if (!qa_cvars_save_capture(cvars, &state->final_cvars, error)) return false;
    state->previous_cvar_owner = cvar_owner;
    state->previous_cvars = NULL;
    return true;
}

bool application_q3_campaign_launch_admitted(qa_application *app,
    application_publication *publication, qa_error *error)
{
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state || !state->original) return true;
    application_provider *provider = publication ? publication->map_provider : NULL;
    qa_cvars *cvars = state->candidate_cvars;
    const qa_cvar_view *map = qa_cvars_find(cvars, "mapname");
    const qa_cvar_view *capacity = qa_cvars_find(cvars, "sv_maxclients");
    if (app->operation != APPLICATION_CONFIGURING || !publication ||
        !publication->published || provider != state->candidate ||
        !provider->attached || !provider->constructed || !state->final_cvars.data ||
        !map || !capacity || !state->candidate_cvar_owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign admission lacks its genuine final Shutdown carry");
    size_t map_length = strlen(map->value);
    char *map_name = malloc(map_length + 1);
    if (!map_name) return application_fail(error, QA_ERROR_MEMORY,
        "Retaining campaign destination map identity");
    memcpy(map_name, map->value, map_length + 1);
    size_t capacity_length = strlen(capacity->value);
    char *max_clients = malloc(capacity_length + 1);
    if (!max_clients) {
        free(map_name);
        return application_fail(error, QA_ERROR_MEMORY,
            "Retaining campaign physical client capacity");
    }
    memcpy(max_clients, capacity->value, capacity_length + 1);
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(cvars,
        (qa_bytes){state->final_cvars.data, state->final_cvars.size}, &ticket, error);
    if (okay) okay = qa_cvars_save_commit(ticket, error);
    if (!okay) qa_cvars_save_abort(ticket);
    size_t count = okay ? qa_cvars_count(cvars) : 0;
    qa_cvar_record_state *rows = NULL;
    if (okay && count > SIZE_MAX / sizeof(*rows))
        okay = application_fail(error, QA_ERROR_MEMORY, "Campaign cvar owners are exhausted");
    if (okay && count && !(rows = calloc(count, sizeof(*rows))))
        okay = application_fail(error, QA_ERROR_MEMORY, "Retaining campaign cvar owners");
    qa_cvar_registry_state registry;
    if (okay) okay = qa_cvars_capture_metadata(cvars, &registry, rows, count, error);
    for (size_t i = 0; okay && i < count; ++i) {
        if (rows[i].owner == state->previous_owner) rows[i].owner = provider->owner;
        else if (rows[i].owner == state->previous_cvar_owner)
            rows[i].owner = state->candidate_cvar_owner;
    }
    if (okay) okay = qa_cvars_restore_metadata(cvars, &registry, rows, count, error);
    free(rows);
    if (okay) okay = qa_cvars_apply_latched(cvars, NULL, error) &&
        qa_cvars_set(cvars, "mapname", map_name, true, error) &&
        qa_cvars_set(cvars, "sv_mapname", map_name, true, error);
    for (size_t i = 0; okay && i < state->count; ++i)
        if (!named(state->settings[i].name, "sv_cheats"))
            okay = qa_cvars_set(cvars, state->settings[i].name,
                state->settings[i].value, true, error);
    if (okay) okay = qa_cvars_set(cvars, "sv_maxclients", max_clients, true, error);
    free(max_clients);
    free(map_name);
    return okay;
}

static bool campaign_launch(qa_application *app,
    const qa_application_q3_campaign *source, const qa_launch_draft *draft,
    const qa_application_q3_setting *settings, size_t count, qa_error *error)
{
    if (!app || !draft || (count && !settings) ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING) ||
        app->operation != APPLICATION_IDLE || app->q3_campaign_launch ||
        app->q3_round_active || app->q3_world_restart || app->frame_preparing ||
        app->publication_started || app->pending_close || app->destroy_requested ||
        app->finalizing || !qa_session_safe(app->session) ||
        (app->world && !qa_world_idle(app->world)) || !qa_combat_idle(app->combat) ||
        !qa_console_idle(app->console) || !application_guests_idle(app) ||
        !application_rankings_idle(app) || !application_bots_can_destroy(app) ||
        (source && (!source->cvars || qa_cvars_dialect(source->cvars) != QA_CONSOLE_Q3 ||
            !qa_application_q3_campaign_current(app, source))))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign launch requires an idle application and current source cut");
    const qa_launch_choices *choices = qa_launch_draft_choices(draft);
    const qa_launch_binding *binding = choices ? qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "") : NULL;
    const qa_launch_provider *selection = NULL;
    for (size_t i = 0; binding && i < choices->provider_count; ++i)
        if (!strcmp(choices->providers[i].instance, binding->instance)) {
            selection = choices->providers + i;
            break;
        }
    const qa_product *product = selection ? qa_catalog_product(
        qa_launch_draft_catalog(draft), selection->product) : NULL;
    if (!selection || !product || product->family != QA_GAME_Q3 ||
        (source && (!source->launch ||
            strcmp(selection->instance, source->launch->selection.instance) ||
            selection->product != source->content_product ||
            selection->runtime != source->launch->selection.runtime)) ||
        selection->clock.kind != QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign draft must retain the actual selected Q3 GAME identity");
    for (size_t i = 0; i < count; ++i)
        if (!settings[i].name || !*settings[i].name || !settings[i].value)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Campaign startup values require source names and values");

    struct application_q3_campaign_launch state = {
        .previous = source ? source->publication : qa_application_launch(app),
        .instance = selection->instance,
        .product = selection->product, .runtime = selection->runtime,
        .previous_cvars = source ? source->cvars : NULL,
        .previous_owner = source ? source->source_owner : 0,
        .original = source && !source->native_source,
        .settings = settings, .count = count};
    if (state.previous) qa_launch_snapshot_retain(state.previous);
    app->q3_campaign_launch = &state;
    app->operation = APPLICATION_CONFIGURING;
    qa_configuration_transaction *transaction = NULL;
    application_q3_product_preparation prepared = {0};
    bool okay = application_q3_product_prepare_draft(app, draft, &prepared, error) &&
        qa_configuration_prepare_replacing(app->configuration, prepared.draft,
        &transaction, error) && qa_configuration_validate(transaction, error);
    if (okay) okay = qa_configuration_commit(transaction, error);
    bool published = okay;
    if (published) transaction = NULL;
    if (transaction) {
        qa_error cleanup = {0};
        qa_error first = error ? *error : (qa_error){0};
        if (!qa_configuration_abort(transaction, &cleanup) && first.code == QA_OK)
            first = cleanup;
        if (error) *error = first;
    }
    application_q3_product_finish(app, &prepared, published);
    if (published && app->state != QA_APPLICATION_FAULTED)
        for (size_t i = 0; okay && i < count; ++i)
            if (named(settings[i].name, "sv_cheats")) {
                okay = qa_cvars_set(app->cvars, "sv_cheats", settings[i].value,
                    true, error);
                if (!okay) application_fault(app, error);
            }
    if (okay && app->state == QA_APPLICATION_FAULTED) {
        okay = false;
        if (error) *error = app->publication_error;
    }
    if (okay && app->state == QA_APPLICATION_READY)
        app->state = QA_APPLICATION_RUNNING;
    app->q3_campaign_launch = NULL;
    app->operation = APPLICATION_IDLE;
    if (state.previous) qa_launch_snapshot_release(state.previous);
    qa_buffer_free(&state.final_cvars);
    return okay;
}

bool qa_application_q3_campaign_launch(qa_application *app,
    const qa_application_q3_campaign *source, const qa_launch_draft *draft,
    const qa_application_q3_setting *settings, size_t count, qa_error *error)
{
    if (!source) return application_fail(error, QA_ERROR_ARGUMENT,
        "Campaign travel requires its current physical GAME cut");
    return campaign_launch(app, source, draft, settings, count, error);
}

bool qa_application_q3_campaign_start(qa_application *app, const qa_launch_draft *draft,
    const qa_application_q3_setting *settings, size_t count, qa_error *error)
{
    return campaign_launch(app, NULL, draft, settings, count, error);
}
