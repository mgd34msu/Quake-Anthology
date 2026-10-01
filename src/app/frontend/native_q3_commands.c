#include "native_q3_client_internal.h"
#include <limits.h>
#include <stdio.h>

typedef struct native_dispatch {
    qa_frontend *frontend;
    qa_console *console;
    qa_actor_owner source;
    qa_q3_product product;
    size_t users, installed;
} native_dispatch;
struct frontend_native_q3_commands {
    frontend_native_q3 *row;
    native_dispatch *dispatch;
    size_t contributed;
    int32_t scores_request_time;
    bool registered, closed, busy;
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
#define QA_NATIVE_COUNT(array) (sizeof(array)/sizeof((array)[0]))
static size_t local_count(qa_q3_product product)
{ return QA_NATIVE_COUNT(common)+(product==QA_Q3_TEAM_ARENA?QA_NATIVE_COUNT(mission):0)+QA_NATIVE_COUNT(tail); }
static size_t command_count(qa_q3_product product)
{ return local_count(product)+QA_NATIVE_COUNT(forwarded); }
static const char *command_name(qa_q3_product product,size_t index)
{
    if(index<QA_NATIVE_COUNT(common))return common[index]; index-=QA_NATIVE_COUNT(common);
    if(product==QA_Q3_TEAM_ARENA) { if(index<QA_NATIVE_COUNT(mission))return mission[index]; index-=QA_NATIVE_COUNT(mission); }
    if(index<QA_NATIVE_COUNT(tail))return tail[index]; index-=QA_NATIVE_COUNT(tail);
    return index<QA_NATIVE_COUNT(forwarded)?forwarded[index]:NULL;
}
static unsigned char lower(unsigned char c) { return c>='A' && c<='Z'?(unsigned char)(c+32):c; }
static bool equal(const char *a,const char *b)
{ while(*a && *b && lower((unsigned char)*a)==lower((unsigned char)*b)) { ++a; ++b; } return !*a && !*b; }
static int32_t signed_word(uint32_t word) { int32_t out; memcpy(&out,&word,4); return out; }
static int32_t sum(int32_t a,int32_t b) { return signed_word((uint32_t)a+(uint32_t)b); }
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
static const char *arg(const qa_command_invocation *command,size_t index,char out[1024])
{
    const char *text=index<command->argc?command->argv[index]:"";
    size_t n=strlen(text); if(n>1023)n=1023; memcpy(out,text,n); out[n]=0; return out;
}
static bool set(frontend_native_q3 *row,const char *name,const char *value,qa_error *e)
{ return qa_cvars_set(row->view.cvars,name,value,true,e); }
static bool send(frontend_native_q3 *row,const char *text,qa_error *e)
{ return qa_native_q3_client_reliable(row->view.client,text,e); }
static bool append(frontend_native_q3 *row,const char *text,qa_error *e)
{ return qa_native_q3_client_console(row->view.client,text,e); }
static int32_t crosshair(const q3n_frame *f,const q3n_hud_state *hud)
{ return f->time>sum(hud->crosshair_client_time,1000)?-1:hud->crosshair_client; }
static bool scores(frontend_native_q3_commands *o,const q3n_frame *f,qa_error *e)
{
    q3n_native_owners owners; frontend_native_q3 *row=o->row;
    if(!q3n_native_owners_read(row->view.core,&owners,e))return false;
    if(f->source.product==QA_Q3_TEAM_ARENA && !q3n_server_commands_spectators_build(owners.commands,f,e))return false;
    const q3n_hud_state *hud=q3n_hud_read(owners.hud); if(!hud)return false;
    if(sum(o->scores_request_time,2000)<f->time) {
        o->scores_request_time=f->time;
        if(!send(row,"score",e))return false;
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
    if(equal(name,"testmodel") || equal(name,"testgun")) {
        float back=0; const float *optional=NULL;
        if(command->argc==3) { char arg2[1024]; back=game_atof(arg(command,2,arg2)); optional=&back; }
        return q3n_view_test_model(owners.view,f,command->argc<2?NULL:value,optional,equal(name,"testgun"),e);
    }
    if(equal(name,"nextframe") || equal(name,"prevframe") || equal(name,"nextskin") || equal(name,"prevskin")) {
        q3n_view_test_step(owners.view,equal(name,"nextskin") || equal(name,"prevskin"),lower((unsigned char)*name)=='n'?1:-1); return true;
    }
    if(equal(name,"+zoom") || equal(name,"-zoom")) { q3n_view_zoom(owners.view,*name=='+',f->time); return true; }
    if(equal(name,"weapnext") || equal(name,"weapprev")) { q3n_weapons_cycle(f,equal(name,"weapnext")?1:-1); return true; }
    if(equal(name,"weapon")) { q3n_weapons_select(f,game_atoi(value)); return true; }
    if(equal(name,"viewpos")) {
        snprintf(text,sizeof(text),"(%i %i %i) : %i\n",integer(f->refdef.origin.x),integer(f->refdef.origin.y),
            integer(f->refdef.origin.z),integer(f->view_angles.y)); qa_console_emit(row->console,&command->context,text); return true;
    }
    if(equal(name,"sizeup") || equal(name,"sizedown")) {
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_client_cvar_read(row->view.client,"cg_viewsize",&cached,e))return false;
        snprintf(text,sizeof(text),"%i",sum(cached.integer,equal(name,"sizeup")?10:-10)); return set(row,"cg_viewsize",text,e);
    }
    if(equal(name,"+scores"))return scores(o,f,e);
    if(equal(name,"-scores")) { if(hud->show_scores)q3n_hud_scores(owners.hud,false,f->time); return true; }
    if(equal(name,"tcmd")) {
        int32_t target=crosshair(f,hud); if(target==0)return true;
        char narrow[4]={0}; size_t n=strlen(value); if(n>3)n=3; memcpy(narrow,value,n);
        snprintf(text,sizeof(text),"gc %i %i",target,game_atoi(narrow)); return append(row,text,e);
    }
    if(equal(name,"tell_target") || equal(name,"tell_attacker") || equal(name,"vtell_target") || equal(name,"vtell_attacker")) {
        const q3n_player_feedback *feedback=q3n_player_state_feedback(owners.player_state);
        bool target=equal(name,"tell_target") || equal(name,"vtell_target");
        int32_t client=target?crosshair(f,hud):feedback && feedback->attacker_time?f->local_player.persistant[6]:-1;
        if(client==-1)return true;
        char args[1024]={0}; size_t used=0;
        for(size_t i=1;i<command->argc;++i) {
            size_t n=strlen(command->argv[i]); size_t space=i>1?1:0;
            if(n+space>=sizeof(args)-used)return frontend_fail(e,QA_ERROR_FORMAT,"Cmd_Args exceeds MAX_STRING_CHARS");
            if(space)args[used++]=' '; memcpy(args+used,command->argv[i],n); used+=n;
        }
        args[used]=0; if(used>127)args[127]=0;
        snprintf(text,128,"%s %i %s",lower((unsigned char)*name)=='v'?"vtell":"tell",client,args); return send(row,text,e);
    }
    if(equal(name,"loaddeferred")) {
        q3n_native_frame_options projected;
        if(!frontend_native_q3_project_settings(row,&projected,e))return false;
        return q3n_clients_load_deferred(owners.clients,f->application,&f->source,&projected.clients,e);
    }
    if(equal(name,"startOrbit")) {
        const qa_cvar_view *developer=qa_cvars_find(row->view.cvars,"developer");
        if(!developer || !game_atoi(developer->value))return true;
        qa_native_q3_client_cvar cached;
        if(!qa_native_q3_client_cvar_read(row->view.client,"cg_cameraOrbit",&cached,e))return false;
        if(cached.number!=0)return set(row,"cg_cameraOrbit","0",e) && set(row,"cg_thirdPerson","0",e);
        return set(row,"cg_cameraOrbit","5",e) && set(row,"cg_thirdPerson","1",e) &&
            set(row,"cg_thirdPersonAngle","0",e) && set(row,"cg_thirdPersonRange","100",e);
    }
    if(!row->view.mission)return frontend_fail(e,QA_ERROR_UNSUPPORTED,"Team Arena console requires its actual menu owner");
    if(equal(name,"loadhud")) {
        const qa_cvar_view *file=qa_cvars_find(row->view.cvars,"cg_hudFiles");
        const char *path=file && *file->value?file->value:"ui/hud.txt";
        size_t n=strlen(path); if(n>1023)n=1023; memcpy(text,path,n); text[n]=0;
        return q3n_mission_hud_reset(row->view.mission,f,true,e) && q3n_mission_hud_load_menus(row->view.mission,f,text,e);
    }
    if(equal(name,"scoresDown") || equal(name,"scoresUp"))return q3n_mission_hud_scroll(row->view.mission,f,equal(name,"scoresDown"),e);
    if(equal(name,"nextTeamMember") || equal(name,"prevTeamMember"))return q3n_mission_hud_select(row->view.mission,f,equal(name,"nextTeamMember"),e);
    if(equal(name,"nextOrder"))return q3n_mission_hud_next_order(row->view.mission,f,e);
    if(equal(name,"confirmOrder") || equal(name,"denyOrder")) {
        bool yes=equal(name,"confirmOrder"); int32_t leader=state->accept_leader,task=state->accept_task,until=state->accept_order_time;
        snprintf(text,sizeof(text),"cmd vtell %d %s\n",leader,yes?"yes":"no");
        if(!append(row,text,e) || !append(row,yes?"+button5; wait; -button5":"+button6; wait; -button6",e))return false;
        if(f->time<until) {
            if(yes) { snprintf(text,sizeof(text),"teamtask %d\n",task); if(!send(row,text,e))return false; }
            return q3n_server_commands_order_answered(owners.commands,f,e);
        }
        return true;
    }
    static const char *const tasks[]={"taskOffense","taskDefense","taskPatrol","taskCamp","taskFollow","taskRetrieve","taskEscort"};
    static const char *const voices[]={"onoffense","ondefense","onpatrol","oncamp","onfollow","onreturnflag","onfollowcarrier"};
    static const int32_t ids[]={1,2,3,7,4,5,6};
    for(size_t i=0;i<QA_NATIVE_COUNT(tasks);++i)if(equal(name,tasks[i])) {
        const char *voice=i==0 && (state->game_type==4 || state->game_type==5)?"ongetflag":voices[i];
        snprintf(text,sizeof(text),"cmd vsay_team %s\n",voice); if(!append(row,text,e))return false;
        snprintf(text,sizeof(text),"teamtask %d\n",ids[i]); return send(row,text,e);
    }
    if(equal(name,"taskOwnFlag"))return append(row,"cmd vsay_team ihaveflag\n",e);
    if(equal(name,"taskSuicide")) { int32_t target=crosshair(f,hud); if(target==-1)return true;
        snprintf(text,128,"tell %i suicide",target); return send(row,text,e); }
    static const char *const taunts[]={"tauntKillInsult","tauntPraise","tauntTaunt","tauntDeathInsult","tauntGauntlet"};
    static const char *const calls[]={"cmd vsay kill_insult\n","cmd vsay praise\n","cmd vtaunt\n","cmd vsay death_insult\n","cmd vsay kill_guantlet\n"};
    for(size_t i=0;i<QA_NATIVE_COUNT(taunts);++i)if(equal(name,taunts[i]))return append(row,calls[i],e);
    if(equal(name,"spWin") || equal(name,"spLose")) {
        bool win=equal(name,"spWin"); const q3n_media_view *media=q3n_media_read(owners.media);
        return media && set(row,"cg_cameraOrbit","2",e) && set(row,"cg_cameraOrbitDelay","35",e) &&
            set(row,"cg_thirdPerson","1",e) && set(row,"cg_thirdPersonAngle","0",e) && set(row,"cg_thirdPersonRange","100",e) &&
            q3n_events_buffer(owners.events,media->sounds[win?Q3N_S_WINNER:Q3N_S_LOSER],e) &&
            q3n_hud_center_print(owners.hud,f,win?"YOU WIN!":"YOU LOSE...",144,0,e);
    }
    return frontend_fail(e,QA_ERROR_FORMAT,"Native registered console command lacks its genuine handler");
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
    if(!command || !command->argc || command->argc>1024 || command->console!=d->console || command->context.owner!=d->source)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console lost its real source invocation");
    size_t bytes=0;
    for(size_t i=0;i<command->argc;++i) { size_t n=strlen(command->argv[i]); if(n>=9216-bytes)return false; bytes+=n+1; }
    for(frontend_native_q3 *row=d->frontend->native_q3;row;row=row->next)
        if(row->constructed && row->console==d->console && row->view.source_owner==d->source &&
            row->view.launch_seat==command->context.seat && qa_actor_id_equal(row->view.actor,command->context.actor)) {
            if(o)return frontend_fail(e,QA_ERROR_FORMAT,"Native console has duplicate actual recipients"); o=row->commands;
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
bool frontend_native_q3_commands_create(frontend_native_q3 *row,bool restoring,frontend_native_q3_commands **out,qa_error *e)
{
    if(!row || !out || *out || (!restoring && !row->view.client) ||
        (row->view.product!=QA_Q3_ARENA && row->view.product!=QA_Q3_TEAM_ARENA))return false;
    frontend_native_q3_commands *o=calloc(1,sizeof(*o)); if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Allocating native console state");
    o->row=row;
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
    for(size_t i=0;i<count;++i) {
        const char *name=command_name(d->product,i);
        if(i<local_count(d->product) && i>=d->installed) {
            if(!qa_console_register_owned(d->console,name,NULL,d->source,d->source,false,handle,d,e))return false;
            d->installed=i+1;
        }
        if(i>=o->contributed) {
            if(!qa_console_contribute(d->console,name,d->source,o->row->view.service_owner,e))return false;
            o->contributed=i+1;
        }
    }
    return true;
}
bool frontend_native_q3_commands_register(frontend_native_q3_commands *o,const q3n_frame *f,qa_error *e)
{
    if(!o || o->busy || o->registered || !frontend_native_q3_cut(o->row,f,e))return false;
    o->busy=true; bool ok=bind_commands(o,command_count(o->dispatch->product),e);
    if(ok)o->registered=true; o->busy=false; return ok;
}
bool frontend_native_q3_commands_idle(const frontend_native_q3_commands *o) { return !o || !o->busy; }
bool frontend_native_q3_commands_destroy(frontend_native_q3_commands *o,qa_error *e)
{
    if(!o)return true;
    if(o->busy || !qa_console_idle(o->dispatch->console))return frontend_fail(e,QA_ERROR_ARGUMENT,"Native console teardown requires inactive callbacks");
    native_dispatch *d=o->dispatch;
    if(d->users==1)for(size_t i=0;i<d->installed;++i)qa_console_unregister(d->console,command_name(d->product,i),d->source);
    for(size_t i=0;i<o->contributed;++i)qa_console_uncontribute(d->console,command_name(d->product,i),d->source,o->row->view.service_owner);
    if(!--d->users)free(d); free(o); return true;
}
static bool fields(qa_source_save_io *io,frontend_native_q3_commands *o)
{
    uint8_t magic[4]={'Q','N','C','C'}; uint32_t version=1,product=o->dispatch->product;
    uint64_t identity=o->row->view.identity; size_t contributed=o->contributed;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QNCC",4) || !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_u32(io,&product) || product!=(uint32_t)o->dispatch->product ||
        !qa_source_save_u64(io,&identity) || identity!=o->row->view.identity ||
        !qa_source_save_i32(io,&o->scores_request_time) || !qa_source_save_bool(io,&o->registered) ||
        !qa_source_save_bool(io,&o->closed) || (o->closed && !o->registered) ||
        !qa_source_save_count(io,&contributed,command_count(o->dispatch->product)) ||
        (o->registered && contributed!=command_count(o->dispatch->product)))return false;
    o->contributed=contributed; return true;
}
bool frontend_native_q3_commands_checkpoint(const frontend_native_q3_commands *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed || !frontend_native_q3_commands_idle(borrowed) || !out || out->data || out->size)return false;
    frontend_native_q3_commands copy=*borrowed; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&copy) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_native_q3_commands_restore(frontend_native_q3_commands *o,qa_bytes bytes,qa_error *e)
{
    if(!o || o->busy || !o->row->restoring)return false;
    frontend_native_q3_commands candidate=*o; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok)return frontend_fail(e,QA_ERROR_FORMAT,"Invalid actual native console continuation");
    if(o->contributed || o->registered)
        return o->contributed==candidate.contributed && o->registered==candidate.registered &&
            o->closed==candidate.closed && o->scores_request_time==candidate.scores_request_time ? true :
            frontend_fail(e,QA_ERROR_FORMAT,"Native command continuation differs from its prepared callback prefix");
    if(!bind_commands(o,candidate.contributed,e))return false;
    o->scores_request_time=candidate.scores_request_time; o->registered=candidate.registered; o->closed=candidate.closed; return true;
}
void frontend_native_q3_commands_rebind(frontend_native_q3_commands *o,qa_frontend *f)
{ if(o)o->dispatch->frontend=f; }
