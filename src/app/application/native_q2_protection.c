#include "native_q2_protection.h"
#include "qa/source_save.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct protection_counter {
    application_native_q2_field field;
    qa_item_id inventory,item;
    qa_power_kind power;
} protection_counter;
typedef struct protection_definition {
    qa_json_id source;
    qa_protection_channel channel;
    qa_protection_claim claim;
    application_native_q2_armor *armor;
    protection_counter *counters;
    qa_item_id *items;
    size_t count,item_count;
} protection_definition;
typedef struct protection_actor {
    struct protection_actor *next;
    struct application_native_q2_protection *owner;
    qa_actor_id actor;
    size_t definition;
    qa_protection_lease lease;
    unsigned references;
    bool bound,restoring;
} protection_actor;
typedef struct protection_watch {
    struct protection_watch *next;
    struct protection_stage *stage;
    protection_actor *actor;
    application_native_q2_armor_watch *watch;
} protection_watch;
typedef struct protection_stage {
    struct protection_stage *outer;
    application_native_q2_protection *owner;
    qa_actor_id actor;
    qa_protection_observer *observer;
    qa_pickup_execution *pickup;
    protection_watch *watches;
    qa_item_id *items;
    size_t item_count;
    qa_armor before;
    unsigned suppressed;
    bool active;
} protection_stage;
struct application_native_q2_protection {
    application_native_q2_protection_options options;
    protection_definition *definitions;
    size_t count;
    protection_actor *actors;
    protection_stage *stages;
    unsigned writing;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
static bool identity(application_native_q2_protection *o,qa_json_id id,qa_string_id *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks); qa_buffer text={0};
    if(!qa_json_string(d,id,&text,e)) return false;
    bool ok=text.size&&!memchr(text.data,0,text.size)&&qa_strings_intern(qa_session_strings(o->options.session),
        (qa_bytes){text.data,text.size},out,e); qa_buffer_free(&text); return ok||fail(e,"Native protection identity is invalid");
}
static bool source_current(application_native_q2_protection *o,qa_actor_id actor,qa_error *e)
{
    return o&&application_native_q2_callbacks_storage_current(o->options.callbacks,e)&&
        qa_actors_get(qa_session_actors(o->options.session),actor)&&
        application_native_q2_callbacks_client_current(o->options.callbacks,actor,e);
}
static bool current(protection_actor *a,qa_error *e)
{
    return source_current(a->owner,a->actor,e)&&
        ((a->restoring&&application_native_q2_callbacks_restoring(a->owner->options.callbacks))||
            qa_combat_protection_current(a->owner->options.combat,a->lease));
}
static bool counter_parse(application_native_q2_protection *o,protection_definition *definition,
    protection_counter *counter,qa_json_id row,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    if(!application_native_q2_field_parse(o->options.callbacks,qa_json_get(d,row,
        definition->channel==QA_PROTECTION_REGULAR?"points":"cells"),&counter->field,e)) return false;
    qa_json_id item=qa_json_get(d,row,"item");
    if(qa_json_type(d,item)!=QA_JSON_NULL&&!identity(o,item,&counter->item,e)) return false;
    if(definition->channel==QA_PROTECTION_POWERED) {
        if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"screen")) counter->power=QA_POWER_SCREEN;
        else if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"shield")) counter->power=QA_POWER_SHIELD;
        else return fail(e,"Native protection counter has no powered kind");
    }
    qa_json_id records=qa_json_get(d,qa_json_root(d),"actorRecords");
    for(size_t r=0;r<qa_json_size(d,records);++r) {
        qa_json_id record=qa_json_at(d,records,r);
        if(!qa_json_string_equal(d,qa_json_get(d,record,"id"),counter->field.record)) continue;
        qa_json_id fields=qa_json_get(d,record,"fields");
        for(size_t j=0;j<qa_json_size(d,fields);++j) {
            qa_json_id field=qa_json_at(d,fields,j); uint64_t offset;
            if(!qa_json_string_equal(d,qa_json_get(d,field,"binding"),"inventory")) continue;
            if(!qa_json_u64(d,qa_json_get(d,field,"offset"),&offset,e)) return false;
            if(offset!=counter->field.offset) continue;
            application_native_q2_field parsed={0};
            /* Record projections do not repeat their containing record name. */
            const char *names[]={"void","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
            for(size_t k=1;k<sizeof(names)/sizeof(*names);++k)
                if(qa_json_string_equal(d,qa_json_get(d,field,"encoding"),names[k])) parsed.encoding=(qa_native_value_type)k;
            if(parsed.encoding==counter->field.encoding&&!identity(o,qa_json_get(d,field,"item"),&counter->inventory,e)) return false;
        }
    }
    if(definition->channel==QA_PROTECTION_POWERED&&!counter->inventory) return fail(e,"Native powered protection requires its actual shared inventory counter");
    if(counter->inventory) {
        bool found=false;
        for(size_t i=0;i<definition->item_count;++i) if(definition->items[i]==counter->inventory) found=true;
        if(!found) definition->items[definition->item_count++]=counter->inventory;
    }
    return true;
}
static bool create_layout(const application_native_q2_protection_options *options,
    application_native_q2_protection **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->session||!options->combat||!options->inventory||!options->owner)
        return fail(e,"Native protection requires its real source and canonical services");
    application_native_q2_protection *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native protection declarations"); return false; }
    o->options=*options; *out=o;
    const qa_json_document *d=application_native_q2_callbacks_document(options->callbacks);
    qa_json_id rows=qa_json_get(d,qa_json_root(d),"protection");
    if(rows==QA_JSON_NONE) return true;
    if(qa_json_type(d,rows)!=QA_JSON_ARRAY) return fail(e,"Native protection declarations require their authored array");
    o->count=qa_json_size(d,rows); o->definitions=o->count?calloc(o->count,sizeof(*o->definitions)):NULL;
    if(o->count&&!o->definitions) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native protection channels"); return false; }
    for(size_t i=0;i<o->count;++i) {
        protection_definition *definition=o->definitions+i; qa_json_id row=qa_json_at(d,rows,i);
        definition->source=row; definition->claim.owner=options->owner;
        if(!identity(o,qa_json_get(d,row,"id"),&definition->claim.rule,e)) return false;
        if(qa_json_string_equal(d,qa_json_get(d,row,"channel"),"regular")) definition->channel=QA_PROTECTION_REGULAR;
        else if(qa_json_string_equal(d,qa_json_get(d,row,"channel"),"powered")) definition->channel=QA_PROTECTION_POWERED;
        else return fail(e,"Native protection channel is undeclared");
        for(size_t j=0;j<i;++j) if(o->definitions[j].claim.rule==definition->claim.rule||o->definitions[j].channel==definition->channel)
            return fail(e,"Native protection claim identity or channel repeats");
        qa_json_id admission=qa_json_get(d,row,"admission");
        if(admission==QA_JSON_NONE||qa_json_string_equal(d,qa_json_get(d,admission,"kind"),"claim")) definition->claim.admission=QA_PROTECTION_CLAIM;
        else if(qa_json_string_equal(d,qa_json_get(d,admission,"kind"),"replace-current-primary")) definition->claim.admission=QA_PROTECTION_REPLACE_CURRENT;
        else if(qa_json_string_equal(d,qa_json_get(d,admission,"kind"),"replace-primary")) {
            definition->claim.admission=QA_PROTECTION_REPLACE_PRIMARY;
            if(!identity(o,qa_json_get(d,admission,"owner"),&definition->claim.expected_owner,e)) return false;
        } else return fail(e,"Native protection admission is undeclared");
        qa_json_id storage=qa_json_get(d,row,"storage");
        if(qa_json_type(d,storage)!=QA_JSON_ARRAY||!qa_json_size(d,storage)) return fail(e,"Native protection lacks its real source storage");
        application_native_q2_armor_options armor={options->callbacks,qa_session_strings(options->session),options->owner};
        if(!application_native_q2_armor_create(&armor,definition->channel==QA_PROTECTION_REGULAR?storage:QA_JSON_NONE,
            definition->channel==QA_PROTECTION_POWERED?storage:QA_JSON_NONE,false,&definition->armor,e)) return false;
        definition->count=qa_json_size(d,storage);
        definition->counters=definition->count?calloc(definition->count,sizeof(*definition->counters)):NULL;
        definition->items=definition->count?calloc(definition->count,sizeof(*definition->items)):NULL;
        if(definition->count&&(!definition->counters||!definition->items)) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native protection shared counter receipts"); return false; }
        for(size_t j=0;j<definition->count;++j) if(!counter_parse(o,definition,definition->counters+j,qa_json_at(d,storage,j),e)) return false;
    }
    return true;
}
bool application_native_q2_protection_create(const application_native_q2_protection_options *options,
    application_native_q2_protection **out,qa_error *e)
{
    if(!out||*out) return fail(e,"Native protection construction requires an empty destination");
    application_native_q2_protection *o=NULL;
    if(!create_layout(options,&o,e)) {
        /* Claims and Source observation begin only after successful parsing. */
        (void)application_native_q2_protection_destroy(&o,NULL);return false;
    }
    *out=o;return true;
}
static bool read_actor(protection_actor *a,qa_armor *out,qa_error *e)
{
    application_native_q2_protection *o=a->owner; protection_definition *definition=o->definitions+a->definition;
    qa_armor armor;
    if(!current(a,e)||!application_native_q2_armor_read(definition->armor,a->actor,&armor,e)) return false;
    for(size_t i=0;i<definition->count;++i) {
        protection_counter *counter=definition->counters+i;
        if(!counter->inventory) continue;
        bool selected=definition->channel==QA_PROTECTION_REGULAR?
            armor.regular.kind!=QA_ARMOR_NONE&&armor.regular.item==counter->item:armor.powered.kind==counter->power;
        if(!selected) continue;
        double value;
        if(!qa_inventory_count_read(o->options.inventory,a->actor,counter->inventory,&value,e)) return false;
        if(!isfinite(value)) return fail(e,"Native protection inventory is not finite");
        if(definition->channel==QA_PROTECTION_REGULAR) armor.regular.points=value;
        else armor.powered.cells=value;
    }
    *out=armor; return current(a,e);
}
static bool read_binding(void *context,qa_armor *out,qa_error *e)
{ return read_actor(context,out,e); }
static bool merge(protection_actor *a,const qa_armor *requested,qa_armor *out,qa_error *e)
{
    qa_armor old;
    if(!read_actor(a,&old,e)) return false;
    qa_protection_channel channel=a->owner->definitions[a->definition].channel;
    if(channel==QA_PROTECTION_REGULAR) old.regular=requested->regular;
    else {
        if(requested->powered.kind!=old.powered.kind) return fail(e,"Native powered activation requires its actual source operation");
        old.powered=requested->powered;
    }
    *out=old; return true;
}
static bool validate_binding(void *context,const qa_armor *next,qa_error *e)
{
    protection_actor *a=context; qa_armor merged;
    return next&&merge(a,next,&merged,e)&&application_native_q2_armor_validate(a->owner->definitions[a->definition].armor,a->actor,&merged,e);
}
static bool read_all(application_native_q2_protection *o,qa_actor_id actor,qa_armor *out,qa_error *e)
{
    qa_armor result={0};
    for(protection_actor *a=o->actors;a;a=a->next) if(qa_actor_id_equal(a->actor,actor)&&a->bound) {
        qa_armor armor; if(!read_actor(a,&armor,e)) return false;
        if(o->definitions[a->definition].channel==QA_PROTECTION_REGULAR) result.regular=armor.regular;
        else result.powered=armor.powered;
    }
    *out=result; return true;
}
typedef struct protection_write {
    protection_actor *actor;
    qa_armor armor;
} protection_write;
static bool write_execute(void *context,qa_error *e)
{
    protection_write *write=context;protection_actor *a=write->actor;
    if(!current(a,e)) return false;
    ++a->owner->writing;
    bool ok=application_native_q2_armor_write(a->owner->definitions[a->definition].armor,a->actor,&write->armor,e);
    --a->owner->writing;
    return ok&&current(a,e);
}
static bool write_binding(void *context,const qa_armor *next,qa_error *e)
{
    protection_actor *a=context; application_native_q2_protection *o=a->owner; qa_armor merged;
    if(!next||!merge(a,next,&merged,e)) return false;
    protection_stage *suppressed=o->stages;
    for(protection_stage *s=suppressed;s;s=s->outer) ++s->suppressed;
    protection_write write={a,merged};
    bool ok=application_native_q2_callbacks_storage_transfer(o->options.callbacks,write_execute,&write,e);
    for(protection_stage *s=suppressed;s;s=s->outer) --s->suppressed;
    if(ok) for(protection_stage *s=o->stages;s;s=s->outer) if(s->active&&qa_actor_id_equal(s->actor,a->actor)) {
        if(!read_all(o,a->actor,&s->before,e)) return false;
    }
    return ok;
}
static bool stage_current(const protection_stage *stage,qa_error *e)
{
    return (stage->active&&(!stage->pickup||qa_pickup_recipient_is(stage->pickup,stage->actor)))||
        fail(e,"Native protection observation lost its actual execution receipt");
}
static bool publish(protection_stage *stage,qa_error *e)
{
    application_native_q2_protection *o=stage->owner; qa_armor result;
    if(!stage_current(stage,e)||!read_all(o,stage->actor,&result,e)) return false;
    qa_protection_store store={.before=stage->before,.after=result,
        .regular=!qa_regular_armor_equal(stage->before.regular,result.regular),
        .powered=!qa_powered_armor_equal(stage->before.powered,result.powered)};
    for(protection_stage *s=o->stages;s;s=s->outer) if(s->active&&qa_actor_id_equal(s->actor,stage->actor)) s->before=result;
    bool ok=(!store.regular&&!store.powered)||(stage->pickup?
        qa_pickup_store_protection(stage->pickup,&store,e):qa_protection_observe(stage->observer,&store,e));
    return ok&&stage_current(stage,e);
}
static bool committed(void *context,const qa_inventory_change *change,qa_error *e)
{
    protection_watch *watch=context; protection_stage *stage=watch->stage;
    application_native_q2_protection *o=stage->owner;
    if(!stage_current(stage,e)||!change->had_before||!qa_actor_id_equal(change->actor,stage->actor)||!current(watch->actor,e))
        return fail(e,"Native protection inventory commit lost its actual lexical reservoir");
    protection_stage *suppressed=o->stages;
    for(protection_stage *s=suppressed;s;s=s->outer) ++s->suppressed;
    bool ok=true;
    for(protection_actor *a=o->actors;ok&&a;a=a->next) if(a->bound&&qa_actor_id_equal(a->actor,stage->actor)) {
        protection_definition *definition=o->definitions+a->definition;
        ok=current(a,e);
        for(size_t i=0;ok&&i<definition->count;++i) {
            protection_counter *counter=definition->counters+i; double raw;
            if(counter->inventory!=change->after.item||change->before.count==change->after.count) continue;
            ok=application_native_q2_field_read(o->options.callbacks,stage->actor,&counter->field,&raw,e);
            if(ok&&raw!=change->after.count) ok=application_native_q2_field_write(o->options.callbacks,stage->actor,&counter->field,change->after.count,e);
        }
    }
    for(protection_stage *s=suppressed;s;s=s->outer) --s->suppressed;
    return ok&&publish(stage,e);
}
static bool commit_current(void *context,qa_error *e)
{
    protection_watch *watch=context;
    return stage_current(watch->stage,e)&&current(watch->actor,e);
}
static bool changed(void *context,const qa_armor *before,const qa_armor *after,qa_error *e)
{
    protection_watch *watch=context; protection_stage *stage=watch->stage;
    application_native_q2_protection *o=stage->owner; (void)before; (void)after;
    if(!stage->active) return true;
    if(o->writing||stage->suppressed||o->stages!=stage) return true;
    if(!stage_current(stage,e)||!current(watch->actor,e)) return false;
    application_native_q2_inventory_commit receipt={.actor=stage->actor,.items=stage->items,.count=stage->item_count,
        .context=watch,.current=commit_current,.committed=committed};
    if(!application_native_q2_records_commit_inventory(application_native_q2_callbacks_records(o->options.callbacks),&receipt,e)) return false;
    return publish(stage,e);
}
static bool stage_close(protection_stage *stage,qa_error *e)
{
    for(protection_watch *watch=stage->watches;watch;watch=watch->next)
        if(!application_native_q2_armor_observe_cancel(watch->watch,e)) return false;
    for(protection_watch *watch=stage->watches;watch;watch=watch->next)
        if(!application_native_q2_armor_observe_end(&watch->watch,e)) return false;
    protection_watch *watch=stage->watches;
    while(watch) { protection_watch *next=watch->next; free(watch); watch=next; }
    free(stage->items); stage->items=NULL; stage->item_count=0;
    return true;
}
typedef struct observe_call {
    application_native_q2_protection *owner;
    qa_actor_id actor;
    qa_protection_observer *observer;
    qa_pickup_execution *pickup;
    application_native_q2_protection_execute_fn execute;
    void *context;
} observe_call;
static bool observe_transfer(void *context,qa_error *e)
{
    observe_call *call=context; application_native_q2_protection *o=call->owner;
    protection_stage *stage=calloc(1,sizeof(*stage));
    if(!stage) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native protection execution scope"); return false; }
    stage->owner=o; stage->actor=call->actor; stage->observer=call->observer;
    stage->pickup=call->pickup;stage->outer=o->stages;
    stage->active=true; o->stages=stage;
    bool ok=stage_current(stage,e)&&read_all(o,call->actor,&stage->before,e);
    size_t maximum=0;
    for(size_t i=0;ok&&i<o->count;++i) {
        if(o->definitions[i].item_count>SIZE_MAX-maximum) ok=fail(e,"Native protection inventory receipt overflows");
        else maximum+=o->definitions[i].item_count;
    }
    if(ok&&maximum) {
        stage->items=calloc(maximum,sizeof(*stage->items));
        if(!stage->items) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining complete protection inventory receipt"); ok=false; }
    }
    for(protection_actor *a=o->actors;ok&&a;a=a->next) if(qa_actor_id_equal(a->actor,call->actor)&&a->bound) {
        protection_definition *definition=o->definitions+a->definition;
        for(size_t i=0;i<definition->item_count;++i) {
            bool found=false;
            for(size_t j=0;j<stage->item_count;++j) if(stage->items[j]==definition->items[i]) found=true;
            if(!found) stage->items[stage->item_count++]=definition->items[i];
        }
        protection_watch *watch=calloc(1,sizeof(*watch));
        if(!watch) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native protection field watchers"); ok=false; break; }
        watch->stage=stage; watch->actor=a; watch->next=stage->watches; stage->watches=watch;
        ok=application_native_q2_armor_observe(o->definitions[a->definition].armor,call->actor,changed,watch,&watch->watch,e);
    }
    if(ok) ok=call->execute(call->context,e);
    stage->active=false; stage->observer=NULL;stage->pickup=NULL;
    if(o->stages!=stage) {
        if(ok) fail(e,"Native protection retains an unfinished inner observation scope");
        return false;
    }
    qa_error cleanup={0}; bool closed=stage_close(stage,&cleanup);
    if(closed) { o->stages=stage->outer; free(stage); }
    else if(ok&&e) *e=cleanup;
    return ok&&closed;
}
bool application_native_q2_protection_observe(application_native_q2_protection *o,qa_actor_id actor,
    qa_protection_observer *observer,application_native_q2_protection_execute_fn execute,void *context,qa_error *e)
{
    if(!o||!observer||!execute||!source_current(o,actor,e)||(o->stages&&!o->stages->active)) return fail(e,"Native protection observation requires its actual active source scope");
    observe_call call={.owner=o,.actor=actor,.observer=observer,.execute=execute,.context=context};
    return application_native_q2_callbacks_transfer(o->options.callbacks,observe_transfer,&call,e);
}
bool application_native_q2_protection_observe_pickup(application_native_q2_protection *o,qa_actor_id actor,
    qa_pickup_execution *pickup,application_native_q2_protection_execute_fn execute,void *context,qa_error *e)
{
    if(!o||!pickup||!execute||!qa_pickup_recipient_is(pickup,actor)||
        !source_current(o,actor,e)||(o->stages&&!o->stages->active))
        return fail(e,"Native pickup protection requires its actual current recipient scope");
    observe_call call={.owner=o,.actor=actor,.pickup=pickup,.execute=execute,.context=context};
    return application_native_q2_callbacks_transfer(o->options.callbacks,observe_transfer,&call,e);
}
typedef struct absorb_call {
    protection_actor *actor;
    const qa_damage_request *request;
    const qa_damage_geometry *geometry;
    float amount,*saved;
    qa_damage_flags flags;
} absorb_call;
static bool authority_current(void *context,qa_error *e)
{ return current(context,e); }
static bool authority_retain(void *context,qa_error *e)
{
    protection_actor *a=context;
    if(!current(a,e)||a->references==UINT_MAX) return fail(e,"Native protection authority cannot retain its actual source lease");
    ++a->references;
    return true;
}
static void authority_release(void *context)
{
    protection_actor *a=context;
    if(a->references) --a->references;
}
static bool absorb_execute(void *context,qa_error *e)
{
    absorb_call *call=context; protection_actor *a=call->actor;
    application_native_q2_source_authority authority={.context=a,.current=authority_current,
        .retain=authority_retain,.release=authority_release};
    return current(a,e)&&application_native_q2_callbacks_protection_absorb(a->owner->options.callbacks,
        a->owner->definitions[a->definition].source,call->request,call->geometry,call->amount,call->flags,&authority,call->saved,e);
}
static bool absorb_binding(void *context,const qa_damage_request *request,const qa_damage_geometry *geometry,
    float amount,qa_damage_flags flags,qa_protection_observer *observer,float *saved,qa_error *e)
{
    protection_actor *a=context;
    if(!request||!saved||!qa_actor_id_equal(request->target,a->actor)||!current(a,e)) return fail(e,"Native protection absorption lost its selected actor");
    absorb_call call={a,request,geometry,amount,saved,flags};
    return application_native_q2_protection_observe(a->owner,a->actor,observer,absorb_execute,&call,e);
}
bool application_native_q2_protection_reserve(application_native_q2_protection *o,qa_actor_id actor,qa_error *e)
{
    if(!o||!application_native_q2_callbacks_client_reserved_current(o->options.callbacks,actor,e)) return false;
    for(size_t i=0;i<o->count;++i) {
        protection_actor *found=NULL;
        for(protection_actor *a=o->actors;a;a=a->next) if(a->definition==i&&qa_actor_id_equal(a->actor,actor)) found=a;
        if(found) { if(!qa_combat_protection_current(o->options.combat,found->lease)) return false; continue; }
        protection_definition *definition=o->definitions+i;
        for(size_t j=0;j<definition->item_count;++j) {
            qa_inventory_entry entry;
            if(!qa_inventory_entry_read(o->options.inventory,actor,definition->items[j],&entry,e)) return false;
        }
        protection_actor *a=calloc(1,sizeof(*a));
        if(!a) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native protection reservation"); return false; }
        a->owner=o; a->actor=actor; a->definition=i;
        if(!qa_combat_reserve_protection(o->options.combat,actor,definition->channel,&definition->claim,&a->lease,e)) { free(a); return false; }
        protection_actor **tail=&o->actors;
        while(*tail) tail=&(*tail)->next;
        *tail=a;
    }
    return true;
}
bool application_native_q2_protection_bind(application_native_q2_protection *o,qa_actor_id actor,qa_error *e)
{
    if(!source_current(o,actor,e)||!application_native_q2_protection_reserve(o,actor,e)) return false;
    for(protection_actor *a=o->actors;a;a=a->next) if(!a->bound&&qa_actor_id_equal(a->actor,actor)) {
        qa_protection_binding binding={.context=a,.read=read_binding,.validate_write=validate_binding,.write=write_binding,.absorb=absorb_binding};
        if(!qa_combat_bind_protection(o->options.combat,a->lease,&binding,e)) return false;
        a->bound=true;
    }
    return true;
}
bool application_native_q2_protection_item(void *context,qa_actor_id actor,qa_protection_channel channel,
    qa_item_id item,bool *found,qa_error *e)
{
    application_native_q2_protection *o=context;
    if(!o||!found||!source_current(o,actor,e)) return false;
    *found=false;
    for(protection_actor *a=o->actors;a;a=a->next) if(a->bound&&qa_actor_id_equal(a->actor,actor)&&o->definitions[a->definition].channel==channel) {
        if(!current(a,e)||!qa_combat_protection_bound(o->options.combat,a->lease)) return false;
        protection_definition *definition=o->definitions+a->definition;
        for(size_t i=0;i<definition->item_count;++i) if(definition->items[i]==item) *found=true;
    }
    return true;
}
bool application_native_q2_protection_restored_inventory(application_native_q2_protection *o,qa_error *e)
{
    if(!o||o->stages) return fail(e,"Native protection restore retains an entered source scope");
    for(protection_actor *a=o->actors;a;a=a->next) if(a->bound) {
        if(!current(a,e)) return false;
        protection_definition *definition=o->definitions+a->definition;
        for(size_t i=0;i<definition->count;++i) {
            protection_counter *counter=definition->counters+i; double raw,count;
            if(!counter->inventory) continue;
            if(!application_native_q2_field_read(o->options.callbacks,a->actor,&counter->field,&raw,e)||
                !qa_inventory_count_read(o->options.inventory,a->actor,counter->inventory,&count,e)) return false;
            if(raw!=count) return fail(e,"Restored native protection counter differs from canonical inventory");
        }
    }
    return true;
}
static bool codec_prefix(application_native_q2_protection *o,qa_source_save_io *io)
{
    uint8_t magic[4]={'N','Q','P','R'},expected[4]={'N','Q','P','R'};
    uint32_t version=1; size_t count=o->count;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||!qa_source_save_u32(io,&version)||version!=1||
        !qa_source_save_count(io,&count,o->count)||count!=o->count) return fail(io->error,"Native protection codec differs from its declaration");
    for(size_t i=0;i<count;++i) {
        qa_protection_claim expected_claim=o->definitions[i].claim,claim=expected_claim;
        uint32_t channel=(uint32_t)o->definitions[i].channel,admission=(uint32_t)claim.admission;
        if(!qa_source_save_string(io,&claim.owner)||!qa_source_save_string(io,&claim.expected_owner)||
            !qa_source_save_string(io,&claim.rule)||!qa_source_save_u32(io,&channel)||!qa_source_save_u32(io,&admission)||
            claim.owner!=expected_claim.owner||claim.expected_owner!=expected_claim.expected_owner||claim.rule!=expected_claim.rule||
            channel!=(uint32_t)o->definitions[i].channel||admission!=(uint32_t)expected_claim.admission)
            return fail(io->error,"Saved native protection claim differs from its acquired declaration");
    }
    return true;
}
bool application_native_q2_protection_checkpoint(application_native_q2_protection *o,qa_buffer *out,qa_error *e)
{
    if(!o||!out||out->data||out->size||!application_native_q2_protection_idle(o)||
        !application_native_q2_callbacks_current(o->options.callbacks)) return fail(e,"Native protection capture requires its idle source");
    qa_source_save_io io;
    if(!qa_source_save_writer(&io,o->options.session,e)) return false;
    size_t count=0; for(protection_actor *a=o->actors;a;a=a->next) ++count;
    bool ok=codec_prefix(o,&io)&&qa_source_save_count(&io,&count,UINT32_MAX);
    for(protection_actor *a=o->actors;ok&&a;a=a->next) {
        qa_actor_owner owner=0; uint64_t serial=0;
        ok=application_native_q2_callbacks_client_reserved_current(o->options.callbacks,a->actor,e)&&!a->restoring&&
            qa_combat_protection_owner(o->options.combat,a->actor,a->lease.channel,&owner,&serial)&&
            owner==o->options.owner&&serial==a->lease.serial&&
            qa_combat_protection_bound(o->options.combat,a->lease)==a->bound;
        qa_actor_id actor=a->actor; size_t definition=a->definition; bool bound=a->bound;
        if(ok) ok=qa_source_save_actor(&io,&actor)&&qa_source_save_count(&io,&definition,o->count)&&
            qa_source_save_u64(&io,&serial)&&qa_source_save_bool(&io,&bound);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_native_q2_protection_restore(application_native_q2_protection *o,qa_bytes bytes,qa_error *e)
{
    if(!o||o->actors||!application_native_q2_protection_idle(o)||!application_native_q2_callbacks_restoring(o->options.callbacks))
        return fail(e,"Native protection import requires its empty actual restoring source");
    qa_source_save_io io;
    if(!qa_source_save_reader(&io,o->options.session,bytes,e)) return false;
    size_t count=0;
    bool ok=codec_prefix(o,&io)&&qa_source_save_count(&io,&count,UINT32_MAX);
    protection_actor *head=NULL,**tail=&head;
    for(size_t i=0;ok&&i<count;++i) {
        protection_actor *a=calloc(1,sizeof(*a));
        if(!a) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining restored native protection leases"); ok=false; break; }
        a->owner=o; a->restoring=true;
        ok=qa_source_save_actor(&io,&a->actor)&&qa_source_save_count(&io,&a->definition,o->count)&&
            qa_source_save_u64(&io,&a->lease.serial)&&qa_source_save_bool(&io,&a->bound)&&
            a->definition<o->count&&a->lease.serial&&
            qa_actors_get(qa_session_actors(o->options.session),a->actor);
        if(ok) { a->lease.actor=a->actor; a->lease.channel=o->definitions[a->definition].channel; }
        for(protection_actor *prior=head;ok&&prior;prior=prior->next)
            if(prior->lease.serial==a->lease.serial||(prior->definition==a->definition&&qa_actor_id_equal(prior->actor,a->actor)))
                ok=fail(e,"Saved native protection lease repeats its actual ownership");
        if(!ok) { free(a); break; }
        *tail=a; tail=&a->next;
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) { while(head) { protection_actor *next=head->next; free(head); head=next; } return false; }
    o->actors=head; return true;
}
bool application_native_q2_protection_saved_binding(application_native_q2_protection *o,qa_actor_id actor,
    qa_protection_channel channel,const qa_protection_claim *claim,qa_protection_binding *out,qa_error *e)
{
    if(!o||!claim||!out||!application_native_q2_callbacks_restoring(o->options.callbacks))
        return fail(e,"Saved native protection requires its actual restoring source");
    for(protection_actor *a=o->actors;a;a=a->next) if(a->bound&&a->restoring&&a->lease.channel==channel&&qa_actor_id_equal(a->actor,actor)) {
        const qa_protection_claim *actual=&o->definitions[a->definition].claim;
        if(claim->owner!=actual->owner||claim->expected_owner!=actual->expected_owner||claim->rule!=actual->rule||claim->admission!=actual->admission||
            !current(a,e)) return fail(e,"Saved native protection claim differs from its retained source lease");
        *out=(qa_protection_binding){.context=a,.read=read_binding,.validate_write=validate_binding,.write=write_binding,.absorb=absorb_binding};
        return true;
    }
    return fail(e,"Saved native protection has no retained bound source channel");
}
bool application_native_q2_protection_finish_restore(application_native_q2_protection *o,qa_error *e)
{
    if(!o) return true;
    if(!application_native_q2_protection_idle(o)) return fail(e,"Native protection restore retains source observation");
    for(protection_actor *a=o->actors;a;a=a->next) {
        qa_actor_owner owner=0; uint64_t serial=0;
        if(!application_native_q2_callbacks_client_reserved_current(o->options.callbacks,a->actor,e)||
            !qa_combat_protection_owner(o->options.combat,a->actor,a->lease.channel,&owner,&serial)||
            owner!=o->options.owner||serial!=a->lease.serial||qa_combat_protection_bound(o->options.combat,a->lease)!=a->bound)
            return fail(e,"Restored native protection differs from its real canonical lease");
    }
    for(protection_actor *a=o->actors;a;a=a->next) a->restoring=false;
    return application_native_q2_protection_restored_inventory(o,e);
}
bool application_native_q2_protection_release(application_native_q2_protection *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    for(protection_stage *s=o->stages;s;s=s->outer) if(qa_actor_id_equal(s->actor,actor)) return fail(e,"Native protection actor retains source write scopes");
    protection_actor **link=&o->actors;
    while(*link) {
        protection_actor *a=*link;
        if(!qa_actor_id_equal(a->actor,actor)) { link=&a->next; continue; }
        if(a->references) return fail(e,"Native protection retains pending source-region authority");
        if(!qa_combat_close_protection(o->options.combat,a->lease,e)) return false;
        *link=a->next; free(a);
    }
    return true;
}
bool application_native_q2_protection_idle(const application_native_q2_protection *o)
{
    if(!o) return true;
    if(o->stages||o->writing) return false;
    for(protection_actor *a=o->actors;a;a=a->next) if(a->references) return false;
    return true;
}
bool application_native_q2_protection_drain(application_native_q2_protection *o,qa_error *e)
{
    if(!o) return true;
    if(o->writing) return fail(e,"Native protection storage is entered");
    for(protection_stage *s=o->stages;s;s=s->outer)
        if(s->active||s->suppressed) return fail(e,"Native protection observation is entered");
    while(o->stages) {
        protection_stage *stage=o->stages;
        if(!stage_close(stage,e)) return false;
        o->stages=stage->outer; free(stage);
    }
    return true;
}
bool application_native_q2_protection_destroy(application_native_q2_protection **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_protection *o=*owner;
    if(!application_native_q2_protection_drain(o,e)) return false;
    while(o->actors) if(!application_native_q2_protection_release(o,o->actors->actor,e)) return false;
    if(o->definitions) for(size_t i=0;i<o->count;++i) {
        protection_definition *definition=o->definitions+i;
        if(!application_native_q2_armor_destroy(&definition->armor,e)) return false;
        if(definition->counters) for(size_t j=0;j<definition->count;++j) application_native_q2_field_dispose(&definition->counters[j].field);
        free(definition->counters); free(definition->items);
        definition->counters=NULL; definition->items=NULL; definition->count=definition->item_count=0;
    }
    free(o->definitions); free(o); *owner=NULL; return true;
}
