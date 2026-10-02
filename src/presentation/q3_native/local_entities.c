/* id Software cg_localents.c; GPL-2.0-or-later. */
#include "events_internal.h"

void q3ne_local_reset(q3n_events *o)
{
    memset(o->locals,0,sizeof(o->locals)); o->local_head=o->local_tail=-1;
    o->local_free=0; o->local_count=0;
    for(int32_t i=0;i<Q3N_LOCAL_CAPACITY;++i) {
        o->locals[i].prev=-1; o->locals[i].next=i+1==Q3N_LOCAL_CAPACITY?-1:i+1;
    }
}
void q3ne_local_free(q3n_events *o, int32_t i)
{
    q3n_local_slot *s=&o->locals[i];
    if(s->prev==-1)o->local_head=s->next; else o->locals[s->prev].next=s->next;
    if(s->next==-1)o->local_tail=s->prev; else o->locals[s->next].prev=s->prev;
    s->active=false; s->next=o->local_free; o->local_free=i; --o->local_count;
}
q3n_local_entity *q3n_local_allocate(q3n_events *o, q3n_local_type type, qa_q3_ref_kind kind)
{
    if(o->local_free==-1)q3ne_local_free(o,o->local_tail);
    int32_t i=o->local_free; q3n_local_slot *s=&o->locals[i]; o->local_free=s->next;
    memset(&s->value,0,sizeof(s->value)); s->value.type=type; s->value.ref.kind=kind;
    s->active=s->present=true; s->prev=-1; s->next=o->local_head;
    if(o->local_head!=-1)o->locals[o->local_head].prev=i; else o->local_tail=i;
    o->local_head=i; ++o->local_count; return &s->value;
}
static bool entity(const q3n_frame *f, const qa_q3_ref_entity *ref, qa_error *error)
{
    if ((f->effects_source || f->unified_effects) && f->effect_entity_output) {
        float radius = 0;
        /* ApplicationEffects captures the exact activeLocal.ref identity.
         * Stack-generated shockwaves and copied refs have no pool radius. */
        for (int32_t i = f->events->local_head; i != -1; i = f->events->locals[i].next)
            if (&f->events->locals[i].value.ref == ref) {
                radius = f->events->locals[i].value.radius; break;
            }
        if (!f->effect_entity_output(f->effect_output_context, ref, radius, error) || !q3ne_current(f, error))
            return false;
    }
    return qa_q3_presentation_entity(f->presentation,ref,error) && q3ne_current(f,error);
}
static bool light(const q3n_frame *f, const q3n_local_entity *v, qa_error *error)
{
    if(!v->light || f->preferences.reduced_flashes)return true;
    float c=q3ne_div((float)q3ne_sub(f->time,v->start_time),(float)q3ne_sub(v->end_time,v->start_time));
    c=c<0.5f?1:q3ne_add(1,-q3ne_mul(q3ne_add(c,-0.5f),2));
    return qa_q3_presentation_light(f->presentation,v->ref.origin,q3ne_mul(v->light,c),v->light_color,false,error) && q3ne_current(f,error);
}
static bool scale_fade(const q3n_frame *f, int32_t index, qa_error *error)
{
    q3n_events *o=f->events; q3n_local_entity *v=&o->locals[index].value; qa_q3_ref_entity *r=&v->ref;
    float c=q3ne_remaining(v,f->time);
    if(v->type==Q3N_LE_MOVE_SCALE_FADE && v->fade_in_time>v->start_time && f->time<v->fade_in_time)
        c=q3ne_add(1,-q3ne_div((float)q3ne_sub(v->fade_in_time,f->time),(float)q3ne_sub(v->fade_in_time,v->start_time)));
    r->color[3]=q3ne_byte(q3ne_mul(q3ne_mul(255,c),v->color[3]));
    if(v->type!=Q3N_LE_MOVE_SCALE_FADE || !(v->flags&Q3N_LE_DONT_SCALE))
        r->radius=q3ne_add(q3ne_mul(v->radius,q3ne_add(1,-c)),v->type==Q3N_LE_FALL_SCALE_FADE?16:8);
    if(v->type==Q3N_LE_MOVE_SCALE_FADE) {
        if(!q3n_trajectory(&v->pos,f->time,&r->origin,error))return false;
    } else if(v->type==Q3N_LE_FALL_SCALE_FADE)
        r->origin.z=q3ne_add(v->pos.base[2],-q3ne_mul(q3ne_add(1,-c),v->pos.delta[2]));
    if(q3ne_length(q3ne_difference(r->origin,f->refdef.origin))<v->radius) { q3ne_local_free(o,index); return true; }
    return entity(f,r,error);
}
static bool blood_trail(const q3n_frame *f, int32_t index, qa_error *error)
{
    int32_t start=q3ne_word(150u*(uint32_t)(q3ne_plus(q3ne_sub(f->time,f->frame_milliseconds),150)/150));
    int32_t end=q3ne_word(150u*(uint32_t)(f->time/150));
    for(int32_t time=start; time<=end; time=q3ne_plus(time,150)) {
        qa_vec3 origin;
        /* Allocation may recycle this exact record while the source traverses it. */
        if(!q3n_trajectory(&f->events->locals[index].value.pos,time,&origin,error))return false;
        q3n_smoke smoke={.origin=origin,.velocity={0,0,0},.radius=20,.color={1,1,1,1},
            .duration=2000,.start_time=time,.shader=q3n_media_read(f->media)->graphics[Q3N_G_BLOOD_TRAIL]};
        q3n_local_entity *v=q3n_effect_smoke(f,&smoke); v->type=Q3N_LE_FALL_SCALE_FADE; v->pos.delta[2]=40;
        if(time>INT32_MAX-150)break;
    }
    return true;
}
static bool fragment(const q3n_frame *f, int32_t index, qa_error *error)
{
    q3n_events *o=f->events; q3n_local_entity *v=&o->locals[index].value; qa_q3_ref_entity *r=&v->ref;
    if(v->pos.type==0) {
        int32_t t=q3ne_sub(v->end_time,f->time);
        if(t<1000) {
            r->lighting_origin=r->origin; r->flags|=128; qa_vec3 saved=r->origin;
            r->origin.z=q3ne_add(r->origin.z,-q3ne_mul(16,q3ne_add(1,-q3ne_div((float)t,1000))));
            bool ok=entity(f,r,error); r->origin=saved; return ok;
        }
        return entity(f,r,error);
    }
    qa_vec3 destination; qa_trace_result tr;
    if(!q3n_trajectory(&v->pos,f->time,&destination,error) ||
       !q3n_events_trace(f,r->origin,destination,(qa_bounds){{0,0,0},{0,0,0}},-1,1,&tr,error))return false;
    if(tr.fraction==1) {
        r->origin=destination;
        if(v->flags&Q3N_LE_TUMBLE) {
            qa_vec3 angles; if(!q3n_trajectory(&v->angles,f->time,&angles,error))return false;
            q3n_angles_axis(angles,r->axis);
        }
        if(!entity(f,r,error))return false;
        return v->bounce_sound==Q3N_BOUNCE_BLOOD?blood_trail(f,index,error):true;
    }
    uint32_t contents;
    if(!q3n_events_point_contents(f,tr.end,0,&contents,error))return false;
    if(contents&0x80000000u) { q3ne_local_free(o,index); return true; }
    if(!tr.contact && !tr.all_solid)return q3ne_fail(error,QA_ERROR_FORMAT,"Fragment impact lacks its actual source plane");
    qa_vec3 normal=tr.contact?tr.contact_plane.normal:qa_v3(0,0,0);
    const q3n_media_view *m=q3n_media_read(f->media);
    if(v->mark!=Q3N_MARK_NONE) {
        bool blood=v->mark==Q3N_MARK_BLOOD;
        float radius=(float)((blood?16:8)+(q3n_events_rand(o)&(blood?31:15)));
        q3n_impact_mark mark={.shader=m->graphics[blood?Q3N_G_BLOOD_MARK:Q3N_G_BURN_MARK],
            .origin=tr.end,.direction=normal,.orientation=q3ne_mul(q3n_events_random(o),360),
            .color={1,1,1,1},.alpha_fade=true,.radius=radius};
        if(!q3n_marks_impact(f,&mark,error))return false;
    }
    v->mark=Q3N_MARK_NONE;
    if(v->bounce_sound==Q3N_BOUNCE_BLOOD && (q3n_events_rand(o)&1)) {
        int32_t n=q3n_events_rand(o)&3;
        if(!q3ne_sound(f,m->sounds[n==0?Q3N_S_GIB_BOUNCE1:n==1?Q3N_S_GIB_BOUNCE2:Q3N_S_GIB_BOUNCE3],&tr.end,1022,0,false,error))return false;
    }
    v->bounce_sound=Q3N_BOUNCE_NONE;
    int32_t hit_time=q3ne_int(q3ne_add((float)q3ne_sub(f->time,f->frame_milliseconds),q3ne_mul((float)f->frame_milliseconds,tr.fraction)));
    qa_vec3 velocity;
    if(!q3n_trajectory_delta(&v->pos,hit_time,&velocity,error))return false;
    qa_vec3 delta=q3ne_scale(q3ne_sum(velocity,q3ne_scale(normal,q3ne_mul(-2,q3ne_dot(velocity,normal)))),v->bounce_factor);
    if(tr.all_solid || (normal.z>0 && (delta.z<40 || delta.z<q3ne_mul(-(float)f->frame_milliseconds,delta.z))))v->pos.type=0;
    q3ne_store(v->pos.base,tr.end); q3ne_store(v->pos.delta,delta); v->pos.time=f->time;
    return entity(f,r,error);
}
static bool score_plum(const q3n_frame *f, int32_t index, qa_error *error)
{
    q3n_local_entity *v=&f->events->locals[index].value; qa_q3_ref_entity *r=&v->ref;
    float c=q3ne_remaining(v,f->time); int32_t score=q3ne_int(v->radius);
    const uint8_t colors[6][4]={{255,17,17,255},{255,0,255,255},{0,0,255,255},{255,255,0,255},{0,255,0,255},{255,255,255,255}};
    memcpy(r->color,colors[score<0?0:score>=50?1:score>=20?2:score>=10?3:score>=2?4:5],4);
    if(c<0.25f)r->color[3]=q3ne_byte(q3ne_mul(1020,c)); r->radius=4;
    qa_vec3 origin=q3ne_array(v->pos.base); origin.z=q3ne_add(origin.z,q3ne_add(110,-q3ne_mul(c,100)));
    qa_vec3 dir=q3ne_normalize(q3ne_cross(q3ne_difference(f->refdef.origin,origin),qa_v3(0,0,1)));
    float phase=q3ne_mul(q3ne_mul(c,2),3.14159274101257324219f);
    origin=q3ne_sum(origin,q3ne_scale(dir,q3ne_add(-10,q3ne_mul(20,(float)sin((double)phase)))));
    if(q3ne_length(q3ne_difference(origin,f->refdef.origin))<20) { q3ne_local_free(f->events,index); return true; }
    uint32_t magnitude=score<0?0u-(uint32_t)score:(uint32_t)score, digits[11],count=0;
    do { digits[count++]=magnitude%10; magnitude/=10; } while(magnitude);
    if(score<0)digits[count++]=10;
    for(uint32_t i=0;i<count;++i) {
        r->origin=q3ne_sum(origin,q3ne_scale(dir,q3ne_mul(q3ne_add((float)count/2,-(float)i),8)));
        r->custom_shader=q3n_media_read(f->media)->number_shaders[digits[count-1-i]];
        if(!entity(f,r,error))return false;
    }
    return true;
}
static bool shockwave(const q3n_frame *f, q3n_local_entity *v, const qa_vec3 axis[3],
    int32_t time, int32_t start, int32_t end, int32_t fade, float radius, qa_error *error)
{
    qa_q3_ref_entity ref={.kind=QA_Q3_REF_MODEL,.model=q3n_media_read(f->media)->graphics[Q3N_G_KAMIKAZE_SHOCKWAVE],
        .shader_time=v->ref.shader_time,.origin=v->ref.origin,.non_normalized_axes=true};
    float c=q3ne_div((float)q3ne_sub(time,start),(float)q3ne_sub(end,start)), scale=q3ne_div(q3ne_mul(c,radius),88);
    for(unsigned i=0;i<3;++i)ref.axis[i]=q3ne_scale(axis[i],scale);
    float alpha=time>fade?q3ne_div((float)q3ne_sub(time,fade),(float)q3ne_sub(end,fade)):0;
    memset(ref.color,q3ne_byte(q3ne_add(255,-q3ne_mul(alpha,255))),4);
    return entity(f,&ref,error);
}
static bool kamikaze(const q3n_frame *f, q3n_local_entity *v, qa_error *error)
{
    int32_t t=q3ne_sub(f->time,v->start_time); qa_vec3 axis[3]; q3ne_identity(axis);
    const q3n_media_view *m=q3n_media_read(f->media);
    if(t>0 && t<2000) {
        if(!(v->flags&Q3N_LE_SOUND1)) {
            if(!q3ne_sound(f,m->sounds[Q3N_S_KAMIKAZE_EXPLODE],NULL,(int32_t)f->viewing_client,0,true,error))return false;
            v->flags|=Q3N_LE_SOUND1;
        }
        if(!shockwave(f,v,axis,t,0,2000,1500,1320,error))return false;
    }
    if(t>250 && t<2250) {
        float factor=q3ne_mul(q3ne_remaining(v,f->time),255);
        for(unsigned i=0;i<4;++i)v->ref.color[i]=q3ne_byte(q3ne_mul(v->color[i],factor));
        float c;
        if(t<2000)c=q3ne_div((float)q3ne_sub(t,250),1750);
        else {
            if(!(v->flags&Q3N_LE_SOUND2)) {
                if(!q3ne_sound(f,m->sounds[Q3N_S_KAMIKAZE_IMPLODE],NULL,(int32_t)f->viewing_client,0,true,error))return false;
                v->flags|=Q3N_LE_SOUND2;
            }
            c=q3ne_div((float)q3ne_sub(2250,t),250);
        }
        for(unsigned i=0;i<3;++i)v->ref.axis[i]=q3ne_scale(axis[i],q3ne_div(q3ne_mul(c,720),72));
        v->ref.non_normalized_axes=true;
        if(!entity(f,&v->ref,error))return false;
        if(!f->preferences.reduced_flashes && (!qa_q3_presentation_light(f->presentation,v->ref.origin,q3ne_mul(c,1000),qa_v3(1,1,c),false,error) || !q3ne_current(f,error)))return false;
    }
    if(t>2000 && t<3000) {
        if(v->angles.base[0]==0 && v->angles.base[1]==0 && v->angles.base[2]==0)
            for(unsigned i=0;i<3;++i)v->angles.base[i]=q3ne_mul(q3n_events_random(f->events),360);
        q3n_angles_axis(q3ne_array(v->angles.base),axis);
        if(!shockwave(f,v,axis,t,2000,3000,2500,704,error))return false;
    }
    return true;
}
bool q3n_local_submit(const q3n_frame *f, qa_error *error)
{
    if(!q3ne_current(f,error))return false;
    if(f->events->busy)return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 local entities are already producing");
    f->events->busy=true;
    for(int32_t index=f->events->local_tail; index!=-1;) {
        q3n_local_slot *slot=&f->events->locals[index]; int32_t next=slot->prev; q3n_local_entity *v=&slot->value;
        bool ok=true;
        if(f->time>=v->end_time)q3ne_local_free(f->events,index);
        else switch(v->type) {
        case Q3N_LE_MARK:break;
        case Q3N_LE_FRAGMENT:ok=fragment(f,index,error);break;
        case Q3N_LE_MOVE_SCALE_FADE:case Q3N_LE_FALL_SCALE_FADE:case Q3N_LE_SCALE_FADE:ok=scale_fade(f,index,error);break;
        case Q3N_LE_FADE_RGB:
            for(unsigned i=0;i<4;++i)v->ref.color[i]=q3ne_byte(q3ne_mul(v->color[i],q3ne_mul(q3ne_remaining(v,f->time),255)));
            ok=entity(f,&v->ref,error);break;
        case Q3N_LE_EXPLOSION:ok=entity(f,&v->ref,error) && light(f,v,error);break;
        case Q3N_LE_SPRITE_EXPLOSION: {
            float c=fminf(1,q3ne_div((float)q3ne_sub(v->end_time,f->time),(float)q3ne_sub(v->end_time,v->start_time)));
            qa_q3_ref_entity r=v->ref; memset(r.color,255,3); r.color[3]=q3ne_byte(q3ne_mul(q3ne_mul(255,c),0.33f));
            r.radius=q3ne_add(q3ne_mul(42,q3ne_add(1,-c)),30); ok=entity(f,&r,error) && light(f,v,error);break;
        }
        case Q3N_LE_SCORE_PLUM:ok=score_plum(f,index,error);break;
        case Q3N_LE_KAMIKAZE:ok=kamikaze(f,v,error);break;
        case Q3N_LE_INVUL_IMPACT:case Q3N_LE_SHOW:ok=entity(f,&v->ref,error);break;
        case Q3N_LE_INVUL_JUICED: {
            int32_t t=q3ne_sub(f->time,v->start_time);
            if(t>3000) {
                float xy=q3ne_add(1,q3ne_div(q3ne_mul(0.3f,(float)q3ne_sub(t,3000)),2000));
                float z=q3ne_add(0.7f,q3ne_div(q3ne_mul(0.3f,(float)q3ne_sub(2000,q3ne_sub(t,3000))),2000));
                v->ref.axis[0].x=xy; v->ref.axis[1].y=xy; v->ref.axis[2].z=z;
            }
            if(t>5000) { v->end_time=0; qa_vec3 origin=v->ref.origin; q3n_effect_gib_player(f,origin); }
            else ok=entity(f,&v->ref,error); break;
        }
        }
        if(!ok) { f->events->busy=false; return false; }
        index=next;
    }
    f->events->busy=false; return true;
}
