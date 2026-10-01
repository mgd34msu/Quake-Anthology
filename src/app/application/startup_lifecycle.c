#include "startup_flow.h"

static const qa_application_startup_hooks *hooks_for(application_provider *provider)
{ return provider && provider->application ? provider->application->startup_hooks : NULL; }

static const qa_launch_snapshot *source_snapshot(application_provider *provider)
{
    qa_application *app = provider->application;
    return app->routing_snapshot ? app->routing_snapshot : qa_application_launch(app);
}

bool application_startup_source_preinit(application_provider *provider, qa_console *console,
    qa_cvars *cvars, const qa_command_context *command, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || provider->application->operation == APPLICATION_PERSISTING) return true;
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshot = source_snapshot(provider);
    const qa_launch_instance *instance = provider->launch && snapshot
        ? qa_launch_snapshot_find(snapshot, provider->launch->selection.instance) : NULL;
    if (!hooks->preinit_source || !console || !cvars || !command || !instance ||
        instance->state != provider || provider->attached || provider->close_pending ||
        app->startup_preinit_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source preinitialization lost its real candidate owner");
    app->startup_preinit_provider = provider;
    qa_command_context captured;
    bool ok = qa_application_capture_command_context(app, command, &captured, error) &&
        hooks->preinit_source(hooks->context, app, snapshot, instance, console, cvars, &captured, error);
    app->startup_preinit_provider = NULL;
    return ok;
}

bool application_startup_source_restore(application_provider *provider, qa_console *console,
    qa_cvars *cvars, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) return true;
    const qa_launch_snapshot *snapshot = source_snapshot(provider);
    const qa_launch_instance *instance = provider->launch && snapshot
        ? qa_launch_snapshot_find(snapshot, provider->launch->selection.instance) : NULL;
    if (!hooks->restore_source || !console || !cvars || !instance || instance->state != provider ||
        provider->application->operation != APPLICATION_PERSISTING || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source restoration needs its decoded physical owner");
    return hooks->restore_source(hooks->context, provider->application, snapshot, instance, console, cvars, error);
}

bool application_startup_source_retire(application_provider *provider, qa_console *console,
    qa_cvars *cvars, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) return true;
    if (!hooks->retire_source || !console || !cvars || provider->attached ||
        provider->component_attached || provider->policy_attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source retirement requires its detached live console");
    return hooks->retire_source(hooks->context, provider->application, provider->launch, console, cvars, error);
}

qa_cvars *application_startup_cvar_owner(application_provider *provider, qa_console *console,
    const qa_command_context *command, const char *name)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    return hooks && hooks->cvar_owner
        ? hooks->cvar_owner(hooks->context, provider->application, console, command, name) : NULL;
}

bool application_startup_visible_cvars(application_provider *provider, qa_console *console,
    const qa_command_context *command, size_t index, qa_cvars **out)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    return hooks && hooks->visible_cvars &&
        hooks->visible_cvars(hooks->context, provider->application, console, command, index, out);
}
