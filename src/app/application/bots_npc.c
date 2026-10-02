#include "bots_npc_private.h"
#include "qa/game_q1_bots.h"
#include "qa/hash.h"
#include "qa/binary.h"
#include "qa/persistence_navigation.h"
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
    size_t count,capacity;
    bool initialized;
} npc_prediction;

bool application_npc_current(const application_bots_npc *owner,qa_error *error)
{
    application_provider *source=owner?owner->source:NULL;
    qa_application *app=source?source->application:NULL;
    bool game=source && ((source->kind==APPLICATION_PROVIDER_Q1 && source->state.q1) ||
        (source->kind==APPLICATION_PROVIDER_QC && source->product &&
         source->product->family==QA_GAME_Q1 && source->state.qc.game));
    return (game && source->constructed &&
        source->attached && !source->close_pending &&
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
    if(!(properties->flags&QA_PHYSICS_MONSTER) || properties->family!=QA_COLLISION_Q1) return true;
    if(!qa_world_body_read(owner->world,actor,body,error)) return false;
    *found=true;return true;
}
static bool point(npc_prediction *p,qa_vec3 origin,qa_error *error)
{
    if(p->count==p->capacity) {
        size_t capacity=p->capacity?p->capacity*2:32;
        if(capacity<p->capacity || capacity>SIZE_MAX/sizeof(*p->points))
            return application_fail(error,QA_ERROR_MEMORY,"Monster trajectory capacity overflow");
        qa_vec3 *points=realloc(p->points,capacity*sizeof(*points));
        if(!points) return application_fail(error,QA_ERROR_MEMORY,"Retaining monster walk trajectory");
        p->points=points;p->capacity=capacity;
    }
    p->points[p->count++]=origin;return true;
}
static bool prediction_begin(void *opaque,qa_actor_id actor,void **lease,qa_error *error)
{
    application_bots_npc *owner=opaque;
    if(!application_npc_current(owner,error)) return false;
    npc_prediction *p=calloc(1,sizeof(*p));
    if(!p) return application_fail(error,QA_ERROR_MEMORY,"Retaining detached monster walk continuation");
    p->owner=owner;p->actor=actor;*lease=p;return true;
}
static void prediction_end(void *opaque,void *lease)
{
    (void)opaque;
    npc_prediction *p=lease;
    if(p) {free(p->points);free(p);}
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
qa_navigation_services application_npc_services(application_bots_npc *owner)
{
    return (qa_navigation_services){.context=owner,.world=owner->world,.revision=revision,
        .traversal_begin=prediction_begin,.traversal_admit=prediction_admit,.traversal_end=prediction_end};
}
void application_npc_actor_free(npc_actor *actor)
{
    if(!actor) return;
    qa_nav_route_free(&actor->route);qa_nav_workspace_destroy(actor->workspace);
    qa_navigation_destroy(actor->navigation);free(actor);
}
void application_npc_owner_free(application_bots_npc *owner)
{
    if(!owner) return;
    while(owner->actors) {
        npc_actor *actor=owner->actors;owner->actors=actor->next;application_npc_actor_free(actor);
    }
    while(owner->graphs) {
        npc_graph *graph=owner->graphs;owner->graphs=graph->next;
        qa_nav_graph_release(graph->graph);qa_resource_release(graph->asset);
        qa_vfs_acquisition_dispose(&graph->acquisition);free(graph);
    }
    qa_vfs_destroy(owner->files);qa_resource_release(owner->map_resource);free(owner);
}
void application_bots_npc_destroy(application_provider *source)
{
    if(!source) return;
    application_bots_npc *owner=source->bots_npc;source->bots_npc=NULL;
    application_npc_owner_free(owner);
}
bool application_bots_npc_idle(const application_provider *source)
{
    return !source || !source->bots_npc || !source->bots_npc->busy;
}
bool application_bots_npc_content_visit(const application_provider *source,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    const application_bots_npc *owner=source?source->bots_npc:NULL;
    if(!owner) return true;
    if(!visitor || !visitor->view || owner->busy || !application_npc_current(owner,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation content is borrowed or stale");
    if(owner->files && !visitor->view(visitor->context,owner->files,error)) return false;
    for(npc_graph *graph=owner->graphs;graph;graph=graph->next) {
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
    if(!source || (source->kind!=APPLICATION_PROVIDER_Q1 && source->kind!=APPLICATION_PROVIDER_QC) ||
       !app->map_resource || !out)
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation needs its actual Q1 source map");
    application_bots_npc *owner=calloc(1,sizeof(*owner));
    if(!owner) return application_fail(error,QA_ERROR_MEMORY,"Retaining source monster navigation");
    owner->source=source;owner->world=app->world;owner->physics=app->physics;
    owner->map_resource=app->map_resource;qa_resource_retain(owner->map_resource);
    qa_q1_options options={0};double time;
    bool okay=application_npc_current(owner,error);
    if(okay && source->kind==APPLICATION_PROVIDER_Q1)
        okay=prepared?qa_q1_source_respawn_options_prepared(source->state.q1,&options,error):
            qa_q1_source_respawn_options_read(source->state.q1,&options,&time,error);
    else if(okay) {
        qa_clock_kind kind=source->launch->selection.clock.kind;
        if(kind!=QA_CLOCK_NETQUAKE && kind!=QA_CLOCK_QUAKEWORLD)
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Original monster source has no Q1 movement clock");
        options.quakeworld=kind==QA_CLOCK_QUAKEWORLD;
    }
    if(okay) okay=qa_bsp_open(qa_resource_bytes(owner->map_resource),&owner->geometry,error);
    if(okay) {
        owner->movement=qa_movement_profile_default(options.quakeworld?QA_MOVEMENT_QUAKEWORLD:QA_MOVEMENT_NETQUAKE);
        owner->map=(qa_nav_map){.name=app->current_map,.format=owner->geometry.format};
        qa_sha256_digest digest;qa_sha256(owner->geometry.source,&digest);
        memcpy(owner->map.digest,digest.bytes,32);
    }
    if(!okay) {application_npc_owner_free(owner);return false;}
    *out=owner;return true;
}
static bool graph_asset(application_bots_npc *owner,const qa_nav_profile *profile,npc_graph *graph,
    bool *found,qa_error *error)
{
    *found=false;
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
    char *name=malloc(stem+22);
    if(!name) return application_fail(error,QA_ERROR_MEMORY,"Retaining monster navigation path");
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
        qa_bytes bytes=qa_resource_bytes(resource);qa_nav_asset *asset=NULL;
        uint32_t word=qa_block_checksum(owner->geometry.source);int32_t checksum;
        memcpy(&checksum,&word,sizeof(checksum));
        qa_navigation_services services=application_npc_services(owner);
        services.topology_geometry_only=true;
        okay=qa_nav_asset_read(bytes,aas?&checksum:NULL,&asset,error) &&
            qa_nav_graph_from_asset(&owner->map,asset,profile,&services,&graph->graph,error);
        if(okay) {
            graph->asset=resource;resource=NULL;
            graph->acquisition=acquisition;acquisition=(qa_vfs_acquisition){0};*found=true;
        }
        qa_nav_asset_release(asset);qa_resource_release(resource);qa_vfs_acquisition_dispose(&acquisition);break;
    }
    free(name);return okay;
}
static bool graph_for(application_bots_npc *owner,qa_bounds bounds,uint32_t flags,npc_graph **out,qa_error *error)
{
    flags&=QA_PHYSICS_FLYING|QA_PHYSICS_SWIMMING;
    for(npc_graph *graph=owner->graphs;graph;graph=graph->next)
        if(graph->flags==flags && !memcmp(&graph->bounds,&bounds,sizeof(bounds))) {*out=graph;return true;}
    npc_graph *graph=calloc(1,sizeof(*graph));
    if(!graph) return application_fail(error,QA_ERROR_MEMORY,"Retaining monster navigation graph");
    qa_nav_profile profile={.movement=owner->movement,.shape={QA_SHAPE_BOX,bounds},
        .policy={.family=QA_COLLISION_Q1,.q1_hull=-1},.maximum_step=18,.minimum_floor_normal=.7f,
        .maximum_drop=18,.monster=true,.capabilities=QA_NAV_CAPABILITY(QA_NAV_WALK)|
            QA_NAV_CAPABILITY(QA_NAV_DROP)|QA_NAV_CAPABILITY(QA_NAV_SWIM)};
    bool found;bool okay=graph_asset(owner,&profile,graph,&found,error);
    if(okay && !found) {
        qa_nav_construction construction={.geometry=&owner->geometry,.map=owner->map,.profile=profile};
        qa_navigation_services services=application_npc_services(owner);
        services.topology_geometry_only=true;
        okay=qa_nav_graph_construct(&construction,&services,&graph->graph,error);
    }
    if(!okay) {qa_nav_graph_release(graph->graph);qa_resource_release(graph->asset);
        qa_vfs_acquisition_dispose(&graph->acquisition);free(graph);return false;}
    graph->bounds=bounds;graph->flags=flags;graph->next=owner->graphs;owner->graphs=graph;
    *out=graph;return true;
}
bool application_npc_actor_create(application_bots_npc *owner,qa_actor_id id,npc_graph *graph,
    npc_actor **out,qa_error *error)
{
    npc_actor *actor=calloc(1,sizeof(*actor));
    if(!actor) return application_fail(error,QA_ERROR_MEMORY,"Retaining actual monster route owner");
    actor->actor=id;actor->graph=graph;
    qa_navigation_services services=application_npc_services(owner);
    bool okay=qa_navigation_create(graph->graph,&services,&actor->navigation,error) &&
        qa_nav_workspace_create(&actor->workspace,error);
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
    npc_actor **link=&owner->actors;
    while(*link && !qa_actor_id_equal((*link)->actor,id)) link=&(*link)->next;
    if(*link) {
        npc_actor *actor=*link;
        if(owner->busy) {actor->retired=true;return;}
        *link=actor->next;application_npc_actor_free(actor);
    }
}
static bool walking(void *context,const qa_nav_edge *edge)
{
    (void)context;return edge->mode==QA_NAV_WALK || edge->mode==QA_NAV_DROP || edge->mode==QA_NAV_SWIM;
}
static bool route_copy(const qa_nav_route *source,qa_nav_route *out,qa_error *error)
{
    qa_nav_route result={.travel_seconds=source->travel_seconds,.generation=source->generation,
        .found=source->found};
    if(source->node_count>SIZE_MAX/sizeof(*result.nodes) ||
       source->edge_count>SIZE_MAX/sizeof(*result.edges) ||
       source->point_count>SIZE_MAX/sizeof(*result.points))
        return application_fail(error,QA_ERROR_MEMORY,"Cloned monster route extent overflow");
    if(source->node_count) result.nodes=malloc(source->node_count*sizeof(*result.nodes));
    if(source->edge_count) result.edges=malloc(source->edge_count*sizeof(*result.edges));
    if(source->point_count) result.points=malloc(source->point_count*sizeof(*result.points));
    if((source->node_count&&!result.nodes) || (source->edge_count&&!result.edges) ||
       (source->point_count&&!result.points)) {
        qa_nav_route_free(&result);
        return application_fail(error,QA_ERROR_MEMORY,"Retaining cloned monster route");
    }
    if(source->node_count) memcpy(result.nodes,source->nodes,source->node_count*sizeof(*result.nodes));
    if(source->edge_count) memcpy(result.edges,source->edges,source->edge_count*sizeof(*result.edges));
    if(source->point_count) memcpy(result.points,source->points,source->point_count*sizeof(*result.points));
    result.node_count=result.node_capacity=source->node_count;
    result.edge_count=result.edge_capacity=source->edge_count;
    result.point_count=result.point_capacity=source->point_count;
    result.graph=source->graph;qa_nav_graph_retain(result.graph);*out=result;return true;
}
bool application_bots_npc_clone(void *opaque,qa_actor_id from,qa_actor_id to,qa_error *error)
{
    application_provider *source=opaque;
    application_bots_npc *owner=source?source->bots_npc:NULL;
    if(!owner) return true;
    npc_actor *original=owner->actors;
    while(original && !qa_actor_id_equal(original->actor,from)) original=original->next;
    if(!original || !original->has_path || original->retired) return true;
    if(qa_actor_id_equal(from,to) || !application_npc_current(owner,error)) return false;
    qa_body_state body;qa_physics_properties properties;bool found;
    if(!actual(owner,to,&body,&properties,&found,error)) return false;
    if(!found) return application_fail(error,QA_ERROR_NOT_FOUND,"Cloned monster route lacks its actual target body");
    for(npc_actor *held=owner->actors;held;held=held->next)
        if(qa_actor_id_equal(held->actor,to))
            return application_fail(error,QA_ERROR_ARGUMENT,"Cloned monster route target already has a continuation");
    npc_actor *copy=NULL;qa_nav_checkpoint continuation={0};
    bool okay=application_npc_actor_create(owner,to,original->graph,&copy,error) &&
        route_copy(&original->route,&copy->route,error) &&
        qa_navigation_capture(original->navigation,&continuation,error) &&
        qa_navigation_restore(copy->navigation,&continuation,error);
    qa_nav_checkpoint_free(&continuation);
    if(!okay) {application_npc_actor_free(copy);return false;}
    copy->goal=original->goal;copy->cursor=original->cursor;copy->has_path=true;
    copy->next=owner->actors;owner->actors=copy;return true;
}
static bool walk(application_bots_npc *owner,qa_actor_id id,qa_vec3 goal,float distance,
    qa_q1_path_result *result,qa_error *error)
{
    qa_body_state body;qa_physics_properties properties;bool found;
    if(!actual(owner,id,&body,&properties,&found,error)) return false;
    if(!found) {application_bots_npc_released(owner->source,id);return true;}
    npc_graph *graph;
    if(!graph_for(owner,body.bounds,properties.flags,&graph,error)) return false;
    npc_actor *actor=owner->actors;
    while(actor && !qa_actor_id_equal(actor->actor,id)) actor=actor->next;
    if(!actor) {
        if(!application_npc_actor_create(owner,id,graph,&actor,error)) return false;
        actor->next=owner->actors;owner->actors=actor;
    } else if(actor->graph!=graph) {
        qa_navigation_services services=application_npc_services(owner);qa_navigation *next=NULL;
        if(!qa_navigation_create(graph->graph,&services,&next,error)) return false;
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
    npc_actor **link=&owner->actors;
    while(*link) {
        npc_actor *actor=*link;
        if(actor->retired || !qa_actors_get(qa_session_actors(source->application->session),actor->actor)) {
            *link=actor->next;application_npc_actor_free(actor);
        } else link=&actor->next;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
