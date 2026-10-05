#include "kex_game_internal.h"
#include "qa/network_q2_kex_game.h"

static const qa_q2_entity zero_entity;
static const qa_q2_player zero_player;
static const qa_q2_usercmd zero_usercmd;

bool qa_q2_wide_read_header(qa_q2_codec *c,qa_net_reader *r,uint32_t *number,uint64_t *bits) {
    if(!number||!bits)return qa_net_reader_fail(r,"Missing Q2 entity header output");
    uint64_t b=qa_net_read_u8(r);
    if(b&Q2_U_MORE1)b|=(uint64_t)qa_net_read_u8(r)<<8;
    if(b&Q2_U_MORE2)b|=(uint64_t)qa_net_read_u8(r)<<16;
    if(b&Q2_U_MORE3)b|=(uint64_t)qa_net_read_u8(r)<<24;
    if(b&Q2_U_MORE4)b|=(uint64_t)qa_net_read_u8(r)<<32;
    *number=(b&Q2_U_NUMBER16)?qa_net_read_u16(r):qa_net_read_u8(r);
    *bits=b;
    bool kex=c->protocol.kind==QA_NET_Q2KEX_2023||c->protocol.kind==QA_NET_Q2KEX_DEMO_2022;
    uint64_t high=b>>32;
    if((kex&&((b&Q2_U_ANGLE16)||(high>15&&high!=255)))||(!kex&&high>3))
        return qa_net_reader_fail(r,"Unknown Q2 rerelease entity flags");
    if(*number==0&&b)return qa_net_reader_fail(r,"Q2 entity terminator has flags");
    return !r->failed;
}
bool qa_q2_wide_write_header(qa_q2_codec *c,qa_net_writer *w,uint32_t n,uint64_t b) {
    (void)c;
    if(n>UINT16_MAX||b>>40||(!n&&b))return qa_net_writer_fail(w,"Q2 rerelease entity header outside wire range");
    if(n>255)b|=Q2_U_NUMBER16;
    if(b>>32)b|=Q2_U_MORE4|Q2_U_MORE3|Q2_U_MORE2|Q2_U_MORE1;
    else if(b&UINT64_C(0xff000000))b|=Q2_U_MORE3|Q2_U_MORE2|Q2_U_MORE1;
    else if(b&UINT64_C(0x00ff0000))b|=Q2_U_MORE2|Q2_U_MORE1;
    else if(b&UINT64_C(0x0000ff00))b|=Q2_U_MORE1;
    qa_net_write_u8(w,(uint8_t)b);
    if(b&Q2_U_MORE1)qa_net_write_u8(w,(uint8_t)(b>>8));
    if(b&Q2_U_MORE2)qa_net_write_u8(w,(uint8_t)(b>>16));
    if(b&Q2_U_MORE3)qa_net_write_u8(w,(uint8_t)(b>>24));
    if(b&Q2_U_MORE4)qa_net_write_u8(w,(uint8_t)(b>>32));
    if(b&Q2_U_NUMBER16)qa_net_write_u16(w,(uint16_t)n);else qa_net_write_u8(w,(uint8_t)n);
    return !w->failed;
}
bool qa_q2_wide_remove(qa_q2_codec *c,qa_net_writer *w,uint32_t n) {
    if(!n)return qa_net_writer_fail(w,"Cannot remove Q2 entity zero");
    return qa_q2_wide_write_header(c,w,n,Q2_U_REMOVE);
}
static const uint64_t origin_bits[3]={Q2_U_ORIGIN1,Q2_U_ORIGIN2,Q2_U_ORIGIN3};
static const uint64_t angle_bits[3]={Q2_U_ANGLE1,Q2_U_ANGLE2,Q2_U_ANGLE3};
static const uint64_t model_bits[4]={Q2_U_MODEL,Q2_U_MODEL2,Q2_U_MODEL3,Q2_U_MODEL4};

bool qa_q2_extended_read_entity(qa_q2_codec *c,qa_net_reader *r,const qa_q2_entity *f,
                               uint32_t n,uint64_t b,qa_q2_entity *t,bool kex) {
    if(!t||!n||n>UINT16_MAX||(b&Q2_U_REMOVE))return qa_net_reader_fail(r,"Invalid Q2 rerelease entity delta");
    uint64_t high=b>>32;
    if((kex&&((b&Q2_U_ANGLE16)||(high>15&&high!=255)))||(!kex&&high>3))
        return qa_net_reader_fail(r,"Unknown Q2 rerelease entity flags");
    if(!f)f=&zero_entity;
    *t=*f;t->number=n;
    if(!kex)memcpy(t->old_origin,f->origin,sizeof(t->old_origin));
    uint32_t *models[4]={&t->modelindex,&t->modelindex2,&t->modelindex3,&t->modelindex4};
    for(unsigned i=0;i<4;i++)if(b&model_bits[i])*models[i]=(b&Q2_U_MODEL16)?qa_net_read_u16(r):qa_net_read_u8(r);
    if(kex&&(b&Q2_U_FRAME8))t->frame=qa_net_read_u8(r);
    else if(b&Q2_U_FRAME16)t->frame=qa_net_read_u16(r);
    else if(b&Q2_U_FRAME8)t->frame=qa_net_read_u8(r);
    t->skinnum=q2_read_width(r,t->skinnum,b,Q2_U_SKIN8,Q2_U_SKIN16);
    if(kex&&(b&Q2_U_MOREFX8)){
        t->effects=qa_net_read_u32(r);
        t->morefx=q2_read_width(r,0,b,Q2_U_EFFECTS8,Q2_U_EFFECTS16);
    }else if(b&(Q2_U_EFFECTS8|Q2_U_EFFECTS16)){
        t->effects=q2_read_width(r,0,b,Q2_U_EFFECTS8,Q2_U_EFFECTS16);
        if(kex)t->morefx=0;
    }
    t->renderfx=q2_read_width(r,t->renderfx,b,Q2_U_RENDER8,Q2_U_RENDER16);
    bool floating=true;
    if(kex){
        if(b&Q2_U_SOLID){
            t->solid=qa_net_read_u32(r);
            uint8_t mask=(uint8_t)(1u<<(n&7u));
            if(t->solid)c->kex_nonzero_solid[n>>3]|=mask;
            else c->kex_nonzero_solid[n>>3]&=(uint8_t)~mask;
        }
        floating=c->protocol.kind!=QA_NET_Q2KEX_DEMO_2022||
            (c->kex_nonzero_solid[n>>3]&(uint8_t)(1u<<(n&7u)))!=0;
    }
    for(unsigned i=0;i<3;i++)if(b&origin_bits[i])t->origin[i]=floating?q2_read_float(r):qa_q2_read_coord(r);
    if(kex&&(b&Q2_U_OLDORIGIN))qa_q2_read_vec3(r,t->old_origin,floating);
    for(unsigned i=0;i<3;i++)if(b&angle_bits[i]){
        if(kex)t->angles[i]=q2_read_float(r);
        else if(b&Q2_U_ANGLE16)t->angles[i]=qa_q2_read_angle16(r);
        else t->angles[i]=(float)qa_net_read_u8(r)*(360.0f/256.0f);
    }
    if(!kex&&(b&Q2_U_OLDORIGIN))qa_q2_read_vec3(r,t->old_origin,true);
    if(b&Q2_U_SOUND){
        uint16_t sound=qa_net_read_u16(r);t->sound=sound&16383u;
        if(sound&16384u)t->loop_volume=(float)qa_net_read_u8(r)/255.0f;
        if(sound&32768u){uint8_t v=qa_net_read_u8(r);t->loop_attenuation=v==192?-1.0f:(float)v/64.0f;}
    }
    t->event=(b&Q2_U_EVENT)?qa_net_read_u8(r):0;
    if(!kex){
        if(b&Q2_U_SOLID)t->solid=qa_net_read_u32(r);
        t->morefx=q2_read_width(r,t->morefx,b,Q2_U_MOREFX8,Q2_U_MOREFX16);
    }
    if(b&Q2_U_ALPHA)t->alpha=(float)qa_net_read_u8(r)/255.0f;
    if(b&Q2_U_SCALE)t->scale=(float)qa_net_read_u8(r)/16.0f;
    if(kex){
        if(b&Q2_U_MOREFX16)t->instance_bits=qa_net_read_u8(r);
        if(b&Q2_U_OWNER)t->owner=qa_net_read_u16(r);
        if(b&Q2_U_OLDFRAME)t->old_frame=qa_net_read_u16(r);
    }
    return !r->failed;
}

bool qa_q2_extended_write_entity(qa_q2_codec *c,qa_net_writer *w,const qa_q2_entity *f,
                                const qa_q2_entity *t,bool force,bool fresh,bool kex) {
    if(!t||!t->number||t->number>UINT16_MAX)return qa_net_writer_fail(w,"Invalid Q2 rerelease entity number");
    if(!f)f=&zero_entity;
    if(!kex&&(t->instance_bits||t->owner||t->old_frame))
        return qa_net_writer_fail(w,"Q2repro entity cannot carry KEX instance, owner or old-frame fields");
    if(!q2_finite_array(t->origin,3)||!q2_finite_array(t->angles,3)||!q2_finite_array(t->old_origin,3)||
       !isfinite(t->alpha)||!isfinite(t->scale)||!isfinite(t->loop_volume)||!isfinite(t->loop_attenuation))
        return qa_net_writer_fail(w,"Nonfinite Q2 rerelease entity state");
    if(t->effects>UINT32_MAX||t->frame>UINT16_MAX||t->sound>16383||t->event>255||
       t->modelindex>UINT16_MAX||t->modelindex2>UINT16_MAX||t->modelindex3>UINT16_MAX||t->modelindex4>UINT16_MAX||
       (kex&&(t->instance_bits>255||t->owner>UINT16_MAX||t->old_frame>UINT16_MAX)))
        return qa_net_writer_fail(w,"Q2 rerelease entity state outside wire range");
    uint64_t b=0;
    for(unsigned i=0;i<3;i++){
        if(t->origin[i]!=f->origin[i])b|=origin_bits[i];
        if(kex?t->angles[i]!=f->angles[i]:qa_angle_to_word(t->angles[i])!=qa_angle_to_word(f->angles[i]))b|=angle_bits[i];
    }
    if(!kex&&(b&(Q2_U_ANGLE1|Q2_U_ANGLE2|Q2_U_ANGLE3)))b|=Q2_U_ANGLE16;
    if(t->frame!=f->frame)b|=t->frame>255?Q2_U_FRAME16:Q2_U_FRAME8;
    if(t->skinnum!=f->skinnum)b|=q2_width(t->skinnum,Q2_U_SKIN8,Q2_U_SKIN16,!kex);
    if(kex){
        if(t->effects!=f->effects||t->morefx!=f->morefx){
            if(t->morefx)b|=Q2_U_MOREFX8|q2_width(t->morefx,Q2_U_EFFECTS8,Q2_U_EFFECTS16,false);
            else b|=q2_width((uint32_t)t->effects,Q2_U_EFFECTS8,Q2_U_EFFECTS16,false);
        }
    }else{
        if(t->effects!=f->effects)b|=q2_width((uint32_t)t->effects,Q2_U_EFFECTS8,Q2_U_EFFECTS16,true);
        if(t->morefx!=f->morefx)b|=q2_width(t->morefx,Q2_U_MOREFX8,Q2_U_MOREFX16,true);
    }
    if(t->renderfx!=f->renderfx)b|=q2_width(t->renderfx,Q2_U_RENDER8,Q2_U_RENDER16,!kex);
    if(t->solid!=f->solid)b|=Q2_U_SOLID;
    if(t->event)b|=Q2_U_EVENT;
    const uint32_t models[4]={t->modelindex,t->modelindex2,t->modelindex3,t->modelindex4};
    const uint32_t old_models[4]={f->modelindex,f->modelindex2,f->modelindex3,f->modelindex4};
    for(unsigned i=0;i<4;i++)if(models[i]!=old_models[i]){b|=model_bits[i];if(models[i]>255)b|=Q2_U_MODEL16;}
    uint8_t volume=q2_loop_volume(t->loop_volume),attenuation=q2_loop_attenuation(t->loop_attenuation);
    bool volume_changed=kex?volume!=q2_loop_volume(f->loop_volume):t->loop_volume!=f->loop_volume;
    bool attenuation_changed=kex?attenuation!=q2_loop_attenuation(f->loop_attenuation):t->loop_attenuation!=f->loop_attenuation;
    if(t->sound!=f->sound||(kex&&(volume_changed||attenuation_changed)))b|=Q2_U_SOUND;
    if(fresh||(t->renderfx&128u))b|=Q2_U_OLDORIGIN;
    uint8_t alpha=q2_quantized(t->alpha,255.0f,true),scale=q2_quantized(t->scale,16.0f,true);
    if(kex?alpha!=q2_quantized(f->alpha,255.0f,true):t->alpha!=f->alpha)b|=Q2_U_ALPHA;
    if(kex?scale!=q2_quantized(f->scale,16.0f,true):t->scale!=f->scale)b|=Q2_U_SCALE;
    if(kex){
        if(t->instance_bits!=f->instance_bits)b|=Q2_U_MOREFX16;
        if(t->owner!=f->owner)b|=Q2_U_OWNER;
        if(t->old_frame!=f->old_frame)b|=Q2_U_OLDFRAME;
        /* Native KEX sign-extends bit 31; all high fields must be emitted. */
        if(b>>32)b|=UINT64_C(255)<<32;
    }
    if(!b&&!force)return true;
    if(!qa_q2_wide_write_header(c,w,t->number,b))return false;
    for(unsigned i=0;i<4;i++)if(b&model_bits[i]){
        if(b&Q2_U_MODEL16)qa_net_write_u16(w,(uint16_t)models[i]);else qa_net_write_u8(w,(uint8_t)models[i]);
    }
    if(b&Q2_U_FRAME16)qa_net_write_u16(w,(uint16_t)t->frame);else if(b&Q2_U_FRAME8)qa_net_write_u8(w,(uint8_t)t->frame);
    q2_write_width(w,t->skinnum,b,Q2_U_SKIN8,Q2_U_SKIN16);
    if(kex&&(b&Q2_U_MOREFX8))qa_net_write_u32(w,(uint32_t)t->effects);
    q2_write_width(w,kex&&(b&Q2_U_MOREFX8)?t->morefx:(uint32_t)t->effects,b,Q2_U_EFFECTS8,Q2_U_EFFECTS16);
    q2_write_width(w,t->renderfx,b,Q2_U_RENDER8,Q2_U_RENDER16);
    if(kex&&(b&Q2_U_SOLID))qa_net_write_u32(w,t->solid);
    bool floating=!kex||c->protocol.kind!=QA_NET_Q2KEX_DEMO_2022||t->solid!=0;
    for(unsigned i=0;i<3;i++)if(b&origin_bits[i]){
        if(floating)qa_net_write_f32(w,t->origin[i]);else qa_q2_write_coord(w,t->origin[i]);
    }
    if(kex&&(b&Q2_U_OLDORIGIN))qa_q2_write_vec3(w,t->old_origin,floating);
    for(unsigned i=0;i<3;i++)if(b&angle_bits[i]){
        if(kex)qa_net_write_f32(w,t->angles[i]);else qa_net_write_u16(w,qa_angle_to_word(t->angles[i]));
    }
    if(!kex&&(b&Q2_U_OLDORIGIN))qa_q2_write_vec3(w,t->old_origin,true);
    if(b&Q2_U_SOUND){
        qa_net_write_u16(w,(uint16_t)(t->sound|(volume_changed?16384u:0u)|(attenuation_changed?32768u:0u)));
        if(volume_changed)qa_net_write_u8(w,volume);
        if(attenuation_changed)qa_net_write_u8(w,attenuation);
    }
    if(b&Q2_U_EVENT)qa_net_write_u8(w,(uint8_t)t->event);
    if(!kex){
        if(b&Q2_U_SOLID)qa_net_write_u32(w,t->solid);
        q2_write_width(w,t->morefx,b,Q2_U_MOREFX8,Q2_U_MOREFX16);
    }
    if(b&Q2_U_ALPHA)qa_net_write_u8(w,alpha);
    if(b&Q2_U_SCALE)qa_net_write_u8(w,scale);
    if(kex){
        if(b&Q2_U_MOREFX16)qa_net_write_u8(w,(uint8_t)t->instance_bits);
        if(b&Q2_U_OWNER)qa_net_write_u16(w,(uint16_t)t->owner);
        if(b&Q2_U_OLDFRAME)qa_net_write_u16(w,(uint16_t)t->old_frame);
    }
    return !w->failed;
}

static bool read_serverdata(qa_q2_codec *c,qa_net_reader *r,qa_q2_serverdata *d) {
    if(!d)return qa_net_reader_fail(r,"Missing Q2 rerelease serverdata output");
    c->frame_player_pending=false;c->frame_extra=0;
    memset(d,0,sizeof(*d));d->servercount=qa_net_read_i32(r);d->attractloop=qa_net_read_u8(r)!=0;
    qa_net_read_string(r,d->gamedir,sizeof(d->gamedir));d->clientnum=qa_net_read_i16(r);
    d->clientnums[0]=d->clientnum;d->client_count=1;
    qa_net_read_string(r,d->levelname,sizeof(d->levelname));d->protocol_revision=qa_net_read_u16(r);
    d->server_state=qa_net_read_u8(r);d->wire_flags=qa_net_read_u16(r);d->server_fps=qa_net_read_u8(r);
    c->wire_flags=d->wire_flags;
    return !r->failed;
}
static bool write_serverdata(qa_q2_codec *c,qa_net_writer *w,const qa_q2_serverdata *d) {
    if(!d)return qa_net_writer_fail(w,"Missing Q2 rerelease serverdata");
    if(!memchr(d->gamedir,0,sizeof(d->gamedir))||!memchr(d->levelname,0,sizeof(d->levelname)))
        return qa_net_writer_fail(w,"Unterminated Q2 rerelease serverdata string");
    if(d->clientnum<INT16_MIN||d->clientnum>INT16_MAX||d->client_count>1||d->protocol_revision>UINT16_MAX||
       d->server_state>255||d->wire_flags>UINT16_MAX||d->server_fps>255)
        return qa_net_writer_fail(w,"Q2 rerelease serverdata outside wire range");
    qa_net_write_u8(w,12);qa_net_write_u32(w,qa_q2_protocol_version(c->protocol));
    qa_net_write_i32(w,d->servercount);qa_net_write_u8(w,d->attractloop?1:0);
    qa_net_write_string(w,d->gamedir);qa_net_write_i16(w,(int16_t)d->clientnum);qa_net_write_string(w,d->levelname);
    qa_net_write_u16(w,(uint16_t)(d->protocol_revision?d->protocol_revision:1024));
    qa_net_write_u8(w,(uint8_t)d->server_state);qa_net_write_u16(w,(uint16_t)d->wire_flags);
    qa_net_write_u8(w,(uint8_t)(d->server_fps?d->server_fps:10));return !w->failed;
}
static bool read_entity(qa_q2_codec *c,qa_net_reader *r,const qa_q2_entity *f,uint32_t n,uint64_t b,qa_q2_entity *t) {
    return qa_q2_extended_read_entity(c,r,f,n,b,t,false);
}
static bool write_entity(qa_q2_codec *c,qa_net_writer *w,const qa_q2_entity *f,const qa_q2_entity *t,bool force,bool fresh) {
    return qa_q2_extended_write_entity(c,w,f,t,force,fresh,false);
}

typedef struct player_delta {uint16_t flags;uint8_t extra,blend;uint64_t stats;} player_delta;
static player_delta player_flags(const qa_q2_player *f,const qa_q2_player *t) {
    player_delta d={0};
    if(t->pmove.type!=f->pmove.type)d.flags|=Q2_PS_TYPE;
    if(t->pmove.origin_f[0]!=f->pmove.origin_f[0]||t->pmove.origin_f[1]!=f->pmove.origin_f[1])d.flags|=Q2_PS_ORIGIN;
    if(t->pmove.origin_f[2]!=f->pmove.origin_f[2])d.extra|=Q2_EPS_ORIGIN_Z;
    if(t->pmove.velocity_f[0]!=f->pmove.velocity_f[0]||t->pmove.velocity_f[1]!=f->pmove.velocity_f[1])d.flags|=Q2_PS_VELOCITY;
    if(t->pmove.velocity_f[2]!=f->pmove.velocity_f[2])d.extra|=Q2_EPS_VELOCITY_Z;
    if(t->pmove.time!=f->pmove.time)d.flags|=Q2_PS_TIME;
    if(t->pmove.flags!=f->pmove.flags)d.flags|=Q2_PS_FLAGS;
    if(t->pmove.gravity!=f->pmove.gravity)d.flags|=Q2_PS_GRAVITY;
    for(unsigned i=0;i<3;i++)if(t->pmove.delta_angles[i]!=f->pmove.delta_angles[i])d.flags|=Q2_PS_DELTA_ANGLES;
    if(t->pmove.viewheight!=f->pmove.viewheight)d.flags|=Q2_PS_VIEWHEIGHT;
    if(q2_fixed_changed3(t->viewoffset,f->viewoffset,16.0f))d.flags|=Q2_PS_VIEWOFFSET;
    for(unsigned i=0;i<3;i++)if(qa_angle_to_word(t->viewangles[i])!=qa_angle_to_word(f->viewangles[i])){
        if(i==2)d.extra|=Q2_EPS_VIEWANGLE_Z;else d.flags|=Q2_PS_VIEWANGLES;
    }
    if(q2_fixed_changed3(t->kick_angles,f->kick_angles,1024.0f))d.flags|=Q2_PS_KICK;
    for(unsigned i=0;i<4;i++){
        if(q2_byte_color(t->blend[i],false)!=q2_byte_color(f->blend[i],false))d.blend|=(uint8_t)(1u<<i);
        if(q2_byte_color(t->damage_blend[i],false)!=q2_byte_color(f->damage_blend[i],false))d.blend|=(uint8_t)(1u<<(i+4));
    }
    if(d.blend)d.flags|=Q2_PS_BLEND;
    if(t->fov!=f->fov)d.flags|=Q2_PS_FOV;
    if(t->rdflags!=f->rdflags)d.flags|=Q2_PS_RDFLAGS;
    if(t->gunindex!=f->gunindex||t->gunskin!=f->gunskin)d.flags|=Q2_PS_WEAPON;
    if(t->gunframe!=f->gunframe)d.flags|=Q2_PS_WEAPONFRAME;
    if(q2_fixed_changed3(t->gunoffset,f->gunoffset,512.0f))d.extra|=Q2_EPS_GUNOFFSET;
    if(q2_fixed_changed3(t->gunangles,f->gunangles,4096.0f))d.extra|=Q2_EPS_GUNANGLES;
    for(unsigned i=0;i<64;i++)if(t->stats[i]!=f->stats[i])d.stats|=UINT64_C(1)<<i;
    if(d.stats)d.extra|=Q2_EPS_STATS;
    if(t->gunrate!=f->gunrate)d.extra|=Q2_EPS_GUNRATE;
    return d;
}
static bool player_valid(qa_net_writer *w,const qa_q2_player *t) {
    if(!t)return qa_net_writer_fail(w,"Missing Q2 rerelease player state");
    if(t->clientnum||t->team_id||q2_fog_nonzero(&t->fog))
        return qa_net_writer_fail(w,"Q2repro player cannot carry client, team or player-fog fields");
    if(!q2_player_finite(t))return qa_net_writer_fail(w,"Nonfinite Q2 rerelease player state");
    if(t->pmove.type<0||t->pmove.type>255||t->pmove.time<0||t->pmove.time>65535||
       t->pmove.flags<0||t->pmove.flags>65535||t->pmove.gravity<INT16_MIN||t->pmove.gravity>INT16_MAX||
       t->pmove.viewheight<INT8_MIN||t->pmove.viewheight>INT8_MAX||t->gunindex>8191||t->gunskin>7||
       t->gunframe>65535||t->gunrate>255||t->rdflags>255||t->fov<0.0f||t->fov>=256.0f)
        return qa_net_writer_fail(w,"Q2 rerelease player state outside wire range");
    return true;
}
static void write_player_body(qa_net_writer *w,const qa_q2_player *t,player_delta d) {
    uint16_t b=d.flags;uint8_t e=d.extra;
    if(b&Q2_PS_TYPE)qa_net_write_u8(w,(uint8_t)t->pmove.type);
    if(b&Q2_PS_ORIGIN)for(unsigned i=0;i<2;i++)qa_net_write_f32(w,t->pmove.origin_f[i]);
    if(e&Q2_EPS_ORIGIN_Z)qa_net_write_f32(w,t->pmove.origin_f[2]);
    if(b&Q2_PS_VELOCITY)for(unsigned i=0;i<2;i++)qa_net_write_f32(w,t->pmove.velocity_f[i]);
    if(e&Q2_EPS_VELOCITY_Z)qa_net_write_f32(w,t->pmove.velocity_f[2]);
    if(b&Q2_PS_TIME)qa_net_write_u16(w,(uint16_t)t->pmove.time);
    if(b&Q2_PS_FLAGS)qa_net_write_u16(w,(uint16_t)t->pmove.flags);
    if(b&Q2_PS_GRAVITY)qa_net_write_i16(w,(int16_t)t->pmove.gravity);
    if(b&Q2_PS_DELTA_ANGLES)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,t->pmove.delta_angles[i]);
    if(b&Q2_PS_VIEWOFFSET)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->viewoffset[i],16.0f));
    if(b&Q2_PS_VIEWANGLES)for(unsigned i=0;i<2;i++)qa_net_write_u16(w,qa_angle_to_word(t->viewangles[i]));
    if(e&Q2_EPS_VIEWANGLE_Z)qa_net_write_u16(w,qa_angle_to_word(t->viewangles[2]));
    if(b&Q2_PS_KICK)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->kick_angles[i],1024.0f));
    if(b&Q2_PS_WEAPON)qa_net_write_u16(w,(uint16_t)(t->gunindex|(t->gunskin<<13)));
    if(b&Q2_PS_WEAPONFRAME)qa_net_write_u16(w,(uint16_t)t->gunframe);
    if(e&Q2_EPS_GUNOFFSET)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->gunoffset[i],512.0f));
    if(e&Q2_EPS_GUNANGLES)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->gunangles[i],4096.0f));
    if(b&Q2_PS_BLEND){
        qa_net_write_u8(w,d.blend);
        for(unsigned i=0;i<4;i++)if(d.blend&(1u<<i))qa_net_write_u8(w,q2_byte_color(t->blend[i],false));
        for(unsigned i=0;i<4;i++)if(d.blend&(1u<<(i+4)))qa_net_write_u8(w,q2_byte_color(t->damage_blend[i],false));
    }
    if(b&Q2_PS_FOV)qa_net_write_u8(w,(uint8_t)t->fov);
    if(b&Q2_PS_RDFLAGS)qa_net_write_u8(w,(uint8_t)t->rdflags);
    if(e&Q2_EPS_STATS){qa_net_write_u64(w,d.stats);for(unsigned i=0;i<64;i++)if(d.stats&(UINT64_C(1)<<i))qa_net_write_i16(w,t->stats[i]);}
    if(e&Q2_EPS_GUNRATE)qa_net_write_u8(w,(uint8_t)t->gunrate);
    if(b&Q2_PS_VIEWHEIGHT)qa_net_write_i8(w,(int8_t)t->pmove.viewheight);
}
static bool write_player(qa_q2_codec *c,qa_net_writer *w,const qa_q2_player *f,const qa_q2_player *t) {
    (void)c;
    if(!f)f=&zero_player;
    if(!player_valid(w,t))return false;
    player_delta d=player_flags(f,t);
    qa_net_write_u8(w,17);qa_net_write_u16(w,d.flags);qa_net_write_u8(w,d.extra);
    write_player_body(w,t,d);return !w->failed;
}
static bool read_player(qa_q2_codec *c,qa_net_reader *r,const qa_q2_player *f,qa_q2_player *t) {
    if(!t)return qa_net_reader_fail(r,"Missing Q2 rerelease player output");
    if(!f)f=&zero_player;
    *t=*f;uint16_t b=qa_net_read_u16(r);
    uint8_t e=c->frame_player_pending?(uint8_t)c->frame_extra:qa_net_read_u8(r);c->frame_player_pending=false;
    if(e&64u)return qa_net_reader_fail(r,"Unknown Q2repro player extra flags");
    if(b&Q2_PS_TYPE)t->pmove.type=qa_net_read_u8(r);
    if(b&Q2_PS_ORIGIN)for(unsigned i=0;i<2;i++)t->pmove.origin_f[i]=q2_read_float(r);
    if(e&Q2_EPS_ORIGIN_Z)t->pmove.origin_f[2]=q2_read_float(r);
    if((b&Q2_PS_ORIGIN)||(e&Q2_EPS_ORIGIN_Z))for(unsigned i=0;i<3;i++)t->pmove.origin[i]=q2_pm_short(t->pmove.origin_f[i]);
    if(b&Q2_PS_VELOCITY)for(unsigned i=0;i<2;i++)t->pmove.velocity_f[i]=q2_read_float(r);
    if(e&Q2_EPS_VELOCITY_Z)t->pmove.velocity_f[2]=q2_read_float(r);
    if((b&Q2_PS_VELOCITY)||(e&Q2_EPS_VELOCITY_Z))for(unsigned i=0;i<3;i++)t->pmove.velocity[i]=q2_pm_short(t->pmove.velocity_f[i]);
    if(b&Q2_PS_TIME)t->pmove.time=qa_net_read_u16(r);
    if(b&Q2_PS_FLAGS)t->pmove.flags=qa_net_read_u16(r);
    if(b&Q2_PS_GRAVITY)t->pmove.gravity=qa_net_read_i16(r);
    if(b&Q2_PS_DELTA_ANGLES){for(unsigned i=0;i<3;i++)t->pmove.delta_angles[i]=qa_net_read_i16(r);t->pmove.float_delta_angles=false;}
    if(b&Q2_PS_VIEWOFFSET)for(unsigned i=0;i<3;i++)t->viewoffset[i]=(float)qa_net_read_i16(r)/16.0f;
    if(b&Q2_PS_VIEWANGLES)for(unsigned i=0;i<2;i++)t->viewangles[i]=qa_q2_read_angle16(r);
    if(e&Q2_EPS_VIEWANGLE_Z)t->viewangles[2]=qa_q2_read_angle16(r);
    if(b&Q2_PS_KICK)for(unsigned i=0;i<3;i++)t->kick_angles[i]=(float)qa_net_read_i16(r)/1024.0f;
    if(b&Q2_PS_WEAPON){uint16_t gun=qa_net_read_u16(r);t->gunindex=gun&8191u;t->gunskin=gun>>13;}
    if(b&Q2_PS_WEAPONFRAME)t->gunframe=qa_net_read_u16(r);
    if(e&Q2_EPS_GUNOFFSET)for(unsigned i=0;i<3;i++)t->gunoffset[i]=(float)qa_net_read_i16(r)/512.0f;
    if(e&Q2_EPS_GUNANGLES)for(unsigned i=0;i<3;i++)t->gunangles[i]=(float)qa_net_read_i16(r)/4096.0f;
    if(b&Q2_PS_BLEND){
        uint8_t blend=qa_net_read_u8(r);
        for(unsigned i=0;i<4;i++)if(blend&(1u<<i))t->blend[i]=(float)qa_net_read_u8(r)/255.0f;
        for(unsigned i=0;i<4;i++)if(blend&(1u<<(i+4)))t->damage_blend[i]=(float)qa_net_read_u8(r)/255.0f;
    }
    if(b&Q2_PS_FOV)t->fov=qa_net_read_u8(r);
    if(b&Q2_PS_RDFLAGS)t->rdflags=qa_net_read_u8(r);
    if(e&Q2_EPS_STATS){uint64_t stats=qa_net_read_u64(r);for(unsigned i=0;i<64;i++)if(stats&(UINT64_C(1)<<i))t->stats[i]=qa_net_read_i16(r);}
    if(e&Q2_EPS_GUNRATE)t->gunrate=qa_net_read_u8(r);
    if(b&Q2_PS_VIEWHEIGHT)t->pmove.viewheight=qa_net_read_i8(r);
    return !r->failed;
}
static bool read_frame(qa_q2_codec *c,qa_net_reader *r,qa_q2_frame_header *h) {
    if(!h)return qa_net_reader_fail(r,"Missing Q2 rerelease frame header output");
    uint32_t encoded=qa_net_read_u32(r),offset=encoded>>27;
    h->serverframe=(int32_t)(encoded&UINT32_C(0x07ffffff));h->deltaframe=offset==31?-1:h->serverframe-(int32_t)offset;
    h->suppress_count=(qa_net_read_u8(r)&1u)?1:0;c->frame_extra=qa_net_read_u8(r);
    h->areabytes=qa_net_read_u8(r);qa_net_read_data(r,h->areabits,h->areabytes);c->frame_player_pending=true;
    return !r->failed;
}
static bool write_frame(qa_q2_codec *c,qa_net_writer *w,const qa_q2_frame_header *h,
                        const qa_q2_player *f,const qa_q2_player *t,qa_q2_write_entities_fn fn,void *user) {
    if(!h)return qa_net_writer_fail(w,"Missing Q2 rerelease frame header");
    (void)c;qa_q2_player zero={0};if(!f)f=&zero;
    int64_t offset=h->deltaframe==-1?31:(int64_t)h->serverframe-h->deltaframe;
    if(h->serverframe<0||h->serverframe>0x07ffffff||offset<0||offset>31||
       (h->deltaframe!=-1&&offset==31)||h->areabytes>255)
        return qa_net_writer_fail(w,"Q2 rerelease frame outside wire range");
    if(!player_valid(w,t))return false;
    player_delta d=player_flags(f,t);
    qa_net_write_u8(w,20);qa_net_write_u32(w,(uint32_t)h->serverframe|((uint32_t)offset<<27));
    qa_net_write_u8(w,h->suppress_count?1:0);qa_net_write_u8(w,d.extra);
    qa_net_write_u8(w,(uint8_t)h->areabytes);qa_net_write_data(w,h->areabits,h->areabytes);
    qa_net_write_u16(w,d.flags);write_player_body(w,t,d);
    return !w->failed&&(!fn||fn(user,w,w->error));
}
static bool read_usercmd(qa_q2_codec *c,qa_net_reader *r,const qa_q2_usercmd *f,qa_q2_usercmd *t) {
    if(!t)return qa_net_reader_fail(r,"Missing Q2repro user command output");
    if(!f)f=&zero_usercmd;
    if(c->protocol.kind!=QA_NET_Q2PRIVATE_4038){
        qa_net_reader peek=*r;uint8_t b=qa_net_read_u8(&peek);
        if(b&32u)return qa_net_reader_fail(r,"Q2repro nonbatched movement has reserved CM_UP bit");
    }
    return qa_q2_classic_read_usercmd(c,r,f,t);
}
static bool write_usercmd(qa_q2_codec *c,qa_net_writer *w,const qa_q2_usercmd *f,const qa_q2_usercmd *t) {
    if(!t)return qa_net_writer_fail(w,"Missing Q2repro user command");
    if(!f)f=&zero_usercmd;
    if(c->protocol.kind!=QA_NET_Q2PRIVATE_4038&&t->upmove!=f->upmove)
        return qa_net_writer_fail(w,"Q2repro nonbatched movement cannot carry upmove changes");
    return qa_q2_classic_write_usercmd(c,w,f,t);
}
const qa_q2_codec_ops qa_q2_rerelease_ops={
    read_serverdata,write_serverdata,qa_q2_wide_read_header,read_entity,write_entity,qa_q2_wide_remove,
    read_player,write_player,read_frame,write_frame,read_usercmd,write_usercmd
};

static int16_t batch_angle(qa_net_reader *r,int16_t previous) {
    int32_t value=qa_net_read_bits(r,1)?(int32_t)previous+qa_net_read_sbits(r,8):qa_net_read_sbits(r,16);
    uint16_t bits=(uint16_t)value;
    return (int16_t)(bits<32768u?(int32_t)bits:(int32_t)bits-65536);
}
static bool read_batch_cmd(qa_net_reader *r,const qa_q2_usercmd *previous,qa_q2_usercmd *t,bool classic) {
    if(previous)*t=*previous;else memset(t,0,sizeof(*t));
    t->server_frame=0;
    if(!qa_net_read_bits(r,1))return !r->failed;
    uint32_t b=qa_net_read_bits(r,8);
    if(b&1u)t->angles[0]=batch_angle(r,t->angles[0]);
    if(b&2u)t->angles[1]=batch_angle(r,t->angles[1]);
    if(b&4u)t->angles[2]=(int16_t)qa_net_read_sbits(r,16);
    if(b&8u)t->forwardmove=(float)qa_net_read_sbits(r,10);
    if(b&16u)t->sidemove=(float)qa_net_read_sbits(r,10);
    if(b&32u){
        if(!classic)return qa_net_reader_fail(r,"Q2repro batched movement has reserved CM_UP bit");
        t->upmove=(float)qa_net_read_sbits(r,10);
    }
    if(b&64u)t->buttons=(uint8_t)qa_net_read_bits(r,8);
    if(b&128u)t->msec=(uint8_t)qa_net_read_bits(r,8);
    return !r->failed;
}
static int32_t batch_move(float f) {
    if(f< -512.0f)return -512;
    if(f>511.0f)return 511;
    return (int32_t)f;
}
static bool write_batch_cmd(qa_net_writer *w,const qa_q2_usercmd *previous,const qa_q2_usercmd *t,bool classic) {
    qa_q2_usercmd zero={0};if(!previous)previous=&zero;
    if(!isfinite(t->forwardmove)||!isfinite(t->sidemove)||!isfinite(t->upmove))
        return qa_net_writer_fail(w,"Nonfinite Q2repro batch movement");
    uint8_t b=128;
    for(unsigned i=0;i<3;i++)if(t->angles[i]!=previous->angles[i])b|=(uint8_t)(1u<<i);
    if(t->forwardmove!=previous->forwardmove)b|=8u;
    if(t->sidemove!=previous->sidemove)b|=16u;
    if(classic&&t->upmove!=previous->upmove)b|=32u;
    if(t->buttons!=previous->buttons)b|=64u;
    qa_net_write_bits(w,1,1);qa_net_write_bits(w,b,8);
    for(unsigned i=0;i<3;i++)if(b&(1u<<i)){
        if(i<2)qa_net_write_bits(w,0,1);
        qa_net_write_bits(w,(uint16_t)t->angles[i],16);
    }
    if(b&8u)qa_net_write_bits(w,(uint32_t)batch_move(t->forwardmove),10);
    if(b&16u)qa_net_write_bits(w,(uint32_t)batch_move(t->sidemove),10);
    if(b&32u)qa_net_write_bits(w,(uint32_t)batch_move(t->upmove),10);
    if(b&64u)qa_net_write_bits(w,t->buttons,8);
    qa_net_write_bits(w,t->msec,8);return !w->failed;
}
bool qa_q2_repro_read_batch(qa_q2_codec *c,qa_net_reader *r,bool nodelta,qa_q2_repro_batch *batch) {
    if(!c||!batch)return qa_net_reader_fail(r,"Missing Q2repro batch codec or output");
    if(c->protocol.kind!=QA_NET_Q2REPRO_1038&&c->protocol.kind!=QA_NET_Q2PRIVATE_4038)
        return qa_net_reader_fail(r,"Q2repro batch requires a rerelease protocol");
    memset(batch,0,sizeof(*batch));batch->last_frame=nodelta?-1:qa_net_read_i32(r);
    uint8_t duplicates=qa_net_read_u8(r);
    if(duplicates>=QA_Q2_REPRO_BATCH_FRAMES)return qa_net_reader_fail(r,"Q2repro batch duplicate count exceeds two");
    uint8_t lightlevel=qa_net_read_u8(r);batch->frame_count=(size_t)duplicates+1;
    bool classic=c->protocol.kind==QA_NET_Q2PRIVATE_4038;
    const qa_q2_usercmd *previous=NULL;
    for(size_t i=0;i<batch->frame_count;i++){
        qa_q2_batch_frame *frame=&batch->frames[i];frame->count=qa_net_read_bits(r,5);
        for(size_t j=0;j<frame->count;j++){
            qa_q2_usercmd *t=&frame->commands[j];
            if(!read_batch_cmd(r,previous,t,classic))return false;
            if(classic)t->lightlevel=lightlevel;
            previous=t;
        }
    }
    unsigned padding=(unsigned)((8u-(r->bit&7u))&7u);
    if(padding)(void)qa_net_read_bits(r,padding);
    return !r->failed;
}
bool qa_q2_repro_write_batch(qa_q2_codec *c,qa_net_writer *w,bool nodelta,const qa_q2_repro_batch *batch) {
    if(!c||!batch)return qa_net_writer_fail(w,"Missing Q2repro batch codec or data");
    if(c->protocol.kind!=QA_NET_Q2REPRO_1038&&c->protocol.kind!=QA_NET_Q2PRIVATE_4038)
        return qa_net_writer_fail(w,"Q2repro batch requires a rerelease protocol");
    if(!batch->frame_count||batch->frame_count>QA_Q2_REPRO_BATCH_FRAMES)
        return qa_net_writer_fail(w,"Q2repro batch frame count outside wire range");
    bool classic=c->protocol.kind==QA_NET_Q2PRIVATE_4038;uint8_t lightlevel=0;
    for(size_t i=0;i<batch->frame_count;i++){
        const qa_q2_batch_frame *frame=&batch->frames[i];
        if(frame->count>QA_Q2_BATCH_COMMANDS)return qa_net_writer_fail(w,"Q2repro batch command count exceeds five bits");
        if(classic&&frame->count)lightlevel=frame->commands[frame->count-1].lightlevel;
    }
    if(!nodelta)qa_net_write_i32(w,batch->last_frame);
    qa_net_write_u8(w,(uint8_t)(batch->frame_count-1));qa_net_write_u8(w,lightlevel);
    const qa_q2_usercmd *previous=NULL;
    for(size_t i=0;i<batch->frame_count;i++){
        const qa_q2_batch_frame *frame=&batch->frames[i];qa_net_write_bits(w,(uint32_t)frame->count,5);
        for(size_t j=0;j<frame->count;j++){
            const qa_q2_usercmd *t=&frame->commands[j];
            if(!write_batch_cmd(w,previous,t,classic))return false;
            previous=t;
        }
    }
    unsigned padding=(unsigned)((8u-(w->bit&7u))&7u);
    if(padding)qa_net_write_bits(w,0,padding);
    return !w->failed;
}
