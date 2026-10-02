#include "native_q2_callbacks.h"
#include "guest_native_q2_private.h"
#include <float.h>
#include <math.h>

typedef struct native_temporary {
    struct native_temporary *next;
    qa_native_address address;
} native_temporary;
typedef struct native_global {
    struct native_global *next;
    qa_native_address address;
    uint8_t bytes[12];
    size_t size;
} native_global;
typedef struct native_userinfo {
    struct native_userinfo *next;
    qa_actor_id actor;
    uint32_t slot;
    qa_native_address address;
    size_t capacity;
} native_userinfo;
struct application_native_q2_callbacks {
    struct application_native_q2 *engine;
    qa_json_document *document;
    const qa_native_module *module;
    const qa_native_declaration *declaration;
    qa_native_target target;
    native_temporary *pending;
    native_global *pending_globals;
    unsigned calls;
    bool validated;
};
static bool fail(qa_error *e, const char *text)
{ return application_fail(e, QA_ERROR_FORMAT, text); }
static qa_native_instance *instance(application_native_q2_callbacks *o)
{ return o->engine->provider->state.native.host ? qa_native_host_instance(o->engine->provider->state.native.host) : NULL; }
static bool current(application_native_q2_callbacks *o, qa_error *e)
{
    struct application_native_q2 *n = o ? o->engine : NULL;
    application_provider *p = n ? n->provider : NULL;
    return (p && p->state.native.q2_engine == n && n->callbacks == o &&
        p->state.native.module == o->module && n->declaration == o->declaration &&
        p->launch && p->launch->declaration && instance(o) &&
        qa_native_get_module(instance(o)) == o->module && !qa_native_terminal(instance(o))) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Native callbacks lost their acquired module instance");
}
static bool word(const qa_json_document *d, qa_json_id object, const char *name,
    uint32_t *out, qa_error *e)
{
    uint64_t v;
    if (!qa_json_u64(d, qa_json_get(d, object, name), &v, e)) return false;
    if (v > UINT32_MAX) return fail(e, "Native callback address exceeds its declared word");
    *out = (uint32_t)v; return true;
}
static bool text(const qa_json_document *d, qa_json_id id, qa_buffer *out, qa_error *e)
{
    if (!qa_json_string(d, id, out, e)) return false;
    if (memchr(out->data, 0, out->size)) { qa_buffer_free(out); return fail(e, "Native callback text contains NUL"); }
    return true;
}
static size_t scalar_type(const qa_json_document *d, qa_json_id id, qa_native_value_type *out)
{
    static const char *names[] = {"void", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32", "float64"};
    static const size_t sizes[] = {0,1,1,2,2,4,4,8,8,4,8};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (qa_json_string_equal(d,id,names[i])) { *out = (qa_native_value_type)i; return sizes[i]; }
    *out = QA_NATIVE_BYTES; return 0;
}
static bool number_value(qa_native_value_type type, double value, qa_native_value *out, qa_error *e)
{
    if (!isfinite(value)) return fail(e, "Native callback number is nonfinite");
    *out = (qa_native_value){.type=type};
    double integer = trunc(value);
    switch (type) {
    case QA_NATIVE_F32: out->as.f32=(float)value; return isfinite(out->as.f32) || fail(e,"Native callback exceeds float32");
    case QA_NATIVE_F64: out->as.f64=value; return true;
    case QA_NATIVE_I8: if(integer<INT8_MIN||integer>INT8_MAX) break; out->as.i8=(int8_t)integer; return true;
    case QA_NATIVE_U8: if(integer<0||integer>UINT8_MAX) break; out->as.u8=(uint8_t)integer; return true;
    case QA_NATIVE_I16: if(integer<INT16_MIN||integer>INT16_MAX) break; out->as.i16=(int16_t)integer; return true;
    case QA_NATIVE_U16: if(integer<0||integer>UINT16_MAX) break; out->as.u16=(uint16_t)integer; return true;
    case QA_NATIVE_I32: if(integer<INT32_MIN||integer>INT32_MAX) break; out->as.i32=(int32_t)integer; return true;
    case QA_NATIVE_U32: if(integer<0||integer>UINT32_MAX) break; out->as.u32=(uint32_t)integer; return true;
    case QA_NATIVE_I64: if(fabs(integer)>9007199254740991.0) break; out->as.i64=(int64_t)integer; return true;
    case QA_NATIVE_U64: if(integer<0||integer>9007199254740991.0) break; out->as.u64=(uint64_t)integer; return true;
    default: break;
    }
    return fail(e,"Native callback number exceeds its source scalar");
}
static void encoded(qa_native_value v, uint8_t *bytes)
{
    switch(v.type) {
    case QA_NATIVE_I8: bytes[0]=(uint8_t)v.as.i8; break;
    case QA_NATIVE_U8: bytes[0]=v.as.u8; break;
    case QA_NATIVE_I16: qa_store_u16le(bytes,(uint16_t)v.as.i16); break;
    case QA_NATIVE_U16: qa_store_u16le(bytes,v.as.u16); break;
    case QA_NATIVE_I32: qa_store_u32le(bytes,(uint32_t)v.as.i32); break;
    case QA_NATIVE_U32: qa_store_u32le(bytes,v.as.u32); break;
    case QA_NATIVE_I64: qa_store_u64le(bytes,(uint64_t)v.as.i64); break;
    case QA_NATIVE_U64: qa_store_u64le(bytes,v.as.u64); break;
    case QA_NATIVE_F32: { uint32_t bits; memcpy(&bits,&v.as.f32,4); qa_store_u32le(bytes,bits); break; }
    case QA_NATIVE_F64: { uint64_t bits; memcpy(&bits,&v.as.f64,8); qa_store_u64le(bytes,bits); break; }
    default: break;
    }
}
static double result_number(qa_native_value v)
{
    switch(v.type) {
    case QA_NATIVE_I8:return v.as.i8; case QA_NATIVE_U8:return v.as.u8;
    case QA_NATIVE_I16:return v.as.i16; case QA_NATIVE_U16:return v.as.u16;
    case QA_NATIVE_I32:return v.as.i32; case QA_NATIVE_U32:return v.as.u32;
    case QA_NATIVE_I64:return (double)v.as.i64; case QA_NATIVE_U64:return (double)v.as.u64;
    case QA_NATIVE_F32:return v.as.f32; case QA_NATIVE_F64:return v.as.f64;
    default:return 0;
    }
}
static bool pointer_read(application_native_q2_callbacks *o, qa_native_address at,
    qa_native_address *out, qa_error *e)
{
    uint8_t bytes[8];
    if (!qa_native_read(instance(o),at,bytes,o->target.pointer_bytes,e)) return false;
    *out=o->target.pointer_bytes==4?qa_load_u32le(bytes):qa_load_u64le(bytes); return true;
}
bool application_native_q2_callbacks_address(application_native_q2_callbacks *o, qa_json_id id,
    qa_native_address *out, qa_error *e)
{
    if(!out||!current(o,e)) return false;
    if(qa_json_type(o->document,id)==QA_JSON_NULL) { *out=0; return true; }
    uint32_t rva;
    if(!word(o->document,id,"rva",&rva,e)||!qa_native_rva(instance(o),rva,1,out,e)) return false;
    qa_json_id list=qa_json_get(o->document,id,"indirections");
    if(qa_json_type(o->document,list)!=QA_JSON_ARRAY) return fail(e,"Native address has no declared indirection roster");
    for(size_t i=0;i<qa_json_size(o->document,list);++i) {
        uint64_t offset;
        if(!qa_json_u64(o->document,qa_json_at(o->document,list,i),&offset,e)||offset>UINT32_MAX||
            !pointer_read(o,*out,out,e)||!*out||*out>UINT64_MAX-offset)
            return fail(e,"Native address indirection is null or overflows");
        *out+=offset;
    }
    return true;
}
static qa_json_id record_find(application_native_q2_callbacks *o,const char *name)
{
    qa_json_id rows=qa_json_get(o->document,qa_json_root(o->document),"actorRecords");
    for(size_t i=0;i<qa_json_size(o->document,rows);++i) {
        qa_json_id r=qa_json_at(o->document,rows,i);
        if(qa_json_string_equal(o->document,qa_json_get(o->document,r,"id"),name)) return r;
    }
    return QA_JSON_NONE;
}
static bool source_slot(application_native_q2_callbacks *o,qa_actor_id actor,uint32_t *out,qa_error *e)
{
    if(!current(o,e)||!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callback actor generation retired");
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(o),&table,e)) return false;
    for(uint32_t i=0;i<table.count;++i) {
        qa_native_slot_binding b;
        if(!qa_native_slot(instance(o),i,&b,e)) return false;
        if(b.kind!=QA_NATIVE_SLOT_FREE&&qa_actor_id_equal(b.actor,actor)) { *out=i; return true; }
    }
    return application_fail(e,QA_ERROR_NOT_FOUND,"Native callback actor has no actual source slot");
}
bool application_native_q2_callbacks_record(application_native_q2_callbacks *o,qa_actor_id actor,
    const char *name,qa_native_address *out,qa_error *e)
{
    if(!name||!out||!current(o,e)) return false;
    if(!actor.registry) { *out=0; return true; }
    qa_json_id r=record_find(o,name),base=qa_json_get(o->document,r,"base");
    uint32_t first,stride,capacity,slot;
    if(r==QA_JSON_NONE||!word(o->document,r,"firstSlot",&first,e)||!word(o->document,r,"stride",&stride,e)||
        !word(o->document,r,"capacity",&capacity,e)||!source_slot(o,actor,&slot,e)) return false;
    /* The retained fixed client map supplies the donor projection index. */
    uint32_t index=UINT32_MAX;
    for(uint32_t i=1;i<257;++i) if(qa_actor_id_equal(o->engine->clients[i].actor,actor)) { index=i-1; break; }
    if(index==UINT32_MAX) {
        if(slot<first) return fail(e,"Native actor precedes its declared source record");
        index=slot-first;
    }
    if(index>=capacity) return fail(e,"Native actor exceeds its declared record capacity");
    if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"clients")) {
        qa_native_address entity;
        uint32_t physical=first+index+1;
        if(physical<first||!qa_native_entity_address(instance(o),physical,&entity,e)) return false;
        size_t public_pointer=o->target.pointer_bytes==4?84u:o->engine->profile==QA_NATIVE_Q2_GAME_API2023?120u:88u;
        if(!pointer_read(o,entity+public_pointer,out,e)||!*out) return fail(e,"Native client record has no original storage");
    } else if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"entities")) {
        qa_native_entity_table table;
        if(!qa_native_entity_table_get(instance(o),&table,e)||table.stride!=stride||
            (uint64_t)first+capacity>table.capacity||(uint64_t)first+index>=table.count)
            return fail(e,"Native actor declaration differs from the actual export table");
        *out=table.base+((uint64_t)first+index)*stride;
    } else if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"address")) {
        if(!application_native_q2_callbacks_address(o,base,out,e)) return false;
        uint64_t delta=((uint64_t)first+index)*stride;
        if(*out>UINT64_MAX-delta) return fail(e,"Native record address overflows");
        *out+=delta;
    } else return fail(e,"Unknown native actor record base");
    if(*out>UINT64_MAX-stride) return fail(e,"Native actor record extent overflows");
    return true;
}
static const application_native_callback_value *input(const application_native_callback_inputs *in,const char *name)
{
    for(size_t i=0;in&&i<in->count;++i) if(in->values[i].name&&!strcmp(in->values[i].name,name)) return in->values+i;
    return NULL;
}
static bool temporary(application_native_q2_callbacks *o,qa_bytes bytes,size_t capacity,
    native_temporary **owned,qa_native_address *out,qa_error *e)
{
    if(bytes.size>capacity) return fail(e,"Native temporary exceeds its source extent");
    native_temporary *t=calloc(1,sizeof(*t));
    if(!t) return application_fail(e,QA_ERROR_MEMORY,"Owning native callback allocation receipt");
    if(!qa_native_allocate(instance(o),capacity,INT32_MIN+13,&t->address,e)) { free(t); return false; }
    t->next=*owned; *owned=t; *out=t->address;
    return qa_native_write(instance(o),t->address,bytes,e);
}
static bool lower(application_native_q2_callbacks *o,qa_json_id id,const application_native_callback_inputs *in,
    qa_native_value *out,size_t *storage,native_temporary **owned,native_userinfo **corrections,qa_error *e)
{
    const qa_json_document *d=o->document; qa_json_id kind=qa_json_get(d,id,"kind"),raw=qa_json_get(d,id,"value");
    qa_native_value_type type; size_t size=scalar_type(d,kind,&type); *storage=size;
    qa_buffer name={0}; const application_native_callback_value *value=NULL;
    if(qa_json_string_equal(d,kind,"actor")||qa_json_string_equal(d,kind,"client")||qa_json_string_equal(d,kind,"userinfo")||qa_json_string_equal(d,kind,"time")) {
        if(!text(d,qa_json_get(d,id,"input"),&name,e)) return false;
        value=input(in,(char *)name.data); qa_buffer_free(&name);
        if(!value) return fail(e,"Native callback omitted its declared input");
    } else if(qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"input")) {
        if(!text(d,qa_json_get(d,raw,"name"),&name,e)) return false;
        value=input(in,(char *)name.data); qa_buffer_free(&name);
        if(!value) return fail(e,"Native callback omitted its value input");
    }
    if(qa_json_string_equal(d,kind,"time")) {
        if(value->kind!=APPLICATION_NATIVE_VALUE_NUMBER) return fail(e,"Native time input has the wrong type");
        *storage=scalar_type(d,qa_json_get(d,id,"encoding"),&type);
        double scale=qa_json_string_equal(d,qa_json_get(d,id,"units"),"milliseconds")?1000:1;
        return *storage&&number_value(type,value->value.number*scale,out,e);
    }
    if(size) {
        double number;
        if(value) { if(value->kind!=APPLICATION_NATIVE_VALUE_NUMBER) return fail(e,"Native scalar input has the wrong type"); number=value->value.number; }
        else if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"float")||!qa_json_number(d,qa_json_get(d,raw,"value"),&number,e)) return false;
        return number_value(type,number,out,e);
    }
    *out=(qa_native_value){.type=QA_NATIVE_ADDRESS}; *storage=o->target.pointer_bytes;
    if(qa_json_string_equal(d,kind,"address")) return application_native_q2_callbacks_address(o,raw,&out->as.address,e);
    if(qa_json_string_equal(d,kind,"actor")) {
        if(value->kind!=APPLICATION_NATIVE_VALUE_ACTOR||!text(d,qa_json_get(d,id,"record"),&name,e)) return fail(e,"Native actor input has the wrong type");
        bool ok=application_native_q2_callbacks_record(o,value->value.actor,(char *)name.data,&out->as.address,e);
        qa_buffer_free(&name); return ok;
    }
    if(qa_json_string_equal(d,kind,"client")||qa_json_string_equal(d,kind,"userinfo")) {
        if(value->kind!=APPLICATION_NATIVE_VALUE_ACTOR) return fail(e,"Native client input has the wrong type");
        uint32_t slot=0;
        for(uint32_t i=1;i<257;++i) if(o->engine->clients[i].reserved&&qa_actor_id_equal(o->engine->clients[i].actor,value->value.actor)) { slot=i; break; }
        if(!slot) return fail(e,"Native client input has no retained physical client");
        if(qa_json_string_equal(d,kind,"client")) { *storage=4; *out=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=(int32_t)slot-1}; return true; }
        size_t capacity=o->engine->profile==QA_NATIVE_Q2_GAME_API3?512u:2048u;
        const char *userinfo=o->engine->clients[slot].userinfo; size_t length=strlen(userinfo);
        if(length>=capacity) return fail(e,"Native client userinfo exceeds its API buffer");
        native_userinfo *c=calloc(1,sizeof(*c));
        if(!c) return application_fail(e,QA_ERROR_MEMORY,"Owning native userinfo correction");
        c->next=*corrections; *corrections=c; c->actor=value->value.actor; c->slot=slot; c->capacity=capacity;
        bool ok=temporary(o,(qa_bytes){(const uint8_t *)userinfo,length+1},capacity+4,owned,&out->as.address,e);
        c->address=out->as.address; return ok;
    }
    if(qa_json_string_equal(d,kind,"user-command"))
        return in&&in->user_command.data&&in->user_command.size&&temporary(o,in->user_command,in->user_command.size,owned,&out->as.address,e);
    if(qa_json_string_equal(d,kind,"vector")) {
        qa_vec3 v; uint8_t bytes[12];
        if(value) { if(value->kind!=APPLICATION_NATIVE_VALUE_VECTOR) return fail(e,"Native vector input has the wrong type"); v=value->value.vector; }
        else { double x,y,z; qa_json_id at=qa_json_get(d,raw,"value");
            if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"vector")||!qa_json_number(d,qa_json_get(d,at,"x"),&x,e)||
                !qa_json_number(d,qa_json_get(d,at,"y"),&y,e)||!qa_json_number(d,qa_json_get(d,at,"z"),&z,e)) return false;
            v=qa_v3((float)x,(float)y,(float)z);
        }
        if(!qa_vec_finite(v)) return fail(e,"Native vector exceeds binary32 storage");
        float axes[]={v.x,v.y,v.z}; for(size_t i=0;i<3;++i) encoded((qa_native_value){.type=QA_NATIVE_F32,.as.f32=axes[i]},bytes+i*4);
        *storage=12; return temporary(o,(qa_bytes){bytes,12},12,owned,&out->as.address,e);
    }
    if(qa_json_string_equal(d,kind,"string")) {
        const char *string;
        if(value) { if(value->kind!=APPLICATION_NATIVE_VALUE_STRING||!value->value.string) return fail(e,"Native string input has the wrong type"); string=value->value.string; }
        else { if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"string")||!text(d,qa_json_get(d,raw,"value"),&name,e)) return false; string=(char *)name.data; }
        bool ok=temporary(o,(qa_bytes){(const uint8_t *)string,strlen(string)+1},strlen(string)+1,owned,&out->as.address,e);
        qa_buffer_free(&name); return ok;
    }
    return fail(e,"Unknown native callback value kind");
}
static bool target(application_native_q2_callbacks *o,qa_json_id call,qa_native_address *address,qa_error *e)
{
    qa_json_id entry=qa_json_get(o->document,call,"entry"),kind=qa_json_get(o->document,entry,"kind");
    if(qa_json_string_equal(o->document,kind,"rva")) {
        uint32_t rva; return word(o->document,entry,"rva",&rva,e)&&qa_native_rva(instance(o),rva,1,address,e);
    }
    qa_buffer name={0};
    if(!text(o->document,qa_json_get(o->document,entry,"name"),&name,e)) return false;
    bool ok=qa_json_string_equal(o->document,kind,"export")?qa_native_export(instance(o),(char *)name.data,address,e):
        qa_json_string_equal(o->document,kind,"game-export")?qa_native_entry_address(instance(o),(char *)name.data,address,e):fail(e,"Unknown native callback entry kind");
    qa_buffer_free(&name); return ok;
}
static bool cleanup(application_native_q2_callbacks *o,native_global **globals,native_temporary **allocations,qa_error *e)
{
    bool ok=true; qa_error first={0};
    while(*globals) {
        native_global *g=*globals; qa_error fault={0};
        if(!qa_native_write(instance(o),g->address,(qa_bytes){g->bytes,g->size},&fault)) {
            if(ok) first=fault; ok=false; break;
        }
        *globals=g->next; free(g);
    }
    while(!*globals&&*allocations) {
        native_temporary *t=*allocations; qa_error fault={0};
        if(!qa_native_free(instance(o),t->address,&fault)) { if(ok) first=fault; ok=false; break; }
        *allocations=t->next; free(t);
    }
    if(!ok&&e) *e=first;
    return ok;
}
bool application_native_q2_callbacks_call(application_native_q2_callbacks *o,qa_json_id call,
    const application_native_callback_inputs *in,double *returned,qa_error *e)
{
    if(!returned||!current(o,e)||o->pending||o->pending_globals) return false;
    const qa_json_document *d=o->document;
    qa_json_id arguments=qa_json_get(d,call,"arguments"),globals=qa_json_get(d,call,"globals");
    if(qa_json_type(d,arguments)!=QA_JSON_ARRAY||qa_json_type(d,globals)!=QA_JSON_ARRAY) return fail(e,"Native call has no argument/global roster");
    size_t count=qa_json_size(d,arguments);
    if(count>64) return fail(e,"Native call exceeds the admitted ABI argument limit");
    qa_native_type types[64]; qa_native_value values[64],result={0};
    native_temporary *allocations=NULL; native_global *saved=NULL; native_userinfo *corrections=NULL;
    qa_native_address entry; qa_native_value_type returns;
    scalar_type(d,qa_json_get(d,call,"returns"),&returns);
    if(returns==QA_NATIVE_BYTES) return fail(e,"Native callback return has no scalar ABI");
    ++o->calls; ++o->engine->calls;
    bool ok=target(o,call,&entry,e);
    for(size_t i=0;ok&&i<count;++i) {
        size_t storage;
        ok=lower(o,qa_json_at(d,arguments,i),in,values+i,&storage,&allocations,&corrections,e);
        if(ok) types[i]=(qa_native_type){.kind=values[i].type,.count=1};
    }
    for(size_t i=0;ok&&i<qa_json_size(d,globals);++i) {
        qa_json_id g=qa_json_at(d,globals,i),definition=qa_json_get(d,g,"value");
        native_global *s=calloc(1,sizeof(*s)); qa_native_value v; size_t bytes;
        if(!s) { ok=application_fail(e,QA_ERROR_MEMORY,"Owning native projected global bytes"); break; }
        ok=application_native_q2_callbacks_address(o,qa_json_get(d,g,"address"),&s->address,e)&&
            lower(o,definition,in,&v,&bytes,&allocations,&corrections,e);
        if(ok) { s->size=bytes;
            ok=bytes<=sizeof(s->bytes)&&qa_native_read(instance(o),s->address,s->bytes,bytes,e);
        }
        if(!ok) { free(s); break; }
        s->next=saved; saved=s;
        uint8_t encoded_value[12]={0};
        if(v.type==QA_NATIVE_ADDRESS) {
            if(qa_json_string_equal(d,qa_json_get(d,definition,"kind"),"vector"))
                ok=qa_native_read(instance(o),v.as.address,encoded_value,12,e);
            else if(o->target.pointer_bytes==4) qa_store_u32le(encoded_value,(uint32_t)v.as.address);
            else qa_store_u64le(encoded_value,v.as.address);
        } else encoded(v,encoded_value);
        if(ok) ok=qa_native_write(instance(o),s->address,(qa_bytes){encoded_value,bytes},e);
    }
    if(ok) {
        qa_native_signature signature={.abi=o->target.abi,.parameters=types,.parameter_count=count,
            .result={.kind=returns,.count=1}};
        ok=qa_native_invoke(instance(o),entry,&signature,values,count,returns==QA_NATIVE_VOID?NULL:&result,e)&&current(o,e);
    }
    for(native_userinfo *c=corrections;ok&&c;c=c->next) {
        application_native_q2_client *client=o->engine->clients+c->slot;
        qa_buffer after={0};
        ok=qa_actor_id_equal(client->actor,c->actor)&&qa_actors_get(qa_session_actors(o->engine->provider->application->session),c->actor)&&
            qa_native_read_string(instance(o),c->address,c->capacity,&after,e);
        if(ok) { memcpy(client->userinfo,after.data,after.size); client->userinfo[after.size]=0; client->userinfo_present=true; }
        qa_buffer_free(&after);
    }
    while(corrections) { native_userinfo *c=corrections; corrections=c->next; free(c); }
    /* A nested callback can retain a failed restoration. Its newer projections
     * must unwind before the caller's saved bytes or allocations. */
    if(o->pending_globals) {
        native_global *tail=o->pending_globals;
        while(tail->next) tail=tail->next;
        tail->next=saved; saved=o->pending_globals; o->pending_globals=NULL;
    }
    if(o->pending) {
        native_temporary *tail=o->pending;
        while(tail->next) tail=tail->next;
        tail->next=allocations; allocations=o->pending; o->pending=NULL;
    }
    qa_error cleanup_error={0}; bool cleaned=cleanup(o,&saved,&allocations,&cleanup_error);
    if(!cleaned) {
        o->pending_globals=saved; o->pending=allocations;
        if(ok&&e) *e=cleanup_error;
    }
    --o->engine->calls; --o->calls;
    if(ok&&cleaned) { *returned=result_number(result); return isfinite(*returned)||fail(e,"Native callback returned nonfinite data"); }
    return false;
}
bool application_native_q2_callbacks_run(struct application_native_q2 *n,const char *section,
    const application_native_callback_inputs *in,bool *accepted,qa_error *e)
{
    if(!n||!section||!accepted) return application_fail(e,QA_ERROR_ARGUMENT,"Native callback stage requires its actual owner");
    *accepted=true; application_native_q2_callbacks *o=n->callbacks;
    if(!o) return true;
    qa_json_id object=qa_json_root(o->document);
    if(!strncmp(section,"clients.",8)) { object=qa_json_get(o->document,object,"clients"); section+=8; }
    qa_json_id list=qa_json_get(o->document,object,section);
    if(list==QA_JSON_NONE&&object!=qa_json_root(o->document)) return true;
    if(qa_json_type(o->document,list)!=QA_JSON_ARRAY) return fail(e,"Native callback stage is not a declared array");
    for(size_t i=0;i<qa_json_size(o->document,list);++i) {
        qa_json_id call=qa_json_at(o->document,list,i); double value;
        if(!application_native_q2_callbacks_call(o,call,in,&value,e)) return false;
        qa_json_id accepts=qa_json_get(o->document,call,"accepts");
        if(accepts!=QA_JSON_NONE) {
            if(qa_json_string_equal(o->document,accepts,"nonzero")) { if(value==0) { *accepted=false; return true; } }
            else if(!qa_json_string_equal(o->document,accepts,"always")) return fail(e,"Native admission has an unknown result policy");
        }
    }
    return true;
}
bool application_native_q2_callbacks_prepare(struct application_native_q2 *n,qa_error *e)
{
    qa_bytes bytes=qa_native_declaration_callbacks(n?n->declaration:NULL);
    if(!bytes.data) return true;
    if(n->callbacks) return application_fail(e,QA_ERROR_ARGUMENT,"Native callbacks already have a physical owner");
    application_native_q2_callbacks *o=calloc(1,sizeof(*o));
    if(!o) return application_fail(e,QA_ERROR_MEMORY,"Owning acquired native callbacks");
    n->callbacks=o; o->engine=n; o->declaration=n->declaration; o->module=n->provider->state.native.module;
    o->target=qa_native_module_describe(o->module).image.target;
    return qa_json_parse(bytes,&o->document,e);
}
static bool validate_calls(application_native_q2_callbacks *o,qa_json_id value,qa_error *e)
{
    const qa_json_document *d=o->document;
    if(qa_json_type(d,value)==QA_JSON_OBJECT) {
        qa_json_id arguments=qa_json_get(d,value,"arguments");
        if(arguments!=QA_JSON_NONE&&qa_json_get(d,value,"returns")!=QA_JSON_NONE) {
            qa_native_address address;
            if(!target(o,value,&address,e)) return false;
            qa_native_value_type returns;
            scalar_type(d,qa_json_get(d,value,"returns"),&returns);
            if(returns==QA_NATIVE_BYTES||qa_json_type(d,arguments)!=QA_JSON_ARRAY||
                qa_json_size(d,arguments)>64||qa_json_type(d,qa_json_get(d,value,"globals"))!=QA_JSON_ARRAY)
                return fail(e,"Native callback has an invalid call ABI");
        }
    }
    qa_json_kind kind=qa_json_type(d,value);
    if(kind==QA_JSON_OBJECT||kind==QA_JSON_ARRAY)
        for(size_t i=0;i<qa_json_size(d,value);++i) if(!validate_calls(o,qa_json_at(d,value,i),e)) return false;
    return true;
}
bool application_native_q2_callbacks_validate(struct application_native_q2 *n,qa_error *e)
{
    if(!n) return application_fail(e,QA_ERROR_ARGUMENT,"Native callback validation requires its engine");
    if(!n->callbacks) return true;
    application_native_q2_callbacks *o=n->callbacks;
    if(!current(o,e)||!validate_calls(o,qa_json_root(o->document),e)) return false;
    o->validated=true; return true;
}
bool application_native_q2_callbacks_idle(const application_native_q2_callbacks *o)
{ return !o||!o->calls; }
bool application_native_q2_callbacks_current(const application_native_q2_callbacks *o)
{
    return o && o->document && o->validated && !o->calls && !o->pending &&
        !o->pending_globals && current((application_native_q2_callbacks *)o,NULL);
}
bool application_native_q2_callbacks_close(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(o->calls) return application_fail(e,QA_ERROR_ARGUMENT,"Native callbacks retain an entered source frame");
    if((o->pending||o->pending_globals)&&!cleanup(o,&o->pending_globals,&o->pending,e)) return false;
    qa_json_destroy(o->document); free(o); n->callbacks=NULL; return true;
}
const qa_json_document *application_native_q2_callbacks_document(const application_native_q2_callbacks *o)
{ return o?o->document:NULL; }
