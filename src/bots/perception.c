#include "qa/bot_perception.h"

static float clamp(float value,float minimum,float maximum) { return fmaxf(minimum,fminf(value,maximum)); }
static float angle_mod(float value) { value=fmodf(value,360); return value<0 ? value+360 : value; }
static float angle_delta(float from,float to) { float value=angle_mod(to-from); return value>180 ? value-360 : value; }
static qa_vec3 view_angles(qa_vec3 direction)
{
    if (direction.x==0 && direction.y==0) return qa_v3(direction.z>0 ? -90 : 90,0,0);
    return qa_v3(-atan2f(direction.z,hypotf(direction.x,direction.y))*57.29577951308232f,
        angle_mod(atan2f(direction.y,direction.x)*57.29577951308232f),0);
}
qa_bot_awareness qa_bot_awareness_new(qa_actor_id actor,double now,qa_vec3 origin)
{
    return (qa_bot_awareness){.actor=actor,.last_contact=now,.last_seen=-1,.last_heard=-1,.last_known_origin=origin};
}
qa_bot_sight_geometry qa_bot_sight(qa_vec3 eye,float pitch,float yaw,qa_vec3 target,bool invisible,const qa_bot_senses *s,const qa_bot_weapon_senses *w)
{
    const float radians=0.017453292519943295f;
    float cp=cosf(pitch*radians);
    qa_vec3 forward=qa_v3(cp*cosf(yaw*radians),cp*sinf(yaw*radians),-sinf(pitch*radians)),direction=qa_vec_sub(target,eye);
    float distance=qa_vec_length(direction),angle=distance==0 ? 90 : acosf(clamp(qa_vec_dot(forward,direction)/distance,-1,1))*57.29577951308232f;
    return (qa_bot_sight_geometry){.in_sight_fov=angle<=s->fov_angle/2,.in_weapon_fov=angle<=w->fov_angle/2,.distance=distance,
        .within_invisible_range=!invisible || s->maximum_invisible_distance<=0 || distance<=s->maximum_invisible_distance};
}
void qa_bot_sense_step(qa_bot_awareness *a,const qa_bot_contact *c,const qa_bot_senses *s,const qa_bot_weapon_senses *w,float dt,double now)
{
    float fill=s->sight_time;
    if (c->invisible && s->invisible_sight_scalar>0) fill*=s->invisible_sight_scalar;
    if (c->line_of_sight && c->in_sight_fov) {
        a->sight=fill>0 ? clamp(a->sight+dt/fill,0,1) : 1;
        a->last_seen=now; a->last_contact=now; a->last_known_origin=c->origin;
    } else if (c->audible) {
        a->sight=s->sound_time>0 ? clamp(a->sight+dt/s->sound_time,0,1) : 1;
        a->last_heard=now; a->last_contact=now; a->last_known_origin=c->origin;
    } else {
        float decay=a->last_seen>=a->last_heard ? s->sight_decay_time : s->sound_decay_time;
        a->sight=decay>0 ? clamp(a->sight-dt/decay,0,1) : 0;
    }
    if (c->line_of_sight && c->in_weapon_fov) a->weapon=w->sight_time>0 ? clamp(a->weapon+dt/w->sight_time,0,1) : 1;
    else a->weapon=w->decay_time>0 ? clamp(a->weapon-dt/w->decay_time,0,1) : 0;
}
bool qa_bot_should_forget(const qa_bot_awareness *a,const qa_bot_senses *s,double now)
{
    return a->sight<=0 && now-a->last_contact>=s->forget_non_visible_time;
}
bool qa_bot_sound_audible(const qa_bot_sound *sound,qa_vec3 listener,const qa_bot_senses *s,double now)
{
    return now-sound->time<=s->sound_persist_time && qa_vec_length(qa_vec_sub(sound->origin,listener))<=s->sound_range*(sound->loudness>0 ? sound->loudness : 1);
}
qa_vec3 qa_bot_aim_lead(qa_vec3 target,qa_vec3 velocity,const qa_bot_aiming *s) { return qa_vec_add(target,qa_vec_scale(velocity,s->velocity_offset)); }
static void advance(float *angle,float *velocity,float error,float stiffness,float damping,float max_speed,float h,unsigned steps)
{
    float initial=*angle;
    for (unsigned i=0;i<steps;++i) {
        float acceleration=stiffness*(error-(*angle-initial))-damping*(*velocity);
        *velocity+=acceleration*h;
        if (max_speed>0) *velocity=clamp(*velocity,-max_speed,max_speed);
        *angle+=*velocity*h;
    }
}
void qa_bot_aim_step(qa_bot_aim_state *state,qa_vec3 direction,const qa_bot_aiming *s,float dt,double now)
{
    if (dt<=0) return;
    qa_vec3 ideal=view_angles(direction);
    float pitch_error=angle_delta(state->pitch,ideal.x),yaw_error=angle_delta(state->yaw,ideal.y);
    if (s->modifier_apply_time>0 && s->modifier_maximum_angle>0 && fmaxf(fabsf(pitch_error),fabsf(yaw_error))>s->modifier_maximum_angle)
        state->modifier_until=now+s->modifier_apply_time;
    bool modified=state->modifier_until>now;
    float max_speed=s->maximum_speed*(modified ? s->modifier_acceleration_scalar : 1),
        stiffness=s->spring_stiffness*(modified ? s->modifier_spring_scalar : 1),damping=s->damping*(modified ? s->modifier_damping_scalar : 1);
    unsigned steps=(unsigned)clamp(ceilf(dt*sqrtf(fmaxf(stiffness,1))/0.25f),1,64);
    float h=dt/(float)steps;
    advance(&state->pitch,&state->pitch_velocity,pitch_error,stiffness,damping,max_speed,h,steps);
    advance(&state->yaw,&state->yaw_velocity,yaw_error,stiffness,damping,max_speed,h,steps);
    float clamped=clamp(state->pitch,-80,80);
    if (state->pitch!=clamped) { state->pitch=clamped; state->pitch_velocity=0; }
    state->yaw=angle_mod(state->yaw);
}
float qa_bot_aim_error(const qa_bot_aim_state *state,qa_vec3 direction)
{
    qa_vec3 ideal=view_angles(direction); return fmaxf(fabsf(angle_delta(state->pitch,ideal.x)),fabsf(angle_delta(state->yaw,ideal.y)));
}
