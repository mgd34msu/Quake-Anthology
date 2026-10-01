#include "engine_shutdown.h"
#include "rankings.h"
#include "qa/console_cvar_observer.h"
#include <stdlib.h>

struct qa_application_engine_shutdown {
    qa_application *application;
    qa_console *console;
    qa_cvars *cvars;
    qa_session *session;
    uint64_t registry, generation;
};

static bool retained(const qa_application_engine_shutdown *loan)
{
    const qa_application *app = loan ? loan->application : NULL;
    return app && app->engine_shutdown == loan && !app->console && !app->cvars &&
        app->session == loan->session && !app->destroy_requested && !app->finalizing;
}

bool qa_application_engine_shutdown_begin(qa_application *app,
    qa_application_engine_shutdown **out, qa_error *error)
{
    if (!app || !out || (*out && *out != app->engine_shutdown))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown requires its actual retained application");
    if (app->engine_shutdown) {
        if (!retained(app->engine_shutdown))
            return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown lost its held physical slots");
        *out = app->engine_shutdown;
        return true;
    }
    if (app->operation != APPLICATION_IDLE || app->q3_round_active || app->frame_preparing ||
        app->startup_flow || app->failed_publications || app->q1_original_save ||
        app->publication_started || app->destroy_requested || app->finalizing ||
        app->engine_shutdown_provider || !app->console || !app->cvars ||
        !qa_console_idle(app->console) || !qa_cvars_observer_idle(app->cvars) ||
        !application_guests_idle(app) || !application_rankings_idle(app) ||
        !application_bots_can_destroy(app) || !qa_inventory_idle(app->inventory) ||
        (app->pickups && !qa_pickups_idle(app->pickups)) ||
        (app->combat && !qa_combat_idle(app->combat)) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->world && !qa_world_idle(app->world)) ||
        (app->session && !qa_session_safe(app->session)))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE detach requires returned application callbacks");
    qa_application_engine_shutdown *loan = calloc(1, sizeof(*loan));
    if (!loan) return application_fail(error, QA_ERROR_MEMORY, "Retaining the actual ENGINE shutdown owner");
    *loan = (qa_application_engine_shutdown){.application = app, .console = app->console,
        .cvars = app->cvars, .session = app->session, .generation = app->command_generation,
        .registry = app->session ? qa_actors_identity(qa_session_actors(app->session)) : 0};
    app->engine_shutdown = loan;
    app->console = NULL;
    app->cvars = NULL;
    app->state = QA_APPLICATION_STOPPING;
    *out = loan;
    return true;
}

bool qa_application_engine_shutdown_read(const qa_application_engine_shutdown *loan,
    qa_console **console, qa_cvars **cvars, qa_error *error)
{
    if (!retained(loan) || !console || !cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown read lost its physical owner");
    *console = loan->console;
    *cvars = loan->cvars;
    return true;
}

bool qa_application_engine_shutdown_retiring(const qa_application *app,
    const qa_console *console, const qa_command_context *command)
{
    const qa_application_engine_shutdown *loan = app ? app->engine_shutdown : NULL;
    return retained(loan) && console == loan->console && command && !command->owner &&
        loan->registry && command->registry == loan->registry && command->generation &&
        command->generation <= loan->generation;
}

qa_application *qa_application_engine_shutdown_owner(const qa_application_engine_shutdown *loan)
{
    return retained(loan) ? loan->application : NULL;
}

qa_cvars *application_engine_shutdown_cvars(const application_provider *provider)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app) return NULL;
    if (app->cvars) return app->cvars;
    return retained(app->engine_shutdown) && app->engine_shutdown_provider == provider &&
        !provider->attached && !provider->component_attached && !provider->policy_attached
        ? app->engine_shutdown->cvars : NULL;
}

bool qa_application_engine_shutdown_finish(qa_application *app,
    qa_application_engine_shutdown **slot, qa_error *error)
{
    if (!app || !slot || !*slot || *slot != app->engine_shutdown || !retained(*slot))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown completion lost its actual loan");
    qa_application_engine_shutdown *loan = *slot;
    if (app->operation != APPLICATION_IDLE || app->q3_round_active || app->frame_preparing ||
        app->configuration || app->provider_states || app->live_providers || app->pending_close ||
        app->startup_flow || app->failed_publications || app->engine_shutdown_provider ||
        !qa_console_destroy_ready(loan->console) || !qa_cvars_observer_idle(loan->cvars) ||
        !application_guests_idle(app) || !application_rankings_idle(app) ||
        !application_bots_can_destroy(app) || !qa_inventory_idle(app->inventory) ||
        (app->pickups && !qa_pickups_idle(app->pickups)) ||
        (app->combat && !qa_combat_idle(app->combat)) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->session && !qa_session_safe(app->session)) || (app->world && !qa_world_idle(app->world)))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown retains source, input or command borrowers");
    qa_console_destroy(loan->console);
    qa_cvars_destroy(loan->cvars);
    app->engine_shutdown = NULL;
    *slot = NULL;
    free(loan);
    return true;
}
