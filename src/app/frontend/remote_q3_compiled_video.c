#include "remote_q3_compiled_video.h"
#include "source_renderer_runtime.h"
#include "remote_q3_private.h"
#include "remote_q3_services.h"
#include "remote_q3_frame.h"
#include "remote_q3_runtime.h"
#include "video_guests.h"
#include "capture.h"
#include <stdlib.h>

struct frontend_remote_q3_compiled_video {
    frontend_remote_q3 *row;
    qa_frontend *frontend;
    qa_application *application;
    const frontend_video_guests *aggregate;
    frontend_remote_q3_resources resources;
    qa_native_q3_remote_client_service *client;
    q3n_remote_source *source;
    int32_t reached_command,latest_message,server_message;
    uint64_t time_ns,frame_number;
    bool closed,media_ready,rebuild_attempted,reopened,aborting;
};
bool frontend_remote_q3_compiled_video_parent_is(const frontend_remote_q3 *row,
    const frontend_remote_q3_compiled_video *ticket)
{
    return row && ticket && row->compiled_video==ticket && ticket->row==row &&
        row->frontend==ticket->frontend && row->application==ticket->application &&
        frontend_video_guests_parent_is(row->frontend,ticket->aggregate);
}
bool frontend_remote_q3_compiled_video_current(const frontend_remote_q3_compiled_video *ticket,qa_error *e)
{
    frontend_remote_q3 *row=ticket?ticket->row:NULL;
    frontend_remote_q3_resources resources; frontend_remote_q3_services_view services;
    q3n_remote_source_view source;
    if(!frontend_remote_q3_compiled_video_parent_is(row,ticket) ||
        row->frontend->time_ns!=ticket->time_ns || row->frontend->frame_number!=ticket->frame_number ||
        row->users || row->modules || row->transport || row->constructing ||
        !frontend_seat_callbacks_returned(row->frontend) ||
        !frontend_remote_q3_resources_compiled_video_read(row,&resources,e) ||
        !frontend_remote_q3_services_video_read(row,&services,e) ||
        services.client!=ticket->client || services.source!=ticket->source ||
        !frontend_remote_q3_runtime_idle(row->runtime) || !frontend_remote_q3_frame_idle(row->frames) ||
        resources.identity!=ticket->resources.identity || resources.descriptor!=ticket->resources.descriptor ||
        resources.mounts!=ticket->resources.mounts || resources.map!=ticket->resources.map ||
        resources.geometry!=ticket->resources.geometry || resources.registry!=ticket->resources.registry ||
        resources.input!=ticket->resources.input || resources.physical_seat!=ticket->resources.physical_seat ||
        resources.domain.restart_generation!=ticket->resources.domain.restart_generation ||
        !q3n_remote_source_read(ticket->source,&source,e) ||
        source.reached_command!=ticket->reached_command || source.publication.latest_message!=ticket->latest_message ||
        source.publication.server_message!=ticket->server_message)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Compiled video changed its actual Source, map, command or CLIENT parents");
    return true;
}
bool frontend_remote_q3_compiled_video_media_ready(const frontend_remote_q3 *row,qa_error *e)
{
    frontend_remote_q3_services_view services;
    if(!row || !frontend_remote_q3_compiled_video_current(row->compiled_video,e) ||
        !row->compiled_video->closed || row->frames || row->runtime ||
        !frontend_remote_q3_services_video_read(row,&services,e) || services.media || services.clients)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Compiled media replacement requires its genuinely closed CG children");
    return true;
}
bool frontend_remote_q3_compiled_video_close(frontend_remote_q3_compiled_video *ticket,qa_error *e)
{
    if(!frontend_remote_q3_compiled_video_current(ticket,e))return false;
    frontend_remote_q3 *row=ticket->row;
    ticket->closed=false; ticket->media_ready=false; ticket->rebuild_attempted=false; ticket->reopened=false;
    if(!frontend_remote_q3_frame_destroy(&row->frames,e) ||
        !frontend_remote_q3_runtime_destroy(&row->runtime,e) ||
        !frontend_remote_q3_services_video_close(row,e))return false;
    ticket->closed=true;
    return frontend_remote_q3_compiled_video_current(ticket,e);
}
bool frontend_remote_q3_compiled_video_prepare(frontend_remote_q3 *row,
    frontend_remote_q3_compiled_video **out,qa_error *e)
{
    frontend_remote_q3_resources resources; frontend_remote_q3_services_view services;
    q3n_remote_source_view source;
    const frontend_video_guests *aggregate=frontend_video_guests_read(row?row->frontend:NULL);
    if(!row || !out || *out || !aggregate || row->compiled_video || row->modules || row->transport ||
        row->users || row->constructing || row->retiring || row->importing ||
        !frontend_seat_callbacks_returned(row->frontend) ||
        !frontend_remote_q3_resources_read(row,&resources,e) ||
        !frontend_remote_q3_services_read(row,&services,e) ||
        !frontend_remote_q3_services_idle(row->services) ||
        !frontend_remote_q3_runtime_initialized_current(row->runtime) ||
        !q3n_remote_source_read(services.source,&source,e))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Compiled video requires its returned initialized actual CG owner");
    frontend_remote_q3_compiled_video *ticket=calloc(1,sizeof(*ticket));
    if(!ticket)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining compiled CG video reconstruction");
    ticket->row=row; ticket->frontend=row->frontend; ticket->application=row->application;
    ticket->aggregate=aggregate; ticket->resources=resources;
    ticket->client=services.client; ticket->source=services.source;
    ticket->reached_command=source.reached_command; ticket->latest_message=source.publication.latest_message;
    ticket->server_message=source.publication.server_message;
    ticket->time_ns=row->frontend->time_ns; ticket->frame_number=row->frontend->frame_number;
    row->compiled_video=ticket; *out=ticket;
    return frontend_remote_q3_compiled_video_close(ticket,e);
}
bool frontend_remote_q3_compiled_video_reopen(frontend_remote_q3_compiled_video *ticket,qa_error *e)
{
    if(!frontend_remote_q3_compiled_video_current(ticket,e))return false;
    if(ticket->reopened)return true;
    frontend_remote_q3 *row=ticket->row;
    if((!ticket->closed || ticket->rebuild_attempted) &&
        !frontend_remote_q3_compiled_video_close(ticket,e))return false;
    if(!ticket->media_ready) {
        if(!frontend_remote_q3_resources_compiled_video_refresh(row,e))return false;
        ticket->media_ready=true;
    }
    frontend_remote_q3_frame_callbacks callbacks;
    ticket->rebuild_attempted=true;
    if(!frontend_remote_q3_services_video_reopen(row,e) ||
        !frontend_remote_q3_runtime_create(row,&row->runtime,e) ||
        !frontend_remote_q3_runtime_callbacks_read(row->runtime,&callbacks,e) ||
        !frontend_remote_q3_frame_create_video(row,&callbacks,&row->frames,e) ||
        !frontend_remote_q3_runtime_bind_frames(row->runtime,row->frames,e) ||
        !frontend_remote_q3_frame_initialize(row->frames,row->runtime,frontend_remote_q3_runtime_initialize,e) ||
        !frontend_source_renderer_end_registration(row->frontend,e))return false;
    ticket->reopened=true;
    return frontend_remote_q3_compiled_video_current(ticket,e);
}
bool frontend_remote_q3_compiled_video_finish(frontend_remote_q3_compiled_video **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_remote_q3_compiled_video *ticket=*slot;
    if(!frontend_remote_q3_compiled_video_current(ticket,e) || !ticket->reopened ||
        !frontend_remote_q3_runtime_initialized_current(ticket->row->runtime))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Compiled video retains unfinished actual CG initialization");
    ticket->row->compiled_video=NULL; free(ticket); *slot=NULL; return true;
}
bool frontend_remote_q3_compiled_video_abort(frontend_remote_q3_compiled_video **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_remote_q3_compiled_video *ticket=*slot;
    if(!frontend_remote_q3_compiled_video_current(ticket,e))return false;
    if(!ticket->aborting) {
        ticket->aborting=true;
        if(!frontend_remote_q3_compiled_video_close(ticket,e))return false;
    }
    return frontend_remote_q3_compiled_video_reopen(ticket,e) &&
        frontend_remote_q3_compiled_video_finish(slot,e);
}
