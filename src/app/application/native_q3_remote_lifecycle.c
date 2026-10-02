#include "native_q3_remote_role_private.h"
#include "qa/application_native_q3_remote_lifecycle.h"

static struct application_native_q3_remote_role *source_row(qa_application *app,
    const qa_application_q3_remote_source *source, qa_error *error)
{
    if (!app || !source || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->destroy_requested || app->q3_round_active || app->q3_world_restart ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        !source->receiver.native_source || !qa_application_q3_remote_source_current(app, source)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT lifecycle requires its current idle physical source");
        return NULL;
    }
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->owner == source->receiver.receiver) {
            if (provider) return NULL;
            provider = app->providers[i];
        }
    if (!provider || provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !application_native_q3_remote_roles_idle(provider)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT lifecycle retains a source callback or detached receiver");
        return NULL;
    }
    for (struct application_native_q3_remote_role *row = provider->native_q3_remote_roles; row; row = row->next)
        if (row->seat == source->receiver.seat && !row->retiring) return row;
    application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT lifecycle lost its actual authored seat");
    return NULL;
}

bool qa_native_q3_remote_client_clear(qa_application *app, const qa_native_q3_remote_retirement *request,
    qa_application_q3_remote_source *out, qa_error *error)
{
    if (!request || !request->context || !request->retire || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT clear requires its actual checked frontend retirement");
    struct application_native_q3_remote_role *row = source_row(app, &request->previous, error);
    if (!row || row->modules_restore.size) return false;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = request->retire(request->context, &request->previous, error);
    app->operation = APPLICATION_IDLE;
    if (!ok) return false;
    /* The callback may refresh initialized while consuming its service. The
     * retained descriptor, epoch and physical configuration must survive. */
    qa_application_q3_remote_source actual;
    if (row->modules || row->service || row->transport || row->initialized || row->calls || row->module_calls ||
        !qa_console_idle(row->console) || !qa_cvars_observer_idle(row->cvars) ||
        !application_native_q3_remote_role_source_read(row->provider, row->seat,
            request->previous.connection_epoch, &actual, error) ||
        actual.descriptor->storage != request->previous.descriptor->storage ||
        actual.descriptor->content != request->previous.descriptor->content ||
        actual.configuration_generation != request->previous.configuration_generation ||
        actual.receiver.frontend_lifetime != request->previous.receiver.frontend_lifetime ||
        actual.receiver.console != request->previous.receiver.console ||
        actual.receiver.cvars != request->previous.receiver.cvars || !source_row(app, &actual, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT retirement did not return its complete physical owner");
    row->connection_epoch = request->previous.connection_epoch;
    row->lifecycle = NATIVE_Q3_REMOTE_CLEARED;
    *out = actual; return true;
}

bool qa_native_q3_remote_client_rebind(qa_application *app, const qa_application_q3_remote_binding *request,
    qa_application_q3_remote_source *out, qa_error *error)
{
    if (!request || !out || !request->connection || !request->current ||
        request->new_epoch <= request->previous.connection_epoch)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT rebind requires its true fresh transport admission");
    struct application_native_q3_remote_role *row = source_row(app, &request->previous, error);
    if (!row || row->lifecycle != NATIVE_Q3_REMOTE_CLEARED || row->modules || row->service || row->transport ||
        row->initialized || row->modules_restore.size || row->connection_epoch != request->previous.connection_epoch ||
        !request->current(request->connection, request->previous.connection_epoch, request->new_epoch, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native CLIENT rebind has no completed retirement or current transport proof");
    if (source_row(app, &request->previous, error) != row ||
        !request->current(request->connection, request->previous.connection_epoch, request->new_epoch, error)) return false;
    row->connection_epoch = request->new_epoch;
    qa_application_q3_remote_source rebound = request->previous;
    rebound.connection_epoch = request->new_epoch;
    *out = rebound; return true;
}
