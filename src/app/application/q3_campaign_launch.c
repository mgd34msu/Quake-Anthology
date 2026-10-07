#include "q3_campaign_launch.h"
#include "rankings.h"
#include "q3_product.h"
#include "startup_flow.h"

#include <stdlib.h>
#include <string.h>

struct application_q3_campaign_launch {
    const qa_launch_snapshot *previous;
    char *instance;
    qa_product_id product;
    qa_program_kind runtime;
    const qa_cvars *previous_cvars;
    qa_actor_owner previous_owner;
    application_provider *candidate;
    qa_cvars *candidate_cvars;
    uint64_t candidate_cvar_owner;
    bool shutdown_returned;
    bool original;
    qa_application_q3_setting *settings;
    size_t count;
};

static char *copy_text(const char *text, qa_error *error)
{
    size_t size = strlen(text);
    char *copy = size < SIZE_MAX ? malloc(size + 1) : NULL;
    if (!copy) {
        application_fail(error, QA_ERROR_MEMORY, "Retaining campaign startup text");
        return NULL;
    }
    memcpy(copy, text, size + 1);
    return copy;
}

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

bool application_q3_campaign_launch_finish(qa_application *app,
    bool published, qa_error *error)
{
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state) return true;
    (void)published;
    (void)error;
    app->q3_campaign_launch = NULL;
    qa_launch_snapshot_release(state->previous);
    for (size_t i = 0; i < state->count; ++i) {
        free((void *)state->settings[i].name);
        free((void *)state->settings[i].value);
    }
    free(state->settings);
    free(state->instance);
    free(state);
    return true;
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
        !cvars || !cvar_owner || !qa_cvars_same_store(cvars, app->cvars) ||
        cvars == state->previous_cvars || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign startup values require their fresh physical GAME registry");
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
        cvars != state->previous_cvars || !cvar_owner || state->shutdown_returned ||
        !qa_cvars_same_store(cvars, app->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign Shutdown carry lost its actual previous GAME registry");
    state->shutdown_returned = true;
    state->previous_cvars = NULL;
    return true;
}

bool application_q3_campaign_launch_retired(qa_application *app,
    application_publication *publication, qa_error *error)
{
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state) return true;
    if (!publication || !publication->sources_retired ||
        (state->original && !state->shutdown_returned) ||
        publication->map_provider != state->candidate || !state->candidate_cvars ||
        !qa_cvars_same_store(state->candidate_cvars, app->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign values require the actual completed Source Shutdown");
    for (size_t i = 0; i < state->count; ++i)
        if (!qa_cvars_set(state->candidate_cvars, state->settings[i].name,
                state->settings[i].value, true, error)) return false;
    return true;
}

bool application_q3_campaign_launch_admitted(qa_application *app,
    application_publication *publication, qa_error *error)
{
    struct application_q3_campaign_launch *state = app ? app->q3_campaign_launch : NULL;
    if (!state || !state->original) return true;
    application_provider *provider = publication ? publication->map_provider : NULL;
    return app->operation == APPLICATION_CONFIGURING && publication &&
        publication->published && publication->sources_retired && state->shutdown_returned &&
        provider == state->candidate && provider->attached && provider->constructed &&
        state->candidate_cvar_owner && qa_cvars_same_store(state->candidate_cvars, app->cvars)
        ? true : application_fail(error, QA_ERROR_ARGUMENT,
            "Campaign admission lacks its genuine completed Source Shutdown");
}

static bool campaign_launch(qa_application *app,
    const qa_application_q3_campaign *source, const qa_launch_draft *draft,
    const qa_application_q3_setting *settings, size_t count, qa_error *error)
{
    if (!app || !draft || (count && !settings) ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING) ||
        app->operation != APPLICATION_IDLE || app->q3_campaign_launch || qa_application_startup_pending(app) ||
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

    if (count > SIZE_MAX / sizeof(*settings))
        return application_fail(error, QA_ERROR_MEMORY, "Campaign startup settings exceed retained storage");
    struct application_q3_campaign_launch *state = calloc(1, sizeof(*state));
    if (!state) return application_fail(error, QA_ERROR_MEMORY, "Retaining campaign launch owner");
    *state = (struct application_q3_campaign_launch){
        .previous = source ? source->publication : qa_application_launch(app),
        .product = selection->product, .runtime = selection->runtime,
        .previous_cvars = source ? source->cvars : NULL,
        .previous_owner = source ? source->source_owner : 0,
        .original = source && !source->native_source};
    if (state->previous) qa_launch_snapshot_retain(state->previous);
    app->q3_campaign_launch = state;
    state->instance = copy_text(selection->instance, error);
    if (count) state->settings = calloc(count, sizeof(*state->settings));
    bool copied = state->instance && (!count || state->settings);
    if (!copied && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_MEMORY, "Retaining campaign startup settings");
    for (size_t i = 0; copied && i < count; ++i) {
        state->count = i + 1;
        state->settings[i].name = copy_text(settings[i].name, error);
        state->settings[i].value = copy_text(settings[i].value, error);
        copied = state->settings[i].name && state->settings[i].value;
    }
    if (!copied) {
        (void)application_q3_campaign_launch_finish(app, false, NULL);
        return false;
    }
    app->operation = APPLICATION_CONFIGURING;
    if (app->startup_hooks) {
        bool started = application_startup_flow_begin_replacing(app, draft, error);
        app->operation = APPLICATION_IDLE;
        if (!started && !qa_application_startup_pending(app))
            (void)application_q3_campaign_launch_finish(app, false, NULL);
        return started;
    }
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
    bool finished = application_q3_campaign_launch_finish(app, published, error);
    okay = okay && finished;
    if (okay && app->state == QA_APPLICATION_FAULTED) {
        okay = false;
        if (error) *error = app->publication_error;
    }
    if (okay && app->state == QA_APPLICATION_READY)
        app->state = QA_APPLICATION_RUNNING;
    app->operation = APPLICATION_IDLE;
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
