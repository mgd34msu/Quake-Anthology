#include "world_inventory.h"
#include "component_scene.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "equipment_media.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "selected_character.h"
#include "selected_effects.h"
#include "source_restore.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q1_restore.h"
#include "remote_q2_restore.h"
#include "save_private.h"
#include "renderer_materials.h"
#include "renderer_worlds.h"
#include "renderer_registries.h"
#include "unified_media_inventory.h"
#include "remote_unified_media_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"

typedef frontend_scene_heap root_heap;
typedef struct world_policy {
    qa_scene_world_options options;
    char *sky;
    qa_bytes spans[4];
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
    bool restoring, frames_restored;
};
typedef struct world_scope {
    frontend_world_inventory *inventory;
    world_row *row;
    frontend_scene_identity_scope identity;
} world_scope;

bool frontend_scene_heap_read(const qa_frontend *f, root_heap heap, const qa_vfs **files,
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
    } else if(heap.kind==4) {
        frontend_remote_q3_resources owner; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !(f->resource_inventory?
            frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,(size_t)heap.ordinal),&owner,&error):
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,(size_t)heap.ordinal),&owner,&error))) return false;
        *files=owner.mounts; *images=owner.images; *materials=owner.materials;
    } else if(heap.kind==5) {
        frontend_remote_q3_initial_view owner; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !(f->resource_inventory?
            frontend_remote_q3_initial_metadata_read(frontend_remote_q3_initial_at(f,(size_t)heap.ordinal),&owner,&error):
            frontend_remote_q3_initial_read(frontend_remote_q3_initial_at(f,(size_t)heap.ordinal),&owner,&error))) return false;
        *files=owner.mounts; *images=owner.images; *materials=owner.materials;
    } else if(heap.kind==6) {
        frontend_remote_q1_view owner; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !(f->source_restoring?
            frontend_remote_q1_import_read(frontend_remote_q1_at(f,(size_t)heap.ordinal),&owner,&error):
            frontend_remote_q1_metadata_read(frontend_remote_q1_at(f,(size_t)heap.ordinal),&owner,&error))) return false;
        *files=owner.content.mounts; *images=owner.images; *materials=owner.materials;
    } else if(heap.kind==7) {
        frontend_remote_q2_view owner; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !(f->source_restoring?
            frontend_remote_q2_import_read(frontend_remote_q2_at(f,(size_t)heap.ordinal),&owner,&error):
            frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,(size_t)heap.ordinal),&owner,&error))) return false;
        *files=owner.content.mounts; *images=owner.images; *materials=owner.materials;
    } else if(heap.kind==8) {
        frontend_renderer_materials_view row; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !frontend_renderer_materials_read_at(f,(size_t)heap.ordinal,&row,&error)) return false;
        *files=row.mounts; *images=row.images; *materials=row.library;
    } else if(heap.kind==9) {
        frontend_renderer_worlds_view row; qa_error error={0};
        if(heap.ordinal>SIZE_MAX || !frontend_renderer_worlds_read_at(f,(size_t)heap.ordinal,&row,&error) || !row.private_heaps) return false;
        *files=row.files; *images=row.images; *materials=row.materials;
    } else if(heap.kind==10) {
        size_t media_ordinal,bank_ordinal; frontend_unified_media *media=NULL;
        frontend_unified_bank_view bank;
        if(!frontend_unified_media_bank_key_read(heap.ordinal,&media_ordinal,&bank_ordinal) ||
            !frontend_unified_media_inventory_at(f,media_ordinal,&media,NULL) || !media ||
            !frontend_unified_media_bank_read(media,bank_ordinal,&bank)) return false;
        *files=bank.files; *images=bank.images; *materials=bank.materials;
    } else if(heap.kind==11) {
        frontend_component_scene_view row;
        if(heap.ordinal>SIZE_MAX || !frontend_component_scene_metadata_read(f,(size_t)heap.ordinal,&row,NULL)) return false;
        *files=row.files; *images=row.images; *materials=row.materials;
    } else if(heap.kind==12) {
        frontend_equipment_media_view row;
        if(heap.ordinal>SIZE_MAX || !frontend_equipment_media_at(f,(size_t)heap.ordinal,&row) || !row.source_slot) return false;
        *files=row.owner.mounts; *images=row.owner.images; *materials=row.owner.materials;
    } else return false;
    return *files && *images && *materials && qa_scene_resources_files(*images)==*files &&
        qa_material_library_resource_owner(*materials)==*images;
}
bool frontend_scene_heap_find(const qa_frontend *f,const qa_vfs *files,
    qa_scene_resources *images,qa_material_library *materials,root_heap *out,bool *found,qa_error *error)
{
    if(!f || !out || !found) return false;
    *found=false;
    size_t retained_count=0;
    if(!frontend_renderer_materials_count(f,&retained_count,error)) return false;
    for (uint32_t kind=0;kind<9;++kind) {
        size_t count=kind==0?1:kind==1?frontend_source_group_count(f):
            kind==2?frontend_visual_owner_count(f):kind==3?frontend_native_q3_count(f):
            kind==4?frontend_remote_q3_count(f):kind==5?frontend_remote_q3_initial_count(f):
            kind==6?frontend_remote_q1_count(f):kind==7?frontend_remote_q2_count(f):retained_count;
        for (size_t i=0;i<count;++i) {
            root_heap heap={kind,i,0}; const qa_vfs *actual_files=NULL;
            qa_scene_resources *actual_images=NULL; qa_material_library *actual_materials=NULL;
            if (!frontend_scene_heap_read(f,heap,&actual_files,&actual_images,&actual_materials)) continue;
            if (actual_files==files && actual_images==images && actual_materials==materials) {
                qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
                heap.view=graph?qa_application_content_view_id(graph,files):0;
                if (graph && !heap.view) return frontend_fail(error,QA_ERROR_FORMAT,"Scene paired bank view is outside its actual content graph");
                *out=heap; *found=true; return true;
            }
        }
    }
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        for(size_t j=0;media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank; uint64_t key;
            if(!frontend_unified_media_bank_read(media,j,&bank) || !frontend_unified_media_bank_key(i,j,&key)) return false;
            if(bank.files==files && bank.images==images && bank.materials==materials) {
                qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
                uint64_t view=graph?qa_application_content_view_id(graph,files):0;
                if(graph && !view) return false;
                *out=(root_heap){10,key,view}; *found=true; return true;
            }
        }
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error)) return false;
        if(row.files==files && row.images==images && row.materials==materials) {
            qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
            uint64_t view=graph?qa_application_content_view_id(graph,files):0;
            if(graph && !view) return false;
            *out=(root_heap){11,i,view}; *found=true; return true;
        }
    }
    for(size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view row;
        if(!frontend_equipment_media_at(f,i,&row)) return false;
        if(row.source_slot && row.owner.mounts==files && row.owner.images==images && row.owner.materials==materials) {
            qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
            uint64_t view=graph?qa_application_content_view_id(graph,files):0;
            if(graph && !view) return false;
            *out=(root_heap){12,i,view}; *found=true; return true;
        }
    }
    return true;
}
static bool heap_find(frontend_world_inventory *inventory,const qa_vfs *files,
    qa_scene_resources *images,qa_material_library *materials,root_heap *out,qa_error *error)
{
    bool found=false;
    if(!frontend_scene_heap_find(inventory->frontend,files,images,materials,out,&found,error)) return false;
    if(found) return true;
    size_t count=0;
    if(!frontend_renderer_worlds_count(inventory->frontend,&count,error)) return false;
    uint64_t view=qa_application_content_view_id(qa_application_content_graph_read(inventory->frontend->application),files);
    for(size_t i=0;view && i<count;++i) {
        frontend_renderer_worlds_view row;
        if(!frontend_renderer_worlds_read_at(inventory->frontend,i,&row,error)) return false;
        if(row.private_heaps && row.files==files && row.images==images && row.materials==materials) {
            *out=(root_heap){9,i,view}; return true;
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
    qa_bytes spans[]={row->policy.options.external_lit,row->policy.options.images.palette_rgb,
        row->policy.options.images.translation,row->policy.options.external_entities};
    for (size_t i=0;i<4;++i) {
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
    if (owner.kind==FRONTEND_SCENE_OWNER_VISUAL) return owner.owner && owner.row;
    if (owner.kind==FRONTEND_SCENE_OWNER_RENDERER) return world && owner.owner && owner.row==1;
    if(owner.kind==FRONTEND_SCENE_OWNER_UNIFIED_MAP) return world && owner.owner && owner.row==1;
    if(owner.kind==FRONTEND_SCENE_OWNER_UNIFIED_MODEL) return owner.owner && owner.row;
    if(owner.kind==FRONTEND_SCENE_OWNER_UNIFIED_Q3) return owner.owner && (owner.row>>32) && (uint32_t)owner.row;
    if(owner.kind==FRONTEND_SCENE_OWNER_COMPONENT || owner.kind==FRONTEND_SCENE_OWNER_REGISTRY) return owner.owner && owner.row;
    if (owner.kind==FRONTEND_SCENE_OWNER_SOURCE) return world && owner.owner && owner.row==1;
    if (owner.kind==FRONTEND_SCENE_OWNER_REMOTE_MAP || owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q2_MAP)
        return world && owner.owner && owner.row==1;
    if(owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP) return world && owner.owner && owner.row;
    return owner.owner && owner.row && (owner.kind==FRONTEND_SCENE_OWNER_Q3 ||
        owner.kind==FRONTEND_SCENE_OWNER_NATIVE_Q3 || owner.kind==FRONTEND_SCENE_OWNER_SELECTED_Q3 ||
        owner.kind==FRONTEND_SCENE_OWNER_CHARACTER || owner.kind==FRONTEND_SCENE_OWNER_EFFECTS ||
        owner.kind==FRONTEND_SCENE_OWNER_GEAR || owner.kind==FRONTEND_SCENE_OWNER_REMOTE ||
        owner.kind==FRONTEND_SCENE_OWNER_INITIAL || (!world &&
        (owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q2 ||
         owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q1 ||
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
static bool registry_model_claim(frontend_world_inventory *inventory,
    const qa_q3_asset_model_holder *model,frontend_scene_owner owner,qa_error *error)
{
    if(model->owns_world && (!model->world || !world_claim(inventory,model->world,owner,error))) return false;
    for(unsigned j=0;j<3;++j) if(model->scenes[j]) {
        bool alias=false;
        for(unsigned k=0;k<j;++k) if(model->scenes[k]==model->scenes[j]) alias=true;
        if(!alias && !model_claim(inventory,model->scenes[j],owner,NULL,error)) return false;
    }
    return !model->source_md4_scene || model_claim(inventory,model->source_md4_scene,owner,NULL,error);
}
static bool registry_roots_claim(frontend_world_inventory *inventory,qa_q3_presentation_assets *assets,
    frontend_scene_owner_kind kind,uint64_t ordinal,qa_error *error)
{
    size_t count=0;
    if(!assets || !qa_q3_assets_model_count(assets,&count,error)) return false;
    for(size_t i=0;i<count;++i) {
        qa_q3_asset_model_holder model;
        if(!qa_q3_assets_model_holder(assets,i,&model,error)) return false;
        if(!model.present || model.shared_parent) continue;
        frontend_scene_owner owner={kind,ordinal,i+1};
        if(!registry_model_claim(inventory,&model,owner,error)) return false;
    }
    return true;
}
static bool retained_registry_roots_claim(frontend_world_inventory *inventory,
    qa_q3_presentation_assets *assets,uint64_t ordinal,qa_error *error)
{
    size_t count=0;
    if(!assets || !qa_q3_assets_model_count(assets,&count,error)) return false;
    for(size_t i=0;i<count;++i) {
        qa_q3_asset_model_holder model;
        if(!qa_q3_assets_model_holder(assets,i,&model,error)) return false;
        if(!model.present || model.shared_parent) continue;
        frontend_scene_owner owner={FRONTEND_SCENE_OWNER_REGISTRY,ordinal,i+1};
        if(model.owns_world) {
            bool found=false;
            for(size_t n=0;n<inventory->world_count;++n) if(inventory->worlds[n].source.world==model.world) {
                found=true;
                if(!inventory->worlds[n].claimed && !world_claim(inventory,model.world,owner,error)) return false;
            }
            if(!found) return frontend_fail(error,QA_ERROR_FORMAT,"Retained registry world is outside actual captured roots");
        }
        const qa_scene_model *scenes[4]={model.scenes[0],model.scenes[1],model.scenes[2],model.source_md4_scene};
        for(size_t j=0;j<4;++j) if(scenes[j]) {
            bool found=false;
            for(size_t n=0;n<inventory->model_count;++n) if(inventory->models_roots[n].view.scene==scenes[j]) {
                found=true;
                if(!inventory->models_roots[n].claimed && !model_claim(inventory,scenes[j],owner,NULL,error)) return false;
            }
            if(!found) return frontend_fail(error,QA_ERROR_FORMAT,"Retained registry scene is outside actual captured roots");
        }
    }
    return true;
}
static bool owners_capture(frontend_world_inventory *inventory,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    if (f->scene_world && !world_claim(inventory,f->scene_world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_FRONTEND,0,0},error)) return false;
    for (size_t i=0;i<frontend_visual_owner_count(f);++i) {
        for (size_t j=0;j<frontend_visual_brush_count(f,i);++j) {
            frontend_visual_brush_view brush;
            if (!frontend_visual_brush_read(f,i,j,&brush) ||
                !world_claim(inventory,brush.world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_VISUAL,i+1,j+1},error)) return false;
        }
        for (size_t j=0;j<frontend_visual_model_count(f,i);++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(f,i,j,&model) || !model.scene ||
                !model_claim(inventory,model.scene,(frontend_scene_owner){FRONTEND_SCENE_OWNER_VISUAL,i+1,j+1},model.path,error)) return false;
        }
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
        if (!registry_roots_claim(inventory,group.assets,FRONTEND_SCENE_OWNER_Q3,i+1,error)) return false;
    }
    for (size_t i=0;i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view native;
        if (!frontend_native_q3_read(f,i,&native,error) || !native.assets) return false;
        if (!registry_roots_claim(inventory,native.assets,FRONTEND_SCENE_OWNER_NATIVE_Q3,i+1,error)) return false;
    }
    for (size_t i=0;i<frontend_equipment_q3_count(f);++i) {
        frontend_equipment_q3_owner_view selected;
        if (!frontend_equipment_q3_at(f,i,&selected,error) || !selected.assets) return false;
        if (!registry_roots_claim(inventory,selected.assets,FRONTEND_SCENE_OWNER_SELECTED_Q3,i+1,error)) return false;
    }
    for (size_t i=0;i<frontend_selected_character_count(f);++i) {
        frontend_selected_character_view character;
        if (!frontend_selected_character_at(f,i,&character,error) || !character.assets) return false;
        if (!registry_roots_claim(inventory,character.assets,FRONTEND_SCENE_OWNER_CHARACTER,i+1,error)) return false;
    }
    for (size_t i=0;i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view effects;
        if (!frontend_selected_effects_at(f,i,&effects,error) || !effects.assets) return false;
        if (!registry_roots_claim(inventory,effects.assets,FRONTEND_SCENE_OWNER_EFFECTS,i+1,error)) return false;
    }
    for (size_t i=0;i<frontend_equipment_gear_count(f);++i) {
        frontend_equipment_gear_owner_view gear;
        if (!frontend_equipment_gear_at(f,i,&gear,error) || !gear.assets) return false;
        if (!registry_roots_claim(inventory,gear.assets,FRONTEND_SCENE_OWNER_GEAR,i+1,error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        if(!frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) || !owner.world ||
            !world_claim(inventory,owner.world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_MAP,i+1,1},error) ||
            !registry_roots_claim(inventory,owner.assets,FRONTEND_SCENE_OWNER_REMOTE,i+1,error)) return false;
    }
    frontend_network_initial_graph_view initial;
    if(!frontend_network_initial_graph_read(f,&initial,error)) return false;
    if(initial.present) {
        frontend_remote_q3_initial_view owner;
        if(!initial.parent || !frontend_remote_q3_initial_read(initial.parent,&owner,error) ||
            !registry_roots_claim(inventory,owner.assets,FRONTEND_SCENE_OWNER_INITIAL,1,error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *owner=frontend_remote_q1_at(f,i); frontend_remote_q1_media media;
        if(!frontend_remote_q1_media_read(owner,&media,error) || (media.world &&
            !world_claim(inventory,media.world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP,i+1,1},error))) return false;
        for(size_t j=0;j<frontend_remote_q1_model_count(owner);++j) {
            frontend_remote_q1_model_view model;
            if(!frontend_remote_q1_model_at(owner,j,&model,error) ||
                (model.world? !world_claim(inventory,model.world,
                    (frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP,i+1,j+2},error):
                    !model_claim(inventory,model.scene,
                        (frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_Q1,i+1,j+1},model.path,error))) return false;
        }
    }
    for(size_t i=0;i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2 *owner=frontend_remote_q2_at(f,i); frontend_remote_q2_view media;
        if(!frontend_remote_q2_metadata_read(owner,&media,error) || (media.world &&
            !world_claim(inventory,media.world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_Q2_MAP,i+1,1},error))) return false;
        for(size_t j=0;j<frontend_remote_q2_model_count(owner);++j) {
            frontend_remote_q2_model_view model;
            if(!frontend_remote_q2_model_at(owner,j,&model,error) ||
                !model_claim(inventory,model.scene,
                    (frontend_scene_owner){FRONTEND_SCENE_OWNER_REMOTE_Q2,i+1,j+1},model.path,error)) return false;
        }
    }
    for (size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view media;
        if (!frontend_equipment_media_at(f,i,&media) || !media.declaration) return false;
        if (media.declaration->none) continue;
        if (!media.held_scene || !model_claim(inventory,media.held_scene,
                (frontend_scene_owner){FRONTEND_SCENE_OWNER_EQUIPMENT,i+1,1},NULL,error)) return false;
    }
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        if(!media) continue;
        qa_scene_world *world=frontend_unified_media_world(media);
        if(world && !world_claim(inventory,world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_UNIFIED_MAP,i+1,1},error))
            return false;
        for(size_t j=0;j<frontend_unified_media_model_count(media);++j) {
            frontend_unified_model_view model;
            if(!frontend_unified_media_model_read(media,j,&model)) return false;
            frontend_scene_owner owner={FRONTEND_SCENE_OWNER_UNIFIED_MODEL,i+1,j+1};
            if(model.world?!world_claim(inventory,model.world,owner,error):
                !model_claim(inventory,model.scene,owner,model.path,error)) return false;
        }
        for(size_t j=0;j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            if(!frontend_unified_media_bank_read(media,j,&bank)) return false;
            if(!bank.q3_assets) continue;
            size_t count=0;
            if(!qa_q3_assets_model_count(bank.q3_assets,&count,error)) return false;
            for(size_t k=0;k<count;++k) {
                qa_q3_asset_model_holder model; uint64_t row;
                if(!qa_q3_assets_model_holder(bank.q3_assets,k,&model,error)) return false;
                if(!model.present || model.shared_parent) continue;
                if(!frontend_unified_media_q3_row(j,k,&row)) return false;
                frontend_scene_owner owner={FRONTEND_SCENE_OWNER_UNIFIED_Q3,i+1,row};
                if(!registry_model_claim(inventory,&model,owner,error)) return false;
            }
        }
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error) ||
            !registry_roots_claim(inventory,row.assets,FRONTEND_SCENE_OWNER_COMPONENT,i+1,error)) return false;
    }
    for(size_t i=0;i<frontend_renderer_registries_count(f);++i) {
        qa_q3_presentation_assets *assets=NULL;
        if(!frontend_renderer_registries_at(f,i,&assets,error) ||
            !retained_registry_roots_claim(inventory,assets,i+1,error)) return false;
    }
    size_t retained_count=0;
    if(!frontend_renderer_worlds_count(f,&retained_count,error)) return false;
    for(size_t i=0;i<retained_count;++i) {
        frontend_renderer_worlds_view retained;
        if(!frontend_renderer_worlds_read_at(f,i,&retained,error)) return false;
        for(size_t j=0;j<inventory->world_count;++j) {
            world_row *row=inventory->worlds+j;
            if(row->source.world==retained.world && !row->claimed &&
                !world_claim(inventory,retained.world,(frontend_scene_owner){FRONTEND_SCENE_OWNER_RENDERER,i+1,1},error)) return false;
        }
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
        options->has_external_entities!=expected->has_external_entities ||
        memcmp(&options->subdivisions,&expected->subdivisions,sizeof(float)) ||
        memcmp(&options->q1_water_alpha,&expected->q1_water_alpha,sizeof(float)) ||
        memcmp(&options->q2_light_modulate,&expected->q2_light_modulate,sizeof(float)) ||
        (options->q2_sky!=NULL)!=(policy->sky!=NULL) ||
        (options->q2_sky && strcmp(options->q2_sky,policy->sky)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene world source or actual constructor policy differs");
    qa_bytes spans[]={options->external_lit,a->palette_rgb,a->translation,options->external_entities};
    for (size_t i=0;i<4;++i) {
        if (spans[i].size!=policy->spans[i].size || (spans[i].size && !spans[i].data) ||
            (spans[i].size && memcmp(spans[i].data,policy->spans[i].data,spans[i].size)))
            return frontend_fail(error,QA_ERROR_FORMAT,"World retained lighting or image policy bytes differ");
    }
    return true;
}
static bool world_source_options(void *context, qa_scene_world_options *out, qa_error *error)
{
    (void)error;
    world_scope *scope = context;
    *out = scope->row->policy.options;
    out->external_lit = scope->row->policy.spans[0];
    out->images.palette_rgb = scope->row->policy.spans[1];
    out->images.translation = scope->row->policy.spans[2];
    out->external_entities = scope->row->policy.spans[3];
    out->q2_sky = scope->row->policy.sky;
    return true;
}
static qa_scene_world_owner_refs world_refs(world_scope *scope)
{
    return (qa_scene_world_owner_refs){.state={.images={scope,image_encode,image_decode},.context=scope,
        .material_encode=material_encode,.material_decode=material_decode,.frame_encode=frame_encode,.frame_decode=frame_decode},
        .context=scope,.geometry_encode=geometry_encode,.geometry_decode=geometry_decode,
        .source_qualify=world_source_qualify,.source_options=world_source_options,.identity_decode=identity_decode,.identity_encode=identity_encode};
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
    if (!qa_source_save_u32(io,&kind) || kind>FRONTEND_SCENE_OWNER_REGISTRY ||
        !qa_source_save_u64(io,&owner->owner) || !qa_source_save_u64(io,&owner->row)) return false;
    owner->kind=(frontend_scene_owner_kind)kind; return owner_shape(*owner,world);
}
static bool heap_fields(qa_source_save_io *io,frontend_world_inventory *inventory,root_heap *heap,
    uint64_t *pool,uint64_t *version,const qa_vfs **files,qa_scene_resources **images,
    qa_material_library **materials,const qa_resource **resource)
{
    if (!qa_source_save_u32(io,&heap->kind) || !qa_source_save_u64(io,&heap->ordinal) ||
        !qa_source_save_u64(io,&heap->view) || !qa_source_save_u64(io,pool) || !qa_source_save_u64(io,version) ||
        !frontend_scene_heap_read(inventory->frontend,*heap,files,images,materials)) return false;
    qa_application_content_graph *graph=qa_application_content_graph_read(inventory->frontend->application);
    if (!heap->view || qa_application_content_view_id(graph,*files)!=heap->view) return false;
    const qa_resource *actual=qa_application_content_resource(graph,*pool,*version);
    if (!actual || qa_resource_pool_find(qa_vfs_resources(*files),qa_resource_id(actual))!=actual ||
        (io->direction==QA_SOURCE_SAVE_WRITE && actual!=*resource)) return false;
    *resource=actual; return true;
}
static const qa_resource *policy_resource(const world_row *row, size_t span)
{
    if (span != 0 && span != 3) return NULL;
    qa_bytes bytes = row->policy.spans[span];
    if ((!bytes.size && span == 0) || (span == 3 && !row->policy.options.has_external_entities)) return NULL;
    const char *extension = span == 0 ? ".lit" : ".ent";
    size_t count = qa_vfs_resource_count(row->source.files);
    for (size_t i = 0; i < count; ++i) {
        const qa_resource *resource = qa_vfs_resource_at(row->source.files, i, NULL);
        const char *path = qa_resource_path(resource); size_t length = strlen(path);
        qa_bytes source = qa_resource_bytes(resource);
        if (length >= 4 && !strcmp(path + length - 4, extension) && source.size == bytes.size &&
            (!bytes.size || !memcmp(source.data, bytes.data, bytes.size))) return resource;
    }
    return NULL;
}
static bool policy_fields(qa_source_save_io *io,world_row *row)
{
    world_policy *policy = &row->policy;
    qa_scene_world_options *o=&policy->options; qa_scene_image_options *image=&o->images;
    uint32_t family=image->family,wrap=image->wrap,filter=image->filter,usage=image->usage,encoding=o->q1_lightmap_encoding;
    int32_t transparent_index=image->transparent_index;
    if (!qa_source_save_u32(io,&family) || family>QA_SCENE_Q3 || !qa_source_save_u32(io,&wrap) || wrap>QA_SCENE_CLAMP ||
        !qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR || !qa_source_save_u32(io,&usage) || usage>QA_IMAGE_USAGE_SKY ||
        !qa_source_save_i32(io,&transparent_index) || !qa_source_save_bool(io,&image->mipmap) ||
        !qa_source_save_bool(io,&image->transparent) || !qa_source_save_bool(io,&image->fullbright_only) ||
        !qa_source_save_f32(io,&o->subdivisions) || !qa_source_save_f32(io,&o->q1_water_alpha) ||
        !qa_source_save_f32(io,&o->q2_light_modulate) || !qa_source_save_u32(io,&o->q3_overbright) ||
        !qa_source_save_u32(io,&encoding) || !qa_source_save_bool(io,&o->has_external_entities) ||
        !qa_source_save_owned_text(io,&policy->sky)) return false;
    image->family=(qa_scene_family)family; image->wrap=(qa_scene_wrap)wrap;
    image->filter=(qa_scene_filter)filter; image->usage=(qa_scene_image_usage)usage;
    image->transparent_index=transparent_index; o->q1_lightmap_encoding=(qa_scene_q1_lightmap_encoding)encoding;
    for (size_t i = 0; i < 4; ++i) {
        bool reading = io->direction == QA_SOURCE_SAVE_READ;
        const qa_resource *resource = !reading ? policy_resource(row, i) : NULL;
        qa_bytes palette = {0};
        bool installed_palette = i == 1 && qa_scene_resources_palette_read(row->source.images, image->family, &palette) &&
            palette.size == policy->spans[i].size && (!palette.size || !memcmp(palette.data, policy->spans[i].data, palette.size));
        uint8_t storage = reading ? 0 : resource ? 1 : installed_palette ? 2 : 0;
        if (!qa_source_save_u8(io, &storage) || storage > 2) return false;
        if (storage == 1) {
            uint64_t id = reading ? 0 : qa_resource_id(resource);
            if (!qa_source_save_u64(io, &id) || !id) return false;
            if (reading) resource = qa_resource_pool_find(qa_vfs_resources(row->source.files), id);
            if (!resource) return frontend_fail(io->error, QA_ERROR_FORMAT, "Saved world sidecar resource is unavailable");
            policy->spans[i] = qa_resource_bytes(resource);
        } else if (storage == 2) {
            if (i != 1 || !qa_scene_resources_palette_read(row->source.images, image->family, &palette)) return false;
            policy->spans[i] = palette;
        } else {
            size_t count = policy->spans[i].size;
            if (!qa_source_save_count(io, &count, reading ? io->input.size - io->offset : SIZE_MAX)) return false;
            if (!reading) {
                if (!qa_source_save_bytes(io, (void *)policy->spans[i].data, count)) return false;
            } else {
                if (count > io->input.size - io->offset) return false;
                policy->spans[i] = (qa_bytes){io->input.data + io->offset, count}; io->offset += count;
            }
        }
    }
    return o->has_external_entities || policy->spans[3].size==0;
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
            !policy_fields(io,row) || !blob(io,&row->state)) return false;
        for (size_t j=0;j<i;++j)
            if (row->owner.kind==inventory->worlds[j].owner.kind && row->owner.owner==inventory->worlds[j].owner.owner &&
                row->owner.row==inventory->worlds[j].owner.row) return false;
    }
    for (size_t i=0;i<inventory->model_count;++i) {
        model_row *row=inventory->models_roots+i; qa_scene_resources *images=NULL; qa_material_library *materials=NULL;
        if (!owner_fields(io,&row->view.owner,false) || !heap_fields(io,inventory,&row->heap,&row->pool,&row->resource,
            &row->view.source.files,&images,&materials,&row->view.source.resource) || !qa_source_save_u64(io,&row->model) ||
            !qa_source_save_owned_text(io,&row->path) ||
            ((row->view.owner.kind==FRONTEND_SCENE_OWNER_VISUAL ||
              row->view.owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q1 ||
              row->view.owner.kind==FRONTEND_SCENE_OWNER_REMOTE_Q2 ||
              row->view.owner.kind==FRONTEND_SCENE_OWNER_UNIFIED_MODEL) ? !row->path : row->path!=NULL) || !blob(io,&row->state)) return false;
        row->view.visual_path=row->path;
        if (io->direction==QA_SOURCE_SAVE_READ && !frontend_model_decode(inventory->models,row->model,
            &row->view.source.model,io->error)) return false;
        frontend_model_source actual={0}; bool found=false;
        for (size_t j=0;j<frontend_model_count(inventory->models);++j)
            if (frontend_model_source_at(inventory->models,j,&actual) && actual.model==row->view.source.model) { found=true; break; }
        if (!found || actual.files!=row->view.source.files || actual.resource!=row->view.source.resource) return false;
    }
    return true;
}
static bool header(qa_source_save_io *io,size_t *worlds,size_t *models)
{
    uint8_t magic[4]={'Q','F','W','R'}; size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFWR",4) && qa_source_save_count(io,worlds,maximum) && qa_source_save_count(io,models,maximum);
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
        ok=ok && qa_scene_world_source_resource_bind(world,row->source.resource,error) &&
            frontend_scene_namespace_qualify_world(space,i+1,world,error);
        row->state=(qa_bytes){0};
    }
    for (size_t i=0;ok && i<model_count;++i) {
        model_row *row=inventory->models_roots+i; const qa_vfs *files=NULL;
        qa_scene_resources *images=NULL; qa_material_library *materials=NULL; qa_scene_model *scene=NULL;
        frontend_scene_model_scope scope={{space,i+1},models}; qa_scene_model_owner_refs refs=frontend_scene_model_refs(&scope);
        ok=frontend_scene_heap_read(f,row->heap,&files,&images,&materials) &&
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
    if (!inventory || !inventory->restoring || !inventory->frames_restored)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene adoption readiness requires its actual imported root inventory");
    for (size_t i=0;i<inventory->world_count;++i)
        if (!inventory->worlds[i].adopted) return frontend_fail(error,QA_ERROR_FORMAT,"Imported world lacks its actual destructor consumer");
    for (size_t i=0;i<inventory->model_count;++i)
        if (!inventory->models_roots[i].adopted) return frontend_fail(error,QA_ERROR_FORMAT,"Imported scene model lacks its actual destructor consumer");
    return true;
}
bool frontend_world_inventory_finish_restore(frontend_world_inventory *inventory,qa_error *error)
{
    if (!inventory || !inventory->restoring || !inventory->frontend->source_restoring ||
        inventory->frontend->stepping || inventory->frontend->capture)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World frame completion requires its actual isolated root inventory");
    for (size_t i=0;i<inventory->world_count;++i)
        if (!qa_scene_world_restore_finish((qa_scene_world *)inventory->worlds[i].source.world,error)) return false;
    inventory->frames_restored=true;
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
bool frontend_remote_roots_attach_restored(qa_frontend *f,frontend_world_inventory *inventory,qa_error *error)
{
    if(!f || !f->application || f->stepping || f->capture || !f->source_restoring ||
        !inventory || !inventory->restoring || inventory->frontend!=f)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote map adoption requires its actual candidate root inventory");
    size_t count=frontend_remote_q3_count(f);
    for(size_t i=0;i<inventory->world_count;++i) {
        const world_row *row=inventory->worlds+i;
        if(row->owner.kind==FRONTEND_SCENE_OWNER_REMOTE_MAP &&
            (!row->owner.owner || row->owner.owner>count || row->owner.row!=1))
            return frontend_fail(error,QA_ERROR_FORMAT,"Remote map root leaves its physical resource parent");
    }
    for(size_t i=0;i<count;++i) {
        frontend_remote_q3 *parent=frontend_remote_q3_at(f,i);
        frontend_remote_q3_resources owner; size_t key=0;
        if(!frontend_remote_q3_resources_import_read(parent,&owner,error) || owner.world || !owner.map || !owner.geometry)
            return frontend_fail(error,QA_ERROR_FORMAT,"Remote map parent lacks its genuine prepared collision owner");
        for(size_t j=0;j<inventory->world_count;++j) {
            world_row *row=inventory->worlds+j;
            if(row->owner.kind!=FRONTEND_SCENE_OWNER_REMOTE_MAP || row->owner.owner!=i+1) continue;
            if(key || row->source.resource!=owner.map || row->source.files!=owner.mounts ||
                row->source.images!=owner.images || row->source.materials!=owner.materials ||
                !frontend_world_owner_ready(inventory,j+1,FRONTEND_SCENE_OWNER_REMOTE_MAP,i+1,error) ||
                !frontend_remote_q3_resources_world_adopt_ready(parent,(qa_scene_world *)row->source.world,error))
                return frontend_fail(error,QA_ERROR_FORMAT,"Remote map root differs from its actual resource and paired heaps");
            key=j+1;
        }
        if(!key) return frontend_fail(error,QA_ERROR_FORMAT,"Remote map has no actual saved destructor root");
    }
    for(size_t i=0;i<inventory->world_count;++i) {
        world_row *row=inventory->worlds+i;
        if(row->owner.kind!=FRONTEND_SCENE_OWNER_REMOTE_MAP) continue;
        frontend_remote_q3_resources_world_adopt(frontend_remote_q3_at(f,(size_t)row->owner.owner-1),
            (qa_scene_world *)row->source.world);
        frontend_world_adopt(inventory,i+1);
    }
    return true;
}
