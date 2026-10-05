#include "bots_npc_private.h"
#include "bots_save_private.h"
#include "qa/persistence_navigation.h"
#include "qa/persistence_fields.h"
#include "qa/hash.h"
#include "qa/binary.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_source_save_io *io,const char *message)
{
    if(!io->failed) qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,"%s",message);
    io->failed=true;return false;
}
static bool bytes(qa_source_save_io *io,qa_buffer *buffer)
{
    size_t size=buffer->size;
    if(!qa_source_save_count(io,&size,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(size>io->input.size-io->offset) return fail(io,"Truncated monster navigation bytes");
        if(size) {
            buffer->data=malloc(size);
            if(!buffer->data) return application_fail(io->error,QA_ERROR_MEMORY,"Retaining monster navigation bytes");
        }
        buffer->size=size;
    }
    return qa_source_save_bytes(io,buffer->data,size);
}
static size_t graph_count(const application_bots_npc *owner)
{
    size_t count=0;for(npc_graph *graph=owner->graphs;graph;graph=graph->next) ++count;return count;
}
static size_t graph_number(const application_bots_npc *owner,const qa_nav_graph *value)
{
    size_t index=1;
    for(npc_graph *graph=owner->graphs;graph;graph=graph->next,++index)
        if(graph->graph==value) return index;
    return 0;
}
static npc_graph *graph_at(application_bots_npc *owner,size_t index)
{
    npc_graph *graph=owner->graphs;
    while(graph && index>1) {graph=graph->next;--index;}
    return index==1?graph:NULL;
}
static bool graph_fields(qa_source_save_io *io,application_bots_npc *owner,npc_graph *graph)
{
    if(!qa_persistence_bounds(io,&graph->bounds) || !qa_source_save_u32(io,&graph->flags) ||
       (graph->flags&~(uint32_t)(QA_PHYSICS_FLYING|QA_PHYSICS_SWIMMING)) ||
       !application_bot_resource_field(io,owner->source->application,owner->files,
            &graph->asset,&graph->acquisition,io->direction==QA_SOURCE_SAVE_READ))
        return fail(io,"Invalid monster graph profile");
    if(io->direction==QA_SOURCE_SAVE_READ && !application_npc_graph_rebuild(owner,graph,io->error))
        return false;
    const qa_nav_graph_view *view=qa_nav_graph_read(graph->graph);
    if(!view || !view->profile.monster || view->profile.movement.kind!=owner->movement.kind ||
       memcmp(&view->profile.shape.bounds,&graph->bounds,sizeof(graph->bounds)) ||
       view->profile.maximum_step!=18 || view->profile.maximum_drop!=18 ||
       view->profile.minimum_floor_normal!=.7f || view->profile.policy.family!=QA_COLLISION_Q1 ||
       view->profile.policy.q1_hull!=-1 || view->profile.capabilities!=
        (QA_NAV_CAPABILITY(QA_NAV_WALK)|QA_NAV_CAPABILITY(QA_NAV_DROP)|QA_NAV_CAPABILITY(QA_NAV_SWIM)))
        return fail(io,"Monster graph is not its genuine source walking profile");
    return true;
}
static bool route_fields(qa_source_save_io *io,application_bots_npc *owner,npc_actor *actor)
{
    qa_nav_route *route=&actor->route;
    size_t graph=io->direction==QA_SOURCE_SAVE_WRITE?graph_number(owner,route->graph):0;
    if(!qa_source_save_count(io,&graph,graph_count(owner)) || !graph)
        return fail(io,"Missing monster route graph");
    npc_graph *source=graph_at(owner,graph);
    if(io->direction==QA_SOURCE_SAVE_READ) {
        route->graph=source->graph;qa_nav_graph_retain(route->graph);route->found=true;
    }
    size_t nodes=route->node_count,edges=route->edge_count,points=route->point_count;
    if(!qa_source_save_count(io,&nodes,SIZE_MAX/sizeof(*route->nodes)) ||
       !qa_source_save_count(io,&edges,SIZE_MAX/sizeof(*route->edges)) ||
       !qa_source_save_count(io,&points,SIZE_MAX/sizeof(*route->points)) ||
       !qa_source_save_count(io,&actor->cursor,points) ||
       !qa_source_save_f32(io,&route->travel_seconds) ||
       !qa_source_save_u64(io,&route->generation) || !qa_source_save_vec3(io,&actor->goal) ||
       !isfinite(route->travel_seconds) || route->travel_seconds<0 || !qa_vec_finite(actor->goal))
        return fail(io,"Invalid monster route continuation");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        size_t remaining=io->input.size-io->offset;
        if(nodes>remaining/4 || edges>(remaining-nodes*4)/4 ||
           points>(remaining-nodes*4-edges*4)/12) return fail(io,"Truncated monster route");
        if(nodes) route->nodes=malloc(nodes*sizeof(*route->nodes));
        if(edges) route->edges=malloc(edges*sizeof(*route->edges));
        if(points) route->points=malloc(points*sizeof(*route->points));
        if((nodes&&!route->nodes) || (edges&&!route->edges) || (points&&!route->points))
            return application_fail(io->error,QA_ERROR_MEMORY,"Retaining restored monster route");
        route->node_count=route->node_capacity=nodes;route->edge_count=route->edge_capacity=edges;
        route->point_count=route->point_capacity=points;
    }
    const qa_nav_graph_view *view=qa_nav_graph_read(source->graph);
    for(size_t i=0;i<nodes;++i) {
        if(!qa_source_save_u32(io,route->nodes+i)) return false;
        bool found=false;
        for(size_t j=0;j<view->node_count;++j) if(view->nodes[j].id==route->nodes[i]) {found=true;break;}
        if(!found) return fail(io,"Missing saved monster route node");
    }
    for(size_t i=0;i<edges;++i) {
        if(!qa_source_save_u32(io,route->edges+i)) return false;
        bool found=false;
        for(size_t j=0;j<view->edge_count;++j) if(view->edges[j].id==route->edges[i]) {found=true;break;}
        if(!found) return fail(io,"Missing saved monster route edge");
    }
    for(size_t i=0;i<points;++i)
        if(!qa_source_save_vec3(io,route->points+i) || !qa_vec_finite(route->points[i]))
            return fail(io,"Invalid saved monster route point");
    return true;
}
static bool actor_fields(qa_source_save_io *io,application_bots_npc *owner,npc_actor **value)
{
    npc_actor *actor=*value;qa_actor_id id=actor?actor->actor:(qa_actor_id){0};
    size_t index=actor?graph_number(owner,actor->graph->graph):0;
    if(!qa_source_save_actor(io,&id) || !qa_source_save_count(io,&index,graph_count(owner)) ||
       !index || !qa_actors_get(qa_session_actors(io->session),id))
        return fail(io,"Missing actual restored monster actor");
    npc_graph *graph=graph_at(owner,index);
    if(io->direction==QA_SOURCE_SAVE_READ) {
        for(npc_actor *previous=owner->actors;previous;previous=previous->next)
            if(qa_actor_id_equal(previous->actor,id)) return fail(io,"Duplicate monster route actor");
        if(!application_npc_actor_create(owner,id,graph,&actor,io->error)) return false;
        *value=actor;
    }
    qa_body_state body;qa_physics_properties properties;bool physical;
    if(!application_npc_physics_read(owner->source,id,&properties,&physical,io->error)) return false;
    if(!physical || !(properties.flags&QA_PHYSICS_MONSTER) ||
       !qa_world_body_read(owner->world,id,&body,io->error))
        return fail(io,"Saved monster route has no actual source body");
    qa_buffer encoded={0};bool okay=true;
    if(io->direction==QA_SOURCE_SAVE_WRITE)
        okay=qa_persistence_navigation_capture(io->session,actor->navigation,&encoded,io->error);
    if(okay) okay=bytes(io,&encoded);
    if(okay && io->direction==QA_SOURCE_SAVE_READ)
        okay=qa_persistence_navigation_restore(io->session,actor->navigation,
            (qa_bytes){encoded.data,encoded.size},io->error);
    qa_buffer_free(&encoded);
    if(!okay || !qa_source_save_bool(io,&actor->has_path)) return false;
    return !actor->has_path || route_fields(io,owner,actor);
}
static bool fields(qa_source_save_io *io,application_provider *source,application_bots_npc **value)
{
    uint8_t magic[8]={'Q','A','N','P','C',0,0,0};
    if(!qa_source_save_bytes(io,magic,8) || memcmp(magic,"QANPC\0\0\0",8)) return fail(io,"Invalid monster navigation owner");
    bool present=*value!=NULL;
    if(!qa_source_save_bool(io,&present)) return false;
    if(!present) return true;
    if(io->direction==QA_SOURCE_SAVE_READ && !application_npc_owner_create(source,true,value,io->error)) return false;
    application_bots_npc *owner=*value;
    if(!owner || owner->busy || !application_npc_current(owner,io->error)) return false;
    qa_application_content_graph *content=qa_application_content_graph_read(source->application);
    uint64_t pool=0,resource=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE &&
       !qa_application_content_resource_id(content,owner->map_resource,&pool,&resource))
        return fail(io,"Monster map is outside its actual content pool");
    if(!qa_source_save_u64(io,&pool) || !pool || !qa_source_save_u64(io,&resource) || !resource ||
       qa_application_content_resource(content,pool,resource)!=owner->map_resource)
        return fail(io,"Saved monster map resource differs");
    uint64_t view=io->direction==QA_SOURCE_SAVE_WRITE?qa_application_content_view_id(content,owner->files):0;
    if(!qa_source_save_u64(io,&view) ||
       (io->direction==QA_SOURCE_SAVE_WRITE && owner->files && !view))
        return fail(io,"Monster navigation VFS is outside its actual content graph");
    if(io->direction==QA_SOURCE_SAVE_READ && view &&
       !qa_application_content_retain_view(content,view,&owner->files,io->error)) return false;
    if(owner->files) {
        qa_launch_resource_origin origin;
        if(!qa_application_map_origin_read(source->application,&origin) || !origin.acquisition ||
           origin.acquisition->resource_id!=qa_resource_id(owner->map_resource) ||
           !qa_catalog_product_view_current(origin.catalog,origin.product,owner->files))
            return fail(io,"Monster navigation view differs from actual geometry content");
    }
    size_t graphs=graph_count(owner);
    if(!qa_source_save_count(io,&graphs,SIZE_MAX/sizeof(npc_graph))) return false;
    if(io->direction==QA_SOURCE_SAVE_READ && graphs>io->input.size-io->offset)
        return fail(io,"Truncated monster graph registry");
    npc_graph **tail=&owner->graphs;
    for(size_t i=0;i<graphs;++i) {
        if(io->direction==QA_SOURCE_SAVE_READ) {
            *tail=calloc(1,sizeof(**tail));
            if(!*tail) return application_fail(io->error,QA_ERROR_MEMORY,"Retaining saved monster graph registry");
        }
        npc_graph *graph=*tail;
        if(!graph_fields(io,owner,graph)) return false;
        for(npc_graph *previous=owner->graphs;previous!=graph;previous=previous->next)
            if(previous->flags==graph->flags && !memcmp(&previous->bounds,&graph->bounds,sizeof(graph->bounds)))
                return fail(io,"Duplicate source monster graph profile");
        tail=&graph->next;
    }
    size_t actors=0;
    for(npc_actor *actor=owner->actors;actor;actor=actor->next) ++actors;
    if(!qa_source_save_count(io,&actors,qa_actors_capacity(qa_session_actors(io->session)))) return false;
    npc_actor **actor_tail=&owner->actors;
    for(size_t i=0;i<actors;++i) {
        npc_actor *actor=*actor_tail;
        bool okay=actor_fields(io,owner,&actor);
        if(io->direction==QA_SOURCE_SAVE_READ) *actor_tail=actor;
        if(!okay) return false;
        actor_tail=&actor->next;
    }
    return true;
}
bool application_bots_npc_capture(application_provider *source,qa_buffer *out,qa_error *error)
{
    if(!source || !out || out->data || out->size || !application_bots_npc_idle(source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation capture requires its idle retained owner");
    qa_source_save_io io={0};application_bots_npc *owner=source->bots_npc;
    bool okay=qa_source_save_writer(&io,source->application->session,error) &&
        fields(&io,source,&owner) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return okay;
}
bool application_bots_npc_restore(application_provider *source,qa_bytes bytes_,qa_error *error)
{
    if(!source || !application_bots_npc_idle(source))
        return application_fail(error,QA_ERROR_ARGUMENT,"Monster navigation restore requires its idle actual source");
    qa_source_save_io io={0};application_bots_npc *owner=NULL;
    bool okay=qa_source_save_reader(&io,source->application->session,bytes_,error) &&
        fields(&io,source,&owner) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!okay) {application_npc_owner_free(owner);return false;}
    application_bots_npc_destroy(source);source->bots_npc=owner;return true;
}
