#include "guest_q3_private.h"
#include "qa/application_q3_scene_world.h"

static qa_q3_host *role_host(qa_application *app,
    const qa_application_q3_scene_role *wanted, qa_error *error)
{
    if (!app || !wanted || !wanted->receiver || !wanted->service_owner ||
        !wanted->frontend_lifetime || (wanted->role != QA_QVM_CGAME && wanted->role != QA_QVM_UI) ||
        app->destroy_requested || app->state == QA_APPLICATION_FAULTED ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_CONFIGURING &&
         app->operation != APPLICATION_PERSISTING && app->operation != APPLICATION_DESTROYING)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 scene exchange requires its idle actual role lease");
        return NULL;
    }
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    application_provider *provider = NULL;
    for (size_t i = 0; providers && i < count; ++i)
        if (providers[i] && providers[i]->owner == wanted->receiver) {
            if (provider) {
                application_fail(error, QA_ERROR_ARGUMENT, "Q3 scene receiver has ambiguous provider ownership");
                return NULL;
            }
            provider = providers[i];
        }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!provider || provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending || !engine || engine->provider != provider ||
        engine->world != app->world || engine->round.phase != Q3G_ROUND_NONE) {
        application_fail(error, QA_ERROR_NOT_FOUND, "Q3 scene receiver is not an installed idle source");
        return NULL;
    }
    q3g_role *selected = NULL;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == wanted->role && role->seat == wanted->seat &&
            role->service_owner == wanted->service_owner) {
            if (selected) {
                application_fail(error, QA_ERROR_ARGUMENT, "Q3 scene lease has ambiguous physical roles");
                return NULL;
            }
            selected = role;
        }
    qa_q3_host_client_context context;
    if (!selected || selected->engine != engine || !selected->ready || !selected->host ||
        !qa_q3_host_client_context_read(selected->host, &context) || context.session != app->session ||
        context.role != wanted->role || context.owner != wanted->receiver ||
        context.service_owner != wanted->service_owner || context.frontend_lifetime != wanted->frontend_lifetime ||
        context.command_context.owner != wanted->receiver || context.command_context.seat != wanted->seat ||
        context.command_context.dialect != QA_RULESET_Q3) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 scene lease differs from its actual role host");
        return NULL;
    }
    return selected->host;
}

bool qa_application_q3_scene_world_read(qa_application *app,
    const qa_application_q3_scene_role *role, const qa_scene_world **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 scene read requires its borrowed world output");
    qa_q3_host *host = role_host(app, role, error);
    if (!host) return false;
    const qa_scene_world *world = qa_q3_host_scene_world(host);
    if (!qa_q3_host_scene_world_rebind_ready(host, world, world, error)) return false;
    *out = world;
    return true;
}

bool qa_application_q3_scene_world_rebind_ready(qa_application *app,
    const qa_application_q3_scene_role *role, const qa_scene_world *current,
    const qa_scene_world *destination, qa_error *error)
{
    qa_q3_host *host = role_host(app, role, error);
    return host && qa_q3_host_scene_world_rebind_ready(host, current, destination, error);
}

void qa_application_q3_scene_world_rebind(qa_application *app,
    const qa_application_q3_scene_role *role, qa_scene_world *destination)
{
    qa_q3_host *host = role_host(app, role, NULL);
    if (host) qa_q3_host_scene_world_rebind(host, destination);
}
