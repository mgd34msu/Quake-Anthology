#include "guest_q3_mod_operations.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct operation_service {
    application_q3_mod_operations *owner;
    application_q3_mod_operation kind;
    qa_operation *operation;
} operation_service;
struct application_q3_mod_operations {
    qa_session *session;
    operation_service rows[Q3_MOD_OPERATION_COUNT];
};
static bool fail(qa_error *e, const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
static void actor(application_q3_mod_inputs *v, application_q3_mod_input n, qa_actor_id value)
{ v->values[n]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=value}; }
static void scalar(application_q3_mod_inputs *v, application_q3_mod_input n, double value)
{ v->values[n]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=value}; }
static void vector(application_q3_mod_inputs *v, application_q3_mod_input n, qa_vec3 value)
{ v->values[n]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_VECTOR,.as.vector=value}; }
static bool inputs(void *context,const void *request,const void *result,
    application_q3_mod_inputs *v,qa_error *e)
{
    operation_service *s=context;
    if(!s||!request||!v) return fail(e,"Mod callback requires its actual canonical operation");
    memset(v,0,sizeof(*v));
    if(s->kind==Q3_MOD_DAMAGE) {
        const qa_damage_request *r=request;
        actor(v,Q3_MOD_SELF,r->target); actor(v,Q3_MOD_ATTACKER,r->attack.attacker);
        actor(v,Q3_MOD_INFLICTOR,r->attack.inflictor);
        scalar(v,Q3_MOD_AMOUNT,r->amount); scalar(v,Q3_MOD_KNOCKBACK,r->knockback);
        vector(v,Q3_MOD_DIRECTION,r->direction); vector(v,Q3_MOD_POINT,r->point); vector(v,Q3_MOD_NORMAL,r->normal);
        if(result) { const qa_damage_outcome *out=result; scalar(v,Q3_MOD_RESULT,out->stale?0:out->result.applied_damage); }
    } else if(s->kind==Q3_MOD_GIVE||s->kind==Q3_MOD_CONSUME) {
        const qa_inventory_request *r=request;
        const char *name=qa_strings_cstr(qa_session_strings(s->owner->session),r->item);
        if(!name) return fail(e,"Inventory mod callback lost its canonical item identity");
        actor(v,Q3_MOD_SELF,r->actor); scalar(v,Q3_MOD_AMOUNT,r->amount);
        v->values[Q3_MOD_ITEM]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_STRING,.as.string=name};
        if(result) { const qa_inventory_result *out=result; scalar(v,Q3_MOD_RESULT,s->kind==Q3_MOD_GIVE?out->amount:out->consumed?1:0); }
    } else {
        const application_q3_mod_actor_request *r=request;
        actor(v,Q3_MOD_SELF,r->self);
        switch(s->kind) {
        case Q3_MOD_THINK:
            scalar(v,Q3_MOD_TIME,(double)r->source.think.time_ns/1000000000.0);
            scalar(v,Q3_MOD_ELAPSED,(double)r->source.think.elapsed_ns/1000000000.0); break;
        case Q3_MOD_TOUCH: actor(v,Q3_MOD_OTHER,r->source.touch.other); break;
        case Q3_MOD_USE:
            actor(v,Q3_MOD_OTHER,r->source.use.other); actor(v,Q3_MOD_ACTIVATOR,r->source.use.activator); break;
        case Q3_MOD_PAIN:
            actor(v,Q3_MOD_ATTACKER,r->source.pain.attacker); scalar(v,Q3_MOD_AMOUNT,r->source.pain.damage);
            scalar(v,Q3_MOD_KNOCKBACK,r->source.pain.kick); break;
        case Q3_MOD_DIE:
            actor(v,Q3_MOD_ATTACKER,r->source.die.attacker); actor(v,Q3_MOD_INFLICTOR,r->source.die.inflictor);
            scalar(v,Q3_MOD_AMOUNT,r->source.die.damage); scalar(v,Q3_MOD_KNOCKBACK,r->source.die.kick);
            vector(v,Q3_MOD_POINT,r->source.die.point); break;
        default: return fail(e,"Unknown canonical actor callback channel");
        }
        if(result) scalar(v,Q3_MOD_RESULT,*(const bool *)result?1:0);
    }
    return true;
}
static bool transform(void *context,void *request,bool knockback,double value,qa_error *e)
{
    operation_service *s=context;
    if(!s||!request||!isfinite(value)) return fail(e,"Mod transform returned a nonfinite native value");
    if(s->kind==Q3_MOD_DAMAGE) {
        float narrowed=(float)value;
        if(!isfinite(narrowed)) return fail(e,"Mod damage transform exceeds canonical binary32");
        qa_damage_request *r=request; if(knockback) r->knockback=narrowed; else r->amount=narrowed;
        return qa_damage_request_validate(r,e);
    }
    if((s->kind==Q3_MOD_GIVE||s->kind==Q3_MOD_CONSUME)&&!knockback) {
        ((qa_inventory_request *)request)->amount=value; return true;
    }
    return fail(e,"Mod transform does not own this native request field");
}
static bool replace(void *context,void *result,bool value,qa_error *e)
{
    operation_service *s=context;
    if(!s||s->kind<Q3_MOD_THINK||!result) return fail(e,"Mod replacement has no canonical actor result");
    *(bool *)result=value; return true;
}
bool application_q3_mod_operations_create(qa_session *session,qa_combat *combat,qa_inventory *inventory,
    application_q3_mod_operations **out,qa_error *e)
{
    if(!session||!combat||!inventory||!out||*out) return fail(e,"Mod operations require actual canonical gameplay owners");
    application_q3_mod_operations *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning canonical mod operation adapters"); return false; }
    o->session=session;
    for(size_t i=0;i<Q3_MOD_OPERATION_COUNT;++i) {
        o->rows[i]=(operation_service){.owner=o,.kind=(application_q3_mod_operation)i};
        if(i==Q3_MOD_DAMAGE) o->rows[i].operation=qa_combat_damage_operation(combat);
        else if(i==Q3_MOD_GIVE||i==Q3_MOD_CONSUME) o->rows[i].operation=qa_inventory_operation(inventory,i==Q3_MOD_GIVE?QA_INVENTORY_GIVE:QA_INVENTORY_CONSUME);
        else if(!qa_operation_create(sizeof(application_q3_mod_actor_request),sizeof(bool),&o->rows[i].operation,e)) {
            for(size_t j=Q3_MOD_THINK;j<i;++j) qa_operation_destroy(o->rows[j].operation,NULL);
            free(o); return false;
        }
        if(!o->rows[i].operation) { free(o); return fail(e,"Canonical gameplay owner has no real operation"); }
    }
    *out=o; return true;
}
bool application_q3_mod_operations_idle(const application_q3_mod_operations *o)
{
    for(size_t i=0;o&&i<Q3_MOD_OPERATION_COUNT;++i)
        if(!qa_operation_destroy_validate(o->rows[i].operation,NULL)) return false;
    return true;
}
bool application_q3_mod_operations_destroy(application_q3_mod_operations **in,qa_error *e)
{
    if(!in||!*in) return true;
    application_q3_mod_operations *o=*in;
    if(!application_q3_mod_operations_idle(o)) return fail(e,"Mod operations retain a canonical invocation");
    for(size_t i=Q3_MOD_THINK;i<Q3_MOD_OPERATION_COUNT;++i) if(!qa_operation_destroy_validate(o->rows[i].operation,e)) return false;
    for(size_t i=Q3_MOD_THINK;i<Q3_MOD_OPERATION_COUNT;++i) if(!qa_operation_destroy(o->rows[i].operation,e)) return false;
    free(o); *in=NULL; return true;
}
bool application_q3_mod_operations_read(application_q3_mod_operations *o,
    application_q3_mod_operation_services out[Q3_MOD_OPERATION_COUNT],qa_error *e)
{
    if(!o||!out) return fail(e,"Mod operation inventory requires its actual canonical owner");
    for(size_t i=0;i<Q3_MOD_OPERATION_COUNT;++i) out[i]=(application_q3_mod_operation_services){
        .operation=o->rows[i].operation,.context=o->rows+i,.inputs=inputs,
        .transform=i<=Q3_MOD_CONSUME?transform:NULL,.replace=i>=Q3_MOD_THINK?replace:NULL};
    return true;
}
typedef struct actor_body { application_q3_mod_actor_body call; void *context; } actor_body;
static bool canonical(void *context,const void *request,void *result,qa_error *e)
{ actor_body *b=context; return b->call(b->context,request,result,e); }
bool application_q3_mod_actor_dispatch(application_q3_mod_operations *o,application_q3_mod_operation kind,
    const application_q3_mod_actor_request *request,application_q3_mod_actor_body call,void *context,bool *result,qa_error *e)
{
    if(!o||kind<Q3_MOD_THINK||kind>=Q3_MOD_OPERATION_COUNT||!request||!call||!result||
        !qa_actors_get(qa_session_actors(o->session),request->self))
        return fail(e,"Actor mod operation requires its actual live source callback");
    actor_body body={call,context}; bool value=false;
    if(!qa_operation_dispatch(o->rows[kind].operation,request,&value,canonical,&body,NULL,NULL,e)) return false;
    *result=value; return true;
}
