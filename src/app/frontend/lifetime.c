#include "internal.h"
#include "source_restore.h"
#include "native_q2_save.h"
#include "round.h"
#include "rankings.h"
#include "capture.h"
#include "save_commands.h"
#include "input_profile.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "keys.h"
#include "config_store.h"
#include "equipment_media.h"
#include "qc_rerelease_events.h"
#include "qa/application_startup_prepare.h"
#include <signal.h>
#include <stdio.h>

static volatile sig_atomic_t interrupted;
static void stop_signal(int signal_number) { (void)signal_number; interrupted = 1; }
void frontend_audio_engine_options(qa_frontend *frontend,qa_audio_engine_options *options)
{
    *options=(qa_audio_engine_options){.sample_rate=48000,.output_channels=2,
        .mix_frames=1024,.initial_voices=128,.observer=frontend_ui_audio_event,.observer_user=frontend};
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
            const qa_launch_instance *instance=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
            const qa_product *product=instance?qa_catalog_product(qa_launch_snapshot_catalog(publication),instance->selection.product):NULL;
            if (product) {
                if (!qa_application_provider_owner(application,binding->instance,&context.owner))
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup source no longer owns its published provider");
                context.dialect=product->family==QA_GAME_Q3?QA_CONSOLE_Q3:
                    product->family==QA_GAME_Q2?(product->edition==QA_EDITION_RERELEASE?QA_CONSOLE_Q2_RERELEASE:QA_CONSOLE_Q2):
                    product->edition==QA_EDITION_QUAKEWORLD?QA_CONSOLE_QW:QA_CONSOLE_Q1;
                for (size_t i=0;i<qa_application_console_count(application);++i) {
                    qa_console *candidate=qa_application_console_at(application,i,NULL);
                    qa_application_console_scope scope;
                    if (!qa_application_console_scope_read(application,candidate,&scope) || scope.provider!=context.owner ||
                        (scope.kind!=QA_APPLICATION_CONSOLE_QC && scope.kind!=QA_APPLICATION_CONSOLE_Q1_GAME &&
                         scope.kind!=QA_APPLICATION_CONSOLE_NATIVE_Q2 && scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME)) continue;
                    if (console && console!=candidate)
                        return frontend_fail(error,QA_ERROR_FORMAT,"Startup source has multiple physical GAME consoles");
                    console=candidate;
                }
                if (!console) return frontend_fail(error,QA_ERROR_ARGUMENT,"Startup source has no published GAME console");
            } else console=qa_application_console(application);
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
    if (frontend->stepping || frontend->preparing || frontend->round || !frontend_save_commands_idle(frontend) || !frontend_owners_idle(frontend) ||
        !frontend_seat_callbacks_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend frame, round or retained child owner is active");
    if (!frontend_network_close_client(frontend,error) || !frontend_cinematic_destroy(frontend,error) || !frontend_save_commands_destroy(frontend,error) ||
        !frontend_campaign_destroy(frontend,error)) return false;
    /* Application guests borrow frontend services. Retire them before releasing
     * their seats, scene registry, device or SDL handles. */
    for (unsigned i = 0; i < frontend->options.seats; ++i)
        if (frontend->seats[i].input && !qa_input_seat_release(frontend->seats[i].input,
            (double)frontend->time_ns / 1000000, error)) return false;
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
    if (frontend->application && !qa_application_destroy(frontend->application, error)) return false;
    frontend->application = NULL;
    if (!frontend_config_store_destroy(frontend->config_store,error)) return false;
    frontend->config_store=NULL;
    frontend_input_profile_destroy(frontend);
    if (!frontend_native_q2_discard_unbound(frontend, error)) return false;
    if (!frontend_source_discard_unbound(frontend, error)) return false;
    if (!frontend_keys_destroy(frontend->keys,error)) return false;
    frontend->keys=NULL;
    qa_native_runtime_release(frontend->native_runtime); frontend->native_runtime=NULL;
    if (!frontend_seats_destroy(frontend, error)) return false;
    qa_dedicated_console_destroy(frontend->terminal);
    qa_audio_device_close(frontend->device); frontend->device=NULL;
    qa_audio_engine_destroy(frontend->audio); frontend->audio=NULL;
    if (!frontend_ui_features_destroy(frontend,error)) return false;
    qa_scene_frame_destroy(&frontend->frame);
    if (!frontend_equipment_retire(frontend,error)) return false;
    frontend_equipment_destroy(frontend);
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
    free(frontend->map_name); free(frontend->audio_ids); free(frontend->seats); free(frontend);
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
        ok = qa_frontend_step(frontend, elapsed, error);
        if (ok) ok=frontend_save_commands_drain(slot,error);
        frontend=*slot;
        if (frontend->options.frame_limit && frontend->frame_number >= frontend->options.frame_limit) break;
        SDL_Delay(1);
    }
    if (previous_int != SIG_ERR) signal(SIGINT, previous_int);
    if (previous_term != SIG_ERR) signal(SIGTERM, previous_term);
    return ok;
}
