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

static bool owner_matches_actor(const qa_actor_registry *actors,qa_actor_reference owner,qa_actor_id actor)
{
    if(owner.kind==QA_ACTOR_REFERENCE_LIFETIME) return qa_actor_id_equal(owner.value.actor,actor);
    if(owner.kind!=QA_ACTOR_REFERENCE_SOURCE) return false;
    const qa_actor_record *record=qa_actors_get(actors,actor);
    return record!=NULL && record->has_source && owner.value.source.owner==record->owner &&
        owner.value.source.slot==record->source_slot;
}

static bool owners_match(const qa_actor_registry *actors,qa_actor_reference left,qa_actor_reference right)
{
    if(!qa_actor_reference_present(left) || !qa_actor_reference_present(right)) return false;
    if(qa_actor_reference_equal(left,right)) return true;
    if(left.kind==QA_ACTOR_REFERENCE_LIFETIME)
        return owner_matches_actor(actors,right,left.value.actor);
    if(right.kind==QA_ACTOR_REFERENCE_LIFETIME)
        return owner_matches_actor(actors,left,right.value.actor);
    return false;
}

static bool skip_owner(const qa_world *world,const qa_trace_query *query,const qa_actor_collision *candidate,
                       qa_actor_id id,const qa_actor_collision *pass)
{
    qa_actor_id actor=query->pass_actor;
    if(actor.registry==0) return false;
    if(qa_actor_id_equal(actor,id)) return true;
    if(query->policy.family==QA_COLLISION_Q3 && pass!=NULL && pass->has_q3_owner && candidate->has_q3_owner) {
        const qa_actor_record *pass_record=qa_actors_get(world->actors,actor);
        const qa_actor_record *candidate_record=qa_actors_get(world->actors,id);
        if(pass_record!=NULL && candidate_record!=NULL && pass_record->owner==candidate_record->owner) {
            int32_t owner=pass->q3_owner_number==1023?-1:pass->q3_owner_number;
            return candidate->q3_owner_number==pass->q3_entity_number || candidate->q3_owner_number==owner;
        }
    }
    if(owner_matches_actor(world->actors,candidate->owner,actor)) return true;
    if(pass==NULL || !qa_actor_reference_present(pass->owner)) return false;
    return query->policy.family==QA_COLLISION_Q3?
        owners_match(world->actors,pass->owner,candidate->owner):owner_matches_actor(world->actors,pass->owner,id);
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
    qa_error local={0};
    const qa_actor_collision *pass=qa_world_get_collision(world,query->pass_actor,&pass_collision,&local)?&pass_collision:NULL;
    if(local.code!=QA_OK) { if(error!=NULL) *error=local; return false; }
    bool pass_has_width=false;
    if(query->policy.family==QA_COLLISION_Q1 && pass!=NULL) {
        qa_body_state body;
        if(!qa_world_body_read(world,query->pass_actor,&body,error)) return false;
        pass_has_width=body.bounds.maxs.x!=body.bounds.mins.x;
    }
    qa_trace_query broad=*query;
    const qa_trace_shape missile={QA_SHAPE_BOX,{{-15,-15,-15},{15,15,15}}};
    if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE) broad.shape=missile;
    qa_world_actor_snapshot candidates;
    if(!qa_world_snapshot_capture(world,swept_bounds(&broad),QA_COLLISION_BOTH,&candidates,error)) return false;
    bool ok=true;
    for(size_t i=0;i<candidates.count;++i) {
        qa_actor_id id=candidates.actors[i];
        qa_actor_collision collision;
        if(!qa_world_get_collision(world,id,&collision,&local)) {
            if(local.code!=QA_OK) { if(error!=NULL) *error=local; ok=false; break; }
            continue;
        }
        if(collision.role!=QA_COLLISION_SOLID && collision.role!=QA_COLLISION_BOTH) continue;
        bool skip=false;
        for(size_t j=0;j<exclude_count;++j) if(qa_actor_id_equal(excluded[j],id)) { skip=true; break; }
        if(skip || skip_owner(world,query,&collision,id,pass)) continue;
        if(collision.q1_corpse && has_volume(query->shape)) continue;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_NO_MONSTERS && !collision.inline_model) continue;
        int32_t contents=qa_world_actor_contents(&collision,query->policy.family);
        if(query->policy.family==QA_COLLISION_Q1?contents!=-2:((uint32_t)contents&query->policy.contents_mask)==0) continue;
        qa_body_state state;
        if(!qa_world_body_read(world,id,&state,error)) {
            ok=false; break;
        }
        if(pass_has_width && state.bounds.maxs.x==state.bounds.mins.x) continue;
        qa_trace_query moving=*query;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE && collision.monster) moving.shape=missile;
        qa_trace_result hit;
        if(collision.inline_model) {
            moving.target=(qa_collision_target){true,collision.model,state.origin,state.angles};
            ok=qa_collision_trace(qa_world_model_geometry(world,&collision),&moving,&hit,error);
        } else ok=qa_collision_trace_body(&moving,collision.family,collision.shape,state.bounds,state.origin,contents,&hit,error);
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
    qa_world_snapshot_release(&candidates);
    if(ok) *out=result;
    return ok;
}

bool qa_world_trace(qa_world *world,const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{ return qa_world_trace_excluding(world,query,NULL,0,out,error); }

bool qa_world_point_contents(qa_world *world,const qa_point_query *query,qa_point_contents *out,qa_error *error)
{
    if(world==NULL || query==NULL || out==NULL
        || (query->q3_server_entities && query->policy.family!=QA_COLLISION_Q3))
        return fail(error,QA_ERROR_ARGUMENT,"Invalid shared contents query");
    qa_point_contents result;
    if(!qa_collision_point_contents(world->geometry,query,&result,error)) return false;
    if(query->target.inline_model || query->policy.family==QA_COLLISION_Q1) { *out=result; return true; }
    qa_world_actor_snapshot candidates;
    if(!qa_world_snapshot_capture(world,(qa_bounds){query->point,query->point},QA_COLLISION_BOTH,&candidates,error)) return false;
    bool ok=true;
    qa_spatial_actor linked={0};
    for(size_t i=0;i<candidates.count;++i) {
        linked.body.actor=candidates.actors[i];
        if(query->pass_actor.registry!=0 && qa_actor_id_equal(query->pass_actor,linked.body.actor)) continue;
        qa_spatial_actor actor; qa_error refresh_error={0};
        if(!qa_world_refresh(world,&linked,&actor,&refresh_error)) {
            if(refresh_error.code!=QA_OK) { if(error!=NULL) *error=refresh_error; ok=false; break; }
            continue;
        }
        if(!query->q3_server_entities && actor.collision.role!=QA_COLLISION_SOLID
            && actor.collision.role!=QA_COLLISION_BOTH) continue;
        int32_t added;
        if(actor.collision.inline_model) {
            qa_point_query local=*query;
            local.target=(qa_collision_target){true,actor.collision.model,actor.body.state.origin,actor.body.state.angles};
            qa_point_contents sample;
            if(!qa_collision_point_contents(qa_world_model_geometry(world,&actor.collision),&local,&sample,error)) { ok=false; break; }
            added=sample.family==QA_COLLISION_Q2?(query->policy.q2_merged_contents?sample.merged:sample.stored):sample.contents;
        } else {
            qa_vec3 point=qa_vec_sub(query->point,actor.body.state.origin);
            bool source_temporary=query->q3_server_entities && actor.collision.family==QA_COLLISION_Q3;
            if(source_temporary && actor.collision.shape==QA_SHAPE_CAPSULE) {
                qa_vec3 basis[3];
                qa_collision_basis(actor.body.state.angles,basis);
                point=qa_collision_to_local(point,basis);
            }
            if(!qa_bounds_contains(actor.body.state.bounds,point)) continue;
            added=source_temporary?0x02000000:qa_world_actor_contents(&actor.collision,query->policy.family);
        }
        if(result.family==QA_COLLISION_Q2) { result.stored|=added; result.merged|=added; result.contents=query->policy.q2_merged_contents?result.merged:result.stored; }
        else result.contents|=added;
    }
    qa_world_snapshot_release(&candidates);
    if(ok) *out=result;
    return ok;
}
