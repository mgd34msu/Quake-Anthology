#include "native_q3_client_modules_private.h"
#include "qa/application_native_q3_client_modules_video.h"
#include <stdlib.h>

struct qa_application_native_q3_client_modules_video {
    application_native_q3_client_modules *owner;
    qa_resource *ui_artifact, *cgame_artifact;
    bool closed, reopened;
};

bool qa_application_native_q3_client_modules_video_current(
    const qa_application_native_q3_client_modules_video *ticket, qa_error *error)
{
    application_native_q3_client_modules *owner = ticket ? ticket->owner : NULL;
    qa_application_q3_remote_source source;
    if (!owner || owner->video != ticket || owner->video_entering || owner->retiring ||
        owner->restore_pending || owner->app->destroy_requested ||
        owner->app->operation == APPLICATION_PERSISTING ||
        !native_client_modules_executors_idle(owner) ||
        owner->ui.artifact.resource != ticket->ui_artifact ||
        owner->cgame.artifact.resource != ticket->cgame_artifact)
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT video ticket lost its retained physical modules");
    return native_client_modules_physical(owner, &source, error) &&
        owner->options.current(owner->options.context, &source, owner->options.gamestate, error);
}

static bool close_executors(qa_application_native_q3_client_modules_video *ticket, qa_error *error)
{
    application_native_q3_client_modules *owner = ticket->owner;
    ticket->closed = false; ticket->reopened = false;
    owner->video_entering = true;
    /* CL_Vid_Restart shuts down UI before CGAME. Every failed stage remains
     * reachable through the same actual module and frontend lease. */
    bool okay = native_client_module_close_executor(&owner->ui, error) &&
        native_client_module_close_executor(&owner->cgame, error);
    owner->video_entering = false;
    if (okay) ticket->closed = true;
    return okay;
}

bool qa_application_native_q3_client_modules_video_close(qa_application_native_q3_client_modules_video *ticket,
    qa_error *error)
{
    return qa_application_native_q3_client_modules_video_current(ticket, error) && close_executors(ticket, error);
}

bool qa_application_native_q3_client_modules_video_media_ready(
    const qa_application_native_q3_client_modules_video *ticket, qa_error *error)
{
    if (!qa_application_native_q3_client_modules_video_current(ticket,error)) return false;
    const application_native_q3_client_modules *owner=ticket->owner;
    const native_client_module *roles[]={&owner->ui,&owner->cgame};
    if (!ticket->closed || ticket->reopened)
        return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT media replacement requires completed executor closure");
    for (size_t i=0;i<2;++i) {
        const native_client_module *role=roles[i];
        if (role->ready || role->initialized || role->host || role->vm || role->native || role->body || role->equipment ||
            role->process.platform || role->process.resources)
            return application_fail(error,QA_ERROR_ARGUMENT,"CLIENT media replacement retains an actual executor child");
    }
    return true;
}

bool qa_application_native_q3_client_modules_video_prepare(application_native_q3_client_modules *owner,
    qa_application_native_q3_client_modules_video **out, qa_error *error)
{
    qa_application_q3_remote_source source;
    if (!owner || !out || *out || owner->video || !owner->prepared ||
        !owner->ui.init_succeeded || !owner->ui.initialized ||
        (owner->cgame.ready && (!owner->cgame.init_succeeded || !owner->cgame.initialized)) ||
        !qa_application_native_q3_client_modules_idle(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT video preparation requires completed actual modules");
    if (!native_client_modules_physical(owner, &source, error) ||
        !owner->options.current(owner->options.context, &source, owner->options.gamestate, error)) return false;
    if (!qa_application_native_q3_client_modules_current(owner, &source))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT video preparation lost its current physical source");
    qa_application_native_q3_client_modules_video *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return application_fail(error, QA_ERROR_MEMORY, "Retaining CLIENT video reconstruction");
    ticket->owner = owner; ticket->ui_artifact = owner->ui.artifact.resource;
    ticket->cgame_artifact = owner->cgame.artifact.resource;
    owner->video = ticket; owner->prepared = false; *out = ticket;
    return close_executors(ticket, error) &&
        qa_application_native_q3_client_modules_video_current(ticket, error);
}

bool qa_application_native_q3_client_modules_video_reopen(qa_application_native_q3_client_modules_video *ticket,
    const qa_application_q3_remote_init *request, bool connecting, qa_error *error)
{
    if (!qa_application_native_q3_client_modules_video_current(ticket, error)) return false;
    application_native_q3_client_modules *owner = ticket->owner;
    if ((owner->options.gamestate != NULL) != (request != NULL))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT video Init requires its real connection phase");
    if (ticket->reopened) return true;
    if (!ticket->closed && !close_executors(ticket, error)) return false;
    ticket->closed = false;
    owner->video_entering = true;
    bool okay = native_client_module_construct(&owner->ui, false, error) &&
        (!ticket->cgame_artifact || native_client_module_construct(&owner->cgame, false, error));
    if (okay) okay = request ? qa_application_native_q3_client_modules_initialize(owner, request, error) :
        qa_application_native_q3_client_modules_initialize_ui(owner, connecting, error);
    owner->video_entering = false;
    if (okay) okay = qa_application_native_q3_client_modules_video_current(ticket, error);
    if (okay) { ticket->reopened = true; owner->prepared = true; }
    return okay;
}

bool qa_application_native_q3_client_modules_video_finish(qa_application_native_q3_client_modules_video **pointer,
    qa_error *error)
{
    if (!pointer || !*pointer) return true;
    qa_application_native_q3_client_modules_video *ticket = *pointer;
    if (!qa_application_native_q3_client_modules_video_current(ticket, error)) return false;
    application_native_q3_client_modules *owner = ticket->owner;
    if (!ticket->reopened || !owner->prepared || !owner->ui.init_succeeded ||
        (ticket->cgame_artifact && !owner->cgame.init_succeeded))
        return application_fail(error, QA_ERROR_ARGUMENT, "CLIENT video completion requires genuine successful Init");
    owner->video = NULL; free(ticket); *pointer = NULL; return true;
}

bool qa_application_native_q3_client_modules_video_abort(qa_application_native_q3_client_modules_video **pointer,
    const qa_application_q3_remote_init *request, bool connecting, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    return qa_application_native_q3_client_modules_video_reopen(*pointer, request, connecting, error) &&
        qa_application_native_q3_client_modules_video_finish(pointer, error);
}
