/* Received CLIENT cg_consolecmds, id Software 1999-2005, GPL-2.0-or-later. */
#include "remote_q3_commands.h"
#include "remote_q3_runtime.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct remote_dispatch remote_dispatch;
struct frontend_remote_q3_commands {
    frontend_remote_q3_commands *next;
    frontend_remote_q3_runtime *runtime;
    frontend_remote_q3 *row;
    remote_dispatch *dispatch;
    uint64_t identity;
    uint64_t service;
    uint32_t seat;
    size_t contributed;
    bool registered, closed, busy, retiring;
};
struct remote_dispatch {
    frontend_remote_q3_commands *owners;
    qa_console *console;
    qa_actor_owner receiver;
    qa_q3_product product;
    size_t installed;
};
static const char *const common[]={"testgun","testmodel","nextframe","prevframe","nextskin","prevskin","viewpos",
    "+scores","-scores","+zoom","-zoom","sizeup","sizedown","weapnext","weapprev","weapon",
    "tell_target","tell_attacker","vtell_target","vtell_attacker","tcmd"};
static const char *const mission[]={"loadhud","nextTeamMember","prevTeamMember","nextOrder","confirmOrder","denyOrder",
    "taskOffense","taskDefense","taskPatrol","taskCamp","taskFollow","taskRetrieve","taskEscort","taskSuicide","taskOwnFlag",
    "tauntKillInsult","tauntPraise","tauntTaunt","tauntDeathInsult","tauntGauntlet","spWin","spLose","scoresDown","scoresUp"};
static const char *const tail[]={"startOrbit","loaddeferred"};
static const char *const forwarded[]={"kill","say","say_team","tell","vsay","vsay_team","vtell","vtaunt","vosay","vosay_team","votell",
    "give","god","notarget","noclip","team","follow","levelshot","addbot","setviewpos","callvote","vote","callteamvote","teamvote",
    "stats","teamtask","loaddefered"};
#define COUNT(array) (sizeof(array)/sizeof((array)[0]))
static size_t local_count(qa_q3_product product)
{ return COUNT(common)+(product==QA_Q3_TEAM_ARENA?COUNT(mission):0)+COUNT(tail); }
static size_t command_count(qa_q3_product product)
{ return local_count(product)+COUNT(forwarded); }
static const char *command_name(qa_q3_product product,size_t index)
{
    if(index<COUNT(common))return common[index];
    index-=COUNT(common);
    if(product==QA_Q3_TEAM_ARENA) { if(index<COUNT(mission))return mission[index]; index-=COUNT(mission); }
    if(index<COUNT(tail))return tail[index];
    index-=COUNT(tail);
    return index<COUNT(forwarded)?forwarded[index]:NULL;
}
static bool fail(qa_error *e,qa_status status,const char *text)
{ return frontend_fail(e,status,text); }
static unsigned char lower(unsigned char c)
{ return c>='A' && c<='Z'?(unsigned char)(c+32):c; }
static bool equal(const char *a,const char *b)
{ while(*a && *b && lower((unsigned char)*a)==lower((unsigned char)*b)) { ++a; ++b; } return !*a && !*b; }
static int32_t signed_word(uint32_t word)
{ int32_t out; memcpy(&out,&word,sizeof(out)); return out; }
static int32_t sum(int32_t a,int32_t b)
{ return signed_word((uint32_t)a+(uint32_t)b); }
static int32_t integer(float value)
{ return value>=-2147483648.0f && value<2147483648.0f?(int32_t)value:INT32_MIN; }
static const char *number_start(const char *text,bool *negative)
{
    while(*text && (int8_t)(uint8_t)*text<=32)++text;
    *negative=*text=='-'; if(*text=='+' || *text=='-')++text; return text;
}
static int32_t game_atoi(const char *text)
{
    bool negative; text=number_start(text,&negative); uint32_t value=0;
    while(*text>='0' && *text<='9')value=value*10+(uint32_t)(*text++-'0');
    return signed_word(negative?0-value:value);
}
static float game_atof(const char *text)
{
    bool negative; text=number_start(text,&negative); float value=0;
    while(*text>='0' && *text<='9') { volatile float product=value*10; volatile float next=product+(float)(*text++-'0'); value=next; }
    if(*text=='.') {
        ++text; float fraction=.1f;
        while(*text>='0' && *text<='9') {
            volatile float product=(float)(*text++-'0')*fraction; volatile float next=value+product;
            volatile float scale=fraction*.1f; value=next; fraction=scale;
        }
    }
    return negative?-value:value;
}
static const char *arg(const qa_command_invocation *call,size_t index,char out[1024])
{
    const char *text=index<call->argc?call->argv[index]:"";
    size_t n=strlen(text); if(n>1023)n=1023; memcpy(out,text,n); out[n]=0; return out;
}
static bool current(const frontend_remote_q3_commands *o,frontend_remote_q3_services_view *out,qa_error *e)
{
    frontend_remote_q3_services_view actual;
    if(!o || o->retiring || frontend_remote_q3_runtime_parent(o->runtime)!=o->row ||
        frontend_remote_q3_runtime_read(o->row)!=o->runtime ||
        !frontend_remote_q3_services_read(o->row,&actual,e) ||
        actual.resources.identity!=o->identity || actual.resources.domain.source.receiver.console!=o->dispatch->console ||
        actual.resources.domain.source.receiver.receiver!=o->dispatch->receiver ||
        actual.resources.domain.source.receiver.service_owner!=o->service ||
        actual.resources.domain.source.receiver.seat!=o->seat)
        return fail(e,QA_ERROR_ARGUMENT,"Remote console lost its actual runtime and CLIENT parents");
    qa_native_q3_remote_client_basis basis;
    if(!qa_native_q3_remote_client_basis_read(actual.client,&basis,e) || basis.product!=o->dispatch->product)
        return fail(e,QA_ERROR_ARGUMENT,"Remote console changed its actual compiled CLIENT product");
    if(out)*out=actual;
    return true;
}
static bool invocation_current(frontend_remote_q3_commands *o,const qa_command_invocation *call,qa_error *e)
{
    frontend_remote_q3_services_view actual; qa_command_context expected;
    qa_frontend *f=frontend_remote_q3_frontend(o->row);
    if(!f || !current(o,&actual,e) || !qa_application_capture_command_context(f->application,
        &actual.resources.domain.source.receiver.command_context,&expected,e))return false;
    const qa_command_context *a=&call->context,*b=&expected;
    return call->console==o->dispatch->console && a->session==b->session && a->owner==b->owner &&
        a->client==b->client && a->seat==b->seat && a->dialect==b->dialect && a->origin==b->origin &&
        a->registry==b->registry && a->generation==b->generation && qa_actor_id_equal(a->actor,b->actor) &&
        qa_application_command_context_active(f->application,a) ? true :
        fail(e,QA_ERROR_ARGUMENT,"Remote command differs from its captured physical CLIENT origin");
}
static bool send(frontend_remote_q3_commands *o,const qa_command_invocation *call,const char *text,qa_error *e)
{
    return invocation_current(o,call,e) && frontend_network_client_reliable(
        frontend_remote_q3_frontend(o->row),&call->context,text,e) && invocation_current(o,call,e);
}
static bool append(frontend_remote_q3_commands *o,const qa_command_invocation *call,const char *text,qa_error *e)
{
    if(!invocation_current(o,call,e))return false;
    qa_command_context origin=call->context;
    origin.script="q3-cgame"; origin.direct=false; origin.console_text=false;
    return qa_console_append(call->console,&origin,text,e) && invocation_current(o,call,e);
}
static bool set(frontend_remote_q3_commands *o,const qa_command_invocation *call,const char *name,const char *value,qa_error *e)
{
    frontend_remote_q3_services_view actual;
    return invocation_current(o,call,e) && current(o,&actual,e) &&
        qa_cvars_set(actual.resources.domain.source.receiver.cvars,name,value,true,e) && invocation_current(o,call,e);
}
static int32_t crosshair(const q3n_frame *f,const q3n_hud_state *hud)
{ return f->time>sum(hud->crosshair_client_time,1000)?-1:hud->crosshair_client; }
static bool scores(frontend_remote_q3_commands *o,const qa_command_invocation *call,const q3n_frame *f,
    const frontend_remote_q3_runtime_owners *owners,qa_error *e)
{
    if(q3n_frame_product(f)==QA_Q3_TEAM_ARENA && !q3n_server_commands_spectators_build(owners->commands,f,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners->hud); bool due;
    if(!hud || !q3n_hud_scores_request(owners->hud,f,&due,e))return false;
    if(due && (!send(o,call,"score",e) ||
        (!hud->show_scores && !q3n_server_commands_scores_clear(owners->commands,f,e))))return false;
    q3n_hud_scores(owners->hud,true,f->time); return true;
}
static bool dispatch(frontend_remote_q3_commands *o,const qa_command_invocation *call,const q3n_frame *f,qa_error *e)
{
    const char *name=call->argv[0]; char operand[1024],text[1024]; const char *value=arg(call,1,operand);
    frontend_remote_q3_runtime_owners owners; frontend_remote_q3_services_view services;
    if(!frontend_remote_q3_runtime_owners_read(o->runtime,&owners,e) || !current(o,&services,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners.hud);
    const q3n_command_state *state=q3n_server_commands_state(owners.commands);
    if(!hud || !state)return fail(e,QA_ERROR_ARGUMENT,"Remote console children have an active operation");
    if(equal(name,"testmodel") || equal(name,"testgun")) {
        float back=0; const float *optional=NULL;
        if(call->argc==3) { char arg2[1024]; back=game_atof(arg(call,2,arg2)); optional=&back; }
        return q3n_view_test_model(owners.view,f,call->argc<2?NULL:value,optional,equal(name,"testgun"),e);
    }
    if(equal(name,"nextframe") || equal(name,"prevframe") || equal(name,"nextskin") || equal(name,"prevskin")) {
        q3n_view_test_step(owners.view,equal(name,"nextskin") || equal(name,"prevskin"),lower((unsigned char)*name)=='n'?1:-1); return true;
    }
    if(equal(name,"+zoom") || equal(name,"-zoom")) { q3n_view_zoom(owners.view,*name=='+',f->time); return true; }
    if(equal(name,"weapnext") || equal(name,"weapprev")) { q3n_weapons_cycle(f,equal(name,"weapnext")?1:-1); return true; }
    if(equal(name,"weapon")) { q3n_weapons_select(f,game_atoi(value)); return true; }
    if(equal(name,"viewpos")) {
        snprintf(text,sizeof(text),"(%i %i %i) : %i\n",integer(f->refdef.origin.x),integer(f->refdef.origin.y),
            integer(f->refdef.origin.z),integer(f->view_angles.y)); qa_console_emit(call->console,&call->context,text); return true;
    }
    if(equal(name,"sizeup") || equal(name,"sizedown")) {
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_remote_client_cvar_read(services.client,"cg_viewsize",&cached,e))return false;
        snprintf(text,sizeof(text),"%i",sum(cached.integer,equal(name,"sizeup")?10:-10)); return set(o,call,"cg_viewsize",text,e);
    }
    if(equal(name,"+scores"))return scores(o,call,f,&owners,e);
    if(equal(name,"-scores")) { if(hud->show_scores)q3n_hud_scores(owners.hud,false,f->time); return true; }
    if(equal(name,"tcmd")) {
        int32_t target=crosshair(f,hud); if(target==0)return true;
        char narrow[4]={0}; size_t n=strlen(value); if(n>3)n=3; memcpy(narrow,value,n);
        snprintf(text,sizeof(text),"gc %i %i",target,game_atoi(narrow)); return append(o,call,text,e);
    }
    if(equal(name,"tell_target") || equal(name,"tell_attacker") || equal(name,"vtell_target") || equal(name,"vtell_attacker")) {
        bool target=equal(name,"tell_target") || equal(name,"vtell_target"); int32_t client;
        if(target)client=crosshair(f,hud);
        else {
            const q3n_player_feedback *feedback=q3n_player_state_feedback(owners.player_state);
            if(!feedback)return fail(e,QA_ERROR_ARGUMENT,"Remote attacker feedback is active");
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
        size_t prefix=(size_t)snprintf(text,128,"%s %i ",lower((unsigned char)*name)=='v'?"vtell":"tell",client);
        size_t message=strlen(args); if(message>127-prefix)message=127-prefix;
        memcpy(text+prefix,args,message); text[prefix+message]=0;
        return send(o,call,text,e);
    }
    if(equal(name,"loaddeferred"))return frontend_remote_q3_runtime_load_deferred(o->runtime,f,e);
    if(equal(name,"startOrbit")) {
        const qa_cvar_view *developer=qa_cvars_find(services.resources.domain.source.receiver.cvars,"developer");
        char immediate[1024]; snprintf(immediate,sizeof(immediate),"%s",developer?developer->value:"");
        if(!game_atoi(immediate))return true;
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_remote_client_cvar_read(services.client,"cg_cameraOrbit",&cached,e))return false;
        if(cached.number!=0)return set(o,call,"cg_cameraOrbit","0",e) && set(o,call,"cg_thirdPerson","0",e);
        return set(o,call,"cg_cameraOrbit","5",e) && set(o,call,"cg_thirdPerson","1",e) &&
            set(o,call,"cg_thirdPersonAngle","0",e) && set(o,call,"cg_thirdPersonRange","100",e);
    }
    if(!owners.mission)return fail(e,QA_ERROR_UNSUPPORTED,"Remote Team Arena console requires its real menu owner");
    if(equal(name,"loadhud")) {
        const qa_cvar_view *file=qa_cvars_find(services.resources.domain.source.receiver.cvars,"cg_hudFiles");
        snprintf(text,sizeof(text),"%s",file && *file->value?file->value:"ui/hud.txt");
        return q3n_mission_hud_reset(owners.mission,f,true,e) && q3n_mission_hud_load_menus(owners.mission,f,text,e);
    }
    if(equal(name,"scoresDown") || equal(name,"scoresUp"))return q3n_mission_hud_scroll(owners.mission,f,equal(name,"scoresDown"),e);
    if(equal(name,"nextTeamMember") || equal(name,"prevTeamMember"))return q3n_mission_hud_select(owners.mission,f,equal(name,"nextTeamMember"),e);
    if(equal(name,"nextOrder"))return q3n_mission_hud_next_order(owners.mission,f,e);
    if(equal(name,"confirmOrder") || equal(name,"denyOrder")) {
        bool yes=equal(name,"confirmOrder"); int32_t leader=state->accept_leader,task=state->accept_task,until=state->accept_order_time;
        snprintf(text,sizeof(text),"cmd vtell %d %s\n",leader,yes?"yes":"no");
        if(!append(o,call,text,e) || !append(o,call,yes?"+button5; wait; -button5":"+button6; wait; -button6",e))return false;
        if(f->time<until) {
            if(yes) { snprintf(text,sizeof(text),"teamtask %d\n",task); if(!send(o,call,text,e))return false; }
            return q3n_server_commands_order_answered(owners.commands,f,e);
        }
        return true;
    }
    static const char *const tasks[]={"taskOffense","taskDefense","taskPatrol","taskCamp","taskFollow","taskRetrieve","taskEscort"};
    static const char *const voices[]={"onoffense","ondefense","onpatrol","oncamp","onfollow","onreturnflag","onfollowcarrier"};
    static const int32_t ids[]={1,2,3,7,4,5,6};
    for(size_t i=0;i<COUNT(tasks);++i)if(equal(name,tasks[i])) {
        const char *voice=i==0 && (state->game_type==4 || state->game_type==5)?"ongetflag":voices[i];
        snprintf(text,sizeof(text),"cmd vsay_team %s\n",voice); if(!append(o,call,text,e))return false;
        snprintf(text,sizeof(text),"teamtask %d\n",ids[i]); return send(o,call,text,e);
    }
    if(equal(name,"taskOwnFlag"))return append(o,call,"cmd vsay_team ihaveflag\n",e);
    if(equal(name,"taskSuicide")) { int32_t target=crosshair(f,hud); if(target==-1)return true;
        snprintf(text,128,"tell %i suicide",target); return send(o,call,text,e); }
    static const char *const taunts[]={"tauntKillInsult","tauntPraise","tauntTaunt","tauntDeathInsult","tauntGauntlet"};
    static const char *const calls[]={"cmd vsay kill_insult\n","cmd vsay praise\n","cmd vtaunt\n","cmd vsay death_insult\n","cmd vsay kill_guantlet\n"};
    for(size_t i=0;i<COUNT(taunts);++i)if(equal(name,taunts[i]))return append(o,call,calls[i],e);
    if(equal(name,"spWin") || equal(name,"spLose")) {
        bool win=equal(name,"spWin"); const q3n_media_view *media=q3n_media_read(owners.media);
        return media && set(o,call,"cg_cameraOrbit","2",e) && set(o,call,"cg_cameraOrbitDelay","35",e) &&
            set(o,call,"cg_thirdPerson","1",e) && set(o,call,"cg_thirdPersonAngle","0",e) && set(o,call,"cg_thirdPersonRange","100",e) &&
            q3n_events_buffer(owners.events,media->sounds[win?Q3N_S_WINNER:Q3N_S_LOSER],e) &&
            q3n_hud_center_print(owners.hud,f,win?"YOU WIN!":"YOU LOSE...",144,0,e);
    }
    return fail(e,QA_ERROR_FORMAT,"Remote registered console command lacks its genuine handler");
}
static bool dispose(frontend_remote_q3_commands *o,qa_error *e)
{
    o->closed=true;
    frontend_remote_q3_runtime_owners owners; frontend_remote_q3_services_view services;
    q3n_remote_source_view source;
    if(!frontend_remote_q3_runtime_owners_read(o->runtime,&owners,e) || !current(o,&services,e))return false;
    q3n_view_test_clear(owners.view);
    return q3n_remote_source_read(services.source,&source,e) && q3n_clients_remote_reset(owners.clients,&source,e);
}
typedef struct command_entry {
    frontend_remote_q3_commands *owner;
    const qa_command_invocation *call;
} command_entry;
static bool projected(void *context,const q3n_frame *frame,qa_error *e)
{
    command_entry *entry=context;
    if(!invocation_current(entry->owner,entry->call,e) || !dispatch(entry->owner,entry->call,frame,e))return false;
    if(!q3n_frame_current(frame))return fail(e,QA_ERROR_ARGUMENT,"Remote command lost its entered CLIENT frame");
    return invocation_current(entry->owner,entry->call,e);
}
static bool entered(void *context,const q3n_remote_frame *remote,qa_error *e)
{
    command_entry *entry=context;
    return invocation_current(entry->owner,entry->call,e) &&
        frontend_remote_q3_runtime_command_call(entry->owner->runtime,remote,entry,projected,e) &&
        invocation_current(entry->owner,entry->call,e);
}
static bool invocation_valid(remote_dispatch *d,const qa_command_invocation *call,qa_error *e)
{
    if(!call || !call->argc || call->argc>1024 || !call->argv || !call->raw ||
        call->console!=d->console || call->context.owner!=d->receiver)
        return fail(e,QA_ERROR_ARGUMENT,"Remote console lost its real tokenized CLIENT invocation");
    size_t bytes=0;
    for(size_t i=0;i<call->argc;++i) {
        if(!call->argv[i])return fail(e,QA_ERROR_ARGUMENT,"Remote console has an absent source token");
        size_t n=strlen(call->argv[i]);
        if(n>=9216-bytes)return fail(e,QA_ERROR_FORMAT,"Remote command exceeds source token storage");
        bytes+=n+1;
    }
    return true;
}
static bool handle(void *context,const qa_command_invocation *call,qa_error *e)
{
    remote_dispatch *d=context; frontend_remote_q3_commands *o=NULL;
    if(!invocation_valid(d,call,e))return false;
    for(frontend_remote_q3_commands *p=d->owners;p;p=p->next)if(p->seat==call->context.seat && !p->retiring) {
        if(!invocation_current(p,call,e))return false;
        if(o)return fail(e,QA_ERROR_FORMAT,"Remote console has duplicate actual CLIENT recipients");
        o=p;
    }
    if(!o || !o->registered || o->busy || o->closed || !frontend_remote_q3_runtime_idle(o->runtime))
        return fail(e,QA_ERROR_ARGUMENT,"Remote console has no idle open admitted CLIENT recipient");
    o->busy=true; bool okay=false,forward=false;
    for(size_t i=0;i<COUNT(forwarded);++i)if(equal(call->argv[0],forwarded[i])) { forward=true; break; }
    if(forward)okay=invocation_current(o,call,e) && frontend_network_client_forward(
        frontend_remote_q3_frontend(o->row),call,e) && invocation_current(o,call,e);
    else {
        command_entry entry={o,call};
        okay=frontend_remote_q3_frame_command(frontend_remote_q3_frames_read(o->row),&entry,entered,e);
    }
    if(!okay && !forward) {
        qa_error cleanup={0};
        if(!dispose(o,&cleanup) && e && e->code==QA_OK)*e=cleanup;
    }
    o->busy=false; return okay;
}
qa_command_result frontend_remote_q3_commands_execute(frontend_remote_q3_commands *o,
    const qa_command_invocation *call,qa_error *e)
{
    if(!o) {
        fail(e,QA_ERROR_ARGUMENT,"Remote source console lost its actual command continuation");
        return QA_COMMAND_FAILED;
    }
    if(!invocation_valid(o->dispatch,call,e) || !invocation_current(o,call,e))return QA_COMMAND_FAILED;
    if(!o->registered || o->busy || o->closed || o->retiring) {
        fail(e,QA_ERROR_ARGUMENT,"Remote source console has no open admitted command continuation");
        return QA_COMMAND_FAILED;
    }
    for(size_t i=0;i<local_count(o->dispatch->product);++i)
        if(equal(call->argv[0],command_name(o->dispatch->product,i)))
            return handle(o->dispatch,call,e)?QA_COMMAND_HANDLED:QA_COMMAND_FAILED;
    return QA_COMMAND_UNHANDLED;
}
static bool create(frontend_remote_q3_runtime *runtime,bool restoring,frontend_remote_q3_commands **out,qa_error *e)
{
    frontend_remote_q3 *row=frontend_remote_q3_runtime_parent(runtime);
    frontend_remote_q3_services_view services; qa_native_q3_remote_client_basis basis;
    qa_frontend *f=frontend_remote_q3_frontend(row);
    if(!f || !out || *out || f->capture || f->source_restoring!=restoring || frontend_remote_q3_runtime_read(row)!=runtime ||
        !frontend_remote_q3_services_read(row,&services,e) ||
        !qa_native_q3_remote_client_basis_read(services.client,&basis,e) ||
        (restoring?!frontend_remote_q3_runtime_restore_candidate_ready(runtime,e):basis.client.initialized))
        return fail(e,QA_ERROR_ARGUMENT,"Remote console construction requires its actual fresh or empty restoring runtime");
    frontend_remote_q3_commands *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining remote CLIENT console continuation");
    for(size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3 *p=frontend_remote_q3_at(f,i);
        frontend_remote_q3_commands *other=frontend_remote_q3_runtime_console(frontend_remote_q3_runtime_read(p));
        if(other && other->dispatch->console==basis.client.console && other->dispatch->receiver==basis.client.receiver) {
            if(other->dispatch->product!=basis.product || other->retiring) { free(o); return fail(e,QA_ERROR_ARGUMENT,"Remote console namespace has another product or retiring owner"); }
            o->dispatch=other->dispatch;
        }
    }
    if(!o->dispatch) {
        o->dispatch=calloc(1,sizeof(*o->dispatch));
        if(!o->dispatch) { free(o); return fail(e,QA_ERROR_MEMORY,"Retaining actual remote console dispatch namespace"); }
        *o->dispatch=(remote_dispatch){.console=basis.client.console,.receiver=basis.client.receiver,.product=basis.product};
    }
    o->runtime=runtime; o->row=row; o->identity=services.resources.identity;
    o->service=basis.client.service_owner; o->seat=basis.client.seat;
    o->next=o->dispatch->owners; o->dispatch->owners=o; *out=o; return true;
}
bool frontend_remote_q3_commands_create(frontend_remote_q3_runtime *runtime,frontend_remote_q3_commands **out,qa_error *e)
{ return create(runtime,false,out,e); }
bool frontend_remote_q3_commands_create_restored(frontend_remote_q3_runtime *runtime,frontend_remote_q3_commands **out,qa_error *e)
{ return create(runtime,true,out,e); }
static bool bind(frontend_remote_q3_commands *o,size_t installed,size_t contributed,qa_error *e)
{
    remote_dispatch *d=o->dispatch;
    for(size_t i=0;i<installed;++i) {
        const char *name=command_name(d->product,i);
        if(i>=d->installed) {
            if(!qa_console_register_owned(d->console,name,NULL,d->receiver,d->receiver,false,handle,d,e))return false;
            d->installed=i+1;
        }
        if(i<contributed && i>=o->contributed) {
            if(!qa_console_contribute(d->console,name,d->receiver,o->service,e))return false;
            o->contributed=i+1;
        }
    }
    return true;
}
bool frontend_remote_q3_commands_register(frontend_remote_q3_commands *o,qa_error *e)
{
    if(!o || o->busy || o->registered || o->closed || !current(o,NULL,e))return false;
    size_t count=command_count(o->dispatch->product);
    o->busy=true; bool okay=bind(o,count,count,e);
    if(okay) { o->registered=true; okay=current(o,NULL,e); }
    o->busy=false; return okay;
}
bool frontend_remote_q3_commands_idle(const frontend_remote_q3_commands *o)
{ return !o || !o->busy; }
bool frontend_remote_q3_commands_destroy(frontend_remote_q3_commands **owned,qa_error *e)
{
    if(!owned || !*owned)return true;
    frontend_remote_q3_commands *o=*owned; remote_dispatch *d=o->dispatch;
    if(o->busy || !qa_console_idle(d->console))return fail(e,QA_ERROR_ARGUMENT,"Remote console retirement retains an entered invocation");
    o->retiring=true;
    while(o->contributed) {
        if(!qa_console_uncontribute(d->console,command_name(d->product,o->contributed-1),d->receiver,o->service))
            return fail(e,QA_ERROR_ARGUMENT,"Remote console contribution retirement was refused");
        --o->contributed;
    }
    if(d->owners==o && !o->next)while(d->installed) {
        if(!qa_console_unregister(d->console,command_name(d->product,d->installed-1),d->receiver))
            return fail(e,QA_ERROR_ARGUMENT,"Remote console callback retirement was refused");
        --d->installed;
    }
    frontend_remote_q3_commands **link=&d->owners;
    while(*link && *link!=o)link=&(*link)->next;
    if(!*link)return fail(e,QA_ERROR_ARGUMENT,"Remote console lost its retained namespace membership");
    *link=o->next; if(!d->owners)free(d); free(o); *owned=NULL; return true;
}
static bool fields(qa_source_save_io *io,frontend_remote_q3_commands *o,size_t *installed)
{
    uint8_t magic[4]={'Q','R','C','C'}; uint32_t version=1,product=o->dispatch->product;
    uint64_t identity=o->identity; uint32_t seat=o->seat;
    return qa_source_save_bytes(io,magic,sizeof(magic)) && !memcmp(magic,"QRCC",sizeof(magic)) &&
        qa_source_save_u32(io,&version) && version==1 && qa_source_save_u32(io,&product) && product==(uint32_t)o->dispatch->product &&
        qa_source_save_u64(io,&identity) && identity==o->identity && qa_source_save_u32(io,&seat) && seat==o->seat &&
        qa_source_save_bool(io,&o->registered) && qa_source_save_bool(io,&o->closed) && (!o->closed || o->registered) &&
        qa_source_save_count(io,installed,command_count(o->dispatch->product)) &&
        qa_source_save_count(io,&o->contributed,*installed) &&
        o->registered==(o->contributed==command_count(o->dispatch->product)) &&
        (!o->registered || *installed==o->contributed);
}
bool frontend_remote_q3_commands_checkpoint(const frontend_remote_q3_commands *o,qa_buffer *out,qa_error *e)
{
    if(!o || o->busy || o->retiring || !out || out->data || out->size ||
       !qa_console_idle(o->dispatch->console) || !current(o,NULL,e))return false;
    frontend_remote_q3_commands copy=*o; qa_source_save_io io={0}; size_t installed=o->dispatch->installed;
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&io,&copy,&installed) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_remote_q3_commands_restore(frontend_remote_q3_commands *o,qa_bytes bytes,qa_error *e)
{
    if(!o || o->busy || o->retiring || !qa_console_idle(o->dispatch->console) || !current(o,NULL,e))return false;
    qa_frontend *f=frontend_remote_q3_frontend(o->row);
    if(!f || !f->source_restoring)return fail(e,QA_ERROR_ARGUMENT,"Remote console import requires its actual restoring frontend");
    frontend_remote_q3_commands candidate=*o; qa_source_save_io io={0}; size_t installed=o->dispatch->installed;
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&candidate,&installed) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!okay)return fail(e,QA_ERROR_FORMAT,"Invalid remote CLIENT console continuation");
    if(o->registered || o->closed)
        return o->contributed==candidate.contributed && o->dispatch->installed==installed &&
            o->registered==candidate.registered && o->closed==candidate.closed ? true :
            fail(e,QA_ERROR_FORMAT,"Remote console continuation differs from its actual registration prefix");
    if(o->contributed>candidate.contributed || o->dispatch->installed>installed)
        return fail(e,QA_ERROR_FORMAT,"Remote console import contradicts its actual retained namespace prefix");
    o->busy=true; okay=bind(o,installed,candidate.contributed,e);
    if(okay) { o->registered=candidate.registered; o->closed=candidate.closed; okay=current(o,NULL,e); }
    o->busy=false; return okay;
}
