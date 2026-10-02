#include "internal.h"
#include "qa/scene_world_save.h"
#include "qa/scene_save.h"
#include "qa/source_save.h"
#include "qa/hash.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)

typedef struct light_state {
    qa_scene_image *encoded, *direct;
    bool valid, dynamic;
    uint8_t monolightmap;
    float *styles;
    uint8_t *pixels, *encoded_pixels;
} light_state;
typedef struct lighting_state {
    qa_scene_image **textures, *sky[6];
    light_state *lights;
    float *q1_styles;
    qa_vec3 *q2_styles;
    size_t style_count;
} lighting_state;

static bool failure(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error,status,0,"%s",message); return false;
}
static bool span_digest(qa_source_save_io *io, qa_bytes bytes)
{
    size_t count=bytes.size; qa_sha256_digest digest;
    qa_sha256(bytes,&digest);
    return qa_source_save_count(io,&count,SIZE_MAX) && qa_source_save_bytes(io,digest.bytes,sizeof(digest.bytes));
}
static bool text(qa_source_save_io *io, const char *name)
{
    bool present=name!=NULL; size_t count=name?strlen(name):0;
    return qa_source_save_bool(io,&present) && qa_source_save_count(io,&count,SIZE_MAX) &&
        qa_source_save_bytes(io,(void *)name,count);
}
/* The digest describes actual installed static owners, including selected
 * palette/light source bytes and texture extents that affect projection. */
static bool descriptor(qa_source_save_io *io, const qa_scene_world *world)
{
    uint32_t family=world->bsp.family, format=world->bsp.format;
    qa_scene_world_options options=world->options;
    qa_scene_image_options image=options.images;
    uint32_t image_family=image.family, wrap=image.wrap, filter=image.filter, usage=image.usage;
    int32_t transparent_index=image.transparent_index;
    uint32_t encoding=options.q1_lightmap_encoding;
    if (!qa_source_save_u32(io,&family) || !qa_source_save_u32(io,&format) ||
        !span_digest(io,(qa_bytes){world->bytes.data,world->bytes.size}) ||
        !span_digest(io,options.external_lit) || !span_digest(io,image.palette_rgb) || !span_digest(io,image.translation) ||
        !qa_source_save_bool(io,&options.has_external_entities) || !span_digest(io,options.external_entities) ||
        !text(io,options.q2_sky) || !qa_source_save_u32(io,&image_family) ||
        !qa_source_save_u32(io,&wrap) || !qa_source_save_u32(io,&filter) || !qa_source_save_u32(io,&usage) ||
        !qa_source_save_i32(io,&transparent_index)) return false;
    FIELD(bool,&image,mipmap); FIELD(bool,&image,transparent); FIELD(bool,&image,fullbright_only);
    FIELD(f32,&options,subdivisions); FIELD(f32,&options,q1_water_alpha); FIELD(f32,&options,q2_light_modulate);
    FIELD(u32,&options,q3_overbright);
    if (!qa_source_save_u32(io,&encoding)) return false;
    const qawl_world *data=world->legacy_data;
    size_t textures=data->texture_count, surfaces=world->surface_count;
    if (!qa_source_save_count(io,&textures,SIZE_MAX) || !qa_source_save_count(io,&surfaces,SIZE_MAX)) return false;
    for (size_t i=0;i<textures;++i) {
        qawl_texture texture=data->textures[i];
        if (!text(io,texture.name)) return false;
        FIELD(u32,&texture,width); FIELD(u32,&texture,height); FIELD(u32,&texture,quake64_shift); FIELD(i32,&texture,next);
        for (size_t j=0;j<2;++j) {
            if (!qa_source_save_count(io,&texture.animation_count[j],10)) return false;
            for (size_t k=0;k<texture.animation_count[j];++k)
                if (!qa_source_save_count(io,&texture.animation[j][k],SIZE_MAX)) return false;
        }
    }
    for (size_t i=0;i<surfaces;++i) {
        const qaw_legacy *original=world->surfaces[i].legacy;
        bool present=original!=NULL;
        if (!qa_source_save_bool(io,&present)) return false;
        if (!present) continue;
        qaw_legacy light=*original;
        uint32_t source=world->surfaces[i].source_index;
        if (!qa_source_save_u32(io,&source) || !qa_source_save_count(io,&light.texture,SIZE_MAX) ||
            !qa_source_save_count(io,&light.frame_count,SIZE_MAX)) return false;
        for (size_t j=0;j<light.frame_count;++j) {
            size_t frame=light.frames[j]; if (!qa_source_save_count(io,&frame,SIZE_MAX)) return false;
        }
        FIELD(bool,&light,warp); FIELD(bool,&light,flowing); FIELD(bool,&light,fence);
        FIELD(bool,&light,lightmapped); FIELD(bool,&light,decoupled); FIELD(f32,&light,alpha);
        FIELD(u32,&light,width); FIELD(u32,&light,height);
        for (size_t j=0;j<2;++j) {
            for (size_t k=0;k<4;++k) if (!qa_source_save_f32(io,&light.projection[j][k])) return false;
            if (!qa_source_save_f32(io,&light.light_step[j])) return false;
        }
        if (!qa_source_save_count(io,&light.sample_offset,SIZE_MAX) || !qa_source_save_count(io,&light.style_count,SIZE_MAX)) return false;
        for (size_t j=0;j<light.style_count;++j) {
            uint16_t style=light.styles[j]; if (!qa_source_save_u16(io,&style)) return false;
        }
    }
    return true;
}
static bool qualify(qa_source_save_io *io, const qa_scene_world *world)
{
    qa_source_save_io description; qa_buffer bytes={0};
    if (!qa_source_save_writer(&description,NULL,io->error)) return false;
    bool ok=descriptor(&description,world) && qa_source_save_finish(&description,&bytes);
    qa_source_save_dispose(&description);
    qa_sha256_digest actual={0}, saved;
    if (ok) qa_sha256((qa_bytes){bytes.data,bytes.size},&actual);
    qa_buffer_free(&bytes); saved=actual;
    if (ok) ok=qa_source_save_bytes(io,saved.bytes,sizeof(saved.bytes)) && !memcmp(actual.bytes,saved.bytes,sizeof(saved.bytes));
    return ok;
}
static bool image(qa_source_save_io *io, const qa_scene_world *world,
    const qa_scene_world_image_refs *refs, qa_scene_image **value)
{
    bool present=*value!=NULL; uint64_t key=0;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction==QA_SOURCE_SAVE_READ) *value=NULL; return true; }
    if (io->direction==QA_SOURCE_SAVE_WRITE) {
        const qa_scene_resources *owners[]={world->resources}; size_t index;
        if (!qa_scene_image_owner_index(owners,1,*value,&index) ||
            !refs->encode(refs->context,*value,&key,io->error)) return false;
    }
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded=NULL;
        const qa_scene_resources *owners[]={world->resources}; size_t index;
        if (!refs->decode(refs->context,key,&decoded,io->error) || !decoded ||
            !qa_scene_image_owner_index(owners,1,decoded,&index)) return false;
        qa_scene_image_retain(decoded); *value=(qa_scene_image *)decoded;
    }
    return true;
}
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t stride)
{
    if (count>SIZE_MAX/stride) return false;
    *out=count?calloc(count,stride):NULL;
    return !count || *out || failure(io->error,QA_ERROR_MEMORY,"Allocating saved world lighting");
}
static bool fields(qa_source_save_io *io, const qa_scene_world *world,
    const qa_scene_world_image_refs *refs, lighting_state *saved)
{
    uint8_t magic[4]={'Q','W','L','S'}; uint32_t schema=3;
    bool reading=io->direction==QA_SOURCE_SAVE_READ, q1=world->bsp.family==QA_BSP_Q1;
    const qawl_world *data=world->legacy_data;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QWLS",4) ||
        !qa_source_save_u32(io,&schema) || schema!=3 || !qualify(io,world)) return false;
    if (!reading) saved->style_count=data->style_count;
    if (!qa_source_save_count(io,&saved->style_count,reading?io->input.size/(q1?4:12):SIZE_MAX)) return false;
    if (reading) {
        if (q1 ? !allocate(io,(void **)&saved->q1_styles,saved->style_count,sizeof(float)) :
            !allocate(io,(void **)&saved->q2_styles,saved->style_count,sizeof(qa_vec3))) return false;
        if (!allocate(io,(void **)&saved->textures,data->texture_count,4*sizeof(*saved->textures)) ||
            !allocate(io,(void **)&saved->lights,world->surface_count,sizeof(*saved->lights))) return false;
    }
    for (size_t i=0;i<saved->style_count;++i) {
        if (q1) {
            float value=reading?0:data->q1_styles[i];
            if (!qa_source_save_f32(io,&value) || !isfinite(value)) return false;
            if (reading) saved->q1_styles[i]=value;
        } else {
            qa_vec3 value=reading?(qa_vec3){0}:data->q2_styles[i];
            if (!qa_source_save_vec3(io,&value) || !qa_vec_finite(value)) return false;
            if (reading) saved->q2_styles[i]=value;
        }
    }
    for (size_t i=0;i<data->texture_count;++i) {
        const qawl_texture *texture=&data->textures[i];
        qa_scene_image *values[4]={texture->image,texture->fullbright,texture->sky[0],texture->sky[1]};
        for (size_t j=0;j<4;++j) {
            qa_scene_image **value=reading?&saved->textures[i*4+j]:&values[j];
            if (!image(io,world,refs,value)) return false;
            if (!j && !*value) return false;
        }
    }
    for (size_t i=0;i<6;++i) {
        qa_scene_image *value=data->sky[i];
        if (!image(io,world,refs,reading?&saved->sky[i]:&value)) return false;
    }
    for (size_t i=0;i<world->surface_count;++i) {
        const qaw_surface *surface=&world->surfaces[i]; const qaw_legacy *light=surface->legacy;
        if (!light) continue;
        light_state original={.encoded=surface->lightmap,.direct=light->direct_lightmap,
            .valid=light->light_cache_valid,.dynamic=light->light_cache_dynamic,
            .monolightmap=light->light_cache_monolightmap,
            .styles=light->cached_styles,.pixels=light->light_pixels,.encoded_pixels=light->encoded_pixels};
        light_state *state=reading?&saved->lights[i]:&original;
        if (!image(io,world,refs,&state->encoded) || !image(io,world,refs,&state->direct)) return false;
        FIELD(bool,state,valid); FIELD(bool,state,dynamic);
        FIELD(u8,state,monolightmap);
        if (!light->lightmapped) {
            if (state->encoded || state->direct || state->valid || state->dynamic) return false;
            continue;
        }
        size_t pixels=(size_t)light->width*light->height, styles=light->style_count*3+1;
        if (!light->width || !light->height || light->width>SIZE_MAX/light->height || pixels>SIZE_MAX/4 ||
            light->style_count>(SIZE_MAX-1)/3 || !state->encoded || !state->direct) return false;
        const qa_scene_image *versions[]={state->encoded,state->direct};
        for (size_t j=0;j<2;++j) {
            const qa_scene_image *version=versions[j];
            if (version->kind!=QA_SCENE_RGBA8 || version->level_count!=1 || version->levels[0].width!=light->width ||
                version->levels[0].height!=light->height || version->levels[0].bytes!=pixels*4) return false;
        }
        if (!light->encoded_pixels && state->encoded!=state->direct) return false;
        if (!reading && (!state->styles || !state->pixels)) return false;
        if (reading && (!allocate(io,(void **)&state->styles,styles,sizeof(float)) ||
            !allocate(io,(void **)&state->pixels,pixels,4) ||
            (light->encoded_pixels && !allocate(io,(void **)&state->encoded_pixels,pixels,4)))) return false;
        for (size_t j=0;j<styles;++j) if (!qa_source_save_f32(io,&state->styles[j]) || !isfinite(state->styles[j])) return false;
        if (!qa_source_save_bytes(io,state->pixels,pixels*4) ||
            (light->encoded_pixels && !qa_source_save_bytes(io,state->encoded_pixels,pixels*4))) return false;
        /* A failed update may leave partly written buffers with the previous
         * images. Only a valid cache guarantees their published byte match. */
        if (state->valid && (memcmp(state->pixels,state->direct->levels[0].pixels,pixels*4) ||
            memcmp(state->encoded_pixels?state->encoded_pixels:state->pixels,state->encoded->levels[0].pixels,pixels*4))) return false;
    }
    return true;
}
static void discard(const qa_scene_world *world, lighting_state *saved)
{
    const qawl_world *data=world->legacy_data;
    if (saved->textures) for (size_t i=0;i<data->texture_count*4;++i) qa_scene_image_release(saved->textures[i]);
    for (size_t i=0;i<6;++i) qa_scene_image_release(saved->sky[i]);
    if (saved->lights) for (size_t i=0;i<world->surface_count;++i) {
        light_state *state=&saved->lights[i];
        qa_scene_image_release(state->encoded); qa_scene_image_release(state->direct);
        free(state->styles); free(state->pixels); free(state->encoded_pixels);
    }
    free(saved->textures); free(saved->lights); free(saved->q1_styles); free(saved->q2_styles);
}
static void publish(qa_scene_world *world, lighting_state *saved)
{
    qawl_world *data=world->legacy_data;
    free(data->q1_styles); free(data->q2_styles);
    data->q1_styles=saved->q1_styles; data->q2_styles=saved->q2_styles; data->style_count=saved->style_count;
    saved->q1_styles=NULL; saved->q2_styles=NULL;
    for (size_t i=0;i<data->texture_count;++i) {
        qawl_texture *texture=&data->textures[i];
        qa_scene_image **destinations[]={&texture->image,&texture->fullbright,&texture->sky[0],&texture->sky[1]};
        for (size_t j=0;j<4;++j) {
            qa_scene_image_release(*destinations[j]); *destinations[j]=saved->textures[i*4+j]; saved->textures[i*4+j]=NULL;
            if (j < 2 && *destinations[j] && (*destinations[j])->recipient_upload_pixels)
                (*destinations[j])->recipient_mipmap = world->options.images.mipmap;
        }
    }
    for (size_t i=0;i<6;++i) { qa_scene_image_release(data->sky[i]); data->sky[i]=saved->sky[i]; saved->sky[i]=NULL; }
    for (size_t i=0;i<world->surface_count;++i) {
        qaw_surface *surface=&world->surfaces[i]; qaw_legacy *light=surface->legacy;
        if (!light) continue;
        light_state *state=&saved->lights[i];
        qa_scene_image_release(surface->lightmap); qa_scene_image_release(light->direct_lightmap);
        surface->lightmap=state->encoded; light->direct_lightmap=state->direct; state->encoded=state->direct=NULL;
        free(light->cached_styles); free(light->light_pixels); free(light->encoded_pixels);
        light->cached_styles=state->styles; light->light_pixels=state->pixels; light->encoded_pixels=state->encoded_pixels;
        state->styles=NULL; state->pixels=state->encoded_pixels=NULL;
        light->light_cache_valid=state->valid; light->light_cache_dynamic=state->dynamic;
        light->light_cache_monolightmap=state->monolightmap;
        /* Accumulation is operation scratch, zeroed before every update. */
    }
}
static bool ready(const qa_scene_world *world, const qa_scene_world_image_refs *refs, qa_error *error)
{
    return (qa_scene_world_observation_ready(world) && world->bsp.family!=QA_BSP_Q3 && world->legacy_data &&
        refs && refs->encode && refs->decode) || failure(error,QA_ERROR_ARGUMENT,"Lighting continuation requires an idle legacy world and image resolver");
}
bool qaw_lighting_checkpoint_locked(const qa_scene_world *world, const qa_scene_world_image_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!world || !world->checkpoint_active || !out || !refs)
        return failure(error,QA_ERROR_ARGUMENT,"Legacy lighting capture lease is missing");
    qa_source_save_io io; lighting_state saved={0};
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=fields(&io,world,refs,&saved) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid legacy lighting state");
    qa_source_save_dispose(&io); return ok;
}
bool qa_scene_world_lighting_checkpoint(const qa_scene_world *world, const qa_scene_world_image_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || !ready(world,refs,error)) return false;
    qa_scene_world *owner=(qa_scene_world *)world;
    owner->checkpoint_active=true;
    bool ok=qaw_lighting_checkpoint_locked(world,refs,out,error);
    owner->checkpoint_active=false;
    return ok;
}
bool qaw_lighting_restore_locked(qa_scene_world *world, qa_bytes bytes, const qa_scene_world_image_refs *refs, qa_error *error)
{
    if (!world || !world->checkpoint_active || !refs)
        return failure(error,QA_ERROR_ARGUMENT,"Legacy lighting restore lease is missing");
    qa_source_save_io io; lighting_state saved={0};
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=fields(&io,world,refs,&saved) && qa_source_save_finish(&io,NULL);
    if (ok) publish(world,&saved);
    else if (error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid saved legacy lighting state");
    discard(world,&saved); qa_source_save_dispose(&io); return ok;
}
bool qa_scene_world_lighting_restore(qa_scene_world *world, qa_bytes bytes, const qa_scene_world_image_refs *refs, qa_error *error)
{
    if (!qa_scene_world_idle(world)) return failure(error,QA_ERROR_ARGUMENT,"Lighting import requires an uncaptured world owner");
    if (!ready(world,refs,error)) return false;
    world->checkpoint_active=true;
    bool ok=qaw_lighting_restore_locked(world,bytes,refs,error);
    world->checkpoint_active=false;
    return ok;
}
#undef FIELD
