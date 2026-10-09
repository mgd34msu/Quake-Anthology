#include "internal.h"
#include <limits.h>
#include <stdio.h>

static int32_t signed_word(uint32_t word) {
    return word<=INT32_MAX?(int32_t)word:(int32_t)((int64_t)word-INT64_C(4294967296));
}
static int32_t difference(int32_t a, int32_t b) { return signed_word((uint32_t)a-(uint32_t)b); }
void qa_q3_command_history_init(qa_q3_command_history *history) {
    if (!history) return;
    memset(history,0,sizeof(*history));
    for (size_t i=0;i<64;i++) history->commands[i].kind=QA_RULESET_Q3;
}
bool qa_q3_command_history_append(qa_q3_command_history *history, const qa_movement_command *command, uint32_t *number, qa_error *error) {
    if (!history||!command||command->kind!=QA_RULESET_Q3) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 command history requires a Q3 command"); return false;
    }
    history->current_number++;
    history->commands[history->current_number&63u]=*command;
    if (number) *number=history->current_number;
    return true;
}
bool qa_q3_command_history_read(const qa_q3_command_history *history, uint32_t number, qa_movement_command *out, bool *available, qa_error *error) {
    if (!history||!out||!available) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q3 command history output"); return false; }
    uint32_t distance=history->current_number-number;
    if (distance>INT32_MAX) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 command number is newer than the current command"); return false; }
    *available=distance<64;
    if (*available) *out=history->commands[number&63u];
    return true;
}
void qa_q3_prediction_init(qa_q3_prediction *prediction) {
    if (!prediction) return;
    memset(prediction,0,sizeof(*prediction));
    prediction->predicted.kind=QA_RULESET_Q3;
    prediction->command.kind=QA_RULESET_Q3;
}
void qa_q3_prediction_free(qa_q3_prediction *prediction) {
    if (!prediction) return;
    qa_movement_result_free(&prediction->scratch);
    memset(prediction,0,sizeof(*prediction));
}
bool qa_q3_prediction_view(qa_movement_state *state, int32_t health, const qa_movement_command *command, qa_error *error) {
    if (!state||!command||state->kind!=QA_RULESET_Q3||command->kind!=QA_RULESET_Q3) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 prediction view requires Q3 state and command"); return false;
    }
    qa_q3_movement_state *s=&state->data.q3;
    if (s->movement_type==5||s->movement_type==6||(s->movement_type!=2&&health<=0)) return true;
    qa_move_q3_view(s, command);
    return true;
}
void qa_movement_q3_finish_jump_pads(qa_movement_state *state) {
    if (state&&state->kind==QA_RULESET_Q3&&state->data.q3.jump_pad_frame!=state->data.q3.movement_frame) {
        state->data.q3.jump_pad=(qa_actor_id){0}; state->data.q3.jump_pad_frame=0;
    }
}
static bool required_command(const qa_q3_prediction_host *host, uint32_t number, qa_movement_command *out, qa_error *error) {
    bool available;
    if (!qa_q3_command_history_read(host->commands,number,out,&available,error)) return false;
    if (!available) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 prediction command is outside CMD_BACKUP"); return false; }
    return true;
}
static float lerp_angle(float a, float b, float fraction) {
    if (b-a>180) b-=360;
    if (b-a < -180) b+=360;
    return a+fraction*(b-a);
}
static bool interpolate(qa_q3_prediction *p, const qa_q3_prediction_host *host, const qa_q3_prediction_frame *frame, bool angles, qa_error *error) {
    p->predicted=frame->snapshot->movement;
    if (angles) {
        qa_movement_command command;
        if (!required_command(host,host->commands->current_number,&command,error)||!qa_q3_prediction_view(&p->predicted,frame->health,&command,error)) return false;
    }
    const qa_q3_prediction_snapshot *next=frame->next_snapshot;
    if (frame->next_frame_teleport||!next||next->server_time_ms<=frame->snapshot->server_time_ms) return true;
    float fraction=(float)difference(frame->time_ms,frame->snapshot->server_time_ms)/(float)difference(next->server_time_ms,frame->snapshot->server_time_ms);
    const qa_q3_movement_state *a=&frame->snapshot->movement.data.q3,*b=&next->movement.data.q3;
    qa_q3_movement_state *s=&p->predicted.data.q3;
    int32_t cycle=b->bob_cycle<a->bob_cycle?signed_word((uint32_t)b->bob_cycle+256u):b->bob_cycle;
    float bob=(float)a->bob_cycle+fraction*(float)difference(cycle,a->bob_cycle);
    s->bob_cycle=!isfinite(bob)||bob>=2147483648.0f||bob < -2147483648.0f?INT32_MIN:(int32_t)truncf(bob);
    s->origin=qa_vec_lerp(a->origin,b->origin,fraction);
    s->velocity=qa_vec_lerp(a->velocity,b->velocity,fraction);
    if (!angles) s->view_angles=qa_v3(lerp_angle(a->view_angles.x,b->view_angles.x,fraction),
        lerp_angle(a->view_angles.y,b->view_angles.y,fraction),lerp_angle(a->view_angles.z,b->view_angles.z,fraction));
    return true;
}
static bool adjusted(const qa_q3_prediction_host *host, qa_vec3 origin, qa_movement_ground ground, int32_t from, int32_t to, qa_vec3 *out, qa_error *error) {
    if (!host->adjust_mover) { *out=origin; return true; }
    return host->adjust_mover(host->context,origin,ground,from,to,out,error);
}
static void warn(const qa_q3_prediction_host *host, const qa_q3_prediction_settings *settings, const char *message) {
    if (settings->show_miss&&host->warning) host->warning(host->context,message);
}
bool qa_q3_predict(qa_q3_prediction *p, const qa_q3_prediction_host *host, const qa_q3_prediction_settings *settings,
                    const qa_q3_prediction_frame *frame, qa_q3_prediction_output *out, qa_error *error) {
    if (!p||!host||!settings||!frame||!out||!host->commands||!frame->snapshot||
        frame->snapshot->movement.kind!=QA_RULESET_Q3||
        (frame->next_snapshot&&frame->next_snapshot->movement.kind!=QA_RULESET_Q3)||
        !isfinite(settings->error_decay_value)||(settings->error_decay_integer&&settings->error_decay_value<=0)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q3 prediction frame or settings"); return false;
    }
    if (!p->initialized) { p->predicted=frame->snapshot->movement; p->initialized=true; }
    qa_q3_prediction_output result={.status=QA_PREDICTION_UNCHANGED,.movement=p->predicted};
    bool follow=(frame->snapshot->movement.data.q3.movement_flags&4096u)!=0;
    if (settings->demo_playback||follow||settings->no_predict||settings->synchronous_clients) {
        if (!interpolate(p,host,frame,!settings->demo_playback&&!follow,error)) return false;
        result.status=QA_PREDICTION_INTERPOLATED; result.movement=p->predicted;
        result.interpolated_owners=frame->snapshot->owners; *out=result; return true;
    }
    if (!host->restore||!host->movement_input) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 replay requires owner restore and movement input callbacks"); return false; }
    qa_movement_state old=p->predicted;
    uint32_t current=host->commands->current_number;
    qa_movement_command oldest,latest;
    if (!required_command(host,current-63u,&oldest,error)||!required_command(host,current,&latest,error)) return false;
    if (oldest.server_time_ms>frame->snapshot->movement.data.q3.command_time_ms&&oldest.server_time_ms<frame->time_ms) {
        warn(host,settings,"exceeded PACKET_BACKUP on commands\n");
        result.status=QA_PREDICTION_COMMAND_BACKUP_EXCEEDED; *out=result; return true;
    }
    const qa_q3_prediction_snapshot *selected=frame->next_snapshot&&!frame->next_frame_teleport&&!frame->this_frame_teleport?frame->next_snapshot:frame->snapshot;
    if (host->before_replay&&!host->before_replay(host->context,error)) return false;
    if (!host->restore(host->context,selected,error)) return false;
    p->predicted=selected->movement;
    int32_t physics_time=selected->server_time_ms;
    uint32_t msec=settings->movement_ms<8?8:settings->movement_ms>33?33:settings->movement_ms;
    if (msec!=settings->movement_ms&&host->set_movement_ms) host->set_movement_ms(host->context,msec);
    bool moved=false;
    for (uint32_t index=0;index<64;index++) {
        uint32_t number=current-63u+index;
        bool available;
        if (!qa_q3_command_history_read(host->commands,number,&p->command,&available,error)) return false;
        if (settings->fixed&&!qa_q3_prediction_view(&p->predicted,frame->health,&p->command,error)) return false;
        qa_q3_movement_state *s=&p->predicted.data.q3;
        if (p->command.server_time_ms<=s->command_time_ms||p->command.server_time_ms>latest.server_time_ms) continue;
        if (s->command_time_ms==old.data.q3.command_time_ms) {
            if (frame->this_frame_teleport&&!result.consumed_teleport) {
                p->error=qa_v3(0,0,0); result.consumed_teleport=true; warn(host,settings,"PredictionTeleport\n");
            } else {
                qa_vec3 position;
                if (!adjusted(host,s->origin,s->ground,physics_time,frame->previous_time_ms,&position,error)) return false;
                qa_vec3 delta=qa_vec_sub(old.data.q3.origin,position);
                float length=qa_vec_length(delta);
                if (length>0.1f) {
                    if (settings->show_miss&&host->warning) { char message[96]; snprintf(message,sizeof(message),"Prediction miss: %.6f\n",(double)length); host->warning(host->context,message); }
                    if (settings->error_decay_integer) {
                        float fraction=(settings->error_decay_value-(float)difference(frame->time_ms,p->error_time_ms))/settings->error_decay_value;
                        if (fraction<0) fraction=0;
                        p->error=qa_vec_scale(p->error,fraction);
                    } else p->error=qa_v3(0,0,0);
                    p->error=qa_vec_add(delta,p->error); p->error_time_ms=frame->previous_time_ms;
                }
            }
        }
        if (settings->fixed) {
            int32_t rounded=signed_word((uint32_t)p->command.server_time_ms+msec-1u);
            int32_t quotient=rounded/(int32_t)msec;
            p->command.server_time_ms=signed_word((uint32_t)quotient*msec);
        }
        qa_movement_input input;
        if (!host->movement_input(host->context,&p->predicted,&p->command,number,physics_time,&input,error)) return false;
        input.state=p->predicted; input.command=p->command; input.command.sequence=number;
        input.prediction=true; input.profile.kind=QA_RULESET_Q3; input.profile.data.q3.fixed_ms=settings->fixed?msec:0;
        qa_movement_result *movement=&p->scratch;
        if (!qa_movement_move(&input,&host->movement_services,movement,error)) return false;
        if (movement->status==QA_MOVEMENT_ACTOR_REMOVED) {
            result.status=QA_PREDICTION_ACTOR_REMOVED; result.movement=p->predicted; *out=result; return true;
        }
        p->predicted=movement->state; qa_bounds bounds=movement->bounds;
        moved=true;
        if (host->touch_triggers) {
            bool hyperspace=false;
            qa_movement_control state=host->touch_triggers(host->context,&p->predicted,bounds,physics_time,&hyperspace,error);
            result.hyperspace|=hyperspace;
            if (state==QA_MOVEMENT_ERROR) return false;
            if (state==QA_MOVEMENT_REMOVED) { result.status=QA_PREDICTION_ACTOR_REMOVED; result.movement=p->predicted; *out=result; return true; }
            if (state!=QA_MOVEMENT_CONTINUE||p->predicted.kind!=QA_RULESET_Q3) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 prediction trigger changed its movement family"); return false; }
        }
    }
    if (moved) {
        qa_q3_movement_state *s=&p->predicted.data.q3;
        if (!adjusted(host,s->origin,s->ground,physics_time,frame->time_ms,&s->origin,error)) return false;
        if (signed_word(s->event_sequence)>signed_word(old.data.q3.event_sequence+2u)) warn(host,settings,"WARNING: dropped event\n");
        if (host->transition&&!host->transition(host->context,&p->predicted,&old,error)) return false;
        result.status=QA_PREDICTION_PREDICTED;
    }
    result.movement=p->predicted; *out=result; return true;
}
