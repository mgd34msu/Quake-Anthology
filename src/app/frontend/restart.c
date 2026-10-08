#include "restart.h"
#include "capture.h"
#include "q3_color_policy.h"
#include "input_shutdown.h"
#include "qa/console_cvar_observer.h"
#include <math.h>
#include <stdio.h>

typedef struct frontend_video_attempt {
    qa_display *candidate,*previous;
    qa_gl_renderer *gl,*previous_gl;
    qa_cpu_renderer *cpu,*previous_cpu;
    void *ticket;
    qa_display_info info;
    qa_display_backend backend;
    float gamma;
    bool input_attempted,published,color_retired,reopened,native_started,release_started;
    qa_error failure;
} frontend_video_attempt;
struct frontend_restart {
    frontend_restart_options options;
    qa_restart_controls *controls;
    qa_console *console;
    qa_command_context command;
    char *script;
    qa_display_backend backend;
    uint64_t generation;
    frontend_video_attempt *attempt;
    bool video_requested,running,registered,input_registered,releasing;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static float number(const frontend_restart *owner,const char *name,float fallback)
{
    const qa_cvar_view *value=qa_cvars_find(owner->options.cvars,name);
    return value?(float)value->number:fallback;
}
static bool display_options(frontend_restart *owner,qa_display_backend backend,qa_display_options *options,
    float *gamma,int *interval,qa_error *error)
{
    qa_frontend *f=owner->options.frontend; qa_display_info info;
    if (!qa_display_info_get(f->display,&info,error)) return false;
    *options=f->options.display; options->backend=backend;
    float width=number(owner,"r_customwidth",0),height=number(owner,"r_customheight",0);
    if (width==0) width=(float)(f->cpu?f->width:info.logical_width);
    if (height==0) height=(float)(f->cpu?f->height:info.logical_height);
    float fullscreen=number(owner,"r_fullscreen",info.fullscreen!=QA_DISPLAY_WINDOWED);
    float swap=number(owner,"r_swapInterval",0); *gamma=number(owner,"r_gamma",f->options.gamma);
    if (!isfinite(width) || !isfinite(height) || floorf(width)!=width || floorf(height)!=height ||
        width<64 || width>16384 || height<64 || height>16384 ||
        (fullscreen!=0 && fullscreen!=1) || (swap!=0 && swap!=1) || !isfinite(*gamma) || *gamma<.5f || *gamma>3)
        return fail(error,QA_ERROR_ARGUMENT,"Restart display settings require valid size, fullscreen, brightness and swap interval");
    options->width=(uint32_t)width; options->height=(uint32_t)height;
    options->fullscreen=fullscreen==1?QA_DISPLAY_DESKTOP:QA_DISPLAY_WINDOWED;
    options->hidden=true; *interval=(int)swap; return true;
}
static bool retire_surface(qa_display *display,qa_gl_renderer *gl,qa_cpu_renderer *cpu,qa_error *error)
{
    if (gl && !qa_display_make_current(display,error)) return false;
    qa_gl_destroy(gl); qa_cpu_destroy(cpu); qa_display_destroy(display);
    return true;
}
static bool video_cleanup(frontend_restart *owner,bool cancel,qa_error *error)
{
    qa_frontend *f=owner->options.frontend; frontend_video_attempt *a=owner->attempt;
    if (!a) return true;
    if (!a->native_started && a->release_started && f->input_shutdown)
        return fail(error,QA_ERROR_ARGUMENT,"Video restart retains its actual ALL input release history");
    if (!a->published) {
        if (a->input_attempted) {
            if (!qa_input_platform_window(f->input,a->previous,(double)f->wall_time_ns/1000000.0,error)) return false;
            a->input_attempted=false;
        }
        if (a->previous_gl && !qa_display_make_current(a->previous,error)) return false;
        if (a->color_retired && !frontend_q3_source_color_ensure(f,error)) return false;
        a->color_retired=false;
    } else if (!a->reopened) {
        if (a->color_retired && !frontend_q3_source_color_ensure(f,error)) return false;
        if (!cancel) {
            if (!owner->options.reopen_video(owner->options.context,a->ticket,error)) return false;
            a->reopened=true;
        }
    }
    if (a->ticket && !(a->reopened?owner->options.finish_video:owner->options.abort_video)
        (owner->options.context,&a->ticket,error)) return false;
    if (a->ticket) return fail(error,QA_ERROR_ARGUMENT,"Video cleanup retained its source owner");
    if (!a->published) {
        if (!retire_surface(a->candidate,a->gl,a->cpu,error)) return false;
        a->candidate=NULL; a->gl=NULL; a->cpu=NULL;
    } else {
        /* Reopen may fail after transfer. Its new surface remains the actual
         * frontend owner; old native parents retire only after child cleanup. */
        if (!retire_surface(a->previous,a->previous_gl,a->previous_cpu,error)) return false;
        a->previous=NULL; a->previous_gl=NULL; a->previous_cpu=NULL;
    }
    if (f->gl && !qa_display_make_current(f->display,error)) return false;
    qa_error failure=a->failure;
    free(a); owner->attempt=NULL; ++owner->generation;
    if (!cancel && failure.code==QA_OK) frontend_console_print(f,&owner->command,owner->backend==QA_DISPLAY_CPU?
        "Renderer restarted (cpu).\n":"Renderer restarted (gl).\n");
    free(owner->script); owner->script=NULL; owner->command=(qa_command_context){0};
    if (failure.code!=QA_OK) { if (error) *error=failure; return false; }
    return true;
}
static bool video_continue(frontend_restart *owner,qa_error *error)
{
    qa_frontend *f=owner->options.frontend; frontend_video_attempt *a=owner->attempt;
    if (!a->native_started) {
        bool complete=false;
        owner->releasing=true;
        bool ok=owner->options.current(owner->options.context,&owner->command,error);
        if (ok && !a->release_started) {
            a->release_started=true;
            ok=frontend_input_shutdown_prepare(f,(double)f->wall_time_ns/1000000.0,&f->input_shutdown,error);
        }
        if (ok) ok=frontend_input_shutdown_advance(f->input_shutdown,&complete,error);
        if (ok && complete) ok=frontend_input_shutdown_destroy(&f->input_shutdown,error);
        owner->releasing=false;
        if (!ok) {
            if (a->failure.code==QA_OK) {
                if (error && error->code!=QA_OK) a->failure=*error;
                else qa_error_set(&a->failure,QA_ERROR_ARGUMENT,0,"Video restart input release failed");
            }
            return f->input_shutdown?false:video_cleanup(owner,false,error);
        }
        if (!complete) return true;
        a->native_started=true;
    }
    qa_display_options options={0}; int interval=1;
    bool ok=owner->options.current(owner->options.context,&owner->command,error) &&
        display_options(owner,a->backend,&options,&a->gamma,&interval,error);
    if (ok) a->candidate=qa_display_create(&options,error);
    if (ok) ok=a->candidate && qa_display_info_get(a->candidate,&a->info,error);
    if (ok && a->backend==QA_DISPLAY_CPU) {
        qa_cpu_options renderer; qa_cpu_options_default(&renderer);
        renderer.width=options.width; renderer.height=options.height; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        renderer.present=qa_display_present_cpu; renderer.present_context=a->candidate;
        a->cpu=qa_cpu_create(&renderer,error); ok=a->cpu && qa_cpu_set_gamma(a->cpu,a->gamma,error);
    } else if (ok) {
        qa_gl_options renderer; qa_gl_options_default(&renderer); renderer.display=a->candidate; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        a->gl=qa_gl_create(&renderer,error); ok=a->gl && qa_gl_set_gamma(a->gl,a->gamma,error) && qa_display_set_swap_interval(a->candidate,interval,error);
    }
    if (ok && f->gl) ok=qa_display_make_current(f->display,error);
    if (ok) ok=owner->options.prepare_video(owner->options.context,&a->ticket,error) &&
        owner->options.current(owner->options.context,&owner->command,error) && owner->options.validate_video(owner->options.context,a->ticket,error);
    if (ok && f->source_color) {
        ok=frontend_q3_source_color_retire(f,error); a->color_retired=ok;
    }
    double now=(double)f->wall_time_ns/1000000.0;
    if (ok && f->input) { a->input_attempted=true; ok=qa_input_platform_window(f->input,a->candidate,now,error); }
    if (ok) ok=qa_display_set_visible(a->candidate,!f->options.display.hidden,error);
    if (ok && a->gl) ok=qa_display_make_current(a->candidate,error);
    if (ok) {
        f->display=a->candidate; f->gl=a->gl; f->cpu=a->cpu;
        f->width=a->cpu?options.width:a->info.drawable_width;
        f->height=a->cpu?options.height:a->info.drawable_height;
        f->observed_display=a->info;
        f->options.display.backend=a->backend; f->options.gamma=a->gamma;
        f->options.display.width=options.width; f->options.display.height=options.height;
        f->options.display.fullscreen=options.fullscreen;
        a->candidate=NULL; a->gl=NULL; a->cpu=NULL; a->published=true;
        ok=(!a->color_retired || frontend_q3_source_color_ensure(f,error)) &&
            owner->options.reopen_video(owner->options.context,a->ticket,error);
        a->reopened=ok;
    }
    if (!ok) {
        if (error && error->code!=QA_OK) a->failure=*error;
        else qa_error_set(&a->failure,QA_ERROR_ARGUMENT,0,"Physical video restart failed");
    }
    /* A entered reopen failure keeps the real continuation and both native
     * parents. The next driver retries that ticket; destruction checks abort. */
    return !ok && a->published?false:video_cleanup(owner,false,error);
}
static bool video(void *context,qa_error *error)
{
    frontend_restart *owner=context; qa_frontend *f=owner->options.frontend;
    if (!owner->video_requested || owner->attempt || !f->display || f->input_shutdown || owner->generation==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Video restart lacks its retained actual local request");
    frontend_video_attempt *a=calloc(1,sizeof(*a));
    if (!a) return fail(error,QA_ERROR_MEMORY,"Retaining physical video restart");
    owner->attempt=a; owner->video_requested=false; a->backend=owner->backend;
    a->previous=f->display; a->previous_gl=f->gl; a->previous_cpu=f->cpu;
    return video_continue(owner,error);
}
static bool input(void *context,qa_error *error)
{
    frontend_restart *owner=context; qa_frontend *f=owner->options.frontend;
    if (!f->input) return fail(error,QA_ERROR_UNSUPPORTED,"Input restart has no actual local input device owner");
    double now=(double)f->wall_time_ns/1000000.0;
    for (unsigned i=0;i<f->options.seats;++i)
        if (!qa_input_seat_release(f->seats[i].input,now,error)) return false;
    return qa_input_platform_restart(f->input,now,error);
}
static bool audio(void *context,qa_error *error)
{
    frontend_restart *owner=context; qa_frontend *f=owner->options.frontend;
    if (!f->device) return fail(error,QA_ERROR_UNSUPPORTED,"Audio restart has no actual native output device owner");
    qa_audio_device_options options=qa_audio_device_requested_configuration(f->device);
    float rate=number(owner,"s_outputRate",(float)options.format.sample_rate);
    float bits=number(owner,"s_outputBits",(float)options.format.sample_bits);
    float channels=number(owner,"s_outputChannels",(float)options.format.channels);
    if (!isfinite(rate) || rate<8000 || rate>192000 || floorf(rate)!=rate ||
        (bits!=8 && bits!=16) || (channels!=1 && channels!=2))
        return fail(error,QA_ERROR_ARGUMENT,"Audio restart requires a valid physical output format");
    options.format=(qa_audio_output_format){(uint32_t)rate,(unsigned)channels,(unsigned)bits};
    size_t maximum=UINT32_MAX/(options.format.channels*(options.format.sample_bits/8));
    if (maximum>SIZE_MAX/(2*sizeof(int16_t))) maximum=SIZE_MAX/(2*sizeof(int16_t));
    if (options.maximum_queued_frames>maximum)
        return fail(error,QA_ERROR_ARGUMENT,"Audio restart format exceeds the retained native queue limit");
    /* select deliberately preserves an unchanged live device. A source
     * snd_restart closes its actual native output while retaining queued PCM,
     * then opens the selected format again. */
    qa_audio_device_detach(f->device);
    if (!qa_audio_device_select(f->device,&options,error)) return false;
    f->audio_output_format=qa_audio_device_requested_configuration(f->device).format;
    return true;
}
frontend_restart *frontend_restart_create(const frontend_restart_options *options,qa_error *error)
{
    if (!options || !options->frontend || !options->cvars || !options->current || !options->save_context ||
        !options->prepare_video || !options->validate_video || !options->reopen_video || !options->finish_video || !options->abort_video)
        return fail(error,QA_ERROR_ARGUMENT,"Restart controls require genuine device and source video owners"),NULL;
    frontend_restart *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating frontend restart owner"),NULL;
    owner->options=*options;
    qa_restart_service services[3]={{owner,video,options->latched[0],options->latched_count[0]},
        {owner,input,options->latched[1],options->latched_count[1]}, {owner,audio,options->latched[2],options->latched_count[2]}};
    owner->controls=qa_restart_create(options->cvars,services,error);
    if (!owner->controls) { free(owner); return NULL; }
    return owner;
}
bool frontend_restart_idle(const frontend_restart *owner) { return !owner || (!owner->running && !owner->attempt); }
bool frontend_restart_release_phase(const frontend_restart *owner,const qa_frontend *f)
{ return owner && f && owner->options.frontend==f && f->restart==owner && owner->running && owner->releasing && owner->attempt && !owner->attempt->native_started; }
bool frontend_restart_destroy(frontend_restart *owner,qa_error *error)
{
    if (!owner) return true;
    if (owner->running) return fail(error,QA_ERROR_ARGUMENT,"Restart callback has not returned");
    if (owner->attempt && !video_cleanup(owner,true,error)) return false;
    if (!qa_restart_destroy(owner->controls,error)) return false;
    if (owner->registered) qa_console_unregister(owner->console,"vid_restart",0);
    if (owner->input_registered) qa_console_unregister(owner->console,"in_restart",0);
    free(owner->script); free(owner); return true;
}
static bool video_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_restart *owner=context;
    if (owner->attempt || owner->running)
        return fail(error,QA_ERROR_ARGUMENT,"Video restart still owns its prior physical attempt");
    if (command->context.origin==QA_COMMAND_REMOTE) {
        frontend_console_print(owner->options.frontend,&command->context,"vid_restart is a local client command.\n"); return true;
    }
    if (command->argc>2 || (command->argc==2 && strcmp(command->argv[1],"cpu") && strcmp(command->argv[1],"gl"))) {
        frontend_console_print(owner->options.frontend,&command->context,"vid_restart [cpu|gl]\n"); return true;
    }
    if (!owner->options.current(owner->options.context,&command->context,error)) return false;
    qa_display_info info;
    if (!qa_display_info_get(owner->options.frontend->display,&info,error)) return false;
    char *script=NULL;
    if (command->context.script) { script=malloc(strlen(command->context.script)+1);
        if (!script) return fail(error,QA_ERROR_MEMORY,"Retaining video request script origin");
        strcpy(script,command->context.script); }
    if (!qa_restart_request(owner->controls,QA_RESTART_VIDEO,error)) { free(script); return false; }
    free(owner->script); owner->script=script; owner->command=command->context; owner->command.script=script;
    owner->backend=command->argc==1?info.backend:!strcmp(command->argv[1],"cpu")?QA_DISPLAY_CPU:QA_DISPLAY_OPENGL;
    owner->video_requested=true; return true;
}
static bool input_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    frontend_restart *owner=context;
    if (command->context.origin==QA_COMMAND_REMOTE) {
        frontend_console_print(owner->options.frontend,&command->context,"Device restart is a local client command.\n");
        return true;
    }
    bool staged=false;
    if (owner->options.stage_input &&
        !owner->options.stage_input(owner->options.context,command,&staged,error)) return false;
    return staged || (owner->options.current(owner->options.context,&command->context,error) &&
        qa_restart_request(owner->controls,QA_RESTART_INPUT,error));
}
bool frontend_restart_register(frontend_restart *owner,qa_console *console,qa_error *error)
{
    if (!owner || !console || owner->console || owner->running)
        return fail(error,QA_ERROR_ARGUMENT,"Restart commands already have their physical frontend owner");
    if (!qa_console_register_owned(console,"vid_restart","Restart the client renderer while retaining the current game",0,
        QA_FRONTEND_COMMAND_OWNER,true,video_command,owner,error)) return false;
    owner->console=console; owner->registered=true;
    if (!qa_console_register_owned(console,"in_restart","Restart the actual local input device owner",0,
        QA_FRONTEND_COMMAND_OWNER,true,input_command,owner,error)) {
        qa_console_unregister(console,"vid_restart",0); owner->registered=false; owner->console=NULL; return false;
    }
    owner->input_registered=true;
    if (!qa_restart_register(owner->controls,console,QA_FRONTEND_COMMAND_OWNER,error)) {
        qa_console_unregister(console,"in_restart",0); owner->input_registered=false;
        qa_console_unregister(console,"vid_restart",0); owner->registered=false; owner->console=NULL; return false;
    }
    return true;
}
static bool drain(frontend_restart *owner,bool frame,qa_error *error)
{
    if (!owner->attempt && !owner->video_requested &&
        !qa_restart_pending(owner->controls,QA_RESTART_VIDEO) &&
        !qa_restart_pending(owner->controls,QA_RESTART_INPUT) &&
        !qa_restart_pending(owner->controls,QA_RESTART_AUDIO)) return true;
    qa_frontend *f=owner->options.frontend;
    if (owner->running || f->stepping!=frame || f->preparing || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f) || (!owner->attempt &&
        (!frontend_owners_idle(f) || !frontend_seat_callbacks_idle(f) ||
            !qa_cvars_observer_idle(owner->options.cvars))))
        return true;
    owner->running=true;
    bool ok=true;
    if (owner->attempt) ok=!owner->attempt->native_started?video_continue(owner,error):
        video_cleanup(owner,false,error);
    else while (ok && !owner->attempt && (qa_restart_pending(owner->controls,QA_RESTART_VIDEO) ||
        qa_restart_pending(owner->controls,QA_RESTART_INPUT) || qa_restart_pending(owner->controls,QA_RESTART_AUDIO)))
        ok=qa_restart_drain_one(owner->controls,error);
    owner->running=false;
    /* The lower queue consumes a request before applying its latches. A latch
     * failure retires this request without constructing a video attempt. */
    if (owner->video_requested && !qa_restart_pending(owner->controls,QA_RESTART_VIDEO)) {
        free(owner->script); owner->script=NULL; owner->command=(qa_command_context){0}; owner->video_requested=false;
    }
    return ok;
}
bool frontend_restart_drain(frontend_restart *owner,qa_error *error)
{ return !owner || drain(owner,false,error); }
bool frontend_restart_drain_frame(frontend_restart *owner,qa_error *error)
{ return !owner || drain(owner,true,error); }
static bool fields(qa_source_save_io *io,frontend_restart *owner)
{
    uint8_t magic[4]={'Q','F','R','S'}; uint32_t backend=owner->backend;
    bool stage=owner->options.stage_input!=NULL;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFRS",4) || !qa_source_save_bool(io,&stage) || stage!=(owner->options.stage_input!=NULL) ||
        !qa_source_save_u64(io,&owner->generation) || !qa_source_save_bool(io,&owner->video_requested)) return false;
    if (owner->video_requested) {
        if (!qa_source_save_u32(io,&backend) || backend>QA_DISPLAY_OPENGL ||
            !owner->options.save_context(owner->options.context,io,&owner->command) || owner->command.origin==QA_COMMAND_REMOTE) return false;
        owner->backend=(qa_display_backend)backend;
    }
    return true;
}
bool frontend_restart_checkpoint(const frontend_restart *owner,qa_buffer *out,qa_error *error)
{
    if (!owner || owner->running || owner->attempt || !out || out->data || out->size ||
        qa_restart_pending(owner->controls,QA_RESTART_VIDEO)!=owner->video_requested)
        return fail(error,QA_ERROR_ARGUMENT,"Restart capture requires its returned actual request owner");
    qa_buffer pending={0}; frontend_restart state=*owner; qa_source_save_io io={0};
    bool ok=qa_restart_checkpoint(owner->controls,&pending,error) &&
        qa_source_save_writer(&io,qa_application_session(owner->options.frontend->application),error) && fields(&io,&state);
    size_t count=pending.size;
    if (ok) ok=qa_source_save_count(&io,&count,SIZE_MAX) && qa_source_save_bytes(&io,pending.data,pending.size) && qa_source_save_finish(&io,out);
    qa_buffer_free(&pending); qa_source_save_dispose(&io); return ok;
}
bool frontend_restart_restore(frontend_restart *owner,qa_bytes bytes,qa_error *error)
{
    if (!owner || owner->running || owner->attempt || owner->video_requested || owner->generation || owner->console)
        return fail(error,QA_ERROR_ARGUMENT,"Restart import requires its empty detached controls");
    frontend_restart state=*owner; qa_source_save_io io={0}; size_t count=0;
    bool ok=qa_source_save_reader(&io,qa_application_session(owner->options.frontend->application),bytes,error) && fields(&io,&state) &&
        qa_source_save_count(&io,&count,io.input.size-io.offset);
    qa_bytes pending=ok?(qa_bytes){io.input.data+io.offset,count}:(qa_bytes){0};
    if (ok) { io.offset+=count; ok=qa_source_save_finish(&io,NULL); }
    if (ok && state.video_requested) {
        if (state.command.script) { state.script=malloc(strlen(state.command.script)+1);
            if (!state.script) ok=fail(error,QA_ERROR_MEMORY,"Importing video script origin"); else { strcpy(state.script,state.command.script); state.command.script=state.script; } }
    }
    qa_restart_controls *controls=NULL;
    if (ok) {
        qa_restart_service services[3]={{owner,video,owner->options.latched[0],owner->options.latched_count[0]},
            {owner,input,owner->options.latched[1],owner->options.latched_count[1]},
            {owner,audio,owner->options.latched[2],owner->options.latched_count[2]}};
        controls=qa_restart_create(owner->options.cvars,services,error);
        ok=controls && qa_restart_restore(controls,pending,error) &&
            qa_restart_pending(controls,QA_RESTART_VIDEO)==state.video_requested;
        if (!ok && (!error || error->code==QA_OK)) fail(error,QA_ERROR_FORMAT,"Video request and actual restart queue disagree");
    }
    if (ok) {
        ok=qa_restart_destroy(owner->controls,error);
        if (ok) { owner->controls=controls; controls=NULL; }
    }
    if (ok) { owner->generation=state.generation; owner->video_requested=state.video_requested;
        owner->backend=state.backend; owner->command=state.command; owner->script=state.script; }
    if (!ok) free(state.script);
    qa_restart_destroy(controls,NULL); qa_source_save_dispose(&io); return ok;
}
void frontend_restart_rebind(frontend_restart *owner,qa_frontend *frontend,void *context)
{ if (owner && !owner->running) { owner->options.frontend=frontend; owner->options.context=context; } }
