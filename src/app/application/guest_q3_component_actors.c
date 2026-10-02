#include "guest_q3_component_private.h"

static bool owned(void *context,qa_actor_id actor)
{
    application_q3_component *c=context;
    component_actor *row=q3records_actor(c->records,actor);
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(c->options.host.session),actor);
    return row&&!row->retired&&row->owned&&actual&&actual->owner==c->options.host.owner;
}
static bool pointer(void *context,qa_actor_id actor,uint32_t *out,qa_error *e)
{
    application_q3_component *c=context;
    if(!actor.registry) { *out=0; return true; }
    if(c->entity_record==SIZE_MAX) return q3records_fail(e,QA_ERROR_ARGUMENT,"Actor semantics require their declared entity record");
    return application_q3_component_records_pointer(c->records,actor,c->records->records[c->entity_record].id,out,e);
}
static bool actor(void *context,int32_t word,qa_actor_id *out,qa_error *e)
{
    application_q3_component *c=context;
    *out=(qa_actor_id){0};
    if(!word) return true;
    if(c->entity_record==SIZE_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Actor callback has no declared entity array");
    component_record *record=c->records->records+c->entity_record;
    uint32_t at=(uint32_t)word;
    if(at<record->address||(uint64_t)at>=(uint64_t)record->address+(uint64_t)record->stride*record->capacity||(at-record->address)%record->stride)
        return q3records_fail(e,QA_ERROR_FORMAT,"Actor callback pointer leaves its actual entity row");
    uint32_t slot=(at-record->address)/record->stride;
    for(size_t i=0;i<c->records->actor_count;++i) {
        component_actor *row=c->records->actors+i;
        if(row->slot==slot&&!row->retired&&q3records_live(c->records,row->actor)) { *out=row->actor; return true; }
    }
    return true;
}
static bool team(void *context,qa_actor_id actor,qa_team_id *out,qa_error *e)
{
    application_q3_component *c=context; double score;
    return c->options.match_read&&c->options.match_read(c->options.context,actor,out,&score,e);
}
static bool damage(void *context,qa_damage_request *request,qa_error *e)
{
    application_q3_component *c=context;
    return c->options.damage_context&&c->options.damage_context(c->options.context,request,e);
}
bool q3component_actors_create(application_q3_component *c,qa_error *e)
{
    application_q3_mod_actors_options options={.profile=c->profile,.mod=c->mod,.operations=c->options.actor_operations,
        .vm=c->vm,.session=c->options.host.session,.world=c->options.host.world,.combat=c->options.combat,
        .owner=c->options.host.owner,.context=c,.current=q3component_current,.owned=owned,
        .pointer=pointer,.actor=actor,.team=team,.damage_context=damage};
    return application_q3_mod_actors_create(&options,c->restoring,&c->actor_semantics,e);
}
static bool actor_hook(void *context,const qa_qvm_call *call,int32_t *result,qa_error *e)
{
    application_q3_component *c=context;
    if(c->actor_error.code!=QA_OK) { if(e) *e=c->actor_error; c->actor_error=(qa_error){0}; return false; }
    return application_q3_mod_actors_hook(c->actor_semantics,call,result,e);
}
qa_qvm_function_hook q3component_actor_resolve(void *context,const qa_qvm_call *call,void **hook_context)
{
    application_q3_component *c=context; bool found=false; qa_error e={0};
    if(!application_q3_mod_actors_match(c->actor_semantics,call,&found,&e)) {
        c->actor_error=e; *hook_context=c; return actor_hook;
    }
    if(!found) return NULL;
    *hook_context=c; return actor_hook;
}
bool application_q3_component_touch(application_q3_component *c,const qa_touch_contact *contact,bool *handled,qa_error *e)
{
    return c&&application_q3_mod_actors_touch(c->actor_semantics,contact,handled,e);
}
bool application_q3_component_actor_callback(application_q3_component *c,application_q3_mod_operation operation,
    const application_q3_mod_actor_request *request,bool *handled,qa_error *e)
{
    return c&&application_q3_mod_actors_callback(c->actor_semantics,operation,request,handled,e);
}
bool application_q3_component_combat_binding(application_q3_component *c,qa_actor_id actor,uint64_t serial,qa_combat_binding *out,qa_error *e)
{
    return c&&application_q3_mod_actors_binding_saved(c->actor_semantics,actor,serial,out,e);
}
