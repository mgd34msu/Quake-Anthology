#ifndef QA_Q2_KEX_GAME_INTERNAL_H
#define QA_Q2_KEX_GAME_INTERNAL_H
#include "internal.h"

#define Q2_U_ORIGIN1 (UINT64_C(1) << 0)
#define Q2_U_ORIGIN2 (UINT64_C(1) << 1)
#define Q2_U_ANGLE2 (UINT64_C(1) << 2)
#define Q2_U_ANGLE3 (UINT64_C(1) << 3)
#define Q2_U_FRAME8 (UINT64_C(1) << 4)
#define Q2_U_EVENT (UINT64_C(1) << 5)
#define Q2_U_REMOVE (UINT64_C(1) << 6)
#define Q2_U_MORE1 (UINT64_C(1) << 7)
#define Q2_U_NUMBER16 (UINT64_C(1) << 8)
#define Q2_U_ORIGIN3 (UINT64_C(1) << 9)
#define Q2_U_ANGLE1 (UINT64_C(1) << 10)
#define Q2_U_MODEL (UINT64_C(1) << 11)
#define Q2_U_RENDER8 (UINT64_C(1) << 12)
#define Q2_U_ANGLE16 (UINT64_C(1) << 13)
#define Q2_U_EFFECTS8 (UINT64_C(1) << 14)
#define Q2_U_MORE2 (UINT64_C(1) << 15)
#define Q2_U_SKIN8 (UINT64_C(1) << 16)
#define Q2_U_FRAME16 (UINT64_C(1) << 17)
#define Q2_U_RENDER16 (UINT64_C(1) << 18)
#define Q2_U_EFFECTS16 (UINT64_C(1) << 19)
#define Q2_U_MODEL2 (UINT64_C(1) << 20)
#define Q2_U_MODEL3 (UINT64_C(1) << 21)
#define Q2_U_MODEL4 (UINT64_C(1) << 22)
#define Q2_U_MORE3 (UINT64_C(1) << 23)
#define Q2_U_OLDORIGIN (UINT64_C(1) << 24)
#define Q2_U_SKIN16 (UINT64_C(1) << 25)
#define Q2_U_SOUND (UINT64_C(1) << 26)
#define Q2_U_SOLID (UINT64_C(1) << 27)
#define Q2_U_MODEL16 (UINT64_C(1) << 28)
#define Q2_U_MOREFX8 (UINT64_C(1) << 29)
#define Q2_U_ALPHA (UINT64_C(1) << 30)
#define Q2_U_MORE4 (UINT64_C(1) << 31)
#define Q2_U_SCALE (UINT64_C(1) << 32)
#define Q2_U_MOREFX16 (UINT64_C(1) << 33)
#define Q2_U_OWNER (UINT64_C(1) << 34)
#define Q2_U_OLDFRAME (UINT64_C(1) << 35)
enum {
    Q2_PS_TYPE=1, Q2_PS_ORIGIN=2, Q2_PS_VELOCITY=4, Q2_PS_TIME=8,
    Q2_PS_FLAGS=16, Q2_PS_GRAVITY=32, Q2_PS_DELTA_ANGLES=64,
    Q2_PS_VIEWOFFSET=128, Q2_PS_VIEWANGLES=256, Q2_PS_KICK=512,
    Q2_PS_BLEND=1024, Q2_PS_FOV=2048, Q2_PS_WEAPON=4096,
    Q2_PS_WEAPONFRAME=8192, Q2_PS_RDFLAGS=16384,
    Q2_PS_VIEWHEIGHT=32768, Q2_PS_DAMAGE_BLEND=65536, Q2_PS_TEAM=131072,
    Q2_EPS_GUNOFFSET=1, Q2_EPS_GUNANGLES=2, Q2_EPS_VELOCITY_Z=4,
    Q2_EPS_ORIGIN_Z=8, Q2_EPS_VIEWANGLE_Z=16, Q2_EPS_STATS=32,
    Q2_EPS_GUNRATE=128
};

bool qa_q2_wide_read_header(qa_q2_codec *,qa_net_reader *,uint32_t *,uint64_t *);
bool qa_q2_wide_write_header(qa_q2_codec *,qa_net_writer *,uint32_t,uint64_t);
bool qa_q2_wide_remove(qa_q2_codec *,qa_net_writer *,uint32_t);
/* Shared rerelease entity fields; KEX changes ordering, effects and coordinates. */
bool qa_q2_extended_read_entity(qa_q2_codec *,qa_net_reader *,const qa_q2_entity *,uint32_t,uint64_t,qa_q2_entity *,bool kex);
bool qa_q2_extended_write_entity(qa_q2_codec *,qa_net_writer *,const qa_q2_entity *,const qa_q2_entity *,bool force,bool fresh,bool kex);

static inline float q2_read_float(qa_net_reader *r) {
    float v=qa_net_read_f32(r);
    if(!isfinite(v))qa_net_reader_fail(r,"Nonfinite Q2 rerelease scalar");
    return isfinite(v)?v:0.0f;
}
static inline int16_t q2_fixed(float f,float scale) {
    double n=trunc((double)f*(double)scale);
    if(!isfinite(n))return 0;
    if(n>32767.0)return INT16_MAX;
    if(n< -32768.0)return INT16_MIN;
    return (int16_t)n;
}
static inline int16_t q2_pm_short(float f) {
    double n=floor((double)f*8.0+0.5);
    if(!isfinite(n))return 0;
    if(n>32767.0)return INT16_MAX;
    if(n< -32768.0)return INT16_MIN;
    return (int16_t)n;
}
static inline uint16_t q2_angle_short(float f) {
    if(!isfinite(f))return 0;
    return (uint16_t)(int32_t)trunc(fmod((double)f,360.0)*(65536.0/360.0));
}
static inline uint8_t q2_byte_color(float f,bool clamp) {
    double n=trunc((double)f*255.0);
    if(!isfinite(n))return 0;
    if(clamp){if(n<0.0)return 0;if(n>255.0)return 255;}
    return (uint8_t)(int32_t)fmod(n,256.0);
}
static inline uint8_t q2_quantized(float f,float scale,bool nonzero) {
    if(f==0.0f)return 0;
    double n=trunc((double)f*(double)scale);
    if(!isfinite(n))return 0;
    if(n<(nonzero?1.0:0.0))return (uint8_t)(nonzero?1:0);
    if(n>255.0)return 255;
    return (uint8_t)n;
}
static inline uint8_t q2_loop_volume(float f) {
    uint8_t b=q2_quantized(f,255.0f,false);return b==255?0:b;
}
static inline uint8_t q2_loop_attenuation(float f) {
    if(f== -1.0f)return 192;
    uint8_t b=q2_quantized(f,64.0f,false);return b==192?0:b;
}
static inline uint64_t q2_width(uint32_t n,uint64_t byte,uint64_t word,bool unsigned_word) {
    return (n&(unsigned_word?UINT32_C(0xffff0000):UINT32_C(0xffff8000)))?byte|word:n>255?word:byte;
}
static inline void q2_write_width(qa_net_writer *w,uint32_t n,uint64_t b,uint64_t byte,uint64_t word) {
    if((b&(byte|word))==(byte|word))qa_net_write_u32(w,n);
    else if(b&word)qa_net_write_u16(w,(uint16_t)n);
    else if(b&byte)qa_net_write_u8(w,(uint8_t)n);
}
static inline uint32_t q2_read_width(qa_net_reader *r,uint32_t n,uint64_t b,uint64_t byte,uint64_t word) {
    if((b&(byte|word))==(byte|word))return qa_net_read_u32(r);
    if(b&word)return qa_net_read_u16(r);
    if(b&byte)return qa_net_read_u8(r);
    return n;
}
static inline bool q2_finite_array(const float *p,size_t n) {
    for(size_t i=0;i<n;i++)if(!isfinite(p[i]))return false;
    return true;
}
static inline bool q2_player_finite(const qa_q2_player *p) {
    return q2_finite_array(p->pmove.origin_f,3)&&q2_finite_array(p->pmove.velocity_f,3)&&
        q2_finite_array(p->pmove.delta_angles_f,3)&&q2_finite_array(p->viewangles,3)&&
        q2_finite_array(p->viewoffset,3)&&q2_finite_array(p->kick_angles,3)&&
        q2_finite_array(p->gunangles,3)&&q2_finite_array(p->gunoffset,3)&&
        q2_finite_array(p->blend,4)&&q2_finite_array(p->damage_blend,4)&&isfinite(p->fov);
}
static inline bool q2_fog_nonzero(const qa_q2_player_fog *fog) {
    if(fog->density||fog->sky_factor||fog->height_density||fog->height_falloff||
       fog->height_start_distance||fog->height_end_distance)return true;
    for(unsigned i=0;i<3;i++)
        if(fog->color[i]||fog->height_start_color[i]||fog->height_end_color[i])return true;
    return false;
}
static inline bool q2_changed3(const float a[3],const float b[3]) {
    return a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2];
}
static inline bool q2_fixed_changed3(const float a[3],const float b[3],float scale) {
    for(unsigned i=0;i<3;i++)if(q2_fixed(a[i],scale)!=q2_fixed(b[i],scale))return true;
    return false;
}
#endif
