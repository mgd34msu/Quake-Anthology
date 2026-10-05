#include "qa/network_q2_mvd.h"
#include "qa/math.h"
#include "q2pro_internal.h"
#include <stdlib.h>
#include <errno.h>

enum { MP_TYPE=1, MP_ORIGIN=2, MP_ORIGIN_Z=4, MP_VIEWOFFSET=8,
    MP_VIEWANGLES=16, MP_VIEWANGLE_Z=32, MP_KICK=64, MP_BLEND=128,
    MP_FOV=256, MP_GUNINDEX=512, MP_GUNFRAME=1024, MP_GUNOFFSET=2048,
    MP_GUNANGLES=4096, MP_RDFLAGS=8192, MP_STATS=16384, MP_MORE=32768,
    MP_REMOVE=65536, MP_FOG=131072 };

bool qa_q2_mvd_profile_init(uint16_t revision, uint16_t flags,
                             qa_q2_mvd_profile *out, qa_error *error)
{
    if (!out || !((revision>=2009 && revision<=2013) || revision==3038)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Unsupported MVD revision"); return false;
    }
    qa_q2_mvd_profile p={0}; p.revision=revision; p.flags=flags;
    p.rerelease=revision==3038;
    p.extended=p.rerelease || (revision>=2011 && (flags&4)!=0);
    p.v2=!p.rerelease && revision>=2012 && (flags&8)!=0;
    p.fog=p.v2 && revision>=2013;
    if (p.v2 && !p.extended) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"MVD v2 requires extended limits"); return false;
    }
    p.protocol.kind=p.rerelease ? QA_NET_Q2REPRO_1038 : p.extended ? QA_NET_Q2PRO_36 : QA_NET_Q2_34;
    if (p.extended && !p.rerelease) p.protocol.revision=revision==2013 ? 1026u : p.v2 ? 1025u : 1024u;
    p.max_configstrings=p.rerelease ? 12448 : p.extended ? 13630 : 2080;
    p.max_clients_index=p.extended ? 60 : 30; p.max_entities=p.extended ? 8192 : 1024;
    *out=p; return true;
}
bool qa_q2_mvd_read_command(qa_net_reader *r, qa_q2_mvd_command *out)
{
    if (!r || !out) return false;
    uint8_t b=qa_net_read_u8(r);
    if ((b&31)>QA_MVD_STUFFTEXT || !(b&31)) return qa_net_reader_fail(r,"Invalid MVD command");
    if (r->failed) return false;
    *out=(qa_q2_mvd_command){(qa_q2_mvd_opcode)(b&31),(uint8_t)(b>>5)}; return true;
}
bool qa_q2_mvd_write_command(qa_net_writer *w, qa_q2_mvd_command c)
{
    if (!w) return false;
    if (c.opcode<=QA_MVD_BAD || c.opcode>QA_MVD_STUFFTEXT || c.extra>7)
        return qa_net_writer_fail(w,"Invalid MVD command");
    return qa_net_write_u8(w,(uint8_t)((unsigned)c.opcode|((unsigned)c.extra<<5)));
}
static bool mvd_text(qa_net_reader *r, qa_bytes *out)
{
    if (r->failed || (r->bit&7)) return qa_net_reader_fail(r,"Unaligned MVD text");
    size_t offset=r->bit/8, left=qa_net_reader_remaining(r);
    const uint8_t *end=left ? memchr(r->bytes.data+offset,0,left) : NULL;
    if (!end) return qa_net_reader_fail(r,"Unterminated MVD text");
    size_t count=(size_t)(end-(r->bytes.data+offset));
    *out=(qa_bytes){r->bytes.data+offset,count}; r->bit+=(count+1)*8; return true;
}
static bool mvd_write_text(qa_net_writer *w, qa_bytes text)
{
    if ((text.size && !text.data) || (text.size && memchr(text.data,0,text.size)))
        return qa_net_writer_fail(w,"Invalid MVD text span");
    return qa_net_write_data(w,text.data,text.size) && qa_net_write_u8(w,0);
}
void qa_q2_mvd_header_free(qa_q2_mvd_header *h)
{
    if (!h) return;
    for (size_t i=0;i<h->config_count;i++) free(h->configstrings[i].value);
    free(h->configstrings); memset(h,0,sizeof(*h));
}
static bool mvd_client_limits(qa_q2_mvd_header *h)
{
    const char *value=NULL;
    for (size_t i=0;i<h->config_count;i++)
        if (h->configstrings[i].index==h->profile.max_clients_index) value=h->configstrings[i].value;
    if (!value || !*value) return false;
    char *end; errno=0; long n=strtol(value,&end,10);
    while (*end==' ' || *end=='\t' || *end=='\r' || *end=='\n') end++;
    if (errno || *end || n<1 || n>256 || h->dummy < -1 || h->dummy>=n) return false;
    h->max_clients=(uint16_t)n; return true;
}
bool qa_q2_mvd_read_header(qa_net_reader *r, qa_q2_mvd_command c, qa_q2_mvd_header *out)
{
    if (!r || !out) return false;
    qa_q2_mvd_header h={0};
    if (c.opcode!=QA_MVD_SERVERDATA || qa_net_read_i32(r)!=37)
        return qa_net_reader_fail(r,"Expected MVD serverdata");
    uint16_t revision=qa_net_read_u16(r);
    uint16_t flags=revision>=2012 && revision!=3038 ? qa_net_read_u16(r) : c.extra;
    if (r->failed) return false;
    if (!qa_q2_mvd_profile_init(revision,flags,&h.profile,r->error)) { r->failed=true; return false; }
    h.servercount=qa_net_read_i32(r);
    if (!qa_net_read_string(r,h.gamedir,sizeof(h.gamedir))) return false;
    h.dummy=qa_net_read_i16(r);
    h.configstrings=calloc(h.profile.max_configstrings,sizeof(*h.configstrings));
    uint16_t *slots=calloc(h.profile.max_configstrings,sizeof(*slots));
    if (!h.configstrings || !slots) { free(slots); qa_q2_mvd_header_free(&h); qa_error_set(r->error,QA_ERROR_MEMORY,r->bit/8,"MVD config allocation failed"); r->failed=true; return false; }
    for (;;) {
        uint16_t index=qa_net_read_u16(r); qa_bytes value;
        if (r->failed) goto fail;
        if (index==h.profile.max_configstrings) break;
        if (index>h.profile.max_configstrings) { qa_net_reader_fail(r,"MVD config index out of range"); goto fail; }
        if (!mvd_text(r,&value)) goto fail;
        size_t slot=slots[index]?(size_t)slots[index]-1:h.config_count;
        char *copy=malloc(value.size+1);
        if (!copy) { qa_error_set(r->error,QA_ERROR_MEMORY,r->bit/8,"MVD text allocation failed"); r->failed=true; goto fail; }
        memcpy(copy,value.data,value.size); copy[value.size]=0;
        if (slot<h.config_count) free(h.configstrings[slot].value);
        else { h.config_count++; slots[index]=(uint16_t)h.config_count; }
        h.configstrings[slot]=(qa_q2_mvd_config){index,copy};
    }
    if (!mvd_client_limits(&h)) { qa_net_reader_fail(r,"Invalid MVD player limits"); goto fail; }
    free(slots); *out=h; return true;
fail: free(slots); qa_q2_mvd_header_free(&h); return false;
}
bool qa_q2_mvd_write_header(qa_net_writer *w, const qa_q2_mvd_header *h)
{
    if (!w || !h) return false;
    qa_q2_mvd_profile p;
    if (!qa_q2_mvd_profile_init(h->profile.revision,h->profile.flags,&p,w->error)) { w->failed=true; return false; }
    bool short_flags=p.revision<2012 || p.rerelease;
    qa_q2_mvd_header limits=*h; limits.profile=p;
    if ((h->config_count && !h->configstrings) || h->config_count>p.max_configstrings ||
        (short_flags && p.flags>7) || !mvd_client_limits(&limits) ||
        !memchr(h->gamedir,0,sizeof(h->gamedir))) return qa_net_writer_fail(w,"Invalid MVD header");
    if (!qa_q2_mvd_write_command(w,(qa_q2_mvd_command){QA_MVD_SERVERDATA,(uint8_t)(short_flags?p.flags:0)}) ||
        !qa_net_write_i32(w,37) || !qa_net_write_u16(w,p.revision) ||
        (!short_flags && !qa_net_write_u16(w,p.flags)) || !qa_net_write_i32(w,h->servercount) ||
        !qa_net_write_string(w,h->gamedir) || !qa_net_write_i16(w,h->dummy)) return false;
    for (size_t i=0;i<h->config_count;i++) {
        if (h->configstrings[i].index>=p.max_configstrings || !h->configstrings[i].value)
            return qa_net_writer_fail(w,"Invalid MVD configstring");
        if (!qa_net_write_u16(w,h->configstrings[i].index) || !qa_net_write_string(w,h->configstrings[i].value)) return false;
    }
    return qa_net_write_u16(w,p.max_configstrings);
}
static qa_q2_player mvd_player_copy(const qa_q2_mvd_profile *p, const qa_q2_player *from)
{
    qa_q2_player s={0}; if (!from) return s;
    s.pmove.type=from->pmove.type; memcpy(s.pmove.origin,from->pmove.origin,sizeof(s.pmove.origin));
    memcpy(s.viewoffset,from->viewoffset,sizeof(s.viewoffset)); memcpy(s.viewangles,from->viewangles,sizeof(s.viewangles));
    memcpy(s.kick_angles,from->kick_angles,sizeof(s.kick_angles)); memcpy(s.gunoffset,from->gunoffset,sizeof(s.gunoffset));
    memcpy(s.gunangles,from->gunangles,sizeof(s.gunangles)); memcpy(s.blend,from->blend,sizeof(s.blend));
    memcpy(s.stats,from->stats,sizeof(s.stats)); s.gunindex=from->gunindex; s.gunframe=from->gunframe;
    s.fov=from->fov; s.rdflags=from->rdflags;
    if (p->extended) { memcpy(s.pmove.origin_f,from->pmove.origin_f,sizeof(s.pmove.origin_f));
        memcpy(s.damage_blend,from->damage_blend,sizeof(s.damage_blend)); s.gunskin=from->gunskin; s.fog=from->fog; }
    return s;
}
static void mvd_vector_read(qa_net_reader *r, float v[3], float scale, bool wide)
{ for (unsigned i=0;i<3;i++) v[i]=(float)(wide?qa_net_read_i16(r):qa_net_read_i8(r))/scale; }
static bool mvd_vector_write(qa_net_writer *w, const float v[3], float scale, bool wide)
{
    for (unsigned i=0;i<3;i++)
        if (!qa_q2_write_scaled(w,v[i],scale,wide?16u:8u,true)) return false;
    return true;
}
bool qa_q2_mvd_read_player(const qa_q2_mvd_profile *p, qa_net_reader *r, uint8_t number,
                           const qa_q2_player *from, bool *removed, qa_q2_player *out)
{
    if (!p || !r || !removed || !out) return false;
    if (number==255) return qa_net_reader_fail(r,"Invalid MVD player number");
    uint32_t bits=qa_net_read_u16(r); qa_q2_player s=mvd_player_copy(p,from);
    bool remove=false;
    if (bits&MP_MORE) {
        if (p->fog) bits|=(uint32_t)qa_net_read_u8(r)<<16;
        else if (p->extended || bits==MP_MORE) remove=true;
    }
    if (!remove) {
        if (bits&MP_TYPE) s.pmove.type=qa_net_read_u8(r);
        for (unsigned i=0;i<3;i++) if (bits&(i==2?MP_ORIGIN_Z:MP_ORIGIN)) {
            if (p->rerelease) {
                s.pmove.origin_f[i]=qa_net_read_f32(r);
                if (!isfinite(s.pmove.origin_f[i])) return qa_net_reader_fail(r,"Nonfinite MVD origin");
            }
            else if (p->v2) { if (!qa_q2pro_read_int23(r,s.pmove.origin[i],&s.pmove.origin[i])) return false; }
            else s.pmove.origin[i]=qa_net_read_i16(r);
        }
        if (bits&MP_VIEWOFFSET) mvd_vector_read(r,s.viewoffset,p->rerelease?16.0f:4.0f,p->rerelease);
        if (bits&MP_VIEWANGLES) { s.viewangles[0]=qa_q2_read_angle16(r); s.viewangles[1]=qa_q2_read_angle16(r); }
        if (bits&MP_VIEWANGLE_Z) s.viewangles[2]=qa_q2_read_angle16(r);
        if (bits&MP_KICK) mvd_vector_read(r,s.kick_angles,p->rerelease?1024.0f:4.0f,p->rerelease);
        if (bits&MP_GUNINDEX) { uint32_t packed=p->extended?qa_net_read_u16(r):qa_net_read_u8(r);
            s.gunindex=p->extended?packed&8191u:packed; s.gunskin=p->extended?packed>>13:0; }
        if (bits&MP_GUNFRAME) s.gunframe=p->rerelease?qa_net_read_u16(r):qa_net_read_u8(r);
        if (bits&MP_GUNOFFSET) mvd_vector_read(r,s.gunoffset,p->rerelease?512.0f:p->extended?8.0f:4.0f,p->extended);
        if (bits&MP_GUNANGLES) mvd_vector_read(r,s.gunangles,p->rerelease?4096.0f:p->extended?65536.0f/360.0f:4.0f,p->extended);
        if (bits&MP_BLEND) {
            uint8_t mask=(p->v2||p->rerelease)?qa_net_read_u8(r):15;
            for (unsigned i=0;i<4;i++) if (mask&(1u<<i)) s.blend[i]=(float)qa_net_read_u8(r)/255.0f;
            for (unsigned i=0;i<4;i++) if (mask&(16u<<i)) s.damage_blend[i]=(float)qa_net_read_u8(r)/255.0f;
        }
        if (bits&MP_FOG) { if (!qa_q2pro_read_fog(r,&s.fog,&s.fog)) return false; }
        if (bits&MP_FOV) s.fov=qa_net_read_u8(r);
        if (bits&MP_RDFLAGS) s.rdflags=qa_net_read_u8(r);
        if (bits&MP_STATS) {
            uint64_t mask;
            if (p->rerelease) mask=qa_net_read_u64(r);
            else if (p->v2) { if (!qa_q2pro_read_var64(r,&mask)) return false; }
            else mask=qa_net_read_u32(r);
            for (unsigned i=0;i<64;i++) if (mask&(UINT64_C(1)<<i)) s.stats[i]=qa_net_read_i16(r);
        }
        remove=(bits&MP_REMOVE)!=0;
    }
    if (r->failed) return false;
    *removed=remove; *out=s; return true;
}
static bool mvd_diff(const float *a, const float *b, unsigned n)
{
    for (unsigned i=0;i<n;i++) if (a[i]!=b[i]) return true;
    return false;
}
bool qa_q2_mvd_write_player(const qa_q2_mvd_profile *p, qa_net_writer *w, uint8_t number,
                            const qa_q2_player *from, const qa_q2_player *to, bool force)
{
    if (!p || !w) return false;
    if (number==255) return qa_net_writer_fail(w,"Invalid MVD player number");
    if (!to) return qa_net_write_u8(w,number) && qa_net_write_u16(w,MP_MORE) && (!p->fog || qa_net_write_u8(w,1));
    qa_q2_player zero={0}; const qa_q2_player *f=from?from:&zero;
    for (unsigned i=0;i<3;i++) {
        if (!isfinite(to->viewangles[i]) || !isfinite(f->viewangles[i]) ||
            (p->rerelease && !isfinite(to->pmove.origin_f[i])))
            return qa_net_writer_fail(w,"Nonfinite MVD player transform");
    }
    uint32_t bits=0; uint64_t stats=0;
    if (to->pmove.type!=f->pmove.type) bits|=MP_TYPE;
    for (unsigned i=0;i<3;i++) if (p->rerelease?to->pmove.origin_f[i]!=f->pmove.origin_f[i]:to->pmove.origin[i]!=f->pmove.origin[i]) bits|=i==2?MP_ORIGIN_Z:MP_ORIGIN;
    if (mvd_diff(to->viewoffset,f->viewoffset,3)) bits|=MP_VIEWOFFSET;
    if (qa_angle_to_word(to->viewangles[0])!=qa_angle_to_word(f->viewangles[0]) || qa_angle_to_word(to->viewangles[1])!=qa_angle_to_word(f->viewangles[1])) bits|=MP_VIEWANGLES;
    if (qa_angle_to_word(to->viewangles[2])!=qa_angle_to_word(f->viewangles[2])) bits|=MP_VIEWANGLE_Z;
    if (mvd_diff(to->kick_angles,f->kick_angles,3)) bits|=MP_KICK;
    if (to->gunindex!=f->gunindex || (p->extended && to->gunskin!=f->gunskin)) bits|=MP_GUNINDEX;
    if (to->gunframe!=f->gunframe) bits|=MP_GUNFRAME;
    if (mvd_diff(to->gunoffset,f->gunoffset,3)) bits|=MP_GUNOFFSET;
    if (mvd_diff(to->gunangles,f->gunangles,3)) bits|=MP_GUNANGLES;
    if (mvd_diff(to->blend,f->blend,4) || ((p->v2||p->rerelease) && mvd_diff(to->damage_blend,f->damage_blend,4))) bits|=MP_BLEND;
    uint8_t fog=p->fog?qa_q2pro_fog_bits(&f->fog,&to->fog):0;
    if (fog) bits|=MP_MORE|MP_FOG;
    if (to->fov!=f->fov) bits|=MP_FOV;
    if (to->rdflags!=f->rdflags) bits|=MP_RDFLAGS;
    for (unsigned i=0;i<(p->v2||p->rerelease?64u:32u);i++) if (to->stats[i]!=f->stats[i]) stats|=UINT64_C(1)<<i;
    if (stats) bits|=MP_STATS;
    if (!bits && !force && from) return true;
    if (!qa_net_write_u8(w,number) || !qa_net_write_u16(w,(uint16_t)bits) || ((bits&MP_MORE) && !qa_net_write_u8(w,(uint8_t)(bits>>16)))) return false;
    if (bits&MP_TYPE) {
        if (to->pmove.type<0 || to->pmove.type>255) return qa_net_writer_fail(w,"MVD movement type out of range");
        qa_net_write_u8(w,(uint8_t)to->pmove.type);
    }
    for (unsigned i=0;i<3;i++) if (bits&(i==2?MP_ORIGIN_Z:MP_ORIGIN)) {
        if (p->rerelease) {
            if (!isfinite(to->pmove.origin_f[i])) return qa_net_writer_fail(w,"Nonfinite MVD origin");
            qa_net_write_f32(w,to->pmove.origin_f[i]);
        }
        else if (p->v2) qa_q2pro_write_int23(w,to->pmove.origin[i],f->pmove.origin[i]);
        else {
            if (to->pmove.origin[i]<INT16_MIN || to->pmove.origin[i]>INT16_MAX) return qa_net_writer_fail(w,"MVD origin out of range");
            qa_net_write_i16(w,(int16_t)to->pmove.origin[i]);
        }
    }
    if (bits&MP_VIEWOFFSET) mvd_vector_write(w,to->viewoffset,p->rerelease?16.0f:4.0f,p->rerelease);
    if (bits&MP_VIEWANGLES) { qa_q2_write_angle16(w,to->viewangles[0]); qa_q2_write_angle16(w,to->viewangles[1]); }
    if (bits&MP_VIEWANGLE_Z) qa_q2_write_angle16(w,to->viewangles[2]);
    if (bits&MP_KICK) mvd_vector_write(w,to->kick_angles,p->rerelease?1024.0f:4.0f,p->rerelease);
    if (bits&MP_GUNINDEX) {
        if (to->gunindex>(p->extended?8191u:255u) || (p->extended && to->gunskin>7)) return qa_net_writer_fail(w,"MVD gun index out of range");
        if (p->extended) qa_net_write_u16(w,(uint16_t)(to->gunindex|(to->gunskin<<13))); else qa_net_write_u8(w,(uint8_t)to->gunindex);
    }
    if (bits&MP_GUNFRAME) { if (to->gunframe>(p->rerelease?65535u:255u)) return qa_net_writer_fail(w,"MVD gun frame out of range");
        if (p->rerelease) qa_net_write_u16(w,(uint16_t)to->gunframe); else qa_net_write_u8(w,(uint8_t)to->gunframe); }
    if (bits&MP_GUNOFFSET) mvd_vector_write(w,to->gunoffset,p->rerelease?512.0f:p->extended?8.0f:4.0f,p->extended);
    if (bits&MP_GUNANGLES) mvd_vector_write(w,to->gunangles,p->rerelease?4096.0f:p->extended?65536.0f/360.0f:4.0f,p->extended);
    if (bits&MP_BLEND) {
        uint8_t mask=15;
        if (p->v2||p->rerelease) { mask=0; for (unsigned i=0;i<4;i++) {
            if (to->blend[i]!=f->blend[i]) mask|=(uint8_t)(1u<<i);
            if (to->damage_blend[i]!=f->damage_blend[i]) mask|=(uint8_t)(16u<<i); }
            qa_net_write_u8(w,mask); }
        for (unsigned i=0;i<4;i++) if (mask&(1u<<i)) qa_q2_write_scaled(w,to->blend[i],255.0f,8,false);
        for (unsigned i=0;i<4;i++) if (mask&(16u<<i)) qa_q2_write_scaled(w,to->damage_blend[i],255.0f,8,false);
    }
    if (fog) qa_q2pro_write_fog(w,fog,&to->fog);
    if (bits&MP_FOV) qa_q2_write_scaled(w,to->fov,1.0f,8,false);
    if (bits&MP_RDFLAGS) {
        if (to->rdflags>255) return qa_net_writer_fail(w,"MVD render flags out of range");
        qa_net_write_u8(w,(uint8_t)to->rdflags);
    }
    if (stats) {
        if (p->rerelease) qa_net_write_u64(w,stats); else if (p->v2) qa_q2pro_write_var64(w,stats); else qa_net_write_u32(w,(uint32_t)stats);
        for (unsigned i=0;i<64;i++) if (stats&(UINT64_C(1)<<i)) qa_net_write_i16(w,to->stats[i]);
    }
    return !w->failed;
}

void qa_q2_mvd_frame_free(qa_q2_mvd_frame *f)
{ if (f) { free(f->players); free(f->entities); memset(f,0,sizeof(*f)); } }
static bool mvd_frame_valid(const qa_q2_mvd_profile *p, const qa_q2_mvd_frame *f)
{
    if (!f) return true;
    if (f->player_count>255 || f->entity_count>=p->max_entities ||
        (f->player_count && !f->players) || (f->entity_count && !f->entities)) return false;
    for (size_t i=0;i<f->player_count;i++) if (f->players[i].number==255 ||
        (i && f->players[i-1].number>=f->players[i].number)) return false;
    for (size_t i=0;i<f->entity_count;i++) if (!f->entities[i].number ||
        f->entities[i].number>=p->max_entities || (i && f->entities[i-1].number>=f->entities[i].number)) return false;
    return true;
}
static bool mvd_codec(const qa_q2_mvd_profile *p, qa_q2_codec *c, qa_error *error)
{
    if (!qa_q2_codec_init(c,p->protocol,error)) return false;
    if (p->protocol.kind==QA_NET_Q2PRO_36) c->wire_flags=p->v2?24:8;
    return true;
}
bool qa_q2_mvd_read_frame(const qa_q2_mvd_profile *p, qa_net_reader *r,
                          const qa_q2_mvd_frame *previous, uint16_t max_clients, int16_t dummy, qa_q2_mvd_frame *out)
{
    if (!p || !r || !out) return false;
    if ((p->max_entities!=1024 && p->max_entities!=8192) || !max_clients || max_clients>256 ||
        dummy < -1 || dummy >= (int)max_clients || !mvd_frame_valid(p,previous))
        return qa_net_reader_fail(r,"Invalid MVD prior frame or player limits");
    qa_q2_codec c;
    if (!mvd_codec(p,&c,r->error)) { r->failed=true; return false; }
    qa_q2_mvd_frame f={0};
    qa_q2_player *players=calloc(255,sizeof(*players));
    qa_q2_entity *entities=calloc(p->max_entities,sizeof(*entities));
    const qa_q2_entity **old=calloc(p->max_entities,sizeof(*old));
    bool active[255]={0};
    if (!players || !entities || !old) {
        qa_error_set(r->error,QA_ERROR_MEMORY,r->bit/8,"MVD frame allocation failed"); r->failed=true; goto fail;
    }
    if (previous) {
        for (size_t i=0;i<previous->player_count;i++) {
            unsigned n=previous->players[i].number;
            if (n>=max_clients) { qa_net_reader_fail(r,"MVD prior player exceeds client limit"); goto fail; }
            players[n]=previous->players[i].state; active[n]=true;
        }
        for (size_t i=0;i<previous->entity_count;i++) {
            const qa_q2_entity *s=&previous->entities[i]; old[s->number]=s;
            if (!qa_q2_read_entity(&c,r,s,s->number,0,&entities[s->number])) goto fail;
            memcpy(entities[s->number].old_origin,s->renderfx&128?s->old_origin:s->origin,sizeof(s->origin));
        }
    }
    f.portal_bytes=qa_net_read_u8(r);
    if (!qa_net_read_data(r,f.portal_bits,f.portal_bytes)) goto fail;
    for (;;) {
        uint8_t n=qa_net_read_u8(r); bool removed;
        if (r->failed) goto fail;
        if (n==255) break;
        if (n>=max_clients) { qa_net_reader_fail(r,"MVD player exceeds client limit"); goto fail; }
        qa_q2_player player;
        if (!qa_q2_mvd_read_player(p,r,n,active[n]?&players[n]:NULL,&removed,&player)) goto fail;
        player.clientnum=n; players[n]=player; active[n]=!removed;
    }
    for (;;) {
        uint32_t n; uint64_t bits;
        if (!qa_q2_read_entity_header(&c,r,&n,&bits)) goto fail;
        if (!n) break;
        if (n>=p->max_entities) { qa_net_reader_fail(r,"MVD entity number out of range"); goto fail; }
        if (bits&UINT64_C(64)) { memset(&entities[n],0,sizeof(entities[n])); continue; }
        qa_q2_entity zero={0}; const qa_q2_entity *s=old[n]?old[n]:&zero;
        if (!qa_q2_read_entity(&c,r,s,n,bits,&entities[n])) goto fail;
        if (!(bits&(UINT64_C(1)<<24))) memcpy(entities[n].old_origin,s->renderfx&128?s->old_origin:s->origin,sizeof(s->origin));
        /* Classic live messages expose signed words; MVD restores their
         * unsigned state representation before retaining the next baseline. */
        if (bits&(UINT64_C(1)<<17)) entities[n].frame&=65535u;
        if ((bits&((UINT64_C(1)<<16)|(UINT64_C(1)<<25)))==(UINT64_C(1)<<25)) entities[n].skinnum&=65535u;
        if ((bits&((UINT64_C(1)<<14)|(UINT64_C(1)<<19)))==(UINT64_C(1)<<19)) entities[n].effects&=65535u;
        if ((bits&((UINT64_C(1)<<12)|(UINT64_C(1)<<18)))==(UINT64_C(1)<<18)) entities[n].renderfx&=65535u;
        if (!p->extended && (bits&(UINT64_C(1)<<27))) entities[n].solid&=65535u;
    }
    for (unsigned n=0;n<255;n++) if (active[n]) {
        f.player_count++;
        qa_q2_entity *s=&entities[n+1]; const qa_q2_player *player=&players[n];
        if (!s->number || (int)n==dummy || player->pmove.type!=0) continue;
        for (unsigned i=0;i<3;i++) s->origin[i]=p->rerelease?player->pmove.origin_f[i]:(float)player->pmove.origin[i]/8.0f;
        float pitch=player->viewangles[0]; s->angles[0]=(pitch>180.0f?pitch-360.0f:pitch)/3.0f;
        s->angles[1]=player->viewangles[1]; s->angles[2]=0;
    }
    for (unsigned n=1;n<p->max_entities;n++) if (entities[n].number) f.entity_count++;
    f.players=f.player_count?malloc(f.player_count*sizeof(*f.players)):NULL;
    f.entities=f.entity_count?malloc(f.entity_count*sizeof(*f.entities)):NULL;
    if ((f.player_count && !f.players) || (f.entity_count && !f.entities)) {
        qa_error_set(r->error,QA_ERROR_MEMORY,r->bit/8,"MVD result allocation failed"); r->failed=true; goto fail;
    }
    size_t at=0; for (unsigned n=0;n<255;n++) if (active[n]) f.players[at++]=(qa_q2_mvd_player){(uint8_t)n,players[n]};
    at=0; for (unsigned n=1;n<p->max_entities;n++) if (entities[n].number) f.entities[at++]=entities[n];
    free(players); free(entities); free(old); *out=f; return true;
fail: free(players); free(entities); free(old); qa_q2_mvd_frame_free(&f); return false;
}
bool qa_q2_mvd_write_frame(const qa_q2_mvd_profile *p, qa_net_writer *w,
                           const qa_q2_mvd_frame *previous, const qa_q2_mvd_frame *f)
{
    if (!p || !w || !f) return false;
    if (!mvd_frame_valid(p,previous) || !mvd_frame_valid(p,f)) return qa_net_writer_fail(w,"Invalid MVD frame order or identity");
    qa_q2_codec c;
    if (!mvd_codec(p,&c,w->error)) { w->failed=true; return false; }
    if (!qa_net_write_u8(w,f->portal_bytes) || !qa_net_write_data(w,f->portal_bits,f->portal_bytes)) return false;
    size_t i=0,j=0, np=previous?previous->player_count:0;
    while (i<np || j<f->player_count) {
        unsigned a=i<np?previous->players[i].number:255, b=j<f->player_count?f->players[j].number:255;
        if (a<b) { if (!qa_q2_mvd_write_player(p,w,(uint8_t)a,&previous->players[i++].state,NULL,false)) return false; }
        else { const qa_q2_player *old=a==b?&previous->players[i++].state:NULL;
            if (!qa_q2_mvd_write_player(p,w,(uint8_t)b,old,&f->players[j++].state,old==NULL)) return false; }
    }
    if (!qa_net_write_u8(w,255)) return false;
    i=0; j=0; np=previous?previous->entity_count:0;
    while (i<np || j<f->entity_count) {
        uint32_t a=i<np?previous->entities[i].number:UINT32_MAX,b=j<f->entity_count?f->entities[j].number:UINT32_MAX;
        if (a<b) {
            if (!qa_q2_write_entity_remove(&c,w,a)) return false;
            i++;
        }
        else { qa_q2_entity zero={0}; const qa_q2_entity *old=a==b?&previous->entities[i++]:&zero;
            if (!qa_q2_write_entity(&c,w,old,&f->entities[j++],a!=b,true)) return false; }
    }
    return qa_q2_write_entity_end(&c,w);
}
bool qa_q2_mvd_read_record(const qa_q2_mvd_profile *p, qa_net_reader *r,
                           qa_q2_mvd_command c, qa_q2_mvd_record *out)
{
    if (!p || !r || !out) return false;
    qa_q2_mvd_record v={0}; v.command=c;
    switch (c.opcode) {
    case QA_MVD_NOP: case QA_MVD_DISCONNECT: case QA_MVD_RECONNECT: break;
    case QA_MVD_CONFIGSTRING:
        v.data.config.index=qa_net_read_u16(r);
        if (v.data.config.index>=p->max_configstrings) return qa_net_reader_fail(r,"MVD config index out of range");
        if (!mvd_text(r,&v.data.config.text)) return false;
        break;
    case QA_MVD_PRINT:
        v.data.print.level=qa_net_read_u8(r);
        if (!mvd_text(r,&v.data.print.text)) return false;
        break;
    case QA_MVD_STUFFTEXT:
        if (!mvd_text(r,&v.data.text)) return false;
        break;
    case QA_MVD_SOUND: {
        uint8_t flags=qa_net_read_u8(r); v.data.sound.flags=flags;
        v.data.sound.index=flags&32?qa_net_read_u16(r):qa_net_read_u8(r);
        v.data.sound.volume=flags&1?(float)qa_net_read_u8(r)/255.0f:1;
        v.data.sound.attenuation=flags&2?(float)qa_net_read_u8(r)/64.0f:1;
        v.data.sound.delay_seconds=flags&16?(float)qa_net_read_u8(r)/1000.0f:0;
        uint16_t channel=qa_net_read_u16(r); v.data.sound.entity=(uint16_t)(channel>>3); v.data.sound.channel=(uint8_t)(channel&7);
        v.data.sound.global=(c.extra&1)!=0;
        if (v.data.sound.entity>=p->max_entities) return qa_net_reader_fail(r,"MVD sound entity out of range");
        break;
    }
    case QA_MVD_UNICAST: case QA_MVD_UNICAST_RELIABLE:
    case QA_MVD_ALL: case QA_MVD_PHS: case QA_MVD_PVS:
    case QA_MVD_ALL_RELIABLE: case QA_MVD_PHS_RELIABLE: case QA_MVD_PVS_RELIABLE: {
        size_t count=(size_t)qa_net_read_u8(r)|((size_t)c.extra<<8);
        if (!count || count>2047) return qa_net_reader_fail(r,"Invalid MVD routed payload size");
        v.data.route.reliable=c.opcode==QA_MVD_UNICAST_RELIABLE || c.opcode>=QA_MVD_ALL_RELIABLE;
        if (c.opcode<=QA_MVD_UNICAST_RELIABLE) {
            v.data.route.recipient=QA_MVD_RECIPIENT_PLAYER; v.data.route.target=qa_net_read_u8(r);
            if (v.data.route.target==255) return qa_net_reader_fail(r,"Invalid MVD unicast recipient");
        } else if (c.opcode==QA_MVD_ALL || c.opcode==QA_MVD_ALL_RELIABLE) v.data.route.recipient=QA_MVD_RECIPIENT_ALL;
        else { v.data.route.recipient=c.opcode==QA_MVD_PHS || c.opcode==QA_MVD_PHS_RELIABLE?QA_MVD_RECIPIENT_PHS:QA_MVD_RECIPIENT_PVS;
            v.data.route.target=qa_net_read_u16(r);
            if (v.data.route.target==65535) return qa_net_reader_fail(r,"Invalid MVD multicast leaf"); }
        if (!qa_net_read_bytes(r,count,&v.data.route.payload)) return false;
        break;
    }
    default: return qa_net_reader_fail(r,"MVD record requires specialized state decoder");
    }
    if (r->failed) return false;
    *out=v; return true;
}
bool qa_q2_mvd_write_record(const qa_q2_mvd_profile *p, qa_net_writer *w, const qa_q2_mvd_record *v)
{
    if (!p || !w || !v) return false;
    qa_q2_mvd_command c=v->command;
    if (c.opcode>=QA_MVD_UNICAST && c.opcode<=QA_MVD_PVS_RELIABLE) {
        size_t n=v->data.route.payload.size;
        if (!n || n>2047 || !v->data.route.payload.data) return qa_net_writer_fail(w,"Invalid MVD routed payload");
        bool reliable=v->data.route.reliable;
        switch (v->data.route.recipient) {
        case QA_MVD_RECIPIENT_PLAYER:
            if (v->data.route.target>=255) return qa_net_writer_fail(w,"Invalid MVD unicast recipient");
            c.opcode=reliable?QA_MVD_UNICAST_RELIABLE:QA_MVD_UNICAST; break;
        case QA_MVD_RECIPIENT_ALL: c.opcode=reliable?QA_MVD_ALL_RELIABLE:QA_MVD_ALL; break;
        case QA_MVD_RECIPIENT_PVS: case QA_MVD_RECIPIENT_PHS:
            if (v->data.route.target==65535) return qa_net_writer_fail(w,"Invalid MVD multicast leaf");
            c.opcode=v->data.route.recipient==QA_MVD_RECIPIENT_PVS ? (reliable?QA_MVD_PVS_RELIABLE:QA_MVD_PVS) : (reliable?QA_MVD_PHS_RELIABLE:QA_MVD_PHS); break;
        default: return qa_net_writer_fail(w,"Invalid MVD recipient kind");
        }
        c.extra=(uint8_t)(n>>8);
        if (!qa_q2_mvd_write_command(w,c) || !qa_net_write_u8(w,(uint8_t)n)) return false;
        if (v->data.route.recipient==QA_MVD_RECIPIENT_PLAYER) qa_net_write_u8(w,(uint8_t)v->data.route.target);
        else if (v->data.route.recipient!=QA_MVD_RECIPIENT_ALL) qa_net_write_u16(w,v->data.route.target);
        return qa_net_write_data(w,v->data.route.payload.data,n);
    }
    if (c.opcode==QA_MVD_SOUND) c.extra=v->data.sound.global?1:0;
    if (!qa_q2_mvd_write_command(w,c)) return false;
    switch (c.opcode) {
    case QA_MVD_NOP: case QA_MVD_DISCONNECT: case QA_MVD_RECONNECT: return true;
    case QA_MVD_CONFIGSTRING:
        if (v->data.config.index>=p->max_configstrings) return qa_net_writer_fail(w,"MVD config index out of range");
        return qa_net_write_u16(w,v->data.config.index) && mvd_write_text(w,v->data.config.text);
    case QA_MVD_PRINT: return qa_net_write_u8(w,v->data.print.level) && mvd_write_text(w,v->data.print.text);
    case QA_MVD_STUFFTEXT: return mvd_write_text(w,v->data.text);
    case QA_MVD_SOUND: {
        uint8_t flags=v->data.sound.flags;
        if (v->data.sound.entity>=p->max_entities || v->data.sound.channel>7 || (!(flags&32) && v->data.sound.index>255)) return qa_net_writer_fail(w,"Invalid MVD sound");
        qa_net_write_u8(w,flags);
        if (flags&32) qa_net_write_u16(w,v->data.sound.index); else qa_net_write_u8(w,(uint8_t)v->data.sound.index);
        if (flags&1) qa_q2_write_scaled(w,v->data.sound.volume,255.0f,8,false);
        if (flags&2) qa_q2_write_scaled(w,v->data.sound.attenuation,64.0f,8,false);
        if (flags&16) qa_q2_write_scaled(w,v->data.sound.delay_seconds,1000.0f,8,false);
        return qa_net_write_u16(w,(uint16_t)(((uint32_t)v->data.sound.entity<<3)|v->data.sound.channel));
    }
    default: return qa_net_writer_fail(w,"MVD record requires specialized state encoder");
    }
}

struct qa_q2_mvd_framer {
    uint8_t *buffer;
    size_t limit, used, need;
    bool identified, finished, failed;
};
bool qa_q2_mvd_framer_create(bool magic, size_t limit, qa_q2_mvd_framer **out, qa_error *error)
{
    if (!out || !limit || limit>QA_Q2_MVD_MESSAGE_BYTES) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid MVD framing limit"); return false;
    }
    qa_q2_mvd_framer *f=calloc(1,sizeof(*f));
    if (f) f->buffer=malloc(limit+4);
    if (!f || !f->buffer) { free(f); qa_error_set(error,QA_ERROR_MEMORY,0,"MVD framer allocation failed"); return false; }
    f->limit=limit; f->identified=!magic; f->need=magic?4:2; *out=f; return true;
}
void qa_q2_mvd_framer_destroy(qa_q2_mvd_framer *f)
{ if (f) { free(f->buffer); free(f); } }
bool qa_q2_mvd_framer_push(qa_q2_mvd_framer *f, qa_bytes bytes, size_t *consumed,
                           bool *present, qa_bytes *message, qa_error *error)
{
    if (!f || !consumed || !present || !message || (bytes.size && !bytes.data)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid MVD framing input"); return false;
    }
    *consumed=0; *present=false; *message=(qa_bytes){0};
    if (f->failed || (f->finished && bytes.size)) goto malformed;
    while (*consumed<bytes.size) {
        size_t n=f->need-f->used, available=bytes.size-*consumed; if (n>available) n=available;
        memcpy(f->buffer+f->used,bytes.data+*consumed,n); f->used+=n; *consumed+=n;
        if (f->used<f->need) break;
        if (!f->identified) {
            if (qa_load_u32le(f->buffer)!=QA_Q2_MVD_MAGIC) goto malformed;
            f->identified=true; f->used=0; f->need=2; continue;
        }
        size_t length=qa_load_u16le(f->buffer);
        if (length>f->limit) goto malformed;
        if (!length) {
            f->finished=true; f->used=0;
            if (*consumed!=bytes.size) goto malformed;
            break;
        }
        if (f->need==2) { f->need=length+2; continue; }
        *present=true; *message=(qa_bytes){f->buffer+2,length}; f->used=0; f->need=2; return true;
    }
    return true;
malformed:
    f->failed=true; qa_error_set(error,QA_ERROR_FORMAT,*consumed,"Invalid MVD/GTV stream framing"); return false;
}
bool qa_q2_mvd_framer_identified(const qa_q2_mvd_framer *f) { return f && f->identified; }
bool qa_q2_mvd_framer_finished(const qa_q2_mvd_framer *f) { return f && f->finished; }
bool qa_q2_mvd_framer_finish(const qa_q2_mvd_framer *f, bool terminator, qa_error *error)
{
    if (!f || f->failed || !f->identified || f->used || (terminator && !f->finished)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Truncated MVD/GTV stream"); return false;
    }
    return true;
}
bool qa_q2_mvd_write_magic(qa_net_writer *w) { return qa_net_write_u32(w,QA_Q2_MVD_MAGIC); }
bool qa_q2_mvd_write_framed(qa_net_writer *w, qa_bytes bytes)
{
    if (!bytes.size || bytes.size>QA_Q2_MVD_MESSAGE_BYTES) return qa_net_writer_fail(w,"MVD frame length out of range");
    return qa_net_write_u16(w,(uint16_t)bytes.size) && qa_net_write_data(w,bytes.data,bytes.size);
}
bool qa_q2_mvd_write_terminator(qa_net_writer *w) { return qa_net_write_u16(w,0); }
