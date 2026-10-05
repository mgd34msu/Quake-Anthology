#include "scene_refs.h"
#include "internal.h"
#include "visual_access.h"

static void model_release(void *token) { frontend_model_release(token); }
static void animation_release(void *token) { frontend_animation_release(token); }
bool frontend_scene_model_source_read(const qa_scene_model *model, qa_scene_model_content_kind kind,
    frontend_model_source *out)
{
    if (!out || (kind!=QA_SCENE_MODEL_CONTENT_SOURCE && kind!=QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE)) return false;
    qa_scene_model_content_lease lease={0}; frontend_model_source source;
    if (!qa_scene_model_content_read(model,kind,&lease) || !lease.context) return false;
    if (lease.release!=model_release) return frontend_visual_scene_model_source_read(model,kind,out);
    if (!frontend_model_lease_source(lease.context,&source)) return false;
    const qa_model_replacement *replacement=qa_scene_model_replacement_description(model);
    const qa_model *actual=kind==QA_SCENE_MODEL_CONTENT_SOURCE?qa_scene_model_source(model):replacement?replacement->source:NULL;
    if (source.model!=actual) return false;
    *out=source; return true;
}
bool frontend_scene_animation_source_read(const qa_scene_model *model, frontend_animation_source *out)
{
    if (!out) return false;
    qa_scene_model_content_lease lease={0}; frontend_animation_source source;
    const qa_model_replacement *replacement=qa_scene_model_replacement_description(model);
    if (!replacement || !qa_scene_model_content_read(model,QA_SCENE_MODEL_CONTENT_ANIMATION,&lease) || !lease.context) return false;
    if (lease.release!=animation_release) return frontend_visual_scene_animation_source_read(model,out);
    if (!frontend_animation_lease_source(lease.context,&source) ||
        source.animation!=replacement->animation) return false;
    *out=source; return true;
}
bool frontend_scene_model_content_clone(const qa_scene_model *model,qa_scene_model_content_kind kind,
    qa_scene_model_content_lease *out,qa_error *error)
{
    qa_scene_model_content_lease held={0};
    if(!out || out->context || out->release || !qa_scene_model_content_read(model,kind,&held) || !held.context)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene content clone requires its actual owning token and empty output");
    if((kind==QA_SCENE_MODEL_CONTENT_SOURCE || kind==QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE) &&
        held.release==model_release) {
        frontend_model_source source; frontend_model_lease *lease=NULL;
        if(!frontend_scene_model_source_read(model,kind,&source) ||
            !frontend_model_lease_clone(held.context,&lease,error)) return false;
        *out=(qa_scene_model_content_lease){lease,model_release,source.resource}; return true;
    }
    if(kind==QA_SCENE_MODEL_CONTENT_ANIMATION && held.release==animation_release) {
        frontend_animation_source source; frontend_animation_lease *lease=NULL;
        if(!frontend_scene_animation_source_read(model,&source) ||
            !frontend_animation_lease_clone(held.context,&lease,error)) return false;
        *out=(qa_scene_model_content_lease){lease,animation_release,source.resource}; return true;
    }
    return frontend_visual_scene_model_content_clone(model,kind,out,error);
}
