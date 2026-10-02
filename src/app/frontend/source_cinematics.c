#include "source_cinematics.h"
#include "q3_render_policy.h"
#include "qa/render_controls.h"
#include "qa/material_source_scratch.h"

static bool fullscreen_draw(void *context,const qa_scene_image *image,qa_scene_rect viewport,
    uint32_t seat,qa_scene_frame *frame,qa_error *error)
{
    qa_frontend *f=context;
    if (!f || frame!=&f->frame || !image || f->capture || f->resource_inventory || f->source_restoring ||
        (!!f->cpu==!!f->gl) || seat>=f->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Fullscreen raw draw lost its actual physical renderer");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):qa_gl_render_controls(f->gl);
    qa_material_source_scratch *scratch=qa_render_controls_source_scratch(controls,error);
    qa_q3_presentation_options policy={0};
    if (!scratch || !frontend_q3_renderer_options_read(f,&policy,error)) return false;
    qa_material_context material={.source_scratch=scratch,.identity_light=policy.identity_light,
        .milliseconds=(int32_t)((f->wall_time_ns/UINT64_C(1000000)) & INT32_MAX)};
    material.view.viewport=viewport; material.view.seat=seat;
    qa_scene_draw draw={.textures={image,NULL},.texture_count=1,.source_direct=QA_SOURCE_DIRECT_RAW,
        .retain_texture={true,false}};
    qa_scene_state_default(&draw.state);
    float u=0.5f/(float)image->logical_width,v=0.5f/(float)image->logical_height;
    qa_scene_rect_f rect={(float)viewport.x,(float)viewport.y,(float)viewport.width,(float)viewport.height};
    qa_scene_vec4 color={policy.identity_light,policy.identity_light,policy.identity_light,1};
    if (!qa_scene_picture_geometry(frame,viewport,rect,(qa_scene_vec4){u,v,1-u,1-v},color,&draw.mesh,error)) return false;
    qa_scene_vertex *vertices=(qa_scene_vertex *)draw.mesh.vertices;
    for (size_t i=0;i<draw.mesh.vertex_count;++i) vertices[i].normal=qa_v3(0,0,0);
    qa_scene_matrix_identity(&draw.model);
    draw.mvp=(qa_scene_matrix){.m={2.0f/(float)viewport.width,0,0,0,
        0,-2.0f/(float)viewport.height,0,0,0,0,-2,0,
        -1.0f-2.0f*(float)viewport.x/(float)viewport.width,
        1.0f+2.0f*(float)viewport.y/(float)viewport.height,-1,1}};
    material.source_picture_projection=draw.mvp;
    return qa_material_source_raw_submit(scratch,frame,&material,&draw,error);
}

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
        pool.upload!=upload || pool.ui_limits!=ui_limits || pool.fullscreen_draw!=fullscreen_draw ||
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
        out->context!=f || out->upload!=upload || out->ui_limits!=ui_limits ||
        out->fullscreen_draw!=fullscreen_draw || !out->images)
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
    qa_q3_cinematic_handles_options options={.images=images,.context=f,.ui_limits=ui_limits,.upload=upload,
        .fullscreen_draw=fullscreen_draw};
    return qa_q3_cinematic_handles_create(&options,&f->source_cinematics,error);
}
bool frontend_source_cinematics_destroy(qa_frontend *f,qa_error *error)
{
    return !f || qa_q3_cinematic_handles_destroy(&f->source_cinematics,error);
}
bool frontend_source_cinematics_rebind_ready(const qa_frontend *owned,const qa_frontend *destination,qa_error *error)
{
    if (!owned || !destination) return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic adoption requires its actual frontend owners");
    if (!owned->source_cinematics) return true;
    qa_q3_cinematic_handles_options options;
    if (!frontend_source_cinematics_read(owned,&options,error)) return false;
    options.context=(void *)destination;
    return qa_q3_cinematic_handles_rebind_ready(owned->source_cinematics,&options,error);
}
void frontend_source_cinematics_rebind(qa_frontend *f)
{
    qa_q3_cinematic_handles_options options;
    if (f && qa_q3_cinematic_handles_read(f->source_cinematics,&options)) {
        options.context=f; qa_q3_cinematic_handles_rebind(f->source_cinematics,&options);
    }
}
