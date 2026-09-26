#include "qa/network_q2_kex.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static bool word(qa_net_writer*w,uint16_t v) {
    return qa_net_write_u8(w,(uint8_t)(v>>8))&&qa_net_write_u8(w,(uint8_t)v);
}
static uint16_t read_word(qa_net_reader*r) {
    uint16_t v=(uint16_t)((uint16_t)qa_net_read_u8(r)<<8);
    return (uint16_t)(v|qa_net_read_u8(r));
}
static bool dword(qa_net_writer*w,uint32_t v) {
    return word(w,(uint16_t)(v>>16))&&word(w,(uint16_t)v);
}
bool qa_kex_discovery_query(qa_net_writer*w) {
    uint8_t data[64];
    qa_net_writer payload;
    qa_net_writer_init(&payload,data,sizeof(data),w->error);
    qa_kex_write_string(&payload,"CRANTIME");
    qa_kex_write_string(&payload,"QuakeII");
    qa_kex_packet p= {
        .kind=129,.has_kind=true,.payload= {
            data,qa_net_writer_size(&payload)
        }
    };
    return !payload.failed&&qa_kex_packet_write(w,&p);
}
bool qa_kex_discovery_read(qa_bytes b,qa_kex_discovery*out,qa_error*e) {
    if(!out) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing KEX discovery output");
        return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r,b,e);
    out->attribute_count=0;
    if(!qa_kex_read_string(&r,out->name,sizeof(out->name)))return false;
    uint64_t players=qa_kex_read_varint(&r),maximum=qa_kex_read_varint(&r);
    if(r.failed)return false;
    if(players>255||maximum>255||players>maximum)return qa_net_reader_fail(&r,"Invalid KEX lobby capacity");
    out->players=(uint8_t)players;
    out->max_players=(uint8_t)maximum;
    while(qa_net_reader_remaining(&r)) {
        if(out->attribute_count>=256)return qa_net_reader_fail(&r,"Too many KEX lobby attributes");
        qa_kex_attribute*a=&out->attributes[out->attribute_count++];
        if(!qa_kex_read_string(&r,a->key,sizeof(a->key))||!qa_kex_read_string(&r,a->value,sizeof(a->value)))return false;
    }
    return qa_net_reader_finish(&r);
}
bool qa_kex_discovery_write(qa_net_writer*w,const qa_kex_discovery*d) {
    if(!d||d->attribute_count>256||d->players>d->max_players||!memchr(d->name,0,sizeof(d->name)))return qa_net_writer_fail(w,"Invalid KEX discovery state");
    for(size_t i=0;i<d->attribute_count;i++) {
        if(!memchr(d->attributes[i].key,0,sizeof(d->attributes[i].key))||!memchr(d->attributes[i].value,0,sizeof(d->attributes[i].value)))return qa_net_writer_fail(w,"Unterminated KEX discovery attribute");
    }
    qa_kex_write_string(w,d->name);
    qa_kex_write_varint(w,d->players);
    qa_kex_write_varint(w,d->max_players);
    for(size_t i=0;i<d->attribute_count;i++) {
        qa_kex_write_string(w,d->attributes[i].key);
        qa_kex_write_string(w,d->attributes[i].value);
    }
    return !w->failed;
}
static bool domain(qa_net_writer*w,const char*s) {
    if(!s||!*s||strlen(s)>253)return qa_net_writer_fail(w,"Invalid mDNS domain");
    while(*s) {
        const char*dot=strchr(s,'.');
        size_t n=dot?(size_t)(dot-s):strlen(s);
        if(!n||n>63)return qa_net_writer_fail(w,"Invalid mDNS label");
        qa_net_write_u8(w,(uint8_t)n);
        qa_net_write_data(w,s,n);
        if(!dot)break;
        s=dot+1;
    }
    return qa_net_write_u8(w,0);
}
static bool record(qa_net_writer*w,const char*name,uint16_t type,uint32_t ttl,qa_bytes data) {
    if(data.size>65535)return qa_net_writer_fail(w,"mDNS record too large");
    return domain(w,name)&&word(w,type)&&word(w,type==12?1:0x8001)&&dword(w,ttl)&&word(w,(uint16_t)data.size)&&qa_net_write_data(w,data.data,data.size);
}
bool qa_kex_mdns_query(qa_net_writer*w) {
    word(w,0);
    word(w,0);
    word(w,1);
    word(w,0);
    word(w,0);
    word(w,0);
    return domain(w,"_game._udp.local")&&word(w,12)&&word(w,1);
}
bool qa_kex_mdns_announce(qa_net_writer*w,const char*host,uint16_t port,const qa_net_address*addresses,size_t count,uint32_t ttl) {
    if(!host||!port||(count&&!addresses)||count>253)return qa_net_writer_fail(w,"Invalid KEX mDNS advertisement");
    char target[71];
    size_t n=0;
    for(;host[n]&&n<63;n++) {
        unsigned char c=(unsigned char)host[n];
        target[n]=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')?(char)c:'-';
    }
    if(!n)target[n++]='q';
    memcpy(target+n,".local",7);
    size_t records=3;
    for(size_t i=0;i<count;i++)if(addresses[i].kind==QA_NET_IPV4||addresses[i].kind==QA_NET_IPV6)records++;
    word(w,0);
    word(w,0x8400);
    word(w,0);
    word(w,(uint16_t)records);
    word(w,0);
    word(w,0);
    uint8_t data[512];
    qa_net_writer r;
    qa_net_writer_init(&r,data,sizeof(data),w->error);
    domain(&r,"Quake II._game._udp.local");
    record(w,"_game._udp.local",12,ttl,(qa_bytes) {
        data,qa_net_writer_size(&r)
    });
    qa_net_writer_init(&r,data,sizeof(data),w->error);
    word(&r,0);
    word(&r,0);
    word(&r,port);
    domain(&r,target);
    record(w,"Quake II._game._udp.local",33,ttl,(qa_bytes) {
        data,qa_net_writer_size(&r)
    });
    data[0]=0;
    record(w,"Quake II._game._udp.local",16,ttl,(qa_bytes) {
        data,1
    });
    for(size_t i=0;i<count;i++) {
        if(addresses[i].kind==QA_NET_IPV4)record(w,target,1,ttl,(qa_bytes) {
            addresses[i].host.ipv4,4
        });
        else if(addresses[i].kind==QA_NET_IPV6)record(w,target,28,ttl,(qa_bytes) {
            addresses[i].host.ipv6.bytes,16
        });
    }
    return !w->failed&&!r.failed;
}
static bool dns_name(qa_net_reader*r,size_t position,bool advance,char*out,size_t capacity) {
    size_t seen[128],count=0,used=0,end=SIZE_MAX;
    for(;;) {
        if(position>=r->bytes.size||count==128)return qa_net_reader_fail(r,"Invalid mDNS name pointer");
        for(size_t i=0;i<count;i++)if(seen[i]==position)return qa_net_reader_fail(r,"mDNS name compression loop");
        seen[count++]=position;
        uint8_t n=r->bytes.data[position++];
        if(!n) {
            if(used>=capacity)return qa_net_reader_fail(r,"mDNS name exceeds output capacity");
            out[used]=0;
            if(advance)r->bit=(end==SIZE_MAX?position:end)*8;
            return true;
        }
        if((n&192)==192) {
            if(position>=r->bytes.size)return qa_net_reader_fail(r,"Truncated mDNS compression");
            if(end==SIZE_MAX)end=position+1;
            position=((size_t)(n&63)<<8)|r->bytes.data[position];
            continue;
        }
        if(n>63||n>r->bytes.size-position||used+n+(used?1:0)>=capacity)return qa_net_reader_fail(r,"Invalid mDNS label");
        if(!qa_kex_text_valid((qa_bytes){r->bytes.data+position,n}))return qa_net_reader_fail(r,"Invalid mDNS label UTF-8");
        if(used)out[used++]='.';
        for(unsigned i=0;i<n;i++) {
            unsigned char b=r->bytes.data[position++];
            if(!b)return qa_net_reader_fail(r,"Zero in mDNS label");
            out[used++]=(char)((b>='A'&&b<='Z')?b+32:b);
        }
    }
}
bool qa_kex_mdns_read(qa_bytes b,qa_kex_mdns_result*out,qa_error*e) {
    if(!out||!b.data||b.size<12||b.size>9000) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid mDNS packet size");
        return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r,b,e);
    read_word(&r);
    read_word(&r);
    uint32_t questions=read_word(&r),records=read_word(&r);
    records+=read_word(&r);
    records+=read_word(&r);
    if(questions+records>256)return qa_net_reader_fail(&r,"Too many mDNS records");
    out->question=false;
    out->endpoint_count=out->address_count=0;
    for(uint32_t i=0;i<questions;i++) {
        char name[256];
        if(!dns_name(&r,r.bit/8,true,name,sizeof(name)))return false;
        uint16_t type=read_word(&r);
        read_word(&r);
        if(!strcmp(name,"_game._udp.local")&&(type==12||type==255))out->question=true;
    }
    for(uint32_t i=0;i<records;i++) {
        char name[256];
        if(!dns_name(&r,r.bit/8,true,name,sizeof(name)))return false;
        uint16_t type=read_word(&r);
        read_word(&r);
        read_word(&r);
        read_word(&r);
        uint16_t length=read_word(&r);
        size_t start=r.bit/8;
        if(r.failed||length>qa_net_reader_remaining(&r))return qa_net_reader_fail(&r,"Truncated mDNS record");
        size_t name_size=strlen(name),suffix_size=strlen("._game._udp.local");
        if(type==33&&name_size>suffix_size&&!strcmp(name+name_size-suffix_size,"._game._udp.local")&&length>=7) {
            qa_kex_mdns_endpoint ep= {
                0
            };
            r.bit=(start+4)*8;
            ep.port=read_word(&r);
            if(!dns_name(&r,start+6,true,ep.target,sizeof(ep.target)))return false;
            if(r.bit/8!=start+length)return qa_net_reader_fail(&r,"Invalid mDNS service target length");
            memcpy(ep.instance,name,name_size+1);
            if(ep.port)out->endpoints[out->endpoint_count++]=ep;
        }  else if((type==1&&length==4)||(type==28&&length==16)) {
            qa_kex_mdns_address*a=&out->addresses[out->address_count++];
            memset(a,0,sizeof(*a));
            memcpy(a->target,name,name_size+1);
            a->address.kind=type==1?QA_NET_IPV4:QA_NET_IPV6;
            a->address.port=QA_KEX_LAN_PORT;
            if(type==1)memcpy(a->address.host.ipv4,b.data+start,4);
            else memcpy(a->address.host.ipv6.bytes,b.data+start,16);
        }
        r.bit=(start+length)*8;
    }
    return !r.failed;
}
