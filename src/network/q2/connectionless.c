#include "qa/network_q2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
bool qa_q2_status_write(qa_net_writer *w,const qa_q2_status *s,size_t limit) {
    if(!s||!s->server_info||(s->player_count&&!s->players)||!limit)
    return qa_net_writer_fail(w,"Invalid Q2 status arguments");
    size_t used=strlen(s->server_info)+1;
    if(used>=limit)return qa_net_writer_fail(w,"Q2 serverinfo exceeds status packet capacity");
    if(!qa_q2_oob_write(w,"print\n")||!qa_net_write_data(w,s->server_info,used-1)||!qa_net_write_u8(w,'\n'))return false;
    for(size_t i=0;i<s->player_count;i++) {
        const qa_q2_status_player *p=&s->players[i];
        if(!p->name||strpbrk(p->name,"\r\n"))return qa_net_writer_fail(w,"Invalid Q2 status player name");
        char numbers[64];
        int length=snprintf(numbers,sizeof(numbers),"%d %d \"",p->score,p->ping);
        if(length<0||(size_t)length>=sizeof(numbers))return qa_net_writer_fail(w,"Q2 status number conversion failed");
        size_t name_size=strlen(p->name),prefix=(size_t)length;
        if(name_size>=limit||prefix+2>=limit-name_size||prefix+name_size+2>=limit-used)break;
        qa_net_write_data(w,numbers,prefix);
        qa_net_write_data(w,p->name,name_size);
        qa_net_write_data(w,"\"\n",2);
        used+=prefix+name_size+2;
    }
    return !w->failed;
}
bool qa_q2_status_read(const qa_q2_oob *m,char *info,size_t capacity,qa_q2_status_player_fn callback,void *user,bool *recognized,qa_error *error) {
    if(!m||!info||!capacity||!recognized) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 status parse arguments");
        return false;
    }
    *recognized=false;
    if(strcmp(m->command,"print"))return true;
    const char *body=m->text+m->body_offset;
    if(*body!='\\')return true;
    const char *end=strchr(body,'\n');
    if(!end)end=body+strlen(body);
    size_t size=(size_t)(end-body);
    if(size>=capacity) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 serverinfo exceeds output capacity");
        return false;
    }
    memcpy(info,body,size);
    info[size]=0;
    *recognized=true;
    body=*end?end+1:end;
    while(*body) {
        end=strchr(body,'\n');
        if(!end)end=body+strlen(body);
        const char *line_end=end;
        char *at;
        errno=0;
        long long score=strtoll(body,&at,10);
        if(!errno&&at!=body&&at<line_end&&*at==' ') {
            const char *ping_start=at+1;
            errno=0;
            long long ping=strtoll(ping_start,&at,10);
            if(!errno&&at!=ping_start&&at<line_end&&(size_t)(line_end-at)>2&&at[0]==' '&&at[1]=='"'&&line_end[-1]=='"'&&score>=INT32_MIN&&score<=INT32_MAX&&ping>=INT32_MIN&&ping<=INT32_MAX) {
                size_t name_size=(size_t)(line_end-at-3);
                char *name=malloc(name_size+1);
                if(!name) {
                    qa_error_set(error,QA_ERROR_MEMORY,0,"Q2 status name allocation failed");
                    return false;
                }
                memcpy(name,at+2,name_size);
                name[name_size]=0;
                bool ok=!callback||callback(user,(int32_t)score,(int32_t)ping,name,error);
                free(name);
                if(!ok)return false;
            }
        }
        body=*end?end+1:end;
    }
    return true;
}
bool qa_q2_info_write(qa_net_writer *w,const char *name,const char *map,uint32_t players,uint32_t maximum,const qa_net_protocol_id *protocols,size_t count,uint32_t offered,bool *present) {
    if(!name||!map||!protocols||!count||!present||players>maximum||strpbrk(name,"\r\n")||strpbrk(map,"\r\n"))return qa_net_writer_fail(w,"Invalid Q2 info arguments");
    *present=false;
    if(maximum==1)return true;
    bool supported=false;
    for(size_t i=0;i<count;i++)if(qa_q2_protocol_version(protocols[i])==offered)supported=true;
    char text[2048];
    int length=supported?snprintf(text,sizeof(text),"info\n%16s %8s %2u/%2u\n",name,map,players,maximum):snprintf(text,sizeof(text),"info\n%s: wrong version\n",name);
    if(length<0||(size_t)length>=sizeof(text))return qa_net_writer_fail(w,"Q2 info exceeds capacity");
    if(!qa_q2_oob_write(w,text))return false;
    *present=true;
    return true;
}
bool qa_q2_master_read(qa_bytes bytes,qa_net_address *addresses,size_t capacity,size_t *count,bool *recognized,qa_error *error) {
    if(!count||!recognized||(capacity&&!addresses)||(bytes.size&&!bytes.data)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 master parse arguments");
        return false;
    }
    *count=0;
    *recognized=false;
    size_t start=0;
    if(bytes.size>=4&&bytes.data[0]==255&&bytes.data[1]==255&&bytes.data[2]==255&&bytes.data[3]==255)start=4;
    if(bytes.size-start<8||memcmp(bytes.data+start,"servers",7)||(bytes.data[start+7]!=' '&&bytes.data[start+7]!='\n'))return true;
    *recognized=true;
    start+=8;
    if((bytes.size-start)%6) {
        qa_error_set(error,QA_ERROR_FORMAT,start,"Partial Q2 master address");
        return false;
    }
    for(size_t i=start;i<bytes.size;i+=6) {
        uint16_t port=(uint16_t)(((uint16_t)bytes.data[i+4]<<8)|bytes.data[i+5]);
        if(!port)continue;
        bool duplicate=false;
        for(size_t j=start;j<i;j+=6)if(!memcmp(bytes.data+i,bytes.data+j,6)) {
            duplicate=true;
            break;
        }
        if(duplicate)continue;
        if(*count>=capacity) {
            qa_error_set(error,QA_ERROR_ARGUMENT,i,"Q2 master addresses exceed output capacity");
            return false;
        }
        qa_net_address address= {
            .kind=QA_NET_IPV4,.port=port
        };
        memcpy(address.host.ipv4,bytes.data+i,4);
        addresses[(*count)++]=address;
    }
    return true;
}
