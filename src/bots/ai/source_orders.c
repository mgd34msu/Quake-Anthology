/* Source ai_cmd.c text orders over the actual botlib and GAME services. */
#include "internal.h"
#include "source_team_state.h"
#include "source_timers.h"
#include "source_orders.h"
#include "source_storage.h"
#include "qa/network_q3.h"
#include <stdio.h>

enum {
    MSG_HELP=3, MSG_ACCOMPANY=4, MSG_DEFEND=5, MSG_RUSH=6, MSG_FLAG=7,
    MSG_START_LEADER=8, MSG_STOP_LEADER=9, MSG_WHO_LEADER=10, MSG_WAIT=11,
    MSG_DOING=12, MSG_JOIN=13, MSG_LEAVE=14, MSG_FORMATION=15,
    MSG_POSITION=16, MSG_SPACE=17, MSG_DO_FORMATION=18, MSG_DISMISS=19,
    MSG_CAMP=20, MSG_CHECKPOINT=21, MSG_PATROL=22, MSG_LEAD=23,
    MSG_ITEM=24, MSG_KILL=25, MSG_WHERE=26, MSG_RETURN=27,
    MSG_MY_COMMAND=28, MSG_TEAM=29, MSG_PREFERENCE=30, MSG_ATTACK=31,
    MSG_HARVEST=32, MSG_SUICIDE=33, MSG_ME=100, MSG_EVERYONE=101,
    MSG_NAMES=102, MSG_MINUTES=105, MSG_SECONDS=106, MSG_FOREVER=107,
    MSG_LONG_TIME=108, MSG_WHILE=109, MSG_TELL=202, MSG_CTF=300,
    MSG_NEW_LEADER=1, MSG_ENTER=2
};
enum {
    MATCH_NEAR_ITEM=1, MATCH_ADDRESSED=2, MATCH_FEET=8, MATCH_TIME=16,
    MATCH_HERE=32, MATCH_THERE=64, MATCH_I=128, MATCH_MORE=256,
    MATCH_BACK=512, MATCH_REVERSE=1024, MATCH_SOMEONE=2048,
    MATCH_GOT_FLAG=4096, MATCH_CAPTURED_FLAG=8192, MATCH_RETURNED_FLAG=16384
};
enum { VAR_NAME=0, VAR_MESSAGE=2, VAR_ITEM=3, VAR_TEAMMATE=4, VAR_AREA=5, VAR_TIME=6 };

static bool alive(qa_bots *b,bot_ai_state *s) {
    return !s->retired && bot_ai_live(b,s->view.actor);
}
static unsigned char fold(unsigned char c) {
    return c>='a' && c<='z'?(unsigned char)(c-'a'+'A'):c;
}
static bool same(const char *a,const char *b) {
    while(*a && *b) if(fold((unsigned char)*a++)!=fold((unsigned char)*b++)) return false;
    return !*a && !*b;
}
static void copy_name(char *out,size_t size,const char *in) {
    size_t length=strlen(in);if(length>=size) length=size-1;
    memcpy(out,in,length);out[length]=0;
}
static int32_t integer(const char *text) {
    while(*text && (int8_t)(unsigned char)*text<=32) ++text;
    bool negative=*text=='-';if(*text=='-' || *text=='+') ++text;
    uint32_t value=0;
    while(*text>='0' && *text<='9') value=value*10+(uint32_t)(*text++-'0');
    if(negative) value=0-value;
    int32_t result;memcpy(&result,&value,sizeof(result));return result;
}
typedef struct source_number {const char *text;size_t length,offset;} source_number;
static int number_byte(const source_number *n) {
    return n->offset<n->length?(int8_t)(unsigned char)n->text[n->offset]:0;
}
static bool number_take(source_number *n,int *out,qa_error *e) {
    if(n->offset>n->length) return bot_ai_fail(e,"Source bot numeric scan reads beyond its byte string");
    *out=number_byte(n);++n->offset;return true;
}
static bool number_float(source_number *n,bool scan,float *out,qa_error *e) {
    if(n->offset>n->length) return bot_ai_fail(e,"Source bot numeric cursor is outside its byte string");
    while(number_byte(n) && number_byte(n)<=32) ++n->offset;
    if(!number_byte(n)) {*out=0;return true;}
    int sign=1,c=scan?'0':number_byte(n);
    if(number_byte(n)=='-' || number_byte(n)=='+') {
        sign=number_byte(n)=='-'?-1:1;++n->offset;
    }
    if(!scan) c=number_byte(n);
    float value=0;
    if(number_byte(n)!='.') {
        for(;;) {
            if(!number_take(n,&c,e)) return false;
            if(c<'0' || c>'9') break;
            volatile float product=value*10.0f;value=product+(float)(c-'0');
        }
    } else if(!scan) ++n->offset;
    if(c=='.') {
        float fraction=.1f;
        for(;;) {
            if(!number_take(n,&c,e)) return false;
            if(c<'0' || c>'9') break;
            volatile float product=(float)(c-'0')*fraction;value=value+product;
            fraction=fraction*.1f;
        }
    }
    *out=value*(float)sign;return true;
}
static bool scalar(const char *text,float *out,qa_error *e) {
    source_number n={.text=text,.length=strlen(text)};
    return number_float(&n,false,out,e);
}
static bool coordinate_text(float value,char text[32],qa_error *e) {
    if(!isfinite(value) || fabs((double)value)>2147483647.0)
        return bot_ai_fail(e,"Source checkpoint format has an undefined integer conversion");
    int32_t magnitude=(int32_t)fabsf(value);
    snprintf(text,32,"%s%d",value<0?"-":"",magnitude);return true;
}
static bool variable(const qa_bot_chat_match *m,uint32_t index,char text[256],qa_error *e) {
    return qa_bot_chat_match_variable(m,index,text,256,e);
}
static bool match_text(qa_bots *b,const char *text,uint32_t context,
                        qa_bot_chat_match *match,bool *found,qa_error *e) {
    return qa_bot_chat_find_match(qa_bot_runtime_chat_system(b->runtime),text,context,match,found,e);
}
static bool config(qa_bots *b,int32_t client,char text[1024],qa_error *e) {
    if(!b->services.configstring) return bot_ai_fail(e,"Source bot orders require the actual GAME configstrings");
    return b->services.configstring(b->services.context,544+(uint32_t)client,text,1024,e);
}
static int32_t maximum(qa_bots *b,int32_t *cache) {
    if(!*cache) {
        qa_cvars *vars=b->services.configuration?b->services.configuration(b->services.context):NULL;
        const qa_cvar_view *view=vars?qa_cvars_find(vars,"sv_maxclients"):NULL;
        *cache=view?view->integer:0;
    }
    return *cache<64?*cache:64;
}
bool bot_ai_source_team(qa_bots *b,int32_t client,int32_t *out,qa_error *e) {
    if(client<0 || client>=64) {*out=0;return true;}
    char information[1024],team[1024];
    if(!config(b,client,information,e) || !qa_q3_info_value(information,"t",team,sizeof(team),e)) return false;
    *out=integer(team);return true;
}
bool bot_ai_source_same_team(qa_bots *b,bot_ai_state *s,int32_t client,bool *out,qa_error *e) {
    int32_t self,own,other;*out=false;
    if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(self<0 || self>=64 || client<0 || client>=64 || b->source_goals.game_type<3) return true;
    if(!bot_ai_source_team(b,self,&own,e) || !bot_ai_source_team(b,client,&other,e)) return false;
    *out=own==other;return true;
}
static bool name_lookup(qa_bots *b,bot_ai_state *s,const char *name,bool teammate,
                         int32_t *out,qa_error *e) {
    int32_t *cache=teammate?&b->source_orders.team_name_maxclients:&b->source_orders.client_name_maxclients;
    int32_t count=maximum(b,cache);*out=-1;
    for(int32_t i=0;i<count;++i) {
        bool same_team;
        if(teammate && !bot_ai_source_same_team(b,s,i,&same_team,e)) return false;
        if(teammate && !alive(b,s)) return true;
        if(teammate && !same_team) continue;
        char information[1024],clean[1024],value[1024];
        if(!config(b,i,information,e)) return false;
        size_t at=0;
        for(size_t j=0;information[j];++j) {
            unsigned char c=(unsigned char)information[j];
            if(c=='^' && information[j+1] && information[j+1]!='^') {++j;continue;}
            if(c>=32 && c<=126) clean[at++]=(char)c;
        }
        clean[at]=0;
        if(!qa_q3_info_value(clean,"n",value,sizeof(value),e)) return false;
        if(same(value,name)) {*out=i;return true;}
    }
    return true;
}
bool bot_ai_source_client_from_name(qa_bots *b,const char *name,int32_t *out,qa_error *e) {
    return name_lookup(b,NULL,name,false,out,e);
}
static bool find_name(qa_bots *b,bot_ai_state *s,const char *name,bool enemy,
                       int32_t *out,qa_error *e) {
    int32_t *cache=enemy?&b->source_orders.find_enemy_maxclients:&b->source_orders.find_client_maxclients;
    int32_t count=maximum(b,cache);*out=-1;
    for(unsigned pass=0;pass<2;++pass) for(int32_t i=0;i<count;++i) {
        bool same_team;
        if(enemy && !bot_ai_source_same_team(b,s,i,&same_team,e)) return false;
        if(enemy && same_team) continue;
        char client_name[1024];
        if(!bot_ai_client_name(b,i,client_name,sizeof(client_name),true,e)) return false;
        if((pass==0 && same(client_name,name)) ||
           (pass==1 && qa_bot_chat_contains(client_name,name,false)>=0)) {*out=i;return true;}
    }
    return true;
}
static bool addressed(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,
                       bool *out,qa_error *e) {
    char name[256];int32_t client;*out=false;
    if(!variable(m,VAR_NAME,name,e) || !name_lookup(b,s,name,true,&client,e)) return false;
    if(client<0 || !alive(b,s)) return true;
    if(m->subtype&MATCH_ADDRESSED) {
        char text[256],own_name[128];
        int32_t self;if(!bot_ai_source_client(b,s,&self,e) ||
            !bot_ai_client_name(b,self,own_name,sizeof(own_name),true,e) ||
            !variable(m,VAR_MESSAGE,text,e)) return false;
        for(;;) {
            qa_bot_chat_match addressee;bool found;
            if(!match_text(b,text,32,&addressee,&found,e)) return false;
            if(!found) return true;
            if(addressee.type==MSG_EVERYONE) {*out=true;return true;}
            char target[256];if(!variable(&addressee,VAR_TEAMMATE,target,e)) return false;
            if(*target && (qa_bot_chat_contains(own_name,target,false)>=0 ||
                qa_bot_chat_contains(s->source_order.subteam,target,false)>=0)) {*out=true;return true;}
            if(addressee.type!=MSG_NAMES) return true;
            if(!variable(&addressee,VAR_TIME,text,e)) return false;
        }
    }
    qa_bot_chat_match tell;bool found;
    if(!match_text(b,m->text,128,&tell,&found,e)) return false;
    if(!found || tell.type!=MSG_TELL) {
        int32_t count=0,limit=maximum(b,&b->source_orders.same_team_maxclients);
        for(int32_t i=0;i<limit;++i) {
            char information[1024];bool same_team;
            if(!config(b,i,information,e)) return false;
            if(!*information) continue;
            if(!bot_ai_source_same_team(b,s,i+1,&same_team,e)) return false;
            if(same_team) ++count;
        }
        float random;if(!bot_ai_random(b,&random,e)) return false;
        float chance=1.0f/(float)(count-1);
        if(random>chance) return true;
    }
    *out=true;return true;
}
bool bot_ai_source_synonym_context(qa_bots *b,bot_ai_state *s,uint32_t *out,qa_error *e) {
    int32_t self,team;
    if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_source_team(b,self,&team,e)) return false;
    uint32_t context=1027;
    if(b->source_goals.game_type==4 || (s->team_arena && b->source_goals.game_type==5)) context|=team==1?4u:8u;
    else if(s->team_arena && b->source_goals.game_type==6) context|=team==1?32u:64u;
    else if(s->team_arena && b->source_goals.game_type==7) context|=team==1?128u:256u;
    *out=context;return true;
}
bool bot_ai_source_initial_chat(qa_bots *b,bot_ai_state *s,const char *type,
                                const char *const arguments[8],qa_error *e) {
    if(!alive(b,s)) return true;
    uint32_t context;if(!bot_ai_source_synonym_context(b,s,&context,e)) return false;
    const char *values[8]={NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    for(size_t i=0;i<8 && arguments[i];++i) values[i]=arguments[i];
    bool found;
    return qa_bot_chat_initial(qa_bot_runtime_chat(b->runtime,s->chat),type,context,values,b->time,&found,e);
}
static bool send_chat(qa_bots *b,bot_ai_state *s,const char *type,const char *first,
                       const char *second,int32_t recipient,qa_bot_chat_destination destination,qa_error *e) {
    const char *arguments[8]={first,second,NULL,NULL,NULL,NULL,NULL,NULL};
    if(!bot_ai_source_initial_chat(b,s,type,arguments,e)) return false;
    return !alive(b,s) || qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),recipient,destination,e);
}
bool bot_ai_source_voice(qa_bots *b,bot_ai_state *s,int32_t recipient,const char *voice,
                         bool only,qa_error *e) {
    if(!s->team_arena || !alive(b,s)) return true;
    char command[256];
    if(recipient==-1) snprintf(command,sizeof(command),"%s %s",only?"vosay_team":"vsay_team",voice);
    else snprintf(command,sizeof(command),"%s %d %s",only?"votell":"vtell",recipient,voice);
    return !alive(b,s) || qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,QA_BOT_COMMAND,0,command,e);
}
static bool say(qa_bots *b,bot_ai_state *s,const char *text,qa_error *e) {
    return !alive(b,s) || qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,QA_BOT_SAY_TEAM,0,text,e);
}
static bool action(qa_bots *b,bot_ai_state *s,uint32_t flag,qa_error *e) {
    return !alive(b,s) || qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,flag,e);
}
bool bot_ai_source_locate(qa_bots *b,bot_ai_state *s,int32_t client,qa_bot_goal *goal,qa_error *e) {
    goal->entity=-1;qa_bot_entity_info entity;bool found;
    if(!qa_bot_runtime_entity(b->runtime,client,&entity,&found,e)) return false;
    if(!found || !entity.valid || !alive(b,s)) return true;
    uint32_t area;if(!bot_ai_point_area(b,s,entity.state.origin,&area,e)) return false;
    if(!area || !alive(b,s)) return true;
    goal->entity=client;goal->area=(int32_t)area;goal->origin=entity.state.origin;
    goal->mins=qa_v3(-8,-8,-8);goal->maxs=qa_v3(8,8,8);return true;
}
void bot_ai_source_orders_init(bot_source_orders_state *state) {
    memset(state,0,sizeof(*state));state->free_point=-1;
    for(int32_t i=0;i<BOT_SOURCE_WAYPOINTS;++i) {
        state->points[i].next=state->free_point;state->points[i].prev=-1;state->free_point=i;
    }
}
void bot_ai_source_order_init(bot_source_order_state *state) {
    memset(state,0,sizeof(*state));state->checkpoints=-1;state->patrol_points=-1;state->current_patrol_point=-1;
}
static void free_points(qa_bots *b,int32_t first) {
    while(first>=0) {
        bot_source_waypoint *point=&b->source_orders.points[first];int32_t next=point->next;
        point->next=b->source_orders.free_point;b->source_orders.free_point=first;first=next;
    }
}
void bot_ai_source_order_clear(qa_bots *b,bot_ai_state *s) {
    free_points(b,s->source_order.checkpoints);free_points(b,s->source_order.patrol_points);
    bot_ai_source_order_init(&s->source_order);
}
static int32_t find_point(qa_bots *b,int32_t first,const char *name) {
    for(int32_t at=first;at>=0;at=b->source_orders.points[at].next)
        if(same(b->source_orders.points[at].name,name)) return at;
    return -1;
}
static bool create_point(qa_bots *b,const char *name,qa_vec3 origin,int32_t area,
                          int32_t *out,qa_error *e) {
    int32_t index=b->source_orders.free_point;
    *out=index;
    if(index<0) {
        return bot_ai_source_print(b,"^3Warning: BotCreateWayPoint: Out of waypoints\n",e);
    }
    bot_source_waypoint *point=&b->source_orders.points[index];b->source_orders.free_point=point->next;
    copy_name(point->name,sizeof(point->name),name);
    point->goal.origin=origin;point->goal.area=area;
    point->goal.mins=qa_v3(-8,-8,-8);point->goal.maxs=qa_v3(8,8,8);
    point->next=-1;point->prev=-1;return true;
}
bool bot_ai_source_message_goal(qa_bots *b,bot_ai_state *s,const char *name,
                                qa_bot_goal *goal,bool *found,qa_error *e) {
    *found=false;if(!*name) return true;
    int32_t index=-1;bool item;
    do {
        if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),index,name,goal,&item,e)) return false;
        if(!item) break;
        index=goal->number;
        if(index>0 && !(goal->flags&QA_BOT_GOAL_DROPPED)) {*found=true;return true;}
    } while(index>0);
    int32_t point=find_point(b,s->source_order.checkpoints,name);
    if(point>=0) {*goal=b->source_orders.points[point].goal;*found=true;}
    return true;
}
static bool deadline(qa_bots *b,const qa_bot_chat_match *m,float *out,qa_error *e) {
    *out=0;if(!(m->subtype&MATCH_TIME)) return true;
    char text[256];qa_bot_chat_match time;bool found;
    if(!variable(m,VAR_TIME,text,e) || !match_text(b,text,8,&time,&found,e)) return false;
    if(!found) return true;
    float duration=0;
    switch(time.type) {
    case MSG_FOREVER:duration=99999999.0f;break;
    case MSG_WHILE:duration=600;break;
    case MSG_LONG_TIME:duration=1800;break;
    default:
        if(!variable(&time,VAR_TIME,text,e)) return false;
        if(time.type==MSG_MINUTES || time.type==MSG_SECONDS) {
            if(!scalar(text,&duration,e)) return false;
            if(time.type==MSG_MINUTES) duration=duration*60.0f;
        }
        break;
    }
    if(duration>0) *out=b->time+duration;
    return true;
}
static bool ordered(qa_bots *b,bot_ai_state *s,int32_t client,qa_error *e) {
    if(!alive(b,s)) return true;
    bot_ai_decisionmaker_set(s,client);bot_ai_ordered_set(s,true);bot_ai_order_time_set(s,b->time);
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(alive(b,s)) {volatile float delay=2.0f*random;bot_ai_team_message_time_set(s,b->time+delay);}
    return true;
}
static bool requester(qa_bots *b,const qa_bot_chat_match *m,bool find,int32_t *out,qa_error *e) {
    char name[256];if(!variable(m,VAR_NAME,name,e)) return false;
    return find?find_name(b,NULL,name,false,out,e):bot_ai_source_client_from_name(b,name,out,e);
}
static bool requester_chat(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,
                            const char *type,const char *first,const char *second,
                            qa_bot_chat_destination destination,qa_error *e) {
    const char *arguments[8]={first,second,NULL,NULL,NULL,NULL,NULL,NULL};
    if(!bot_ai_source_initial_chat(b,s,type,arguments,e)) return false;
    if(!alive(b,s)) return true;
    int32_t client;if(!requester(b,m,false,&client,e)) return false;
    return !alive(b,s) || qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),client,destination,e);
}
static bool order_allowed(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,bool *out,qa_error *e) {
    *out=false;if(b->source_goals.game_type<3) return true;
    return addressed(b,s,m,out,e);
}
static bool finish_order(qa_bots *b,bot_ai_state *s,bool remember,qa_error *e) {
    if(!alive(b,s)) return true;
    if(!bot_ai_team_status(b,s,e)) return false;
    return !remember || !alive(b,s) || bot_ai_remember_order(b,s,e);
}
static bool help_accompany(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char teammate[256],name[256];qa_bot_chat_match teammate_match;bool found,other;
    int32_t client,self;bool have_name=false;
    if(!variable(m,VAR_TEAMMATE,teammate,e) ||
       !match_text(b,teammate,16,&teammate_match,&found,e) ||
       !bot_ai_source_client(b,s,&self,e)) return false;
    if(found && teammate_match.type==MSG_ME) {
        if(!variable(m,VAR_NAME,name,e) || !bot_ai_source_client_from_name(b,name,&client,e)) return false;
        have_name=true;other=false;
    } else {
        if(!find_name(b,s,teammate,false,&client,e)) return false;
        if(client==self) other=false;
        else {
            bool same_team;if(!bot_ai_source_same_team(b,s,client,&same_team,e)) return false;
            if(!same_team) return true;
            other=true;
        }
    }
    if(!alive(b,s)) return true;
    if(client<0) {
        if(!have_name) return bot_ai_fail(e,"Source HelpAccompany reads an uninitialized netname");
        return requester_chat(b,s,m,"whois",other?teammate:name,NULL,QA_BOT_CHAT_TELL,e);
    }
    if(client==self) return true;
    if(!bot_ai_source_locate(b,s,client,&s->team_goal,e)) return false;
    if(s->team_goal.entity<0 && (m->subtype&MATCH_NEAR_ITEM)) {
        char item[256];if(!variable(m,VAR_ITEM,item,e) ||
            !bot_ai_source_message_goal(b,s,item,&s->team_goal,&found,e)) return false;
        if(!found) return true;
    }
    if(s->team_goal.entity<0) {
        if(!other && !have_name) return bot_ai_fail(e,"Source HelpAccompany reads an uninitialized netname");
        const char *arguments[8]={other?teammate:name,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
        if(!bot_ai_source_initial_chat(b,s,other?"whereis":"whereareyou",arguments,e)) return false;
        if(!have_name) return bot_ai_fail(e,"Source HelpAccompany reads an uninitialized reply recipient");
        if(!alive(b,s)) return true;
        int32_t recipient;if(!bot_ai_source_client_from_name(b,name,&recipient,e)) return false;
        return qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),recipient,QA_BOT_CHAT_TEAM,e);
    }
    bot_ai_teammate_set(s,client);
    if(!requester(b,m,false,&client,e)) return false;
    bot_ai_decisionmaker_set(s,client);bot_ai_ordered_set(s,true);bot_ai_order_time_set(s,b->time);bot_ai_teammate_visible_time_set(s,b->time);
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    volatile float delay=2.0f*random;bot_ai_team_message_time_set(s,b->time+delay);
    float deadline_value;if(!deadline(b,m,&deadline_value,e)) return false;
    bot_ai_team_goal_time_set(s,deadline_value);
    if(m->type==MSG_HELP) {
        bot_ai_long_term_goal_set(s,BOT_LTG_TEAM_HELP);
        if(!bot_ai_team_goal_time(s)) bot_ai_team_goal_time_set(s,b->time+60.0f);
        return true;
    }
    bot_ai_long_term_goal_set(s,BOT_LTG_TEAM_ACCOMPANY);
    if(!bot_ai_team_goal_time(s)) bot_ai_team_goal_time_set(s,b->time+600.0f);
    bot_ai_formation_distance_set(s,112);bot_ai_arrive_time_set(s,0);return finish_order(b,s,true,e);
}
static bool named_goal_order(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,bool item,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char name[256];bool found;
    if(!variable(m,item?VAR_ITEM:VAR_AREA,name,e) ||
       !bot_ai_source_message_goal(b,s,name,&s->team_goal,&found,e)) return false;
    if(!found || !alive(b,s)) return true;
    int32_t client;
    if(item) {
        if(!variable(m,VAR_NAME,name,e) || !name_lookup(b,s,name,true,&client,e)) return false;
    } else if(!requester(b,m,false,&client,e)) return false;
    if(!ordered(b,s,client,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,item?BOT_LTG_GET_ITEM:BOT_LTG_DEFEND);
    if(item) bot_ai_team_goal_time_set(s,b->time+60.0f);
    else {
        float deadline_value;if(!deadline(b,m,&deadline_value,e)) return false;
    bot_ai_team_goal_time_set(s,deadline_value);
        if(!bot_ai_team_goal_time(s)) bot_ai_team_goal_time_set(s,b->time+600.0f);
        bot_ai_defend_away_time_set(s,0);
    }
    return finish_order(b,s,!item,e);
}
static bool camp(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char name[256],area[256];int32_t client,self;
    if(!variable(m,VAR_NAME,name,e) || !find_name(b,s,name,false,&client,e) ||
       !bot_ai_source_client(b,s,&self,e)) return false;
    if(client<0) return send_chat(b,s,"whois",name,NULL,self,QA_BOT_CHAT_TEAM,e);
    if(!variable(m,VAR_AREA,area,e)) return false;
    if(m->subtype&MATCH_THERE) {
        s->team_goal.entity=s->view.entity;s->team_goal.area=(int32_t)s->area;
        s->team_goal.origin=s->player.origin;s->team_goal.mins=qa_v3(-8,-8,-8);s->team_goal.maxs=qa_v3(8,8,8);
    } else if(m->subtype&MATCH_HERE) {
        if(client==self) return true;
        if(!bot_ai_source_locate(b,s,client,&s->team_goal,e)) return false;
        if(s->team_goal.entity<0) {
            return requester_chat(b,s,m,"whereareyou",name,NULL,QA_BOT_CHAT_TELL,e);
        }
    } else {
        bool found;if(!bot_ai_source_message_goal(b,s,area,&s->team_goal,&found,e)) return false;
        if(!found) return true;
    }
    if(!ordered(b,s,client,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,BOT_LTG_CAMP_ORDER);
    float deadline_value;if(!deadline(b,m,&deadline_value,e)) return false;
    bot_ai_team_goal_time_set(s,deadline_value);
    if(!bot_ai_team_goal_time(s)) bot_ai_team_goal_time_set(s,b->time+600.0f);
    bot_ai_arrive_time_set(s,0);return finish_order(b,s,true,e);
}
static bool patrol_points(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,bool *out,qa_error *e) {
    char text[256];if(!variable(m,VAR_AREA,text,e)) return false;
    int32_t points=-1,tail=-1,flags=0;qa_bot_goal goal={0};*out=false;
    for(;;) {
        qa_bot_chat_match area;bool found;
        if(!match_text(b,text,64,&area,&found,e)) {free_points(b,points);return false;}
        if(!found) {
            bool ok=say(b,s,"what do you say?",e);
            free_points(b,points);s->source_order.patrol_points=-1;return ok;
        }
        char name[256];
        if(!variable(&area,VAR_AREA,name,e) || !bot_ai_source_message_goal(b,s,name,&goal,&found,e)) {
            free_points(b,points);return false;
        }
        if(!found) {free_points(b,points);s->source_order.patrol_points=-1;return true;}
        int32_t point;if(!create_point(b,name,goal.origin,goal.area,&point,e)) {
            free_points(b,points);return false;
        }
        if(point<0) break;
        b->source_orders.points[point].prev=tail;
        if(tail>=0) b->source_orders.points[tail].next=point;else points=point;
        tail=point;
        if(area.subtype&MATCH_BACK) {flags=1;break;}
        if(area.subtype&MATCH_REVERSE) {flags=2;break;}
        if(!(area.subtype&MATCH_MORE)) break;
        if(!variable(&area,VAR_TIME,text,e)) {free_points(b,points);return false;}
    }
    if(points<0 || b->source_orders.points[points].next<0) {
        bool ok=say(b,s,"I need more key points to patrol\n",e);free_points(b,points);return ok;
    }
    free_points(b,s->source_order.patrol_points);
    s->source_order.patrol_points=points;s->source_order.current_patrol_point=points;
    s->source_order.patrol_flags=flags;*out=true;return true;
}
static bool patrol(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    bool found;if(!patrol_points(b,s,m,&found,e)) return false;
    if(!found || !alive(b,s)) return true;
    int32_t client;if(!requester(b,m,true,&client,e) || !ordered(b,s,client,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,BOT_LTG_PATROL);
    float deadline_value;if(!deadline(b,m,&deadline_value,e)) return false;
    bot_ai_team_goal_time_set(s,deadline_value);
    if(!bot_ai_team_goal_time(s)) bot_ai_team_goal_time_set(s,b->time+600.0f);
    return finish_order(b,s,true,e);
}
static bool checkpoint(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    if(b->source_goals.game_type<3) return true;
    char text[256],name[256];int32_t client;
    if(!variable(m,VAR_AREA,text,e) || !requester(b,m,false,&client,e)) return false;
    source_number input={.text=text,.length=strlen(text)};qa_vec3 position;
    if(!number_float(&input,true,&position.x,e) || !number_float(&input,true,&position.y,e) ||
       !number_float(&input,true,&position.z,e)) return false;
    position.z=position.z+.5f;
    uint32_t area;if(!bot_ai_point_area(b,s,position,&area,e)) return false;
    if(!alive(b,s)) return true;
    bool allowed;
    if(!area) {
        if(!addressed(b,s,m,&allowed,e)) return false;
        return !allowed || send_chat(b,s,"checkpoint_invalid",NULL,NULL,client,QA_BOT_CHAT_TELL,e);
    }
    if(!variable(m,VAR_TIME,name,e)) return false;
    int32_t old=find_point(b,s->source_order.checkpoints,name);
    if(old>=0) {
        bot_source_waypoint *point=&b->source_orders.points[old];
        if(point->next>=0) b->source_orders.points[point->next].prev=point->prev;
        if(point->prev>=0) b->source_orders.points[point->prev].next=point->next;
        else s->source_order.checkpoints=point->next;
        point->inuse=false;
    }
    int32_t index;if(!create_point(b,name,position,(int32_t)area,&index,e)) return false;
    if(index<0) return bot_ai_fail(e,"Source CheckPoint exhausted its waypoint heap");
    bot_source_waypoint *point=&b->source_orders.points[index];point->next=s->source_order.checkpoints;
    if(point->next>=0) b->source_orders.points[point->next].prev=index;
    s->source_order.checkpoints=index;
    if(!addressed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char coordinates[256],x[32],y[32],z[32];
    if(!coordinate_text(point->goal.origin.x,x,e) || !coordinate_text(point->goal.origin.y,y,e) ||
       !coordinate_text(point->goal.origin.z,z,e)) return false;
    snprintf(coordinates,sizeof(coordinates),"%s %s %s",x,y,z);
    return send_chat(b,s,"checkpoint_confirm",point->name,coordinates,client,QA_BOT_CHAT_TELL,e);
}
static void leader_name(const bot_ai_state *s,char name[33]) {
    memcpy(name,s->team_leader_name,32);name[32]=0;
}
static bool self_is_leader(qa_bots *b,bot_ai_state *s,size_t size,bool *out,qa_error *e) {
    char own[256],leader[33];int32_t self;
    if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_client_name(b,self,own,size,true,e)) return false;
    leader_name(s,leader);*out=same(own,leader);return true;
}
static bool preference(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool leader;if(!self_is_leader(b,s,36,&leader,e)) return false;
    if(!leader || !alive(b,s)) return true;
    int32_t client;if(!requester(b,m,false,&client,e)) return false;
    if(client<0) return true;
    char name[36],easy[256];
    if(!bot_ai_client_name(b,client,name,sizeof(name),true,e)) return false;
    int32_t value=same(b->team_preferences[client].name,name)?b->team_preferences[client].preference:0;
    switch(m->subtype) {
    case 1:value=(value&~2)|1;break;
    case 2:value=(value&~1)|2;break;
    case 4:value&=~3;break;
    default:break;
    }
    b->team_preferences[client].preference=value;copy_name(b->team_preferences[client].name,36,name);
    if(!bot_ai_easy_name(b,client,easy,sizeof(easy),e) ||
       !send_chat(b,s,"keepinmind",easy,NULL,client,QA_BOT_CHAT_TELL,e) ||
       !bot_ai_source_voice(b,s,client,"yes",true,e)) return false;
    return action(b,s,QA_BOT_AFFIRMATIVE,e);
}
static bool subteam(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed || !alive(b,s)) return true;
    if(m->type==MSG_JOIN) {
        char team[256];if(!variable(m,VAR_TEAMMATE,team,e)) return false;
        size_t length=strlen(team);if(length>32) length=32;
        memcpy(s->source_order.subteam,team,length);
        if(length<32) memset(s->source_order.subteam+length,0,32-length);
        s->source_order.subteam[31]=0;
        return requester_chat(b,s,m,"joinedteam",team,NULL,QA_BOT_CHAT_TELL,e);
    }
    if(m->type==MSG_LEAVE) {
        if(*s->source_order.subteam &&
           !requester_chat(b,s,m,"leftteam",s->source_order.subteam,NULL,QA_BOT_CHAT_TELL,e)) return false;
        s->source_order.subteam[0]=0;return true;
    }
    int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    return send_chat(b,s,*s->source_order.subteam?"inteam":"noteam",
        *s->source_order.subteam?s->source_order.subteam:NULL,NULL,self,QA_BOT_CHAT_TEAM,e);
}
static bool formation_space(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char text[256];float value;
    if(!variable(m,VAR_AREA,text,e) || !scalar(text,&value,e)) return false;
    float space;
    if(m->subtype&MATCH_FEET) {volatile float feet=.3048f*32.0f;space=feet*value;}
    else space=32.0f*value;
    if(space<48 || space>500) space=100;
    if(alive(b,s)) bot_ai_formation_distance_set(s,space);
    return true;
}
static bool dismiss(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed || !alive(b,s)) return true;
    int32_t client;if(!requester(b,m,false,&client,e)) return false;
    bot_ai_decisionmaker_set(s,client);bot_ai_long_term_goal_set(s,BOT_LTG_NONE);bot_ai_lead_time_set(s,0);
    int32_t last_type=BOT_LTG_NONE;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,true,e)) return false;
    return send_chat(b,s,"dismissed",NULL,NULL,client,QA_BOT_CHAT_TELL,e);
}
static bool suicide(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    if(alive(b,s) && !qa_bot_actions_text(qa_bot_runtime_actions(b->runtime),(int32_t)s->view.client,QA_BOT_COMMAND,0,"kill",e)) return false;
    if(!alive(b,s)) return true;
    int32_t client;if(!requester(b,m,false,&client,e) ||
        !bot_ai_source_voice(b,s,client,"taunt",false,e)) return false;
    return action(b,s,QA_BOT_AFFIRMATIVE,e);
}
static bool doing(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!addressed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    const char *type="roaming",*argument=NULL;char text[256];
    switch(bot_ai_long_term_goal(s)) {
    case BOT_LTG_TEAM_HELP:case BOT_LTG_TEAM_ACCOMPANY:
        type=bot_ai_long_term_goal(s)==BOT_LTG_TEAM_HELP?"helping":"accompanying";
        if(!bot_ai_easy_name(b,bot_ai_teammate(s),text,sizeof(text),e)) return false;
        argument=text;break;
    case BOT_LTG_DEFEND:case BOT_LTG_GET_ITEM: {
        const char *name;
        type=bot_ai_long_term_goal(s)==BOT_LTG_DEFEND?"defending":"gettingitem";
        if(!qa_bot_goals_name_read(qa_bot_runtime_goals(b->runtime),s->team_goal.number,&name,e)) return false;
        copy_name(text,sizeof(text),name);
        argument=text;break;
    }
    case BOT_LTG_KILL:
        type="killing";
        if(!bot_ai_client_name(b,s->team_goal.entity,text,sizeof(text),true,e)) return false;
        argument=text;break;
    case BOT_LTG_CAMP:case BOT_LTG_CAMP_ORDER:type="camping";break;
    case BOT_LTG_PATROL:type="patrolling";break;
    case BOT_LTG_GET_FLAG:type="capturingflag";break;
    case BOT_LTG_RUSH_BASE:type="rushingbase";break;
    case BOT_LTG_RETURN_FLAG:type="returningflag";break;
    case BOT_LTG_ATTACK_BASE:if(s->team_arena) type="attackingenemybase";break;
    case BOT_LTG_HARVEST:if(s->team_arena) type="harvesting";break;
    default:break;
    }
    return requester_chat(b,s,m,type,argument,NULL,QA_BOT_CHAT_TELL,e);
}
static bool lead(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char name[256],teammate[256];bool have_name=false,other;
    int32_t client,self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(m->subtype&MATCH_SOMEONE) {
        if(!variable(m,VAR_TEAMMATE,teammate,e) || !find_name(b,s,teammate,false,&client,e)) return false;
        if(client==self) other=false;
        else {
            bool same_team;if(!bot_ai_source_same_team(b,s,client,&same_team,e)) return false;
            if(!same_team) return true;
            other=true;
        }
    } else {
        if(!variable(m,VAR_NAME,name,e) || !bot_ai_source_client_from_name(b,name,&client,e)) return false;
        have_name=true;other=false;
    }
    if(client<0) {
        if(!have_name) return bot_ai_fail(e,"Source LeadTheWay reads an uninitialized netname");
        return send_chat(b,s,"whois",name,NULL,self,QA_BOT_CHAT_TEAM,e);
    }
    if(!bot_ai_source_locate(b,s,client,&s->source_order.lead_goal,e)) return false;
    if(s->team_goal.entity<0) {
        if(!other && !have_name) return bot_ai_fail(e,"Source LeadTheWay reads an uninitialized teammate name");
        return send_chat(b,s,other?"whereis":"whereareyou",other?teammate:name,NULL,self,QA_BOT_CHAT_TEAM,e);
    }
    if(!alive(b,s)) return true;
    s->source_order.lead_teammate=client;bot_ai_lead_time_set(s,b->time+600.0f);s->source_order.lead_visible_time=0;
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(alive(b,s)) {volatile float delay=2.0f*random;s->source_order.lead_message_time=-(b->time+delay);}
    return true;
}
static bool kill(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    char name[256];int32_t client;
    if(!variable(m,VAR_TEAMMATE,name,e) || !find_name(b,s,name,true,&client,e)) return false;
    if(client<0) {
        return requester_chat(b,s,m,"whois",name,NULL,QA_BOT_CHAT_TELL,e);
    }
    if(!alive(b,s)) return true;
    s->team_goal.entity=client;
    float random;if(!bot_ai_random(b,&random,e)) return false;
    if(!alive(b,s)) return true;
    volatile float delay=2.0f*random;bot_ai_team_message_time_set(s,b->time+delay);
    bot_ai_long_term_goal_set(s,BOT_LTG_KILL);bot_ai_team_goal_time_set(s,b->time+180.0f);
    return finish_order(b,s,false,e);
}
static bool nearest_item(qa_bots *b,bot_ai_state *s,const char *name,qa_bot_goal *goal,float *out,qa_error *e) {
    *out=999999;int32_t index=-1;qa_bot_goal candidate={0};
    for(;;) {
        bool found;if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),index,name,&candidate,&found,e)) return false;
        if(!found) return true;
        index=candidate.number;char goal_name[64];const char *current_name;
        if(!qa_bot_goals_name_read(qa_bot_runtime_goals(b->runtime),candidate.number,&current_name,e)) return false;
        copy_name(goal_name,sizeof(goal_name),current_name);
        if(same(goal_name,name)) {
            float distance=qa_vec_length(qa_vec_sub(candidate.origin,s->player.origin));
            if(distance<*out) {
                qa_bot_navigation *navigation=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
                if(!navigation) return bot_ai_fail(e,"Source bot location requires its loaded AAS query owner");
                qa_trace_result trace;
                if(!qa_bot_navigation_trace(navigation,s->player.eye,candidate.origin,NULL,s->view.actor,0x10001,&trace,e)) return false;
                if(!alive(b,s)) return true;
                if(trace.fraction>=1) {*out=distance;*goal=candidate;}
            }
        }
        if(index<=0) return true;
    }
}
static bool where(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bool allowed;if(!order_allowed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    static const char *const names[]={"Shotgun","Grenade Launcher","Rocket Launcher","Plasmagun","Railgun",
        "Lightning Gun","BFG10K","Quad Damage","Regeneration","Battle Suit","Speed","Invisibility","Flight",
        "Armor","Heavy Armor","Red Flag","Blue Flag","Nailgun","Prox Launcher","Chaingun","Scout","Guard",
        "Doubler","Ammo Regen","Neutral Flag","Red Obelisk","Blue Obelisk","Neutral Obelisk"};
    const char *nearest=NULL;float best=999999;qa_bot_goal goal={0};size_t count=s->team_arena?28:17;
    for(size_t i=0;i<count;++i) {
        float distance;if(!nearest_item(b,s,names[i],&goal,&distance,e)) return false;
        if(!alive(b,s)) return true;
        if(distance<best) {best=distance;nearest=names[i];}
    }
    if(!nearest) return true;
    uint32_t red=0,blue=0;int32_t type=b->source_goals.game_type;
    bool flags=type==4 || (s->team_arena && type==5);
    bool obelisks=s->team_arena && (type==6 || type==7);
    if(flags || obelisks) {
        qa_bot_navigation *navigation=qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
        if(!navigation) return bot_ai_fail(e,"Source WhereAreYou requires loaded AAS routing");
        const qa_bot_goal *red_goal=flags?&b->source_goals.red_flag:&b->source_goals.red_obelisk;
        const qa_bot_goal *blue_goal=flags?&b->source_goals.blue_flag:&b->source_goals.blue_obelisk;
        qa_bot_nav_route_query query={.area=s->area,.origin=s->player.origin,.has_origin=true,
            .travel_flags=0x011c0fbe,.goal_area=(uint32_t)red_goal->area};qa_bot_nav_route route;
        if(!qa_bot_navigation_route(navigation,&query,&route,e)) return false;
        red=route.travel_time;query.goal_area=(uint32_t)blue_goal->area;
        if(!qa_bot_navigation_route(navigation,&query,&route,e)) return false;
        blue=route.travel_time;
    }
    float limit=(float)(((double)red+(double)blue)*(double).4f);
    return requester_chat(b,s,m,(float)red<limit || (float)blue<limit?"teamlocation":"location",nearest,
        (float)red<limit?"red":(float)blue<limit?"blue":NULL,QA_BOT_CHAT_TELL,e);
}
static bool get_flag(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bot_source_goals *goals=&b->source_goals;int32_t type=goals->game_type;
    if(type==4) {
        if(!goals->red_flag.area || !goals->blue_flag.area) return true;
    } else if(s->team_arena && type==5) {
        if(!goals->neutral_flag.area || !goals->red_flag.area || !goals->blue_flag.area) return true;
    } else return true;
    bool allowed;if(!addressed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    int32_t client;if(!requester(b,m,true,&client,e) || !ordered(b,s,client,e)) return false;
    if(!alive(b,s)) return true;
    bot_ai_long_term_goal_set(s,BOT_LTG_GET_FLAG);bot_ai_team_goal_time_set(s,b->time+600.0f);
    if(type==4) {
        int32_t self,team;if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_source_team(b,self,&team,e) ||
            !bot_ai_source_alternate_route(b,s,team==1?2:team==2?1:0,e)) return false;
    }
    return finish_order(b,s,true,e);
}
static bool objective_order(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bot_source_goals *goals=&b->source_goals;int32_t type=goals->game_type;
    if(m->type==MSG_ATTACK) {
        if(type==4) {
            if(!get_flag(b,s,m,e)) return false;
        } else if(s->team_arena && (type==5 || type==6 || type==7)) {
            if(!goals->red_obelisk.area || !goals->blue_obelisk.area) return true;
        } else return true;
    } else if(m->type==MSG_HARVEST) {
        if(!s->team_arena || type!=7 || !goals->neutral_obelisk.area ||
            !goals->red_obelisk.area || !goals->blue_obelisk.area) return true;
    } else if(m->type==MSG_RUSH) {
        if(type==4) {
            if(!goals->red_flag.area || !goals->blue_flag.area) return true;
        } else if(s->team_arena && (type==5 || type==7)) {
            if(!goals->red_obelisk.area || !goals->blue_obelisk.area) return true;
        } else return true;
    } else if(type!=4 && !(s->team_arena && type==5)) return true;
    if(!alive(b,s)) return true;
    bool allowed;if(!addressed(b,s,m,&allowed,e)) return false;
    if(!allowed) return true;
    int32_t client;if(!requester(b,m,true,&client,e) || !ordered(b,s,client,e)) return false;
    if(!alive(b,s)) return true;
    if(m->type==MSG_ATTACK) {
        bot_ai_long_term_goal_set(s,BOT_LTG_ATTACK_BASE);bot_ai_team_goal_time_set(s,b->time+600.0f);bot_ai_attack_away_time_set(s,0);
    } else if(m->type==MSG_HARVEST) {
        bot_ai_long_term_goal_set(s,BOT_LTG_HARVEST);bot_ai_team_goal_time_set(s,b->time+120.0f);bot_ai_harvest_away_time_set(s,0);
    } else {
        bot_ai_long_term_goal_set(s,m->type==MSG_RUSH?BOT_LTG_RUSH_BASE:BOT_LTG_RETURN_FLAG);
        bot_ai_team_goal_time_set(s,b->time+(m->type==MSG_RUSH?120.0f:180.0f));bot_ai_rush_base_away_time_set(s,0);
    }
    return finish_order(b,s,m->type==MSG_ATTACK || m->type==MSG_HARVEST,e);
}
static bool flag_message(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    bot_source_order_state *order=&s->source_order;
    if(b->source_goals.game_type==4) {
        char flag[128];if(!qa_bot_chat_match_variable(m,1,flag,sizeof(flag),e)) return false;
        if(m->subtype&MATCH_GOT_FLAG) {
            int32_t self,team;if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_source_team(b,self,&team,e)) return false;
            bool red=same(flag,"RED");if(red) order->red_flag_status=1;else order->blue_flag_status=1;
            if(team==(red?2:1)) {
                char name[36];if(!qa_bot_chat_match_variable(m,VAR_NAME,name,sizeof(name),e) ||
                   !bot_ai_source_client_from_name(b,name,&order->flag_carrier,e)) return false;
            }
            order->flag_status_changed=true;order->last_flag_capture_time=b->time;
        } else if(m->subtype&MATCH_CAPTURED_FLAG) {
            order->red_flag_status=0;order->blue_flag_status=0;order->flag_carrier=0;
            order->flag_status_changed=true;
        } else if(m->subtype&MATCH_RETURNED_FLAG) {
            if(same(flag,"RED")) order->red_flag_status=0;else order->blue_flag_status=0;
            order->flag_status_changed=true;
        }
    } else if(s->team_arena && b->source_goals.game_type==5 && (m->subtype&65535)) {
        char name[36];if(!qa_bot_chat_match_variable(m,VAR_NAME,name,sizeof(name),e) ||
           !bot_ai_source_client_from_name(b,name,&order->flag_carrier,e)) return false;
    }
    return true;
}
static bool leader_command(qa_bots *b,bot_ai_state *s,const qa_bot_chat_match *m,qa_error *e) {
    if(b->source_goals.game_type<3) return true;
    if(m->type==MSG_START_LEADER) {
        if(m->subtype&MATCH_I) {
            char name[256];if(!variable(m,VAR_NAME,name,e)) return false;
            size_t length=strlen(name);if(length>32) length=32;
            memcpy(s->team_leader_name,name,length);
            if(length<32) memset(s->team_leader_name+length,0,32-length);
            uint32_t bits;memcpy(&bits,&s->source_order.ask_team_leader_time,sizeof(bits));
            bits&=UINT32_C(0xffffff00);memcpy(&s->source_order.ask_team_leader_time,&bits,sizeof(bits));
        } else {
            char name[256];int32_t client;
            if(!variable(m,VAR_TEAMMATE,name,e) || !find_name(b,s,name,false,&client,e)) return false;
            if(client>=0 && !bot_ai_client_name(b,client,s->team_leader_name,sizeof(s->team_leader_name),true,e)) return false;
        }
        return true;
    }
    if(m->type==MSG_STOP_LEADER) {
        char name[256],actual[256],leader[33];int32_t client;
        if(!variable(m,m->subtype&MATCH_I?VAR_NAME:VAR_TEAMMATE,name,e) ||
            !find_name(b,s,name,false,&client,e)) return false;
        if(client<0) return true;
        if(!bot_ai_client_name(b,client,actual,sizeof(actual),true,e)) return false;
        leader_name(s,leader);
        if(same(leader,actual)) {s->team_leader_name[0]=0;b->not_leader[client]=true;}
        return true;
    }
    bool leader;if(!self_is_leader(b,s,256,&leader,e)) return false;
    return !leader || say(b,s,"I'm the team leader\n",e);
}
bool bot_ai_source_order_message(qa_bots *b,bot_ai_state *s,const char *message,bool *matched,qa_error *e) {
    qa_bot_chat_match m;
    if(!match_text(b,message,2|4|256,&m,matched,e)) return false;
    if(!*matched || !alive(b,s)) return true;
    switch(m.type) {
    case MSG_HELP:case MSG_ACCOMPANY:return help_accompany(b,s,&m,e);
    case MSG_DEFEND:return named_goal_order(b,s,&m,false,e);
    case MSG_CAMP:return camp(b,s,&m,e);
    case MSG_PATROL:return patrol(b,s,&m,e);
    case MSG_FLAG:return get_flag(b,s,&m,e);
    case MSG_ATTACK:case MSG_HARVEST:
        return s->team_arena?objective_order(b,s,&m,e):bot_ai_source_print(b,"unknown match type\n",e);
    case MSG_RUSH:case MSG_RETURN:return objective_order(b,s,&m,e);
    case MSG_PREFERENCE:return preference(b,s,&m,e);
    case MSG_CTF:return flag_message(b,s,&m,e);
    case MSG_ITEM:return named_goal_order(b,s,&m,true,e);
    case MSG_JOIN:case MSG_LEAVE:case MSG_TEAM:return subteam(b,s,&m,e);
    case MSG_CHECKPOINT:return checkpoint(b,s,&m,e);
    case MSG_FORMATION:case MSG_POSITION:return say(b,s,"the part of my brain to create formations has been damaged",e);
    case MSG_SPACE:return formation_space(b,s,&m,e);
    case MSG_DO_FORMATION:case MSG_WAIT:return true;
    case MSG_DISMISS:return dismiss(b,s,&m,e);
    case MSG_START_LEADER:case MSG_STOP_LEADER:case MSG_WHO_LEADER:return leader_command(b,s,&m,e);
    case MSG_DOING:return doing(b,s,&m,e);
    case MSG_MY_COMMAND: {
        bool leader;if(!self_is_leader(b,s,36,&leader,e)) return false;
        if(leader && alive(b,s)) s->source_order.force_orders=true;
        return true;
    }
    case MSG_WHERE:return where(b,s,&m,e);
    case MSG_LEAD:return lead(b,s,&m,e);
    case MSG_KILL:return kill(b,s,&m,e);
    case MSG_ENTER: {
        char name[36];int32_t client;
        if(!qa_bot_chat_match_variable(&m,VAR_NAME,name,sizeof(name),e) ||
           !find_name(b,s,name,false,&client,e)) return false;
        if(client>=0) b->not_leader[client]=false;
        return true;
    }
    case MSG_NEW_LEADER: {
        char name[36];int32_t client;bool same_team;
        if(!qa_bot_chat_match_variable(&m,VAR_NAME,name,sizeof(name),e) ||
           !find_name(b,s,name,false,&client,e) ||
           !bot_ai_source_same_team(b,s,client,&same_team,e)) return false;
        if(same_team && alive(b,s)) {
            size_t length=strlen(name);if(length>31) length=31;
            memcpy(s->team_leader_name,name,length);memset(s->team_leader_name+length,0,32-length);
        }
        return true;
    }
    case MSG_SUICIDE:return suicide(b,s,&m,e);
    default:return bot_ai_source_print(b,"unknown match type\n",e);
    }
}
