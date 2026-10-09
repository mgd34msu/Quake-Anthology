#include "shared_video.h"
#include "capture.h"
#include "video_modes_generated.h"
#include <ctype.h>
#include <errno.h>

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
    uint32_t width,height;
    bool prepared,published,rolling_back,rolled_back;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool modelist_token(const char *text,const char *end,qa_cvar_video_mode *mode)
{
    if (end-text==7 && !memcmp(text,"desktop",7)) { mode->desktop=true; return true; }
    char *at; errno=0;
    unsigned long width=strtoul(text,&at,10);
    if (at==text || at>=end || (*at!='x' && *at!='X')) return false;
    const char *height_text=at+1;
    unsigned long height=strtoul(height_text,&at,10),rate=0,depth=0;
    if (at==height_text) return false;
    if (at<end && (*at=='@' || *at==':')) {
        char first=*at; const char *number=at+1;
        unsigned long value=strtoul(number,&at,10);
        if (at==number) return false;
        if (first=='@') rate=value; else depth=value;
        if (at<end && *at==(first=='@'?':':'@')) {
            number=at+1; value=strtoul(number,&at,10);
            if (at==number) return false;
            if (first=='@') depth=value; else rate=value;
        }
    }
    if (errno || at!=end || width<320 || width>8192 || height<240 || height>8192 || rate>1000 || depth>32)
        return false;
    mode->width=(uint32_t)width; mode->height=(uint32_t)height; return true;
}
static bool fullscreen_mode(const qa_cvar_video_query *query,qa_cvar_video_mode *out)
{
    const char *at=query->modelist?query->modelist:"";
    int32_t index=1,desktop=0;
    while (*at) {
        while (isspace((unsigned char)*at)) ++at;
        if (!*at) break;
        const char *end=at;
        while (*end && !isspace((unsigned char)*end)) ++end;
        qa_cvar_video_mode mode={.index=index};
        bool valid=modelist_token(at,end,&mode);
        if (!query->dimensions_to_index && query->index==index) {
            *out=valid?mode:(qa_cvar_video_mode){.index=index,.desktop=true}; return true;
        }
        if (valid && query->dimensions_to_index) {
            if (mode.desktop && !desktop) desktop=index;
            if (!mode.desktop && mode.width==query->width && mode.height==query->height) {
                *out=mode; return true;
            }
        }
        at=end; ++index;
    }
    /* q2repro VID_GetFullscreen falls back to desktop for an absent/invalid token. */
    *out=(qa_cvar_video_mode){.index=query->dimensions_to_index?(desktop?desktop:1):query->index,.desktop=true};
    return true;
}
bool frontend_shared_video_resolve(void *user,const qa_cvar_video_query *query,
    qa_cvar_video_mode *out,qa_error *error)
{
    (void)user;
    bool canonical=!strcmp(query->member,FRONTEND_VIDEO_MODE_NAME),classic=false;
    for (size_t i=0;i<sizeof(frontend_q2_video_members)/sizeof(*frontend_q2_video_members);++i)
        classic|=!strcmp(query->member,frontend_q2_video_members[i]);
    if (query->dialect==QA_RULESET_Q2_RERELEASE && !strcmp(query->member,FRONTEND_VIDEO_FULLSCREEN_MEMBER))
        return fullscreen_mode(query,out);
    if (!canonical && !classic) return fail(error,"Source video mode has no native resolution table");
    size_t count=classic?FRONTEND_Q2_VIDEO_MODE_COUNT:sizeof(frontend_video_modes)/sizeof(*frontend_video_modes);
    *out=(qa_cvar_video_mode){.index=-1,.width=query->width,.height=query->height};
    if (query->dimensions_to_index) {
        for (size_t i=0;i<count;++i)
            if (frontend_video_modes[i].width==query->width && frontend_video_modes[i].height==query->height) {
                out->index=(int32_t)i; break;
            }
        return true;
    }
    if (canonical && query->index==-1) return true;
    /* Q2 VID_GetModeInfo and Q3 R_GetModeInfo reject indices outside their tables. */
    if (query->index<0 || (size_t)query->index>=count) return fail(error,"Video mode index is outside its Source table");
    *out=(qa_cvar_video_mode){.index=query->index,.width=frontend_video_modes[query->index].width,
        .height=frontend_video_modes[query->index].height};
    return true;
}
bool frontend_shared_video_dimensions(const qa_cvars_edit *edit,uint32_t fallback_width,
    uint32_t fallback_height,uint32_t *width,uint32_t *height,qa_error *error)
{
    const qa_cvar_view *mode=qa_cvars_edit_find(edit,FRONTEND_VIDEO_MODE_NAME);
    if (!mode) return fail(error,"Display projection lost its canonical mode");
    qa_cvar_video_query query={.member=FRONTEND_VIDEO_MODE_NAME,.dialect=QA_RULESET_Q3,.index=mode->integer};
    if (query.index==-1) {
        const qa_cvar_view *w=qa_cvars_edit_find(edit,"r_customwidth"),*h=qa_cvars_edit_find(edit,"r_customheight");
        if (!w || !h) return fail(error,"Display projection lost its canonical custom dimensions");
        double x=w->number!=0?(double)w->number:(double)fallback_width;
        double y=h->number!=0?(double)h->number:(double)fallback_height;
        if (!isfinite(x) || !isfinite(y) || floor(x)!=x || floor(y)!=y || x<64 || x>16384 || y<64 || y>16384)
            return fail(error,"Display settings require valid native dimensions");
        query.width=(uint32_t)x; query.height=(uint32_t)y;
    }
    qa_cvar_video_mode selected;
    if (!frontend_shared_video_resolve(NULL,&query,&selected,error)) return false;
    *width=selected.width; *height=selected.height; return true;
}
bool frontend_shared_video_settings(qa_frontend *f,const qa_cvars_edit *edit,
    qa_display_settings *out,bool *changed,qa_error *error)
{
    if (!f || !out || !changed || !f->application || !f->display ||
        !qa_cvars_edit_returned_is(edit,qa_application_cvars(f->application)) ||
        f->stepping || f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f))
        return fail(error,"Display projection requires its returned actual canonical owner");
    static const char *const names[]={"r_customwidth","r_customheight","r_fullscreen","r_swapInterval"};
    const qa_cvar_view *rows[4];
    for (size_t i=0;i<4;++i) {
        rows[i]=qa_cvars_edit_find(edit,names[i]);
        if (!rows[i]) return fail(error,"Display projection lost a canonical declaration");
    }
    qa_display_info info;
    if (!qa_display_info_get(f->display,&info,error)) return false;
    uint32_t current_width=f->cpu?f->width:info.logical_width;
    uint32_t current_height=f->cpu?f->height:info.logical_height;
    uint32_t width,height;
    if (!frontend_shared_video_dimensions(edit,current_width,current_height,&width,&height,error)) return false;
    double fullscreen=rows[2]->number,swap=rows[3]->number;
    if ((fullscreen!=0 && fullscreen!=1) || (f->gl && swap!=0 && swap!=1))
        return fail(error,"Display settings require valid native size, fullscreen and swap interval");
    int actual_swap=0;
    if (f->gl && !qa_display_swap_interval(f->display,&actual_swap,error)) return false;
    /* The source only resizes a currently windowed endpoint. Fullscreen
     * dimensions come from the actual current native window. */
    *out=(qa_display_settings){.width=f->cpu || info.fullscreen==QA_DISPLAY_WINDOWED?(uint32_t)width:info.logical_width,
        .height=f->cpu || info.fullscreen==QA_DISPLAY_WINDOWED?(uint32_t)height:info.logical_height,
        .fullscreen=fullscreen==1?QA_DISPLAY_DESKTOP:QA_DISPLAY_WINDOWED,
        .swap_interval=f->gl?(int)swap:0};
    *changed=out->width!=current_width || out->height!=current_height ||
        out->fullscreen!=info.fullscreen || (f->gl && out->swap_interval!=actual_swap);
    return true;
}
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
    owner->width=settings->width; owner->height=settings->height;
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
    } else if (!qa_cpu_surface_prepare(owner->cpu,owner->width,
        owner->height,gamma,qa_display_present_cpu,owner->candidate,
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
bool frontend_shared_video_ready_is(const frontend_shared_video *owner)
{
    return current(owner,NULL) && owner->prepared && !owner->published && !owner->rolling_back &&
        owner->frontend->input_settings==owner->input && frontend_input_settings_ready_is(owner->input) &&
        qa_display_surface_ready_is(owner->surface,owner->original,owner->candidate) &&
        (owner->gl?qa_gl_surface_ready_is(owner->gl_surface):qa_cpu_surface_ready_is(owner->cpu_surface));
}
qa_display *frontend_shared_video_candidate(const frontend_shared_video *owner)
{
    return frontend_shared_video_ready_is(owner)?owner->candidate:NULL;
}
bool frontend_shared_video_refresh(frontend_shared_video *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->prepared || owner->published || owner->rolling_back)
        return fail(error,"Video refresh requires its actual unpublished prepared surface");
    return !owner->cpu || qa_cpu_surface_refresh(owner->cpu_surface,error);
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
    owner->frontend->width=owner->cpu?owner->width:owner->observed.drawable_width;
    owner->frontend->height=owner->cpu?owner->height:owner->observed.drawable_height;
    owner->frontend->options.display.width=owner->width;
    owner->frontend->options.display.height=owner->height;
    owner->frontend->observed_display=owner->observed;
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
