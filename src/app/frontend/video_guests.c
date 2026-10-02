#include "video_guests.h"
#include "../application/guest_q3_video.h"
#include "../application/guest_q3_components_video.h"
#include "native_q3_client.h"
#include "native_q3_video.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "remote_q3_modules_video.h"
#include "remote_q3_compiled_video.h"
#include "remote_q3_video_media.h"
#include "network_q3_video.h"
#include "unified_q3_video.h"
#include "source_acoustics.h"
#include "shared_resource_policy.h"
#include "source_renderer_runtime.h"
#include "q3_render_policy.h"
#include "qa/material_source_scratch.h"

typedef struct video_remote_row {
    frontend_remote_q3 *decoded;
    frontend_remote_q3_initial *initial;
    frontend_remote_q3_modules *modules;
    frontend_remote_q3_modules_video *ticket;
    frontend_remote_q3_compiled_video *compiled;
    uint64_t media_generation;
    bool media_ready,init_attempted,reopened;
} video_remote_row;
struct frontend_video_guests {
    qa_frontend *frontend;
    qa_application *application;
    application_guest_q3_video *hosted;
    application_q3_components_video *components;
    frontend_native_q3_video *native;
    video_remote_row *remote;
    size_t remote_count;
    frontend_unified_q3_video **unified;
    size_t unified_count;
    frontend_shared_resource_policy *resources;
    qa_display *resource_display;
    qa_cpu_renderer *resource_cpu;
    qa_gl_renderer *resource_gl;
    qa_cvars *resource_registry;
    bool prepared,reopened,resource_phase,resources_prepared,resources_published,resources_finished;
};
bool frontend_video_guests_parent_is(const qa_frontend *f,const frontend_video_guests *owner)
{ return f && owner && f->video_guests==owner && owner->frontend==f && owner->application==f->application; }
const frontend_video_guests *frontend_video_guests_read(const qa_frontend *f)
{ return f && frontend_video_guests_parent_is(f,f->video_guests)?f->video_guests:NULL; }
bool frontend_video_guests_resources_associated(const qa_frontend *f,const frontend_video_guests *owner)
{
    return frontend_video_guests_parent_is(f,owner) && owner->prepared && !owner->reopened &&
        owner->resource_phase && owner->resource_display==f->display && owner->resource_cpu==f->cpu &&
        owner->resource_gl==f->gl && owner->resource_registry==qa_application_cvars(f->application) &&
        !f->capture && !f->source_restoring;
}
bool frontend_video_guests_current(const frontend_video_guests *owner,qa_error *error)
{
    if (!owner || !frontend_video_guests_parent_is(owner->frontend,owner) ||
        owner->frontend->capture || owner->frontend->resource_inventory ||
        owner->frontend->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video guests lost their retained physical application");
    for (size_t i=0;i<owner->remote_count;++i) {
        if (owner->remote[i].ticket &&
            !frontend_remote_q3_modules_video_current(owner->remote[i].ticket,error)) return false;
        if (owner->remote[i].compiled &&
            !frontend_remote_q3_compiled_video_current(owner->remote[i].compiled,error)) return false;
    }
    if(owner->prepared&&frontend_remote_unified_count(owner->frontend)!=owner->unified_count)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video restart changed its actual Unified replica roster");
    for(size_t i=0;i<owner->unified_count;++i)
        if(owner->unified[i]&&!frontend_unified_q3_video_current(owner->unified[i],error))return false;
    return (!owner->hosted || application_guest_q3_video_current(owner->hosted,error)) &&
        (!owner->native || frontend_native_q3_video_current(owner->native,error)) &&
        (!owner->components || application_q3_components_video_current(owner->components,error));
}
bool frontend_video_guests_resources_returned(const qa_frontend *f,const frontend_video_guests *owner,qa_error *error)
{
    if (!frontend_video_guests_resources_associated(f,owner) ||
        frontend_remote_q3_count(f)+frontend_remote_q3_initial_count(f)!=owner->remote_count)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video resource refresh lost its closed guest roster");
    if(!frontend_video_guests_current(owner,error))return false;
    for(size_t i=0;i<owner->unified_count;++i)
        if(!frontend_unified_q3_video_returned(owner->unified[i],owner,error))return false;
    return true;
}
bool frontend_video_guests_prepare(qa_frontend *f,frontend_video_guests **out,qa_error *error)
{
    if (!f || !f->application || !out || *out || f->video_guests || !f->display ||
        f->capture || f->resource_inventory || f->source_restoring || f->input_shutdown)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video guests require returned Source and input owners");
    size_t decoded_count=frontend_remote_q3_count(f);
    for (size_t i=0;i<decoded_count;++i)
        if (!frontend_remote_q3_runtime_read(frontend_remote_q3_at(f,i)) &&
            !frontend_remote_q3_modules_read(frontend_remote_q3_at(f,i)))
            return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Remote CG lacks its retained video reset producer");
    frontend_remote_q3_initial *initial=NULL;
    frontend_remote_q3_modules *initial_modules=NULL;
    bool initial_present=false;
    if (!frontend_network_q3_video_initial_read(f,&initial,&initial_modules,&initial_present,error)) return false;
    if (frontend_remote_q3_initial_count(f)!=(size_t)initial_present || decoded_count>SIZE_MAX-(size_t)initial_present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video restart has an unassociated Initial resource parent");
    frontend_video_guests *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Source video guest recipes");
    owner->frontend=f; owner->application=f->application; *out=owner; f->video_guests=owner;
    owner->unified_count=frontend_remote_unified_count(f);
    if(owner->unified_count) {
        owner->unified=calloc(owner->unified_count,sizeof(*owner->unified));
        if(!owner->unified) { owner->unified_count=0;return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Unified video replicas"); }
    }
    owner->remote_count=decoded_count+(size_t)initial_present;
    if (owner->remote_count) {
        owner->remote=calloc(owner->remote_count,sizeof(*owner->remote));
        if (!owner->remote) { owner->remote_count=0; return frontend_fail(error,QA_ERROR_MEMORY,"Retaining acquired video CLIENT roster"); }
    }
    for (size_t i=0;i<decoded_count;++i) {
        owner->remote[i].decoded=frontend_remote_q3_at(f,i);
        owner->remote[i].modules=frontend_remote_q3_modules_read(owner->remote[i].decoded);
    }
    if (initial_present) owner->remote[decoded_count]=(video_remote_row){.initial=initial,.modules=initial_modules};
    if (f->frame.source_pending &&
        !qa_material_source_frame_end(f->frame.source_pending,&f->frame,false,error)) return false;
    qa_scene_frame_reset(&f->frame,f->frame.sequence);
    if ((f->audio && !qa_audio_engine_acoustics_release(f->audio,error)) ||
        !application_guest_q3_video_prepare(f->application,&owner->hosted,error) ||
        !frontend_native_q3_video_prepare(f,&owner->native,error) ||
        !application_q3_components_video_prepare(f->application,&owner->components,error)) return false;
    for (size_t i=0;i<owner->remote_count;++i) {
        video_remote_row *row=owner->remote+i;
        if (!(row->modules?frontend_remote_q3_modules_video_prepare(row->modules,&row->ticket,error):
            frontend_remote_q3_compiled_video_prepare(row->decoded,&row->compiled,error))) return false;
    }
    for(size_t i=0;i<owner->unified_count;++i)
        if(!frontend_unified_q3_video_prepare(f,frontend_remote_unified_at(f,i),owner->unified+i,error))return false;
    owner->prepared=true;
    return frontend_video_guests_current(owner,error);
}
static bool reopen_remote(frontend_video_guests *owner,video_remote_row *row,qa_error *error)
{
    if (row->reopened) return true;
    if (row->compiled) {
        if (!frontend_remote_q3_compiled_video_reopen(row->compiled,error)) return false;
        row->reopened=true; return true;
    }
    if (row->init_attempted) {
        if (!frontend_remote_q3_modules_video_close(row->ticket,error)) return false;
        row->media_ready=false; row->init_attempted=false;
    }
    if (!row->media_ready) {
        if (!frontend_remote_q3_modules_video_close(row->ticket,error) ||
            !(row->initial?frontend_remote_q3_initial_video_refresh(row->initial,row->modules,&row->media_generation,error):
                frontend_remote_q3_resources_video_refresh(row->decoded,row->modules,&row->media_generation,error))) return false;
        row->media_ready=true;
    }
    frontend_network_q3_video_reinit_view reinit;
    if (!row->initial && !frontend_network_q3_video_reinit_read(owner->frontend,&reinit,error)) return false;
    row->init_attempted=true;
    if (!frontend_remote_q3_modules_video_reopen(row->ticket,row->initial?NULL:&reinit.init,
        row->initial || reinit.connecting,error)) return false;
    row->reopened=true; return true;
}
bool frontend_video_guests_reopen(frontend_video_guests *owner,qa_error *error)
{
    if (!frontend_video_guests_parent_is(owner?owner->frontend:NULL,owner) || !owner->prepared)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video guest reconstruction lacks its admitted recipes");
    if (owner->reopened) return true;
    if (!frontend_source_renderer_runtime_bind(owner->frontend,error)) return false;
    if (owner->frontend->source_color &&
        (!frontend_q3_texture_mode_initialize(owner->frontend,error) ||
         !frontend_q3_scene_limits_initialize(owner->frontend,error))) return false;
    if (!owner->resources_finished) {
        qa_frontend *f=owner->frontend;
        owner->resource_display=f->display; owner->resource_cpu=f->cpu; owner->resource_gl=f->gl;
        owner->resource_registry=qa_application_cvars(f->application); owner->resource_phase=true;
        if (!owner->resources_prepared) {
            if (owner->resources && !frontend_shared_resource_policy_abort(&owner->resources,error)) return false;
            if (!frontend_shared_resource_policy_restart_prepare(f,owner,&owner->resources,error)) return false;
            owner->resources_prepared=true;
        }
        if (!owner->resources_published) {
            if (!frontend_shared_resource_policy_ready(owner->resources,error) ||
                !frontend_shared_resource_policy_ready_is(owner->resources)) return false;
            frontend_shared_resource_policy_publish(owner->resources);
            owner->resources_published=true;
        }
        frontend_shared_resource_policy_render_publish(owner->resources);
        if (!frontend_shared_resource_policy_finish(&owner->resources,error)) return false;
        owner->resources_finished=true; owner->resource_phase=false;
    }
    if (!frontend_video_guests_current(owner,error)) return false;
    if ((owner->hosted && !application_guest_q3_video_reopen(owner->hosted,error)) ||
        (owner->native && !frontend_native_q3_video_reopen(owner->native,error)) ||
        (owner->components && !application_q3_components_video_reopen(owner->components,error))) return false;
    for (size_t i=0;i<owner->remote_count;++i)
        if (!reopen_remote(owner,owner->remote+i,error)) return false;
    for(size_t i=0;i<owner->unified_count;++i)
        if(!frontend_unified_q3_video_reopen(owner->unified[i],error))return false;
    owner->reopened=true;
    return true;
}
bool frontend_video_guests_finish(frontend_video_guests **slot,qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_video_guests *owner=*slot;
    if (!frontend_video_guests_current(owner,error) || !owner->reopened)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Video guests retain unfinished replacement roles");
    for (size_t i=0;i<owner->remote_count;++i)
        if (!frontend_remote_q3_modules_video_finish(&owner->remote[i].ticket,error) ||
            !frontend_remote_q3_compiled_video_finish(&owner->remote[i].compiled,error)) return false;
    for(size_t i=0;i<owner->unified_count;++i)
        if(!frontend_unified_q3_video_finish(owner->unified+i,error))return false;
    if (!application_guest_q3_video_finish(&owner->hosted,error) ||
        !frontend_native_q3_video_finish(&owner->native,error) ||
        !application_q3_components_video_finish(&owner->components,error) ||
        !frontend_acoustics_source_bind(owner->frontend,error)) return false;
    owner->frontend->video_guests=NULL; free(owner->unified);free(owner->remote); free(owner); *slot=NULL; return true;
}
bool frontend_video_guests_abort(frontend_video_guests **slot,qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_video_guests *owner=*slot;
    if (!frontend_video_guests_parent_is(owner->frontend,owner)) return false;
    if (owner->resources && !(owner->resources_published?
        frontend_shared_resource_policy_finish(&owner->resources,error):
        frontend_shared_resource_policy_abort(&owner->resources,error))) return false;
    owner->resource_phase=false;
    for(size_t i=owner->unified_count;i>0;--i)
        if(!frontend_unified_q3_video_abort(owner->unified+i-1,error))return false;
    if (!frontend_video_guests_current(owner,error)) return false;
    for (size_t i=0;i<owner->remote_count;++i) {
        video_remote_row *row=owner->remote+i;
        if (row->compiled) {
            if (!frontend_remote_q3_compiled_video_abort(&row->compiled,error)) return false;
            continue;
        }
        if (!row->ticket) continue;
        if (!frontend_remote_q3_modules_video_close(row->ticket,error)) return false;
        row->reopened=false; row->media_ready=false; row->init_attempted=false;
        if (!reopen_remote(owner,row,error) || !frontend_remote_q3_modules_video_finish(&row->ticket,error)) return false;
    }
    if (
        !application_guest_q3_video_abort(&owner->hosted,error) ||
        !frontend_native_q3_video_abort(&owner->native,error) ||
        !application_q3_components_video_abort(&owner->components,error) ||
        !frontend_acoustics_source_bind(owner->frontend,error)) return false;
    owner->frontend->video_guests=NULL; free(owner->unified);free(owner->remote); free(owner); *slot=NULL; return true;
}
