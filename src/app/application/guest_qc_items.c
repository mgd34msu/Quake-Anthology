#include "guest_qc_items.h"
#include "guest_mod_item_definition.h"
#include "guest_qc_item_weapons.h"
#include <float.h>

struct application_qc_item_actor {
    struct application_qc_state *engine;
    qa_actor_id actor;
    int32_t reference;
    qa_inventory_lease lease;
    unsigned calls;
    struct application_qc_item_actor *next;
};
static bool list(const qa_json_document *d,qa_json_id id,size_t stride,void **out,size_t *count,qa_error *e)
{
    if(qa_json_type(d,id)!=QA_JSON_ARRAY)return application_fail(e,QA_ERROR_FORMAT,"QC items require an array");
    *count=qa_json_size(d,id);
    if(*count>SIZE_MAX/stride)return application_fail(e,QA_ERROR_MEMORY,"QC item declaration is too large");
    *out=*count?calloc(*count,stride):NULL;
    return !*count||*out||application_fail(e,QA_ERROR_MEMORY,"Owning QC item declarations");
}
static const qa_qc_definition *field(application_provider *p,const qa_json_document *d,qa_json_id id,qa_error *e)
{
    char *name=application_qc_declaration_string(d,id,e);
    const qa_qc_definition *out=name?qa_qc_program_find_field(p->state.qc.program,name):NULL;
    free(name);
    if(out&&out->type==QA_QC_FLOAT)
        for(size_t i=0;i<p->state.qc.qualified->field_count;++i){
            const application_qc_bound_field *f=p->state.qc.qualified->fields+i;
            if(f->definition==out&&f->kind==QC_FIELD_PRIVATE)return out;
        }
    application_fail(e,QA_ERROR_FORMAT,"QC item storage requires an actual declared private float field");return NULL;
}
static bool binary32(const qa_json_document *d,qa_json_id id,float *out,qa_error *e)
{
    double value;
    if(!qa_json_number(d,id,&value,e))return false;
    if(!isfinite(value)||fabs(value)>FLT_MAX||(double)(float)value!=value)
        return application_fail(e,QA_ERROR_FORMAT,"QC item value must be exactly representable as binary32");
    *out=(float)value;return true;
}
static bool mask(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{
    uint64_t value;
    if(!qa_json_u64(d,id,&value,e)||value>UINT32_C(0xffffff))return application_fail(e,QA_ERROR_FORMAT,"QC item mask exceeds exact Source float integer storage");
    *out=(uint32_t)value;return true;
}
static const application_qc_item_storage *storage_for(const struct application_qc_items *p,qa_item_id item,size_t *bit)
{
    for(size_t i=0;i<p->storage_count;++i){const application_qc_item_storage *s=p->storage+i;
        if(!s->bits&&s->item==item){*bit=0;return s;}
        for(size_t j=0;s->bits&&j<s->count;++j)if(s->items[j].item==item){*bit=j;return s;}
    }return NULL;
}
bool application_qc_items_qualify(application_provider *provider,const qa_json_document *d,qa_json_id root,qa_error *e)
{
    if(root==QA_JSON_NONE||(qa_json_type(d,root)==QA_JSON_ARRAY&&!qa_json_size(d,root)))return true;
    struct application_qc_profile *qualified=provider->state.qc.qualified;
    if(!qualified->clients)return application_fail(e,QA_ERROR_FORMAT,"QC items require declared actual clients");
    struct application_qc_items *p=calloc(1,sizeof(*p));
    if(!p)return application_fail(e,QA_ERROR_MEMORY,"Owning QC item profile");
    qualified->items=p;
    qa_json_id definitions=qa_json_get(d,root,"definitions"),storage=qa_json_get(d,root,"storage");
    if(!list(d,definitions,sizeof(*p->admissions),(void **)&p->admissions,&p->definition_count,e)||!p->definition_count||
        !list(d,storage,sizeof(*p->storage),(void **)&p->storage,&p->storage_count,e))return false;
    p->definitions=calloc(p->definition_count,sizeof(*p->definitions));
    if(!p->definitions)return application_fail(e,QA_ERROR_MEMORY,"Owning QC item actions and media");
    size_t weapons=0;uint64_t inputs=(UINT64_C(1)<<QC_INPUT_SELF)|(UINT64_C(1)<<QC_INPUT_TIME);
    for(size_t i=0;i<p->definition_count;++i){application_qc_item_definition *v=p->definitions+i;qa_json_id actions[2];
        if(!application_mod_item_definition(d,qa_json_at(d,definitions,i),qa_session_strings(provider->application->session),p->admissions+i,&v->icon,&v->held,actions,e))return false;
        qa_item_definition *item=&p->admissions[i].definition;item->owner=provider->owner;
        if(!*item->label)return application_fail(e,QA_ERROR_FORMAT,"QC item label must not be empty");
        weapons+=item->weapon?1u:0u;
        for(size_t j=0;j<i;++j)if(p->admissions[j].definition.item==item->item)return application_fail(e,QA_ERROR_FORMAT,"Duplicate QC item identity");
        for(size_t j=0;j<2;++j)if(actions[j]!=QA_JSON_NONE&&
            !application_qc_call_parse(d,actions[j],provider->state.qc.program,inputs,false,v->actions+j,e))return false;
    }
    size_t bound=0;
    for(size_t i=0;i<p->storage_count;++i){application_qc_item_storage *s=p->storage+i;qa_json_id row=qa_json_at(d,storage,i),kind=qa_json_get(d,row,"kind");
        s->bits=qa_json_string_equal(d,kind,"bits");s->field=field(provider,d,qa_json_get(d,row,"field"),e);
        if(!s->field||(!s->bits&&!qa_json_string_equal(d,kind,"counter")))return false;
        if(s->bits){qa_json_id bits=qa_json_get(d,row,"items");
            if(!mask(d,qa_json_get(d,row,"privateMask"),&s->private_mask,e)||
                !list(d,bits,sizeof(*s->items),(void **)&s->items,&s->count,e)||!s->count)return false;
            uint32_t occupied=s->private_mask;
            for(size_t j=0;j<s->count;++j){qa_json_id bit=qa_json_at(d,bits,j);application_qc_item_bit *b=s->items+j;
                if(!application_mod_item_identity(d,qa_json_get(d,bit,"item"),qa_session_strings(provider->application->session),&b->item,e)||
                    !mask(d,qa_json_get(d,bit,"mask"),&b->mask,e)||!b->mask||(b->mask&(b->mask-1))||(occupied&b->mask))return false;
                occupied|=b->mask;
            }
        }else{qa_json_id capacity=qa_json_get(d,row,"capacity"),capacity_kind=qa_json_get(d,capacity,"kind");
            if(!application_mod_item_identity(d,qa_json_get(d,row,"item"),qa_session_strings(provider->application->session),&s->item,e))return false;
            if(qa_json_string_equal(d,capacity_kind,"constant")){
                if(!binary32(d,qa_json_get(d,capacity,"value"),&s->constant_capacity,e)||s->constant_capacity<0)return false;
            }else if(qa_json_string_equal(d,capacity_kind,"field")){
                s->capacity=field(provider,d,qa_json_get(d,capacity,"field"),e);if(!s->capacity)return false;
            }else return application_fail(e,QA_ERROR_FORMAT,"Unknown QC item capacity declaration");
        }
        if(s->capacity==s->field)return application_fail(e,QA_ERROR_FORMAT,"QC item count overlaps its capacity");
        for(size_t j=0;j<i;++j){const application_qc_item_storage *prior=p->storage+j;
            if(prior->field==s->field||prior->capacity==s->field||s->capacity==prior->field)
                return application_fail(e,QA_ERROR_FORMAT,"QC item counts overlap declared storage");
        }
        for(size_t j=0;j<(s->bits?s->count:1);++j){qa_item_id item=s->bits?s->items[j].item:s->item;size_t matches=0;
            for(size_t k=0;k<p->definition_count;++k)matches+=p->admissions[k].definition.item==item?1u:0u;
            for(size_t k=0;k<i;++k){const application_qc_item_storage *prior=p->storage+k;
                if(!prior->bits&&prior->item==item)return application_fail(e,QA_ERROR_FORMAT,"Duplicate QC item storage");
                for(size_t n=0;prior->bits&&n<prior->count;++n)if(prior->items[n].item==item)return application_fail(e,QA_ERROR_FORMAT,"Duplicate QC bit item storage");
            }
            for(size_t k=0;s->bits&&k<j;++k)if(s->items[k].item==item)return application_fail(e,QA_ERROR_FORMAT,"Duplicate QC bit item identity");
            if(matches!=1)return application_fail(e,QA_ERROR_FORMAT,"QC storage has no declared item definition");
            ++bound;
        }
    }
    if(bound!=p->definition_count)return application_fail(e,QA_ERROR_FORMAT,"QC item definitions lack complete distinct storage");
    for(size_t i=0;i<p->definition_count;++i){qa_item_id ammo=p->admissions[i].definition.ammo;size_t bit;
        if(!ammo||storage_for(p,ammo,&bit))continue;
        bool found=false;for(size_t j=0;j<qualified->field_count;++j)found|=qualified->fields[j].kind==QC_FIELD_INVENTORY&&qualified->fields[j].item==ammo;
        if(!found)return application_fail(e,QA_ERROR_FORMAT,"QC weapon ammo has no actual inventory storage");
    }
    qa_json_id stage=qa_json_get(d,root,"weapons");
    if(!!weapons!=(stage!=QA_JSON_NONE))return application_fail(e,QA_ERROR_FORMAT,"QC weapon items require their actual Source stage");
    return stage==QA_JSON_NONE||application_qc_item_weapons_qualify(provider,d,stage,e);
}
void application_qc_items_profile_free(struct application_qc_items *p)
{
    if(!p)return;
    for(size_t i=0;i<p->definition_count;++i){if(p->admissions)free((void *)p->admissions[i].definition.label);
        if(p->definitions){for(size_t j=0;j<2;++j)application_qc_call_free(p->definitions[i].actions+j);
            qa_buffer_free(&p->definitions[i].icon);qa_buffer_free(&p->definitions[i].held);}}
    for(size_t i=0;p->storage&&i<p->storage_count;++i)free(p->storage[i].items);
    application_qc_item_weapons_profile_free(p->weapons);
    free(p->admissions);free(p->definitions);free(p->storage);free(p);
}
static struct application_qc_item_actor *actor_find(struct application_qc_state *engine,qa_actor_id actor)
{for(struct application_qc_item_actor *a=engine->item_actors;a;a=a->next)if(qa_actor_id_equal(a->actor,actor)&&a->lease.serial)return a;return NULL;}
static bool physical_current(struct application_qc_item_actor *a,qa_error *e)
{
    bool member=false;int32_t reference;
    return a&&application_qc_control_source_client(a->engine->provider,a->actor,&member,e)&&member&&
        application_qc_reference(a->engine,a->actor,&reference,e)&&reference==a->reference;
}
bool application_qc_items_actor_current(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    struct application_qc_item_actor *a=actor_find(engine,actor);
    return physical_current(a,e)&&qa_inventory_lease_current(engine->services.inventory,a->lease);
}
static const struct application_qc_items *profile_of(struct application_qc_item_actor *a)
{return a->engine->provider->state.qc.qualified->items;}
static bool scalar(struct application_qc_item_actor *a,const qa_qc_definition *f,const qa_qc_store_event *before,float *out,qa_error *e)
{
    if(before&&f->offset>=before->word&&f->offset<before->word+before->count)memcpy(out,before->before+f->offset-before->word,sizeof(*out));
    else if(!qa_qc_entity_float(a->engine->provider->state.qc.instance,a->reference,f->offset,out,e))return false;
    return isfinite(*out)||application_fail(e,QA_ERROR_FORMAT,"QC item storage is not finite");
}
static bool read_entry(struct application_qc_item_actor *a,const application_qc_item_storage *s,size_t bit,const qa_qc_store_event *before,qa_inventory_entry *out,qa_error *e)
{
    float count,capacity=s->constant_capacity;
    if(!physical_current(a,e)||!scalar(a,s->field,before,&count,e))return false;
    if(s->bits){uint32_t allowed=s->private_mask;for(size_t i=0;i<s->count;++i)allowed|=s->items[i].mask;
        if(count<0||count>16777215||truncf(count)!=count||((uint32_t)count&~allowed))return application_fail(e,QA_ERROR_FORMAT,"QC item bits leave declared exact Source storage");
        *out=(qa_inventory_entry){s->items[bit].item,((uint32_t)count&s->items[bit].mask)?1:0,1,QA_COUNT_STACK};return true;
    }
    if(s->capacity&&!scalar(a,s->capacity,before,&capacity,e))return false;
    *out=(qa_inventory_entry){s->item,count,capacity,QA_COUNT_SOURCE_FLOAT};
    qa_inventory_entry normalized;return qa_inventory_validate_entry(out,&normalized,e);
}
static size_t entry_count(void *context){return profile_of(context)->definition_count;}
static bool checked_count(void *context,size_t *out,qa_error *e)
{if(!physical_current(context,e))return false;*out=entry_count(context);return true;}
static bool entry_at(void *context,size_t index,qa_inventory_entry *out,qa_error *e)
{
    struct application_qc_item_actor *a=context;const struct application_qc_items *p=profile_of(a);size_t bit;
    if(index>=p->definition_count)return application_fail(e,QA_ERROR_ARGUMENT,"QC item index exceeds declarations");
    const application_qc_item_storage *s=storage_for(p,p->admissions[index].definition.item,&bit);
    return s&&read_entry(a,s,bit,NULL,out,e);
}
static bool entry_write(void *context,const qa_inventory_entry *value,qa_error *e)
{
    struct application_qc_item_actor *a=context;size_t bit;
    if(!physical_current(a,e))return false;
    const application_qc_item_storage *s=storage_for(profile_of(a),value->item,&bit);qa_inventory_entry previous,normalized;
    if(!s||!qa_inventory_validate_entry(value,&normalized,e)||!read_entry(a,s,bit,NULL,&previous,e))return false;
    qa_qc_instance *vm=a->engine->provider->state.qc.instance;
    if(s->bits){float count;if(value->capacity!=1||(value->count!=0&&value->count!=1)||!scalar(a,s->field,NULL,&count,e))return false;
        uint32_t bits=value->count!=0?((uint32_t)count|s->items[bit].mask):((uint32_t)count&~s->items[bit].mask);
        return qa_qc_project_entity_float(vm,a->reference,s->field->offset,(float)bits,e);
    }
    if(value->policy!=QA_COUNT_SOURCE_FLOAT||(!s->capacity&&value->capacity!=previous.capacity))return application_fail(e,QA_ERROR_ARGUMENT,"QC item write changes its immutable Source policy");
    if(!qa_qc_project_entity_float(vm,a->reference,s->field->offset,(float)normalized.count,e))return false;
    return !s->capacity||qa_qc_project_entity_float(vm,a->reference,s->capacity->offset,(float)normalized.capacity,e);
}
static bool mutable_capacity(void *context,qa_item_id item)
{struct application_qc_item_actor *a=context;size_t bit;const application_qc_item_storage *s=storage_for(profile_of(a),item,&bit);return s&&s->capacity;}
static bool invoke(void *context,qa_item_id item,qa_item_action action,qa_error *e)
{
    struct application_qc_item_actor *a=context;
    if((action!=QA_ITEM_USE&&action!=QA_ITEM_DROP)||!physical_current(a,e)||!qa_inventory_lease_current(a->engine->services.inventory,a->lease))return false;
    const struct application_qc_items *p=profile_of(a);
    for(size_t i=0;i<p->definition_count;++i)if(p->admissions[i].definition.item==item){
        const application_qc_call *call=p->definitions[i].actions+(action==QA_ITEM_USE?0:1);
        if(!call->function)return application_fail(e,QA_ERROR_NOT_FOUND,"QC item action is not declared");
        application_qc_inputs inputs={.self=a->actor,.time_ns=a->engine->source_time_ns};
        ++a->calls;bool ok=application_qc_run_call(a->engine,call,&inputs,NULL,e);--a->calls;return ok;
    }
    return application_fail(e,QA_ERROR_NOT_FOUND,"QC item action has no definition");
}
static void binding(struct application_qc_item_actor *a,qa_inventory_items *out)
{
    const struct application_qc_items *p=profile_of(a);
    *out=(qa_inventory_items){.owner=a->engine->provider->owner,.items=p->admissions,.count=p->definition_count,
        .state={.context=a,.count=entry_count,.at=entry_at,.write=entry_write,.mutable_capacity=mutable_capacity,.checked_count=checked_count},.action_context=a,.invoke=invoke};
}
static struct application_qc_item_actor *actor_create(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    struct application_qc_item_actor *a=calloc(1,sizeof(*a));
    if(!a){application_fail(e,QA_ERROR_MEMORY,"Owning actual QC item binding");return NULL;}
    a->engine=engine;a->actor=actor;
    if(!application_qc_reference(engine,actor,&a->reference,e)||!physical_current(a,e)){free(a);return NULL;}
    a->next=engine->item_actors;engine->item_actors=a;return a;
}
bool application_qc_items_admit(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!engine->provider->state.qc.qualified||!engine->provider->state.qc.qualified->items)return true;
    struct application_qc_item_actor *a=actor_find(engine,actor);
    if(a){
        if(!physical_current(a,e)||!qa_inventory_lease_current(engine->services.inventory,a->lease))return false;
    }else{
        a=actor_create(engine,actor,e);if(!a)return false;
        qa_inventory_items source;binding(a,&source);
        if(!qa_inventory_bind_items(engine->services.inventory,actor,&source,&a->lease,e))return false;
    }
    if(!application_qc_item_weapons_reserve(engine,actor,e))return false;
    qa_equipment_state equipment;
    return !qa_equipment_read(engine->provider->application->equipment,actor,&equipment) ||
        application_qc_item_weapons_admit(engine,actor,e);
}
bool application_qc_items_release(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!application_qc_item_weapons_release(engine,actor,e))return false;
    for(struct application_qc_item_actor *a=engine->item_actors;a;a=a->next)if(qa_actor_id_equal(a->actor,actor)&&a->lease.serial){
        if(qa_inventory_lease_current(engine->services.inventory,a->lease)&&!qa_inventory_close_items(engine->services.inventory,a->lease,e))return false;
        a->lease.serial=0;
    }
    return true;
}
bool application_qc_items_close(struct application_qc_state *engine,qa_error *e)
{
    for(struct application_qc_item_actor *a=engine->item_actors;a;a=a->next)if(a->calls)return application_fail(e,QA_ERROR_ARGUMENT,"QC items retain an entered Source action");
    if(!application_qc_item_weapons_close(engine,e))return false;
    while(engine->item_actors){struct application_qc_item_actor *a=engine->item_actors;
        if(!application_qc_items_release(engine,a->actor,e))return false;
        engine->item_actors=a->next;free(a);
    }return true;
}
bool application_qc_items_field_permission(struct application_qc_state *engine,qa_actor_id actor,qa_item_id item,bool count,bool capacity,qa_error *e)
{
    const qa_pickup_execution *execution=NULL;bool found=false;
    if(!qa_pickups_execution_read(engine->services.pickups,actor,engine->provider->owner,&execution,&found,e))return false;
    if(!found)return true;
    size_t size;const qa_pickup_write *writes=qa_pickup_writes(execution,&size);bool allow_count=!count,allow_capacity=!capacity;
    for(size_t i=0;i<size;++i)if(writes[i].resource.kind==QA_PICKUP_INVENTORY&&writes[i].resource.item==item){
        allow_count|=writes[i].fields==QA_PICKUP_COUNT||writes[i].fields==QA_PICKUP_COUNT_CAPACITY;
        allow_capacity|=writes[i].fields==QA_PICKUP_CAPACITY||writes[i].fields==QA_PICKUP_COUNT_CAPACITY;
    }
    return (qa_pickup_current(execution)&&allow_count&&allow_capacity)||application_fail(e,QA_ERROR_ARGUMENT,"QC pickup Source store exceeds its declared resource dimensions");
}
bool application_qc_items_source_stored(struct application_qc_state *engine,qa_qc_instance *vm,const qa_qc_store_event *event,qa_error *e)
{
    (void)vm;qa_actor_id actor;
    if(!engine->provider->state.qc.qualified||!engine->provider->state.qc.qualified->items||event->kind!=QA_QC_STORE_ENTITY||!event->entity_reference)return true;
    if(!qa_qc_reference_actor(engine->provider->state.qc.instance,event->entity_reference,&actor,e))return false;
    struct application_qc_item_actor *a=actor_find(engine,actor);if(!a)return true;
    if(!physical_current(a,e)||!qa_inventory_lease_current(engine->services.inventory,a->lease))return false;
    const struct application_qc_items *p=profile_of(a);qa_inventory_change *changes=calloc(p->definition_count,sizeof(*changes));
    if(!changes)return application_fail(e,QA_ERROR_MEMORY,"Observing QC item Source stores");
    size_t count=0;bool ok=true;
    for(size_t i=0;ok&&i<p->storage_count;++i){const application_qc_item_storage *s=p->storage+i;
        bool touches=s->field->offset>=event->word&&s->field->offset<event->word+event->count;
        touches|=s->capacity&&s->capacity->offset>=event->word&&s->capacity->offset<event->word+event->count;
        if(!touches)continue;
        for(size_t j=0;ok&&j<(s->bits?s->count:1);++j){qa_inventory_change change={.had_before=true,.actor=actor};
            ok=read_entry(a,s,j,event,&change.before,e)&&read_entry(a,s,j,NULL,&change.after,e);
            if(ok&&(change.before.count!=change.after.count||change.before.capacity!=change.after.capacity)){
                ok=application_qc_items_field_permission(engine,actor,change.after.item,change.before.count!=change.after.count,change.before.capacity!=change.after.capacity,e);
                if(ok)changes[count++]=change;
            }
        }
    }
    if(ok&&count){++a->calls;ok=qa_inventory_source_stored(engine->services.inventory,a->lease,changes,count,e);--a->calls;}
    free(changes);return ok;
}
bool application_qc_items_saved_group(application_provider *provider,qa_actor_id actor,uint64_t serial,const qa_inventory_source_group *group,qa_inventory_items *out,qa_error *e)
{
    struct application_qc_state *engine=provider->state.qc.engine;const struct application_qc_items *p=provider->state.qc.qualified?provider->state.qc.qualified->items:NULL;
    if(!engine||!p||!serial||group->owner!=provider->owner||group->definitions_only||group->count!=p->definition_count)return application_fail(e,QA_ERROR_FORMAT,"Saved QC item group differs from its actual declaration");
    struct application_qc_item_actor *a=actor_find(engine,actor);
    if(a&&a->lease.serial!=serial)return application_fail(e,QA_ERROR_FORMAT,"Saved QC inventory duplicates full actor ownership");
    if(!a){a=actor_create(engine,actor,e);if(!a)return false;a->lease=(qa_inventory_lease){actor,serial};}
    binding(a,out);return true;
}
bool application_qc_items_item_read(application_provider *provider,qa_actor_id actor,qa_item_id item,qa_item_admission *out,qa_bytes *icon,qa_bytes *held,bool *found,qa_error *e)
{
    struct application_qc_state *engine=provider->state.qc.engine;struct application_qc_item_actor *a=engine?actor_find(engine,actor):NULL;
    *found=false;if(!a)return true;
    if(!physical_current(a,e)||!qa_inventory_lease_current(engine->services.inventory,a->lease))return false;
    const struct application_qc_items *p=profile_of(a);
    for(size_t i=0;i<p->definition_count;++i)if(p->admissions[i].definition.item==item){*out=p->admissions[i];*icon=(qa_bytes){p->definitions[i].icon.data,p->definitions[i].icon.size};*held=(qa_bytes){p->definitions[i].held.data,p->definitions[i].held.size};*found=true;break;}
    return true;
}

bool application_qc_items_restore_finish(application_provider *provider,qa_error *e)
{
    struct application_qc_state *engine=provider->state.qc.engine;
    if(!engine||!provider->state.qc.qualified||!provider->state.qc.qualified->items)return true;
    for(uint32_t slot=1;slot<=engine->max_clients;++slot){const application_qc_client *client=engine->clients+slot;
        if(!client->connected||!client->spawned)continue;
        if(!application_qc_items_actor_current(engine,client->actor,e)||
            !application_qc_item_weapons_admit(engine,client->actor,e))
            return application_fail(e,QA_ERROR_FORMAT,"Restored QC client lacks its canonical saved item group");
    }
    for(struct application_qc_item_actor *a=engine->item_actors;a;a=a->next)
        if(a->lease.serial&&!application_qc_items_actor_current(engine,a->actor,e))return false;
    return true;
}
