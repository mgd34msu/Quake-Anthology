#include "guest_q3_mod_private.h"

static bool inputs(mod_registered_callback *r, const void *request, const void *result,
    application_q3_mod_inputs *values, bool *eligible, qa_error *e)
{
    application_q3_mod *o=r->owner;
    const application_q3_mod_operation_services *s=o->services.operations+r->definition->operation;
    memset(values,0,sizeof(*values));
    if (!q3mod_current(o,e) || !s->inputs(s->context,request,result,values,e) || !q3mod_current(o,e)) return false;
    const application_q3_mod_value *self=values->values+Q3_MOD_SELF;
    if (self->kind!=Q3_MOD_VALUE_ACTOR) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Canonical operation omitted its actual target actor");
    *eligible=o->services.eligible_actor(o->services.context,self->as.actor);
    if (!*eligible) return true;
    if (values->values[Q3_MOD_TIME].kind==Q3_MOD_VALUE_ABSENT) {
        double seconds;
        if (!o->services.time(o->services.context,&seconds,e) || !isfinite(seconds) || !q3mod_current(o,e))
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Canonical operation has no finite actual source time");
        values->values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=seconds};
    }
    return true;
}
static bool transform(void *context, void *request, qa_error *e)
{
    mod_registered_callback *r=context; application_q3_mod_inputs values; bool eligible;
    if (!inputs(r,request,NULL,&values,&eligible,e)) return false;
    if (!eligible) return true;
    double value;
    if (!q3mod_invoke(r->owner,&r->definition->call,&values,&value,e)) return false;
    if (r->definition->call.returns==MOD_VOID) return q3mod_current(r->owner,e);
    application_q3_mod_operation_services *s=r->owner->services.operations+r->definition->operation;
    return s->transform(s->context,request,r->definition->knockback,value,e) && q3mod_current(r->owner,e);
}
static bool observe(void *context, const void *request, const void *result, qa_error *e)
{
    mod_registered_callback *r=context; application_q3_mod_inputs values; bool eligible; double ignored;
    return inputs(r,request,result,&values,&eligible,e) &&
        (!eligible || q3mod_invoke(r->owner,&r->definition->call,&values,&ignored,e));
}
static bool replace(void *context, const void *request, qa_operation_next next, void *result, qa_error *e)
{
    mod_registered_callback *r=context; application_q3_mod_inputs values; bool eligible;
    if (!inputs(r,request,NULL,&values,&eligible,e)) return false;
    if (!eligible) return qa_operation_continue(next,request,result,e);
    double value;
    if (!q3mod_invoke(r->owner,&r->definition->call,&values,&value,e)) return false;
    if (r->definition->call.returns==MOD_VOID) return qa_operation_continue(next,request,result,e);
    application_q3_mod_operation_services *s=r->owner->services.operations+r->definition->operation;
    return s->replace(s->context,result,value!=0,e) && q3mod_current(r->owner,e);
}
bool q3mod_callbacks_activate(application_q3_mod *o, qa_error *e)
{
    if (!q3mod_current(o,e)) return false;
    if (!o->callbacks && o->profile->callback_count) {
        o->callbacks=calloc(o->profile->callback_count,sizeof(*o->callbacks));
        if (!o->callbacks) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original callback continuation contexts");
    }
    for (size_t i=0;i<o->profile->callback_count;++i) {
        mod_registered_callback *r=o->callbacks+i;
        if (r->registration) continue;
        const mod_callback *d=o->profile->callbacks+i;
        const application_q3_mod_operation_services *s=o->services.operations+d->operation;
        if (!s->operation || !s->inputs || (d->stage==QA_OPERATION_TRANSFORM && !s->transform) ||
            (d->stage==QA_OPERATION_REPLACE && !s->replace))
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Declared callback requires its actual typed native operation");
        r->owner=o; r->definition=d;
        qa_operation_hook hook={.owner=o->owner,.name=d->id,.kind=d->stage,.context=r};
        if (d->stage==QA_OPERATION_TRANSFORM) hook.call.transform=transform;
        else if (d->stage==QA_OPERATION_OBSERVE) hook.call.observe=observe;
        else hook.call.replace=replace;
        if (!qa_operation_register(s->operation,&hook,&r->registration,e)) return false;
    }
    o->callbacks_active=true; return true;
}
bool q3mod_callbacks_close(application_q3_mod *o, qa_error *e)
{
    if (!o->callbacks) { o->callbacks_active=false; return true; }
    for (size_t i=0;i<o->profile->callback_count;++i) {
        mod_registered_callback *r=o->callbacks+i;
        if (!r->registration) continue;
        qa_operation *operation=o->services.operations[r->definition->operation].operation;
        if (!qa_operation_destroy_validate(operation,e)) return false;
    }
    for (size_t i=0;i<o->profile->callback_count;++i) {
        mod_registered_callback *r=o->callbacks+i;
        if (r->registration && !qa_operation_unregister(o->services.operations[r->definition->operation].operation,r->registration))
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original callback registration could not be retired");
        r->registration=0;
    }
    o->callbacks_active=false; return true;
}
