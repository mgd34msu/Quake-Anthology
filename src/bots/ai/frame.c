#include "internal.h"
#include "qa/binary.h"
#include "source_report.h"
#include "source_command.h"
#include "source_inventory.h"
#include "source_alias.h"
#include "source_player.h"
#include "source_view.h"
#include "source_storage.h"
#include "source_timers.h"
#include "source_behavior_state.h"
#include "source_selectors.h"
#include "source_flags.h"
#include "qa/collision_bits.h"

bool bot_ai_source_intermission(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    if(!b->services.source_intermission)
        return bot_ai_fail(e,"bot intermission requires its actual source clock and retained player state");
    if(!b->services.source_intermission(b->services.context,out,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(!*out) {
        int32_t type;
        if(!bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&type,e)) return false;
        *out=type==4 || type==5;
    }
    return true;
}
bool bot_ai_source_observer(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    int32_t type;
    if(!bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&type,e)) return false;
    if(type==2) {*out=true;return true;}
    int32_t client,team;
    if(!bot_ai_source_client(b,s,&client,e) || !bot_ai_source_team(b,client,&team,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    *out=team==3;return true;
}
static bool ready(qa_bots *b, bot_ai_state *s) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return false;
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
    const qa_nav_graph_view *graph = navigation ? qa_navigation_graph(qa_bot_navigation_runtime(navigation)) : NULL;
    return graph && graph->node_count &&
        qa_actor_id_equal(qa_bot_navigation_actor(navigation), s->view.actor);
}
static bool player(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if (!bot_ai_live(b, s->view.actor)) { s->retired = true; return true; }
    int32_t held=bot_ai_firing_weapon_number(s);
    uint64_t spawn=s->player.spawn_sequence,teleport=s->player.teleport_sequence;
    qa_q3_player source;
    qa_bot_player observed=bot_ai_player_sample(s);
    bool ok=b->services.player(b->services.context,s->view.actor,&observed,&source,e);
    bot_ai_player_observe(s,&observed);
    if(!ok) return false;
    if(observed.dead || observed.spawn_sequence!=spawn) {
        s->hazard=(bot_hazard_memory){0};
        s->interaction=(bot_interaction_state){0};
    }
    if(observed.dead || observed.spawn_sequence!=spawn || observed.teleport_sequence!=teleport)
        s->progress=(bot_progress_memory){0};
    if (!bot_ai_live(b,s->view.actor)) {s->retired=true;return true;}
    if(!bot_ai_source_player_copy(b,s,&source,e)) return false;
    if(s->source_settings && held!=source.weapon) bot_ai_trigger_clear(s);
    if (!bot_ai_carrying(b,s,&s->player.carrying_objective,e)) return false;
    if (!bot_ai_live(b, s->view.actor)) s->retired = true;
    return true;
}
static bool connected(qa_bots *b, bot_ai_state *s) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return false;
    qa_builtin_player_info info;
    return b->services.shared.player_info(b->services.shared.context, s->view.actor, &info) &&
        bot_ai_live(b, s->view.actor) && info.connected;
}
static bool submit(qa_bots *b, bot_ai_state *s, const qa_bot_input *input,
                     qa_usercmd *command, qa_error *e) {
    if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
    if (s->command_sequence == UINT64_MAX) return bot_ai_fail(e, "bot command sequence exhausted");
    command->sequence = ++s->command_sequence;
    return b->services.submit(b->services.context, s->view.actor, input, command, e);
}
static bool delta_angles(qa_bots *b,bot_ai_state *s,int32_t out[3],qa_error *e) {
    for(int32_t i=0;i<3;++i)
        if(!bot_ai_source_player_slot(b,s,BOT_PS_DELTA_ANGLES,i,out+i,e)) return false;
    return true;
}
static bool floor_probe(qa_bot_navigation *navigation,qa_actor_id actor,qa_vec3 point,
    qa_trace_result *trace,bool *hazard,qa_error *e) {
    qa_vec3 end=point;end.z-=1024;
    if(!qa_bot_navigation_trace(navigation,point,end,NULL,actor,1,trace,e)) return false;
    point=trace->end;point.z+=2;
    int32_t contents;
    if(!qa_bot_navigation_contents(navigation,point,&contents,e)) return false;
    *hazard=((uint32_t)contents&((1u<<QA_CONTENT_LAVA)|(1u<<QA_CONTENT_SLIME)))!=0;
    return true;
}
static bool floor_route(qa_bots *b,bot_ai_state *s,qa_bot_navigation *navigation,
    qa_vec3 origin,qa_vec3 direction,float *hazard_at,bool *planned,qa_error *e) {
    *planned=false;
    qa_bot_move_state move;
    if(!qa_bot_moves_capture(qa_bot_runtime_moves(b->runtime),s->movement,&move,e)) return false;
    if(move.reachability_time<=b->time || !move.last_reachability) return true;
    const qa_nav_edge *edge=qa_bot_navigation_reachability(navigation,move.last_reachability);
    if(!edge) return true;
    if(edge->mode==QA_NAV_WALK || edge->mode==QA_NAV_CROUCH) {
        float along=qa_vec_dot(qa_vec_sub(edge->end,origin),direction);
        if(along>16 && along<*hazard_at) *hazard_at=0;
    }
    switch(edge->mode) {
    case QA_NAV_JUMP: case QA_NAV_WATER_JUMP: case QA_NAV_JUMP_PAD:
    case QA_NAV_ROCKET_JUMP: case QA_NAV_BFG_JUMP: case QA_NAV_GRAPPLE:
    case QA_NAV_DOUBLE_JUMP: case QA_NAV_RAMP_JUMP: case QA_NAV_STRAFE_JUMP:
    case QA_NAV_LADDER: case QA_NAV_TELEPORT: case QA_NAV_MOVER:
        *planned=true;break;
    default: break;
    }
    return true;
}
static bool floor_jump(qa_bot_navigation *navigation,qa_actor_id actor,const qa_body_state *body,
    qa_vec3 direction,float speed,qa_bot_movement_prediction *out,qa_error *e) {
    qa_bot_movement_prediction_query query={.pass_actor=actor,.origin=body->origin,
        .velocity=body->velocity,.command_move=qa_vec_scale(direction,speed),.presence=2,
        .on_ground=true,.command_frames=30,.maximum_frames=30,.frame_time=.1f,
        .stop_events=1|4|8|16|32};
    query.origin.z+=.5f;
    query.command_move.z=400;
    return qa_bot_navigation_predict_movement(navigation,&query,out,e);
}
static bool floor_landing(qa_bot_navigation *navigation,qa_actor_id actor,qa_vec3 origin,
    qa_vec3 direction,float minimum,const qa_bot_movement_prediction *prediction,
    bool *crossable,qa_error *e) {
    *crossable=false;
    if(!(prediction->stop_event&1) || (prediction->stop_event&(8|16|32)) ||
        qa_vec_dot(qa_vec_sub(prediction->end,origin),direction)<minimum) return true;
    qa_trace_result trace;bool hazard;
    if(!floor_probe(navigation,actor,prediction->end,&trace,&hazard,e)) return false;
    *crossable=!trace.start_solid && !trace.all_solid && trace.fraction<1 && !hazard;
    return true;
}
static qa_vec3 floor_command_direction(const qa_bot_input *input,const qa_usercmd *command) {
    qa_vec3 forward,right;
    qa_angle_vectors(qa_v3(0,input->view_angles.y,0),&forward,&right,NULL,QA_ANGLE_FLOAT_MATH);
    return qa_vec_ma(qa_vec_scale(forward,command->forward_move),command->side_move,right);
}
static void floor_command(const qa_bot_input *input,qa_usercmd *command,qa_vec3 *direction,float *speed) {
    qa_input_command_intent intent;
    qa_bot_input_intent(input,&intent);
    qa_input_command_frame frame={.kind=QA_RULESET_Q3,.attack_allowed=true};
    qa_usercmd_build(&intent,&frame,0,command);
    *direction=floor_command_direction(input,command);
    *speed=fminf(1,qa_vec_length(*direction)/qa_input_command_units(QA_RULESET_Q3))*400;
    *direction=qa_vec_normalize(*direction);
}
static void progress_input(qa_bots *b,bot_ai_state *s,qa_bot_input *input,bool grounded) {
    if(!s->source_settings || !s->progress.pursuing || s->progress.recover_until<=b->time) return;
    qa_usercmd command;qa_vec3 direction;float speed;
    floor_command(input,&command,&direction,&speed);
    float units=qa_input_command_units(QA_RULESET_Q3);
    command.forward_move*=.5f;
    command.side_move=bot_ai_flag(s,BOT_AI_AVOID_RIGHT)?-units:units;
    input->direction=qa_vec_scale(floor_command_direction(input,&command),1/units);
    input->speed=400;
    input->action_flags&=~(uint32_t)BOT_FLOOR_HORIZONTAL;
    if(grounded) input->action_flags|=QA_BOT_JUMP;
    s->hazard.floor.recovering=true;
    s->hazard.floor.recovery_direction=input->direction;
    s->hazard.floor.recovery_speed=input->speed;
    s->hazard.floor.recovery_actions=input->action_flags&BOT_FLOOR_HORIZONTAL;
}
static bool floor_evaluate(qa_bots *b,bot_ai_state *s,qa_bot_navigation *navigation,
    const qa_body_state *body,qa_vec3 direction,float speed,bool jumping,bool crouching,
    bool recovering,bot_floor_action *action,qa_error *e) {
    *action=BOT_FLOOR_CLEAR;
    float horizontal_speed=sqrtf(body->velocity.x*body->velocity.x+body->velocity.y*body->velocity.y);
    float ahead=32+horizontal_speed*.15f,hazard_at=0;
    qa_trace_result trace;
    bool hazard;
    if(speed>0) for(float distance=16;distance<=ahead;distance+=16) {
        if(!floor_probe(navigation,s->view.actor,qa_vec_ma(body->origin,distance,direction),
            &trace,&hazard,e)) return false;
        if(hazard) {hazard_at=distance;break;}
    }
    bool planned=false;
    if((hazard_at>0 || jumping) && !floor_route(b,s,navigation,body->origin,direction,
        &hazard_at,&planned,e)) return false;
    qa_bot_movement_prediction prediction;
    bool predicted=false;
    if(!planned && hazard_at>0 && recovering) {*action=BOT_FLOOR_STOP;return true;}
    if(!planned && hazard_at>0) {
        /* A landing must support the scaled body, rather than one narrow rail. */
        float front=fabsf(direction.x)*fmaxf(fabsf(body->bounds.mins.x),fabsf(body->bounds.maxs.x))+
            fabsf(direction.y)*fmaxf(fabsf(body->bounds.mins.y),fabsf(body->bounds.maxs.y));
        float landing_start=0,landing_z=0;
        bool landing=false;
        for(float distance=hazard_at+16;distance<=hazard_at+320;distance+=16) {
            if(!floor_probe(navigation,s->view.actor,qa_vec_ma(body->origin,distance,direction),
                &trace,&hazard,e)) return false;
            if(trace.start_solid || trace.all_solid) break;
            if(trace.fraction>=1 || hazard) {landing_start=0;continue;}
            if(!landing_start || fabsf(trace.end.z-landing_z)>8) {
                landing_start=distance;landing_z=trace.end.z;continue;
            }
            if(distance-landing_start>=fmaxf(16,front)) {landing=true;break;}
        }
        bool crossable=false,run_up=false;
        if(landing && !crouching) {
            if(!floor_jump(navigation,s->view.actor,body,direction,speed,&prediction,e)) return false;
            predicted=true;
            if(!floor_landing(navigation,s->view.actor,body->origin,direction,landing_start+front,
                &prediction,&crossable,e)) return false;
            if(!crossable) {
                /* Accelerate on actual ground before asking a stationary bot to jump. */
                qa_bot_movement_prediction_query approach={.pass_actor=s->view.actor,
                    .origin=body->origin,.velocity=body->velocity,.presence=2,.on_ground=true,
                    .command_move=qa_vec_scale(direction,speed),.command_frames=5,
                    .maximum_frames=5,.frame_time=.016f,.stop_events=2|4|8|16|32};
                approach.origin.z+=.5f;
                qa_bot_movement_prediction walking;
                if(!qa_bot_navigation_predict_movement(navigation,&approach,&walking,e)) return false;
                if(!walking.stop_event && walking.frames==5) {
                    qa_body_state rim=*body;rim.origin=walking.end;rim.velocity=walking.velocity;
                    if(!floor_jump(navigation,s->view.actor,&rim,direction,speed,&prediction,e) ||
                        !floor_landing(navigation,s->view.actor,body->origin,direction,
                            landing_start+front,&prediction,&crossable,e)) return false;
                    run_up=crossable;
                }
            }
        }
        if(!crossable) *action=BOT_FLOOR_STOP;
        else if(run_up) *action=BOT_FLOOR_NO_JUMP;
        else if(hazard_at<=32) *action=BOT_FLOOR_JUMP;
    }
    if(!planned && jumping && !crouching && *action==BOT_FLOOR_CLEAR) {
        if(!predicted && !floor_jump(navigation,s->view.actor,body,direction,speed,&prediction,e)) return false;
        qa_vec3 travel=qa_vec_sub(prediction.end,body->origin);travel.z=0;
        float distance=qa_vec_length(travel);
        qa_vec3 along=qa_vec_normalize(travel);
        if(prediction.stop_event&(8|16|32)) *action=BOT_FLOOR_NO_JUMP;
        for(float step=16;*action==BOT_FLOOR_CLEAR && step<=distance;step+=16) {
            if(!floor_probe(navigation,s->view.actor,qa_vec_ma(body->origin,step,along),
                &trace,&hazard,e)) return false;
            if(hazard) *action=BOT_FLOOR_NO_JUMP;
        }
    }
    return true;
}
static bool floor_input(qa_bots *b,bot_ai_state *s,qa_bot_navigation *navigation,
    const qa_body_state *body,const qa_movement_ground *ground,qa_bot_input *input,qa_error *e) {
    qa_usercmd command;qa_vec3 direction;float speed;
    floor_command(input,&command,&direction,&speed);
    bool jumping=(input->action_flags&(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP))!=0;
    bool crouching=(input->action_flags&QA_BOT_CROUCH)!=0;
    if(speed==0 && !jumping) {s->hazard.floor.valid=false;return true;}
    bool recovering=s->hazard.floor.recovering &&
        qa_vec_equal(input->direction,s->hazard.floor.recovery_direction) &&
        input->speed==s->hazard.floor.recovery_speed &&
        (input->action_flags&BOT_FLOOR_HORIZONTAL)==s->hazard.floor.recovery_actions;
    qa_vec3 displacement=qa_vec_sub(body->origin,s->hazard.floor.origin);
    qa_vec3 velocity=qa_vec_sub(body->velocity,s->hazard.floor.velocity);
    bool same=s->hazard.floor.valid && s->hazard.floor.navigation==navigation &&
        s->hazard.floor.settings==s->source_settings && s->hazard.floor.jumping==jumping &&
        s->hazard.floor.crouching==crouching && s->hazard.floor.checked_recovery==recovering &&
        qa_bounds_equal(s->hazard.floor.bounds,body->bounds) &&
        s->hazard.floor.ground.hit==ground->hit && s->hazard.floor.ground.model==ground->model &&
        qa_actor_id_equal(s->hazard.floor.ground.actor,ground->actor) &&
        qa_vec_dot(displacement,displacement)<64 && qa_vec_dot(velocity,velocity)<1024 &&
        fabsf(speed-s->hazard.floor.speed)<16 &&
        (speed==0 ? qa_vec_dot(s->hazard.floor.direction,s->hazard.floor.direction)==0 :
            qa_vec_dot(direction,s->hazard.floor.direction)>.98f);
    if(!same) {
        s->hazard.floor.valid=false;s->hazard.floor.replaced=false;
        if(!floor_evaluate(b,s,navigation,body,direction,speed,jumping,crouching,recovering,
            &s->hazard.floor.action,e)) return false;
        if(recovering && s->hazard.floor.action==BOT_FLOOR_STOP) {
            if(!s->hazard.floor.side_flipped) {
                bot_ai_flag_toggle(s,BOT_AI_AVOID_RIGHT);s->hazard.floor.side_flipped=true;
            }
            qa_bot_input candidate=*input;
            for(unsigned attempt=0;attempt<2;++attempt) {
                if(!attempt) {
                    if(command.side_move==0) continue;
                    command.side_move=-command.side_move;
                } else {
                    command.forward_move=-qa_input_command_units(QA_RULESET_Q3);
                    command.side_move=0;
                    candidate.action_flags&=~(uint32_t)(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP);
                }
                candidate.direction=qa_vec_scale(floor_command_direction(input,&command),
                    1/qa_input_command_units(QA_RULESET_Q3));
                candidate.direction.z=input->direction.z;candidate.speed=400;
                candidate.action_flags&=~(uint32_t)BOT_FLOOR_HORIZONTAL;
                qa_usercmd checked;qa_vec3 alternate;float alternate_speed;
                floor_command(&candidate,&checked,&alternate,&alternate_speed);
                bool hop=(candidate.action_flags&(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP))!=0;
                if(!floor_evaluate(b,s,navigation,body,alternate,alternate_speed,hop,crouching,true,
                    &s->hazard.floor.action,e)) return false;
                if(s->hazard.floor.action!=BOT_FLOOR_STOP) {
                    s->hazard.floor.replaced=true;
                    s->hazard.floor.replacement_direction=candidate.direction;
                    s->hazard.floor.replacement_speed=candidate.speed;
                    s->hazard.floor.replacement_actions=candidate.action_flags&
                        (BOT_FLOOR_HORIZONTAL|QA_BOT_JUMP|QA_BOT_DELAYED_JUMP);
                    break;
                }
            }
        }
        s->hazard.floor.origin=body->origin;s->hazard.floor.direction=direction;
        s->hazard.floor.velocity=body->velocity;s->hazard.floor.bounds=body->bounds;
        s->hazard.floor.ground=*ground;s->hazard.floor.settings=s->source_settings;
        s->hazard.floor.navigation=navigation;s->hazard.floor.speed=speed;
        s->hazard.floor.jumping=jumping;s->hazard.floor.crouching=crouching;
        s->hazard.floor.checked_recovery=recovering;s->hazard.floor.valid=true;
    }
    if(s->hazard.floor.replaced) {
        input->direction=s->hazard.floor.replacement_direction;
        input->speed=s->hazard.floor.replacement_speed;
        input->action_flags=(input->action_flags&
            ~(uint32_t)(BOT_FLOOR_HORIZONTAL|QA_BOT_JUMP|QA_BOT_DELAYED_JUMP))|
            s->hazard.floor.replacement_actions;
    }
    switch(s->hazard.floor.action) {
    case BOT_FLOOR_STOP:
        input->direction.x=0;input->direction.y=0;input->speed=0;
        input->action_flags&=~(uint32_t)(BOT_FLOOR_HORIZONTAL|QA_BOT_JUMP|QA_BOT_DELAYED_JUMP);break;
    case BOT_FLOOR_JUMP: input->action_flags|=QA_BOT_JUMP;break;
    case BOT_FLOOR_NO_JUMP:
        input->action_flags&=~(uint32_t)(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP);break;
    case BOT_FLOOR_CLEAR: break;
    }
    return true;
}
static bool hazard_input(qa_bots *b,bot_ai_state *s,qa_bot_input *input,qa_error *e) {
    if(!s->source_settings || !b->services.shared.player_control) return true;
    if(s->player.dead || s->player.observer || s->player.intermission ||
       s->view.decision==QA_BOT_RESPAWNING || s->view.decision==QA_BOT_OBSERVER ||
       s->view.decision==QA_BOT_INTERMISSION) return true;
    qa_builtin_player_control control;
    if(!b->services.shared.player_control(b->services.shared.context,0,s->view.actor,&control,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(control.player->cutscene) {s->progress=(bot_progress_memory){0};return true;}
    bool grounded=control.ground->hit!=QA_TRACE_HIT_NONE;
    qa_movement_ground ground=*control.ground;
    int32_t water_level=control.player->water_level;
    if(water_level>=2) s->progress=(bot_progress_memory){0};
    else progress_input(b,s,input,grounded);
    qa_body_state body;
    if(!qa_world_body_read(b->services.shared.world,s->view.actor,&body,e)) return false;
    qa_bot_navigation *navigation=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
    uint32_t danger=(1u<<QA_CONTENT_LAVA)|(1u<<QA_CONTENT_SLIME);
    int32_t center,feet;
    qa_vec3 foot=body.origin;foot.z+=fminf(0,body.bounds.mins.z*(5.0f/6.0f));
    if(!qa_bot_navigation_contents(navigation,body.origin,&center,e) ||
       !qa_bot_navigation_contents(navigation,foot,&feet,e)) return false;
    if(!(((uint32_t)center|(uint32_t)feet)&danger)) {
        s->hazard.escaping=false;
        if(grounded && water_level==0) {
            s->hazard.safe_origin=body.origin;s->hazard.has_safe_origin=true;
            return floor_input(b,s,navigation,&body,&ground,input,e);
        }
        s->hazard.floor.valid=false;
        return true;
    }
    s->hazard.floor.valid=false;
    if(!s->hazard.escaping) {
        if(!qa_bot_moves_reset(qa_bot_runtime_moves(b->runtime),s->movement,e)) return false;
    }
    qa_vec3 exit=s->hazard.safe_origin;
    bool found=s->hazard.has_safe_origin;
    for(unsigned i=0;!found && i<12;++i) {
        qa_vec3 direction;
        qa_angle_vectors(qa_v3(0,(float)i*30,0),&direction,NULL,NULL,QA_ANGLE_FLOAT_MATH);
        qa_vec3 point=qa_vec_ma(body.origin,96,direction),below=point;
        below.z+=body.bounds.mins.z;
        int32_t contents,floor;
        if(!qa_bot_navigation_contents(navigation,point,&contents,e)) return false;
        if((uint32_t)contents&(danger|(1u<<QA_CONTENT_SOLID))) continue;
        if(!qa_bot_navigation_contents(navigation,below,&floor,e)) return false;
        if((uint32_t)floor&danger) continue;
        exit=point;found=true;
    }
    s->hazard.escaping=found;
    input->action_flags&=~(uint32_t)(QA_BOT_JUMP|QA_BOT_DELAYED_JUMP|QA_BOT_MOVE_UP|
        QA_BOT_MOVE_DOWN|QA_BOT_CROUCH);
    input->direction.z=0;
    if(water_level>=3) input->action_flags|=QA_BOT_MOVE_UP;
    if(found) {
        qa_vec3 direction=qa_v3(exit.x-body.origin.x,exit.y-body.origin.y,0);
        qa_vec3 angles=qa_vec_angles(direction,QA_DIRECTION_FLOAT_RATIO,QA_VECTOR_SOURCE);
        angles.x=0;angles.z=input->view_angles.z;
        input->direction=qa_vec_normalize(direction);input->speed=400;input->view_angles=angles;
        input->action_flags&=~(uint32_t)(QA_BOT_MOVE_FORWARD|QA_BOT_MOVE_BACK|QA_BOT_MOVE_LEFT|
            QA_BOT_MOVE_RIGHT|QA_BOT_ATTACK);
        bot_ai_view_angles_set(s,angles);bot_ai_view_ideal_set(s,angles);
        bot_source_vec3_write(s->source_span.data+QA_BOT_SOURCE_VIEW_SPEED,qa_v3(0,0,0));
    }
    return true;
}
bool bot_ai_input(qa_bots *b, bot_ai_state *s, int32_t time, int32_t elapsed, qa_error *e) {
    int32_t delta[3];
    if(!delta_angles(b,s,delta,e)) return false;
    bot_ai_view_delta(s,delta,true);
    bot_ai_view_prepare(s);
    if(s->source_settings) {
        if(!s->player.dead && !s->player.observer) {
            qa_vec3 angles=bot_ai_view_angles(s),velocity=bot_ai_view_velocity(s),direction;
            qa_angle_vectors(bot_ai_view_ideal(s),&direction,NULL,NULL,QA_ANGLE_FLOAT_MATH);
            qa_bot_aim_state aim={.pitch=angles.x,.yaw=angles.y,
                .pitch_velocity=velocity.x,.yaw_velocity=velocity.y,.modifier_until=s->aim_modifier_until};
            qa_bot_aim_step(&aim,direction,&s->source_settings->aiming,(float)elapsed/1000,b->time);
            angles.x=aim.pitch;angles.y=aim.yaw;velocity.x=aim.pitch_velocity;velocity.y=aim.yaw_velocity;
            bot_ai_view_angles_set(s,angles);
            bot_source_vec3_write(s->source_span.data+QA_BOT_SOURCE_VIEW_SPEED,velocity);
            s->aim_modifier_until=aim.modifier_until;
        }
    } else {
        float factor = .05f, maximum = 360;
        if (bot_ai_enemy_number(s)>=0) {
            if(!bot_ai_character_float(b,s,BOT_C_VIEW_FACTOR,.01f,1,&factor,e)) return false;
            if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
            if(!bot_ai_character_float(b,s,BOT_C_VIEW_MAX,1,1800,&maximum,e)) return false;
        }
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        bot_ai_view_change(s,factor,maximum,(float)elapsed/1000,b->controls.challenge);
    }
    qa_bot_input input;
    qa_usercmd command;
    if(!bot_ai_source_action_view(b,s,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(!bot_ai_source_action_input(b,s,(float)time/1000,&input,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(!hazard_input(b,s,&input,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    bool ok=true;
    if (ok) ok = bot_ai_source_command_read(b, s, &command, e);
    if (ok && (input.action_flags & QA_BOT_RESPAWN) && (command.buttons & 1))
        input.action_flags &= ~(uint32_t)(QA_BOT_RESPAWN | QA_BOT_ATTACK);
    if (ok) ok = delta_angles(b,s,delta,e);
    if (ok) {
        qa_input_command_intent intent;
        qa_bot_input_intent(&input, &intent);
        qa_input_command_frame frame = {.kind = QA_RULESET_Q3, .server_time_ms = time,
            .weapon = input.weapon, .attack_allowed = true};
        memcpy(frame.delta_angle_words, delta, sizeof(delta));
        qa_usercmd built;
        qa_usercmd_build(&intent, &frame, 0, &built);
        if(s->source_settings && s->progress.pursuing)
            s->progress.pressing=built.forward_move!=0 || built.side_move!=0 ||
                (s->hazard.floor.valid && s->hazard.floor.action==BOT_FLOOR_STOP);
        command = built;
        ok = bot_ai_source_command_write(b, s, &command, e);
    }
    if(!ok) return false;
    if(!delta_angles(b,s,delta,e)) return false;
    bot_ai_view_delta(s,delta,false);
    return submit(b, s, &input, &command, e);
}
bool bot_ai_point_area(qa_bots *b, bot_ai_state *s, qa_vec3 origin, uint32_t *area, qa_error *e) {
    qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
    if (!navigation) return bot_ai_fail(e, "bot navigation is unavailable");
    if (!qa_bot_navigation_point(navigation, origin, area, e)) return false;
    if (*area) return true;
    qa_vec3 end = origin; end.z += 10;
    qa_aas_crossing crossing[10]; size_t count;
    if (!qa_bot_navigation_trace_areas(navigation, origin, end, crossing, 10, &count, e)) return false;
    *area = count ? crossing[0].area : 0;
    return true;
}
bool bot_ai_think(qa_bots *b, bot_ai_state *s, float elapsed, qa_error *e) {
    s->progress.pursuing=false;
    s->hazard.floor.valid=false;s->hazard.floor.recovering=false;s->hazard.floor.side_flipped=false;
    if(!s->source_span.data && !bot_ai_source_alias_bind(b,s,e)) return false;
    qa_bot_actions *actions = qa_bot_runtime_actions(b->runtime);
    int32_t old_inventory[QA_BOT_INVENTORY_SIZE];
    if (!qa_bot_actions_reset(actions, s->view.client, e) || !player(b, s, e)) return false;
    if (s->retired) return true;
    if (!bot_ai_console(b, s, e)) return false;
    if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
    int32_t delta[3];
    if(!delta_angles(b,s,delta,e)) return false;
    bot_ai_view_delta(s,delta,true);
    float local_time;
    if(!bot_ai_storage_f32(b,s,QA_BOT_SOURCE_LOCAL_TIME,&local_time,false,e)) return false;
    local_time+=elapsed;
    if(!bot_ai_storage_f32(b,s,QA_BOT_SOURCE_LOCAL_TIME,&local_time,true,e)) return false;
    if(!bot_ai_storage_f32(b,s,QA_BOT_SOURCE_THINK_TIME,&elapsed,true,e)) return false;
    int32_t height;
    qa_vec3 origin;
    bool ok=bot_ai_source_player_vector(b,s,BOT_PS_ORIGIN,&origin,e);
    if(ok) {
        bot_ai_origin_set(s,origin);bot_ai_eye_set(s,origin);
        ok=bot_ai_source_player_word(b,s,BOT_PS_VIEW_HEIGHT,&height,e);
        if(ok) bot_ai_eye_height_add(s,height);
    }
    if(ok) {
        uint32_t area;
        ok=bot_ai_point_area(b,s,bot_ai_origin(s),&area,e);
        if(ok) bot_ai_area_set(s,area);
    }
    bool setup_ready=true;
    if(ok) ok=bot_ai_source_setup_frame(b,s,&setup_ready,e);
    if(ok && (!setup_ready || s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
    if(ok && s->source_settings) ok=bot_ai_authored_chat_emit(b,s,BOT_CHAT_CONNECTED,e);
    if(ok && s->source_settings && !s->retired && bot_ai_live(b,s->view.actor))
        ok=bot_ai_authored_chat_emit(b,s,BOT_CHAT_MATCH_START,e);
    if(ok) bot_ai_flag_set(s,BOT_AI_IDEAL_VIEW_SET,false);
    if (ok) {
        bool intermission,observer;
        ok=bot_ai_source_intermission(b,s,&intermission,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if (ok && !intermission) {
            bot_source_inventory inventory={b,s};
            qa_bot_inventory_target target=bot_ai_source_inventory_target(&inventory);
            qa_bot_player_state_view source;
            ok=bot_ai_source_set_teleport_time(b,s,e) &&
                bot_ai_source_inventory_snapshot(&inventory,old_inventory,e) &&
                bot_ai_source_player_view(b,s,&source,e);
            if(ok) {
                qa_bot_player sample=bot_ai_player_sample(s);
                ok=b->services.inventory(b->services.context,s->view.actor,&sample,&source,&target,e);
            }
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok) ok=bot_ai_source_task_preference(b,s,old_inventory,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok) ok=bot_ai_source_check_snapshot(b,s,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            qa_bot_navigation *navigation = qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
            int32_t contents;
            if(ok) ok = navigation && qa_bot_navigation_contents(navigation, bot_ai_eye(s), &contents, e);
            if (ok && (bot_ai_inventory_value(s,QA_BOT_INV_ENVIRO) > 0 || !(contents & (8 | 16 | 32))))
                bot_ai_last_air_time_set(s,b->time);
        }
        if (ok) ok = bot_ai_messages(b, s, e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok) ok=bot_ai_source_intermission(b,s,&intermission,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !intermission) ok=bot_ai_source_observer(b,s,&observer,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !intermission && !observer) ok=bot_ai_source_team_policy(b,s,e);
        if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
        if(ok && !bot_ai_enter_game_chat(s) && bot_ai_enter_time(s)>b->time-8) {
            bool chat;
            ok=bot_ai_source_chat_enter_game(b,s,&chat,e);
            if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
            if(ok && chat) {
                float duration;ok=bot_ai_source_chat_time(b,s,&duration,e);
                if(ok && (s->retired || !bot_ai_live(b,s->view.actor))) goto finished;
                if(ok) {
                    bot_ai_stand_until_set(s,b->time+duration);bot_ai_stand_enemy_time_set(s,b->time+1);
                    s->view.decision=QA_BOT_STANDING;s->state_time=b->time;
                }
            }
            if(ok) bot_ai_enter_game_chat_set(s,true);
        }
        if (ok && !intermission && !observer) ok=bot_ai_senses_update(b,s,elapsed,e);
        if (ok) ok = bot_ai_decide(b, s, e);
        if(ok && !s->retired && bot_ai_live(b,s->view.actor)) {
            bot_ai_last_frame_health_set(s,bot_ai_inventory_value(s,QA_BOT_INV_HEALTH));
            int32_t hit_count;
            ok=bot_ai_source_player_slot(b,s,BOT_PS_PERSISTENT,1,&hit_count,e);
            if(ok) bot_ai_last_hit_count_set(s,hit_count);
        }
    }
finished:
    if(s->source_settings && !s->progress.pursuing) s->progress=(bot_progress_memory){0};
    if (ok && !s->retired && bot_ai_live(b,s->view.actor))
        ok = bot_ai_source_action_weapon(b,s,e);
    if(ok && !s->retired && bot_ai_live(b,s->view.actor) && s->source_settings)
        ok=bot_ai_trigger(b,s,e);
    if(ok && !s->retired && bot_ai_live(b,s->view.actor) && s->source_settings)
        ok=bot_ai_authored_chat_step(b,s,e);
    if(!ok) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(!delta_angles(b,s,delta,e)) return false;
    bot_ai_view_delta(s,delta,false);
    return true;
}
static bool observations(qa_bots *b, qa_error *e) {
    uint32_t extent;
    if (!b->services.entity_extent(b->services.context,&extent,e) ||
        !qa_bot_runtime_invalidate_entity_range(b->runtime,0,extent,e)) return false;
    b->entities.capacity=qa_actors_capacity(qa_session_actors(b->services.shared.session));
    if (!b->services.entity_list(b->services.context,&b->entities,e)) return false;
    b->players.capacity=qa_actors_capacity(qa_session_actors(b->services.shared.session));
    if (!qa_builtin_players(&b->services.shared, &b->players, e)) return false;
    for (size_t i = 0; i < b->entities.count; ++i) {
        qa_actor_id actor = b->entities.actors[i];
        if (!bot_ai_live(b, actor)) continue;
        qa_bot_entity entity;
        if (!b->services.entity(b->services.context, actor, &entity, e)) return false;
        if (!bot_ai_live(b, actor)) continue;
        bool visible = entity.present && entity.linked && !entity.hidden &&
            (!entity.missile || entity.grapple) && !entity.temporary_event && !entity.proximity_trigger;
        if (!qa_bot_runtime_update_entity(b->runtime, entity.number,
                                             visible ? &entity.observation : NULL, e)) return false;
    }
    return true;
}
static bool retire_pending(qa_bots *b, qa_error *e) {
    qa_error first = {0};
    for (uint32_t i = 0; i < b->client_capacity; ++i) {
        bot_ai_state *s = b->clients[i];
        if (!s || (!s->retired && bot_ai_live(b, s->view.actor))) continue;
        qa_error local = {0};
        bool published=s->inuse;
        if (!bot_ai_cleanup(b, s, &local)) {if(!first.code) first=local;continue;}
        if(published && !qa_bot_source_record_clear(&b->services.memory,s->source_record,false,&local)) {
            if(!first.code) first=local;
            continue;
        }
        bot_ai_source_cell_clear(b,s);
    }
    if (first.code && e) *e = first;
    return !first.code;
}
static bool frame(qa_bots *b, int32_t time, qa_error *e) {
    if (!bot_ai_source_frame_cvars(b,e)) return false;
    if (b->controls.report && !bot_ai_source_report(b,e)) return false;
    if (b->controls.paused) {
        for (uint32_t i = 0; i < 64; ++i) {
            bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
            if (!s || !connected(b, s)) continue;
            qa_usercmd command;
            if (!bot_ai_source_command_pause(b, s, time, e) ||
                !bot_ai_source_command_read(b, s, &command, e)) return false;
            qa_bot_input input = {.view_angles = bot_ai_view_angles(s), .weapon = command.weapon};
            command.angles = input.view_angles;
            if (!submit(b, s, &input, &command, e)) return false;
        }
        return true;
    }
    if(!bot_ai_source_frame_requests(b,e) || !bot_ai_source_interbreeding(b,e)) return false;
    int32_t period = b->controls.think_time_ms;
    if (!bot_ai_source_frame_limit_think(b,e)) return false;
    if (period != b->scheduled_think_ms) {
        b->scheduled_think_ms=period;if(!bot_ai_schedule(b,e)) return false;
    }
    int32_t elapsed = qa_i32_from_u32((uint32_t)time - (uint32_t)b->local_time_ms);
    b->local_time_ms = time;
    b->library_residual_ms = qa_i32_from_u32((uint32_t)b->library_residual_ms + (uint32_t)elapsed);
    int32_t think = elapsed > period ? elapsed : period;
    if (b->library_residual_ms >= think) {
        b->library_residual_ms = qa_i32_from_u32((uint32_t)b->library_residual_ms - (uint32_t)think);
        if (!qa_bot_runtime_start_frame(b->runtime, (float)time / 1000, e) || !observations(b, e)) return false;
        if (b->regular_update_time < b->time) {
            if (!qa_bot_goals_update_items(qa_bot_runtime_goals(b->runtime),
                                              qa_bot_runtime_navigation(b->runtime, -1), e)) return false;
            b->regular_update_time = b->time + .3f;
        }
    }
    b->time = qa_bot_runtime_time(b->runtime);
    for (uint32_t i = 0; i < 64; ++i) {
        bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
        if (!s || s->retired) continue;
        int32_t residual;
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_RESIDUAL,&residual,false,e)) return false;
        residual = qa_i32_from_u32((uint32_t)residual + (uint32_t)elapsed);
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_RESIDUAL,&residual,true,e)) return false;
        if (residual < think) continue;
        residual = qa_i32_from_u32((uint32_t)residual - (uint32_t)think);
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_RESIDUAL,&residual,true,e)) return false;
        if (!ready(b, s)) continue;
        if (connected(b, s) && !bot_ai_think(b, s, (float)think / 1000, e)) return false;
    }
    for (uint32_t i = 0; i < 64; ++i) {
        bot_ai_state *s = b->source_clients[i]?b->clients[b->source_clients[i]-1]:NULL;
        if (!s || s->retired) continue;
        if (connected(b, s) && !bot_ai_input(b, s, time, elapsed, e)) return false;
    }
    return true;
}
bool qa_bots_frame(qa_bots *b, int32_t time, qa_error *e) {
    if (!bot_ai_mutable(b, e)) return false;
    if (b->checking_spawn) return bot_ai_fail(e,"bot frame reentered its spawn callback");
    if (!qa_bot_runtime_lease_begin(b->runtime,e)) return false;
    b->checking_spawn=true;
    bool spawn_ok=!b->services.check_spawn || b->services.check_spawn(b->services.context,e);
    b->checking_spawn=false;
    if(!spawn_ok) {qa_bot_runtime_lease_end(b->runtime);return false;}
    b->busy = true;
    bool ok = frame(b, time, e);
    qa_error cleanup = {0};
    if (!retire_pending(b, &cleanup) && ok) { ok = false; if (e) *e = cleanup; }
    b->busy = false;
    qa_bot_runtime_lease_end(b->runtime);
    return ok;
}
