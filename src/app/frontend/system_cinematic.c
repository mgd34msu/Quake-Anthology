#include "system_cinematic_private.h"
#include "cinematic_captions.h"
#include "campaign.h"
#include "ui_features.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include <ctype.h>
#include <math.h>

static char *copy_text(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *copy=malloc(size);
    if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining source system cinematic text"); return NULL; }
    memcpy(copy,text,size); return copy;
}
static bool suffix(const char *name,const char *extension)
{
    size_t size=strlen(name),tail=strlen(extension);
    if (size<tail) return false;
    for (size_t i=0;i<tail;++i)
        if (tolower((unsigned char)name[size-tail+i])!=(unsigned char)extension[i]) return false;
    return true;
}
bool frontend_system_cinematic_path_valid(const char *path)
{
    if (!path || (strncmp(path,"video/",6) && strncmp(path,"pics/",5)) || strchr(path,'\\') ||
        (!suffix(path,".roq") && !suffix(path,".cin") && !suffix(path,".ogv") && !suffix(path,".pcx"))) return false;
    for (const char *part=path;*part;) {
        const char *end=strchr(part,'/'); size_t size=end?(size_t)(end-part):strlen(part);
        if (!size || (size==1 && part[0]=='.') || (size==2 && part[0]=='.' && part[1]=='.') || (end && !end[1])) return false;
        part=end?end+1:part+size;
    }
    return true;
}
static char *resource_path(const char *name,qa_error *error)
{
    if (!name || !*name || name[0]=='/' || strchr(name,'\\')) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid source system cinematic path"); return NULL;
    }
    for (const char *part=name;*part;) {
        const char *end=strchr(part,'/'); size_t size=end?(size_t)(end-part):strlen(part);
        if (!size || (size==1 && part[0]=='.') || (size==2 && part[0]=='.' && part[1]=='.') || (end && !end[1])) {
            frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid source system cinematic component"); return NULL;
        }
        part=end?end+1:part+size;
    }
    const char *leaf=strrchr(name,'/'); leaf=leaf?leaf+1:name;
    size_t tail=strrchr(leaf,'.')?0:4;
    const char *prefix=!strncmp(name,"video/",6) || !strncmp(name,"pics/",5)?"":suffix(name,".pcx")?"pics/":"video/";
    size_t head=strlen(prefix),size=strlen(name);
    if (size>SIZE_MAX-head-tail-1) { frontend_fail(error,QA_ERROR_MEMORY,"System cinematic path overflow"); return NULL; }
    char *path=malloc(head+size+tail+1);
    if (!path) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining system cinematic path"); return NULL; }
    memcpy(path,prefix,head); memcpy(path+head,name,size);
    if (tail) memcpy(path+head+size,".roq",4);
    path[head+size+tail]=0;
    if (!frontend_system_cinematic_path_valid(path)) {
        free(path); frontend_fail(error,QA_ERROR_UNSUPPORTED,"Unsupported system cinematic format"); return NULL;
    }
    return path;
}
bool frontend_system_cinematic_source_valid(const qa_frontend *f,const frontend_system_cinematic_source *source)
{
    return f && f->application && !f->options.dedicated && f->ui_images && f->display && source &&
        source->identity.source_group && source->identity.source_owner && source->identity.service_owner &&
        source->identity.audio_bus && source->identity.audio_bus!=QA_AUDIO_NO_OWNER &&
        (source->identity.role==QA_QVM_UI || source->identity.role==QA_QVM_CGAME) &&
        source->identity.physical_seat<f->options.seats && source->files && source->movies && source->cvars &&
        source->current && source->append && source->release && source->current(source->context,source);
}
bool frontend_system_cinematic_source_current(const frontend_system_cinematic *row)
{
    if (!row || !frontend_system_cinematic_source_valid(row->frontend,&row->source)) return false;
    for (const frontend_system_cinematic *held=row->frontend->system_cinematics;held;held=held->next)
        if (held==row) return true;
    return false;
}
static frontend_system_cinematic *screen(const qa_frontend *f)
{
    for (frontend_system_cinematic *row=f?f->system_cinematics:NULL;row;row=row->next)
        if (row->screen && row->phase==SYSTEM_PLAYING &&
            frontend_system_cinematic_source_current(row)) return row;
    return NULL;
}
static double sample(void *context) { return ((frontend_system_cinematic *)context)->clock_ms; }
static void diagnostic(void *context,const char *text)
{ frontend_print(((frontend_system_cinematic *)context)->frontend,text); }
qa_cinematic_options frontend_system_cinematic_options(frontend_system_cinematic *row,qa_audio_engine *audio)
{
    qa_q3_cinematic_source_options numeric={0};
    bool source=row->numeric_source && qa_q3_cinematic_source_read(row->numeric_source,&numeric);
    return (qa_cinematic_options){.clock=source?numeric.clock:(qa_media_clock){.context=row,.sample=sample},
        .target={.kind=QA_CINEMATIC_SEAT,.id.seat=row->source.identity.physical_seat},
        .loop=row->loop,.hold=row->hold,.silent=row->silent,.audio=audio,
        .audio_bus=row->source.identity.audio_bus,.gain=1,
        .audio_audience={.kind=QA_CINEMATIC_AUDIO_WORLD},.context=source?numeric.context:row,
        .diagnostic=source?numeric.print:diagnostic,
        .roq_scratch=source?qa_q3_cinematic_source_decoder_scratch(row->numeric_source):NULL};
}
static bool finish(frontend_system_cinematic *row,qa_cinematic_end reason,qa_error *error)
{
    if (row->phase!=SYSTEM_PLAYING) return true;
    qa_frontend *f=row->frontend;
    if (!frontend_ui_features_idle(f) || (f->audio && !qa_audio_engine_round_ready(f->audio,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic cleanup retains an active UI or audio owner");
    if (!row->ending) { row->ending=true; row->ending_reason=reason; }
    reason=row->ending_reason;
    bool complete=reason!=QA_CINEMATIC_STOPPED && row->screen && frontend_system_cinematic_source_current(row);
    if (row->movie && !qa_cinematic_end_playback(row->movie,complete?reason:QA_CINEMATIC_STOPPED,error)) return false;
    if (complete) {
        if (!row->nextmap) {
            const qa_cvar_view *next=qa_cvars_find(row->source.cvars,"nextmap");
            row->nextmap=copy_text(next && next->value?next->value:"",error);
            if (!row->nextmap) return false;
        }
        if (*row->nextmap && !row->appended) {
            size_t size=strlen(row->nextmap); char *text=size<=SIZE_MAX-2?malloc(size+2):NULL;
            if (!text) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source nextmap command");
            memcpy(text,row->nextmap,size); text[size]='\n'; text[size+1]=0;
            bool ok=row->source.append(row->source.context,text,error); free(text);
            if (!ok) return false;
            row->appended=true;
        }
        if (!frontend_system_cinematic_source_current(row))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic completion lost its retained source");
        if (*row->nextmap && !qa_cvars_set(row->source.cvars,"nextmap","",true,error)) return false;
        if (!frontend_system_cinematic_source_current(row))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic completion retired its source namespace");
    }
    if (row->screen) frontend_ui_cinematic_clear(f,row->source.identity.physical_seat);
    row->screen=false; row->phase=complete?SYSTEM_COMPLETED:SYSTEM_STOPPED;
    qa_cinematic_destroy(row->movie); row->movie=NULL;
    qa_cinematic_asset_release(row->asset); row->asset=NULL;
    return true;
}
static qa_media_status status(void *context)
{
    frontend_system_cinematic *row=context;
    if (!row) return QA_MEDIA_STOPPED;
    if (row->phase==SYSTEM_COMPLETED) return QA_MEDIA_ENDED;
    if (row->phase==SYSTEM_STOPPED || !row->screen || !frontend_system_cinematic_source_current(row)) return QA_MEDIA_STOPPED;
    /* A failed checked completion still owns its real pending transition. */
    return row->ending?QA_MEDIA_PLAYING:qa_cinematic_status(row->movie);
}
static bool end(void *context,qa_cinematic_end reason,qa_error *error)
{
    frontend_system_cinematic *row=context;
    if (!row || row->busy || reason<QA_CINEMATIC_FINISHED || reason>QA_CINEMATIC_STOPPED)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic end requires its returned handle");
    row->busy=true; bool ok=finish(row,reason,error); row->busy=false; return ok;
}
void frontend_system_cinematic_row_free(frontend_system_cinematic *row)
{
    if (!row) return;
    qa_frontend *f=row->frontend;
    frontend_system_cinematic **link=&f->system_cinematics;
    while (*link && *link!=row) link=&(*link)->next;
    if (*link!=row) return;
    *link=row->next;
    qa_cinematic_destroy(row->movie);
    qa_cinematic_asset_release(row->asset); free(row->path); free(row->nextmap);
    frontend_system_cinematic_source source=row->source;
    free(row);
    if (source.release) source.release(source.context);
}
static void release(void *context)
{
    frontend_system_cinematic *row=context;
    if (!row || row->busy || row->phase==SYSTEM_PLAYING) return;
    frontend_system_cinematic_row_free(row);
}
static qa_cinematic *playback(void *context)
{ return ((frontend_system_cinematic *)context)->movie; }
void frontend_system_cinematic_handle(frontend_system_cinematic *row,qa_q3_system_movie *out)
{ *out=(qa_q3_system_movie){.context=row,.status=status,.end=end,.release=release,
    .playback=row->numeric_source?playback:NULL}; }
bool frontend_system_cinematic_open(qa_frontend *f,const frontend_system_cinematic_source *source,
    const qa_q3_movie_request *request,qa_q3_system_movie *out,qa_error *error)
{
    if (!request || !out || !frontend_system_cinematic_source_valid(f,source) || f->capture || f->source_restoring ||
        !frontend_system_cinematic_idle(f) || !frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic request lacks its actual returned source lease");
    frontend_system_cinematic *row=calloc(1,sizeof(*row));
    if (!row) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine system cinematic owner");
    row->frontend=f; row->source=*source; row->loop=request->loop; row->hold=request->hold; row->silent=request->silent;
    row->numeric_source=request->numeric_source; row->numeric_handle=request->numeric_source?request->numeric_handle:-1;
    if (row->numeric_source) {
        qa_q3_cinematic_source_options numeric;
        if (source->cinematics!=row->numeric_source || row->numeric_handle<0 || row->numeric_handle>=16 ||
            qa_q3_cinematic_source_handles(row->numeric_source)!=f->source_cinematics ||
            !qa_q3_cinematic_source_read(row->numeric_source,&numeric) || numeric.files!=source->files ||
            numeric.seat!=source->identity.physical_seat ||
            numeric.audio_bus(numeric.context)!=source->identity.audio_bus) {
            free(row); return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic request changed its creating numeric Source lease");
        }
    }
    row->path=row->numeric_source && request->path && *request->path?
        copy_text(request->path,error):resource_path(request->path,error);
    if (!row->path) { free(row); return false; }
    /* Source reserveMovie retires the previous screen request before preparing
     * the new decoder. Its numeric handle remains a genuine stopped owner. */
    if (!frontend_system_cinematic_stop_all(f,error) || !frontend_cinematic_destroy(f,error)) {
        free(row->path); free(row); return false;
    }
    row->next=f->system_cinematics; f->system_cinematics=row; row->screen=true; row->busy=true;
    qa_error opened={0};
    bool ok=row->numeric_source?
        qa_media_library_load_source_roq(source->movies,source->files,row->path,&row->asset,&opened):
        qa_media_library_load(source->movies,source->files,row->path,&row->asset,&opened);
    if (!ok && !row->numeric_source && opened.code==QA_ERROR_NOT_FOUND && suffix(row->path,".cin")) {
        memcpy(row->path+strlen(row->path)-4,".ogv",4);
        ok=qa_media_library_load(source->movies,source->files,row->path,&row->asset,&opened);
    }
    if (!ok && error) *error=opened;
    qa_audio_engine *staging=NULL; qa_audio_stream_cut *cut=NULL;
    if (ok && f->audio) {
        qa_audio_engine_options audio; frontend_audio_engine_options(f,&audio);
        audio.sample_rate=qa_audio_engine_rate(f->audio); audio.observer=NULL; audio.observer_user=NULL;
        ok=qa_audio_engine_create(&audio,&staging,error);
    }
    if (ok) {
        qa_cinematic_options options=frontend_system_cinematic_options(row,staging);
        qa_cinematic_source movie_source=qa_cinematic_asset_source(row->asset); movie_source.name=row->path;
        ok=qa_cinematic_create(&movie_source,&options,&row->movie,error);
    }
    if (ok) ok=frontend_system_cinematic_source_current(row) &&
        frontend_ui_cinematic_prepare(f,source->files,row->path,source->identity.physical_seat,error);
    if (ok) ok=qa_cinematic_audio_rebind_ready(row->movie,staging,source->identity.audio_bus,error) &&
        frontend_system_cinematic_source_current(row) && (!f->device || qa_audio_device_round_ready(f->device,error));
    if (ok && f->audio) ok=qa_audio_engine_stream_cut_prepare(f->audio,staging,
        source->identity.audio_bus,QA_AUDIO_WORLD,1,&cut,error);
    if (ok && cut) ok=frontend_system_cinematic_source_current(row) && qa_audio_engine_stream_cut_current(cut);
    if (ok) {
        if (cut) qa_audio_engine_stream_cut_publish(cut);
        qa_cinematic_audio_rebind(row->movie,f->audio,source->identity.audio_bus);
        qa_audio_device_clear(f->device);
        frontend_source_audio_stopped(f); frontend_campaign_audio_stopped(f);
    }
    qa_audio_engine_stream_cut_destroy(cut);
    row->busy=false;
    if (!ok) {
        row->source.release=NULL; frontend_ui_cinematic_clear(f,source->identity.physical_seat);
        frontend_system_cinematic_row_free(row);
    }
    qa_audio_engine_destroy(staging);
    if (!ok) return false;
    frontend_system_cinematic_handle(row,out); return true;
}
bool frontend_system_cinematic_idle(const qa_frontend *f)
{
    if (!f) return false;
    for (const frontend_system_cinematic *row=f->system_cinematics;row;row=row->next)
        if (row->frontend!=f || row->busy) return false;
    return true;
}
bool frontend_system_cinematic_capture_ready(const qa_frontend *f)
{
    if (!frontend_system_cinematic_idle(f)) return false;
    for (const frontend_system_cinematic *row=f->system_cinematics;row;row=row->next)
        if (row->phase==SYSTEM_PLAYING && !frontend_system_cinematic_source_current(row)) return false;
    return true;
}
bool frontend_system_cinematic_running(const qa_frontend *f) { return screen(f)!=NULL; }
bool frontend_system_cinematic_stop_all(qa_frontend *f,qa_error *error)
{
    if (!frontend_system_cinematic_idle(f)) return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic source callback is active");
    for (frontend_system_cinematic *row=f->system_cinematics;row;row=row->next)
        if (!end(row,QA_CINEMATIC_STOPPED,error)) return false;
    return true;
}
bool frontend_system_cinematic_destroy(qa_frontend *f,qa_error *error)
{
    if (!f) return true;
    if (!frontend_system_cinematic_stop_all(f,error)) return false;
    return !f->system_cinematics || frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic numeric handles retain actual source leases");
}
bool frontend_system_cinematic_drain(qa_frontend *f,qa_error *error)
{
    if (!frontend_system_cinematic_idle(f)) return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic drain retains a callback");
    for (frontend_system_cinematic *row=f->system_cinematics;row;row=row->next) {
        if (row->phase!=SYSTEM_PLAYING) continue;
        if (!frontend_system_cinematic_source_current(row)) { if (!end(row,QA_CINEMATIC_STOPPED,error)) return false; }
        else if (row->ending && !end(row,row->ending_reason,error)) return false;
    }
    return true;
}
bool frontend_system_cinematic_command(qa_frontend *f,const qa_command_invocation *command,bool *handled,qa_error *error)
{
    if (!f || !command || !command->argc || !command->argv || !handled)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid system cinematic command");
    *handled=false;
    bool stop=!strcmp(command->argv[0],"stopcinematic");
    if (!stop && strcmp(command->argv[0],"cinematicpause")) return true;
    frontend_system_cinematic *row=screen(f);
    if (!row) return true;
    *handled=true;
    if (row->busy || f->capture || !frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic command retains a live callback");
    if (stop) return end(row,QA_CINEMATIC_STOPPED,error);
    row->busy=true;
    bool ok=qa_cinematic_pause(row->movie,qa_cinematic_status(row->movie)!=QA_MEDIA_PAUSED,error);
    if (ok && !frontend_system_cinematic_source_current(row))
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic pause retired its source lease");
    row->busy=false; return ok;
}
bool frontend_system_cinematic_view_current(const qa_frontend *f,const qa_vfs *files,const char *path,uint32_t seat)
{
    const frontend_system_cinematic *row=screen(f);
    return row && row->movie && files==row->source.files && path && !strcmp(path,row->path) && seat==row->source.identity.physical_seat;
}
bool frontend_system_cinematic_view_read(qa_frontend *f,frontend_cinematic_view *out,bool *found,qa_error *error)
{
    if (!f || !out || !found) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid system cinematic view");
    *found=false; frontend_system_cinematic *row=screen(f);
    if (!row || !row->movie) return true;
    frontend_cinematic_view view={.files=row->source.files,.path=row->path,.seat=row->source.identity.physical_seat,
        .viewport={0,0,f->width,f->height},.status=qa_cinematic_status(row->movie)};
    if (!qa_cinematic_time(row->movie,&view.elapsed_ms,&view.source_ms,&view.loop,error)) return false;
    if (row->numeric_source) view.elapsed_ms=row->clock_ms;
    if (!frontend_system_cinematic_source_current(row)) return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic view lost its source lease");
    *out=view; *found=true; return true;
}
bool frontend_system_cinematic_input(qa_frontend *f,uint32_t seat,qa_input_focus focus,
    const qa_input_event *event,bool *handled,qa_error *error)
{
    if (!f || !event || !handled) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid system cinematic input");
    *handled=false; frontend_system_cinematic *row=screen(f);
    if (!row || seat!=row->source.identity.physical_seat || focus!=QA_INPUT_GAME) return true;
    *handled=true;
    bool press=event->down && ((event->kind==QA_INPUT_EVENT_KEY && !event->repeat) || event->kind==QA_INPUT_EVENT_BUTTON);
    if (!press) return true;
    double elapsed,source; uint64_t loop;
    if (!qa_cinematic_time(row->movie,&elapsed,&source,&loop,error)) return false;
    if (row->numeric_source) elapsed=row->clock_ms;
    if (elapsed>1000 || (qa_cinematic_status(row->movie)==QA_MEDIA_HELD && row->clock_ms>1000))
        return end(row,QA_CINEMATIC_SKIPPED,error);
    return true;
}
bool frontend_system_cinematic_frame(qa_frontend *f,uint64_t elapsed_ns,bool *rendered,qa_error *error)
{
    if (!f || !rendered) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid system cinematic frame");
    *rendered=false; frontend_system_cinematic *row=screen(f);
    if (!row) return true;
    if (!frontend_ui_features_idle(f) || row->busy)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic frame retains an active owner");
    double duration=(double)elapsed_ns/1000000.0;
    if (!isfinite(row->clock_ms+duration)) return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic clock overflow");
    row->clock_ms+=duration;
    bool obscured;
    if (!frontend_cinematic_focus(f,row->movie,&row->focus_paused,&obscured,error)) return false;
    if (obscured) return true;
    bool ready;
    if (!frontend_display_ready(f,&ready,error)) return false;
    if (!ready) return true;
    row->busy=true;
    qa_scene_frame_reset(&f->frame,f->frame_number); bool blank=false;
    bool ok=qa_scene_frame_material_order(&f->frame,f->order,error);
    if (ok && row->numeric_source) ok=frontend_q3_source_begin_frame(f,0,error);
    if (ok) ok=row->numeric_source?
        qa_q3_cinematic_system_fullscreen(row->numeric_source,row->numeric_handle,(qa_scene_rect){0,0,f->width,f->height},&f->frame,&blank,error):
        qa_cinematic_fullscreen(row->movie,QA_CINEMATIC_GAME,(qa_scene_rect){0,0,f->width,f->height},f->ui_images,&f->frame,&blank,error);
    if (ok && !blank) {
        frontend_cinematic_view view; bool found=false;
        ok=frontend_system_cinematic_view_read(f,&view,&found,error) && found &&
            frontend_ui_cinematic_draw(f,view.files,view.path,view.seat,view.elapsed_ms,view.source_ms,
                view.loop,view.status,view.viewport,&f->frame,error);
    }
    if (ok) ok=frontend_system_cinematic_source_current(row);
    if (ok) ok=frontend_render_controls_live(f,error);
    if (ok && f->frame.source_backend && f->frame.source_skip_backend) {
        if (f->frame.source_pending)
            ok=qa_material_source_frame_end(f->frame.source_pending,&f->frame,false,error);
    } else if (ok) {
        ok=frontend_frame_present(f,error);
    }
    row->busy=false;
    if (!ok) return false;
    *rendered=true;
    if (qa_cinematic_status(row->movie)==QA_MEDIA_ENDED &&
        (!row->numeric_source || qa_cinematic_frame(row->movie))) return end(row,QA_CINEMATIC_FINISHED,error);
    return true;
}

bool frontend_system_cinematic_rebind_ready(const qa_frontend *owned,const qa_frontend *destination,qa_error *error)
{
    if (!owned || !destination || !frontend_system_cinematic_idle(owned) || destination->system_cinematics)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic exchange requires real idle frontend owners");
    for (frontend_system_cinematic *row=owned->system_cinematics;row;row=row->next)
        if ((row->movie && (!qa_cinematic_frame_rebind_ready(row->movie,&owned->frame,error) ||
            !qa_cinematic_audio_rebind_ready(row->movie,destination->audio,row->source.identity.audio_bus,error)))) return false;
    return true;
}
void frontend_system_cinematic_rebind(qa_frontend *owned,qa_frontend *destination)
{
    destination->system_cinematics=owned->system_cinematics; owned->system_cinematics=NULL;
    for (frontend_system_cinematic *row=destination->system_cinematics;row;row=row->next) {
        row->frontend=destination;
        if (row->movie) {
            qa_cinematic_frame_rebind(row->movie,&owned->frame,&destination->frame);
            qa_cinematic_audio_rebind(row->movie,destination->audio,row->source.identity.audio_bus);
        }
    }
}
bool frontend_system_cinematic_content_visit(const qa_frontend *f,const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!f || !visitor || !visitor->view || !frontend_system_cinematic_capture_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"System cinematic content capture requires returned real owners");
    for (const frontend_system_cinematic *row=f->system_cinematics;row;row=row->next)
        if (!visitor->view(visitor->context,row->source.files,error)) return false;
    return true;
}
