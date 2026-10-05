#include "shared_publication.h"
#include "shared_settings_private.h"
#include "shared_resource_policy.h"
#include "shared_music_policy.h"
#include "music_sources.h"
#include "shared_audio.h"
#include "shared_acoustics.h"
#include "shared_ui.h"
#include "shared_video.h"
#include "shared_gamma.h"
#include "q3_color_policy.h"
#include "shared_render_controls.h"
#include "view_settings.h"
#include "capture.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"

struct frontend_shared_publication {
    frontend_shared_settings *parent;
    const qa_cvars_edit *edit;
    frontend_shared_resource_policy *resources;
    frontend_music_sources *music_sources;
    frontend_music_policy *policies[2];
    frontend_shared_music *music[2];
    frontend_shared_audio *audio;
    frontend_shared_acoustics *acoustics;
    qa_audio_engine *audio_engine;
    qa_audio_device *audio_device;
    frontend_shared_ui *ui;
    frontend_shared_video *video;
    frontend_shared_gamma *gamma;
    frontend_q3_color *color_owner;
    frontend_q3_color_ticket *color;
    frontend_shared_render_controls *render_controls;
    frontend_view_preparation *view;
    qa_application_language_ticket *language[QA_INPUT_LOCAL_SEATS];
    const qa_application_language_ticket *language_view[QA_INPUT_LOCAL_SEATS];
    qa_actor_id actors[QA_INPUT_LOCAL_SEATS];
    const qa_launch_snapshot *recipients;
    uint32_t logical_seats[QA_INPUT_LOCAL_SEATS];
    bool recipient_present[QA_INPUT_LOCAL_SEATS],actor_present[QA_INPUT_LOCAL_SEATS];
    size_t player_count;
    char *language_name[QA_INPUT_LOCAL_SEATS];
    size_t language_count;
    qa_error cleanup_error;
    bool prepared,published,scalar_finished,engine_only;
};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_ARGUMENT,text); }
static bool preparing(const frontend_shared_settings *owner)
{
    return owner->client?qa_application_client_prepare_entered(owner->client,QA_CLIENT_PREPARE_RESOURCES):
        qa_application_startup_resource_phase(owner->application,owner->candidate);
}
static bool associated(const frontend_shared_settings *owner)
{
    return owner->client?(qa_application_client_prepare_associated(owner->application,owner->client) &&
        qa_application_client_prepare_phase_is(owner->client,QA_CLIENT_PREPARE_RESOURCES)):
        qa_application_startup_resource_phase_associated(owner->application,owner->candidate);
}
static bool consuming(const frontend_shared_settings *owner)
{
    return owner->client?qa_application_client_prepare_entered(owner->client,QA_CLIENT_PREPARE_CONSUMING):
        qa_application_startup_publication_consuming(owner->application,owner->candidate);
}
static bool cleaning(const frontend_shared_settings *owner)
{
    return owner->client?qa_application_client_prepare_entered(owner->client,QA_CLIENT_PREPARE_CLEANUP):
        qa_application_startup_publication_cleanup(owner->application,owner->candidate);
}
static bool current(const frontend_shared_publication *ticket)
{
    frontend_shared_settings *owner=ticket?ticket->parent:NULL;
    if (!owner || owner->publication!=ticket || !owner->after_complete || owner->scalar_aborted)
        return false;
    if (!ticket->published)
        return frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate);
    qa_frontend *f=owner->frontend;
    return owner->consumed && f && f->application==owner->application &&
        f->config_store==owner->manager && !f->stepping && !f->capture && !f->source_restoring &&
        frontend_seat_callbacks_returned(f) &&
        frontend_shared_values_registry(owner->values)==qa_application_cvars(owner->application) &&
        (!owner->input || (f->input_settings==owner->input && frontend_input_settings_current(owner->input,f,NULL))) &&
        (consuming(owner) || cleaning(owner));
}
static void remember(frontend_shared_publication *ticket,const qa_error *error)
{
    if (error && error->code!=QA_OK && ticket->cleanup_error.code==QA_OK)
        ticket->cleanup_error=*error;
}
static bool input_dispose(frontend_shared_publication *ticket,bool published,qa_error *e)
{
    frontend_shared_settings *owner=ticket->parent;
    if (!owner->input) return true;
    frontend_input_settings_view view;
    if (!frontend_input_settings_read(owner->input,&view,e)) return false;
    qa_error fault={0};
    if (!view.terminal && !published) {
        bool ok=frontend_input_settings_abort_empty(owner->input,&fault);
        remember(ticket,&fault);
        if (!frontend_input_settings_read(owner->input,&view,e) || !view.terminal) {
            if (!ok && e) *e=fault;
            return false;
        }
    }
    if (!view.terminal) return fail(e,"Publication input cleanup has no actual terminal receipt");
    frontend_input_settings *held=owner->input;
    bool ok=frontend_input_settings_destroy(&owner->input,&fault);
    remember(ticket,&fault);
    if (!owner->input && owner->frontend->input_settings==held) owner->frontend->input_settings=NULL;
    if (owner->input) { if (!ok && e) *e=fault; return false; }
    return true;
}
static void release(frontend_shared_publication *ticket)
{
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) free(ticket->language_name[i]);
    qa_launch_snapshot_release(ticket->recipients);
    ticket->parent->publication=NULL;
    free(ticket);
}
static bool observed(frontend_shared_publication *ticket,const qa_display_info *info,
    int swap,qa_error *e)
{
    return ticket && ticket->edit==frontend_shared_values_prepared(ticket->parent->values) &&
        frontend_shared_values_window_observed(ticket->parent->values,info,swap,
            ticket->parent->frontend->gl!=NULL,e);
}
bool frontend_shared_publication_prepare(frontend_shared_settings *owner,
    frontend_shared_publication **out,qa_error *e)
{
    if (!owner || !out || *out || owner->publication || owner->aborting || owner->consumed ||
        owner->input || !owner->after_complete ||
        !frontend_shared_settings_current(owner,owner->frontend,owner->application,owner->candidate) ||
        !preparing(owner))
        return fail(e,"Final publication requires its actual validated returned scalar and release owner");
    const qa_cvars_edit *edit=frontend_shared_values_prepared(owner->values);
    if (!qa_cvars_edit_returned_is(edit,qa_application_cvars(owner->application)))
        return fail(e,"Final publication lost its actual canonical preparation");
    frontend_shared_publication *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining complete shared resource publication");
    ticket->parent=owner; ticket->edit=edit; owner->publication=ticket; *out=ticket;
    qa_frontend *f=owner->frontend;
    if (!owner->candidate && !owner->client) {
        qa_console *console=NULL; qa_cvars *registry=NULL;
        if (!qa_application_startup_root_read(owner->application,NULL,&console,&registry,NULL,e) ||
            console!=owner->root_console || registry!=qa_cvars_edit_registry(edit) ||
            qa_application_launch(owner->application) || qa_application_player_count(owner->application))
            return fail(e,"Source-free publication lost its actual empty player roster and ENGINE root");
        ticket->engine_only=true;
    }
    if (f->options.dedicated) {
        if (!frontend_shared_render_controls_prepare(f,edit,&ticket->render_controls,e)) return false;
        ticket->prepared=true; return true;
    }
    ticket->music_sources=f->music_sources;
    size_t queued=0;
    if (!frontend_music_sources_parent_is(ticket->music_sources,f,f->audio) ||
        !frontend_music_sources_queued(ticket->music_sources,&queued) || queued)
        return fail(e,"Final publication requires returned music requests before holding resource children");
    frontend_view_transition transition;
    if (owner->client) {
        bool published=false;
        if (!frontend_view_settings_has_published(f->view_settings,&published))
            return fail(e,"CLIENT view preparation lost its actual published preference history");
        transition=published?FRONTEND_VIEW_REPLACEMENT:FRONTEND_VIEW_INITIAL;
        if (!frontend_view_settings_prepare_client(f->view_settings,owner->client,edit,transition,&ticket->view,e) ||
            !frontend_shared_resource_policy_begin_client(f,owner->client,edit,&ticket->resources,e)) return false;
    } else if (!frontend_config_store_view_transition(owner->manager,owner->application,
        owner->candidate,&transition,e) || !frontend_view_settings_prepare(f->view_settings,
        owner->candidate,edit,transition,&ticket->view,e) ||
        !frontend_shared_resource_policy_begin(f,owner->candidate,edit,&ticket->resources,e)) return false;
    ticket->audio_engine=f->audio; ticket->audio_device=f->device;
    if (!ticket->audio_engine && ticket->audio_device)
        return fail(e,"Final publication has an output device without its actual audio engine");
    for (unsigned i=0;i<2;++i) {
        ticket->policies[i]=frontend_music_sources_policy(ticket->music_sources,(frontend_music_slot)i);
        if (ticket->policies[i] && !(owner->client?
            frontend_shared_music_prepare_client(f,owner->client,edit,ticket->policies[i],ticket->music+i,e):
            frontend_shared_music_prepare(f,owner->candidate,edit,ticket->policies[i],ticket->music+i,e))) return false;
    }
    qa_display_settings settings; bool window=false;
    if (!frontend_shared_video_settings(f,edit,&settings,&window,e)) return false;
    qa_input_seat *configuration[QA_INPUT_LOCAL_SEATS]={0};
    qa_controller_selection selections[QA_INPUT_LOCAL_SEATS]={0};
    bool dictionaries_changed=false;
    for (unsigned i=0;i<f->options.seats;++i) {
        if (!(owner->client?frontend_config_store_client_input_configuration(owner->manager,
            owner->client,i,configuration+i,e):frontend_config_store_input_configuration(owner->manager,
            owner->application,owner->candidate,i,configuration+i,e))) return false;
        if (configuration[i]!=f->seats[i].input) dictionaries_changed=true;
        if (owner->client && !frontend_config_store_client_controller_selection(owner->manager,
            owner->client,i,selections+i,e)) return false;
    }
    bool prepared=owner->client?frontend_input_settings_prepare_selected(f,&owner->projected,configuration,
        selections,(double)f->wall_time_ns/1000000.0,&owner->input,e):
        frontend_input_settings_prepare(f,&owner->projected,configuration,
            (double)f->wall_time_ns/1000000.0,&owner->input,e);
    if (owner->input) f->input_settings=owner->input;
    if (!prepared || ((window || dictionaries_changed) && !frontend_input_settings_release_all_prepare(owner->input,
        (double)f->wall_time_ns/1000000.0,e))) return false;
    if (!frontend_input_settings_unentered_empty(owner->input,e))
        return fail(e,"Final resources cannot dispatch a newly captured physical release programme");
    bool complete=false;
    if (!frontend_input_settings_release_advance(owner->input,&complete,e) || !complete ||
        !frontend_input_settings_enter(owner->input,e)) return false;
    const qa_cvar_view *gamma_row=qa_cvars_edit_find(edit,"r_gamma");
    if (!gamma_row || !gamma_row->value || !isfinite(gamma_row->number) ||
        gamma_row->number<.5 || gamma_row->number>3) return fail(e,"Final brightness lacks its actual canonical scalar");
    double gamma=gamma_row->number;
    qa_display_info info; int swap=0;
    if (window) {
        if (!frontend_shared_video_prepare(f,&settings,(float)gamma,owner->input,&ticket->video,e) ||
            !frontend_shared_video_configuration(ticket->video,&info,e)) return false;
        swap=settings.swap_interval;
    } else {
        if (!frontend_shared_gamma_prepare(f,(float)gamma,&ticket->gamma,e) ||
            !qa_display_info_get(f->display,&info,e) ||
            (f->gl && !qa_display_swap_interval(f->display,&swap,e))) return false;
    }
    ticket->color_owner=f->source_color;
    if (ticket->color_owner) {
        qa_display *target=ticket->video?frontend_shared_video_candidate(ticket->video):f->display;
        if (!target || !frontend_q3_source_color_prepare(f,edit,target,&ticket->color,e)) return false;
        if (!(ticket->video?frontend_shared_video_refresh(ticket->video,e):
            frontend_shared_gamma_refresh(ticket->gamma,e))) return false;
    }
    if (!frontend_shared_resource_policy_prepare_children(ticket->resources,e) ||
        !observed(ticket,&info,swap,e) ||
        !frontend_shared_render_controls_prepare(f,edit,&ticket->render_controls,e) ||
        !frontend_shared_ui_prepare(f,edit,&ticket->ui,e)) return false;
    for (unsigned i=0;!ticket->engine_only && i<f->options.seats;++i) {
        if (owner->client) {
            const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_client_prepare_launch(owner->client));
            ticket->recipient_present[i]=choices && i<choices->seat_count;
            if (ticket->recipient_present[i]) ticket->logical_seats[i]=choices->seats[i].id;
        } else ticket->recipient_present[i]=frontend_seat_launch_id_read(f,i,ticket->logical_seats+i);
        qa_actor_id actor={0};
        ticket->actor_present[i]=ticket->recipient_present[i] &&
            qa_application_player_actor(owner->application,ticket->logical_seats[i],&actor);
        if (!ticket->actor_present[i]) continue;
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_edit_read(edit,i,&preferences,e)) return false;
        char *name=malloc(strlen(preferences.language)+1);
        if (!name) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual source language recipient");
        strcpy(name,preferences.language); ticket->language_name[i]=name; ticket->actors[i]=actor;
        const qa_launch_instance *prior=qa_application_selected_instance(owner->application,
            qa_application_launch(owner->application),actor,QA_ROLE_CHARACTER,"");
        const qa_launch_instance *selected=qa_application_selected_instance(owner->application,
            owner->candidate,actor,QA_ROLE_CHARACTER,"");
        if (owner->candidate && (!prior || !selected || prior->state!=selected->state ||
            prior->storage!=selected->storage)) continue;
        if (!qa_application_language_prepare(owner->application,actor,name,ticket->language+i,e)) return false;
        if (ticket->language[i]) ticket->language_view[ticket->language_count++]=ticket->language[i];
    }
    ticket->recipients=qa_application_launch(owner->application);
    qa_launch_snapshot_retain(ticket->recipients);
    ticket->player_count=qa_application_player_count(owner->application);
    if (!frontend_shared_audio_prepare(f,edit,&ticket->audio,e) ||
        !frontend_shared_acoustics_prepare(f,edit,ticket->audio,&ticket->acoustics,e)) return false;
    ticket->prepared=true; return true;
}
bool frontend_shared_publication_languages(const frontend_shared_settings *owner,
    const qa_application_language_ticket *const **out,size_t *count)
{
    if (!out || !count || !owner || !frontend_shared_settings_current(owner,
        owner->frontend,owner->application,owner->candidate)) return false;
    const frontend_shared_publication *ticket=owner->publication;
    *out=ticket?ticket->language_view:NULL; *count=ticket?ticket->language_count:0;
    return true;
}
bool frontend_shared_publication_ready_is(const frontend_shared_publication *ticket,
    const frontend_shared_settings *owner,const qa_frontend *f,const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    if (!current(ticket) || ticket->parent!=owner || owner->frontend!=f || owner->application!=app ||
        owner->candidate!=candidate || !ticket->prepared || ticket->published || owner->aborting ||
        !(associated(owner) || consuming(owner)) ||
        !frontend_shared_values_ready_is(owner->values)) return false;
    if (f->options.dedicated) return !f->cpu && !f->gl && !ticket->render_controls &&
        !f->audio && !f->device && !ticket->resources && !owner->input && !ticket->ui &&
        !ticket->audio && !ticket->acoustics && !ticket->view && !ticket->video && !ticket->gamma &&
        !ticket->color && !ticket->color_owner &&
        !ticket->music_sources && !ticket->music[0] && !ticket->music[1] && !ticket->language_count;
    bool resources=consuming(owner)?
        frontend_shared_resource_policy_consume_ready_is(ticket->resources):
        frontend_shared_resource_policy_ready_is(ticket->resources);
    if (!resources ||
        !frontend_view_settings_ready_is(ticket->view) ||
        !frontend_input_settings_ready_is(owner->input) ||
        !(ticket->video?frontend_shared_video_ready_is(ticket->video):frontend_shared_gamma_ready_is(ticket->gamma)) ||
        !frontend_shared_ui_ready_is(ticket->ui) || f->audio!=ticket->audio_engine ||
        f->source_color!=ticket->color_owner ||
        (ticket->color_owner && !frontend_q3_source_color_ready_is(ticket->color)) ||
        f->device!=ticket->audio_device ||
        (ticket->audio_engine?(!frontend_shared_audio_ready_is(ticket->audio) ||
            !frontend_shared_acoustics_ready_is(ticket->acoustics)):
            (ticket->audio || ticket->acoustics || ticket->audio_device || ticket->music[0] || ticket->music[1])) ||
        !frontend_shared_render_controls_ready_is(ticket->render_controls) ||
        f->music_sources!=ticket->music_sources || !frontend_music_sources_parent_is(ticket->music_sources,f,f->audio)) return false;
    size_t queued=0;
    if (!frontend_music_sources_queued(ticket->music_sources,&queued) || queued) return false;
    for (unsigned i=0;i<2;++i)
        if (frontend_music_sources_policy(ticket->music_sources,(frontend_music_slot)i)!=ticket->policies[i] ||
            (ticket->policies[i] && !frontend_shared_music_ready_is(ticket->music[i]))) return false;
    if (qa_application_player_count(app)!=ticket->player_count ||
        (associated(owner) &&
            qa_application_launch(app)!=ticket->recipients)) return false;
    for (unsigned i=0;i<f->options.seats;++i) {
        if (ticket->engine_only) {
            if (candidate || ticket->recipients || ticket->player_count ||
                ticket->recipient_present[i] || ticket->actor_present[i] || ticket->language[i]) return false;
            continue;
        }
        const qa_launch_choices *choices=qa_launch_snapshot_choices(ticket->recipients);
        bool present=choices && i<choices->seat_count;
        if (present!=ticket->recipient_present[i] ||
            (present && choices->seats[i].id!=ticket->logical_seats[i])) return false;
        qa_actor_id actor={0};
        bool admitted=present && qa_application_player_actor(app,ticket->logical_seats[i],&actor);
        if (admitted!=ticket->actor_present[i] || (admitted && !qa_actor_id_equal(actor,ticket->actors[i])) ||
            (ticket->language[i] && !qa_application_language_ready_is(ticket->language[i],app,
                ticket->actors[i],ticket->language_name[i]))) return false;
    }
    return true;
}
bool frontend_shared_publication_ready(frontend_shared_publication *ticket,qa_error *e)
{
    if (!current(ticket) || !ticket->prepared || ticket->published || ticket->parent->aborting)
        return fail(e,"Final resources lost their retained preparation");
    frontend_shared_settings *owner=ticket->parent; qa_frontend *f=owner->frontend;
    if (!frontend_shared_values_ready(owner->values,e)) return false;
    if (!f->options.dedicated) {
        if (!frontend_shared_resource_policy_ready(ticket->resources,e) ||
            !frontend_shared_ui_ready(ticket->ui,e) ||
            (ticket->audio_engine && (!frontend_shared_audio_ready(ticket->audio,e) ||
                !frontend_shared_acoustics_ready(ticket->acoustics,e))) ||
            !frontend_shared_render_controls_ready(ticket->render_controls,e)) return false;
        for (unsigned i=0;i<2;++i)
            if (ticket->music[i] && !frontend_shared_music_ready(ticket->music[i],ticket->audio,e)) return false;
        if (!frontend_input_settings_ready(owner->input,e) ||
            !(ticket->video?frontend_shared_video_ready(ticket->video,e):frontend_shared_gamma_ready(ticket->gamma,e))) return false;
        if (ticket->color && !frontend_q3_source_color_ready(ticket->color,e)) return false;
    }
    return frontend_shared_publication_ready_is(ticket,owner,f,owner->application,owner->candidate) ||
        fail(e,"Final resource publication lost an actual sealed child receipt");
}
void frontend_shared_publication_consume(frontend_shared_publication *ticket)
{
    frontend_shared_settings *owner=ticket->parent;
    if (!frontend_shared_publication_ready_is(ticket,owner,owner->frontend,owner->application,owner->candidate)) return;
    if (!owner->frontend->options.dedicated) {
        frontend_shared_render_controls_consume(&ticket->render_controls);
        frontend_shared_resource_policy_consume(ticket->resources);
        frontend_shared_ui_consume(&ticket->ui);
        for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) if (ticket->language[i]) {
            qa_application_language_commit(ticket->language[i]); ticket->language[i]=NULL;
        }
        ticket->language_count=0;
        for (unsigned i=0;i<2;++i) if (ticket->music[i]) frontend_shared_music_publish(ticket->music+i);
        if (ticket->acoustics) frontend_shared_acoustics_publish(&ticket->acoustics);
        if (ticket->audio) frontend_shared_audio_publish(&ticket->audio);
        frontend_input_settings_publish(owner->input);
        if (ticket->video) frontend_shared_video_publish(ticket->video);
        else frontend_shared_gamma_publish(ticket->gamma);
        frontend_shared_resource_policy_render_publish(ticket->resources);
        if (ticket->color) frontend_q3_source_color_publish(ticket->color);
        frontend_view_settings_publish(ticket->view);
    }
    frontend_shared_values_publish(owner->values);
    owner->consumed=true; ticket->published=true;
}
bool frontend_shared_settings_consumed_is(const frontend_shared_settings *owner,
    const qa_frontend *f,const qa_application *app,const qa_launch_snapshot *candidate)
{
    return owner && owner->consumed && owner->publication && owner->publication->published &&
        owner->frontend==f && owner->application==app && owner->candidate==candidate &&
        current(owner->publication);
}
bool frontend_shared_publication_abort(frontend_shared_publication **in,qa_error *e)
{
    if (!in || !*in) return true;
    frontend_shared_publication *ticket=*in;
    if (!current(ticket) || ticket->published) return fail(e,"Resource abort requires its actual unpublished parent");
    if (!frontend_shared_render_controls_abort(&ticket->render_controls,e)) return false;
    if (ticket->ui && !frontend_shared_ui_abort(&ticket->ui,e)) return false;
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) if (ticket->language[i]) {
        qa_application_language_abort(ticket->language[i]); ticket->language[i]=NULL;
    }
    ticket->language_count=0;
    for (unsigned i=0;i<2;++i) if (!frontend_shared_music_abort(ticket->music+i,e)) return false;
    if (!frontend_shared_acoustics_abort(&ticket->acoustics,e) || !frontend_shared_audio_abort(&ticket->audio,e)) return false;
    if (!frontend_q3_source_color_abort(&ticket->color,e)) return false;
    if (ticket->video && !frontend_shared_video_rollback(ticket->video,e)) return false;
    if (!input_dispose(ticket,false,e)) return false;
    if (!frontend_shared_video_abort(&ticket->video,e) || !frontend_shared_gamma_abort(&ticket->gamma,e) ||
        !frontend_shared_resource_policy_abort(&ticket->resources,e) ||
        !frontend_view_settings_abort(&ticket->view,e)) return false;
    qa_error fault=ticket->cleanup_error; release(ticket); *in=NULL;
    if (fault.code!=QA_OK) { if (e) *e=fault; return false; }
    return true;
}
bool frontend_shared_publication_finish(frontend_shared_settings **parent,
    frontend_shared_publication **in,bool *complete,qa_error *e)
{
    if (complete) *complete=false;
    if (!parent || !*parent || !in || !*in || !complete)
        return fail(e,"Resource finish requires both actual retained owner slots");
    frontend_shared_publication *ticket=*in; frontend_shared_settings *owner=*parent;
    if (ticket->parent!=owner || !current(ticket) || !ticket->published || !owner->consumed)
        return fail(e,"Resource finish lost its real entered publication receipt");
    if (!frontend_shared_render_controls_finish(&ticket->render_controls,e) ||
        !input_dispose(ticket,true,e) || !frontend_q3_source_color_finish(&ticket->color,e) ||
        !frontend_shared_video_finish(&ticket->video,e) ||
        !frontend_shared_gamma_finish(&ticket->gamma,e) ||
        !frontend_shared_resource_policy_finish(&ticket->resources,e)) return false;
    if (ticket->view && !frontend_view_settings_apply(ticket->view,e)) {
        remember(ticket,e); return false;
    }
    if (!ticket->scalar_finished) {
        if (!frontend_shared_values_finish(owner->values,e)) return false;
        ticket->scalar_finished=true;
    }
    if (!frontend_view_settings_finish(&ticket->view,e)) return false;
    if (!frontend_shared_values_destroy(&owner->values,e)) return false;
    qa_error fault=ticket->cleanup_error; release(ticket); *in=NULL;
    free(owner); *parent=NULL; *complete=true;
    if (fault.code!=QA_OK) { if (e) *e=fault; return false; }
    return true;
}
