#include "guest_qc_pickups.h"
#include "guest_qc_items.h"
#include "guest_qc_item_weapons.h"
#include "guest_mod_item_definition.h"
#include "guest_qc_protection.h"
#include "control_frame.h"
#include "supplies.h"
#include "qa/game_q1.h"
#include "qa/qc_observation.h"
#include <stdio.h>
#include "qa/pool.h"
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
typedef enum qc_pickup_source_kind { QC_PICKUP_ARMOR, QC_PICKUP_AMMO, QC_PICKUP_WEAPON, QC_PICKUP_BACKPACK } qc_pickup_source_kind;
typedef enum qc_pickup_operation {
    QC_PICKUP_DECISION, QC_PICKUP_ADMISSION, QC_PICKUP_GRANT, QC_PICKUP_SELECTION,
    QC_PICKUP_SET_WEAPON, QC_PICKUP_CARGO_OWNERSHIP, QC_PICKUP_CARGO_CURRENT, QC_PICKUP_COUNTER
} qc_pickup_operation;
typedef struct qc_pickup_descriptor {
    qa_item_id item, ammo;
    qa_string_id name;
    float value;
    uint32_t quantity, leave;
    bool quantity_field;
    qa_pickup_resource resource;
} qc_pickup_descriptor;
typedef struct qc_pickup_caller {
    uint32_t function;
    qc_pickup_source_kind kind;
    const qa_qc_definition *discriminator;
    qc_pickup_descriptor *descriptors;
    size_t count;
    uint32_t effect_word;
    float effect_value;
} qc_pickup_caller;
typedef struct qc_pickup_source_region {
    qc_pickup_operation operation;
    uint32_t word;
    const qa_qc_definition *field;
    qa_item_id item;
} qc_pickup_source_region;
struct application_qc_pickups {
    qc_pickup_definition *rules;
    size_t count;
    qc_pickup_caller callers[4];
    qa_qc_inline_region *regions;
    qc_pickup_source_region *operations;
    size_t region_count;
    const qa_qc_definition *self, *other, *time, *items;
    uint32_t remove, deathmatch_weapon, set_current_ammo;
};
typedef struct qc_pickup_source_frame qc_pickup_source_frame;
typedef struct qc_pickup_projection {
    const qa_qc_inline_region *region;
    int32_t reference;
    uint32_t word;
    bool stored;
} qc_pickup_projection;
static bool source_compile(application_provider *, struct application_qc_pickups *, qa_error *);
typedef struct qc_pickup_bound { struct application_qc_pickup_actor *actor;const qc_pickup_definition *definition; } qc_pickup_bound;
struct application_qc_pickup_actor {
    size_t storage_slot;
    struct application_qc_state *engine;
    qa_actor_id actor;
    int32_t reference;
    qa_pickup_lease lease;
    qc_pickup_bound *bindings;
    unsigned calls;
    struct application_qc_pickup_actor *next;
};
struct application_qc_pickup_store {
    qa_unified_frame_lease *storage;
    qa_pool actors;
    qa_actor_index index;
    struct application_qc_pickup_actor *head;
    qc_pickup_source_frame *source;
    qc_pickup_projection *projection;
};
bool application_qc_pickups_create(struct application_qc_state *engine,qa_error *e)
{
    const struct application_qc_pickups *p=engine->provider->state.qc.pickup_profile;
    if(engine->pickup_store||!p)return true;
    struct application_qc_pickup_store *store=calloc(1,sizeof(*store));
    if(!store)return application_fail(e,QA_ERROR_MEMORY,"Owning QC pickup binding store");
    if(!p->count){engine->pickup_store=store;return true;}
    const qa_actor_registry *registry=qa_session_actors(engine->services.session);
    size_t stride=sizeof(struct application_qc_pickup_actor);
    if(p->count>(SIZE_MAX-stride)/sizeof(qc_pickup_bound)){
        free(store);return application_fail(e,QA_ERROR_MEMORY,"QC pickup binding storage is too large");
    }
    stride+=p->count*sizeof(qc_pickup_bound);
    store->storage=application_control_storage_acquire(engine->provider->application,e);
    qa_arena *arena=store->storage?qa_unified_frame_lease_arena(store->storage):NULL;
    if(!arena||!qa_actor_index_create(&store->index,registry,arena,e)||
        !qa_pool_prepare(&store->actors,arena,qa_actors_capacity(registry),stride,
            _Alignof(struct application_qc_pickup_actor),e)){
        qa_actor_index_destroy(&store->index);
        qa_unified_frame_lease_release(store->storage);
        free(store);return false;
    }
    engine->pickup_store=store;return true;
}
void application_qc_pickups_destroy(struct application_qc_state *engine)
{
    if(!engine->pickup_store)return;
    qa_actor_index_destroy(&engine->pickup_store->index);
    qa_unified_frame_lease_release(engine->pickup_store->storage);
    free(engine->pickup_store);engine->pickup_store=NULL;
}
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
    struct application_qc_pickups *p=calloc(1,sizeof(*p));
    if(!p)return application_fail(e,QA_ERROR_MEMORY,"Owning QC pickup profile");
    provider->state.qc.pickup_profile=p;
    if(!source_compile(provider,p,e))return false;
    if(node==QA_JSON_NONE){
        bool found=false;for(size_t i=0;i<4;++i)found|=p->callers[i].function!=0;
        if(!found){application_qc_pickups_profile_free(p);provider->state.qc.pickup_profile=NULL;}
        return true;
    }
    if(!application_qc_declaration_array(d,node,false,e))return false;
    size_t count=qa_json_size(d,node);if(!count)return true;
    if(!provider->state.qc.qualified->clients)return application_fail(e,QA_ERROR_FORMAT,"QC pickups require admitted Source clients");
    p->count=count;
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
    for(size_t i=0;i<4;++i)free(p->callers[i].descriptors);
    free(p->regions);free(p->operations);free(p->rules);free(p);
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
static qa_pickup_rule bound_rule(struct application_qc_pickup_actor *a,size_t index)
{
    const qc_pickup_definition *v=a->bindings[index].definition;
    return (qa_pickup_rule){.id=v->id,.offered=v->offered,.offered_count=v->offered_count,
        .writes=v->writes,.write_count=v->write_count,.context=a->bindings+index,.take=take};
}
static struct application_qc_pickup_actor *find(struct application_qc_state *engine,qa_actor_id actor)
{
    size_t slot;struct application_qc_pickup_store *store=engine->pickup_store;
    if(!store||!qa_actor_index_find(&store->index,actor,&slot))return NULL;
    struct application_qc_pickup_actor *a=(struct application_qc_pickup_actor *)(uintptr_t)slot;
    return a->lease.serial?a:NULL;
}
static void released_sweep(struct application_qc_state *engine)
{
    if(!qa_pickups_idle(engine->services.pickups))return;
    struct application_qc_pickup_store *store=engine->pickup_store;
    struct application_qc_pickup_actor **link=&store->head;
    while(*link){
        struct application_qc_pickup_actor *a=*link;
        if(a->lease.serial||a->calls){link=&a->next;continue;}
        size_t row;
        if(qa_actor_index_find(&store->index,a->actor,&row)&&row==(size_t)(uintptr_t)a)
            qa_actor_index_remove(&store->index,a->actor);
        *link=a->next;
        qa_pool_release(&store->actors,a->storage_slot);
    }
}
static struct application_qc_pickup_actor *create(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    const struct application_qc_pickups *p=engine->provider->state.qc.pickup_profile;
    struct application_qc_pickup_store *store=engine->pickup_store;
    released_sweep(engine);
    if(store->actors.head==SIZE_MAX&&!qa_pool_extend(&store->actors,
        qa_unified_frame_lease_arena(store->storage),store->index.capacity,e))return NULL;
    size_t storage_slot;
    struct application_qc_pickup_actor *a=qa_pool_take(&store->actors,&storage_slot);
    if(!a){application_fail(e,QA_ERROR_MEMORY,"Owning actual QC pickup registration");return NULL;}
    *a=(struct application_qc_pickup_actor){.storage_slot=storage_slot,.engine=engine,.actor=actor};
    a->bindings=(qc_pickup_bound *)(a+1);
    if(!application_qc_reference(engine,actor,&a->reference,e)||!physical_current(a,e)){
        qa_pool_release(&store->actors,storage_slot);return NULL;
    }
    for(size_t i=0;i<p->count;++i)a->bindings[i]=(qc_pickup_bound){a,p->rules+i};
    a->next=store->head;store->head=a;
    qa_actor_index_put(&store->index,actor,(size_t)(uintptr_t)a);return a;
}
bool application_qc_pickups_admit(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!engine->provider->state.qc.pickup_profile||!engine->provider->state.qc.pickup_profile->count)return true;
    struct application_qc_pickup_actor *a=find(engine,actor);
    if(a)return physical_current(a,e)&&qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner);
    a=create(engine,actor,e);if(!a)return false;
    size_t count=engine->provider->state.qc.pickup_profile->count;
    qa_unified_frame_lease *storage=application_control_storage_acquire(engine->provider->application,e);
    if(!storage)return false;
    qa_pickup_rule *rules=qa_unified_frame_lease_alloc(storage,count,sizeof(*rules),_Alignof(qa_pickup_rule),e);
    if(!rules){qa_unified_frame_lease_release(storage);return false;}
    for(size_t i=0;i<count;++i)rules[i]=bound_rule(a,i);
    bool ok=qa_pickups_bind(engine->services.pickups,actor,engine->provider->owner,rules,count,&a->lease,e);
    qa_unified_frame_lease_release(storage);return ok;
}
bool application_qc_pickups_release(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    struct application_qc_pickup_actor *a=find(engine,actor);
    if(a){
        if(qa_pickups_registration_owned(engine->services.pickups,a->lease,engine->provider->owner)&&!qa_pickups_close(engine->services.pickups,a->lease,e))return false;
        a->lease.serial=0;
        qa_actor_index_remove(&engine->pickup_store->index,actor);
    }
    return true;
}
bool application_qc_pickups_close(struct application_qc_state *engine,qa_error *e)
{
    struct application_qc_pickup_store *store=engine->pickup_store;
    if(!application_qc_pickups_idle(engine))return application_fail(e,QA_ERROR_ARGUMENT,"QC pickups retain an entered Source caller");
    for(struct application_qc_pickup_actor *a=store?store->head:NULL;a;a=a->next)if(a->calls)return application_fail(e,QA_ERROR_ARGUMENT,"QC pickups retain an entered Source grant");
    while(store&&store->head){struct application_qc_pickup_actor *a=store->head;
        if(!application_qc_pickups_release(engine,a->actor,e))return false;
        size_t row;
        if(qa_actor_index_find(&store->index,a->actor,&row)&&row==(size_t)(uintptr_t)a)
            qa_actor_index_remove(&store->index,a->actor);
        store->head=a->next;
        qa_pool_release(&store->actors,a->storage_slot);
    }return true;
}
bool application_qc_pickups_saved_rule(application_provider *provider,qa_actor_id actor,qa_actor_owner owner,uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *e)
{
    struct application_qc_state *engine=provider->state.qc.engine;const struct application_qc_pickups *p=provider->state.qc.pickup_profile;
    if(!engine||!p||!p->count||owner!=provider->owner||!serial)return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup has no matching actual declaration");
    struct application_qc_pickup_actor *a=find(engine,actor);
    if(a&&a->lease.serial!=serial)return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup duplicates full actor ownership");
    if(!a){a=create(engine,actor,e);if(!a)return false;a->lease=(qa_pickup_lease){actor,serial};}
    for(size_t i=0;i<p->count;++i)if(p->rules[i].id==id){*out=bound_rule(a,i);return true;}
    return application_fail(e,QA_ERROR_FORMAT,"Saved QC pickup rule is absent from the actual declaration");
}

bool application_qc_pickups_ready(struct application_qc_state *engine,qa_error *e)
{
    if(!engine||!engine->provider->state.qc.pickup_profile||!engine->provider->state.qc.pickup_profile->count)return true;
    for(uint32_t slot=1;slot<=engine->max_clients;++slot){const application_qc_client *client=engine->clients+slot;
        if(!client->connected||!client->spawned)continue;
        struct application_qc_pickup_actor *a=find(engine,client->actor);
        if(!a||!physical_current(a,e)||!qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner))
            return application_fail(e,QA_ERROR_FORMAT,"Restored QC client lacks its canonical saved pickup registration");
    }
    for(struct application_qc_pickup_actor *a=engine->pickup_store?engine->pickup_store->head:NULL;a;a=a->next)
        if(a->lease.serial&&(!physical_current(a,e)||!qa_pickups_registration_current(engine->services.pickups,a->lease,engine->provider->owner)))return false;
    return true;
}

typedef struct qc_pickup_analysis {
    application_provider *provider;
    struct application_qc_pickups *profile;
    const qa_qc_program *program;
    uint8_t *written;
} qc_pickup_analysis;
static bool source_type(const qa_qc_program *program,uint32_t word,qa_qc_value_type type)
{
    qa_qc_program_info info=qa_qc_program_describe(program);
    for(uint32_t i=0;i<info.global_count;++i){const qa_qc_definition *d=qa_qc_program_global(program,i);
        if(d->offset==word&&d->type==type)return true;}
    return false;
}
static bool source_initial(const qc_pickup_analysis *a,uint32_t word,qa_qc_value_type type,int32_t *out)
{
    return !a->written[word]&&source_type(a->program,word,type)&&
        qa_qc_program_initial_int(a->program,word,out,NULL);
}
static bool source_field(const qc_pickup_analysis *a,uint32_t word,const qa_qc_definition *field)
{
    int32_t initial;
    return field&&source_initial(a,word,QA_QC_FIELD,&initial)&&initial==field->offset;
}
static bool source_float(const qc_pickup_analysis *a,uint32_t word,float *out)
{
    int32_t initial;
    if(!source_initial(a,word,QA_QC_FLOAT,&initial))return false;
    memcpy(out,&initial,4);return isfinite(*out);
}
static uint32_t source_function(const qc_pickup_analysis *a,const char *name,size_t arguments,bool builtin)
{
    uint32_t index;int32_t initial;
    const qa_qc_function *f=qa_qc_program_find_function(a->program,name,&index);
    const qa_qc_definition *d=qa_qc_program_find_global(a->program,name);
    if(!f||!d||!index||f->named_builtin||f->parameter_count!=arguments||
        !source_initial(a,d->offset,QA_QC_FUNCTION,&initial)||initial!=(int32_t)index||
        (builtin?f->first_statement!=-15:f->first_statement<=0))return 0;
    uint32_t parameter=f->parameter_start;
    for(size_t i=0;i<arguments;++i){
        if(f->parameter_sizes[i]!=1||(!builtin&&!source_type(a->program,parameter,QA_QC_FLOAT)))return 0;
        ++parameter;
    }
    if(!builtin)for(uint32_t pc=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f);pc<end;++pc){
        const qa_qc_statement *s=qa_qc_program_statement(a->program,pc);
        if((s->opcode==QA_QC_RETURN||s->opcode==QA_QC_DONE)&&s->a)return 0;}
    return index;
}
static bool source_call(const qc_pickup_analysis *a,const qa_qc_statement *s,uint32_t function)
{
    int32_t initial;
    return function&&s->opcode>=QA_QC_CALL0&&s->opcode<=QA_QC_CALL8&&
        source_initial(a,s->a,QA_QC_FUNCTION,&initial)&&initial==(int32_t)function;
}
static bool source_load(const qc_pickup_analysis *a,const qa_qc_statement *s,
    qa_qc_opcode opcode,uint32_t entity,const qa_qc_definition *field)
{
    return s&&s->opcode==opcode&&s->a==entity&&source_field(a,s->b,field)&&
        (opcode!=QA_QC_LOAD_F||field->type==QA_QC_FLOAT)&&(opcode!=QA_QC_LOAD_S||field->type==QA_QC_STRING);
}
static const qa_qc_statement *source_at(const qc_pickup_analysis *a,uint32_t pc)
{ return qa_qc_program_statement(a->program,pc); }
static bool source_branch_return(const qc_pickup_analysis *a,uint32_t pc,uint32_t word)
{
    const qa_qc_statement *branch=source_at(a,pc),*ret=source_at(a,pc+1);
    return branch&&ret&&branch->opcode==QA_QC_IFNOT&&branch->a==word&&
        (int16_t)branch->b==2&&ret->opcode==QA_QC_RETURN&&!ret->a;
}
static const qa_qc_definition *source_ammo(const qc_pickup_analysis *a,uint32_t field,qa_item_id *item,qa_error *e)
{
    const qa_qc_game_fields *f=qa_qc_program_resolved_fields(a->program);
    const qa_qc_definition *fields[]={f->ammo_shells,f->ammo_nails,f->ammo_rockets,f->ammo_cells};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i)if(source_field(a,field,fields[i])){
        if(!qa_strings_intern_cstr(qa_session_strings(a->provider->application->session),
            qa_q1_ammo_identity((qa_q1_ammo)i),item,e))return NULL;
        return fields[i];
    }
    return NULL;
}
static bool source_weapon(const qc_pickup_analysis *a,uint32_t word,qa_item_id *item,float *value,qa_error *e)
{
    qa_q1_weapon weapon;qa_q1_weapon_profile profile;
    if(!source_float(a,word,value)||*value<1||*value>UINT32_C(0x80000000)||truncf(*value)!=*value||
        !qa_q1_weapon_source(application_q1_program(a->provider->product->campaign),(uint32_t)*value,&weapon)||
        !qa_q1_weapon_profile_identity(application_q1_program(a->provider->product->campaign),weapon,&profile))return false;
    return qa_strings_intern_cstr(qa_session_strings(a->provider->application->session),profile.item,item,e);
}
static bool source_name(const qc_pickup_analysis *a,uint32_t word,qa_string_id *out,qa_error *e)
{
    int32_t initial;qa_bytes strings=qa_qc_program_strings(a->program);
    if(!source_initial(a,word,QA_QC_STRING,&initial)||initial<0||(size_t)initial>=strings.size)return false;
    const uint8_t *start=strings.data+initial,*end=memchr(start,0,strings.size-(size_t)initial);
    return end&&qa_strings_intern(qa_session_strings(a->provider->application->session),
        (qa_bytes){start,(size_t)(end-start)},out,e);
}
static bool source_descriptor(qc_pickup_caller *c,qc_pickup_descriptor value,qa_error *e)
{
    qc_pickup_descriptor *grown=realloc(c->descriptors,(c->count+1)*sizeof(*grown));
    if(!grown)return application_fail(e,QA_ERROR_MEMORY,"Owning compiled QC pickup descriptors");
    c->descriptors=grown;grown[c->count++]=value;return true;
}
static bool source_region(qc_pickup_analysis *a,qc_pickup_caller *c,uint32_t entry,uint32_t exit,
    qc_pickup_source_region operation,bool standalone,qa_error *e)
{
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f);
    if(entry<first||exit<=entry||exit>=end)return false;
    size_t declared_count=0;
    const qa_qc_inline_region *declared=application_qc_item_weapons_regions(a->provider,&declared_count);
    for(size_t i=0;i<declared_count;++i)if(declared[i].function==c->function&&
        entry<declared[i].exit&&exit>declared[i].entry)return false;
    for(uint32_t pc=first;pc<end;++pc){const qa_qc_statement *s=source_at(a,pc);
        int64_t destination=s->opcode==QA_QC_GOTO?(int64_t)pc+(int16_t)s->a:
            (s->opcode==QA_QC_IF||s->opcode==QA_QC_IFNOT)?(int64_t)pc+(int16_t)s->b:-1;
        if(pc<entry||pc>=exit){if(destination>(int64_t)entry&&destination<(int64_t)exit)return false;}
        else if(destination>=0&&(destination<(int64_t)entry||destination>(int64_t)exit))return false;
        if(pc>=entry&&pc<exit&&(s->opcode==QA_QC_RETURN||s->opcode==QA_QC_DONE))return false;
    }
    struct application_qc_pickups *p=a->profile;size_t n=p->region_count;
    for(size_t i=0;i<n;++i)if(p->regions[i].function==c->function&&p->regions[i].entry==entry)return false;
    qa_qc_inline_region *regions=realloc(p->regions,(n+1)*sizeof(*regions));
    if(!regions)return application_fail(e,QA_ERROR_MEMORY,"Owning compiled QC pickup regions");
    p->regions=regions;
    qc_pickup_source_region *operations=realloc(p->operations,(n+1)*sizeof(*operations));
    if(!operations)return application_fail(e,QA_ERROR_MEMORY,"Owning compiled QC pickup operations");
    p->operations=operations;
    p->regions[n]=(qa_qc_inline_region){.function=c->function,.entry=entry,.exit=exit,.replaceable=true,
        .saved_scope=standalone?QA_QC_INLINE_FRAME:QA_QC_INLINE_NOT_STANDALONE,.saved_word=f->parameter_start};
    p->operations[n]=operation;++p->region_count;return true;
}
static bool source_decision(qc_pickup_analysis *a,qc_pickup_caller *c,uint32_t entry,uint32_t exit,qa_error *e)
{
    const qa_qc_statement *branch=source_at(a,exit);
    return branch&&source_branch_return(a,exit,branch->a)&&source_region(a,c,entry,exit,
        (qc_pickup_source_region){.operation=QC_PICKUP_DECISION,.word=branch->a},false,e);
}
static bool source_armor(qc_pickup_analysis *a,qc_pickup_caller *c,qa_error *e)
{
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_definition *type=qa_qc_program_find_field(a->program,"armortype");
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),other=a->profile->other->offset;
    c->discriminator=fields->classname;
    uint32_t decision=0,grant=0,join=0;
    for(uint32_t pc=first;pc+6<end;++pc){const qa_qc_statement *s=source_at(a,pc),*next=source_at(a,pc+1);
        if(source_load(a,s,QA_QC_LOAD_S,a->profile->self->offset,fields->classname)&&next->opcode==QA_QC_EQ_S&&next->a==s->c){
            qc_pickup_descriptor d={.resource={.kind=QA_PICKUP_PROTECTION,.channel=QA_PROTECTION_REGULAR}};
            if(!source_name(a,next->b,&d.name,e))return false;
            const char *name=qa_strings_cstr(qa_session_strings(a->provider->application->session),d.name);
            if(strcmp(name,"item_armor1")&&strcmp(name,"item_armor2")&&strcmp(name,"item_armorInv"))return false;
            char identity[64];int length=snprintf(identity,sizeof(identity),"q1:%s",name);
            if(length<=0||(size_t)length>=sizeof(identity)||!qa_strings_intern_cstr(
                qa_session_strings(a->provider->application->session),identity,&d.item,e)||!source_descriptor(c,d,e))return false;
        }
        if(source_load(a,s,QA_QC_LOAD_F,other,type)&&source_load(a,next,QA_QC_LOAD_F,other,fields->armorvalue)){
            const qa_qc_statement *left=source_at(a,pc+2),*right=source_at(a,pc+3),*compare=source_at(a,pc+4);
            if(left->opcode!=QA_QC_MUL_F||left->a!=s->c||left->b!=next->c||right->opcode!=QA_QC_MUL_F||
                compare->opcode!=QA_QC_GE||compare->a!=left->c||compare->b!=right->c||
                !source_branch_return(a,pc+5,compare->c))return false;
            decision=pc;grant=pc+7;
            if(!source_load(a,source_at(a,grant),QA_QC_ADDRESS,other,type))return false;
            for(uint32_t tail=grant;tail<end;++tail)if(source_load(a,source_at(a,tail),QA_QC_ADDRESS,
                a->profile->self->offset,fields->solid)){join=tail;break;}
        }
    }
    if(c->count!=3||!decision||!join)return false;
    /* Only the actual recipient armor fields belong to the grant. */
    for(uint32_t pc=grant;pc<join;++pc){const qa_qc_statement *s=source_at(a,pc);
        if(s->opcode==QA_QC_ADDRESS&&!(s->a==other&&(source_field(a,s->b,type)||
            source_field(a,s->b,fields->armorvalue)||source_field(a,s->b,fields->items))))return false;
        if((s->opcode>=QA_QC_CALL0&&s->opcode<=QA_QC_CALL8)||s->opcode==QA_QC_STATE||s->opcode==QA_QC_STOREP_V||s->opcode==QA_QC_STOREP_S||
            s->opcode==QA_QC_STOREP_ENT||s->opcode==QA_QC_STOREP_FLD||s->opcode==QA_QC_STOREP_FN)return false;
    }
    return source_decision(a,c,decision,grant-2,e)&&source_region(a,c,grant,join,
        (qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e);
}
/* Both compilers follow the original lvalue/load/add/store data flow. Numeric
 * amounts, limits and leave rules stay in the admitted program. */
static bool source_ammunition(qc_pickup_analysis *a,qc_pickup_caller *c,qa_error *e)
{
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_definition *quantity=qa_qc_program_find_field(a->program,"aflag");
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),other=a->profile->other->offset;
    c->discriminator=fields->weapon;
    for(uint32_t pc=first;pc+11<end;++pc){const qa_qc_statement *s=source_at(a,pc),*test=source_at(a,pc+1),*branch=source_at(a,pc+2);
        if(!source_load(a,s,QA_QC_LOAD_F,a->profile->self->offset,fields->weapon)||test->opcode!=QA_QC_EQ_F||test->a!=s->c||
            branch->opcode!=QA_QC_IFNOT||branch->a!=test->c)continue;
        const qa_qc_statement *load=source_at(a,pc+3),*compare=source_at(a,pc+4),*address=source_at(a,pc+7),
            *before=source_at(a,pc+8),*amount=source_at(a,pc+9),*add=source_at(a,pc+10),*store=source_at(a,pc+11);
        qc_pickup_descriptor d={.quantity_field=true,.quantity=quantity?quantity->offset:0};
        const qa_qc_definition *counter=source_ammo(a,load->b,&d.item,e);
        d.ammo=d.item;d.resource=(qa_pickup_resource){.kind=QA_PICKUP_INVENTORY,.item=d.item};
        if(!counter||!source_float(a,test->b,&d.value)||!source_load(a,load,QA_QC_LOAD_F,other,counter)||
            compare->opcode!=QA_QC_GE||compare->a!=load->c||!source_branch_return(a,pc+5,compare->c)||
            !source_load(a,address,QA_QC_ADDRESS,other,counter)||!source_load(a,before,QA_QC_LOAD_F,other,counter)||
            !source_load(a,amount,QA_QC_LOAD_F,a->profile->self->offset,quantity)||add->opcode!=QA_QC_ADD_F||
            add->a!=before->c||add->b!=amount->c||store->opcode!=QA_QC_STOREP_F||store->a!=add->c||store->b!=address->c||
            !source_descriptor(c,d,e)||!source_decision(a,c,pc+3,pc+5,e)||!source_region(a,c,pc+7,pc+12,
                (qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e))return false;
    }
    return c->count!=0;
}
static bool source_weapons(qc_pickup_analysis *a,qc_pickup_caller *c,qa_error *e)
{
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),other=a->profile->other->offset;
    c->discriminator=fields->classname;
    for(uint32_t pc=first;pc+14<end;++pc){const qa_qc_statement *s=source_at(a,pc),*test=source_at(a,pc+1),*branch=source_at(a,pc+2);
        if(!source_load(a,s,QA_QC_LOAD_S,a->profile->self->offset,fields->classname)||test->opcode!=QA_QC_EQ_S||test->a!=s->c||
            branch->opcode!=QA_QC_IFNOT||branch->a!=test->c)continue;
        const qa_qc_statement *items=source_at(a,pc+3),*bits=source_at(a,pc+4),*leave=source_at(a,pc+5),
            *address=source_at(a,pc+11),*before=source_at(a,pc+12),*add=source_at(a,pc+13),*store=source_at(a,pc+14);
        qc_pickup_descriptor d={0};float bit;
        const qa_qc_definition *counter=source_ammo(a,address->b,&d.ammo,e);
        d.quantity=add->b;d.leave=leave->a;
        if(!source_name(a,test->b,&d.name,e)||!source_load(a,items,QA_QC_LOAD_F,other,fields->items)||
            bits->opcode!=QA_QC_BITAND||bits->a!=items->c||!source_weapon(a,bits->b,&d.item,&bit,e)||
            leave->opcode!=QA_QC_AND||leave->b!=bits->c||!source_branch_return(a,pc+6,leave->c)||
            !counter||!source_load(a,address,QA_QC_ADDRESS,other,counter)||!source_load(a,before,QA_QC_LOAD_F,other,counter)||
            add->opcode!=QA_QC_ADD_F||add->a!=before->c||store->opcode!=QA_QC_STOREP_F||store->a!=add->c||store->b!=address->c)return false;
        float amount;if(!source_float(a,d.quantity,&amount)||amount<0)return false;
        d.resource=(qa_pickup_resource){.kind=QA_PICKUP_INVENTORY,.item=d.item};
        if(!source_descriptor(c,d,e)||!source_decision(a,c,pc+3,pc+6,e)||!source_region(a,c,pc+11,pc+15,
            (qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e))return false;
    }
    return c->count!=0;
}
static bool source_backpack(qc_pickup_analysis *a,qc_pickup_caller *c,qa_error *e)
{
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    const qa_qc_definition *mode=qa_qc_program_find_global(a->program,"deathmatch");
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),
        other=a->profile->other->offset,self=a->profile->self->offset;
    qc_pickup_descriptor descriptor={0};
    if(!qa_strings_intern_cstr(qa_session_strings(a->provider->application->session),"q1:item_backpack",&descriptor.item,e)||
        !source_descriptor(c,descriptor,e))return false;
    bool admission=false,ownership=false;size_t counters=0;
    for(uint32_t pc=first;pc+6<end;++pc){const qa_qc_statement *s=source_at(a,pc),*next=source_at(a,pc+1);
        if(source_load(a,s,QA_QC_LOAD_F,self,fields->items)&&next->opcode==QA_QC_IFNOT&&next->a==s->c&&
            source_load(a,source_at(a,pc+2),QA_QC_LOAD_F,other,fields->items)){
            const qa_qc_statement *carried=source_at(a,pc+3),*bits=source_at(a,pc+4),*test=source_at(a,pc+5),*branch=source_at(a,pc+6);
            float zero;
            if(!source_load(a,carried,QA_QC_LOAD_F,self,fields->items)||bits->opcode!=QA_QC_BITAND||
                bits->a!=source_at(a,pc+2)->c||bits->b!=carried->c||test->opcode!=QA_QC_EQ_F||
                test->a!=bits->c||!source_float(a,test->b,&zero)||zero!=0||branch->opcode!=QA_QC_IFNOT||branch->a!=test->c)return false;
            if(!source_region(a,c,pc,pc+1,(qc_pickup_source_region){.operation=QC_PICKUP_ADMISSION},false,e)||
                !source_region(a,c,pc+2,pc+6,(qc_pickup_source_region){.operation=QC_PICKUP_CARGO_OWNERSHIP,.word=test->c},false,e))return false;
            admission=ownership=true;
        }
        if(s->opcode==QA_QC_EQ_F&&mode&&mode->type==QA_QC_FLOAT&&s->a==mode->offset&&
            next->opcode==QA_QC_IFNOT&&next->a==s->c&&source_load(a,source_at(a,pc+2),QA_QC_ADDRESS,other,fields->health)){
            if(c->effect_word||!source_float(a,s->b,&c->effect_value))return false;
            c->effect_word=mode->offset;
        }
        if(s->opcode!=QA_QC_ADDRESS||s->a!=other)continue;
        qa_item_id item;const qa_qc_definition *counter=source_ammo(a,s->b,&item,e);
        if(!counter)continue;
        const qa_qc_statement *load=source_at(a,pc+1),*amount=source_at(a,pc+2),*add=source_at(a,pc+3),*store=source_at(a,pc+4);
        if(source_load(a,load,QA_QC_LOAD_F,other,counter)&&source_load(a,amount,QA_QC_LOAD_F,self,counter)&&
            add->opcode==QA_QC_ADD_F&&add->a==load->c&&add->b==amount->c&&
            store->opcode==QA_QC_STOREP_F&&store->a==add->c&&store->b==s->c){
            if(!source_region(a,c,pc,pc+5,(qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e))return false;
            ++counters;
        }
    }
    return admission&&ownership&&counters==4;
}
static bool source_pure_code(const qc_pickup_analysis *a,uint32_t function)
{
    const qa_qc_function *f=qa_qc_program_function(a->program,function);
    if(!f||f->first_statement<=0||f->named_builtin)return false;
    for(uint32_t pc=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f);pc<end;++pc){
        const qa_qc_statement *s=source_at(a,pc);qa_qc_statement_access access;
        if((s->opcode>=QA_QC_STOREP_F&&s->opcode<=QA_QC_STOREP_FN)||s->opcode==QA_QC_STATE||
            (s->opcode>=QA_QC_CALL0&&s->opcode<=QA_QC_CALL8)||!qa_qc_program_statement_access(a->program,pc,&access,NULL))return false;
        for(uint32_t i=0;i<access.write_count;++i){uint32_t word=access.write[i];
            if(word<=27||(word>=f->parameter_start&&word-f->parameter_start<f->local_words))continue;
            qa_qc_program_info info=qa_qc_program_describe(a->program);
            for(uint32_t j=0;j<info.global_count;++j){const qa_qc_definition *d=qa_qc_program_global(a->program,j);
                if(d->offset==word&&*d->name)return false;}
            for(uint32_t j=0;j<info.statement_count;++j){qa_qc_statement_access outside;
                if(j>=(uint32_t)f->first_statement&&j<end)continue;
                if(!qa_qc_program_statement_access(a->program,j,&outside,NULL))return false;
                for(uint32_t k=0;k<outside.read_count;++k)if(outside.read[k]==word)return false;}
        }
    }
    return true;
}
static bool source_ammo_clamp(const qc_pickup_analysis *a,uint32_t function)
{
    const qa_qc_function *f=qa_qc_program_function(a->program,function);
    if(!f)return false;
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),other=a->profile->other->offset;
    size_t count=0;
    for(uint32_t pc=first;pc<end;){const qa_qc_statement *load=source_at(a,pc);
        if(load->opcode==QA_QC_DONE&&!load->a&&pc+1==end)return count==4;
        if(pc+4>=end)return false;
        qa_item_id item;const qa_qc_definition *field=source_ammo(a,load->b,&item,NULL);
        const qa_qc_statement *compare=source_at(a,pc+1),*branch=source_at(a,pc+2),*address=source_at(a,pc+3),*store=source_at(a,pc+4);
        float capacity;
        if(!field||!source_load(a,load,QA_QC_LOAD_F,other,field)||compare->opcode!=QA_QC_GT||compare->a!=load->c||
            !source_float(a,compare->b,&capacity)||branch->opcode!=QA_QC_IFNOT||branch->a!=compare->c||(int16_t)branch->b!=3||
            !source_load(a,address,QA_QC_ADDRESS,other,field)||store->opcode!=QA_QC_STOREP_F||store->a!=compare->b||store->b!=address->c)return false;
        ++count;pc+=5;
    }
    return false;
}
static bool source_continuations(qc_pickup_analysis *a,qc_pickup_caller *c,qa_error *e)
{
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_function *f=qa_qc_program_function(a->program,c->function);
    uint32_t first=(uint32_t)f->first_statement,end=qa_qc_program_function_end(a->program,f),
        self=a->profile->self->offset,other=a->profile->other->offset;
    uint32_t clamp=source_function(a,"bound_other_ammo",0,false),clamp_pc=0;
    if(!source_ammo_clamp(a,clamp))return false;
    size_t items=0,counter_count=0;bool ammo_selection=false;
    uint32_t admission=first;
    for(size_t i=0;i<a->profile->region_count;++i)if(a->profile->regions[i].function==c->function&&
        a->profile->operations[i].operation==QC_PICKUP_ADMISSION)admission=a->profile->regions[i].entry;
    for(uint32_t pc=first;pc+1<end;++pc){const qa_qc_statement *s=source_at(a,pc),*next=source_at(a,pc+1);
        if(source_call(a,s,clamp)){
            if(clamp_pc||!source_region(a,c,pc,pc+1,(qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e))return false;
            clamp_pc=pc;
        }
        if(c->kind==QC_PICKUP_AMMO&&clamp_pc&&source_load(a,s,QA_QC_LOAD_F,other,fields->weapon)){
            uint32_t join=pc+1;
            while(join<end&&!source_load(a,source_at(a,join),QA_QC_ADDRESS,self,fields->model))++join;
            if(join==end||ammo_selection||!source_region(a,c,pc,join,
                (qc_pickup_source_region){.operation=QC_PICKUP_SELECTION},false,e))return false;
            ammo_selection=true;pc=join;continue;
        }
        if(c->kind==QC_PICKUP_AMMO)continue;
        if(pc>=admission&&source_load(a,s,QA_QC_ADDRESS,other,fields->items)){
            uint32_t join=pc+1;
            while(join<end&&source_at(a,join)->opcode!=QA_QC_STOREP_F){qa_qc_opcode opcode=source_at(a,join)->opcode;
                if((opcode>=QA_QC_CALL0&&opcode<=QA_QC_CALL8)||opcode==QA_QC_STATE||opcode==QA_QC_RETURN||opcode==QA_QC_DONE)break;
                ++join;}
            if(join==end||source_at(a,join)->opcode!=QA_QC_STOREP_F||source_at(a,join)->b!=s->c||
                !source_region(a,c,pc,join+1,(qc_pickup_source_region){.operation=QC_PICKUP_GRANT},false,e))return false;
            ++items;
        }
        if(source_load(a,s,QA_QC_ADDRESS,self,fields->weapon)&&next->opcode==QA_QC_STOREP_F&&next->b==s->c){
            if(!source_region(a,c,pc,pc+2,(qc_pickup_source_region){.operation=QC_PICKUP_SET_WEAPON,.word=next->a},false,e))return false;
        }
        if(c->kind!=QC_PICKUP_BACKPACK)continue;
        if(source_load(a,s,QA_QC_LOAD_F,other,fields->weapon)&&next->opcode==QA_QC_STORE_F&&next->a==s->c){
            if(!source_region(a,c,pc,pc+2,(qc_pickup_source_region){.operation=QC_PICKUP_CARGO_CURRENT,.word=next->b},false,e))return false;
        }
        if(s->opcode!=QA_QC_ADDRESS||s->a!=other||next->opcode!=QA_QC_STOREP_F||next->b!=s->c)continue;
        qa_item_id item;const qa_qc_definition *field=source_ammo(a,s->b,&item,e);if(!field)continue;
        uint32_t entry=pc,exit=pc+2;float constant;
        if(!source_float(a,next->a,&constant))return false;
        /* The conditional floor includes its original predicate and WeaponCode
         * calls; the DM4 reset is the actual two-statement assignment. */
        const qa_qc_statement *branch=pc>first?source_at(a,pc-1):NULL;
        if(branch&&branch->opcode==QA_QC_IFNOT&&(int16_t)branch->b==3){
            const qa_qc_definition *mode=qa_qc_program_find_global(a->program,"deathmatch");
            const qa_qc_definition *code=qa_qc_program_find_global(a->program,"WeaponCode");int32_t code_function;
            if(!mode||!code||!source_initial(a,code->offset,QA_QC_FUNCTION,&code_function)||
                code_function<=0||!source_pure_code(a,(uint32_t)code_function))return false;
            bool found=false;
            for(uint32_t scan=pc-1;scan>first;){const qa_qc_statement *part=source_at(a,--scan);
                if(part->opcode==QA_QC_EQ_F&&part->a==mode->offset){entry=scan;found=true;}
                if(part->opcode==QA_QC_IF||part->opcode==QA_QC_IFNOT||part->opcode==QA_QC_GOTO||
                    (part->opcode>=QA_QC_STOREP_F&&part->opcode<=QA_QC_STOREP_FN))break;
                if(part->opcode>=QA_QC_CALL0&&part->opcode<=QA_QC_CALL8&&!source_call(a,part,(uint32_t)code_function))break;
            }
            if(!found)return false;
            for(uint32_t scan=entry;scan<pc;++scan){qa_qc_statement_access access;
                if(!qa_qc_program_statement_access(a->program,scan,&access,e))return false;
                for(uint32_t i=0;i<access.write_count;++i){uint32_t word=access.write[i];
                    if(word<=27||(word>=f->parameter_start&&word-f->parameter_start<f->local_words))continue;
                    qa_qc_program_info info=qa_qc_program_describe(a->program);
                    for(uint32_t j=0;j<info.global_count;++j){const qa_qc_definition *d=qa_qc_program_global(a->program,j);
                        if(d->offset==word&&*d->name)return false;}}}
        }
        if(!source_region(a,c,entry,exit,(qc_pickup_source_region){.operation=QC_PICKUP_COUNTER,.field=field,.item=item},true,e))return false;
        ++counter_count;
    }
    return clamp_pc&&(c->kind==QC_PICKUP_AMMO?ammo_selection:items==1)&&
        (!c->effect_word||counter_count==2);
}
static bool source_current_ammo_regions(qc_pickup_analysis *a,qa_error *e)
{
    struct application_qc_pickups *p=a->profile;
    if(!p->set_current_ammo)return true;
    const qa_qc_function *f=qa_qc_program_function(a->program,p->set_current_ammo);
    const qa_qc_game_fields *fields=qa_qc_program_resolved_fields(a->program);
    const qa_qc_definition *owned[]={fields->items,fields->currentammo,fields->weaponmodel,fields->weaponframe};
    qc_pickup_caller caller={.function=p->set_current_ammo};
    uint32_t end=qa_qc_program_function_end(a->program,f);
    for(uint32_t pc=(uint32_t)f->first_statement;pc+1<end;++pc){const qa_qc_statement *address=source_at(a,pc);
        bool field=false;
        for(size_t i=0;i<sizeof(owned)/sizeof(*owned);++i)
            field|=source_load(a,address,QA_QC_ADDRESS,p->self->offset,owned[i]);
        if(!field)continue;
        uint32_t store=pc+1;bool found=false;
        for(;store<end;++store){const qa_qc_statement *s=source_at(a,store);
            if((s->opcode==QA_QC_STOREP_F||s->opcode==QA_QC_STOREP_S)&&s->b==address->c){found=true;break;}
            if(s->opcode==QA_QC_ADDRESS||(s->opcode>=QA_QC_STORE_F&&s->opcode<=QA_QC_STOREP_FN)||
                s->opcode==QA_QC_RETURN||s->opcode==QA_QC_DONE||(s->opcode>=QA_QC_IF&&s->opcode<=QA_QC_GOTO))break;
            qa_qc_statement_access access;
            if(!qa_qc_program_statement_access(a->program,store,&access,e))return false;
            qa_qc_program_info info=qa_qc_program_describe(a->program);
            for(uint32_t i=0;i<access.write_count;++i)for(uint32_t j=0;j<info.global_count;++j){
                const qa_qc_definition *d=qa_qc_program_global(a->program,j);
                if(d->offset==access.write[i]&&*d->name)found=true;
            }
            if(found){found=false;break;}
        }
        if(found){qa_error optional={0};
            if(!source_region(a,&caller,pc,store+1,(qc_pickup_source_region){.operation=QC_PICKUP_SELECTION},false,&optional)&&
                optional.code==QA_ERROR_MEMORY){if(e)*e=optional;return false;}}
    }
    return true;
}
static bool source_compile(application_provider *provider,struct application_qc_pickups *p,qa_error *e)
{
    const qa_qc_program *program=provider->state.qc.program;qa_qc_program_info info=qa_qc_program_describe(program);
    p->self=qa_qc_program_find_global(program,"self");p->other=qa_qc_program_find_global(program,"other");
    p->time=qa_qc_program_find_global(program,"time");p->items=qa_qc_program_find_field(program,"items");
    if(!p->self||p->self->type!=QA_QC_ENTITY||!p->other||p->other->type!=QA_QC_ENTITY||
        !p->time||p->time->type!=QA_QC_FLOAT||!p->items||p->items->type!=QA_QC_FLOAT)return true;
    qc_pickup_analysis a={.provider=provider,.profile=p,.program=program,.written=calloc(info.global_words,1)};
    if(!a.written)return application_fail(e,QA_ERROR_MEMORY,"Qualifying immutable QC pickup operands");
    bool ok=true;
    for(uint32_t pc=0;ok&&pc<info.statement_count;++pc){qa_qc_statement_access access;
        ok=qa_qc_program_statement_access(program,pc,&access,e);
        for(uint32_t i=0;ok&&i<access.write_count;++i)a.written[access.write[i]]=1;
    }
    p->remove=source_function(&a,"remove",1,true);
    p->deathmatch_weapon=source_function(&a,"Deathmatch_Weapon",2,false);
    p->set_current_ammo=source_function(&a,"W_SetCurrentAmmo",0,false);
    static const char *const names[]={"armor_touch","ammo_touch","weapon_touch","BackpackTouch"};
    for(size_t i=0;ok&&i<4;++i){qc_pickup_caller *c=p->callers+i;
        c->function=source_function(&a,names[i],0,false);c->kind=(qc_pickup_source_kind)i;
        if(!c->function)continue;
        size_t before=p->region_count;qa_error optional={0};
        bool found=i==QC_PICKUP_ARMOR?source_armor(&a,c,&optional):i==QC_PICKUP_AMMO?source_ammunition(&a,c,&optional):
            i==QC_PICKUP_WEAPON?source_weapons(&a,c,&optional):source_backpack(&a,c,&optional);
        if(found&&i!=QC_PICKUP_ARMOR)found=source_continuations(&a,c,&optional);
        if(found&&i==QC_PICKUP_BACKPACK&&!p->remove)found=false;
        if(!found){free(c->descriptors);*c=(qc_pickup_caller){0};p->region_count=before;
            if(optional.code==QA_ERROR_MEMORY){if(e)*e=optional;ok=false;}}
    }
    if(ok)ok=source_current_ammo_regions(&a,e);
    free(a.written);return ok;
}

struct qc_pickup_source_frame {
    qc_pickup_source_frame *previous;
    struct application_qc_state *engine;
    qa_qc_instance *vm;
    const qc_pickup_caller *caller;
    const qc_pickup_descriptor *descriptor;
    qa_qc_call_next next;
    qa_pickup_offer offer;
    qa_pickup_execution *execution;
    qa_pickup_selection selection;
    int32_t pickup_reference,recipient_reference;
    uint32_t recipient_slot;
    qa_pickup_grant *grants;
    size_t grant_count;
    qa_item_id cargo_weapon;
    bool accepted,new_weapon,foreign_arsenal;
};
const qa_qc_inline_region *application_qc_pickups_regions(const application_provider *provider,size_t *count)
{
    const struct application_qc_pickups *p=provider->state.qc.pickup_profile;
    *count=p?p->region_count:0;return p?p->regions:NULL;
}
bool application_qc_pickups_idle(const struct application_qc_state *engine)
{
    const struct application_qc_pickup_store *store=engine?engine->pickup_store:NULL;
    if(!store)return true;
    if(store->source||store->projection)return false;
    for(const struct application_qc_pickup_actor *a=store->head;a;a=a->next)if(a->calls)return false;
    return true;
}
qa_item_id application_qc_pickups_cargo_item(const application_provider *provider)
{
    const struct application_qc_pickups *p=provider->state.qc.pickup_profile;
    return p&&p->callers[QC_PICKUP_BACKPACK].function?p->callers[QC_PICKUP_BACKPACK].descriptors[0].item:0;
}
bool application_qc_pickups_source_present(const application_provider *provider)
{
    const struct application_qc_pickups *p=provider->state.qc.pickup_profile;
    if(p)for(size_t i=0;i<4;++i)if(p->callers[i].function)return true;
    return false;
}
static bool source_cancel(qc_pickup_source_frame *frame,qa_error *e)
{ return qa_qc_call_cancel(frame->next,(uint32_t[3]){0},e); }
static bool source_selected(qc_pickup_source_frame *frame,float value,qa_pickup_selection_mode mode,bool *found,qa_error *e)
{
    struct application_qc_state *engine=frame->engine;const struct application_qc_items *items=engine->items;
    *found=false;
    if(!items||!isfinite(value)||value<0||(double)value>UINT32_MAX||truncf(value)!=value)return true;
    for(size_t i=0;i<items->storage_count;++i){const application_qc_item_storage *s=items->storage+i;
        if(!s->bits||s->field!=engine->field_bindings->items)continue;
        for(size_t j=0;j<s->count;++j)if(s->items[j].mask==(uint32_t)value){qa_supply *supply;
            *found=true;
            return application_supplies_source_for(engine->provider,frame->offer.recipient,&supply,e)&&
                qa_supply_select_weapon(supply,frame->offer.recipient,s->items[j].item,mode,e);}
    }
    return true;
}
static bool source_current_weapon(qc_pickup_source_frame *frame,float *out,qa_error *e)
{
    struct application_qc_state *engine=frame->engine;qa_application *app=engine->provider->application;
    qa_item_id selected,source=0;qa_item_definition definition;bool found=false;
    *out=0;
    if(!qa_application_weapon_read(app,frame->offer.recipient,&selected,e))return false;
    if(!selected)return true;
    application_provider *arsenal=application_provider_for(app,frame->offer.recipient,QA_ROLE_ARSENAL,"");
    if(!arsenal||!qa_inventory_item_definition_find(engine->services.inventory,frame->offer.recipient,
        &arsenal->owner,selected,&definition,&found,e))return false;
    if(!found||!definition.weapon)return true;
    const struct application_qc_items *items=engine->items;
    if(!items)return true;
    qa_arena *arena=qa_qc_scratch(frame->vm);
    qa_supply_weapon *original=qa_arena_alloc(arena,items->definition_count*sizeof(*original),_Alignof(qa_supply_weapon),e);
    if(items->definition_count&&!original)return false;
    size_t count=0;
    for(size_t i=0;i<items->definition_count;++i){const qa_item_definition *d=&items->admissions[i].definition;
        if(d->weapon)original[count++]=(qa_supply_weapon){.item=d->item,.ammo=d->ammo,.drop=true};}
    qa_supply_weapon one={.item=selected,.ammo=definition.ammo,.drop=true};
    if(!application_supplies_weapon_sources(app->supplies,engine->provider,frame->offer.recipient,
        &one,1,original,count,arena,&source,e))return false;
    for(size_t i=0;i<items->storage_count;++i){const application_qc_item_storage *s=items->storage+i;
        if(!s->bits||s->field!=engine->field_bindings->items)continue;
        for(size_t j=0;j<s->count;++j)if(s->items[j].item==source){*out=(float)s->items[j].mask;return true;}}
    return true;
}
static bool source_actor(qa_qc_instance *vm,int32_t reference,qa_actor_id *out)
{ return qa_qc_reference_actor(vm,reference,out,NULL)&&out->registry; }
static bool source_run(void *context,const qa_pickup_offer *offer,qa_pickup_selection selection,
    qa_pickup_execution *execution,qa_error *e)
{
    qc_pickup_source_frame *frame=context;struct application_qc_pickup_store *store=frame->engine->pickup_store;
    if(selection==QA_PICKUP_SELECT_STALE)return qa_qc_call_skip(frame->next,(uint32_t[3]){0},e);
    frame->offer=*offer;frame->selection=selection;frame->execution=execution;
    frame->accepted=selection==QA_PICKUP_SELECT_ORIGINAL;
    frame->previous=store->source;store->source=frame;
    bool ok=qa_qc_call_continue(frame->next,e);
    store->source=frame->previous;return ok;
}
static bool source_remove(void *context,qa_error *e)
{ return qa_qc_call_continue(*(qa_qc_call_next *)context,e); }
static bool source_cargo(qc_pickup_source_frame *frame,qa_error *e)
{
    struct application_qc_state *engine=frame->engine;const struct application_qc_items *items=engine->items;
    if(!items)return false;
    qa_arena *arena=qa_qc_scratch(frame->vm);
    qa_pickup_cargo *cargo=qa_arena_alloc(arena,(items->storage_count+1)*sizeof(*cargo),_Alignof(qa_pickup_cargo),e);
    frame->grants=qa_arena_alloc(arena,items->storage_count*sizeof(*frame->grants),_Alignof(qa_pickup_grant),e);
    if(!cargo||(items->storage_count&&!frame->grants))return false;
    float carried;
    if(!qa_qc_entity_float(frame->vm,frame->pickup_reference,engine->field_bindings->items->offset,&carried,e))return false;
    size_t count=0;
    for(size_t i=0;i<items->storage_count;++i){const application_qc_item_storage *s=items->storage+i;
        if(s->bits){
            if(s->field!=engine->field_bindings->items)continue;
            for(size_t j=0;j<s->count;++j)if(carried==(float)s->items[j].mask){
                frame->cargo_weapon=s->items[j].item;
                cargo[count++]=(qa_pickup_cargo){.item=frame->cargo_weapon,.count=1,.weapon=true};break;}
        }else{
            const qa_qc_definition *f=engine->field_bindings->ammo_shells;
            if(s->field!=f&&s->field!=engine->field_bindings->ammo_nails&&s->field!=engine->field_bindings->ammo_rockets&&
                s->field!=engine->field_bindings->ammo_cells)continue;
            float amount;
            if(!qa_qc_entity_float(frame->vm,frame->pickup_reference,s->field->offset,&amount,e))return false;
            if(!isfinite(amount)||amount<0)return false;
            cargo[count++]=(qa_pickup_cargo){.item=s->item,.count=amount};
            frame->grants[frame->grant_count++]=(qa_pickup_grant){s->item,amount};
        }
    }
    if(carried!=0&&!frame->cargo_weapon)return false;
    frame->offer.cargo=cargo;frame->offer.cargo_count=count;
    if(frame->cargo_weapon){
        if(frame->foreign_arsenal){qa_supply *supply;bool owns;
            if(!application_supplies_source_for(engine->provider,frame->offer.recipient,&supply,e)||
                !qa_supply_owns(supply,frame->offer.recipient,frame->cargo_weapon,&owns,e))return false;
            frame->new_weapon=!owns;
        }else{float owned;
            if(!qa_qc_entity_float(frame->vm,frame->recipient_reference,engine->field_bindings->items->offset,&owned,e))return false;
            if(!isfinite(owned)||owned<0||(double)owned>UINT32_MAX||truncf(owned)!=owned)return false;
            frame->new_weapon=((uint32_t)owned&(uint32_t)carried)==0;
        }
    }
    return true;
}
bool application_qc_pickups_replace(struct application_qc_state *engine,qa_qc_instance *vm,
    const qa_qc_call_event *event,qa_qc_call_next next,bool *handled,qa_error *e)
{
    *handled=false;struct application_qc_pickup_store *store=engine->pickup_store;
    const struct application_qc_pickups *p=engine->provider->state.qc.pickup_profile;
    if(!store||!p)return true;
    qc_pickup_source_frame *active=store->source;
    if(active&&active->caller&&event->caller==active->caller->function){
        if(event->function==p->remove){int32_t reference;
            if(!qa_qc_arg_int(vm,0,&reference,e))return false;
            if(reference==active->pickup_reference){*handled=true;
                return qa_pickup_consume(active->execution,source_remove,&next,e);}
        }
        if(active->foreign_arsenal&&event->function==p->deathmatch_weapon&&active->accepted){float value;bool selected;
            if(!qa_qc_arg_float(vm,1,&value,e)||!source_selected(active,value,QA_PICKUP_SWITCH_IF_BETTER,&selected,e))return false;
            if(selected){*handled=true;return qa_qc_call_skip(next,(uint32_t[3]){0},e);}
        }
    }
    const qc_pickup_caller *caller=NULL;
    for(size_t i=0;i<4;++i)if(p->callers[i].function&&p->callers[i].function==event->function){caller=p->callers+i;break;}
    if(!caller)return true;
    *handled=true;
    qc_pickup_source_frame frame={.engine=engine,.vm=vm,.caller=caller,.next=next};
    if(!qa_qc_global_int(vm,p->self->offset,&frame.pickup_reference,e)||
        !qa_qc_global_int(vm,p->other->offset,&frame.recipient_reference,e))return false;
    qa_actor_id pickup={0},recipient={0};
    bool known=source_actor(vm,frame.pickup_reference,&pickup)&&source_actor(vm,frame.recipient_reference,&recipient);
    if(known&&caller->kind==QC_PICKUP_BACKPACK)frame.descriptor=caller->descriptors;
    else if(known&&caller->discriminator){
        if(caller->discriminator->type==QA_QC_STRING){int32_t name;qa_string_id identity;
            if(!qa_qc_entity_int(vm,frame.pickup_reference,caller->discriminator->offset,&name,e)||!qa_qc_string_identity(vm,name,&identity,e))return false;
            for(size_t i=0;i<caller->count;++i)if(caller->descriptors[i].name==identity){frame.descriptor=caller->descriptors+i;break;}
        }else{float value;
            if(!qa_qc_entity_float(vm,frame.pickup_reference,caller->discriminator->offset,&value,e))return false;
            for(size_t i=0;i<caller->count;++i)if(caller->descriptors[i].value==value){frame.descriptor=caller->descriptors+i;break;}}
    }
    if(frame.descriptor&&frame.descriptor->quantity_field){float amount;
        if(!qa_qc_entity_float(vm,frame.pickup_reference,frame.descriptor->quantity,&amount,e))return false;
        if(!isfinite(amount)||amount<0)frame.descriptor=NULL;
    }
    if(!frame.descriptor){qc_pickup_source_frame *previous=store->source;store->source=NULL;
        bool ok=qa_qc_call_continue(next,e);store->source=previous;return ok;}
    float seconds;
    if(!qa_qc_global_float(vm,p->time->offset,&seconds,e)||!isfinite(seconds)||(double)seconds*1e9>=(double)UINT64_MAX)
        return application_fail(e,QA_ERROR_FORMAT,"QC pickup time exceeds its Source timeline");
    frame.foreign_arsenal=application_provider_for(engine->provider->application,recipient,QA_ROLE_ARSENAL,"")!=engine->provider;
    frame.offer=(qa_pickup_offer){.pickup=pickup,.recipient=recipient,.source=engine->provider->owner,
        .item=frame.descriptor->item,.default_resource=frame.descriptor->resource,
        .time_ns=seconds>0?(uint64_t)((double)seconds*1e9):0,.dropped=caller->kind==QC_PICKUP_BACKPACK};
    qa_actor_owner owner=0;
    if(frame.offer.default_resource.kind==QA_PICKUP_INVENTORY){
        if(qa_inventory_item_owner(engine->services.inventory,recipient,frame.offer.default_resource.item,&owner,NULL)&&owner==engine->provider->owner)
            frame.offer.default_resource=(qa_pickup_resource){0};
    }else if(frame.offer.default_resource.kind==QA_PICKUP_PROTECTION&&
        qa_combat_protection_owner(engine->services.combat,recipient,frame.offer.default_resource.channel,&owner,NULL)&&owner==engine->provider->owner)
        frame.offer.default_resource=(qa_pickup_resource){0};
    if(!qa_qc_actor_observation_slot(vm,recipient,&frame.recipient_slot,e))return false;
    bool source_effect=false;
    if(caller->effect_word){float mode;
        if(!qa_qc_global_float(vm,caller->effect_word,&mode,e))return false;
        source_effect=mode==caller->effect_value;
    }
    if(source_effect)frame.offer.grant=QA_PICKUP_SOURCE_EFFECT;
    else if(caller->kind==QC_PICKUP_BACKPACK){qa_error cargo_error={0};
        if(!source_cargo(&frame,&cargo_error)){
            if(cargo_error.code!=QA_OK){if(e)*e=cargo_error;return false;}
            qc_pickup_source_frame *previous=store->source;store->source=NULL;
            bool ok=qa_qc_call_continue(next,e);store->source=previous;return ok;
        }
    }
    return qa_pickups_run_source(engine->services.pickups,&frame.offer,source_run,&frame,e);
}
bool application_qc_pickups_supply(struct application_qc_state *engine,const qa_pickup_offer *offer,
    qa_supply_offer *out,qa_supply_options *options,qa_error *e)
{
    qc_pickup_source_frame *frame=engine->pickup_store?engine->pickup_store->source:NULL;
    if(!frame||!frame->caller||frame->descriptor->item!=offer->item||
        !qa_actor_id_equal(frame->offer.recipient,offer->recipient)||!qa_actor_id_equal(frame->offer.pickup,offer->pickup))
        return application_fail(e,QA_ERROR_ARGUMENT,"QC supply requires its entered original pickup caller");
    *options=(qa_supply_options){.selection=QA_PICKUP_SWITCH_NEVER,.auto_switch=true};
    if(frame->caller->kind==QC_PICKUP_BACKPACK){
        *out=(qa_supply_offer){.kind=frame->cargo_weapon?QA_SUPPLY_WEAPON:QA_SUPPLY_AMMO,.item=frame->cargo_weapon,
            .ammo=frame->grants,.ammo_count=frame->grant_count};return true;
    }
    const qc_pickup_descriptor *d=frame->descriptor;
    if(!d->ammo)return application_fail(e,QA_ERROR_ARGUMENT,"QC pickup has no original supply descriptor");
    float amount,leave=0;
    if(!(d->quantity_field?qa_qc_entity_float(frame->vm,frame->pickup_reference,d->quantity,&amount,e):
        qa_qc_global_float(frame->vm,d->quantity,&amount,e)))return false;
    if(d->leave&&!qa_qc_global_float(frame->vm,d->leave,&leave,e))return false;
    qa_pickup_grant *grant=qa_arena_alloc(qa_qc_scratch(frame->vm),sizeof(*grant),_Alignof(qa_pickup_grant),e);
    if(!grant)return false;
    *grant=(qa_pickup_grant){d->ammo,amount};
    options->only_empty=d->leave&&leave!=0;
    *out=(qa_supply_offer){.kind=frame->caller->kind==QC_PICKUP_WEAPON?QA_SUPPLY_WEAPON:QA_SUPPLY_AMMO,
        .item=d->item,.ammo=grant,.ammo_count=1};
    return true;
}
typedef struct qc_pickup_counter {
    qc_pickup_source_frame *frame;
    const qa_qc_inline_region *region;
    const qc_pickup_source_region *operation;
} qc_pickup_counter;
static bool source_counter_quantity(void *context,const qa_inventory_entry *entry,qa_supply_quantity *out,qa_error *e)
{
    qc_pickup_counter *counter=context;qc_pickup_source_frame *frame=counter->frame;
    struct application_qc_state *engine=frame->engine;struct application_qc_pickup_store *store=engine->pickup_store;
    int32_t saved;float result=0,after=0;
    if(!isfinite((float)entry->count))return application_fail(e,QA_ERROR_FORMAT,"Selected pickup counter exceeds its Source float ABI");
    if(!qa_qc_actor_observation_int(frame->vm,frame->recipient_slot,frame->offer.recipient,counter->operation->field->offset,&saved,e))return false;
    qc_pickup_projection projection={.region=counter->region,.reference=frame->recipient_reference,.word=counter->operation->field->offset};
    qc_pickup_projection *previous=store->projection;bool projecting=engine->projecting;
    engine->projecting=true;store->projection=&projection;
    bool ok=qa_qc_project_entity_float(frame->vm,frame->recipient_reference,projection.word,(float)entry->count,e)&&
        qa_qc_execute_region(frame->vm,counter->region,0,&result,e);
    if(ok&&projection.stored)ok=qa_qc_actor_observation_float(frame->vm,frame->recipient_slot,
        frame->offer.recipient,projection.word,&after,e);
    qa_error restore={0};bool restored=qa_qc_project_entity_int(frame->vm,frame->recipient_reference,projection.word,saved,&restore);
    store->projection=previous;engine->projecting=projecting;
    if(!restored){if(ok&&e)*e=restore;ok=false;}
    if(ok)*out=(qa_supply_quantity){.amount=projection.stored?(double)after-entry->count:0,.exact=true,.accepted=projection.stored};
    return ok;
}
bool application_qc_pickups_source_stored(struct application_qc_state *engine,qa_qc_instance *vm,
    const qa_qc_store_event *event,qa_error *e)
{
    (void)vm;(void)e;
    qc_pickup_projection *projection=engine->pickup_store?engine->pickup_store->projection:NULL;
    if(projection&&event->kind==QA_QC_STORE_ENTITY&&event->entity_reference==projection->reference&&
        event->word==projection->word&&event->count==1&&event->function==projection->region->function&&
        event->statement>=projection->region->entry&&event->statement<projection->region->exit)projection->stored=true;
    return true;
}
static bool source_counter(qc_pickup_source_frame *frame,const qa_qc_inline_region *region,
    const qc_pickup_source_region *operation,qa_error *e)
{
    qa_supply *supply;
    if(!application_supplies_source_for(frame->engine->provider,frame->offer.recipient,&supply,e))return false;
    qa_pickup_grant ammo={operation->item,0};
    qa_supply_offer offer={.kind=QA_SUPPLY_AMMO,.ammo=&ammo,.ammo_count=1};
    qc_pickup_counter counter={frame,region,operation};
    qa_supply_options options={.selection=QA_PICKUP_SWITCH_NEVER,.quantity=source_counter_quantity,.quantity_context=&counter};
    bool accepted;
    return qa_supply_apply(supply,frame->offer.recipient,&offer,&options,&accepted,e);
}
bool application_qc_pickups_inline(struct application_qc_state *engine,qa_qc_instance *vm,
    const qa_qc_inline_event *event,qa_qc_inline_next next,bool *handled,qa_error *e)
{
    *handled=false;const struct application_qc_pickups *p=engine->provider->state.qc.pickup_profile;
    struct application_qc_pickup_store *store=engine->pickup_store;
    if(!p||!store)return true;
    size_t index=0;
    while(index<p->region_count&&(p->regions[index].function!=event->region.function||p->regions[index].entry!=event->region.entry))++index;
    if(index==p->region_count)return true;
    *handled=true;qc_pickup_source_frame *frame=store->source;
    if(!frame||!frame->caller||(frame->caller->function!=event->region.function&&event->region.function!=p->set_current_ammo)||
        (store->projection&&store->projection->region->entry==event->region.entry&&
         store->projection->region->function==event->region.function))return qa_qc_inline_continue(next,e);
    const qc_pickup_source_region *operation=p->operations+index;
    if(!qa_pickup_current(frame->execution))return source_cancel(frame,e);
    if(operation->operation==QC_PICKUP_COUNTER&&frame->foreign_arsenal)
        return source_counter(frame,p->regions+index,operation,e)&&qa_qc_inline_skip_to_join(next,e);
    if(operation->operation==QC_PICKUP_SELECTION&&frame->foreign_arsenal)return qa_qc_inline_skip_to_join(next,e);
    if(operation->operation==QC_PICKUP_SET_WEAPON&&frame->foreign_arsenal&&frame->accepted){float value;bool selected;
        if(!qa_qc_global_float(vm,operation->word,&value,e)||!source_selected(frame,value,QA_PICKUP_SWITCH_ALWAYS,&selected,e))return false;
        return selected?qa_qc_inline_skip_to_join(next,e):qa_qc_inline_continue(next,e);
    }
    if(frame->selection==QA_PICKUP_SELECT_ORIGINAL)return qa_qc_inline_continue(next,e);
    if(operation->operation==QC_PICKUP_DECISION||operation->operation==QC_PICKUP_ADMISSION){
        if(frame->selection!=QA_PICKUP_SELECT_REPLACEMENT)return source_cancel(frame,e);
        if(frame->foreign_arsenal&&frame->caller->kind==QC_PICKUP_WEAPON&&frame->descriptor->leave){float leave;
            if(!qa_qc_global_float(vm,frame->descriptor->leave,&leave,e))return false;
            if(leave!=0){qa_supply *supply;bool owns;
                if(!application_supplies_source_for(engine->provider,frame->offer.recipient,&supply,e)||
                    !qa_supply_owns(supply,frame->offer.recipient,frame->descriptor->item,&owns,e))return false;
                if(owns)return source_cancel(frame,e);
            }
        }
        qa_pickup_outcome outcome;
        if(!qa_pickup_execute_grant(frame->execution,&outcome,e))return false;
        if(outcome!=QA_PICKUP_ACCEPTED||!qa_pickup_current(frame->execution))return source_cancel(frame,e);
        frame->accepted=true;
        if(operation->operation==QC_PICKUP_ADMISSION)return qa_qc_inline_continue(next,e);
        return qa_qc_set_global_float(vm,operation->word,0,e)&&qa_qc_inline_skip_to_join(next,e);
    }
    if(!frame->accepted)return source_cancel(frame,e);
    if(operation->operation==QC_PICKUP_CARGO_OWNERSHIP){
        if(!qa_qc_set_global_float(vm,operation->word,frame->new_weapon?1:0,e))return false;
    }else if(operation->operation==QC_PICKUP_CARGO_CURRENT){float current;
        if(!source_current_weapon(frame,&current,e)||!qa_qc_set_global_float(vm,operation->word,current,e))return false;
    }
    return qa_qc_inline_skip_to_join(next,e);
}
