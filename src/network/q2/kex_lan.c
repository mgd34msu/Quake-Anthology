#include "kex_lan_internal.h"
#include "kex_channel_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void attrs_free(struct attributes*a) {
    free(a->data);
    a->data=NULL;
    a->count=0;
}
static const char*attrs_get(const struct attributes*a,const char*key) {
    for(size_t i=0;i<a->count;i++)if(!strcmp(a->data[i].key,key))return a->data[i].value;
    return "";
}
static bool attrs_set(struct attributes*a,const char*key,const char*value,bool preserve_name,qa_error*e) {
    size_t kn=strlen(key),vn=strlen(value);
    if(!kn||kn>=sizeof(a->data[0].key)||vn>=sizeof(a->data[0].value)||strchr(key,'\\')||
        !qa_kex_text_valid((qa_bytes){(const uint8_t*)key,kn})||
        !qa_kex_text_valid((qa_bytes){(const uint8_t*)value,vn})) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX lobby attribute");
        return false;
    }
    size_t index=a->count;
    for(size_t i=0;i<a->count;i++)if(!strcmp(key,a->data[i].key)) {
        index=i;
        break;
    }
    if(!vn) {
        if(preserve_name&&!strcmp(key,"name"))return true;
        if(index<a->count) {
            memmove(a->data+index,a->data+index+1,(a->count-index-1)*sizeof(*a->data));
            a->count--;
        }
        return true;
    }
    if(index==a->count) {
        if(a->count==256) {
            qa_error_set(e,QA_ERROR_FORMAT,0,"Too many KEX lobby attributes");
            return false;
        }
        qa_kex_attribute*data=realloc(a->data,(a->count+1)*sizeof(*data));
        if(!data) {
            qa_error_set(e,QA_ERROR_MEMORY,0,"KEX attribute allocation failed");
            return false;
        }
        a->data=data;
        a->count++;
    }
    memcpy(a->data[index].key,key,kn+1);
    memcpy(a->data[index].value,value,vn+1);
    return true;
}
static bool text_attribute(struct attributes*a,qa_bytes b,bool player,qa_error*e) {
    if(!player&&b.size>=3&&b.data&&!memcmp(b.data,"\xef\xbb\xbf",3)) {
        b.data+=3;
        b.size-=3;
    }
    if(!b.data||b.size==0||b.size>=(player?128u:5120u)||memchr(b.data,0,b.size)||!qa_kex_text_valid(b)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Invalid KEX attribute text");
        return false;
    }
    const uint8_t*separator=memchr(b.data,'\\',b.size);
    if(!separator||separator==b.data) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Missing KEX attribute separator");
        return false;
    }
    size_t keysize=(size_t)(separator-b.data);
    if(keysize>=1024) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX attribute key too long");
        return false;
    }
    char key[1024],value[4096];
    size_t valuesize=b.size-keysize-1;
    if(valuesize>=sizeof(value)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX attribute value too long");
        return false;
    }
    memcpy(key,b.data,keysize);
    key[keysize]=0;
    memcpy(value,separator+1,valuesize);
    value[valuesize]=0;
    return attrs_set(a,key,value,player,e);
}
bool qa_kex_lan_emit(void*user,qa_bytes b,qa_error*e) {
    struct peer*p=user;
    return qa_net_transport_send(p->owner->transport,&p->address,b,e);
}
static struct peer*find_peer(const qa_kex_lan*l,const qa_net_address*a) {
    for(size_t i=0;i<l->peer_count;i++)if(qa_net_address_equal(&l->peers[i]->address,a,true))return l->peers[i];
    return NULL;
}
static struct peer*add_peer(qa_kex_lan*l,const qa_net_address*a,qa_error*e) {
    struct peer*p=find_peer(l,a);
    if(p)return p;
    if(l->peer_count==256) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX peer capacity exhausted");
        return NULL;
    }
    p=calloc(1,sizeof(*p));
    if(!p) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX peer allocation failed");
        return NULL;
    }
    p->owner=l;
    p->address=*a;
    if(!qa_kex_channel_create(qa_kex_lan_emit,p,&p->channel,e)) {
        free(p);
        return NULL;
    }
    l->peers[l->peer_count++]=p;
    return p;
}
static bool peer_send(struct peer*p,uint8_t kind,qa_bytes b,qa_kex_mode mode,qa_error*e) {
    return qa_kex_channel_send(p->channel,kind,b,mode,p->owner->clock,e);
}
static bool broadcast(qa_kex_lan*l,const struct peer*exclude,uint8_t kind,qa_bytes b,qa_error*e) {
    bool ok=true;
    for(size_t i=0;i<l->peer_count;i++)if(l->peers[i]!=exclude&&l->peers[i]->count&&!peer_send(l->peers[i],kind,b,QA_KEX_RELIABLE,e))ok=false;
    return ok;
}
static bool remove_peer(qa_kex_lan*l,struct peer*p,qa_error*e) {
    size_t slot=0;
    while(slot<l->peer_count&&l->peers[slot]!=p)slot++;
    if(slot==l->peer_count)return true;
    memmove(l->peers+slot,l->peers+slot+1,(l->peer_count-slot-1)*sizeof(*l->peers));
    l->peer_count--;
    bool ok=true;
    if(!l->options.host) {
        l->joined=false;
        attrs_free(&l->attributes);
        for(size_t i=0;i<l->player_count;i++)attrs_free(&l->players[i].attributes);
        l->player_count=0;
    }  else for(size_t n=0;n<p->count;n++) {
        size_t i=0;
        while(i<l->player_count&&l->players[i].id!=p->players[n])i++;
        if(i==l->player_count)continue;
        attrs_free(&l->players[i].attributes);
        memmove(l->players+i,l->players+i+1,(l->player_count-i-1)*sizeof(*l->players));
        l->player_count--;
        uint8_t data[16];
        qa_net_writer w;
        qa_net_writer_init(&w,data,sizeof(data),e);
        qa_net_write_u8(&w,2);
        qa_kex_write_varint(&w,i);
        if(!broadcast(l,NULL,253,(qa_bytes) {
            data,qa_net_writer_size(&w)
        },e))ok=false;
    }
    qa_kex_channel_destroy(p->channel);
    free(p);
    return ok;
}
bool qa_kex_lan_open(qa_net_transport*t,const qa_kex_lan_options*o,qa_kex_lan**out,qa_error*e) {
    const qa_net_address*a=t?qa_net_transport_address(t):NULL;
    if(!t||!o||!out||!a||a->kind==QA_NET_IPX||o->local_players>8||(!o->host&&!o->local_players)||(o->host&&o->max_players<o->local_players)||
        (o->name&&(strlen(o->name)>1024||!qa_kex_text_valid((qa_bytes){(const uint8_t*)o->name,strlen(o->name)})))) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX LAN options");
        return false;
    }
    qa_kex_lan*l=calloc(1,sizeof(*l));
    if(!l) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX LAN allocation failed");
        return false;
    }
    l->transport=t;
    l->local_address=*a;
    l->options=*o;
    l->next_id=1;
    if(o->name)strcpy(l->name,o->name);
    l->options.name=l->name;
    if(o->host) {
        for(unsigned i=0;i<o->local_players;i++) {
            uint64_t id=l->next_id++;
            l->players[l->player_count++].id=id;
            l->local_ids[i]=id;
        }
        if(!attrs_set(&l->attributes,"ingame","1",false,e)) {
            free(l);
            return false;
        }
        l->joined=true;
    }  else if(!add_peer(l,&o->server,e)) {
        free(l);
        return false;
    }
    *out=l;
    return true;
}
void qa_kex_lan_close(qa_kex_lan*l) {
    if(!l||l->entered)return;
    if(!l->transport) { qa_kex_lan_destroy_detached(l); return; }
    l->entered=true;
    uint8_t data[32];
    qa_error ignored= {
        0
    };
    qa_net_writer w;
    qa_net_writer_init(&w,data,sizeof(data),&ignored);
    qa_kex_write_string(&w,"Disconnected");
    for(size_t i=0;i<l->peer_count;i++) {
        peer_send(l->peers[i],254,(qa_bytes) {
            data,qa_net_writer_size(&w)
        },QA_KEX_UNSEQUENCED,&ignored);
        qa_kex_channel_destroy(l->peers[i]->channel);
        free(l->peers[i]);
    }
    for(size_t i=0;i<l->player_count;i++)attrs_free(&l->players[i].attributes);
    attrs_free(&l->attributes);
    while(l->head) {
        struct queued*q=l->head;
        l->head=q->next;
        free(q);
    }
    free(l->borrowed);
    qa_net_transport_close(l->transport);
    free(l);
}
bool qa_kex_lan_admitted(const qa_kex_lan*l,const qa_net_address*a) {
    if(!l||!a)return false;
    if(!l->options.host)return l->joined&&qa_net_address_equal(a,&l->options.server,true);
    struct peer*p=find_peer(l,a);
    return p&&p->count>0;
}
bool qa_kex_lan_ready(const qa_kex_lan*l) {
    return l&&qa_net_transport_ready(l->transport)&&l->joined&&!strcmp(attrs_get(&l->attributes,"ingame"),"1");
}
static bool send_body(qa_kex_lan*l,const qa_net_address*to,qa_bytes b,qa_error*e) {
    if(!l||!to||(b.size&&!b.data)||b.size>65535) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX LAN send");
        return false;
    }
    if(!qa_kex_lan_admitted(l,to))return false;
    struct peer*p=add_peer(l,to,e);
    if(!p)return false;
    bool reliable=b.size>=8&&b.data[0]==0&&b.data[1]==0&&b.data[2]==0&&b.data[3]==128&&b.data[4]==0&&b.data[5]==0&&b.data[6]==0&&b.data[7]==128;
    return peer_send(p,0,b,reliable?QA_KEX_RELIABLE:QA_KEX_SEQUENCED,e);
}
static bool queue(qa_kex_lan*l,const qa_net_address*a,qa_bytes b,qa_error*e) {
    if(b.size>65535) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"KEX game datagram exceeds capacity");
        return false;
    }
    if(l->queued_count==256) {
        struct queued*old=l->head;
        l->dropped=true;
        l->dropped_from=old->address;
        l->head=old->next;
        if(!l->head)l->tail=NULL;
        free(old);
        l->queued_count--;
    }
    struct queued*q=malloc(sizeof(*q)+b.size);
    if(!q) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"KEX receive queue allocation failed");
        return false;
    }
    q->next=NULL;
    q->address=*a;
    q->received=l->clock;
    q->size=b.size;
    q->kind=QA_NET_POLL_PACKET;
    if(b.size)memcpy(q->bytes,b.data,b.size);
    if(l->tail)l->tail->next=q;
    else l->head=q;
    l->tail=q;
    l->queued_count++;
    return true;
}
static bool receive_body(qa_kex_lan*l,qa_net_datagram*out,qa_error*e) {
    if(!l||!out) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX receive queue arguments");
        return false;
    }
    free(l->borrowed);
    l->borrowed=NULL;
    memset(out,0,sizeof(*out));
    if(l->dropped) {
        out->kind=QA_NET_POLL_DROPPED;
        out->from=l->dropped_from;
        l->dropped=false;
        return true;
    }
    if(!l->head) {
        out->kind=QA_NET_POLL_EMPTY;
        return true;
    }
    l->borrowed=l->head;
    l->head=l->head->next;
    if(!l->head)l->tail=NULL;
    l->queued_count--;
    out->kind=l->borrowed->kind;
    out->from=l->borrowed->address;
    out->received_ns=l->borrowed->received;
    out->payload=(qa_bytes) {
        l->borrowed->bytes,l->borrowed->size
    };
    return true;
}
static bool send_attribute(struct peer*p,const char*key,const char*value,qa_error*e) {
    char text[5121];
    int n=snprintf(text,sizeof(text),"%s\\%s",key,value);
    if(n<0||(size_t)n>=sizeof(text)) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"KEX attribute exceeds packet capacity");
        return false;
    }
    return peer_send(p,255,(qa_bytes) {
        (const uint8_t*)text,(size_t)n
    },QA_KEX_RELIABLE,e);
}
static bool attribute_body(qa_kex_lan*l,const char*key,const char*value,qa_error*e) {
    if(!l||!l->options.host||!key||!value||strchr(key,'\\')||strchr(value,'\\')) {
        qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid KEX host attribute");
        return false;
    }
    if(!attrs_set(&l->attributes,key,value,false,e))return false;
    bool ok=true;
    for(size_t i=0;i<l->peer_count;i++)if(l->peers[i]->count&&!send_attribute(l->peers[i],key,value,e))ok=false;
    return ok;
}
static bool join(qa_kex_lan*l,struct peer*p,qa_bytes bytes,qa_error*e) {
    qa_net_reader r;
    qa_net_reader_init(&r,bytes,e);
    char text[64];
    if(!qa_kex_read_string(&r,text,sizeof(text)))return false;
    if(strcmp(text,"CRANTIME"))return true;
    if(l->options.host) {
        if(!qa_kex_read_string(&r,text,sizeof(text)))return false;
        if(strcmp(text,"QuakeII"))return true;
        uint8_t count=qa_net_read_u8(&r);
        if(!qa_net_reader_finish(&r))return false;
        if(count<1||count>8||(!p->count&&l->player_count+count>l->options.max_players))return true;
        if(!p->count) {
            if(!l->next_id||((uint64_t)count-1)>UINT64_MAX-l->next_id) {
                qa_error_set(e,QA_ERROR_FORMAT,0,"KEX lobby player identity space exhausted");
                return false;
            }
            uint8_t data[96];
            qa_net_writer w;
            qa_net_writer_init(&w,data,sizeof(data),e);
            qa_net_write_u8(&w,1);
            qa_kex_write_varint(&w,count);
            for(unsigned i=0;i<count;i++) {
                uint64_t id=l->next_id++;
                p->players[p->count++]=id;
                l->players[l->player_count++]=(struct player) {
                    .id=id
                };
                qa_kex_write_varint(&w,id);
            }
            if(!broadcast(l,p,253,(qa_bytes) {
                data,qa_net_writer_size(&w)
            },e))return false;
        }
        size_t first=0;
        while(first<l->player_count&&l->players[first].id!=p->players[0])first++;
        uint8_t data[2600];
        qa_net_writer w;
        qa_net_writer_init(&w,data,sizeof(data),e);
        qa_kex_write_string(&w,"CRANTIME");
        qa_kex_write_varint(&w,first);
        for(size_t i=0;i<l->player_count;i++)qa_kex_write_varint(&w,l->players[i].id);
        if(w.failed||!peer_send(p,128,(qa_bytes) {
            data,qa_net_writer_size(&w)
        },QA_KEX_UNSEQUENCED,e))return false;
        for(size_t i=0;i<l->attributes.count;i++)if(!send_attribute(p,l->attributes.data[i].key,l->attributes.data[i].value,e))return false;
        return true;
    }
    if(l->joined)return true;
    uint64_t first=qa_kex_read_varint(&r);
    if(r.failed||first>255||first+l->options.local_players>255)return qa_net_reader_fail(&r,"Invalid KEX local player index");
    size_t minimum=(size_t)first+l->options.local_players,count=0;
    uint64_t ids[255];
    while(qa_net_reader_remaining(&r)) {
        if(count==255)return qa_net_reader_fail(&r,"KEX received roster exceeds capacity");
        ids[count]=qa_kex_read_varint(&r);
        if(r.failed)return false;
        if(!ids[count])return qa_net_reader_fail(&r,"Invalid KEX player identity");
        for(size_t j=0;j<count;j++)if(ids[count]==ids[j])return qa_net_reader_fail(&r,"Duplicate KEX player identity");
        count++;
    }
    if(!qa_net_reader_finish(&r))return false;
    if(count<minimum)return qa_net_reader_fail(&r,"KEX local seats exceed received roster");
    for(size_t i=0;i<l->player_count;i++)attrs_free(&l->players[i].attributes);
    for(size_t i=0;i<count;i++)l->players[i]=(struct player) {
        .id=ids[i]
    };
    l->player_count=count;
    l->local_first=(uint8_t)first;
    for(unsigned i=0;i<l->options.local_players;i++)l->local_ids[i]=ids[(size_t)first+i];
    l->joined=true;
    return true;
}
static bool player_message(qa_kex_lan*l,struct peer*p,qa_bytes bytes,qa_error*e) {
    qa_net_reader r;
    qa_net_reader_init(&r,bytes,e);
    uint8_t operation=qa_net_read_u8(&r);
    char text[128];
    if(l->options.host) {
        if(operation>=p->count)return true;
        size_t index=0;
        while(index<l->player_count&&l->players[index].id!=p->players[operation])index++;
        if(index==l->player_count)return true;
        if(!qa_kex_read_string(&r,text,sizeof(text))||!qa_net_reader_finish(&r)||!text_attribute(&l->players[index].attributes,(qa_bytes) {
            (uint8_t*)text,strlen(text)
        },true,e))return false;
        uint8_t data[160];
        qa_net_writer w;
        qa_net_writer_init(&w,data,sizeof(data),e);
        qa_net_write_u8(&w,0);
        qa_kex_write_varint(&w,index);
        qa_kex_write_string(&w,text);
        return !w.failed&&broadcast(l,NULL,253,(qa_bytes) {
            data,qa_net_writer_size(&w)
        },e);
    }
    uint64_t value=qa_kex_read_varint(&r);
    if(r.failed||value>255)return qa_net_reader_fail(&r,"Invalid KEX player index");
    size_t index=(size_t)value;
    if(operation==0) {
        if(index>=l->player_count)return qa_net_reader_fail(&r,"KEX player attribute index outside roster");
        if(!qa_kex_read_string(&r,text,sizeof(text))||!qa_net_reader_finish(&r))return false;
        return text_attribute(&l->players[index].attributes,(qa_bytes) {
            (uint8_t*)text,strlen(text)
        },true,e);
    }
    if(operation==1) {
        if(index>255-l->player_count)return qa_net_reader_fail(&r,"KEX roster exceeds capacity");
        uint64_t ids[255];
        for(size_t i=0;i<index;i++) {
            ids[i]=qa_kex_read_varint(&r);
            if(!ids[i])return qa_net_reader_fail(&r,"Invalid KEX player identity");
            for(size_t j=0;j<l->player_count;j++)if(l->players[j].id==ids[i])return qa_net_reader_fail(&r,"Duplicate KEX player identity");
            for(size_t j=0;j<i;j++)if(ids[j]==ids[i])return qa_net_reader_fail(&r,"Duplicate KEX player identity");
        }
        if(!qa_net_reader_finish(&r))return false;
        for(size_t i=0;i<index;i++)l->players[l->player_count++]=(struct player) {
            .id=ids[i]
        };
        return true;
    }
    if(operation==2) {
        if(index>=l->player_count)return qa_net_reader_fail(&r,"KEX player removal outside roster");
        if(!qa_net_reader_finish(&r))return false;
        attrs_free(&l->players[index].attributes);
        memmove(l->players+index,l->players+index+1,(l->player_count-index-1)*sizeof(*l->players));
        l->player_count--;
        return true;
    }
    return qa_net_reader_fail(&r,"Invalid KEX player operation");
}
static bool message(qa_kex_lan*l,struct peer*p,const qa_kex_message*m,qa_error*e) {
    if(m->kind<127)return !qa_kex_lan_admitted(l,&p->address)||queue(l,&p->address,m->payload,e);
    if(m->kind==128)return join(l,p,m->payload,e);
    if(m->kind==129) {
        if(!m->payload.size||!l->options.host)return true;
        qa_net_reader r;
        qa_net_reader_init(&r,m->payload,e);
        char text[64];
        if(!qa_kex_read_string(&r,text,sizeof(text)))return false;
        if(strcmp(text,"CRANTIME"))return true;
        if(!qa_kex_read_string(&r,text,sizeof(text)))return false;
        if(strcmp(text,"QuakeII"))return true;
        if(!qa_net_reader_finish(&r))return false;
        uint8_t*data=malloc(65535);
        if(!data) {
            qa_error_set(e,QA_ERROR_MEMORY,0,"KEX discovery allocation failed");
            return false;
        }
        qa_net_writer w;
        qa_net_writer_init(&w,data,65535,e);
        qa_kex_write_string(&w,l->name);
        qa_kex_write_varint(&w,l->player_count);
        qa_kex_write_varint(&w,l->options.max_players);
        for(size_t i=0;i<l->attributes.count;i++) {
            qa_kex_attribute*a=&l->attributes.data[i];
            if(a->value[0]&&a->key[0]!='_') {
                qa_kex_write_string(&w,a->key);
                qa_kex_write_string(&w,a->value);
            }
        }
        bool ok=!w.failed&&qa_net_transport_send(l->transport,&p->address,(qa_bytes) {
            data,qa_net_writer_size(&w)
        },e);
        free(data);
        return ok;
    }
    if(m->kind==254) {
        qa_net_reader r;
        qa_net_reader_init(&r,m->payload,e);
        char reason[1024];
        if(!qa_kex_read_string(&r,reason,sizeof(reason))||!qa_net_reader_finish(&r))return false;
        if(!remove_peer(l,p,e))return false;
        qa_error_set(e,QA_ERROR_IO,0,"KEX LAN disconnected: %s",reason);
        return false;
    }
    if(!qa_kex_lan_admitted(l,&p->address))return true;
    if(m->kind==255&&!l->options.host)return text_attribute(&l->attributes,m->payload,false,e);
    if(m->kind==253)return player_message(l,p,m->payload,e);
    return true;
}
static bool dispatch_body(qa_kex_lan*l,const qa_net_datagram *event,qa_error*e) {
    l->clock=event->received_ns;
    if(event->kind==QA_NET_POLL_EMPTY)return true;
    if(event->kind!=QA_NET_POLL_PACKET) {
        if(!queue(l,&event->from,(qa_bytes){0},e))return false;
        l->tail->kind=event->kind;
        l->tail->received=event->received_ns;
        return true;
    }
    if(!l->options.host&&!qa_net_address_equal(&event->from,&l->options.server,true))return true;
    struct peer*p=find_peer(l,&event->from);
    if(!p) {
        qa_error parse= {
            0
        };
        qa_kex_packet packet;
        if(!qa_kex_packet_read(event->payload,&packet,&parse)||packet.flags||!packet.has_kind||(packet.kind!=128&&packet.kind!=129)||l->peer_count==256)return true;
        p=add_peer(l,&event->from,e);
        if(!p)return false;
    }
    qa_kex_message m;
    bool present;
    qa_error parse= {
        0
    };
    if(!qa_kex_channel_receive(p->channel,event->payload,event->received_ns,&m,&present,&parse)) {
        if(parse.code==QA_ERROR_MEMORY||parse.code==QA_ERROR_IO) {
            if(e)*e=parse;
            return false;
        }
        return true;
    }
    if(present&&!message(l,p,&m,&parse)) {
        if(parse.code==QA_ERROR_MEMORY||parse.code==QA_ERROR_IO) {
            if(e)*e=parse;
            return false;
        }
    }
    return true;
}
static bool tick_body(qa_kex_lan*l,uint64_t now,qa_error*e) {
    l->clock=now;
    if(!qa_net_transport_maintenance(l->transport,now,e))return false;
    if(!l->options.host&&!l->joined&&(!l->retried||now<l->retry_at||now-l->retry_at>=UINT64_C(500000000))) {
        struct peer*p=add_peer(l,&l->options.server,e);
        if(!p)return false;
        uint8_t bytes[64];
        qa_net_writer w;
        qa_net_writer_init(&w,bytes,sizeof(bytes),e);
        qa_kex_write_string(&w,"CRANTIME");
        qa_kex_write_string(&w,"QuakeII");
        qa_net_write_u8(&w,l->options.local_players);
        if(w.failed||!peer_send(p,128,(qa_bytes) {
            bytes,qa_net_writer_size(&w)
        },QA_KEX_UNSEQUENCED,e))return false;
        l->retry_at=now;
        l->retried=true;
    }
    for(size_t i=0;i<l->peer_count;) {
        qa_error failure= {
            0
        };
        bool expired=false;
        if(!qa_kex_channel_tick_expiry(l->peers[i]->channel,now,&expired,&failure)) {
            if(expired) remove_peer(l,l->peers[i],e);
            if(e)*e=failure;
            return false;
        }
        i++;
    }
    return true;
}
static bool enter(qa_kex_lan *l, qa_error *e)
{
    if (!l || l->entered || !l->transport) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX LAN operation requires a bound idle owner");
        return false;
    }
    l->entered = true;
    return true;
}
bool qa_kex_lan_send(qa_kex_lan *l, const qa_net_address *to, qa_bytes bytes, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool ok = send_body(l, to, bytes, e);
    l->entered = false;
    return ok;
}
bool qa_kex_lan_receive(qa_kex_lan *l, qa_net_datagram *out, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool ok = receive_body(l, out, e);
    l->entered = false;
    return ok;
}
bool qa_kex_lan_dispatch(qa_kex_lan *l, const qa_net_datagram *event, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool ok = dispatch_body(l, event, e);
    l->entered = false;
    return ok;
}
bool qa_kex_lan_set_attribute(qa_kex_lan *l, const char *key, const char *value, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool ok = attribute_body(l, key, value, e);
    l->entered = false;
    return ok;
}
bool qa_kex_lan_set_maximum(qa_kex_lan *l, uint8_t maximum, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool okay = l->options.host && maximum >= l->options.local_players;
    if (okay) l->options.max_players = maximum;
    else qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX capacity requires the actual host and its retained local members");
    l->entered = false;
    return okay;
}
bool qa_kex_lan_tick(qa_kex_lan *l, uint64_t now, qa_error *e)
{
    if (!enter(l, e)) return false;
    bool ok = tick_body(l, now, e);
    l->entered = false;
    return ok;
}
size_t qa_kex_lan_player_count(const qa_kex_lan*l) {
    return l?l->player_count:0;
}
bool qa_kex_lan_player(const qa_kex_lan*l,size_t index,uint64_t*id,const qa_kex_attribute**attributes,size_t*count) {
    if(!l||index>=l->player_count||!id||!attributes||!count)return false;
    *id=l->players[index].id;
    *attributes=l->players[index].attributes.data;
    *count=l->players[index].attributes.count;
    return true;
}
bool qa_kex_lan_peer_players(const qa_kex_lan *l, const qa_net_address *address,
                             const uint64_t **ids, size_t *count)
{
    if (!l || !address || !ids || !count) return false;
    struct peer *p = find_peer(l, address);
    if (!p || !l->options.host || !p->count) return false;
    *ids = p->players;
    *count = p->count;
    return true;
}
bool qa_kex_lan_local_player(const qa_kex_lan *l, uint8_t seat, uint64_t *id)
{
    if (!l || !id || !l->joined || seat >= l->options.local_players) return false;
    for (size_t i=0;i<l->player_count;i++)if(l->players[i].id==l->local_ids[seat]) {
        *id=l->local_ids[seat];
        return true;
    }
    return false;
}
bool qa_kex_lan_idle(const qa_kex_lan *l)
{
    if(!l||l->entered)return false;
    for(size_t i=0;i<l->peer_count;i++)if(!qa_kex_channel_idle(l->peers[i]->channel))return false;
    return true;
}
