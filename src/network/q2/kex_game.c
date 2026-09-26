#include "kex_game_internal.h"
#include "qa/network_q2_kex_game.h"
#include "qa/network_q2_messages.h"
#include <stdlib.h>
#include <zlib.h>

static const qa_q2_player zero_player;
static const qa_q2_usercmd zero_usercmd;

static bool read_serverdata(qa_q2_codec *c,qa_net_reader *r,qa_q2_serverdata *d) {
    if(!d)return qa_net_reader_fail(r,"Missing KEX serverdata output");
    memset(d,0,sizeof(*d));memset(c->kex_nonzero_solid,0,sizeof(c->kex_nonzero_solid));
    c->frame_player_pending=false;c->frame_extra=0;
    d->servercount=qa_net_read_i32(r);d->attractloop=qa_net_read_u8(r)!=0;d->server_fps=qa_net_read_u8(r);
    qa_net_read_string(r,d->gamedir,sizeof(d->gamedir));d->clientnum=qa_net_read_i16(r);
    if(d->clientnum==-2){
        int count=qa_net_read_i16(r);
        if(count<1||count>QA_Q2_MAX_SEATS)return qa_net_reader_fail(r,"Invalid KEX split player count");
        d->client_count=(size_t)count;
        for(size_t i=0;i<d->client_count;i++)d->clientnums[i]=qa_net_read_i16(r);
        d->clientnum=d->clientnums[0];
    }else{d->client_count=1;d->clientnums[0]=d->clientnum;}
    c->split_players=d->client_count;qa_net_read_string(r,d->levelname,sizeof(d->levelname));
    return !r->failed;
}
static bool write_serverdata(qa_q2_codec *c,qa_net_writer *w,const qa_q2_serverdata *d) {
    if(!d)return qa_net_writer_fail(w,"Missing KEX serverdata");
    if(!memchr(d->gamedir,0,sizeof(d->gamedir))||!memchr(d->levelname,0,sizeof(d->levelname)))
        return qa_net_writer_fail(w,"Unterminated KEX serverdata string");
    if(d->client_count>QA_Q2_MAX_SEATS||d->server_fps>255||d->clientnum<INT16_MIN||d->clientnum>INT16_MAX)
        return qa_net_writer_fail(w,"KEX serverdata outside wire range");
    if(d->client_count>1)for(size_t i=0;i<d->client_count;i++)
        if(d->clientnums[i]<INT16_MIN||d->clientnums[i]>INT16_MAX)return qa_net_writer_fail(w,"KEX client number outside wire range");
    if(d->client_count<=1&&d->clientnum==-2)return qa_net_writer_fail(w,"KEX split sentinel requires a client list");
    qa_net_write_u8(w,12);qa_net_write_u32(w,qa_q2_protocol_version(c->protocol));
    qa_net_write_i32(w,d->servercount);qa_net_write_u8(w,d->attractloop?1:0);
    qa_net_write_u8(w,(uint8_t)(d->server_fps?d->server_fps:40));qa_net_write_string(w,d->gamedir);
    if(d->client_count>1){
        qa_net_write_i16(w,-2);qa_net_write_u16(w,(uint16_t)d->client_count);
        for(size_t i=0;i<d->client_count;i++)qa_net_write_i16(w,(int16_t)d->clientnums[i]);
    }else qa_net_write_i16(w,(int16_t)d->clientnum);
    qa_net_write_string(w,d->levelname);c->split_players=d->client_count?d->client_count:1;
    return !w->failed;
}
static bool read_entity(qa_q2_codec *c,qa_net_reader *r,const qa_q2_entity *f,uint32_t n,uint64_t b,qa_q2_entity *t) {
    return qa_q2_extended_read_entity(c,r,f,n,b,t,true);
}
static bool write_entity(qa_q2_codec *c,qa_net_writer *w,const qa_q2_entity *f,const qa_q2_entity *t,bool force,bool fresh) {
    return qa_q2_extended_write_entity(c,w,f,t,force,fresh,true);
}
static float delta_angle(const qa_q2_player *p,unsigned i) {
    return p->pmove.float_delta_angles?p->pmove.delta_angles_f[i]:(float)p->pmove.delta_angles[i]*(360.0f/65536.0f);
}
static bool read_player(qa_q2_codec *c,qa_net_reader *r,const qa_q2_player *f,qa_q2_player *t) {
    if(!t)return qa_net_reader_fail(r,"Missing KEX player output");
    if(!f)f=&zero_player;
    if(c->frame_player_pending){
        c->frame_player_pending=false;
        if(qa_net_read_u8(r)!=17)return qa_net_reader_fail(r,"Expected KEX playerinfo");
    }
    *t=*f;uint32_t b=qa_net_read_u16(r);
    if(b&Q2_PS_VIEWHEIGHT)b|=(uint32_t)qa_net_read_u16(r)<<16;
    if(b&~UINT32_C(0x3ffff))return qa_net_reader_fail(r,"Unknown KEX player flags");
    if(b&Q2_PS_TYPE)t->pmove.type=qa_net_read_u8(r);
    if(b&Q2_PS_ORIGIN)for(unsigned i=0;i<3;i++){
        t->pmove.origin_f[i]=q2_read_float(r);t->pmove.origin[i]=q2_pm_short(t->pmove.origin_f[i]);
    }
    if(b&Q2_PS_VELOCITY)for(unsigned i=0;i<3;i++){
        t->pmove.velocity_f[i]=q2_read_float(r);t->pmove.velocity[i]=q2_pm_short(t->pmove.velocity_f[i]);
    }
    if(b&Q2_PS_TIME)t->pmove.time=qa_net_read_u16(r);
    if(b&Q2_PS_FLAGS)t->pmove.flags=qa_net_read_u16(r);
    if(b&Q2_PS_GRAVITY)t->pmove.gravity=qa_net_read_i16(r);
    if(b&Q2_PS_DELTA_ANGLES){
        t->pmove.float_delta_angles=true;
        for(unsigned i=0;i<3;i++){
            t->pmove.delta_angles_f[i]=q2_read_float(r);
            uint16_t a=q2_angle_short(t->pmove.delta_angles_f[i]);
            t->pmove.delta_angles[i]=(int16_t)(a<32768u?(int32_t)a:(int32_t)a-65536);
        }
    }
    if(b&Q2_PS_VIEWOFFSET){
        for(unsigned i=0;i<3;i++)t->viewoffset[i]=(float)qa_net_read_i16(r)/16.0f;
        t->pmove.viewheight=qa_net_read_i8(r);
    }
    if(b&Q2_PS_VIEWANGLES)for(unsigned i=0;i<3;i++)t->viewangles[i]=q2_read_float(r);
    if(b&Q2_PS_KICK)for(unsigned i=0;i<3;i++)t->kick_angles[i]=(float)qa_net_read_i16(r)/1024.0f;
    if(b&Q2_PS_WEAPON){uint16_t gun=qa_net_read_u16(r);t->gunindex=gun&8191u;t->gunskin=gun>>13;}
    if(b&Q2_PS_WEAPONFRAME){
        uint16_t gun=qa_net_read_u16(r);t->gunframe=gun&511u;gun>>=9;
        for(unsigned i=0;i<3;i++)if(gun&(1u<<i))t->gunoffset[i]=q2_read_float(r);
        for(unsigned i=0;i<3;i++)if(gun&(1u<<(i+3)))t->gunangles[i]=q2_read_float(r);
        if(gun&64u)t->gunrate=qa_net_read_u8(r);
    }
    if(b&Q2_PS_BLEND)for(unsigned i=0;i<4;i++)t->blend[i]=(float)qa_net_read_u8(r)/255.0f;
    if(b&Q2_PS_FOV)t->fov=qa_net_read_u8(r);
    if(b&Q2_PS_RDFLAGS)t->rdflags=qa_net_read_u8(r);
    for(unsigned half=0;half<2;half++){
        uint32_t bits=qa_net_read_u32(r);
        for(unsigned i=0;i<32;i++)if(bits&(UINT32_C(1)<<i))t->stats[half*32+i]=qa_net_read_i16(r);
    }
    if(b&Q2_PS_DAMAGE_BLEND)for(unsigned i=0;i<4;i++)t->damage_blend[i]=(float)qa_net_read_u8(r)/255.0f;
    if(b&Q2_PS_TEAM)t->team_id=qa_net_read_u8(r);
    return !r->failed;
}
static bool write_player(qa_q2_codec *c,qa_net_writer *w,const qa_q2_player *f,const qa_q2_player *t) {
    (void)c;
    if(!t)return qa_net_writer_fail(w,"Missing KEX player state");
    if(!f)f=&zero_player;
    if(t->clientnum||q2_fog_nonzero(&t->fog))
        return qa_net_writer_fail(w,"KEX player cannot carry client or player-fog fields");
    if(!q2_player_finite(t))return qa_net_writer_fail(w,"Nonfinite KEX player state");
    if(t->pmove.type<0||t->pmove.type>255||t->pmove.time<0||t->pmove.time>65535||
       t->pmove.flags<0||t->pmove.flags>65535||t->pmove.gravity<INT16_MIN||t->pmove.gravity>INT16_MAX||
       t->pmove.viewheight<INT8_MIN||t->pmove.viewheight>INT8_MAX||t->gunindex>8191||t->gunskin>7||
       t->gunframe>511||t->gunrate>255||t->rdflags>255||t->fov<0.0f||t->fov>=256.0f)
        return qa_net_writer_fail(w,"KEX player state outside wire range");
    uint32_t b=0;uint8_t gun=0;
    if(t->pmove.type!=f->pmove.type)b|=Q2_PS_TYPE;
    if(q2_changed3(t->pmove.origin_f,f->pmove.origin_f))b|=Q2_PS_ORIGIN;
    if(q2_changed3(t->pmove.velocity_f,f->pmove.velocity_f))b|=Q2_PS_VELOCITY;
    if(t->pmove.time!=f->pmove.time)b|=Q2_PS_TIME;
    if(t->pmove.flags!=f->pmove.flags)b|=Q2_PS_FLAGS;
    if(t->pmove.gravity!=f->pmove.gravity)b|=Q2_PS_GRAVITY;
    for(unsigned i=0;i<3;i++)if(delta_angle(t,i)!=delta_angle(f,i))b|=Q2_PS_DELTA_ANGLES;
    if(q2_fixed_changed3(t->viewoffset,f->viewoffset,16.0f)||t->pmove.viewheight!=f->pmove.viewheight)b|=Q2_PS_VIEWOFFSET;
    if(q2_changed3(t->viewangles,f->viewangles))b|=Q2_PS_VIEWANGLES;
    if(q2_fixed_changed3(t->kick_angles,f->kick_angles,1024.0f))b|=Q2_PS_KICK;
    for(unsigned i=0;i<4;i++){
        if(q2_byte_color(t->blend[i],true)!=q2_byte_color(f->blend[i],true))b|=Q2_PS_BLEND;
        if(q2_byte_color(t->damage_blend[i],true)!=q2_byte_color(f->damage_blend[i],true))b|=Q2_PS_DAMAGE_BLEND;
    }
    if(t->team_id!=f->team_id)b|=Q2_PS_TEAM;
    if(t->fov!=f->fov)b|=Q2_PS_FOV;
    if(t->rdflags!=f->rdflags)b|=Q2_PS_RDFLAGS;
    if(t->gunindex!=f->gunindex||t->gunskin!=f->gunskin)b|=Q2_PS_WEAPON;
    for(unsigned i=0;i<3;i++){
        if(t->gunoffset[i]!=f->gunoffset[i])gun|=(uint8_t)(1u<<i);
        if(t->gunangles[i]!=f->gunangles[i])gun|=(uint8_t)(1u<<(i+3));
    }
    if(t->gunrate!=f->gunrate)gun|=64u;
    if(t->gunframe!=f->gunframe||gun)b|=Q2_PS_WEAPONFRAME;
    if(b>65535)b|=Q2_PS_VIEWHEIGHT;
    qa_net_write_u16(w,(uint16_t)b);if(b&Q2_PS_VIEWHEIGHT)qa_net_write_u16(w,(uint16_t)(b>>16));
    if(b&Q2_PS_TYPE)qa_net_write_u8(w,(uint8_t)t->pmove.type);
    if(b&Q2_PS_ORIGIN)qa_q2_write_vec3(w,t->pmove.origin_f,true);
    if(b&Q2_PS_VELOCITY)qa_q2_write_vec3(w,t->pmove.velocity_f,true);
    if(b&Q2_PS_TIME)qa_net_write_u16(w,(uint16_t)t->pmove.time);
    if(b&Q2_PS_FLAGS)qa_net_write_u16(w,(uint16_t)t->pmove.flags);
    if(b&Q2_PS_GRAVITY)qa_net_write_i16(w,(int16_t)t->pmove.gravity);
    if(b&Q2_PS_DELTA_ANGLES)for(unsigned i=0;i<3;i++)qa_net_write_f32(w,delta_angle(t,i));
    if(b&Q2_PS_VIEWOFFSET){
        for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->viewoffset[i],16.0f));
        qa_net_write_i8(w,(int8_t)t->pmove.viewheight);
    }
    if(b&Q2_PS_VIEWANGLES)qa_q2_write_vec3(w,t->viewangles,true);
    if(b&Q2_PS_KICK)for(unsigned i=0;i<3;i++)qa_net_write_i16(w,q2_fixed(t->kick_angles[i],1024.0f));
    if(b&Q2_PS_WEAPON)qa_net_write_u16(w,(uint16_t)(t->gunindex|(t->gunskin<<13)));
    if(b&Q2_PS_WEAPONFRAME){
        qa_net_write_u16(w,(uint16_t)(t->gunframe|((uint32_t)gun<<9)));
        for(unsigned i=0;i<3;i++)if(gun&(1u<<i))qa_net_write_f32(w,t->gunoffset[i]);
        for(unsigned i=0;i<3;i++)if(gun&(1u<<(i+3)))qa_net_write_f32(w,t->gunangles[i]);
        if(gun&64u)qa_net_write_u8(w,(uint8_t)t->gunrate);
    }
    if(b&Q2_PS_BLEND)for(unsigned i=0;i<4;i++)qa_net_write_u8(w,q2_byte_color(t->blend[i],true));
    if(b&Q2_PS_FOV)qa_net_write_u8(w,(uint8_t)t->fov);
    if(b&Q2_PS_RDFLAGS)qa_net_write_u8(w,(uint8_t)t->rdflags);
    for(unsigned half=0;half<2;half++){
        uint32_t bits=0;for(unsigned i=0;i<32;i++)if(t->stats[half*32+i]!=f->stats[half*32+i])bits|=UINT32_C(1)<<i;
        qa_net_write_u32(w,bits);for(unsigned i=0;i<32;i++)if(bits&(UINT32_C(1)<<i))qa_net_write_i16(w,t->stats[half*32+i]);
    }
    if(b&Q2_PS_DAMAGE_BLEND)for(unsigned i=0;i<4;i++)qa_net_write_u8(w,q2_byte_color(t->damage_blend[i],true));
    if(b&Q2_PS_TEAM)qa_net_write_u8(w,t->team_id);
    return !w->failed;
}
static bool read_frame(qa_q2_codec *c,qa_net_reader *r,qa_q2_frame_header *h) {
    if(!h)return qa_net_reader_fail(r,"Missing KEX frame header output");
    h->serverframe=qa_net_read_i32(r);h->deltaframe=qa_net_read_i32(r);h->suppress_count=qa_net_read_u8(r);
    h->areabytes=qa_net_read_u8(r);qa_net_read_data(r,h->areabits,h->areabytes);c->frame_player_pending=true;return !r->failed;
}
static bool write_frame(qa_q2_codec *c,qa_net_writer *w,const qa_q2_frame_header *h,const qa_q2_player *f,
                        const qa_q2_player *t,qa_q2_write_entities_fn fn,void *user) {
    if(!h)return qa_net_writer_fail(w,"Missing KEX frame header");
    qa_q2_player zero={0};if(!f)f=&zero;
    if(h->areabytes>255)return qa_net_writer_fail(w,"KEX frame area bits outside wire range");
    qa_net_write_u8(w,20);qa_net_write_i32(w,h->serverframe);qa_net_write_i32(w,h->deltaframe);
    qa_net_write_u8(w,h->suppress_count);qa_net_write_u8(w,(uint8_t)h->areabytes);qa_net_write_data(w,h->areabits,h->areabytes);
    qa_net_write_u8(w,17);if(!write_player(c,w,f,t))return false;
    return !w->failed&&(!fn||fn(user,w,w->error));
}
static bool read_usercmd(qa_q2_codec *c,qa_net_reader *r,const qa_q2_usercmd *f,qa_q2_usercmd *t) {
    if(!t)return qa_net_reader_fail(r,"Missing KEX usercmd output");
    if(!f)f=&zero_usercmd;
    (void)c;uint8_t b=qa_net_read_u8(r);*t=*f;t->upmove=0.0f;t->impulse=0;
    if(b&32u)return qa_net_reader_fail(r,"KEX usercmd uses reserved bit 5");
    for(unsigned i=0;i<3;i++)if(b&(1u<<i)){
        uint16_t a=q2_angle_short(q2_read_float(r));t->angles[i]=(int16_t)(a<32768u?(int32_t)a:(int32_t)a-65536);
    }
    if(b&8u)t->forwardmove=q2_read_float(r);
    if(b&16u)t->sidemove=q2_read_float(r);
    if(b&64u)t->buttons=qa_net_read_u8(r);
    if(b&128u)t->server_frame=qa_net_read_i32(r);
    t->msec=qa_net_read_u8(r);return !r->failed;
}
static bool write_usercmd(qa_q2_codec *c,qa_net_writer *w,const qa_q2_usercmd *f,const qa_q2_usercmd *t) {
    (void)c;
    if(!t)return qa_net_writer_fail(w,"Missing KEX usercmd");
    if(!f)f=&zero_usercmd;
    if(t->upmove!=0.0f||t->impulse)return qa_net_writer_fail(w,"KEX usercmd cannot carry upmove or impulse");
    if(!isfinite(t->forwardmove)||!isfinite(t->sidemove))return qa_net_writer_fail(w,"Nonfinite KEX usercmd movement");
    uint8_t b=0;for(unsigned i=0;i<3;i++)if(t->angles[i]!=f->angles[i])b|=(uint8_t)(1u<<i);
    if(t->forwardmove!=f->forwardmove)b|=8u;
    if(t->sidemove!=f->sidemove)b|=16u;
    if(t->buttons!=f->buttons)b|=64u;
    if(t->server_frame!=f->server_frame)b|=128u;
    qa_net_write_u8(w,b);
    for(unsigned i=0;i<3;i++)if(b&(1u<<i))qa_net_write_f32(w,(float)t->angles[i]*(360.0f/65536.0f));
    if(b&8u)qa_net_write_f32(w,t->forwardmove);
    if(b&16u)qa_net_write_f32(w,t->sidemove);
    if(b&64u)qa_net_write_u8(w,t->buttons);
    if(b&128u)qa_net_write_i32(w,t->server_frame);
    qa_net_write_u8(w,t->msec);return !w->failed;
}
const qa_q2_codec_ops qa_q2_kex_ops={
    read_serverdata,write_serverdata,qa_q2_wide_read_header,read_entity,write_entity,qa_q2_wide_remove,
    read_player,write_player,read_frame,write_frame,read_usercmd,write_usercmd
};

bool qa_q2_kex_read_damage(qa_net_reader *r,qa_q2_kex_damage out[4],size_t *count) {
    if(!out||!count)return qa_net_reader_fail(r,"Missing KEX damage output");
    uint8_t n=qa_net_read_u8(r);*count=n>4?4:n;
    for(unsigned i=0;i<n;i++){
        qa_q2_kex_damage d={0};uint8_t flags=qa_net_read_u8(r);
        d.damage=flags&31u;d.health=(flags&32u)!=0;d.armor=(flags&64u)!=0;d.shield=(flags&128u)!=0;
        if(!qa_q2_read_dir(r,d.direction))return false;
        if(i<4)out[i]=d;
    }
    return !r->failed;
}
bool qa_q2_kex_write_damage(qa_net_writer *w,const qa_q2_kex_damage *d,size_t n) {
    if(n>255||(n&&!d))return qa_net_writer_fail(w,"Invalid KEX damage count");
    qa_net_write_u8(w,(uint8_t)n);
    for(size_t i=0;i<n;i++){
        if(d[i].damage>31)return qa_net_writer_fail(w,"KEX damage exceeds five bits");
        qa_net_write_u8(w,(uint8_t)(d[i].damage|(d[i].health?32u:0u)|(d[i].armor?64u:0u)|(d[i].shield?128u:0u)));
        if(!qa_q2_write_dir(w,d[i].direction))return false;
    }
    return !w->failed;
}
bool qa_q2_kex_read_poi(qa_net_reader *r,qa_q2_kex_poi *p) {
    if(!p)return qa_net_reader_fail(r,"Missing KEX POI output");
    p->key=qa_net_read_u16(r);p->time=qa_net_read_u16(r);qa_q2_read_vec3(r,p->position,true);
    p->image=qa_net_read_u16(r);p->color=qa_net_read_u8(r);p->flags=qa_net_read_u8(r);return !r->failed;
}
bool qa_q2_kex_write_poi(qa_net_writer *w,const qa_q2_kex_poi *p) {
    if(!p)return qa_net_writer_fail(w,"Missing KEX POI");
    qa_net_write_u16(w,p->key);qa_net_write_u16(w,p->time);qa_q2_write_vec3(w,p->position,true);
    qa_net_write_u16(w,p->image);qa_net_write_u8(w,p->color);qa_net_write_u8(w,p->flags);return !w->failed;
}
bool qa_q2_kex_read_help_path(qa_net_reader *r,qa_q2_kex_help_path *p) {
    if(!p)return qa_net_reader_fail(r,"Missing KEX help path output");
    p->start=qa_net_read_u8(r)!=0;return qa_q2_read_vec3(r,p->position,true)&&qa_q2_read_dir(r,p->direction);
}
bool qa_q2_kex_write_help_path(qa_net_writer *w,const qa_q2_kex_help_path *p) {
    if(!p)return qa_net_writer_fail(w,"Missing KEX help path");
    return qa_net_write_u8(w,p->start?1:0)&&qa_q2_write_vec3(w,p->position,true)&&qa_q2_write_dir(w,p->direction);
}
bool qa_q2_kex_read_muzzleflash(qa_net_reader *r,qa_q2_kex_muzzleflash *p) {
    if(!p)return qa_net_reader_fail(r,"Missing KEX muzzleflash output");
    p->entity=qa_net_read_i16(r);p->weapon=qa_net_read_u16(r);return !r->failed;
}
bool qa_q2_kex_write_muzzleflash(qa_net_writer *w,const qa_q2_kex_muzzleflash *p) {
    if(!p)return qa_net_writer_fail(w,"Missing KEX muzzleflash");
    return qa_net_write_i16(w,p->entity)&&qa_net_write_u16(w,p->weapon);
}
bool qa_q2_kex_read_locprint(qa_net_reader *r,qa_q2_kex_locprint *p) {
    if(!p)return qa_net_reader_fail(r,"Missing KEX localization output");
    p->flags=qa_net_read_u8(r);qa_net_read_string(r,p->base,sizeof(p->base));p->arg_count=qa_net_read_u8(r);
    if(p->arg_count>8)return qa_net_reader_fail(r,"KEX localization argument count exceeds eight");
    for(size_t i=0;i<p->arg_count;i++)qa_net_read_string(r,p->args[i],sizeof(p->args[i]));
    return !r->failed;
}
bool qa_q2_kex_write_locprint(qa_net_writer *w,const qa_q2_kex_locprint *p) {
    if(!p)return qa_net_writer_fail(w,"Missing KEX localization");
    if(p->arg_count>8)return qa_net_writer_fail(w,"KEX localization argument count exceeds eight");
    if(!memchr(p->base,0,sizeof(p->base)))return qa_net_writer_fail(w,"Unterminated KEX localization base");
    for(size_t i=0;i<p->arg_count;i++)
        if(!memchr(p->args[i],0,sizeof(p->args[i])))return qa_net_writer_fail(w,"Unterminated KEX localization argument");
    qa_net_write_u8(w,p->flags);qa_net_write_string(w,p->base);qa_net_write_u8(w,(uint8_t)p->arg_count);
    for(size_t i=0;i<p->arg_count;i++)qa_net_write_string(w,p->args[i]);
    return !w->failed;
}
bool qa_q2_kex_read_achievement(qa_net_reader *r,char *text,size_t capacity){return qa_net_read_string(r,text,capacity);}
bool qa_q2_kex_write_achievement(qa_net_writer *w,const char *text){return qa_net_write_string(w,text);}
bool qa_q2_kex_read_splitclient(qa_net_reader *r,uint8_t *seat){
    if(!seat)return qa_net_reader_fail(r,"Missing KEX split seat output");
    *seat=qa_net_read_u8(r);return !r->failed;
}
bool qa_q2_kex_write_splitclient(qa_net_writer *w,uint8_t seat){return qa_net_write_u8(w,seat);}
bool qa_q2_kex_read_sound(qa_q2_codec *c,qa_net_reader *r,qa_q2_kex_sound *s) {
    if(!c||!s)return qa_net_reader_fail(r,"Missing KEX sound codec or output");
    memset(s,0,sizeof(*s));s->flags=qa_net_read_u8(r);s->index=qa_net_read_u16(r);
    s->volume=(s->flags&1u)?(float)qa_net_read_u8(r)/255.0f:1.0f;
    s->attenuation=(s->flags&2u)?(float)qa_net_read_u8(r)/64.0f:1.0f;
    s->time_offset=(s->flags&16u)?(float)qa_net_read_u8(r)/1000.0f:0.0f;
    if(s->flags&8u){uint32_t ent=(s->flags&64u)?qa_net_read_u32(r):qa_net_read_u16(r);s->entity=ent>>3;s->channel=(uint8_t)(ent&7u);}
    s->has_position=(s->flags&4u)!=0;
    if(s->has_position)qa_q2_read_vec3(r,s->position,c->protocol.kind!=QA_NET_Q2KEX_DEMO_2022);
    return !r->failed;
}
bool qa_q2_kex_write_sound(qa_q2_codec *c,qa_net_writer *w,const qa_q2_kex_sound *s) {
    if(!c||!s)return qa_net_writer_fail(w,"Missing KEX sound codec or data");
    if(s->channel>7||s->entity>UINT32_MAX/8u)return qa_net_writer_fail(w,"KEX sound entity outside wire range");
    uint8_t flags=s->flags;
    if((flags&4u)&&!s->has_position)return qa_net_writer_fail(w,"Positioned KEX sound has no position");
    if(s->has_position)flags|=4u;
    if(s->entity||s->channel)flags|=8u;
    if(s->volume!=1.0f)flags|=1u;
    if(s->attenuation!=1.0f)flags|=2u;
    if(s->time_offset!=0.0f)flags|=16u;
    if((flags&8u)&&s->entity>8191u)flags|=64u;
    qa_net_write_u8(w,flags);qa_net_write_u16(w,s->index);
    if(flags&1u)qa_q2_write_scaled(w,s->volume,255.0f,8,false);
    if(flags&2u)qa_q2_write_scaled(w,s->attenuation,64.0f,8,false);
    if(flags&16u)qa_q2_write_scaled(w,s->time_offset,1000.0f,8,false);
    if(flags&8u){uint32_t ent=(s->entity<<3)|s->channel;if(flags&64u)qa_net_write_u32(w,ent);else qa_net_write_u16(w,(uint16_t)ent);}
    if(flags&4u)qa_q2_write_vec3(w,s->position,c->protocol.kind!=QA_NET_Q2KEX_DEMO_2022);
    return !w->failed;
}

static bool read_blast(qa_net_reader *r,size_t limit,qa_buffer *out) {
    uint16_t compressed=qa_net_read_u16(r);(void)qa_net_read_u16(r);
    if(!limit||limit>UINT_MAX)return qa_net_reader_fail(r,"Invalid KEX blast expansion budget");
    qa_bytes bytes;if(!qa_net_read_bytes(r,compressed,&bytes))return false;
    uint8_t *data=malloc(limit);if(!data)return qa_net_reader_fail(r,"KEX blast allocation failed");
    z_stream z={0};z.next_in=(Bytef *)bytes.data;z.avail_in=compressed;z.next_out=data;z.avail_out=(uInt)limit;
    int status=inflateInit(&z);
    if(status!=Z_OK){free(data);return qa_net_reader_fail(r,"KEX blast inflater initialization failed");}
    status=inflate(&z,Z_FINISH);size_t size=(size_t)z.total_out;
    bool valid=status==Z_STREAM_END&&z.total_in==compressed;inflateEnd(&z);
    if(!valid){free(data);return qa_net_reader_fail(r,"Malformed or oversized KEX blast");}
    out->data=data;out->size=size;return true;
}
bool qa_q2_kex_read_configblast(qa_net_reader *r,size_t limit,qa_q2_kex_config_fn fn,void *user) {
    if(!fn)return qa_net_reader_fail(r,"Missing KEX configblast callback");
    qa_buffer data={0};if(!read_blast(r,limit,&data))return false;
    qa_net_reader inner;qa_net_reader_init(&inner,(qa_bytes){data.data,data.size},r->error);
    bool ok=true;
    while(inner.bit/8<data.size&&!inner.failed){
        uint16_t index=qa_net_read_u16(&inner);size_t start=inner.bit/8;
        if(inner.failed){ok=false;break;}
        const uint8_t *end=memchr(data.data+start,0,data.size-start);
        if(!end){qa_net_reader_fail(&inner,"Unterminated KEX configblast string");ok=false;break;}
        inner.bit=((size_t)(end-data.data)+1)*8;
        if(!fn(user,index,(const char *)data.data+start,r->error)){ok=false;break;}
    }
    if(inner.failed)ok=false;
    free(data.data);
    if(!ok)return qa_net_reader_fail(r,"KEX configblast record rejected");
    return true;
}
bool qa_q2_kex_read_baselineblast(qa_q2_codec *c,qa_net_reader *r,size_t limit,qa_q2_kex_baseline_fn fn,void *user) {
    if(!c||!fn)return qa_net_reader_fail(r,"Missing KEX baselineblast codec or callback");
    qa_buffer data={0};if(!read_blast(r,limit,&data))return false;
    qa_net_reader inner;qa_net_reader_init(&inner,(qa_bytes){data.data,data.size},r->error);bool ok=true;
    while(inner.bit/8<data.size&&!inner.failed){
        uint32_t n;uint64_t b;qa_q2_entity zero={0},entity;
        if(!qa_q2_wide_read_header(c,&inner,&n,&b)||!read_entity(c,&inner,&zero,n,b,&entity)||!fn(user,&entity,r->error)){ok=false;break;}
    }
    if(inner.failed)ok=false;
    free(data.data);
    if(!ok)return qa_net_reader_fail(r,"KEX baselineblast record rejected");
    return true;
}
