/* Team Arena cg_consolecmds.c, id Software; GPL-2.0-or-later. */
#include "cgame_commands.h"
#include "weapon.h"
#include "qa/application_equipment.h"
#include "qa/session.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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
size_t q3n_cgame_local_command_count(qa_q3_product product)
{ return COUNT(common)+(product==QA_Q3_TEAM_ARENA?COUNT(mission):0)+COUNT(tail); }
size_t q3n_cgame_command_count(qa_q3_product product)
{ return q3n_cgame_local_command_count(product)+COUNT(forwarded); }
const char *q3n_cgame_command_name(qa_q3_product product,size_t index)
{
    if(index<COUNT(common))return common[index];
    index-=COUNT(common);
    if(product==QA_Q3_TEAM_ARENA) { if(index<COUNT(mission))return mission[index]; index-=COUNT(mission); }
    if(index<COUNT(tail))return tail[index];
    index-=COUNT(tail);
    return index<COUNT(forwarded)?forwarded[index]:NULL;
}
static unsigned char lower(unsigned char c) { return c>='A' && c<='Z'?(unsigned char)(c+32):c; }
bool q3n_cgame_command_equal(const char *a,const char *b)
{ while(*a && *b && lower((unsigned char)*a)==lower((unsigned char)*b)) { ++a; ++b; } return !*a && !*b; }
bool q3n_cgame_weapon_command(const q3n_frame *frame,const qa_command_invocation *command,
    qa_actor_id actor,qa_error *error)
{
    if (actor.registry) {
        qa_application_item_selection selected;
        qa_command_result result=qa_application_item_command(frame->application,actor,command,&selected,error);
        if (result==QA_COMMAND_FAILED) return false;
        if (result==QA_COMMAND_HANDLED) {
            if (selected.item && selected.q3_weapon>=0) {
                bool same_source=selected.provider==frame->source.source_owner;
                if (frame->compiled) {
                    const char *requested=qa_strings_cstr(qa_session_strings(
                        qa_application_session(frame->application)),selected.provider);
                    const char *receiver=qa_strings_cstr(qa_session_strings(
                        frame->compiled->source.basis.session),frame->compiled->source.basis.instance);
                    same_source=requested && receiver && !strcmp(requested,receiver);
                }
                if (same_source) q3n_weapons_select(frame,selected.q3_weapon);
            }
            return true;
        }
    }
    const char *value=command->argc>1?command->argv[1]:"";
    q3n_weapons_select(frame,qa_parse_decimal_i32(value,QA_DECIMAL_SIGNED_WRAP));
    return true;
}
static bool fail(qa_error *e,qa_status status,const char *text)
{ qa_error_set(e,status,0,"%s",text);return false; }
static int32_t crosshair(const q3n_frame *f,const q3n_hud_state *hud)
{ uint32_t word=(uint32_t)hud->crosshair_client_time+1000;int32_t until;memcpy(&until,&word,4);
    return f->time>until?-1:hud->crosshair_client; }
bool q3n_cgame_mission_command(const q3n_cgame_command_services *s,
    const qa_command_invocation *command,const q3n_frame *f,qa_error *e)
{
    const char *name=command->argv[0];char text[1024];
    const q3n_hud_state *hud=q3n_hud_read(s->hud);
    const q3n_command_state *state=q3n_server_commands_state(s->commands);
    if(!s->mission)return fail(e,QA_ERROR_UNSUPPORTED,"Team Arena console requires its menu owner");
    if(q3n_cgame_command_equal(name,"loadhud")) {
        const qa_cvar_view *file=qa_cvars_find(s->cvars,"cg_hudFiles");
        const char *path=file && *file->value?file->value:"ui/hud.txt";
        size_t n=strlen(path); if(n>1023)n=1023; memcpy(text,path,n); text[n]=0;
        return q3n_mission_hud_reset(s->mission,f,true,e) && q3n_mission_hud_load_menus(s->mission,f,text,e);
    }
    if(q3n_cgame_command_equal(name,"scoresDown") || q3n_cgame_command_equal(name,"scoresUp"))return q3n_mission_hud_scroll(s->mission,f,q3n_cgame_command_equal(name,"scoresDown"),e);
    if(q3n_cgame_command_equal(name,"nextTeamMember") || q3n_cgame_command_equal(name,"prevTeamMember"))return q3n_mission_hud_select(s->mission,f,q3n_cgame_command_equal(name,"nextTeamMember"),e);
    if(q3n_cgame_command_equal(name,"nextOrder"))return q3n_mission_hud_next_order(s->mission,f,e);
    if(q3n_cgame_command_equal(name,"confirmOrder") || q3n_cgame_command_equal(name,"denyOrder")) {
        bool yes=q3n_cgame_command_equal(name,"confirmOrder"); int32_t leader=state->accept_leader,task=state->accept_task,until=state->accept_order_time;
        snprintf(text,sizeof(text),"cmd vtell %d %s\n",leader,yes?"yes":"no");
        if(!s->append(s->context,command,text,e) || !s->append(s->context,command,yes?"+button5; wait; -button5":"+button6; wait; -button6",e))return false;
        if(f->time<until) {
            if(yes) { snprintf(text,sizeof(text),"teamtask %d\n",task); if(!s->send(s->context,command,text,e))return false; }
            return q3n_server_commands_order_answered(s->commands,f,e);
        }
        return true;
    }
    static const char *const tasks[]={"taskOffense","taskDefense","taskPatrol","taskCamp","taskFollow","taskRetrieve","taskEscort"};
    static const char *const voices[]={"onoffense","ondefense","onpatrol","oncamp","onfollow","onreturnflag","onfollowcarrier"};
    static const int32_t ids[]={1,2,3,7,4,5,6};
    for(size_t i=0;i<COUNT(tasks);++i)if(q3n_cgame_command_equal(name,tasks[i])) {
        const char *voice=i==0 && (state->game_type==4 || state->game_type==5)?"ongetflag":voices[i];
        snprintf(text,sizeof(text),"cmd vsay_team %s\n",voice); if(!s->append(s->context,command,text,e))return false;
        snprintf(text,sizeof(text),"teamtask %d\n",ids[i]); return s->send(s->context,command,text,e);
    }
    if(q3n_cgame_command_equal(name,"taskOwnFlag"))return s->append(s->context,command,"cmd vsay_team ihaveflag\n",e);
    if(q3n_cgame_command_equal(name,"taskSuicide")) { int32_t target=crosshair(f,hud); if(target==-1)return true;
        snprintf(text,128,"tell %i suicide",target); return s->send(s->context,command,text,e); }
    static const char *const taunts[]={"tauntKillInsult","tauntPraise","tauntTaunt","tauntDeathInsult","tauntGauntlet"};
    static const char *const calls[]={"cmd vsay kill_insult\n","cmd vsay praise\n","cmd vtaunt\n","cmd vsay death_insult\n","cmd vsay kill_guantlet\n"};
    for(size_t i=0;i<COUNT(taunts);++i)if(q3n_cgame_command_equal(name,taunts[i]))return s->append(s->context,command,calls[i],e);
    if(q3n_cgame_command_equal(name,"spWin") || q3n_cgame_command_equal(name,"spLose")) {
        bool win=q3n_cgame_command_equal(name,"spWin"); const q3n_media_view *media=q3n_media_read(s->media);
        const qa_cvar_view *center_time=qa_cvars_find(s->cvars,"cg_centertime");
        uint64_t duration=(uint64_t)(fmax(0,fmin(86400,center_time?center_time->number:3))*1e9);
        return media && s->set(s->context,command,"cg_cameraOrbit","2",e) && s->set(s->context,command,"cg_cameraOrbitDelay","35",e) &&
            s->set(s->context,command,"cg_thirdPerson","1",e) && s->set(s->context,command,"cg_thirdPersonAngle","0",e) && s->set(s->context,command,"cg_thirdPersonRange","100",e) &&
            q3n_events_buffer(s->events,media->sounds[win?Q3N_S_WINNER:Q3N_S_LOSER],e) &&
            qa_hud_center_print(s->messages,win?"YOU WIN!":"YOU LOSE...",
                (uint64_t)(uint32_t)f->time*UINT64_C(1000000),duration,
                (qa_hud_center_policy){.instant=true,.source_layout=true,.y=144,.character_width=0,.fade_ns=UINT64_C(200000000)},e);
    }
    return fail(e,QA_ERROR_FORMAT,"Registered cgame command lacks its handler");
}
