#include "guest_native_q2_private.h"
#include "native_q2_source_actors.h"
#include "native_q2_source_combat_state.h"
#include "native_q2_source_damage.h"
#include "native_q2_source_invocation.h"
#include "native_q2_client_stages.h"
#include "guest_native_q2_combat_state.h"
#include "guest_q3_mod_operations.h"
#include "qa/native_observe.h"
#include <math.h>

enum { NATIVE_ACTOR_THINK = 4, NATIVE_ACTOR_CALLBACKS = 5 };

typedef struct native_actor_entry {
    struct native_actor_entry *next;
    struct application_native_q2_source_actors *owner;
    qa_native_address address;
    qa_native_entry_observer *binding;
    application_q3_mod_operation operation;
} native_actor_entry;

typedef struct native_source_actor {
    struct native_source_actor *next;
    struct application_native_q2_source_actors *owner;
    qa_actor_id actor;
    uint32_t slot;
    uint64_t serial;
    bool retired;
    struct native_actor_watch {
        struct native_source_actor *actor;
        qa_native_write_observer *binding;
        qa_native_address address;
        size_t kind;
    } watches[NATIVE_ACTOR_CALLBACKS];
} native_source_actor;
typedef struct native_actor_touch {
    struct native_actor_touch *next;
    qa_native_host_source_touch *ticket;
    qa_native_address scratch;
} native_actor_touch;
struct application_native_q2_source_actors {
    struct application_native_q2 *engine;
    native_source_actor *actors;
    uint32_t velocity,ground;
    uint32_t offsets[NATIVE_ACTOR_CALLBACKS];
    bool present[NATIVE_ACTOR_CALLBACKS],rerelease,suspended;
    native_actor_entry *entries;
    native_actor_touch *touches;
    application_native_q2_source_combat_state *combat;
    struct application_native_q2_source_damage *damage;
    bool ready;
    unsigned calls;
};
static bool current(native_source_actor *,qa_error *);
static qa_native_instance *instance(struct application_native_q2_source_actors *o)
{return qa_native_host_instance(o->engine->provider->state.native.host);}
static bool scratch_close(struct application_native_q2_source_actors *o,native_actor_touch *call,qa_error *e)
{
    if(!qa_native_host_source_touch_close(&call->ticket,e))return false;
    if(call->scratch&&!qa_native_free(instance(o),call->scratch,e))return false;
    call->scratch=0;return true;
}
static native_source_actor *row_at(struct application_native_q2_source_actors *o,uint32_t slot)
{
    for(native_source_actor *r=o->actors;r;r=r->next)
        if(!r->retired&&r->slot==slot&&qa_actors_get(qa_session_actors(o->engine->provider->application->session),r->actor)) return r;
    return NULL;
}
static bool pointer_read(struct application_native_q2_source_actors *o,qa_native_address address,qa_native_address *out,qa_error *e)
{
    uint8_t raw[8];size_t bytes=qa_native_module_describe(o->engine->provider->state.native.module).image.target.pointer_bytes;
    if(!qa_native_read(instance(o),address,raw,bytes,e)) return false;
    *out=bytes==4?qa_load_u32le(raw):qa_load_u64le(raw);return true;
}
static bool nullable(struct application_native_q2_source_actors *o,const qa_native_value *value,qa_actor_id *actor,qa_error *e)
{
    *actor=(qa_actor_id){0};
    if(value->type!=QA_NATIVE_ADDRESS) return application_fail(e,QA_ERROR_FORMAT,"Declared native reaction requires an actor pointer");
    if(!value->as.address) return true;
    uint32_t slot;qa_native_slot_binding binding;
    if(!qa_native_entity_slot(instance(o),value->as.address,&slot,e)||!qa_native_slot(instance(o),slot,&binding,e)) return false;
    if(binding.kind==QA_NATIVE_SLOT_FREE||!qa_actors_get(qa_session_actors(o->engine->provider->application->session),binding.actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared native reaction names an unbound source actor");
    *actor=binding.actor;return true;
}
static bool eligible(struct application_native_q2_source_actors *o,qa_actor_id actor)
{
    if(!actor.registry)return true;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))return false;
    for(uint32_t i=1;i<257;++i)if(o->engine->clients[i].reserved&&qa_actor_id_equal(o->engine->clients[i].actor,actor))
        return !o->engine->clients[i].denied;
    return true;
}
static bool request_eligible(struct application_native_q2_source_actors *o,
    application_q3_mod_operation operation,const application_q3_mod_actor_request *r)
{
    if(!eligible(o,r->self))return false;
    if(operation==Q3_MOD_THINK)return true;
    if(operation==Q3_MOD_USE)return eligible(o,r->source.use.other)&&eligible(o,r->source.use.activator);
    if(operation==Q3_MOD_TOUCH)return eligible(o,r->source.touch.other);
    if(operation==Q3_MOD_PAIN)return eligible(o,r->source.pain.attacker);
    return eligible(o,r->source.die.attacker)&&eligible(o,r->source.die.inflictor);
}
typedef struct native_actor_call {
    native_actor_entry *entry;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
    qa_actor_id actor;
    const application_q3_mod_actor_request *request;
} native_actor_call;
static bool address_for(struct application_native_q2_source_actors *,qa_actor_id,qa_native_address *,qa_error *);
static bool modified_reaction(native_actor_call *,const application_q3_mod_actor_request *,bool *,qa_error *);
static bool original(void *context,const application_q3_mod_actor_request *request,bool *result,qa_error *e)
{
    native_actor_call *call=context;
    struct application_native_q2_source_actors *o=call->entry->owner;
    if(!qa_actor_id_equal(request->self,call->actor)) return application_fail(e,QA_ERROR_ARGUMENT,"Native reaction target cannot change inside its source continuation");
    if(!request_eligible(o,call->entry->operation,request)) {*result=false;return true;}
    if(request!=call->request&&call->entry->operation!=Q3_MOD_THINK){
        if(call->entry->operation==Q3_MOD_PAIN||call->entry->operation==Q3_MOD_DIE)
            return modified_reaction(call,request,result,e);
        qa_native_value arguments[4];memcpy(arguments,call->arguments,call->count*sizeof(*arguments));
        if(!address_for(o,call->entry->operation==Q3_MOD_USE?request->source.use.other:request->source.touch.other,&arguments[1].as.address,e)||
            (call->entry->operation==Q3_MOD_USE&&!address_for(o,request->source.use.activator,&arguments[2].as.address,e)))return false;
        bool cancelled=false;
        if(!application_native_q2_callbacks_source_before(o->engine,e)||
            !application_native_q2_source_invoke_original(o->engine,request->self,call->entry->binding,
                arguments,call->count,call->result,&cancelled,e))return false;
        *result=!cancelled;return true;
    }
    bool cancelled=false;
    if(!application_native_q2_callbacks_source_before(o->engine,e)||
        !application_native_q2_source_invoke_original(o->engine,request->self,call->entry->binding,
            call->arguments,call->count,call->result,&cancelled,e)) return false;
    *result=!cancelled;return true;
}
static bool dispatch_reaction(void *context,qa_session *session,qa_error *e)
{
    (void)session;native_actor_call *call=context;bool handled=false;
    return application_q3_mod_actor_dispatch(call->entry->owner->engine->provider->application->mod_operations,
        call->entry->operation,call->request,original,call,&handled,e);
}
static bool reaction(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *e)
{
    native_actor_entry *entry=context;struct application_native_q2_source_actors *o=entry->owner;
    if(o->suspended) return application_native_q2_source_original(o->engine,binding,arguments,count,result,e);
    if(native!=instance(o)||!count||arguments[0].type!=QA_NATIVE_ADDRESS)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared native reaction lost its source invocation");
    uint32_t slot;
    if(!qa_native_entity_slot(native,arguments[0].as.address,&slot,e)) return false;
    native_source_actor *r=row_at(o,slot);
    if(!r) return application_native_q2_source_original(o->engine,binding,arguments,count,result,e);
    size_t kind=entry->operation==Q3_MOD_THINK?NATIVE_ACTOR_THINK:(size_t)entry->operation-Q3_MOD_TOUCH;
    qa_native_address target;
    if(!pointer_read(o,arguments[0].as.address+o->offsets[kind],&target,e)) return false;
    if(target!=entry->address) return application_native_q2_source_original(o->engine,binding,arguments,count,result,e);
    if(!current(r,e)) return false;
    application_q3_mod_actor_request request={.self=r->actor};
    if(entry->operation==Q3_MOD_THINK) {
        qa_source_frame frame;
        if(count!=1||!application_native_q2_stages_time_read(o->engine,&frame,e))return false;
        request.source.think.time_ns=frame.time_ns;
        request.source.think.elapsed_ns=frame.elapsed_ns;
    } else if(entry->operation==Q3_MOD_USE) {
        if(count!=3||!nullable(o,arguments+1,&request.source.use.other,e)||
            !nullable(o,arguments+2,&request.source.use.activator,e)) return false;
    } else if(entry->operation==Q3_MOD_TOUCH) {
        if(count!=4||!nullable(o,arguments+1,&request.source.touch.other,e)) return false;
    } else if(entry->operation==Q3_MOD_PAIN) {
        if(count!=(o->rerelease?5u:4u)||arguments[2].type!=QA_NATIVE_F32||arguments[3].type!=QA_NATIVE_I32||
            !isfinite(arguments[2].as.f32)||!nullable(o,arguments+1,&request.source.pain.attacker,e))
            return application_fail(e,QA_ERROR_FORMAT,"Declared pain differs from its source reaction ABI");
        request.source.pain.kick=arguments[2].as.f32;request.source.pain.damage=(float)arguments[3].as.i32;
        if((double)request.source.pain.damage!=arguments[3].as.i32)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared pain exceeds exact canonical damage storage");
    } else {
        uint8_t point[12];
        if(count!=(o->rerelease?6u:5u)||arguments[3].type!=QA_NATIVE_I32||arguments[4].type!=QA_NATIVE_ADDRESS||
            !nullable(o,arguments+1,&request.source.die.inflictor,e)||
            !nullable(o,arguments+2,&request.source.die.attacker,e)||
            !qa_native_read(native,arguments[4].as.address,point,sizeof(point),e)) return false;
        request.source.die.damage=(float)arguments[3].as.i32;
        if((double)request.source.die.damage!=arguments[3].as.i32)
            return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared death exceeds exact canonical damage storage");
        request.source.die.point=qa_v3(qa_load_f32le(point),qa_load_f32le(point+4),qa_load_f32le(point+8));
        if(!qa_vec_finite(request.source.die.point)) return application_fail(e,QA_ERROR_FORMAT,"Declared death has a nonfinite source point");
    }
    if(!request_eligible(o,entry->operation,&request)){if(result)*result=(qa_native_value){.type=QA_NATIVE_VOID};return true;}
    if(entry->operation==Q3_MOD_PAIN||entry->operation==Q3_MOD_DIE){
        float knockback=0;
        qa_damage_result reaction={.reaction=entry->operation==Q3_MOD_DIE?QA_REACTION_DEATH:QA_REACTION_PAIN,
            .applied_damage=entry->operation==Q3_MOD_DIE?request.source.die.damage:request.source.pain.damage};
        if(!application_native_q2_source_damage_reaction(o->damage,r->actor,&reaction,
            &request.attack,&knockback,&request.has_attack,e))return false;
        if(!request.has_attack&&o->combat&&o->rerelease){
            size_t mod=entry->operation==Q3_MOD_DIE?5:4;
            qa_actor_id attacker=entry->operation==Q3_MOD_DIE?request.source.die.attacker:request.source.pain.attacker;
            qa_actor_id inflictor=entry->operation==Q3_MOD_DIE?request.source.die.inflictor:attacker;
            if(arguments[mod].type!=QA_NATIVE_ADDRESS||!application_native_q2_source_damage_reaction_attack(o->damage,
                r->actor,attacker,inflictor,arguments[mod].as.address,&request.attack,e))return false;
            request.has_attack=true;
        }
        if(request.has_attack&&entry->operation==Q3_MOD_DIE)request.source.die.kick=knockback;
    }
    native_actor_call call={entry,arguments,count,result,r->actor,&request};
    qa_invocation_kind invocation=entry->operation==Q3_MOD_THINK?QA_INVOKE_THINK:
        entry->operation==Q3_MOD_USE?QA_INVOKE_USE:entry->operation==Q3_MOD_TOUCH?QA_INVOKE_TOUCH:
        entry->operation==Q3_MOD_PAIN?QA_INVOKE_PAIN:QA_INVOKE_DIE;
    ++o->calls;++o->engine->calls;
    bool ok=qa_session_invoke(o->engine->provider->application->session,r->actor,invocation,dispatch_reaction,&call,e);
    --o->engine->calls;--o->calls;
    if(ok&&result) *result=(qa_native_value){.type=QA_NATIVE_VOID};
    return ok;
}
static bool ensure(struct application_native_q2_source_actors *o,uint32_t slot,size_t kind,qa_error *e)
{
    if(!o->present[kind]||o->suspended) return true;
    qa_native_address address,target;
    if(!qa_native_entity_address(instance(o),slot,&address,e)||!pointer_read(o,address+o->offsets[kind],&target,e)) return false;
    if(!target) return true;
    application_q3_mod_operation operation=kind==NATIVE_ACTOR_THINK?Q3_MOD_THINK:
        (application_q3_mod_operation)(Q3_MOD_TOUCH+kind);
    native_actor_entry *entry=o->entries;
    while(entry&&entry->address!=target) entry=entry->next;
    if(entry&&entry->operation!=operation) return application_fail(e,QA_ERROR_FORMAT,"Native reaction entry has incompatible declared kinds");
    if(entry&&entry->binding) return true;
    if(!entry) {
        entry=calloc(1,sizeof(*entry));
        if(!entry) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native reaction entry");
        entry->owner=o;entry->address=target;entry->operation=operation;entry->next=o->entries;o->entries=entry;
    }
    qa_native_type parameters[6];size_t count=operation==Q3_MOD_THINK?1:operation==Q3_MOD_USE?3:operation==Q3_MOD_TOUCH?4:
        operation==Q3_MOD_PAIN?(o->rerelease?5:4):(o->rerelease?6:5);
    for(size_t i=0;i<count;++i) parameters[i]=(qa_native_type){.kind=QA_NATIVE_ADDRESS,.count=1};
    if(operation==Q3_MOD_TOUCH&&o->rerelease) parameters[3].kind=QA_NATIVE_U8;
    if(operation==Q3_MOD_PAIN) {parameters[2].kind=QA_NATIVE_F32;parameters[3].kind=QA_NATIVE_I32;}
    if(operation==Q3_MOD_DIE) parameters[3].kind=QA_NATIVE_I32;
    qa_native_signature signature={.abi=qa_native_module_describe(o->engine->provider->state.native.module).image.target.abi,
        .parameters=parameters,.parameter_count=count,.result={.kind=QA_NATIVE_VOID,.count=1}};
    return qa_native_observe_entry(instance(o),target,&signature,reaction,entry,&entry->binding,e);
}
static bool changed(void *context,qa_native_instance *native,const qa_native_write_event *event,qa_error *e)
{
    struct native_actor_watch *watch=context;native_source_actor *r=watch->actor;
    struct application_native_q2_source_actors *o=r->owner;
    if(o->suspended||r->retired||!event->size) return true;
    if(native!=instance(o)||!current(r,e)) return false;
    ++o->calls;
    bool ok=ensure(o,r->slot,watch->kind,e);
    --o->calls;return ok;
}
static bool unwatch(native_source_actor *r,qa_error *e)
{
    for(size_t i=0;i<NATIVE_ACTOR_CALLBACKS;++i) {
        if(r->watches[i].binding&&!qa_native_unobserve_writes(r->watches[i].binding,e)) return false;
        r->watches[i].binding=NULL;
    }
    return true;
}
static bool watch(native_source_actor *r,qa_error *e)
{
    struct application_native_q2_source_actors *o=r->owner;
    if(o->suspended) return true;
    qa_native_address address;
    if(!qa_native_entity_address(instance(o),r->slot,&address,e)) return false;
    size_t pointer=qa_native_module_describe(o->engine->provider->state.native.module).image.target.pointer_bytes;
    for(size_t i=0;i<NATIVE_ACTOR_CALLBACKS;++i) if(o->present[i]) {
        struct native_actor_watch *w=r->watches+i;qa_native_address field=address+o->offsets[i];
        if(w->binding&&w->address==field) continue;
        if(w->binding&&!qa_native_unobserve_writes(w->binding,e)) return false;
        w->binding=NULL;w->actor=r;w->kind=i;w->address=field;
        if(!qa_native_observe_writes(instance(o),field,pointer,changed,w,&w->binding,e)) return false;
    }
    return true;
}
bool application_native_q2_source_actors_declared(const struct application_native_q2 *n)
{
    const qa_json_document *d=n?application_native_q2_callbacks_document(n->callbacks):NULL;
    return d&&qa_json_get(d,qa_json_root(d),"sourceActors")!=QA_JSON_NONE;
}
static bool current(native_source_actor *r,qa_error *e)
{
    struct application_native_q2_source_actors *o=r?r->owner:NULL;
    struct application_native_q2 *n=o?o->engine:NULL;
    const qa_actor_record *actor=n?qa_actors_get(qa_session_actors(n->provider->application->session),r->actor):NULL;
    qa_native_slot_binding slot;
    if(!n||n->source_actors!=o||n->provider->state.native.q2_engine!=n||!n->provider->state.native.host||
        r->retired||!actor||!actor->has_source||actor->owner!=n->provider->owner||actor->source_slot!=r->slot||
        !application_native_q2_callbacks_storage_current(n->callbacks,e)||
        !qa_native_slot(qa_native_host_instance(n->provider->state.native.host),r->slot,&slot,e)) return false;
    return (slot.kind==QA_NATIVE_SLOT_OWNED&&qa_actor_id_equal(slot.actor,r->actor))||
        application_fail(e,QA_ERROR_ARGUMENT,"Declared native body lost its exact source actor");
}
static bool body_write(void *context,const qa_body_state *body,qa_error *e)
{
    native_source_actor *r=context;
    if(!current(r,e)) return false;
    struct application_native_q2_source_actors *o=r->owner;
    ++o->calls;
    bool ok=qa_native_host_source_body_write(o->engine->provider->state.native.host,r->slot,o->velocity,o->ground,body,e);
    --o->calls;
    return ok&&current(r,e);
}
static bool import_damage(struct application_native_q2 *n,struct application_native_q2_source_actors *o,qa_error *e)
{
    qa_bytes saved=application_native_q2_stages_damage_saved(n);if(!saved.size)return true;
    if(!application_native_q2_source_damage_suspend(o->damage,e)||
        !application_native_q2_source_damage_restore(o->damage,saved,e))return false;
    o->suspended=true;application_native_q2_stages_damage_adopted(n);return true;
}
static bool prepare(struct application_native_q2 *n,qa_error *e)
{
    if(n->source_actors)return n->source_actors->ready?import_damage(n,n->source_actors,e):
        application_fail(e,QA_ERROR_ARGUMENT,"Declared SourceActor owner retains an incomplete layout construction");
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id fields=qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"fields");
    uint64_t velocity,ground,think,nextthink;qa_native_entity_table table;
    qa_native_module_info info=qa_native_module_describe(n->provider->state.native.module);
    if(!qa_json_u64(d,qa_json_get(d,fields,"velocity"),&velocity,e)||
        !qa_json_u64(d,qa_json_get(d,fields,"ground"),&ground,e)||
        !qa_json_u64(d,qa_json_get(d,fields,"think"),&think,e)||
        !qa_json_u64(d,qa_json_get(d,qa_json_get(d,fields,"nextthink"),"offset"),&nextthink,e)||
        !qa_native_entity_table_get(qa_native_host_instance(n->provider->state.native.host),&table,e)) return false;
    size_t pointer=info.image.target.pointer_bytes;
    const char *encodings[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    const size_t sizes[]={1,1,2,2,4,4,8,8,4,8};size_t nextthink_size=0;
    qa_json_id encoding=qa_json_get(d,qa_json_get(d,fields,"nextthink"),"encoding");
    for(size_t i=0;i<10;++i) if(qa_json_string_equal(d,encoding,encodings[i])) nextthink_size=sizes[i];
    if(velocity>UINT32_MAX||ground>UINT32_MAX||velocity>table.stride||12>table.stride-velocity||
        ground>table.stride||pointer>table.stride-ground||think>table.stride||pointer>table.stride-think||
        !nextthink_size||nextthink>table.stride||nextthink_size>table.stride-nextthink)
        return application_fail(e,QA_ERROR_FORMAT,"Declared native body fields exceed their actual source stride");
    qa_json_id callbacks=qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"callbacks");
    uint32_t offsets[NATIVE_ACTOR_CALLBACKS]={0};bool present[NATIVE_ACTOR_CALLBACKS]={0};
    const char *names[]={"touch","use","pain","die","think"};
    for(size_t i=0;i<NATIVE_ACTOR_CALLBACKS;++i) {
        qa_json_id field=qa_json_get(d,i==1||i==NATIVE_ACTOR_THINK?fields:callbacks,names[i]);uint64_t offset;
        if(field==QA_JSON_NONE||qa_json_type(d,field)==QA_JSON_NULL) continue;
        if(!qa_json_u64(d,field,&offset,e)||offset>UINT32_MAX||offset>table.stride||pointer>table.stride-offset)
            return application_fail(e,QA_ERROR_FORMAT,"Declared native reaction pointer exceeds its source stride");
        offsets[i]=(uint32_t)offset;present[i]=true;
    }
    struct application_native_q2_source_actors *o=calloc(1,sizeof(*o));
    if(!o) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native SourceActor bodies");
    o->engine=n;o->velocity=(uint32_t)velocity;o->ground=(uint32_t)ground;
    o->rerelease=qa_json_string_equal(d,qa_json_get(d,callbacks,"abi"),"q2-rerelease");
    memcpy(o->offsets,offsets,sizeof(offsets));memcpy(o->present,present,sizeof(present));n->source_actors=o;
    if(!application_native_q2_source_combat_state_create(n,&o->combat,e))return false;
    if(!application_native_q2_source_damage_create(n,o->combat,o->velocity,&o->damage,e))return false;
    o->ready=true;
    return import_damage(n,o,e);
}
typedef struct native_touch_execution {
    native_source_actor *actor;
    native_actor_entry *entry;
    qa_native_host_source_touch *ticket;
    qa_actor_id other;
    bool transformed,cancelled;
} native_touch_execution;
static bool touch_original(void *context,qa_error *e)
{
    native_touch_execution *call=context;
    qa_native_value arguments[4],result={.type=QA_NATIVE_VOID};
    return qa_native_host_source_touch_arguments(call->ticket,arguments,e)&&current(call->actor,e)&&
        (!call->transformed||address_for(call->actor->owner,call->other,&arguments[1].as.address,e))&&
        application_native_q2_callbacks_source_before(call->actor->owner->engine,e)&&
        application_native_q2_source_invoke_original(call->actor->owner->engine,call->actor->actor,
            call->entry->binding,arguments,4,&result,&call->cancelled,e);
}
static bool touch_mod_body(void *context,const application_q3_mod_actor_request *request,bool *handled,qa_error *e)
{
    native_touch_execution *call=context;
    if(!qa_actor_id_equal(call->actor->actor,request->self))return application_fail(e,QA_ERROR_ARGUMENT,"Native touch target changed inside its original continuation");
    if(!request_eligible(call->actor->owner,Q3_MOD_TOUCH,request)){*handled=false;return true;}
    call->other=request->source.touch.other;call->transformed=true;
    bool ok=application_native_q2_callbacks_transfer(call->actor->owner->engine->callbacks,touch_original,call,e);
    if(ok)*handled=true;
    return ok;
}
static bool touch_mod_dispatch(void *context,qa_session *session,qa_error *e)
{
    (void)session;native_touch_execution *call=context;bool handled;
    application_q3_mod_actor_request request={.self=call->actor->actor,.source.touch.other=call->other};
    return application_q3_mod_actor_dispatch(call->actor->owner->engine->provider->application->mod_operations,
        Q3_MOD_TOUCH,&request,touch_mod_body,call,&handled,e);
}
bool application_native_q2_source_actors_touch(struct application_native_q2 *n,
    const qa_touch_contact *contact,bool *handled,qa_error *e)
{
    if(!n||!contact||!handled) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native touch requires its source and contact");
    *handled=false;
    struct application_native_q2_source_actors *o=n->source_actors;
    if(!o||o->suspended||!o->present[0]) return true;
    if(!eligible(o,contact->self)||!eligible(o,contact->other)||
        (contact->has_source_trace&&!eligible(o,contact->source_trace.actor)))return true;
    native_source_actor *r=NULL;
    for(native_source_actor *row=o->actors;row;row=row->next)
        if(!row->retired&&qa_actor_id_equal(row->actor,contact->self)) {r=row;break;}
    if(!r) return true;
    if(!current(r,e)||!ensure(o,r->slot,0,e)) return false;
    qa_native_address self,target;
    if(!qa_native_entity_address(instance(o),r->slot,&self,e)||
        !pointer_read(o,self+o->offsets[0],&target,e)) return false;
    if(!target) return true;
    native_actor_entry *entry=o->entries;
    while(entry&&entry->address!=target) entry=entry->next;
    if(!entry||!entry->binding||entry->operation!=Q3_MOD_TOUCH)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared touch lost its retained original entry");
    native_actor_touch *call=calloc(1,sizeof(*call));
    if(!call) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native touch scratch");
    call->next=o->touches;o->touches=call;
    ++o->calls;++n->calls;
    bool ok=qa_native_host_source_touch_prepare(n->provider->state.native.host,o->rerelease,contact,&call->ticket,e);
    native_touch_execution execution={.actor=r,.entry=entry,.ticket=call->ticket,.other=contact->other};
    if(ok)ok=qa_session_invoke(n->provider->application->session,r->actor,QA_INVOKE_TOUCH,touch_mod_dispatch,&execution,e);
    --n->calls;--o->calls;
    if(ok) *handled=true;
    qa_error cleanup={0};
    if(scratch_close(o,call,&cleanup)) {
        native_actor_touch **link=&o->touches;
        while(*link!=call) link=&(*link)->next;
        *link=call->next;free(call);
    } else {if(ok&&e) *e=cleanup;ok=false;}
    return ok;
}
static bool address_for(struct application_native_q2_source_actors *o,qa_actor_id actor,
    qa_native_address *out,qa_error *e)
{
    *out=0;if(!actor.registry)return true;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native reaction references a retired actor");
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(o),&table,e))return false;
    for(uint32_t i=0;i<table.count;++i){qa_native_slot_binding slot;
        if(!qa_native_slot(instance(o),i,&slot,e))return false;
        if(slot.kind!=QA_NATIVE_SLOT_FREE&&qa_actor_id_equal(slot.actor,actor))
            return qa_native_entity_address(instance(o),i,out,e);}
    return application_fail(e,QA_ERROR_UNSUPPORTED,"Native reaction lacks its actual foreign actor projection");
}
typedef struct native_canonical_reaction {
    native_source_actor *actor;
    application_q3_mod_operation operation;
    const application_q3_mod_actor_request *request;
    native_actor_entry *entry;
    qa_native_value arguments[6];
    size_t count;
    bool captured,cancelled;
} native_canonical_reaction;
static bool canonical_reaction_original(void *context,qa_error *e)
{
    native_canonical_reaction *call=context;qa_native_value result={.type=QA_NATIVE_VOID};
    return current(call->actor,e)&&application_native_q2_callbacks_source_before(call->actor->owner->engine,e)&&
        application_native_q2_source_invoke_original(call->actor->owner->engine,call->actor->actor,
            call->entry->binding,call->arguments,call->count,&result,&call->cancelled,e);
}
static bool canonical_reaction_body(void *context,const application_q3_mod_actor_request *request,
    bool *handled,qa_error *e)
{
    native_canonical_reaction *call=context;native_source_actor *r=call->actor;
    struct application_native_q2_source_actors *o=r->owner;
    if(!qa_actor_id_equal(request->self,r->actor))return application_fail(e,QA_ERROR_ARGUMENT,"Native reaction target changed inside its canonical continuation");
    if(!request_eligible(o,call->operation,request)){*handled=false;return true;}
    if(!current(r,e))return false;
    bool death=call->operation==Q3_MOD_DIE;size_t kind=death?3:2;
    if(!call->captured&&!ensure(o,r->slot,kind,e))return false;
    qa_native_address self,target,attacker,inflictor=0;
    if(!qa_native_entity_address(instance(o),r->slot,&self,e))return false;
    native_actor_entry *entry=call->entry;
    if(!call->captured){if(!pointer_read(o,self+o->offsets[kind],&target,e))return false;
        if(!target){*handled=false;return true;}
        entry=o->entries;while(entry&&entry->address!=target)entry=entry->next;}
    if(!entry||!entry->binding||entry->operation!=call->operation)return application_fail(e,QA_ERROR_ARGUMENT,"Native reaction lost its exact held original entry");
    float damage=death?request->source.die.damage:request->source.pain.damage,
        kick=death?request->source.die.kick:request->source.pain.kick;
    if(!isfinite(damage)||!isfinite(kick)||(double)damage<INT32_MIN||(double)damage>INT32_MAX)
        return application_fail(e,QA_ERROR_FORMAT,"Native reaction damage exceeds its declared int32 ABI");
    if(!address_for(o,death?request->source.die.attacker:request->source.pain.attacker,&attacker,e)||
        (death&&!address_for(o,request->source.die.inflictor,&inflictor,e)))return false;
    native_actor_touch *scratch=calloc(1,sizeof(*scratch));
    if(!scratch)return application_fail(e,QA_ERROR_MEMORY,"Retaining native reaction ABI scratch");
    scratch->next=o->touches;o->touches=scratch;
    bool ok=true;uint8_t raw[15]={0};size_t bytes=death?12:0;
    if(death){qa_vec3 point=request->source.die.point;
        ok=qa_vec_finite(point)||application_fail(e,QA_ERROR_FORMAT,"Native death reaction has a nonfinite point");
        qa_store_f32le(raw,point.x);qa_store_f32le(raw+4,point.y);qa_store_f32le(raw+8,point.z);}
    if(ok&&o->rerelease){qa_native_value value;uint8_t mod[3];
        qa_damage_cause cause=request->has_attack&&request->attack.cause.kind==QA_CAUSE_Q2?
            request->attack.cause:(qa_damage_cause){.kind=QA_CAUSE_Q2};
        ok=application_q2_native_cause_lower(true,QA_Q2_NATIVE_BASE,&cause,mod,&value,e);
        if(ok){memcpy(raw+bytes,mod,3);bytes+=3;}}
    if(ok&&bytes)ok=qa_native_allocate(instance(o),bytes,INT32_MIN+11,&scratch->scratch,e)&&
        qa_native_write(instance(o),scratch->scratch,(qa_bytes){raw,bytes},e);
    call->entry=entry;call->arguments[0]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=self};
    call->arguments[1]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=death?inflictor:attacker};
    call->arguments[2]=death?(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=attacker}:
        (qa_native_value){.type=QA_NATIVE_F32,.as.f32=kick};
    call->arguments[3]=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=(int32_t)damage};call->count=4;
    if(death)call->arguments[call->count++]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=scratch->scratch};
    if(o->rerelease)call->arguments[call->count++]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=scratch->scratch+(death?12:0)};
    if(ok)ok=application_native_q2_callbacks_transfer(o->engine->callbacks,canonical_reaction_original,call,e);
    if(ok)*handled=!call->cancelled;
    qa_error cleanup={0};if(scratch_close(o,scratch,&cleanup)){
        native_actor_touch **link=&o->touches;while(*link!=scratch)link=&(*link)->next;
        *link=scratch->next;free(scratch);
    }else{if(ok&&e)*e=cleanup;ok=false;}
    return ok;
}
static bool modified_reaction(native_actor_call *call,const application_q3_mod_actor_request *request,
    bool *handled,qa_error *e)
{
    struct application_native_q2_source_actors *o=call->entry->owner;native_source_actor *r=o->actors;
    while(r&&(r->retired||!qa_actor_id_equal(r->actor,call->actor)))r=r->next;
    if(!r)return application_fail(e,QA_ERROR_ARGUMENT,"Modified reaction lost its actual SourceActor");
    native_canonical_reaction lower={.actor=r,.operation=call->entry->operation,.request=request,
        .entry=call->entry,.captured=true};
    return canonical_reaction_body(&lower,request,handled,e);
}
static bool canonical_reaction_dispatch(void *context,qa_session *session,qa_error *e)
{
    (void)session;native_canonical_reaction *call=context;bool handled;
    return application_q3_mod_actor_dispatch(call->actor->owner->engine->provider->application->mod_operations,
        call->operation,call->request,canonical_reaction_body,call,&handled,e);
}
bool application_native_q2_source_actors_reaction(struct application_native_q2 *n,
    const qa_damage_outcome *outcome,bool *handled,qa_error *e)
{
    if(!n||!outcome||!handled)return application_fail(e,QA_ERROR_ARGUMENT,"Declared native reaction requires its actual outcome");
    *handled=false;struct application_native_q2_source_actors *o=n->source_actors;
    if(!o||o->suspended)return true;
    bool death=outcome->result.reaction==QA_REACTION_DEATH;
    if(!death&&outcome->result.reaction!=QA_REACTION_PAIN)return true;
    native_source_actor *r=o->actors;
    while(r&&(r->retired||!qa_actor_id_equal(r->actor,outcome->request.target)))r=r->next;
    if(!r)return true;
    *handled=true;if(!o->present[death?3:2])return true;
    application_q3_mod_actor_request request={.self=r->actor,.has_attack=true,.attack=outcome->request.attack};
    if(death){request.source.die.attacker=outcome->request.attack.attacker;
        request.source.die.inflictor=outcome->request.attack.inflictor;request.source.die.damage=outcome->result.applied_damage;
        request.source.die.kick=outcome->request.knockback;request.source.die.point=outcome->request.point;}
    else{request.source.pain.attacker=outcome->request.attack.attacker;request.source.pain.damage=outcome->result.applied_damage;
        request.source.pain.kick=outcome->request.knockback;}
    native_canonical_reaction call={.actor=r,.operation=death?Q3_MOD_DIE:Q3_MOD_PAIN,.request=&request};
    ++o->calls;++n->calls;
    bool ok=qa_session_invoke(n->provider->application->session,r->actor,death?QA_INVOKE_DIE:QA_INVOKE_PAIN,
        canonical_reaction_dispatch,&call,e);
    --n->calls;--o->calls;return ok;
}
bool application_native_q2_source_actors_admit(struct application_native_q2 *n,uint32_t slot,qa_actor_id actor,qa_error *e)
{
    if(!application_native_q2_source_actors_declared(n)) return true;
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(n->provider->application->session),actor);
    if(!actual||!actual->has_source||actual->owner!=n->provider->owner||actual->source_slot!=slot||!n->world)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared native body requires its actual allocated source actor");
    if(!prepare(n,e)) return false;
    struct application_native_q2_source_actors *o=n->source_actors;
    native_source_actor *r=o->actors;
    while(r&&!qa_actor_id_equal(r->actor,actor)) r=r->next;
    if(!r) {
        r=calloc(1,sizeof(*r));
        if(!r) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native body callback");
        r->owner=o;r->actor=actor;r->slot=slot;r->next=o->actors;o->actors=r;
    }
    if(!r->serial||qa_world_body_storage_serial(n->world,actor)!=r->serial) {
        if(!qa_native_host_source_body_bind(n->provider->state.native.host,slot,
            o->velocity,o->ground,r,body_write,e)) return false;
        r->serial=qa_world_body_storage_serial(n->world,actor);
    }
    if(!watch(r,e)) return false;
    for(size_t i=0;i<NATIVE_ACTOR_CALLBACKS;++i) if(!ensure(o,slot,i,e)) return false;
    if(o->combat){qa_combat_state state;
        if(!application_native_q2_source_combat_state_read(o->combat,actor,&state,e))return false;}
    return application_native_q2_source_damage_admit(o->damage,actor,e);
}
bool application_native_q2_source_actors_refresh(struct application_native_q2 *n,qa_error *e)
{
    if(!application_native_q2_source_actors_declared(n)) return true;
    if(!prepare(n,e)) return false;
    n->source_actors->suspended=false;
    if(!application_native_q2_source_damage_activate(n->source_actors->damage,e))return false;
    native_source_actor **link=&n->source_actors->actors;
    while(*link) {
        native_source_actor *r=*link;
        if(r->retired&&!n->source_actors->calls) {
            if(!unwatch(r,e)) return false;
            *link=r->next;free(r);
        }
        else link=&r->next;
    }
    qa_native_instance *native=qa_native_host_instance(n->provider->state.native.host);
    qa_native_entity_table table;
    if(!qa_native_entity_table_refresh(native,&table,e)) return false;
    for(uint32_t i=1;i<table.count;++i) {
        qa_native_slot_binding binding;
        if(!qa_native_slot(native,i,&binding,e)) return false;
        if(binding.kind==QA_NATIVE_SLOT_OWNED&&!application_native_q2_source_actors_admit(n,i,binding.actor,e)) return false;
    }
    return application_native_q2_source_damage_finish_restore(n->source_actors->damage,e);
}
void application_native_q2_source_actors_released(struct application_native_q2 *n,qa_actor_id actor)
{
    struct application_native_q2_source_actors *o=n?n->source_actors:NULL;
    if(!o) return;
    application_native_q2_source_damage_released(o->damage,actor);
    native_source_actor **link=&o->actors;
    while(*link&&!qa_actor_id_equal((*link)->actor,actor)) link=&(*link)->next;
    if(*link) (*link)->retired=true;
}
bool application_native_q2_source_actors_idle(const struct application_native_q2_source_actors *o)
{return !o||(!o->calls&&!o->touches&&application_native_q2_source_damage_idle(o->damage));}
bool application_native_q2_source_actors_returned(const struct application_native_q2_source_actors *o)
{return !o||(!o->calls&&application_native_q2_source_damage_returned(o->damage));}
bool application_native_q2_source_actors_suspend(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_source_actors *o=n?n->source_actors:NULL;
    if(!o) return true;
    if(o->calls) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native reaction is entered");
    if(!application_native_q2_source_damage_suspend(o->damage,e))return false;
    while(o->touches) {
        native_actor_touch *call=o->touches;
        if(!scratch_close(o,call,e)) return false;
        o->touches=call->next;free(call);
    }
    o->suspended=true;
    for(native_source_actor *r=o->actors;r;r=r->next) if(!unwatch(r,e)) return false;
    for(native_actor_entry *entry=o->entries;entry;entry=entry->next) {
        if(entry->binding&&!qa_native_unobserve_entry(entry->binding,e)) return false;
        entry->binding=NULL;
    }
    while(o->entries) {native_actor_entry *entry=o->entries;o->entries=entry->next;free(entry);}
    return true;
}
bool application_native_q2_source_actors_close(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_source_actors *o=n?n->source_actors:NULL;
    if(!o) return true;
    if(o->calls) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native body is entered");
    for(native_source_actor *r=o->actors;r;r=r->next)
        if(qa_actors_get(qa_session_actors(n->provider->application->session),r->actor))
            return application_fail(e,QA_ERROR_ARGUMENT,"Declared native body still owns a live canonical actor");
    if(!application_native_q2_source_actors_suspend(n,e)) return false;
    if(!application_native_q2_source_damage_destroy(&o->damage,e))return false;
    if(!application_native_q2_source_combat_state_destroy(&o->combat,e))return false;
    while(o->actors) {native_source_actor *r=o->actors;o->actors=r->next;free(r);}
    free(o);n->source_actors=NULL;return true;
}
bool application_native_q2_source_actors_damage_capture(struct application_native_q2 *n,qa_buffer *out,qa_error *e)
{
    if(!out)return false;
    *out=(qa_buffer){0};
    if(!application_native_q2_source_actors_declared(n))return true;
    if(!n->source_actors||!n->source_actors->ready)return application_fail(e,QA_ERROR_ARGUMENT,"SourceActor capture has no prepared original owner");
    return application_native_q2_source_damage_capture(n->source_actors->damage,out,e);
}
bool application_native_q2_source_actors_combat_binding(struct application_native_q2 *n,qa_actor_id actor,
    uint64_t serial,qa_combat_binding *out,qa_error *e)
{
    if(!application_native_q2_source_actors_declared(n)||!prepare(n,e))return false;
    return application_native_q2_source_damage_saved_binding(n->source_actors->damage,actor,serial,out,e);
}
