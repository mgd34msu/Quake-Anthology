#include "source_renderer_runtime.h"
#include "material_movies.h"
#include "q3_render_policy.h"
#include "qa/material_source_scratch.h"
static bool diagnostics(void *context,qa_scene_source_diagnostics *out,qa_error *error)
{
    qa_frontend *f=context;
    if (!f || !f->application || !out || (!f->cpu && !f->gl) || (f->cpu && f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source issue diagnostics lost its physical renderer lifetime");
    return frontend_q3_material_diagnostics_read(f,out,error);
}
bool frontend_source_renderer_runtime_bind(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || (f->cpu && f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source runtime binding requires its actual frontend renderer");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    return !controls || qa_render_controls_source_runtime_bind(controls,
        frontend_material_movies_frontend_resolve,f,diagnostics,f,error);
}
