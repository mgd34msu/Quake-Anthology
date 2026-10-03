#include "remote_q2_effects_private.h"
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
static void *reserve(void *rows, size_t *capacity, size_t count, size_t size, qa_error *e)
{
    if (count<*capacity) return rows;
    size_t maximum=SIZE_MAX/size;
    if (count>=maximum) { q2fx_fail(e,QA_ERROR_MEMORY,"Q2 semantic effect rows exceed physical capacity"); return NULL; }
    size_t next=*capacity>maximum/2?maximum:*capacity?*capacity*2:8;
    if (next<=count) next=count+1;
    void *memory=realloc(rows,next*size);
    if (!memory) { q2fx_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 semantic effect rows"); return NULL; }
    *capacity=next; return memory;
}
static void beam_remove(frontend_remote_q2_effects *o, size_t index)
{
    --o->source_beam_count;
    memmove(o->source_beams+index,o->source_beams+index+1,
        (o->source_beam_count-index)*sizeof(*o->source_beams));
}
static bool beam_set(frontend_remote_q2_effects *o, const q2fx_source_beam *value, qa_error *e)
{
    size_t index=o->source_beam_count;
    for (size_t i=0;i<o->source_beam_count;++i)
        if (o->source_beams[i].persistent==value->persistent &&
            qa_actor_id_equal(o->source_beams[i].beam.actor,value->beam.actor)) { index=i; break; }
    if (index==o->source_beam_count) {
        q2fx_source_beam *rows=reserve(o->source_beams,&o->source_beam_capacity,o->source_beam_count,sizeof(*rows),e);
        if (!rows) return false;
        o->source_beams=rows;
    }
    if (index<o->source_beam_count) beam_remove(o,index);
    o->source_beams[o->source_beam_count++]=*value; o->dirty=true; return true;
}
bool frontend_remote_q2_effects_source_beam(frontend_remote_q2_effects *o, qa_actor_id actor,
    qa_vec3 start, qa_vec3 end, float width, uint32_t color, bool visible, qa_error *e)
{
    if (!intake(o,actor,e) || !qa_vec_finite(start) || !qa_vec_finite(end) || !isfinite(width)) return false;
    if (!visible) {
        for (size_t i=0;i<o->source_beam_count;)
            if (qa_actor_id_equal(o->source_beams[i].beam.actor,actor)) beam_remove(o,i); else ++i;
        o->dirty=true; return true;
    }
    q2fx_source_beam value={.beam={.active=true,.actor=actor,.start=start,.end=end},
        .width=width,.color=color&255,.persistent=true};
    return beam_set(o,&value,e);
}
bool frontend_remote_q2_effects_monster_beam(frontend_remote_q2_effects *o, qa_actor_id actor,
    qa_vec3 start, qa_vec3 end, double time, qa_error *e)
{
    if (!intake(o,actor,e) || !qa_vec_finite(start) || !qa_vec_finite(end) || !isfinite(time) ||
        !isfinite(time+200) || !q2fx_model_admit(o,Q2FX_PARASITE,e)) return false;
    q2fx_source_beam value={.beam={.active=true,.model=Q2FX_PARASITE,.actor=actor,
        .start=start,.end=end,.die=time+200}};
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
    for (size_t i=0;i<o->source_light_count;++i) if (o->source_lights[i].shadow==shadow &&
        qa_actor_id_equal(o->source_lights[i].light.actor,v->actor)) { index=i; break; }
    if (!shadow && !v->visible) {
        if (index<o->source_light_count) {
            --o->source_light_count;
            memmove(o->source_lights+index,o->source_lights+index+1,(o->source_light_count-index)*sizeof(*o->source_lights));
        }
        o->dirty=true; return true;
    }
    uint64_t identity, revision;
    if (index<o->source_light_count) {
        if (o->source_lights[index].revision==UINT64_MAX) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 Source light revision exhausted");
        identity=o->source_lights[index].identity; revision=o->source_lights[index].revision+1;
    } else {
        q2fx_source_light *rows=reserve(o->source_lights,&o->source_light_capacity,index,sizeof(*rows),e);
        if (!rows) return false;
        o->source_lights=rows;
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
    for (size_t i=0;i<o->flashlight_count;++i) if (qa_actor_id_equal(o->flashlights[i].actor,actor)) { index=i; break; }
    if (!enabled) {
        if (index<o->flashlight_count) {
            --o->flashlight_count;
            memmove(o->flashlights+index,o->flashlights+index+1,(o->flashlight_count-index)*sizeof(*o->flashlights));
        }
    } else if (index<o->flashlight_count) o->flashlights[index].hand=hand;
    else {
        q2fx_flashlight *rows=reserve(o->flashlights,&o->flashlight_capacity,index,sizeof(*rows),e);
        if (!rows) return false;
        o->flashlights=rows;
        o->flashlights[o->flashlight_count++]=(q2fx_flashlight){actor,qa_scene_identity(),hand};
    }
    o->dirty=true; return true;
}
bool frontend_remote_q2_effects_retire_presentation(frontend_remote_q2_effects *o, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !q2fx_source_current(o,e)) return false;
    o->source_beam_count=0; o->source_light_count=0; o->flashlight_count=0; o->dirty=true;
    return true;
}
bool frontend_remote_q2_effects_remove_actor_presentation(frontend_remote_q2_effects *o, qa_actor_id actor,
    frontend_remote_q2_effects_presentation_kind kind, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !actor.registry ||
        (uint32_t)kind>FRONTEND_REMOTE_Q2_FLASHLIGHT || !q2fx_source_current(o,e)) return false;
    bool changed=false;
    if (kind<=FRONTEND_REMOTE_Q2_ALL_BEAMS) for (size_t i=0;i<o->source_beam_count;) {
        q2fx_source_beam *b=o->source_beams+i;
        if (qa_actor_id_equal(b->beam.actor,actor) && (kind==FRONTEND_REMOTE_Q2_ALL_BEAMS ||
            (b->persistent?FRONTEND_REMOTE_Q2_ORDINARY_BEAM:FRONTEND_REMOTE_Q2_MONSTER_BEAM)==kind)) {
            beam_remove(o,i); changed=true;
        } else ++i;
    }
    if (kind==FRONTEND_REMOTE_Q2_SHADOW_LIGHT || kind==FRONTEND_REMOTE_Q2_SOURCE_LIGHT)
        for (size_t i=0;i<o->source_light_count;) {
            q2fx_source_light *v=o->source_lights+i;
            if (qa_actor_id_equal(v->light.actor,actor) && v->shadow==(kind==FRONTEND_REMOTE_Q2_SHADOW_LIGHT)) {
                --o->source_light_count; changed=true;
                memmove(o->source_lights+i,o->source_lights+i+1,(o->source_light_count-i)*sizeof(*o->source_lights));
            } else ++i;
        }
    if (kind==FRONTEND_REMOTE_Q2_FLASHLIGHT) for (size_t i=0;i<o->flashlight_count;) {
        if (qa_actor_id_equal(o->flashlights[i].actor,actor)) {
            --o->flashlight_count; changed=true;
            memmove(o->flashlights+i,o->flashlights+i+1,(o->flashlight_count-i)*sizeof(*o->flashlights));
        } else ++i;
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
            if (kind!=FRONTEND_REMOTE_Q2_ALL_BEAMS &&
                (row->persistent?FRONTEND_REMOTE_Q2_ORDINARY_BEAM:FRONTEND_REMOTE_Q2_MONSTER_BEAM)!=kind) continue;
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
        if (!b->persistent && b->beam.die<s->milliseconds) { beam_remove(o,i); continue; }
        if (!b->persistent && !q2fx_prepare_beams(o,&b->beam,1,s,controls,advance,e)) return false;
        ++i;
    }
    for (size_t i=0;i<o->source_light_count;) {
        bool present;
        if (!live(o,o->source_lights[i].light.actor,&present,e)) return false;
        if (!present) {
            --o->source_light_count;
            memmove(o->source_lights+i,o->source_lights+i+1,(o->source_light_count-i)*sizeof(*o->source_lights));
        } else ++i;
    }
    for (size_t i=0;i<o->flashlight_count;) {
        bool present;
        if (!live(o,o->flashlights[i].actor,&present,e)) return false;
        if (!present) {
            --o->flashlight_count;
            memmove(o->flashlights+i,o->flashlights+i+1,(o->flashlight_count-i)*sizeof(*o->flashlights));
        } else ++i;
    }
    return true;
}
static bool lights(frontend_remote_q2_effects *o, const frontend_remote_q2_effects_sample *s, qa_error *e)
{
    if (o->source_light_count>SIZE_MAX-o->flashlight_count ||
        o->source_light_count+o->flashlight_count>SIZE_MAX-Q2FX_LIGHT_CAPACITY) return false;
    size_t maximum=Q2FX_LIGHT_CAPACITY+o->source_light_count+o->flashlight_count;
    if (maximum>SIZE_MAX/sizeof(*o->sampled_lights)) return false;
    if (maximum>o->light_capacity) {
        qa_scene_light *rows=realloc(o->sampled_lights,maximum*sizeof(*rows));
        if (!rows) return q2fx_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 Source scene lights");
        o->sampled_lights=rows; o->light_capacity=maximum;
    }
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
            double pitch=pose.angles.x*.017453292519943295, yaw=pose.angles.y*.017453292519943295;
            forward=qa_v3((float)(cos(pitch)*cos(yaw)),(float)(cos(pitch)*sin(yaw)),(float)-sin(pitch));
        }
        o->sampled_lights[o->light_count++]=(qa_scene_light){.origin=origin,.color={1,1,1},.direction=forward,
            .radius=512,.scale=2,.cos_half_angle=.92718385f,.additive=true,.spot=true,.casts_shadow=true,
            .identity=f->identity,.shadow_resolution=512,.family=QA_SCENE_Q2};
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
            .shadow_resolution=(uint32_t)v->resolution,.family=QA_SCENE_Q2};
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
bool q2fx_semantic_draw(frontend_remote_q2_effects *o, const frontend_remote_q2_effects_sample *s,
    qa_scene_frame *frame, qa_error *e)
{
    qa_bytes palette={0};
    if (o->source_beam_count && (!qa_scene_resources_palette_read(o->source.images,QA_SCENE_Q2,&palette) || palette.size<768)) return false;
    for (size_t i=0;i<o->source_beam_count;++i) {
        const q2fx_source_beam *b=o->source_beams+i;
        if (!b->persistent || b->width<=0) continue;
        uint32_t color=b->color&255;
        qa_scene_vec4 rgba={palette.data[color*3]/255.f,palette.data[color*3+1]/255.f,palette.data[color*3+2]/255.f,.3f};
        if (!qa_scene_beam(frame,&s->view,b->beam.start,b->beam.end,b->width,rgba,o->source.white,e)) return false;
    }
    return true;
}
static bool semantic_vector(qa_source_save_io *io, qa_vec3 *v)
{ return qa_source_save_vec3(io,v) && qa_vec_finite(*v); }
static bool semantic_scalar(qa_source_save_io *io, float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
static bool semantic_rows(qa_source_save_io *io, void **rows, size_t *count, size_t *capacity, size_t size)
{
    if (!qa_source_save_count(io,count,SIZE_MAX/size) || !qa_source_save_count(io,capacity,SIZE_MAX/size) || *count>*capacity) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && *capacity) {
        *rows=calloc(*capacity,size); if (!*rows) return false;
    }
    return true;
}
bool q2fx_semantic_fields(qa_source_save_io *io, frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    void *rows=o->source_beams;
    if (!semantic_rows(io,&rows,&o->source_beam_count,&o->source_beam_capacity,sizeof(*o->source_beams))) return false;
    o->source_beams=rows; rows=o->source_lights;
    if (!semantic_rows(io,&rows,&o->source_light_count,&o->source_light_capacity,sizeof(*o->source_lights))) return false;
    o->source_lights=rows; rows=o->flashlights;
    if (!semantic_rows(io,&rows,&o->flashlight_count,&o->flashlight_capacity,sizeof(*o->flashlights))) return false;
    o->flashlights=rows;
    if ((o->source_beam_count || o->source_light_count || o->flashlight_count) && !o->source.actor_live) return false;
    for (size_t i=0;i<o->source_beam_count;++i) {
        q2fx_source_beam *b=o->source_beams+i;
        if (!q2fx_actor_fields(io,&b->beam.actor,refs) || !b->beam.actor.registry ||
            !semantic_vector(io,&b->beam.start) || !semantic_vector(io,&b->beam.end) ||
            !qa_source_save_bool(io,&b->persistent) || !qa_source_save_f64(io,&b->beam.die) || !isfinite(b->beam.die) ||
            !qa_source_save_u8(io,&b->beam.model) || !semantic_scalar(io,&b->width) ||
            !qa_source_save_u32(io,&b->color) || b->color>255 ||
            (b->persistent && (b->beam.model || b->beam.die)) ||
            (!b->persistent && (b->beam.model!=Q2FX_PARASITE || b->width || b->color || !o->model_admitted[Q2FX_PARASITE]))) return false;
        for (size_t j=0;j<i;++j) if (b->persistent==o->source_beams[j].persistent &&
            qa_actor_id_equal(b->beam.actor,o->source_beams[j].beam.actor)) return false;
        if (reading) b->beam.active=true;
    }
    for (size_t i=0;i<o->source_light_count;++i) {
        q2fx_source_light *row=o->source_lights+i; frontend_remote_q2_effects_shadow_light *v=&row->light;
        if (!q2fx_actor_fields(io,&v->actor,refs) || !semantic_vector(io,&v->origin) || !semantic_vector(io,&v->color) ||
            !semantic_vector(io,&v->direction) || !semantic_scalar(io,&v->radius) || !semantic_scalar(io,&v->intensity) ||
            !semantic_scalar(io,&v->fade_start) || !semantic_scalar(io,&v->fade_end) || !semantic_scalar(io,&v->cos_half_angle) ||
            !qa_source_save_i32(io,&v->resolution) || !qa_source_save_i32(io,&v->lightstyle) ||
            !qa_source_save_bool(io,&v->visible) || !qa_source_save_bool(io,&v->cone) ||
            !qa_source_save_bool(io,&row->shadow) || !q2fx_light_identity_fields(io,&row->identity,refs) || !qa_source_save_u64(io,&row->revision) ||
            !row->identity || !light_valid(v) ||
            (!row->shadow && (!v->visible || v->intensity!=1 || v->resolution || v->lightstyle!=-1 || v->cone ||
                v->fade_start || v->fade_end || v->cos_half_angle || qa_vec_length(v->direction)!=0))) return false;
        for (size_t j=0;j<i;++j) if (o->source_lights[j].identity==row->identity ||
            (o->source_lights[j].shadow==row->shadow && qa_actor_id_equal(o->source_lights[j].light.actor,v->actor))) return false;
    }
    for (size_t i=0;i<o->flashlight_count;++i) {
        q2fx_flashlight *f=o->flashlights+i;
        if (!q2fx_actor_fields(io,&f->actor,refs) || !f->actor.registry || !q2fx_light_identity_fields(io,&f->identity,refs) ||
            !qa_source_save_i32(io,&f->hand) || f->hand<-1 || f->hand>1 || !f->identity) return false;
        for (size_t j=0;j<i;++j) if (o->flashlights[j].identity==f->identity || qa_actor_id_equal(o->flashlights[j].actor,f->actor)) return false;
        for (size_t j=0;j<o->source_light_count;++j) if (o->source_lights[j].identity==f->identity) return false;
    }
    return true;
}
