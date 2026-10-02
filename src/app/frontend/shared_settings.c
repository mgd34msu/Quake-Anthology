#include "shared_settings_private.h"
#include "shared_publication.h"
#include "capture.h"
#include "shared_video.h"
#include "shared_audio.h"
#include "qa/text.h"
#include "qa/application_engine_shutdown.h"

static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool root_current(const frontend_shared_settings *owner,const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    qa_console *console=NULL; qa_cvars *registry=NULL; qa_command_context command={0};
    return qa_application_startup_root_read(app,candidate,&console,&registry,&command,NULL) &&
        console==owner->root_console && registry==frontend_shared_values_registry(owner->values) &&
        !command.owner;
}
bool frontend_shared_settings_current(const frontend_shared_settings *owner,const qa_frontend *f,
    const qa_application *app,const qa_launch_snapshot *candidate)
{
    return owner && f && owner->frontend==f && owner->application==app && f->application==app &&
        owner->manager==f->config_store && owner->candidate==candidate &&
        (owner->client?(!candidate && qa_application_client_prepare_associated(app,owner->client)):
            owner->root_console?root_current(owner,app,candidate):
            candidate && qa_application_startup_candidate(app)==candidate) &&
        (!owner->root_console || owner->root_console==qa_application_console(owner->application)) &&
        frontend_shared_values_registry(owner->values)==qa_application_cvars(owner->application) &&
        !f->stepping && !f->capture && !f->source_restoring && frontend_seat_callbacks_returned(f) &&
        (!owner->input || (f->input_settings==owner->input &&
            frontend_input_settings_current(owner->input,f,NULL)));
}
bool frontend_shared_settings_begin(qa_frontend *f,frontend_config_store *manager,qa_application *app,
    const qa_launch_snapshot *candidate,const qa_application_startup_source *source,
    frontend_shared_settings **out,qa_error *error)
{
    if (!out || !f || f->application!=app || f->config_store!=manager || !candidate ||
        !source || !frontend_config_store_source_pending(manager,app,candidate,source))
        return fail(error,"Shared settings require the linked actual candidate source");
    frontend_shared_settings *owner=*out;
    if (owner) {
        if (!frontend_shared_settings_current(owner,f,app,candidate) || owner->aborting ||
            owner->before_complete || owner->after_started)
            return fail(error,"Source cannot join a finished or unrelated shared settings preparation");
        return frontend_shared_values_begin(f,manager,app,candidate,source,&owner->values,error);
    }
    owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual shared settings candidate");
    owner->frontend=f; owner->manager=manager; owner->application=app; owner->candidate=candidate;
    if (!frontend_shared_values_begin(f,manager,app,candidate,source,&owner->values,error)) {
        free(owner); return false;
    }
    *out=owner; return true;
}
frontend_shared_values *frontend_shared_settings_values(const frontend_shared_settings *owner)
{ return owner?owner->values:NULL; }
qa_application_client_preparation *frontend_shared_settings_client(const frontend_shared_settings *owner)
{ return owner?owner->client:NULL; }
bool frontend_shared_settings_begin_client(qa_frontend *f,frontend_config_store *manager,qa_application *app,
    qa_application_client_preparation *client,frontend_shared_settings **out,qa_error *error)
{
    if (!out || *out || !f || f->application!=app || f->config_store!=manager ||
        !qa_application_client_prepare_associated(app,client))
        return fail(error,"Shared settings require the actual standalone physical CLIENT preparation");
    frontend_shared_settings *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining standalone CLIENT shared settings");
    owner->frontend=f; owner->manager=manager; owner->application=app; owner->client=client;
    if (!frontend_shared_values_begin_client(f,manager,app,client,&owner->values,error)) { free(owner); return false; }
    *out=owner; return true;
}
bool frontend_shared_settings_begin_root(qa_frontend *f,frontend_config_store *manager,qa_application *app,
    const qa_launch_snapshot *candidate,frontend_shared_settings **out,qa_error *error)
{
    if (!out || !f || f->application!=app || f->config_store!=manager ||
        !qa_application_startup_root_phase(app,candidate))
        return fail(error,"Shared settings require the actual entered ENGINE root phase");
    frontend_shared_settings *owner=*out;
    if (owner) {
        if (!frontend_shared_settings_current(owner,f,app,candidate) || owner->aborting ||
            owner->before_complete || owner->after_started)
            return fail(error,"Shared root cannot join a finished or unrelated preparation");
        return frontend_shared_values_begin_root(f,manager,app,candidate,&owner->values,error);
    }
    owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining real ENGINE root settings");
    owner->frontend=f; owner->manager=manager; owner->application=app; owner->candidate=candidate;
    owner->root_console=qa_application_console(app);
    if (!frontend_shared_values_begin_root(f,manager,app,candidate,&owner->values,error)) {
        free(owner); return false;
    }
    *out=owner; return true;
}
bool frontend_shared_settings_refresh(frontend_shared_settings *owner,
    const qa_application_startup_source *source,qa_error *error)
{
    return owner && !owner->aborting &&
        frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate) &&
        frontend_shared_values_refresh(owner->values,source,error);
}
static bool project(const frontend_shared_settings *owner,qa_input_platform_settings *out,qa_error *error)
{
    static const char *const names[]={"in_mouse","in_nograb","in_joystick","in_joystickProfile",
        "in_midi","in_joystickSeat","in_midiseat","in_mididevice","in_midichannel",
        "joy_threshold","in_joyBallScale"};
    const qa_cvars_edit *edit=frontend_shared_values_prepared(owner->values);
    const qa_cvar_view *row[sizeof(names)/sizeof(*names)];
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) {
        row[i]=qa_cvars_edit_find(edit,names[i]);
        if (!row[i]) return fail(error,"Input settings lack an actual canonical declaration");
    }
    if (strcmp(row[3]->value,"linux") && strcmp(row[3]->value,"windows"))
        return fail(error,"in_joystickProfile must be linux or windows");
    if (!isfinite(row[9]->number) || !isfinite(row[10]->number))
        return fail(error,"Source joystick tuning must be finite");
    *out=(qa_input_platform_settings){.mouse_available=row[0]->integer!=0,.no_grab=row[1]->integer!=0,
        .joystick_enabled=row[2]->integer!=0,.windows_joystick=!strcmp(row[3]->value,"windows"),
        .midi_enabled=row[4]->integer!=0,.joystick_seat=row[5]->integer,.midi_seat=row[6]->integer,
        .midi_device=row[7]->integer,.midi_channel=row[8]->integer,
        .joystick_threshold=row[9]->number,.joystick_ball_scale=row[10]->number,
        .restart_requested=frontend_shared_values_input_restart_pending(owner->values)};
    return true;
}
bool frontend_shared_settings_constructor_settings(frontend_shared_settings *owner,
    qa_display_options *display,int *swap_interval,float *gamma,qa_audio_output_format *output,
    float *effects,float *music,qa_input_platform_settings *input,qa_error *error)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    if (!display || !swap_interval || !gamma || !output || !effects || !music || !input ||
        !f || owner->candidate || !owner->root_console || owner->aborting || owner->consumed ||
        owner->input || owner->publication || owner->before_complete || owner->after_started ||
        f->display || f->input || f->audio || f->device || f->cpu || f->gl ||
        !frontend_shared_settings_current(owner,f,owner->application,NULL) ||
        !qa_application_startup_bootstrap_images_ready(owner->application))
        return fail(error,"Native constructors require their actual completed bootstrap images owner");
    const qa_cvars_edit *edit=frontend_shared_values_prepared(owner->values);
    if (!qa_cvars_edit_returned_is(edit,qa_application_cvars(owner->application)))
        return fail(error,"Native constructors lost their returned canonical preparation");
    if (!frontend_shared_values_native_initialize(owner->values,error)) return false;
    static const char *const names[]={"r_customwidth","r_customheight","r_fullscreen","r_swapInterval","r_gamma"};
    const qa_cvar_view *row[5];
    for (size_t i=0;i<5;++i) {
        row[i]=qa_cvars_edit_find(edit,names[i]);
        if (!row[i] || !row[i]->value) return fail(error,"Native constructors lost a canonical setting");
    }
    double width=row[0]->number?(double)row[0]->number:(double)display->width;
    double height=row[1]->number?(double)row[1]->number:(double)display->height;
    double fullscreen=row[2]->number,swap=row[3]->number,brightness=0;
    if (!isfinite(width) || !isfinite(height) || floor(width)!=width || floor(height)!=height ||
        width<64 || width>16384 || height<64 || height>16384 ||
        (fullscreen!=0 && fullscreen!=1) || (swap!=0 && swap!=1) ||
        !qa_parse_ecmascript_number((qa_bytes){(const uint8_t *)row[4]->value,strlen(row[4]->value)},
            &brightness,error) || !isfinite(brightness) || brightness<.5 || brightness>3)
        return fail(error,"Bootstrap native settings require valid size, fullscreen, swap and brightness");
    qa_audio_output_format prepared_output; float prepared_effects,prepared_music;
    qa_input_platform_settings prepared_input;
    if (!frontend_shared_audio_configuration(edit,&prepared_output,&prepared_effects,&prepared_music,error) ||
        !project(owner,&prepared_input,error)) return false;
    qa_display_options prepared_display=*display;
    prepared_display.width=(uint32_t)width; prepared_display.height=(uint32_t)height;
    prepared_display.fullscreen=fullscreen?QA_DISPLAY_DESKTOP:QA_DISPLAY_WINDOWED;
    *display=prepared_display; *swap_interval=(int)swap; *gamma=(float)brightness;
    *output=prepared_output; *effects=prepared_effects; *music=prepared_music; *input=prepared_input;
    return true;
}
static bool same_settings(const qa_input_platform_settings *a,const qa_input_platform_settings *b)
{
    return a->mouse_available==b->mouse_available && a->no_grab==b->no_grab &&
        a->joystick_enabled==b->joystick_enabled && a->windows_joystick==b->windows_joystick &&
        a->midi_enabled==b->midi_enabled && a->joystick_seat==b->joystick_seat && a->midi_seat==b->midi_seat &&
        a->midi_device==b->midi_device && a->midi_channel==b->midi_channel &&
        a->joystick_threshold==b->joystick_threshold && a->joystick_ball_scale==b->joystick_ball_scale &&
        a->restart_requested==b->restart_requested;
}
static bool discard_input(frontend_shared_settings *owner,qa_error *error)
{
    if (!owner->input) return true;
    frontend_input_settings_view view;
    if (!frontend_input_settings_read(owner->input,&view,error)) return false;
    bool ok=true;
    if (!view.terminal) ok=frontend_input_settings_abort(owner->input,error);
    if (!frontend_input_settings_read(owner->input,&view,NULL) || !view.terminal) return false;
    frontend_input_settings *held=owner->input;
    bool destroyed=frontend_input_settings_destroy(&owner->input,error);
    if (!owner->input && owner->frontend->input_settings==held) owner->frontend->input_settings=NULL;
    return ok && destroyed;
}
bool frontend_shared_settings_advance(frontend_shared_settings *owner,bool validated,
    bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !owner || owner->aborting ||
        !frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate) ||
        !(owner->client?(qa_application_client_prepare_entered(owner->client,QA_CLIENT_PREPARE_RELEASE) &&
            !qa_application_client_prepare_cancel_entered(owner->client)):
            qa_application_startup_resource_phase(owner->application,owner->candidate)) ||
        (validated && !owner->before_complete) || (!validated && owner->after_started))
        return fail(error,"Shared release advancement lost its actual candidate phase");
    bool *finished=validated?&owner->after_complete:&owner->before_complete;
    if (*finished) { *complete=true; return true; }
    if (validated) owner->after_started=true;
    qa_frontend *f=owner->frontend;
    if (f->options.dedicated) { *finished=true; *complete=true; return true; }
    if (!owner->input) {
        if (f->input_settings || !project(owner,&owner->projected,error))
            return fail(error,"Candidate input release has an unrelated physical settings owner");
        owner->projected_window=false;
        if (validated) {
            qa_display_settings settings;
            if (!frontend_shared_video_settings(f,frontend_shared_values_prepared(owner->values),
                &settings,&owner->projected_window,error)) return false;
        }
        qa_input_seat *configuration[QA_INPUT_LOCAL_SEATS]={0};
        for (unsigned slot=0;slot<f->options.seats;++slot) {
            if (!(owner->client?frontend_config_store_client_input_configuration(owner->manager,
                owner->client,slot,configuration+slot,error):
                frontend_config_store_input_configuration(owner->manager,owner->application,
                    owner->candidate,slot,configuration+slot,error))) return false;
        }
        bool prepared=frontend_input_settings_prepare(f,&owner->projected,configuration,
            (double)f->wall_time_ns/1000000.0,&owner->input,error);
        if (owner->input) f->input_settings=owner->input;
        if (!prepared) return false;
    }
    if (!frontend_input_settings_reserve_all(owner->input,error)) return false;
    if (owner->projected_window && !frontend_input_settings_release_all_prepare(owner->input,
        (double)f->wall_time_ns/1000000.0,error)) return false;
    bool released=false;
    if (!frontend_input_settings_release_advance(owner->input,&released,error)) return false;
    if (!released) return true;
    /* Successful source commands are real effects. Checked unentered abort
     * consumes only completed rows and reverses the retained native candidate. */
    if (!discard_input(owner,error)) return false;
    qa_input_platform_settings final;
    if (!project(owner,&final,error)) return false;
    if (!same_settings(&owner->projected,&final)) return true;
    if (validated && !owner->projected_window) {
        qa_display_settings settings; bool changed;
        if (!frontend_shared_video_settings(f,frontend_shared_values_prepared(owner->values),
            &settings,&changed,error)) return false;
        if (changed) return true;
    }
    *finished=true; *complete=true; return true;
}
bool frontend_shared_settings_abort(frontend_shared_settings **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_settings *owner=*in;
    if (!frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate))
        return fail(error,"Shared abort lost its retained candidate parents");
    owner->aborting=true;
    if (owner->publication && !frontend_shared_publication_abort(&owner->publication,error)) return false;
    if (!discard_input(owner,error)) return false;
    if (!owner->scalar_aborted) {
        if (!frontend_shared_values_abort(owner->values,error)) return false;
        owner->scalar_aborted=true;
    }
    if (!frontend_shared_values_destroy(&owner->values,error)) return false;
    free(owner); *in=NULL; return true;
}
bool frontend_shared_settings_cancel_advance(frontend_shared_settings *owner,
    bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !owner ||
        !frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate))
        return fail(error,"Shared cancellation lost its returned candidate parents");
    if (!owner->input) { *complete=true; return true; }
    if (!(owner->client?(qa_application_client_prepare_entered(owner->client,QA_CLIENT_PREPARE_CLEANUP) ||
        qa_application_client_prepare_cancel_entered(owner->client)):
        qa_application_startup_release_cleanup_phase(owner->application,owner->candidate)))
        return fail(error,"Shared cancellation lacks its real returned release phase");
    owner->aborting=true;
    frontend_input_settings_view view;
    if (!frontend_input_settings_read(owner->input,&view,error)) return false;
    if (!view.terminal && !view.aborting) {
        bool released=false;
        if (!frontend_input_settings_release_advance(owner->input,&released,error)) return false;
        if (!released) return true;
    }
    if (!discard_input(owner,error)) return false;
    *complete=true; return true;
}
bool frontend_shared_settings_retirement_ready(const frontend_shared_settings *owner,
    const qa_frontend *f,const qa_application *app,const qa_launch_snapshot *candidate,
    const qa_cvars_edit *edit)
{
    return frontend_shared_settings_current(owner,f,app,candidate) && !f->preparing &&
        !owner->scalar_aborted && !owner->publication && !owner->consumed && owner->input &&
        frontend_shared_values_prepared(owner->values)==edit &&
        qa_cvars_edit_abort_is(edit,frontend_shared_values_registry(owner->values)) &&
        frontend_input_settings_failed_coverage_is(owner->input,f);
}
bool frontend_shared_settings_engine_shutdown(frontend_shared_settings **in,
    const qa_application_engine_shutdown *loan,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!in || !*in || !complete) return fail(error,"Shared cancellation lacks its retained slot and receipt");
    frontend_shared_settings *owner=*in;
    qa_frontend *f=owner->frontend;
    if (owner->publication || owner->consumed || f->application!=owner->application || f->config_store!=owner->manager ||
        f->stepping || f->preparing || f->capture || f->source_restoring ||
        !frontend_seat_callbacks_returned(f) ||
        qa_application_engine_shutdown_owner(loan)!=owner->application ||
        qa_application_engine_shutdown_candidate(loan)!=owner->candidate ||
        (owner->client && qa_application_engine_shutdown_client(loan)!=owner->client) ||
        (owner->input && (f->input_settings!=owner->input ||
            !frontend_input_settings_current(owner->input,f,error))))
        return fail(error,"Shared cancellation lost its physical ENGINE loan parents");
    owner->aborting=true;
    bool ok=true;
    if (owner->input) {
        bool terminal=false;
        ok=frontend_input_settings_engine_shutdown(owner->input,loan,&terminal,error);
        if (!terminal) return false;
        frontend_input_settings *held=owner->input;
        bool destroyed=frontend_input_settings_destroy(&owner->input,error);
        if (!owner->input && f->input_settings==held) f->input_settings=NULL;
        if (owner->input) return false;
        ok=destroyed && ok;
    }
    if (!frontend_shared_values_engine_shutdown(&owner->values,loan,error)) return false;
    free(owner); *in=NULL; *complete=true; return ok;
}
static bool client_retirement_ready(void *context,const qa_application_client_preparation *client,
    const qa_cvars_edit *edit)
{
    const frontend_shared_settings *owner=context;
    return owner && owner->client==client && owner->candidate==NULL &&
        qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_CLEANUP) &&
        frontend_shared_settings_current(owner,owner->frontend,owner->application,NULL) &&
        !owner->frontend->preparing && !owner->scalar_aborted && !owner->publication && !owner->consumed &&
        owner->input && frontend_shared_values_prepared(owner->values)==edit &&
        qa_cvars_edit_abort_is(edit,frontend_shared_values_registry(owner->values)) &&
        frontend_input_settings_failed_coverage_is(owner->input,owner->frontend);
}
bool frontend_shared_settings_client_shutdown(frontend_shared_settings **in,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    frontend_shared_settings *owner=in?*in:NULL;
    if (!owner || !complete || !owner->client || owner->publication || owner->consumed)
        return fail(error,"CLIENT cancellation lacks its actual failed settings parent");
    qa_application_engine_shutdown *loan=NULL;
    const qa_cvars_edit *values=frontend_shared_values_prepared(owner->values);
    if (!qa_application_engine_shutdown_begin_client(owner->application,owner->client,values,owner,
        client_retirement_ready,&loan,error)) return false;
    return frontend_shared_settings_engine_shutdown(in,loan,complete,error);
}
