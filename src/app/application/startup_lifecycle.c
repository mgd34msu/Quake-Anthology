#include "startup_flow.h"
#include "native_q1_console.h"
#include "native_q2_console.h"
#include "native_q3_console.h"
#include "guest_q3_client_console.h"
#include "startup_program.h"
#include "engine_shutdown.h"
#include <string.h>

static const qa_application_startup_hooks *hooks_for(const application_provider *provider)
{ return provider && provider->application ? provider->application->startup_hooks : NULL; }

static const qa_launch_snapshot *source_snapshot(application_provider *provider)
{
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshots[] = {app->routing_snapshot,
        qa_application_startup_candidate(app), qa_application_launch(app)};
    for (size_t i = 0; i < sizeof(snapshots) / sizeof(*snapshots); ++i) {
        const qa_launch_instance *selected = provider->launch && snapshots[i]
            ? qa_launch_snapshot_find(snapshots[i], provider->launch->selection.instance) : NULL;
        if (selected && selected->state == provider && selected->storage == provider->launch->storage)
            return snapshots[i];
    }
    return NULL;
}

static bool physical_source(application_provider *provider, qa_console *console, qa_cvars *cvars,
    const qa_command_context *command, qa_application_startup_source *out, qa_error *error)
{
    for (size_t i = 0;; ++i) {
        bool found;
        qa_application_startup_source source;
        if (!application_provider_startup_source_at(provider, i, &source, &found, error)) return false;
        if (!found) break;
        if (source.console == console && source.cvars == cvars) {
            if (command) source.command = *command;
            *out = source;
            return true;
        }
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Source lifecycle lost its physical configuration tuple");
}

static bool qualify_source(application_provider *provider, const qa_launch_snapshot *snapshot,
    const qa_application_startup_source *source, qa_application_startup_source *out, qa_error *error)
{
    const qa_launch_instance *selected = source && source->descriptor && snapshot
        ? qa_launch_snapshot_find(snapshot, source->descriptor->selection.instance) : NULL;
    if (!provider || !source || !selected || selected->state != provider ||
        selected->storage != source->descriptor->storage || source->scope.provider != provider->owner ||
        source->scope.kind == QA_APPLICATION_CONSOLE_ENGINE || !source->console || !source->cvars ||
        source->command.owner != provider->owner || !source->declaration_owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source lifecycle lost its retained receiver authority");
    qa_application_startup_source physical;
    if (!physical_source(provider, source->console, source->cvars, NULL, &physical, error)) return false;
    if (physical.descriptor->storage != source->descriptor->storage ||
        physical.scope.kind != source->scope.kind || physical.scope.seat != source->scope.seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source lifecycle selected another physical scope");
    *out = *source; out->descriptor = selected;
    return true;
}

qa_command_result application_startup_common_command(application_provider *provider,
    qa_console *console, qa_cvars *cvars, const qa_command_invocation *invocation, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || !hooks->source_common_command) return QA_COMMAND_UNHANDLED;
    qa_application_startup_source source, qualified;
    if (!physical_source(provider, console, cvars, &invocation->context, &source, error) ||
        !qualify_source(provider, source_snapshot(provider), &source, &qualified, error))
        return QA_COMMAND_FAILED;
    bool handled = false;
    if (!hooks->source_common_command(hooks->context, provider->application,
            &qualified, invocation, &handled, error)) return QA_COMMAND_FAILED;
    return handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

bool qa_application_startup_source_engine_cvars(const qa_application *app,
    const qa_application_startup_source *source, qa_cvars **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Source ENGINE namespace requires its output");
    *out = NULL;
    if (!app || !source || !source->descriptor || !source->descriptor->storage ||
        !source->console || !source->cvars || !source->scope.provider ||
        source->scope.kind == QA_APPLICATION_CONSOLE_ENGINE ||
        source->command.owner != source->scope.provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source ENGINE namespace requires its physical receiver tuple");
    for (application_provider *provider = app->live_providers; provider; provider = provider->next_live) {
        if (provider->application != app || !provider->launch ||
            provider->launch->storage != source->descriptor->storage || provider->owner != source->scope.provider)
            continue;
        if (!app->cvars && app->engine_shutdown_provider != provider)
            return application_fail(error, QA_ERROR_ARGUMENT, "Detached ENGINE namespace has no entered source shutdown loan");
        qa_application_startup_source actual;
        if (!physical_source(provider, source->console, source->cvars, NULL, &actual, error)) return false;
        if (actual.descriptor->storage != source->descriptor->storage ||
            actual.scope.kind != source->scope.kind || actual.scope.seat != source->scope.seat ||
            actual.command.owner != source->command.owner || actual.command.dialect != source->command.dialect)
            return application_fail(error, QA_ERROR_ARGUMENT, "Source ENGINE namespace selected another physical scope");
        *out = application_engine_shutdown_cvars(provider);
        return *out != NULL || application_fail(error, QA_ERROR_ARGUMENT,
            "Source ENGINE namespace lost its canonical registry owner");
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Source ENGINE namespace lost its retained provider");
}

bool qa_application_startup_console_primary(qa_application *app, qa_console *console,
    bool *primary, qa_error *error)
{
    if (!app || !console || !primary || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup consumption requires its live physical console");
    *primary = false;
    const qa_launch_snapshot *snapshot = qa_application_launch(app);
    if (!snapshot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup consumption has no published source roster");
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (!provider || !provider->constructed || !provider->attached || provider->close_pending) continue;
        for (size_t index = 0;; ++index) {
            qa_application_startup_source source, qualified;
            bool found;
            if (!application_provider_startup_source_at(provider, index, &source, &found, error)) return false;
            if (!found) break;
            if (source.console != console) continue;
            if (!qualify_source(provider, snapshot, &source, &qualified, error)) return false;
            const qa_application_startup_hooks *hooks = app->startup_hooks;
            if (hooks && hooks->startup_source)
                return hooks->startup_source(hooks->context, app, snapshot, &qualified, primary, error);
            const qa_launch_binding *binding = qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
                (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
            *primary = binding && !strcmp(binding->instance, qualified.descriptor->selection.instance) &&
                qualified.scope.kind != QA_APPLICATION_CONSOLE_Q3_CGAME &&
                qualified.scope.kind != QA_APPLICATION_CONSOLE_Q3_UI;
            return true;
        }
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Startup consumption lost its published physical source");
}

bool application_startup_source_carry(application_provider *provider,
    const qa_application_startup_source *source, bool *carried, qa_error *error)
{
    if (!provider || !source || !carried)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source carry requires its actual physical configuration tuple");
    *carried = false;
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || !hooks->carry_source_variables || provider->application->operation == APPLICATION_PERSISTING)
        return true;
    const qa_launch_snapshot *snapshot = source_snapshot(provider);
    if (source->cvars == provider->application->cvars || provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source carry lost its detached candidate registry authority");
    qa_application_startup_source qualified;
    if (!qualify_source(provider, snapshot, source, &qualified, error)) return false;
    return hooks->carry_source_variables(hooks->context, provider->application,
        snapshot, &qualified, carried, error);
}

bool application_startup_source_configuration(application_provider *provider, qa_console *console,
    qa_cvars *cvars, qa_settings_store *out, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || !hooks->configuration_store || !console || !cvars || !out)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Source has no retained configuration store authority");
    const qa_launch_snapshot *snapshot = qa_configuration_current(provider->application->configuration);
    const qa_launch_instance *selected = provider->launch && snapshot
        ? qa_launch_snapshot_find(snapshot, provider->launch->selection.instance) : NULL;
    if (!selected || selected->state != provider || selected->storage != provider->launch->storage)
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign configuration lost its published source descriptor");
    qa_application_startup_source source = {.descriptor = selected,
        .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q3_GAME},
        .console = console, .cvars = cvars, .declaration_owner = provider->owner,
        .command = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER}};
    if (!provider->product || provider->product->family != QA_GAME_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Campaign configuration requires its actual Q3 GAME source");
    return hooks->configuration_store(hooks->context, provider->application, &source, out, error);
}

bool application_startup_publication_prepare(qa_application *app,
    application_publication *publication, qa_error *error)
{
    if (!app || !publication || !publication->candidate || publication->published ||
        publication->failed_retained || app->startup_flow || app->startup_publication ||
        app->startup_preinit_provider || app->operation != APPLICATION_CONFIGURING ||
        publication->previous != qa_configuration_current(app->configuration))
        return application_fail(error, QA_ERROR_ARGUMENT, "Configuration preflight requires its real detached publication ticket");
    const qa_application_startup_hooks *hooks = app->startup_hooks;
    if (!hooks) return true;
    if (!hooks->prepare_candidate || !hooks->finish_candidate)
        return application_fail(error, QA_ERROR_ARGUMENT, "Configuration publication requires its actual preflight and outcome hooks");
    for (size_t i = 0; i < publication->next_count; ++i) {
        application_provider *provider = publication->next[i];
        const qa_launch_instance *instance = provider && provider->launch
            ? qa_launch_snapshot_find(publication->candidate, provider->launch->selection.instance) : NULL;
        if (!provider || provider->application != app || !provider->constructed ||
            provider->close_pending || !instance || instance->state != provider)
            return application_fail(error, QA_ERROR_ARGUMENT, "Configuration preflight lost a constructed candidate source");
    }
    const qa_launch_snapshot *routing = app->routing_snapshot;
    application_provider **providers = app->routing_providers;
    size_t count = app->routing_provider_count;
    app->startup_publication = publication;
    app->routing_snapshot = publication->candidate;
    app->routing_providers = publication->next;
    app->routing_provider_count = publication->next_count;
    bool ok = hooks->prepare_candidate(hooks->context, app, publication->candidate, error);
    app->routing_snapshot = routing;
    app->routing_providers = providers;
    app->routing_provider_count = count;
    app->startup_publication = NULL;
    return ok;
}

void application_startup_publication_finish(qa_application *app,
    const qa_launch_snapshot *candidate, bool published)
{
    const qa_application_startup_hooks *hooks = app ? app->startup_hooks : NULL;
    if (hooks && !app->startup_flow && hooks->finish_candidate)
        hooks->finish_candidate(hooks->context, app, candidate, published);
}

bool application_startup_source_preinit(application_provider *provider, qa_console *console,
    qa_cvars *cvars, const qa_command_context *command, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || provider->application->operation == APPLICATION_PERSISTING) return true;
    qa_application_startup_source source;
    return physical_source(provider, console, cvars, command, &source, error) &&
        application_startup_tuple_preinit(provider, &source, error);
}

bool application_startup_tuple_preinit(application_provider *provider,
    const qa_application_startup_source *source, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks || provider->application->operation == APPLICATION_PERSISTING) return true;
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshot = source_snapshot(provider);
    if (!hooks->preinit_source || provider->attached || provider->close_pending ||
        app->startup_preinit_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source preinitialization lost its real candidate owner");
    qa_application_startup_source qualified;
    if (!qualify_source(provider, snapshot, source, &qualified, error)) return false;
    app->startup_preinit_provider = provider;
    bool ok = qa_application_capture_command_context(app, &source->command, &qualified.command, error) &&
        hooks->preinit_source(hooks->context, app, snapshot, &qualified, error);
    app->startup_preinit_provider = NULL;
    return ok;
}

bool application_startup_source_restore(application_provider *provider, qa_console *console,
    qa_cvars *cvars, const qa_command_context *command, qa_error *error)
{
    if (!hooks_for(provider)) return true;
    qa_application_startup_source source;
    return physical_source(provider, console, cvars, command, &source, error) &&
        application_startup_tuple_restore(provider, &source, error);
}

bool application_startup_tuple_restore(application_provider *provider,
    const qa_application_startup_source *source, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) return true;
    const qa_launch_snapshot *snapshot = source_snapshot(provider);
    qa_application *app = provider->application;
    if (!hooks->restore_source ||
        app->operation != APPLICATION_PERSISTING || provider->close_pending || app->startup_preinit_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source restoration needs its decoded physical owner");
    qa_application_startup_source qualified;
    if (!qualify_source(provider, snapshot, source, &qualified, error)) return false;
    const qa_launch_snapshot *routing = app->routing_snapshot;
    app->routing_snapshot = snapshot;
    app->startup_preinit_provider = provider;
    bool ok = qa_application_capture_command_context(app, &source->command, &qualified.command, error) &&
        hooks->restore_source(hooks->context, app, snapshot, &qualified, error);
    app->startup_preinit_provider = NULL;
    app->routing_snapshot = routing;
    return ok;
}

bool application_startup_source_retire(application_provider *provider, qa_console *console,
    qa_cvars *cvars, qa_error *error)
{
    if (!hooks_for(provider)) return true;
    qa_application_startup_source source;
    return physical_source(provider, console, cvars, NULL, &source, error) &&
        application_startup_tuple_retire(provider, &source, error);
}

bool application_startup_tuple_retire(application_provider *provider,
    const qa_application_startup_source *source, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) return true;
    if (!hooks->retire_source || !source || !source->descriptor || !source->console || !source->cvars ||
        !provider->launch || source->descriptor->storage != provider->launch->storage ||
        source->scope.provider != provider->owner || source->scope.kind == QA_APPLICATION_CONSOLE_ENGINE ||
        provider->attached ||
        provider->component_attached || provider->policy_attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source retirement requires its detached live console");
    return hooks->retire_source(hooks->context, provider->application, source, error);
}

bool application_startup_tuple_retire_client(application_provider *provider,
    const qa_application_startup_source *source, qa_error *error)
{
    if (!provider || !provider->application ||
        !application_guest_q3_client_console_retirement(provider, source) ||
        provider->application->startup_retiring_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement still has actual role or source leases");
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) return true;
    if (!hooks->retire_hosted_configuration)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT retirement has no checked configuration owner");
    qa_application *app = provider->application;
    app->startup_retiring_provider = provider;
    bool ok = hooks->retire_hosted_configuration(hooks->context, app, source, error);
    app->startup_retiring_provider = NULL;
    return ok;
}

bool application_startup_tuple_bind_client(application_provider *provider,
    const qa_launch_snapshot *candidate, const qa_application_startup_source *target,
    const qa_application_startup_source *backing, qa_cvars **out, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !candidate || candidate != qa_configuration_current(app->configuration) ||
        !app->publication_started || app->operation != APPLICATION_CONFIGURING || !out || *out ||
        !application_guest_q3_client_console_retirement(provider, target) || !backing ||
        !backing->descriptor || backing->scope.kind != QA_APPLICATION_CONSOLE_Q3_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT binding lost its entered publication child");
    const qa_launch_instance *receiver = qa_launch_snapshot_find(candidate, target->descriptor->selection.instance);
    const qa_launch_instance *game = qa_launch_snapshot_find(candidate, backing->descriptor->selection.instance);
    application_provider *game_provider = game ? game->state : NULL;
    qa_application_startup_source qualified;
    if (!receiver || receiver->state != provider || receiver->storage != target->descriptor->storage ||
        !game_provider || game_provider->application != app || !game_provider->constructed ||
        game_provider->close_pending || !qualify_source(game_provider, candidate, backing, &qualified, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT binding lost its real new GAME registry");
    const qa_launch_binding *entities = qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    if (!entities || strcmp(entities->instance, game->selection.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT binding selected another GAME publication");
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!hooks) { *out = target->cvars; return true; }
    if (!hooks->bind_hosted_configuration || !hooks->publish_hosted_configuration)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted CLIENT binding has no retained configuration publisher");
    if (!hooks->bind_hosted_configuration(hooks->context, app, candidate, target, &qualified, out, error))
        return false;
    return *out != NULL || application_fail(error, QA_ERROR_ARGUMENT,
        "Hosted CLIENT configuration did not return its actual completed heap");
}

void application_startup_tuple_bound_client(application_provider *provider,
    const qa_application_startup_source *source)
{
    if (!provider || !provider->application ||
        !application_guest_q3_client_console_bound(provider, source)) return;
    qa_application *app = provider->application;
    application_startup_program_bound_client(app, provider, source);
    application_startup_flow_bound_client(app, provider, source);
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (hooks && hooks->publish_hosted_configuration)
        hooks->publish_hosted_configuration(hooks->context, app, source);
}

bool application_startup_source_deconstruct(application_provider *provider, qa_error *error)
{
    if (!provider || !provider->application)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source teardown requires its actual application owner");
    qa_application *app = provider->application;
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (provider->attached || provider->component_attached || provider->policy_attached ||
        app->startup_retiring_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Client lease retirement requires entered detached source teardown");
    bool ok = true;
    app->startup_retiring_provider = provider;
    for (size_t i = 0; hooks && hooks->begin_retire_source; ++i) {
        qa_application_startup_source source;
        bool found;
        if (!application_provider_startup_source_at(provider, i, &source, &found, error)) { ok = false; break; }
        if (!found) break;
        if (!hooks->begin_retire_source(hooks->context, app, &source, error)) { ok = false; break; }
    }
    if (ok) ok = application_startup_flow_release_provider(provider, error);
    app->startup_retiring_provider = NULL;
    return ok;
}

bool qa_application_startup_source_retiring(const qa_application *app, const qa_console *console,
    const qa_command_context *command)
{
    application_provider *provider = app ? app->startup_retiring_provider : NULL;
    if (!provider || provider->application != app || !app->session || !console || !command ||
        !provider->launch || command->owner != provider->owner ||
        command->registry != qa_actors_identity(qa_session_actors(app->session)) ||
        !command->generation || command->generation > app->command_generation)
        return false;
    for (size_t i = 0;; ++i) {
        qa_application_startup_source source;
        bool found;
        qa_error error = {0};
        if (!application_provider_startup_source_at(provider, i, &source, &found, &error) || !found)
            return false;
        if (source.console != console) continue;
        if (!source.descriptor || source.descriptor->storage != provider->launch->storage ||
            source.scope.provider != provider->owner || source.scope.kind == QA_APPLICATION_CONSOLE_ENGINE ||
            source.command.session != command->session || source.command.dialect != command->dialect)
            return false;
        if ((provider->attached || provider->component_attached || provider->policy_attached) &&
            !application_guest_q3_client_console_retirement(provider, &source))
            return false;
        /* A GAME console serves all authored seats. CLIENT consoles are each
         * physically bound to one retained authored seat. */
        return (source.scope.kind != QA_APPLICATION_CONSOLE_Q3_CGAME &&
                source.scope.kind != QA_APPLICATION_CONSOLE_Q3_UI) || source.scope.seat == command->seat;
    }
}

qa_cvars *application_startup_cvar_owner(application_provider *provider, qa_console *console,
    const qa_command_context *command, const char *name)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    return hooks && hooks->cvar_owner
        ? hooks->cvar_owner(hooks->context, provider->application, console, command, name) : NULL;
}

bool application_startup_console_cvar_edit(qa_application *app, qa_console *console,
    const qa_command_context *command, qa_cvars *registry, qa_cvars_edit **out, qa_error *error)
{
    if (!app || !console || !registry || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Prepared cvar access requires its physical application console");
    *out = NULL;
    const qa_application_startup_hooks *hooks = app->startup_hooks;
    if (!hooks || !hooks->cvar_edit) return true;
    bool captured = command && qa_application_command_context_active(app, command);
    bool entered = !captured && qa_console_cvar_entered(console, command);
    if (!captured && !entered)
        return application_fail(error, QA_ERROR_ARGUMENT, "Prepared cvar access lost its actual command or entered lexical operation");
    bool physical = console == app->console;
    for (application_provider *provider = app->live_providers; !physical && provider;
         provider = provider->next_live) {
        if (provider->application != app || provider->owner != command->owner) continue;
        for (size_t index = 0;; ++index) {
            qa_application_startup_source source;
            bool found;
            if (!application_provider_startup_source_at(provider, index, &source, &found, error)) return false;
            if (!found) break;
            if (source.console == console && source.command.dialect == command->dialect &&
                (!entered || (source.descriptor && provider->launch &&
                    source.descriptor->storage == provider->launch->storage &&
                    source.command.owner == command->owner && source.command.session == command->session &&
                    ((source.scope.kind != QA_APPLICATION_CONSOLE_Q3_CGAME &&
                      source.scope.kind != QA_APPLICATION_CONSOLE_Q3_UI) || source.scope.seat == command->seat)))) {
                physical = true;
                break;
            }
        }
    }
    if (!physical)
        return application_fail(error, QA_ERROR_ARGUMENT, "Prepared cvar access selected another physical console");
    return hooks->cvar_edit(hooks->context, app, console, command, registry, out, error);
}

bool application_startup_cvar_edit(application_provider *provider, qa_console *console,
    const qa_command_context *command, qa_cvars *registry, qa_cvars_edit **out, qa_error *error)
{
    if (!provider || !provider->application || !command || command->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Prepared source cvar access lost its actual provider");
    return application_startup_console_cvar_edit(provider->application, console, command, registry, out, error);
}

bool application_startup_visible_cvars(application_provider *provider, qa_console *console,
    const qa_command_context *command, size_t index, qa_cvars **out)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    return hooks && hooks->visible_cvars &&
        hooks->visible_cvars(hooks->context, provider->application, console, command, index, out);
}

bool application_startup_source_scripts(const application_provider *provider)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    return hooks && hooks->read_source_script && hooks->release_source_script;
}

bool application_startup_source_script_read(application_provider *provider, qa_console *console,
    const qa_command_context *command, const char *name, qa_bytes *bytes,
    void **lease, qa_error *error)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (!application_startup_source_scripts(provider) || !console ||
        !qa_application_command_context_active(provider->application, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source script read lost its actual configuration authority");
    return hooks->read_source_script(hooks->context, provider->application, console,
        command, name, bytes, lease, error);
}

void application_startup_source_script_release(application_provider *provider,
    qa_console *console, void *lease)
{
    const qa_application_startup_hooks *hooks = hooks_for(provider);
    if (application_startup_source_scripts(provider))
        hooks->release_source_script(hooks->context, provider->application, console, lease);
}
