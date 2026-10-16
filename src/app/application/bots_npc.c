#include "bots_npc_private.h"
#include "control_frame.h"
#include "guest_native_q2_private.h"
#include "qa/game_q1_bots.h"
#include "qa/binary.h"
#include "qa/persistence_navigation.h"
#include "qa/tools.h"
#include "qa/native_host_q2_wire.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct npc_prediction {
    application_bots_npc *owner;
    qa_actor_id actor;
    qa_body_state body;
    qa_physics_properties properties;
    qa_vec3 *points;
    size_t count,capacity,slot;
    bool initialized;
} npc_prediction;

typedef struct npc_advisory_query {
    qa_arena storage;
    qa_nav_workspace *workspace;
    qa_nav_route route;
} npc_advisory_query;

static bool native_source(const application_provider *source)
{
    const struct application_native_q2 *engine=source && source->kind==APPLICATION_PROVIDER_NATIVE?
        source->state.native.q2_engine:NULL;
    return engine && engine->provider==source && engine->profile==QA_NATIVE_Q2_GAME_API2023;
}

typedef struct npc_native_rows {
    qa_native_host *host;
    qa_native_instance *instance;
    uint32_t cursor,end;
    bool importing;
} npc_native_rows;

static bool native_rows_begin(application_provider *source,npc_native_rows *rows,qa_error *error)
{
    *rows=(npc_native_rows){.host=source->state.native.host,
        .importing=source->state.native.q2_engine->calls!=0};
    rows->instance=qa_native_host_instance(rows->host);
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(rows->instance,&table,error)) return false;
    rows->end=rows->importing?table.capacity:table.count;
    return true;
}

static bool native_rows_next(npc_native_rows *rows,qa_native_host_q2_entity *out,
    bool *found,qa_error *error)
{
    *found=false;
    while(rows->cursor<rows->end) {
        uint32_t slot=rows->cursor++;
        qa_native_slot_binding binding;
        if(!qa_native_slot(rows->instance,slot,&binding,error)) return false;
        if(binding.kind==QA_NATIVE_SLOT_FREE) continue;
        if(!(rows->importing?qa_native_host_q2_wire_entity_import(rows->host,slot,out,error):
            qa_native_host_q2_bot_entity(rows->host,slot,out,error))) return false;
        if(out->in_use) {*found=true;return true;}
    }
    return true;
}

bool application_bots_npc_native_hazard(application_provider *source,qa_bounds bounds,
    bool *hazardous,qa_error *error)
{
    *hazardous=false;
    if(!native_source(source)) return true;
    const struct application_native_q2 *engine=source->state.native.q2_engine;
    if(!source->state.native.host || !engine->prepared || engine->shutting_down ||
        source->close_pending || (!(source->constructed && source->attached) && !engine->calls)) return true;
    npc_native_rows rows;
    if(!native_rows_begin(source,&rows,error)) return false;
    for(;;) {
        qa_native_host_q2_entity entity;bool found;
        if(!native_rows_next(&rows,&entity,&found,error)) return false;
        if(!found) return true;
        if(!entity.bot.registered || !(entity.bot.flags&(UINT64_C(1)<<27))) continue;
        if(entity.state.renderfx&128) {
            if(entity.server_flags&1) continue;
            qa_vec3 start=qa_v3(entity.state.origin[0],entity.state.origin[1],entity.state.origin[2]);
            qa_vec3 end=qa_v3(entity.state.old_origin[0],entity.state.old_origin[1],entity.state.old_origin[2]);
            *hazardous=qa_bounds_intersects_segment(bounds,start,end);
        } else if(entity.solid==1) *hazardous=qa_bounds_overlap(bounds,entity.absolute_bounds);
        if(*hazardous) return true;
    }
}

bool application_npc_current(const application_bots_npc *owner,qa_error *error)
{
    application_provider *source=owner?owner->source:NULL;
    qa_application *app=source?source->application:NULL;
    bool game=source && ((source->kind==APPLICATION_PROVIDER_Q1 && source->state.q1) ||
        (source->kind==APPLICATION_PROVIDER_QC && source->product &&
         source->product->family==QA_GAME_Q1 && source->state.qc.game));
    bool entered=native_source(source) &&
        ((source->constructed && source->attached) || source->state.native.q2_engine->calls) &&
        source->state.native.q2_engine->prepared && !source->state.native.q2_engine->shutting_down &&
        source->state.native.q2_engine->world==owner->world;
    return (((game && source->constructed && source->attached) || entered) && !source->close_pending &&
        app->world==owner->world && app->physics==owner->physics &&
        app->map_resource==owner->map_resource) ||
        application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation lost its actual source world");
}
bool application_npc_physics_read(application_provider *source,qa_actor_id actor,
    qa_physics_properties *properties,bool *found,qa_error *error)
{
    *found=false;
    if(!source) return application_fail(error,QA_ERROR_ARGUMENT,"Monster physics lost its source owner");
    if(source->kind==APPLICATION_PROVIDER_Q1) {
        *found=qa_q1_game_physics_read(source->state.q1,actor,properties);return true;
    }
    if(source->kind!=APPLICATION_PROVIDER_QC ||
       !qa_qc_game_read_physics(source->state.qc.game,actor,properties,error)) return false;
    *found=true;return true;
}
static bool actual(application_bots_npc *owner,qa_actor_id actor,qa_body_state *body,
    qa_physics_properties *properties, bool *found,qa_error *error)
{
    *found=false;
    if(!application_npc_current(owner,error)) return false;
    if(!qa_actors_get(qa_session_actors(owner->source->application->session),actor)) return true;
    bool physical;
    if(!application_npc_physics_read(owner->source,actor,properties,&physical,error)) return false;
    if(!physical) return true;
    if(!(properties->flags&QA_PHYSICS_MONSTER) || qa_ruleset_read(properties->rules)->family!=QA_GAME_Q1) return true;
    if(!qa_world_body_read(owner->world,actor,body,error)) return false;
    *found=true;return true;
}
static bool prediction_prepare(application_bots_npc *owner, size_t points, qa_error *error)
{
    if(points<=owner->prediction_points) return true;
    if(points>(SIZE_MAX-sizeof(npc_prediction))/sizeof(qa_vec3))
        return application_fail(error,QA_ERROR_MEMORY,"Monster trajectory capacity overflow");
    qa_arena storage;qa_pool predictions={0};
    qa_arena_init_pool(&storage,qa_unified_frame_lease_arena(owner->storage)->pages);
    size_t capacity=qa_actors_capacity(qa_session_actors(owner->source->application->session));
    if(!qa_pool_reserve_pages(&predictions,&storage,capacity,
            sizeof(npc_prediction)+points*sizeof(qa_vec3),_Alignof(npc_prediction),error)) {
        qa_arena_destroy(&storage);return false;
    }
    qa_arena_destroy(&owner->prediction_storage);
    owner->prediction_storage=storage;owner->predictions=predictions;
    owner->prediction_points=points;return true;
}
static bool prediction_extent(application_bots_npc *owner,qa_vec3 minimum,qa_vec3 maximum,
    qa_error *error)
{
    double x=(double)maximum.x-minimum.x,y=(double)maximum.y-minimum.y,z=(double)maximum.z-minimum.z;
    double points=ceil(sqrt(x*x+y*y+z*z)/8)+4;
    if(!isfinite(points) || points>=(double)SIZE_MAX)
        return application_fail(error,QA_ERROR_MEMORY,"Monster trajectory capacity overflow");
    return prediction_prepare(owner,(size_t)points,error);
}
static bool point(npc_prediction *p,qa_vec3 origin,qa_error *error)
{
    if(p->count==p->capacity)
        return application_fail(error,QA_ERROR_MEMORY,"Monster trajectory exceeds admitted map extent");
    p->points[p->count++]=origin;return true;
}
static bool prediction_begin(void *opaque,qa_actor_id actor,void **lease,qa_error *error)
{
    application_bots_npc *owner=opaque;
    if(!application_npc_current(owner,error)) return false;
    size_t slot;
    npc_prediction *p=qa_pool_take(&owner->predictions,&slot);
    if(!p) return application_fail(error,QA_ERROR_MEMORY,"Monster prediction lease storage exhausted");
    *p=(npc_prediction){.owner=owner,.actor=actor,.slot=slot,
        .points=(qa_vec3 *)(p+1),.capacity=owner->prediction_points};
    *lease=p;return true;
}
static void prediction_end(void *opaque,void *lease)
{
    application_bots_npc *owner=opaque;npc_prediction *p=lease;
    if(p) qa_pool_release(&owner->predictions,p->slot);
}
static bool prediction_admit(void *opaque,void *lease,qa_vec3 from,qa_vec3 to,
    qa_nav_travel mode,const qa_vec3 **points,size_t *count,float *seconds,
    bool *admitted,qa_error *error)
{
    npc_prediction *p=lease;application_bots_npc *owner=opaque;
    *points=NULL;*count=0;*seconds=0;*admitted=false;
    if(!p || p->owner!=owner || !application_npc_current(owner,error)) return false;
    if(mode!=QA_NAV_WALK && mode!=QA_NAV_DROP && mode!=QA_NAV_SWIM) return true;
    qa_body_state body;qa_physics_properties properties;bool found;
    if(!actual(owner,p->actor,&body,&properties,&found,error)) return false;
    if(!found) return true;
    if(!p->initialized) {
        p->body=body;p->body.origin=from;p->properties=properties;p->initialized=true;
    }
    if(qa_vec_length(qa_vec_sub(p->body.origin,from))>2) return true;
    p->count=0;
    if(!point(p,p->body.origin,error)) return false;
    double length=(double)qa_vec_length(qa_vec_sub(to,from));
    double steps=ceil(length/8)+2;
    if(!isfinite(steps) || steps>=(double)SIZE_MAX)
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster traversal exceeds its finite step domain");
    for(size_t i=0;i<(size_t)steps;++i) {
        qa_vec3 offset=qa_vec_sub(to,p->body.origin);
        float horizontal=hypotf(offset.x,offset.y);
        if(horizontal<=1) break;
        bool moved;
        if(!qa_physics_monster_walk_detached(owner->physics,p->actor,&p->body,&p->properties,
                atan2f(offset.y,offset.x)*(180.0f/3.14159265358979323846f),fminf(8,horizontal),
                &p->body,&p->properties,&moved,error)) return false;
        if(!moved) return true;
        if(!point(p,p->body.origin,error)) return false;
    }
    if(qa_vec_length(qa_vec_sub(p->body.origin,to))>2) return true;
    *points=p->points;*count=p->count;*seconds=(float)(length/100);*admitted=true;
    return application_npc_current(owner,error);
}
static uint64_t revision(void *opaque)
{
    application_bots_npc *owner=opaque;
    return qa_session_elapsed(owner->source->application->session)/UINT64_C(1000000);
}
static bool hazard(void *opaque,qa_bounds bounds,bool *hazardous,qa_error *error)
{
    application_bots_npc *owner=opaque;
    return application_navigation_hazard(owner->source->application,owner->world,bounds,hazardous,error);
}
qa_navigation_services application_npc_services(application_bots_npc *owner)
{
    return (qa_navigation_services){.context=owner,.world=owner->world,.revision=revision,
        .hazard=hazard,.traversal_begin=prediction_begin,.traversal_admit=prediction_admit,
        .traversal_end=prediction_end};
}
void application_npc_actor_free(npc_actor *actor)
{
    if(!actor) return;
    qa_nav_route_free(&actor->route);qa_nav_workspace_destroy(actor->workspace);
    qa_navigation_destroy(actor->navigation);
    qa_arena storage=actor->storage;qa_arena_destroy(&storage);
}
void application_npc_owner_free(application_bots_npc *owner)
{
    if(!owner) return;
    while(owner->actors) {
        npc_actor *actor=owner->actors;owner->actors=actor->next;application_npc_actor_free(actor);
    }
    while(owner->graphs) {
        npc_graph *graph=owner->graphs;owner->graphs=graph->next;
        application_npc_graph_free(graph);
    }
    for(size_t i=0;i<owner->advisory_queries.capacity;++i) {
        npc_advisory_query *query=qa_pool_at(&owner->advisory_queries,i);
        qa_nav_route_free(&query->route);qa_nav_workspace_destroy(query->workspace);
        qa_arena_destroy(&query->storage);
    }
    qa_navigation_destroy(owner->advisory_navigation);
    application_npc_graph_free(owner->advisory_graph);
    qa_arena_destroy(&owner->advisory_storage);
    qa_actor_index_destroy(&owner->actor_routes);
    qa_arena_destroy(&owner->prediction_storage);
    qa_vfs_destroy(owner->files);qa_resource_release(owner->map_resource);
    qa_unified_frame_lease *storage=owner->storage;qa_unified_frame_lease_release(storage);
}
void application_bots_npc_destroy(application_provider *source)
{
    if(!source) return;
    application_bots_npc *owner=source->bots_npc;source->bots_npc=NULL;
    application_npc_owner_free(owner);
}
bool application_bots_npc_idle(const application_provider *source)
{
    return !source || !source->bots_npc ||
        (!source->bots_npc->busy && !source->bots_npc->advisory_depth);
}
bool application_bots_npc_content_visit(const application_provider *source,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    const application_bots_npc *owner=source?source->bots_npc:NULL;
    if(!owner) return true;
    if(!visitor || !visitor->view || !application_bots_npc_idle(source) ||
       !application_npc_current(owner,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation content is borrowed or stale");
    if(owner->files && !visitor->view(visitor->context,owner->files,error)) return false;
    for(npc_graph *graph=owner->advisory_graph?owner->advisory_graph:owner->graphs;graph;
        graph=graph==owner->advisory_graph?owner->graphs:graph->next) {
        if(!graph->asset) continue;
        qa_resource_pool *pool=qa_vfs_resources(owner->files);
        if(!pool || qa_resource_pool_find(pool,qa_resource_id(graph->asset))!=graph->asset ||
           graph->acquisition.resource_id!=qa_resource_id(graph->asset) ||
           !qa_vfs_acquisition_retained(owner->files,&graph->acquisition,error))
            return application_fail(error,QA_ERROR_FORMAT,"Monster navigation asset lost its actual opening");
    }
    return true;
}
bool application_bots_npc_horde(void *opaque)
{
    application_provider *source=opaque;
    qa_application *app=source?source->application:NULL;
    for(size_t i=0;app && app->modes && i<app->mode_count;++i) {
        qa_mode_view mode;qa_actor_id manager;
        if(!qa_modes_read(app->modes,app->mode_ids[i],&mode,NULL) ||
           !mode.rules.enabled || mode.rules.source!=QA_MODE_Q1_HORDE ||
           !qa_modes_horde_manager_actor(app->modes,app->mode_ids[i],&manager,NULL)) continue;
        const qa_actor_record *actor=qa_actors_get(qa_session_actors(app->session),manager);
        if(actor && actor->owner==source->owner) return true;
    }
    return false;
}
bool application_npc_owner_create(application_provider *source,bool prepared,application_bots_npc **out,qa_error *error)
{
    qa_application *app=source?source->application:NULL;
    if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_QC &&
           !native_source(source)) ||
       !app->map_resource || !out)
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation needs its actual source map");
    qa_unified_frame_lease *storage=application_control_storage_acquire(app,error);
    if(!storage) return false;
    application_bots_npc *owner=qa_unified_frame_lease_alloc(storage,1,sizeof(*owner),_Alignof(application_bots_npc),error);
    if(!owner) {qa_unified_frame_lease_release(storage);return false;}
    owner->storage=storage;
    owner->source=source;owner->world=app->world;owner->physics=app->physics;
    owner->map_resource=app->map_resource;qa_resource_retain(owner->map_resource);
    qa_q1_options options={0};double time;
    bool okay=qa_actor_index_create(&owner->actor_routes,qa_session_actors(app->session),qa_unified_frame_lease_arena(storage),error) &&
        application_npc_current(owner,error);
    if(okay && source->kind==APPLICATION_PROVIDER_Q1)
        okay=prepared?qa_q1_source_respawn_options_prepared(source->state.q1,&options,error):
            qa_q1_source_respawn_options_read(source->state.q1,&options,&time,error);
    else if(okay) {
        qa_ruleset_id kind=source->launch->selection.clock.kind;
        if(!native_source(source) && kind!=QA_RULESET_NETQUAKE && kind!=QA_RULESET_QUAKEWORLD)
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Original monster source has no Q1 movement clock");
        options.rules=kind;
    }
    if(okay) okay=qa_bsp_open(qa_resource_bytes(owner->map_resource),&owner->geometry,error);
    if(okay && !native_source(source)) {
        qa_bsp_model model;
        okay=qa_bsp_read_model(&owner->geometry,0,&model,error) &&
            prediction_extent(owner,model.bounds.min,model.bounds.max,error);
    }
    if(okay) {
        owner->movement=qa_movement_profile_default(options.rules);
        owner->map=(qa_nav_map){.name=app->current_map,.format=owner->geometry.format};
    }
    if(!okay) {application_npc_owner_free(owner);return false;}
    *out=owner;return true;
}
static bool graph_resource(application_bots_npc *owner,npc_graph *graph,qa_error *error)
{
    qa_launch_resource_origin origin;
    if(!qa_application_map_origin_read(owner->source->application,&origin) ||
       !origin.catalog || !origin.content || !origin.acquisition ||
       origin.acquisition->resource_id!=qa_resource_id(owner->map_resource))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation lost its actual geometry acquisition");
    if(!owner->files && !qa_catalog_open(origin.catalog,origin.product,&owner->files,error)) return false;
    const char *path=origin.acquisition->path;
    if(!path) return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation lacks its actual map path");
    size_t stem=strlen(path);
    if(stem>=5 && (path[0]=='m'||path[0]=='M') && (path[1]=='a'||path[1]=='A') &&
       (path[2]=='p'||path[2]=='P') && (path[3]=='s'||path[3]=='S') && path[4]=='/') {
        path+=5;stem-=5;
    }
    if(stem>=4 && path[stem-4]=='.' && (path[stem-3]=='b'||path[stem-3]=='B') &&
       (path[stem-2]=='s'||path[stem-2]=='S') && (path[stem-1]=='p'||path[stem-1]=='P')) stem-=4;
    if(!stem || stem>SIZE_MAX-22) return application_fail(error,QA_ERROR_MEMORY,"Monster navigation path overflow");
    for(size_t start=0;start<=stem;) {
        size_t end=start;
        while(end<stem && path[end]!='/') ++end;
        size_t length=end-start;
        if(!length || (length==1 && path[start]=='.') ||
           (length==2 && path[start]=='.' && path[start+1]=='.'))
            return application_fail(error,QA_ERROR_ARGUMENT,"Invalid navigation map resource path");
        if(end==stem) break;
        start=end+1;
    }
    qa_arena storage;qa_arena_init_pool(&storage,qa_unified_frame_lease_arena(owner->storage)->pages);
    char *name=qa_arena_alloc(&storage,stem+22,_Alignof(char),error);
    if(!name) {qa_arena_destroy(&storage);return false;}
    bool okay=true;
    for(size_t i=0;i<2;++i) {
        bool aas=owner->geometry.family==QA_BSP_Q3?i==0:i==1;
        const char *prefix=aas?"maps/":"bots/navigation/";
        size_t prefix_size=strlen(prefix);
        memcpy(name,prefix,prefix_size);memcpy(name+prefix_size,path,stem);
        strcpy(name+prefix_size+stem,aas?".aas":".nav");
        qa_resource *resource=NULL;qa_error local={0};qa_vfs_acquisition acquisition={0};
        if(!qa_vfs_acquire_receipt(owner->files,name,&resource,&acquisition,&local)) {
            if(local.code==QA_ERROR_NOT_FOUND) continue;
            if(error) *error=local;
            okay=false;break;
        }
        if(!aas) {
            qa_product_id product;qa_mount_id physical;
            if(!qa_catalog_product_mount_origin(origin.catalog,origin.product,owner->files,acquisition.mount,&product,&physical)) {
                qa_resource_release(resource);qa_vfs_acquisition_dispose(&acquisition);
                okay=application_fail(error,QA_ERROR_ARGUMENT,"Monster NAV lost its genuine mount content");
                break;
            }
            if(product!=origin.product) {qa_resource_release(resource);qa_vfs_acquisition_dispose(&acquisition);continue;}
        }
        graph->asset=resource;graph->acquisition=acquisition;break;
    }
    qa_arena_destroy(&storage);return okay;
}
bool application_npc_graph_rebuild(application_bots_npc *owner,npc_graph *graph,qa_error *error)
{
    qa_nav_profile profile={.movement=owner->movement,.shape={QA_SHAPE_BOX,graph->bounds},
        .policy={.behavior = &qa_trace_behaviors[QA_RULESET_NETQUAKE],.q1_hull=-1},.maximum_step=18,.minimum_floor_normal=.7f,
        .maximum_drop=18,.monster=true,.capabilities=QA_NAV_CAPABILITY(QA_NAV_WALK)|
            QA_NAV_CAPABILITY(QA_NAV_DROP)|QA_NAV_CAPABILITY(QA_NAV_SWIM)};
    qa_navigation_services services=application_npc_services(owner);
    services.topology_geometry_only=true;
    if(graph->asset) {
        uint32_t word=qa_collision_map_checksum(owner->source->application->geometry);int32_t checksum;
        memcpy(&checksum,&word,sizeof(checksum));
        qa_nav_asset *asset=NULL;
        bool okay=qa_nav_asset_read(qa_resource_bytes(graph->asset),&checksum,&asset,error);
        if(okay) okay=qa_nav_graph_from_asset(&owner->map,asset,&profile,&services,&graph->storage,&graph->graph,error);
        qa_nav_asset_release(asset);
        if(!okay) return false;
    } else {
        qa_nav_construction construction={.geometry=&owner->geometry,.map=owner->map,.profile=profile};
        if(!qa_nav_graph_construct(&construction,&services,&graph->storage,&graph->graph,error)) return false;
    }
    const qa_nav_graph_view *view=qa_nav_graph_read(graph->graph);
    qa_bsp_model model;
    if(!qa_bsp_read_model(&owner->geometry,0,&model,error)) return false;
    qa_bounds bounds={model.bounds.min,model.bounds.max};
    for(size_t i=0;i<view->node_count;++i) {
        qa_vec3 origin=view->nodes[i].origin;
        bounds=qa_bounds_union(bounds,(qa_bounds){origin,origin});
    }
    for(size_t i=0;i<view->edge_count;++i) {
        qa_vec3 start=view->edges[i].start,end=view->edges[i].end;
        bounds=qa_bounds_union(bounds,(qa_bounds){start,start});
        bounds=qa_bounds_union(bounds,(qa_bounds){end,end});
    }
    return prediction_extent(owner,bounds.mins,bounds.maxs,error);

}
bool application_npc_graph_create(application_bots_npc *owner,npc_graph **out,qa_error *error)
{
    qa_arena storage;qa_arena_init_pool(&storage,qa_unified_frame_lease_arena(owner->storage)->pages);
    npc_graph *graph=qa_arena_alloc(&storage,sizeof(*graph),_Alignof(npc_graph),error);
    if(!graph) {qa_arena_destroy(&storage);return false;}
    *graph=(npc_graph){.storage=storage};*out=graph;return true;
}
void application_npc_graph_free(npc_graph *graph)
{
    if(!graph) return;
    qa_nav_graph_release(graph->graph);qa_resource_release(graph->asset);
    qa_vfs_acquisition_dispose(&graph->acquisition);
    qa_arena storage=graph->storage;qa_arena_destroy(&storage);
}
static bool graph_for(application_bots_npc *owner,qa_bounds bounds,uint32_t flags,npc_graph **out,qa_error *error)
{
    flags&=QA_PHYSICS_FLYING|QA_PHYSICS_SWIMMING;
    for(npc_graph *graph=owner->graphs;graph;graph=graph->next)
        if(graph->flags==flags && !memcmp(&graph->bounds,&bounds,sizeof(bounds))) {*out=graph;return true;}
    npc_graph *graph=NULL;
    if(!application_npc_graph_create(owner,&graph,error)) return false;
    graph->bounds=bounds;graph->flags=flags;
    bool okay=graph_resource(owner,graph,error) && application_npc_graph_rebuild(owner,graph,error);
    if(!okay) {application_npc_graph_free(graph);return false;}
    graph->next=owner->graphs;owner->graphs=graph;
    *out=graph;return true;
}

static bool advisory_entity(void *opaque,qa_actor_id traveller,const qa_nav_binding *binding,qa_nav_entity_state *out,
    bool *found,qa_error *error)
{
    (void)traveller;
    application_bots_npc *owner=opaque;
    *found=false;
    bool raw_model=false;
    if(binding->raw_count==1) {
        const qa_nav_graph_view *graph=qa_nav_graph_read(owner->advisory_graph->graph);
        const qa_nav_source_view *asset=qa_nav_asset_kex(graph->asset);
        raw_model=asset && asset->kind==QA_NAV_NAV3 && asset->version>=2;
    }
    npc_native_rows rows;
    if(!native_rows_begin(owner->source,&rows,error)) return false;
    for(;;) {
        qa_native_host_q2_entity entity;bool present;
        if(!native_rows_next(&rows,&entity,&present,error)) return false;
        if(!present) return true;
        if(!entity.bot.classname) continue;
        bool matches;
        if(raw_model)
            matches=entity.state.modelindex==(uint32_t)binding->raw[0];
        else if(binding->has_model) {
            qa_actor_collision collision;
            matches=qa_world_get_collision(owner->world,entity.binding.actor,&collision,NULL) &&
                collision.inline_model && collision.model==(uint32_t)binding->model;
        } else matches=qa_bounds_overlap(binding->bounds,entity.absolute_bounds);
        if(!matches) continue;
        *out=(qa_nav_entity_state){.actor=entity.binding.actor,
            .enabled=entity.linked && entity.solid==3,
            .locked=(entity.bot.flags&(UINT64_C(1)<<24))!=0,
            .bounds=entity.absolute_bounds,.velocity=entity.bot.velocity,.kind=QA_NAV_ENTITY_GENERIC};
        *found=true;return true;
    }
}

static bool advisory_prepare(application_bots_npc *owner,qa_error *error)
{
    if(owner->advisory_loaded) return true;
    npc_graph *graph=NULL;
    if(!application_npc_graph_create(owner,&graph,error)) return false;
    bool okay=graph_resource(owner,graph,error);
    qa_nav_asset *asset=NULL;
    if(okay && graph->asset) {
        uint32_t word=qa_collision_map_checksum(owner->source->application->geometry);int32_t checksum;
        memcpy(&checksum,&word,sizeof(checksum));
        okay=qa_nav_asset_read(qa_resource_bytes(graph->asset),&checksum,&asset,error);
        qa_nav_profile profile={.movement=qa_movement_profile_default(QA_RULESET_Q2_RERELEASE),
            .shape={.kind=QA_SHAPE_POINT},.policy={.behavior=&qa_trace_behaviors[QA_RULESET_Q2_RERELEASE],
                .q1_hull=-1,.contents_mask=qa_collision_contents_mask(0x30003,QA_GAME_Q2)},
            .minimum_floor_normal=.7f,.capabilities=(UINT32_C(1)<<QA_NAV_TRAVEL_COUNT)-1};
        qa_navigation_services services={.context=owner,.world=owner->world,.revision=revision,
            .hazard=hazard,.entity=advisory_entity,.topology_geometry_only=true};
        if(okay) okay=qa_nav_graph_from_asset(&owner->map,asset,&profile,&services,
            &graph->storage,&graph->graph,error);
        services.topology_geometry_only=false;
        if(okay) okay=qa_navigation_create(graph->graph,&services,&graph->storage,
            &owner->advisory_navigation,error);
    }
    qa_nav_asset_release(asset);
    if(!okay) {application_npc_graph_free(graph);return false;}
    owner->advisory_graph=graph;owner->advisory_loaded=true;
    return true;
}

typedef struct npc_path_filter {
    const qa_native_host_q2_path_request *request;
    const qa_nav_graph_view *graph;
    const qa_navigation *navigation;
} npc_path_filter;

static bool advisory_node(void *opaque,const qa_nav_node *node)
{
    const qa_native_host_q2_path_request *request=((npc_path_filter *)opaque)->request;
    if(node->source.kind!=QA_NAV_ORIGIN_NAV3) return true;
    if(request->ignore_node_flags) return !(node->flags&1024);
    if(node->flags&(1|2|8|256|512)) return false;
    uint32_t medium=request->flags&3;
    return !(medium==2 && (node->flags&16)) && !(medium==1 && !(node->flags&16)) &&
        !(!(request->flags&32) && (node->flags&4));
}

static bool advisory_edge(void *opaque,const qa_nav_edge *edge)
{
    npc_path_filter *filter=opaque;
    const qa_native_host_q2_path_request *request=filter->request;
    if(request->ignore_node_flags) return true;
    if(request->flags==1 && edge->mode!=QA_NAV_WALK && edge->mode!=QA_NAV_SWIM) return false;
    switch(edge->mode) {
    case QA_NAV_WALK: case QA_NAV_SWIM: return !edge->has_entity;
    case QA_NAV_DROP: {
        const qa_nav_node *from=qa_navigation_node(filter->navigation,edge->from);
        const qa_nav_node *to=qa_navigation_node(filter->navigation,edge->to);
        return (request->flags&4) && (request->drop_height<=0 ||
            from->origin.z-to->origin.z<=request->drop_height);
    }
    case QA_NAV_JUMP: {
        bool barrier=edge->source_travel_type==5;
        const qa_nav_node *from=qa_navigation_node(filter->navigation,edge->from);
        const qa_nav_node *to=qa_navigation_node(filter->navigation,edge->to);
        return (request->flags&(barrier?16u:8u)) && (!barrier || request->jump_height<=0 ||
            to->origin.z-from->origin.z<=request->jump_height);
    }
    case QA_NAV_MOVER: return (request->flags&32)!=0;
    default: return false;
    }
}

static float advisory_cost(void *opaque,const qa_nav_node *from,const qa_nav_edge *edge,
    const qa_nav_node *to)
{
    const qa_nav_source_view *source=qa_nav_asset_kex(((npc_path_filter *)opaque)->graph->asset);
    return edge->mode==QA_NAV_TELEPORT?1:
        qa_vec_length(qa_vec_sub(from->origin,to->origin))*(source?source->heuristic:1);
}

static qa_trace_policy advisory_policy(void)
{
    return (qa_trace_policy){.behavior=&qa_trace_behaviors[QA_RULESET_Q2_RERELEASE],
        .q1_hull=-1,.contents_mask=qa_collision_contents_mask(0x30003,QA_GAME_Q2)};
}

static bool advisory_nearest(application_bots_npc *owner,npc_path_filter *filter,qa_vec3 point,
    const qa_nav_node **out,qa_error *error)
{
    const qa_native_host_q2_path_request *request=filter->request;
    float best=request->radius>0?request->radius:512;
    float lower=point.z-(request->min_height>0?request->min_height:64);
    float upper=point.z+(request->max_height>0?request->max_height:64);
    qa_nav_route_query eligibility={.context=filter,.node_filter=advisory_node,.advisory=true};
    *out=NULL;
    for(size_t i=0;i<filter->graph->node_count;++i) {
        const qa_nav_node *node=filter->graph->nodes+i;
        if(node->origin.z<lower || node->origin.z>upper) continue;
        float distance=hypotf(node->origin.x-point.x,node->origin.y-point.y);
        if(distance>best) continue;
        bool allowed;
        if(!qa_navigation_node_allowed(owner->advisory_navigation,(qa_actor_id){0},node->id,
            &eligibility,&allowed,error)) return false;
        if(!allowed) continue;
        qa_trace_query query={.start=point,.end=qa_vec_add(node->origin,qa_v3(0,0,32)),
            .shape={.kind=QA_SHAPE_POINT},.policy=advisory_policy()};
        qa_trace_result trace;
        if(!qa_world_trace(owner->world,&query,&trace,error)) return false;
        if(trace.fraction<1 || trace.start_solid || trace.all_solid) continue;
        *out=node;best=distance;
    }
    return true;
}

static bool advisory_scope(application_bots_npc *owner,npc_advisory_query **out,size_t *slot,
    qa_error *error)
{
    qa_pool *pool=&owner->advisory_queries;
    if(!pool->stride) {
        qa_arena_init_pool(&owner->advisory_storage,qa_unified_frame_lease_arena(owner->storage)->pages);
        if(!qa_pool_prepare(pool,&owner->advisory_storage,
            qa_actors_capacity(qa_session_actors(owner->source->application->session)),
            sizeof(npc_advisory_query),_Alignof(npc_advisory_query),error)) return false;
        for(size_t i=0;i<pool->capacity;++i) *(npc_advisory_query *)qa_pool_at(pool,i)=(npc_advisory_query){0};
    }
    if(pool->head==SIZE_MAX) {
        size_t prior=pool->capacity;
        if(!qa_pool_extend(pool,&owner->advisory_storage,prior?prior:1,error)) return false;
        for(size_t i=prior;i<pool->capacity;++i) *(npc_advisory_query *)qa_pool_at(pool,i)=(npc_advisory_query){0};
    }
    npc_advisory_query *query=qa_pool_take(pool,slot);
    if(!query) return application_fail(error,QA_ERROR_MEMORY,"Retaining advisory query scope");
    if(!query->storage.pages)
        qa_arena_init_pool(&query->storage,owner->advisory_storage.pages);
    if((!query->workspace && !qa_nav_workspace_create(&query->storage,&query->workspace,error)) ||
        !qa_nav_workspace_prepare(query->workspace,qa_nav_workspace_limits_read(
            qa_navigation_graph(owner->advisory_navigation)),error)) {
        qa_pool_release(pool,*slot);return false;
    }
    ++owner->advisory_depth;*out=query;return true;
}

typedef struct npc_path_debug {
    qa_arena shapes;
    qa_arena *storage;
    qa_debug_line *lines;
    size_t count,capacity;
} npc_path_debug;

static bool advisory_debug_shape(npc_path_debug *debug,const qa_debug_shape *shape,
    qa_vec4 color,qa_error *error)
{
    const qa_debug_line *lines;size_t count;
    if(!qa_debug_shape_lines(shape,color,false,&debug->shapes,&lines,&count,error)) return false;
    if(count>SIZE_MAX-debug->count || debug->count+count>SIZE_MAX/sizeof(*lines))
        return application_fail(error,QA_ERROR_MEMORY,"Advisory debug geometry size overflow");
    size_t needed=debug->count+count;
    if(needed>debug->capacity) {
        size_t capacity=debug->capacity?debug->capacity:count;
        while(capacity<needed)
            capacity=capacity>SIZE_MAX/2?needed:capacity*2;
        if(capacity>SIZE_MAX/sizeof(*lines)) capacity=needed;
        qa_debug_line *grown=qa_arena_grow(debug->storage,debug->lines,
            debug->count*sizeof(*lines),capacity*sizeof(*lines),_Alignof(qa_debug_line),error);
        if(!grown) return false;
        debug->lines=grown;debug->capacity=capacity;
    }
    if(count) memcpy(debug->lines+debug->count,lines,count*sizeof(*lines));
    debug->count=needed;qa_arena_reset(&debug->shapes);return true;
}

static bool advisory_debug(application_provider *source,const qa_native_host_q2_path_request *request,
    const qa_nav_route *route,const qa_navigation *navigation,size_t begin,
    const qa_native_host_q2_path_info *info,qa_error *error)
{
    if(request->draw_time<=0) return true;
    qa_application *app=source->application;qa_clock_state clock;
    if(!qa_session_clock(app->session,source->owner,&clock)) return true;
    qa_unified_frame_lease *storage=application_control_storage_acquire(app,error);
    if(!storage) return false;
    npc_path_debug debug={.storage=qa_unified_frame_lease_arena(storage)};
    qa_arena_init_pool(&debug.shapes,debug.storage->pages);
    qa_vec4 red={1,0,0,64.0f/255},yellow={1,1,0,64.0f/255};
    qa_debug_shape sphere={.kind=QA_DEBUG_SPHERE,
        .data.round={.origin=request->start,.radius=8}};
    bool okay=advisory_debug_shape(&debug,&sphere,red,error);
    sphere.data.round.origin=request->goal;
    if(okay) okay=advisory_debug_shape(&debug,&sphere,red,error);
    qa_debug_shape arrow={.kind=QA_DEBUG_ARROW,
        .data.arrow={.start=request->start,.size=8,.cap_color=yellow}};
    for(size_t i=begin;okay && route && i<route->node_count;++i) {
        arrow.data.arrow.end=qa_navigation_node(navigation,route->nodes[i])->origin;
        okay=advisory_debug_shape(&debug,&arrow,yellow,error);
        arrow.data.arrow.start=arrow.data.arrow.end;
    }
    arrow.data.arrow.end=request->goal;
    if(okay) okay=advisory_debug_shape(&debug,&arrow,yellow,error);
    sphere.data.round.origin=info->first;sphere.data.round.radius=16;
    if(okay) okay=advisory_debug_shape(&debug,&sphere,red,error);
    arrow.data.arrow.start=info->first;arrow.data.arrow.end=info->second;
    arrow.data.arrow.size=16;arrow.data.arrow.cap_color=red;
    if(okay) okay=advisory_debug_shape(&debug,&arrow,red,error);
    long double nanoseconds=(long double)request->draw_time*1000000000.0L;
    uint64_t lifetime=nanoseconds>=UINT64_MAX?UINT64_MAX:(uint64_t)nanoseconds;
    if(okay) okay=qa_application_debug_emit(app,&(qa_application_debug_event){
        .provider=source->owner,.time_ns=clock.frame.time_ns,
        .lines=debug.lines,.count=debug.count,.lifetime_ns=lifetime},error);
    qa_arena_destroy(&debug.shapes);qa_unified_frame_lease_release(storage);return okay;
}

static bool advisory_result(const qa_native_host_q2_path_request *request,
    const qa_nav_route *route,const qa_navigation *navigation,qa_native_host_q2_path_info *info,
    size_t *first_point,qa_error *error)
{
    const qa_nav_edge *edge=route->edge_count?qa_navigation_edge(navigation,route->edges[0]):NULL;
    const qa_nav_node *first=qa_navigation_node(navigation,route->nodes[0]);
    size_t begin=0;
    if(!request->ignore_node_flags && edge && (edge->mode==QA_NAV_WALK || edge->mode==QA_NAV_CROUCH)) {
        qa_vec3 offset=qa_vec_sub(request->start,first->origin);
        float length=hypotf(offset.x,offset.y);
        const qa_nav_node *next=qa_navigation_node(navigation,route->nodes[1]);
        qa_vec3 direction=qa_vec_sub(next->origin,first->origin);
        if((length<=first->radius && fabsf(offset.z)<=64) ||
            (length>0 && offset.x*direction.x+offset.y*direction.y>first->radius*length)) begin=1;
    }
    first=qa_navigation_node(navigation,route->nodes[begin]);
    const qa_nav_node *last=qa_navigation_node(navigation,route->nodes[route->node_count-1]);
    float distance=0;qa_vec3 at=request->start;
    for(size_t i=begin;i<route->node_count;++i) {
        qa_vec3 point=qa_navigation_node(navigation,route->nodes[i])->origin;
        distance+=qa_vec_length(qa_vec_sub(at,point));at=point;
    }
    distance+=qa_vec_length(qa_vec_sub(at,request->goal));
    info->distance_squared=distance*distance;
    bool add_start=qa_vec_length(qa_vec_sub(request->start,first->origin))>64;
    bool add_goal=qa_vec_length(qa_vec_sub(request->goal,last->origin))>64;
    size_t count=route->node_count-begin+add_start+add_goal;
    if(count>INT32_MAX) return application_fail(error,QA_ERROR_FORMAT,"Native path count exceeds its I32 result");
    info->point_count=(int32_t)count;
    if(request->point_capacity>0) {
        size_t written=0;
        if(add_start && !request->point(request->point_context,request->start,error)) return false;
        written+=add_start;
        for(size_t i=begin;i<route->node_count && written<(uint64_t)request->point_capacity;++i,++written)
            if(!request->point(request->point_context,qa_navigation_node(navigation,route->nodes[i])->origin,error)) return false;
        if(add_goal && written<(uint64_t)request->point_capacity &&
            !request->point(request->point_context,request->goal,error)) return false;
    }
    info->return_code=request->ignore_node_flags?3:edge && edge->has_hint?2:4;
    if(!request->ignore_node_flags) {
        info->first=edge && edge->has_hint?edge->hint.start:first->origin;
        info->second=edge && edge->has_hint?edge->hint.end:begin+1<route->node_count?
            qa_navigation_node(navigation,route->nodes[begin+1])->origin:request->goal;
    }
    info->link_type=!edge?0:edge->mode==QA_NAV_DROP?1:edge->mode==QA_NAV_JUMP?
        (edge->source_travel_type==5?3:2):edge->mode==QA_NAV_MOVER?4:0;
    *first_point=begin;
    return true;
}

bool application_bots_npc_path(void *opaque,const qa_native_host_q2_path_request *request,
    qa_native_host_q2_path_info *info,qa_error *error)
{
    application_provider *source=opaque;
    *info=(qa_native_host_q2_path_info){.return_code=8};
    if(!source->application->map_resource || !source->application->world) goto done;
    if(source->bots_npc && source->bots_npc->map_resource!=source->application->map_resource) {
        if(!application_bots_npc_idle(source))
            return application_fail(error,QA_ERROR_ARGUMENT,"Advisory map changed inside a query");
        application_bots_npc_destroy(source);
    }
    if(!source->bots_npc && !application_npc_owner_create(source,false,&source->bots_npc,error)) return false;
    application_bots_npc *owner=source->bots_npc;
    if(!application_npc_current(owner,error) || !advisory_prepare(owner,error)) return false;
    const qa_nav_graph_view *graph=qa_navigation_graph(owner->advisory_navigation);
    if(!graph || !graph->node_count) goto done;
    if(!(request->flags&3)) {info->return_code=12;goto done;}
    npc_path_filter filter={.request=request,.graph=graph,.navigation=owner->advisory_navigation};
    const qa_nav_node *start,*goal;
    if(!advisory_nearest(owner,&filter,request->start,&start,error)) return false;
    if(!start) {info->return_code=9;goto done;}
    if(!advisory_nearest(owner,&filter,request->goal,&goal,error)) return false;
    if(!goal) {info->return_code=10;goto done;}
    if(start==goal || qa_vec_length(qa_vec_sub(request->start,goal->origin))<=request->move_distance) {
        info->return_code=0;goto done;
    }
    if(!request->ignore_node_flags) {
        const qa_vec3 points[]={request->start,request->goal};
        for(size_t i=0;i<2;++i) {
            qa_point_contents contents;
            qa_point_query query={.point=points[i],.policy=advisory_policy()};
            if(!qa_world_point_contents(owner->world,&query,&contents,error)) return false;
            if(qa_collision_bits_overlap(contents.merged,qa_collision_contents_mask(3,QA_GAME_Q2))) {
                info->return_code=(int32_t)(6+i);goto done;
            }
        }
    }
    npc_advisory_query *query=NULL;size_t slot=SIZE_MAX;
    if(!advisory_scope(owner,&query,&slot,error)) return false;
    qa_nav_route_query path={.start=request->start,.goal=request->goal,
        .start_node=start->id,.goal_node=goal->id,.context=&filter,.edge_filter=advisory_edge,
        .node_filter=advisory_node,.edge_cost=advisory_cost,.advisory=true};
    bool okay=qa_navigation_route(owner->advisory_navigation,query->workspace,&path,&query->route,error);
    info->return_code=11;size_t first_point=0;
    if(okay && query->route.found)
        okay=advisory_result(request,&query->route,owner->advisory_navigation,info,&first_point,error);
    if(okay) okay=advisory_debug(source,request,query->route.found?&query->route:NULL,
        owner->advisory_navigation,first_point,info,error);
    qa_nav_route_reset(&query->route);--owner->advisory_depth;
    qa_pool_release(&owner->advisory_queries,slot);
    return okay;
done:
    return advisory_debug(source,request,NULL,NULL,0,info,error);
}
bool application_npc_actor_create(application_bots_npc *owner,qa_actor_id id,npc_graph *graph,
    npc_actor **out,qa_error *error)
{
    qa_arena storage;qa_arena_init_pool(&storage,qa_unified_frame_lease_arena(owner->storage)->pages);
    npc_actor *actor=qa_arena_alloc(&storage,sizeof(*actor),_Alignof(npc_actor),error);
    if(!actor) {qa_arena_destroy(&storage);return false;}
    *actor=(npc_actor){.storage=storage,.actor=id,.graph=graph};
    qa_navigation_services services=application_npc_services(owner);
    bool okay=qa_navigation_create(graph->graph,&services,&actor->storage,&actor->navigation,error) &&
        qa_nav_workspace_create(&actor->storage,&actor->workspace,error) &&
        qa_nav_workspace_prepare(actor->workspace,
            qa_nav_workspace_limits_read(qa_nav_graph_read(graph->graph)),error);
    if(!okay) {application_npc_actor_free(actor);return false;}
    *out=actor;return true;
}
static void drop_path(npc_actor *actor)
{
    qa_nav_route_free(&actor->route);actor->has_path=false;actor->cursor=0;
}
void application_bots_npc_released(void *opaque,qa_actor_id id)
{
    application_provider *source=opaque;
    application_bots_npc *owner=source?source->bots_npc:NULL;
    if(!owner) return;
    npc_actor *actor=application_npc_actor_find(owner,id);
    if(!actor) return;
    qa_actor_index_remove(&owner->actor_routes,id);
    if(owner->busy) {actor->retired=true;return;}
    application_npc_actor_unlink(owner,actor);application_npc_actor_free(actor);
}
static bool walking(void *context,const qa_nav_edge *edge)
{
    (void)context;return edge->mode==QA_NAV_WALK || edge->mode==QA_NAV_DROP || edge->mode==QA_NAV_SWIM;
}
static bool route_copy(const qa_nav_route *source,qa_arena *storage,qa_nav_route *out,qa_error *error)
{
    qa_nav_route result={.travel_seconds=source->travel_seconds,.generation=source->generation,
        .found=source->found};
    qa_arena_init_pool(&result.storage,storage->pages);
    if(!qa_nav_route_reserve(&result,source->node_count,source->edge_count,source->point_count,error)) {
        qa_nav_route_free(&result);return false;
    }
    if(source->node_count) memcpy(result.nodes,source->nodes,source->node_count*sizeof(*result.nodes));
    if(source->edge_count) memcpy(result.edges,source->edges,source->edge_count*sizeof(*result.edges));
    if(source->point_count) memcpy(result.points,source->points,source->point_count*sizeof(*result.points));
    result.node_count=source->node_count;result.edge_count=source->edge_count;
    result.point_count=source->point_count;
    result.graph=source->graph;qa_nav_graph_retain(result.graph);*out=result;return true;
}
bool application_bots_npc_clone(void *opaque,qa_actor_id from,qa_actor_id to,qa_error *error)
{
    application_provider *source=opaque;
    application_bots_npc *owner=source?source->bots_npc:NULL;
    if(!owner) return true;
    npc_actor *original=application_npc_actor_find(owner,from);
    if(!original || !original->has_path || original->retired) return true;
    if(qa_actor_id_equal(from,to) || !application_npc_current(owner,error)) return false;
    qa_body_state body;qa_physics_properties properties;bool found;
    if(!actual(owner,to,&body,&properties,&found,error)) return false;
    if(!found) return application_fail(error,QA_ERROR_NOT_FOUND,"Cloned monster route lacks its actual target body");
    if(application_npc_actor_find(owner,to))
        return application_fail(error,QA_ERROR_ARGUMENT,"Cloned monster route target already has a continuation");
    npc_actor *copy=NULL;qa_nav_checkpoint continuation={0};
    bool okay=application_npc_actor_create(owner,to,original->graph,&copy,error) &&
        route_copy(&original->route,&copy->storage,&copy->route,error) &&
        qa_navigation_capture(original->navigation,&continuation,error) &&
        qa_navigation_restore(copy->navigation,&continuation,error);
    qa_nav_checkpoint_free(&continuation);
    if(!okay) {application_npc_actor_free(copy);return false;}
    copy->goal=original->goal;copy->cursor=original->cursor;copy->has_path=true;
    application_npc_actor_link(owner,copy);return true;
}
static bool walk(application_bots_npc *owner,qa_actor_id id,qa_vec3 goal,float distance,
    qa_q1_path_result *result,qa_error *error)
{
    qa_body_state body;qa_physics_properties properties;bool found;
    if(!actual(owner,id,&body,&properties,&found,error)) return false;
    if(!found) {application_bots_npc_released(owner->source,id);return true;}
    npc_graph *graph;
    if(!graph_for(owner,body.bounds,properties.flags,&graph,error)) return false;
    npc_actor *actor=application_npc_actor_find(owner,id);
    if(!actor) {
        if(!application_npc_actor_create(owner,id,graph,&actor,error)) return false;
        application_npc_actor_link(owner,actor);
    } else if(actor->graph!=graph) {
        qa_navigation_services services=application_npc_services(owner);qa_navigation *next=NULL;
        if(!qa_navigation_create(graph->graph,&services,&actor->storage,&next,error)) return false;
        if(!qa_nav_workspace_prepare(actor->workspace,
                qa_nav_workspace_limits_read(qa_nav_graph_read(graph->graph)),error)) {
            qa_navigation_destroy(next);return false;
        }
        qa_navigation_destroy(actor->navigation);actor->navigation=next;actor->graph=graph;
    }
    if(qa_vec_length(qa_vec_sub(goal,body.origin))<=1) {
        drop_path(actor);*result=QA_Q1_PATH_REACHED_GOAL;return true;
    }
    bool okay=true,valid=false;
    if(actor->has_path) okay=qa_navigation_route_valid(actor->navigation,id,&actor->route,&valid,error);
    if(okay && (!actor->has_path || qa_vec_length(qa_vec_sub(goal,actor->goal))>1 || !valid)) {
        qa_nav_route route={0};qa_nav_route_query query={.actor=id,.start=body.origin,.goal=goal,
            .start_node=QA_NAV_NO_INDEX,.goal_node=QA_NAV_NO_INDEX,.edge_filter=walking};
        okay=qa_navigation_route(actor->navigation,actor->workspace,&query,&route,error);
        if(okay) {
            drop_path(actor);
            if(route.found) {actor->route=route;route=(qa_nav_route){0};actor->goal=goal;actor->has_path=true;}
        }
        qa_nav_route_free(&route);
    }
    if(okay && actor->has_path) {
        okay=actual(owner,id,&body,&properties,&found,error);
        if(okay && !found) drop_path(actor);
    }
    if(okay && actor->has_path) {
        while(actor->cursor<actor->route.point_count &&
            qa_vec_length(qa_vec_sub(actor->route.points[actor->cursor],body.origin))<=1) ++actor->cursor;
        if(actor->cursor==actor->route.point_count) {drop_path(actor);*result=QA_Q1_PATH_REACHED_END;}
        else {
            qa_vec3 offset=qa_vec_sub(actor->route.points[actor->cursor],body.origin);
            float step=fminf(fabsf(distance),hypotf(offset.x,offset.y));bool moved=false;
            if(step!=0) okay=qa_physics_walk_move(owner->physics,id,
                atan2f(offset.y,offset.x)*(180.0f/3.14159265358979323846f),copysignf(step,distance),
                0,true,true,&moved,error);
            if(okay && !moved) {drop_path(actor);*result=QA_Q1_PATH_BLOCKED;}
            else if(okay) *result=QA_Q1_PATH_IN_PROGRESS;
        }
    }
    return okay;
}
bool application_bots_npc_walk(void *opaque,qa_actor_id id,qa_vec3 goal,float distance,
    qa_q1_path_result *result,qa_error *error)
{
    application_provider *source=opaque;
    if(!source || !result || !qa_vec_finite(goal) || !isfinite(distance))
        return application_fail(error,QA_ERROR_ARGUMENT,"Invalid actual monster path request");
    *result=QA_Q1_PATH_ERROR;
    if(!application_bots_npc_idle(source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation reentered its route workspace");
    if(source->bots_npc && source->bots_npc->map_resource!=source->application->map_resource)
        application_bots_npc_destroy(source);
    if(!source->bots_npc && !application_npc_owner_create(source,false,&source->bots_npc,error)) return false;
    application_bots_npc *owner=source->bots_npc;
    qa_q1_game_operation operation={0};
    if(source->kind==APPLICATION_PROVIDER_Q1 &&
       !qa_q1_game_operation_begin(source->state.q1,&operation,error)) return false;
    owner->busy=true;
    bool okay=walk(owner,id,goal,distance,result,error);
    owner->busy=false;
    npc_actor *actor=owner->actors;
    while(actor) {
        npc_actor *next=actor->next;
        if(actor->retired || !qa_actors_get(qa_session_actors(source->application->session),actor->actor)) {
            application_npc_actor_unlink(owner,actor);application_npc_actor_free(actor);
        }
        actor=next;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
