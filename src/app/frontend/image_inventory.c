#include "image_inventory.h"
#include "component_scene.h"
#include "equipment_media.h"
#include "save_private.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q1_client.h"
#include "remote_q2_restore.h"
#include "renderer_materials.h"
#include "renderer_worlds.h"
#include "unified_media_inventory.h"
#include "qa/scene_resource_save.h"
#include "qa/persistence_content.h"

typedef struct image_owner {
    qa_scene_resources *images;
    uint32_t kind;
    uint64_t ordinal, identity, view;
} image_owner;
typedef struct image_inventory { image_owner *entries; qa_scene_resources **owners; size_t count; } image_inventory;
static void dispose(image_inventory *inventory)
{ free(inventory->entries); free(inventory->owners); *inventory = (image_inventory){0}; }
static bool add(image_inventory *inventory, qa_application_content_graph *graph,
    const qa_scene_resources *images, uint32_t kind, uint64_t ordinal, uint64_t identity, qa_error *error)
{
    if (!images) return true;
    uint64_t view = qa_application_content_view_id(graph, qa_scene_resources_files(images));
    if (!view) return frontend_fail(error, QA_ERROR_FORMAT, "image owner view is outside the actual content graph");
    for (size_t i = 0; i < inventory->count; ++i)
        if (inventory->entries[i].images == images)
            return inventory->entries[i].view==view;
    if (inventory->count == SIZE_MAX / sizeof(*inventory->entries) || inventory->count == SIZE_MAX / sizeof(*inventory->owners))
        return frontend_fail(error, QA_ERROR_MEMORY, "image owner inventory overflows storage");
    image_owner *entries = realloc(inventory->entries, (inventory->count + 1) * sizeof(*entries));
    if (!entries) return frontend_fail(error, QA_ERROR_MEMORY, "allocating image owner inventory");
    inventory->entries = entries;
    qa_scene_resources **owners = realloc(inventory->owners, (inventory->count + 1) * sizeof(*owners));
    if (!owners) return frontend_fail(error, QA_ERROR_MEMORY, "allocating image codec owner array");
    inventory->owners = owners;
    entries[inventory->count] = (image_owner){(qa_scene_resources *)images, kind, ordinal, identity, view};
    owners[inventory->count++] = (qa_scene_resources *)images; return true;
}
static bool collect(qa_frontend *f, image_inventory *inventory, qa_error *error)
{
    if (!f || !f->application || f->stepping || !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "image graph requires actual idle frontend owners");
    qa_application_content_graph *graph = qa_application_content_graph_read(f->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "image graph requires the actual content graph lease");
    bool ok = add(inventory, graph, f->ui_images, 0, 0, 0, error) && add(inventory, graph, f->images, 1, 0, 0, error);
    for (size_t i = 0; ok && i < frontend_source_group_count(f); ++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f, i, &group)) return frontend_fail(error, QA_ERROR_ARGUMENT, "source image heap is not fully constructed");
        ok = group.images && add(inventory, graph, group.images, 2, i, group.identity, error);
    }
    for (uint32_t kind = 3; ok && kind <= 5; ++kind) {
        for (size_t i = 0; ok; ++i) {
            const qa_scene_resources *images = kind == 3 ? frontend_event_images_at(f, i) :
                kind == 4 ? frontend_visual_images_at(f, i) : frontend_native_q2_images_at(f, i);
            if (!images) break;
            ok = add(inventory, graph, images, kind, i, 0, error);
        }
    }
    for (size_t i = 0; ok && i < frontend_native_q3_count(f); ++i) {
        frontend_native_q3_view owner;
        ok = frontend_native_q3_read(f, i, &owner, error) && owner.images &&
            add(inventory, graph, owner.images, 6, i, owner.identity, error);
    }
    for(size_t i=0;ok && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) && owner.images &&
            add(inventory,graph,owner.images,7,i,owner.identity,error);
    }
    frontend_network_initial_graph_view initial;
    if(ok) ok=frontend_network_initial_graph_read(f,&initial,error);
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.images &&
            add(inventory,graph,owner.images,8,0,owner.identity,error);
    }
    for(size_t i=0;ok && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_view owner; frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        ok=frontend_remote_q1_metadata_read(row,&owner,error) &&
            add(inventory,graph,owner.images,9,i,owner.map_generation,error);
    }
    for(size_t i=0;ok && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view owner; frontend_remote_q2 *row=frontend_remote_q2_at(f,i);
        ok=(f->source_restoring?frontend_remote_q2_import_read(row,&owner,error):
            frontend_remote_q2_metadata_read(row,&owner,error)) &&
            add(inventory,graph,owner.images,10,i,owner.identity,error);
    }
    size_t retained_count=0;
    if(ok) ok=frontend_renderer_materials_count(f,&retained_count,error);
    for(size_t i=0;ok && i<retained_count;++i) {
        frontend_renderer_materials_view retained;
        ok=frontend_renderer_materials_read_at(f,i,&retained,error) &&
            add(inventory,graph,retained.images,11,i,0,error) &&
            add(inventory,graph,retained.lightmap_images,12,i,0,error);
    }
    size_t world_count=0;
    if(ok) ok=frontend_renderer_worlds_count(f,&world_count,error);
    for(size_t i=0;ok && i<world_count;++i) {
        frontend_renderer_worlds_view world;
        ok=frontend_renderer_worlds_read_at(f,i,&world,error);
        if(ok && world.private_heaps) ok=add(inventory,graph,world.images,13,i,0,error);
    }
    size_t unified_count=0;
    if(ok) ok=frontend_unified_media_inventory_count(f,&unified_count,error);
    for(size_t i=0;ok && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        ok=frontend_unified_media_inventory_at(f,i,&media,error);
        for(size_t j=0;ok && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank; uint64_t key;
            ok=frontend_unified_media_bank_read(media,j,&bank) && frontend_unified_media_bank_key(i,j,&key) &&
                add(inventory,graph,bank.images,14,key,0,error);
        }
    }
    for(size_t i=0;ok && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        ok=frontend_component_scene_metadata_read(f,i,&row,error) &&
            add(inventory,graph,row.images,15,i,row.identity,error);
    }
    for(size_t i=0;ok && i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view row;
        ok=frontend_equipment_media_at(f,i,&row);
        if(ok && row.source_slot) ok=add(inventory,graph,row.owner.images,16,i,row.source_generation,error);
    }
    return ok;
}
bool frontend_image_index(qa_frontend *f, const qa_scene_image *image, uint64_t *out, qa_error *error)
{
    if (!image || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "image reference requires an actual version and output");
    image_inventory inventory = {0}; size_t index = 0;
    bool ok = collect(f, &inventory, error) && qa_scene_image_owner_index((const qa_scene_resources *const *)inventory.owners,
        inventory.count, image, &index);
    if (ok) *out = index;
    dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "retained image is outside the actual frontend owner graph");
    return ok;
}
