#include "world_inventory.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "source_restore.h"
#include "save_private.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"

typedef struct root_heap { uint32_t kind; uint64_t ordinal, view; } root_heap;
typedef struct world_policy {
    qa_scene_world_options options;
    char *sky;
    qa_bytes spans[3];
} world_policy;
typedef struct world_row {
    frontend_world_source source;
    frontend_scene_owner owner;
    root_heap heap;
    uint64_t pool, resource;
    world_policy policy;
    qa_buffer captured;
    qa_bytes state;
    bool claimed, adopted;
} world_row;
typedef struct model_row {
    frontend_scene_root_view view;
    root_heap heap;
    uint64_t pool, resource, model;
    char *path;
    qa_buffer captured;
    qa_bytes state;
    bool claimed, adopted;
} model_row;
struct frontend_world_inventory {
    qa_frontend *frontend;
    frontend_model_inventory *models;
    frontend_scene_namespace *space;
    world_row *worlds;
    model_row *models_roots;
    size_t world_count, model_count;
    bool restoring;
};
typedef struct world_scope {
    frontend_world_inventory *inventory;
    world_row *row;
    frontend_scene_identity_scope identity;
} world_scope;

static bool heap_read(const qa_frontend *f, root_heap heap, const qa_vfs **files,
    qa_scene_resources **images, qa_material_library **materials)
{
    if (heap.kind == 0) {
        if (heap.ordinal) return false;
        *files=f->mounts; *images=f->images; *materials=f->materials;
    } else if (heap.kind == 1) {
        frontend_source_group_view group;
        if (heap.ordinal>SIZE_MAX || !frontend_source_group_read(f,(size_t)heap.ordinal,&group)) return false;
        *files=group.mounts; *images=group.images; *materials=group.materials;
    } else if (heap.kind == 2) {
        frontend_visual_owner_view owner;
        if (heap.ordinal>SIZE_MAX || !frontend_visual_owner_read(f,(size_t)heap.ordinal,&owner)) return false;
        *files=owner.mounts; *images=owner.images; *materials=owner.materials;
    } else if (heap.kind == 3) {
        frontend_native_q3_view owner; qa_error error={0};
        if (heap.ordinal>SIZE_MAX || !frontend_native_q3_read(f,(size_t)heap.ordinal,&owner,&error)) return false;
        *files=owner.mounts; *images=owner.images; *materials=owner.materials;
    } else return false;
    return *files && *images && *materials && qa_scene_resources_files(*images)==*files &&
        qa_material_library_resource_owner(*materials)==*images;
}
static bool heap_find(frontend_world_inventory *inventory,const qa_vfs *files,
    qa_scene_resources *images,qa_material_library *materials,root_heap *out,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    for (uint32_t kind=0;kind<4;++kind) {
        size_t count=kind==0?1:kind==1?frontend_source_group_count(f):
            kind==2?frontend_visual_owner_count(f):frontend_native_q3_count(f);
        for (size_t i=0;i<count;++i) {
            root_heap heap={kind,i,0}; const qa_vfs *actual_files=NULL;
            qa_scene_resources *actual_images=NULL; qa_material_library *actual_materials=NULL;
            if (!heap_read(f,heap,&actual_files,&actual_images,&actual_materials)) continue;
            if (actual_files==files && actual_images==images && actual_materials==materials) {
                heap.view=qa_application_content_view_id(qa_application_content_graph_read(f->application),files);
                if (!heap.view) break;
                *out=heap; return true;
            }
        }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Scene root has no genuine paired frontend resource and material owner");
}
static bool root_resource(frontend_world_inventory *inventory,const qa_vfs *files,
    const qa_resource *resource,uint64_t *pool,uint64_t *version,qa_error *error)
{
    if (!resource || !files || qa_resource_pool_find(qa_vfs_resources(files),qa_resource_id(resource))!=resource ||
        !qa_application_content_resource_id(qa_application_content_graph_read(inventory->frontend->application),resource,pool,version))
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene source resource leaves its genuine retained content pool");
    return true;
}
static bool policy_capture(world_row *row,qa_error *error)
{
    if (!qa_scene_world_options_read(row->source.world,&row->policy.options))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World policy cannot be observed under its actual owner lease");
    qa_bytes spans[]={row->policy.options.external_lit,row->policy.options.images.palette_rgb,row->policy.options.images.translation};
    for (size_t i=0;i<3;++i) {
        if (spans[i].size && !spans[i].data) return false;
        row->policy.spans[i]=spans[i];
    }
    if (row->policy.options.q2_sky) {
        size_t count=strlen(row->policy.options.q2_sky);
        if (count==SIZE_MAX || !(row->policy.sky=malloc(count+1)))
            return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual scene world sky policy");
        memcpy(row->policy.sky,row->policy.options.q2_sky,count+1);
    }
    return true;
}
static bool owner_shape(frontend_scene_owner owner,bool world)
{
    if (owner.kind==FRONTEND_SCENE_OWNER_FRONTEND) return world && !owner.owner && !owner.row;
    if (owner.kind==FRONTEND_SCENE_OWNER_SOURCE) return world && owner.owner && owner.row==1;
    return owner.owner && owner.row && (owner.kind==FRONTEND_SCENE_OWNER_Q3 ||
        owner.kind==FRONTEND_SCENE_OWNER_NATIVE_Q3 || owner.kind==FRONTEND_SCENE_OWNER_SELECTED_Q3 || (!world &&
        (owner.kind==FRONTEND_SCENE_OWNER_VISUAL ||
         (owner.kind==FRONTEND_SCENE_OWNER_EQUIPMENT && owner.row==1))));
}
static bool world_claim(frontend_world_inventory *inventory,const qa_scene_world *world,
    frontend_scene_owner owner,qa_error *error)
{
    for (size_t i=0;i<inventory->world_count;++i) if (inventory->worlds[i].source.world==world) {
        world_row *row=inventory->worlds+i;
        if (row->claimed) return frontend_fail(error,QA_ERROR_FORMAT,"World root has multiple actual destructor owners");
        row->owner=owner; row->claimed=true; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"World destructor references a root outside its capture lease");
}
static bool model_claim(frontend_world_inventory *inventory,const qa_scene_model *scene,
    frontend_scene_owner owner,const char *path,qa_error *error)
{
    for (size_t i=0;i<inventory->model_count;++i) if (inventory->models_roots[i].view.scene==scene) {
        model_row *row=inventory->models_roots+i;
        if (row->claimed) return frontend_fail(error,QA_ERROR_FORMAT,"Scene model root has multiple actual destructor owners");
        row->view.owner=owner; row->claimed=true;
        if (path) {
            size_t size=strlen(path);
            if (size==SIZE_MAX || !(row->path=malloc(size+1)))
                return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual visual cache path");
            memcpy(row->path,path,size+1); row->view.visual_path=row->path;
        }
        return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Scene destructor references a root outside its capture lease");
}
static bool owners_capture(frontend_world_inventory *inventory,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    if (f->scene_world && !world_claim(inventory,f->scene_world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_FRONTEND,0,0},error)) return false;
    for (size_t i=0;i<frontend_visual_owner_count(f);++i)
        for (size_t j=0;j<frontend_visual_model_count(f,i);++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(f,i,j,&model) || !model.scene ||
                !model_claim(inventory,model.scene,(frontend_scene_owner){FRONTEND_SCENE_OWNER_VISUAL,i+1,j+1},model.path,error)) return false;
        }
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f,i,&group)) return false;
        if (group.world && !world_claim(inventory,group.world,
            (frontend_scene_owner){FRONTEND_SCENE_OWNER_SOURCE,i+1,1},error)) return false;
        bool prior=false;
        for (size_t j=0;j<i;++j) {
            frontend_source_group_view earlier;
            if (!frontend_source_group_read(f,j,&earlier)) return false;
            if (earlier.assets==group.assets) { prior=true; break; }
        }
        if (prior) continue;
        size_t count=0;
        if (!qa_q3_assets_model_count(group.assets,&count,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(group.assets,j,&model,error)) return false;
            if (!model.present) continue;
            frontend_scene_owner owner={FRONTEND_SCENE_OWNER_Q3,i+1,j+1};
            if (model.owns_world && (!model.world || !world_claim(inventory,model.world,owner,error))) return false;
            for (unsigned k=0;k<3;++k) if (model.scenes[k]) {
                bool alias=false;
                for (unsigned p=0;p<k;++p) if (model.scenes[p]==model.scenes[k]) alias=true;
                if (!alias && !model_claim(inventory,model.scenes[k],owner,NULL,error)) return false;
            }
        }
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view native;
        if (!frontend_native_q3_read(f,i,&native,error) || !native.assets) return false;
        size_t count=0;
        if (!qa_q3_assets_model_count(native.assets,&count,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(native.assets,j,&model,error)) return false;
            if (!model.present) continue;
            frontend_scene_owner owner={FRONTEND_SCENE_OWNER_NATIVE_Q3,i+1,j+1};
            if (model.owns_world && (!model.world || !world_claim(inventory,model.world,owner,error))) return false;
            for (unsigned k=0;k<3;++k) if (model.scenes[k]) {
                bool alias=false;
                for (unsigned p=0;p<k;++p) if (model.scenes[p]==model.scenes[k]) alias=true;
                if (!alias && !model_claim(inventory,model.scenes[k],owner,NULL,error)) return false;
            }
        }
    }
    for (size_t i=0;i<frontend_equipment_q3_count(f);++i) {
        frontend_equipment_q3_owner_view selected;
        if (!frontend_equipment_q3_at(f,i,&selected,error) || !selected.assets) return false;
        size_t count=0;
        if (!qa_q3_assets_model_count(selected.assets,&count,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(selected.assets,j,&model,error)) return false;
            if (!model.present) continue;
            frontend_scene_owner owner={FRONTEND_SCENE_OWNER_SELECTED_Q3,i+1,j+1};
            if (model.owns_world && (!model.world || !world_claim(inventory,model.world,owner,error))) return false;
            for (unsigned k=0;k<3;++k) if (model.scenes[k]) {
                bool alias=false;
                for (unsigned p=0;p<k;++p) if (model.scenes[p]==model.scenes[k]) alias=true;
                if (!alias && !model_claim(inventory,model.scenes[k],owner,NULL,error)) return false;
            }
        }
    }
    for (size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view media;
        if (!frontend_equipment_media_at(f,i,&media) || !media.declaration) return false;
        if (media.declaration->none) continue;
        if (!media.held_scene || !model_claim(inventory,media.held_scene,
                (frontend_scene_owner){FRONTEND_SCENE_OWNER_EQUIPMENT,i+1,1},NULL,error)) return false;
    }
    return true;
}
static bool image_encode(void *context,const qa_scene_image *image,uint64_t *key,qa_error *error)
{ return frontend_scene_image_encode(((world_scope *)context)->inventory->space,image,key,error); }
static bool image_decode(void *context,uint64_t key,const qa_scene_image **image,qa_error *error)
{ return frontend_scene_image_decode(((world_scope *)context)->inventory->space,key,image,error); }
static bool material_encode(void *context,const qa_material *material,uint64_t *key,qa_error *error)
{ return frontend_scene_material_encode(((world_scope *)context)->inventory->space,material,key,error); }
static bool material_decode(void *context,uint64_t key,const qa_material **material,qa_error *error)
{ return frontend_scene_material_decode(((world_scope *)context)->inventory->space,key,material,error); }
static bool frame_encode(void *context,const qa_scene_frame *frame,uint64_t *key,qa_error *error)
{ return frontend_scene_frame_encode(((world_scope *)context)->inventory->space,frame,key,error); }
static bool frame_decode(void *context,uint64_t key,const qa_scene_frame **frame,qa_error *error)
{ return frontend_scene_frame_decode(((world_scope *)context)->inventory->space,key,frame,error); }
static bool geometry_encode(void *context,const qa_scene_geometry *geometry,uint64_t *key,qa_error *error)
{ return frontend_scene_geometry_encode(((world_scope *)context)->inventory->space,geometry,key,error); }
static bool geometry_decode(void *context,uint64_t key,const qa_scene_geometry **geometry,qa_error *error)
{ return frontend_scene_geometry_decode(((world_scope *)context)->inventory->space,key,geometry,error); }
static bool identity_decode(void *context,qa_scene_world_identity_kind kind,size_t ordinal,uint64_t saved,uint64_t *out,qa_error *error)
{ return frontend_scene_world_install(&((world_scope *)context)->identity,kind,ordinal,saved,out,error); }
static bool identity_encode(void *context,qa_scene_world_identity_kind kind,size_t ordinal,uint64_t actual,uint64_t *out,qa_error *error)
{ return frontend_scene_world_saved(&((world_scope *)context)->identity,kind,ordinal,actual,out,error); }
static bool world_source_qualify(void *context,qa_bytes bytes,const qa_scene_world_options *options,qa_error *error)
{
    world_scope *scope=context; const world_policy *policy=&scope->row->policy;
    qa_bytes source=qa_resource_bytes(scope->row->source.resource);
    const qa_scene_world_options *expected=&policy->options;
    const qa_scene_image_options *a=&options->images,*b=&expected->images;
    if (bytes.size!=source.size || (bytes.size && memcmp(bytes.data,source.data,bytes.size)) ||
        a->family!=b->family || a->wrap!=b->wrap || a->filter!=b->filter || a->usage!=b->usage ||
        a->mipmap!=b->mipmap || a->transparent!=b->transparent || a->fullbright_only!=b->fullbright_only ||
        a->transparent_index!=b->transparent_index || options->q3_overbright!=expected->q3_overbright ||
        options->q1_lightmap_encoding!=expected->q1_lightmap_encoding ||
        memcmp(&options->subdivisions,&expected->subdivisions,sizeof(float)) ||
        memcmp(&options->q1_water_alpha,&expected->q1_water_alpha,sizeof(float)) ||
        memcmp(&options->q2_light_modulate,&expected->q2_light_modulate,sizeof(float)) ||
        (options->q2_sky!=NULL)!=(policy->sky!=NULL) ||
        (options->q2_sky && strcmp(options->q2_sky,policy->sky)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene world source or actual constructor policy differs");
    qa_bytes spans[]={options->external_lit,a->palette_rgb,a->translation};
    for (size_t i=0;i<3;++i) {
        if (spans[i].size!=policy->spans[i].size || (spans[i].size && !spans[i].data) ||
            (spans[i].size && memcmp(spans[i].data,policy->spans[i].data,spans[i].size)))
            return frontend_fail(error,QA_ERROR_FORMAT,"World retained lighting or image policy bytes differ");
    }
    return true;
}
static qa_scene_world_owner_refs world_refs(world_scope *scope)
{
    return (qa_scene_world_owner_refs){.state={.images={scope,image_encode,image_decode},.context=scope,
        .material_encode=material_encode,.material_decode=material_decode,.frame_encode=frame_encode,.frame_decode=frame_decode},
        .context=scope,.geometry_encode=geometry_encode,.geometry_decode=geometry_decode,
        .source_qualify=world_source_qualify,.identity_decode=identity_decode,.identity_encode=identity_encode};
}
void frontend_world_inventory_destroy(frontend_world_inventory *inventory)
{
    if (!inventory) return;
    for (size_t i=0;i<inventory->model_count;++i) {
        model_row *row=inventory->models_roots+i;
        if (inventory->restoring && !row->adopted) qa_scene_model_destroy((qa_scene_model *)row->view.scene);
        free(row->path); qa_buffer_free(&row->captured);
    }
    for (size_t i=0;i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        if (inventory->restoring && !row->adopted) qa_scene_world_destroy((qa_scene_world *)row->source.world);
        free(row->policy.sky); qa_buffer_free(&row->captured);
    }
    free(inventory->worlds); free(inventory->models_roots); free(inventory);
}
static frontend_world_inventory *inventory_create(qa_frontend *f,frontend_model_inventory *models,
    frontend_scene_namespace *space,size_t worlds,size_t roots,bool restoring,qa_error *error)
{
    if (worlds>SIZE_MAX/sizeof(world_row) || roots>SIZE_MAX/sizeof(model_row)) {
        frontend_fail(error,QA_ERROR_MEMORY,"Scene root inventory exceeds address space"); return NULL;
    }
    frontend_world_inventory *inventory=calloc(1,sizeof(*inventory));
    if (!inventory) { frontend_fail(error,QA_ERROR_MEMORY,"Allocating actual scene root inventory"); return NULL; }
    inventory->frontend=f; inventory->models=models; inventory->space=space; inventory->restoring=restoring;
    inventory->worlds=worlds?calloc(worlds,sizeof(*inventory->worlds)):NULL;
    inventory->models_roots=roots?calloc(roots,sizeof(*inventory->models_roots)):NULL;
    if ((worlds && !inventory->worlds) || (roots && !inventory->models_roots)) {
        frontend_world_inventory_destroy(inventory);
        frontend_fail(error,QA_ERROR_MEMORY,"Allocating physical scene root rows"); return NULL;
    }
    inventory->world_count=worlds; inventory->model_count=roots; return inventory;
}
bool frontend_world_inventory_capture(qa_frontend *f,const frontend_scene_inventory *scenes,
    frontend_scene_namespace *space,frontend_world_inventory **out,qa_error *error)
{
    if (!f || !f->application || !f->capture || f->stepping || f->source_restoring || !scenes || !space || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene root capture requires its actual retained frontend graph");
    frontend_model_inventory *models=frontend_scene_inventory_models(scenes);
    frontend_world_inventory *inventory=inventory_create(f,models,space,frontend_scene_inventory_world_count(scenes),
        frontend_scene_inventory_model_count(scenes),false,error);
    if (!inventory) return false;
    bool ok=true;
    for (size_t i=0;ok && i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        ok=frontend_scene_inventory_world_at(scenes,i,&row->source) &&
            heap_find(inventory,row->source.files,row->source.images,row->source.materials,&row->heap,error) &&
            root_resource(inventory,row->source.files,row->source.resource,&row->pool,&row->resource,error) && policy_capture(row,error);
    }
    for (size_t i=0;ok && i<inventory->model_count;++i) {
        model_row *row=inventory->models_roots+i;
        row->view.scene=frontend_scene_inventory_model_at(scenes,i);
        const qa_model *source=qa_scene_model_source(row->view.scene);
        bool found=false;
        for (size_t j=0;j<frontend_model_count(models);++j)
            if (frontend_model_source_at(models,j,&row->view.source) && row->view.source.model==source) { found=true; break; }
        ok=found && heap_find(inventory,row->view.source.files,qa_scene_model_resource_owner(row->view.scene),
            qa_scene_model_material_owner(row->view.scene),&row->heap,error) &&
            root_resource(inventory,row->view.source.files,row->view.source.resource,&row->pool,&row->resource,error) &&
            frontend_model_encode(models,source,&row->model,error);
    }
    ok=ok && owners_capture(inventory,error);
    for (size_t i=0;ok && i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i; world_scope scope={inventory,row,{space,i+1}};
        qa_scene_world_owner_refs refs=world_refs(&scope);
        ok=row->claimed && owner_shape(row->owner,true) && qa_scene_world_owner_checkpoint(row->source.world,&refs,&row->captured,error);
        row->state=(qa_bytes){row->captured.data,row->captured.size};
    }
    for (size_t i=0;ok && i<inventory->model_count;++i) {
        model_row *row=inventory->models_roots+i; frontend_scene_model_scope scope={{space,i+1},models};
        qa_scene_model_owner_refs refs=frontend_scene_model_refs(&scope);
        ok=row->claimed && owner_shape(row->view.owner,false) && qa_scene_model_owner_checkpoint(row->view.scene,&refs,&row->captured,error);
        row->state=(qa_bytes){row->captured.data,row->captured.size};
    }
    if (!ok) {
        frontend_world_inventory_destroy(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Scene root graph has incomplete actual ownership or source provenance");
        return false;
    }
    *out=inventory; return true;
}
static bool owner_fields(qa_source_save_io *io,frontend_scene_owner *owner,bool world)
{
    uint32_t kind=owner->kind;
    if (!qa_source_save_u32(io,&kind) || kind>FRONTEND_SCENE_OWNER_SOURCE ||
        !qa_source_save_u64(io,&owner->owner) || !qa_source_save_u64(io,&owner->row)) return false;
    owner->kind=(frontend_scene_owner_kind)kind; return owner_shape(*owner,world);
}
static bool heap_fields(qa_source_save_io *io,frontend_world_inventory *inventory,root_heap *heap,
    uint64_t *pool,uint64_t *version,const qa_vfs **files,qa_scene_resources **images,
    qa_material_library **materials,const qa_resource **resource)
{
    if (!qa_source_save_u32(io,&heap->kind) || !qa_source_save_u64(io,&heap->ordinal) ||
        !qa_source_save_u64(io,&heap->view) || !qa_source_save_u64(io,pool) || !qa_source_save_u64(io,version) ||
        !heap_read(inventory->frontend,*heap,files,images,materials)) return false;
    qa_application_content_graph *graph=qa_application_content_graph_read(inventory->frontend->application);
    if (!heap->view || qa_application_content_view_id(graph,*files)!=heap->view) return false;
    const qa_resource *actual=qa_application_content_resource(graph,*pool,*version);
    if (!actual || qa_resource_pool_find(qa_vfs_resources(*files),qa_resource_id(actual))!=actual ||
        (io->direction==QA_SOURCE_SAVE_WRITE && actual!=*resource)) return false;
    *resource=actual; return true;
}
static bool policy_fields(qa_source_save_io *io,world_policy *policy)
{
    qa_scene_world_options *o=&policy->options; qa_scene_image_options *image=&o->images;
    uint32_t family=image->family,wrap=image->wrap,filter=image->filter,usage=image->usage,encoding=o->q1_lightmap_encoding;
    int32_t transparent_index=image->transparent_index;
    if (!qa_source_save_u32(io,&family) || family>QA_SCENE_Q3 || !qa_source_save_u32(io,&wrap) || wrap>QA_SCENE_CLAMP ||
        !qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR || !qa_source_save_u32(io,&usage) || usage>QA_IMAGE_USAGE_SKY ||
        !qa_source_save_i32(io,&transparent_index) || !qa_source_save_bool(io,&image->mipmap) ||
        !qa_source_save_bool(io,&image->transparent) || !qa_source_save_bool(io,&image->fullbright_only) ||
        !qa_source_save_f32(io,&o->subdivisions) || !qa_source_save_f32(io,&o->q1_water_alpha) ||
        !qa_source_save_f32(io,&o->q2_light_modulate) || !qa_source_save_u32(io,&o->q3_overbright) ||
        !qa_source_save_u32(io,&encoding) || !frontend_save_text(io,&policy->sky)) return false;
    image->family=(qa_scene_family)family; image->wrap=(qa_scene_wrap)wrap;
    image->filter=(qa_scene_filter)filter; image->usage=(qa_scene_image_usage)usage;
    image->transparent_index=transparent_index; o->q1_lightmap_encoding=(qa_scene_q1_lightmap_encoding)encoding;
    for (size_t i=0;i<3;++i) {
        size_t count=policy->spans[i].size;
        if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
        if (io->direction==QA_SOURCE_SAVE_WRITE) {
            if (!qa_source_save_bytes(io,(void *)policy->spans[i].data,count)) return false;
        } else {
            if (count>io->input.size-io->offset) return false;
            policy->spans[i]=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count;
        }
    }
    return true;
}
static bool blob(qa_source_save_io *io,qa_bytes *state)
{
    size_t count=state->size;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)state->data,count);
    if (count>io->input.size-io->offset) return false;
    *state=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return count!=0;
}
static bool rows_fields(qa_source_save_io *io,frontend_world_inventory *inventory)
{
    for (size_t i=0;i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        if (!owner_fields(io,&row->owner,true) || !heap_fields(io,inventory,&row->heap,&row->pool,&row->resource,
            &row->source.files,&row->source.images,&row->source.materials,&row->source.resource) ||
            !policy_fields(io,&row->policy) || !blob(io,&row->state)) return false;
        for (size_t j=0;j<i;++j)
            if (row->owner.kind==inventory->worlds[j].owner.kind && row->owner.owner==inventory->worlds[j].owner.owner &&
                row->owner.row==inventory->worlds[j].owner.row) return false;
    }
    for (size_t i=0;i<inventory->model_count;++i) {
        model_row *row=inventory->models_roots+i; qa_scene_resources *images=NULL; qa_material_library *materials=NULL;
        if (!owner_fields(io,&row->view.owner,false) || !heap_fields(io,inventory,&row->heap,&row->pool,&row->resource,
            &row->view.source.files,&images,&materials,&row->view.source.resource) || !qa_source_save_u64(io,&row->model) ||
            !frontend_save_text(io,&row->path) ||
            (row->view.owner.kind==FRONTEND_SCENE_OWNER_VISUAL ? !row->path : row->path!=NULL) || !blob(io,&row->state)) return false;
        row->view.visual_path=row->path;
        if (io->direction==QA_SOURCE_SAVE_READ && !frontend_model_decode(inventory->models,row->model,
            qa_resource_bytes(row->view.source.resource),&row->view.source.model,io->error)) return false;
        frontend_model_source actual={0}; bool found=false;
        for (size_t j=0;j<frontend_model_count(inventory->models);++j)
            if (frontend_model_source_at(inventory->models,j,&actual) && actual.model==row->view.source.model) { found=true; break; }
        if (!found || actual.files!=row->view.source.files || actual.resource!=row->view.source.resource) return false;
    }
    return true;
}
static bool header(qa_source_save_io *io,size_t *worlds,size_t *models)
{
    uint8_t magic[4]={'Q','F','W','R'}; uint32_t version=4;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFWR",4) && qa_source_save_u32(io,&version) && version==4 &&
        qa_source_save_count(io,worlds,maximum) && qa_source_save_count(io,models,maximum);
}
bool frontend_world_inventory_checkpoint(const frontend_world_inventory *inventory,qa_buffer *out,qa_error *error)
{
    if (!inventory || inventory->restoring || !inventory->frontend->capture || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World root checkpoint requires its held real capture and empty output");
    qa_source_save_io io={0}; size_t worlds=inventory->world_count,models=inventory->model_count;
    bool ok=qa_source_save_writer(&io,NULL,error) && header(&io,&worlds,&models) &&
        rows_fields(&io,(frontend_world_inventory *)inventory) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Scene root envelope has invalid genuine owner metadata");
    return ok;
}
bool frontend_world_inventory_restore(qa_frontend *f,frontend_model_inventory *models,frontend_scene_namespace *space,
    qa_bytes bytes,frontend_world_inventory **out,qa_error *error)
{
    if (!f || !f->application || !f->source_restoring || f->stepping || f->capture || !models || !space || !out || *out ||
        !qa_application_content_graph_read(f->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene root import requires its isolated actual frontend and content owners");
    qa_source_save_io io={0}; size_t world_count=0,model_count=0;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && header(&io,&world_count,&model_count);
    frontend_world_inventory *inventory=ok?inventory_create(f,models,space,world_count,model_count,true,error):NULL;
    ok=ok && inventory && rows_fields(&io,inventory) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for (size_t i=0;ok && i<world_count;++i) {
        world_row *row=inventory->worlds+i; qa_bsp_view bsp={0}; qa_scene_world *world=NULL;
        world_scope scope={inventory,row,{space,i+1}}; qa_scene_world_owner_refs refs=world_refs(&scope);
        ok=qa_bsp_open(qa_resource_bytes(row->source.resource),&bsp,error) &&
            qa_scene_world_owner_restore(&bsp,row->source.images,row->source.materials,row->state,&refs,&world,error);
        row->source.world=world;
        ok=ok && frontend_scene_namespace_qualify_world(space,i+1,world,error);
        row->state=(qa_bytes){0};
    }
    for (size_t i=0;ok && i<model_count;++i) {
        model_row *row=inventory->models_roots+i; const qa_vfs *files=NULL;
        qa_scene_resources *images=NULL; qa_material_library *materials=NULL; qa_scene_model *scene=NULL;
        frontend_scene_model_scope scope={{space,i+1},models}; qa_scene_model_owner_refs refs=frontend_scene_model_refs(&scope);
        ok=heap_read(f,row->heap,&files,&images,&materials) &&
            qa_scene_model_owner_restore(row->view.source.model,images,materials,row->state,&refs,&scene,error);
        row->view.scene=scene;
        ok=ok && frontend_scene_namespace_qualify_model(space,i+1,scene,error);
        row->state=(qa_bytes){0};
    }
    if (!ok) {
        frontend_world_inventory_destroy(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved scene root graph differs from the genuine prepared owners");
        return false;
    }
    *out=inventory; return true;
}
size_t frontend_world_inventory_world_count(const frontend_world_inventory *inventory)
{ return inventory?inventory->world_count:0; }
size_t frontend_world_inventory_model_count(const frontend_world_inventory *inventory)
{ return inventory?inventory->model_count:0; }
bool frontend_world_inventory_world_at(const frontend_world_inventory *inventory,size_t ordinal,
    frontend_world_source *source,frontend_scene_owner *owner)
{
    if (!inventory || ordinal>=inventory->world_count || !source || !owner) return false;
    *source=inventory->worlds[ordinal].source; *owner=inventory->worlds[ordinal].owner; return true;
}
bool frontend_world_inventory_model_at(const frontend_world_inventory *inventory,size_t ordinal,frontend_scene_root_view *view)
{
    if (!inventory || ordinal>=inventory->model_count || !view) return false;
    *view=inventory->models_roots[ordinal].view; return true;
}
bool frontend_world_encode(void *context,const qa_scene_world *world,uint64_t *key,qa_error *error)
{
    frontend_world_inventory *inventory=context;
    for (size_t i=0;inventory && key && world && i<inventory->world_count;++i)
        if (inventory->worlds[i].source.world==world) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"World reference is outside its genuine physical root inventory");
}
bool frontend_world_decode(void *context,uint64_t key,qa_scene_world **world,qa_error *error)
{
    frontend_world_inventory *inventory=context;
    if (!inventory || !world || !key || key>inventory->world_count || !inventory->worlds[key-1].source.world)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved world reference has no actual imported root");
    *world=(qa_scene_world *)inventory->worlds[key-1].source.world; return true;
}
bool frontend_scene_root_encode(void *context,const qa_scene_model *model,uint64_t *key,qa_error *error)
{
    frontend_world_inventory *inventory=context;
    for (size_t i=0;inventory && key && model && i<inventory->model_count;++i)
        if (inventory->models_roots[i].view.scene==model) { *key=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Scene model reference is outside its genuine physical root inventory");
}
bool frontend_scene_root_decode(void *context,uint64_t key,qa_scene_model **model,qa_error *error)
{
    frontend_world_inventory *inventory=context;
    if (!inventory || !model || !key || key>inventory->model_count || !inventory->models_roots[key-1].view.scene)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved scene model reference has no actual imported root");
    *model=(qa_scene_model *)inventory->models_roots[key-1].view.scene; return true;
}
bool frontend_world_owner_ready(const frontend_world_inventory *inventory,uint64_t key,
    frontend_scene_owner_kind kind,uint64_t owner,qa_error *error)
{
    if (!inventory || !key || key>inventory->world_count || !inventory->worlds[key-1].source.world ||
        inventory->worlds[key-1].adopted || inventory->worlds[key-1].owner.kind!=kind || inventory->worlds[key-1].owner.owner!=owner)
        return frontend_fail(error,QA_ERROR_FORMAT,"World ownership transfer differs from its genuine destructor scope");
    return true;
}
bool frontend_scene_root_owner_ready(const frontend_world_inventory *inventory,uint64_t key,
    frontend_scene_owner_kind kind,uint64_t owner,qa_error *error)
{
    if (!inventory || !key || key>inventory->model_count || !inventory->models_roots[key-1].view.scene ||
        inventory->models_roots[key-1].adopted || inventory->models_roots[key-1].view.owner.kind!=kind ||
        inventory->models_roots[key-1].view.owner.owner!=owner)
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene model ownership transfer differs from its genuine destructor scope");
    return true;
}
void frontend_world_adopt(void *context,uint64_t key)
{
    frontend_world_inventory *inventory=context;
    if (inventory && key && key<=inventory->world_count) inventory->worlds[key-1].adopted=true;
}
void frontend_scene_root_adopt(void *context,uint64_t key)
{
    frontend_world_inventory *inventory=context;
    if (inventory && key && key<=inventory->model_count) inventory->models_roots[key-1].adopted=true;
}
bool frontend_world_inventory_ready(const frontend_world_inventory *inventory,qa_error *error)
{
    if (!inventory || !inventory->restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene adoption readiness requires its actual imported root inventory");
    for (size_t i=0;i<inventory->world_count;++i)
        if (!inventory->worlds[i].adopted) return frontend_fail(error,QA_ERROR_FORMAT,"Imported world lacks its actual destructor consumer");
    for (size_t i=0;i<inventory->model_count;++i)
        if (!inventory->models_roots[i].adopted) return frontend_fail(error,QA_ERROR_FORMAT,"Imported scene model lacks its actual destructor consumer");
    return true;
}
bool frontend_source_roots_attach_restored(qa_frontend *f,frontend_world_inventory *inventory,qa_error *error)
{
    if (!f || !f->application || f->stepping || f->capture || !f->source_restoring ||
        !inventory || !inventory->restoring || inventory->frontend!=f)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source map adoption requires its actual candidate root inventory");
    size_t groups=frontend_source_group_count(f);
    for (size_t i=0;i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        if (row->owner.kind==FRONTEND_SCENE_OWNER_SOURCE &&
            (!row->owner.owner || row->owner.owner>groups || row->owner.row!=1))
            return frontend_fail(error,QA_ERROR_FORMAT,"Source map root leaves its physical group destructor");
    }
    /* Every root and map-presence edge qualifies before the first nofail
     * transfer, so a rejected group leaves all roots dictionary-owned. */
    for (size_t group=0;group<groups;++group) {
        frontend_source_group_view actual; size_t key=0;
        if (!frontend_source_group_read(f,group,&actual) || actual.world ||
            (!actual.map_resource!=!actual.geometry))
            return frontend_fail(error,QA_ERROR_FORMAT,"Source map group is not its prepared collision owner");
        for (size_t i=0;i<inventory->world_count;++i) {
            world_row *row=inventory->worlds+i;
            if (row->owner.kind!=FRONTEND_SCENE_OWNER_SOURCE || row->owner.owner!=group+1) continue;
            if (key || row->owner.row!=1 || row->source.resource!=actual.map_resource ||
                row->source.files!=actual.mounts || row->source.images!=actual.images ||
                row->source.materials!=actual.materials ||
                !frontend_world_owner_ready(inventory,i+1,FRONTEND_SCENE_OWNER_SOURCE,group+1,error) ||
                !frontend_source_world_adopt_ready(f,group,(qa_scene_world *)row->source.world,error))
                return frontend_fail(error,QA_ERROR_FORMAT,"Source map root differs from its retained resource and paired heaps");
            key=i+1;
        }
        if ((!actual.map_resource)!=(!key))
            return frontend_fail(error,QA_ERROR_FORMAT,"Source map presence differs from its genuine root inventory");
    }
    for (size_t i=0;i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        if (row->owner.kind!=FRONTEND_SCENE_OWNER_SOURCE) continue;
        frontend_source_world_adopt(f,(size_t)row->owner.owner-1,(qa_scene_world *)row->source.world);
        frontend_world_adopt(inventory,i+1);
    }
    return true;
}
