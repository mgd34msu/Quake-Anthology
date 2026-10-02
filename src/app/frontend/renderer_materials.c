#include "renderer_materials.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/source_save.h"
#include "q3_render_policy.h"

struct frontend_renderer_materials {
    qa_frontend *frontend;
    frontend_renderer_materials_view view;
    qa_resource_pool *pool,*lightmap_pool;
};
static bool renderer_library(const qa_frontend *f,qa_material_library **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained shader has two physical renderers");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!controls) { *out=NULL; return true; }
    const qa_material_source_scratch *scratch=qa_render_controls_source_metadata(controls,error);
    const qa_material *material=NULL;
    if(!scratch || !qa_material_source_material_metadata(scratch,&material,error)) return false;
    *out=material?material->library:NULL; return true;
}
static bool parent_library(const qa_frontend *f,const qa_material_library *library,bool *found,qa_error *error)
{
    *found=library==f->materials;
    for(size_t i=0;!*found && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view row;
        if(!frontend_source_group_read(f,i,&row)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view row;
        if(!frontend_visual_owner_read(f,i,&row)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view row;
        if(!frontend_native_q3_read(f,i,&row,error)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources row;
        if(!(f->resource_inventory?frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&row,error):
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&row,error))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_initial_count(f);++i) {
        frontend_remote_q3_initial_view row;
        if(!(f->resource_inventory?frontend_remote_q3_initial_metadata_read(frontend_remote_q3_initial_at(f,i),&row,error):
            frontend_remote_q3_initial_read(frontend_remote_q3_initial_at(f,i),&row,error))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_media row;
        if(!frontend_remote_q1_media_read(frontend_remote_q1_at(f,i),&row,error)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view row;
        if(!frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&row,error)) return false;
        *found=library==row.materials;
    }
    return true;
}
static bool renderer_lightmap(const qa_frontend *f,const qa_scene_image **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained lightmap has two physical renderers");
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!controls) { *out=NULL; return true; }
    const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
    return source && qa_material_source_lightmap_metadata(source,out,error);
}
static bool parent_images(const qa_frontend *f,const qa_scene_resources *images,bool *found,qa_error *error)
{
    *found=images==f->images || images==f->ui_images;
    for(size_t i=0;!*found && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view row;
        if(!frontend_source_group_read(f,i,&row)) return false;
        *found=images==row.images;
    }
    for(unsigned kind=0;!*found && kind<3;++kind) for(size_t i=0;!*found;++i) {
        const qa_scene_resources *row=kind==0?frontend_event_images_at((qa_frontend *)f,i):
            kind==1?frontend_visual_images_at((qa_frontend *)f,i):frontend_native_q2_images_at(f,i);
        if(!row) break;
        *found=images==row;
    }
    for(size_t i=0;!*found && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view row;
        if(!frontend_native_q3_read(f,i,&row,error)) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources row;
        if(!(f->resource_inventory?frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&row,error):
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&row,error))) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_initial_count(f);++i) {
        frontend_remote_q3_initial_view row;
        if(!(f->resource_inventory?frontend_remote_q3_initial_metadata_read(frontend_remote_q3_initial_at(f,i),&row,error):
            frontend_remote_q3_initial_read(frontend_remote_q3_initial_at(f,i),&row,error))) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_media row;
        if(!frontend_remote_q1_media_read(frontend_remote_q1_at(f,i),&row,error)) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view row;
        if(!frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&row,error)) return false;
        *found=images==row.images;
    }
    return true;
}
bool frontend_renderer_materials_read(const qa_frontend *f,frontend_renderer_materials_view *out,bool *present,qa_error *error)
{
    if(!f || !out || !present) return false;
    *present=false; *out=(frontend_renderer_materials_view){0};
    if(f->source_restoring && f->renderer_materials) {
        const frontend_renderer_materials_view *row=&f->renderer_materials->view;
        if(f->renderer_materials->frontend!=f ||
            (!!row->library!=!!row->images) || (!!row->images!=!!row->mounts) ||
            (!!row->lightmap_images!=!!row->lightmap_mounts) || (!row->library && !row->lightmap_images)) return false;
        *out=f->renderer_materials->view; *present=true; return true;
    }
    qa_material_library *library=NULL; bool known=false;
    if(!renderer_library(f,&library,error)) return false;
    if(library) {
        if(!parent_library(f,library,&known,error)) return false;
        if(!known) {
            qa_scene_resources *images=qa_material_library_resource_owner(library);
            qa_vfs *mounts=images?qa_scene_resources_files(images):NULL;
            if(!images || !mounts) return frontend_fail(error,QA_ERROR_FORMAT,"Retained renderer shader lost its actual bank and VFS");
            out->library=library; out->images=images; out->mounts=mounts;
        }
    }
    const qa_scene_image *lightmap=NULL;
    if(!renderer_lightmap(f,&lightmap,error)) return false;
    if(lightmap) {
        qa_scene_resources *images=qa_scene_image_resource_owner(lightmap);
        if(!images) return frontend_fail(error,QA_ERROR_FORMAT,"Retained Source lightmap lost its actual bank owner");
        if(images!=out->images) {
            if(!parent_images(f,images,&known,error)) return false;
            if(!known) {
                out->lightmap_images=images; out->lightmap_mounts=qa_scene_resources_files(images);
                if(!out->lightmap_mounts) return false;
            }
        }
    }
    *present=out->library || out->lightmap_images; return true;
}
bool frontend_renderer_materials_destroy(frontend_renderer_materials **out,qa_error *error)
{
    if(!out || !*out) return true;
    frontend_renderer_materials *owner=*out;
    if(owner->frontend->capture || owner->frontend->resource_inventory ||
        (owner->view.library && !qa_material_library_idle(owner->view.library)) ||
        (owner->view.images && !qa_scene_resources_idle(owner->view.images)) ||
        (owner->view.lightmap_images && !qa_scene_resources_idle(owner->view.lightmap_images)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained renderer material owners remain borrowed");
    qa_material_library_destroy(owner->view.library);
    qa_scene_resources_destroy(owner->view.images);
    qa_vfs_destroy(owner->view.mounts); qa_resource_pool_destroy(owner->pool);
    qa_scene_resources_destroy(owner->view.lightmap_images);
    qa_vfs_destroy(owner->view.lightmap_mounts); qa_resource_pool_destroy(owner->lightmap_pool);
    free(owner); *out=NULL; return true;
}
bool frontend_renderer_materials_prune(qa_frontend *f,qa_error *error)
{
    if(!f || f->source_restoring || !f->renderer_materials) return f!=NULL;
    qa_material_library *library=NULL;
    const qa_scene_image *lightmap=NULL;
    return renderer_library(f,&library,error) && renderer_lightmap(f,&lightmap,error) &&
        ((library && library==f->renderer_materials->view.library) ||
        (lightmap && qa_scene_image_resource_owner(lightmap)==f->renderer_materials->view.lightmap_images) ||
        frontend_renderer_materials_destroy(&f->renderer_materials,error));
}
bool frontend_renderer_materials_checkpoint(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    frontend_renderer_materials_view row={0}; bool present=false; qa_source_save_io io={0};
    uint8_t magic[4]={'Q','F','R','M'}; uint32_t version=2; uint64_t view=0,pool=0,lightmap_view=0,lightmap_pool=0;
    bool shader=false,lightmap=false;
    qa_application_content_graph *graph=f?qa_application_content_graph_read(f->application):NULL;
    bool okay=f && f->capture && graph && frontend_renderer_materials_read(f,&row,&present,error);
    if(okay) { shader=row.library!=NULL; lightmap=row.lightmap_images!=NULL; }
    if(okay && shader) okay=(view=qa_application_content_view_id(graph,row.mounts))!=0 &&
        (pool=qa_application_content_pool_id(graph,qa_vfs_resources(row.mounts)))!=0;
    if(okay && lightmap) okay=(lightmap_view=qa_application_content_view_id(graph,row.lightmap_mounts))!=0 &&
        (lightmap_pool=qa_application_content_pool_id(graph,qa_vfs_resources(row.lightmap_mounts)))!=0;
    okay=okay && qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_u32(&io,&version) && qa_source_save_bool(&io,&shader) &&
        qa_source_save_u64(&io,&view) && qa_source_save_u64(&io,&pool) && qa_source_save_bool(&io,&lightmap) &&
        qa_source_save_u64(&io,&lightmap_view) && qa_source_save_u64(&io,&lightmap_pool) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_renderer_materials_prepare_restored(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    qa_source_save_io io={0}; uint8_t magic[4]={0}; uint32_t version=0; bool shader=false,lightmap=false;
    uint64_t view=0,pool=0,lightmap_view=0,lightmap_pool=0;
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || f->renderer_materials) return false;
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    bool okay=graph && qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QFRM",4) && qa_source_save_u32(&io,&version) && version==2 &&
        qa_source_save_bool(&io,&shader) && qa_source_save_u64(&io,&view) && qa_source_save_u64(&io,&pool) &&
        qa_source_save_bool(&io,&lightmap) && qa_source_save_u64(&io,&lightmap_view) && qa_source_save_u64(&io,&lightmap_pool) &&
        shader==(view!=0) && shader==(pool!=0) && lightmap==(lightmap_view!=0) && lightmap==(lightmap_pool!=0) &&
        (!shader ||
            (qa_application_content_view(graph,view) && qa_application_content_pool(graph,pool) &&
             qa_vfs_resources(qa_application_content_view(graph,view))==qa_application_content_pool(graph,pool))) &&
        (!lightmap || (qa_application_content_view(graph,lightmap_view) && qa_application_content_pool(graph,lightmap_pool) &&
             qa_vfs_resources(qa_application_content_view(graph,lightmap_view))==qa_application_content_pool(graph,lightmap_pool))) &&
        (!shader || !lightmap || view!=lightmap_view) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!okay || (!shader && !lightmap)) return okay;
    frontend_renderer_materials *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining restored renderer material parents");
    owner->frontend=f; f->renderer_materials=owner;
    if(shader) {
        if(!qa_application_content_retain_pool(graph,pool,&owner->pool,error) ||
            !qa_application_content_claim_view(graph,view,&owner->view.mounts,error)) return false;
        owner->view.images=qa_scene_resources_create_detached(owner->view.mounts,error);
        if(!owner->view.images) return false;
        owner->view.library=qa_material_library_create_detached(owner->view.images,error);
        if(!owner->view.library) return false;
    }
    if(lightmap) {
        if(!qa_application_content_retain_pool(graph,lightmap_pool,&owner->lightmap_pool,error) ||
            !qa_application_content_claim_view(graph,lightmap_view,&owner->view.lightmap_mounts,error)) return false;
        owner->view.lightmap_images=qa_scene_resources_create_detached(owner->view.lightmap_mounts,error);
        if(!owner->view.lightmap_images) return false;
    }
    return true;
}
bool frontend_renderer_materials_bind_restored(qa_frontend *f,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory) return false;
    return !f->renderer_materials || !f->renderer_materials->view.library ||
        !qa_material_library_has_source_profile(f->renderer_materials->view.library) ||
        frontend_q3_material_source_bind(f,f->renderer_materials->view.library,error);
}
