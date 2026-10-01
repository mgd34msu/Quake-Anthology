#include "packet.h"
#include "attachments.h"
#include "trajectory.h"
#include "qa/game_q3.h"
#include "qa/game_q3_source.h"

#include <limits.h>
#include <string.h>

static float add(float a, float b) { volatile float v = a + b; return v; }
static float mul(float a, float b) { volatile float v = a * b; return v; }
static float divide(float a, float b) { volatile float v = a / b; return v; }
static int32_t word(uint32_t v) { int32_t out; memcpy(&out, &v, sizeof(out)); return out; }
static int32_t sum(int32_t a, int32_t b) { return word((uint32_t)a + (uint32_t)b); }
static int32_t difference(int32_t a, int32_t b) { return word((uint32_t)a - (uint32_t)b); }
static int32_t integer(float v) { return isfinite(v) && v >= -2147483648.0f && v < 2147483648.0f ? (int32_t)truncf(v) : INT32_MIN; }
static qa_vec3 vector(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static qa_vec3 plus(qa_vec3 a, qa_vec3 b) { return qa_v3(add(a.x,b.x),add(a.y,b.y),add(a.z,b.z)); }
static qa_vec3 scale(qa_vec3 a, float s) { return qa_v3(mul(a.x,s),mul(a.y,s),mul(a.z,s)); }
static float dot(qa_vec3 a, qa_vec3 b) { return add(add(mul(a.x,b.x),mul(a.y,b.y)),mul(a.z,b.z)); }
static qa_vec3 cross(qa_vec3 a, qa_vec3 b) {
    return qa_v3(add(mul(a.y,b.z),-mul(a.z,b.y)),add(mul(a.z,b.x),-mul(a.x,b.z)),add(mul(a.x,b.y),-mul(a.y,b.x)));
}
static qa_vec3 normalize(qa_vec3 v) {
    float length=(float)sqrt((double)dot(v,v)); return length==0 ? v : scale(v,divide(1,length));
}
static qa_vec3 perpendicular(qa_vec3 v) {
    float minimum=1; qa_vec3 axis={1,0,0};
    if (fabsf(v.x)<minimum) { minimum=fabsf(v.x); axis=qa_v3(1,0,0); }
    if (fabsf(v.y)<minimum) { minimum=fabsf(v.y); axis=qa_v3(0,1,0); }
    if (fabsf(v.z)<minimum) axis=qa_v3(0,0,1);
    float inverse=divide(1,dot(v,v)), distance=mul(dot(v,axis),inverse);
    qa_vec3 normal=scale(v,inverse);
    return normalize(plus(axis,scale(normal,-distance)));
}
static void matrix(const qa_vec3 a[3], const qa_vec3 b[3], qa_vec3 out[3]) {
    qa_vec3 columns[3]={{b[0].x,b[1].x,b[2].x},{b[0].y,b[1].y,b[2].y},{b[0].z,b[1].z,b[2].z}}, result[3];
    for(unsigned i=0;i<3;++i) result[i]=qa_v3(dot(a[i],columns[0]),dot(a[i],columns[1]),dot(a[i],columns[2]));
    memcpy(out,result,sizeof(result));
}
static qa_vec3 rotate(qa_vec3 direction, qa_vec3 point, float degrees) {
    qa_vec3 radial=perpendicular(direction), vertical=cross(radial,direction);
    float radians=divide(mul(degrees,3.14159265358979323846f),180);
    float cosine=(float)cos((double)radians), sine=(float)sin((double)radians);
    qa_vec3 basis[3]={{radial.x,vertical.x,direction.x},{radial.y,vertical.y,direction.y},{radial.z,vertical.z,direction.z}};
    qa_vec3 inverse[3]={radial,vertical,direction}, turn[3]={{cosine,sine,0},{-sine,cosine,0},{0,0,1}}, first[3], result[3];
    matrix(basis,turn,first); matrix(first,inverse,result);
    return qa_v3(dot(result[0],point),dot(result[1],point),dot(result[2],point));
}
static qa_vec3 missile_direction(const qa_q3_trajectory *t) {
    qa_vec3 direction=vector(t->delta); return dot(direction,direction)==0 ? qa_v3(0,0,1) : normalize(direction);
}
static void direction_axis(qa_vec3 direction, int32_t yaw, qa_vec3 axis[3]) {
    qa_vec3 side=perpendicular(direction); if(yaw) side=rotate(direction,side,(float)yaw);
    axis[0]=direction; axis[1]=side; axis[2]=cross(direction,side);
}
static void scale_axis(qa_vec3 axis[3], float amount) { for(unsigned i=0;i<3;++i) axis[i]=scale(axis[i],amount); }
static bool range(int32_t index, size_t count, const char *kind, qa_error *error) {
    if(index>=0 && (size_t)index<count) return true;
    qa_error_set(error,QA_ERROR_FORMAT,0,"Native Q3 %s index %d is outside its actual registry",kind,index); return false;
}
static bool body(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,
    const q3n_packet_imports *imports,const qa_q3_ref_entity *ref,qa_error *error) {
    return imports->body ? imports->body(imports->context,f,source,entity,ref,error)
        : qa_q3_presentation_entity(f->presentation,ref,error);
}
bool q3n_packet_sound_position(const q3n_frame *f,const qa_application_native_q3_entity *source,
    const q3n_entity *entity,qa_error *error) {
    const q3n_media_view *media=q3n_media_read(f->media); qa_vec3 origin=entity->lerp_origin;
    if(source->state.solid==0xffffff) {
        if(!range(source->state.modelindex,media->inline_count,"inline model",error)) return false;
        origin=plus(origin,media->inline_models[source->state.modelindex].midpoint);
    }
    return qa_q3_presentation_sound_position(f->presentation,source->state.number,origin,error);
}
static bool effects(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,qa_error *error) {
    const qa_q3_entity *s=&source->state; const q3n_media_view *media=q3n_media_read(f->media);
    if(!q3n_packet_sound_position(f,source,entity,error)) return false;
    if(!entity->loop_stopped && s->loopSound && (!range(s->loopSound,256,"sound",error) ||
        !qa_q3_presentation_loop(f->presentation,media->game_sounds[s->loopSound],s->number,entity->lerp_origin,
            qa_v3(0,0,0),s->eType==7,error))) return false;
    uint32_t light=(uint32_t)s->constantLight;
    return !light || qa_q3_presentation_light(f->presentation,entity->lerp_origin,(float)((light>>24)&255u)*4,
        qa_v3((float)(light&255u),(float)((light>>8)&255u),(float)((light>>16)&255u)),false,error);
}
static bool item(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,
    const q3n_packet_options *options,const q3n_packet_imports *imports,qa_error *error) {
    const qa_q3_entity *s=&source->state; const q3n_media_view *media=q3n_media_read(f->media);
    size_t count; const qa_q3_item *items=qa_q3_items(f->source.product,&count);
    if(!range(s->modelindex,count,"item",error)) return false;
    if(!s->modelindex || (s->eFlags&128)) return true;
    const qa_q3_item *definition=&items[s->modelindex]; const q3n_item_media *visual=&media->items[s->modelindex];
    qa_q3_ref_entity ref={0};
    if(options->simple_items && definition->kind!=QA_Q3_ITEM_TEAM) {
        ref.kind=QA_Q3_REF_SPRITE; ref.origin=entity->lerp_origin; ref.radius=14; ref.custom_shader=visual->icon;
        memset(ref.color,255,sizeof(ref.color)); return body(f,source,entity,imports,&ref,error);
    }
    float bob=add(4,mul((float)cos((double)mul((float)sum(f->time,1000),
        add(0.005f,mul((float)s->number,0.00001f)))),4));
    entity->lerp_origin.z=add(entity->lerp_origin.z,bob);
    ref.kind=QA_Q3_REF_MODEL; ref.model=visual->models[0];
    float yaw=definition->kind==QA_Q3_ITEM_HEALTH ? (float)((f->time&1023)*360)/1024
        : (float)((f->time&2047)*360)/2048;
    entity->lerp_angles=qa_v3(0,yaw,0); q3n_angles_axis(entity->lerp_angles,ref.axis);
    const q3n_weapon_media *weapon=NULL;
    if(definition->kind==QA_Q3_ITEM_WEAPON) {
        if(!range(definition->tag,16,"weapon",error)) return false;
        weapon=&media->weapons[definition->tag];
        qa_vec3 midpoint=weapon->weapon_midpoint;
        qa_vec3 offset=plus(plus(scale(ref.axis[0],midpoint.x),scale(ref.axis[1],midpoint.y)),scale(ref.axis[2],midpoint.z));
        entity->lerp_origin=plus(entity->lerp_origin,scale(offset,-1)); entity->lerp_origin.z=add(entity->lerp_origin.z,8);
    }
    ref.origin=ref.old_origin=entity->lerp_origin;
    int32_t elapsed=difference(f->time,entity->misc_time); float fraction=1;
    if(elapsed>=0 && elapsed<1000) { fraction=divide((float)elapsed,1000); scale_axis(ref.axis,fraction); ref.non_normalized_axes=true; }
    if(definition->kind==QA_Q3_ITEM_WEAPON || definition->kind==QA_Q3_ITEM_ARMOR) ref.flags|=1;
    if(weapon) {
        scale_axis(ref.axis,1.5f); ref.non_normalized_axes=true;
        if(f->source.product==QA_Q3_TEAM_ARENA && !qa_q3_presentation_loop(f->presentation,
            media->sounds[Q3N_S_WEAPON_HOVER],s->number,entity->lerp_origin,qa_v3(0,0,0),false,error)) return false;
    }
    if(f->source.product==QA_Q3_TEAM_ARENA && definition->kind==QA_Q3_ITEM_HOLDABLE && definition->tag==QA_Q3_H_KAMIKAZE) {
        scale_axis(ref.axis,2); ref.non_normalized_axes=true;
    }
    if(!body(f,source,entity,imports,&ref,error)) return false;
    if(f->source.product==QA_Q3_TEAM_ARENA && weapon && weapon->barrel_model) {
        qa_q3_ref_entity barrel={.kind=QA_Q3_REF_MODEL,.model=weapon->barrel_model,
            .lighting_origin=ref.lighting_origin,.shadow_plane=ref.shadow_plane,.flags=ref.flags};
        if(!q3n_attach(f->assets,&barrel,&ref,"tag_barrel",true,error)) return false;
        memcpy(barrel.axis,ref.axis,sizeof(ref.axis)); barrel.non_normalized_axes=ref.non_normalized_axes;
        if(!body(f,source,entity,imports,&barrel,error)) return false;
    }
    if(!options->simple_items && (definition->kind==QA_Q3_ITEM_HEALTH || definition->kind==QA_Q3_ITEM_POWERUP) && visual->models[1]) {
        ref.model=visual->models[1]; yaw=0;
        if(definition->kind==QA_Q3_ITEM_POWERUP) { ref.origin.z=add(ref.origin.z,12); yaw=divide((float)((f->time&1023)*360),-1024); }
        q3n_angles_axis(qa_v3(0,yaw,0),ref.axis);
        if(fraction!=1) { scale_axis(ref.axis,fraction); ref.non_normalized_axes=true; }
        if(!body(f,source,entity,imports,&ref,error)) return false;
    }
    return true;
}
static bool missile(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,
    const q3n_packet_imports *imports,bool grapple,qa_error *error) {
    const qa_q3_entity *s=&source->state; const q3n_media_view *media=q3n_media_read(f->media);
    int32_t weapon_index=s->weapon>(f->source.product==QA_Q3_TEAM_ARENA ? 14 : 11) ? 0 : s->weapon;
    if(!range(weapon_index,16,"weapon",error)) return false;
    const q3n_weapon_media *weapon=&media->weapons[weapon_index]; entity->lerp_angles=vector(s->angles);
    if((grapple || weapon->trail!=Q3N_TRAIL_NONE) &&
        !imports->trail(imports->context,f,source,entity,weapon,grapple,error)) return false;
    if(!grapple && weapon->missile_light && !qa_q3_presentation_light(f->presentation,
        entity->lerp_origin,weapon->missile_light,weapon->missile_light_color,false,error)) return false;
    if(!grapple && weapon->missile_sound) {
        qa_vec3 velocity; if(!q3n_trajectory_delta(&s->pos,f->time,&velocity,error) ||
            !qa_q3_presentation_loop(f->presentation,weapon->missile_sound,s->number,entity->lerp_origin,velocity,false,error)) return false;
    }
    qa_q3_ref_entity ref={.origin=entity->lerp_origin,.old_origin=entity->lerp_origin};
    if(!grapple && s->weapon==QA_Q3_W_PLASMA) {
        ref.kind=QA_Q3_REF_SPRITE; ref.radius=16; ref.custom_shader=media->graphics[Q3N_G_PLASMA_BALL];
        return body(f,source,entity,imports,&ref,error);
    }
    ref.kind=QA_Q3_REF_MODEL; ref.model=weapon->missile_model; ref.skin=f->client_frame&1; ref.flags=weapon->missile_render_flags|64;
    if(!grapple && f->source.product==QA_Q3_TEAM_ARENA && s->weapon==QA_Q3_W_PROX && s->generic1==2)
        ref.model=media->graphics[Q3N_G_BLUE_PROX_MINE];
    qa_vec3 direction=missile_direction(&s->pos);
    if(grapple) { ref.axis[0]=direction; return body(f,source,entity,imports,&ref,error); }
    if(s->pos.type!=0) direction_axis(direction,f->time/4,ref.axis);
    else if(f->source.product==QA_Q3_TEAM_ARENA && s->weapon==QA_Q3_W_PROX) q3n_angles_axis(entity->lerp_angles,ref.axis);
    else direction_axis(direction,s->time,ref.axis);
    return imports->powerups(imports->context,f,source,entity,&ref,0,error);
}
static bool team(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,
    const q3n_packet_options *options,const q3n_packet_imports *imports,qa_error *error) {
    const qa_q3_entity *s=&source->state; const q3n_media_view *media=q3n_media_read(f->media);
    qa_q3_ref_entity ref={.kind=QA_Q3_REF_MODEL,.origin=entity->lerp_origin,.lighting_origin=entity->lerp_origin};
    q3n_angles_axis(vector(s->angles),ref.axis); int32_t game_type=f->source.game_type;
    if(game_type==4 || (f->source.product==QA_Q3_TEAM_ARENA && game_type==5)) {
        ref.model=media->graphics[s->modelindex==1 ? Q3N_G_RED_FLAG_BASE : s->modelindex==2 ? Q3N_G_BLUE_FLAG_BASE : Q3N_G_NEUTRAL_FLAG_BASE];
        return body(f,source,entity,imports,&ref,error);
    }
    if(f->source.product!=QA_Q3_TEAM_ARENA) return true;
    if(game_type==7) {
        ref.model=media->graphics[s->modelindex==1 || s->modelindex==2 ? Q3N_G_HARVESTER : Q3N_G_HARVESTER_NEUTRAL];
        ref.custom_skin=s->modelindex==1 ? media->graphics[Q3N_G_HARVESTER_RED_SKIN] : s->modelindex==2 ? media->graphics[Q3N_G_HARVESTER_BLUE_SKIN] : 0;
        return body(f,source,entity,imports,&ref,error);
    }
    if(game_type!=6) return true;
    ref.model=media->graphics[Q3N_G_OVERLOAD_BASE]; if(!body(f,source,entity,imports,&ref,error)) return false;
    uint8_t health=(uint8_t)integer((float)s->modelindex2);
    if(s->frame==1) { ref.color[0]=ref.color[3]=255; ref.color[1]=ref.color[2]=health; ref.model=media->graphics[Q3N_G_OVERLOAD_ENERGY];
        if(!body(f,source,entity,imports,&ref,error)) return false; }
    if(s->frame!=2) {
        entity->misc_time=entity->muzzle_flash_time=0;
        ref.color[0]=ref.color[3]=255; ref.color[1]=ref.color[2]=health; ref.model=media->graphics[Q3N_G_OVERLOAD_LIGHTS];
        if(!body(f,source,entity,imports,&ref,error)) return false;
        ref.origin.z=add(ref.origin.z,56); ref.model=media->graphics[Q3N_G_OVERLOAD_TARGET]; return body(f,source,entity,imports,&ref,error);
    }
    if(!entity->misc_time) entity->misc_time=f->time;
    int32_t elapsed=difference(f->time,entity->misc_time), threshold=word((uint32_t)difference(options->obelisk_respawn_delay,5)*1000u);
    float amount=elapsed>threshold ? fminf(1,divide((float)difference(elapsed,threshold),(float)threshold)) : 0;
    memset(ref.color,(uint8_t)integer(mul(amount,255)),sizeof(ref.color)); ref.model=media->graphics[Q3N_G_OVERLOAD_LIGHTS];
    if(!body(f,source,entity,imports,&ref,error)) return false;
    if(elapsed>threshold) {
        if(!entity->muzzle_flash_time) {
            if(!qa_q3_presentation_sound(f->presentation,media->sounds[Q3N_S_OBELISK_RESPAWN],&entity->lerp_origin,1023,5,false,error)) return false;
            entity->muzzle_flash_time=1;
        }
        float spin=divide(mul(mul(16,(float)acos((double)add(1,-amount))),180),3.14159265358979323846f);
        qa_vec3 angles=vector(s->angles); angles.y=add(angles.y,spin); q3n_angles_axis(angles,ref.axis); scale_axis(ref.axis,amount);
        memset(ref.color,255,sizeof(ref.color)); ref.origin.z=add(ref.origin.z,56); ref.model=media->graphics[Q3N_G_OVERLOAD_TARGET];
        if(!body(f,source,entity,imports,&ref,error)) return false;
    }
    return true;
}
bool q3n_packet_entity(const q3n_frame *f,const qa_application_native_q3_entity *source,q3n_entity *entity,
    const q3n_packet_options *options,const q3n_packet_imports *imports,qa_error *error) {
    const qa_q3_entity *s=&source->state; const q3n_media_view *media=q3n_media_read(f->media);
    uint32_t physical;
    if(!media || !imports || !imports->rand || !imports->player || !imports->trail || !imports->powerups ||
        !qa_application_native_q3_presentation_current(f->application,&f->source) || f->time!=f->source.source_time_ms ||
        !qa_actor_id_equal(entity->actor,source->binding.actor) || !entity->valid || !source->present ||
        !qa_q3_source_actor_slot(f->source.source_game,source->binding.actor,&physical,error) || physical!=entity->physical) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Native packet lacks its actual qualified source and child consumers"); return false;
    }
    if(s->eType>=13) return true;
    qa_q3_trajectory pos=s->pos;
    if(!options->smooth_clients && s->number<64) pos.type=1;
    if(!q3n_trajectory(&pos,f->time,&entity->lerp_origin,error) ||
        !q3n_trajectory(&s->apos,f->time,&entity->lerp_angles,error) || !effects(f,source,entity,error)) return false;
    qa_q3_ref_entity ref={.kind=QA_Q3_REF_MODEL};
    switch(s->eType) {
    case 8: case 9: case 10: return true;
    case 0:
        if(!s->modelindex) return true;
        if(!range(s->modelindex,256,"game model",error)) return false;
        ref.model=media->game_models[s->modelindex]; ref.frame=ref.old_frame=s->frame;
        ref.origin=ref.old_origin=entity->lerp_origin; q3n_angles_axis(entity->lerp_angles,ref.axis);
        if(f->has_local_player && s->number==f->local_player.clientNum) ref.flags|=2;
        return body(f,source,entity,imports,&ref,error);
    case 1: return imports->player(imports->context,f,source,entity,error);
    case 2: return item(f,source,entity,options,imports,error);
    case 3: return missile(f,source,entity,imports,false,error);
    case 4:
        if(s->solid==0xffffff) { if(!range(s->modelindex,media->inline_count,"inline model",error)) return false; ref.model=media->inline_models[s->modelindex].model; }
        else { if(!range(s->modelindex,256,"game model",error)) return false; ref.model=media->game_models[s->modelindex]; }
        ref.origin=ref.old_origin=entity->lerp_origin; q3n_angles_axis(entity->lerp_angles,ref.axis); ref.flags=64; ref.skin=(f->time>>6)&1;
        if(!body(f,source,entity,imports,&ref,error)) return false;
        if(!s->modelindex2) return true;
        if(!range(s->modelindex2,256,"game model",error)) return false;
        ref.skin=0; ref.model=media->game_models[s->modelindex2]; return body(f,source,entity,imports,&ref,error);
    case 5:
        ref.kind=QA_Q3_REF_BEAM; ref.origin=vector(s->pos.base); ref.old_origin=vector(s->origin2); ref.flags=64;
        ref.axis[0]=qa_v3(1,0,0); ref.axis[1]=qa_v3(0,1,0); ref.axis[2]=qa_v3(0,0,1);
        return body(f,source,entity,imports,&ref,error);
    case 6: {
        ref.kind=QA_Q3_REF_PORTAL; ref.origin=entity->lerp_origin; ref.old_origin=vector(s->origin2);
        qa_vec3 forward={0}; if(s->eventParm>=0 && s->eventParm<(int32_t)QA_BYTE_NORMAL_COUNT) qa_byte_normal((uint8_t)s->eventParm,&forward);
        qa_vec3 side=scale(perpendicular(forward),-1); ref.axis[0]=forward; ref.axis[1]=side; ref.axis[2]=cross(forward,side);
        ref.old_frame=s->powerups; ref.frame=s->frame; ref.skin=integer(mul(divide((float)s->clientNum,256),360));
        return body(f,source,entity,imports,&ref,error);
    }
    case 7: {
        if(!s->clientNum || f->time<entity->misc_time) return true;
        if(!range(s->eventParm,256,"sound",error) || !qa_q3_presentation_sound(f->presentation,media->game_sounds[s->eventParm],NULL,s->number,4,false,error)) return false;
        float random=divide((float)((uint32_t)imports->rand(imports->context)&32767u),32767), crandom=mul(2,add(random,-0.5f));
        entity->misc_time=integer(add((float)sum(f->time,word((uint32_t)s->frame*100u)),mul((float)word((uint32_t)s->clientNum*100u),crandom)));
        return true;
    }
    case 11: return missile(f,source,entity,imports,true,error);
    case 12: return team(f,source,entity,options,imports,error);
    default: qa_error_set(error,QA_ERROR_FORMAT,0,"Bad native Q3 entity type: %d",s->eType); return false;
    }
}
