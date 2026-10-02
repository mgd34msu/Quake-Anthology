#include "internal.h"
#include "source_event_state.h"
#include "source_team_state.h"
#include "source_inventory.h"
#include "source_chat.h"
#include "source_goal.h"
#include "source_orders.h"
#include "source_player.h"
#include "source_timers.h"
#include "source_behavior_state.h"

enum {
    SOURCE_CHAT_INSULT=24, SOURCE_CHAT_MISC=25, SOURCE_CHAT_START_END=26,
    SOURCE_CHAT_ENTER_EXIT=27, SOURCE_CHAT_KILL=28, SOURCE_CHAT_DEATH=29,
    SOURCE_CHAT_HIT_TALKING=31, SOURCE_CHAT_HIT_NO_DEATH=32,
    SOURCE_CHAT_HIT_NO_KILL=33, SOURCE_CHAT_RANDOM=34
};
static const char invalid_variable[]="[invalid var]";
static bool live(qa_bots *b,bot_ai_state *s) {
    return !s->retired && bot_ai_live(b,s->view.actor);
}
#define CHAT_CALL(call) do { if(!(call)) return false; if(!live(b,s)) return true; } while(0)

void bot_ai_source_chat_globals_init(bot_source_chat_globals *state) {
    *state=(bot_source_chat_globals){0};
}
static int32_t source_integer(const char *text) {
    while(*text && (int8_t)(unsigned char)*text<=32) ++text;
    bool negative=*text=='-';if(*text=='-' || *text=='+') ++text;
    uint32_t value=0;
    while(*text>='0' && *text<='9') value=value*10+(uint32_t)(*text++-'0');
    if(negative) value=0-value;
    int32_t result;memcpy(&result,&value,sizeof(result));return result;
}
static bool maximum(qa_bots *b,int32_t *cell,int32_t *out,qa_error *e) {
    if(!b->services.configuration) return bot_ai_fail(e,"Source chat has no actual cvar owner");
    if(!*cell) {
        qa_cvars *registry=b->services.configuration(b->services.context);
        if(!registry) return bot_ai_fail(e,"Source chat cvar owner is unavailable");
        const qa_cvar_view *value=qa_cvars_find(registry,"sv_maxclients");
        *cell=value?value->integer:0;
    }
    *out=*cell<64?*cell:64;return true;
}
static bool active(qa_bots *b,int32_t client,bool *out,qa_error *e) {
    if(!b->services.configstring) return bot_ai_fail(e,"Source chat has no actual GAME configstrings");
    char text[1024],name[1024],team[1024];
    if(!b->services.configstring(b->services.context,544+(uint32_t)client,text,sizeof(text),e) ||
       !qa_q3_info_value(text,"n",name,sizeof(name),e) ||
       !qa_q3_info_value(text,"t",team,sizeof(team),e)) return false;
    *out=text[0] && name[0] && source_integer(team)!=3;return true;
}
static bool active_count(qa_bots *b,bot_ai_state *s,int32_t *out,qa_error *e) {
    *out=0;int32_t max;
    CHAT_CALL(maximum(b,&b->source_chat.active_maxclients,&max,e));
    for(int32_t client=0;client<max;++client) {
        bool present;CHAT_CALL(active(b,client,&present,e));
        if(present) ++*out;
    }
    return true;
}
static bool player(qa_bots *b,int32_t client,qa_bot_source_player_state *out,qa_error *e) {
    return b->services.source_player_state?
        b->services.source_player_state(b->services.context,client,out,e):
        bot_ai_fail(e,"Source chat lacks its actual fixed GAME player-state reader");
}
static bool ranking(qa_bots *b,bot_ai_state *s,bool first,bool *out,qa_error *e) {
    *out=false;
    int32_t max;
    CHAT_CALL(maximum(b,first?&b->source_chat.first_maxclients:&b->source_chat.last_maxclients,&max,e));
    int32_t score;
    CHAT_CALL(bot_ai_source_player_slot(b,s,BOT_PS_PERSISTENT,0,&score,e));
    for(int32_t client=0;client<max;++client) {
        bool present;CHAT_CALL(active(b,client,&present,e));if(!present) continue;
        qa_bot_source_player_state current;CHAT_CALL(player(b,client,&current,e));
        if(!current.present || !current.has_player)
            return bot_ai_fail(e,"Source chat ranking active player has no client state");
        if(first?score<current.score:score>current.score) return true;
    }
    *out=true;return true;
}
static bool ranked_name(qa_bots *b,bot_ai_state *s,bool first,char out[32],qa_error *e) {
    int32_t max;out[0]=0;
    CHAT_CALL(maximum(b,first?&b->source_chat.first_name_maxclients:&b->source_chat.last_name_maxclients,&max,e));
    int32_t score=first?-999999:999999,selected=0;
    for(int32_t client=0;client<max;++client) {
        bool present;CHAT_CALL(active(b,client,&present,e));if(!present) continue;
        qa_bot_source_player_state current;CHAT_CALL(player(b,client,&current,e));
        if(!current.present || !current.has_player)
            return bot_ai_fail(e,"Source chat ranked name active player has no client state");
        if(first?current.score>score:current.score<score) {score=current.score;selected=client;}
    }
    return bot_ai_easy_name(b,selected,out,32,e);
}
static bool opponent_name(qa_bots *b,bot_ai_state *s,char out[32],qa_error *e) {
    out[0]=0;int32_t max,self,opponents[64],count=0;
    CHAT_CALL(maximum(b,&b->source_chat.opponent_maxclients,&max,e));
    CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    for(int32_t client=0;client<max;++client) {
        if(client==self) continue;
        bool present;CHAT_CALL(active(b,client,&present,e));if(!present) continue;
        bool same;CHAT_CALL(bot_ai_source_same_team(b,s,client,&same,e));
        if(!same) opponents[count++]=client;
    }
    float random;CHAT_CALL(bot_ai_random(b,&random,e));
    volatile float scaled=random*(float)count;
    int32_t index=(int32_t)scaled,selected=count?opponents[0]:0;
    for(int32_t i=0;i<count;++i) if(--index<=0) {selected=opponents[i];break;}
    return bot_ai_easy_name(b,selected,out,32,e);
}
static bool own_name(qa_bots *b,bot_ai_state *s,char out[32],qa_error *e) {
    out[0]=0;int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    return bot_ai_easy_name(b,self,out,32,e);
}
static bool map_title(qa_bots *b,char out[128],qa_error *e) {
    out[0]=0;
    qa_cvars *registry=b->services.configuration?b->services.configuration(b->services.context):NULL;
    if(!registry) return bot_ai_fail(e,"Source chat map title lacks its actual server-info registry");
    qa_buffer info={0};char full[1024];
    bool ok=qa_cvars_info(registry,QA_CVAR_SERVERINFO,0,&info,e);
    if(ok) ok=qa_q3_info_value(info.data?(const char *)info.data:"","mapname",full,sizeof(full),e);
    if(ok) {
        size_t length=strlen(full);if(length>127) length=127;
        memcpy(out,full,length);out[length]=0;
    }
    qa_buffer_free(&info);return ok;
}
static const char *weapon_name(bool missionpack,int32_t method) {
    switch(method) {
    case 1:return "Shotgun";case 2:return "Gauntlet";case 3:return "Machinegun";
    case 4:case 5:return "Grenade Launcher";case 6:case 7:return "Rocket Launcher";
    case 8:case 9:return "Plasmagun";case 10:return "Railgun";case 11:return "Lightning Gun";
    case 12:case 13:return "BFG10K";case 23:return missionpack?"Nailgun":"Grapple";
    case 24:if(missionpack) return "Chaingun";break;
    case 25:if(missionpack) return "Proximity Launcher";break;
    case 26:if(missionpack) return "Kamikaze";break;
    case 27:if(missionpack) return "Prox mine";break;
    case 28:if(missionpack) return "Grapple";break;
    default:break;
    }
    return "[unknown weapon]";
}
static bool random_weapon(qa_bots *b,bot_ai_state *s,const char **out,qa_error *e) {
    *out="BFG10K";float random;CHAT_CALL(bot_ai_random(b,&random,e));
    volatile float scaled=random*(b->services.team_arena?11.9f:8.9f);
    switch((int32_t)scaled) {
    case 0:*out="Gauntlet";break;case 1:*out="Shotgun";break;case 2:*out="Machinegun";break;
    case 3:*out="Grenade Launcher";break;case 4:*out="Rocket Launcher";break;case 5:*out="Plasmagun";break;
    case 6:*out="Railgun";break;case 7:*out="Lightning Gun";break;
    case 8:if(b->services.team_arena) *out="Nailgun";break;
    case 9:if(b->services.team_arena) *out="Chaingun";break;
    case 10:if(b->services.team_arena) *out="Proximity Launcher";break;
    default:break;
    }
    return true;
}
static bool observation(qa_bots *b,int32_t number,qa_bot_entity_info *out,qa_error *e) {
    bool found;return qa_bot_runtime_entity(b->runtime,number,out,&found,e);
}
static bool observer(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    int32_t type;CHAT_CALL(bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&type,e));
    if(type==2) {*out=true;return true;}
    int32_t self,team;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    CHAT_CALL(bot_ai_source_team(b,self,&team,e));*out=team==3;return true;
}
bool bot_ai_source_visible_enemies(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    for(int32_t client=0;client<64;++client) {
        if(client==self) continue;
        qa_bot_entity_info info;CHAT_CALL(observation(b,client,&info,e));
        if(!info.valid) continue;
        if(info.number>=0 && info.number<64) {
            qa_bot_source_player_state current;CHAT_CALL(player(b,info.number,&current,e));
            if(current.has_player && current.pm_type!=0) continue;
        }
        if(info.number==s->view.entity) continue;
        uint32_t flags=(1u<<7)|(1u<<8)|(b->services.team_arena?(1u<<9):0);
        bool invisible=!((uint32_t)info.state.powerups&flags) && ((uint32_t)info.state.powerups&(1u<<4));
        if(invisible && !(info.state.flags&0x100)) continue;
        bool same;CHAT_CALL(bot_ai_source_same_team(b,s,client,&same,e));if(same) continue;
        float visible;CHAT_CALL(bot_ai_source_entity_visible(b,s,client,&visible,e));
        if(visible>0) {*out=true;return true;}
    }
    return true;
}
static bool contents(qa_bots *b,qa_vec3 point,qa_actor_id pass,int32_t *out,qa_error *e) {
    qa_point_query query={.point=point,.pass_actor=pass,.q3_server_entities=true,
        .policy={.family=QA_COLLISION_Q3,.q1_hull=-1}};
    qa_point_contents result;
    if(!qa_world_point_contents(b->services.shared.world,&query,&result,e)) return false;
    *out=result.contents;return true;
}
bool bot_ai_source_valid_chat_position(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;
    int32_t type;CHAT_CALL(bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&type,e));
    if(type==3) {*out=true;return true;}
    static const int powerups[]={QA_BOT_INV_QUAD,QA_BOT_INV_HASTE,QA_BOT_INV_INVISIBILITY,QA_BOT_INV_REGEN,QA_BOT_INV_FLIGHT};
    for(size_t i=0;i<5;++i) if(bot_ai_inventory_value(s,powerups[i])!=0) return true;
    qa_vec3 below=s->player.origin,above=below;int32_t point;
    below.z-=24.0f;above.z+=32.0f;
    qa_actor_id pass=bot_ai_source_actor(b,s->view.entity);
    CHAT_CALL(contents(b,below,pass,&point,e));if(point&(8|16)) return true;
    CHAT_CALL(contents(b,above,pass,&point,e));if(point&(8|16|32)) return true;
    qa_bot_navigation *nav=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
    if(!nav) return bot_ai_fail(e,"Source chat position lacks its actual navigation");
    qa_bounds bounds=qa_bot_navigation_presence(nav,4);qa_trace_result trace;
    qa_vec3 start=s->player.origin,end=start;start.z+=1.0f;end.z-=10.0f;
    int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    CHAT_CALL(qa_bot_navigation_trace(nav,start,end,&bounds,bot_ai_source_actor(b,self),1,&trace,e));
    qa_actor_id world=bot_ai_source_actor(b,QA_Q3_ENTITY_WORLD);
    *out=trace.hit==QA_TRACE_HIT_WORLD || (world.registry && qa_actor_id_equal(trace.actor,world));
    return true;
}
static bool unavailable(qa_bots *b,bot_ai_state *s) {
    volatile float before=b->time-25.0f;
    return b->controls.no_chat || bot_ai_last_chat_time(s)>before;
}
static bool characteristic(qa_bots *b,bot_ai_state *s,uint32_t index,float *out,qa_error *e) {
    return bot_ai_character_float(b,s,index,0,1,out,e);
}
static bool refused(qa_bots *b,bot_ai_state *s,float probability,bool *out,qa_error *e) {
    *out=false;if(b->controls.fast_chat) return true;
    float random;CHAT_CALL(bot_ai_random(b,&random,e));*out=random>probability;return true;
}
static void all_chat(qa_bots *b,bot_ai_state *s,bool *out) {
    bot_ai_last_chat_time_set(s,b->time);bot_ai_chat_to_set(s,QA_BOT_CHAT_ALL);*out=true;
}
static bool taunt(qa_bots *b,bot_ai_state *s,qa_error *e) {
    return qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,
        QA_BOT_COMMAND,0,"vtaunt",e);
}
static bool random_type(qa_bots *b,bot_ai_state *s,uint32_t index,const char *yes,const char *no,
                         const char **out,qa_error *e) {
    *out=no;float random,chance;
    CHAT_CALL(bot_ai_random(b,&random,e));CHAT_CALL(characteristic(b,s,index,&chance,e));
    *out=random<chance?yes:no;return true;
}
static bool game_chat(qa_bots *b,bot_ai_state *s,bool entering,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s) || b->source_goals.game_type>=3 || b->source_goals.game_type==1) return true;
    float chance;bool skip;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_ENTER_EXIT,&chance,e));
    CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    if(entering) {bool valid;CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;}
    char own[32],opponent[32],map[128];
    CHAT_CALL(own_name(b,s,own,e));CHAT_CALL(opponent_name(b,s,opponent,e));CHAT_CALL(map_title(b,map,e));
    const char *variables[8]={own,opponent,invalid_variable,invalid_variable,map,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,entering?"game_enter":"game_exit",variables,e));
    all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_enter_game(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    return game_chat(b,s,true,out,e);
}
bool bot_ai_source_chat_exit_game(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    return game_chat(b,s,false,out,e);
}
static bool level_available(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(b->controls.no_chat) return true;
    bool watching;CHAT_CALL(observer(b,s,&watching,e));if(watching) return true;
    volatile float before=b->time-25.0f;*out=!(bot_ai_last_chat_time(s)>before);return true;
}
bool bot_ai_source_chat_start_level(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;bool available;CHAT_CALL(level_available(b,s,&available,e));if(!available) return true;
    if(b->source_goals.game_type>=3) return taunt(b,s,e);
    if(b->source_goals.game_type==1) return true;
    float chance;bool skip;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_START_END,&chance,e));
    CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    char own[32];CHAT_CALL(own_name(b,s,own,e));
    const char *variables[8]={own,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,"level_start",variables,e));all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_end_level(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;bool available;CHAT_CALL(level_available(b,s,&available,e));if(!available) return true;
    if(b->source_goals.game_type>=3) {
        bool first;CHAT_CALL(ranking(b,s,true,&first,e));if(first) CHAT_CALL(taunt(b,s,e));
        *out=true;return true;
    }
    if(b->source_goals.game_type==1) return true;
    float chance;bool skip;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_START_END,&chance,e));
    CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    bool first,last=false;CHAT_CALL(ranking(b,s,true,&first,e));if(!first) CHAT_CALL(ranking(b,s,false,&last,e));
    char own[32],opponent[32],winner[32],loser[32],map[128];
    CHAT_CALL(own_name(b,s,own,e));CHAT_CALL(opponent_name(b,s,opponent,e));
    if(!first) CHAT_CALL(ranked_name(b,s,true,winner,e));
    if(!last) CHAT_CALL(ranked_name(b,s,false,loser,e));
    CHAT_CALL(map_title(b,map,e));
    const char *variables[8]={own,opponent,first?invalid_variable:winner,last?invalid_variable:loser,map,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,first?"level_end_victory":last?"level_end_lose":"level_end",variables,e));
    all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_death(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s)) return true;
    float chance;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_DEATH,&chance,e));
    if(b->source_goals.game_type==1) return true;
    bool skip;CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    int32_t attacker=bot_ai_last_killed_by(s),self;char name[32];
    if(attacker>=0 && attacker<64) CHAT_CALL(bot_ai_easy_name(b,attacker,name,sizeof(name),e));
    else memcpy(name,"[world]",sizeof("[world]"));
    bool same=false;
    if(b->source_goals.game_type>=3) CHAT_CALL(bot_ai_source_same_team(b,s,attacker,&same,e));
    if(same) {
        CHAT_CALL(bot_ai_source_client(b,s,&self,e));if(attacker==self) return true;
        const char *variables[8]={name,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
        CHAT_CALL(bot_ai_source_initial_chat(b,s,"death_teammate",variables,e));
        bot_ai_chat_to_set(s,QA_BOT_CHAT_TEAM);
    } else {
        if(b->source_goals.game_type>=3) {CHAT_CALL(taunt(b,s,e));*out=true;return true;}
        int32_t method=bot_ai_bot_death_type(s);const char *type=NULL;
        char opponent[32];const char *first=name,*second=NULL;
        if(method==14 || method==15 || method==16 || method==19 || bot_ai_bot_suicide(s) ||
           method==17 || method==20 || method==21 || method==22 || method==0) {
            type=method==14?"death_drown":method==15?"death_slime":method==16?"death_lava":
                method==19?"death_cratered":"death_suicide";
            CHAT_CALL(opponent_name(b,s,opponent,e));first=opponent;
        } else if(method==18) type="death_telefrag";
        else {
            size_t templates=0;
            if(b->services.team_arena && method==26) {
                templates=qa_bot_chat_initial_count(qa_bot_runtime_chat(b->runtime,s->chat),"death_kamikaze");
                if(!live(b,s)) return true;
            }
            if(templates) type="death_kamikaze";
            else {
                bool special=false;
                if(method==2 || method==10 || method==12 || method==13) {
                    float random;CHAT_CALL(bot_ai_random(b,&random,e));special=random<.5f;
                }
                if(special) type=method==2?"death_gauntlet":method==10?"death_rail":"death_bfg";
                else CHAT_CALL(random_type(b,s,SOURCE_CHAT_INSULT,"death_insult","death_praise",&type,e));
                second=weapon_name(b->services.team_arena,method);
            }
        }
        const char *variables[8]={first,second,NULL,NULL,NULL,NULL,NULL,NULL};
        CHAT_CALL(bot_ai_source_initial_chat(b,s,type,variables,e));bot_ai_chat_to_set(s,QA_BOT_CHAT_ALL);
    }
    bot_ai_last_chat_time_set(s,b->time);*out=true;return true;
}
bool bot_ai_source_chat_kill(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s)) return true;
    float chance;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_KILL,&chance,e));
    if(b->source_goals.game_type==1) return true;
    bool skip;CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));if(bot_ai_last_killed_player(s)==self) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    bool valid;CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;
    bool enemies;CHAT_CALL(bot_ai_source_visible_enemies(b,s,&enemies,e));if(enemies) return true;
    char name[32];CHAT_CALL(bot_ai_easy_name(b,bot_ai_last_killed_player(s),name,sizeof(name),e));
    bot_ai_chat_to_set(s,QA_BOT_CHAT_ALL);bool same=false;
    if(b->source_goals.game_type>=3) CHAT_CALL(bot_ai_source_same_team(b,s,bot_ai_last_killed_player(s),&same,e));
    const char *type;
    if(same) type="kill_teammate";
    else {
        if(b->source_goals.game_type>=3) return taunt(b,s,e);
        int32_t method=bot_ai_enemy_death_type(s);
        if(method==2) type="kill_gauntlet";else if(method==10) type="kill_rail";else if(method==18) type="kill_telefrag";
        else {
            size_t templates=0;
            if(b->services.team_arena && bot_ai_bot_death_type(s)==26) {
                templates=qa_bot_chat_initial_count(qa_bot_runtime_chat(b->runtime,s->chat),"kill_kamikaze");
                if(!live(b,s)) return true;
            }
            if(templates) type="kill_kamikaze";
            else CHAT_CALL(random_type(b,s,SOURCE_CHAT_INSULT,"kill_insult","kill_praise",&type,e));
        }
    }
    const char *variables[8]={name,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,type,variables,e));
    if(same) bot_ai_chat_to_set(s,QA_BOT_CHAT_TEAM);
    bot_ai_last_chat_time_set(s,b->time);*out=true;return true;
}
bool bot_ai_source_chat_enemy_suicide(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s)) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    float chance;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_KILL,&chance,e));
    if(b->source_goals.game_type>=3 || b->source_goals.game_type==1) return true;
    bool skip;CHAT_CALL(refused(b,s,chance,&skip,e));if(skip) return true;
    bool valid,enemies;CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;
    CHAT_CALL(bot_ai_source_visible_enemies(b,s,&enemies,e));if(enemies) return true;
    char name[32]={0};if(s->source_enemy>=0) CHAT_CALL(bot_ai_easy_name(b,s->source_enemy,name,sizeof(name),e));
    const char *variables[8]={name,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,"enemy_suicide",variables,e));all_chat(b,s,out);return true;
}
static bool own_player(qa_bots *b,bot_ai_state *s,qa_bot_source_player_state *out,qa_error *e) {
    int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));CHAT_CALL(player(b,self,out,e));
    return out->has_player?true:bot_ai_fail(e,"Source hit chat requires an actual GAME client");
}
static bool attacker_allowed(qa_bots *b,bot_ai_state *s,int32_t attacker,bool *out,qa_error *e) {
    *out=false;int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    *out=attacker!=0 && attacker!=self && attacker>=0 && attacker<64;return true;
}
static bool hit_chance(qa_bots *b,bot_ai_state *s,uint32_t index,bool *out,qa_error *e) {
    *out=false;float chance;CHAT_CALL(characteristic(b,s,index,&chance,e));
    if(b->source_goals.game_type>=3 || b->source_goals.game_type==1) return true;
    bool skip;volatile float probability=chance*.5f;
    CHAT_CALL(refused(b,s,probability,&skip,e));*out=!skip;return true;
}
bool bot_ai_source_chat_hit_talking(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s)) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    qa_bot_source_player_state current;CHAT_CALL(own_player(b,s,&current,e));
    bool allowed;CHAT_CALL(attacker_allowed(b,s,current.last_hurt_client,&allowed,e));if(!allowed) return true;
    CHAT_CALL(hit_chance(b,s,SOURCE_CHAT_HIT_TALKING,&allowed,e));if(!allowed) return true;
    bool valid;CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;
    char name[32];CHAT_CALL(bot_ai_client_name(b,current.last_hurt_client,name,sizeof(name),true,e));
    const char *variables[8]={name,weapon_name(b->services.team_arena,current.last_hurt_client),NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,"hit_talking",variables,e));all_chat(b,s,out);return true;
}
static bool safe_hit_position(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;bool valid,enemies;
    CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;
    CHAT_CALL(bot_ai_source_visible_enemies(b,s,&enemies,e));if(enemies) return true;
    qa_bot_entity_info info;CHAT_CALL(observation(b,s->source_enemy,&info,e));
    *out=!(info.state.flags&0x100);return true;
}
bool bot_ai_source_chat_hit_no_death(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;qa_bot_source_player_state current;CHAT_CALL(own_player(b,s,&current,e));
    bool allowed;CHAT_CALL(attacker_allowed(b,s,current.last_hurt_client,&allowed,e));if(!allowed) return true;
    if(unavailable(b,s)) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    CHAT_CALL(hit_chance(b,s,SOURCE_CHAT_HIT_NO_DEATH,&allowed,e));if(!allowed) return true;
    CHAT_CALL(safe_hit_position(b,s,&allowed,e));if(!allowed) return true;
    char name[32];CHAT_CALL(bot_ai_client_name(b,current.last_hurt_client,name,sizeof(name),true,e));
    const char *variables[8]={name,weapon_name(b->services.team_arena,current.last_hurt_mod),NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,"hit_nodeath",variables,e));all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_hit_no_kill(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;if(unavailable(b,s)) return true;
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    bool allowed;CHAT_CALL(hit_chance(b,s,SOURCE_CHAT_HIT_NO_KILL,&allowed,e));if(!allowed) return true;
    CHAT_CALL(safe_hit_position(b,s,&allowed,e));if(!allowed) return true;
    qa_bot_source_player_state enemy;CHAT_CALL(player(b,s->source_enemy,&enemy,e));
    if(!enemy.has_player) return bot_ai_fail(e,"Source hit-no-kill chat requires an actual enemy client");
    char name[32];CHAT_CALL(bot_ai_client_name(b,s->source_enemy,name,sizeof(name),true,e));
    const char *variables[8]={name,weapon_name(b->services.team_arena,enemy.last_hurt_mod),NULL,NULL,NULL,NULL,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,"hit_nokill",variables,e));all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_random(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    *out=false;bool available;CHAT_CALL(level_available(b,s,&available,e));if(!available) return true;
    if(b->source_goals.game_type==1 || bot_ai_long_term_goal(s)==BOT_LTG_TEAM_HELP ||
       bot_ai_long_term_goal(s)==BOT_LTG_TEAM_ACCOMPANY || bot_ai_long_term_goal(s)==BOT_LTG_RUSH_BASE) return true;
    float chance,random;CHAT_CALL(characteristic(b,s,SOURCE_CHAT_RANDOM,&chance,e));
    CHAT_CALL(bot_ai_random(b,&random,e));volatile float limit=s->view.think_time*.1f;
    if(random>limit) return true;
    if(!b->controls.fast_chat) {
        CHAT_CALL(bot_ai_random(b,&random,e));if(random>chance) return true;
        CHAT_CALL(bot_ai_random(b,&random,e));if(random>.25f) return true;
    }
    int32_t count;CHAT_CALL(active_count(b,s,&count,e));if(count<=1) return true;
    bool valid,enemies;CHAT_CALL(bot_ai_source_valid_chat_position(b,s,&valid,e));if(!valid) return true;
    CHAT_CALL(bot_ai_source_visible_enemies(b,s,&enemies,e));if(enemies) return true;
    int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));char name[32];
    if(bot_ai_last_killed_player(s)==self) CHAT_CALL(opponent_name(b,s,name,e));
    else CHAT_CALL(bot_ai_easy_name(b,bot_ai_last_killed_player(s),name,sizeof(name),e));
    if(b->source_goals.game_type>=3) return taunt(b,s,e);
    const char *type;CHAT_CALL(random_type(b,s,SOURCE_CHAT_MISC,"random_misc","random_insult",&type,e));
    char opponent[32],map[128];const char *weapon;
    CHAT_CALL(opponent_name(b,s,opponent,e));CHAT_CALL(map_title(b,map,e));CHAT_CALL(random_weapon(b,s,&weapon,e));
    const char *variables[8]={opponent,name,invalid_variable,invalid_variable,map,weapon,NULL,NULL};
    CHAT_CALL(bot_ai_source_initial_chat(b,s,type,variables,e));all_chat(b,s,out);return true;
}
bool bot_ai_source_chat_time(qa_bots *b,bot_ai_state *s,float *out,qa_error *e) {
    *out=0;int32_t ignored;
    CHAT_CALL(qa_bot_runtime_character_bounded_integer(b->runtime,s->character,BOT_C_CHAT_CPM,1,4000,&ignored,e));
    *out=2.0f;return true;
}
typedef enum test_variables {
    TEST_GAME,TEST_START,TEST_END,TEST_ONE,TEST_DEATH_WEAPON,TEST_HIT,TEST_RANDOM
} test_variables;
static bool test_emit(qa_bots *b,bot_ai_state *s,const char *type,test_variables kind,
                       const char *name,const char *weapon,qa_error *e) {
    size_t count=qa_bot_chat_initial_count(qa_bot_runtime_chat(b->runtime,s->chat),type);
    if(!live(b,s)) return true;
    for(size_t i=0;i<count;++i) {
        char own[32],opponent[32],first[32],last[32],map[128];const char *random_name;
        const char *values[8]={0};
        switch(kind) {
        case TEST_GAME:
            CHAT_CALL(own_name(b,s,own,e));CHAT_CALL(opponent_name(b,s,opponent,e));CHAT_CALL(map_title(b,map,e));
            values[0]=own;values[1]=opponent;values[2]=invalid_variable;values[3]=invalid_variable;values[4]=map;break;
        case TEST_START:CHAT_CALL(own_name(b,s,own,e));values[0]=own;break;
        case TEST_END:
            CHAT_CALL(own_name(b,s,own,e));CHAT_CALL(opponent_name(b,s,opponent,e));
            CHAT_CALL(ranked_name(b,s,true,first,e));CHAT_CALL(ranked_name(b,s,false,last,e));CHAT_CALL(map_title(b,map,e));
            values[0]=own;values[1]=opponent;values[2]=first;values[3]=last;values[4]=map;break;
        case TEST_ONE:values[0]=name;break;
        case TEST_DEATH_WEAPON:values[0]=name;values[1]=weapon_name(b->services.team_arena,bot_ai_bot_death_type(s));break;
        case TEST_HIT:values[0]=name;values[1]=weapon;break;
        case TEST_RANDOM:
            CHAT_CALL(opponent_name(b,s,opponent,e));CHAT_CALL(map_title(b,map,e));CHAT_CALL(random_weapon(b,s,&random_name,e));
            values[0]=opponent;values[1]=name;values[2]=invalid_variable;values[3]=invalid_variable;values[4]=map;values[5]=random_name;break;
        }
        CHAT_CALL(bot_ai_source_initial_chat(b,s,type,values,e));
        CHAT_CALL(qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),0,QA_BOT_CHAT_ALL,e));
    }
    return true;
}
bool bot_ai_source_chat_test(qa_bots *b,bot_ai_state *s,qa_error *e) {
    static const char *const game[]={"game_enter","game_exit"};
    static const char *const end[]={"level_end_victory","level_end_lose","level_end"};
    static const char *const death_one[]={"death_drown","death_slime","death_lava","death_cratered","death_suicide","death_telefrag"};
    static const char *const death_weapon[]={"death_gauntlet","death_rail","death_bfg","death_insult","death_praise"};
    static const char *const kill[]={"kill_gauntlet","kill_rail","kill_telefrag","kill_insult","kill_praise","enemy_suicide"};
    static const char *const hit[]={"hit_talking","hit_nodeath","hit_nokill"};
    static const char *const random[]={"random_misc","random_insult"};
    for(size_t i=0;i<2;++i) CHAT_CALL(test_emit(b,s,game[i],TEST_GAME,NULL,NULL,e));
    CHAT_CALL(test_emit(b,s,"level_start",TEST_START,NULL,NULL,e));
    for(size_t i=0;i<3;++i) CHAT_CALL(test_emit(b,s,end[i],TEST_END,NULL,NULL,e));
    char name[32];CHAT_CALL(bot_ai_easy_name(b,bot_ai_last_killed_by(s),name,sizeof(name),e));
    for(size_t i=0;i<6;++i) CHAT_CALL(test_emit(b,s,death_one[i],TEST_ONE,name,NULL,e));
    for(size_t i=0;i<5;++i) CHAT_CALL(test_emit(b,s,death_weapon[i],TEST_DEATH_WEAPON,name,NULL,e));
    CHAT_CALL(bot_ai_easy_name(b,bot_ai_last_killed_player(s),name,sizeof(name),e));
    for(size_t i=0;i<6;++i) CHAT_CALL(test_emit(b,s,kill[i],TEST_ONE,name,NULL,e));
    qa_bot_source_player_state current;CHAT_CALL(own_player(b,s,&current,e));
    CHAT_CALL(bot_ai_client_name(b,current.last_hurt_client,name,sizeof(name),true,e));
    const char *weapon=weapon_name(b->services.team_arena,current.last_hurt_client);
    for(size_t i=0;i<3;++i) CHAT_CALL(test_emit(b,s,hit[i],TEST_HIT,name,weapon,e));
    int32_t self;CHAT_CALL(bot_ai_source_client(b,s,&self,e));
    if(bot_ai_last_killed_player(s)==self) CHAT_CALL(opponent_name(b,s,name,e));
    else CHAT_CALL(bot_ai_easy_name(b,bot_ai_last_killed_player(s),name,sizeof(name),e));
    for(size_t i=0;i<2;++i) CHAT_CALL(test_emit(b,s,random[i],TEST_RANDOM,name,NULL,e));
    return true;
}
