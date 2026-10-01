#include "owner_private.h"
#include "q3/internal.h"
#include "qa/scene_save.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

#define F(type,object,member) do { if (!qa_source_save_##type(io,&(object)->member)) return false; } while (0)
static bool fail(qa_source_save_io *io, qa_status code, const char *message)
{ qa_error_set(io->error,code,0,"%s",message); return false; }
static bool array(qa_source_save_io *io, void **out, size_t count, size_t stride)
{
    if (io->direction!=QA_SOURCE_SAVE_READ) return !count || *out || fail(io,QA_ERROR_ARGUMENT,"Missing world allocation");
    if (count>SIZE_MAX/stride) return fail(io,QA_ERROR_FORMAT,"World allocation exceeds address space");
    *out=count?calloc(count,stride):NULL;
    return !count || *out || fail(io,QA_ERROR_MEMORY,"Allocating detached world core");
}
static bool count(qa_source_save_io *io, size_t *value, size_t expected)
{ return qa_source_save_count(io,value,expected) && (*value==expected || fail(io,QA_ERROR_FORMAT,"World source extent changed")); }
static bool range(qa_source_save_io *io, qa_bsp_range *value)
{ F(u32,value,first); F(u32,value,count); return true; }
static bool bounds(qa_source_save_io *io, qa_bounds *value)
{ F(vec3,value,mins); F(vec3,value,maxs); return true; }
static bool source_bounds(qa_source_save_io *io, qa_bsp_bounds *value)
{ F(vec3,value,min); F(vec3,value,max); return true; }
static bool plane(qa_source_save_io *io, qa_scene_plane *value)
{ F(vec3,value,normal); F(f32,value,distance); return true; }
static bool fog(qa_source_save_io *io, qa_scene_fog *value)
{
    uint32_t kind=value->kind, effect=value->effect;
    if (!qa_source_save_u32(io,&kind) || kind>QA_FOG_Q2 || !qa_source_save_u32(io,&effect) || effect>QA_FOG_NO_EFFECT)
        return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { value->kind=(qa_scene_fog_kind)kind; value->effect=(qa_scene_fog_effect)effect; }
    F(vec3,value,color); F(vec3,value,height_color); F(vec3,value,height_end_color);
    F(f32,value,density); F(f32,value,amount); F(f32,value,sky_factor); F(f32,value,height_density);
    F(f32,value,height_start); F(f32,value,height_end); F(f32,value,height_falloff); F(f32,value,far_depth);
    F(bool,value,sky_drawn); return true;
}
static bool source_equal(const void *a, const void *b, size_t bytes)
{ return !memcmp(a,b,bytes); }
#define SAME(a,b,member) source_equal(&(a)->member,&(b)->member,sizeof((a)->member))
static bool source_topology(qa_source_save_io *io, const qa_scene_world *world)
{
    for (size_t i=0;i<world->plane_count;++i) {
        qa_bsp_plane source; const qa_bsp_plane *value=&world->planes[i];
        if (!qa_bsp_read_plane(&world->bsp,i,&source,io->error) || !SAME(value,&source,normal) ||
            !SAME(value,&source,distance) || value->type!=source.type) return false;
    }
    for (size_t i=0;i<world->node_count;++i) {
        qa_bsp_node source; const qa_bsp_node *value=&world->nodes[i];
        if (!qa_bsp_read_node(&world->bsp,i,&source,io->error) || value->plane!=source.plane ||
            !SAME(value,&source,children) || !SAME(value,&source,bounds) ||
            value->faces.first!=source.faces.first || value->faces.count!=source.faces.count) return false;
    }
    uint32_t clusters=0;
    for (size_t i=0;i<world->leaf_count;++i) {
        qa_bsp_leaf source; const qa_bsp_leaf *value=&world->leaves[i];
        if (!qa_bsp_read_leaf(&world->bsp,i,&source,io->error) || value->contents!=source.contents ||
            value->cluster!=source.cluster || value->area!=source.area || value->visibility_offset!=source.visibility_offset ||
            !SAME(value,&source,bounds) || !SAME(value,&source,ambient) ||
            value->faces.first!=source.faces.first || value->faces.count!=source.faces.count ||
            value->brushes.first!=source.brushes.first || value->brushes.count!=source.brushes.count || source.cluster>INT32_MAX)
            return false;
        if (source.cluster>=0 && (uint64_t)source.cluster>=clusters) clusters=(uint32_t)source.cluster+1;
    }
    if (clusters!=world->cluster_count) return false;
    for (size_t i=0;i<world->leaf_surface_count;++i) {
        int64_t index;
        if (!qa_bsp_read_index(&world->bsp,QA_BSP_LEAF_FACES,i,&index,io->error) || index<0 ||
            (uint64_t)index!=world->leaf_surfaces[i]) return false;
    }
    for (size_t i=0;i<world->model_count;++i) {
        qa_bsp_model source; const qaw_model *model=&world->models[i]; const qa_bsp_model *value=&model->source;
        if (!qa_bsp_read_model(&world->bsp,i,&source,io->error) || !SAME(value,&source,bounds) ||
            !SAME(value,&source,origin) || !SAME(value,&source,headnodes) || value->visible_leaves!=source.visible_leaves ||
            value->membership_from_tree!=source.membership_from_tree || value->faces.first!=source.faces.first ||
            value->faces.count!=source.faces.count || value->brushes.first!=source.brushes.first ||
            value->brushes.count!=source.brushes.count) return false;
        if (world->bsp.family!=QA_BSP_Q3) {
            if (model->surface_count!=model->surface_capacity) return false;
            for (size_t j=0;j<model->surface_count;++j) if (model->surfaces[j]!=value->faces.first+j) return false;
        } else {
            uint32_t *members=model->surface_capacity?calloc(model->surface_capacity,sizeof(*members)):NULL;
            if (model->surface_capacity && !members) return fail(io,QA_ERROR_MEMORY,"Qualifying immutable BSP model members");
            size_t used=0;
            bool ok=qa_bsp_model_members(&world->bsp,i,false,members,model->surface_capacity,&used,io->error) &&
                used==model->surface_count && (!used || !memcmp(members,model->surfaces,used*sizeof(*members)));
            free(members);
            if (!ok) return false;
        }
    }
    qa_bounds bounds=world->model_count?(qa_bounds){world->models[0].source.bounds.min,world->models[0].source.bounds.max}:(qa_bounds){0};
    return SAME(&world->bounds,&bounds,mins) && SAME(&world->bounds,&bounds,maxs);
}
static bool topology(qa_source_save_io *io, qa_scene_world *world)
{
    bool q3=world->bsp.family==QA_BSP_Q3;
    if (!count(io,&world->plane_count,qa_bsp_record_count(&world->bsp,QA_BSP_PLANES)) ||
        !count(io,&world->node_count,qa_bsp_record_count(&world->bsp,QA_BSP_NODES)) ||
        !count(io,&world->leaf_count,qa_bsp_record_count(&world->bsp,QA_BSP_LEAVES)) ||
        !count(io,&world->leaf_surface_count,qa_bsp_record_count(&world->bsp,QA_BSP_LEAF_FACES)) ||
        !count(io,&world->model_count,qa_bsp_record_count(&world->bsp,QA_BSP_MODELS)) ||
        !count(io,&world->surface_count,qa_bsp_record_count(&world->bsp,q3?QA_BSP_SURFACES:QA_BSP_FACES)) ||
        world->node_count>INT32_MAX || world->leaf_count>INT32_MAX || world->surface_count>=UINT32_MAX)
        return false;
    if (!array(io,(void **)&world->planes,world->plane_count,sizeof(*world->planes)) ||
        !array(io,(void **)&world->nodes,world->node_count,sizeof(*world->nodes)) ||
        !array(io,(void **)&world->leaves,world->leaf_count,sizeof(*world->leaves)) ||
        !array(io,(void **)&world->leaf_surfaces,world->leaf_surface_count,sizeof(*world->leaf_surfaces)) ||
        !array(io,(void **)&world->models,world->model_count,sizeof(*world->models)) ||
        !array(io,(void **)&world->surfaces,world->surface_count,sizeof(*world->surfaces))) return false;
    for (size_t i=0;i<world->plane_count;++i) {
        qa_bsp_plane *v=&world->planes[i]; F(vec3,v,normal); F(f32,v,distance); F(i32,v,type);
    }
    for (size_t i=0;i<world->node_count;++i) {
        qa_bsp_node *v=&world->nodes[i]; F(u32,v,plane);
        for (size_t j=0;j<2;++j) if (!qa_source_save_i32(io,&v->children[j])) return false;
        if (!source_bounds(io,&v->bounds) || !range(io,&v->faces)) return false;
    }
    for (size_t i=0;i<world->leaf_count;++i) {
        qa_bsp_leaf *v=&world->leaves[i]; F(i32,v,contents); F(i64,v,cluster); F(i64,v,area); F(i32,v,visibility_offset);
        if (!source_bounds(io,&v->bounds) || !range(io,&v->faces) || !range(io,&v->brushes) ||
            !qa_source_save_bytes(io,v->ambient,sizeof(v->ambient))) return false;
    }
    for (size_t i=0;i<world->leaf_surface_count;++i)
        if (!qa_source_save_u32(io,&world->leaf_surfaces[i]) || world->leaf_surfaces[i]>=world->surface_count) return false;
    for (size_t i=0;i<world->model_count;++i) {
        qaw_model *model=&world->models[i]; qa_bsp_model *v=&model->source;
        F(u64,model,identity);
        if (!model->identity || !source_bounds(io,&v->bounds) || !qa_source_save_vec3(io,&v->origin)) return false;
        for (size_t j=0;j<4;++j) if (!qa_source_save_i32(io,&v->headnodes[j])) return false;
        F(i32,v,visible_leaves); F(bool,v,membership_from_tree);
        if (!range(io,&v->faces) || !range(io,&v->brushes) ||
            !count(io,&model->surface_capacity,v->membership_from_tree?world->surface_count:v->faces.count) ||
            !qa_source_save_count(io,&model->surface_count,model->surface_capacity) ||
            !array(io,(void **)&model->surfaces,model->surface_capacity,sizeof(*model->surfaces))) return false;
        for (size_t j=0;j<model->surface_count;++j)
            if (!qa_source_save_u32(io,&model->surfaces[j]) || model->surfaces[j]>=world->surface_count) return false;
    }
    if (!bounds(io,&world->bounds) || !qa_source_save_vec3(io,&world->grid_size)) return false;
    F(u32,world,cluster_count);
    size_t pvs=world->bsp.family==QA_BSP_Q1?(world->leaf_count+7)/8:((size_t)world->cluster_count+7)/8;
    qa_bytes vis=world->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    if (vis.size && world->bsp.family!=QA_BSP_Q1) {
        size_t row=q3?qa_load_u32le(vis.data+4):((size_t)qa_load_u32le(vis.data)+7)/8;
        if (row>pvs) pvs=row;
    }
    if (!count(io,&world->pvs_capacity,pvs) || !count(io,&world->pending_capacity,world->node_count+1) ||
        !count(io,&world->admission_change_capacity,world->surface_count)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ && world->pvs_capacity>io->input.size)
        return fail(io,QA_ERROR_FORMAT,"World PVS allocation lacks saved byte extent");
    return source_topology(io,world) && array(io,(void **)&world->surface_marks,world->surface_count,sizeof(*world->surface_marks)) &&
        array(io,(void **)&world->visible_surfaces,world->surface_count,sizeof(*world->visible_surfaces)) &&
        array(io,(void **)&world->surface_lights,world->surface_count,sizeof(*world->surface_lights)) &&
        array(io,(void **)&world->admitted_surfaces,world->surface_count,sizeof(*world->admitted_surfaces)) &&
        array(io,(void **)&world->admission_changes,world->admission_change_capacity,sizeof(*world->admission_changes)) &&
        array(io,(void **)&world->pending,world->pending_capacity,sizeof(*world->pending)) &&
        array(io,(void **)&world->pvs,world->pvs_capacity,1) && array(io,(void **)&world->secondary_pvs,world->pvs_capacity,1);
}
static bool mesh(qa_source_save_io *io, qaw_surface *surface, const qaw_owner_refs *refs)
{
    qa_scene_mesh *value=&surface->mesh; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool present=value->geometry!=NULL; uint64_t key=0; uint32_t primitive=value->primitive;
    if (!qa_source_save_bool(io,&present)) return false;
    if (present) {
        if (!reading && !refs->geometry_encode(refs->context,value->geometry,&key,io->error)) return false;
        if (!qa_source_save_u64(io,&key)) return false;
        if (reading) {
            const qa_scene_geometry *decoded=NULL;
            if (!refs->geometry_decode(refs->context,key,&decoded,io->error) || !decoded) return false;
            qa_scene_geometry_retain(decoded); value->geometry=decoded;
        }
    }
    F(u64,value,identity); F(u64,value,revision);
    if (!qa_source_save_count(io,&value->vertex_count,UINT32_MAX) || !qa_source_save_count(io,&value->index_count,UINT32_MAX) ||
        !qa_source_save_u32(io,&primitive) || primitive>QA_SCENE_LINES || !bounds(io,&value->bounds)) return false;
    if (reading) value->primitive=(qa_scene_primitive)primitive;
    if (!present) return (!value->identity && !value->revision && !value->vertex_count && !value->index_count &&
        !surface->vertices && !surface->indices) || fail(io,QA_ERROR_FORMAT,"Mesh allocation is absent");
    qa_scene_geometry_view allocation;
    if (!value->identity || !value->revision || !qa_scene_geometry_read(value->geometry,&allocation) ||
        value->vertex_count>allocation.vertex_count || value->index_count>allocation.index_count)
        return fail(io,QA_ERROR_FORMAT,"World mesh allocation extent changed");
    if (reading) {
        surface->vertices=(qa_scene_vertex *)allocation.vertices; surface->indices=(uint32_t *)allocation.indices;
        value->vertices=allocation.vertices; value->indices=allocation.indices;
    } else if (surface->vertices!=allocation.vertices || surface->indices!=allocation.indices ||
        value->vertices!=allocation.vertices || value->indices!=allocation.indices)
        return fail(io,QA_ERROR_ARGUMENT,"World mesh storage aliases changed");
    return true;
}
static bool surfaces(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs)
{
    for (size_t i=0;i<world->surface_count;++i) {
        qaw_surface *surface=&world->surfaces[i]; uint32_t type=surface->type;
        F(u32,surface,source_index); F(u32,surface,fog_index); F(bool,surface,has_plane);
        if (surface->source_index!=i || !plane(io,&surface->plane) || !qa_source_save_u32(io,&type) ||
            type>QA_BSP_SURFACE_FLARE || !mesh(io,surface,refs)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) surface->type=(qa_bsp_surface_type)type;
        if (world->bsp.family==QA_BSP_Q3) {
            qa_bsp_surface source;
            if (!qa_bsp_read_surface(&world->bsp,i,&source,io->error) || source.type!=surface->type ||
                surface->fog_index!=(source.fog>=0?(uint32_t)source.fog+1:0)) return false;
        } else if (surface->type!=QA_BSP_SURFACE_PLANAR || surface->fog_index) return false;
        bool patched=surface->patch!=NULL;
        if (!qa_source_save_bool(io,&patched)) return false;
        if (!patched) continue;
        if (world->bsp.family!=QA_BSP_Q3 || surface->type!=QA_BSP_SURFACE_PATCH) return false;
        if (io->direction==QA_SOURCE_SAVE_READ && !array(io,(void **)&surface->patch,1,sizeof(*surface->patch))) return false;
        qaw_patch *patch=surface->patch; uint32_t width=patch->width,height=patch->height;
        if (!qa_source_save_u32(io,&width) || !qa_source_save_u32(io,&height) || width<2 || height<2 ||
            width>QAW_PATCH_LIMIT || height>QAW_PATCH_LIMIT || (size_t)width*height!=surface->mesh.vertex_count) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) { patch->width=width; patch->height=height; }
        for (size_t j=0;j<QAW_PATCH_LIMIT;++j)
            if (!qa_source_save_f32(io,&patch->width_error[j]) || !qa_source_save_f32(io,&patch->height_error[j])) return false;
        F(vec3,patch,lod_origin); F(f32,patch,lod_radius); F(bool,patch,stitched); F(bool,patch,fixed);
    }
    return true;
}
static bool image(qa_source_save_io *io, qa_scene_world *world, const qa_scene_world_image_refs *refs, qa_scene_image **value)
{
    bool present=*value!=NULL; uint64_t key=0; size_t ordinal;
    const qa_scene_resources *owners[]={world->resources};
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    if (io->direction==QA_SOURCE_SAVE_WRITE && (!qa_scene_image_owner_index(owners,1,*value,&ordinal) ||
        !refs->encode(refs->context,*value,&key,io->error))) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded=NULL;
        if (!refs->decode(refs->context,key,&decoded,io->error) || !decoded ||
            !qa_scene_image_owner_index(owners,1,decoded,&ordinal)) return false;
        qa_scene_image_retain(decoded); *value=(qa_scene_image *)decoded;
    }
    return true;
}
static bool q3_fields(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs)
{
    q3_data *data=world->q3_data;
    if (world->bsp.lumps[QA_BSP_LIGHTING].bytes.size%(128*128*3)) return false;
    size_t grid_extent=qa_bsp_record_count(&world->bsp,QA_BSP_LIGHTGRID);
    if (!count(io,&data->lightmap_count,world->bsp.lumps[QA_BSP_LIGHTING].bytes.size/(128*128*3)) ||
        !count(io,&data->fog_count,qa_bsp_record_count(&world->bsp,QA_BSP_FOGS)) ||
        !qa_source_save_count(io,&data->grid_count,grid_extent) ||
        !array(io,(void **)&data->lightmaps,data->lightmap_count,sizeof(*data->lightmaps)) ||
        !array(io,(void **)&data->fogs,data->fog_count,sizeof(*data->fogs)) ||
        !array(io,(void **)&data->grid,data->grid_count,sizeof(*data->grid))) return false;
    size_t grid_product=1;
    for (size_t j=0;j<3;++j) {
        if (!qa_source_save_count(io,&data->grid_bounds[j],grid_extent)) return false;
        if (data->grid_count) {
            if (!data->grid_bounds[j] || data->grid_bounds[j]>data->grid_count/grid_product) return false;
            grid_product*=data->grid_bounds[j];
        }
    }
    if (data->grid_count && (data->grid_count!=grid_extent || grid_product!=data->grid_count)) return false;
    F(vec3,data,grid_origin); F(vec3,data,grid_inverse);
    q3_grid_layout layout;
    if (!qaw_q3_grid_layout(world,&layout,io->error) || layout.count!=data->grid_count ||
        memcmp(layout.bounds,data->grid_bounds,sizeof(layout.bounds)) ||
        !source_equal(&layout.size,&world->grid_size,sizeof(layout.size)) ||
        !source_equal(&layout.origin,&data->grid_origin,sizeof(layout.origin)) ||
        !source_equal(&layout.inverse,&data->grid_inverse,sizeof(layout.inverse)) || world->options.q3_overbright>15) return false;
    for (size_t i=0;i<data->lightmap_count;++i) {
        if (!image(io,world,&refs->images,&data->lightmaps[i]) || !data->lightmaps[i]) return false;
        for (size_t j=0;j<i;++j) if (data->lightmaps[j]==data->lightmaps[i]) return false;
    }
    for (size_t i=0;i<world->surface_count;++i) {
        qa_scene_image **value=&world->surfaces[i].lightmap;
        if (!image(io,world,&refs->images,value)) return false;
        if (*value) {
            size_t j=0;
            while (j<data->lightmap_count && data->lightmaps[j]!=*value) ++j;
            if (j==data->lightmap_count) return false;
        }
    }
    for (size_t i=0;i<data->fog_count;++i) {
        q3_fog *value=&data->fogs[i];
        if (!fog(io,&value->fog) || !bounds(io,&value->bounds) || !plane(io,&value->surface)) return false;
        F(f32,value,tc_scale); F(bool,value,active); F(bool,value,has_surface);
    }
    for (size_t i=0;i<data->grid_count;++i) {
        qa_bsp_grid_point *value=&data->grid[i];
        if (!qa_source_save_bytes(io,value->ambient,3) || !qa_source_save_bytes(io,value->directed,3) ||
            !qa_source_save_bytes(io,value->lat_long,2)) return false;
        qa_bsp_grid_point source; uint8_t ambient[3], directed[3];
        if (!qa_bsp_read_grid_point(&world->bsp,i,&source,io->error)) return false;
        qaw_q3_shift_color(source.ambient,world->options.q3_overbright,ambient);
        qaw_q3_shift_color(source.directed,world->options.q3_overbright,directed);
        if (memcmp(ambient,value->ambient,3) || memcmp(directed,value->directed,3) || memcmp(source.lat_long,value->lat_long,2)) return false;
    }
    return true;
}
bool qaw_owner_core_fields(qa_source_save_io *io, qa_scene_world *world, const qaw_owner_refs *refs)
{
    if (!io || !world || !refs || !refs->geometry_encode || !refs->geometry_decode || !refs->images.encode || !refs->images.decode)
        return false;
    if (io->direction==QA_SOURCE_SAVE_READ && (world->planes || world->nodes || world->surfaces || world->q3_data))
        return fail(io,QA_ERROR_ARGUMENT,"Detached world core is already populated");
    if (!topology(io,world)) return false;
    if (world->bsp.family==QA_BSP_Q3 && !array(io,&world->q3_data,1,sizeof(q3_data))) return false;
    return surfaces(io,world,refs) &&
        (world->bsp.family!=QA_BSP_Q3 || q3_fields(io,world,refs));
}
