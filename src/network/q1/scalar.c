#include "qa/network_q1.h"
#include "qa/math.h"
#include "qa/text.h"
#include <math.h>
#include <string.h>

bool qa_q1_is_qw(qa_net_protocol_id p) { return p.kind == QA_NET_QW28 || p.kind == QA_NET_QW29; }
uint32_t qa_q1_version(qa_net_protocol_id p)
{
    switch (p.kind) {
    case QA_NET_NQ15: return 15;
    case QA_NET_FITZ666: return 666;
    case QA_NET_RMQ999: return 999;
    case QA_NET_QW28: return 28;
    case QA_NET_QW29: return 29;
    default: return 0;
    }
}
bool qa_q1_profile_valid(qa_net_protocol_id p, qa_error *e)
{
    if (!qa_q1_version(p) || p.revision != 0 ||
        (p.flags & ~((uint32_t)QA_Q1_SUPPORTED_FLAGS)) ||
        (p.flags && p.kind != QA_NET_RMQ999 && p.kind != QA_NET_QW29)) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Unsupported Quake 1 protocol or flags");
        return false;
    }
    return true;
}
bool qa_q1_profile(uint32_t v, uint32_t flags, qa_net_protocol_id *out, qa_error *e)
{
    qa_net_protocol_id p = {0};
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing protocol output"); return false; }
    switch (v) {
    case 15: p.kind = QA_NET_NQ15; break;
    case 666: p.kind = QA_NET_FITZ666; break;
    case 999: p.kind = QA_NET_RMQ999; break;
    case 28: p.kind = QA_NET_QW28; break;
    case 29: p.kind = QA_NET_QW29; break;
    default: qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Unknown Quake 1 protocol %u", v); return false;
    }
    p.flags = flags;
    if (!qa_q1_profile_valid(p, e)) return false;
    *out = p; return true;
}
void qa_q1_entity_init(qa_q1_entity *s) { memset(s, 0, sizeof(*s)); s->scale = QA_Q1_SCALE_DEFAULT; }
static bool read_profile(qa_net_reader *r, qa_net_protocol_id p)
{
    if (!r || r->failed) return false;
    if (!qa_q1_profile_valid(p, NULL)) return qa_net_reader_fail(r, "Invalid Quake 1 dialect");
    return true;
}
static bool write_profile(qa_net_writer *w, qa_net_protocol_id p)
{
    if (!w || w->failed) return false;
    if (!qa_q1_profile_valid(p, NULL)) return qa_net_writer_fail(w, "Invalid Quake 1 dialect");
    return true;
}
static float finite_read(qa_net_reader *r, float x)
{
    if (!isfinite(x)) { qa_net_reader_fail(r, "Non-finite Quake 1 scalar"); return 0; }
    return x;
}
float qa_q1_read_coord(qa_net_reader *r, qa_net_protocol_id p)
{
    if (!read_profile(r,p)) return 0;
    if (p.flags & QA_Q1_FLOATCOORD) return finite_read(r,qa_net_read_f32(r));
    if (p.flags & QA_Q1_INT32COORD) return (float)((double)qa_net_read_i32(r) / 16.0);
    if (p.flags & QA_Q1_COORD24) {
        float whole = (float)qa_net_read_i16(r);
        return whole + (float)qa_net_read_u8(r)/255.0f;
    }
    return (float)qa_net_read_i16(r)/8.0f;
}
float qa_q1_read_angle(qa_net_reader *r, qa_net_protocol_id p)
{
    if (!read_profile(r,p)) return 0;
    if (p.flags & QA_Q1_FLOATANGLE) return finite_read(r,qa_net_read_f32(r));
    if (p.flags & QA_Q1_SHORTANGLE) return (float)qa_net_read_i16(r)*(360.0f/65536.0f);
    return (float)qa_net_read_i8(r)*(360.0f/256.0f);
}
static double nearest(double x) { return x > 0 ? trunc(x+0.5) : trunc(x-0.5); }
static bool coord_integer(qa_net_writer *w, double x, bool wide)
{
    if (x < (wide ? -2147483648.0 : -32768.0) || x > (wide ? 2147483647.0 : 32767.0))
        return qa_net_writer_fail(w,"Coordinate exceeds dialect range");
    return wide ? qa_net_write_i32(w,(int32_t)x) : qa_net_write_i16(w,(int16_t)x);
}
bool qa_q1_write_coord(qa_net_writer *w, qa_net_protocol_id p, float x)
{
    if (!write_profile(w,p)) return false;
    if (!isfinite(x)) return qa_net_writer_fail(w,"Non-finite Quake 1 coordinate");
    if (p.flags & QA_Q1_FLOATCOORD) return qa_net_write_f32(w,x);
    if (p.flags & QA_Q1_INT32COORD) return coord_integer(w,nearest((double)(x*16.0f)),true);
    if (p.flags & QA_Q1_COORD24) {
        if (!coord_integer(w,trunc(x),false)) return false;
        int32_t remainder = qa_source_float_to_i32(x*255.0f)%255;
        return qa_net_write_u8(w,(uint8_t)(uint32_t)remainder);
    }
    double fixed = (double)(x*8.0f);
    return coord_integer(w,p.kind == QA_NET_NQ15 || p.kind == QA_NET_QW28 ? trunc(fixed) : nearest(fixed),false);
}
static bool rounded_angle(qa_net_writer *w, qa_net_protocol_id p, float value, unsigned bits)
{
    if (!isfinite(value)) return qa_net_writer_fail(w,"Non-finite Quake 1 angle");
    double degrees = p.kind == QA_NET_QW29 ? fmod((double)value,360.0) : (double)value;
    double scaled = degrees * (bits == 8 ? 256.0 : 65536.0)/360.0;
    double rounded = nearest(scaled);
    int32_t integer = rounded >= INT32_MIN && rounded < 2147483648.0 ? (int32_t)rounded : INT32_MIN;
    return bits == 8 ? qa_net_write_u8(w,(uint8_t)(uint32_t)integer) :
        qa_net_write_u16(w,(uint16_t)(uint32_t)integer);
}
bool qa_q1_write_angle(qa_net_writer *w, qa_net_protocol_id p, float x)
{
    if (!write_profile(w,p)) return false;
    if (!isfinite(x)) return qa_net_writer_fail(w,"Non-finite Quake 1 angle");
    if (p.flags & QA_Q1_FLOATANGLE) return qa_net_write_f32(w,x);
    if (p.flags & QA_Q1_SHORTANGLE) return rounded_angle(w,p,x,16);
    if (p.kind == QA_NET_NQ15) {
        uint32_t bits = (uint32_t)qa_source_float_to_i32(x)*UINT32_C(256);
        int32_t product;
        memcpy(&product,&bits,sizeof(product));
        return qa_net_write_u8(w,(uint8_t)(uint32_t)(product/360));
    }
    if (p.kind == QA_NET_QW28)
        return qa_net_write_u8(w,(uint8_t)(uint32_t)qa_source_float_to_i32(x*256.0f/360.0f));
    return rounded_angle(w,p,x,8);
}
bool qa_q1_read_protocol(qa_net_reader *r, bool qw, qa_net_protocol_id *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing Quake protocol output");
    uint32_t version = qa_net_read_u32(r), flags = 0;
    if (version == 999 || version == 29) flags = qa_net_read_u32(r);
    qa_net_protocol_id p;
    if (r->failed) return false;
    if (!qa_q1_profile(version,flags,&p,NULL) || qa_q1_is_qw(p) != qw)
        return qa_net_reader_fail(r,"Unexpected Quake protocol family");
    *out = p; return true;
}
bool qa_q1_write_protocol(qa_net_writer *w, qa_net_protocol_id p)
{
    if (!write_profile(w,p) || !qa_net_write_u32(w,qa_q1_version(p))) return false;
    return p.kind == QA_NET_RMQ999 || p.kind == QA_NET_QW29 ? qa_net_write_u32(w,p.flags) : true;
}
bool qa_q1_read_temp(qa_net_reader *r, qa_net_protocol_id p, qa_q1_temp *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing temporary entity output");
    qa_q1_temp t = {0};
    bool qw = qa_q1_is_qw(p);
    t.type = qa_net_read_u8(r); t.count = 1;
    if (t.type > 13) return qa_net_reader_fail(r,"Unknown temporary entity");
    bool beam = t.type == 5 || t.type == 6 || t.type == 9 || (!qw && t.type == 13);
    if (beam) { t.kind = QA_Q1_TEMP_BEAM; t.entity = qa_net_read_u16(r); }
    else if (qw && (t.type == 2 || t.type == 12)) t.count = qa_net_read_u8(r);
    for (unsigned i=0;i<3;++i) t.origin[i] = qa_q1_read_coord(r,p);
    if (beam) for (unsigned i=0;i<3;++i) t.end[i] = qa_q1_read_coord(r,p);
    if (!qw && t.type == 12) {
        t.kind = QA_Q1_TEMP_COLORS;
        t.color_start = qa_net_read_u8(r); t.color_length = qa_net_read_u8(r);
    }
    if (r->failed) return false;
    *out = t; return true;
}
bool qa_q1_write_temp(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_temp *t)
{
    if (!t || !write_profile(w,p)) return qa_net_writer_fail(w,"Missing temporary entity");
    bool qw = qa_q1_is_qw(p);
    bool beam = t->type == 5 || t->type == 6 || t->type == 9 || (!qw && t->type == 13);
    qa_q1_temp_kind kind = beam ? QA_Q1_TEMP_BEAM : !qw && t->type == 12 ? QA_Q1_TEMP_COLORS : QA_Q1_TEMP_POINT;
    if (t->type > 13 || t->kind != kind || (kind == QA_Q1_TEMP_POINT && !(qw && (t->type==2 || t->type==12)) && t->count != 1))
        return qa_net_writer_fail(w,"Unrepresentable temporary entity");
    qa_net_write_u8(w,t->type);
    if (beam) qa_net_write_u16(w,t->entity);
    else if (qw && (t->type==2 || t->type==12)) qa_net_write_u8(w,t->count);
    for (unsigned i=0;i<3;++i) qa_q1_write_coord(w,p,t->origin[i]);
    if (beam) for (unsigned i=0;i<3;++i) qa_q1_write_coord(w,p,t->end[i]);
    if (kind == QA_Q1_TEMP_COLORS) { qa_net_write_u8(w,t->color_start); qa_net_write_u8(w,t->color_length); }
    return !w->failed;
}
bool qa_q1_read_move(qa_net_reader *r, qa_net_protocol_id p, qa_q1_command *out)
{
    if (!out) return qa_net_reader_fail(r,"Missing NetQuake command output");
    qa_q1_command c = {0};
    if (!read_profile(r,p) || qa_q1_is_qw(p)) return qa_net_reader_fail(r,"Expected NetQuake move");
    c.time = finite_read(r,qa_net_read_f32(r));
    for (unsigned i=0;i<3;++i) c.angles[i] = p.kind == QA_NET_NQ15 ? qa_q1_read_angle(r,p) :
        p.flags & QA_Q1_FLOATANGLE ? finite_read(r,qa_net_read_f32(r)) : (float)qa_net_read_i16(r)*(360.0f/65536.0f);
    c.forward=qa_net_read_i16(r); c.side=qa_net_read_i16(r); c.up=qa_net_read_i16(r);
    c.buttons=qa_net_read_u8(r); c.impulse=qa_net_read_u8(r);
    if (r->failed) return false;
    *out=c; return true;
}
bool qa_q1_write_move(qa_net_writer *w, qa_net_protocol_id p, const qa_q1_command *c)
{
    if (!c || !write_profile(w,p) || qa_q1_is_qw(p)) return qa_net_writer_fail(w,"Expected NetQuake move");
    if (!isfinite(c->time)) return qa_net_writer_fail(w,"Non-finite move time");
    qa_net_write_f32(w,c->time);
    for (unsigned i=0;i<3;++i) {
        if (p.kind == QA_NET_NQ15) qa_q1_write_angle(w,p,c->angles[i]);
        else if (p.flags & QA_Q1_FLOATANGLE) {
            if (!isfinite(c->angles[i])) return qa_net_writer_fail(w,"Non-finite move angle");
            qa_net_write_f32(w,c->angles[i]);
        } else rounded_angle(w,p,c->angles[i],16);
    }
    qa_net_write_i16(w,c->forward); qa_net_write_i16(w,c->side); qa_net_write_i16(w,c->up);
    qa_net_write_u8(w,c->buttons); qa_net_write_u8(w,c->impulse);
    return !w->failed;
}
bool qa_qw_read_delta_command(qa_net_reader *r, const qa_qw_command *from, qa_qw_command *out)
{
    if (!from || !out) return qa_net_reader_fail(r,"Missing QuakeWorld command state");
    qa_qw_command c = *from;
    uint8_t bits = qa_net_read_u8(r);
    const unsigned angle_bits[3] = {1,128,2};
    for (unsigned i=0;i<3;++i) if (bits & angle_bits[i]) c.angles[i]=(float)qa_net_read_i16(r)*(360.0f/65536.0f);
    if (bits & 4) c.forward=qa_net_read_i16(r);
    if (bits & 8) c.side=qa_net_read_i16(r);
    if (bits & 16) c.up=qa_net_read_i16(r);
    if (bits & 32) c.buttons=qa_net_read_u8(r);
    if (bits & 64) c.impulse=qa_net_read_u8(r);
    c.msec=qa_net_read_u8(r);
    if (r->failed) return false;
    *out=c; return true;
}
bool qa_qw_write_delta_command(qa_net_writer *w, const qa_qw_command *from, const qa_qw_command *c)
{
    if (!from || !c) return qa_net_writer_fail(w,"Missing QuakeWorld command state");
    unsigned bits=0;
    const unsigned angle_bits[3]={1,128,2};
    for (unsigned i=0;i<3;++i) {
        if (!isfinite(c->angles[i]) || !isfinite(from->angles[i])) return qa_net_writer_fail(w,"Non-finite QW command angle");
        if (c->angles[i] != from->angles[i]) bits |= angle_bits[i];
    }
    if (c->forward != from->forward) bits |= 4;
    if (c->side != from->side) bits |= 8;
    if (c->up != from->up) bits |= 16;
    if (c->buttons != from->buttons) bits |= 32;
    if (c->impulse != from->impulse) bits |= 64;
    qa_net_write_u8(w,(uint8_t)bits);
    for (unsigned i=0;i<3;++i) if (bits & angle_bits[i]) qa_net_write_u16(w,qa_angle_to_word(c->angles[i]));
    if (bits & 4) qa_net_write_i16(w,c->forward);
    if (bits & 8) qa_net_write_i16(w,c->side);
    if (bits & 16) qa_net_write_i16(w,c->up);
    if (bits & 32) qa_net_write_u8(w,c->buttons);
    if (bits & 64) qa_net_write_u8(w,c->impulse);
    qa_net_write_u8(w,c->msec);
    return !w->failed;
}
