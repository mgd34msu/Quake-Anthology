#include "guest_native_q2_private.h"
#include "native_q2_source_invocation.h"
#include "native_q2_client_stages.h"
#include "native_q2_source_actors.h"
#include "native_q2_client_outputs.h"
#include "qa/native_observe.h"
#include "qa/network.h"
#include <math.h>

typedef struct native_input_store {
    struct native_input_store *next;
    qa_native_address address;
    qa_json_id field;
    uint8_t previous[12];
    size_t bytes;
} native_input_store;
typedef struct native_input_output native_input_output;
typedef struct native_input_handler native_input_handler;
struct application_native_q2_stages {
    struct application_native_q2 *engine;
    qa_native_entry_observer *allocate,*release;
    struct application_native_q2_input *inputs;
    native_input_handler *handlers;
    struct application_native_q2_client_outputs *client_outputs;
    qa_buffer damage_saved;
    uint64_t frame,next_ns,interval_ns;
    uint32_t cursor;
    bool advancing,ticking,failed;
    uint32_t *pending;
    size_t pending_count,pending_capacity;
};
struct application_native_q2_input {
    struct application_native_q2_input *outer;
    struct application_native_q2_stages *owner;
    qa_actor_id actor;
    bool slice,completed,failed;
    unsigned executing;
    bool captured;
    application_q3_mod_inputs inputs;
    char *strings[Q3_MOD_VALUE_COUNT];
    native_input_store *stores;
    native_input_output *outputs;
    uint8_t command[28];
    bool (*values)(void *,application_q3_mod_inputs *,qa_error *);
    bool (*output)(void *,const application_q3_mod_output *,qa_error *);
    void *context;
};
struct native_input_output {
    struct native_input_output *next;
    struct application_native_q2_input *input;
    qa_json_id declaration;
    native_input_handler *handler;
    struct native_input_output *subscriber_next;
    qa_native_address address,self;
    size_t self_index,bytes;
    uint8_t before[12];
    bool called;
};
struct native_input_handler {
    native_input_handler *next;
    struct application_native_q2_stages *owner;
    qa_native_address address;
    qa_native_entry_observer *binding;
    native_input_output *subscribers;
    qa_native_value_type parameters[64];
    size_t count;
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
bool application_native_q2_stages_time_read(const struct application_native_q2 *n,
    qa_source_frame *out,qa_error *e)
{
    application_provider *provider=n?n->provider:NULL;
    qa_application *app=provider?provider->application:NULL;
    if(!out||!app||provider->state.native.q2_engine!=n||!n->callbacks)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared source time requires its actual native owner");
    if(n->stages&&n->stages->ticking) {
        if(!n->stages->advancing||n->frame.provider!=provider->owner)
            return application_fail(e,QA_ERROR_ARGUMENT,"Declared source tick lost its actual frame");
        *out=n->frame;return true;
    }
    application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
    qa_clock_state clock;
    if(!primary||!primary->attached||!primary->component_attached||
        !qa_session_clock(app->session,primary->owner,&clock)||clock.frame.provider!=primary->owner)
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared source time lost the actual primary SourceFrame clock");
    *out=clock.frame;return true;
}
static bool intercepted(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *e)
{
    struct application_native_q2_stages *o=context;
    if(!current(o,e)||native!=instance(o->engine)) return false;
    ++o->engine->calls;
    bool ok=application_native_q2_source_original(o->engine,binding,arguments,count,result,e);
    if(ok&&binding==o->allocate&&result&&result->type==QA_NATIVE_ADDRESS&&result->as.address) {
        qa_actor_id actor;uint32_t slot;
        ok=qa_native_host_source_birth(o->engine->provider->state.native.host,result->as.address,&actor,e)&&
            qa_native_entity_slot(native,result->as.address,&slot,e);
        if(ok) for(size_t i=0;i<o->pending_count;) {
            if(o->pending[i]!=slot) {++i;continue;}
            --o->pending_count;memmove(o->pending+i,o->pending+i+1,(o->pending_count-i)*sizeof(*o->pending));
        }
    }
    if(ok&&binding==o->release) ok=qa_native_host_source_frame_end(o->engine->provider->state.native.host,e);
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
    if(!application_native_q2_client_outputs_create(n,&o->client_outputs,e))return false;
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
        qa_source_frame now;
        if(!application_native_q2_stages_time_read(n,&now,e))return false;
        if(now.time_ns>UINT64_MAX-interval) return application_fail(e,QA_ERROR_FORMAT,"Declared native next frame overflows");
        o->interval_ns=interval; o->next_ns=now.time_ns+interval;
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
    return application_native_q2_source_actors_refresh(n,e);
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
typedef struct native_client_frame {
    struct application_native_q2 *engine;
    qa_actor_id actor;
    const char *section;
} native_client_frame;
static bool invoke_client_frame(void *context,qa_session *session,qa_error *e)
{
    (void)session;native_client_frame *call=context;struct application_native_q2 *n=call->engine;
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=call->actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)n->frame.time_ns/1e9}};
    application_native_callback_inputs inputs={values,2,{0}};bool accepted;
    return application_native_q2_callbacks_run(n,call->section,&inputs,&accepted,e);
}
static bool client_frame(struct application_native_q2 *n,uint32_t slot,const char *section,qa_error *e)
{
    application_native_q2_client *client=n->clients+slot;
    if(!client->reserved||!client->connected||client->denied||client->disconnect_started||
        !qa_actors_get(qa_session_actors(n->provider->application->session),client->actor)) return true;
    native_client_frame call={n,client->actor,section};
    return qa_session_invoke(n->provider->application->session,client->actor,QA_INVOKE_THINK,invoke_client_frame,&call,e);
}
typedef struct native_actor_update {
    struct application_native_q2 *engine;
    qa_json_id update;
    qa_native_address address;
} native_actor_update;
static bool invoke_update(void *context,qa_session *session,qa_error *e)
{
    (void)session;native_actor_update *call=context;
    const qa_json_document *d=application_native_q2_callbacks_document(call->engine->callbacks);
    qa_native_value argument={.type=QA_NATIVE_ADDRESS,.as.address=call->address};bool entered=false;
    return application_native_q2_callbacks_entry_call(call->engine->callbacks,qa_json_get(d,call->update,"entry"),
        qa_json_get(d,call->update,"returns"),&argument,1,&entered,e);
}
static bool releases(struct application_native_q2_stages *o,qa_error *e)
{
    struct application_native_q2 *n=o->engine;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    while(o->pending_count) {
        uint32_t slot=o->pending[0];qa_native_entity_table table;bool active=false;
        if(!qa_native_entity_table_refresh(instance(n),&table,e)||slot>=table.capacity)
            return application_fail(e,QA_ERROR_FORMAT,"Pending native actor exceeds its original source table");
        if(slot<table.count&&!qa_native_host_source_active(n->provider->state.native.host,slot,&active,e)) return false;
        if(active) {
            qa_native_address address;if(!qa_native_entity_address(instance(n),slot,&address,e)) return false;
            qa_native_value argument={.type=QA_NATIVE_ADDRESS,.as.address=address};bool entered;
            if(!application_native_q2_callbacks_entry_call(n->callbacks,qa_json_get(d,source(n),"release"),QA_JSON_NONE,&argument,1,&entered,e)||
                !qa_native_host_source_active(n->provider->state.native.host,slot,&active,e)) return false;
            if(active) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native source refused a released actor");
        }
        --o->pending_count;memmove(o->pending,o->pending+1,o->pending_count*sizeof(*o->pending));
    }
    return true;
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
    qa_source_frame outer=n->frame;bool ok=releases(o,e);o->advancing=true;
    while(ok&&o->next_ns<=frame->time_ns) {
        if(o->frame==UINT64_MAX||o->next_ns>UINT64_MAX-o->interval_ns) { ok=application_fail(e,QA_ERROR_FORMAT,"Declared native clock is exhausted");break; }
        ++o->frame;n->frame.number=o->frame;n->frame.time_ns=o->next_ns;
        n->frame.start_ns=o->next_ns-o->interval_ns;n->frame.elapsed_ns=o->interval_ns;o->next_ns+=o->interval_ns;
        o->ticking=true;
        ok=clock_write(o,e)&&qa_native_host_source_frame_begin(n->provider->state.native.host,e);
        qa_native_entity_table table={0};
        if(ok) ok=qa_native_entity_table_get(instance(n),&table,e);
        for(o->cursor=0;ok;++o->cursor) {
            ok=qa_native_entity_table_refresh(instance(n),&table,e);
            if(!ok||o->cursor>=table.count) break;
            if(o->cursor&&o->cursor<257&&n->clients[o->cursor].reserved&&n->clients[o->cursor].actor.registry) {
                ok=client_frame(n,o->cursor,"clients.frame",e);continue;
            }
            qa_native_slot_binding binding;qa_native_address address;
            ok=qa_native_slot(instance(n),o->cursor,&binding,e);
            if(!ok||binding.kind!=QA_NATIVE_SLOT_OWNED) continue;
            ok=qa_native_entity_address(instance(n),o->cursor,&address,e);
            if(ok) {
                native_actor_update call={n,update,address};
                ok=qa_session_invoke(n->provider->application->session,binding.actor,QA_INVOKE_THINK,invoke_update,&call,e);
            }
        }
        for(uint32_t slot=1;ok&&slot<257;++slot) ok=client_frame(n,slot,"clients.endFrame",e);
        if(ok) ok=qa_native_host_source_frame_end(n->provider->state.native.host,e);
        o->ticking=false;
    }
    n->frame=outer;o->ticking=false;o->advancing=false;o->failed=!ok;return ok;
}
bool application_native_q2_stages_idle(const struct application_native_q2_stages *o)
{ return !o||(!o->advancing&&!o->inputs&&!o->handlers); }
uint64_t application_native_q2_stages_frame(const struct application_native_q2 *n)
{ return n&&n->stages&&n->stages->interval_ns?n->stages->frame:n?n->frame.number:0; }
struct application_native_q2_client_outputs *application_native_q2_stages_outputs(const struct application_native_q2 *n)
{return n&&n->stages?n->stages->client_outputs:NULL;}
qa_bytes application_native_q2_stages_damage_saved(const struct application_native_q2 *n)
{return n&&n->stages?(qa_bytes){n->stages->damage_saved.data,n->stages->damage_saved.size}:(qa_bytes){0};}
void application_native_q2_stages_damage_adopted(struct application_native_q2 *n)
{if(n&&n->stages)qa_buffer_free(&n->stages->damage_saved);}
bool application_native_q2_stages_close(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_stages *o=n?n->stages:NULL;
    if(!o) return true;
    if(!application_native_q2_stages_idle(o)) return application_fail(e,QA_ERROR_ARGUMENT,"Declared native stages retain an input or source callback");
    if(o->release&&!qa_native_unobserve_entry(o->release,e)) return false;
    o->release=NULL;
    if(o->allocate&&!qa_native_unobserve_entry(o->allocate,e)) return false;
    o->allocate=NULL;application_native_q2_client_outputs_destroy(&o->client_outputs);
    qa_buffer_free(&o->damage_saved);
    free(o->pending);free(o);n->stages=NULL;return true;
}
void application_native_q2_stages_released(struct application_native_q2 *n,qa_actor_id actor)
{
    struct application_native_q2_stages *o=n?n->stages:NULL;
    application_native_q2_client_outputs_release(n,actor);
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
    if(o->damage_saved.size)return application_fail(e,QA_ERROR_ARGUMENT,"Native stages capture retains an unadopted combat continuation");
    qa_buffer outputs={0},damage={0};if(!application_native_q2_client_outputs_capture(n,&outputs,e))return false;
    if(!application_native_q2_source_actors_damage_capture(n,&damage,e)||damage.size>UINT32_MAX){qa_buffer_free(&outputs);qa_buffer_free(&damage);return false;}
    if(damage.size>SIZE_MAX-78||o->pending_count>(SIZE_MAX-78-damage.size)/4){qa_buffer_free(&outputs);qa_buffer_free(&damage);return application_fail(e,QA_ERROR_MEMORY,"Native stages continuation exceeds its physical extent");}
    size_t bytes=40+o->pending_count*4+outputs.size+damage.size;uint8_t *data=malloc(bytes);
    if(!data){qa_buffer_free(&outputs);qa_buffer_free(&damage);return application_fail(e,QA_ERROR_MEMORY,"Retaining native stages continuation");}
    qa_store_u32le(data,UINT32_C(0x3353434e));qa_store_u64le(data+4,o->frame);
    qa_store_u64le(data+12,o->next_ns);qa_store_u64le(data+20,o->interval_ns);qa_store_u32le(data+28,o->cursor);
    qa_store_u32le(data+32,(uint32_t)o->pending_count);
    for(size_t i=0;i<o->pending_count;++i) qa_store_u32le(data+36+i*4,o->pending[i]);
    memcpy(data+36+o->pending_count*4,outputs.data,outputs.size);qa_buffer_free(&outputs);
    size_t offset=74+o->pending_count*4;qa_store_u32le(data+offset,(uint32_t)damage.size);
    if(damage.size)memcpy(data+offset+4,damage.data,damage.size);qa_buffer_free(&damage);
    *out=(qa_buffer){data,bytes};return true;
}
bool application_native_q2_stages_restore(struct application_native_q2 *n,qa_bytes bytes,qa_error *e)
{
    if(!n||!n->callbacks) return !bytes.size||application_fail(e,QA_ERROR_FORMAT,"Saved native stages require their declaration");
    if(bytes.size<78||qa_load_u32le(bytes.data)!=UINT32_C(0x3353434e)) return application_fail(e,QA_ERROR_FORMAT,"Invalid declared native stage continuation");
    uint32_t count=qa_load_u32le(bytes.data+32);
    if((uint64_t)count*4>bytes.size-78) return application_fail(e,QA_ERROR_FORMAT,"Invalid native pending release extent");
    size_t damage_offset=74+(size_t)count*4;uint32_t damage_size=qa_load_u32le(bytes.data+damage_offset);
    if(damage_size!=bytes.size-damage_offset-4)return application_fail(e,QA_ERROR_FORMAT,"Saved declared combat continuation extent is invalid");
    struct application_native_q2_stages *o=calloc(1,sizeof(*o));
    if(!o) return application_fail(e,QA_ERROR_MEMORY,"Restoring native client stages");
    o->engine=n;o->frame=qa_load_u64le(bytes.data+4);o->next_ns=qa_load_u64le(bytes.data+12);
    o->interval_ns=qa_load_u64le(bytes.data+20);o->cursor=qa_load_u32le(bytes.data+28);
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);qa_json_id definition=source(n);
    double seconds=0;bool has_clock=definition!=QA_JSON_NONE;
    bool ok=(!has_clock||qa_json_number(d,qa_json_get(d,definition,"frameSeconds"),&seconds,e))&&
        (has_clock?(isfinite(seconds)&&seconds>0&&seconds<UINT64_MAX/1e9&&o->interval_ns==(uint64_t)floor(seconds*1e9+0.5)&&o->interval_ns&&o->next_ns>=o->interval_ns):(!o->interval_ns&&!o->frame&&!o->next_ns&&!count));
    if(ok&&count) {o->pending=malloc((size_t)count*sizeof(*o->pending));ok=o->pending!=NULL;o->pending_count=o->pending_capacity=count;}
    if(ok&&damage_size){o->damage_saved.data=malloc(damage_size);ok=o->damage_saved.data!=NULL;
        if(ok){memcpy(o->damage_saved.data,bytes.data+damage_offset+4,damage_size);o->damage_saved.size=damage_size;}}
    for(size_t i=0;ok&&i<count;++i) {o->pending[i]=qa_load_u32le(bytes.data+36+i*4);ok=o->pending[i]!=0;
        for(size_t j=0;ok&&j<i;++j) ok=o->pending[i]!=o->pending[j];}
    if(ok) ok=application_native_q2_stages_close(n,e);
    if(!ok) {qa_buffer_free(&o->damage_saved);free(o->pending);free(o);return application_fail(e,QA_ERROR_FORMAT,"Saved native source cadence differs from its declaration");}
    n->stages=o;
    return application_native_q2_client_outputs_create(n,&o->client_outputs,e)&&
        application_native_q2_client_outputs_restore(n,(qa_bytes){bytes.data+36+(size_t)count*4,38},e);
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
    for(uint32_t i=1;i<257;++i) if(n->clients[i].reserved&&n->clients[i].connected&&
        !n->clients[i].denied&&!n->clients[i].disconnect_started&&qa_actor_id_equal(n->clients[i].actor,s->actor)) return true;
    return application_fail(e,QA_ERROR_ARGUMENT,"Native input lost its admitted physical client");
}
static bool input_live(const struct application_native_q2_input *s)
{
    struct application_native_q2 *n=s->owner->engine;
    if(!qa_actors_get(qa_session_actors(n->provider->application->session),s->actor)) return false;
    for(uint32_t i=1;i<257;++i)
        if(n->clients[i].reserved&&n->clients[i].connected&&!n->clients[i].denied&&
            !n->clients[i].disconnect_started&&qa_actor_id_equal(n->clients[i].actor,s->actor)) return true;
    return false;
}
static bool input_values(struct application_native_q2_input *s,application_native_callback_value values[Q3_MOD_VALUE_COUNT],
    application_native_callback_inputs *out,qa_error *e)
{
    if(!input_current(s,e)) return false;
    if(!s->captured) {
        if(!s->values(s->context,&s->inputs,e)||!input_current(s,e)) return false;
        bool accepts_attack;
        if(!application_native_q2_client_accepts_attack(s->owner->engine,s->actor,&accepts_attack,e)||
            !input_current(s,e)) return false;
        if(!accepts_attack&&s->inputs.values[Q3_MOD_ATTACK].kind==Q3_MOD_VALUE_SCALAR)
            s->inputs.values[Q3_MOD_ATTACK].as.scalar=0;
        for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i) if(s->inputs.values[i].kind==Q3_MOD_VALUE_STRING) {
            const char *value=s->inputs.values[i].as.string;
            if(!value) return application_fail(e,QA_ERROR_ARGUMENT,"Native input string has no actual parent value");
            size_t bytes=strlen(value);
            s->strings[i]=malloc(bytes+1);
            if(!s->strings[i]) return application_fail(e,QA_ERROR_MEMORY,"Retaining declared native input string");
            memcpy(s->strings[i],value,bytes+1);s->inputs.values[i].as.string=s->strings[i];
        }
        s->captured=true;
    }
    const application_q3_mod_inputs *inputs=&s->inputs;
    size_t count=0;
    for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i) {
        application_q3_mod_value value=inputs->values[i];if(value.kind==Q3_MOD_VALUE_ABSENT) continue;
        application_native_callback_value *v=values+count++;v->name=input_names[i];
        switch(value.kind) {
        case Q3_MOD_VALUE_SCALAR:v->kind=APPLICATION_NATIVE_VALUE_NUMBER;v->value.number=value.as.scalar;break;
        case Q3_MOD_VALUE_VECTOR:v->kind=APPLICATION_NATIVE_VALUE_VECTOR;v->value.vector=value.as.vector;break;
        case Q3_MOD_VALUE_ACTOR:v->kind=APPLICATION_NATIVE_VALUE_ACTOR;v->value.actor=value.as.actor;break;
        case Q3_MOD_VALUE_STRING:v->kind=APPLICATION_NATIVE_VALUE_STRING;v->value.string=value.as.string;break;
        default:return application_fail(e,QA_ERROR_ARGUMENT,"Native input received an unknown canonical value");
        }
    }
    bool rerelease=s->owner->engine->profile==QA_NATIVE_Q2_GAME_API2023;
    const application_q3_mod_value *v=inputs->values;
    const application_q3_mod_input scalar_inputs[]={Q3_MOD_ATTACK,Q3_MOD_JUMP,Q3_MOD_IMPULSE,Q3_MOD_FORWARD,Q3_MOD_SIDE,Q3_MOD_UP,Q3_MOD_ELAPSED};
    for(size_t i=0;i<sizeof(scalar_inputs)/sizeof(*scalar_inputs);++i) if(v[scalar_inputs[i]].kind!=Q3_MOD_VALUE_SCALAR||!isfinite(v[scalar_inputs[i]].as.scalar))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native user command lacks a finite actual input");
    if(v[Q3_MOD_VIEW_ANGLES].kind!=Q3_MOD_VALUE_VECTOR||!qa_vec_finite(v[Q3_MOD_VIEW_ANGLES].as.vector))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native user command lacks its actual absolute aim");
    double milliseconds=round(v[Q3_MOD_ELAPSED].as.scalar*1000),impulse=v[Q3_MOD_IMPULSE].as.scalar;
    if(milliseconds<0||milliseconds>255||impulse<0||impulse>255||impulse!=trunc(impulse))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native user command exceeds its source byte ABI");
    double moves[]={v[Q3_MOD_FORWARD].as.scalar*200,v[Q3_MOD_SIDE].as.scalar*200,v[Q3_MOD_UP].as.scalar*200};
    if(!rerelease&&v[Q3_MOD_JUMP].as.scalar!=0) moves[2]=fmax(200,moves[2]);
    memset(s->command,0,sizeof(s->command));s->command[0]=(uint8_t)milliseconds;
    s->command[1]=(uint8_t)((v[Q3_MOD_ATTACK].as.scalar!=0?1:0)|(rerelease&&v[Q3_MOD_JUMP].as.scalar!=0?8:0)|(rerelease&&moves[2]<0?16:0));
    qa_vec3 aim=v[Q3_MOD_VIEW_ANGLES].as.vector;double angles[]={aim.x,aim.y,aim.z};
    for(size_t i=0;i<3;++i) {
        if(rerelease) {float f=(float)angles[i];uint32_t bits;memcpy(&bits,&f,4);qa_store_u32le(s->command+4+i*4,bits);}
        else {double word=trunc(angles[i]*65536/360);if(!isfinite(word)||fabs(word)>9007199254740991.0) return application_fail(e,QA_ERROR_ARGUMENT,"Native user command aim exceeds its source integer ABI");
            qa_store_u16le(s->command+2+i*2,(uint16_t)(uint32_t)fmod(fmod(word,65536)+65536,65536));}
    }
    for(size_t i=0;i<(rerelease?2u:3u);++i) {
        if(!isfinite(moves[i])||(!rerelease&&(moves[i]<INT16_MIN||moves[i]>INT16_MAX))||!isfinite((float)moves[i]))
            return application_fail(e,QA_ERROR_ARGUMENT,"Native user command movement exceeds its actual source ABI");
        if(rerelease) {float f=(float)moves[i];uint32_t bits;memcpy(&bits,&f,4);qa_store_u32le(s->command+16+i*4,bits);}
        else qa_store_u16le(s->command+8+i*2,(uint16_t)(int16_t)trunc(moves[i]));
    }
    if(rerelease) qa_store_u32le(s->command+24,(uint32_t)application_native_q2_stages_frame(s->owner->engine));
    else s->command[14]=(uint8_t)impulse;
    *out=(application_native_callback_inputs){values,count,{s->command,rerelease?28u:16u}};return true;
}
bool application_native_q2_input_values(struct application_native_q2 *n,qa_actor_id actor,
    application_native_callback_value values[Q3_MOD_VALUE_COUNT],application_native_callback_inputs *out,qa_error *e)
{
    if(!n||!n->callbacks||!values||!out||!application_native_q2_callbacks_storage_current(n->callbacks,e)||
        !qa_actors_get(qa_session_actors(n->provider->application->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native source inputs require their actual live actor and callback owner");
    struct application_native_q2_input *s=n->stages?n->stages->inputs:NULL;
    if(s&&s->executing) {
        if(!input_values(s,values,out,e)) return false;
        size_t i=0;while(i<out->count&&strcmp(values[i].name,"self")) ++i;
        if(i==out->count) {
            if(i==Q3_MOD_VALUE_COUNT) return application_fail(e,QA_ERROR_ARGUMENT,"Native source input map has no room for self");
            ++out->count;
        }
        values[i]=(application_native_callback_value){.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=actor};
        return true;
    }
    qa_source_frame frame;
    if(!application_native_q2_stages_time_read(n,&frame,e))return false;
    values[0]=(application_native_callback_value){.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=actor};
    values[1]=(application_native_callback_value){.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,
        .value.number=(double)frame.time_ns/1e9};
    *out=(application_native_callback_inputs){values,2,{0}};return true;
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
    native_input_handler *handler=context;
    if(!current(handler->owner,e)||native!=instance(handler->owner->engine)||count!=handler->count)
        return application_fail(e,QA_ERROR_ARGUMENT,"Native input output lost its retained handler invocation");
    for(native_input_output *o=handler->subscribers;o;o=o->subscriber_next) {
        if(!input_live(o->input)) continue;
        if(!input_current(o->input,e)) return false;
        if(o->self_index<count&&arguments[o->self_index].type==QA_NATIVE_ADDRESS&&arguments[o->self_index].as.address==o->self)
            o->called=true;
    }
    return application_native_q2_source_original(handler->owner->engine,binding,arguments,count,result,e);
}
static bool handler_open(native_input_output *output,qa_native_address address,
    const qa_native_signature *signature,qa_error *e)
{
    struct application_native_q2_stages *owner=output->input->owner;
    native_input_handler *handler=owner->handlers;
    while(handler&&handler->address!=address) handler=handler->next;
    if(handler) {
        if(handler->count!=signature->parameter_count)
            return application_fail(e,QA_ERROR_FORMAT,"Native output handler has conflicting declared ABI counts");
        for(size_t i=0;i<handler->count;++i) if(handler->parameters[i]!=signature->parameters[i].kind)
            return application_fail(e,QA_ERROR_FORMAT,"Native output handler has conflicting declared ABI values");
    } else {
        handler=calloc(1,sizeof(*handler));
        if(!handler) return application_fail(e,QA_ERROR_MEMORY,"Retaining native input handler subscribers");
        handler->owner=owner;handler->address=address;handler->count=signature->parameter_count;
        for(size_t i=0;i<handler->count;++i) handler->parameters[i]=signature->parameters[i].kind;
        handler->next=owner->handlers;owner->handlers=handler;
    }
    output->handler=handler;output->subscriber_next=handler->subscribers;handler->subscribers=output;
    return handler->binding||qa_native_observe_entry(instance(owner->engine),address,signature,
        output_handler,handler,&handler->binding,e);
}
static bool outputs_close(native_input_output **list,qa_error *e)
{
    while(*list) {
        native_input_output *o=*list;
        if(o->handler) {
            native_input_handler *handler=o->handler;
            bool last=handler->subscribers==o&&!o->subscriber_next;
            if(last&&handler->binding&&!qa_native_unobserve_entry(handler->binding,e)) return false;
            native_input_output **subscriber=&handler->subscribers;
            while(*subscriber!=o) subscriber=&(*subscriber)->subscriber_next;
            *subscriber=o->subscriber_next;
            if(last) {
                native_input_handler **link=&handler->owner->handlers;
                while(*link!=handler) link=&(*link)->next;
                *link=handler->next;free(handler);
            }
        }
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
                !handler_open(o,address,&signature,e)) return false;
        } else return application_fail(e,QA_ERROR_FORMAT,"Native input has an unknown output declaration");
    }
    return true;
}
static bool outputs_read(native_input_output *list,qa_error *e)
{
    for(native_input_output *o=list;o;o=o->next) {
        struct application_native_q2_input *s=o->input;struct application_native_q2 *n=s->owner->engine;
        const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);application_q3_mod_output result={0};
        if(!input_live(s)) break;
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
        if(!s->output(s->context,&result,e)) return false;
        if(!input_live(s)) break;
        if(!input_current(s,e)) return false;
    }
    return true;
}
static bool input_run(struct application_native_q2_input *s,bool before,qa_error *e)
{
    struct application_native_q2 *n=s->owner->engine;const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id rows=qa_json_get(d,qa_json_get(d,qa_json_root(d),"clients"),"input");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        if(!input_live(s)) break;
        qa_json_id binding=qa_json_at(d,rows,i);
        if(!qa_json_string_equal(d,qa_json_get(d,binding,"scope"),s->slice?"movement-slice":"client-command")||
            !qa_json_string_equal(d,qa_json_get(d,binding,"phase"),before?"before":"after")) continue;
        if(s->outputs) return application_fail(e,QA_ERROR_ARGUMENT,"Native input retains output observer cleanup");
        bool ok=!before||outputs_open(s,binding,&s->outputs,e);
        qa_json_id calls=qa_json_get(d,binding,"calls");
        for(size_t j=0;ok&&j<qa_json_size(d,calls);++j) {
            if(!input_live(s)) break;
            application_native_callback_value values[Q3_MOD_VALUE_COUNT];application_native_callback_inputs inputs;double result;
            ok=input_values(s,values,&inputs,e);
            if(ok) {
                ++s->executing;
                ok=application_native_q2_callbacks_call(n->callbacks,qa_json_at(d,calls,j),&inputs,&result,e);
                --s->executing;
            }
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
    bool admitted=false;for(uint32_t i=1;i<257;++i) admitted|=n->clients[i].reserved&&n->clients[i].connected&&
        !n->clients[i].denied&&!n->clients[i].disconnect_started&&qa_actor_id_equal(n->clients[i].actor,actor);
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
        store->field=field;
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
        if(restore&&input_live(s)) {
            qa_native_address address;size_t bytes;
            if(!field_address(s,store->field,&address,&bytes,e)) return false;
            if(address!=store->address||bytes!=store->bytes)
                return application_fail(e,QA_ERROR_ARGUMENT,"Native input private backing changed before restoration");
            if(!qa_native_write(instance(s->owner->engine),address,(qa_bytes){store->previous,bytes},e)) return false;
        }
        s->stores=store->next;free(store);
    }
    for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i) free(s->strings[i]);
    s->owner->inputs=s->outer;free(s);*slot=NULL;return true;
}
