#include "events_internal.h"
#include "../q3/internal.h"

static bool handle(q3n_events *o, int32_t value, q3p_resource_kind kind)
{
    const qa_q3_presentation_assets *a=o->options.assets;
    if(value<0)return false;
    if(!value)return true;
    size_t i=(size_t)value-1;
    switch(kind) {
    case Q3P_MODEL:return i<a->model_count && a->models[i];
    case Q3P_SKIN:return i<a->skin_count && a->skins[i];
    case Q3P_SHADER:return i<a->shader_count && a->shaders[i];
    case Q3P_SOUND:return i<a->sound_count && a->sounds[i];
    }
    return false;
}
static bool finite_value(qa_source_save_io *io, float *value)
{ return qa_source_save_f32(io,value) && isfinite(*value); }
static bool vector(qa_source_save_io *io, qa_vec3 *value)
{ return qa_source_save_vec3(io,value) && qa_vec_finite(*value); }
static bool trajectory(qa_source_save_io *io, qa_q3_trajectory *v)
{
    if(!qa_source_save_i32(io,&v->type) || v->type<0 || v->type>5 ||
       !qa_source_save_i32(io,&v->time) || !qa_source_save_i32(io,&v->duration))return false;
    for(unsigned i=0;i<3;++i)if(!finite_value(io,&v->base[i]) || !finite_value(io,&v->delta[i]))return false;
    return true;
}
static bool ref(qa_source_save_io *io, q3n_events *o, qa_q3_ref_entity *r)
{
    uint32_t kind=(uint32_t)r->kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_Q3_REF_PORTAL ||
       !qa_source_save_i32(io,&r->flags) || !qa_source_save_i32(io,&r->model) ||
       !qa_source_save_i32(io,&r->frame) || !qa_source_save_i32(io,&r->old_frame) ||
       !qa_source_save_i32(io,&r->skin) || !qa_source_save_i32(io,&r->custom_skin) ||
       !qa_source_save_i32(io,&r->custom_shader) || !vector(io,&r->lighting_origin))return false;
    r->kind=(qa_q3_ref_kind)kind;
    for(unsigned i=0;i<3;++i)if(!vector(io,&r->axis[i]))return false;
    if(!vector(io,&r->origin) || !vector(io,&r->old_origin) || !finite_value(io,&r->shadow_plane) ||
       !finite_value(io,&r->back_lerp) || !finite_value(io,&r->shader_time) || !finite_value(io,&r->radius) ||
       !finite_value(io,&r->rotation) || !finite_value(io,&r->shader_texcoord.x) || !finite_value(io,&r->shader_texcoord.y) ||
       !qa_source_save_bytes(io,r->color,4) || !qa_source_save_bool(io,&r->non_normalized_axes))return false;
    return handle(o,r->model,Q3P_MODEL) && handle(o,r->custom_skin,Q3P_SKIN) && handle(o,r->custom_shader,Q3P_SHADER);
}
static bool local(qa_source_save_io *io, q3n_events *o, q3n_local_entity *v)
{
    uint32_t type=(uint32_t)v->type,mark=(uint32_t)v->mark,sound=(uint32_t)v->bounce_sound;
    if(!qa_source_save_u32(io,&type) || type>Q3N_LE_INVUL_JUICED ||
       !qa_source_save_i32(io,&v->flags) || !qa_source_save_i32(io,&v->start_time) ||
       !qa_source_save_i32(io,&v->end_time) || !qa_source_save_i32(io,&v->fade_in_time) ||
       !qa_source_save_f32(io,&v->life_rate) || isnan(v->life_rate) ||
       !trajectory(io,&v->pos) || !trajectory(io,&v->angles) || !finite_value(io,&v->bounce_factor))return false;
    v->type=(q3n_local_type)type;
    for(unsigned i=0;i<4;++i)if(!finite_value(io,&v->color[i]))return false;
    if(!finite_value(io,&v->radius) || !finite_value(io,&v->light) || !vector(io,&v->light_color) ||
       !qa_source_save_u32(io,&mark) || mark>Q3N_MARK_BLOOD ||
       !qa_source_save_u32(io,&sound) || sound>Q3N_BOUNCE_BRASS || !ref(io,o,&v->ref))return false;
    v->mark=(q3n_local_mark)mark; v->bounce_sound=(q3n_local_sound)sound;
    bool sprite=type==Q3N_LE_MOVE_SCALE_FADE || type==Q3N_LE_FALL_SCALE_FADE || type==Q3N_LE_SCALE_FADE || type==Q3N_LE_SCORE_PLUM || type==Q3N_LE_SPRITE_EXPLOSION;
    bool model=type==Q3N_LE_FRAGMENT || type==Q3N_LE_KAMIKAZE || type==Q3N_LE_INVUL_IMPACT || type==Q3N_LE_INVUL_JUICED;
    bool mission=type==Q3N_LE_SHOW || type==Q3N_LE_KAMIKAZE || type==Q3N_LE_INVUL_IMPACT || type==Q3N_LE_INVUL_JUICED;
    return (!sprite || v->ref.kind==QA_Q3_REF_SPRITE) && (!model || v->ref.kind==QA_Q3_REF_MODEL) &&
        (!mission || o->options.product==QA_Q3_TEAM_ARENA) &&
        ((type!=Q3N_LE_FADE_RGB && type!=Q3N_LE_EXPLOSION) || v->ref.kind!=QA_Q3_REF_PORTAL);
}
static bool topology(q3n_events *o)
{
    bool seen[Q3N_LOCAL_CAPACITY]={0}; uint32_t count=0;
    int32_t previous=-1;
    for(int32_t i=o->local_head;i!=-1;i=o->locals[i].next) {
        if(i<0 || i>=Q3N_LOCAL_CAPACITY || seen[i] || !o->locals[i].active || !o->locals[i].present || o->locals[i].prev!=previous)return false;
        seen[i]=true; previous=i; ++count;
    }
    if(previous!=o->local_tail || count!=o->local_count)return false;
    for(int32_t i=o->local_free;i!=-1;i=o->locals[i].next) {
        if(i<0 || i>=Q3N_LOCAL_CAPACITY || seen[i] || o->locals[i].active)return false;
        seen[i]=true; ++count;
    }
    return count==Q3N_LOCAL_CAPACITY;
}
static bool mark(qa_source_save_io *io, q3n_events *o, q3n_stored_mark *m)
{
    if(!qa_source_save_i32(io,&m->time) || !qa_source_save_i32(io,&m->shader) ||
       !handle(o,m->shader,Q3P_SHADER) || !qa_source_save_bool(io,&m->alpha_fade))return false;
    for(unsigned i=0;i<4;++i)if(!finite_value(io,&m->color[i]) || m->color[i]<0 || m->color[i]>1)return false;
    if(!qa_source_save_u32(io,&m->count) || m->count>Q3N_MARK_VERTICES)return false;
    for(uint32_t i=0;i<m->count;++i) {
        qa_q3_poly_vertex *v=&m->vertices[i];
        if(!vector(io,&v->position) || !finite_value(io,&v->texcoord.x) || !finite_value(io,&v->texcoord.y) || !qa_source_save_bytes(io,v->color,4))return false;
    }
    return true;
}
bool q3ne_codec_fields(qa_source_save_io *io, q3n_events *o)
{
    uint8_t magic[4]={'Q','3','E','V'}; uint32_t schema=o->options.compiled_source?4:o->remote_source?3:2,product=(uint32_t)o->options.product;
    bool standalone = o->standalone_effects;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3EV",4) || !qa_source_save_u32(io,&schema) || schema!=(o->options.compiled_source?4u:o->remote_source?3u:2u) ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->options.product ||
       !qa_source_save_bool(io,&standalone) || standalone != o->standalone_effects ||
       !qa_source_save_u32(io,&o->seed) || !qa_source_save_u32(io,&o->smoke_seed) || !vector(io,&o->last_score_position))return false;
    if (o->options.compiled_source && !q3n_compiled_source_fields(io,o->options.compiled_source)) return false;
    q3n_event_state *g=&o->state;
    if(!finite_value(io,&g->land_change) || !finite_value(io,&g->step_change) ||
       !qa_source_save_i32(io,&g->land_time) || !qa_source_save_i32(io,&g->step_time) ||
       !qa_source_save_i32(io,&g->item_pickup) || !qa_source_save_i32(io,&g->item_pickup_time) ||
       !qa_source_save_i32(io,&g->item_pickup_blend_time) || !qa_source_save_i32(io,&g->powerup_active) ||
       !qa_source_save_i32(io,&g->powerup_time) || !qa_source_save_bytes(io,g->killer_name,sizeof(g->killer_name)) ||
       !memchr(g->killer_name,0,sizeof(g->killer_name)) || !qa_source_save_i32(io,&o->local_head) ||
       !qa_source_save_i32(io,&o->local_tail) || !qa_source_save_i32(io,&o->local_free) ||
       !qa_source_save_u32(io,&o->local_count) || o->local_count>Q3N_LOCAL_CAPACITY)return false;
    for(unsigned i=0;i<Q3N_LOCAL_CAPACITY;++i) {
        q3n_local_slot *slot=&o->locals[i];
        if(!qa_source_save_i32(io,&slot->prev) || slot->prev < -1 || slot->prev>=Q3N_LOCAL_CAPACITY ||
           !qa_source_save_i32(io,&slot->next) || slot->next < -1 || slot->next>=Q3N_LOCAL_CAPACITY ||
           !qa_source_save_bool(io,&slot->active) || !qa_source_save_bool(io,&slot->present) ||
           (slot->active && !slot->present) || (slot->present && !local(io,o,&slot->value)))return false;
    }
    if(!topology(o) || !qa_source_save_u32(io,&o->mark_count) || o->mark_count>Q3N_MARK_CAPACITY)return false;
    for(uint32_t i=0;i<o->mark_count;++i)if(!mark(io,o,&o->marks[i]))return false;
    if(!qa_source_save_i32(io,&o->sound_in) || o->sound_in<0 || o->sound_in>=20 ||
       !qa_source_save_i32(io,&o->sound_out) || o->sound_out<0 || o->sound_out>20 ||
       !qa_source_save_i32(io,&o->sound_time))return false;
    for(unsigned i=0;i<20;++i)if(!qa_source_save_i32(io,&o->sound_buffer[i]) || !handle(o,o->sound_buffer[i],Q3P_SOUND))return false;
    return true;
}
static bool ready(const q3n_events *o, qa_error *error)
{
    const qa_q3_presentation_assets *a=o?o->options.assets:NULL;
    return o && !o->busy && a && a->capturing && a->busy==1 && !a->codec_busy?true:
        q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 event codec requires its real backend capture lease");
}
bool q3n_events_checkpoint(const q3n_events *borrowed, qa_buffer *out, qa_error *error)
{
    if(!out || out->data || out->size || !ready(borrowed,error))return false;
    q3n_events *o=(q3n_events *)borrowed; o->busy=true;
    q3n_events *copy=malloc(sizeof(*copy));
    if(!copy) { o->busy=false; return q3ne_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 event capture"); }
    *copy=*o; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && q3ne_codec_fields(&io,copy) && qa_source_save_finish(&io,out);
    if(!ok && error && error->code==QA_OK)q3ne_fail(error,QA_ERROR_FORMAT,"Native Q3 event continuation is inconsistent");
    qa_source_save_dispose(&io); free(copy); o->busy=false; return ok;
}
bool q3n_events_restore(q3n_events *o, qa_bytes bytes, qa_error *error)
{
    if(!ready(o,error))return false;
    o->busy=true; q3n_events *candidate=calloc(1,sizeof(*candidate));
    if(!candidate) { o->busy=false; return q3ne_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 event restore candidate"); }
    candidate->options=o->options; candidate->standalone_effects=o->standalone_effects;
    candidate->remote_source=o->remote_source; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && q3ne_codec_fields(&io,candidate) && qa_source_save_finish(&io,NULL);
    if(ok) { *o=*candidate; o->busy=true; }
    else if(error && error->code==QA_OK)q3ne_fail(error,QA_ERROR_FORMAT,"Saved native Q3 event continuation is inconsistent");
    qa_source_save_dispose(&io); free(candidate); o->busy=false; return ok;
}
