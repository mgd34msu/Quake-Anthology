#include "shared_gamma.h"
#include "capture.h"
#include <math.h>

struct frontend_shared_gamma {
    qa_frontend *frontend;
    qa_application *application;
    qa_display *display;
    qa_gl_renderer *gl;
    qa_cpu_renderer *cpu;
    qa_gl_surface_ticket *gl_ticket;
    qa_cpu_surface_ticket *cpu_ticket;
    uint32_t width,height;
    float original_gamma;
    qa_display_endpoint ready_endpoint;
    bool prepared,published,ready_receipt;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool current(const frontend_shared_gamma *owner,qa_error *error)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    return (f && f->application==owner->application && f->display==owner->display &&
        f->gl==owner->gl && f->cpu==owner->cpu && f->width==owner->width && f->height==owner->height &&
        !f->stepping && !f->capture && !f->source_restoring && frontend_seat_callbacks_returned(f)) ||
        fail(error,"Prepared brightness lost its actual returned display/renderer parents");
}
bool frontend_shared_gamma_read(const qa_frontend *f,float *out,qa_error *error)
{
    if (!f || !out || (!!f->gl==!!f->cpu))
        return fail(error,"Brightness observation requires its actual installed renderer");
    return f->gl?qa_gl_gamma_read(f->gl,out,error):qa_cpu_gamma_read(f->cpu,out,error);
}
bool frontend_shared_gamma_prepare(qa_frontend *f,float gamma,frontend_shared_gamma **out,qa_error *error)
{
    if (!f || !out || *out || !f->application || !f->display || (!!f->gl==!!f->cpu) ||
        f->stepping || f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f) ||
        !isfinite(gamma) || gamma<.5f || gamma>3)
        return fail(error,"Brightness preparation requires its returned actual display and renderer");
    float original=0;
    if (!frontend_shared_gamma_read(f,&original,error)) return false;
    qa_display_info info={0};
    if (!qa_display_info_get(f->display,&info,error)) return false;
    if (info.drawable_width!=f->width ||
        info.drawable_height!=f->height || info.backend!=(f->gl?QA_DISPLAY_OPENGL:QA_DISPLAY_CPU))
        return fail(error,"Brightness preparation requires unchanged actual drawable dimensions");
    frontend_shared_gamma *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared renderer brightness");
    owner->frontend=f; owner->application=f->application; owner->display=f->display;
    owner->gl=f->gl; owner->cpu=f->cpu; owner->width=f->width; owner->height=f->height;
    owner->original_gamma=original; *out=owner;
    if (owner->gl) {
        if (!qa_gl_gamma_prepare(owner->gl,owner->display,gamma,&owner->gl_ticket,error)) return false;
    } else if (!qa_cpu_gamma_prepare(owner->cpu,gamma,qa_display_present_cpu,owner->display,&owner->cpu_ticket,error)) return false;
    owner->prepared=true;
    return frontend_shared_gamma_ready(owner,error);
}
bool frontend_shared_gamma_ready(frontend_shared_gamma *owner,qa_error *error)
{
    if (owner) owner->ready_receipt=false;
    if (!current(owner,error)) return false;
    if (!owner->prepared || owner->published)
        return fail(error,"Brightness publication requires its current complete renderer ticket");
    float original=0; qa_display_info info={0};
    if (!frontend_shared_gamma_read(owner->frontend,&original,error) ||
        !qa_display_info_get(owner->display,&info,error)) return false;
    if (original!=owner->original_gamma || info.drawable_width!=owner->width ||
        info.drawable_height!=owner->height)
        return fail(error,"Brightness publication lost its retained value or native drawable");
    if (!(owner->gl?qa_gl_surface_ready(owner->gl_ticket,error):qa_cpu_surface_ready(owner->cpu_ticket,error)))
        return false;
    if (!qa_display_endpoint_read(owner->display,&owner->ready_endpoint))
        return fail(error,"Brightness readiness lost its retained native endpoint");
    owner->ready_receipt=true; return true;
}
bool frontend_shared_gamma_ready_is(const frontend_shared_gamma *owner)
{
    return current(owner,NULL) && owner->prepared && !owner->published && owner->ready_receipt &&
        qa_display_endpoint_is(owner->display,&owner->ready_endpoint) &&
        (owner->gl?qa_gl_surface_ready_is(owner->gl_ticket):qa_cpu_surface_ready_is(owner->cpu_ticket));
}
bool frontend_shared_gamma_refresh(frontend_shared_gamma *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->prepared || owner->published)
        return fail(error,"Brightness refresh requires its actual unpublished renderer surface");
    owner->ready_receipt=false;
    return !owner->cpu || qa_cpu_surface_refresh(owner->cpu_ticket,error);
}
void frontend_shared_gamma_publish(frontend_shared_gamma *owner)
{
    if (!owner || !owner->prepared || owner->published) return;
    owner->ready_receipt=false;
    if (owner->gl) qa_gl_surface_publish(owner->gl_ticket);
    else qa_cpu_surface_publish(owner->cpu_ticket);
    owner->published=true;
}
bool frontend_shared_gamma_abort(frontend_shared_gamma **in,qa_error *error)
{
    if (!in) return fail(error,"Invalid retained brightness abort");
    frontend_shared_gamma *owner=*in;
    if (!owner) return true;
    if (!current(owner,error)) return false;
    if (owner->published)
        return fail(error,"Brightness abort requires its unpublished actual parents");
    owner->ready_receipt=false;
    if (owner->gl_ticket && !qa_gl_surface_abort(&owner->gl_ticket,error)) return false;
    if (owner->cpu_ticket && !qa_cpu_surface_abort(&owner->cpu_ticket,error)) return false;
    free(owner); *in=NULL; return true;
}
bool frontend_shared_gamma_finish(frontend_shared_gamma **in,qa_error *error)
{
    if (!in) return fail(error,"Invalid retained brightness retirement");
    frontend_shared_gamma *owner=*in;
    if (!owner) return true;
    if (!current(owner,error)) return false;
    if (!owner->published)
        return fail(error,"Brightness retirement requires its published actual parents");
    if (owner->gl_ticket && !qa_gl_surface_retire(&owner->gl_ticket,error)) return false;
    if (owner->cpu_ticket && !qa_cpu_surface_retire(&owner->cpu_ticket,error)) return false;
    free(owner); *in=NULL; return true;
}
