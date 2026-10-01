/* id Software cg_players.c, GPL-2.0-or-later. */
#include "player_fx.h"
#include "events_internal.h"
#include "attachments.h"

enum { FX_WATER=8|16|32, FX_PLAYERSOLID=1|0x10000|0x2000000,
    FX_FLAG_RUN=34, FX_FLAG_STAND=35 };
typedef struct player_fx {
    const q3n_frame *frame;
    const qa_application_native_q3_entity *observation;
    const q3n_remote_entity *remote;
    const qa_q3_entity *state;
    q3n_entity *cent;
    q3n_client_info client;
    const q3n_media_view *media;
    const q3n_player_fx_settings *settings;
    const q3n_player_fx_backend *backend;
    const q3n_player_fx_remote_backend *remote_backend;
} player_fx;
static bool powered(const player_fx *p, uint32_t bit)
{ return ((uint32_t)p->state->powerups & (UINT32_C(1)<<bit))!=0; }
static bool current(const player_fx *p, qa_error *e)
{
    const q3n_frame *f=p->frame;
    if(!q3ne_current(f,e))return false;
    if(p->remote) {
        const q3n_client_info *ci=q3n_clients_get(f->clients,p->client.physical_client);
        return p->remote->frame==f->remote && q3n_remote_entity_current(p->remote) &&
            p->remote->presentation==p->cent && p->state==p->remote->current && ci && ci->info_valid &&
            ci->configstring_revision==p->client.configstring_revision && ci->media_revision==p->client.media_revision ? true :
            q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote player effects have a superseded cache or client media row");
    }
    qa_application_native_q3_entity actual;
    if(!qa_application_native_q3_presentation_entity(f->application,&f->source,p->cent->physical,&actual,e))return false;
    const q3n_client_info *ci=q3n_clients_get(f->clients,p->client.physical_client);
    return actual.present && p->cent->valid &&
        actual.binding.number==(int32_t)p->cent->physical && actual.state.number==(int32_t)p->cent->physical &&
        qa_actor_id_equal(actual.binding.actor,p->observation->binding.actor) &&
        qa_actor_id_equal(actual.binding.actor,p->cent->actor) &&
        actual.state.clientNum==p->observation->state.clientNum && ci && ci->info_valid &&
        ci->configstring_revision==p->client.configstring_revision && ci->media_revision==p->client.media_revision ? true :
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Native player effects have a superseded actor or client media row");
}
static qa_q3_ref_entity reference(qa_q3_ref_kind kind,int32_t handle)
{
    qa_q3_ref_entity r={.kind=kind};
    if(kind==QA_Q3_REF_MODEL)r.model=handle; else r.custom_shader=handle;
    return r;
}
static bool emit(player_fx *p,const qa_q3_ref_entity *r,qa_error *e)
{ return qa_q3_presentation_entity(p->frame->presentation,r,e) && current(p,e); }
static bool world_trace(player_fx *p,qa_vec3 start,qa_vec3 end,qa_bounds bounds,uint32_t mask,qa_trace_result *out,qa_error *e)
{ return (p->remote?p->remote_backend->world_trace(p->remote_backend->context,p->frame,start,end,bounds,mask,out,e):
    p->backend->world_trace(p->backend->context,p->frame,start,end,bounds,mask,out,e)) && current(p,e); }
static bool contents(player_fx *p,qa_vec3 point,uint32_t *out,qa_error *e)
{ return (p->remote?p->remote_backend->world_point_contents(p->remote_backend->context,p->frame,point,out,e):
    p->backend->world_point_contents(p->backend->context,p->frame,point,out,e)) && current(p,e); }
static bool sprite(player_fx *p,int32_t shader,qa_error *e)
{
    qa_q3_ref_entity r=reference(QA_Q3_REF_SPRITE,shader);
    memset(r.color,255,sizeof(r.color));
    r.origin=q3ne_sum(p->cent->lerp_origin,qa_v3(0,0,48)); r.radius=10;
    if(p->state->number==q3n_frame_snapshot_player(p->frame)->clientNum && !p->frame->third_person)r.flags=2;
    return emit(p,&r,e);
}
static bool sprites(player_fx *p,qa_error *e)
{
    static const uint32_t flags[]={0x2000,0x1000,0x8000,8,64,0x10000,0x20000,0x800};
    static const q3n_graphic shaders[]={Q3N_G_CONNECTION,Q3N_G_BALLOON,Q3N_G_MEDAL_IMPRESSIVE,
        Q3N_G_MEDAL_EXCELLENT,Q3N_G_MEDAL_GAUNTLET,Q3N_G_MEDAL_DEFEND,Q3N_G_MEDAL_ASSIST,Q3N_G_MEDAL_CAPTURE};
    uint32_t ef=(uint32_t)p->state->eFlags;
    for(unsigned i=0;i<8;++i)if(ef&flags[i])return sprite(p,p->media->graphics[shaders[i]],e);
    if(!(ef&1) && q3n_frame_snapshot_player(p->frame)->persistant[3]==p->client.team &&
        q3n_frame_game_type(p->frame)>=3 && p->settings->draw_friend)
        return sprite(p,p->media->graphics[Q3N_G_FRIEND],e);
    return true;
}
static bool shadow(player_fx *p,float *plane,bool *visible,qa_error *e)
{
    *plane=0; *visible=false;
    if(!p->settings->shadow_mode || powered(p,4))return true;
    qa_vec3 origin=p->cent->lerp_origin; qa_trace_result tr;
    qa_bounds bounds={{-15,-15,0},{15,15,2}};
    if(!world_trace(p,origin,q3ne_sum(origin,qa_v3(0,0,-128)),bounds,FX_PLAYERSOLID,&tr,e))return false;
    if(tr.fraction==1 || tr.start_solid || tr.all_solid)return true;
    *plane=q3ne_add(tr.end.z,1); *visible=true;
    if(p->settings->shadow_mode!=1)return true;
    float alpha=q3ne_add(1,-tr.fraction);
    q3n_impact_mark mark={.shader=p->media->graphics[Q3N_G_SHADOW_MARK],.origin=tr.end,
        .direction=tr.contact?tr.contact_plane.normal:tr.plane.normal,.orientation=p->cent->player.legs.yaw_angle,
        .color={alpha,alpha,alpha,1},.radius=24,.temporary=true};
    return q3n_marks_impact(p->frame,&mark,e) && current(p,e);
}
static bool splash(player_fx *p,qa_error *e)
{
    if(!p->settings->shadow_mode)return true;
    qa_vec3 origin=p->cent->lerp_origin,end=q3ne_sum(origin,qa_v3(0,0,-24)),start=q3ne_sum(origin,qa_v3(0,0,32));
    uint32_t c; if(!contents(p,end,&c,e))return false;
    if(!(c&FX_WATER))return true;
    if(!contents(p,start,&c,e))return false;
    if(c&(1|FX_WATER))return true;
    qa_trace_result tr;
    if(!world_trace(p,start,end,(qa_bounds){0},FX_WATER,&tr,e))return false;
    if(tr.fraction==1)return true;
    qa_q3_poly_vertex vertices[4]={
        {.position=q3ne_sum(tr.end,qa_v3(-32,-32,0)),.texcoord={0,0},.color={255,255,255,255}},
        {.position=q3ne_sum(tr.end,qa_v3(-32,32,0)),.texcoord={0,1},.color={255,255,255,255}},
        {.position=q3ne_sum(tr.end,qa_v3(32,32,0)),.texcoord={1,1},.color={255,255,255,255}},
        {.position=q3ne_sum(tr.end,qa_v3(32,-32,0)),.texcoord={1,0},.color={255,255,255,255}}};
    return qa_q3_presentation_poly(p->frame->presentation,p->media->graphics[Q3N_G_WAKE_MARK],vertices,4,e) && current(p,e);
}
static bool body_pass(player_fx *p,uint32_t part,qa_q3_ref_entity *r,bool base,qa_error *e)
{
    bool consumed=false;
    bool ok=p->remote?p->remote_backend->body_submit(p->remote_backend->context,p->frame,p->remote,part,r,base,&consumed,e):
        p->backend->body_submit(p->backend->context,p->frame,p->observation,part,r,base,&consumed,e);
    if(!ok || !current(p,e))return false;
    return consumed || emit(p,r,e);
}
static bool body_powerups(player_fx *p,uint32_t part,qa_q3_ref_entity *r,bool hidden,qa_error *e)
{
    if(hidden)return true;
    int32_t initial=r->custom_shader;
    if(powered(p,4)) { r->custom_shader=p->media->graphics[Q3N_G_INVIS]; return body_pass(p,part,r,r->custom_shader==initial,e); }
    if(!body_pass(p,part,r,true,e))return false;
    if(powered(p,1)) { r->custom_shader=p->media->graphics[p->client.team==1?Q3N_G_RED_QUAD:Q3N_G_QUAD];
        if(!body_pass(p,part,r,r->custom_shader==initial,e))return false; }
    if(powered(p,5) && (p->frame->time/100)%10==1) { r->custom_shader=p->media->graphics[Q3N_G_REGEN];
        if(!body_pass(p,part,r,r->custom_shader==initial,e))return false; }
    if(powered(p,2)) { r->custom_shader=p->media->graphics[Q3N_G_BATTLE_SUIT];
        if(!body_pass(p,part,r,r->custom_shader==initial,e))return false; }
    return true;
}
static bool write_dynamic(player_fx *p,const q3n_client_dynamic *dynamic,qa_error *e)
{
    if(!current(p,e))return false;
    bool ok=p->remote?q3n_clients_remote_dynamic_write(p->frame->clients,&p->frame->remote->source,
        p->client.physical_client,p->client.configstring_revision,p->client.media_revision,dynamic,e):
        q3n_clients_dynamic_write(p->frame->clients,p->frame->application,&p->frame->source,
        p->client.physical_client,p->client.configstring_revision,p->client.media_revision,dynamic,e);
    return ok && current(p,e);
}
static bool breath(player_fx *p,const qa_q3_ref_entity *head,qa_error *e)
{
    const q3n_frame *f=p->frame; const qa_q3_entity *s=p->state;
    if(!p->settings->enable_breath || (s->number==q3n_frame_snapshot_player(f)->clientNum && !f->third_person) || (s->eFlags&1))return true;
    /* CG_BreathPuffs indexes by entity number. A body queue corpse is outside
     * CS_PLAYERS and must never index an unrelated client continuation. */
    if(s->number<0 || s->number>=64)return true;
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)s->number);
    if(!ci || !ci->info_valid)return true;
    uint64_t config_revision=ci->configstring_revision,media_revision=ci->media_revision;
    uint32_t c; if(!contents(p,head->origin,&c,e))return false;
    ci=q3n_clients_get(f->clients,(uint32_t)s->number);
    if(!ci || !ci->info_valid || ci->configstring_revision!=config_revision || ci->media_revision!=media_revision)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Native breath has a superseded physical client continuation");
    if((c&FX_WATER) || ci->dynamic.breath_puff_time>f->time)return true;
    q3n_smoke smoke={.origin=q3ne_sum(q3ne_sum(head->origin,q3ne_scale(head->axis[0],8)),q3ne_scale(head->axis[2],-4)),
        .velocity={0,0,8},.radius=16,.color={1,1,1,0.66f},.duration=1500,
        .start_time=f->time,.fade_in_time=q3ne_plus(f->time,400),.flags=Q3N_LE_DONT_SCALE,
        .shader=p->media->graphics[Q3N_G_SHOTGUN_SMOKE]};
    q3n_effect_smoke(f,&smoke);
    q3n_client_dynamic dynamic=ci->dynamic; dynamic.breath_puff_time=q3ne_plus(f->time,2000);
    bool ok=p->remote?q3n_clients_remote_dynamic_write(f->clients,&f->remote->source,ci->physical_client,
        ci->configstring_revision,ci->media_revision,&dynamic,e):
        q3n_clients_dynamic_write(f->clients,f->application,&f->source,ci->physical_client,
        ci->configstring_revision,ci->media_revision,&dynamic,e);
    return ok && current(p,e);
}
static bool dust(player_fx *p,qa_error *e)
{
    const q3n_frame *f=p->frame; q3n_entity *cent=p->cent;
    if(!p->settings->enable_dust || cent->dust_trail_time>f->time)return true;
    int32_t animation=cent->player.legs.animation.animation_number&~128;
    if(animation!=19 && animation!=21)return true;
    cent->dust_trail_time=q3ne_plus(cent->dust_trail_time,40);
    if(cent->dust_trail_time<f->time)cent->dust_trail_time=f->time;
    qa_vec3 origin=q3ne_array(p->state->pos.base); qa_trace_result tr;
    if(!q3n_events_trace(f,origin,q3ne_sum(origin,qa_v3(0,0,-64)),(qa_bounds){0},p->state->number,FX_PLAYERSOLID,&tr,e) || !current(p,e))return false;
    if(!(tr.surface_flags&0x40000))return true;
    q3n_smoke smoke={.origin=q3ne_sum(origin,qa_v3(0,0,-16)),.velocity={0,0,-30},.radius=24,
        .color={0.8f,0.8f,0.7f,0.33f},.duration=500,.start_time=f->time,.shader=p->media->graphics[Q3N_G_DUST_PUFF]};
    q3n_effect_smoke(f,&smoke); return true;
}
static float sphere_angle(int32_t time,int32_t divisor)
{ return q3ne_div(q3ne_mul((float)((uint32_t)(time/divisor)&255u),q3ne_mul(3.14159265358979323846f,2)),255); }
static bool tokens(player_fx *p,int32_t flags,qa_error *e)
{
    q3n_player_fx_state *state=&p->cent->player_fx;
    int32_t count=p->state->generic1; if(count>10)count=10;
    if(count<0)return q3ne_fail(e,QA_ERROR_FORMAT,"Native Harvester token count is negative");
    if(!count) { state->skull_count=0; return true; }
    uint32_t previous=state->skull_count;
    uint32_t additions=(uint32_t)count>previous?(uint32_t)count-previous:0;
    for(uint32_t i=0;i<additions;++i) {
        for(uint32_t j=previous;j>0;--j)state->skull_positions[j]=state->skull_positions[j-1];
        state->skull_positions[0]=p->cent->lerp_origin;
    }
    state->skull_count=(uint32_t)count; qa_vec3 origin=p->cent->lerp_origin;
    for(uint32_t i=0;i<state->skull_count;++i) {
        qa_vec3 delta=q3ne_difference(state->skull_positions[i],origin);
        if(q3ne_length(delta)>30)state->skull_positions[i]=q3ne_sum(origin,q3ne_scale(q3ne_normalize(delta),30));
        origin=state->skull_positions[i];
    }
    qa_q3_ref_entity skull=reference(QA_Q3_REF_MODEL,p->media->graphics[p->client.team==2?Q3N_G_RED_CUBE:Q3N_G_BLUE_CUBE]);
    skull.flags=flags; origin=p->cent->lerp_origin;
    for(uint32_t i=0;i<state->skull_count;++i) {
        qa_vec3 position=state->skull_positions[i],delta=q3ne_difference(origin,position);
        skull.axis[0]=q3ne_normalize(qa_v3(delta.x,delta.y,0)); skull.axis[2]=qa_v3(0,0,1);
        skull.axis[1]=q3ne_cross(skull.axis[0],skull.axis[2]);
        float angle=sphere_angle(q3ne_plus(p->frame->time,5000-(int32_t)i*500),16);
        skull.origin=q3ne_sum(position,qa_v3(0,0,q3ne_mul((float)sin((double)angle),10)));
        if(!emit(p,&skull,e))return false; origin=position;
    }
    return true;
}
static void orbit_axis(qa_q3_ref_entity *skull,qa_vec3 direction)
{
    skull->axis[1]=q3ne_normalize(qa_v3(direction.x,direction.y,0)); skull->axis[2]=qa_v3(0,0,1);
    skull->axis[0]=q3ne_cross(skull->axis[1],skull->axis[2]);
}
static bool skull_pair(player_fx *p,qa_q3_ref_entity *skull,bool flip,qa_error *e)
{
    skull->model=p->media->graphics[Q3N_G_KAMIKAZE_HEAD]; if(!emit(p,skull,e))return false;
    if(flip)skull->axis[1]=q3ne_scale(skull->axis[1],-1);
    skull->model=p->media->graphics[Q3N_G_KAMIKAZE_TRAIL]; return emit(p,skull,e);
}
static bool kamikaze(player_fx *p,const qa_q3_ref_entity *torso,qa_error *e)
{
    const float pi=3.14159265358979323846f,two_pi=q3ne_mul(pi,2);
    qa_q3_ref_entity skull=reference(QA_Q3_REF_MODEL,0);
    skull.lighting_origin=p->cent->lerp_origin; skull.shadow_plane=torso->shadow_plane; skull.flags=torso->flags;
    int32_t time=p->frame->time; float angle; qa_vec3 direction;
    if(p->state->eFlags&1) {
        angle=sphere_angle(time,7); if(angle>two_pi)angle=q3ne_add(angle,-two_pi);
        float x=q3ne_mul((float)sin((double)angle),20),y=q3ne_mul((float)cos((double)angle),20);
        angle=sphere_angle(time,4); direction=qa_v3(x,y,q3ne_add(15,q3ne_mul((float)sin((double)angle),8)));
        skull.origin=q3ne_sum(torso->origin,direction); orbit_axis(&skull,direction); return skull_pair(p,&skull,false,e);
    }
    angle=sphere_angle(time,4);
    direction=qa_v3(q3ne_mul((float)cos((double)angle),20),q3ne_mul((float)sin((double)angle),20),q3ne_mul((float)cos((double)angle),20));
    skull.origin=q3ne_sum(torso->origin,direction);
    float yaw=q3ne_add(q3ne_div(q3ne_mul(angle,180),pi),90); if(yaw>360)yaw=q3ne_add(yaw,-360);
    q3n_angles_axis(qa_v3(q3ne_mul((float)sin((double)angle),30),yaw,0),skull.axis);
    if(!skull_pair(p,&skull,true,e))return false;
    angle=q3ne_add(sphere_angle(time,4),pi); if(angle>two_pi)angle=q3ne_add(angle,-two_pi);
    direction=qa_v3(q3ne_mul((float)sin((double)angle),20),q3ne_mul((float)cos((double)angle),20),q3ne_mul((float)cos((double)angle),20));
    skull.origin=q3ne_sum(torso->origin,direction);
    yaw=q3ne_add(360,-q3ne_div(q3ne_mul(angle,180),pi)); if(yaw>360)yaw=q3ne_add(yaw,-360);
    q3n_angles_axis(qa_v3(q3ne_mul((float)cos((double)q3ne_add(angle,-q3ne_mul(0.5f,pi))),30),yaw,0),skull.axis);
    if(!skull_pair(p,&skull,false,e))return false;
    angle=q3ne_add(sphere_angle(time,3),q3ne_mul(0.5f,pi)); if(angle>two_pi)angle=q3ne_add(angle,-two_pi);
    direction=qa_v3(q3ne_mul((float)sin((double)angle),20),q3ne_mul((float)cos((double)angle),20),0);
    skull.origin=q3ne_sum(torso->origin,direction); orbit_axis(&skull,direction); return skull_pair(p,&skull,false,e);
}
static bool mission(player_fx *p,const qa_q3_ref_entity *torso,qa_error *e)
{
    if((p->state->eFlags&0x200) && !kamikaze(p,torso,e))return false;
    static const uint32_t bits[]={11,10,12,13};
    static const q3n_graphic models[]={Q3N_G_GUARD_PLAYER,Q3N_G_SCOUT_PLAYER,Q3N_G_DOUBLER_PLAYER,Q3N_G_AMMOREGEN_PLAYER};
    for(unsigned i=0;i<4;++i)if(powered(p,bits[i])) {
        qa_q3_ref_entity r=*torso; r.model=p->media->graphics[models[i]]; r.frame=r.old_frame=r.custom_skin=0;
        if(!emit(p,&r,e))return false;
    }
    q3n_client_dynamic dynamic=q3n_clients_get(p->frame->clients,p->client.physical_client)->dynamic;
    bool invulnerable=powered(p,14); int32_t time=p->frame->time;
    if(invulnerable) { if(!dynamic.invulnerability_start_time)dynamic.invulnerability_start_time=time;
        dynamic.invulnerability_stop_time=time; } else dynamic.invulnerability_start_time=0;
    if(!write_dynamic(p,&dynamic,e))return false;
    int32_t start=q3ne_sub(time,dynamic.invulnerability_start_time),stop=q3ne_sub(time,dynamic.invulnerability_stop_time);
    if(invulnerable || stop<250) {
        qa_q3_ref_entity r=*torso; r.model=p->media->graphics[Q3N_G_INVULNERABILITY_PLAYER];
        r.custom_skin=0; r.flags&=~2; r.origin=p->cent->lerp_origin;
        float scale=start<250?q3ne_div((float)start,250):stop<250?q3ne_div((float)q3ne_sub(250,stop),250):1;
        r.axis[0]=qa_v3(scale,0,0); r.axis[1]=qa_v3(0,scale,0); r.axis[2]=qa_v3(0,0,scale);
        if(!emit(p,&r,e))return false;
    }
    int32_t elapsed=q3ne_sub(time,dynamic.medkit_usage_time);
    if(dynamic.medkit_usage_time && elapsed<500) {
        qa_q3_ref_entity r=*torso; r.model=p->media->graphics[Q3N_G_MEDKIT_USAGE]; r.custom_skin=0; r.flags&=~2;
        q3ne_identity(r.axis); r.origin=q3ne_sum(p->cent->lerp_origin,qa_v3(0,0,q3ne_add(-24,q3ne_div(q3ne_mul((float)elapsed,80),500))));
        uint8_t color=elapsed>400?q3ne_byte(q3ne_add(255,-q3ne_div(q3ne_mul((float)q3ne_sub(elapsed,1000),255),100))):255;
        memset(r.color,color,4); if(!emit(p,&r,e))return false;
    }
    return true;
}
static float angle_subtract(float a,float b)
{ float d=q3ne_add(a,-b); while(d>180)d=q3ne_add(d,-360); while(d< -180)d=q3ne_add(d,360); return d; }
static float angle_mod(float a)
{ return q3ne_mul((float)((uint32_t)q3ne_int(q3ne_mul(a,65536.0f/360.0f))&65535u),360.0f/65536.0f); }
static void flag_swing(q3n_player_fx_state *s,float destination,int32_t milliseconds)
{
    if(!s->flag_yawing && fabsf(angle_subtract(s->flag_yaw,destination))>25)s->flag_yawing=true;
    if(!s->flag_yawing)return;
    float d=angle_subtract(destination,s->flag_yaw),distance=fabsf(d),scale=distance<12.5f?0.5f:distance<25?1:2;
    float move=q3ne_mul(q3ne_mul((float)milliseconds,scale),d>=0?0.15f:-0.15f);
    if((d>=0 && move>=d) || (d<0 && move<=d)) { move=d; s->flag_yawing=false; }
    s->flag_yaw=angle_mod(q3ne_add(s->flag_yaw,move)); d=angle_subtract(destination,s->flag_yaw);
    if(d>90)s->flag_yaw=angle_mod(q3ne_add(destination,-89));
    else if(d< -90)s->flag_yaw=angle_mod(q3ne_add(destination,89));
}
static bool flag(player_fx *p,int32_t skin,const qa_q3_ref_entity *torso,qa_error *e)
{
    qa_q3_ref_entity pole=reference(QA_Q3_REF_MODEL,p->media->graphics[Q3N_G_FLAG_POLE]);
    pole.lighting_origin=torso->lighting_origin; pole.shadow_plane=torso->shadow_plane; pole.flags=torso->flags;
    if(!q3n_attach(p->frame->assets,&pole,torso,"tag_flag",false,e) || !current(p,e) || !emit(p,&pole,e))return false;
    qa_q3_ref_entity r=reference(QA_Q3_REF_MODEL,p->media->graphics[Q3N_G_FLAG_FLAP]);
    r.custom_skin=skin; r.lighting_origin=torso->lighting_origin; r.shadow_plane=torso->shadow_plane; r.flags=torso->flags;
    int32_t animation=p->state->legsAnim&~128;
    bool idle=animation==22 || animation==23,walk=animation==13 || animation==14;
    q3n_player_fx_state *s=&p->cent->player_fx;
    if(!idle) {
        qa_vec3 d=q3ne_normalize(q3ne_sum(q3ne_array(p->state->pos.delta),qa_v3(0,0,100)));
        if(fabsf(q3ne_dot(pole.axis[2],d))<0.9f) {
            float a=q3ne_f((float)acos((double)fmaxf(-1,fminf(1,q3ne_dot(pole.axis[0],d)))));
            float degrees=q3ne_div(q3ne_mul(a,180),3.14159265358979323846f);
            float yaw=q3ne_dot(pole.axis[1],d)<0?q3ne_add(360,-degrees):degrees;
            if(yaw<0)yaw=q3ne_add(yaw,360); if(yaw>360)yaw=q3ne_add(yaw,-360);
            flag_swing(s,yaw,p->frame->frame_milliseconds);
        }
    }
    if(!q3n_lerp_run(&p->client.animations,&s->flag,idle||walk?FX_FLAG_STAND:FX_FLAG_RUN,p->frame->time,1,p->settings->animations_disabled,e))return false;
    r.old_frame=s->flag.old_frame; r.frame=s->flag.frame; r.back_lerp=s->flag.back_lerp;
    q3n_angles_axis(qa_v3(0,s->flag_yaw,0),r.axis);
    return q3n_attach(p->frame->assets,&r,&pole,"tag_flag",true,e) && current(p,e) && emit(p,&r,e);
}
static bool light(player_fx *p,qa_vec3 color,qa_error *e)
{
    float radius=(float)(200+(q3n_events_rand(p->frame->events)&31));
    return qa_q3_presentation_light(p->frame->presentation,p->cent->lerp_origin,radius,color,false,e) && current(p,e);
}
static bool powerups(player_fx *p,const qa_q3_ref_entity *torso,qa_error *e)
{
    if(!p->state->powerups)return true;
    if(powered(p,1) && !light(p,qa_v3(0.2f,0.2f,1),e))return false;
    if(powered(p,6) && (!qa_q3_presentation_loop(p->frame->presentation,p->media->sounds[Q3N_S_FLIGHT],
        p->state->number,p->cent->lerp_origin,qa_v3(0,0,0),false,e) || !current(p,e)))return false;
    static const uint32_t bits[]={7,8,9};
    static const q3n_graphic models[]={Q3N_G_RED_FLAG,Q3N_G_BLUE_FLAG,Q3N_G_NEUTRAL_FLAG};
    static const q3n_graphic skins[]={Q3N_G_RED_FLAG_SKIN,Q3N_G_BLUE_FLAG_SKIN,Q3N_G_NEUTRAL_FLAG_SKIN};
    static const qa_vec3 colors[]={{1,0.2f,0.2f},{0.2f,0.2f,1},{1,1,1}};
    for(unsigned i=0;i<3;++i)if(powered(p,bits[i])) {
        if(p->client.new_anims) { if(!flag(p,p->media->graphics[skins[i]],torso,e))return false; }
        else {
            qa_q3_ref_entity r=reference(QA_Q3_REF_MODEL,p->media->graphics[models[i]]); qa_vec3 axis[3];
            q3n_angles_axis(qa_v3(0,p->cent->lerp_angles.y,0),axis);
            r.origin=q3ne_sum(q3ne_sum(p->cent->lerp_origin,q3ne_scale(axis[0],-16)),qa_v3(0,0,16));
            q3n_angles_axis(qa_v3(0,q3ne_add(p->cent->lerp_angles.y,90),0),r.axis);
            if(!emit(p,&r,e))return false;
        }
        if(!light(p,colors[i],e))return false;
    }
    if(powered(p,3) && p->cent->trail_time<=p->frame->time) {
        int32_t animation=p->cent->player.legs.animation.animation_number&~128;
        if(animation==15 || animation==16) {
            p->cent->trail_time=q3ne_plus(p->cent->trail_time,100);
            if(p->cent->trail_time<p->frame->time)p->cent->trail_time=p->frame->time;
            q3n_smoke smoke={.origin=q3ne_sum(p->cent->lerp_origin,qa_v3(0,0,-16)),.velocity={0,0,0},.radius=8,
                .color={1,1,1,1},.duration=500,.start_time=p->frame->time,.shader=p->media->graphics[Q3N_G_HASTE_PUFF]};
            q3n_effect_smoke(p->frame,&smoke)->type=Q3N_LE_SCALE_FADE;
        }
    }
    return true;
}
static bool submit(player_fx *p,q3n_player_body *body,qa_error *e)
{
    bool hidden=false;
    bool ok=p->remote?p->remote_backend->body_hidden(p->remote_backend->context,p->frame,p->remote,&hidden,e):
        p->backend->body_hidden(p->backend->context,p->frame,p->observation,&hidden,e);
    if(!ok || !current(p,e))return false;
    float plane=0; bool visible=false;
    if(!hidden && (!sprites(p,e) || !shadow(p,&plane,&visible,e) || !splash(p,e)))return false;
    for(uint32_t i=0;i<body->count;++i) {
        body->parts[i].shadow_plane=plane; body->parts[i].flags&=~256;
        if(p->settings->shadow_mode==3 && visible)body->parts[i].flags|=256;
    }
    if(q3n_frame_product(p->frame)==QA_Q3_TEAM_ARENA && q3n_frame_game_type(p->frame)==7 && !tokens(p,body->parts[0].flags,e))return false;
    if(!body_powerups(p,0,&body->parts[0],hidden,e))return false;
    if(body->count<2)return true;
    if(!body_powerups(p,1,&body->parts[1],hidden,e))return false;
    if(q3n_frame_product(p->frame)==QA_Q3_TEAM_ARENA && !mission(p,&body->parts[1],e))return false;
    if(body->count<3)return true;
    if(!body_powerups(p,2,&body->parts[2],hidden,e))return false;
    if(!hidden && q3n_frame_product(p->frame)==QA_Q3_TEAM_ARENA && (!breath(p,&body->parts[2],e) || !dust(p,e)))return false;
    ok=p->remote?p->remote_backend->player_weapon(p->remote_backend->context,p->frame,p->remote,&body->parts[1],p->client.team,e):
        p->backend->player_weapon(p->backend->context,p->frame,p->observation,p->cent,&body->parts[1],p->client.team,e);
    if(!ok || !current(p,e))return false;
    return powerups(p,&body->parts[1],e);
}
bool q3n_player_fx_submit(const q3n_frame *f,const qa_application_native_q3_entity *actual,
    q3n_entity *cent,const q3n_client_info *client,q3n_player_body *body,
    const q3n_player_fx_settings *settings,const q3n_player_fx_backend *backend,qa_error *e)
{
    if(!f || f->remote || !actual || !actual->present || !cent || !client || !body || body->count>3 || !settings ||
        !backend || !backend->world_trace || !backend->world_point_contents || !backend->body_hidden ||
        !backend->body_submit || !backend->player_weapon || actual->state.clientNum<0 || actual->state.clientNum>=64 ||
        client->physical_client!=(uint32_t)actual->state.clientNum)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Native player effects require genuine authored body and backend callbacks");
    if(!body->count || !client->info_valid)return true;
    player_fx p={.frame=f,.observation=actual,.state=&actual->state,.cent=cent,.client=*client,
        .media=q3n_media_read(f->media),.settings=settings,.backend=backend};
    if(!f->has_local_player)return q3ne_fail(e,QA_ERROR_ARGUMENT,"CG_Player requires its actual viewing playerstate");
    if(!current(&p,e))return false;
    bool own_lease=!f->events->busy; if(own_lease)f->events->busy=true;
    bool ok=submit(&p,body,e);
    if(own_lease)f->events->busy=false;
    return ok;
}
bool q3n_player_fx_submit_remote(const q3n_frame *f,const q3n_remote_entity *actual,
    const q3n_client_info *client,q3n_player_body *body,const q3n_player_fx_settings *settings,
    const q3n_player_fx_remote_backend *backend,qa_error *e)
{
    if(!f || !f->remote || !actual || actual->frame!=f->remote || !actual->current ||
       !actual->presentation || (!actual->predicted && (!actual->published || !actual->current_valid)) ||
       !client || !body || body->count>3 || !settings ||
       !backend || !backend->world_trace || !backend->world_point_contents || !backend->body_hidden ||
       !backend->body_submit || !backend->player_weapon || actual->current->clientNum<0 ||
       actual->current->clientNum>=64 || client->physical_client!=(uint32_t)actual->current->clientNum)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote player effects require their actual cache/body/backend receipt");
    if(!body->count || !client->info_valid)return true;
    if(!q3n_frame_snapshot_player(f))return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote CG_Player requires its actual snapshot playerstate");
    player_fx p={.frame=f,.remote=actual,.state=actual->current,.cent=actual->presentation,
        .client=*client,.media=q3n_media_read(f->media),.settings=settings,.remote_backend=backend};
    if(!current(&p,e))return false;
    bool own_lease=!f->events->busy; if(own_lease)f->events->busy=true;
    bool ok=submit(&p,body,e);
    if(own_lease)f->events->busy=false;
    return ok;
}
