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
static char *text_copy(const char *text,qa_error *error)
{
    if (!text) return NULL;
    size_t bytes=strlen(text)+1;
    char *copy=malloc(bytes);
    if (!copy) { q3cin_fail(error,QA_ERROR_MEMORY,"Retaining cinematic resource name"); return NULL; }
    memcpy(copy,text,bytes); return copy;
}
static bool same_name(const char *a,const char *b)
{ return a==b || (a && b && !strcmp(a,b)); }
void q3cin_restore_dispose(q3cin_movie *movie)
{
    free((char *)movie->restore.instance); free((char *)movie->restore.content);
    free((char *)movie->restore.parent_instance); free((char *)movie->restore.parent_content);
    free((char *)movie->restore.path); free((char *)movie->restore.system_state.nextmap);
    movie->restore=(qa_q3_cinematic_restore_slot){0};
    movie->restore_pending=false;
}
void q3cin_movie_dispose(q3cin_movie *movie)
{
    /* Fullscreen owners return and release before their slot storage retires. */
    qa_cinematic_destroy(movie->playback); qa_cinematic_asset_release(movie->asset);
    qa_roq_scratch_release(movie->decoder_scratch);
    free(movie->path); q3cin_restore_dispose(movie); *movie=(q3cin_movie){0};
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
    owner->options=*options;
    owner->selected_handle=owner->decoder_handle=-1;
    owner->restore_selected=owner->restore_decoder=-1;
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
    for (size_t i=0;i<16;++i)
        if (!q3cin_close(owner,(uint32_t)i,QA_CINEMATIC_STOPPED,error)) return false;
    for (size_t i=0;i<16;++i) qa_scene_image_release(owner->scratch[i]);
    qa_scene_resources_destroy(owner->options.images); free(owner); *slot=NULL; return true;
}
bool qa_q3_cinematic_handles_restore_reserve(qa_q3_cinematic_handles *owner,
    const qa_q3_cinematic_restore_slot *rows,size_t count,qa_error *error)
{
    if (!qa_q3_cinematic_handles_idle(owner) || count>16 || (count && !rows))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic import requires a returned handle bank");
    q3cin_movie candidate[16]={0};
    int32_t selected=-1,decoder=-1;
    bool ok=true;
    for (size_t i=0;ok && i<count;++i) {
        const qa_q3_cinematic_restore_slot *row=rows+i;
        if (row->handle<0 || row->handle>=16 || candidate[row->handle].restore_pending ||
            owner->movies[row->handle].source || owner->movies[row->handle].path ||
            (!row->instance && !row->content) || !row->path ||
            ((row->flags&1u) && (row->detached ||
                row->system_state.phase<QA_Q3_SYSTEM_MOVIE_PLAYING ||
                row->system_state.phase>QA_Q3_SYSTEM_MOVIE_STOPPED ||
                !isfinite(row->system_state.screen_ms) || row->system_state.screen_ms<0 ||
                (row->system_state.phase==QA_Q3_SYSTEM_MOVIE_PLAYING)!=row->has_playback ||
                (row->system_state.phase==QA_Q3_SYSTEM_MOVIE_PLAYING)!=row->system_state.screen ||
                (row->system_state.ending && (row->system_state.ending_reason<QA_CINEMATIC_FINISHED ||
                    row->system_state.ending_reason>QA_CINEMATIC_STOPPED)))) ||
            (row->detached && (!row->parent_instance && !row->parent_content)) ||
            (row->detached && row->source_group==UINT32_MAX) ||
            !isfinite(row->rect.x) || !isfinite(row->rect.y) || !isfinite(row->rect.width) ||
            !isfinite(row->rect.height) || (row->has_playback &&
            (!isfinite(row->playback.source_ms) || row->playback.source_ms<0)) ||
            (row->selected && selected>=0) || (row->decoder && decoder>=0)) {
            ok=q3cin_fail(error,QA_ERROR_FORMAT,"Invalid saved cinematic slot"); break;
        }
        for (size_t j=0;ok && j<i;++j) if (rows[j].source_group==row->source_group &&
            (!same_name(rows[j].instance,row->instance) || !same_name(rows[j].content,row->content) ||
            !same_name(rows[j].parent_instance,row->parent_instance) ||
            !same_name(rows[j].parent_content,row->parent_content) || rows[j].role!=row->role ||
            rows[j].seat!=row->seat || rows[j].parent_seat!=row->parent_seat || rows[j].detached!=row->detached))
            ok=q3cin_fail(error,QA_ERROR_FORMAT,"Saved cinematic source group has conflicting names");
        if (!ok) break;
        q3cin_movie *movie=candidate+row->handle;
        movie->restore=*row;
        movie->restore.instance=text_copy(row->instance,error);
        movie->restore.content=text_copy(row->content,error);
        movie->restore.parent_instance=text_copy(row->parent_instance,error);
        movie->restore.parent_content=text_copy(row->parent_content,error);
        movie->restore.path=text_copy(row->path,error);
        movie->restore.system_state.nextmap=text_copy(row->system_state.nextmap,error);
        movie->path=text_copy(row->path,error);
        ok=(!row->instance || movie->restore.instance) && (!row->content || movie->restore.content) &&
            (!row->parent_instance || movie->restore.parent_instance) &&
            (!row->parent_content || movie->restore.parent_content) &&
            movie->restore.path && movie->path &&
            (!row->system_state.nextmap || movie->restore.system_state.nextmap);
        movie->restore.playback.resource=movie->restore.path;
        movie->rect=row->rect; movie->flags=row->flags;
        movie->status=row->status; movie->play_on_walls=row->play_on_walls;
        movie->restore_pending=true; movie->occupied=true;
        if (row->selected) selected=row->handle;
        if (row->decoder) decoder=row->handle;
    }
    if (ok) {
        for (size_t i=0;i<16;++i) if (candidate[i].restore_pending) {
            owner->movies[i]=candidate[i]; candidate[i]=(q3cin_movie){0};
        }
        owner->restore_selected=selected; owner->restore_decoder=decoder;
        owner->restoring=count!=0;
        owner->restore_published=owner->restore_superseded=false;
    }
    for (size_t i=0;i<16;++i) {
        free(candidate[i].path); q3cin_restore_dispose(candidate+i);
    }
    return ok;
}
void qa_q3_cinematic_handles_restore_published(qa_q3_cinematic_handles *owner)
{ if (owner && owner->restoring) owner->restore_published=true; }
bool qa_q3_cinematic_source_restore_bind(qa_q3_cinematic_source *source,const char *instance,
    const char *content,int32_t role,uint32_t seat,uint32_t source_group,qa_error *error)
{
    if (!source || !qa_q3_cinematic_handles_idle(source->handles)) return false;
    char *instance_copy=text_copy(instance,error),*content_copy=text_copy(content,error);
    if ((instance && !instance_copy) || (content && !content_copy)) {
        free(instance_copy); free(content_copy); return false;
    }
    free(source->instance); free(source->content);
    source->instance=instance_copy; source->content=content_copy; source->role=role; source->seat=seat;
    source->detached=source_group!=UINT32_MAX;
    uint32_t group=source_group;
    for (size_t i=0;i<16;++i) {
        q3cin_movie *movie=source->handles->movies+i;
        const qa_q3_cinematic_restore_slot *saved=&movie->restore;
        if (!movie->restore_pending || movie->source || saved->detached!=source->detached ||
            saved->role!=role || saved->seat!=seat || !same_name(saved->instance,instance) ||
            !same_name(saved->content,content)) continue;
        if (group==UINT32_MAX) group=saved->source_group;
        if (group==saved->source_group) movie->source=source;
    }
    return true;
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
        (source->diagnostic_print || source->diagnostic_current || source->diagnostic_context ?
            !source->diagnostic_print || !source->diagnostic_current || source->diagnostic_context!=context : context!=NULL))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Detached cinematic role requires its returned actual diagnostic owner");
    for (size_t i=0;i<16;++i) {
        const q3cin_movie *movie=&source->handles->movies[i];
        if (movie->source==source && (movie->system.context || movie->system.status || movie->system.end ||
            movie->system.release || movie->system.playback))
            return q3cin_fail(error,QA_ERROR_ARGUMENT,"Detached cinematic role retains a real fullscreen lease on its retiring owner");
    }
    source->diagnostic_context=NULL; source->diagnostic_print=NULL; source->diagnostic_current=NULL;
    source->restore_open=NULL; source->restore_context=NULL;
    source->detached=true;
    source->options.print=source->parent->options.print?role_print:NULL;
    return true;
}
void qa_q3_cinematic_source_release(qa_q3_cinematic_source *source)
{ if (source && source->users) --source->users; }
bool q3cin_close(qa_q3_cinematic_handles *owner,uint32_t index,qa_cinematic_end reason,qa_error *error)
{
    q3cin_movie *movie=&owner->movies[index];
    if (movie->system.context || movie->system.status || movie->system.end || movie->system.release || movie->system.playback) {
        if (!movie->system.end || !movie->system.release)
            return q3cin_fail(error,QA_ERROR_ARGUMENT,"Partial system cinematic retains its real lifetime owner");
        if (!movie->system.end(movie->system.context,reason,error)) return false;
        movie->system.release(movie->system.context);
    }
    if (owner->restoring && movie->restore.path && !owner->stage) {
        if (owner->restore_selected==(int32_t)index) owner->restore_selected=-1;
        if (owner->restore_decoder==(int32_t)index) owner->restore_decoder=-1;
    }
    q3cin_movie_dispose(movie); return true;
}
bool qa_q3_cinematic_source_systems_close(qa_q3_cinematic_source *source,qa_error *error)
{
    if (!source || !qa_q3_cinematic_handles_idle(source->handles) || source->users)
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"System cinematic shutdown requires its returned actual Source");
    qa_q3_cinematic_handles *owner=source->handles;
    qa_q3_cinematic_source *member=owner->sources;
    while (member && member!=source) member=member->next;
    if (!member) return q3cin_fail(error,QA_ERROR_ARGUMENT,"System cinematic shutdown lost its actual pool membership");
    owner->busy=true;
    for (uint32_t i=0;i<16;++i) {
        const q3cin_movie *movie=&owner->movies[i];
        if (movie->source==source && ((movie->restore_pending && (movie->flags&1u)) ||
            movie->system.context || movie->system.status || movie->system.end ||
            movie->system.release || movie->system.playback) && !q3cin_close(owner,i,QA_CINEMATIC_STOPPED,error)) {
            owner->busy=false; return false;
        }
    }
    owner->busy=false; return true;
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
    qa_vfs_destroy(source->options.files); free(source->instance); free(source->content);
    free(source); *slot=NULL; owner->busy=false; return true;
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
qa_roq_scratch *qa_q3_cinematic_source_decoder_scratch(const qa_q3_cinematic_source *source,int32_t handle)
{
    if (!source || !source->handles || handle<0 || handle>=16) return NULL;
    const q3cin_movie *movie=source->handles->movies+handle;
    return movie->source==source?movie->decoder_scratch:NULL;
}
bool qa_q3_cinematic_system_select(qa_q3_cinematic_source *source,int32_t handle,
    qa_cinematic *playback,qa_error *error)
{
    if (!playback || handle<0 || handle>=16 || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    q3cin_movie *movie=&owner->movies[handle];
    bool ok=movie->source==source && (movie->flags&1u) && !movie->pending &&
        movie->system.playback && movie->system.playback(movie->system.context)==playback;
    if (!ok) q3cin_fail(error,QA_ERROR_ARGUMENT,"Fullscreen decoder lost its actual global cinematic slot");
    if (ok && movie->status==2) return returned(source,true,error);
    if (ok && owner->restoring && owner->restore_published) owner->restore_superseded=true;
    if (ok) owner->selected_handle=owner->decoder_handle=handle;
    return returned(source,ok,error);
}
qa_cinematic_options q3cin_options(const qa_q3_cinematic_source *source,uint32_t flags,uint64_t bus)
{
    const qa_q3_cinematic_source_options *s=&source->options;
    qa_cinematic_options options={.clock=s->clock,.target={.kind=flags&16u?QA_CINEMATIC_MATERIAL:QA_CINEMATIC_SEAT},
        .loop=(flags&2u)!=0,.hold=(flags&4u)!=0,.silent=(flags&8u)!=0,
        .audio=s->audio,.audio_bus=bus,.gain=1,.audio_audience={QA_CINEMATIC_AUDIO_SEAT,s->seat},
        .context=s->context,.diagnostic=s->print};
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
static bool dimensions(qa_q3_cinematic_handles *owner,q3cin_movie *movie,
    const qa_media_frame *picture,bool announce,qa_error *error)
{
    if (!picture || (movie->width==picture->width && movie->height==picture->height && movie->draw_width))
        return true;
    bool limited=false;
    if (!owner->options.ui_limits(owner->options.context,&limited,error)) return false;
    movie->width=picture->width; movie->height=picture->height;
    movie->draw_width=limited && picture->width>256?256:picture->width;
    movie->draw_height=limited && picture->height>256?256:picture->height;
    if (announce && limited && (picture->width!=256 || picture->height!=256) && movie->source->options.print)
        movie->source->options.print(movie->source->options.context,
            "HACK: approxmimating cinematic for Rage Pro or Voodoo\n");
    return true;
}
static bool playback_create(qa_q3_cinematic_source *source,q3cin_movie *movie,
    const qa_cinematic_checkpoint *checkpoint,qa_error *error)
{
    qa_cinematic_source asset=qa_cinematic_asset_source(movie->asset); asset.name=movie->path;
    movie->bus=source->options.audio_bus(source->options.context);
    qa_cinematic_options options=q3cin_options(source,movie->flags,movie->bus);
    options.roq_scratch=movie->decoder_scratch;
    return checkpoint?qa_cinematic_restore(&asset,&options,checkpoint,&movie->playback,error):
        qa_cinematic_create(&asset,&options,&movie->playback,error);
}
bool q3cin_play_into(qa_q3_cinematic_source *source,q3cin_movie slots[16],qa_media_library *media,
    const bool owned_slots[16],int32_t *selected,int32_t *decoder,
    const char *request,qa_scene_rect_f rect,uint32_t flags,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *context,
    int32_t *out,qa_error *error)
{
    char *path=path_copy(request,error);
    if (!path) return false;
    uint32_t restored=16;
    for (uint32_t i=0;i<16;++i) if (slots[i].restore_pending && slots[i].source==source &&
        slots[i].path && (slots[i].flags&1u)==(flags&1u) && !strcmp(slots[i].path,path)) { restored=i; break; }
    if (!(flags&1u)) for (uint32_t i=0;i<16;++i) if ((slots[i].occupied ||
        (source->handles->restoring && slots[i].restore.path)) && slots[i].path &&
        !slots[i].restore_pending && slots[i].source && restored==16 &&
        slots[i].source->options.files==source->options.files &&
        slots[i].source->options.media==source->options.media && !strcmp(slots[i].path,path)) {
        free(path); *out=(int32_t)i; return true;
    }
    uint32_t index=restored;
    if (index==16) { index=0; while (index<16 && slots[index].occupied) ++index; }
    if (index==16) { free(path); return q3cin_fail(error,QA_ERROR_FORMAT,"CIN_HandleForVideo: none free"); }
    qa_q3_cinematic_restore_slot saved={0};
    bool restore=restored!=16, actual=slots==source->handles->movies;
    bool owns_slot=actual || (owned_slots && owned_slots[index]);
    if (!restore && actual && source->handles->restoring && source->handles->restore_published)
        source->handles->restore_superseded=true;
    if (restore) {
        saved=slots[index].restore;
        if (!owns_slot) {
            saved.instance=text_copy(slots[index].restore.instance,error);
            saved.content=text_copy(slots[index].restore.content,error);
            saved.parent_instance=text_copy(slots[index].restore.parent_instance,error);
            saved.parent_content=text_copy(slots[index].restore.parent_content,error);
            saved.path=text_copy(slots[index].restore.path,error);
            saved.system_state.nextmap=text_copy(slots[index].restore.system_state.nextmap,error);
            if ((slots[index].restore.instance && !saved.instance) ||
                (slots[index].restore.content && !saved.content) ||
                (slots[index].restore.parent_instance && !saved.parent_instance) ||
                (slots[index].restore.parent_content && !saved.parent_content) || !saved.path ||
                (slots[index].restore.system_state.nextmap && !saved.system_state.nextmap)) {
                q3cin_movie failed={.restore=saved}; q3cin_restore_dispose(&failed); free(path); return false;
            }
        } else slots[index].restore=(qa_q3_cinematic_restore_slot){0};
        saved.playback.resource=saved.path;
        rect=saved.rect; flags=saved.flags;
    }
    bool terminal_system=restore && (flags&1u) && saved.system_state.phase!=QA_Q3_SYSTEM_MOVIE_PLAYING;
    qa_roq_scratch *scratch=NULL;
    if (!terminal_system && (!restore || saved.has_playback) && !qa_roq_scratch_create(&scratch,error)) {
        if (restore) {
            if (owns_slot) slots[index].restore=saved;
            else { q3cin_movie failed={.restore=saved}; q3cin_restore_dispose(&failed); }
        }
        free(path); return false;
    }
    if (actual && slots[index].source && !q3cin_close(source->handles,index,QA_CINEMATIC_STOPPED,error)) {
        if (restore) slots[index].restore=saved;
        qa_roq_scratch_release(scratch); free(path); return false;
    }
    if (!actual && owns_slot) q3cin_movie_dispose(slots+index);
    q3cin_movie *movie=&slots[index];
    *movie=(q3cin_movie){.source=source,.path=path,.rect=rect,.flags=flags,.decoder_scratch=scratch,.uploaded=UINT64_MAX,
        .pending=true,.occupied=true,.dirty=true,.play_on_walls=1,.status=1,.width=512,.height=512};
    if (restore) movie->restore=saved;
    if (restore && !saved.has_playback && !(flags&1u)) {
        movie->pending=false; movie->occupied=saved.occupied;
        movie->status=saved.status; movie->play_on_walls=saved.play_on_walls;
        *out=(int32_t)index; return true;
    }
    bool ok;
    if (flags&1u) {
        qa_error local={0};
        qa_q3_movie_request requested={.path=path,.loop=(flags&2u)!=0,.hold=(flags&4u)!=0,
            .silent=(flags&8u)!=0,.numeric_source=source,.numeric_handle=(int32_t)index,
            .restore_state=restore?&saved.system_state:NULL,
            .restore_playback=restore && saved.has_playback?&saved.playback:NULL};
        ok=open && open(context,&requested,&movie->system,&local);
        bool owned=movie->system.context || movie->system.status || movie->system.end ||
            movie->system.release || movie->system.playback;
        if (!ok && !owned && local.code==QA_ERROR_NOT_FOUND) {
            q3cin_movie_dispose(movie); *out=-1; return true;
        }
        if (!ok && !owned && local.code==QA_ERROR_FORMAT) {
            movie->pending=false; movie->status=0; *out=-1; return true;
        }
        if (!ok && error) *error=local;
        if (ok && (!movie->system.status || !movie->system.end || !movie->system.release))
            ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"System cinematic returned incomplete ownership");
    } else {
        qa_error local={0};
        ok=qa_media_library_load_source_roq(media,source->options.files,path,&movie->asset,&local);
        if (!ok && local.code==QA_ERROR_NOT_FOUND) {
            q3cin_movie_dispose(movie); *out=-1; return true;
        }
        if (!ok && local.code==QA_ERROR_FORMAT) {
            movie->pending=false; movie->status=0; *out=-1; return true;
        }
        if (!ok) { if (error) *error=local; }
        else {
            if (!source->options.in_game_video(source->options.context,&movie->play_on_walls,error)) {
                q3cin_movie_dispose(movie); return false;
            }
            ok=playback_create(source,movie,restore?&saved.playback:NULL,&local);
            if (!ok && local.code==QA_ERROR_FORMAT) {
                qa_cinematic_destroy(movie->playback); movie->playback=NULL;
                movie->pending=false; movie->status=0; *out=-1; return true;
            }
            if (!ok && error) *error=local;
        }
    }
    if (!ok) {
        if (movie->system.context || movie->system.status || movie->system.end || movie->system.release || movie->system.playback) {
            if (!movie->system.end || !movie->system.release ||
                !movie->system.end(movie->system.context,QA_CINEMATIC_STOPPED,NULL)) return false;
            movie->system.release(movie->system.context);
        }
        q3cin_movie_dispose(movie);
        return false;
    }
    movie->pending=false;
    if (restore) {
        qa_cinematic *restored_playback=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (!dimensions(source->handles,movie,restored_playback?qa_cinematic_frame(restored_playback):NULL,false,error))
            return false;
        movie->status=saved.status; movie->play_on_walls=saved.play_on_walls;
        movie->occupied=saved.occupied;
    }
    if (!terminal_system) *selected=*decoder=(int32_t)index;
    *out=(int32_t)index; return true;
}
bool qa_q3_cinematic_source_restore_finish(qa_q3_cinematic_source *source,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *context,
    qa_error *error)
{
    if (!source || !source->handles->restoring) return true;
    qa_q3_cinematic_handles *owner=source->handles;
    if (open) { source->restore_open=open; source->restore_context=context; }
    open=source->restore_open; context=source->restore_context;
    bool needed=false;
    for (size_t i=0;i<16;++i) if (owner->movies[i].restore_pending && owner->movies[i].source==source) needed=true;
    if (!needed) return true;
    if (!q3cin_enter(source,error)) return false;
    int32_t selected=owner->selected_handle,decoder=owner->decoder_handle;
    bool preserve=owner->restore_published && owner->restore_superseded;
    bool ok=true,system_opened=false;
    /* Finish ordinary slots before reopening fullscreen owners. Each movie
     * reconstructs its own decoder, including later lazy source slots. */
    for (unsigned phase=0;ok && phase<3;++phase) for (uint32_t i=0;ok && i<16;++i) {
        q3cin_movie *movie=owner->movies+i;
        bool system=(movie->flags&1u)!=0;
        unsigned selected_phase=system?(movie->restore.system_state.phase==QA_Q3_SYSTEM_MOVIE_PLAYING?2u:1u):0u;
        if (!movie->restore_pending || movie->source!=source || selected_phase!=phase ||
            (system && !open)) continue;
        int32_t handle;
        ok=q3cin_play_into(source,owner->movies,source->options.media,NULL,
            &owner->selected_handle,&owner->decoder_handle,movie->path,movie->rect,movie->flags,
            open,context,&handle,error);
        if (ok && system && handle>=0 && movie->system.playback &&
            movie->system.playback(movie->system.context)) system_opened=true;
    }
    if (ok && preserve && !system_opened) { owner->selected_handle=selected; owner->decoder_handle=decoder; }
    return returned(source,ok,error);
}
bool qa_q3_cinematic_handles_restore_finish(qa_q3_cinematic_handles *owner,bool *complete,qa_error *error)
{
    *complete=!owner || !owner->restoring;
    if (*complete) return true;
    for (qa_q3_cinematic_source *source=owner->sources;source;source=source->next)
        if (!qa_q3_cinematic_source_restore_finish(source,NULL,NULL,error)) return false;
    for (size_t i=0;i<16;++i) if (owner->movies[i].restore_pending) return true;
    int32_t decoder=owner->restore_decoder;
    if (!owner->restore_superseded) {
        owner->selected_handle=owner->restore_selected; owner->decoder_handle=decoder;
    }
    owner->restoring=false;
    for (qa_q3_cinematic_source *source=owner->sources;source;source=source->next) {
        source->restore_open=NULL; source->restore_context=NULL;
    }
    for (size_t i=0;i<16;++i) q3cin_restore_dispose(owner->movies+i);
    *complete=true; return true;
}
bool qa_q3_cinematic_play(qa_q3_cinematic_source *source,const char *path,qa_scene_rect_f rect,uint32_t flags,
    bool (*open)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *context,int32_t *out,qa_error *error)
{
    if (!path || !out || ((flags&1u) && !open) || !isfinite(rect.x) || !isfinite(rect.y) ||
        !isfinite(rect.width) || !isfinite(rect.height) || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    return returned(source,q3cin_play_into(source,owner->movies,source->options.media,NULL,
        &owner->selected_handle,&owner->decoder_handle,path,rect,flags,open,context,out,error),error);
}
static bool run(qa_q3_cinematic_handles *owner,int32_t handle,int32_t *out,qa_error *error)
{
    *out=2;
    if (handle<0 || handle>=16) return true;
    q3cin_movie *movie=&owner->movies[handle];
    if (movie->restore_pending) { *out=0; return true; }
    if (!movie->source) return true;
    if (owner->restoring && owner->restore_published) owner->restore_superseded=true;
    if (movie->pending || !movie->source->options.current(movie->source->options.context,&movie->source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic handle retains an unavailable physical source");
    if (movie->status==2) return true;
    if (!movie->playback && !movie->system.status) {
        *out=movie->status; return true;
    }
    qa_cinematic *decoder=movie->playback?movie->playback:
        movie->system.playback?movie->system.playback(movie->system.context):NULL;
    if (decoder) owner->decoder_handle=handle;
    if (decoder && movie->play_on_walls < -1) { *out=movie->status; return true; }
    owner->selected_handle=handle;
    if (decoder && movie->status==0) { *out=0; return true; }
    qa_media_tick tick={.status=QA_MEDIA_STOPPED};
    uint64_t before=decoder?qa_cinematic_revision(decoder):0;
    bool ok=true;
    if (decoder) ok=qa_cinematic_tick(decoder,&tick,error);
    else tick.status=movie->system.status(movie->system.context);
    if (!ok) return false;
    if (decoder && qa_cinematic_revision(decoder)!=before) movie->dirty=true;
    const qa_media_frame *picture=decoder?qa_cinematic_frame(decoder):NULL;
    if (!dimensions(owner,movie,picture,true,error)) return false;
    if (!movie->source->options.current(movie->source->options.context,&movie->source->options))
        return q3cin_fail(error,QA_ERROR_ARGUMENT,"Cinematic source changed during reached decoder callbacks");
    if (tick.looped) *out=5;
    else if (tick.status==QA_MEDIA_HELD) *out=0;
    else if (tick.status==QA_MEDIA_PLAYING || tick.status==QA_MEDIA_PAUSED) *out=1;
    else if (movie->playback) {
        movie->status=2;
        if (qa_cinematic_frame(movie->playback)) {
            movie->status=0; movie->occupied=false; owner->selected_handle=-1;
        }
        *out=movie->status; return true;
    } else {
        if (decoder && tick.status==QA_MEDIA_ENDED && !qa_cinematic_frame(decoder)) {
            movie->status=2; *out=2; return true;
        }
        return q3cin_close(owner,(uint32_t)handle,
            tick.status==QA_MEDIA_ENDED?QA_CINEMATIC_FINISHED:QA_CINEMATIC_STOPPED,error);
    }
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
    if (source->handles->restoring && source->handles->restore_published)
        source->handles->restore_superseded=true;
    if (handle>=0 && handle<16) {
        q3cin_movie *movie=&source->handles->movies[handle];
        if (movie->restore_pending)
            return returned(source,q3cin_close(source->handles,(uint32_t)handle,
                skip?QA_CINEMATIC_SKIPPED:QA_CINEMATIC_STOPPED,error),error);
        qa_cinematic *system=movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if ((!movie->playback && !movie->system.status) ||
            (movie->system.playback && (!system || !qa_cinematic_frame(system)))) {
            source->handles->selected_handle=handle; return returned(source,true,error);
        }
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
    if (handle>=0 && handle<16) {
        q3cin_movie *movie=&source->handles->movies[handle];
        qa_cinematic *decoder=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (decoder && movie->status!=2) {
            if (source->handles->restoring && source->handles->restore_published)
                source->handles->restore_superseded=true;
            movie->rect=rect; movie->dirty=true;
        }
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
    bool ok=decoder && movie->source==source && (movie->flags&1u) && !movie->pending;
    if (!ok) q3cin_fail(error,QA_ERROR_ARGUMENT,"Fullscreen draw lost its selected global decoder");
    uint64_t before=ok?qa_cinematic_revision(decoder):0;
    qa_media_tick tick={0};
    if (ok) ok=qa_cinematic_tick(decoder,&tick,error);
    if (ok && tick.status==QA_MEDIA_ENDED && !tick.frame) movie->status=2;
    bool visible=ok && tick.status!=QA_MEDIA_ENDED && tick.status!=QA_MEDIA_STOPPED && tick.frame;
    if (visible) {
        if (qa_cinematic_revision(decoder)!=before) movie->dirty=true;
        const qa_media_frame *picture=qa_cinematic_frame(decoder);
        ok=dimensions(owner,movie,picture,false,error);
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
    if (handle>=0 && handle<16) {
        q3cin_movie *movie=&source->handles->movies[handle];
        qa_cinematic *decoder=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (decoder && qa_cinematic_frame(decoder)) {
            ok=upload(source->handles,(uint32_t)handle,false,frame,error);
            if (ok) { *out=source->handles->scratch[handle]; *rect=movie->rect; }
        }
    }
    return returned(source,ok,error);
}
bool qa_q3_cinematic_draw_complete(qa_q3_cinematic_source *source,int32_t handle,qa_error *error)
{
    if (!q3cin_enter(source,error)) return false;
    if (handle>=0 && handle<16) {
        q3cin_movie *movie=&source->handles->movies[handle];
        qa_cinematic *decoder=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (decoder) movie->dirty=false;
    }
    return returned(source,true,error);
}
bool qa_q3_cinematic_shader_play(qa_q3_cinematic_source *source,const char *path,int32_t *handle,
    const qa_scene_image **out,qa_error *error)
{
    if (!out || !handle || !path || !q3cin_enter(source,error)) return false;
    qa_q3_cinematic_handles *owner=source->handles;
    bool ok=q3cin_play_into(source,owner->movies,source->options.media,NULL,
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
        const q3cin_movie *movie=&owner->movies[i];
        qa_cinematic *decoder=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL;
        if (ok && decoder && qa_cinematic_frame(decoder))
            ok=upload(owner,i,true,frame,error);
        if (ok) image=owner->scratch[i];
        break;
    }
    if (!found && ok) ok=q3cin_fail(error,QA_ERROR_ARGUMENT,"Reached video stage is not an actual cinematic scratch slot");
    return returned(source,ok,error)?image:NULL;
}
bool qa_q3_cinematic_handles_at(const qa_q3_cinematic_handles *owner,uint32_t index,qa_q3_cinematic_slot *out)
{
    if (!owner || owner->busy || index>=16 || !out) return false;
    const q3cin_movie *movie=&owner->movies[index];
    const qa_q3_cinematic_source *parent=movie->source?movie->source->parent:NULL;
    uint32_t group=movie->restore.source_group;
    if (movie->source) {
        group=index;
        for (uint32_t i=0;i<index;++i) if (owner->movies[i].source==movie->source) { group=i; break; }
    }
    *out=(qa_q3_cinematic_slot){.handle=(int32_t)index,.source=movie->source,
        .instance=movie->source?movie->source->instance:movie->restore.instance,
        .content=movie->source?movie->source->content:movie->restore.content,
        .parent_instance=parent?parent->instance:movie->restore.parent_instance,
        .parent_content=parent?parent->content:movie->restore.parent_content,
        .parent_seat=parent?parent->seat:movie->restore.parent_seat,.source_group=group,
        .detached=movie->source?movie->source->detached:movie->restore.detached,
        .role=movie->source?movie->source->role:movie->restore.role,
        .seat=movie->source?movie->source->seat:movie->restore.seat,
        .occupied=movie->occupied,
        .selected=(owner->restoring && !owner->restore_superseded?owner->restore_selected:owner->selected_handle)==(int32_t)index,
        .decoder=(owner->restoring && !owner->restore_superseded?owner->restore_decoder:owner->decoder_handle)==(int32_t)index,
        .restore_pending=movie->restore_pending,.path=movie->path,.asset=movie->asset,
        .playback=movie->playback?movie->playback:
            movie->system.playback?movie->system.playback(movie->system.context):NULL,
        .scratch=owner->scratch[index],
        .system=movie->flags&1u?&movie->system:NULL,.flags=movie->flags,
        .rect=movie->rect,.status=movie->status,.play_on_walls=movie->play_on_walls}; return true;
}
