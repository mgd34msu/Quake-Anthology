#include "remote_q3_modules_private.h"
#include "remote_q3_modules_video.h"
#include "remote_q3_video_media.h"
#include "qa/application_native_q3_client_modules_video.h"
#include <stdlib.h>

struct frontend_remote_q3_modules_video {
    frontend_remote_q3_modules *owner;
    qa_application_native_q3_client_modules_video *modules;
    uint64_t media_generation, reopened_generation;
    bool closed, reopened;
};

static bool media_read(const frontend_remote_q3_modules *owner, uint64_t *generation,
    bool *complete, qa_error *error)
{
    if (owner->kind == REMOTE_MODULE_INITIAL) {
        frontend_remote_q3_initial_view view;
        return frontend_remote_q3_initial_video_read(owner->basis.initial.owner, owner,
            &view, generation, complete, error);
    }
    frontend_remote_q3_resources view;
    return frontend_remote_q3_resources_video_read(owner->basis.decoded.row, owner,
        &view, generation, complete, error);
}

bool frontend_remote_q3_modules_video_current(const frontend_remote_q3_modules_video *ticket, qa_error *error)
{
    frontend_remote_q3_modules *owner = ticket ? ticket->owner : NULL;
    qa_frontend *f = owner ? owner->frontend : NULL;
    if (!owner || !f || owner->video != ticket || !owner->attached || owner->constructing ||
        owner->retiring || owner->restoring || owner->application != f->application ||
        f->capture || f->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video modules lost their actual returned parent");
    for (const remote_module_lease *lease = owner->leases; lease; lease = lease->next)
        if (lease->callbacks || lease->render_definition || !frontend_equipment_source_idle(lease->equipment) ||
            (lease->presentation && !qa_q3_presentation_idle(lease->presentation)))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video modules retain an entered frontend lease");
    uint64_t generation; bool complete;
    if (!media_read(owner, &generation, &complete, error)) return false;
    if (ticket->reopened && (!complete || generation != ticket->reopened_generation))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Reopened remote modules lost their actual media generation");
    return qa_application_native_q3_client_modules_video_current(ticket->modules, error);
}

bool frontend_remote_q3_modules_video_prepare(frontend_remote_q3_modules *owner,
    frontend_remote_q3_modules_video **out, qa_error *error)
{
    if (!owner || !out || *out || owner->video || owner->retiring || owner->restoring ||
        owner->frontend->capture || owner->frontend->resource_inventory ||
        !frontend_remote_q3_modules_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video preparation requires returned actual modules");
    frontend_remote_q3_modules_video *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining remote module video lifetime");
    bool complete;
    if (!media_read(owner, &ticket->media_generation, &complete, error)) { free(ticket); return false; }
    if (!complete) { free(ticket); return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video preparation requires complete actual media"); }
    ticket->owner = owner; owner->video = ticket; *out = ticket;
    bool okay = qa_application_native_q3_client_modules_video_prepare(owner->modules, &ticket->modules, error);
    if (!ticket->modules) {
        owner->video = NULL; free(ticket); *out = NULL; return false;
    }
    if (okay) okay = frontend_remote_modules_released_drain(owner, error);
    if (okay) { ticket->closed = true; okay = frontend_remote_q3_modules_video_current(ticket, error); }
    return okay;
}

bool frontend_remote_q3_modules_video_close(frontend_remote_q3_modules_video *ticket, qa_error *error)
{
    if (!frontend_remote_q3_modules_video_current(ticket, error)) return false;
    if (!ticket->closed) {
        bool complete;
        if (!media_read(ticket->owner, &ticket->media_generation, &complete, error)) return false;
    }
    ticket->closed = false; ticket->reopened = false;
    if (!qa_application_native_q3_client_modules_video_close(ticket->modules, error) ||
        !frontend_remote_modules_released_drain(ticket->owner, error)) return false;
    ticket->closed = true; return true;
}

bool frontend_remote_q3_modules_video_media_ready(const frontend_remote_q3_modules *owner, qa_error *error)
{
    const frontend_remote_q3_modules_video *ticket=owner?owner->video:NULL;
    if (!frontend_remote_q3_modules_video_current(ticket,error)) return false;
    if (!ticket->closed || ticket->reopened || owner->leases)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote media replacement retains an actual frontend host lease");
    return qa_application_native_q3_client_modules_video_media_ready(ticket->modules,error);
}

bool frontend_remote_q3_modules_video_reopen(frontend_remote_q3_modules_video *ticket,
    const qa_application_q3_remote_init *request, bool connecting, qa_error *error)
{
    if (!frontend_remote_q3_modules_video_current(ticket, error)) return false;
    if (ticket->reopened) return true;
    if (!ticket->closed)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video retry must close partial modules before media replacement");
    frontend_remote_q3_modules *owner = ticket->owner;
    uint64_t generation; bool complete;
    if (!media_read(owner, &generation, &complete, error)) return false;
    if (!complete || generation <= ticket->media_generation)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video reopen requires newly rebuilt actual media");
    if ((owner->kind == REMOTE_MODULE_INITIAL) == (request != NULL))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video Init does not match its retained connection phase");
    if (owner->kind == REMOTE_MODULE_INITIAL) {
        frontend_remote_q3_initial_view view;
        if (!frontend_remote_q3_initial_read(owner->basis.initial.owner, &view, error) ||
            !frontend_remote_q3_initial_current(&view)) return false;
        owner->basis.initial.view = view;
    } else {
        frontend_remote_q3_resources view;
        if (!frontend_remote_q3_resources_read(owner->basis.decoded.row, &view, error) ||
            !frontend_remote_q3_resources_current(&view)) return false;
        owner->basis.decoded.view = view;
    }
    ticket->closed = false;
    owner->constructing = true;
    bool okay = qa_application_native_q3_client_modules_video_reopen(ticket->modules, request, connecting, error);
    owner->constructing = false;
    if (okay) okay = frontend_remote_modules_released_drain(owner, error) &&
        frontend_remote_q3_modules_video_current(ticket, error);
    if (okay) { ticket->reopened_generation = generation; ticket->reopened = true;
        okay = frontend_remote_q3_modules_video_current(ticket, error); }
    return okay;
}

bool frontend_remote_q3_modules_video_finish(frontend_remote_q3_modules_video **pointer, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    frontend_remote_q3_modules_video *ticket = *pointer;
    if (!frontend_remote_q3_modules_video_current(ticket, error)) return false;
    if (!ticket->reopened)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Remote video completion requires reconstructed actual hosts");
    if (!frontend_remote_modules_released_drain(ticket->owner, error) ||
        !qa_application_native_q3_client_modules_video_finish(&ticket->modules, error)) return false;
    ticket->owner->video = NULL; free(ticket); *pointer = NULL; return true;
}

bool frontend_remote_q3_modules_video_abort(frontend_remote_q3_modules_video **pointer,
    const qa_application_q3_remote_init *request, bool connecting, qa_error *error)
{
    if (!pointer || !*pointer) return true;
    frontend_remote_q3_modules_video *ticket = *pointer;
    if (!ticket->reopened && !ticket->closed && !frontend_remote_q3_modules_video_close(ticket, error)) return false;
    return frontend_remote_q3_modules_video_reopen(ticket, request, connecting, error) &&
        frontend_remote_q3_modules_video_finish(pointer, error);
}
