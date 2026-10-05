#include "qa/material_source_scratch.h"
#include "internal.h"
#include "campaign_cinematic.h"
#include "system_cinematic.h"
#include "qa/audio_save.h"
#include "campaign.h"
#include "config_store.h"
#include "cinematic_captions.h"
#include "ui_features.h"
#include "shared_render_controls.h"
#include <ctype.h>

typedef struct cinematic_request {
    qa_command_context command;
    qa_vfs *files;
    char *path, *script;
    uint32_t seat;
    uint64_t travel_revision;
    qa_actor_owner travel_provider;
    bool loop, hold;
} cinematic_request;
struct frontend_cinematic {
    qa_frontend *frontend;
    cinematic_request request,pending_request;
    qa_media_library *library;
    qa_cinematic_asset *asset;
    qa_cinematic *movie;
    double clock_ms;
    bool pending, stopping, busy, focus_paused, complete;
};
static char *copy_text(const char *text,qa_error *error)
{
    size_t length=strlen(text); char *copy=length<SIZE_MAX?malloc(length+1):NULL;
    if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic request"); return NULL; }
    memcpy(copy,text,length+1); return copy;
}
static bool suffix(const char *text,const char *extension)
{
    size_t length=strlen(text),size=strlen(extension);
    if (length<size) return false;
    for (size_t i=0;i<size;++i)
        if (tolower((unsigned char)text[length-size+i])!=(unsigned char)extension[i]) return false;
    return true;
}
static bool equal_folded(const char *text,const char *expected)
{ return strlen(text)==strlen(expected) && suffix(text,expected); }
static char *resource_path(const char *name,qa_error *error)
{
    if (!name || !*name || name[0]=='/' || strchr(name,'\\')) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic resource path"); return NULL;
    }
    for (const char *part=name;*part;) {
        const char *end=strchr(part,'/'); size_t size=end?(size_t)(end-part):strlen(part);
        if (!size || (size==1 && part[0]=='.') || (size==2 && part[0]=='.' && part[1]=='.') ||
            (end && !end[1])) { frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic resource component"); return NULL; }
        part=end?end+1:part+size;
    }
    const char *leaf=strrchr(name,'/'); leaf=leaf?leaf+1:name;
    bool extension=strrchr(leaf,'.')!=NULL;
    const char *prefix=!strncmp(name,"video/",6) || !strncmp(name,"pics/",5)?"":suffix(name,".pcx")?"pics/":"video/";
    size_t size=strlen(name),head=strlen(prefix),tail=extension?0:4;
    if (size>SIZE_MAX-head-tail-1) { frontend_fail(error,QA_ERROR_MEMORY,"Cinematic path exceeds address space"); return NULL; }
    char *path=malloc(head+size+tail+1);
    if (!path) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic resource path"); return NULL; }
    memcpy(path,prefix,head); memcpy(path+head,name,size);
    if (tail) memcpy(path+head+size,".roq",4);
    path[head+size+tail]=0;
    if (!suffix(path,".cin") && !suffix(path,".roq") && !suffix(path,".ogv") && !suffix(path,".pcx")) {
        free(path); frontend_fail(error,QA_ERROR_UNSUPPORTED,"Unsupported cinematic format"); return NULL;
    }
    return path;
}
static void request_free(cinematic_request *request)
{
    qa_vfs_destroy(request->files); free(request->path); free(request->script);
    *request=(cinematic_request){0};
}
static bool current(const frontend_cinematic *owner)
{
    uint32_t ordinal;
    if (!owner || !owner->frontend || !owner->frontend->application) return false;
    if (owner->request.travel_revision) {
        qa_frontend *f=owner->frontend;
        qa_application_travel_view travel;
        /* The returned Source route transfers its presentation to this actual
         * ENGINE child and its cloned media view. GAME can then shut down
         * without lending a retired command context to the decoder. */
        return !f->options.dedicated && owner->request.seat<f->options.seats &&
            f->seats && f->seats[owner->request.seat].ui && owner->request.files &&
            qa_application_travel_read(f->application,&travel) &&
            travel.revision==owner->request.travel_revision && travel.provider==owner->request.travel_provider &&
            (travel.target.kind==QA_TRAVEL_CINEMATIC || travel.target.kind==QA_TRAVEL_PICTURE);
    }
    return
        qa_application_command_context_active(owner->frontend->application,&owner->request.command) &&
        frontend_command_seat_read(owner->frontend,&owner->request.command,&ordinal) && ordinal==owner->request.seat;
}
static void release_playback(frontend_cinematic *owner)
{
    qa_cinematic_destroy(owner->movie); owner->movie=NULL;
    frontend_ui_cinematic_clear(owner->frontend,owner->request.seat);
    qa_cinematic_asset_release(owner->asset); owner->asset=NULL;
    qa_media_library_destroy(owner->library); owner->library=NULL;
    owner->clock_ms=0; owner->focus_paused=false; owner->complete=false;
}
static double sample(void *context)
{ return ((frontend_cinematic *)context)->clock_ms; }
static void completed(void *context,qa_cinematic_target target,qa_cinematic_end reason)
{
    frontend_cinematic *owner=context;
    if (target.kind==QA_CINEMATIC_SEAT && target.id.seat==owner->request.seat && reason!=QA_CINEMATIC_STOPPED)
        owner->complete=true;
}
static void diagnostic(void *context,const char *text)
{ frontend_print(((frontend_cinematic *)context)->frontend,text); }
bool frontend_cinematic_idle(const qa_frontend *f)
{ return f && frontend_ui_features_idle(f) && (!f->cinematic || !f->cinematic->busy); }
bool frontend_cinematic_running(const qa_frontend *f)
{ return frontend_system_cinematic_running(f) ||
    (f && f->cinematic && f->cinematic->movie && current(f->cinematic)); }
bool frontend_cinematic_capture_ready(const qa_frontend *f)
{ return f && !f->cinematic; }
bool frontend_cinematic_destroy(qa_frontend *f,qa_error *error)
{
    if (!f || !f->cinematic) return true;
    frontend_cinematic *owner=f->cinematic;
    if (!frontend_cinematic_idle(f) || (f->audio && !qa_audio_engine_round_ready(f->audio,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic cleanup requires completed playback and audio callbacks");
    release_playback(owner); request_free(&owner->request); request_free(&owner->pending_request);
    f->cinematic=NULL; free(owner); return true;
}
static bool queue_request(qa_frontend *f,cinematic_request *request,qa_error *error)
{
    frontend_cinematic *owner=f->cinematic;
    if (!owner) {
        owner=calloc(1,sizeof(*owner));
        if (!owner) { request_free(request); return frontend_fail(error,QA_ERROR_MEMORY,"Allocating fullscreen cinematic owner"); }
        owner->frontend=f; f->cinematic=owner;
    }
    if (owner->pending) request_free(&owner->pending_request);
    owner->pending_request=*request; *request=(cinematic_request){0};
    owner->pending=true; owner->stopping=false;
    return true;
}
bool frontend_cinematic_travel(qa_frontend *f,const qa_application_travel_view *travel,qa_error *error)
{
    qa_application_travel_view actual;
    if (!f || !travel || !travel->revision || f->options.dedicated || f->capture || f->source_restoring ||
        !f->ui_images || !f->display || !frontend_cinematic_idle(f) ||
        (travel->target.kind!=QA_TRAVEL_CINEMATIC && travel->target.kind!=QA_TRAVEL_PICTURE) ||
        !qa_application_travel_read(f->application,&actual) || actual.revision!=travel->revision ||
        actual.provider!=travel->provider || actual.target.kind!=travel->target.kind ||
        strcmp(actual.target.name,travel->target.name))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel requires its actual returned media route");
    frontend_cinematic *owner=f->cinematic;
    if (owner && ((owner->pending && owner->pending_request.travel_revision==travel->revision) ||
        (owner->request.travel_revision==travel->revision && current(owner)))) return true;
    qa_application_startup_source authority; bool present=false;
    if (!frontend_config_store_primary_server_read(f->config_store,&authority,&present,error)) return false;
    bool parked=present && frontend_config_store_parked_current(f->config_store,&authority);
    if (!present || authority.scope.provider!=travel->provider ||
        (!parked && authority.scope.kind!=QA_APPLICATION_CONSOLE_Q2_GAME && authority.scope.kind!=QA_APPLICATION_CONSOLE_NATIVE_Q2))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel lost its actual Q2 GAME authority");
    cinematic_request request={.command=authority.command,.travel_revision=travel->revision,
        .travel_provider=travel->provider};
    if (!(parked?frontend_config_store_parked_recipient(f->config_store,&authority,&request.seat):
        frontend_command_seat_read(f,&request.command,&request.seat)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel has no physical local recipient");
    if (request.command.script) {
        request.script=copy_text(request.command.script,error);
        if (!request.script) return false;
        request.command.script=request.script;
    }
    request.path=resource_path(travel->target.name,error);
    qa_vfs *files=parked?authority.descriptor->content:
        qa_application_context_files(f->application,&request.command,NULL);
    if (!request.path || !files || !(request.files=qa_vfs_clone(files,error))) { request_free(&request); return false; }
    return queue_request(f,&request,error);
}
bool frontend_cinematic_command(qa_frontend *f,const qa_command_invocation *command,qa_error *error)
{
    if (!f || !f->application || !command || !command->argc || !command->argv || f->capture ||
        f->options.dedicated || !f->ui_images || !f->display)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic requires its current local presentation");
    frontend_cinematic *owner=f->cinematic;
    if ((owner && owner->busy) || !frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic callback or retained UI child is active");
    if (!strcmp(command->argv[0],"cinematicpause")) {
        if (!owner || !owner->movie || !current(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"No cinematic is playing");
        return qa_cinematic_pause(owner->movie,qa_cinematic_status(owner->movie)!=QA_MEDIA_PAUSED,error);
    }
    if (!strcmp(command->argv[0],"stopcinematic")) {
        if (!owner || (!owner->movie && !owner->pending)) return frontend_fail(error,QA_ERROR_ARGUMENT,"No cinematic is playing");
        owner->stopping=true; return true;
    }
    if (strcmp(command->argv[0],"cinematic") || command->argc<2)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Usage: cinematic <name> [loop|hold]");
    cinematic_request request={0};
    if (!qa_application_capture_command_context(f->application,&command->context,&request.command,error)) return false;
    if (!frontend_command_seat_read(f,&request.command,&request.seat))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic request has no admitted physical local recipient");
    request.path=resource_path(command->argv[1],error);
    if (!request.path) return false;
    if (command->context.script) {
        request.script=copy_text(command->context.script,error);
        if (!request.script) { request_free(&request); return false; }
        request.command.script=request.script;
    }
    qa_vfs *files=qa_application_context_files(f->application,&request.command,NULL);
    if (!files || !(request.files=qa_vfs_clone(files,error))) { request_free(&request); return false; }
    const char *mode=command->argc>2?command->argv[2]:"";
    request.loop=mode[0]=='2' || !strcmp(mode,"loop");
    const char *leaf=strrchr(request.path,'/'); leaf=leaf?leaf+1:request.path;
    request.hold=mode[0]=='1' || !strcmp(mode,"hold") || equal_folded(leaf,"end.roq") || equal_folded(leaf,"demoend.roq");
    return queue_request(f,&request,error);
}
static bool prepare(frontend_cinematic *owner,qa_error *error)
{
    qa_frontend *f=owner->frontend;
    if (!current(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic preparation belongs to a retired request");
    owner->library=qa_media_library_create(f->ui_images,error);
    if (!owner->library) return false;
    qa_error opened={0};
    bool ok=qa_media_library_load(owner->library,owner->request.files,owner->request.path,&owner->asset,&opened);
    if (!ok && opened.code==QA_ERROR_NOT_FOUND && suffix(owner->request.path,".cin")) {
        size_t size=strlen(owner->request.path);
        memcpy(owner->request.path+size-4,".ogv",4);
        ok=qa_media_library_load(owner->library,owner->request.files,owner->request.path,&owner->asset,&opened);
    }
    if (!ok) { if (error) *error=opened; return false; }
    qa_audio_engine *staging=NULL;
    if (f->audio) {
        qa_audio_engine_options options; frontend_audio_engine_options(f,&options);
        options.sample_rate=qa_audio_engine_rate(f->audio); options.observer=NULL; options.observer_user=NULL;
        if (!qa_audio_engine_create(&options,&staging,error)) return false;
    }
    qa_cinematic_options options={.clock={.context=owner,.sample=sample},
        .target={.kind=QA_CINEMATIC_SEAT,.id.seat=owner->request.seat},
        .loop=owner->request.loop,.hold=owner->request.hold,.silent=!f->audio,
        .audio=staging,.audio_bus=QA_FRONTEND_COMMAND_OWNER,.gain=1,
        .audio_audience={.kind=QA_CINEMATIC_AUDIO_WORLD},.context=owner,
        .complete=completed,.diagnostic=diagnostic};
    qa_cinematic_source source=qa_cinematic_asset_source(owner->asset);
    ok=qa_cinematic_create(&source,&options,&owner->movie,error);
    if (ok) ok=frontend_ui_cinematic_prepare(f,owner->request.files,owner->request.path,owner->request.seat,error);
    qa_audio_stream_cut *cut=NULL;
    if (ok) ok=qa_cinematic_audio_rebind_ready(owner->movie,staging,QA_FRONTEND_COMMAND_OWNER,error);
    if (ok) ok=current(owner) && (!f->audio || qa_audio_engine_round_ready(f->audio,error)) &&
        (!f->device || qa_audio_device_round_ready(f->device,error));
    qa_media_status status=owner->movie?qa_cinematic_status(owner->movie):QA_MEDIA_STOPPED;
    if (ok && f->audio && status!=QA_MEDIA_ENDED && status!=QA_MEDIA_STOPPED) {
        ok=qa_audio_engine_stream_cut_prepare(f->audio,staging,QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1,&cut,error);
        if (ok && (!current(owner) || !qa_audio_engine_stream_cut_current(cut)))
            ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic activation lost its actual prepared owners");
        if (ok) {
            qa_audio_engine_stream_cut_publish(cut);
            qa_cinematic_audio_rebind(owner->movie,f->audio,QA_FRONTEND_COMMAND_OWNER);
            qa_audio_device_clear(f->device);
            frontend_source_audio_stopped(f); frontend_campaign_audio_stopped(f);
        }
    }
    if (ok && !cut) qa_cinematic_audio_rebind(owner->movie,f->audio,QA_FRONTEND_COMMAND_OWNER);
    if (!ok) {
        qa_cinematic_destroy(owner->movie); owner->movie=NULL;
    }
    qa_audio_engine_stream_cut_destroy(cut); qa_audio_engine_destroy(staging);
    return ok;
}
static bool finish(frontend_cinematic *owner,qa_error *error)
{
    if (!owner->complete || !current(owner)) return true;
    if (owner->request.travel_revision) {
        if (!qa_application_complete_travel(owner->frontend->application,owner->request.travel_revision,error)) return false;
        owner->complete=false; return true;
    }
    owner->complete=false;
    qa_frontend *f=owner->frontend;
    if (frontend_network_remote(f)) return true;
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
    const qa_launch_instance *instance=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
    const qa_product *product=instance?qa_catalog_product(qa_launch_snapshot_catalog(publication),instance->selection.product):NULL;
    if (!product || product->family!=QA_GAME_Q3) return true;
    qa_application_q3_campaign source;
    if (!qa_application_q3_campaign_read(f->application,0,&source,error)) return false;
    qa_application_startup_source authority; bool present=false;
    if (!frontend_config_store_primary_server_read(f->config_store,&authority,&present,error)) return false;
    if (!present || authority.scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME ||
        authority.scope.provider!=source.source_owner || authority.console!=source.console ||
        authority.cvars!=source.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic completion lost its actual GAME command authority");
    qa_command_context command=authority.command;
    command.script="cinematic";
    const qa_cvar_view *next=qa_cvars_find(source.cvars,"nextmap");
    if (!next || !next->value || !*next->value) return true;
    size_t size=strlen(next->value); char *text=size<=SIZE_MAX-2?malloc(size+2):NULL;
    if (!text) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic nextmap command");
    memcpy(text,next->value,size); text[size]='\n'; text[size+1]=0;
    bool ok=qa_application_q3_campaign_current(f->application,&source) &&
        qa_console_append(source.console,&command,text,error);
    free(text);
    return ok && qa_cvars_set(source.cvars,"nextmap","",true,error);
}
bool frontend_cinematic_drain(qa_frontend *f,qa_error *error)
{
    if (f && !frontend_system_cinematic_drain(f,error)) return false;
    if (!f || !f->cinematic) return true;
    frontend_cinematic *owner=f->cinematic;
    if (owner->busy || f->capture || f->preparing || f->round || !frontend_ui_features_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic drain requires completed callbacks");
    if (owner->pending && !owner->stopping && !frontend_system_cinematic_stop_all(f,error)) return false;
    owner->busy=true; bool ok=true;
    if (owner->stopping) {
        release_playback(owner); owner->pending=false;
    } else if (owner->pending) {
        release_playback(owner); request_free(&owner->request);
        owner->request=owner->pending_request; owner->pending_request=(cinematic_request){0};
        owner->pending=false; ok=prepare(owner,error);
        if (!ok) release_playback(owner);
    } else if (!current(owner)) {
        release_playback(owner);
    } else if (owner->complete) { ok=finish(owner,error); if (ok) release_playback(owner); }
    owner->busy=false;
    if (ok && !owner->movie && !owner->pending) return frontend_cinematic_destroy(f,error);
    return ok;
}
bool frontend_cinematic_input(qa_frontend *f,uint32_t seat,qa_input_focus focus,
    const qa_input_event *event,bool *handled,qa_error *error)
{
    if (!f || !event || !handled) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic input event");
    if (!frontend_system_cinematic_input(f,seat,focus,event,handled,error)) return false;
    if (*handled) return true;
    *handled=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || owner->request.seat!=seat || !current(owner) || focus!=QA_INPUT_GAME) return true;
    if (!frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic input retains its actual UI child");
    *handled=true;
    bool press=event->down && ((event->kind==QA_INPUT_EVENT_KEY && !event->repeat) || event->kind==QA_INPUT_EVENT_BUTTON);
    if (!press) return true;
    double elapsed,source; uint64_t loop;
    if (!qa_cinematic_time(owner->movie,&elapsed,&source,&loop,error)) return false;
    if (elapsed>1000 || (qa_cinematic_status(owner->movie)==QA_MEDIA_HELD && owner->clock_ms>1000))
        return qa_cinematic_end_playback(owner->movie,QA_CINEMATIC_SKIPPED,error);
    return true;
}
bool frontend_cinematic_view_current(const qa_frontend *f,const qa_vfs *files,const char *path,uint32_t seat)
{
    const frontend_cinematic *owner=f?f->cinematic:NULL;
    return owner && owner->frontend==f && owner->movie && files && path &&
        owner->request.files==files && owner->request.seat==seat && owner->request.path &&
        !strcmp(owner->request.path,path) && current(owner);
}
bool frontend_cinematic_view_read(qa_frontend *f,frontend_cinematic_view *out,bool *found,qa_error *error)
{
    if (!f || !out || !found) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic view request");
    *found=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || !current(owner)) return true;
    frontend_cinematic_view view={.files=owner->request.files,.path=owner->request.path,
        .seat=owner->request.seat,.viewport={0,0,f->width,f->height},.status=qa_cinematic_status(owner->movie)};
    if (!qa_cinematic_time(owner->movie,&view.elapsed_ms,&view.source_ms,&view.loop,error)) return false;
    *out=view; *found=true; return true;
}
bool frontend_cinematic_focus(qa_frontend *f,qa_cinematic *movie,bool *paused,bool *obscured,qa_error *error)
{
    *obscured=false;
    for (uint32_t i=0;i<f->options.seats;++i) {
        qa_ui_state ui;
        if (!qa_ui_state_read(f->seats[i].ui,&ui,error)) return false;
        *obscured|=qa_input_seat_focus(f->seats[i].input)==QA_INPUT_CONSOLE || ui.depth!=0;
    }
    if (*obscured && qa_cinematic_status(movie)==QA_MEDIA_PLAYING) {
        if (!qa_cinematic_pause(movie,true,error)) return false;
        *paused=true;
    } else if (!*obscured && *paused) {
        if (!qa_cinematic_pause(movie,false,error)) return false;
        *paused=false;
    }
    return true;
}
bool frontend_cinematic_frame(qa_frontend *f,uint64_t elapsed_ns,bool *rendered,qa_error *error)
{
    if (!f || !rendered) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic frame");
    if (frontend_system_cinematic_running(f))
        return frontend_system_cinematic_frame(f,elapsed_ns,rendered,error);
    *rendered=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || !current(owner)) return true;
    if (!frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic frame retains its actual UI child");
    double duration=(double)elapsed_ns/1000000.0;
    if (!isfinite(owner->clock_ms+duration)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic clock overflow");
    owner->clock_ms+=duration;
    bool obscured;
    if (!frontend_cinematic_focus(f,owner->movie,&owner->focus_paused,&obscured,error)) return false;
    if (obscured) return true;
    bool ready;
    if (!frontend_display_ready(f,&ready,error)) return false;
    if (!ready) return true;
    qa_scene_frame_reset(&f->frame,f->frame_number);
    bool blank;
    if (!qa_scene_frame_material_order(&f->frame,f->order,error) ||
        !qa_cinematic_fullscreen(owner->movie,QA_CINEMATIC_GAME,(qa_scene_rect){0,0,f->width,f->height},
            f->ui_images,&f->frame,&blank,error)) return false;
    if (!blank) {
        frontend_cinematic_view view; bool found=false;
        if (!frontend_cinematic_view_read(f,&view,&found,error) || !found ||
            !frontend_ui_cinematic_draw(f,view.files,view.path,view.seat,view.elapsed_ms,view.source_ms,
                view.loop,view.status,view.viewport,&f->frame,error)) return false;
    }
    if (!frontend_render_controls_live(f,error)) return false;
    if (f->frame.source_backend && f->frame.source_skip_backend) {
        if (f->frame.source_pending &&
            !qa_material_source_frame_end(f->frame.source_pending,&f->frame,false,error)) return false;
        *rendered=true; return true;
    }
    bool ok=frontend_frame_present(f,error);
    if (ok) *rendered=true;
    return ok;
}
