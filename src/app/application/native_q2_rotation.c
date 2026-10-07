#include "internal.h"
#include "qa/application_q2_rotation.h"
#include "qa/console_cvars_prepare.h"
#include "startup_flow.h"

static application_provider *rotation_source(qa_application *app, qa_actor_owner owner)
{
    for (application_provider *provider = app ? app->live_providers : NULL; provider; provider = provider->next_live)
        if (provider->owner == owner && provider->constructed && provider->attached && !provider->close_pending &&
            provider->launch && provider->product && provider->product->family == QA_GAME_Q2 &&
            (provider->kind == APPLICATION_PROVIDER_Q2 ||
                (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine))) return provider;
    return NULL;
}
bool qa_application_q2_rotation_read(qa_application *app, qa_actor_owner owner,
    qa_application_q2_rotation_view *out, qa_error *error)
{
    application_provider *provider = rotation_source(app, owner);
    qa_application_q2_rotation_view view = {.application = app, .owner = owner};
    if (!provider || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation requires its actual constructed Q2 Source console");
    qa_application_startup_source source;
    bool present;
    if (!application_provider_startup_source_at(provider, 0, &source, &present, error)) return false;
    if (!present || source.scope.provider != owner ||
        (source.scope.kind != QA_APPLICATION_CONSOLE_Q2_GAME && source.scope.kind != QA_APPLICATION_CONSOLE_NATIVE_Q2))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation requires its actual constructed Q2 Source console");
    if (!qa_application_capture_command_context(app, &source.command, &view.command, error)) return false;
    view.console = source.console; view.cvars = source.cvars; view.scope = source.scope;
    qa_console_dialect dialect = qa_cvars_dialect(view.cvars);
    if (dialect != QA_CONSOLE_Q2 && dialect != QA_CONSOLE_Q2_RERELEASE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation Source has no actual Q2 edition");
    view.rerelease = dialect == QA_CONSOLE_Q2_RERELEASE;
    const char *name = view.rerelease ? "g_map_list" : "sv_maplist";
    qa_cvars *actual = NULL; qa_cvars_edit *edit = NULL; const qa_cvar_view *desired = NULL;
    if (!qa_console_cvar_access(view.console, &view.command, name, &actual, &edit, error) || actual != view.cvars ||
        !qa_console_cvar_read(view.console, &view.command, name, &desired, error) || !desired)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation has no genuine registered Source map-list row");
    const qa_cvar_view *effective = qa_cvars_find(view.cvars, name);
    if (!effective) return false;
    view.descriptor = provider->launch; view.prepared = edit != NULL;
    view.desired_maps = desired->latched_value ? desired->latched_value : desired->value;
    view.latched_maps = desired->latched_value; view.effective_maps = effective->value;
    view.maps_revision = desired->modification_count;
    if (view.rerelease) {
        const qa_cvar_view *shuffle = NULL;
        if (!qa_console_cvar_access(view.console, &view.command, "g_map_list_shuffle", &actual, &edit, error) ||
            actual != view.cvars || !qa_console_cvar_read(view.console, &view.command, "g_map_list_shuffle", &shuffle, error) || !shuffle)
            return application_fail(error, QA_ERROR_ARGUMENT, "Rotation shuffle has no genuine Source row");
        effective = qa_cvars_find(view.cvars, "g_map_list_shuffle");
        if (!effective) return false;
        view.desired_shuffle = shuffle->integer != 0; view.effective_shuffle = effective->integer != 0;
        view.shuffle_revision = shuffle->modification_count;
    }
    view.configuration_generation = qa_application_configuration_generation(app);
    *out = view; return true;
}
bool qa_application_q2_rotation_current(const qa_application_q2_rotation_view *view)
{
    qa_application_q2_rotation_view actual;
    return view && qa_application_q2_rotation_read((qa_application *)view->application, view->owner, &actual, NULL) &&
        view->descriptor == actual.descriptor && view->console == actual.console && view->cvars == actual.cvars &&
        view->command.session == actual.command.session && view->command.cvar_view == actual.command.cvar_view &&
        view->scope.provider == actual.scope.provider && view->scope.kind == actual.scope.kind &&
        view->scope.seat == actual.scope.seat && view->configuration_generation == actual.configuration_generation &&
        view->rerelease == actual.rerelease && view->prepared == actual.prepared &&
        view->maps_revision == actual.maps_revision && view->shuffle_revision == actual.shuffle_revision;
}
static bool apply(qa_application *app, const qa_application_q2_rotation_view *view,
    const char *name, const char *value, qa_error *error)
{
    if (!app || !view || view->application != app || !value || !qa_application_q2_rotation_current(view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation edit lost its genuine current Source");
    qa_command_context command;
    if (!qa_application_capture_command_context(app, &view->command, &command, error)) return false;
    qa_cvars_edit_command edit = {.kind = QA_CVARS_EDIT_SET, .name = name, .value = value, .owner = view->owner};
    return qa_console_cvar_apply(view->console, &command, &edit, error);
}
bool qa_application_q2_rotation_maps(qa_application *app, const qa_application_q2_rotation_view *view,
    const char *maps, qa_error *error)
{ return view && apply(app, view, view->rerelease ? "g_map_list" : "sv_maplist", maps, error); }
bool qa_application_q2_rotation_shuffle(qa_application *app, const qa_application_q2_rotation_view *view,
    bool enabled, qa_error *error)
{
    if (!view || !view->rerelease)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rotation shuffle requires its genuine rerelease Source");
    return apply(app, view, "g_map_list_shuffle", enabled ? "1" : "0", error);
}
