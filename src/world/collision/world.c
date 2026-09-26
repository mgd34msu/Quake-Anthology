#include "world_internal.h"

#include <stdlib.h>

static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }

int32_t qa_world_actor_contents(const qa_actor_collision *collision,qa_collision_family to)
{
    if(collision->family==QA_COLLISION_Q1 && !collision->inline_model && to!=QA_COLLISION_Q1)
        return collision->dead_monster?0x04000000:0x02000000;
    return qa_collision_convert_contents(collision->contents,collision->family,to);
}

typedef struct actor_snapshot { qa_spatial_actor *actors; size_t count,capacity; bool counting; } actor_snapshot;
static qa_spatial_visit snapshot_actor(void *opaque,const qa_spatial_actor *actor)
{
    actor_snapshot *snapshot=opaque;
    if(snapshot->counting) ++snapshot->count;
    else if(snapshot->count<snapshot->capacity) snapshot->actors[snapshot->count++]=*actor;
    return QA_SPATIAL_CONTINUE;
}

static bool snapshot(qa_world *world,qa_bounds bounds,actor_snapshot *out,qa_error *error)
{
    actor_snapshot result={.counting=true};
    if(!qa_spatial_visit_raw(world,bounds,snapshot_actor,&result,error)) return false;
    if(result.count>SIZE_MAX/sizeof(*result.actors)) return fail(error,QA_ERROR_MEMORY,"Spatial snapshot is too large");
    if(result.count!=0) {
        result.actors=malloc(result.count*sizeof(*result.actors));
        if(result.actors==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot allocate spatial snapshot");
    }
    result.capacity=result.count; result.count=0; result.counting=false;
    if(!qa_spatial_visit_raw(world,bounds,snapshot_actor,&result,error)) { free(result.actors); return false; }
    *out=result; return true;
}

static qa_bounds swept_bounds(const qa_trace_query *query)
{
    qa_bounds box=query->shape.kind==QA_SHAPE_POINT?(qa_bounds){0}:query->shape.bounds;
    qa_bounds line=qa_bounds_union((qa_bounds){query->start,query->start},(qa_bounds){query->end,query->end});
    return (qa_bounds){qa_vec_sub(qa_vec_add(line.mins,box.mins),qa_v3(1,1,1)),
        qa_vec_add(qa_vec_add(line.maxs,box.maxs),qa_v3(1,1,1))};
}

static bool has_volume(qa_trace_shape shape)
{
    return shape.kind!=QA_SHAPE_POINT && (shape.bounds.mins.x!=shape.bounds.maxs.x
        || shape.bounds.mins.y!=shape.bounds.maxs.y || shape.bounds.mins.z!=shape.bounds.maxs.z);
}

static bool skip_owner(const qa_trace_query *query,const qa_actor_collision *candidate,
                       qa_actor_id id,const qa_actor_collision *pass)
{
    qa_actor_id actor=query->pass_actor;
    if(actor.registry==0) return false;
    if(qa_actor_id_equal(actor,id)) return true;
    if(query->policy.family==QA_COLLISION_Q3 && pass!=NULL && pass->has_q3_owner && candidate->has_q3_owner) {
        int32_t owner=pass->q3_owner_number==1023?-1:pass->q3_owner_number;
        return candidate->q3_owner_number==pass->q3_entity_number || candidate->q3_owner_number==owner;
    }
    if(candidate->owner.registry!=0 && qa_actor_id_equal(actor,candidate->owner)) return true;
    if(pass==NULL || pass->owner.registry==0) return false;
    return query->policy.family==QA_COLLISION_Q3?
        candidate->owner.registry!=0 && qa_actor_id_equal(pass->owner,candidate->owner):qa_actor_id_equal(pass->owner,id);
}

bool qa_world_trace_excluding(qa_world *world,const qa_trace_query *query,const qa_actor_id *excluded,
                              size_t exclude_count,qa_trace_result *out,qa_error *error)
{
    if(world==NULL || query==NULL || out==NULL || (exclude_count!=0 && excluded==NULL))
        return fail(error,QA_ERROR_ARGUMENT,"Invalid shared world trace");
    qa_trace_result result;
    if(!qa_collision_trace(world->geometry,query,&result,error)) return false;
    if(query->target.inline_model || result.all_solid || (query->policy.family==QA_COLLISION_Q3 && result.fraction==0.0f)) { *out=result; return true; }
    qa_actor_collision pass_collision;
    const qa_actor_collision *pass=qa_world_get_collision(world,query->pass_actor,&pass_collision)?&pass_collision:NULL;
    qa_trace_query broad=*query;
    const qa_trace_shape missile={QA_SHAPE_BOX,{{-15,-15,-15},{15,15,15}}};
    if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE) broad.shape=missile;
    actor_snapshot candidates;
    if(!snapshot(world,swept_bounds(&broad),&candidates,error)) return false;
    bool ok=true;
    for(size_t i=0;i<candidates.count;++i) {
        const qa_spatial_actor *linked=&candidates.actors[i]; qa_actor_id id=linked->body.actor;
        qa_actor_collision collision;
        if(!qa_world_get_collision(world,id,&collision) || collision.role!=QA_COLLISION_SOLID) continue;
        bool skip=false;
        for(size_t j=0;j<exclude_count;++j) if(qa_actor_id_equal(excluded[j],id)) { skip=true; break; }
        if(skip || skip_owner(query,&collision,id,pass)) continue;
        if(collision.q1_corpse && has_volume(query->shape)) continue;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_NO_MONSTERS && !collision.inline_model) continue;
        int32_t contents=qa_world_actor_contents(&collision,query->policy.family);
        if(query->policy.family==QA_COLLISION_Q1?contents!=-2:((uint32_t)contents&query->policy.contents_mask)==0) continue;
        qa_spatial_actor actor; qa_error refresh_error={0};
        if(!qa_world_refresh(world,linked,&actor,&refresh_error)) {
            if(refresh_error.code!=QA_OK) { if(error!=NULL) *error=refresh_error; ok=false; break; }
            continue;
        }
        qa_trace_query moving=*query;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE && collision.monster) moving.shape=missile;
        qa_trace_result hit;
        if(collision.inline_model) {
            moving.target=(qa_collision_target){true,collision.model,actor.body.state.origin,actor.body.state.angles};
            ok=qa_collision_trace(world->geometry,&moving,&hit,error);
        } else ok=qa_collision_trace_body(&moving,collision.family,collision.shape,actor.body.state.bounds,actor.body.state.origin,contents,&hit,error);
        if(!ok) break;
        if(hit.hit!=QA_TRACE_HIT_NONE) { hit.hit=QA_TRACE_HIT_ACTOR; hit.actor=id; }
        if(query->policy.family==QA_COLLISION_Q3) {
            if(hit.fraction<result.fraction) { hit.start_solid=hit.start_solid||result.start_solid; result=hit; }
            else { result.all_solid=result.all_solid||hit.all_solid; result.start_solid=result.start_solid||(!hit.all_solid&&hit.start_solid); }
        } else if(hit.all_solid || hit.fraction<result.fraction || (query->policy.family==QA_COLLISION_Q1 && hit.start_solid)) {
            hit.start_solid=hit.start_solid||result.start_solid; result=hit;
        } else if(hit.start_solid) result.start_solid=true;
        if(result.all_solid) break;
    }
    free(candidates.actors);
    if(ok) *out=result;
    return ok;
}

bool qa_world_trace(qa_world *world,const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{ return qa_world_trace_excluding(world,query,NULL,0,out,error); }

bool qa_world_point_contents(qa_world *world,const qa_point_query *query,qa_point_contents *out,qa_error *error)
{
    if(world==NULL || query==NULL || out==NULL) return fail(error,QA_ERROR_ARGUMENT,"Invalid shared contents query");
    qa_point_contents result;
    if(!qa_collision_point_contents(world->geometry,query,&result,error)) return false;
    if(query->target.inline_model || query->policy.family==QA_COLLISION_Q1) { *out=result; return true; }
    actor_snapshot candidates;
    if(!snapshot(world,(qa_bounds){query->point,query->point},&candidates,error)) return false;
    bool ok=true;
    for(size_t i=0;i<candidates.count;++i) {
        const qa_spatial_actor *linked=&candidates.actors[i];
        if(query->pass_actor.registry!=0 && qa_actor_id_equal(query->pass_actor,linked->body.actor)) continue;
        qa_spatial_actor actor; qa_error refresh_error={0};
        if(!qa_world_refresh(world,linked,&actor,&refresh_error)) {
            if(refresh_error.code!=QA_OK) { if(error!=NULL) *error=refresh_error; ok=false; break; }
            continue;
        }
        if(actor.collision.role!=QA_COLLISION_SOLID) continue;
        int32_t added;
        if(actor.collision.inline_model) {
            qa_point_query local=*query;
            local.target=(qa_collision_target){true,actor.collision.model,actor.body.state.origin,actor.body.state.angles};
            qa_point_contents sample;
            if(!qa_collision_point_contents(world->geometry,&local,&sample,error)) { ok=false; break; }
            added=sample.family==QA_COLLISION_Q2?(query->policy.q2_merged_contents?sample.merged:sample.stored):sample.contents;
        } else {
            if(!qa_bounds_contains(actor.body.state.bounds,qa_vec_sub(query->point,actor.body.state.origin))) continue;
            added=qa_world_actor_contents(&actor.collision,query->policy.family);
        }
        if(result.family==QA_COLLISION_Q2) { result.stored|=added; result.merged|=added; result.contents=query->policy.q2_merged_contents?result.merged:result.stored; }
        else result.contents|=added;
    }
    free(candidates.actors);
    if(ok) *out=result;
    return ok;
}
