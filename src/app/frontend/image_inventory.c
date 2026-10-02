#include "image_inventory.h"
#include "component_scene.h"
#include "equipment_media.h"
#include "save_private.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q1_restore.h"
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
        ok=(f->source_restoring?frontend_remote_q1_import_read(row,&owner,error):
            frontend_remote_q1_metadata_read(row,&owner,error)) &&
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
    frontend_renderer_worlds_view world; bool has_world=false;
    if(ok) ok=frontend_renderer_worlds_read(f,&world,&has_world,error);
    if(ok && has_world && world.private_heaps) ok=add(inventory,graph,world.images,13,0,0,error);
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
static bool header(qa_source_save_io *io, const image_inventory *inventory)
{
    uint8_t magic[4] = {'Q','F','I','M'}; uint32_t version = 6; size_t count = inventory->count;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFIM", 4) ||
        !qa_source_save_u32(io, &version) || version != 6 ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(image_owner)) || count != inventory->count) return false;
    for (size_t i = 0; i < count; ++i) {
        image_owner saved = inventory->entries[i];
        if (!qa_source_save_u32(io, &saved.kind) || !qa_source_save_u64(io, &saved.ordinal) ||
            !qa_source_save_u64(io, &saved.identity) || !qa_source_save_u64(io, &saved.view) ||
            saved.kind != inventory->entries[i].kind || saved.ordinal != inventory->entries[i].ordinal ||
            saved.identity != inventory->entries[i].identity || saved.view != inventory->entries[i].view) return false;
    }
    return true;
}
bool frontend_images_checkpoint(qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size) return frontend_fail(error, QA_ERROR_ARGUMENT, "image capture requires empty output");
    image_inventory inventory = {0}; qa_buffer images = {0}; qa_source_save_io io = {0};
    bool ok = collect(f, &inventory, error) &&
        (!inventory.count || qa_scene_images_checkpoint((const qa_scene_resources *const *)inventory.owners, inventory.count, &images, error)) &&
        qa_source_save_writer(&io, qa_application_session(f->application), error) && header(&io, &inventory);
    size_t count = images.size;
    ok = ok && qa_source_save_count(&io, &count, SIZE_MAX) && qa_source_save_bytes(&io, images.data, count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&images); dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "image owner topology is not completely qualified");
    return ok;
}
bool frontend_images_restore(qa_frontend *f, qa_bytes bytes, qa_scene_image_set **out, qa_error *error)
{
    if (!out || *out) return frontend_fail(error, QA_ERROR_ARGUMENT, "image restore requires empty construction-reference output");
    image_inventory inventory = {0}; qa_source_save_io io = {0}; size_t size = 0;
    bool ok = collect(f, &inventory, error) && qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        header(&io, &inventory) && qa_source_save_count(&io, &size, bytes.size);
    qa_bytes images = {0};
    if (ok) {
        if (io.offset > bytes.size || size > bytes.size - io.offset) ok = false;
        else { images = (qa_bytes){bytes.data + io.offset, size}; io.offset += size; }
    }
    ok = ok && qa_source_save_finish(&io, NULL) && (inventory.count ?
        qa_scene_images_restore(inventory.owners, inventory.count, images, out, error) : images.size == 0);
    qa_source_save_dispose(&io); dispose(&inventory);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "saved image topology differs from prepared actual owners");
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
typedef struct image_state_scope {
    frontend_scene_namespace *space;
    qa_application_content_graph *content;
    const image_owner *owner;
} image_state_scope;
static bool state_image_encode(void *context,const qa_scene_image *image,uint64_t *key,qa_error *error)
{ return frontend_scene_image_encode(((image_state_scope *)context)->space,image,key,error); }
static bool state_image_decode(void *context,uint64_t key,const qa_scene_image **image,qa_error *error)
{ return frontend_scene_image_decode(((image_state_scope *)context)->space,key,image,error); }
static bool state_resource_encode(void *context,const qa_resource *resource,uint64_t *pool,uint64_t *version,qa_error *error)
{
    image_state_scope *scope=context;
    if (!pool || !version || pool==version || !resource ||
        qa_resource_pool_find(qa_vfs_resources(qa_scene_resources_files(scope->owner->images)),qa_resource_id(resource))!=resource ||
        !qa_application_content_resource_id(scope->content,resource,pool,version))
        return frontend_fail(error,QA_ERROR_FORMAT,"Image cache source leaves its actual resource owner pool");
    return true;
}
static bool state_resource_decode(void *context,uint64_t pool,uint64_t version,const qa_resource **out,qa_error *error)
{
    image_state_scope *scope=context; const qa_resource *resource=qa_application_content_resource(scope->content,pool,version);
    if (!out || !resource ||
        qa_resource_pool_find(qa_vfs_resources(qa_scene_resources_files(scope->owner->images)),qa_resource_id(resource))!=resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved image cache source has no genuine restored pool owner");
    *out=resource; return true;
}
static qa_scene_resource_checkpoint_refs state_refs(image_state_scope *scope)
{
    return (qa_scene_resource_checkpoint_refs){scope,state_image_encode,state_image_decode,state_resource_encode,state_resource_decode};
}
static bool owners_header(qa_source_save_io *io,const image_inventory *inventory)
{
    uint8_t magic[4]={'Q','F','I','S'}; uint32_t version=1;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFIS",4) &&
        qa_source_save_u32(io,&version) && version==1 && header(io,inventory);
}
bool frontend_image_owners_checkpoint(qa_frontend *f,frontend_scene_namespace *space,qa_buffer *out,qa_error *error)
{
    if (!f || !f->capture || f->source_restoring || !space || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Image owner capture requires its retained frontend and shared namespace");
    image_inventory inventory={0}; qa_source_save_io io={0};
    bool ok=collect(f,&inventory,error) && qa_source_save_writer(&io,NULL,error) && owners_header(&io,&inventory);
    for (size_t i=0;ok && i<inventory.count;++i) {
        image_state_scope scope={space,qa_application_content_graph_read(f->application),inventory.entries+i};
        qa_scene_resource_checkpoint_refs refs=state_refs(&scope); qa_buffer state={0};
        ok=qa_scene_resources_checkpoint(inventory.owners[i],&refs,&state,error) &&
            qa_source_save_count(&io,&state.size,SIZE_MAX) && qa_source_save_bytes(&io,state.data,state.size);
        qa_buffer_free(&state);
    }
    ok=ok && qa_source_save_finish(&io,out); qa_source_save_dispose(&io); dispose(&inventory);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Image owner private continuation has invalid actual resource references");
    return ok;
}
bool frontend_image_owners_restore(qa_frontend *f,frontend_scene_namespace *space,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->source_restoring || f->capture || !space)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Image owner import requires its isolated actual frontend and namespace");
    image_inventory inventory={0}; qa_source_save_io io={0}; qa_bytes *states=NULL;
    bool ok=collect(f,&inventory,error) && inventory.count<=SIZE_MAX/sizeof(*states);
    if (ok && inventory.count) {
        states=calloc(inventory.count,sizeof(*states));
        if (!states) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining complete image owner section inventory");
    }
    ok=ok && qa_source_save_reader(&io,NULL,bytes,error) && owners_header(&io,&inventory);
    for (size_t i=0;ok && i<inventory.count;++i) {
        size_t count=0;
        ok=qa_source_save_count(&io,&count,bytes.size-io.offset) && count<=bytes.size-io.offset;
        if (ok) { states[i]=(qa_bytes){bytes.data+io.offset,count}; io.offset+=count; }
    }
    ok=ok && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    for (size_t i=0;ok && i<inventory.count;++i) {
        image_state_scope scope={space,qa_application_content_graph_read(f->application),inventory.entries+i};
        qa_scene_resource_checkpoint_refs refs=state_refs(&scope);
        ok=qa_scene_resources_restore(inventory.owners[i],states[i],&refs,error);
    }
    free(states); dispose(&inventory);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved image owner envelope differs from its prepared real topology");
    return ok;
}
