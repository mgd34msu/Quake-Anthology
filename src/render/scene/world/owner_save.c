#include "owner_legacy.h"
#include "q3/internal.h"
#include "qa/material_library_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define F(type,object,member) do { if (!qa_source_save_##type(io,&(object)->member)) return false; } while (0)
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
static bool buffer(qa_source_save_io *io, qa_buffer *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!qa_source_save_count(io,&value->size,reading?io->input.size-io->offset:SIZE_MAX)) return false;
    if (reading) {
        if (value->size>io->input.size-io->offset) return false;
        value->data=value->size?malloc(value->size):NULL;
        if (value->size && !value->data) return fail(io->error,QA_ERROR_MEMORY,"Copying retained scene source bytes");
    }
    return qa_source_save_bytes(io,value->data,value->size);
}
static bool text(qa_source_save_io *io, char **value)
{
    bool present=*value!=NULL, reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=present?strlen(*value):0;
    if (!qa_source_save_bool(io,&present) || !qa_source_save_count(io,&count,reading?io->input.size-io->offset:SIZE_MAX) ||
        count==SIZE_MAX || (!present && count)) return false;
    if (!present) return true;
    if (reading) {
        if (count>io->input.size-io->offset) return false;
        *value=calloc(count+1,1);
        if (!*value) return fail(io->error,QA_ERROR_MEMORY,"Copying retained scene sky name");
    }
    return qa_source_save_bytes(io,*value,count) && (!memchr(*value,0,count) ||
        fail(io->error,QA_ERROR_FORMAT,"Embedded NUL in scene sky name"));
}
static bool palette(qa_source_save_io *io, qa_scene_world *world)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool borrowed=world->options.images.palette_rgb.data!=world->palette_bytes.data ||
        world->options.images.palette_rgb.size!=world->palette_bytes.size;
    if (!qa_source_save_bool(io,&borrowed)) return false;
    qa_bytes owned={world->palette_bytes.data,world->palette_bytes.size};
    if (!borrowed) {
        if (reading) world->options.images.palette_rgb=owned;
        return true;
    }
    qa_bytes installed={0};
    if (world->palette_bytes.size || !qa_scene_resources_palette_read(world->resources,world->options.images.family,&installed))
        return fail(io->error,QA_ERROR_FORMAT,"World borrowed palette owner is absent");
    size_t bytes=reading?0:world->options.images.palette_rgb.size;
    if (!qa_source_save_count(io,&bytes,installed.size) || bytes!=installed.size) return false;
    if (reading) {
        if (bytes>io->input.size-io->offset || memcmp(installed.data,io->input.data+io->offset,bytes)) return false;
        io->offset+=bytes; world->options.images.palette_rgb=installed; return true;
    }
    return world->options.images.palette_rgb.data==installed.data &&
        qa_source_save_bytes(io,(void *)installed.data,installed.size);
}
static bool source_buffer_copy(qa_buffer *out, qa_bytes bytes, qa_error *error)
{
    if (!bytes.size) return true;
    out->data = malloc(bytes.size);
    if (!out->data) return fail(error, QA_ERROR_MEMORY, "Restoring installed world source view");
    memcpy(out->data, bytes.data, bytes.size); out->size = bytes.size; return true;
}
static bool source(qa_source_save_io *io, qa_scene_world *world, const qa_scene_world_owner_refs *refs, const qa_bsp_view *expected)
{
    uint32_t family=world->bsp.family, format=world->bsp.format;
    qa_scene_world_options *options=&world->options; qa_scene_image_options *image=&options->images;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_buffer installed = {0};
    bool shared_policy = !reading && refs->source_options != NULL;
    if (!qa_source_save_u32(io, &family) || !qa_source_save_u32(io, &format) ||
        !buffer(io, !reading && world->source_resource ? &installed : &world->bytes) ||
        !qa_source_save_bool(io, &shared_policy)) return false;
    if (shared_policy) {
        qa_scene_world_options policy = {0};
        if (!refs->source_options || !refs->source_options(refs->context, &policy, io->error)) return false;
        if (reading && (!source_buffer_copy(&world->lit_bytes, policy.external_lit, io->error) ||
            !source_buffer_copy(&world->palette_bytes, policy.images.palette_rgb, io->error) ||
            !source_buffer_copy(&world->translation_bytes, policy.images.translation, io->error) ||
            !source_buffer_copy(&world->entity_bytes, policy.external_entities, io->error))) return false;
        if (reading && policy.q2_sky) {
            size_t size = strlen(policy.q2_sky);
            world->sky_name = malloc(size + 1);
            if (!world->sky_name) return fail(io->error, QA_ERROR_MEMORY, "Restoring world sky policy");
            memcpy(world->sky_name, policy.q2_sky, size + 1);
        }
        if (reading) options->has_external_entities = policy.has_external_entities;
    } else if (!buffer(io, &world->lit_bytes) || !buffer(io, &world->palette_bytes) ||
        !buffer(io, &world->translation_bytes) || !text(io, &world->sky_name) ||
        !qa_source_save_bool(io, &options->has_external_entities) || !buffer(io, &world->entity_bytes)) return false;
    if (options->has_external_entities ? family != QA_BSP_Q1 : world->entity_bytes.size != 0) return false;
    if (reading && !world->bytes.size && (!expected ||
        !source_buffer_copy(&world->bytes, expected->source, io->error))) return false;
    if (reading) {
        if (!qa_bsp_open((qa_bytes){world->bytes.data,world->bytes.size},&world->bsp,io->error) ||
            !qa_bsp_validate(&world->bsp,io->error) || world->bsp.family!=family || world->bsp.format!=format) return false;
        options->external_lit=(qa_bytes){world->lit_bytes.data,world->lit_bytes.size};
        options->external_entities=(qa_bytes){world->entity_bytes.data,world->entity_bytes.size};
        if (options->has_external_entities) world->bsp.lumps[QA_BSP_ENTITIES] =
            (qa_bsp_lump){.bytes = options->external_entities, .present = true};
        options->images.translation=(qa_bytes){world->translation_bytes.data,world->translation_bytes.size};
        if (shared_policy) options->images.palette_rgb=(qa_bytes){world->palette_bytes.data,world->palette_bytes.size};
        options->q2_sky=world->sky_name;
    } else if (options->external_lit.data!=world->lit_bytes.data || options->external_lit.size!=world->lit_bytes.size ||
        options->external_entities.data!=world->entity_bytes.data || options->external_entities.size!=world->entity_bytes.size ||
        (options->has_external_entities && (world->bsp.lumps[QA_BSP_ENTITIES].bytes.data!=world->entity_bytes.data ||
            world->bsp.lumps[QA_BSP_ENTITIES].bytes.size!=world->entity_bytes.size)) ||
        options->images.translation.data!=world->translation_bytes.data || options->images.translation.size!=world->translation_bytes.size ||
        options->q2_sky!=world->sky_name) return false;
    if (expected && (family!=expected->family || format!=expected->format || world->bytes.size!=expected->source.size ||
        (world->bytes.size && memcmp(world->bytes.data,expected->source.data,world->bytes.size)))) return false;
    if (!reading && (world->bsp.source.data!=world->bytes.data || world->bsp.source.size!=world->bytes.size)) return false;
    uint32_t image_family=image->family, wrap=image->wrap, filter=image->filter, usage=image->usage, encoding=options->q1_lightmap_encoding;
    int32_t transparent_index=image->transparent_index;
    if (!qa_source_save_u32(io,&image_family) || image_family!=(family==QA_BSP_Q1?QA_SCENE_Q1:family==QA_BSP_Q2?QA_SCENE_Q2:QA_SCENE_Q3) ||
        !qa_source_save_u32(io,&wrap) || wrap>QA_SCENE_CLAMP || !qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io,&usage) || usage>QA_IMAGE_USAGE_SKY || !qa_source_save_i32(io,&transparent_index)) return false;
    if (reading) {
        image->family=(qa_scene_family)image_family; image->wrap=(qa_scene_wrap)wrap; image->filter=(qa_scene_filter)filter;
        image->usage=(qa_scene_image_usage)usage; image->transparent_index=transparent_index;
    }
    F(bool,image,mipmap); F(bool,image,transparent); F(bool,image,fullbright_only);
    F(f32,options,subdivisions); F(f32,options,q1_water_alpha); F(f32,options,q2_light_modulate); F(u32,options,q3_overbright);
    F(bool,options,source_fullbright);
    if (!qa_source_save_u32(io,&encoding)) return false;
    if (reading) options->q1_lightmap_encoding=(qa_scene_q1_lightmap_encoding)encoding;
    if ((!shared_policy && !palette(io,world)) || !refs->source_qualify(refs->context,(qa_bytes){world->bytes.data,world->bytes.size},options,io->error)) return false;
    if (family==QA_BSP_Q3) return options->q3_overbright<=15;
    if (!isfinite(options->q1_water_alpha) || !isfinite(options->q2_light_modulate) ||
        (family==QA_BSP_Q1 && encoding>QA_Q1_LIGHTMAP_INVERTED_ALPHA)) return false;
    if (reading) {
        if (!qa_bsp_select_lighting(&world->bsp,options->external_lit,&world->lighting,io->error)) return false;
        qa_bsp_extension extension;
        if (qa_bsp_find_extension(&world->bsp,"LIGHTGRID_OCTREE",&extension,NULL) &&
            !qa_bsp_read_lightgrid(&world->bsp,&world->lightgrid,io->error)) return false;
    }
    return true;
}
static bool blob(qa_source_save_io *io, qa_bytes *value)
{
    size_t count=value->size;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)value->data,count);
    if (count>io->input.size-io->offset) return false;
    *value=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool q3_source_images(const qa_scene_world *world, qa_error *error)
{
    if (world->bsp.family!=QA_BSP_Q3) return true;
    const q3_data *data=world->q3_data;
    qa_bytes lighting=world->bsp.lumps[QA_BSP_LIGHTING].bytes;
    for (size_t i=0;i<data->lightmap_count;++i) {
        const qa_scene_image *image=data->lightmaps[i];
        if (!image || image->kind!=QA_SCENE_RGB8 || image->wrap!=QA_SCENE_CLAMP || image->filter!=QA_SCENE_LINEAR ||
            image->level_count!=1 || image->logical_width!=128 || image->logical_height!=128 || image->animation_count ||
            image->levels[0].width!=128 || image->levels[0].height!=128 || image->levels[0].bytes!=128*128*4)
            return fail(error,QA_ERROR_FORMAT,"Q3 retained source lightmap shape changed");
        for (size_t pixel=0;pixel<128*128;++pixel) {
            uint8_t rgb[3]; qaw_q3_shift_color(lighting.data+i*(128*128*3)+pixel*3,world->options.q3_overbright,rgb);
            const uint8_t *saved=(const uint8_t *)image->levels[0].pixels+pixel*4;
            if (memcmp(rgb,saved,3) || saved[3]!=255) return fail(error,QA_ERROR_FORMAT,"Q3 retained lightmap differs from immutable source");
        }
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_scene_world *world, const qa_scene_world_owner_refs *refs,
    const qa_bsp_view *expected, qa_bytes *state)
{
    uint8_t magic[4]={'Q','W','O','N'}; if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QWON",4) || !qa_source_save_u64(io,&world->identity) || !world->identity || !source(io,world,refs,expected)) return false;
    qaw_owner_refs core={.context=refs->context,.geometry_encode=refs->geometry_encode,
        .geometry_decode=refs->geometry_decode,.images=refs->state.images};
    return qaw_owner_core_fields(io,world,&core) &&
        (world->bsp.family==QA_BSP_Q3 || qaw_owner_legacy_fields(io,world,&core)) &&
        q3_source_images(world,io->error) && blob(io,state);
}
static bool refs_ready(const qa_scene_world_owner_refs *refs)
{
    return refs && refs->geometry_encode && refs->geometry_decode && refs->source_qualify && refs->identity_decode &&
        refs->state.images.encode && refs->state.images.decode && refs->state.material_encode && refs->state.material_decode &&
        refs->state.frame_encode && refs->state.frame_decode;
}
static int identity_compare(const void *a, const void *b)
{
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return x<y?-1:x>y;
}
static bool identities(qa_scene_world *world, const qa_scene_world_owner_refs *refs, qa_error *error)
{
    if (world->model_count>SIZE_MAX-1 || world->surface_count>SIZE_MAX-1-world->model_count)
        return fail(error,QA_ERROR_FORMAT,"Scene namespace extent overflow");
    size_t capacity=1+world->model_count+world->surface_count;
    if (capacity>SIZE_MAX/sizeof(uint64_t)) return fail(error,QA_ERROR_FORMAT,"Scene namespace allocation overflow");
    uint64_t *seen=malloc(capacity*sizeof(*seen));
    if (!seen) return fail(error,QA_ERROR_MEMORY,"Qualifying restored scene namespace");
    size_t count=0; uint64_t saved=world->identity;
    bool ok=refs->identity_decode(refs->context,QA_SCENE_WORLD_IDENTITY_WORLD,0,saved,&world->identity,error) && world->identity;
    if (ok) seen[count++]=world->identity;
    for (size_t i=0;ok && i<world->model_count;++i) {
        saved=world->models[i].identity;
        ok=refs->identity_decode(refs->context,QA_SCENE_WORLD_IDENTITY_MODEL,i,saved,&world->models[i].identity,error) && world->models[i].identity;
        if (ok) seen[count++]=world->models[i].identity;
    }
    for (size_t i=0;ok && i<world->surface_count;++i) {
        qa_scene_mesh *mesh=&world->surfaces[i].mesh;
        if (!mesh->identity) continue;
        saved=mesh->identity;
        ok=refs->identity_decode(refs->context,QA_SCENE_WORLD_IDENTITY_MESH,i,saved,&mesh->identity,error) && mesh->identity;
        if (ok) seen[count++]=mesh->identity;
    }
    if (ok) {
        qsort(seen,count,sizeof(*seen),identity_compare);
        for (size_t i=1;i<count;++i) if (seen[i]==seen[i-1]) { ok=fail(error,QA_ERROR_FORMAT,"Restored scene identities alias"); break; }
    }
    free(seen); return ok;
}
bool qa_scene_world_owner_checkpoint(const qa_scene_world *world, const qa_scene_world_owner_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!world || !out || out->data || out->size || !qa_scene_world_observation_ready(world) || !refs_ready(refs))
        return fail(error,QA_ERROR_ARGUMENT,"World owner capture requires idle owners and empty output");
    qa_scene_world *owner=(qa_scene_world *)world;
    owner->checkpoint_active=true;
    qa_buffer retained={0};
    if (!qaw_world_checkpoint_locked(world,&refs->state,&retained,error)) {
        owner->checkpoint_active=false;
        return false;
    }
    qa_bytes state={retained.data,retained.size}; qa_scene_world saved=*world; qa_source_save_io io={0};
    qaw_model *models=NULL; qaw_surface *surfaces=NULL; bool ok=true;
    if (refs->identity_encode) {
        ok=world->model_count<=SIZE_MAX/sizeof(*models) && world->surface_count<=SIZE_MAX/sizeof(*surfaces);
        if (ok && world->model_count) models=malloc(world->model_count*sizeof(*models));
        if (ok && world->surface_count) surfaces=malloc(world->surface_count*sizeof(*surfaces));
        if (ok && ((world->model_count && !models) || (world->surface_count && !surfaces)))
            ok=fail(error,QA_ERROR_MEMORY,"Retaining readonly world identity capture rows");
        if (ok) {
            if (world->model_count) memcpy(models,world->models,world->model_count*sizeof(*models));
            if (world->surface_count) memcpy(surfaces,world->surfaces,world->surface_count*sizeof(*surfaces));
            saved.models=models; saved.surfaces=surfaces;
            ok=refs->identity_encode(refs->context,QA_SCENE_WORLD_IDENTITY_WORLD,0,world->identity,&saved.identity,error) && saved.identity;
        }
        for (size_t i=0;ok && i<world->model_count;++i)
            ok=refs->identity_encode(refs->context,QA_SCENE_WORLD_IDENTITY_MODEL,i,world->models[i].identity,&models[i].identity,error) && models[i].identity;
        for (size_t i=0;ok && i<world->surface_count;++i) if (world->surfaces[i].mesh.identity)
            ok=refs->identity_encode(refs->context,QA_SCENE_WORLD_IDENTITY_MESH,i,world->surfaces[i].mesh.identity,&surfaces[i].mesh.identity,error) && surfaces[i].mesh.identity;
    }
    ok=ok && qa_source_save_writer(&io,NULL,error) && fields(&io,&saved,refs,NULL,&state) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid complete world owner");
    free(models); free(surfaces); qa_source_save_dispose(&io); qa_buffer_free(&retained); owner->checkpoint_active=false; return ok;
}
bool qa_scene_world_owner_restore(const qa_bsp_view *qualified_source, qa_scene_resources *resources, qa_material_library *materials,
    qa_bytes bytes, const qa_scene_world_owner_refs *refs, qa_scene_world **out, qa_error *error)
{
    if (!qualified_source || (qualified_source->source.size && !qualified_source->source.data) ||
        !resources || !materials || !out || *out || !refs_ready(refs) ||
        qa_material_library_resource_owner(materials)!=resources || !qa_material_library_order_ready(materials))
        return fail(error,QA_ERROR_ARGUMENT,"World owner restore requires qualified source, installed tables and empty output");
    qa_scene_world *world=calloc(1,sizeof(*world));
    if (!world) return fail(error,QA_ERROR_MEMORY,"Allocating detached scene world owner");
    if (!qaw_world_owners_retain(world,resources,materials,error)) { qa_scene_world_destroy(world); return false; }
    qa_bytes state={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,world,refs,qualified_source,&state) && qa_source_save_finish(&io,NULL);
    if (ok) ok=identities(world,refs,error) && qa_scene_world_restore(world,state,&refs->state,error);
    if (ok) *out=world;
    else {
        if (error && error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Complete world owner differs from qualified source or references");
        qa_scene_world_destroy(world);
    }
    qa_source_save_dispose(&io); return ok;
}
bool qa_scene_world_owner_identities_read(const qa_bsp_view *qualified_source, qa_scene_resources *resources, qa_bytes bytes,
    const qa_scene_world_owner_refs *refs, qa_scene_world_saved_identity **out, size_t *out_count, qa_error *error)
{
    if (!qualified_source || (qualified_source->source.size && !qualified_source->source.data) || !resources ||
        !refs_ready(refs) || !out || *out || !out_count || *out_count)
        return fail(error,QA_ERROR_ARGUMENT,"World namespace read requires qualified owners and empty outputs");
    qa_scene_world *world=calloc(1,sizeof(*world));
    if (!world) return fail(error,QA_ERROR_MEMORY,"Allocating private world namespace view");
    world->references=1;
    world->resources=resources; qa_source_save_io io={0}; qa_bytes state={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,world,refs,qualified_source,&state) && qa_source_save_finish(&io,NULL);
    qa_scene_world_saved_identity *identities=NULL; size_t count=0;
    if (ok) {
        if (world->model_count>SIZE_MAX-1 || world->surface_count>SIZE_MAX-1-world->model_count ||
            1+world->model_count+world->surface_count>SIZE_MAX/sizeof(*identities))
            ok=fail(error,QA_ERROR_FORMAT,"Saved world namespace extent overflow");
        else {
            identities=malloc((1+world->model_count+world->surface_count)*sizeof(*identities));
            if (!identities) ok=fail(error,QA_ERROR_MEMORY,"Reading saved scene identity inventory");
        }
    }
    if (ok) {
        identities[count++]=(qa_scene_world_saved_identity){QA_SCENE_WORLD_IDENTITY_WORLD,0,world->identity};
        for (size_t i=0;i<world->model_count;++i)
            identities[count++]=(qa_scene_world_saved_identity){QA_SCENE_WORLD_IDENTITY_MODEL,i,world->models[i].identity};
        for (size_t i=0;i<world->surface_count;++i) if (world->surfaces[i].mesh.identity)
            identities[count++]=(qa_scene_world_saved_identity){QA_SCENE_WORLD_IDENTITY_MESH,i,world->surfaces[i].mesh.identity};
        *out=identities; *out_count=count;
    } else {
        free(identities);
        if (error && error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid saved world identity inventory");
    }
    qa_scene_world_destroy(world); qa_source_save_dispose(&io); return ok;
}
