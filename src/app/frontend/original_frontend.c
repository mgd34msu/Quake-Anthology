#include "original_frontend.h"
#include "internal.h"
#include "persistence.h"
#include "constructor.h"
#include "capture.h"
#include "input_profile.h"
#include "campaign_cinematic.h"
#include "qa/application_q1_save.h"
#include "qa/application_q2_save.h"
#include <SDL.h>

struct qa_frontend_original_restore {
    qa_frontend *active,*source;
    const qa_application_persistence_ops *services;
    qa_frontend_original_save save;
    char *product;
    uint64_t wall_time_ns;
    bool begun,import_begun,imported,finished,visible;
};
static void save_destroy(qa_frontend_original_restore *operation)
{
    if (operation->save.family==QA_GAME_Q1) qa_q1_save_destroy(operation->save.state.q1);
    else qa_q2_save_destroy(operation->save.state.q2);
    operation->save.state.q1=NULL;
}
bool frontend_graphics_create(qa_frontend *f,qa_frontend *active,
    frontend_persistence_native *native,qa_error *error)
{
    if (f->options.dedicated) return true;
    if (active) {
        if (!native || !qa_display_create_detached(active->display,&f->display,&native->display,error)) return false;
    } else {
        f->display=qa_display_create(&f->options.display,error);
        if (!f->display) return false;
    }
    qa_display_info info;
    if (!qa_display_info_get(f->display,&info,error)) return false;
    f->observed_display=info;
    f->width=f->options.display.backend==QA_DISPLAY_CPU?f->options.display.width:info.drawable_width;
    f->height=f->options.display.backend==QA_DISPLAY_CPU?f->options.display.height:info.drawable_height;
    if (f->options.display.backend==QA_DISPLAY_CPU) {
        qa_cpu_options renderer; qa_cpu_options_default(&renderer);
        renderer.width=f->width; renderer.height=f->height; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        renderer.present=qa_display_present_cpu; renderer.present_context=f->display;
        f->cpu=qa_cpu_create(&renderer,error);
        if (!f->cpu || !qa_cpu_set_gamma(f->cpu,f->options.gamma,error)) return false;
    } else {
        qa_gl_options renderer; qa_gl_options_default(&renderer);
        renderer.display=f->display; renderer.owner=QA_FRONTEND_COMMAND_OWNER;
        if (active) {
            if (!qa_gl_create_detached(&renderer,f->options.gamma,active->gl,&f->gl,&native->gl,error)) return false;
        } else {
            f->gl=qa_gl_create(&renderer,error);
            if (!f->gl || !qa_gl_set_gamma(f->gl,f->options.gamma,error)) return false;
        }
    }
    return true;
}
static bool original_create(qa_frontend_original_restore *operation,qa_error *error)
{
    qa_frontend *active=operation->active;
    qa_frontend_options options=active->options;
    options.game=operation->product; options.map=NULL; options.map_game=NULL;
    options.movement=NULL; options.character=NULL;
    options.mods=NULL; options.mod_count=0; options.startup=NULL; options.startup_count=0;
    options.seats=1; options.menu=false;
    options.network_host=NULL; options.network_connect=NULL; options.network_port=0;
    options.network_protocol=(qa_net_protocol_id){operation->save.family==QA_GAME_Q1?QA_NET_NQ15:QA_NET_Q2_34,0,0};
    options.application.player_profile_root=NULL;
    options.application.actor_capacity=qa_actors_capacity(
        qa_session_actors(qa_application_session(active->application)));
    if (active->default_user_root) options.application.user_root=NULL;
    if (!options.dedicated) {
        qa_display_info display;
        if (!qa_display_info_get(active->display,&display,error)) return false;
        options.display.width=active->cpu?active->width:display.logical_width;
        options.display.height=active->cpu?active->height:display.logical_height;
        operation->visible=display.visible;
        options.display.hidden=true;
        if ((active->cpu && !qa_cpu_gamma_read(active->cpu,&options.gamma,error)) ||
            (active->gl && !qa_gl_gamma_read(active->gl,&options.gamma,error))) return false;
    }
    bool created=frontend_create_for_import(&options,active->native_runtime,&operation->source,error);
    qa_frontend *source=operation->source;
    if (source) {
        const qa_product *product=source->application?
            qa_catalog_find(qa_application_catalog(source->application),operation->product):NULL;
        source->options.game=product?product->key:NULL;
    }
    return created;
}
bool qa_frontend_original_restore_begin(qa_frontend *active,const qa_application_persistence_ops *services,
    qa_frontend_original_save *save,const char *product,qa_frontend_original_restore **out,qa_error *error)
{
    if (!active || !active->application || !save ||
        (save->family!=QA_GAME_Q1 && save->family!=QA_GAME_Q2) ||
        (save->family==QA_GAME_Q1?!save->state.q1:!save->state.q2) || !product || !*product || !out || *out ||
        (services && ((services->owner_count && !services->owners) ||
            ((services->publish_ready!=NULL)!=(services->publish!=NULL)))) ||
        active->stepping || active->preparing || active->round || !frontend_owners_idle(active) ||
        !frontend_seat_callbacks_idle(active) || !frontend_cinematic_capture_ready(active))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original frontend import needs its idle driver and empty operation output");
    qa_frontend_original_restore *operation=calloc(1,sizeof(*operation));
    if (!operation) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining original frontend preparation");
    operation->product=SDL_strdup(product);
    if (!operation->product) {
        free(operation); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining selected original product");
    }
    operation->active=active; operation->services=services; operation->wall_time_ns=active->wall_time_ns;
    operation->save=*save; save->state.q1=NULL;
    *out=operation;
    operation->begun=original_create(operation,error);
    return operation->begun;
}
static bool publish_ready(qa_frontend_original_restore *operation,qa_error *error)
{
    qa_frontend *source=operation->source,*active=operation->active;
    if (!frontend_owners_idle(source) || !frontend_seat_callbacks_idle(source) ||
        !frontend_cinematic_capture_ready(source) ||
        qa_application_get_state(source->application)!=QA_APPLICATION_RUNNING ||
        !frontend_network_world_change_ready(source,error) || !frontend_network_world_change_ready(active,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original publication requires its completed candidate and returned owners");
    if (source->display) {
        qa_display_info display;
        if (!qa_display_info_get(source->display,&display,error)) return false;
        if (display.visible) return frontend_fail(error,QA_ERROR_ARGUMENT,"Original candidate window became visible before publication");
    }
    return !operation->services || !operation->services->publish_ready ||
        operation->services->publish_ready(operation->services->context,active->application,source->application,error);
}
static void publish(qa_frontend_original_restore *operation,qa_frontend **slot,qa_frontend **displaced)
{
    qa_frontend *source=operation->source,*active=operation->active;
    if (operation->services && operation->services->publish)
        operation->services->publish(operation->services->context,active->application,source->application);
    active->archive_enabled=false;
    source->archive_enabled=true; source->archive_saved=false;
    source->options.display.hidden=!operation->visible;
    if (source->display) (void)qa_display_set_visible(source->display,operation->visible,NULL);
    if (source->device) qa_audio_device_pause(source->device,false);
    *slot=source; *displaced=active; operation->source=NULL;
}
bool qa_frontend_original_restore_advance(qa_frontend_original_restore *operation,qa_frontend **slot,
    bool *complete,qa_frontend **displaced,qa_frontend **retained_candidate,qa_error *error)
{
    if (!operation || !operation->begun || operation->finished || !slot || *slot!=operation->active ||
        !complete || !displaced || *displaced || !retained_candidate || *retained_candidate ||
        slot==displaced || slot==retained_candidate || displaced==retained_candidate ||
        operation->active->stepping || operation->active->preparing || operation->active->round ||
        operation->active->wall_time_ns<operation->wall_time_ns ||
        !frontend_owners_idle(operation->active) || !frontend_seat_callbacks_idle(operation->active) ||
        !frontend_cinematic_capture_ready(operation->active))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original preparation advance lost its actual idle driver and owner outputs");
    *complete=false;
    qa_frontend *source=operation->source;
    uint64_t elapsed=operation->active->wall_time_ns-operation->wall_time_ns;
    operation->wall_time_ns=operation->active->wall_time_ns;
    if (elapsed>UINT64_MAX-source->wall_time_ns) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Original candidate clock exceeds its native extent"); goto failed;
    }
    source->wall_time_ns+=elapsed;
    if (frontend_constructor_pending(source)) {
        bool constructed=false;
        if (!frontend_constructor_advance(source,&constructed,error)) goto failed;
        if (!constructed) return true;
    }
    if (!operation->import_begun) {
        source->options.game=NULL;
        bool begun=operation->save.family==QA_GAME_Q1?
            qa_application_q1_save_import(source->application,operation->save.state.q1,operation->product,error):
            qa_application_q2_save_import(source->application,operation->save.state.q2,operation->product,error);
        if (!begun) goto failed;
        operation->import_begun=true;
        save_destroy(operation);
    }
    if (!operation->imported) {
        bool advanced=operation->save.family==QA_GAME_Q1?
            qa_application_q1_save_import_advance(source->application,&operation->imported,error):
            qa_application_q2_save_import_advance(source->application,&operation->imported,error);
        if (!advanced) goto failed;
        if (!operation->imported) return true;
    }
    bool started=false;
    if (!frontend_startup_advance(source,&started,error)) goto failed;
    if (!started) return true;
    if (!frontend_input_profile_bind(source,error) || !frontend_tools_sync(source,error) ||
        (!source->options.dedicated && !frontend_scene_sync(source,error)) ||
        !publish_ready(operation,error)) goto failed;
    publish(operation,slot,displaced);
    operation->finished=true; *complete=true;
    return true;
failed:
    operation->finished=true; return false;
}
bool qa_frontend_original_restore_dispose(qa_frontend_original_restore *operation,qa_frontend **retained_source,qa_error *error)
{
    if (!retained_source || *retained_source)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Original preparation disposal needs an empty retained source output");
    if (!operation) return true;
    save_destroy(operation);
    qa_frontend *source=operation->source;
    bool ok=!source || qa_frontend_destroy(source,error);
    if (!ok) *retained_source=source;
    SDL_free(operation->product); free(operation);
    return ok;
}
