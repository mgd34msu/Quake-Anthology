#include "qc_messages.h"
#include "original_frontend.h"
#include "internal.h"
#include "persistence.h"
#include "capture.h"
#include "input_profile.h"
#include "save_commands.h"
#include "ui_features.h"
#include "campaign_cinematic.h"
#include "config_store.h"
#include "keys.h"
#include "equipment_events.h"
#include "shared_register.h"
#include "view_bindings.h"
#include "q1_sky.h"
#include "music_sources.h"
#include "global_settings_storage.h"
#include "qa/application_q1_save.h"
#include <SDL.h>

struct qa_frontend_q1_restore {
    qa_frontend *active,*source;
    const qa_application_persistence_ops *services;
    frontend_persistence_native native;
    bool begun,finished,final_cut;
};
static void native_guards_destroy(frontend_persistence_native *native)
{
    qa_input_platform_restore_guard_destroy(native->input);
    qa_audio_device_restore_guard_destroy(native->device);
    qa_gl_restore_guard_destroy(native->gl);
    qa_display_restore_guard_destroy(native->display);
    *native=(frontend_persistence_native){0};
}
static bool graphics_create(qa_frontend *f,qa_frontend *active,
    frontend_persistence_native *native,qa_error *error)
{
    if (!qa_display_create_detached(active->display,&f->display,&native->display,error)) return false;
    qa_display_info info;
    if (!qa_display_info_get(f->display,&info,error)) return false;
    f->width=info.drawable_width; f->height=info.drawable_height;
    if (f->options.display.backend==QA_DISPLAY_CPU) {
        qa_cpu_options renderer; qa_cpu_options_default(&renderer);
        renderer.width=f->width; renderer.height=f->height; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        renderer.present=qa_display_present_cpu; renderer.present_context=f->display;
        f->cpu=qa_cpu_create(&renderer,error);
        if (!f->cpu || !qa_cpu_set_gamma(f->cpu,f->options.gamma,error)) return false;
    } else {
        qa_gl_options renderer; qa_gl_options_default(&renderer);
        renderer.display=f->display; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        if (!qa_gl_create_detached(&renderer,f->options.gamma,active->gl,&f->gl,&native->gl,error)) return false;
    }
    if (!frontend_resources(f,error) || !frontend_seats_create(f,error)) return false;
    qa_audio_engine_options audio; frontend_audio_engine_options(f,&audio);
    if (!qa_audio_engine_create(&audio,&f->audio,error) ||
        (active->device && !qa_audio_device_create_detached(active->device,f->audio,&f->device,&native->device,error))) return false;
    qa_input_platform_options input={.cvars=qa_application_cvars(f->application),.user=f,.print=frontend_print};
    f->input=qa_input_platform_create_detached(&input,error);
    qa_input_seat *seats[4]={f->seats[0].input}; qa_controller_selection controllers[4]={0};
    return f->input && qa_input_platform_prepare_fresh(f->input,active->input,seats,controllers,0,f->display,0,
        &native->input,error);
}
static bool original_create(qa_frontend *active,const qa_q1_save_data *save,const char *product,
    qa_frontend **out,frontend_persistence_native *native,qa_error *error)
{
    qa_frontend *f=calloc(1,sizeof(*f));
    if (!f) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating original source frontend");
    f->seats=calloc(1,sizeof(*f->seats));
    if (!f->seats) {
        free(f); return frontend_fail(error,QA_ERROR_MEMORY,"Allocating original source seat");
    }
    *out=f;
    f->options=active->options;
    f->native_runtime=active->native_runtime;
    qa_native_runtime_retain(f->native_runtime);
    f->options.game=NULL; f->options.map=NULL; f->options.map_game=NULL;
    f->options.movement=NULL; f->options.character=NULL;
    f->options.mods=NULL; f->options.mod_count=0; f->options.startup=NULL; f->options.startup_count=0;
    f->options.seats=1; f->options.menu=false;
    f->options.network_host=NULL; f->options.network_connect=NULL; f->options.network_port=0;
    f->options.network_protocol=(qa_net_protocol_id){QA_NET_NQ15,0,0};
    f->options.application.player_profile_root=NULL;
    f->options.application.actor_capacity=qa_actors_capacity(
        qa_session_actors(qa_application_session(active->application)));
    if (active->default_user_root) {
        f->default_user_root=SDL_strdup(active->default_user_root);
        if (!f->default_user_root) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining original frontend user directory");
        f->options.application.user_root=f->default_user_root;
    }
    f->seats[0].frontend=f; f->seats[0].id=0;
    qa_scene_frame_init(&f->frame,QA_FRONTEND_COMMAND_OWNER);
    f->keys=frontend_keys_create(error);
    if (!f->keys) return false;
    if (!frontend_global_settings_storage_create(f->options.application.user_root,&f->global_settings_storage,error)) return false;
    f->config_store=frontend_config_store_create(f,error);
    if (!f->config_store) return false;
    qa_application_options options=f->options.application;
    frontend_application_options(f,&options);
    if (!qa_application_create(&options,&f->application,error)) return false;
    const qa_product *selected=frontend_product_selection(qa_application_catalog(f->application),product);
    if (!selected || selected->availability!=QA_CONTENT_INSTALLED || selected->family!=QA_GAME_Q1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original ENGINE settings lack their selected Quake source product");
    qa_console_dialect dialect=selected->edition==QA_EDITION_QUAKEWORLD?QA_CONSOLE_QW:QA_CONSOLE_Q1;
    qa_audio_output_format output=active->device?qa_audio_device_requested_configuration(active->device).format:
        active->audio_output_format;
    f->audio_output_format=output;
    if ((active->cpu && !qa_cpu_gamma_read(active->cpu,&f->options.gamma,error)) ||
        (active->gl && !qa_gl_gamma_read(active->gl,&f->options.gamma,error)) ||
        !frontend_shared_register(qa_application_cvars(f->application),&dialect,output,f->options.gamma,error) ||
        (!f->options.dedicated && !frontend_q1_sky_create(f,&f->q1_sky,error)) ||
        !frontend_qc_messages_create(f,&f->qc_messages,error) ||
        !frontend_view_bindings_create(f,error) ||
        !frontend_equipment_events_create(f,&f->gear_events,error) || !frontend_commands(f,error)) return false;
    if (f->options.dedicated) {
        f->terminal=qa_dedicated_console_create(error);
        if (!f->terminal || !frontend_ui_features_prepare(f,error)) return false;
    } else {
        /* The selected source supplies the genuine new UI font view; it never
         * becomes a constructor startup command or a retained option borrow. */
        f->options.game=product;
        bool ready=graphics_create(f,active,native,error);
        f->options.game=NULL;
        if (!ready) return false;
    }
    f->options.game=product;
    bool music_ready=frontend_music_sources_create(f,&f->music_sources,error);
    f->options.game=NULL;
    if (!music_ready) return false;
    if (!frontend_tools_create(f,error) || !frontend_save_commands_create(f,error) ||
        !frontend_network_create(f,error)) return false;
    if (!f->options.dedicated) {
        if (!qa_ui_llm_create(f->seats[0].ui,frontend_tools_llm(f),FRONTEND_ASSISTANCE,
            &f->seats[0].assistance,error)) return false;
    }
    return qa_application_q1_save_import(f->application,save,product,error);
}
bool qa_frontend_q1_restore_begin(qa_frontend *active,const qa_application_persistence_ops *services,
    const qa_q1_save_data *save,const char *product,qa_frontend_q1_restore **out,qa_error *error)
{
    if (!active || !active->application || !save || !product || !*product || !out || *out ||
        active->stepping || active->preparing || active->round || !frontend_owners_idle(active) ||
        !frontend_seat_callbacks_idle(active) || !frontend_cinematic_capture_ready(active))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original frontend import needs its idle driver and empty operation output");
    qa_frontend_q1_restore *operation=calloc(1,sizeof(*operation));
    if (!operation) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining original frontend preparation");
    operation->active=active; operation->services=services;
    *out=operation;
    operation->begun=original_create(active,save,product,&operation->source,&operation->native,error);
    return operation->begun;
}
bool qa_frontend_q1_restore_advance(qa_frontend_q1_restore *operation,qa_frontend **slot,
    bool *complete,qa_frontend **displaced,qa_frontend **retained_candidate,qa_error *error)
{
    if (!operation || !operation->begun || operation->finished || !slot || *slot!=operation->active ||
        !complete || !displaced || *displaced || !retained_candidate || *retained_candidate ||
        slot==displaced || slot==retained_candidate || displaced==retained_candidate ||
        operation->active->stepping || operation->active->preparing || operation->active->round ||
        !frontend_owners_idle(operation->active) || !frontend_seat_callbacks_idle(operation->active) ||
        !frontend_cinematic_capture_ready(operation->active))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original preparation advance lost its actual idle driver and owner outputs");
    *complete=false;
    qa_frontend *source=operation->source;
    bool imported=false;
    if (!qa_application_q1_save_import_advance(source->application,&imported,error)) {
        operation->finished=true; return false;
    }
    if (!imported) return true;
    operation->finished=true;
    qa_save_image *image=NULL;
    bool ok=frontend_input_profile_bind(source,error) && frontend_tools_sync(source,error) &&
        (source->options.dedicated || frontend_scene_sync(source,error)) &&
        qa_application_rankings_start(source->application,error);
    if (ok) {
        operation->final_cut=true;
        ok=frontend_persistence_capture_detached(source,operation->services,&operation->native,QA_SAVE_MANUAL,&image,error) &&
            frontend_persistence_restore_original(slot,operation->services,source,image,displaced,retained_candidate,error);
        operation->final_cut=false;
    }
    qa_save_image_destroy(image);
    *complete=ok;
    return ok;
}
bool qa_frontend_q1_restore_capture_ready(const qa_frontend_q1_restore *operation)
{ return operation && operation->begun && operation->finished && operation->final_cut; }
bool qa_frontend_q1_restore_dispose(qa_frontend_q1_restore *operation,qa_frontend **retained_source,qa_error *error)
{
    if (!retained_source || *retained_source)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original preparation disposal needs an empty retained source output");
    if (!operation) return true;
    native_guards_destroy(&operation->native);
    qa_frontend *source=operation->source;
    bool ok=!source || ((!source->application || !qa_application_startup_pending(source->application) ||
        qa_application_startup_abort(source->application,error)) && qa_frontend_destroy(source,error));
    if (!ok) *retained_source=source;
    free(operation);
    return ok;
}
