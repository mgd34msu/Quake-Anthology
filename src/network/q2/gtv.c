#include "qa/network_q2_mvd.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <zlib.h>

static void gtv_wipe(void *data, size_t size)
{ volatile uint8_t *p=data; while (size--) *p++=0; }
static bool gtv_error(qa_error *error, const char *text)
{ qa_error_set(error,QA_ERROR_FORMAT,0,"%s",text); return false; }
static bool gtv_packet(uint8_t opcode, qa_bytes body, qa_buffer *out, qa_error *error)
{
    if (!out || body.size>=QA_Q2_MVD_MESSAGE_BYTES || (body.size && !body.data))
        return gtv_error(error,"Invalid GTV message size");
    uint8_t *bytes=malloc(body.size+3);
    if (!bytes) { qa_error_set(error,QA_ERROR_MEMORY,0,"GTV packet allocation failed"); return false; }
    qa_store_u16le(bytes,(uint16_t)(body.size+1)); bytes[2]=opcode;
    if (body.size) memcpy(bytes+3,body.data,body.size);
    *out=(qa_buffer){bytes,body.size+3}; return true;
}
bool qa_gtv_request_read(qa_bytes bytes, qa_gtv_request *out, qa_error *error)
{
    if (!out || !bytes.size || bytes.size>256 || !bytes.data) return gtv_error(error,"Invalid GTV request size");
    qa_net_reader r; qa_net_reader_init(&r,bytes,error); qa_gtv_request q={0};
    uint8_t opcode=qa_net_read_u8(&r);
    if (opcode>QA_GTV_REQUEST_COMMAND) return gtv_error(error,"Unknown GTV request opcode");
    q.kind=(qa_gtv_request_kind)opcode;
    switch (q.kind) {
    case QA_GTV_REQUEST_HELLO:
        if (qa_net_read_u16(&r)!=QA_GTV_PROTOCOL) return qa_net_reader_fail(&r,"Unsupported GTV protocol");
        q.flags=qa_net_read_u32(&r); (void)qa_net_read_u32(&r);
        qa_net_read_string(&r,q.username,sizeof(q.username)); qa_net_read_string(&r,q.password,sizeof(q.password));
        qa_net_read_string(&r,q.version,sizeof(q.version)); break;
    case QA_GTV_REQUEST_START: q.buffered_packets=qa_net_read_u16(&r); break;
    case QA_GTV_REQUEST_COMMAND: qa_net_read_string(&r,q.command,sizeof(q.command)); break;
    case QA_GTV_REQUEST_PING: case QA_GTV_REQUEST_STOP: break;
    }
    if (!qa_net_reader_finish(&r)) { gtv_wipe(&q,sizeof(q)); return false; }
    *out=q; gtv_wipe(&q,sizeof(q)); return true;
}
bool qa_gtv_request_write(const qa_gtv_request *q, qa_buffer *out, qa_error *error)
{
    if (!q || !out || q->kind<QA_GTV_REQUEST_HELLO || q->kind>QA_GTV_REQUEST_COMMAND)
        return gtv_error(error,"Invalid GTV request");
    uint8_t bytes[256]; qa_net_writer w; qa_net_writer_init(&w,bytes,sizeof(bytes),error);
    qa_net_write_u8(&w,(uint8_t)q->kind);
    switch (q->kind) {
    case QA_GTV_REQUEST_HELLO:
        if (!memchr(q->username,0,sizeof(q->username)) || !memchr(q->password,0,sizeof(q->password)) ||
            !memchr(q->version,0,sizeof(q->version))) return gtv_error(error,"Unterminated GTV identity");
        qa_net_write_u16(&w,QA_GTV_PROTOCOL); qa_net_write_u32(&w,q->flags); qa_net_write_u32(&w,0);
        qa_net_write_string(&w,q->username); qa_net_write_string(&w,q->password); qa_net_write_string(&w,q->version); break;
    case QA_GTV_REQUEST_START: qa_net_write_u16(&w,q->buffered_packets); break;
    case QA_GTV_REQUEST_COMMAND:
        if (!memchr(q->command,0,sizeof(q->command))) return gtv_error(error,"Unterminated GTV command");
        qa_net_write_string(&w,q->command); break;
    case QA_GTV_REQUEST_PING: case QA_GTV_REQUEST_STOP: break;
    }
    bool ok=false;
    if (!w.failed) ok=gtv_packet(bytes[0],(qa_bytes){bytes+1,qa_net_writer_size(&w)-1},out,error);
    gtv_wipe(bytes,sizeof(bytes)); return ok;
}

typedef enum gtv_phase { GTV_MAGIC, GTV_HELLO_WAIT, GTV_CONNECTED, GTV_STARTING,
    GTV_READING, GTV_WAITING, GTV_STOPPING, GTV_CLOSED } gtv_phase;
struct qa_gtv_client {
    qa_gtv_request hello;
    qa_gtv_send_fn send;
    void *send_user;
    qa_q2_mvd_framer *frames;
    z_stream inflate;
    bool compressed, started, busy;
    gtv_phase phase;
    uint32_t flags;
    uint8_t magic[4];
    size_t magic_bytes;
};
static void gtv_close(qa_gtv_client *c)
{
    c->phase=GTV_CLOSED;
    if (c->compressed) { inflateEnd(&c->inflate); c->compressed=false; }
    gtv_wipe(c->hello.password,sizeof(c->hello.password));
}
bool qa_gtv_client_create(const qa_gtv_identity *identity, uint32_t flags,
                           qa_gtv_send_fn send, void *user, qa_gtv_client **out, qa_error *error)
{
    if (!identity || !identity->username || !identity->password || !identity->version || !send || !out)
        return gtv_error(error,"Missing GTV client identity or callback");
    size_t a=strlen(identity->username), b=strlen(identity->password), d=strlen(identity->version);
    if (a>255 || b>255 || d>255 || a+b+d+14>256) return gtv_error(error,"GTV identity exceeds message limit");
    qa_gtv_client *c=calloc(1,sizeof(*c));
    if (!c) { qa_error_set(error,QA_ERROR_MEMORY,0,"GTV client allocation failed"); return false; }
    if (!qa_q2_mvd_framer_create(false,QA_Q2_MVD_MESSAGE_BYTES,&c->frames,error)) { free(c); return false; }
    c->hello.kind=QA_GTV_REQUEST_HELLO; c->hello.flags=flags;
    memcpy(c->hello.username,identity->username,a+1); memcpy(c->hello.password,identity->password,b+1); memcpy(c->hello.version,identity->version,d+1);
    c->send=send; c->send_user=user; c->phase=GTV_MAGIC; *out=c; return true;
}
void qa_gtv_client_destroy(qa_gtv_client *c)
{
    if (!c || c->busy) return;
    gtv_close(c); qa_q2_mvd_framer_destroy(c->frames); gtv_wipe(c,sizeof(*c)); free(c);
}
void qa_gtv_client_close(qa_gtv_client *c) { if (c && !c->busy) gtv_close(c); }
bool qa_gtv_client_closed(const qa_gtv_client *c) { return !c || c->phase==GTV_CLOSED; }
bool qa_gtv_client_start(qa_gtv_client *c, qa_error *error)
{
    if (!c || c->started || c->phase!=GTV_MAGIC) return gtv_error(error,"GTV client already started");
    uint8_t magic[4]; qa_store_u32le(magic,QA_Q2_MVD_MAGIC);
    if (!c->send(c->send_user,(qa_bytes){magic,sizeof(magic)},error)) return false;
    c->started=true; return true;
}
static bool gtv_send_request(qa_gtv_client *c, const qa_gtv_request *q, qa_error *error)
{
    qa_buffer bytes={0}; if (!qa_gtv_request_write(q,&bytes,error)) return false;
    bool ok=c->send(c->send_user,(qa_bytes){bytes.data,bytes.size},error);
    gtv_wipe(bytes.data,bytes.size); qa_buffer_free(&bytes); return ok;
}
static bool gtv_connected(const qa_gtv_client *c)
{ return c && c->started && c->phase>=GTV_CONNECTED && c->phase<GTV_CLOSED; }
bool qa_gtv_client_request_start(qa_gtv_client *c, uint16_t buffered, qa_error *error)
{
    if (!c || c->phase!=GTV_CONNECTED) return gtv_error(error,"GTV client is not ready to start");
    qa_gtv_request q={.kind=QA_GTV_REQUEST_START,.buffered_packets=buffered};
    if (!gtv_send_request(c,&q,error)) return false;
    c->phase=GTV_STARTING; return true;
}
bool qa_gtv_client_request_stop(qa_gtv_client *c, qa_error *error)
{
    if (!c || (c->phase!=GTV_READING && c->phase!=GTV_WAITING)) return gtv_error(error,"GTV client is not streaming");
    qa_gtv_request q={.kind=QA_GTV_REQUEST_STOP};
    if (!gtv_send_request(c,&q,error)) return false;
    c->phase=GTV_STOPPING; return true;
}
bool qa_gtv_client_ping(qa_gtv_client *c, qa_error *error)
{
    if (!gtv_connected(c)) return gtv_error(error,"GTV client is not connected");
    qa_gtv_request q={.kind=QA_GTV_REQUEST_PING}; return gtv_send_request(c,&q,error);
}
bool qa_gtv_client_command(qa_gtv_client *c, const char *text, qa_error *error)
{
    if (!gtv_connected(c) || !text || !(c->flags&QA_GTV_STRINGCMDS)) return gtv_error(error,"GTV commands were not negotiated");
    qa_gtv_request q={.kind=QA_GTV_REQUEST_COMMAND};
    size_t n=0;
    while (n<150 && text[n]) n++;
    memcpy(q.command,text,n); q.command[n]=0;
    return gtv_send_request(c,&q,error);
}
static bool gtv_emit(qa_gtv_event_fn emit, void *user, qa_gtv_event event, qa_error *error)
{ return !emit || emit(user,&event,error); }
static bool gtv_consume(qa_gtv_client *c, qa_bytes bytes, qa_gtv_event_fn emit, void *user, qa_error *error)
{
    qa_net_reader r; qa_net_reader_init(&r,bytes,error);
    uint8_t opcode=qa_net_read_u8(&r); qa_gtv_event event={0};
    switch (opcode) {
    case QA_GTV_HELLO:
        if (c->phase!=GTV_HELLO_WAIT) return gtv_error(error,"Unexpected GTV hello");
        c->flags=qa_net_read_u32(&r);
        if (c->flags&~c->hello.flags) return gtv_error(error,"Unrequested GTV features");
        if (!qa_net_reader_finish(&r)) return false;
        if (c->flags&QA_GTV_DEFLATE) {
            memset(&c->inflate,0,sizeof(c->inflate));
            if (inflateInit(&c->inflate)!=Z_OK) return gtv_error(error,"GTV inflate initialization failed");
            c->compressed=true;
        }
        c->phase=GTV_CONNECTED; event.kind=QA_GTV_EVENT_HELLO; event.flags=c->flags; break;
    case QA_GTV_START:
        if (c->phase!=GTV_STARTING) return gtv_error(error,"Unexpected GTV start acknowledgement");
        c->phase=GTV_READING; event.kind=QA_GTV_EVENT_STARTED; break;
    case QA_GTV_STOP:
        if (c->phase!=GTV_STOPPING) return gtv_error(error,"Unexpected GTV stop acknowledgement");
        c->phase=GTV_CONNECTED; event.kind=QA_GTV_EVENT_STOPPED; break;
    case QA_GTV_PONG:
        if (!gtv_connected(c)) return gtv_error(error,"Unexpected GTV pong");
        event.kind=QA_GTV_EVENT_PONG; break;
    case QA_GTV_DATA:
        if (c->phase==GTV_STOPPING) return true;
        if (c->phase!=GTV_READING && c->phase!=GTV_WAITING) return gtv_error(error,"Unexpected GTV stream data");
        if (bytes.size==1) { c->phase=GTV_WAITING; event.kind=QA_GTV_EVENT_SUSPENDED; }
        else {
            bool resumed=c->phase==GTV_WAITING; c->phase=GTV_READING;
            if (resumed && !gtv_emit(emit,user,(qa_gtv_event){.kind=QA_GTV_EVENT_RESUMED},error)) return false;
            event.kind=QA_GTV_EVENT_DATA; event.payload=(qa_bytes){bytes.data+1,bytes.size-1};
        }
        return gtv_emit(emit,user,event,error);
    case QA_GTV_ERROR: case QA_GTV_BAD_REQUEST: case QA_GTV_NO_ACCESS: case QA_GTV_DISCONNECT: case QA_GTV_RECONNECT:
        /* Keep inflate storage alive until the current inflate call unwinds. */
        c->phase=GTV_CLOSED; event.kind=QA_GTV_EVENT_CLOSED; event.reason=opcode; break;
    default: return gtv_error(error,"Unknown GTV server opcode");
    }
    if (!qa_net_reader_finish(&r)) return false;
    return gtv_emit(emit,user,event,error);
}
static bool gtv_plain(qa_gtv_client *c, qa_bytes bytes, size_t *consumed,
                        bool stop_at_compression, qa_gtv_event_fn emit, void *user, qa_error *error)
{
    *consumed=0;
    while (*consumed<bytes.size) {
        size_t n=0; bool present=false; qa_bytes message;
        if (!qa_q2_mvd_framer_push(c->frames,(qa_bytes){bytes.data+*consumed,bytes.size-*consumed},&n,&present,&message,error)) return false;
        *consumed+=n;
        if (present && !gtv_consume(c,message,emit,user,error)) return false;
        if (qa_q2_mvd_framer_finished(c->frames)) {
            c->phase=GTV_CLOSED;
            return gtv_emit(emit,user,(qa_gtv_event){.kind=QA_GTV_EVENT_CLOSED,.reason=-1},error);
        }
        if (c->phase==GTV_CLOSED) {
            if (*consumed!=bytes.size) return gtv_error(error,"Data after GTV closure");
            return true;
        }
        if (stop_at_compression && c->compressed) return true;
        if (!n) return gtv_error(error,"GTV framer made no progress");
    }
    return true;
}
static bool gtv_inflate(qa_gtv_client *c, qa_bytes bytes, qa_gtv_event_fn emit, void *user, qa_error *error)
{
    uint8_t output[16384]; size_t at=0,total=0;
    while (at<bytes.size) {
        size_t chunk=bytes.size-at; if (chunk>UINT_MAX) chunk=UINT_MAX;
        c->inflate.next_in=(Bytef *)(bytes.data+at); c->inflate.avail_in=(uInt)chunk;
        do {
            uInt before=c->inflate.avail_in;
            c->inflate.next_out=output; c->inflate.avail_out=(uInt)sizeof(output);
            int status=inflate(&c->inflate,Z_SYNC_FLUSH);
            size_t n=sizeof(output)-c->inflate.avail_out;
            total+=n;
            if (total>QA_Q2_MVD_MESSAGE_BYTES*64u) return gtv_error(error,"GTV decompressed output limit exceeded");
            if (status!=Z_OK && status!=Z_BUF_ERROR) return gtv_error(error,"Invalid GTV deflate stream");
            if (n) {
                size_t consumed;
                if (!gtv_plain(c,(qa_bytes){output,n},&consumed,false,emit,user,error)) return false;
                if (consumed!=n) return gtv_error(error,"Unconsumed GTV inflated bytes");
                if (c->phase==GTV_CLOSED) {
                    if (c->inflate.avail_in || at+chunk<bytes.size) return gtv_error(error,"Compressed data after GTV closure");
                    return true;
                }
            }
            if (before==c->inflate.avail_in && !n) {
                if (c->inflate.avail_in) return gtv_error(error,"GTV inflate made no progress");
                break;
            }
        } while (c->inflate.avail_in || c->inflate.avail_out==0);
        at+=chunk;
    }
    return true;
}
bool qa_gtv_client_receive(qa_gtv_client *c, qa_bytes bytes, qa_gtv_event_fn emit, void *user, qa_error *error)
{
    if (!c || !c->started || c->busy || c->phase==GTV_CLOSED || (bytes.size && !bytes.data))
        return gtv_error(error,"Invalid GTV receive state");
    c->busy=true; bool ok=false; size_t at=0;
    if (c->phase==GTV_MAGIC) {
        size_t n=4-c->magic_bytes; if (n>bytes.size) n=bytes.size;
        if (n) memcpy(c->magic+c->magic_bytes,bytes.data,n);
        c->magic_bytes+=n; at=n;
        if (c->magic_bytes<4) { ok=true; goto done; }
        if (qa_load_u32le(c->magic)!=QA_Q2_MVD_MAGIC) { gtv_error(error,"Not a GTV server"); goto done; }
        c->phase=GTV_HELLO_WAIT;
        if (!gtv_send_request(c,&c->hello,error)) goto done;
        gtv_wipe(c->hello.password,sizeof(c->hello.password));
    }
    if (!c->compressed && at<bytes.size) {
        size_t consumed;
        if (!gtv_plain(c,(qa_bytes){bytes.data+at,bytes.size-at},&consumed,true,emit,user,error)) goto done;
        at+=consumed;
    }
    if (c->compressed && at<bytes.size && !gtv_inflate(c,(qa_bytes){bytes.data+at,bytes.size-at},emit,user,error)) goto done;
    ok=true;
done:
    c->busy=false;
    if (!ok || c->phase==GTV_CLOSED) gtv_close(c);
    return ok;
}

struct qa_gtv_server_stream { z_stream deflate; bool greeted, compressed, failed; };
bool qa_gtv_server_stream_create(qa_gtv_server_stream **out, qa_error *error)
{
    if (!out) return gtv_error(error,"Missing GTV stream output");
    qa_gtv_server_stream *s=calloc(1,sizeof(*s));
    if (!s) { qa_error_set(error,QA_ERROR_MEMORY,0,"GTV stream allocation failed"); return false; }
    *out=s; return true;
}
void qa_gtv_server_stream_destroy(qa_gtv_server_stream *s)
{
    if (!s) return;
    if (s->compressed) deflateEnd(&s->deflate);
    free(s);
}
bool qa_gtv_server_hello(qa_gtv_server_stream *s, uint32_t flags, qa_buffer *out, qa_error *error)
{
    if (!s || !out || s->greeted || s->failed || (flags&~UINT32_C(3))) return gtv_error(error,"Invalid GTV server hello");
    uint8_t body[4]; qa_store_u32le(body,flags); qa_buffer packet={0};
    if (!gtv_packet(QA_GTV_HELLO,(qa_bytes){body,sizeof(body)},&packet,error)) return false;
    if (flags&QA_GTV_DEFLATE) {
        if (deflateInit(&s->deflate,Z_DEFAULT_COMPRESSION)!=Z_OK) { qa_buffer_free(&packet); return gtv_error(error,"GTV deflate initialization failed"); }
        s->compressed=true;
    }
    s->greeted=true; *out=packet; return true;
}
bool qa_gtv_server_message(qa_gtv_server_stream *s, qa_gtv_server_opcode opcode,
                            qa_bytes body, qa_buffer *out, qa_error *error)
{
    if (!s || !out || !s->greeted || s->failed || opcode<=QA_GTV_HELLO || opcode>QA_GTV_RECONNECT)
        return gtv_error(error,"Invalid GTV server stream state");
    qa_buffer packet={0}; if (!gtv_packet((uint8_t)opcode,body,&packet,error)) return false;
    if (!s->compressed) { *out=packet; return true; }
    uLong bound=deflateBound(&s->deflate,(uLong)packet.size)+16;
    if (bound>UINT_MAX) { qa_buffer_free(&packet); return gtv_error(error,"GTV compression bound overflow"); }
    uint8_t *bytes=malloc((size_t)bound);
    if (!bytes) { qa_buffer_free(&packet); qa_error_set(error,QA_ERROR_MEMORY,0,"GTV compressed packet allocation failed"); return false; }
    s->deflate.next_in=packet.data; s->deflate.avail_in=(uInt)packet.size;
    s->deflate.next_out=bytes; s->deflate.avail_out=(uInt)bound;
    int status=deflate(&s->deflate,Z_SYNC_FLUSH);
    qa_buffer_free(&packet);
    if (status!=Z_OK || s->deflate.avail_in || !s->deflate.avail_out) {
        free(bytes); s->failed=true; return gtv_error(error,"GTV compression failed");
    }
    *out=(qa_buffer){bytes,(size_t)bound-s->deflate.avail_out}; return true;
}
