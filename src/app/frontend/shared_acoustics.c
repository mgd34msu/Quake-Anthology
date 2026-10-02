#include "shared_acoustics.h"
#include "source_acoustics.h"
#include "capture.h"

struct frontend_shared_acoustics {
    qa_frontend *frontend;
    qa_application *application;
    qa_audio_engine *engine;
    const qa_cvars_edit *edit;
    frontend_shared_audio *parent;
    qa_audio_engine_gains *gains;
    qa_audio_engine_acoustics *child;
    char *value;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool canonical(const qa_cvars_edit *edit,bool *enabled,const char **text,qa_error *error)
{
    const qa_cvar_view *row=qa_cvars_edit_find(edit,"s_geometryAcoustics");
    if (!row || !row->value || (strcmp(row->value,"0") && strcmp(row->value,"1")))
        return fail(error,"Prepared acoustic policy requires its actual canonical 0 or 1 declaration");
    *enabled=!strcmp(row->value,"1"); *text=row->value; return true;
}
static bool current(const frontend_shared_acoustics *owner)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    return f && f->application==owner->application && f->audio==owner->engine &&
        !f->capture && !f->source_restoring && frontend_seat_callbacks_returned(f) &&
        qa_cvars_edit_registry(owner->edit)==qa_application_cvars(owner->application) &&
        frontend_shared_audio_parent_is(owner->parent,f,owner->engine) &&
        frontend_shared_audio_gains(owner->parent)==owner->gains;
}
static void release(frontend_shared_acoustics *owner)
{ free(owner->value); free(owner); }
bool frontend_shared_acoustics_prepare(qa_frontend *f,const qa_cvars_edit *edit,
    frontend_shared_audio *parent,frontend_shared_acoustics **out,qa_error *error)
{
    if (!f || !f->application || !edit || !out || *out ||
        qa_cvars_edit_registry(edit)!=qa_application_cvars(f->application) ||
        f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f))
        return fail(error,"Acoustic preparation requires its actual returned canonical ENGINE owner");
    bool enabled; const char *value;
    if (!canonical(edit,&enabled,&value,error)) return false;
    if (!f->audio) return (!parent && !f->device) ||
        fail(error,"Acoustic publication lacks its actual shared audio parent");
    qa_audio_engine_gains *gains=frontend_shared_audio_gains(parent);
    if (!gains || !frontend_shared_audio_parent_is(parent,f,f->audio))
        return fail(error,"Acoustic publication requires its admitted shared gains parent");
    frontend_shared_acoustics *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared acoustic policy");
    owner->frontend=f; owner->application=f->application; owner->engine=f->audio;
    owner->edit=edit; owner->parent=parent; owner->gains=gains;
    owner->value=malloc(strlen(value)+1);
    if (!owner->value) { release(owner); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining acoustic scalar text"); }
    strcpy(owner->value,value);
    qa_audio_acoustics_source source={0};
    if (enabled && !frontend_acoustics_source_hold(f,&source,error)) { release(owner); return false; }
    if (!qa_audio_engine_acoustics_prepare(gains,enabled,enabled?&source:NULL,&owner->child,error)) {
        if (source.context) source.release(source.context);
        release(owner); return false;
    }
    *out=owner; return true;
}
bool frontend_shared_acoustics_ready(frontend_shared_acoustics *owner,qa_error *error)
{
    bool enabled; const char *value;
    if (!current(owner) || !canonical(owner->edit,&enabled,&value,error) ||
        strcmp(value,owner->value))
        return fail(error,"Prepared acoustic scalar or source parent changed");
    return qa_audio_engine_acoustics_ready(owner->child,error);
}
bool frontend_shared_acoustics_ready_is(const frontend_shared_acoustics *owner)
{
    bool enabled; const char *value;
    return current(owner) && qa_cvars_edit_ready_is(owner->edit) &&
        canonical(owner->edit,&enabled,&value,NULL) && !strcmp(value,owner->value) &&
        qa_audio_engine_acoustics_ready_is(owner->child,owner->gains);
}
void frontend_shared_acoustics_publish(frontend_shared_acoustics **in)
{
    frontend_shared_acoustics *owner=*in;
    qa_audio_engine_acoustics_publish(owner->child);
    release(owner); *in=NULL;
}
bool frontend_shared_acoustics_abort(frontend_shared_acoustics **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_acoustics *owner=*in;
    if (!current(owner)) return fail(error,"Acoustic cancellation lost its actual gains parent");
    if (!qa_audio_engine_acoustics_abort(owner->child,error)) return false;
    release(owner); *in=NULL; return true;
}
