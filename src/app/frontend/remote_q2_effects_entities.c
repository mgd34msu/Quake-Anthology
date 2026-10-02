#include "remote_q2_effects_private.h"
#include <math.h>
#include <stdio.h>

static uint32_t draw(frontend_remote_q2_effects *o) { return qa_builtin_random_integer(&o->random); }
static float unit(frontend_remote_q2_effects *o) { return (float)(draw(o)&32767)/32767; }
static float signed_unit(frontend_remote_q2_effects *o) { return unit(o)*2-1; }
static qa_vec3 random_direction(frontend_remote_q2_effects *o)
{
    float x=signed_unit(o),y=signed_unit(o),z=signed_unit(o);
    return qa_vec_normalize(qa_v3(x,y,z));
}
static bool rerelease(const frontend_remote_q2_effects *o)
{ return o->source.protocol.kind==QA_NET_Q2KEX_2023 || o->source.protocol.kind==QA_NET_Q2REPRO_1038 ||
    o->source.protocol.kind==QA_NET_Q2PRIVATE_4038 || o->source.protocol.kind==QA_NET_Q2KEX_DEMO_2022; }
static void append(frontend_remote_q2_effects *o, frontend_fx_q2_particle p)
{ o->particles.values.q2[o->particles.count++]=p; }
static bool play(frontend_remote_q2_effects *o,const frontend_remote_q2_effects_pose *p,
    const char *path,double time,int32_t channel,float volume,float attenuation,qa_error *e)
{ return o->source.sound(o->source.context,path,p->origin,p->actor,time,channel,volume,attenuation,0,e) && q2fx_source_current(o,e); }
static void barrel(frontend_remote_q2_effects *o,qa_vec3 origin,double seconds)
{
    static const qa_vec3 offsets[6]={{-10,0,40},{10,0,40},{0,16,30},{16,0,25},{0,-16,20},{-16,0,15}};
    static const qa_vec3 dirs[6]={{0,0,1},{0,0,1},{0,1,0},{1,0,0},{0,-1,0},{-1,0,0}};
    static const uint32_t colors[4]={52,64,96,112};
    for (size_t i=0;i<6;++i) for (size_t j=0;j<4;++j)
        (void)frontend_fx_q2_steam(&o->particles,&o->random,qa_vec_add(origin,offsets[i]),dirs[i],colors[j],5,40,seconds,true);
}
static void teleporter2(frontend_remote_q2_effects *o,qa_vec3 origin,double time)
{
    for (unsigned i=0;i<8 && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;++i) {
        qa_vec3 dir=random_direction(o);
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xdb,.alpha=1,.alpha_velocity=-.8f,
            .origin=qa_vec_add(origin,qa_vec_scale(dir,30)),.velocity=qa_vec_scale(dir,-25)};
        p.origin.z+=20; append(o,p);
    }
}
bool frontend_remote_q2_effects_frame(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_sample *s,qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !s || !isfinite(s->milliseconds) ||
        !isfinite(s->server_milliseconds) || !isfinite(s->footsteps) || (s->entity_count && !s->entities) || !q2fx_source_current(o,e)) return false;
    if (o->event_received && o->event_sequence==s->frame_sequence) {
        if (o->event_failed && e) *e=o->event_error;
        return !o->event_failed;
    }
    for (size_t i=0;i<s->entity_count;++i)
        if (!s->entities[i].actor.registry || !s->entities[i].actor.generation || !qa_vec_finite(s->entities[i].origin)) return false;
    o->event_received=true; o->event_sequence=s->frame_sequence; o->event_failed=false; o->event_error=(qa_error){0};
    ++o->busy; bool ok=true;
    double seconds=s->milliseconds*.001;
    for (size_t i=0;ok && i<s->entity_count;++i) {
        const frontend_remote_q2_effects_pose *p=&s->entities[i];
        if (p->effects&UINT64_C(0x20000)) frontend_fx_q2_teleporter(&o->particles,&o->random,p->origin,seconds);
        if (rerelease(o)) {
            if (p->effects&(UINT64_C(1)<<36)) teleporter2(o,p->origin,s->milliseconds);
            if (p->effects&(UINT64_C(1)<<35)) barrel(o,p->origin,seconds);
        }
        switch (p->event) {
        case 1:
            ok=play(o,p,"items/respawn1.wav",s->milliseconds,1,1,2,e);
            if (ok) frontend_fx_q2_respawn_particles(&o->particles,&o->random,p->origin,seconds,FRONTEND_FX_Q2_ITEM);
            break;
        case 6:
            ok=play(o,p,"misc/tele1.wav",s->milliseconds,1,1,2,e);
            if (ok) frontend_fx_q2_teleport(&o->particles,&o->random,p->origin,seconds);
            break;
        case 2: case 8: case 9:
            if (!s->footsteps || (p->event!=2 && !rerelease(o))) break;
            if (rerelease(o)) {
                ok=o->source.footstep && o->source.footstep(o->source.context,p,p->event,s->milliseconds,&o->random,e) && q2fx_source_current(o,e);
                if (!ok && (!e || e->code==QA_OK)) q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 rerelease footstep has no actual material sound owner");
            } else {
                char path[48]; snprintf(path,sizeof(path),"player/step%u.wav",(draw(o)&3)+1);
                ok=play(o,p,path,s->milliseconds,4,1,1,e);
            }
            break;
        case 3: ok=play(o,p,"player/land1.wav",s->milliseconds,0,1,1,e); break;
        case 4: ok=play(o,p,"*fall2.wav",s->milliseconds,0,1,1,e); break;
        case 5: ok=play(o,p,"*fall1.wav",s->milliseconds,0,1,1,e); break;
        default: break;
        }
    }
    --o->busy; o->dirty=true;
    if (ok) ok=q2fx_source_current(o,e);
    if (!ok) {
        o->event_failed=true;
        if (!e || e->code==QA_OK) q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 received frame effect failed in its actual source owner");
        if (e) o->event_error=*e;
        else q2fx_fail(&o->event_error,QA_ERROR_ARGUMENT,"Q2 received frame effect failed in its actual source owner");
    }
    return ok;
}
static void scattered_trail(frontend_remote_q2_effects *o,qa_vec3 start,qa_vec3 end,double time,uint32_t color,bool tag)
{
    qa_vec3 delta=qa_vec_sub(end,start), direction=qa_vec_normalize(delta); double length=qa_vec_length(delta);
    for (double d=0;(tag?d<=length:d<length) && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=color,.alpha=1,.alpha_velocity=-1/(.8f+unit(o)*.2f)};
        qa_vec3 point=qa_vec_add(start,qa_vec_scale(direction,(float)d));
        p.origin.x=point.x+signed_unit(o)*16; p.velocity.x=signed_unit(o)*5;
        p.origin.y=point.y+signed_unit(o)*16; p.velocity.y=signed_unit(o)*5;
        p.origin.z=point.z+signed_unit(o)*16; p.velocity.z=signed_unit(o)*5; append(o,p);
    }
}
static void ion_trail(frontend_remote_q2_effects *o,qa_vec3 start,qa_vec3 end,double time)
{
    qa_vec3 delta=qa_vec_sub(end,start), direction=qa_vec_normalize(delta); double length=qa_vec_length(delta);
    unsigned index=0;
    for (double d=0;d<length && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5,++index) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.origin=qa_vec_add(start,qa_vec_scale(direction,(float)d)),
            .alpha=.5f,.alpha_velocity=-1/(.3f+unit(o)*.2f),.color=0xe4+(draw(o)&3),.velocity={index&1?10:-10,0,0}};
        append(o,p);
    }
}
static void orbital(frontend_remote_q2_effects *o,qa_vec3 origin,double time,int32_t count,bool bfg)
{
    if (o->particles.angular[0].x==0) for (unsigned i=0;i<QA_BYTE_NORMAL_COUNT;++i) {
        float x=(float)(draw(o)&255)*.01f,y=(float)(draw(o)&255)*.01f,z=(float)(draw(o)&255)*.01f;
        o->particles.angular[i]=qa_v3(x,y,z);
    }
    double seconds=time*.001;
    for (int32_t i=0;i<count && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;i+=bfg?1:2) {
        qa_vec3 normal; if (!qa_byte_normal((uint8_t)i,&normal)) return;
        double yaw=seconds*o->particles.angular[i].x,pitch=seconds*o->particles.angular[i].y;
        qa_vec3 forward=qa_v3((float)(cos(pitch)*cos(yaw)),(float)(cos(pitch)*sin(yaw)),(float)-sin(pitch));
        qa_vec3 offset=qa_vec_add(qa_vec_scale(normal,(float)(sin(seconds+i)*64)),qa_vec_scale(forward,16));
        float distance=qa_vec_length(offset)/90;
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.origin=qa_vec_add(origin,offset),
            .alpha=bfg?1-distance:1,.alpha_velocity=-100,.color=bfg?(uint32_t)floorf(0xd0+distance*7):0};
        append(o,p);
    }
}
static void trap(frontend_remote_q2_effects *o,qa_vec3 origin,double time)
{
    qa_vec3 start=origin; start.z-=14;
    for (unsigned d=0;d<64 && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xe0,.alpha=1,.alpha_velocity=-1/(.3f+unit(o)*.2f),.acceleration={0,0,40}};
        p.origin.x=start.x+signed_unit(o); p.velocity.x=signed_unit(o)*15;
        p.origin.y=start.y+signed_unit(o); p.velocity.y=signed_unit(o)*15;
        p.origin.z=start.z+(float)d+signed_unit(o); p.velocity.z=signed_unit(o)*15; append(o,p);
    }
    for (int i=-2;i<=2;i+=4) for (int j=-2;j<=2;j+=4) for (int k=-2;k<=4;k+=4) {
        if (o->particles.count==FRONTEND_FX_PARTICLE_CAPACITY) return;
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xe0+(draw(o)&3),.alpha=1,.acceleration={0,0,-40}};
        p.alpha_velocity=-1/(.3f+(float)(draw(o)&7)*.02f);
        uint32_t scatter=draw(o)&23; p.origin.x=origin.x+(float)i+(float)scatter*signed_unit(o);
        scatter=draw(o)&23; p.origin.y=origin.y+(float)j+(float)scatter*signed_unit(o);
        scatter=draw(o)&23; p.origin.z=origin.z+(float)k+(float)scatter*signed_unit(o);
        p.velocity=qa_vec_scale(qa_vec_normalize(qa_v3((float)(j*8),(float)(i*8),(float)(k*8))),(float)((50+draw(o))&63)); append(o,p);
    }
}
static void hologram(frontend_remote_q2_effects *o,qa_vec3 origin,double time)
{
    double angle=time*.03*.017453292519943295, sp=sin(angle),cp=cos(angle);
    qa_vec3 axis[3]={qa_v3((float)(cp*cp),(float)(cp*sp),(float)-sp),qa_v3((float)-sp,(float)cp,0),qa_v3((float)(sp*cp),(float)(sp*sp),(float)cp)};
    for (unsigned i=0;i<QA_BYTE_NORMAL_COUNT && o->particles.count<FRONTEND_FX_PARTICLE_CAPACITY;++i) {
        qa_vec3 n; if (!qa_byte_normal((uint8_t)i,&n)) return;
        qa_vec3 dir=qa_vec_add(qa_vec_add(qa_vec_scale(axis[0],n.x),qa_vec_scale(axis[1],n.y)),qa_vec_scale(axis[2],n.z));
        append(o,(frontend_fx_q2_particle){.spawn_milliseconds=time,.origin=qa_vec_add(origin,qa_vec_scale(dir,100)),.color=0xd0,.alpha=1,.alpha_velocity=-10000});
    }
}
bool q2fx_entities(frontend_remote_q2_effects *o,const frontend_remote_q2_effects_sample *s,
    q2fx_trail *trails,bool advance,qa_error *e)
{
    (void)e; double seconds=s->milliseconds*.001;
    frontend_fx_particles *p=&o->particles; qa_builtin_random *r=&o->random;
    for (size_t i=0;i<s->entity_count;++i) {
        const frontend_remote_q2_effects_pose *row=&s->entities[i]; const q2fx_trail *prior=NULL;
        for (size_t j=0;j<o->trail_count;++j) if (qa_actor_id_equal(o->trails[j].actor,row->actor)) { prior=&o->trails[j]; break; }
        qa_vec3 delta=prior?qa_vec_sub(prior->origin,row->origin):qa_v3(0,0,0);
        bool reset=!prior || fabsf(delta.x)>512 || fabsf(delta.y)>512 || fabsf(delta.z)>512 || row->event==6 || row->event==7;
        qa_vec3 start=reset?row->origin:prior->origin; int32_t count=reset?1024:prior->count;
        double fly_end=prior?prior->fly_end:0; uint64_t flags=row->effects;
        if (flags&UINT64_C(0x800000)) {
            double yaw=(s->milliseconds*.5+row->angles.y)*.017453292519943295;
            q2fx_sampled_light(o,qa_vec_add(row->origin,qa_v3((float)cos(yaw)*64,(float)sin(yaw)*64,0)),100,qa_v3(1,0,0),0);
        }
        if (flags&(UINT64_C(1)<<37)) q2fx_sampled_light(o,row->origin,100,qa_v3(1,1,0),0);
        if (qa_actor_id_equal(row->actor,s->viewer)) {
            if (flags&0x40000) q2fx_sampled_light(o,row->origin,225,qa_v3(1,.1f,.1f),0);
            else if (flags&0x80000) q2fx_sampled_light(o,row->origin,225,qa_v3(.1f,.1f,1),0);
            else if (flags&0x20000000) q2fx_sampled_light(o,row->origin,225,qa_v3(1,1,0),0);
            else if (flags&UINT64_C(0x80000000)) q2fx_sampled_light(o,row->origin,225,qa_v3(-1,-1,-1),0);
        } else if (row->model_index) {
            if (advance && (flags&(UINT64_C(1)<<33))) hologram(o,row->origin,s->milliseconds);
            if (flags&0x10) { if (advance) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_ROCKET); q2fx_sampled_light(o,row->origin,200,qa_v3(1,1,0),0); }
            else if (flags&8) { bool tracker=(flags&0x04000000)!=0; if (advance) frontend_fx_q2_blaster_trail(p,r,start,row->origin,seconds,tracker); q2fx_sampled_light(o,row->origin,200,qa_v3(tracker?0:1,1,0),0); }
            else if (flags&0x40) q2fx_sampled_light(o,row->origin,200,qa_v3(flags&0x04000000?0:1,1,0),0);
            else if (flags&2) { if (advance) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_BLOOD); }
            else if (flags&0x20) { if (advance) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_SMOKE); }
            else if (flags&0x4000) {
                if (fly_end<s->milliseconds) fly_end=s->milliseconds+60000;
                double elapsed=s->milliseconds-(fly_end-60000),remaining=fly_end-s->milliseconds;
                int32_t n=(int32_t)(elapsed<20000?elapsed*162/20000:remaining<20000?remaining*162/20000:162);
                if (advance) orbital(o,row->origin,s->milliseconds,n<0?0:n>162?162:n,false);
            } else if (flags&0x80) {
                static const float radii[]={300,400,600,300,150,75};
                if (advance && (flags&0x2000)) orbital(o,row->origin,s->milliseconds,162,true);
                q2fx_sampled_light(o,row->origin,flags&0x2000?200:row->frame>=0 && row->frame<6?radii[row->frame]:0,qa_v3(0,1,0),0);
            } else if (flags&0x02000000) {
                qa_vec3 point=row->origin; point.z+=32; if (advance) trap(o,point,s->milliseconds);
                q2fx_sampled_light(o,point,(float)(draw(o)%100+100),qa_v3(1,.8f,.1f),0);
            } else if (flags&0x40000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,242,false); q2fx_sampled_light(o,row->origin,225,qa_v3(1,.1f,.1f),0); }
            else if (flags&0x80000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,115,false); q2fx_sampled_light(o,row->origin,225,qa_v3(.1f,.1f,1),0); }
            else if (flags&0x20000000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,220,true); q2fx_sampled_light(o,row->origin,225,qa_v3(1,1,0),0); }
            else if (flags&UINT64_C(0x80000000)) {
                if (flags&0x04000000) q2fx_sampled_light(o,row->origin,(float)(50+500*(sin(seconds*2)+1)),qa_v3(-1,-1,-1),0);
                else { if (advance) frontend_fx_q2_tracker_shell(p,r,start,seconds); q2fx_sampled_light(o,row->origin,155,qa_v3(-1,-1,-1),0); }
            } else if (flags&0x04000000) { if (advance) frontend_fx_q2_tracker_trail(p,r,start,row->origin,seconds); q2fx_sampled_light(o,row->origin,200,qa_v3(-1,-1,-1),0); }
            else if (flags&0x00200000) { if (advance) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_GREEN_BLOOD); }
            else if (flags&0x00100000) { if (advance) ion_trail(o,start,row->origin,s->milliseconds); q2fx_sampled_light(o,row->origin,100,qa_v3(1,.5f,.5f),0); }
            else if (flags&0x00400000) q2fx_sampled_light(o,row->origin,200,qa_v3(0,0,1),0);
            else if (flags&0x01000000) { if (advance && (flags&0x2000)) frontend_fx_q2_blaster_trail(p,r,start,row->origin,seconds,false); q2fx_sampled_light(o,row->origin,130,qa_v3(1,.5f,.5f),0); }
        }
        trails[i]=(q2fx_trail){row->actor,row->origin,count,fly_end};
    }
    return true;
}
