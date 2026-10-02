#include "internal.h"
#include "qa/application_q3_asset_selection.h"

static bool allowed(qa_launch_role role)
{ return role == QA_ROLE_CHARACTER || role == QA_ROLE_BODY || role == QA_ROLE_SKIN || role == QA_ROLE_ARSENAL; }

static bool ready(const qa_application *app, qa_actor_id actor)
{
    return app && app->session && !app->destroy_requested && !app->routing_snapshot &&
        !app->q3_round_active && !app->q3_world_restart && app->state != QA_APPLICATION_FAULTED &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING ||
            app->operation == APPLICATION_PERSISTING) &&
        qa_actors_get(qa_session_actors(app->session), actor);
}

bool qa_application_q3_asset_selection_read(qa_application *app, qa_actor_id actor,
    qa_launch_role role, qa_application_q3_asset_selection *out, bool *found, qa_error *error)
{
    if (!out || !found || !allowed(role) || !ready(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 asset selection requires its genuine full actor and content role");
    *found = false;
    application_provider *provider = application_provider_for(app, actor, role, "");
    if (!provider) return true;
    if (provider->application != app || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->launch->content || !provider->product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 asset selection lost its actual constructed content provider");
    *out = (qa_application_q3_asset_selection){.actor = actor, .role = role, .provider = provider->owner,
        .launch = provider->launch, .content = provider->launch->content, .product = provider->product->id,
        .family = provider->product->family, .publication_generation = app->publication_generation,
        .map_revision = app->map_revision};
    *found = true; return true;
}

bool qa_application_q3_asset_selection_current(qa_application *app,
    const qa_application_q3_asset_selection *saved)
{
    qa_application_q3_asset_selection actual; bool found;
    return saved && qa_application_q3_asset_selection_read(app, saved->actor, saved->role, &actual, &found, NULL) && found &&
        actual.provider == saved->provider && actual.launch == saved->launch && actual.content == saved->content &&
        actual.product == saved->product && actual.family == saved->family &&
        actual.publication_generation == saved->publication_generation && actual.map_revision == saved->map_revision;
}
