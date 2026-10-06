#include "internal.h"
#include "q2_temporary_beams.h"
#include "q2_entity_effects.h"
#include <math.h>
#include <string.h>
static bool beam_fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message);return false; }
bool frontend_q2_beam_recipe_read(uint32_t type,qa_vec3 offset,frontend_q2_beam_recipe *out)
{
    frontend_q2_beam_recipe recipe={.model=Q2FX_PARASITE};
    switch (type) {
    case QA_Q2_TE_PARASITE_ATTACK:case QA_Q2_TE_MEDIC_CABLE_ATTACK:break;
    case QA_Q2_TE_GRAPPLE_CABLE:recipe.model=Q2FX_CABLE;recipe.offset=offset;break;
    case QA_Q2_TE_LIGHTNING:recipe.model=Q2FX_LIGHTNING;recipe.destination=true;recipe.lightning_sound=true;break;
    case QA_Q2_TE_HEATBEAM:recipe.model=Q2FX_HEAT;recipe.player=true;recipe.offset=qa_v3(2,7,-3);break;
    case QA_Q2_TE_MONSTER_HEATBEAM:recipe.model=Q2FX_HEAT;recipe.player=true;recipe.monster=true;break;
    case QA_Q2_TE_GRAPPLE_CABLE_2:recipe.model=Q2FX_CABLE;recipe.player=true;recipe.offset=qa_v3(9,12,-3);break;
    case QA_Q2_TE_LIGHTNING_BEAM:recipe.model=Q2FX_LIGHTNING;recipe.player=true;recipe.offset=qa_v3(0,12,-12);break;
    default:return false;
    }
    *out=recipe;return true;
}
bool frontend_q2_beam_named_recipe(const char *name,qa_vec3 offset,double duration,frontend_q2_beam_recipe *out)
{
    if (!name) return false;
    if (!strncmp(name,"q2:",3)) name+=3;
    uint32_t type;
    if (!strcmp(name,"parasite")) type=QA_Q2_TE_PARASITE_ATTACK;
    else if (!strcmp(name,"medic-cable")) type=QA_Q2_TE_MEDIC_CABLE_ATTACK;
    else if (!strcmp(name,"grapple-cable")) type=QA_Q2_TE_GRAPPLE_CABLE;
    else if (!strcmp(name,"lightning") || !strcmp(name,"bfg-lightning")) type=QA_Q2_TE_LIGHTNING;
    else if (!strcmp(name,"heatbeam")) type=QA_Q2_TE_HEATBEAM;
    else if (!strcmp(name,"monster-heatbeam")) type=QA_Q2_TE_MONSTER_HEATBEAM;
    else return false;
    frontend_q2_beam_recipe_read(type,offset,out);
    if (!strcmp(name,"bfg-lightning")) { out->lightning_sound=false;out->destination=false;out->independent=true; }
    if (out->player || out->independent) out->duration_seconds=duration>0?duration:.1;
    return true;
}
frontend_q2_temporary_beam *frontend_q2_beam_retain(frontend_q2_temporary_beam *pool,size_t count,
    bool rerelease,const frontend_q2_beam_recipe *recipe,qa_actor_id actor,qa_actor_id destination,
    qa_vec3 start,qa_vec3 end,double time)
{
    frontend_q2_temporary_beam *row=NULL;
    for (size_t i=0;!recipe->independent && i<count;++i)
        if (pool[i].actor.registry && qa_actor_id_equal(pool[i].actor,actor) &&
            (recipe->player || (!rerelease && recipe->model!=Q2FX_LIGHTNING) ||
             qa_actor_id_equal(pool[i].destination,destination))) { row=pool+i;break; }
    double duration=recipe->player && (rerelease || !row)?100:200;
    if (recipe->duration_seconds>0) duration=recipe->duration_seconds*1000;
    if (!row) for (size_t i=0;i<count;++i)
        if (!pool[i].active || pool[i].die<time) { row=pool+i;break; }
    if (row) {
        double sound_until=row->sound_until;
        *row=(frontend_q2_temporary_beam){.active=true,.player=recipe->player,.monster=recipe->monster,
            .model=(uint8_t)recipe->model,.unkeyed=recipe->independent && !actor.registry,.actor=actor,.destination=destination,.start=start,.end=end,
            .offset=recipe->offset,.die=time+duration,.sound_until=sound_until};
    }
    return row;
}
bool frontend_q2_beam_lightning_sound(frontend_q2_temporary_beam *beam,bool rerelease,double milliseconds)
{
    if (!rerelease) return true;
    if (!beam || beam->sound_until>=milliseconds) return false;
    beam->sound_until=milliseconds+500;return true;
}
void frontend_q2_temporary_radial(frontend_fx_particles *particles,qa_builtin_random *random,
    bool rerelease,qa_vec3 origin,double time,
    int32_t count, float radius, float speed, uint32_t palette, bool instant)
{
    static const uint32_t colors[4] = {16,104,168,144};
    for (int32_t i = 0; i < count && particles->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        uint32_t index = palette == 0xe0 ? 0 : qa_builtin_random_integer(random) & 3;
        frontend_fx_q2_particle p = {.spawn_milliseconds = time, .alpha = 1,
            .color = palette == 0 ? colors[index] : palette + (palette == 110 ? index * 2 : index)};
        qa_vec3 dir = frontend_q2_effect_random_direction(random,rerelease); p.origin = qa_vec_add(origin, qa_vec_scale(dir, radius));
        p.velocity = qa_vec_scale(dir, speed);
        p.alpha_velocity = instant ? -10000 : (float)((palette == 208 ? -1 : -.8) / (.5 + (double)(qa_builtin_random_integer(random)&32767)/32767 * .3));
        particles->values.q2[particles->count++] = p;
    }
}
static bool beam_roll(frontend_q2_beam_context *o,uint32_t *out,qa_error *error)
{
    if (!o->rerelease) { *out=qa_builtin_random_integer(o->random)%360;return true; }
    uint64_t wall,frame;
    if (!o->render_clock(o->context,&wall,&frame,error)) return false;
    uint32_t bin=(uint32_t)wall/16;
    if (o->roll->bin!=bin) { o->roll->bin=bin;o->roll->base=qa_builtin_random_integer(o->random); }
    if (o->roll->frame!=frame) { o->roll->frame=frame;o->roll->seed=o->roll->base; }
    uint32_t value=o->roll->seed;value^=value<<13;value^=value>>17;value^=value<<5;
    o->roll->seed=value;*out=value%360;return true;
}
static bool rerelease_regular_beam(frontend_q2_beam_context *o, const frontend_q2_temporary_beam *beam_row,
    qa_vec3 start, qa_vec3 delta, qa_vec3 angles, qa_error *e)
{
    float remaining=qa_vec_length(delta), segment=beam_row->model==Q2FX_LIGHTNING?35:30;
    if (!isfinite(remaining)) return beam_fail(e,QA_ERROR_FORMAT,"Q2 beam length exceeds its source vector");
    double steps=ceil((double)remaining/segment);
    if (steps>(double)(SIZE_MAX/sizeof(frontend_q2_beam_draw)))
        return beam_fail(e,QA_ERROR_FORMAT,"Q2 beam exceeds its physical model draw capacity");
    qa_vec3 direction=qa_vec_normalize(delta);
    bool lightning=beam_row->model==Q2FX_LIGHTNING;
    if (lightning) { angles.x=-angles.x; angles.y+=180; }
    while (remaining>0) {
        float used=fminf(remaining,segment), longitudinal=used/segment;
        if (!lightning) start=qa_vec_add(start,qa_vec_scale(direction,.5f*used));
        uint32_t roll;
        if (!beam_roll(o,&roll,e)) return false;
        angles.z=(float)roll;
        frontend_q2_beam_draw draw={.model=beam_row->model,.origin=start,.angles=angles,
            .flags=lightning?8|8192:8192,.alpha=1,.scale={longitudinal,1,1}};
        if (!o->draw(o->context,&draw,e)) return false;
        start=qa_vec_add(start,qa_vec_scale(direction,(lightning?1:.5f)*segment));
        float next=remaining-segment;
        if (next>=remaining) return beam_fail(e,QA_ERROR_FORMAT,"Q2 beam segment exceeds its source float precision");
        remaining=next;
    }
    return true;
}
static void heat_particles(frontend_q2_beam_context *o, qa_vec3 origin, qa_vec3 direction,
    const frontend_q2_beam_view *s)
{
    if (qa_vec_length(direction)==0) return;
    qa_vec3 right=qa_vec_scale(s->view.axis[1],-1), up=s->view.axis[2], move=origin;
    if (s->hardware) move=qa_vec_sub(move,qa_vec_scale(qa_vec_add(right,up),.5f));
    float begin=fmodf((float)(s->milliseconds*.096),32);
    move=qa_vec_add(move,qa_vec_scale(direction,begin));
    for (float distance=begin;distance<4096 && distance<=160;distance+=32) {
        for (double rotation=0;rotation<6.283185307179586;rotation+=.3141592653589793) {
            if (o->particles->count==FRONTEND_FX_PARTICLE_CAPACITY) return;
            float taper=distance<10?distance/10:1;
            qa_vec3 radial=qa_vec_add(qa_vec_scale(right,(float)cos(rotation)*.5f*taper),qa_vec_scale(up,(float)sin(rotation)*.5f*taper));
            frontend_fx_q2_particle p={.spawn_milliseconds=s->milliseconds,.origin=qa_vec_add(move,qa_vec_scale(radial,3)),
                .color=223-(qa_builtin_random_integer(o->random)&7),.alpha=.5f,.alpha_velocity=-1000};
            o->particles->values.q2[o->particles->count++]=p;
        }
        move=qa_vec_add(move,qa_vec_scale(direction,32));
    }
}
static qa_vec3 beam_angles(const frontend_q2_beam_context *o, qa_vec3 delta)
{
    float horizontal=hypotf(delta.x,delta.y), yaw=horizontal==0?0:atan2f(delta.y,delta.x)*57.29577951308232f;
    bool rerelease=o->rerelease;
    float pitch=horizontal==0?(delta.z>0?90:270):atan2f(delta.z,horizontal)*(rerelease?57.29577951308232f:-57.29577951308232f);
    if (yaw<0) yaw+=360;
    if (pitch<0) pitch+=360;
    return qa_v3(rerelease?-pitch:pitch,yaw,0);
}
bool frontend_q2_beams_prepare(frontend_q2_beam_context *o, frontend_q2_temporary_beam *pool, size_t count,
    const frontend_q2_beam_view *s, bool advance, qa_error *e)
{
    for (size_t i=0;i<count;++i) {
        frontend_q2_temporary_beam *b=&pool[i];
        if (!b->active) continue;
        if (b->die<s->milliseconds) { b->active=false; continue; }
        ++o->active_beams;
        if (!o->model_ready(o->context,(q2fx_model)b->model)) continue;
        qa_vec3 start=b->start, offset=b->offset, delta;
        bool local=!b->unkeyed && b->actor.registry && s->viewer.registry && qa_actor_id_equal(b->actor,s->viewer);
        bool heat=b->model==Q2FX_HEAT, lightning=b->model==Q2FX_LIGHTNING;
        bool rerelease=o->rerelease;
        float hand=s->hand==2?0:s->hand==1?-1:1;
        if (rerelease && s->gun==3) hand=-1;
        else if (rerelease && s->gun==2) hand=1;
        if (b->player && local) {
            if (rerelease && s->gun_fov>0) {
                float tangent=tanf(fminf(160,fmaxf(30,s->gun_fov))*.008726646259971648f);
                if (!isfinite(s->player_fov) || s->player_fov<=0 || s->player_fov>=180)
                    return beam_fail(e,QA_ERROR_ARGUMENT,"Q2 player beam lost its actual player field of view");
                float ratio=tanf(s->player_fov*.008726646259971648f)/tangent;
                offset.x*=ratio;
                offset.z*=ratio;
            }
            start=qa_vec_add(s->view.origin,s->gun_offset);
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[1],-hand*offset.x));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[0],offset.y));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[2],offset.z));
            if (hand==0) start=qa_vec_sub(start,s->view.axis[2]);
        } else if (!b->player && local) {
            if (o->rerelease) {
                if (!s->viewer_origin_present || !qa_vec_finite(s->viewer_origin))
                    return beam_fail(e,QA_ERROR_ARGUMENT,"Q2 beam lost its actual rendered player body origin");
                start=qa_vec_add(s->viewer_origin,offset);
            } else { start=s->view.origin; start.z-=22; start=qa_vec_add(start,offset); }
        }
        else if (!(rerelease && b->player)) start=qa_vec_add(start,offset);
        delta=qa_vec_sub(b->end,start);
        if (rerelease && b->player && !local) {
            if (offset.x!=0 || offset.y!=0 || offset.z!=0) {
                qa_vec3 rotation=beam_angles(o,delta), axis[3];
                frontend_camera_axes(qa_v3(-rotation.x,rotation.y+180,0),axis);
                start=qa_vec_add(start,qa_vec_scale(axis[1],offset.x-1));
                start=qa_vec_sub(start,qa_vec_scale(axis[0],offset.y));
                start=qa_vec_add(start,qa_vec_scale(axis[2],-offset.z-10));
            } else if (heat && advance) frontend_q2_temporary_radial(o->particles,o->random,o->rerelease,b->start,s->milliseconds,40,10,0,0xe0,true);
            delta=qa_vec_sub(b->end,start);
        }
        if (rerelease && b->player && local && b->model!=Q2FX_CABLE)
            delta=qa_vec_scale(s->view.axis[0],qa_vec_length(delta));
        else if (!rerelease && heat && local) {
            delta=qa_vec_scale(s->view.axis[0],qa_vec_length(delta));
            delta=qa_vec_sub(delta,qa_vec_scale(s->view.axis[1],hand*offset.x));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[0],offset.y));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[2],offset.z));
            if (s->hand==2) start=qa_vec_sub(start,s->view.axis[2]);
        }
        qa_vec3 rotation=beam_angles(o,delta);
        float yaw=rotation.y,pitch=rotation.x;
        if (!b->player && o->rerelease) {
            if (!rerelease_regular_beam(o,b,start,delta,qa_v3(pitch,yaw,0),e)) return false;
            continue;
        }
        qa_vec3 direction=qa_vec_normalize(delta);
        if (!rerelease && heat && !local) {
            if (!b->monster) {
                qa_vec3 axis[3]; frontend_camera_axes(qa_v3(-pitch,yaw+180,0),axis);
                start=qa_vec_add(start,qa_vec_scale(axis[1],offset.x-1));
                start=qa_vec_sub(start,qa_vec_scale(axis[0],offset.y));
                start=qa_vec_add(start,qa_vec_scale(axis[2],-offset.z-10));
            } else if (advance) frontend_q2_temporary_radial(o->particles,o->random,o->rerelease,b->start,s->milliseconds,40,10,0,0xe0,true);
        }
        if (heat && local && advance) heat_particles(o,start,direction,s);
        float length=qa_vec_length(delta)-(lightning?20:0), segment=heat?32:lightning?35:30;
        if (rerelease && b->player && local && b->model==Q2FX_CABLE && hand!=0) {
            start=qa_vec_add(start,qa_vec_scale(direction,segment*.5f));
            length-=segment*.5f;
        }
        bool short_lightning=lightning && length<=segment;
        if (length<=0 && !short_lightning) continue;
        double steps=short_lightning?1:ceil((double)length/segment);
        if (!isfinite(steps) || steps>(double)(SIZE_MAX/sizeof(frontend_q2_beam_draw))) return beam_fail(e,QA_ERROR_FORMAT,"Q2 beam exceeds physical model draw capacity");
        float spacing=steps>1?(length-segment)/(float)(steps-1):0;
        for (size_t j=0;j<(size_t)steps;++j) {
            qa_vec3 origin=short_lightning?b->end:qa_vec_add(start,qa_vec_scale(direction,(float)j*spacing));
            uint32_t roll=0;
            if (!heat && !beam_roll(o,&roll,e)) return false;
            qa_vec3 angles=qa_v3(short_lightning || (!lightning && !heat)?pitch:-pitch,
                short_lightning || (!lightning && !heat)?yaw:yaw+180,heat?(float)fmod(s->milliseconds,360):(float)roll);
            int32_t frame=heat?(local?1:2):0;
            frontend_q2_beam_draw draw={.model=b->model,.origin=origin,.angles=angles,
                .frame=frame,.old_frame=frame,.flags=heat || lightning?8:rerelease?8192:0,
                .alpha=1,.scale={1,1,1}};
            if (!o->draw(o->context,&draw,e)) return false;
        }
    }
    return true;
}
