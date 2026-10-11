#include "native_q2_items.h"
#include "native_q2_records.h"
#include "qa/native_observe.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum item_capacity_kind { ITEM_CONSTANT,ITEM_FIELD,ITEM_SOURCE } item_capacity_kind;
typedef struct item_storage {
    application_native_q2_field field,capacity_field;
    qa_item_id item;
    item_capacity_kind capacity_kind;
    double capacity;
    qa_json_id source;
    qa_native_value_type source_encoding;
    qa_item_bit *bits;
    size_t bit_count;
    uint32_t private_mask;
} item_storage;
typedef struct item_definition {
    qa_json_id use,drop;
    char *label;
} item_definition;
typedef struct item_watch {
    struct item_actor *actor;
    size_t storage;
    qa_native_address address;
    const application_native_q2_field *field;
    qa_native_write_observer *binding;
} item_watch;
typedef struct item_actor {
    struct item_actor *next;
    application_native_q2_items *owner;
    qa_actor_id actor;
    qa_inventory_lease lease;
    item_watch *watches;
    size_t watch_count;
    qa_inventory_entry *previous;
    unsigned entered;
    bool admitting,releasing,ready,restore_inventory;
} item_actor;
struct application_native_q2_item_receipt {
    application_native_q2_item_receipt *previous;
    item_actor *entry;
    qa_inventory_lease lease;
    qa_error failure;
    bool retired;
};
struct application_native_q2_items {
    application_native_q2_items_options options;
    qa_item_admission *admissions;
    item_definition *definitions;
    item_storage *storage;
    size_t count,storage_count;
    qa_json_id weapon_stage;
    item_actor *actors;
    application_native_q2_item_receipt *receipts;
    unsigned writing,calls;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_FORMAT,0,"%s",text); return false; }
static bool text(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer bytes={0}; if(!qa_json_string(d,id,&bytes,e)) return false;
    if(!bytes.size||memchr(bytes.data,0,bytes.size)) { qa_buffer_free(&bytes); return fail(e,"Native item text is empty or contains NUL"); }
    *out=(char *)bytes.data; return true;
}
static bool label_read(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer bytes={0};
    if(!qa_json_string(d,id,&bytes,e)) return false;
    if(memchr(bytes.data,0,bytes.size)) { qa_buffer_free(&bytes); return fail(e,"Native item label contains NUL"); }
    *out=(char *)bytes.data;
    return true;
}
static bool identity(application_native_q2_items *o,qa_json_id id,qa_item_id *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    if(qa_json_type(d,id)==QA_JSON_NULL) { *out=0; return true; }
    char *name=NULL; if(!text(d,id,&name,e)) return false;
    bool ok=strchr(name,':')&&qa_strings_intern_cstr(qa_session_strings(o->options.session),name,out,e); free(name);
    return ok||fail(e,"Native item identity is not namespaced");
}
static qa_native_value_type scalar(const qa_json_document *d,qa_json_id id)
{
    const char *names[]={"void","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    for(size_t i=1;i<sizeof(names)/sizeof(*names);++i) if(qa_json_string_equal(d,id,names[i])) return (qa_native_value_type)i;
    return QA_NATIVE_VOID;
}
static item_actor *actor_find(application_native_q2_items *o,qa_actor_id actor)
{ for(item_actor *a=o?o->actors:NULL;a;a=a->next) if(qa_actor_id_equal(a->actor,actor)) return a; return NULL; }
static bool retired(item_actor *a,qa_error *e,const char *message)
{
    qa_error failure={0};fail(&failure,message);
    for(application_native_q2_item_receipt *r=a->owner->receipts;r;r=r->previous)
        if(r->entry==a&&r->lease.serial==a->lease.serial&&qa_actor_id_equal(r->lease.actor,a->actor)) {
            r->retired=true;r->failure=failure;
        }
    if(e) *e=failure;
    return false;
}
static bool current_read(item_actor *a,bool *valid,qa_error *e)
{
    *valid=false;
    application_native_q2_items *o=a?a->owner:NULL;
    if(!o||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    if(a->releasing||!qa_actors_get(qa_session_actors(o->options.session),a->actor)||
        (!a->admitting&&!(a->restore_inventory&&application_native_q2_records_restoring(
            application_native_q2_callbacks_records(o->options.callbacks)))&&
            !qa_inventory_lease_current(o->options.inventory,a->lease))) return true;
    bool live=false;
    if(!application_native_q2_callbacks_client_live_read(o->options.callbacks,a->actor,&live,e)) return false;
    if(!live) return true;
    for(size_t i=0;i<a->watch_count;++i) {
        item_watch *watch=a->watches+i; qa_native_address address;
        bool ok=watch->field?application_native_q2_field_address(o->options.callbacks,a->actor,watch->field,&address,e):
            application_native_q2_callbacks_address(o->options.callbacks,o->storage[watch->storage].source,&address,e);
        if(!ok) return false;
        if(address!=watch->address) return true;
    }
    *valid=true;return true;
}
static bool current(item_actor *a,qa_error *e)
{
    bool valid;
    if(!current_read(a,&valid,e)) return false;
    return valid||retired(a,e,"Native source item retained actor, lease or storage retired");
}
static bool capacity(item_actor *a,const item_storage *s,double *out,qa_error *e)
{
    if(s->capacity_kind==ITEM_CONSTANT) { *out=s->capacity; return true; }
    if(s->capacity_kind==ITEM_FIELD) return application_native_q2_field_read(a->owner->options.callbacks,a->actor,&s->capacity_field,out,e);
    qa_native_address address;
    return application_native_q2_callbacks_address(a->owner->options.callbacks,s->source,&address,e)&&
        application_native_q2_callbacks_scalar_read(a->owner->options.callbacks,address,s->source_encoding,out,e);
}
static size_t storage_index(application_native_q2_items *o,size_t storage)
{ size_t n=0; for(size_t i=0;i<storage;++i) n+=o->storage[i].bit_count?o->storage[i].bit_count:1; return n; }
static bool storage_read(item_actor *a,size_t index,qa_inventory_entry *out,qa_error *e)
{
    const item_storage *s=a->owner->storage+index; double count;
    if(!application_native_q2_field_read(a->owner->options.callbacks,a->actor,&s->field,&count,e)) return false;
    if(!s->bit_count) {
        double cap; if(!capacity(a,s,&cap,e)) return false;
        *out=(qa_inventory_entry){s->item,count,cap,s->field.encoding==QA_NATIVE_F32?QA_COUNT_SOURCE_FLOAT:
            s->field.encoding==QA_NATIVE_I32?QA_COUNT_SOURCE_INT32:QA_COUNT_SOURCE_DOUBLE}; return true;
    }
    if(count!=trunc(count)||fabs(count)>9007199254740991.0) return fail(e,"Native packed items require their exact integer word");
    uint32_t bits=(uint32_t)(int64_t)count;
    size_t bytes=application_native_q2_field_size(s->field.encoding);
    if(bytes<4) bits&=(UINT32_C(1)<<(bytes*8))-1;
    uint32_t mask=s->private_mask;
    for(size_t i=0;i<s->bit_count;++i) mask|=s->bits[i].mask;
    if(bits&~mask) return fail(e,"Native source wrote an undeclared item bit");
    for(size_t i=0;i<s->bit_count;++i) out[i]=(qa_inventory_entry){s->bits[i].item,(bits&s->bits[i].mask)?1:0,1,QA_COUNT_STACK};
    return true;
}
static bool pickup_check(item_actor *a,const qa_inventory_entry *before,const qa_inventory_entry *after,qa_error *e)
{
    const qa_pickup_execution *execution=NULL; bool found=false;
    if(!qa_pickups_execution_read(a->owner->options.pickups,a->actor,0,&execution,&found,e)) return false;
    if(!found) return true;
    if(!qa_pickup_recipient_is(execution,a->actor)) return fail(e,"Native item store left its actual pickup recipient");
    size_t count=0; const qa_pickup_write *writes=qa_pickup_writes(execution,&count);
    for(size_t i=0;i<count;++i) if(writes[i].resource.kind==QA_PICKUP_INVENTORY&&writes[i].resource.item==after->item&&
        (before->count==after->count||writes[i].fields!=QA_PICKUP_CAPACITY)&&
        (before->capacity==after->capacity||writes[i].fields!=QA_PICKUP_COUNT)) return true;
    return fail(e,"Original native pickup changed an undeclared source item field");
}
static bool changed(item_actor *a,qa_item_id canonical,size_t storage,qa_error *e)
{
    application_native_q2_items *o=a->owner;
    if(o->writing||a->releasing) return true;
    bool valid;
    if(!current_read(a,&valid,e)) return false;
    if(!valid) return true;
    qa_inventory_entry *after=o->count?calloc(o->count,sizeof(*after)):NULL;
    qa_inventory_change *changes=o->count?calloc(o->count,sizeof(*changes)):NULL;
    if(o->count&&(!after||!changes)) { free(after); free(changes); qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native committed item values"); return false; }
    memcpy(after,a->previous,o->count*sizeof(*after));
    bool ok=true; size_t count=0;
    for(size_t i=0;ok&&i<o->storage_count;++i)
        if(storage==SIZE_MAX||storage==i) ok=storage_read(a,i,after+storage_index(o,i),e);
    for(size_t i=0;ok&&i<o->count;++i) if(after[i].item!=canonical&&
        (a->previous[i].count!=after[i].count||a->previous[i].capacity!=after[i].capacity)) {
        ok=pickup_check(a,a->previous+i,after+i,e);
        if(ok) changes[count++]=(qa_inventory_change){true,a->actor,a->previous[i],after[i]};
    }
    if(ok) {
        memcpy(a->previous,after,o->count*sizeof(*after));
        ++a->entered; ++o->calls;
        bool retired_token=false;
        ok=!count||qa_inventory_source_stored_ex(o->options.inventory,a->lease,changes,count,&retired_token,e);
        if(retired_token) ok=retired(a,e,"Native item Source lease retired during stored changes");
        --o->calls; --a->entered;
        if(ok&&count) ok=current(a,e);
    }
    free(after); free(changes); return ok;
}
static size_t count_binding(void *context)
{ return ((item_actor *)context)->owner->count; }
static bool checked_count(void *context,size_t *out,qa_error *e)
{ item_actor *a=context; if(!current(a,e)) return false; *out=a->owner->count; return true; }
static bool at(void *context,size_t index,qa_inventory_entry *out,qa_error *e)
{
    item_actor *a=context; if(!out||!current(a,e)||index>=a->owner->count) return false;
    for(size_t i=0;i<a->owner->storage_count;++i) {
        item_storage *s=a->owner->storage+i; size_t n=s->bit_count?s->bit_count:1;
        if(index>=n) { index-=n; continue; }
        qa_inventory_entry rows[32];
        if(n>sizeof(rows)/sizeof(*rows)) return fail(e,"Native packed item declaration exceeds its actual word");
        bool ok=storage_read(a,i,rows,e); if(ok) *out=rows[index]; return ok&&current(a,e);
    }
    return false;
}
static item_storage *storage_for(application_native_q2_items *o,qa_item_id item,size_t *bit)
{
    for(size_t i=0;i<o->storage_count;++i) {
        item_storage *s=o->storage+i;
        if(!s->bit_count&&s->item==item) { *bit=0; return s; }
        for(size_t j=0;j<s->bit_count;++j) if(s->bits[j].item==item) { *bit=j; return s; }
    }
    return NULL;
}
static bool mutable_capacity(void *context,qa_item_id item)
{
    item_actor *a=context; size_t bit; item_storage *s=storage_for(a->owner,item,&bit);
    return s&&!s->bit_count&&s->capacity_kind==ITEM_FIELD;
}
static bool write_binding(void *context,const qa_inventory_entry *entry,qa_error *e)
{
    item_actor *a=context; application_native_q2_items *o=a->owner; size_t bit;
    if(!entry||!current(a,e)) return false;
    item_storage *s=storage_for(o,entry->item,&bit); if(!s) return false;
    double count=entry->count;
    if(s->bit_count) {
        if(entry->capacity!=1||(count!=0&&count!=1)) return fail(e,"Native item requires its declared ownership bit");
        double before; if(!application_native_q2_field_read(o->options.callbacks,a->actor,&s->field,&before,e)) return false;
        uint32_t bits=(uint32_t)(int64_t)before;
        bits=count!=0?bits|s->bits[bit].mask:bits&~s->bits[bit].mask;
        size_t width=application_native_q2_field_size(s->field.encoding)*8;
        bool signed_value=s->field.encoding==QA_NATIVE_I8||s->field.encoding==QA_NATIVE_I16||s->field.encoding==QA_NATIVE_I32;
        if(signed_value) {
            uint32_t mask=width==32?UINT32_MAX:(UINT32_C(1)<<width)-1;
            bits&=mask; int64_t signed_bits=(bits&(UINT32_C(1)<<(width-1)))?(int64_t)bits-(INT64_C(1)<<width):bits;
            count=(double)signed_bits;
        } else count=bits;
    } else {
        double cap; if(!capacity(a,s,&cap,e)||!application_native_q2_field_value(s->field.encoding,count,e)||
            (s->capacity_kind!=ITEM_FIELD&&entry->capacity!=cap)||
            (s->capacity_kind==ITEM_FIELD&&!application_native_q2_field_value(s->capacity_field.encoding,entry->capacity,e))) return fail(e,"Native item write changes immutable capacity or exceeds original storage");
    }
    ++o->writing;
    bool ok=application_native_q2_field_write(o->options.callbacks,a->actor,&s->field,count,e);
    if(ok&&!s->bit_count&&s->capacity_kind==ITEM_FIELD) ok=application_native_q2_field_write(o->options.callbacks,a->actor,&s->capacity_field,entry->capacity,e);
    --o->writing;
    qa_error cleanup={0}; bool delivered=changed(a,entry->item,SIZE_MAX,&cleanup);
    if(ok&&!delivered&&e) *e=cleanup;
    return ok&&delivered;
}
static bool action(void *context,qa_item_id item,qa_item_action action_kind,qa_error *e)
{
    item_actor *a=context; application_native_q2_items *o=a->owner;
    if(!current(a,e)||(action_kind!=QA_ITEM_USE&&action_kind!=QA_ITEM_DROP)) return false;
    for(size_t i=0;i<o->count;++i) if(o->admissions[i].definition.item==item) {
        qa_json_id call=action_kind==QA_ITEM_USE?o->definitions[i].use:o->definitions[i].drop; double time,ignored;
        if(call==QA_JSON_NONE||!application_native_q2_callbacks_time_read(o->options.callbacks,&time,e)) return false;
        application_native_callback_value values[]={
            {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=a->actor},
            {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=time}};
        application_native_callback_inputs inputs={.values=values,.count=2};
        ++a->entered; ++o->calls;
        bool ok=application_native_q2_callbacks_call(o->options.callbacks,call,&inputs,&ignored,e);
        --o->calls; --a->entered; return ok&&current(a,e);
    }
    return false;
}
static void binding(item_actor *a,qa_inventory_items *out)
{
    *out=(qa_inventory_items){.owner=a->owner->options.owner,.items=a->owner->admissions,.count=a->owner->count,
        .state={.context=a,.count=count_binding,.checked_count=checked_count,.at=at,.write=write_binding,.mutable_capacity=mutable_capacity},
        .action_context=a,.invoke=action};
}
static bool watched(void *context,qa_native_instance *instance,const qa_native_write_event *event,qa_error *e)
{
    item_watch *watch=context; item_actor *a=watch->actor; (void)event;
    if(instance!=application_native_q2_callbacks_instance(a->owner->options.callbacks)) return fail(e,"Native item watch changed its source instance");
    return changed(a,0,watch->storage,e);
}
static bool field_allowed(application_native_q2_items *o,const application_native_q2_field *field,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    qa_json_id clients=qa_json_get(d,qa_json_root(d),"clients"),rows=qa_json_get(d,clients,"records");
    for(size_t i=0;i<qa_json_size(d,rows);++i) if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_at(d,rows,i))==field->record) return true;
    return fail(e,"Native source item field is outside its actual admitted client records");
}
static bool occupied(application_native_q2_items *o,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    qa_json_id records=qa_json_get(d,qa_json_root(d),"actorRecords");
    uint8_t pointer=qa_native_module_describe(qa_native_get_module(application_native_q2_callbacks_instance(o->options.callbacks))).image.target.pointer_bytes;
    for(size_t i=0;i<o->storage_count;++i) {
        item_storage *s=o->storage+i;
        for(size_t f=0;f<(s->capacity_kind==ITEM_FIELD&&!s->bit_count?2u:1u);++f) {
            const application_native_q2_field *field=f?&s->capacity_field:&s->field;
            if(!field_allowed(o,field,e)) return false;
            size_t bytes=application_native_q2_field_size(field->encoding);
            for(size_t j=0;j<=i;++j) {
                item_storage *old=o->storage+j;
                for(size_t k=0;k<(old->capacity_kind==ITEM_FIELD&&!old->bit_count?2u:1u);++k) {
                    if(j==i&&k>=f) break;
                    const application_native_q2_field *other=k?&old->capacity_field:&old->field;
                    if(field->record!=other->record||field->offset>=(uint64_t)other->offset+application_native_q2_field_size(other->encoding)||
                        other->offset>=(uint64_t)field->offset+bytes) continue;
                    if(f==1&&k==1&&field->offset==other->offset&&field->encoding==other->encoding) continue;
                    return fail(e,"Native item storage overlaps another exclusive field");
                }
            }
            for(size_t r=0;r<qa_json_size(d,records);++r) {
                qa_json_id record=qa_json_at(d,records,r); if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,record,"id"))!=field->record) continue;
                qa_json_id fields=qa_json_get(d,record,"fields");
                for(size_t k=0;k<qa_json_size(d,fields);++k) {
                    qa_json_id projection=qa_json_at(d,fields,k),kind=qa_json_get(d,projection,"binding"); uint64_t offset,length;
                    if(qa_json_string_equal(d,kind,"private")||qa_json_string_equal(d,kind,"constant")) continue;
                    if(!qa_json_u64(d,qa_json_get(d,projection,"offset"),&offset,e)) return false;
                    qa_native_value_type type=scalar(d,qa_json_get(d,projection,"encoding"));
                    length=application_native_q2_field_size(type);
                    if(!length) length=qa_json_string_equal(d,kind,"record")||qa_json_string_equal(d,kind,"address")?pointer:12;
                    if(offset<(uint64_t)field->offset+bytes&&field->offset<offset+length) return fail(e,"Native item storage overlaps another canonical projection");
                }
            }
        }
    }
    return true;
}
static bool create_layout(const application_native_q2_items_options *options,application_native_q2_items **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->session||!options->inventory||!options->pickups||!options->owner) return fail(e,"Native items require actual acquired Source services");
    const qa_json_document *d=application_native_q2_callbacks_document(options->callbacks);
    qa_json_id root=qa_json_root(d),items=qa_json_get(d,root,"items");
    if(items==QA_JSON_NONE) return true;
    qa_json_id defs=qa_json_get(d,items,"definitions"),storage=qa_json_get(d,items,"storage");
    if(qa_json_get(d,root,"clients")==QA_JSON_NONE||qa_json_type(d,defs)!=QA_JSON_ARRAY||!qa_json_size(d,defs)||qa_json_type(d,storage)!=QA_JSON_ARRAY)
        return fail(e,"Native items require real definitions, storage and client admission");
    application_native_q2_items *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native item declaration"); return false; }
    *out=o; o->options=*options; o->count=qa_json_size(d,defs); o->storage_count=qa_json_size(d,storage);
    o->weapon_stage=qa_json_get(d,items,"weapons");
    o->definitions=calloc(o->count,sizeof(*o->definitions)); o->admissions=calloc(o->count,sizeof(*o->admissions));
    o->storage=o->storage_count?calloc(o->storage_count,sizeof(*o->storage)):NULL;
    if(!o->definitions||!o->admissions||(o->storage_count&&!o->storage)) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native item fields and admissions"); return false; }
    size_t weapons=0;
    for(size_t i=0;i<o->count;++i) {
        qa_json_id row=qa_json_at(d,defs,i),kind=qa_json_get(d,row,"kind"),actions=qa_json_get(d,row,"actions");
        item_definition *definition=o->definitions+i; qa_item_admission *admission=o->admissions+i;
        definition->use=qa_json_get(d,actions,"use"); definition->drop=qa_json_get(d,actions,"drop");
        if(!identity(o,qa_json_get(d,row,"item"),&admission->definition.item,e)||!admission->definition.item||!label_read(d,qa_json_get(d,row,"label"),&definition->label,e)) return false;
        admission->definition.label=definition->label; admission->definition.owner=options->owner;
        if(qa_json_string_equal(d,kind,"weapon")) {
            ++weapons; admission->definition.weapon=true;
            if(!identity(o,qa_json_get(d,row,"ammo"),&admission->definition.ammo,e)) return false;
        } else if(!qa_json_string_equal(d,kind,"counter")) return fail(e,"Native item kind is undeclared");
        admission->definition.actions=(definition->use==QA_JSON_NONE?0u:QA_ITEM_USE)|(definition->drop==QA_JSON_NONE?0u:QA_ITEM_DROP);
        if(qa_json_string_equal(d,qa_json_get(d,row,"admission"),"replace-primary")) admission->replace_primary=true;
        else if(!qa_json_string_equal(d,qa_json_get(d,row,"admission"),"add")) return fail(e,"Native item admission is undeclared");
        for(size_t j=0;j<i;++j) if(o->admissions[j].definition.item==admission->definition.item) return fail(e,"Native source item definition repeats");
    }
    if((weapons!=0)!=(o->weapon_stage!=QA_JSON_NONE)) return fail(e,"Native weapon items require their original dispatcher");
    size_t rows=0;
    for(size_t i=0;i<o->storage_count;++i) {
        item_storage *s=o->storage+i; qa_json_id row=qa_json_at(d,storage,i),kind=qa_json_get(d,row,"kind");
        if(!application_native_q2_field_parse(options->callbacks,qa_json_get(d,row,"field"),&s->field,e)) return false;
        if(qa_json_string_equal(d,kind,"counter")) {
            if(!identity(o,qa_json_get(d,row,"item"),&s->item,e)||!s->item) return false;
            qa_json_id capacity_id=qa_json_get(d,row,"capacity"),capacity_kind=qa_json_get(d,capacity_id,"kind");
            if(qa_json_string_equal(d,capacity_kind,"constant")) {
                s->capacity_kind=ITEM_CONSTANT;
                if(!qa_json_number(d,qa_json_get(d,capacity_id,"value"),&s->capacity,e)||!isfinite(s->capacity)||s->capacity<0) return fail(e,"Native source item capacity is invalid");
            } else if(qa_json_string_equal(d,capacity_kind,"field")) {
                s->capacity_kind=ITEM_FIELD;
                if(!application_native_q2_field_parse(options->callbacks,qa_json_get(d,capacity_id,"field"),&s->capacity_field,e)) return false;
            } else if(qa_json_string_equal(d,capacity_kind,"source")) {
                s->capacity_kind=ITEM_SOURCE; s->source=qa_json_get(d,capacity_id,"address");
                s->source_encoding=scalar(d,qa_json_get(d,capacity_id,"encoding")); qa_native_address address;
                if(!application_native_q2_field_size(s->source_encoding)||!application_native_q2_callbacks_address(options->callbacks,s->source,&address,e)) return false;
            } else return fail(e,"Native source item capacity owner is undeclared");
            ++rows;
        } else if(qa_json_string_equal(d,kind,"bits")) {
            size_t bytes=application_native_q2_field_size(s->field.encoding);
            if(bytes>4||s->field.encoding==QA_NATIVE_F32) return fail(e,"Native packed inventory requires an integer word");
            uint64_t mask; if(!qa_json_u64(d,qa_json_get(d,row,"privateMask"),&mask,e)||mask>UINT32_MAX) return fail(e,"Native private item mask exceeds uint32");
            s->private_mask=(uint32_t)mask; qa_json_id bits=qa_json_get(d,row,"items");
            if(qa_json_type(d,bits)!=QA_JSON_ARRAY||!qa_json_size(d,bits)) return fail(e,"Native packed inventory has no item bits");
            s->bit_count=qa_json_size(d,bits); s->bits=calloc(s->bit_count,sizeof(*s->bits));
            if(!s->bits) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native packed item definitions"); return false; }
            uint32_t occupied_mask=s->private_mask;
            for(size_t j=0;j<s->bit_count;++j) {
                qa_json_id bit=qa_json_at(d,bits,j); uint64_t value;
                if(!identity(o,qa_json_get(d,bit,"item"),&s->bits[j].item,e)||!s->bits[j].item||
                    !qa_json_u64(d,qa_json_get(d,bit,"mask"),&value,e)||!value||value>UINT32_C(0x80000000)||
                    (value&(value-1))||(occupied_mask&(uint32_t)value)) return fail(e,"Native item bits overlap or exceed original storage");
                s->bits[j].mask=(uint32_t)value; occupied_mask|=(uint32_t)value;
            }
            if(bytes<4&&occupied_mask>=(UINT32_C(1)<<(bytes*8))) return fail(e,"Native packed item mask exceeds its original word");
            if(s->bit_count>SIZE_MAX-rows) return fail(e,"Native item storage row count overflows");
            rows+=s->bit_count;
        } else return fail(e,"Native item storage kind is undeclared");
    }
    if(rows!=o->count) return fail(e,"Native item definitions do not cover source storage");
    for(size_t i=0;i<o->count;++i) {
        qa_item_id item=o->admissions[i].definition.item; size_t found=0;
        for(size_t j=0;j<o->storage_count;++j) {
            item_storage *s=o->storage+j; if(!s->bit_count&&s->item==item) ++found;
            for(size_t k=0;k<s->bit_count;++k) if(s->bits[k].item==item) ++found;
        }
        if(found!=1) return fail(e,"Native source item requires one distinct declared storage");
        if(o->admissions[i].definition.ammo) {
            size_t bit; if(!storage_for(o,o->admissions[i].definition.ammo,&bit)) return fail(e,"Native weapon ammo has no actual source storage");
        }
    }
    return occupied(o,e);
}
bool application_native_q2_items_create(const application_native_q2_items_options *options,application_native_q2_items **out,qa_error *e)
{
    if(!out||*out) return fail(e,"Native items construction requires an empty destination");
    application_native_q2_items *o=NULL;
    if(!create_layout(options,&o,e)) {
        /* Layout parsing installs no actor leases or write subscriptions. */
        (void)application_native_q2_items_destroy(&o,NULL);return false;
    }
    *out=o;return true;
}
static bool actor_prepare_layout(application_native_q2_items *o,qa_actor_id actor,item_actor **out,qa_error *e)
{
    if(!application_native_q2_callbacks_client_current(o->options.callbacks,actor,e)) return false;
    item_actor *a=calloc(1,sizeof(*a));
    if(!a) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native source item actor"); return false; }
    a->owner=o; a->actor=actor; a->admitting=true; *out=a;
    a->previous=calloc(o->count,sizeof(*a->previous));
    size_t maximum=0;
    for(size_t i=0;i<o->storage_count;++i) {
        size_t n=o->storage[i].bit_count||o->storage[i].capacity_kind==ITEM_CONSTANT?1u:2u;
        if(n>SIZE_MAX-maximum) return fail(e,"Native item watch count overflows");
        maximum+=n;
    }
    a->watches=maximum?calloc(maximum,sizeof(*a->watches)):NULL;
    if(!a->previous||(maximum&&!a->watches)) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native item words and watches"); return false; }
    for(size_t i=0;i<o->storage_count;++i) {
        item_storage *s=o->storage+i;
        if(!storage_read(a,i,a->previous+storage_index(o,i),e)) return false;
        size_t n=s->bit_count||s->capacity_kind==ITEM_CONSTANT?1u:2u;
        for(size_t j=0;j<n;++j) {
            item_watch *watch=a->watches+a->watch_count; watch->actor=a; watch->storage=i;
            watch->field=j?(s->capacity_kind==ITEM_FIELD?&s->capacity_field:NULL):&s->field;
            if(!(watch->field?application_native_q2_field_address(o->options.callbacks,actor,watch->field,&watch->address,e):
                application_native_q2_callbacks_address(o->options.callbacks,s->source,&watch->address,e))) return false;
            ++a->watch_count;
        }
    }
    return current(a,e);
}
static bool actor_prepare(application_native_q2_items *o,qa_actor_id actor,item_actor **out,qa_error *e)
{
    item_actor *a=NULL;
    if(!actor_prepare_layout(o,actor,&a,e)) {
        if(a) {free(a->previous);free(a->watches);free(a);}
        return false;
    }
    a->next=o->actors;o->actors=a;*out=a;return true;
}
static bool actor_watch(item_actor *a,qa_error *e)
{
    application_native_q2_items *o=a->owner;
    for(size_t i=0;i<a->watch_count;++i) {
        item_watch *watch=a->watches+i;
        if(watch->binding) continue;
        if(!qa_native_observe_writes(application_native_q2_callbacks_instance(o->options.callbacks),watch->address,
            application_native_q2_field_size(watch->field?watch->field->encoding:o->storage[watch->storage].source_encoding),watched,watch,&watch->binding,e)) return false;
    }
    if(!current(a,e)) return false;
    a->ready=true;
    return true;
}
bool application_native_q2_items_admit(application_native_q2_items *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    item_actor *a=actor_find(o,actor);
    if(a) {
        if(a->admitting||a->restore_inventory||!a->lease.serial)
            return fail(e,"Native item admission differs from its retained inventory lease");
        if(!current(a,e)) return false;
        return a->ready||actor_watch(a,e);
    }
    if(!actor_prepare(o,actor,&a,e)) return false;
    qa_inventory_items items; binding(a,&items);
    if(!qa_inventory_bind_items(o->options.inventory,actor,&items,&a->lease,e)) {
        /* The lower binding publishes a lease only after complete admission. */
        item_actor **link=&o->actors;
        while(*link&&*link!=a) link=&(*link)->next;
        if(*link==a) *link=a->next;
        free(a->previous);free(a->watches);free(a);return false;
    }
    a->admitting=false;
    return actor_watch(a,e);
}
bool application_native_q2_items_actor_current(void *context,qa_actor_id actor,qa_error *e)
{
    item_actor *a=actor_find(context,actor);
    return a&&a->ready&&!a->restore_inventory&&current(a,e);
}
bool application_native_q2_items_actor_admitted(const application_native_q2_items *o,qa_actor_id actor)
{
    for(const item_actor *a=o?o->actors:NULL;a;a=a->next)
        if(qa_actor_id_equal(a->actor,actor)) return a->ready&&!a->restore_inventory&&!a->releasing&&
            qa_inventory_lease_current(o->options.inventory,a->lease);
    return false;
}
bool application_native_q2_items_receipt_begin(application_native_q2_items *o,qa_actor_id actor,
    application_native_q2_item_receipt **out,qa_error *e)
{
    item_actor *a=actor_find(o,actor);
    if(!out||*out||!a||!a->ready||a->restore_inventory||a->entered==UINT_MAX)
        return fail(e,"Native item receipt requires its actual admitted Source lease");
    if(!current(a,e)) return false;
    application_native_q2_item_receipt *r=calloc(1,sizeof(*r));
    if(!r) {qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining lexical native item receipt");return false;}
    r->entry=a;r->lease=a->lease;r->previous=o->receipts;o->receipts=r;++a->entered;*out=r;return true;
}
bool application_native_q2_items_receipt_retired(const application_native_q2_item_receipt *r)
{ return r&&r->retired&&r->entry&&r->lease.serial==r->entry->lease.serial&&qa_actor_id_equal(r->lease.actor,r->entry->actor); }
bool application_native_q2_items_receipt_accepts_error(const application_native_q2_item_receipt *r,const qa_error *e)
{
    return e&&application_native_q2_items_receipt_retired(r)&&e->code==r->failure.code&&
        e->offset==r->failure.offset&&!strcmp(e->message,r->failure.message);
}
bool application_native_q2_items_receipt_source_current(const application_native_q2_item_receipt *r,qa_error *e)
{
    return r&&r->entry&&qa_actor_id_equal(r->lease.actor,r->entry->actor)&&
        application_native_q2_callbacks_client_current(r->entry->owner->options.callbacks,r->entry->actor,e);
}
void application_native_q2_items_receipt_end(application_native_q2_item_receipt **owned)
{
    application_native_q2_item_receipt *r=owned?*owned:NULL;
    if(!r) return;
    application_native_q2_items *o=r->entry->owner;
    application_native_q2_item_receipt **link=&o->receipts;
    while(*link&&*link!=r) link=&(*link)->previous;
    if(*link==r) *link=r->previous;
    --r->entry->entered;free(r);*owned=NULL;
}
bool application_native_q2_items_release(application_native_q2_items *o,qa_actor_id actor,qa_error *e)
{
    item_actor *a=actor_find(o,actor); if(!a) return true;
    if(a->entered) return fail(e,"Native item actor retains entered source callbacks");
    a->releasing=true;
    for(size_t i=a->watch_count;i>0;--i) if(a->watches[i-1].binding) {
        if(!qa_native_unobserve_writes(a->watches[i-1].binding,e)) return false;
        a->watches[i-1].binding=NULL;
    }
    if(a->lease.serial&&qa_inventory_lease_current(o->options.inventory,a->lease)&&!qa_inventory_close_items(o->options.inventory,a->lease,e)) return false;
    item_actor **link=&o->actors; while(*link!=a) link=&(*link)->next; *link=a->next;
    free(a->previous); free(a->watches); free(a); return true;
}
bool application_native_q2_items_inventory_group(application_native_q2_items *o,qa_actor_id actor,uint64_t serial,
    const qa_inventory_source_group *saved,qa_inventory_items *out,qa_error *e)
{
    if(!o||!saved||!out||saved->definitions_only||saved->owner!=o->options.owner||
        !saved->items||!serial||!application_native_q2_records_restoring(application_native_q2_callbacks_records(o->options.callbacks)))
        return fail(e,"Saved native item group has no actual restoring Source owner");
    item_actor *a=actor_find(o,actor);
    if(a) return fail(e,"Saved native item group repeats an actual actor binding");
    if(!actor_prepare(o,actor,&a,e)) return false;
    a->lease=(qa_inventory_lease){actor,serial}; a->admitting=false; a->restore_inventory=true;
    binding(a,out); return current(a,e);
}
bool application_native_q2_items_finish_restore(application_native_q2_items *o,qa_error *e)
{
    if(!o) return true;
    for(item_actor *a=o->actors;a;a=a->next) if(a->restore_inventory) {
        if(!current(a,e)||!qa_inventory_lease_current(o->options.inventory,a->lease)||!actor_watch(a,e)) return false;
        a->restore_inventory=false;
    }
    return true;
}
bool application_native_q2_items_definitions(const application_native_q2_items *o,const qa_item_admission **out,size_t *count,qa_error *e)
{
    if(!o||!out||!count||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    *out=o->admissions; *count=o->count; return true;
}
qa_json_id application_native_q2_items_weapon_stage(const application_native_q2_items *o)
{ return o?o->weapon_stage:QA_JSON_NONE; }
bool application_native_q2_items_idle(const application_native_q2_items *o)
{
    if(!o) return true;
    if(o->calls||o->writing||o->receipts) return false;
    for(item_actor *a=o->actors;a;a=a->next)
        if(!a->ready||a->restore_inventory||a->releasing) return false;
    return true;
}
bool application_native_q2_items_destroy(application_native_q2_items **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_items *o=*owner;
    if(o->calls||o->writing||o->receipts) return fail(e,"Native items retain source execution");
    while(o->actors) if(!application_native_q2_items_release(o,o->actors->actor,e)) return false;
    if(o->definitions) for(size_t i=0;i<o->count;++i) free(o->definitions[i].label);
    if(o->storage) for(size_t i=0;i<o->storage_count;++i) {
        application_native_q2_field_dispose(&o->storage[i].field); application_native_q2_field_dispose(&o->storage[i].capacity_field); free(o->storage[i].bits);
    }
    free(o->definitions); free(o->admissions); free(o->storage); free(o); *owner=NULL; return true;
}
