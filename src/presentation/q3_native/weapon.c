#include "weapon.h"
#include "events.h"
#include "marks.h"
#include "trajectory.h"
#include "attachments.h"
#include "../q3/internal.h"
#include "qa/source_save.h"

struct q3n_weapons {
    q3n_weapon_options options;
    q3n_weapon_selection selection;
    bool busy;
};
static float add(float a,float b) { volatile float result=a+b; return result; }
static float mul(float a,float b) { volatile float result=a*b; return result; }
static float divide(float a,float b) { volatile float result=a/b; return result; }
static int32_t integer(float x) { return x>=-2147483648.0f && x<2147483648.0f ? (int32_t)x : INT32_MIN; }
static int32_t difference(int32_t a,int32_t b) { uint32_t bits=(uint32_t)a-(uint32_t)b; int32_t result; memcpy(&result,&bits,4); return result; }
static int32_t sum(int32_t a,int32_t b) { uint32_t bits=(uint32_t)a+(uint32_t)b; int32_t result; memcpy(&result,&bits,4); return result; }
static qa_vec3 plus(qa_vec3 a,qa_vec3 b) { return qa_v3(add(a.x,b.x),add(a.y,b.y),add(a.z,b.z)); }
static qa_vec3 scale(qa_vec3 a,float x) { return qa_v3(mul(a.x,x),mul(a.y,x),mul(a.z,x)); }
static qa_vec3 ma(qa_vec3 a,float x,qa_vec3 b) { return plus(a,scale(b,x)); }
static qa_vec3 minus(qa_vec3 a,qa_vec3 b) { return plus(a,scale(b,-1)); }
static float dot(qa_vec3 a,qa_vec3 b) { return add(add(mul(a.x,b.x),mul(a.y,b.y)),mul(a.z,b.z)); }
static float length(qa_vec3 a) { return sqrtf(dot(a,a)); }
static qa_vec3 normalized(qa_vec3 a) { float size=length(a); return size?scale(a,divide(1,size)):a; }
static qa_vec3 transform(qa_vec3 v,const qa_vec3 axis[3]) { return plus(plus(scale(axis[0],v.x),scale(axis[1],v.y)),scale(axis[2],v.z)); }
static void vector(float out[3],qa_vec3 value) { out[0]=value.x; out[1]=value.y; out[2]=value.z; }
static qa_vec3 from(const float value[3]) { return qa_v3(value[0],value[1],value[2]); }
static void identity(qa_vec3 axis[3]) { axis[0]=qa_v3(1,0,0); axis[1]=qa_v3(0,1,0); axis[2]=qa_v3(0,0,1); }
static qa_q3_ref_entity reference(qa_q3_ref_kind kind,int32_t model)
{
    return (qa_q3_ref_entity){.kind=kind,.model=model};
}
static bool frame_valid(const q3n_frame *f,qa_error *e)
{
    return f && f->weapons && f->assets==f->weapons->options.assets && f->source.product==f->weapons->options.product &&
        f->media && f->events && f->presentation && f->weapon_settings && f->entities &&
        qa_application_native_q3_presentation_current(f->application,&f->source) ? true :
        q3p_fail(e,QA_ERROR_ARGUMENT,"Native weapon needs its actual current source frame");
}
static bool emit(const q3n_frame *f,const qa_q3_ref_entity *ref,qa_error *e)
{ return qa_q3_presentation_entity(f->presentation,ref,e) && frame_valid(f,e); }
static bool start_sound(const q3n_frame *f,int32_t handle,const qa_vec3 *origin,int32_t number,int32_t channel,qa_error *e)
{ return qa_q3_presentation_sound(f->presentation,handle,origin,number,channel,false,e) && frame_valid(f,e); }
static bool loop_sound(const q3n_frame *f,int32_t handle,int32_t number,qa_vec3 origin,qa_error *e)
{ return qa_q3_presentation_loop(f->presentation,handle,number,origin,qa_v3(0,0,0),false,e) && frame_valid(f,e); }
static bool attach(const q3n_frame *f,qa_q3_ref_entity *child,const qa_q3_ref_entity *parent,const char *tag,bool rotated,qa_error *e)
{ return q3n_attach(f->assets,child,parent,tag,rotated,e) && frame_valid(f,e); }
bool q3n_weapons_idle(const q3n_weapons *w) { return w && !w->busy; }
bool q3n_weapons_create(const q3n_weapon_options *options,q3n_weapons **out,qa_error *e)
{
    if (!options || !options->assets || !out || (options->product!=QA_Q3_ARENA && options->product!=QA_Q3_TEAM_ARENA))
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Native weapons require their real product and backend");
    q3n_weapons *w=calloc(1,sizeof(*w));
    if (!w) return q3p_fail(e,QA_ERROR_MEMORY,"Allocating native weapon continuation");
    w->options=*options; w->selection.weapon=2; *out=w; return true;
}
void q3n_weapons_destroy(q3n_weapons *w) { if (q3n_weapons_idle(w)) free(w); }
const q3n_weapon_selection *q3n_weapons_selection(const q3n_weapons *w)
{ return q3n_weapons_idle(w)?&w->selection:NULL; }
void q3n_weapons_set_selected(q3n_weapons *w,int32_t weapon,int32_t time)
{ if (q3n_weapons_idle(w) && weapon>=0 && weapon<16) w->selection=(q3n_weapon_selection){weapon,time}; }
static uint32_t owned(const q3n_frame *f)
{ return (uint32_t)f->local_player.stats[f->source.product==QA_Q3_ARENA?2:3]; }
static bool selectable(const q3n_frame *f,int32_t weapon)
{ return f->local_player.ammo[weapon]!=0 && (owned(f)&(1u<<(uint32_t)weapon))!=0; }
void q3n_weapons_select(const q3n_frame *f,int32_t weapon)
{
    if (!f || !q3n_weapons_idle(f->weapons) || !f->has_local_player ||
        !qa_application_native_q3_presentation_current(f->application,&f->source) ||
        (f->local_player.pmFlags&4096) || weapon<1 || weapon>15) return;
    f->weapons->selection.time=f->time;
    if (owned(f)&(1u<<(uint32_t)weapon)) f->weapons->selection.weapon=weapon;
}
void q3n_weapons_cycle(const q3n_frame *f,int32_t direction)
{
    if (!f || !q3n_weapons_idle(f->weapons) || !f->has_local_player ||
        !qa_application_native_q3_presentation_current(f->application,&f->source) ||
        (f->local_player.pmFlags&4096) || (direction!=1 && direction!=-1)) return;
    q3n_weapon_selection *selection=&f->weapons->selection; selection->time=f->time;
    int32_t original=selection->weapon;
    for (int32_t i=0;i<16;++i) {
        selection->weapon=(selection->weapon+direction+16)%16;
        if (selection->weapon!=1 && selectable(f,selection->weapon)) return;
    }
    selection->weapon=original;
}
bool q3n_weapons_out_of_ammo(const q3n_frame *f,qa_error *e)
{
    if (!frame_valid(f,e) || !f->has_local_player) return q3p_fail(e,QA_ERROR_ARGUMENT,"CG_WeaponSelectable: cg.snap == NULL");
    f->weapons->selection.time=f->time;
    for (int32_t i=15;i>0;--i) if (selectable(f,i)) { f->weapons->selection.weapon=i; break; }
    return true;
}
static bool weapon(const q3n_frame *f,int32_t number,bool ready,const q3n_weapon_media **out,qa_error *e)
{
    const q3n_media_view *media=q3n_media_read(f->media);
    if (!media || number<0 || number>=16 || (ready && number && !media->weapons[number].ready))
        return q3p_fail(e,QA_ERROR_FORMAT,"Native weapon media must finish registration before presentation");
    *out=&media->weapons[number]; return true;
}
static bool brass(const q3n_frame *f,q3n_entity *cent,q3n_brass kind,qa_error *e)
{
    qa_vec3 axis[3]; q3n_angles_axis(cent->lerp_angles,axis);
    const q3n_media_view *media=q3n_media_read(f->media);
    if (kind==Q3N_BRASS_NAILGUN) {
        q3n_smoke puff={.origin=plus(cent->lerp_origin,transform(qa_v3(0,-12,24),axis)),.velocity={0,0,64},
            .radius=32,.color={1,1,1,.33f},.duration=700,.start_time=f->time,.shader=media->graphics[Q3N_G_SMOKE_PUFF]};
        q3n_effect_smoke(f,&puff)->type=Q3N_LE_SCALE_FADE; return true;
    }
    int32_t duration=f->weapon_settings->brass_time;
    if (duration<=0) return true;
    bool shotgun=kind==Q3N_BRASS_SHOTGUN;
    for (int32_t i=0;i<(shotgun?2:1);++i) {
        q3n_local_entity *le=q3n_local_allocate(f->events,Q3N_LE_FRAGMENT,QA_Q3_REF_MODEL);
        le->ref.model=media->graphics[shotgun?Q3N_G_SHOTGUN_BRASS:Q3N_G_MACHINEGUN_BRASS];
        qa_vec3 velocity={0};
        if (shotgun) velocity.x=add(60,mul(60,q3n_events_crandom(f->events)));
        velocity.y=add(shotgun?(i==0?40:-40):-50,mul(shotgun?10:40,q3n_events_crandom(f->events)));
        velocity.z=add(100,mul(50,q3n_events_crandom(f->events)));
        le->start_time=f->time;
        uint32_t duration_bits=(uint32_t)duration*(shotgun?3u:1u); int32_t initial; memcpy(&initial,&duration_bits,4);
        le->end_time=integer(add((float)sum(f->time,initial),mul((float)(shotgun?duration:duration/4),q3n_events_random(f->events))));
        le->pos.type=5; le->pos.time=shotgun?f->time:difference(f->time,q3n_events_rand(f->events)&15);
        le->ref.origin=plus(cent->lerp_origin,transform(qa_v3(8,shotgun?0:-4,24),axis));
        uint32_t contents;
        if (!q3n_events_point_contents(f,le->ref.origin,-1,&contents,e)) return false;
        float water=(contents&32)?.1f:1;
        vector(le->pos.base,le->ref.origin); vector(le->pos.delta,scale(transform(velocity,axis),water));
        identity(le->ref.axis); le->bounce_factor=shotgun?.3f:mul(.4f,water);
        le->angles.type=2; le->angles.time=f->time;
        for (size_t j=0;j<3;++j) le->angles.base[j]=(float)(q3n_events_rand(f->events)&31);
        vector(le->angles.delta,shotgun?qa_v3(1,.5f,0):qa_v3(2,1,0));
        le->flags=Q3N_LE_TUMBLE; le->bounce_sound=Q3N_BOUNCE_BRASS; le->mark=Q3N_MARK_NONE;
    }
    return true;
}
bool q3n_weapons_fire(const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,qa_error *e)
{
    if (!frame_valid(f,e) || !cent || !state) return false;
    if (!state->weapon) return true;
    if (state->weapon<0 || state->weapon>=(f->source.product==QA_Q3_ARENA?11:14))
        return q3p_fail(e,QA_ERROR_FORMAT,"CG_FireWeapon: ent->weapon >= WP_NUM_WEAPONS");
    const q3n_weapon_media *w;
    if (!weapon(f,state->weapon,false,&w,e)) return false;
    cent->muzzle_flash_time=f->time;
    if (state->weapon==6 && cent->lightning_firing) return true;
    const q3n_media_view *media=q3n_media_read(f->media);
    if ((state->powerups&2) && !start_sound(f,media->sounds[Q3N_S_QUAD],NULL,state->number,4,e)) return false;
    size_t count=0; while (count<4 && w->flash_sounds[count]) ++count;
    if (count) {
        size_t index=(size_t)(uint32_t)q3n_events_rand(f->events)%count;
        if (!start_sound(f,w->flash_sounds[index],NULL,state->number,2,e)) return false;
    }
    return w->eject_brass==Q3N_BRASS_NONE || f->weapon_settings->brass_time<=0 || brass(f,cent,w->eject_brass,e);
}
static bool source_entity(const q3n_frame *f,int32_t number,qa_application_native_q3_entity *out,qa_error *e)
{
    if (number<0 || number>=1024 || (uint32_t)number>=f->source.entity_count)
        return q3p_fail(e,QA_ERROR_FORMAT,"Native weapon source entity number is out of range");
    return qa_application_native_q3_presentation_entity(f->application,&f->source,(uint32_t)number,out,e);
}
static bool lightning(const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,qa_vec3 origin,qa_error *e)
{
    if (state->weapon!=6) return true;
    qa_vec3 angles=cent->lerp_angles;
    float true_lightning=f->weapon_settings->true_lightning;
    if (f->has_local_player && state->number==f->local_player.clientNum && true_lightning!=0) {
        float *actual[3]={&angles.x,&angles.y,&angles.z}; const float view[3]={f->view_angles.x,f->view_angles.y,f->view_angles.z};
        for (size_t i=0;i<3;++i) {
            float delta=add(*actual[i],-view[i]); if (delta>180) delta=add(delta,-360); if (delta< -180) delta=add(delta,360);
            *actual[i]=add(view[i],mul(delta,add(1,-true_lightning)));
            if (*actual[i]<0) *actual[i]=add(*actual[i],360); if (*actual[i]>360) *actual[i]=add(*actual[i],-360);
        }
    }
    qa_vec3 axis[3]; q3n_angles_axis(angles,axis);
    qa_vec3 muzzle=ma(plus(cent->lerp_origin,qa_v3(0,0,26)),14,axis[0]);
    qa_trace_result trace; qa_bounds bounds={0};
    if (!q3n_events_trace(f,muzzle,ma(muzzle,768,axis[0]),bounds,state->number,1u|0x2000000u|0x4000000u,&trace,e)) return false;
    const q3n_media_view *media=q3n_media_read(f->media);
    qa_q3_ref_entity beam=reference(QA_Q3_REF_LIGHTNING,0);
    beam.origin=origin; beam.old_origin=trace.end; beam.custom_shader=media->graphics[Q3N_G_LIGHTNING_SHADER];
    if (!emit(f,&beam,e)) return false;
    if (trace.fraction<1) {
        qa_q3_ref_entity hit=reference(QA_Q3_REF_MODEL,media->graphics[Q3N_G_LIGHTNING_EXPLOSION]);
        hit.origin=ma(trace.end,-16,normalized(minus(beam.old_origin,beam.origin)));
        qa_vec3 random_angles;
        random_angles.x=(float)(q3n_events_rand(f->events)%360);
        random_angles.y=(float)(q3n_events_rand(f->events)%360);
        random_angles.z=(float)(q3n_events_rand(f->events)%360);
        q3n_angles_axis(random_angles,hit.axis);
        if (!emit(f,&hit,e)) return false;
    }
    return true;
}
static bool spin(const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,float *out,qa_error *e)
{
    int32_t delta=difference(f->time,cent->barrel_time); float angle;
    if (cent->barrel_spinning) angle=add(cent->barrel_angle,mul((float)delta,.9f));
    else {
        if (delta>1000) delta=1000;
        float speed=mul(.5f,add(.9f,divide((float)difference(1000,delta),1000)));
        angle=add(cent->barrel_angle,mul((float)delta,speed));
    }
    bool firing=(state->eFlags&256)!=0;
    if (cent->barrel_spinning!=firing) {
        cent->barrel_time=f->time;
        cent->barrel_angle=mul((float)((uint32_t)integer(mul(angle,65536.0f/360.0f))&65535u),360.0f/65536.0f);
        cent->barrel_spinning=firing;
        if (f->source.product==QA_Q3_TEAM_ARENA && state->weapon==13 && !firing &&
            !start_sound(f,q3n_media_read(f->media)->sounds[Q3N_S_CHAINGUN_WIND],NULL,state->number,2,e)) return false;
    }
    *out=angle; return true;
}
static bool powered(const q3n_frame *f,qa_q3_ref_entity *ref,int32_t powerups,qa_error *e)
{
    const q3n_media_view *media=q3n_media_read(f->media);
    if (powerups&16) { ref->custom_shader=media->graphics[Q3N_G_INVIS]; return emit(f,ref,e); }
    if (!emit(f,ref,e)) return false;
    if (powerups&4) { ref->custom_shader=media->graphics[Q3N_G_BATTLE_WEAPON]; if (!emit(f,ref,e)) return false; }
    if (powerups&2) { ref->custom_shader=media->graphics[Q3N_G_QUAD_WEAPON]; if (!emit(f,ref,e)) return false; }
    return true;
}
static qa_q3_ref_entity attached(const qa_q3_ref_entity *parent,int32_t model)
{
    qa_q3_ref_entity ref=reference(QA_Q3_REF_MODEL,model);
    ref.lighting_origin=parent->lighting_origin; ref.shadow_plane=parent->shadow_plane; ref.flags=parent->flags;
    return ref;
}
bool q3n_weapons_player(const q3n_frame *f,const qa_q3_ref_entity *parent,const qa_q3_player *ps,
    q3n_entity *cent,const qa_q3_entity *state,qa_error *e)
{
    if (!frame_valid(f,e) || !parent || !cent || !state) return false;
    const q3n_weapon_media *w;
    if (!weapon(f,state->weapon,true,&w,e)) return false;
    qa_q3_ref_entity gun=attached(parent,w->weapon_model);
    if (ps && f->has_local_player && f->local_player.weapon==7 && f->local_player.weaponState==3) {
        uint8_t color=(uint8_t)((uint32_t)integer(mul(255,add(1,-divide((float)f->local_player.weaponTime,1500))))&255u);
        gun.color[0]=gun.color[2]=color; gun.color[1]=gun.color[3]=0;
    } else if (ps) memset(gun.color,255,sizeof(gun.color));
    if (!ps && f->weapons->options.held_replacement) {
        bool suppressed=false;
        if (!f->weapons->options.held_replacement(f->weapons->options.context,f,state,parent,&suppressed,e)) return false;
        if (!frame_valid(f,e)) return false;
        if (suppressed) return true;
    }
    if (!gun.model) return true;
    if (!ps) {
        cent->lightning_firing=false;
        if ((state->eFlags&256) && w->firing_sound) {
            if (!loop_sound(f,w->firing_sound,state->number,cent->lerp_origin,e)) return false;
            cent->lightning_firing=true;
        } else if (w->ready_sound && !loop_sound(f,w->ready_sound,state->number,cent->lerp_origin,e)) return false;
    }
    if (!attach(f,&gun,parent,"tag_weapon",false,e) || !powered(f,&gun,state->powerups,e)) return false;
    if (w->barrel_model) {
        qa_q3_ref_entity barrel=attached(parent,w->barrel_model); float angle;
        if (!spin(f,cent,state,&angle,e)) return false;
        q3n_angles_axis(qa_v3(0,0,angle),barrel.axis);
        if (!attach(f,&barrel,&gun,"tag_barrel",true,e) || !powered(f,&barrel,state->powerups,e)) return false;
    }
    qa_application_native_q3_entity non_predicted;
    if (!source_entity(f,state->clientNum,&non_predicted,e)) return false;
    q3n_entity *source_cent=&f->entities[state->clientNum];
    if (!((state->weapon==6 || state->weapon==1 || state->weapon==10) && (non_predicted.state.eFlags&256)))
        if (difference(f->time,cent->muzzle_flash_time)>20 && !cent->railgun_flash) return true;
    qa_q3_ref_entity flash=attached(parent,w->flash_model);
    if (!flash.model) return true;
    q3n_angles_axis(qa_v3(0,0,mul(q3n_events_crandom(f->events),10)),flash.axis);
    if (state->weapon==7) {
        const q3n_client_info *ci=state->clientNum>=0 && state->clientNum<64?q3n_clients_get(f->clients,(uint32_t)state->clientNum):NULL;
        if (!ci) return q3p_fail(e,QA_ERROR_FORMAT,"Rail flash requires its actual client-info row");
        flash.color[0]=(uint8_t)((uint32_t)integer(mul(ci->color1.x,255))&255u);
        flash.color[1]=(uint8_t)((uint32_t)integer(mul(ci->color1.y,255))&255u);
        flash.color[2]=(uint8_t)((uint32_t)integer(mul(ci->color1.z,255))&255u); flash.color[3]=0;
    }
    if (!attach(f,&flash,&gun,"tag_flash",true,e)) return false;
    if (!f->preferences.reduced_flashes && !emit(f,&flash,e)) return false;
    if (ps || f->third_person || !f->has_local_player || state->number!=f->local_player.clientNum) {
        if (!lightning(f,source_cent,&non_predicted.state,flash.origin,e)) return false;
        if (state->weapon==7 && cent->railgun_flash) {
            cent->railgun_flash=true;
            if (!q3n_weapons_rail(f,state->clientNum,&flash.origin,cent->rail_impact,e)) return false;
        }
        qa_vec3 color=w->flash_light_color;
        if (color.x || color.y || color.z) {
            float radius=(float)(300+(q3n_events_rand(f->events)&31));
            if (!f->preferences.reduced_flashes && (!qa_q3_presentation_light(f->presentation,flash.origin,radius,color,false,e) || !frame_valid(f,e))) return false;
        }
    }
    return true;
}
static bool torso_frame(const qa_player_animation_config *config,int32_t frame,int32_t *out,qa_error *e)
{
    const size_t indices[3]={9,7,8};
    for (size_t i=0;i<3;++i) {
        const qa_player_animation *animation=&config->animations[indices[i]];
        if (!animation->present) return q3p_fail(e,QA_ERROR_FORMAT,"Missing weapon torso animation");
        if (frame>=animation->first_frame && (int64_t)frame<(int64_t)animation->first_frame+(i?6:9)) {
            *out=frame-animation->first_frame+(i?1:6); return true;
        }
    }
    *out=0; return true;
}
bool q3n_weapons_view(const q3n_frame *f,const q3n_weapon_view *view,qa_error *e)
{
    if (!frame_valid(f,e) || !view || !f->has_local_player) return false;
    const qa_q3_player *ps=&f->local_player;
    if (f->weapons->options.view_replacement) {
        bool consumed=false;
        if (!f->weapons->options.view_replacement(f->weapons->options.context,f,ps,&consumed,e) || !frame_valid(f,e)) return false;
        if (consumed) return true;
    }
    if (!view->predicted_entity || !view->predicted_state)
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Primary view weapon requires its actual predicted entity and state");
    if (ps->persistant[3]==3 || ps->pmType==5 || f->third_person) return true;
    const q3n_weapon_settings *settings=f->weapon_settings;
    if (!settings->draw_gun) {
        if (!(ps->eFlags&256)) return true;
        qa_application_native_q3_entity row;
        if (!source_entity(f,ps->clientNum,&row,e)) return false;
        return lightning(f,&f->entities[ps->clientNum],&row.state,ma(f->refdef.origin,-8,f->refdef.axis[2]),e);
    }
    if (view->test_gun) return true;
    const q3n_weapon_media *w;
    if (!weapon(f,ps->weapon,true,&w,e)) return false;
    float roll_scale=view->bob_cycle&1?-view->xy_speed:view->xy_speed;
    float roll=mul(mul(roll_scale,view->bob_fraction_sin),.005f),yaw=mul(mul(roll_scale,view->bob_fraction_sin),.01f);
    float pitch=mul(mul(view->xy_speed,view->bob_fraction_sin),.005f);
    qa_vec3 origin=f->refdef.origin,angles=plus(f->view_angles,qa_v3(pitch,yaw,roll));
    int32_t delta=difference(f->time,view->land_time);
    if (delta<150) origin.z=add(origin.z,divide(mul(mul(view->land_change,.25f),(float)delta),150));
    else if (delta<450) origin.z=add(origin.z,divide(mul(mul(view->land_change,.25f),(float)difference(450,delta)),300));
    float drift=mul(mul(add(view->xy_speed,40),(float)sin((double)mul((float)f->time,.001f))),.01f);
    angles=plus(angles,qa_v3(drift,drift,drift));
    float offset=settings->fov>90?mul(-.2f,(float)(settings->fov-90)):0;
    qa_q3_ref_entity hands=reference(QA_Q3_REF_MODEL,w->hands_model);
    hands.origin=ma(ma(ma(origin,settings->gun_x,f->refdef.axis[0]),settings->gun_y,f->refdef.axis[1]),add(settings->gun_z,offset),f->refdef.axis[2]);
    q3n_angles_axis(angles,hands.axis);
    if (settings->gun_frame) hands.frame=hands.old_frame=settings->gun_frame;
    else {
        int32_t number=view->predicted_state->clientNum;
        const q3n_client_info *ci=number>=0 && number<64?q3n_clients_get(f->clients,(uint32_t)number):NULL;
        if (!ci) return q3p_fail(e,QA_ERROR_FORMAT,"View weapon requires its actual client-info row");
        const q3n_lerp_frame *torso=&view->predicted_entity->player.torso.animation;
        if (!torso_frame(&ci->animations,torso->frame,&hands.frame,e) || !torso_frame(&ci->animations,torso->old_frame,&hands.old_frame,e)) return false;
        hands.back_lerp=torso->back_lerp;
    }
    hands.flags=8|4|1;
    if (ps->persistant[3]<0 || ps->persistant[3]>3) return q3p_fail(e,QA_ERROR_FORMAT,"Invalid view weapon team");
    return q3n_weapons_player(f,&hands,ps,view->predicted_entity,view->predicted_state,e);
}
static qa_vec3 cross(qa_vec3 a,qa_vec3 b)
{ return qa_v3(add(mul(a.y,b.z),-mul(a.z,b.y)),add(mul(a.z,b.x),-mul(a.x,b.z)),add(mul(a.x,b.y),-mul(a.y,b.x))); }
static qa_vec3 perpendicular(qa_vec3 direction)
{
    qa_vec3 axis={1,0,0}; float minimum=1;
    if (fabsf(direction.x)<minimum) minimum=fabsf(direction.x);
    if (fabsf(direction.y)<minimum) { minimum=fabsf(direction.y); axis=qa_v3(0,1,0); }
    if (fabsf(direction.z)<minimum) axis=qa_v3(0,0,1);
    float inverse=divide(1,dot(direction,direction)),distance=mul(dot(direction,axis),inverse);
    return normalized(minus(axis,scale(scale(direction,inverse),distance)));
}
static float selected_spin(q3n_selected_weapon_barrel *barrel,int32_t time,bool firing)
{
    int32_t delta=difference(time,barrel->time); float angle;
    if (barrel->spinning) angle=add(barrel->angle,mul((float)delta,.9f));
    else {
        if (delta>1000) delta=1000;
        float speed=mul(.5f,add(.9f,divide((float)difference(1000,delta),1000)));
        angle=add(barrel->angle,mul((float)delta,speed));
    }
    if (barrel->spinning!=firing) {
        barrel->time=time;
        barrel->angle=mul((float)((uint32_t)integer(mul(angle,65536.0f/360.0f))&65535u),360.0f/65536.0f);
        barrel->spinning=firing;
    }
    return angle;
}
static float selected_random(q3n_selected_weapon_state *state)
{
    state->random_seed=69069u*state->random_seed+1u;
    return mul(2,add(divide((float)(state->random_seed&32767u),32767),-.5f));
}
static bool selected_current(const q3n_selected_weapon_draw *draw,qa_error *e)
{ return draw->current(draw->context) || q3p_fail(e,QA_ERROR_ARGUMENT,"Selected Q3 weapon source changed during presentation"); }
static bool selected_begin(q3n_weapons *owner,const q3n_selected_weapon_media *media,
    q3n_selected_weapon_state *state,const q3n_selected_weapon_draw *draw,bool *submitted,qa_error *e)
{
    if (submitted) *submitted=false;
    if (!q3n_weapons_idle(owner) || !media || !state || !draw || !submitted ||
        !draw->player || !draw->current || !draw->submit || media->assets!=owner->options.assets ||
        draw->player->product!=owner->options.product || draw->player->weapon<1 || draw->player->weapon>=16 ||
        !qa_q3_assets_idle(media->assets))
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Selected Q3 weapon needs its actual source and retained resource owner");
    const q3p_model *model; const qa_material *shader;
    const int32_t models[4]={media->gun,media->hands,media->barrel,media->flash};
    const int32_t shaders[3]={media->invisibility,media->battle_weapon,media->quad_weapon};
    for (size_t i=0;i<4;++i) if (!q3p_model_get(media->assets,models[i],&model,e)) return false;
    for (size_t i=0;i<3;++i) if (!q3p_shader_get(media->assets,shaders[i],&shader,e)) return false;
    if (!selected_current(draw,e)) return false;
    owner->busy=true; return true;
}
static bool selected_end(q3n_weapons *owner,bool okay)
{ owner->busy=false; return okay; }
static bool selected_emit(const q3n_selected_weapon_media *media,const q3n_selected_weapon_draw *draw,
    const qa_q3_ref_entity *ref,bool *submitted,qa_error *e)
{
    if (!selected_current(draw,e) || !draw->submit(draw->context,media->assets,ref,e)) return false;
    *submitted=true; return selected_current(draw,e);
}
/* Scene attachment composition retains each child's own pose. Missing optional
 * gun tags omit that attachment; selected hands require tag_weapon. */
static bool selected_attach(const qa_q3_presentation_assets *assets,qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent,const char *name,bool required,bool *found,qa_error *e)
{
    qa_model_tag tag; float fraction=add(1,-parent->back_lerp);
    if (!qa_q3_presentation_tag(assets,parent->model,name,parent->old_frame,parent->frame,fraction,&tag,found,e)) return false;
    if (!*found) return !required || q3p_fail(e,QA_ERROR_FORMAT,"Selected Q3 hands model has no required weapon tag");
    qa_vec3 tag_axes[3],axes[3];
    for (size_t i=0;i<3;++i)
        tag_axes[i]=transform(qa_v3(tag.axes[i][0],tag.axes[i][1],tag.axes[i][2]),parent->axis);
    child->origin=plus(parent->origin,transform(qa_v3(tag.origin[0],tag.origin[1],tag.origin[2]),parent->axis));
    child->old_origin=child->origin;
    for (size_t i=0;i<3;++i) axes[i]=transform(child->axis[i],tag_axes);
    memcpy(child->axis,axes,sizeof(axes)); child->lighting_origin=parent->lighting_origin;
    return true;
}
static qa_q3_ref_entity selected_part(int32_t model,int32_t flags,qa_vec3 lighting)
{
    qa_q3_ref_entity ref=reference(QA_Q3_REF_MODEL,model);
    memset(ref.color,255,sizeof(ref.color)); identity(ref.axis);
    ref.flags=flags; ref.lighting_origin=lighting; return ref;
}
static bool selected_flash(const q3n_selected_weapon_draw *draw)
{
    int32_t weapon=draw->player->weapon;
    return (draw->firing && (weapon==1 || weapon==6 || weapon==10)) ||
        (draw->has_last_fire && (int64_t)draw->time-(int64_t)draw->last_fire<=20);
}
static bool selected_parts(const q3n_selected_weapon_media *media,const q3n_selected_weapon_draw *draw,
    qa_q3_ref_entity *gun,qa_q3_ref_entity *barrel,bool have_barrel,int32_t shader,bool *submitted,qa_error *e)
{
    gun->custom_shader=shader;
    if (!selected_emit(media,draw,gun,submitted,e)) return false;
    if (!have_barrel) return true;
    barrel->custom_shader=shader; return selected_emit(media,draw,barrel,submitted,e);
}
bool q3n_weapons_selected_held(q3n_weapons *owner,const q3n_selected_weapon_media *media,
    q3n_selected_weapon_state *state,const q3n_selected_weapon_draw *draw,
    const q3n_selected_weapon_held *held,bool *submitted,qa_error *e)
{
    if (!held || !held->parent_assets || !held->torso || !qa_vec_finite(held->lighting_origin))
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Selected held Q3 weapon requires the actual authored torso");
    if (!selected_begin(owner,media,state,draw,submitted,e)) return false;
    if (!media->gun) return selected_end(owner,true);
    int32_t flags=128|(held->personal_model?2:0); bool gun_found,have_barrel=false,have_flash=false;
    qa_q3_ref_entity gun=selected_part(media->gun,flags,held->lighting_origin);
    if (!selected_attach(held->parent_assets,&gun,held->torso,"tag_weapon",false,&gun_found,e)) return selected_end(owner,false);
    float spin_angle=selected_spin(&state->world_barrel,draw->time,draw->firing);
    qa_q3_ref_entity barrel=selected_part(media->barrel,flags,held->lighting_origin);
    q3n_angles_axis(qa_v3(0,0,spin_angle),barrel.axis);
    if (media->barrel && !selected_attach(media->assets,&barrel,&gun,"tag_barrel",false,&have_barrel,e)) return selected_end(owner,false);
    qa_q3_ref_entity flash=selected_part(media->flash,flags,held->lighting_origin);
    if (media->flash && selected_flash(draw)) {
        if (!selected_attach(media->assets,&flash,&gun,"tag_flash",false,&have_flash,e)) return selected_end(owner,false);
        if (have_flash) {
            q3n_angles_axis(qa_v3(0,0,mul(selected_random(state),10)),flash.axis);
            if (!selected_attach(media->assets,&flash,&gun,"tag_flash",false,&have_flash,e)) return selected_end(owner,false);
        }
    }
    bool okay=true;
    if (gun_found) {
        if (held->powerups&16) okay=selected_parts(media,draw,&gun,&barrel,have_barrel,media->invisibility,submitted,e);
        else {
            okay=selected_parts(media,draw,&gun,&barrel,have_barrel,0,submitted,e);
            if (okay && (held->powerups&4)) okay=selected_parts(media,draw,&gun,&barrel,have_barrel,media->battle_weapon,submitted,e);
            if (okay && (held->powerups&2)) okay=selected_parts(media,draw,&gun,&barrel,have_barrel,media->quad_weapon,submitted,e);
        }
        if (okay && have_flash && !draw->reduced_flashes) okay=selected_emit(media,draw,&flash,submitted,e);
    }
    return selected_end(owner,okay);
}
bool q3n_weapons_selected_view(q3n_weapons *owner,const q3n_selected_weapon_media *media,
    q3n_selected_weapon_state *state,const q3n_selected_weapon_draw *draw,
    const q3n_selected_weapon_view *view,bool *submitted,qa_error *e)
{
    if (!view || !view->animations || !qa_vec_finite(view->origin) || !qa_vec_finite(view->angles) ||
        !isfinite(view->horizontal_speed) || view->horizontal_speed<0)
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Selected Q3 view weapon requires its actual camera and animation owner");
    if (!selected_begin(owner,media,state,draw,submitted,e)) return false;
    if (!view->draw_gun || !media->gun) return selected_end(owner,true);
    if (!media->hands) return selected_end(owner,q3p_fail(e,QA_ERROR_FORMAT,"Selected Q3 weapon has no hands model or fallback"));
    if (!q3n_lerp_run(view->animations,&state->torso,draw->player->torsoAnim,draw->time,1,false,e)) return selected_end(owner,false);
    double bob=fabs(sin((double)(view->bob_cycle&127)/127*3.14159265358979323846));
    double roll_scale=(view->bob_cycle&128)?-view->horizontal_speed:view->horizontal_speed;
    qa_vec3 angles=plus(view->angles,qa_v3(mul((float)(view->horizontal_speed*bob),.005f),
        mul((float)(roll_scale*bob),.01f),mul((float)(roll_scale*bob),.005f)));
    float drift=mul(mul((float)(view->horizontal_speed+40),(float)sin((double)mul((float)draw->time,.001f))),.01f);
    angles=plus(angles,qa_v3(drift,drift,drift));
    qa_q3_ref_entity hands=selected_part(media->hands,1|4|8,view->origin);
    hands.origin=hands.old_origin=view->origin; q3n_angles_axis(angles,hands.axis);
    if (!torso_frame(view->animations,state->torso.frame,&hands.frame,e) ||
        !torso_frame(view->animations,state->torso.old_frame,&hands.old_frame,e)) return selected_end(owner,false);
    hands.back_lerp=state->torso.back_lerp;
    qa_q3_ref_entity gun=selected_part(media->gun,1|4|8,view->origin); bool found;
    if (!selected_attach(media->assets,&gun,&hands,"tag_weapon",true,&found,e)) return selected_end(owner,false);
    float spin_angle=selected_spin(&state->view_barrel,draw->time,draw->firing);
    qa_q3_ref_entity barrel=selected_part(media->barrel,1|4|8,view->origin); bool have_barrel=false;
    q3n_angles_axis(qa_v3(0,0,spin_angle),barrel.axis);
    if (media->barrel && !selected_attach(media->assets,&barrel,&gun,"tag_barrel",false,&have_barrel,e)) return selected_end(owner,false);
    qa_q3_ref_entity flash=selected_part(media->flash,1|4|8,view->origin); bool have_flash=false;
    if (media->flash && selected_flash(draw)) {
        q3n_angles_axis(qa_v3(0,0,mul(selected_random(state),10)),flash.axis);
        if (!selected_attach(media->assets,&flash,&gun,"tag_flash",false,&have_flash,e)) return selected_end(owner,false);
    }
    bool okay=selected_parts(media,draw,&gun,&barrel,have_barrel,0,submitted,e);
    if (okay && have_flash && !draw->reduced_flashes) okay=selected_emit(media,draw,&flash,submitted,e);
    return selected_end(owner,okay);
}
bool q3n_selected_weapon_state_fields(qa_source_save_io *io,q3n_selected_weapon_state *state)
{
    if (!io || !state) return false;
    q3n_lerp_frame *torso=&state->torso;
    if (!qa_source_save_i32(io,&torso->old_frame) || !qa_source_save_i32(io,&torso->old_frame_time) ||
        !qa_source_save_i32(io,&torso->frame) || !qa_source_save_i32(io,&torso->frame_time) ||
        !qa_source_save_f32(io,&torso->back_lerp) || !qa_source_save_i32(io,&torso->animation_number) ||
        !qa_source_save_i32(io,&torso->animation_time) || !qa_source_save_bool(io,&torso->selected) ||
        !qa_source_save_u32(io,&state->random_seed)) return false;
    q3n_selected_weapon_barrel *barrels[2]={&state->view_barrel,&state->world_barrel};
    for (size_t i=0;i<2;++i)
        if (!qa_source_save_i32(io,&barrels[i]->time) || !qa_source_save_f32(io,&barrels[i]->angle) ||
            !qa_source_save_bool(io,&barrels[i]->spinning)) return false;
    if (!isfinite(torso->back_lerp) || !isfinite(state->view_barrel.angle) || !isfinite(state->world_barrel.angle) ||
        (torso->selected && ((torso->animation_number&~128)<0 || (torso->animation_number&~128)>=QA_PLAYER_ANIMATION_COUNT))) {
        io->failed=true; return q3p_fail(io->error,QA_ERROR_FORMAT,"Invalid selected Q3 weapon continuation");
    }
    return true;
}
static void matrix_multiply(const qa_vec3 a[3],const qa_vec3 b[3],qa_vec3 out[3])
{
    qa_vec3 columns[3]={qa_v3(b[0].x,b[1].x,b[2].x),qa_v3(b[0].y,b[1].y,b[2].y),qa_v3(b[0].z,b[1].z,b[2].z)};
    for (size_t i=0;i<3;++i) out[i]=qa_v3(dot(a[i],columns[0]),dot(a[i],columns[1]),dot(a[i],columns[2]));
}
static qa_vec3 rotated(qa_vec3 direction,qa_vec3 point,float degrees)
{
    qa_vec3 radial=perpendicular(direction),vertical=cross(radial,direction);
    float radians=divide(mul(degrees,3.14159265358979323846f),180),cosine=(float)cos((double)radians),sine=(float)sin((double)radians);
    qa_vec3 basis[3]={qa_v3(radial.x,vertical.x,direction.x),qa_v3(radial.y,vertical.y,direction.y),qa_v3(radial.z,vertical.z,direction.z)};
    qa_vec3 rotation[3]={qa_v3(cosine,sine,0),qa_v3(-sine,cosine,0),qa_v3(0,0,1)},inverse[3]={radial,vertical,direction},first[3],final[3];
    matrix_multiply(basis,rotation,first); matrix_multiply(first,inverse,final);
    return qa_v3(dot(final[0],point),dot(final[1],point),dot(final[2],point));
}
static void colored(q3n_local_entity *le,qa_vec3 color,float byte_scale,uint8_t alpha,float fade,float fade_alpha)
{
    le->ref.color[0]=(uint8_t)((uint32_t)integer(mul(color.x,byte_scale))&255u);
    le->ref.color[1]=(uint8_t)((uint32_t)integer(mul(color.y,byte_scale))&255u);
    le->ref.color[2]=(uint8_t)((uint32_t)integer(mul(color.z,byte_scale))&255u); le->ref.color[3]=alpha;
    le->color[0]=mul(color.x,fade); le->color[1]=mul(color.y,fade); le->color[2]=mul(color.z,fade); le->color[3]=fade_alpha;
}
bool q3n_weapons_rail(const q3n_frame *f,int32_t client,qa_vec3 *start,qa_vec3 end,qa_error *e)
{
    if (!frame_valid(f,e) || !start || client<0 || client>=64) return false;
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)client);
    if (!ci) return q3p_fail(e,QA_ERROR_FORMAT,"Rail trail requires its genuine client-info row");
    start->z=add(start->z,-4); qa_vec3 move=*start,delta=minus(end,*start);
    float extent=length(delta); qa_vec3 direction=normalized(delta),temp=perpendicular(direction),axes[36];
    for (size_t i=0;i<36;++i) axes[i]=rotated(direction,temp,(float)(i*10));
    const q3n_media_view *media=q3n_media_read(f->media);
    q3n_local_entity *core=q3n_local_allocate(f->events,Q3N_LE_FADE_RGB,QA_Q3_REF_RAIL_CORE);
    core->start_time=f->time; core->end_time=integer(add((float)f->time,f->weapon_settings->rail_trail_time));
    core->life_rate=divide(1,(float)difference(core->end_time,f->time)); core->ref.shader_time=divide((float)f->time,1000);
    core->ref.custom_shader=media->graphics[Q3N_G_RAIL_CORE]; core->ref.origin=*start; core->ref.old_origin=end;
    colored(core,ci->color1,255,255,.75f,1);
    move=ma(move,20,direction); qa_vec3 step=scale(direction,5);
    if (f->weapon_settings->old_rail) { core->ref.origin.z=add(core->ref.origin.z,-8); core->ref.old_origin.z=add(core->ref.old_origin.z,-8); return true; }
    int32_t skip=-1; size_t j=18;
    for (int32_t i=0;(float)i<extent;i+=5) {
        if (i!=skip) {
            skip=i+5; q3n_local_entity *ring=q3n_local_allocate(f->events,Q3N_LE_MOVE_SCALE_FADE,QA_Q3_REF_SPRITE);
            ring->flags=Q3N_LE_DONT_SCALE; ring->start_time=f->time; ring->end_time=sum(sum(f->time,i>>1),600);
            ring->life_rate=divide(1,(float)difference(ring->end_time,f->time)); ring->ref.shader_time=divide((float)f->time,1000);
            ring->ref.radius=1.1f; ring->ref.custom_shader=media->graphics[Q3N_G_RAIL_RINGS]; colored(ring,ci->color2,255,255,.75f,1);
            ring->pos.type=2; ring->pos.time=f->time; vector(ring->pos.base,ma(move,4,axes[j])); vector(ring->pos.delta,scale(axes[j],6));
        }
        move=plus(move,step); j=(j+1)%36;
        if (i>INT32_MAX-5) break;
    }
    return true;
}
static bool plasma(const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,qa_error *e)
{
    if (f->weapon_settings->no_projectile_trail || f->weapon_settings->old_plasma) return true;
    qa_vec3 origin;
    if (!q3n_trajectory(&state->pos,f->time,&origin,e)) return false;
    q3n_local_entity *le=q3n_local_allocate(f->events,Q3N_LE_MOVE_SCALE_FADE,QA_Q3_REF_SPRITE);
    qa_vec3 velocity;
    velocity.x=add(60,-mul(120,q3n_events_crandom(f->events)));
    velocity.y=add(40,-mul(80,q3n_events_crandom(f->events)));
    velocity.z=add(100,-mul(200,q3n_events_crandom(f->events)));
    le->flags=Q3N_LE_TUMBLE; le->start_time=f->time; le->end_time=sum(f->time,600);
    qa_vec3 axis[3]; q3n_angles_axis(cent->lerp_angles,axis); le->ref.origin=plus(origin,transform(qa_v3(2,2,2),axis));
    uint32_t contents;
    if (!q3n_events_point_contents(f,le->ref.origin,-1,&contents,e)) return false;
    le->pos.type=5; le->pos.time=f->time; vector(le->pos.base,le->ref.origin);
    vector(le->pos.delta,scale(transform(velocity,axis),(contents&32)?.1f:1));
    le->ref.shader_time=divide((float)f->time,1000); le->ref.radius=.25f;
    le->ref.custom_shader=q3n_media_read(f->media)->graphics[Q3N_G_RAIL_RINGS]; le->bounce_factor=.3f;
    const q3n_weapon_media *w;
    if (!weapon(f,state->weapon,false,&w,e)) return false;
    colored(le,w->flash_light_color,63,63,.2f,.25f);
    le->angles.type=2; le->angles.time=f->time;
    for (size_t i=0;i<3;++i) le->angles.base[i]=(float)(q3n_events_rand(f->events)&31);
    vector(le->angles.delta,qa_v3(1,.5f,0)); return true;
}
bool q3n_weapons_trail(const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,qa_error *e)
{
    if (!frame_valid(f,e) || !cent || !state) return false;
    const q3n_weapon_media *w;
    if (!weapon(f,state->weapon,false,&w,e)) return false;
    if (w->trail==Q3N_TRAIL_NONE) return true;
    if (w->trail==Q3N_TRAIL_PLASMA) return plasma(f,cent,state,e);
    qa_vec3 origin;
    if (!q3n_trajectory(&state->pos,f->time,&origin,e)) return false;
    if (w->trail==Q3N_TRAIL_GRAPPLE) {
        cent->trail_time=f->time; qa_application_native_q3_entity owner;
        if (!source_entity(f,state->otherEntityNum,&owner,e)) return false;
        q3n_entity *client=&f->entities[state->otherEntityNum]; qa_vec3 axis[3]; q3n_angles_axis(client->lerp_angles,axis);
        qa_vec3 start=ma(plus(client->lerp_origin,qa_v3(0,0,26)),-6,axis[2]);
        if (length(minus(start,origin))<64) return true;
        qa_q3_ref_entity beam=reference(QA_Q3_REF_LIGHTNING,0);
        memset(beam.color,255,sizeof(beam.color)); identity(beam.axis);
        beam.origin=start; beam.old_origin=origin; beam.custom_shader=q3n_media_read(f->media)->graphics[Q3N_G_LIGHTNING_SHADER];
        return emit(f,&beam,e);
    }
    if (f->weapon_settings->no_projectile_trail) return true;
    int32_t next=sum(cent->trail_time,50)/50;
    uint32_t next_bits=(uint32_t)next*50; memcpy(&next,&next_bits,4);
    uint32_t contents,old_contents;
    if (!q3n_events_point_contents(f,origin,-1,&contents,e)) return false;
    if (!state->pos.type) { cent->trail_time=f->time; return true; }
    qa_vec3 previous;
    if (!q3n_trajectory(&state->pos,cent->trail_time,&previous,e) || !q3n_events_point_contents(f,previous,-1,&old_contents,e)) return false;
    cent->trail_time=f->time;
    if (contents&(32|16|8)) return !(contents&old_contents&32) || q3n_effect_bubbles(f,previous,origin,8,e);
    for (;next<=f->time;next=sum(next,50)) {
        qa_vec3 point;
        if (!q3n_trajectory(&state->pos,next,&point,e)) return false;
        q3n_smoke smoke={.origin=point,.radius=w->trail_radius,.color={1,1,1,.33f},.duration=(float)w->trail_time,.start_time=next,
            .shader=q3n_media_read(f->media)->graphics[w->trail==Q3N_TRAIL_NAIL?Q3N_G_NAIL_PUFF:Q3N_G_SMOKE_PUFF]};
        q3n_effect_smoke(f,&smoke)->type=Q3N_LE_SCALE_FADE;
        if (next>INT32_MAX-50) break;
    }
    return true;
}
bool q3n_weapons_impact(const q3n_frame *f,int32_t number,int32_t client,qa_vec3 origin,qa_vec3 direction,q3n_impact_sound type,qa_error *e)
{
    if (!frame_valid(f,e)) return false;
    const q3n_media_view *media=q3n_media_read(f->media);
    int32_t model=0,shader=0,sound=0,mark=0,duration=600;
    float radius=32,light=0; qa_vec3 light_color={1,1,0}; bool sprite=false;
    int32_t impact=f->source.product==QA_Q3_ARENA && (number==12 || number==13)?0:number;
    switch (impact) {
    default:
    case 11:
        if (f->source.product==QA_Q3_TEAM_ARENA) {
            sound=media->sounds[type==Q3N_IMPACT_FLESH?Q3N_S_NAIL_FLESH:type==Q3N_IMPACT_METAL?Q3N_S_NAIL_METAL:Q3N_S_NAIL_HIT];
            mark=media->graphics[Q3N_G_HOLE_MARK]; radius=12; break;
        }
        /* fall through */
    case 6: {
        int32_t random=q3n_events_rand(f->events)&3;
        sound=media->sounds[random<2?Q3N_S_LIGHTNING_HIT2:random==2?Q3N_S_LIGHTNING_HIT1:Q3N_S_LIGHTNING_HIT3];
        mark=media->graphics[Q3N_G_HOLE_MARK]; radius=12; break;
    }
    case 12:
    case 4:
        model=media->graphics[Q3N_G_DISH_FLASH]; shader=media->graphics[Q3N_G_GRENADE_EXPLOSION];
        sound=media->sounds[impact==12?Q3N_S_PROX_EXPLOSION:Q3N_S_ROCKET_EXPLOSION];
        mark=media->graphics[Q3N_G_BURN_MARK]; radius=64; light=300; sprite=true; break;
    case 5:
        model=media->graphics[Q3N_G_DISH_FLASH]; shader=media->graphics[Q3N_G_ROCKET_EXPLOSION]; sound=media->sounds[Q3N_S_ROCKET_EXPLOSION];
        mark=media->graphics[Q3N_G_BURN_MARK]; radius=64; light=300; sprite=true; duration=1000; light_color=qa_v3(1,.75f,0);
        if (!f->weapon_settings->old_rocket) {
            if (!f->weapons->options.particle_explosion) return q3p_fail(e,QA_ERROR_UNSUPPORTED,"Native rocket impact requires its real particle owner");
            if (!f->weapons->options.particle_explosion(f->weapons->options.context,f,"explode1",ma(origin,24,direction),scale(direction,64),1400,20,30,e) || !frame_valid(f,e)) return false;
        }
        break;
    case 7:
    case 8:
        model=media->graphics[Q3N_G_RING_FLASH]; shader=media->graphics[impact==7?Q3N_G_RAIL_EXPLOSION:Q3N_G_PLASMA_EXPLOSION];
        sound=media->sounds[Q3N_S_PLASMA_EXPLOSION]; mark=media->graphics[Q3N_G_ENERGY_MARK]; radius=impact==7?24:16; break;
    case 9:
        model=media->graphics[Q3N_G_DISH_FLASH]; shader=media->graphics[Q3N_G_BFG_EXPLOSION]; sound=media->sounds[Q3N_S_ROCKET_EXPLOSION];
        mark=media->graphics[Q3N_G_BURN_MARK]; radius=32; sprite=true; break;
    case 3:
        model=media->graphics[Q3N_G_BULLET_FLASH]; shader=media->graphics[Q3N_G_BULLET_EXPLOSION]; mark=media->graphics[Q3N_G_BULLET_MARK]; radius=4; break;
    case 13: {
        model=media->graphics[Q3N_G_BULLET_FLASH]; mark=media->graphics[Q3N_G_BULLET_MARK];
        int32_t random=q3n_events_rand(f->events)&3;
        sound=media->sounds[random<2?Q3N_S_RICOCHET1:random==2?Q3N_S_RICOCHET2:Q3N_S_RICOCHET3]; radius=8; break;
    }
    case 2: {
        model=media->graphics[Q3N_G_BULLET_FLASH]; shader=media->graphics[Q3N_G_BULLET_EXPLOSION]; mark=media->graphics[Q3N_G_BULLET_MARK];
        int32_t random=q3n_events_rand(f->events)&3;
        sound=media->sounds[random==0?Q3N_S_RICOCHET1:random==1?Q3N_S_RICOCHET2:Q3N_S_RICOCHET3]; radius=8; break;
    }
    }
    if (sound && (!qa_q3_presentation_sound(f->presentation,sound,&origin,1022,0,false,e) || !frame_valid(f,e))) return false;
    const q3n_client_info *ci=NULL;
    if (number==7) {
        ci=client>=0 && client<64?q3n_clients_get(f->clients,(uint32_t)client):NULL;
        if (!ci) return q3p_fail(e,QA_ERROR_FORMAT,"Rail impact requires its actual client-info colors");
    }
    if (model) {
        q3n_explosion explosion={.origin=origin,.direction=direction,.has_direction=true,.sprite=sprite,.model=model,.shader=shader,.duration=duration};
        q3n_local_entity *le=q3n_effect_explosion(f,&explosion,e);
        if (!le) return false;
        le->light=light; le->light_color=light_color;
        if (ci) { le->color[0]=ci->color1.x; le->color[1]=ci->color1.y; le->color[2]=ci->color1.z; }
    }
    qa_vec3 color=ci?ci->color2:qa_v3(1,1,1);
    q3n_impact_mark request={.shader=mark,.origin=origin,.direction=direction,.orientation=mul(q3n_events_random(f->events),360),
        .color={color.x,color.y,color.z,1},.alpha_fade=mark==media->graphics[Q3N_G_ENERGY_MARK],.radius=radius};
    return q3n_marks_impact(f,&request,e);
}
static bool missile_player(const q3n_frame *f,int32_t weapon_number,qa_vec3 origin,qa_vec3 direction,int32_t target,qa_error *e)
{
    q3n_effect_bleed(f,origin,target);
    if (weapon_number==4 || weapon_number==5 || (f->source.product==QA_Q3_TEAM_ARENA && (weapon_number==11 || weapon_number==12 || weapon_number==13)))
        return q3n_weapons_impact(f,weapon_number,0,origin,direction,Q3N_IMPACT_FLESH,e);
    return true;
}
static bool muzzle_point(const q3n_frame *f,int32_t number,qa_vec3 *out,bool *found,qa_error *e)
{
    *found=false;
    if (!f->has_local_player) return q3p_fail(e,QA_ERROR_ARGUMENT,"CG_CalcMuzzlePoint: cg.snap == NULL");
    qa_vec3 angles,origin; float height;
    if (number==f->local_player.clientNum) { origin=from(f->local_player.origin); angles=from(f->local_player.viewangles); height=(float)f->local_player.viewheight; }
    else {
        qa_application_native_q3_entity row;
        if (!source_entity(f,number,&row,e)) return false;
        if (!f->entities[number].valid) return true;
        origin=from(row.state.pos.base); angles=from(row.state.apos.base);
        int32_t animation=row.state.legsAnim&~128; height=animation==13 || animation==23?12:26;
    }
    qa_vec3 axis[3]; q3n_angles_axis(angles,axis); origin.z=add(origin.z,height);
    *out=ma(origin,14,axis[0]); *found=true; return true;
}
static bool water_bubbles(const q3n_frame *f,qa_vec3 start,qa_vec3 end,float spacing,qa_error *e)
{
    uint32_t a,b;
    if (!q3n_events_point_contents(f,start,-1,&a,e) || !q3n_events_point_contents(f,end,-1,&b,e)) return false;
    if (a==b) return !(a&32) || q3n_effect_bubbles(f,start,end,spacing,e);
    qa_trace_result water; qa_bounds bounds={0};
    if (a&32) {
        if (!q3n_events_trace(f,end,start,bounds,-1,32,&water,e)) return false;
        return q3n_effect_bubbles(f,start,water.end,spacing,e);
    }
    if (b&32) {
        if (!q3n_events_trace(f,start,end,bounds,-1,32,&water,e)) return false;
        return q3n_effect_bubbles(f,water.end,end,spacing,e);
    }
    return true;
}
static bool tracer(const q3n_frame *f,qa_vec3 source,qa_vec3 destination,qa_error *e)
{
    qa_vec3 delta=minus(destination,source),forward=normalized(delta); float extent=length(delta);
    if (extent<100) return true;
    const q3n_weapon_settings *s=f->weapon_settings;
    float begin=add(50,mul(q3n_events_random(f->events),add(extent,-60))),end=fminf(add(begin,s->tracer_length),extent);
    qa_vec3 start=ma(source,begin,forward),finish=ma(source,end,forward);
    qa_vec3 right=normalized(ma(scale(f->refdef.axis[1],dot(forward,f->refdef.axis[2])),-dot(forward,f->refdef.axis[1]),f->refdef.axis[2]));
    qa_q3_poly_vertex vertices[4]={
        {.position=ma(finish,s->tracer_width,right),.texcoord={0,1},.color={255,255,255,255}},
        {.position=ma(finish,-s->tracer_width,right),.texcoord={1,0},.color={255,255,255,255}},
        {.position=ma(start,-s->tracer_width,right),.texcoord={1,1},.color={255,255,255,255}},
        {.position=ma(start,s->tracer_width,right),.texcoord={0,0},.color={255,255,255,255}}};
    const q3n_media_view *media=q3n_media_read(f->media);
    if (!qa_q3_presentation_poly(f->presentation,media->graphics[Q3N_G_TRACER],vertices,4,e) || !frame_valid(f,e)) return false;
    qa_vec3 middle=scale(plus(start,finish),.5f);
    return qa_q3_presentation_sound(f->presentation,media->sounds[Q3N_S_TRACER],&middle,1022,0,false,e) && frame_valid(f,e);
}
static bool bullet(const q3n_frame *f,qa_vec3 end,int32_t source,bool flesh,int32_t target,qa_vec3 normal,qa_error *e)
{
    if (source>=0 && f->weapon_settings->tracer_chance>0) {
        qa_vec3 start; bool found;
        if (!muzzle_point(f,source,&start,&found,e)) return false;
        if (found) {
            if (!water_bubbles(f,start,end,32,e)) return false;
            if (q3n_events_random(f->events)<f->weapon_settings->tracer_chance && !tracer(f,start,end,e)) return false;
        }
    }
    if (flesh) { q3n_effect_bleed(f,end,target); return true; }
    return q3n_weapons_impact(f,2,0,end,normal,Q3N_IMPACT_DEFAULT,e);
}
static float shot_random(uint32_t *seed)
{ *seed=69069u * *seed+1u; return mul(2,add((float)(*seed&65535u)/65536.0f,-.5f)); }
static bool shotgun(const q3n_frame *f,const qa_q3_entity *state,qa_error *e)
{
    qa_vec3 muzzle=from(state->pos.base),direction=from(state->origin2);
    uint32_t contents;
    if (!f->weapon_settings->ragepro) {
        if (!q3n_events_point_contents(f,muzzle,-1,&contents,e)) return false;
        if (!(contents&32)) {
            q3n_smoke smoke={.origin=ma(muzzle,32,normalized(minus(direction,muzzle))),.velocity={0,0,8},.radius=32,
                .color={1,1,1,.33f},.duration=900,.start_time=f->time,.flags=Q3N_LE_DONT_SCALE,
                .shader=q3n_media_read(f->media)->graphics[Q3N_G_SHOTGUN_SMOKE]};
            q3n_effect_smoke(f,&smoke);
        }
    }
    qa_vec3 forward=normalized(direction),right=perpendicular(forward),up=cross(forward,right);
    uint32_t seed=(uint32_t)state->eventParm;
    for (size_t i=0;i<11;++i) {
        float horizontal=mul(mul(shot_random(&seed),700),16);
        float vertical=mul(mul(shot_random(&seed),700),16);
        qa_vec3 end=ma(ma(ma(muzzle,131072,forward),horizontal,right),vertical,up);
        qa_trace_result trace; qa_bounds bounds={0};
        if (!q3n_events_trace(f,muzzle,end,bounds,state->otherEntityNum,1u|0x2000000u|0x4000000u,&trace,e)) return false;
        uint32_t destination;
        if (!q3n_events_point_contents(f,muzzle,-1,&contents,e) || !q3n_events_point_contents(f,trace.end,-1,&destination,e)) return false;
        if (contents==destination) {
            if ((contents&32) && !q3n_effect_bubbles(f,muzzle,trace.end,32,e)) return false;
        } else if (contents&32) {
            qa_trace_result water;
            if (!q3n_events_trace(f,end,muzzle,bounds,-1,32,&water,e) || !q3n_effect_bubbles(f,muzzle,water.end,32,e)) return false;
        } else if (destination&32) {
            qa_trace_result water;
            if (!q3n_events_trace(f,muzzle,end,bounds,-1,32,&water,e) || !q3n_effect_bubbles(f,trace.end,water.end,32,e)) return false;
        }
        if (trace.surface_flags&16) continue;
        int32_t target;
        if (!q3n_events_trace_number(f,&trace,&target,e)) return false;
        qa_vec3 normal=trace.contact?trace.contact_plane.normal:qa_v3(0,0,0);
        bool player=false;
        if (target>=0 && target<1022 && (uint32_t)target<f->source.entity_count) {
            qa_application_native_q3_entity row;
            if (!source_entity(f,target,&row,e)) return false;
            player=row.state.eType==1;
        }
        if (player) { if (!missile_player(f,3,trace.end,normal,target,e)) return false; }
        else if (!q3n_weapons_impact(f,3,0,trace.end,normal,(trace.surface_flags&4096)?Q3N_IMPACT_METAL:Q3N_IMPACT_DEFAULT,e)) return false;
    }
    return true;
}
bool q3n_weapons_event(void *context,const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *state,int32_t event,qa_vec3 position,qa_error *e)
{
    (void)context;
    if (!frame_valid(f,e) || !cent || !state) return false;
    switch (event) {
    case 21: return !f->has_local_player || state->number!=f->local_player.clientNum || q3n_weapons_out_of_ammo(f,e);
    case 23: return q3n_weapons_fire(f,cent,state,e);
    case 50: return missile_player(f,state->weapon,position,q3n_events_direction(state->eventParm),state->otherEntityNum,e);
    case 51: case 52: return q3n_weapons_impact(f,state->weapon,0,position,q3n_events_direction(state->eventParm),event==51?Q3N_IMPACT_DEFAULT:Q3N_IMPACT_METAL,e);
    case 53: {
        qa_vec3 start=from(state->origin2);
        int32_t client=state->clientNum;
        if (client<0 || client>=64) client=0;
        if (!q3n_weapons_rail(f,client,&start,from(state->pos.base),e)) return false;
        return state->eventParm==255 || q3n_weapons_impact(f,7,state->clientNum,position,q3n_events_direction(state->eventParm),Q3N_IMPACT_DEFAULT,e);
    }
    case 49: return bullet(f,from(state->pos.base),state->otherEntityNum,false,0,q3n_events_direction(state->eventParm),e);
    case 48: return bullet(f,from(state->pos.base),state->otherEntityNum,true,state->eventParm,qa_v3(0,0,0),e);
    case 54: return shotgun(f,state,e);
    default: return q3p_fail(e,QA_ERROR_FORMAT,"Native weapon received a non-weapon source event");
    }
}
bool q3n_weapons_draw_selection(const q3n_frame *f,const q3n_weapon_drawing *drawing,qa_error *e)
{
    if (!frame_valid(f,e) || !drawing || !drawing->fade_color || !drawing->set_color || !drawing->picture || !drawing->string_length || !drawing->big_string)
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Native weapon selection requires the actual HUD drawing services");
    if (!f->has_local_player || f->local_player.stats[0]<=0) return true;
    float color[4]; bool visible;
    if (!drawing->fade_color(drawing->context,f->weapons->selection.time,1400,color,&visible,e) || !frame_valid(f,e)) return false;
    if (!visible) return true;
    if (!drawing->set_color(drawing->context,color,e) || !frame_valid(f,e)) return false;
    q3n_events_clear_pickup_time(f->events);
    uint32_t bits=owned(f); int32_t count=0;
    for (int32_t i=1;i<16;++i) if (bits&(1u<<(uint32_t)i)) ++count;
    int32_t x=320-count*20;
    const q3n_media_view *media=q3n_media_read(f->media);
    for (int32_t i=1;i<16;++i) {
        if (!(bits&(1u<<(uint32_t)i))) continue;
        const q3n_weapon_media *w;
        if (!weapon(f,i,true,&w,e) || !drawing->picture(drawing->context,(float)x,380,32,32,w->weapon_icon,e) || !frame_valid(f,e)) return false;
        if (i==f->weapons->selection.weapon && (!drawing->picture(drawing->context,(float)(x-4),376,40,40,media->graphics[Q3N_G_SELECT],e) || !frame_valid(f,e))) return false;
        if (!f->local_player.ammo[i] && (!drawing->picture(drawing->context,(float)x,380,32,32,media->graphics[Q3N_G_NOAMMO],e) || !frame_valid(f,e))) return false;
        x+=40;
    }
    const q3n_weapon_media *selected;
    if (!weapon(f,f->weapons->selection.weapon,false,&selected,e)) return false;
    if (selected->item_index>=0) {
        const qa_q3_item *item=qa_q3_items(f->source.product,NULL)+selected->item_index;
        if (item->name) {
            size_t length=drawing->string_length(drawing->context,item->name);
            if (!frame_valid(f,e) || length>(size_t)INT32_MAX/16) return q3p_fail(e,QA_ERROR_FORMAT,"Native weapon name drawing extent overflow");
            int32_t position=(640-(int32_t)length*16)/2;
            if (!drawing->big_string(drawing->context,position,358,item->name,color,e) || !frame_valid(f,e)) return false;
        }
    }
    return drawing->set_color(drawing->context,NULL,e) && frame_valid(f,e);
}
static bool weapon_fields(qa_source_save_io *io,q3n_weapons *w)
{
    uint8_t magic[4]={'Q','3','W','P'}; uint32_t schema=1,product=w->options.product;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"Q3WP",4) && qa_source_save_u32(io,&schema) && schema==1 &&
        qa_source_save_u32(io,&product) && product==(uint32_t)w->options.product &&
        qa_source_save_i32(io,&w->selection.weapon) && w->selection.weapon>=0 && w->selection.weapon<16 &&
        qa_source_save_i32(io,&w->selection.time);
}
static bool codec_ready(const q3n_weapons *w,qa_error *e)
{
    const qa_q3_presentation_assets *a=w?w->options.assets:NULL;
    return q3n_weapons_idle(w) && a && a->capturing && a->busy==1 && !a->codec_busy ? true :
        q3p_fail(e,QA_ERROR_ARGUMENT,"Native weapon codec requires the actual backend capture lease");
}
bool q3n_weapons_checkpoint(const q3n_weapons *borrowed,qa_buffer *out,qa_error *e)
{
    if (!out || out->data || out->size || !codec_ready(borrowed,e)) return false;
    q3n_weapons *w=(q3n_weapons *)borrowed; w->busy=true; q3n_weapons saved=*w; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && weapon_fields(&io,&saved) && qa_source_save_finish(&io,out);
    if (!okay && e && e->code==QA_OK) q3p_fail(e,QA_ERROR_FORMAT,"Native weapon checkpoint is inconsistent");
    qa_source_save_dispose(&io); w->busy=false; return okay;
}
bool q3n_weapons_restore(q3n_weapons *w,qa_bytes bytes,qa_error *e)
{
    if (!codec_ready(w,e)) return false;
    w->busy=true; q3n_weapons candidate=*w; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && weapon_fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    if (okay) w->selection=candidate.selection;
    else if (e && e->code==QA_OK) q3p_fail(e,QA_ERROR_FORMAT,"Saved native weapon state is inconsistent");
    qa_source_save_dispose(&io); w->busy=false; return okay;
}
