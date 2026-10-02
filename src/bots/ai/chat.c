/* Source client-name helpers share the real player configstrings. */
#include "internal.h"
#include "qa/network_q3.h"

static bool player_information(qa_bots *b,int32_t client,char text[1024],qa_error *e) {
    if(client<0 || client>=64) {text[0]=0;return true;}
    return b->services.configstring?
        b->services.configstring(b->services.context,544+(uint32_t)client,text,1024,e):
        bot_ai_fail(e,"bot source chat requires its actual player configstrings");
}
bool bot_ai_client_name(qa_bots *b,int32_t client,char *name,size_t capacity,bool clean,qa_error *e) {
    if(!capacity || !name) return bot_ai_fail(e,"bot source client name needs bounded output");
    if(client<0 || client>=64) {
        if(!bot_ai_source_print(b,"^1Error: ClientName: client out of range\n",e)) return false;
        const char *sentinel="[client out of range]";
        size_t length=strlen(sentinel);if(length>=capacity) length=capacity-1;
        memcpy(name,sentinel,length);name[length]=0;return true;
    }
    char text[1024],raw[1024];
    if(!player_information(b,client,text,e) || !qa_q3_info_value(text,"n",raw,sizeof(raw),e)) return false;
    size_t length=strlen(raw);if(length>=capacity) length=capacity-1;
    memcpy(name,raw,length);name[length]=0;
    if(clean) {
        unsigned char *out=(unsigned char *)name;
        for(const unsigned char *in=(const unsigned char *)name;*in;++in) {
            if(*in=='^' && in[1] && in[1]!='^') {++in;continue;}
            if(*in<32 || *in>126) continue;
            *out++=*in;
        }
        *out=0;
    }
    return true;
}
bool bot_ai_leader_client_name(qa_bots *b,bot_ai_state *s,int32_t client,qa_error *e) {
    if(client<0 || client>=64)
        return bot_ai_source_print(b,"^1Error: ClientName: client out of range\n",e);
    char information[1024],raw[1024];
    if(!player_information(b,client,information,e) ||
       !qa_q3_info_value(information,"n",raw,sizeof(raw),e)) return false;
    return qa_bot_source_record_team_leader(&b->services.memory,s->source_record,raw,false,true,e);
}
bool bot_ai_source_print(qa_bots *b,const char *text,qa_error *e) {
    return b->services.print?b->services.print(b->services.context,text,e):
        bot_ai_fail(e,"bot source Print service is unavailable");
}
bool bot_ai_easy_name(qa_bots *b,int32_t client,char *name,size_t capacity,qa_error *e) {
    if(!name || !capacity) return bot_ai_fail(e,"bot source easy name needs bounded output");
    char raw[128],clean[128];
    if(!bot_ai_client_name(b,client,raw,sizeof(raw),true,e)) return false;
    size_t count=0;
    for(size_t i=0;raw[i] && i<127;++i) {
        unsigned char byte=(unsigned char)raw[i]&127;
        if(byte!=' ') clean[count++]=(char)byte;
    }
    clean[count]=0;
    char *left=strchr(clean,'['),*right=strchr(clean,']');
    if(left && right) {
        char *begin=left<right?left:right,*end=left<right?right:left;
        memmove(begin,end+1,strlen(end+1)+1);
    }
    const unsigned char *cursor=(const unsigned char *)clean;
    if((cursor[0]=='m' || cursor[0]=='M') && (cursor[1]=='r' || cursor[1]=='R')) cursor+=2;
    count=0;
    while(*cursor && count+1<capacity) {
        unsigned char byte=*cursor++;
        if(byte>='A' && byte<='Z') byte+='a'-'A';
        if((byte>='a' && byte<='z') || (byte>='0' && byte<='9') || byte=='_') name[count++]=(char)byte;
    }
    name[count]=0;return true;
}
