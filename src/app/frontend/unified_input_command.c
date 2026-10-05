#include "unified_input_command.h"
#include <float.h>

static double clamp(double value, double low, double high)
{ return isnan(value)?value:fmax(low,fmin(high,value)); }
static double rounded(bool q3, double value)
{ return !q3 ? value : fabs(value)>FLT_MAX ? copysign(INFINITY,value) : (double)(float)value; }
static double fraction(const qa_seat_input_sample *s, qa_input_action action)
{ return s->buttons[action].fraction; }
static bool active(const qa_seat_input_sample *s, qa_input_action action)
{ return s->buttons[action].active; }
static bool pressed(const qa_seat_input_sample *s, qa_input_action action)
{ return active(s,action) || s->buttons[action].pressed; }
static double add(bool q3, double value, double amount)
{ return q3 ? trunc(rounded(true,rounded(true,value)+rounded(true,amount))) : value+amount; }
static bool finite_angles(qa_unified_vec3 value)
{ return isfinite(value.x) && isfinite(value.y) && isfinite(value.z); }
static bool valid(const frontend_unified_command_builder *b,const qa_input_command_tuning *t,
    const qa_seat_input_sample *s,const frontend_unified_command_frame *f,double elapsed)
{
    if(!b || !t || !s || !f || b->kind!=f->kind || (unsigned)b->kind>QA_MOVEMENT_Q3 ||
        !finite_angles(b->angles) || !isfinite(b->mouse_x) || !isfinite(b->mouse_y) ||
        !isfinite(b->drift_velocity) || !isfinite(b->drift_seconds) ||
        !qa_mouse_tuning_valid(&t->mouse) || !isfinite(elapsed) || elapsed<0 ||
        !isfinite(s->frame_ms) || s->frame_ms<=0 || !isfinite(s->mouse.x) || !isfinite(s->mouse.y) ||
        !isfinite(s->gamepad.move.x) || !isfinite(s->gamepad.move.y) ||
        !isfinite(s->gamepad.look_degrees.x) || !isfinite(s->gamepad.look_degrees.y) ||
        !isfinite(f->acknowledged_seconds) || !isfinite(f->server_time_ms) || !isfinite(f->server_frame) ||
        !isfinite(f->weapon) || !isfinite(f->sensitivity) || !isfinite(f->light_level) ||
        (f->has_pitch_drift && !isfinite(f->ideal_pitch))) return false;
    const qa_view_input_tuning *v=&t->view;
    if(!isfinite(v->forward_speed) || !isfinite(v->back_speed) || !isfinite(v->side_speed) ||
        !isfinite(v->up_speed) || !isfinite(v->yaw_speed) || !isfinite(v->pitch_speed) ||
        !isfinite(v->angle_multiplier) || !isfinite(v->move_multiplier) ||
        !isfinite(t->drift_speed) || !isfinite(t->drift_delay)) return false;
    for(size_t i=0;i<QA_INPUT_ACTION_COUNT;++i)
        if(!isfinite(s->buttons[i].fraction) || s->buttons[i].fraction<0 || s->buttons[i].fraction>1) return false;
    return true;
}
bool frontend_unified_command_build(frontend_unified_command_builder *b,const qa_input_command_tuning *t,
    const qa_seat_input_sample *s,const frontend_unified_command_frame *f,double elapsed,
    qa_unified_movement *out,qa_error *e)
{
    if(!out || !valid(b,t,s,f,elapsed)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Unified command requires its actual finite physical sample and tuning");
        return false;
    }
    frontend_unified_command_builder next=*b;
    const qa_view_input_tuning *v=&t->view;
    bool q3=f->kind==QA_MOVEMENT_Q3, q1=f->kind==QA_MOVEMENT_NETQUAKE || f->kind==QA_MOVEMENT_QUAKEWORLD;
    bool speed=active(s,QA_INPUT_WALK),strafe=active(s,QA_INPUT_STRAFE),klook=active(s,QA_INPUT_KLOOK);
    double angle_speed=rounded(q3,elapsed/1000*(speed?v->angle_multiplier:1));
    double pitch=next.angles.x,previous_pitch=pitch,yaw=next.angles.y,roll=next.angles.z;
    if(!strafe) {
        yaw=rounded(q3,yaw-rounded(q3,rounded(q3,angle_speed*v->yaw_speed)*fraction(s,QA_INPUT_TURN_RIGHT)));
        yaw=rounded(q3,yaw+rounded(q3,rounded(q3,angle_speed*v->yaw_speed)*fraction(s,QA_INPUT_TURN_LEFT)));
        if(q1) yaw=qa_angle_mod((float)yaw);
    }
    if(klook && !q3) {
        pitch-=angle_speed*v->pitch_speed*fraction(s,QA_INPUT_FORWARD);
        pitch+=angle_speed*v->pitch_speed*fraction(s,QA_INPUT_BACK);
    }
    pitch=rounded(q3,pitch-rounded(q3,rounded(q3,angle_speed*v->pitch_speed)*fraction(s,QA_INPUT_LOOK_UP)));
    pitch=rounded(q3,pitch+rounded(q3,rounded(q3,angle_speed*v->pitch_speed)*fraction(s,QA_INPUT_LOOK_DOWN)));
    if(q1) { pitch=clamp(pitch,-70,80); roll=clamp(roll,-50,50); }
    bool running=speed!=v->always_run;
    double move_speed=q3?(running?127:64):1,forward=0,side=0,up=0;
    if(strafe) {
        side=add(q3,side,rounded(q3,(q3?move_speed:v->side_speed)*fraction(s,QA_INPUT_TURN_RIGHT)));
        side=add(q3,side,-rounded(q3,(q3?move_speed:v->side_speed)*fraction(s,QA_INPUT_TURN_LEFT)));
    }
    side=add(q3,side,rounded(q3,(q3?move_speed:v->side_speed)*fraction(s,QA_INPUT_MOVE_RIGHT)));
    side=add(q3,side,-rounded(q3,(q3?move_speed:v->side_speed)*fraction(s,QA_INPUT_MOVE_LEFT)));
    double jump=q1?fraction(s,QA_INPUT_MOVE_UP):fmax(fraction(s,QA_INPUT_JUMP),fraction(s,QA_INPUT_MOVE_UP));
    double crouch=q1?fraction(s,QA_INPUT_MOVE_DOWN):fmax(fraction(s,QA_INPUT_CROUCH),fraction(s,QA_INPUT_MOVE_DOWN));
    up=add(q3,up,rounded(q3,(q3?move_speed:v->up_speed)*jump));
    up=add(q3,up,-rounded(q3,(q3?move_speed:v->up_speed)*crouch));
    if(!klook || q3) {
        forward=add(q3,forward,rounded(q3,(q3?move_speed:v->forward_speed)*fraction(s,QA_INPUT_FORWARD)));
        forward=add(q3,forward,-rounded(q3,(q3?move_speed:v->back_speed)*fraction(s,QA_INPUT_BACK)));
    }
    if(!q3 && running) { forward*=v->move_multiplier;side*=v->move_multiplier;up*=v->move_multiplier; }
    const qa_mouse_tuning *m=&t->mouse;
    double x=m->filter?rounded(q3,((double)s->mouse.x+next.mouse_x)*.5):rounded(q3,s->mouse.x);
    double y=m->filter?rounded(q3,((double)s->mouse.y+next.mouse_y)*.5):rounded(q3,s->mouse.y);
    double rate=rounded(q3,sqrt(rounded(q3,rounded(q3,x*x)+rounded(q3,y*y)))/rounded(q3,s->frame_ms));
    double gain=rounded(q3,rounded(q3,m->sensitivity+rounded(q3,rate*m->acceleration))*(q3?f->sensitivity:1));
    x=rounded(q3,x*gain);y=rounded(q3,y*gain);
    next.mouse_x=s->mouse.x;next.mouse_y=s->mouse.y;
    bool mlook=active(s,QA_INPUT_MLOOK),horizontal=strafe || (m->look_strafe && mlook);
    bool look=!strafe && (mlook || m->free_look);
    double mouse_yaw=horizontal?0:rounded(q3,-m->yaw*x);
    double mouse_side=horizontal?rounded(q3,m->side*x):0;
    double mouse_pitch=look?rounded(q3,m->pitch*y*(m->invert_pitch?-1:1)):0;
    double mouse_forward=look?0:rounded(q3,-m->forward*y);
    forward=add(q3,forward,mouse_forward);side=add(q3,side,mouse_side);
    yaw=rounded(q3,yaw+mouse_yaw-s->gamepad.look_degrees.x);
    pitch=rounded(q3,pitch+mouse_pitch+s->gamepad.look_degrees.y);
    double pad_forward=q3?move_speed:v->forward_speed*(running?v->move_multiplier:1);
    double pad_side=q3?move_speed:v->side_speed*(running?v->move_multiplier:1);
    forward=add(q3,forward,s->gamepad.move.y*pad_forward);side=add(q3,side,s->gamepad.move.x*pad_side);
    if(q1 && f->has_pitch_drift) {
        bool manual=mlook || m->free_look || (klook && (active(s,QA_INPUT_FORWARD) || active(s,QA_INPUT_BACK))) ||
            fraction(s,QA_INPUT_LOOK_UP)!=0 || fraction(s,QA_INPUT_LOOK_DOWN)!=0 || s->gamepad.look_degrees.y!=0;
        bool start=!mlook && (next.previous_mouse_look || pressed(s,QA_INPUT_MLOOK)) && m->look_spring;
        if(manual) {next.drifting=false;next.drift_velocity=0;next.drift_seconds=0;}
        else if(start && (!next.drifting || next.drift_velocity==0)) {next.drifting=true;next.drift_velocity=t->drift_speed;next.drift_seconds=0;}
        double seconds=elapsed/1000;
        if(f->drift_disabled || !f->grounded) {next.drift_seconds=0;next.drift_velocity=0;}
        else if(!next.drifting) {
            next.drift_seconds=manual || fabs(forward)<(f->kind==QA_MOVEMENT_QUAKEWORLD?200:v->forward_speed)?0:next.drift_seconds+seconds;
            if(next.drift_seconds>t->drift_delay) {next.drifting=true;next.drift_velocity=t->drift_speed;next.drift_seconds=0;}
        } else {
            double delta=f->ideal_pitch-pitch;
            if(delta==0) next.drift_velocity=0;
            else {double move=fmin(fabs(delta),seconds*next.drift_velocity);next.drift_velocity+=seconds*t->drift_speed;
                if(move==fabs(delta)) next.drift_velocity=0;
                pitch+=copysign(move,delta);}
        }
    }
    next.previous_mouse_look=mlook;
    uint32_t buttons=0;
    if(q3) {
        for(unsigned i=0;i<15;++i) if(pressed(s,(qa_input_action)((unsigned)QA_INPUT_BUTTON0+i))) buttons|=UINT32_C(1)<<i;
        if(pressed(s,QA_INPUT_ATTACK)) buttons|=1;
        if(pressed(s,QA_INPUT_USE)) buttons|=4;
        if(!running) buttons|=16;
        if(!s->game_focus) buttons|=2;
        else if(s->any_key_down) buttons|=2048;
        pitch=clamp(pitch,previous_pitch-90,previous_pitch+90);
    } else {
        if(pressed(s,QA_INPUT_ATTACK) && (q1 || f->attack_allowed)) buttons|=1;
        if((q1 && pressed(s,QA_INPUT_JUMP)) || (!q1 && pressed(s,QA_INPUT_USE))) buttons|=2;
        if(!q1 && s->any_key_down && s->game_focus) buttons|=128;
        if(f->kind==QA_MOVEMENT_Q2_RERELEASE) {
            if(pressed(s,QA_INPUT_HOLSTER)) buttons|=4;
            if(pressed(s,QA_INPUT_JUMP) || pressed(s,QA_INPUT_MOVE_UP)) buttons|=8;
            if(pressed(s,QA_INPUT_CROUCH) || pressed(s,QA_INPUT_MOVE_DOWN)) buttons|=16;
        }
    }
    if(q1) pitch=clamp(pitch,-70,80);
    if(f->kind==QA_MOVEMENT_Q2_CLASSIC || f->kind==QA_MOVEMENT_Q2_RERELEASE) {
        /* Unified angles are absolute; provider projection subtracts its
         * authoritative delta exactly once after receiving this command. */
        if(pitch < -360) pitch+=360;
        if(pitch > 360) pitch-=360;
        pitch=clamp(pitch,-89,89);forward=clamp(forward,-400,400);side=clamp(side,-400,400);
    }
    next.angles=(qa_unified_vec3){pitch,yaw,roll};
    if(!finite_angles(next.angles) || !isfinite(forward) || !isfinite(side) || !isfinite(up) ||
        !isfinite(next.drift_velocity) || !isfinite(next.drift_seconds)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Unified physical command overflow");return false;
    }
    double milliseconds=trunc(elapsed>250?100:elapsed);
    qa_unified_movement command={.kind=f->kind};
    switch(f->kind) {
    case QA_MOVEMENT_NETQUAKE:
        command.data.nq.acknowledged_seconds=f->acknowledged_seconds;command.data.nq.angles=next.angles;
        command.data.nq.forward=trunc(forward);command.data.nq.side=trunc(side);command.data.nq.up=trunc(up);
        command.data.nq.buttons=buttons;command.data.nq.impulse=s->impulse;break;
    case QA_MOVEMENT_QUAKEWORLD:
        command.data.qw.milliseconds=milliseconds;command.data.qw.angles=next.angles;
        command.data.qw.forward=trunc(forward);command.data.qw.side=trunc(side);command.data.qw.up=trunc(up);
        command.data.qw.buttons=buttons;command.data.qw.impulse=s->impulse;break;
    case QA_MOVEMENT_Q2_CLASSIC:
        command.data.q2.milliseconds=milliseconds;command.data.q2.angle_shorts[0]=qa_angle_to_word((float)pitch);
        command.data.q2.angle_shorts[1]=qa_angle_to_word((float)yaw);command.data.q2.angle_shorts[2]=qa_angle_to_word((float)roll);
        command.data.q2.forward=trunc(forward);command.data.q2.side=trunc(side);command.data.q2.up=trunc(up);
        command.data.q2.buttons=buttons;command.data.q2.impulse=s->impulse;command.data.q2.light_level=f->light_level;break;
    case QA_MOVEMENT_Q2_RERELEASE:
        command.data.q2r.milliseconds=milliseconds;command.data.q2r.angles=next.angles;
        command.data.q2r.forward=forward;command.data.q2r.side=side;command.data.q2r.buttons=buttons;command.data.q2r.server_frame=f->server_frame;break;
    case QA_MOVEMENT_Q3:
        command.data.q3.server_time_ms=f->server_time_ms;command.data.q3.angle_words[0]=qa_angle_to_word((float)pitch);
        command.data.q3.angle_words[1]=qa_angle_to_word((float)yaw);command.data.q3.angle_words[2]=qa_angle_to_word((float)roll);
        command.data.q3.forward=trunc(clamp(forward,-127,127));command.data.q3.right=trunc(clamp(side,-127,127));command.data.q3.up=trunc(clamp(up,-127,127));
        command.data.q3.buttons=buttons;command.data.q3.weapon=f->weapon;break;
    }
    *b=next;*out=command;return true;
}
