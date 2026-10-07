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
#include "remote_q1_client.h"
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
        if(heap.ordinal>SIZE_MAX ||
            !frontend_remote_q1_metadata_read(frontend_remote_q1_at(f,(size_t)heap.ordinal),&owner,&error)) return false;
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
    qa_scene_resources *images,qa_material_library *materials,bool *found,qa_error *error)
{
    if(!f || !found) return false;
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
                *found=true; return true;
            }
        }
    }
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        for(size_t j=0;media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            if(!frontend_unified_media_bank_read(media,j,&bank)) return false;
            if(bank.files==files && bank.images==images && bank.materials==materials) {
                *found=true; return true;
            }
        }
    }
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error)) return false;
        if(row.files==files && row.images==images && row.materials==materials) {
            *found=true; return true;
        }
    }
    for(size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view row;
        if(!frontend_equipment_media_at(f,i,&row)) return false;
        if(row.source_slot && row.owner.mounts==files && row.owner.images==images && row.owner.materials==materials) {
            *found=true; return true;
        }
    }
    return true;
}
