#include "q2pro_internal.h"

enum player_bits {
    M_TYPE=1, M_ORIGIN=2, M_VELOCITY=4, M_TIME=8, M_FLAGS=16,
    M_GRAVITY=32, M_DELTA_ANGLES=64, VIEW_OFFSET=128, VIEW_ANGLES=256,
    KICK_ANGLES=512, BLEND=1024, FOV=2048, WEAPON_INDEX=4096,
    WEAPON_FRAME=8192, RD_FLAGS=16384, MORE_FLAGS=32768, FOG=65536
};
enum extra_bits { GUN_OFFSET=1, GUN_ANGLES=2, VELOCITY_Z=4, ORIGIN_Z=8, VIEW_ANGLE_Z=16, STATS=32, CLIENT_NUMBER=64 };

static bool valid_revision(uint32_t revision) { return revision>=1015 && revision<=1026 && revision!=1016; }
static bool read_serverdata(qa_q2_codec *c,qa_net_reader *r,qa_q2_serverdata *out) {
    if (!out) return qa_net_reader_fail(r,"Missing Q2PRO serverdata output");
    qa_q2_serverdata d={0};
    d.servercount=qa_net_read_i32(r); d.attractloop=qa_net_read_u8(r)!=0;
    qa_net_read_string(r,d.gamedir,sizeof(d.gamedir)); d.clientnum=qa_net_read_i16(r);
    qa_net_read_string(r,d.levelname,sizeof(d.levelname));
    d.protocol_revision=qa_net_read_u16(r);
    if (!d.protocol_revision) d.protocol_revision=c->protocol.revision;
    if (!valid_revision(d.protocol_revision)) return qa_net_reader_fail(r,"Unsupported Q2PRO server revision");
    d.server_state=qa_net_read_u8(r);
    if (d.protocol_revision>=1024) d.wire_flags=qa_net_read_u16(r);
    else {
        if (qa_net_read_u8(r)) d.wire_flags|=1;
        if (qa_net_read_u8(r)) d.wire_flags|=2;
        if (qa_net_read_u8(r)) d.wire_flags|=4;
    }
    if (r->failed) return false;
    d.strafejump_hack=(d.wire_flags&1u)!=0; d.qw_mode=(d.wire_flags&2u)!=0; d.waterjump_hack=(d.wire_flags&4u)!=0;
    d.client_count=1; d.clientnums[0]=d.clientnum;
    c->protocol.revision=d.protocol_revision; c->wire_flags=d.wire_flags; c->protocol.flags=d.wire_flags;
    c->frame_extra=0; c->frame_player_pending=false; *out=d; return true;
}
static bool write_serverdata(qa_q2_codec *c,qa_net_writer *w,const qa_q2_serverdata *d) {
    if (!d) return qa_net_writer_fail(w,"Missing Q2PRO serverdata");
    uint32_t revision=d->protocol_revision?d->protocol_revision:c->protocol.revision;
    uint32_t flags=d->wire_flags?d->wire_flags:((d->strafejump_hack?1u:0u)|(d->qw_mode?2u:0u)|(d->waterjump_hack?4u:0u));
    if (d->client_count>1) return qa_net_writer_fail(w,"Q2PRO cannot represent split player serverdata");
    if (!valid_revision(revision) || flags>(revision>=1024?65535u:7u) || d->server_state>255 || d->clientnum<INT16_MIN || d->clientnum>INT16_MAX ||
        !memchr(d->gamedir,0,sizeof(d->gamedir)) || !memchr(d->levelname,0,sizeof(d->levelname)))
        return qa_net_writer_fail(w,"Invalid Q2PRO serverdata fields");
    qa_net_write_u8(w,12); qa_net_write_u32(w,36); qa_net_write_i32(w,d->servercount); qa_net_write_u8(w,d->attractloop?1:0);
    qa_net_write_string(w,d->gamedir); qa_net_write_i16(w,(int16_t)d->clientnum); qa_net_write_string(w,d->levelname);
    qa_net_write_u16(w,(uint16_t)revision); qa_net_write_u8(w,(uint8_t)d->server_state);
    if (revision>=1024) qa_net_write_u16(w,(uint16_t)flags);
    else { qa_net_write_u8(w,(flags&1u)?1:0); qa_net_write_u8(w,(flags&2u)?1:0); qa_net_write_u8(w,(flags&4u)?1:0); }
    if (w->failed) return false;
    c->protocol.revision=revision; c->protocol.flags=flags; c->wire_flags=flags; c->frame_extra=0; c->frame_player_pending=false;
    return true;
}

typedef struct player_encoding { uint32_t flags; uint8_t extra,blend,fog; uint64_t stats; } player_encoding;
static bool differs(const float *a,const float *b,unsigned count) {
    for (unsigned i=0;i<count;++i) if (a[i]!=b[i]) return true;
    return false;
}
static bool encode_player(qa_q2_codec *c,qa_net_writer *w,const qa_q2_player *f,const qa_q2_player *t,player_encoding *out) {
    if (!f || !t) return qa_net_writer_fail(w,"Missing Q2PRO player state");
    bool extended=qa_q2pro_extensions(c),v2=qa_q2pro_extensions_v2(c);
    player_encoding e={0};
    if (t->pmove.type<0 || t->pmove.type>255 || t->pmove.time<0 || t->pmove.time>(v2?65535:255) ||
        t->pmove.flags<0 || t->pmove.flags>(v2?65535:255) || t->pmove.gravity<INT16_MIN || t->pmove.gravity>INT16_MAX ||
        t->gunindex>(extended?8191u:255u) || t->gunskin>(extended?7u:0u) || t->gunframe>255 || t->rdflags>255 ||
        t->clientnum<(c->protocol.revision>=1022?INT16_MIN:0) || t->clientnum>(c->protocol.revision>=1022?INT16_MAX:255))
        return qa_net_writer_fail(w,"Q2PRO player fields exceed negotiated ranges");
    if (t->pmove.viewheight || t->gunrate || t->team_id) return qa_net_writer_fail(w,"Q2PRO cannot represent rerelease player fields");
    if (!v2) {
        for (unsigned i=32;i<QA_Q2_MAX_STATS;++i) if (t->stats[i]) return qa_net_writer_fail(w,"Q2PRO extra stats require extension v2");
        for (unsigned i=0;i<4;++i) if (t->damage_blend[i]!=0.0f) return qa_net_writer_fail(w,"Q2PRO damage blend requires extension v2");
    }
    if (t->pmove.float_delta_angles) return qa_net_writer_fail(w,"Q2PRO requires short delta angles");
    for (unsigned i=0;i<3;++i) {
        int32_t lo=v2?-4194304:INT16_MIN,hi=v2?4194303:INT16_MAX;
        if (t->pmove.origin[i]<lo || t->pmove.origin[i]>hi || t->pmove.velocity[i]<lo || t->pmove.velocity[i]>hi)
            return qa_net_writer_fail(w,"Q2PRO player movement outside coordinate range");
    }
    if (f->pmove.type!=t->pmove.type) e.flags|=M_TYPE;
    if (f->pmove.origin[0]!=t->pmove.origin[0] || f->pmove.origin[1]!=t->pmove.origin[1]) e.flags|=M_ORIGIN;
    if (f->pmove.origin[2]!=t->pmove.origin[2]) e.extra|=ORIGIN_Z;
    if (f->pmove.velocity[0]!=t->pmove.velocity[0] || f->pmove.velocity[1]!=t->pmove.velocity[1]) e.flags|=M_VELOCITY;
    if (f->pmove.velocity[2]!=t->pmove.velocity[2]) e.extra|=VELOCITY_Z;
    if (f->pmove.time!=t->pmove.time) e.flags|=M_TIME;
    if (f->pmove.flags!=t->pmove.flags) e.flags|=M_FLAGS;
    if (f->pmove.gravity!=t->pmove.gravity) e.flags|=M_GRAVITY;
    for (unsigned i=0;i<3;++i) if (f->pmove.delta_angles[i]!=t->pmove.delta_angles[i]) e.flags|=M_DELTA_ANGLES;
    if (differs(f->viewoffset,t->viewoffset,3)) e.flags|=VIEW_OFFSET;
    if (differs(f->viewangles,t->viewangles,2)) e.flags|=VIEW_ANGLES;
    if (f->viewangles[2]!=t->viewangles[2]) e.extra|=VIEW_ANGLE_Z;
    if (differs(f->kick_angles,t->kick_angles,3)) e.flags|=KICK_ANGLES;
    if (f->gunindex!=t->gunindex || f->gunskin!=t->gunskin) e.flags|=WEAPON_INDEX;
    if (f->gunframe!=t->gunframe) e.flags|=WEAPON_FRAME;
    if (differs(f->gunoffset,t->gunoffset,3)) e.extra|=GUN_OFFSET;
    if (differs(f->gunangles,t->gunangles,3)) e.extra|=GUN_ANGLES;
    if (f->fov!=t->fov) e.flags|=FOV;
    if (f->rdflags!=t->rdflags) e.flags|=RD_FLAGS;
    if (f->clientnum!=t->clientnum) e.extra|=CLIENT_NUMBER;
    for (unsigned i=0;i<4;++i) {
        if (f->blend[i]!=t->blend[i]) { e.blend|=(uint8_t)(1u<<i); e.flags|=BLEND; }
        if (f->damage_blend[i]!=t->damage_blend[i]) {
            if (!v2) return qa_net_writer_fail(w,"Q2PRO damage blend requires extension v2");
            e.blend|=(uint8_t)(16u<<i); e.flags|=BLEND;
        }
    }
    for (unsigned i=0;i<QA_Q2_MAX_STATS;++i) if (f->stats[i]!=t->stats[i]) {
        if (!v2 && i>=32) return qa_net_writer_fail(w,"Q2PRO extra stats require extension v2");
        e.stats|=UINT64_C(1)<<i;
    }
    if (e.stats) e.extra|=STATS;
    e.fog=qa_q2pro_fog_bits(&f->fog,&t->fog);
    if (e.fog) {
        if (c->protocol.revision<1026) return qa_net_writer_fail(w,"Q2PRO fog requires revision 1026");
        e.flags|=FOG|MORE_FLAGS;
    }
    *out=e; return true;
}
static bool write_movement(qa_net_writer *w,int32_t current,int32_t previous,bool v2) {
    return v2?qa_q2pro_write_int23(w,current,previous):qa_net_write_i16(w,(int16_t)current);
}
static bool write_quarter_vector(qa_net_writer *w,const float v[3]) {
    for (unsigned i=0;i<3;++i) if (!qa_q2_write_scaled(w,v[i],4,8,true)) return false;
    return true;
}
static bool write_player_body(qa_q2_codec *c,qa_net_writer *w,const qa_q2_player *f,const qa_q2_player *t,player_encoding e) {
    bool extended=qa_q2pro_extensions(c),v2=qa_q2pro_extensions_v2(c);
    if (e.flags&M_TYPE) qa_net_write_u8(w,(uint8_t)t->pmove.type);
    if (e.flags&M_ORIGIN) for (unsigned i=0;i<2;++i) write_movement(w,t->pmove.origin[i],f->pmove.origin[i],v2);
    if (e.extra&ORIGIN_Z) write_movement(w,t->pmove.origin[2],f->pmove.origin[2],v2);
    if (e.flags&M_VELOCITY) for (unsigned i=0;i<2;++i) write_movement(w,t->pmove.velocity[i],f->pmove.velocity[i],v2);
    if (e.extra&VELOCITY_Z) write_movement(w,t->pmove.velocity[2],f->pmove.velocity[2],v2);
    if (e.flags&M_TIME) { if (v2) qa_net_write_u16(w,(uint16_t)t->pmove.time); else qa_net_write_u8(w,(uint8_t)t->pmove.time); }
    if (e.flags&M_FLAGS) { if (v2) qa_net_write_u16(w,(uint16_t)t->pmove.flags); else qa_net_write_u8(w,(uint8_t)t->pmove.flags); }
    if (e.flags&M_GRAVITY) qa_net_write_i16(w,(int16_t)t->pmove.gravity);
    if (e.flags&M_DELTA_ANGLES) for (unsigned i=0;i<3;++i) qa_net_write_i16(w,t->pmove.delta_angles[i]);
    if (e.flags&VIEW_OFFSET) write_quarter_vector(w,t->viewoffset);
    if (e.flags&VIEW_ANGLES) for (unsigned i=0;i<2;++i) qa_q2_write_angle16(w,t->viewangles[i]);
    if (e.extra&VIEW_ANGLE_Z) qa_q2_write_angle16(w,t->viewangles[2]);
    if (e.flags&KICK_ANGLES) write_quarter_vector(w,t->kick_angles);
    if (e.flags&WEAPON_INDEX) {
        if (extended) qa_net_write_u16(w,(uint16_t)(t->gunindex|(t->gunskin<<13))); else qa_net_write_u8(w,(uint8_t)t->gunindex);
    }
    if (e.flags&WEAPON_FRAME) qa_net_write_u8(w,(uint8_t)t->gunframe);
    if (e.extra&GUN_OFFSET) write_quarter_vector(w,t->gunoffset);
    if (e.extra&GUN_ANGLES) write_quarter_vector(w,t->gunangles);
    if (e.flags&BLEND) {
        if (v2) {
            qa_net_write_u8(w,e.blend);
            for (unsigned i=0;i<4;++i) if (e.blend&(1u<<i)) qa_q2_write_scaled(w,t->blend[i],255,8,false);
            for (unsigned i=0;i<4;++i) if (e.blend&(16u<<i)) qa_q2_write_scaled(w,t->damage_blend[i],255,8,false);
        } else for (unsigned i=0;i<4;++i) qa_q2_write_scaled(w,t->blend[i],255,8,false);
    }
    if (e.flags&FOG) qa_q2pro_write_fog(w,e.fog,&t->fog);
    if (e.flags&FOV) qa_q2_write_scaled(w,t->fov,1,8,false);
    if (e.flags&RD_FLAGS) qa_net_write_u8(w,(uint8_t)t->rdflags);
    if (e.extra&STATS) {
        if (v2) qa_q2pro_write_var64(w,e.stats); else qa_net_write_u32(w,(uint32_t)e.stats);
        for (unsigned i=0;i<(v2?64u:32u);++i) if (e.stats&(UINT64_C(1)<<i)) qa_net_write_i16(w,t->stats[i]);
    }
    if (e.extra&CLIENT_NUMBER) {
        if (c->protocol.revision>=1022) qa_net_write_i16(w,(int16_t)t->clientnum); else qa_net_write_u8(w,(uint8_t)t->clientnum);
    }
    return !w->failed;
}
static bool write_player_flags(qa_net_writer *w,player_encoding e) {
    return qa_net_write_u16(w,(uint16_t)e.flags) && (!(e.flags&MORE_FLAGS) || qa_net_write_u8(w,(uint8_t)(e.flags>>16)));
}
static bool write_player(qa_q2_codec *c,qa_net_writer *w,const qa_q2_player *f,const qa_q2_player *t) {
    player_encoding e;
    return encode_player(c,w,f,t,&e) && qa_net_write_u8(w,17) && write_player_flags(w,e) && qa_net_write_u8(w,e.extra) && write_player_body(c,w,f,t,e);
}
static void read_quarter_vector(qa_net_reader *r,float v[3]) {
    for (unsigned i=0;i<3;++i) v[i]=(float)qa_net_read_i8(r)*0.25f;
}
static bool read_movement(qa_net_reader *r,int32_t previous,bool v2,int32_t *out) {
    if (v2) return qa_q2pro_read_int23(r,previous,out);
    *out=qa_net_read_i16(r); return !r->failed;
}
static bool read_player_body(qa_q2_codec *c,qa_net_reader *r,const qa_q2_player *f,uint32_t flags,uint32_t extra,qa_q2_player *out) {
    if (!f || !out) return qa_net_reader_fail(r,"Missing Q2PRO player state");
    bool extended=qa_q2pro_extensions(c),v2=qa_q2pro_extensions_v2(c);
    if ((flags&~UINT32_C(0x1ffff)) || (extra&~UINT32_C(0x7f)) || ((flags&MORE_FLAGS) && c->protocol.revision<1026))
        return qa_net_reader_fail(r,"Unnegotiated Q2PRO player flags");
    qa_q2_player t=*f; t.pmove.float_delta_angles=false;
    if (flags&M_TYPE) t.pmove.type=qa_net_read_u8(r);
    if (flags&M_ORIGIN) for (unsigned i=0;i<2;++i) if (!read_movement(r,f->pmove.origin[i],v2,&t.pmove.origin[i])) return false;
    if ((extra&ORIGIN_Z) && !read_movement(r,f->pmove.origin[2],v2,&t.pmove.origin[2])) return false;
    if (flags&M_VELOCITY) for (unsigned i=0;i<2;++i) if (!read_movement(r,f->pmove.velocity[i],v2,&t.pmove.velocity[i])) return false;
    if ((extra&VELOCITY_Z) && !read_movement(r,f->pmove.velocity[2],v2,&t.pmove.velocity[2])) return false;
    if (flags&M_TIME) t.pmove.time=v2?qa_net_read_u16(r):qa_net_read_u8(r);
    if (flags&M_FLAGS) t.pmove.flags=v2?qa_net_read_u16(r):qa_net_read_u8(r);
    if (flags&M_GRAVITY) t.pmove.gravity=qa_net_read_i16(r);
    if (flags&M_DELTA_ANGLES) for (unsigned i=0;i<3;++i) t.pmove.delta_angles[i]=qa_net_read_i16(r);
    if (flags&VIEW_OFFSET) read_quarter_vector(r,t.viewoffset);
    if (flags&VIEW_ANGLES) for (unsigned i=0;i<2;++i) t.viewangles[i]=qa_q2_read_angle16(r);
    if (extra&VIEW_ANGLE_Z) t.viewangles[2]=qa_q2_read_angle16(r);
    if (flags&KICK_ANGLES) read_quarter_vector(r,t.kick_angles);
    if (flags&WEAPON_INDEX) {
        if (extended) { uint16_t gun=qa_net_read_u16(r); t.gunindex=gun&8191u; t.gunskin=gun>>13; }
        else t.gunindex=qa_net_read_u8(r);
    }
    if (flags&WEAPON_FRAME) t.gunframe=qa_net_read_u8(r);
    if (extra&GUN_OFFSET) read_quarter_vector(r,t.gunoffset);
    if (extra&GUN_ANGLES) read_quarter_vector(r,t.gunangles);
    if (flags&BLEND) {
        if (v2) {
            uint8_t bits=qa_net_read_u8(r);
            for (unsigned i=0;i<4;++i) if (bits&(1u<<i)) t.blend[i]=(float)qa_net_read_u8(r)/255.0f;
            for (unsigned i=0;i<4;++i) if (bits&(16u<<i)) t.damage_blend[i]=(float)qa_net_read_u8(r)/255.0f;
        } else for (unsigned i=0;i<4;++i) t.blend[i]=(float)qa_net_read_u8(r)/255.0f;
    }
    if ((flags&FOG) && !qa_q2pro_read_fog(r,&f->fog,&t.fog)) return false;
    if (flags&FOV) t.fov=qa_net_read_u8(r);
    if (flags&RD_FLAGS) t.rdflags=qa_net_read_u8(r);
    if (extra&STATS) {
        uint64_t statbits;
        if (v2) { if (!qa_q2pro_read_var64(r,&statbits)) return false; } else statbits=qa_net_read_u32(r);
        for (unsigned i=0;i<(v2?64u:32u);++i) if (statbits&(UINT64_C(1)<<i)) t.stats[i]=qa_net_read_i16(r);
    }
    if (extra&CLIENT_NUMBER) {
        t.clientnum=c->protocol.revision>=1022?qa_net_read_i16(r):qa_net_read_u8(r);
    }
    if (r->failed) return false;
    *out=t; return true;
}
static bool read_player(qa_q2_codec *c,qa_net_reader *r,const qa_q2_player *f,qa_q2_player *t) {
    bool in_frame=c->frame_player_pending; uint32_t extra=c->frame_extra;
    c->frame_player_pending=false; c->frame_extra=0;
    uint32_t flags=qa_net_read_u16(r);
    if (flags&MORE_FLAGS) {
        if (c->protocol.revision<1026) return qa_net_reader_fail(r,"Q2PRO player flag extension requires revision 1026");
        flags|=(uint32_t)qa_net_read_u8(r)<<16;
    }
    if (!in_frame) extra=qa_net_read_u8(r);
    return read_player_body(c,r,f,flags,extra,t);
}
static bool read_frame_header(qa_q2_codec *c,qa_net_reader *r,qa_q2_frame_header *out) {
    if (!out) return qa_net_reader_fail(r,"Missing Q2PRO frame header");
    uint32_t encoded=qa_net_read_u32(r),offset=encoded>>27;
    qa_q2_frame_header h={0};
    h.serverframe=(int32_t)(encoded&UINT32_C(0x07ffffff)); h.deltaframe=offset==31?-1:h.serverframe-(int32_t)offset;
    uint8_t suppress=qa_net_read_u8(r);
    uint32_t extra=((c->frame_extra>>1)&0x70u)|(suppress>>4);
    c->frame_extra=0; c->frame_player_pending=false;
    h.suppress_count=suppress&15u; h.areabytes=qa_net_read_u8(r);
    if (!qa_net_read_data(r,h.areabits,h.areabytes)) return false;
    c->frame_extra=extra; c->frame_player_pending=true; *out=h; return true;
}
static bool write_frame(qa_q2_codec *c,qa_net_writer *w,const qa_q2_frame_header *h,const qa_q2_player *f,const qa_q2_player *t,qa_q2_write_entities_fn entities,void *context) {
    if (!h || !entities || h->serverframe<0 || h->serverframe>0x07ffffff || h->areabytes>255 || h->suppress_count>15)
        return qa_net_writer_fail(w,"Q2PRO frame outside wire range");
    int64_t offset=h->deltaframe==-1?31:(int64_t)h->serverframe-h->deltaframe;
    if (h->deltaframe< -1 || offset<0 || offset>31 || (h->deltaframe!=-1 && offset==31))
        return qa_net_writer_fail(w,"Q2PRO delta frame exceeds window");
    qa_q2_player zero={0}; player_encoding e;
    if (!f) f=&zero;
    if (!encode_player(c,w,f,t,&e)) return false;
    qa_net_write_u8(w,(uint8_t)(20u|((e.extra&0x70u)<<1)));
    qa_net_write_u32(w,(uint32_t)h->serverframe|((uint32_t)offset<<27));
    qa_net_write_u8(w,(uint8_t)(h->suppress_count|((e.extra&15u)<<4)));
    qa_net_write_u8(w,(uint8_t)h->areabytes); qa_net_write_data(w,h->areabits,h->areabytes);
    if (!write_player_flags(w,e) || !write_player_body(c,w,f,t,e)) return false;
    if (!entities(context,w,w->error)) return qa_net_writer_fail(w,"Writing Q2PRO packet entities failed");
    return !w->failed;
}

static int16_t wrap_short(int32_t value) {
    uint16_t word=(uint16_t)(uint32_t)value;
    return (int16_t)(word<=32767u?(int32_t)word:(int32_t)word-65536);
}
static bool read_batch_command(qa_net_reader *r,const qa_q2_usercmd *from,qa_q2_usercmd *out) {
    qa_q2_usercmd t=*from;
    if (qa_net_read_bits(r,1)) {
        uint32_t bits=qa_net_read_bits(r,8);
        for (unsigned i=0;i<2;++i) if (bits&(1u<<i)) {
            bool delta=qa_net_read_bits(r,1)!=0;
            int32_t value=qa_net_read_sbits(r,delta?8u:16u);
            t.angles[i]=wrap_short(delta?(int32_t)from->angles[i]+value:value);
        }
        if (bits&4u) t.angles[2]=(int16_t)qa_net_read_sbits(r,16);
        if (bits&8u) t.forwardmove=(int16_t)qa_net_read_sbits(r,10);
        if (bits&16u) t.sidemove=(int16_t)qa_net_read_sbits(r,10);
        if (bits&32u) t.upmove=(int16_t)qa_net_read_sbits(r,10);
        if (bits&64u) { uint32_t buttons=qa_net_read_bits(r,3); t.buttons=(uint8_t)((buttons&3u)|((buttons&4u)<<5)); }
        if (bits&128u) t.msec=(uint8_t)qa_net_read_bits(r,8);
    }
    if (r->failed) return false;
    *out=t; return true;
}
static bool valid_batch(const qa_q2pro_batch *batch) {
    if (!batch || !batch->frame_count || batch->frame_count>QA_Q2PRO_BATCH_FRAMES) return false;
    for (size_t f=0;f<batch->frame_count;++f) {
        if (batch->frames[f].count>QA_Q2PRO_BATCH_COMMANDS) return false;
        for (size_t i=0;i<batch->frames[f].count;++i) {
            const qa_q2_usercmd *c=&batch->frames[f].commands[i];
            if (!isfinite(c->forwardmove) || !isfinite(c->sidemove) || !isfinite(c->upmove) ||
                truncf(c->forwardmove)!=c->forwardmove || truncf(c->sidemove)!=c->sidemove || truncf(c->upmove)!=c->upmove ||
                c->forwardmove< -512 || c->forwardmove>511 || c->sidemove< -512 || c->sidemove>511 || c->upmove< -512 || c->upmove>511 ||
                (c->buttons&~0x83u) || c->impulse || c->server_frame) return false;
        }
    }
    return true;
}
static bool write_batch_command(qa_net_writer *w,const qa_q2_usercmd *from,const qa_q2_usercmd *to) {
    uint32_t bits=0;
    for (unsigned i=0;i<3;++i) if (from->angles[i]!=to->angles[i]) bits|=1u<<i;
    if (from->forwardmove!=to->forwardmove) bits|=8;
    if (from->sidemove!=to->sidemove) bits|=16;
    if (from->upmove!=to->upmove) bits|=32;
    if (from->buttons!=to->buttons) bits|=64;
    if (from->msec!=to->msec) bits|=128;
    qa_net_write_bits(w,bits?1:0,1);
    if (!bits) return !w->failed;
    qa_net_write_bits(w,bits,8);
    for (unsigned i=0;i<2;++i) if (bits&(1u<<i)) {
        int32_t delta=(int32_t)to->angles[i]-from->angles[i]; bool small=delta>= -128 && delta<=127;
        qa_net_write_bits(w,small?1:0,1); qa_net_write_bits(w,(uint32_t)(small?delta:to->angles[i]),small?8u:16u);
    }
    if (bits&4u) qa_net_write_bits(w,(uint32_t)(int32_t)to->angles[2],16);
    if (bits&8u) qa_net_write_bits(w,(uint32_t)(int32_t)to->forwardmove,10);
    if (bits&16u) qa_net_write_bits(w,(uint32_t)(int32_t)to->sidemove,10);
    if (bits&32u) qa_net_write_bits(w,(uint32_t)(int32_t)to->upmove,10);
    if (bits&64u) qa_net_write_bits(w,(to->buttons&3u)|((to->buttons>>5)&4u),3);
    if (bits&128u) qa_net_write_bits(w,to->msec,8);
    return !w->failed;
}
bool qa_q2pro_read_batch(qa_q2_codec *c,qa_net_reader *r,bool nodelta,uint8_t opcode_extra,qa_q2pro_batch *out) {
    if (!c || c->protocol.kind!=QA_NET_Q2PRO_36 || !out || opcode_extra>=QA_Q2PRO_BATCH_FRAMES || (r->bit&7u))
        return qa_net_reader_fail(r,"Invalid Q2PRO command batch");
    qa_q2pro_batch batch={.nodelta=nodelta,.lastframe=-1,.frame_count=(size_t)opcode_extra+1};
    if (!nodelta) batch.lastframe=qa_net_read_i32(r);
    batch.lightlevel=qa_net_read_u8(r);
    qa_q2_usercmd previous={0};
    for (size_t f=0;f<batch.frame_count;++f) {
        qa_q2pro_batch_frame *frame=&batch.frames[f]; frame->count=qa_net_read_bits(r,5);
        for (size_t i=0;i<frame->count;++i) {
            if (!read_batch_command(r,&previous,&frame->commands[i])) return false;
            previous=frame->commands[i];
        }
    }
    if (r->failed) return false;
    r->bit=(r->bit+7u)&~(size_t)7u; *out=batch; return true;
}
bool qa_q2pro_write_batch(qa_q2_codec *c,qa_net_writer *w,const qa_q2pro_batch *batch) {
    if (!c || c->protocol.kind!=QA_NET_Q2PRO_36 || !valid_batch(batch) || (w->bit&7u)) return qa_net_writer_fail(w,"Invalid Q2PRO command batch");
    if (!batch->nodelta) qa_net_write_i32(w,batch->lastframe);
    qa_net_write_u8(w,batch->lightlevel);
    qa_q2_usercmd previous={0};
    for (size_t f=0;f<batch->frame_count;++f) {
        const qa_q2pro_batch_frame *frame=&batch->frames[f]; qa_net_write_bits(w,(uint32_t)frame->count,5);
        for (size_t i=0;i<frame->count;++i) {
            if (!write_batch_command(w,&previous,&frame->commands[i])) return false;
            previous=frame->commands[i];
        }
    }
    unsigned padding=(unsigned)((8u-(w->bit&7u))&7u);
    return (!padding || qa_net_write_bits(w,0,padding)) && !w->failed;
}
bool qa_q2pro_write_batch_message(qa_q2_codec *c,qa_net_writer *w,const qa_q2pro_batch *batch) {
    if (!valid_batch(batch)) return qa_net_writer_fail(w,"Invalid Q2PRO command batch");
    uint8_t opcode=(uint8_t)((batch->nodelta?10u:11u)|((batch->frame_count-1u)<<5));
    return qa_net_write_u8(w,opcode) && qa_q2pro_write_batch(c,w,batch);
}
bool qa_q2pro_read_userinfo_delta(qa_net_reader *r,char *name,size_t name_capacity,char *value,size_t value_capacity) {
    return qa_net_read_string(r,name,name_capacity) && qa_net_read_string(r,value,value_capacity);
}
bool qa_q2pro_write_userinfo_delta(qa_net_writer *w,const char *name,const char *value) {
    return qa_net_write_string(w,name) && qa_net_write_string(w,value);
}
bool qa_q2pro_read_client_setting(qa_net_reader *r,int16_t *index,int16_t *value) {
    if (!index || !value) return qa_net_reader_fail(r,"Missing Q2PRO client setting output");
    int16_t i=qa_net_read_i16(r),v=qa_net_read_i16(r);
    if (r->failed) return false;
    *index=i; *value=v; return true;
}
bool qa_q2pro_write_client_setting(qa_net_writer *w,int16_t index,int16_t value) { return qa_net_write_i16(w,index) && qa_net_write_i16(w,value); }

const qa_q2_codec_ops qa_q2_q2pro_ops={
    read_serverdata,write_serverdata,qa_q2pro_entity_header,qa_q2pro_entity_read,qa_q2pro_entity_write,qa_q2pro_entity_remove,
    read_player,write_player,read_frame_header,write_frame,qa_q2_classic_read_usercmd,qa_q2_classic_write_usercmd
};
