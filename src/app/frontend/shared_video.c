#include "shared_video.h"
#include "capture.h"

struct frontend_shared_video {
    qa_frontend *frontend;
    qa_application *application;
    qa_display *original,*candidate,*retired;
    qa_gl_renderer *gl;
    qa_cpu_renderer *cpu;
    qa_input_platform *platform;
    frontend_input_settings *input;
    qa_display_surface_ticket *surface;
    qa_gl_surface_ticket *gl_surface;
    qa_cpu_surface_ticket *cpu_surface;
    qa_display_info observed;
    bool prepared,published,rolling_back,rolled_back;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool current(const frontend_shared_video *owner,qa_error *error)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    return (f && f->application==owner->application && f->gl==owner->gl &&
        f->cpu==owner->cpu && f->input==owner->platform &&
        f->display==(owner->published?owner->candidate:owner->original) &&
        !f->stepping && !f->capture && !f->source_restoring &&
        frontend_seat_callbacks_returned(f)) ||
        fail(error,"Prepared video lost its actual returned display/renderer/input parents");
}
bool frontend_shared_video_prepare(qa_frontend *f,const qa_display_settings *settings,float gamma,
    frontend_input_settings *input,frontend_shared_video **out,qa_error *error)
{
    if (!f || !settings || !out || *out || !f->application || !f->display ||
        (!!f->gl==!!f->cpu) || !f->input || !input || f->input_settings!=input ||
        f->stepping || f->capture || f->source_restoring ||
        !isfinite(gamma) || gamma<.5f || gamma>3 ||
        !frontend_input_settings_release_all_ready(input,error) ||
        !frontend_input_settings_ready(input,error))
        return fail(error,"Video preparation requires its admitted display and completed physical ALL releases");
    frontend_shared_video *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared shared video");
    owner->frontend=f; owner->application=f->application; owner->original=f->display;
    owner->gl=f->gl; owner->cpu=f->cpu; owner->platform=f->input; owner->input=input;
    *out=owner;
    /* The genuine GL native presentation must be captured before a candidate
     * window changes the current endpoint on its retained context. */
    if (owner->gl && !qa_gl_surface_begin(owner->gl,&owner->gl_surface,error)) return false;
    if (!qa_display_surface_prepare(owner->original,settings,&owner->surface,error)) return false;
    owner->candidate=qa_display_surface_candidate(owner->surface);
    if (!qa_display_surface_stage(owner->surface,error) ||
        !qa_display_info_get(owner->candidate,&owner->observed,error)) return false;
    if (owner->gl) {
        if (!qa_gl_surface_prepare(owner->gl_surface,owner->candidate,gamma,error)) return false;
    } else if (!qa_cpu_surface_prepare(owner->cpu,owner->observed.drawable_width,
        owner->observed.drawable_height,gamma,qa_display_present_cpu,owner->candidate,
        &owner->cpu_surface,error)) return false;
    if (!frontend_input_settings_window_stage(input,owner->surface,error)) return false;
    owner->prepared=true;
    return frontend_shared_video_ready(owner,error);
}
bool frontend_shared_video_ready(const frontend_shared_video *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->prepared || owner->published || owner->rolling_back ||
        owner->frontend->input_settings!=owner->input ||
        !frontend_input_settings_ready(owner->input,error))
        return fail(error,"Prepared video has no current complete physical handoff");
    return qa_display_surface_ready(owner->surface,error) &&
        (owner->gl?qa_gl_surface_ready(owner->gl_surface,error):qa_cpu_surface_ready(owner->cpu_surface,error));
}
bool frontend_shared_video_configuration(const frontend_shared_video *owner,
    qa_display_info *out,qa_error *error)
{
    if (!out || !frontend_shared_video_ready(owner,error)) return false;
    *out=owner->observed; return true;
}
void frontend_shared_video_publish(frontend_shared_video *owner)
{
    if (owner->gl) qa_gl_surface_publish(owner->gl_surface);
    else qa_cpu_surface_publish(owner->cpu_surface);
    qa_display_surface_publish(&owner->surface,&owner->frontend->display,&owner->retired);
    owner->frontend->width=owner->observed.drawable_width;
    owner->frontend->height=owner->observed.drawable_height;
    owner->published=true;
}
bool frontend_shared_video_finish(frontend_shared_video **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_video *owner=*in;
    if (!current(owner,error) || !owner->published)
        return fail(error,"Video retirement requires its actual published native endpoint");
    if (owner->gl_surface && !qa_gl_surface_retire(&owner->gl_surface,error)) return false;
    if (owner->cpu_surface && !qa_cpu_surface_retire(&owner->cpu_surface,error)) return false;
    qa_display_destroy(owner->retired);
    free(owner); *in=NULL; return true;
}
bool frontend_shared_video_rollback(frontend_shared_video *owner,qa_error *error)
{
    if (!current(owner,error) || owner->published)
        return fail(error,"Video rollback requires its retained unpublished parents");
    owner->rolling_back=true;
    if (owner->surface && !qa_display_surface_rollback(owner->surface,error)) return false;
    if (owner->gl_surface && !qa_gl_surface_abort(&owner->gl_surface,error)) return false;
    if (owner->cpu_surface && !qa_cpu_surface_abort(&owner->cpu_surface,error)) return false;
    owner->rolled_back=true; return true;
}
bool frontend_shared_video_abort(frontend_shared_video **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_video *owner=*in;
    if (!current(owner,error) || owner->published || !owner->rolled_back ||
        !qa_input_platform_settings_idle(owner->platform))
        return fail(error,"Video abort retains both windows until actual input cleanup completes");
    if (owner->surface && !qa_display_surface_abort(&owner->surface,error)) return false;
    free(owner); *in=NULL; return true;
}
