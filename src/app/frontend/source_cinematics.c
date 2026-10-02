#include "source_cinematics.h"
#include "q3_render_policy.h"
#include "qa/render_controls.h"

static bool ui_limits(void *context,bool *limited,qa_error *error)
{
    int32_t hardware=0; uint32_t maximum=0;
    if (!limited || !frontend_q3_renderer_hardware_read(context,&hardware,&maximum,error)) return false;
    *limited=hardware==3 || (maximum!=0 && maximum<=256);
    return true;
}
static bool upload(void *context,const qa_scene_image *slot,const qa_scene_image *version,
    bool redefine,bool dirty,qa_error *error)
{
    qa_frontend *f=context;
    qa_q3_cinematic_handles_options pool;
    if (!f || f->source_restoring || f->capture || f->resource_inventory ||
        !qa_q3_cinematic_handles_read(f->source_cinematics,&pool) || pool.context!=f ||
        pool.upload!=upload || pool.ui_limits!=ui_limits ||
        qa_scene_image_resource_owner(slot)!=pool.images || qa_scene_image_resource_owner(version)!=pool.images ||
        (!!f->cpu==!!f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source cinematic upload lost its actual scratch pool and physical renderer");
    bool no_bind=false;
    if (!frontend_q3_source_no_bind_read(f,NULL,&no_bind,error)) return false;
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):qa_gl_render_controls(f->gl);
    const qa_scene_image *binding=slot;
    if (no_bind) {
        const qa_scene_image *dlight=NULL;
        if (!qa_render_controls_source_dlight_read(controls,&dlight,error)) return false;
        if (dlight) binding=dlight;
    }
    return qa_render_controls_source_texture_upload(controls,slot,version,binding,redefine,dirty,error);
}
bool frontend_source_cinematics_read(const qa_frontend *f,qa_q3_cinematic_handles_options *out,qa_error *error)
{
    if (!f || !out || !qa_q3_cinematic_handles_read(f->source_cinematics,out) ||
        out->context!=f || out->upload!=upload || out->ui_limits!=ui_limits || !out->images)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source cinematic pool lost its retained physical scratch bank");
    return true;
}
bool frontend_source_cinematics_ensure(qa_frontend *f,qa_scene_resources *images,qa_error *error)
{
    if (!f || !images || f->capture || f->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source cinematic construction requires its actual admitted image bank");
    if (f->source_cinematics) {
        qa_q3_cinematic_handles_options existing;
        return frontend_source_cinematics_read(f,&existing,error);
    }
    qa_q3_cinematic_handles_options options={.images=images,.context=f,.ui_limits=ui_limits,.upload=upload};
    return qa_q3_cinematic_handles_create(&options,&f->source_cinematics,error);
}
bool frontend_source_cinematics_destroy(qa_frontend *f,qa_error *error)
{
    return !f || qa_q3_cinematic_handles_destroy(&f->source_cinematics,error);
}
