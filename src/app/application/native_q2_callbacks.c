#include "native_q2_callbacks.h"
#include "guest_native_q2_private.h"
#include "guest_q3_mod_operations.h"
#include "native_q2_records.h"
#include "native_q2_items.h"
#include "native_q2_protection.h"
#include "native_q2_callback_region.h"
#include "native_q2_weapon_stage.h"
#include "native_q2_client_stages.h"
#include "native_q2_client_outputs.h"
#include "native_q2_source_invocation.h"
#include "native_q2_pickups.h"
#include "native_q2_visibility.h"
#include "qa/source_save.h"
#include "qa/native_observe.h"
#include "control_frame.h"
#include "client_outputs.h"
#include "qa/text.h"
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
typedef struct native_projection_scope {
    struct native_projection_scope *outer;
    application_native_q2_record_scope *scope;
    bool completed, succeeded;
} native_projection_scope;
typedef struct native_registered {
    application_native_q2_callbacks *owner;
    qa_json_id call;
    application_q3_mod_operation operation;
    qa_operation_hook_kind stage;
    qa_string_id name;
    qa_operation_registration registration;
    bool knockback;
} native_registered;
typedef struct native_skip {
    struct native_skip *next;
    application_native_q2_callbacks *owner;
    qa_json_id call;
    uint32_t region;
    qa_native_region_binding *binding;
} native_skip;
typedef struct native_region {
    struct native_region *next;
    application_native_q2_callbacks *owner;
    application_native_q2_callback_region *scope;
    application_native_q2_source_authority authority;
    bool retained;
    qa_error retirement;
} native_region;
typedef struct native_call {
    struct native_call *next;
    qa_native_call_scope *scope;
    application_native_q2_item_receipt *items;
    struct application_native_q2_source_invocation *source;
} native_call;
struct application_native_q2_callbacks {
    struct application_native_q2 *engine;
    qa_json_document *document;
    const qa_native_module *module;
    const qa_native_declaration *declaration;
    qa_native_target target;
    native_temporary *pending;
    native_global *pending_globals;
    native_region *pending_regions;
    native_region *active_region;
    native_call *pending_calls;
    application_native_q2_records *records;
    application_native_q2_items *items;
    application_native_q2_protection *protection;
    application_native_q2_weapon_stage *weapons;
    application_native_q2_pickups *pickups;
    qa_buffer restored_weapons;
    bool components_restoring;
    native_projection_scope *scopes;
    unsigned calls;
    bool validated;
    native_registered *registrations;
    size_t registration_count;
    application_q3_mod_operation_services operations[Q3_MOD_OPERATION_COUNT];
    native_skip *skips;
    qa_json_id active_call;
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
static bool record_source(void *context,const char *name,uint32_t index,qa_native_address *out,qa_error *e)
{
    application_native_q2_callbacks *o=context;
    if(!name||!out||!current(o,e)) return false;
    qa_json_id r=record_find(o,name),base=qa_json_get(o->document,r,"base");
    uint32_t first,stride,capacity;
    if(r==QA_JSON_NONE||!word(o->document,r,"firstSlot",&first,e)||!word(o->document,r,"stride",&stride,e)||
        !word(o->document,r,"capacity",&capacity,e)) return false;
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
static bool record_validate(void *context,const char *name,qa_native_address *out,uint64_t *bytes,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_json_id r=record_find(o,name),base=qa_json_get(o->document,r,"base");
    uint32_t first,stride,capacity;
    if(!out||!bytes||!current(o,e)||r==QA_JSON_NONE||!word(o->document,r,"firstSlot",&first,e)||
        !word(o->document,r,"stride",&stride,e)||!word(o->document,r,"capacity",&capacity,e)) return false;
    *out=0; *bytes=0;
    if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"clients")) return true;
    uint64_t extent=(uint64_t)capacity*stride,delta=(uint64_t)first*stride;
    if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"entities")) {
        qa_native_entity_table table;
        if(!qa_native_entity_table_get(instance(o),&table,e)||table.stride!=stride||(uint64_t)first+capacity>table.capacity)
            return fail(e,"Native declared array differs from the actual export capacity");
        if(table.base>UINT64_MAX-delta) return fail(e,"Native source array address overflows");
        *out=table.base+delta;
    } else if(qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"address")) {
        if(!application_native_q2_callbacks_address(o,base,out,e)||*out>UINT64_MAX-delta) return false;
        *out+=delta;
    } else return fail(e,"Native record has no actual declared array source");
    if(!*out||extent>UINT64_MAX-*out) return fail(e,"Native declared array extent overflows");
    *bytes=extent; return true;
}
static bool original_record(void *context,qa_actor_id actor,const char *name,
    qa_native_address *out,bool *found,qa_error *e)
{
    application_native_q2_callbacks *o=context; *found=false; *out=0;
    const qa_actor_record *r=qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor);
    if(!current(o,e)||!r) return application_fail(e,QA_ERROR_ARGUMENT,"Native original record actor retired");
    for(uint32_t i=1;i<257;++i) if(o->engine->clients[i].reserved&&qa_actor_id_equal(o->engine->clients[i].actor,actor)) return true;
    qa_native_entity_table table; uint32_t slot=UINT32_MAX;
    if(!qa_native_entity_table_get(instance(o),&table,e)) return false;
    for(uint32_t i=0;i<table.count;++i) {
        qa_native_slot_binding binding;
        if(!qa_native_slot(instance(o),i,&binding,e)) return false;
        if(binding.kind==QA_NATIVE_SLOT_OWNED&&qa_actor_id_equal(binding.actor,actor)&&
            binding.owner==o->engine->provider->owner&&binding.source_slot==i) { slot=i; break; }
    }
    if(slot==UINT32_MAX) return true;
    qa_json_id root=qa_json_root(o->document),entity=qa_json_get(o->document,root,"entityRecord");
    if(!qa_json_string_equal(o->document,entity,name))
        return fail(e,"Owned native actor requires its actual original edict record");
    if(!qa_native_entity_address(instance(o),slot,out,e)) return false;
    *found=true; return true;
}
static bool client_slot(void *context,qa_actor_id actor,uint32_t *out,bool *found,qa_error *e)
{
    application_native_q2_callbacks *o=context; *found=false;
    if(!current(o,e)||!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor)) return false;
    for(uint32_t i=1;i<257;++i) if(o->engine->clients[i].reserved&&qa_actor_id_equal(o->engine->clients[i].actor,actor)) {
        if(*found) return fail(e,"Declared native client repeats its full actor");
        *out=i-1; *found=true;
    }
    return true;
}
static bool client_admitted(void *context,qa_actor_id actor)
{
    application_native_q2_callbacks *o=context;
    for(uint32_t i=1;i<257;++i) {
        const application_native_q2_client *c=o->engine->clients+i;
        if(c->reserved&&c->connected&&!c->denied&&!c->disconnect_started&&qa_actor_id_equal(c->actor,actor)) return true;
    }
    return false;
}
static bool client_rejected(void *context,qa_actor_id actor)
{
    application_native_q2_callbacks *o=context;
    for(uint32_t i=1;i<257;++i) {
        const application_native_q2_client *client=o->engine->clients+i;
        if(client->reserved&&client->denied&&qa_actor_id_equal(client->actor,actor)) return true;
    }
    return false;
}
static bool retained_client(application_native_q2_callbacks *o,qa_actor_id actor,bool admitted,uint32_t *out,qa_error *e)
{
    if(!current(o,e)) return false;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native protection client generation retired");
    uint32_t slot=0;
    for(uint32_t i=1;i<257;++i) {
        const application_native_q2_client *client=o->engine->clients+i;
        if(!client->reserved||!qa_actor_id_equal(client->actor,actor)) continue;
        if(slot||(admitted&&!client->connected)||client->denied||
            (client->disconnect_started&&o->engine->disconnect_client!=i))
            return application_fail(e,QA_ERROR_ARGUMENT,"Native protection client is not uniquely admitted");
        slot=i;
    }
    if(!slot) return application_fail(e,QA_ERROR_ARGUMENT,"Native protection actor has no retained source client");
    *out=slot;return true;
}
bool application_native_q2_callbacks_client_reserved_current(application_native_q2_callbacks *o,
    qa_actor_id actor,qa_error *e)
{ uint32_t slot;return retained_client(o,actor,false,&slot,e); }
bool application_native_q2_callbacks_client_live_read(application_native_q2_callbacks *o,
    qa_actor_id actor,bool *live,qa_error *e)
{
    if(!live) return application_fail(e,QA_ERROR_ARGUMENT,"Native client read requires an output");
    *live=false;
    if(!current(o,e)) return false;
    if(!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor)) return true;
    uint32_t slot=0;
    for(uint32_t i=1;i<257;++i) {
        const application_native_q2_client *retained=o->engine->clients+i;
        if(!retained->reserved||!qa_actor_id_equal(retained->actor,actor)) continue;
        if(slot) return application_fail(e,QA_ERROR_ARGUMENT,"Native client repeats its full actor");
        slot=i;
    }
    if(!slot) return true;
    const application_native_q2_client *retained=o->engine->clients+slot;
    if(!retained->connected||retained->denied||
        (retained->disconnect_started&&o->engine->disconnect_client!=slot)) return true;
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(o),&table,e)) return false;
    if(slot>=table.count) return true;
    qa_native_slot_binding binding; qa_native_address entity,client; bool active;
    if(!qa_native_slot(instance(o),slot,&binding,e)||
        !qa_native_entity_address(instance(o),slot,&entity,e)||
        !qa_native_host_source_active(o->engine->provider->state.native.host,slot,&active,e)) return false;
    size_t offset=o->target.pointer_bytes==4?84u:o->engine->profile==QA_NATIVE_Q2_GAME_API2023?120u:88u;
    if(!pointer_read(o,entity+offset,&client,e)) return false;
    *live=active&&binding.kind==QA_NATIVE_SLOT_BORROWED&&binding.owner==o->engine->provider->owner&&
        binding.source_slot==slot&&qa_actor_id_equal(binding.actor,actor)&&client!=0;
    return true;
}
bool application_native_q2_callbacks_client_current(application_native_q2_callbacks *o,
    qa_actor_id actor,qa_error *e)
{
    bool live;
    if(!application_native_q2_callbacks_client_live_read(o,actor,&live,e)) return false;
    return live||application_fail(e,QA_ERROR_ARGUMENT,"Native protection client lost its active source storage");
}
static bool projected_bound(void *context,qa_actor_id actor,uint32_t slot,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_native_slot_binding old;
    if(!current(o,e)||!qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor)||
        !qa_native_slot(instance(o),slot,&old,e)) return false;
    if(old.kind!=QA_NATIVE_SLOT_FREE) return old.kind==QA_NATIVE_SLOT_BORROWED&&qa_actor_id_equal(old.actor,actor)&&
        old.owner==o->engine->provider->owner&&old.source_slot==slot
        ? true : application_fail(e,QA_ERROR_ARGUMENT,"Native projection cannot replace an occupied original source slot");
    qa_native_slot_binding binding={.kind=QA_NATIVE_SLOT_BORROWED,.slot=slot,.actor=actor,
        .owner=o->engine->provider->owner,.source_slot=slot};
    return qa_native_bind_slot(instance(o),&binding,e);
}
static bool projected_released(void *context,qa_actor_id actor,uint32_t slot,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_native_slot_binding old;
    if(!o||!o->engine->provider->state.native.host||qa_native_get_module(instance(o))!=o->module||
        !qa_native_slot(instance(o),slot,&old,e)) return false;
    if(old.kind==QA_NATIVE_SLOT_FREE) return true;
    if(old.kind!=QA_NATIVE_SLOT_BORROWED||!qa_actor_id_equal(old.actor,actor)||old.owner!=o->engine->provider->owner||old.source_slot!=slot)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native projection release replaced its actual borrowed source slot");
    qa_native_slot_binding cleared={.kind=QA_NATIVE_SLOT_FREE,.slot=slot};
    return qa_native_bind_slot(instance(o),&cleared,e);
}
static bool projected_current(void *context,qa_actor_id actor,uint32_t slot)
{
    application_native_q2_callbacks *o=context; qa_native_slot_binding binding;
    return current(o,NULL)&&qa_native_slot(instance(o),slot,&binding,NULL)&&
        binding.kind==QA_NATIVE_SLOT_BORROWED&&qa_actor_id_equal(binding.actor,actor)&&
        binding.owner==o->engine->provider->owner&&binding.source_slot==slot;
}
static bool projection_match_read(void *context,qa_actor_id actor,qa_string_id *team,
    double *score,bool *found,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_application *app=o->engine->provider->application;
    if(!current(o,e)||!qa_actors_get(qa_session_actors(app->session),actor)) return false;
    *found=false;
    if(!app->modes||!app->primary_mode_ready) return true;
    qa_mode_player_view view;
    if(!qa_modes_player_read_optional(app->modes,app->primary_mode,actor,&view,found,e)) return false;
    if(*found) { *team=view.state.team; *score=view.state.score; }
    return true;
}
static bool projection_match_write(void *context,qa_actor_id actor,bool team,qa_string_id value,double score,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_application *app=o->engine->provider->application;
    if(!current(o,e)||!app->modes||!app->primary_mode_ready)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native projected match write has no admitted actual mode owner");
    if(team) return qa_modes_set_team(app->modes,app->primary_mode,actor,value,e);
    if(!isfinite(score)||trunc(score)!=score||score<INT32_MIN||score>INT32_MAX)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native projected score exceeds its canonical match representation");
    return qa_modes_set_score(app->modes,app->primary_mode,actor,(int32_t)score,e);
}
static bool projection_time(void *context,double *out,qa_error *e)
{
    application_native_q2_callbacks *o=context;
    return application_native_q2_callbacks_time_read(o,out,e);
}
static bool projection_pose(void *context,qa_actor_id actor,double *height,bool *crouched,qa_error *e)
{
    application_native_q2_callbacks *o=context; qa_application *app=o->engine->provider->application;
    qa_application_camera_view camera; qa_application_control_view control; application_client_outputs outputs;
    if(!current(o,e)||!qa_application_control_camera(app,actor,&camera)||!qa_application_control_read(app,actor,&control)||
        !application_control_outputs(app,actor,&outputs,e))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native client pose lost its actual canonical controls");
    *height=camera.view_offset.z;
    *crouched=control.state.kind==QA_MOVEMENT_Q3?(control.state.data.q3.movement_flags&1u)!=0:
        control.state.kind==QA_MOVEMENT_Q2_CLASSIC?(control.state.data.q2.flags&1u)!=0:
        control.state.kind==QA_MOVEMENT_Q2_RERELEASE?(control.state.data.q2r.flags&1u)!=0:
        control.state.kind==QA_MOVEMENT_QUAKEWORLD&&control.bounds.maxs.z<qa_movement_input_default(control.state.kind,actor).standing.bounds.maxs.z;
    if(outputs.has_stance) *crouched=outputs.crouched;
    return true;
}
static bool projection_current(void *context,qa_error *e)
{ return current(context,e); }
static bool call_native(application_native_q2_callbacks *,qa_json_id,const application_native_callback_inputs *,double *,bool,bool *,qa_error *);
static bool projection_lifecycle(void *context,qa_json_id call,const application_native_callback_inputs *in,
    double *result,bool *entered,qa_error *e)
{ return call_native(context,call,in,result,false,entered,e); }
static bool records_prepare(application_native_q2_callbacks *o,qa_error *e)
{
    if(o->records) return true;
    qa_application *app=o->engine->provider->application;
    application_native_q2_records_options options={.callbacks=o,.instance=instance(o),.session=app->session,
        .world=o->engine->world,.combat=app->combat,.inventory=app->inventory,.strings=qa_session_strings(app->session),
        .context=o,.current=projection_current,.owned_record=original_record,.client_slot=client_slot,
        .client_admitted=client_admitted,.client_rejected=client_rejected,
        .record_source=record_source,.record_validate=record_validate,
        .bound=projected_bound,.released=projected_released,.binding_current=projected_current,
        .match_read=projection_match_read,.match_write=projection_match_write,.time=projection_time,
        .lifecycle=projection_lifecycle,.pose=projection_pose};
    return application_native_q2_records_create(&options,&o->records,e);
}
bool application_native_q2_callbacks_record(application_native_q2_callbacks *o,qa_actor_id actor,
    const char *name,qa_native_address *out,qa_error *e)
{
    if(!name||!out||!current(o,e)) return false;
    if(!actor.registry) { *out=0; return true; }
    return records_prepare(o,e)&&application_native_q2_records_pointer(o->records,actor,name,out,e);
}
static bool pickup_actor_foreign(application_native_q2_callbacks *o,qa_actor_id actor,qa_error *e)
{
    if(!current(o,e)) return false;
    const qa_actor_record *row=qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor);
    if(!row||row->owner==o->engine->provider->owner||qa_actor_id_equal(actor,o->engine->world_actor))
        return fail(e,"Native pickup context requires a live foreign source actor");
    for(uint32_t i=1;i<257;++i)
        if(o->engine->clients[i].reserved&&qa_actor_id_equal(o->engine->clients[i].actor,actor))
            return fail(e,"Native pickup context cannot use a retained source client");
    qa_native_entity_table table;
    if(!qa_native_entity_table_get(instance(o),&table,e)) return false;
    for(uint32_t i=0;i<table.count;++i) {
        qa_native_slot_binding binding;
        if(!qa_native_slot(instance(o),i,&binding,e)) return false;
        if(binding.kind==QA_NATIVE_SLOT_OWNED&&binding.owner==o->engine->provider->owner&&
            qa_actor_id_equal(binding.actor,actor))
            return fail(e,"Native pickup context cannot use an original owned source actor");
    }
    return true;
}
bool application_native_q2_callbacks_pickup_foreign(application_native_q2_callbacks *o,
    const qa_pickup_offer *offer,qa_error *e)
{
    if(!offer||qa_actor_id_equal(offer->recipient,offer->pickup))
        return fail(e,"Native pickup context cannot borrow its recipient");
    return pickup_actor_foreign(o,offer->pickup,e);
}
bool application_native_q2_callbacks_pickup_context_address(application_native_q2_callbacks *o,
    qa_actor_id actor,const char *name,uint32_t offset,size_t bytes,qa_native_address *out,qa_error *e)
{
    if(!name||!out||!bytes||!pickup_actor_foreign(o,actor,e)) return false;
    qa_json_id record=record_find(o,name),base=qa_json_get(o->document,record,"base"); uint32_t stride;
    if(record==QA_JSON_NONE||!word(o->document,record,"stride",&stride,e)||
        qa_json_string_equal(o->document,qa_json_get(o->document,base,"kind"),"clients")||
        offset>stride||bytes>stride-offset)
        return fail(e,"Native pickup context exceeds its declared nonclient record");
    qa_json_id clients=qa_json_get(o->document,qa_json_root(o->document),"clients");
    qa_json_id client_records=qa_json_get(o->document,clients,"records");
    for(size_t i=0;i<qa_json_size(o->document,client_records);++i)
        if(qa_json_string_equal(o->document,qa_json_at(o->document,client_records,i),name))
            return fail(e,"Native pickup context cannot borrow declared client storage");
    qa_json_id fields=qa_json_get(o->document,record,"fields"); bool private_field=false;
    for(size_t i=0;i<qa_json_size(o->document,fields);++i) {
        qa_json_id field=qa_json_at(o->document,fields,i),binding=qa_json_get(o->document,field,"binding");
        uint32_t start; size_t length=0; qa_native_value_type type;
        if(!word(o->document,field,"offset",&start,e)) return false;
        if(qa_json_string_equal(o->document,binding,"private")) {
            uint32_t extent; if(!word(o->document,field,"byteLength",&extent,e)) return false; length=extent;
        } else if(qa_json_string_equal(o->document,binding,"constant"))
            length=scalar_type(o->document,qa_json_get(o->document,field,"encoding"),&type);
        else if(qa_json_string_equal(o->document,binding,"address")) length=o->target.pointer_bytes;
        else if(qa_json_string_equal(o->document,binding,"constant-vector")) length=12;
        if(length&&start<=offset&&(uint64_t)offset+bytes<=(uint64_t)start+length) private_field=true;
    }
    if(!private_field) return fail(e,"Native pickup context lacks exclusive declared source storage");
    qa_native_address address;
    if(!application_native_q2_callbacks_record(o,actor,name,&address,e)||!address||!pickup_actor_foreign(o,actor,e)) return false;
    if(qa_json_string_equal(o->document,qa_json_get(o->document,qa_json_root(o->document),"entityRecord"),name)) {
        uint32_t slot; size_t public_bytes; qa_native_slot_binding binding;
        if(!qa_native_entity_slot(instance(o),address,&slot,e)||!qa_native_slot(instance(o),slot,&binding,e)||
            !qa_native_host_source_public_bytes(o->engine->provider->state.native.host,slot,&public_bytes,e)) return false;
        if(binding.kind!=QA_NATIVE_SLOT_BORROWED||binding.owner!=o->engine->provider->owner||
            binding.source_slot!=slot||!qa_actor_id_equal(binding.actor,actor)||offset<public_bytes)
            return fail(e,"Native pickup context overlaps the public source entity record");
    }
    if(address>UINT64_MAX-offset||bytes>UINT64_MAX-address-offset)
        return fail(e,"Native pickup context address extent overflows");
    address+=offset;
    if(!qa_native_range_check(instance(o),address,bytes,QA_NATIVE_MEMORY_WRITE,e)) return false;
    *out=address; return current(o,e)&&pickup_actor_foreign(o,actor,e);
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
static bool classic_string(const char *string,qa_buffer *out,qa_error *e)
{
    qa_bytes input_bytes={(const uint8_t *)string,strlen(string)}; size_t cursor=0,used=0; uint32_t scalar;
    uint8_t *classic=malloc(input_bytes.size+1);
    if(!classic) return application_fail(e,QA_ERROR_MEMORY,"Encoding actual native source byte string");
    while(qa_utf8_next(input_bytes,&cursor,&scalar)) {
        if(!scalar||scalar>255) { free(classic); return fail(e,"Native classic strings require source byte characters"); }
        classic[used++]=(uint8_t)scalar;
    }
    classic[used]=0; *out=(qa_buffer){classic,used+1}; return true;
}
static bool returned_userinfo(application_native_q2_callbacks *o,const native_userinfo *c,qa_buffer *out,qa_error *e)
{
    qa_buffer raw={0};
    if(!qa_native_read_string(instance(o),c->address,c->capacity,&raw,e)) return false;
    bool ok=true;
    if(o->engine->profile==QA_NATIVE_Q2_GAME_API3) {
        if(raw.size>(SIZE_MAX-1)/2) ok=application_fail(e,QA_ERROR_MEMORY,"Native userinfo text extent overflows");
        else {
            out->data=malloc(raw.size*2+1);
            if(!out->data) ok=application_fail(e,QA_ERROR_MEMORY,"Retaining actual native source userinfo");
            else {
                for(size_t i=0;i<raw.size;++i) { char bytes[4]; size_t count=qa_utf8_encode(raw.data[i],bytes); memcpy(out->data+out->size,bytes,count); out->size+=count; }
                out->data[out->size]=0;
            }
        }
    } else if(!qa_utf8_valid((qa_bytes){raw.data,raw.size})) ok=fail(e,"Native userinfo result is not valid UTF-8");
    else { *out=raw; raw=(qa_buffer){0}; }
    qa_buffer_free(&raw); return ok;
}
bool application_native_q2_callbacks_userinfo_validate(struct application_native_q2 *n,const char *value,qa_error *e)
{
    if(!n||!value) return fail(e,"Native userinfo requires its actual retained owner and text");
    if(n->profile==QA_NATIVE_Q2_GAME_API3) {
        qa_buffer bytes={0}; bool ok=classic_string(value,&bytes,e)&&bytes.size<=512;
        qa_buffer_free(&bytes); return ok||fail(e,"Native classic userinfo exceeds its source byte limit");
    }
    return strlen(value)<2048&&qa_utf8_valid((qa_bytes){(const uint8_t *)value,strlen(value)}) ? true:
        fail(e,"Native userinfo exceeds its source UTF-8 limit");
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
        const char *userinfo=o->engine->clients[slot].userinfo; qa_buffer source_string={0};
        bool source_ok=o->engine->profile==QA_NATIVE_Q2_GAME_API3?classic_string(userinfo,&source_string,e):true;
        qa_bytes source_bytes=source_string.data?(qa_bytes){source_string.data,source_string.size}:
            (qa_bytes){(const uint8_t *)userinfo,strlen(userinfo)+1};
        if(!source_ok||source_bytes.size>capacity) { qa_buffer_free(&source_string); return source_ok?fail(e,"Native client userinfo exceeds its API buffer"):false; }
        native_userinfo *c=calloc(1,sizeof(*c));
        if(!c) { qa_buffer_free(&source_string); return application_fail(e,QA_ERROR_MEMORY,"Owning native userinfo correction"); }
        c->next=*corrections; *corrections=c; c->actor=value->value.actor; c->slot=slot; c->capacity=capacity;
        bool ok=temporary(o,source_bytes,capacity+4,owned,&out->as.address,e);
        qa_buffer_free(&source_string);
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
        qa_buffer classic={0}; bool ok=classic_string(string,&classic,e);
        if(ok&&classic.size>SIZE_MAX-4095) ok=application_fail(e,QA_ERROR_MEMORY,"Native classic allocation extent overflows");
        if(ok) ok=temporary(o,(qa_bytes){classic.data,classic.size},((classic.size+4095)/4096)*4096,owned,&out->as.address,e);
        qa_buffer_free(&classic);
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
static bool protection_arguments(application_native_q2_callbacks *o,qa_json_id absorb,
    const application_native_callback_inputs *in,qa_native_value *values,size_t count,
    native_temporary **allocations,qa_error *e)
{
    const application_native_callback_value *self=input(in,"self"),*point=input(in,"point"),
        *normal=input(in,"normal"),*amount=input(in,"amount"),*flags=input(in,"damage-flags");
    if(!self||!point||!normal||!amount||!flags||self->kind!=APPLICATION_NATIVE_VALUE_ACTOR||
        point->kind!=APPLICATION_NATIVE_VALUE_VECTOR||normal->kind!=APPLICATION_NATIVE_VALUE_VECTOR||
        amount->kind!=APPLICATION_NATIVE_VALUE_NUMBER||flags->kind!=APPLICATION_NATIVE_VALUE_NUMBER)
        return fail(e,"Native armor check omitted its authentic stage inputs");
    qa_buffer record={0};
    if(!text(o->document,qa_json_get(o->document,qa_json_root(o->document),"entityRecord"),&record,e)) return false;
    values[0]=(qa_native_value){.type=QA_NATIVE_ADDRESS};
    bool ok=application_native_q2_callbacks_record(o,self->value.actor,(char *)record.data,&values[0].as.address,e);
    qa_buffer_free(&record);
    qa_vec3 vectors[]={point->value.vector,normal->value.vector};
    for(size_t i=0;ok&&i<2;++i) {
        uint8_t bytes[12]; float axes[]={vectors[i].x,vectors[i].y,vectors[i].z};
        if(!qa_vec_finite(vectors[i])) return fail(e,"Native armor geometry exceeds its source vector");
        for(size_t j=0;j<3;++j) encoded((qa_native_value){.type=QA_NATIVE_F32,.as.f32=axes[j]},bytes+j*4);
        values[i+1]=(qa_native_value){.type=QA_NATIVE_ADDRESS};
        ok=temporary(o,(qa_bytes){bytes,sizeof(bytes)},sizeof(bytes),allocations,&values[i+1].as.address,e);
    }
    if(ok) ok=number_value(QA_NATIVE_I32,amount->value.number,values+3,e);
    if(ok&&count==6) {
        double sparks;
        ok=qa_json_number(o->document,qa_json_get(o->document,absorb,"sparks"),&sparks,e)&&
            number_value(QA_NATIVE_I32,sparks,values+4,e);
    }
    return ok&&number_value(QA_NATIVE_I32,flags->value.number,values+count-1,e);
}
static bool cleanup(application_native_q2_callbacks *o,native_global **globals,native_temporary **allocations,qa_error *e)
{
    bool ok=true; qa_error first={0};
    while(*globals) {
        native_global *g=*globals; qa_error fault={0};
        if(!qa_native_write(instance(o),g->address,(qa_bytes){g->bytes,g->size},&fault)) {
            if(ok) first=fault;
            ok=false; break;
        }
        *globals=g->next; free(g);
    }
    while(!*globals&&*allocations) {
        native_temporary *t=*allocations; qa_error fault={0};
        if(!qa_native_free(instance(o),t->address,&fault)) {
            if(ok) first=fault;
            ok=false; break;
        }
        *allocations=t->next; free(t);
    }
    if(!ok&&e) *e=first;
    return ok;
}
static bool projection_end(application_native_q2_callbacks *o,qa_error *e)
{
    if(!application_native_q2_pickups_drain(o->pickups,e)) return false;
    if(o->pending_regions||!application_native_q2_protection_idle(o->protection))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native transfer retains its source region or protection observation");
    while(o->scopes&&o->scopes->completed) {
        native_projection_scope *s=o->scopes;
        if(s->scope&&!application_native_q2_records_end(o->records,&s->scope,s->succeeded,e)) return false;
        o->scopes=s->outer; free(s);
    }
    return true;
}
static bool region_current(void *context,qa_error *e)
{
    native_region *r=context;
    return r->retained&&
        application_native_q2_source_invocation_guard(r->owner->engine,&r->authority,&r->retirement,e)&&
        current(r->owner,e);
}
static bool region_close(native_region **owned,qa_error *e)
{
    native_region *r=*owned;
    if(!r) return true;
    if(!application_native_q2_callback_region_close(&r->scope,e)) return false;
    if(r->retained) r->authority.release(r->authority.context);
    free(r); *owned=NULL;
    return true;
}
static bool regions_close(application_native_q2_callbacks *o,qa_error *e)
{
    while(o->pending_regions) {
        native_region *r=o->pending_regions,*next=r->next;
        if(!region_close(&r,e)) return false;
        o->pending_regions=next;
    }
    return true;
}
static bool call_cancel(void *context,const qa_error *e)
{
    native_call *call=context;
    return application_native_q2_source_invocation_accepts(call->source,e)||
        application_native_q2_items_receipt_accepts_error(call->items,e);
}
static bool call_close(native_call **owned,qa_error *e)
{
    native_call *call=*owned;
    if(!call) return true;
    if(!qa_native_call_scope_close(&call->scope,e)||
        !application_native_q2_source_invocation_close(&call->source,e)) return false;
    application_native_q2_items_receipt_end(&call->items);
    free(call); *owned=NULL; return true;
}
static bool calls_close(application_native_q2_callbacks *o,qa_error *e)
{
    while(o->pending_calls) {
        native_call *call=o->pending_calls,*next=call->next;
        if(call->scope&&!qa_native_call_scope_abandon(call->scope,e)) return false;
        if(!call_close(&call,e)) return false;
        o->pending_calls=next;
    }
    return true;
}
static bool call_native_common(application_native_q2_callbacks *o,qa_json_id call,
    const application_native_callback_inputs *in,double *returned,bool transfer,bool *entered,
    bool armor_check,qa_json_id region,const application_native_q2_source_authority *authority,qa_error *e)
{
    if(entered) *entered=false;
    if(!returned||!current(o,e)||o->pending||o->pending_globals||o->pending_regions||o->pending_calls) return false;
    if(transfer&&o->scopes&&o->scopes->completed)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callback retains an unfinished canonical projection transfer");
    const qa_json_document *d=o->document;
    qa_json_id arguments=qa_json_get(d,call,"arguments"),globals=qa_json_get(d,call,"globals");
    if((!armor_check&&qa_json_type(d,arguments)!=QA_JSON_ARRAY)||
        (qa_json_type(d,globals)!=QA_JSON_ARRAY&&!(armor_check&&globals==QA_JSON_NONE)))
        return fail(e,"Native call has no argument/global roster");
    size_t count=armor_check?(qa_json_string_equal(d,qa_json_get(d,call,"abi"),"q2-check-armor")?6u:5u):qa_json_size(d,arguments);
    if(count>64) return fail(e,"Native call exceeds the admitted ABI argument limit");
    qa_native_type types[64]; qa_native_value values[64],result={0};
    native_temporary *allocations=NULL; native_global *saved=NULL; native_userinfo *corrections=NULL;
    qa_native_value *region_values=NULL;
    native_region *region_scope=NULL;
    native_call *processor=NULL;
    qa_native_address entry; qa_native_value_type returns;
    if(armor_check) returns=QA_NATIVE_I32;
    else scalar_type(d,qa_json_get(d,call,"returns"),&returns);
    if(returns==QA_NATIVE_BYTES) return fail(e,"Native callback return has no scalar ABI");
    ++o->calls; ++o->engine->calls;
    bool ok=true;
    const application_native_callback_value *self=input(in,"self");
    bool item_cancellation=self&&self->kind==APPLICATION_NATIVE_VALUE_ACTOR&&
        application_native_q2_items_actor_admitted(o->items,self->value.actor);
    struct application_native_q2_source_invocation *source=NULL;
    if(ok)ok=application_native_q2_source_invocation_begin(o->engine,
        self&&self->kind==APPLICATION_NATIVE_VALUE_ACTOR?self->value.actor:(qa_actor_id){0},&source,e);
    if(ok&&(source||item_cancellation)) {
        processor=calloc(1,sizeof(*processor));
        if(!processor)ok=application_fail(e,QA_ERROR_MEMORY,"Retaining native call cancellation");
        if(processor) {processor->source=source;source=NULL;}
        if(ok&&item_cancellation)ok=application_native_q2_items_receipt_begin(o->items,self->value.actor,&processor->items,e);
        if(ok)ok=qa_native_call_scope_open(instance(o),call_cancel,processor,&processor->scope,e);
    }
    if(source) {
        qa_error cleanup={0};
        if(!application_native_q2_source_invocation_close(&source,&cleanup)&&ok) {ok=false;if(e)*e=cleanup;}
    }
    if(ok&&region!=QA_JSON_NONE) {
        if(!authority||!authority->current||!authority->retain||!authority->release)
            ok=application_fail(e,QA_ERROR_ARGUMENT,"Native region has no retained protection authority");
        if(ok) {
            region_scope=calloc(1,sizeof(*region_scope));
            if(!region_scope)ok=application_fail(e,QA_ERROR_MEMORY,"Retaining actual native region protection scope");
        }
        if(ok) {
            region_scope->owner=o;region_scope->authority=*authority;
            if(o->engine->source_retirement_sequence==SIZE_MAX)
                ok=application_fail(e,QA_ERROR_ARGUMENT,"Native Source region exhausted its exact authority errors");
            if(ok)qa_error_set(&region_scope->retirement,QA_ERROR_NOT_FOUND,++o->engine->source_retirement_sequence,
                "Native Source region retired its retained protection authority");
            if(ok)ok=authority->retain(authority->context,e);
            region_scope->retained=ok;
            if(ok)ok=region_current(region_scope,e);
        }
    }
    if(ok&&transfer)ok=records_prepare(o,e)&&application_native_q2_records_commit(o->records,e);
    if(ok) ok=target(o,call,&entry,e);
    if(ok&&armor_check) ok=protection_arguments(o,call,in,values,count,&allocations,e);
    for(size_t i=0;ok&&i<count;++i) {
        size_t storage;
        if(!armor_check) ok=lower(o,qa_json_at(d,arguments,i),in,values+i,&storage,&allocations,&corrections,e);
        if(ok) types[i]=(qa_native_type){.kind=values[i].type,.count=1};
    }
    size_t region_count=region==QA_JSON_NONE?0:qa_json_size(d,qa_json_get(d,region,"inputs"));
    if(ok&&region!=QA_JSON_NONE) {
        ok=region_current(region_scope,e)&&
            application_native_q2_callback_region_validate(instance(o),o->declaration,d,region,e);
        if(ok&&region_count) {
            region_values=calloc(region_count,sizeof(*region_values));
            if(!region_values) ok=application_fail(e,QA_ERROR_MEMORY,"Lowering original native region inputs");
        }
        for(size_t i=0;ok&&i<region_count;++i) {
            size_t storage;
            ok=lower(o,qa_json_get(d,qa_json_at(d,qa_json_get(d,region,"inputs"),i),"value"),
                in,region_values+i,&storage,&allocations,&corrections,e);
        }

    }
    qa_native_signature signature={.abi=o->target.abi,.parameters=types,.parameter_count=count,
        .result={.kind=returns,.count=1}};
    if(ok&&region!=QA_JSON_NONE) ok=application_native_q2_callback_region_call_validate(
        instance(o),o->declaration,d,region,&signature,e);
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
    native_projection_scope *scope=NULL;
    if(ok&&transfer) {
        ok=records_prepare(o,e);
        if(ok) {
            scope=calloc(1,sizeof(*scope));
            if(!scope) ok=application_fail(e,QA_ERROR_MEMORY,"Retaining actual native callback transfer cleanup");
        }
        if(ok) {
            scope->outer=o->scopes; o->scopes=scope;
            ok=application_native_q2_records_begin(o->records,&scope->scope,e);
        }
    }
    bool dispatched=false;
    qa_json_id previous_call=o->active_call;
    native_region *previous_region=o->active_region;
    if(ok) {
        o->active_call=call;
        if(region_scope) o->active_region=region_scope;
        application_native_q2_visibility_invalidate(o->engine);
        if(region_scope)ok=region_current(region_scope,e);
        if(ok)ok=(region!=QA_JSON_NONE?
            application_native_q2_callback_region_execute(instance(o),o->declaration,d,region,entry,
                &signature,values,count,region_values,region_count,region_current,region_scope,
                &region_scope->scope,&result,&dispatched,e):
            qa_native_invoke_receipt(instance(o),entry,&signature,values,count,
                returns==QA_NATIVE_VOID?NULL:&result,&dispatched,e))&&current(o,e);
    }
    if(region_scope)application_native_q2_source_invocation_unguard(o->engine,
        &region_scope->retirement,ok?NULL:e);
    o->active_call=previous_call;
    o->active_region=previous_region;
    bool cancelled=false;
    if(processor&&processor->scope&&!o->pending_regions&&!o->pending_calls) {
        bool resolved=qa_native_call_scope_resolve(processor->scope,ok,&cancelled,e);
        if(resolved&&cancelled) { ok=true; result=(qa_native_value){.type=QA_NATIVE_I32,.as.i32=0}; }
        else if(!resolved) ok=false;
    }
    free(region_values);
    if(entered) *entered=dispatched;
    qa_error region_error={0}; bool region_closed=region_close(&region_scope,&region_error);
    if(!region_closed) {
        native_region **tail=&o->pending_regions;
        while(*tail) tail=&(*tail)->next;
        *tail=region_scope;
    }
    if(!region_closed&&ok) { ok=false; if(e) *e=region_error; }
    for(native_userinfo *c=corrections;ok&&!cancelled&&c;c=c->next) {
        application_native_q2_client *client=o->engine->clients+c->slot;
        qa_buffer after={0};
        ok=qa_actor_id_equal(client->actor,c->actor)&&qa_actors_get(qa_session_actors(o->engine->provider->application->session),c->actor)&&
            returned_userinfo(o,c,&after,e)&&after.size<sizeof(client->userinfo);
        if(ok) { memcpy(client->userinfo,after.data,after.size); client->userinfo[after.size]=0; client->userinfo_present=true; }
        qa_buffer_free(&after);
    }
    while(corrections) { native_userinfo *c=corrections; corrections=c->next; free(c); }
    if(scope) { scope->completed=true; scope->succeeded=ok&&!cancelled; }
    qa_error projection_error={0}; bool projected=region_closed&&!o->pending_regions&&(!transfer||projection_end(o,&projection_error));
    if(!projected&&ok) {
        ok=false;
        if(projection_error.code!=QA_OK) { if(e) *e=projection_error; }
        else application_fail(e,QA_ERROR_ARGUMENT,"Native invocation retains an unfinished nested source cleanup");
    }
    if(ok&&projected&&transfer&&!cancelled)
        ok=application_native_q2_client_outputs_publish(o->engine,e);
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
    qa_error cleanup_error={0}; bool cleaned=projected&&cleanup(o,&saved,&allocations,&cleanup_error);
    if(!cleaned) {
        o->pending_globals=saved; o->pending=allocations;
        if(ok&&e) *e=cleanup_error;
    }
    qa_error processor_error={0}; bool processor_closed=cleaned&&call_close(&processor,&processor_error);
    if(!processor_closed&&processor) {
        native_call **tail=&o->pending_calls;
        while(*tail) tail=&(*tail)->next;
        *tail=processor;
        if(ok) {
            ok=false;
            if(processor_error.code!=QA_OK) { if(e) *e=processor_error; }
        }
    }
    --o->engine->calls; --o->calls;
    if(ok&&cleaned&&processor_closed) {
        *returned=result_number(result);
        if((result.type==QA_NATIVE_I64||result.type==QA_NATIVE_U64)&&fabs(*returned)>9007199254740991.0)
            return fail(e,"Native integer return exceeds the source safe-number representation");
        return isfinite(*returned)||fail(e,"Native callback returned nonfinite data");
    }
    return false;
}
static bool call_native(application_native_q2_callbacks *o,qa_json_id call,
    const application_native_callback_inputs *in,double *returned,bool transfer,bool *entered,qa_error *e)
{ return call_native_common(o,call,in,returned,transfer,entered,false,QA_JSON_NONE,NULL,e); }
bool application_native_q2_callbacks_call(application_native_q2_callbacks *o,qa_json_id call,
    const application_native_callback_inputs *in,double *returned,qa_error *e)
{ return call_native(o,call,in,returned,true,NULL,e); }
bool application_native_q2_callbacks_call_scoped(application_native_q2_callbacks *o,qa_json_id call,
    const application_native_callback_inputs *in,double *returned,qa_error *e)
{
    if(!o||!o->scopes||o->scopes->completed||!o->scopes->scope)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native source stage requires its actual surrounding transfer");
    return call_native(o,call,in,returned,false,NULL,e);
}
bool application_native_q2_callbacks_time_read(application_native_q2_callbacks *o,double *out,qa_error *e)
{
    if(!out||!current(o,e)) return false;
    qa_source_frame frame;
    if(!application_native_q2_stages_time_read(o->engine,&frame,e)||!current(o,e)) return false;
    *out=(double)frame.time_ns/1000000000.0; return true;
}
static bool protection_scale_input(const qa_json_document *d,qa_json_id value)
{
    return (qa_json_string_equal(d,qa_json_get(d,value,"kind"),"float32")||
        qa_json_string_equal(d,qa_json_get(d,value,"kind"),"float64"))&&
        qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,value,"value"),"kind"),"input")&&
        qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,value,"value"),"name"),"regular-protection-scale");
}
bool application_native_q2_callbacks_protection_absorb(application_native_q2_callbacks *o,qa_json_id definition,
    const qa_damage_request *request,const qa_damage_geometry *geometry,float amount,
    qa_damage_flags flags,const application_native_q2_source_authority *authority,float *saved,qa_error *e)
{
    if(!request||!geometry||!saved||!isfinite(amount)||!isfinite(flags.regular_scale)||
        !o||!authority||!authority->current||
        !o->scopes||o->scopes->completed||!o->scopes->scope)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native protection absorption requires its actual client transfer");
    if(!application_native_q2_callbacks_client_current(o,request->target,e)||
        !authority->current(authority->context,e)) return false;
    const qa_json_document *d=o->document;
    qa_json_id definitions=qa_json_get(d,qa_json_root(d),"protection"); bool found=false;
    for(size_t i=0;i<qa_json_size(d,definitions);++i) if(qa_json_at(d,definitions,i)==definition) found=true;
    if(!found) return fail(e,"Native protection rule is outside its acquired declaration");
    qa_json_id absorb=qa_json_get(d,definition,"absorb"),abi=qa_json_get(d,absorb,"abi");
    bool regular=qa_json_string_equal(d,qa_json_get(d,definition,"channel"),"regular"),
        source=qa_json_string_equal(d,abi,"source-call"),region=qa_json_string_equal(d,abi,"source-region"),
        check=qa_json_string_equal(d,abi,"q2-check-armor")||qa_json_string_equal(d,abi,"q2-check-power-armor");
    if(!source&&!region&&!check) return fail(e,"Native protection has no declared source absorption ABI");
    qa_json_id call=source||region?qa_json_get(d,absorb,"call"):absorb;
    if(regular&&flags.regular_scale!=1) {
        bool scale=false;
        qa_json_id arguments=qa_json_get(d,call,"arguments"),globals=qa_json_get(d,call,"globals"),inputs=qa_json_get(d,absorb,"inputs");
        for(size_t i=0;i<qa_json_size(d,arguments);++i) scale|=protection_scale_input(d,qa_json_at(d,arguments,i));
        for(size_t i=0;i<qa_json_size(d,globals);++i) scale|=protection_scale_input(d,qa_json_get(d,qa_json_at(d,globals,i),"value"));
        if(region) for(size_t i=0;i<qa_json_size(d,inputs);++i) scale|=protection_scale_input(d,qa_json_get(d,qa_json_at(d,inputs,i),"value"));
        if(!scale) return fail(e,"Native regular protection scale requires an authored floating source input");
    }
    bool classic=o->engine->profile==QA_NATIVE_Q2_GAME_API3;
    uint32_t damage=(request->radius?1u:0u)|
        ((flags.no_armor||(classic&&(regular?flags.no_regular_armor:flags.no_power_armor)))?2u:0u)|
        (flags.energy?4u:0u)|(flags.no_regular_armor?128u:0u)|(!classic&&flags.no_power_armor?256u:0u);
    double time;
    if(!application_native_q2_callbacks_time_read(o,&time,e)) return false;
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=request->target},
        {.name="attacker",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=request->attack.attacker},
        {.name="inflictor",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=request->attack.inflictor},
        {.name="amount",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=amount},
        {.name="damage-flags",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=damage},
        {.name="regular-protection-scale",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=flags.regular_scale},
        {.name="point",.kind=APPLICATION_NATIVE_VALUE_VECTOR,.value.vector=geometry->point},
        {.name="normal",.kind=APPLICATION_NATIVE_VALUE_VECTOR,.value.vector=geometry->normal},
        {.name="direction",.kind=APPLICATION_NATIVE_VALUE_VECTOR,.value.vector=geometry->direction},
        {.name="knockback",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=request->knockback},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=time}};
    application_native_callback_inputs inputs={values,sizeof(values)/sizeof(*values),{0}};
    double result;
    if(!call_native_common(o,call,&inputs,&result,false,NULL,check,region?absorb:QA_JSON_NONE,authority,e)) return false;
    float narrowed=(float)result;
    if(!isfinite(narrowed)) return fail(e,"Native protection result exceeds its canonical stage amount");
    *saved=narrowed; return true;
}
bool application_native_q2_callbacks_transfer_current(const application_native_q2_callbacks *o)
{return o&&o->scopes&&!o->scopes->completed&&o->scopes->scope;}
bool application_native_q2_callbacks_transfer(application_native_q2_callbacks *o,
    bool (*execute)(void *,qa_error *),void *context,qa_error *e)
{
    if(!execute||!current(o,e)||o->pending||o->pending_globals||o->pending_regions||o->pending_calls||
        (o->scopes&&o->scopes->completed)||!records_prepare(o,e)) return false;
    native_projection_scope *scope=calloc(1,sizeof(*scope));
    if(!scope) return application_fail(e,QA_ERROR_MEMORY,"Retaining native source stage transfer");
    ++o->calls; ++o->engine->calls;
    bool ok=application_native_q2_records_begin(o->records,&scope->scope,e);
    if(ok) {
        scope->outer=o->scopes; o->scopes=scope;
        ok=execute(context,e);
        scope->completed=true; scope->succeeded=ok;
        qa_error close={0}; bool closed=projection_end(o,&close);
        if(!closed&&ok) { ok=false; if(e) *e=close; }
        if(ok&&closed) ok=application_native_q2_client_outputs_publish(o->engine,e);
    } else free(scope);
    --o->engine->calls; --o->calls;
    return ok;
}
bool application_native_q2_callbacks_entry(application_native_q2_callbacks *o,qa_json_id entry,
    qa_native_address *address,qa_error *e)
{
    if(!address||!current(o,e)) return false;
    const qa_json_document *d=o->document; qa_json_id kind=qa_json_get(d,entry,"kind");
    if(qa_json_string_equal(d,kind,"rva")) {
        uint32_t rva; return word(d,entry,"rva",&rva,e)&&qa_native_rva(instance(o),rva,1,address,e);
    }
    qa_buffer name={0};
    if(!text(d,qa_json_get(d,entry,"name"),&name,e)) return false;
    bool ok=qa_json_string_equal(d,kind,"export")?qa_native_export(instance(o),(char *)name.data,address,e):
        qa_json_string_equal(d,kind,"game-export")?qa_native_entry_address(instance(o),(char *)name.data,address,e):
        fail(e,"Native stage has an unknown declared entry");
    qa_buffer_free(&name); return ok;
}
bool application_native_q2_callbacks_entry_call(application_native_q2_callbacks *o,qa_json_id entry,
    qa_json_id returns,const qa_native_value *arguments,size_t count,bool *entered,qa_error *e)
{
    if(!entered) return fail(e,"Native stage requires its actual dispatch receipt");
    *entered=false;
    if(count>64||!current(o,e)||o->pending||o->pending_globals||o->pending_regions||o->pending_calls||o->scopes) return false;
    qa_native_value_type result_type=QA_NATIVE_VOID;
    if(returns!=QA_JSON_NONE) scalar_type(o->document,returns,&result_type);
    if(result_type==QA_NATIVE_BYTES) return fail(e,"Native stage has no scalar result ABI");
    qa_native_address address;
    if(!application_native_q2_callbacks_entry(o,entry,&address,e)||!records_prepare(o,e)||
        !application_native_q2_records_commit(o->records,e)) return false;
    native_projection_scope *scope=calloc(1,sizeof(*scope));
    if(!scope) return application_fail(e,QA_ERROR_MEMORY,"Retaining native stage transfer");
    scope->outer=o->scopes; o->scopes=scope;
    ++o->calls; ++o->engine->calls;
    bool ok=application_native_q2_records_begin(o->records,&scope->scope,e);
    qa_native_type parameters[64]; qa_native_value result={0};
    for(size_t i=0;i<count;++i) parameters[i]=(qa_native_type){.kind=arguments[i].type,.count=1};
    qa_native_signature signature={.abi=o->target.abi,.parameters=parameters,.parameter_count=count,
        .result={.kind=result_type,.count=1}};
    qa_json_id previous_call=o->active_call;
    o->active_call=QA_JSON_NONE;
    if(ok) {
        application_native_q2_visibility_invalidate(o->engine);
        ok=qa_native_invoke_receipt(instance(o),address,&signature,arguments,count,
            result_type==QA_NATIVE_VOID?NULL:&result,entered,e)&&current(o,e);
    }
    o->active_call=previous_call;
    scope->completed=true; scope->succeeded=ok;
    qa_error cleanup={0}; bool closed=projection_end(o,&cleanup);
    if(ok&&closed) ok=application_native_q2_client_outputs_publish(o->engine,e);
    --o->engine->calls; --o->calls;
    if(ok&&!closed&&e) *e=cleanup;
    return ok&&closed;
}
bool application_native_q2_callbacks_storage_transfer(application_native_q2_callbacks *o,
    bool (*execute)(void *,qa_error *),void *context,qa_error *e)
{
    if(!execute||!current(o,e)||o->pending||o->pending_globals||o->pending_regions||o->pending_calls) return false;
    if(!o->scopes) return application_native_q2_callbacks_transfer(o,execute,context,e);
    if(!o->calls||o->scopes->completed||!o->scopes->scope)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native storage write has no entered record transfer");
    return application_native_q2_records_commit(o->records,e)&&
        application_native_q2_records_refresh(o->records,e)&&execute(context,e)&&current(o,e)&&
        application_native_q2_records_commit(o->records,e)&&application_native_q2_client_outputs_publish(o->engine,e);
}
bool application_native_q2_callbacks_input_write(application_native_q2_callbacks *o,qa_json_id value,
    const application_native_callback_inputs *inputs,qa_native_address address,qa_error *e)
{
    if(!current(o,e)||o->pending||o->pending_globals||o->pending_calls) return false;
    qa_native_value lowered={0}; size_t size=0; native_temporary *allocations=NULL;
    native_userinfo *corrections=NULL; native_global *globals=NULL;
    bool ok=lower(o,value,inputs,&lowered,&size,&allocations,&corrections,e);
    uint8_t bytes[12]={0};
    if(ok&&size>sizeof(bytes)) ok=fail(e,"Native input field exceeds its declared scalar/vector storage");
    if(ok&&qa_json_string_equal(o->document,qa_json_get(o->document,value,"kind"),"vector"))
        ok=qa_native_read(instance(o),lowered.as.address,bytes,size,e);
    else if(ok) encoded(lowered,bytes);
    if(ok) ok=qa_native_write(instance(o),address,(qa_bytes){bytes,size},e);
    while(corrections) { native_userinfo *next=corrections->next; free(corrections); corrections=next; }
    qa_error cleanup_error={0}; bool cleaned=cleanup(o,&globals,&allocations,&cleanup_error);
    if(!cleaned) { o->pending=allocations; if(ok&&e) *e=cleanup_error; }
    return ok&&cleaned;
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
        if(!call_native(o,call,in,&value,strcmp(section,"initialize")!=0,NULL,e)) return false;
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
    o->active_call=QA_JSON_NONE;
    o->target=qa_native_module_describe(o->module).image.target;
    return qa_json_parse(bytes,&o->document,e);
}
static bool address_shape(application_native_q2_callbacks *o,qa_json_id address,bool nullable,qa_error *e)
{
    const qa_json_document *d=o->document;
    if(nullable&&qa_json_type(d,address)==QA_JSON_NULL) return true;
    uint32_t rva;
    qa_json_id list=qa_json_get(d,address,"indirections");
    if(qa_json_type(d,address)!=QA_JSON_OBJECT||!word(d,address,"rva",&rva,e)||
        qa_json_type(d,list)!=QA_JSON_ARRAY) return fail(e,"Native address requires its acquired RVA and indirection roster");
    for(size_t i=0;i<qa_json_size(d,list);++i) {
        uint64_t offset;
        if(!qa_json_u64(d,qa_json_at(d,list,i),&offset,e)||offset>UINT32_MAX)
            return fail(e,"Native address indirection exceeds its source word");
    }
    return true;
}
static bool value_shape(application_native_q2_callbacks *o,qa_json_id value,qa_native_value_type *type,qa_error *e)
{
    const qa_json_document *d=o->document;
    qa_json_id kind=qa_json_get(d,value,"kind"),raw=qa_json_get(d,value,"value");
    if(scalar_type(d,kind,type)) {
        if(qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"float")) {
            double number; qa_native_value checked;
            return qa_json_number(d,qa_json_get(d,raw,"value"),&number,e)&&number_value(*type,number,&checked,e);
        }
        if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"input")) return fail(e,"Native scalar has no literal or source input");
    } else if(qa_json_string_equal(d,kind,"time")) {
        if(!scalar_type(d,qa_json_get(d,value,"encoding"),type)||
            (!qa_json_string_equal(d,qa_json_get(d,value,"input"),"time")&&!qa_json_string_equal(d,qa_json_get(d,value,"input"),"elapsed"))||
            (!qa_json_string_equal(d,qa_json_get(d,value,"units"),"seconds")&&!qa_json_string_equal(d,qa_json_get(d,value,"units"),"milliseconds")))
            return fail(e,"Native time has an invalid source encoding or units");
        return true;
    } else if(qa_json_string_equal(d,kind,"address")) {
        *type=QA_NATIVE_ADDRESS; return address_shape(o,raw,true,e);
    } else if(qa_json_string_equal(d,kind,"user-command")) { *type=QA_NATIVE_ADDRESS; return true; }
    else if(qa_json_string_equal(d,kind,"actor")||qa_json_string_equal(d,kind,"client")||qa_json_string_equal(d,kind,"userinfo")) {
        *type=qa_json_string_equal(d,kind,"client")?QA_NATIVE_I32:QA_NATIVE_ADDRESS;
        qa_json_id input_name=qa_json_get(d,value,"input");
        if(!qa_json_string_equal(d,input_name,"self")&&!qa_json_string_equal(d,input_name,"other")&&
            !qa_json_string_equal(d,input_name,"activator")&&!qa_json_string_equal(d,input_name,"attacker")&&
            !qa_json_string_equal(d,input_name,"inflictor")) return fail(e,"Native actor value names an unknown canonical actor input");
        if(qa_json_string_equal(d,kind,"actor")) {
            qa_buffer name={0};
            if(!text(d,qa_json_get(d,value,"record"),&name,e)) return false;
            bool found=record_find(o,(char *)name.data)!=QA_JSON_NONE; qa_buffer_free(&name);
            if(!found) return fail(e,"Native actor value names an undeclared source record");
        } else if(qa_json_get(d,qa_json_root(d),"clients")==QA_JSON_NONE)
            return fail(e,"Native client value requires its declared physical clients");
        return true;
    } else if(qa_json_string_equal(d,kind,"vector")||qa_json_string_equal(d,kind,"string")) {
        *type=QA_NATIVE_ADDRESS;
        if(!qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"input")) {
            if(qa_json_string_equal(d,kind,"string")) {
                qa_buffer string={0};
                bool valid=qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"string")&&text(d,qa_json_get(d,raw,"value"),&string,e);
                qa_buffer_free(&string); return valid;
            }
            double x,y,z; qa_json_id vector=qa_json_get(d,raw,"value");
            return qa_json_string_equal(d,qa_json_get(d,raw,"kind"),"vector")&&
                qa_json_number(d,qa_json_get(d,vector,"x"),&x,e)&&qa_json_number(d,qa_json_get(d,vector,"y"),&y,e)&&
                qa_json_number(d,qa_json_get(d,vector,"z"),&z,e)&&qa_vec_finite(qa_v3((float)x,(float)y,(float)z));
        }
    } else return fail(e,"Native value has no admitted scalar or pointer ABI");
    qa_buffer input_name={0};
    bool valid=text(d,qa_json_get(d,raw,"name"),&input_name,e)&&input_name.size;
    qa_buffer_free(&input_name);
    return valid||fail(e,"Native callback value omitted its source input name");
}
bool application_native_q2_callbacks_value_validate(application_native_q2_callbacks *o,qa_json_id value,
    qa_native_value_type *type,qa_error *e)
{
    return type&&current(o,e)&&value_shape(o,value,type,e);
}
static bool record_field_size(application_native_q2_callbacks *o,qa_json_id field,uint32_t *bytes,qa_error *e)
{
    const qa_json_document *d=o->document; qa_json_id binding=qa_json_get(d,field,"binding");
    if(qa_json_string_equal(d,binding,"private")) return word(d,field,"byteLength",bytes,e)&&*bytes;
    if(qa_json_string_equal(d,binding,"record")||qa_json_string_equal(d,binding,"address")) { *bytes=o->target.pointer_bytes; return true; }
    if(qa_json_string_equal(d,binding,"origin")||qa_json_string_equal(d,binding,"velocity")||qa_json_string_equal(d,binding,"angles")||
        qa_json_string_equal(d,binding,"bounds-min")||qa_json_string_equal(d,binding,"bounds-max")||qa_json_string_equal(d,binding,"constant-vector")) { *bytes=12; return true; }
    if(qa_json_string_equal(d,binding,"health")||qa_json_string_equal(d,binding,"inventory")||qa_json_string_equal(d,binding,"inventory-capacity")||
        qa_json_string_equal(d,binding,"team")||qa_json_string_equal(d,binding,"score")||qa_json_string_equal(d,binding,"constant")) {
        qa_native_value_type type; *bytes=(uint32_t)scalar_type(d,qa_json_get(d,field,"encoding"),&type);
        return *bytes||fail(e,"Native record scalar has no source encoding");
    }
    return fail(e,"Native record field has no declared semantic binding");
}
static bool records_validate(application_native_q2_callbacks *o,qa_error *e)
{
    const qa_json_document *d=o->document; qa_json_id root=qa_json_root(d),rows=qa_json_get(d,root,"actorRecords");
    qa_json_id clients=qa_json_get(d,root,"clients"); uint32_t maximum=0;
    if(clients!=QA_JSON_NONE&&(!word(d,clients,"maximum",&maximum,e)||!maximum||maximum>256))
        return fail(e,"Native callback client capacity is invalid");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i),base=qa_json_get(d,row,"base"),fields=qa_json_get(d,row,"fields");
        uint32_t stride,capacity,first; qa_buffer name={0};
        if(!text(d,qa_json_get(d,row,"id"),&name,e)) return false;
        bool valid=name.size&&record_find(o,(char *)name.data)==row;
        qa_buffer_free(&name);
        if(!valid||!word(d,row,"stride",&stride,e)||stride<4||!word(d,row,"capacity",&capacity,e)||
            !capacity||capacity>65536||capacity<maximum||!word(d,row,"firstSlot",&first,e)||qa_json_type(d,fields)!=QA_JSON_ARRAY)
            return fail(e,"Native record identity or source extent is invalid");
        qa_json_id base_kind=qa_json_get(d,base,"kind");
        if(qa_json_string_equal(d,base_kind,"address")) { if(!address_shape(o,base,false,e)) return false; }
        else if(!qa_json_string_equal(d,base_kind,"entities")&&!qa_json_string_equal(d,base_kind,"clients"))
            return fail(e,"Native record base has no genuine source owner");
        if(qa_json_string_equal(d,base_kind,"clients")&&(clients==QA_JSON_NONE||first||capacity!=maximum))
            return fail(e,"Native client record differs from its reserved source slots");
        for(size_t j=0;j<qa_json_size(d,fields);++j) {
            qa_json_id field=qa_json_at(d,fields,j); uint32_t offset,bytes;
            if(!word(d,field,"offset",&offset,e)||!record_field_size(o,field,&bytes,e)||offset>stride||bytes>stride-offset)
                return fail(e,"Native field exceeds its declared record extent");
            for(size_t k=0;k<j;++k) {
                uint32_t earlier,length; qa_json_id old=qa_json_at(d,fields,k);
                if(!word(d,old,"offset",&earlier,e)||!record_field_size(o,old,&length,e)) return false;
                if(offset<earlier+length&&earlier<offset+bytes) return fail(e,"Native record fields overlap");
            }
            qa_json_id binding=qa_json_get(d,field,"binding");
            if(qa_json_string_equal(d,binding,"address")&&!address_shape(o,qa_json_get(d,field,"value"),true,e)) return false;
            if(qa_json_string_equal(d,binding,"record")) {
                qa_buffer linked={0}; if(!text(d,qa_json_get(d,field,"record"),&linked,e)) return false;
                bool exists=record_find(o,(char *)linked.data)!=QA_JSON_NONE; qa_buffer_free(&linked);
                if(!exists) return fail(e,"Native record pointer names an undeclared record");
            }
        }
    }
    return true;
}
static bool skip_region(void *context,qa_native_instance *native,const qa_native_region_event *event,
    qa_native_region_decision *decision,qa_error *e)
{
    native_skip *skip=context;application_native_q2_callbacks *o=skip->owner;
    if(!current(o,e)||native!=instance(o)||event->region.id!=skip->region) return false;
    if(event->phase==QA_NATIVE_REGION_ENTER&&o->active_call==skip->call&&o->calls)
        decision->action=QA_NATIVE_REGION_SKIP_TO_JOIN;
    return true;
}
static bool prepare_skips(application_native_q2_callbacks *o,qa_json_id call,qa_error *e)
{
    const qa_json_document *d=o->document;qa_json_id rows=qa_json_get(d,call,"skips");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i);uint32_t entry,join;
        if(!word(d,row,"entry",&entry,e)||!word(d,row,"join",&join,e)) return false;
        qa_native_declared_region region={0};bool found=false;
        for(size_t j=0;j<qa_native_region_count(instance(o));++j) {
            if(!qa_native_region(instance(o),j,&region,e)) return false;
            if(region.entry_rva==entry&&region.join_rva==join) {found=true;break;}
        }
        if(!found) return fail(e,"Native client exclusion has no actual declared instruction region");
        native_skip *skip=o->skips;
        while(skip&&(skip->call!=call||skip->region!=region.id)) skip=skip->next;
        if(!skip) {
            skip=calloc(1,sizeof(*skip));if(!skip) return application_fail(e,QA_ERROR_MEMORY,"Retaining native call exclusions");
            skip->owner=o;skip->call=call;skip->region=region.id;skip->next=o->skips;o->skips=skip;
        }
        if(!skip->binding&&!qa_native_bind_region(instance(o),region.id,skip_region,skip,&skip->binding,e)) return false;
    }
    return true;
}
static bool validate_calls(application_native_q2_callbacks *o,qa_json_id value,qa_error *e)
{
    const qa_json_document *d=o->document;
    if(qa_json_type(d,value)==QA_JSON_OBJECT&&
        qa_json_string_equal(d,qa_json_get(d,value,"abi"),"source-region")) {
        qa_json_id call=qa_json_get(d,value,"call"),arguments=qa_json_get(d,call,"arguments");
        size_t count=qa_json_size(d,arguments); qa_native_type types[64]; qa_native_value_type returns;
        scalar_type(d,qa_json_get(d,call,"returns"),&returns);
        if(qa_json_type(d,arguments)!=QA_JSON_ARRAY||count>64||returns==QA_NATIVE_BYTES)
            return fail(e,"Native protection region has no admitted whole-call ABI");
        for(size_t i=0;i<count;++i) {
            qa_native_value_type type;
            if(!value_shape(o,qa_json_at(d,arguments,i),&type,e)) return false;
            types[i]=(qa_native_type){.kind=type,.count=1};
        }
        qa_json_id inputs=qa_json_get(d,value,"inputs");
        for(size_t i=0;i<qa_json_size(d,inputs);++i) {
            qa_native_value_type type;
            if(!value_shape(o,qa_json_get(d,qa_json_at(d,inputs,i),"value"),&type,e)) return false;
        }
        qa_native_signature signature={.abi=o->target.abi,.parameters=types,.parameter_count=count,
            .result={.kind=returns,.count=1}};
        if(!application_native_q2_callback_region_call_validate(instance(o),o->declaration,d,value,&signature,e)) return false;
    }
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
            qa_json_id entry=qa_json_get(d,value,"entry");
            const qa_native_signature *public_signature=NULL;
            qa_buffer entry_name={0};
            if(qa_json_string_equal(d,qa_json_get(d,entry,"kind"),"game-export")) {
                if(!text(d,qa_json_get(d,entry,"name"),&entry_name,e)) return false;
                public_signature=qa_native_entry_signature(instance(o),(char *)entry_name.data);
                qa_buffer_free(&entry_name);
                if(!public_signature||public_signature->abi!=o->target.abi||public_signature->variadic||
                    public_signature->parameter_count!=qa_json_size(d,arguments)||public_signature->result.kind!=returns)
                    return fail(e,"Native declared call differs from its original public game ABI");
            }
            for(size_t i=0;i<qa_json_size(d,arguments);++i) {
                qa_native_value_type type;
                if(!value_shape(o,qa_json_at(d,arguments,i),&type,e)) return false;
                if(public_signature&&(public_signature->parameters[i].kind!=type||
                    public_signature->parameters[i].count!=1||public_signature->parameters[i].field_count))
                    return fail(e,"Native argument differs from the original game export parameter");
            }
            qa_json_id globals=qa_json_get(d,value,"globals");
            for(size_t i=0;i<qa_json_size(d,globals);++i) {
                qa_json_id global=qa_json_at(d,globals,i); qa_native_value_type type;
                if(!address_shape(o,qa_json_get(d,global,"address"),false,e)||
                    !value_shape(o,qa_json_get(d,global,"value"),&type,e)) return false;
            }
            if(!prepare_skips(o,value,e)) return false;
        }
    }
    qa_json_kind kind=qa_json_type(d,value);
    if(kind==QA_JSON_OBJECT||kind==QA_JSON_ARRAY)
        for(size_t i=0;i<qa_json_size(d,value);++i) if(!validate_calls(o,qa_json_at(d,value,i),e)) return false;
    return true;
}
static bool weapon_actor_current(void *context,qa_actor_id actor,qa_error *e)
{
    struct application_native_q2 *n=context;
    return n&&n->callbacks&&application_native_q2_items_actor_current(n->callbacks->items,actor,e);
}
static bool weapon_source_inputs(void *context,qa_actor_id actor,
    application_native_callback_value values[Q3_MOD_VALUE_COUNT],application_native_callback_inputs *inputs,qa_error *e)
{ return application_native_q2_input_values(context,actor,values,inputs,e); }
static bool components_prepare(application_native_q2_callbacks *o,qa_error *e)
{
    struct application_native_q2 *n=o->engine;
    qa_application *app=n->provider->application;
    if(!o->items&&qa_json_size(o->document,qa_json_get(o->document,qa_json_root(o->document),"items"))) {
        application_native_q2_items_options options={.callbacks=o,.session=app->session,
            .inventory=app->inventory,.pickups=app->pickups,.owner=n->provider->owner};
        if(!application_native_q2_items_create(&options,&o->items,e)) return false;
    }
    if(!o->protection&&qa_json_size(o->document,qa_json_get(o->document,qa_json_root(o->document),"protection"))) {
        application_native_q2_protection_options options={o,app->session,app->combat,app->inventory,n->provider->owner};
        if(!application_native_q2_protection_create(&options,&o->protection,e)) return false;
    }
    if(o->items&&!o->weapons&&application_native_q2_items_weapon_stage(o->items)!=QA_JSON_NONE) {
        application_native_q2_weapon_stage_options options={.callbacks=o,.items=o->items,.session=app->session,
            .inventory=app->inventory,.equipment=app->equipment,.owner=n->provider->owner,.context=n,
            .actor_current=weapon_actor_current,.source_inputs=weapon_source_inputs};
        if(!application_native_q2_weapon_stage_create(&options,&o->weapons,e)) return false;
    }
    if(!o->pickups&&qa_json_size(o->document,qa_json_get(o->document,qa_json_root(o->document),"pickups"))) {
        application_native_q2_pickups_options options={.callbacks=o,.protection=o->protection,
            .session=app->session,.pickups=app->pickups,.owner=n->provider->owner};
        if(!application_native_q2_pickups_create(&options,&o->pickups,e)) return false;
    }
    return true;
}
static bool admission_validate(application_native_q2_callbacks *o,qa_error *e)
{
    const qa_json_document *d=o->document;
    qa_json_id clients=qa_json_get(d,qa_json_root(d),"clients"),admit=qa_json_get(d,clients,"admit");
    if(clients==QA_JSON_NONE) return true;
    if(qa_json_type(d,admit)!=QA_JSON_ARRAY) return fail(e,"Native client admission has no declared call array");
    for(size_t i=0;i<qa_json_size(d,admit);++i) {
        qa_json_id call=qa_json_at(d,admit,i),accepts=qa_json_get(d,call,"accepts"),entry=qa_json_get(d,call,"entry");
        bool nonzero=qa_json_string_equal(d,accepts,"nonzero");
        if(!nonzero&&!qa_json_string_equal(d,accepts,"always"))
            return fail(e,"Native client admission has an unknown result policy");
        if(nonzero&&qa_json_string_equal(d,qa_json_get(d,call,"returns"),"void"))
            return fail(e,"Native client admission requires its actual return value");
        if(qa_json_string_equal(d,qa_json_get(d,entry,"kind"),"game-export")&&
            qa_json_string_equal(d,qa_json_get(d,entry,"name"),"ClientConnect")&&!nonzero)
            return fail(e,"Original ClientConnect rejection cannot be ignored");
    }
    return true;
}
bool application_native_q2_callbacks_validate(struct application_native_q2 *n,qa_error *e)
{
    if(!n) return application_fail(e,QA_ERROR_ARGUMENT,"Native callback validation requires its engine");
    if(!n->callbacks) return true;
    application_native_q2_callbacks *o=n->callbacks;
    if(!current(o,e)||!records_validate(o,e)||!admission_validate(o,e)||!validate_calls(o,qa_json_root(o->document),e)||
        !records_prepare(o,e)||!components_prepare(o,e)) return false;
    o->validated=true; return true;
}
bool application_native_q2_callbacks_idle(const application_native_q2_callbacks *o)
{ return !o||(!o->calls&&(!o->scopes||o->scopes->completed)); }
bool application_native_q2_callbacks_current(const application_native_q2_callbacks *o)
{
    return o && o->document && o->validated && !o->calls && !o->pending &&
        !o->pending_globals && !o->pending_regions && !o->pending_calls && !o->scopes && application_native_q2_records_idle(o->records) &&
        !o->components_restoring&&application_native_q2_items_idle(o->items)&&
        application_native_q2_protection_idle(o->protection)&&application_native_q2_weapon_stage_idle(o->weapons)&&
        application_native_q2_pickups_idle(o->pickups)&&
        current((application_native_q2_callbacks *)o,NULL);
}
bool application_native_q2_callbacks_storage_current(application_native_q2_callbacks *o,qa_error *e)
{ return current(o,e); }
bool application_native_q2_callbacks_restoring(const application_native_q2_callbacks *o)
{
    return o&&current((application_native_q2_callbacks *)o,NULL)&&
        (o->components_restoring||application_native_q2_records_restoring(o->records));
}
bool application_native_q2_callbacks_drain(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(n->calls||o->calls||qa_native_active(instance(o)))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callback cleanup retains entered Source execution");
    return regions_close(o,e)&&application_native_q2_source_invocation_drain(n,e)&&
        application_native_q2_pickups_drain(o->pickups,e)&&
        application_native_q2_protection_drain(o->protection,e)&&projection_end(o,e)&&
        cleanup(o,&o->pending_globals,&o->pending,e)&&calls_close(o,e)&&
        application_native_q2_source_invocation_drain(n,e);
}
bool application_native_q2_callbacks_drain_application(qa_application *app,qa_error *e)
{
    if(!app||app->operation!=APPLICATION_IDLE||app->q3_round_active||app->frame_preparing||
        app->client_preparation||(app->session&&!qa_session_safe(app->session))||
        (app->world&&!qa_world_idle(app->world)))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callback cleanup requires a returned application boundary");
    for(application_provider *p=app->live_providers;p;p=p->next_live) {
        struct application_native_q2 *n=p->kind==APPLICATION_PROVIDER_NATIVE?p->state.native.q2_engine:NULL;
        if(n&&n->callbacks&&(n->calls||n->callbacks->calls||qa_native_active(instance(n->callbacks))))
            return application_fail(e,QA_ERROR_ARGUMENT,"Native callback cleanup retains entered Source execution");
    }
    for(application_provider *p=app->live_providers;p;p=p->next_live) {
        struct application_native_q2 *n=p->kind==APPLICATION_PROVIDER_NATIVE?p->state.native.q2_engine:NULL;
        if(n&&!application_native_q2_callbacks_drain(n,e)) return false;
    }
    return true;
}
bool application_native_q2_callbacks_close(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(o->calls) return application_fail(e,QA_ERROR_ARGUMENT,"Native callbacks retain an entered source frame");
    if(!application_native_q2_callbacks_drain(n,e)) return false;
    if(!application_native_q2_callbacks_suspend(n,e)) return false;
    if(!application_native_q2_pickups_destroy(&o->pickups,e)||
        !application_native_q2_weapon_stage_destroy(&o->weapons,e)||
        !application_native_q2_protection_destroy(&o->protection,e)||
        !application_native_q2_items_destroy(&o->items,e)) return false;
    if(!application_native_q2_records_destroy(&o->records,e)) return false;
    while(o->skips) {native_skip *skip=o->skips;o->skips=skip->next;free(skip);}
    qa_buffer_free(&o->restored_weapons);
    free(o->registrations); qa_json_destroy(o->document); free(o); n->callbacks=NULL; return true;
}
const qa_json_document *application_native_q2_callbacks_document(const application_native_q2_callbacks *o)
{ return o?o->document:NULL; }
application_native_q2_records *application_native_q2_callbacks_records(application_native_q2_callbacks *o)
{ return o?o->records:NULL; }
application_native_q2_items *application_native_q2_callbacks_items(application_native_q2_callbacks *o)
{ return o?o->items:NULL; }
bool application_native_q2_callbacks_components_admit(struct application_native_q2 *n,qa_actor_id actor,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    return !o||(application_native_q2_client_outputs_admit(n,actor,e)&&
        (!o->items||application_native_q2_items_admit(o->items,actor,e))&&
        (!o->protection||application_native_q2_protection_bind(o->protection,actor,e))&&
        (!o->pickups||application_native_q2_pickups_bind(o->pickups,actor,e))&&
        (!o->weapons||application_native_q2_weapon_stage_admit(o->weapons,actor,e)));
}
bool application_native_q2_callbacks_components_project(struct application_native_q2 *n,qa_actor_id actor,qa_error *e)
{ return !n||!n->callbacks||!n->callbacks->protection||application_native_q2_protection_reserve(n->callbacks->protection,actor,e); }
bool application_native_q2_client_accepts_attack(struct application_native_q2 *n,qa_actor_id actor,bool *out,qa_error *e)
{
    if(!out) return application_fail(e,QA_ERROR_ARGUMENT,"Native attack eligibility omitted its result");
    if(!n||!n->callbacks||!current(n->callbacks,e)) return false;
    if(!n->callbacks->weapons) { *out=true; return true; }
    return application_native_q2_weapon_stage_accepts_attack(n->callbacks->weapons,actor,out,e);
}
bool application_native_q2_callbacks_components_begin(struct application_native_q2 *n,qa_actor_id actor,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    return !o||((!o->protection||application_native_q2_protection_bind(o->protection,actor,e))&&
        (!o->pickups||application_native_q2_pickups_bind(o->pickups,actor,e)));
}
bool application_native_q2_callbacks_inventory_group(struct application_native_q2 *n,qa_actor_id actor,
    uint64_t serial,const qa_inventory_source_group *saved,qa_inventory_items *out,qa_error *e)
{
    return n&&n->callbacks&&n->callbacks->items&&
        application_native_q2_items_inventory_group(n->callbacks->items,actor,serial,saved,out,e);
}
qa_native_instance *application_native_q2_callbacks_instance(application_native_q2_callbacks *o)
{ return o?instance(o):NULL; }
static size_t scalar_bytes(qa_native_value_type type)
{
    switch(type) {
    case QA_NATIVE_I8: case QA_NATIVE_U8: return 1;
    case QA_NATIVE_I16: case QA_NATIVE_U16: return 2;
    case QA_NATIVE_I32: case QA_NATIVE_U32: case QA_NATIVE_F32: return 4;
    case QA_NATIVE_I64: case QA_NATIVE_U64: case QA_NATIVE_F64: return 8;
    default: return 0;
    }
}
bool application_native_q2_callbacks_scalar_read(application_native_q2_callbacks *o,
    qa_native_address address,qa_native_value_type type,double *out,qa_error *e)
{
    size_t bytes=scalar_bytes(type); uint8_t raw[8];
    if(!out||!bytes||!current(o,e)||!qa_native_read(instance(o),address,raw,bytes,e)) return false;
    qa_native_value value={.type=type};
    switch(type) {
    case QA_NATIVE_I8: value.as.i8=(int8_t)raw[0]; break;
    case QA_NATIVE_U8: value.as.u8=raw[0]; break;
    case QA_NATIVE_I16: value.as.i16=(int16_t)qa_load_u16le(raw); break;
    case QA_NATIVE_U16: value.as.u16=qa_load_u16le(raw); break;
    case QA_NATIVE_I32: value.as.i32=qa_load_i32le(raw); break;
    case QA_NATIVE_U32: value.as.u32=qa_load_u32le(raw); break;
    case QA_NATIVE_I64: { uint64_t bits=qa_load_u64le(raw); memcpy(&value.as.i64,&bits,8); break; }
    case QA_NATIVE_U64: value.as.u64=qa_load_u64le(raw); break;
    case QA_NATIVE_F32: { uint32_t bits=qa_load_u32le(raw); memcpy(&value.as.f32,&bits,4); break; }
    case QA_NATIVE_F64: { uint64_t bits=qa_load_u64le(raw); memcpy(&value.as.f64,&bits,8); break; }
    default: return false;
    }
    double result=result_number(value);
    if(!isfinite(result)||((type==QA_NATIVE_I64||type==QA_NATIVE_U64)&&fabs(result)>9007199254740991.0))
        return fail(e,"Native source scalar exceeds its finite safe-number representation");
    *out=result; return true;
}
bool application_native_q2_callbacks_scalar_write(application_native_q2_callbacks *o,
    qa_native_address address,qa_native_value_type type,double number,qa_error *e)
{
    size_t bytes=scalar_bytes(type); qa_native_value value; uint8_t raw[8]={0};
    if(!bytes||!current(o,e)||!number_value(type,number,&value,e)) return false;
    encoded(value,raw); return qa_native_write(instance(o),address,(qa_bytes){raw,bytes},e);
}
bool application_native_q2_callbacks_observation_required(const application_native_q2_callbacks *o)
{
    if(!o||!o->document) return false;
    qa_json_id root=qa_json_root(o->document);
    qa_json_id inputs=qa_json_get(o->document,qa_json_get(o->document,root,"clients"),"input");
    for(size_t i=0;i<qa_json_size(o->document,inputs);++i) {
        qa_json_id outputs=qa_json_get(o->document,qa_json_at(o->document,inputs,i),"outputs");
        for(size_t j=0;j<qa_json_size(o->document,outputs);++j)
            if(qa_json_string_equal(o->document,qa_json_get(o->document,qa_json_at(o->document,outputs,j),"kind"),"handler"))
                return true;
    }
    return qa_json_size(o->document,qa_json_get(o->document,root,"pickups"))!=0||
        qa_json_size(o->document,qa_json_get(o->document,root,"protection"))!=0||
        qa_json_size(o->document,qa_json_get(o->document,root,"sourceActors"))!=0||
        qa_json_size(o->document,qa_json_get(o->document,qa_json_get(o->document,root,"items"),"storage"))!=0;
}
bool application_native_q2_callbacks_source_before(void *context,qa_error *e)
{
    struct application_native_q2 *n=context;
    if(n&&n->callbacks&&n->callbacks->active_region&&
        !region_current(n->callbacks->active_region,e)) return false;
    bool committed=!n||!n->callbacks||!n->callbacks->records||application_native_q2_records_lifecycle(n->callbacks->records)||
        application_native_q2_records_commit(n->callbacks->records,e);
    return committed&&(!n||!n->callbacks||!n->callbacks->active_region||
        region_current(n->callbacks->active_region,e));
}
bool application_native_q2_callbacks_source_after(void *context,qa_error *e)
{
    struct application_native_q2 *n=context;
    bool refreshed=!n||!n->callbacks||!n->callbacks->records||application_native_q2_records_lifecycle(n->callbacks->records)||
        application_native_q2_records_refresh(n->callbacks->records,e);
    return refreshed&&(!n||!n->callbacks||!n->callbacks->active_region||
        region_current(n->callbacks->active_region,e));
}
bool application_native_q2_callbacks_import(void *context,const qa_native_import_call *call,
    qa_native_value *result,bool *handled,qa_error *e)
{
    struct application_native_q2 *n=context; application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o||!call||!call->name||!result||!handled) return fail(e,"Native source import lost its actual callback owner");
    *handled=false;
    bool link=strcmp(call->name,"linkentity")==0,unlink=strcmp(call->name,"unlinkentity")==0;
    if(!link&&!unlink) return true;
    if(!o->records) return true;
    if(application_native_q2_records_lifecycle(o->records)) {
        *handled=true; *result=(qa_native_value){.type=QA_NATIVE_VOID}; return true;
    }
    if(!call->argument_count||call->arguments[0].type!=QA_NATIVE_ADDRESS||!call->arguments[0].as.address)
        return fail(e,"Native entity import omitted its genuine source pointer");
    uint32_t slot; qa_native_slot_binding binding;
    if(!qa_native_entity_slot(instance(o),call->arguments[0].as.address,&slot,e)||!qa_native_slot(instance(o),slot,&binding,e)) return false;
    if(binding.kind!=QA_NATIVE_SLOT_BORROWED) return true;
    if(binding.owner!=n->provider->owner||binding.source_slot!=slot||!qa_actors_get(qa_session_actors(n->provider->application->session),binding.actor))
        return fail(e,"Native source import names a retired borrowed actor");
    *handled=true; *result=(qa_native_value){.type=QA_NATIVE_VOID};
    return link?qa_world_link(n->world,binding.actor,NULL,e):qa_world_unlink(n->world,binding.actor,e);
}
bool application_native_q2_callbacks_release_actor(struct application_native_q2 *n,qa_actor_id actor,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(o) application_native_q2_client_outputs_release(n,actor);
    return !o||(application_native_q2_pickups_release(o->pickups,actor,e)&&
        application_native_q2_weapon_stage_release(o->weapons,actor,e)&&
        application_native_q2_protection_release(o->protection,actor,e)&&
        application_native_q2_items_release(o->items,actor,e)&&
        (!o->records||application_native_q2_records_release(o->records,actor,e)));
}
bool application_native_q2_callbacks_capture(struct application_native_q2 *n,qa_buffer *out,qa_error *e)
{
    if(!out) return false;
    if(!n||!n->callbacks) { *out=(qa_buffer){0}; return true; }
    if(!application_native_q2_callbacks_current(n->callbacks)) return application_fail(e,QA_ERROR_ARGUMENT,"Native callbacks retain a reached transfer at capture");
    application_native_q2_callbacks *o=n->callbacks;
    qa_buffer children[4]={{0}}; qa_source_save_io io={0};
    bool ok=application_native_q2_records_checkpoint(o->records,children,e)&&
        (!o->protection||application_native_q2_protection_checkpoint(o->protection,children+1,e))&&
        (!o->weapons||application_native_q2_weapon_stage_capture(o->weapons,children+2,e))&&
        (!o->pickups||application_native_q2_pickups_checkpoint(o->pickups,children+3,e));
    uint8_t tag[8]={'N','Q','C','C',2,0,0,0};
    if(ok) ok=qa_source_save_writer(&io,n->provider->application->session,e)&&qa_source_save_bytes(&io,tag,sizeof(tag));
    for(size_t i=0;ok&&i<4;++i) {
        size_t size=children[i].size;
        ok=qa_source_save_count(&io,&size,UINT32_MAX)&&qa_source_save_bytes(&io,children[i].data,size);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    for(size_t i=0;i<4;++i) qa_buffer_free(children+i);
    return ok;
}
bool application_native_q2_callbacks_restore(struct application_native_q2 *n,qa_bytes state,qa_error *e)
{
    if(!n||!n->callbacks) return !state.size||application_fail(e,QA_ERROR_FORMAT,"Saved callbacks require their actual declaration owner");
    application_native_q2_callbacks *o=n->callbacks;
    if(o->components_restoring||o->restored_weapons.data)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callback components already own a restore continuation");
    qa_source_save_io io={0}; uint8_t tag[8]={0},expected[8]={'N','Q','C','C',2,0,0,0}; qa_bytes children[4]={{0}};
    bool ok=qa_source_save_reader(&io,n->provider->application->session,state,e)&&
        qa_source_save_bytes(&io,tag,sizeof(tag))&&!memcmp(tag,expected,sizeof(tag));
    for(size_t i=0;ok&&i<4;++i) {
        size_t size=0;
        ok=qa_source_save_count(&io,&size,UINT32_MAX)&&io.offset<=io.input.size&&size<=io.input.size-io.offset;
        if(ok) { children[i]=(qa_bytes){io.input.data+io.offset,size}; io.offset+=size; }
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) return e&&e->code!=QA_OK?false:application_fail(e,QA_ERROR_FORMAT,"Native callback component continuation is malformed");
    if(!records_prepare(o,e)||!components_prepare(o,e)) return false;
    if(!children[0].size||(children[1].size!=0)!=(o->protection!=NULL)||
        (children[2].size!=0)!=(o->weapons!=NULL)||(children[3].size!=0)!=(o->pickups!=NULL))
        return fail(e,"Native saved components differ from their acquired declaration");
    qa_buffer weapons={0};
    if(children[2].size) {
        weapons.data=malloc(children[2].size);
        if(!weapons.data) return application_fail(e,QA_ERROR_MEMORY,"Retaining genuine native weapon requests until inventory restoration");
        weapons.size=children[2].size; memcpy(weapons.data,children[2].data,weapons.size);
    }
    ok=application_native_q2_records_restore(o->records,children[0],e);
    if(ok) { o->components_restoring=true; o->restored_weapons=weapons; weapons=(qa_buffer){0}; }
    if(ok&&o->protection) ok=application_native_q2_protection_restore(o->protection,children[1],e);
    if(ok&&o->pickups) ok=application_native_q2_pickups_restore(o->pickups,children[3],e);
    qa_buffer_free(&weapons); return ok;
}
bool application_native_q2_callbacks_equipment_restore_prepare(struct qa_application *app,qa_error *e)
{
    if(!app) return application_fail(e,QA_ERROR_ARGUMENT,"Native equipment restoration lost its actual application");
    for(size_t i=0;i<app->provider_count;++i) {
        application_provider *p=app->providers[i];
        struct application_native_q2 *n=p&&p->kind==APPLICATION_PROVIDER_NATIVE?p->state.native.q2_engine:NULL;
        application_native_q2_callbacks *o=n?n->callbacks:NULL;
        if(!o||!o->components_restoring) continue;
        if(!current(o,e)) return false;
        if(!application_native_q2_items_finish_restore(o->items,e)) return false;
        for(uint32_t slot=1;slot<257;++slot) {
            const application_native_q2_client *client=n->clients+slot;
            if(!client->reserved||!client->connected||client->denied||client->disconnect_started) continue;
            if(application_native_q2_items_actor_admitted(o->items,client->actor)&&o->weapons&&
                !application_native_q2_weapon_stage_admit(o->weapons,client->actor,e)) return false;
        }
        if(o->restored_weapons.data) {
            if(!application_native_q2_weapon_stage_restore(o->weapons,
                (qa_bytes){o->restored_weapons.data,o->restored_weapons.size},e)) return false;
            qa_buffer_free(&o->restored_weapons);
        }
    }
    return true;
}
bool application_native_q2_callbacks_protection_saved_binding(struct application_native_q2 *n,qa_actor_id actor,
    qa_protection_channel channel,const qa_protection_claim *claim,qa_protection_binding *out,qa_error *e)
{
    return n&&n->callbacks&&n->callbacks->protection&&
        application_native_q2_protection_saved_binding(n->callbacks->protection,actor,channel,claim,out,e);
}
bool application_native_q2_callbacks_pickup_saved_rule(struct application_native_q2 *n,qa_actor_id actor,
    qa_actor_owner owner,uint64_t serial,uint32_t rule,qa_pickup_rule *out,qa_error *e)
{
    return n&&n->callbacks&&n->callbacks->pickups&&
        application_native_q2_pickups_saved_rule(n->callbacks->pickups,actor,owner,serial,rule,out,e);
}
bool application_native_q2_callbacks_finish_restore(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(o->restored_weapons.data) return application_fail(e,QA_ERROR_ARGUMENT,"Native weapon continuation has not reached canonical equipment restoration");
    if((application_native_q2_records_restoring(o->records)&&
        !application_native_q2_records_finish_restore(o->records,e))||
        !application_native_q2_items_finish_restore(o->items,e)||
        !application_native_q2_protection_finish_restore(o->protection,e)||
        (o->pickups&&!application_native_q2_pickups_finish_restore(o->pickups,e))) return false;
    o->components_restoring=false; return true;
}
bool application_native_q2_callbacks_arrays_validate(struct application_native_q2 *n,qa_error *e)
{ return !n||!n->callbacks||(records_prepare(n->callbacks,e)&&application_native_q2_records_validate(n->callbacks->records,e)); }
bool application_native_q2_callbacks_reserved_slot(void *context,uint32_t slot,bool *out,qa_error *e)
{
    struct application_native_q2 *n=context;
    if(!n||!out) return application_fail(e,QA_ERROR_ARGUMENT,"Native source reservation requires its retained engine");
    *out=false; application_native_q2_callbacks *o=n->callbacks;
    if(!o) return true;
    if(!current(o,e)) return false;
    const qa_json_document *d=o->document; qa_buffer name={0};
    if(!qa_json_string(d,qa_json_get(d,qa_json_root(d),"entityRecord"),&name,e)) return false;
    qa_json_id r=record_find(o,(char *)name.data); qa_buffer_free(&name);
    uint32_t first,capacity;
    if(r==QA_JSON_NONE||!word(d,r,"firstSlot",&first,e)||!word(d,r,"capacity",&capacity,e)) return false;
    *out=slot>=first&&(uint64_t)slot<(uint64_t)first+capacity;
    return true;
}

static const char *const input_names[Q3_MOD_VALUE_COUNT]={
    "view-angles","attack","jump","impulse","forward-move","side-move","up-move",
    "self","other","activator","attacker","inflictor","amount","damage-flags",
    "regular-protection-scale","knockback","point","direction","normal","item",
    "time","elapsed","result","pickup-count","pickup-has-count","pickup-dropped"
};
static bool canonical_inputs(native_registered *r,const void *request,const void *result,
    application_native_callback_value values[Q3_MOD_VALUE_COUNT],
    application_native_callback_inputs *out,qa_error *e)
{
    application_native_q2_callbacks *o=r->owner;
    application_q3_mod_inputs source={0};
    application_q3_mod_operation_services *s=o->operations+r->operation;
    if(!current(o,e)||!s->inputs(s->context,request,result,&source,e)||!current(o,e)) return false;
    size_t count=0;
    for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i) {
        const application_q3_mod_value *v=source.values+i;
        if(v->kind==Q3_MOD_VALUE_ABSENT) continue;
        application_native_callback_value *dest=values+count++;
        *dest=(application_native_callback_value){.name=input_names[i]};
        switch(v->kind) {
        case Q3_MOD_VALUE_SCALAR:dest->kind=APPLICATION_NATIVE_VALUE_NUMBER;dest->value.number=v->as.scalar;break;
        case Q3_MOD_VALUE_VECTOR:dest->kind=APPLICATION_NATIVE_VALUE_VECTOR;dest->value.vector=v->as.vector;break;
        case Q3_MOD_VALUE_STRING:dest->kind=APPLICATION_NATIVE_VALUE_STRING;dest->value.string=v->as.string;break;
        case Q3_MOD_VALUE_ACTOR:dest->kind=APPLICATION_NATIVE_VALUE_ACTOR;dest->value.actor=v->as.actor;break;
        default:return fail(e,"Canonical native callback has an unknown input type");
        }
    }
    *out=(application_native_callback_inputs){.values=values,.count=count};
    if(!input(out,"time")) {
        double time;
        if(!application_native_q2_callbacks_time_read(o,&time,e))return false;
        values[count++]=(application_native_callback_value){.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,
            .value.number=time};
        out->count=count;
    }
    return true;
}
static bool callback_transform(void *context,void *request,qa_error *e)
{
    native_registered *r=context; application_native_callback_value values[Q3_MOD_VALUE_COUNT];
    application_native_callback_inputs inputs; double value;
    return canonical_inputs(r,request,NULL,values,&inputs,e)&&
        application_native_q2_callbacks_call(r->owner,r->call,&inputs,&value,e)&&
        r->owner->operations[r->operation].transform(r->owner->operations[r->operation].context,
            request,r->knockback,value,e)&&current(r->owner,e);
}
static bool callback_observe(void *context,const void *request,const void *result,qa_error *e)
{
    native_registered *r=context; application_native_callback_value values[Q3_MOD_VALUE_COUNT];
    application_native_callback_inputs inputs; double value;
    return canonical_inputs(r,request,result,values,&inputs,e)&&
        application_native_q2_callbacks_call(r->owner,r->call,&inputs,&value,e);
}
static bool callback_replace(void *context,const void *request,qa_operation_next next,void *result,qa_error *e)
{
    (void)next;
    native_registered *r=context; application_native_callback_value values[Q3_MOD_VALUE_COUNT];
    application_native_callback_inputs inputs; double value;
    return canonical_inputs(r,request,NULL,values,&inputs,e)&&
        application_native_q2_callbacks_call(r->owner,r->call,&inputs,&value,e)&&
        r->owner->operations[r->operation].replace(r->owner->operations[r->operation].context,
            result,value!=0,e)&&current(r->owner,e);
}
bool application_native_q2_callbacks_register(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(!application_native_q2_callbacks_current(o)||!n->initialized||!n->map_ready)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native callbacks require their completed source map owner");
    if(!application_native_q2_weapon_stage_activate(o->weapons,e)||
        (o->pickups&&!application_native_q2_pickups_activate(o->pickups,e))) return false;
    if(o->pickups) for(uint32_t slot=1;slot<257;++slot) {
        const application_native_q2_client *client=n->clients+slot;
        if(client->reserved&&client->connected&&!client->denied&&!client->disconnect_started&&
            !application_native_q2_pickups_bind(o->pickups,client->actor,e)) return false;
    }
    qa_json_id rows=qa_json_get(o->document,qa_json_root(o->document),"callbacks");
    size_t count=qa_json_size(o->document,rows);
    if(!count) return true;
    if(!application_q3_mod_operations_read(n->provider->application->mod_operations,o->operations,e)) return false;
    if(!o->registrations) {
        o->registrations=calloc(count,sizeof(*o->registrations));
        if(!o->registrations) return application_fail(e,QA_ERROR_MEMORY,"Retaining native canonical callback registrations");
        o->registration_count=count;
    }
    static const char *const operations[]={"damage","inventory.give","inventory.consume",
        "actor.think","actor.touch","actor.use","actor.pain","actor.die"};
    for(size_t i=0;i<count;++i) {
        native_registered *r=o->registrations+i;
        if(r->registration) continue;
        qa_json_id row=qa_json_at(o->document,rows,i); size_t kind=0;
        while(kind<Q3_MOD_OPERATION_COUNT&&!qa_json_string_equal(o->document,
            qa_json_get(o->document,row,"operation"),operations[kind])) ++kind;
        if(kind==Q3_MOD_OPERATION_COUNT) return fail(e,"Unknown native canonical callback operation");
        qa_json_id stage=qa_json_get(o->document,row,"stage"),result=qa_json_get(o->document,row,"result");
        qa_operation_hook_kind hook_kind;
        if(qa_json_string_equal(o->document,stage,"observe")) hook_kind=QA_OPERATION_OBSERVE;
        else if(qa_json_string_equal(o->document,stage,"transform")&&kind<=Q3_MOD_CONSUME&&
            (qa_json_string_equal(o->document,result,"amount")||(kind==Q3_MOD_DAMAGE&&qa_json_string_equal(o->document,result,"knockback"))))
            hook_kind=QA_OPERATION_TRANSFORM;
        else if(qa_json_string_equal(o->document,stage,"replace")&&kind>=Q3_MOD_THINK&&qa_json_string_equal(o->document,result,"boolean"))
            hook_kind=QA_OPERATION_REPLACE;
        else return fail(e,"Native callback stage does not own its declared canonical result");
        if(hook_kind!=QA_OPERATION_OBSERVE&&qa_json_string_equal(o->document,qa_json_get(o->document,row,"returns"),"void"))
            return fail(e,"Native callback result requires a scalar source return");
        qa_buffer name={0};
        if(!text(o->document,qa_json_get(o->document,row,"id"),&name,e)) return false;
        bool named=name.size&&memchr(name.data,':',name.size)&&qa_strings_intern_cstr(
            qa_session_strings(n->provider->application->session),(char *)name.data,&r->name,e);
        qa_buffer_free(&name);
        if(!named) return fail(e,"Native callback registration requires its declared namespaced identity");
        for(size_t j=0;j<i;++j) if(o->registrations[j].name==r->name) return fail(e,"Native callback registration identity repeats");
        r->owner=o; r->call=row; r->operation=(application_q3_mod_operation)kind;
        r->stage=hook_kind; r->knockback=qa_json_string_equal(o->document,result,"knockback");
        qa_operation_hook hook={.owner=n->provider->owner,.name=r->name,.kind=hook_kind,.context=r};
        if(hook_kind==QA_OPERATION_OBSERVE) hook.call.observe=callback_observe;
        else if(hook_kind==QA_OPERATION_TRANSFORM) hook.call.transform=callback_transform;
        else hook.call.replace=callback_replace;
        if(!qa_operation_register(o->operations[kind].operation,&hook,&r->registration,e)) return false;
    }
    return true;
}
bool application_native_q2_callbacks_suspend(struct application_native_q2 *n,qa_error *e)
{
    application_native_q2_callbacks *o=n?n->callbacks:NULL;
    if(!o) return true;
    if(o->calls) return application_fail(e,QA_ERROR_ARGUMENT,"Native callback registration retains an entered invocation");
    if(!application_native_q2_weapon_stage_suspend(o->weapons,e)) return false;
    for(native_skip *skip=o->skips;skip;skip=skip->next) {
        if(skip->binding&&!qa_native_remove_region(skip->binding,e)) return false;
        skip->binding=NULL;
    }
    for(size_t i=0;i<o->registration_count;++i) {
        native_registered *r=o->registrations+i;
        if(r->registration&&!qa_operation_destroy_validate(o->operations[r->operation].operation,e)) return false;
    }
    for(size_t i=0;i<o->registration_count;++i) {
        native_registered *r=o->registrations+i;
        if(r->registration&&!qa_operation_unregister(o->operations[r->operation].operation,r->registration))
            return application_fail(e,QA_ERROR_ARGUMENT,"Native callback registration lost its canonical lifetime");
        r->registration=0;
    }
    return true;
}
