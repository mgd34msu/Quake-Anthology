#include "shared_ui.h"
#include "ui_features_private.h"
#include "capture.h"
#include "menu_fonts.h"
#include "cinematic_captions.h"
#include "qa/ui_preferences.h"
#include "qa/ui_presentation_prepare.h"
#include "qa/media_caption_prepare.h"
#include <limits.h>

typedef struct prepared_seat {
    qa_ui *ui;
    qa_font_selection fonts;
    qa_ui_preferences preferences;
    char *language;
    qa_localization *catalog,*previous_catalog;
    char *previous_language;
    qa_ui_presentation_ticket *presentation;
    qa_sound_caption_language *sound;
    frontend_cinematic_language *cinematic;
} prepared_seat;
struct frontend_shared_ui {
    qa_frontend *frontend;
    qa_application *application;
    frontend_ui_features *features;
    frontend_seat *seats;
    unsigned count;
    qa_vfs *original_view,*view;
    const qa_cvars_edit *edit;
    prepared_seat seat[QA_INPUT_LOCAL_SEATS];
    bool prepared,published;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static bool current(const frontend_shared_ui *owner,qa_error *e)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    if (!f || f->application!=owner->application || f->ui_features!=owner->features ||
        !owner->features || owner->features->frontend!=f || owner->features->shared_ui!=owner || owner->features->handling ||
        f->seats!=owner->seats || f->options.seats!=owner->count || f->ui_mounts!=owner->original_view ||
        !frontend_seat_callbacks_returned(f))
        return fail(e,"Prepared UI lost its actual returned frontend/font/catalog parents");
    for (unsigned i=0;i<owner->count;++i)
        if (f->seats[i].frontend!=f || f->seats[i].id!=i || f->seats[i].ui!=owner->seat[i].ui)
            return fail(e,"Prepared UI lost its physical controller");
    return true;
}
bool frontend_shared_ui_prepare(qa_frontend *f,const qa_cvars_edit *edit,
    frontend_shared_ui **out,qa_error *e)
{
    if (!f || !f->application || !edit || qa_cvars_edit_registry(edit)!=qa_application_cvars(f->application) ||
        !out || *out || f->capture || f->source_restoring || !frontend_ui_features_idle(f))
        return fail(e,"UI preparation requires the actual canonical ENGINE ticket and idle children");
    if (f->options.dedicated) return true;
    if (!f->ui_features || !f->seats || !f->ui_mounts || !f->fonts || !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS)
        return fail(e,"UI preparation requires its installed physical seats and typography resources");
    for (unsigned i=0;i<f->options.seats;++i)
        if (!f->seats[i].ui || f->seats[i].frontend!=f || f->seats[i].id!=i ||
            !f->ui_features->seats[i].captions)
            return fail(e,"UI preparation requires fully constructed actual seat children");
    frontend_shared_ui *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining prepared shared UI children");
    owner->frontend=f; owner->application=f->application; owner->features=f->ui_features;
    owner->seats=f->seats; owner->count=f->options.seats; owner->edit=edit;
    owner->original_view=f->ui_mounts;
    for (unsigned i=0;i<owner->count;++i) owner->seat[i].ui=f->seats[i].ui;
    owner->features->shared_ui=owner; *out=owner;
    if (!current(owner,e)) return false;
    uint64_t frame=f->audio?qa_audio_engine_clock(f->audio):0;
    if (frame>INT64_MAX) return fail(e,"Prepared caption clock exceeds the real audio range");
    owner->view=qa_vfs_clone(f->ui_mounts,e);
    if (!owner->view) return false;
    for (unsigned i=0;i<owner->count;++i) {
        prepared_seat *seat=owner->seat+i;
        if (!qa_ui_preferences_edit_read(edit,i,&seat->preferences,e) ||
            !frontend_menu_font_selection(f,i,seat->preferences.typeface==QA_UI_TYPEFACE_BOLD,&seat->fonts,e) ||
            !qa_ui_presentation_prepare(seat->ui,&seat->fonts,seat->preferences.text_scale,
                seat->preferences.color_mode,&seat->presentation,e)) return false;
        size_t size=strlen(seat->preferences.language)+1;
        seat->language=malloc(size);
        if (!seat->language) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining prepared physical UI language");
        memcpy(seat->language,seat->preferences.language,size); seat->preferences.language=seat->language;
        frontend_ui_seat_features *state=owner->features->seats+i;
        seat->previous_catalog=state->localization; seat->previous_language=state->language;
        if (!qa_localization_acquire(owner->features->catalogs,owner->view,seat->language,
            &(qa_localization_options){.profile=owner->features->ui_profile},&seat->catalog,e) ||
            !qa_sound_caption_language_prepare(state->captions,seat->language,(int64_t)frame,&seat->sound,e) ||
            !frontend_ui_cinematic_language_prepare(f,i,seat->language,&seat->cinematic,e)) return false;
    }
    owner->prepared=true; return true;
}
static bool same_preferences(const qa_ui_preferences *a,const qa_ui_preferences *b)
{
    return a->hud_scale==b->hud_scale && a->text_scale==b->text_scale && a->menu_scale==b->menu_scale &&
        a->crosshair_size==b->crosshair_size && a->high_contrast==b->high_contrast &&
        a->reduced_flashes==b->reduced_flashes && a->captions==b->captions && a->crosshair==b->crosshair &&
        a->typeface==b->typeface && a->color_mode==b->color_mode && !strcmp(a->language,b->language);
}
bool frontend_shared_ui_ready(const frontend_shared_ui *owner,qa_error *e)
{
    if (!current(owner,e) || !owner->prepared || owner->published ||
        qa_cvars_edit_registry(owner->edit)!=qa_application_cvars(owner->application) ||
        !qa_vfs_lookup_equal(owner->original_view,owner->view))
        return fail(e,"Prepared UI has no current canonical resource publication");
    for (unsigned i=0;i<owner->count;++i) {
        const prepared_seat *seat=owner->seat+i;
        const frontend_ui_seat_features *state=owner->features->seats+i;
        qa_ui_preferences proposed;
        if (!qa_ui_preferences_edit_read(owner->edit,i,&proposed,e) || !same_preferences(&seat->preferences,&proposed) ||
            state->language!=seat->previous_language || state->localization!=seat->previous_catalog)
            return fail(e,"Prepared UI preference or active catalog changed before publication");
        if (!qa_ui_presentation_ready(seat->presentation,e) || !qa_sound_caption_language_ready(seat->sound,e) ||
            (seat->cinematic && !frontend_ui_cinematic_language_ready(seat->cinematic,e))) return false;
    }
    return true;
}
void frontend_shared_ui_publish(frontend_shared_ui *owner)
{
    for (unsigned i=0;i<owner->count;++i) {
        prepared_seat *seat=owner->seat+i;
        frontend_ui_seat_features *state=owner->features->seats+i;
        qa_ui_presentation_publish(seat->presentation); seat->presentation=NULL;
        owner->seats[i].fonts=seat->fonts;
        state->language=seat->language; state->localization=seat->catalog;
        seat->language=seat->previous_language; seat->catalog=seat->previous_catalog;
        qa_sound_caption_language_publish(seat->sound);
        if (seat->cinematic) frontend_ui_cinematic_language_publish(seat->cinematic);
    }
    owner->published=true;
}
static bool dispose(frontend_shared_ui **in,bool published,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_shared_ui *owner=*in;
    if (!current(owner,e) || owner->published!=published)
        return fail(e,"Prepared UI cleanup lost its retained publication state");
    for (unsigned i=0;i<owner->count;++i) {
        prepared_seat *seat=owner->seat+i;
        if (seat->cinematic && !(published?frontend_ui_cinematic_language_finish(&seat->cinematic,e):
            frontend_ui_cinematic_language_abort(&seat->cinematic,e))) return false;
        if (seat->sound && !(published?qa_sound_caption_language_finish(&seat->sound,e):
            qa_sound_caption_language_abort(&seat->sound,e))) return false;
        if (seat->presentation) {
            if (!qa_ui_presentation_abort(seat->presentation,e)) return false;
            seat->presentation=NULL;
        }
        qa_localization_release(seat->catalog); seat->catalog=NULL;
        free(seat->language); seat->language=NULL;
    }
    qa_vfs_destroy(owner->view); owner->features->shared_ui=NULL; free(owner); *in=NULL; return true;
}
bool frontend_shared_ui_finish(frontend_shared_ui **in,qa_error *e)
{ return dispose(in,true,e); }
bool frontend_shared_ui_abort(frontend_shared_ui **in,qa_error *e)
{ return dispose(in,false,e); }
