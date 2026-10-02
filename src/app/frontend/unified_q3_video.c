#include "unified_q3_video.h"
#include "remote_unified_presentation.h"
#include "video_guests.h"
#include "capture.h"
#include <stdlib.h>

typedef struct unified_video_row {
    frontend_unified_presentation_q3_row retained;
    frontend_unified_q3_runtime *runtime;
    frontend_unified_q3_runtime_video *ticket;
    bool reopened;
} unified_video_row;
struct frontend_unified_q3_video {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    const frontend_video_guests *aggregate;
    unified_video_row *rows;
    size_t count;
    uint64_t time_ns,frame_number;
    bool associated,prepared,reopened;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
bool frontend_unified_q3_video_current(const frontend_unified_q3_video *t,qa_error *e)
{
    qa_frontend *f=t?t->frontend:NULL;
    if(!f||!t->associated||!frontend_video_guests_parent_is(f,t->aggregate)||
        f->time_ns!=t->time_ns||f->frame_number!=t->frame_number||
        !frontend_seat_callbacks_returned(f)||
        !frontend_remote_unified_presentation_video_current(t->replica,t,e)||
        frontend_remote_unified_presentation_q3_client_count(t->replica)!=t->count)
        return fail(e,"Unified video lost its actual returned replica and Source roster");
    for(size_t i=0;i<t->count;++i) {
        const unified_video_row *r=t->rows+i;
        if(!r->runtime&&!t->prepared)continue;
        frontend_unified_presentation_q3_row now;
        if(!frontend_remote_unified_presentation_q3_row_read(t->replica,i,&now,e))return false;
        const frontend_unified_presentation_q3_row *old=&r->retained;
        if(now.retired||now.client!=old->client||now.factory!=old->factory||now.media!=old->media||
            now.receiver!=old->receiver||now.audio_owner!=old->audio_owner||now.bank!=old->bank||
            now.source.source!=old->source.source||now.source.revision!=old->source.revision||
            frontend_unified_q3_runtime_factory_runtime(now.factory)!=r->runtime||
            !frontend_unified_q3_runtime_factory_idle(now.factory))
            return fail(e,"Unified video changed its retained physical Source runtime");
        if(r->ticket&&!frontend_unified_q3_runtime_video_current(r->ticket,e))return false;
    }
    return true;
}
bool frontend_unified_q3_video_prepare(qa_frontend *f,frontend_remote_unified *replica,
    frontend_unified_q3_video **out,qa_error *e)
{
    const frontend_video_guests *aggregate=frontend_video_guests_read(f);
    if(!f||!replica||!out||*out||!aggregate||!frontend_seat_callbacks_returned(f))
        return fail(e,"Unified video requires its actual installed physical restart");
    frontend_unified_q3_video *t=calloc(1,sizeof(*t));
    if(!t)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Unified Source CG restart cohort");
    *t=(frontend_unified_q3_video){.frontend=f,.replica=replica,.aggregate=aggregate,
        .time_ns=f->time_ns,.frame_number=f->frame_number};
    if(!frontend_remote_unified_presentation_video_associate(replica,t,e)) { free(t);return false; }
    t->associated=true;
    t->count=frontend_remote_unified_presentation_q3_client_count(replica);
    if(t->count) {
        t->rows=calloc(t->count,sizeof(*t->rows));
        if(!t->rows) {
            qa_error cleanup={0};
            if(frontend_remote_unified_presentation_video_release(replica,t,&cleanup))free(t);
            else { t->count=0;*out=t; }
            return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual Unified CG runtime roster");
        }
    }
    *out=t;
    for(size_t i=0;i<t->count;++i) {
        unified_video_row *r=t->rows+i;
        if(!frontend_remote_unified_presentation_q3_row_read(replica,i,&r->retained,e))return false;
        if(r->retained.retired||!r->retained.factory||
            !(r->runtime=frontend_unified_q3_runtime_factory_runtime(r->retained.factory)))
            return fail(e,"Unified Source has no completed actual CG video owner");
    }
    for(size_t i=0;i<t->count;++i)
        if(!frontend_unified_q3_runtime_video_prepare(t->rows[i].runtime,&t->rows[i].ticket,e))return false;
    t->prepared=true;
    return frontend_unified_q3_video_current(t,e);
}
bool frontend_unified_q3_video_returned(const frontend_unified_q3_video *t,
    const frontend_video_guests *aggregate,qa_error *e)
{
    if(!t||t->aggregate!=aggregate||!t->prepared||t->reopened||!frontend_unified_q3_video_current(t,e))
        return fail(e,"Unified video resource phase lacks its genuine closed CG cohort");
    for(size_t i=0;i<t->count;++i)
        if(!frontend_unified_q3_runtime_video_returned(t->rows[i].runtime,aggregate,e))return false;
    return true;
}
bool frontend_unified_q3_video_reopen(frontend_unified_q3_video *t,qa_error *e)
{
    if(!t||!t->prepared||!frontend_unified_q3_video_current(t,e))return false;
    for(size_t i=0;i<t->count;++i) {
        unified_video_row *r=t->rows+i;
        if(!r->reopened) {
            if(!frontend_unified_q3_runtime_video_reopen(r->ticket,e))return false;
            r->reopened=true;
        }
    }
    t->reopened=true;return frontend_unified_q3_video_current(t,e);
}
bool frontend_unified_q3_video_finish(frontend_unified_q3_video **out,qa_error *e)
{
    frontend_unified_q3_video *t=out?*out:NULL;if(!t)return true;
    if(!t->reopened||!frontend_unified_q3_video_current(t,e))return false;
    for(size_t i=0;i<t->count;++i)
        if(!frontend_unified_q3_runtime_video_finish(&t->rows[i].ticket,e))return false;
    if(!frontend_remote_unified_presentation_video_release(t->replica,t,e))return false;
    free(t->rows);free(t);*out=NULL;return true;
}
bool frontend_unified_q3_video_abort(frontend_unified_q3_video **out,qa_error *e)
{
    frontend_unified_q3_video *t=out?*out:NULL;if(!t)return true;
    if(!frontend_remote_unified_presentation_video_current(t->replica,t,e))return false;
    for(size_t i=t->count;i>0;--i)
        if(!frontend_unified_q3_runtime_video_abort(&t->rows[i-1].ticket,e))return false;
    if(!frontend_remote_unified_presentation_video_release(t->replica,t,e))return false;
    free(t->rows);free(t);*out=NULL;return true;
}
