#include "guest_q3_mod_private.h"
#include "qa/text.h"

typedef struct store_delivery {
    struct store_delivery *next;
    uint64_t sequence;
    qa_protection_store store;
} store_delivery;
typedef struct watched_channel {
    mod_actor_channel *channel;
    uint32_t count_address, selection_address;
} watched_channel;
struct mod_protection_stage {
    mod_protection_stage *previous;
    application_q3_mod *owner;
    qa_actor_id actor;
    qa_protection_observer *observer;
    qa_pickup_execution *pickup;
    watched_channel channels[2];
    size_t count;
    qa_armor before;
    qa_qvm_binding watch;
    store_delivery *deliveries;
    bool running;
};
static bool require(mod_actor_channel *c, qa_error *e)
{
    int32_t slot;
    return q3mod_storage_current(c->owner,e) && qa_actors_get(qa_session_actors(c->owner->session),c->actor) &&
        c->owner->services.client_slot(c->owner->services.context,c->actor,&slot,e) && slot==c->client &&
        (c->owner->restoring ? c->bound :
            c->owner->services.live_client(c->owner->services.context,c->actor)&&qa_combat_protection_current(c->owner->combat,c->lease)) ? true :
        q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection reservation left its actual client or canonical lease");
}
static bool read_scalar(application_q3_mod *o, uint32_t address, mod_scalar encoding, double *out, qa_error *e)
{
    uint8_t bytes[4]; if (!qa_qvm_read(o->vm,address,bytes,4,e)) return false;
    double value=encoding==MOD_INT32?(double)qa_load_i32le(bytes):qa_load_f32le(bytes);
    if (!isfinite(value)) return q3mod_fail(e,QA_ERROR_FORMAT,"Original protection storage is nonfinite");
    *out=value; return true;
}
static bool read_at(mod_actor_channel *c, uint32_t count_address, uint32_t selection_address,
    qa_armor *out, qa_error *e)
{
    application_q3_mod *o=c->owner; const mod_protection *d=o->profile->protection+c->definition;
    uint32_t selected=d->item; double count=0;
    if (d->has_selection) {
        uint8_t bytes[4];
        if (!qa_qvm_read(o->vm,selection_address,bytes,sizeof(bytes),e)) return false;
        double value=d->selection.field.encoding==MOD_INT32?
            (double)qa_load_i32le(bytes):qa_load_f32le(bytes);
        if (d->selection.masked) {
            uint32_t bits=d->selection.field.encoding==MOD_INT32?
                (uint32_t)qa_load_i32le(bytes):(uint32_t)qa_source_float_to_i32(qa_load_f32le(bytes));
            value=bits&d->selection.mask;
        }
        size_t i=0; while (i<d->selection.count && d->selection.values[i].value!=value) ++i;
        if (i==d->selection.count) return q3mod_fail(e,QA_ERROR_FORMAT,"Original protection selection is undeclared");
        selected=d->selection.values[i].selected;
    }
    if ((d->channel==QA_PROTECTION_REGULAR || selected!=QA_POWER_NONE) &&
        (!read_scalar(o,count_address,d->count.encoding,&count,e) || count<0))
        return q3mod_fail(e,QA_ERROR_FORMAT,"Original protection count is invalid");
    if (d->channel==QA_PROTECTION_REGULAR)
        out->regular=(qa_regular_armor){.kind=QA_ARMOR_SOURCE,.points=count,.item=selected};
    else out->powered=(qa_powered_armor){.kind=(qa_power_kind)selected,
        .cells=selected==QA_POWER_NONE?0:count,
        .source_owner=selected==QA_POWER_NONE?0:o->owner,
        .source_kind=selected==QA_POWER_NONE?QA_POWER_SOURCE_Q2:QA_POWER_SOURCE_GENERIC};
    return true;
}
static bool addresses(mod_actor_channel *c, uint32_t *count, uint32_t *selection, qa_error *e)
{
    const mod_protection *d=c->owner->profile->protection+c->definition;
    *selection=0;
    return require(c,e) && q3mod_address(c->owner,c->actor,d->count.record,d->count.offset,4,count,e) &&
        (!d->has_selection || q3mod_address(c->owner,c->actor,d->selection.field.record,d->selection.field.offset,4,selection,e));
}
bool q3mod_protection_read(mod_actor_channel *c, qa_armor *out, qa_error *e)
{
    uint32_t count, selection;
    return out && addresses(c,&count,&selection,e) && read_at(c,count,selection,out,e);
}
static bool read_binding(void *context, qa_armor *out, qa_error *e)
{ return q3mod_protection_read(context,out,e); }
static bool validate_write(void *context, const qa_armor *next, qa_error *e)
{
    mod_actor_channel *c=context; const mod_protection *d=c->owner->profile->protection+c->definition;
    qa_armor current={0}; int32_t word;
    if (!next || !q3mod_protection_read(c,&current,e)) return false;
    double count;
    if (d->channel==QA_PROTECTION_REGULAR) {
        if (next->regular.kind!=QA_ARMOR_SOURCE || current.regular.item!=next->regular.item)
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Regular protection selection requires its original source operation");
        count=next->regular.points;
    } else {
        if (next->powered.kind!=current.powered.kind || next->powered.source_kind!=current.powered.source_kind ||
            next->powered.source_owner!=current.powered.source_owner || next->powered.source_edition!=current.powered.source_edition)
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Powered protection selection requires its original source operation");
        if (next->powered.kind==QA_POWER_NONE) return true;
        count=next->powered.cells;
    }
    if (count<0 || (d->count.encoding==MOD_INT32 && trunc(count)!=count))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection count must fit its original storage exactly");
    return q3mod_scalar_word(count,d->count.encoding,&word,e);
}
static bool write_binding(void *context, const qa_armor *next, qa_error *e)
{
    mod_actor_channel *c=context; const mod_protection *d=c->owner->profile->protection+c->definition;
    if (!q3mod_current(c->owner,e) || !validate_write(c,next,e)) return false;
    if (d->channel==QA_PROTECTION_POWERED && next->powered.kind==QA_POWER_NONE) return true;
    int32_t word; uint32_t address, selection; uint8_t bytes[4];
    if (!addresses(c,&address,&selection,e) || !q3mod_scalar_word(d->channel==QA_PROTECTION_REGULAR?
        next->regular.points:next->powered.cells,d->count.encoding,&word,e)) return false;
    qa_store_u32le(bytes,(uint32_t)word);
    return qa_qvm_write(c->owner->vm,address,(qa_bytes){bytes,4},e);
}
static void scalar(application_q3_mod_inputs *in, application_q3_mod_input name, double value)
{ in->values[name]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=value}; }
static bool scale_declared(const mod_call *call)
{
    for (size_t i=0;i<call->argument_count;++i) if (call->arguments[i].kind==MOD_SCALAR &&
        call->arguments[i].input && call->arguments[i].name==Q3_MOD_PROTECTION_SCALE) return true;
    for (size_t i=0;i<call->global_count;++i) if (call->globals[i].value.kind==MOD_SCALAR &&
        call->globals[i].value.input && call->globals[i].value.name==Q3_MOD_PROTECTION_SCALE) return true;
    return false;
}
static bool absorb(void *context, const qa_damage_request *request, const qa_damage_geometry *geometry,
    float amount, qa_damage_flags flags, qa_protection_observer *observer, float *saved, qa_error *e)
{
    mod_actor_channel *c=context; application_q3_mod *o=c->owner;
    const mod_protection *d=o->profile->protection+c->definition;
    if (!request || !geometry || !observer || !saved || !qa_actor_id_equal(request->target,c->actor) || !require(c,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection absorption requires its actual target and observer");
    if (d->channel==QA_PROTECTION_REGULAR && flags.regular_scale!=1 && !scale_declared(&d->absorb))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Regular scale has no declared original source lowering");
    double seconds; if (!o->services.time(o->services.context,&seconds,e) || !isfinite(seconds)) return false;
    application_q3_mod_inputs in={0};
    in.values[Q3_MOD_SELF]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=c->actor};
    in.values[Q3_MOD_ATTACKER]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=request->attack.attacker};
    in.values[Q3_MOD_INFLICTOR]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=request->attack.inflictor};
    in.values[Q3_MOD_POINT]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_VECTOR,.as.vector=geometry->point};
    in.values[Q3_MOD_DIRECTION]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_VECTOR,.as.vector=geometry->direction};
    in.values[Q3_MOD_NORMAL]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_VECTOR,.as.vector=geometry->normal};
    uint32_t mask=(flags.no_armor?d->no_armor:0)|(flags.no_power_armor?d->no_power:0)|
        (flags.no_regular_armor?d->no_regular:0)|(flags.energy?d->energy:0)|(request->radius?d->radius:0);
    scalar(&in,Q3_MOD_AMOUNT,amount); scalar(&in,Q3_MOD_KNOCKBACK,request->knockback);
    scalar(&in,Q3_MOD_DAMAGE_FLAGS,mask); scalar(&in,Q3_MOD_PROTECTION_SCALE,flags.regular_scale);
    scalar(&in,Q3_MOD_TIME,seconds); double result;
    if (!q3mod_protection_observe(o,c->actor,observer,&d->absorb,&in,&result,e)) return false;
    if (result<0 || result>FLT_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Original absorption returned invalid savings");
    *saved=(float)result; return true;
}
bool application_q3_mod_protection_binding(application_q3_mod *o, qa_protection_lease lease,
    qa_protection_binding *out, qa_error *e)
{
    if (!o || !out) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection binding requires its owner");
    for (mod_actor_channel *c=o->channels;c;c=c->next) if (c->lease.serial==lease.serial &&
        c->lease.channel==lease.channel && qa_actor_id_equal(c->actor,lease.actor)) {
        *out=(qa_protection_binding){.context=c,.read=read_binding,.validate_write=validate_write,
            .write=write_binding,.absorb=absorb}; return true;
    }
    return q3mod_fail(e,QA_ERROR_NOT_FOUND,"Protection lease does not belong to this actual source owner");
}
bool application_q3_mod_protection_saved_binding(application_q3_mod *o,qa_actor_id actor,
    qa_protection_channel channel,const qa_protection_claim *claim,qa_protection_binding *out,qa_error *e)
{
    if(!o||!claim||!out||!q3mod_storage_current(o,e)||
        !qa_actors_get(qa_session_actors(o->session),actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Saved protection requires its retained source and full actor");
    for(mod_actor_channel *c=o->channels;c;c=c->next) {
        if(!qa_actor_id_equal(c->actor,actor)||c->lease.channel!=channel) continue;
        if(c->definition>=o->profile->protection_count)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Saved protection has no actual declared channel");
        const mod_protection *definition=o->profile->protection+c->definition;
        qa_protection_claim actual=definition->claim; actual.owner=o->owner;
        int32_t slot;
        if(!c->bound||!c->lease.serial||definition->channel!=channel||
            claim->owner!=actual.owner||claim->expected_owner!=actual.expected_owner||
            claim->rule!=actual.rule||claim->admission!=actual.admission||
            !o->services.client_slot(o->services.context,actor,&slot,e)||slot!=c->client)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Saved protection claim differs from its actual retained source lease");
        return application_q3_mod_protection_binding(o,c->lease,out,e);
    }
    return q3mod_fail(e,QA_ERROR_NOT_FOUND,"Saved protection has no bound source channel row");
}
bool application_q3_mod_reserve(application_q3_mod *o, qa_actor_id actor, qa_error *e)
{
    if (!q3mod_current(o,e)) return false;
    if (!o->profile->protection_count) return true;
    int32_t client;
    if (!qa_actors_get(qa_session_actors(o->session),actor) || !o->services.client_slot(o->services.context,actor,&client,e)) return false;
    if (client<0 || (uint32_t)client>=o->profile->maximum)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection reservation leaves its actual declared source client roster");
    for (size_t i=0;i<o->profile->protection_count;++i) {
        mod_actor_channel *found=NULL;
        for (mod_actor_channel *c=o->channels;c;c=c->next) if (c->definition==i && qa_actor_id_equal(c->actor,actor)) found=c;
        if (found) { if (found->client!=client || !qa_combat_protection_current(o->combat,found->lease)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Existing protection reservation changed client"); continue; }
        mod_actor_channel *c=calloc(1,sizeof(*c));
        if (!c) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original protection reservation");
        c->owner=o; c->actor=actor; c->client=client; c->definition=i;
        qa_protection_claim claim=o->profile->protection[i].claim; claim.owner=o->owner;
        if (!qa_combat_reserve_protection(o->combat,actor,o->profile->protection[i].channel,&claim,&c->lease,e)) { free(c); return false; }
        mod_actor_channel **tail=&o->channels; while (*tail) tail=&(*tail)->next; *tail=c;
    }
    return true;
}
bool application_q3_mod_admit(application_q3_mod *o, qa_actor_id actor, qa_error *e)
{
    if (!q3mod_current(o,e)) return false;
    if (!o->profile->protection_count) return q3mod_pickups_admit(o,actor,e);
    if (!o->active || !o->services.live_client(o->services.context,actor)) return true;
    for (mod_actor_channel *c=o->channels;c;c=c->next) if (qa_actor_id_equal(c->actor,actor) && !c->bound) {
        qa_armor current={0}; qa_protection_binding binding;
        if (!q3mod_protection_read(c,&current,e) || !application_q3_mod_protection_binding(o,c->lease,&binding,e) ||
            !qa_combat_bind_protection(o->combat,c->lease,&binding,e)) return false;
        c->bound=true;
    }
    return q3mod_pickups_admit(o,actor,e);
}
static bool stop(mod_protection_stage *stage, qa_error *e)
{
    if (!stage->watch) return true;
    if (!qa_qvm_unobserve_writes(stage->owner->vm,stage->watch,e)) return false;
    stage->watch=0; return true;
}
static void stage_free(mod_protection_stage *stage)
{
    while (stage->deliveries) {
        store_delivery *next=stage->deliveries->next;
        free(stage->deliveries); stage->deliveries=next;
    }
    free(stage);
}
bool q3mod_protection_stages_close(application_q3_mod *o, qa_error *e)
{
    for (mod_protection_stage *s=o->stages;s;s=s->previous) if (s->running)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection retains an executing source absorption");
    while (o->stages) {
        mod_protection_stage *s=o->stages;
        if (!stop(s,e)) return false;
        o->stages=s->previous; stage_free(s);
    }
    return true;
}
bool application_q3_mod_release_actor(application_q3_mod *o, qa_actor_id actor, qa_error *e)
{
    if (!o) return true;
    if(!q3mod_pickups_release(o,actor,e)) return false;
    for (mod_protection_stage *s=o->stages;s;s=s->previous) if (qa_actor_id_equal(s->actor,actor) && !stop(s,e)) return false;
    mod_actor_channel **link=&o->channels;
    while (*link) {
        mod_actor_channel *c=*link;
        if (!qa_actor_id_equal(c->actor,actor)) { link=&c->next; continue; }
        if (!qa_combat_close_protection(o->combat,c->lease,e)) return false;
        *link=c->next; free(c);
    }
    return true;
}
bool q3mod_protection_close(application_q3_mod *o, qa_error *e)
{
    o->active=false;
    while (o->channels) if (!application_q3_mod_release_actor(o,o->channels->actor,e)) return false;
    return true;
}
static bool publish(void *context, qa_qvm *vm, const qa_qvm_committed_write *event, qa_error *e)
{
    mod_protection_stage *s=context; application_q3_mod *o=s->owner; (void)vm;
    if (!s->running) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection write watch awaits checked retirement");
    if (!qa_actors_get(qa_session_actors(o->session),s->actor)) return true;
    qa_armor after=s->before;
    for (size_t i=0;i<s->count;++i) if (!read_at(s->channels[i].channel,
        s->channels[i].count_address,s->channels[i].selection_address,&after,e)) return false;
    qa_protection_store change={.before=s->before,.after=after}; s->before=after;
    for (size_t i=0;i<s->count;++i) {
        qa_protection_channel channel=o->profile->protection[s->channels[i].channel->definition].channel;
        if (channel==QA_PROTECTION_REGULAR) change.regular=!qa_regular_armor_equal(change.before.regular,after.regular);
        else change.powered=!qa_powered_armor_equal(change.before.powered,after.powered);
    }
    mod_protection_stage *inner=o->stages;
    while (inner && !qa_actor_id_equal(inner->actor,s->actor)) inner=inner->previous;
    if (inner!=s || (!change.regular && !change.powered)) return true;
    store_delivery *delivery=calloc(1,sizeof(*delivery));
    if (!delivery) return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining committed protection transition");
    delivery->sequence=event->sequence; delivery->store=change;
    store_delivery **tail=&s->deliveries; while (*tail) tail=&(*tail)->next; *tail=delivery;
    return true;
}
static bool after(void *context, qa_qvm *vm, const qa_qvm_committed_write *event, qa_error *e)
{
    mod_protection_stage *s=context; (void)vm;
    if (!s->running || (!s->observer&&!s->pickup))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection observer left its actual absorption scope");
    store_delivery **link=&s->deliveries;
    while (*link && (*link)->sequence!=event->sequence) link=&(*link)->next;
    if (!*link) return true;
    store_delivery *delivery=*link; *link=delivery->next;
    qa_protection_store change=delivery->store; free(delivery);
    return s->pickup?qa_pickup_store_protection(s->pickup,&change,e):qa_protection_observe(s->observer,&change,e);
}
static bool observe(application_q3_mod *o, qa_actor_id actor, qa_protection_observer *observer,qa_pickup_execution *pickup,
    const mod_call *call, const application_q3_mod_inputs *inputs, double *result, qa_error *e)
{
    mod_protection_stage *stage=calloc(1,sizeof(*stage));
    if (!stage) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning actual protection write continuation");
    *stage=(mod_protection_stage){.owner=o,.actor=actor,.observer=observer,.pickup=pickup,.previous=o->stages,.running=true};
    qa_qvm_write_range ranges[4]; size_t n=0;
    for (mod_actor_channel *c=o->channels;c;c=c->next) if (c->bound && qa_actor_id_equal(c->actor,actor)) {
        if (stage->count==2) { stage_free(stage); return q3mod_fail(e,QA_ERROR_ARGUMENT,"Protection owner duplicates a physical channel"); }
        watched_channel *w=stage->channels+stage->count++; w->channel=c;
        if (!addresses(c,&w->count_address,&w->selection_address,e) || !read_at(c,w->count_address,w->selection_address,&stage->before,e)) { stage_free(stage); return false; }
        ranges[n++]=(qa_qvm_write_range){w->count_address,4};
        if (o->profile->protection[c->definition].has_selection) ranges[n++]=(qa_qvm_write_range){w->selection_address,4};
    }
    o->stages=stage;
    bool ok=qa_qvm_observe_writes(o->vm,ranges,n,publish,after,stage,&stage->watch,e);
    if (ok) ok=q3mod_invoke(o,call,inputs,result,e);
    if (ok && qa_actors_get(qa_session_actors(o->session),actor))
        for (mod_actor_channel *c=o->channels;ok && c;c=c->next)
            if (qa_actor_id_equal(c->actor,actor)) ok=require(c,e);
    stage->running=false; stage->observer=NULL; stage->pickup=NULL;
    qa_error cleanup={0}; bool stopped=stop(stage,&cleanup);
    if (stopped) {
        mod_protection_stage **link=&o->stages;
        while (*link && *link!=stage) link=&(*link)->previous;
        if (*link) *link=stage->previous;
        stage_free(stage);
    } else o->failed_scope=true;
    if (!stopped && ok) { if (e) *e=cleanup; ok=false; }
    return ok;
}

bool q3mod_protection_observe(application_q3_mod *o,qa_actor_id actor,qa_protection_observer *observer,
    const mod_call *call,const application_q3_mod_inputs *inputs,double *result,qa_error *e)
{ return observe(o,actor,observer,NULL,call,inputs,result,e); }
bool q3mod_pickup_observe(application_q3_mod *o,qa_actor_id actor,qa_pickup_execution *pickup,
    const mod_call *call,const application_q3_mod_inputs *inputs,double *result,qa_error *e)
{ return observe(o,actor,NULL,pickup,call,inputs,result,e); }
