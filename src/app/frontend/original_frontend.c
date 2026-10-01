#include "original_frontend.h"
#include "internal.h"
#include "persistence.h"
#include "capture.h"
#include "input_profile.h"
#include "save_commands.h"
#include "ui_features.h"
#include "campaign_cinematic.h"
#include "qa/application_q1_save.h"
#include <SDL.h>

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
    if (active->default_user_root) {
        f->default_user_root=SDL_strdup(active->default_user_root);
        if (!f->default_user_root) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining original frontend user directory");
        f->options.application.user_root=f->default_user_root;
    }
    f->seats[0].frontend=f; f->seats[0].id=0;
    qa_scene_frame_init(&f->frame,QA_FRONTEND_COMMAND_OWNER);
    qa_application_options options=f->options.application;
    frontend_application_options(f,&options);
    if (!qa_application_create(&options,&f->application,error) || !frontend_commands(f,error) ||
        !qa_application_q1_save_import(f->application,save,product,error) ||
        !frontend_input_profile_bind(f,error)) return false;
    if (f->options.dedicated) {
        f->terminal=qa_dedicated_console_create(error);
        if (!f->terminal || !frontend_ui_features_prepare(f,error)) return false;
    } else if (!graphics_create(f,active,native,error)) return false;
    if (!frontend_tools_create(f,error) || !frontend_save_commands_create(f,error) ||
        !frontend_tools_sync(f,error) || !frontend_network_create(f,error)) return false;
    if (!f->options.dedicated) {
        if (!qa_ui_llm_create(f->seats[0].ui,frontend_tools_llm(f),FRONTEND_ASSISTANCE,
            &f->seats[0].assistance,error) || !frontend_scene_sync(f,error)) return false;
    }
    return true;
}
bool qa_frontend_q1_save_restore(qa_frontend **slot,const qa_application_persistence_ops *services,
    const qa_q1_save_data *save,const char *product,qa_frontend **displaced,
    qa_frontend **retained_source,qa_frontend **retained_candidate,qa_error *error)
{
    if (!slot || !*slot || !(*slot)->application || !save || !product || !*product ||
        !displaced || *displaced || !retained_source || *retained_source || !retained_candidate || *retained_candidate ||
        displaced==retained_source || displaced==retained_candidate || retained_source==retained_candidate ||
        displaced==slot || retained_source==slot || retained_candidate==slot || (*slot)->stepping ||
        (*slot)->preparing || (*slot)->round || !frontend_owners_idle(*slot) || !frontend_seat_callbacks_idle(*slot) ||
        !frontend_cinematic_capture_ready(*slot))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original frontend import needs an idle driver and distinct empty owner outputs");
    qa_frontend *source=NULL; frontend_persistence_native native={0}; qa_save_image *image=NULL;
    bool ok=original_create(*slot,save,product,&source,&native,error) &&
        frontend_persistence_capture_detached(source,services,&native,QA_SAVE_MANUAL,&image,error) &&
        frontend_persistence_restore_original(slot,services,source,image,displaced,retained_candidate,error);
    qa_save_image_destroy(image);
    native_guards_destroy(&native);
    qa_error cleanup={0};
    if (source && !qa_frontend_destroy(source,&cleanup)) *retained_source=source;
    return ok;
}
