#include "q3/internal.h"
#include "qa/scene_world_save.h"
#include "qa/scene_save.h"
#include "qa/material_library_save.h"
#include "qa/source_save.h"

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
static bool lightmap_layout(const qa_scene_image *current, const qa_scene_image *saved)
{
    if (!current || !saved || current->kind!=saved->kind || current->wrap!=saved->wrap || current->filter!=saved->filter ||
        current->level_count!=saved->level_count || current->logical_width!=saved->logical_width || current->logical_height!=saved->logical_height ||
        memcmp(&current->border,&saved->border,sizeof(current->border)) || current->animation_count!=saved->animation_count) return false;
    for (size_t i=0;i<current->level_count;++i) {
        const qa_scene_image_level *a=&current->levels[i], *b=&saved->levels[i];
        if (a->width!=b->width || a->height!=b->height || a->bytes!=b->bytes) return false;
    }
    return true;
}
static bool admission_ready(const qa_scene_world *world)
{
    if (world->admission_frame && world->admission_sequence==world->admission_frame->sequence) {
        size_t count=world->admission_frame->command_count;
        if (world->admission_view>count || (world->admission_view &&
            world->admission_frame->commands[world->admission_view-1].kind!=QA_SCENE_COMMAND_VIEW)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, const qa_scene_world *world, qa_scene_world *saved,
    q3_data *q3, const qa_scene_world_checkpoint_refs *refs, qa_bytes *lighting)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ, is_q3=world->bsp.family==QA_BSP_Q3;
    uint8_t magic[4]={'Q','W','S','T'}; if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QWST",4)) return false;
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
    if (is_q3) {
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
    if (!reading && !admission_ready(saved)) return false;
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
        if (!image(io,world,&refs->images,&q3->lightmaps[i]) || !lightmap_layout(current->lightmaps[i],q3->lightmaps[i])) return false;
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
    return (world && !world->restore_pending && !world->transaction_depth && !world->admission_change_count && world->checkpoint_active==leased &&
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
    if (ok) {
        publish(world,&saved,&q3);
        world->restore_pending=world->admission_frame!=NULL;
    }
    else if (error && error->code==QA_OK) failure(error,QA_ERROR_FORMAT,"Saved scene world differs from its qualified geometry or owners");
    discard(world,&saved,&q3); qa_source_save_dispose(&io); world->checkpoint_active=false; return ok;
}
bool qa_scene_world_restore_finish(qa_scene_world *world, qa_error *error)
{
    if (!qa_scene_world_idle(world))
        return failure(error,QA_ERROR_ARGUMENT,"World restore finish requires its uncaptured returned owner");
    if (!admission_ready(world))
        return failure(error,QA_ERROR_FORMAT,"Saved world admission cursor differs from its restored frame");
    world->restore_pending=false;
    return true;
}
#undef FIELD
