/* Q3 ai_main.c bot session scan and bg_lib.c formatting semantics. */
#include "internal.h"
#include "source_storage.h"
#include <stdio.h>

bool bot_ai_source_client(qa_bots *b,bot_ai_state *s,int32_t *client,qa_error *e) {
    return bot_ai_storage_i32(b,s,QA_BOT_SOURCE_CLIENT,client,false,e);
}
bool bot_ai_admitted_source_client(qa_bots *b,bot_ai_state *s,int32_t *client,qa_error *e) {
    if(!b->services.source_client(b->services.context,s->view.actor,client,e)) return false;
    return *client==s->view.source_client?true:
        bot_ai_fail(e,"bot actual source client differs from its admitted AI owner");
}
qa_actor_id bot_ai_source_actor(qa_bots *b,int32_t client) {
    return b->services.source_actor(b->services.context,client);
}
static bool scan_byte(const char *text,size_t length,size_t offset,int32_t *out,qa_error *e) {
    if(offset>length) return bot_ai_fail(e,"bot session scan reads past its source string");
    unsigned char byte=offset==length?0:(unsigned char)text[offset];
    *out=byte<128?(int32_t)byte:(int32_t)byte-256;return true;
}
static bool scan_start(const char *text,size_t length,size_t *cursor,int32_t *byte,qa_error *e) {
    if(!scan_byte(text,length,*cursor,byte,e)) return false;
    while(*byte && *byte<=32) {
        ++*cursor;if(!scan_byte(text,length,*cursor,byte,e)) return false;
    }
    return true;
}
static bool scan_integer(const char *text,size_t length,size_t *cursor,int32_t *out,qa_error *e) {
    size_t at=*cursor;int32_t byte;
    if(!scan_start(text,length,&at,&byte,e)) return false;
    if(!byte) {*out=0;return true;}
    bool negative=byte=='-';if(byte=='-' || byte=='+') ++at;
    uint32_t value=0;
    for(;;) {
        if(!scan_byte(text,length,at++,&byte,e)) return false;
        if(byte<'0' || byte>'9') break;
        value=value*10+(uint32_t)(byte-'0');
    }
    if(negative) value=0-value;
    memcpy(out,&value,sizeof(value));*cursor=at;return true;
}
static bool scan_float(const char *text,size_t length,size_t *cursor,float *out,qa_error *e) {
    int32_t byte;
    if(!scan_start(text,length,cursor,&byte,e)) return false;
    if(!byte) {*out=0;return true;}
    bool negative=byte=='-';
    if(byte=='-' || byte=='+') {++*cursor;if(!scan_byte(text,length,*cursor,&byte,e)) return false;}
    volatile float value=0;
    int32_t delimiter='0';
    if(byte!='.') for(;;) {
        if(!scan_byte(text,length,(*cursor)++,&delimiter,e)) return false;
        if(delimiter<'0' || delimiter>'9') break;
        volatile float product=value*10.0f;value=product+(float)(delimiter-'0');
    }
    if(delimiter=='.') {
        volatile float fraction=.1f;
        for(;;) {
            if(!scan_byte(text,length,(*cursor)++,&byte,e)) return false;
            if(byte<'0' || byte>'9') break;
            volatile float product=(float)(byte-'0')*fraction;
            value=value+product;fraction=fraction*.1f;
        }
    }
    *out=negative?-value:value;return true;
}
bool bot_ai_session_read(qa_bots *b,bot_ai_state *s,qa_error *e) {
    qa_cvars *configuration=b->services.configuration?b->services.configuration(b->services.context):NULL;
    int32_t client;if(!configuration || !bot_ai_source_client(b,s,&client,e))
        return bot_ai_fail(e,"restarted bot requires its actual source session configuration");
    char name[48],text[1024];snprintf(name,sizeof(name),"botsession%d",client);
    const qa_cvar_view *cvar=qa_cvars_find(configuration,name);
    const char *source_text=cvar?cvar->value:"";size_t length=strlen(source_text);
    if(length>=sizeof(text)) length=sizeof(text)-1;
    memcpy(text,source_text,length);text[length]=0;
    qa_bot_goal saved_goal={0},*goal=&saved_goal;size_t cursor=0;
    int32_t decisionmaker=0,type=0,teammate=0;
    int32_t *integers[]={&decisionmaker,&type,&teammate,
        &goal->area,&goal->entity,&goal->flags,&goal->item_info,&goal->number};
    const uint32_t integer_offsets[]={QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER,
        QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE,
        QA_BOT_SOURCE_LAST_TEAM_GOAL+12,QA_BOT_SOURCE_LAST_TEAM_GOAL+40,
        QA_BOT_SOURCE_LAST_TEAM_GOAL+48,QA_BOT_SOURCE_LAST_TEAM_GOAL+52,
        QA_BOT_SOURCE_LAST_TEAM_GOAL+44};
    for(size_t i=0;i<sizeof(integers)/sizeof(*integers);++i) {
        int32_t value;
        if(!scan_integer(text,length,&cursor,&value,e) ||
           !bot_ai_storage_i32(b,s,integer_offsets[i],&value,true,e)) return false;
        *integers[i]=value;
    }
    float *floats[]={&goal->origin.x,&goal->origin.y,&goal->origin.z,
        &goal->mins.x,&goal->mins.y,&goal->mins.z,&goal->maxs.x,&goal->maxs.y,&goal->maxs.z};
    const uint32_t float_offsets[]={0,4,8,16,20,24,28,32,36};
    for(size_t i=0;i<sizeof(floats)/sizeof(*floats);++i) {
        float value;
        if(!scan_float(text,length,&cursor,&value,e) ||
           !bot_ai_storage_f32(b,s,QA_BOT_SOURCE_LAST_TEAM_GOAL+float_offsets[i],&value,true,e)) return false;
        *floats[i]=value;
    }
    if(goal->number>=QA_BOT_SOURCE_GOAL_MIN) {
        *goal=(qa_bot_goal){0};
        if(!bot_ai_storage_goal(b,s,QA_BOT_SOURCE_LAST_TEAM_GOAL,goal,true,e)) return false;
        type=BOT_LTG_NONE;
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&type,true,e)) return false;
        decisionmaker=0;
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER,&decisionmaker,true,e)) return false;
        teammate=0;
        if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE,&teammate,true,e)) return false;
    }
    return true;
}
static void format_integer(char *text,size_t *used,int32_t value) {
    int32_t remaining=value;
    if(value<0) {uint32_t bits=0-(uint32_t)value;memcpy(&remaining,&bits,sizeof(bits));}
    char reversed[16];size_t count=0;
    do {reversed[count++]=(char)(unsigned char)('0'+remaining%10);remaining/=10;} while(remaining);
    if(value<0) reversed[count++]='-';
    while(count) text[(*used)++]=reversed[--count];
}
static bool format_float(char *text,size_t *used,float value,qa_error *e) {
    if(!isfinite(value) || fabs((double)value)>INT32_MAX)
        return bot_ai_fail(e,"bot session float exceeds source formatter integer range");
    volatile float remaining=value<0?-value:value;
    if(value<0) text[(*used)++]='-';
    format_integer(text,used,(int32_t)remaining);text[(*used)++]='.';
    for(size_t i=0;i<6;++i) {
        remaining=remaining-(float)(int32_t)remaining;remaining=remaining*10.0f;
        text[(*used)++]=(char)('0'+(int32_t)remaining%10);
    }
    return true;
}
bool bot_ai_session_write(qa_bots *b,bot_ai_state *s,qa_error *e) {
    qa_cvars *configuration=b->services.configuration?b->services.configuration(b->services.context):NULL;
    int32_t client;if(!configuration || !bot_ai_source_client(b,s,&client,e))
        return bot_ai_fail(e,"bot shutdown requires its actual source session configuration");
    int32_t decisionmaker,type,teammate;qa_bot_goal saved_goal;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER,&decisionmaker,false,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&type,false,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE,&teammate,false,e) ||
       !bot_ai_storage_goal(b,s,QA_BOT_SOURCE_LAST_TEAM_GOAL,&saved_goal,false,e)) return false;
    const qa_bot_goal *goal=&saved_goal;
    const int32_t integers[]={decisionmaker,type,teammate,
        goal->area,goal->entity,goal->flags,goal->item_info,goal->number};
    const float floats[]={goal->origin.x,goal->origin.y,goal->origin.z,
        goal->mins.x,goal->mins.y,goal->mins.z,goal->maxs.x,goal->maxs.y,goal->maxs.z};
    char name[48],text[512];size_t used=0;snprintf(name,sizeof(name),"botsession%d",client);
    for(size_t i=0;i<sizeof(integers)/sizeof(*integers);++i) {
        if(i) text[used++]=' ';
        format_integer(text,&used,integers[i]);
    }
    for(size_t i=0;i<sizeof(floats)/sizeof(*floats);++i) {
        text[used++]=' ';if(!format_float(text,&used,floats[i],e)) return false;
    }
    text[used]=0;return qa_cvars_set(configuration,name,text,true,e);
}
