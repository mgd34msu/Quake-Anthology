#include "guest_q3_component_private.h"

static bool slot(application_q3_component *c,int32_t pointer,uint32_t *out,qa_error *e)
{
    if(c->entity_record==SIZE_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Component source has no entity record");
    component_record *record=c->records->records+c->entity_record; uint32_t at=(uint32_t)pointer;
    if(at<record->address||(uint64_t)at>=(uint64_t)record->address+(uint64_t)record->stride*record->capacity||(at-record->address)%record->stride)
        return q3records_fail(e,QA_ERROR_FORMAT,"Component source lifecycle pointer is outside its declared row");
    *out=(at-record->address)/record->stride; return true;
}
static component_actor *actor_at(application_q3_component *c,uint32_t slot)
{ for(size_t i=0;i<c->records->actor_count;++i) if(c->records->actors[i].slot==slot) return c->records->actors+i; return NULL; }
bool q3component_lifecycle_begin(void *context,uint32_t entry,const int32_t *words,size_t count,qa_error *e)
{
    application_q3_component *c=context; if(!c->has_source||entry!=c->release_entry) return true;
    uint32_t index;
    if(count<=c->release_argument||!slot(c,words[c->release_argument],&index,e)) return false;
    component_actor *actor=actor_at(c,index);
    return !actor||actor->owned||q3records_fail(e,QA_ERROR_ARGUMENT,"Component source cannot release a foreign actor without its owner continuation");
}
bool q3component_lifecycle(void *context,uint32_t entry,const int32_t *words,size_t count,int32_t result,qa_error *e)
{
    application_q3_component *c=context; if(!c->has_source||(entry!=c->allocate_entry&&entry!=c->release_entry)) return true;
    uint32_t index; int32_t pointer=result;
    if(entry==c->release_entry) { if(count<=c->release_argument) return q3records_fail(e,QA_ERROR_FORMAT,"Component source release lost its argument"); pointer=words[c->release_argument]; }
    if(!slot(c,pointer,&index,e)) return false;
    component_record *record=c->records->records+c->entity_record; uint8_t bytes[4];
    if(!qa_qvm_read(c->vm,record->address+index*record->stride+c->inuse,bytes,4,e)) return false;
    component_actor *row=actor_at(c,index);
    if(entry==c->allocate_entry) {
        if(index<c->maximum||row||!qa_load_i32le(bytes)) return q3records_fail(e,QA_ERROR_FORMAT,"Component allocator returned an occupied, inactive or reserved client row");
        qa_actor_id actor;
        if(!qa_session_allocate(c->options.host.session,c->options.host.owner,c->definition,true,index,&actor,e)) return false;
        /* Bind retains the allocated full actor before any fallible body or
         * collision admission. Checked retirement can always reach it. */
        return application_q3_component_records_bind(c->records,actor,index,true,false,e);
    }
    if(!row||qa_load_i32le(bytes)) return true;
    if(!row->owned) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component source removed a foreign actor");
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(c->options.host.session),row->actor);
    if(!actual) return true;
    qa_actor_record released=*actual;
    if(!qa_session_release(c->options.host.session,released.id,e)) return false;
    return application_q3_component_actor_released(c,released,e);
}
static bool finish_call(application_q3_component *c,component_call_lease *lease,qa_error *e)
{
    if(lease->middleware&&!application_q3_mod_entry_end(&lease->middleware,lease->succeeded,lease->result,e)) return false;
    if(lease->scope&&!application_q3_component_records_leave(c->records,&lease->scope,lease->succeeded,lease->result,e)) return false;
    return true;
}
static bool drain(application_q3_component *c,qa_error *e)
{
    if(c->draining) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component cleanup is already consuming its retained source call");
    c->draining=true;
    while(c->calls) {
        component_call_lease *lease=c->calls;
        if(!finish_call(c,lease,e)) { c->draining=false; return false; }
        c->calls=lease->next; free(lease);
    }
    c->draining=false;
    return true;
}
bool q3component_call(application_q3_component *c,uint32_t entry,const int32_t *words,size_t count,int32_t *result,qa_error *e)
{
    if(!q3component_current(c,e)||!drain(c,e)||!application_q3_component_records_prepare(c->records,e)) return false;
    component_call_lease *lease=calloc(1,sizeof(*lease)); if(!lease) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining actual component call cleanup");
    bool ok=application_q3_component_records_enter(c->records,entry,words,count,&lease->scope,e);
    if(ok) ok=qa_qvm_invoke(c->vm,entry,words,count,&lease->result,e);
    lease->succeeded=ok; int32_t raw=lease->result; qa_error first=e?*e:(qa_error){0},cleanup={0};
    bool closed=finish_call(c,lease,&cleanup);
    if(!closed) { lease->next=c->calls; c->calls=lease; } else free(lease);
    if(!ok) { if(e) *e=first; return false; }
    if(!closed) { if(e) *e=cleanup; return false; }
    if(result) *result=raw;
    return true;
}
static bool hook(void *context,const qa_qvm_call *call,int32_t *result,qa_error *e)
{
    component_hook *binding=context; application_q3_component *c=binding->owner;
    if(!q3component_current(c,e)) return false;
    bool source=call->caller_instruction!=UINT32_MAX;
    component_call_lease *lease=calloc(1,sizeof(*lease)); if(!lease) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining actual component function scope");
    int32_t words[62]; size_t count=call->argument_count;
    bool ok=count<=62;
    for(size_t i=0;ok&&i<count;++i) ok=qa_qvm_call_argument(call,i,words+i,e);
    if(ok&&source&&(binding->allocate||binding->release)) ok=application_q3_component_records_prepare(c->records,e)&&application_q3_component_records_enter(c->records,call->instruction,words,count,&lease->scope,e);
    if(ok&&binding->middleware) ok=application_q3_mod_entry_begin(c->mod,call,&lease->middleware,e);
    if(ok) ok=binding->frame?q3component_frame_proceed(c,call,&lease->result,e):qa_qvm_proceed(call,&lease->result,e);
    lease->succeeded=ok; int32_t raw=lease->result; qa_error first=e?*e:(qa_error){0},cleanup={0};
    bool closed=finish_call(c,lease,&cleanup);
    if(!closed) { lease->next=c->calls; c->calls=lease; } else free(lease);
    if(!ok) { if(e) *e=first; return false; }
    if(!closed) { if(e) *e=cleanup; return false; }
    *result=raw; return true;
}
static bool hook_add(application_q3_component *c,uint32_t entry,bool middleware,bool allocate,bool release,qa_error *e)
{
    for(size_t i=0;i<c->hook_count;++i) if(c->hooks[i].entry==entry) { c->hooks[i].middleware|=middleware; c->hooks[i].allocate|=allocate; c->hooks[i].release|=release; return true; }
    component_hook *row=c->hooks+c->hook_count++; *row=(component_hook){.owner=c,.entry=entry,.middleware=middleware,.allocate=allocate,.release=release};
    (void)e; return true;
}
bool q3component_bind_hooks(application_q3_component *c,qa_error *e)
{
    size_t entries=application_q3_mod_entry_count(c->mod);
    c->hooks=calloc(entries+(c->has_source?2:0)+(c->has_actor_frame?1:0),sizeof(*c->hooks));
    if((entries||c->has_source||c->has_actor_frame)&&!c->hooks) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining complete component function union");
    for(size_t i=0;i<entries;++i) { uint32_t entry; if(!application_q3_mod_entry_instruction(c->mod,i,&entry)||!hook_add(c,entry,true,false,false,e)) return false; }
    if(c->has_source&&(!hook_add(c,c->allocate_entry,false,true,false,e)||!hook_add(c,c->release_entry,false,false,true,e))) return false;
    if(c->has_actor_frame) {
        if(!hook_add(c,c->frame_entry,false,false,false,e)) return false;
        for(size_t i=0;i<c->hook_count;++i) if(c->hooks[i].entry==c->frame_entry) c->hooks[i].frame=true;
    }
    for(size_t i=0;i<c->hook_count;++i) if(!qa_qvm_bind_function(c->vm,c->hooks[i].entry,true,hook,c->hooks+i,&c->hooks[i].id,e)) return false;
    return true;
}
bool q3component_descriptors(application_q3_component *c,qa_qvm_saved_function *out,qa_error *e)
{
    if(!c||(c->hook_count&&!out)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component callback inventory requires its real owner");
    for(size_t i=0;i<c->hook_count;++i) {
        component_hook *row=c->hooks+i;
        if(!row->id) return q3records_fail(e,QA_ERROR_FORMAT,"Component physical callback inventory is incomplete");
        out[i]=(qa_qvm_saved_function){.binding=row->id,.instruction=row->entry,.host_invocations=true,.hook=hook,.context=row};
    }
    return true;
}
bool application_q3_component_actor_released(application_q3_component *c,qa_actor_record record,qa_error *e)
{
    if(!c||!c->records) return true;
    if(c->mod&&!application_q3_mod_release_actor(c->mod,record.id,e)) return false;
    if(!application_q3_component_records_release(c->records,record.id,e)) return false;
    return !c->host||qa_q3_host_actor_released(c->host,record,e);
}
bool application_q3_component_destroy(application_q3_component **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    application_q3_component *c=*slot; c->closing=true;
    if(c->calls&&!drain(c,e)) return false;
    if(c->busy||(c->vm&&!qa_qvm_can_destroy(c->vm))||(c->source&&!application_q3_component_source_idle(c->source))||!qa_console_idle(c->console))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component lifetime has active source or presentation users");
    /* Returned refused mod scopes retry while their real RAM and canonical
     * bindings still exist. Their false idle bit is not an execution lock. */
    if(c->mod&&!application_q3_mod_idle(c->mod)&&!application_q3_mod_destroy(&c->mod,e)) return false;
    for(size_t i=0;c->records&&i<c->records->actor_count;) {
        component_actor row=c->records->actors[i];
        if(row.owned&&q3records_live(c->records,row.actor)) {
            const qa_actor_record *actual=qa_actors_get(qa_session_actors(c->options.host.session),row.actor); qa_actor_record released=*actual;
            if(!qa_session_release(c->options.host.session,row.actor,e)||!application_q3_component_actor_released(c,released,e)) return false;
        } else {
            if(c->mod&&!application_q3_mod_release_actor(c->mod,row.actor,e)) return false;
            if(!application_q3_component_records_release(c->records,row.actor,e)) return false;
        }
        if(i<c->records->actor_count&&qa_actor_id_equal(c->records->actors[i].actor,row.actor)) ++i;
    }
    if(c->mod&&!application_q3_mod_destroy(&c->mod,e)) return false;
    if(c->records&&!application_q3_component_records_destroy(&c->records,e)) return false;
    if(c->source&&!application_q3_component_source_destroy(&c->source,e)) return false;
    if(c->host&&!qa_q3_host_destroy_ready(c->host)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component host has retained service users");
    for(size_t i=0;i<c->hook_count;++i) if(c->hooks[i].id) { if(!qa_qvm_unbind(c->vm,c->hooks[i].id,e)) return false; c->hooks[i].id=0; }
    if(c->vm) { if(!qa_qvm_destroy(c->vm,e)) return false; c->vm=NULL; qa_q3_host_qvm_consumed(c->host); }
    if(c->host) { if(!qa_q3_host_destroy(c->host,e)) return false; c->host=NULL; }
    qa_console_destroy(c->console); qa_cvars_destroy(c->cvars); free(c->hooks); free(c->frame_branches); free(c->frame_locals); free(c->initial_stores);
    application_q3_mod_profile_destroy(c->profile); qa_qvm_image_release(c->options.image);
    qa_resource_release(c->options.program); qa_resource_release(c->options.declaration);
    free(c); *slot=NULL; return true;
}
