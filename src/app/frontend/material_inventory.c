#include "material_inventory.h"
#include "capture.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q1_restore.h"
#include "remote_q2_restore.h"
#include "save_private.h"
#include "qa/material_library_save.h"
#include "qa/material_save.h"
#include "qa/persistence_content.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"

typedef enum material_owner_kind { MATERIAL_FRONTEND, MATERIAL_SOURCE, MATERIAL_VISUAL, MATERIAL_NATIVE_Q3,
    MATERIAL_REMOTE, MATERIAL_INITIAL, MATERIAL_REMOTE_Q1, MATERIAL_REMOTE_Q2 } material_owner_kind;
typedef struct material_owner {
    qa_material_library *library;
    qa_scene_resources *images;
    material_owner_kind kind;
    size_t ordinal;
    qa_actor_owner provider;
    uint64_t identity, view, pool, source_view;
    qa_scene_family family;
} material_owner;
typedef struct material_scope {
    frontend_scene_namespace *space;
    qa_application_content_graph *graph;
    const material_owner *owner;
} material_scope;
typedef struct material_saved { qa_bytes catalog, records; } material_saved;

static bool provider_at(const qa_frontend *f,size_t ordinal,qa_q3_presentation_provider *provider)
{
    if (!f || !provider || !f->application || f->stepping) return false;
    if (f->materials) {
        if (!ordinal) {
            qa_scene_world_options options;
            if (!f->scene_world || !qa_scene_world_options_read(f->scene_world,&options)) return false;
            *provider=(qa_q3_presentation_provider){f->mounts,f->images,f->materials,options.images.family};
            return true;
        }
        --ordinal;
    }
    size_t groups=frontend_source_group_count(f);
    if (ordinal<groups) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f,ordinal,&group)) return false;
        *provider=(qa_q3_presentation_provider){group.mounts,group.images,group.materials,QA_SCENE_Q3};
        return true;
    }
    ordinal-=groups;
    size_t visuals=frontend_visual_owner_count(f);
    if (ordinal<visuals) {
        frontend_visual_owner_view visual;
        if (!frontend_visual_owner_read(f,ordinal,&visual)) return false;
        *provider=(qa_q3_presentation_provider){visual.mounts,visual.images,visual.materials,visual.family}; return true;
    }
    ordinal-=visuals;
    qa_error error={0}; size_t native_count=frontend_native_q3_count(f);
    if(ordinal<native_count) {
        frontend_native_q3_view native;
        if(!frontend_native_q3_read(f,ordinal,&native,&error)) return false;
        *provider=(qa_q3_presentation_provider){native.mounts,native.images,native.materials,QA_SCENE_Q3}; return true;
    }
    ordinal-=native_count;
    size_t remote_count=frontend_remote_q3_count(f);
    if(ordinal<remote_count) {
        frontend_remote_q3_resources remote;
        if(!frontend_remote_q3_resources_read(frontend_remote_q3_at(f,ordinal),&remote,&error)) return false;
        *provider=(qa_q3_presentation_provider){remote.mounts,remote.images,remote.materials,QA_SCENE_Q3}; return true;
    }
    ordinal-=remote_count;
    frontend_network_initial_graph_view initial; frontend_remote_q3_initial_view owner;
    if(ordinal || !frontend_network_initial_graph_read(f,&initial,&error) || !initial.present ||
        !initial.parent || !frontend_remote_q3_initial_read(initial.parent,&owner,&error)) return false;
    *provider=(qa_q3_presentation_provider){owner.mounts,owner.images,owner.materials,QA_SCENE_Q3}; return true;
}
bool frontend_material_provider_encode(const qa_frontend *f,const qa_q3_presentation_provider *provider,
    uint64_t *key,qa_error *error)
{
    if (!f || !provider || !key || provider->family>QA_SCENE_Q3)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 provider lookup requires actual frontend heaps");
    size_t groups=frontend_source_group_count(f),visuals=frontend_visual_owner_count(f),native=frontend_native_q3_count(f);
    if (groups==SIZE_MAX || visuals>SIZE_MAX-groups-1 || native>SIZE_MAX-groups-visuals-1) return false;
    size_t count=(f->materials?1:0)+groups+visuals+native,remote=frontend_remote_q3_count(f);
    frontend_network_initial_graph_view initial;
    if(remote>SIZE_MAX-count || !frontend_network_initial_graph_read(f,&initial,error)) return false;
    count+=remote;
    if(initial.present) { if(count==SIZE_MAX) return false; ++count; }
    for (size_t i=0;i<count;++i) {
        qa_q3_presentation_provider actual;
        if (provider_at(f,i,&actual) && actual.mounts==provider->mounts && actual.images==provider->images &&
            actual.materials==provider->materials && actual.family==provider->family) { *key=i+1; return true; }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Q3 provider tuple is outside the actual material library roster");
}
bool frontend_material_provider_decode(const qa_frontend *f,uint64_t key,qa_q3_presentation_provider *provider,qa_error *error)
{
    qa_q3_presentation_provider actual;
    if (!key || key-1>SIZE_MAX || !provider || !provider_at(f,(size_t)(key-1),&actual) ||
        !actual.mounts || !actual.images || !actual.materials || qa_scene_resources_files(actual.images)!=actual.mounts ||
        qa_material_library_resource_owner(actual.materials)!=actual.images)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved Q3 provider has no genuine imported material library tuple");
    *provider=actual; return true;
}

static bool append(material_owner *owners, size_t *count, qa_application_content_graph *graph,
    qa_material_library *library, qa_scene_resources *images, const qa_vfs *files,
    material_owner_kind kind, size_t ordinal, qa_actor_owner provider, uint64_t identity,
    const qa_vfs *source_files, qa_scene_family family, qa_error *error)
{
    if (!library) return true;
    uint64_t view=qa_application_content_view_id(graph,files);
    uint64_t pool=qa_application_content_pool_id(graph,qa_vfs_resources(files));
    uint64_t source_view=source_files?qa_application_content_view_id(graph,source_files):0;
    if (!images || !files || !view || !pool || qa_material_library_resource_owner(library)!=images ||
        qa_scene_resources_files(images)!=files || (source_files && (!source_view || source_view==view ||
        qa_vfs_resources(source_files)!=qa_vfs_resources(files))))
        return frontend_fail(error,QA_ERROR_FORMAT,"Material library leaves its actual image and content owners");
    for (size_t i=0;i<*count;++i) if (owners[i].library==library)
        return frontend_fail(error,QA_ERROR_FORMAT,"Material libraries repeat destructor authority");
    owners[(*count)++]=(material_owner){library,images,kind,ordinal,provider,identity,view,pool,source_view,family};
    return true;
}
static bool collect(qa_frontend *f, bool restoring, material_owner **out, size_t *count, qa_error *error)
{
    if (!f || !f->application || f->stepping || f->preparing || f->round || !out || *out || !count || *count ||
        f->source_restoring!=restoring || (restoring?f->capture!=NULL:f->capture==NULL) ||
        !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Material inventory requires its actual capture or isolated candidate scope");
    if (restoring && (!frontend_seat_callbacks_idle(f) || !frontend_source_complete_groups(f,error)))
        return error && error->code!=QA_OK?false:
            frontend_fail(error,QA_ERROR_ARGUMENT,"Material import requires completely constructed idle borrowers");
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    size_t groups=frontend_source_group_count(f), visuals=frontend_visual_owner_count(f),native=frontend_native_q3_count(f);
    if (!graph || groups==SIZE_MAX || visuals>SIZE_MAX-groups-1 || native>SIZE_MAX-groups-visuals-1 ||
        groups+visuals+native+1>SIZE_MAX/sizeof(material_owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Material inventory requires its bounded genuine content graph");
    size_t capacity=groups+visuals+native+1,remote=frontend_remote_q3_count(f);
    frontend_network_initial_graph_view initial;
    if(remote>SIZE_MAX-capacity || !frontend_network_initial_graph_read(f,&initial,error)) return false;
    capacity+=remote;
    if(initial.present) { if(capacity==SIZE_MAX) return false; ++capacity; }
    size_t q1=frontend_remote_q1_count(f),q2=frontend_remote_q2_count(f);
    if(q1>SIZE_MAX-capacity || q2>SIZE_MAX-capacity-q1) return false;
    capacity+=q1+q2;
    if(capacity>SIZE_MAX/sizeof(material_owner))
        return frontend_fail(error,QA_ERROR_MEMORY,"Remote material inventory exceeds address space");
    material_owner *owners=calloc(capacity,sizeof(*owners));
    if (!owners) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual material library owners");
    bool ok=append(owners,count,graph,f->materials,f->images,f->mounts,MATERIAL_FRONTEND,0,0,0,NULL,0,error);
    for (size_t i=0;ok && i<groups;++i) {
        frontend_source_group_view group;
        ok=frontend_source_group_read(f,i,&group) && group.materials;
        if (ok && restoring) ok=qa_q3_presentation_idle(group.presentation) && qa_q3_assets_idle(group.assets);
        if (ok) ok=append(owners,count,graph,group.materials,group.images,group.mounts,
            MATERIAL_SOURCE,i,group.owner,group.identity,group.source_files,0,error);
    }
    for (size_t i=0;ok && i<visuals;++i) {
        frontend_visual_owner_view owner;
        ok=frontend_visual_owner_read(f,i,&owner) && owner.materials;
        if (ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.mounts,
            MATERIAL_VISUAL,i,owner.owner,0,NULL,owner.family,error);
    }
    for (size_t i=0;ok && i<native;++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(f,i,&owner,error) && owner.materials;
        if (ok && restoring) ok=(!owner.presentation || qa_q3_presentation_idle(owner.presentation)) &&
            (!owner.assets || qa_q3_assets_idle(owner.assets));
        if (ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.mounts,
            MATERIAL_NATIVE_Q3,i,owner.receiver,owner.identity,owner.source_files,QA_SCENE_Q3,error);
    }
    for(size_t i=0;ok && i<q1;++i) {
        frontend_remote_q1_view owner; frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        ok=restoring?frontend_remote_q1_import_read(row,&owner,error):frontend_remote_q1_metadata_read(row,&owner,error);
        if(ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.content.mounts,
            MATERIAL_REMOTE_Q1,i,owner.domain.actor_owner,owner.map_generation,NULL,QA_SCENE_Q1,error);
    }
    for(size_t i=0;ok && i<q2;++i) {
        frontend_remote_q2_view owner; frontend_remote_q2 *row=frontend_remote_q2_at(f,i);
        ok=restoring?frontend_remote_q2_import_read(row,&owner,error):frontend_remote_q2_metadata_read(row,&owner,error);
        if(ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.content.mounts,
            MATERIAL_REMOTE_Q2,i,0,owner.identity,NULL,QA_SCENE_Q2,error);
    }
    for(size_t i=0;ok && i<remote;++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) && owner.materials;
        if(ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.mounts,
            MATERIAL_REMOTE,i,owner.domain.source.receiver.receiver,owner.identity,owner.domain.content,QA_SCENE_Q3,error);
    }
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.materials;
        if(ok) ok=append(owners,count,graph,owner.materials,owner.images,owner.mounts,
            MATERIAL_INITIAL,0,owner.attempt.source.receiver.receiver,owner.identity,owner.descriptor->content,QA_SCENE_Q3,error);
    }
    for (size_t i=0;ok && i<*count;++i) {
        if (restoring) ok=qa_material_library_empty_detached(owners[i].library);
        else ok=frontend_capture_library_at(f->capture,i)==owners[i].library &&
            qa_material_library_order_owner(owners[i].library)==f->order && qa_material_library_order_ready(owners[i].library);
    }
    if (ok && !restoring) ok=frontend_capture_library_at(f->capture,*count)==NULL;
    if (!ok) {
        free(owners); *count=0;
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Material owner inventory is incomplete or outside its actual phase");
        return false;
    }
    *out=owners; return true;
}
static bool image_encode(void *context,const qa_scene_image *image,uint64_t *key,qa_error *error)
{ return frontend_scene_image_encode(((material_scope *)context)->space,image,key,error); }
static bool image_decode(void *context,uint64_t key,const qa_scene_image **image,qa_error *error)
{ return frontend_scene_image_decode(((material_scope *)context)->space,key,image,error); }
static bool image_identity_encode(void *context,uint64_t identity,uint64_t *key,qa_error *error)
{ return frontend_scene_image_identity_encode(((material_scope *)context)->space,identity,key,error); }
static bool image_identity_decode(void *context,uint64_t key,uint64_t *identity,qa_error *error)
{ return frontend_scene_image_identity_decode(((material_scope *)context)->space,key,identity,error); }
static bool world_encode(void *context,uint64_t identity,uint64_t *key,qa_error *error)
{ return frontend_scene_world_identity_encode(((material_scope *)context)->space,identity,key,error); }
static bool world_decode(void *context,uint64_t key,uint64_t *identity,qa_error *error)
{ return frontend_scene_world_identity_decode(((material_scope *)context)->space,key,identity,error); }
static bool resource_encode(void *context,const qa_resource *resource,uint64_t *pool,uint64_t *version,qa_error *error)
{
    material_scope *scope=context; uint64_t actual_pool=0, actual_version=0;
    if (!pool || !version || !qa_application_content_resource_id(scope->graph,resource,&actual_pool,&actual_version) ||
        actual_pool!=scope->owner->pool)
        return frontend_fail(error,QA_ERROR_FORMAT,"Shader catalog source is outside its actual qualified content pool");
    *pool=actual_pool; *version=actual_version; return true;
}
static bool resource_decode(void *context,uint64_t pool,uint64_t version,const qa_resource **out,qa_error *error)
{
    material_scope *scope=context;
    const qa_resource *resource=pool==scope->owner->pool?qa_application_content_resource(scope->graph,pool,version):NULL;
    if (!out || !resource) return frontend_fail(error,QA_ERROR_FORMAT,"Saved shader source is absent from its qualified content pool");
    *out=resource; return true;
}
static qa_material_library_checkpoint_refs references(material_scope *scope)
{
    return (qa_material_library_checkpoint_refs){.context=scope,.image_encode=image_encode,.image_decode=image_decode,
        .image_identity_encode=image_identity_encode,.image_identity_decode=image_identity_decode,
        .world_encode=world_encode,.world_decode=world_decode,.resource_encode=resource_encode,.resource_decode=resource_decode};
}
static bool metadata(qa_source_save_io *io,qa_frontend *f,const material_owner *owner)
{
    material_owner saved=*owner; uint32_t kind=saved.kind, family=saved.family;
    qa_strings *strings=qa_session_strings(qa_application_session(f->application));
    const char *expected=owner->provider?qa_strings_cstr(strings,owner->provider):NULL;
    char *provider=io->direction==QA_SOURCE_SAVE_WRITE?(char *)expected:NULL;
    bool ok=(!owner->provider || expected) && qa_source_save_u32(io,&kind) && kind==(uint32_t)owner->kind &&
        qa_source_save_count(io,&saved.ordinal,SIZE_MAX) && saved.ordinal==owner->ordinal &&
        qa_source_save_u64(io,&saved.identity) && saved.identity==owner->identity &&
        qa_source_save_u64(io,&saved.view) && saved.view==owner->view &&
        qa_source_save_u64(io,&saved.pool) && saved.pool==owner->pool &&
        qa_source_save_u64(io,&saved.source_view) && saved.source_view==owner->source_view &&
        qa_source_save_u32(io,&family) && family==(uint32_t)owner->family && frontend_save_text(io,&provider);
    if (ok) ok=expected?(provider && !strcmp(provider,expected)):provider==NULL;
    if (io->direction==QA_SOURCE_SAVE_READ) free(provider);
    return ok;
}
static bool header(qa_source_save_io *io,size_t count,bool *order)
{
    uint8_t magic[4]={'Q','F','M','A'}; uint32_t version=4; size_t saved=count;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFMA",4) && qa_source_save_u32(io,&version) && version==4 &&
        qa_source_save_count(io,&saved,SIZE_MAX) && saved==count && qa_source_save_bool(io,order) && (!count || *order);
}
static bool write_blob(qa_source_save_io *io,const qa_buffer *buffer)
{
    size_t size=buffer->size;
    return qa_source_save_count(io,&size,SIZE_MAX) && qa_source_save_bytes(io,buffer->data,size);
}
static bool read_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=0;
    if (!qa_source_save_count(io,&size,io->input.size-io->offset) || size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size;
    return true;
}
bool frontend_materials_capture_namespace(qa_frontend *f,frontend_scene_namespace *space,qa_error *error)
{
    if (!space) return frontend_fail(error,QA_ERROR_ARGUMENT,"Material namespace requires its actual shared scene dictionary");
    material_owner *owners=NULL; size_t count=0;
    bool ok=collect(f,false,&owners,&count,error);
    for (size_t i=0;ok && i<count;++i) ok=frontend_scene_namespace_capture_library(space,i+1,owners[i].library,error);
    free(owners); return ok;
}
bool frontend_materials_checkpoint(qa_frontend *f,frontend_scene_namespace *space,qa_buffer *out,qa_error *error)
{
    if (!space || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Material checkpoint requires its shared dictionary and an empty output");
    material_owner *owners=NULL; size_t count=0; qa_source_save_io io={0}; qa_buffer order_bytes={0};
    bool order=f && f->order!=NULL;
    bool ok=collect(f,false,&owners,&count,error) && qa_source_save_writer(&io,NULL,error) && header(&io,count,&order);
    qa_application_content_graph *graph=ok?qa_application_content_graph_read(f->application):NULL;
    for (size_t i=0;ok && i<count;++i) {
        material_scope scope={space,graph,owners+i}; qa_material_library_checkpoint_refs refs=references(&scope);
        qa_buffer catalog={0}, records={0};
        ok=metadata(&io,f,owners+i) && qa_material_library_catalog_checkpoint(owners[i].library,&refs,&catalog,error) &&
            qa_material_library_checkpoint(owners[i].library,&refs,&records,error) && write_blob(&io,&catalog) && write_blob(&io,&records);
        qa_buffer_free(&catalog); qa_buffer_free(&records);
    }
    qa_material_checkpoint_refs refs={space,frontend_scene_material_encode,frontend_scene_material_mutable_decode};
    if (ok && order) ok=qa_material_order_checkpoint(f->order,&refs,&order_bytes,error);
    ok=ok && write_blob(&io,&order_bytes) && qa_source_save_finish(&io,out);
    qa_buffer_free(&order_bytes); qa_source_save_dispose(&io); free(owners);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid actual frontend material owner graph");
    return ok;
}
static bool reject_existing(void *context,const qa_material *material,uint64_t *key,qa_error *error)
{
    (void)context; (void)material; (void)key;
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Candidate renderer order contains records before material import");
}
static bool empty_order(qa_material_order *order,qa_error *error)
{
    if (!order) return true;
    if (!qa_material_order_idle(order)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Candidate renderer order is retained by another operation");
    qa_buffer bytes={0}; qa_material_checkpoint_refs refs={.material_encode=reject_existing};
    bool ok=qa_material_order_checkpoint(order,&refs,&bytes,error);
    qa_buffer_free(&bytes); return ok;
}
bool frontend_materials_restore(qa_frontend *f,frontend_scene_namespace *space,qa_bytes bytes,qa_error *error)
{
    if (!space) return frontend_fail(error,QA_ERROR_ARGUMENT,"Material restore requires its actual imported scene dictionary");
    material_owner *owners=NULL; size_t count=0, held=0; qa_source_save_io io={0}; qa_bytes order_bytes={0};
    qa_material_library **catalogs=NULL; material_saved *saved=NULL; qa_material_order *order=NULL;
    bool has_order=f && f->order!=NULL, expected_order=has_order;
    bool ok=collect(f,true,&owners,&count,error) && empty_order(f->order,error) &&
        qa_source_save_reader(&io,NULL,bytes,error) && header(&io,count,&has_order) && has_order==expected_order;
    if (ok && count) {
        catalogs=calloc(count,sizeof(*catalogs)); saved=calloc(count,sizeof(*saved));
        if (!catalogs || !saved) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining detached material catalog imports");
    }
    for (size_t i=0;ok && i<count;++i)
        ok=metadata(&io,f,owners+i) && read_blob(&io,&saved[i].catalog) && read_blob(&io,&saved[i].records);
    ok=ok && read_blob(&io,&order_bytes) && qa_source_save_finish(&io,NULL) && (has_order?order_bytes.size!=0:order_bytes.size==0);
    qa_application_content_graph *graph=ok?qa_application_content_graph_read(f->application):NULL;
    for (size_t i=0;ok && i<count;++i) {
        material_scope scope={space,graph,owners+i}; qa_material_library_checkpoint_refs refs=references(&scope);
        ok=qa_material_library_catalog_restore(owners[i].images,saved[i].catalog,&refs,catalogs+i,error);
    }
    for (size_t i=0;ok && i<count;++i) {
        material_scope scope={space,graph,owners+i}; qa_material_library_checkpoint_refs refs=references(&scope);
        ok=qa_material_library_restore_into_empty(owners[i].library,catalogs[i],saved[i].records,&refs,error) &&
            frontend_scene_namespace_bind_library(space,i+1,owners[i].library,error);
    }
    while (ok && held<count) {
        ok=qa_material_library_capture_begin(owners[held].library,error);
        if (ok) ++held;
    }
    qa_material_checkpoint_refs refs={space,frontend_scene_material_encode,frontend_scene_material_mutable_decode};
    if (ok && has_order) ok=qa_material_order_restore(order_bytes,&refs,&order,error);
    while (held) qa_material_library_capture_end(owners[--held].library);
    if (ok && has_order) {
        qa_material_order_destroy(f->order); f->order=order; order=NULL;
        for (size_t i=0;ok && i<count;++i) ok=qa_material_library_bind_order(owners[i].library,f->order,error);
    }
    if (catalogs) for (size_t i=0;i<count;++i) qa_material_library_destroy(catalogs[i]);
    qa_material_order_destroy(order); free(catalogs); free(saved); free(owners); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved frontend material ownership graph");
    return ok;
}
