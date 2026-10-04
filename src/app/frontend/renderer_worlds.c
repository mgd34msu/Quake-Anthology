#include "renderer_worlds.h"
#include "renderer_registries.h"
#include "qa/render_controls.h"
#include "qa/material_source_scratch.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include "q3_render_policy.h"

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
    frontend_scene_heap heap={0}; bool found=false;
    if(!frontend_scene_heap_find(f,out->files,out->images,out->materials,&heap,&found,error)) return false;
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
bool frontend_renderer_worlds_checkpoint(qa_frontend *f,const frontend_world_inventory *roots,qa_buffer *out,qa_error *error)
{
    size_t count=0;
    if(!f || !f->capture || !roots || !frontend_renderer_worlds_count(f,&count,error)) return false;
    uint8_t magic[4]={'Q','F','R','W'}; uint64_t rows=count; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_u64(&io,&rows);
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    for(size_t i=0;ok && i<count;++i) {
        frontend_renderer_worlds_view view={0}; frontend_scene_heap heap={0};
        uint64_t root=0,pool=0,resource=0; bool owned=false,found=false;
        ok=frontend_renderer_worlds_read_at(f,i,&view,error);
        for(size_t j=0;ok && j<frontend_world_inventory_world_count(roots);++j) {
            frontend_world_source source; frontend_scene_owner owner;
            ok=frontend_world_inventory_world_at(roots,j,&source,&owner);
            if(ok && source.world==view.world) {
                root=j+1; owned=owner.kind==FRONTEND_SCENE_OWNER_RENDERER;
                if(owned && owner.owner!=i+1) ok=false;
                break;
            }
        }
        ok=ok && root && frontend_scene_heap_find(f,view.files,view.images,view.materials,&heap,&found,error) &&
            qa_application_content_resource_id(graph,view.resource,&pool,&resource);
        if(ok && !found) {
            heap=(frontend_scene_heap){9,i,qa_application_content_view_id(graph,view.files)};
            for(size_t j=0;j<i;++j) {
                frontend_renderer_worlds_view prior={0};
                if(!frontend_renderer_worlds_read_at(f,j,&prior,error)) { ok=false; break; }
                if(prior.files==view.files && prior.images==view.images && prior.materials==view.materials) {
                    heap.ordinal=j; break;
                }
            }
        }
        ok=ok && heap.view && qa_source_save_bool(&io,&owned) && qa_source_save_u64(&io,&root) &&
            qa_source_save_u32(&io,&heap.kind) && qa_source_save_u64(&io,&heap.ordinal) &&
            qa_source_save_u64(&io,&heap.view) && qa_source_save_u64(&io,&pool) && qa_source_save_u64(&io,&resource);
    }
    ok=ok && qa_source_save_finish(&io,out); qa_source_save_dispose(&io); return ok;
}
typedef struct saved_world_row { bool owned; uint64_t root,pool,resource; frontend_scene_heap heap; } saved_world_row;
bool frontend_renderer_worlds_prepare_restored(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || f->renderer_worlds) return false;
    uint8_t magic[4]={0}; uint64_t count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QFRW",4) && qa_source_save_u64(&io,&count) &&
        count<=SIZE_MAX/sizeof(saved_world_row) && count<=bytes.size/45;
    saved_world_row *saved=ok && count?calloc((size_t)count,sizeof(*saved)):NULL;
    if(ok && count && !saved) ok=frontend_fail(error,QA_ERROR_MEMORY,"Decoding renderer world roster");
    for(size_t i=0;ok && i<(size_t)count;++i) {
        saved_world_row *row=&saved[i];
        ok=qa_source_save_bool(&io,&row->owned) && qa_source_save_u64(&io,&row->root) && row->root &&
            qa_source_save_u32(&io,&row->heap.kind) && row->heap.kind<=12 &&
            qa_source_save_u64(&io,&row->heap.ordinal) && qa_source_save_u64(&io,&row->heap.view) && row->heap.view &&
            qa_source_save_u64(&io,&row->pool) && row->pool && qa_source_save_u64(&io,&row->resource) && row->resource &&
            (row->heap.kind!=9 || row->heap.ordinal<=i);
        for(size_t j=0;ok && j<i;++j) if(saved[j].root==row->root) ok=false;
        if(ok && row->heap.kind==9 && row->heap.ordinal<i) {
            const saved_world_row *prior=&saved[row->heap.ordinal];
            ok=prior->heap.kind==9 && prior->heap.ordinal==row->heap.ordinal &&
                prior->heap.view==row->heap.view && prior->pool==row->pool;
        }
    }
    ok=ok && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    if(!ok || !count) { free(saved); return ok; }
    frontend_renderer_worlds *owner=calloc(1,sizeof(*owner));
    if(!owner) { free(saved); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining imported renderer worlds"); }
    owner->frontend=f; f->renderer_worlds=owner;
    owner->rows=calloc((size_t)count,sizeof(*owner->rows));
    if(!owner->rows) { free(saved); return frontend_fail(error,QA_ERROR_MEMORY,"Allocating imported world custody rows"); }
    owner->count=(size_t)count;
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    for(size_t i=0;ok && i<owner->count;++i) {
        renderer_world_row *row=&owner->rows[i]; const saved_world_row *from=&saved[i];
        const qa_vfs *files=qa_application_content_view(graph,from->heap.view);
        const qa_resource *map=qa_application_content_resource(graph,from->pool,from->resource);
        ok=files && map && qa_vfs_resources(files)==qa_application_content_pool(graph,from->pool) &&
            qa_resource_pool_find(qa_vfs_resources(files),qa_resource_id(map))==map;
        if(!ok) break;
        row->root=from->root; row->owned=from->owned; row->view.resource=map;
        qa_resource_retain((qa_resource *)map);
        if(from->heap.kind==9 && from->heap.ordinal==i) {
            row->view.private_heaps=true; qa_vfs *claimed=NULL;
            ok=qa_application_content_retain_pool(graph,from->pool,&row->pool,error) &&
                qa_application_content_retain_view(graph,from->heap.view,&claimed,error);
            row->view.files=claimed;
            if(ok) row->view.images=qa_scene_resources_create_detached(claimed,error);
            ok=ok && row->view.images!=NULL;
            if(ok) row->view.materials=qa_material_library_create_detached(row->view.images,error);
            ok=ok && row->view.materials!=NULL;
        } else if(from->heap.kind==9) {
            const renderer_world_row *prior=&owner->rows[from->heap.ordinal];
            row->view.files=prior->view.files; row->view.images=prior->view.images; row->view.materials=prior->view.materials;
        } else ok=frontend_scene_heap_read(f,from->heap,&row->view.files,&row->view.images,&row->view.materials) && row->view.files==files;
    }
    free(saved); return ok;
}
bool frontend_renderer_worlds_attach_restored(qa_frontend *f,frontend_world_inventory *roots,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || !roots) return false;
    frontend_renderer_worlds *owner=f->renderer_worlds;
    for(size_t i=0;owner && i<owner->count;++i) {
        renderer_world_row *row=&owner->rows[i]; qa_scene_world *world=NULL;
        if(!frontend_world_decode(roots,row->root,&world,error) ||
            qa_scene_world_resource_owner(world)!=row->view.images ||
            qa_scene_world_material_owner(world)!=row->view.materials ||
            qa_scene_world_source_resource_read(world)!=row->view.resource) return false;
        if(row->view.world) { if(row->view.world!=world) return false; continue; }
        if(row->owned) {
            if(!frontend_world_owner_ready(roots,row->root,FRONTEND_SCENE_OWNER_RENDERER,i+1,error)) return false;
            row->view.world=world; frontend_world_adopt(roots,row->root);
        } else {
            if(!qa_scene_world_retain(world,error)) return false;
            row->view.world=world;
        }
    }
    return true;
}
bool frontend_renderer_worlds_bind_restored(qa_frontend *f,qa_error *error)
{
    frontend_renderer_worlds *owner=f?f->renderer_worlds:NULL;
    for(size_t i=0;owner && i<owner->count;++i) {
        const frontend_renderer_worlds_view *view=&owner->rows[i].view;
        if(view->private_heaps && qa_material_library_has_source_profile(view->materials) &&
            !frontend_q3_material_source_bind(f,view->materials,error)) return false;
    }
    return true;
}
