#include "q2_entity_effects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
static uint32_t draw(frontend_q2_entity_effects *o) { return qa_builtin_random_integer(o->random); }
static float unit(frontend_q2_entity_effects *o) { return (float)(draw(o)&32767)/32767; }
static float signed_unit(frontend_q2_entity_effects *o) { return unit(o)*2-1; }
static bool rerelease(const frontend_q2_entity_effects *o) { return o->rerelease; }
static void append(frontend_q2_entity_effects *o, frontend_fx_q2_particle p)
{ o->particles->values.q2[o->particles->count++]=p; }
static void light(frontend_q2_entity_effects *o,qa_vec3 origin,float radius,qa_vec3 color,float minimum)
{
    if (radius>0) {
        qa_scene_light row={.origin=origin,.radius=radius,.color=color,.minimum=minimum,
            .scale=1,.additive=true,.family=QA_SCENE_Q2};
        o->light(o->context,&row);
    }
}

void frontend_q2_effect_particles_initialize(frontend_fx_particles *particles,qa_builtin_random *random,bool is_rerelease)
{
    particles->family=QA_GAME_Q2;
    if (is_rerelease) for (size_t i=0;i<QA_BYTE_NORMAL_COUNT;++i) {
        float x=(float)(qa_builtin_random_integer(random)&255)*.01f;
        float y=(float)(qa_builtin_random_integer(random)&255)*.01f;
        float z=(float)(qa_builtin_random_integer(random)&255)*.01f;
        particles->angular[i]=qa_v3(x,y,z);
    }
}
qa_vec3 frontend_q2_effect_random_direction(qa_builtin_random *random,bool is_rerelease)
{
    float x,y,z;
    if (is_rerelease) {
        float square;
        do {
            x=(float)(2*((double)(qa_builtin_random_integer(random)&32767)/32767)-1);
            y=(float)(2*((double)(qa_builtin_random_integer(random)&32767)/32767)-1);
            square=x*x+y*y;
        } while (square>1);
        float scale=2*sqrtf(1-square);
        return qa_v3(x*scale,y*scale,-1+2*square);
    }
    x=(float)(2*((double)(qa_builtin_random_integer(random)&32767)/32767)-1);
    y=(float)(2*((double)(qa_builtin_random_integer(random)&32767)/32767)-1);
    z=(float)(2*((double)(qa_builtin_random_integer(random)&32767)/32767)-1);
    return qa_vec_normalize(qa_v3(x,y,z));
}
bool frontend_q2_entity_cache_reserve(frontend_q2_entity_cache *cache,uint32_t slot,qa_error *error)
{
    size_t required=(size_t)slot+1;
    if (required<=cache->capacity) return true;
    size_t capacity=cache->capacity?cache->capacity:128;
    while (capacity<required) {
        if (capacity>SIZE_MAX/2) { capacity=required; break; }
        capacity*=2;
    }
    if (capacity>SIZE_MAX/sizeof(*cache->rows)) return fail(error,QA_ERROR_MEMORY,"Q2 entity trail storage overflows");
    frontend_q2_entity_trail *rows=realloc(cache->rows,capacity*sizeof(*rows));
    if (!rows) return fail(error,QA_ERROR_MEMORY,"Retaining Q2 entity trail slots");
    memset(rows+cache->capacity,0,(capacity-cache->capacity)*sizeof(*rows));
    cache->rows=rows;cache->capacity=capacity;return true;
}
static void barrel(frontend_q2_entity_effects *o,qa_vec3 origin,double seconds)
{
    static const qa_vec3 offsets[6]={{-10,0,40},{10,0,40},{0,16,30},{16,0,25},{0,-16,20},{-16,0,15}};
    static const qa_vec3 dirs[6]={{0,0,1},{0,0,1},{0,1,0},{1,0,0},{0,-1,0},{-1,0,0}};
    static const uint32_t colors[4]={52,64,96,112};
    for (size_t i=0;i<6;++i) for (size_t j=0;j<4;++j)
        (void)frontend_fx_q2_steam(o->particles,o->random,qa_vec_add(origin,offsets[i]),dirs[i],colors[j],5,40,seconds,true);
}
static void teleporter2(frontend_q2_entity_effects *o,qa_vec3 origin,double time)
{
    for (unsigned i=0;i<8 && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;++i) {
        qa_vec3 dir=frontend_q2_effect_random_direction(o->random,o->rerelease);
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xdb,.alpha=1,.alpha_velocity=-.8f,
            .origin=qa_vec_add(origin,qa_vec_scale(dir,30)),.velocity=qa_vec_scale(dir,-25)};
        p.origin.z+=20; append(o,p);
    }
}
void frontend_q2_entity_frame_particles(frontend_q2_entity_effects *o,const frontend_q2_entity_pose *p,double milliseconds)
{
    if (p->effects&UINT64_C(0x20000)) frontend_fx_q2_teleporter(o->particles,o->random,p->origin,milliseconds*.001);
    if (rerelease(o)) {
        if (p->effects&(UINT64_C(1)<<36)) teleporter2(o,p->origin,milliseconds);
        if (p->effects&(UINT64_C(1)<<35)) barrel(o,p->origin,milliseconds*.001);
    }
}
static void scattered_trail(frontend_q2_entity_effects *o,qa_vec3 start,qa_vec3 end,double time,uint32_t color,bool tag)
{
    qa_vec3 delta=qa_vec_sub(end,start), direction=qa_vec_normalize(delta); double length=qa_vec_length(delta);
    for (double d=0;(tag?d<=length:d<length) && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=color,.alpha=1,.alpha_velocity=-1/(.8f+unit(o)*.2f)};
        qa_vec3 point=qa_vec_add(start,qa_vec_scale(direction,(float)d));
        p.origin.x=point.x+signed_unit(o)*16; p.velocity.x=signed_unit(o)*5;
        p.origin.y=point.y+signed_unit(o)*16; p.velocity.y=signed_unit(o)*5;
        p.origin.z=point.z+signed_unit(o)*16; p.velocity.z=signed_unit(o)*5; append(o,p);
    }
}
static void ion_trail(frontend_q2_entity_effects *o,qa_vec3 start,qa_vec3 end,double time)
{
    qa_vec3 delta=qa_vec_sub(end,start), direction=qa_vec_normalize(delta); double length=qa_vec_length(delta);
    unsigned index=0;
    for (double d=0;d<length && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5,++index) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.origin=qa_vec_add(start,qa_vec_scale(direction,(float)d)),
            .alpha=.5f,.alpha_velocity=-1/(.3f+unit(o)*.2f),.color=0xe4+(draw(o)&3),.velocity={index&1?10:-10,0,0}};
        append(o,p);
    }
}
static void orbital(frontend_q2_entity_effects *o,qa_vec3 origin,double time,int32_t count,bool bfg)
{
    if (!rerelease(o) && o->particles->angular[0].x==0) for (unsigned i=0;i<QA_BYTE_NORMAL_COUNT;++i) {
        float x=(float)(draw(o)&255)*.01f,y=(float)(draw(o)&255)*.01f,z=(float)(draw(o)&255)*.01f;
        o->particles->angular[i]=qa_v3(x,y,z);
    }
    double seconds=time*.001;
    for (int32_t i=0;i<count && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;i+=bfg?1:2) {
        qa_vec3 normal; if (!qa_byte_normal((uint8_t)i,&normal)) return;
        double yaw=seconds*o->particles->angular[i].x,pitch=seconds*o->particles->angular[i].y;
        qa_vec3 forward=qa_v3((float)(cos(pitch)*cos(yaw)),(float)(cos(pitch)*sin(yaw)),(float)-sin(pitch));
        qa_vec3 offset=qa_vec_add(qa_vec_scale(normal,(float)(sin(seconds+i)*64)),qa_vec_scale(forward,16));
        float distance=qa_vec_length(offset)/90;
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.origin=qa_vec_add(origin,offset),
            .alpha=bfg?1-distance:1,.alpha_velocity=-100,.color=bfg?(uint32_t)floorf(0xd0+distance*7):0};
        append(o,p);
    }
}
static void trap(frontend_q2_entity_effects *o,qa_vec3 origin,double time)
{
    qa_vec3 start=origin; start.z-=14;
    for (unsigned d=0;d<64 && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;d+=5) {
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xe0,.alpha=1,.alpha_velocity=-1/(.3f+unit(o)*.2f),.acceleration={0,0,40}};
        p.origin.x=start.x+signed_unit(o); p.velocity.x=signed_unit(o)*15;
        p.origin.y=start.y+signed_unit(o); p.velocity.y=signed_unit(o)*15;
        p.origin.z=start.z+(float)d+signed_unit(o); p.velocity.z=signed_unit(o)*15; append(o,p);
    }
    for (int i=-2;i<=2;i+=4) for (int j=-2;j<=2;j+=4) for (int k=-2;k<=4;k+=4) {
        if (o->particles->count==FRONTEND_FX_PARTICLE_CAPACITY) return;
        frontend_fx_q2_particle p={.spawn_milliseconds=time,.color=0xe0+(draw(o)&3),.alpha=1,.acceleration={0,0,-40}};
        p.alpha_velocity=-1/(.3f+(float)(draw(o)&7)*.02f);
        uint32_t scatter=draw(o)&23; p.origin.x=origin.x+(float)i+(float)scatter*signed_unit(o);
        scatter=draw(o)&23; p.origin.y=origin.y+(float)j+(float)scatter*signed_unit(o);
        scatter=draw(o)&23; p.origin.z=origin.z+(float)k+(float)scatter*signed_unit(o);
        p.velocity=qa_vec_scale(qa_vec_normalize(qa_v3((float)(j*8),(float)(i*8),(float)(k*8))),(float)((50+draw(o))&63)); append(o,p);
    }
}
static void hologram(frontend_q2_entity_effects *o,qa_vec3 origin,double time)
{
    double angle=time*.03*.017453292519943295, sp=sin(angle),cp=cos(angle);
    qa_vec3 axis[3]={qa_v3((float)(cp*cp),(float)(cp*sp),(float)-sp),qa_v3((float)-sp,(float)cp,0),qa_v3((float)(sp*cp),(float)(sp*sp),(float)cp)};
    for (unsigned i=0;i<QA_BYTE_NORMAL_COUNT && o->particles->count<FRONTEND_FX_PARTICLE_CAPACITY;++i) {
        qa_vec3 n; if (!qa_byte_normal((uint8_t)i,&n)) return;
        qa_vec3 dir=qa_vec_add(qa_vec_add(qa_vec_scale(axis[0],n.x),qa_vec_scale(axis[1],n.y)),qa_vec_scale(axis[2],n.z));
        append(o,(frontend_fx_q2_particle){.spawn_milliseconds=time,.origin=qa_vec_add(origin,qa_vec_scale(dir,100)),.color=0xd0,.alpha=1,.alpha_velocity=-10000});
    }
}
static int32_t fireball_trail(frontend_q2_entity_effects *o,qa_vec3 *start,
    qa_vec3 end,double time,int32_t count)
{
    qa_vec3 delta=qa_vec_sub(end,*start),step=qa_vec_scale(qa_vec_normalize(delta),.5f),move=*start;
    double steps=floor((double)qa_vec_length(delta)*2);
    float origin_scale=count>900?4:count>800?2:1,velocity_scale=count>900?15:count>800?10:5;
    for (double i=0;i<steps;++i) {
        if ((int32_t)(draw(o)&1023)<count) {
            if (o->particles->count==FRONTEND_FX_PARTICLE_CAPACITY) break;
            frontend_fx_q2_particle p={.spawn_milliseconds=time,.alpha=1,.acceleration={0,0,20}};
            p.alpha_velocity=-1/(1+unit(o)*.4f);
            p.origin.x=move.x+signed_unit(o)*origin_scale; p.velocity.x=signed_unit(o)*velocity_scale;
            p.origin.y=move.y+signed_unit(o)*origin_scale; p.velocity.y=signed_unit(o)*velocity_scale;
            p.origin.z=move.z+signed_unit(o)*origin_scale; p.velocity.z=signed_unit(o)*velocity_scale;
            p.color=0xd8u+(uint32_t)((1024-count)/64); append(o,p);
        }
        count=count>105?count-5:100; move=qa_vec_add(move,step);
    }
    *start=move; return count;
}
bool frontend_q2_entity_effect(frontend_q2_entity_effects *o,const frontend_q2_entity_effect_view *s,
    const frontend_q2_entity_pose *row,frontend_q2_entity_trail *state,bool advance,qa_error *e)
{
    double seconds=s->milliseconds*.001;
    frontend_fx_particles *p=o->particles; qa_builtin_random *r=o->random;
    const frontend_q2_entity_trail *prior=qa_actor_id_equal(state->actor,row->actor) &&
        state->model_index==row->model_index &&
        (row->model_index || state->model_identity==row->model_identity)?state:NULL;
        qa_vec3 delta=prior?qa_vec_sub(prior->origin,row->origin):qa_v3(0,0,0);
        bool reset=!prior || fabsf(delta.x)>512 || fabsf(delta.y)>512 || fabsf(delta.z)>512 || row->event==6 || row->event==7;
        qa_vec3 start=reset?row->origin:prior->origin,retained_origin=row->origin; int32_t count=reset?1024:prior->count;
        double fly_end=prior?prior->fly_end:0; uint64_t flags=row->effects;
        float flashlight_fraction=prior?prior->flashlight_fraction:1;
        if (flags&UINT64_C(0x800000)) {
            double yaw=(s->milliseconds*.5+row->angles.y)*.017453292519943295;
            light(o,qa_vec_add(row->origin,qa_v3((float)cos(yaw)*64,(float)sin(yaw)*64,0)),100,qa_v3(1,0,0),0);
        }
        if (flags&(UINT64_C(1)<<37)) light(o,row->origin,100,qa_v3(1,1,0),0);
        if (flags&(UINT64_C(1)<<34)) {
            bool self=qa_actor_id_equal(row->actor,s->viewer);
            float pitch=row->angles.x*.017453292519943295f,yaw=row->angles.y*.017453292519943295f;
            qa_vec3 forward=self?s->view.axis[0]:qa_v3(cosf(pitch)*cosf(yaw),cosf(pitch)*sinf(yaw),-sinf(pitch));
            qa_vec3 point=self?s->view.origin:row->origin;
            qa_trace_query query={.start=point,.end=qa_vec_add(point,qa_vec_scale(forward,self || !s->per_pixel_lighting?256:1024)),
                .shape={.kind=QA_SHAPE_POINT},.policy={.family=QA_COLLISION_Q2,
                    .contents_mask=s->per_pixel_lighting?1u:1u|UINT32_C(0x02000000)|UINT32_C(0x40000000)},.pass_actor=row->actor};
            qa_trace_result hit;
            if (!o->trace)
                return fail(e,QA_ERROR_ARGUMENT,"Q2 flashlight lost its actual private collision owner");
            if (!o->trace(o->context,&query,&hit,e)) return false;
            if (!isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1)
                return fail(e,QA_ERROR_FORMAT,"Q2 flashlight collision fraction leaves its source interval");
            if (s->per_pixel_lighting) {
                if (self && s->hand!=2) point=qa_vec_add(point,qa_vec_scale(s->view.axis[1],s->hand==1?7:-7));
                qa_scene_light spot={.origin=point,.color={2,2,2},.direction=forward,
                    .radius=512,.scale=1,.cos_half_angle=cosf(22*.017453292519943295f),.additive=true,
                    .spot=true,.casts_shadow=true,.shadow_resolution=512,.family=QA_SCENE_Q2};
                o->light(o->context,&spot);
            } else {
                light(o,qa_vec_lerp(query.start,query.end,flashlight_fraction),256,qa_v3(1,1,1),0);
                if (advance) {
                    float fraction_delta=hit.fraction-flashlight_fraction;
                    flashlight_fraction+=fminf(s->frame_seconds,fmaxf(-s->frame_seconds,fraction_delta));
                }
            }
        }
        if (qa_actor_id_equal(row->actor,s->viewer)) {
            if (flags&0x40000) light(o,row->origin,225,qa_v3(1,.1f,.1f),0);
            else if (flags&0x80000) light(o,row->origin,225,qa_v3(.1f,.1f,1),0);
            else if (flags&0x20000000) light(o,row->origin,225,qa_v3(1,1,0),0);
            else if (flags&UINT64_C(0x80000000)) light(o,row->origin,225,qa_v3(-1,-1,-1),0);
        } else if (row->model_index || row->model_present) {
            if (advance && (flags&(UINT64_C(1)<<33))) hologram(o,row->origin,s->milliseconds);
            if (flags&0x10) {
                if (advance) {
                    if (rerelease(o) && (flags&2)) { count=fireball_trail(o,&start,row->origin,s->milliseconds,count); retained_origin=start; }
                    else if (!(o->disable_particles&8)) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_ROCKET);
                }
                else if (rerelease(o) && (flags&2)) retained_origin=start;
                light(o,row->origin,200,o->dlight_hacks&1?qa_v3(1,.23f,0):qa_v3(1,1,0),0);
            }
            else if (flags&8) { bool tracker=(flags&0x04000000)!=0; if (advance && !(o->disable_particles&32)) frontend_fx_q2_blaster_trail(p,r,start,row->origin,seconds,tracker); light(o,row->origin,200,qa_v3(tracker?0:1,1,0),0); }
            else if (flags&0x40) light(o,row->origin,200,qa_v3(flags&0x04000000?0:1,1,0),0);
            else if (flags&2) { if (advance && !(o->disable_particles&16)) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_BLOOD); }
            else if (flags&0x20) { if (advance && !(o->disable_particles&2)) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_SMOKE); }
            else if (flags&0x4000) {
                if (fly_end<s->milliseconds) fly_end=s->milliseconds+60000;
                double elapsed=s->milliseconds-(fly_end-60000),remaining=fly_end-s->milliseconds;
                int32_t n=(int32_t)(elapsed<20000?elapsed*162/20000:remaining<20000?remaining*162/20000:162);
                if (advance) orbital(o,row->origin,s->milliseconds,n<0?0:n>162?162:n,false);
            } else if (flags&0x80) {
                static const float radii[]={300,400,600,300,150,75};
                if (advance && (flags&0x2000)) orbital(o,row->origin,s->milliseconds,162,true);
                light(o,row->origin,flags&0x2000?200:row->frame>=0 && row->frame<6?radii[row->frame]:0,qa_v3(0,1,0),0);
            } else if (flags&0x02000000) {
                qa_vec3 point=row->origin; point.z+=32; if (advance) trap(o,point,s->milliseconds);
                light(o,point,(float)(draw(o)%100+100),qa_v3(1,.8f,.1f),0);
            } else if (flags&0x40000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,242,false); light(o,row->origin,225,qa_v3(1,.1f,.1f),0); }
            else if (flags&0x80000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,115,false); light(o,row->origin,225,qa_v3(.1f,.1f,1),0); }
            else if (flags&0x20000000) { if (advance) scattered_trail(o,start,row->origin,s->milliseconds,220,true); light(o,row->origin,225,qa_v3(1,1,0),0); }
            else if (flags&UINT64_C(0x80000000)) {
                if (flags&0x04000000) light(o,row->origin,(float)(50+500*(sin(seconds*2)+1)),qa_v3(-1,-1,-1),0);
                else { if (advance) frontend_fx_q2_tracker_shell(p,r,start,seconds); light(o,row->origin,155,qa_v3(-1,-1,-1),0); }
            } else if (flags&0x04000000) { if (advance) frontend_fx_q2_tracker_trail(p,r,start,row->origin,seconds); light(o,row->origin,200,qa_v3(-1,-1,-1),0); }
            else if (flags&0x00200000) { if (advance && !(o->disable_particles&16)) count=frontend_fx_q2_diminishing_trail(p,r,start,row->origin,seconds,count,FRONTEND_FX_Q2_GREEN_BLOOD); }
            else if (flags&0x00100000) { if (advance) ion_trail(o,start,row->origin,s->milliseconds); light(o,row->origin,100,qa_v3(1,.5f,.5f),0); }
            else if (flags&0x00400000) light(o,row->origin,200,qa_v3(0,0,1),0);
            else if (flags&0x01000000) { if (advance && (flags&0x2000)) frontend_fx_q2_blaster_trail(p,r,start,row->origin,seconds,false); light(o,row->origin,130,qa_v3(1,.5f,.5f),0); }
        }
    *state=(frontend_q2_entity_trail){.actor=row->actor,.origin=retained_origin,.count=count,
        .fly_end=fly_end,.flashlight_fraction=flashlight_fraction,.model_index=row->model_index,
        .model_identity=row->model_identity};
    return true;
}

bool frontend_q2_entity_beam(qa_builtin_random *random,qa_bytes palette,const qa_scene_image *white,
    const qa_scene_view *view,qa_vec3 start,qa_vec3 end,uint32_t packed_colors,int32_t width,
    qa_scene_frame *frame,qa_error *error)
{
    if (palette.size<768 || !palette.data) return fail(error,QA_ERROR_FORMAT,"Q2 entity beam requires the actual palette");
    uint32_t color=(packed_colors>>((qa_builtin_random_integer(random)%4)*8))&255;
    qa_scene_vec4 rgba={palette.data[color*3]/255.f,palette.data[color*3+1]/255.f,palette.data[color*3+2]/255.f,.3f};
    return qa_scene_beam(frame,view,start,end,(float)(width/2)*2,rgba,white,error);
}
