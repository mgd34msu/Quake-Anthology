#include "remote_q2_effects_private.h"
#include "qa/allocation_gate.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool live(frontend_remote_q2_effects *o, qa_actor_id actor, bool *out, qa_error *e)
{
    return actor.registry && o->source.actor_live &&
        o->source.actor_live(o->source.context,actor,out,e) && q2fx_source_current(o,e);
}
static bool intake(frontend_remote_q2_effects *o, qa_actor_id actor, qa_error *e)
{
    bool present;
    return o && frontend_remote_q2_effects_idle(o) && q2fx_source_current(o,e) && live(o,actor,&present,e);
}
static void *reserve(void *rows, size_t capacity, size_t count, qa_error *e)
{
    if (count<capacity) return rows;
    qa_allocation_gate_capacity_exhausted();
    q2fx_fail(e,QA_ERROR_MEMORY,"Q2 semantic effect actor capacity exhausted");return NULL;
}

static bool actor_find(const frontend_remote_q2_effects *o,
    frontend_remote_q2_effects_presentation_kind kind, qa_actor_id actor, size_t *out)
{
    const qa_id_index *index=kind<=FRONTEND_REMOTE_Q2_ALL_BEAMS?&o->source_beam_index:
        kind==FRONTEND_REMOTE_Q2_FLASHLIGHT?&o->flashlight_index:
        &o->source_light_index[kind==FRONTEND_REMOTE_Q2_SHADOW_LIGHT];
    qa_id_index_cursor cursor=qa_id_index_begin(index,actor.slot,actor.generation);
    size_t value,first=SIZE_MAX;
    while (qa_id_index_next(index,&cursor,&value)) {
        qa_actor_id current=kind<=FRONTEND_REMOTE_Q2_ALL_BEAMS?o->source_beams[value].beam.actor:
            kind==FRONTEND_REMOTE_Q2_FLASHLIGHT?o->flashlights[value].actor:o->source_lights[value].light.actor;
        if (value<first && qa_actor_id_equal(current,actor)) first=value;
    }
    if (first==SIZE_MAX) return false;
    *out=first; return true;
}
static void compact_index(qa_id_index *index, size_t removed)
{
    for (size_t i=0;i<index->count;++i)
        if (index->entries[i].value>removed) --index->entries[i].value;
}
static void beam_remove(frontend_remote_q2_effects *o, size_t index)
{
    qa_actor_id actor=o->source_beams[index].beam.actor;
    (void)qa_id_index_remove_value(&o->source_beam_index,actor.slot,actor.generation,index);
    --o->source_beam_count;
    memmove(o->source_beams+index,o->source_beams+index+1,
        (o->source_beam_count-index)*sizeof(*o->source_beams));
    compact_index(&o->source_beam_index,index);
}
static void light_remove(frontend_remote_q2_effects *o, size_t index)
{
    const q2fx_source_light *row=o->source_lights+index;
    (void)qa_id_index_remove_value(&o->source_light_index[row->shadow],row->light.actor.slot,
        row->light.actor.generation,index);
    --o->source_light_count;
    memmove(o->source_lights+index,o->source_lights+index+1,
        (o->source_light_count-index)*sizeof(*o->source_lights));
    compact_index(&o->source_light_index[0],index); compact_index(&o->source_light_index[1],index);
}
static void flashlight_remove(frontend_remote_q2_effects *o, size_t index)
{
    qa_actor_id actor=o->flashlights[index].actor;
    (void)qa_id_index_remove_value(&o->flashlight_index,actor.slot,actor.generation,index);
    --o->flashlight_count;
    memmove(o->flashlights+index,o->flashlights+index+1,
        (o->flashlight_count-index)*sizeof(*o->flashlights));
    compact_index(&o->flashlight_index,index);
}
static bool beam_set(frontend_remote_q2_effects *o, const q2fx_source_beam *value, qa_error *e)
{
    size_t index=o->source_beam_count;
    (void)actor_find(o,FRONTEND_REMOTE_Q2_MONSTER_BEAM,value->beam.actor,&index);
    if (index==o->source_beam_count) {
        q2fx_source_beam *rows=reserve(o->source_beams,o->source_beam_capacity,o->source_beam_count,e);
        if (!rows) return false;
        o->source_beams=rows;
    }
    if (index<o->source_beam_count) beam_remove(o,index);
    qa_id_index_insert(&o->source_beam_index,value->beam.actor.slot,value->beam.actor.generation,o->source_beam_count);
    o->source_beams[o->source_beam_count++]=*value; o->dirty=true; return true;
}
bool frontend_remote_q2_effects_monster_beam(frontend_remote_q2_effects *o, qa_actor_id actor,
    qa_vec3 start, qa_vec3 end, double time, qa_error *e)
{
    if (!intake(o,actor,e) || !qa_vec_finite(start) || !qa_vec_finite(end) || !isfinite(time) ||
        !isfinite(time+200) || !q2fx_model_admit(o,Q2FX_PARASITE,e)) return false;
    q2fx_source_beam value={0};
    frontend_q2_beam_recipe recipe;
    frontend_q2_beam_recipe_read(QA_Q2_TE_PARASITE_ATTACK,(qa_vec3){0},0,false,&recipe);
    frontend_q2_beam_retain(&value.beam,1,o->source.rules==QA_RULESET_Q2_RERELEASE,
        &recipe,actor,(qa_actor_id){0},start,end,time);
    return beam_set(o,&value,e);
}
static bool light_valid(const frontend_remote_q2_effects_shadow_light *v)
{
    return v && v->actor.registry && qa_vec_finite(v->origin) && qa_vec_finite(v->color) &&
        qa_vec_finite(v->direction) && isfinite(v->radius) && isfinite(v->intensity) &&
        isfinite(v->fade_start) && isfinite(v->fade_end) && isfinite(v->cos_half_angle) &&
        v->resolution>=0 && v->lightstyle>=-1 && v->cos_half_angle>=-1 && v->cos_half_angle<=1;
}
static bool light_set(frontend_remote_q2_effects *o, const frontend_remote_q2_effects_shadow_light *v,
    bool shadow, qa_error *e)
{
    if (!light_valid(v) || !intake(o,v->actor,e)) return false;
    size_t index=o->source_light_count;
    (void)actor_find(o,shadow?FRONTEND_REMOTE_Q2_SHADOW_LIGHT:FRONTEND_REMOTE_Q2_SOURCE_LIGHT,v->actor,&index);
    if (!shadow && !v->visible) {
        if (index<o->source_light_count) light_remove(o,index);
        o->dirty=true; return true;
    }
    uint64_t identity, revision;
    if (index<o->source_light_count) {
        if (o->source_lights[index].revision==UINT64_MAX) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 Source light revision exhausted");
        identity=o->source_lights[index].identity; revision=o->source_lights[index].revision+1;
    } else {
        q2fx_source_light *rows=reserve(o->source_lights,o->source_light_capacity,index,e);
        if (!rows) return false;
        o->source_lights=rows;
        qa_id_index_insert(&o->source_light_index[shadow],v->actor.slot,v->actor.generation,index);
        identity=qa_scene_identity(); revision=0; ++o->source_light_count;
    }
    o->source_lights[index]=(q2fx_source_light){*v,identity,revision,shadow}; o->dirty=true; return true;
}
bool frontend_remote_q2_effects_shadow_light_set(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_shadow_light *v, qa_error *e)
{ return light_set(o,v,true,e); }
bool frontend_remote_q2_effects_source_light(frontend_remote_q2_effects *o, qa_actor_id actor,
    qa_vec3 origin, qa_vec3 color, float radius, bool visible, qa_error *e)
{
    frontend_remote_q2_effects_shadow_light v={.actor=actor,.origin=origin,.color=color,.radius=radius,
        .intensity=1,.lightstyle=-1,.visible=visible};
    return light_set(o,&v,false,e);
}
bool frontend_remote_q2_effects_flashlight(frontend_remote_q2_effects *o, qa_actor_id actor,
    bool enabled, int32_t hand, qa_error *e)
{
    if (!intake(o,actor,e) || hand<-1 || hand>1) return false;
    size_t index=o->flashlight_count;
    (void)actor_find(o,FRONTEND_REMOTE_Q2_FLASHLIGHT,actor,&index);
    if (!enabled) {
        if (index<o->flashlight_count) flashlight_remove(o,index);
    } else if (index<o->flashlight_count) o->flashlights[index].hand=hand;
    else {
        q2fx_flashlight *rows=reserve(o->flashlights,o->flashlight_capacity,index,e);
        if (!rows) return false;
        o->flashlights=rows;
        qa_id_index_insert(&o->flashlight_index,actor.slot,actor.generation,index);
        o->flashlights[o->flashlight_count++]=(q2fx_flashlight){actor,qa_scene_identity(),hand};
    }
    o->dirty=true; return true;
}
bool frontend_remote_q2_effects_retire_presentation(frontend_remote_q2_effects *o, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !q2fx_source_current(o,e)) return false;
    o->source_beam_count=0; o->source_light_count=0; o->flashlight_count=0; o->dirty=true;
    qa_id_index_clear(&o->source_beam_index);
    qa_id_index_clear(&o->source_light_index[0]); qa_id_index_clear(&o->source_light_index[1]);
    qa_id_index_clear(&o->flashlight_index);
    return true;
}
bool frontend_remote_q2_effects_remove_actor_presentation(frontend_remote_q2_effects *o, qa_actor_id actor,
    frontend_remote_q2_effects_presentation_kind kind, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !actor.registry ||
        (uint32_t)kind>FRONTEND_REMOTE_Q2_FLASHLIGHT || !q2fx_source_current(o,e)) return false;
    bool changed=false;
    size_t index;
    while (actor_find(o,kind,actor,&index)) {
        if (kind<=FRONTEND_REMOTE_Q2_ALL_BEAMS) beam_remove(o,index);
        else if (kind==FRONTEND_REMOTE_Q2_FLASHLIGHT) flashlight_remove(o,index);
        else light_remove(o,index);
        changed=true;
    }
    if (changed) o->dirty=true;
    return true;
}
bool frontend_remote_q2_effects_presentation_actor_at(const frontend_remote_q2_effects *o,
    frontend_remote_q2_effects_presentation_kind kind, size_t index, qa_actor_id *out)
{
    if (!o || !out || (uint32_t)kind>FRONTEND_REMOTE_Q2_FLASHLIGHT) return false;
    if (kind<=FRONTEND_REMOTE_Q2_ALL_BEAMS) {
        for (size_t i=0;i<o->source_beam_count;++i) {
            const q2fx_source_beam *row=o->source_beams+i;
            if (!index--) { *out=row->beam.actor; return true; }
        }
    } else if (kind==FRONTEND_REMOTE_Q2_FLASHLIGHT) {
        if (index<o->flashlight_count) { *out=o->flashlights[index].actor; return true; }
    } else for (size_t i=0;i<o->source_light_count;++i) {
        const q2fx_source_light *row=o->source_lights+i;
        if (row->shadow!=(kind==FRONTEND_REMOTE_Q2_SHADOW_LIGHT)) continue;
        if (!index--) { *out=row->light.actor; return true; }
    }
    return false;
}
static bool cached_identity(const frontend_remote_q2_effects *o, size_t index)
{
    uint64_t identity=o->sampled_lights[index].identity;
    if (!identity) return false;
    for (size_t i=0;i<o->source_light_count;++i) if (o->source_lights[i].identity==identity) return false;
    for (size_t i=0;i<o->flashlight_count;++i) if (o->flashlights[i].identity==identity) return false;
    for (size_t i=0;i<index;++i) if (o->sampled_lights[i].identity==identity) return false;
    return true;
}
size_t frontend_remote_q2_effects_light_identity_count(const frontend_remote_q2_effects *o)
{
    if (!o) return 0;
    size_t count=o->source_light_count+o->flashlight_count;
    for (size_t i=0;i<o->light_count;++i) if (cached_identity(o,i)) ++count;
    return count;
}
bool frontend_remote_q2_effects_light_identity_at(const frontend_remote_q2_effects *o, size_t index, uint64_t *out)
{
    if (!o || !out) return false;
    if (index<o->source_light_count) { *out=o->source_lights[index].identity; return true; }
    index-=o->source_light_count;
    if (index<o->flashlight_count) { *out=o->flashlights[index].identity; return true; }
    index-=o->flashlight_count;
    for (size_t i=0;i<o->light_count;++i) if (cached_identity(o,i) && !index--) {
        *out=o->sampled_lights[i].identity; return true;
    }
    return false;
}
bool q2fx_semantic_prepare(frontend_remote_q2_effects *o, const frontend_remote_q2_effects_sample *s,
    const frontend_remote_q2_effects_controls *controls, bool advance, qa_error *e)
{
    for (size_t i=0;i<o->source_beam_count;) {
        q2fx_source_beam *b=o->source_beams+i;
        if (b->beam.die<s->milliseconds) { beam_remove(o,i); continue; }
        if (!q2fx_prepare_beams(o,&b->beam,1,s,controls,advance,e)) return false;
        ++i;
    }
    for (size_t i=0;i<o->source_light_count;) {
        bool present;
        if (!live(o,o->source_lights[i].light.actor,&present,e)) return false;
        if (!present) light_remove(o,i);
        else ++i;
    }
    for (size_t i=0;i<o->flashlight_count;) {
        bool present;
        if (!live(o,o->flashlights[i].actor,&present,e)) return false;
        if (!present) flashlight_remove(o,i);
        else ++i;
    }
    return true;
}
static bool lights(frontend_remote_q2_effects *o, const frontend_remote_q2_effects_sample *s, qa_error *e)
{
    o->light_count=o->transient_light_count;
    for (size_t i=0;i<o->flashlight_count;++i) {
        const q2fx_flashlight *f=o->flashlights+i; frontend_remote_q2_effects_pose pose;
        if (!o->source.actor_pose) return false;
        qa_error pose_error={0};
        if (!o->source.actor_pose(o->source.context,f->actor,&pose,&pose_error)) {
            if (pose_error.code==QA_ERROR_NOT_FOUND && q2fx_source_current(o,e)) continue;
            if (e) *e=pose_error;
            return false;
        }
        if (!qa_actor_id_equal(pose.actor,f->actor) || !qa_vec_finite(pose.origin) || !qa_vec_finite(pose.angles) ||
            !q2fx_source_current(o,e)) return false;
        bool local=qa_actor_id_equal(s->viewer,f->actor);
        qa_vec3 forward,right,origin=pose.origin;
        if (local) {
            forward=s->view.axis[0]; right=s->view.axis[1];
            origin=qa_vec_add(s->view.origin,qa_vec_scale(right,f->hand<0?7:f->hand>0?-7:0));
        } else {
            qa_angle_vectors(pose.angles,&forward,NULL,NULL,QA_ANGLE_FLOAT_MATH);
        }
        o->sampled_lights[o->light_count++]=(qa_scene_light){.origin=origin,.color={1,1,1},.direction=forward,
            .radius=512,.scale=2,.cos_half_angle=.92718385f,.additive=true,.spot=true,.casts_shadow=true,
            .identity=f->identity,.shadow_resolution=512,.family=QA_GAME_Q2};
    }
    for (size_t i=0;i<o->source_light_count;++i) {
        const q2fx_source_light *row=o->source_lights+i; const frontend_remote_q2_effects_shadow_light *v=&row->light;
        if (!v->visible || v->radius<=0) continue;
        float fade=1;
        if (!(v->fade_start<=1 && v->fade_end<=1) && v->fade_start<=v->fade_end) {
            float fraction=fminf(1,fmaxf(0,qa_vec_length(qa_vec_sub(v->origin,s->view.origin))/v->fade_end));
            float start=v->fade_start/v->fade_end;
            if (start<=0) fade=fraction;
            else if (start<1) { float t=fminf(1,fmaxf(0,(fraction-start)/(1-start))); fade=1-t*t*(3-2*t); }
            else fade=fraction<1?1:0;
        }
        if (fade<=0) continue;
        float style=1;
        if (v->lightstyle>=0 && s->world_input && s->world_input->q2_styles &&
            (uint32_t)v->lightstyle<s->world_input->style_count) style=s->world_input->q2_styles[v->lightstyle].x;
        if (!isfinite(style)) return false;
        o->sampled_lights[o->light_count++]=(qa_scene_light){.origin=v->origin,.color=v->color,.direction=v->direction,
            .radius=v->radius,.scale=v->intensity*fade*style,.cos_half_angle=v->cos_half_angle,
            .additive=true,.spot=v->cone,.casts_shadow=row->shadow,.identity=row->identity,.revision=row->revision,
            .shadow_resolution=(uint32_t)v->resolution,.family=QA_GAME_Q2};
    }
    return true;
}
bool frontend_remote_q2_effects_view_lights(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_sample *s, const qa_scene_light **out, size_t *count, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !s || !out || !count || !o->sampled || o->dirty ||
        o->time!=s->milliseconds || o->frame_sequence!=s->frame_sequence || !q2fx_source_current(o,e)) return false;
    ++o->busy; bool okay=lights(o,s,e); --o->busy;
    if (!okay || !q2fx_source_current(o,e)) return false;
    *out=o->sampled_lights; *count=o->light_count; return true;
}
