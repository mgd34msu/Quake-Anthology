#include "qa/network_unified.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}

static bool identifier(qa_bytes value, qa_error *error) {
    if (!value.data || value.size < 3 || value.size > UINT16_MAX)
        return fail(error, "invalid unified identifier length");
    const uint8_t *colon = memchr(value.data, ':', value.size);
    if (!colon || colon == value.data || colon == value.data + value.size - 1 ||
        memchr(value.data, 0, value.size))
        return fail(error, "unified identifier requires a namespace and name");
    for (size_t i=0;i<value.size;) {
        uint8_t c=value.data[i++];
        if (c<0x80) continue;
        unsigned trailing; uint32_t scalar,minimum;
        if (c>=0xc2 && c<=0xdf) { trailing=1; scalar=c&31u; minimum=0x80; }
        else if (c>=0xe0 && c<=0xef) { trailing=2; scalar=c&15u; minimum=0x800; }
        else if (c>=0xf0 && c<=0xf4) { trailing=3; scalar=c&7u; minimum=0x10000; }
        else return fail(error,"invalid UTF-8 in unified identifier");
        if (trailing>value.size-i) return fail(error,"truncated UTF-8 in unified identifier");
        while (trailing--) {
            uint8_t next=value.data[i++];
            if ((next&0xc0u)!=0x80u) return fail(error,"invalid UTF-8 continuation in unified identifier");
            scalar=(scalar<<6)|(next&63u);
        }
        if (scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))
            return fail(error,"invalid Unicode scalar in unified identifier");
    }
    return true;
}

static void get_vector(qa_unified_vec3 v, double *a) { a[0] = v.x; a[1] = v.y; a[2] = v.z; }
static qa_unified_vec3 put_vector(const double *a) { return (qa_unified_vec3){a[0],a[1],a[2]}; }

static size_t fields(const qa_unified_movement *m, double a[10]) {
    switch (m->kind) {
    case QA_MOVEMENT_NETQUAKE:
        a[0]=m->data.nq.acknowledged_seconds; get_vector(m->data.nq.angles,a+1);
        a[4]=m->data.nq.forward; a[5]=m->data.nq.side; a[6]=m->data.nq.up;
        a[7]=m->data.nq.buttons; a[8]=m->data.nq.impulse; return 9;
    case QA_MOVEMENT_QUAKEWORLD:
        a[0]=m->data.qw.milliseconds; get_vector(m->data.qw.angles,a+1);
        a[4]=m->data.qw.forward; a[5]=m->data.qw.side; a[6]=m->data.qw.up;
        a[7]=m->data.qw.buttons; a[8]=m->data.qw.impulse; return 9;
    case QA_MOVEMENT_Q2_CLASSIC:
        a[0]=m->data.q2.milliseconds;
        for (size_t i=0;i<3;++i) a[1+i]=m->data.q2.angle_shorts[i];
        a[4]=m->data.q2.forward; a[5]=m->data.q2.side; a[6]=m->data.q2.up;
        a[7]=m->data.q2.buttons; a[8]=m->data.q2.impulse; a[9]=m->data.q2.light_level; return 10;
    case QA_MOVEMENT_Q2_RERELEASE:
        a[0]=m->data.q2r.milliseconds; get_vector(m->data.q2r.angles,a+1);
        a[4]=m->data.q2r.forward; a[5]=m->data.q2r.side;
        a[6]=m->data.q2r.buttons; a[7]=m->data.q2r.server_frame; return 8;
    case QA_MOVEMENT_Q3:
        a[0]=m->data.q3.server_time_ms;
        for (size_t i=0;i<3;++i) a[1+i]=m->data.q3.angle_words[i];
        a[4]=m->data.q3.buttons; a[5]=m->data.q3.weapon; a[6]=m->data.q3.forward;
        a[7]=m->data.q3.right; a[8]=m->data.q3.up; return 9;
    }
    return 0;
}

static void movement(qa_unified_movement *m, const double a[10]) {
    switch (m->kind) {
    case QA_MOVEMENT_NETQUAKE:
        m->data.nq.acknowledged_seconds=a[0]; m->data.nq.angles=put_vector(a+1);
        m->data.nq.forward=a[4]; m->data.nq.side=a[5]; m->data.nq.up=a[6];
        m->data.nq.buttons=a[7]; m->data.nq.impulse=a[8]; break;
    case QA_MOVEMENT_QUAKEWORLD:
        m->data.qw.milliseconds=a[0]; m->data.qw.angles=put_vector(a+1);
        m->data.qw.forward=a[4]; m->data.qw.side=a[5]; m->data.qw.up=a[6];
        m->data.qw.buttons=a[7]; m->data.qw.impulse=a[8]; break;
    case QA_MOVEMENT_Q2_CLASSIC:
        m->data.q2.milliseconds=a[0];
        for (size_t i=0;i<3;++i) m->data.q2.angle_shorts[i]=a[1+i];
        m->data.q2.forward=a[4]; m->data.q2.side=a[5]; m->data.q2.up=a[6];
        m->data.q2.buttons=a[7]; m->data.q2.impulse=a[8]; m->data.q2.light_level=a[9]; break;
    case QA_MOVEMENT_Q2_RERELEASE:
        m->data.q2r.milliseconds=a[0]; m->data.q2r.angles=put_vector(a+1);
        m->data.q2r.forward=a[4]; m->data.q2r.side=a[5];
        m->data.q2r.buttons=a[6]; m->data.q2r.server_frame=a[7]; break;
    case QA_MOVEMENT_Q3:
        m->data.q3.server_time_ms=a[0];
        for (size_t i=0;i<3;++i) m->data.q3.angle_words[i]=a[1+i];
        m->data.q3.buttons=a[4]; m->data.q3.weapon=a[5]; m->data.q3.forward=a[6];
        m->data.q3.right=a[7]; m->data.q3.up=a[8]; break;
    }
}

bool qa_unified_command_encode(const qa_unified_command *c, qa_buffer *out, qa_error *error) {
    if (!c || !out || c->sequence > QA_UNIFIED_SAFE_INTEGER || c->actor.generation > UINT32_MAX)
        return fail(error, "unified command identity or sequence exceeds version 1");
    double values[10];
    size_t count=fields(&c->movement,values), size=24+count*8;
    if (!count) return fail(error,"unknown unified movement dialect");
    for (size_t i=0;i<count;++i)
        if (!isfinite(values[i])) return fail(error,"non-finite unified command number");
    if (c->has_arsenal) {
        if (!identifier(c->arsenal.provider,error) ||
            (c->arsenal.weapon.size && !identifier(c->arsenal.weapon,error))) return false;
        size+=4+c->arsenal.provider.size;
        if (c->arsenal.weapon.size) size+=2+c->arsenal.weapon.size;
    }
    uint8_t *data=malloc(size);
    if (!data) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating unified command"); return false; }
    qa_net_writer w; qa_net_writer_init(&w,data,size,error);
    qa_net_write_data(&w,"QTCM",4); qa_net_write_u16(&w,1);
    qa_net_write_u32(&w,c->actor.slot); qa_net_write_u32(&w,(uint32_t)c->actor.generation);
    qa_net_write_f64(&w,(double)c->sequence); qa_net_write_u8(&w,(uint8_t)c->movement.kind);
    for (size_t i=0;i<count;++i) qa_net_write_f64(&w,values[i]);
    qa_net_write_u8(&w,c->has_arsenal?1:0);
    if (c->has_arsenal) {
        qa_net_write_u16(&w,(uint16_t)c->arsenal.provider.size);
        qa_net_write_data(&w,c->arsenal.provider.data,c->arsenal.provider.size);
        qa_net_write_u8(&w,c->arsenal.weapon.size?1:0);
        if (c->arsenal.weapon.size) {
            qa_net_write_u16(&w,(uint16_t)c->arsenal.weapon.size);
            qa_net_write_data(&w,c->arsenal.weapon.data,c->arsenal.weapon.size);
        }
        qa_net_write_u8(&w,c->arsenal.use_holdable?1:0);
    }
    if (w.failed) { free(data); return false; }
    *out=(qa_buffer){data,qa_net_writer_size(&w)};
    return true;
}

bool qa_unified_command_decode(qa_bytes bytes, const qa_unified_command_receiver *receiver,
                                qa_unified_command *out, qa_error *error) {
    if (!receiver || !receiver->actor_registry || !receiver->controlled || !out || !bytes.data || bytes.size<24 ||
        memcmp(bytes.data,"QTCM",4) || qa_load_u16le(bytes.data+4)!=1)
        return fail(error,"invalid unified command header or receiver");
    qa_net_reader r; qa_net_reader_init(&r,bytes,error); r.bit=6*8;
    uint32_t slot=qa_net_read_u32(&r), generation=qa_net_read_u32(&r);
    double seq=qa_net_read_f64(&r);
    uint8_t dialect=qa_net_read_u8(&r);
    if (!isfinite(seq) || seq<0 || seq>(double)QA_UNIFIED_SAFE_INTEGER || floor(seq)!=seq || dialect>4)
        return fail(error,"invalid unified command sequence or movement dialect");
    qa_unified_command c={0}; c.sequence=(uint64_t)seq;
    c.movement.kind=(qa_movement_kind)dialect;
    double values[10]={0};
    size_t count=dialect==2?10u:dialect==3?8u:9u;
    for (size_t i=0;i<count;++i) {
        values[i]=qa_net_read_f64(&r);
        if (!isfinite(values[i])) return fail(error,"non-finite unified command number");
    }
    movement(&c.movement,values);
    uint8_t intent=qa_net_read_u8(&r);
    if (intent>1) return fail(error,"invalid unified arsenal tag");
    c.has_arsenal=intent!=0;
    if (c.has_arsenal) {
        uint16_t length=qa_net_read_u16(&r);
        if (!qa_net_read_bytes(&r,length,&c.arsenal.provider) || !identifier(c.arsenal.provider,error)) return false;
        uint8_t has_weapon=qa_net_read_u8(&r);
        if (has_weapon>1) return fail(error,"invalid unified weapon tag");
        if (has_weapon) {
            length=qa_net_read_u16(&r);
            if (!qa_net_read_bytes(&r,length,&c.arsenal.weapon) || !identifier(c.arsenal.weapon,error)) return false;
        }
        uint8_t holdable=qa_net_read_u8(&r);
        if (holdable>1) return fail(error,"invalid unified holdable tag");
        c.arsenal.use_holdable=holdable!=0;
    }
    if (!qa_net_reader_finish(&r)) return false;
    qa_unified_controlled_actor controlled={0};
    if (!receiver->controlled(receiver->context,slot,generation,&controlled,error)) return false;
    if (controlled.actor.registry!=receiver->actor_registry || controlled.actor.slot!=slot || controlled.actor.generation!=generation)
        return fail(error,"unified command actor is stale or uncontrolled");
    c.source=receiver->source;
    if ((unsigned)c.source.kind>(unsigned)QA_UNIFIED_SOURCE_BOT ||
        (c.source.kind!=QA_UNIFIED_SOURCE_BOT && (!c.source.client.owner || !c.source.client.generation)) ||
        (c.source.kind==QA_UNIFIED_SOURCE_LOCAL && c.source.client.owner!=c.source.seat.owner))
        return fail(error,"unified command authority belongs to another session");
    if (controlled.movement!=c.movement.kind)
        return fail(error,"unified command movement differs from actor provider");
    if (c.has_arsenal && (controlled.arsenal.size!=c.arsenal.provider.size ||
        !controlled.arsenal.data || memcmp(controlled.arsenal.data,c.arsenal.provider.data,c.arsenal.provider.size)))
        return fail(error,"unified arsenal belongs to another provider");
    c.actor=controlled.actor; *out=c;
    return true;
}
