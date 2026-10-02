#include "internal.h"
#include "source_report.h"
#include "qa/network_q3.h"
#include <stdio.h>

static bool same_name(const char *left,const char *right)
{
    for(;;++left,++right) {
        unsigned char a=(unsigned char)*left,b=(unsigned char)*right;
        if(a>='A' && a<='Z') a+='a'-'A';
        if(b>='A' && b<='Z') b+='a'-'A';
        if(a!=b) return false;
        if(!a) return true;
    }
}

static bool integer(qa_bots *bots,bot_ai_state *state,uint32_t offset,int32_t *out,qa_error *error)
{
    return qa_bot_source_record_i32(&bots->services.memory,state->source_record,offset,out,false,error);
}

static void cube_count(int32_t value,char out[32])
{
    uint32_t bits=value<0?0u-(uint32_t)value:(uint32_t)value;
    int32_t remaining;memcpy(&remaining,&bits,sizeof(remaining));
    char reversed[16];size_t count=0,at=0;
    do {reversed[count++]=(char)(48+remaining%10);remaining/=10;} while(remaining);
    if(value<0) reversed[count++]='-';
    if(count<2) out[at++]=' ';
    while(count) out[at++]=reversed[--count];
    out[at]=0;
}

static bool carrying(qa_bots *bots,bot_ai_state *state,int32_t client,char out[32],qa_error *error)
{
    int32_t team,red,blue,neutral;
    if(!bot_ai_source_team(bots,client,&team,error) ||
       !integer(bots,state,QA_BOT_SOURCE_INVENTORY+45*4,&red,error) ||
       !integer(bots,state,QA_BOT_SOURCE_INVENTORY+46*4,&blue,error)) return false;
    strcpy(out,"  ");
    if(bots->source_goals.game_type==4 && (red>0 || blue>0)) {strcpy(out,"F ");return true;}
    if(!bots->services.team_arena) return true;
    if(bots->source_goals.game_type==5) {
        if(!integer(bots,state,QA_BOT_SOURCE_INVENTORY+47*4,&neutral,error)) return false;
        if(neutral>0) strcpy(out,"F ");
    } else if(bots->source_goals.game_type==7) {
        if(!integer(bots,state,QA_BOT_SOURCE_INVENTORY+48*4,&red,error) ||
           !integer(bots,state,QA_BOT_SOURCE_INVENTORY+49*4,&blue,error)) return false;
        if(red>0 || blue>0) cube_count(team==1?red:blue,out);
    }
    return true;
}

static bool action(qa_bots *bots,bot_ai_state *state,char out[512],qa_error *error)
{
    int32_t type,teammate;char name[256];const char *prefix=NULL,*description=NULL;
    qa_bot_goal goal;
    if(!integer(bots,state,QA_BOT_SOURCE_LTG_TYPE,&type,error)) return false;
    switch(type) {
    case BOT_LTG_TEAM_HELP:case BOT_LTG_TEAM_ACCOMPANY:
        if(!integer(bots,state,QA_BOT_SOURCE_TEAMMATE,&teammate,error) ||
           !bot_ai_easy_name(bots,teammate,name,sizeof(name),error)) return false;
        prefix=type==BOT_LTG_TEAM_HELP?"helping ":"accompanying ";description=name;break;
    case BOT_LTG_DEFEND:case BOT_LTG_GET_ITEM:case BOT_LTG_KILL:
        if(!qa_bot_source_record_goal(&bots->services.memory,state->source_record,
            QA_BOT_SOURCE_TEAM_GOAL,&goal,false,error)) return false;
        if(type==BOT_LTG_KILL) {
            if(!bot_ai_client_name(bots,goal.entity,name,sizeof(name),true,error)) return false;
            prefix="killing ";description=name;
        } else {
            prefix=type==BOT_LTG_DEFEND?"defending ":"getting item ";
            if(!qa_bot_goals_name_read(qa_bot_runtime_goals(bots->runtime),goal.number,&description,error)) return false;
        }
        break;
    case BOT_LTG_CAMP:case BOT_LTG_CAMP_ORDER:description="camping";break;
    case BOT_LTG_PATROL:description="patrolling";break;
    case BOT_LTG_GET_FLAG:description="capturing flag";break;
    case BOT_LTG_RUSH_BASE:description="rushing base";break;
    case BOT_LTG_RETURN_FLAG:description="returning flag";break;
    case BOT_LTG_ATTACK_BASE:description="attacking the enemy base";break;
    case BOT_LTG_HARVEST:description="harvesting";break;
    default: {
        int32_t handle;bool found;
        if(!integer(bots,state,QA_BOT_SOURCE_GOALS,&handle,error) ||
           !qa_bot_goals_top(qa_bot_runtime_goals(bots->runtime),(uint32_t)handle,false,&goal,&found,error)) return false;
        if(!found) return bot_ai_fail(error,"BotSetInfoConfigString reads an uninitialized source goal when the goal stack is empty");
        prefix="roaming ";
        if(!qa_bot_goals_name_read(qa_bot_runtime_goals(bots->runtime),goal.number,&description,error)) return false;
        break;
    }
    }
    snprintf(out,512,"%s%s",prefix?prefix:"",description?description:"");
    out[255]=0;
    return true;
}

bool bot_ai_source_report(qa_bots *bots,qa_error *error)
{
    if(!bots->services.configstring || !bots->services.set_configstring)
        return bot_ai_fail(error,"BotSetInfoConfigString requires its actual source configstring owner");
    for(int32_t index=0;index<bots->source_goals.max_clients && index<64;++index) {
        bot_ai_state *state=bots->source_cells[index];bool inuse;
        if(!state) continue;
        if(!qa_bot_source_record_bool(&bots->services.memory,state->source_record,
            QA_BOT_SOURCE_INUSE,&inuse,false,error)) return false;
        if(!inuse) continue;
        char information[1024],name[256],leader[257],cargo[32],description[512],value[320];
        if(!bots->services.configstring(bots->services.context,544+(uint32_t)index,
            information,sizeof(information),error) ||
           !qa_q3_info_value(information,"n",name,sizeof(name),error)) return false;
        if(!information[0] || !name[0]) continue;
        int32_t client;
        if(!integer(bots,state,QA_BOT_SOURCE_CLIENT,&client,error) ||
           !bot_ai_client_name(bots,client,name,sizeof(name),true,error) ||
           !qa_bot_source_record_text_read(&bots->services.memory,state->source_record,
                QA_BOT_SOURCE_TEAM_LEADER,leader,sizeof(leader),error) ||
           !carrying(bots,state,client,cargo,error) || !action(bots,state,description,error)) return false;
        snprintf(value,sizeof(value),"l\\%s\\c\\%s\\a\\%s",same_name(name,leader)?"L":" ",cargo,description);
        if(!bots->services.set_configstring(bots->services.context,25u+(uint32_t)client,value,error)) return false;
    }
    return true;
}
