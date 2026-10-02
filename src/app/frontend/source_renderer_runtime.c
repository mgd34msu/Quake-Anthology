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
    qa_frontend *f=context;
    const qa_cvar_view *row=f && f->application && reached && (f->cpu || f->gl) && !(f->cpu && f->gl)?
        frontend_render_control_record(qa_application_cvars(f->application),"r_skipBackEnd"):NULL;
    if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source swap lost its actual physical ENGINE skip row");
    reached->source_skip_backend=row->integer!=0; return true;
}
bool frontend_source_renderer_runtime_bind(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || (f->cpu && f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source runtime binding requires its actual frontend renderer");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    return !controls || qa_render_controls_source_runtime_bind(controls,
        frontend_material_movies_frontend_resolve,f,diagnostics,f,frame_policy,f,error);
}
