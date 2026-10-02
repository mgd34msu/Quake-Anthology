#include "guest_q3_mod_items_private.h"
item_actor *q3items_actor(application_q3_mod_items *o,qa_actor_id actor)
{for(item_actor *a=o?o->actors:NULL;a;a=a->next)if(qa_actor_id_equal(a->actor,actor))return a;return NULL;}
bool q3items_current(item_actor *a,qa_error *e)
{
    if(!a||a->releasing||!q3mod_storage_current(a->owner->mod,e)||
        !application_q3_mod_client_live(a->owner->mod,a->actor)||
        !qa_actors_get(qa_session_actors(a->owner->mod->session),a->actor)||
        (!a->admitting&&!qa_inventory_lease_current(a->owner->inventory,a->lease)))return false;
    for(size_t i=0;i<a->address_count;++i){uint32_t actual;const char *record=a->owner->profile->source->records[a->addresses[i].record].id;
        if(!a->owner->mod->services.pointer(a->owner->mod->services.context,a->actor,record,&actual,e)||actual!=a->addresses[i].address)return false;}
    return true;
}
static size_t entry_count(void *context)
{return ((item_actor *)context)->owner->profile->definition_count;}
static bool checked_count(void *context,size_t *out,qa_error *e)
{item_actor *a=context;if(!q3items_current(a,e))return false;*out=entry_count(a);return true;}
static const item_storage *storage_for(application_q3_mod_items_profile *p,qa_item_id item,size_t *bit)
{
    for(size_t i=0;i<p->storage_count;++i){item_storage *s=p->storage+i;if(!s->bits&&s->item==item){*bit=0;return s;}
        for(size_t j=0;s->bits&&j<s->count;++j)if(s->items[j].item==item){*bit=j;return s;}}
    return NULL;
}
static bool at(void *context,size_t index,qa_inventory_entry *out,qa_error *e)
{
    item_actor *a=context;if(!q3items_current(a,e)||index>=entry_count(a))return false;
    qa_item_id item=a->owner->profile->definitions[index].admission.definition.item;size_t bit;
    const item_storage *s=storage_for(a->owner->profile,item,&bit);if(!s)return false;
    qa_inventory_entry *rows=calloc(s->bits?s->count:1,sizeof(*rows));if(!rows)return q3mod_fail(e,QA_ERROR_MEMORY,"Reading genuine item storage");
    bool ok=q3items_read(a,s,NULL,rows,s->bits?s->count:1,e);if(ok)*out=rows[bit];free(rows);return ok&&q3items_current(a,e);
}
static bool write_word(item_actor *a,item_field f,int32_t value,qa_error *e)
{uint32_t address;uint8_t bytes[4];qa_store_u32le(bytes,(uint32_t)value);return q3items_address(a,f,&address,e)&&qa_qvm_write(a->owner->mod->vm,address,(qa_bytes){bytes,4},e);}
static bool write(void *context,const qa_inventory_entry *value,qa_error *e)
{
    item_actor *a=context;size_t bit;
    if(!value||!q3items_current(a,e))return false;const item_storage *s=storage_for(a->owner->profile,value->item,&bit);
    if(!s)return false;
    if(s->bits){int32_t previous;if(value->capacity!=1||(value->count!=0&&value->count!=1)||!q3items_scalar(a,s->field,NULL,&previous,e))return false;
        uint32_t bits=value->count?((uint32_t)previous|s->items[bit].mask):((uint32_t)previous&~s->items[bit].mask);int32_t word;memcpy(&word,&bits,4);
        return write_word(a,s->field,word,e);
    }
    if(!isfinite(value->count)||trunc(value->count)!=value->count||value->count<INT32_MIN||value->count>INT32_MAX||
        !isfinite(value->capacity)||trunc(value->capacity)!=value->capacity||value->capacity<0||value->capacity>INT32_MAX)return false;
    int32_t capacity;if(s->capacity.kind!=ITEM_CAPACITY_FIELD&&(!q3items_capacity(a,&s->capacity,NULL,&capacity,e)||value->capacity!=capacity))return false;
    if(!write_word(a,s->field,(int32_t)value->count,e))return false;
    return s->capacity.kind!=ITEM_CAPACITY_FIELD||
        (q3items_current(a,e)&&write_word(a,s->capacity.field,(int32_t)value->capacity,e));
}
static bool mutable_capacity(void *context,qa_item_id item)
{item_actor *a=context;size_t bit;const item_storage *s=storage_for(a->owner->profile,item,&bit);return s&&!s->bits&&s->capacity.kind==ITEM_CAPACITY_FIELD;}
static bool action(void *context,qa_item_id item,qa_item_action action_kind,qa_error *e)
{
    item_actor *a=context;if((action_kind!=QA_ITEM_USE&&action_kind!=QA_ITEM_DROP)||!q3items_current(a,e))return false;
    for(size_t i=0;i<a->owner->profile->definition_count;++i){item_definition *d=a->owner->profile->definitions+i;if(d->admission.definition.item!=item)continue;
        application_q3_mod_call *call=d->actions[action_kind==QA_ITEM_USE?0:1];if(!call)return false;
        double time;if(!a->owner->mod->services.time(a->owner->mod->services.context,&time,e))return false;
        application_q3_mod_inputs inputs={0};inputs.values[Q3_MOD_SELF]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=a->actor};
        inputs.values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=time};
        ++a->references;++a->owner->calls;double ignored;bool ok=application_q3_mod_call_run(a->owner->mod,call,&inputs,&ignored,e);
        --a->owner->calls;--a->references;return ok;
    }return false;
}
bool q3items_binding(item_actor *a,qa_inventory_items *out,qa_error *e)
{
    application_q3_mod_items *o=a->owner;
    if(!a->definitions){
        a->definitions=calloc(o->profile->definition_count,sizeof(*a->definitions));
        if(!a->definitions)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining exact canonical item definitions");
        for(size_t i=0;i<o->profile->definition_count;++i){a->definitions[i]=o->profile->definitions[i].admission;a->definitions[i].definition.owner=o->mod->owner;}
    }
    *out=(qa_inventory_items){.owner=o->mod->owner,.items=a->definitions,.count=o->profile->definition_count,
        .state={.context=a,.count=entry_count,.checked_count=checked_count,.at=at,.write=write,.mutable_capacity=mutable_capacity},.action_context=a,.invoke=action};
    return true;
}
static bool overlaps(uint32_t address,const qa_qvm_committed_write *event)
{for(size_t i=0;i<event->count;++i)if(address<(uint64_t)event->ranges[i].offset+event->ranges[i].after.size&&event->ranges[i].offset<(uint64_t)address+4)return true;return false;}
static bool publish(void *context,qa_qvm *vm,const qa_qvm_committed_write *event,qa_error *e)
{
    item_actor *a=context;application_q3_mod_items *o=a->owner;if(vm!=o->mod->vm||a->releasing)return true;
    item_pending *pending=calloc(1,sizeof(*pending));if(!pending)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining committed item changes");
    pending->sequence=event->sequence;pending->changes=calloc(o->profile->definition_count,sizeof(*pending->changes));
    if(!pending->changes){free(pending);return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining committed item values");}
    bool ok=true;
    for(size_t i=0;ok&&i<o->profile->storage_count;++i){const item_storage *s=o->profile->storage+i;uint32_t address;
        if(!q3items_address(a,s->field,&address,e)){ok=false;break;}bool changed=overlaps(address,event);
        if(!s->bits&&s->capacity.kind==ITEM_CAPACITY_FIELD){if(!q3items_address(a,s->capacity.field,&address,e)){ok=false;break;}changed|=overlaps(address,event);}
        for(size_t j=0;!s->bits&&s->capacity.kind==ITEM_CAPACITY_SOURCE&&j<s->capacity.count;++j)changed|=overlaps(s->capacity.overrides[j].address,event);
        if(!changed)continue;
        size_t count=s->bits?s->count:1;qa_inventory_entry *before=calloc(count,sizeof(*before)),*after=calloc(count,sizeof(*after));
        if(!before||!after){free(before);free(after);ok=q3mod_fail(e,QA_ERROR_MEMORY,"Reading committed original items");break;}
        ok=q3items_read(a,s,event,before,count,e)&&q3items_read(a,s,NULL,after,count,e);
        for(size_t j=0;ok&&j<count;++j){bool counts=before[j].count!=after[j].count,capacities=before[j].capacity!=after[j].capacity;
            if(!counts&&!capacities)continue;
            if(o->services.pickup_write&&!o->services.pickup_write(o->services.context,a->actor,after[j].item,counts,capacities,e)){ok=false;break;}
            pending->changes[pending->count++]=(qa_inventory_change){true,a->actor,before[j],after[j]};
        }free(before);free(after);
    }
    if(!ok){free(pending->changes);free(pending);return false;}
    item_pending **tail=&a->pending;while(*tail)tail=&(*tail)->next;*tail=pending;return true;
}
static bool after(void *context,qa_qvm *vm,const qa_qvm_committed_write *event,qa_error *e)
{
    item_actor *a=context;if(vm!=a->owner->mod->vm)return false;item_pending **link=&a->pending;
    while(*link&&(*link)->sequence!=event->sequence)link=&(*link)->next;
    if(!*link)return true;item_pending *pending=*link;*link=pending->next;
    ++a->references;++a->owner->calls;
    bool current=q3items_current(a,NULL);
    bool ok=!pending->count||!current||qa_inventory_source_stored(a->owner->inventory,a->lease,pending->changes,pending->count,e);
    if(current&&!q3items_current(a,NULL))for(application_q3_mod_items_entry *entry=a->owner->entries;entry;entry=entry->previous)
        if(entry->input&&qa_actor_id_equal(entry->actor,a->actor)){
            qa_error cancelled={0};bool stopped=qa_qvm_cancel(&entry->call,ok?e:&cancelled);
            ok=ok&&stopped;break;
        }
    --a->owner->calls;--a->references;free(pending->changes);free(pending);return ok;
}
bool q3items_watch_create(item_actor *a,qa_error *e)
{
    application_q3_mod_items *o=a->owner;
    qa_qvm_write_range *ranges=NULL;size_t count=0;bool ok=true;
    for(size_t i=0;ok&&i<o->profile->storage_count;++i){
        const item_storage *s=o->profile->storage+i;
        size_t fields=(!s->bits&&s->capacity.kind==ITEM_CAPACITY_FIELD)?2:1;
        size_t globals=(!s->bits&&s->capacity.kind==ITEM_CAPACITY_SOURCE)?s->capacity.count:0;
        if(fields>SIZE_MAX-globals||fields+globals>SIZE_MAX-count||
            count+fields+globals>SIZE_MAX/sizeof(*ranges)){ok=false;break;}
        qa_qvm_write_range *next=realloc(ranges,(count+fields+globals)*sizeof(*ranges));
        if(!next){ok=q3mod_fail(e,QA_ERROR_MEMORY,"Retaining actual item watch addresses");break;}
        ranges=next;
        for(size_t j=0;ok&&j<fields;++j){
            item_field f=j?s->capacity.field:s->field;
            size_t n=0;while(n<a->address_count&&a->addresses[n].record!=f.record)++n;
            if(n==a->address_count||(uint64_t)a->addresses[n].address+f.offset>UINT32_MAX){ok=false;break;}
            ranges[count++]=(qa_qvm_write_range){a->addresses[n].address+f.offset,4};
        }
        for(size_t j=0;ok&&j<globals;++j)ranges[count++]=(qa_qvm_write_range){s->capacity.overrides[j].address,4};
    }
    if(ok)ok=qa_qvm_observe_writes(o->mod->vm,ranges,count,publish,after,a,&a->watch,e);
    free(ranges);return ok;
}
bool application_q3_mod_items_create(application_q3_mod_items_profile *p,application_q3_mod *mod,
    qa_inventory *inventory,const application_q3_mod_items_services *services,application_q3_mod_items **out,qa_error *e)
{
    if(!p||!mod||p->source!=mod->profile||!inventory||!services||!out||*out||
        (p->stage&&(!services->selected||!services->posture||!services->weapon_bind||!services->weapon_unbind)))return q3mod_fail(e,QA_ERROR_ARGUMENT,"Items require their actual executor and canonical services");
    application_q3_mod_items *o=calloc(1,sizeof(*o));if(!o)return q3mod_fail(e,QA_ERROR_MEMORY,"Owning generic source items");
    o->profile=p;o->mod=mod;o->inventory=inventory;o->services=*services;*out=o;return true;
}
bool application_q3_mod_items_idle(const application_q3_mod_items *o)
{if(!o)return true;if(o->calls||o->entries||o->application)return false;for(item_actor *a=o->actors;a;a=a->next)if(a->pending||a->references)return false;return true;}
static bool address_add(item_actor *a,size_t record,qa_error *e)
{
    for(size_t i=0;i<a->address_count;++i)if(a->addresses[i].record==record)return true;
    if(a->address_count==SIZE_MAX/sizeof(*a->addresses))return false;
    item_record_address *next=realloc(a->addresses,(a->address_count+1)*sizeof(*next));if(!next)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining original item record bases");a->addresses=next;
    uint32_t address;if(!a->owner->mod->services.pointer(a->owner->mod->services.context,a->actor,a->owner->profile->source->records[record].id,&address,e))return false;
    a->addresses[a->address_count++]=(item_record_address){record,address};return true;
}
bool application_q3_mod_items_admit(application_q3_mod_items *o,qa_actor_id actor,qa_error *e)
{
    if(!o||!q3mod_storage_current(o->mod,e)||!application_q3_mod_client_live(o->mod,actor)||!qa_actors_get(qa_session_actors(o->mod->session),actor))return false;
    item_actor *existing=q3items_actor(o,actor);if(existing)return existing->watch&&
        (!o->profile->stage||existing->weapon_bound)&&q3items_current(existing,e);
    item_actor *a=calloc(1,sizeof(*a));if(!a)return q3mod_fail(e,QA_ERROR_MEMORY,"Owning admitted original item lease");a->owner=o;a->actor=actor;a->next=o->actors;o->actors=a;
    size_t range_count=0;qa_qvm_write_range *ranges=NULL;bool ok=true;
    for(size_t i=0;ok&&i<o->profile->storage_count;++i){const item_storage *s=o->profile->storage+i;size_t fields=(!s->bits&&s->capacity.kind==ITEM_CAPACITY_FIELD)?2:1;
        for(size_t j=0;ok&&j<fields;++j){item_field f=j?s->capacity.field:s->field;uint32_t address;
            ok=address_add(a,f.record,e)&&q3items_address(a,f,&address,e);if(!ok)break;
            qa_qvm_write_range *next=realloc(ranges,(range_count+1)*sizeof(*next));if(!next){ok=false;break;}ranges=next;ranges[range_count++]=(qa_qvm_write_range){address,4};
        }
        for(size_t j=0;ok&&!s->bits&&s->capacity.kind==ITEM_CAPACITY_SOURCE&&j<s->capacity.count;++j){qa_qvm_write_range *next=realloc(ranges,(range_count+1)*sizeof(*next));if(!next){ok=false;break;}ranges=next;ranges[range_count++]=(qa_qvm_write_range){s->capacity.overrides[j].address,4};}
    }
    qa_inventory_items binding={0};if(ok)ok=q3items_binding(a,&binding,e);
    if(ok){a->admitting=true;ok=qa_inventory_bind_items(o->inventory,actor,&binding,&a->lease,e);a->admitting=false;}
    if(ok)ok=qa_qvm_observe_writes(o->mod->vm,ranges,range_count,publish,after,a,&a->watch,e);
    free(ranges);
    if(ok&&o->profile->stage){ok=o->services.weapon_bind(o->services.context,actor,o,e);if(ok)a->weapon_bound=true;}
    return ok;
}
bool application_q3_mod_items_release(application_q3_mod_items *o,qa_actor_id actor,qa_error *e)
{
    item_actor *a=q3items_actor(o,actor);if(!a)return true;
    a->releasing=true;if(a->references)return q3mod_fail(e,QA_ERROR_ARGUMENT,"Item lease retains its real source callback");
    if(a->weapon_bound){if(!o->services.weapon_unbind(o->services.context,actor,o,e))return false;a->weapon_bound=false;}
    if(a->watch){if(!qa_qvm_unobserve_writes(o->mod->vm,a->watch,e))return false;a->watch=0;}
    if(a->lease.serial){if(qa_inventory_lease_current(o->inventory,a->lease)&&!qa_inventory_close_items(o->inventory,a->lease,e))return false;a->lease=(qa_inventory_lease){0};}
    item_actor **link=&o->actors;while(*link!=a)link=&(*link)->next;*link=a->next;
    while(a->pending){item_pending *p=a->pending;a->pending=p->next;free(p->changes);free(p);}free(a->addresses);free(a->definitions);free(a);return true;
}
bool application_q3_mod_items_destroy(application_q3_mod_items **in,qa_error *e)
{if(!in)return false;application_q3_mod_items *o=*in;if(!o)return true;if(!application_q3_mod_items_idle(o))return false;while(o->actors)if(!application_q3_mod_items_release(o,o->actors->actor,e))return false;free(o);*in=NULL;return true;}
