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
                            const qa_movement_command *source,qa_error *error) {
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
    qa_application_control_view view;
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
    qa_movement_command command=*source;
    command.kind=view.state.kind;command.sequence=seen?sequence+1:0;
    uint64_t milliseconds=admitted.elapsed_ns/1000000;
    command.milliseconds=(uint32_t)(milliseconds>UINT32_MAX?UINT32_MAX:milliseconds);
    command.server_time_ms=source->server_time_ms;
    command.weapon=source->weapon;
    if(view.state.kind!=QA_MOVEMENT_Q3) {
        float scale=view.state.kind==QA_MOVEMENT_NETQUAKE || view.state.kind==QA_MOVEMENT_QUAKEWORLD?320.0f:400.0f;
        command.forward_move=(float)((double)source->forward_move*(double)scale/127.0);
        command.side_move=(float)((double)source->side_move*(double)scale/127.0);
        command.up_move=(float)((double)source->up_move*(double)scale/127.0);
        command.angles=qa_v3((float)((double)source->angle_words[0]*360.0/65536.0),
                            (float)((double)source->angle_words[1]*360.0/65536.0),
                            (float)((double)source->angle_words[2]*360.0/65536.0));
        command.buttons=source->buttons&1;command.impulse=0;command.light_level=0;
        if(view.state.kind==QA_MOVEMENT_Q2_CLASSIC) {
            for(size_t i=0;i<3;++i) command.angle_words[i]=source->angle_words[i];
        } else if(view.state.kind==QA_MOVEMENT_Q2_RERELEASE) {
            command.buttons|=source->up_move>0?8:source->up_move<0?16:0;
            command.up_move=0;command.server_frame=0;
        } else if(view.state.kind==QA_MOVEMENT_NETQUAKE) {
            command.acknowledged_server_seconds=(double)source->server_time_ms/1000.0;
            if(source->up_move>0) command.buttons|=2;
        }
    }
    if(!application_control_frames_receive_bot(application,actor,&command,requested_owner,requested_item,error)) return false;
    return true;
}
