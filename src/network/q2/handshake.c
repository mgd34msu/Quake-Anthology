#include "handshake_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
static bool copy_text(char*out,size_t size,const char*in,qa_error*e) {
    size_t n=strlen(in);
    if(n>=size) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Q2 text exceeds capacity");
        return false;
    }
    memcpy(out,in,n+1);
    return true;
}
static bool utf8_valid(const unsigned char*s,size_t n) {
    for(size_t i=0;i<n;) {
        unsigned b=s[i++],count=0;
        uint32_t v;
        if(b<128)continue;
        if(b>=194&&b<=223) {
            count=1;
            v=b&31;
        } else if(b>=224&&b<=239) {
            count=2;
            v=b&15;
        } else if(b>=240&&b<=244) {
            count=3;
            v=b&7;
        } else return false;
        if(count>n-i)return false;
        unsigned total=count;
        while(count--) {
            b=s[i++];
            if((b&192)!=128)return false;
            v=(v<<6)|(b&63);
        }
        if((total==1&&v<128)||(total==2&&v<2048)||(total==3&&v<65536)||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return false;
    }
    return true;
}
bool qa_q2_oob_write(qa_net_writer*w,const char*text) {
    return text&&qa_net_write_u32(w,UINT32_MAX)&&qa_net_write_data(w,text,strlen(text));
}
bool qa_q2_oob_read(qa_bytes b,bool utf8,qa_q2_oob*out,bool*recognized,qa_error*e) {
    if(!out||!recognized||(b.size&&!b.data)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 OOB arguments");
        return false;
    }
    *recognized=false;
    if(b.size<4||b.data[0]!=255||b.data[1]!=255||b.data[2]!=255||b.data[3]!=255)return true;
    size_t n=0;
    while(n<b.size-4&&b.data[4+n])n++;
    if(n>=sizeof(out->text)) {
        qa_error_set(e,QA_ERROR_FORMAT,4,"Q2 OOB message too long");
        return false;
    }
    if(utf8&&!utf8_valid(b.data+4,n)) {
        qa_error_set(e,QA_ERROR_FORMAT,4,"Invalid Q2 OOB UTF-8");
        return false;
    }
    memset(out,0,sizeof(*out));
    memcpy(out->text,b.data+4,n);
    const char*newline=strchr(out->text,'\n');
    size_t end=newline?(size_t)(newline-out->text):n;
    out->body_offset=newline?end+1:n;
    size_t i=0,used=0;
    while(i<end) {
        while(i<end&&(unsigned char)out->text[i]<=32)i++;
        if(i>=end)break;
        if(out->text[i]=='/'&&i+1<end&&out->text[i+1]=='/')break;
        if(out->command&&out->argc==QA_Q2_MAX_OOB_ARGS) {
            qa_error_set(e,QA_ERROR_FORMAT,i,"Too many Q2 OOB arguments");
            return false;
        }
        char*token=out->tokens+used;
        bool quoted=out->text[i]=='"';
        if(quoted)i++;
        while(i<end&&(quoted?out->text[i]!='"':(unsigned char)out->text[i]>32)) {
            if(used+1>=sizeof(out->tokens)) {
                qa_error_set(e,QA_ERROR_FORMAT,i,"Q2 OOB tokens too long");
                return false;
            }
            out->tokens[used++]=out->text[i++];
        }
        if(quoted) {
            if(i>=end) {
                qa_error_set(e,QA_ERROR_FORMAT,i,"Unterminated Q2 OOB quote");
                return false;
            }
            i++;
        }
        out->tokens[used++]=0;
        if(!out->command)out->command=token;
        else out->argv[out->argc++]=token;
    }
    if(!out->command)out->command=out->tokens;
    *recognized=true;
    return true;
}
static bool integer(const char*s,int64_t fallback,bool optional,int64_t*out,qa_error*e) {
    if(!s&&optional) {
        *out=fallback;
        return true;
    }
    if(!s||!*s) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Missing Q2 connection integer");
        return false;
    }
    const char*p=s;
    if(*p=='+'||*p=='-')p++;
    if(!*p)goto invalid;
    for(;*p;p++)if(*p<'0'||*p>'9')goto invalid;
    errno=0;
    char*end;
    long long v=strtoll(s,&end,10);
    if(errno||*end)goto invalid;
    *out=(int64_t)v;
    return true;
    invalid:qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid Q2 connection integer");
    return false;
}
static const char*arg(const qa_q2_oob*m,size_t i) {
    return i<m->argc?m->argv[i]:NULL;
}
static bool safe_info(const char*s) {
    return s&&!strpbrk(s,"\"\r\n");
}
bool qa_q2_connect_read(const qa_q2_oob*m,qa_q2_connect_request*out,qa_error*e) {
    if(!m||!out||!m->command||strcmp(m->command,"connect")) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Not a Q2 connect request");
        return false;
    }
    qa_q2_connect_request q= {
        0
    };
    int64_t version,qport,challenge,payload,minor=0;
    if(!integer(arg(m,0),0,false,&version,e)||version<0||version>UINT32_MAX)return false;
    if(version==2023) {
        int64_t count;
        if(!integer(arg(m,1),0,false,&count,e)||count<1||count>8||m->argc<(size_t)count+3)goto invalid;
        q.protocol.kind=QA_NET_Q2KEX_2023;
        q.payload_bytes=65527;
        q.social_count=(size_t)count;
        for(size_t i=0;i<q.social_count;i++) {
            if(!safe_info(m->argv[i+2])||strchr(m->argv[i+2],'\\')||!copy_text(q.social_ids[i],sizeof(q.social_ids[i]),m->argv[i+2],e))return false;
        }
        size_t used=0;
        for(size_t i=2+q.social_count;i<m->argc;i++) {
            size_t n=strlen(m->argv[i]);
            if(!safe_info(m->argv[i])||n>sizeof(q.userinfo)-1-used)goto invalid;
            memcpy(q.userinfo+used,m->argv[i],n);
            used+=n;
        }
        q.userinfo[used]=0;
        *out=q;
        return true;
    }
    if(!integer(arg(m,1),0,false,&qport,e)||!integer(arg(m,2),0,false,&challenge,e)||!safe_info(arg(m,3))||challenge<INT32_MIN||challenge>INT32_MAX)goto invalid;
    if(!copy_text(q.userinfo,sizeof(q.userinfo),m->argv[3],e))return false;
    if(version==34)payload=1390;
    else if(!integer(arg(m,4),1390,true,&payload,e))return false;
    if(payload<512)payload=512;
    if(payload>4086)payload=4086;
    if(version==35) {
        if(!integer(arg(m,5),1903,true,&minor,e))return false;
    } else if(version==36) {
        if(!integer(arg(m,7),1015,true,&minor,e))return false;
    }
    if(minor<0||minor>UINT32_MAX||!qa_q2_protocol_from_version((uint32_t)version,(uint32_t)minor,&q.protocol,e))return false;
    q.qport=(uint16_t)((uint64_t)qport&(version==34?65535u:255u));
    q.challenge=(int32_t)challenge;
    q.payload_bytes=(size_t)payload;
    int64_t value;
    if(version==36) {
        if(!integer(arg(m,5),1,true,&value,e))return false;
        q.new_channel=value==1;
        if(!integer(arg(m,6),0,true,&value,e))return false;
        q.compression=value!=0;
    }  else if(version==1038||version==4038) {
        q.new_channel=true;
        if(!integer(arg(m,5),0,true,&value,e))return false;
        q.compression=value!=0;
    }  else if(version==35)q.compression=true;
    else if(version!=34)goto invalid;
    *out=q;
    return true;
    invalid:qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid Q2 connect request");
    return false;
}
static bool raw(qa_net_writer*w,const char*s) {
    return qa_net_write_data(w,s,strlen(s));
}
static bool quoted(qa_net_writer*w,const char*s) {
    return raw(w,"\"")&&raw(w,s)&&raw(w,"\"");
}
bool qa_q2_connect_write(qa_net_writer*w,const qa_q2_connect_request*q) {
    if(!q||!memchr(q->userinfo,0,sizeof(q->userinfo))||!safe_info(q->userinfo)||!qa_q2_protocol_version(q->protocol)||!qa_net_protocol_valid(q->protocol,w->error))return qa_net_writer_fail(w,"Invalid Q2 connect request");
    char line[256];
    uint32_t version=qa_q2_protocol_version(q->protocol);
    if(version==2022)return qa_net_writer_fail(w,"KEX demo protocol has no live connection handshake");
    if(version==2023) {
        size_t count=q->social_count?q->social_count:1;
        if(count>8)return qa_net_writer_fail(w,"Too many KEX social identities");
        char userinfo[16385];
        if(!qa_q2_kex_client_userinfo(q->userinfo,userinfo,sizeof(userinfo),w->error))return qa_net_writer_fail(w,"Invalid KEX userinfo");
        snprintf(line,sizeof(line),"connect 2023 %zu",count);
        qa_q2_oob_write(w,line);
        for(size_t i=0;i<count;i++) {
            const char*s=q->social_count?q->social_ids[i]:"";
            if(q->social_count&&!memchr(s,0,sizeof(q->social_ids[i])))return qa_net_writer_fail(w,"Unterminated KEX social identity");
            if(!safe_info(s)||strchr(s,'\\'))return qa_net_writer_fail(w,"Invalid KEX social identity");
            raw(w," ");
            quoted(w,s);
        }
        size_t n=strlen(userinfo);
        if(n>8192)return qa_net_writer_fail(w,"KEX userinfo exceeds native capacity");
        if(!utf8_valid((const unsigned char*)userinfo,n))return qa_net_writer_fail(w,"Invalid KEX userinfo UTF-8");
        size_t at=0;
        do {
            size_t length=n-at;
            if(length>510) {
                length=510;
                while(length&&((unsigned char)userinfo[at+length]&192)==128)length--;
            }
            raw(w," \"");
            qa_net_write_data(w,userinfo+at,length);
            raw(w,"\"");
            at+=length;
        }
        while(at<n);
        return raw(w,"\n");
    }
    if(q->payload_bytes>65535)return qa_net_writer_fail(w,"Q2 connect payload limit too large");
    snprintf(line,sizeof(line),"connect %u %u %d ",version,version==34?q->qport:(uint8_t)q->qport,q->challenge);
    qa_q2_oob_write(w,line);
    quoted(w,q->userinfo);
    switch(version) {
        case 34:line[0]=0;
        break;
        case 35:snprintf(line,sizeof(line)," %zu %u",q->payload_bytes,q->protocol.revision);
        break;
        case 36:snprintf(line,sizeof(line)," %zu %u %u %u",q->payload_bytes,q->new_channel?1u:0u,q->compression?1u:0u,q->protocol.revision);
        break;
        default:snprintf(line,sizeof(line)," %zu %u",q->payload_bytes,q->compression?1u:0u);
        break;
    }
    return raw(w,line)&&raw(w,"\n");
}
typedef struct pair {
    char key[1024],value[8193];
    bool selected;
}
pair;
static bool info_pairs(const char*text,pair*p,size_t*count,qa_error*e) {
    *count=0;
    const char*s=text;
    if(*s=='\\')s++;
    while(*s) {
        if(*count>=256)goto invalid;
        const char*mid=strchr(s,'\\');
        if(!mid||mid==s)goto invalid;
        const char*end=strchr(mid+1,'\\');
        if(!end)end=mid+1+strlen(mid+1);
        size_t kn=(size_t)(mid-s),vn=(size_t)(end-mid-1);
        if(kn>=sizeof(p[0].key)||vn>=sizeof(p[0].value))goto invalid;
        memcpy(p[*count].key,s,kn);
        p[*count].key[kn]=0;
        memcpy(p[*count].value,mid+1,vn);
        p[*count].value[vn]=0;
        p[*count].selected=false;
        (*count)++;
        s=*end?end+1:end;
    }
    return true;
    invalid:qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX userinfo pairs");
    return false;
}
static bool suffix(const char*key,size_t*base,uint64_t*seat) {
    const char*p=strrchr(key,'_');
    if(!p||!p[1])return false;
    uint64_t n=0;
    for(const char*s=p+1;*s;s++) {
        if(*s<'0'||*s>'9')return false;
        if(n>UINT64_MAX/10||(n==UINT64_MAX/10&&(unsigned)(*s-'0')>UINT64_MAX%10))return false;
        n=n*10+(unsigned)(*s-'0');
    }
    *base=(size_t)(p-key);
    *seat=n;
    return true;
}
static bool append_pair(char*out,size_t cap,size_t*used,const char*key,const char*value,qa_error*e) {
    size_t k=strlen(key),v=strlen(value);
    if(k>cap||v>cap||*used>=cap||k+v+2>=cap-*used) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"KEX userinfo exceeds output capacity");
        return false;
    }
    out[(*used)++]='\\';
    memcpy(out+*used,key,k);
    *used+=k;
    out[(*used)++]='\\';
    memcpy(out+*used,value,v);
    *used+=v;
    out[*used]=0;
    return true;
}
bool qa_q2_kex_seat_userinfo(const char*text,unsigned seat,char*out,size_t cap,qa_error*e) {
    if(!text||!out||!cap||seat>=8) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX seat userinfo arguments");
        return false;
    }
    pair*p=calloc(256,sizeof(*p));
    if(!p) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX userinfo allocation failed");
        return false;
    }
    size_t count=0,used=0;
    bool ok=info_pairs(text,p,&count,e);
    out[0]=0;
    if(ok) {
        for(size_t i=0;i<count;i++) {
            size_t base;
            uint64_t id;
            if(suffix(p[i].key,&base,&id)) {
                if(id==seat) {
                    p[i].key[base]=0;
                    p[i].selected=true;
                } else p[i].key[0]=0;
            }
        }
        for(size_t i=0;i<count&&ok;i++) {
            if(!p[i].key[0])continue;
            bool skip=false;
            size_t chosen=i;
            for(size_t j=0;j<i;j++)if(!strcmp(p[j].key,p[i].key)) {
                skip=true;
                break;
            }
            if(skip)continue;
            for(size_t j=i+1;j<count;j++)if(!strcmp(p[j].key,p[i].key)&&(p[j].selected||!p[chosen].selected))chosen=j;
            ok=append_pair(out,cap,&used,p[i].key,p[chosen].value,e);
        }
    }
    free(p);
    return ok;
}
bool qa_q2_kex_client_userinfo(const char*text,char*out,size_t cap,qa_error*e) {
    if(!text||!out||!cap||(*text&&text[0]!='\\')) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX client userinfo arguments");
        return false;
    }
    pair*p=calloc(256,sizeof(*p));
    if(!p) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX userinfo allocation failed");
        return false;
    }
    size_t count=0;
    bool ok=info_pairs(text,p,&count,e)&&copy_text(out,cap,text,e);
    size_t used=ok?strlen(out):0;
    for(size_t i=0;i<count&&ok;i++) {
        size_t base;
        uint64_t seat;
        if(suffix(p[i].key,&base,&seat))continue;
        char key[1027];
        snprintf(key,sizeof(key),"%s_0",p[i].key);
        bool exists=false;
        for(size_t j=0;j<count;j++)if(!strcmp(p[j].key,key)) {
            exists=true;
            break;
        }
        if(!exists)ok=append_pair(out,cap,&used,key,p[i].value,e);
    }
    free(p);
    return ok;
}
bool qa_q2_challenges_create(size_t capacity,qa_q2_random_fn random,void*user,qa_q2_challenges**out,qa_error*e) {
    if(!capacity||capacity>65536||!random||!out) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 challenge table");
        return false;
    }
    qa_q2_challenges*t=calloc(1,sizeof(*t));
    if(!t) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"Q2 challenge allocation failed");
        return false;
    }
    t->entries=calloc(capacity,sizeof(*t->entries));
    if(!t->entries) {
        free(t);
        qa_error_set(e,QA_ERROR_MEMORY,0,"Q2 challenge allocation failed");
        return false;
    }
    t->capacity=capacity;
    t->random=random;
    t->user=user;
    *out=t;
    return true;
}
void qa_q2_challenges_destroy(qa_q2_challenges*t) {
    if(t) {
        free(t->entries);
        free(t);
    }
}
bool qa_q2_challenge_validate(const qa_q2_challenges*t,const qa_net_address*a,int32_t v) {
    if(!t||!a)return false;
    if(a->kind==QA_NET_LOOPBACK)return true;
    for(size_t i=0;i<t->count;i++)if(qa_net_address_equal(&t->entries[i].address,a,false))return t->entries[i].value==v;
    return false;
}
bool qa_q2_challenge_reply(qa_q2_challenges*t,const qa_net_address*a,uint64_t now,const qa_net_protocol_id*protocols,size_t count,qa_net_writer*w) {
    if(!t||!a||!protocols||!count)return qa_net_writer_fail(w,"Invalid Q2 challenge reply");
    size_t slot=t->count;
    for(size_t i=0;i<t->count;i++)if(qa_net_address_equal(a,&t->entries[i].address,false)) {
        slot=i;
        break;
    }
    if(slot==t->count) {
        if(slot==t->capacity) {
            slot=0;
            for(size_t i=1;i<t->count;i++)if(t->entries[i].time<t->entries[slot].time)slot=i;
        } else t->count++;
        t->entries[slot]=(struct challenge_entry) {
            *a,(int32_t)(t->random(t->user)&0x7fff),now
        };
    }
    char buffer[64];
    snprintf(buffer,sizeof(buffer),"challenge %d p=",t->entries[slot].value);
    qa_q2_oob_write(w,buffer);
    bool first=true;
    for(size_t i=0;i<count;i++) {
        uint32_t version=qa_q2_protocol_version(protocols[i]);
        if(!version)return qa_net_writer_fail(w,"Invalid challenge protocol");
        bool seen=false;
        for(size_t j=0;j<i;j++)if(qa_q2_protocol_version(protocols[j])==version)seen=true;
        if(seen)continue;
        snprintf(buffer,sizeof(buffer),"%s%u",first?"":",",version);
        first=false;
        raw(w,buffer);
    }
    return !w->failed;
}
bool qa_q2_handshake_init(qa_q2_handshake*h,const qa_net_address*a,const qa_net_protocol_id*p,size_t n,uint16_t port,const char*info,const char*social,size_t payload,qa_error*e) {
    if(!h||!a||!p||!n||n>8||!info||!safe_info(info)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 handshake");
        return false;
    }
    for(size_t i=0;i<n;i++)if(!qa_q2_protocol_version(p[i])||!qa_net_protocol_valid(p[i],e))return false;
    memset(h,0,sizeof(*h));
    h->phase=QA_Q2_CHALLENGING;
    h->remote=*a;
    memcpy(h->preferences,p,n*sizeof(*p));
    h->preference_count=n;
    h->request.qport=port;
    h->qport=port;
    h->request.payload_bytes=payload?payload:1390;
    h->retry_ns=UINT64_C(3000000000);
    if(!copy_text(h->request.userinfo,sizeof(h->request.userinfo),info,e))return false;
    if(p[0].kind==QA_NET_Q2KEX_2023) {
        h->phase=QA_Q2_CONNECTING;
        h->request.protocol=p[0];
        h->request.qport=0;
        h->request.payload_bytes=65527;
        h->request.social_count=1;
        if(!copy_text(h->request.social_ids[0],sizeof(h->request.social_ids[0]),social?social:"",e))return false;
    }
    return true;
}
bool qa_q2_handshake_poll(qa_q2_handshake*h,uint64_t now,qa_net_writer*w,bool*present) {
    if(!h||!present)return qa_net_writer_fail(w,"Invalid Q2 handshake poll");
    *present=false;
    if(h->phase==QA_Q2_CONNECTED||h->phase==QA_Q2_REFUSED)return true;
    if(h->sent&&now>=h->last_sent_ns&&now-h->last_sent_ns<h->retry_ns)return true;
    bool ok=h->phase==QA_Q2_CHALLENGING?qa_q2_oob_write(w,"getchallenge\n"):qa_q2_connect_write(w,&h->request);
    if(ok) {
        h->sent=true;
        h->last_sent_ns=now;
        *present=true;
    }
    return ok;
}
static bool download_url(const char*s,char*out,size_t cap) {
    size_t n=strlen(s),scheme=0;
    if(n>=cap||strpbrk(s,"#?\\\r\n "))return false;
    char prefix[9]={0};
    for(size_t i=0;i<n&&i<8;i++)prefix[i]=(char)tolower((unsigned char)s[i]);
    if(!strncmp(prefix,"http://",7))scheme=7;
    else if(!strncmp(prefix,"https://",8))scheme=8;
    else return false;
    const char*authority=s+scheme,*end=strchr(authority,'/');
    if(!end)end=s+n;
    if(end==authority||memchr(authority,'@',(size_t)(end-authority)))return false;
    const char*port=NULL;
    if(*authority=='[') {
        const char*close=memchr(authority,']',(size_t)(end-authority));
        if(!close||close==authority+1)return false;
        if(close+1<end) {if(close[1]!=':')return false;port=close+2;}
    } else {
        const char*colon=memchr(authority,':',(size_t)(end-authority));
        if(colon) {if(colon==authority)return false;port=colon+1;}
    }
    if(port) {
        if(port==end)return false;
        uint32_t value=0;
        for(const char*p=port;p<end;p++) {
            if(*p<'0'||*p>'9'||value>6553)return false;
            value=value*10+(unsigned)(*p-'0');
            if(value>65535)return false;
        }
    }
    if(s[n-1]!='/'&&n+1>=cap)return false;
    memcpy(out,s,n);memcpy(out,prefix,scheme);
    if(s[n-1]!='/')out[n++]='/';
    out[n]=0;return true;
}

bool qa_q2_handshake_receive(qa_q2_handshake*h,const qa_net_address*a,const qa_q2_oob*m,bool*accepted,qa_error*e) {
    if(!h||!a||!m||!m->command||!accepted) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 handshake message");
        return false;
    }
    *accepted=false;
    if(!qa_net_address_equal(&h->remote,a,true))return true;
    if(!strcmp(m->command,"challenge")&&h->phase!=QA_Q2_CONNECTED) {
        int64_t challenge;
        if(!integer(arg(m,0),0,false,&challenge,e)||challenge<INT32_MIN||challenge>INT32_MAX)return false;
        uint32_t versions[32]= {
            34
        };
        size_t count=1;
        for(size_t i=1;i<m->argc;i++)if(!strncmp(m->argv[i],"p=",2)) {
            count=0;
            const char*s=m->argv[i]+2;
            while(*s) {
                char value[32];
                size_t n=0;
                while(*s&&*s!=',') {
                    if(n+1>=sizeof(value))goto invalid;
                    value[n++]=*s++;
                }
                value[n]=0;
                int64_t v;
                if(!integer(value,0,false,&v,e)||v<0||v>UINT32_MAX||count>=32)goto invalid;
                versions[count++]=(uint32_t)v;
                if(*s)s++;
            }
            break;
        }
        size_t chosen=h->preference_count;
        for(size_t i=0;i<h->preference_count&&chosen==h->preference_count;i++)for(size_t j=0;j<count;j++)if(qa_q2_protocol_version(h->preferences[i])==versions[j]) {
            chosen=i;
            break;
        }
        *accepted=true;
        if(chosen==h->preference_count) {
            h->phase=QA_Q2_REFUSED;
            copy_text(h->refusal,sizeof(h->refusal),"No compatible advertised Quake II protocol",e);
            return true;
        }
        qa_net_protocol_id p=h->preferences[chosen];
        if(p.kind==QA_NET_Q2KEX_DEMO_2022||p.kind==QA_NET_Q2KEX_2023||(p.kind==QA_NET_Q2PRO_36&&p.revision==1016)) {
            h->phase=QA_Q2_REFUSED;
            copy_text(h->refusal,sizeof(h->refusal),"Selected Q2 dialect has no challenge handshake",e);
            return true;
        }
        h->request.protocol=p;
        h->request.challenge=(int32_t)challenge;
        h->request.qport=p.kind==QA_NET_Q2_34?h->qport:(uint8_t)h->qport;
        h->request.new_channel=p.kind!=QA_NET_Q2_34&&p.kind!=QA_NET_R1Q2_35;
        h->request.compression=p.kind!=QA_NET_Q2_34;
        h->phase=QA_Q2_CONNECTING;
        h->sent=false;
        return true;
    }
    if(!strcmp(m->command,"client_connect")&&h->phase==QA_Q2_CONNECTING&&(h->request.protocol.kind!=QA_NET_Q2KEX_2023||(arg(m,0)&&!strcmp(m->argv[0],"2023")))) {
        h->phase=QA_Q2_CONNECTED;
        h->download_server[0]=0;
        for(size_t i=0;i<m->argc;i++)if(!strncmp(m->argv[i],"dlserver=",9)) {
            download_url(m->argv[i]+9,h->download_server,sizeof(h->download_server));
            break;
        }
        *accepted=true;
    }
    return true;
    invalid:qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid Q2 challenge offer");
    return false;
}
