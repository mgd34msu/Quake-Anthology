#include "remote_q2_effects_private.h"
#include <math.h>
#include <stdio.h>

static uint32_t draw(frontend_remote_q2_effects *o) { return qa_builtin_random_integer(&o->random); }
static bool rerelease(const frontend_remote_q2_effects *o)
{ return o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE; }
static bool play(frontend_remote_q2_effects *o,const frontend_remote_q2_effects_pose *p,
    const char *path,double time,int32_t channel,float volume,float attenuation,qa_error *e)
{ return o->source.sound(o->source.context,path,p->origin,p->actor,time,channel,volume,attenuation,0,e) && q2fx_source_current(o,e); }
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
        if (!s->entities[i].actor.registry || !qa_vec_finite(s->entities[i].origin)) return false;
    o->event_received=true; o->event_sequence=s->frame_sequence; o->event_failed=false; o->event_error=(qa_error){0};
    ++o->busy; bool ok=true;
    frontend_q2_entity_effects effects={.particles=&o->particles,.random=&o->random,.rerelease=rerelease(o)};
    for (size_t i=0;ok && i<s->entity_count;++i) {
        const frontend_remote_q2_effects_pose *p=&s->entities[i];
        frontend_q2_entity_frame_particles(&effects,p,s->milliseconds);
        switch (p->event) {
        case 1:
            ok=play(o,p,"items/respawn1.wav",s->milliseconds,1,1,2,e);
            if (ok) frontend_fx_q2_respawn_particles(&o->particles,&o->random,p->origin,s->milliseconds*.001,FRONTEND_FX_Q2_ITEM);
            break;
        case 6:
            ok=play(o,p,"misc/tele1.wav",s->milliseconds,1,1,2,e);
            if (ok) frontend_fx_q2_teleport(&o->particles,&o->random,p->origin,s->milliseconds*.001);
            break;
        case 2: case 8: case 9:
            if (s->footsteps == 0 || (p->event!=2 && !rerelease(o))) break;
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
static void entity_light(void *context,const qa_scene_light *light)
{
    frontend_remote_q2_effects *o=context;
    if (o->light_count<Q2FX_LIGHT_CAPACITY) o->sampled_lights[o->light_count++]=*light;
}
static bool entity_trace(void *context,const qa_trace_query *query,qa_trace_result *result,qa_error *error)
{
    frontend_remote_q2_effects *o=context;
    return o->source.trace && o->source.trace(o->source.context,query,result,error) && q2fx_source_current(o,error);
}
bool q2fx_entities(frontend_remote_q2_effects *o,const frontend_remote_q2_effects_sample *s,
    bool advance,qa_error *e)
{
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    frontend_q2_entity_effects effects={.particles=&o->particles,.random=&o->random,.rerelease=rerelease(o),
        .disable_particles=controls.disable_particles,.dlight_hacks=controls.dlight_hacks,
        .context=o,.light=entity_light,.trace=entity_trace};
    frontend_q2_entity_effect_view sample={.milliseconds=s->milliseconds,.view=s->view,.viewer=s->viewer,
        .hand=s->hand,.per_pixel_lighting=s->per_pixel_lighting,.frame_seconds=s->frame_seconds};
    for (size_t i=0;i<s->entity_count;++i) {
        const frontend_q2_entity_pose *row=&s->entities[i];
        frontend_q2_entity_trail *trail=frontend_q2_entity_cache_at(&o->entity_trails,row->actor.slot,e);
        if (!trail || !frontend_q2_entity_effect(&effects,&sample,row,trail,advance,e)) return false;
    }
    return true;
}
