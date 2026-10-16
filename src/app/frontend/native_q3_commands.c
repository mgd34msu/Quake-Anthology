#include "native_q3_client_internal.h"
#include "qa/binary.h"
#include "qa/text.h"
#include "../../presentation/q3_native/cgame_commands.h"
#include <math.h>
#include <stdio.h>

typedef struct native_dispatch {
    qa_frontend *frontend;
    qa_console *console;
    qa_actor_owner source;
    qa_q3_product product;
    size_t users;
} native_dispatch;
struct frontend_native_q3_commands {
    frontend_native_q3 *row;
    native_dispatch *dispatch;
    qa_command_context constructor;
    size_t installed, contributed;
    bool registered, closed, busy;
};
static unsigned char lower(unsigned char c) { return c>='A' && c<='Z'?(unsigned char)(c+32):c; }

static const char *arg(const qa_command_invocation *command,size_t index,char out[1024])
{
    const char *text=index<command->argc?command->argv[index]:"";
    size_t n=strlen(text); if(n>1023)n=1023; memcpy(out,text,n); out[n]=0; return out;
}
static bool set(void *context,const qa_command_invocation *command,const char *name,const char *value,qa_error *e)
{ (void)command; frontend_native_q3 *row=context; return qa_cvars_set(row->view.cvars,name,value,true,e); }
static bool send(void *context,const qa_command_invocation *command,const char *text,qa_error *e)
{ (void)command; frontend_native_q3 *row=context; return qa_native_q3_client_reliable(row->view.client,text,e); }
static bool append(void *context,const qa_command_invocation *command,const char *text,qa_error *e)
{ (void)command; frontend_native_q3 *row=context; return qa_native_q3_client_console(row->view.client,text,e); }
static int32_t crosshair(const q3n_frame *f,const q3n_hud_state *hud)
{ return f->time>qa_i32_add(hud->crosshair_client_time,1000)?-1:hud->crosshair_client; }
static bool scores(frontend_native_q3_commands *o,const qa_command_invocation *command,const q3n_frame *f,qa_error *e)
{
    q3n_native_owners owners; frontend_native_q3 *row=o->row;
    if(!q3n_native_owners_read(row->view.core,&owners,e))return false;
    if(f->source.product==QA_Q3_TEAM_ARENA && !q3n_server_commands_spectators_build(owners.commands,f,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners.hud); bool due;
    if(!hud || !q3n_hud_scores_request(owners.hud,f,&due,e))return false;
    if(due) {
        if(!send(row,command,"score",e))return false;
        if(!hud->show_scores && !q3n_server_commands_scores_clear(owners.commands,f,e))return false;
    }
    q3n_hud_scores(owners.hud,true,f->time); return true;
}
static bool dispatch_command(frontend_native_q3_commands *o,const qa_command_invocation *command,const q3n_frame *f,qa_error *e)
{
    frontend_native_q3 *row=o->row; const char *name=command->argv[0]; char operand[1024],text[1024];
    const char *value=arg(command,1,operand); q3n_native_owners owners;
    if(!q3n_native_owners_read(row->view.core,&owners,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners.hud);
    const q3n_command_state *state=q3n_server_commands_state(owners.commands);
    if(!hud || !state)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console children are active");
    if(q3n_cgame_command_equal(name,"testmodel") || q3n_cgame_command_equal(name,"testgun")) {
        float back=0; const float *optional=NULL;
        if(command->argc==3) { char arg2[1024]; back=(float)qa_parse_quake_number(arg(command,2,arg2),QA_QUAKE_NUMBER_Q3_VM); optional=&back; }
        return q3n_view_test_model(owners.view,f,command->argc<2?NULL:value,optional,q3n_cgame_command_equal(name,"testgun"),e);
    }
    if(q3n_cgame_command_equal(name,"nextframe") || q3n_cgame_command_equal(name,"prevframe") || q3n_cgame_command_equal(name,"nextskin") || q3n_cgame_command_equal(name,"prevskin")) {
        q3n_view_test_step(owners.view,q3n_cgame_command_equal(name,"nextskin") || q3n_cgame_command_equal(name,"prevskin"),lower((unsigned char)*name)=='n'?1:-1); return true;
    }
    if(q3n_cgame_command_equal(name,"+zoom") || q3n_cgame_command_equal(name,"-zoom")) { q3n_view_zoom(owners.view,*name=='+',f->time); return true; }
    if(q3n_cgame_command_equal(name,"weapnext") || q3n_cgame_command_equal(name,"weapprev")) { q3n_weapons_cycle(f,q3n_cgame_command_equal(name,"weapnext")?1:-1); return true; }
    if(q3n_cgame_command_equal(name,"weapon"))
        return q3n_cgame_weapon_command(f,command,command->context.actor,e);
    if(q3n_cgame_command_equal(name,"viewpos")) {
        snprintf(text,sizeof(text),"(%i %i %i) : %i\n",qa_source_to_i32(f->refdef.origin.x),qa_source_to_i32(f->refdef.origin.y),
            qa_source_to_i32(f->refdef.origin.z),qa_source_to_i32(f->view_angles.y)); qa_console_emit(row->console,&command->context,text); return true;
    }
    if(q3n_cgame_command_equal(name,"sizeup") || q3n_cgame_command_equal(name,"sizedown")) {
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_client_cvar_read(row->view.client,QA_NATIVE_Q3_CVAR_cg_viewsize,&cached,e))return false;
        snprintf(text,sizeof(text),"%i",qa_i32_add(cached.integer,q3n_cgame_command_equal(name,"sizeup")?10:-10)); return set(row,command,"cg_viewsize",text,e);
    }
    if(q3n_cgame_command_equal(name,"+scores"))return scores(o,command,f,e);
    if(q3n_cgame_command_equal(name,"-scores")) { if(hud->show_scores)q3n_hud_scores(owners.hud,false,f->time); return true; }
    if(q3n_cgame_command_equal(name,"tcmd")) {
        int32_t target=crosshair(f,hud); if(target==0)return true;
        char narrow[4]={0}; size_t n=strlen(value); if(n>3)n=3; memcpy(narrow,value,n);
        snprintf(text,sizeof(text),"gc %i %i",target,qa_parse_decimal_i32(narrow,QA_DECIMAL_SIGNED_WRAP)); return append(row,command,text,e);
    }
    if(q3n_cgame_command_equal(name,"tell_target") || q3n_cgame_command_equal(name,"tell_attacker") || q3n_cgame_command_equal(name,"vtell_target") || q3n_cgame_command_equal(name,"vtell_attacker")) {
        const q3n_player_feedback *feedback=q3n_player_state_feedback(owners.player_state);
        bool target=q3n_cgame_command_equal(name,"tell_target") || q3n_cgame_command_equal(name,"vtell_target");
        int32_t client=target?crosshair(f,hud):feedback && feedback->attacker_time?f->local_player.persistant[6]:-1;
        if(client==-1)return true;
        char args[1024]={0}; size_t used=0;
        for(size_t i=1;i<command->argc;++i) {
            size_t n=strlen(command->argv[i]); size_t space=i>1?1:0;
            if(n+space>=sizeof(args)-used)return frontend_fail(e,QA_ERROR_FORMAT,"Cmd_Args exceeds MAX_STRING_CHARS");
            if(space)args[used++]=' ';
            memcpy(args+used,command->argv[i],n); used+=n;
        }
        args[used]=0; if(used>127)args[127]=0;
        size_t prefix=(size_t)snprintf(text,128,"%s %i ",lower((unsigned char)*name)=='v'?"vtell":"tell",client);
        size_t message=strlen(args); if(message>127-prefix)message=127-prefix;
        memcpy(text+prefix,args,message); text[prefix+message]=0;
        return send(row,command,text,e);
    }
    if(q3n_cgame_command_equal(name,"loaddeferred")) {
        q3n_native_frame_options projected;
        if(!frontend_native_q3_project_settings(row,&projected,e))return false;
        return q3n_clients_load_deferred(owners.clients,f->application,&f->source,&projected.clients,e);
    }
    if(q3n_cgame_command_equal(name,"startOrbit")) {
        const qa_cvar_view *developer=qa_cvars_find(row->view.cvars,"developer");
        if(!developer || !qa_parse_decimal_i32(developer->value,QA_DECIMAL_SIGNED_WRAP))return true;
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_client_cvar_read(row->view.client,QA_NATIVE_Q3_CVAR_cg_cameraOrbit,&cached,e))return false;
        if(cached.number!=0)return set(row,command,"cg_cameraOrbit","0",e) && set(row,command,"cg_thirdPerson","0",e);
        return set(row,command,"cg_cameraOrbit","5",e) && set(row,command,"cg_thirdPerson","1",e) &&
            set(row,command,"cg_thirdPersonAngle","0",e) && set(row,command,"cg_thirdPersonRange","100",e);
    }
    q3n_cgame_command_services shared={.context=row,.cvars=row->view.cvars,.mission=row->view.mission,
        .hud=owners.hud,.messages=row->frontend->seats[row->view.seat].hud,
        .commands=owners.commands,.media=owners.media,.events=owners.events,.set=set,.send=send,.append=append};
    return q3n_cgame_mission_command(&shared,command,f,e);
}

static void dispose(frontend_native_q3_commands *o,const q3n_frame *frame)
{
    q3n_native_owners owners;
    o->closed=true;
    if(!q3n_native_owners_read(o->row->view.core,&owners,NULL))return;
    q3n_view_test_clear(owners.view);
    if(frame) {
        qa_error cleanup={0};
        (void)q3n_clients_reset(owners.clients,frame->application,&frame->source,&cleanup);
    }
}
static bool handle(void *context,const qa_command_invocation *command,qa_error *e)
{
    native_dispatch *d=context; frontend_native_q3_commands *o=NULL;
    if(!command || !command->argc || command->argc>1024 || command->console!=d->console || command->receiver!=d->source)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console lost its real source invocation");
    size_t bytes=0;
    for(size_t i=0;i<command->argc;++i) { size_t n=strlen(command->argv[i]); if(n>=9216-bytes)return false; bytes+=n+1; }
    for(frontend_native_q3 *row=d->frontend->native_q3;row;row=row->next)
        if(row->constructed && row->commands && row->console==d->console && row->view.source_owner==d->source &&
            qa_console_invocation_delivered_view(command,row->commands->constructor.cvar_view,
                row->view.source_owner,row->view.service_owner) &&
            row->view.launch_seat==command->context.seat && qa_actor_id_equal(frontend_native_q3_actor(row),command->context.actor)) {
            if(o)return frontend_fail(e,QA_ERROR_FORMAT,"Native console has duplicate actual recipients");
            o=row->commands;
        }
    if(!o || !o->registered || o->busy)return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console has no idle admitted recipient");
    if(o->closed)return frontend_fail(e,QA_ERROR_ARGUMENT,"Cgame console runtime is closed");
    q3n_frame frame;
    if(!q3n_native_command_frame(o->row->view.core,&frame,e) || !frontend_native_q3_cut(o->row,&frame,e)) { dispose(o,NULL); return false; }
    o->busy=true; ++o->row->callbacks;
    bool ok=dispatch_command(o,command,&frame,e) && frontend_native_q3_cut(o->row,&frame,e);
    if(!ok)dispose(o,&frame);
    --o->row->callbacks; o->busy=false; return ok;
}
bool frontend_native_q3_commands_create(frontend_native_q3 *row,frontend_native_q3_commands **out,qa_error *e)
{
    if(!row || !out || *out || !row->view.client ||
        (row->view.product!=QA_Q3_ARENA && row->view.product!=QA_Q3_TEAM_ARENA))return false;
    frontend_native_q3_commands *o=calloc(1,sizeof(*o)); if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Allocating native console state");
    o->row=row; o->constructor=row->command;
    for(frontend_native_q3 *p=row->frontend->native_q3;p;p=p->next)if(p!=row && p->commands &&
        p->console==row->console && p->view.source_owner==row->view.source_owner)o->dispatch=p->commands->dispatch;
    if(!o->dispatch) {
        o->dispatch=calloc(1,sizeof(*o->dispatch));
        if(!o->dispatch) { free(o); return frontend_fail(e,QA_ERROR_MEMORY,"Allocating actual source console dispatch"); }
        *o->dispatch=(native_dispatch){.frontend=row->frontend,.console=row->console,.source=row->view.source_owner,.product=row->view.product};
    }
    ++o->dispatch->users; *out=o; return true;
}
static bool bind_commands(frontend_native_q3_commands *o,size_t count,qa_error *e)
{
    native_dispatch *d=o->dispatch;
    qa_command_context constructor=o->constructor;
    for(size_t i=0;i<count;++i) {
        const char *name=q3n_cgame_command_name(d->product,i);
        if(i<q3n_cgame_local_command_count(d->product) && i>=o->installed) {
            if(!qa_console_register_context(d->console,&constructor,name,NULL,d->source,
                o->row->view.service_owner,false,handle,d,e))return false;
            o->installed=i+1;
        }
        if(i>=o->contributed) {
            qa_console_contribution owner={.receiver=d->source,.lifetime_owner=o->row->view.service_owner,
                .seat=o->row->view.launch_seat,.cvar_view=constructor.cvar_view};
            if(!qa_console_contribute(d->console,name,&owner,e))return false;
            o->contributed=i+1;
        }
    }
    return true;
}
bool frontend_native_q3_commands_register(frontend_native_q3_commands *o,const q3n_frame *f,qa_error *e)
{
    if(!o || o->busy || o->registered || !frontend_native_q3_cut(o->row,f,e))return false;
    o->busy=true; bool ok=bind_commands(o,q3n_cgame_command_count(o->dispatch->product),e);
    if(ok)o->registered=true;
    o->busy=false; return ok;
}
bool frontend_native_q3_commands_idle(const frontend_native_q3_commands *o) { return !o || !o->busy; }
bool frontend_native_q3_commands_destroy(frontend_native_q3_commands *o,qa_error *e)
{
    if(!o)return true;
    if(o->busy || !qa_console_idle(o->dispatch->console))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console teardown requires inactive callbacks");
    native_dispatch *d=o->dispatch;
    qa_command_context constructor=o->constructor;
    for(size_t i=0;i<o->installed;++i)qa_console_unregister_context(d->console,&constructor,q3n_cgame_command_name(d->product,i),d->source);
    for(size_t i=0;i<o->contributed;++i)qa_console_uncontribute(d->console,q3n_cgame_command_name(d->product,i),d->source,o->row->view.service_owner);
    if(!--d->users)free(d);
    free(o); return true;
}
void frontend_native_q3_commands_rebind(frontend_native_q3_commands *o,qa_frontend *f)
{ if(o)o->dispatch->frontend=f; }
