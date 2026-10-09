#include "source_acoustics.h"
#include "capture.h"
#include "config_store.h"
#include "qa/console_cvar_observer.h"
#include "qa/application_acoustics.h"
#include "remote_q3_client.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
typedef struct frontend_acoustic_scene {
    qa_frontend *frontend;
    qa_application *application;
    qa_application_acoustics *shared;
    qa_q3_host_collision_scene **private_scenes;
    qa_q3_host_collision_view *private_views;
    uint32_t seat_count;
} frontend_acoustic_scene;
static bool current(const void *context)
{
    const frontend_acoustic_scene *owner=context;
    bool shared=false;
    if (!(owner && owner->frontend->application==owner->application &&
        owner->seat_count==owner->frontend->options.seats &&
        frontend_source_acoustics_shared(owner->frontend,&shared,NULL) &&
        !frontend_remote_q3_count(owner->frontend) && !frontend_remote_q1_count(owner->frontend) &&
        !frontend_remote_q2_count(owner->frontend) && !owner->frontend->remote_unified &&
        qa_application_acoustics_current(owner->shared))) return false;
    bool has_private=false;
    for (uint32_t i=0;i<owner->seat_count;++i) if (owner->private_scenes[i]) {
        has_private=true;
        if (!qa_q3_host_collision_current(owner->private_scenes[i]) ||
            !frontend_source_acoustics_private_current(owner->frontend,i,owner->private_views+i)) return false;
    }
    return shared || has_private;
}
static void release(void *context)
{
    frontend_acoustic_scene *owner=context;
    for (uint32_t i=0;i<owner->seat_count;++i)
        qa_q3_host_collision_release(owner->private_scenes?owner->private_scenes[i]:NULL);
    free(owner->private_scenes); free(owner->private_views);
    qa_application_acoustics_release(owner->shared); free(owner);
}
static bool trace(void *context,const qa_audio_listener *listener,qa_vec3 start,qa_vec3 end,
    qa_audio_trace_hit *out,qa_error *error)
{
    frontend_acoustic_scene *owner=context; qa_application_acoustics_view scene;
    if (!current(owner) || !listener || !out || listener->seat>=owner->frontend->options.seats ||
        !qa_vec_finite(start) || !qa_vec_finite(end) ||
        !qa_application_acoustics_read(owner->shared,&scene,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic trace lost its actual listener scene");
    qa_actor_id pass={0}; bool found=listener->actor==QA_AUDIO_NO_ACTOR;
    for (size_t i=0;!found && i<owner->frontend->audio_id_count;++i) {
        const frontend_audio_identity *row=owner->frontend->audio_ids+i;
        if (!row->retired && row->id==listener->actor) { pass=row->actor; found=true; }
    }
    if (!found) return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic listener has no actual full actor identity");
    if (owner->private_scenes[listener->seat]) {
        qa_trace_query query={.start=start,.end=end,.shape={.kind=QA_SHAPE_POINT},
            .policy={.family=QA_COLLISION_Q3,.contents_mask=3,.curves=true},.pass_actor=pass};
        qa_trace_result result;
        if (!qa_q3_host_collision_trace(owner->private_scenes[listener->seat],&query,&result,error)) return false;
        *out=(qa_audio_trace_hit){.fraction=result.fraction,.start_solid=result.start_solid,
            .all_solid=result.all_solid,.end=result.end};
        return true;
    }
    if (!scene.world) {
        if (pass.registry) return frontend_fail(error,QA_ERROR_ARGUMENT,"Actor listener has no admitted acoustic world");
        *out=(qa_audio_trace_hit){.fraction=1,.end=end}; return true;
    }
    return qa_application_acoustics_trace(owner->shared,pass,start,end,out,error);
}
bool frontend_acoustics_source_hold(qa_frontend *f,qa_audio_acoustics_source *out,qa_error *error)
{
    if (!f || !f->application || !out || out->context)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic scene requires its actual frontend owner and empty hold");
    bool shared=false;
    if (!frontend_source_acoustics_shared(f,&shared,error)) return false;
    if (frontend_remote_q3_count(f) || frontend_remote_q1_count(f) || frontend_remote_q2_count(f) || f->remote_unified)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Remote acoustics requires its received collision-scene receipt");
    frontend_acoustic_scene *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual frontend acoustic scene");
    owner->frontend=f; owner->application=f->application;
    owner->seat_count=f->options.seats;
    owner->private_scenes=calloc(owner->seat_count,sizeof(*owner->private_scenes));
    owner->private_views=calloc(owner->seat_count,sizeof(*owner->private_views));
    if (owner->seat_count && (!owner->private_scenes || !owner->private_views)) {
        release(owner); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining physical private acoustic scenes");
    }
    if (!qa_application_acoustics_hold(f->application,&owner->shared,error)) { release(owner); return false; }
    bool has_private=false;
    for (uint32_t i=0;i<owner->seat_count;++i) {
        bool private_scene=false,present=false;
        if (!frontend_source_acoustics_private_hold(f,i,owner->private_scenes+i,&private_scene,error)) {
            release(owner); return false;
        }
        has_private|=private_scene;
        if (private_scene && !qa_q3_host_collision_read(owner->private_scenes[i],owner->private_views+i,&present,error)) {
            release(owner); return false;
        }
    }
    if (!shared && !has_private) {
        release(owner); return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Private Source has no completed physical CG acoustic receiver");
    }
    *out=(qa_audio_acoustics_source){owner,current,trace,release}; return true;
}
bool frontend_acoustics_source_bind(qa_frontend *f,qa_error *error)
{
    if (!f || !f->audio || !qa_audio_engine_acoustics_enabled(f->audio)) return f!=NULL;
    qa_audio_acoustics_source source={0};
    if (!frontend_acoustics_source_hold(f,&source,error)) return false;
    if (qa_audio_engine_acoustics_bind(f->audio,true,&source,error)) return true;
    source.release(source.context); return false;
}
bool frontend_acoustics_source_sync(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Live acoustics requires its returned frontend ENGINE owner");
    if (!f->audio || frontend_config_store_shared_pending(f->config_store)) return true;
    const qa_cvars *registry=qa_application_cvars(f->application);
    const qa_cvar_view *row=qa_cvars_read(registry,f->engine_cvars.s_geometry_acoustics);
    if (!qa_cvars_observer_idle(registry) || !row || !row->value ||
        (strcmp(row->value,"0") && strcmp(row->value,"1")))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Live acoustics requires its committed canonical 0 or 1 declaration");
    bool enabled=!strcmp(row->value,"1");
    if (enabled==qa_audio_engine_acoustics_enabled(f->audio)) return true;
    if (!enabled) return qa_audio_engine_acoustics_bind(f->audio,false,NULL,error);
    qa_audio_acoustics_source source={0};
    if (!frontend_acoustics_source_hold(f,&source,error)) return false;
    if (qa_audio_engine_acoustics_bind(f->audio,true,&source,error)) return true;
    source.release(source.context); return false;
}
