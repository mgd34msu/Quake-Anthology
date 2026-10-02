#include "internal.h"
#include "guest_q3_private.h"
#include "native_q3_remote_role.h"
#include "qa/application_native_q3_client_modules.h"
#include "qa/application_network_q3_status.h"

static application_provider *receiver(qa_application *app, qa_actor_owner owner)
{
    application_provider *found = NULL;
    application_provider **rows = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t i = 0; i < count; ++i) if (rows[i] && rows[i]->owner == owner) {
        if (found) return NULL;
        found = rows[i];
    }
    return found;
}

bool qa_application_network_q3_cgame_host_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_application_network_q3_cgame_host *out,
    bool *present, qa_error *error)
{
    if (!app || !out || !present || !qa_application_q3_remote_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache requires its actual current physical CLIENT source");
    application_provider *provider = receiver(app, source->receiver.receiver);
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache lost its actual receiver provider");
    qa_application_network_q3_cgame_host value = {.source = *source};
    bool found = false;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        application_native_q3_client_modules *modules = NULL;
        if (!application_native_q3_remote_role_modules_read(provider, source, &modules, error)) return false;
        if (modules && (!qa_application_native_q3_client_modules_current(modules, source) ||
            !qa_application_native_q3_client_modules_optional_host_read(modules, QA_QVM_CGAME,
                &value.host, &value.context, &found, error))) return false;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        q3g_role *role = NULL;
        for (q3g_role *row = engine ? engine->roles : NULL; row; row = row->next)
            if (row->kind == QA_QVM_CGAME && row->seat == source->receiver.seat && row->ready &&
                !row->retired && !row->local_client && row->client_services.gamestate) {
                if (role) return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache has ambiguous physical hosts");
                role = row;
            }
        if (!role || !qa_q3_host_client_context_read(role->host, &value.context) ||
            value.context.service_owner != role->service_owner ||
            value.context.service_owner != source->receiver.service_owner)
            return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache lost its actual external-service host");
        value.host = role->host; found = true;
    }
    if (found && (!value.host || value.context.session != app->session ||
        value.context.role != QA_QVM_CGAME || value.context.owner != source->receiver.receiver ||
        !value.context.service_owner || value.context.console != source->receiver.console ||
        value.context.cvars != source->receiver.cvars ||
        value.context.command_context.owner != source->receiver.receiver ||
        value.context.command_context.seat != source->receiver.seat ||
        value.context.command_context.dialect != QA_CONSOLE_Q3))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache differs from its actual host namespace");
    if (!qa_application_q3_remote_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME cache source changed during its pure host observation");
    *out = found ? value : (qa_application_network_q3_cgame_host){0};
    *present = found;
    return true;
}

bool qa_application_network_q3_cgame_host_current(qa_application *app,
    const qa_application_network_q3_cgame_host *retained)
{
    qa_application_network_q3_cgame_host actual; bool present;
    if (!retained || !retained->host || !qa_application_network_q3_cgame_host_read(app,
        &retained->source, &actual, &present, NULL) || !present || actual.host != retained->host) return false;
    const qa_q3_host_client_context *a = &actual.context, *b = &retained->context;
    return a->session == b->session && a->role == b->role && a->owner == b->owner &&
        a->service_owner == b->service_owner && a->console == b->console && a->cvars == b->cvars &&
        a->client_time_cvars == b->client_time_cvars && a->client_time_owner == b->client_time_owner &&
        a->frontend_lifetime == b->frontend_lifetime && a->command_context.session == b->command_context.session &&
        a->command_context.owner == b->command_context.owner && a->command_context.client == b->command_context.client &&
        a->command_context.seat == b->command_context.seat && a->command_context.dialect == b->command_context.dialect &&
        a->command_context.origin == b->command_context.origin && a->command_context.direct == b->command_context.direct &&
        a->command_context.console_text == b->command_context.console_text && a->command_context.script == b->command_context.script &&
        a->command_context.registry == b->command_context.registry && a->command_context.generation == b->command_context.generation &&
        qa_actor_id_equal(a->command_context.actor, b->command_context.actor);
}
