#include "guest_q3_component_private.h"
#include "native_q3_settings.h"

static bool source_config(void *context,uint32_t index,const char **out,qa_error *e)
{ return application_q3_component_source_configstring(((application_q3_component *)context)->source,index,out,e); }
static bool source_set_config(void *context,uint32_t index,const char *text,qa_error *e)
{
    application_q3_component *c=context;
    return application_q3_component_source_set_configstring(c->source,index,text,e);
}
static bool find_client(application_q3_component *,uint32_t,qa_actor_id *,bool *,qa_error *);
static bool source_command(void *context,int32_t slot,const char *text,qa_error *e)
{
    application_q3_component *c=context;
    qa_actor_id recipient={0};
    if(slot!=-1) {
        bool found=false;
        if(slot<0) return true;
        if(!find_client(c,(uint32_t)slot,&recipient,&found,e)) return false;
        if(!found) return true;
    }
    return application_q3_component_source_command(c->source,slot,text,e)&&
        c->options.source_command_event&&c->options.source_command_event(c->options.context,recipient,text,c->milliseconds,e);
}
static bool find_client(application_q3_component *c,uint32_t slot,qa_actor_id *actor,bool *found,qa_error *e)
{
    *found=false;
    for(size_t i=0;c->records&&i<c->records->actor_count;++i) {
        component_actor row=c->records->actors[i];
        if(row.slot==slot&&row.client&&!row.retired) {
            if(!c->options.clients.current||!c->options.clients.current(c->options.clients.context,row.actor))
                return q3records_fail(e,QA_ERROR_ARGUMENT,"Component client binding lost its retained physical source");
            *actor=row.actor; *found=true; return true;
        }
    }
    return true;
}
static bool actual_client(application_q3_component *c,uint32_t slot,qa_actor_id *actor,qa_error *e)
{
    bool found=false;
    if(!find_client(c,slot,actor,&found,e)) return false;
    if(found) return true;
    return q3records_fail(e,QA_ERROR_NOT_FOUND,"Component service has no actual bound client identity");
}
static bool userinfo(void *context,uint32_t slot,const char **out,qa_error *e)
{
    application_q3_component *c=context; qa_actor_id actor; bool found=false;
    if(!out||!find_client(c,slot,&actor,&found,e)) return false;
    if(!found) { *out=""; return true; }
    return c->options.clients.userinfo&&c->options.clients.userinfo(c->options.clients.context,actor,out,e);
}
static bool set_userinfo(void *context,uint32_t slot,const char *text,qa_error *e)
{ application_q3_component *c=context; qa_actor_id actor; return actual_client(c,slot,&actor,e)&&c->options.clients.set_userinfo&&c->options.clients.set_userinfo(c->options.clients.context,actor,text,e); }
static bool user_command(void *context,uint32_t slot,qa_q3_usercmd *out,qa_error *e)
{
    application_q3_component *c=context; qa_actor_id actor;
    if(!out||!actual_client(c,slot,&actor,e)) return false;
    application_q3_mod_inputs inputs={0}; bool found=false;
    if(!application_q3_mod_input_current(c->mod,actor,&inputs,&found,e)) return false;
    qa_q3_player state;
    if(!qa_q3_host_source_player(c->host,slot,&state,e)) return false;
    if(c->items) {
        int32_t requested=0; bool present=false;
        if(!application_q3_mod_items_requested(c->items,actor,&requested,&present,e)) return false;
        if(present) state.weapon=requested;
    }
    if(found) {
        if(!c->options.clients.active_command) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component usercmd has no actual active command converter");
        return c->options.clients.active_command(c->options.clients.context,actor,&inputs,&state,out,e);
    }
    return c->options.clients.command&&c->options.clients.command(c->options.clients.context,actor,&state,out,e);
}
static bool drop(void *context,uint32_t slot,const char *reason,qa_error *e)
{
    application_q3_component *c=context; qa_actor_id actor; bool found=false;
    if(!find_client(c,slot,&actor,&found,e)) return false;
    return !found||(c->options.clients.drop&&c->options.clients.drop(c->options.clients.context,c->options.host.owner,actor,reason,e));
}
static uint32_t milliseconds(void *context) { return (uint32_t)((application_q3_component *)context)->milliseconds; }
static int32_t calendar(void *context,qa_q3_host_calendar *out)
{
    application_q3_component *c=context;
    return c->options.host.common.calendar?c->options.host.common.calendar(c->options.host.common.context,out):-1;
}
static void print(void *context,const char *text)
{ application_q3_component *c=context; if(c->options.host.common.print) c->options.host.common.print(c->options.host.common.context,text); }
static void console_print(void *context,const qa_command_context *command,const char *text)
{ (void)command; print(context,text); }
static bool arguments(void *context,qa_native_host_command_view *out,qa_error *e)
{
    (void)e; application_q3_component *c=context; const qa_command_invocation *a=c->arguments;
    *out=(qa_native_host_command_view){.count=a?a->argc:0,.arguments=a?a->argv:NULL,.tail=a?a->args_text:""}; return true;
}
static bool command_current(void *context,const qa_command_context *command)
{
    application_q3_component *c=context; qa_error e={0};
    return command&&command->owner==c->options.host.command_context.owner&&command->dialect==QA_RULESET_Q3&&q3component_current(c,&e);
}
static bool command_capture(void *context,const qa_command_context *command,qa_command_context *out,qa_error *e)
{ if(!command_current(context,command)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component command left its private source namespace"); *out=*command; return true; }
static bool cheats(void *context)
{ application_q3_component *c=context; const qa_cvar_view *v=qa_cvars_find(c->cvars,"sv_cheats"); return v&&v->integer!=0; }
static bool read_script(void *context,const qa_command_context *command,const char *path,qa_bytes *out,void **lease,qa_error *e)
{
    application_q3_component *c=context; qa_resource *resource=NULL;
    if(!command_current(c,command)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component script source retired");
    if(!qa_vfs_acquire(c->options.host.mounts,path,&resource,NULL,e)) return false;
    *out=qa_resource_bytes(resource); *lease=resource; return true;
}
static void release_script(void *context,void *lease) { (void)context; qa_resource_release(lease); }
static qa_command_result console_command(void *context,const qa_command_invocation *command,qa_error *e)
{
    application_q3_component *c=context; if(!c->initialized) return QA_COMMAND_UNHANDLED;
    const qa_command_invocation *previous=c->arguments; c->arguments=command;
    int32_t words[]={9},result=0; bool ok=q3component_call(c,0,words,1,&result,e); c->arguments=previous;
    return !ok?QA_COMMAND_FAILED:result?QA_COMMAND_HANDLED:QA_COMMAND_UNHANDLED;
}
bool q3component_services(application_q3_component *c,qa_error *e)
{
    qa_cvar_options cvars={.dialect=QA_RULESET_Q3,
        .side=QA_CVAR_SIDE_SERVER,.role=QA_CVAR_ROLE_GAME,.user=c,.print=print,.cheats_allowed=cheats,
        .declaration_save_policy=application_native_q3_cvar_save_policy};
    c->cvars=qa_cvars_create_view(qa_application_cvars(c->options.application),&cvars,e); if(!c->cvars) return false;
    if(c->options.map_path) {
        const char *path=c->options.map_path;
        if(!strncmp(path,"maps/",5)) path+=5;
        size_t length=strlen(path);
        if(length>=4&&!strcmp(path+length-4,".bsp")) length-=4;
        char *name=malloc(length+1);
        if(!name) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining actual component map cvar");
        memcpy(name,path,length); name[length]=0;
        bool registered=qa_cvars_register(c->cvars,"mapname",name,QA_CVAR_SERVERINFO|QA_CVAR_READONLY,0,NULL,e)&&
            qa_cvars_declare_save_policy(c->cvars,"mapname",QA_CVAR_SAVE_SETTING,e)&&
            qa_cvars_set(c->cvars,"mapname",name,true,e);
        free(name); if(!registered) return false;
    }
    qa_console_options console={.context=c->options.host.command_context,.cvars=c->cvars,.user=c,.print=console_print,.read_script=read_script,.release_script=release_script,.source_command=console_command,.capture_context=command_capture,.context_active=command_current};
    c->options.host.command_context.cvar_view=qa_cvars_view_identity(c->cvars);
    console.context=c->options.host.command_context;
    c->console=qa_application_console(c->options.application);
    if(!qa_console_bind_source(c->console,&console,e)) return false;
    qa_q3_host_options host=c->options.host; host.cvars=c->cvars; host.console=c->console; host.engine_cvars=NULL;
    host.common=(qa_q3_host_common_services){.context=c,.print=print,.milliseconds=milliseconds,.arguments=arguments,.calendar=calendar};
    const char *entity,*player; uint32_t maximum=0; application_q3_mod_clients(c->profile,&maximum,&entity,&player);
    host.server=(qa_q3_host_server_services){.context=c,.maximum_clients=maximum,.configstring=source_config,.set_configstring=source_set_config,.send_command=source_command,
        .userinfo=userinfo,.set_userinfo=set_userinfo,.user_command=user_command,.drop_client=drop};
    return qa_q3_host_create(&host,&c->host,e);
}
bool q3component_syscall(void *context,const qa_qvm_call *call,int32_t trap,int32_t *result,qa_error *e)
{
    application_q3_component *c=context;
    if(!q3component_current(c,e)||!application_q3_component_records_prepare(c->records,e)) return false;
    int32_t code; bool engine;
    if(!qa_qvm_classify_syscall(QA_QVM_GAME,c->options.abi,trap,&code,&engine,e)) return false;
    if(engine&&code==15) {
        int32_t entity,count,stride,clients,client_stride;
        if(c->entity_record==SIZE_MAX||!qa_qvm_call_argument(call,1,&entity,e)||!qa_qvm_call_argument(call,2,&count,e)||
            !qa_qvm_call_argument(call,3,&stride,e)||!qa_qvm_call_argument(call,4,&clients,e)||!qa_qvm_call_argument(call,5,&client_stride,e)) return false;
        component_record *record=c->records->records+c->entity_record;
        if((uint32_t)entity!=record->address||(uint32_t)stride!=record->stride||count<0||(uint32_t)count>record->capacity)
            return q3records_fail(e,QA_ERROR_FORMAT,"Component LocateGameData differs from its held declaration");
        bool found=false; for(size_t i=0;i<c->records->record_count;++i) {
            component_record *row=c->records->records+i;
            if(row->address==(uint32_t)clients&&row->stride==(uint32_t)client_stride&&(c->player_record==SIZE_MAX||i==c->player_record)) found=true;
        }
        if(!found) return q3records_fail(e,QA_ERROR_FORMAT,"Component LocateGameData has another client record");
        *result=0;
        return q3component_current(c,e)&&application_q3_component_records_refresh(c->records,e);
    }
    bool ok=c->lower.syscall(c->lower.context,call,trap,result,e);
    if(ok) ok=q3component_current(c,e)&&application_q3_component_records_refresh(c->records,e);
    return ok;
}
bool q3component_host_checkpoint(void *context,qa_buffer *out,qa_error *e)
{ application_q3_component *c=context; return c->lower.checkpoint(c->lower.context,out,e); }
bool q3component_host_restore(void *context,qa_bytes bytes,qa_error *e)
{ application_q3_component *c=context; return c->lower.restore(c->lower.context,bytes,e); }
