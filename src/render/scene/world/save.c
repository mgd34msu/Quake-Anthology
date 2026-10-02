#include "q3/internal.h"
#include "qa/scene_world_save.h"
#include "qa/scene_save.h"
#include "qa/material_library_save.h"
#include "qa/source_save.h"
#include "qa/hash.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io, &(object)->name)) return false; } while (0)
const qa_scene_mesh *qa_scene_world_mesh_at(const qa_scene_world *world, size_t index)
{ return world && index<world->surface_count?&world->surfaces[index].mesh:NULL; }
size_t qa_scene_world_model_count(const qa_scene_world *world) { return world?world->model_count:0; }
uint64_t qa_scene_world_model_identity_at(const qa_scene_world *world, size_t index)
{ return world && index<world->model_count?world->models[index].identity:0; }
qa_scene_resources *qa_scene_world_resource_owner(const qa_scene_world *world) { return world?world->resources:NULL; }
qa_material_library *qa_scene_world_material_owner(const qa_scene_world *world) { return world?world->materials:NULL; }

static bool failure(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static bool plane(qa_source_save_io *io, qa_scene_plane *value)
{ FIELD(vec3,value,normal); FIELD(f32,value,distance); return true; }
static bool bounds(qa_source_save_io *io, qa_bounds *value)
{ FIELD(vec3,value,mins); FIELD(vec3,value,maxs); return true; }
static bool bsp_bounds(qa_source_save_io *io, qa_bsp_bounds *value)
{
    FIELD(f32,&value->min,x); FIELD(f32,&value->min,y); FIELD(f32,&value->min,z);
    FIELD(f32,&value->max,x); FIELD(f32,&value->max,y); FIELD(f32,&value->max,z); return true;
}
static bool range(qa_source_save_io *io, qa_bsp_range *value)
{ FIELD(u32,value,first); FIELD(u32,value,count); return true; }
static bool fog(qa_source_save_io *io, qa_scene_fog *value)
{
    uint32_t kind=value->kind, effect=value->effect;
    if (!qa_source_save_u32(io,&kind) || kind>QA_FOG_Q2 || !qa_source_save_u32(io,&effect) || effect>QA_FOG_NO_EFFECT) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { value->kind=(qa_scene_fog_kind)kind; value->effect=(qa_scene_fog_effect)effect; }
    FIELD(vec3,value,color); FIELD(vec3,value,height_color); FIELD(vec3,value,height_end_color);
    FIELD(f32,value,density); FIELD(f32,value,amount); FIELD(f32,value,sky_factor); FIELD(f32,value,height_density);
    FIELD(f32,value,height_start); FIELD(f32,value,height_end); FIELD(f32,value,height_falloff); FIELD(f32,value,far_depth);
    FIELD(bool,value,sky_drawn); return true;
}
static bool bytes_digest(qa_source_save_io *io, qa_bytes bytes)
{
    size_t count=bytes.size; qa_sha256_digest digest; qa_sha256(bytes,&digest);
    return qa_source_save_count(io,&count,SIZE_MAX) && qa_source_save_bytes(io,digest.bytes,sizeof(digest.bytes));
}
static bool profile(qa_source_save_io *io, const qa_scene_world *world)
{
    qa_scene_world_options options=world->options; qa_scene_image_options image=options.images;
    uint32_t family=world->bsp.family, format=world->bsp.format, encoding=options.q1_lightmap_encoding;
    uint32_t image_family=image.family, wrap=image.wrap, filter=image.filter, usage=image.usage;
    int32_t transparent_index=image.transparent_index;
    bool sky=options.q2_sky!=NULL; size_t length=sky?strlen(options.q2_sky):0;
    if (!qa_source_save_u32(io,&family) || !qa_source_save_u32(io,&format) ||
        !bytes_digest(io,(qa_bytes){world->bytes.data,world->bytes.size}) || !bytes_digest(io,options.external_lit) ||
        !qa_source_save_bool(io,&options.has_external_entities) || !bytes_digest(io,options.external_entities) ||
        !bytes_digest(io,image.palette_rgb) || !bytes_digest(io,image.translation) ||
        !qa_source_save_bool(io,&sky) || !qa_source_save_count(io,&length,SIZE_MAX) ||
        !qa_source_save_bytes(io,(void *)options.q2_sky,length) || !qa_source_save_u32(io,&image_family) ||
        !qa_source_save_u32(io,&wrap) || !qa_source_save_u32(io,&filter) || !qa_source_save_u32(io,&usage) ||
        !qa_source_save_i32(io,&transparent_index)) return false;
    FIELD(bool,&image,mipmap); FIELD(bool,&image,transparent); FIELD(bool,&image,fullbright_only);
    FIELD(f32,&options,subdivisions); FIELD(f32,&options,q1_water_alpha); FIELD(f32,&options,q2_light_modulate);
    FIELD(u32,&options,q3_overbright); return qa_source_save_u32(io,&encoding);
}
static bool vertex(qa_source_save_io *io, qa_scene_vertex *value)
{
    FIELD(vec3,value,position); FIELD(vec3,value,normal);
    FIELD(f32,&value->texcoord,x); FIELD(f32,&value->texcoord,y); FIELD(f32,&value->lightmap,x); FIELD(f32,&value->lightmap,y);
    FIELD(f32,&value->color,x); FIELD(f32,&value->color,y); FIELD(f32,&value->color,z); FIELD(f32,&value->color,w); return true;
}
static bool geometry(qa_source_save_io *io, const qa_scene_world *world)
{
    if (!profile(io,world)) return false;
    size_t counts[]={world->plane_count,world->node_count,world->leaf_count,world->leaf_surface_count,
        world->model_count,world->surface_count,world->pvs_capacity,world->pending_capacity};
    for (size_t i=0;i<sizeof(counts)/sizeof(counts[0]);++i) if (!qa_source_save_count(io,&counts[i],SIZE_MAX)) return false;
    qa_bounds extent=world->bounds; qa_vec3 grid=world->grid_size; uint32_t clusters=world->cluster_count;
    if (!bounds(io,&extent) || !qa_source_save_vec3(io,&grid) || !qa_source_save_u32(io,&clusters)) return false;
    for (size_t i=0;i<world->plane_count;++i) {
        qa_bsp_plane value=world->planes[i];
        FIELD(f32,&value.normal,x); FIELD(f32,&value.normal,y); FIELD(f32,&value.normal,z);
        FIELD(f32,&value,distance); FIELD(i32,&value,type);
    }
    for (size_t i=0;i<world->node_count;++i) {
        qa_bsp_node value=world->nodes[i]; FIELD(u32,&value,plane);
        for (size_t j=0;j<2;++j) if (!qa_source_save_i32(io,&value.children[j])) return false;
        if (!bsp_bounds(io,&value.bounds) || !range(io,&value.faces)) return false;
    }
    for (size_t i=0;i<world->leaf_count;++i) {
        qa_bsp_leaf value=world->leaves[i]; FIELD(i32,&value,contents); FIELD(i64,&value,cluster); FIELD(i64,&value,area);
        FIELD(i32,&value,visibility_offset);
        if (!bsp_bounds(io,&value.bounds) || !range(io,&value.faces) || !range(io,&value.brushes) ||
            !qa_source_save_bytes(io,value.ambient,sizeof(value.ambient))) return false;
    }
    for (size_t i=0;i<world->leaf_surface_count;++i) {
        uint32_t value=world->leaf_surfaces[i]; if (!qa_source_save_u32(io,&value)) return false;
    }
    for (size_t i=0;i<world->model_count;++i) {
        qaw_model model=world->models[i]; qa_bsp_model value=model.source;
        if (!bsp_bounds(io,&value.bounds)) return false;
        FIELD(f32,&value.origin,x); FIELD(f32,&value.origin,y); FIELD(f32,&value.origin,z);
        for (size_t j=0;j<4;++j) if (!qa_source_save_i32(io,&value.headnodes[j])) return false;
        FIELD(i32,&value,visible_leaves); FIELD(bool,&value,membership_from_tree);
        size_t expected_capacity=value.membership_from_tree?world->surface_count:value.faces.count;
        if (model.surface_capacity!=expected_capacity || model.surface_count>model.surface_capacity ||
            (model.surface_capacity && !model.surfaces))
            return failure(io->error,QA_ERROR_ARGUMENT,"World model surface allocation changed");
        if (!range(io,&value.faces) || !range(io,&value.brushes) ||
            !qa_source_save_count(io,&model.surface_capacity,SIZE_MAX) ||
            !qa_source_save_count(io,&model.surface_count,model.surface_capacity)) return false;
        for (size_t j=0;j<model.surface_count;++j) {
            uint32_t value_index=model.surfaces[j]; if (!qa_source_save_u32(io,&value_index)) return false;
        }
    }
    for (size_t i=0;i<world->surface_count;++i) {
        qaw_surface surface=world->surfaces[i]; qa_scene_mesh mesh=surface.mesh;
        uint32_t type=surface.type, primitive=mesh.primitive;
        FIELD(u32,&surface,source_index); FIELD(u32,&surface,fog_index); FIELD(bool,&surface,has_plane);
        if (!plane(io,&surface.plane) || !qa_source_save_u32(io,&type) || !qa_source_save_u32(io,&primitive) ||
            !qa_source_save_u64(io,&mesh.revision) || !bounds(io,&mesh.bounds) ||
            !qa_source_save_count(io,&mesh.vertex_count,SIZE_MAX) || !qa_source_save_count(io,&mesh.index_count,SIZE_MAX)) return false;
        for (size_t j=0;j<mesh.vertex_count;++j) {
            qa_scene_vertex value=mesh.vertices[j]; if (!vertex(io,&value)) return false;
        }
        for (size_t j=0;j<mesh.index_count;++j) {
            uint32_t value=mesh.indices[j]; if (!qa_source_save_u32(io,&value)) return false;
        }
        bool patched=surface.patch!=NULL;
        if (!qa_source_save_bool(io,&patched)) return false;
        if (patched) {
            qaw_patch patch=*surface.patch; uint32_t width=patch.width, height=patch.height;
            if (!qa_source_save_u32(io,&width) || !qa_source_save_u32(io,&height)) return false;
            for (size_t j=0;j<QAW_PATCH_LIMIT;++j)
                if (!qa_source_save_f32(io,&patch.width_error[j]) || !qa_source_save_f32(io,&patch.height_error[j])) return false;
            FIELD(vec3,&patch,lod_origin); FIELD(f32,&patch,lod_radius); FIELD(bool,&patch,stitched); FIELD(bool,&patch,fixed);
        }
    }
    bool q3=world->bsp.family==QA_BSP_Q3;
    if (!qa_source_save_bool(io,&q3)) return false;
    if (q3) {
        q3_data *data=world->q3_data; size_t lights=data->lightmap_count, fogs=data->fog_count, grid_count=data->grid_count;
        if (!qa_source_save_count(io,&lights,SIZE_MAX) || !qa_source_save_count(io,&fogs,SIZE_MAX) ||
            !qa_source_save_count(io,&grid_count,SIZE_MAX)) return false;
        for (size_t j=0;j<3;++j) { size_t bound=data->grid_bounds[j]; if (!qa_source_save_count(io,&bound,SIZE_MAX)) return false; }
        qa_vec3 origin=data->grid_origin, inverse=data->grid_inverse;
        if (!qa_source_save_vec3(io,&origin) || !qa_source_save_vec3(io,&inverse)) return false;
        for (size_t j=0;j<grid_count;++j) {
            qa_bsp_grid_point value=data->grid[j];
            if (!qa_source_save_bytes(io,value.ambient,3) || !qa_source_save_bytes(io,value.directed,3) || !qa_source_save_bytes(io,value.lat_long,2)) return false;
        }
        for (size_t j=0;j<fogs;++j) {
            q3_fog value=data->fogs[j];
            if (!fog(io,&value.fog) || !bounds(io,&value.bounds) || !plane(io,&value.surface)) return false;
            FIELD(f32,&value,tc_scale); FIELD(bool,&value,active); FIELD(bool,&value,has_surface);
        }
    }
    return true;
}
static bool qualify(qa_source_save_io *io, const qa_scene_world *world)
{
    qa_source_save_io description; qa_buffer bytes={0};
    if (!qa_source_save_writer(&description,NULL,io->error)) return false;
    bool ok=geometry(&description,world) && qa_source_save_finish(&description,&bytes);
    qa_source_save_dispose(&description);
    qa_sha256_digest actual={0}, saved;
    if (ok) qa_sha256((qa_bytes){bytes.data,bytes.size},&actual);
    qa_buffer_free(&bytes); saved=actual;
    return ok && qa_source_save_bytes(io,saved.bytes,sizeof(saved.bytes)) && !memcmp(actual.bytes,saved.bytes,sizeof(saved.bytes));
}
static bool material(qa_source_save_io *io, const qa_scene_world *world, const qa_scene_world_checkpoint_refs *refs, const qa_material **value)
{
    bool present=*value!=NULL; uint64_t key=0;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction==QA_SOURCE_SAVE_READ) *value=NULL; return true; }
    if (io->direction==QA_SOURCE_SAVE_WRITE && !refs->material_encode(refs->context,*value,&key,io->error)) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && (!refs->material_decode(refs->context,key,value,io->error) || !*value)) return false;
    qa_material_library_record_view record;
    return qa_material_library_record_read(world->materials,(*value)->sorted_index,&record) && record.material==*value &&
        (*value)->order_entry && (!record.world_identity || record.world_identity==world->identity);
}
static bool image(qa_source_save_io *io, const qa_scene_world *world, const qa_scene_world_image_refs *refs, qa_scene_image **value)
{
    bool present=*value!=NULL; uint64_t key=0; const qa_scene_resources *owners[]={world->resources}; size_t index;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction==QA_SOURCE_SAVE_READ) *value=NULL; return true; }
    if (io->direction==QA_SOURCE_SAVE_WRITE && (!qa_scene_image_owner_index(owners,1,*value,&index) || !refs->encode(refs->context,*value,&key,io->error))) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded=NULL;
        if (!refs->decode(refs->context,key,&decoded,io->error) || !decoded || !qa_scene_image_owner_index(owners,1,decoded,&index)) return false;
        qa_scene_image_retain(decoded); *value=(qa_scene_image *)decoded;
    }
    return true;
}
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t stride)
{
    if (count>SIZE_MAX/stride) return false;
    *out=count?calloc(count,stride):NULL;
    return !count || *out || failure(io->error,QA_ERROR_MEMORY,"Allocating saved scene world state");
}
static bool frame(qa_source_save_io *io, const qa_scene_world_checkpoint_refs *refs, qa_scene_world *saved)
{
    bool present=saved->admission_frame!=NULL; uint64_t key=0;
    if (!qa_source_save_bool(io,&present)) return false;
    if (present) {
        if (io->direction==QA_SOURCE_SAVE_WRITE && !refs->frame_encode(refs->context,saved->admission_frame,&key,io->error)) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ && (!refs->frame_decode(refs->context,key,&saved->admission_frame,io->error) || !saved->admission_frame)) return false;
    } else if (io->direction==QA_SOURCE_SAVE_READ) saved->admission_frame=NULL;
    return true;
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count=bytes->size;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if (count>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool visible_list(qa_source_save_io *io, qa_scene_world *saved)
{
    if (saved->visible_count && !saved->visibility_generation) return false;
    uint8_t *seen=saved->surface_count?calloc(saved->surface_count,1):NULL;
    if (saved->surface_count && !seen) return failure(io->error,QA_ERROR_MEMORY,"Allocating visibility qualification marks");
    bool ok=true;
    for (size_t i=0;ok && i<saved->visible_count;++i) {
        uint32_t *surface=&saved->visible_surfaces[i];
        ok=qa_source_save_u32(io,surface) && *surface<saved->surface_count && !seen[*surface] &&
            saved->surface_marks[*surface]==saved->visibility_generation;
        if (ok) seen[*surface]=1;
    }
    free(seen); return ok;
}
static bool same_lightmap(const qa_scene_image *current, const qa_scene_image *saved)
{
    if (!current || !saved || current->kind!=saved->kind || current->wrap!=saved->wrap || current->filter!=saved->filter ||
        current->level_count!=saved->level_count || current->logical_width!=saved->logical_width || current->logical_height!=saved->logical_height ||
        memcmp(&current->border,&saved->border,sizeof(current->border)) || current->animation_count!=saved->animation_count) return false;
    for (size_t i=0;i<current->level_count;++i) {
        const qa_scene_image_level *a=&current->levels[i], *b=&saved->levels[i];
        if (a->width!=b->width || a->height!=b->height || a->bytes!=b->bytes || memcmp(a->pixels,b->pixels,a->bytes)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, const qa_scene_world *world, qa_scene_world *saved,
    q3_data *q3, const qa_scene_world_checkpoint_refs *refs, qa_bytes *lighting)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ, is_q3=world->bsp.family==QA_BSP_Q3;
    uint8_t magic[4]={'Q','W','S','T'}; uint32_t schema=4;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QWST",4) ||
        !qa_source_save_u32(io,&schema) || schema!=4 || !qualify(io,world)) return false;
    if (reading) {
        if (!allocate(io,(void **)&saved->surfaces,world->surface_count,sizeof(*saved->surfaces)) ||
            !allocate(io,(void **)&saved->surface_marks,world->surface_count,sizeof(*saved->surface_marks)) ||
            !allocate(io,(void **)&saved->surface_lights,world->surface_count,sizeof(*saved->surface_lights)) ||
            !allocate(io,(void **)&saved->admitted_surfaces,world->surface_count,sizeof(*saved->admitted_surfaces)) ||
            !allocate(io,(void **)&saved->visible_surfaces,world->surface_count,sizeof(*saved->visible_surfaces)) ||
            !allocate(io,(void **)&saved->pvs,world->pvs_capacity,1) || !allocate(io,(void **)&saved->secondary_pvs,world->pvs_capacity,1) ||
            (is_q3 && (!allocate(io,(void **)&saved->source_leaf_marks,world->leaf_count,sizeof(*saved->source_leaf_marks)) ||
                !allocate(io,(void **)&saved->source_dlight_masks,world->surface_count,sizeof(*saved->source_dlight_masks))))) return false;
        for (size_t i=0;i<world->surface_count;++i) {
            saved->surfaces[i]=world->surfaces[i]; if (is_q3) saved->surfaces[i].lightmap=NULL;
        }
    }
    FIELD(u64,saved,revision); FIELD(u64,saved,admission_generation); FIELD(u64,saved,admission_sequence);
    if (!saved->revision || !qa_source_save_count(io,&saved->admission_view,SIZE_MAX) || !frame(io,refs,saved)) return false;
    if (saved->admission_frame && !saved->admission_generation) return false;
    FIELD(u32,saved,visibility_generation); FIELD(bool,saved,sky_drawn); FIELD(bool,saved,pvs_cached); FIELD(bool,saved,pvs_all);
    FIELD(i32,saved,pvs_selector); FIELD(i32,saved,pvs_secondary);
    if (is_q3 && schema>=3) {
        FIELD(u32,saved,source_vis_generation); FIELD(i32,saved,source_view_cluster);
        FIELD(bool,saved,source_area_mask_modified);
        if ((saved->source_view_cluster < -1 || (saved->source_view_cluster > 0 &&
            (uint32_t)saved->source_view_cluster >= world->cluster_count)) ||
            !qa_source_save_bytes(io,saved->source_area_mask,sizeof(saved->source_area_mask))) return false;
        for (size_t i=0;i<world->leaf_count;++i)
            if (!qa_source_save_u32(io,&saved->source_leaf_marks[i]) ||
                saved->source_leaf_marks[i]>saved->source_vis_generation) return false;
        for (size_t i=0;i<world->surface_count;++i)
            if (!qa_source_save_u32(io,&saved->source_dlight_masks[i])) return false;
    } else if (reading) {
        saved->source_vis_generation=0; saved->source_view_cluster=0;
        saved->source_area_mask_modified=false;
        memset(saved->source_area_mask,0,sizeof(saved->source_area_mask));
    }
    if (!qa_source_save_count(io,&saved->pvs_size,world->pvs_capacity) || !qa_source_save_count(io,&saved->visible_count,world->surface_count) ||
        !qa_source_save_bytes(io,saved->pvs,world->pvs_capacity) || !qa_source_save_bytes(io,saved->secondary_pvs,world->pvs_capacity)) return false;
    if (saved->admission_frame && saved->admission_sequence==saved->admission_frame->sequence) {
        size_t count=saved->admission_frame->command_count;
        if (saved->admission_view>count || (saved->admission_view &&
            saved->admission_frame->commands[saved->admission_view-1].kind!=QA_SCENE_COMMAND_VIEW)) return false;
    }
    for (size_t i=0;i<world->surface_count;++i) {
        if (!qa_source_save_u32(io,&saved->surface_marks[i]) || saved->surface_marks[i]>saved->visibility_generation ||
            !qa_source_save_u32(io,&saved->surface_lights[i]) || !qa_source_save_u64(io,&saved->admitted_surfaces[i]) ||
            saved->admitted_surfaces[i]>saved->admission_generation) return false;
        qaw_surface *surface=&saved->surfaces[i];
        if (!material(io,world,refs,&surface->material) || !material(io,world,refs,&surface->base_material)) return false;
        FIELD(f32,surface,material_time_offset); FIELD(f32,surface,sort); FIELD(bool,surface,skip); FIELD(bool,surface,sky); FIELD(bool,surface,flare);
        if (!isfinite(surface->material_time_offset) || !isfinite(surface->sort) || !fog(io,&surface->fog) ||
            (is_q3 && !image(io,world,&refs->images,&surface->lightmap))) return false;
    }
    if (!visible_list(io,saved)) return false;
    if (!is_q3) return blob(io,lighting);
    const q3_data *current=world->q3_data;
    if (reading && !allocate(io,(void **)&q3->lightmaps,current->lightmap_count,sizeof(*q3->lightmaps))) return false;
    for (size_t i=0;i<current->lightmap_count;++i) {
        if (!image(io,world,&refs->images,&q3->lightmaps[i]) || !same_lightmap(current->lightmaps[i],q3->lightmaps[i])) return false;
        for (size_t j=0;j<i;++j) if (q3->lightmaps[j]==q3->lightmaps[i]) return false;
    }
    for (size_t i=0;i<world->surface_count;++i) {
        const qa_scene_image *original=world->surfaces[i].lightmap, *decoded=saved->surfaces[i].lightmap;
        if (!original) { if (decoded) return false; continue; }
        size_t index=0;
        while (index<current->lightmap_count && current->lightmaps[index]!=original) ++index;
        if (index==current->lightmap_count || decoded!=q3->lightmaps[index]) return false;
    }
    return true;
}
static bool ready(const qa_scene_world *world, const qa_scene_world_checkpoint_refs *refs, bool leased, qa_error *error)
{
    return (world && !world->transaction_depth && !world->admission_change_count && world->checkpoint_active==leased &&
        refs && refs->images.encode && refs->images.decode &&
        refs->material_encode && refs->material_decode && refs->frame_encode && refs->frame_decode &&
        world->resources==qa_material_library_resource_owner(world->materials) && qa_material_library_order_ready(world->materials) &&
        (world->bsp.family==QA_BSP_Q3?world->q3_data!=NULL:world->legacy_data!=NULL)) ||
        failure(error,QA_ERROR_ARGUMENT,"World continuation requires idle installed geometry, restored resource/material owners and resolvers");
}
static void discard(const qa_scene_world *world, qa_scene_world *saved, q3_data *q3)
{
    if (world->bsp.family==QA_BSP_Q3) {
        if (saved->surfaces) for (size_t i=0;i<world->surface_count;++i) qa_scene_image_release(saved->surfaces[i].lightmap);
        if (q3->lightmaps) for (size_t i=0;i<((q3_data *)world->q3_data)->lightmap_count;++i) qa_scene_image_release(q3->lightmaps[i]);
        free(q3->lightmaps);
    }
    free(saved->surfaces); free(saved->surface_marks); free(saved->surface_lights); free(saved->admitted_surfaces);
    free(saved->visible_surfaces); free(saved->pvs); free(saved->secondary_pvs);
    free(saved->source_leaf_marks);
    free(saved->source_dlight_masks);
}
static void publish(qa_scene_world *world, qa_scene_world *saved, q3_data *q3)
{
    world->revision=saved->revision; world->admission_generation=saved->admission_generation;
    world->admission_frame=saved->admission_frame; world->admission_sequence=saved->admission_sequence; world->admission_view=saved->admission_view;
    world->visibility_generation=saved->visibility_generation; world->visible_count=saved->visible_count; world->sky_drawn=saved->sky_drawn;
    world->pvs_size=saved->pvs_size; world->pvs_selector=saved->pvs_selector; world->pvs_secondary=saved->pvs_secondary;
    world->pvs_cached=saved->pvs_cached; world->pvs_all=saved->pvs_all;
    world->source_vis_generation=saved->source_vis_generation; world->source_view_cluster=saved->source_view_cluster;
    world->source_area_mask_modified=saved->source_area_mask_modified;
    memcpy(world->source_area_mask,saved->source_area_mask,sizeof(world->source_area_mask));
    if (world->bsp.family==QA_BSP_Q3 && world->leaf_count)
        memcpy(world->source_leaf_marks,saved->source_leaf_marks,world->leaf_count*sizeof(*world->source_leaf_marks));
    if (world->bsp.family==QA_BSP_Q3 && world->surface_count)
        memcpy(world->source_dlight_masks,saved->source_dlight_masks,world->surface_count*sizeof(*world->source_dlight_masks));
    size_t count=world->surface_count, capacity=world->pvs_capacity;
    if (count) {
        memcpy(world->surface_marks,saved->surface_marks,count*sizeof(*world->surface_marks));
        memcpy(world->surface_lights,saved->surface_lights,count*sizeof(*world->surface_lights));
        memcpy(world->admitted_surfaces,saved->admitted_surfaces,count*sizeof(*world->admitted_surfaces));
    }
    if (saved->visible_count) memcpy(world->visible_surfaces,saved->visible_surfaces,saved->visible_count*sizeof(*world->visible_surfaces));
    if (capacity) { memcpy(world->pvs,saved->pvs,capacity); memcpy(world->secondary_pvs,saved->secondary_pvs,capacity); }
    for (size_t i=0;i<count;++i) {
        qaw_surface *destination=&world->surfaces[i], *source=&saved->surfaces[i];
        destination->material=source->material; destination->base_material=source->base_material;
        destination->material_time_offset=source->material_time_offset; destination->sort=source->sort;
        destination->skip=source->skip; destination->sky=source->sky; destination->flare=source->flare; destination->fog=source->fog;
        if (world->bsp.family==QA_BSP_Q3) {
            qa_scene_image_release(destination->lightmap); destination->lightmap=source->lightmap; source->lightmap=NULL;
        }
    }
    if (world->bsp.family==QA_BSP_Q3) {
        q3_data *destination=world->q3_data;
        for (size_t i=0;i<destination->lightmap_count;++i) qa_scene_image_release(destination->lightmaps[i]);
        free(destination->lightmaps); destination->lightmaps=q3->lightmaps; q3->lightmaps=NULL;
    }
    /* Traversal stack and inactive admission journal entries are operation
     * scratch. No active transaction crosses this owner boundary. */
}
bool qaw_world_checkpoint_locked(const qa_scene_world *world, const qa_scene_world_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || !ready(world,refs,true,error)) return false;
    qa_buffer lighting={0};
    if (world->bsp.family!=QA_BSP_Q3 && !qaw_lighting_checkpoint_locked(world,&refs->images,&lighting,error)) return false;
    qa_bytes lighting_bytes={lighting.data,lighting.size}; qa_scene_world saved=*world;
    q3_data q3=world->bsp.family==QA_BSP_Q3?*(q3_data *)world->q3_data:(q3_data){0};
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) { qa_buffer_free(&lighting); return false; }
    bool ok=fields(&io,world,&saved,&q3,refs,&lighting_bytes) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Invalid retained scene world state");
    qa_source_save_dispose(&io); qa_buffer_free(&lighting); return ok;
}
bool qa_scene_world_checkpoint(const qa_scene_world *world, const qa_scene_world_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || !ready(world,refs,false,error)) return false;
    qa_scene_world *owner=(qa_scene_world *)world;
    owner->checkpoint_active=true;
    bool ok=qaw_world_checkpoint_locked(world,refs,out,error);
    owner->checkpoint_active=false;
    return ok;
}
bool qa_scene_world_restore(qa_scene_world *world, qa_bytes bytes, const qa_scene_world_checkpoint_refs *refs, qa_error *error)
{
    if (!qa_scene_world_idle(world) || !ready(world,refs,false,error))
        return failure(error,QA_ERROR_ARGUMENT,"World restore requires its uncaptured idle candidate owner");
    qa_scene_world saved=*world; q3_data q3={0}; qa_bytes lighting={0};
    saved.surfaces=NULL; saved.surface_marks=saved.surface_lights=saved.visible_surfaces=NULL;
    saved.admitted_surfaces=NULL; saved.pvs=saved.secondary_pvs=NULL;
    saved.source_leaf_marks=NULL;
    saved.source_dlight_masks=NULL;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    world->checkpoint_active=true;
    bool ok=fields(&io,world,&saved,&q3,refs,&lighting) && qa_source_save_finish(&io,NULL);
    if (ok && world->bsp.family!=QA_BSP_Q3) ok=qaw_lighting_restore_locked(world,lighting,&refs->images,error);
    if (ok) publish(world,&saved,&q3);
    else if (error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Saved scene world differs from its qualified geometry or owners");
    discard(world,&saved,&q3); qa_source_save_dispose(&io); world->checkpoint_active=false; return ok;
}
#undef FIELD
