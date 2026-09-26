#include "qa/network_q2_kex.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
struct pending {
    struct pending*next;
    uint16_t reliable;
    size_t size;
    uint8_t bytes[QA_KEX_DATAGRAM_BYTES];
};
struct qa_kex_channel {
    qa_kex_emit_fn emit;
    void*user;
    uint16_t sequence,reliable,incoming_sequence,incoming_reliable,fragment_sequence;
    uint8_t fragment_kind,ack;
    bool fragmented;
    struct pending*head,*tail;
    size_t pending_bytes,fragment_size,fragment_capacity,expanded_capacity;
    unsigned retries;
    uint64_t retry_at,received_at;
    uint8_t*fragments,*expanded;
};
static void free_pending(struct pending *p) {
    while(p) {
        struct pending *next=p->next;
        free(p);
        p=next;
    }
}
bool qa_kex_channel_create(qa_kex_emit_fn emit,void*user,qa_kex_channel**out,qa_error*e) {
    if(!emit||!out) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX channel arguments");
        return false;
    }
    qa_kex_channel*c=calloc(1,sizeof(*c));
    if(!c) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX channel allocation failed");
        return false;
    }
    c->emit=emit;
    c->user=user;
    *out=c;
    return true;
}
void qa_kex_channel_destroy(qa_kex_channel*c) {
    if(c) {
        while(c->head) {
            struct pending*p=c->head;
            c->head=p->next;
            free(p);
        }
        free(c->fragments);
        free(c->expanded);
        free(c);
    }
}
bool qa_kex_channel_send(qa_kex_channel*c,uint8_t kind,qa_bytes payload,qa_kex_mode mode,uint64_t now,qa_error*e) {
    if(!c||(payload.size&&!payload.data)||payload.size>QA_KEX_MESSAGE_BYTES||(mode!=QA_KEX_UNSEQUENCED&&mode!=QA_KEX_SEQUENCED&&mode!=QA_KEX_RELIABLE)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX send");
        return false;
    }
    qa_bytes data=payload;
    uint8_t*compressed=NULL;
    if(kind<127&&payload.size>=128) {
        uLongf size=compressBound((uLong)payload.size);
        compressed=malloc((size_t)size+1);
        if(!compressed) {
            qa_error_set(e,QA_ERROR_MEMORY,0,"KEX compression allocation failed");
            return false;
        }
        int status=compress2(compressed+1,&size,payload.data,(uLong)payload.size,Z_DEFAULT_COMPRESSION);
        if(status!=Z_OK) {
            free(compressed);
            qa_error_set(e,QA_ERROR_FORMAT,0,"KEX compression failed");
            return false;
        }
        if(size+1<payload.size) {
            compressed[0]=kind;
            kind=127;
            data=(qa_bytes) {
                compressed,(size_t)size+1
            };
        }
    }
    bool fragmented=data.size+(mode==QA_KEX_UNSEQUENCED?3u:7u)>QA_KEX_DATAGRAM_BYTES;
    if(fragmented&&mode==QA_KEX_UNSEQUENCED) {
        free(compressed);
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Unsequenced KEX message cannot fragment");
        return false;
    }
    size_t packet_count=data.size<=1393?1:1+(data.size-1393+1393)/1394;
    size_t queued_bytes=data.size+packet_count*6+1;
    if(mode==QA_KEX_RELIABLE&&queued_bytes>QA_KEX_MESSAGE_BYTES*2-c->pending_bytes) {
        free(compressed);
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"KEX reliable queue full");
        return false;
    }
    struct pending *staged_head=NULL,*staged_tail=NULL;
    uint16_t sequence=c->sequence,reliable=c->reliable;
    size_t at=0;
    bool first=true,accepted=true;
    do {
        size_t capacity=QA_KEX_DATAGRAM_BYTES-(mode==QA_KEX_UNSEQUENCED?2u:6u)-(first?1u:0u),count=data.size-at;
        if(count>capacity)count=capacity;
        bool final=at+count==data.size;
        uint8_t flags=(uint8_t)mode;
        if(fragmented)flags|=first?4:final?12:8;
        uint16_t seq=(uint16_t)(sequence+(mode!=QA_KEX_UNSEQUENCED?1:0)),rel=(uint16_t)(reliable+(mode==QA_KEX_RELIABLE?1:0));
        uint8_t bytes[QA_KEX_DATAGRAM_BYTES];
        qa_net_writer w;
        qa_net_writer_init(&w,bytes,sizeof(bytes),e);
        qa_kex_packet p= {
            .flags=flags,.sequence=seq,.reliable=rel,.kind=kind,.has_kind=first,.payload= {
                data.data?data.data+at:NULL,count
            }
        };
        if(!qa_kex_packet_write(&w,&p)) {
            free_pending(staged_head);
            free(compressed);
            return false;
        }
        size_t size=qa_net_writer_size(&w);
        struct pending*pending=NULL;
        if(mode==QA_KEX_RELIABLE) {
            pending=malloc(sizeof(*pending));
            if(!pending) {
                free_pending(staged_head);
                free(compressed);
                qa_error_set(e,QA_ERROR_MEMORY,0,"KEX reliable packet allocation failed");
                return false;
            }
            pending->next=NULL;
            pending->reliable=rel;
            pending->size=size;
            memcpy(pending->bytes,bytes,size);
            if(staged_tail)staged_tail->next=pending;
            else staged_head=pending;
            staged_tail=pending;
        } else {
            c->sequence=seq;
            if(!c->emit(c->user,(qa_bytes){bytes,size},e))accepted=false;
        }
        sequence=seq;
        reliable=rel;
        at+=count;
        first=false;
    }
    while(at<data.size);
    free(compressed);
    if(mode==QA_KEX_RELIABLE) {
        if(c->tail)c->tail->next=staged_head;
        else {
            c->head=staged_head;
            c->retry_at=now;
            c->retries=0;
        }
        c->tail=staged_tail;
        c->pending_bytes+=queued_bytes;
        c->sequence=sequence;
        c->reliable=reliable;
        for(struct pending *p=staged_head;p;p=p->next) {
            qa_error ignored={0};
            if(!c->emit(c->user,(qa_bytes){p->bytes,p->size},&ignored))break;
        }
    }
    return accepted;
}
static bool reserve(uint8_t**data,size_t*capacity,size_t required,qa_error*e) {
    if(required<=*capacity)return true;
    size_t next=*capacity?*capacity:1400;
    while(next<required) {
        if(next>QA_KEX_MESSAGE_BYTES/2) {
            next=QA_KEX_MESSAGE_BYTES;
            break;
        }
        next*=2;
    }
    if(required>QA_KEX_MESSAGE_BYTES) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX buffer exceeds message capacity");
        return false;
    }
    uint8_t*p=realloc(*data,next);
    if(!p) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX receive allocation failed");
        return false;
    }
    *data=p;
    *capacity=next;
    return true;
}
static bool expand(qa_kex_channel*c,uint8_t kind,qa_bytes b,qa_kex_message*out,qa_error*e) {
    if(kind!=127) {
        if(!reserve(&c->expanded,&c->expanded_capacity,b.size,e))return false;
        if(b.size)memcpy(c->expanded,b.data,b.size);
        *out=(qa_kex_message) {
            kind, {
                c->expanded,b.size
            }
        };
        return true;
    }
    if(!b.size||b.data[0]>=127) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid compressed KEX game kind");
        return false;
    }
    if(!reserve(&c->expanded,&c->expanded_capacity,QA_KEX_MESSAGE_BYTES,e))return false;
    z_stream z= {
        0
    };
    z.next_in=(Bytef*)(b.data+1);
    z.avail_in=(uInt)(b.size-1);
    z.next_out=c->expanded;
    z.avail_out=QA_KEX_MESSAGE_BYTES;
    int status=inflateInit(&z);
    if(status!=Z_OK) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX inflater initialization failed");
        return false;
    }
    status=inflate(&z,Z_FINISH);
    size_t size=(size_t)z.total_out;
    bool valid=status==Z_STREAM_END&&z.total_in==b.size-1;
    inflateEnd(&z);
    if(!valid) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Malformed compressed KEX message");
        return false;
    }
    *out=(qa_kex_message) {
        b.data[0], {
            c->expanded,size
        }
    };
    return true;
}
bool qa_kex_channel_receive(qa_kex_channel*c,qa_bytes bytes,uint64_t now,qa_kex_message*out,bool*present,qa_error*e) {
    if(!c||!out||!present) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX receive");
        return false;
    }
    *present=false;
    qa_kex_packet p;
    if(!qa_kex_packet_read(bytes,&p,e))return false;
    bool reliable=(p.flags&1)!=0,sequential=(p.flags&2)!=0;
    if(p.has_kind&&p.kind==130&&!p.flags) {
        if(p.payload.size!=3) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX acknowledgement");
            return false;
        }
        uint16_t ack=(uint16_t)(((uint16_t)p.payload.data[1]<<8)|p.payload.data[2]);
        while(c->head&&c->head->reliable<=ack) {
            struct pending*remove=c->head;
            c->head=remove->next;
            c->pending_bytes-=remove->size;
            free(remove);
            c->retry_at=now;
            c->retries=0;
        }
        if(!c->head)c->tail=NULL;
        c->received_at=now;
        return true;
    }
    if(reliable) {
        c->ack|=4;
        if(p.reliable!=(uint16_t)(c->incoming_reliable+1)) {
            c->ack|=2;
            return true;
        }
        c->incoming_reliable=p.reliable;
        c->ack&=(uint8_t)~2u;
    }  else if(sequential) {
        if(p.sequence==c->incoming_sequence)return true;
        uint32_t comparison=c->incoming_sequence;
        if(((p.sequence^comparison)&32768)&&(p.sequence&32768))comparison<<=16;
        else if(p.sequence>=comparison)comparison=(uint32_t)p.sequence+16384;
        if((uint32_t)(comparison-p.sequence)<16384||p.reliable!=c->incoming_reliable)return true;
    }
    if(sequential)c->incoming_sequence=p.sequence;
    c->received_at=now;
    uint8_t fragment=p.flags&12;
    if(!fragment) {
        if(!p.has_kind) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Missing KEX message kind");
            return false;
        }
        if(!expand(c,p.kind,p.payload,out,e))return false;
        *present=true;
        return true;
    }
    if(fragment==4) {
        if(!p.has_kind) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Missing KEX fragment kind");
            return false;
        }
        if(!reserve(&c->fragments,&c->fragment_capacity,p.payload.size,e))return false;
        c->fragmented=true;
        c->fragment_kind=p.kind;
        c->fragment_sequence=p.sequence;
        c->fragment_size=p.payload.size;
        if(p.payload.size)memcpy(c->fragments,p.payload.data,p.payload.size);
        return true;
    }
    if(!c->fragmented||p.sequence!=(uint16_t)(c->fragment_sequence+1)) {
        c->fragmented=false;
        return true;
    }
    if(p.payload.size>QA_KEX_MESSAGE_BYTES-c->fragment_size) {
        c->fragmented=false;
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX fragment message exceeds limit");
        return false;
    }
    if(!reserve(&c->fragments,&c->fragment_capacity,c->fragment_size+p.payload.size,e))return false;
    if(p.payload.size)memcpy(c->fragments+c->fragment_size,p.payload.data,p.payload.size);
    c->fragment_size+=p.payload.size;
    c->fragment_sequence=p.sequence;
    if(fragment==8)return true;
    c->fragmented=false;
    if(!expand(c,c->fragment_kind,(qa_bytes) {
        c->fragments,c->fragment_size
    },out,e))return false;
    *present=true;
    return true;
}
bool qa_kex_channel_tick(qa_kex_channel*c,uint64_t now,qa_error*e) {
    if(!c) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Missing KEX channel");
        return false;
    }
    if(c->ack) {
        uint8_t payload[3]= {
            (c->ack&2)?1:0,(uint8_t)(c->incoming_reliable>>8),(uint8_t)c->incoming_reliable
        },bytes[6];
        qa_net_writer w;
        qa_net_writer_init(&w,bytes,sizeof(bytes),e);
        qa_kex_packet p= {
            .kind=130,.has_kind=true,.payload= {
                payload,sizeof(payload)
            }
        };
        if(!qa_kex_packet_write(&w,&p)||!c->emit(c->user,(qa_bytes) {
            bytes,qa_net_writer_size(&w)
        },e))return false;
        c->ack=0;
    }
    if(c->head&&now>=c->retry_at&&now-c->retry_at>=UINT64_C(500000000)) {
        if(++c->retries>=40) {
            qa_error_set(e,QA_ERROR_IO,0,"KEX LAN peer timed out");
            return false;
        }
        c->retry_at=now;
        if(!c->emit(c->user,(qa_bytes) {
            c->head->bytes,c->head->size
        },e))return false;
    }
    if(!c->head&&now>=c->received_at&&now-c->received_at>=UINT64_C(5000000000)) {
        if(!qa_kex_channel_send(c,129,(qa_bytes) {
            0
        },QA_KEX_RELIABLE,now,e))return false;
        c->received_at=now;
    }
    return true;
}
