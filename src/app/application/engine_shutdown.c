#include "engine_shutdown.h"
#include "rankings.h"
#include "startup_flow.h"
#include "qa/console_cvar_observer.h"
#include "qa/application_client_prepare.h"
#include <stdlib.h>
#include <string.h>

struct qa_application_engine_shutdown {
    qa_application *application;
    qa_console *console;
    qa_cvars *cvars;
    qa_session *session;
    const qa_launch_snapshot *candidate;
    const qa_cvars_edit *values;
    struct application_startup_flow *startup_flow;
    qa_application_client_preparation *client;
    uint64_t registry, generation;
};

static bool retained(const qa_application_engine_shutdown *loan)
{
    const qa_application *app = loan ? loan->application : NULL;
    return app && app->engine_shutdown == loan && !app->console && !app->cvars &&
        app->session == loan->session && !app->destroy_requested && !app->finalizing;
}

static bool callbacks_returned(const qa_application *app)
{
    return app->operation == APPLICATION_IDLE && !app->q3_round_active && !app->frame_preparing &&
        !app->failed_publications && !app->q1_original_save && !app->q2_original_save && !app->publication_started &&
        !app->destroy_requested && !app->finalizing && !app->engine_shutdown_provider &&
        app->console && app->cvars && qa_console_idle(app->console) &&
        application_rankings_idle(app) && application_bots_can_destroy(app) &&
        qa_inventory_idle(app->inventory) && (!app->pickups || qa_pickups_idle(app->pickups)) &&
        (!app->combat || qa_combat_idle(app->combat)) && (!app->modes || qa_modes_idle(app->modes)) &&
        (!app->world || qa_world_idle(app->world)) && (!app->session || qa_session_safe(app->session));
}

static bool detach(qa_application *app, const qa_launch_snapshot *candidate,
    const qa_cvars_edit *values, qa_application_engine_shutdown **out, qa_error *error)
{
    qa_application_engine_shutdown *loan = calloc(1, sizeof(*loan));
    if (!loan) return application_fail(error, QA_ERROR_MEMORY, "Retaining the actual ENGINE shutdown owner");
    *loan = (qa_application_engine_shutdown){.application = app, .console = app->console,
        .cvars = app->cvars, .session = app->session, .generation = app->command_generation,
        .candidate = candidate, .values = values,
        .startup_flow = values ? app->startup_flow : NULL,
        .registry = app->session ? qa_actors_identity(qa_session_actors(app->session)) : 0};
    qa_launch_snapshot_retain(candidate);
    app->engine_shutdown = loan;
    app->console = NULL;
    app->cvars = NULL;
    app->state = QA_APPLICATION_STOPPING;
    *out = loan;
    return true;
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
    if (!callbacks_returned(app) || app->startup_flow || app->client_preparation ||
        !qa_cvars_observer_idle(app->cvars) || !application_guests_idle(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE detach requires returned application callbacks");
    return detach(app, NULL, NULL, out, error);
}

bool qa_application_engine_shutdown_begin_candidate(qa_application *app,
    const qa_launch_snapshot *candidate, const qa_cvars_edit *values,
    qa_application_engine_shutdown **out, qa_error *error)
{
    if (!app || !values || !out || (*out && *out != app->engine_shutdown))
        return application_fail(error, QA_ERROR_ARGUMENT, "Candidate ENGINE shutdown requires its actual retained parents");
    if (app->engine_shutdown) {
        qa_application_engine_shutdown *loan = app->engine_shutdown;
        if (!retained(loan) || loan->candidate != candidate || loan->values != values ||
            !loan->startup_flow || app->startup_flow != loan->startup_flow ||
            qa_application_startup_candidate(app) != candidate)
            return application_fail(error, QA_ERROR_ARGUMENT, "Candidate ENGINE shutdown selected another retained cancellation");
        *out = loan;
        return true;
    }
    if (!callbacks_returned(app) ||
        !application_startup_flow_retirement_ready(app, candidate, values, error))
        return error && error->code != QA_OK ? false :
            application_fail(error, QA_ERROR_ARGUMENT, "Candidate ENGINE shutdown retains entered callbacks");
    return detach(app, candidate, values, out, error);
}

const qa_launch_snapshot *qa_application_engine_shutdown_candidate(const qa_application_engine_shutdown *loan)
{
    return retained(loan) && loan->startup_flow && loan->application->startup_flow == loan->startup_flow &&
        qa_application_startup_candidate(loan->application) == loan->candidate
        ? loan->candidate : NULL;
}
const qa_application_client_preparation *qa_application_engine_shutdown_client(const qa_application_engine_shutdown *loan)
{
    return retained(loan) && loan->client && loan->application->client_preparation==loan->client?
        loan->client:NULL;
}
const qa_cvars_edit *qa_application_engine_shutdown_values(const qa_application_engine_shutdown *loan)
{
    return retained(loan) && loan->values &&
        ((loan->client && loan->application->client_preparation==loan->client) ||
         (loan->startup_flow && loan->application->startup_flow==loan->startup_flow))?loan->values:NULL;
}
bool qa_application_engine_shutdown_begin_client(qa_application *app,
    qa_application_client_preparation *client,const qa_cvars_edit *values,void *context,
    bool (*ready)(void *,const qa_application_client_preparation *,const qa_cvars_edit *),
    qa_application_engine_shutdown **out,qa_error *error)
{
    if (!app || !values || !ready || !out || (*out && *out!=app->engine_shutdown))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT ENGINE detach requires its actual failed settings owner");
    if (app->engine_shutdown) {
        qa_application_engine_shutdown *loan=app->engine_shutdown;
        if (!retained(loan) || loan->client!=client || loan->values!=values || app->client_preparation!=client)
            return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT ENGINE detach selected another retained cancellation");
        *out=loan; return true;
    }
    if (!callbacks_returned(app) || !qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_CLEANUP) ||
        !qa_application_client_prepare_associated(app,client) || !qa_cvars_edit_abort_is(values,app->cvars) ||
        !ready(context,client,values) || !qa_application_client_prepare_associated(app,client))
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT ENGINE detach requires complete retained failed input coverage");
    if (!detach(app,NULL,values,out,error)) return false;
    (*out)->client=client; return true;
}
void application_engine_shutdown_release_client(qa_application *app,const qa_application_client_preparation *client)
{
    qa_application_engine_shutdown *loan=app?app->engine_shutdown:NULL;
    if (!retained(loan) || loan->client!=client || app->client_preparation!=client) return;
    loan->client=NULL; loan->values=NULL;
}

void application_engine_shutdown_release_candidate(qa_application *app,
    const qa_launch_snapshot *candidate)
{
    qa_application_engine_shutdown *loan = app ? app->engine_shutdown : NULL;
    if (!retained(loan) || !loan->startup_flow || app->startup_flow != loan->startup_flow ||
        loan->candidate != candidate) return;
    loan->candidate = NULL;
    loan->values = NULL;
    loan->startup_flow = NULL;
    qa_launch_snapshot_release(candidate);
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
    if (!retained(loan) || !command) return false;
    if (loan->client && app->client_preparation==loan->client &&
        qa_application_client_prepare_entered(loan->client,QA_CLIENT_PREPARE_CLEANUP)) {
        const qa_application_client_source *source=qa_application_client_prepare_source(loan->client);
        const qa_command_context *held=source?&source->context.command:NULL;
        if (source && console==source->context.console && qa_application_client_associated(app,source) &&
            command->origin==QA_COMMAND_SEAT && command->script && !strcmp(command->script,"key-binding") &&
            !command->direct && !command->console_text &&
            command->session==held->session && command->owner==held->owner && command->client==held->client &&
            command->seat==held->seat && command->dialect==held->dialect &&
            command->registry==held->registry && command->generation==held->generation &&
            qa_actor_id_equal(command->actor,held->actor)) return true;
    }
    return console == loan->console && !command->owner &&
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
        app->startup_flow || app->failed_publications || app->engine_shutdown_provider || loan->candidate ||
        loan->startup_flow || loan->client || app->client_preparation || loan->values ||
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
