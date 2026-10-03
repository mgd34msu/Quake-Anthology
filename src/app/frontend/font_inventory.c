#include "font_inventory.h"
#include "component_scene.h"
#include "qa/font_save.h"
#include "qa/persistence_content.h"
#include "qa/scene_resource_save.h"
#include "native_q2_save.h"
#include "native_q3_client.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "remote_q2_restore.h"
#include "unified_media_inventory.h"
#include "ui_features_private.h"

typedef struct font_owner {
    qa_font_library *library;
    const qa_vfs *files;
    uint32_t kind;
    uint64_t ordinal, identity, view, pool;
} font_owner;
typedef struct font_scope {
    frontend_scene_namespace *space;
    qa_application_content_graph *graph;
    const font_owner *owner;
} font_scope;

static bool append(font_owner *owners, size_t *count, qa_application_content_graph *graph,
    qa_font_library *library, qa_scene_resources *images, const qa_vfs *files,
    uint32_t kind, uint64_t ordinal, uint64_t identity, qa_error *error)
{
    if (!library) return true;
    uint64_t view=qa_application_content_view_id(graph,files);
    uint64_t pool=qa_application_content_pool_id(graph,qa_vfs_resources(files));
    if (!images || !files || !view || !pool || qa_font_library_resource_owner(library)!=images ||
        qa_scene_resources_files(images)!=files)
        return frontend_fail(error,QA_ERROR_FORMAT,"Font library leaves its actual image/content owners");
    for (size_t i=0;i<*count;++i) if (owners[i].library==library)
        return frontend_fail(error,QA_ERROR_FORMAT,"Font libraries repeat destructor authority");
    owners[(*count)++]=(font_owner){library,files,kind,ordinal,identity,view,pool};
    return true;
}
static bool collect(qa_frontend *frontend, font_owner **out, size_t *count, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || *out || !count || *count)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Font inventory requires idle actual frontend owners and empty output");
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    size_t groups=frontend_source_group_count(frontend), native=frontend_native_q2_owner_count(frontend);
    size_t q3=frontend_native_q3_count(frontend);
    if (!graph || groups==SIZE_MAX || native>SIZE_MAX-groups-1 || q3>SIZE_MAX-groups-native-1 ||
        groups+native+q3+1>SIZE_MAX/sizeof(font_owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Font inventory requires a bounded genuine content graph");
    size_t capacity=groups+native+q3+1,remote=frontend_remote_q3_count(frontend);
    frontend_network_initial_graph_view initial;
    if(remote>SIZE_MAX-capacity || !frontend_network_initial_graph_read(frontend,&initial,error)) return false;
    capacity+=remote;
    if(initial.present) { if(capacity==SIZE_MAX) return false; ++capacity; }
    size_t q2=frontend_remote_q2_count(frontend);
    if(q2>SIZE_MAX-capacity) return false;
    capacity+=q2;
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(frontend,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(frontend,i,&media,error)) return false;
        size_t banks=media?frontend_unified_media_bank_count(media):0;
        if(banks>SIZE_MAX-capacity) return false;
        capacity+=banks;
    }
    size_t components=frontend_component_scene_count(frontend);
    if(components>SIZE_MAX-capacity) return false;
    capacity+=components;
    if(capacity>SIZE_MAX/sizeof(font_owner))
        return frontend_fail(error,QA_ERROR_MEMORY,"Remote font inventory exceeds address space");
    font_owner *owners=calloc(capacity,sizeof(*owners));
    if (!owners) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual font library owners");
    bool ok=append(owners,count,graph,frontend->fonts,frontend->ui_images,frontend->ui_mounts,0,0,0,error);
    for (size_t i=0;ok && i<groups;++i) {
        frontend_source_group_view group;
        ok=frontend_source_group_read(frontend,i,&group) && group.fonts;
        if (ok) ok=append(owners,count,graph,group.fonts,group.images,group.mounts,1,i,group.identity,error);
    }
    for (size_t i=0;ok && i<native;++i) {
        frontend_native_q2_owner_view owner;
        ok=frontend_native_q2_owner_read(frontend,i,&owner);
        if (ok) ok=append(owners,count,graph,owner.fonts,owner.images,owner.mounts,2,i,owner.identity,error);
    }
    for (size_t i=0;ok && i<q3;++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(frontend,i,&owner,error) && owner.fonts;
        if (ok) ok=append(owners,count,graph,owner.fonts,owner.images,owner.mounts,3,i,owner.identity,error);
    }
    for(size_t i=0;ok && i<q2;++i) {
        frontend_remote_q2_view owner; frontend_remote_q2 *row=frontend_remote_q2_at(frontend,i);
        ok=frontend->source_restoring?frontend_remote_q2_import_read(row,&owner,error):
            frontend_remote_q2_metadata_read(row,&owner,error);
        if(ok) ok=append(owners,count,graph,frontend_remote_q2_fonts(row),owner.images,owner.content.mounts,6,i,owner.identity,error);
    }
    for(size_t i=0;ok && i<remote;++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(frontend,i),&owner,error) && owner.fonts;
        if(ok) ok=append(owners,count,graph,owner.fonts,owner.images,owner.mounts,4,i,owner.identity,error);
    }
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.fonts;
        if(ok) ok=append(owners,count,graph,owner.fonts,owner.images,owner.mounts,5,0,owner.identity,error);
    }
    for(size_t i=0;ok && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        ok=frontend_unified_media_inventory_at(frontend,i,&media,error);
        for(size_t j=0;ok && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank; uint64_t key;
            ok=frontend_unified_media_bank_read(media,j,&bank) && frontend_unified_media_bank_key(i,j,&key) &&
                append(owners,count,graph,bank.fonts,bank.images,bank.files,7,key,0,error);
        }
    }
    for(size_t i=0;ok && i<components;++i) {
        frontend_component_scene_view row;
        ok=frontend_component_scene_metadata_read(frontend,i,&row,error) &&
            append(owners,count,graph,row.fonts,row.images,row.files,8,i,row.identity,error);
    }
    if (!ok) {
        free(owners); *count=0;
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Source font library is not fully constructed");
        return false;
    }
    *out=owners; return true;
}
bool frontend_font_encode(void *context, const qa_font *font, uint64_t *out, qa_error *error)
{
    qa_frontend *frontend=context;
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font encoder output is absent");
    if (!font) { *out=0; return true; }
    font_owner *owners=NULL; size_t count=0; uint64_t key=0;
    bool ok=collect(frontend,&owners,&count,error), found=false;
    for (size_t i=0;ok && i<count && !found;++i) {
        size_t fonts=qa_font_library_record_count(owners[i].library);
        if (fonts>UINT64_MAX-key) { ok=false; break; }
        for (size_t j=0;j<fonts;++j) if (qa_font_library_record_at(owners[i].library,j)==font) {
            key+=j+1; found=true; break;
        }
        if (!found) key+=fonts;
    }
    free(owners);
    if (!ok || !found) return frontend_fail(error,QA_ERROR_FORMAT,"Font reference is outside its actual library inventory");
    *out=key; return true;
}
bool frontend_font_decode(void *context, uint64_t key, const qa_font **out, qa_error *error)
{
    qa_frontend *frontend=context;
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font decoder output is absent");
    if (!key) { *out=NULL; return true; }
    font_owner *owners=NULL; size_t count=0;
    bool ok=collect(frontend,&owners,&count,error); const qa_font *font=NULL;
    for (size_t i=0;ok && i<count;++i) {
        size_t fonts=qa_font_library_record_count(owners[i].library);
        if (key<=fonts) { font=qa_font_library_record_at(owners[i].library,(size_t)key-1); break; }
        key-=fonts;
    }
    free(owners);
    if (!ok || !font) return frontend_fail(error,QA_ERROR_FORMAT,"Saved font row is absent from restored actual libraries");
    *out=font; return true;
}
static bool image_encode(void *context, const qa_scene_image *image, uint64_t *out, qa_error *error)
{ return frontend_scene_image_encode(((font_scope *)context)->space,image,out,error); }
static bool image_decode(void *context, uint64_t key, const qa_scene_image **out, qa_error *error)
{ return frontend_scene_image_decode(((font_scope *)context)->space,key,out,error); }
static bool resource_encode(void *context, const qa_resource *resource, uint64_t *out, qa_error *error)
{
    font_scope *scope=context; uint64_t pool=0, version=0;
    if (!out || !qa_application_content_resource_id(scope->graph,resource,&pool,&version) || pool!=scope->owner->pool)
        return frontend_fail(error,QA_ERROR_FORMAT,"Font source is outside its actual qualified content pool");
    *out=version; return true;
}
static bool resource_decode(void *context, uint64_t key, const qa_resource **out, qa_error *error)
{
    font_scope *scope=context;
    const qa_resource *resource=qa_application_content_resource(scope->graph,scope->owner->pool,key);
    if (!out || !resource) return frontend_fail(error,QA_ERROR_FORMAT,"Saved font source is absent from its qualified pool");
    *out=resource; return true;
}
static bool metadata(qa_source_save_io *io, const font_owner *owner)
{
    font_owner saved=*owner;
    return qa_source_save_u32(io,&saved.kind) && qa_source_save_u64(io,&saved.ordinal) &&
        qa_source_save_u64(io,&saved.identity) && qa_source_save_u64(io,&saved.view) && qa_source_save_u64(io,&saved.pool) &&
        saved.kind==owner->kind && saved.ordinal==owner->ordinal && saved.identity==owner->identity &&
        saved.view==owner->view && saved.pool==owner->pool;
}
static bool root_font(const qa_font_library *library, const qa_font *font)
{
    if (!library) return font==NULL;
    for (size_t i=0;i<qa_font_library_record_count(library);++i)
        if (qa_font_library_record_at(library,i)==font) return true;
    return false;
}
static bool fields(qa_source_save_io *io, qa_frontend *frontend, frontend_scene_namespace *space,
    const font_owner *owners, size_t count)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','F','F','O'}; size_t saved_count=count;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFFO",4) || !qa_source_save_count(io,&saved_count,SIZE_MAX) || saved_count!=count) return false;
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    for (size_t i=0;i<count;++i) {
        if (!metadata(io,owners+i)) return false;
        font_scope scope={space,graph,owners+i};
        qa_font_checkpoint_refs refs={&scope,image_encode,image_decode,resource_encode,resource_decode};
        qa_buffer saved={0}; size_t size=0;
        bool ok=reading || qa_font_library_checkpoint(owners[i].library,&refs,&saved,io->error);
        if (!reading) size=saved.size;
        ok=ok && qa_source_save_count(io,&size,reading?io->input.size-io->offset:SIZE_MAX);
        if (ok && reading) {
            if (size>io->input.size-io->offset) ok=false;
            else {
                qa_bytes bytes={io->input.data+io->offset,size}; io->offset+=size;
                ok=qa_font_library_restore(owners[i].library,bytes,&refs,io->error);
            }
        } else if (ok) ok=qa_source_save_bytes(io,saved.data,size);
        qa_buffer_free(&saved); if (!ok) return false;
    }
    uint64_t classic=0, primary=0;
    if (!reading && (!root_font(frontend->fonts,frontend->classic) || !root_font(frontend->fonts,frontend->primary))) return false;
    if (!reading && (!frontend_font_encode(frontend,frontend->classic,&classic,io->error) ||
        !frontend_font_encode(frontend,frontend->primary,&primary,io->error))) return false;
    if (!qa_source_save_u64(io,&classic) || !qa_source_save_u64(io,&primary) ||
        (frontend->fonts!=NULL)!=(classic!=0) || (frontend->fonts!=NULL)!=(primary!=0)) return false;
    if (reading && (!frontend_font_decode(frontend,classic,&frontend->classic,io->error) ||
        !frontend_font_decode(frontend,primary,&frontend->primary,io->error))) return false;
    if (!root_font(frontend->fonts,frontend->classic) || !root_font(frontend->fonts,frontend->primary) || !frontend->ui_features) return false;
    frontend_ui_features *features=frontend->ui_features;
    uint64_t bold=0,console=0;
    if (!reading && (!root_font(frontend->fonts,features->bold) ||
        (features->console && !root_font(frontend->fonts,features->console)) ||
        !frontend_font_encode(frontend,features->bold,&bold,io->error) ||
        !frontend_font_encode(frontend,features->console,&console,io->error))) return false;
    if (!qa_source_save_u64(io,&bold) || !qa_source_save_u64(io,&console) || (frontend->fonts!=NULL)!=(bold!=0)) return false;
    if (reading && (!frontend_font_decode(frontend,bold,&features->bold,io->error) ||
        !frontend_font_decode(frontend,console,&features->console,io->error))) return false;
    size_t fallbacks=features->fallback_count,capacity=features->fallback_capacity;
    if (!qa_source_save_count(io,&fallbacks,SIZE_MAX/sizeof(*features->fallbacks)) ||
        !qa_source_save_count(io,&capacity,SIZE_MAX/sizeof(*features->fallbacks)) || capacity<fallbacks) return false;
    for (size_t i=0;i<capacity;++i) { uint8_t zero=0; if (!qa_source_save_u8(io,&zero) || zero) return false; }
    if (reading) {
        if (features->fallbacks || features->fallback_count || features->fallback_capacity) return false;
        features->fallbacks=capacity?calloc(capacity,sizeof(*features->fallbacks)):NULL;
        if (capacity && !features->fallbacks) return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual menu font fallback roots");
        features->fallback_capacity=capacity;
    }
    if (fallbacks && !features->fallbacks) return false;
    for (size_t i=0;i<fallbacks;++i) {
        uint64_t key=0;
        if (!reading && !frontend_font_encode(frontend,features->fallbacks[i],&key,io->error)) return false;
        if (!qa_source_save_u64(io,&key) || !key) return false;
        if (reading && !frontend_font_decode(frontend,key,features->fallbacks+i,io->error)) return false;
        if (!root_font(frontend->fonts,features->fallbacks[i])) return false;
        for (size_t j=0;j<i;++j) if (features->fallbacks[j]==features->fallbacks[i]) return false;
        if (reading) ++features->fallback_count;
    }
    return root_font(frontend->fonts,features->bold) && (!features->console || root_font(frontend->fonts,features->console));
}
bool frontend_fonts_checkpoint(qa_frontend *frontend, frontend_scene_namespace *space, qa_buffer *out, qa_error *error)
{
    if (!space || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Font capture requires shared scene inventory and empty output");
    font_owner *owners=NULL; size_t count=0; qa_source_save_io io={0};
    bool ok=collect(frontend,&owners,&count,error) && qa_source_save_writer(&io,NULL,error) &&
        fields(&io,frontend,space,owners,count) && qa_source_save_finish(&io,out);
    free(owners); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid genuine frontend font owner");
    return ok;
}
bool frontend_fonts_restore(qa_frontend *frontend, frontend_scene_namespace *space, qa_bytes bytes, qa_error *error)
{
    if (!space) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font restore requires the imported shared scene inventory");
    font_owner *owners=NULL; size_t count=0; qa_source_save_io io={0};
    bool ok=collect(frontend,&owners,&count,error) && qa_source_save_reader(&io,NULL,bytes,error) &&
        fields(&io,frontend,space,owners,count) && qa_source_save_finish(&io,NULL);
    free(owners); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved frontend font ownership");
    return ok;
}
