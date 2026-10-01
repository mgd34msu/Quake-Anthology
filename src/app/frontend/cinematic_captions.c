#include "cinematic_captions.h"
#include "campaign_cinematic.h"
#include "ui_features_private.h"
#include "save_private.h"
#include "qa/media_captions_save.h"

typedef struct cinematic_caption_seat {
    qa_media_captions *captions;
    qa_vfs *view;
    const qa_vfs *origin;
    char *path,*language,*failed_language;
} cinematic_caption_seat;
struct frontend_cinematic_captions {
    qa_frontend *frontend;
    cinematic_caption_seat seats[QA_INPUT_LOCAL_SEATS];
};
bool frontend_ui_cinematic_idle(const qa_frontend *f)
{
    const frontend_cinematic_captions *owner=f && f->ui_features?f->ui_features->cinematic_captions:NULL;
    if (!owner) return true;
    if (owner->frontend!=f) return false;
    for (unsigned i=0;i<f->options.seats;++i)
        if (owner->seats[i].captions && !qa_media_captions_idle(owner->seats[i].captions)) return false;
    return true;
}
void frontend_ui_cinematic_clear(qa_frontend *f,uint32_t seat)
{
    frontend_cinematic_captions *owner=f && f->ui_features?f->ui_features->cinematic_captions:NULL;
    if (!owner || seat>=f->options.seats || (owner->seats[seat].captions && !qa_media_captions_idle(owner->seats[seat].captions))) return;
    cinematic_caption_seat *state=owner->seats+seat;
    qa_media_captions_clear(state->captions); qa_vfs_destroy(state->view);
    free(state->path); free(state->language); free(state->failed_language);
    state->view=NULL; state->origin=NULL; state->path=state->language=NULL;
    state->failed_language=NULL;
}
void frontend_ui_cinematic_destroy(qa_frontend *f)
{
    frontend_cinematic_captions *owner=f && f->ui_features?f->ui_features->cinematic_captions:NULL;
    if (!owner) return;
    for (unsigned i=0;i<f->options.seats;++i) {
        frontend_ui_cinematic_clear(f,i); qa_media_captions_destroy(owner->seats[i].captions);
    }
    free(owner); f->ui_features->cinematic_captions=NULL;
}
static qa_media_caption_options caption_options(const qa_frontend *f,uint32_t seat)
{
    return (qa_media_caption_options){.seat=seat,.kind=QA_CAPTION_SUBTITLE,
        .tracks=f->ui_features->tracks,.catalogs=f->ui_features->catalogs,
        .localization={.profile=QA_LOCALIZATION_Q1_RERELEASE}};
}
bool frontend_ui_cinematic_init(qa_frontend *f,qa_error *error)
{
    if (f->options.dedicated) return true;
    frontend_cinematic_captions *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Creating actual cinematic subtitle owner");
    owner->frontend=f; f->ui_features->cinematic_captions=owner;
    for (unsigned i=0;i<f->options.seats;++i) {
        qa_media_caption_options options=caption_options(f,i);
        owner->seats[i].captions=qa_media_captions_create(&options,error);
        if (!owner->seats[i].captions) return false;
    }
    return true;
}
static bool current(qa_frontend *f,qa_vfs *files,const char *path,uint32_t seat,frontend_cinematic_view *out,qa_error *error)
{
    bool found=false;
    if (!f || !f->ui_features || !f->ui_features->cinematic_captions || f->ui_features->handling ||
        f->source_restoring || seat>=f->options.seats || !files || !path || !*path)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitles require their idle actual cinematic owner");
    if (!frontend_cinematic_view_read(f,out,&found,error)) return false;
    return (found && out->seat==seat && out->files==files && !strcmp(out->path,path)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitle request lost its actual cinematic recipient and resource");
}
static char *copy_text(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *out=malloc(size);
    if (!out) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual subtitle identity"); return NULL; }
    memcpy(out,text,size); return out;
}
bool frontend_ui_cinematic_prepare(qa_frontend *f,qa_vfs *files,const char *path,uint32_t seat,qa_error *error)
{
    frontend_cinematic_view movie;
    if (!current(f,files,path,seat,&movie,error)) return false;
    cinematic_caption_seat *state=f->ui_features->cinematic_captions->seats+seat;
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(f->application),seat,&preferences,error)) return false;
    if (state->origin==files && state->path && !strcmp(state->path,path) && !strcmp(state->language,preferences.language)) {
        free(state->failed_language); state->failed_language=NULL; return true;
    }
    qa_vfs *view=qa_vfs_clone(files,error);
    char *saved_path=copy_text(path,error),*language=copy_text(preferences.language,error);
    if (!view || !saved_path || !language) { qa_vfs_destroy(view); free(saved_path); free(language); return false; }
    qa_media_caption_options options=caption_options(f,seat);
    qa_media_captions *candidate=qa_media_captions_create(&options,error);
    if (!candidate) { qa_vfs_destroy(view); free(saved_path); free(language); return false; }
    f->ui_features->handling=true;
    bool ok=qa_media_captions_prepare(candidate,view,path,preferences.language,error);
    f->ui_features->handling=false;
    if (!ok) { qa_media_captions_destroy(candidate); qa_vfs_destroy(view); free(saved_path); free(language); return false; }
    qa_media_captions_destroy(state->captions); state->captions=candidate;
    qa_vfs_destroy(state->view); free(state->path); free(state->language);
    free(state->failed_language); state->failed_language=NULL;
    state->view=view; state->origin=files; state->path=saved_path; state->language=language; return true;
}
typedef struct caption_copy {
    qa_arena *arena;
    qa_active_caption *values;
    size_t count;
    bool failed;
    qa_error *error;
} caption_copy;
static void count_caption(void *context,const qa_active_caption *value)
{ (void)value; ++((caption_copy *)context)->count; }
static void copy_caption(void *context,const qa_active_caption *value)
{
    caption_copy *copy=context;
    if (copy->failed) return;
    qa_active_caption active=*value;
    size_t text_size=strlen(value->text)+1,speaker_size=value->speaker?strlen(value->speaker)+1:0;
    char *text=qa_arena_alloc(copy->arena,text_size,1,copy->error);
    char *speaker=speaker_size?qa_arena_alloc(copy->arena,speaker_size,1,copy->error):NULL;
    if (!text || (speaker_size && !speaker)) { copy->failed=true; return; }
    memcpy(text,value->text,text_size); if (speaker_size) memcpy(speaker,value->speaker,speaker_size);
    active.text=text; active.speaker=speaker; copy->values[copy->count++]=active;
}
bool frontend_ui_cinematic_draw(qa_frontend *f,qa_vfs *files,const char *path,uint32_t seat,double elapsed,
    double source,uint64_t loop,qa_media_status status,qa_scene_rect viewport,qa_scene_frame *scene,qa_error *error)
{
    frontend_cinematic_view movie;
    if (!f || scene!=&f->frame) return frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitles require their actual scene frame");
    if (!current(f,files,path,seat,&movie,error)) return false;
    if (movie.elapsed_ms!=elapsed || movie.source_ms!=source || movie.loop!=loop || movie.status!=status ||
        movie.viewport.x!=viewport.x || movie.viewport.y!=viewport.y ||
        movie.viewport.width!=viewport.width || movie.viewport.height!=viewport.height)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitle clock and viewport belong to another cinematic frame");
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(f->application),seat,&preferences,error)) return false;
    cinematic_caption_seat *state=f->ui_features->cinematic_captions->seats+seat;
    if (state->failed_language && !strcmp(state->failed_language,preferences.language)) return true;
    qa_error preparation={0};
    if (!frontend_ui_cinematic_prepare(f,files,path,seat,&preparation)) {
        char *language=copy_text(preferences.language,error);
        if (!language) return false;
        free(state->failed_language); state->failed_language=language;
        frontend_print(f,preparation.message); return true;
    }
    if (!preferences.captions || viewport.width<=16 || !viewport.height) return true;
    qa_caption_preferences enabled={.subtitles=true,.sound_captions=true,.speakers=true};
    caption_copy copy={.arena=&scene->storage,.error=error};
    if (!qa_media_captions_visit(state->captions,path,source,status,enabled,count_caption,&copy,error)) return false;
    if (copy.count>SIZE_MAX/sizeof(*copy.values)) return frontend_fail(error,QA_ERROR_MEMORY,"Active subtitles overflow");
    copy.values=copy.count?qa_arena_alloc(copy.arena,copy.count*sizeof(*copy.values),_Alignof(qa_active_caption),error):NULL;
    if (copy.count && !copy.values) return false;
    copy.count=0;
    if (!qa_media_captions_visit(state->captions,path,source,status,enabled,copy_caption,&copy,error) || copy.failed) return false;
    return qa_ui_captions_draw(f->seats[seat].ui,scene,viewport,
        (qa_scene_rect_f){(float)viewport.x+8,(float)viewport.y+(float)viewport.height*.65f,
            (float)viewport.width-16,(float)viewport.height*.30f},
        fminf((float)viewport.width/640,(float)viewport.height/480),copy.values,copy.count,error);
}
bool frontend_ui_cinematic_content_visit(const qa_frontend *f,const qa_application_content_visitor *visitor,qa_error *error)
{
    frontend_cinematic_captions *owner=f->ui_features->cinematic_captions;
    for (unsigned i=0;owner && i<f->options.seats;++i)
        if (owner->seats[i].view && !visitor->view(visitor->context,owner->seats[i].view,error)) return false;
    return true;
}
bool frontend_ui_cinematic_fields(qa_source_save_io *io,qa_frontend *f)
{
    frontend_cinematic_captions *owner=f->ui_features->cinematic_captions;
    if (f->options.dedicated) return owner==NULL;
    if (!owner || !frontend_ui_cinematic_idle(f)) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    for (unsigned i=0;i<f->options.seats;++i) {
        cinematic_caption_seat *state=owner->seats+i;
        if (reading && (state->view || state->path || state->language || state->origin || state->failed_language)) return false;
        uint64_t view=reading || !state->view?0:qa_application_content_view_id(qa_application_content_graph_read(f->application),state->view);
        if ((!reading && state->view && !view) || !qa_source_save_u64(io,&view) ||
            !frontend_save_text(io,&state->path) || !frontend_save_text(io,&state->language) ||
            !frontend_save_text(io,&state->failed_language) ||
            ((view!=0)!=(state->path!=NULL)) || ((view!=0)!=(state->language!=NULL))) return false;
        if (state->failed_language) {
            if (!view || !*state->failed_language || !strcmp(state->failed_language,state->language)) return false;
            for (const char *p=state->failed_language;*p;++p) if (*p<'a' || *p>'z') return false;
        }
        if (reading && view && !qa_application_content_claim_view(qa_application_content_graph_read(f->application),view,&state->view,io->error)) return false;
        qa_buffer saved={0}; size_t size=0;
        bool ok=reading || qa_media_captions_checkpoint(state->captions,state->view,state->path,&saved,io->error);
        if (!reading) size=saved.size;
        ok=ok && qa_source_save_count(io,&size,reading?io->input.size-io->offset:SIZE_MAX);
        if (ok && reading) {
            if (size>io->input.size-io->offset) { qa_buffer_free(&saved); return false; }
            qa_bytes bytes={io->input.data+io->offset,size}; io->offset+=size;
            ok=qa_media_captions_restore(state->captions,state->view,state->path,bytes,io->error);
        } else if (ok) ok=qa_source_save_bytes(io,saved.data,size);
        ok=ok && qa_media_captions_prepared_is(state->captions,state->view,state->path,state->language);
        qa_buffer_free(&saved); if (!ok) return false;
    }
    return true;
}
