#include "q3_color_policy.h"
#include "restart.h"
#include "network_q2_input.h"
#include "network_q1_input.h"
#include "qc_messages.h"
#include "remote_q1_client.h"
#include "remote_unified.h"
#include "remote_unified_input.h"
#include "internal.h"
#include "capture.h"
#include "save_commands.h"
#include "campaign.h"
#include "campaign_ui.h"
#include "campaign_cinematic.h"
#include "ui_features.h"
#include "input_profile.h"
#include "config_store.h"
#include "shared_settings.h"
#include "shared_resource_policy.h"
#include "music_sources.h"
#include "source_acoustics.h"
#include "view_bindings.h"
#include "constructor.h"
#include "remote_q2_client.h"
#include "network_prediction.h"
#include "network_predictor.h"
#include "network_config.h"
#include "equipment_events.h"
#include "particle_clock.h"
#include "round.h"
#include "qa/source_frame_time.h"
#include "qa/application_startup_prepare.h"
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
static bool source_elapsed(qa_frontend *frontend,uint64_t supplied,const qa_cvars **owner,
    uint64_t *out,qa_error *error)
{
    const qa_cvars *cvars=NULL;
    bool present=false;
    if (!frontend_network_client_time_cvars_read(frontend,&cvars,&present,error)) return false;
    bool remote=present || frontend->options.network_connect!=NULL;
    if (!present) cvars=NULL;
    if (!remote && !qa_application_startup_pending(frontend->application)) {
        const qa_launch_snapshot *publication=qa_application_launch(frontend->application);
        const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(publication),
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        const qa_launch_instance *source=entities?qa_launch_snapshot_find(publication,entities->instance):NULL;
        if (source) {
            qa_console *console; qa_cvars *actual; qa_command_context command;
            if (!qa_application_startup_source_read(frontend->application,publication,source,
                &console,&actual,&command,error)) return false;
            cvars=actual;
        }
    }
    double milliseconds=(double)supplied/1000000.0;
    if (cvars && !qa_source_frame_time_sample(cvars,milliseconds,frontend->options.dedicated,!remote,
        &milliseconds,error)) return false;
    double duration=milliseconds*1000000.0;
    if (!isfinite(duration) || duration<0 || duration>=18446744073709551616.0) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source frame duration exceeds the native elapsed range");
        return false;
    }
    *owner=cvars; *out=cvars?(uint64_t)duration:supplied; return true;
}
static bool selected_bindings(qa_frontend *frontend,qa_error *error)
{
    qa_inventory *inventory=qa_application_inventory(frontend->application);
    qa_strings *strings=qa_session_strings(qa_application_session(frontend->application));
    for (uint32_t ordinal=0;ordinal<frontend->options.seats;++ordinal) {
        uint32_t launch_seat; qa_actor_id actor;
        if (!frontend_seat_launch_id_read(frontend,ordinal,&launch_seat) ||
            !qa_application_player_actor(frontend->application,launch_seat,&actor)) continue;
        size_t count;
        if (!qa_inventory_item_definitions(inventory,actor,NULL,0,&count,error)) return false;
        qa_item_definition local[64],*items=local;
        if (count>sizeof(local)/sizeof(*local)) {
            if (count>SIZE_MAX/sizeof(*items))
                return frontend_fail(error,QA_ERROR_MEMORY,"Selected binding catalog storage overflow");
            items=malloc(count*sizeof(*items));
            if (!items) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating selected binding catalog");
        }
        bool ok=qa_inventory_item_definitions(inventory,actor,items,count,&count,error) &&
            frontend_config_store_select_bindings(frontend->config_store,launch_seat,strings,items,count,
                qa_input_platform_controller(frontend->input,ordinal),error);
        if (items!=local) free(items);
        if (!ok) return false;
    }
    return true;
}
static bool controls(qa_frontend *frontend,uint64_t elapsed_ns,uint64_t wall_elapsed_ns,qa_error *error)
{
    double now=(double)frontend->wall_time_ns/1000000.0;
    double duration=(double)elapsed_ns/1000000.0;
    double wall_duration=(double)wall_elapsed_ns/1000000.0;
    if (wall_duration<=0) return true;
    bool remote=frontend_network_remote(frontend);
    bool client_only=frontend_network_client_only(frontend);
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        bool unified_owned=false,sample_needed=false;
        if (!frontend_remote_unified_input_prepare(frontend,i,&seat->sequence,&unified_owned,&sample_needed,error)) return false;
        if (unified_owned) {
            if (!sample_needed) continue;
            if (seat->sequence==UINT64_MAX)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified physical sample sequence overflow");
            qa_seat_input_sample sample;
            if (!qa_input_seat_sample(seat->input,now,wall_duration,&sample,error)) return false;
            qa_actor_id selected_actor; uint32_t selected_seat;
            if (frontend_seat_launch_id_read(frontend,i,&selected_seat) &&
                qa_application_player_actor(frontend->application,selected_seat,&selected_actor)) {
                qa_application_control_view selected_control;
                if (qa_application_control_read(frontend->application,selected_actor,&selected_control)) {
                    qa_hud_wheel_command wheel;
                    if (!qa_hud_wheel_update(seat->wheel,frontend->time_ns,error) ||
                        !qa_hud_wheel_prepare(seat->wheel,sample.buttons[QA_INPUT_ATTACK].active,
                            frontend->time_ns,&wheel,error)) return false;
                    if (wheel.consume_attack) sample.buttons[QA_INPUT_ATTACK]=(qa_input_action_sample){0};
                    if (wheel.holster) sample.buttons[QA_INPUT_HOLSTER].active=true;
                }
            }
            bool handled=false; uint64_t sequence=seat->sequence+1;
            if (!frontend_remote_unified_input(frontend,i,&sample,sequence,duration,&handled,error)) return false;
            if (!handled) return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified sample lost its actual replica recipient");
            seat->sequence=sequence;
            continue;
        }
        bool q2_owned=false,q1_owned=false;
        bool q2_input_retained=frontend_network_q2_input_owned(frontend,i);
        bool q1_input_retained=frontend_network_q1_input_owned(frontend,i);
        for (size_t row=0;row<frontend_remote_q1_count(frontend);++row) {
            frontend_remote_q1_view view;
            if (!frontend_remote_q1_metadata_read(frontend_remote_q1_at(frontend,row),&view,error)) return false;
            if (!view.retired && view.domain.physical_seat==i) {
                if (q1_owned) return frontend_fail(error,QA_ERROR_ARGUMENT,"Two Q1 CLIENT receivers own one physical input");
                q1_owned=true;
            }
        }
        for (size_t row=0;row<frontend_remote_q2_count(frontend);++row) {
            frontend_remote_q2_view view;
            if (!frontend_remote_q2_metadata_read(frontend_remote_q2_at(frontend,row),&view,error)) return false;
            if (!view.retired && view.domain.physical_seat==i) {
                if (q2_owned) return frontend_fail(error,QA_ERROR_ARGUMENT,"Two Q2 CLIENT receivers own one physical input");
                q2_owned=true;
            }
        }
        if ((q2_input_retained && !q2_owned) || (q1_input_retained && !q1_owned)) continue;
        if (q1_owned && q2_owned)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Two remote protocols own the same physical input");
        if (q1_owned || q2_owned) {
            if (seat->sequence==UINT64_MAX)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 physical sample sequence overflow");
            qa_seat_input_sample sample;
            if (!qa_input_seat_sample(seat->input,now,wall_duration,&sample,error)) return false;
            qa_actor_id selected_actor; uint32_t selected_seat;
            if (frontend_seat_launch_id_read(frontend,i,&selected_seat) &&
                qa_application_player_actor(frontend->application,selected_seat,&selected_actor)) {
                qa_application_control_view selected_control;
                if (qa_application_control_read(frontend->application,selected_actor,&selected_control)) {
                    qa_hud_wheel_command wheel;
                    if (!qa_hud_wheel_update(seat->wheel,frontend->time_ns,error) ||
                        !qa_hud_wheel_prepare(seat->wheel,sample.buttons[QA_INPUT_ATTACK].active,
                            frontend->time_ns,&wheel,error)) return false;
                    if (wheel.consume_attack) sample.buttons[QA_INPUT_ATTACK]=(qa_input_action_sample){0};
                    if (wheel.holster) sample.buttons[QA_INPUT_HOLSTER].active=true;
                }
            }
            bool handled=false;
            uint64_t sequence=seat->sequence+1;
            if (!(q1_owned?frontend_network_q1_input(frontend,i,&sample,sequence,duration,&handled,error):
                frontend_network_q2_input(frontend,i,&sample,sequence,&handled,error))) return false;
            if (!handled) return frontend_fail(error,QA_ERROR_ARGUMENT,"Physical sample lost its genuine remote CLIENT owner");
            seat->sequence=sequence;
            continue;
        }
        if (client_only && !remote) continue;
        qa_actor_id actor; uint32_t launch_seat;
        if (!frontend_seat_launch_id_read(frontend,i,&launch_seat) ||
            !qa_application_player_actor(frontend->application, launch_seat, &actor)) continue;
        qa_application_control_view state;
        if (!qa_application_control_read(frontend->application, actor, &state))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "local player lacks its application control continuation");
        qa_movement_kind kind = state.profile.kind;
        qa_console_dialect profile = dialect(kind);
        bool changed = !qa_actor_id_equal(seat->actor, actor) || seat->builder.kind != kind;
        if (changed) {
            if (!qa_ui_rankings_reset_binding(seat->rankings, error)) return false;
            if (!qa_input_seat_release(seat->input, now, error) || !qa_input_seat_profile(seat->input, profile, error)) return false;
            qa_input_command_clear(&seat->builder); seat->builder.kind = kind; seat->actor = actor;
            if (remote && !qa_input_command_angles(&seat->builder,state.view_angles,error)) return false;
        }
        /* Local forced angles come from the current player. Remote selected
         * angles continue independently of the raw Q3 transport builder. */
        if (!remote &&
            !qa_input_command_angles(&seat->builder, state.command_angles, error)) return false;
        qa_seat_input_sample sample;
        qa_input_command_tuning tuning;
        qa_movement_kind configured_kind;
        qa_cvars *input_settings, *view_settings;
        if (remote) {
            frontend_remote_config_view configuration;
            if (!frontend_network_client_configuration(frontend,launch_seat,&configuration,error)) return false;
            input_settings=configuration.q3_mouse; view_settings=configuration.movement_mouse;
            configured_kind=configuration.movement;
        } else {
            input_settings=frontend_config_store_primary_mouse_cvars(frontend->config_store,launch_seat,&configured_kind);
            view_settings=input_settings;
        }
        if (!input_settings || !view_settings || configured_kind!=kind)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Player input lacks its actual published source settings and movement profile");
        if (!qa_input_seat_sample(seat->input,now,wall_duration,&sample,error) ||
            !qa_input_settings_read_routed(input_settings, view_settings, kind, &tuning, error)) return false;
        if (!remote && qa_application_q1_paused(frontend->application)) continue;
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
        if (!qa_input_command_build(&seat->builder, &tuning, &sample, &frame, duration, &command, error)) return false;
        if (remote) {
            if (!frontend_network_client_sample(frontend,i,actor,&command,
                FRONTEND_REMOTE_PREDICTION_ABSOLUTE,&sample,duration,error)) return false;
        } else if (!frontend_network_command(frontend,i,actor,&command,error)) return false;
        uint32_t source_slot;
        if (!qa_ui_rankings_set_slot(seat->rankings,
            qa_application_rankings_client_slot(frontend->application, actor, &source_slot) && source_slot <= INT32_MAX ?
                (int32_t)source_slot : -1, error)) return false;
    }
    return true;
}
static bool input_events(qa_frontend *frontend, qa_error *error)
{
    if (qa_application_should_stop(frontend->application)) return true;
    if (!frontend_ui_features_sync(frontend,error)) return false;
    SDL_Event event;
    double now = (double)frontend->wall_time_ns / 1000000.0;
    while (!qa_application_should_stop(frontend->application) && SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) { qa_application_request_stop(frontend->application); return true; }
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
        if (qa_application_should_stop(frontend->application)) return true;
        if (!frontend_ui_features_sync(frontend,error)) return false;
    }
    if (qa_application_should_stop(frontend->application) || frontend->options.dedicated) return true;
    if (!qa_input_platform_frame(frontend->input, now, error)) return false;
    return qa_application_should_stop(frontend->application) || frontend_ui_features_sync(frontend,error);
}
bool frontend_events(qa_frontend *frontend, qa_error *error)
{
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (size_t i = 0; i < qa_application_event_count(frontend->application); ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frontend->application, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "event queue changed during consumption");
        if (event.kind == QA_BUILTIN_LOG) continue;
        const char *text = qa_strings_cstr(strings, event.text);
        if ((event.kind == QA_BUILTIN_MESSAGE || event.kind == QA_BUILTIN_CENTERPRINT) && text) {
            bool printed=false;
            for (unsigned seat = 0; seat < frontend->options.seats && !frontend->options.dedicated; ++seat) {
                qa_actor_id actor; uint32_t launch_seat;
                if (event.actor.registry && (!frontend_seat_launch_id_read(frontend,seat,&launch_seat) ||
                    !qa_application_player_actor(frontend->application, launch_seat, &actor) || !qa_actor_id_equal(actor, event.actor))) continue;
                char localized[1024]; const char *recipient_text;
                if (!frontend_ui_source_message(frontend,seat,&event,localized,&recipient_text,error)) return false;
                bool ok = event.kind == QA_BUILTIN_CENTERPRINT ? qa_hud_center_print(frontend->seats[seat].hud,
                    recipient_text, event.time_ns, UINT64_C(4000000000), true, 0, error) : qa_hud_notify(frontend->seats[seat].hud,
                    recipient_text, false, event.time_ns, UINT64_C(4000000000), error);
                if (!ok || !qa_seat_console_print(frontend->seats[seat].console, recipient_text, error)) return false;
                if (!printed) { fputs(recipient_text,stdout); printed=true; }
            }
            if (!printed && frontend->options.dedicated) {
                char localized[1024]; const char *recipient_text;
                if (!event.actor.registry) {
                    if (!frontend_ui_source_message(frontend,0,&event,localized,&recipient_text,error)) return false;
                    fputs(recipient_text,stdout);
                } else fputs(text,stdout);
            }
        }
        if (!frontend_event_sound(frontend, &event, error)) return false;
    }
    /* Network, demos and tools also consume application events. Their owner
     * must drain its projections before this shared queue is released. */
    return (!frontend->qc_messages || frontend_qc_messages_drain(frontend->qc_messages,error)) &&
        frontend_equipment_events_drain(frontend->gear_events,error) &&
        (frontend->round ? frontend_round_clear_events(frontend,error) :
         qa_application_clear_events(frontend->application, error));
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
        if (identity.retired) continue;
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
static bool resource_wait(const qa_frontend *frontend)
{
    const qa_launch_snapshot *candidate=qa_application_startup_candidate(frontend->application);
    return frontend_config_store_images_pending(frontend->config_store) ||
        (frontend_config_store_shared_pending(frontend->config_store) &&
        qa_application_startup_resource_phase(frontend->application,candidate));
}
static bool resource_returned(qa_frontend *frontend,qa_error *error)
{
    const qa_launch_snapshot *candidate=qa_application_startup_candidate(frontend->application);
    if (!resource_wait(frontend) ||
        !frontend_config_store_shared(frontend->config_store,frontend->application,candidate) ||
        !frontend_owners_returned(frontend) || !frontend_seat_callbacks_returned(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Candidate settings lost their returned preparation phase");
    return !frontend->input_settings ||
        frontend_input_settings_shutdown_ready(frontend->input_settings,frontend,error);
}
bool frontend_startup_advance(qa_frontend *frontend,bool *complete,qa_error *error)
{
    if (frontend->music_sources) {
        size_t queued;
        if (!frontend_music_sources_queued(frontend->music_sources,&queued) ||
            (queued && !frontend_music_sources_flush(frontend->music_sources,error))) return false;
    }
    if (qa_application_launch(frontend->application) &&
        (qa_application_event_count(frontend->application) ||
         qa_application_protocol_event_count(frontend->application) ||
         qa_application_equipment_event_count(frontend->application)) &&
        !frontend_events_flush(frontend,error)) return false;
    frontend->preparing=true;
    bool prepared=qa_application_startup_advance(frontend->application,complete,error);
    frontend->preparing=false;
    if (!prepared || !*complete) return prepared;
    if (!qa_application_launch(frontend->application)) {
        if (frontend->options.game) {
            if (!frontend_launch(frontend,error)) return false;
            *complete=!qa_application_startup_pending(frontend->application);
            if (!*complete) return true;
        } else {
            if (!frontend->options.network_host && !frontend_network_create(frontend,error)) return false;
            return frontend->options.dedicated || frontend_game_menu(&frontend->seats[0],error);
        }
    }
    if (!frontend_campaign_sync(frontend,error) || !frontend_view_bindings_apply_restored(frontend,error)) return false;
    if (*complete && frontend->music_sources &&
        (!frontend_music_sources_world(frontend->music_sources,error) ||
         !frontend_source_publish_music(frontend,error) ||
         !frontend_music_sources_output(frontend->music_sources,FRONTEND_MUSIC_WORLD,error))) return false;
    uint64_t travel_revision;
    return (qa_application_travel_publication_read(frontend->application,&travel_revision) ||
        qa_application_rankings_start(frontend->application,error)) &&
        frontend_network_create(frontend,error);
}
static bool stop_server(qa_frontend *f, bool *complete, qa_error *error)
{
    *complete=true;
    if (!f->server_stop_owner) return true;
    if (f->server_stopped) {
        if (qa_application_startup_pending(f->application)) return true;
        if (!frontend_config_store_parked_finish(f->config_store,error)) return false;
        if (!qa_application_server_restart_pending(f->application)) {
            f->server_stop_owner=0; f->server_stop_generation=0;
            f->server_stopped=false; f->server_stop_follow_map=false;
        }
        return true;
    }
    qa_application_startup_source source; bool present=false;
    if (f->stepping || f->preparing || f->capture || f->resource_inventory || f->source_restoring ||
        !frontend_owners_idle(f) || !frontend_seat_callbacks_idle(f) ||
        qa_application_startup_pending(f->application) ||
        qa_application_configuration_generation(f->application)!=f->server_stop_generation ||
        !frontend_config_store_primary_server_read(f->config_store,&source,&present,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 server shutdown requires its returned Source owners");
    if (!present || source.scope.provider!=f->server_stop_owner ||
        (source.scope.kind!=QA_APPLICATION_CONSOLE_Q2_GAME &&
         source.scope.kind!=QA_APPLICATION_CONSOLE_NATIVE_Q2) || !qa_console_idle(source.console))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 server shutdown lost its actual primary invocation parent");
    if (qa_console_pending(source.console)) { *complete=false; return true; }
    qa_application_travel_view travel;
    bool traveling=qa_application_travel_read(f->application,&travel);
    bool direct_map=f->server_stop_follow_map && traveling && travel.target.kind==QA_TRAVEL_MAP;
    if (!direct_map && !frontend_config_store_park_server(f->config_store,&source,error)) return false;
    if (!frontend_network_stop_server(f,complete,error)) return false;
    if (!*complete) return true;
    if (direct_map) {
        if (travel.provider!=f->server_stop_owner ||
            !frontend_events(f,error) || !frontend_travel(f,error)) return false;
        if (!qa_application_startup_pending(f->application) &&
            qa_application_configuration_generation(f->application)==f->server_stop_generation) {
            *complete=false;
            return true;
        }
        f->server_stop_owner=0; f->server_stop_generation=0;
        f->server_stop_follow_map=false;
        return true;
    }
    if (!frontend_events(f,error) ||
        !qa_application_stop_server(f->application,f->server_stop_owner,error)) return false;
    f->server_stopped=true;
    if (!frontend_config_store_parked_finish(f->config_store,error)) return false;
    if (traveling && (travel.target.kind==QA_TRAVEL_CINEMATIC || travel.target.kind==QA_TRAVEL_PICTURE) &&
        !frontend_cinematic_travel(f,&travel,error)) return false;
    if (!traveling) {
        if (!f->options.dedicated && !frontend_menu_open(&f->seats[0],FRONTEND_LIBRARY,error)) return false;
    }
    return true;
}

bool qa_frontend_step(qa_frontend *frontend, uint64_t elapsed_ns, qa_error *error)
{
    if (!frontend || frontend->shutdown || frontend->stepping || frontend->preparing || frontend->round || !frontend_save_commands_idle(frontend) ||
        frontend_save_commands_restoring(frontend) ||
        frontend->frame_number == UINT64_MAX ||
        elapsed_ns > UINT64_MAX - frontend->wall_time_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend frame duration or reentry");
    if (!frontend_shared_resource_policy_live_retire(frontend,error) ||
        !frontend_player_sources_drain(frontend,error)) return false;
    if (frontend->server_stopped) {
        bool complete;
        if (!stop_server(frontend,&complete,error)) return false;
    }
    if (frontend_constructor_pending(frontend)) {
        bool complete=false;
        return frontend_constructor_advance(frontend,elapsed_ns,&complete,error);
    }
    if (!frontend_network_client_attempts_advance(frontend,error)) return false;
    qa_application_client_preparation *client=frontend_config_store_client_preparation(frontend->config_store);
    if (client) {
        if (!qa_application_client_prepare_associated(frontend->application,client) ||
            !qa_application_client_prepare_current(client) || frontend->capture || frontend->resource_inventory ||
            frontend->source_restoring || !frontend_seat_callbacks_returned(frontend))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT settings lost their returned physical preparation");
        frontend->wall_time_ns+=elapsed_ns;
        bool complete=false;
        return frontend_network_client_configuration_advance(frontend,client,&complete,error);
    }
    if (frontend->restart && !frontend_restart_idle(frontend->restart)) {
        if (frontend->capture || frontend->resource_inventory || frontend->source_restoring ||
            !frontend_seat_callbacks_returned(frontend))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Video continuation retains an entered frontend callback");
        frontend->wall_time_ns+=elapsed_ns;
        return frontend_restart_drain(frontend->restart,error);
    }
    bool waiting=resource_wait(frontend),wall_advanced=false;
    if (waiting) {
        if (!resource_returned(frontend,error)) return false;
    } else if (!frontend_owners_idle(frontend) || !frontend_seat_callbacks_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend frame owners have not returned idle");
    if (qa_application_startup_pending(frontend->application)) {
        frontend->wall_time_ns+=elapsed_ns; wall_advanced=true;
        bool complete=false;
        if (!frontend_startup_advance(frontend,&complete,error)) return false;
        if (!complete && resource_wait(frontend)) return resource_returned(frontend,error);
        if (!frontend_owners_idle(frontend) || !frontend_seat_callbacks_idle(frontend))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Candidate settings have not completed their physical release");
        if (!complete) return true;
    }
    if (!frontend_q3_source_color_publication_finish(frontend,error) ||
        !frontend_source_publish_music(frontend,error) ||
        !frontend_view_bindings_finish_restore(frontend,error) ||
        !frontend_startup_replay(frontend,error) ||
        !frontend_shared_resource_policy_live_sync(frontend,error)) return false;
    bool client_only=frontend_network_client_only(frontend);
    qa_application_travel_view pending;
    bool retiring_map=qa_application_travel_read(frontend->application,&pending) &&
        pending.target.kind==QA_TRAVEL_MAP;
    if (!client_only && !qa_application_should_stop(frontend->application) && !frontend_startup_queued(frontend) &&
        !retiring_map && !qa_application_startup_pending(frontend->application) && frontend_cinematic_capture_ready(frontend) &&
        qa_application_world(frontend->application)) {
        frontend->preparing = true;
        bool prepared = qa_application_prepare_frame(frontend->application, error);
        frontend->preparing = false;
        if (!prepared) return false;
    }
    if (!frontend->options.dedicated && !client_only &&
        !qa_application_should_stop(frontend->application) && !retiring_map &&
        !qa_application_startup_pending(frontend->application) && !selected_bindings(frontend,error)) return false;
    frontend->stepping = true;
    uint64_t raw_elapsed=elapsed_ns;
    if (!wall_advanced) frontend->wall_time_ns+=raw_elapsed;
    bool ok = frontend_tools_pump(frontend, error) && input_events(frontend, error);
    if (ok && qa_application_should_stop(frontend->application)) {
        frontend->stepping=false;
        return true;
    }
    qa_console *console = qa_application_console(frontend->application);
    qa_console *source_console=console;
    qa_command_context source_context={.origin=QA_COMMAND_LOCAL,.dialect=QA_CONSOLE_Q1,.direct=true};
    if (ok) {
        qa_application_startup_source source; bool present=false;
        ok=frontend_config_store_primary_server_read(frontend->config_store,&source,&present,error);
        if (ok && present && (source.scope.kind==QA_APPLICATION_CONSOLE_Q1_GAME ||
            source.scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
            source.scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME ||
            frontend_config_store_parked_current(frontend->config_store,&source))) {
            source_console=source.console; source_context=source.command;
        }
    }
    if (ok && frontend->terminal) {
        size_t lines;
        if (ok) ok = qa_dedicated_console_poll(frontend->terminal, 0, 65536, error) &&
             qa_dedicated_console_drain(frontend->terminal, source_console, &source_context, &lines, error);
    }
    size_t executed;
    if (ok && source_console!=console &&
        !qa_application_startup_console_queued(frontend->application,source_console))
        ok=qa_console_drain(source_console,4096,&executed,error);
    if (ok) ok = qa_application_startup_console_queued(frontend->application,console) ||
        qa_console_drain(console, 4096, &executed, error);
    if (ok && !qa_application_should_stop(frontend->application) &&
        frontend->server_stop_owner && !frontend->server_stopped) {
        bool complete=false;
        frontend->stepping=false;
        if (!stop_server(frontend,&complete,error)) return false;
        ++frontend->frame_number;
        return !complete || frontend_travel(frontend,error);
    }
    if (ok && qa_application_should_stop(frontend->application)) {
        frontend->stepping=false;
        return true;
    }
    if (ok) ok=frontend_tools_sync(frontend,error) && frontend_network_pump(frontend,error);
    if (ok) ok=frontend_cinematic_drain(frontend,error);
    if (ok) ok=frontend_restart_drain_frame(frontend->restart,error);
    if (ok && frontend->restart && !frontend_restart_idle(frontend->restart)) {
        frontend->stepping=false;
        return true;
    }
    if (ok && !qa_application_should_stop(frontend->application) && frontend_cinematic_running(frontend)) {
        bool rendered=false;
        /* Playback owns its separate media clock. A console fallback may
         * still draw the frozen GAME scene under this actual host frame. */
        ok=frontend_particle_source_begin(frontend,0,error) &&
            frontend_particle_source_complete(frontend,error) &&
            frontend_cinematic_frame(frontend,elapsed_ns,&rendered,error);
        if (ok && !rendered) {
            bool console_open=false;
            for (uint32_t i=0;i<frontend->options.seats;++i)
                console_open|=qa_input_seat_focus(frontend->seats[i].input)==QA_INPUT_CONSOLE;
            if (console_open) ok=frontend_present(frontend,error);
        }
        if (ok && frontend->audio) ok=audio_output(frontend,elapsed_ns,error);
        frontend->stepping=false;
        if (ok) ok=frontend_cinematic_drain(frontend,error);
        if (ok) ++frontend->frame_number;
        return ok;
    }
    uint64_t source_duration,adjusted;
    const qa_cvars *time_owner;
    if (ok) ok=source_elapsed(frontend,raw_elapsed,&time_owner,&source_duration,error) &&
        frontend_tools_capture_clock(frontend,time_owner,source_duration,&adjusted,error);
    bool paused=!client_only && qa_application_q1_paused(frontend->application);
    if (ok && !paused && adjusted>UINT64_MAX-frontend->time_ns)
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Source frame duration overflow");
    if (ok) { elapsed_ns=adjusted; if (!paused) frontend->time_ns+=elapsed_ns; }
    if (ok && frontend->recipient_begin_generation==UINT64_MAX)
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Recipient frame begin generation overflow");
    if (ok) {
        ++frontend->recipient_begin_generation;
        ok=frontend_remote_unified_begin_frame(frontend,frontend->wall_time_ns,raw_elapsed,error);
    }
    if (ok) ok=frontend_particle_source_begin(frontend,elapsed_ns,error);
    if (ok) ok=frontend_network_client_frame(frontend,error);
    retiring_map=qa_application_travel_read(frontend->application,&pending) && pending.target.kind==QA_TRAVEL_MAP;
    qa_profiler *profiler = qa_tools_profiler(frontend_tools_owner(frontend));
    if (ok && !qa_application_should_stop(frontend->application)) {
        if (!retiring_map && !qa_application_startup_pending(frontend->application) && !frontend->options.dedicated) {
            ok = qa_profiler_push(profiler, "controls", error);
            if (ok) ok = phase_end(profiler, controls(frontend,elapsed_ns,raw_elapsed,error), error);
        }
        if (ok && !retiring_map && !qa_application_startup_pending(frontend->application) &&
            (client_only || qa_application_get_state(frontend->application) == QA_APPLICATION_RUNNING)) {
            bool source_ready=false;
            ok = frontend_network_tick(frontend, elapsed_ns, retiring_map, &source_ready, error);
            if (ok && source_ready && !client_only) {
                ok=qa_profiler_push(profiler, "application", error);
                if (ok) ok=phase_end(profiler, qa_application_advance(frontend->application, elapsed_ns, error), error);
            }
        }
        if (ok) ok = frontend_remote_unified_sample(frontend,frontend->wall_time_ns,error) &&
            frontend_remote_q1_sample_all(frontend,frontend->wall_time_ns,error) &&
            frontend_remote_q2_sample(frontend,frontend->wall_time_ns,error) && frontend_network_publish(frontend, error) && frontend_source_times_sync(frontend, false, error) &&
            frontend_input_profile_bind(frontend,error) && frontend_campaign_drain(frontend,error);
        if (ok && !frontend->options.dedicated) ok = frontend_scene_sync(frontend, error) &&
            frontend_particle_source_complete(frontend, error) &&
            frontend_map_events(frontend, error) && frontend_particle_events(frontend, error) && frontend_player_events(frontend, error);
        if (ok && !frontend->options.dedicated) {
            ok = qa_profiler_push(profiler, "presentation", error);
            if (ok) ok = phase_end(profiler, frontend_present(frontend, error), error);
            if (ok) ok=frontend_network_client_pose_publish(frontend,error);
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
                ok=frontend_acoustics_source_sync(frontend,error);
                if (ok) ok=frontend_music_sources_update(frontend->music_sources,error);
                if (ok) qa_audio_engine_update(frontend->audio, (double)frontend->time_ns / 1000000);
                if (ok) ok = audio_positions(frontend, error) && frontend_event_audio(frontend, error) &&
                     qa_audio_engine_q3_publish(frontend->audio, error) && qa_audio_engine_end_loop_frame(frontend->audio, error) &&
                     audio_output(frontend, elapsed_ns, error);
                ok = phase_end(profiler, ok, error);
            }
        }
    }
    frontend->stepping = false;
    if (ok && !qa_application_should_stop(frontend->application))
        ok = qa_application_complete_frame(frontend->application, error);
    if (ok) ok = frontend_source_drain(frontend, error) && frontend_campaign_ui_drain(frontend,error);
    if (ok) ++frontend->frame_number;
    return ok && frontend_travel(frontend, error);
}
