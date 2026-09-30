#include "internal.h"
#include <stdio.h>

static qa_console_dialect dialect(qa_movement_kind kind)
{
    switch (kind) {
    case QA_MOVEMENT_NETQUAKE: return QA_CONSOLE_Q1;
    case QA_MOVEMENT_QUAKEWORLD: return QA_CONSOLE_QW;
    case QA_MOVEMENT_Q2_CLASSIC: return QA_CONSOLE_Q2;
    case QA_MOVEMENT_Q2_RERELEASE: return QA_CONSOLE_Q2_RERELEASE;
    case QA_MOVEMENT_Q3: return QA_CONSOLE_Q3;
    }
    return QA_CONSOLE_Q1;
}
static bool controls(qa_frontend *frontend, uint64_t elapsed_ns, qa_error *error)
{
    double now = (double)frontend->time_ns / 1000000.0;
    double duration = fmin((double)elapsed_ns / 1000000.0, 250.0);
    if (duration <= 0) return true;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        qa_actor_id actor;
        if (!qa_application_player_actor(frontend->application, i, &actor)) continue;
        qa_application_control_view state;
        if (!qa_application_control_read(frontend->application, actor, &state))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "local player lacks its application control continuation");
        qa_movement_kind kind = state.profile.kind;
        qa_console_dialect profile = dialect(kind);
        bool changed = !qa_actor_id_equal(seat->actor, actor) || seat->builder.kind != kind;
        if (changed) {
            if (!qa_input_seat_release(seat->input, now, error) || !qa_input_seat_profile(seat->input, profile, error)) return false;
            qa_input_command_clear(&seat->builder); seat->builder.kind = kind; seat->actor = actor;
        }
        /* Read the authority each frame, including forced view angles after
         * teleport/cutscene. Builder angles are not another player store. */
        if (!frontend_network_remote(frontend) &&
            !qa_input_command_angles(&seat->builder, state.command_angles, error)) return false;
        qa_seat_input_sample sample;
        qa_input_command_tuning tuning;
        if (!qa_input_seat_sample(seat->input, now, duration, &sample, error) ||
            !qa_input_settings_read(qa_application_cvars(frontend->application), kind, &tuning, error)) return false;
        qa_hud_wheel_command wheel;
        if (!qa_hud_wheel_update(seat->wheel, frontend->time_ns, error) ||
            !qa_hud_wheel_prepare(seat->wheel, sample.buttons[QA_INPUT_ATTACK].active,
                frontend->time_ns, &wheel, error)) return false;
        if (wheel.consume_attack) sample.buttons[QA_INPUT_ATTACK] = (qa_input_action_sample){0};
        if (wheel.holster) sample.buttons[QA_INPUT_HOLSTER].active = true;
        qa_input_command_frame frame = {.kind = kind, .sequence = ++seat->sequence,
            .server_time_ms = (int32_t)((frontend->time_ns / 1000000) & INT32_MAX),
            .sensitivity = 1, .attack_allowed = true, .grounded = state.ground.hit != QA_TRACE_HIT_NONE};
        qa_movement_command command;
        if (!frontend_network_client_input(frontend, &seat->builder, &frame, error)) return false;
        if (!qa_input_command_build(&seat->builder, &tuning, &sample, &frame, duration, &command, error) ||
            !frontend_network_command(frontend, i, actor, &command, error)) return false;
        const qa_actor_record *record = qa_actors_get(qa_world_actors(qa_application_world(frontend->application)), actor);
        if (!qa_ui_rankings_set_slot(seat->rankings, record && record->has_source && record->source_slot <= INT32_MAX ? (int32_t)record->source_slot : -1, error)) return false;
    }
    return true;
}
static bool input_events(qa_frontend *frontend, qa_error *error)
{
    SDL_Event event;
    double now = (double)frontend->time_ns / 1000000.0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) { qa_application_request_stop(frontend->application); continue; }
        if (frontend->options.dedicated) continue;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKQUOTE) {
            qa_display_info display;
            if (!qa_display_info_get(frontend->display, &display, error)) return false;
            if (event.key.windowID == display.window_id &&
                !qa_seat_console_toggle(frontend->seats[0].console, true, event.key.repeat != 0, error)) return false;
            continue;
        }
        bool handled;
        if (!qa_input_platform_event(frontend->input, &event, now, &handled, error)) return false;
    }
    return frontend->options.dedicated || qa_input_platform_frame(frontend->input, now, error);
}
bool frontend_events(qa_frontend *frontend, qa_error *error)
{
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "event queue changed during consumption");
        const char *text = qa_strings_cstr(strings, event.text);
        if ((event.kind == QA_BUILTIN_MESSAGE || event.kind == QA_BUILTIN_CENTERPRINT) && text) {
            for (unsigned seat = 0; seat < frontend->options.seats && !frontend->options.dedicated; ++seat) {
                qa_actor_id actor;
                if (event.actor.registry && (!qa_application_player_actor(frontend->application, seat, &actor) || !qa_actor_id_equal(actor, event.actor))) continue;
                bool ok = event.kind == QA_BUILTIN_CENTERPRINT ? qa_hud_center_print(frontend->seats[seat].hud,
                    text, event.time_ns, UINT64_C(4000000000), true, 0, error) : qa_hud_notify(frontend->seats[seat].hud,
                    text, false, event.time_ns, UINT64_C(4000000000), error);
                if (!ok || !qa_seat_console_print(frontend->seats[seat].console, text, error)) return false;
            }
            fputs(text, stdout);
        }
        if (!frontend_event_sound(frontend, &event, error)) return false;
    }
    /* Network, demos and tools also consume application events. Their owner
     * must drain its projections before this shared queue is released. */
    return qa_application_clear_events(frontend->application, error);
}
static bool phase_end(qa_profiler *profiler, bool ok, qa_error *error)
{
    qa_error cleanup = {0};
    bool retired = qa_profiler_pop(profiler, &cleanup);
    if (ok && !retired && error) *error = cleanup;
    return ok && retired;
}
static bool audio_output(qa_frontend *frontend, uint64_t elapsed_ns, qa_error *error)
{
    if (frontend->device) {
        size_t mixed;
        return qa_audio_device_pump_auto(frontend->device, frontend->audio, 0, &mixed, error);
    }
    uint64_t duration = elapsed_ns < UINT64_C(250000000) ? elapsed_ns : UINT64_C(250000000);
    uint64_t samples = duration * 48000 + frontend->silent_audio_remainder;
    size_t frames = (size_t)(samples / UINT64_C(1000000000));
    frontend->silent_audio_remainder = samples % UINT64_C(1000000000);
    int16_t discarded[2048];
    while (frames) {
        size_t count = frames < 1024 ? frames : 1024;
        if (!qa_audio_engine_mix(frontend->audio, discarded, count, error)) return false;
        frames -= count;
    }
    return true;
}
static bool audio_positions(qa_frontend *frontend, qa_error *error)
{
    qa_world *world = qa_application_world(frontend->application);
    for (size_t i = 0; i < frontend->audio_id_count; ++i) {
        frontend_audio_identity identity = frontend->audio_ids[i];
        if (!qa_actors_get(qa_world_actors(world), identity.actor)) continue;
        if (frontend_network_client_actor(frontend, identity.actor)) continue;
        qa_body_state body; qa_error observed = {0};
        if (!qa_world_body_read(world, identity.actor, &body, &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = observed;
            return false;
        }
        if (!qa_audio_engine_position(frontend->audio, identity.id, body.origin, error)) return false;
    }
    return true;
}
bool qa_frontend_step(qa_frontend *frontend, uint64_t elapsed_ns, qa_error *error)
{
    if (!frontend || frontend->stepping || elapsed_ns > UINT64_MAX - frontend->time_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend frame duration or reentry");
    frontend->stepping = true;
    uint64_t adjusted;
    bool ok = frontend_tools_capture_clock(frontend, elapsed_ns, &adjusted, error);
    if (ok && adjusted > UINT64_MAX - frontend->time_ns) ok = frontend_fail(error, QA_ERROR_ARGUMENT, "capture duration overflow");
    if (ok) { elapsed_ns = adjusted; frontend->time_ns += elapsed_ns; }
    if (ok) ok = frontend_tools_pump(frontend, error) && input_events(frontend, error);
    qa_console *console = qa_application_console(frontend->application);
    if (ok && frontend->terminal) {
        size_t lines;
        qa_command_context context = {.origin = QA_COMMAND_LOCAL, .dialect = QA_CONSOLE_Q1, .direct = true};
        ok = qa_dedicated_console_poll(frontend->terminal, 0, 65536, error) &&
             qa_dedicated_console_drain(frontend->terminal, console, &context, &lines, error);
    }
    size_t executed;
    if (ok) ok = qa_console_drain(console, 4096, &executed, error) &&
        frontend_tools_sync(frontend, error) && frontend_network_pump(frontend, error);
    qa_profiler *profiler = qa_tools_profiler(frontend_tools_owner(frontend));
    if (ok && !qa_application_should_stop(frontend->application)) {
        if (!frontend->options.dedicated) {
            ok = qa_profiler_push(profiler, "controls", error);
            if (ok) ok = phase_end(profiler, controls(frontend, elapsed_ns, error), error);
        }
        if (ok && qa_application_get_state(frontend->application) == QA_APPLICATION_RUNNING && !frontend_network_remote(frontend)) {
            ok = qa_profiler_push(profiler, "application", error);
            if (ok) ok = phase_end(profiler, qa_application_advance(frontend->application, elapsed_ns, error), error);
        }
        if (ok) ok = frontend_network_publish(frontend, error);
        if (ok && !frontend->options.dedicated) ok = frontend_scene_sync(frontend, error) &&
            frontend_map_events(frontend, error) && frontend_particle_events(frontend, error) && frontend_player_events(frontend, error);
        if (ok && !frontend->options.dedicated) {
            ok = qa_profiler_push(profiler, "presentation", error);
            if (ok) ok = phase_end(profiler, frontend_present(frontend, error), error);
        }
        if (ok && !frontend->options.dedicated) {
            qa_error capture_error = {0};
            if (!frontend_tools_after_present(frontend, &capture_error)) {
                frontend_print(frontend, capture_error.message);
                frontend_print(frontend, "\n");
            }
        }
        if (ok) ok = frontend_particle_advance(frontend, error);
        if (ok) ok = frontend_events(frontend, error);
        if (ok && frontend->audio) {
            ok = qa_profiler_push(profiler, "audio", error);
            if (ok) {
                const qa_cvar_view *volume = qa_cvars_find(qa_application_cvars(frontend->application), "s_volume");
                qa_audio_engine_gain(frontend->audio, volume ? fmaxf(0, fminf(1, volume->number)) : .7f);
                qa_audio_engine_update(frontend->audio, (double)frontend->time_ns / 1000000);
                ok = audio_positions(frontend, error) && frontend_event_audio(frontend, error) && qa_audio_engine_end_loop_frame(frontend->audio, error) &&
                     audio_output(frontend, elapsed_ns, error);
                ok = phase_end(profiler, ok, error);
            }
        }
    }
    if (ok) ++frontend->frame_number;
    frontend->stepping = false;
    return ok && frontend_travel(frontend, error);
}
