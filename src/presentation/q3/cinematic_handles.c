#include "cinematic_handles_private.h"
#include "qa/material_source_scratch.h"
#include <math.h>

bool q3cin_fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
bool q3cin_enter(qa_q3_cinematic_source *source,qa_error *error)
{
    if (!source || !source->handles || !qa_q3_cinematic_handles_idle(source->handles) ||
        !source->options.current(source->options.context,&source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Original cinematic requires its returned actual source");
    source->handles->busy=true; return true;
}
static bool returned(qa_q3_cinematic_source *source,bool ok,qa_error *error)
{
    bool current=source->options.current(source->options.context,&source->options);
    source->handles->busy=false;
    return ok && (current || q3cin_fail(error,QA_ERROR_ARGUMENT,"Original cinematic source changed during playback"));
}
bool qa_q3_cinematic_handles_create(const qa_q3_cinematic_handles_options *options,
    qa_q3_cinematic_handles **out,qa_error *error)
{
    if (!options || !options->images || !options->upload || !options->ui_limits || !out || *out)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Original cinematic pool requires actual scratch upload services");
    for (size_t i=0;i<16;++i) if (!qa_scene_source_q3_scratch(options->images,i))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Original cinematic scratch bank is not initialized");
    qa_q3_cinematic_handles *owner=calloc(1,sizeof(*owner));
    if (!owner) return q3cin_fail(error,QA_ERROR_MEMORY,"Allocating original cinematic handle pool");
    if (!qa_scene_resources_retain(options->images,error)) { free(owner); return false; }
    if (!qa_roq_scratch_create(&owner->decoder_scratch,error)) {
        qa_scene_resources_destroy(options->images); free(owner); return false;
    }
    owner->options=*options;
    owner->selected_handle=-1;
    for (size_t i=0;i<16;++i) {
        owner->scratch[i]=qa_scene_source_q3_scratch(options->images,i);
        qa_scene_image_retain(owner->scratch[i]);
    }
    *out=owner; return true;
}
bool qa_q3_cinematic_handles_idle(const qa_q3_cinematic_handles *owner)
{ return owner && !owner->busy && !owner->stage; }
bool qa_q3_cinematic_handles_read(const qa_q3_cinematic_handles *owner,qa_q3_cinematic_handles_options *out)
{ if (!owner || !out) return false; *out=owner->options; return true; }
bool qa_q3_cinematic_handles_rebind_ready(const qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_handles_options *options,qa_error *error)
{
    return (qa_q3_cinematic_handles_idle(owner) && options && options->images==owner->options.images &&
        options->upload==owner->options.upload && options->ui_limits==owner->options.ui_limits &&
        options->fullscreen_draw==owner->options.fullscreen_draw) ||
        q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic pool adoption changed its physical scratch bank or renderer services");
}
void qa_q3_cinematic_handles_rebind(qa_q3_cinematic_handles *owner,const qa_q3_cinematic_handles_options *options)
{ if (owner && options) owner->options=*options; }
bool qa_q3_cinematic_handles_destroy(qa_q3_cinematic_handles **slot,qa_error *error)
{
    if (!slot) return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic pool disposal requires its owned slot");
    qa_q3_cinematic_handles *owner=*slot;
    if (!owner) return true;
    if (owner->busy || owner->stage || owner->sources)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic pool retains actual source children");
    for (size_t i=0;i<16;++i) qa_scene_image_release(owner->scratch[i]);
    qa_roq_scratch_release(owner->decoder_scratch);
    qa_scene_resources_destroy(owner->options.images); free(owner); *slot=NULL; return true;
}
bool qa_q3_cinematic_source_create(qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_source_options *options,qa_q3_cinematic_source **out,qa_error *error)
{
    if (!qa_q3_cinematic_handles_idle(owner) || !options || !options->files || !options->media ||
        !options->clock.sample || !options->current || !options->audio_bus || !options->in_game_video ||
        options->seat==QA_AUDIO_WORLD || !out || *out || !options->current(options->context,options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source requires its genuine provider and physical seat");
    qa_q3_cinematic_source *source=calloc(1,sizeof(*source));
    if (!source) return q3cin_fail(error,QA_ERROR_MEMORY,"Retaining original cinematic source");
    if (!qa_vfs_retain(options->files,error)) { free(source); return false; }
    source->handles=owner; source->options=*options;
    source->next=owner->sources; owner->sources=source; *out=source; return true;
}
bool qa_q3_cinematic_source_retain(qa_q3_cinematic_source *source,qa_error *error)
{
    if (!source || !qa_q3_cinematic_handles_idle(source->handles) || source->users==UINT_MAX)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source cannot acquire another presentation");
    ++source->users; return true;
}
static uint64_t role_bus(void *context)
{ return ((const qa_q3_cinematic_source *)context)->role_bus; }
static bool role_video(void *context,int32_t *out,qa_error *error)
{
    const qa_q3_cinematic_source *source=context;
    return source->parent->options.in_game_video(source->parent->options.context,out,error);
}
static void role_print(void *context,const char *message)
{
    const qa_q3_cinematic_source *source=context;
    if (source->diagnostic_print) source->diagnostic_print(source->diagnostic_context,message);
    else if (source->parent->options.print) source->parent->options.print(source->parent->options.context,message);
}
static bool role_current(void *context,const qa_q3_cinematic_source_options *options)
{
    const qa_q3_cinematic_source *source=context;
    const qa_q3_cinematic_source *parent=source?source->parent:NULL;
    return parent && options && source->handles==parent->handles && options->context==source &&
        options->files==parent->options.files && options->media==parent->options.media &&
        options->clock.context==parent->options.clock.context && options->clock.sample==parent->options.clock.sample &&
        options->audio==parent->options.audio && options->seat==source->options.seat &&
        options->audio_bus==role_bus && options->in_game_video==role_video &&
        options->print==((source->diagnostic_print || parent->options.print)?role_print:NULL) &&
        options->current==role_current &&
        (!source->diagnostic_current || source->diagnostic_current(source->diagnostic_context,source)) &&
        parent->options.current(parent->options.context,&parent->options);
}
bool qa_q3_cinematic_source_create_role(qa_q3_cinematic_source *parent,uint32_t seat,uint64_t bus,
    qa_q3_cinematic_source **out,qa_error *error)
{
    if (!parent || parent->parent || !out || *out || seat!=parent->options.seat ||
        !bus || bus==QA_AUDIO_NO_OWNER || !qa_q3_cinematic_handles_idle(parent->handles) ||
        !parent->options.current(parent->options.context,&parent->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Role cinematic source requires its actual stable provider, physical seat and bus");
    for (const qa_q3_cinematic_source *row=parent->handles->sources;row;row=row->next)
        if (row->parent==parent && row->options.seat==seat && row->role_bus==bus)
            return q3cin_fail(error,QA_ERROR_ARGUMENT,"Role cinematic source already has its actual namespace owner");
    qa_q3_cinematic_source *source=calloc(1,sizeof(*source));
    if (!source) return q3cin_fail(error,QA_ERROR_MEMORY,"Retaining role cinematic source");
    if (!qa_q3_cinematic_source_retain(parent,error)) { free(source); return false; }
    if (!qa_vfs_retain(parent->options.files,error)) { qa_q3_cinematic_source_release(parent); free(source); return false; }
    source->handles=parent->handles; source->parent=parent; source->role_bus=bus;
    source->options=parent->options; source->options.seat=seat; source->options.context=source;
    source->options.audio_bus=role_bus; source->options.print=parent->options.print?role_print:NULL;
    source->options.in_game_video=role_video;
    source->options.current=role_current;
    source->next=source->handles->sources; source->handles->sources=source; *out=source; return true;
}
const qa_q3_cinematic_source *qa_q3_cinematic_source_parent(const qa_q3_cinematic_source *source)
{ return source?source->parent:NULL; }
bool qa_q3_cinematic_source_role_read(const qa_q3_cinematic_source *source,
    const qa_q3_cinematic_source **parent,uint32_t *seat,uint64_t *bus)
{
    if (!source || !source->parent || !parent || !seat || !bus) return false;
    *parent=source->parent; *seat=source->options.seat; *bus=source->role_bus; return true;
}
bool qa_q3_cinematic_source_role_find(const qa_q3_cinematic_source *parent,
    uint32_t seat,uint64_t bus,qa_q3_cinematic_source **out)
{
    if (!parent || parent->parent || !out) return false;
    for (qa_q3_cinematic_source *source=parent->handles->sources;source;source=source->next)
        if (source->parent==parent && source->options.seat==seat && source->role_bus==bus) {
            *out=source; return true;
        }
    return false;
}
bool qa_q3_cinematic_source_role_diagnostic_bind(qa_q3_cinematic_source *source,void *context,
    void (*print)(void *,const char *),bool (*current)(void *,const qa_q3_cinematic_source *),qa_error *error)
{
    if (!source || !source->parent || !print || !current ||
        !qa_q3_cinematic_handles_idle(source->handles) ||
        !current(context,source))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Role cinematic diagnostic requires its actual retained console owner");
    if (source->diagnostic_print || source->diagnostic_current)
        return (source->diagnostic_context==context && source->diagnostic_print==print && source->diagnostic_current==current) ||
            q3cin_fail(error,QA_ERROR_ARGUMENT,"Role cinematic diagnostic cannot replace its retained owner");
    for (size_t i=0;i<16;++i) if (source->handles->movies[i].source==source)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Role cinematic diagnostic binding must precede actual handle construction");
    source->diagnostic_context=context; source->diagnostic_print=print; source->diagnostic_current=current;
    source->options.print=role_print; return true;
}
bool qa_q3_cinematic_source_role_diagnostic_read(const qa_q3_cinematic_source *source,void **context,bool *bound)
{
    if (!source || !source->parent || !context || !bound) return false;
    *context=source->diagnostic_context;
    *bound=source->diagnostic_print!=NULL || source->diagnostic_current!=NULL;
    return true;
}
bool qa_q3_cinematic_source_role_detach(qa_q3_cinematic_source *source,void *context,qa_error *error)
{
    if (!source || !source->parent || !qa_q3_cinematic_handles_idle(source->handles) || source->users ||
        !source->diagnostic_print || !source->diagnostic_current || source->diagnostic_context!=context)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Detached cinematic role requires its returned actual diagnostic owner");
    for (size_t i=0;i<16;++i) if (source->handles->movies[i].source==source &&
        (source->handles->movies[i].flags&1u))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Detached cinematic role retains a real fullscreen lease on its retiring owner");
    source->diagnostic_context=NULL; source->diagnostic_print=NULL; source->diagnostic_current=NULL;
    source->options.print=source->parent->options.print?role_print:NULL;
    return true;
}
void qa_q3_cinematic_source_release(qa_q3_cinematic_source *source)
{ if (source && source->users) --source->users; }
bool q3cin_close(qa_q3_cinematic_handles *owner,uint32_t index,qa_cinematic_end reason,qa_error *error)
{
    q3cin_movie *movie=&owner->movies[index];
    if (movie->system.context || movie->system.status || movie->system.end || movie->system.release) {
        if (!movie->system.end || !movie->system.release)
            return q3cin_fail(error,QA_ERROR_ARGUMENT,"Partial system cinematic retains its real lifetime owner");
        if (!movie->system.end(movie->system.context,reason,error)) return false;
        movie->system.release(movie->system.context);
    }
    qa_cinematic_destroy(movie->playback); qa_cinematic_asset_release(movie->asset);
    free(movie->path); *movie=(q3cin_movie){0}; return true;
}
bool qa_q3_cinematic_source_destroy(qa_q3_cinematic_source **slot,qa_error *error)
{
    if (!slot) return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source disposal requires its owned slot");
    qa_q3_cinematic_source *source=*slot;
    if (!source) return true;
    qa_q3_cinematic_handles *owner=source->handles;
    if (owner->busy || owner->stage || source->users)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source retains entered playback or presentations");
    owner->busy=true;
    for (uint32_t i=0;i<16;++i) if (owner->movies[i].source==source &&
        !q3cin_close(owner,i,QA_CINEMATIC_STOPPED,error)) { owner->busy=false; return false; }
    qa_q3_cinematic_source **link=&owner->sources;
    while (*link && *link!=source) link=&(*link)->next;
    if (*link!=source) { owner->busy=false; return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source lost its actual pool membership"); }
    *link=source->next; qa_q3_cinematic_source_release(source->parent);
    qa_vfs_destroy(source->options.files); free(source); *slot=NULL; owner->busy=false; return true;
}
bool qa_q3_cinematic_source_read(const qa_q3_cinematic_source *source,qa_q3_cinematic_source_options *out)
{ if (!source || !out) return false; *out=source->options; return true; }
bool qa_q3_cinematic_source_retained(const qa_q3_cinematic_source *source)
{
    if (!source) return false;
    if (source->users) return true;
    for (size_t i=0;i<16;++i) if (source->handles->movies[i].source==source) return true;
    return q3cin_stage_retains_source(source->handles->stage,source);
}
size_t qa_q3_cinematic_handles_source_count(const qa_q3_cinematic_handles *owner)
{
    size_t count=0;
    if (owner) for (const qa_q3_cinematic_source *source=owner->sources;source;source=source->next) ++count;
    return count;
}
bool qa_q3_cinematic_handles_source_at(const qa_q3_cinematic_handles *owner,size_t index,qa_q3_cinematic_source **out)
{
    if (!owner || !out) return false;
    qa_q3_cinematic_source *source=owner->sources;
    while (source && index) { source=source->next; --index; }
    if (!source) return false;
    *out=source; return true;
}
bool qa_q3_cinematic_source_audio_rebind_ready(qa_q3_cinematic_source *source,qa_audio_engine *audio,
    uint64_t bus,qa_error *error)
{
    if (!source || !qa_q3_cinematic_handles_idle(source->handles) || (!source->options.audio!=!audio))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source audio exchange requires its actual idle owner");
    for (size_t i=0;i<16;++i) {
        q3cin_movie *movie=&source->handles->movies[i];
        if (movie->source==source && movie->playback && !qa_cinematic_audio_rebind_ready(movie->playback,audio,bus,error)) return false;
    }
    return true;
}
void qa_q3_cinematic_source_audio_rebind(qa_q3_cinematic_source *source,qa_audio_engine *audio,uint64_t bus)
{
    if (!source || !qa_q3_cinematic_handles_idle(source->handles)) return;
    for (size_t i=0;i<16;++i) {
        q3cin_movie *movie=&source->handles->movies[i];
        if (movie->source==source && movie->playback) {
            qa_cinematic_audio_rebind(movie->playback,audio,bus); movie->bus=bus;
        }
    }
    source->options.audio=audio;
    if (source->parent) source->role_bus=bus;
}
qa_q3_cinematic_handles *qa_q3_cinematic_source_handles(const qa_q3_cinematic_source *source)
{ return source?source->handles:NULL; }
qa_roq_scratch *qa_q3_cinematic_source_decoder_scratch(const qa_q3_cinematic_source *source)
{ return source && source->handles?source->handles->decoder_scratch:NULL; }
bool qa_q3_cinematic_system_select(qa_q3_cinematic_source *source,int32_t handle,
    qa_cinematic *playback,qa_error *error)
{
    if (!playback || handle<0 || handle>=16 || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    q3cin_movie *movie=&owner->movies[handle];
    bool ok=movie->source==source && (movie->flags&1u) && !movie->pending &&
        movie->system.playback && movie->system.playback(movie->system.context)==playback;
    if (!ok) q3cin_fail(error,QA_ERROR_ARGUMENT,"Fullscreen decoder lost its actual global cinematic slot");
    if (ok && owner->decoder_handle!=handle) ok=qa_cinematic_roq_restart(playback,error);
    if (ok) owner->selected_handle=owner->decoder_handle=handle;
    return returned(source,ok,error);
}
qa_cinematic_options q3cin_options(const qa_q3_cinematic_source *source,uint32_t flags,uint64_t bus)
{
    const qa_q3_cinematic_source_options *s=&source->options;
    qa_cinematic_options options={.clock=s->clock,.target={.kind=flags&16u?QA_CINEMATIC_MATERIAL:QA_CINEMATIC_SEAT},
        .loop=(flags&2u)!=0,.hold=(flags&4u)!=0,.silent=(flags&8u)!=0,
        .audio=s->audio,.audio_bus=bus,.gain=1,.audio_audience={QA_CINEMATIC_AUDIO_SEAT,s->seat},
        .context=s->context,.diagnostic=s->print,.roq_scratch=source->handles->decoder_scratch};
    if (flags&16u) options.target.id.material=bus; else options.target.id.seat=s->seat;
    return options;
}
static char *path_copy(const char *request,qa_error *error)
{
    size_t length=strlen(request);
    bool prefix=!strchr(request,'/') && !strchr(request,'\\');
    size_t before=prefix?6u:0u;
    if (length>SIZE_MAX-before-1) { q3cin_fail(error,QA_ERROR_MEMORY,"Cinematic request exceeds capacity"); return NULL; }
    char *path=malloc(before+length+1);
    if (!path) { q3cin_fail(error,QA_ERROR_MEMORY,"Retaining cinematic request"); return NULL; }
    if (prefix) memcpy(path,"video/",before);
    memcpy(path+before,request,length);
    path[before+length]=0; return path;
}
bool q3cin_play_into(qa_q3_cinematic_source *source,q3cin_movie slots[16],qa_media_library *media,
    qa_roq_scratch *scratch,int32_t *selected,int32_t *decoder,
    const char *request,qa_scene_rect_f rect,uint32_t flags,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *context,
    int32_t *out,qa_error *error)
{
    char *path=path_copy(request,error);
    if (!path) return false;
    if (!(flags&1u)) for (uint32_t i=0;i<16;++i) if (slots[i].occupied && slots[i].path &&
        !(slots[i].flags&1u) && slots[i].source->options.files==source->options.files &&
        slots[i].source->options.media==source->options.media && !strcmp(slots[i].path,path)) {
        free(path); *out=(int32_t)i; return true;
    }
    uint32_t index=0; while (index<16 && slots[index].occupied) ++index;
    if (index==16) { free(path); return q3cin_fail(error,QA_ERROR_FORMAT,"CIN_HandleForVideo: none free"); }
    qa_roq_scratch_clear(scratch,false); *selected=*decoder=(int32_t)index;
    if (slots==source->handles->movies && slots[index].source &&
        !q3cin_close(source->handles,index,QA_CINEMATIC_STOPPED,error)) { free(path); return false; }
    q3cin_movie *movie=&slots[index];
    *movie=(q3cin_movie){.source=source,.path=path,.rect=rect,.flags=flags,.uploaded=UINT64_MAX,
        .pending=true,.occupied=true,.dirty=true,.play_on_walls=1,.status=1,.width=512,.height=512};
    bool ok;
    if (flags&1u) {
        qa_q3_movie_request requested={.path=path,.loop=(flags&2u)!=0,.hold=(flags&4u)!=0,
            .silent=(flags&8u)!=0,.numeric_source=source,.numeric_handle=(int32_t)index};
        ok=open && open(context,&requested,&movie->system,error);
        if (ok && (!movie->system.status || !movie->system.end || !movie->system.release))
            ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"System cinematic returned incomplete ownership");
    } else {
        qa_error local={0};
        ok=qa_media_library_load_source_roq(media,source->options.files,path,&movie->asset,&local);
        if (!ok && local.code==QA_ERROR_NOT_FOUND) {
            free(movie->path); *movie=(q3cin_movie){0}; *out=-1; return true;
        }
        if (!ok && local.code==QA_ERROR_FORMAT) {
            movie->pending=false; movie->status=0; *out=-1; return true;
        }
        if (!ok) { if (error) *error=local; }
        else {
            if (!source->options.in_game_video(source->options.context,&movie->play_on_walls,error)) {
                qa_cinematic_asset_release(movie->asset); free(movie->path); *movie=(q3cin_movie){0}; return false;
            }
            qa_cinematic_source asset=qa_cinematic_asset_source(movie->asset); asset.name=path;
            movie->bus=source->options.audio_bus(source->options.context);
            qa_cinematic_options options=q3cin_options(source,flags,movie->bus);
            options.roq_scratch=scratch;
            ok=qa_cinematic_create(&asset,&options,NULL,&movie->playback,&local);
            if (!ok && local.code==QA_ERROR_FORMAT) {
                qa_cinematic_destroy(movie->playback); movie->playback=NULL;
                movie->pending=false; movie->status=0; *out=-1; return true;
            }
            if (!ok && error) *error=local;
        }
    }
    if (!ok) {
        if (movie->system.context || movie->system.status || movie->system.end || movie->system.release) {
            if (!movie->system.end || !movie->system.release ||
                !movie->system.end(movie->system.context,QA_CINEMATIC_STOPPED,NULL)) return false;
            movie->system.release(movie->system.context);
        }
        qa_cinematic_destroy(movie->playback); qa_cinematic_asset_release(movie->asset);
        free(movie->path); *movie=(q3cin_movie){0};
        return false;
    }
    movie->pending=false; *out=(int32_t)index; return true;
}
bool qa_q3_cinematic_play(qa_q3_cinematic_source *source,const char *path,qa_scene_rect_f rect,uint32_t flags,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *context,int32_t *out,qa_error *error)
{
    if (!path || !out || ((flags&1u) && !open) || !isfinite(rect.x) || !isfinite(rect.y) ||
        !isfinite(rect.width) || !isfinite(rect.height) || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    return returned(source,q3cin_play_into(source,owner->movies,source->options.media,owner->decoder_scratch,
        &owner->selected_handle,&owner->decoder_handle,path,rect,flags,open,context,out,error),error);
}
static bool run(qa_q3_cinematic_handles *owner,int32_t handle,int32_t *out,qa_error *error)
{
    *out=2;
    if (handle<0 || handle>=16) return true;
    if (!owner->movies[handle].source) {
        if (owner->decoder_handle!=handle)
            return q3cin_fail(error,QA_ERROR_FORMAT,"Cannot reset an uninitialized cinematic without its retained file");
        *out=0; return true;
    }
    q3cin_movie *movie=&owner->movies[handle];
    if (movie->pending || !movie->source->options.current(movie->source->options.context,&movie->source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic handle retains an unavailable physical source");
    if (movie->status==2) return true;
    if (!(movie->flags&1u) && !movie->playback) {
        if (owner->decoder_handle!=handle)
            return q3cin_fail(error,QA_ERROR_FORMAT,"Cannot reset an uninitialized cinematic without its retained decoder");
        *out=movie->status; return true;
    }
    qa_cinematic *decoder=movie->playback?movie->playback:
        movie->system.playback?movie->system.playback(movie->system.context):NULL;
    bool switched=decoder && owner->decoder_handle!=handle;
    if (switched) {
        owner->selected_handle=owner->decoder_handle=handle;
        if (!qa_cinematic_roq_restart(decoder,error)) return false;
        movie->status=5;
    }
    if (movie->playback && movie->play_on_walls < -1) { *out=movie->status; return true; }
    owner->selected_handle=handle;
    if (movie->playback && movie->status==0) { *out=0; return true; }
    qa_media_tick tick={.status=QA_MEDIA_STOPPED};
    uint64_t before=movie->playback?qa_cinematic_revision(movie->playback):0;
    bool ok=true;
    if (movie->flags&1u) tick.status=movie->system.status(movie->system.context);
    else ok=qa_cinematic_tick(movie->playback,&tick,error);
    if (!ok) return false;
    if (movie->playback && qa_cinematic_revision(movie->playback)!=before) movie->dirty=true;
    const qa_media_frame *picture=movie->playback?qa_cinematic_frame(movie->playback):NULL;
    if (picture && (movie->width!=picture->width || movie->height!=picture->height || !movie->draw_width)) {
        bool limited=false;
        if (!owner->options.ui_limits(owner->options.context,&limited,error)) return false;
        movie->width=picture->width; movie->height=picture->height;
        movie->draw_width=limited && picture->width>256?256:picture->width;
        movie->draw_height=limited && picture->height>256?256:picture->height;
        if (limited && (picture->width!=256 || picture->height!=256) && movie->source->options.print)
            movie->source->options.print(movie->source->options.context,
                "HACK: approxmimating cinematic for Rage Pro or Voodoo\n");
    }
    if (!movie->source->options.current(movie->source->options.context,&movie->source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source changed during reached decoder callbacks");
    if (tick.looped && !switched) *out=5;
    else if (tick.status==QA_MEDIA_HELD) *out=0;
    else if (tick.status==QA_MEDIA_PLAYING || tick.status==QA_MEDIA_PAUSED) *out=1;
    else if (movie->playback) {
        movie->status=2;
        if (qa_cinematic_frame(movie->playback)) {
            movie->status=0; movie->occupied=false; owner->selected_handle=-1;
        }
        *out=movie->status; return true;
    } else return q3cin_close(owner,(uint32_t)handle,QA_CINEMATIC_STOPPED,error);
    movie->status=*out;
    return true;
}
bool qa_q3_cinematic_run(qa_q3_cinematic_source *source,int32_t handle,int32_t *out,qa_error *error)
{
    if (!out || !q3cin_enter(source,error)) return false;
    return returned(source,run(source->handles,handle,out,error),error);
}
bool qa_q3_cinematic_stop(qa_q3_cinematic_source *source,int32_t handle,bool skip,qa_error *error)
{
    if (!q3cin_enter(source,error)) return false;
    if (handle>=0 && handle<16 && !(source->handles->movies[handle].flags&1u) &&
        !source->handles->movies[handle].playback) {
        source->handles->selected_handle=handle; return returned(source,true,error);
    }
    if (handle>=0 && handle<16 && source->handles->movies[handle].playback) {
        q3cin_movie *movie=&source->handles->movies[handle]; source->handles->selected_handle=handle;
        if (qa_cinematic_frame(movie->playback) && movie->status!=2) {
            movie->occupied=false; movie->status=0; source->handles->selected_handle=-1;
        }
        return returned(source,true,error);
    }
    return returned(source,handle<0 || handle>=16 || q3cin_close(source->handles,(uint32_t)handle,
        skip?QA_CINEMATIC_SKIPPED:QA_CINEMATIC_STOPPED,error),error);
}
bool qa_q3_cinematic_extents(qa_q3_cinematic_source *source,int32_t handle,qa_scene_rect_f rect,qa_error *error)
{
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) || !q3cin_enter(source,error)) return false;
    if (handle>=0 && handle<16 && source->handles->movies[handle].playback &&
        source->handles->movies[handle].status!=2) {
        source->handles->movies[handle].rect=rect; source->handles->movies[handle].dirty=true;
    }
    return returned(source,true,error);
}
bool q3cin_scratch_valid(const qa_q3_cinematic_handles *owner,uint32_t slot,const qa_scene_image *image)
{ return qa_scene_image_source_scratch_is(owner->options.images,slot,image); }
bool qa_q3_cinematic_handles_image_is(const qa_q3_cinematic_handles *owner,const qa_scene_image *image)
{
    if (!owner || !image) return false;
    for (uint32_t i=0;i<16;++i) if (q3cin_scratch_valid(owner,i,image)) return true;
    return false;
}
static bool upload(qa_q3_cinematic_handles *owner,uint32_t index,bool shader,qa_scene_frame *frame,qa_error *error)
{
    q3cin_movie *movie=&owner->movies[index];
    qa_cinematic *decoder=movie->playback?movie->playback:
        movie->system.playback?movie->system.playback(movie->system.context):NULL;
    const qa_media_frame *picture=decoder?qa_cinematic_frame(decoder):NULL;
    if (!picture) return true;
    qa_media_frame reached;
    if (shader) {
        if (!qa_cinematic_upload_frame(decoder,true,&reached,error)) return false;
    } else if (!qa_cinematic_source_ui_frame(decoder,movie->draw_width,movie->draw_height,
        movie->dirty,&reached,error)) return false;
    picture=&reached;
    uint64_t revision=qa_cinematic_revision(decoder);
    if (shader && movie->play_on_walls<=0 && movie->dirty) {
        if (movie->play_on_walls==0) movie->play_on_walls=-1;
        else if (movie->play_on_walls==-1) movie->play_on_walls=-2;
        else movie->dirty=false;
    }
    bool dirty=movie->dirty;
    const qa_scene_image *previous=owner->scratch[index];
    bool redefine=movie->redefine || previous->logical_width!=picture->width || previous->logical_height!=picture->height;
    qa_scene_image *next=NULL;
    if (dirty || redefine) {
        qa_image pixels={.width=picture->width,.height=picture->height,
            .rgba={.data=(uint8_t *)picture->rgba.data,.size=picture->rgba.size}};
        if (!qa_scene_image_source_scratch_version(owner->options.images,index,previous,&pixels,&next,error)) return false;
    }
    const qa_scene_image *image=next?next:previous;
    const qa_scene_image *registered=qa_scene_source_q3_scratch(owner->options.images,index);
    bool ok=owner->options.upload(owner->options.context,registered,image,redefine,dirty,error);
    if (ok && next) { owner->scratch[index]=next; qa_scene_image_release(previous); movie->uploaded=revision; movie->redefine=false; }
    else qa_scene_image_release(next);
    if (ok) movie->uploaded_shader=shader;
    if (ok && shader) {
        int32_t enabled;
        ok=movie->source->options.in_game_video(movie->source->options.context,&enabled,error);
        if (ok && enabled==0 && movie->play_on_walls==1) movie->play_on_walls=0;
    }
    if (ok && frame) ok=qa_scene_frame_image(frame,image,error);
    return ok;
}
bool qa_q3_cinematic_system_fullscreen(qa_q3_cinematic_source *source,int32_t handle,
    qa_scene_rect viewport,qa_scene_frame *frame,bool *blank,qa_error *error)
{
    if (!frame || !blank || !viewport.width || !viewport.height || handle<0 || handle>=16 ||
        !source || !source->handles->options.fullscreen_draw) return false;
    if (frame->source_pending && !qa_material_source_frame_end(frame->source_pending,frame,true,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    q3cin_movie *movie=&owner->movies[handle];
    qa_cinematic *decoder=movie->system.playback?movie->system.playback(movie->system.context):NULL;
    if (!qa_q3_cinematic_system_select(source,handle,decoder,error) || !q3cin_enter(source,error)) return false;
    bool ok=decoder && movie->source==source && (movie->flags&1u) && !movie->pending && owner->decoder_handle==handle;
    if (!ok) q3cin_fail(error,QA_ERROR_ARGUMENT,"Fullscreen draw lost its selected global decoder");
    uint64_t before=ok?qa_cinematic_revision(decoder):0;
    qa_media_tick tick={0};
    if (ok) ok=qa_cinematic_tick(decoder,&tick,error);
    bool visible=ok && tick.status!=QA_MEDIA_ENDED && tick.status!=QA_MEDIA_STOPPED && tick.frame;
    if (visible) {
        if (qa_cinematic_revision(decoder)!=before) movie->dirty=true;
        const qa_media_frame *picture=qa_cinematic_frame(decoder);
        if (movie->width!=picture->width || movie->height!=picture->height || !movie->draw_width) {
            bool limited=false;
            ok=owner->options.ui_limits(owner->options.context,&limited,error);
            if (ok) {
                movie->width=picture->width; movie->height=picture->height;
                movie->draw_width=limited && picture->width>256?256:picture->width;
                movie->draw_height=limited && picture->height>256?256:picture->height;
            }
        }
        if (ok) ok=upload(owner,(uint32_t)handle,false,frame,error);
    } else if (ok) {
        qa_scene_command clear={.kind=QA_SCENE_COMMAND_VIEW,.data.view={.viewport=viewport,
            .clear_color=true,.color={0,0,0,1},.seat=source->options.seat}};
        ok=qa_scene_frame_emit(frame,&clear,error);
    }
    if (ok) *blank=!visible;
    if (!returned(source,ok,error)) return false;
    /* Raw submission may finish an earlier shader batch and reach another
     * numeric decoder. Image upload returned before that renderer entry. */
    if (visible) {
        ok=owner->options.fullscreen_draw(owner->options.context,owner->scratch[handle],viewport,
            source->options.seat,frame,error);
        if (ok && movie->source==source) movie->dirty=false;
    }
    return ok;
}
bool qa_q3_cinematic_image(qa_q3_cinematic_source *source,int32_t handle,qa_scene_frame *frame,
    const qa_scene_image **out,qa_scene_rect_f *rect,qa_error *error)
{
    if (!out || !rect || !frame || !q3cin_enter(source,error)) return false;
    *out=NULL;
    bool ok=true;
    if (handle>=0 && handle<16 && source->handles->movies[handle].playback &&
        qa_cinematic_frame(source->handles->movies[handle].playback)) {
        ok=upload(source->handles,(uint32_t)handle,false,frame,error);
        if (ok) { *out=source->handles->scratch[handle]; *rect=source->handles->movies[handle].rect; }
    }
    return returned(source,ok,error);
}
bool qa_q3_cinematic_draw_complete(qa_q3_cinematic_source *source,int32_t handle,qa_error *error)
{
    if (!q3cin_enter(source,error)) return false;
    if (handle>=0 && handle<16 && source->handles->movies[handle].playback)
        source->handles->movies[handle].dirty=false;
    return returned(source,true,error);
}
bool qa_q3_cinematic_shader_play(qa_q3_cinematic_source *source,const char *path,int32_t *handle,
    const qa_scene_image **out,qa_error *error)
{
    if (!out || !handle || !path || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    bool ok=q3cin_play_into(source,owner->movies,source->options.media,owner->decoder_scratch,
        &owner->selected_handle,&owner->decoder_handle,path,
        (qa_scene_rect_f){0,0,256,256},2u|8u|16u,NULL,NULL,handle,error);
    if (ok) *out=*handle>=0?source->handles->scratch[*handle]:NULL;
    return returned(source,ok,error);
}
const qa_scene_image *qa_q3_cinematic_shader_resolve(qa_q3_cinematic_source *source,uint64_t identity,
    qa_scene_frame *frame,qa_error *error)
{
    if (!frame || !q3cin_enter(source,error)) return NULL;
    qa_q3_cinematic_handles *owner=source->handles;
    const qa_scene_image *image=NULL; bool ok=true,found=false;
    for (uint32_t i=0;i<16;++i) if (owner->scratch[i]->identity==identity) {
        found=true;
        int32_t status;
        ok=run(owner,(int32_t)i,&status,error);
        if (ok && owner->movies[i].playback && qa_cinematic_frame(owner->movies[i].playback)) {
            ok=upload(owner,i,true,frame,error);
            if (ok) image=owner->scratch[i];
        }
        break;
    }
    if (!found && ok) ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"Reached video stage is not an actual cinematic scratch slot");
    return returned(source,ok,error)?image:NULL;
}
bool qa_q3_cinematic_handles_at(const qa_q3_cinematic_handles *owner,uint32_t index,qa_q3_cinematic_slot *out)
{
    if (!owner || owner->busy || index>=16 || !out) return false;
    const q3cin_movie *movie=&owner->movies[index];
    *out=(qa_q3_cinematic_slot){(int32_t)index,movie->source,movie->path,movie->asset,movie->playback,
        owner->scratch[index],movie->flags&1u?&movie->system:NULL,movie->flags}; return true;
}
