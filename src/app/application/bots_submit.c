#include "bots_private.h"
#include "control_frame.h"
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
    if(arsenal->kind==APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view current;
        if(!qa_q1_player_read(arsenal->state.q1,actor,&current))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected Q1 weapon state is absent");
        for(int i=0;i<QA_Q1_WEAPON_COUNT;++i) if(qa_q1_weapon_item(arsenal->state.q1,(qa_q1_weapon)i)==item)
            return current.weapon==(qa_q1_weapon)i ||
                qa_q1_player_select(arsenal->state.q1,actor,(qa_q1_weapon)i,error);
    } else if(arsenal->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state current;qa_q2_selection result;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&current,error)) return false;
        for(int i=1;i<QA_Q2_WEAPON_COUNT;++i) {
            const qa_q2_weapon_definition *definition=qa_q2_weapon_definition_at(arsenal->state.q2,(qa_q2_weapon)i);
            const qa_q2_item_definition *candidate=definition?qa_q2_item_lookup(arsenal->state.q2,definition->item):NULL;
            if(candidate && candidate->item==item)
                return current.weapon==(qa_q2_weapon)i || current.pending==(qa_q2_weapon)i ||
                    qa_q2_weapon_select(arsenal->state.q2,actor,(qa_q2_weapon)i,false,&result,error);
        }
    }
    return application_fail(error,QA_ERROR_NOT_FOUND,"queued bot weapon item is absent from its actual selected arsenal");
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
    application_bot_seat *seat=NULL;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) {seat=&bots->seats[i];break;}
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
    if(seat->retired || !qa_actors_get(qa_session_actors(application->session),actor)) return true;
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
    if(!application_control_frames_receive_bot(application,actor,&command,requested_owner,requested_item,error)) return false;
    return true;
}
