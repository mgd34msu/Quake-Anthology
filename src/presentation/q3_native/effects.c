/* id Software cg_effects.c; GPL-2.0-or-later. */
#include "events_internal.h"

q3n_local_entity *q3n_effect_smoke(const q3n_frame *f, const q3n_smoke *s)
{
    q3n_events *o=f->events;
    q3n_local_entity *v=q3n_local_allocate(o,Q3N_LE_MOVE_SCALE_FADE,QA_Q3_REF_SPRITE);
    v->flags=s->flags; v->radius=s->radius;
    o->smoke_seed=69069u*o->smoke_seed+1u;
    v->ref.rotation=q3ne_mul((float)(o->smoke_seed&65535u)/65536.0f,360);
    v->ref.radius=s->radius; v->ref.shader_time=q3ne_div((float)s->start_time,1000);
    v->start_time=s->start_time; v->fade_in_time=s->fade_in_time;
    v->end_time=q3ne_int(q3ne_add((float)v->start_time,s->duration));
    v->life_rate=q3ne_life(v->fade_in_time>v->start_time?v->fade_in_time:v->start_time,v->end_time);
    memcpy(v->color,s->color,sizeof(v->color));
    v->pos.type=2; v->pos.time=v->start_time;
    q3ne_store(v->pos.base,s->origin); q3ne_store(v->pos.delta,s->velocity);
    v->ref.origin=s->origin; v->ref.custom_shader=s->shader;
    const q3n_media_view *m=q3n_media_read(f->media);
    if(f->event_settings->ragepro) {
        v->ref.custom_shader=m->graphics[Q3N_G_SMOKE_RAGEPRO]; memset(v->ref.color,255,4);
    } else {
        for(unsigned i=0;i<3;++i) v->ref.color[i]=q3ne_byte(q3ne_mul(v->color[i],255));
        v->ref.color[3]=255;
    }
    return v;
}
bool q3n_effect_bubbles(const q3n_frame *f, qa_vec3 start, qa_vec3 end, float spacing, qa_error *error)
{
    q3n_events *o=f->events;
    if(f->event_settings->no_projectile_trail) return true;
    if(!isfinite(spacing) || spacing<1 || spacing>=2147483648.0f)
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Bubble spacing requires a positive source integer divisor");
    qa_vec3 difference=q3ne_difference(end,start), direction=q3ne_normalize(difference);
    float length=q3ne_length(difference);
    int32_t i=q3n_events_rand(o)%(int32_t)spacing;
    qa_vec3 move=q3ne_sum(start,q3ne_scale(direction,(float)i)), step=q3ne_scale(direction,spacing);
    for(; (float)i<length; i=q3ne_int(q3ne_add((float)i,spacing))) {
        q3n_local_entity *v=q3n_local_allocate(o,Q3N_LE_MOVE_SCALE_FADE,QA_Q3_REF_SPRITE);
        v->flags=Q3N_LE_DONT_SCALE; v->start_time=f->time;
        v->end_time=q3ne_int(q3ne_add((float)q3ne_plus(f->time,1000),q3ne_mul(q3n_events_random(o),250)));
        v->life_rate=q3ne_life(v->start_time,v->end_time);
        v->ref.shader_time=q3ne_div((float)f->time,1000); v->ref.radius=3;
        v->ref.custom_shader=q3n_media_read(f->media)->graphics[Q3N_G_WATER_BUBBLE];
        memset(v->ref.color,255,4); v->color[3]=1;
        v->pos.type=2; v->pos.time=f->time; q3ne_store(v->pos.base,move);
        v->pos.delta[0]=q3ne_mul(q3n_events_crandom(o),5);
        v->pos.delta[1]=q3ne_mul(q3n_events_crandom(o),5);
        v->pos.delta[2]=q3ne_add(q3ne_mul(q3n_events_crandom(o),5),6);
        move=q3ne_sum(move,step);
    }
    return true;
}
void q3n_effect_spawn(const q3n_frame *f, qa_vec3 origin)
{
    const q3n_media_view *m=q3n_media_read(f->media);
    q3n_local_entity *v=q3n_local_allocate(f->events,Q3N_LE_FADE_RGB,QA_Q3_REF_MODEL);
    v->start_time=f->time; v->end_time=q3ne_plus(f->time,500); v->life_rate=q3ne_life(v->start_time,v->end_time);
    for(unsigned i=0;i<4;++i)v->color[i]=1;
    v->ref.shader_time=q3ne_div((float)f->time,1000); q3ne_identity(v->ref.axis);
    v->ref.model=m->graphics[Q3N_G_TELEPORT_MODEL]; v->ref.origin=origin;
    qa_q3_product product = f->unified_effects ? f->unified_effects->product :
        f->effects_source ? f->effects_source->q3_product : q3n_frame_product(f);
    v->ref.origin.z=q3ne_add(origin.z,product==QA_Q3_ARENA?-24:16);
    if(product==QA_Q3_ARENA)v->ref.custom_shader=m->graphics[Q3N_G_TELEPORT_SHADER];
}
q3n_local_entity *q3n_effect_explosion(const q3n_frame *f, const q3n_explosion *s, qa_error *error)
{
    if(s->duration<=0 || (s->sprite && !s->has_direction)) {
        q3ne_fail(error,QA_ERROR_ARGUMENT,"CG_MakeExplosion requires positive duration and sprite direction"); return NULL;
    }
    int32_t offset=q3n_events_rand(f->events)&63;
    q3n_local_entity *v=q3n_local_allocate(f->events,s->sprite?Q3N_LE_SPRITE_EXPLOSION:Q3N_LE_EXPLOSION,
        s->sprite?QA_Q3_REF_SPRITE:QA_Q3_REF_MODEL);
    if(s->sprite) {
        v->ref.rotation=(float)(q3n_events_rand(f->events)%360);
        v->ref.origin=q3ne_sum(s->origin,q3ne_scale(s->direction,16));
    } else {
        if(!s->has_direction)q3ne_identity(v->ref.axis);
        else {
            int32_t angle=q3n_events_rand(f->events)%360;
            v->ref.axis[0]=s->direction;
            qa_vec3 p=q3ne_perpendicular(s->direction);
            v->ref.axis[1]=angle?q3ne_rotate(s->direction,p,(float)angle):p;
            v->ref.axis[2]=q3ne_cross(v->ref.axis[0],v->ref.axis[1]);
        }
        v->ref.origin=s->origin;
    }
    v->ref.old_origin=v->ref.origin; v->ref.model=s->model;
    v->start_time=q3ne_sub(f->time,offset); v->end_time=q3ne_plus(v->start_time,s->duration);
    v->ref.shader_time=q3ne_div((float)v->start_time,1000); v->ref.custom_shader=s->shader;
    v->color[0]=v->color[1]=v->color[2]=1;
    return v;
}
void q3n_effect_bleed(const q3n_frame *f, qa_vec3 origin, int32_t client)
{
    if(!f->event_settings->blood)return;
    q3n_local_entity *v=q3n_local_allocate(f->events,Q3N_LE_EXPLOSION,QA_Q3_REF_SPRITE);
    v->start_time=f->time; v->end_time=q3ne_plus(f->time,500);
    v->ref.origin=origin; v->ref.rotation=(float)(q3n_events_rand(f->events)%360); v->ref.radius=24;
    v->ref.custom_shader=q3n_media_read(f->media)->graphics[Q3N_G_BLOOD_EXPLOSION];
    const qa_q3_player *ps=q3n_frame_snapshot_player(f);
    if(ps && client==ps->clientNum)v->ref.flags|=2;
}
q3n_local_entity *q3n_effect_gib(const q3n_frame *f, qa_vec3 origin, qa_vec3 velocity, int32_t model)
{
    q3n_local_entity *v=q3n_local_allocate(f->events,Q3N_LE_FRAGMENT,QA_Q3_REF_MODEL);
    v->start_time=f->time;
    v->end_time=q3ne_int(q3ne_add((float)q3ne_plus(f->time,5000),q3ne_mul(q3n_events_random(f->events),3000)));
    v->ref.model=model; v->ref.origin=origin; q3ne_identity(v->ref.axis);
    v->pos.type=5; v->pos.time=f->time; q3ne_store(v->pos.base,origin); q3ne_store(v->pos.delta,velocity);
    v->bounce_factor=0.6f; v->bounce_sound=Q3N_BOUNCE_BLOOD; v->mark=Q3N_MARK_BLOOD;
    return v;
}
static qa_vec3 gib_velocity(q3n_events *o)
{
    float x=q3ne_mul(q3n_events_crandom(o),250), y=q3ne_mul(q3n_events_crandom(o),250);
    float z=q3ne_add(250,q3ne_mul(q3n_events_crandom(o),250)); return qa_v3(x,y,z);
}
void q3n_effect_gib_player(const q3n_frame *f, qa_vec3 origin)
{
    q3n_events *o=f->events; if(!f->event_settings->blood)return;
    const q3n_media_view *m=q3n_media_read(f->media);
    qa_vec3 first=gib_velocity(o); int32_t head=m->graphics[(q3n_events_rand(o)&1)?Q3N_G_GIB_SKULL:Q3N_G_GIB_BRAIN];
    q3n_effect_gib(f,origin,first,head); if(!f->event_settings->gibs)return;
    const q3n_graphic models[]={Q3N_G_GIB_ABDOMEN,Q3N_G_GIB_ARM,Q3N_G_GIB_CHEST,Q3N_G_GIB_FIST,
        Q3N_G_GIB_FOOT,Q3N_G_GIB_FOREARM,Q3N_G_GIB_INTESTINE,Q3N_G_GIB_LEG,Q3N_G_GIB_LEG};
    for(unsigned i=0;i<9;++i)q3n_effect_gib(f,origin,gib_velocity(o),m->graphics[models[i]]);
}
void q3n_effect_big_explode(const q3n_frame *f, qa_vec3 origin)
{
    q3n_events *o=f->events; if(!f->event_settings->blood)return;
    const float scales[]={1,1,1.5f,2,2.5f};
    for(unsigned i=0;i<5;++i) {
        float x=q3ne_mul(q3ne_mul(q3n_events_crandom(o),100),scales[i]);
        float y=q3ne_mul(q3ne_mul(q3n_events_crandom(o),100),scales[i]);
        float z=q3ne_add(150,q3ne_mul(q3n_events_crandom(o),100));
        q3n_local_entity *v=q3n_local_allocate(o,Q3N_LE_FRAGMENT,QA_Q3_REF_MODEL);
        v->start_time=f->time;
        v->end_time=q3ne_int(q3ne_add((float)q3ne_plus(f->time,10000),q3ne_mul(q3n_events_random(o),6000)));
        v->ref.model=q3n_media_read(f->media)->graphics[Q3N_G_SMOKE2]; v->ref.origin=origin; q3ne_identity(v->ref.axis);
        v->pos.type=5; v->pos.time=f->time; q3ne_store(v->pos.base,origin); q3ne_store(v->pos.delta,qa_v3(x,y,z));
        v->bounce_factor=0.1f; v->bounce_sound=Q3N_BOUNCE_BRASS;
    }
}
void q3n_effect_score(const q3n_frame *f, int32_t client, qa_vec3 origin, int32_t score)
{
    q3n_events *o=f->events;
    const qa_q3_player *ps=f->remote?q3n_frame_predicted_player(f):q3n_frame_snapshot_player(f);
    if(!ps || client!=ps->clientNum || !f->event_settings->score_plum)return;
    q3n_local_entity *v=q3n_local_allocate(o,Q3N_LE_SCORE_PLUM,QA_Q3_REF_SPRITE);
    v->start_time=f->time; v->end_time=q3ne_plus(f->time,4000); v->life_rate=q3ne_life(v->start_time,v->end_time);
    for(unsigned i=0;i<4;++i)v->color[i]=1;
    v->radius=(float)score; qa_vec3 p=origin;
    if(origin.z>=q3ne_add(o->last_score_position.z,-20) && origin.z<=q3ne_add(o->last_score_position.z,20))p.z=q3ne_add(origin.z,-20);
    q3ne_store(v->pos.base,p); o->last_score_position=origin; v->ref.radius=16; q3ne_identity(v->ref.axis);
}
static int32_t hit_sound(q3n_events *o, const q3n_media_view *m, q3n_sound a, q3n_sound b, q3n_sound c)
{ int32_t value=q3n_events_rand(o)&3; return m->sounds[value<2?a:value==2?b:c]; }
bool q3n_effect_mission(const q3n_frame *f, int32_t event, qa_vec3 origin, qa_vec3 angles, qa_error *error)
{
    qa_q3_product product = f->unified_effects ? f->unified_effects->product :
        f->effects_source ? f->effects_source->q3_product : q3n_frame_product(f);
    if(product!=QA_Q3_TEAM_ARENA)return q3ne_fail(error,QA_ERROR_FORMAT,"Missionpack effect reached baseq3 cgame");
    const q3n_media_view *m=q3n_media_read(f->media); q3n_events *o=f->events;
    q3n_local_entity *v=NULL;
    switch(event) {
    case Q3N_EV_LIGHTNING:
        v=q3n_local_allocate(o,Q3N_LE_SHOW,QA_Q3_REF_LIGHTNING);
        v->start_time=f->time; v->end_time=q3ne_plus(f->time,50);
        v->ref.origin=origin; v->ref.old_origin=angles; v->ref.custom_shader=m->graphics[Q3N_G_LIGHTNING_SHADER]; return true;
    case Q3N_EV_KAMIKAZE:
        v=q3n_local_allocate(o,Q3N_LE_KAMIKAZE,QA_Q3_REF_MODEL);
        v->ref.model=m->graphics[Q3N_G_KAMIKAZE_EFFECT]; v->end_time=q3ne_plus(f->time,3000); break;
    case Q3N_EV_OBELISK_EXPLODE: {
        origin.z=q3ne_add(origin.z,64);
        q3n_explosion x={.origin=origin,.direction={0,0,0},.has_direction=true,.sprite=true,
            .model=m->graphics[Q3N_G_DISH_FLASH],.shader=m->graphics[Q3N_G_ROCKET_EXPLOSION],.duration=600};
        v=q3n_effect_explosion(f,&x,error); if(!v)return false; v->light=300; v->light_color=qa_v3(1,0.75f,0); return true;
    }
    case Q3N_EV_OBELISK_PAIN:
        return q3ne_sound(f,hit_sound(o,m,Q3N_S_OBELISK_HIT1,Q3N_S_OBELISK_HIT2,Q3N_S_OBELISK_HIT3),&origin,1023,5,false,error);
    case Q3N_EV_INVUL_IMPACT:
        v=q3n_local_allocate(o,Q3N_LE_INVUL_IMPACT,QA_Q3_REF_MODEL);
        v->ref.model=m->graphics[Q3N_G_INVULNERABILITY_IMPACT]; v->end_time=q3ne_plus(f->time,1000); q3n_angles_axis(angles,v->ref.axis); break;
    case Q3N_EV_JUICED:
        v=q3n_local_allocate(o,Q3N_LE_INVUL_JUICED,QA_Q3_REF_MODEL);
        v->ref.model=m->graphics[Q3N_G_INVULNERABILITY_JUICED]; v->end_time=q3ne_plus(f->time,10000); q3ne_identity(v->ref.axis); break;
    default:return q3ne_fail(error,QA_ERROR_FORMAT,"Unknown missionpack effect");
    }
    v->start_time=f->time; v->life_rate=q3ne_life(v->start_time,v->end_time);
    for(unsigned i=0;i<4;++i)v->color[i]=1;
    v->ref.shader_time=q3ne_div((float)f->time,1000); v->ref.origin=origin;
    if(event==Q3N_EV_INVUL_IMPACT)return q3ne_sound(f,hit_sound(o,m,Q3N_S_INVULNERABILITY_IMPACT1,Q3N_S_INVULNERABILITY_IMPACT2,Q3N_S_INVULNERABILITY_IMPACT3),&origin,1023,5,false,error);
    if(event==Q3N_EV_JUICED)return q3ne_sound(f,m->sounds[Q3N_S_INVULNERABILITY_JUICED],&origin,1023,5,false,error);
    return true;
}
