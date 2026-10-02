#include "channel_internal.h"
#include <stdlib.h>
#include <string.h>
static bool kex(const qa_q2_channel*c) {
    return c->options.protocol.kind==QA_NET_Q2KEX_2023;
}
static size_t qport_width(const qa_q2_channel*c) {
    return !c->options.new_channel&&c->options.protocol.kind==QA_NET_Q2_34?2:c->options.qport?1:0;
}
bool qa_q2_channel_create(const qa_q2_channel_options *o,qa_q2_channel**out,qa_error*e) {
    if(!o||!out||!qa_q2_protocol_version(o->protocol)||o->protocol.kind==QA_NET_Q2KEX_DEMO_2022||!qa_net_protocol_valid(o->protocol,e)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 channel options");
        return false;
    }
    qa_q2_channel*c=calloc(1,sizeof(*c));
    if(!c) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"Q2 channel allocation failed");
        return false;
    }
    c->options=*o;
    c->outgoing=1;
    c->id_recording=o->sequence_recording==QA_Q2_SEQUENCE_ID||(o->sequence_recording==QA_Q2_SEQUENCE_DEFAULT&&o->protocol.kind==QA_NET_Q2_34);
    size_t datagrams=o->datagram_bytes?o->datagram_bytes:65507;
    if(kex(c)) {
        c->capacity=65527;
        c->payload_bytes=65527;
        c->packet_bytes=65535;
    }  else {
        if(datagrams<524||datagrams>65507)goto invalid;
        c->payload_bytes=o->payload_bytes?o->payload_bytes:1390;
        if(c->payload_bytes>datagrams-12)c->payload_bytes=datagrams-12;
        c->capacity=o->message_bytes?o->message_bytes:o->new_channel?32768:c->id_recording?1384:c->payload_bytes;
        if(!o->new_channel&&c->capacity>datagrams-10)c->capacity=datagrams-10;
        c->packet_bytes=!o->new_channel&&c->id_recording?1400:4096;
        if(c->packet_bytes>datagrams)c->packet_bytes=datagrams;
        if(c->payload_bytes<512||c->payload_bytes>4086||!c->capacity||c->capacity>32768)goto invalid;
    }
    c->queued=malloc(c->capacity);
    c->reliable=malloc(c->capacity);
    c->sending=malloc(kex(c)?65527:32768);
    c->receiving=malloc(kex(c)?65527:32768);
    c->packet=malloc(c->packet_bytes);
    if(!c->queued||!c->reliable||!c->sending||!c->receiving||!c->packet) {
        qa_q2_channel_destroy(c);
        qa_error_set(e,QA_ERROR_MEMORY,0,"Q2 channel buffers allocation failed");
        return false;
    }
    *out=c;
    return true;
    invalid:free(c);
    qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 channel limits");
    return false;
}
void qa_q2_channel_destroy(qa_q2_channel*c) {
    if(c) {
        free(c->queued);
        free(c->reliable);
        free(c->sending);
        free(c->receiving);
        free(c->packet);
        free(c);
    }
}
bool qa_q2_channel_queue(qa_q2_channel*c,qa_bytes b,qa_error*e) {
    if(!c||(b.size&&!b.data)||b.size>c->capacity-c->queued_size) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Q2 reliable queue exceeds capacity");
        return false;
    }
    if(b.size)memcpy(c->queued+c->queued_size,b.data,b.size);
    c->queued_size+=b.size;
    return true;
}
static bool header(qa_q2_channel*c,qa_net_writer*w,bool reliable,bool fragmented) {
    uint32_t mask=c->options.new_channel?0x3fffffff:0x7fffffff;
    qa_net_write_u32(w,(c->outgoing&mask)|(reliable?0x80000000u:0)|(fragmented?0x40000000u:0));
    qa_net_write_u32(w,(c->incoming&mask)|(c->incoming_reliable?0x80000000u:0));
    if(!c->options.server) {
        size_t width=qport_width(c);
        if(width==2)qa_net_write_u16(w,c->options.qport);
        else if(width==1)qa_net_write_u8(w,(uint8_t)c->options.qport);
    }
    return !w->failed;
}
static bool fragment(qa_q2_channel*c,qa_net_writer*w,uint64_t now) {
    if(!header(c,w,c->sending_reliable,true))return false;
    size_t size=qa_net_writer_size(w);
    if(size+2>=c->packet_bytes)return qa_net_writer_fail(w,"Q2 packet header exceeds capacity");
    size_t writable=c->packet_bytes-size-2;
    if(writable>c->payload_bytes)writable=c->payload_bytes;
    size_t count=c->sending_size-c->sending_offset;
    if(count>writable)count=writable;
    bool more=c->sending_offset+count<c->sending_size;
    if(!qa_net_write_u16(w,(uint16_t)(c->sending_offset|(more?0x8000:0)))||!qa_net_write_data(w,c->sending+c->sending_offset,count))return false;
    c->sending_offset+=count;
    if(!more) {
        c->sending_size=0;
        c->sending_offset=0;
        c->outgoing++;
        c->sent_ns=now;
    }
    return true;
}
bool qa_q2_channel_transmit(qa_q2_channel*c,qa_bytes unrel,uint64_t now,qa_net_writer*w,bool*included) {
    if(!c||!w||!included||(unrel.size&&!unrel.data))return qa_net_writer_fail(w,"Invalid Q2 channel transmit");
    *included=false;
    if(w->bit!=0)return qa_net_writer_fail(w,"Q2 channel requires an empty packet writer");
    if(kex(c)) {
        bool reliable=c->queued_size!=0;
        qa_bytes payload=reliable?(qa_bytes) {
            c->queued,c->queued_size
        }
        :unrel;
        if(payload.size>c->capacity)return qa_net_writer_fail(w,"KEX game message exceeds capacity");
        qa_net_write_u32(w,reliable?0x80000000u:c->outgoing&0x7fffffffu);
        qa_net_write_u32(w,reliable?0x80000000u:c->incoming&0x7fffffffu);
        qa_net_write_data(w,payload.data,payload.size);
        if(w->failed)return false;
        if(reliable)c->queued_size=0;
        else {
            c->outgoing++;
            *included=true;
        }
        c->sent_ns=now;
        return true;
    }
    if(c->sending_size)return fragment(c,w,now);
    bool reliable=c->incoming_ack>c->last_reliable&&c->incoming_reliable_ack!=c->reliable_bit;
    if(!c->reliable_size&&c->queued_size) {
        qa_buffer wrapped= {
            0
        };
        bool compressed=false;
        if(c->options.server&&c->options.compress) {
            uint8_t opcode=(c->options.protocol.kind==QA_NET_Q2REPRO_1038||c->options.protocol.kind==QA_NET_Q2PRIVATE_4038)?34:21;
            if(!qa_q2_zpacket_wrap((qa_bytes) {
                c->queued,c->queued_size
            },c->capacity,opcode,&wrapped,&compressed,w->error)) {
                w->failed=true;
                return false;
            }
        }
        c->reliable_size=compressed?wrapped.size:c->queued_size;
        memcpy(c->reliable,compressed?wrapped.data:c->queued,c->reliable_size);
        qa_buffer_free(&wrapped);
        c->queued_size=0;
        c->reliable_bit=!c->reliable_bit;
        reliable=true;
    }
    size_t rsize=reliable?c->reliable_size:0;
    if(c->options.new_channel&&(unrel.size>c->payload_bytes||rsize>c->payload_bytes-unrel.size)) {
        size_t n=rsize;
        if(unrel.size<=32768-rsize) {
            n+=unrel.size;
            *included=true;
        }
        if(!n)return qa_net_writer_fail(w,"Q2 unreliable message exceeds assembly capacity");
        if(rsize)memcpy(c->sending,c->reliable,rsize);
        if(*included&&unrel.size)memcpy(c->sending+rsize,unrel.data,unrel.size);
        c->sending_size=n;
        c->sending_offset=0;
        c->sending_reliable=reliable;
        if(reliable)c->last_reliable=c->outgoing;
        return fragment(c,w,now);
    }
    if(!header(c,w,reliable,false))return false;
    if(rsize>c->packet_bytes-qa_net_writer_size(w))return qa_net_writer_fail(w,"Q2 reliable packet exceeds negotiated datagram");
    qa_net_write_data(w,c->reliable,rsize);
    if(unrel.size<=c->packet_bytes-qa_net_writer_size(w)) {
        qa_net_write_data(w,unrel.data,unrel.size);
        *included=true;
    }
    if(w->failed)return false;
    if(reliable)c->last_reliable=c->outgoing+(c->id_recording?1u:0u);
    c->outgoing++;
    c->ack_pending=false;
    c->sent_ns=now;
    return true;
}
static bool rejected(qa_q2_received*out,qa_q2_reject why) {
    out->kind=QA_Q2_REJECTED;
    out->rejected=why;
    return true;
}
bool qa_q2_channel_receive(qa_q2_channel*c,qa_bytes b,uint64_t now,qa_q2_received*out,qa_error*e) {
    if(!c||!out||(b.size&&!b.data)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 channel receive");
        return false;
    }
    memset(out,0,sizeof(*out));
    if(b.size<8)return rejected(out,QA_Q2_REJECT_SHORT);
    qa_net_reader r;
    qa_net_reader_init(&r,b,e);
    uint32_t sw=qa_net_read_u32(&r),aw=qa_net_read_u32(&r);
    if(kex(c)&&sw==0x80000000u&&aw==0x80000000u) {
        out->kind=QA_Q2_MESSAGE;
        out->sequence=c->incoming;
        out->acknowledged=c->incoming_ack;
        out->payload=(qa_bytes) {
            b.data+8,b.size-8
        };
        c->received_ns=now;
        return true;
    }
    if(!kex(c)&&c->options.server) {
        size_t width=qport_width(c);
        if(b.size<8+width)return rejected(out,QA_Q2_REJECT_SHORT);
        uint16_t port=width==2?qa_net_read_u16(&r):width==1?qa_net_read_u8(&r):0,expected=width==2?c->options.qport:(uint8_t)c->options.qport;
        if(port!=expected)return rejected(out,QA_Q2_REJECT_QPORT);
    }
    uint32_t mask=!kex(c)&&c->options.new_channel?0x3fffffff:0x7fffffff,sequence=sw&mask,ack=aw&mask;
    bool reliable=(sw>>31)!=0,fragmented=!kex(c)&&c->options.new_channel&&(sw&0x40000000u)!=0;
    if(fragmented&&qa_net_reader_remaining(&r)<2)return rejected(out,QA_Q2_REJECT_SHORT);
    uint16_t fw=fragmented?qa_net_read_u16(&r):0;
    if(sequence<=c->incoming)return rejected(out,QA_Q2_REJECT_SEQUENCE);
    size_t offset=r.bit/8;
    qa_bytes payload= {
        b.data+offset,b.size-offset
    };
    if(!kex(c)) {
        c->incoming_reliable_ack=(aw>>31)!=0;
        if(c->incoming_reliable_ack==c->reliable_bit)c->reliable_size=0;
    }
    if(fragmented) {
        if(sequence!=c->receive_sequence) {
            c->receive_sequence=sequence;
            c->receive_size=0;
        }
        if((fw&0x7fff)!=c->receive_size)return rejected(out,QA_Q2_REJECT_FRAGMENT_ORDER);
        if(payload.size>32768-c->receive_size)return rejected(out,QA_Q2_REJECT_FRAGMENT_SIZE);
        if(payload.size)memcpy(c->receiving+c->receive_size,payload.data,payload.size);
        c->receive_size+=payload.size;
        if(fw&0x8000) {
            out->kind=QA_Q2_FRAGMENT;
            out->sequence=sequence;
            out->received_bytes=c->receive_size;
            return true;
        }
        payload=(qa_bytes) {
            c->receiving,c->receive_size
        };
        c->receive_size=0;
    }
    out->kind=QA_Q2_MESSAGE;
    out->sequence=sequence;
    out->acknowledged=ack;
    out->dropped=sequence-c->incoming-1;
    out->payload=payload;
    c->incoming=sequence;
    c->incoming_ack=ack;
    if(!kex(c)&&reliable) {
        c->ack_pending=true;
        c->incoming_reliable=!c->incoming_reliable;
    }
    c->received_ns=now;
    return true;
}
bool qa_q2_channel_pending(const qa_q2_channel*c) {
    return c&&(c->queued_size||c->reliable_size);
}
bool qa_q2_channel_fragment_pending(const qa_q2_channel*c) {
    return c&&c->sending_size;
}
bool qa_q2_channel_should_update(const qa_q2_channel*c,uint64_t now) {
    return c&&(c->queued_size||c->ack_pending||c->sending_size||(now>c->sent_ns&&now-c->sent_ns>UINT64_C(1000000000)));
}
uint32_t qa_q2_channel_incoming(const qa_q2_channel*c) {
    return c?c->incoming:0;
}
uint32_t qa_q2_channel_outgoing(const qa_q2_channel*c) {
    return c?c->outgoing:0;
}
bool qa_q2_channel_send(qa_q2_channel*c,qa_net_transport*t,const qa_net_address*to,qa_bytes unreliable,uint64_t now,bool*included,qa_error*e) {
    if(!c||!t||!to||!included||(unreliable.size&&!unreliable.data)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid Q2 channel send");
        return false;
    }
    *included=false;
    size_t capacity=kex(c)?65535:c->packet_bytes;
    uint8_t*bytes=c->packet;
    qa_net_writer w;
    if(kex(c)) {
        if(c->queued_size) {
            qa_net_writer_init(&w,bytes,capacity,e);
            qa_net_write_u32(&w,0x80000000u);
            qa_net_write_u32(&w,0x80000000u);
            qa_net_write_data(&w,c->queued,c->queued_size);
            if(w.failed||!qa_net_transport_send(t,to,(qa_bytes) {
                bytes,qa_net_writer_size(&w)
            },e))return false;
            c->queued_size=0;
            c->sent_ns=now;
        }
        if(unreliable.size) {
            if(unreliable.size>c->capacity) {
                qa_error_set(e,QA_ERROR_ARGUMENT,0,"KEX game message exceeds capacity");
                return false;
            }
            qa_net_writer_init(&w,bytes,capacity,e);
            qa_net_write_u32(&w,c->outgoing&0x7fffffffu);
            qa_net_write_u32(&w,c->incoming&0x7fffffffu);
            qa_net_write_data(&w,unreliable.data,unreliable.size);
            if(w.failed||!qa_net_transport_send(t,to,(qa_bytes) {
                bytes,qa_net_writer_size(&w)
            },e))return false;
            c->outgoing++; *included=true;
            c->sent_ns=now;
        } else *included=true;
        return true;
    }
    qa_q2_channel before=*c;
    qa_net_writer_init(&w,bytes,capacity,e);
    bool ok=qa_q2_channel_transmit(c,unreliable,now,&w,included);
    if(ok)ok=qa_net_transport_send(t,to,(qa_bytes) {
        bytes,qa_net_writer_size(&w)
    },e);
    if(!ok) { *c=before; *included=false; }
    return ok;
}
bool qa_q2_channel_get_status(const qa_q2_channel *c,qa_q2_channel_status *out) {
    if(!c||!out)return false;
    *out=(qa_q2_channel_status) {
        .incoming=c->incoming,.outgoing=c->outgoing,
        .acknowledged=c->incoming_ack,.reliable_pending=c->queued_size||c->reliable_size,
        .can_reliable=c->reliable_size==0,.fragment_pending=c->sending_size!=0,
        .acknowledgement_pending=c->ack_pending,.sent_ns=c->sent_ns,.received_ns=c->received_ns,
        .capacity=c->capacity,.payload_bytes=c->payload_bytes
    };
    return true;
}
