#include "guest_qc_pickups.h"
#include "guest_qc_items.h"
#include "guest_mod_item_definition.h"
#include "guest_qc_protection.h"
#include <float.h>
typedef struct qc_pickup_definition {
    uint32_t id;
    qa_item_id *offered;
    size_t offered_count;
    qa_pickup_write *writes;
    size_t write_count;
    application_qc_call gate,grant;
    bool gated,always;
} qc_pickup_definition;
struct application_qc_pickups { qc_pickup_definition *rules;size_t count; };
typedef struct qc_pickup_bound { struct application_qc_pickup_actor *actor;const qc_pickup_definition *definition; } qc_pickup_bound;
struct application_qc_pickup_actor {
    struct application_qc_state *engine;
    qa_actor_id actor;
    int32_t reference;
    qa_pickup_lease lease;
    qa_pickup_rule *rules;
    qc_pickup_bound *bindings;
    unsigned calls;
    struct application_qc_pickup_actor *next;
};
static bool storage_declared(application_provider *provider,const qa_pickup_write *write,qa_error *e)
{
    const struct application_qc_profile *p=provider->state.qc.qualified;
    if(write->resource.kind==QA_PICKUP_PROTECTION)
        return application_qc_protection_channel_declared(provider,write->resource.channel)||application_fail(e,QA_ERROR_FORMAT,"QC pickup has no actual declared protection channel");
    if(write->fields==QA_PICKUP_COUNT)
        for(size_t i=0;i<p->field_count;++i)if(p->fields[i].kind==QC_FIELD_INVENTORY&&p->fields[i].item==write->resource.item)return true;
    for(size_t i=0;p->items&&i<p->items->storage_count;++i){const application_qc_item_storage *s=p->items->storage+i;
        if(!s->bits&&s->item==write->resource.item&&(write->fields==QA_PICKUP_COUNT||s->capacity))return true;
        for(size_t j=0;s->bits&&write->fields==QA_PICKUP_COUNT&&j<s->count;++j)if(s->items[j].item==write->resource.item)return true;
    }return application_fail(e,QA_ERROR_FORMAT,"QC pickup write lacks actual declared inventory dimensions");
}
bool application_qc_pickups_qualify(application_provider *provider,const qa_json_document *d,qa_json_id node,qa_error *e)
{
    if(node==QA_JSON_NONE)return true;
    if(!application_qc_declaration_array(d,node,false,e))return false;
    size_t count=qa_json_size(d,node);if(!count)return true;
    if(!provider->state.qc.qualified->clients)return application_fail(e,QA_ERROR_FORMAT,"QC pickups require admitted Source clients");
    struct application_qc_pickups *p=calloc(1,sizeof(*p));
    if(!p)return application_fail(e,QA_ERROR_MEMORY,"Owning QC pickup profile");
    provider->state.qc.qualified->pickups=p;p->count=count;
    p->rules=calloc(count,sizeof(*p->rules));if(!p->rules)return application_fail(e,QA_ERROR_MEMORY,"Owning QC pickup rules");
    uint64_t inputs=(UINT64_C(1)<<QC_INPUT_SELF)|(UINT64_C(1)<<QC_INPUT_OTHER)|(UINT64_C(1)<<QC_INPUT_ITEM)|
        (UINT64_C(1)<<QC_INPUT_TIME)|(UINT64_C(1)<<QC_INPUT_PICKUP_COUNT)|(UINT64_C(1)<<QC_INPUT_PICKUP_HAS_COUNT)|(UINT64_C(1)<<QC_INPUT_PICKUP_DROPPED);
    for(size_t i=0;i<count;++i){qc_pickup_definition *v=p->rules+i;qa_json_id row=qa_json_at(d,node,i),operation=qa_json_get(d,row,"operation");
        if(!application_mod_pickup_definition(d,row,qa_session_strings(provider->application->session),&v->id,&v->offered,&v->offered_count,&v->writes,&v->write_count,e))return false;
        for(size_t j=0;j<i;++j)if(p->rules[j].id==v->id)return application_fail(e,QA_ERROR_FORMAT,"Duplicate QC pickup rule identity");
        for(size_t j=0;j<v->offered_count;++j)for(size_t k=0;k<=i;++k)for(size_t n=0;n<(k==i?j:p->rules[k].offered_count);++n)
            if(v->offered[j]==p->rules[k].offered[n])return application_fail(e,QA_ERROR_FORMAT,"Ambiguous QC offered pickup identity");
        for(size_t j=0;j<v->write_count;++j)if(!storage_declared(provider,v->writes+j,e))return false;
        v->gated=qa_json_string_equal(d,qa_json_get(d,operation,"kind"),"gate-then-grant");
        if(!v->gated&&!qa_json_string_equal(d,qa_json_get(d,operation,"kind"),"boolean-grant"))return application_fail(e,QA_ERROR_FORMAT,"Unknown QC pickup operation");
        if(!application_qc_call_parse(d,qa_json_get(d,operation,"grant"),provider->state.qc.program,inputs,false,&v->grant,e))return false;
        if(v->gated){v->always=qa_json_string_equal(d,qa_json_get(d,operation,"grantAccepts"),"always");
            if((!v->always&&!qa_json_string_equal(d,qa_json_get(d,operation,"grantAccepts"),"nonzero"))||
                !application_qc_call_parse(d,qa_json_get(d,operation,"gate"),provider->state.qc.program,inputs,false,&v->gate,e))return false;
        }
    }return true;
}
void application_qc_pickups_profile_free(struct application_qc_pickups *p)
{
    if(!p)return;
    for(size_t i=0;p->rules&&i<p->count;++i){qc_pickup_definition *v=p->rules+i;free(v->offered);free(v->writes);application_qc_call_free(&v->gate);application_qc_call_free(&v->grant);}
    free(p->rules);free(p);
}
static bool physical_current(struct application_qc_pickup_actor *a,qa_error *e)
{
    bool member=false;int32_t reference;
    return application_qc_control_source_client(a->engine->provider,a->actor,&member,e)&&member&&
        application_qc_reference(a->engine,a->actor,&reference,e)&&reference==a->reference;
}
static bool current(struct application_qc_pickup_actor *a,const qa_pickup_offer *offer,qa_pickup_execution *execution,qa_error *e)
{
    return qa_actor_id_equal(a->actor,offer->recipient)&&physical_current(a,e)&&
        qa_pickups_registration_current(a->engine->services.pickups,a->lease,a->engine->provider->owner)&&
        qa_pickup_current(execution)&&qa_pickup_recipient_is(execution,a->actor)&&
        qa_actors_get(qa_session_actors(a->engine->services.session),offer->pickup);
}
static bool decision(struct application_qc_state *engine,const application_qc_call *call,const application_qc_inputs *inputs,bool *accepted,qa_error *e)
{
    uint32_t result[3];if(!application_qc_run_call(engine,call,inputs,result,e))return false;
    float value;memcpy(&value,result,sizeof(value));
    if(!isfinite(value))return application_fail(e,QA_ERROR_FORMAT,"QC pickup returned a nonfinite Source decision");
    *accepted=value!=0;return true;
}
static bool take(void *context,const qa_pickup_offer *offer,qa_pickup_execution *execution,qa_pickup_outcome *out,qa_error *e)
{
    qc_pickup_bound *bound=context;struct application_qc_pickup_actor *a=bound->actor;const qc_pickup_definition *v=bound->definition;
    if(!current(a,offer,execution,e))return application_fail(e,QA_ERROR_ARGUMENT,"QC pickup lost its actual Source registration or full recipient");
    bool offered=false;for(size_t i=0;i<v->offered_count;++i)offered|=v->offered[i]==offer->item;
    double count=offer->override_count?offer->count:0,time=(double)offer->time_ns/1e9;
    if(!offered||!isfinite(count)||fabs(count)>FLT_MAX||(double)(float)count!=count||!isfinite((float)time))return application_fail(e,QA_ERROR_FORMAT,"QC pickup input exceeds its Source ABI");
    application_qc_inputs inputs={.self=a->actor,.other=offer->pickup,.item=qa_strings_cstr(qa_session_strings(a->engine->services.session),offer->item),
        .time_ns=offer->time_ns,.pickup_count=(float)count,.pickup_has_count=offer->override_count,.pickup_dropped=offer->dropped};
    ++a->calls;bool accepted=false,ok=true;
    if(v->gated){ok=decision(a->engine,&v->gate,&inputs,&accepted,e);
        if(ok&&(!accepted||!current(a,offer,execution,NULL))){*out=QA_PICKUP_REFUSED;--a->calls;return true;}}
    if(ok){if(v->always)ok=application_qc_run_call(a->engine,&v->grant,&inputs,NULL,e),accepted=ok;
        else ok=decision(a->engine,&v->grant,&inputs,&accepted,e);}
    if(ok)*out=accepted?QA_PICKUP_ACCEPTED:QA_PICKUP_REFUSED;
    --a->calls;return ok;
}
static struct application_qc_pickup_actor *find(struct application_qc_state *engine,qa_actor_id actor)
{for(struct application_qc_pickup_actor *a=engine->pickup_actors;a;a=a->next)if(a->lease.serial&&qa_actor_id_equal(a->actor,actor))return a;return NULL;}
static struct application_qc_pickup_actor *create(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    const struct application_qc_pickups *p=engine->provider->state.qc.qualified->pickups;
    struct application_qc_pickup_actor *a=calloc(1,sizeof(*a));
    if(!a){application_fail(e,QA_ERROR_MEMORY,"Owning actual QC pickup registration");return NULL;}
    a->engine=engine;a->actor=actor;
    a->rules=calloc(p->count,sizeof(*a->rules));a->bindings=calloc(p->count,sizeof(*a->bindings));
    if(!a->rules||!a->bindings||!application_qc_reference(engine,actor,&a->reference,e)||!physical_current(a,e)){
        free(a->rules);free(a->bindings);free(a);return NULL;
    }
    for(size_t i=0;i<p->count;++i){const qc_pickup_definition *v=p->rules+i;a->bindings[i]=(qc_pickup_bound){a,v};
        a->rules[i]=(qa_pickup_rule){.id=v->id,.offered=v->offered,.offered_count=v->offered_count,.writes=v->writes,.write_count=v->write_count,.context=a->bindings+i,.take=take};}
    a->next=engine->pickup_actors;engine->pickup_actors=a;return a;
}
bool application_qc_pickups_admit(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!engine->provider->state.qc.qualified||!engine->provider->state.qc.qualified->pickups)return true;
    struct application_qc_pickup_actor *a=find(engine,actor);
    if(a)return physical_current(a,e)&&qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner);
    a=create(engine,actor,e);return a&&qa_pickups_bind(engine->services.pickups,actor,engine->provider->owner,a->rules,engine->provider->state.qc.qualified->pickups->count,&a->lease,e);
}
bool application_qc_pickups_release(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    for(struct application_qc_pickup_actor *a=engine->pickup_actors;a;a=a->next)if(qa_actor_id_equal(a->actor,actor)&&a->lease.serial){
        if(qa_pickups_registration_owned(engine->services.pickups,a->lease,engine->provider->owner)&&!qa_pickups_close(engine->services.pickups,a->lease,e))return false;
        a->lease.serial=0;
    }
    return true;
}
bool application_qc_pickups_close(struct application_qc_state *engine,qa_error *e)
{
    for(struct application_qc_pickup_actor *a=engine->pickup_actors;a;a=a->next)if(a->calls)return application_fail(e,QA_ERROR_ARGUMENT,"QC pickups retain an entered Source grant");
    while(engine->pickup_actors){struct application_qc_pickup_actor *a=engine->pickup_actors;
        if(!application_qc_pickups_release(engine,a->actor,e))return false;
        engine->pickup_actors=a->next;free(a->rules);free(a->bindings);free(a);
    }return true;
}
bool application_qc_pickups_saved_rule(application_provider *provider,qa_actor_id actor,qa_actor_owner owner,uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *e)
{
    struct application_qc_state *engine=provider->state.qc.engine;const struct application_qc_pickups *p=provider->state.qc.qualified?provider->state.qc.qualified->pickups:NULL;
    if(!engine||!p||owner!=provider->owner||!serial)return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup has no matching actual declaration");
    struct application_qc_pickup_actor *a=find(engine,actor);
    if(a&&a->lease.serial!=serial)return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup duplicates full actor ownership");
    if(!a){a=create(engine,actor,e);if(!a)return false;a->lease=(qa_pickup_lease){actor,serial};}
    for(size_t i=0;i<p->count;++i)if(a->rules[i].id==id){*out=a->rules[i];return true;}
    return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup rule is absent from the actual declaration");
}

bool application_qc_pickups_ready(struct application_qc_state *engine,qa_error *e)
{
    if(!engine||!engine->provider->state.qc.qualified||!engine->provider->state.qc.qualified->pickups)return true;
    for(uint32_t slot=1;slot<=engine->max_clients;++slot){const application_qc_client *client=engine->clients+slot;
        if(!client->connected||!client->spawned)continue;
        struct application_qc_pickup_actor *a=find(engine,client->actor);
        if(!a||!physical_current(a,e)||!qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner))
            return application_fail(e,QA_ERROR_FORMAT,"Restored QC client lacks its canonical saved pickup registration");
    }
    for(struct application_qc_pickup_actor *a=engine->pickup_actors;a;a=a->next)
        if(a->lease.serial&&(!physical_current(a,e)||!qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner)))return false;
    return true;
}
