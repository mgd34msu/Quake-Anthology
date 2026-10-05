#include "qa/ui_preferences.h"
#include "cinematic_captions.h"
#include "campaign_cinematic.h"
#include "system_cinematic.h"
#include "ui_features_private.h"
#include "save_private.h"
#include "qa/media_captions_save.h"

typedef struct cinematic_caption_seat {
    qa_media_captions *captions;
    qa_vfs *view;
    const qa_vfs *origin;
    char *path,*language,*failed_language;
    frontend_cinematic_language *language_ticket;
} cinematic_caption_seat;
struct frontend_cinematic_captions {
    qa_frontend *frontend;
    cinematic_caption_seat seats[QA_INPUT_LOCAL_SEATS];
};
struct frontend_cinematic_language {
    frontend_cinematic_captions *owner;
    uint32_t seat;
    cinematic_caption_seat original,candidate;
    const qa_vfs *movie_files;
    bool published;
};
bool frontend_ui_cinematic_idle(const qa_frontend *f)
{
    const frontend_cinematic_captions *owner=f && f->ui_features?f->ui_features->cinematic_captions:NULL;
    if (!owner) return true;
    if (owner->frontend!=f) return false;
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i)
        if (owner->seats[i].language_ticket ||
            (owner->seats[i].captions && !qa_media_captions_idle(owner->seats[i].captions))) return false;
    return true;
}
void frontend_ui_cinematic_clear(qa_frontend *f,uint32_t seat)
{
    frontend_cinematic_captions *owner=f && f->ui_features?f->ui_features->cinematic_captions:NULL;
    if (!owner || seat>=QA_INPUT_LOCAL_SEATS || owner->seats[seat].language_ticket ||
        (owner->seats[seat].captions && !qa_media_captions_idle(owner->seats[seat].captions))) return;
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
    if (!frontend_ui_cinematic_idle(f)) return;
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) {
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
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) {
        qa_media_caption_options options=caption_options(f,i);
        owner->seats[i].captions=qa_media_captions_create(&options,error);
        if (!owner->seats[i].captions) return false;
    }
    return true;
}
static bool movie_read(qa_frontend *f,const qa_vfs *files,const char *path,uint32_t seat,
    bool lookup_equal,frontend_cinematic_view *out,bool *found,qa_error *error)
{
    *found=false;
    for (unsigned kind=0;kind<2;++kind) {
        frontend_cinematic_view view; bool present=false;
        bool ok=kind?frontend_system_cinematic_view_read(f,&view,&present,error):
            frontend_cinematic_view_read(f,&view,&present,error);
        if (!ok) return false;
        if (!present || view.seat!=seat || !path || strcmp(view.path,path) ||
            (lookup_equal?!qa_vfs_lookup_equal(view.files,files):view.files!=files)) continue;
        *out=view; *found=true; return true;
    }
    return true;
}
static bool movie_current(const qa_frontend *f,const qa_vfs *files,const char *path,uint32_t seat)
{
    return frontend_cinematic_view_current(f,files,path,seat) ||
        frontend_system_cinematic_view_current(f,files,path,seat);
}
static bool current(qa_frontend *f,qa_vfs *files,const char *path,uint32_t seat,frontend_cinematic_view *out,qa_error *error)
{
    bool found=false;
    if (!f || !f->ui_features || !f->ui_features->cinematic_captions || f->ui_features->handling ||
        f->source_restoring || !frontend_ui_cinematic_idle(f) || seat>=f->options.seats || !files || !path || !*path)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitles require their idle actual cinematic owner");
    if (!movie_read(f,files,path,seat,false,out,&found,error)) return false;
    return (found && out->seat==seat && out->files==files && !strcmp(out->path,path)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Subtitle request lost its actual cinematic recipient and resource");
}
static char *copy_text(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *out=malloc(size);
    if (!out) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual subtitle identity"); return NULL; }
    memcpy(out,text,size); return out;
}
static void language_candidate_free(cinematic_caption_seat *state)
{
    qa_media_captions_destroy(state->captions); qa_vfs_destroy(state->view);
    free(state->path); free(state->language); free(state->failed_language);
}
bool frontend_ui_cinematic_language_prepare(qa_frontend *f,uint32_t seat,const char *language,
    frontend_cinematic_language **out,qa_error *error)
{
    if (!f || !f->ui_features || !out || *out || !language || seat>=f->options.seats ||
        f->ui_features->handling || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic language requires its returned actual caption owner");
    frontend_cinematic_captions *owner=f->ui_features->cinematic_captions;
    if (!owner) return true;
    if (owner->frontend!=f || owner->seats[seat].language_ticket ||
        !qa_media_captions_idle(owner->seats[seat].captions))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic language retains an active actual seat child");
    if (!owner->seats[seat].view) return true;
    cinematic_caption_seat *state=owner->seats+seat;
    frontend_cinematic_view movie; bool found=false;
    if (!movie_read(f,state->view,state->path,seat,true,&movie,&found,error)) return false;
    if (!found || movie.seat!=seat || !state->path || strcmp(movie.path,state->path) ||
        !qa_vfs_lookup_equal(movie.files,state->view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic language lost its actual movie resource and recipient");
    frontend_cinematic_language *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared cinematic language");
    ticket->owner=owner; ticket->seat=seat; ticket->original=*state; ticket->movie_files=movie.files;
    state->language_ticket=ticket;
    cinematic_caption_seat *candidate=&ticket->candidate;
    candidate->view=qa_vfs_clone(state->view,error); candidate->origin=movie.files;
    candidate->path=copy_text(state->path,error); candidate->language=copy_text(language,error);
    qa_media_caption_options options=caption_options(f,seat);
    candidate->captions=qa_media_captions_create(&options,error);
    bool ok=candidate->view && candidate->path && candidate->language && candidate->captions &&
        qa_media_captions_prepare(candidate->captions,candidate->view,candidate->path,language,error);
    if (!ok) {
        language_candidate_free(candidate); state->language_ticket=NULL; free(ticket); return false;
    }
    *out=ticket; return true;
}
static bool language_parent(const frontend_cinematic_language *ticket,qa_error *error)
{
    const frontend_cinematic_captions *owner=ticket?ticket->owner:NULL;
    const qa_frontend *f=owner?owner->frontend:NULL;
    return (f && f->ui_features && f->ui_features->cinematic_captions==owner &&
        !f->ui_features->handling && ticket->seat<f->options.seats &&
        owner->seats[ticket->seat].language_ticket==ticket &&
        qa_media_captions_idle(owner->seats[ticket->seat].captions) &&
        qa_media_captions_idle(ticket->candidate.captions)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic language lost its retained returned caption parents");
}
bool frontend_ui_cinematic_language_ready(const frontend_cinematic_language *ticket,qa_error *error)
{
    if (!language_parent(ticket,error) || ticket->published) return false;
    const cinematic_caption_seat *state=ticket->owner->seats+ticket->seat;
    const cinematic_caption_seat *original=&ticket->original;
    if (state->captions!=original->captions || state->view!=original->view || state->origin!=original->origin ||
        state->path!=original->path || state->language!=original->language || state->failed_language!=original->failed_language)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Active cinematic captions changed during preparation");
    frontend_cinematic_view movie; bool found=false;
    if (!movie_read(ticket->owner->frontend,ticket->movie_files,ticket->candidate.path,
        ticket->seat,false,&movie,&found,error)) return false;
    return (found && movie.seat==ticket->seat && movie.files==ticket->movie_files &&
        !strcmp(movie.path,ticket->candidate.path) &&
        qa_media_captions_prepared_is(ticket->candidate.captions,ticket->candidate.view,
            ticket->candidate.path,ticket->candidate.language)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Prepared cinematic language lost its exact current movie");
}
bool frontend_ui_cinematic_language_ready_is(const frontend_cinematic_language *ticket)
{
    if (!language_parent(ticket,NULL) || ticket->published) return false;
    const cinematic_caption_seat *state=ticket->owner->seats+ticket->seat;
    const cinematic_caption_seat *original=&ticket->original;
    return state->captions==original->captions && state->view==original->view &&
        state->origin==original->origin && state->path==original->path &&
        state->language==original->language && state->failed_language==original->failed_language &&
        movie_current(ticket->owner->frontend,ticket->movie_files,
            ticket->candidate.path,ticket->seat) &&
        qa_media_captions_prepared_is(ticket->candidate.captions,ticket->candidate.view,
            ticket->candidate.path,ticket->candidate.language);
}
void frontend_ui_cinematic_language_publish(frontend_cinematic_language *ticket)
{
    cinematic_caption_seat *state=ticket->owner->seats+ticket->seat;
    *state=ticket->candidate; state->language_ticket=ticket;
    ticket->candidate=ticket->original; ticket->published=true;
}
void frontend_ui_cinematic_language_commit(frontend_cinematic_language *ticket)
{
    frontend_ui_cinematic_language_publish(ticket);
    language_candidate_free(&ticket->candidate);
    ticket->owner->seats[ticket->seat].language_ticket=NULL;
    free(ticket);
}
static bool language_dispose(frontend_cinematic_language **in,bool published,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_cinematic_language *ticket=*in;
    if (!language_parent(ticket,error) || ticket->published!=published) return false;
    language_candidate_free(&ticket->candidate);
    ticket->owner->seats[ticket->seat].language_ticket=NULL;
    free(ticket); *in=NULL; return true;
}
bool frontend_ui_cinematic_language_finish(frontend_cinematic_language **in,qa_error *error)
{ return language_dispose(in,true,error); }
bool frontend_ui_cinematic_language_abort(frontend_cinematic_language **in,qa_error *error)
{ return language_dispose(in,false,error); }
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
    frontend_caption_collection copy={.arena=&scene->storage,.error=error};
    if (!qa_media_captions_visit(state->captions,path,source,status,enabled,frontend_caption_count,&copy,error)) return false;
    if (copy.count>SIZE_MAX/sizeof(*copy.values)) return frontend_fail(error,QA_ERROR_MEMORY,"Active subtitles overflow");
    copy.values=copy.count?qa_arena_alloc(copy.arena,copy.count*sizeof(*copy.values),_Alignof(qa_active_caption),error):NULL;
    if (copy.count && !copy.values) return false;
    copy.count=0;
    if (!qa_media_captions_visit(state->captions,path,source,status,enabled,frontend_caption_collect,&copy,error) || copy.failed) return false;
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
