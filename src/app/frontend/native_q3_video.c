#include "native_q3_video.h"
#include "native_q3_client_internal.h"
#include "video_guests.h"
#include "capture.h"
#include <stdlib.h>

typedef struct native_video_row {
    frontend_native_q3 *owner;
    frontend_native_q3_view retained;
    qa_native_q3_wire_basis basis;
    bool closed,reopened;
} native_video_row;
struct frontend_native_q3_video {
    qa_frontend *frontend;
    qa_application *application;
    const frontend_video_guests *aggregate;
    native_video_row *rows;
    size_t count;
    uint64_t time_ns,frame_number;
    bool aborting;
};
static frontend_native_q3 *video_row_at(const qa_frontend *f,size_t ordinal)
{
    frontend_native_q3 *row=f?f->native_q3:NULL;
    while(row && ordinal--)row=row->next;
    return row;
}
bool frontend_native_q3_video_read(const qa_frontend *f,size_t ordinal,frontend_native_q3_view *out,qa_error *error)
{
    frontend_native_q3 *row=video_row_at(f,ordinal);
    const frontend_native_q3_video *ticket=row?row->video:NULL;
    qa_native_q3_wire_basis basis;
    if(!out || !ticket || ordinal>=ticket->count || ticket->rows[ordinal].owner!=row ||
        ticket->frontend!=f || f->application!=ticket->application || f->capture || f->source_restoring ||
        !frontend_video_guests_resources_associated(f,ticket->aggregate) ||
        !frontend_seat_callbacks_returned(f) || f->time_ns!=ticket->time_ns || f->frame_number!=ticket->frame_number ||
        !frontend_native_q3_current(row) || row->frame_active || row->callbacks || row->restoring ||
        !qa_native_q3_wire_reader_basis(row->view.reader,&basis,error) ||
        basis.source_game!=ticket->rows[ordinal].basis.source_game ||
        basis.session!=ticket->rows[ordinal].basis.session ||
        basis.map_revision!=ticket->rows[ordinal].basis.map_revision ||
        basis.publication_generation!=ticket->rows[ordinal].basis.publication_generation)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native resource metadata requires its exact closed video Source row");
    *out=row->view; return true;
}
bool frontend_native_q3_video_current(const frontend_native_q3_video *ticket,qa_error *error)
{
    qa_frontend *f=ticket?ticket->frontend:NULL;
    if(!f || f->application!=ticket->application || f->capture || f->resource_inventory ||
        f->source_restoring || !frontend_video_guests_parent_is(f,ticket->aggregate) ||
        !frontend_seat_callbacks_returned(f) || f->time_ns!=ticket->time_ns || f->frame_number!=ticket->frame_number ||
        frontend_native_q3_count(f)!=ticket->count)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native video ticket changed its actual source/frame parents");
    for(size_t i=0;i<ticket->count;++i) {
        const native_video_row *saved=ticket->rows+i;
        frontend_native_q3 *row=saved->owner;
        const frontend_native_q3_view *a=&row->view,*b=&saved->retained;
        qa_native_q3_wire_basis basis;
        if(video_row_at(f,i)!=row || row->video!=ticket || !frontend_native_q3_current(row) ||
            row->frame_active || row->callbacks || row->restoring || a->identity!=b->identity ||
            a->service_owner!=b->service_owner || a->reader!=b->reader || a->client!=b->client ||
            a->registry!=b->registry || a->cvars!=b->cvars || a->input!=b->input ||
            a->source_launch!=b->source_launch || a->source_files!=b->source_files ||
            !qa_native_q3_wire_reader_basis(a->reader,&basis,error) ||
            basis.source_game!=saved->basis.source_game || basis.session!=saved->basis.session ||
            basis.publication_generation!=saved->basis.publication_generation ||
            basis.map_revision!=saved->basis.map_revision ||
            !qa_native_q3_client_service_idle(a->client))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Native video ticket changed its installed full Source client");
    }
    return true;
}
bool frontend_native_q3_video_prepare(qa_frontend *f,frontend_native_q3_video **out,qa_error *error)
{
    const frontend_video_guests *aggregate=frontend_video_guests_read(f);
    if(!f || !f->application || !out || *out || !aggregate || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f) || !frontend_native_q3_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Native video preparation requires returned actual clients");
    frontend_native_q3_video *ticket=calloc(1,sizeof(*ticket));
    if(!ticket)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native CG video reconstruction");
    ticket->frontend=f; ticket->application=f->application;
    ticket->aggregate=aggregate;
    ticket->time_ns=f->time_ns; ticket->frame_number=f->frame_number;
    ticket->count=frontend_native_q3_count(f);
    ticket->rows=calloc(ticket->count,sizeof(*ticket->rows));
    if(ticket->count && !ticket->rows) { free(ticket); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native video roster"); }
    for(size_t i=0;i<ticket->count;++i) {
        native_video_row *saved=ticket->rows+i;
        saved->owner=video_row_at(f,i);
        if(!saved->owner || saved->owner->video || !saved->owner->constructed ||
            !frontend_native_q3_current(saved->owner) || saved->owner->frame_active || saved->owner->callbacks ||
            !qa_native_q3_wire_reader_basis(saved->owner->view.reader,&saved->basis,error)) {
            free(ticket->rows); free(ticket); return false;
        }
        saved->retained=saved->owner->view;
    }
    for(size_t i=0;i<ticket->count;++i)ticket->rows[i].owner->video=ticket;
    *out=ticket;
    for(size_t i=0;i<ticket->count;++i) {
        if(!frontend_native_q3_video_row_close(ticket->rows[i].owner,error))return false;
        ticket->rows[i].closed=true;
    }
    return frontend_native_q3_video_current(ticket,error);
}
bool frontend_native_q3_video_reopen(frontend_native_q3_video *ticket,qa_error *error)
{
    if(!frontend_native_q3_video_current(ticket,error))return false;
    for(size_t i=0;i<ticket->count;++i) {
        native_video_row *saved=ticket->rows+i;
        if(saved->reopened)continue;
        if(!saved->closed) {
            if(!frontend_native_q3_video_row_close(saved->owner,error))return false;
            saved->closed=true;
        }
        if(!frontend_native_q3_video_row_reopen(saved->owner,error))return false;
        saved->reopened=true;
    }
    return frontend_native_q3_video_current(ticket,error);
}
bool frontend_native_q3_video_finish(frontend_native_q3_video **slot,qa_error *error)
{
    if(!slot || !*slot)return true;
    frontend_native_q3_video *ticket=*slot;
    if(!frontend_native_q3_video_current(ticket,error))return false;
    for(size_t i=0;i<ticket->count;++i)
        if(!ticket->rows[i].reopened || !ticket->rows[i].owner->constructed)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Native video completion retains unfinished CG initialization");
    for(size_t i=0;i<ticket->count;++i)ticket->rows[i].owner->video=NULL;
    free(ticket->rows); free(ticket); *slot=NULL; return true;
}
bool frontend_native_q3_video_abort(frontend_native_q3_video **slot,qa_error *error)
{
    if(!slot || !*slot)return true;
    frontend_native_q3_video *ticket=*slot;
    if(!frontend_native_q3_video_current(ticket,error))return false;
    if(!ticket->aborting) {
        ticket->aborting=true;
        for(size_t i=0;i<ticket->count;++i)ticket->rows[i].closed=ticket->rows[i].reopened=false;
    }
    return frontend_native_q3_video_reopen(ticket,error) && frontend_native_q3_video_finish(slot,error);
}
