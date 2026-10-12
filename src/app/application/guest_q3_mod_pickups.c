#include "guest_q3_mod_private.h"

static void row_free(mod_pickup_actor *row)
{
    mod_actor_resources *resources=row->resources;
    if(resources) {
        resources->pickup=NULL; row->resources=NULL;
        q3mod_resources_collect(row->owner,resources);
    }
    application_q3_mod *o=row->owner;
    if(row->rules_slot!=SIZE_MAX)qa_pool_release_run(&o->pickup_rules,row->rules_slot,o->profile->pickup_count);
    if(row->bindings_slot!=SIZE_MAX)qa_pool_release_run(&o->pickup_bindings,row->bindings_slot,o->profile->pickup_count);
    qa_pool_release(&o->pickup_pool,row->storage_slot);
}
static bool take(void *context,const qa_pickup_offer *offer,qa_pickup_execution *execution,qa_pickup_outcome *out,qa_error *e)
{
    mod_pickup_bound *bound=context; mod_pickup_actor *actor=bound->actor;
    if(!qa_actor_id_equal(actor->actor,offer->recipient)||!qa_pickups_registration_current(actor->owner->services.pickups,actor->lease,actor->owner->owner))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickup rule lost its admitted full recipient");
    application_q3_mod *o=actor->owner;
    bool ok=q3mod_pickup_run(o,bound->definition,offer,execution,out,e);
    if(!o->pickup_calls) { mod_pickup_actor **link=&o->pickup_actors;
        while(*link) { mod_pickup_actor *row=*link; if(row->lease.serial) { link=&row->next; continue; }
            *link=row->next; row_free(row);
        }
    }
    return ok;
}
static bool rules_create(application_q3_mod *o,qa_actor_id actor,mod_pickup_actor **out,qa_error *e)
{
    if(o->pickup_pool.head==SIZE_MAX&&!qa_pool_extend(&o->pickup_pool,
        qa_unified_frame_lease_arena(o->resource_storage),o->pickup_chunk,e))return false;
    size_t slot;mod_pickup_actor *row=qa_pool_take(&o->pickup_pool,&slot);
    if(!row) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning admitted original pickup recipient");
    *row=(mod_pickup_actor){.owner=o,.actor=actor,.storage_slot=slot,.rules_slot=SIZE_MAX,.bindings_slot=SIZE_MAX};
    size_t count=o->profile->pickup_count;
    bool okay=(qa_pool_run_available(&o->pickup_rules,count)||qa_pool_extend(&o->pickup_rules,
        qa_unified_frame_lease_arena(o->resource_storage),o->pickup_rule_chunk,e))&&
        (qa_pool_run_available(&o->pickup_bindings,count)||qa_pool_extend(&o->pickup_bindings,
            qa_unified_frame_lease_arena(o->resource_storage),o->pickup_rule_chunk,e));
    if(okay){
        row->rules=qa_pool_take_run(&o->pickup_rules,count,&row->rules_slot);
        row->bindings=qa_pool_take_run(&o->pickup_bindings,count,&row->bindings_slot);
        okay=row->rules&&row->bindings;
    }
    if(!okay){row_free(row);return false;}
    for(size_t i=0;i<o->profile->pickup_count;++i) {
        const mod_pickup *definition=o->profile->pickups+i;
        row->bindings[i]=(mod_pickup_bound){row,definition};
        row->rules[i]=(qa_pickup_rule){.id=definition->id,.offered=definition->offered,.offered_count=definition->offered_count,
            .writes=definition->writes,.write_count=definition->write_count,.context=row->bindings+i,.take=take};
    }
    *out=row; return true;
}
bool q3mod_pickups_admit(application_q3_mod *o,qa_actor_id actor,qa_error *e)
{
    if(!o->profile->pickup_count||!o->active||!o->services.live_client(o->services.context,actor)) return true;
    mod_actor_resources *resources=q3mod_resources_find(o,actor);
    mod_pickup_actor *row=resources?resources->pickup:NULL;
    if(row)
        return qa_pickups_registration_current(o->services.pickups,row->lease,o->owner)||q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickup recipient retained a retired canonical registration");
    if(!rules_create(o,actor,&row,e)) return false;
    resources=q3mod_resources_take(o,actor,e);
    if(!resources) { row_free(row); return false; }
    ++resources->admissions;
    bool bound=qa_pickups_bind(o->services.pickups,actor,o->owner,row->rules,o->profile->pickup_count,&row->lease,e);
    resources=q3mod_resources_find(o,actor);
    if(bound) {
        row->resources=resources; resources->pickup=row;
        mod_pickup_actor **tail=&o->pickup_actors; while(*tail) tail=&(*tail)->next; *tail=row;
    }
    --resources->admissions;
    if(!bound) { row_free(row); q3mod_resources_collect(o,resources); }
    return bound;
}
bool q3mod_pickups_release(application_q3_mod *o,qa_actor_id actor,qa_error *e)
{
    mod_actor_resources *resources=q3mod_resources_find(o,actor);
    mod_pickup_actor *row=resources?resources->pickup:NULL;
    if(!row) return true;
    if(row->lease.serial&&!qa_pickups_close(o->services.pickups,row->lease,e)) return false;
    row->lease.serial=0;
    if(o->pickup_calls) return true;
    mod_pickup_actor **link=&o->pickup_actors;
    while(*link&&*link!=row) link=&(*link)->next;
    if(*link) *link=row->next;
    row_free(row);
    return true;
}
bool q3mod_pickups_close(application_q3_mod *o,qa_error *e)
{
    if(o->pickup_calls) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickups retain their actual source execution");
    while(o->pickup_actors) if(!q3mod_pickups_release(o,o->pickup_actors->actor,e)) return false;
    return !o->pickup_calls;
}
bool application_q3_mod_pickup_saved_rule(application_q3_mod *o,qa_actor_id actor,qa_actor_owner owner,
    uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *e)
{
    if(!o||!out||owner!=o->owner||!serial||!q3mod_storage_current(o,e)) return false;
    mod_actor_resources *resources=q3mod_resources_find(o,actor);
    mod_pickup_actor *row=resources?resources->pickup:NULL;
    if(row&&row->lease.serial==serial)
        for(size_t i=0;i<o->profile->pickup_count;++i) if(row->rules[i].id==id) { *out=row->rules[i]; return true; }
    return q3mod_fail(e,QA_ERROR_NOT_FOUND,"Saved original pickup has no actual declared recipient rule");
}
bool q3mod_pickups_fields(application_q3_mod *o,qa_source_save_io *io)
{
    size_t count=0;
    for(mod_pickup_actor *row=o->pickup_actors;row;row=row->next) ++count;
    if(!qa_source_save_count(io,&count,UINT32_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) {
        for(mod_pickup_actor *row=o->pickup_actors;row;row=row->next) {
            qa_actor_id actor=row->actor; uint64_t serial=row->lease.serial;
            if(!qa_source_save_actor(io,&actor)||!qa_source_save_u64(io,&serial)) return false;
        }
        return true;
    }
    mod_pickup_actor **tail=&o->pickup_actors;
    for(size_t i=0;i<count;++i) {
        qa_actor_id actor={0}; uint64_t serial=0; int32_t slot;
        if(!qa_source_save_actor(io,&actor)||!qa_source_save_u64(io,&serial)||!serial||!o->active||!o->profile->pickup_count||
            !qa_actors_get(qa_session_actors(o->session),actor)||!o->services.client_slot(o->services.context,actor,&slot,io->error)) return false;
        mod_actor_resources *resources=q3mod_resources_find(o,actor);
        if(resources&&resources->pickup)
            return q3mod_fail(io->error,QA_ERROR_FORMAT,"Saved original pickup recipients duplicate physical ownership");
        for(mod_pickup_actor *row=o->pickup_actors;row;row=row->next)
            if(row->lease.serial==serial) return q3mod_fail(io->error,QA_ERROR_FORMAT,"Saved original pickup recipients duplicate physical ownership");
        mod_pickup_actor *row=NULL; if(!rules_create(o,actor,&row,io->error)) return false;
        resources=q3mod_resources_take(o,actor,io->error);
        if(!resources) { row_free(row); return false; }
        row->resources=resources; resources->pickup=row;
        row->lease=(qa_pickup_lease){actor,serial}; *tail=row; tail=&row->next;
    }
    return true;
}
bool q3mod_pickups_validate(application_q3_mod *o,qa_error *e)
{
    for(mod_pickup_actor *row=o->pickup_actors;row;row=row->next)
        if(!o->active||!o->services.live_client(o->services.context,row->actor)||!qa_pickups_registration_current(o->services.pickups,row->lease,o->owner))
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickup continuation differs from its actual canonical resource graph");
    return true;
}
