#include "internal.h"
#include "qa/math.h"
#include "qa/text.h"
uint32_t qa_q2_protocol_version(qa_net_protocol_id p) {
    switch (p.kind) {
        case QA_NET_Q2_34: return 34;
        case QA_NET_R1Q2_35: return 35;
        case QA_NET_Q2PRO_36: return 36;
        case QA_NET_Q2REPRO_1038: return 1038;
        case QA_NET_Q2KEX_2023: return 2023;
        case QA_NET_Q2KEX_DEMO_2022: return 2022;
        case QA_NET_Q2PRIVATE_4038: return 4038;
        default: return 0;
    }
}
bool qa_q2_protocol_from_version(uint32_t version, uint32_t minor, qa_net_protocol_id *out, qa_error *error) {
    qa_net_protocol_id p = {
        0
    };
    switch (version) {
        case 34: p.kind=QA_NET_Q2_34;
        break;
        case 35: p.kind=QA_NET_R1Q2_35;
        p.revision=minor<=1903?1903:minor==1904?1904:1905;
        break;
        case 36: p.kind=QA_NET_Q2PRO_36;
        p.revision=minor<=1016?1015:minor>1026?1026:minor;
        break;
        case 1038: p.kind=QA_NET_Q2REPRO_1038;
        break;
        case 4038: p.kind=QA_NET_Q2PRIVATE_4038;
        break;
        case 2022: p.kind=QA_NET_Q2KEX_DEMO_2022;
        break;
        case 2023: p.kind=QA_NET_Q2KEX_2023;
        break;
        default: qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Unsupported Q2 protocol %u",version);
        return false;
    }
    if (!out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Missing Q2 protocol output");
        return false;
    }
    *out=p;
    return true;
}
static const qa_q2_codec_ops *ops(const qa_q2_codec *c) {
    if(!c) return NULL;
    switch(c->protocol.kind) {
        case QA_NET_Q2_34: return &qa_q2_vanilla_ops;
        case QA_NET_R1Q2_35: return &qa_q2_r1q2_ops;
        case QA_NET_Q2PRO_36: return &qa_q2_q2pro_ops;
        case QA_NET_Q2REPRO_1038: case QA_NET_Q2PRIVATE_4038: return &qa_q2_rerelease_ops;
        case QA_NET_Q2KEX_2023: case QA_NET_Q2KEX_DEMO_2022: return &qa_q2_kex_ops;
        default: return NULL;
    }
}
bool qa_q2_config_layout_read(const qa_q2_codec *c, qa_q2_config_layout *out, qa_error *error) {
    if (!c || !out || !qa_q2_protocol_version(c->protocol)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 config layout lost its actual codec"); return false;
    }
    if (c->protocol.kind == QA_NET_Q2PRO_36 && c->protocol.revision >= 1024 && (c->wire_flags & 8u)) {
        *out = (qa_q2_config_layout){62,8254,10302,12350,12606,12862,61,60,59,8192,2048,2048,13630,true,8192};
    } else if (c->protocol.kind == QA_NET_Q2REPRO_1038 || c->protocol.kind == QA_NET_Q2KEX_2023 ||
        c->protocol.kind == QA_NET_Q2KEX_DEMO_2022) {
        *out = (qa_q2_config_layout){62,8254,10302,10814,11326,11582,61,60,59,8192,2048,512,12448,true,8192};
    } else {
        *out = (qa_q2_config_layout){32,288,544,800,1056,1312,31,30,29,256,256,256,2080,false,1024};
    }
    return true;
}
bool qa_q2_codec_init(qa_q2_codec *c, qa_net_protocol_id p, qa_error *error) {
    if(!c || !qa_q2_protocol_version(p)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Q2 codec identity");
        return false;
    }
    if(!qa_net_protocol_valid(p,error)) return false;
    if(p.kind==QA_NET_Q2PRO_36 && p.revision==1016) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Q2PRO 1016 is reserved");
        return false;
    }
    memset(c,0,sizeof(*c));
    c->protocol=p;
    c->wire_flags=p.flags;
    c->split_players=1;
    return true;
}
uint8_t qa_q2_service_opcode(qa_q2_codec *c,uint8_t raw) {
    if(c&&(c->protocol.kind==QA_NET_R1Q2_35||c->protocol.kind==QA_NET_Q2PRO_36)) {
        c->frame_extra=raw&0xe0;
        return raw&31;
    }
    return raw;
}
#define READ_DISPATCH(name,args) do { \
    const qa_q2_codec_ops *o=ops(c); \
    if(!o)return qa_net_reader_fail(r,"Invalid Q2 codec"); \
    return o->name args; \
} while(0)
#define WRITE_DISPATCH(name,args) do {const qa_q2_codec_ops *o=ops(c);if(!o)return qa_net_writer_fail(w,"Invalid Q2 codec");return o->name args;}while(0)
bool qa_q2_read_serverdata(qa_q2_codec*c,qa_net_reader*r,qa_q2_serverdata*d) {
    const qa_q2_codec_ops*o=ops(c);
    if(!o)return qa_net_reader_fail(r,"Invalid Q2 codec");
    if(!o->read_serverdata(c,r,d))return false;
    if(c->protocol.kind==QA_NET_R1Q2_35) {
        if(d->protocol_revision<1903||d->protocol_revision>1905)return qa_net_reader_fail(r,"Unsupported R1Q2 server revision");
        if(d->protocol_revision<c->protocol.revision)c->protocol.revision=d->protocol_revision;
    }
    return true;
}
bool qa_q2_write_serverdata(qa_q2_codec*c,qa_net_writer*w,const qa_q2_serverdata*d) {
    WRITE_DISPATCH(write_serverdata,(c,w,d));
}
bool qa_q2_read_entity_header(qa_q2_codec*c,qa_net_reader*r,uint32_t*n,uint64_t*b) {
    if(!n||!b)return qa_net_reader_fail(r,"Missing Q2 entity header output");
    READ_DISPATCH(read_entity_header,(c,r,n,b));
}
bool qa_q2_read_entity(qa_q2_codec*c,qa_net_reader*r,const qa_q2_entity*f,uint32_t n,uint64_t b,qa_q2_entity*t) {
    static const qa_q2_entity zero;
    if(!t)return qa_net_reader_fail(r,"Missing Q2 entity output");
    if(!f)f=&zero;
    READ_DISPATCH(read_entity,(c,r,f,n,b,t));
}
bool qa_q2_write_entity(qa_q2_codec*c,qa_net_writer*w,const qa_q2_entity*f,const qa_q2_entity*t,bool force,bool fresh) {
    static const qa_q2_entity zero;
    if(!t)return qa_net_writer_fail(w,"Missing Q2 entity state");
    if(!f)f=&zero;
    WRITE_DISPATCH(write_entity,(c,w,f,t,force,fresh));
}
bool qa_q2_write_entity_remove(qa_q2_codec*c,qa_net_writer*w,uint32_t n) {
    WRITE_DISPATCH(write_entity_remove,(c,w,n));
}
bool qa_q2_read_player(qa_q2_codec*c,qa_net_reader*r,const qa_q2_player*f,qa_q2_player*t) {
    static const qa_q2_player zero;
    if(!t)return qa_net_reader_fail(r,"Missing Q2 player output");
    if(!f)f=&zero;
    READ_DISPATCH(read_player,(c,r,f,t));
}
bool qa_q2_write_player(qa_q2_codec*c,qa_net_writer*w,const qa_q2_player*f,const qa_q2_player*t) {
    static const qa_q2_player zero;
    if(!t)return qa_net_writer_fail(w,"Missing Q2 player state");
    if(!f)f=&zero;
    WRITE_DISPATCH(write_player,(c,w,f,t));
}
bool qa_q2_read_frame_header(qa_q2_codec*c,qa_net_reader*r,qa_q2_frame_header*h) {
    READ_DISPATCH(read_frame_header,(c,r,h));
}
bool qa_q2_write_frame(qa_q2_codec*c,qa_net_writer*w,const qa_q2_frame_header*h,const qa_q2_player*f,const qa_q2_player*t,qa_q2_write_entities_fn fn,void*user) {
    static const qa_q2_player zero;
    if(!f)f=&zero;
    WRITE_DISPATCH(write_frame,(c,w,h,f,t,fn,user));
}
bool qa_q2_read_usercmd(qa_q2_codec*c,qa_net_reader*r,const qa_q2_usercmd*f,qa_q2_usercmd*t) {
    static const qa_q2_usercmd zero;
    if(!t)return qa_net_reader_fail(r,"Missing Q2 user command output");
    if(!f)f=&zero;
    const qa_q2_codec_ops *o=ops(c);
    if(!o)return qa_net_reader_fail(r,"Invalid Q2 codec");
    bool ok = o->read_usercmd(c,r,f,t);
    if (ok && c->protocol.kind != QA_NET_Q2KEX_2023 && c->protocol.kind != QA_NET_Q2KEX_DEMO_2022)
        t->float_angles = false;
    return ok;
}
bool qa_q2_write_usercmd(qa_q2_codec*c,qa_net_writer*w,const qa_q2_usercmd*f,const qa_q2_usercmd*t) {
    static const qa_q2_usercmd zero;
    if(!t)return qa_net_writer_fail(w,"Missing Q2 user command");
    if(!f)f=&zero;
    WRITE_DISPATCH(write_usercmd,(c,w,f,t));
}
bool qa_q2_write_entity_end(qa_q2_codec*c,qa_net_writer*w) {
    (void)c;
    return qa_net_write_u16(w,0);
}
bool qa_q2_write_baseline(qa_q2_codec*c,qa_net_writer*w,const qa_q2_entity*e) {
    qa_q2_entity zero= {
        0
    };
    return qa_net_write_u8(w,14)&&qa_q2_write_entity(c,w,&zero,e,true,true);
}
bool qa_q2_read_entities_begin(qa_q2_codec*c,qa_net_reader*r) {
    if((c->protocol.kind==QA_NET_Q2_34||c->protocol.kind==QA_NET_Q2KEX_2023||c->protocol.kind==QA_NET_Q2KEX_DEMO_2022)&&qa_net_read_u8(r)!=18)return qa_net_reader_fail(r,"Expected Q2 packetentities");
    return !r->failed;
}
bool qa_q2_write_entities_begin(qa_q2_codec*c,qa_net_writer*w) {
    return (c->protocol.kind!=QA_NET_Q2_34&&c->protocol.kind!=QA_NET_Q2KEX_2023&&c->protocol.kind!=QA_NET_Q2KEX_DEMO_2022)||qa_net_write_u8(w,18);
}
float qa_q2_read_coord(qa_net_reader*r) {
    return (float)qa_net_read_i16(r)*0.125f;
}
float qa_q2_read_angle8(qa_net_reader*r) {
    return (float)qa_net_read_i8(r)*(360.0f/256.0f);
}
float qa_q2_read_angle16(qa_net_reader*r) {
    return (float)qa_net_read_i16(r)*(360.0f/65536.0f);
}
bool qa_q2_write_scaled(qa_net_writer*w,float f,float scale,unsigned bits,bool sign) {
    if(!isfinite(f)||!isfinite(scale)||bits==0||bits>32)return qa_net_writer_fail(w,"Invalid Q2 fixed-point value");
    double v=trunc((double)f*(double)scale),lo=sign?-ldexp(1.0,(int)bits-1):0.0,hi=ldexp(1.0,(int)bits-(sign?1:0))-1.0;
    if(v<lo||v>hi)return qa_net_writer_fail(w,"Q2 fixed-point value outside wire range");
    return qa_net_write_bits(w,(uint32_t)(int64_t)v,bits);
}
bool qa_q2_write_coord(qa_net_writer*w,float f) {
    return qa_q2_write_scaled(w,f,8.0f,16,true);
}
static bool angle(qa_net_writer*w,float f,unsigned bits) {
    if(!isfinite(f))return qa_net_writer_fail(w,"Nonfinite Q2 angle");
    uint32_t value=bits==16 ? qa_angle_to_word(f) : (uint32_t)qa_source_float_to_i32(f*256.0f/360.0f);
    return qa_net_write_bits(w,value,bits);
}
bool qa_q2_write_angle8(qa_net_writer*w,float f) {
    return angle(w,f,8);
}
bool qa_q2_write_angle16(qa_net_writer*w,float f) {
    return angle(w,f,16);
}
bool qa_q2_read_vec3(qa_net_reader*r,float v[3],bool floating) {
    for(unsigned i=0;i<3;i++) {
        v[i]=floating?qa_net_read_f32(r):qa_q2_read_coord(r);
        if(!isfinite(v[i]))return qa_net_reader_fail(r,"Nonfinite Q2 position");
    }
    return !r->failed;
}
bool qa_q2_write_vec3(qa_net_writer*w,const float v[3],bool floating) {
    for(unsigned i=0;i<3;i++) {
        if(!isfinite(v[i]))return qa_net_writer_fail(w,"Nonfinite Q2 position");
        if(!(floating?qa_net_write_f32(w,v[i]):qa_q2_write_coord(w,v[i])))return false;
    }
    return true;
}
bool qa_q2_read_entity_header_common(qa_q2_codec*c,qa_net_reader*r,uint32_t*n,uint64_t*b) {
    (void)c;
    uint64_t bits=qa_net_read_u8(r);
    if(bits&128)bits|=(uint64_t)qa_net_read_u8(r)<<8;
    if(bits&32768)bits|=(uint64_t)qa_net_read_u8(r)<<16;
    if(bits&8388608)bits|=(uint64_t)qa_net_read_u8(r)<<24;
    *n=(bits&256)?qa_net_read_u16(r):qa_net_read_u8(r);
    *b=bits;
    return !r->failed;
}
bool qa_q2_write_entity_header_common(qa_q2_codec*c,qa_net_writer*w,uint32_t n,uint64_t b) {
    (void)c;
    if(n>65535||b>UINT32_MAX)return qa_net_writer_fail(w,"Q2 entity header outside wire range");
    if(n>255)b|=256;
    if(b&UINT64_C(0xff000000))b|=8388608|32768|128;
    else if(b&0xff0000)b|=32768|128;
    else if(b&0xff00)b|=128;
    qa_net_write_u8(w,(uint8_t)b);
    if(b&128)qa_net_write_u8(w,(uint8_t)(b>>8));
    if(b&32768)qa_net_write_u8(w,(uint8_t)(b>>16));
    if(b&8388608)qa_net_write_u8(w,(uint8_t)(b>>24));
    if(b&256)qa_net_write_u16(w,(uint16_t)n);
    else qa_net_write_u8(w,(uint8_t)n);
    return !w->failed;
}
bool qa_q2_classic_read_usercmd(qa_q2_codec*c,qa_net_reader*r,const qa_q2_usercmd*f,qa_q2_usercmd*t) {
    (void)c;
    static const qa_q2_usercmd zero;
    if(!t)return qa_net_reader_fail(r,"Missing classic Q2 user command output");
    if(!f)f=&zero;
    *t=*f;
    uint8_t b=qa_net_read_u8(r);
    for(unsigned i=0;i<3;i++)if(b&(1u<<i))t->angles[i]=qa_net_read_i16(r);
    if(b&8)t->forwardmove=qa_net_read_i16(r);
    if(b&16)t->sidemove=qa_net_read_i16(r);
    if(b&32)t->upmove=qa_net_read_i16(r);
    if(b&64)t->buttons=qa_net_read_u8(r);
    if(b&128)t->impulse=qa_net_read_u8(r);
    t->msec=qa_net_read_u8(r);
    t->lightlevel=qa_net_read_u8(r);
    return !r->failed;
}
bool qa_q2_classic_write_usercmd(qa_q2_codec*c,qa_net_writer*w,const qa_q2_usercmd*f,const qa_q2_usercmd*t) {
    (void)c;
    static const qa_q2_usercmd zero;
    if(!t)return qa_net_writer_fail(w,"Missing classic Q2 user command");
    if(!f)f=&zero;
    if(!isfinite(t->forwardmove)||!isfinite(t->sidemove)||!isfinite(t->upmove)||truncf(t->forwardmove)!=t->forwardmove||truncf(t->sidemove)!=t->sidemove||truncf(t->upmove)!=t->upmove)return qa_net_writer_fail(w,"Fractional classic Q2 movement cannot be represented");
    if(t->forwardmove<INT16_MIN||t->forwardmove>INT16_MAX||t->sidemove<INT16_MIN||t->sidemove>INT16_MAX||t->upmove<INT16_MIN||t->upmove>INT16_MAX)return qa_net_writer_fail(w,"Classic Q2 movement outside wire range");
    if(t->server_frame!=f->server_frame)return qa_net_writer_fail(w,"Classic Q2 command cannot carry a server frame");
    uint8_t b=0;
    for(unsigned i=0;i<3;i++)if(t->angles[i]!=f->angles[i])b|=(uint8_t)(1u<<i);
    if(t->forwardmove!=f->forwardmove)b|=8;
    if(t->sidemove!=f->sidemove)b|=16;
    if(t->upmove!=f->upmove)b|=32;
    if(t->buttons!=f->buttons)b|=64;
    if(t->impulse!=f->impulse)b|=128;
    qa_net_write_u8(w,b);
    for(unsigned i=0;i<3;i++)if(b&(1u<<i))qa_net_write_i16(w,t->angles[i]);
    if(b&8)qa_q2_write_scaled(w,t->forwardmove,1,16,true);
    if(b&16)qa_q2_write_scaled(w,t->sidemove,1,16,true);
    if(b&32)qa_q2_write_scaled(w,t->upmove,1,16,true);
    if(b&64)qa_net_write_u8(w,t->buttons);
    if(b&128)qa_net_write_u8(w,t->impulse);
    qa_net_write_u8(w,t->msec);
    qa_net_write_u8(w,t->lightlevel);
    return !w->failed;
}
