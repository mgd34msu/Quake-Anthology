#include "persistence.h"
#include "internal.h"
#include "constructor.h"
#include "capture.h"
#include "original_frontend.h"
#include "rankings.h"
#include "native_q2_baseline.h"
#include "native_q2_save.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "native_resource_inventory.h"
#include "network_restore.h"
#include "network_declarations.h"
#include "tools_restore.h"
#include "config_store.h"
#include "keys.h"
#include "global_settings_storage.h"
#include "shared_register.h"
#include "save_commands.h"
#include "equipment_events.h"
#include "equipment_gear.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "selected_character.h"
#include "selected_effects.h"
#include "qc_rerelease_events.h"
#include "qc_messages.h"
#include "q1_sky.h"
#include "q3_color_policy.h"
#include "source_prompt.h"
#include "source_restore.h"
#include "client_source.h"
#include "view_bindings.h"
#include "view_settings.h"
#include "music_sources.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "root_resources.h"
#include "shared_resource_policy.h"
#include "source_renderer_runtime.h"
#include "qa/audio_acoustics_prepare.h"
#include "qa/application_profile.h"
#include "qa/http_save.h"
#include <SDL.h>

typedef struct frontend_persistence {
    qa_frontend *active,*candidate,*constructor;
    qa_frontend **slot;
    const qa_application_persistence_ops *services;
    qa_application_persistence_ops ops;
    qa_application_ranking_checkpoint_refs ranking;
    qa_vfs_checkpoint_refs content_files;
    frontend_native_resource_context native_resources;
    qa_application_native_resource_refs native_resource_refs;
    frontend_persistence_native native;
    frontend_q3_color_ticket *color_ticket;
    bool finished;
} frontend_persistence;

static bool content_visit(void *context,const qa_application *application,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    frontend_persistence *operation=context;
    return !operation->services || !operation->services->visit_content ||
        operation->services->visit_content(operation->services->context,application,visitor,error);
}
static bool prepare_services(void *context,qa_application *candidate,const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    qa_frontend *source=operation->constructor?operation->constructor:operation->active;
    if (!f || f->application || !candidate)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend rebuild needs its fresh application owner");
    f->application=candidate;
    f->audio_output_format=source->device?qa_audio_device_requested_configuration(source->device).format:source->audio_output_format;
    bool ok=(!source->cpu || qa_cpu_gamma_read(source->cpu,&f->options.gamma,error)) &&
        (!source->gl || qa_gl_gamma_read(source->gl,&f->options.gamma,error)) &&
        frontend_shared_register(qa_application_cvars(candidate),NULL,f->audio_output_format,f->options.gamma,error) &&
        frontend_network_declarations(qa_application_cvars(candidate),error) &&
        (f->options.dedicated || frontend_q1_sky_create(f,&f->q1_sky,error)) &&
        frontend_qc_messages_create(f,&f->qc_messages,error) && frontend_view_bindings_create(f,error) &&
        frontend_equipment_events_create(f,&f->gear_events,error) && frontend_commands(f,error) &&
        frontend_outputs_create_detached(f,operation->active,&operation->native,error);
    return ok && (!operation->services || !operation->services->prepare_services ||
        operation->services->prepare_services(operation->services->context,candidate,image,error));
}
static bool prepare_content(void *context,qa_application *candidate,const qa_launch_snapshot *snapshot,
    const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context;
    return !operation->services || !operation->services->prepare_content ||
        operation->services->prepare_content(operation->services->context,candidate,snapshot,image,error);
}
static bool reconnect(void *context,qa_application *candidate,const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context;
    return !operation->services || !operation->services->reconnect ||
        operation->services->reconnect(operation->services->context,candidate,image,error);
}
static bool complete_state(void *context,qa_application *candidate,const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    qa_frontend *source=operation->constructor?operation->constructor:operation->active;
    if (operation->services && operation->services->complete_state &&
        !operation->services->complete_state(operation->services->context,candidate,image,error)) return false;
    const qa_save_metadata *metadata=qa_save_image_metadata(image);
    if (metadata && metadata->purpose==QA_SAVE_TRANSITION &&
        !qa_application_campaign_reenter(candidate,operation->active->application,error)) return false;
    return frontend_config_store_rebuild_finish(f->config_store,error) && frontend_scene_sync(f,error) &&
        frontend_tools_sync(f,error) && frontend_network_create_detached(f,source,error);
}
static bool validate(void *context,qa_application *application,const qa_save_image *image,qa_error *error)
{
    frontend_persistence *operation=context;
    if (operation->candidate && operation->candidate->application!=application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend rebuild belongs to another application");
    bool ok=!operation->services || !operation->services->validate ||
        operation->services->validate(operation->services->context,application,image,error);
    if (ok && operation->candidate) operation->finished=true;
    return ok;
}
static bool ranking_capture(void *context,qa_application_ranking_effect_fn installed,
    void *binding,qa_buffer *out,qa_error *error)
{
    frontend_persistence *operation=context;
    qa_frontend *f=binding==operation->active?operation->active:
        binding==operation->candidate?operation->candidate:NULL;
    qa_application_ranking_checkpoint_refs refs=frontend_ranking_refs(f);
    return refs.capture(refs.context,installed,binding,out,error);
}
static bool commands_restored(void *context,qa_application *application,qa_console *console,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (!f || f->application!=application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Command custody has no actual isolated frontend owner");
    return frontend_config_store_commands_restored(f->config_store,application,console,error);
}
static bool ranking_resolve(void *context,qa_bytes bytes,qa_application_ranking_effect_fn *installed,
    void **binding,qa_error *error)
{
    frontend_persistence *operation=context;
    qa_application_ranking_checkpoint_refs refs=frontend_ranking_refs(operation->candidate);
    return refs.resolve(refs.context,bytes,installed,binding,error);
}
static bool ranking_handoff(void *context,qa_rankings *active,qa_rankings *candidate,
    bool *relinquish,qa_error *error)
{
    frontend_persistence *operation=context;
    return operation->services->rankings_handoff(operation->services->context,active,candidate,relinquish,error);
}
static bool native_baseline(void *context,qa_application *candidate,qa_actor_owner owner,
    qa_application_native_baseline_services *services,qa_error *error)
{
    frontend_persistence *operation=context;
    if (operation->services && operation->services->prepare_native_baseline)
        return operation->services->prepare_native_baseline(operation->services->context,candidate,owner,services,error);
    return frontend_native_q2_baseline_prepare(operation->candidate,candidate,owner,services,error);
}
static bool discard_services(void *context,qa_application *candidate,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    if (!f || (f->application && f->application!=candidate))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed application belongs to another frontend graph");
    if (!f->application) f->application=candidate;
    if (!qa_input_platform_handoff_abort(operation->native.input,error) ||
        !frontend_q3_source_color_abort(&operation->color_ticket,error) ||
        !qa_display_handoff_abort(operation->native.display,error)) return false;
    if (f->input && !qa_input_platform_settings_idle(f->input))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains its native input settings preparation");
    if (!frontend_seat_callbacks_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains actual seat callbacks or source release history");
    if (!frontend_ui_features_idle(f) || !frontend_remote_q3_idle(f) || !frontend_qc_messages_idle(f->qc_messages))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains UI preparation or remote CLIENT children");
    if (!frontend_equipment_events_idle(f->gear_events) || !frontend_equipment_gear_idle(f) ||
        !frontend_selected_effects_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Failed candidate retains gear delivery or selected effect callbacks");
    if (!frontend_shared_resource_policy_live_destroy(f,error)) return false;
    if (!frontend_network_close_client(f,error) || !frontend_cinematic_destroy(f,error) ||
        !frontend_selected_effects_retire(f,error) || !frontend_remote_q3_destroy(f,error) ||
        !frontend_remote_q3_initial_destroy_all(f,error) ||
        !frontend_native_q3_destroy(f,error) ||
        !frontend_equipment_events_destroy(f->gear_events,error)) return false;
    f->gear_events=NULL;
    if (!frontend_equipment_gear_retire(f,error)) return false;
    frontend_equipment_gear_destroy(f);
    if (operation->services && operation->services->discard_services &&
        !operation->services->discard_services(operation->services->context,candidate,error)) return false;
    if (!frontend_qc_rerelease_idle(f) || !frontend_equipment_retire(f,error) ||
        !frontend_equipment_q3_retire(f,error) ||
        !frontend_selected_character_retire(f,error) ||
        !frontend_save_commands_destroy(f,error) || !frontend_campaign_destroy(f,error)) return false;
    frontend_equipment_destroy(f);
    frontend_equipment_q3_destroy(f);
    frontend_qc_rerelease_destroy(f);
    for (unsigned i=0;i<f->options.seats;++i)
        if (f->seats[i].input && !qa_input_seat_release(f->seats[i].input,
            (double)f->wall_time_ns/1000000.0,error)) return false;
    /* The application is destroyed after this callback returns. Retire its
     * prompt callbacks while the physical UI and input parents still exist. */
    for (unsigned i=0;i<f->options.seats;++i)
        if (!frontend_source_prompt_destroy(&f->seats[i].source_prompt,error)) return false;
    qa_input_platform_destroy(f->input); f->input=NULL;
    qa_input_console_destroy(f->input_commands); f->input_commands=NULL;
    for (unsigned i=0;i<f->options.seats;++i) {
        if (!qa_ui_llm_destroy(f->seats[i].assistance,(double)f->time_ns/1000000.0,error)) return false;
        f->seats[i].assistance=NULL;
    }
    return frontend_qc_messages_destroy(&f->qc_messages,error) && frontend_q1_sky_destroy(&f->q1_sky,error) &&
        frontend_music_sources_destroy(&f->music_sources,error) &&
        frontend_view_settings_destroy(&f->view_settings,error) &&
        frontend_tools_before_world_change(f,error) &&
        (!f->audio || qa_audio_engine_acoustics_release(f->audio,error)) &&
        frontend_client_sources_destroy(f,error) &&
        qa_application_retire_sources(candidate,error) &&
        frontend_config_store_restore_abort_unbound(f->config_store,candidate,error) &&
        frontend_root_resources_destroy(f,error) &&
        frontend_q3_source_color_retire(f,error) &&
        frontend_network_destroy(f,error) && frontend_tools_destroy(f,error);
}
static bool publish_ready(void *context,qa_application *active,qa_application *candidate,qa_error *error)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    qa_frontend *source=operation->constructor?operation->constructor:operation->active;
    if (!operation->finished || !operation->slot || *operation->slot!=operation->active ||
        operation->active->application!=active || !f || f->application!=candidate ||
        !frontend_owners_idle(operation->active) || !frontend_owners_idle(f) ||
        !frontend_seat_callbacks_returned(operation->active) || !frontend_seat_callbacks_returned(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend publication requires returned active and candidate callbacks");
    if (!frontend_source_rebind_ready(f,operation->active,error) ||
        !frontend_native_q2_rebind_ready(f,operation->active,error) ||
        !frontend_native_q3_rebind_ready(f,operation->active,error) ||
        !frontend_tools_rebind_ready(f,operation->active,error) ||
        !frontend_network_rebuild_ready(f,source,error) ||
        !qa_http_handoff_ready(frontend_tools_http(source),frontend_tools_http(f),error)) return false;
    if (operation->services && operation->services->publish_ready &&
        !operation->services->publish_ready(operation->services->context,active,candidate,error)) return false;
    if ((operation->native.input && !qa_input_platform_handoff_prepare(operation->native.input,error)) ||
        (operation->native.display && !qa_display_handoff_prepare(operation->native.display,error)) ||
        (operation->native.gl && !qa_gl_handoff_prepare(operation->native.gl,error)) ||
        (operation->native.device && !qa_audio_device_handoff_prepare(operation->native.device,error))) return false;
    if (f->source_color && !frontend_q3_source_color_restore_prepare(f,f->display,&operation->color_ticket,error)) return false;
    return (!operation->native.input || qa_input_platform_handoff_ready(operation->native.input,error)) &&
        (!operation->native.display || qa_display_handoff_ready(operation->native.display,error)) &&
        (!operation->native.gl || qa_gl_handoff_ready(operation->native.gl,error)) &&
        (!operation->native.device || qa_audio_device_handoff_ready(operation->native.device,error)) &&
        (!operation->color_ticket || frontend_q3_source_color_ready_is(operation->color_ticket));
}
static void publish(void *context,qa_application *active,qa_application *candidate)
{
    frontend_persistence *operation=context; qa_frontend *f=operation->candidate;
    qa_frontend *source=operation->constructor?operation->constructor:operation->active;
    if (operation->services && operation->services->publish)
        operation->services->publish(operation->services->context,active,candidate);
    frontend_network_transport_exchange(source,f);
    qa_http_handoff_publish(frontend_tools_http(source),frontend_tools_http(f));
    if (operation->native.input) qa_input_platform_handoff(operation->native.input);
    if (operation->native.display) qa_display_handoff(operation->native.display);
    if (operation->color_ticket) frontend_q3_source_color_publish(operation->color_ticket);
    if (operation->native.gl) qa_gl_handoff(operation->native.gl);
    if (operation->native.device) qa_audio_device_handoff(operation->native.device);
    frontend_equipment_events_rebind(f->gear_events,f);
    frontend_view_bindings_restore_published(f);
    operation->active->archive_enabled=false;
    f->archive_enabled=true; f->archive_saved=false;
    f->sdl_subsystems=operation->active->sdl_subsystems; operation->active->sdl_subsystems=0;
    *operation->slot=f;
}
static bool content_file_open(void *context,const char *path,qa_fs_file **out,
    qa_fs_identity *identity,qa_error *error)
{
    frontend_persistence *operation=context;
    const qa_vfs_checkpoint_refs *refs=operation->services?operation->services->content_files:NULL;
    return refs && refs->file_open?refs->file_open(refs->context,path,out,identity,error):
        qa_fs_file_open(path,out,identity,error);
}
static bool content_directory_open(void *context,const char *mount_path,const char *retained_path,
    const qa_fs_identity *identity,qa_fs_root **out,qa_error *error)
{
    frontend_persistence *operation=context;
    const qa_frontend *constructor=operation->constructor?operation->constructor:operation->active;
    qa_fs_root *profile=qa_application_player_profile_root(constructor->application);
    if (!out || *out || !retained_path || !identity)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Content directory admission needs a saved identity and empty owner");
    if (profile) {
        char *path=NULL; qa_fs_entry_kind kind; qa_fs_identity actual;
        if (!qa_fs_root_join(profile,"",&path,error)) return false;
        bool same=!strcmp(path,retained_path); free(path);
        if (same) {
            if (!qa_fs_root_status(profile,"",&kind,&actual,error)) return false;
            if (kind!=QA_FS_DIRECTORY || !qa_fs_root_identity_is(profile,&actual) ||
                !qa_fs_root_identity_is(profile,identity))
                return frontend_fail(error,QA_ERROR_FORMAT,"Saved input profile directory differs from its retained native object");
            qa_fs_root_retain(profile); *out=profile; return true;
        }
    }
    const qa_vfs_checkpoint_refs *refs=operation->services?operation->services->content_files:NULL;
    if (refs && refs->directory_open)
        return refs->directory_open(refs->context,mount_path,retained_path,identity,out,error);
    if (!qa_fs_root_open(retained_path,out,error)) return false;
    qa_fs_entry_kind kind; qa_fs_identity actual;
    return qa_fs_root_status(*out,"",&kind,&actual,error) &&
        ((kind==QA_FS_DIRECTORY && qa_fs_root_identity_is(*out,&actual) &&
            qa_fs_root_identity_is(*out,identity)) ||
         frontend_fail(error,QA_ERROR_FORMAT,"Saved content directory differs from its actual native object"));
}
static bool operation_init(frontend_persistence *operation,qa_frontend *active,
    const qa_application_persistence_ops *services,qa_error *error)
{
    if (!active || !active->application || active->stepping || active->preparing || active->capture ||
        (services && ((services->owner_count && !services->owners) ||
        ((services->publish_ready!=NULL)!=(services->publish!=NULL)))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Persistence requires an idle application and paired publication callbacks");
    operation->active=active; operation->services=services;
    operation->ranking=(qa_application_ranking_checkpoint_refs){operation,ranking_capture,ranking_resolve};
    operation->content_files=(qa_vfs_checkpoint_refs){operation,content_file_open,content_directory_open};
    operation->native_resource_refs=frontend_native_resource_refs(&operation->native_resources);
    operation->ops=(qa_application_persistence_ops){.context=operation,
        .owners=services?services->owners:NULL,.owner_count=services?services->owner_count:0,
        .visit_content=content_visit,.content_files=&operation->content_files,
        .native_resources=services && services->native_resources?services->native_resources:&operation->native_resource_refs,
        .rankings=services?services->rankings:NULL,.progress=services?services->progress:NULL,.ranking_source=&operation->ranking,
        .rankings_handoff=services && services->rankings_handoff?ranking_handoff:NULL,
        .prepare_services=prepare_services,.prepare_native_baseline=native_baseline,.prepare_content=prepare_content,
        .commands_restored=commands_restored,.reconnect=reconnect,
        .complete_state=complete_state,.validate=validate,.publish_ready=publish_ready,.publish=publish,
        .discard_services=discard_services};
    return true;
}
static void operation_close(frontend_persistence *operation)
{
    qa_input_platform_restore_guard_destroy(operation->native.input);
    qa_audio_device_restore_guard_destroy(operation->native.device);
    qa_gl_restore_guard_destroy(operation->native.gl);
    qa_display_restore_guard_destroy(operation->native.display);
    frontend_q3_source_color_defer_finish(&operation->color_ticket);
    frontend_q3_source_color_defer_abort(&operation->color_ticket);
}
static void native_capture_close(frontend_persistence *operation)
{
    if (!operation->native_resources.captured) return;
    qa_error cleanup={0};
    if (!qa_native_resource_inventory_release(&operation->native_resources.captured,&cleanup)) {
        /* The active frontend was proved to have no previous refused capture
         * before this operation began. Keep this admitted partial graph there. */
        operation->active->native_resource_inventory_pending=operation->native_resources.captured;
        operation->native_resources.captured=NULL;
    }
}
bool frontend_persistence_capture(qa_frontend *f,const qa_application_persistence_ops *services,
    qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{
    if (!f || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Save requires an empty image output");
    if (!qa_save_image_destroy_checked(&f->save_image_pending,error) ||
        !qa_native_resource_inventory_release(&f->native_resource_inventory_pending,error)) return false;
    frontend_persistence operation={0};
    bool ok=operation_init(&operation,f,services,error) &&
        qa_application_persistence_capture(f->application,&operation.ops,purpose,out,error);
    native_capture_close(&operation); operation_close(&operation); return ok;
}
bool frontend_persistence_capture_detached(qa_frontend *f,const qa_application_persistence_ops *services,
    const frontend_persistence_native *native,qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{
    if (!f || !native || (!!f->input!=!!native->input) || (!!f->device!=!!native->device) ||
        (!!f->display!=!!native->display) || (!!f->gl!=!!native->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Detached save needs its actual native ownership guards");
    return frontend_persistence_capture(f,services,purpose,out,error);
}
static bool restore_frontend(qa_frontend **slot,const qa_application_persistence_ops *services,
    qa_frontend *constructor,const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{
    if (!slot || !*slot || !image || !displaced || !retained || *retained ||
        slot==displaced || slot==retained || displaced==retained)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend restore needs distinct active, displaced and empty retained owner slots");
    frontend_persistence operation={.slot=slot,.constructor=constructor,
        .native_resources={.image=image}};
    bool ok=operation_init(&operation,*slot,services,error);
    const qa_frontend *source=constructor?constructor:operation.active;
    if (ok) {
        operation.candidate=calloc(1,sizeof(*operation.candidate));
        if (!operation.candidate) ok=frontend_fail(error,QA_ERROR_MEMORY,"Allocating stable detached frontend owner");
    }
    if (ok) {
        qa_frontend *f=operation.candidate; f->options=source->options;
        f->seats=calloc(QA_INPUT_LOCAL_SEATS,sizeof(*f->seats));
        if (!f->seats) ok=frontend_fail(error,QA_ERROR_MEMORY,"Allocating stable detached frontend seats");
        else {
            for (unsigned i=0;i<f->options.seats;++i) { f->seats[i].frontend=f; f->seats[i].id=i; }
            qa_scene_frame_init(&f->frame,QA_FRONTEND_COMMAND_OWNER);
            f->native_runtime=source->native_runtime;
            qa_native_runtime_retain(f->native_runtime);
            if (source->default_user_root) {
                f->default_user_root=SDL_strdup(source->default_user_root);
                if (!f->default_user_root) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining the candidate default user-content path");
                else f->options.application.user_root=f->default_user_root;
            }
            f->options.application.player_profile_root=qa_application_player_profile_root(source->application);
        }
    }
    if (ok) {
        ok=frontend_global_settings_storage_create(operation.candidate->options.application.user_root,
            &operation.candidate->global_settings_storage,error);
        if (ok) operation.candidate->keys=frontend_keys_create(error);
        ok=ok && operation.candidate->keys!=NULL;
        if (ok) {
            operation.candidate->config_store=frontend_config_store_create(operation.candidate,error);
            ok=operation.candidate->config_store!=NULL;
        }
    }
    qa_application *next=operation.active?operation.active->application:NULL,*old=NULL,*held=NULL;
    if (ok) {
        qa_application_options options=operation.candidate->options.application;
        frontend_application_options(operation.candidate,&options);
        ok=qa_application_persistence_restore(&next,&options,&operation.ops,image,&old,&held,error);
        if (!ok) operation.candidate->application=held;
    }
    operation_close(&operation);
    if (ok) {
        /* The concrete frontend heap was published at the same nofail boundary
         * as its application. Both displaced owners still have their original
         * stable callback contexts for separate ordinary retirement. */
        operation.active->application=old; *displaced=operation.active;
    } else if (operation.candidate && !operation.candidate->seats) {
        free(operation.candidate);
    } else if (operation.candidate) {
        qa_error cleanup={0};
        if (held || !qa_frontend_destroy(operation.candidate,&cleanup)) *retained=operation.candidate;
    }
    return ok;
}
bool frontend_persistence_restore(qa_frontend **slot,const qa_application_persistence_ops *services,
    const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{ return restore_frontend(slot,services,NULL,image,displaced,retained,error); }
bool frontend_persistence_restore_original(qa_frontend **slot,const qa_application_persistence_ops *services,
    qa_frontend *source,const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{
    if (!slot || !*slot || !source || source==*slot || !source->application ||
        source->stepping || source->preparing || source->capture || source->options.seats!=1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original publication needs its real finished isolated singleplayer frontend");
    return restore_frontend(slot,services,source,image,displaced,retained,error);
}
bool qa_frontend_persistence_capture(qa_frontend *f,const qa_application_persistence_ops *services,
    qa_save_purpose purpose,qa_save_image **out,qa_error *error)
{ return frontend_persistence_capture(f,services,purpose,out,error); }
bool qa_frontend_persistence_restore(qa_frontend **slot,const qa_application_persistence_ops *services,
    const qa_save_image *image,qa_frontend **displaced,qa_frontend **retained,qa_error *error)
{ return frontend_persistence_restore(slot,services,image,displaced,retained,error); }
