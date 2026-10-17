#include "bots_private.h"
#include "control_frame.h"
#include "qa/application_equipment.h"
#include <math.h>

bool application_bot_weapon_apply(qa_application *application,qa_actor_id actor,
    qa_actor_owner owner,qa_item_id item,qa_error *error) {
    if(!item) return true;
    if(!application_control_frame_current(application,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot weapon selection requires its actual command source phase");
    application_provider *arsenal=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    if(!arsenal || arsenal->owner!=owner)
        return application_fail(error,QA_ERROR_NOT_FOUND,"queued bot weapon no longer belongs to the selected arsenal");
    if(!qa_actors_get(qa_session_actors(application->session),actor)) return true;
    qa_item_id current;
    if(!qa_application_weapon_read(application,actor,&current,error)) return false;
    if(current==item) return true;
    if(arsenal->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&state,error)) return false;
        if(qa_q2_weapon_item(arsenal->state.q2,state.pending,false)==item) return true;
    }
    bool accepted;
    return qa_application_equipment_request_weapon(application,actor,owner,item,&accepted,error);
}

static void source_walk_input(const qa_bot_source_skill *settings,qa_usercmd *command) {
    if(!settings || !settings->movement.walk_only) return;
    bool q3=command->kind==QA_RULESET_Q3;
    qa_usercmd velocity;
    qa_input_command_basis physical={.kind=command->kind,.units=q3?320:1};
    qa_input_command_basis wire={.kind=command->kind,.units=q3?127:1};
    qa_input_axis_rule exact={.quantization=QA_INPUT_AXIS_EXACT};
    qa_input_command_convert(command,NULL,&wire,&physical,exact,&velocity);
    float speed=sqrtf(velocity.forward_move*velocity.forward_move+velocity.side_move*velocity.side_move);
    if(speed<=160) return;
    velocity.forward_move*=160/speed;velocity.side_move*=160/speed;
    qa_input_axis_rule quantize={.quantization=command->kind==QA_RULESET_Q2_RERELEASE?
        QA_INPUT_AXIS_EXACT:QA_INPUT_AXIS_TRUNCATE};
    qa_usercmd walked;
    qa_input_command_convert(&velocity,NULL,&physical,&wire,quantize,&walked);
    command->forward_move=walked.forward_move;command->side_move=walked.side_move;
}

static bool idle_lift_input(application_bots *bots,qa_actor_id actor,const qa_player_state *view,
    const qa_bot_view *bot,qa_usercmd *command,qa_error *error) {
    if(bots->behavior==QA_RULESET_Q3 || !bots->population || view->cutscene ||
       view->ground.hit!=QA_TRACE_HIT_ACTOR || command->forward_move!=0 || command->side_move!=0) return true;
    qa_builtin_services services=application_builtin_services(bots->application,
        bots->application->world,bots->application->physics);
    qa_builtin_actor_traits traits;
    if(!services.actor_traits(services.context,actor,&traits) || traits.spectator ||
       (traits.has_life && traits.dead)) return true;
    bool lift=false;qa_nav_entity_state ground;
    if(!application_bot_lift_ground(bots,view->ground.actor,&ground,&lift,error)) return false;
    if(!lift) return true;
    bool riding=false;
    if(bot->paused || bot->decision==QA_BOT_INTERMISSION || bot->decision==QA_BOT_OBSERVER ||
       bot->decision==QA_BOT_RESPAWNING) return true;
    if(!qa_bot_moves_riding(qa_bot_runtime_moves(bots->runtime),bot->movement,
            &ground,&riding,error)) return false;
    if(riding) return true;
    qa_usercmd walk=*command;
    walk.forward_move=bots->walk_speed;walk.side_move=0;
    bool q3=command->kind==QA_RULESET_Q3;
    qa_input_command_basis from={.kind=command->kind,.units=q3?320:1};
    qa_input_command_basis to={.kind=command->kind,.units=q3?127:1};
    qa_input_axis_rule axes={.quantization=command->kind==QA_RULESET_Q2_RERELEASE?
        QA_INPUT_AXIS_EXACT:QA_INPUT_AXIS_TRUNCATE};
    qa_input_command_convert(&walk,NULL,&from,&to,axes,&walk);
    command->forward_move=walk.forward_move;command->side_move=walk.side_move;
    return true;
}

bool application_bot_submit(void *opaque,qa_actor_id actor,const qa_bot_input *input,
                            const qa_usercmd *source,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    if(!bots->producing || bots->round_phase!=APPLICATION_BOT_ROUND_ACTIVE)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot movement requires its active source-stage producer");
    qa_source_frame admitted;uint64_t host_ns;
    if(!qa_session_active_frame(application->session,bots->producer_frame.provider,&admitted) ||
       !qa_session_frame_host_time(application->session,&host_ns) || host_ns!=bots->producer_host_ns ||
       admitted.number!=bots->producer_frame.number || admitted.time_ns!=bots->producer_frame.time_ns)
        return application_fail(error,QA_ERROR_ARGUMENT,"bot movement escaped its admitted source interval");
    application_bot_seat *seat = application_bot_seat_find(bots, actor);
    if(!seat) return true;
    qa_player_state view;
    if(!qa_application_control_read(application,actor,&view)) return true;
    if(!qa_vec_finite(input->direction) || !qa_vec_finite(input->view_angles) || !isfinite(input->speed))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot action contains nonfinite movement");
    application_provider *arsenal=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    qa_item_id requested_item=0;qa_actor_owner requested_owner=0;
    if(arsenal && (arsenal->kind==APPLICATION_PROVIDER_Q1 || arsenal->kind==APPLICATION_PROVIDER_Q2) &&
       !application_bot_weapon_resolve(bots,actor,input->weapon,&requested_item,error)) return false;
    if(requested_item) requested_owner=arsenal->owner;
    if (!application_bot_seat_live(bots, seat)) return true;
    if(!qa_application_control_read(application,actor,&view)) return true;
    bool seen=false;uint64_t sequence=0;
    if(!application_control_frames_sequence(application,actor,&seen,&sequence))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot movement has no actual command admission owner");
    if(seen && sequence==UINT64_MAX) return application_fail(error,QA_ERROR_ARGUMENT,"bot command sequence exhausted");
    qa_usercmd command;
    if (view.state.kind == QA_RULESET_Q3) {
        /* Already built by the common path, including stock paused angles. */
        command = *source;
        command.sequence = seen ? sequence + 1 : 0;
        uint64_t milliseconds = admitted.elapsed_ns / 1000000;
        command.milliseconds = (uint32_t)(milliseconds > UINT32_MAX ? UINT32_MAX : milliseconds);
    } else {
        qa_input_command_intent intent;
        qa_bot_input_intent(input, &intent);
        qa_input_command_frame frame = {.kind = view.state.kind, .sequence = seen ? sequence + 1 : 0,
            .acknowledged_server_seconds = (double)source->server_time_ms / 1000.0,
            .attack_allowed = true};
        if (view.state.kind == QA_RULESET_Q2_CLASSIC)
            for (unsigned i = 0; i < 3; ++i) frame.delta_angle_words[i] = view.state.data.q2.delta_angle_shorts[i];
        else if (view.state.kind == QA_RULESET_Q2_RERELEASE)
            frame.delta_angles = view.state.data.q2r.delta_angles;
        qa_usercmd built;
        qa_usercmd_build(&intent, &frame, (double)admitted.elapsed_ns / 1000000.0, &built);
        command = built;
    }
    qa_bot_view bot;
    if(!qa_bots_read(bots->population,actor,&bot,error)) return false;
    source_walk_input(bot.source_settings,&command);
    if(!idle_lift_input(bots,actor,&view,&bot,&command,error) ||
       !application_control_frames_receive_bot(application,actor,&command,requested_owner,requested_item,error)) return false;
    return true;
}
