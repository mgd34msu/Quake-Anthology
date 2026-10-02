/* Q3 ai_team.c and ai_dmq3.c policy over the retained GAME and botlib owners. */
#include "internal.h"
#include "source_goal_record.h"
#include "source_team_state.h"
#include "source_timers.h"
#include "source_inventory.h"
#include "source_orders.h"
#include "source_team_policy.h"
#include "source_goal.h"
#include "source_storage.h"
#include "qa/network_q3.h"
#include <stdio.h>

enum { SOURCE_DEFAULT_TRAVEL = 0x011c0fbe };

static bool alive(qa_bots *b, bot_ai_state *s) {
    return !s->retired && bot_ai_live(b, s->view.actor);
}
static bool team_status(qa_bots *b, bot_ai_state *s, qa_error *e) {
    return !alive(b,s) || bot_ai_team_status(b,s,e);
}
static bool same(const char *a, const char *b) {
    for (;;) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z') x=(unsigned char)(x-'A'+'a');
        if(y>='A' && y<='Z') y=(unsigned char)(y-'A'+'a');
        if(x!=y) return false;
        if(!x) return true;
    }
}
static void leader_name(const bot_ai_state *s, char name[33]) {
    memcpy(name,s->team_leader_name,32);name[32]=0;
}
static int32_t maximum(qa_bots *b, int32_t *cache) {
    if(!*cache) {
        qa_cvars *vars=b->services.configuration?b->services.configuration(b->services.context):NULL;
        const qa_cvar_view *view=vars?qa_cvars_find(vars,"sv_maxclients"):NULL;
        *cache=view?view->integer:0;
    }
    return *cache<64?*cache:64;
}
static int32_t integer(const char *text) {
    while(*text && (int8_t)(unsigned char)*text<=32) ++text;
    bool negative=*text=='-';if(*text=='-' || *text=='+') ++text;
    uint32_t value=0;
    while(*text>='0' && *text<='9') value=value*10+(uint32_t)(*text++-'0');
    if(negative) value=0-value;
    int32_t result;memcpy(&result,&value,sizeof(result));return result;
}
static bool team_mates(qa_bots *b, bot_ai_state *s, int32_t maximum_clients,
                       int32_t teammates[64], int32_t *count, qa_error *e) {
    *count=0;
    if(!b->services.configstring) return bot_ai_fail(e,"Source team policy requires actual GAME configstrings");
    for(int32_t i=0;i<maximum_clients && i<64 && alive(b,s);++i) {
        char information[1024],name[1024],team[1024];bool same_team;
        if(!b->services.configstring(b->services.context,544+(uint32_t)i,information,sizeof(information),e)) return false;
        if(!alive(b,s)) return true;
        if(!*information) continue;
        if(!qa_q3_info_value(information,"n",name,sizeof(name),e)) return false;
        if(!*name) continue;
        if(!qa_q3_info_value(information,"t",team,sizeof(team),e)) return false;
        if(integer(team)==3) continue;
        if(!bot_ai_source_same_team(b,s,i,&same_team,e)) return false;
        if(!alive(b,s)) return true;
        if(same_team) teammates[(*count)++]=i;
    }
    return true;
}
static qa_bot_navigation *navigation(qa_bots *b, bot_ai_state *s) {
    return qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
}
static bool travel(qa_bots *b, bot_ai_state *s, qa_vec3 origin, uint32_t area,
                    const qa_bot_goal *goal, uint32_t flags, uint32_t *time, qa_error *e) {
    qa_bot_navigation *nav=navigation(b,s);
    if(!nav) return bot_ai_fail(e,"Source team policy requires its retained AAS query owner");
    qa_bot_nav_route_query query={.area=area,.origin=origin,.has_origin=true,
        .goal_area=(uint32_t)goal->area,.travel_flags=flags};
    qa_bot_nav_route route;
    if(!qa_bot_navigation_route(nav,&query,&route,e)) return false;
    *time=route.travel_time;return true;
}
static bool sorted_team_mates(qa_bots *b, bot_ai_state *s, int32_t teammates[64],
                               int32_t *count, qa_error *e) {
    int32_t self,team;
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_source_team(b,self,&team,e)) return false;
    if(!alive(b,s)) return true;
    int32_t type=b->source_goals.game_type;
    if(!s->team_arena && type!=4 && type!=5)
        return bot_ai_fail(e,"Source base-game team ordering has no base goal in this mode");
    const qa_bot_goal *goal=type==4 || type==5?
        (team==1?&b->source_goals.red_flag:&b->source_goals.blue_flag):
        (team==1?&b->source_goals.red_obelisk:&b->source_goals.blue_obelisk);
    int32_t candidates[64],candidate_count;
    if(!team_mates(b,s,maximum(b,&b->source_team_policy.sort_team_mates_maxclients),
        candidates,&candidate_count,e)) return false;
    if(!alive(b,s)) return true;
    uint32_t times[64];*count=0;
    for(int32_t i=0;i<candidate_count && alive(b,s);++i) {
        qa_bot_source_player player;
        if(!b->services.source_player) return bot_ai_fail(e,"Source teammate routing requires actual client player states");
        if(!b->services.source_player(b->services.context,candidates[i],&player,e)) return false;
        if(!alive(b,s)) return true;
        if(!player.present) return bot_ai_fail(e,"Source teammate routing reads an uninitialized client player state");
        uint32_t area,time;
        if(!bot_ai_point_area(b,s,player.origin,&area,e)) return false;
        if(!alive(b,s)) return true;
        if(!area) time=1;
        else if(!travel(b,s,player.origin,area,goal,SOURCE_DEFAULT_TRAVEL,&time,e)) return false;
        if(!alive(b,s)) return true;
        int32_t at=0;
        while(at<*count && time>=times[at]) ++at;
        for(int32_t j=*count;j>at;--j) {times[j]=times[j-1];teammates[j]=teammates[j-1];}
        times[at]=time;teammates[at]=candidates[i];++*count;
    }
    int32_t defenders[64],roamers[64],attackers[64],defend_count=0,roam_count=0,attack_count=0;
    for(int32_t i=0;i<*count && alive(b,s);++i) {
        int32_t client=teammates[i],preference=b->team_preferences[client].preference;
        if(preference) {
            char name[36];if(!bot_ai_client_name(b,client,name,sizeof(name),true,e)) return false;
            if(!alive(b,s)) return true;
            if(!same(name,b->team_preferences[client].name)) preference=0;
        }
        if(preference&1) defenders[defend_count++]=client;
        else if(preference&2) attackers[attack_count++]=client;
        else roamers[roam_count++]=client;
    }
    int32_t at=0;
    for(int32_t i=0;i<defend_count;++i) teammates[at++]=defenders[i];
    for(int32_t i=0;i<roam_count;++i) teammates[at++]=roamers[i];
    for(int32_t i=0;i<attack_count;++i) teammates[at++]=attackers[i];
    return true;
}
static bool chat_initial(qa_bots *b, bot_ai_state *s, const char *type,
                         const char *first, const char *second, qa_error *e) {
    const char *arguments[8]={first,second,NULL,NULL,NULL,NULL,NULL,NULL};
    return !alive(b,s) || bot_ai_source_initial_chat(b,s,type,arguments,e);
}
static bool say_order(qa_bots *b, bot_ai_state *s, int32_t client, bool always, qa_error *e) {
    if(!alive(b,s)) return true;
    qa_bot_chat *chat=qa_bot_runtime_chat(b->runtime,s->chat);
    if(s->team_arena && !always) {
        char text[256];return qa_bot_chat_take_message(chat,text,sizeof(text),e);
    }
    int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(client!=self) return qa_bot_chat_enter(chat,client,QA_BOT_CHAT_TELL,e);
    char text[256],name[36],message[512];
    if(!qa_bot_chat_take_message(chat,text,sizeof(text),e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_client_name(b,self,name,sizeof(name),true,e)) return false;
    if(!alive(b,s)) return true;
    int length=snprintf(message,sizeof(message),"\x19(%s\x19)\x19: %s",name,text);
    if(length>=256) {
        char diagnostic[80];snprintf(diagnostic,sizeof(diagnostic),"Com_sprintf: overflow of %d in 256\n",length);
        if(!bot_ai_source_print(b,diagnostic,e)) return false;
        if(!alive(b,s)) return true;
    }
    message[255]=0;uint32_t handle;
    return qa_bot_chat_console_queue(chat,1,message,b->time,&handle,e);
}
static bool issue(qa_bots *b, bot_ai_state *s, int32_t teammate, const char *chat,
                   const char *voice, int32_t voice_client, bool voice_first, qa_error *e) {
    if(!alive(b,s)) return true;
    char name[36];if(!bot_ai_client_name(b,teammate,name,sizeof(name),true,e)) return false;
    if(!alive(b,s)) return true;
    if(!chat_initial(b,s,chat,name,NULL,e)) return false;
    if(voice_first && !bot_ai_source_voice(b,s,voice_client,voice,false,e)) return false;
    if(!say_order(b,s,teammate,false,e)) return false;
    return voice_first || !alive(b,s) || bot_ai_source_voice(b,s,voice_client,voice,false,e);
}
static bool follow_carrier(qa_bots *b, bot_ai_state *s, int32_t teammate,
                            const char *carrier, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t self;char name[36];
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_client_name(b,teammate,name,sizeof(name),true,e)) return false;
    if(!alive(b,s)) return true;
    bool own=s->source_order.flag_carrier==self;
    if(!chat_initial(b,s,own?"cmd_accompanyme":"cmd_accompany",name,own?NULL:carrier,e) ||
       !bot_ai_source_voice(b,s,teammate,own?"followme":"followflagcarrier",false,e)) return false;
    return say_order(b,s,teammate,false,e);
}
static bool at(const int32_t teammates[64], int32_t count, int32_t index,
                int32_t *client, qa_error *e) {
    if(index<0 || index>=count) return bot_ai_fail(e,"Source team policy indexes outside its populated teammates");
    *client=teammates[index];return true;
}
static int32_t role_count(int32_t count, double fraction, int32_t maximum_count) {
    int32_t result=(int32_t)((double)count*fraction+.5);
    return result<maximum_count?result:maximum_count;
}
static bool issue_at(qa_bots *b, bot_ai_state *s, const int32_t teammates[64], int32_t count,
                      int32_t index, const char *chat, const char *voice,
                      int32_t voice_index, bool voice_first, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t client,voice_client;
    if(!at(teammates,count,index,&client,e) || !at(teammates,count,voice_index,&voice_client,e)) return false;
    return issue(b,s,client,chat,voice,voice_client,voice_first,e);
}
static bool carrier_name(qa_bots *b, bot_ai_state *s, bool always, char name[36], qa_error *e) {
    name[0]=0;
    return !alive(b,s) || (!always && s->source_order.flag_carrier==-1) ||
        bot_ai_client_name(b,s->source_order.flag_carrier,name,36,true,e);
}
static bool follow_or_get(qa_bots *b, bot_ai_state *s, int32_t teammate,
                           const char *carrier, bool follow, bool voice_first, qa_error *e) {
    return follow?follow_carrier(b,s,teammate,carrier,e):
        issue(b,s,teammate,"cmd_getflag","getflag",teammate,voice_first,e);
}
static bool orders_ctf(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t self,team,teammates[64],count=0;
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_source_team(b,self,&team,e)) return false;
    if(!alive(b,s)) return true;
    int64_t status=team==1?(int64_t)s->source_order.red_flag_status*2+s->source_order.blue_flag_status:
        (int64_t)s->source_order.blue_flag_status*2+s->source_order.red_flag_status;
    if(status<0 || status>3) return true;
    if(!sorted_team_mates(b,s,teammates,&count,e)) return false;
    if(!alive(b,s)) return true;
    bool aggressive=(s->source_team_policy.ctf_strategy&1)!=0;
    int32_t carrier=s->source_order.flag_carrier,client,first,last,defenders,attackers;
    char name[36];
    if(status==3) {
        if(s->source_team_policy.num_teammates==1) return true;
        if(s->source_team_policy.num_teammates==2) {
            if(!at(teammates,count,0,&first,e)) return false;
            return issue_at(b,s,teammates,count,first!=carrier?0:1,"cmd_getflag","getflag",first!=carrier?0:1,false,e);
        }
        if(s->source_team_policy.num_teammates==3) {
            if(!at(teammates,count,0,&first,e) || !at(teammates,count,first!=carrier?0:1,&client,e)) return false;
            if(!carrier_name(b,s,false,name,e) || !follow_or_get(b,s,client,name,carrier!=-1,true,e)) return false;
            if(!alive(b,s)) return true;
            if(!at(teammates,count,2,&last,e)) return false;
            return issue_at(b,s,teammates,count,last!=carrier?2:1,"cmd_getflag","returnflag",last!=carrier?2:1,false,e);
        }
        defenders=role_count(count,.4,4);attackers=role_count(count,.5,5);
        if(!carrier_name(b,s,false,name,e)) return false;
        for(int32_t i=0;i<defenders && alive(b,s);++i) {
            client=teammates[i];if(client==carrier) continue;
            if(!follow_or_get(b,s,client,name,carrier!=-1,true,e)) return false;
        }
        for(int32_t i=0;i<attackers && alive(b,s);++i) {
            client=teammates[count-i-1];if(client==carrier) continue;
            if(!issue(b,s,client,"cmd_getflag","returnflag",client,false,e)) return false;
        }
        return true;
    }
    if(status==2) {
        if(s->source_team_policy.num_teammates==1) return true;
        if(s->source_team_policy.num_teammates==2)
            return issue_at(b,s,teammates,count,0,aggressive?"cmd_getflag":"cmd_defendbase","getflag",0,false,e) &&
                issue_at(b,s,teammates,count,1,"cmd_getflag","getflag",1,false,e);
        if(s->source_team_policy.num_teammates==3)
            return issue_at(b,s,teammates,count,0,"cmd_defendbase",aggressive?"getflag":"defend",0,false,e) &&
                issue_at(b,s,teammates,count,1,"cmd_getflag","getflag",1,false,e) &&
                issue_at(b,s,teammates,count,2,"cmd_getflag","getflag",2,false,e);
        defenders=role_count(count,aggressive?.2:.3,aggressive?2:3);
        attackers=role_count(count,.7,aggressive?7:6);
        for(int32_t i=0;i<defenders && alive(b,s);++i)
            if(!issue_at(b,s,teammates,count,i,"cmd_defendbase","defend",i,false,e)) return false;
        for(int32_t i=0;i<attackers && alive(b,s);++i)
            if(!issue_at(b,s,teammates,count,count-i-1,"cmd_getflag","getflag",aggressive?count-i-1:0,false,e)) return false;
        return true;
    }
    if(status==1) {
        if(count==1) return true;
        if(count==2 || count==3) {
            if(!at(teammates,count,0,&first,e)) return false;
            int32_t index=first==carrier?1:0;
            if(!issue_at(b,s,teammates,count,index,"cmd_defendbase","defend",index,false,e)) return false;
            if(count!=3 || !alive(b,s)) return true;
            index=teammates[2]!=carrier?2:1;
            return issue_at(b,s,teammates,count,index,"cmd_defendbase","defend",index,false,e);
        }
        defenders=role_count(count,.6,6);attackers=role_count(count,.3,3);
        for(int32_t i=0;i<defenders && alive(b,s);++i) {
            client=teammates[i];if(client==carrier) continue;
            if(!issue(b,s,client,"cmd_defendbase","defend",client,false,e)) return false;
        }
        if(!carrier_name(b,s,false,name,e)) return false;
        for(int32_t i=0;i<attackers && alive(b,s);++i) {
            client=teammates[count-i-1];if(client==carrier) continue;
            if(!follow_or_get(b,s,client,name,carrier!=-1,true,e)) return false;
        }
        return true;
    }
    if(count==1) return true;
    if(count==2 || count==3) {
        if(!issue_at(b,s,teammates,count,0,"cmd_defendbase","defend",0,false,e)) return false;
        if(count==3 && !issue_at(b,s,teammates,count,1,aggressive?"cmd_getflag":"cmd_defendbase",
            aggressive?"getflag":"defend",1,false,e)) return false;
        return issue_at(b,s,teammates,count,count-1,"cmd_getflag","getflag",count-1,false,e);
    }
    defenders=role_count(count,aggressive?.4:.5,aggressive?4:5);
    attackers=role_count(count,aggressive?.5:.4,aggressive?5:4);
    for(int32_t i=0;i<defenders && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,i,"cmd_defendbase","defend",i,false,e)) return false;
    for(int32_t i=0;i<attackers && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,count-i-1,"cmd_getflag","getflag",count-i-1,false,e)) return false;
    return true;
}
static bool orders_one_flag(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t status=s->source_order.neutral_flag_status;
    if(status<0 || status>3) return true;
    int32_t teammates[64],count=0;
    if(!sorted_team_mates(b,s,teammates,&count,e)) return false;
    if(!alive(b,s) || count==1) return true;
    bool aggressive=(s->source_team_policy.ctf_strategy&1)!=0;
    int32_t carrier=s->source_order.flag_carrier,client,first,defenders,attackers;
    if(status==1) {
        if(count==2) {
            int32_t index=teammates[0]==carrier?1:0;
            return issue_at(b,s,teammates,count,index,aggressive?"cmd_defendbase":"cmd_attackenemybase",
                aggressive?"defend":"offense",index,false,e);
        }
        if(count==3) {
            int32_t index=teammates[0]!=carrier?0:1;
            if(!issue_at(b,s,teammates,count,index,"cmd_defendbase","defend",index,false,e)) return false;
            if(!alive(b,s)) return true;
            client=teammates[teammates[2]!=carrier?2:1];char name[36];
            if(aggressive || carrier!=-1) {
                if(!carrier_name(b,s,true,name,e)) return false;
                return follow_carrier(b,s,client,name,e);
            }
            return issue(b,s,client,"cmd_getflag","getflag",client,true,e);
        }
        defenders=role_count(count,aggressive?.2:.3,aggressive?2:3);
        attackers=role_count(count,aggressive?.8:.7,aggressive?8:7);
        for(int32_t i=0;i<defenders && alive(b,s);++i) {
            client=teammates[i];if(client==carrier) continue;
            if(!issue(b,s,client,"cmd_defendbase","defend",client,false,e)) return false;
        }
        char name[36];if(!carrier_name(b,s,aggressive,name,e)) return false;
        for(int32_t i=0;i<attackers && alive(b,s);++i) {
            client=teammates[count-i-1];if(client==carrier) continue;
            if(!follow_or_get(b,s,client,name,aggressive || carrier!=-1,false,e)) return false;
        }
        return true;
    }
    if(status==2) {
        if(count==2 || count==3) {
            if(!issue_at(b,s,teammates,count,0,"cmd_defendbase","defend",0,false,e) ||
               !issue_at(b,s,teammates,count,1,"cmd_defendbase","defend",1,false,e)) return false;
            return count!=3 || issue_at(b,s,teammates,count,2,aggressive?"cmd_returnflag":"cmd_defendbase",
                aggressive?"getflag":"defend",2,false,e);
        }
        defenders=role_count(count,aggressive?.7:.8,8);attackers=role_count(count,aggressive?.2:.1,2);
        for(int32_t i=0;i<defenders && alive(b,s);++i)
            if(!issue_at(b,s,teammates,count,i,"cmd_defendbase","defend",i,false,e)) return false;
        for(int32_t i=0;i<attackers && alive(b,s);++i)
            if(!issue_at(b,s,teammates,count,count-i-1,"cmd_returnflag","getflag",count-i-1,false,e)) return false;
        return true;
    }
    if(count==2 || count==3) {
        if(!issue_at(b,s,teammates,count,0,"cmd_defendbase","defend",0,false,e)) return false;
        if(count==3) {
            first=status==0 && !aggressive?0:1;
            if(!issue_at(b,s,teammates,count,1,aggressive?"cmd_getflag":"cmd_defendbase",
                aggressive?"getflag":"defend",first,false,e)) return false;
        }
        return issue_at(b,s,teammates,count,count-1,"cmd_getflag","getflag",count-1,false,e);
    }
    defenders=role_count(count,aggressive?.3:.5,aggressive?3:5);
    attackers=role_count(count,aggressive?.6:.4,aggressive?6:4);
    for(int32_t i=0;i<defenders && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,i,"cmd_defendbase","defend",i,false,e)) return false;
    for(int32_t i=0;i<attackers && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,count-i-1,"cmd_getflag",status==3 && aggressive?"defend":"getflag",count-i-1,false,e)) return false;
    return true;
}
static bool orders_bases(qa_bots *b, bot_ai_state *s, const char *attack_command, qa_error *e) {
    int32_t teammates[64],count=0;
    if(!sorted_team_mates(b,s,teammates,&count,e)) return false;
    if(!alive(b,s) || count==1) return true;
    bool aggressive=(s->source_team_policy.ctf_strategy&1)!=0;
    if(count==2 || count==3) {
        if(!issue_at(b,s,teammates,count,0,"cmd_defendbase","defend",0,false,e)) return false;
        if(count==3 && !issue_at(b,s,teammates,count,1,aggressive?attack_command:"cmd_defendbase",
            aggressive?"offense":"defend",1,false,e)) return false;
        return issue_at(b,s,teammates,count,count-1,attack_command,"offense",count-1,false,e);
    }
    int32_t defenders=role_count(count,aggressive?.3:.5,aggressive?3:5);
    int32_t attackers=role_count(count,aggressive?.7:.4,aggressive?7:4);
    for(int32_t i=0;i<defenders && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,i,"cmd_defendbase","defend",i,false,e)) return false;
    for(int32_t i=0;i<attackers && alive(b,s);++i)
        if(!issue_at(b,s,teammates,count,count-i-1,attack_command,"offense",count-i-1,false,e)) return false;
    return true;
}
static bool create_group(qa_bots *b, bot_ai_state *s, const int32_t teammates[64],
                          int32_t count, int32_t start, int32_t group_size, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t leader,self;char leader_text[36];
    if(!at(teammates,count,start,&leader,e) || !bot_ai_client_name(b,leader,leader_text,sizeof(leader_text),true,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    for(int32_t i=1;i<group_size && alive(b,s);++i) {
        int32_t teammate;char name[36];
        if(!at(teammates,count,start+i,&teammate,e) || !bot_ai_client_name(b,teammate,name,sizeof(name),true,e)) return false;
        if(!chat_initial(b,s,leader==self?"cmd_accompanyme":"cmd_accompany",name,leader==self?NULL:leader_text,e) ||
           !say_order(b,s,teammate,true,e)) return false;
    }
    return true;
}
static bool orders_team(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t teammates[64],count=0;
    if(!team_mates(b,s,maximum(b,&b->source_team_policy.team_orders_maxclients),teammates,&count,e)) return false;
    if(!alive(b,s)) return true;
    if(count==3) return create_group(b,s,teammates,count,0,2,e);
    if(count==4 || count==5)
        return create_group(b,s,teammates,count,0,2,e) && create_group(b,s,teammates,count,2,count-2,e);
    if(count<=10 && count!=1 && count!=2)
        for(int32_t i=0;i<count/2 && alive(b,s);++i)
            if(!create_group(b,s,teammates,count,i*2,2,e)) return false;
    return true;
}
static bool known_bot_leader(qa_bots *b, bot_ai_state *s, bool *out, qa_error *e) {
    char name[33];leader_name(s,name);int32_t client;
    *out=false;if(!alive(b,s)) return true;
    if(!bot_ai_source_client_from_name(b,name,&client,e)) return false;
    if(!alive(b,s) || client<0 || client>=64) return true;
    uint32_t slot=b->source_clients[client];
    if(slot && slot<=b->client_capacity) {
        bot_ai_state *leader=b->clients[slot-1];
        *out=leader && !leader->retired;
    }
    return true;
}
static bool picked(const bot_ai_state *s, const int32_t *before, uint32_t index) {
    return before[index]==0 && bot_ai_inventory_value(s,(int32_t)index)>=1;
}
bool bot_ai_source_task_preference(qa_bots *b, bot_ai_state *s,
                                   const int32_t *old_inventory, qa_error *e) {
    if(!alive(b,s) || !s->team_arena || b->source_goals.game_type<=3) return true;
    int offense=-1;
    if(picked(s,old_inventory,QA_BOT_INV_KAMIKAZE) || picked(s,old_inventory,QA_BOT_INV_INVULNERABILITY)) offense=1;
    if(!bot_ai_inventory_value(s,QA_BOT_INV_KAMIKAZE) && !bot_ai_inventory_value(s,QA_BOT_INV_INVULNERABILITY)) {
        if(picked(s,old_inventory,QA_BOT_INV_SCOUT)) offense=1;
        if(picked(s,old_inventory,QA_BOT_INV_GUARD)) offense=1;
        if(picked(s,old_inventory,QA_BOT_INV_DOUBLER)) offense=0;
        if(picked(s,old_inventory,QA_BOT_INV_AMMO_REGEN)) offense=0;
    }
    if(offense<0) return true;
    int32_t leader;char name[33];leader_name(s,name);
    if(!bot_ai_source_client_from_name(b,name,&leader,e)) return false;
    if(!alive(b,s)) return true;
    int32_t type=b->source_goals.game_type;
    bool flags_at_base=(type!=4 || (!s->source_order.red_flag_status && !s->source_order.blue_flag_status)) &&
        (type!=5 || !s->source_order.neutral_flag_status);
    int32_t *preference=&s->source_team_policy.team_task_preference;
    if(!(*preference&(offense?2:1))) {
        bool bot_leader;if(!known_bot_leader(b,s,&bot_leader,e)) return false;
        if(!alive(b,s)) return true;
        if(bot_leader) {
            if(!bot_ai_source_voice(b,s,offense?leader:-1,offense?"wantonoffense":"wantondefense",false,e)) return false;
        } else {
            const bot_source_match_cvar *skill=&b->source_match.cvars[BOT_SOURCE_SP_SKILL];
            if(!skill->registered) return bot_ai_fail(e,"Source bot pickup preference requires its cached g_spSkill cvar");
            if(skill->integer_value<=3) {
                bool notify=offense?(bot_ai_long_term_goal(s)!=BOT_LTG_GET_FLAG && bot_ai_long_term_goal(s)!=BOT_LTG_ATTACK_BASE &&
                    bot_ai_long_term_goal(s)!=BOT_LTG_HARVEST && flags_at_base):
                    (bot_ai_long_term_goal(s)!=BOT_LTG_DEFEND && flags_at_base);
                if(notify && !bot_ai_source_voice(b,s,offense?leader:-1,offense?"wantonoffense":"wantondefense",false,e)) return false;
                if(offense && alive(b,s)) *preference|=2;
            }
        }
        if(!offense && alive(b,s)) *preference|=1;
    }
    if(alive(b,s)) *preference&=~(offense?1:2);
    return true;
}
bool bot_ai_source_print_team_goal(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t self;char name[36];
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_client_name(b,self,name,sizeof(name),true,e)) return false;
    if(!alive(b,s)) return true;
    float time=bot_ai_team_goal_time(s)-b->time;const char *action=NULL;
    switch(bot_ai_long_term_goal(s)) {
    case BOT_LTG_TEAM_HELP:action="help a team mate";break;
    case BOT_LTG_TEAM_ACCOMPANY:action="accompany a team mate";break;
    case BOT_LTG_GET_FLAG:action="get the flag";break;
    case BOT_LTG_RUSH_BASE:action="rush to the base";break;
    case BOT_LTG_RETURN_FLAG:action="try to return the flag";break;
    case BOT_LTG_ATTACK_BASE:if(s->team_arena) action="attack the enemy base";break;
    case BOT_LTG_HARVEST:if(s->team_arena) action="harvest";break;
    case BOT_LTG_DEFEND:action="defend a key area";break;
    case BOT_LTG_GET_ITEM:action="get an item";break;
    case BOT_LTG_KILL:action="kill someone";break;
    case BOT_LTG_CAMP:case BOT_LTG_CAMP_ORDER:action="camp";break;
    case BOT_LTG_PATROL:action="patrol";break;
    default:break;
    }
    char text[256];
    if(!action) {
        if(s->source_team_policy.ctf_roam_time>b->time) {
            time=s->source_team_policy.ctf_roam_time-b->time;action="roam";
        } else {
            snprintf(text,sizeof(text),"%s: I've got a regular goal\n",name);
            return bot_ai_source_print(b,text,e);
        }
    }
    if(!isfinite(time) || fabs((double)time)>2147483647.0)
        return bot_ai_fail(e,"Source bot goal diagnostic has an undefined float integer conversion");
    char seconds[16];snprintf(seconds,sizeof(seconds),"%s%d",time<0?"-":"",(int32_t)fabsf(time));
    snprintf(text,sizeof(text),"%s: I'm gonna %s for %s secs\n",name,action,seconds);
    return bot_ai_source_print(b,text,e);
}
static bool source_team(qa_bots *b, bot_ai_state *s, int32_t *team, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t client;
    if(!bot_ai_source_client(b,s,&client,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_source_team(b,client,team,e)) return false;
    if(*team!=1 && *team!=2) *team=0;
    return true;
}
static int32_t opposite(int32_t team) { return team==1?2:team==2?1:0; }
static bool refuse_order(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!bot_ai_ordered(s) || !bot_ai_order_time(s) || !(bot_ai_order_time(s)>b->time-10.0f) || !alive(b,s)) return true;
    if(!qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_NEGATIVE,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_source_voice(b,s,bot_ai_decisionmaker(s),"no",false,e)) return false;
    if(alive(b,s)) bot_ai_order_time_set(s,0);
    return true;
}
bool bot_ai_source_set_last_order(qa_bots *b, bot_ai_state *s, bool *out, qa_error *e) {
    *out=false;if(!alive(b,s)) return true;
    int32_t team,last_type;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,false,e)) return false;
    if(b->source_goals.game_type==4 && last_type==BOT_LTG_RETURN_FLAG) {
        if(!source_team(b,s,&team,e)) return false;
        if(!alive(b,s)) return true;
        if((team==1?s->source_order.red_flag_status:s->source_order.blue_flag_status)==0) {
            last_type=BOT_LTG_NONE;
            if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,true,e)) return false;
        }
    }
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,false,e)) return false;
    if(!last_type) return true;
    int32_t value;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER,&value,false,e)) return false;
    bot_ai_decisionmaker_set(s,value);
    bot_ai_ordered_set(s,true);
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&value,false,e)) return false;
    bot_ai_long_term_goal_set(s,value);
    bot_ai_goal_record_copy(s,QA_BOT_SOURCE_TEAM_GOAL,QA_BOT_SOURCE_LAST_TEAM_GOAL);
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE,&value,false,e)) return false;
    bot_ai_teammate_set(s,value);
    bot_ai_team_goal_time_set(s,b->time+300.0f);
    if(!team_status(b,s,e)) return false;
    if(!alive(b,s)) return true;
    if(b->source_goals.game_type==4 && bot_ai_long_term_goal(s)==BOT_LTG_GET_FLAG) {
        if(!source_team(b,s,&team,e)) return false;
        if(!alive(b,s)) return true;
        const qa_bot_goal *own=team==1?&b->source_goals.red_flag:&b->source_goals.blue_flag;
        const qa_bot_goal *enemy=team==1?&b->source_goals.blue_flag:&b->source_goals.red_flag;
        uint32_t own_time,enemy_time;
        if(!travel(b,s,s->player.origin,s->area,own,SOURCE_DEFAULT_TRAVEL,&own_time,e)) return false;
        if(!alive(b,s)) return true;
        if(!travel(b,s,s->player.origin,s->area,enemy,SOURCE_DEFAULT_TRAVEL,&enemy_time,e)) return false;
        if(!alive(b,s)) return true;
        if(enemy_time>own_time && !bot_ai_source_alternate_route(b,s,opposite(team),e)) return false;
    }
    *out=alive(b,s);return true;
}
static void copy_leader(bot_ai_state *s, const char *name, bool overflow) {
    size_t length=strlen(name),maximum_bytes=overflow?32:31;
    if(length>maximum_bytes) length=maximum_bytes;
    memcpy(s->team_leader_name,name,length);
    if(length<32) memset(s->team_leader_name+length,0,32-length);
    if(overflow) {
        uint32_t bits;memcpy(&bits,&s->source_order.ask_team_leader_time,sizeof(bits));
        bits&=UINT32_C(0xffffff00);memcpy(&s->source_order.ask_team_leader_time,&bits,sizeof(bits));
    }
}
static bool human_leader(qa_bots *b, bot_ai_state *s, bool *found, qa_error *e) {
    *found=false;
    if(!b->services.source_player) return bot_ai_fail(e,"Source human leadership requires actual GAME clients");
    for(int32_t i=0;i<64 && alive(b,s);++i) {
        qa_bot_source_player player;bool same_team;
        if(!b->services.source_player(b->services.context,i,&player,e)) return false;
        if(!alive(b,s)) return true;
        if(!player.present || player.bot || b->not_leader[i]) continue;
        if(!bot_ai_source_same_team(b,s,i,&same_team,e)) return false;
        if(!alive(b,s)) return true;
        if(!same_team) continue;
        char information[1024],name[1024];
        if(!b->services.configstring || !b->services.configstring(b->services.context,544+(uint32_t)i,information,sizeof(information),e)) return false;
        if(!alive(b,s)) return true;
        if(!qa_q3_info_value(information,"n",name,sizeof(name),e)) return false;
        copy_leader(s,name,false);
        size_t written=0;
        for(size_t j=0;j<32 && s->team_leader_name[j];++j) {
            unsigned char c=(unsigned char)s->team_leader_name[j];
            if(c=='^' && j+1<32 && s->team_leader_name[j+1] && s->team_leader_name[j+1]!='^') {++j;continue;}
            if(c>=32 && c<=126) s->team_leader_name[written++]=(char)c;
        }
        s->team_leader_name[written]=0;
        bool retained;if(!bot_ai_source_set_last_order(b,s,&retained,e)) return false;
        if(!alive(b,s)) return true;
        if(!retained) {
            int32_t team;if(!source_team(b,s,&team,e)) return false;
            if(!alive(b,s)) return true;
            int32_t type=b->source_goals.game_type;
            if(team!=1 && team!=2) {*found=true;return true;}
            if(s->team_arena && (type==6 || type==7))
                bot_ai_team_goal_set(s,team==1?b->source_goals.red_obelisk:b->source_goals.blue_obelisk);
            else if(type==4 || (s->team_arena && type==5))
                bot_ai_team_goal_set(s,team==1?b->source_goals.red_flag:b->source_goals.blue_flag);
            else {*found=true;return true;}
            bot_ai_decisionmaker_set(s,i);bot_ai_ordered_set(s,true);bot_ai_order_time_set(s,b->time);
            float random;if(!bot_ai_random(b,&random,e)) return false;
            if(!alive(b,s)) return true;
            volatile float delay=2.0f*random;bot_ai_team_message_time_set(s,b->time+delay);
            bot_ai_long_term_goal_set(s,BOT_LTG_DEFEND);bot_ai_team_goal_time_set(s,b->time+600.0f);bot_ai_defend_away_time_set(s,0);
            if(!team_status(b,s,e)) return false;
            if(alive(b,s) && !bot_ai_remember_order(b,s,e)) return false;
            if(alive(b,s) && qa_bot_runtime_debug(b->runtime) && !bot_ai_source_print_team_goal(b,s,e)) return false;
        }
        *found=alive(b,s);return true;
    }
    return true;
}
static bool leader_chat(qa_bots *b, bot_ai_state *s, const char *type, qa_error *e) {
    if(!chat_initial(b,s,type,NULL,NULL,e)) return false;
    return !alive(b,s) || qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),0,QA_BOT_CHAT_TEAM,e);
}
static bool random_deadline(qa_bots *b, bot_ai_state *s, float wait, float *out, qa_error *e) {
    if(!alive(b,s)) return true;
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(alive(b,s)) {volatile float delay=random*10.0f;volatile float start=b->time+wait;*out=start+delay;}
    return true;
}
bool bot_ai_source_team_policy(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s) || b->source_goals.game_type<3) return true;
    char leader[33];leader_name(s,leader);int32_t client=-1;
    if(*leader && !bot_ai_source_client_from_name(b,leader,&client,e)) return false;
    if(!alive(b,s)) return true;
    if(!*leader || client==-1) {
        bool found;if(!human_leader(b,s,&found,e)) return false;
        if(!alive(b,s)) return true;
        if(!found) {
            if(!s->source_order.ask_team_leader_time && !s->source_team_policy.become_team_leader_time) {
                float *time=s->view.enter_time+10.0f>b->time?&s->source_order.ask_team_leader_time:
                    &s->source_team_policy.become_team_leader_time;
                if(!random_deadline(b,s,5,time,e)) return false;
            }
            if(!alive(b,s)) return true;
            if(s->source_order.ask_team_leader_time && s->source_order.ask_team_leader_time<b->time) {
                if(!leader_chat(b,s,"whoisteamleader",e)) return false;
                if(!alive(b,s)) return true;
                s->source_order.ask_team_leader_time=0;
                if(!random_deadline(b,s,8,&s->source_team_policy.become_team_leader_time,e)) return false;
            }
            if(!alive(b,s)) return true;
            if(s->source_team_policy.become_team_leader_time && s->source_team_policy.become_team_leader_time<b->time) {
                if(!leader_chat(b,s,"iamteamleader",e) || !bot_ai_source_voice(b,s,-1,"startleader",false,e)) return false;
                if(!alive(b,s)) return true;
                int32_t self;char name[36];
                if(!bot_ai_source_client(b,s,&self,e)) return false;
                if(!alive(b,s)) return true;
                if(!bot_ai_client_name(b,self,name,sizeof(name),true,e)) return false;
                if(!alive(b,s)) return true;
                copy_leader(s,name,true);s->source_team_policy.become_team_leader_time=0;
            }
            return true;
        }
    }
    s->source_order.ask_team_leader_time=0;s->source_team_policy.become_team_leader_time=0;
    int32_t self;char name[36];
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    if(!bot_ai_client_name(b,self,name,sizeof(name),true,e)) return false;
    if(!alive(b,s)) return true;
    leader_name(s,leader);if(!same(name,leader)) return true;
    int32_t teammates[64],count;
    if(!team_mates(b,s,maximum(b,&b->source_team_policy.num_team_mates_maxclients),teammates,&count,e)) return false;
    if(!alive(b,s)) return true;
    int32_t type=b->source_goals.game_type;
    if(type>7 || (type>4 && !s->team_arena)) return true;
    bot_source_team_policy_state *policy=&s->source_team_policy;
    bool flags=type==4 || type==5;
    if(policy->num_teammates!=count || s->source_order.force_orders || (flags && s->source_order.flag_status_changed)) {
        policy->team_give_orders_time=b->time;policy->num_teammates=count;s->source_order.force_orders=false;
        if(flags) s->source_order.flag_status_changed=false;
    }
    if(flags && s->source_order.last_flag_capture_time<b->time-240.0f) {
        s->source_order.last_flag_capture_time=b->time;float random;
        if(!bot_ai_random(b,&random,e)) return false;
        if(!alive(b,s)) return true;
        if((double)random<.4) {policy->ctf_strategy^=1;policy->team_give_orders_time=b->time;}
    }
    float delay=type==4?3.0f:type==5?2.0f:5.0f;
    if(!policy->team_give_orders_time || !(policy->team_give_orders_time<b->time-delay)) return true;
    bool ok=type==3?orders_team(b,s,e):type==4?orders_ctf(b,s,e):type==5?orders_one_flag(b,s,e):
        type==6?orders_bases(b,s,"cmd_attackenemybase",e):type==7?orders_bases(b,s,"cmd_harvest",e):true;
    if(!ok) return false;
    if(alive(b,s) && type>=3 && type<=7) policy->team_give_orders_time=flags?0:b->time+(type==3?120.0f:30.0f);
    return true;
}
static bool carries(qa_bots *b, bot_ai_state *s, const qa_bot_entity_info *info,
                      bool cubes, bool *out, qa_error *e) {
    if(!cubes) {
        int32_t flags=(1<<7)|(1<<8)|(s->team_arena?(1<<9):0);
        *out=(info->state.powerups&flags)!=0;return true;
    }
    *out=false;if(b->source_goals.game_type!=7) return true;
    int32_t count;
    if(!b->services.source_generic1 ||
       !b->services.source_generic1(b->services.context,info->number,&count,e)) return false;
    if(alive(b,s)) *out=count>0;
    return true;
}
bool bot_ai_source_flag_carrier(qa_bots *b, bot_ai_state *s, bool teammate,
                                 bool visible, bool cubes, int32_t *out, qa_error *e) {
    *out=-1;if(!alive(b,s)) return true;
    int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!alive(b,s)) return true;
    for(int32_t i=0;i<b->source_goals.max_clients && i<64 && alive(b,s);++i) {
        if(i==self) continue;
        qa_bot_entity_info info;bool found,carrying,same_team;
        if(!qa_bot_runtime_entity(b->runtime,i,&info,&found,e)) return false;
        if(!info.valid) continue;
        if(!carries(b,s,&info,cubes,&carrying,e)) return false;
        if(!alive(b,s)) return true;
        if(!carrying) continue;
        if(!bot_ai_source_same_team(b,s,i,&same_team,e)) return false;
        if(!alive(b,s)) return true;
        if(same_team!=teammate) continue;
        if(visible) {
            float visibility;if(!bot_ai_source_entity_visible(b,s,i,&visibility,e)) return false;
            if(!alive(b,s)) return true;
            if(visibility<=0) continue;
        }
        *out=i;return true;
    }
    return true;
}
static bool teammate_carrying(qa_bots *b, bot_ai_state *s, bool cubes, bool *out, qa_error *e) {
    qa_bot_entity_info info;bool found;
    if(!qa_bot_runtime_entity(b->runtime,bot_ai_teammate(s),&info,&found,e)) return false;
    return carries(b,s,&info,cubes,out,e);
}
static bool own_decision(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t client;if(!bot_ai_source_client(b,s,&client,e)) return false;
    if(alive(b,s)) {bot_ai_decisionmaker_set(s,client);bot_ai_ordered_set(s,false);}
    return true;
}
static bool decision_deadline(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    volatile float deadline=b->time+5.0f;
    if(!isfinite(deadline) || (double)deadline<-2147483648.0 || (double)deadline>2147483647.0)
        return bot_ai_fail(e,"Source bot own-decision deadline has an undefined integer conversion");
    if(alive(b,s)) s->source_team_policy.own_decision_time=(int32_t)deadline;
    return true;
}
static bool accompany(qa_bots *b, bot_ai_state *s, int32_t teammate, qa_error *e) {
    if(!own_decision(b,s,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_teammate_set(s,teammate);bot_ai_teammate_visible_time_set(s,b->time);bot_ai_team_message_time_set(s,0);bot_ai_arrive_time_set(s,1);
    if(!bot_ai_source_voice(b,s,teammate,"onfollow",false,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_team_goal_time_set(s,b->time+600.0f);bot_ai_long_term_goal_set(s,BOT_LTG_TEAM_ACCOMPANY);bot_ai_formation_distance_set(s,112);
    return team_status(b,s,e);
}
static bool rush(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!refuse_order(b,s,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,BOT_LTG_RUSH_BASE);bot_ai_team_goal_time_set(s,b->time+120.0f);bot_ai_rush_base_away_time_set(s,0);
    return own_decision(b,s,e);
}
static bool protect_order(int32_t type) {
    return type==BOT_LTG_TEAM_HELP || type==BOT_LTG_TEAM_ACCOMPANY || type==BOT_LTG_CAMP_ORDER ||
        type==BOT_LTG_PATROL || type==BOT_LTG_GET_ITEM;
}
static bool ctf_goal(int32_t type, bool one_flag) {
    return protect_order(type) || type==BOT_LTG_DEFEND || type==BOT_LTG_GET_FLAG || type==BOT_LTG_RUSH_BASE ||
        type==BOT_LTG_RETURN_FLAG || type==BOT_LTG_MAKELOVE_UNDER || type==BOT_LTG_MAKELOVE_ONTOP ||
        (one_flag && type==BOT_LTG_ATTACK_BASE);
}
static bool defend(qa_bots *b, bot_ai_state *s, const qa_bot_goal *goal, qa_error *e) {
    if(!own_decision(b,s,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_team_goal_set(s,*goal);bot_ai_long_term_goal_set(s,BOT_LTG_DEFEND);bot_ai_team_goal_time_set(s,b->time+600.0f);bot_ai_defend_away_time_set(s,0);
    return team_status(b,s,e);
}
static bool roam(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,BOT_LTG_NONE);s->source_team_policy.ctf_roam_time=b->time+60.0f;
    return team_status(b,s,e);
}
static bool aggression(qa_bots *b, bot_ai_state *s, float *out, qa_error *e) {
    const qa_bot_weapon_knowledge *weapons=NULL;size_t count=0;void *lease=NULL;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,e)) return false;
    bool valid=!count || weapons;
    if(valid && alive(b,s)) *out=qa_bot_knowledge_aggression(weapons,count,s->view.weapon,bot_ai_inventory(s));
    b->services.arsenal_end(b->services.context,lease);
    return valid?true:bot_ai_fail(e,"Source aggression received a missing admitted arsenal");
}
static void thresholds(const bot_ai_state *s, float *attack, float *defense) {
    int32_t preference=s->source_team_policy.team_task_preference;
    if(preference&3) {*attack=preference&2?.7f:.2f;*defense=.9f;}
    else {*attack=.4f;*defense=.7f;}
}
static bool message_delay(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(alive(b,s)) {volatile float delay=2.0f*random;bot_ai_team_message_time_set(s,b->time+delay);}
    return true;
}
static bool common_seek(qa_bots *b, bot_ai_state *s, bool one_flag, bool harvester,
                         bool decision_gate, bool *allowed, qa_error *e) {
    *allowed=false;bool leader;
    if(!harvester) {
        if(!known_bot_leader(b,s,&leader,e)) return false;
        if(!alive(b,s) || leader) return true;
    }
    int32_t last_type;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,false,e)) return false;
    if(last_type) bot_ai_team_goal_time_set(s,bot_ai_team_goal_time(s)+60.0f);
    if(!harvester && b->source_goals.game_type!=6 && !bot_ai_ordered(s) && last_type) bot_ai_long_term_goal_set(s,BOT_LTG_NONE);
    bool protected_goal=ctf_goal(bot_ai_long_term_goal(s),one_flag);
    if(harvester) protected_goal=(protected_goal && bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE &&
        bot_ai_long_term_goal(s)!=BOT_LTG_RETURN_FLAG) || bot_ai_long_term_goal(s)==BOT_LTG_HARVEST;
    if(protected_goal) return true;
    bool restored;if(!bot_ai_source_set_last_order(b,s,&restored,e)) return false;
    if(!alive(b,s) || restored) return true;
    if((decision_gate && (double)s->source_team_policy.own_decision_time>(double)b->time) ||
       s->source_team_policy.ctf_roam_time>b->time) return true;
    float value=0;if(!aggression(b,s,&value,e)) return false;
    if(!alive(b,s) || value<50) return true;
    if(!message_delay(b,s,e)) return false;
    *allowed=alive(b,s);return true;
}
static bool seek_ctf(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t team;
    if(bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0) {
        if(bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) {
            if(!rush(b,s,e)) return false;
            if(!alive(b,s)) return true;
            if(!source_team(b,s,&team,e)) return false;
            if(!alive(b,s)) return true;
            qa_vec3 direction=team==1?qa_vec_sub(s->player.origin,b->source_goals.blue_flag.origin):
                team==2?qa_vec_sub(s->player.origin,b->source_goals.red_flag.origin):qa_v3(999,999,999);
            if(qa_vec_length(direction)<128) {
                if(!bot_ai_source_alternate_route(b,s,opposite(team),e)) return false;
            } else bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_ALT_GOAL+12,0);
            if(!alive(b,s)) return true;
            if(!b->services.userinfo || !b->services.userinfo(b->services.context,s->view.actor,"teamtask","1",e)) return false;
            return !alive(b,s) || bot_ai_source_voice(b,s,-1,"ihaveflag",false,e);
        }
        if(bot_ai_rush_base_away_time(s)>b->time) {
            if(!source_team(b,s,&team,e)) return false;
            if(!alive(b,s)) return true;
            if((team==1?s->source_order.red_flag_status:s->source_order.blue_flag_status)==0) bot_ai_rush_base_away_time_set(s,0);
        }
        return true;
    }
    if(bot_ai_long_term_goal(s)==BOT_LTG_TEAM_ACCOMPANY && !bot_ai_ordered(s)) {
        bool carrying;if(!teammate_carrying(b,s,false,&carrying,e)) return false;
        if(!alive(b,s)) return true;
        if(!carrying) bot_ai_long_term_goal_set(s,BOT_LTG_NONE);
    }
    if(!source_team(b,s,&team,e)) return false;
    if(!alive(b,s)) return true;
    int64_t status=team==1?(int64_t)s->source_order.red_flag_status*2+s->source_order.blue_flag_status:
        (int64_t)s->source_order.blue_flag_status*2+s->source_order.red_flag_status;
    if(status==1) {
        if((double)s->source_team_policy.own_decision_time<(double)b->time &&
           !(bot_ai_long_term_goal(s)==BOT_LTG_DEFEND && (bot_ai_team_goal(s).number==b->source_goals.red_flag.number ||
               bot_ai_team_goal(s).number==b->source_goals.blue_flag.number))) {
            int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,true,true,false,&carrier,e)) return false;
            if(!alive(b,s)) return true;
            if(carrier>=0 && (bot_ai_long_term_goal(s)!=BOT_LTG_TEAM_ACCOMPANY || bot_ai_teammate(s)!=carrier))
                return refuse_order(b,s,e) && accompany(b,s,carrier,e) && decision_deadline(b,s,e);
        }
        return true;
    }
    if(status==2) {
        if((double)s->source_team_policy.own_decision_time<(double)b->time) {
            int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,false,true,false,&carrier,e)) return false;
            if(!alive(b,s)) return true;
            if(bot_ai_long_term_goal(s)!=BOT_LTG_GET_FLAG && bot_ai_long_term_goal(s)!=BOT_LTG_RETURN_FLAG && !protect_order(bot_ai_long_term_goal(s))) {
                if(!refuse_order(b,s,e) || !own_decision(b,s,e)) return false;
                if(!alive(b,s)) return true;
                float random;if(!bot_ai_random(b,&random,e)) return false;
                if(!alive(b,s)) return true;
                bot_ai_long_term_goal_set(s,random<.5f?BOT_LTG_GET_FLAG:BOT_LTG_RETURN_FLAG);
                bot_ai_team_message_time_set(s,0);bot_ai_team_goal_time_set(s,b->time+600.0f);
                if(!bot_ai_source_alternate_route(b,s,opposite(team),e) || !team_status(b,s,e)) return false;
                return decision_deadline(b,s,e);
            }
        }
        return true;
    }
    if(status==3) {
        if((double)s->source_team_policy.own_decision_time<(double)b->time &&
           bot_ai_long_term_goal(s)!=BOT_LTG_RETURN_FLAG && bot_ai_long_term_goal(s)!=BOT_LTG_TEAM_ACCOMPANY) {
            int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,true,true,false,&carrier,e) || !refuse_order(b,s,e)) return false;
            if(!alive(b,s)) return true;
            if(carrier>=0) {if(!accompany(b,s,carrier,e)) return false;}
            else {
                if(!own_decision(b,s,e) || !message_delay(b,s,e)) return false;
                if(!alive(b,s)) return true;
                bot_ai_long_term_goal_set(s,BOT_LTG_RETURN_FLAG);bot_ai_team_goal_time_set(s,b->time+180.0f);
                if(!bot_ai_source_alternate_route(b,s,opposite(team),e) || !team_status(b,s,e)) return false;
            }
            return decision_deadline(b,s,e);
        }
        return true;
    }
    bool allowed;if(!common_seek(b,s,false,false,true,&allowed,e)) return false;
    if(!allowed) return true;
    float attack,defense,random;thresholds(s,&attack,&defense);
    if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    bool bases=b->source_goals.red_flag.area && b->source_goals.blue_flag.area;
    if(random<attack && bases) {
        if(!own_decision(b,s,e)) return false;
        if(!alive(b,s)) return true;
        bot_ai_long_term_goal_set(s,BOT_LTG_GET_FLAG);bot_ai_team_goal_time_set(s,b->time+600.0f);
        if(!bot_ai_source_alternate_route(b,s,opposite(team),e) || !team_status(b,s,e)) return false;
    } else if(random<defense && bases) {
        if(!defend(b,s,team==1?&b->source_goals.red_flag:&b->source_goals.blue_flag,e)) return false;
    } else if(!roam(b,s,e)) return false;
    if(!decision_deadline(b,s,e)) return false;
    return !alive(b,s) || !qa_bot_runtime_debug(b->runtime) || bot_ai_source_print_team_goal(b,s,e);
}
static bool seek_one_flag(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t team;
    if(bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0) {
        if(bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) {
            if(!rush(b,s,e) || !source_team(b,s,&team,e)) return false;
            if(!alive(b,s)) return true;
            if(!bot_ai_source_alternate_route(b,s,opposite(team),e) || !team_status(b,s,e)) return false;
            return !alive(b,s) || bot_ai_source_voice(b,s,-1,"ihaveflag",false,e);
        }
        return true;
    }
    if(bot_ai_long_term_goal(s)==BOT_LTG_TEAM_ACCOMPANY && !bot_ai_ordered(s)) {
        bool carrying;if(!teammate_carrying(b,s,false,&carrying,e)) return false;
        if(!alive(b,s)) return true;
        if(!carrying) bot_ai_long_term_goal_set(s,BOT_LTG_NONE);
    }
    if(s->source_order.neutral_flag_status==1) {
        if((double)s->source_team_policy.own_decision_time<(double)b->time) {
            if(bot_ai_long_term_goal(s)!=BOT_LTG_TEAM_ACCOMPANY) {
                int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,true,true,false,&carrier,e)) return false;
                if(!alive(b,s)) return true;
                if(carrier>=0) return refuse_order(b,s,e) && accompany(b,s,carrier,e) && decision_deadline(b,s,e);
            }
            if(ctf_goal(bot_ai_long_term_goal(s),true) && bot_ai_long_term_goal(s)!=BOT_LTG_RETURN_FLAG) return true;
            if(bot_ai_long_term_goal(s)!=BOT_LTG_ATTACK_BASE) {
                if(!refuse_order(b,s,e) || !own_decision(b,s,e)) return false;
                if(!alive(b,s)) return true;
                if(!source_team(b,s,&team,e)) return false;
                if(!alive(b,s)) return true;
                bot_ai_team_goal_set(s,team==1?b->source_goals.blue_flag:b->source_goals.red_flag);
                bot_ai_long_term_goal_set(s,BOT_LTG_ATTACK_BASE);bot_ai_team_goal_time_set(s,b->time+600.0f);
                if(!team_status(b,s,e)) return false;
                return decision_deadline(b,s,e);
            }
        }
        return true;
    }
    if(s->source_order.neutral_flag_status==2) {
        if((double)s->source_team_policy.own_decision_time<(double)b->time) {
            int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,false,true,false,&carrier,e)) return false;
            if(!alive(b,s) || protect_order(bot_ai_long_term_goal(s))) return true;
            if(bot_ai_long_term_goal(s)!=BOT_LTG_DEFEND) {
                if(!refuse_order(b,s,e) || !source_team(b,s,&team,e)) return false;
                if(!alive(b,s)) return true;
                return defend(b,s,team==1?&b->source_goals.red_flag:&b->source_goals.blue_flag,e) && decision_deadline(b,s,e);
            }
        }
        return true;
    }
    bool allowed;if(!common_seek(b,s,true,false,true,&allowed,e)) return false;
    if(!allowed) return true;
    float attack,defense,random;thresholds(s,&attack,&defense);
    if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    if(random<attack && b->source_goals.neutral_flag.area) {
        if(!own_decision(b,s,e)) return false;
        if(!alive(b,s)) return true;
        bot_ai_long_term_goal_set(s,BOT_LTG_GET_FLAG);bot_ai_team_goal_time_set(s,b->time+600.0f);
        if(!team_status(b,s,e)) return false;
    } else if(random<defense && b->source_goals.red_flag.area && b->source_goals.blue_flag.area) {
        if(!source_team(b,s,&team,e)) return false;
        if(!alive(b,s)) return true;
        if(!defend(b,s,team==1?&b->source_goals.red_flag:&b->source_goals.blue_flag,e)) return false;
    } else if(!roam(b,s,e)) return false;
    if(!decision_deadline(b,s,e)) return false;
    return !alive(b,s) || !qa_bot_runtime_debug(b->runtime) || bot_ai_source_print_team_goal(b,s,e);
}
bool bot_ai_source_go_harvest(qa_bots *b, bot_ai_state *s, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t team;if(!source_team(b,s,&team,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_team_goal_set(s,team==1?b->source_goals.blue_obelisk:b->source_goals.red_obelisk);
    bot_ai_long_term_goal_set(s,BOT_LTG_HARVEST);bot_ai_team_goal_time_set(s,b->time+120.0f);bot_ai_harvest_away_time_set(s,0);
    return team_status(b,s,e);
}
static bool seek_bases(qa_bots *b, bot_ai_state *s, bool harvester, qa_error *e) {
    int32_t team;
    if(harvester && (bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0)) {
        if(bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) {
            if(!rush(b,s,e) || !source_team(b,s,&team,e)) return false;
            if(!alive(b,s)) return true;
            return bot_ai_source_alternate_route(b,s,opposite(team),e) && team_status(b,s,e);
        }
        return true;
    }
    if(harvester) {
        bool leader;if(!known_bot_leader(b,s,&leader,e)) return false;
        if(!alive(b,s) || leader) return true;
        if(bot_ai_long_term_goal(s)==BOT_LTG_TEAM_ACCOMPANY && !bot_ai_ordered(s)) {
            bool carrying;if(!teammate_carrying(b,s,true,&carrying,e)) return false;
            if(!alive(b,s)) return true;
            if(!carrying) bot_ai_long_term_goal_set(s,BOT_LTG_NONE);
        }
    }
    bool allowed;if(!common_seek(b,s,true,harvester,false,&allowed,e)) return false;
    if(!allowed) return true;
    if(harvester) {
        int32_t carrier;if(!bot_ai_source_flag_carrier(b,s,false,true,true,&carrier,e)) return false;
        if(!alive(b,s)) return true;
        if(bot_ai_long_term_goal(s)!=BOT_LTG_TEAM_ACCOMPANY) {
            if(!bot_ai_source_flag_carrier(b,s,true,true,true,&carrier,e)) return false;
            if(!alive(b,s)) return true;
            if(carrier>=0) return accompany(b,s,carrier,e);
        }
    }
    float attack,defense,random;thresholds(s,&attack,&defense);
    if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    bool bases=b->source_goals.red_obelisk.area && b->source_goals.blue_obelisk.area;
    if(random<attack && bases) {
        if(!own_decision(b,s,e)) return false;
        if(!alive(b,s)) return true;
        if(harvester) return bot_ai_source_go_harvest(b,s,e);
        if(!source_team(b,s,&team,e)) return false;
        if(!alive(b,s)) return true;
        bot_ai_team_goal_set(s,team==1?b->source_goals.blue_obelisk:b->source_goals.red_obelisk);
        bot_ai_long_term_goal_set(s,BOT_LTG_ATTACK_BASE);bot_ai_team_goal_time_set(s,b->time+600.0f);
        return bot_ai_source_alternate_route(b,s,opposite(team),e) && team_status(b,s,e);
    }
    if(random<defense && bases) {
        if(!source_team(b,s,&team,e)) return false;
        if(!alive(b,s)) return true;
        return defend(b,s,team==1?&b->source_goals.red_obelisk:&b->source_goals.blue_obelisk,e);
    }
    return roam(b,s,e);
}
bool bot_ai_source_team_goals(qa_bots *b, bot_ai_state *s, bool retreat, qa_error *e) {
    if(!alive(b,s)) return true;
    int32_t type=b->source_goals.game_type;bool ok=true;
    if(type==4) {
        if(!retreat) ok=seek_ctf(b,s,e);
        else if((bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0) &&
                bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) ok=rush(b,s,e) && team_status(b,s,e);
    } else if(s->team_arena) {
        if(type==5) {
            if(!retreat) ok=seek_one_flag(b,s,e);
            else if(bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0 && bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) {
                int32_t team;
                ok=rush(b,s,e) && source_team(b,s,&team,e);
                if(ok && alive(b,s)) ok=bot_ai_source_alternate_route(b,s,opposite(team),e) && team_status(b,s,e);
            }
        } else if(type==6) {if(!retreat) ok=seek_bases(b,s,false,e);}
        else if(type==7) {
            if(!retreat) ok=seek_bases(b,s,true,e);
            else if((bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0) &&
                    bot_ai_long_term_goal(s)!=BOT_LTG_RUSH_BASE) ok=rush(b,s,e) && team_status(b,s,e);
        }
    }
    if(ok && alive(b,s)) bot_ai_order_time_set(s,0);
    return ok;
}

bool bot_ai_source_routes_setup(qa_bots *b, bool team_arena, qa_error *e) {
    bot_source_team_policy_globals *state=&b->source_team_policy;
    if(state->routes_setup) return true;
    if(team_arena) {
        qa_bot_goal *neutral=NULL,*red=&b->source_goals.red_flag,*blue=&b->source_goals.blue_flag;
        int32_t type=b->source_goals.game_type;
        if(type==4) {
            bool found;
            if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),-1,"Neutral Flag",&b->source_goals.neutral_flag,&found,e)) return false;
            if(!found && !bot_ai_source_print(b,"^3Warning: no alt routes without Neutral Flag\n",e)) return false;
            if(b->source_goals.neutral_flag.area) neutral=&b->source_goals.neutral_flag;
        } else if(type==5) neutral=&b->source_goals.neutral_flag;
        else if(type==6 || type==7) {
            if(type==6) {
                bool found;
                if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),-1,"Neutral Obelisk",&b->source_goals.neutral_obelisk,&found,e)) return false;
                if(!found && !bot_ai_source_print(b,"^3Warning: Harvester without neutral obelisk\n",e)) return false;
            }
            neutral=&b->source_goals.neutral_obelisk;red=&b->source_goals.red_obelisk;blue=&b->source_goals.blue_obelisk;
        }
        if(neutral) {
            qa_bot_navigation *nav=qa_bot_runtime_navigation(b->runtime,-1);
            if(!nav) return bot_ai_fail(e,"Source alternate routes require the actual map AAS owner");
            qa_bot_nav_route_query query={.area=(uint32_t)neutral->area,.origin=neutral->origin,
                .has_origin=true,.goal_area=(uint32_t)red->area,.travel_flags=SOURCE_DEFAULT_TRAVEL};
            if(!qa_bot_navigation_alternatives(nav,&query,QA_BOT_ALTERNATIVE_CLUSTER|QA_BOT_ALTERNATIVE_VIEW,
                BOT_SOURCE_ALTERNATE_ROUTES,state->red_routes,BOT_SOURCE_ALTERNATE_ROUTES,&state->red_route_count,e)) return false;
            query.goal_area=(uint32_t)blue->area;
            if(!qa_bot_navigation_alternatives(nav,&query,QA_BOT_ALTERNATIVE_CLUSTER|QA_BOT_ALTERNATIVE_VIEW,
                BOT_SOURCE_ALTERNATE_ROUTES,state->blue_routes,BOT_SOURCE_ALTERNATE_ROUTES,&state->blue_route_count,e)) return false;
        }
    }
    state->routes_setup=true;return true;
}
bool bot_ai_source_alternate_route(qa_bots *b, bot_ai_state *s, int32_t base, qa_error *e) {
    if(!alive(b,s)) return true;
    bot_source_team_policy_globals *state=&b->source_team_policy;
    size_t count=base==1?state->red_route_count:state->blue_route_count;
    if(!count) return true;
    if(count>BOT_SOURCE_ALTERNATE_ROUTES) return bot_ai_fail(e,"Source alternate route count exceeds retained storage");
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    volatile float scaled=random*(float)count;
    if(!isfinite(scaled) || scaled<0 || (double)scaled>2147483647.0)
        return bot_ai_fail(e,"Source alternate-route RNG index has an undefined integer conversion");
    size_t index=(size_t)(int32_t)scaled;if(index>=count) index=count-1;
    const qa_bot_alternative_goal *route=base==1?&state->red_routes[index]:&state->blue_routes[index];
    bot_ai_goal_record_set(s,QA_BOT_SOURCE_ALT_GOAL,(qa_bot_goal){.origin=route->origin,.area=(int32_t)route->area,
        .mins=qa_v3(-8,-8,-8),.maxs=qa_v3(8,8,8)});
    s->source_team_policy.reached_alt_route_time=0;return true;
}
bool bot_ai_source_route_goal(qa_bots *b, bot_ai_state *s, qa_bot_goal *goal, qa_error *e) {
    bot_source_team_policy_state *state=&s->source_team_policy;
    if(!alive(b,s) || !bot_ai_alternate_goal(s).area || state->reached_alt_route_time) return true;
    uint32_t time;
    qa_bot_goal alternate=bot_ai_alternate_goal(s);
    if(!travel(b,s,s->player.origin,s->area,&alternate,s->travel_flags,&time,e)) return false;
    if(!alive(b,s)) return true;
    if(time && time<20) state->reached_alt_route_time=b->time;
    *goal=bot_ai_alternate_goal(s);return true;
}
