#include "internal.h"
#include "view_bindings.h"
#include "view_settings.h"
#include "qa/application_view_preferences.h"

static bool apply(qa_frontend *f,double value,qa_application_view_preference_mode mode,qa_error *error)
{
    return f && f->application &&
        qa_application_player_field_of_view_apply(f->application,value,mode,error);
}
static bool changed(void *context,double value,qa_error *error)
{ return apply(context,value,QA_APPLICATION_VIEW_CHANGE,error); }
bool frontend_view_bindings_create(qa_frontend *f,qa_error *error)
{
    return f && f->application && frontend_view_settings_create(f,
        qa_application_cvars(f->application),f,changed,&f->view_settings,error);
}
bool frontend_view_bindings_apply_restored(qa_frontend *f,qa_error *error)
{
    double value; bool explicit_override;
    if (!f || !f->view_settings || !frontend_view_settings_read(f->view_settings,&value,&explicit_override))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Published view preference lost its real canonical owner");
    return !explicit_override || apply(f,value,QA_APPLICATION_VIEW_RESTORE,error);
}

void frontend_view_bindings_restore_published(qa_frontend *f)
{ if (f) f->view_restore_pending=f->view_settings; }
bool frontend_view_bindings_finish_restore(qa_frontend *f,qa_error *error)
{
    if (!f || !f->view_restore_pending) return true;
    if (f->source_restoring || f->capture || f->resource_inventory ||
        f->view_restore_pending!=f->view_settings || !frontend_view_settings_parent_is(f->view_settings,f,qa_application_cvars(f->application)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored view recipients are not published yet");
    f->view_restore_pending=NULL;
    return frontend_view_bindings_apply_restored(f,error);
}
