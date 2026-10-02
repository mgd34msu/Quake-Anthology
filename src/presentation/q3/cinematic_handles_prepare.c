#include "cinematic_handles_private.h"
#include "qa/media_library_prepare.h"

struct qa_q3_cinematic_handles_stage {
    qa_q3_cinematic_handles *owner;
    qa_scene_resource_policy *bank;
    qa_scene_resources *destination;
    qa_roq_scratch *decoder_scratch;
    int32_t selected_handle,decoder_handle;
    q3cin_movie movies[16];
    const qa_scene_image *scratch[16];
    bool added[16];
    bool restart,decoder_changed;
    bool busy,sealed,published;
};
bool q3cin_stage_retains_source(const qa_q3_cinematic_handles_stage *stage,const qa_q3_cinematic_source *source)
{
    if (!stage || !source || stage->owner->stage!=stage) return false;
    for (size_t i=0;i<16;++i) if (stage->movies[i].source==source) return true;
    return false;
}
static bool held(const qa_q3_cinematic_handles_stage *stage)
{
    return stage && stage->owner && stage->owner->stage==stage && !stage->owner->busy &&
        stage->bank && qa_scene_resource_policy_source(stage->bank)==stage->owner->options.images &&
        qa_scene_resource_policy_destination(stage->bank)==stage->destination;
}
qa_q3_cinematic_handles *qa_q3_cinematic_handles_stage_owner(const qa_q3_cinematic_handles_stage *stage)
{ return held(stage)?stage->owner:NULL; }
qa_scene_resource_policy *qa_q3_cinematic_handles_stage_bank(const qa_q3_cinematic_handles_stage *stage)
{ return held(stage)?stage->bank:NULL; }
bool qa_q3_cinematic_handles_stage_associated(const qa_q3_cinematic_handles *owner)
{ return owner && held(owner->stage) && !owner->stage->busy; }
bool qa_q3_cinematic_handles_stage_prepare(qa_q3_cinematic_handles *owner,
    qa_scene_resource_policy *bank,qa_q3_cinematic_handles_stage **out,qa_error *error)
{
    if (!qa_q3_cinematic_handles_idle(owner) || !bank || !out || *out ||
        qa_scene_resource_policy_source(bank)!=owner->options.images)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic slots require the actual global scratch bank ticket");
    qa_q3_cinematic_handles_stage *stage=calloc(1,sizeof(*stage));
    if (!stage) return q3cin_fail(error,QA_ERROR_MEMORY,"Preparing global cinematic slot custody");
    stage->owner=owner; stage->bank=bank; stage->destination=qa_scene_resource_policy_destination(bank);
    stage->selected_handle=owner->selected_handle; stage->decoder_handle=owner->decoder_handle;
    if (!qa_roq_scratch_create(&stage->decoder_scratch,error)) { free(stage); return false; }
    qa_buffer shared={0};
    bool copied=qa_roq_scratch_capture(owner->decoder_scratch,&shared,error) &&
        qa_roq_scratch_restore(stage->decoder_scratch,(qa_bytes){shared.data,shared.size},error);
    qa_buffer_free(&shared);
    if (!copied) { qa_roq_scratch_release(stage->decoder_scratch); free(stage); return false; }
    qa_q3_image_upload_options restart;
    stage->restart=qa_scene_resource_policy_source_restart_read(bank,&restart);
    memcpy(stage->movies,owner->movies,sizeof(stage->movies));
    for (size_t i=0;i<16;++i) {
        qa_scene_image *mapped=NULL;
        if (!qa_scene_resource_policy_image(bank,owner->scratch[i],&mapped,error)) {
            for (size_t j=0;j<i;++j) qa_scene_image_release(stage->scratch[j]);
            qa_roq_scratch_release(stage->decoder_scratch); free(stage); return false;
        }
        stage->scratch[i]=mapped;
    }
    owner->stage=stage; *out=stage; return true;
}
bool qa_q3_cinematic_handles_stage_shader(qa_q3_cinematic_handles_stage *stage,
    qa_q3_cinematic_source *source,qa_media_library_stage *media,const char *path,
    int32_t *handle,const qa_scene_image **out,qa_error *error)
{
    if (!held(stage) || stage->busy || stage->sealed || stage->published || !source ||
        source->handles!=stage->owner || !media || qa_media_library_stage_source(media)!=source->options.media ||
        !path || !handle || !out || !source->options.current(source->options.context,&source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared videoMap requires its actual Source and prepared media ticket");
    bool occupied[16];
    for (size_t i=0;i<16;++i) occupied[i]=stage->movies[i].occupied;
    stage->busy=true;
    bool ok=q3cin_play_into(source,stage->movies,qa_media_library_stage_destination(media),stage->decoder_scratch,
        &stage->selected_handle,&stage->decoder_handle,path,
        (qa_scene_rect_f){0,0,256,256},2u|8u|16u,NULL,NULL,handle,error);
    for (size_t i=0;i<16;++i) if (!occupied[i] && stage->movies[i].occupied) stage->added[i]=true;
    if (ok && (*handle<0 || !occupied[*handle])) stage->decoder_changed=true;
    if (ok) *out=*handle>=0?qa_scene_source_q3_scratch(stage->destination,(size_t)*handle):NULL;
    if (ok && *handle>=0 && !*out) ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared shader handle lacks its actual destination scratch slot");
    if (ok && !source->options.current(source->options.context,&source->options))
        ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic Source changed during decoder construction");
    stage->busy=false; return ok;
}
bool qa_q3_cinematic_handles_stage_ready_is(const qa_q3_cinematic_handles_stage *stage)
{
    if (!held(stage) || stage->busy || !stage->sealed || stage->published) return false;
    for (size_t i=0;i<16;++i) {
        const q3cin_movie *movie=&stage->movies[i];
        if (!stage->scratch[i] || (stage->added[i] && (!movie->source || movie->source->handles!=stage->owner ||
            movie->pending || movie->flags!=(2u|8u|16u) ||
            (!movie->playback && movie->status!=0)))) return false;
    }
    return true;
}
bool qa_q3_cinematic_handles_stage_ready(qa_q3_cinematic_handles_stage *stage,qa_error *error)
{
    if (!held(stage) || stage->busy || stage->published)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic slots lost their actual global claim");
    if (stage->decoder_changed) for (size_t i=0;i<16;++i) {
        q3cin_movie *movie=&stage->movies[i];
        qa_cinematic *decoder=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (decoder && !qa_cinematic_roq_scratch_rebind_ready(decoder,stage->decoder_scratch,error)) return false;
    }
    stage->sealed=true;
    return qa_q3_cinematic_handles_stage_ready_is(stage) ||
        q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic slot ownership is incomplete");
}
void qa_q3_cinematic_handles_stage_publish(qa_q3_cinematic_handles_stage *stage)
{
    if (!stage || !stage->sealed || stage->published || stage->busy || stage->owner->stage!=stage) return;
    /* Banks/media publish first. These are retained real destination versions;
     * no parser, frame upload, clock sample or playback callback runs here. */
    if (stage->decoder_changed) {
        for (size_t i=0;i<16;++i) {
            q3cin_movie *movie=&stage->movies[i];
            qa_cinematic *decoder=movie->playback?movie->playback:
                movie->system.playback?movie->system.playback(movie->system.context):NULL;
            if (decoder) qa_cinematic_roq_scratch_rebind(decoder,stage->decoder_scratch);
        }
        qa_roq_scratch_release(stage->owner->decoder_scratch);
        stage->owner->decoder_scratch=stage->decoder_scratch; stage->decoder_scratch=NULL;
        stage->owner->selected_handle=stage->selected_handle;
        stage->owner->decoder_handle=stage->decoder_handle;
    }
    for (size_t i=0;i<16;++i) {
        qa_scene_image_release(stage->owner->scratch[i]);
        stage->owner->scratch[i]=stage->scratch[i]; stage->scratch[i]=NULL;
        if (stage->added[i]) {
            q3cin_close(stage->owner,(uint32_t)i,QA_CINEMATIC_STOPPED,NULL);
            stage->owner->movies[i]=stage->movies[i]; stage->movies[i]=(q3cin_movie){0};
        }
        if (stage->restart && stage->owner->movies[i].playback) {
            stage->owner->movies[i].uploaded=UINT64_MAX; stage->owner->movies[i].redefine=true;
        }
    }
    stage->published=true;
}
static bool dispose(qa_q3_cinematic_handles_stage **slot,bool published,qa_error *error)
{
    if (!slot) return q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic disposal requires its actual slot");
    qa_q3_cinematic_handles_stage *stage=*slot;
    if (!stage) return true;
    if (stage->owner->stage!=stage || stage->owner->busy || stage->busy || stage->published!=published)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic disposal lost its retained claim");
    for (size_t i=0;i<16;++i) {
        if (!published && stage->added[i]) {
            qa_cinematic_destroy(stage->movies[i].playback);
            qa_cinematic_asset_release(stage->movies[i].asset); free(stage->movies[i].path);
        }
        qa_scene_image_release(stage->scratch[i]);
    }
    qa_roq_scratch_release(stage->decoder_scratch);
    stage->owner->stage=NULL; free(stage); *slot=NULL; return true;
}
bool qa_q3_cinematic_handles_stage_finish(qa_q3_cinematic_handles_stage **slot,qa_error *error)
{ return dispose(slot,true,error); }
bool qa_q3_cinematic_handles_stage_abort(qa_q3_cinematic_handles_stage **slot,qa_error *error)
{ return dispose(slot,false,error); }
