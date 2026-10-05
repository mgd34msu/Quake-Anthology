#include "qa/network_q1_nq.h"
#include "qa/network_q1_decoder_save.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    U_MORE=1, U_ORIGIN1=2, U_ANGLE2=16, U_STEP=32, U_FRAME=64,
    U_ANGLE1=256, U_ANGLE3=512, U_MODEL=1024, U_COLORMAP=2048,
    U_SKIN=4096, U_EFFECTS=8192, U_LONG=16384, U_EXTEND1=32768,
    U_ALPHA=65536, U_FRAME2=131072, U_MODEL2=262144,
    U_LERP=524288, U_SCALE=1048576, U_EXTEND2=8388608,
    SU_VIEW=1, SU_PITCH=2, SU_PUNCH1=4, SU_VELOCITY1=32,
    SU_ITEMS=512, SU_GROUND=1024, SU_WATER=2048, SU_FRAME=4096,
    SU_ARMOR=8192, SU_WEAPON=16384, SU_EXTEND1=32768,
    SU_WEAPON2=65536, SU_ARMOR2=131072, SU_AMMO2=262144,
    SU_SHELLS2=524288, SU_NAILS2=1048576, SU_ROCKETS2=2097152,
    SU_CELLS2=4194304, SU_EXTEND2=8388608, SU_FRAME2=16777216,
    SU_ALPHA=33554432
};
static const uint32_t angle_bits[3]={U_ANGLE1,U_ANGLE2,U_ANGLE3};
struct qa_nq_decoder {
    qa_net_protocol_id protocol;
    qa_nq_options options;
    float time;
    qa_q1_entity *baselines;
    uint8_t *present;
    const char **models, **sounds;
    const qa_q1_entity *source_baselines;
    size_t source_baseline_count;
};
static bool nq_profile(qa_net_protocol_id p)
{
    return qa_q1_profile_valid(p,NULL) && !qa_q1_is_qw(p);
}
static bool entity_valid(qa_net_protocol_id p, const qa_q1_entity *s)
{
    uint32_t limit=p.kind==QA_NET_NQ15?255:65535;
    if (!s || s->number>65535 || s->model>limit || s->frame>limit || s->colormap>255 ||
        s->skin>255 || s->effects>255 || s->qw_flags || !isfinite(s->lerp_finish)) return false;
    if (p.kind==QA_NET_NQ15 && (s->alpha!=0 || s->scale!=16 || s->lerp_finish!=0)) return false;
    for (unsigned i=0;i<3;++i) if (!isfinite(s->origin[i]) || !isfinite(s->angles[i])) return false;
    return true;
}
bool qa_nq_decoder_create(qa_net_protocol_id p, qa_nq_options options, qa_nq_decoder **out, qa_error *e)
{
    if (!out || !nq_profile(p)) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid NetQuake decoder configuration"); return false; }
    qa_nq_decoder *d=calloc(1,sizeof(*d));
    if (d) {
        d->baselines=calloc(65536,sizeof(*d->baselines)); d->present=calloc(65536,1);
        d->models=calloc(8192,sizeof(*d->models)); d->sounds=calloc(8192,sizeof(*d->sounds));
    }
    if (!d || !d->baselines || !d->present || !d->models || !d->sounds) {
        qa_nq_decoder_destroy(d); qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating NetQuake decoder"); return false;
    }
    d->protocol=p; d->options=options; *out=d; return true;
}
void qa_nq_decoder_destroy(qa_nq_decoder *d)
{
    if (d) { free(d->baselines); free(d->present); free(d->models); free(d->sounds); free(d); }
}
void qa_nq_decoder_reset(qa_nq_decoder *d) { if (d) { memset(d->present,0,65536); d->time=0; } }
qa_net_protocol_id qa_nq_decoder_protocol(const qa_nq_decoder *d) { return d ? d->protocol : (qa_net_protocol_id){QA_NET_NQ15,0,0}; }
float qa_nq_decoder_time(const qa_nq_decoder *d) { return d?d->time:0; }
const qa_q1_entity *qa_nq_decoder_baseline(const qa_nq_decoder *d, uint32_t number)
{
    if (!d || number >= 65536) return NULL;
    if (d->present) return d->present[number] ? &d->baselines[number] : NULL;
    size_t first = 0, end = d->source_baseline_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (d->source_baselines[middle].number < number) first = middle + 1; else end = middle;
    }
    return first < d->source_baseline_count && d->source_baselines[first].number == number ?
        d->source_baselines + first : NULL;
}
bool qa_nq_decoder_set_baseline(qa_nq_decoder *d, const qa_q1_entity *s, qa_error *e)
{
    if (!d || !entity_valid(d->protocol,s) || s->step || s->lerp_finish!=0 || s->effects) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Unrepresentable NetQuake baseline"); return false;
    }
    d->baselines[s->number]=*s; d->present[s->number]=1; return true;
}
bool qa_nq_decoder_set_time(qa_nq_decoder *d, float time, qa_error *e)
{
    if (!d || !isfinite(time)) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Invalid NetQuake time"); return false; }
    d->time=time; return true;
}
static float read_float(qa_net_reader *r)
{
    float value=qa_net_read_f32(r);
    if (!isfinite(value)) qa_net_reader_fail(r,"Non-finite NetQuake float");
    return value;
}
static void read_vec(qa_net_reader *r, qa_net_protocol_id p, float v[3])
{
    for (unsigned i=0;i<3;++i) v[i]=qa_q1_read_coord(r,p);
}
static void write_vec(qa_net_writer *w, qa_net_protocol_id p, const float v[3])
{
    for (unsigned i=0;i<3;++i) qa_q1_write_coord(w,p,v[i]);
}
static bool read_entity(qa_nq_decoder *d, qa_net_reader *r, uint32_t bits, qa_q1_entity *out)
{
    if (bits & U_MORE) bits|=(uint32_t)qa_net_read_u8(r)<<8;
    bool wide=d->protocol.kind!=QA_NET_NQ15;
    if (wide && (bits & U_EXTEND1)) bits|=(uint32_t)qa_net_read_u8(r)<<16;
    if (wide && (bits & U_EXTEND2)) bits|=(uint32_t)qa_net_read_u8(r)<<24;
    uint32_t allowed=wide?0x009fff7fu:0x00007f7fu;
    if ((bits & ~allowed) || ((bits & U_FRAME2) && !(bits & U_FRAME)) || ((bits & U_MODEL2) && !(bits & U_MODEL)))
        return qa_net_reader_fail(r,"Unknown or inconsistent NetQuake entity flags");
    uint32_t number=bits & U_LONG?qa_net_read_u16(r):qa_net_read_u8(r);
    qa_q1_entity s;
    const qa_q1_entity *baseline=qa_nq_decoder_baseline(d,number);
    if (baseline) s=*baseline; else qa_q1_entity_init(&s);
    s.number=number;
    if (bits & U_MODEL) s.model=qa_net_read_u8(r);
    if (bits & U_FRAME) s.frame=qa_net_read_u8(r);
    if (bits & U_COLORMAP) s.colormap=qa_net_read_u8(r);
    if (bits & U_SKIN) s.skin=qa_net_read_u8(r);
    if (bits & U_EFFECTS) s.effects=qa_net_read_u8(r);
    for (unsigned i=0;i<3;++i) {
        if (bits & ((uint32_t)U_ORIGIN1<<i)) s.origin[i]=qa_q1_read_coord(r,d->protocol);
        if (bits & angle_bits[i]) s.angles[i]=qa_q1_read_angle(r,d->protocol);
    }
    if (bits & U_ALPHA) s.alpha=qa_net_read_u8(r);
    if (bits & U_SCALE) s.scale=qa_net_read_u8(r);
    if (bits & U_FRAME2) s.frame|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & U_MODEL2) s.model|=(uint32_t)qa_net_read_u8(r)<<8;
    s.step=(bits & U_STEP)!=0;
    s.lerp_finish=bits & U_LERP ? d->time+(float)qa_net_read_u8(r)/255.0f : 0;
    if (r->failed) return false;
    *out=s; return true;
}
static bool read_baseline(qa_net_reader *r, qa_net_protocol_id p, bool version2, uint32_t number, qa_q1_entity *out)
{
    if (version2 && p.kind==QA_NET_NQ15) return qa_net_reader_fail(r,"Extended baseline requires FitzQuake or RMQ");
    uint8_t bits=version2?qa_net_read_u8(r):0;
    if (bits & ~15u) return qa_net_reader_fail(r,"Unknown baseline flags");
    qa_q1_entity s; qa_q1_entity_init(&s); s.number=number;
    s.model=bits & 1?qa_net_read_u16(r):qa_net_read_u8(r);
    s.frame=bits & 2?qa_net_read_u16(r):qa_net_read_u8(r);
    s.colormap=qa_net_read_u8(r); s.skin=qa_net_read_u8(r);
    for (unsigned i=0;i<3;++i) { s.origin[i]=qa_q1_read_coord(r,p); s.angles[i]=qa_q1_read_angle(r,p); }
    if (bits & 4) s.alpha=qa_net_read_u8(r);
    if (bits & 8) s.scale=qa_net_read_u8(r);
    if (r->failed) return false;
    *out=s; return true;
}
static bool read_clientdata(qa_net_reader *r, qa_net_protocol_id p, bool standard, qa_q1_clientdata *out)
{
    uint32_t bits=qa_net_read_u16(r);
    if (p.kind!=QA_NET_NQ15 && (bits & SU_EXTEND1)) bits|=(uint32_t)qa_net_read_u8(r)<<16;
    if (p.kind!=QA_NET_NQ15 && (bits & SU_EXTEND2)) bits|=(uint32_t)qa_net_read_u8(r)<<24;
    uint32_t allowed=p.kind==QA_NET_NQ15?0x7effu:0x03fffeffu;
    if ((bits & ~allowed) || ((bits & SU_FRAME2) && !(bits & SU_FRAME)) ||
        ((bits & (SU_WEAPON2|SU_ALPHA)) && !(bits & SU_WEAPON)) ||
        ((bits & SU_ARMOR2) && !(bits & SU_ARMOR)))
        return qa_net_reader_fail(r,"Unknown or inconsistent clientdata flags");
    qa_q1_clientdata c={0};
    c.viewheight=bits & SU_VIEW?(float)qa_net_read_i8(r):22.0f;
    c.idealpitch=bits & SU_PITCH?(float)qa_net_read_i8(r):0.0f;
    for (unsigned i=0;i<3;++i) {
        if (bits & ((uint32_t)SU_PUNCH1<<i)) c.punch[i]=(float)qa_net_read_i8(r);
        if (bits & ((uint32_t)SU_VELOCITY1<<i)) c.velocity[i]=(float)qa_net_read_i8(r)*16;
    }
    c.items=qa_net_read_u32(r);
    c.onground=(bits & SU_GROUND)!=0; c.inwater=(bits & SU_WATER)!=0;
    if (bits & SU_FRAME) c.weapon_frame=qa_net_read_u8(r);
    if (bits & SU_ARMOR) c.armor=qa_net_read_u8(r);
    if (bits & SU_WEAPON) c.weapon_model=qa_net_read_u8(r);
    c.health=qa_net_read_i16(r);
    c.ammo=qa_net_read_u8(r); c.shells=qa_net_read_u8(r); c.nails=qa_net_read_u8(r);
    c.rockets=qa_net_read_u8(r); c.cells=qa_net_read_u8(r);
    uint8_t weapon=qa_net_read_u8(r);
    if (!standard && weapon>31) return qa_net_reader_fail(r,"Invalid mission-pack weapon bit");
    c.weapon=standard?weapon:UINT32_C(1)<<weapon;
    if (bits & SU_WEAPON2) c.weapon_model|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_ARMOR2) c.armor|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_AMMO2) c.ammo|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_SHELLS2) c.shells|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_NAILS2) c.nails|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_ROCKETS2) c.rockets|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_CELLS2) c.cells|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_FRAME2) c.weapon_frame|=(uint32_t)qa_net_read_u8(r)<<8;
    if (bits & SU_ALPHA) c.weapon_alpha=qa_net_read_u8(r);
    if (r->failed) return false;
    *out=c; return true;
}
static bool read_names(qa_net_reader *r, const char **names, size_t limit, size_t *out)
{
    size_t count=0;
    for (;;) {
        const char *name;
        if (!qa_q1_read_cstring(r,&name)) return false;
        if (!*name) { *out=count; return true; }
        if (count>=limit-1) return qa_net_reader_fail(r,"NetQuake precache overflow");
        names[count++]=name;
    }
}
static bool private_op(unsigned op)
{
    return op==38 || op==39 || (op>=45 && op<=51) || op==53 || op==54 || op==55 || op==57;
}
bool qa_nq_read(qa_nq_decoder *d, qa_net_reader *r, qa_nq_message *out)
{
    if (!d || !out || !r || (r->bit&7)) return qa_net_reader_fail(r,"Invalid NetQuake service reader");
    qa_nq_message m={0}; qa_net_protocol_id p=d->protocol;
    unsigned op=qa_net_read_u8(r);
    if (r->failed) return false;
    if (op&128) {
        m.op=QA_NQ_ENTITY;
        if (!read_entity(d,r,op&127,&m.data.entity)) return false;
        *out=m; return true;
    }
    if (private_op(op) && !d->options.private_rerelease) return qa_net_reader_fail(r,"Unidentified private rerelease service");
    m.op=(qa_nq_svc)op;
    switch (op) {
    case 1: case 2: case 27: case 28: case 30: case 33: case 40: case 54: case 55: break;
    case 3: m.data.indexed.index=qa_net_read_u8(r); m.data.indexed.value=qa_net_read_i32(r); break;
    case 4: {
        uint32_t version=qa_net_read_u32(r);
        if (!qa_q1_profile(version,version==999?p.flags:0,&p,NULL) || qa_q1_is_qw(p) ||
            (!d->present && p.kind != QA_NET_NQ15))
            return qa_net_reader_fail(r,"Unexpected NetQuake protocol version");
        m.data.value=version; break;
    }
    case 5: m.data.value=qa_net_read_u16(r); break;
    case 6: {
        qa_q1_sound *s=&m.data.sound;
        unsigned flags=qa_net_read_u8(r);
        if ((flags & ~(p.kind==QA_NET_NQ15?3u:27u))!=0) return qa_net_reader_fail(r,"Unknown NetQuake sound flags");
        s->volume=flags & 1?qa_net_read_u8(r):255;
        s->attenuation=flags & 2?(float)qa_net_read_u8(r)/64.0f:1;
        if (flags & 8) { s->entity=qa_net_read_u16(r); s->channel=qa_net_read_u8(r); }
        else { uint16_t channel=qa_net_read_u16(r); s->entity=channel>>3; s->channel=channel&7; }
        s->index=flags & 16?qa_net_read_u16(r):qa_net_read_u8(r);
        read_vec(r,p,s->origin); break;
    }
    case 7: m.data.seconds=read_float(r); break;
    case 8: case 9: case 26: case 31: case 34: case 37: case 38:
    case 49: case 50: case 52: case 53: qa_q1_read_cstring(r,&m.data.text); break;
    case 10: for (unsigned i=0;i<3;++i) m.data.angles[i]=qa_q1_read_angle(r,p); break;
    case 11: {
        qa_nq_serverinfo *s=&m.data.serverinfo;
        if (!qa_q1_read_protocol(r,false,&p)) return false;
        if (!d->present && p.kind != QA_NET_NQ15) return qa_net_reader_fail(r,"Original source changes its NetQuake dialect");
        s->protocol=p; s->max_clients=qa_net_read_u8(r); s->game_type=qa_net_read_u8(r);
        qa_q1_read_cstring(r,&s->level);
        size_t limit=p.kind==QA_NET_NQ15?256:8192;
        if (!read_names(r,d->models,limit,&s->model_count) || !read_names(r,d->sounds,limit,&s->sound_count)) return false;
        s->models=d->models; s->sounds=d->sounds; break;
    }
    case 12: case 13: case 47: case 48:
        m.data.indexed_text.index=qa_net_read_u8(r); qa_q1_read_cstring(r,&m.data.indexed_text.text); break;
    case 14: case 46: m.data.indexed.index=qa_net_read_u8(r); m.data.indexed.value=qa_net_read_i16(r); break;
    case 15: if (!read_clientdata(r,p,d->options.standard_quake,&m.data.clientdata)) return false; break;
    case 16: {
        uint16_t channel=qa_net_read_u16(r); m.data.stop_sound.entity=channel>>3; m.data.stop_sound.channel=(uint8_t)(channel&7); break;
    }
    case 17: m.data.indexed.index=qa_net_read_u8(r); m.data.indexed.value=qa_net_read_u8(r); break;
    case 18:
        read_vec(r,p,m.data.particle.origin);
        for (unsigned i=0;i<3;++i) m.data.particle.direction[i]=(float)qa_net_read_i8(r)/16.0f;
        m.data.particle.count=qa_net_read_u8(r); m.data.particle.color=qa_net_read_u8(r); break;
    case 19:
        m.data.damage.armor=qa_net_read_u8(r); m.data.damage.blood=qa_net_read_u8(r);
        read_vec(r,p,m.data.damage.origin); break;
    case 20: case 22: case 42: case 43: {
        bool baseline=op==22 || op==42;
        uint32_t number=baseline?qa_net_read_u16(r):0;
        if (!read_baseline(r,p,op>=42,number,&m.data.entity)) return false;
        m.op=baseline?QA_NQ_BASELINE:QA_NQ_STATIC; break;
    }
    case 23: if (!qa_q1_read_temp(r,p,&m.data.temporary)) return false; break;
    case 24: m.data.value=qa_net_read_u8(r)!=0; break;
    case 25: m.data.value=qa_net_read_u8(r); if (!m.data.value || m.data.value>4) return qa_net_reader_fail(r,"Invalid signon stage"); break;
    case 29: case 44:
        if (op==44 && p.kind==QA_NET_NQ15) return qa_net_reader_fail(r,"Wide static sound in NQ15");
        read_vec(r,p,m.data.sound.origin);
        m.data.sound.index=op==44?qa_net_read_u16(r):qa_net_read_u8(r);
        m.data.sound.volume=qa_net_read_u8(r); m.data.sound.attenuation=(float)qa_net_read_u8(r)/64.0f;
        m.op=QA_NQ_STATICSOUND; break;
    case 32: m.data.cd.track=qa_net_read_u8(r); m.data.cd.loop=qa_net_read_u8(r); break;
    case 39: case 45: m.data.value=qa_net_read_u8(r); break;
    case 41:
        m.data.fog.density=(float)qa_net_read_u8(r)/255.0f;
        for (unsigned i=0;i<3;++i) m.data.fog.color[i]=(float)qa_net_read_u8(r)/255.0f;
        m.data.fog.seconds=(float)qa_net_read_i16(r)/100.0f; break;
    case 51: m.data.value=qa_net_read_u32(r); break;
    case 56: {
        uint8_t flags=qa_net_read_u8(r);
        if (flags & ~16u) return qa_net_reader_fail(r,"Unknown local sound flags");
        m.data.value=flags & 16?qa_net_read_u16(r):qa_net_read_u8(r); break;
    }
    case 57:
        m.data.prompt.operation=qa_net_read_u8(r);
        if (m.data.prompt.operation<2) {
            qa_q1_read_cstring(r,&m.data.prompt.text); m.data.prompt.value=qa_net_read_u8(r);
        } else if (m.data.prompt.operation!=2) return qa_net_reader_fail(r,"Unknown private prompt");
        break;
    default: return qa_net_reader_fail(r,"Unknown NetQuake service");
    }
    if (r->failed) return false;
    if (op==4 && d->present) {
        for (uint32_t i=0;i<65536;++i) {
            if (d->present[i] && !entity_valid(p,&d->baselines[i]))
                return qa_net_reader_fail(r,"Protocol change cannot represent retained baseline");
        }
    }
    if (op==11 && d->present) memset(d->present,0,65536);
    if (m.op==QA_NQ_BASELINE && d->present) { d->baselines[m.data.entity.number]=m.data.entity; d->present[m.data.entity.number]=1; }
    if (op==7) d->time=m.data.seconds;
    d->protocol=p; *out=m; return true;
}

static bool write_i8(qa_net_writer *w, double value)
{
    double integer=trunc(value);
    if (!isfinite(value) || integer<INT8_MIN || integer>INT8_MAX) return qa_net_writer_fail(w,"Signed byte field exceeds NetQuake range");
    return qa_net_write_i8(w,(int8_t)integer);
}
static bool write_u8(qa_net_writer *w, double value)
{
    double integer=trunc(value);
    if (!isfinite(value) || integer<0 || integer>UINT8_MAX) return qa_net_writer_fail(w,"Byte field exceeds NetQuake range");
    return qa_net_write_u8(w,(uint8_t)integer);
}
static bool write_i16(qa_net_writer *w, double value)
{
    double integer=trunc(value);
    if (!isfinite(value) || integer<INT16_MIN || integer>INT16_MAX) return qa_net_writer_fail(w,"Short field exceeds NetQuake range");
    return qa_net_write_i16(w,(int16_t)integer);
}
static bool write_f32(qa_net_writer *w, float value)
{
    return isfinite(value) ? qa_net_write_f32(w,value) : qa_net_writer_fail(w,"Non-finite NetQuake float");
}
bool qa_nq_write_entity(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_entity *s,
                         const qa_q1_entity *base, float time)
{
    qa_q1_entity zero; qa_q1_entity_init(&zero);
    const qa_q1_entity *b=base?base:&zero;
    if (!nq_profile(p) || !entity_valid(p,s) || !entity_valid(p,b) || !isfinite(time))
        return qa_net_writer_fail(w,"Unrepresentable NetQuake entity");
    uint32_t bits=0;
    for (unsigned i=0;i<3;++i) {
        double miss=(double)s->origin[i]-b->origin[i];
        if (miss < -0.1 || miss > 0.1) bits|=(uint32_t)U_ORIGIN1<<i;
        if (s->angles[i]!=b->angles[i]) bits|=angle_bits[i];
    }
    if (s->step) bits|=U_STEP;
    if (s->model!=b->model) bits|=U_MODEL;
    if (s->frame!=b->frame) bits|=U_FRAME;
    if (s->colormap!=b->colormap) bits|=U_COLORMAP;
    if (s->skin!=b->skin) bits|=U_SKIN;
    if (s->effects!=b->effects) bits|=U_EFFECTS;
    double lerp=0;
    if (p.kind!=QA_NET_NQ15) {
        if (s->alpha!=b->alpha) bits|=U_ALPHA;
        if (s->scale!=b->scale) bits|=U_SCALE;
        if ((bits & U_FRAME) && s->frame>255) bits|=U_FRAME2;
        if ((bits & U_MODEL) && s->model>255) bits|=U_MODEL2;
        if (s->lerp_finish!=0) {
            double delta=(double)s->lerp_finish-time;
            lerp=delta>0?trunc(delta*255.0+0.5):trunc(delta*255.0-0.5);
            if (lerp<0 || lerp>255) return qa_net_writer_fail(w,"Entity lerp finish exceeds protocol interval");
            bits|=U_LERP;
        }
        if (bits>=65536) bits|=U_EXTEND1;
        if (bits>=16777216) bits|=U_EXTEND2;
    }
    if (s->number>=256) bits|=U_LONG;
    if (bits>=256) bits|=U_MORE;
    qa_net_write_u8(w,(uint8_t)(bits|128));
    if (bits & U_MORE) qa_net_write_u8(w,(uint8_t)(bits>>8));
    if (bits & U_EXTEND1) qa_net_write_u8(w,(uint8_t)(bits>>16));
    if (bits & U_EXTEND2) qa_net_write_u8(w,(uint8_t)(bits>>24));
    if (bits & U_LONG) qa_net_write_u16(w,(uint16_t)s->number); else qa_net_write_u8(w,(uint8_t)s->number);
    if (bits & U_MODEL) qa_net_write_u8(w,(uint8_t)s->model);
    if (bits & U_FRAME) qa_net_write_u8(w,(uint8_t)s->frame);
    if (bits & U_COLORMAP) qa_net_write_u8(w,(uint8_t)s->colormap);
    if (bits & U_SKIN) qa_net_write_u8(w,(uint8_t)s->skin);
    if (bits & U_EFFECTS) qa_net_write_u8(w,(uint8_t)s->effects);
    for (unsigned i=0;i<3;++i) {
        if (bits & ((uint32_t)U_ORIGIN1<<i)) qa_q1_write_coord(w,p,s->origin[i]);
        if (bits & angle_bits[i]) qa_q1_write_angle(w,p,s->angles[i]);
    }
    if (bits & U_ALPHA) qa_net_write_u8(w,s->alpha);
    if (bits & U_SCALE) qa_net_write_u8(w,s->scale);
    if (bits & U_FRAME2) qa_net_write_u8(w,(uint8_t)(s->frame>>8));
    if (bits & U_MODEL2) qa_net_write_u8(w,(uint8_t)(s->model>>8));
    if (bits & U_LERP) qa_net_write_u8(w,(uint8_t)lerp);
    return !w->failed;
}
bool qa_nq_write_clientdata(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_clientdata *c, bool standard)
{
    if (!c || !nq_profile(p)) return qa_net_writer_fail(w,"Invalid NetQuake clientdata dialect");
    uint32_t limit=p.kind==QA_NET_NQ15?255:65535;
    const uint32_t fields[]={c->weapon_frame,c->armor,c->weapon_model,c->ammo,c->shells,c->nails,c->rockets,c->cells};
    for (unsigned i=0;i<8;++i) if (fields[i]>limit) return qa_net_writer_fail(w,"Clientdata field exceeds selected dialect");
    if (c->health<INT16_MIN || c->health>INT16_MAX || (p.kind==QA_NET_NQ15 && c->weapon_alpha))
        return qa_net_writer_fail(w,"Unrepresentable NetQuake clientdata");
    unsigned weapon=c->weapon;
    if (standard) {
        if (weapon>255) return qa_net_writer_fail(w,"Weapon exceeds NetQuake byte");
    } else {
        if (weapon && (weapon & (weapon-1))) return qa_net_writer_fail(w,"Mission-pack weapon must contain one active bit");
        weapon=0;
        if (c->weapon) while (!(c->weapon & (UINT32_C(1)<<weapon))) ++weapon;
    }
    uint32_t bits=SU_ITEMS|SU_WEAPON;
    if (c->viewheight!=22) bits|=SU_VIEW;
    if (c->idealpitch!=0) bits|=SU_PITCH;
    if (c->onground) bits|=SU_GROUND;
    if (c->inwater) bits|=SU_WATER;
    for (unsigned i=0;i<3;++i) {
        if (c->punch[i]!=0) bits|=(uint32_t)SU_PUNCH1<<i;
        if (c->velocity[i]!=0) bits|=(uint32_t)SU_VELOCITY1<<i;
    }
    if (c->weapon_frame) bits|=SU_FRAME;
    if (c->armor) bits|=SU_ARMOR;
    if (c->weapon_model>255) bits|=SU_WEAPON2;
    if (c->armor>255) bits|=SU_ARMOR2;
    if (c->ammo>255) bits|=SU_AMMO2;
    if (c->shells>255) bits|=SU_SHELLS2;
    if (c->nails>255) bits|=SU_NAILS2;
    if (c->rockets>255) bits|=SU_ROCKETS2;
    if (c->cells>255) bits|=SU_CELLS2;
    if (c->weapon_frame>255) bits|=SU_FRAME2;
    if (c->weapon_alpha) bits|=SU_ALPHA;
    if (bits>=65536) bits|=SU_EXTEND1;
    if (bits>=16777216) bits|=SU_EXTEND2;
    qa_net_write_u8(w,15); qa_net_write_u16(w,(uint16_t)bits);
    if (bits & SU_EXTEND1) qa_net_write_u8(w,(uint8_t)(bits>>16));
    if (bits & SU_EXTEND2) qa_net_write_u8(w,(uint8_t)(bits>>24));
    if (bits & SU_VIEW) write_i8(w,c->viewheight);
    if (bits & SU_PITCH) write_i8(w,c->idealpitch);
    for (unsigned i=0;i<3;++i) {
        if (bits & ((uint32_t)SU_PUNCH1<<i)) write_i8(w,c->punch[i]);
        if (bits & ((uint32_t)SU_VELOCITY1<<i)) write_i8(w,(double)c->velocity[i]/16);
    }
    qa_net_write_u32(w,c->items);
    if (bits & SU_FRAME) qa_net_write_u8(w,(uint8_t)c->weapon_frame);
    if (bits & SU_ARMOR) qa_net_write_u8(w,(uint8_t)c->armor);
    qa_net_write_u8(w,(uint8_t)c->weapon_model);
    qa_net_write_i16(w,(int16_t)c->health);
    qa_net_write_u8(w,(uint8_t)c->ammo); qa_net_write_u8(w,(uint8_t)c->shells);
    qa_net_write_u8(w,(uint8_t)c->nails); qa_net_write_u8(w,(uint8_t)c->rockets);
    qa_net_write_u8(w,(uint8_t)c->cells); qa_net_write_u8(w,(uint8_t)weapon);
    if (bits & SU_WEAPON2) qa_net_write_u8(w,(uint8_t)(c->weapon_model>>8));
    if (bits & SU_ARMOR2) qa_net_write_u8(w,(uint8_t)(c->armor>>8));
    if (bits & SU_AMMO2) qa_net_write_u8(w,(uint8_t)(c->ammo>>8));
    if (bits & SU_SHELLS2) qa_net_write_u8(w,(uint8_t)(c->shells>>8));
    if (bits & SU_NAILS2) qa_net_write_u8(w,(uint8_t)(c->nails>>8));
    if (bits & SU_ROCKETS2) qa_net_write_u8(w,(uint8_t)(c->rockets>>8));
    if (bits & SU_CELLS2) qa_net_write_u8(w,(uint8_t)(c->cells>>8));
    if (bits & SU_FRAME2) qa_net_write_u8(w,(uint8_t)(c->weapon_frame>>8));
    if (bits & SU_ALPHA) qa_net_write_u8(w,c->weapon_alpha);
    return !w->failed;
}
static bool write_baseline(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_entity *s, bool is_static)
{
    if (!entity_valid(p,s) || s->effects || s->step || s->lerp_finish!=0 || (is_static && s->number))
        return qa_net_writer_fail(w,"Unrepresentable baseline or static entity");
    if (is_static && p.kind==QA_NET_FITZ666 && s->scale!=16)
        return qa_net_writer_fail(w,"Fitz static entity cannot carry donor RMQ scale");
    unsigned bits=0;
    if (s->model>255) bits|=1;
    if (s->frame>255) bits|=2;
    if (s->alpha) bits|=4;
    if (s->scale!=16) bits|=8;
    qa_net_write_u8(w,(uint8_t)(is_static?(bits?43:20):(bits?42:22)));
    if (!is_static) qa_net_write_u16(w,(uint16_t)s->number);
    if (bits) qa_net_write_u8(w,(uint8_t)bits);
    if (bits & 1) qa_net_write_u16(w,(uint16_t)s->model); else qa_net_write_u8(w,(uint8_t)s->model);
    if (bits & 2) qa_net_write_u16(w,(uint16_t)s->frame); else qa_net_write_u8(w,(uint8_t)s->frame);
    qa_net_write_u8(w,(uint8_t)s->colormap); qa_net_write_u8(w,(uint8_t)s->skin);
    for (unsigned i=0;i<3;++i) { qa_q1_write_coord(w,p,s->origin[i]); qa_q1_write_angle(w,p,s->angles[i]); }
    if (bits & 4) qa_net_write_u8(w,s->alpha);
    if (bits & 8) qa_net_write_u8(w,s->scale);
    return !w->failed;
}
static bool write_sound(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_sound *s, bool is_static)
{
    if (s->entity>65535 || s->channel>255 || s->index>(p.kind==QA_NET_NQ15?255u:65535u) ||
        !isfinite(s->attenuation) || s->attenuation<0 || s->attenuation>=4)
        return qa_net_writer_fail(w,"Unrepresentable NetQuake sound");
    if (is_static) {
        if (s->entity || s->channel) return qa_net_writer_fail(w,"Static sound cannot carry an entity channel");
        qa_net_write_u8(w,s->index>255?44:29); write_vec(w,p,s->origin);
        if (s->index>255) qa_net_write_u16(w,(uint16_t)s->index); else qa_net_write_u8(w,(uint8_t)s->index);
        qa_net_write_u8(w,s->volume); write_u8(w,(double)s->attenuation*64); return !w->failed;
    }
    if (p.kind==QA_NET_NQ15 && (s->entity>=8192 || s->channel>=8))
        return qa_net_writer_fail(w,"NQ15 sound entity/channel exceeds packed field");
    unsigned bits=0;
    if (s->volume!=255) bits|=1;
    if (s->attenuation!=1) bits|=2;
    if (s->entity>=8192 || s->channel>=8) bits|=8;
    if (s->index>=256) bits|=16;
    qa_net_write_u8(w,6); qa_net_write_u8(w,(uint8_t)bits);
    if (bits & 1) qa_net_write_u8(w,s->volume);
    if (bits & 2) write_u8(w,(double)s->attenuation*64);
    if (bits & 8) { qa_net_write_u16(w,(uint16_t)s->entity); qa_net_write_u8(w,(uint8_t)s->channel); }
    else qa_net_write_u16(w,(uint16_t)((s->entity<<3)|s->channel));
    if (bits & 16) qa_net_write_u16(w,(uint16_t)s->index); else qa_net_write_u8(w,(uint8_t)s->index);
    write_vec(w,p,s->origin); return !w->failed;
}
static bool write_names(qa_net_writer *w, const char *const *names, size_t count, size_t limit)
{
    if (count>=limit || (count && !names)) return qa_net_writer_fail(w,"Precache exceeds selected NetQuake dialect");
    for (size_t i=0;i<count;++i) {
        if (!names[i] || !*names[i]) return qa_net_writer_fail(w,"Empty precache entry");
        if (!qa_net_write_string(w,names[i])) return false;
    }
    return qa_net_write_u8(w,0);
}
bool qa_nq_write_damage(qa_net_writer *w, uint8_t armor, uint8_t blood, const double origin[3])
{
    if (!origin) return qa_net_writer_fail(w, "Missing original NetQuake damage center");
    uint16_t fixed[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(origin[axis]) || !isfinite(origin[axis] * 8))
            return qa_net_writer_fail(w, "Nonfinite original NetQuake damage center");
        fixed[axis] = (uint16_t)(uint32_t)qa_source_float_to_i32((float)origin[axis] * 8.0f);
    }
    return qa_net_write_u8(w, QA_NQ_DAMAGE) && qa_net_write_u8(w, armor) && qa_net_write_u8(w, blood) &&
        qa_net_write_u16(w, fixed[0]) && qa_net_write_u16(w, fixed[1]) && qa_net_write_u16(w, fixed[2]);
}

bool qa_nq_write(qa_net_writer *w, qa_net_protocol_id p, qa_nq_options options,
                  const qa_nq_message *m, const qa_q1_entity *baseline, float time)
{
    if (!m || !nq_profile(p)) return qa_net_writer_fail(w,"Invalid NetQuake service dialect");
    if (private_op(m->op) && !options.private_rerelease) return qa_net_writer_fail(w,"Private rerelease service requires explicit layout");
    switch (m->op) {
    case QA_NQ_ENTITY: return qa_nq_write_entity(w,p,&m->data.entity,baseline,time);
    case QA_NQ_CLIENTDATA: return qa_nq_write_clientdata(w,p,&m->data.clientdata,options.standard_quake);
    case QA_NQ_BASELINE: return write_baseline(w,p,&m->data.entity,false);
    case QA_NQ_STATIC: return write_baseline(w,p,&m->data.entity,true);
    case QA_NQ_SOUND: return write_sound(w,p,&m->data.sound,false);
    case QA_NQ_STATICSOUND: return write_sound(w,p,&m->data.sound,true);
    default: break;
    }
    qa_net_write_u8(w,(uint8_t)m->op);
    switch (m->op) {
    case QA_NQ_NOP: case QA_NQ_DISCONNECT: case QA_NQ_KILLEDMONSTER: case QA_NQ_FOUNDSECRET:
    case QA_NQ_INTERMISSION: case QA_NQ_SELLSCREEN: case QA_NQ_BONUSFLASH: case QA_NQ_LEVELCOMPLETED: case QA_NQ_BACKTOLOBBY: break;
    case QA_NQ_STAT: qa_net_write_u8(w,m->data.indexed.index); qa_net_write_i32(w,m->data.indexed.value); break;
    case QA_NQ_VERSION: {
        qa_net_protocol_id next;
        if (!qa_q1_profile(m->data.value,m->data.value==999?p.flags:0,&next,NULL) || qa_q1_is_qw(next))
            return qa_net_writer_fail(w,"Invalid NetQuake service version");
        qa_net_write_u32(w,m->data.value); break;
    }
    case QA_NQ_SETVIEW:
        if (m->data.value>65535) return qa_net_writer_fail(w,"View entity exceeds NetQuake short");
        qa_net_write_u16(w,(uint16_t)m->data.value); break;
    case QA_NQ_TIME: write_f32(w,m->data.seconds); break;
    case QA_NQ_PRINT: case QA_NQ_STUFFTEXT: case QA_NQ_CENTERPRINT: case QA_NQ_FINALE: case QA_NQ_CUTSCENE:
    case QA_NQ_SKYBOX: case QA_NQ_BOTCHAT: case QA_NQ_RAWPRINT: case QA_NQ_SERVERVARS: case QA_NQ_ACHIEVEMENT: case QA_NQ_CHAT:
        qa_net_write_string(w,m->data.text); break;
    case QA_NQ_SETANGLE: for (unsigned i=0;i<3;++i) qa_q1_write_angle(w,p,m->data.angles[i]); break;
    case QA_NQ_SERVERINFO: {
        const qa_nq_serverinfo *s=&m->data.serverinfo;
        if (!nq_profile(s->protocol)) return qa_net_writer_fail(w,"Server info is not a NetQuake dialect");
        qa_q1_write_protocol(w,s->protocol); qa_net_write_u8(w,s->max_clients); qa_net_write_u8(w,s->game_type);
        qa_net_write_string(w,s->level);
        size_t limit=s->protocol.kind==QA_NET_NQ15?256:8192;
        write_names(w,s->models,s->model_count,limit); write_names(w,s->sounds,s->sound_count,limit); break;
    }
    case QA_NQ_LIGHTSTYLE: case QA_NQ_NAME: case QA_NQ_SOCIAL: case QA_NQ_PLAYERINFO:
        qa_net_write_u8(w,m->data.indexed_text.index); qa_net_write_string(w,m->data.indexed_text.text); break;
    case QA_NQ_FRAGS: case QA_NQ_PING:
        qa_net_write_u8(w,m->data.indexed.index); write_i16(w,m->data.indexed.value); break;
    case QA_NQ_STOPSOUND:
        if (m->data.stop_sound.entity>=8192 || m->data.stop_sound.channel>=8)
            return qa_net_writer_fail(w,"Stop sound exceeds NetQuake packed field");
        qa_net_write_u16(w,(uint16_t)(((uint32_t)m->data.stop_sound.entity<<3)|m->data.stop_sound.channel)); break;
    case QA_NQ_COLORS: qa_net_write_u8(w,m->data.indexed.index); write_u8(w,m->data.indexed.value); break;
    case QA_NQ_PARTICLE:
        write_vec(w,p,m->data.particle.origin);
        for (unsigned i=0;i<3;++i) write_i8(w,(double)m->data.particle.direction[i]*16);
        qa_net_write_u8(w,m->data.particle.count); qa_net_write_u8(w,m->data.particle.color); break;
    case QA_NQ_DAMAGE:
        qa_net_write_u8(w,m->data.damage.armor); qa_net_write_u8(w,m->data.damage.blood); write_vec(w,p,m->data.damage.origin); break;
    case QA_NQ_TEMPENTITY: qa_q1_write_temp(w,p,&m->data.temporary); break;
    case QA_NQ_PAUSE:
        if (m->data.value>1) return qa_net_writer_fail(w,"Invalid pause boolean");
        qa_net_write_u8(w,(uint8_t)m->data.value); break;
    case QA_NQ_SIGNON:
        if (!m->data.value || m->data.value>4) return qa_net_writer_fail(w,"Invalid signon stage");
        qa_net_write_u8(w,(uint8_t)m->data.value); break;
    case QA_NQ_CDTRACK: qa_net_write_u8(w,m->data.cd.track); qa_net_write_u8(w,m->data.cd.loop); break;
    case QA_NQ_FOG:
        write_u8(w,(double)m->data.fog.density*255);
        for (unsigned i=0;i<3;++i) write_u8(w,(double)m->data.fog.color[i]*255);
        write_i16(w,(double)m->data.fog.seconds*100); break;
    case QA_NQ_SPAWNEDMONSTER: case QA_NQ_SETVIEWS: write_u8(w,m->data.value); break;
    case QA_NQ_SEQUENCE: qa_net_write_u32(w,m->data.value); break;
    case QA_NQ_LOCALSOUND:
        if (m->data.value>65535) return qa_net_writer_fail(w,"Local sound index exceeds short");
        qa_net_write_u8(w,m->data.value>255?16:0);
        if (m->data.value>255) qa_net_write_u16(w,(uint16_t)m->data.value); else qa_net_write_u8(w,(uint8_t)m->data.value);
        break;
    case QA_NQ_PROMPT:
        if (m->data.prompt.operation>2) return qa_net_writer_fail(w,"Unknown private prompt");
        qa_net_write_u8(w,m->data.prompt.operation);
        if (m->data.prompt.operation<2) { qa_net_write_string(w,m->data.prompt.text); qa_net_write_u8(w,m->data.prompt.value); }
        break;
    default: return qa_net_writer_fail(w,"Unknown NetQuake service");
    }
    return !w->failed;
}
bool qa_nq_transcode_original(qa_net_reader *reader, qa_net_writer *writer,
    qa_net_protocol_id destination, qa_nq_options options, const qa_q1_entity *baselines,
    size_t baseline_count, float source_time, qa_q1_emit_fn emit, void *context)
{
    if (!reader || !writer || !emit || !nq_profile(destination) || (baseline_count && !baselines) || !isfinite(source_time))
        return qa_net_writer_fail(writer,"Invalid original NetQuake wire conversion");
    const char *models[256], *sounds[256];
    qa_nq_decoder source = {.protocol = {.kind = QA_NET_NQ15}, .options = options,
        .time = source_time, .models = models, .sounds = sounds,
        .source_baselines = baselines, .source_baseline_count = baseline_count};
    while (qa_net_reader_remaining(reader)) {
        qa_nq_message message;
        if (!qa_nq_read(&source, reader, &message)) return false;
        if (message.op == QA_NQ_SERVERINFO) message.data.serverinfo.protocol = destination;
        else if (message.op == QA_NQ_VERSION) message.data.value = qa_q1_version(destination);
        const qa_q1_entity *saved = message.op == QA_NQ_ENTITY ?
            qa_nq_decoder_baseline(&source, message.data.entity.number) : NULL;
        qa_q1_entity empty;
        if (message.op == QA_NQ_ENTITY && !saved) {
            qa_q1_entity_init(&empty); empty.number = message.data.entity.number; saved = &empty;
        }
        if (!qa_nq_write(writer, destination, options, &message, saved, source.time) ||
            !emit(context, (qa_bytes){writer->data, qa_net_writer_size(writer)}, writer->error)) return false;
        qa_net_writer_init(writer, writer->data, writer->capacity, writer->error);
    }
    return qa_net_reader_finish(reader);
}

bool qa_nq_move_send(uint32_t *count, bool demo, qa_net_writer *w,
                      qa_net_protocol_id p, const qa_q1_command *c, bool *present)
{
    if (!count || !present) return qa_net_writer_fail(w,"Missing NetQuake move sender state");
    *present=false;
    if (demo) return true;
    if (*count<2) { ++*count; return true; }
    if (!qa_net_write_u8(w,3) || !qa_q1_write_move(w,p,c)) return false;
    if (*count<UINT32_MAX) ++*count;
    *present=true; return true;
}

bool qa_nq_decoder_save(qa_net_writer *w, const qa_nq_decoder *d)
{
    if (!d || !nq_profile(d->protocol) || !isfinite(d->time)) return qa_net_writer_fail(w,"Invalid NetQuake decoder checkpoint");
    qa_net_write_u32(w,UINT32_C(0x4443514e)); /* NQCD */
    qa_q1_write_protocol(w,d->protocol);
    qa_net_write_u8(w,(uint8_t)((d->options.standard_quake?1:0)|(d->options.private_rerelease?2:0)));
    qa_net_write_f32(w,d->time);
    uint32_t count=0;
    for (uint32_t i=0;i<65536;++i) if (d->present[i]) ++count;
    qa_net_write_u32(w,count);
    for (uint32_t i=0;i<65536;++i) {
        if (!d->present[i]) continue;
        const qa_q1_entity *s=&d->baselines[i];
        qa_net_write_u16(w,(uint16_t)i);
        qa_net_write_u16(w,(uint16_t)s->model); qa_net_write_u16(w,(uint16_t)s->frame);
        qa_net_write_u8(w,(uint8_t)s->colormap); qa_net_write_u8(w,(uint8_t)s->skin);
        for (unsigned j=0;j<3;++j) qa_net_write_f32(w,s->origin[j]);
        for (unsigned j=0;j<3;++j) qa_net_write_f32(w,s->angles[j]);
        qa_net_write_u8(w,s->alpha); qa_net_write_u8(w,s->scale);
    }
    return !w->failed;
}
bool qa_nq_decoder_restore(qa_net_reader *r, qa_nq_decoder *d)
{
    if (!d) return qa_net_reader_fail(r,"Missing NetQuake decoder for checkpoint");
    uint32_t magic=qa_net_read_u32(r);
    if (magic!=UINT32_C(0x4443514e)) return qa_net_reader_fail(r,"Unknown NetQuake decoder checkpoint");
    qa_net_protocol_id profile;
    if (!qa_q1_read_protocol(r,false,&profile)) return false;
    uint8_t options=qa_net_read_u8(r);
    float time=read_float(r);
    uint32_t count=qa_net_read_u32(r);
    if (r->failed) return false;
    if ((options & ~3u) || count>65536 || count>qa_net_reader_remaining(r)/34)
        return qa_net_reader_fail(r,"Invalid NetQuake checkpoint fields");
    qa_nq_decoder *next=NULL;
    if (!qa_nq_decoder_create(profile,(qa_nq_options){(options&1)!=0,(options&2)!=0},&next,r->error)) {
        r->failed=true; return false;
    }
    next->time=time;
    for (uint32_t i=0;i<count;++i) {
        qa_q1_entity s; qa_q1_entity_init(&s);
        s.number=qa_net_read_u16(r); s.model=qa_net_read_u16(r); s.frame=qa_net_read_u16(r);
        s.colormap=qa_net_read_u8(r); s.skin=qa_net_read_u8(r);
        for (unsigned j=0;j<3;++j) s.origin[j]=read_float(r);
        for (unsigned j=0;j<3;++j) s.angles[j]=read_float(r);
        s.alpha=qa_net_read_u8(r); s.scale=qa_net_read_u8(r);
        if (r->failed || next->present[s.number] || !entity_valid(profile,&s)) {
            qa_nq_decoder_destroy(next);
            return qa_net_reader_fail(r,"Invalid or duplicate checkpoint baseline");
        }
        next->baselines[s.number]=s; next->present[s.number]=1;
    }
    if (!qa_net_reader_finish(r)) { qa_nq_decoder_destroy(next); return false; }
    qa_nq_decoder old=*d; *d=*next; *next=old;
    qa_nq_decoder_destroy(next); return true;
}

static bool decoder_admitted(const qa_nq_decoder *decoder, qa_net_protocol_id protocol, qa_nq_options options)
{
    return decoder && nq_profile(protocol) && decoder->protocol.kind==protocol.kind &&
        decoder->protocol.revision==protocol.revision && decoder->protocol.flags==protocol.flags &&
        decoder->options.standard_quake==options.standard_quake && decoder->options.private_rerelease==options.private_rerelease;
}
bool qa_nq_decoder_checkpoint(const qa_nq_decoder *decoder, qa_net_protocol_id protocol,
    qa_nq_options options, qa_buffer *out, qa_error *error)
{
    if (!out || !decoder_admitted(decoder,protocol,options)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"NetQuake decoder requires admitted source dialect and options"); return false;
    }
    size_t count=0;
    for (size_t i=0;i<65536;++i) if (decoder->present[i]) ++count;
    size_t capacity=32+count*34;
    uint8_t *data=malloc(capacity);
    if (!data) { qa_error_set(error,QA_ERROR_MEMORY,0,"Encoding NetQuake decoder continuation"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer,data,capacity,error);
    if (!qa_nq_decoder_save(&writer,decoder)) { free(data); return false; }
    *out=(qa_buffer){data,qa_net_writer_size(&writer)}; return true;
}
bool qa_nq_decoder_restore_checkpoint(qa_bytes bytes, qa_net_protocol_id protocol, qa_nq_options options,
    qa_nq_decoder **out, qa_error *error)
{
    if (!out || *out || !nq_profile(protocol) || (bytes.size && !bytes.data)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"NetQuake decoder restore requires admitted source and empty output"); return false;
    }
    qa_net_reader reader; qa_net_reader_init(&reader,bytes,error);
    qa_net_protocol_id saved;
    if (qa_net_read_u32(&reader)!=UINT32_C(0x4443514e) ||
        !qa_q1_read_protocol(&reader,false,&saved)) return qa_net_reader_fail(&reader,"Invalid NetQuake decoder continuation schema");
    uint8_t flags=qa_net_read_u8(&reader);
    if (reader.failed || saved.kind!=protocol.kind || saved.revision!=protocol.revision || saved.flags!=protocol.flags ||
        flags!=(uint8_t)((options.standard_quake?1:0)|(options.private_rerelease?2:0)))
        return qa_net_reader_fail(&reader,"NetQuake decoder source admission differs");
    qa_nq_decoder *candidate=NULL;
    if (!qa_nq_decoder_create(protocol,options,&candidate,error)) return false;
    qa_net_reader_init(&reader,bytes,error);
    if (!qa_nq_decoder_restore(&reader,candidate) || !qa_net_reader_finish(&reader) ||
        !decoder_admitted(candidate,protocol,options)) { qa_nq_decoder_destroy(candidate); return false; }
    *out=candidate; return true;
}
