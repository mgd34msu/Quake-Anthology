#include "q2pro_internal.h"

bool qa_q2pro_extensions(const qa_q2_codec *c) {
    return (c->protocol.revision>=1024 && (c->wire_flags&8u)) || (c->protocol.revision>=1025 && (c->wire_flags&16u));
}
bool qa_q2pro_extensions_v2(const qa_q2_codec *c) { return c->protocol.revision>=1025 && (c->wire_flags&16u); }

bool qa_q2pro_read_int23(qa_net_reader *r, int32_t previous, int32_t *out) {
    uint16_t low=qa_net_read_u16(r); int64_t value;
    if (low&1u) {
        uint32_t raw=(uint32_t)low|((uint32_t)qa_net_read_u8(r)<<16);
        int32_t full=(int32_t)raw;
        if (raw&UINT32_C(0x800000)) full-=INT32_C(0x1000000);
        value=(int64_t)(full-(full<0?1:0))/2;
    } else {
        int32_t delta=(int32_t)low;
        if (low&0x8000u) delta-=65536;
        value=(int64_t)previous+delta/2;
    }
    if (r->failed) return false;
    if (!out || value< -4194304 || value>4194303) return qa_net_reader_fail(r,"Q2PRO int23 outside coordinate range");
    *out=(int32_t)value; return true;
}
bool qa_q2pro_write_int23(qa_net_writer *w, int32_t current, int32_t previous) {
    if (current< -4194304 || current>4194303) return qa_net_writer_fail(w,"Q2PRO coordinate exceeds signed 23-bit range");
    int64_t delta=(int64_t)current-previous;
    if (delta>= -16384 && delta<16384) return qa_net_write_u16(w,(uint16_t)((uint32_t)delta<<1));
    uint32_t value=((uint32_t)current<<1)|1u;
    return qa_net_write_u16(w,(uint16_t)value) && qa_net_write_u8(w,(uint8_t)(value>>16));
}
bool qa_q2pro_read_var64(qa_net_reader *r, uint64_t *out) {
    uint64_t value=0;
    for (unsigned shift=0;shift<70;shift+=7) {
        uint8_t byte=qa_net_read_u8(r);
        if (r->failed) return false;
        if (shift==63 && byte>1) return qa_net_reader_fail(r,"Invalid Q2PRO variable uint64");
        value|=(uint64_t)(byte&127u)<<shift;
        if (!(byte&128u)) {
            if (!out) return qa_net_reader_fail(r,"Missing Q2PRO variable integer output");
            *out=value; return true;
        }
    }
    return qa_net_reader_fail(r,"Unterminated Q2PRO variable uint64");
}
bool qa_q2pro_write_var64(qa_net_writer *w, uint64_t value) {
    do {
        uint8_t byte=(uint8_t)(value&127u); value>>=7;
        if (!qa_net_write_u8(w,(uint8_t)(byte|(value?128u:0u)))) return false;
    } while (value);
    return true;
}
uint8_t qa_q2pro_fog_bits(const qa_q2_player_fog *f, const qa_q2_player_fog *t) {
    return (uint8_t)((memcmp(f->color,t->color,3)?1u:0u) |
        (f->density!=t->density || f->sky_factor!=t->sky_factor?2u:0u) |
        (f->height_density!=t->height_density?4u:0u) | (f->height_falloff!=t->height_falloff?8u:0u) |
        (memcmp(f->height_start_color,t->height_start_color,3)?16u:0u) |
        (memcmp(f->height_end_color,t->height_end_color,3)?32u:0u) |
        (f->height_start_distance!=t->height_start_distance?64u:0u) |
        (f->height_end_distance!=t->height_end_distance?128u:0u));
}
bool qa_q2pro_read_fog(qa_net_reader *r, const qa_q2_player_fog *from, qa_q2_player_fog *out) {
    if (!from || !out) return qa_net_reader_fail(r,"Missing Q2PRO fog state");
    qa_q2_player_fog f=*from; uint8_t bits=qa_net_read_u8(r);
    if (bits&1u) qa_net_read_data(r,f.color,3);
    if (bits&2u) { f.density=qa_net_read_u16(r); f.sky_factor=qa_net_read_u16(r); }
    if (bits&4u) f.height_density=qa_net_read_u16(r);
    if (bits&8u) f.height_falloff=qa_net_read_u16(r);
    if (bits&16u) qa_net_read_data(r,f.height_start_color,3);
    if (bits&32u) qa_net_read_data(r,f.height_end_color,3);
    if ((bits&64u) && !qa_q2pro_read_int23(r,0,&f.height_start_distance)) return false;
    if ((bits&128u) && !qa_q2pro_read_int23(r,0,&f.height_end_distance)) return false;
    if (r->failed) return false;
    *out=f; return true;
}
bool qa_q2pro_write_fog(qa_net_writer *w, uint8_t bits, const qa_q2_player_fog *f) {
    if (!f) return qa_net_writer_fail(w,"Missing Q2PRO fog state");
    qa_net_write_u8(w,bits);
    if (bits&1u) qa_net_write_data(w,f->color,3);
    if (bits&2u) { qa_net_write_u16(w,f->density); qa_net_write_u16(w,f->sky_factor); }
    if (bits&4u) qa_net_write_u16(w,f->height_density);
    if (bits&8u) qa_net_write_u16(w,f->height_falloff);
    if (bits&16u) qa_net_write_data(w,f->height_start_color,3);
    if (bits&32u) qa_net_write_data(w,f->height_end_color,3);
    if ((bits&64u) && !qa_q2pro_write_int23(w,f->height_start_distance,0)) return false;
    if ((bits&128u) && !qa_q2pro_write_int23(w,f->height_end_distance,0)) return false;
    return !w->failed;
}

static const uint64_t origin_bits[3]={1,2,512},angle_bits[3]={1024,4,8},model_bits[4]={2048,1048576,2097152,4194304};
#define NUMBER16 UINT64_C(256)
#define ANGLE16 UINT64_C(8192)
#define MODEL16 UINT64_C(268435456)
#define MORE_FX8 UINT64_C(536870912)
#define ALPHA UINT64_C(1073741824)
#define SCALE UINT64_C(4294967296)
#define MORE_FX16 UINT64_C(8589934592)
#define ENTITY_EXTENSIONS (MODEL16|MORE_FX8|MORE_FX16|ALPHA|SCALE)

static uint64_t width(uint32_t value,uint64_t byte,uint64_t word) { return value<256?byte:value<65536?word:byte|word; }
static bool write_width(qa_net_writer *w,uint64_t bits,uint32_t value,uint64_t byte,uint64_t word) {
    if ((bits&(byte|word))==(byte|word)) return qa_net_write_u32(w,value);
    if (bits&byte) return qa_net_write_u8(w,(uint8_t)value);
    if (bits&word) return qa_net_write_u16(w,(uint16_t)value);
    return true;
}
static uint32_t read_width(qa_net_reader *r,uint64_t bits,uint32_t value,uint64_t byte,uint64_t word) {
    if ((bits&(byte|word))==(byte|word)) return qa_net_read_u32(r);
    if (bits&byte) return qa_net_read_u8(r);
    if (bits&word) return qa_net_read_u16(r);
    return value;
}

bool qa_q2pro_entity_header(qa_q2_codec *c,qa_net_reader *r,uint32_t *number,uint64_t *out) {
    uint64_t bits=qa_net_read_u8(r);
    for (unsigned i=1;i<=4;++i) if (bits&(UINT64_C(1)<<(i*8-1))) bits|=(uint64_t)qa_net_read_u8(r)<<(i*8);
    uint32_t n=(bits&NUMBER16)?qa_net_read_u16(r):qa_net_read_u8(r);
    if (r->failed) return false;
    if (!number || !out || n>8191 || (bits>>34) || (!n && bits)) return qa_net_reader_fail(r,"Invalid Q2PRO entity header");
    if ((bits&64u) && (bits&~(UINT64_C(64)|NUMBER16|UINT64_C(0x80808080))))
        return qa_net_reader_fail(r,"Q2PRO removal contains entity fields");
    if ((!qa_q2pro_extensions(c) && (bits&ENTITY_EXTENSIONS)) || (c->protocol.revision<1018 && (bits&ANGLE16)))
        return qa_net_reader_fail(r,"Unnegotiated Q2PRO entity extension");
    *number=n; *out=bits; return true;
}
static bool write_header(qa_net_writer *w,uint32_t number,uint64_t bits) {
    if (number>=256) bits|=NUMBER16;
    for (unsigned i=4;i>0;--i) if (bits>>(i*8)) bits|=UINT64_C(1)<<(i*8-1);
    qa_net_write_u8(w,(uint8_t)bits);
    for (unsigned i=1;i<=4;++i) if (bits&(UINT64_C(1)<<(i*8-1))) qa_net_write_u8(w,(uint8_t)(bits>>(i*8)));
    if (bits&NUMBER16) qa_net_write_u16(w,(uint16_t)number); else qa_net_write_u8(w,(uint8_t)number);
    return !w->failed;
}
bool qa_q2pro_entity_remove(qa_q2_codec *c,qa_net_writer *w,uint32_t number) {
    (void)c;
    if (!number || number>8191) return qa_net_writer_fail(w,"Q2PRO removal outside entity range");
    return write_header(w,number,64);
}
static bool eighths(qa_net_writer *w,float input,int32_t *out,bool v2) {
    double value=trunc((double)input*8.0);
    if (!isfinite(value) || value<(v2?-4194304:-32768) || value>(v2?4194303:32767))
        return qa_net_writer_fail(w,"Q2PRO origin outside negotiated coordinate range");
    *out=(int32_t)value; return true;
}
static bool alpha_byte(qa_net_writer *w,float value,float factor) {
    if (!isfinite(value)) return qa_net_writer_fail(w,"Nonfinite Q2PRO entity scalar");
    double scaled=trunc((double)value*factor);
    uint8_t byte=value==0?0:scaled<1?1:scaled>255?255:(uint8_t)scaled;
    return qa_net_write_u8(w,byte);
}

bool qa_q2pro_entity_write(qa_q2_codec *c,qa_net_writer *w,const qa_q2_entity *from,const qa_q2_entity *to,bool force,bool fresh) {
    if (!from || !to || !to->number || to->number>8191) return qa_net_writer_fail(w,"Q2PRO entity outside source range");
    if (to->instance_bits || to->owner || to->old_frame) return qa_net_writer_fail(w,"Q2PRO cannot represent KEX entity fields");
    if (to->effects>UINT32_MAX || from->effects>UINT32_MAX) return qa_net_writer_fail(w,"Q2PRO effects use uint32 plus separate morefx");
    bool extended=qa_q2pro_extensions(c),v2=qa_q2pro_extensions_v2(c);
    uint64_t bits=0;
    uint32_t old_models[4]={from->modelindex,from->modelindex2,from->modelindex3,from->modelindex4};
    uint32_t models[4]={to->modelindex,to->modelindex2,to->modelindex3,to->modelindex4};
    for (unsigned i=0;i<3;++i) {
        if (from->origin[i]!=to->origin[i]) bits|=origin_bits[i];
        if (from->angles[i]!=to->angles[i]) bits|=angle_bits[i];
    }
    if ((bits&(1024u|4u|8u)) && c->protocol.revision>=1018) bits|=ANGLE16;
    for (unsigned i=0;i<4;++i) if (old_models[i]!=models[i]) {
        if (models[i]>65535) return qa_net_writer_fail(w,"Q2PRO model index exceeds uint16");
        bits|=model_bits[i]; if (models[i]>255) bits|=MODEL16;
    }
    if (to->frame>65535 || to->event>255) return qa_net_writer_fail(w,"Q2PRO entity frame or event outside wire range");
    if (from->frame!=to->frame) bits|=to->frame<256?16u:131072u;
    if (from->skinnum!=to->skinnum) bits|=width(to->skinnum,65536,33554432);
    if (from->effects!=to->effects) bits|=width((uint32_t)to->effects,16384,524288);
    if (from->renderfx!=to->renderfx) bits|=width(to->renderfx,4096,262144);
    if (from->morefx!=to->morefx) bits|=width(to->morefx,MORE_FX8,MORE_FX16);
    if (from->alpha!=to->alpha) bits|=ALPHA;
    if (from->scale!=to->scale) bits|=SCALE;
    if (!extended && (bits&ENTITY_EXTENSIONS)) return qa_net_writer_fail(w,"Q2PRO entity needs game extensions");
    if (from->solid!=to->solid) bits|=134217728;
    if (to->event) bits|=32;
    bool volume=from->loop_volume!=to->loop_volume,attenuation=from->loop_attenuation!=to->loop_attenuation;
    if (from->sound!=to->sound || volume || attenuation) bits|=67108864;
    if ((!extended && (to->sound>255 || volume || attenuation)) || to->sound>16383)
        return qa_net_writer_fail(w,"Q2PRO looping sound exceeds negotiated format");
    bool old_origin=false;
    for (unsigned i=0;i<3;++i) if (from->old_origin[i]!=to->old_origin[i]) old_origin=true;
    if (fresh || ((to->renderfx&128u) && (c->protocol.revision<1017 || old_origin))) bits|=16777216;
    if (!bits && !force) return !w->failed;
    if (!write_header(w,to->number,bits)) return false;
    for (unsigned i=0;i<4;++i) if (bits&model_bits[i]) {
        if (bits&MODEL16) qa_net_write_u16(w,(uint16_t)models[i]); else qa_net_write_u8(w,(uint8_t)models[i]);
    }
    if (bits&16u) qa_net_write_u8(w,(uint8_t)to->frame); else if (bits&131072u) qa_net_write_u16(w,(uint16_t)to->frame);
    write_width(w,bits,to->skinnum,65536,33554432);
    write_width(w,bits,(uint32_t)to->effects,16384,524288);
    write_width(w,bits,to->renderfx,4096,262144);
    for (unsigned i=0;i<3;++i) if (bits&origin_bits[i]) {
        int32_t current,previous;
        if (!eighths(w,to->origin[i],&current,v2) || !eighths(w,from->origin[i],&previous,v2)) return false;
        if (v2) qa_q2pro_write_int23(w,current,previous); else qa_net_write_i16(w,(int16_t)current);
    }
    for (unsigned i=0;i<3;++i) if (bits&angle_bits[i]) {
        if (bits&ANGLE16) qa_q2_write_angle16(w,to->angles[i]); else qa_q2_write_angle8(w,to->angles[i]);
    }
    if (bits&16777216u) for (unsigned i=0;i<3;++i) {
        int32_t value;
        if (!eighths(w,to->old_origin[i],&value,v2)) return false;
        if (v2) qa_q2pro_write_int23(w,value,0); else qa_net_write_i16(w,(int16_t)value);
    }
    if (bits&67108864u) {
        if (extended) {
            qa_net_write_u16(w,(uint16_t)(to->sound|(volume?16384u:0u)|(attenuation?32768u:0u)));
            if (volume) qa_q2_write_scaled(w,to->loop_volume,255,8,false);
            if (attenuation) {
                if (to->loop_attenuation==-1) qa_net_write_u8(w,192); else qa_q2_write_scaled(w,to->loop_attenuation,64,8,false);
            }
        } else qa_net_write_u8(w,(uint8_t)to->sound);
    }
    if (bits&32u) qa_net_write_u8(w,(uint8_t)to->event);
    if (bits&134217728u) qa_net_write_u32(w,to->solid);
    write_width(w,bits,to->morefx,MORE_FX8,MORE_FX16);
    if (bits&ALPHA) alpha_byte(w,to->alpha,255);
    if (bits&SCALE) alpha_byte(w,to->scale,16);
    return !w->failed;
}

bool qa_q2pro_entity_read(qa_q2_codec *c,qa_net_reader *r,const qa_q2_entity *from,uint32_t number,uint64_t bits,qa_q2_entity *out) {
    if (!from || !out || !number || number>8191 || (bits>>34) || (bits&64u) || from->effects>UINT32_MAX)
        return qa_net_reader_fail(r,"Invalid Q2PRO entity delta or baseline");
    bool extended=qa_q2pro_extensions(c),v2=qa_q2pro_extensions_v2(c);
    if ((!extended && (bits&ENTITY_EXTENSIONS)) || (c->protocol.revision<1018 && (bits&ANGLE16)))
        return qa_net_reader_fail(r,"Unnegotiated Q2PRO entity extension");
    qa_q2_entity to=*from; to.number=number; to.event=0;
    memcpy(to.old_origin,from->origin,sizeof(to.old_origin));
    uint32_t *models[4]={&to.modelindex,&to.modelindex2,&to.modelindex3,&to.modelindex4};
    for (unsigned i=0;i<4;++i) if (bits&model_bits[i]) *models[i]=(bits&MODEL16)?qa_net_read_u16(r):qa_net_read_u8(r);
    if (bits&16u) to.frame=qa_net_read_u8(r); else if (bits&131072u) to.frame=qa_net_read_u16(r);
    to.skinnum=read_width(r,bits,to.skinnum,65536,33554432);
    to.effects=read_width(r,bits,(uint32_t)to.effects,16384,524288);
    to.renderfx=read_width(r,bits,to.renderfx,4096,262144);
    for (unsigned i=0;i<3;++i) if (bits&origin_bits[i]) {
        int32_t value;
        if (v2) {
            double previous=trunc((double)from->origin[i]*8.0);
            if (!isfinite(previous) || previous< -4194304 || previous>4194303) return qa_net_reader_fail(r,"Invalid Q2PRO coordinate baseline");
            if (!qa_q2pro_read_int23(r,(int32_t)previous,&value)) return false;
        } else value=qa_net_read_i16(r);
        to.origin[i]=(float)value*0.125f;
    }
    for (unsigned i=0;i<3;++i) if (bits&angle_bits[i]) to.angles[i]=(bits&ANGLE16)?qa_q2_read_angle16(r):qa_q2_read_angle8(r);
    if (bits&16777216u) for (unsigned i=0;i<3;++i) {
        int32_t value;
        if (v2) { if (!qa_q2pro_read_int23(r,0,&value)) return false; } else value=qa_net_read_i16(r);
        to.old_origin[i]=(float)value*0.125f;
    }
    if (bits&67108864u) {
        if (extended) {
            uint16_t sound=qa_net_read_u16(r); to.sound=sound&16383u;
            if (sound&16384u) to.loop_volume=(float)qa_net_read_u8(r)/255.0f;
            if (sound&32768u) { uint8_t value=qa_net_read_u8(r); to.loop_attenuation=value==192?-1.0f:(float)value/64.0f; }
        } else to.sound=qa_net_read_u8(r);
    }
    if (bits&32u) to.event=qa_net_read_u8(r);
    if (bits&134217728u) to.solid=qa_net_read_u32(r);
    to.morefx=read_width(r,bits,to.morefx,MORE_FX8,MORE_FX16);
    if (bits&ALPHA) to.alpha=(float)qa_net_read_u8(r)/255.0f;
    if (bits&SCALE) to.scale=(float)qa_net_read_u8(r)/16.0f;
    if (r->failed) return false;
    *out=to; return true;
}
