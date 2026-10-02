#include "remote_q2_effects_private.h"
#include "save_private.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool vector(qa_source_save_io *io, qa_vec3 *v)
{ return qa_source_save_vec3(io,v) && qa_vec_finite(*v); }
static bool number(qa_source_save_io *io, double *v)
{ return qa_source_save_f64(io,v) && isfinite(*v); }
static bool scalar(qa_source_save_io *io, float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
bool q2fx_actor_fields(qa_source_save_io *io, qa_actor_id *id, const frontend_remote_q2_effects_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ, present=!reading && id->registry!=0;
    qa_saved_actor_id saved={0};
    if (!reading && present && (!refs->actor_encode || !refs->actor_encode(refs->context,*id,&saved,io->error))) return false;
    if (!qa_source_save_bool(io,&present) || !qa_source_save_u64(io,&saved.generation) || !qa_source_save_u32(io,&saved.slot)) return false;
    if (!present) { if (saved.generation || saved.slot) return false; if (reading) *id=(qa_actor_id){0}; return true; }
    if (!saved.generation) return false;
    if (reading && (!refs->actor_decode || !refs->actor_decode(refs->context,saved,id,io->error))) return false;
    return id->registry && id->generation;
}
bool q2fx_light_identity_fields(qa_source_save_io *io, uint64_t *identity, const frontend_remote_q2_effects_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint64_t saved=0;
    if (!reading && *identity && (!refs->light_encode ||
        !refs->light_encode(refs->context,*identity,&saved,io->error) || !saved)) return false;
    if (!qa_source_save_u64(io,&saved)) return false;
    if (reading) {
        *identity=0;
        if (saved && (!refs->light_decode || !refs->light_decode(refs->context,saved,identity,io->error) || !*identity)) return false;
    }
    return true;
}
bool q2fx_state_fields(qa_source_save_io *io, frontend_remote_q2_effects *o, const frontend_remote_q2_effects_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint32_t event_status=o->event_error.code;
    if (!qa_source_save_bool(io,&o->event_received) || !qa_source_save_bool(io,&o->event_failed) ||
        !qa_source_save_u64(io,&o->event_sequence) || !qa_source_save_u32(io,&event_status) || event_status>QA_ERROR_NOT_FOUND ||
        !qa_source_save_count(io,&o->event_error.offset,SIZE_MAX) ||
        !qa_source_save_bytes(io,o->event_error.message,sizeof(o->event_error.message)) ||
        !memchr(o->event_error.message,0,sizeof(o->event_error.message)) ||
        (o->event_failed && (!o->event_received || event_status==QA_OK)) ||
        (!o->event_failed && (event_status!=QA_OK || o->event_error.offset || o->event_error.message[0])) ||
        (!o->event_received && o->event_sequence)) return false;
    if (reading) o->event_error.code=(qa_status)event_status;
    if (!frontend_save_random(io,&o->random) || !frontend_fx_particles_fields(io,&o->particles) || o->particles.family!=QA_GAME_Q2 ||
        !qa_source_save_u32(io,&o->slow_bin) || !qa_source_save_u32(io,&o->slow_base) ||
        !qa_source_save_u32(io,&o->slow_seed) || !qa_source_save_u64(io,&o->slow_frame) || !qa_source_save_u64(io,&o->render_frame) ||
        !qa_source_save_count(io,&o->sampled_particle_count,FRONTEND_FX_PARTICLE_CAPACITY) ||
        !number(io,&o->time) || !number(io,&o->server_time) || !qa_source_save_u64(io,&o->frame_sequence) ||
        !qa_source_save_bool(io,&o->sampled) || !qa_source_save_bool(io,&o->dirty) || o->sampled_particle_count>o->particles.count ||
        (!o->sampled && (o->sampled_particle_count || o->frame_sequence || o->time || o->server_time))) return false;
    for (size_t i=0;i<Q2FX_POOL;++i) {
        q2fx_explosion *x=&o->explosions[i];
        if (!qa_source_save_bool(io,&x->active) || !qa_source_save_u8(io,&x->kind) || !qa_source_save_u8(io,&x->model) ||
            !qa_source_save_i32(io,&x->frames) || !qa_source_save_i32(io,&x->base) || !qa_source_save_i32(io,&x->skin) ||
            !qa_source_save_u32(io,&x->flags) || !vector(io,&x->origin) || !vector(io,&x->angles) || !vector(io,&x->light_color) ||
            !number(io,&x->start) || !scalar(io,&x->light) || !scalar(io,&x->scale) ||
            (x->active && (!x->kind || x->kind>5 || x->model>=Q2FX_MODEL_COUNT || x->frames<2 || x->base<0 || x->skin<0 || x->scale<=0)) ||
            (x->active && x->kind==4 && (o->source.profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE ||
                x->model<Q2FX_MUZZLE_MACHINE || !o->models[x->model] || x->flags!=(8|32|8192))) ||
            (x->active && x->kind==5 && o->source.profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)) return false;
        q2fx_light *light=&o->lights[i];
        if (!qa_source_save_bool(io,&light->active) || !q2fx_actor_fields(io,&light->actor,refs) || !vector(io,&light->origin) || !vector(io,&light->color) ||
            !number(io,&light->born) || !number(io,&light->die) || !scalar(io,&light->radius) || !scalar(io,&light->decay) ||
            !scalar(io,&light->minimum) || (light->active && (light->die<light->born || light->radius<0))) return false;
        q2fx_sustain *s=&o->sustains[i];
        if (!qa_source_save_bool(io,&s->active) || !qa_source_save_u8(io,&s->kind) || s->kind>2 ||
            !qa_source_save_i32(io,&s->id) || !qa_source_save_i32(io,&s->count) || !qa_source_save_i32(io,&s->color) ||
            !qa_source_save_i32(io,&s->magnitude) || !vector(io,&s->origin) || !vector(io,&s->direction) ||
            !number(io,&s->end) || !number(io,&s->next) || (s->active && (s->count<0 || s->color<0 || s->color>255))) return false;
        for (size_t pool=0;pool<2;++pool) {
            q2fx_beam *b=(pool?o->player_beams:o->beams)+i;
            if (!qa_source_save_bool(io,&b->active) || !qa_source_save_bool(io,&b->player) || !qa_source_save_bool(io,&b->monster) ||
                !qa_source_save_bool(io,&b->unkeyed) ||
                !qa_source_save_u8(io,&b->model) || !q2fx_actor_fields(io,&b->actor,refs) || !q2fx_actor_fields(io,&b->destination,refs) ||
                !vector(io,&b->start) || !vector(io,&b->end) || !vector(io,&b->offset) || !number(io,&b->die) || !number(io,&b->sound_until) ||
                (b->active && ((!b->actor.registry && !b->unkeyed) || b->player!=(pool!=0) || b->model>=Q2FX_MODEL_COUNT ||
                    (b->unkeyed && (b->actor.registry || b->destination.registry || b->player || b->monster || b->model!=Q2FX_LIGHTNING)) ||
                    (b->model!=Q2FX_PARASITE && b->model!=Q2FX_CABLE && b->model!=Q2FX_LIGHTNING && b->model!=Q2FX_HEAT)))) return false;
        }
    }
    for (size_t i=0;i<Q2FX_LASER_CAPACITY;++i) {
        q2fx_laser *laser=&o->lasers[i];
        if (!qa_source_save_bool(io,&laser->active) || !vector(io,&laser->start) || !vector(io,&laser->end) ||
            !number(io,&laser->born) || !number(io,&laser->die) || !qa_source_save_u32(io,&laser->color) ||
            !qa_source_save_u32(io,&laser->rgba) || (laser->color>255 && laser->color!=UINT32_MAX) ||
            (laser->color!=UINT32_MAX && laser->rgba) || (laser->active && laser->die<laser->born) ||
            !scalar(io,&laser->width) || (laser->active && laser->width<=0) ||
            (laser->active && i>=Q2FX_POOL && o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC && laser->color!=UINT32_MAX)) return false;
    }
    size_t maximum=reading?(io->input.size-io->offset)/24:SIZE_MAX/sizeof(*o->trails);
    if (!qa_source_save_count(io,&o->trail_count,maximum)) return false;
    if (reading && o->trail_count) {
        o->trails=calloc(o->trail_count,sizeof(*o->trails)); if (!o->trails) return false;
    }
    for (size_t i=0;i<o->trail_count;++i) {
        q2fx_trail *t=&o->trails[i];
        if (!q2fx_actor_fields(io,&t->actor,refs) || !t->actor.registry || !vector(io,&t->origin) || !qa_source_save_i32(io,&t->count) ||
            t->count<0 || t->count>1024 || !number(io,&t->fly_end) || !scalar(io,&t->flashlight_fraction) ||
            t->flashlight_fraction<0 || t->flashlight_fraction>1) return false;
        for (size_t j=0;j<i;++j) if (qa_actor_id_equal(o->trails[j].actor,t->actor)) return false;
    }
    if (!q2fx_semantic_fields(io,o,refs) ||
        !qa_source_save_count(io,&o->light_count,SIZE_MAX/sizeof(*o->sampled_lights)) ||
        !qa_source_save_count(io,&o->light_capacity,SIZE_MAX/sizeof(*o->sampled_lights)) ||
        !qa_source_save_count(io,&o->transient_light_count,Q2FX_LIGHT_CAPACITY) ||
        o->light_count>o->light_capacity || o->transient_light_count>o->light_count || o->light_capacity<Q2FX_LIGHT_CAPACITY) return false;
    if (reading) {
        o->sampled_lights=calloc(o->light_capacity,sizeof(*o->sampled_lights));
        if (!o->sampled_lights) return false;
    }
    for (size_t i=0;i<o->light_count;++i) {
        qa_scene_light *l=&o->sampled_lights[i];
        if (!vector(io,&l->origin) || !vector(io,&l->color) || !vector(io,&l->direction) ||
            !scalar(io,&l->radius) || !scalar(io,&l->minimum) || !scalar(io,&l->scale) || !scalar(io,&l->cos_half_angle) ||
            !qa_source_save_bool(io,&l->spot) || !qa_source_save_bool(io,&l->casts_shadow) ||
            !q2fx_light_identity_fields(io,&l->identity,refs) || !qa_source_save_u64(io,&l->revision) ||
            !qa_source_save_u32(io,&l->shadow_resolution) || l->radius<=0 || l->cos_half_angle< -1 || l->cos_half_angle>1) return false;
        l->family=QA_SCENE_Q2; l->additive=true;
    }
    maximum=reading?(io->input.size-io->offset)/61:SIZE_MAX/sizeof(*o->draws);
    if (!qa_source_save_count(io,&o->draw_count,maximum) ||
        !qa_source_save_count(io,&o->draw_capacity,SIZE_MAX/sizeof(*o->draws)) || o->draw_count>o->draw_capacity) return false;
    if (reading && o->draw_capacity) { o->draws=calloc(o->draw_capacity,sizeof(*o->draws)); if (!o->draws) return false; }
    for (size_t i=0;i<o->draw_count;++i) {
        q2fx_model_draw *d=&o->draws[i];
        if (!qa_source_save_u8(io,&d->model) || d->model>=Q2FX_MODEL_COUNT || !vector(io,&d->origin) || !vector(io,&d->angles) ||
            !qa_source_save_i32(io,&d->frame) || !qa_source_save_i32(io,&d->old_frame) || !qa_source_save_i32(io,&d->skin) ||
            !o->models[d->model] || !qa_source_save_u32(io,&d->flags) || !scalar(io,&d->alpha) || !scalar(io,&d->back_lerp) || !vector(io,&d->scale) ||
            d->frame<0 || d->old_frame<0 || d->skin<0 || d->scale.x<=0 || d->scale.y<=0 || d->scale.z<=0 || d->back_lerp<0 || d->back_lerp>1) return false;
    }
    q2fx_weapon_muzzle *m=&o->weapon_muzzle;
    if (!qa_source_save_u32(io,&o->sampled_dlight_hacks) || !qa_source_save_u32(io,&o->sampled_disable_particles) ||
        !qa_source_save_i32(io,&o->sampled_gun) || !scalar(io,&o->sampled_gun_fov) ||
        (!o->sampled && (o->sampled_dlight_hacks || o->sampled_disable_particles || o->sampled_gun || o->sampled_gun_fov))) return false;
    if (!qa_source_save_bool(io,&m->active) || !qa_source_save_u8(io,&m->model) || !q2fx_actor_fields(io,&m->actor,refs) ||
        !vector(io,&m->offset) || !scalar(io,&m->scale) || !scalar(io,&m->roll) || !number(io,&m->start) ||
        (m->active && (o->source.profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE || !m->actor.registry ||
            m->model<Q2FX_MUZZLE_MACHINE || m->model>=Q2FX_MODEL_COUNT || !o->models[m->model] || m->scale<=0 || m->roll<0 || m->roll>=360))) return false;
    return o->sampled || (!o->trail_count && !o->light_count && !o->draw_count);
}
static bool fields(qa_source_save_io *io, frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_refs *refs)
{
    uint8_t magic[4]={'Q','2','F','X'}; uint32_t version=4;
    uint64_t identity=o->source.identity, generation=o->source.content_generation, image=0;
    uint32_t profile=o->source.profile,protocol=o->source.protocol.kind, revision=o->source.protocol.revision, flags=o->source.protocol.flags;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q2FX",4) || !qa_source_save_u32(io,&version) || version!=4 ||
        !qa_source_save_u64(io,&identity) || identity!=o->source.identity || !qa_source_save_u64(io,&generation) || generation!=o->source.content_generation ||
        !qa_source_save_u32(io,&profile) || profile!=(uint32_t)o->source.profile ||
        !qa_source_save_u32(io,&protocol) || protocol!=(uint32_t)o->source.protocol.kind ||
        !qa_source_save_u32(io,&revision) || revision!=o->source.protocol.revision || !qa_source_save_u32(io,&flags) || flags!=o->source.protocol.flags) return false;
    if (!reading && (!refs->image_encode || !refs->image_encode(refs->context,o->particle_image,&image,io->error))) return false;
    if (!qa_source_save_u64(io,&image) || !image) return false;
    if (reading) {
        const qa_scene_image *decoded=NULL;
        if (!refs->image_decode || !refs->image_decode(refs->context,image,&decoded,io->error) ||
            !decoded || qa_scene_image_owner(decoded)!=o->source.images) return false;
        qa_scene_image_retain(decoded); o->particle_image=decoded;
    }
    for (size_t i=0;i<Q2FX_MODEL_COUNT;++i) {
        bool present=o->models[i]!=NULL;
        if (!qa_source_save_bool(io,&o->model_admitted[i]) || !qa_source_save_bool(io,&present) ||
            (!o->model_admitted[i] && present)) return false;
        if (o->model_admitted[i]) {
            qa_scene_model *held=NULL;
            if (!o->source.model(o->source.context,q2fx_model_paths[i],false,&held,io->error) ||
                present!=(held!=NULL) || (!reading && held!=o->models[i])) return false;
            if (reading) o->models[i]=held;
        }
    }
    return q2fx_state_fields(io,o,refs);
}
bool frontend_remote_q2_effects_checkpoint(const frontend_remote_q2_effects *source,
    const frontend_remote_q2_effects_refs *refs, qa_buffer *out, qa_error *e)
{
    if (!source || !frontend_remote_q2_effects_idle(source) || !refs || !out || out->data || out->size ||
        !source->particle_image || qa_scene_image_owner(source->particle_image)!=source->source.images || !q2fx_source_current(source,e)) return false;
    frontend_remote_q2_effects *copy=malloc(sizeof(*copy));
    if (!copy) return q2fx_fail(e,QA_ERROR_MEMORY,"Capturing remote Q2 effect continuation");
    *copy=*source; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,source->source.session,e) && fields(&io,copy,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); free(copy);
    if (ok) ok=q2fx_source_current(source,e);
    if (!ok) { qa_buffer_free(out); if (!e || e->code==QA_OK) q2fx_fail(e,QA_ERROR_FORMAT,"Invalid retained Q2 effect continuation"); }
    return ok;
}
bool frontend_remote_q2_effects_restore(const frontend_remote_q2_effects_source *source,
    const frontend_remote_q2_effects_refs *refs, qa_bytes bytes, frontend_remote_q2_effects **out, qa_error *e)
{
    if (!out || *out || !q2fx_source_valid(source) || !refs || !refs->image_decode) return false;
    frontend_remote_q2_effects *o=calloc(1,sizeof(*o));
    if (!o) return q2fx_fail(e,QA_ERROR_MEMORY,"Restoring remote Q2 effect continuation");
    o->source=*source; *out=o; qa_source_save_io io={0};
    bool ok=q2fx_source_current(o,e) && qa_source_save_reader(&io,source->session,bytes,e) && fields(&io,o,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_bytes palette={0};
    if (ok) ok=qa_scene_resources_palette_read(source->images,QA_SCENE_Q2,&palette) && palette.size>=768 && q2fx_source_current(o,e);
    if (!ok && (!e || e->code==QA_OK)) q2fx_fail(e,QA_ERROR_FORMAT,"Q2 effects cold state leaves its retained CLIENT graph");
    return ok;
}
