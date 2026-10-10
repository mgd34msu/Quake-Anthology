#include "channel_internal.h"

#include <stdlib.h>
#include <string.h>

static bool error_message(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error,code,0,"%s",message);
    return false;
}

qa_unified_limits qa_unified_limits_default(void) {
    return (qa_unified_limits){
        .datagram_bytes=1200, .message_bytes=4u*1024u*1024u,
        .queued_reliable_bytes=8u*1024u*1024u,
        .queued_reliable_messages=256, .reliable_window_messages=8,
        .fragments=8192, .packets_per_flush=32, .maximum_transmissions=40,
        .retry_ns=UINT64_C(250000000), .assembly_ns=UINT64_C(30000000000)
    };
}

static uint32_t window(const qa_unified_channel *c) {
    return c->limits.reliable_window_messages<c->limits.queued_reliable_messages
        ? c->limits.reliable_window_messages:c->limits.queued_reliable_messages;
}

static void close_channel(qa_unified_channel *c) {
    c->closed=true;
    while (c->reliable) {
        outgoing *next=c->reliable->next;
        qa_unified_outgoing_release(c, c->reliable); c->reliable=next;
    }
    c->tail=NULL; c->reliable_count=0; c->queued_bytes=0;
    qa_unified_outgoing_release(c, c->frame); c->frame=NULL;
    qa_unified_outgoing_release(c, c->pending_frame); c->pending_frame=NULL;
    for (size_t i=0;i<64;++i) { qa_unified_assembly_release(c, c->assemblies[i]); c->assemblies[i]=NULL; }
    qa_unified_assembly_release(c, c->frame_assembly); c->frame_assembly=NULL;
    qa_unified_assembly_release(c, c->waiting_frame); c->waiting_frame=NULL;
    c->received_bytes=0; c->cumulative_pending=false; c->frame_ack_pending=false;
}

void qa_unified_channel_close(qa_unified_channel *c) { if (c && !c->busy) close_channel(c); }
void qa_unified_channel_destroy(qa_unified_channel *c) {
    if (!c || c->busy) return;
    close_channel(c); qa_arena_destroy(&c->storage); free(c);
}
bool qa_unified_channel_closed(const qa_unified_channel *c) { return !c || c->closed; }
uint32_t qa_unified_channel_received(const qa_unified_channel *c) { return c?c->reliable_received:0; }
uint32_t qa_unified_channel_acknowledged(const qa_unified_channel *c) { return c?c->reliable_acknowledged:0; }

bool qa_unified_channel_create(qa_unified_token token, const qa_unified_limits *limits,
                                qa_unified_channel **out, qa_error *error) {
    qa_unified_limits l=limits?*limits:qa_unified_limits_default();
    if (!out || l.datagram_bytes<=QA_UNIFIED_HEADER_BYTES || l.datagram_bytes>QA_UNIFIED_MAX_DATAGRAM ||
        !l.message_bytes || l.queued_reliable_bytes<l.message_bytes || !l.queued_reliable_messages ||
        !l.reliable_window_messages || l.reliable_window_messages>64 || !l.fragments ||
        !l.packets_per_flush || !l.maximum_transmissions || !l.retry_ns || !l.assembly_ns)
        return error_message(error,QA_ERROR_ARGUMENT,"invalid unified channel capacity");
    qa_unified_channel *c=calloc(1,sizeof(*c));
    if (!c) return error_message(error,QA_ERROR_MEMORY,"allocating unified channel");
    c->limits=l; c->token=token; c->next_reliable=1; c->next_frame=1;
    if (!qa_unified_channel_storage_prepare(c,error)) {
        qa_arena_destroy(&c->storage); free(c); return false;
    }
    *out=c; return true;
}

static bool writable_channel(const qa_unified_channel *c, qa_error *error) {
    if (!c || c->closed) return error_message(error,QA_ERROR_ARGUMENT,"unified channel is closed");
    return true;
}

static bool open_channel(const qa_unified_channel *c, qa_error *error) {
    if (!writable_channel(c,error)) return false;
    if (c->busy) return error_message(error,QA_ERROR_ARGUMENT,"unified channel callback reentry");
    return true;
}

bool qa_unified_channel_frame_applied(qa_unified_channel *c, uint32_t sequence, qa_error *error) {
    if (!open_channel(c,error) || !sequence || sequence<c->frame_admitted || sequence>c->frame_received)
        return error_message(error,QA_ERROR_ARGUMENT,"Unified frame acknowledgement lacks its applied receive receipt");
    c->frame_admitted=sequence; c->frame_ack_pending=true; return true;
}

static outgoing *outgoing_create(qa_unified_channel *c, qa_bytes payload,
                                  uint64_t sequence, uint32_t required, bool reliable,
                                  qa_error *error) {
    if (payload.size>c->limits.message_bytes || (payload.size && !payload.data) || sequence>UINT32_MAX) {
        error_message(error,QA_ERROR_ARGUMENT,"unified message capacity or sequence exhausted"); return NULL;
    }
    size_t fragment_bytes=c->limits.datagram_bytes-QA_UNIFIED_HEADER_BYTES;
    size_t fragments=payload.size?(payload.size-1)/fragment_bytes+1:1;
    if (fragments>c->limits.fragments) {
        error_message(error,QA_ERROR_ARGUMENT,"unified message exceeds fragment limit"); return NULL;
    }
    outgoing *message=qa_unified_outgoing_acquire(c,payload.size,(uint16_t)fragments,reliable,error);
    if (!message) return NULL;
    if (payload.size) qa_unified_payload_copy(&message->payload,0,payload.data,payload.size);
    message->sequence=(uint32_t)sequence; message->required=required;
    return message;
}

bool qa_unified_channel_reliable(qa_unified_channel *c, qa_bytes payload,
                                  uint32_t *sequence, qa_error *error) {
    if (!writable_channel(c,error)) return false;
    if (c->reliable_count>=c->limits.queued_reliable_messages ||
        payload.size>c->limits.queued_reliable_bytes-c->queued_bytes)
        return error_message(error,QA_ERROR_ARGUMENT,"unified reliable queue overflow");
    outgoing *message=outgoing_create(c,payload,c->next_reliable,0,true,error);
    if (!message) return false;
    if (c->tail) c->tail->next=message; else c->reliable=message;
    c->tail=message; c->reliable_count++; c->queued_bytes+=payload.size;
    c->next_reliable++;
    if (sequence) *sequence=message->sequence;
    return true;
}

bool qa_unified_channel_reliable_ready(const qa_unified_channel *c, const qa_bytes *payloads,
                                        size_t count, bool *ready, qa_error *error) {
    if (!open_channel(c,error) || !ready || (count && !payloads))
        return error_message(error,QA_ERROR_ARGUMENT,"invalid unified reliable admission owner");
    *ready=false;
    if (count>c->limits.queued_reliable_messages ||
        count>(uint64_t)UINT32_MAX+1-c->next_reliable)
        return error_message(error,QA_ERROR_ARGUMENT,"unified reliable batch exceeds permanent sequence or message capacity");
    size_t bytes=0,fragment_bytes=c->limits.datagram_bytes-QA_UNIFIED_HEADER_BYTES;
    for (size_t i=0;i<count;++i) {
        qa_bytes payload=payloads[i];
        size_t fragments=payload.size?(payload.size-1)/fragment_bytes+1:1;
        if (payload.size>c->limits.message_bytes || (payload.size && !payload.data) ||
            fragments>c->limits.fragments || payload.size>c->limits.queued_reliable_bytes-bytes)
            return error_message(error,QA_ERROR_ARGUMENT,"unified reliable batch exceeds permanent payload capacity");
        bytes+=payload.size;
    }
    *ready=count<=c->limits.queued_reliable_messages-c->reliable_count &&
        bytes<=c->limits.queued_reliable_bytes-c->queued_bytes;
    return true;
}

bool qa_unified_channel_reliable_batch(qa_unified_channel *c, const qa_bytes *payloads,
                                       size_t count, uint32_t *first, uint32_t *last,
                                       qa_error *error) {
    if (!open_channel(c,error) || !first || !last || (count && !payloads))
        return error_message(error,QA_ERROR_ARGUMENT,"invalid unified reliable batch owner");
    bool ready=false;
    if (!qa_unified_channel_reliable_ready(c,payloads,count,&ready,error)) return false;
    if (!ready) return error_message(error,QA_ERROR_ARGUMENT,"unified reliable batch awaits queue capacity");
    size_t bytes=0;
    for (size_t i=0;i<count;++i) {
        bytes+=payloads[i].size;
    }
    outgoing *head=NULL,*tail=NULL;
    for (size_t i=0;i<count;++i) {
        outgoing *m=outgoing_create(c,payloads[i],c->next_reliable+i,0,true,error);
        if (!m) {
            while (head) { outgoing *next=head->next; qa_unified_outgoing_release(c, head); head=next; }
            return false;
        }
        if (tail) tail->next=m; else head=m;
        tail=m;
    }
    *first=head?head->sequence:0; *last=tail?tail->sequence:0;
    if (!head) return true;
    if (c->tail) c->tail->next=head; else c->reliable=head;
    c->tail=tail; c->reliable_count+=(uint32_t)count; c->queued_bytes+=bytes;
    c->next_reliable+=count;
    return true;
}

bool qa_unified_channel_frame(qa_unified_channel *c, qa_bytes payload,
                               uint32_t required_reliable, qa_error *error) {
    if (!writable_channel(c,error)) return false;
    if (required_reliable>=c->next_reliable)
        return error_message(error,QA_ERROR_ARGUMENT,"unified frame depends on unqueued control");
    size_t fragment_bytes=c->limits.datagram_bytes-QA_UNIFIED_HEADER_BYTES;
    size_t fragments=payload.size?(payload.size-1)/fragment_bytes+1:1;
    if (payload.size>c->limits.message_bytes || (payload.size && !payload.data) ||
        c->next_frame>UINT32_MAX)
        return error_message(error,QA_ERROR_ARGUMENT,"unified message capacity or sequence exhausted");
    if (fragments>c->limits.fragments)
        return error_message(error,QA_ERROR_ARGUMENT,"unified message exceeds fragment limit");
    qa_unified_outgoing_release(c,c->pending_frame); c->pending_frame=NULL;
    outgoing *message=outgoing_create(c,payload,c->next_frame,required_reliable,false,error);
    if (!message) return false;
    c->next_frame++;
    if (!c->frame) c->frame=message;
    else { qa_unified_outgoing_release(c, c->pending_frame); c->pending_frame=message; }
    return true;
}

static bool elapsed(uint64_t now, uint64_t then, uint64_t duration) { return now>=then && now-then>=duration; }

static bool expire(qa_unified_channel *c, uint64_t now, qa_error *error) {
    for (size_t i=0;i<64;++i) {
        if (c->assemblies[i] && c->assemblies[i]->received_count<c->assemblies[i]->fragments &&
            elapsed(now,c->assemblies[i]->started,c->limits.assembly_ns)) {
            close_channel(c); return error_message(error,QA_ERROR_IO,"unified reliable assembly timed out");
        }
    }
    if (c->frame_assembly && elapsed(now,c->frame_assembly->started,c->limits.assembly_ns)) {
        qa_unified_assembly_release(c, c->frame_assembly); c->frame_assembly=NULL;
    }
    return true;
}

static assembly **assembly_slot(qa_unified_channel *c, uint32_t sequence) {
    for (size_t i=0;i<64;++i)
        if (c->assemblies[i] && c->assemblies[i]->sequence==sequence) return &c->assemblies[i];
    return NULL;
}

static assembly *assembly_create(qa_unified_channel *c,const qa_unified_packet *p, uint64_t now, qa_error *error) {
    assembly *a=qa_unified_assembly_acquire(c,p->total_bytes,p->fragments,p->kind==QA_UNIFIED_RELIABLE,p->sequence,error);
    if (!a) return NULL;
    a->sequence=p->sequence; a->required=p->required_reliable;
    a->fragment_bytes=p->fragment_bytes; a->started=now;
    return a;
}

static bool append(assembly *a, const qa_unified_packet *p) {
    if (a->required!=p->required_reliable || a->payload.size!=p->total_bytes ||
        a->fragment_bytes!=p->fragment_bytes || a->fragments!=p->fragments) return false;
    if (!a->received[p->fragment]) {
        if (p->payload.size) qa_unified_payload_copy(&a->payload,(size_t)p->fragment*p->fragment_bytes,p->payload.data,p->payload.size);
        a->received[p->fragment]=1; a->received_count++;
    }
    if (a->pending_ack) a->pending_ack[p->fragment]=1;
    return true;
}

static void cumulative_ack(qa_unified_channel *c, uint32_t sequence, uint16_t fragment) {
    c->cumulative_pending=true; c->cumulative_sequence=sequence; c->cumulative_fragment=fragment;
}

static void accept_ack(qa_unified_channel *c, const qa_unified_packet *p) {
    if (p->acknowledged_frame>c->frame_acknowledged && p->acknowledged_frame<=c->frame_transmitted)
        c->frame_acknowledged=p->acknowledged_frame;
    outgoing *message=c->reliable;
    uint32_t prefix=0;
    while (message && prefix<window(c)) {
        if (message->next_fragment!=message->fragments) break;
        prefix++;
        if (message->sequence==p->acknowledged_reliable) {
            for (uint32_t i=0;i<prefix;++i) {
                outgoing *done=c->reliable; c->reliable=done->next;
                c->queued_bytes-=done->payload.size; c->reliable_count--;
                qa_unified_outgoing_release(c, done);
            }
            if (!c->reliable) c->tail=NULL;
            c->reliable_acknowledged=p->acknowledged_reliable;
            break;
        }
        message=message->next;
    }
    if (p->kind!=QA_UNIFIED_ACK) return;
    message=c->reliable;
    for (uint32_t i=0;message && i<window(c);++i,message=message->next) {
        if (message->sequence==p->sequence && p->fragment<message->fragments &&
            qa_unified_sent_fragment(message,p->fragment)->attempts && !qa_unified_sent_fragment(message,p->fragment)->acknowledged) {
            qa_unified_sent_fragment(message,p->fragment)->acknowledged=true; message->acknowledged++; break;
        }
    }
}

static bool deliver_frame(qa_unified_channel *c, qa_unified_admit_delivery_fn admit,
                           qa_unified_deliver_fn deliver, void *context, qa_error *error) {
    assembly *frame=c->waiting_frame;
    if (!frame || frame->required>c->reliable_received) return true;
    if (frame->sequence>c->frame_received) {
        qa_unified_delivery value={QA_UNIFIED_FRAME,frame->sequence,frame->required,{frame->payload.data,frame->payload.size}};
        if (admit && !admit(context,&value)) return true;
        bool accepted=false;
        if (!deliver(context,&value,&accepted,error)) { close_channel(c); return false; }
        if (!accepted) return true;
        c->frame_received=frame->sequence;
    }
    c->waiting_frame=NULL; qa_unified_assembly_release(c, frame); return true;
}

static bool deliver_ready(qa_unified_channel *c, qa_unified_admit_delivery_fn admit,
                           qa_unified_deliver_fn deliver, void *context, qa_error *error) {
    while (c->reliable_received<UINT32_MAX) {
        assembly **ready_slot=assembly_slot(c,c->reliable_received+1);
        if (!ready_slot) break;
        assembly *ready=*ready_slot;
        qa_unified_assembly_promote_head(c,ready);
        if (ready->received_count!=ready->fragments) break;
        qa_unified_delivery value={QA_UNIFIED_RELIABLE,ready->sequence,0,{ready->payload.data,ready->payload.size}};
        if (admit && !admit(context,&value)) break;
        bool accepted=false;
        if (!deliver(context,&value,&accepted,error)) { close_channel(c); return false; }
        if (!accepted) break;
        c->reliable_received=ready->sequence; c->received_bytes-=ready->payload.size;
        cumulative_ack(c,ready->sequence,(uint16_t)(ready->fragments-1));
        *ready_slot=NULL; qa_unified_assembly_release(c, ready);
    }
    return deliver_frame(c,admit,deliver,context,error);
}

static bool receive_reliable(qa_unified_channel *c, const qa_unified_packet *p, uint64_t now,
                               qa_unified_admit_delivery_fn admit, qa_unified_deliver_fn deliver,
                               void *context, qa_error *error) {
    if (p->sequence<=c->reliable_received) { cumulative_ack(c,p->sequence,p->fragment); return true; }
    if ((uint64_t)p->sequence>(uint64_t)c->reliable_received+window(c)) return true;
    assembly **slot=assembly_slot(c,p->sequence);
    if (!slot) {
        size_t reserve=0;
        for (uint64_t seq=(uint64_t)c->reliable_received+1;seq<p->sequence;++seq) {
            if (!assembly_slot(c,(uint32_t)seq)) { reserve=c->limits.message_bytes; break; }
        }
        size_t available=c->limits.queued_reliable_bytes-c->received_bytes;
        if (reserve>available || p->total_bytes>available-reserve) return true;
        for (size_t i=0;i<64;++i) if (!c->assemblies[i]) { slot=&c->assemblies[i]; break; }
        if (!slot) return true;
        *slot=assembly_create(c,p,now,error);
        if (!*slot) return false;
        c->received_bytes+=p->total_bytes;
    }
    if (!append(*slot,p)) return true;
    return deliver_ready(c,admit,deliver,context,error);
}

static bool receive_frame(qa_unified_channel *c, const qa_unified_packet *p, uint64_t now,
                           qa_unified_admit_delivery_fn admit, qa_unified_deliver_fn deliver,
                           void *context, qa_error *error) {
    if (p->sequence<=c->frame_received || p->sequence<c->newest_frame) return true;
    if (p->sequence>c->newest_frame) {
        c->newest_frame=p->sequence; qa_unified_assembly_release(c, c->frame_assembly); c->frame_assembly=NULL;
    }
    if (c->waiting_frame && c->waiting_frame->sequence==p->sequence) return true;
    if (!c->frame_assembly) {
        c->frame_assembly=assembly_create(c,p,now,error);
        if (!c->frame_assembly) return false;
    }
    if (!append(c->frame_assembly,p)) return true;
    if (c->frame_assembly->received_count==c->frame_assembly->fragments) {
        /* Keep a frame that can release the accepted reliable prefix instead
         * of replacing it with one blocked behind a deferred event payload. */
        if (c->waiting_frame && c->waiting_frame->required<=c->reliable_received &&
            c->frame_assembly->required>c->reliable_received) {
            qa_unified_assembly_release(c,c->frame_assembly); c->frame_assembly=NULL;
            return deliver_frame(c,admit,deliver,context,error);
        }
        qa_unified_assembly_release(c, c->waiting_frame); c->waiting_frame=c->frame_assembly; c->frame_assembly=NULL;
        return deliver_frame(c,admit,deliver,context,error);
    }
    return true;
}

bool qa_unified_channel_receive_buffered(qa_unified_channel *c, qa_bytes datagram, uint64_t now,
                                 qa_unified_admit_delivery_fn admit, qa_unified_deliver_fn deliver,
                                 void *context, qa_error *error) {
    if (!open_channel(c,error)) return false;
    if (!deliver) return error_message(error,QA_ERROR_ARGUMENT,"unified channel requires a delivery consumer");
    if (!expire(c,now,error)) return false;
    if (datagram.size>c->limits.datagram_bytes) return true;
    qa_unified_packet p;
    if (!qa_unified_packet_decode(datagram,&p,NULL)) return true;
    unsigned mismatch=0;
    for (size_t i=0;i<16;++i) mismatch|=(unsigned)(p.token.bytes[i]^c->token.bytes[i]);
    if (mismatch || (p.kind!=QA_UNIFIED_ACK &&
        (p.total_bytes>c->limits.message_bytes || p.fragments>c->limits.fragments))) return true;
    c->busy=true; accept_ack(c,&p);
    bool result=p.kind==QA_UNIFIED_ACK ? deliver_ready(c,admit,deliver,context,error) : p.kind==QA_UNIFIED_RELIABLE
        ? receive_reliable(c,&p,now,admit,deliver,context,error)
        : receive_frame(c,&p,now,admit,deliver,context,error);
    c->busy=false; return result;
}

bool qa_unified_channel_receive(qa_unified_channel *c, qa_bytes datagram, uint64_t now,
                                 qa_unified_deliver_fn deliver, void *context, qa_error *error) {
    return qa_unified_channel_receive_buffered(c,datagram,now,NULL,deliver,context,error);
}

bool qa_unified_channel_resume(qa_unified_channel *c, qa_unified_admit_delivery_fn admit,
                               qa_unified_deliver_fn deliver, void *context, qa_error *error) {
    if (!open_channel(c,error) || !deliver)
        return error_message(error,QA_ERROR_ARGUMENT,"unified retained delivery requires its idle consumer");
    c->busy=true;
    bool result=deliver_ready(c,admit,deliver,context,error);
    c->busy=false; return result;
}

static qa_net_send_result send_packet(qa_unified_channel *c, const qa_unified_packet *p,
                         qa_unified_send_fn send, void *context, qa_error *error) {
    size_t size=0;
    if (!qa_unified_packet_encode(p,c->packet,c->limits.datagram_bytes,&size,error)) return false;
    return send(context,(qa_bytes){c->packet,size},error);
}

static bool send_ack(qa_unified_channel *c, qa_unified_send_fn send, void *context,
                      bool *progress, qa_error *error) {
    qa_unified_packet p={.kind=QA_UNIFIED_ACK,.token=c->token,.acknowledged_reliable=c->reliable_received,
        .acknowledged_frame=c->frame_admitted};
    if (c->cumulative_pending) {
        p.sequence=c->cumulative_sequence; p.fragment=c->cumulative_fragment;
        qa_net_send_result result=send_packet(c,&p,send,context,error);
        if (result!=QA_NET_SEND_ACCEPTED) return result==QA_NET_SEND_FULL;
        c->cumulative_pending=false; *progress=true; return true;
    }
    assembly *selected=NULL;
    uint16_t selected_fragment=0;
    for (size_t i=0;i<64;++i) {
        assembly *a=c->assemblies[i];
        if (!a || (selected && selected->sequence<a->sequence)) continue;
        for (uint32_t f=0;f<a->fragments;++f) if (a->pending_ack[f]) {
            selected=a; selected_fragment=(uint16_t)f; break;
        }
    }
    if (!selected) {
        if (!c->frame_ack_pending) return true;
        qa_net_send_result result=send_packet(c,&p,send,context,error);
        if (result!=QA_NET_SEND_ACCEPTED) return result==QA_NET_SEND_FULL;
        c->frame_ack_pending=false; *progress=true; return true;
    }
    p.sequence=selected->sequence; p.fragment=selected_fragment;
    qa_net_send_result result=send_packet(c,&p,send,context,error);
    if (result!=QA_NET_SEND_ACCEPTED) return result==QA_NET_SEND_FULL;
    selected->pending_ack[selected_fragment]=0; *progress=true; return true;
}

static qa_unified_packet data_packet(qa_unified_channel *c, outgoing *message,
                                      qa_unified_packet_kind kind, uint16_t fragment) {
    uint32_t fragment_bytes=c->limits.datagram_bytes-QA_UNIFIED_HEADER_BYTES;
    size_t offset=(size_t)fragment*fragment_bytes;
    size_t size=message->payload.size-offset;
    if (size>fragment_bytes) size=fragment_bytes;
    return (qa_unified_packet){
        .kind=kind,.token=c->token,.sequence=message->sequence,
        .acknowledged_reliable=c->reliable_received,.required_reliable=message->required,
        .acknowledged_frame=c->frame_admitted,
        .total_bytes=(uint32_t)message->payload.size,.fragment_bytes=fragment_bytes,
        .fragment=fragment,.fragments=message->fragments,
        .payload={qa_unified_payload_page(&message->payload,fragment),size}
    };
}

static bool send_reliable(qa_unified_channel *c, uint64_t now, qa_unified_send_fn send,
                           void *context, bool *progress, qa_error *error) {
    uint32_t count=c->reliable_count<window(c)?c->reliable_count:window(c);
    for (uint32_t checked=0;checked<count;++checked) {
        uint32_t index=c->reliable_cursor%count;
        c->reliable_cursor=(index+1)%count;
        outgoing *message=c->reliable;
        for (uint32_t i=0;i<index;++i) message=message->next;
        uint32_t fragment=message->next_fragment;
        bool fresh=fragment<message->fragments;
        if (!fresh) {
            for (fragment=0;fragment<message->fragments;++fragment) {
                sent_fragment *s=qa_unified_sent_fragment(message,fragment);
                if (!s->acknowledged && elapsed(now,s->at,c->limits.retry_ns)) break;
            }
            if (fragment==message->fragments) {
                if (index || message->acknowledged!=message->fragments) continue;
                fragment=(uint32_t)message->fragments-1;
                if (!elapsed(now,qa_unified_sent_fragment(message,fragment)->at,c->limits.retry_ns)) continue;
            }
        }
        sent_fragment *s=qa_unified_sent_fragment(message,fragment);
        if (s->attempts>=c->limits.maximum_transmissions) {
            close_channel(c); return error_message(error,QA_ERROR_IO,"unified reliable retry limit exceeded");
        }
        qa_unified_packet p=data_packet(c,message,QA_UNIFIED_RELIABLE,(uint16_t)fragment);
        qa_net_send_result result=send_packet(c,&p,send,context,error);
        if (result!=QA_NET_SEND_ACCEPTED) { c->reliable_cursor=index; return result==QA_NET_SEND_FULL; }
        s->at=now; s->attempts++;
        if (fresh) message->next_fragment++;
        *progress=true; return true;
    }
    return true;
}

static bool send_frame(qa_unified_channel *c, qa_unified_send_fn send, void *context,
                        bool *progress, qa_error *error) {
    outgoing *message=c->frame;
    if (!message) return true;
    qa_unified_packet p=data_packet(c,message,QA_UNIFIED_FRAME,(uint16_t)message->next_fragment);
    qa_net_send_result result=send_packet(c,&p,send,context,error);
    if (result!=QA_NET_SEND_ACCEPTED) return result==QA_NET_SEND_FULL;
    if (++message->next_fragment==message->fragments) {
        c->frame_transmitted=message->sequence;
        c->frame=c->pending_frame; c->pending_frame=NULL; qa_unified_outgoing_release(c, message);
    }
    *progress=true; return true;
}

bool qa_unified_channel_flush(qa_unified_channel *c, uint64_t now, qa_unified_send_fn send,
                               void *context, size_t *sent, qa_error *error) {
    if (sent) *sent=0;
    if (!open_channel(c,error)) return false;
    if (!send) return error_message(error,QA_ERROR_ARGUMENT,"unified channel requires a send callback");
    if (!expire(c,now,error)) return false;
    size_t count=0; c->busy=true;
    while (count<c->limits.packets_per_flush) {
        bool progressed=false;
        for (unsigned turn=0;turn<3 && count<c->limits.packets_per_flush;++turn) {
            unsigned lane=c->lane;
            bool progress=false;
            bool ok=lane==0?send_ack(c,send,context,&progress,error):lane==1
                ?send_reliable(c,now,send,context,&progress,error)
                :send_frame(c,send,context,&progress,error);
            if (!ok) { c->busy=false; if (sent) *sent=count; return false; }
            c->lane=(lane+1)%3;
            if (progress) { count++; progressed=true; }
        }
        if (!progressed) break;
    }
    c->busy=false; if (sent) *sent=count; return true;
}
