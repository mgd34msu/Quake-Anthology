#include "collision/world_internal.h"

#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }

qa_world_body *qa_world_find_body(const qa_world *world, qa_actor_id actor)
{
    qa_world_body *body=qa_world_raw_body(world,actor.slot);
    return body!=NULL && body->present && qa_actor_id_equal(body->actor,actor)
        && qa_actors_get(world->actors,actor)!=NULL?body:NULL;
}

void qa_world_reset_bodies(qa_world *world)
{
    for(uint32_t slot=0;slot<world->capacity;++slot) {
        qa_world_body *body=qa_world_raw_body(world,slot);
        if(body!=NULL) {
            qa_spatial_remove(world,slot);
            free(body->leaves);
            memset(body,0,sizeof(*body));
            *qa_actors_link(world->actors->links,slot)=(qa_spatial_link){0};
        }
    }
}

static qa_world_body *ensure_body(qa_world *world, qa_actor_id actor, qa_error *error)
{
    if(world==NULL || qa_actors_get(world->actors,actor)==NULL) {
        fail(error,QA_ERROR_ARGUMENT,"Body actor is not live in this world"); return NULL;
    }
    qa_world_body *body=qa_world_raw_body(world,actor.slot);
    if(body==NULL) return NULL;
    if(body->present && !qa_actor_id_equal(body->actor,actor)) {
        fail(error,QA_ERROR_ARGUMENT,"Session did not forward the previous actor release"); return NULL;
    }
    body->world=world;
    return body;
}

static bool valid_state(const qa_body_state *state,qa_entity_body_components components)
{
    return state!=NULL && qa_vec_finite(state->origin) && qa_vec_finite(state->angles)
        && (components!=QA_ENTITY_BODY_ALL || qa_vec_finite(state->velocity))
        && qa_bounds_valid(state->bounds);
}

static bool valid_link_state(const qa_body_link_state *saved)
{
    return saved!=NULL && (!saved->linked || (saved->link_count!=0
        && valid_state(&saved->state,QA_ENTITY_BODY_ALL) && qa_bounds_valid(saved->absolute_bounds)));
}

static bool same_vector(qa_vec3 left,qa_vec3 right)
{ return left.x==right.x && left.y==right.y && left.z==right.z; }

static bool same_bounds(qa_bounds left,qa_bounds right)
{ return same_vector(left.mins,right.mins) && same_vector(left.maxs,right.maxs); }

static bool same_state(const qa_body_state *left,const qa_body_state *right)
{
    return same_vector(left->origin,right->origin) && same_vector(left->angles,right->angles)
        && same_vector(left->velocity,right->velocity) && same_bounds(left->bounds,right->bounds)
        && qa_actor_reference_equal(left->ground,right->ground);
}

static bool same_collision(const qa_actor_collision *left,const qa_actor_collision *right)
{
    return left->family==right->family && left->shape==right->shape
        && left->inline_model==right->inline_model && left->model==right->model
        && left->model_geometry==right->model_geometry && qa_collision_bits_equal(left->contents,right->contents)
        && left->q1_opaque_token==right->q1_opaque_token
        && qa_actor_reference_equal(left->owner,right->owner) && left->role==right->role
        && left->monster==right->monster && left->dead_monster==right->dead_monster
        && left->q1_corpse==right->q1_corpse && left->has_q3_owner==right->has_q3_owner
        && left->q3_entity_number==right->q3_entity_number && left->q3_owner_number==right->q3_owner_number;
}

bool qa_world_create(qa_actor_registry *actors, qa_collision_geometry *geometry,
                     const qa_world_hooks *hooks, qa_world **out, qa_error *error)
{
    if(actors==NULL || geometry==NULL || out==NULL)
        return fail(error,QA_ERROR_ARGUMENT,"World needs actors, geometry and output");
    qa_bounds bounds;
    if(!qa_collision_model_bounds(geometry,0,&bounds,error)) return false;
    qa_world *world=calloc(1,sizeof(*world));
    if(world==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot allocate shared world");
    world->actors=actors; world->geometry=geometry; world->capacity=qa_actors_capacity(actors);
    if(hooks!=NULL) world->hooks=*hooks;
    if(!qa_trace_scratch_create(geometry,&world->trace_scratch,error)) { free(world); return false; }
    if(!qa_spatial_initialize(world,bounds,error)) {
        qa_trace_scratch_destroy(world->trace_scratch); free(world); return false;
    }
    *out=world; return true;
}

static void dispose_trace_geometries(qa_world *world)
{
    qa_world_trace_geometry *entry=world->trace_geometries;
    while(entry!=NULL) {
        qa_world_trace_geometry *next=entry->next;
        qa_trace_scratch_destroy(entry->scratch);
        qa_collision_destroy(entry->geometry);
        free(entry);
        entry=next;
    }
    world->trace_geometries=NULL;
}

qa_trace_scratch *qa_world_trace_scratch(qa_world *world,const qa_collision_geometry *geometry)
{
    if(world==NULL || geometry==NULL) return NULL;
    if(world->geometry==geometry) return world->trace_scratch;
    for(qa_world_trace_geometry *entry=world->trace_geometries;entry!=NULL;entry=entry->next)
        if(entry->geometry==geometry) return entry->scratch;
    return NULL;
}

bool qa_world_prepare_trace_geometry(qa_world *world,qa_collision_geometry *geometry,qa_error *error)
{
    if(world==NULL || geometry==NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Trace geometry preparation requires a world and geometry");
    if(qa_world_trace_scratch(world,geometry)!=NULL) return true;
    qa_world_trace_geometry *entry=calloc(1,sizeof(*entry));
    if(entry==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot allocate foreign geometry scratch owner");
    if(!qa_trace_scratch_create(geometry,&entry->scratch,error)) { free(entry); return false; }
    if(!qa_collision_retain(geometry,error)) { qa_trace_scratch_destroy(entry->scratch); free(entry); return false; }
    entry->geometry=geometry; entry->next=world->trace_geometries;
    world->trace_geometries=entry;
    return true;
}

bool qa_world_idle(const qa_world *world)
{
    return world!=NULL && world->callback_depth==0 && world->visit_depth==0;
}

bool qa_world_destroy(qa_world *world, qa_error *error)
{
    if(world==NULL) return true;
    if(!qa_world_idle(world))
        return fail(error,QA_ERROR_ARGUMENT,"Cannot destroy world during a callback or spatial visit");
    if(world->geometry_admission!=NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Abort geometry admission before world destruction");
    qa_world_reset_bodies(world);
    qa_spatial_dispose(world);
    dispose_trace_geometries(world);
    qa_trace_scratch_destroy(world->trace_scratch);
    free(world); return true;
}

qa_actor_registry *qa_world_actors(qa_world *world) { return world==NULL?NULL:world->actors; }
qa_collision_geometry *qa_world_geometry(qa_world *world) { return world==NULL?NULL:world->geometry; }

struct qa_world_geometry_admission {
    qa_world *world;
    qa_collision_geometry *geometry;
    qa_trace_scratch *scratch;
    qa_spatial_sector sectors[QA_SPATIAL_SECTORS];
};

bool qa_world_prepare_geometry(qa_world *world,qa_collision_geometry *geometry,
                                qa_world_geometry_admission **out,qa_error *error)
{
    if(world==NULL || geometry==NULL || out==NULL || world->callback_depth!=0
        || world->visit_depth!=0 || world->geometry_admission!=NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Geometry preparation requires an idle world without an admission");
    qa_bounds bounds;
    if(!qa_collision_model_bounds(geometry,0,&bounds,error)) return false;
    qa_world candidate={0};
    if(!qa_spatial_initialize(&candidate,bounds,error)) return false;
    qa_world_geometry_admission *token=malloc(sizeof(*token));
    if(token==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot allocate geometry admission");
    token->world=world; token->geometry=geometry;
    token->scratch=NULL;
    if(!qa_trace_scratch_create(geometry,&token->scratch,error)) { free(token); return false; }
    memcpy(token->sectors,candidate.sectors,sizeof(token->sectors));
    world->geometry_admission=token;
    *out=token;
    return true;
}

bool qa_world_geometry_admission_validate(qa_world_geometry_admission *token,qa_error *error)
{
    if(token==NULL) return fail(error,QA_ERROR_ARGUMENT,"Missing geometry admission");
    qa_world *world=token->world;
    if(world->geometry_admission!=token || world->callback_depth!=0 || world->visit_depth!=0
        || qa_actors_count(world->actors)!=0)
        return fail(error,QA_ERROR_ARGUMENT,"Geometry publication requires an idle empty world");
    for(uint32_t slot=0;slot<world->capacity;++slot) {
        qa_world_body *body=qa_world_raw_body(world,slot);
        if(body!=NULL && (body->present || qa_actors_link(world->actors->links,slot)->linked))
            return fail(error,QA_ERROR_ARGUMENT,"Forward all body releases before geometry publication");
    }
    for(uint32_t i=0;i<QA_SPATIAL_SECTORS;++i)
        if(world->sectors[i].head!=QA_SPATIAL_NONE || world->sectors[i].tail!=QA_SPATIAL_NONE)
            return fail(error,QA_ERROR_ARGUMENT,"Geometry publication found retained spatial links");
    return true;
}

bool qa_world_geometry_admission_commit(qa_world_geometry_admission *token,qa_error *error)
{
    if(!qa_world_geometry_admission_validate(token,error)) return false;
    dispose_trace_geometries(token->world);
    qa_trace_scratch_destroy(token->world->trace_scratch);
    token->world->geometry=token->geometry;
    token->world->trace_scratch=token->scratch;
    token->scratch=NULL;
    memcpy(token->world->sectors,token->sectors,sizeof(token->sectors));
    qa_world_geometry_admission_abort(token);
    return true;
}

void qa_world_geometry_admission_abort(qa_world_geometry_admission *token)
{
    if(token==NULL) return;
    token->world->geometry_admission=NULL;
    qa_trace_scratch_destroy(token->scratch);
    free(token);
}

static void notify_unlink(qa_world *world,qa_actor_id actor)
{
    if(world->hooks.unlinked!=NULL) {
        ++world->callback_depth; world->hooks.unlinked(world->hooks.context,actor); --world->callback_depth;
    }
}

typedef struct attached_actor { qa_actor_id actor; uint64_t order; } attached_actor;
static int attachment_compare(const void *left,const void *right)
{
    uint64_t a=((const attached_actor *)left)->order,b=((const attached_actor *)right)->order;
    return a<b?-1:a>b?1:0;
}

bool qa_world_actor_released(qa_world *world,qa_actor_record released,qa_error *error)
{
    if(world==NULL) return fail(error,QA_ERROR_ARGUMENT,"Missing world for actor release");
    if(qa_actors_get(world->actors,released.id)!=NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Actor release must be forwarded after invalidation");
    size_t child_count=0;
    for(uint32_t slot=0;slot<world->capacity;++slot) {
        qa_world_body *child=qa_world_raw_body(world,slot);
        if(child!=NULL && child->present && child->attached
            && qa_actor_id_equal(child->attachment.anchor,released.id)) ++child_count;
    }
    if(child_count>SIZE_MAX/sizeof(attached_actor)) return fail(error,QA_ERROR_MEMORY,"Attached child snapshot is too large");
    attached_actor *children=child_count==0?NULL:malloc(child_count*sizeof(*children));
    if(child_count!=0 && children==NULL) return fail(error,QA_ERROR_MEMORY,"Cannot snapshot attached children for release");
    size_t written=0;
    for(uint32_t slot=0;slot<world->capacity;++slot) {
        qa_world_body *child=qa_world_raw_body(world,slot);
        if(child==NULL || !child->present || !child->attached
            || !qa_actor_id_equal(child->attachment.anchor,released.id)) continue;
        children[written++]=(attached_actor){child->actor,child->attachment_order};
        child->attached=false;
    }
    if(child_count>1) qsort(children,child_count,sizeof(*children),attachment_compare);
    ++world->callback_depth;
    qa_world_body *body=qa_world_raw_body(world,released.id.slot);
    if(body!=NULL && body->present && qa_actor_id_equal(body->actor,released.id)) {
        bool linked=body->linked;
        qa_spatial_remove(world,body->actor.slot);
        free(body->leaves);
        memset(body,0,sizeof(*body));
        *qa_actors_link(world->actors->links,released.id.slot)=(qa_spatial_link){0};
        if(linked) notify_unlink(world,released.id);
    }
    bool success=true;
    qa_error first={0};
    for(size_t index=0;index<child_count;++index) {
        qa_actor_id id=children[index].actor;
        if(qa_actors_get(world->actors,id)==NULL) continue;
        qa_error current={0};
        if(!qa_actors_release(world->actors,id,&current) && success) { first=current; success=false; }
    }
    --world->callback_depth;
    free(children);
    if(!success && error!=NULL) *error=first;
    return success;
}

bool qa_world_body_create(qa_world *world,qa_actor_id actor,const qa_body_state *state,qa_error *error)
{
    if(!valid_state(state,QA_ENTITY_BODY_ALL)) return fail(error,QA_ERROR_ARGUMENT,"Invalid body state");
    qa_world_body *body=ensure_body(world,actor,error);
    if(body==NULL) return false;
    if(body->present) return fail(error,QA_ERROR_ARGUMENT,"Actor already has a body");
    if(world->body_serial==UINT64_MAX) return fail(error,QA_ERROR_ARGUMENT,"Body storage identity exhausted");
    body->storage_serial=++world->body_serial;
    body->present=true; body->actor=actor; body->state=*state; return true;
}

bool qa_world_body_bind(qa_world *world,qa_actor_id actor,const qa_body_binding *binding,bool replace,qa_error *error)
{
    if(binding==NULL || binding->fields==NULL || binding->write==NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Body binding needs fields and a write callback");
    qa_world_body *body=ensure_body(world,actor,error);
    if(body==NULL) return false;
    if(body->present && !replace) return fail(error,QA_ERROR_ARGUMENT,"Actor already has a body binding");
    if(world->body_serial==UINT64_MAX) return fail(error,QA_ERROR_ARGUMENT,"Body storage identity exhausted");
    body->storage_serial=++world->body_serial;
    body->actor=actor; body->present=true; body->external=true; body->binding=*binding; return true;
}

uint64_t qa_world_body_storage_serial(const qa_world *world,qa_actor_id actor)
{
    const qa_world_body *body=qa_world_find_body(world,actor);
    return body==NULL?0:body->storage_serial;
}

bool qa_world_body_sample(qa_world_body *body,qa_entity_pose pose,
                          qa_entity_body_components components,
                          qa_body_state *out,qa_error *error)
{
    qa_body_state state;
    qa_body_state *selected=components==QA_ENTITY_BODY_ALL?&state:out;
    if(body->external) {
        if(!qa_entity_body_read(body->binding.fields,pose,components,selected,error)) return false;
        if(!valid_state(selected,components)) return fail(error,QA_ERROR_FORMAT,"Binding returned invalid body state");
        if(components==QA_ENTITY_BODY_ALL) body->state=*selected;
    } else if(components==QA_ENTITY_BODY_ALL) state=body->state;
    else {
        selected->origin=body->state.origin;
        selected->angles=body->state.angles;
        selected->bounds=body->state.bounds;
    }
    if(components==QA_ENTITY_BODY_ALL) *out=*selected;
    return true;
}

bool qa_world_body_read_pose(qa_world *world,qa_actor_id actor,qa_entity_pose pose,
                            qa_body_state *out,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || out==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Actor body is unavailable");
    return qa_world_body_sample(body,pose,QA_ENTITY_BODY_ALL,out,error);
}

bool qa_world_body_read(qa_world *world,qa_actor_id actor,qa_body_state *out,qa_error *error)
{ return qa_world_body_read_pose(world,actor,QA_ENTITY_CONTROL_POSE,out,error); }

bool qa_world_body_write(qa_world *world,qa_actor_id actor,const qa_body_state *state,qa_error *error)
{
    if(!valid_state(state,QA_ENTITY_BODY_ALL)) return fail(error,QA_ERROR_ARGUMENT,"Invalid body state");
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return qa_world_body_create(world,actor,state,error);
    if(!body->external) { body->state=*state; return true; }
    uint64_t serial=body->storage_serial;
    qa_body_state copy=*state; qa_body_binding binding=body->binding;
    ++world->callback_depth; bool ok=binding.write(binding.context,&copy,error); --world->callback_depth;
    if(!ok) return false;
    return (qa_world_find_body(world,actor)==body && body->storage_serial==serial)
        || fail(error,QA_ERROR_NOT_FOUND,"Body storage changed during write callback");
}

static bool valid_collision(qa_world *world,const qa_actor_collision *collision,qa_status code,qa_error *error)
{
    if(collision!=NULL && (collision->family<QA_COLLISION_Q1 || collision->family>QA_COLLISION_Q3
        || (!collision->inline_model && (collision->model_geometry ||
            collision->shape<QA_SHAPE_BOX || collision->shape>QA_SHAPE_CAPSULE))
        || collision->owner.kind<QA_ACTOR_REFERENCE_NONE || collision->owner.kind>QA_ACTOR_REFERENCE_SOURCE
        || (collision->owner.kind==QA_ACTOR_REFERENCE_LIFETIME &&
            (!qa_actor_reference_present(collision->owner) || collision->owner.value.actor.registry!=qa_actors_identity(world->actors)))
        || collision->role<QA_COLLISION_SOLID || collision->role>QA_COLLISION_BOTH))
        return fail(error,code,"Invalid actor collision policy");
    if(collision!=NULL && collision->inline_model && collision->model>=qa_collision_model_count(qa_world_model_geometry(world,collision)))
        return fail(error,code,"Actor inline model is unavailable");
    return true;
}

bool qa_world_collision_validate(qa_world *world,const qa_actor_collision *collision,qa_error *error)
{
    if(world==NULL) return fail(error,QA_ERROR_ARGUMENT,"Collision validation requires a world");
    return valid_collision(world,collision,QA_ERROR_FORMAT,error);
}

bool qa_world_collision_rows_validate(qa_world *world,const qa_spatial_actor *rows,size_t count,qa_error *error)
{
    for(size_t i=0;i<count;++i) {
        const qa_spatial_actor *row=rows+i;
        if(qa_actors_get(world->actors,row->body.actor)==NULL)
            return fail(error,QA_ERROR_ARGUMENT,"Body actor is not live in this world");
        if(!valid_state(&row->body.state,QA_ENTITY_BODY_ALL))
            return fail(error,QA_ERROR_ARGUMENT,"Invalid body state");
        if(!valid_collision(world,&row->collision,QA_ERROR_ARGUMENT,error)) return false;
        qa_body_link_state link={.linked=true,.state=row->body.state,
            .absolute_bounds=row->body.absolute_bounds,.link_count=row->body.link_count};
        if(!valid_link_state(&link))
            return fail(error,QA_ERROR_ARGUMENT,"Invalid saved body link state");
    }
    return true;
}

bool qa_world_set_collision(qa_world *world,qa_actor_id actor,const qa_actor_collision *collision,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Actor body is unavailable");
    if(!valid_collision(world,collision,QA_ERROR_ARGUMENT,error)) return false;
    if(collision!=NULL && collision->model_geometry!=NULL &&
        !qa_world_prepare_trace_geometry(world,collision->model_geometry,error)) return false;
    body->has_collision=collision!=NULL;
    if(collision!=NULL) body->collision=*collision;
    return true;
}

bool qa_world_collision_bind(qa_world *world,qa_actor_id actor,const qa_collision_binding *binding,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Actor body is unavailable");
    if(binding!=NULL && (binding->fields==NULL || binding->fields->family<QA_COLLISION_Q1 ||
        binding->fields->family>QA_COLLISION_Q3))
        return fail(error,QA_ERROR_ARGUMENT,"Collision binding needs fields");
    if(binding!=NULL && binding->fields->models!=NULL) {
        const qa_entity_model_fields *models=binding->fields->models;
        for(uint32_t index=0;index<models->count;++index) {
            const qa_entity_model_field *model=models->entries+index;
            if(model->present && model->geometry!=NULL &&
                !qa_world_prepare_trace_geometry(world,model->geometry,error)) return false;
        }
    }
    if(body->collision_serial==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Collision binding identity exhausted");
    ++body->collision_serial;
    body->collision_binding=binding!=NULL?*binding:(qa_collision_binding){0};
    return true;
}
bool qa_world_collision_unbind(qa_world *world,qa_actor_id actor,void *expected_context,qa_error *error)
{
    if(world==NULL || !qa_world_idle(world))
        return fail(error,QA_ERROR_ARGUMENT,"Collision binding teardown requires an idle world");
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || body->collision_binding.fields==NULL || body->collision_binding.context!=expected_context)
        return true;
    if(body->collision_serial==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Collision binding identity exhausted");
    ++body->collision_serial;
    body->collision_binding=(qa_collision_binding){0};
    return true;
}

bool qa_world_collision_sample(const qa_world_body *body,bool link_metadata,
                               qa_entity_collision_components components,
                               qa_actor_collision *out,qa_error *error)
{
    const qa_entity_collision_fields *fields=body->collision_binding.fields;
    if(fields==NULL) {
        if(!body->has_collision) return false;
        if(components==QA_ENTITY_COLLISION_ROLE) out->role=body->collision.role;
        else *out=body->collision;
        return true;
    }
    return qa_entity_collision_read(fields,link_metadata,components,out,error);
}

static bool read_collision(qa_world *world,qa_actor_id actor,qa_actor_collision *out,
                           bool link_metadata,qa_error *error)
{
    if(world==NULL || out==NULL) return fail(error,QA_ERROR_ARGUMENT,"Invalid collision read");
    qa_world_body *body=qa_world_find_body(world,actor);
    return body!=NULL && qa_world_collision_sample(body,link_metadata,QA_ENTITY_COLLISION_ALL,out,error);
}

bool qa_world_get_collision(qa_world *world,qa_actor_id actor,qa_actor_collision *out,qa_error *error)
{ return read_collision(world,actor,out,false,error); }
bool qa_world_get_link_collision(qa_world *world,qa_actor_id actor,qa_actor_collision *out,qa_error *error)
{ return read_collision(world,actor,out,true,error); }
bool qa_world_refresh(qa_world *world,qa_actor_id actor,qa_entity_pose pose,
                      qa_entity_body_components components,
                      qa_spatial_actor *out,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return false;
    qa_spatial_actor current={.body=qa_world_published_body(world,body)};
    if(!qa_world_collision_sample(body,false,QA_ENTITY_COLLISION_ALL,&current.collision,error) ||
       !qa_world_body_sample(body,pose,components,&current.body.state,error)) return false;
    *out=current; return true;
}

bool qa_world_attach(qa_world *world,qa_actor_id actor,const qa_body_attachment *attachment,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || attachment==NULL || !qa_vec_finite(attachment->offset)
        || attachment->follow<QA_BODY_FOLLOW_TRANSLATION || attachment->follow>QA_BODY_FOLLOW_BOUNDS_MIN)
        return fail(error,QA_ERROR_ARGUMENT,"Invalid body attachment");
    qa_world_body *anchor=qa_world_find_body(world,attachment->anchor);
    if(anchor==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Attachment anchor body is unavailable");
    for(uint32_t depth=0;anchor!=NULL;++depth) {
        if(depth>=world->capacity || qa_actor_id_equal(anchor->actor,actor))
            return fail(error,QA_ERROR_ARGUMENT,"Body attachment cycle");
        anchor=anchor->attached?qa_world_find_body(world,anchor->attachment.anchor):NULL;
    }
    if(!body->attached) {
        if(world->attachment_order==UINT64_MAX) return fail(error,QA_ERROR_ARGUMENT,"Attachment insertion order exhausted");
        body->attachment_order=++world->attachment_order;
    }
    body->attached=true; body->attachment=*attachment; return true;
}

bool qa_world_detach(qa_world *world,qa_actor_id actor,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Actor body is unavailable");
    body->attached=false; return true;
}

bool qa_world_attachment(const qa_world *world,qa_actor_id actor,qa_body_attachment *out)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || !body->attached || out==NULL) return false;
    *out=body->attachment; return true;
}

bool qa_world_next_attachment(const qa_world *world,uint64_t *cursor,qa_actor_id *actor,qa_body_attachment *out)
{
    if(world==NULL || cursor==NULL || actor==NULL || out==NULL) return false;
    const qa_world_body *next=NULL;
    for(uint32_t slot=0;slot<world->capacity;++slot) {
        qa_world_body *body=qa_world_raw_body(world,slot);
        if(body!=NULL && body->present && body->attached && body->attachment_order>*cursor
            && qa_actors_get(world->actors,body->actor)!=NULL && (next==NULL || body->attachment_order<next->attachment_order)) next=body;
    }
    if(next==NULL) return false;
    *cursor=next->attachment_order; *actor=next->actor; *out=next->attachment; return true;
}

typedef enum body_link_policy { BODY_LINK_COMMIT, BODY_LINK_EXPLICIT, BODY_LINK_RESTORE } body_link_policy;

static bool publish_link(qa_world *world,qa_world_body *body,const qa_linked_body *linked,
                         body_link_policy policy,qa_error *error)
{
    qa_spatial_link *spatial=qa_actors_link(world->actors->links,body->actor.slot);
    uint64_t serial=body->storage_serial;
    qa_actor_collision collision;
    qa_error local={0};
    if(!qa_world_get_link_collision(world,linked->actor,&collision,&local)) {
        if(local.code!=QA_OK) { if(error!=NULL) *error=local; return false; }
        memset(&collision,0,sizeof(collision));
        collision.family=qa_collision_geometry_family(world->geometry);
    }
    if(policy==BODY_LINK_COMMIT && body->linked && spatial->linked && same_state(&body->linked_state,&linked->state)
        && same_bounds(spatial->bounds,linked->absolute_bounds)
        && same_collision(&body->linked_collision,&collision)) return true;
    qa_linked_body snapshot=*linked;
    if(policy!=BODY_LINK_RESTORE) {
        if(body->link_count==UINT64_MAX) return fail(error,QA_ERROR_ARGUMENT,"Body link count exhausted");
        snapshot.link_count=body->link_count+1;
    }
    linked=&snapshot;
    if(body->leaves_ready || body->leaves) {
        qa_world_leaf_membership membership;
        if(!qa_world_link_membership(world,linked->actor,&linked->absolute_bounds,
            body->leaf_policy,&membership,error)) return false;
    }
    body->linked=true; body->linked_state=linked->state;
    spatial->bounds=linked->absolute_bounds; body->link_count=linked->link_count;
    body->linked_collision=collision;
    qa_spatial_publish(world,body->actor.slot);
    qa_linked_body copy=*linked;
    if(body->external && body->binding.linked!=NULL) {
        qa_body_binding binding=body->binding;
        ++world->callback_depth; binding.linked(binding.context,&copy); --world->callback_depth;
    }
    body=qa_world_find_body(world,copy.actor);
    if(body!=NULL && body->storage_serial==serial && body->linked
        && body->link_count==copy.link_count && world->hooks.linked!=NULL) {
        ++world->callback_depth; world->hooks.linked(world->hooks.context,&copy); --world->callback_depth;
    }
    return true;
}

typedef struct membership_writer { qa_world_body *body; size_t limit; } membership_writer;
static qa_leaf_visit membership_leaf(void *context,const qa_collision_leaf *leaf,qa_error *error)
{
    membership_writer *writer=context;
    qa_world_body *body=writer->body;
    if(body->leaf_count==body->leaf_capacity) {
        size_t capacity=body->leaf_capacity?body->leaf_capacity*2:8;
        if(capacity<body->leaf_capacity || capacity>SIZE_MAX/sizeof(*body->leaves)) {
            fail(error,QA_ERROR_MEMORY,"Linked leaf membership extent overflow"); return QA_LEAF_FAILED;
        }
        qa_collision_leaf *leaves=realloc(body->leaves,capacity*sizeof(*leaves));
        if(!leaves) { fail(error,QA_ERROR_MEMORY,"Retaining linked leaf membership"); return QA_LEAF_FAILED; }
        body->leaves=leaves; body->leaf_capacity=capacity;
    }
    body->leaves[body->leaf_count++]=*leaf;
    return body->leaf_count==writer->limit?QA_LEAF_STOP:QA_LEAF_CONTINUE;
}

bool qa_world_link_membership(qa_world *world,qa_actor_id actor,const qa_bounds *explicit_bounds,
    qa_world_leaf_policy policy,qa_world_leaf_membership *out,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(!body || !out || (unsigned)policy>QA_WORLD_LEAVES_Q1_TOUCHED ||
        (!explicit_bounds && !body->linked))
        return fail(error,QA_ERROR_ARGUMENT,"Leaf membership requires its actual body and bounds");
    qa_bounds bounds=explicit_bounds?*explicit_bounds:qa_actors_link(world->actors->links,body->actor.slot)->bounds;
    if(!qa_bounds_valid(bounds) || !world->geometry ||
        (policy==QA_WORLD_LEAVES_Q1_TOUCHED && qa_collision_geometry_family(world->geometry)!=QA_COLLISION_Q1))
        return fail(error,QA_ERROR_ARGUMENT,"Leaf membership lost its actual geometry or Source policy");
    if(!body->leaves_ready || body->leaf_geometry!=world->geometry ||
        body->leaf_storage!=body->storage_serial || body->leaf_policy!=policy ||
        memcmp(&body->leaf_bounds,&bounds,sizeof(bounds))) {
        body->leaves_ready=false; body->leaf_count=0;
        qa_leaf_list list;
        membership_writer writer={body,policy==QA_WORLD_LEAVES_Q1_TOUCHED?16:SIZE_MAX};
        if(!qa_collision_walk_leaves(world->geometry,world->trace_scratch,bounds,policy==QA_WORLD_LEAVES_Q1_TOUCHED,
            membership_leaf,&writer,&list,error)) return false;
        body->leaf_bounds=bounds; body->leaf_geometry=world->geometry;
        body->leaf_storage=body->storage_serial; body->leaf_policy=policy;
        body->leaf_topnode=list.topnode; body->leaf_last=list.last_leaf; body->leaves_ready=true;
    }
    *out=(qa_world_leaf_membership){body->leaves,body->leaf_count,body->leaf_topnode,body->leaf_last};
    return true;
}

bool qa_world_q1_visible(qa_world *world,qa_actor_id actor,const qa_bounds *bounds,
    qa_bytes pvs,bool *out,qa_error *error)
{
    qa_collision_geometry *geometry=qa_world_geometry(world);
    if(!geometry || qa_collision_geometry_family(geometry)!=QA_COLLISION_Q1 || !out ||
        (bounds && !qa_bounds_valid(*bounds)) ||
        pvs.size!=qa_collision_q1_pvs_bytes(geometry) || (pvs.size && !pvs.data))
        return fail(error,QA_ERROR_ARGUMENT,"Q1 entity visibility requires its actual fat-PVS and source bounds");
    *out=false;
    qa_world_leaf_membership membership;
    if(!qa_world_link_membership(world,actor,bounds,QA_WORLD_LEAVES_Q1_TOUCHED,&membership,error)) return false;
    *out=qa_collision_q1_membership_visible(pvs,membership.leaves,membership.count);
    return true;
}

static bool link_body(qa_world *world,qa_actor_id actor,const qa_vec3 *origin_override,
                       const qa_bounds *explicit_bounds,bool force,qa_error *error)
{
    if(origin_override!=NULL && !qa_vec_finite(*origin_override)) return fail(error,QA_ERROR_ARGUMENT,"Invalid link origin");
    qa_bounds bounds={0};
    if(explicit_bounds!=NULL) {
        bounds=*explicit_bounds;
        if(!qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid explicit body bounds");
    }
    qa_body_state state;
    if(!qa_world_body_read(world,actor,&state,error)) return false;
    if(origin_override!=NULL) state.origin=*origin_override;
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL) return fail(error,QA_ERROR_NOT_FOUND,"Actor retired while linking");
    uint64_t serial=body->storage_serial;
    if(explicit_bounds==NULL) bounds=qa_bounds_translate(state.bounds,state.origin);
    if(explicit_bounds==NULL && world->hooks.absolute_bounds!=NULL) {
        ++world->callback_depth;
        bool ok=world->hooks.absolute_bounds(world->hooks.context,actor,&state,&bounds,error);
        --world->callback_depth;
        if(!ok) return false;
        body=qa_world_find_body(world,actor);
        if(body==NULL || body->storage_serial!=serial)
            return fail(error,QA_ERROR_NOT_FOUND,"Body storage changed during link bounds callback");
    }
    if(!qa_bounds_valid(bounds)) return fail(error,QA_ERROR_ARGUMENT,"Invalid absolute body bounds");
    qa_linked_body linked={actor,state,bounds,body->link_count};
    return publish_link(world,body,&linked,force?BODY_LINK_EXPLICIT:BODY_LINK_COMMIT,error);
}

bool qa_world_body_commit(qa_world *world,qa_actor_id actor,const qa_body_state *state,
                           bool link,qa_error *error)
{
    if(state!=NULL) {
        qa_body_state current;
        if(!qa_world_body_read(world,actor,&current,error) ||
            (!same_state(&current,state) && !qa_world_body_write(world,actor,state,error))) return false;
    }
    if(!link || qa_actors_get(world->actors,actor)==NULL) return true;
    return link_body(world,actor,NULL,NULL,false,error);
}

bool qa_world_link(qa_world *world,qa_actor_id actor,const qa_vec3 *origin_override,qa_error *error)
{ return link_body(world,actor,origin_override,NULL,true,error); }

bool qa_world_link_bounds(qa_world *world,qa_actor_id actor,const qa_bounds *bounds,qa_error *error)
{ return qa_world_link_bounds_at(world,actor,bounds,NULL,error); }

bool qa_world_link_bounds_at(qa_world *world,qa_actor_id actor,const qa_bounds *bounds,
                             const qa_vec3 *origin_override,qa_error *error)
{
    if(bounds==NULL) return fail(error,QA_ERROR_ARGUMENT,"Missing explicit body bounds");
    return link_body(world,actor,origin_override,bounds,true,error);
}

bool qa_world_unlink(qa_world *world,qa_actor_id actor,qa_error *error)
{
    if(world==NULL || qa_actors_get(world->actors,actor)==NULL) return fail(error,QA_ERROR_ARGUMENT,"Actor is not live in this world");
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || !body->linked) return true;
    qa_spatial_remove(world,body->actor.slot); body->linked=false; notify_unlink(world,actor); return true;
}

bool qa_world_suspend_collision(qa_world *world,qa_actor_id actor,qa_error *error)
{
    if(world==NULL || qa_actors_get(world->actors,actor)==NULL)
        return fail(error,QA_ERROR_ARGUMENT,"Actor is not live in this world");
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body!=NULL) qa_spatial_remove(world,body->actor.slot);
    return true;
}

bool qa_world_linked(const qa_world *world,qa_actor_id actor,qa_linked_body *out)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || !body->linked || out==NULL) return false;
    *out=qa_world_published_body(world,body); return true;
}

bool qa_world_link_state(const qa_world *world,qa_actor_id actor,qa_body_link_state *out)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || out==NULL) return false;
    *out=(qa_body_link_state){body->link_count,body->linked,body->linked_state,qa_actors_link(world->actors->links,body->actor.slot)->bounds}; return true;
}

bool qa_world_restore_link_state(qa_world *world,qa_actor_id actor,const qa_body_link_state *saved,qa_error *error)
{
    qa_world_body *body=qa_world_find_body(world,actor);
    if(body==NULL || !valid_link_state(saved))
        return fail(error,QA_ERROR_ARGUMENT,"Invalid saved body link state");
    if(!saved->linked) {
        qa_spatial_remove(world,body->actor.slot); body->linked=false; body->link_count=saved->link_count;
        notify_unlink(world,actor); return true;
    }
    qa_linked_body linked={actor,saved->state,saved->absolute_bounds,saved->link_count};
    return publish_link(world,body,&linked,BODY_LINK_RESTORE,error);
}

typedef struct attachment_transport { qa_actor_id actor; qa_body_attachment attachment; } attachment_transport;
static bool same_float(float left,float right)
{ return left==right && (left!=0.0f || signbit(left)==signbit(right)); }

bool qa_world_transport_attachments(qa_world *world,qa_error *error)
{
    if(world==NULL) return fail(error,QA_ERROR_ARGUMENT,"Missing world");
    size_t capacity=world->capacity;
    if(capacity>SIZE_MAX/sizeof(attachment_transport)) return fail(error,QA_ERROR_MEMORY,"Attachment traversal too large");
    qa_actor_id *visited=calloc(capacity,sizeof(*visited));
    qa_actor_id *visiting=calloc(capacity,sizeof(*visiting));
    attachment_transport *chain=malloc(capacity*sizeof(*chain));
    if(visited==NULL || visiting==NULL || chain==NULL) { free(visited); free(visiting); free(chain); return fail(error,QA_ERROR_MEMORY,"Cannot allocate attachment traversal"); }
    bool ok=true;
    uint64_t last_order=0;
    while(ok) {
        qa_world_body *body=NULL;
        for(uint32_t slot=0;slot<world->capacity;++slot) {
            qa_world_body *candidate=qa_world_raw_body(world,slot);
            if(candidate!=NULL && candidate->present && candidate->attached && candidate->attachment_order>last_order
                && (body==NULL || candidate->attachment_order<body->attachment_order)) body=candidate;
        }
        if(body==NULL) break;
        last_order=body->attachment_order;
        if(qa_actor_id_equal(visited[body->actor.slot],body->actor)) continue;
        size_t depth=0;
        while(body!=NULL && body->attached && !qa_actor_id_equal(visited[body->actor.slot],body->actor)) {
            if(qa_actor_id_equal(visiting[body->actor.slot],body->actor)) { ok=fail(error,QA_ERROR_ARGUMENT,"Body attachment cycle during transport"); break; }
            visiting[body->actor.slot]=body->actor;
            chain[depth++]=(attachment_transport){body->actor,body->attachment};
            body=qa_world_find_body(world,body->attachment.anchor);
        }
        while(depth>0 && ok) {
            attachment_transport entry=chain[--depth];
            qa_actor_id actor=entry.actor; visited[actor.slot]=actor; visiting[actor.slot]=(qa_actor_id){0};
            body=qa_world_find_body(world,actor);
            if(body==NULL) continue;
            /* Match the source capture before transporting the anchor. A
             * nested link can change the next transport's attachment only. */
            qa_body_attachment follow=entry.attachment;
            if(qa_world_find_body(world,follow.anchor)==NULL) continue;
            qa_body_state anchor,current;
            if(!qa_world_body_read(world,follow.anchor,&anchor,error) || !qa_world_body_read(world,actor,&current,error)) { ok=false; break; }
            qa_vec3 offset=follow.follow==QA_BODY_FOLLOW_CENTER?qa_vec_scale(qa_vec_add(anchor.bounds.mins,anchor.bounds.maxs),0.5f)
                :follow.follow==QA_BODY_FOLLOW_BOUNDS_MIN?qa_vec_add(anchor.bounds.mins,follow.offset):follow.offset;
            qa_vec3 origin=qa_vec_add(anchor.origin,offset);
            if(same_float(origin.x,current.origin.x) && same_float(origin.y,current.origin.y) && same_float(origin.z,current.origin.z)) continue;
            current.origin=origin;
            if(!qa_world_body_write(world,actor,&current,error) || !qa_world_link(world,actor,NULL,error)) ok=false;
        }
    }
    free(visited); free(visiting); free(chain); return ok;
}
