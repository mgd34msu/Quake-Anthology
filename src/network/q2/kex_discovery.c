#include "kex_discovery_owner_internal.h"
#include "qa/text.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
bool qa_kex_mdns_scratch_prepare(qa_arena *storage,qa_error *e) {
    if(!qa_arena_reserve(storage,QA_KEX_DNS_NAME_BYTES+QA_KEX_DNS_FOLDED_BYTES+2u,e))return false;
    qa_arena_seal(storage);
    return true;
}
static bool retain_name(qa_buffer name,qa_buffer*out,qa_error*e) {
    uint8_t*owned=malloc(name.size+1);
    if(!owned) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining mDNS name");
        return false;
    }
    memcpy(owned,name.data,name.size+1);
    *out=(qa_buffer){owned,name.size};
    return true;
}
static bool dns_name(qa_net_reader*r,size_t position,bool advance,bool fold,qa_arena*temporary,qa_buffer*out) {
    uint8_t text[QA_KEX_DNS_NAME_BYTES];
    size_t seen[128],count=0,labels=0,used=0,end=SIZE_MAX;
    for(;;) {
        if(position>=r->bytes.size||count==128)return qa_net_reader_fail(r,"Invalid mDNS name pointer");
        for(size_t i=0;i<count;i++)if(seen[i]==position)return qa_net_reader_fail(r,"mDNS name compression loop");
        seen[count++]=position;
        uint8_t n=r->bytes.data[position++];
        if(!n) {
            if(advance)r->bit=(end==SIZE_MAX?position:end)*8;
            if(fold)return qa_utf8_lower_arena((qa_bytes){text,used},temporary,out,r->error);
            uint8_t *owned=qa_arena_alloc(temporary,used+1,1,r->error);
            if(!owned)return false;
            if(used)memcpy(owned,text,used);
            owned[used]=0;
            *out=(qa_buffer){owned,used};
            return true;
        }
        if((n&192)==192) {
            if(position>=r->bytes.size)return qa_net_reader_fail(r,"Truncated mDNS compression");
            if(end==SIZE_MAX)end=position+1;
            position=((size_t)(n&63)<<8)|r->bytes.data[position];
            continue;
        }
        if(n>63||n>r->bytes.size-position||used+n+(labels?1:0)>=sizeof(text))return qa_net_reader_fail(r,"Invalid mDNS label");
        if(!qa_kex_text_valid((qa_bytes){r->bytes.data+position,n}))return qa_net_reader_fail(r,"Invalid mDNS label UTF-8");
        if(labels++)text[used++]='.';
        size_t skip=n>=3&&!memcmp(r->bytes.data+position,"\xef\xbb\xbf",3)?3:0;
        memcpy(text+used,r->bytes.data+position+skip,n-skip);
        used+=n-skip;
        position+=n;
    }
}
static bool service_suffix(qa_buffer name)
{
    const char *suffix="._game._udp.local";
    size_t extent=strlen(suffix);
    if(name.size<extent)return false;
    const uint8_t *tail=name.data+name.size-extent;
    for(size_t i=0;i<extent;i++) {
        unsigned char c=tail[i];
        if(c>='A'&&c<='Z')c=(unsigned char)(c+32);
        if(c!=(unsigned char)suffix[i])return false;
    }
    return true;
}
void qa_kex_mdns_result_free(qa_kex_mdns_result *r)
{
    if(!r)return;
    for(size_t i=0;i<r->endpoint_count;i++) {
        qa_buffer_free(&r->endpoints[i].instance);
        qa_buffer_free(&r->endpoints[i].target);
    }
    for(size_t i=0;i<r->address_count;i++)qa_buffer_free(&r->addresses[i].target);
    *r=(qa_kex_mdns_result){0};
}
bool qa_kex_mdns_read_scratch(qa_bytes b,qa_arena*temporary,qa_kex_mdns_result*out,qa_error*e) {
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
    *out=(qa_kex_mdns_result){0};
    qa_arena_reset(temporary);
    if(!qa_kex_mdns_scratch_prepare(temporary,e))goto failed;
    for(uint32_t i=0;i<questions;i++) {
        qa_buffer name={0};
        if(!dns_name(&r,r.bit/8,true,true,temporary,&name))goto failed;
        uint16_t type=read_word(&r);
        read_word(&r);
        if(name.size==16&&!memcmp(name.data,"_game._udp.local",16)&&(type==12||type==255))out->question=true;
        qa_arena_reset(temporary);
    }
    for(uint32_t i=0;i<records;i++) {
        qa_buffer name={0};
        if(!dns_name(&r,r.bit/8,true,false,temporary,&name))goto failed;
        uint16_t type=read_word(&r);
        read_word(&r);
        read_word(&r);
        read_word(&r);
        uint16_t length=read_word(&r);
        size_t start=r.bit/8;
        if(r.failed||length>qa_net_reader_remaining(&r)) {
            qa_net_reader_fail(&r,"Truncated mDNS record");goto failed;
        }
        if(type==33&&service_suffix(name)&&length>=7) {
            qa_kex_mdns_endpoint ep= {
                0
            };
            r.bit=(start+4)*8;
            ep.port=read_word(&r);
            qa_buffer target={0};
            if(!dns_name(&r,start+6,false,true,temporary,&target))goto failed;
            if(ep.port) {
                if(!retain_name(target,&ep.target,e))goto failed;
                if(!retain_name(name,&ep.instance,e)) { qa_buffer_free(&ep.target);goto failed; }
                out->endpoints[out->endpoint_count++]=ep;
            }
        }  else if((type==1&&length==4)||(type==28&&length==16)) {
            qa_kex_mdns_address*a=&out->addresses[out->address_count++];
            memset(a,0,sizeof(*a));
            qa_buffer target={0};
            if(!qa_utf8_lower_arena((qa_bytes){name.data,name.size},temporary,&target,e)||
                !retain_name(target,&a->target,e))goto failed;
            a->address.kind=type==1?QA_NET_IPV4:QA_NET_IPV6;
            a->address.port=QA_KEX_LAN_PORT;
            if(type==1)memcpy(a->address.host.ipv4,b.data+start,4);
            else memcpy(a->address.host.ipv6.bytes,b.data+start,16);
        }
        qa_arena_reset(temporary);
        r.bit=(start+length)*8;
    }
    if(!r.failed)return true;
failed:
    qa_kex_mdns_result_free(out);
    qa_arena_reset(temporary);
    return false;
}
bool qa_kex_mdns_read(qa_bytes b,qa_kex_mdns_result*out,qa_error*e) {
    qa_arena temporary={0};
    bool ok=qa_kex_mdns_read_scratch(b,&temporary,out,e);
    qa_arena_destroy(&temporary);
    return ok;
}
