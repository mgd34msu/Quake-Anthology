#include "source_acoustics.h"
#include "qa/application_acoustics.h"
#include "remote_q3_client.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
typedef struct frontend_acoustic_scene {
    qa_frontend *frontend;
    qa_application *application;
    qa_application_acoustics *shared;
} frontend_acoustic_scene;
static bool current(const void *context)
{
    const frontend_acoustic_scene *owner=context;
    bool shared=false;
    return owner && owner->frontend->application==owner->application &&
        frontend_source_acoustics_shared(owner->frontend,&shared,NULL) && shared &&
        !frontend_remote_q3_count(owner->frontend) && !frontend_remote_q1_count(owner->frontend) &&
        !frontend_remote_q2_count(owner->frontend) && !owner->frontend->remote_unified &&
        qa_application_acoustics_current(owner->shared);
}
static void release(void *context)
{
    frontend_acoustic_scene *owner=context;
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
    if (!shared) return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Private Source acoustics requires its selected CG solid-scene receipt");
    if (frontend_remote_q3_count(f) || frontend_remote_q1_count(f) || frontend_remote_q2_count(f) || f->remote_unified)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Remote acoustics requires its received collision-scene receipt");
    frontend_acoustic_scene *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual frontend acoustic scene");
    owner->frontend=f; owner->application=f->application;
    if (!qa_application_acoustics_hold(f->application,&owner->shared,error)) { free(owner); return false; }
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
