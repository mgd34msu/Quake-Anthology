#include "source_renderer_runtime.h"
#include "material_movies.h"
#include "q3_render_policy.h"
#include "shared_render_controls.h"
#include "qa/material_source_scratch.h"
static bool diagnostics(void *context,qa_scene_source_diagnostics *out,qa_error *error)
{
    qa_frontend *f=context;
    if (!f || !f->application || !out || (!f->cpu && !f->gl) || (f->cpu && f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source issue diagnostics lost its physical renderer lifetime");
    return frontend_q3_material_diagnostics_read(f,out,error);
}
static bool frame_policy(void *context,qa_scene_frame *reached,qa_error *error)
{
    if (reached && !reached->source_backend) return true;
    qa_frontend *f=context;
    const qa_cvar_view *row=f && f->application && reached && (f->cpu || f->gl) && !(f->cpu && f->gl)?
        qa_cvars_read(qa_application_cvars(f->application), f->engine_cvars.r_skipBackEnd):NULL;
    if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source swap lost its actual physical ENGINE skip row");
    const qa_cvar_view *buffer=qa_cvars_read(qa_application_cvars(f->application), f->engine_cvars.r_drawBuffer);
    if (!buffer) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source swap lost its actual physical ENGINE draw buffer");
    const unsigned char *a=(const unsigned char *)buffer->value,*b=(const unsigned char *)"GL_FRONT";
    while (*a && *b) {
        unsigned char c=*a;
        if (c>='a' && c<='z') c-='a'-'A';
        if (c!=*b) break;
        ++a; ++b;
    }
    if (!frontend_source_renderer_policy(f,error)) return false;
    reached->source_skip_backend=row->integer!=0;
    reached->source_front_buffer=!*a && !*b;
    return true;
}
bool frontend_source_renderer_runtime_bind(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || (f->cpu && f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source runtime binding requires its actual frontend renderer");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    return !controls || (qa_render_controls_source_print_bind(controls,frontend_print,f,error) &&
        qa_render_controls_source_runtime_bind(controls,
        frontend_material_movies_frontend_resolve,f,diagnostics,f,frame_policy,f,error));
}

bool frontend_source_renderer_policy(qa_frontend *f,qa_error *error)
{
    qa_cvars *registry=f && f->application?qa_application_cvars(f->application):NULL;
    const qa_cvar_view *rows[6];
    if (!registry || (f->cpu && f->gl)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Renderer diagnostics lost ENGINE ownership");
    const qa_cvar_handle handles[]={f->engine_cvars.r_finish,f->engine_cvars.r_showImages,
        f->engine_cvars.r_speeds,f->engine_cvars.r_measureOverdraw,f->engine_cvars.r_shadows,f->engine_cvars.r_nobind};
    for (size_t i=0;i<6;++i) {
        rows[i]=qa_cvars_read(registry,handles[i]);
        if (!rows[i]) return frontend_fail(error,QA_ERROR_ARGUMENT,"Renderer diagnostics lost an ENGINE setting");
    }
    uint32_t stencil_bits=0;
    if (f->cpu) {
        qa_cpu_capabilities caps;
        if (!qa_cpu_capabilities_read(f->cpu,&caps,error)) return false;
        stencil_bits=caps.stencil_bits;
    } else if (f->gl) {
        const qa_gl_capabilities *caps=qa_gl_capabilities_get(f->gl);
        if (!caps) return false;
        stencil_bits=caps->stencil_bits;
    }
    bool overdraw=rows[3]->integer!=0;
    if (overdraw && (stencil_bits<4 || rows[4]->integer==2)) {
        frontend_print(f,stencil_bits<4?"Warning: not enough stencil bits to measure overdraw\n":
            "Warning: stencil shadows and overdraw measurement are mutually exclusive\n");
        if (!qa_cvars_set(registry,"r_measureOverdraw","0",true,error)) return false;
        overdraw=false;
    }
    if (qa_cvars_read(registry,f->engine_cvars.r_measureOverdraw)->modified)
        qa_cvars_clear_modified(registry,"r_measureOverdraw");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    uint32_t clock_word=(uint32_t)(f->wall_time_ns/UINT64_C(1000000));
    int32_t clock_value; memcpy(&clock_value,&clock_word,sizeof(clock_value));
    qa_render_source_frame_values values={.finish=rows[0]->integer,.show_images=rows[1]->integer,
        .speeds=rows[2]->integer,.milliseconds=clock_value,.measure_overdraw=overdraw,.no_bind=rows[5]->integer!=0};
    return !controls || qa_render_controls_source_frame_policy(controls,&values,error);
}
bool frontend_source_renderer_image_grid(qa_frontend *f,int32_t mode,qa_error *error)
{
    if (!frontend_source_renderer_policy(f,error)) return false;
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    return controls?qa_render_controls_source_image_grid(controls,mode,error):
        frontend_fail(error,QA_ERROR_ARGUMENT,"Image grid requires the actual physical renderer");
}
bool frontend_source_renderer_end_registration(qa_frontend *f,qa_error *error)
{
    const qa_cvar_view *row=f && f->application?qa_cvars_read(qa_application_cvars(f->application), f->engine_cvars.r_showImages):NULL;
    if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"EndRegistration lost its actual ENGINE image policy");
    int32_t mode=row->integer;
    bool ok=f->cpu?qa_cpu_execute(f->cpu,&f->frame,error):f->gl?qa_gl_execute(f->gl,&f->frame,error):false;
    if (!ok) return false;
    f->frame.command_count=f->frame.group_count=0;
    return frontend_source_renderer_image_grid(f,mode,error);
}
