#include "particles_internal.h"
#include "player_state_internal.h"
#include <stdio.h>

static bool shader(const q3n_particles *o, int32_t value)
{
    return value>=0 && (size_t)value<=o->assets->shader_count &&
        (!value || o->assets->shaders[value-1]);
}
static bool scalar(qa_source_save_io *io, float *value)
{ return qa_source_save_f32(io,value) && isfinite(*value); }
static bool vector(qa_source_save_io *io, qa_vec3 *value)
{ return qa_source_save_vec3(io,value) && qa_vec_finite(*value); }
static bool topology(const q3n_particles *o)
{
    bool seen[Q3N_PARTICLE_CAPACITY]={0}; uint32_t active=0,total=0;
    for (int32_t index=o->active;index!=-1;index=o->slots[index].next) {
        if (index<0 || index>=Q3N_PARTICLE_CAPACITY || seen[index] || o->slots[index].type!=6) return false;
        seen[index]=true; ++active; ++total;
    }
    if (active!=o->count) return false;
    for (int32_t index=o->free;index!=-1;index=o->slots[index].next) {
        if (index<0 || index>=Q3N_PARTICLE_CAPACITY || seen[index] || o->slots[index].type!=0) return false;
        seen[index]=true; ++total;
    }
    return total==Q3N_PARTICLE_CAPACITY;
}
bool q3np_codec_fields(qa_source_save_io *io, q3n_particles *o)
{
    const char *signature=o->compiled_source?"Q3CA":o->remote_source?"Q3RA":"Q3PA";
    uint8_t magic[4]; memcpy(magic,signature,4); uint32_t schema=1,product=o->product;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,signature,4) ||
        !qa_source_save_u32(io,&schema) || schema!=1 || !qa_source_save_u32(io,&product) ||
        product!=(uint32_t)o->product ||
        !q3nh_remote_basis_fields(io,q3n_remote_source_client(o->remote_source)) ||
        (o->compiled_source && !q3n_compiled_source_fields(io,o->compiled_source)) || !qa_source_save_bool(io,&o->initialized) ||
        !qa_source_save_i32(io,&o->active) || !qa_source_save_i32(io,&o->free) ||
        !qa_source_save_u32(io,&o->count) || o->count>Q3N_PARTICLE_CAPACITY ||
        !scalar(io,&o->old_time) || !scalar(io,&o->view_roll)) return false;
    for (size_t i=0;i<3;++i) if (!vector(io,&o->view_axes[i]) || !vector(io,&o->rotated_axes[i])) return false;
    for (size_t i=0;i<Q3N_PARTICLE_FRAMES;++i) {
        if (!qa_source_save_i32(io,&o->shaders[i]) || !shader(o,o->shaders[i])) return false;
        if (o->initialized && o->shaders[i]) {
            char name[32]; snprintf(name,sizeof(name),"explode1%zu",i+1);
            const q3p_name *actual=q3p_find_name(o->assets,Q3P_SHADER,name);
            if (!actual || actual->handle!=o->shaders[i]) return false;
        }
    }
    for (size_t i=0;i<Q3N_PARTICLE_CAPACITY;++i) {
        q3n_particle *p=&o->slots[i];
        if (!qa_source_save_i32(io,&p->next) || p->next < -1 || p->next>=Q3N_PARTICLE_CAPACITY ||
            !qa_source_save_i32(io,&p->type) || (p->type!=0 && p->type!=6) ||
            !qa_source_save_i32(io,&p->roll) || p->roll < -179 || p->roll > 179 ||
            !qa_source_save_i32(io,&p->shader) || !shader(o,p->shader) ||
            !scalar(io,&p->time) || !scalar(io,&p->end_time) || !scalar(io,&p->alpha) ||
            p->alpha!=(p->type==6?0.5f:0.0f) || !scalar(io,&p->alpha_velocity) || p->alpha_velocity!=0 ||
            !scalar(io,&p->width) || !scalar(io,&p->height) || !scalar(io,&p->end_width) ||
            !scalar(io,&p->end_height) || p->width!=p->height || p->end_width!=p->end_height ||
            !vector(io,&p->origin) || !vector(io,&p->velocity) || !vector(io,&p->acceleration) ||
            p->acceleration.x!=0 || p->acceleration.y!=0 || p->acceleration.z!=0) return false;
    }
    return topology(o) && (o->initialized || !o->count);
}
static bool ready(const q3n_particles *o, qa_error *error)
{
    return q3n_particles_idle(o) && o->assets->capturing && o->assets->busy==1 && !o->assets->codec_busy ? true :
        q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 particle codec requires its actual registry capture lease");
}
bool q3n_particles_checkpoint(const q3n_particles *borrowed, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !ready(borrowed,error)) return false;
    q3n_particles *o=(q3n_particles *)borrowed; o->busy=true;
    q3n_particles *copy=malloc(sizeof(*copy));
    if (!copy) { o->busy=false; return q3np_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 particle capture"); }
    *copy=*o; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && q3np_codec_fields(&io,copy) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) q3np_fail(error,QA_ERROR_FORMAT,"Native Q3 particle continuation is inconsistent");
    qa_source_save_dispose(&io); free(copy); o->busy=false; return ok;
}
bool q3n_particles_restore(q3n_particles *o, qa_bytes bytes, qa_error *error)
{
    if (!ready(o,error)) return false;
    if (o->initialized || o->count || o->view_roll!=0)
        return q3np_fail(error,QA_ERROR_ARGUMENT,"Particle restore requires its empty native candidate");
    for (size_t i=0;i<Q3N_PARTICLE_FRAMES;++i) if (o->shaders[i])
        return q3np_fail(error,QA_ERROR_ARGUMENT,"Particle restore cannot replace registered native media");
    o->busy=true; q3n_particles *candidate=calloc(1,sizeof(*candidate));
    if (!candidate) { o->busy=false; return q3np_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 particle restore candidate"); }
    candidate->assets=o->assets; candidate->product=o->product; candidate->remote_source=o->remote_source;
    candidate->compiled_source=o->compiled_source; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && q3np_codec_fields(&io,candidate) && qa_source_save_finish(&io,NULL);
    if (ok) { *o=*candidate; o->busy=true; }
    else if (error && error->code==QA_OK) q3np_fail(error,QA_ERROR_FORMAT,"Saved native Q3 particles do not bind their actual registry");
    qa_source_save_dispose(&io); free(candidate); o->busy=false; return ok;
}
