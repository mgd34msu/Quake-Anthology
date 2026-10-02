#include "renderer_worlds.h"
#include "qa/render_controls.h"
#include "qa/material_source_scratch.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include "q3_render_policy.h"

struct frontend_renderer_worlds {
    qa_frontend *frontend;
    frontend_renderer_worlds_view view;
    qa_resource_pool *pool;
    uint64_t root;
};
static bool physical_world(const qa_frontend *f,const qa_scene_world **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained Source world has two physical renderers");
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!controls) { *out=NULL; return true; }
    const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
    return source && qa_material_source_world_metadata(source,out,error);
}
bool frontend_renderer_worlds_read(const qa_frontend *f,frontend_renderer_worlds_view *out,bool *present,qa_error *error)
{
    if(!f || !out || !present) return false;
    *out=(frontend_renderer_worlds_view){0}; *present=false;
    if(f->source_restoring && f->renderer_worlds) {
        if(f->renderer_worlds->frontend!=f) return false;
        *out=f->renderer_worlds->view; *present=true; return true;
    }
    if(!physical_world(f,&out->world,error)) return false;
    if(!out->world) return true;
    out->resource=qa_scene_world_source_resource_read(out->world);
    out->images=qa_scene_world_resource_owner(out->world);
    out->materials=qa_scene_world_material_owner(out->world);
    out->files=out->images?qa_scene_resources_files(out->images):NULL;
    if(!out->resource || !out->images || !out->materials || !out->files ||
        qa_material_library_resource_owner(out->materials)!=out->images ||
        qa_resource_pool_find(qa_vfs_resources(out->files),qa_resource_id(out->resource))!=out->resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Retained Source world lost its actual immutable map and paired heaps");
    frontend_scene_heap heap={0}; bool found=false;
    if(!frontend_scene_heap_find(f,out->files,out->images,out->materials,&heap,&found,error)) return false;
    out->private_heaps=!found; *present=true; return true;
}
bool frontend_renderer_worlds_destroy(frontend_renderer_worlds **slot,qa_error *error)
{
    if(!slot || !*slot) return true;
    frontend_renderer_worlds *owner=*slot;
    if(owner->frontend->capture || owner->frontend->resource_inventory ||
        (owner->view.world && !qa_scene_world_idle(owner->view.world)) ||
        (owner->view.private_heaps && ((owner->view.images && !qa_scene_resources_idle(owner->view.images)) ||
        (owner->view.materials && !qa_material_library_idle(owner->view.materials)))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained renderer world remains borrowed");
    qa_scene_world_destroy((qa_scene_world *)owner->view.world);
    if(owner->view.private_heaps) {
        qa_material_library_destroy(owner->view.materials);
        qa_scene_resources_destroy(owner->view.images);
        qa_vfs_destroy((qa_vfs *)owner->view.files); qa_resource_pool_destroy(owner->pool);
    }
    qa_resource_release((qa_resource *)owner->view.resource);
    free(owner); *slot=NULL; return true;
}
bool frontend_renderer_worlds_idle(const frontend_renderer_worlds *owner)
{
    return !owner || ((!owner->view.world || qa_scene_world_idle(owner->view.world)) &&
        (!owner->view.private_heaps || ((!owner->view.images || qa_scene_resources_idle(owner->view.images)) &&
        (!owner->view.materials || qa_material_library_idle(owner->view.materials)))));
}
bool frontend_renderer_worlds_prune(qa_frontend *f,qa_error *error)
{
    if(!f || f->source_restoring || !f->renderer_worlds) return f!=NULL;
    const qa_scene_world *world=NULL;
    return physical_world(f,&world,error) && (world==f->renderer_worlds->view.world ||
        frontend_renderer_worlds_destroy(&f->renderer_worlds,error));
}
bool frontend_renderer_worlds_checkpoint(qa_frontend *f,const frontend_world_inventory *roots,qa_buffer *out,qa_error *error)
{
    frontend_renderer_worlds_view view={0}; bool present=false,owned=false;
    frontend_scene_heap heap={0}; uint64_t root=0,pool=0,resource=0;
    if(!f || !f->capture || !roots || !frontend_renderer_worlds_read(f,&view,&present,error)) return false;
    for(size_t i=0;present && i<frontend_world_inventory_world_count(roots);++i) {
        frontend_world_source source; frontend_scene_owner owner;
        if(!frontend_world_inventory_world_at(roots,i,&source,&owner)) return false;
        if(source.world==view.world && owner.kind==FRONTEND_SCENE_OWNER_RENDERER) { root=i+1; owned=true; break; }
    }
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    bool found=false;
    if(owned && (!frontend_scene_heap_find(f,view.files,view.images,view.materials,&heap,&found,error) ||
        !qa_application_content_resource_id(graph,view.resource,&pool,&resource))) return false;
    if(owned && !found) heap=(frontend_scene_heap){9,0,qa_application_content_view_id(graph,view.files)};
    uint8_t magic[4]={'Q','F','R','W'}; uint32_t version=1; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_u32(&io,&version) && qa_source_save_bool(&io,&owned);
    if(ok && owned) ok=heap.view && qa_source_save_u64(&io,&root) && qa_source_save_u32(&io,&heap.kind) &&
        qa_source_save_u64(&io,&heap.ordinal) && qa_source_save_u64(&io,&heap.view) &&
        qa_source_save_u64(&io,&pool) && qa_source_save_u64(&io,&resource);
    ok=ok && qa_source_save_finish(&io,out); qa_source_save_dispose(&io); return ok;
}
bool frontend_renderer_worlds_prepare_restored(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || f->renderer_worlds) return false;
    uint8_t magic[4]={0}; uint32_t version=0; bool present=false;
    uint64_t root=0,pool=0,resource=0; frontend_scene_heap heap={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QFRW",4) && qa_source_save_u32(&io,&version) && version==1 && qa_source_save_bool(&io,&present);
    if(ok && present) ok=qa_source_save_u64(&io,&root) && root && qa_source_save_u32(&io,&heap.kind) && heap.kind<=9 &&
        qa_source_save_u64(&io,&heap.ordinal) && qa_source_save_u64(&io,&heap.view) && heap.view &&
        qa_source_save_u64(&io,&pool) && pool && qa_source_save_u64(&io,&resource) && resource &&
        (heap.kind!=9 || !heap.ordinal);
    ok=ok && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    if(!ok || !present) return ok;
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    const qa_vfs *files=qa_application_content_view(graph,heap.view);
    const qa_resource *map=qa_application_content_resource(graph,pool,resource);
    if(!files || !map || qa_vfs_resources(files)!=qa_application_content_pool(graph,pool) ||
        qa_resource_pool_find(qa_vfs_resources(files),qa_resource_id(map))!=map) return false;
    frontend_renderer_worlds *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining imported renderer world custody");
    owner->frontend=f; owner->root=root; owner->view.private_heaps=heap.kind==9;
    owner->view.resource=map; qa_resource_retain((qa_resource *)map); f->renderer_worlds=owner;
    if(owner->view.private_heaps) {
        qa_vfs *claimed=NULL;
        if(!qa_application_content_retain_pool(graph,pool,&owner->pool,error) ||
            !qa_application_content_retain_view(graph,heap.view,&claimed,error)) return false;
        owner->view.files=claimed;
        owner->view.images=qa_scene_resources_create_detached(claimed,error);
        if(!owner->view.images) return false;
        owner->view.materials=qa_material_library_create_detached(owner->view.images,error);
        return owner->view.materials!=NULL;
    }
    return frontend_scene_heap_read(f,heap,&owner->view.files,&owner->view.images,&owner->view.materials) &&
        owner->view.files==files;
}
bool frontend_renderer_worlds_attach_restored(qa_frontend *f,frontend_world_inventory *roots,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || !roots) return false;
    frontend_renderer_worlds *owner=f->renderer_worlds;
    if(!owner) return true;
    qa_scene_world *world=NULL;
    if(owner->view.world || !frontend_world_decode(roots,owner->root,&world,error) ||
        !frontend_world_owner_ready(roots,owner->root,FRONTEND_SCENE_OWNER_RENDERER,1,error) ||
        qa_scene_world_resource_owner(world)!=owner->view.images ||
        qa_scene_world_material_owner(world)!=owner->view.materials ||
        qa_scene_world_source_resource_read(world)!=owner->view.resource) return false;
    owner->view.world=world; frontend_world_adopt(roots,owner->root); return true;
}
bool frontend_renderer_worlds_bind_restored(qa_frontend *f,qa_error *error)
{
    frontend_renderer_worlds *owner=f?f->renderer_worlds:NULL;
    return !owner || !owner->view.private_heaps || !qa_material_library_has_source_profile(owner->view.materials) ||
        frontend_q3_material_source_bind(f,owner->view.materials,error);
}
