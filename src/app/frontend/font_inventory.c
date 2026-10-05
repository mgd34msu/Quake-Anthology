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

typedef struct font_owner {
    qa_font_library *library;
    const qa_vfs *files;
    uint32_t kind;
    uint64_t ordinal, identity, view, pool;
} font_owner;
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
static bool font_encode(const font_owner *owners, size_t count, const qa_font *font,
    uint64_t *out, qa_error *error)
{
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font encoder output is absent");
    if (!font) { *out=0; return true; }
    uint64_t key=0; bool ok=true, found=false;
    for (size_t i=0;ok && i<count && !found;++i) {
        size_t fonts=qa_font_library_record_count(owners[i].library);
        if (fonts>UINT64_MAX-key) { ok=false; break; }
        for (size_t j=0;j<fonts;++j) if (qa_font_library_record_at(owners[i].library,j)==font) {
            key+=j+1; found=true; break;
        }
        if (!found) key+=fonts;
    }
    if (!ok || !found) return frontend_fail(error,QA_ERROR_FORMAT,"Font reference is outside its actual library inventory");
    *out=key; return true;
}
static bool font_decode(const font_owner *owners, size_t count, uint64_t key,
    const qa_font **out, qa_error *error)
{
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font decoder output is absent");
    if (!key) { *out=NULL; return true; }
    const qa_font *font=NULL;
    for (size_t i=0;i<count;++i) {
        size_t fonts=qa_font_library_record_count(owners[i].library);
        if (key<=fonts) { font=qa_font_library_record_at(owners[i].library,(size_t)key-1); break; }
        key-=fonts;
    }
    if (!font) return frontend_fail(error,QA_ERROR_FORMAT,"Saved font row is absent from restored actual libraries");
    *out=font; return true;
}
bool frontend_font_encode(void *context, const qa_font *font, uint64_t *out, qa_error *error)
{
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font encoder output is absent");
    if (!font) { *out=0; return true; }
    font_owner *owners=NULL; size_t count=0;
    bool ok=collect(context,&owners,&count,error);
    if (ok) ok=font_encode(owners,count,font,out,error);
    else frontend_fail(error,QA_ERROR_FORMAT,"Font reference is outside its actual library inventory");
    free(owners); return ok;
}
bool frontend_font_decode(void *context, uint64_t key, const qa_font **out, qa_error *error)
{
    if (!out) return frontend_fail(error,QA_ERROR_ARGUMENT,"Font decoder output is absent");
    if (!key) { *out=NULL; return true; }
    font_owner *owners=NULL; size_t count=0;
    bool ok=collect(context,&owners,&count,error);
    if (ok) ok=font_decode(owners,count,key,out,error);
    else frontend_fail(error,QA_ERROR_FORMAT,"Saved font row is absent from restored actual libraries");
    free(owners); return ok;
}
