#include "guest_q3_component_private.h"
#include <limits.h>

static application_q3_mod_inputs inputs(application_q3_component *c,qa_actor_id actor,double elapsed)
{
    application_q3_mod_inputs result={0};
    result.values[Q3_MOD_SELF]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=actor};
    result.values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=(double)c->milliseconds/1000};
    result.values[Q3_MOD_ELAPSED]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=elapsed};
    return result;
}
static bool client_current(application_q3_component *c,qa_actor_id actor,qa_error *e)
{
    if(!c||!c->initialized||!c->options.clients.current||!c->options.clients.current(c->options.clients.context,actor)||!q3records_live(c->records,actor))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component lifecycle lost its actual canonical client");
    return q3component_current(c,e);
}
bool application_q3_component_client_current(const application_q3_component *c,qa_actor_id actor)
{
    qa_error e={0};
    return c&&c->initialized&&application_q3_component_idle(c)&&c->options.clients.current&&
        c->options.clients.current(c->options.clients.context,actor)&&q3records_live(c->records,actor)&&
        application_q3_component_records_live_client(c->records,actor)&&q3component_current((void *)c,&e);
}
bool application_q3_component_client_bound(const application_q3_component *c,qa_actor_id actor,bool *present,qa_error *e)
{
    if(!c||!present||!q3component_storage((void *)c,e)) return false;
    *present=false;
    component_actor *row=q3records_actor(c->records,actor);
    if(!row||!row->client||!row->admitted) return true;
    if(!application_q3_component_client_current(c,actor))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component userinfo listener lost its actual admitted client");
    *present=true; return true;
}
bool application_q3_component_admit(application_q3_component *c,qa_actor_id actor,qa_error *e)
{
    if(!client_current(c,actor,e)) return false;
    component_actor *existing=q3records_actor(c->records,actor);
    if(existing&&existing->client&&existing->admitted&&!existing->retired) {
        if(c->items&&!application_q3_mod_items_admit(c->items,actor,e)) return false;
        return application_q3_mod_admit(c->mod,actor,e)&&q3component_player_events_track(c,actor,e);
    }
    if(!application_q3_component_idle(c)) return q3records_fail(e,QA_ERROR_ARGUMENT,"New component client admission requires returned source execution");
    c->busy=true; uint32_t slot;
    bool ok=application_q3_component_records_reserve_client(c->records,actor,&slot,e)&&application_q3_mod_reserve(c->mod,actor,e);
    if(ok) ok=application_q3_component_records_bind(c->records,actor,slot,false,true,e);
    component_actor *row=ok?q3records_actor(c->records,actor):NULL; bool entered=row&&row->admitted;
    if(ok&&!entered) {
        /* As in the source client binder, the actual source admission is
         * marked before its calls can reenter the same client. */
        ok=application_q3_component_records_admitted(c->records,actor,true,e);
        application_q3_mod_inputs values=inputs(c,actor,0);
        if(ok) ok=application_q3_mod_stage_run(c->mod,Q3_MOD_CLIENT_ADMIT,&values,e);
    }
    if(ok) ok=client_current(c,actor,e)&&(!c->items||application_q3_mod_items_admit(c->items,actor,e))&&
        application_q3_mod_admit(c->mod,actor,e)&&q3component_player_events_track(c,actor,e)&&application_q3_component_source_publish(c->source,c->milliseconds,false,e);
    c->busy=false; return ok;
}
bool application_q3_component_userinfo(application_q3_component *c,qa_actor_id actor,qa_error *e)
{
    if(!client_current(c,actor,e)||!application_q3_component_records_live_client(c->records,actor)||!application_q3_component_idle(c))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component userinfo requires its already admitted returned client");
    c->busy=true; application_q3_mod_inputs values=inputs(c,actor,0);
    bool ok=application_q3_mod_stage_run(c->mod,Q3_MOD_CLIENT_USERINFO,&values,e)&&client_current(c,actor,e)&&application_q3_component_source_publish(c->source,c->milliseconds,false,e);
    c->busy=false; return ok;
}
bool application_q3_component_disconnect(application_q3_component *c,qa_actor_id actor,qa_error *e)
{
    if(!c||!q3component_current(c,e)||!application_q3_component_idle(c)) return false;
    component_actor *row=q3records_actor(c->records,actor); if(!row||!row->client) return true;
    c->busy=true; application_q3_mod_inputs values=inputs(c,actor,0);
    bool ok=row->disconnected||application_q3_mod_stage_run(c->mod,Q3_MOD_CLIENT_DISCONNECT,&values,e);
    row=q3records_actor(c->records,actor);
    if(ok&&row) row->disconnected=true;
    if(ok&&c->items) ok=application_q3_mod_items_release(c->items,actor,e);
    if(ok) ok=application_q3_mod_release_actor(c->mod,actor,e);
    row=q3records_actor(c->records,actor);
    if(ok&&row&&row->projected&&c->entity_record!=SIZE_MAX) {
        uint32_t source_slot=row->slot;
        ok=qa_q3_host_detach_actor(c->host,source_slot,actor,e);
        row=q3records_actor(c->records,actor);
        if(ok&&row) row->projected=false;
    }
    if(ok) { q3component_player_events_release(c,actor); ok=application_q3_component_records_release(c->records,actor,e); }
    if(ok) ok=application_q3_component_source_publish(c->source,c->milliseconds,false,e);
    c->busy=false; return ok;
}
bool application_q3_component_command(application_q3_component *c,qa_actor_id actor,const qa_command_invocation *command,bool *handled,qa_error *e)
{
    if(!command||!handled||!client_current(c,actor,e)||!application_q3_component_records_live_client(c->records,actor)) return false;
    int32_t slot;
    if(!application_q3_component_records_client_slot(c->records,actor,&slot,e)) return false;
    const qa_command_invocation *previous=c->arguments; c->arguments=command;
    int32_t words[]={6,slot},result; bool ok=q3component_call(c,0,words,2,&result,e); c->arguments=previous;
    if(ok) { *handled=true; ok=client_current(c,actor,e)&&application_q3_component_source_publish(c->source,c->milliseconds,false,e); }
    return ok;
}
bool application_q3_component_frame(application_q3_component *c,const qa_source_frame *frame,qa_error *e)
{
    if(!c||!frame||!q3component_current(c,e)||!c->initialized||!application_q3_component_idle(c)||
        frame->provider!=c->options.host.owner||frame->kind!=QA_RULESET_Q3||frame->time_ns/1000000>INT32_MAX)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component frame requires its actual admitted SOURCE interval");
    qa_source_frame actual;
    if(!qa_session_active_frame(c->options.host.session,c->options.host.owner,&actual)||actual.number!=frame->number||actual.time_ns!=frame->time_ns||actual.elapsed_ns!=frame->elapsed_ns)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component frame does not name its entered session interval");
    c->milliseconds=(int32_t)(frame->time_ns/1000000); c->busy=true; bool ok=true;
    /* Snapshot the source-slot order; nested source calls may allocate or
     * retire rows while the declared update executes. */
    size_t count=c->records->actor_count; component_actor *rows=count?malloc(count*sizeof(*rows)):NULL;
    if(count&&!rows) ok=q3records_fail(e,QA_ERROR_MEMORY,"Retaining true component frame actor order");
    if(ok&&count) memcpy(rows,c->records->actors,count*sizeof(*rows));
    for(size_t i=1;ok&&i<count;++i) { component_actor value=rows[i]; size_t j=i; while(j&&rows[j-1].slot>value.slot) { rows[j]=rows[j-1]; --j; } rows[j]=value; }
    for(size_t i=0;ok&&i<count;++i) if(rows[i].owned&&!rows[i].retired&&q3records_live(c->records,rows[i].actor)) {
        application_q3_mod_inputs values=inputs(c,rows[i].actor,(double)frame->elapsed_ns/1e9);
        ok=application_q3_mod_stage_run(c->mod,Q3_MOD_SOURCE_UPDATE,&values,e);
    }
    bool clients=true;
    if(ok&&c->has_actor_frame) {
        c->actor_frame_active=true; c->actor_frame_completed=false; application_q3_mod_inputs values=inputs(c,(qa_actor_id){0},(double)frame->elapsed_ns/1e9);
        ok=application_q3_mod_stage_run(c->mod,Q3_MOD_SOURCE_FRAME,&values,e);
        clients=c->actor_frame_completed; c->actor_frame_active=false;
    }
    if(ok) {
        free(rows); count=c->records->actor_count;
        rows=count?malloc(count*sizeof(*rows)):NULL;
        if(count&&!rows) ok=q3records_fail(e,QA_ERROR_MEMORY,"Retaining reached component client frame order");
        if(ok&&count) memcpy(rows,c->records->actors,count*sizeof(*rows));
        for(size_t i=1;ok&&i<count;++i) { component_actor value=rows[i]; size_t j=i; while(j&&rows[j-1].slot>value.slot) { rows[j]=rows[j-1]; --j; } rows[j]=value; }
    }
    for(size_t i=0;ok&&clients&&i<count;++i) if(rows[i].client&&application_q3_component_records_live_client(c->records,rows[i].actor)) {
        application_q3_mod_inputs values=inputs(c,rows[i].actor,(double)frame->elapsed_ns/1e9);
        ok=application_q3_mod_stage_run(c->mod,Q3_MOD_CLIENT_FRAME,&values,e);
    }
    free(rows);
    if(ok) ok=application_q3_component_source_publish(c->source,c->milliseconds,false,e);
    c->busy=false; return ok;
}
bool application_q3_component_activate(application_q3_component *c,qa_error *e)
{
    if(!c||!c->initialized||!q3component_current(c,e)||!application_q3_mod_activate(c->mod,e)) return false;
    for(size_t i=0;i<c->records->actor_count;++i) if(c->records->actors[i].client&&c->records->actors[i].admitted&&!c->records->actors[i].retired)
        if(!application_q3_mod_admit(c->mod,c->records->actors[i].actor,e)) return false;
    return true;
}
bool application_q3_component_callbacks_register(application_q3_component *c,qa_error *e)
{ return c&&c->initialized&&q3component_current(c,e)&&application_q3_mod_callbacks_register(c->mod,e); }
