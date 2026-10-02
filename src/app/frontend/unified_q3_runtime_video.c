#include "unified_q3_runtime_video.h"
#include "unified_q3_runtime_private.h"
#include "video_guests.h"
#include "capture.h"
#include <stdlib.h>

struct frontend_unified_q3_runtime_video {
    frontend_unified_q3_runtime *owner;
    frontend_unified_q3_runtime_options retained;
    frontend_unified_q3_client_video *client;
    const frontend_video_guests *aggregate;
    uint64_t time_ns,frame_number,generation;
    bool closed,attempted,reopened,aborting;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
bool frontend_unified_q3_runtime_video_current(const frontend_unified_q3_runtime_video *t,qa_error *e)
{
    frontend_unified_q3_runtime *o=t?t->owner:NULL;
    qa_frontend *f=o?o->options.frontend:NULL;
    const frontend_unified_q3_runtime_options *a=o?&o->options:NULL,*b=t?&t->retained:NULL;
    if(!o || o->video!=t || !f || f!=b->frontend || o->retiring || o->restoring || o->prepared ||
        f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f) ||
        !frontend_video_guests_parent_is(f,t->aggregate) || f->time_ns!=t->time_ns ||
        f->frame_number!=t->frame_number || o->video_generation!=t->generation ||
        a->replica!=b->replica || a->client!=b->client || a->media!=b->media || a->clients!=b->clients ||
        a->context!=b->context || a->current!=b->current || !a->current(a->context,a) ||
        !frontend_remote_unified_current(a->replica,e) || !frontend_unified_q3_runtime_idle(o) ||
        !frontend_unified_q3_client_video_current(t->client))
        return fail(e,"Unified CG video lost its actual retained Source, CLIENT or returned frame");
    return true;
}
const frontend_unified_q3_client_video *frontend_unified_q3_runtime_video_client(
    const frontend_unified_q3_runtime_video *t)
{ return t && t->owner && t->owner->video==t && frontend_unified_q3_client_video_current(t->client)?t->client:NULL; }
static bool closed(const void *context)
{
    const frontend_unified_q3_runtime *o=context;
    const frontend_unified_q3_runtime_owners *c=o?&o->children:NULL;
    return o && o->video && !o->complete && !o->busy && !o->entered && !o->command && !o->rebind &&
        !c->presentation && !c->weapons && !c->events && !c->particles && !c->view && !c->player_state &&
        !c->hud && !c->commands && !c->loading && !c->mission && !c->snapshots;
}
bool frontend_unified_q3_runtime_video_returned(const frontend_unified_q3_runtime *o,
    const frontend_video_guests *aggregate,qa_error *e)
{
    const frontend_unified_q3_runtime_video *t=o?o->video:NULL;
    return t && t->aggregate==aggregate && t->closed && !t->attempted && !t->reopened && closed(o) &&
        frontend_unified_q3_runtime_video_current(t,e);
}
bool frontend_unified_q3_runtime_video_close(frontend_unified_q3_runtime_video *t,qa_error *e)
{
    if(!frontend_unified_q3_runtime_video_current(t,e))return false;
    frontend_unified_q3_runtime *o=t->owner;
    if(!frontend_unified_q3_runtime_close_children(o,e))return false;
    q3n_compiled_source_view source;
    if(!q3n_compiled_source_read(frontend_unified_q3_client_source(o->options.client),&source,e) ||
        !q3n_clients_compiled_reset(o->options.clients,&source,e) ||
        !q3n_media_compiled_reset(o->options.media,&source,e) ||
        !q3n_compiled_source_current(&source) ||
        !frontend_unified_q3_client_video_begin(t->client,o,closed,e))return false;
    o->settings=(q3n_native_frame_options){0}; o->refdef=(qa_q3_refdef){0}; o->view_angles=qa_v3(0,0,0);
    o->old_time=0; o->frame_milliseconds=0; o->client_frame=0; o->stereo=0;
    o->initialized=false; o->faulted=false; o->prediction_prepared=false;
    o->information_prepared=false; o->rendered=false; o->video_constructor=true;
    t->closed=true; t->attempted=false; t->reopened=false;
    return frontend_unified_q3_runtime_video_current(t,e);
}
bool frontend_unified_q3_runtime_video_prepare(frontend_unified_q3_runtime *o,
    frontend_unified_q3_runtime_video **out,qa_error *e)
{
    qa_frontend *f=o?o->options.frontend:NULL;
    const frontend_video_guests *aggregate=frontend_video_guests_read(f);
    if(!out || *out || !o || o->video || !aggregate || !o->initialized || o->prepared ||
        o->video_generation==UINT64_MAX || !frontend_unified_q3_runtime_current(o) ||
        !frontend_unified_q3_runtime_idle(o) || !frontend_seat_callbacks_returned(f))
        return fail(e,"Unified CG video preparation requires its actual initialized returned runtime");
    frontend_unified_q3_runtime_video *t=calloc(1,sizeof(*t));
    if(!t)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual Unified CG video reconstruction");
    t->owner=o; t->retained=o->options; t->aggregate=aggregate;
    t->time_ns=f->time_ns; t->frame_number=f->frame_number; t->generation=o->video_generation;
    if(!frontend_unified_q3_client_video_prepare(o->options.client,f,aggregate,&t->client,e)) { free(t); return false; }
    o->video=t; *out=t;
    return frontend_unified_q3_runtime_video_close(t,e);
}
bool frontend_unified_q3_runtime_video_reopen(frontend_unified_q3_runtime_video *t,qa_error *e)
{
    if(!frontend_unified_q3_runtime_video_current(t,e))return false;
    if(t->reopened)return true;
    if((!t->closed || t->attempted) && !frontend_unified_q3_runtime_video_close(t,e))return false;
    frontend_unified_q3_runtime *o=t->owner;
    if(o->video_generation==UINT64_MAX)return fail(e,"Unified CG video generation is exhausted");
    t->attempted=true;
    if(!frontend_unified_q3_runtime_build_children(o,e) ||
        !frontend_unified_q3_runtime_initialize_video(o,e))return false;
    ++o->video_generation; t->generation=o->video_generation; t->reopened=true;
    return frontend_unified_q3_runtime_video_current(t,e);
}
bool frontend_unified_q3_runtime_video_finish(frontend_unified_q3_runtime_video **out,qa_error *e)
{
    if(!out || !*out)return true;
    frontend_unified_q3_runtime_video *t=*out;
    if(!frontend_unified_q3_runtime_video_current(t,e) || !t->reopened || !t->owner->initialized ||
        !t->owner->complete || !frontend_unified_q3_client_video_finish(&t->client,e))return false;
    t->owner->video=NULL; free(t); *out=NULL; return true;
}
bool frontend_unified_q3_runtime_video_abort(frontend_unified_q3_runtime_video **out,qa_error *e)
{
    if(!out || !*out)return true;
    frontend_unified_q3_runtime_video *t=*out;
    if(!frontend_unified_q3_runtime_video_current(t,e))return false;
    if(!t->aborting) { t->aborting=true; t->closed=false; t->reopened=false; }
    return frontend_unified_q3_runtime_video_reopen(t,e) && frontend_unified_q3_runtime_video_finish(out,e);
}
