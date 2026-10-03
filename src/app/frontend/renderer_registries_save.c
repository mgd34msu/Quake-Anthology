#include "renderer_registries_save.h"
#include "renderer_registries.h"
#include "material_inventory.h"
#include "remote_q3_graph.h"
#include "qa/persistence_content.h"
#include "qa/scene_world_save.h"
#include "save_private.h"

typedef struct registry_map {
    uint64_t world,pool,resource,alias;
    qa_bytes portals;
} registry_map;
typedef struct registry_prefix {
    uint64_t alias,provider,services,map;
    uint64_t *providers;
    size_t provider_count,map_count;
    registry_map *maps;
} registry_prefix;
static bool span(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if(io->offset>io->input.size || size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool row_fields(qa_source_save_io *io,registry_prefix *row)
{
    if(!qa_source_save_u64(io,&row->alias)) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!row->alias && (!qa_source_save_u64(io,&row->provider) || !row->provider ||
        !qa_source_save_u64(io,&row->services) || !qa_source_save_u64(io,&row->map) ||
        !qa_source_save_count(io,&row->provider_count,SIZE_MAX/sizeof(*row->providers)))) return false;
    if(row->alias && !qa_source_save_u64(io,&row->map)) return false;
    if(reading && row->provider_count) {
        if(io->offset>io->input.size || row->provider_count>(io->input.size-io->offset)/8) return false;
        row->providers=calloc(row->provider_count,sizeof(*row->providers));
        if(!row->providers) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining registry provider prefix");
    }
    for(size_t i=0;i<row->provider_count;++i)
        if(!qa_source_save_u64(io,row->providers+i) || !row->providers[i]) return false;
    if(!qa_source_save_count(io,&row->map_count,SIZE_MAX/sizeof(*row->maps)) || row->map>row->map_count) return false;
    if(reading && row->map_count) {
        if(io->offset>io->input.size || row->map_count>(io->input.size-io->offset)/8) return false;
        row->maps=calloc(row->map_count,sizeof(*row->maps));
        if(!row->maps) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining registry map prefix");
    }
    for(size_t i=0;i<row->map_count;++i) {
        registry_map *map=row->maps+i;
        if(!qa_source_save_u64(io,&map->alias)) return false;
        if(!qa_source_save_u64(io,&map->world) || !map->world || !qa_source_save_u64(io,&map->pool) || !map->pool ||
            !qa_source_save_u64(io,&map->resource) || !map->resource) return false;
        if(!map->alias && (!span(io,&map->portals) || !map->portals.size)) return false;
    }
    return true;
}
static void dispose(registry_prefix *rows,size_t count,bool writing)
{
    for(size_t i=0;rows && i<count;++i) {
        if(writing) for(size_t j=0;j<rows[i].map_count;++j) free((void *)rows[i].maps[j].portals.data);
        free(rows[i].maps); free(rows[i].providers);
    }
    free(rows);
}
static bool previous_geometry(qa_frontend *f,size_t row,size_t index,const qa_collision_geometry *geometry,
    uint64_t *key,qa_error *error)
{
    for(size_t i=0;i<=row;++i) {
        qa_q3_presentation_assets *assets=NULL;
        if(!frontend_renderer_registries_at(f,i,&assets,error)) return false;
        size_t count=i==row?index:qa_q3_assets_map_count(assets);
        for(size_t n=0;n<count;++n) {
            qa_q3_asset_map_custody map;
            if(!qa_q3_assets_map_at(assets,n,&map)) return false;
            if(map.geometry==geometry) {
                if(i>=UINT32_MAX || n>=UINT32_MAX) return false;
                *key=((uint64_t)(i+1)<<32)|(uint64_t)(n+1); return true;
            }
        }
    }
    *key=0; return true;
}
bool frontend_renderer_registries_checkpoint(qa_frontend *f,frontend_q3_inventory *q3,
    frontend_world_inventory *roots,qa_buffer *out,qa_error *error)
{
    if(!f || !f->capture || !q3 || !roots || !out || out->data || out->size) return false;
    size_t count=frontend_renderer_registries_count(f);
    if(count>UINT32_MAX || count>SIZE_MAX/sizeof(registry_prefix)) return false;
    registry_prefix *rows=count?calloc(count,sizeof(*rows)):NULL;
    if(count && !rows) return frontend_fail(error,QA_ERROR_MEMORY,"Capturing retained registry prefixes");
    bool okay=true; qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    for(size_t i=0;okay && i<count;++i) {
        registry_prefix *row=rows+i; qa_q3_presentation_assets *assets=NULL;
        bool alias=false; qa_q3_presentation_asset_options services;
        qa_scene_world *world=NULL; qa_collision_geometry *geometry=NULL;
        okay=frontend_renderer_registries_at(f,i,&assets,error) &&
            frontend_q3_registry_alias(q3,assets,&row->alias,&alias,error);
        if(!okay) continue;
        okay=qa_q3_assets_services(assets,&services,&world,&geometry,error);
        if(okay && !alias) okay=
            frontend_material_provider_encode(f,&services.provider,&row->provider,error) &&
            frontend_q3_registry_services_key(q3,&services,&row->services,error);
        row->provider_count=alias?0:qa_q3_assets_provider_count(assets); row->map_count=qa_q3_assets_map_count(assets);
        if(row->provider_count>SIZE_MAX/sizeof(*row->providers) || row->map_count>SIZE_MAX/sizeof(*row->maps)) okay=false;
        if(okay && row->provider_count) { row->providers=calloc(row->provider_count,sizeof(*row->providers)); okay=row->providers!=NULL; }
        if(okay && row->map_count) { row->maps=calloc(row->map_count,sizeof(*row->maps)); okay=row->maps!=NULL; }
        for(size_t j=0;okay && j<row->provider_count;++j) {
            qa_q3_presentation_provider provider;
            okay=qa_q3_assets_provider_at(assets,j,&provider) && frontend_material_provider_encode(f,&provider,row->providers+j,error);
        }
        for(size_t j=0;okay && j<row->map_count;++j) {
            registry_map *saved=row->maps+j; qa_q3_asset_map_custody map;
            okay=qa_q3_assets_map_at(assets,j,&map) && map.world && map.geometry && map.resource &&
                previous_geometry(f,i,j,map.geometry,&saved->alias,error) &&
                frontend_world_encode(roots,map.world,&saved->world,error) &&
                qa_application_content_resource_id(graph,map.resource,&saved->pool,&saved->resource);
            if(okay && world==map.world && geometry==map.geometry) row->map=j+1;
            if(!okay || saved->alias) continue;
            qa_buffer portals={0};
            okay=qa_collision_geometry_family(map.geometry)==QA_COLLISION_Q3 &&
                frontend_remote_q3_graph_geometry_checkpoint(map.geometry,&portals,error);
            saved->portals=(qa_bytes){portals.data,portals.size};
        }
        if(okay && ((world==NULL)!=(geometry==NULL) || (world && !row->map))) okay=false;
    }
    qa_source_save_io io={0}; uint8_t magic[4]={'Q','F','R','G'}; okay=okay && qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_count(&io,&count,SIZE_MAX);
    for(size_t i=0;okay && i<count;++i) okay=row_fields(&io,rows+i);
    okay=okay && qa_source_save_finish(&io,out); qa_source_save_dispose(&io); dispose(rows,count,true);
    if(!okay && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Retained registry prefix lost its actual provider or Q3 map custody");
    return okay;
}
static bool existing_geometry(frontend_q3_inventory *base,const registry_prefix *rows,size_t count,
    uint64_t key,const qa_resource *resource,qa_collision_geometry **out,qa_error *error)
{
    *out=NULL;
    for(size_t i=0;i<count;++i) {
        const registry_prefix *row=rows+i;
        if(!row->alias || !row->map) continue;
        const registry_map *map=row->maps+row->map-1;
        uint64_t first=map->alias?map->alias:((uint64_t)(i+1)<<32)|row->map;
        if(first!=key) continue;
        qa_q3_presentation_assets *assets=NULL; qa_q3_presentation_asset_options options;
        qa_scene_world *world=NULL; qa_collision_geometry *geometry=NULL;
        if(!frontend_q3_assets_decode(base,row->alias,&assets,error) || !assets ||
            !qa_q3_assets_services(assets,&options,&world,&geometry,error)) {
            if(!error || error->code==QA_OK)
                frontend_fail(error,QA_ERROR_FORMAT,"Saved collision alias lacks its actual constructor owner");
            return false;
        }
        if(!world && !geometry) continue;
        if(!world || !geometry || qa_collision_resource(geometry)!=resource || qa_scene_world_source_resource_read(world)!=resource ||
            (*out && *out!=geometry))
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved collision alias changes its actual constructor identity");
        *out=geometry;
    }
    return true;
}
static bool map_resolve(qa_frontend *f,frontend_q3_inventory *base,const registry_prefix *rows,size_t count,
    frontend_world_inventory *roots,const registry_map *saved,
    size_t row,size_t index,const qa_q3_asset_map_custody *current,
    qa_q3_asset_map_custody *out,qa_collision_geometry **created,qa_error *error)
{
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    const qa_resource *resource=qa_application_content_resource(graph,saved->pool,saved->resource);
    if(!resource || !frontend_world_decode(roots,saved->world,&out->world,error) ||
        qa_scene_world_source_resource_read(out->world)!=resource) return false;
    out->resource=resource;
    if(saved->alias) {
        uint64_t owner=saved->alias>>32,ordinal=saved->alias&UINT32_MAX;
        qa_q3_presentation_assets *assets=NULL;
        qa_q3_asset_map_custody prior={0};
        if(!owner || !ordinal || owner-1>row || (owner-1==row && ordinal-1>=index)) return false;
        if(owner-1==row) prior=current[ordinal-1];
        else if(!frontend_renderer_registries_at(f,(size_t)owner-1,&assets,error) || !assets ||
            !qa_q3_assets_map_at(assets,(size_t)ordinal-1,&prior)) return false;
        if(prior.resource!=resource || qa_collision_resource(prior.geometry)!=resource) return false;
        out->geometry=prior.geometry;
        return true;
    }
    qa_bsp_view bsp;
    uint64_t key=((uint64_t)(row+1)<<32)|(uint64_t)(index+1);
    if(!existing_geometry(base,rows,count,key,resource,created,error)) { *created=NULL; return false; }
    qa_collision_geometry *application_geometry=qa_world_geometry(qa_application_world(f->application));
    if(out->world==f->scene_world && application_geometry &&
        qa_collision_resource(application_geometry)==resource &&
        qa_collision_geometry_family(application_geometry)==QA_COLLISION_Q3) {
        if(*created && *created!=application_geometry) {
            *created=NULL;
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved collision alias differs from its actual application map owner");
        }
        *created=application_geometry;
    }
    if(*created) {
        if(!qa_collision_retain(*created,error)) { *created=NULL; return false; }
    } else if(!qa_bsp_open(qa_resource_bytes(resource),&bsp,error) ||
        !qa_bsp_validate(&bsp,error) || !qa_collision_create(&bsp,created,error)) return false;
    out->geometry=*created; out->resource=resource;
    return qa_collision_geometry_family(*created)==QA_COLLISION_Q3 &&
        qa_collision_bind_resource(*created,(qa_resource *)resource,error) &&
        frontend_remote_q3_graph_geometry_restore(*created,saved->portals,error);
}
static bool prefix_qualify(frontend_q3_inventory *base,const registry_prefix *rows,size_t count,qa_error *error)
{
    for(size_t i=0;i<count;++i) {
        const registry_prefix *row=rows+i;
        if(row->alias) {
            qa_q3_presentation_assets *assets=NULL;
            if(!frontend_q3_assets_decode(base,row->alias,&assets,error) || !assets) return false;
        }
        if(!row->alias && (!row->provider_count || row->providers[0]!=row->provider))
            return frontend_fail(error,QA_ERROR_FORMAT,"Registry prefix lost its actual first provider");
        for(size_t j=0;j<row->provider_count;++j)
            for(size_t k=0;k<j;++k) if(row->providers[k]==row->providers[j])
                return frontend_fail(error,QA_ERROR_FORMAT,"Registry prefix repeats a retained provider");
        for(size_t j=0;j<row->map_count;++j) {
            const registry_map *map=row->maps+j;
            if(!map->alias) continue;
            uint64_t owner=map->alias>>32,ordinal=map->alias&UINT32_MAX;
            if(!owner || !ordinal || owner-1>i || (owner-1==i && ordinal-1>=j))
                return frontend_fail(error,QA_ERROR_FORMAT,"Registry collision alias is not a reached prior row");
            const registry_prefix *prior=rows+(size_t)owner-1;
            if(ordinal>prior->map_count || prior->maps[ordinal-1].pool!=map->pool ||
                prior->maps[ordinal-1].resource!=map->resource)
                return frontend_fail(error,QA_ERROR_FORMAT,"Registry collision alias changes its immutable BSP source");
        }
    }
    return true;
}
bool frontend_renderer_registries_prepare_restored(qa_frontend *f,frontend_q3_inventory *base,
    frontend_world_inventory *roots,qa_bytes bytes,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || !base || !roots || f->renderer_registries) return false;
    qa_source_save_io io={0}; uint8_t magic[4]={0}; size_t count=0;
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QFRG",4) && qa_source_save_count(&io,&count,SIZE_MAX/sizeof(registry_prefix)) &&
        count<=UINT32_MAX && io.offset<=io.input.size && count<=(io.input.size-io.offset)/8;
    registry_prefix *rows=okay && count?calloc(count,sizeof(*rows)):NULL;
    okay=okay && (!count || rows);
    for(size_t i=0;okay && i<count;++i) okay=row_fields(&io,rows+i);
    okay=okay && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    okay=okay && prefix_qualify(base,rows,count,error);
    if(okay) okay=frontend_renderer_registries_restore_prefix(f,count,error);
    for(size_t i=0;okay && i<count;++i) {
        registry_prefix *row=rows+i; qa_q3_presentation_assets *assets=NULL;
        if(row->alias) {
            okay=frontend_q3_assets_decode(base,row->alias,&assets,error) && assets && qa_q3_assets_retain(assets,error);
            if(okay && !frontend_renderer_registries_restore_adopt(f,i,&assets,error)) { qa_q3_assets_release(assets); okay=false; }
        } else {
            qa_q3_presentation_asset_options options;
            okay=frontend_q3_registry_services_read(base,row->services,&options,error) &&
                frontend_material_provider_decode(f,row->provider,&options.provider,error) &&
                qa_q3_presentation_assets_create(&options,&assets,error);
            if(!okay) continue;
            if(!frontend_renderer_registries_restore_adopt(f,i,&assets,error)) { qa_q3_assets_release(assets); okay=false; continue; }
        }
        if(!okay) continue;
        okay=frontend_renderer_registries_at(f,i,&assets,error);
        for(size_t j=0;okay && j<row->provider_count;++j) {
            qa_q3_presentation_provider provider;
            okay=frontend_material_provider_decode(f,row->providers[j],&provider,error) &&
                qa_q3_assets_provider_hold(assets,&provider,error);
        }
        qa_q3_asset_map_custody *maps=row->map_count?calloc(row->map_count,sizeof(*maps)):NULL;
        qa_collision_geometry **created=row->map_count?calloc(row->map_count,sizeof(*created)):NULL;
        if(row->map_count && (!maps || !created)) okay=frontend_fail(error,QA_ERROR_MEMORY,"Retaining exact registry map chronology");
        for(size_t j=0;okay && j<row->map_count;++j)
            okay=map_resolve(f,base,rows,count,roots,row->maps+j,i,j,maps,maps+j,created+j,error);
        if(okay && row->map)
            okay=qa_q3_assets_prepare_restored_map(assets,maps[row->map-1].world,maps[row->map-1].geometry,error);
        if(okay) okay=qa_q3_assets_prepare_restored_custody_maps(assets,maps,row->map_count,
            row->map?maps[row->map-1].world:NULL,row->map?maps[row->map-1].geometry:NULL,error);
        for(size_t j=0;created && j<row->map_count;++j) qa_collision_destroy(created[j]);
        free(created); free(maps);
    }
    dispose(rows,count,false);
    if(!okay && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Saved registry prefix lacks genuine nominal allocations or retained map roots");
    return okay;
}
