#include "native_q2_weapon_stage.h"
#include "qa/native_observe.h"
#include "qa/binary.h"
#include "qa/source_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct stage_pointer { size_t record; uint32_t offset; } stage_pointer;
typedef struct stage_test {
    stage_pointer pointer;
    application_native_q2_field field;
    qa_json_id address;
    double expected;
    uint32_t mask;
    bool masked, at_most;
} stage_test;
typedef struct stage_tests {
    stage_test *tests;
    size_t count, group_count;
    size_t *ends;
} stage_tests;
typedef struct stage_choice { qa_item_id item; qa_json_id address, request; } stage_choice;
typedef struct stage_mask { application_native_q2_field field; uint32_t mask; } stage_mask;
typedef struct stage_region {
    application_native_q2_weapon_stage *owner;
    uint32_t id;
    stage_mask *fields;
    size_t count;
    qa_native_region_binding *binding;
} stage_region;
typedef struct stage_saved {
    const stage_mask *field;
    qa_native_address address;
    uint32_t original;
    bool written;
} stage_saved;
typedef struct stage_projection {
    struct stage_projection *next;
    stage_region *region;
    qa_actor_id actor;
    stage_saved *fields;
    size_t count;
} stage_projection;
typedef struct stage_watch {
    struct stage_actor *actor;
    const stage_pointer *field;
    qa_native_address address;
    qa_native_write_observer *binding;
} stage_watch;
typedef struct stage_actor {
    struct stage_actor *next;
    application_native_q2_weapon_stage *owner;
    qa_actor_id actor;
    qa_native_address dispatcher;
    uint64_t request;
    qa_item_id requested;
    qa_weapon_request_status status;
    qa_item_id source_selection;
    stage_watch watches[2];
    size_t watch_count;
    unsigned entered;
    bool bound, releasing, requesting;
} stage_actor;
typedef struct stage_dispatch {
    struct stage_dispatch *previous;
    stage_actor *actor;
    stage_projection *projection_base;
    application_native_q2_item_receipt *items;
    bool committed, reached;
} stage_dispatch;
struct application_native_q2_weapon_stage {
    application_native_q2_weapon_stage_options options;
    qa_json_id definition;
    stage_tests committed, continuations, settled;
    size_t record;
    qa_native_address entry;
    qa_native_type *parameters;
    qa_native_signature signature;
    size_t argument;
    uint8_t pointer_bytes;
    stage_pointer active, pending;
    stage_choice *choices;
    size_t choice_count;
    stage_region *regions;
    size_t region_count;
    qa_native_entry_observer *dispatcher;
    stage_dispatch *dispatch;
    stage_projection *projections;
    stage_actor *actors;
    uint64_t next_request;
    unsigned calls;
};
static bool fail(qa_error *e, qa_status status, const char *text)
{ qa_error_set(e, status, 0, "%s", text); return false; }
static const qa_json_document *document(application_native_q2_weapon_stage *o)
{ return application_native_q2_callbacks_document(o->options.callbacks); }
static qa_native_instance *instance(application_native_q2_weapon_stage *o)
{ return application_native_q2_callbacks_instance(o->options.callbacks); }
static stage_actor *find(application_native_q2_weapon_stage *o, qa_actor_id actor)
{ for(stage_actor *a=o?o->actors:NULL;a;a=a->next) if(qa_actor_id_equal(a->actor,actor)) return a; return NULL; }
static bool source_current(stage_actor *a, qa_error *e)
{
    application_native_q2_weapon_stage *o=a?a->owner:NULL;
    qa_native_address address;
    if(!(o&&!a->releasing&&find(o,a->actor)==a&&
        application_native_q2_callbacks_storage_current(o->options.callbacks,e)&&
        application_native_q2_callbacks_client_current(o->options.callbacks,a->actor,e)&&
        application_native_q2_callbacks_record(o->options.callbacks,a->actor,o->record,&address,e)&&
        (address==a->dispatcher||fail(e,QA_ERROR_NOT_FOUND,"Native weapon actor changed its actual dispatcher record")))) return false;
    for(size_t i=0;i<a->watch_count;++i) {
        const stage_watch *watch=a->watches+i; qa_native_address base;
        if(!application_native_q2_callbacks_record(o->options.callbacks,a->actor,watch->field->record,&base,e)||
            base>UINT64_MAX-watch->field->offset||base+watch->field->offset!=watch->address)
            return fail(e,QA_ERROR_NOT_FOUND,"Native weapon selection changed its actual source record");
    }
    return true;
}
static bool current(stage_actor *a,qa_error *e)
{ return source_current(a,e)&&a->owner->options.actor_current(a->owner->options.context,a->actor,e); }
static bool word(const qa_json_document *d,qa_json_id object,const char *name,uint32_t *out,qa_error *e)
{
    uint64_t value;
    if(!qa_json_u64(d,qa_json_get(d,object,name),&value,e)||value>UINT32_MAX) return fail(e,QA_ERROR_FORMAT,"Native weapon declaration exceeds its source word");
    *out=(uint32_t)value; return true;
}
static bool text(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer value={0};
    if(!qa_json_string(d,id,&value,e)) return false;
    if(!value.size||memchr(value.data,0,value.size)) { qa_buffer_free(&value); return fail(e,QA_ERROR_FORMAT,"Native weapon record text is invalid"); }
    *out=(char *)value.data; return true;
}
static bool address_validate(application_native_q2_weapon_stage *o,qa_json_id id,bool nullable,qa_error *e)
{
    const qa_json_document *d=document(o);
    if(nullable&&qa_json_type(d,id)==QA_JSON_NULL) return true;
    uint32_t rva; qa_native_address address;
    if(!word(d,id,"rva",&rva,e)||!qa_native_rva(instance(o),rva,1,&address,e)) return false;
    qa_json_id offsets=qa_json_get(d,id,"indirections");
    if(qa_json_type(d,offsets)!=QA_JSON_ARRAY) return fail(e,QA_ERROR_FORMAT,"Native weapon address lacks declared indirections");
    for(size_t i=0;i<qa_json_size(d,offsets);++i) { uint64_t offset; if(!qa_json_u64(d,qa_json_at(d,offsets,i),&offset,e)||offset>UINT32_MAX) return false; }
    return true;
}
static bool client_record(application_native_q2_weapon_stage *o,size_t name)
{
    const qa_json_document *d=document(o);
    qa_json_id records=qa_json_get(d,qa_json_get(d,qa_json_root(d),"clients"),"records");
    for(size_t i=0;i<qa_json_size(d,records);++i) if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_at(d,records,i))==name) return true;
    return false;
}
static bool field_parse(application_native_q2_weapon_stage *o,qa_json_id id,application_native_q2_field *out,qa_error *e)
{
    if(!application_native_q2_field_parse(o->options.callbacks,id,out,e)) return false;
    if(client_record(o,out->record)) return true;
    application_native_q2_field_dispose(out);
    return fail(e,QA_ERROR_FORMAT,"Native weapon field is outside its declared client records");
}
static bool pointer_parse(application_native_q2_weapon_stage *o,qa_json_id id,stage_pointer *out,qa_error *e)
{
    const qa_json_document *d=document(o); stage_pointer value={0};
    if(!((value.record=application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,id,"record")))!=0)||!word(d,id,"offset",&value.offset,e)) { return false; }
    qa_json_id records=qa_json_get(d,qa_json_root(d),"actorRecords"); bool found=false;
    for(size_t i=0;i<qa_json_size(d,records);++i) {
        qa_json_id row=qa_json_at(d,records,i); uint32_t stride;
        if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,row,"id"))!=value.record) continue;
        found=word(d,row,"stride",&stride,e)&&value.offset<=stride&&o->pointer_bytes<=stride-value.offset;
        break;
    }
    if(!found||!client_record(o,value.record)) { return fail(e,QA_ERROR_FORMAT,"Native weapon pointer exceeds its declared client record"); }
    *out=value; return true;
}
static bool pointer_address(stage_actor *a,const stage_pointer *field,qa_native_address *out,qa_error *e)
{
    qa_native_address base;
    if(!field->record||!current(a,e)||!application_native_q2_callbacks_record(a->owner->options.callbacks,a->actor,field->record,&base,e)||
        !base||base>UINT64_MAX-field->offset) return false;
    *out=base+field->offset;
    return qa_native_range_check(instance(a->owner),*out,a->owner->pointer_bytes,QA_NATIVE_MEMORY_READ,e);
}
static bool pointer_read(application_native_q2_weapon_stage *o,qa_native_address at,qa_native_address *out,qa_error *e)
{
    uint8_t bytes[8]; if(!qa_native_read(instance(o),at,bytes,o->pointer_bytes,e)) return false;
    *out=o->pointer_bytes==4?qa_load_u32le(bytes):qa_load_u64le(bytes); return true;
}
static bool bits(double value,uint32_t *out,qa_error *e)
{
    if(!isfinite(value)||value < -0x1p63||value >= 0x1p63) {
        (void)fail(e,QA_ERROR_FORMAT,"Native weapon mask exceeds its integer representation");
        return false;
    }
    *out=(uint32_t)(int64_t)value; return true;
}
static bool test_number(stage_actor *a,const application_native_q2_field *field,double *out,qa_error *e)
{
    qa_native_address address; uint8_t raw[8];
    if(!current(a,e)||!application_native_q2_field_address(a->owner->options.callbacks,a->actor,field,&address,e)||
        !qa_native_read(instance(a->owner),address,raw,application_native_q2_field_size(field->encoding),e)) return false;
    switch(field->encoding) {
    case QA_NATIVE_I8: { int8_t value; memcpy(&value,raw,1); *out=value; break; }
    case QA_NATIVE_U8: *out=raw[0]; break;
    case QA_NATIVE_I16: { uint16_t word_value=qa_load_u16le(raw); int16_t value; memcpy(&value,&word_value,2); *out=value; break; }
    case QA_NATIVE_U16: *out=qa_load_u16le(raw); break;
    case QA_NATIVE_I32: *out=qa_load_i32le(raw); break;
    case QA_NATIVE_U32: *out=qa_load_u32le(raw); break;
    case QA_NATIVE_I64: { uint64_t word_value=qa_load_u64le(raw); int64_t value; memcpy(&value,&word_value,8); *out=(double)value; break; }
    case QA_NATIVE_U64: *out=(double)qa_load_u64le(raw); break;
    case QA_NATIVE_F32: { uint32_t word_value=qa_load_u32le(raw); float value; memcpy(&value,&word_value,4); *out=value; break; }
    case QA_NATIVE_F64: { uint64_t word_value=qa_load_u64le(raw); memcpy(out,&word_value,8); break; }
    default: return false;
    }
    if((field->encoding==QA_NATIVE_I64||field->encoding==QA_NATIVE_U64)&&fabs(*out)>9007199254740991.0)
        return fail(e,QA_ERROR_FORMAT,"Native weapon integer test exceeds its exact Source number");
    return current(a,e);
}
static double stored_bits(qa_native_value_type type,uint32_t value)
{
    if(type==QA_NATIVE_I8) { uint8_t u=(uint8_t)value; int8_t s; memcpy(&s,&u,1); return s; }
    if(type==QA_NATIVE_I16) { uint16_t u=(uint16_t)value; int16_t s; memcpy(&s,&u,2); return s; }
    if(type==QA_NATIVE_I32) { int32_t s; memcpy(&s,&value,4); return s; }
    if(type==QA_NATIVE_U8) return (uint8_t)value;
    if(type==QA_NATIVE_U16) return (uint16_t)value;
    return value;
}
static bool test(stage_actor *a,const stage_test *t,bool *out,qa_error *e)
{
    application_native_q2_weapon_stage *o=a->owner;
    if(t->pointer.record) {
        qa_native_address address,value,expected;
        if(!pointer_address(a,&t->pointer,&address,e)||!pointer_read(o,address,&value,e)||
            !application_native_q2_callbacks_address(o->options.callbacks,t->address,&expected,e)) return false;
        *out=value==expected; return true;
    }
    double value;
    if(!test_number(a,&t->field,&value,e)) return false;
    if(t->masked) { uint32_t u; if(!bits(value,&u,e)) return false; u&=t->mask; int32_t s; memcpy(&s,&u,4); value=s; }
    *out=t->at_most?value<=t->expected:value==t->expected;
    return true;
}
static bool groups(stage_actor *a,const stage_tests *rows,bool *out,qa_error *e)
{
    *out=false;
    size_t start=0;
    for(size_t i=0;i<rows->group_count;++i) {
        bool matches=true;
        for(size_t j=start;matches&&j<rows->ends[i];++j) if(!test(a,rows->tests+j,&matches,e)) return false;
        if(matches) { *out=true; break; }
        start=rows->ends[i];
    }
    return current(a,e);
}
static bool selection(stage_actor *a,const stage_pointer *field,qa_item_id *out,qa_error *e)
{
    if(!field->record) { *out=0; return true; }
    qa_native_address at,value; application_native_q2_weapon_stage *o=a->owner;
    if(!pointer_address(a,field,&at,e)||!pointer_read(o,at,&value,e)) return false;
    if(!value) { *out=0; return true; }
    for(size_t i=0;i<o->choice_count;++i) {
        qa_native_address expected;
        if(!application_native_q2_callbacks_address(o->options.callbacks,o->choices[i].address,&expected,e)) return false;
        if(value==expected) { *out=o->choices[i].item; return current(a,e); }
    }
    return fail(e,QA_ERROR_FORMAT,"Original native selected an undeclared source weapon");
}
bool application_native_q2_weapon_stage_read(application_native_q2_weapon_stage *o,qa_actor_id actor,qa_item_id *active,qa_item_id *pending,qa_error *e)
{
    stage_actor *a=find(o,actor); qa_item_id x,y;
    if(!active||!pending||!current(a,e)||!selection(a,&o->active,&x,e)||!selection(a,&o->pending,&y,e)) return false;
    *active=x; *pending=y; return true;
}
static bool completed(stage_actor *a,qa_error *e)
{
    if(!a->request||a->status!=QA_WEAPON_REQUEST_PENDING) return true;
    qa_item_id active,pending;
    if(!application_native_q2_weapon_stage_read(a->owner,a->actor,&active,&pending,e)) return false;
    if(active==a->requested) a->status=QA_WEAPON_REQUEST_ACCEPTED;
    else if(pending!=a->requested) a->status=QA_WEAPON_REQUEST_REFUSED;
    return true;
}
static bool projection_restore(application_native_q2_weapon_stage *o,stage_projection **head,qa_error *e)
{
    stage_projection *p=*head; if(!p) return true;
    stage_actor *a=find(o,p->actor);
    for(size_t i=p->count;i>0;--i) {
        stage_saved *saved=p->fields+i-1; if(!saved->written) continue;
        qa_native_address address; double value; uint32_t current_bits;
        if(!source_current(a,e)||!application_native_q2_field_address(o->options.callbacks,p->actor,&saved->field->field,&address,e)||
            address!=saved->address||!application_native_q2_field_read(o->options.callbacks,p->actor,&saved->field->field,&value,e)||
            !bits(value,&current_bits,e)||
            !application_native_q2_field_write(o->options.callbacks,p->actor,&saved->field->field,
                stored_bits(saved->field->field.encoding,(current_bits&~saved->field->mask)|(saved->original&saved->field->mask)),e)) return false;
        saved->written=false;
    }
    *head=p->next; free(p->fields); free(p); return true;
}
static bool region_call(void *context,qa_native_instance *native,const qa_native_region_event *event,qa_native_region_decision *decision,qa_error *e)
{
    stage_region *region=context; application_native_q2_weapon_stage *o=region->owner;
    if(native!=instance(o)||event->region.id!=region->id||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    *decision=(qa_native_region_decision){.action=QA_NATIVE_REGION_CONTINUE};
    if(event->phase==QA_NATIVE_REGION_JOIN) {
        if(o->projections&&o->projections->region==region) return projection_restore(o,&o->projections,e);
        return true;
    }
    stage_dispatch *frame=o->dispatch;
    if(!frame) return true;
    frame->reached=true;
    stage_actor *a=frame->actor;
    if(frame->committed||qa_equipment_weapon_selected(o->options.equipment,a->actor,o->options.owner)) return true;
    if(!current(a,e)) return false;
    stage_projection *p=calloc(1,sizeof(*p));
    if(!p) return fail(e,QA_ERROR_MEMORY,"Retaining native weapon decision projection");
    p->fields=region->count?calloc(region->count,sizeof(*p->fields)):NULL;
    if(region->count&&!p->fields) { free(p); return fail(e,QA_ERROR_MEMORY,"Retaining native weapon input masks"); }
    p->actor=a->actor; p->region=region; p->next=o->projections; o->projections=p;
    for(size_t i=0;i<region->count;++i) {
        stage_saved *saved=p->fields+i; double value; saved->field=region->fields+i;
        if(!application_native_q2_field_address(o->options.callbacks,a->actor,&saved->field->field,&saved->address,e)||
            !application_native_q2_field_read(o->options.callbacks,a->actor,&saved->field->field,&value,e)||
            !bits(value,&saved->original,e)) return false;
        ++p->count;
    }
    for(size_t i=0;i<p->count;++i) {
        stage_saved *saved=p->fields+i;
        saved->written=true;
        if(!application_native_q2_field_write(o->options.callbacks,a->actor,&saved->field->field,
            stored_bits(saved->field->field.encoding,saved->original&~saved->field->mask),e)) return false;
    }
    return true;
}
static bool dispatch_cancel(void *context,const qa_error *error)
{
    stage_dispatch *frame=context; qa_error current_error={0};
    return application_native_q2_items_receipt_accepts_error(frame->items,error)&&
        application_native_q2_items_receipt_source_current(frame->items,&current_error)&&
        source_current(frame->actor,&current_error);
}
static bool dispatch_call(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *e)
{
    application_native_q2_weapon_stage *o=context;
    if(native!=instance(o)||binding!=o->dispatcher||count!=o->signature.parameter_count||o->projections)
        return fail(e,QA_ERROR_ARGUMENT,"Native weapon dispatcher retains an unfinished source projection");
    const qa_native_value *argument=arguments+o->argument; stage_actor *a=NULL;
    if(argument->type!=QA_NATIVE_ADDRESS) return fail(e,QA_ERROR_FORMAT,"Native weapon dispatcher received another ABI type");
    for(stage_actor *row=o->actors;argument->as.address&&row;row=row->next) if(row->dispatcher==argument->as.address) {
        if(!current(row,e)) return false;
        a=row; break;
    }
    if(!a) return qa_native_invoke_original(binding,arguments,count,result,e);
    stage_dispatch frame={.previous=o->dispatch,.actor=a,.projection_base=o->projections};
    if(!groups(a,&o->committed,&frame.committed,e)||
        !application_native_q2_items_receipt_begin(o->options.items,a->actor,&frame.items,e)) return false;
    o->dispatch=&frame; ++o->calls;
    bool cancelled=false;
    bool ok=qa_native_invoke_original_cancellable(binding,arguments,count,result,
        dispatch_cancel,&frame,&cancelled,e);
    qa_error cleanup={0};
    while(o->projections!=frame.projection_base) if(!projection_restore(o,&o->projections,&cleanup)) { if(ok&&e) *e=cleanup; ok=false; break; }
    if(ok&&!cancelled&&current(a,&cleanup)) { if(!completed(a,e)) ok=false; }
    o->dispatch=frame.previous; --o->calls;
    application_native_q2_items_receipt_end(&frame.items);
    return ok;
}
static stage_choice *choice(application_native_q2_weapon_stage *o,qa_item_id item)
{ for(size_t i=0;i<o->choice_count;++i) if(o->choices[i].item==item) return o->choices+i; return NULL; }
static bool accepts(stage_actor *a,qa_item_id item,bool *out,qa_error *e)
{
    double count;
    if(!current(a,e)) return false;
    if(!choice(a->owner,item)) { *out=false; return true; }
    if(!qa_inventory_count_read(a->owner->options.inventory,a->actor,item,&count,e)||!current(a,e)) return false;
    *out=count>0; return true;
}
static bool request_run(stage_actor *a,qa_item_id item,qa_error *e)
{
    application_native_q2_weapon_stage *o=a->owner; qa_actor_id actor=a->actor;
    a->request=++o->next_request; a->requested=item; a->status=QA_WEAPON_REQUEST_PENDING;
    qa_item_id active,pending; bool accepted=false;
    if(!item) {
        bool settled;
        if(!selection(a,&o->active,&active,e)||!groups(a,&o->settled,&settled,e)) return false;
        a->status=active&&settled?QA_WEAPON_REQUEST_ACCEPTED:QA_WEAPON_REQUEST_REFUSED;
    } else {
        if(!accepts(a,item,&accepted,e)) return false;
        if(!accepted) a->status=QA_WEAPON_REQUEST_REFUSED;
        else {
            a->requesting=true;
            double result;
            application_native_callback_value values[Q3_MOD_VALUE_COUNT];
            application_native_callback_inputs inputs={0};
            bool ok=o->options.source_inputs(o->options.context,actor,values,&inputs,e)&&
                current(a,e)&&application_native_q2_callbacks_call(o->options.callbacks,choice(o,item)->request,&inputs,&result,e);
            a->requesting=false;
            if(!ok||!current(a,e)||!application_native_q2_weapon_stage_read(o,actor,&active,&pending,e)) return false;
            if(active==item) a->status=QA_WEAPON_REQUEST_ACCEPTED;
            else if(pending!=item) a->status=QA_WEAPON_REQUEST_REFUSED;
        }
    }
    return true;
}
bool application_native_q2_weapon_stage_request(application_native_q2_weapon_stage *o,qa_actor_id actor,
    qa_item_id item,uint64_t *id,qa_weapon_request_status *status,qa_error *e)
{
    stage_actor *a=find(o,actor);
    if(!id||!status||!current(a,e)||a->requesting||o->next_request==UINT64_C(9007199254740991)) return fail(e,QA_ERROR_ARGUMENT,"Native weapon request requires its current source owner");
    ++o->calls; ++a->entered;
    bool ok=request_run(a,item,e);
    --a->entered; --o->calls;
    if(ok) { *id=a->request; *status=a->status; }
    return ok;
}
static bool binding_current(void *context,qa_actor_id actor)
{ stage_actor *a=context; qa_error e={0}; return qa_actor_id_equal(a->actor,actor)&&current(a,&e); }
static bool binding_read(void *context,qa_actor_id actor,qa_weapon_presentation *out,qa_error *e)
{
    stage_actor *a=context; qa_item_id active,pending;
    if(!qa_actor_id_equal(a->actor,actor)||!application_native_q2_weapon_stage_read(a->owner,actor,&active,&pending,e)) return false;
    *out=(qa_weapon_presentation){.provider=a->owner->options.owner,.active=active,.pending=pending,.context=a,.actor=actor}; return true;
}
static bool binding_declares(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{ stage_actor *a=context; if(!qa_actor_id_equal(a->actor,actor)||!current(a,e)) return false; *out=choice(a->owner,item)!=NULL; return true; }
static bool binding_accepts(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{ stage_actor *a=context; return qa_actor_id_equal(a->actor,actor)&&accepts(a,item,out,e); }
static bool binding_resume(void *context,qa_actor_id actor,qa_item_id item,bool *accepted,uint64_t *id,qa_error *e)
{
    stage_actor *a=context; qa_weapon_request_status status;
    if(!qa_actor_id_equal(a->actor,actor)||!application_native_q2_weapon_stage_request(a->owner,actor,item,id,&status,e)) return false;
    *accepted=status!=QA_WEAPON_REQUEST_REFUSED; return true;
}
static bool binding_select(void *context,qa_actor_id actor,qa_item_id item,bool *accepted,qa_error *e)
{ uint64_t id; return binding_resume(context,actor,item,accepted,&id,e); }
static bool binding_holster(void *context,qa_actor_id actor,qa_error *e)
{ stage_actor *a=context; return qa_actor_id_equal(a->actor,actor)&&current(a,e); }
static bool binding_holstered(void *context,qa_actor_id actor,bool *out,qa_error *e)
{ stage_actor *a=context; return qa_actor_id_equal(a->actor,actor)&&current(a,e)&&groups(a,&a->owner->settled,out,e); }
static bool request_current(stage_actor *a,qa_actor_id actor,uint64_t id,qa_item_id item,qa_error *e)
{ return qa_actor_id_equal(a->actor,actor)&&current(a,e)&&(id&&a->request==id&&a->requested==item?true:fail(e,QA_ERROR_FORMAT,"Native weapon request differs from its retained source capability")); }
static bool binding_status(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_weapon_request_status *out,qa_error *e)
{
    (void)e; stage_actor *a=context; qa_error ignored={0};
    *out=qa_actor_id_equal(a->actor,actor)&&current(a,&ignored)&&id&&a->request==id&&a->requested==item?
        a->status:QA_WEAPON_REQUEST_REFUSED;
    return true;
}
static bool binding_cancel(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_error *e)
{
    (void)e; stage_actor *a=context; qa_error ignored={0};
    if(qa_actor_id_equal(a->actor,actor)&&current(a,&ignored)&&id&&a->request==id&&a->requested==item&&a->status==QA_WEAPON_REQUEST_PENDING) {
        a->status=QA_WEAPON_REQUEST_REFUSED; a->request=0; a->requested=0;
    }
    return true;
}
static bool binding_restore(void *context,qa_actor_id actor,uint64_t id,qa_item_id item,qa_error *e)
{ return request_current(context,actor,id,item,e); }
static bool selection_written(void *context,qa_native_instance *native,const qa_native_write_event *event,qa_error *e)
{
    stage_watch *watch=context; stage_actor *a=watch->actor;
    application_native_q2_weapon_stage *o=a->owner; qa_native_address address;
    if(native!=instance(o)||event->address!=watch->address||!pointer_address(a,watch->field,&address,e)||address!=watch->address) return false;
    qa_item_id active,pending;
    if(!application_native_q2_weapon_stage_read(o,a->actor,&active,&pending,e)) return false;
    qa_item_id item=pending?pending:active,previous=a->source_selection;
    a->source_selection=item;
    if(!item||item==previous||a->requesting) return true;
    qa_native_write_scope *scope=NULL;
    if(!qa_native_write_scope_open(native,event,&scope,e)) return false;
    ++a->entered; ++o->calls;
    bool accepted=false;
    bool ok=qa_equipment_weapon_request(o->options.equipment,a->actor,o->options.owner,item,&accepted,e);
    --o->calls; --a->entered;
    qa_error cleanup={0};
    if(!qa_native_write_scope_close(&scope,&cleanup)) { if(ok&&e)*e=cleanup; return false; }
    return ok&&current(a,e);
}
bool application_native_q2_weapon_stage_status(application_native_q2_weapon_stage *o,qa_actor_id actor,
    uint64_t id,qa_item_id item,qa_weapon_request_status *out,qa_error *e)
{
    stage_actor *a=find(o,actor);
    if(!out) return false;
    if(!a) { *out=QA_WEAPON_REQUEST_REFUSED; return true; }
    return binding_status(a,actor,id,item,out,e);
}
bool application_native_q2_weapon_stage_cancel(application_native_q2_weapon_stage *o,qa_actor_id actor,
    uint64_t id,qa_item_id item,qa_error *e)
{
    stage_actor *a=find(o,actor);
    return !a||binding_cancel(a,actor,id,item,e);
}
bool application_native_q2_weapon_stage_admit(application_native_q2_weapon_stage *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    stage_actor *a=find(o,actor);
    if(!a) {
        qa_native_address address;
        if(!o->options.actor_current(o->options.context,actor,e)||!application_native_q2_callbacks_record(o->options.callbacks,actor,o->record,&address,e)) return false;
        a=calloc(1,sizeof(*a)); if(!a) return fail(e,QA_ERROR_MEMORY,"Admitting native source weapon owner");
        a->owner=o; a->actor=actor; a->dispatcher=address; a->next=o->actors; o->actors=a;
    }
    if(!current(a,e)) return false;
    if(!a->watch_count) {
        qa_item_id active,pending;
        if(!application_native_q2_weapon_stage_read(o,actor,&active,&pending,e)) return false;
        a->source_selection=pending?pending:active;
        const stage_pointer *fields[]={&o->active,&o->pending};
        size_t count=0;
        for(size_t i=0;i<2;++i) if(fields[i]->record) {
            stage_watch *watch=a->watches+count;
            watch->actor=a; watch->field=fields[i];
            if(!pointer_address(a,fields[i],&watch->address,e)) return false;
            ++count;
        }
        a->watch_count=count;
    }
    qa_equipment_weapon_binding binding={.provider=o->options.owner,.context=a,.source_input=true,
        .current=binding_current,.read=binding_read,.declares=binding_declares,.accepts=binding_accepts,
        .select=binding_select,.holster=binding_holster,.holstered=binding_holstered,.resume=binding_resume,
        .status=binding_status,.cancel=binding_cancel,.restore_request=binding_restore};
    if(!a->bound) {
        if(!qa_equipment_weapon_bind(o->options.equipment,actor,&binding,e)) return false;
        a->bound=true;
    } else if(!qa_equipment_weapon_binding_is(o->options.equipment,actor,o->options.owner,a)) return false;
    for(size_t i=0;i<a->watch_count;++i) if(!a->watches[i].binding&&
        !qa_native_observe_writes(instance(o),a->watches[i].address,o->pointer_bytes,selection_written,a->watches+i,&a->watches[i].binding,e)) return false;
    return current(a,e);
}
bool application_native_q2_weapon_stage_accepts_attack(application_native_q2_weapon_stage *o,qa_actor_id actor,bool *out,qa_error *e)
{
    if(!out) return false;
    if(!o) { *out=true; return true; }
    stage_actor *a=find(o,actor); if(!current(a,e)) return false;
    if(qa_equipment_weapon_selected(o->options.equipment,actor,o->options.owner)) { *out=true; return true; }
    return groups(a,&o->continuations,out,e);
}
static bool tests_prepare(application_native_q2_weapon_stage *o,qa_json_id rows,stage_tests *out,qa_error *e)
{
    const qa_json_document *d=document(o);
    if(rows==QA_JSON_NONE) return true;
    if(qa_json_type(d,rows)!=QA_JSON_ARRAY) return fail(e,QA_ERROR_FORMAT,"Native weapon test groups require an array");
    out->group_count=qa_json_size(d,rows);
    out->ends=out->group_count?calloc(out->group_count,sizeof(*out->ends)):NULL;
    if(out->group_count&&!out->ends) return fail(e,QA_ERROR_MEMORY,"Owning native weapon test groups");
    size_t count=0;
    for(size_t i=0;i<out->group_count;++i) {
        qa_json_id group=qa_json_at(d,rows,i);
        size_t size=qa_json_size(d,group);
        if(qa_json_type(d,group)!=QA_JSON_ARRAY||!size||size>SIZE_MAX-count)
            return fail(e,QA_ERROR_FORMAT,"Native weapon tests require a nonempty declared group");
        count+=size; out->ends[i]=count;
    }
    out->tests=count?calloc(count,sizeof(*out->tests)):NULL;
    if(count&&!out->tests) return fail(e,QA_ERROR_MEMORY,"Owning native weapon tests");
    for(size_t i=0;i<out->group_count;++i) {
        qa_json_id group=qa_json_at(d,rows,i);
        for(size_t j=0;j<qa_json_size(d,group);++j) {
            qa_json_id t=qa_json_at(d,group,j); stage_test *test_row=out->tests+out->count++;
            if(qa_json_string_equal(d,qa_json_get(d,t,"kind"),"pointer")) {
                test_row->address=qa_json_get(d,t,"value");
                if(!pointer_parse(o,qa_json_get(d,t,"field"),&test_row->pointer,e)||!address_validate(o,test_row->address,true,e)) return false;
            } else if(qa_json_string_equal(d,qa_json_get(d,t,"kind"),"scalar")) {
                double value=0;
                if(!field_parse(o,qa_json_get(d,t,"field"),&test_row->field,e)||!qa_json_number(d,qa_json_get(d,t,"value"),&test_row->expected,e)) return false;
                qa_json_id mask=qa_json_get(d,t,"mask");
                if(qa_json_type(d,mask)!=QA_JSON_NULL&&(!qa_json_number(d,mask,&value,e)||value!=trunc(value)||value<INT32_MIN||value>UINT32_MAX)) return fail(e,QA_ERROR_FORMAT,"Native weapon test mask exceeds its bitwise word");
                test_row->masked=qa_json_type(d,mask)!=QA_JSON_NULL;
                if(test_row->masked) test_row->mask=(uint32_t)(int64_t)value;
                if(!qa_json_string_equal(d,qa_json_get(d,t,"comparison"),"equals")&&!qa_json_string_equal(d,qa_json_get(d,t,"comparison"),"at-most")) return fail(e,QA_ERROR_FORMAT,"Native weapon test comparison is invalid");
                test_row->at_most=qa_json_string_equal(d,qa_json_get(d,t,"comparison"),"at-most");
            } else return fail(e,QA_ERROR_FORMAT,"Native weapon test kind is invalid");
        }
    }
    return true;
}
static bool stage_prepare(application_native_q2_weapon_stage *o,qa_error *e)
{
    const application_native_q2_weapon_stage_options *options=&o->options;
    qa_json_id definition=o->definition;
    const qa_json_document *d=document(o); qa_json_id dispatcher=qa_json_get(d,definition,"dispatcher"),selection_id=qa_json_get(d,definition,"selection");
    qa_native_target target=qa_native_module_describe(qa_native_get_module(instance(o))).image.target;
    o->pointer_bytes=target.pointer_bytes; uint32_t count,argument;
    if(!((o->record=application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,dispatcher,"record")))!=0)||!word(d,dispatcher,"arguments",&count,e)||!count||count>16||
        !word(d,dispatcher,"argument",&argument,e)||argument>=count||
        !application_native_q2_callbacks_entry(options->callbacks,qa_json_get(d,dispatcher,"entry"),&o->entry,e)) return false;
    o->argument=argument; o->parameters=calloc(count,sizeof(*o->parameters));
    if(!o->parameters) return fail(e,QA_ERROR_MEMORY,"Owning native dispatcher ABI");
    for(size_t i=0;i<count;++i) o->parameters[i]=(qa_native_type){.kind=QA_NATIVE_ADDRESS,.count=1};
    qa_json_id records=qa_json_get(d,qa_json_root(d),"actorRecords"); bool dispatcher_record=false;
    for(size_t i=0;i<qa_json_size(d,records);++i) if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,qa_json_at(d,records,i),"id"))==o->record) dispatcher_record=true;
    if(!dispatcher_record) return fail(e,QA_ERROR_FORMAT,"Native weapon dispatcher names no actual Source record");
    o->signature=(qa_native_signature){.abi=target.abi,.parameters=o->parameters,.parameter_count=count,.result={.kind=QA_NATIVE_VOID,.count=1}};
    if(!pointer_parse(o,qa_json_get(d,selection_id,"active"),&o->active,e)) return false;
    qa_json_id pending=qa_json_get(d,selection_id,"pending");
    if(qa_json_type(d,pending)!=QA_JSON_NULL&&!pointer_parse(o,pending,&o->pending,e)) return false;
    qa_json_id values=qa_json_get(d,selection_id,"values");
    if(qa_json_type(d,values)!=QA_JSON_ARRAY||!qa_json_size(d,values)) return fail(e,QA_ERROR_FORMAT,"Native weapons require declared selection values");
    o->choice_count=qa_json_size(d,values); o->choices=calloc(o->choice_count,sizeof(*o->choices));
    if(!o->choices) return fail(e,QA_ERROR_MEMORY,"Owning native weapon selections");
    const qa_item_admission *admissions; size_t admission_count;
    if(!application_native_q2_items_definitions(options->items,&admissions,&admission_count,e)) return false;
    size_t weapon_count=0;
    for(size_t i=0;i<admission_count;++i) if(admissions[i].definition.weapon) ++weapon_count;
    if(weapon_count!=o->choice_count) return fail(e,QA_ERROR_FORMAT,"Native weapon selection differs from its admitted weapon definitions");
    for(size_t i=0;i<o->choice_count;++i) {
        qa_json_id row=qa_json_at(d,values,i); char *name=NULL;
        if(!text(d,qa_json_get(d,row,"item"),&name,e)) return false;
        bool ok=qa_strings_intern_cstr(qa_session_strings(options->session),name,&o->choices[i].item,e); free(name); if(!ok) return false;
        bool declared=false; for(size_t j=0;j<admission_count;++j) if(admissions[j].definition.item==o->choices[i].item&&admissions[j].definition.weapon) declared=true;
        if(!declared) return fail(e,QA_ERROR_FORMAT,"Native weapon selection has no source weapon admission");
        for(size_t j=0;j<i;++j) if(o->choices[j].item==o->choices[i].item) return fail(e,QA_ERROR_FORMAT,"Native weapon selection repeats its item");
        o->choices[i].address=qa_json_get(d,row,"address"); o->choices[i].request=qa_json_get(d,row,"request");
        if(!address_validate(o,o->choices[i].address,false,e)||qa_json_type(d,o->choices[i].request)!=QA_JSON_OBJECT) return false;
    }
    qa_json_id continuations=qa_json_get(d,definition,"continuations"),settled=qa_json_get(d,definition,"settled");
    if(!qa_json_size(d,continuations)||!qa_json_size(d,settled)||
        !tests_prepare(o,qa_json_get(d,definition,"committedInput"),&o->committed,e)||
        !tests_prepare(o,continuations,&o->continuations,e)||!tests_prepare(o,settled,&o->settled,e)) return false;
    qa_json_id regions=qa_json_get(d,definition,"decisions");
    if(qa_json_type(d,regions)!=QA_JSON_ARRAY||!qa_json_size(d,regions)) return fail(e,QA_ERROR_FORMAT,"Native weapon decisions require their exact region roster");
    o->region_count=qa_json_size(d,regions); o->regions=o->region_count?calloc(o->region_count,sizeof(*o->regions)):NULL;
    if(o->region_count&&!o->regions) return fail(e,QA_ERROR_MEMORY,"Owning native weapon regions");
    for(size_t i=0;i<o->region_count;++i) {
        stage_region *r=o->regions+i; qa_json_id row=qa_json_at(d,regions,i); uint32_t entry,join;
        if(!word(d,row,"entry",&entry,e)||!word(d,row,"join",&join,e)||entry>=join) return false;
        bool found=false; qa_native_declared_region actual;
        for(size_t j=0;j<qa_native_region_count(instance(o));++j) {
            if(!qa_native_region(instance(o),j,&actual,e)) return false;
            if(actual.entry_rva==entry&&actual.join_rva==join) { r->id=actual.id; found=true; break; }
        }
        if(!found) return fail(e,QA_ERROR_FORMAT,"Native weapon decision has no admitted original instruction region");
        for(size_t j=0;j<i;++j) if(o->regions[j].id==r->id) return fail(e,QA_ERROR_FORMAT,"Native weapon decision repeats its original region");
        r->owner=o; qa_json_id fields=qa_json_get(d,row,"fields");
        if(qa_json_type(d,fields)!=QA_JSON_ARRAY||!qa_json_size(d,fields)) return fail(e,QA_ERROR_FORMAT,"Native weapon decision omitted its source fields");
        r->count=qa_json_size(d,fields); r->fields=r->count?calloc(r->count,sizeof(*r->fields)):NULL;
        if(r->count&&!r->fields) return fail(e,QA_ERROR_MEMORY,"Owning native weapon decision fields");
        for(size_t j=0;j<r->count;++j) {
            qa_json_id field=qa_json_at(d,fields,j); stage_mask *m=r->fields+j;
            if(!field_parse(o,qa_json_get(d,field,"field"),&m->field,e)||!word(d,field,"clearMask",&m->mask,e)) return false;
            size_t width=application_native_q2_field_size(m->field.encoding);
            if(width>4||m->field.encoding==QA_NATIVE_F32||!m->mask||m->mask>INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Native weapon input mask exceeds its declared integer domain");
        }
    }
    return true;
}
bool application_native_q2_weapon_stage_create(const application_native_q2_weapon_stage_options *options,application_native_q2_weapon_stage **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->items||!options->session||!options->inventory||!options->equipment||!options->owner||!options->actor_current||!options->source_inputs)
        return fail(e,QA_ERROR_ARGUMENT,"Native weapon stage requires its actual callback, item and broker owners");
    qa_json_id definition=application_native_q2_items_weapon_stage(options->items);
    if(definition==QA_JSON_NONE) return true;
    application_native_q2_weapon_stage *o=calloc(1,sizeof(*o));
    if(!o) return fail(e,QA_ERROR_MEMORY,"Owning native declared weapon stage");
    o->options=*options; o->definition=definition;
    if(!stage_prepare(o,e)) {
        qa_error cleanup={0};
        if(!application_native_q2_weapon_stage_destroy(&o,&cleanup)) *out=o;
        return false;
    }
    *out=o;
    return true;
}
bool application_native_q2_weapon_stage_activate(application_native_q2_weapon_stage *o,qa_error *e)
{
    if(!o) return true;
    if(!application_native_q2_weapon_stage_idle(o)||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    if(!o->dispatcher&&!qa_native_observe_entry(instance(o),o->entry,&o->signature,dispatch_call,o,&o->dispatcher,e)) return false;
    for(size_t i=0;i<o->region_count;++i) if(!o->regions[i].binding&&!qa_native_bind_region(instance(o),o->regions[i].id,region_call,o->regions+i,&o->regions[i].binding,e)) return false;
    for(stage_actor *a=o->actors;a;a=a->next) if(!application_native_q2_weapon_stage_admit(o,a->actor,e)) return false;
    return true;
}
bool application_native_q2_weapon_stage_idle(const application_native_q2_weapon_stage *o)
{ return !o||(!o->calls&&!o->dispatch&&!o->projections); }
bool application_native_q2_weapon_stage_release(application_native_q2_weapon_stage *o,qa_actor_id actor,qa_error *e)
{
    stage_actor *a=find(o,actor); if(!a) return true;
    if(!application_native_q2_weapon_stage_idle(o)||a->requesting||a->entered) return fail(e,QA_ERROR_ARGUMENT,"Native weapon release retains an entered source decision");
    for(size_t i=a->watch_count;i>0;--i) if(a->watches[i-1].binding) {
        if(!qa_native_unobserve_writes(a->watches[i-1].binding,e)) return false;
        a->watches[i-1].binding=NULL;
    }
    if(a->bound&&!qa_equipment_weapon_unbind(o->options.equipment,actor,o->options.owner,a,e)) return false;
    a->bound=false; a->releasing=true;
    stage_actor **link=&o->actors; while(*link!=a) link=&(*link)->next; *link=a->next; free(a); return true;
}
bool application_native_q2_weapon_stage_suspend(application_native_q2_weapon_stage *o,qa_error *e)
{
    if(!o) return true;
    if(o->calls||o->dispatch) return fail(e,QA_ERROR_ARGUMENT,"Native weapon suspend retains an entered source dispatcher");
    while(o->projections) if(!projection_restore(o,&o->projections,e)) return false;
    for(stage_actor *a=o->actors;a;a=a->next) for(size_t i=a->watch_count;i>0;--i) if(a->watches[i-1].binding) {
        if(!qa_native_unobserve_writes(a->watches[i-1].binding,e)) return false;
        a->watches[i-1].binding=NULL;
    }
    for(size_t i=o->regions?o->region_count:0;i>0;--i) if(o->regions[i-1].binding) {
        if(!qa_native_remove_region(o->regions[i-1].binding,e)) return false;
        o->regions[i-1].binding=NULL;
    }
    if(o->dispatcher) { if(!qa_native_unobserve_entry(o->dispatcher,e)) return false; o->dispatcher=NULL; }
    return true;
}
bool application_native_q2_weapon_stage_destroy(application_native_q2_weapon_stage **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_weapon_stage *o=*owner;
    if(!application_native_q2_weapon_stage_suspend(o,e)) return false;
    while(o->actors) if(!application_native_q2_weapon_stage_release(o,o->actors->actor,e)) return false;
    for(size_t i=0;o->regions&&i<o->region_count;++i) {
        for(size_t j=0;o->regions[i].fields&&j<o->regions[i].count;++j) application_native_q2_field_dispose(&o->regions[i].fields[j].field);
        free(o->regions[i].fields);
    }
    stage_tests *groups_list[]={&o->committed,&o->continuations,&o->settled};
    for(size_t i=0;i<sizeof(groups_list)/sizeof(*groups_list);++i) {
        stage_tests *rows=groups_list[i];
        for(size_t j=0;j<rows->count;++j) {

            application_native_q2_field_dispose(&rows->tests[j].field);
        }
        free(rows->tests); free(rows->ends);
    }
    free(o->regions); free(o->choices); free(o->parameters); free(o); *owner=NULL; return true;
}
typedef struct stage_request_row { qa_actor_id actor; uint64_t id; qa_item_id item; qa_weapon_request_status status; } stage_request_row;
static bool row_fields(qa_source_save_io *io,stage_request_row *row)
{
    uint32_t status=(uint32_t)row->status;
    bool ok=qa_source_save_actor(io,&row->actor)&&qa_source_save_u64(io,&row->id)&&qa_source_save_string(io,&row->item)&&qa_source_save_u32(io,&status);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) row->status=(qa_weapon_request_status)status;
    return ok&&status<=QA_WEAPON_REQUEST_REFUSED;
}
bool application_native_q2_weapon_stage_capture(application_native_q2_weapon_stage *o,qa_buffer *out,qa_error *e)
{
    if(!o||!out||!application_native_q2_weapon_stage_idle(o)) return fail(e,QA_ERROR_ARGUMENT,"Native weapon capture requires its returned source owner");
    qa_source_save_io io={0}; uint8_t tag[6]={'Q','A','N','2','W','S'}; size_t count=0;
    qa_actor_owner owner=o->options.owner;
    for(stage_actor *a=o->actors;a;a=a->next) if(a->request) ++count;
    bool ok=qa_source_save_writer(&io,o->options.session,e)&&qa_source_save_bytes(&io,tag,sizeof(tag))&&
        qa_source_save_string(&io,&owner)&&
        qa_source_save_u64(&io,&o->next_request)&&qa_source_save_count(&io,&count,256);
    for(stage_actor *a=o->actors;ok&&a;a=a->next) if(a->request) {
        stage_request_row row={a->actor,a->request,a->requested,a->status}; ok=current(a,e)&&row_fields(&io,&row);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_native_q2_weapon_stage_restore(application_native_q2_weapon_stage *o,qa_bytes bytes,qa_error *e)
{
    if(!o||!application_native_q2_weapon_stage_idle(o)||o->next_request) return fail(e,QA_ERROR_ARGUMENT,"Native weapon restore requires an isolated empty request owner");
    qa_source_save_io io={0}; uint8_t tag[6]={0}; const uint8_t expected[6]={'Q','A','N','2','W','S'}; uint64_t next=0; size_t count=0;
    qa_actor_owner owner=0;
    bool ok=qa_source_save_reader(&io,o->options.session,bytes,e)&&qa_source_save_bytes(&io,tag,sizeof(tag))&&!memcmp(tag,expected,sizeof(tag))&&
        qa_source_save_string(&io,&owner)&&owner==o->options.owner&&
        qa_source_save_u64(&io,&next)&&next<=UINT64_C(9007199254740991)&&qa_source_save_count(&io,&count,256);
    stage_request_row *rows=ok&&count?calloc(count,sizeof(*rows)):NULL;
    if(ok&&count&&!rows) ok=fail(e,QA_ERROR_MEMORY,"Decoding native weapon request continuation");
    for(size_t i=0;ok&&i<count;++i) {
        ok=row_fields(&io,rows+i)&&rows[i].id&&rows[i].id<=next&&current(find(o,rows[i].actor),e)&&(!rows[i].item||choice(o,rows[i].item));
        for(size_t j=0;ok&&j<i;++j) if(qa_actor_id_equal(rows[i].actor,rows[j].actor)||rows[i].id==rows[j].id) ok=fail(e,QA_ERROR_FORMAT,"Native weapon continuation repeats its source capability");
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    if(ok) { o->next_request=next; for(size_t i=0;i<count;++i) { stage_actor *a=find(o,rows[i].actor); a->request=rows[i].id; a->requested=rows[i].item; a->status=rows[i].status; } }
    free(rows); qa_source_save_dispose(&io); return ok;
}
