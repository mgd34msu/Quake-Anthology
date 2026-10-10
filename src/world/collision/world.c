#include "world_internal.h"

#include <stdlib.h>

qa_collision_trace_rules qa_collision_rules(const qa_trace_policy *policy)
{
    if (policy != NULL && policy->family == QA_COLLISION_Q3)
        return (qa_collision_trace_rules){{0.125f, true, false, true, true},
            {0.125f, 1, false}, true};
    bool rerelease = policy != NULL && policy->family == QA_COLLISION_Q2 && policy->q2_merged_contents;
    return (qa_collision_trace_rules){{0.03125f, rerelease, rerelease, rerelease,
        policy != NULL && policy->family != QA_COLLISION_Q1}, {0.03125f, 0, true}, false};
}

bool qa_collision_trace_brush(void *context, qa_collision_side_distances_fn distances,
    size_t first_side, size_t side_count, bool stationary,
    const qa_collision_brush_rules *rules, qa_collision_bits contents,
    qa_trace_result *result, qa_collision_brush_contact *contact)
{
    float enter = -1, second_enter = -1, leave = 1;
    bool start_out = false, get_out = false;
    size_t lead = SIZE_MAX, second = SIZE_MAX;
    for (size_t i = 0; i < side_count; ++i) {
        size_t side = first_side + i;
        qa_collision_side_distances sample = distances(context, side, !stationary);
        float first = sample.first;
        if (stationary) {
            if (first > 0) return false;
            continue;
        }
        float last = sample.last;
        if (first > 0) start_out = true;
        if (last > 0) get_out = true;
        if (first > 0 && (last >= first || (rules->clamp_fractions && last >= rules->epsilon))) return false;
        if (first <= 0 && last <= 0) continue;
        if (first > last) {
            float fraction = (first - rules->epsilon) / (first - last);
            if (rules->clamp_fractions) fraction = fmaxf(0, fraction);
            if (fraction > enter) {
                enter = fraction;
                lead = side;
            } else if (rules->secondary_plane && fraction > second_enter) {
                second_enter = fraction;
                second = side;
            }
        } else {
            float fraction = (first + rules->epsilon) / (first - last);
            if (rules->clamp_fractions) fraction = fminf(1, fraction);
            leave = fminf(leave, fraction);
        }
    }
    if (!start_out) {
        result->start_solid = true;
        if (!rules->zero_stationary) result->contents = contents;
        if (!get_out) {
            result->all_solid = true;
            if ((stationary && rules->zero_stationary) || rules->zero_all_solid) {
                result->fraction = 0;
                result->contents = contents;
            }
        }
        return false;
    }
    if (enter < leave && enter > -1 && enter < result->fraction && lead != SIZE_MAX) {
        result->fraction = enter < 0 ? 0 : enter;
        result->contents = contents;
        *contact = (qa_collision_brush_contact){lead, second};
        return true;
    }
    return false;
}

void qa_collision_trace_tree(const qa_collision_tree_trace *trace, int32_t headnode)
{
    size_t depth = 0;
    qa_collision_trace_frame frame = {headnode, 0, 1, trace->start, trace->end};
    for (;;) {
        if (trace->result->fraction <= frame.first) goto next_frame;
        if (frame.child < 0) {
            trace->leaf(trace->context, (uint32_t)(-(int64_t)frame.child - 1));
            goto next_frame;
        }
        const qa_collision_node *node = &trace->nodes[(size_t)frame.child];
        const qa_collision_plane *plane = &trace->planes[node->plane];
        float first = qa_collision_plane_distance(frame.start, plane);
        float last = qa_collision_plane_distance(frame.end, plane);
        float offset = trace->extent(trace->context, node->plane);
        if (first >= offset + trace->rules.margin && last >= offset + trace->rules.margin) {
            frame.child = node->children[0];
            continue;
        }
        if (first < -offset - trace->rules.margin && last < -offset - trace->rules.margin) {
            frame.child = node->children[1];
            continue;
        }
        unsigned side = 0;
        float near_fraction = 1, far_fraction = 0;
        if (first != last) {
            float near_distance, far_distance;
            if (first < last) {
                side = 1;
                far_distance = first + offset + trace->rules.epsilon;
                near_distance = first - offset + trace->rules.epsilon;
            } else {
                far_distance = first - offset - trace->rules.epsilon;
                near_distance = first + offset + trace->rules.epsilon;
            }
            if (trace->rules.reciprocal) {
                float inverse = 1 / (first - last);
                far_fraction = far_distance * inverse;
                near_fraction = near_distance * inverse;
            } else {
                far_fraction = far_distance / (first - last);
                near_fraction = near_distance / (first - last);
            }
        }
        near_fraction = qa_collision_clamp_fraction(near_fraction);
        far_fraction = qa_collision_clamp_fraction(far_fraction);
        float span = frame.last - frame.first;
        trace->stack[depth++] = (qa_collision_trace_frame){node->children[side ^ 1u],
            frame.first + span * far_fraction, frame.last,
            qa_vec_lerp(frame.start, frame.end, far_fraction), frame.end};
        frame = (qa_collision_trace_frame){node->children[side], frame.first,
            frame.first + span * near_fraction, frame.start,
            qa_vec_lerp(frame.start, frame.end, near_fraction)};
        continue;
next_frame:
        if (depth == 0) break;
        frame = trace->stack[--depth];
    }
}

static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }

qa_collision_bits qa_world_actor_contents(const qa_actor_collision *collision)
{
    if(collision->family==QA_COLLISION_Q1 && !collision->inline_model)
        return qa_collision_bit(collision->dead_monster?QA_CONTENT_CORPSE:QA_CONTENT_MONSTER);
    return collision->contents;
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

void qa_world_set_query_rules(qa_world *world,const qa_world_query_rules *rules)
{ world->query_rules=rules!=NULL?*rules:(qa_world_query_rules){0}; }

static bool query_candidates(qa_world *world,qa_bounds bounds,
    qa_world_actor_snapshot *snapshot,const qa_actor_id **actors,size_t *count,qa_error *error)
{
    if(world->query_rules.actors!=NULL) {
        *snapshot=(qa_world_actor_snapshot){0};
        *actors=world->query_rules.actors;
        *count=world->query_rules.count;
        return true;
    }
    if(!qa_world_snapshot_capture(world,bounds,QA_COLLISION_BOTH,snapshot,error)) return false;
    *actors=snapshot->actors; *count=snapshot->count; return true;
}

bool qa_world_trace_excluding(qa_world *world,const qa_trace_query *query,const qa_actor_id *excluded,
                              size_t exclude_count,qa_trace_result *out,qa_error *error)
{
    if(world==NULL || query==NULL || out==NULL || (exclude_count!=0 && excluded==NULL))
        return fail(error,QA_ERROR_ARGUMENT,"Invalid shared world trace");
    qa_trace_result result;
    if(!qa_collision_trace(world->geometry,world->trace_scratch,query,&result,error)) return false;
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
    const qa_actor_id *actors; size_t count;
    qa_bounds query_bounds=swept_bounds(&broad);
    if(!query_candidates(world,query_bounds,&candidates,&actors,&count,error)) return false;
    qa_bounds body_query_bounds=query_bounds;
    if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE)
        body_query_bounds=qa_bounds_union(body_query_bounds,swept_bounds(query));
    const bool occupancy_replaces=query->policy.family!=QA_COLLISION_Q3;
    bool ok=true;
    for(size_t i=0;i<count;++i) {
        qa_actor_id id=actors[i];
        qa_world_body *body=qa_world_find_body(world,id);
        if(body==NULL || !body->linked) continue;
        qa_actor_collision collision;
        if(!qa_world_collision_sample(body,false,QA_ENTITY_COLLISION_ALL,&collision,&local)) {
            if(local.code!=QA_OK) { if(error!=NULL) *error=local; ok=false; break; }
            continue;
        }
        if(collision.role!=QA_COLLISION_SOLID && collision.role!=QA_COLLISION_BOTH) continue;
        bool skip=false;
        for(size_t j=0;j<exclude_count;++j) if(qa_actor_id_equal(excluded[j],id)) { skip=true; break; }
        if(skip || skip_owner(world,query,&collision,id,pass)) continue;
        if(query->policy.family==QA_COLLISION_Q1 && collision.q1_corpse && has_volume(query->shape)) continue;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_NO_MONSTERS && !collision.inline_model) continue;
        qa_collision_bits contents=qa_world_actor_contents(&collision);
        if(!qa_collision_bits_overlap(contents,query->policy.contents_mask)) continue;
        qa_body_state state;
        if(!qa_world_body_sample(body,QA_ENTITY_CLIP_POSE,QA_ENTITY_BODY_SPATIAL,&state,error)) {
            ok=false; break;
        }
        if(pass_has_width && state.bounds.maxs.x==state.bounds.mins.x) continue;
        if(!collision.inline_model && collision.shape==QA_SHAPE_BOX &&
            query->shape.kind!=QA_SHAPE_CAPSULE && qa_bounds_valid(state.bounds) &&
            !qa_bounds_overlap(body_query_bounds,qa_bounds_translate(state.bounds,state.origin))) continue;
        qa_trace_query moving=*query;
        if(query->policy.family==QA_COLLISION_Q1 && query->policy.q1_move==QA_Q1_MOVE_MISSILE && collision.monster) moving.shape=missile;
        qa_trace_result hit;
        if(collision.inline_model) {
            moving.target=(qa_collision_target){true,collision.model,state.origin,state.angles};
            ok=qa_collision_trace(qa_world_model_geometry(world,&collision),
                qa_world_trace_scratch(world,qa_world_model_geometry(world,&collision)),&moving,&hit,error);
        } else ok=qa_collision_trace_body(&moving,collision.family,collision.shape,state.bounds,state.origin,contents,&hit,error);
        if(!ok) break;
        if(hit.hit!=QA_TRACE_HIT_NONE) { hit.hit=QA_TRACE_HIT_ACTOR; hit.actor=id; }
        if(!occupancy_replaces) {
            if(hit.fraction<result.fraction) { hit.start_solid=hit.start_solid||result.start_solid; result=hit; }
            else { result.all_solid=result.all_solid||hit.all_solid; result.start_solid=result.start_solid||(!hit.all_solid&&hit.start_solid); }
        } else if(hit.all_solid || hit.fraction<result.fraction || hit.start_solid) {
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
    if(!qa_collision_point_contents(world->geometry,world->trace_scratch,query,&result,error)) return false;
    if(query->target.inline_model || query->policy.family==QA_COLLISION_Q1) { *out=result; return true; }
    qa_world_actor_snapshot candidates;
    const qa_actor_id *actors; size_t count;
    if(!query_candidates(world,(qa_bounds){query->point,query->point},&candidates,&actors,&count,error)) return false;
    bool ok=true;
    for(size_t i=0;i<count;++i) {
        qa_actor_id id=actors[i];
        qa_world_body *body=qa_world_find_body(world,id);
        if(body==NULL || !body->linked) continue;
        if(!world->query_rules.contents_ignore_pass && query->pass_actor.registry!=0 && qa_actor_id_equal(query->pass_actor,id)) continue;
        qa_actor_collision collision; qa_error refresh_error={0};
        if(!qa_world_collision_sample(body,false,QA_ENTITY_COLLISION_ALL,&collision,&refresh_error)) {
            if(refresh_error.code!=QA_OK) { if(error!=NULL) *error=refresh_error; ok=false; break; }
            continue;
        }
        if(!query->q3_server_entities && collision.role!=QA_COLLISION_SOLID
            && collision.role!=QA_COLLISION_BOTH) continue;
        if(world->query_rules.brush_contents_only && !collision.inline_model) continue;
        qa_body_state state;
        if(!qa_world_body_sample(body,QA_ENTITY_CONTENTS_POSE,QA_ENTITY_BODY_SPATIAL,&state,&refresh_error)) {
            if(refresh_error.code!=QA_OK) { if(error!=NULL) *error=refresh_error; ok=false; break; }
            continue;
        }
        qa_collision_bits added;
        if(collision.inline_model) {
            qa_point_query local=*query;
            local.target=(qa_collision_target){true,collision.model,state.origin,state.angles};
            qa_point_contents sample;
            if(!qa_collision_point_contents(qa_world_model_geometry(world,&collision),
                qa_world_trace_scratch(world,qa_world_model_geometry(world,&collision)),&local,&sample,error)) { ok=false; break; }
            if(result.family==QA_COLLISION_Q2 && world->query_rules.brush_contents_only) {
                result.stored=qa_collision_bits_union(result.stored,sample.stored);
                result.merged=qa_collision_bits_union(result.merged,sample.merged);
                result.contents=query->policy.q2_merged_contents?result.merged:result.stored;
                continue;
            }
            added=sample.family==QA_COLLISION_Q2?(query->policy.q2_merged_contents?sample.merged:sample.stored):sample.contents;
        } else {
            qa_vec3 point=qa_vec_sub(query->point,state.origin);
            bool source_temporary=query->q3_server_entities && collision.family==QA_COLLISION_Q3;
            if(source_temporary && collision.shape==QA_SHAPE_CAPSULE) {
                qa_vec3 basis[3];
                qa_collision_basis(state.angles,basis);
                point=qa_collision_to_local(point,basis);
            }
            if(!qa_bounds_contains(state.bounds,point)) continue;
            added=source_temporary?qa_collision_bit(QA_CONTENT_BODY):qa_world_actor_contents(&collision);
        }
        if(result.family==QA_COLLISION_Q2) { result.stored=qa_collision_bits_union(result.stored,added); result.merged=qa_collision_bits_union(result.merged,added); result.contents=query->policy.q2_merged_contents?result.merged:result.stored; }
        else result.contents=qa_collision_bits_union(result.contents,added);
    }
    qa_world_snapshot_release(&candidates);
    if(ok) *out=result;
    return ok;
}
