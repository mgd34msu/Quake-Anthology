#include "renderer_worlds.h"
#include "renderer_registries.h"
#include "qa/render_controls.h"
#include "qa/material_source_scratch.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"

typedef struct renderer_world_row {
    frontend_renderer_worlds_view view;
    qa_resource_pool *pool;
    uint64_t root;
    bool owned;
} renderer_world_row;
struct frontend_renderer_worlds {
    qa_frontend *frontend;
    renderer_world_row *rows;
    size_t count;
};
static bool source_read(const qa_frontend *f,const qa_material_source_scratch **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained Source worlds have two physical renderers");
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    *out=controls?qa_render_controls_source_metadata(controls,error):NULL;
    return !controls || *out!=NULL;
}
static bool occurrence(const qa_frontend *f,const qa_material_source_scratch *source,size_t ordinal,
    const qa_scene_world **out,qa_error *error)
{
    size_t count=qa_material_source_world_count(source);
    if(ordinal<count) { *out=qa_material_source_world_at(source,ordinal); return *out!=NULL; }
    ordinal-=count;
    for(size_t i=0;i<frontend_renderer_registries_count(f);++i) {
        qa_q3_presentation_assets *assets=NULL;
        if(!frontend_renderer_registries_at(f,i,&assets,error)) return false;
        count=qa_q3_assets_map_count(assets);
        if(ordinal<count) {
            qa_q3_asset_map_custody map={0};
            if(!qa_q3_assets_map_at(assets,ordinal,&map) || !map.world) return false;
            *out=map.world; return true;
        }
        ordinal-=count;
    }
    *out=NULL; return true;
}
static bool world_at(const qa_frontend *f,size_t wanted,const qa_scene_world **out,size_t *total,qa_error *error)
{
    const qa_material_source_scratch *source=NULL;
    if(!source_read(f,&source,error)) return false;
    size_t count=0;
    *out=NULL;
    for(size_t i=0;;++i) {
        const qa_scene_world *world=NULL;
        if(!occurrence(f,source,i,&world,error)) return false;
        if(!world) break;
        bool duplicate=false;
        for(size_t j=0;j<i;++j) {
            const qa_scene_world *prior=NULL;
            if(!occurrence(f,source,j,&prior,error)) return false;
            if(prior==world) { duplicate=true; break; }
        }
        if(duplicate) continue;
        if(count==wanted) *out=world;
        if(count==SIZE_MAX) return false;
        ++count;
    }
    if(total) *total=count;
    return true;
}
bool frontend_renderer_worlds_count(const qa_frontend *f,size_t *out,qa_error *error)
{
    if(!f || !out) return false;
    if(f->source_restoring && f->renderer_worlds) {
        if(f->renderer_worlds->frontend!=f) return false;
        *out=f->renderer_worlds->count; return true;
    }
    const qa_scene_world *world=NULL;
    return world_at(f,SIZE_MAX,&world,out,error);
}
bool frontend_renderer_worlds_read_at(const qa_frontend *f,size_t ordinal,frontend_renderer_worlds_view *out,qa_error *error)
{
    if(!f || !out) return false;
    *out=(frontend_renderer_worlds_view){0};
    if(f->source_restoring && f->renderer_worlds) {
        if(f->renderer_worlds->frontend!=f || ordinal>=f->renderer_worlds->count) return false;
        *out=f->renderer_worlds->rows[ordinal].view; return true;
    }
    if(!world_at(f,ordinal,&out->world,NULL,error) || !out->world) return false;
    out->resource=qa_scene_world_source_resource_read(out->world);
    out->images=qa_scene_world_resource_owner(out->world);
    out->materials=qa_scene_world_material_owner(out->world);
    out->files=out->images?qa_scene_resources_files(out->images):NULL;
    if(!out->resource || !out->images || !out->materials || !out->files ||
        qa_material_library_resource_owner(out->materials)!=out->images ||
        qa_resource_pool_find(qa_vfs_resources(out->files),qa_resource_id(out->resource))!=out->resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Retained Source world lost its immutable map and paired heaps");
    bool found=false;
    if(!frontend_scene_heap_find(f,out->files,out->images,out->materials,&found,error)) return false;
    out->private_heaps=!found; return true;
}
static bool row_idle(const renderer_world_row *row)
{
    return (!row->view.world || qa_scene_world_idle(row->view.world)) &&
        (!row->view.private_heaps || ((!row->view.images || qa_scene_resources_idle(row->view.images)) &&
        (!row->view.materials || qa_material_library_idle(row->view.materials))));
}
bool frontend_renderer_worlds_idle(const frontend_renderer_worlds *owner)
{
    if(owner) for(size_t i=0;i<owner->count;++i) if(!row_idle(&owner->rows[i])) return false;
    return true;
}
static void dispose_row(renderer_world_row *row)
{
    qa_scene_world_destroy((qa_scene_world *)row->view.world);
    if(row->view.private_heaps) {
        qa_material_library_destroy(row->view.materials);
        qa_scene_resources_destroy(row->view.images);
        qa_vfs_destroy((qa_vfs *)row->view.files); qa_resource_pool_destroy(row->pool);
    }
    qa_resource_release((qa_resource *)row->view.resource);
    *row=(renderer_world_row){0};
}
bool frontend_renderer_worlds_destroy(frontend_renderer_worlds **slot,qa_error *error)
{
    if(!slot || !*slot) return true;
    frontend_renderer_worlds *owner=*slot;
    if(!owner->frontend || owner->frontend->capture || owner->frontend->resource_inventory ||
        !frontend_renderer_worlds_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained renderer worlds remain borrowed");
    for(size_t i=owner->count;i>0;--i) dispose_row(&owner->rows[i-1]);
    free(owner->rows); free(owner); *slot=NULL; return true;
}
bool frontend_renderer_worlds_prune(qa_frontend *f,qa_error *error)
{
    if(!f || f->source_restoring || !f->renderer_worlds) return f!=NULL;
    frontend_renderer_worlds *owner=f->renderer_worlds;
    if(f->capture || f->resource_inventory || !frontend_renderer_worlds_idle(owner)) return false;
    for(size_t i=0;i<owner->count;) {
        const qa_material_source_scratch *source=NULL;
        if(!source_read(f,&source,error)) return false;
        bool found=false;
        for(size_t j=0;;++j) {
            const qa_scene_world *world=NULL;
            if(!occurrence(f,source,j,&world,error)) return false;
            if(!world) break;
            if(world==owner->rows[i].view.world) found=true;
            if(j==SIZE_MAX) return false;
        }
        if(found) { ++i; continue; }
        dispose_row(&owner->rows[i]);
        memmove(owner->rows+i,owner->rows+i+1,(owner->count-i-1)*sizeof(*owner->rows));
        --owner->count;
    }
    return owner->count || frontend_renderer_worlds_destroy(&f->renderer_worlds,error);
}
