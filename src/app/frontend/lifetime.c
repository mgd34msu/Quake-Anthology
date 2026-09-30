#include "internal.h"
#include <signal.h>
#include <stdio.h>

static volatile sig_atomic_t interrupted;
static void stop_signal(int signal_number) { (void)signal_number; interrupted = 1; }
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
        if (source && (source->origin == QA_COMMAND_SEAT || source->origin == QA_COMMAND_LOCAL) && source->seat != i) continue;
        if (source && source->actor.registry) {
            qa_actor_id actor;
            if (!qa_application_player_actor(frontend->application, i, &actor) || !qa_actor_id_equal(actor, source->actor)) continue;
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
    qa_scene_frame_init(&frontend->frame, QA_FRONTEND_COMMAND_OWNER);
    qa_application_options application = options->application;
    application.guest_context = frontend;
    application.console_print = frontend_console_print;
    application.q3_services = frontend_source_services;
    application.native_q2_services = frontend_native_q2_services;
    application.world_change_ready = frontend_world_change_ready;
    application.before_world_change = frontend_before_world_change;
    application.world_retired = frontend_world_retired;
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
            qa_audio_engine_options audio = {.sample_rate = 48000, .output_channels = 2,
                .mix_frames = 1024, .initial_voices = 128};
            qa_audio_device_options device = {.format = {48000, 2, 16}, .buffer_frames = 1024};
            if (!qa_audio_engine_create(&audio, &frontend->audio, error)) goto fail;
            if (options->audio) {
                if (!qa_audio_device_open(&device, &frontend->device, error)) goto fail;
                qa_audio_device_pause(frontend->device, false);
            }
        }
    }
    if (!frontend_tools_create(frontend, error) || !frontend_launch(frontend, error) ||
        !frontend_tools_sync(frontend, error) || !frontend_network_create(frontend, error)) goto fail;
    if (!options->dedicated) for (unsigned i = 0; i < options->seats; ++i)
        if (!qa_ui_llm_create(frontend->seats[i].ui, frontend_tools_llm(frontend),
            FRONTEND_ASSISTANCE, &frontend->seats[i].assistance, error)) goto fail;
    for (size_t i = 0; i < options->startup_count; ++i)
        if (!qa_console_execute_now(qa_application_console(frontend->application),
            &(qa_command_context){.origin = QA_COMMAND_LOCAL, .dialect = QA_CONSOLE_Q1}, options->startup[i], error)) goto fail;
    if (!options->dedicated && (options->menu || !qa_application_launch(frontend->application)))
        if (!frontend_game_menu(&frontend->seats[0], error)) goto fail;
    *out = frontend;
    return true;
fail: {
    qa_error cleanup = {0};
    if (!qa_frontend_destroy(frontend, &cleanup)) fprintf(stderr, "frontend cleanup: %s\n", cleanup.message);
    return false;
}}
bool qa_frontend_destroy(qa_frontend *frontend, qa_error *error)
{
    if (!frontend) return true;
    if (frontend->stepping) return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend frame callback is active");
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
    if (frontend->application && !qa_application_destroy(frontend->application, error)) return false;
    frontend->application = NULL;
    if (!frontend_seats_destroy(frontend, error)) return false;
    qa_dedicated_console_destroy(frontend->terminal);
    qa_audio_device_close(frontend->device); qa_audio_engine_destroy(frontend->audio);
    qa_scene_frame_destroy(&frontend->frame);
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
bool qa_frontend_run(qa_frontend *frontend, qa_error *error)
{
    if (!frontend) return frontend_fail(error, QA_ERROR_ARGUMENT, "missing frontend");
    interrupted = 0;
    void (*previous_int)(int) = signal(SIGINT, stop_signal);
    void (*previous_term)(int) = signal(SIGTERM, stop_signal);
    uint64_t frequency = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    bool ok = frequency != 0;
    if (!ok) frontend_fail(error, QA_ERROR_IO, "monotonic timer unavailable");
    while (ok && !interrupted && !qa_application_should_stop(frontend->application)) {
        uint64_t now = SDL_GetPerformanceCounter(), ticks = now - last;
        last = now;
        uint64_t seconds = ticks / frequency;
        if (seconds > UINT64_MAX / UINT64_C(1000000000)) { ok = frontend_fail(error, QA_ERROR_ARGUMENT, "monotonic duration overflow"); break; }
        uint64_t elapsed = seconds * UINT64_C(1000000000) +
            (uint64_t)((long double)(ticks % frequency) * 1000000000.0L / (long double)frequency);
        ok = qa_frontend_step(frontend, elapsed, error);
        if (frontend->options.frame_limit && frontend->frame_number >= frontend->options.frame_limit) break;
        SDL_Delay(1);
    }
    if (previous_int != SIG_ERR) signal(SIGINT, previous_int);
    if (previous_term != SIG_ERR) signal(SIGTERM, previous_term);
    return ok;
}
