#include "q3_color_policy.h"
#include "restart.h"
#include "network_q2_input.h"
#include "network_q1_input.h"
#include "qa/source_frame_time.h"
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
#include "settings_devices.h"
#include "startup_menus.h"
#include "demo_dispatch.h"
#include "remote_q2_client.h"
#include "network_prediction.h"
#include "network_predictor.h"
#include "network_config.h"
#include "network_q3_restart.h"
#include "equipment_events.h"
#include "particle_clock.h"
#include "round.h"
#include "qa/source_frame_time.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_network.h"
#include "qa/text.h"
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
static bool pause_flag(qa_cvars *cvars,uint64_t owner,const char *name,bool paused,qa_error *error)
{
    const qa_cvar_view *value=qa_cvars_find(cvars,name);
    if (!value) {
        if (!qa_cvars_register(cvars,name,"0",QA_CVAR_READONLY,owner,"Q3 pause state",error)) return false;
    } else if (!(value->flags&QA_CVAR_READONLY) &&
        !qa_cvars_add_flags(cvars,name,QA_CVAR_READONLY,error)) return false;
    return qa_cvars_set(cvars,name,paused?"1":"0",true,error);
}
static bool pause_client_current(qa_frontend *f,const qa_application_q3_client_context *client,bool remote,
    qa_error *error)
{
    bool current=remote?frontend_network_q3_client_context_current(f,client):
        qa_application_q3_client_context_current(f->application,client);
    return current || frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 pause lost its actual physical CLIENT custody");
}
static bool menu_paused(qa_frontend *f,bool *out,qa_error *error)
{
    *out=false;
    const qa_launch_snapshot *snapshot = qa_application_launch(f->application);
    if (!snapshot || qa_application_startup_pending(f->application)) return true;
    bool remote=frontend_network_remote(f),menu=false;
    for (uint32_t i=0;i<f->options.seats && !f->options.dedicated;++i)
        menu|=qa_ui_menu_opened(f->seats[i].ui,FRONTEND_HOME);
    qa_application_startup_source source; bool present=false;
    if (!frontend_config_store_primary_server_read(f->config_store,&source,&present,error)) return false;
    bool q3=present && source.scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME && !remote &&
        !frontend_network_client_only(f) && qa_application_get_state(f->application)==QA_APPLICATION_RUNNING;
    qa_application_q3_client_context clients[QA_INPUT_LOCAL_SEATS];
    uint64_t owners[QA_INPUT_LOCAL_SEATS];
    size_t count=0;
    bool requested=menu;
    for (uint32_t i=0;i<f->options.seats && !f->options.dedicated;++i) {
        qa_actor_owner receiver=0; uint32_t seat;
        if (!frontend_seat_launch_id_read(f,i,&seat)) continue;
        if (remote) {
            frontend_remote_config_view configuration; bool installed=false;
            if (!frontend_network_client_configuration_read(f,seat,&configuration,&installed,error)) return false;
            if (!installed) continue;
            receiver=configuration.scope.provider;
        } else {
            if (!q3) continue;
            if (!frontend_source_cgame_recipient(f,i,&receiver,error)) return false;
            if (!receiver) continue;
        }
        qa_application_q3_client_context client;
        qa_application_startup_source configuration;
        if (!(remote?frontend_network_q3_client_context_read(f,receiver,seat,&client,error):
            qa_application_q3_client_context_read(f->application,receiver,seat,&client,error)) ||
            !qa_application_q3_client_configuration_read(f->application,receiver,QA_QVM_CGAME,seat,&configuration,error)) return false;
        if (client.cvars!=configuration.cvars || client.console!=configuration.console ||
            !configuration.declaration_owner || (q3 && (client.source_owner!=source.scope.provider ||
            client.source_cvars!=source.cvars)) || !pause_client_current(f,&client,remote,error))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 pause lost its actual physical CLIENT and primary Source");
        bool opened=qa_ui_menu_opened(f->seats[i].ui,FRONTEND_HOME);
        if (!pause_flag(client.cvars,configuration.declaration_owner,"cl_paused",opened,error) ||
            !pause_client_current(f,&client,remote,error)) return false;
        const qa_cvar_view *value=qa_cvars_find(client.cvars,"cl_paused");
        requested|=value && value->number!=0;
        clients[count]=client; owners[count++]=configuration.declaration_owner;
    }
    if (q3) {
        qa_application_network_q3_host_slot slots[64];
        if (!qa_application_network_q3_host_slots(f->application,source.scope.provider,slots,error)) return false;
        size_t humans=0;
        for (size_t i=0;i<64;++i) humans+=slots[i].occupied && !slots[i].bot;
        *out=requested && humans<=1;
        if (!pause_flag(source.cvars,source.declaration_owner,"sv_paused",*out,error)) return false;
    }
    for (size_t i=0;i<count;++i)
        if (!pause_client_current(f,&clients[i],remote,error) ||
            !pause_flag(clients[i].cvars,owners[i],"sv_paused",*out,error) ||
            !pause_client_current(f,&clients[i],remote,error)) return false;
    if (q3 || remote || f->options.dedicated ||
        frontend_network_save_authority(f)!=QA_SAVE_OFFLINE) return true;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    bool singleplayer = false;
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (choices->modes[i].primary_score) singleplayer = choices->modes[i].rules.kind == QA_MODE_SINGLE_PLAYER;
    *out=singleplayer && menu;
    return true;
}
static bool source_elapsed(qa_frontend *frontend,uint64_t supplied,const qa_cvars **owner,
    uint64_t *out,uint64_t *application_ns,qa_error *error)
{
    const qa_cvars *cvars=NULL;
    bool present=false;
    if(!frontend_network_q1_frame_time(frontend,&cvars,out,&present,error)) return false;
    if(present) { *owner=cvars; *application_ns=supplied; return true; }
    if (!frontend_network_client_time_cvars_read(frontend,&cvars,&present,error)) return false;
    bool remote=present || frontend->options.network_connect!=NULL;
    if (!present) cvars=NULL;
    if (!remote && !qa_application_startup_pending(frontend->application)) {
        const qa_launch_snapshot *publication=qa_application_launch(frontend->application);
        const qa_launch_binding *entities=qa_launch_binding_for(qa_launch_snapshot_choices(publication),
            (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
        const qa_launch_instance *source=entities?qa_launch_snapshot_find(publication,entities->instance):NULL;
        if (source) {
            qa_application_startup_source actual;
            if (!qa_application_startup_source_read(frontend->application,publication,source,
                &actual,error)) return false;
            cvars=actual.cvars;
            bool accepted; uint64_t frame; qa_actor_owner provider=actual.scope.provider;
            if (!qa_session_pending_frame(qa_application_session(frontend->application), provider,
                supplied, &accepted, &frame, out, error)) return false;
            *owner=cvars; *application_ns=supplied;
            return true;
        }
    }
    uint64_t sampled = supplied;
    qa_console_dialect source_dialect = cvars ? qa_cvars_dialect(cvars) : QA_CONSOLE_Q1;
    if (cvars && (source_dialect == QA_CONSOLE_Q2 || source_dialect == QA_CONSOLE_Q2_RERELEASE ||
        source_dialect == QA_CONSOLE_Q3))
        sampled = qa_source_frame_time_host_delta(frontend->wall_time_ns, supplied);
    double milliseconds=(double)sampled/1000000.0;
    if (cvars && sampled && !qa_source_frame_time_sample(cvars,milliseconds,frontend->options.dedicated,!remote,
        &milliseconds,error)) return false;
    double duration=milliseconds*1000000.0;
    if (!isfinite(duration) || duration<0 || duration>=18446744073709551616.0) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source frame duration exceeds the native elapsed range");
        return false;
    }
    *owner=cvars; *out=cvars?(uint64_t)duration:supplied;
    *application_ns=remote?*out:supplied; return true;
}
static bool control_binding(qa_frontend *frontend,frontend_seat *seat,qa_actor_id actor,
    const qa_application_control_view *state,bool remote,qa_error *error)
{
    qa_movement_kind kind = state->profile.kind;
    qa_console_dialect profile = dialect(kind);
    bool changed = !qa_actor_id_equal(seat->actor, actor) || seat->builder.kind != kind;
    if (changed) {
        double now=(double)frontend->wall_time_ns/1000000.0;
        if (!qa_ui_rankings_reset_binding(seat->rankings, error)) return false;
        if (!qa_input_seat_release(seat->input, now, error) || !qa_input_seat_profile(seat->input, profile, error)) return false;
        qa_input_command_clear(&seat->builder); seat->builder.kind = kind; seat->actor = actor;
        if (!remote && seat->sequence < state->command_sequence)
            seat->sequence = state->command_sequence;
        if (!qa_input_command_angles(&seat->builder,
            remote?state->view_angles:state->command_angles,error)) return false;
        seat->command_angle_revision=state->command_angle_revision;
    }
    return true;
}
static bool control_bindings(qa_frontend *frontend,qa_error *error)
{
    if (frontend->options.dedicated || frontend_network_remote(frontend) ||
        qa_application_startup_pending(frontend->application) ||
        qa_application_should_stop(frontend->application)) return true;
    for (uint32_t ordinal=0;ordinal<frontend->options.seats;++ordinal) {
        frontend_seat *seat=frontend->seats+ordinal;
        if (qa_input_seat_context(seat->input).owner ||
            frontend_network_q1_input_owned(frontend,ordinal) ||
            frontend_network_q2_input_owned(frontend,ordinal)) continue;
        qa_actor_id actor; uint32_t launch_seat;
        if (!frontend_seat_launch_id_read(frontend,ordinal,&launch_seat) ||
            !qa_application_player_actor(frontend->application,launch_seat,&actor)) continue;
        qa_application_control_view state;
        if (!qa_application_control_read(frontend->application,actor,&state))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"local player lacks its application control continuation");
        if (!control_binding(frontend,seat,actor,&state,false,error)) return false;
    }
    return true;
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
static bool wheel_sample(frontend_seat *seat,uint64_t time,qa_seat_input_sample *sample,qa_error *error)
{
    qa_hud_wheel_command wheel;
    if (!qa_hud_wheel_update(seat->wheel,time,error) ||
        !qa_hud_wheel_prepare(seat->wheel,sample->buttons[QA_INPUT_ATTACK].active,time,&wheel,error)) return false;
    if (wheel.consume_attack) sample->buttons[QA_INPUT_ATTACK]=(qa_input_action_sample){0};
    if (wheel.holster) sample->buttons[QA_INPUT_HOLSTER].active=true;
    return true;
}
static bool controls(qa_frontend *frontend,uint64_t elapsed_ns,uint64_t wall_elapsed_ns,qa_error *error)
{
    double now=(double)frontend->wall_time_ns/1000000.0;
    double default_duration=(double)elapsed_ns/1000000.0;
    double default_wall_duration=(double)wall_elapsed_ns/1000000.0;
    bool remote=frontend_network_remote(frontend);
    bool client_only=frontend_network_client_only(frontend);
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        seat->client_frame_ns=0;
        if (default_wall_duration<=0) continue;
        double duration=default_duration, wall_duration=default_wall_duration;
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
                    if (!wheel_sample(seat,frontend->time_ns,&sample,error)) return false;
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
            if(q1_owned) {
                bool accepted; uint64_t source_ns,wall_ns;
                if(!frontend_network_q1_input_prepare(frontend,i,&accepted,&source_ns,&wall_ns,error)) return false;
                if(!accepted) continue;
                seat->client_frame_ns=source_ns;
                duration=(double)source_ns/1000000.0; wall_duration=(double)wall_ns/1000000.0;
            }
            if (seat->sequence==UINT64_MAX)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Q2 physical sample sequence overflow");
            qa_seat_input_sample sample;
            if (!qa_input_seat_sample(seat->input,now,wall_duration,&sample,error)) return false;
            qa_actor_id selected_actor; uint32_t selected_seat;
            if (frontend_seat_launch_id_read(frontend,i,&selected_seat) &&
                qa_application_player_actor(frontend->application,selected_seat,&selected_actor)) {
                qa_application_control_view selected_control;
                if (qa_application_control_read(frontend->application,selected_actor,&selected_control)) {
                    if (!wheel_sample(seat,frontend->time_ns,&sample,error)) return false;
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
        if (!control_binding(frontend,seat,actor,&state,remote,error)) return false;
        uint64_t source_duration=elapsed_ns;
        bool admitted_client=false;
        if (!remote) {
            qa_actor_owner source_owner; const qa_cvars *source_cvars;
            qa_clock_config recipe; uint64_t order;
            qa_session *session=qa_application_session(frontend->application);
            if (!qa_application_control_source_read(frontend->application,actor,&source_owner,&source_cvars,error) ||
                !qa_session_component_recipe(session,source_owner,&recipe,&order)) return false;
            uint64_t pending=frontend->wall_time_ns-seat->client_clock_ns;
            if(!recipe.interval_ns) {
                bool accepted;
                if(!qa_source_frame_time_admit(source_cvars,pending,false,&accepted,&source_duration,error)) return false;
                if(!accepted) continue;
            } else {
                uint64_t client_delta=qa_source_frame_time_host_delta(frontend->wall_time_ns,pending);
                double milliseconds=(double)client_delta/1000000.0;
                if(!qa_source_frame_time_sample(source_cvars,milliseconds,false,true,&milliseconds,error)) return false;
                source_duration=(uint64_t)(milliseconds*1000000.0);
            }
            wall_duration=(double)pending/1000000.0;
            admitted_client=true;
            duration=(double)source_duration/1000000.0;
        }
        seat->client_frame_ns=source_duration;
        qa_movement_kind kind = state.profile.kind;
        if (!remote && ((kind!=QA_MOVEMENT_Q3 && kind!=QA_MOVEMENT_Q2_CLASSIC &&
                kind!=QA_MOVEMENT_Q2_RERELEASE) || state.cutscene ||
                seat->command_angle_revision!=state.command_angle_revision)) {
            if (!qa_input_command_angles(&seat->builder, state.command_angles, error)) return false;
            seat->command_angle_revision=state.command_angle_revision;
        }
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
        if (!wheel_sample(seat,frontend->time_ns,&sample,error)) return false;
        uint64_t command_time=frontend->time_ns;
        if (!remote && kind==QA_MOVEMENT_Q3) {
            qa_application_startup_source source; bool present=false; qa_clock_state clock;
            if (!frontend_config_store_primary_server_read(frontend->config_store,&source,&present,error)) return false;
            if (!present || !qa_session_clock(qa_application_session(frontend->application),source.scope.provider,&clock))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Local Q3 input has no actual GAME clock");
            command_time=clock.frame.time_ns;
            command_time+=clock.debt_ns;
        }
        uint32_t server_time_word=(uint32_t)(command_time / UINT64_C(1000000));
        int32_t server_time_ms;
        memcpy(&server_time_ms,&server_time_word,sizeof(server_time_ms));
        qa_input_command_frame frame = {.kind = kind, .sequence = ++seat->sequence,
            .server_time_ms = server_time_ms,
            .sensitivity = 1, .attack_allowed = true, .grounded = state.ground.hit != QA_TRACE_HIT_NONE};
        if (kind==QA_MOVEMENT_Q2_CLASSIC) {
            const int16_t *delta=state.state.data.q2.delta_angle_shorts;
            frame.delta_angles=qa_v3((float)delta[0]*(360.f/65536.f),
                (float)delta[1]*(360.f/65536.f),(float)delta[2]*(360.f/65536.f));
        } else if (kind==QA_MOVEMENT_Q2_RERELEASE)
            frame.delta_angles=state.state.data.q2r.delta_angles;
        if (!remote && kind==QA_MOVEMENT_Q3) {
            bool present;
            if (!qa_application_q3_input_values_read(frontend->application,launch_seat,actor,
                &frame.weapon,&frame.sensitivity,&present,error)) return false;
        }
        qa_movement_command command;
        if (!qa_input_command_build(&seat->builder, &tuning, &sample, &frame, duration, &command, error)) return false;
        if (remote) {
            if (!frontend_network_client_sample(frontend,i,actor,&command,
                FRONTEND_REMOTE_PREDICTION_ABSOLUTE,&sample,duration,error)) return false;
        } else if (!frontend_network_command(frontend,i,actor,&command,error)) return false;
        if (admitted_client) seat->client_clock_ns=frontend->wall_time_ns;
        uint32_t source_slot;
        if (!qa_ui_rankings_set_slot(seat->rankings,
            qa_application_rankings_client_slot(frontend->application, actor, &source_slot) && source_slot <= INT32_MAX ?
                (int32_t)source_slot : -1, error)) return false;
    }
    return true;
}
static bool runtime_console(qa_frontend *frontend, qa_console **console,
    qa_command_context *context, qa_error *error)
{
    *console=qa_application_console(frontend->application);
    *context=(qa_command_context){.origin=QA_COMMAND_LOCAL,.dialect=QA_CONSOLE_Q1,.direct=true};
    qa_application_startup_source source; bool present=false;
    if (!frontend_config_store_primary_server_read(frontend->config_store,&source,&present,error)) return false;
    if (present && (source.scope.kind==QA_APPLICATION_CONSOLE_Q1_GAME ||
        source.scope.kind==QA_APPLICATION_CONSOLE_Q2_GAME ||
        source.scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME ||
        frontend_config_store_parked_current(frontend->config_store,&source))) {
        *console=source.console; *context=source.command;
    }
    return true;
}
bool frontend_platform_drain(qa_frontend *frontend, qa_error *error)
{
    qa_platform_event event; qa_bytes payload;
    while (qa_platform_events_peek(frontend->platform_events,&event,&payload)) {
        bool ok=true,consumed=true;
        switch (event.kind) {
        case QA_PLATFORM_EVENT_TIME:
            if (event.time_ns>frontend->wall_time_ns) frontend->wall_time_ns=event.time_ns;
            break;
        case QA_PLATFORM_EVENT_QUIT:
            qa_application_request_stop(frontend->application);
            break;
        case QA_PLATFORM_EVENT_PACKET:
            if (!frontend_network_receive_ready(frontend)) return true;
            ok=frontend_network_receive(frontend,&event,payload,&consumed,error);
            break;
        case QA_PLATFORM_EVENT_CONSOLE_LINE:
            if (!qa_application_should_stop(frontend->application)) {
                qa_console *console; qa_command_context context;
                ok=runtime_console(frontend,&console,&context,error) &&
                    qa_console_append(console,&context,(const char *)payload.data,error);
            }
            break;
        default:
            if (!frontend->options.dedicated && !qa_application_should_stop(frontend->application)) {
                if (event.kind==QA_PLATFORM_EVENT_WINDOW && event.value2 &&
                    (uint32_t)event.value==frontend->observed_display.window_id)
                    ok=qa_display_info_get(frontend->display,&frontend->observed_display,error);
                if (ok) ok=qa_input_platform_dispatch(frontend->input,&event,payload,NULL,error) &&
                    (qa_application_should_stop(frontend->application) || frontend_ui_features_sync(frontend,error));
            }
            break;
        }
        if(consumed) qa_platform_events_consume(frontend->platform_events);
        if (!ok) return false;
    }
    return true;
}
static bool platform_events(qa_frontend *frontend, qa_error *error)
{
    if (qa_application_should_stop(frontend->application)) return true;
    if (!frontend_ui_features_sync(frontend,error)) return false;
    qa_platform_events_frame(frontend->platform_events,frontend->wall_time_ns);
    qa_input_platform_collect(frontend->input,frontend->platform_events,frontend->wall_time_ns);
    if (frontend->terminal && !qa_platform_console_pump(frontend->terminal,frontend->platform_events,
        0,65536,frontend->wall_time_ns,error)) return false;
    if (!frontend_platform_drain(frontend,error)) return false;
    if (qa_application_should_stop(frontend->application) || frontend->options.dedicated) return true;
    return qa_input_platform_sample(frontend->input,frontend->platform_events,frontend->wall_time_ns,error) &&
        frontend_platform_drain(frontend,error);
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
            bool center = event.kind == QA_BUILTIN_CENTERPRINT;
            uint64_t center_duration = UINT64_C(4000000000);
            if (center) {
                const qa_cvar_view *duration = qa_cvars_find(qa_application_cvars(frontend->application), "scr_centertime");
                if (duration && isfinite(duration->number))
                    center_duration = (uint64_t)(fmax(0, fmin(86400, duration->number)) * 1e9);
            }
            bool printed=false;
            for (unsigned seat = 0; seat < frontend->options.seats && !frontend->options.dedicated; ++seat) {
                qa_actor_id actor; uint32_t launch_seat;
                if (event.actor.registry && (!frontend_seat_launch_id_read(frontend,seat,&launch_seat) ||
                    !qa_application_player_actor(frontend->application, launch_seat, &actor) || !qa_actor_id_equal(actor, event.actor))) continue;
                char localized[1024]; const char *recipient_text;
                if (!frontend_ui_source_message(frontend,seat,&event,localized,&recipient_text,error)) return false;
                bool ok = center ? qa_hud_center_print(frontend->seats[seat].hud,
                    recipient_text, frontend->time_ns, center_duration, true, 0, error) : qa_hud_notify(frontend->seats[seat].hud,
                    recipient_text, false, event.time_ns, UINT64_C(4000000000), error);
                if (!ok || (!center && !qa_seat_console_print(frontend->seats[seat].console, recipient_text, error))) return false;
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
    return frontend_equipment_events_drain(frontend->gear_events,error) &&
        (frontend->round ? frontend_round_clear_events(frontend,error) :
         qa_application_clear_events(frontend->application, error));
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
        qa_vec3 origin;
        if (!frontend_audio_actor_position(frontend, identity.actor, identity.id, &body, &origin, error)) return false;
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
    if (!frontend_startup_launch_complete(frontend,error)) return false;
    if (!qa_application_launch(frontend->application)) {
        if (frontend->options.network_connect)
            return frontend_network_create(frontend,error);
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
        frontend_network_create(frontend,error) && control_bindings(frontend,error);
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

static bool drain_runtime_console(qa_frontend *frontend, qa_console *console,
    bool playing, const char *site, qa_error *error)
{
    size_t executed;
    if (qa_console_drain(console, 4096, &executed, error)) return true;
    if (!playing || qa_application_should_stop(frontend->application) ||
        qa_application_startup_pending(frontend->application) || !qa_console_idle(console)) return false;
    qa_application_feature_report(frontend->application, site, error);
    if (error) *error = (qa_error){0};
    return true;
}

static bool commands(qa_frontend *frontend,bool playing,qa_error *error)
{
    qa_console *engine=qa_application_console(frontend->application),*source;
    qa_command_context context;
    if(!runtime_console(frontend,&source,&context,error)) return false;
    if(source!=engine && !qa_application_startup_console_queued(frontend->application,source) &&
        !drain_runtime_console(frontend,source,playing,"source console command",error)) return false;
    return qa_application_startup_console_queued(frontend->application,engine) ||
        drain_runtime_console(frontend,engine,playing,"ENGINE console command",error);
}

static bool frontend_step(qa_frontend *frontend, uint64_t elapsed_ns, bool *playing, qa_error *error)
{
    if (!frontend || frontend->shutdown || frontend->stepping || frontend->preparing || frontend->round || !frontend_save_commands_idle(frontend) ||
        frontend_save_commands_restoring(frontend) ||
        frontend->frame_number == UINT64_MAX ||
        elapsed_ns > UINT64_MAX - frontend->wall_time_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid frontend frame duration or reentry");
    if (!frontend->capture && !frontend->resource_inventory && !frontend->source_restoring &&
        !frontend->frame.source_pending)
        qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
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
    if (frontend->startup_launch) {
        if (!frontend_startup_launch_drain(frontend,error)) return false;
        if (frontend->startup_launch && !qa_application_startup_pending(frontend->application)) {
            frontend->wall_time_ns += elapsed_ns;
            return true;
        }
    }
    if (frontend_settings_devices_pending(frontend)) {
        bool complete = false;
        frontend->wall_time_ns += elapsed_ns;
        return frontend_settings_devices_drain(frontend, &complete, error);
    }
    if (!frontend_demo_dispatch_execute(frontend->demos,error) ||
        !frontend_demo_dispatch_advance(frontend->demos,elapsed_ns,frontend->frame_number,error)) return false;
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
    qa_profiler *profiler = qa_tools_profiler(frontend_tools_owner(frontend));
    if (!qa_profiler_push(profiler,"source_maintenance",error)) return false;
    bool maintained = true;
    if (maintained) maintained = qa_profiler_push(profiler,"source_color_retirement",error);
    if (maintained) maintained = frontend_profiler_end(profiler,frontend_q3_source_color_publication_finish(frontend,error),error);
    if (maintained) maintained = qa_profiler_push(profiler,"source_music_publication",error);
    if (maintained) maintained = frontend_profiler_end(profiler,frontend_source_publish_music(frontend,error),error);
    if (maintained) maintained = qa_profiler_push(profiler,"view_restore_publication",error);
    if (maintained) maintained = frontend_profiler_end(profiler,frontend_view_bindings_finish_restore(frontend,error),error);
    if (maintained) maintained = qa_profiler_push(profiler,"startup_replay",error);
    if (maintained) maintained = frontend_profiler_end(profiler,frontend_startup_replay(frontend,error),error);
    if (maintained) maintained = qa_profiler_push(profiler,"resource_policy",error);
    if (maintained) maintained = frontend_profiler_end(profiler,frontend_shared_resource_policy_live_sync(frontend,error),error);
    if (!frontend_profiler_end(profiler,maintained,error)) return false;
    bool client_only=frontend_network_client_only(frontend);
    *playing = !frontend_startup_queued(frontend) &&
        !qa_application_startup_pending(frontend->application) &&
        (client_only || qa_application_get_state(frontend->application) == QA_APPLICATION_RUNNING);
    qa_application_travel_view pending;
    bool retiring_map=qa_application_travel_read(frontend->application,&pending) &&
        pending.target.kind==QA_TRAVEL_MAP;
    if (!client_only && !qa_application_should_stop(frontend->application) && !frontend_startup_queued(frontend) &&
        !retiring_map && !qa_application_startup_pending(frontend->application) && frontend_cinematic_capture_ready(frontend) &&
        qa_application_world(frontend->application)) {
        frontend->preparing = true;
        bool prepared = qa_profiler_push(profiler,"frame_preparation",error);
        if (prepared) prepared = frontend_profiler_end(profiler,
            qa_application_prepare_frame(frontend->application, error),error);
        frontend->preparing = false;
        if (!prepared) return false;
    }
    if (!frontend->options.dedicated && !client_only &&
        !qa_application_should_stop(frontend->application) && !retiring_map &&
        !qa_application_startup_pending(frontend->application)) {
        if (!qa_profiler_push(profiler,"selected_bindings",error) ||
            !frontend_profiler_end(profiler,selected_bindings(frontend,error),error)) return false;
    }
    frontend->stepping = true;
    uint64_t raw_elapsed=elapsed_ns;
    if (!wall_advanced) frontend->wall_time_ns+=raw_elapsed;
    bool ok = frontend_tools_pump(frontend, error) &&
        frontend_startup_menus_pump(frontend,error) && platform_events(frontend, error);
    if (ok && qa_application_should_stop(frontend->application)) {
        frontend->stepping=false;
        return true;
    }
    if(ok) ok=commands(frontend,*playing,error);
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
    if (ok) {
        ok=qa_profiler_push(profiler,"network_pump",error);
        if (ok) ok=frontend_profiler_end(profiler,
            frontend_tools_sync(frontend,error) &&
                frontend_network_collect(frontend,frontend->platform_events,error) &&
                frontend_platform_drain(frontend,error) && frontend_network_maintenance(frontend,error),error);
    }
    if (ok) ok=frontend_cinematic_drain(frontend,error);
    if (ok) {
        ok=frontend_restart_drain_frame(frontend->restart,error);
        if (!ok) *playing=false;
    }
    if (ok && frontend->restart && !frontend_restart_idle(frontend->restart)) {
        frontend->stepping=false;
        return true;
    }
    if (ok && !qa_application_startup_pending(frontend->application))
        ok=qa_application_clients_drain(frontend->application,error);
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
            if (console_open) {
                ok=frontend_present(frontend,error);
                if (!ok) *playing=false;
            }
        }
        if (ok && frontend->audio) ok=audio_output(frontend,elapsed_ns,error);
        frontend->stepping=false;
        if (ok) ok=frontend_cinematic_drain(frontend,error);
        if (ok) ++frontend->frame_number;
        return ok;
    }
    uint64_t source_duration=raw_elapsed,adjusted=raw_elapsed,application_duration=raw_elapsed;
    const qa_cvars *time_owner=NULL;
    if (ok) ok=source_elapsed(frontend,raw_elapsed,&time_owner,&source_duration,&application_duration,error) &&
        frontend_tools_capture_clock(frontend,time_owner,source_duration,&adjusted,error);
    bool menu_pause=false;
    if (ok) ok=menu_paused(frontend,&menu_pause,error);
    bool paused=!client_only && (qa_application_q1_paused(frontend->application) || menu_pause);
    if (ok && !paused && adjusted>UINT64_MAX-frontend->time_ns)
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Source frame duration overflow");
    if (ok) { elapsed_ns=paused?0:adjusted; frontend->time_ns+=elapsed_ns; }
    if (ok && frontend->recipient_begin_generation==UINT64_MAX)
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Recipient frame begin generation overflow");
    if (ok) {
        ++frontend->recipient_begin_generation;
        ok=frontend_remote_unified_begin_frame(frontend,frontend->wall_time_ns,raw_elapsed,error);
    }
    if (ok) ok=frontend_particle_source_begin(frontend,elapsed_ns,error);
    retiring_map=qa_application_travel_read(frontend->application,&pending) && pending.target.kind==QA_TRAVEL_MAP;
    if (ok && !qa_application_should_stop(frontend->application)) {
        if (ok && !retiring_map && !qa_application_startup_pending(frontend->application) &&
            (client_only || qa_application_get_state(frontend->application) == QA_APPLICATION_RUNNING)) {
            bool source_ready=false;
            ok = frontend_network_tick(frontend, elapsed_ns, retiring_map, &source_ready, error);
            if (ok && source_ready && !client_only && !paused) {
                ok=qa_profiler_push(profiler, "application", error);
                if (ok) ok=frontend_profiler_end(profiler, qa_application_advance(frontend->application, application_duration, error), error);
            }
        }
        if (ok) {
            ok = qa_profiler_push(profiler, "publication", error);
            if (ok) ok = frontend_profiler_end(profiler,
                frontend_remote_unified_sample(frontend,frontend->wall_time_ns,error) &&
                frontend_remote_q1_sample_all(frontend,frontend->wall_time_ns,error) &&
                frontend_remote_q2_sample(frontend,frontend->wall_time_ns,error) && frontend_network_publish(frontend, error) &&
                frontend_input_profile_bind(frontend,error) && frontend_campaign_drain(frontend,error), error);
        }
        if(ok) ok=frontend_platform_drain(frontend,error) && commands(frontend,*playing,error);
        if(ok && !qa_application_should_stop(frontend->application))
            ok=frontend_network_client_frame(frontend,error);
        retiring_map=qa_application_travel_read(frontend->application,&pending) && pending.target.kind==QA_TRAVEL_MAP;
        if(ok && !qa_application_should_stop(frontend->application) && !retiring_map &&
            !qa_application_startup_pending(frontend->application) && !frontend->options.dedicated) {
            ok=qa_profiler_push(profiler,"controls",error);
            if(ok) ok=frontend_profiler_end(profiler,controls(frontend,elapsed_ns,raw_elapsed,error),error);
        }
        if (ok) {
            ok = qa_profiler_push(profiler, "scene_updates", error);
            if (ok) ok = frontend_profiler_end(profiler,
                (frontend->options.dedicated || (frontend_scene_sync(frontend, error) &&
                    frontend_particle_source_complete(frontend, error) && frontend_map_events(frontend, error))) &&
                (!frontend->qc_messages || frontend_qc_messages_drain(frontend->qc_messages, error)) &&
                (frontend->options.dedicated || (frontend_particle_events(frontend, error) &&
                    frontend_player_events(frontend, error))), error);
        }
        if (ok && !frontend->options.dedicated) {
            ok = qa_profiler_push(profiler, "presentation", error);
            if (ok) {
                bool presented=frontend_present(frontend,error);
                ok=frontend_profiler_end(profiler,presented,error);
                if (!presented) *playing=false;
            }
            if (ok) {
                ok=qa_profiler_push(profiler,"client_pose_publication",error);
                if (ok) ok=frontend_profiler_end(profiler,
                    frontend_network_client_pose_publish(frontend,error),error);
            }
        }
        if (ok && !frontend->options.dedicated) {
            qa_error capture_error = {0};
            if (!frontend_tools_after_present(frontend, &capture_error)) {
                frontend_print(frontend, capture_error.message);
                frontend_print(frontend, "\n");
            }
        }
        if (ok) {
            ok = qa_profiler_push(profiler, "events", error);
            if (ok) ok = frontend_profiler_end(profiler,
                frontend_particle_advance(frontend, error) && frontend_events(frontend, error), error);
        }
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
                     audio_output(frontend, adjusted, error);
                ok = frontend_profiler_end(profiler, ok, error);
            }
        }
    }
    frontend->stepping = false;
    if (ok && !qa_application_should_stop(frontend->application)) {
        ok=qa_profiler_push(profiler,"frame_completion",error);
        if (ok) ok=frontend_profiler_end(profiler,
            qa_application_complete_frame(frontend->application, error),error);
    }
    if (ok) {
        ok=qa_profiler_push(profiler,"source_events",error);
        if (ok) ok=frontend_profiler_end(profiler,
            frontend_source_drain(frontend, error) && frontend_campaign_ui_drain(frontend,error),error);
    }
    if (ok) ++frontend->frame_number;
    if (!ok) return false;
    qa_application_map_view previous, current;
    bool mapped=qa_application_map_read(frontend->application,&previous);
    if (!frontend_travel(frontend,error)) return false;
    return !qa_application_map_read(frontend->application,&current) ||
        (mapped && previous.revision==current.revision) || control_bindings(frontend,error);
}
bool qa_frontend_step(qa_frontend *frontend,uint64_t elapsed_ns,qa_error *error)
{
    qa_error local = {0};
    qa_error *fault = error ? error : &local;
    bool playing = false;
    uint64_t frame = frontend ? frontend->frame_number : 0;
    bool ok=frontend_step(frontend,elapsed_ns,&playing,fault);
    if (ok) return true;
    frontend_save_commands_recovery_abandon(frontend);
    if (!playing || qa_application_should_stop(frontend->application) ||
        qa_application_startup_pending(frontend->application)) return false;
    if (!qa_profiler_idle(qa_tools_profiler(frontend_tools_owner(frontend)))) return false;
    qa_error cleanup = {0};
    if (!frontend_frame_cancel(frontend, &cleanup)) {
        if (error) *error = cleanup;
        return false;
    }
    qa_application_feature_report(frontend->application, "frontend frame", fault);
    if (frontend->frame_number == frame) ++frontend->frame_number;
    *fault = (qa_error){0};
    return true;
}
