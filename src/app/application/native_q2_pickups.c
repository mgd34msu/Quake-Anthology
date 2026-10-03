#include "native_q2_pickups.h"
#include <stdlib.h>
#include <string.h>

typedef struct pickup_context_field {
    char *record;
    uint32_t offset;
    size_t bytes;
    qa_json_id value;
} pickup_context_field;
typedef struct pickup_definition {
    char *name;
    qa_item_id *offered;
    size_t offered_count;
    qa_pickup_write *writes;
    size_t write_count;
    pickup_context_field *fields;
    size_t field_count;
    qa_json_id gate,grant;
    bool always;
} pickup_definition;
typedef struct pickup_actor pickup_actor;
typedef struct pickup_rule_context { pickup_actor *actor; pickup_definition *definition; } pickup_rule_context;
struct pickup_actor {
    pickup_actor *next;
    application_native_q2_pickups *owner;
    qa_actor_id actor;
    qa_pickup_lease lease;
    qa_pickup_rule *rules;
    pickup_rule_context *contexts;
    size_t rule_count;
    unsigned entered;
    bool releasing,closed;
};
typedef struct pickup_saved { qa_native_address address; size_t bytes; uint8_t before[12]; } pickup_saved;
typedef struct pickup_call {
    struct pickup_call *next;
    pickup_rule_context *rule;
    qa_actor_id pickup;
    qa_pickup_execution *execution;
    application_native_callback_inputs inputs;
    application_native_q2_pickup_scope *watch;
    pickup_saved *saved;
    size_t saved_count;
    qa_pickup_outcome outcome;
    bool executing,watch_close_ready,restore_ready;
} pickup_call;
struct application_native_q2_pickups {
    application_native_q2_pickups_options options;
    pickup_definition *definitions;
    size_t count,bound_count;
    pickup_actor *actors;
    pickup_call *pending;
    unsigned calls;
    uint8_t pointer_bytes;
    bool active,restoring;
};
static bool fail(qa_error *e,qa_status status,const char *message)
{ qa_error_set(e,status,0,"%s",message); return false; }
static const qa_json_document *document(application_native_q2_pickups *o)
{ return application_native_q2_callbacks_document(o->options.callbacks); }
static bool text(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer b={0};
    if(!qa_json_string(d,id,&b,e)) return false;
    if(!b.size||memchr(b.data,0,b.size)) { qa_buffer_free(&b); return fail(e,QA_ERROR_FORMAT,"Native pickup text is empty or contains NUL"); }
    *out=(char *)b.data; return true;
}
static bool word(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{
    uint64_t n;
    if(!qa_json_u64(d,id,&n,e)||n>UINT32_MAX) return fail(e,QA_ERROR_FORMAT,"Native pickup offset exceeds its source word");
    *out=(uint32_t)n; return true;
}
static size_t scalar_bytes(const qa_json_document *d,qa_json_id id)
{
    const char *names[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    const size_t sizes[]={1,1,2,2,4,4,8,8,4,8};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) if(qa_json_string_equal(d,id,names[i])) return sizes[i];
    return 0;
}
static bool item(application_native_q2_pickups *o,qa_json_id id,qa_item_id *out,qa_error *e)
{
    char *name=NULL;
    if(!text(document(o),id,&name,e)) return false;
    bool ok=strchr(name,':')&&qa_strings_intern_cstr(qa_session_strings(o->options.session),name,out,e);
    free(name); return ok||fail(e,QA_ERROR_FORMAT,"Native pickup item is not namespaced");
}
static bool overlap(uint32_t a,size_t an,uint32_t b,size_t bn)
{ return (uint64_t)a+an>b&&(uint64_t)b+bn>a; }
static bool private_context(application_native_q2_pickups *o,pickup_context_field *f,qa_error *e)
{
    const qa_json_document *d=document(o); qa_json_id root=qa_json_root(d);
    qa_json_id clients=qa_json_get(d,qa_json_get(d,root,"clients"),"records");
    for(size_t i=0;i<qa_json_size(d,clients);++i)
        if(qa_json_string_equal(d,qa_json_at(d,clients,i),f->record)) return fail(e,QA_ERROR_FORMAT,"Native pickup context addresses client storage");
    qa_json_id records=qa_json_get(d,root,"actorRecords"); bool declared=false;
    for(size_t i=0;i<qa_json_size(d,records);++i) {
        qa_json_id r=qa_json_at(d,records,i);
        if(!qa_json_string_equal(d,qa_json_get(d,r,"id"),f->record)) continue;
        uint32_t stride;
        if(!word(d,qa_json_get(d,r,"stride"),&stride,e)||f->offset>stride||f->bytes>stride-f->offset) return false;
        qa_json_id fields=qa_json_get(d,r,"fields");
        for(size_t j=0;j<qa_json_size(d,fields);++j) {
            qa_json_id field=qa_json_at(d,fields,j),binding=qa_json_get(d,field,"binding");
            size_t bytes=0; uint32_t at;
            if(qa_json_string_equal(d,binding,"private")) { uint32_t n; if(!word(d,qa_json_get(d,field,"byteLength"),&n,e)) return false; bytes=n; }
            else if(qa_json_string_equal(d,binding,"constant")) bytes=scalar_bytes(d,qa_json_get(d,field,"encoding"));
            else if(qa_json_string_equal(d,binding,"address")) bytes=o->pointer_bytes;
            else if(qa_json_string_equal(d,binding,"constant-vector")) bytes=12;
            else continue;
            if(!word(d,qa_json_get(d,field,"offset"),&at,e)) return false;
            if(at<=f->offset&&(uint64_t)f->offset+f->bytes<=(uint64_t)at+bytes) declared=true;
        }
        break;
    }
    if(!declared) return fail(e,QA_ERROR_FORMAT,"Native pickup context lacks declared separate source storage");
    qa_json_id source=qa_json_get(d,root,"sourceActors");
    if(source!=QA_JSON_NONE&&qa_json_type(d,source)!=QA_JSON_NULL&&qa_json_string_equal(d,qa_json_get(d,root,"entityRecord"),f->record)) {
        qa_json_id fields=qa_json_get(d,source,"fields"),callbacks=qa_json_get(d,source,"callbacks");
        const char *pointers[]={"ground","think","use"};
        for(size_t i=0;i<sizeof(pointers)/sizeof(*pointers);++i) {
            qa_json_id id=qa_json_get(d,fields,pointers[i]); uint32_t at;
            if(id==QA_JSON_NONE||qa_json_type(d,id)==QA_JSON_NULL) continue;
            if(!word(d,id,&at,e)||overlap(f->offset,f->bytes,at,o->pointer_bytes)) return fail(e,QA_ERROR_FORMAT,"Native pickup context overlaps source actor lifetime");
        }
        for(size_t i=0;i<qa_json_size(d,callbacks);++i) {
            qa_json_id id=qa_json_at(d,callbacks,i); uint32_t at;
            if(qa_json_type(d,id)!=QA_JSON_NUMBER) continue;
            if(!word(d,id,&at,e)||overlap(f->offset,f->bytes,at,o->pointer_bytes)) return fail(e,QA_ERROR_FORMAT,"Native pickup context overlaps source actor callback");
        }
        qa_json_id next=qa_json_get(d,fields,"nextthink"); uint32_t at;
        size_t bytes=scalar_bytes(d,qa_json_get(d,next,"encoding"));
        if(!bytes||!word(d,qa_json_get(d,next,"offset"),&at,e)||overlap(f->offset,f->bytes,at,bytes)) return fail(e,QA_ERROR_FORMAT,"Native pickup context overlaps source actor timer");
    }
    return true;
}
static bool inventory_declared(application_native_q2_pickups *o,qa_json_id item_id,bool capacity)
{
    const qa_json_document *d=document(o); qa_json_id root=qa_json_root(d),items=qa_json_get(d,root,"items");
    qa_json_id rows=qa_json_get(d,items,capacity?"storage":"definitions");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i); qa_buffer a={0},b={0};
        bool same=qa_json_string(d,item_id,&a,NULL)&&qa_json_string(d,qa_json_get(d,row,"item"),&b,NULL)&&a.size==b.size&&!memcmp(a.data,b.data,a.size);
        qa_buffer_free(&a); qa_buffer_free(&b);
        if(same&&(!capacity||(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"counter")&&qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,row,"capacity"),"kind"),"field")))) return true;
    }
    rows=qa_json_get(d,root,"actorRecords");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id fields=qa_json_get(d,qa_json_at(d,rows,i),"fields");
        for(size_t j=0;j<qa_json_size(d,fields);++j) {
            qa_json_id field=qa_json_at(d,fields,j); qa_buffer a={0},b={0};
            bool same=qa_json_string(d,item_id,&a,NULL)&&qa_json_string(d,qa_json_get(d,field,"item"),&b,NULL)&&a.size==b.size&&!memcmp(a.data,b.data,a.size);
            qa_buffer_free(&a); qa_buffer_free(&b);
            if(same&&qa_json_string_equal(d,qa_json_get(d,field,"binding"),capacity?"inventory-capacity":"inventory")) return true;
        }
    }
    return false;
}
static bool input_validate(application_native_q2_pickups *o,qa_json_id value,qa_error *e)
{
    const qa_json_document *d=document(o); qa_native_value_type type;
    if(!application_native_q2_callbacks_value_validate(o->options.callbacks,value,&type,e)) return false;
    qa_json_id kind=qa_json_get(d,value,"kind"),raw=qa_json_get(d,value,"value"),name;
    if(qa_json_string_equal(d,kind,"address")) return true;
    if(qa_json_string_equal(d,kind,"time"))
        return qa_json_string_equal(d,qa_json_get(d,value,"input"),"time")||fail(e,QA_ERROR_FORMAT,"Native pickup requires unavailable time input");
    if(qa_json_string_equal(d,kind,"actor")||qa_json_string_equal(d,kind,"client")||qa_json_string_equal(d,kind,"userinfo")) {
        name=qa_json_get(d,value,"input");
        return qa_json_string_equal(d,name,"self")||qa_json_string_equal(d,name,"other")||fail(e,QA_ERROR_FORMAT,"Native pickup requires unavailable actor input");
    }
    if(qa_json_string_equal(d,kind,"user-command")) return fail(e,QA_ERROR_FORMAT,"Native pickup has no user command input");
    if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"input")) return true;
    name=qa_json_get(d,raw,"name");
    if(qa_json_string_equal(d,kind,"string")) return qa_json_string_equal(d,name,"item")||fail(e,QA_ERROR_FORMAT,"Native pickup requires unavailable string input");
    if(qa_json_string_equal(d,kind,"vector")) return fail(e,QA_ERROR_FORMAT,"Native pickup has no vector input");
    return qa_json_string_equal(d,name,"time")||qa_json_string_equal(d,name,"pickup-count")||
        qa_json_string_equal(d,name,"pickup-has-count")||qa_json_string_equal(d,name,"pickup-dropped")||
        fail(e,QA_ERROR_FORMAT,"Native pickup requires unavailable scalar input");
}
static bool call_validate(application_native_q2_pickups *o,qa_json_id call,qa_error *e)
{
    const qa_json_document *d=document(o); qa_json_id args=qa_json_get(d,call,"arguments"),globals=qa_json_get(d,call,"globals");
    if(qa_json_type(d,call)!=QA_JSON_OBJECT||qa_json_type(d,args)!=QA_JSON_ARRAY||qa_json_type(d,globals)!=QA_JSON_ARRAY)
        return fail(e,QA_ERROR_FORMAT,"Native pickup requires its authored source call");
    qa_json_id returns=qa_json_get(d,call,"returns");
    if(!scalar_bytes(d,returns)&&!qa_json_string_equal(d,returns,"void")) return fail(e,QA_ERROR_FORMAT,"Native pickup has unknown source result representation");
    for(size_t i=0;i<qa_json_size(d,args);++i) if(!input_validate(o,qa_json_at(d,args,i),e)) return false;
    for(size_t i=0;i<qa_json_size(d,globals);++i) if(!input_validate(o,qa_json_get(d,qa_json_at(d,globals,i),"value"),e)) return false;
    return true;
}
static bool parse_definition(application_native_q2_pickups *o,size_t index,qa_json_id id,qa_error *e)
{
    const qa_json_document *d=document(o); pickup_definition *r=o->definitions+index;
    r->gate=QA_JSON_NONE;
    if(!text(d,qa_json_get(d,id,"id"),&r->name,e)) return false;
    for(size_t i=0;i<index;++i) if(!strcmp(r->name,o->definitions[i].name)) return fail(e,QA_ERROR_FORMAT,"Native pickup repeats its source rule identity");
    qa_json_id offered=qa_json_get(d,id,"offered"),writes=qa_json_get(d,id,"writes"),fields=qa_json_get(d,id,"context");
    if(qa_json_type(d,offered)!=QA_JSON_ARRAY||qa_json_type(d,writes)!=QA_JSON_ARRAY||qa_json_type(d,fields)!=QA_JSON_ARRAY)
        return fail(e,QA_ERROR_FORMAT,"Native pickup lacks its authored rule arrays");
    r->offered_count=qa_json_size(d,offered); r->write_count=qa_json_size(d,writes); r->field_count=qa_json_size(d,fields);
    if(!r->offered_count) return fail(e,QA_ERROR_FORMAT,"Native pickup has no offered items");
    r->offered=calloc(r->offered_count,sizeof(*r->offered)); r->writes=r->write_count?calloc(r->write_count,sizeof(*r->writes)):NULL;
    r->fields=r->field_count?calloc(r->field_count,sizeof(*r->fields)):NULL;
    if(!r->offered||(r->write_count&&!r->writes)||(r->field_count&&!r->fields)) return fail(e,QA_ERROR_MEMORY,"Owning native pickup declaration");
    for(size_t i=0;i<r->offered_count;++i) {
        if(!item(o,qa_json_at(d,offered,i),r->offered+i,e)) return false;
        for(size_t j=0;j<=index;++j) {
            pickup_definition *other=o->definitions+j;
            for(size_t k=0;k<(j==index?i:other->offered_count);++k)
                if(r->offered[i]==other->offered[k]) return fail(e,QA_ERROR_FORMAT,"Native pickup has ambiguous offered item ownership");
        }
    }
    for(size_t i=0;i<r->write_count;++i) {
        qa_json_id w=qa_json_at(d,writes,i),kind=qa_json_get(d,w,"kind"); qa_pickup_write *out=r->writes+i;
        if(qa_json_string_equal(d,kind,"protection")) {
            out->resource.kind=QA_PICKUP_PROTECTION; qa_json_id channel=qa_json_get(d,w,"channel");
            if(qa_json_string_equal(d,channel,"regular")) out->resource.channel=QA_PROTECTION_REGULAR;
            else if(qa_json_string_equal(d,channel,"powered")) out->resource.channel=QA_PROTECTION_POWERED;
            else return fail(e,QA_ERROR_FORMAT,"Native pickup names unknown protection channel");
            bool found=false; qa_json_id protections=qa_json_get(d,qa_json_root(d),"protection");
            for(size_t j=0;j<qa_json_size(d,protections);++j)
                if(qa_json_string_equal(d,qa_json_get(d,qa_json_at(d,protections,j),"channel"),out->resource.channel==QA_PROTECTION_REGULAR?"regular":"powered")) found=true;
            if(!found||!o->options.protection) return fail(e,QA_ERROR_FORMAT,"Native pickup has no actual protection owner");
        } else if(qa_json_string_equal(d,kind,"inventory")) {
            out->resource.kind=QA_PICKUP_INVENTORY; qa_json_id item_id=qa_json_get(d,w,"item"),f=qa_json_get(d,w,"fields");
            if(!item(o,item_id,&out->resource.item,e)) return false;
            if(qa_json_string_equal(d,f,"count")) out->fields=QA_PICKUP_COUNT;
            else if(qa_json_string_equal(d,f,"capacity")) out->fields=QA_PICKUP_CAPACITY;
            else if(qa_json_string_equal(d,f,"count-and-capacity")) out->fields=QA_PICKUP_COUNT_CAPACITY;
            else return fail(e,QA_ERROR_FORMAT,"Native pickup names unknown inventory fields");
            if((out->fields!=QA_PICKUP_CAPACITY&&!inventory_declared(o,item_id,false))||(out->fields!=QA_PICKUP_COUNT&&!inventory_declared(o,item_id,true)))
                return fail(e,QA_ERROR_FORMAT,"Native pickup has no declared inventory storage");
        } else return fail(e,QA_ERROR_FORMAT,"Native pickup names unknown resource kind");
        for(size_t j=0;j<i;++j) if(out->resource.kind==r->writes[j].resource.kind&&
            (out->resource.kind==QA_PICKUP_PROTECTION?out->resource.channel==r->writes[j].resource.channel:out->resource.item==r->writes[j].resource.item))
            return fail(e,QA_ERROR_FORMAT,"Native pickup repeats resource writes");
    }
    qa_json_id operation=qa_json_get(d,id,"operation"),kind=qa_json_get(d,operation,"kind");
    r->grant=qa_json_get(d,operation,"grant");
    if(qa_json_string_equal(d,kind,"gate-then-grant")) {
        r->gate=qa_json_get(d,operation,"gate"); qa_json_id accepts=qa_json_get(d,operation,"grantAccepts");
        r->always=qa_json_string_equal(d,accepts,"always");
        if(!r->always&&!qa_json_string_equal(d,accepts,"nonzero")) return fail(e,QA_ERROR_FORMAT,"Native pickup has unknown grant decision");
        if(qa_json_string_equal(d,qa_json_get(d,r->gate,"returns"),"void")) return fail(e,QA_ERROR_FORMAT,"Native pickup gate lacks source decision");
    } else if(!qa_json_string_equal(d,kind,"boolean-grant")) return fail(e,QA_ERROR_FORMAT,"Native pickup has unknown operation");
    if(!r->always&&qa_json_string_equal(d,qa_json_get(d,r->grant,"returns"),"void")) return fail(e,QA_ERROR_FORMAT,"Native pickup grant lacks source decision");
    if(!call_validate(o,r->grant,e)||(r->gate!=QA_JSON_NONE&&!call_validate(o,r->gate,e))) return false;
    for(size_t i=0;i<r->field_count;++i) {
        qa_json_id f=qa_json_at(d,fields,i); pickup_context_field *out=r->fields+i; qa_native_value_type type;
        out->value=qa_json_get(d,f,"value");
        if(!text(d,qa_json_get(d,f,"record"),&out->record,e)||!word(d,qa_json_get(d,f,"offset"),&out->offset,e)||
            !application_native_q2_callbacks_value_validate(o->options.callbacks,out->value,&type,e)) return false;
        qa_json_id vkind=qa_json_get(d,out->value,"kind");
        out->bytes=qa_json_string_equal(d,vkind,"vector")?12:qa_json_string_equal(d,vkind,"address")?o->pointer_bytes:
            scalar_bytes(d,qa_json_string_equal(d,vkind,"time")?qa_json_get(d,out->value,"encoding"):vkind);
        if(!out->bytes||!private_context(o,out,e)) return fail(e,QA_ERROR_FORMAT,"Native pickup context has invalid source representation");
        if(!qa_json_string_equal(d,vkind,"address")) {
            qa_json_id value=qa_json_get(d,out->value,"value");
            if(qa_json_string_equal(d,vkind,"time")) {
                if(!qa_json_string_equal(d,qa_json_get(d,out->value,"input"),"time")) return fail(e,QA_ERROR_FORMAT,"Native pickup context requires unavailable time input");
            } else if(qa_json_string_equal(d,qa_json_get(d,value,"kind"),"input")) {
                qa_json_id name=qa_json_get(d,value,"name");
                if(!qa_json_string_equal(d,name,"time")&&!qa_json_string_equal(d,name,"pickup-count")&&!qa_json_string_equal(d,name,"pickup-has-count")&&!qa_json_string_equal(d,name,"pickup-dropped"))
                    return fail(e,QA_ERROR_FORMAT,"Native pickup context requires unavailable scalar input");
                if(qa_json_string_equal(d,vkind,"vector")) return fail(e,QA_ERROR_FORMAT,"Native pickup context has no vector input");
            }
        }
        for(size_t j=0;j<i;++j) if(!strcmp(out->record,r->fields[j].record)&&overlap(out->offset,out->bytes,r->fields[j].offset,r->fields[j].bytes))
            return fail(e,QA_ERROR_FORMAT,"Native pickup context overlaps another context write");
    }
    return true;
}
static pickup_actor *find(application_native_q2_pickups *o,qa_actor_id actor)
{ for(pickup_actor *a=o->actors;a;a=a->next) if(qa_actor_id_equal(a->actor,actor)) return a; return NULL; }
static bool actor_current(pickup_actor *a,bool *out,qa_error *e)
{
    application_native_q2_pickups *o=a->owner; *out=false;
    if(!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    if(!o->active||o->restoring||a->releasing||!qa_actors_get(qa_session_actors(o->options.session),a->actor)||
        !qa_pickups_registration_current(o->options.pickups,a->lease,o->options.owner)) return true;
    return application_native_q2_callbacks_client_live_read(o->options.callbacks,a->actor,out,e);
}
static bool call_current(pickup_call *call,bool *out,qa_error *e)
{
    if(!actor_current(call->rule->actor,out,e)) return false;
    *out=*out&&qa_actors_get(qa_session_actors(call->rule->actor->owner->options.session),call->pickup)&&qa_pickup_current(call->execution);
    return true;
}
static bool cleanup(application_native_q2_pickups *o,pickup_call *call,qa_error *e)
{
    if(!call->watch_close_ready&&!call->restore_ready) return true;
    if(!application_native_q2_records_pickup_end(application_native_q2_callbacks_records(o->options.callbacks),&call->watch,e)) return false;
    if(!call->restore_ready) return true;
    while(call->saved_count) {
        pickup_saved *s=call->saved+call->saved_count-1;
        if(!qa_native_write(application_native_q2_callbacks_instance(o->options.callbacks),s->address,(qa_bytes){s->before,s->bytes},e)) return false;
        --call->saved_count;
    }
    return true;
}
static bool execute(void *context,qa_error *e)
{
    pickup_call *call=context; pickup_actor *a=call->rule->actor; application_native_q2_pickups *o=a->owner;
    pickup_definition *r=call->rule->definition; bool valid=false; double result=0;
    bool ok=call_current(call,&valid,e)&&valid;
    if(ok) ok=application_native_q2_records_pickup_begin(application_native_q2_callbacks_records(o->options.callbacks),a->actor,call->execution,
        o->options.protection?application_native_q2_protection_item:NULL,o->options.protection,&call->watch,e);
    if(ok&&r->gate!=QA_JSON_NONE) {
        ok=application_native_q2_callbacks_call_scoped(o->options.callbacks,r->gate,&call->inputs,&result,e)&&call_current(call,&valid,e);
        if(ok&&(!valid||result==0)) goto finished;
    }
    if(ok) {
        ok=application_native_q2_callbacks_call_scoped(o->options.callbacks,r->grant,&call->inputs,&result,e)&&call_current(call,&valid,e);
        if(ok&&valid&&(r->always||result!=0)) call->outcome=QA_PICKUP_ACCEPTED;
    }
finished:;
    call->watch_close_ready=true;
    qa_error original=e?*e:(qa_error){0},close={0};
    bool closed=application_native_q2_records_pickup_end(application_native_q2_callbacks_records(o->options.callbacks),&call->watch,&close);
    if(ok&&!closed&&e) *e=close;
    else if(!ok&&e) *e=original;
    return ok&&closed;
}
static bool take(void *context,const qa_pickup_offer *offer,qa_pickup_execution *execution,qa_pickup_outcome *out,qa_error *e)
{
    pickup_rule_context *rule=context; pickup_actor *a=rule->actor; application_native_q2_pickups *o=a->owner;
    bool valid=false,offered=false;
    if(!offer||!out||!qa_actor_id_equal(offer->recipient,a->actor)||!actor_current(a,&valid,e)||!valid||
        !qa_actors_get(qa_session_actors(o->options.session),offer->pickup)||!qa_pickup_current(execution))
        return fail(e,QA_ERROR_NOT_FOUND,"Native pickup rule lost its actual recipient or resource lease");
    for(size_t i=0;i<rule->definition->offered_count;++i) if(rule->definition->offered[i]==offer->item) offered=true;
    if(!offered) return fail(e,QA_ERROR_ARGUMENT,"Native pickup grant names an unauthored offered item");
    if(!application_native_q2_callbacks_pickup_foreign(o->options.callbacks,offer,e)) return false;
    const char *name=qa_strings_cstr(qa_session_strings(o->options.session),offer->item);
    if(!name) return fail(e,QA_ERROR_ARGUMENT,"Native pickup item lost its actual string identity");
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=a->actor},
        {.name="other",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=offer->pickup},
        {.name="item",.kind=APPLICATION_NATIVE_VALUE_STRING,.value.string=name},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)offer->time_ns/1e9},
        {.name="pickup-count",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=offer->override_count?offer->count:0},
        {.name="pickup-has-count",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=offer->override_count?1:0},
        {.name="pickup-dropped",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=offer->dropped?1:0}};
    pickup_call *call=calloc(1,sizeof(*call));
    if(!call) return fail(e,QA_ERROR_MEMORY,"Retaining native pickup source execution");
    call->saved=rule->definition->field_count?calloc(rule->definition->field_count,sizeof(*call->saved)):NULL;
    if(rule->definition->field_count&&!call->saved) { free(call); return fail(e,QA_ERROR_MEMORY,"Owning native pickup context restoration"); }
    call->rule=rule; call->pickup=offer->pickup; call->execution=execution;
    call->inputs=(application_native_callback_inputs){values,sizeof(values)/sizeof(*values),{0}};
    call->executing=true; call->next=o->pending; o->pending=call; ++o->calls; ++a->entered;
    bool ok=true;
    for(size_t i=0;ok&&i<rule->definition->field_count;++i) {
        pickup_context_field *f=rule->definition->fields+i; pickup_saved *s=call->saved+call->saved_count;
        ok=call_current(call,&valid,e)&&valid&&application_native_q2_callbacks_pickup_context_address(o->options.callbacks,offer->pickup,f->record,f->offset,f->bytes,&s->address,e);
        s->bytes=f->bytes;
        if(ok) ok=qa_native_read(application_native_q2_callbacks_instance(o->options.callbacks),s->address,s->before,s->bytes,e);
        if(ok) { ++call->saved_count; ok=application_native_q2_callbacks_input_write(o->options.callbacks,f->value,&call->inputs,s->address,e); }
    }
    if(ok) ok=o->options.protection?application_native_q2_protection_observe_pickup(o->options.protection,a->actor,execution,execute,call,e):
        application_native_q2_callbacks_transfer(o->options.callbacks,execute,call,e);
    qa_error original=e?*e:(qa_error){0},close={0};
    call->execution=NULL; call->inputs=(application_native_callback_inputs){0}; call->restore_ready=true;
    bool closed=application_native_q2_pickups_drain(o,&close); call->executing=false;
    if(ok) *out=call->outcome;
    --a->entered; --o->calls;
    if(closed) {
        pickup_call **link=&o->pending; while(*link&&*link!=call) link=&(*link)->next;
        if(*link) *link=call->next;
        free(call->saved); free(call);
    }
    if(ok&&!closed&&e) *e=close;
    else if(!ok&&e) *e=original;
    return ok&&closed;
}
static pickup_actor *actor_new(application_native_q2_pickups *o,qa_actor_id actor,qa_error *e)
{
    pickup_actor *a=calloc(1,sizeof(*a));
    if(!a) { fail(e,QA_ERROR_MEMORY,"Owning native pickup recipient"); return NULL; }
    a->rules=calloc(o->count,sizeof(*a->rules)); a->contexts=calloc(o->count,sizeof(*a->contexts));
    if(!a->rules||!a->contexts) { free(a->rules); free(a->contexts); free(a); fail(e,QA_ERROR_MEMORY,"Owning native pickup actor rules"); return NULL; }
    a->owner=o; a->actor=actor;
    for(size_t i=0;i<o->count;++i) {
        pickup_definition *r=o->definitions+i; a->contexts[i]=(pickup_rule_context){a,r};
        if(!r->write_count) continue;
        a->rules[a->rule_count++]=(qa_pickup_rule){.id=(uint32_t)i+1,.offered=r->offered,.offered_count=r->offered_count,
            .writes=r->writes,.write_count=r->write_count,.context=a->contexts+i,.take=take};
    }
    return a;
}
static void actor_free(pickup_actor *a) { free(a->rules); free(a->contexts); free(a); }
static void definitions_free(application_native_q2_pickups *o)
{
    for(size_t i=0;i<o->count;++i) {
        pickup_definition *r=o->definitions+i;
        if(r->fields) for(size_t j=0;j<r->field_count;++j) free(r->fields[j].record);
        free(r->name); free(r->offered); free(r->writes); free(r->fields);
    }
    free(o->definitions);
}
bool application_native_q2_pickups_create(const application_native_q2_pickups_options *options,application_native_q2_pickups **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->session||!options->pickups||!options->owner)
        return fail(e,QA_ERROR_ARGUMENT,"Native pickups require their actual source and canonical owners");
    application_native_q2_pickups *o=calloc(1,sizeof(*o));
    if(!o) return fail(e,QA_ERROR_MEMORY,"Owning native pickups");
    o->options=*options; const qa_json_document *d=document(o); qa_json_id root=qa_json_root(d),rows=qa_json_get(d,root,"pickups");
    bool ok=rows==QA_JSON_NONE||qa_json_type(d,rows)==QA_JSON_ARRAY;
    o->count=qa_json_size(d,rows); uint32_t width=0;
    if(ok) ok=word(d,qa_json_get(d,qa_json_get(d,qa_json_get(d,root,"target"),"abi"),"pointerBytes"),&width,e)&&(width==4||width==8);
    o->pointer_bytes=(uint8_t)width;
    if(ok&&o->count) ok=o->count<UINT32_MAX&&qa_json_type(d,qa_json_get(d,root,"clients"))==QA_JSON_OBJECT;
    if(ok&&o->count) {
        o->definitions=calloc(o->count,sizeof(*o->definitions));
        ok=o->definitions!=NULL;
        if(!ok) fail(e,QA_ERROR_MEMORY,"Owning native pickup source definitions");
    }
    if(!o->definitions&&o->count) o->count=0;
    for(size_t i=0;ok&&i<o->count;++i) {
        ok=parse_definition(o,i,qa_json_at(d,rows,i),e);
        if(ok&&o->definitions[i].write_count) ++o->bound_count;
    }
    if(!ok) {
        definitions_free(o); free(o);
        if(e&&e->code!=QA_OK) return false;
        return fail(e,QA_ERROR_FORMAT,"Native pickup declaration is invalid");
    }
    *out=o; return true;
}
bool application_native_q2_pickups_activate(application_native_q2_pickups *o,qa_error *e)
{
    if(!o||o->restoring||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    o->active=true; return true;
}
bool application_native_q2_pickups_bind(application_native_q2_pickups *o,qa_actor_id actor,qa_error *e)
{
    if(!o||o->restoring) return fail(e,QA_ERROR_ARGUMENT,"Native pickup binding requires a live source owner");
    if(!o->active||!o->bound_count) return true;
    bool live=false;
    if(!application_native_q2_callbacks_client_live_read(o->options.callbacks,actor,&live,e)) return false;
    if(!live) return true;
    pickup_actor *a=find(o,actor);
    if(a) return !a->releasing&&qa_pickups_registration_current(o->options.pickups,a->lease,o->options.owner);
    a=actor_new(o,actor,e); if(!a) return false;
    if(!qa_pickups_bind(o->options.pickups,actor,o->options.owner,a->rules,a->rule_count,&a->lease,e)) { actor_free(a); return false; }
    a->next=o->actors; o->actors=a; return true;
}
bool application_native_q2_pickups_release(application_native_q2_pickups *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    pickup_actor *a=find(o,actor); if(!a) return true;
    a->releasing=true;
    if(a->closed) return true;
    if(qa_pickups_registration_owned(o->options.pickups,a->lease,o->options.owner)&&!qa_pickups_close(o->options.pickups,a->lease,e)) return false;
    a->closed=true; return true;
}
bool application_native_q2_pickups_drain(application_native_q2_pickups *o,qa_error *e)
{
    if(!o) return true;
    pickup_call **link=&o->pending;
    while(*link) {
        pickup_call *call=*link;
        if(!cleanup(o,call,e)) return false;
        if(call->executing) break;
        *link=call->next; free(call->saved); free(call);
    }
    if(qa_pickups_idle(o->options.pickups)) {
        pickup_actor **row=&o->actors;
        while(*row) {
            pickup_actor *a=*row;
            if(a->releasing&&a->closed&&!a->entered) { *row=a->next; actor_free(a); }
            else row=&a->next;
        }
    }
    return true;
}
bool application_native_q2_pickups_idle(const application_native_q2_pickups *o)
{ return !o||(!o->calls&&!o->pending); }
bool application_native_q2_pickups_destroy(application_native_q2_pickups **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_pickups *o=*owner; o->active=false;
    if(o->calls||!application_native_q2_pickups_drain(o,e)) return false;
    for(pickup_actor *a=o->actors;a;a=a->next) if(!application_native_q2_pickups_release(o,a->actor,e)) return false;
    if(!application_native_q2_pickups_drain(o,e)||o->actors) return fail(e,QA_ERROR_ARGUMENT,"Native pickup retirement retains an actual canonical grant callback");
    definitions_free(o); free(o); *owner=NULL; return true;
}
bool application_native_q2_pickups_saved_rule(application_native_q2_pickups *o,qa_actor_id actor,qa_actor_owner owner,uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *e)
{
    pickup_actor *a=o?find(o,actor):NULL;
    if(!o||!out||!o->restoring||owner!=o->options.owner||!a||a->releasing||a->lease.serial!=serial||!id||id>o->count)
        return fail(e,QA_ERROR_FORMAT,"Saved native pickup rule lacks its retained source recipient and serial");
    for(size_t i=0;i<a->rule_count;++i) if(a->rules[i].id==id) { *out=a->rules[i]; return true; }
    return fail(e,QA_ERROR_FORMAT,"Saved native pickup rule has no authored resource writes");
}
static bool codec(application_native_q2_pickups *o,qa_source_save_io *io,pickup_actor **decoded,bool *active,qa_error *e)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint8_t tag[4]={'Q','N','P','K'};
    if(!qa_source_save_bytes(io,tag,sizeof(tag))||memcmp(tag,(uint8_t[]){'Q','N','P','K'},sizeof(tag))||!qa_source_save_bool(io,active)) return false;
    qa_string_id owner=o->options.owner;
    if(!qa_source_save_string(io,&owner)||owner!=o->options.owner) return fail(e,QA_ERROR_FORMAT,"Saved native pickup source owner changed");
    size_t count=o->count;
    if(!qa_source_save_count(io,&count,UINT32_MAX-1)||count!=o->count) return fail(e,QA_ERROR_FORMAT,"Saved native pickup rule count changed");
    for(size_t i=0;i<count;++i) {
        const char *name=o->definitions[i].name;
        if(!qa_source_save_text(io,&name)||!name||strcmp(name,o->definitions[i].name)) return fail(e,QA_ERROR_FORMAT,"Saved native pickup source rule identity changed");
    }
    size_t actors=0;
    if(!reading) for(pickup_actor *a=o->actors;a;a=a->next) ++actors;
    if(!qa_source_save_count(io,&actors,reading?io->input.size/21:SIZE_MAX)||(!o->bound_count&&actors)||(!*active&&actors)) return false;
    pickup_actor *a=o->actors,**tail=decoded;
    for(size_t i=0;i<actors;++i) {
        qa_actor_id actor=reading?(qa_actor_id){0}:a->actor; uint64_t serial=reading?0:a->lease.serial;
        if(!qa_source_save_actor(io,&actor)||!qa_source_save_u64(io,&serial)||!serial||!qa_actors_get(qa_session_actors(o->options.session),actor)) return false;
        if(reading) {
            for(pickup_actor *other=*decoded;other;other=other->next) if(qa_actor_id_equal(other->actor,actor)||other->lease.serial==serial)
                return fail(e,QA_ERROR_FORMAT,"Saved native pickup repeats a recipient or registration serial");
            pickup_actor *row=actor_new(o,actor,e); if(!row) return false;
            row->lease=(qa_pickup_lease){actor,serial}; *tail=row; tail=&row->next;
        } else {
            if(a->releasing||!qa_pickups_registration_current(o->options.pickups,a->lease,o->options.owner)) return fail(e,QA_ERROR_NOT_FOUND,"Native pickup capture lost its actual canonical registration");
            a=a->next;
        }
    }
    return true;
}
bool application_native_q2_pickups_checkpoint(application_native_q2_pickups *o,qa_buffer *out,qa_error *e)
{
    if(!o||!out||o->restoring||!application_native_q2_pickups_idle(o)||!qa_pickups_idle(o->options.pickups)||
        !application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    qa_source_save_io io; if(!qa_source_save_writer(&io,o->options.session,e)) return false;
    bool active=o->active; pickup_actor *unused=NULL;
    bool ok=codec(o,&io,&unused,&active,e)&&qa_source_save_finish(&io,out); qa_source_save_dispose(&io); return ok;
}
bool application_native_q2_pickups_restore(application_native_q2_pickups *o,qa_bytes input,qa_error *e)
{
    if(!o||o->actors||o->active||o->restoring||!application_native_q2_pickups_idle(o)||!application_native_q2_callbacks_restoring(o->options.callbacks))
        return fail(e,QA_ERROR_ARGUMENT,"Native pickup restore requires its detached empty source candidate");
    qa_source_save_io io; if(!qa_source_save_reader(&io,o->options.session,input,e)) return false;
    pickup_actor *rows=NULL; bool active=false;
    bool ok=codec(o,&io,&rows,&active,e)&&qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    if(!ok) { while(rows) { pickup_actor *next=rows->next; actor_free(rows); rows=next; } return false; }
    o->actors=rows; o->active=active; o->restoring=true; return true;
}
bool application_native_q2_pickups_finish_restore(application_native_q2_pickups *o,qa_error *e)
{
    if(!o||!application_native_q2_pickups_idle(o)||!application_native_q2_callbacks_storage_current(o->options.callbacks,e)) return false;
    for(pickup_actor *a=o->actors;a;a=a->next) {
        bool live=false;
        if(a->releasing||a->closed||!application_native_q2_callbacks_client_live_read(o->options.callbacks,a->actor,&live,e)||!live||
            !qa_pickups_registration_current(o->options.pickups,a->lease,o->options.owner)) return fail(e,QA_ERROR_FORMAT,"Restored native pickup source or canonical lease changed");
    }
    o->restoring=false; return true;
}
