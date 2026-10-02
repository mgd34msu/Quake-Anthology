#include "guest_native_q2_private.h"
#include "native_q2_source_invocation.h"
#include "native_q2_source_damage.h"
#include "qa/native_observe.h"
#include "qa/persistence_fields.h"
#include <math.h>

typedef struct declared_damage_actor {
    struct declared_damage_actor *next;
    struct application_native_q2_source_damage *owner;
    qa_actor_id actor;
    uint64_t serial;
    uint32_t slot;
    qa_native_address watched;
    qa_native_write_observer *receipt_watch,*blood_watch;
    qa_damage_request pending_request;
    double saved_blood,saved_knockback;
    qa_vec3 saved_point;
    uint8_t saved_mod[3];
    uint32_t saved_attacker,saved_inflictor;
    bool bound,prepared,pending,pending_prepared;
} declared_damage_actor;
typedef struct declared_damage_frame {
    struct declared_damage_frame *outer,*retained_next;
    struct application_native_q2_source_damage *owner;
    qa_actor_id target;
    uint32_t slot;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result result;
    qa_native_write_observer *health_watch,*velocity_watch;
    application_native_q2_armor_watch *armor_watch;
    qa_native_address scratch;
    float health;
    qa_vec3 velocity;
    bool active,reacting;
} declared_damage_frame;
typedef struct declared_damage_processing {
    struct declared_damage_processing *outer;
    qa_damage_request request;
} declared_damage_processing;
typedef struct declared_damage_scalar {
    uint32_t offset;
    qa_native_value_type encoding;
} declared_damage_scalar;
struct application_native_q2_source_damage {
    struct application_native_q2 *engine;
    application_native_q2_source_combat_state *state;
    uint32_t velocity;
    uint64_t sequence;
    bool rerelease,suspended,ready;
    qa_json_id entry;
    qa_native_entry_observer *binding;
    qa_native_entry_observer *process_binding;
    qa_json_id process_entry;
    declared_damage_scalar blood,knockback;
    uint32_t attacker,inflictor,point,mod,receipt;
    uint8_t pointer_bytes;
    bool deferred;
    declared_damage_actor *actors;
    declared_damage_frame *frames,*retained;
    declared_damage_processing *processing;
};
static qa_native_instance *instance(struct application_native_q2_source_damage *o)
{return application_native_q2_callbacks_instance(o->engine->callbacks);}
static bool eligible(struct application_native_q2_source_damage *,qa_actor_id);
static declared_damage_actor *actor_find(struct application_native_q2_source_damage *o,qa_actor_id actor)
{for(declared_damage_actor *r=o->actors;r;r=r->next)if(qa_actor_id_equal(r->actor,actor))return r;return NULL;}
static bool address_for(struct application_native_q2_source_damage *o,qa_actor_id actor,
    qa_native_address *out,qa_error *e)
{
    if(!actor.registry)return qa_native_entity_address(instance(o),0,out,e);
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(o),&table,e))return false;
    for(uint32_t i=0;i<table.count;++i){qa_native_slot_binding b;
        if(!qa_native_slot(instance(o),i,&b,e))return false;
        if(b.kind!=QA_NATIVE_SLOT_FREE&&qa_actor_id_equal(b.actor,actor))return qa_native_entity_address(instance(o),i,out,e);}
    return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared damage lacks its actual actor projection");
}
static bool vector_read(struct application_native_q2_source_damage *o,qa_native_address at,qa_vec3 *out,qa_error *e)
{
    uint8_t raw[12];if(!at||!qa_native_read(instance(o),at,raw,12,e))return false;
    *out=qa_v3(qa_load_f32le(raw),qa_load_f32le(raw+4),qa_load_f32le(raw+8));
    return qa_vec_finite(*out)||application_fail(e,QA_ERROR_FORMAT,"Declared damage source vector is nonfinite");
}
static bool scalar_parse(struct application_native_q2_source_damage *o,qa_json_id id,
    declared_damage_scalar *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->engine->callbacks);
    static const char *const names[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    static const qa_native_value_type types[]={QA_NATIVE_I8,QA_NATIVE_U8,QA_NATIVE_I16,QA_NATIVE_U16,
        QA_NATIVE_I32,QA_NATIVE_U32,QA_NATIVE_I64,QA_NATIVE_U64,QA_NATIVE_F32,QA_NATIVE_F64};
    qa_json_id encoding=qa_json_get(d,id,"encoding");size_t i=0;uint64_t offset;qa_native_entity_table table;
    while(i<10&&!qa_json_string_equal(d,encoding,names[i]))++i;
    if(i==10||!qa_json_u64(d,qa_json_get(d,id,"offset"),&offset,e)||offset>UINT32_MAX||
        !qa_native_entity_table_get(instance(o),&table,e)||offset>table.stride||
        application_native_q2_field_size(types[i])>table.stride-offset)
        return application_fail(e,QA_ERROR_FORMAT,"Declared deferred scalar exceeds its actual edict");
    *out=(declared_damage_scalar){(uint32_t)offset,types[i]};return true;
}
static bool scalar_read(struct application_native_q2_source_damage *o,qa_native_address base,
    declared_damage_scalar field,double *out,qa_error *e)
{
    return application_native_q2_callbacks_scalar_read(o->engine->callbacks,base+field.offset,field.encoding,out,e)&&
        (isfinite(*out)||application_fail(e,QA_ERROR_FORMAT,"Declared deferred accumulator is nonfinite"));
}
static bool deferred_changed(void *context,qa_native_instance *native,const qa_native_write_event *event,qa_error *e)
{
    declared_damage_actor *r=context;struct application_native_q2_source_damage *o=r->owner;
    if(o->suspended||!r->bound||r->pending_prepared)return true;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),r->actor))return true;
    qa_native_address base;double blood;
    if(native!=instance(o)||!address_for(o,r->actor,&base,e)||base!=r->watched||
        !scalar_read(o,base,o->blood,&blood,e))return false;
    if(event->address==base+o->receipt&&blood!=0){
        declared_damage_frame *frame=o->frames;
        if(!frame||!frame->active||!qa_actor_id_equal(frame->request->target,r->actor))
            return application_fail(e,QA_ERROR_ARGUMENT,"Declared deferred receipt has no original attack provenance");
        r->pending_request=*frame->request;r->pending=true;
    }
    if(event->address==base+o->blood.offset&&blood==0)r->pending=false;
    return true;
}
static bool actor_watches_close(declared_damage_actor *r,qa_error *e)
{
    if(r->receipt_watch&&!qa_native_unobserve_writes(r->receipt_watch,e))return false;r->receipt_watch=NULL;
    if(r->blood_watch&&!qa_native_unobserve_writes(r->blood_watch,e))return false;r->blood_watch=NULL;
    r->watched=0;return true;
}
static bool actor_watches(declared_damage_actor *r,qa_error *e)
{
    struct application_native_q2_source_damage *o=r->owner;if(!o->deferred||o->suspended)return true;
    qa_native_address base;if(!address_for(o,r->actor,&base,e))return false;
    if(r->watched&&r->watched!=base&&!actor_watches_close(r,e))return false;r->watched=base;
    if(!r->receipt_watch&&!qa_native_observe_writes(instance(o),base+o->receipt,1,deferred_changed,r,&r->receipt_watch,e))return false;
    return r->blood_watch||qa_native_observe_writes(instance(o),base+o->blood.offset,
        application_native_q2_field_size(o->blood.encoding),deferred_changed,r,&r->blood_watch,e);
}
static bool state_read(void *context,qa_combat_state *out,qa_error *e)
{
    declared_damage_actor *r=context;
    return application_native_q2_source_combat_state_read(r->owner->state,r->actor,out,e);
}
static bool health_write(void *context,float value,qa_error *e)
{
    declared_damage_actor *r=context;
    return application_native_q2_source_combat_health_write(r->owner->state,r->actor,value,e);
}
static bool armor_validate(void *context,const qa_armor *value,qa_error *e)
{
    declared_damage_actor *r=context;
    return application_native_q2_source_combat_armor_validate(r->owner->state,r->actor,value,e);
}
static bool armor_write(void *context,const qa_armor *value,qa_error *e)
{
    declared_damage_actor *r=context;
    return application_native_q2_source_combat_armor_write(r->owner->state,r->actor,value,e);
}
static bool armor_normalize(void *context,const qa_armor *value,qa_armor *out,qa_error *e)
{
    declared_damage_actor *r=context;
    return application_native_q2_source_combat_armor_normalize(r->owner->state,r->actor,value,out,e);
}
static bool frame_live(declared_damage_frame *f)
{
    bool active=false;qa_error error={0};qa_native_slot_binding slot;
    return f->active&&!f->reacting&&f->owner->frames==f&&
        qa_actors_get(qa_session_actors(f->owner->engine->provider->application->session),f->target)&&
        qa_native_slot(instance(f->owner),f->slot,&slot,&error)&&qa_actor_id_equal(slot.actor,f->target)&&
        qa_native_host_source_active(f->owner->engine->provider->state.native.host,f->slot,&active,&error)&&active;
}
static bool health_changed(void *context,qa_native_instance *native,const qa_native_write_event *event,qa_error *e)
{
    (void)event;declared_damage_frame *f=context;if(!f->active||f->reacting)return true;
    if(native!=instance(f->owner))return application_fail(e,QA_ERROR_ARGUMENT,"Declared health observer crossed its original instance");
    qa_combat_state state;if(!application_native_q2_source_combat_projection_read(f->owner->state,f->target,&state,e))return false;
    float before=f->health;f->health=state.health;
    if(!frame_live(f)||before==state.health)return true;
    qa_damage_mutation mutation={.kind=QA_MUTATION_HEALTH,.value.health={before,state.health}};
    if(!qa_damage_observe(f->observer,&mutation,e))return false;f->result.applied_damage+=before-state.health;return true;
}
static bool velocity_changed(void *context,qa_native_instance *native,const qa_native_write_event *event,qa_error *e)
{
    (void)event;declared_damage_frame *f=context;if(!f->active||f->reacting)return true;
    qa_native_address base;qa_vec3 after;
    if(native!=instance(f->owner)||!address_for(f->owner,f->target,&base,e)||
        !vector_read(f->owner,base+f->owner->velocity,&after,e))return false;
    qa_vec3 before=f->velocity;f->velocity=after;if(!frame_live(f))return true;
    qa_damage_mutation mutation={.kind=QA_MUTATION_SOURCE_VELOCITY,
        .value.velocity={before,after,f->request->attack.movement_provider}};
    return qa_damage_observe(f->observer,&mutation,e);
}
static bool armor_changed(void *context,const qa_armor *before,const qa_armor *after,qa_error *e)
{
    declared_damage_frame *f=context;if(!frame_live(f))return true;
    qa_damage_mutation mutation={.kind=QA_MUTATION_ARMOR,.value.armor={*before,*after}};
    return qa_damage_observe(f->observer,&mutation,e);
}
static bool frame_close(declared_damage_frame *f,qa_error *e)
{
    if(f->health_watch&&!qa_native_unobserve_writes(f->health_watch,e))return false;f->health_watch=NULL;
    if(f->velocity_watch&&!qa_native_unobserve_writes(f->velocity_watch,e))return false;f->velocity_watch=NULL;
    if(!application_native_q2_armor_observe_end(&f->armor_watch,e))return false;
    if(f->scratch&&!qa_native_free(instance(f->owner),f->scratch,e))return false;f->scratch=0;return true;
}
static bool original_damage(void *context,qa_error *e)
{
    declared_damage_frame *f=context;struct application_native_q2_source_damage *o=f->owner;
    const qa_damage_request *r=f->request;qa_native_address target,attacker,inflictor;
    qa_native_value arguments[10],mod;uint8_t raw[36],cause[3];
    if((double)r->amount<INT32_MIN||(double)r->amount>INT32_MAX||
        (double)r->knockback<INT32_MIN||(double)r->knockback>INT32_MAX)
        return application_fail(e,QA_ERROR_FORMAT,"Declared damage exceeds its original integer ABI");
    if(!address_for(o,r->target,&target,e)||!address_for(o,r->attack.attacker,&attacker,e)||
        !address_for(o,r->attack.inflictor,&inflictor,e)||
        !application_native_q2_source_combat_cause_lower(o->state,&r->attack.cause,cause,&mod,e))return false;
    qa_vec3 vectors[]={r->direction,r->point,r->normal};
    for(size_t i=0;i<3;++i){qa_store_f32le(raw+i*12,vectors[i].x);qa_store_f32le(raw+i*12+4,vectors[i].y);qa_store_f32le(raw+i*12+8,vectors[i].z);}
    if(!qa_native_allocate(instance(o),36,INT32_MIN+12,&f->scratch,e)||
        !qa_native_write(instance(o),f->scratch,(qa_bytes){raw,36},e))return false;
    arguments[0]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=target};
    arguments[1]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=inflictor};
    arguments[2]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=attacker};
    for(size_t i=0;i<3;++i)arguments[3+i]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=f->scratch+i*12};
    arguments[6]=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=(int32_t)r->amount};
    arguments[7]=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=(int32_t)r->knockback};
    qa_damage_flags flags=qa_attack_flags(&r->attack);
    uint32_t source_flags=r->attack.cause.kind==QA_CAUSE_Q2?r->attack.cause.source.q2.flags:
        (r->radius?1u:0u)|(flags.no_armor?2u:0u)|(flags.energy?4u:0u)|(flags.no_knockback?8u:0u)|(flags.no_protection?32u:0u);
    arguments[8]=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=(int32_t)source_flags};arguments[9]=mod;
    qa_native_value result={.type=QA_NATIVE_VOID};bool cancelled=false;
    return application_native_q2_callbacks_source_before(o->engine,e)&&
        application_native_q2_source_invoke_original(o->engine,r->target,o->binding,arguments,10,&result,&cancelled,e);
}
static bool observed_damage(void *context,qa_error *e)
{
    declared_damage_frame *f=context;struct application_native_q2_source_damage *o=f->owner;
    qa_combat_state state;qa_native_address health,base;size_t bytes;
    bool ok=application_native_q2_source_combat_projection_read(o->state,f->target,&state,e)&&
        address_for(o,f->target,&base,e)&&qa_native_entity_slot(instance(o),base,&f->slot,e)&&
        vector_read(o,base+o->velocity,&f->velocity,e)&&
        application_native_q2_source_combat_projection_health(o->state,f->target,&health,&bytes,e);
    if(ok) {
        f->health=state.health;
        ok=application_native_q2_source_combat_projection_armor_observe(o->state,f->target,armor_changed,f,&f->armor_watch,e)&&
            qa_native_observe_writes(instance(o),health,bytes,health_changed,f,&f->health_watch,e)&&
            qa_native_observe_writes(instance(o),base+o->velocity,12,velocity_changed,f,&f->velocity_watch,e)&&
            original_damage(f,e);
    }
    f->active=false;qa_error cleanup={0};
    if(!frame_close(f,&cleanup)) {if(ok&&e)*e=cleanup;ok=false;}
    return ok;
}
static bool execute_damage(void *context,qa_combat *combat,const qa_damage_request *request,
    qa_damage_observer *observer,qa_damage_result *result,qa_error *e)
{
    (void)combat;declared_damage_actor *origin=context;struct application_native_q2_source_damage *o=origin->owner;
    declared_damage_actor *actor=actor_find(o,request->target);
    if(o->suspended||!o->binding||(actor&&(!actor->bound||!qa_combat_primary_current(o->engine->provider->application->combat,
        actor->actor,actor->serial,actor))))return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage lost its original primary binding");
    if(!eligible(o,request->target)||!eligible(o,request->attack.attacker)||!eligible(o,request->attack.inflictor)){
        *result=(qa_damage_result){0};return true;}
    declared_damage_frame *f=calloc(1,sizeof(*f));if(!f)return application_fail(e,QA_ERROR_MEMORY,"Retaining declared source damage observations");
    *f=(declared_damage_frame){.outer=o->frames,.owner=o,.target=request->target,
        .request=request,.observer=observer,.active=true};
    o->frames=f;++o->engine->calls;
    bool ok=application_native_q2_callbacks_transfer(o->engine->callbacks,observed_damage,f,e);
    --o->engine->calls;o->frames=f->outer;
    f->active=false;f->request=NULL;f->observer=NULL;if(ok)*result=f->result;
    qa_error cleanup={0};if(frame_close(f,&cleanup))free(f);
    else {f->retained_next=o->retained;o->retained=f;if(ok&&e)*e=cleanup;ok=false;}
    return ok;
}
static qa_combat_binding binding(declared_damage_actor *r)
{
    return (qa_combat_binding){.context=r,.read=state_read,.write_health=health_write,
        .write_armor=armor_write,.validate_armor=armor_validate,.normalize_legacy_armor=armor_normalize,
        .source_damage=execute_damage};
}
typedef struct declared_foreign_damage {
    qa_combat *combat;
    const qa_damage_request *request;
    qa_damage_outcome *outcome;
} declared_foreign_damage;
static bool foreign_damage(void *context,qa_error *e)
{
    declared_foreign_damage *call=context;
    return qa_combat_apply(call->combat,call->request,call->outcome,e);
}
static bool eligible(struct application_native_q2_source_damage *o,qa_actor_id actor)
{
    if(!actor.registry)return true;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))return false;
    for(uint32_t i=1;i<257;++i)if(o->engine->clients[i].reserved&&
        qa_actor_id_equal(o->engine->clients[i].actor,actor))return !o->engine->clients[i].denied;
    return true;
}
static bool provenance(struct application_native_q2_source_damage *o,qa_actor_id target,qa_attack *attack,qa_error *e)
{
    qa_application *app=o->engine->provider->application;qa_clock_state clock;
    application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
    if(!primary||!qa_session_clock(app->session,primary->owner,&clock)||clock.frame.provider!=primary->owner)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage lost the actual primary SourceFrame clock");
    attack->time_ns=clock.frame.time_ns;attack->weapon_provider=o->engine->provider->owner;
    qa_actor_id source=attack->attacker.registry?attack->attacker:target;
    application_provider *combat=application_provider_for(app,target,QA_ROLE_COMBAT,""),
        *inventory=application_provider_for(app,source,QA_ROLE_INVENTORY,""),*movement=application_provider_for(app,target,QA_ROLE_MOVEMENT,"");
    attack->combat_provider=combat?combat->owner:0;attack->inventory_provider=inventory?inventory->owner:0;
    attack->movement_provider=movement?movement->owner:0;
    return qa_attack_next(&o->sequence,attack,e);
}
bool application_native_q2_source_damage_reaction_attack(struct application_native_q2_source_damage *o,
    qa_actor_id target,qa_actor_id attacker,qa_actor_id inflictor,qa_native_address at,qa_attack *out,qa_error *e)
{
    if(!o||!o->rerelease||!at||!out)return application_fail(e,QA_ERROR_ARGUMENT,"Declared reaction lacks its real rerelease cause pointer");
    uint8_t bytes[3];qa_attack attack={.attacker=attacker,.inflictor=inflictor};
    qa_native_value mod={.type=QA_NATIVE_BYTES,.as.bytes={bytes,3}};
    if(!qa_native_read(instance(o),at,bytes,3,e)||
        !application_native_q2_source_combat_cause_read(o->state,&mod,0,&attack.cause,e)||
        !provenance(o,target,&attack,e))return false;
    *out=attack;return true;
}
static bool incoming(void *context,qa_native_instance *native,qa_native_entry_observer *entry,
    const qa_native_value *a,size_t count,qa_native_value *result,qa_error *e)
{
    struct application_native_q2_source_damage *o=context;
    if(native!=instance(o)||entry!=o->binding||count!=10)return application_fail(e,QA_ERROR_FORMAT,"Declared damage differs from its original signature");
    if(o->suspended)return application_native_q2_source_original(o->engine,entry,a,count,result,e);
    for(size_t i=0;i<6;++i)if(a[i].type!=QA_NATIVE_ADDRESS)return application_fail(e,QA_ERROR_FORMAT,"Declared damage requires its six pointer arguments");
    for(size_t i=6;i<9;++i)if(a[i].type!=QA_NATIVE_I32)return application_fail(e,QA_ERROR_FORMAT,"Declared damage requires its integer amount, kick and flags");
    qa_application *app=o->engine->provider->application;qa_damage_request r={0};
    if(!qa_native_host_source_actor(o->engine->provider->state.native.host,a[0].as.address,true,&r.target,e))return false;
    if(!r.target.registry){if(result)*result=(qa_native_value){.type=QA_NATIVE_VOID};return true;}
    if(a[1].as.address&&!qa_native_host_source_actor(o->engine->provider->state.native.host,a[1].as.address,true,&r.attack.inflictor,e))return false;
    if(a[2].as.address&&!qa_native_host_source_actor(o->engine->provider->state.native.host,a[2].as.address,true,&r.attack.attacker,e))return false;
    if(!eligible(o,r.target)||!eligible(o,r.attack.attacker)||!eligible(o,r.attack.inflictor)){
        if(result)*result=(qa_native_value){.type=QA_NATIVE_VOID};return true;}
    if(!vector_read(o,a[3].as.address,&r.direction,e)||!vector_read(o,a[4].as.address,&r.point,e)||!vector_read(o,a[5].as.address,&r.normal,e)||
        !application_native_q2_source_combat_cause_read(o->state,a+9,(uint32_t)a[8].as.i32,&r.attack.cause,e))return false;
    r.amount=(float)a[6].as.i32;r.knockback=(float)a[7].as.i32;r.radius=(a[8].as.i32&1)!=0;
    if((double)r.amount!=a[6].as.i32||(double)r.knockback!=a[7].as.i32)
        return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared damage exceeds exact canonical amount storage");
    if(!provenance(o,r.target,&r.attack,e))return false;
    declared_damage_actor *actor=actor_find(o,r.target);qa_damage_outcome outcome={0};
    declared_foreign_damage foreign={app->combat,&r,&outcome};
    ++o->engine->calls;
    bool ok=actor&&actor->bound?qa_combat_run_source(app->combat,&r,execute_damage,actor,&outcome,e):
        application_native_q2_callbacks_transfer(o->engine->callbacks,foreign_damage,&foreign,e);
    --o->engine->calls;qa_damage_outcome_free(&outcome);
    if(ok)ok=application_native_q2_callbacks_source_before(o->engine,e);
    if(ok&&result)*result=(qa_native_value){.type=QA_NATIVE_VOID};return ok;
}
static bool process_deferred(void *context,qa_native_instance *native,qa_native_entry_observer *entry,
    const qa_native_value *a,size_t count,qa_native_value *result,qa_error *e)
{
    struct application_native_q2_source_damage *o=context;
    if(native!=instance(o)||entry!=o->process_binding||count!=1||a[0].type!=QA_NATIVE_ADDRESS)
        return application_fail(e,QA_ERROR_FORMAT,"Declared deferred process differs from its real pointer ABI");
    if(o->suspended)return application_native_q2_source_original(o->engine,entry,a,count,result,e);
    qa_actor_id actor;
    if(!qa_native_host_source_actor(o->engine->provider->state.native.host,a[0].as.address,true,&actor,e))return false;
    declared_damage_actor *r=actor_find(o,actor);
    if(!r||!r->bound||!r->pending)return application_native_q2_source_original(o->engine,entry,a,count,result,e);
    double blood,kick;qa_vec3 point;qa_native_address base;
    if(!address_for(o,actor,&base,e)||base!=a[0].as.address||!scalar_read(o,base,o->blood,&blood,e))return false;
    if(blood==0)return application_native_q2_source_original(o->engine,entry,a,count,result,e);
    if(!scalar_read(o,base,o->knockback,&kick,e)||!vector_read(o,base+o->point,&point,e))return false;
    if((double)(float)blood!=blood||(double)(float)kick!=kick)
        return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared deferred reaction exceeds exact canonical scalar storage");
    declared_damage_processing processing={.outer=o->processing,.request=r->pending_request};
    processing.request.knockback=(float)kick;processing.request.point=point;
    if(!qa_damage_request_validate(&processing.request,e))return false;
    qa_combat_state state;if(!state_read(r,&state,e))return false;
    qa_damage_result reaction={.reaction=state.health<=0?QA_REACTION_DEATH:QA_REACTION_PAIN,.applied_damage=(float)blood};
    r->pending=false;o->processing=&processing;++o->engine->calls;
    bool cancelled=false;
    bool ok=qa_combat_source_reaction(o->engine->provider->application->combat,&processing.request,&reaction,e)&&
        application_native_q2_source_invoke_original(o->engine,actor,entry,a,count,result,&cancelled,e);
    --o->engine->calls;o->processing=processing.outer;return ok;
}
bool application_native_q2_source_damage_activate(struct application_native_q2_source_damage *o,qa_error *e)
{
    if(!o)return true;if(!o->ready)return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage retains unfinished declaration preparation");o->suspended=false;
    qa_native_address entry;
    qa_native_type fields[3]={{.kind=QA_NATIVE_U8,.count=1},{.kind=QA_NATIVE_U8,.count=1},{.kind=QA_NATIVE_U8,.count=1}},parameters[10];
    for(size_t i=0;i<6;++i)parameters[i]=(qa_native_type){.kind=QA_NATIVE_ADDRESS,.count=1};
    for(size_t i=6;i<10;++i)parameters[i]=(qa_native_type){.kind=QA_NATIVE_I32,.count=1};
    if(o->rerelease)parameters[9]=(qa_native_type){.kind=QA_NATIVE_BYTES,.fields=fields,.field_count=3,.count=1};
    qa_native_signature signature={.abi=qa_native_module_describe(o->engine->provider->state.native.module).image.target.abi,
        .parameters=parameters,.parameter_count=10,.result={.kind=QA_NATIVE_VOID,.count=1}};
    if(!o->binding&&(!application_native_q2_callbacks_entry(o->engine->callbacks,o->entry,&entry,e)||
        !qa_native_observe_entry(instance(o),entry,&signature,incoming,o,&o->binding,e)))return false;
    if(o->deferred&&!o->process_binding){qa_native_type parameter={.kind=QA_NATIVE_ADDRESS,.count=1};
        qa_native_signature process={.abi=signature.abi,.parameters=&parameter,.parameter_count=1,.result={.kind=QA_NATIVE_VOID,.count=1}};
        if(!application_native_q2_callbacks_entry(o->engine->callbacks,o->process_entry,&entry,e)||
            !qa_native_observe_entry(instance(o),entry,&process,process_deferred,o,&o->process_binding,e))return false;}
    return true;
}
bool application_native_q2_source_damage_create(struct application_native_q2 *n,
    application_native_q2_source_combat_state *state,uint32_t velocity,
    struct application_native_q2_source_damage **out,qa_error *e)
{
    if(!out)return false;if(!state)return true;if(*out)return application_native_q2_source_damage_activate(*out,e);
    struct application_native_q2_source_damage *o=calloc(1,sizeof(*o));if(!o)return application_fail(e,QA_ERROR_MEMORY,"Retaining declared original damage entry");
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id damage=qa_json_get(d,qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"combat"),"damage");
    o->engine=n;o->state=state;o->velocity=velocity;o->entry=qa_json_get(d,damage,"entry");
    o->rerelease=qa_json_string_equal(d,qa_json_get(d,damage,"abi"),"q2-rerelease");*out=o;
    o->pointer_bytes=qa_native_module_describe(qa_native_get_module(instance(o))).image.target.pointer_bytes;
    qa_json_id combat=qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"combat"),deferred=qa_json_get(d,combat,"deferred");
    if(deferred!=QA_JSON_NONE){
        o->deferred=true;o->process_entry=qa_json_get(d,deferred,"process");
        if(!scalar_parse(o,qa_json_get(d,deferred,"blood"),&o->blood,e)||
            !scalar_parse(o,qa_json_get(d,deferred,"knockback"),&o->knockback,e))return false;
        static const char *const names[]={"attacker","inflictor","point","mod","receipt"};
        uint32_t *fields[]={&o->attacker,&o->inflictor,&o->point,&o->mod,&o->receipt};
        size_t lengths[]={o->pointer_bytes,o->pointer_bytes,12,3,1};qa_native_entity_table table;
        if(!qa_native_entity_table_get(instance(o),&table,e))return false;
        for(size_t i=0;i<5;++i){uint64_t value;if(!qa_json_u64(d,qa_json_get(d,deferred,names[i]),&value,e)||
                value>table.stride||lengths[i]>table.stride-value||value>UINT32_MAX)
                return application_fail(e,QA_ERROR_FORMAT,"Declared deferred field exceeds its actual source edict");
            *fields[i]=(uint32_t)value;}
    }
    o->ready=true;return application_native_q2_source_damage_activate(o,e);
}
bool application_native_q2_source_damage_admit(struct application_native_q2_source_damage *o,qa_actor_id actor,qa_error *e)
{
    if(!o)return true;declared_damage_actor *r=actor_find(o,actor);
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor);
    if(!actual||!actual->has_source||actual->owner!=o->engine->provider->owner)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared combat admission lost its owned SourceActor");
    if(!r)for(declared_damage_actor *p=o->actors;p;p=p->next)if(p->prepared&&p->slot==actual->source_slot){r=p;break;}
    if(!r){r=calloc(1,sizeof(*r));if(!r)return application_fail(e,QA_ERROR_MEMORY,"Retaining declared SourceActor combat binding");
        r->owner=o;r->actor=actor;r->slot=actual->source_slot;r->next=o->actors;o->actors=r;}
    qa_combat *combat=o->engine->provider->application->combat;
    if(r->prepared&&!r->bound)return application_fail(e,QA_ERROR_ARGUMENT,"Saved declared combat primary has not been adopted by its canonical root");
    if(r->bound){if(!qa_combat_primary_current(combat,actor,r->serial,r))return application_fail(e,QA_ERROR_ARGUMENT,"Declared SourceActor primary binding changed");r->prepared=false;return actor_watches(r,e);}
    qa_combat_binding b=binding(r);if(!qa_combat_bind(combat,actor,&b,true,e))return false;
    r->serial=qa_combat_storage_serial(combat,actor);r->bound=true;return actor_watches(r,e);
}
bool application_native_q2_source_damage_reaction(struct application_native_q2_source_damage *o,qa_actor_id actor,
    const qa_damage_result *result,qa_attack *attack,float *knockback,bool *present,qa_error *e)
{
    *present=false;declared_damage_frame *f=o?o->frames:NULL;
    if(!f||!f->active||!qa_actor_id_equal(f->target,actor)){
        declared_damage_processing *p=o?o->processing:NULL;
        if(p&&qa_actor_id_equal(p->request.target,actor)){*present=true;*attack=p->request.attack;*knockback=p->request.knockback;}
        return true;}
    *present=true;*attack=f->request->attack;*knockback=f->request->knockback;
    if(f->reacting)return true;f->reacting=true;f->result=*result;
    return qa_damage_before_reaction(f->observer,result,e);
}
void application_native_q2_source_damage_released(struct application_native_q2_source_damage *o,qa_actor_id actor)
{declared_damage_actor *r=o?actor_find(o,actor):NULL;if(r){r->bound=false;r->pending=false;}}
bool application_native_q2_source_damage_returned(const struct application_native_q2_source_damage *o)
{return !o||(!o->frames&&!o->processing);}
bool application_native_q2_source_damage_idle(const struct application_native_q2_source_damage *o)
{return !o||(!o->frames&&!o->processing&&!o->retained);}
bool application_native_q2_source_damage_suspend(struct application_native_q2_source_damage *o,qa_error *e)
{
    if(!o)return true;if(o->frames||o->processing)return application_fail(e,QA_ERROR_ARGUMENT,"Declared original damage remains entered");
    o->suspended=true;
    while(o->retained){declared_damage_frame *f=o->retained;if(!frame_close(f,e))return false;o->retained=f->retained_next;free(f);}
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(!actor_watches_close(r,e))return false;
    if(o->process_binding&&!qa_native_unobserve_entry(o->process_binding,e))return false;o->process_binding=NULL;
    if(o->binding&&!qa_native_unobserve_entry(o->binding,e))return false;o->binding=NULL;return true;
}
bool application_native_q2_source_damage_destroy(struct application_native_q2_source_damage **out,qa_error *e)
{
    if(!out||!*out)return true;struct application_native_q2_source_damage *o=*out;
    if(!application_native_q2_source_damage_suspend(o,e))return false;
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(r->bound){qa_application *app=o->engine->provider->application;
        if(qa_actors_get(qa_session_actors(app->session),r->actor)&&!qa_combat_detach_primary(app->combat,r->actor,r->serial,r,e))return false;r->bound=false;}
    while(o->actors){declared_damage_actor *r=o->actors;o->actors=r->next;free(r);}free(o);*out=NULL;return true;
}
static bool request_fields(qa_source_save_io *io,qa_damage_request *request)
{
    qa_attack *attack=&request->attack;
    return qa_source_save_actor(io,&request->target)&&qa_persistence_attack(io,attack)&&
        (attack->cause.kind!=QA_CAUSE_Q1||qa_source_save_string(io,&attack->cause.source.q1.death_type))&&
        qa_source_save_string(io,&attack->weapon)&&qa_source_save_string(io,&attack->weapon_provider)&&
        qa_source_save_string(io,&attack->combat_provider)&&qa_source_save_string(io,&attack->inventory_provider)&&
        qa_source_save_string(io,&attack->movement_provider)&&qa_source_save_string(io,&attack->powerup_owner)&&
        qa_source_save_f32(io,&request->amount)&&qa_source_save_f32(io,&request->knockback)&&
        qa_source_save_vec3(io,&request->direction)&&qa_source_save_vec3(io,&request->point)&&
        qa_source_save_vec3(io,&request->normal)&&qa_source_save_bool(io,&request->radius);
}
static bool pending_fields(qa_source_save_io *io,declared_damage_actor *r)
{
    return request_fields(io,&r->pending_request)&&qa_source_save_f64(io,&r->saved_blood)&&
        qa_source_save_f64(io,&r->saved_knockback)&&qa_source_save_vec3(io,&r->saved_point)&&
        qa_source_save_bytes(io,r->saved_mod,3)&&qa_source_save_u32(io,&r->saved_attacker)&&
        qa_source_save_u32(io,&r->saved_inflictor);
}
static bool pointer_slot(struct application_native_q2_source_damage *o,qa_native_address at,uint32_t *out,qa_error *e)
{
    uint8_t raw[8];if(!qa_native_read(instance(o),at,raw,o->pointer_bytes,e))return false;
    qa_native_address pointer=o->pointer_bytes==4?qa_load_u32le(raw):qa_load_u64le(raw);
    if(!pointer){*out=UINT32_MAX;return true;}
    qa_native_entity_table table;
    return qa_native_entity_table_get(instance(o),&table,e)&&qa_native_entity_slot(instance(o),pointer,out,e)&&
        (*out<table.count||application_fail(e,QA_ERROR_FORMAT,"Deferred source pointer exceeds the actual allocated table"));
}
static bool pending_read(declared_damage_actor *r,qa_error *e)
{
    struct application_native_q2_source_damage *o=r->owner;qa_native_address base;
    if(!o->deferred||!address_for(o,r->actor,&base,e)||!scalar_read(o,base,o->blood,&r->saved_blood,e)||
        !scalar_read(o,base,o->knockback,&r->saved_knockback,e)||!vector_read(o,base+o->point,&r->saved_point,e)||
        !qa_native_read(instance(o),base+o->mod,r->saved_mod,3,e)||
        !pointer_slot(o,base+o->attacker,&r->saved_attacker,e)||!pointer_slot(o,base+o->inflictor,&r->saved_inflictor,e)||
        !qa_damage_request_validate(&r->pending_request,e))return false;
    return (r->saved_blood!=0&&qa_actor_id_equal(r->pending_request.target,r->actor))||
        application_fail(e,QA_ERROR_FORMAT,"Deferred capture lacks its actual accumulator provenance");
}
bool application_native_q2_source_damage_capture(struct application_native_q2_source_damage *o,qa_buffer *out,qa_error *e)
{
    if(!out)return false;*out=(qa_buffer){0};if(!o)return true;
    if(!application_native_q2_source_damage_idle(o))return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage capture retains original work");
    size_t count=0;qa_application *app=o->engine->provider->application;
    for(declared_damage_actor *r=o->actors;r;r=r->next){
        if(r->prepared)return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage capture retains an unadopted saved primary");
        if(!r->bound)continue;
        const qa_actor_record *actual=qa_actors_get(qa_session_actors(app->session),r->actor);
        if(r->prepared||!actual||!actual->has_source||actual->owner!=o->engine->provider->owner||actual->source_slot!=r->slot||
            !qa_combat_primary_current(app->combat,r->actor,r->serial,r))return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage capture lost its full primary source receipt");++count;}
    qa_source_save_io io={0};uint32_t version=UINT32_C(0x3243444e);
    bool ok=qa_source_save_writer(&io,app->session,e)&&qa_source_save_u32(&io,&version)&&
        qa_source_save_u64(&io,&o->sequence)&&qa_source_save_count(&io,&count,65536);
    for(declared_damage_actor *r=o->actors;ok&&r;r=r->next)if(r->bound){
        ok=qa_source_save_u32(&io,&r->slot)&&qa_source_save_u64(&io,&r->serial)&&qa_source_save_bool(&io,&r->pending);
        if(ok&&r->pending)ok=pending_read(r,e)&&pending_fields(&io,r);}
    if(ok)ok=qa_source_save_finish(&io,out);qa_source_save_dispose(&io);return ok;
}
bool application_native_q2_source_damage_restore(struct application_native_q2_source_damage *o,qa_bytes bytes,qa_error *e)
{
    if(!o)return !bytes.size||application_fail(e,QA_ERROR_FORMAT,"Saved declared damage has no real combat declaration");
    if(!o->suspended||!application_native_q2_source_damage_idle(o))
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage import requires its returned suspended observers");
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(r->bound&&
        qa_actors_get(qa_session_actors(o->engine->provider->application->session),r->actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage import cannot discard a live primary binding");
    qa_source_save_io io={0};uint32_t version=0;uint64_t sequence=0;size_t count=0;
    bool ok=qa_source_save_reader(&io,o->engine->provider->application->session,bytes,e)&&
        qa_source_save_u32(&io,&version)&&version==UINT32_C(0x3243444e)&&
        qa_source_save_u64(&io,&sequence)&&qa_source_save_count(&io,&count,65536);
    declared_damage_actor *rows=NULL,**tail=&rows;
    for(size_t i=0;ok&&i<count;++i){declared_damage_actor *r=calloc(1,sizeof(*r));
        if(!r){ok=application_fail(e,QA_ERROR_MEMORY,"Retaining decoded declared damage primary");break;}
        r->owner=o;r->prepared=true;*tail=r;tail=&r->next;
        ok=qa_source_save_u32(&io,&r->slot)&&qa_source_save_u64(&io,&r->serial)&&qa_source_save_bool(&io,&r->pending)&&
            (!r->pending||pending_fields(&io,r));
        if(ok&&(!r->slot||!r->serial||(r->pending&&(!o->deferred||
            !isfinite(r->saved_blood)||!isfinite(r->saved_knockback)||!qa_vec_finite(r->saved_point)||
            !qa_damage_request_validate(&r->pending_request,e)))))
            ok=application_fail(e,QA_ERROR_FORMAT,"Saved declared damage has invalid source or pending provenance");
        for(declared_damage_actor *p=rows;ok&&p!=r;p=p->next)if(p->slot==r->slot||p->serial==r->serial)
            ok=application_fail(e,QA_ERROR_FORMAT,"Saved declared damage repeats a slot or serial");
        r->pending_prepared=r->pending;}
    if(ok)ok=qa_source_save_finish(&io,NULL);qa_source_save_dispose(&io);
    if(!ok){while(rows){declared_damage_actor *r=rows;rows=r->next;free(r);}return false;}
    while(o->actors){declared_damage_actor *r=o->actors;o->actors=r->next;free(r);}o->actors=rows;o->sequence=sequence;return true;
}
bool application_native_q2_source_damage_saved_binding(struct application_native_q2_source_damage *o,
    qa_actor_id actor,uint64_t serial,qa_combat_binding *out,qa_error *e)
{
    qa_application *app=o?o->engine->provider->application:NULL;
    const qa_actor_record *actual=app?qa_actors_get(qa_session_actors(app->session),actor):NULL;
    if(!o||!out||!serial||!o->suspended||!actual||!actual->has_source||actual->owner!=o->engine->provider->owner)
        return application_fail(e,QA_ERROR_ARGUMENT,"Saved declared damage requires its retained real SourceActor");
    declared_damage_actor *r=o->actors;
    while(r&&r->slot!=actual->source_slot)r=r->next;
    if(!r||!r->prepared||r->serial!=serial||(r->actor.registry&&!qa_actor_id_equal(r->actor,actor)))
        return application_fail(e,QA_ERROR_FORMAT,"Saved declared combat primary differs from its source slot and serial");
    if(r->pending&&!qa_actor_id_equal(r->pending_request.target,actor))
        return application_fail(e,QA_ERROR_FORMAT,"Saved deferred request differs from its actual owned target");
    qa_combat_state state;if(!application_native_q2_source_combat_state_read(o->state,actor,&state,e))return false;
    r->actor=actor;r->bound=true;*out=binding(r);return true;
}
bool application_native_q2_source_damage_finish_restore(struct application_native_q2_source_damage *o,qa_error *e)
{
    if(!o)return true;
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(r->prepared){
        if(!r->bound||!r->actor.registry||!qa_combat_primary_current(o->engine->provider->application->combat,r->actor,r->serial,r))
            return application_fail(e,QA_ERROR_FORMAT,"Saved declared damage primary was not adopted by its actual SourceActor");
        qa_combat_state state;if(!state_read(r,&state,e))return false;}
    qa_native_entity_table table;if(!qa_native_entity_table_get(instance(o),&table,e))return false;
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(r->pending_prepared){
        qa_native_address base;
        if(!o->deferred||!r->bound||!qa_actor_id_equal(r->pending_request.target,r->actor)||
            r->saved_blood==0||
            (r->saved_attacker!=UINT32_MAX&&r->saved_attacker>=table.count)||
            (r->saved_inflictor!=UINT32_MAX&&r->saved_inflictor>=table.count)||
            !application_native_q2_field_value(o->blood.encoding,r->saved_blood,e)||
            !application_native_q2_field_value(o->knockback.encoding,r->saved_knockback,e)||
            !state_read(r,&(qa_combat_state){0},e)||!address_for(o,r->actor,&base,e))
            return application_fail(e,QA_ERROR_FORMAT,"Saved deferred damage lost its actual accumulator or full target");
        uint32_t offsets[]={o->attacker,o->inflictor,o->blood.offset,o->knockback.offset,o->point,o->mod};
        size_t lengths[]={o->pointer_bytes,o->pointer_bytes,application_native_q2_field_size(o->blood.encoding),
            application_native_q2_field_size(o->knockback.encoding),12,3};
        for(size_t i=0;i<6;++i)if(!qa_native_range_check(instance(o),base+offsets[i],lengths[i],QA_NATIVE_MEMORY_WRITE,e))return false;
    }
    for(declared_damage_actor *r=o->actors;r;r=r->next)if(r->pending_prepared){
        qa_native_address base,attacker=0,inflictor=0;uint8_t raw[12];
        if(!address_for(o,r->actor,&base,e)||
            (r->saved_attacker!=UINT32_MAX&&!qa_native_entity_address(instance(o),r->saved_attacker,&attacker,e))||
            (r->saved_inflictor!=UINT32_MAX&&!qa_native_entity_address(instance(o),r->saved_inflictor,&inflictor,e)))return false;
        if(o->pointer_bytes==4)qa_store_u32le(raw,(uint32_t)attacker);else qa_store_u64le(raw,attacker);
        if(!qa_native_write(instance(o),base+o->attacker,(qa_bytes){raw,o->pointer_bytes},e))return false;
        if(o->pointer_bytes==4)qa_store_u32le(raw,(uint32_t)inflictor);else qa_store_u64le(raw,inflictor);
        if(!qa_native_write(instance(o),base+o->inflictor,(qa_bytes){raw,o->pointer_bytes},e)||
            !application_native_q2_callbacks_scalar_write(o->engine->callbacks,base+o->blood.offset,o->blood.encoding,r->saved_blood,e)||
            !application_native_q2_callbacks_scalar_write(o->engine->callbacks,base+o->knockback.offset,o->knockback.encoding,r->saved_knockback,e))return false;
        qa_store_f32le(raw,r->saved_point.x);qa_store_f32le(raw+4,r->saved_point.y);qa_store_f32le(raw+8,r->saved_point.z);
        if(!qa_native_write(instance(o),base+o->point,(qa_bytes){raw,12},e)||
            !qa_native_write(instance(o),base+o->mod,(qa_bytes){r->saved_mod,3},e))return false;
        r->pending_prepared=false;
    }
    for(declared_damage_actor *r=o->actors;r;r=r->next)r->prepared=false;
    return true;
}
