#include "internal.h"
#include "source_inventory.h"
#include "source_storage.h"
#include "qa/network_q3.h"
#include <stdio.h>

static int32_t source_integer(const char *text) {
    while(*text && (int8_t)(unsigned char)*text<=32) ++text;
    bool negative=*text=='-';if(*text=='-' || *text=='+') ++text;
    uint32_t value=0;
    while(*text>='0' && *text<='9') value=value*10+(uint32_t)(*text++-'0');
    if(negative) value=0-value;
    int32_t result;memcpy(&result,&value,sizeof(result));return result;
}
static bool source_team(qa_bots *b,int32_t client,int32_t *out,qa_error *e) {
    if(client<0 || client>=64) {*out=0;return true;}
    char information[1024],team[1024];
    if(!b->services.configstring ||
       !b->services.configstring(b->services.context,544+(uint32_t)client,information,sizeof(information),e) ||
       !qa_q3_info_value(information,"t",team,sizeof(team),e)) return false;
    *out=source_integer(team);return true;
}
static bool source_same_name(const char *a,const char *b) {
    for(;;++a,++b) {
        unsigned char left=(unsigned char)*a,right=(unsigned char)*b;
        if(left>='a' && left<='z') left-='a'-'A';
        if(right>='a' && right<='z') right-='a'-'A';
        if(left!=right) return false;
        if(!left) return true;
    }
}
static bool source_goal_entity(qa_bots *b,qa_bot_goal *goal,const char *classname,qa_error *e) {
    uint32_t count;
    if(!b->services.source_row_count || !b->services.source_row)
        return bot_ai_fail(e,"bot objective binding requires its actual physical source rows");
    if(!b->services.source_row_count(b->services.context,&count,e)) return false;
    if(count>INT32_MAX) return bot_ai_fail(e,"bot source entity extent exceeds signed source indices");
    for(uint32_t index=0;index<count;++index) {
        qa_bot_source_row row;
        if(!b->services.source_row(b->services.context,(int32_t)index,&row,e)) return false;
        if(!row.present) continue;
        if(row.classname) {
            const char *name=qa_strings_cstr(qa_session_strings(b->services.shared.session),row.classname);
            if(!name) return bot_ai_fail(e,"bot objective classname lost its actual source string");
            if(source_same_name(name,classname)) continue;
        }
        volatile float x=goal->origin.x-row.state.origin[0],y=goal->origin.y-row.state.origin[1],z=goal->origin.z-row.state.origin[2];
        volatile float xx=x*x,yy=y*y,zz=z*z,xy=xx+yy,distance=xy+zz;
        if(distance<100.0f) {goal->entity=(int32_t)index;return true;}
    }
    return true;
}
static bool source_level_goal(qa_bots *b,const char *name,qa_bot_goal *goal,const char *missing,qa_error *e) {
    bool found;
    if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),-1,name,goal,&found,e)) return false;
    if(found) return true;
    char warning[128];snprintf(warning,sizeof(warning),"^3Warning: %s",missing);
    return bot_ai_source_print(b,warning,e);
}
static int32_t source_model_index(qa_bytes text) {
    size_t cursor=0;
    while(cursor<text.size && text.data[cursor] && (int8_t)text.data[cursor]<=32) ++cursor;
    bool negative=cursor<text.size && text.data[cursor]=='-';
    if(cursor<text.size && (text.data[cursor]=='+' || text.data[cursor]=='-')) ++cursor;
    uint32_t value=0;
    while(cursor<text.size && text.data[cursor]>='0' && text.data[cursor]<='9')
        value=value*10+(uint32_t)(text.data[cursor++]-'0');
    if(negative) value=0-value;
    int32_t result;memcpy(&result,&value,sizeof(result));return result;
}
bool bot_ai_source_goals_load(qa_bots *b,qa_error *e) {
    qa_cvars *configuration=b->services.configuration?b->services.configuration(b->services.context):NULL;
    const qa_cvar_view *type=configuration?qa_cvars_find(configuration,"g_gametype"):NULL;
    bot_source_goals *source=&b->source_goals;source->game_type=type?type->integer:0;
    const qa_cvar_view *maximum=configuration?qa_cvars_find(configuration,"sv_maxclients"):NULL;
    source->max_clients=maximum?maximum->integer:0;
    static const struct {const char *name,*value;} cvars[]={
        {"bot_rocketjump","1"},{"bot_grapple","0"},{"bot_fastchat","0"},{"bot_nochat","0"},
        {"bot_testrchat","0"},{"bot_challenge","0"},{"bot_predictobstacles","1"},{"g_spSkill","2"}
    };
    if(!configuration || !b->services.register_cvar)
        return bot_ai_fail(e,"bot deathmatch setup requires its actual source cvar registration owner");
    for(size_t i=0;i<sizeof(cvars)/sizeof(*cvars);++i)
        if(!b->services.register_cvar(b->services.context,cvars[i].name,cvars[i].value,0,e) ||
           !bot_ai_source_match_register(b,cvars[i].name,e)) return false;
    if(source->game_type==4) {
        if(!source_level_goal(b,"Red Flag",&source->red_flag,"CTF without Red Flag\n",e) ||
           !source_level_goal(b,"Blue Flag",&source->blue_flag,"CTF without Blue Flag\n",e)) return false;
    } else if(b->services.team_arena) {
        if(source->game_type==5) {
            if(!source_level_goal(b,"Neutral Flag",&source->neutral_flag,"One Flag CTF without Neutral Flag\n",e) ||
               !source_level_goal(b,"Red Flag",&source->red_flag,"CTF without Red Flag\n",e) ||
               !source_level_goal(b,"Blue Flag",&source->blue_flag,"CTF without Blue Flag\n",e)) return false;
        } else if(source->game_type==6 || source->game_type==7) {
            bool harvest=source->game_type==7;
            if(!source_level_goal(b,"Red Obelisk",&source->red_obelisk,
                    harvest?"Harvester without red obelisk\n":"Obelisk without red obelisk\n",e) ||
               !source_goal_entity(b,&source->red_obelisk,"team_redobelisk",e) ||
               !source_level_goal(b,"Blue Obelisk",&source->blue_obelisk,
                    harvest?"Harvester without blue obelisk\n":"Obelisk without blue obelisk\n",e) ||
               !source_goal_entity(b,&source->blue_obelisk,"team_blueobelisk",e)) return false;
            if(harvest && (!source_level_goal(b,"Neutral Obelisk",&source->neutral_obelisk,
                    "Harvester without neutral obelisk\n",e) ||
                !source_goal_entity(b,&source->neutral_obelisk,"team_neutralobelisk",e))) return false;
        }
    }
    source->max_bsp_model_index=0;
    const qa_entities *entities=qa_bot_runtime_bsp(b->runtime);
    for(int32_t entity=qa_bot_bsp_next(entities,0);entity;entity=qa_bot_bsp_next(entities,entity)) {
        qa_bytes model;
        if(!qa_bot_bsp_value(entities,entity,"model",&model) || !model.size || model.data[0]!='*') continue;
        int32_t index=source_model_index((qa_bytes){model.data+1,model.size-1});
        if(index>source->max_bsp_model_index) source->max_bsp_model_index=index;
    }
    bot_ai_source_orders_init(&b->source_orders);
    return true;
}
bool bot_ai_remember_order(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(!s->ordered) return true;
    return bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER,&s->decisionmaker,true,e) &&
        bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&s->long_term_goal,true,e) &&
        bot_ai_storage_goal(b,s,QA_BOT_SOURCE_LAST_TEAM_GOAL,&s->team_goal,true,e) &&
        bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE,&s->teammate,true,e);
}
bool bot_ai_team_status(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(!s->team_arena) return true;
    int task=3;
    switch(s->long_term_goal) {
    case BOT_LTG_TEAM_ACCOMPANY: {
        qa_bot_entity_info observation;bool found,carrying=false;
        if(!qa_bot_runtime_entity(b->runtime,s->teammate,&observation,&found,e)) return false;
        if(b->source_goals.game_type==4 || b->source_goals.game_type==5)
            carrying=(observation.state.powerups&((1<<7)|(1<<8)|(1<<9)))!=0;
        else if(b->source_goals.game_type==7) {
            int32_t cubes;
            if(!b->services.source_generic1 || !b->services.source_generic1(b->services.context,observation.number,&cubes,e)) return false;
            carrying=cubes>0;
        }
        task=carrying?6:4;break;
    }
    case BOT_LTG_DEFEND:case BOT_LTG_RUSH_BASE:task=2;break;
    case BOT_LTG_GET_FLAG:case BOT_LTG_HARVEST:case BOT_LTG_ATTACK_BASE:task=1;break;
    case BOT_LTG_RETURN_FLAG:task=5;break;
    case BOT_LTG_CAMP:case BOT_LTG_CAMP_ORDER:task=7;break;
    default:break;
    }
    char value[2]={(char)('0'+task),0};
    return b->services.userinfo?b->services.userinfo(b->services.context,s->view.actor,"teamtask",value,e):
        bot_ai_fail(e,"native team task requires its actual source userinfo owner");
}

static bool same_mode(qa_mode_id a, qa_mode_id b) {
    return a.slot==b.slot && a.generation==b.generation;
}
bool bot_ai_carrying(qa_bots *b,bot_ai_state *s,bool *carrying,qa_error *e) {
    *carrying=bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0 ||
        bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 ||
        bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0;
    if(!b->services.modes || !s->view.mode.generation) return true;
    *carrying=false;
    qa_mode_statistics statistics;
    if(!qa_modes_statistics(b->services.modes,s->view.mode,s->view.actor,&statistics,e)) return false;
    if(statistics.tokens>0) *carrying=true;
    const qa_actor_registry *actors=qa_session_actors(b->services.shared.session);
    uint32_t cursor=0;const qa_actor_record *record;
    while(!*carrying && qa_actors_next(actors,&cursor,&record)) {
        qa_mode_object_view object;
        if(qa_modes_object_read(b->services.modes,record->id,&object) && same_mode(object.mode,s->view.mode) &&
           object.kind==QA_MODE_OBJECT_FLAG && object.phase==QA_OBJECTIVE_CARRIED &&
           qa_actor_id_equal(object.carrier,s->view.actor))
            *carrying=true;
    }
    return true;
}
static bool command_word(const char *text,const char *word) {
    while(*text && *word) {
        unsigned char c=(unsigned char)*text++;
        if(c>='A' && c<='Z') c+='a'-'A';
        unsigned char expected=(unsigned char)*word++;
        if(expected>='A' && expected<='Z') expected+='a'-'A';
        if(c!=expected) return false;
    }
    return !*text && !*word;
}
static int32_t token_number(const char **text) {
    const char *start=*text;
    while(**text && (int8_t)(unsigned char)**text>32) ++*text;
    bool empty=*text==start;
    while(**text && (int8_t)(unsigned char)**text<=32) ++*text;
    return empty?0:source_integer(start);
}
static bool ordered(qa_bots *b,bot_ai_state *s,int32_t client,bot_long_term_goal goal,float duration,qa_error *e) {
    s->decisionmaker=client;s->ordered=true;s->order_time=b->time;
    float random;if(!bot_ai_random(b,&random,e)) return false;
    volatile float delay=2.0f*random;s->team_message_time=b->time+delay;
    s->long_term_goal=goal;s->team_goal_time=b->time+duration;
    return true;
}
static bool initial_chat(qa_bots *b,bot_ai_state *s,const char *type,
    const char *argument,int32_t recipient,qa_bot_chat_destination destination,qa_error *e) {
    const char *variables[8]={argument,NULL,NULL,NULL,NULL,NULL,NULL,NULL};bool found;
    qa_bot_chat *chat=qa_bot_runtime_chat(b->runtime,s->chat);
    int32_t self,team;
    if(!bot_ai_source_client(b,s,&self,e) || !source_team(b,self,&team,e)) return false;
    uint32_t context=1027u;
    if(b->source_goals.game_type==4 || (s->team_arena && b->source_goals.game_type==5)) context|=team==1?4u:8u;
    else if(s->team_arena && b->source_goals.game_type==6) context|=team==1?32u:64u;
    else if(s->team_arena && b->source_goals.game_type==7) context|=team==1?128u:256u;
    return qa_bot_chat_initial(chat,type,context,variables,b->time,&found,e) &&
        qa_bot_chat_enter(chat,recipient,destination,e);
}
static bool voice_only(qa_bots *b,bot_ai_state *s,int32_t recipient,const char *name,qa_error *e) {
    char command[80];
    if(recipient==-1) snprintf(command,sizeof(command),"vosay_team %s",name);
    else snprintf(command,sizeof(command),"votell %d %s",recipient,name);
    return qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,QA_BOT_COMMAND,0,command,e);
}
static bool locate_requester(qa_bots *b,bot_ai_state *s,int32_t client,bool *found,qa_error *e) {
    *found=false;s->team_goal.entity=-1;
    qa_bot_entity_info entity;bool present;
    if(!qa_bot_runtime_entity(b->runtime,client,&entity,&present,e)) return false;
    if(entity.valid) {
        uint32_t area;if(!bot_ai_point_area(b,s,entity.state.origin,&area,e)) return false;
        if(area) {
            s->team_goal.entity=client;s->team_goal.area=(int32_t)area;
            s->team_goal.origin=entity.state.origin;
            s->team_goal.mins=qa_v3(-8,-8,-8);s->team_goal.maxs=qa_v3(8,8,8);*found=true;
        }
    }
    if(*found) return true;
    char name[36];
    return bot_ai_easy_name(b,client,name,sizeof(name),e) &&
        initial_chat(b,s,"whereareyou",name,client,QA_BOT_CHAT_TELL,e);
}
bool bot_ai_voice(qa_bots *b, bot_ai_state *s, int32_t channel, const char *text, qa_error *e) {
    if(!text || !channel || b->source_goals.game_type<3) return true;
    char source[256];size_t size=strlen(text);if(size>=sizeof(source)) size=sizeof(source)-1;
    memcpy(source,text,size);source[size]=0;text=source;
    token_number(&text);int32_t client=token_number(&text);token_number(&text);
    int32_t self,team,requester_team;
    if(!bot_ai_source_client(b,s,&self,e) || !source_team(b,self,&team,e) ||
       !source_team(b,client,&requester_team,e)) return false;
    if(client<0 || client>=64 || team!=requester_team) return true;
    char leader[33];memcpy(leader,s->team_leader_name,32);leader[32]=0;
    if(command_word(text,"startleader")) {
        return bot_ai_client_name(b,client,s->team_leader_name,sizeof(s->team_leader_name),true,e);
    }
    if(command_word(text,"stopleader")) {
        char name[256];if(!bot_ai_client_name(b,client,name,sizeof(name),true,e)) return false;
        if(command_word(leader,name)) {
            s->team_leader_name[0]=0;b->not_leader[client]=true;
        }
        return true;
    }
    if(command_word(text,"whoisleader")) {
        char name[256];if(!bot_ai_client_name(b,self,name,sizeof(name),true,e)) return false;
        return !command_word(leader,name) ||
            (initial_chat(b,s,"iamteamleader",NULL,0,QA_BOT_CHAT_TEAM,e) && voice_only(b,s,-1,"startleader",e));
    }
    if(command_word(text,"patrol")) {
        s->decisionmaker=client;s->long_term_goal=BOT_LTG_NONE;s->lead_time=0;
        int32_t last_type=0;
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,true,e)) return false;
        s->view.order=(qa_bot_order){0};
        return initial_chat(b,s,"dismissed",NULL,client,QA_BOT_CHAT_TELL,e) &&
            voice_only(b,s,-1,"onpatrol",e) && bot_ai_team_status(b,s,e);
    }
    int32_t type=b->source_goals.game_type;bool remember=true;
    if(command_word(text,"followflagcarrier")) {
        int32_t carrier=-1;
        for(int32_t i=0;i<b->source_goals.max_clients && i<64;++i) {
            if(i==self) continue;
            qa_bot_entity_info observation;bool found;
            if(!qa_bot_runtime_entity(b->runtime,i,&observation,&found,e)) return false;
            if(!observation.valid || !(observation.state.powerups&((1<<7)|(1<<8)|(s->team_arena?(1<<9):0)))) continue;
            int32_t other_team;if(!source_team(b,i,&other_team,e)) return false;
            if(other_team==team) {carrier=i;break;}
        }
        if(carrier<0) return true;
        client=carrier;text="followme";
    }
    if(command_word(text,"followme") || command_word(text,"camp")) {
        bool found;if(!locate_requester(b,s,client,&found,e)) return false;
        if(!found) return true;
        bool follow=command_word(text,"followme");
        if(!ordered(b,s,client,follow?BOT_LTG_TEAM_ACCOMPANY:BOT_LTG_CAMP_ORDER,600,e)) return false;
        s->teammate=client;s->arrive_time=0;
        if(follow) {s->teammate_visible_time=b->time;s->formation_distance=112;}
    } else if(command_word(text,"getflag") || command_word(text,"offense")) {
        if(type==4 || (s->team_arena && type==5)) {
            if(!b->source_goals.red_flag.area || !b->source_goals.blue_flag.area ||
               (type==5 && !b->source_goals.neutral_flag.area)) return true;
            if(!ordered(b,s,client,BOT_LTG_GET_FLAG,600,e)) return false;
            if(type==4 && (!bot_ai_source_client(b,s,&self,e) || !source_team(b,self,&team,e) ||
                !bot_ai_source_alternate_route(b,s,team==1?2:team==2?1:0,e))) return false;
        } else if(command_word(text,"getflag")) return true;
        else if(s->team_arena && type==7) {
            if(!ordered(b,s,client,BOT_LTG_HARVEST,120,e)) return false;
            s->harvest_away_time=0;
        } else {
            if(!ordered(b,s,client,BOT_LTG_ATTACK_BASE,600,e)) return false;
            s->attack_away_time=0;
        }
    } else if(command_word(text,"defend") || command_word(text,"defendflag")) {
        if(team!=1 && team!=2) return true;
        if(s->team_arena && (type==6 || type==7))
            s->team_goal=team==1?b->source_goals.red_obelisk:b->source_goals.blue_obelisk;
        else if(type==4 || (s->team_arena && type==5))
            s->team_goal=team==1?b->source_goals.red_flag:b->source_goals.blue_flag;
        else return true;
        if(!ordered(b,s,client,BOT_LTG_DEFEND,600,e)) return false;
        s->defend_away_time=0;
    } else if(command_word(text,"returnflag")) {
        if(type!=4 && !(s->team_arena && type==5)) return true;
        if(!ordered(b,s,client,BOT_LTG_RETURN_FLAG,180,e)) return false;
        s->rush_base_away_time=0;remember=false;
    } else if(command_word(text,"wantondefense") || command_word(text,"wantonoffense")) {
        char name[36],easy[36];
        if(!bot_ai_client_name(b,client,name,sizeof(name),true,e)) return false;
        int32_t preference=command_word(b->team_preferences[client].name,name)?b->team_preferences[client].preference:0;
        bool defense=command_word(text,"wantondefense");
        b->team_preferences[client].preference=(preference&~(defense?2:1))|(defense?1:2);
        memcpy(b->team_preferences[client].name,name,strlen(name)+1);
        return bot_ai_easy_name(b,client,easy,sizeof(easy),e) &&
            initial_chat(b,s,"keepinmind",easy,client,QA_BOT_CHAT_TELL,e) && voice_only(b,s,client,"yes",e) &&
            qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_AFFIRMATIVE,e);
    } else return true;
    if(!bot_ai_team_status(b,s,e)) return false;
    return !remember || bot_ai_remember_order(b,s,e);
}
