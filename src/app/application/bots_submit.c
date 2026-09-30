#include "bots_private.h"
#include <math.h>

bool application_bot_submit(void *opaque,qa_actor_id actor,const qa_bot_input *input,
                            const qa_movement_command *source,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    application_bot_seat *seat=NULL;
    for(uint32_t i=0;i<bots->capacity;++i)
        if(!bots->seats[i].retired && qa_actor_id_equal(bots->seats[i].actor,actor)) {seat=&bots->seats[i];break;}
    if(!seat) return true;
    qa_application_control_view view;
    if(!qa_application_control_read(application,actor,&view)) return true;
    if(!qa_vec_finite(input->direction) || !qa_vec_finite(input->view_angles) || !isfinite(input->speed))
        return application_fail(error,QA_ERROR_ARGUMENT,"bot action contains nonfinite movement");
    application_provider *arsenal=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q1 && input->weapon>0 && input->weapon<=QA_Q1_WEAPON_COUNT) {
        qa_q1_player_view current;
        if(!qa_q1_player_read(arsenal->state.q1,actor,&current))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected Q1 weapon state is absent");
        if(current.weapon!=(qa_q1_weapon)(input->weapon-1) &&
           !qa_q1_player_select(arsenal->state.q1,actor,(qa_q1_weapon)(input->weapon-1),error)) return false;
    } else if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q2 && input->weapon>0 && input->weapon<QA_Q2_WEAPON_COUNT) {
        qa_q2_weapon_state current;qa_q2_selection result;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&current,error)) return false;
        if(current.weapon!=(qa_q2_weapon)input->weapon && current.pending!=(qa_q2_weapon)input->weapon &&
           !qa_q2_weapon_select(arsenal->state.q2,actor,(qa_q2_weapon)input->weapon,false,&result,error)) return false;
    }
    if(seat->retired || !qa_actors_get(qa_session_actors(application->session),actor)) return true;
    if(!qa_application_control_read(application,actor,&view)) return true;
    uint64_t now=qa_session_elapsed(application->session);
    if(now<seat->last_command_ns) {seat->last_command_ns=now;return true;}
    uint64_t elapsed=now-seat->last_command_ns;
    if(elapsed<1000000) return true;
    if(view.command_sequence==UINT64_MAX) return application_fail(error,QA_ERROR_ARGUMENT,"bot command sequence exhausted");
    qa_movement_command command=*source;
    command.kind=view.state.kind;command.sequence=view.command_sequence+1;
    command.milliseconds=(uint32_t)(elapsed/1000000>UINT32_MAX?UINT32_MAX:elapsed/1000000);
    command.server_time_ms=(int32_t)(uint32_t)(now/1000000);
    command.weapon=input->weapon>0 && input->weapon<=UINT8_MAX?(uint8_t)input->weapon:0;
    if(view.state.kind!=QA_MOVEMENT_Q3) {
        qa_vec3 forward,right;
        qa_builtin_angle_vectors(qa_v3(input->direction.z!=0?input->view_angles.x:0,
                                     input->view_angles.y,0),&forward,&right,NULL);
        float speed=fmaxf(0,fminf(400,input->speed));
        command.forward_move=qa_vec_dot(input->direction,forward)*speed;
        command.side_move=qa_vec_dot(input->direction,right)*speed;
        command.up_move=input->direction.z*speed;
        command.angles=input->view_angles;command.buttons=0;
        if(input->action_flags&(QA_BOT_ATTACK|QA_BOT_RESPAWN)) command.buttons|=1;
        if(input->action_flags&QA_BOT_USE) command.buttons|=4;
        if(input->action_flags&QA_BOT_MOVE_FORWARD) command.forward_move=400;
        if(input->action_flags&QA_BOT_MOVE_BACK) command.forward_move=-400;
        if(input->action_flags&QA_BOT_MOVE_RIGHT) command.side_move=400;
        if(input->action_flags&QA_BOT_MOVE_LEFT) command.side_move=-400;
        if(input->action_flags&(QA_BOT_MOVE_UP|QA_BOT_JUMP|QA_BOT_DELAYED_JUMP)) command.up_move=400;
        if(input->action_flags&(QA_BOT_MOVE_DOWN|QA_BOT_CROUCH)) command.up_move=-400;
        if(view.state.kind==QA_MOVEMENT_Q2_CLASSIC) {
            command.angle_words[0]=(uint16_t)(application_bot_angle_word(input->view_angles.x)-view.state.data.q2.delta_angle_shorts[0]);
            command.angle_words[1]=(uint16_t)(application_bot_angle_word(input->view_angles.y)-view.state.data.q2.delta_angle_shorts[1]);
            command.angle_words[2]=(uint16_t)(application_bot_angle_word(input->view_angles.z)-view.state.data.q2.delta_angle_shorts[2]);
        } else if(view.state.kind==QA_MOVEMENT_Q2_RERELEASE) {
            command.angles=qa_vec_sub(input->view_angles,view.state.data.q2r.delta_angles);
            if(input->action_flags&(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP)) command.buttons|=8;
            if(input->action_flags&QA_BOT_CROUCH) command.buttons|=16;
            command.up_move=0;
        } else if(input->action_flags&(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP)) command.buttons|=2;
        if(input->action_flags&QA_BOT_WALK) {command.forward_move*=.5f;command.side_move*=.5f;}
    }
    if(!qa_application_control_move(application,actor,&command,error)) return false;
    seat->last_command_ns+=UINT64_C(1000000)*command.milliseconds;
    return true;
}
