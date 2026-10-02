#include "guest_q3_component_private.h"
#include "internal.h"
#include <limits.h>

bool q3component_storage(void *context,qa_error *e)
{
    application_q3_component *c=context;
    return (c&&c->vm&&c->host&&c->options.storage_current(c->options.context)&&
        qa_qvm_get_role(c->vm)==QA_QVM_GAME&&qa_qvm_get_abi(c->vm)==c->options.abi&&
        qa_sha256_equal(qa_qvm_digest(c->vm),qa_qvm_image_digest(c->options.image)))||
        q3records_fail(e,QA_ERROR_ARGUMENT,"Component storage left its retained GAME executor");
}
bool q3component_current(void *context,qa_error *e)
{
    application_q3_component *c=context;
    return q3component_storage(c,e)&&!c->closing&&!c->restoring&&c->options.current(c->options.context);
}
static bool publication_current(void *context)
{ qa_error e={0}; return q3component_current(context,&e); }
static bool information(void *context,uint32_t flags,qa_buffer *out,qa_error *e)
{
    application_q3_component *c=context;
    return q3component_current(c,e)&&qa_cvars_info(c->cvars,flags,1024,out,e);
}
static bool bound(void *context,qa_actor_id actor,uint32_t slot,bool owned,bool client,qa_error *e)
{
    application_q3_component *c=context;
    if(c->entity_record==SIZE_MAX) return true;
    return qa_q3_host_bind_actor(c->host,slot,actor,!owned,e)&&application_q3_component_source_bind(c->source,slot,actor,owned,client,e);
}
static bool released(void *context,qa_actor_id actor,qa_error *e)
{ application_q3_component *c=context; return application_q3_component_source_release_actor(c->source,actor,e); }
static bool match_read(void *context,qa_actor_id actor,qa_string_id *team,double *score,qa_error *e)
{ application_q3_component *c=context; return c->options.match_read&&c->options.match_read(c->options.context,actor,team,score,e); }
static bool match_write(void *context,qa_actor_id actor,bool team,qa_string_id id,double score,qa_error *e)
{ application_q3_component *c=context; return c->options.match_write&&c->options.match_write(c->options.context,actor,team,id,score,e); }
static bool pointer(void *context,qa_actor_id actor,const char *record,uint32_t *out,qa_error *e)
{ return application_q3_component_records_pointer(((application_q3_component *)context)->records,actor,record,out,e); }
static bool eligible(void *context,qa_actor_id actor)
{ return application_q3_component_records_eligible(((application_q3_component *)context)->records,actor); }
static bool client(void *context,qa_actor_id actor)
{
    application_q3_component *c=context;
    return c->options.clients.current&&c->options.clients.current(c->options.clients.context,actor)&&application_q3_component_records_live_client(c->records,actor);
}
static bool client_slot(void *context,qa_actor_id actor,int32_t *out,qa_error *e)
{ return application_q3_component_records_client_slot(((application_q3_component *)context)->records,actor,out,e); }
static bool player(void *context,qa_actor_id actor,qa_q3_player *out,qa_error *e)
{
    application_q3_component *c=context; uint32_t at;
    if(c->player_record==SIZE_MAX||!client(c,actor)||!pointer(c,actor,c->records->records[c->player_record].id,&at,e)) return false;
    return qa_qvm_read_player(c->vm,(int32_t)at,true,out,e);
}
static bool time_read(void *context,double *seconds,qa_error *e)
{ application_q3_component *c=context; if(!seconds||!q3component_current(c,e)) return false; *seconds=(double)c->milliseconds/1000; return true; }
static bool prepare(void *context,qa_error *e)
{ return application_q3_component_records_prepare(((application_q3_component *)context)->records,e); }
static bool enter(void *context,uint32_t at,const int32_t *words,size_t n,void **scope,qa_error *e)
{ return application_q3_component_records_enter(((application_q3_component *)context)->records,at,words,n,scope,e); }
static bool leave(void *context,void **scope,bool ok,int32_t result,qa_error *e)
{
    application_q3_component *c=context;
    if(!application_q3_component_records_leave(c->records,scope,ok,result,e)) return false;
    return q3component_player_events_publish(c,ok,e);
}
static bool source_metadata(application_q3_component *c,qa_error *e)
{
    qa_json_document *d=NULL; if(!qa_json_parse(application_q3_mod_declaration(c->profile),&d,e)) return false;
    qa_json_id source=qa_json_get(d,qa_json_root(d),"sourceActors"); c->has_source=source!=QA_JSON_NONE;
    bool ok=true;
    if(c->has_source) {
        uint64_t allocate,release,argument,inuse; qa_json_id release_row=qa_json_get(d,source,"release");
        ok=qa_json_u64(d,qa_json_get(d,source,"allocate"),&allocate,e)&&qa_json_u64(d,qa_json_get(d,release_row,"entry"),&release,e)&&
            qa_json_u64(d,qa_json_get(d,release_row,"argument"),&argument,e)&&qa_json_u64(d,qa_json_get(d,source,"inuse"),&inuse,e);
        size_t count; const qa_qvm_instruction *code=qa_qvm_image_instructions(c->options.image,&count);
        if(ok) ok=allocate<count&&release<count&&code[allocate].opcode==QA_QVM_ENTER&&code[release].opcode==QA_QVM_ENTER&&argument<62&&
            c->entity_record!=SIZE_MAX&&inuse>=qa_qvm_shared_entity_bytes(c->options.abi)&&inuse<=c->records->records[c->entity_record].stride-4;
        if(ok) { c->allocate_entry=(uint32_t)allocate; c->release_entry=(uint32_t)release; c->release_argument=(uint32_t)argument; c->inuse=(uint32_t)inuse; }
        else if(e&&e->code==QA_OK) q3records_fail(e,QA_ERROR_FORMAT,"Component source actor lifecycle exceeds its original layout");
    }
    qa_json_destroy(d); return ok;
}
bool application_q3_component_create(const application_q3_component_options *o,bool restoring,application_q3_component **out,qa_error *e)
{
    if(!o||!out||*out||!o->program||!o->declaration||!o->program_path||!o->image||!o->current||!o->storage_current||!o->generation||
        !o->host.session||!o->host.world||!o->host.owner||!o->host.service_owner||!o->host.mounts||o->host.role!=QA_QVM_GAME||o->host.abi!=o->abi||
        !o->combat||!o->inventory||o->host.cvars||o->host.console||o->host.frontend_lifetime||
        !qa_sha256_equal(&o->program_digest,qa_resource_digest(o->program))||!qa_sha256_equal(&o->program_digest,qa_qvm_image_digest(o->image))||
        !qa_sha256_equal(&o->declaration_digest,qa_resource_digest(o->declaration)))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component construction requires its genuinely retained artifacts and source namespace");
    application_q3_component *c=calloc(1,sizeof(*c)); if(!c) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining external component GAME owner");
    c->options=*o; c->restoring=restoring; c->entity_record=c->player_record=SIZE_MAX; *out=c;
    qa_resource_retain(o->program); qa_resource_retain(o->declaration); qa_qvm_image_retain(o->image);
    if(!application_q3_mod_profile_create(o->image,o->abi,o->program_path,qa_resource_bytes(o->declaration),qa_session_strings(o->host.session),&c->profile,e)) return false;
    qa_json_document *declaration=NULL;
    if(!qa_json_parse(application_q3_mod_declaration(c->profile),&declaration,e)) return false;
    c->scene=qa_json_string_equal(declaration,qa_json_get(declaration,qa_json_get(declaration,qa_json_root(declaration),"presentation"),"runtime"),"qvm-scene");
    qa_json_destroy(declaration);
    application_q3_component_source_options source={.session=o->host.session,.owner=o->host.owner,.generation=o->generation,.image=o->image,.abi=o->abi,.context=c,.current=publication_current,.information=information,.visibility=o->visibility,.scene=c->scene};
    if(!application_q3_component_source_create(&source,&c->source,e)||!q3component_services(c,e)) return false;
    c->lower=qa_q3_host_qvm_options(c->host,QA_QVM_INTERPRETED);
    qa_qvm_options vm=c->lower; vm.context=c;
    /* Services installs the actual wrappers before create. */
    extern bool q3component_syscall(void *,const qa_qvm_call *,int32_t,int32_t *,qa_error *);
    extern bool q3component_host_checkpoint(void *,qa_buffer *,qa_error *);
    extern bool q3component_host_restore(void *,qa_bytes,qa_error *);
    vm.syscall=q3component_syscall; vm.checkpoint=q3component_host_checkpoint; vm.restore=q3component_host_restore;
    if(!qa_qvm_create(o->image,&vm,&c->vm,e)||!qa_q3_host_attach_qvm(c->host,c->vm,e)) return false;
    application_q3_component_records_options records={.profile=c->profile,.vm=c->vm,.image=o->image,.session=o->host.session,.world=o->host.world,.combat=o->combat,.inventory=o->inventory,
        .strings=qa_session_strings(o->host.session),.context=c,.storage_current=q3component_storage,.match_read=match_read,.match_write=match_write,.inventory_write=o->application?q3component_pickup_write:NULL,.bound=bound,.released=released,
        .lifecycle_begin=q3component_lifecycle_begin,.lifecycle=q3component_lifecycle};
    if(!application_q3_component_records_create(&records,&c->records,e)) return false;
    c->entity_record=c->records->entity_record; c->player_record=c->records->player_record; c->maximum=c->records->client_maximum;
    if(c->entity_record!=SIZE_MAX) {
        component_record *entity=c->records->records+c->entity_record;
        component_record *state=c->player_record!=SIZE_MAX?c->records->records+c->player_record:NULL;
        qa_q3_host_game_data data={.entity_count=entity->capacity,.entity_stride=entity->stride,.entities_address=entity->address,
            .client_count=c->maximum,.client_stride=state?state->stride:0,.clients_address=state?state->address:0};
        if(!qa_q3_host_game_data_bind(c->host,c->vm,&data,e)) return false;
    }
    if(!source_metadata(c,e)||!q3component_bootstrap_profile(c,e)||!q3component_frame_profile(c,e)||!qa_strings_intern_cstr(qa_session_strings(o->host.session),"qvm:mod-actor",&c->definition,e)) return false;
    if(c->maximum&&(!o->clients.current||!o->clients.userinfo||!o->clients.set_userinfo||!o->clients.command||!o->clients.drop))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Declared component clients lack their real canonical client services");
    application_q3_mod_services services={.context=c,.pickups=o->application?o->application->pickups:NULL,.current=q3component_current,.storage_current=q3component_storage,.pointer=pointer,.eligible_actor=eligible,.live_client=client,
        .client_slot=client_slot,.player_state=player,.time=time_read,.source_prepare=prepare,.source_enter=enter,.source_leave=leave};
    memcpy(services.operations,o->operations,sizeof(services.operations));
    if(!application_q3_mod_create(c->profile,c->vm,o->host.session,o->host.owner,o->combat,&services,restoring,&c->mod,e)||
        !q3component_actors_create(c,e)||!q3component_items_create(c,e)||!q3component_player_events_create(c,e)||!q3component_bind_hooks(c,e)||
        !qa_qvm_bind_resolver(c->vm,q3component_actor_resolve,c,&c->actor_resolver,e)) return false;
    /* Restored publication attaches once executable activation is qualified. */
    if(!restoring&&!application_q3_component_source_attach(c->source,c->vm,c->host,e)) return false;
    return true;
}
bool application_q3_component_idle(const application_q3_component *c)
{ return c&&!c->busy&&!c->calls&&!c->draining&&(!c->vm||qa_qvm_can_destroy(c->vm))&&(!c->records||application_q3_component_records_idle(c->records))&&
    (!c->mod||application_q3_mod_idle(c->mod))&&application_q3_mod_items_idle(c->items)&&application_q3_mod_actors_idle(c->actor_semantics)&&(!c->source||application_q3_component_source_idle(c->source))&&qa_console_idle(c->console); }
bool application_q3_component_initialize(application_q3_component *c,qa_error *e)
{
    if(!c||!q3component_current(c,e)||c->initialized||!application_q3_component_idle(c)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component Initialize requires its fresh actual executor");
    qa_clock_state clock;
    if(!qa_session_clock(c->options.host.session,c->options.host.owner,&clock)||clock.frame.kind!=QA_CLOCK_Q3||clock.frame.time_ns/1000000>INT32_MAX)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component Initialize requires its admitted actual SOURCE clock");
    c->milliseconds=(int32_t)(clock.frame.time_ns/1000000);
    c->busy=true; application_q3_mod_inputs inputs={0}; inputs.values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=(double)c->milliseconds/1000};
    bool ok=true;
    for(size_t i=0;ok&&i<c->initial_store_count;++i) {
        uint8_t bytes[4]; uint32_t bits; memcpy(&bits,&c->initial_stores[i].value,4); qa_store_u32le(bytes,bits);
        ok=qa_qvm_write(c->vm,c->initial_stores[i].address,(qa_bytes){bytes,4},e);
    }
    if(ok) ok=application_q3_mod_stage_run(c->mod,Q3_MOD_INITIALIZE,&inputs,e)&&application_q3_component_records_defaults(c->records,e);
    if(ok) c->initialized=true;
    c->busy=false; return ok;
}
application_q3_component_source *application_q3_component_source_read(application_q3_component *c) { return c?c->source:NULL; }
application_q3_mod *application_q3_component_mod(application_q3_component *c) { return c?c->mod:NULL; }
qa_console *application_q3_component_console(application_q3_component *c,qa_cvars **cvars) { if(cvars) *cvars=c?c->cvars:NULL; return c?c->console:NULL; }
const application_q3_mod_profile *application_q3_component_profile(const application_q3_component *c) { return c?c->profile:NULL; }
