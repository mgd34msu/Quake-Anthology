#include "internal.h"

static bool clock_read(qa_bot_catalog *c,qa_bot_catalog_clock *clock,qa_error *e) {
    if(!c->services.clock(c->services.context,clock,e)) return false;
    return clock->max_clients>=0?true:bot_catalog_fail(e,QA_ERROR_FORMAT,"source bot catalogue has a negative client extent");
}
static bool floating(float value,unsigned digits,unsigned width,char *out,size_t capacity,qa_error *e) {
    if(!isfinite(value) || value>=2147483648.0f || value<=-2147483648.0f || digits>32)
        return bot_catalog_fail(e,QA_ERROR_FORMAT,"game format float is outside the source safe int-cast range");
    volatile float remaining=value<0?-value:value;
    char integer[32];snprintf(integer,sizeof(integer),"%s%d",value<0?"-":"",(int32_t)remaining);
    size_t n=strlen(integer),padding=width>n?width-n:0,total=padding+n+(digits?digits+1:0);
    if(total>=capacity) return bot_catalog_fail(e,QA_ERROR_FORMAT,"source float format exceeds its destination");
    memset(out,' ',padding);memcpy(out+padding,integer,n);n+=padding;
    if(digits) {
        out[n++]='.';
        for(unsigned i=0;i<digits;++i) {
            remaining=remaining-(float)(int32_t)remaining;
            remaining=remaining*10.0f;
            out[n++]=(char)('0'+(int32_t)remaining%10);
        }
    }
    out[n]=0;return true;
}
static bool fixed(float value,unsigned digits,char *out,size_t capacity,qa_error *e) {
    return floating(value,digits,0,out,capacity,e);
}
bool bot_catalog_spawn_list(qa_bot_catalog *c,const char *list,int32_t delay,qa_error *e) {
    if(!c->services.reset_podium(c->services.context,e)) return false;
    char value[128],skill_text[64];
    if(!bot_catalog_cvar(c,"g_spSkill",value,sizeof(value),NULL,e)) return false;
    float skill=bot_catalog_atof(value);
    if(skill<1) {if(!c->services.set_cvar(c->services.context,"g_spSkill","1",e)) return false;skill=1;}
    else if(skill>5) {if(!c->services.set_cvar(c->services.context,"g_spSkill","5",e)) return false;skill=5;}
    if(!fixed(skill,6,skill_text,sizeof(skill_text),e)) return false;
    size_t size=strlen(list);if(size>1023) size=1023;size_t position=0;
    while(position<size) {
        while(position<size && list[position]==' ') ++position;
        size_t start=position;
        while(position<size && list[position]!=' ') ++position;
        size_t length=position-start;if(position<size) ++position;
        char command[1200];int n=snprintf(command,sizeof(command),"addbot %.*s %s free %d\n",(int)length,list+start,skill_text,delay);
        if(n<0 || (size_t)n>=sizeof(command)) return bot_catalog_fail(e,QA_ERROR_FORMAT,"source addbot command exceeds its formatter storage");
        if(!c->services.insert_command(c->services.context,command,e)) return false;
        delay=bot_catalog_word((uint32_t)delay+1500u);
    }
    return true;
}
static bool queue_begin(qa_bot_catalog *c,int32_t client,int32_t delay,qa_error *e) {
    qa_bot_catalog_clock clock;if(!clock_read(c,&clock,e)) return false;
    for(size_t i=0;i<BOT_CATALOG_QUEUE;++i) if(c->queue[i].time==0) {
        c->queue[i].time=bot_catalog_word((uint32_t)clock.time+(uint32_t)delay);c->queue[i].client=client;return true;
    }
    return c->services.print(c->services.context,"^3Unable to delay spawn\n",e) &&
        c->services.begin(c->services.context,client,e);
}
static bool add_bot(qa_bot_catalog *c,const char *name,float skill,const char *requested_team,
    int32_t delay,const char *alternate_name,qa_error *e) {
    qa_buffer info={0};bool found;
    if(!bot_catalog_lookup(c,&c->bots,"name",name,&info,&found,e)) return false;
    if(!found) {
        char message[1200];snprintf(message,sizeof(message),"^1Error: Bot '%s' not defined\n",name);
        return c->services.print(c->services.context,message,e);
    }
    char userinfo[1024]={0},value[8192],model[8192],headmodel[8192],character[8192],team[1024],skill_text[64];
    bool okay=bot_catalog_info_value((char *)info.data,"funname",value,sizeof(value),e);
    if(okay && !*alternate_name && !*value) okay=bot_catalog_info_value((char *)info.data,"name",value,sizeof(value),e);
    if(okay) okay=bot_catalog_info_set(c,userinfo,"name",*alternate_name?alternate_name:value,e) &&
        bot_catalog_info_set(c,userinfo,"rate","25000",e) && bot_catalog_info_set(c,userinfo,"snaps","20",e) &&
        fixed(skill,2,skill_text,sizeof(skill_text),e) && bot_catalog_info_set(c,userinfo,"skill",skill_text,e);
    if(okay && skill>=1 && skill<4)
        okay=bot_catalog_info_set(c,userinfo,"handicap",skill<2?"50":skill<3?"70":"90",e);
    if(okay) okay=bot_catalog_info_value((char *)info.data,"model",model,sizeof(model),e);
    if(okay && !*model) strcpy(model,"visor/default");
    if(okay) okay=bot_catalog_info_set(c,userinfo,"model",model,e) && bot_catalog_info_set(c,userinfo,"team_model",model,e) &&
        bot_catalog_info_value((char *)info.data,"headmodel",headmodel,sizeof(headmodel),e);
    if(okay && !*headmodel) strcpy(headmodel,model);
    if(okay) okay=bot_catalog_info_set(c,userinfo,"headmodel",headmodel,e) &&
        bot_catalog_info_set(c,userinfo,"team_headmodel",headmodel,e);
    const struct {const char *key,*fallback;} fields[]={{"gender","male"},{"color1","4"},{"color2","5"}};
    for(size_t i=0;okay && i<3;++i) {
        okay=bot_catalog_info_value((char *)info.data,fields[i].key,value,sizeof(value),e) &&
            bot_catalog_info_set(c,userinfo,i?fields[i].key:"sex",*value?value:fields[i].fallback,e);
    }
    if(okay) okay=bot_catalog_info_value((char *)info.data,"aifile",character,sizeof(character),e);
    qa_buffer_free(&info);if(!okay) return false;
    if(!*character) return c->services.print(c->services.context,"^1Error: bot has no aifile specified\n",e);
    int32_t client;
    if(!c->services.allocate_client(c->services.context,&client,e)) return false;
    if(client==-1) return c->services.print(c->services.context,"^1Unable to add bot.  All player slots are in use.\n",e) &&
        c->services.print(c->services.context,"^1Start server with more 'open' slots (or check setting of sv_maxclients cvar).\n",e);
    size_t length=strlen(requested_team);if(length>=sizeof(team)) length=sizeof(team)-1;
    memcpy(team,requested_team,length);team[length]=0;
    if(!*team) {
        qa_bot_catalog_clock clock;int32_t chosen;
        if(!clock_read(c,&clock,e)) return false;
        if(clock.game_type>=3) {
            if(!c->services.choose_team(c->services.context,client,&chosen,e)) return false;
            strcpy(team,chosen==1?"red":"blue");
        } else strcpy(team,"red");
    }
    char padded[72];if(!floating(skill,2,5,padded,sizeof(padded),e)) return false;
    if(!bot_catalog_info_set(c,userinfo,"characterfile",character,e) ||
       !bot_catalog_info_set(c,userinfo,"skill",padded,e) || !bot_catalog_info_set(c,userinfo,"team",team,e) ||
       !c->services.activate(c->services.context,client,e) || !c->services.set_userinfo(c->services.context,client,userinfo,e)) return false;
    bool connected;
    if(!c->services.connect(c->services.context,client,true,true,&connected,e)) return false;
    if(!connected) return true;
    return delay?queue_begin(c,client,delay,e):c->services.begin(c->services.context,client,e);
}
static bool list_bots(qa_bot_catalog *c,qa_error *e) {
    if(!c->services.print(c->services.context,"^1name             model            aifile              funname\n",e)) return false;
    for(uint32_t i=0;i<c->bots.count;++i) {
        qa_buffer info={0};char name[8192],model[8192],file[8192],funname[8192];
        if(!bot_catalog_text(c,c->bots.records[i],&info,e)) return false;
        bool okay=bot_catalog_info_value((char *)info.data,"name",name,sizeof(name),e) &&
            bot_catalog_info_value((char *)info.data,"model",model,sizeof(model),e) &&
            bot_catalog_info_value((char *)info.data,"aifile",file,sizeof(file),e) &&
            bot_catalog_info_value((char *)info.data,"funname",funname,sizeof(funname),e);
        qa_buffer_free(&info);if(!okay) return false;
        char text[32768];int length=snprintf(text,sizeof(text),"%-16s %-16s %-20s %-20s\n",
            *name?name:"UnnamedPlayer",*model?model:"visor/default",*file?file:"bots/default_c.c",funname);
        if(length<0 || (size_t)length>=sizeof(text)) return bot_catalog_fail(e,QA_ERROR_FORMAT,"source bot list overflows formatter storage");
        if(!c->services.print(c->services.context,text,e)) return false;
    }
    return true;
}
bool qa_bot_catalog_add(qa_bot_catalog *c,const qa_bot_catalog_add_request *request,qa_error *e) {
    if(!request || !request->definition_name || !request->public_name || !request->team)
        return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"source AddBot requires definition, public name and team text");
    if(!bot_catalog_enter(c,e)) return false;
    bool okay=add_bot(c,request->definition_name,request->skill,request->team,request->delay_ms,request->public_name,e);
    --c->calls;return okay;
}
bool qa_bot_catalog_add_utf8(qa_bot_catalog *c,const qa_bot_catalog_add_request *request,qa_error *e) {
    if(!request || !request->definition_name || !request->public_name || !request->team)
        return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"public AddBot requires definition, public name and team text");
    const char *texts[3]={request->definition_name,request->public_name,request->team};
    char *source[3]={0};bool okay=true;
    for(size_t i=0;okay && i<3;++i) {
        size_t length=strlen(texts[i]);source[i]=malloc(length+1);
        if(!source[i]) okay=bot_catalog_fail(e,QA_ERROR_MEMORY,"decoding public AddBot source text");
        else okay=bot_catalog_latin1(texts[i],source[i],length+1,e);
    }
    if(okay) {
        qa_bot_catalog_add_request raw={.definition_name=source[0],.public_name=source[1],.team=source[2],
            .skill=request->skill,.delay_ms=request->delay_ms};
        okay=qa_bot_catalog_add(c,&raw,e);
    }
    for(size_t i=0;i<3;++i) free(source[i]);
    return okay;
}
bool qa_bot_catalog_console(qa_bot_catalog *c,const char *const *argv,size_t argc,qa_error *e) {
    if((argc && !argv) || !bot_catalog_enter(c,e)) return false;
    const char *command=argc?argv[0]:"";bool okay=true;
    if(bot_catalog_equal(command,"botlist")) okay=list_bots(c,e);
    else if(bot_catalog_equal(command,"addbot")) {
        int32_t enabled;okay=bot_catalog_cvar(c,"bot_enable",NULL,0,&enabled,e);
        if(okay && enabled) {
            char arguments[5][1024];
            for(size_t i=0;i<5;++i) {
                const char *value=i+1<argc?argv[i+1]:"";size_t length=strlen(value);if(length>1023) length=1023;
                memcpy(arguments[i],value,length);arguments[i][length]=0;
            }
            if(!*arguments[0]) okay=c->services.print(c->services.context,
                "Usage: Addbot <botname> [skill 1-5] [team] [msec delay] [altname]\n",e);
            else {
                okay=add_bot(c,arguments[0],*arguments[1]?bot_catalog_atof(arguments[1]):4,arguments[2],
                    *arguments[3]?bot_catalog_atoi(arguments[3]):0,arguments[4],e);
                qa_bot_catalog_clock clock;int32_t running;
                if(okay) okay=clock_read(c,&clock,e) && bot_catalog_cvar(c,"cl_running",NULL,0,&running,e);
                if(okay && bot_catalog_word((uint32_t)clock.time-(uint32_t)clock.start_time)>1000 && running)
                    okay=c->services.server_command(c->services.context,-1,"loaddefered\n",e);
            }
        }
    }
    --c->calls;return okay;
}
bool qa_bot_catalog_remove_queued_begin(qa_bot_catalog *c,int32_t client,qa_error *e) {
    if(!bot_catalog_enter(c,e)) return false;
    for(size_t i=0;i<BOT_CATALOG_QUEUE;++i) if(c->queue[i].client==client) {c->queue[i].time=0;break;}
    --c->calls;return true;
}
static bool eligible(qa_bot_catalog *c,int32_t number,int32_t team,bool bot,qa_bot_catalog_client *client,bool *found,qa_error *e) {
    if(!c->services.client(c->services.context,number,client,e)) return false;
    *found=client->has_player && client->connected && client->bot==bot && (team<0 || client->team==team);return true;
}
static bool count_players(qa_bot_catalog *c,int32_t team,bool bots,const qa_bot_catalog_clock *clock,int32_t *out,qa_error *e) {
    uint32_t count=0;
    for(int32_t i=0;i<clock->max_clients;++i) {
        qa_bot_catalog_client client={0};bool found;
        if(!eligible(c,i,team,bots,&client,&found,e)) return false;
        if(found) ++count;
    }
    if(bots) for(size_t i=0;i<BOT_CATALOG_QUEUE;++i)
        if(c->queue[i].time!=0 && c->queue[i].time<=clock->time) ++count;
    *out=bot_catalog_word(count);return true;
}
static bool name_used(qa_bot_catalog *c,const char *name,int32_t team,const qa_bot_catalog_clock *clock,bool *out,qa_error *e) {
    *out=false;
    for(int32_t i=0;i<clock->max_clients;++i) {
        qa_bot_catalog_client client={0};bool found;
        if(!eligible(c,i,team,true,&client,&found,e)) return false;
        if(found && (!client.name || bot_catalog_equal(name,client.name))) {
            if(!client.name) return bot_catalog_fail(e,QA_ERROR_FORMAT,"actual bot player has no source netname");
            *out=true;break;
        }
    }
    return true;
}
static void clean(char *text) {
    size_t read=0,write=0;
    while(text[read]) {
        unsigned char ch=(unsigned char)text[read++];
        if(ch=='^' && text[read] && text[read]!='^') {++read;continue;}
        if(ch>=32 && ch<=126) text[write++]=(char)ch;
    }
    text[write]=0;
}
static bool add_random(qa_bot_catalog *c,int32_t team,const qa_bot_catalog_clock *clock,qa_error *e) {
    int32_t count=0;
    for(uint32_t i=0;i<c->bots.count;++i) {
        qa_buffer info={0};char name[8192];bool used;
        if(!bot_catalog_text(c,c->bots.records[i],&info,e)) return false;
        bool okay=bot_catalog_info_value((char *)info.data,"name",name,sizeof(name),e) && name_used(c,name,team,clock,&used,e);
        qa_buffer_free(&info);if(!okay) return false;if(!used) ++count;
    }
    float random;if(!c->services.random(c->services.context,&random,e)) return false;
    volatile float product=random*(float)count;
    int64_t selected=!isfinite(product) || product>=2147483648.0f || product<-2147483648.0f?INT32_MIN:(int32_t)product;
    for(uint32_t i=0;i<c->bots.count;++i) {
        qa_buffer info={0};char name[8192];bool used;
        if(!bot_catalog_text(c,c->bots.records[i],&info,e)) return false;
        bool okay=bot_catalog_info_value((char *)info.data,"name",name,sizeof(name),e) && name_used(c,name,team,clock,&used,e);
        qa_buffer_free(&info);if(!okay) return false;if(used) continue;
        --selected;if(selected>0) continue;
        char skill[128],decimal[64],command[256];
        if(!bot_catalog_cvar(c,"g_spSkill",skill,sizeof(skill),NULL,e) || !fixed(bot_catalog_atof(skill),6,decimal,sizeof(decimal),e)) return false;
        if(strlen(name)>35) name[35]=0;
        clean(name);
        snprintf(command,sizeof(command),"addbot %s %s %s 0\n",name,decimal,team==1?"red":team==2?"blue":"");
        return c->services.insert_command(c->services.context,command,e);
    }
    return true;
}
static bool remove_random(qa_bot_catalog *c,int32_t team,const qa_bot_catalog_clock *clock,bool *removed,qa_error *e) {
    *removed=false;
    for(int32_t i=0;i<clock->max_clients;++i) {
        qa_bot_catalog_client client={0};bool found;
        if(!eligible(c,i,team,true,&client,&found,e)) return false;
        if(!found) continue;
        if(!client.name || strlen(client.name)>=36) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Bot netname exceeds source 36-byte storage");
        char name[36],command[80];strcpy(name,client.name);clean(name);snprintf(command,sizeof(command),"kick %s\n",name);
        if(!c->services.insert_command(c->services.context,command,e)) return false;
        *removed=true;return true;
    }
    return true;
}
static bool check_team(qa_bot_catalog *c,const qa_bot_catalog_clock *clock,int32_t minimum,
    int32_t count_team,int32_t add_team,int32_t remove_team,bool tournament,qa_error *e) {
    int32_t humans,bots;
    if(!count_players(c,count_team,false,clock,&humans,e) || !count_players(c,count_team,true,clock,&bots,e)) return false;
    int64_t total=(int64_t)humans+bots;
    if(total<minimum) return add_random(c,add_team,clock,e);
    if(total>minimum && bots) {
        bool removed=false;
        if(tournament && !remove_random(c,3,clock,&removed,e)) return false;
        if(!removed) return remove_random(c,remove_team,clock,&removed,e);
    }
    return true;
}
bool bot_catalog_minimum_check(qa_bot_catalog *c,qa_error *e) {
    qa_bot_catalog_clock clock;if(!clock_read(c,&clock,e)) return false;
    if(clock.intermission_time || c->check_minimum_time>bot_catalog_word((uint32_t)clock.time-10000u)) return true;
    c->check_minimum_time=clock.time;
    if(!c->minimum_registered) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"G_CheckMinimumPlayers requires G_InitBots");
    if(!bot_catalog_minimum_update(c,e)) return false;
    int32_t minimum=c->minimum_integer;if(minimum<=0) return true;
    if(clock.game_type>=3) {
        int32_t half=clock.max_clients/2;if(minimum>=half) minimum=half-1;
        return check_team(c,&clock,minimum,1,1,1,false,e) && check_team(c,&clock,minimum,2,2,2,false,e);
    }
    if(minimum>=clock.max_clients) minimum=clock.max_clients-1;
    if(clock.game_type==1) return check_team(c,&clock,minimum,-1,0,-1,true,e);
    if(clock.game_type==0) return check_team(c,&clock,minimum,0,0,0,false,e);
    return true;
}
bool qa_bot_catalog_check_spawn(qa_bot_catalog *c,qa_error *e) {
    if(!bot_catalog_enter(c,e)) return false;
    bool okay=bot_catalog_minimum_check(c,e);
    for(size_t i=0;okay && i<BOT_CATALOG_QUEUE;++i) {
        qa_bot_catalog_clock clock;okay=clock_read(c,&clock,e);if(!okay) break;
        if(c->queue[i].time==0 || c->queue[i].time>clock.time) continue;
        int32_t client=c->queue[i].client;
        okay=c->services.begin(c->services.context,client,e);if(!okay) break;
        c->queue[i].time=0;
        /* Begin may change the source game. Read its actual type afterwards. */
        okay=clock_read(c,&clock,e);
        if(okay && clock.game_type==2) {
            char info[1024],model[64],command[144];
            okay=c->services.userinfo(c->services.context,client,info,sizeof(info),e) &&
                bot_catalog_info_value(info,"model",model,sizeof(model),e);
            if(okay) {
                char *slash=strrchr(model,'/');char *skin=slash?slash+1:model;
                if(bot_catalog_equal(skin,"default")) {if(slash) *slash=0;skin=model;}
                snprintf(command,sizeof(command),"play sound/player/announce/%s.wav\n",skin);
                okay=c->services.append_command(c->services.context,command,e);
            }
        }
    }
    --c->calls;return okay;
}
