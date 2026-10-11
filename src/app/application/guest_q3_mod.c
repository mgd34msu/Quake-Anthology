#include "guest_q3_mod_private.h"
#include "qa/text.h"

bool q3mod_fail(qa_error *e, qa_status status, const char *text)
{ qa_error_set(e,status,0,"%s",text); return false; }
bool q3mod_current(application_q3_mod *o, qa_error *e)
{
    if (!o || o->closing || o->restoring || o->failed_scope || !o->services.current(o->services.context,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source operation has no current installed owner");
    return qa_qvm_get_role(o->vm)==QA_QVM_GAME && qa_qvm_get_abi(o->vm)==o->profile->abi &&
        (qa_qvm_image_of(o->vm) == o->profile->image) ? true :
        q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source operation left its actual executor");
}
bool q3mod_storage_current(application_q3_mod *o, qa_error *e)
{
    if (!o || !o->services.storage_current(o->services.context,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source storage has no retained physical owner");
    return qa_qvm_get_role(o->vm)==QA_QVM_GAME && qa_qvm_get_abi(o->vm)==o->profile->abi &&
        (qa_qvm_image_of(o->vm) == o->profile->image) ? true :
        q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic storage left its actual retained executor");
}
bool q3mod_address(application_q3_mod *o, qa_actor_id actor, size_t index,
    uint32_t relative, size_t length, uint32_t *out, qa_error *e)
{
    if (!q3mod_storage_current(o,e) || index >= o->profile->record_count || !out || !qa_actors_get(qa_session_actors(o->session),actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source storage requires its full live source actor");
    const mod_record *r=o->profile->records+index;
    if (!r || relative>r->stride || length>r->stride-relative)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic field leaves its exact declared source record");
    uint32_t base;
    if (!o->services.pointer(o->services.context,actor,index,&base,e)) return false;
    uint64_t limit=(uint64_t)r->address+(uint64_t)r->stride*r->capacity;
    if (base<r->address || base>=limit || (base-r->address)%r->stride ||
        (uint64_t)base+relative>UINT32_MAX ||
        !qa_qvm_qualify_source_span(o->profile->image,base+relative,length,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic actor locator returned another physical source array");
    if (!qa_actors_get(qa_session_actors(o->session),actor) || !q3mod_storage_current(o,e)) return false;
    *out=base+relative; return true;
}
bool q3mod_scalar_word(double value, mod_scalar encoding, int32_t *out, qa_error *e)
{
    if (!isfinite(value) || !out) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source scalar is nonfinite");
    if (encoding==MOD_INT32) {
        if (value<INT32_MIN || value>INT32_MAX)
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source scalar exceeds signed int32 storage");
        *out=(int32_t)trunc(value); return true;
    }
    float f=(float)value;
    if (encoding!=MOD_FLOAT32 || !isfinite(f)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source scalar exceeds binary32 storage");
    memcpy(out,&f,4); return true;
}
typedef struct call_run {
    application_q3_mod *owner;
    const mod_call *call;
    const application_q3_mod_inputs *inputs;
    uint32_t scratch, cursor;
    size_t length;
    int32_t result;
    bool *started;
} call_run;
static bool resolve(call_run *r, const mod_argument *a, application_q3_mod_value *v, qa_error *e)
{
    if (a->input || a->kind==MOD_ACTOR || a->kind==MOD_CLIENT || a->kind==MOD_TIME) *v=r->inputs->values[a->name];
    else *v=a->literal;
    application_q3_mod_value_kind kind=a->kind==MOD_ACTOR || a->kind==MOD_CLIENT?Q3_MOD_VALUE_ACTOR:
        a->kind==MOD_VECTOR?Q3_MOD_VALUE_VECTOR:a->kind==MOD_STRING?Q3_MOD_VALUE_STRING:Q3_MOD_VALUE_SCALAR;
    return v->kind==kind || q3mod_fail(e,QA_ERROR_ARGUMENT,"Source callback is missing its typed input");
}
static bool scratch_bytes(call_run *r, const void *bytes, size_t n, uint32_t *address, qa_error *e)
{
    size_t aligned=n>SIZE_MAX-3?SIZE_MAX:(n+3)&~(size_t)3;
    if (aligned>r->length-r->cursor || (uint64_t)r->scratch+r->cursor>INT32_MAX)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source argument leaves its genuine scratch reservation");
    *address=r->scratch+r->cursor;
    if (!qa_qvm_write(r->owner->vm,*address,(qa_bytes){bytes,n},e)) return false;
    r->cursor+=(uint32_t)aligned; return true;
}
static bool string_units(const char *text, size_t *units, qa_error *e)
{
    if (!text || !units) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source string input is absent");
    qa_bytes input={(const uint8_t *)text,strlen(text)};
    if (!qa_utf8_valid(input)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Typed source string is not UTF-8");
    size_t cursor=0,count=0; uint32_t scalar;
    while (qa_utf8_next(input,&cursor,&scalar)) {
        size_t width=scalar>0xffff?2:1;
        if (count>SIZE_MAX-width) return q3mod_fail(e,QA_ERROR_MEMORY,"Source string extent overflows");
        count+=width;
    }
    if (count==SIZE_MAX) return q3mod_fail(e,QA_ERROR_MEMORY,"Source string extent overflows");
    *units=count; return true;
}
static bool scratch_string(call_run *r, const char *text, uint32_t *address, qa_error *e)
{
    size_t units;
    if (!string_units(text,&units,e)) return false;
    uint8_t *bytes=calloc(units+1,1);
    if (!bytes) return q3mod_fail(e,QA_ERROR_MEMORY,"Lowering original source string bytes");
    qa_bytes input={(const uint8_t *)text,strlen(text)};
    size_t cursor=0,index=0; uint32_t scalar;
    while (qa_utf8_next(input,&cursor,&scalar)) {
        /* Q_strncpyz uses charCodeAt(i)&255, including each surrogate word.
         * A zero low byte terminates the copy and pads its remaining capacity. */
        if (scalar>0xffff) {
            scalar-=0x10000;
            uint8_t high=(uint8_t)(0xd800+(scalar>>10));
            if (!high) break;
            bytes[index++]=high;
            scalar=0xdc00+(scalar&0x3ff);
        }
        uint8_t low=(uint8_t)scalar;
        if (!low) break;
        bytes[index++]=low;
    }
    bool ok=scratch_bytes(r,bytes,units+1,address,e);
    free(bytes); return ok;
}
static bool lower(call_run *r, const mod_argument *a, int32_t *word, qa_error *e)
{
    application_q3_mod *o=r->owner;
    if (a->kind==MOD_ADDRESS) { memcpy(word,&a->address,4); return true; }
    application_q3_mod_value v;
    if (!resolve(r,a,&v,e)) return false;
    if (a->kind==MOD_ACTOR) {
        if (!v.as.actor.registry) { *word=0; return true; }
        uint32_t at;
        if (!q3mod_address(o,v.as.actor,a->record,0,0,&at,e)) return false;
        memcpy(word,&at,4); return true;
    }
    if (a->kind==MOD_CLIENT) {
        uint32_t entity;
        if (!o->profile->clients || !v.as.actor.registry ||
            !q3mod_address(o,v.as.actor,o->profile->entity_record,0,0,&entity,e) ||
            !o->services.client_slot(o->services.context,v.as.actor,word,e) || !q3mod_current(o,e)) return false;
        return *word>=0 && (uint32_t)*word<o->profile->maximum ? true :
            q3mod_fail(e,QA_ERROR_ARGUMENT,"Source client argument leaves its actual reserved rows");
    }
    if (a->kind==MOD_SCALAR || a->kind==MOD_TIME)
        return q3mod_scalar_word(v.as.scalar*(a->kind==MOD_TIME && a->milliseconds?1000:1),a->encoding,word,e);
    uint32_t at;
    if (a->kind==MOD_VECTOR) {
        if (!qa_vec_finite(v.as.vector)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source vector is nonfinite");
        uint8_t bytes[12]; float values[3]={v.as.vector.x,v.as.vector.y,v.as.vector.z};
        for (size_t i=0;i<3;++i) { uint32_t bits; memcpy(&bits,values+i,4); qa_store_u32le(bytes+i*4,bits); }
        if (!scratch_bytes(r,bytes,12,&at,e)) return false;
    } else {
        if (!scratch_string(r,v.as.string,&at,e)) return false;
    }
    memcpy(word,&at,4); return true;
}
static bool source_finish(application_q3_mod *o, mod_source_lease *source, qa_error *e)
{
    bool ok=true;
    if (source->scope) ok=o->services.source_leave(o->services.context,&source->scope,
        source->succeeded,source->result,e);
    if (source->scope) {
        if (ok) q3mod_fail(e,QA_ERROR_ARGUMENT,"Source projection cleanup retains its actual continuation");
        return false;
    }
    qa_error first=e?*e:(qa_error){0},cleanup={0};
    bool returned=qa_qvm_source_words_end(&source->globals,true,&cleanup);
    if (!ok) { if (e) *e=first; return false; }
    if (!returned) { if (e) *e=cleanup; return false; }
    return true;
}
static bool run(void *context, qa_qvm *vm, uint32_t scratch, qa_error *e)
{
    call_run *r=context; r->scratch=scratch;
    int32_t words[62]; size_t count=0;
    for (size_t i=0;i<r->call->global_count;++i) {
        size_t width=r->call->globals[i].value.kind==MOD_VECTOR?3:1;
        if (count>SIZE_MAX-width) return q3mod_fail(e,QA_ERROR_MEMORY,"Source global projection overflows");
        count+=width;
    }
    if (count>SIZE_MAX/sizeof(qa_qvm_source_word)) return q3mod_fail(e,QA_ERROR_MEMORY,"Source global projection overflows");
    qa_qvm_source_word *globals=count?calloc(count,sizeof(*globals)):NULL;
    if (count && !globals) return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining scoped source global words");
    bool ok=true; size_t n=0;
    for (size_t i=0;ok && i<r->call->argument_count;++i) ok=lower(r,r->call->arguments+i,words+i,e);
    for (size_t i=0;ok && i<r->call->global_count;++i) {
        const mod_global *g=r->call->globals+i; int32_t value;
        ok=lower(r,&g->value,&value,e);
        if (!ok) break;
        if (g->value.kind==MOD_VECTOR) {
            uint8_t bytes[12]; ok=qa_qvm_read(vm,(uint32_t)value,bytes,12,e);
            for (size_t j=0;ok && j<3;++j) globals[n++]=(qa_qvm_source_word){g->address+(uint32_t)j*4,qa_load_i32le(bytes+j*4)};
        } else globals[n++]=(qa_qvm_source_word){g->address,value};
    }
    mod_source_lease *source=ok?calloc(1,sizeof(*source)):NULL;
    if (ok && !source) ok=q3mod_fail(e,QA_ERROR_MEMORY,"Owning actual source projection continuation");
    if (source) { source->next=r->owner->source_calls; r->owner->source_calls=source; }
    if (ok && n) ok=qa_qvm_source_words_begin(vm,r->owner->profile->image,globals,n,&source->globals,e);
    if (ok) ok=r->owner->services.source_enter(r->owner->services.context,r->call->entry,
        words,r->call->argument_count,&source->scope,e);
    if (ok && !source->scope) ok=q3mod_fail(e,QA_ERROR_ARGUMENT,"Source projection omitted its actual call scope");
    if (ok) ok=r->started?
        qa_qvm_invoke_started(vm,r->call->entry,words,r->call->argument_count,&r->result,r->started,e):
        qa_qvm_invoke(vm,r->call->entry,words,r->call->argument_count,&r->result,e);
    if (ok && r->call->returns==MOD_FLOAT32) {
        float value; memcpy(&value,&r->result,4);
        if (!isfinite(value)) ok=q3mod_fail(e,QA_ERROR_FORMAT,"Source callback returned a nonfinite scalar");
    }
    qa_error cleanup={0};
    bool left=true;
    if (source) {
        source->succeeded=ok; source->result=r->result;
        left=source_finish(r->owner,source,&cleanup);
        if (source->scope || source->globals) { r->owner->failed_scope=true; left=false; }
        else { mod_source_lease **link=&r->owner->source_calls;
            while (*link && *link!=source) link=&(*link)->next;
            if (*link) *link=source->next;
            free(source);
        }
    }
    if (!left && ok) { if (e) *e=cleanup; ok=false; }
    free(globals);
    return ok && q3mod_current(r->owner,e);
}
static bool invoke(application_q3_mod *o, const mod_call *call,
    const application_q3_mod_inputs *inputs, double *result, bool *started, qa_error *e)
{
    if(started) *started=false;
    if (!q3mod_current(o,e) || !call || call->profile!=o->profile || !inputs || !result || o->calls>=64)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source callback exceeds its admitted recursion scope");
    call_run r={.owner=o,.call=call,.inputs=inputs,.started=started};
    if (call->global_count>SIZE_MAX-call->argument_count)
        return q3mod_fail(e,QA_ERROR_MEMORY,"Source argument inventory overflows");
    size_t total=call->argument_count+call->global_count;
    for (size_t i=0;i<total;++i) {
        const mod_argument *a=i<call->argument_count?call->arguments+i:&call->globals[i-call->argument_count].value;
        if (a->kind!=MOD_VECTOR && a->kind!=MOD_STRING) continue;
        application_q3_mod_value v;
        if (!resolve(&r,a,&v,e)) return false;
        size_t bytes=12;
        if (a->kind==MOD_STRING) {
            size_t length;
            if (!string_units(v.as.string,&length,e)) return false;
            bytes=length+1;
        }
        if (bytes>SIZE_MAX-3 || ((bytes+3)&~(size_t)3)>SIZE_MAX-r.length)
            return q3mod_fail(e,QA_ERROR_MEMORY,"Source callback scratch overflows");
        r.length+=(bytes+3)&~(size_t)3;
    }
    ++o->calls;
    bool ok=o->services.source_prepare(o->services.context,e) && q3mod_current(o,e);
    if (ok) ok=r.length?qa_qvm_source_scratch_run_reserved(o->vm,o->profile->image,r.length,65536,run,&r,e):run(&r,o->vm,0,e);
    --o->calls;
    if (!ok) return false;
    if (call->returns==MOD_VOID) *result=0;
    else if (call->returns==MOD_INT32) *result=r.result;
    else { float f; memcpy(&f,&r.result,4); if (!isfinite(f)) return q3mod_fail(e,QA_ERROR_FORMAT,"Source callback returned nonfinite savings"); *result=f; }
    return true;
}
bool q3mod_invoke(application_q3_mod *o,const mod_call *call,
    const application_q3_mod_inputs *inputs,double *result,qa_error *e)
{ return invoke(o,call,inputs,result,NULL,e); }
bool q3mod_invoke_started(application_q3_mod *o,const mod_call *call,
    const application_q3_mod_inputs *inputs,double *result,bool *started,qa_error *e)
{
    if(!started) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source invocation requires its actual entry receipt");
    return invoke(o,call,inputs,result,started,e);
}
bool application_q3_mod_create(application_q3_mod_profile *p, qa_qvm *vm, qa_session *session,
    qa_actor_owner owner, qa_combat *combat, const application_q3_mod_services *s,
    bool restoring, application_q3_mod **out, qa_error *e)
{
    if (!p || !vm || !session || !owner || !out || *out || !s || !s->current || !s->storage_current || !s->pointer ||
        !s->eligible_actor || (p->clients && (!s->live_client || !s->client_slot || !s->player_state)) || !s->time ||
        !s->source_prepare || !s->source_enter || !s->source_leave ||
        (p->pickup_count && !s->pickups) || (p->protection_count && !combat) || qa_qvm_get_role(vm)!=QA_QVM_GAME ||
        qa_qvm_get_abi(vm)!=p->abi || (qa_qvm_image_of(vm) != p->image))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic runtime requires its exact artifact and genuine source services");
    application_q3_mod *o=calloc(1,sizeof(*o));
    if (!o) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning generic source runtime");
    o->profile=p; o->vm=vm; o->session=session; o->owner=owner; o->combat=combat; o->services=*s;
    o->restoring=restoring; o->restored_owner=restoring;
    *out=o; return true;
}
bool application_q3_mod_idle(const application_q3_mod *o)
{ return !o || (!o->application && !o->capture && !o->stages && !o->calls && !o->source_calls && !o->pickup_calls); }
bool application_q3_mod_destroy(application_q3_mod **in, qa_error *e)
{
    if (!in || !*in) return true;
    application_q3_mod *o=*in;
    if (o->application || o->capture || o->calls)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic runtime retains an actual source callback scope");
    o->closing=true;
    if (!q3mod_pickups_close(o,e) || !q3mod_protection_stages_close(o,e) || !q3mod_callbacks_close(o,e)) return false;
    while (o->source_calls) {
        mod_source_lease *source=o->source_calls;
        bool consumed=source_finish(o,source,e);
        if (source->scope || source->globals) return false;
        o->source_calls=source->next; free(source);
        if (!consumed) return false;
    }
    if (!q3mod_protection_close(o,e)) return false;
    free(o->callbacks); free(o); *in=NULL; return true;
}
size_t application_q3_mod_entry_count(const application_q3_mod *o) { return o?o->profile->entry_count:0; }
bool application_q3_mod_entry_instruction(const application_q3_mod *o, size_t i, uint32_t *out)
{ if (!o || !out || i>=o->profile->entry_count) return false; *out=o->profile->entries[i]; return true; }
size_t application_q3_mod_input_binding_count(const application_q3_mod *o) { return o?o->profile->input_count:0; }
bool application_q3_mod_input_binding(const application_q3_mod *o, size_t i, bool *slice, bool *before)
{
    if (!o || i>=o->profile->input_count || !slice || !before) return false;
    *slice=o->profile->inputs[i].slice; *before=o->profile->inputs[i].before; return true;
}
bool application_q3_mod_call_run(application_q3_mod *o, const application_q3_mod_call *call_value,
    const application_q3_mod_inputs *inputs, double *result, qa_error *e)
{ return q3mod_invoke(o,call_value,inputs,result,e); }
bool application_q3_mod_stage_run(application_q3_mod *o, application_q3_mod_stage stage,
    const application_q3_mod_inputs *inputs, qa_error *e)
{
    if (!o || (unsigned)stage>=Q3_MOD_STAGE_COUNT || !inputs || !q3mod_current(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Lifecycle stage requires its exact source owner");
    const mod_call_group *group=o->profile->stages+stage; double ignored;
    for (size_t i=0;i<group->count;++i) if (!q3mod_invoke(o,group->calls+i,inputs,&ignored,e)) return false;
    return true;
}

typedef struct pickup_run {
    call_run lowering;
    const mod_pickup *definition;
    const qa_pickup_offer *offer;
    qa_pickup_execution *execution;
    qa_pickup_outcome outcome;
} pickup_run;
static bool pickup_live(const pickup_run *r)
{
    application_q3_mod *o=r->lowering.owner;
    return o->active&&!o->closing&&qa_pickup_current(r->execution)&&
        qa_pickup_recipient_is(r->execution,r->offer->recipient)&&
        qa_actors_get(qa_session_actors(o->session),r->offer->pickup)&&
        o->services.live_client(o->services.context,r->offer->recipient);
}
static bool pickup_run_source(void *context,qa_qvm *vm,uint32_t scratch,qa_error *e)
{
    pickup_run *r=context; application_q3_mod *o=r->lowering.owner;
    const mod_pickup *d=r->definition; r->lowering.scratch=scratch;
    if(!o->services.source_prepare(o->services.context,e)||!q3mod_current(o,e)) return false;
    uint32_t recipient,pickup;
    if(o->profile->entity_record==SIZE_MAX||!q3mod_address(o,r->offer->recipient,o->profile->entity_record,0,0,&recipient,e)||
        !q3mod_address(o,r->offer->pickup,o->profile->entity_record,0,0,&pickup,e)) return false;
    (void)recipient; (void)pickup;
    qa_qvm_source_word *words=d->context_count?calloc(d->context_count,sizeof(*words)):NULL;
    if(d->context_count&&!words) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original offered pickup context");
    bool ok=true;
    for(size_t i=0;ok&&i<d->context_count;++i) {
        ok=q3mod_address(o,r->offer->pickup,d->context[i].record,d->context[i].offset,4,&words[i].offset,e)&&
            lower(&r->lowering,&d->context[i].value,&words[i].value,e);
    }
    mod_source_lease *held=ok?calloc(1,sizeof(*held)):NULL;
    if(ok&&!held) ok=q3mod_fail(e,QA_ERROR_MEMORY,"Retaining offered pickup projection continuation");
    if(held) { held->next=o->source_calls; o->source_calls=held; }
    if(ok&&d->context_count) ok=qa_qvm_source_words_begin(vm,o->profile->image,words,d->context_count,&held->globals,e);
    free(words);
    double result=0; r->outcome=QA_PICKUP_REFUSED;
    if(ok&&d->gated) ok=q3mod_pickup_observe(o,r->offer->recipient,r->execution,&d->gate,r->lowering.inputs,&result,e);
    if(ok&&(!d->gated||result!=0)&&pickup_live(r)) {
        ok=q3mod_pickup_observe(o,r->offer->recipient,r->execution,&d->grant,r->lowering.inputs,&result,e);
        if(ok&&(d->always||result!=0)) r->outcome=QA_PICKUP_ACCEPTED;
    }
    if(ok&&!pickup_live(r)) r->outcome=QA_PICKUP_STALE;
    if(held) {
        qa_error cleanup={0}; bool returned=source_finish(o,held,&cleanup);
        if(held->globals||held->scope) { o->failed_scope=true; returned=false; }
        else { mod_source_lease **link=&o->source_calls; while(*link&&*link!=held) link=&(*link)->next; if(*link) *link=held->next; free(held); }
        if(!returned&&ok) { if(e) *e=cleanup; ok=false; }
    }
    return ok;
}
bool q3mod_pickup_run(application_q3_mod *o,const mod_pickup *definition,const qa_pickup_offer *offer,
    qa_pickup_execution *execution,qa_pickup_outcome *out,qa_error *e)
{
    const qa_actor_record *pickup=o&&offer?qa_actors_get(qa_session_actors(o->session),offer->pickup):NULL;
    if(!o||!definition||!offer||!execution||!out||!q3mod_current(o,e)||!pickup||pickup->owner==o->owner||
        qa_actor_id_equal(offer->pickup,offer->recipient)||o->services.live_client(o->services.context,offer->pickup))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickup context requires its real foreign nonclient offered actor");
    application_q3_mod_inputs inputs={0};
    inputs.values[Q3_MOD_SELF]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=offer->recipient};
    inputs.values[Q3_MOD_OTHER]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=offer->pickup};
    inputs.values[Q3_MOD_ITEM]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_STRING,.as.string=qa_strings_cstr(qa_session_strings(o->session),offer->item)};
    inputs.values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=(double)offer->time_ns/1e9};
    inputs.values[Q3_MOD_PICKUP_COUNT]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=offer->override_count?offer->count:0};
    inputs.values[Q3_MOD_PICKUP_HAS_COUNT]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=offer->override_count?1:0};
    inputs.values[Q3_MOD_PICKUP_DROPPED]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=offer->dropped?1:0};
    pickup_run run_value={.lowering={.owner=o,.inputs=&inputs},.definition=definition,.offer=offer,.execution=execution};
    if(!pickup_live(&run_value)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Original pickup lost its actual recipient execution");
    for(size_t i=0;i<definition->context_count;++i) {
        const mod_argument *a=&definition->context[i].value;
        if(a->kind!=MOD_VECTOR&&a->kind!=MOD_STRING) continue;
        application_q3_mod_value v; size_t size=12;
        if(!resolve(&run_value.lowering,a,&v,e)) return false;
        if(a->kind==MOD_STRING) { if(!string_units(v.as.string,&size,e)||size==SIZE_MAX) return false; ++size; }
        if(size>SIZE_MAX-3||((size+3)&~(size_t)3)>SIZE_MAX-run_value.lowering.length) return false;
        run_value.lowering.length+=(size+3)&~(size_t)3;
    }
    ++o->pickup_calls;
    bool ok=run_value.lowering.length?qa_qvm_source_scratch_run_reserved(o->vm,o->profile->image,run_value.lowering.length,65536,pickup_run_source,&run_value,e):
        pickup_run_source(&run_value,o->vm,0,e);
    --o->pickup_calls;
    if(ok) *out=run_value.outcome;
    return ok;
}
