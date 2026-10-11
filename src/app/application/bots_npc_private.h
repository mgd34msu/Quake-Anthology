#ifndef QA_APPLICATION_BOTS_NPC_PRIVATE_H
#define QA_APPLICATION_BOTS_NPC_PRIVATE_H
#include "internal.h"
#include "bots_npc.h"
#include "qa/navigation.h"
#include "qa/pool.h"
#include "qa/source_save.h"

typedef struct npc_graph {
    struct npc_graph *next;
    qa_bounds bounds;
    uint32_t flags;
    qa_nav_graph *graph;
    qa_resource *asset;
    qa_vfs_acquisition acquisition;
} npc_graph;
typedef struct npc_actor {
    struct npc_actor *next,*previous;
    qa_actor_id actor;
    npc_graph *graph;
    qa_navigation *navigation;
    qa_nav_workspace *workspace;
    qa_nav_route route;
    qa_vec3 goal;
    size_t cursor;
    bool has_path,retired;
} npc_actor;
typedef struct application_bots_npc {
    application_provider *source;
    qa_world *world;
    qa_physics *physics;
    qa_resource *map_resource;
    qa_bsp_view geometry;
    qa_nav_map map;
    qa_movement_profile movement;
    qa_vfs *files;
    npc_graph *graphs;
    npc_actor *actors;
    qa_actor_index actor_routes;
    qa_arena prediction_storage;
    qa_pool predictions;
    size_t prediction_points;
    bool busy;
} application_bots_npc;

static inline npc_actor *application_npc_actor_find(const application_bots_npc *owner,qa_actor_id actor)
{
    size_t value;
    return qa_actor_index_find(&owner->actor_routes,actor,&value)
        ? (npc_actor *)(uintptr_t)value : NULL;
}
static inline void application_npc_actor_link(application_bots_npc *owner,npc_actor *actor)
{
    actor->next=owner->actors;
    if(actor->next) actor->next->previous=actor;
    owner->actors=actor;
    qa_actor_index_put(&owner->actor_routes,actor->actor,(size_t)(uintptr_t)actor);
}
static inline void application_npc_actor_unlink(application_bots_npc *owner,npc_actor *actor)
{
    if(actor->previous) actor->previous->next=actor->next;
    else owner->actors=actor->next;
    if(actor->next) actor->next->previous=actor->previous;
    qa_actor_index_remove(&owner->actor_routes,actor->actor);
}

bool application_npc_owner_create(application_provider *,bool prepared,application_bots_npc **,qa_error *);
void application_npc_owner_free(application_bots_npc *);
bool application_npc_current(const application_bots_npc *,qa_error *);
bool application_npc_physics_read(application_provider *,qa_actor_id,qa_physics_properties *,bool *found,qa_error *);
qa_navigation_services application_npc_services(application_bots_npc *);
bool application_npc_graph_rebuild(application_bots_npc *,npc_graph *,qa_error *);
bool application_npc_actor_create(application_bots_npc *,qa_actor_id,npc_graph *,npc_actor **,qa_error *);
void application_npc_actor_free(npc_actor *);
#endif
