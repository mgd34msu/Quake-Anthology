#include "guest_native_q2_private.h"
#include "native_q2_client_stages.h"
#include "qa/native_observe.h"
#include "qa/network.h"
#include <math.h>

typedef struct native_input_store {
    struct native_input_store *next;
    qa_native_address address;
    uint8_t previous[12];
    size_t bytes;
} native_input_store;
typedef struct native_input_output native_input_output;
struct application_native_q2_stages {
    struct application_native_q2 *engine;
    qa_native_entry_observer *allocate,*release;
    struct application_native_q2_input *inputs;
    uint64_t frame,next_ns,interval_ns;
    uint32_t cursor;
    bool advancing,failed;
    uint32_t *pending;
    size_t pending_count,pending_capacity;
};
struct application_native_q2_input {
    struct application_native_q2_input *outer;
    struct application_native_q2_stages *owner;
    qa_actor_id actor;
    bool slice,completed,failed;
    native_input_store *stores;
    native_input_output *outputs;
    bool (*values)(void *,application_q3_mod_inputs *,qa_error *);
    bool (*output)(void *,const application_q3_mod_output *,qa_error *);
    void *context;
};
struct native_input_output {
    struct native_input_output *next;
    struct application_native_q2_input *input;
    qa_json_id declaration;
    qa_native_entry_observer *handler;
    qa_native_address address,self;
    size_t self_index,bytes;
    uint8_t before[12];
    bool called;
};
static qa_native_instance *instance(struct application_native_q2 *n)
{ return qa_native_host_instance(n->provider->state.native.host); }
static bool current(struct application_native_q2_stages *o,qa_error *e)
{
    struct application_native_q2 *n=o?o->engine:NULL;
    return n&&n->stages==o&&n->provider->state.native.q2_engine==n&&n->callbacks&&
        !qa_native_terminal(instance(n)) ? true:
        application_fail(e,QA_ERROR_ARGUMENT,"Declared native client stages lost their physical source");
}
static qa_json_id source(struct application_native_q2 *n)
{
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    return qa_json_get(d,qa_json_root(d),"sourceActors");
}
static bool intercepted(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *e)
{
    struct application_native_q2_stages *o=context;
    if(!current(o,e)||native!=instance(o->engine)) return false;
    ++o->engine->calls;
    bool ok=qa_native_invoke_original(binding,arguments,count,result,e);
    if(ok&&binding==o->allocate&&result&&result->type==QA_NATIVE_ADDRESS&&result->as.address) {
        qa_actor_id actor;uint32_t slot;qa_native_slot_binding old;
        ok=qa_native_entity_slot(native,result->as.address,&slot,e)&&qa_native_slot(native,slot,&old,e);
        if(ok&&old.kind==QA_NATIVE_SLOT_OWNED) ok=qa_native_host_detach_actor(o->engine->provider->state.native.host,slot,old.actor,e);
        if(ok) ok=qa_native_host_source_actor(o->engine->provider->state.native.host,result->as.address,true,&actor,e);
    }
    if(ok&&binding==o->release) ok=qa_native_host_source_reconcile(o->engine->provider->state.native.host,e);
    --o->engine->calls;
    return ok;
}
bool application_native_q2_stages_prepare(struct application_native_q2 *n,qa_error *e)
{
    if(!n||!n->callbacks) return true;
    if(!n->stages) {
        n->stages=calloc(1,sizeof(*n->stages));
        if(!n->stages) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native client stages");
        n->stages->engine=n;
    }
    struct application_native_q2_stages *o=n->stages;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id definition=source(n);
    if(definition==QA_JSON_NONE) return true;
    double seconds;
    if(!qa_json_number(d,qa_json_get(d,definition,"frameSeconds"),&seconds,e)||
        !isfinite(seconds)||seconds<=0||seconds>UINT64_MAX/1e9)
        return application_fail(e,QA_ERROR_FORMAT,"Declared native source frame interval is invalid");
    uint64_t interval=(uint64_t)floor(seconds*1e9+0.5);
    if(!interval) return application_fail(e,QA_ERROR_FORMAT,"Declared native frame interval is below clock resolution");
    if(!o->interval_ns) {
        if(n->frame.time_ns>UINT64_MAX-interval) return application_fail(e,QA_ERROR_FORMAT,"Declared native next frame overflows");
        o->interval_ns=interval; o->next_ns=n->frame.time_ns+interval;
    } else if(o->interval_ns!=interval) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native source clock changed");
    qa_native_module_info info=qa_native_module_describe(n->provider->state.native.module);
    qa_native_type pointer={.kind=QA_NATIVE_ADDRESS,.count=1};
    qa_native_signature allocate={.abi=info.image.target.abi,.result=pointer};
    qa_native_signature release={.abi=info.image.target.abi,.parameters=&pointer,.parameter_count=1,
        .result={.kind=QA_NATIVE_VOID,.count=1}};
    qa_native_address address;
    if(!o->allocate&&(!application_native_q2_callbacks_entry(n->callbacks,qa_json_get(d,definition,"allocate"),&address,e)||
        !qa_native_observe_entry(instance(n),address,&allocate,intercepted,o,&o->allocate,e))) return false;
    if(!o->release&&(!application_native_q2_callbacks_entry(n->callbacks,qa_json_get(d,definition,"release"),&address,e)||
        !qa_native_observe_entry(instance(n),address,&release,intercepted,o,&o->release,e))) return false;
    return true;
}
static bool clock_write(struct application_native_q2_stages *o,qa_error *e)
{
    struct application_native_q2 *n=o->engine;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id fields=qa_json_get(d,source(n),"clock");
    for(size_t i=0;i<qa_json_size(d,fields);++i) {
        qa_json_id field=qa_json_at(d,fields,i); qa_native_address address;
        bool frame=qa_json_string_equal(d,qa_json_get(d,field,"input"),"frame");
        double value=frame?(double)o->frame:(double)n->frame.time_ns/1e9;
        if(!frame&&qa_json_string_equal(d,qa_json_get(d,field,"units"),"milliseconds")) value*=1000;
        qa_json_id encoding=qa_json_get(d,field,"encoding"); uint8_t bytes[8];size_t count=0;
        if(qa_json_string_equal(d,encoding,"float32")) { float f=(float)value;uint32_t bits;memcpy(&bits,&f,4);qa_store_u32le(bytes,bits);count=4; }
        else if(qa_json_string_equal(d,encoding,"float64")) { uint64_t bits;memcpy(&bits,&value,8);qa_store_u64le(bytes,bits);count=8; }
        else {
            if(!frame) value=round(value);
            if(!isfinite(value)||value<0||value>9007199254740991.0||value!=trunc(value))
                return application_fail(e,QA_ERROR_FORMAT,"Declared native source clock exceeds its integer field");
            uint64_t integer=(uint64_t)value;
            const char *names[]={"uint8","int8","uint16","int16","uint32","int32","uint64","int64"};
            for(size_t j=0;j<8;++j) if(qa_json_string_equal(d,encoding,names[j])) {
                count=(size_t)1<<(j/2);uint64_t maximum=j%2?(count==8?INT64_MAX:((UINT64_C(1)<<(count*8-1))-1)):
                    (count==8?UINT64_MAX:((UINT64_C(1)<<(count*8))-1));
                if(integer>maximum) return application_fail(e,QA_ERROR_FORMAT,"Declared native clock exceeds its original encoding");
            }
            qa_store_u64le(bytes,integer);
        }
        if(!count||!application_native_q2_callbacks_address(n->callbacks,qa_json_get(d,field,"address"),&address,e)||
            !qa_native_write(instance(n),address,(qa_bytes){bytes,count},e)) return false;
    }
    return true;
}
static bool client_frame(struct application_native_q2 *n,uint32_t slot,const char *section,qa_error *e)
{
    application_native_q2_client *client=n->clients+slot;
    if(!client->connected||!client->begun||!qa_actors_get(qa_session_actors(n->provider->application->session),client->actor)) return true;
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=client->actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)n->frame.time_ns/1e9}};
    application_native_callback_inputs inputs={values,2,{0}};bool accepted;
    return application_native_q2_callbacks_run(n,section,&inputs,&accepted,e);
}
bool application_native_q2_stages_advance(struct application_native_q2 *n,const qa_source_frame *frame,qa_error *e)
{
    if(!n||!n->callbacks) return true;
    if(!application_native_q2_stages_prepare(n,e)) return false;
    struct application_native_q2_stages *o=n->stages;
    if(!current(o,e)||o->advancing||o->failed||o->inputs) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native source retains an unfinished stage");
    if(!o->interval_ns) return true;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id definition=source(n),update=qa_json_get(d,definition,"update");
    qa_source_frame outer=n->frame;bool ok=true;o->advancing=true;
    while(ok&&o->next_ns<=frame->time_ns) {
        if(o->frame==UINT64_MAX||o->next_ns>UINT64_MAX-o->interval_ns) { ok=application_fail(e,QA_ERROR_FORMAT,"Declared native clock is exhausted");break; }
        ++o->frame;n->frame.number=o->frame;n->frame.time_ns=o->next_ns;
        n->frame.start_ns=o->next_ns-o->interval_ns;n->frame.elapsed_ns=o->interval_ns;o->next_ns+=o->interval_ns;
        ok=clock_write(o,e)&&qa_native_host_source_reconcile(n->provider->state.native.host,e);
        while(ok&&o->pending_count) {
            uint32_t slot=o->pending[0];qa_native_address address;qa_actor_id actor;
            ok=qa_native_entity_address(instance(n),slot,&address,e)&&
                qa_native_host_source_actor(n->provider->state.native.host,address,false,&actor,e);
            if(ok&&actor.registry) {
                qa_native_value argument={.type=QA_NATIVE_ADDRESS,.as.address=address};bool entered;
                ok=application_native_q2_callbacks_entry_call(n->callbacks,qa_json_get(d,definition,"release"),QA_JSON_NONE,&argument,1,&entered,e)&&
                    qa_native_host_source_reconcile(n->provider->state.native.host,e);
                if(ok) {qa_native_slot_binding binding;ok=qa_native_slot(instance(n),slot,&binding,e)&&binding.kind==QA_NATIVE_SLOT_FREE;
                    if(!ok) application_fail(e,QA_ERROR_ARGUMENT,"Declared native source refused a released actor");}
            }
            if(ok) {--o->pending_count;memmove(o->pending,o->pending+1,o->pending_count*sizeof(*o->pending));}
        }
        qa_native_entity_table table={0};
        if(ok) ok=qa_native_entity_table_get(instance(n),&table,e);
        for(o->cursor=0;ok&&o->cursor<table.count;++o->cursor) {
            if(o->cursor&&o->cursor<257&&n->clients[o->cursor].reserved&&n->clients[o->cursor].actor.registry) {
                ok=client_frame(n,o->cursor,"clients.frame",e);continue;
            }
            qa_native_slot_binding binding;qa_native_address address;
            ok=qa_native_slot(instance(n),o->cursor,&binding,e);
            if(!ok||binding.kind!=QA_NATIVE_SLOT_OWNED) continue;
            ok=qa_native_entity_address(instance(n),o->cursor,&address,e);
            qa_native_value argument={.type=QA_NATIVE_ADDRESS,.as.address=address};bool entered=false;
            if(ok) ok=application_native_q2_callbacks_entry_call(n->callbacks,qa_json_get(d,update,"entry"),
                qa_json_get(d,update,"returns"),&argument,1,&entered,e);
        }
        for(uint32_t slot=1;ok&&slot<257;++slot) ok=client_frame(n,slot,"clients.endFrame",e);
        if(ok) ok=qa_native_host_source_reconcile(n->provider->state.native.host,e);
    }
    n->frame=outer;o->advancing=false;o->failed=!ok;return ok;
}
bool application_native_q2_stages_idle(const struct application_native_q2_stages *o)
{ return !o||(!o->advancing&&!o->inputs); }
uint64_t application_native_q2_stages_frame(const struct application_native_q2 *n)
{ return n&&n->stages&&n->stages->interval_ns?n->stages->frame:n?n->frame.number:0; }
bool application_native_q2_stages_close(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_stages *o=n?n->stages:NULL;
    if(!o) return true;
    if(!application_native_q2_stages_idle(o)) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native stages retain an input or source callback");
    if(o->release&&!qa_native_unobserve_entry(o->release,e)) return false;
    o->release=NULL;
    if(o->allocate&&!qa_native_unobserve_entry(o->allocate,e)) return false;
    o->allocate=NULL;free(o->pending);free(o);n->stages=NULL;return true;
}
void application_native_q2_stages_released(struct application_native_q2 *n,qa_actor_id actor)
{
    struct application_native_q2_stages *o=n?n->stages:NULL;
    if(!o||!o->interval_ns||!n->provider->state.native.host) return;
    qa_error error={0};qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(n),&table,&error)) {application_fault(n->provider->application,&error);return;}
    for(uint32_t slot=1;slot<table.count;++slot) {
        qa_native_slot_binding binding;
        if(!qa_native_slot(instance(n),slot,&binding,&error)) {application_fault(n->provider->application,&error);return;}
        if(binding.kind!=QA_NATIVE_SLOT_OWNED||!qa_actor_id_equal(binding.actor,actor)) continue;
        for(size_t i=0;i<o->pending_count;++i) if(o->pending[i]==slot) return;
        if(o->pending_count==o->pending_capacity) {
            size_t capacity=o->pending_capacity?o->pending_capacity*2:16;
            uint32_t *pending=realloc(o->pending,capacity*sizeof(*pending));
            if(!pending) {application_fail(&error,QA_ERROR_MEMORY,"Retaining released native source actor");application_fault(n->provider->application,&error);return;}
            o->pending=pending;o->pending_capacity=capacity;
        }
        o->pending[o->pending_count++]=slot;return;
    }
}
bool application_native_q2_stages_capture(struct application_native_q2 *n,qa_buffer *out,qa_error *e)
{
    if(!out) return false;*out=(qa_buffer){0};
    if(!n||!n->callbacks) return true;
    struct application_native_q2_stages *o=n->stages;
    if(!o||!application_native_q2_stages_idle(o)||o->failed)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native stages capture requires a completed source boundary");
    for(uint32_t i=1;i<257;++i) if(n->clients[i].denied)
        return application_fail(e,QA_ERROR_ARGUMENT,"Rejected declared native client must disconnect before capture");
    if(o->pending_count>UINT32_MAX) return false;
    size_t bytes=36+o->pending_count*4;uint8_t *data=malloc(bytes);
    if(!data) return application_fail(e,QA_ERROR_MEMORY,"Retaining native stages continuation");
    qa_store_u32le(data,UINT32_C(0x3153434e));qa_store_u64le(data+4,o->frame);
    qa_store_u64le(data+12,o->next_ns);qa_store_u64le(data+20,o->interval_ns);qa_store_u32le(data+28,o->cursor);
    qa_store_u32le(data+32,(uint32_t)o->pending_count);
    for(size_t i=0;i<o->pending_count;++i) qa_store_u32le(data+36+i*4,o->pending[i]);
    *out=(qa_buffer){data,bytes};return true;
}
bool application_native_q2_stages_restore(struct application_native_q2 *n,qa_bytes bytes,qa_error *e)
{
    if(!n||!n->callbacks) return !bytes.size||application_fail(e,QA_ERROR_FORMAT,"Saved native stages require their declaration");
    if(bytes.size<36||qa_load_u32le(bytes.data)!=UINT32_C(0x3153434e)) return application_fail(e,QA_ERROR_FORMAT,"Invalid declared native stage continuation");
    uint32_t count=qa_load_u32le(bytes.data+32);
    if((uint64_t)count*4!=bytes.size-36) return application_fail(e,QA_ERROR_FORMAT,"Invalid native pending release extent");
    struct application_native_q2_stages *o=calloc(1,sizeof(*o));
    if(!o) return application_fail(e,QA_ERROR_MEMORY,"Restoring native client stages");
    o->engine=n;o->frame=qa_load_u64le(bytes.data+4);o->next_ns=qa_load_u64le(bytes.data+12);
    o->interval_ns=qa_load_u64le(bytes.data+20);o->cursor=qa_load_u32le(bytes.data+28);
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);qa_json_id definition=source(n);
    double seconds=0;bool has_clock=definition!=QA_JSON_NONE;
    bool ok=(!has_clock||qa_json_number(d,qa_json_get(d,definition,"frameSeconds"),&seconds,e))&&
        (has_clock?(isfinite(seconds)&&seconds>0&&seconds<UINT64_MAX/1e9&&o->interval_ns==(uint64_t)floor(seconds*1e9+0.5)&&o->interval_ns&&o->next_ns>=o->interval_ns):(!o->interval_ns&&!o->frame&&!o->next_ns&&!count));
    if(ok&&count) {o->pending=malloc((size_t)count*sizeof(*o->pending));ok=o->pending!=NULL;o->pending_count=o->pending_capacity=count;}
    for(size_t i=0;ok&&i<count;++i) {o->pending[i]=qa_load_u32le(bytes.data+36+i*4);ok=o->pending[i]!=0;
        for(size_t j=0;ok&&j<i;++j) ok=o->pending[i]!=o->pending[j];}
    if(ok) ok=application_native_q2_stages_close(n,e);
    if(!ok) {free(o->pending);free(o);return application_fail(e,QA_ERROR_FORMAT,"Saved native source cadence differs from its declaration");}
    n->stages=o;return true;
}
static const char *const input_names[]={"view-angles","attack","jump","impulse","forward-move","side-move","up-move",
    "self","other","activator","attacker","inflictor","amount","damage-flags","regular-protection-scale",
    "knockback","point","direction","normal","item","time","elapsed","result","pickup-count","pickup-has-count","pickup-dropped"};
static bool input_current(struct application_native_q2_input *s,qa_error *e)
{
    if(!s||!current(s->owner,e)) return false;
    struct application_native_q2 *n=s->owner->engine;
    if(!qa_actors_get(qa_session_actors(n->provider->application->session),s->actor))
        return application_fail(e,QA_ERROR_NOT_FOUND,"Native input actor has retired");
    for(uint32_t i=1;i<257;++i) if(n->clients[i].connected&&n->clients[i].begun&&qa_actor_id_equal(n->clients[i].actor,s->actor)) return true;
    return application_fail(e,QA_ERROR_ARGUMENT,"Native input lost its admitted physical client");
}
static bool input_values(struct application_native_q2_input *s,application_native_callback_value values[Q3_MOD_VALUE_COUNT],
    application_native_callback_inputs *out,qa_error *e)
{
    application_q3_mod_inputs inputs={0};
    if(!input_current(s,e)||!s->values(s->context,&inputs,e)||!input_current(s,e)) return false;
    size_t count=0;
    for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i) {
        application_q3_mod_value value=inputs.values[i];if(value.kind==Q3_MOD_VALUE_ABSENT) continue;
        application_native_callback_value *v=values+count++;v->name=input_names[i];
        switch(value.kind) {
        case Q3_MOD_VALUE_SCALAR:v->kind=APPLICATION_NATIVE_VALUE_NUMBER;v->value.number=value.as.scalar;break;
        case Q3_MOD_VALUE_VECTOR:v->kind=APPLICATION_NATIVE_VALUE_VECTOR;v->value.vector=value.as.vector;break;
        case Q3_MOD_VALUE_ACTOR:v->kind=APPLICATION_NATIVE_VALUE_ACTOR;v->value.actor=value.as.actor;break;
        case Q3_MOD_VALUE_STRING:v->kind=APPLICATION_NATIVE_VALUE_STRING;v->value.string=value.as.string;break;
        default:return application_fail(e,QA_ERROR_ARGUMENT,"Native input received an unknown canonical value");
        }
    }
    *out=(application_native_callback_inputs){values,count,{0}};return true;
}
static size_t field_size(const qa_json_document *d,qa_json_id value)
{
    qa_json_id kind=qa_json_get(d,value,"kind");
    if(qa_json_string_equal(d,kind,"time")) kind=qa_json_get(d,value,"encoding");
    if(qa_json_string_equal(d,kind,"vector")) return 12;
    const char *names[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    const size_t sizes[]={1,1,2,2,4,4,8,8,4,8};
    for(size_t i=0;i<10;++i) if(qa_json_string_equal(d,kind,names[i])) return sizes[i];
    return 0;
}
static bool field_address(struct application_native_q2_input *s,qa_json_id field,qa_native_address *address,size_t *size,qa_error *e)
{
    struct application_native_q2 *n=s->owner->engine;const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_buffer name={0};uint64_t offset=0;
    if(!qa_json_string(d,qa_json_get(d,field,"record"),&name,e)||memchr(name.data,0,name.size)||
        !qa_json_u64(d,qa_json_get(d,field,"offset"),&offset,e)) {qa_buffer_free(&name);return false;}
    qa_json_id rows=qa_json_get(d,qa_json_root(d),"actorRecords"),record=QA_JSON_NONE;
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i);
        if(qa_json_string_equal(d,qa_json_get(d,row,"id"),(char *)name.data)) {record=row;break;}
    }
    uint64_t stride=0;bool ok=record!=QA_JSON_NONE&&qa_json_u64(d,qa_json_get(d,record,"stride"),&stride,e);
    *size=field_size(d,qa_json_get(d,field,"value"));
    bool private=false;qa_json_id fields=qa_json_get(d,record,"fields");
    for(size_t i=0;ok&&i<qa_json_size(d,fields);++i) {
        qa_json_id f=qa_json_at(d,fields,i);uint64_t start,length;
        if(qa_json_string_equal(d,qa_json_get(d,f,"binding"),"private")&&
            qa_json_u64(d,qa_json_get(d,f,"offset"),&start,e)&&qa_json_u64(d,qa_json_get(d,f,"byteLength"),&length,e)&&
            offset>=start&&offset-start<=length&&*size<=length-(offset-start)) private=true;
    }
    ok=ok&&*size&&offset<=stride&&*size<=stride-offset&&private;
    if(ok) ok=application_native_q2_callbacks_record(n->callbacks,s->actor,(char *)name.data,address,e)&&
        *address&&offset<=UINT64_MAX-*address;
    if(ok) {*address+=offset;ok=qa_native_range_check(instance(n),*address,*size,QA_NATIVE_MEMORY_READ|QA_NATIVE_MEMORY_WRITE,e);}
    qa_buffer_free(&name);return ok||application_fail(e,QA_ERROR_FORMAT,"Native input field is outside declared private client storage");
}
static bool output_handler(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *e)
{
    native_input_output *o=context;
    if(!input_current(o->input,e)||native!=instance(o->input->owner->engine)) return false;
    if(o->self_index<count&&arguments[o->self_index].type==QA_NATIVE_ADDRESS&&arguments[o->self_index].as.address==o->self)
        o->called=true;
    return qa_native_invoke_original(binding,arguments,count,result,e);
}
static bool outputs_close(native_input_output **list,qa_error *e)
{
    while(*list) {
        native_input_output *o=*list;
        if(o->handler&&!qa_native_unobserve_entry(o->handler,e)) return false;
        *list=o->next;free(o);
    }
    return true;
}
static bool outputs_open(struct application_native_q2_input *s,qa_json_id binding,native_input_output **out,qa_error *e)
{
    struct application_native_q2 *n=s->owner->engine;const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id rows=qa_json_get(d,binding,"outputs"),fields=qa_json_get(d,qa_json_get(d,qa_json_root(d),"clients"),"inputFields");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i);native_input_output *o=calloc(1,sizeof(*o));
        if(!o) return application_fail(e,QA_ERROR_MEMORY,"Retaining native input output observations");
        native_input_output **tail=out;while(*tail) tail=&(*tail)->next;
        *tail=o;o->input=s;o->declaration=row;
        if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"field")) {
            qa_buffer record={0};uint64_t offset=0;qa_json_id field=QA_JSON_NONE;
            bool ok=qa_json_string(d,qa_json_get(d,row,"record"),&record,e)&&qa_json_u64(d,qa_json_get(d,row,"offset"),&offset,e);
            for(size_t j=0;ok&&j<qa_json_size(d,fields);++j) {qa_json_id f=qa_json_at(d,fields,j);uint64_t at;
                if(qa_json_string_equal(d,qa_json_get(d,f,"record"),(char *)record.data)&&qa_json_u64(d,qa_json_get(d,f,"offset"),&at,e)&&at==offset) {field=f;break;}}
            qa_buffer_free(&record);o->declaration=field;
            if(!ok||field==QA_JSON_NONE||!field_address(s,field,&o->address,&o->bytes,e)||
                !qa_native_read(instance(n),o->address,o->before,o->bytes,e)) return false;
        } else if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"handler")) {
            qa_json_id args=qa_json_get(d,row,"arguments");size_t count=qa_json_size(d,args);qa_native_type types[64];
            if(count>64) return application_fail(e,QA_ERROR_FORMAT,"Native input output handler exceeds ABI arguments");
            o->self_index=SIZE_MAX;
            static const char *names[]={"void","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
            for(size_t j=0;j<count;++j) {qa_json_id arg=qa_json_at(d,args,j),kind=qa_json_get(d,arg,"kind");qa_native_value_type type=QA_NATIVE_ADDRESS;
                for(size_t k=1;k<11;++k) if(qa_json_string_equal(d,kind,names[k])) type=(qa_native_value_type)k;
                types[j]=(qa_native_type){.kind=type,.count=1};
                if(qa_json_string_equal(d,kind,"actor")&&qa_json_string_equal(d,qa_json_get(d,arg,"input"),"self")) {
                    qa_buffer name={0};if(!qa_json_string(d,qa_json_get(d,arg,"record"),&name,e)) return false;
                    bool ok=application_native_q2_callbacks_record(n->callbacks,s->actor,(char *)name.data,&o->self,e);qa_buffer_free(&name);
                    if(!ok) return false;o->self_index=j;
                }
            }
            if(o->self_index==SIZE_MAX) return application_fail(e,QA_ERROR_FORMAT,"Native input handler has no self record argument");
            qa_native_address address;qa_native_module_info module=qa_native_module_describe(n->provider->state.native.module);
            qa_native_signature signature={.abi=module.image.target.abi,.parameters=types,.parameter_count=count,.result={.kind=QA_NATIVE_VOID,.count=1}};
            if(!application_native_q2_callbacks_entry(n->callbacks,qa_json_get(d,row,"entry"),&address,e)||
                !qa_native_observe_entry(instance(n),address,&signature,output_handler,o,&o->handler,e)) return false;
        } else return application_fail(e,QA_ERROR_FORMAT,"Native input has an unknown output declaration");
    }
    return true;
}
static bool outputs_read(native_input_output *list,qa_error *e)
{
    for(native_input_output *o=list;o;o=o->next) {
        struct application_native_q2_input *s=o->input;struct application_native_q2 *n=s->owner->engine;
        const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);application_q3_mod_output result={0};
        if(!input_current(s,e)) return false;
        if(o->handler) {
            if(!o->called) continue;result.consume=true;qa_json_id names=qa_json_get(d,o->declaration,"inputs");
            for(size_t i=0;i<qa_json_size(d,names);++i) {size_t j=1;while(j<Q3_MOD_INPUT_COUNT&&!qa_json_string_equal(d,qa_json_at(d,names,i),input_names[j])) ++j;
                if(j==Q3_MOD_INPUT_COUNT) return application_fail(e,QA_ERROR_FORMAT,"Native input consume names an unknown scalar input");result.value.inputs|=1u<<j;}
        } else {
            uint8_t bytes[12];if(!qa_native_read(instance(n),o->address,bytes,o->bytes,e)) return false;
            if(!memcmp(bytes,o->before,o->bytes)) continue;
            qa_json_id value=qa_json_get(d,o->declaration,"value"),name=qa_json_get(d,qa_json_get(d,value,"value"),"name");
            size_t j=0;while(j<Q3_MOD_INPUT_COUNT&&!qa_json_string_equal(d,name,input_names[j])) ++j;
            if(j==Q3_MOD_INPUT_COUNT) return application_fail(e,QA_ERROR_FORMAT,"Native output field is not a declared client input");result.input=(application_q3_mod_input)j;
            qa_json_id kind=qa_json_get(d,value,"kind");
            if(!j) {if(o->bytes!=12) return application_fail(e,QA_ERROR_FORMAT,"Native view-angle output is not a source vector");result.value.angles=qa_v3(qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8));}
            else if(qa_json_string_equal(d,kind,"float32")) result.value.scalar=qa_load_f32le(bytes);
            else if(qa_json_string_equal(d,kind,"float64")) {uint64_t bits=qa_load_u64le(bytes);memcpy(&result.value.scalar,&bits,8);}
            else {uint64_t integer=o->bytes==1?bytes[0]:o->bytes==2?qa_load_u16le(bytes):o->bytes==4?qa_load_u32le(bytes):qa_load_u64le(bytes);
                bool signed_value=qa_json_string_equal(d,kind,"int8")||qa_json_string_equal(d,kind,"int16")||qa_json_string_equal(d,kind,"int32")||qa_json_string_equal(d,kind,"int64");
                if(signed_value&&o->bytes<8&&(integer&(UINT64_C(1)<<(o->bytes*8-1)))) integer|=UINT64_MAX<<(o->bytes*8);
                result.value.scalar=signed_value?(double)(int64_t)integer:(double)integer;}
        }
        if(!s->output(s->context,&result,e)||!input_current(s,e)) return false;
    }
    return true;
}
static bool input_run(struct application_native_q2_input *s,bool before,qa_error *e)
{
    struct application_native_q2 *n=s->owner->engine;const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id rows=qa_json_get(d,qa_json_get(d,qa_json_root(d),"clients"),"input");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id binding=qa_json_at(d,rows,i);
        if(!qa_json_string_equal(d,qa_json_get(d,binding,"scope"),s->slice?"movement-slice":"client-command")||
            !qa_json_string_equal(d,qa_json_get(d,binding,"phase"),before?"before":"after")) continue;
        if(s->outputs) return application_fail(e,QA_ERROR_ARGUMENT,"Native input retains output observer cleanup");
        bool ok=!before||outputs_open(s,binding,&s->outputs,e);
        qa_json_id calls=qa_json_get(d,binding,"calls");
        for(size_t j=0;ok&&j<qa_json_size(d,calls);++j) {
            application_native_callback_value values[Q3_MOD_VALUE_COUNT];application_native_callback_inputs inputs;double result;
            ok=input_values(s,values,&inputs,e)&&application_native_q2_callbacks_call(n->callbacks,qa_json_at(d,calls,j),&inputs,&result,e);
        }
        if(ok&&before) ok=outputs_read(s->outputs,e);
        qa_error cleanup={0};bool closed=outputs_close(&s->outputs,&cleanup);
        if(!closed) { /* Observer contexts must remain owned until checked retirement. */
            s->failed=true;if(ok&&e) *e=cleanup;
        }
        if(!ok||!closed) return false;
    }
    return true;
}
bool application_native_q2_input_begin(struct application_native_q2 *n,qa_actor_id actor,bool slice,
    bool (*values)(void *,application_q3_mod_inputs *,qa_error *),
    bool (*output)(void *,const application_q3_mod_output *,qa_error *),void *context,
    struct application_native_q2_input **out,qa_error *e)
{
    if(!out||*out) return application_fail(e,QA_ERROR_ARGUMENT,"Native input scope requires an empty owner slot");
    if(!n||!n->callbacks) return true;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id clients=qa_json_get(d,qa_json_root(d),"clients"),bindings=qa_json_get(d,clients,"input");bool matching=false;
    for(size_t i=0;i<qa_json_size(d,bindings);++i) if(qa_json_string_equal(d,qa_json_get(d,qa_json_at(d,bindings,i),"scope"),slice?"movement-slice":"client-command")) matching=true;
    if(!matching) return true;
    bool admitted=false;for(uint32_t i=1;i<257;++i) admitted|=n->clients[i].connected&&n->clients[i].begun&&qa_actor_id_equal(n->clients[i].actor,actor);
    if(!admitted) return true;
    if(!values||!output||!application_native_q2_stages_prepare(n,e)) return false;
    struct application_native_q2_input *s=calloc(1,sizeof(*s));
    if(!s) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native input application");
    s->owner=n->stages;s->actor=actor;s->slice=slice;s->values=values;s->output=output;s->context=context;
    s->outer=n->stages->inputs;n->stages->inputs=s;*out=s;
    application_native_callback_value input[Q3_MOD_VALUE_COUNT];application_native_callback_inputs in;
    bool ok=input_values(s,input,&in,e);qa_json_id fields=qa_json_get(d,clients,"inputFields");
    for(size_t i=0;ok&&i<qa_json_size(d,fields);++i) {
        native_input_store *store=calloc(1,sizeof(*store));if(!store) {ok=application_fail(e,QA_ERROR_MEMORY,"Retaining native input private bytes");break;}
        qa_json_id field=qa_json_at(d,fields,i);
        ok=field_address(s,field,&store->address,&store->bytes,e)&&qa_native_read(instance(n),store->address,store->previous,store->bytes,e);
        if(!ok) {free(store);break;}store->next=s->stores;s->stores=store;
        ok=application_native_q2_callbacks_input_write(n->callbacks,qa_json_get(d,field,"value"),&in,store->address,e);
    }
    if(ok) ok=input_run(s,true,e);
    s->failed=!ok;return ok;
}
bool application_native_q2_input_complete(struct application_native_q2_input *s,bool completed,qa_error *e)
{
    if(!s||s->completed) return true;
    s->completed=true;
    if(!completed||s->failed) {s->failed=true;return true;}
    bool ok=input_run(s,false,e);s->failed=!ok;return ok;
}
bool application_native_q2_input_abort(struct application_native_q2_input **slot,qa_error *e)
{
    struct application_native_q2_input *s=slot?*slot:NULL;if(!s) return true;
    if(s->owner->inputs!=s) return application_fail(e,QA_ERROR_ARGUMENT,"Native input scopes must retire in reverse entry order");
    if(!outputs_close(&s->outputs,e)) return false;
    bool restore=s->failed;for(struct application_native_q2_input *outer=s->outer;outer;outer=outer->outer) restore|=qa_actor_id_equal(outer->actor,s->actor);
    while(s->stores) {
        native_input_store *store=s->stores;
        if(restore&&qa_actors_get(qa_session_actors(s->owner->engine->provider->application->session),s->actor)&&
            !qa_native_write(instance(s->owner->engine),store->address,(qa_bytes){store->previous,store->bytes},e)) return false;
        s->stores=store->next;free(store);
    }
    s->owner->inputs=s->outer;free(s);*slot=NULL;return true;
}
