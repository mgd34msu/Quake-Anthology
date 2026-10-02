#include "material_movie_inventory.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "network_initial_graph.h"
#include "root_resources.h"
#include "renderer_materials.h"
#include "qa/media_library_save.h"
#include "qa/media_resource.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

typedef enum movie_kind { MOVIE_SOURCE,MOVIE_NATIVE,MOVIE_REMOTE,MOVIE_INITIAL,MOVIE_VISUAL,MOVIE_FRONTEND,MOVIE_RENDERER } movie_kind;
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
    case MOVIE_RENDERER: return !ordinal && frontend_renderer_materials_movie_source_read(f,out,error);
    }
    return false;
}
static void rows_free(movie_row *rows,size_t count)
{ for(size_t i=0;rows && i<count;++i) free(rows[i].resources); free(rows); }
static bool collect(qa_frontend *f,movie_row **out,size_t *count,qa_error *error)
{
    frontend_renderer_materials_view retained; bool present=false;
    if(!frontend_renderer_materials_read(f,&retained,&present,error)) return false;
    size_t counts[]={frontend_source_group_count(f),frontend_native_q3_count(f),
        frontend_remote_q3_count(f),frontend_remote_q3_initial_count(f),frontend_visual_owner_count(f),
        f->root_resources?1:0,present && retained.media?1:0};
    size_t total=0;
    for(size_t i=0;i<7;++i) {
        if(counts[i]>SIZE_MAX/sizeof(movie_row)-total) return false;
        total+=counts[i];
    }
    movie_row *rows=total?calloc(total,sizeof(*rows)):NULL;
    if(total && !rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual shader-movie provider rows");
    size_t at=0;
    for(size_t kind=0;kind<7;++kind) for(size_t i=0;i<counts[kind];++i) {
        movie_row *row=rows+at; row->kind=(movie_kind)kind; row->ordinal=i;
        if(!source_read(f,row->kind,i,&row->source,error)) { rows_free(rows,total); return false; }
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
static frontend_material_movies_refs movie_refs(movie_scope *scope)
{
    return (frontend_material_movies_refs){scope,asset_encode,asset_decode,
        {scope,image_encode,image_decode},*scope->frames};
}
static bool resource_fields(qa_source_save_io *io,movie_scope *scope)
{
    movie_row *row=scope->row; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool private_cache=row->kind==MOVIE_VISUAL || row->kind==MOVIE_FRONTEND || row->kind==MOVIE_RENDERER;
    size_t count=reading?0:(private_cache?qa_media_library_record_count(row->source.media):0);
    if(!qa_source_save_count(io,&count,SIZE_MAX/sizeof(*row->resources)) ||
        (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/16)) ||
        (!private_cache && count)) return false;
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
    case MOVIE_RENDERER: return !row->ordinal && frontend_renderer_materials_movies_restore(f,refs,row->state,error);
    }
    return false;
}
static bool fields(qa_source_save_io *io,qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,movie_row *rows,size_t count)
{
    uint8_t magic[4]={'Q','F','V','M'}; uint32_t version=3; size_t saved=count;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFVM",4) ||
        !qa_source_save_u32(io,&version) || version!=3 || !qa_source_save_count(io,&saved,count) || saved!=count) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    for(size_t i=0;i<count;++i) {
        movie_row *row=rows+i; uint32_t kind=row->kind; uint64_t ordinal=row->ordinal;
        uint64_t view=qa_application_content_view_id(qa_application_content_graph_read(f->application),row->source.files),saved_view=view;
        movie_scope scope={qa_application_content_graph_read(f->application),space,frames,row};
        if(!view || !qa_source_save_u32(io,&kind) || kind!=(uint32_t)row->kind ||
            !qa_source_save_u64(io,&ordinal) || ordinal!=row->ordinal ||
            !qa_source_save_u64(io,&saved_view) || saved_view!=view || !resource_fields(io,&scope)) return false;
        qa_buffer cache={0},state={0}; bool okay=true;
        qa_media_library_checkpoint_refs library={&scope,resource_encode,resource_decode,image_encode,image_decode};
        frontend_material_movies_refs refs=movie_refs(&scope);
        if(!reading) {
            okay=frontend_material_movies_library_owner(row->source.materials,&row->owner,io->error) &&
                ((row->kind!=MOVIE_VISUAL && row->kind!=MOVIE_FRONTEND && row->kind!=MOVIE_RENDERER) ||
                    qa_media_library_checkpoint(row->source.media,&library,&cache,io->error)) &&
                frontend_material_movies_checkpoint(row->owner,&refs,&state,io->error);
            row->cache=(qa_bytes){cache.data,cache.size}; row->state=(qa_bytes){state.data,state.size};
        }
        okay=okay && blob(io,&row->cache) &&
            ((row->kind==MOVIE_VISUAL || row->kind==MOVIE_FRONTEND || row->kind==MOVIE_RENDERER)==(row->cache.size!=0)) &&
            blob(io,&row->state) && row->state.size;
        qa_buffer_free(&cache); qa_buffer_free(&state);
        if(!okay) return false;
    }
    return true;
}
bool frontend_material_movie_inventory_checkpoint(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,qa_buffer *out,qa_error *error)
{
    if(!f || !f->capture || !space || !frames || !out || out->data || out->size) return false;
    movie_row *rows=NULL; size_t count=0; qa_source_save_io io={0};
    bool okay=collect(f,&rows,&count,error) && qa_source_save_writer(&io,NULL,error) &&
        fields(&io,f,space,frames,rows,count) && roster_matches(f,rows,count,error) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); rows_free(rows,count); return okay;
}
bool frontend_material_movie_inventory_restore(qa_frontend *f,frontend_scene_namespace *space,
    const qa_scene_frame_checkpoint_refs *frames,qa_bytes bytes,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || !space || !frames) return false;
    movie_row *rows=NULL; size_t count=0; qa_source_save_io io={0};
    bool okay=collect(f,&rows,&count,error) && qa_source_save_reader(&io,NULL,bytes,error) &&
        fields(&io,f,space,frames,rows,count) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for(size_t i=0;okay && i<count;++i) {
        movie_row *row=rows+i; movie_scope scope={qa_application_content_graph_read(f->application),space,frames,row};
        qa_media_library_checkpoint_refs library={&scope,resource_encode,resource_decode,image_encode,image_decode};
        frontend_material_movies_refs refs=movie_refs(&scope);
        if(row->kind==MOVIE_VISUAL || row->kind==MOVIE_FRONTEND || row->kind==MOVIE_RENDERER)
            okay=!qa_media_library_record_count(row->source.media) &&
            qa_media_library_restore(row->source.media,row->cache,&library,error);
        if(okay) okay=restore_owner(f,row,&refs,error) &&
            frontend_material_movies_library_owner(row->source.materials,&row->owner,error) &&
            frontend_material_movies_publish_ready(row->owner,error);
    }
    if(okay) okay=roster_matches(f,rows,count,error);
    if(okay) for(size_t i=0;i<count;++i) frontend_material_movies_publish(rows[i].owner);
    rows_free(rows,count); return okay;
}
