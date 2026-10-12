/* Compiled Unified CLIENT cg_consolecmds, id Software 1999-2005, GPL-2.0-or-later. */
#include "unified_q3_commands.h"
#include "internal.h"
#include "qa/binary.h"
#include "qa/console_cvars_prepare.h"
#include "qa/text.h"
#include "../../presentation/q3_native/cgame_commands.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_unified_q3_commands {
    frontend_unified_q3_commands_options options;
    qa_console *console;
    uint64_t receiver;
    qa_command_context constructor;
    qa_q3_product product;
    size_t installed;
    bool registered, closed, busy, retiring, resetting;
};
static bool fail(qa_error *e,qa_status status,const char *text)
{ return frontend_fail(e,status,text); }
static unsigned char lower(unsigned char c)
{ return c>='A' && c<='Z'?(unsigned char)(c+32):c; }

static const char *arg(const qa_command_invocation *call,size_t index,char out[1024])
{
    const char *text=index<call->argc?call->argv[index]:"";
    size_t n=strlen(text); if(n>1023)n=1023; memcpy(out,text,n); out[n]=0; return out;
}
static bool current(const frontend_unified_q3_commands *o,bool checkpoint,q3n_compiled_source_view *out,qa_error *e)
{
    q3n_compiled_source_view actual;
    if(!o || (!checkpoint && o->retiring) || !o->options.current(o->options.context,&o->options,checkpoint))
        return fail(e,QA_ERROR_ARGUMENT,"Unified console lost its actual factory parents");
    q3n_compiled_source *source=frontend_unified_q3_client_source(o->options.client);
    if(!(checkpoint?q3n_compiled_source_checkpoint_read(source,&actual,e):q3n_compiled_source_read(source,&actual,e)))return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->options.replica);
    const qa_command_context *origin=checkpoint?frontend_unified_q3_client_checkpoint_stage_context(o->options.client,
        frontend_unified_q3_runtime_rebind_frame(o->options.runtime)):
        frontend_unified_q3_client_context(o->options.client);
    const qa_command_context *domain_command=domain?&domain->command_context:NULL;
    if(!domain || domain->application!=o->options.frontend->application || domain->console!=o->console ||
        actual.basis.application!=domain->application || actual.basis.registry!=frontend_remote_unified_registry(o->options.replica) ||
        actual.basis.receiver!=o->receiver || actual.basis.product!=o->product ||
        actual.basis.seat!=domain->seat.index || actual.basis.physical_seat!=domain->physical_seat || !origin ||
        origin->cvar_view!=o->constructor.cvar_view || origin->cvar_view!=qa_cvars_view_identity(domain->cvars) ||
        !qa_command_context_equal(origin,domain_command,0))
        return fail(e,QA_ERROR_ARGUMENT,"Unified console changed its actual Source receiver namespace");
    if(out)*out=actual;
    return true;
}
static bool invocation_current(frontend_unified_q3_commands *o,const qa_command_invocation *call,qa_error *e)
{
    if(!current(o,false,NULL,e))return false;
    const qa_command_context *b=frontend_unified_q3_client_context(o->options.client),*a=&call->context;
    bool source=a->owner==b->owner && a->cvar_view==b->cvar_view && a->dialect==b->dialect && a->origin==b->origin &&
        a->client==b->client && a->seat==b->seat && a->registry==b->registry && a->generation==b->generation &&
        qa_actor_id_equal(a->actor,b->actor);
    bool engine=!a->owner && a->cvar_view==qa_cvars_view_identity(qa_application_cvars(o->options.frontend->application)) &&
        qa_console_invocation_delivered_view(call,o->constructor.cvar_view,o->receiver,o->receiver);
    return call->console==o->console && qa_console_invocation_current(o->console,call) &&
        a->session==b->session && (source||engine) &&
        qa_application_command_context_active(o->options.frontend->application,a) ? true :
        fail(e,QA_ERROR_ARGUMENT,"Unified command differs from its actual entered CLIENT invocation");
}
static bool send(void *context,const qa_command_invocation *call,const char *text,qa_error *e)
{ frontend_unified_q3_commands *o=context;
    if(!invocation_current(o,call,e))return false;
    qa_command_context origin=*frontend_unified_q3_client_context(o->options.client);
    return o->options.send_client(o->options.context,o->options.client,&origin,text,e) && invocation_current(o,call,e);
}
static bool append(void *context,const qa_command_invocation *call,const char *text,qa_error *e)
{ frontend_unified_q3_commands *o=context;
    if(!invocation_current(o,call,e))return false;
    qa_command_context origin=*frontend_unified_q3_client_context(o->options.client);
    origin.script="q3-cgame"; origin.direct=false; origin.console_text=false;
    return qa_console_append(call->console,&origin,text,e) && invocation_current(o,call,e);
}
static bool set(void *context,const qa_command_invocation *call,const char *name,const char *value,qa_error *e)
{ frontend_unified_q3_commands *o=context;
    qa_cvars_edit_command command={.kind=QA_CVARS_EDIT_SET,.name=name,.value=value,.force=true};
    if(!invocation_current(o,call,e))return false;
    qa_command_context origin=*frontend_unified_q3_client_context(o->options.client);
    return qa_console_cvar_apply(call->console,&origin,&command,e) && invocation_current(o,call,e);
}
static int32_t crosshair(const q3n_frame *f,const q3n_hud_state *hud)
{ return f->time>qa_i32_add(hud->crosshair_client_time,1000)?-1:hud->crosshair_client; }
static bool scores(frontend_unified_q3_commands *o,const qa_command_invocation *call,const q3n_frame *f,
    const frontend_unified_q3_runtime_owners *owners,qa_error *e)
{
    if(q3n_frame_product(f)==QA_Q3_TEAM_ARENA && !q3n_server_commands_spectators_build(owners->commands,f,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners->hud); bool due;
    if(!hud || !q3n_hud_scores_request(owners->hud,f,&due,e))return false;
    if(due && (!send(o,call,"score",e) ||
        (!hud->show_scores && !q3n_server_commands_scores_clear(owners->commands,f,e))))return false;
    q3n_hud_scores(owners->hud,true,f->time); return true;
}
static bool dispatch(frontend_unified_q3_commands *o,const qa_command_invocation *call,const q3n_frame *f,
    const frontend_unified_q3_runtime_owners *children,qa_error *e)
{
    const char *name=call->argv[0]; char operand[1024],text[1024]; const char *value=arg(call,1,operand);
    frontend_unified_q3_runtime_owners owners=*children;
    if(!current(o,false,NULL,e))return false;
    qa_cvars *cvars=frontend_unified_q3_client_cvars(o->options.client);
    const q3n_hud_state *hud=q3n_hud_read(owners.hud);
    const q3n_command_state *state=q3n_server_commands_state(owners.commands);
    if(!hud || !state)return fail(e,QA_ERROR_ARGUMENT,"Unified console children have an active operation");
    if(q3n_cgame_command_equal(name,"testmodel") || q3n_cgame_command_equal(name,"testgun")) {
        float back=0; const float *optional=NULL;
        if(call->argc==3) { char arg2[1024]; back=(float)qa_parse_quake_number(arg(call,2,arg2),QA_QUAKE_NUMBER_Q3_VM); optional=&back; }
        return q3n_view_test_model(owners.view,f,call->argc<2?NULL:value,optional,q3n_cgame_command_equal(name,"testgun"),e);
    }
    if(q3n_cgame_command_equal(name,"nextframe") || q3n_cgame_command_equal(name,"prevframe") || q3n_cgame_command_equal(name,"nextskin") || q3n_cgame_command_equal(name,"prevskin")) {
        q3n_view_test_step(owners.view,q3n_cgame_command_equal(name,"nextskin") || q3n_cgame_command_equal(name,"prevskin"),lower((unsigned char)*name)=='n'?1:-1); return true;
    }
    if(q3n_cgame_command_equal(name,"+zoom") || q3n_cgame_command_equal(name,"-zoom")) { q3n_view_zoom(owners.view,*name=='+',f->time); return true; }
    if(q3n_cgame_command_equal(name,"weapnext") || q3n_cgame_command_equal(name,"weapprev")) { q3n_weapons_cycle(f,q3n_cgame_command_equal(name,"weapnext")?1:-1); return true; }
    if(q3n_cgame_command_equal(name,"weapon")) { q3n_weapons_select(f,qa_parse_decimal_i32(value,QA_DECIMAL_SIGNED_WRAP)); return true; }
    if(q3n_cgame_command_equal(name,"viewpos")) {
        snprintf(text,sizeof(text),"(%i %i %i) : %i\n",qa_source_to_i32(f->refdef.origin.x),qa_source_to_i32(f->refdef.origin.y),
            qa_source_to_i32(f->refdef.origin.z),qa_source_to_i32(f->view_angles.y)); qa_console_emit(call->console,&call->context,text); return true;
    }
    if(q3n_cgame_command_equal(name,"sizeup") || q3n_cgame_command_equal(name,"sizedown")) {
        qa_native_q3_client_cvar cached;
        if(!frontend_unified_q3_client_cvar_read(o->options.client,QA_NATIVE_Q3_CVAR_cg_viewsize,&cached,e))return false;
        snprintf(text,sizeof(text),"%i",qa_i32_add(cached.integer,q3n_cgame_command_equal(name,"sizeup")?10:-10)); return set(o,call,"cg_viewsize",text,e);
    }
    if(q3n_cgame_command_equal(name,"+scores"))return scores(o,call,f,&owners,e);
    if(q3n_cgame_command_equal(name,"-scores")) { if(hud->show_scores)q3n_hud_scores(owners.hud,false,f->time); return true; }
    if(q3n_cgame_command_equal(name,"tcmd")) {
        int32_t target=crosshair(f,hud); if(target==0)return true;
        char narrow[4]={0}; size_t n=strlen(value); if(n>3)n=3; memcpy(narrow,value,n);
        snprintf(text,sizeof(text),"gc %i %i",target,qa_parse_decimal_i32(narrow,QA_DECIMAL_SIGNED_WRAP)); return append(o,call,text,e);
    }
    if(q3n_cgame_command_equal(name,"tell_target") || q3n_cgame_command_equal(name,"tell_attacker") || q3n_cgame_command_equal(name,"vtell_target") || q3n_cgame_command_equal(name,"vtell_attacker")) {
        bool target=q3n_cgame_command_equal(name,"tell_target") || q3n_cgame_command_equal(name,"vtell_target"); int32_t client;
        if(target)client=crosshair(f,hud);
        else {
            const q3n_player_feedback *feedback=q3n_player_state_feedback(owners.player_state);
            if(!feedback)return fail(e,QA_ERROR_ARGUMENT,"Unified attacker feedback is active");
            if(!feedback->attacker_time)client=-1;
            else {
                const qa_q3_player *player=q3n_frame_snapshot_player(f);
                if(!player)return fail(e,QA_ERROR_ARGUMENT,"CG_LastAttacker requires the real received snapshot");
                client=player->persistant[6];
            }
        }
        if(client==-1)return true;
        char args[1024]={0}; size_t used=0;
        for(size_t i=1;i<call->argc;++i) {
            size_t n=strlen(call->argv[i]),space=i>1?1:0;
            if(n+space>=sizeof(args)-used)return fail(e,QA_ERROR_FORMAT,"Cmd_Args exceeds MAX_STRING_CHARS");
            if(space)args[used++]=' ';
            memcpy(args+used,call->argv[i],n); used+=n;
        }
        args[used]=0; if(used>127)args[127]=0;
        int length=snprintf(text,128,"%s %i ",lower((unsigned char)*name)=='v'?"vtell":"tell",client);
        if(length<0 || length>=128)return fail(e,QA_ERROR_FORMAT,"Formatting source tell command");
        size_t prefix=(size_t)length,n=strlen(args);
        if(n>127-prefix)n=127-prefix;
        memcpy(text+prefix,args,n); text[prefix+n]=0;
        return send(o,call,text,e);
    }
    if(q3n_cgame_command_equal(name,"loaddeferred"))return frontend_unified_q3_runtime_load_deferred(o->options.runtime,f,e);
    if(q3n_cgame_command_equal(name,"startOrbit")) {
        const qa_cvar_view *developer=qa_cvars_find(cvars,"developer");
        char immediate[1024]; snprintf(immediate,sizeof(immediate),"%s",developer?developer->value:"");
        if(!qa_parse_decimal_i32(immediate,QA_DECIMAL_SIGNED_WRAP))return true;
        qa_native_q3_client_cvar cached;
        if(!frontend_unified_q3_client_cvar_read(o->options.client,QA_NATIVE_Q3_CVAR_cg_cameraOrbit,&cached,e))return false;
        if(cached.number!=0)return set(o,call,"cg_cameraOrbit","0",e) && set(o,call,"cg_thirdPerson","0",e);
        return set(o,call,"cg_cameraOrbit","5",e) && set(o,call,"cg_thirdPerson","1",e) &&
            set(o,call,"cg_thirdPersonAngle","0",e) && set(o,call,"cg_thirdPersonRange","100",e);
    }
    q3n_cgame_command_services shared={.context=o,.cvars=cvars,.mission=owners.mission,
        .hud=owners.hud,.messages=o->options.frontend->seats[f->physical_presentation_seat].hud,
        .commands=owners.commands,.media=owners.media,.events=owners.events,.set=set,.send=send,.append=append};
    return q3n_cgame_mission_command(&shared,call,f,e);
}

static bool dispose(frontend_unified_q3_commands *o,qa_error *e)
{
    o->closed=true;
    frontend_unified_q3_runtime_owners owners; q3n_compiled_source_view source;
    if(!frontend_unified_q3_runtime_owners_read(o->options.runtime,&owners,e) || !current(o,false,&source,e))return false;
    q3n_view_test_clear(owners.view);
    return q3n_clients_compiled_reset(owners.clients,&source,e);
}
typedef struct command_entry {
    frontend_unified_q3_commands *owner;
    const qa_command_invocation *call;
} command_entry;
static bool projected(void *context,const q3n_frame *f,const frontend_unified_q3_runtime_owners *children,qa_error *e)
{
    command_entry *entry=context;
    return invocation_current(entry->owner,entry->call,e) && dispatch(entry->owner,entry->call,f,children,e) &&
        (q3n_frame_current(f)?invocation_current(entry->owner,entry->call,e):
            fail(e,QA_ERROR_ARGUMENT,"Unified command lost its entered compiled CLIENT frame"));
}
static bool entered(void *context,const q3n_compiled_frame *f,qa_error *e)
{
    command_entry *entry=context;
    return invocation_current(entry->owner,entry->call,e) &&
        frontend_unified_q3_runtime_command_call(entry->owner->options.runtime,f,entry,projected,e) &&
        invocation_current(entry->owner,entry->call,e);
}
static bool invocation_valid(frontend_unified_q3_commands *o,const qa_command_invocation *call,qa_error *e)
{
    if(!call || !call->argc || call->argc>1024 || !call->argv || !call->raw || call->console!=o->console)
        return fail(e,QA_ERROR_ARGUMENT,"Unified console lost its real tokenized invocation");
    size_t bytes=0;
    for(size_t i=0;i<call->argc;++i) {
        if(!call->argv[i])return fail(e,QA_ERROR_ARGUMENT,"Unified console has an absent token");
        size_t n=strlen(call->argv[i]);
        if(n+1>9216-bytes)return fail(e,QA_ERROR_FORMAT,"Unified command exceeds source token storage");
        bytes+=n+1;
    }
    return true;
}
static bool handle(void *context,const qa_command_invocation *call,qa_error *e)
{
    frontend_unified_q3_commands *o=context;
    if(!invocation_valid(o,call,e) || !invocation_current(o,call,e))return false;
    if(!o->registered || o->busy || o->closed || o->resetting || !frontend_unified_q3_runtime_idle(o->options.runtime))
        return fail(e,QA_ERROR_ARGUMENT,"Unified console has no returned open CG recipient");
    frontend_unified_q3_runtime_owners owners;
    if(!frontend_unified_q3_runtime_owners_read(o->options.runtime,&owners,e))return false;
    o->busy=true; command_entry entry={o,call};
    bool okay=frontend_unified_q3_snapshots_console(owners.snapshots,entered,&entry,e);
    if(!okay) {
        qa_error cleanup={0};
        if(!dispose(o,&cleanup) && e && e->code==QA_OK)*e=cleanup;
    }
    o->busy=false; return okay;
}
qa_command_result frontend_unified_q3_commands_execute(frontend_unified_q3_commands *o,
    const qa_command_invocation *call,qa_error *e)
{
    if(!o) { fail(e,QA_ERROR_ARGUMENT,"Unified source console lost its command continuation"); return QA_COMMAND_FAILED; }
    if(!invocation_valid(o,call,e) || !invocation_current(o,call,e))return QA_COMMAND_FAILED;
    if(!o->registered || o->busy || o->closed || o->resetting) {
        fail(e,QA_ERROR_ARGUMENT,"Unified source console has no open command continuation"); return QA_COMMAND_FAILED;
    }
    for(size_t i=0;i<q3n_cgame_local_command_count(o->product);++i)if(q3n_cgame_command_equal(call->argv[0],q3n_cgame_command_name(o->product,i)))
        return handle(o,call,e)?QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
    return QA_COMMAND_UNHANDLED;
}
bool frontend_unified_q3_commands_create(const frontend_unified_q3_commands_options *options,
    frontend_unified_q3_commands **out,qa_error *e)
{
    if(!options || !out || *out || !options->frontend || !options->replica || !options->client ||
        !options->runtime || !options->current || !options->send_client || options->frontend->capture ||
        options->frontend->source_restoring || !options->current(options->context,options,false))
        return fail(e,QA_ERROR_ARGUMENT,"Unified console requires its actual factory-owned CLIENT and CG");
    q3n_compiled_source_view source;
    q3n_compiled_source *owner=frontend_unified_q3_client_source(options->client);
    if(!q3n_compiled_source_read(owner,&source,e))return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(options->replica);
    if(!domain || !domain->console || source.basis.initialized)
        return fail(e,QA_ERROR_ARGUMENT,"Unified command construction precedes genuine CG initialization");
    frontend_unified_q3_commands *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining compiled Unified console continuation");
    o->options=*options; o->console=domain->console; o->receiver=source.basis.receiver; o->product=source.basis.product;
    const qa_command_context *constructor=frontend_unified_q3_client_context(options->client);
    if(!constructor) { free(o); return fail(e,QA_ERROR_ARGUMENT,"Unified command constructor lost its actual CLIENT receipt"); }
    o->constructor=*constructor;
    if(!current(o,false,NULL,e)) { free(o); return false; }
    *out=o; return true;
}
static bool bind(frontend_unified_q3_commands *o,size_t count,qa_error *e)
{
    while(o->installed<count) {
        size_t i=o->installed;
        qa_command_handler handler=i<q3n_cgame_local_command_count(o->product)?handle:NULL;
        void *user=handler?o:NULL;
        uint64_t owner; qa_command_handler actual_handler; void *actual_user;
        if(qa_console_registration_read_context(o->console,&o->constructor,q3n_cgame_command_name(o->product,i),o->receiver,
            &owner,&actual_handler,&actual_user)) {
            if(owner!=o->receiver || actual_handler!=handler || actual_user!=user)
                return fail(e,QA_ERROR_FORMAT,"Unified command prefix has a different actual callback owner");
            ++o->installed; continue;
        }
        /* Q3 AddCommand names without CG handlers remain engine fallbacks. */
        if(!qa_console_register_context(o->console,&o->constructor,q3n_cgame_command_name(o->product,i),NULL,o->receiver,o->receiver,
            false,handler,user,e))return false;
        ++o->installed;
    }
    return true;
}
static bool bindings_current(const frontend_unified_q3_commands *o)
{
    for(size_t i=0;i<q3n_cgame_command_count(o->product);++i) {
        uint64_t owner; qa_command_handler handler; void *user;
        bool present=qa_console_registration_read_context(o->console,&o->constructor,q3n_cgame_command_name(o->product,i),o->receiver,&owner,&handler,&user);
        if(i>=o->installed) { if(present)return false; continue; }
        qa_command_handler expected=i<q3n_cgame_local_command_count(o->product)?handle:NULL;
        if(!present || owner!=o->receiver || handler!=expected || user!=(expected?(void *)o:NULL))return false;
    }
    return true;
}
bool frontend_unified_q3_commands_registration_ready(frontend_unified_q3_commands *o,const q3n_frame *f,qa_error *e)
{
    return o && !o->busy && !o->resetting && o->registered && !o->closed && f && f->compiled &&
        f->compiled->stage==Q3N_COMPILED_INITIALIZATION &&
        f->compiled->source.owner==frontend_unified_q3_client_source(o->options.client) &&
        q3n_frame_current(f) && current(o,false,NULL,e) && bindings_current(o) ? true :
        fail(e,QA_ERROR_ARGUMENT,"Unified CG Init lost its actual installed console command rows");
}
bool frontend_unified_q3_commands_register(void *context,const q3n_frame *f,qa_error *e)
{
    frontend_unified_q3_commands *o=context;
    if(!o || o->busy || o->resetting || o->registered || o->closed || !f || !f->compiled ||
        f->compiled->stage!=Q3N_COMPILED_INITIALIZATION ||
        f->compiled->source.owner!=frontend_unified_q3_client_source(o->options.client) ||
        !q3n_frame_current(f) || !current(o,false,NULL,e))
        return fail(e,QA_ERROR_ARGUMENT,"Unified command registration requires its genuine CG Init scope");
    o->busy=true; bool okay=bind(o,q3n_cgame_command_count(o->product),e);
    if(okay) { o->registered=true; okay=q3n_frame_current(f) && current(o,false,NULL,e); }
    o->busy=false; return okay && frontend_unified_q3_commands_registration_ready(o,f,e);
}
bool frontend_unified_q3_commands_client_command(void *context,const q3n_frame *f,const char *text,qa_error *e)
{
    frontend_unified_q3_commands *o=context;
    if(!o || !text || !f || !f->compiled ||
        f->compiled->source.owner!=frontend_unified_q3_client_source(o->options.client) ||
        !q3n_frame_current(f) || !current(o,false,NULL,e))
        return fail(e,QA_ERROR_ARGUMENT,"Unified reliable command requires its actual entered Source frame");
    const qa_command_context *origin=frontend_unified_q3_client_context(o->options.client);
    return o->options.send_client(o->options.context,o->options.client,origin,text,e) &&
        q3n_frame_current(f) && current(o,false,NULL,e);
}
bool frontend_unified_q3_commands_idle(const frontend_unified_q3_commands *o)
{ return !o || !o->busy; }
bool frontend_unified_q3_commands_current(const frontend_unified_q3_commands *o)
{ return current(o,false,NULL,NULL); }
bool frontend_unified_q3_commands_video_reset(frontend_unified_q3_commands *o,qa_error *e)
{
    if(!o || o->busy || o->retiring || !qa_console_idle(o->console) ||
        !frontend_unified_q3_runtime_idle(o->options.runtime) || !current(o,false,NULL,e))
        return fail(e,QA_ERROR_ARGUMENT,"Unified CG video reset retains a command invocation or stale Source");
    o->resetting=true;
    while(o->installed) {
        if(!qa_console_unregister_context(o->console,&o->constructor,q3n_cgame_command_name(o->product,o->installed-1),o->receiver))
            return fail(e,QA_ERROR_ARGUMENT,"Unified CG video command removal was refused");
        --o->installed;
    }
    o->registered=false; o->closed=false; o->resetting=false;
    return current(o,false,NULL,e);
}
bool frontend_unified_q3_commands_destroy(frontend_unified_q3_commands **owned,qa_error *e)
{
    if(!owned || !*owned)return true;
    frontend_unified_q3_commands *o=*owned;
    if(o->busy || !qa_console_idle(o->console) || !frontend_unified_q3_runtime_idle(o->options.runtime))
        return fail(e,QA_ERROR_ARGUMENT,"Unified console retirement retains a CG or console invocation");
    o->retiring=true;
    while(o->installed) {
        if(!qa_console_unregister_context(o->console,&o->constructor,q3n_cgame_command_name(o->product,o->installed-1),o->receiver))
            return fail(e,QA_ERROR_ARGUMENT,"Unified console callback retirement was refused");
        --o->installed;
    }
    free(o); *owned=NULL; return true;
}
