#include "collision/world_internal.h"

#include <stdalign.h>

static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }

static void build_sector(qa_world *world,uint32_t index,uint32_t parent,
                         qa_bounds bounds,unsigned depth,uint32_t *next)
{
    qa_spatial_sector *sector=&world->sectors[index];
    sector->head=sector->tail=QA_SPATIAL_NONE;
    sector->parent=parent;
    sector->subtree_links=0;
    sector->axis=-1;
    if(depth==4) return;
    unsigned axis=(bounds.maxs.x-bounds.mins.x)>(bounds.maxs.y-bounds.mins.y)?0u:1u;
    float distance=(qa_vec_component(bounds.maxs,axis)+qa_vec_component(bounds.mins,axis))*0.5f;
    sector->axis=(int)axis; sector->distance=distance;
    qa_bounds front=bounds,back=bounds;
    qa_vec_set_component(&front.mins,axis,distance); qa_vec_set_component(&back.maxs,axis,distance);
    sector->front=(*next)++; build_sector(world,sector->front,index,front,depth+1,next);
    sector->back=(*next)++; build_sector(world,sector->back,index,back,depth+1,next);
}

bool qa_spatial_initialize(qa_world *world,qa_bounds bounds,qa_error *error)
{
    if(!qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial world bounds");
    uint32_t next=1; build_sector(world,0,QA_SPATIAL_NONE,bounds,0,&next); return true;
}

static void adjust_occupancy(qa_world *world,uint32_t index,bool add)
{
    do {
        qa_spatial_sector *sector=&world->sectors[index];
        if(add) ++sector->subtree_links;
        else --sector->subtree_links;
        index=sector->parent;
    } while(index!=QA_SPATIAL_NONE);
}

void qa_spatial_remove(qa_world *world,uint32_t slot)
{
    qa_spatial_link *link=qa_actors_link(world->actors->links,slot);
    if(!link->linked) return;
    uint32_t previous=link->previous,next=link->next;
    qa_spatial_sector *sector=&world->sectors[link->sector];
    for(qa_spatial_cursor *cursor=world->cursors;cursor!=NULL;cursor=cursor->outer)
        if(cursor->next==slot) cursor->next=next;
    if(previous==QA_SPATIAL_NONE) sector->head=next;
    else qa_actors_link(world->actors->links,previous)->next=next;
    if(next==QA_SPATIAL_NONE) sector->tail=previous;
    else qa_actors_link(world->actors->links,next)->previous=previous;
    adjust_occupancy(world,link->sector,false);
    link->linked=false;
    link->previous=link->next=QA_SPATIAL_NONE;
}

void qa_spatial_publish(qa_world *world,uint32_t slot)
{
    qa_spatial_remove(world,slot);
    qa_spatial_link *link=qa_actors_link(world->actors->links,slot);
    uint32_t index=0;
    qa_bounds bounds=link->bounds;
    while(world->sectors[index].axis>=0) {
        qa_spatial_sector *sector=&world->sectors[index]; unsigned axis=(unsigned)sector->axis;
        if(qa_vec_component(bounds.mins,axis)>sector->distance) index=sector->front;
        else if(qa_vec_component(bounds.maxs,axis)<sector->distance) index=sector->back;
        else break;
    }
    qa_spatial_sector *sector=&world->sectors[index]; link->sector=index;
    if(qa_actors_body(world->actors->pages,slot)->linked_collision.family==QA_COLLISION_Q3) {
        link->previous=QA_SPATIAL_NONE;
        link->next=sector->head;
        if(sector->head!=QA_SPATIAL_NONE) qa_actors_link(world->actors->links,sector->head)->previous=slot;
        else sector->tail=slot;
        sector->head=slot;
    } else {
        link->previous=sector->tail;
        link->next=QA_SPATIAL_NONE;
        if(sector->tail!=QA_SPATIAL_NONE) qa_actors_link(world->actors->links,sector->tail)->next=slot;
        else sector->head=slot;
        sector->tail=slot;
    }
    link->linked=true;
    adjust_occupancy(world,index,true);
}

void qa_spatial_clear(qa_world *world)
{
    for(uint32_t index=0;index<QA_SPATIAL_SECTORS;++index) {
        uint32_t slot=world->sectors[index].head;
        while(slot!=QA_SPATIAL_NONE) {
            qa_spatial_link *link=qa_actors_link(world->actors->links,slot);
            slot=link->next;
            link->linked=false;
            link->previous=link->next=QA_SPATIAL_NONE;
        }
        world->sectors[index].head=world->sectors[index].tail=QA_SPATIAL_NONE;
        world->sectors[index].subtree_links=0;
    }
}

void qa_spatial_rebuild_occupancy(qa_world *world)
{
    for(uint32_t index=0;index<QA_SPATIAL_SECTORS;++index)
        world->sectors[index].subtree_links=0;
    for(uint32_t index=0;index<QA_SPATIAL_SECTORS;++index)
        for(uint32_t slot=world->sectors[index].head;slot!=QA_SPATIAL_NONE;
            slot=qa_actors_link(world->actors->links,slot)->next)
            adjust_occupancy(world,index,true);
}

void qa_spatial_dispose(qa_world *world)
{
    qa_spatial_clear(world);
    qa_arena_destroy(&world->snapshot_storage);
    world->snapshot_pool=(qa_pool){0};
}

bool qa_spatial_prepare_snapshots(qa_world *world,size_t capacity,qa_error *error)
{
    size_t actors=world->capacity;
    size_t offset=(sizeof(qa_world_snapshot_frame)+alignof(qa_actor_id)-1)
        & ~(alignof(qa_actor_id)-1);
    if(actors>(SIZE_MAX-offset)/sizeof(qa_actor_id))
        return fail(error,QA_ERROR_MEMORY,"Spatial snapshot actor capacity is too large");
    size_t alignment=alignof(qa_world_snapshot_frame)>alignof(qa_actor_id)?
        alignof(qa_world_snapshot_frame):alignof(qa_actor_id);
    if(!qa_pool_prepare(&world->snapshot_pool,&world->snapshot_storage,capacity,
        offset+actors*sizeof(qa_actor_id),alignment,error)) return false;
    for(size_t i=0;i<capacity;++i) {
        qa_world_snapshot_frame *frame=qa_pool_at(&world->snapshot_pool,i);
        *frame=(qa_world_snapshot_frame){.actors=(qa_actor_id *)((uint8_t *)frame+offset),
            .capacity=actors,.slot=i};
    }
    qa_arena_seal(&world->snapshot_storage);
    return true;
}

qa_world_snapshot_usage qa_world_snapshot_statistics(const qa_world *world)
{
    return world==NULL?(qa_world_snapshot_usage){0}:(qa_world_snapshot_usage){
        world->snapshot_pool.capacity,world->snapshot_pool.active,world->snapshot_pool.peak,
        world->snapshot_pool.overflow};
}

static bool visit_sector(qa_world *world,uint32_t index,const qa_bounds *bounds,qa_spatial_raw_fn visit,void *context,qa_spatial_cursor *cursor)
{
    qa_spatial_link *links=world->actors->links;
    qa_spatial_sector *sector=&world->sectors[index];
    if(sector->subtree_links==0) return true;
    uint32_t next=sector->head;
    qa_spatial_visit result=QA_SPATIAL_CONTINUE;
    while(next!=QA_SPATIAL_NONE) {
        uint32_t slot=next;
        qa_spatial_link *link=qa_actors_link(links,slot);
        next=link->next;
        if(qa_bounds_overlap(link->bounds,*bounds)) {
            cursor->next=next;
            result=visit(context,slot);
            next=cursor->next;
            if(result!=QA_SPATIAL_CONTINUE) break;
        }
    }
    if(result==QA_SPATIAL_STOP) return false;
    if(result==QA_SPATIAL_STOP_SECTOR) return true;
    if(sector->axis>=0) {
        unsigned axis=(unsigned)sector->axis;
        if(qa_vec_component(bounds->maxs,axis)>sector->distance && !visit_sector(world,sector->front,bounds,visit,context,cursor)) return false;
        if(qa_vec_component(bounds->mins,axis)<sector->distance && !visit_sector(world,sector->back,bounds,visit,context,cursor)) return false;
    }
    return true;
}

bool qa_spatial_visit_raw(qa_world *world,qa_bounds bounds,qa_spatial_raw_fn visit,void *context,qa_error *error)
{
    if(world==NULL || visit==NULL || !qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial visit");
    if(world->visit_depth==UINT32_MAX) return fail(error,QA_ERROR_ARGUMENT,"Spatial visit nesting exhausted");
    qa_spatial_cursor cursor={.outer=world->cursors,.next=QA_SPATIAL_NONE};
    world->cursors=&cursor;
    ++world->visit_depth; (void)visit_sector(world,0,&bounds,visit,context,&cursor); --world->visit_depth;
    world->cursors=cursor.outer;
    return true;
}


typedef struct world_visit_context {
    qa_world *world;
    qa_collision_role role;
    qa_spatial_visit_fn visit;
    void *context;
    qa_error error;
    bool failed;
} world_visit_context;

static qa_spatial_visit visit_current(void *opaque,uint32_t slot)
{
    world_visit_context *context=opaque;
    qa_spatial_actor actor; qa_error error={0};
    qa_actor_id id=qa_actors_body(context->world->actors->pages,slot)->actor;
    if(!qa_world_refresh(context->world,id,QA_ENTITY_CLIP_POSE,QA_ENTITY_BODY_ALL,&actor,&error)) {
        if(error.code!=QA_OK) { context->error=error; context->failed=true; return QA_SPATIAL_STOP; }
        return QA_SPATIAL_CONTINUE;
    }
    if(context->role!=QA_COLLISION_BOTH && actor.collision.role!=context->role
        && actor.collision.role!=QA_COLLISION_BOTH) return QA_SPATIAL_CONTINUE;
    ++context->world->callback_depth;
    qa_spatial_visit result=context->visit(context->context,&actor);
    --context->world->callback_depth; return result;
}

bool qa_world_visit(qa_world *world,qa_bounds bounds,qa_collision_role role,qa_spatial_visit_fn visit,void *opaque,qa_error *error)
{
    if(visit==NULL || role<QA_COLLISION_SOLID || role>QA_COLLISION_BOTH) return fail(error,QA_ERROR_ARGUMENT,"Invalid world query callback or role");
    world_visit_context context={.world=world,.role=role,.visit=visit,.context=opaque};
    if(!qa_spatial_visit_raw(world,bounds,visit_current,&context,error)) return false;
    if(context.failed) { if(error!=NULL) *error=context.error; return false; }
    return true;
}

typedef struct query_context {
    qa_world *world;
    qa_collision_role role;
    qa_actor_id *actors;
    size_t capacity,count;
    bool overflow,failed;
    qa_error error;
} query_context;
static qa_spatial_visit collect_actor(void *opaque,uint32_t slot)
{
    query_context *context=opaque;
    qa_world_body *body=qa_world_raw_body(context->world,slot);
    if(body==NULL) return QA_SPATIAL_CONTINUE;
    qa_actor_collision collision; qa_error error={0};
    if(!qa_world_collision_sample(body,false,QA_ENTITY_COLLISION_ROLE,&collision,&error)) {
        if(error.code!=QA_OK) { context->error=error; context->failed=true; return QA_SPATIAL_STOP; }
        return QA_SPATIAL_CONTINUE;
    }
    if(context->role!=QA_COLLISION_BOTH && collision.role!=context->role
        && collision.role!=QA_COLLISION_BOTH) return QA_SPATIAL_CONTINUE;
    if(context->count<context->capacity) context->actors[context->count++]=body->actor;
    else context->overflow=true;
    return QA_SPATIAL_CONTINUE;
}

bool qa_world_query(qa_world *world,qa_bounds bounds,qa_collision_role role,qa_actor_id *actors,size_t capacity,size_t *count,bool *overflow,qa_error *error)
{
    if(count==NULL || overflow==NULL || (capacity!=0 && actors==NULL)
        || role<QA_COLLISION_SOLID || role>QA_COLLISION_BOTH)
        return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial query output");
    query_context context={.world=world,.role=role,.actors=actors,.capacity=capacity};
    if(!qa_spatial_visit_raw(world,bounds,collect_actor,&context,error)) return false;
    if(context.failed) { if(error!=NULL) *error=context.error; return false; }
    *count=context.count; *overflow=context.overflow; return true;
}

static qa_spatial_visit snapshot_actor(void *opaque,qa_actor_id actor)
{
    qa_world_actor_snapshot *snapshot=opaque;
    if(snapshot->count==snapshot->capacity) {
        qa_world *world=snapshot->world;
        size_t slot;
        qa_world_snapshot_frame *frame=qa_pool_take(&world->snapshot_pool,&slot);
        if(frame==NULL) {
            snapshot->failed=true;
            (void)fail(snapshot->error,QA_ERROR_MEMORY,"Spatial snapshot capacity exhausted");
            return QA_SPATIAL_STOP;
        }
        snapshot->frame=frame;
        memcpy(frame->actors,snapshot->local,snapshot->count*sizeof(*snapshot->actors));
        snapshot->actors=frame->actors;
        snapshot->capacity=frame->capacity;
    }
    snapshot->actors[snapshot->count++]=actor;
    return QA_SPATIAL_CONTINUE;
}

static qa_spatial_visit snapshot_slot(void *opaque,uint32_t slot)
{
    qa_world_actor_snapshot *snapshot=opaque;
    return snapshot_actor(opaque,qa_actors_body(snapshot->world->actors->pages,slot)->actor);
}

static qa_spatial_visit snapshot_current(void *opaque,const qa_spatial_actor *actor)
{ return snapshot_actor(opaque,actor->body.actor); }

void qa_world_snapshot_release(qa_world_actor_snapshot *snapshot)
{
    if(snapshot->frame!=NULL) {
        qa_pool_release(&snapshot->world->snapshot_pool,snapshot->frame->slot);
        snapshot->frame=NULL;
    }
}

bool qa_world_snapshot_capture(qa_world *world,qa_bounds bounds,qa_collision_role role,
                               qa_world_actor_snapshot *out,qa_error *error)
{
    out->actors=out->local;
    out->count=0;
    out->capacity=sizeof(out->local)/sizeof(out->local[0]);
    out->world=world;
    out->frame=NULL;
    out->error=error;
    out->failed=false;
    bool ok=role==QA_COLLISION_BOTH?
        qa_spatial_visit_raw(world,bounds,snapshot_slot,out,error):
        qa_world_visit(world,bounds,role,snapshot_current,out,error);
    if(!ok || out->failed) {
        qa_world_snapshot_release(out);
        return false;
    }
    return true;
}

typedef struct trigger_context {
    qa_world *world;
    qa_actor_id actor;
    qa_world_is_trigger_fn is_trigger;
    qa_world_touch_fn touch;
    void *context;
    qa_error error;
    bool failed;
} trigger_context;

static void touch_candidate(trigger_context *context,qa_actor_id candidate)
{
    if(context->failed || qa_actor_id_equal(candidate,context->actor)) return;
    qa_linked_body trigger,moving; qa_actor_collision collision;
    if(!qa_world_linked(context->world,context->actor,&moving)
        || !qa_world_linked(context->world,candidate,&trigger)) return;
    if(!qa_world_get_collision(context->world,candidate,&collision,&context->error)) {
        context->failed=context->error.code!=QA_OK;
        return;
    }
    if((collision.role!=QA_COLLISION_TRIGGER && collision.role!=QA_COLLISION_BOTH)
        || !qa_world_linked(context->world,context->actor,&moving)
        || !qa_world_linked(context->world,candidate,&trigger)
        || !qa_bounds_overlap(trigger.absolute_bounds,moving.absolute_bounds)) return;
    ++context->world->callback_depth;
    bool valid=context->is_trigger==NULL || context->is_trigger(context->context,candidate);
    if(valid && qa_actors_get(context->world->actors,candidate)!=NULL && qa_actors_get(context->world->actors,context->actor)!=NULL) {
        qa_touch_contact contact={.self=candidate,.other=context->actor};
        context->touch(context->context,context->world,&contact);
    }
    --context->world->callback_depth;
}

static qa_spatial_visit touch_live(void *opaque,uint32_t slot)
{
    trigger_context *context=opaque;
    if(qa_actors_get(context->world->actors,context->actor)==NULL) return QA_SPATIAL_STOP;
    qa_actor_id candidate=qa_actors_body(context->world->actors->pages,slot)->actor;
    touch_candidate(context,candidate);
    return !context->failed && qa_actors_get(context->world->actors,context->actor)!=NULL?QA_SPATIAL_CONTINUE:QA_SPATIAL_STOP;
}

bool qa_world_touch_triggers(qa_world *world,qa_actor_id actor,qa_collision_family family,
                            qa_world_is_trigger_fn is_trigger,qa_world_touch_fn touch,void *opaque,qa_error *error)
{
    if(world==NULL || touch==NULL || family<QA_COLLISION_Q1 || family>QA_COLLISION_Q3) return fail(error,QA_ERROR_ARGUMENT,"Invalid trigger dispatch");
    qa_linked_body moving;
    if(!qa_world_linked(world,actor,&moving)) return true;
    trigger_context context={.world=world,.actor=actor,.is_trigger=is_trigger,.touch=touch,.context=opaque};
    if(family==QA_COLLISION_Q1) {
        if(!qa_spatial_visit_raw(world,moving.absolute_bounds,touch_live,&context,error)) return false;
        if(context.failed && error!=NULL) *error=context.error;
        return !context.failed;
    }
    qa_world_actor_snapshot candidates;
    if(!qa_world_snapshot_capture(world,moving.absolute_bounds,QA_COLLISION_TRIGGER,&candidates,error)) return false;
    for(size_t i=0;i<candidates.count && !context.failed && qa_actors_get(world->actors,actor)!=NULL;++i) touch_candidate(&context,candidates.actors[i]);
    bool ok=!context.failed;
    if(context.failed && error!=NULL) *error=context.error;
    qa_world_snapshot_release(&candidates); return ok;
}
