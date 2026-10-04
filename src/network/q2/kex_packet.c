#include "qa/network_q2_kex.h"
#include "qa/text.h"
#include <string.h>
static uint16_t be16(const uint8_t*p) {
    return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}
static bool word(qa_net_writer*w,uint16_t n) {
    return qa_net_write_u8(w,(uint8_t)(n>>8))&&qa_net_write_u8(w,(uint8_t)n);
}
bool qa_kex_text_valid(qa_bytes b) {
    return qa_utf8_valid(b);
}
bool qa_kex_packet_read(qa_bytes b,qa_kex_packet*out,qa_error*e) {
    if(!out||!b.data||b.size<3||b.size>QA_KEX_DATAGRAM_BYTES) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX packet size");
        return false;
    }
    uint16_t h=be16(b.data);
    uint8_t f=(uint8_t)(h&15);
    bool seq=(f&2)!=0,cont=(f&8)!=0;
    size_t offset=seq?6:2;
    if((size_t)(h>>4)!=b.size||((f&1)&&!seq)||b.size<offset+(cont?0u:1u)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX packet framing");
        return false;
    }
    *out=(qa_kex_packet) {
        .flags=f,.kind=cont?0:b.data[offset],.has_kind=!cont,.sequence=seq?be16(b.data+2):0,.reliable=seq?be16(b.data+4):0,.payload= {
            b.data+offset+(cont?0:1),b.size-offset-(cont?0:1)
        }
    };
    return true;
}
bool qa_kex_packet_write(qa_net_writer*w,const qa_kex_packet*p) {
    if(!p)return qa_net_writer_fail(w,"Missing KEX packet");
    bool seq=(p->flags&2)!=0,cont=(p->flags&8)!=0;
    size_t header=(seq?6u:2u)+(cont?0u:1u);
    if(p->flags>15||((p->flags&1)&&!seq)||cont==p->has_kind||p->payload.size>QA_KEX_DATAGRAM_BYTES-header||(p->payload.size&&!p->payload.data))return qa_net_writer_fail(w,"Invalid KEX packet fields");
    word(w,(uint16_t)(((header+p->payload.size)<<4)|p->flags));
    if(seq) {
        word(w,p->sequence);
        word(w,p->reliable);
    }
    if(p->has_kind)qa_net_write_u8(w,p->kind);
    return qa_net_write_data(w,p->payload.data,p->payload.size);
}
uint64_t qa_kex_read_varint(qa_net_reader*r) {
    uint64_t v=0;
    for(unsigned shift=0;shift<70;shift+=7) {
        uint8_t b=qa_net_read_u8(r);
        if(r->failed)return 0;
        if(shift==63&&b>1) {
            qa_net_reader_fail(r,"KEX variable integer overflow");
            return 0;
        }
        v|=(uint64_t)(b&127)<<shift;
        if(!(b&128))return v;
    }
    qa_net_reader_fail(r,"Unterminated KEX integer");
    return 0;
}
bool qa_kex_write_varint(qa_net_writer*w,uint64_t v) {
    do {
        uint8_t b=(uint8_t)(v&127);
        v>>=7;
        if(!qa_net_write_u8(w,(uint8_t)(b|(v?128:0))))return false;
    }
    while(v);
    return true;
}
bool qa_kex_read_string(qa_net_reader*r,char*out,size_t cap) {
    uint64_t n=qa_kex_read_varint(r);
    if(r->failed)return false;
    if(!out||!cap||n>qa_net_reader_remaining(r))return qa_net_reader_fail(r,"KEX string exceeds capacity");
    qa_bytes text;
    if(!qa_net_read_bytes(r,(size_t)n,&text))return false;
    if(!qa_utf8_valid(text))return qa_net_reader_fail(r,"Invalid KEX string UTF-8");
    if(text.size>=3&&!memcmp(text.data,"\xef\xbb\xbf",3)) {
        text.data+=3;
        text.size-=3;
    }
    if(text.size>=cap||memchr(text.data,0,text.size))return qa_net_reader_fail(r,"KEX string exceeds its native text capacity");
    if(text.size)memcpy(out,text.data,text.size);
    out[text.size]=0;
    return true;
}
bool qa_kex_write_string(qa_net_writer*w,const char*s) {
    if(!s)return qa_net_writer_fail(w,"Missing KEX string");
    size_t n=strlen(s);
    if(!qa_utf8_valid((qa_bytes){(const uint8_t *)s,n}))return qa_net_writer_fail(w,"Invalid KEX string UTF-8");
    return qa_kex_write_varint(w,n)&&qa_net_write_data(w,s,n);
}
