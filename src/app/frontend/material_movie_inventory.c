#include "material_movie_inventory.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "network_initial_graph.h"
#include "root_resources.h"
#include "renderer_materials.h"
#include "remote_q2_material_movies_bridge.h"
#include "component_scene.h"
#include "unified_media_inventory.h"
#include "remote_unified_material_movies_bridge.h"
#include "equipment_media.h"
#include "source_cinematics.h"
#include "cinematic_roles.h"
#include "remote_q3_modules.h"
#include "qa/q3_presentation_save.h"
#include "qa/media_library_save.h"
#include "qa/media_resource.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef enum movie_kind { MOVIE_SOURCE,MOVIE_NATIVE,MOVIE_REMOTE,MOVIE_INITIAL,MOVIE_VISUAL,MOVIE_FRONTEND,MOVIE_RENDERER,MOVIE_REMOTE_Q2,MOVIE_COMPONENT,MOVIE_UNIFIED,MOVIE_SOURCE_SLOT,MOVIE_KIND_COUNT } movie_kind;
typedef struct movie_row {
    movie_kind kind;
    size_t ordinal;
    frontend_material_movie_source source;
    frontend_material_movies *owner;
    const qa_resource **resources;
    size_t resource_count;
    qa_bytes cache,state;
} movie_row;
typedef struct movie_scope {
    qa_application_content_graph *graph;
    frontend_scene_namespace *space;
    const qa_scene_frame_checkpoint_refs *frames;
    movie_row *row;
    qa_frontend *frontend;
    const qa_audio_checkpoint_refs *audio;
} movie_scope;
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)
        return !size || (bytes->data && qa_source_save_bytes(io,(void *)bytes->data,size));
    if(io->offset>io->input.size || size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool source_read(qa_frontend *f,movie_kind kind,size_t ordinal,
    frontend_material_movie_source *out,qa_error *error)
{
    switch(kind) {
    case MOVIE_SOURCE: return frontend_source_movie_source_read(f,ordinal,out,error);
    case MOVIE_NATIVE: return frontend_native_q3_movie_source_read(f,ordinal,out,error);
    case MOVIE_REMOTE: return frontend_remote_q3_movie_source_read(frontend_remote_q3_at(f,ordinal),out,error);
    case MOVIE_INITIAL: return frontend_remote_q3_initial_movie_source_read(frontend_remote_q3_initial_at(f,ordinal),out,error);
    case MOVIE_VISUAL: return frontend_visual_movie_source_read(f,ordinal,out,error);
    case MOVIE_FRONTEND: return !ordinal && frontend_root_movie_source_read(f,out,error);
    case MOVIE_RENDERER: return frontend_renderer_materials_movie_source_at(f,ordinal,out,error);
    case MOVIE_REMOTE_Q2: return frontend_remote_q2_movie_source_read(frontend_remote_q2_material_movie_at(f,ordinal),out,error);
    case MOVIE_COMPONENT: {
        frontend_component_scene_view row;
        return frontend_component_scene_metadata_read(f,ordinal,&row,error) &&
            frontend_component_scene_movie_source_read(f,row.identity,out,error);
    }
    case MOVIE_UNIFIED: {
        size_t media_index,bank; frontend_unified_media *media=NULL;
        return frontend_unified_media_bank_key_read(ordinal,&media_index,&bank) &&
            frontend_unified_media_inventory_at(f,media_index,&media,error) && media &&
            frontend_unified_material_movie_source_read(media,bank,out,error);
    }
    case MOVIE_SOURCE_SLOT: return frontend_equipment_movie_source_read(f,ordinal,out,error);
    case MOVIE_KIND_COUNT: break;
    }
    return false;
}
static void rows_free(movie_row *rows,size_t count)
{ for(size_t i=0;rows && i<count;++i) free(rows[i].resources); free(rows); }
static bool unified_movie_key(qa_frontend *f,size_t ordinal,size_t *key,qa_error *error)
{
    size_t count;
    if(!frontend_unified_media_inventory_count(f,&count,error)) return false;
    for(size_t i=0;i<count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        size_t rows=media?frontend_unified_material_movie_count(media):0;
        if(ordinal>=rows) { ordinal-=rows; continue; }
        size_t bank; uint64_t encoded;
        if(!frontend_unified_material_movie_at(media,ordinal,&bank) ||
            !frontend_unified_media_bank_key(i,bank,&encoded) || encoded>SIZE_MAX) return false;
        *key=(size_t)encoded; return true;
    }
    return false;
}
static bool private_cache(movie_kind kind)
{ return kind==MOVIE_VISUAL || kind==MOVIE_FRONTEND || kind==MOVIE_RENDERER || kind==MOVIE_REMOTE_Q2 || kind==MOVIE_UNIFIED || kind==MOVIE_SOURCE_SLOT; }
static bool collect(qa_frontend *f,movie_row **out,size_t *count,qa_error *error)
{
    size_t retained_count=0;
    if(!frontend_renderer_materials_movie_count(f,&retained_count,error)) return false;
    size_t counts[]={frontend_source_group_count(f),frontend_native_q3_count(f),
        frontend_remote_q3_count(f),frontend_remote_q3_initial_count(f),frontend_visual_owner_count(f),
        f->root_resources?1:0,retained_count,frontend_remote_q2_material_movie_count(f),
        frontend_component_scene_count(f),0,frontend_equipment_movie_count(f)};
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        size_t rows=media?frontend_unified_material_movie_count(media):0;
        if(rows>SIZE_MAX-counts[MOVIE_UNIFIED]) return false;
        counts[MOVIE_UNIFIED]+=rows;
    }
    size_t total=0;
    for(size_t i=0;i<MOVIE_KIND_COUNT;++i) {
        if(counts[i]>SIZE_MAX/sizeof(movie_row)-total) return false;
        total+=counts[i];
    }
    movie_row *rows=total?calloc(total,sizeof(*rows)):NULL;
    if(total && !rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual shader-movie provider rows");
    size_t at=0;
    for(size_t kind=0;kind<MOVIE_KIND_COUNT;++kind) for(size_t i=0;i<counts[kind];++i) {
        movie_row *row=rows+at; row->kind=(movie_kind)kind; row->ordinal=i;
        if((row->kind==MOVIE_UNIFIED && !unified_movie_key(f,i,&row->ordinal,error)) ||
            (row->kind==MOVIE_SOURCE_SLOT && !frontend_equipment_movie_at(f,i,&row->ordinal)) ||
            !source_read(f,row->kind,row->ordinal,&row->source,error)) { rows_free(rows,total); return false; }
        if(row->kind==MOVIE_SOURCE && f->options.dedicated && !row->source.images &&
            !row->source.materials && !row->source.media) continue;
        if(!row->source.files || !row->source.images || !row->source.materials || !row->source.media) {
            rows_free(rows,total); return frontend_fail(error,QA_ERROR_FORMAT,"Shader-movie provider has an incomplete actual heap tuple");
        }
        for(size_t j=0;j<at;++j) if(rows[j].source.materials==row->source.materials ||
            rows[j].source.media==row->source.media) {
            rows_free(rows,total); return frontend_fail(error,QA_ERROR_FORMAT,"Shader-movie providers repeat an actual heap owner");
        }
        ++at;
    }
    *out=rows; *count=at; return true;
}
static bool roster_matches(qa_frontend *f,movie_row *rows,size_t count,qa_error *error)
{
    size_t actual=0;
    if(!frontend_material_movies_roster_count(f,&actual,error) || actual!=count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Shader-movie inventory does not cover its actual provider roster");
    for(size_t i=0;i<actual;++i) {
        frontend_material_movies *owner=NULL; frontend_material_movie_source source;
        if(!frontend_material_movies_roster_at(f,i,&owner,error) ||
            !frontend_material_movies_source_read(owner,&source,error)) return false;
        size_t matches=0;
        for(size_t j=0;j<count;++j) {
            frontend_material_movie_source *expected=&rows[j].source;
            if(rows[j].owner==owner && source.frontend==expected->frontend &&
                source.files==expected->files && source.images==expected->images &&
                source.materials==expected->materials && source.media==expected->media &&
                source.context==expected->context && source.current==expected->current) ++matches;
        }
        if(matches!=1) return frontend_fail(error,QA_ERROR_FORMAT,"Shader-movie owner leaves its exact typed provider row");
    }
    return true;
}
static bool image_encode(void *context,const qa_scene_image *image,uint64_t *out,qa_error *error)
{ return frontend_scene_image_encode(((movie_scope *)context)->space,image,out,error); }
static bool image_decode(void *context,uint64_t key,const qa_scene_image **out,qa_error *error)
{ return frontend_scene_image_decode(((movie_scope *)context)->space,key,out,error); }
static bool resource_encode(void *context,const qa_resource *resource,uint64_t *out,qa_error *error)
{
    movie_row *row=((movie_scope *)context)->row;
    for(size_t i=0;out && i<row->resource_count;++i)
        if(row->resources[i]==resource) { *out=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Visual movie resource leaves its actual cache roster");
}
static bool resource_decode(void *context,uint64_t key,const qa_resource **out,qa_error *error)
{
    movie_row *row=((movie_scope *)context)->row;
    if(!out || !key || key>row->resource_count) return frontend_fail(error,QA_ERROR_FORMAT,"Saved visual movie resource has no cache row");
    *out=row->resources[key-1]; return true;
}
static bool asset_encode(void *context,const qa_cinematic_asset *asset,uint64_t *out,qa_error *error)
{
    qa_media_library *media=((movie_scope *)context)->row->source.media;
    for(size_t i=0;out && i<qa_media_library_record_count(media);++i)
        if(qa_media_library_record_at(media,i)==asset) { *out=i+1; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Shader movie asset leaves its actual provider cache");
}
static bool asset_decode(void *context,uint64_t key,const char *path,const qa_cinematic_asset **out,qa_error *error)
{
    movie_scope *scope=context; movie_row *row=scope->row;
    const qa_cinematic_asset *asset=key && key<=qa_media_library_record_count(row->source.media)?
        qa_media_library_record_at(row->source.media,(size_t)key-1):NULL;
    const qa_resource *resource=qa_cinematic_asset_resource(asset); uint64_t pool=0,version=0;
    if(!out || *out || !path || !*path || !asset || !resource ||
        !qa_application_content_resource_id(scope->graph,resource,&pool,&version) ||
        qa_resource_pool_find(qa_vfs_resources(row->source.files),qa_resource_id(resource))!=resource)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved shader movie lacks its genuine retained cache resource");
    *out=asset; return true;
}
static bool cinematic_encode(void *context,uint64_t bus,qa_buffer *out,qa_error *error)
{
    movie_scope *scope=context;
    return scope->audio && scope->audio->encode &&
        scope->audio->encode(scope->audio->context,QA_AUDIO_REFERENCE_BUS,bus,out,error);
}
static bool cinematic_decode(void *context,uint32_t saved_seat,qa_bytes descriptor,
    qa_q3_cinematic_handles **pool,uint32_t *seat,uint64_t *bus,qa_error *error)
{
    movie_scope *scope=context; uint64_t actual=0; qa_q3_cinematic_handles_options options;
    if(!pool || !seat || !bus || !scope->audio || !scope->audio->decode ||
        saved_seat>=scope->frontend->options.seats ||
        !frontend_source_cinematics_read(scope->frontend,&options,error) ||
        !scope->audio->decode(scope->audio->context,QA_AUDIO_REFERENCE_BUS,descriptor,&actual,error)) return false;
    *pool=scope->frontend->source_cinematics; *seat=saved_seat; *bus=actual; return true;
}
static frontend_material_movies_refs movie_refs(movie_scope *scope)
{
    return (frontend_material_movies_refs){.context=scope,.asset_encode=asset_encode,.asset_decode=asset_decode,
        .cinematic_encode=cinematic_encode,.cinematic_decode=cinematic_decode,
        .images={scope,image_encode,image_decode},.frames=*scope->frames};
}
static bool resource_fields(qa_source_save_io *io,movie_scope *scope)
{
    movie_row *row=scope->row; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool owns_cache=private_cache(row->kind);
    size_t count=reading?0:(owns_cache?qa_media_library_record_count(row->source.media):0);
    if(!qa_source_save_count(io,&count,SIZE_MAX/sizeof(*row->resources)) ||
        (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/16)) ||
        (!owns_cache && count)) return false;
    row->resources=count?calloc(count,sizeof(*row->resources)):NULL; row->resource_count=count;
    if(count && !row->resources) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining actual visual movie resources");
    for(size_t i=0;i<count;++i) {
        uint64_t pool=0,version=0;
        const qa_resource *resource=reading?NULL:qa_cinematic_asset_resource(qa_media_library_record_at(row->source.media,i));
        if((!reading && !qa_application_content_resource_id(scope->graph,resource,&pool,&version)) ||
            !qa_source_save_u64(io,&pool) || !pool || !qa_source_save_u64(io,&version) || !version) return false;
        if(reading) resource=qa_application_content_resource(scope->graph,pool,version);
        if(!resource || qa_resource_pool_find(qa_vfs_resources(row->source.files),qa_resource_id(resource))!=resource) return false;
        row->resources[i]=resource;
    }
    return true;
}
static bool restore_owner(qa_frontend *f,movie_row *row,const frontend_material_movies_refs *refs,qa_error *error)
{
    switch(row->kind) {
    case MOVIE_SOURCE: return frontend_source_movies_restore(f,row->ordinal,refs,row->state,error);
    case MOVIE_NATIVE: return frontend_native_q3_movies_restore(f,row->ordinal,refs,row->state,error);
    case MOVIE_REMOTE: return frontend_remote_q3_movies_restore(frontend_remote_q3_at(f,row->ordinal),refs,row->state,error);
    case MOVIE_INITIAL: return frontend_remote_q3_initial_movies_restore(frontend_remote_q3_initial_at(f,row->ordinal),refs,row->state,error);
    case MOVIE_VISUAL: return frontend_visual_movies_restore(f,row->ordinal,refs,row->state,error);
    case MOVIE_FRONTEND: return !row->ordinal && frontend_root_movies_restore(f,refs,row->state,error);
    case MOVIE_RENDERER: return frontend_renderer_materials_movies_restore_at(f,row->ordinal,refs,row->state,error);
    case MOVIE_REMOTE_Q2: return frontend_remote_q2_movies_restore(frontend_remote_q2_material_movie_at(f,row->ordinal),refs,row->state,error);
    case MOVIE_COMPONENT: {
        frontend_component_scene_view owner;
        return frontend_component_scene_metadata_read(f,row->ordinal,&owner,error) &&
            frontend_component_scene_restore_movies(f,owner.identity,refs,row->state,error);
    }
    case MOVIE_UNIFIED: {
        size_t media_index,bank; frontend_unified_media *media=NULL;
        return frontend_unified_media_bank_key_read(row->ordinal,&media_index,&bank) &&
            frontend_unified_media_inventory_at(f,media_index,&media,error) && media &&
            frontend_unified_material_movies_restore(media,bank,refs,row->state,error);
    }
    case MOVIE_SOURCE_SLOT: return frontend_equipment_movies_restore(f,row->ordinal,refs,row->state,error);
    case MOVIE_KIND_COUNT: break;
    }
    return false;
}
typedef struct global_movie_scope {
    qa_frontend *frontend;
    frontend_scene_namespace *space;
    const qa_audio_checkpoint_refs *audio;
    qa_application_content_graph *graph;
    movie_row *rows;
    size_t count;
    frontend_q3_inventory *q3;
} global_movie_scope;
static movie_row *global_source_row(global_movie_scope *scope,const qa_q3_cinematic_source *source,size_t *index,qa_error *error)
{
    const qa_q3_cinematic_source *parent=NULL; uint32_t seat=0; uint64_t bus=0;
    if(qa_q3_cinematic_source_role_read(source,&parent,&seat,&bus)) source=parent;
    for(size_t i=0;i<scope->count;++i) {
        movie_row *row=scope->rows+i; qa_q3_cinematic_source *actual=NULL;
        if(!row->owner || !frontend_material_movies_cinematic_read(row->owner,&actual,error)) return NULL;
        if(actual==source) { if(index) *index=i; return row; }
    }
    frontend_fail(error,QA_ERROR_FORMAT,"Global cinematic source has no actual shader-movie owner"); return NULL;
}
static frontend_remote_q3_modules *global_modules(global_movie_scope *scope,movie_row *row,qa_error *error)
{
    if(row->kind==MOVIE_REMOTE)
        return frontend_remote_q3_modules_read(frontend_remote_q3_at(scope->frontend,row->ordinal));
    if(row->kind==MOVIE_INITIAL) {
        frontend_network_initial_graph_view view;
        if(frontend_network_initial_graph_read(scope->frontend,&view,error) &&
            view.parent==frontend_remote_q3_initial_at(scope->frontend,row->ordinal)) return view.modules;
    }
    return NULL;
}
static bool global_unified_role(global_movie_scope *scope,movie_row *owner,
    const qa_q3_cinematic_source *source,uint64_t wanted,uint64_t *key,
    frontend_unified_q3_runtime_factory **out,qa_error *error)
{
    if(owner->kind!=MOVIE_UNIFIED) return false;
    size_t media_index=0,bank=0; frontend_unified_media *media=NULL;
    if(!frontend_unified_media_bank_key_read(owner->ordinal,&media_index,&bank) ||
        !frontend_unified_media_inventory_at(scope->frontend,media_index,&media,error) || !media) return false;
    uint64_t ordinal=0;
    for(size_t i=0;i<frontend_remote_unified_count(scope->frontend);++i) {
        frontend_remote_unified *replica=frontend_remote_unified_at(scope->frontend,i);
        for(size_t j=0;j<frontend_remote_unified_presentation_q3_client_count(replica);++j) {
            ++ordinal;
            if(wanted && wanted!=ordinal) continue;
            frontend_unified_presentation_q3_row row;
            if(!frontend_remote_unified_presentation_q3_row_read(replica,j,&row,error)) return false;
            if(!row.factory || row.media!=media || row.bank!=bank) continue;
            qa_q3_cinematic_source *actual=NULL;
            if(!frontend_unified_q3_runtime_factory_cinematic_read(row.factory,&actual,error)) return false;
            if(actual && (!source || actual==source)) {
                if(key) *key=ordinal;
                *out=row.factory; return true;
            }
        }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Global cinematic role has no actual compiled Unified factory");
}
static bool global_role(global_movie_scope *scope,const qa_q3_cinematic_source *source,
    frontend_remote_q3_module_topology *out,qa_error *error)
{
    const qa_q3_cinematic_source *parent=NULL; uint32_t seat=0; uint64_t bus=0;
    movie_row *row=global_source_row(scope,source,NULL,error);
    if(!row || !qa_q3_cinematic_source_role_read(source,&parent,&seat,&bus)) return false;
    frontend_remote_q3_modules *modules=global_modules(scope,row,error);
    for(size_t i=0;i<frontend_remote_q3_modules_role_count(modules);++i) {
        frontend_remote_q3_module_topology role; qa_q3_presentation_binding binding;
        if(!frontend_remote_q3_modules_cinematics_role_read(modules,i,&role,error)) return false;
        if(role.physical_seat==seat && role.service_owner==bus &&
            qa_q3_presentation_binding_read(role.presentation,&binding,error) && binding.options.cinematics==source) {
            *out=role; return true;
        }
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Global cinematic role has no actual attached module namespace");
}
static bool global_source_encode(void *context,const qa_q3_cinematic_source *source,uint64_t *out,qa_error *error)
{
    global_movie_scope *scope=context; size_t index=0;
    movie_row *owner=global_source_row(scope,source,&index,error);
    if(!out || !owner || index>=UINT32_MAX) return false;
    const qa_q3_cinematic_source *parent=NULL; uint32_t seat=0; uint64_t bus=0;
    uint64_t role_key=0;
    if(qa_q3_cinematic_source_role_read(source,&parent,&seat,&bus)) {
        for(size_t i=0;i<frontend_cinematic_roles_count(scope->frontend);++i) {
            frontend_cinematic_role_view detached;
            if(!frontend_cinematic_roles_read(scope->frontend,i,&detached,error)) return false;
            if(detached.cinematics==source) {
                if(i>=UINT32_C(0x7fffffff)) return false;
                role_key=UINT32_C(0x80000000)|(uint64_t)(i+1); break;
            }
        }
        if(!role_key) {
            if(owner->kind==MOVIE_UNIFIED) {
                frontend_unified_q3_runtime_factory *factory=NULL; uint64_t ordinal=0;
                if(!global_unified_role(scope,owner,source,0,&ordinal,&factory,error) ||
                    !ordinal || ordinal>UINT32_C(0x3fffffff)) return false;
                role_key=UINT32_C(0x40000000)|ordinal;
            } else {
                frontend_remote_q3_module_topology role;
                if(!global_role(scope,source,&role,error) || role.index>=UINT32_C(0x3fffffff)) return false;
                role_key=role.index+1;
            }
        }
    }
    *out=(role_key<<32)|(uint64_t)(index+1); return true;
}
static bool global_source_decode(void *context,uint64_t key,qa_q3_cinematic_source **out,qa_error *error)
{
    global_movie_scope *scope=context; uint64_t index=key&UINT32_MAX,role_key=key>>32;
    if(!out || !index || index>scope->count) return false;
    movie_row *row=scope->rows+index-1; qa_q3_cinematic_source *parent=NULL;
    if(!row->owner || !frontend_material_movies_cinematic_read(row->owner,&parent,error) || !parent ||
        qa_q3_cinematic_source_handles(parent)!=scope->frontend->source_cinematics) return false;
    if(!role_key) { *out=parent; return true; }
    if(role_key&UINT32_C(0x80000000)) {
        uint64_t detached_key=role_key&UINT32_C(0x7fffffff);
        frontend_cinematic_role_view detached;
        if(!detached_key || detached_key-1>SIZE_MAX ||
            !frontend_cinematic_roles_read(scope->frontend,(size_t)detached_key-1,&detached,error) ||
            detached.parent!=row->owner || !detached.cinematics ||
            qa_q3_cinematic_source_parent(detached.cinematics)!=parent) return false;
        *out=detached.cinematics; return true;
    }
    if(role_key&UINT32_C(0x40000000)) {
        frontend_unified_q3_runtime_factory *factory=NULL;
        uint64_t ordinal=role_key&UINT32_C(0x3fffffff);
        if(!ordinal || !global_unified_role(scope,row,NULL,ordinal,NULL,&factory,error) ||
            !frontend_unified_q3_runtime_factory_cinematic_read(factory,out,error) || !*out ||
            qa_q3_cinematic_source_parent(*out)!=parent) return false;
        return true;
    }
    frontend_remote_q3_modules *modules=global_modules(scope,row,error);
    frontend_remote_q3_module_topology role; qa_q3_presentation_binding binding;
    if(role_key-1>SIZE_MAX || !frontend_remote_q3_modules_cinematics_role_read(modules,(size_t)role_key-1,&role,error) ||
        !qa_q3_cinematic_source_role_find(parent,role.physical_seat,role.service_owner,out) ||
        !qa_q3_presentation_binding_read(role.presentation,&binding,error) || binding.options.cinematics!=*out) return false;
    return true;
}
static bool global_asset_encode(void *context,const qa_q3_cinematic_source *source,
    const qa_cinematic_asset *asset,uint64_t *out,qa_error *error)
{
    global_movie_scope *scope=context; movie_row *row=global_source_row(scope,source,NULL,error);
    if(!row) return false;
    movie_scope local={.row=row}; return asset_encode(&local,asset,out,error);
}
static bool global_asset_decode(void *context,const qa_q3_cinematic_source *source,
    uint64_t key,const char *path,qa_cinematic_asset **out,qa_error *error)
{
    global_movie_scope *scope=context; movie_row *row=global_source_row(scope,source,NULL,error);
    const qa_cinematic_asset *asset=NULL;
    movie_scope local={.row=row,.graph=scope->graph};
    if(!row || !out || *out || !asset_decode(&local,key,path,&asset,error)) return false;
    qa_cinematic_asset_retain((qa_cinematic_asset *)asset); *out=(qa_cinematic_asset *)asset; return true;
}
static bool global_bus_decode(void *context,const qa_q3_cinematic_source *source,uint64_t saved,uint64_t *out,qa_error *error)
{
    (void)saved; global_movie_scope *scope=context; uint32_t seat=0;
    const qa_q3_cinematic_source *parent=NULL; uint64_t bus=0;
    if(qa_q3_cinematic_source_role_read(source,&parent,&seat,&bus)) { *out=bus; return true; }
    movie_row *row=global_source_row(scope,source,NULL,error);
    /* QFMM decoded the portable BUS receipt before constructing this source. */
    return row && out && frontend_material_movies_cinematic_parameters(row->owner,&seat,out,error);
}
static bool global_source_clock(void *context,const qa_q3_cinematic_source *source,double *out,qa_error *error)
{
    movie_row *row=global_source_row(context,source,NULL,error);
    return row && out && frontend_material_movies_cinematic_clock_read(row->owner,out,error);
}
static bool global_image_encode(void *context,const qa_scene_image *image,uint64_t *out,qa_error *error)
{ return frontend_scene_image_encode(((global_movie_scope *)context)->space,image,out,error); }
static bool global_image_decode(void *context,uint64_t key,const qa_scene_image **out,qa_error *error)
{ return frontend_scene_image_decode(((global_movie_scope *)context)->space,key,out,error); }
static bool global_target_encode(void *context,uint64_t target,qa_buffer *out,qa_error *error)
{
    const qa_audio_checkpoint_refs *audio=((global_movie_scope *)context)->audio;
    return audio && audio->encode && audio->encode(audio->context,QA_AUDIO_REFERENCE_BUS,target,out,error);
}
static bool global_target_decode(void *context,qa_bytes bytes,uint64_t *out,qa_error *error)
{
    const qa_audio_checkpoint_refs *audio=((global_movie_scope *)context)->audio;
    return audio && audio->decode && audio->decode(audio->context,QA_AUDIO_REFERENCE_BUS,bytes,out,error);
}
typedef struct global_system_scope {
    global_movie_scope *global;
    const qa_q3_cinematic_source *source;
} global_system_scope;
static bool system_asset_encode(void *context,const qa_cinematic_asset *asset,uint64_t *out,qa_error *error)
{
    global_system_scope *scope=context;
    return global_asset_encode(scope->global,scope->source,asset,out,error);
}
static bool system_asset_decode(void *context,uint64_t key,const char *path,qa_cinematic_asset **out,qa_error *error)
{
    global_system_scope *scope=context;
    return global_asset_decode(scope->global,scope->source,key,path,out,error);
}
static qa_q3_movie_checkpoint_refs system_movie_refs(global_system_scope *scope)
{
    return (qa_q3_movie_checkpoint_refs){.context=scope,.asset_encode=system_asset_encode,.asset_decode=system_asset_decode,
        .playback={scope->global,global_target_encode,global_target_decode},
        .publication={scope->global,global_image_encode,global_image_decode}};
}
static bool global_system_encode(void *context,const qa_q3_cinematic_source *source,
    const qa_q3_system_movie *movie,uint32_t flags,qa_buffer *out,qa_error *error)
{
    global_movie_scope *scope=context; frontend_remote_q3_module_topology role;
    movie_row *owner=global_source_row(scope,source,NULL,error);
    if(owner && owner->kind==MOVIE_UNIFIED) {
        frontend_unified_q3_runtime_factory *factory=NULL;
        global_system_scope system={scope,source}; qa_q3_movie_checkpoint_refs refs=system_movie_refs(&system);
        return global_unified_role(scope,owner,source,0,NULL,&factory,error) &&
            frontend_unified_q3_runtime_factory_system_checkpoint(factory,&refs,movie,flags,out,error);
    }
    qa_q3_movie_checkpoint_refs refs;
    return global_role(scope,source,&role,error) && frontend_q3_module_cinematic_refs(scope->q3,&role,&refs,error) &&
        refs.system_encode && refs.system_encode(refs.context,movie,flags,out,error);
}
static bool global_system_decode(void *context,const qa_q3_cinematic_source *source,
    qa_bytes bytes,uint32_t flags,qa_q3_system_movie *out,qa_error *error)
{
    global_movie_scope *scope=context; frontend_remote_q3_module_topology role;
    movie_row *owner=global_source_row(scope,source,NULL,error);
    if(owner && owner->kind==MOVIE_UNIFIED) {
        frontend_unified_q3_runtime_factory *factory=NULL;
        global_system_scope system={scope,source}; qa_q3_movie_checkpoint_refs refs=system_movie_refs(&system);
        return global_unified_role(scope,owner,source,0,NULL,&factory,error) &&
            frontend_unified_q3_runtime_factory_system_restore(factory,&refs,bytes,flags,out,error);
    }
    qa_q3_movie_checkpoint_refs refs;
    return global_role(scope,source,&role,error) && frontend_q3_module_cinematic_refs(scope->q3,&role,&refs,error) &&
        refs.system_decode && refs.system_decode(refs.context,bytes,flags,out,error);
}
static void global_system_discard(void *context,const qa_q3_cinematic_source *source,qa_q3_system_movie *movie)
{ (void)context; (void)source; frontend_system_cinematic_discard(movie); }
static qa_q3_cinematic_handles_refs global_refs(global_movie_scope *scope)
{
    return (qa_q3_cinematic_handles_refs){.context=scope,.source_encode=global_source_encode,
        .source_decode=global_source_decode,.source_clock_read=global_source_clock,.asset_encode=global_asset_encode,.asset_decode=global_asset_decode,
        .audio_bus_decode=global_bus_decode,.system_encode=global_system_encode,
        .system_decode=global_system_decode,.system_discard=global_system_discard,
        .movies={.playback={scope,global_target_encode,global_target_decode},
            .publication={scope,global_image_encode,global_image_decode}}};
}
static bool role_prefix(qa_source_save_io *io,qa_frontend *f,movie_row *rows,size_t count)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t roles=reading?0:frontend_cinematic_roles_count(f);
    if(!qa_source_save_count(io,&roles,16) ||
        (reading && frontend_cinematic_roles_count(f)>roles)) return false;
    for(size_t i=0;i<roles;++i) {
        uint64_t parent_key=0; uint32_t seat=0;
        if(!reading) {
            frontend_cinematic_role_view role;
            if(!frontend_cinematic_roles_read(f,i,&role,io->error) || !role.parent || !role.cinematics ||
                !qa_q3_cinematic_source_retained(role.cinematics)) return false;
            seat=role.seat;
            for(size_t j=0;j<count;++j) {
                const frontend_material_movie_source *source=&rows[j].source;
                if(source->frontend==role.source.frontend && source->files==role.source.files &&
                    source->images==role.source.images && source->materials==role.source.materials &&
                    source->media==role.source.media && source->context==role.source.context &&
                    source->current==role.source.current) {parent_key=j+1;break;}
            }
            if(!parent_key) return frontend_fail(io->error,QA_ERROR_FORMAT,"Detached cinematic role has no actual movie inventory parent");
        }
        if(!qa_source_save_u64(io,&parent_key) || !parent_key || parent_key>count ||
            !qa_source_save_u32(io,&seat) || seat>=f->options.seats) return false;
        if(reading && !frontend_cinematic_roles_restore_add(f,&rows[parent_key-1].source,seat,i,io->error)) return false;
    }
    return !reading || frontend_cinematic_roles_count(f)==roles;
}
static bool fields(qa_source_save_io *io,qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,const qa_audio_checkpoint_refs *audio,frontend_q3_inventory *q3,movie_row *rows,size_t count,
    uint64_t *pool_image,qa_bytes *pool_state)
{
    uint8_t magic[4]={'Q','F','V','M'}; uint32_t version=8; size_t saved=count;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!reading && f->source_cinematics) {
        qa_q3_cinematic_handles_options options;
        if(!frontend_source_cinematics_read(f,&options,io->error) ||
            !frontend_scene_image_encode(space,qa_scene_source_q3_scratch(options.images,0),pool_image,io->error) || !*pool_image) return false;
    }
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFVM",4) ||
        !qa_source_save_u32(io,&version) || version!=8 || !qa_source_save_u64(io,pool_image) ||
        !qa_source_save_count(io,&saved,count) || saved!=count || !role_prefix(io,f,rows,count)) return false;
    for(size_t i=0;i<count;++i) {
        movie_row *row=rows+i; uint32_t kind=row->kind; uint64_t ordinal=row->ordinal;
        uint64_t view=qa_application_content_view_id(qa_application_content_graph_read(f->application),row->source.files),saved_view=view;
        movie_scope scope={.graph=qa_application_content_graph_read(f->application),.space=space,.frames=frames,
            .row=row,.frontend=f,.audio=audio};
        if(!view || !qa_source_save_u32(io,&kind) || kind!=(uint32_t)row->kind ||
            !qa_source_save_u64(io,&ordinal) || ordinal!=row->ordinal ||
            !qa_source_save_u64(io,&saved_view) || saved_view!=view || !resource_fields(io,&scope)) return false;
        qa_buffer cache={0},state={0}; bool okay=true;
        qa_media_library_checkpoint_refs library={&scope,resource_encode,resource_decode,image_encode,image_decode};
        frontend_material_movies_refs refs=movie_refs(&scope);
        if(!reading) {
            okay=frontend_material_movies_library_owner(row->source.materials,&row->owner,io->error) &&
                (!private_cache(row->kind) ||
                    qa_media_library_checkpoint(row->source.media,&library,&cache,io->error)) &&
                frontend_material_movies_checkpoint(row->owner,&refs,&state,io->error);
            row->cache=(qa_bytes){cache.data,cache.size}; row->state=(qa_bytes){state.data,state.size};
        }
        okay=okay && blob(io,&row->cache) &&
            (private_cache(row->kind)==(row->cache.size!=0)) &&
            blob(io,&row->state) && row->state.size;
        qa_buffer_free(&cache); qa_buffer_free(&state);
        if(!okay) return false;
    }
    qa_buffer state={0}; bool okay=true;
    if(!reading && *pool_image) {
        global_movie_scope scope={f,space,audio,qa_application_content_graph_read(f->application),rows,count,q3};
        qa_q3_cinematic_handles_refs refs=global_refs(&scope);
        okay=qa_q3_cinematic_handles_checkpoint(f->source_cinematics,&refs,&state,io->error);
        *pool_state=(qa_bytes){state.data,state.size};
    }
    okay=okay && blob(io,pool_state) && ((*pool_image!=0)==(pool_state->size!=0));
    qa_buffer_free(&state); return okay;
}
bool frontend_material_movie_inventory_checkpoint(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,const qa_audio_checkpoint_refs *audio,frontend_q3_inventory *q3,qa_buffer *out,qa_error *error)
{
    if(!f || !f->capture || !space || !frames || !out || out->data || out->size) return false;
    movie_row *rows=NULL; size_t count=0; uint64_t pool_image=0; qa_bytes pool_state={0}; qa_source_save_io io={0};
    bool okay=collect(f,&rows,&count,error) && qa_source_save_writer(&io,NULL,error) &&
        fields(&io,f,space,frames,audio,q3,rows,count,&pool_image,&pool_state) && roster_matches(f,rows,count,error) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); rows_free(rows,count); return okay;
}
static bool restore(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,const qa_audio_checkpoint_refs *audio,frontend_q3_inventory *q3,qa_bytes bytes,bool unified_prefix,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || !space || !frames) return false;
    movie_row *rows=NULL; size_t count=0; uint64_t pool_image=0; qa_bytes pool_state={0}; qa_source_save_io io={0};
    bool okay=collect(f,&rows,&count,error) && qa_source_save_reader(&io,NULL,bytes,error) &&
        fields(&io,f,space,frames,audio,q3,rows,count,&pool_image,&pool_state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay && pool_image) {
        const qa_scene_image *scratch=NULL;
        okay=frontend_scene_image_decode(space,pool_image,&scratch,error) && scratch;
        qa_scene_resources *images=okay?qa_scene_image_resource_owner(scratch):NULL;
        okay=okay && images && qa_scene_source_q3_scratch(images,0)==scratch &&
            frontend_source_cinematics_ensure(f,images,error);
        qa_q3_cinematic_handles_options options;
        okay=okay && frontend_source_cinematics_read(f,&options,error) && options.images==images;
    } else if(okay && f->source_cinematics) okay=false;
    for(size_t i=0;okay && i<count;++i) {
        movie_row *row=rows+i; movie_scope scope={.graph=qa_application_content_graph_read(f->application),.space=space,.frames=frames,
            .row=row,.frontend=f,.audio=audio};
        qa_media_library_checkpoint_refs library={&scope,resource_encode,resource_decode,image_encode,image_decode};
        frontend_material_movies_refs refs=movie_refs(&scope);
        if(unified_prefix && row->kind!=MOVIE_UNIFIED) continue;
        if(!unified_prefix && row->kind==MOVIE_UNIFIED) {
            qa_buffer cache={0},state={0};
            okay=frontend_material_movies_library_owner(row->source.materials,&row->owner,error) &&
                qa_media_library_checkpoint(row->source.media,&library,&cache,error) &&
                frontend_material_movies_checkpoint(row->owner,&refs,&state,error) &&
                cache.size==row->cache.size && (!cache.size || !memcmp(cache.data,row->cache.data,cache.size)) &&
                state.size==row->state.size && (!state.size || !memcmp(state.data,row->state.data,state.size));
            qa_buffer_free(&cache); qa_buffer_free(&state); continue;
        }
        if(private_cache(row->kind))
            okay=!qa_media_library_record_count(row->source.media) &&
            qa_media_library_restore(row->source.media,row->cache,&library,error);
        if(okay) okay=restore_owner(f,row,&refs,error) &&
            frontend_material_movies_library_owner(row->source.materials,&row->owner,error);
    }
    for(size_t i=0;okay && i<frontend_cinematic_roles_count(f);++i) {
        frontend_cinematic_role_view role;
        okay=frontend_cinematic_roles_read(f,i,&role,error);
        for(size_t j=0;okay && !role.cinematics && j<count;++j) {
            const frontend_material_movie_source *source=&rows[j].source;
            if(source->frontend!=role.source.frontend || source->files!=role.source.files ||
                source->images!=role.source.images || source->materials!=role.source.materials ||
                source->media!=role.source.media || source->context!=role.source.context ||
                source->current!=role.source.current) continue;
            if(rows[j].owner) okay=frontend_cinematic_roles_restore_bind(f,i,rows[j].owner,error);
            break;
        }
        if(okay && !unified_prefix) okay=frontend_cinematic_roles_read(f,i,&role,error) && role.cinematics;
    }
    if(okay && !unified_prefix && pool_image) {
        global_movie_scope scope={f,space,audio,qa_application_content_graph_read(f->application),rows,count,q3};
        for(size_t i=0;okay && i<count;++i) {
            frontend_remote_q3_modules *modules=global_modules(&scope,rows+i,error);
            if(modules) okay=frontend_remote_q3_modules_cinematics_bind(modules,error);
        }
        if(!okay) { rows_free(rows,count); return false; }
        qa_q3_cinematic_handles_refs refs=global_refs(&scope);
        okay=qa_q3_cinematic_handles_restore(f->source_cinematics,&refs,(double)f->wall_time_ns/1000000.0,pool_state,error);
    }
    for(size_t i=0;okay && !unified_prefix && i<frontend_cinematic_roles_count(f);++i) {
        frontend_cinematic_role_view role;
        okay=frontend_cinematic_roles_read(f,i,&role,error) && role.cinematics &&
            qa_q3_cinematic_source_retained(role.cinematics);
        if(!okay && (!error || error->code==QA_OK))
            frontend_fail(error,QA_ERROR_FORMAT,"Saved detached cinematic role has no actual global pool custody");
    }
    for(size_t i=0;okay && i<count;++i)
        if(!unified_prefix || rows[i].kind==MOVIE_UNIFIED)
            okay=frontend_material_movies_publish_ready(rows[i].owner,error);
    if(okay && !unified_prefix) okay=roster_matches(f,rows,count,error);
    if(okay) for(size_t i=0;i<count;++i)
        if(!unified_prefix || rows[i].kind==MOVIE_UNIFIED) frontend_material_movies_publish(rows[i].owner);
    rows_free(rows,count); return okay;
}
bool frontend_material_movie_inventory_restore(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,const qa_audio_checkpoint_refs *audio,frontend_q3_inventory *q3,qa_bytes bytes,qa_error *error)
{ return restore(f,space,frames,audio,q3,bytes,false,error); }
bool frontend_material_movie_inventory_restore_unified(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,const qa_audio_checkpoint_refs *audio,frontend_q3_inventory *q3,qa_bytes bytes,qa_error *error)
{ return restore(f,space,frames,audio,q3,bytes,true,error); }
