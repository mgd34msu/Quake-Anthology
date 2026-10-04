#include "collision/world_internal.h"

#include <stdlib.h>

static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }

static void build_sector(qa_world *world,uint32_t index,qa_bounds bounds,unsigned depth,uint32_t *next)
{
    qa_spatial_sector *sector=&world->sectors[index];
    sector->axis=-1;
    if(depth==4) return;
    unsigned axis=(bounds.maxs.x-bounds.mins.x)>(bounds.maxs.y-bounds.mins.y)?0u:1u;
    float distance=(qa_vec_component(bounds.maxs,axis)+qa_vec_component(bounds.mins,axis))*0.5f;
    sector->axis=(int)axis; sector->distance=distance;
    qa_bounds front=bounds,back=bounds;
    qa_vec_set_component(&front.mins,axis,distance); qa_vec_set_component(&back.maxs,axis,distance);
    sector->front=(*next)++; build_sector(world,sector->front,front,depth+1,next);
    sector->back=(*next)++; build_sector(world,sector->back,back,depth+1,next);
}

bool qa_spatial_initialize(qa_world *world,qa_bounds bounds,qa_error *error)
{
    if(!qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial world bounds");
    uint32_t next=1; build_sector(world,0,bounds,0,&next); return true;
}

qa_spatial_member *qa_spatial_prepare(qa_world *world,const qa_linked_body *body,const qa_actor_collision *collision,qa_error *error)
{
    qa_spatial_member *member=world->spare_members;
    if(member!=NULL) world->spare_members=member->retired_next;
    else member=malloc(sizeof(*member));
    if(member==NULL) { fail(error,QA_ERROR_MEMORY,"Cannot allocate spatial link"); return NULL; }
    *member=(qa_spatial_member){.actor={*body,*collision}}; return member;
}

void qa_spatial_remove(qa_world *world,qa_world_body *body)
{
    qa_spatial_member *member=body->member;
    if(member==NULL) return;
    qa_spatial_sector *sector=&world->sectors[member->sector];
    if(member->previous==NULL) sector->head=member->next;
    else member->previous->next=member->next;
    if(member->next==NULL) sector->tail=member->previous;
    else member->next->previous=member->previous;
    body->member=NULL;
    /* A live visitor may already have captured this generation's next link.
     * Keep its pointers intact until the outermost visit returns. */
    if(world->visit_depth!=0) { member->retired_next=world->retired; world->retired=member; }
    else { member->retired_next=world->spare_members; world->spare_members=member; }
}

void qa_spatial_publish(qa_world *world,qa_world_body *body,qa_spatial_member *member)
{
    qa_spatial_remove(world,body);
    uint32_t index=0;
    qa_bounds bounds=member->actor.body.absolute_bounds;
    while(world->sectors[index].axis>=0) {
        qa_spatial_sector *sector=&world->sectors[index]; unsigned axis=(unsigned)sector->axis;
        if(qa_vec_component(bounds.mins,axis)>sector->distance) index=sector->front;
        else if(qa_vec_component(bounds.maxs,axis)<sector->distance) index=sector->back;
        else break;
    }
    qa_spatial_sector *sector=&world->sectors[index]; member->sector=index;
    if(member->actor.collision.family==QA_COLLISION_Q3) {
        member->next=sector->head;
        if(sector->head!=NULL) sector->head->previous=member; else sector->tail=member;
        sector->head=member;
    } else {
        member->previous=sector->tail;
        if(sector->tail!=NULL) sector->tail->next=member; else sector->head=member;
        sector->tail=member;
    }
    body->member=member;
}

static void recycle_retired(qa_world *world)
{
    while(world->retired!=NULL) {
        qa_spatial_member *member=world->retired;
        world->retired=member->retired_next;
        member->retired_next=world->spare_members; world->spare_members=member;
    }
}

void qa_spatial_dispose(qa_world *world)
{
    for(uint32_t index=0;index<QA_SPATIAL_SECTORS;++index) {
        qa_spatial_member *member=world->sectors[index].head;
        while(member!=NULL) { qa_spatial_member *next=member->next; free(member); member=next; }
        world->sectors[index].head=NULL; world->sectors[index].tail=NULL;
    }
    recycle_retired(world);
    while(world->spare_members!=NULL) {
        qa_spatial_member *member=world->spare_members;
        world->spare_members=member->retired_next; free(member);
    }
}

static bool visit_sector(qa_world *world,uint32_t index,qa_bounds bounds,qa_spatial_raw_fn visit,void *context)
{
    qa_spatial_sector *sector=&world->sectors[index];
    for(qa_spatial_member *member=sector->head;member!=NULL;) {
        qa_spatial_member *next=member->next;
        if(qa_bounds_overlap(member->actor.body.absolute_bounds,bounds)) {
            qa_spatial_visit result=visit(context,&member->actor);
            if(result==QA_SPATIAL_STOP) return false;
            if(result==QA_SPATIAL_STOP_SECTOR) return true;
        }
        member=next;
    }
    if(sector->axis>=0) {
        unsigned axis=(unsigned)sector->axis;
        if(qa_vec_component(bounds.maxs,axis)>sector->distance && !visit_sector(world,sector->front,bounds,visit,context)) return false;
        if(qa_vec_component(bounds.mins,axis)<sector->distance && !visit_sector(world,sector->back,bounds,visit,context)) return false;
    }
    return true;
}

bool qa_spatial_visit_raw(qa_world *world,qa_bounds bounds,qa_spatial_raw_fn visit,void *context,qa_error *error)
{
    if(world==NULL || visit==NULL || !qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial visit");
    if(world->visit_depth==UINT32_MAX) return fail(error,QA_ERROR_ARGUMENT,"Spatial visit nesting exhausted");
    ++world->visit_depth; (void)visit_sector(world,0,bounds,visit,context); --world->visit_depth;
    if(world->visit_depth==0) recycle_retired(world);
    return true;
}

bool qa_world_refresh(qa_world *world,const qa_spatial_actor *linked,qa_spatial_actor *out,qa_error *error)
{
    qa_actor_collision collision;
    if(!qa_world_get_collision(world,linked->body.actor,&collision,error)) return false;
    qa_body_state state;
    if(!qa_world_body_read(world,linked->body.actor,&state,error)) {
        if(error!=NULL && error->code==QA_OK) qa_error_set(error,QA_ERROR_FORMAT,0,"Body state callback failed");
        return false;
    }
    *out=*linked; out->body.state=state; out->collision=collision; return true;
}

typedef struct world_visit_context {
    qa_world *world;
    qa_collision_role role;
    qa_spatial_visit_fn visit;
    void *context;
    qa_error error;
    bool failed;
} world_visit_context;

static qa_spatial_visit visit_current(void *opaque,const qa_spatial_actor *linked)
{
    world_visit_context *context=opaque;
    qa_spatial_actor actor; qa_error error={0};
    if(!qa_world_refresh(context->world,linked,&actor,&error)) {
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

typedef struct query_context { qa_actor_id *actors; size_t capacity,count; bool overflow; } query_context;
static qa_spatial_visit collect_actor(void *opaque,const qa_spatial_actor *actor)
{
    query_context *context=opaque;
    if(context->count<context->capacity) context->actors[context->count++]=actor->body.actor;
    else context->overflow=true;
    return QA_SPATIAL_CONTINUE;
}

bool qa_world_query(qa_world *world,qa_bounds bounds,qa_collision_role role,qa_actor_id *actors,size_t capacity,size_t *count,bool *overflow,qa_error *error)
{
    if(count==NULL || overflow==NULL || (capacity!=0 && actors==NULL)) return fail(error,QA_ERROR_ARGUMENT,"Invalid spatial query output");
    query_context context={actors,capacity,0,false};
    if(!qa_world_visit(world,bounds,role,collect_actor,&context,error)) return false;
    *count=context.count; *overflow=context.overflow; return true;
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

static qa_spatial_visit touch_live(void *opaque,const qa_spatial_actor *candidate)
{
    trigger_context *context=opaque;
    if(qa_actors_get(context->world->actors,context->actor)==NULL) return QA_SPATIAL_STOP;
    touch_candidate(context,candidate->body.actor);
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
    size_t capacity=world->capacity;
    if(capacity>SIZE_MAX/sizeof(qa_actor_id)) return fail(error,QA_ERROR_MEMORY,"Trigger snapshot too large");
    qa_actor_id *candidates=malloc(capacity*sizeof(*candidates));
    if(candidates==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot allocate trigger snapshot");
    size_t count=0; bool overflow=false;
    bool ok=qa_world_query(world,moving.absolute_bounds,QA_COLLISION_TRIGGER,candidates,capacity,&count,&overflow,error);
    if(ok) for(size_t i=0;i<count && !context.failed && qa_actors_get(world->actors,actor)!=NULL;++i) touch_candidate(&context,candidates[i]);
    if(context.failed) { if(error!=NULL) *error=context.error; ok=false; }
    free(candidates); return ok;
}
