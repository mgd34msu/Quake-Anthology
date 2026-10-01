#include "internal.h"
#include "source_restore.h"
#include "native_q2_save.h"
#include "round.h"
#include "rankings.h"
#include "capture.h"
#include "save_commands.h"
#include "input_profile.h"
#include "input_settings.h"
#include "input_shutdown.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "keys.h"
#include "config_store.h"
#include "client_registry.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "selected_character.h"
#include "selected_effects.h"
#include "equipment_events.h"
#include "qc_rerelease_events.h"
#include "qa/application_startup_prepare.h"
#include <signal.h>
#include <stdio.h>

static volatile sig_atomic_t interrupted;
typedef enum frontend_shutdown_phase {
    SHUTDOWN_PREPARE, SHUTDOWN_FAILED_PREPARE, SHUTDOWN_RELEASE,
    SHUTDOWN_DETACH, SHUTDOWN_RETIRE_INPUT, SHUTDOWN_READY
} frontend_shutdown_phase;
struct frontend_shutdown {
    qa_application *application;
    frontend_shutdown_phase phase;
    qa_error failure;
    bool failure_reported, waiting, retry_cleanup;
};
static bool shutdown_admitted(qa_frontend *f,qa_error *error)
{
    if(f->stepping || f->preparing || f->round || !frontend_save_commands_idle(f) ||
        !frontend_owners_returned(f) || !frontend_seat_callbacks_returned(f) ||
        (f->shutdown && f->shutdown->application!=f->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend shutdown retains an entered parent callback");
    if(f->input_settings && f->input_shutdown)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend shutdown has two physical release owners");
    if(f->input_settings) return frontend_input_settings_shutdown_ready(f->input_settings,f,error);
    if(f->input && !qa_input_platform_settings_idle(f->input))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend shutdown has an unrelated native settings ticket");
    return f->input_shutdown?frontend_input_shutdown_ready(f->input_shutdown,f,error):
        frontend_seat_callbacks_idle(f) || frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend shutdown has an unrelated physical release");
}
static void shutdown_failure(frontend_shutdown *owner,const qa_error *error)
{ if(owner->failure.code==QA_OK && error && error->code!=QA_OK) owner->failure=*error; }
static bool shutdown_inputs(qa_frontend *f,qa_error *error)
{
    if(!f->application) return !f->input_settings && !f->input_shutdown && !f->engine_shutdown;
    if(!f->shutdown) {
        if(qa_application_startup_pending(f->application) && !qa_application_startup_abort(f->application,error)) return false;
        frontend_shutdown *owner=calloc(1,sizeof(*owner));
        if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining final frontend input shutdown");
        owner->application=f->application; f->shutdown=owner;
    }
    frontend_shutdown *owner=f->shutdown;
    double now=(double)f->wall_time_ns/1000000;
    if(owner->phase==SHUTDOWN_FAILED_PREPARE) {
        if(!frontend_input_shutdown_abort(f->input_shutdown,error) ||
            !frontend_input_shutdown_destroy(&f->input_shutdown,error)) return false;
        owner->phase=SHUTDOWN_PREPARE;
    }
    if(owner->phase==SHUTDOWN_PREPARE) {
        if(f->input_settings) {
            frontend_input_settings_view view;
            if(!frontend_input_settings_read(f->input_settings,&view,error)) return false;
            if(view.terminal) {
                if(!frontend_input_settings_destroy(&f->input_settings,error)) return false;
            } else {
                /* Association alone does not prove ALL physical held rows.
                 * Advance this phase only after the actual extension succeeds. */
                if(!frontend_input_settings_shutdown_prepare(f->input_settings,now,error)) return false;
                owner->phase=SHUTDOWN_DETACH;
            }
        }
        if(owner->phase==SHUTDOWN_PREPARE) {
            if(!frontend_input_shutdown_prepare(f,now,&f->input_shutdown,error)) {
                if(f->input_shutdown) owner->phase=SHUTDOWN_FAILED_PREPARE;
                return false;
            }
            owner->phase=SHUTDOWN_RELEASE;
        }
    }
    if(owner->phase==SHUTDOWN_RELEASE) {
        bool complete=false; qa_error fault={0};
        bool ok=frontend_input_shutdown_advance(f->input_shutdown,&complete,&fault);
        if(ok && !complete) {
            owner->waiting=true;
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Final input shutdown awaits its retained source programme");
        }
        if(!ok) shutdown_failure(owner,&fault);
        if(complete && !frontend_input_shutdown_destroy(&f->input_shutdown,error)) return false;
        owner->phase=SHUTDOWN_DETACH;
    }
    if(owner->phase==SHUTDOWN_DETACH) {
        if(!shutdown_admitted(f,error) ||
            !qa_application_engine_shutdown_begin(f->application,&f->engine_shutdown,error)) return false;
        owner->phase=SHUTDOWN_RETIRE_INPUT;
    }
    if(owner->phase==SHUTDOWN_RETIRE_INPUT) {
        if(f->input_settings) {
            bool complete=false; qa_error fault={0};
            bool ok=frontend_input_settings_engine_shutdown(f->input_settings,f->engine_shutdown,&complete,&fault);
            if(!ok) shutdown_failure(owner,&fault);
            if(!complete) { if(error) *error=fault; return false; }
            if(!frontend_input_settings_destroy(&f->input_settings,&fault)) {
                shutdown_failure(owner,&fault);
                if(f->input_settings) { if(error) *error=fault; return false; }
            }
        }
        if(f->input_shutdown && (!frontend_input_shutdown_retire(f->input_shutdown,f->engine_shutdown,error) ||
            !frontend_input_shutdown_destroy(&f->input_shutdown,error))) return false;
        if(!frontend_seat_callbacks_idle(f) || (f->input && !qa_input_platform_settings_idle(f->input)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Final input retirement still retains a physical child");
        owner->phase=SHUTDOWN_READY;
    }
    if(owner->failure.code!=QA_OK && !owner->failure_reported) {
        owner->failure_reported=true; owner->retry_cleanup=true;
        if(error) *error=owner->failure; return false;
    }
    return owner->phase==SHUTDOWN_READY;
}
static void stop_signal(int signal_number) { (void)signal_number; interrupted = 1; }
static int32_t audio_milliseconds(void *context)
{
    uint32_t word=(uint32_t)(((qa_frontend *)context)->time_ns/UINT64_C(1000000));
    int32_t value; memcpy(&value,&word,sizeof(value)); return value;
}
void frontend_audio_engine_options(qa_frontend *frontend,qa_audio_engine_options *options)
{
    *options=(qa_audio_engine_options){.sample_rate=48000,.output_channels=2,
        .mix_frames=1024,.initial_voices=128,.milliseconds=audio_milliseconds,.milliseconds_user=frontend,
        .observer=frontend_ui_audio_event,.observer_user=frontend};
}
bool frontend_audio_engine_options_ready(const qa_frontend *frontend)
{
    return frontend && qa_audio_engine_observer_is(frontend->audio,frontend_ui_audio_event,frontend) &&
        qa_audio_engine_milliseconds_is(frontend->audio,audio_milliseconds,frontend);
}
void frontend_application_options(qa_frontend *frontend, qa_application_options *application)
{
    if (frontend->native_runtime) application->native_runner=qa_native_runtime_config(frontend->native_runtime);
    application->startup_commands=frontend->options.startup;
    application->startup_command_count=frontend->options.startup_count;
    application->initial_product_key=frontend->options.game;
    application->startup_hooks=frontend_config_store_hooks(frontend->config_store);
    application->guest_context = frontend;
    application->console_print = frontend_console_print;
    application->q3_services = frontend_source_services;
    application->q3_client_prepare=frontend_source_client_prepare;
    application->q3_client_registry_reference=frontend_source_client_registry_reference;
    application->q3_client_effect = frontend_source_effect;
    application->q3_campaign_command = frontend_campaign_source_command;
    application->q3_round_services = frontend_q3_round_services();
    application->ranking_effect = frontend_ranking_effect;
    application->native_q2_services = frontend_native_q2_services;
    application->world_change_ready = frontend_world_change_ready;
    application->before_world_change = frontend_before_world_change;
    application->world_retired = frontend_world_retired;
}
bool frontend_startup_queued(const qa_frontend *frontend)
{
    if (!frontend || !frontend->application) return false;
    qa_application *application=frontend->application;
    for (size_t i=0;i<qa_application_console_count(application);++i)
        if (qa_application_startup_console_queued(application,qa_application_console_at(application,i,NULL))) return true;
    return false;
}
bool frontend_startup_replay(qa_frontend *frontend,qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->preparing || frontend->capture)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup replay requires its idle published source");
    qa_application *application=frontend->application;
    if (qa_application_startup_pending(application)) return true;
    for (size_t ordinal=0;ordinal<qa_application_startup_command_count(application);++ordinal) {
        if (!qa_application_startup_command_pending(application,ordinal)) continue;
        qa_console *console=NULL;
        if (!qa_application_startup_command_queued_console(application,ordinal,&console,error)) return false;
        qa_application_travel_view travel;
        if (qa_application_should_stop(application) || (!console && qa_application_travel_read(application,&travel))) return true;
        qa_command_context context={.origin=QA_COMMAND_LOCAL,.dialect=QA_CONSOLE_Q1};
        if (!console) {
            const qa_launch_snapshot *publication=qa_application_launch(application);
            const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
            const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,
                (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
            for (size_t i=0;publication && i<qa_application_console_count(application);++i) {
                qa_console *candidate=qa_application_console_at(application,i,NULL);
                if (candidate==qa_application_console(application)) continue;
                bool primary=false;
                if (!qa_application_startup_console_primary(application,candidate,&primary,error)) return false;
                if (!primary) continue;
                if (console && console!=candidate)
                    return frontend_fail(error,QA_ERROR_FORMAT,"Startup has multiple primary physical consoles");
                console=candidate;
            }
            if (console) {
                qa_application_console_scope scope;
                if (!qa_application_console_scope_read(application,console,&scope))
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup lost its primary physical console scope");
                const char *name=qa_application_provider_instance(application,scope.provider);
                const qa_launch_instance *instance=name?qa_launch_snapshot_find(publication,name):NULL;
                const qa_product *product=instance?qa_catalog_product(qa_launch_instance_catalog(instance),instance->selection.product):NULL;
                if (!product) return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup lost its primary source product");
                context.owner=scope.provider;
                context.seat=scope.seat;
                if (scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME || scope.kind==QA_APPLICATION_CONSOLE_Q3_UI)
                    context.origin=QA_COMMAND_SEAT;
                context.dialect=product->family==QA_GAME_Q3?QA_CONSOLE_Q3:
                    product->family==QA_GAME_Q2?(product->edition==QA_EDITION_RERELEASE?QA_CONSOLE_Q2_RERELEASE:QA_CONSOLE_Q2):
                    product->edition==QA_EDITION_QUAKEWORLD?QA_CONSOLE_QW:QA_CONSOLE_Q1;
            } else {
                if (binding || frontend_network_remote(frontend))
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup source has no published primary console");
                console=qa_application_console(application);
            }
            frontend->preparing=true;
            bool queued=qa_application_startup_command_queue(application,ordinal,console,&context,error);
            frontend->preparing=false;
            if (!queued) return false;
        }
        frontend->preparing=true;
        size_t executed;
        bool ok=qa_console_drain(console,0,&executed,error);
        bool yielded=qa_console_drain_yielded(console);
        if (ok && !yielded && !qa_console_pending(console))
            ok=qa_application_startup_command_complete(application,ordinal,error);
        frontend->preparing=false;
        if (!ok) return false;
        if (yielded || qa_console_pending(console)) return true;
    }
    return true;
}
qa_application *qa_frontend_application(qa_frontend *frontend) { return frontend ? frontend->application : NULL; }
void frontend_print(void *context, const char *message)
{
    qa_frontend *frontend = context;
    fputs(message, stdout);
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        qa_error error = {0};
        if (frontend->seats[i].console && !qa_seat_console_print(frontend->seats[i].console, message, &error))
            fprintf(stderr, "console output: %s\n", error.message);
    }
}
void frontend_console_print(void *context, const qa_command_context *source, const char *text)
{
    qa_frontend *frontend = context;
    fputs(text, stdout);
    if (source && source->origin == QA_COMMAND_REMOTE) return;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        uint32_t launch_seat; bool admitted=frontend_seat_launch_id_read(frontend,i,&launch_seat);
        if (source && (source->origin == QA_COMMAND_SEAT || source->origin == QA_COMMAND_LOCAL) &&
            source->seat != (admitted?launch_seat:i)) continue;
        if (source && source->actor.registry) {
            qa_actor_id actor;
            if (!admitted || !qa_application_player_actor(frontend->application, launch_seat, &actor) || !qa_actor_id_equal(actor, source->actor)) continue;
        }
        qa_error error = {0};
        if (frontend->seats[i].console && !qa_seat_console_print(frontend->seats[i].console, text, &error))
            fprintf(stderr, "console output: %s\n", error.message);
    }
}
bool qa_frontend_create(const qa_frontend_options *options, qa_frontend **out, qa_error *error)
{
    if (!options || !out || !options->seats || options->seats > 4)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend options");
    *out = NULL;
    qa_frontend *frontend = calloc(1, sizeof(*frontend));
    if (!frontend) return frontend_fail(error, QA_ERROR_MEMORY, "allocating frontend owner");
    frontend->options = *options;
    frontend->seats = calloc(options->seats, sizeof(*frontend->seats));
    if (!frontend->seats) {
        free(frontend); return frontend_fail(error, QA_ERROR_MEMORY, "allocating stable local seat contexts");
    }
    for (unsigned i=0;i<options->seats;++i) {
        frontend->seats[i].frontend=frontend; frontend->seats[i].id=i;
    }
    qa_scene_frame_init(&frontend->frame, QA_FRONTEND_COMMAND_OWNER);
    if (!frontend_input_profile_default_options(frontend,error)) goto fail;
    {
        char *executable=SDL_GetBasePath();
        if (!executable) { qa_error_set(error,QA_ERROR_IO,0,"Reading native executable directory: %s",SDL_GetError()); goto fail; }
        qa_native_runtime_options native={.executable_directory=executable,.root=options->native_runtime_root,
            .wine=options->native_wine,.overrides=options->application.native_runner};
        bool ready=qa_native_runtime_create(&native,&frontend->native_runtime,error);
        SDL_free(executable);
        if (!ready) goto fail;
        frontend->options.application.native_runner=qa_native_runtime_config(frontend->native_runtime);
    }
    frontend->keys=frontend_keys_create(error);
    if (!frontend->keys) goto fail;
    frontend->config_store=frontend_config_store_create(frontend,error);
    if (!frontend->config_store) goto fail;
    qa_application_options application = frontend->options.application;
    frontend_application_options(frontend, &application);
    if (!qa_application_create(&application, &frontend->application, error)) goto fail;
    if (!frontend_equipment_events_create(frontend,&frontend->gear_events,error)) goto fail;
    frontend->sdl_subsystems = SDL_INIT_TIMER | SDL_INIT_EVENTS;
    if (!options->dedicated) frontend->sdl_subsystems |= SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
    if (!options->dedicated && options->audio) frontend->sdl_subsystems |= SDL_INIT_AUDIO;
    if (SDL_InitSubSystem(frontend->sdl_subsystems)) {
        frontend->sdl_subsystems = 0;
        qa_error_set(error, QA_ERROR_IO, 0, "SDL initialization: %s", SDL_GetError()); goto fail;
    }
    if (!frontend_commands(frontend, error)) goto fail;
    if (options->dedicated) {
        if (!frontend_ui_features_prepare(frontend,error)) goto fail;
        frontend->terminal = qa_dedicated_console_create(error);
        if (!frontend->terminal) goto fail;
    } else {
        frontend->display = qa_display_create(&options->display, error);
        if (!frontend->display) goto fail;
        qa_display_info info;
        if (!qa_display_info_get(frontend->display, &info, error)) goto fail;
        frontend->width = info.drawable_width; frontend->height = info.drawable_height;
        if (options->display.backend == QA_DISPLAY_CPU) {
            qa_cpu_options renderer;
            qa_cpu_options_default(&renderer);
            renderer.width = frontend->width; renderer.height = frontend->height;
            renderer.owner = QA_FRONTEND_COMMAND_OWNER;
            renderer.present = qa_display_present_cpu; renderer.present_context = frontend->display;
            frontend->cpu = qa_cpu_create(&renderer, error);
            if (!frontend->cpu || !qa_cpu_set_gamma(frontend->cpu, options->gamma, error)) goto fail;
        } else {
            qa_gl_options renderer;
            qa_gl_options_default(&renderer); renderer.display = frontend->display; renderer.owner = QA_FRONTEND_COMMAND_OWNER;
            frontend->gl = qa_gl_create(&renderer, error);
            if (!frontend->gl || !qa_gl_set_gamma(frontend->gl, options->gamma, error)) goto fail;
        }
        if (!frontend_resources(frontend, error) || !frontend_seats_create(frontend, error)) goto fail;
        qa_input_platform_options input = {.cvars = qa_application_cvars(frontend->application),
            .user = frontend, .print = frontend_print};
        frontend->input = qa_input_platform_create(&input, error);
        if (!frontend->input) goto fail;
        qa_input_seat *seats[4] = {0};
        qa_controller_selection controllers[4] = {0};
        for (unsigned i = 0; i < options->seats; ++i) seats[i] = frontend->seats[i].input;
        if (!qa_input_platform_routes(frontend->input, seats, controllers, 0, 0, error) ||
            !qa_input_platform_window(frontend->input, frontend->display, 0, error)) goto fail;
        {
            qa_audio_engine_options audio;
            frontend_audio_engine_options(frontend,&audio);
            qa_audio_device_options device = {.format = {48000, 2, 16}, .buffer_frames = 1024};
            if (!qa_audio_engine_create(&audio, &frontend->audio, error)) goto fail;
            if (options->audio) {
                if (!qa_audio_device_open(&device, &frontend->device, error)) goto fail;
                qa_audio_device_pause(frontend->device, false);
            }
        }
    }
    if (!frontend_tools_create(frontend, error) || !frontend_save_commands_create(frontend,error) || !frontend_launch(frontend, error) ||
        !frontend_input_profile_bind(frontend,error) ||
        !frontend_tools_sync(frontend, error) || !frontend_network_create(frontend, error)) goto fail;
    if (!options->dedicated) for (unsigned i = 0; i < options->seats; ++i)
        if (!qa_ui_llm_create(frontend->seats[i].ui, frontend_tools_llm(frontend),
            FRONTEND_ASSISTANCE, &frontend->seats[i].assistance, error)) goto fail;
    if (!qa_application_startup_pending(frontend->application) &&
        !qa_application_rankings_start(frontend->application, error)) goto fail;
    if (!options->dedicated && (options->menu ||
        (!qa_application_launch(frontend->application) && !qa_application_startup_pending(frontend->application))))
        if (!frontend_game_menu(&frontend->seats[0], error)) goto fail;
    frontend->archive_enabled=true;
    *out = frontend;
    return true;
fail: {
    qa_error cleanup = {0};
    if (!qa_frontend_destroy(frontend, &cleanup)) {
        *out=frontend;
        fprintf(stderr, "frontend cleanup retained its owner: %s\n", cleanup.message);
    }
    return false;
}}
bool qa_frontend_destroy(qa_frontend *frontend, qa_error *error)
{
    if (!frontend) return true;
    if (frontend->shutdown) {
        frontend->shutdown->waiting=false;
        frontend->shutdown->retry_cleanup=false;
    }
    if (!shutdown_admitted(frontend,error)) return false;
    if (frontend->archive_enabled && !frontend->archive_saved && frontend->application &&
        !frontend->source_restoring && !qa_application_startup_pending(frontend->application)) {
        if (!frontend_keys_save(frontend->keys,error) ||
            !frontend_config_store_save(frontend->config_store,error)) return false;
        frontend->archive_saved=true;
    }
    if (!shutdown_inputs(frontend,error)) return false;
    if (!frontend_network_close_client(frontend,error) || !frontend_cinematic_destroy(frontend,error) || !frontend_save_commands_destroy(frontend,error) ||
        !frontend_campaign_destroy(frontend,error)) return false;
    if (!frontend_selected_effects_retire(frontend,error)) return false;
    if (!frontend_remote_q3_destroy(frontend,error)) return false;
    if (!frontend_native_q3_destroy(frontend,error)) return false;
    if (!frontend_equipment_events_destroy(frontend->gear_events,error)) return false;
    frontend->gear_events=NULL;
    /* Application guests borrow frontend services. Retire them before releasing
     * their seats, scene registry, device or SDL handles. */
    qa_input_platform_destroy(frontend->input); frontend->input = NULL;
    qa_input_console_destroy(frontend->input_commands); frontend->input_commands = NULL;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        if (!qa_ui_llm_destroy(frontend->seats[i].assistance, (double)frontend->time_ns / 1000000, error)) return false;
        frontend->seats[i].assistance = NULL;
    }
    if (frontend->application &&
        (!frontend_tools_before_world_change(frontend, error) ||
         !qa_application_retire_sources(frontend->application, error))) return false;
    if (!frontend_network_destroy(frontend, error) || !frontend_tools_destroy(frontend, error)) return false;
    frontend_qc_rerelease_destroy(frontend);
    if (frontend->config_store && !frontend_config_store_retired_ready(frontend->config_store,error)) return false;
    if (!frontend_ui_features_destroy(frontend,error) || !frontend_seats_destroy(frontend,error)) return false;
    if (frontend->engine_shutdown &&
        !qa_application_engine_shutdown_finish(frontend->application,&frontend->engine_shutdown,error)) return false;
    if (frontend->application && !qa_application_destroy(frontend->application, error)) return false;
    frontend->application = NULL;
    if (frontend->shutdown) frontend->shutdown->application=NULL;
    if (!frontend_config_store_destroy(frontend->config_store,error)) return false;
    frontend->config_store=NULL;
    frontend_input_profile_destroy(frontend);
    if (!frontend_native_q2_discard_unbound(frontend, error)) return false;
    if (!frontend_source_discard_unbound(frontend, error)) return false;
    if (!frontend_client_registries_retired(frontend,error)) return false;
    if (!frontend_client_registries_discard_restore(frontend,error)) return false;
    if (!frontend_keys_destroy(frontend->keys,error)) return false;
    frontend->keys=NULL;
    qa_native_runtime_release(frontend->native_runtime); frontend->native_runtime=NULL;
    qa_dedicated_console_destroy(frontend->terminal);
    qa_audio_device_close(frontend->device); frontend->device=NULL;
    qa_audio_engine_destroy(frontend->audio); frontend->audio=NULL;
    qa_scene_frame_destroy(&frontend->frame);
    if (!frontend_equipment_retire(frontend,error)) return false;
    frontend_equipment_destroy(frontend);
    if (!frontend_equipment_q3_retire(frontend,error)) return false;
    frontend_equipment_q3_destroy(frontend);
    if (!frontend_equipment_gear_retire(frontend,error)) return false;
    frontend_equipment_gear_destroy(frontend);
    if (!frontend_selected_character_retire(frontend,error)) return false;
    frontend_visuals_destroy(frontend);
    frontend_particle_retire(frontend);
    frontend_event_retire(frontend);
    frontend_shader_destroy(frontend);
    qa_scene_world_destroy(frontend->scene_world);
    qa_resource_release(frontend->map_resource);
    qa_audio_bank_destroy(frontend->sounds);
    qa_material_library_destroy(frontend->materials);
    qa_scene_resources_destroy(frontend->images);
    qa_vfs_destroy(frontend->mounts);
    qa_font_library_destroy(frontend->fonts);
    qa_scene_image_release(frontend->console_background);
    qa_scene_resources_destroy(frontend->ui_images);
    qa_vfs_destroy(frontend->ui_mounts);
    qa_material_order_destroy(frontend->order);
    qa_cpu_destroy(frontend->cpu); qa_gl_destroy(frontend->gl); qa_display_destroy(frontend->display);
    if (frontend->sdl_subsystems) SDL_QuitSubSystem(frontend->sdl_subsystems);
    free(frontend->map_name); free(frontend->audio_ids); free(frontend->seats); free(frontend->shutdown); free(frontend);
    return true;
}
bool qa_frontend_shutdown(qa_frontend **slot,qa_error *error)
{
    if(!slot) return frontend_fail(error,QA_ERROR_ARGUMENT,"Final shutdown requires its actual frontend slot");
    qa_error original={0};
    uint64_t frequency=0,last=0;
    while(*slot) {
        qa_frontend *frontend=*slot;
        qa_error fault={0};
        if(qa_frontend_destroy(frontend,&fault)) {
            *slot=NULL;
            if(original.code!=QA_OK) { if(error) *error=original; return false; }
            return true;
        }
        frontend_shutdown *owner=frontend->shutdown;
        if(!owner || (!owner->waiting && !owner->retry_cleanup)) {
            if(error) *error=original.code!=QA_OK?original:fault;
            return false;
        }
        if(owner->retry_cleanup) {
            if(original.code==QA_OK) original=fault;
            continue;
        }
        if(!frequency) {
            frequency=SDL_GetPerformanceFrequency(); last=SDL_GetPerformanceCounter();
            if(!frequency) return frontend_fail(error,QA_ERROR_IO,"Final shutdown has no monotonic timer");
        }
        SDL_Delay(1);
        uint64_t now=SDL_GetPerformanceCounter(),ticks=now-last;
        last=now;
        uint64_t seconds=ticks/frequency;
        if(seconds>UINT64_MAX/UINT64_C(1000000000))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Final shutdown wall duration overflow");
        uint64_t elapsed=seconds*UINT64_C(1000000000)+
            (uint64_t)((long double)(ticks%frequency)*1000000000.0L/(long double)frequency);
        if(elapsed>UINT64_MAX-frontend->wall_time_ns)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Final shutdown wall clock overflow");
        frontend->wall_time_ns+=elapsed;
    }
    return true;
}
bool qa_frontend_run(qa_frontend **slot, qa_error *error)
{
    if (!slot || !*slot) return frontend_fail(error, QA_ERROR_ARGUMENT, "missing frontend driver slot");
    interrupted = 0;
    void (*previous_int)(int) = signal(SIGINT, stop_signal);
    void (*previous_term)(int) = signal(SIGTERM, stop_signal);
    uint64_t frequency = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    bool ok = frequency != 0;
    if (!ok) frontend_fail(error, QA_ERROR_IO, "monotonic timer unavailable");
    while (ok && !interrupted && !qa_application_should_stop((*slot)->application)) {
        qa_frontend *frontend=*slot;
        uint64_t now = SDL_GetPerformanceCounter(), ticks = now - last;
        last = now;
        uint64_t seconds = ticks / frequency;
        if (seconds > UINT64_MAX / UINT64_C(1000000000)) { ok = frontend_fail(error, QA_ERROR_ARGUMENT, "monotonic duration overflow"); break; }
        uint64_t elapsed = seconds * UINT64_C(1000000000) +
            (uint64_t)((long double)(ticks % frequency) * 1000000000.0L / (long double)frequency);
        if (!frontend_save_commands_restoring(frontend))
            ok = qa_frontend_step(frontend, elapsed, error);
        if (ok) ok=frontend_save_commands_drain(slot,error);
        frontend=*slot;
        if (!frontend_save_commands_restoring(frontend) && frontend->options.frame_limit &&
            frontend->frame_number >= frontend->options.frame_limit) break;
        SDL_Delay(1);
    }
    if (previous_int != SIG_ERR) signal(SIGINT, previous_int);
    if (previous_term != SIG_ERR) signal(SIGTERM, previous_term);
    return ok;
}
