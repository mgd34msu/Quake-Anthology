#include "internal.h"
#include "source_inventory.h"
#include "source_player.h"
#include "source_view.h"
#include "source_combat_vectors.h"
#include "source_timers.h"
#include "source_flags.h"
#include "source_storage.h"
#include "qa/bot_movement_source.h"

enum { BOT_SOLID=1, BOT_LIQUID=8|16|32, BOT_FOG=64, BOT_PLAYERCLIP=0x10000,
       BOT_SHOT=1|0x2000000|0x4000000, BOT_FIRE_RELEASED=1, BOT_RADIAL=2 };

static qa_vec3 weapon_muzzle(const qa_bot_weapon_knowledge *weapon,
                             const qa_bot_player *player,qa_vec3 angles) {
    if(weapon->muzzle_count) return qa_vec_add(player->origin,weapon->muzzle_offsets[0]);
    qa_vec3 forward,right;
    qa_builtin_angle_vectors(angles,&forward,&right,NULL);
    qa_vec3 muzzle=qa_vec_add(player->eye,qa_vec_add(qa_vec_scale(forward,weapon->weapon.offset.x),
                                                     qa_vec_scale(right,weapon->weapon.offset.y)));
    muzzle.z+=weapon->weapon.offset.z;
    return muzzle;
}

qa_vec3 bot_ai_angles(qa_vec3 direction) {
    float yaw,pitch;const float pi=3.14159265358979323846f;
    if(direction.x==0 && direction.y==0) {yaw=0;pitch=direction.z>0?90:270;}
    else {
        if(direction.x!=0) {
            volatile float radians=(float)atan2((double)direction.y,(double)direction.x);
            volatile float degrees=radians*180;
            yaw=degrees/pi;
        } else yaw=direction.y>0?90:270;
        if(yaw<0) yaw+=360;
        volatile float x_squared=direction.x*direction.x,y_squared=direction.y*direction.y;
        volatile float squared=x_squared+y_squared;
        volatile float horizontal=(float)sqrt((double)squared);
        volatile float radians=(float)atan2((double)direction.z,(double)horizontal);
        volatile float degrees=radians*180;
        pitch=degrees/pi;
        if(pitch<0) pitch+=360;
    }
    return qa_v3(-pitch,yaw,0);
}
static bool in_view(qa_vec3 angles, qa_vec3 direction, float fov) {
    qa_vec3 target = bot_ai_angles(direction);
    return fabsf(qa_builtin_angle_delta(target.x, angles.x)) <= fov*.5f &&
           fabsf(qa_builtin_angle_delta(target.y, angles.y)) <= fov*.5f;
}
static qa_bot_navigation *navigation(qa_bots *b, bot_ai_state *s) {
    return qa_bot_runtime_navigation(b->runtime, (int32_t)s->view.client);
}
static bool trace(qa_bots *b, bot_ai_state *s, qa_vec3 start, qa_vec3 end,
                  const qa_bounds *bounds, qa_actor_id pass, uint32_t mask,
                  qa_trace_result *result, qa_error *e) {
    qa_bot_navigation *nav = navigation(b, s);
    return nav ? qa_bot_navigation_trace(nav, start, end, bounds, pass, mask, result, e) :
                 bot_ai_fail(e, "bot combat navigation is unavailable");
}
bool bot_ai_same_team(qa_bots *b, bot_ai_state *s, qa_actor_id other, bool *same, qa_error *e) {
    *same = false;
    if (!b->services.modes || !s->view.mode.generation) return true;
    qa_mode_view mode;
    if (!qa_modes_read(b->services.modes, s->view.mode, &mode, e)) return false;
    qa_builtin_player_info player;
    bool is_player=b->services.shared.player_info(b->services.shared.context,other,&player) && player.connected;
    if (!is_player) {
        qa_combat_state self,target;
        if(!qa_combat_read(b->services.shared.combat,s->view.actor,&self,e) ||
           !qa_combat_read(b->services.shared.combat,other,&target,e)) return false;
        *same=self.team && self.team==target.team;
        return true;
    }
    if (mode.rules.kind == QA_MODE_COOPERATIVE || mode.rules.kind == QA_MODE_HORDE) {
        *same = true;
        return true;
    }
    if (mode.rules.kind == QA_MODE_FFA || mode.rules.kind == QA_MODE_DUEL ||
        mode.rules.kind == QA_MODE_SINGLE_PLAYER) return true;
    *same = qa_modes_same_team(b->services.modes, s->view.mode, s->view.actor, other);
    return true;
}
bool bot_ai_target(qa_bots *b, bot_ai_state *s, qa_actor_id actor, qa_bot_player *out,
                     bool *present, qa_error *e) {
    *present=false;
    if(!bot_ai_live(b,actor)) return true;
    qa_builtin_player_info info;
    if(b->services.shared.player_info(b->services.shared.context,actor,&info) && info.connected) {
        if(!b->services.player(b->services.context,actor,out,NULL,e)) return false;
        *present=!s->retired && bot_ai_live(b,s->view.actor) && bot_ai_live(b,actor) && out->connected;
        return true;
    }
    qa_builtin_actor_traits traits={0};
    if(!b->services.shared.actor_traits || !b->services.shared.actor_traits(b->services.shared.context,actor,&traits) ||
       traits.no_target || (!traits.monster && !traits.damageable_target)) return true;
    qa_body_state body;qa_combat_state combat;
    if(!qa_world_body_read(b->services.shared.world,actor,&body,e) ||
       !qa_combat_read(b->services.shared.combat,actor,&combat,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,actor)) return true;
    *out=(qa_bot_player){.connected=true,.dead=combat.health<=0 || !combat.can_take_damage,
        .origin=body.origin,.velocity=body.velocity,.view_angles=body.angles,
        .eye=qa_vec_add(body.origin,qa_v3(0,0,traits.view_height)),.invisible=traits.invisible};
    *present=true;
    return true;
}
bool bot_ai_enemy_visible(qa_bots *b, bot_ai_state *s, qa_actor_id actor,
                           float *visibility, qa_error *e) {
    *visibility = 0;
    if (!bot_ai_live(b, actor)) return true;
    qa_bot_entity target;
    if (!b->services.entity(b->services.context, actor, &target, e)) return false;
    if (s->retired || !bot_ai_live(b, s->view.actor) || !bot_ai_live(b, actor) ||
        !target.present || !target.linked || target.hidden) return true;
    qa_bot_navigation *nav = navigation(b, s);
    int32_t eye_contents;
    if (!nav || !qa_bot_navigation_contents(nav, s->player.eye, &eye_contents, e)) return false;
    qa_vec3 center = qa_vec_add(target.observation.origin,
        qa_vec_scale(qa_vec_add(target.observation.mins, target.observation.maxs), .5f));
    for (unsigned i = 0; i < 3; ++i) {
        qa_vec3 end = center;
        if (i == 1) end.z += target.observation.mins.z;
        if (i == 2) end.z += target.observation.maxs.z - target.observation.mins.z;
        int32_t contents;
        if (!qa_bot_navigation_contents(nav, end, &contents, e)) return false;
        uint32_t mask = BOT_SOLID | BOT_PLAYERCLIP;
        if (contents & BOT_LIQUID) mask |= BOT_LIQUID;
        qa_vec3 start = s->player.eye, finish = end;
        qa_actor_id pass = s->view.actor;
        if (eye_contents & BOT_LIQUID) {
            if (!(contents & BOT_LIQUID)) { start = end; finish = s->player.eye; pass = actor; }
            mask ^= BOT_LIQUID;
        }
        qa_trace_result hit;
        if (!trace(b, s, start, finish, NULL, pass, mask, &hit, e)) return false;
        if (s->retired || !bot_ai_live(b, s->view.actor) || !bot_ai_live(b, actor)) return true;
        if (hit.fraction < 1 && !qa_actor_id_equal(hit.actor, actor)) continue;
        float seen = 1;
        if ((eye_contents | contents) & BOT_FOG) {
            qa_vec3 fog_start = s->player.eye, fog_end = end;
            if (!(eye_contents & BOT_FOG)) {
                if (!trace(b, s, end, s->player.eye, NULL, actor, BOT_FOG, &hit, e)) return false;
                fog_start = hit.end;
            } else if (!(contents & BOT_FOG)) {
                if (!trace(b, s, s->player.eye, end, NULL, s->view.actor, BOT_FOG, &hit, e)) return false;
                fog_end = hit.end;
            }
            qa_vec3 distance = qa_vec_sub(fog_end, fog_start);
            seen = 1.0f / fmaxf(1, qa_vec_dot(distance, distance)*.001f);
        }
        if (seen > *visibility) *visibility = seen;
        if (*visibility > .95f) break;
    }
    return true;
}
static bool arsenal(qa_bots *b, bot_ai_state *s, const qa_bot_weapon_knowledge **weapons,
                     size_t *count, void **lease, qa_error *e) {
    *weapons = NULL; *count = 0; *lease = NULL;
    if (!b->services.arsenal(b->services.context, s->view.actor, weapons, count, lease, e)) return false;
    if (*count && !*weapons) {
        b->services.arsenal_end(b->services.context, *lease);
        return bot_ai_fail(e, "selected bot arsenal returned absent weapon storage");
    }
    return true;
}
bool bot_ai_choose_weapon(qa_bots *b, bot_ai_state *s, qa_error *e) {
    /* Q3 raising/dropping are source states 1 and 2. Providers project their
     * selected weapon phase instead of exposing another arsenal's numbering. */
    int32_t weapon_state;
    if (!bot_ai_source_player_word(b,s,BOT_PS_WEAPON_STATE,&weapon_state,e)) return false;
    if (weapon_state == 1 || weapon_state == 2) return true;
    const qa_bot_weapon_knowledge *weapons; size_t count; void *lease;
    if (!arsenal(b, s, &weapons, &count, &lease, e)) return false;
    int32_t choice = s->view.weapon;
    bool ok = s->retired || !bot_ai_live(b, s->view.actor) ||
        qa_bot_knowledge_choose(b->runtime, s->weapons, weapons, count,
            bot_ai_inventory(s), b->inventory_scratch, &choice, e);
    b->services.arsenal_end(b->services.context, lease);
    if (ok && !s->retired && bot_ai_live(b, s->view.actor)) {
        if (s->view.weapon != choice) bot_ai_weapon_change_time_set(s,b->time);
        s->view.weapon = choice;
    }
    return ok;
}
bool bot_ai_retreat(qa_bots *b, bot_ai_state *s, bool *retreat, qa_error *e) {
    const qa_bot_weapon_knowledge *weapons; size_t count; void *lease;
    if (!arsenal(b, s, &weapons, &count, &lease, e)) return false;
    float aggression = qa_bot_knowledge_aggression(weapons, count, s->view.weapon, bot_ai_inventory(s));
    b->services.arsenal_end(b->services.context, lease);
    *retreat = aggression < 50 || s->player.carrying_objective;
    return true;
}
bool bot_ai_find_enemy(qa_bots *b, bot_ai_state *s, bool *found, qa_error *e) {
    *found = false;
    float alertness, easy;
    if (!bot_ai_character_float(b, s, BOT_C_ALERTNESS, 0, 1, &alertness, e) ||
        !bot_ai_character_float(b, s, BOT_C_EASY_FRAGGER, 0, 1, &easy, e)) return false;
    int32_t last_health;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_HEALTH,&last_health,false,e)) return false;
    bool hurt = last_health > bot_ai_inventory_value(s,QA_BOT_INV_HEALTH);
    last_health=bot_ai_inventory_value(s,QA_BOT_INV_HEALTH);
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_HEALTH,&last_health,true,e)) return false;
    float best = INFINITY;
    if (bot_ai_live(b, s->view.enemy)) {
        qa_body_state enemy;
        if (!qa_world_body_read(b->services.shared.world, s->view.enemy, &enemy, e)) return false;
        qa_vec3 d = qa_vec_sub(enemy.origin, s->player.origin);
        best = qa_vec_dot(d,d);
    }
    for (size_t i = 0; i < b->entities.count; ++i) {
        qa_actor_id actor = b->entities.ids[i];
        if (!bot_ai_live(b, actor) || qa_actor_id_equal(actor, s->view.actor) ||
            qa_actor_id_equal(actor, s->view.enemy)) continue;
        qa_bot_player player;bool present;
        if (!bot_ai_target(b,s,actor,&player,&present,e)) return false;
        if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
        if (!present || !bot_ai_live(b, actor) || !player.connected || player.dead || player.observer ||
            (player.invisible && !player.firing) || (easy < .5f && player.chatting)) continue;
        bool same;
        if (!bot_ai_same_team(b, s, actor, &same, e)) return false;
        if (same) continue;
        qa_vec3 d = qa_vec_sub(player.origin, s->player.origin);
        float distance = qa_vec_dot(d,d), limit = 900+alertness*4000;
        qa_vec3 teleport_delta=qa_vec_sub(player.origin,b->source_event_globals.last_teleport_origin);
        volatile float recent_teleport=b->time-3;
        if (distance > best || distance > limit*limit ||
            (b->source_event_globals.last_teleport_time>recent_teleport &&
             qa_vec_dot(teleport_delta,teleport_delta)<70*70)) continue;
        float fov = !s->view.enemy.registry && !hurt && !player.firing ?
            180-(90-fminf(distance,810*810)/(810*9)) : 360;
        if (!in_view(bot_ai_view_angles(s),d,fov)) continue;
        float visible;
        if (!bot_ai_enemy_visible(b,s,actor,&visible,e)) return false;
        if (!visible) continue;
        if (!s->view.enemy.registry && distance > 100*100 && !hurt && !player.firing &&
            !in_view(player.view_angles,qa_vec_scale(d,-1),90)) {
            bool retreat;
            if (!bot_ai_retreat(b,s,&retreat,e)) return false;
            if (retreat) continue;
        }
        bool previous = s->view.enemy.registry != 0;
        qa_bot_entity source_enemy;
        if(!b->services.entity(b->services.context,actor,&source_enemy,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,actor)) return true;
        s->source_enemy=source_enemy.number;s->source_events.enemy_suicide=false;
        s->view.enemy = actor; bot_ai_enemy_sight_time_set(s,b->time-(previous ? 2 : 0));
        bot_ai_enemy_visible_time_set(s,b->time); bot_ai_enemy_death_time_set(s,0);
        bot_ai_enemy_origin_set(s,player.origin);bot_ai_enemy_velocity_set(s,player.velocity);
        *found = true;
        return true;
    }
    return true;
}
typedef struct bot_move_setup_source {
    qa_bots *bots;
    bot_ai_state *state;
    qa_bot_move_input input;
} bot_move_setup_source;
static bool move_setup_integer(void *opaque,qa_bot_move_init_field field,int32_t *out,
                               qa_error *e) {
    bot_move_setup_source *source=opaque;
    switch(field) {
        case QA_BOT_INIT_ENTITY:*out=source->input.entity;return true;
        case QA_BOT_INIT_CLIENT:*out=source->input.client;return true;
        case QA_BOT_INIT_PRESENCE:*out=(int32_t)source->input.presence;return true;
        case QA_BOT_INIT_FLAGS:*out=(int32_t)source->input.flags;return true;
    }
    return bot_ai_fail(e,"unknown bot movement setup field");
}
static bool move_setup_vector(void *opaque,qa_bot_move_init_vector field,unsigned axis,
                              float *out,qa_error *e) {
    bot_move_setup_source *source=opaque;uint32_t offset;
    if(axis>2) return bot_ai_fail(e,"unknown bot movement setup component");
    switch(field) {
        case QA_BOT_INIT_ORIGIN:offset=QA_BOT_SOURCE_PLAYER+BOT_PS_ORIGIN;break;
        case QA_BOT_INIT_VELOCITY:offset=QA_BOT_SOURCE_PLAYER+BOT_PS_VELOCITY;break;
        case QA_BOT_INIT_VIEW_ANGLES:offset=QA_BOT_SOURCE_VIEW_ANGLES;break;
        case QA_BOT_INIT_VIEW_OFFSET:
            *out=axis==2?source->input.view_offset.z:0;return true;
        default:return bot_ai_fail(e,"unknown bot movement setup vector");
    }
    return bot_ai_storage_f32(source->bots,source->state,offset+axis*4,out,false,e);
}
static bool move_setup_think_time(void *opaque,float *out,qa_error *e) {
    bot_move_setup_source *source=opaque;(void)e;
    *out=source->input.think_time;return true;
}
bool bot_ai_move_setup(qa_bots *b, bot_ai_state *s, qa_error *e) {
    int32_t ground,move_flags,move_time;
    if(!bot_ai_source_player_word(b,s,BOT_PS_GROUND_ENTITY,&ground,e) ||
       !bot_ai_source_player_word(b,s,BOT_PS_MOVE_FLAGS,&move_flags,e)) return false;
    uint32_t flags=ground!=1023?QA_BOT_MOVE_ON_GROUND:0;
    if(move_flags&64) {
        if(!bot_ai_source_player_word(b,s,BOT_PS_MOVE_TIME,&move_time,e)) return false;
        if(move_time>0) flags|=QA_BOT_MOVE_TELEPORTED;
    }
    if(move_flags&256) {
        if(!bot_ai_source_player_word(b,s,BOT_PS_MOVE_TIME,&move_time,e)) return false;
        if(move_time>0) flags|=QA_BOT_MOVE_WATER_JUMP;
    }
    float walker;
    if(!bot_ai_storage_f32(b,s,QA_BOT_SOURCE_WALKER,&walker,false,e)) return false;
    if (walker > .5f) flags |= QA_BOT_MOVE_WALK;
    bot_move_setup_source source={.bots=b,.state=s,.input={.flags=flags}};
    int32_t height;
    if(!bot_ai_source_player_word(b,s,BOT_PS_VIEW_HEIGHT,&height,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_ENTITY,&source.input.entity,false,e) ||
       !bot_ai_storage_i32(b,s,QA_BOT_SOURCE_CLIENT,&source.input.client,false,e) ||
       !bot_ai_storage_f32(b,s,QA_BOT_SOURCE_THINK_TIME,&source.input.think_time,false,e)) return false;
    source.input.view_offset=qa_v3(0,0,(float)height);
    source.input.presence=(move_flags&1)?4:2;
    qa_bot_move_init_source input={.context=&source,.integer=move_setup_integer,
        .vector=move_setup_vector,.think_time=move_setup_think_time};
    return qa_bot_moves_initialize_from(qa_bot_runtime_moves(b->runtime),s->movement,&input,e);
}
bool bot_ai_attack_move(qa_bots *b, bot_ai_state *s, qa_error *e) {
    float skill,jumper,croucher;
    if (!bot_ai_character_float(b,s,BOT_C_ATTACK,0,1,&skill,e) ||
        !bot_ai_character_float(b,s,BOT_C_JUMPER,0,1,&jumper,e) ||
        !bot_ai_character_float(b,s,BOT_C_CROUCHER,0,1,&croucher,e)) return false;
    if (skill < .2f) return true;
    if (!bot_ai_move_setup(b,s,e)) return false;
    qa_vec3 toward=qa_vec_sub(bot_ai_enemy_origin(s),s->player.origin);
    float distance=qa_vec_length(toward);
    qa_vec3 forward=qa_vec_normalize(toward),backward=qa_vec_scale(forward,-1);
    uint32_t type=QA_BOT_DIRECTION_WALK;
    if (bot_ai_attack_crouch_time(s) < b->time-1) {
        float random;if(!bot_ai_random(b,&random,e)) return false;
        if (random<jumper) type=QA_BOT_DIRECTION_JUMP;
        else {
            if(!bot_ai_random(b,&random,e)) return false;
            if(random<croucher) bot_ai_attack_crouch_time_set(s,b->time+croucher*5);
        }
    }
    if (bot_ai_attack_crouch_time(s)>b->time) type=QA_BOT_DIRECTION_CROUCH;
    if (type==QA_BOT_DIRECTION_JUMP) {
        if (bot_ai_attack_jump_time(s)>b->time) type=QA_BOT_DIRECTION_WALK;
        else bot_ai_attack_jump_time_set(s,b->time+1);
    }
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if (!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    bool melee=false;
    for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==s->view.weapon) melee=weapons[i].melee;
    b->services.arsenal_end(b->services.context,lease);
    float desired=melee?0:140,range=melee?0:40;
    qa_bot_moves *moves=qa_bot_runtime_moves(b->runtime);bool moved;
    if(skill<=.4f) {
        if(distance>desired+range) return qa_bot_moves_direction(moves,s->movement,forward,400,type,&moved,e);
        if(distance<desired-range) return qa_bot_moves_direction(moves,s->movement,backward,400,type,&moved,e);
        return true;
    }
    bot_ai_attack_strafe_time_set(s,bot_ai_attack_strafe_time(s)+s->view.think_time);
    float change=.4f+(1-skill)*.2f;
    float random;
    if(skill>.7f) {
        if(!bot_ai_random(b,&random,e)) return false;
        change+=(random*2-1)*.2f;
    }
    if(bot_ai_attack_strafe_time(s)>change) {
        if(!bot_ai_random(b,&random,e)) return false;
        if(random>.935f) {bot_ai_flag_toggle(s,BOT_AI_STRAFE_RIGHT);bot_ai_attack_strafe_time_set(s,0);}
    }
    for(unsigned attempt=0;attempt<2;++attempt) {
        qa_vec3 horizontal=qa_vec_normalize(qa_v3(forward.x,forward.y,0));
        qa_vec3 side=qa_vec_cross(horizontal,qa_v3(0,0,1));
        if(bot_ai_flag(s,BOT_AI_STRAFE_RIGHT)) side=qa_vec_scale(side,-1);
        if(!bot_ai_random(b,&random,e)) return false;
        if(random>.9f) side=qa_vec_add(side,backward);
        else if(distance>desired+range) side=qa_vec_add(side,forward);
        else if(distance<desired-range) side=qa_vec_add(side,backward);
        if(!qa_bot_moves_direction(moves,s->movement,side,400,type,&moved,e)) return false;
        if(moved) break;
        bot_ai_flag_toggle(s,BOT_AI_STRAFE_RIGHT);bot_ai_attack_strafe_time_set(s,0);
    }
    return true;
}
static int32_t source_inventory_integer(float value) {
    return value >= -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN;
}
bool bot_ai_attack(qa_bots *b, bot_ai_state *s, bool moving, qa_error *e) {
    (void)moving;
    qa_actor_id enemy=s->view.enemy;
    if(!bot_ai_live(b,enemy)) return true;
    qa_bot_player target;bool present;
    if(!bot_ai_target(b,s,enemy,&target,&present,e)) return false;
    if(!present || s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,enemy) || target.dead) return true;
    bot_ai_enemy_origin_set(s,target.origin);bot_ai_enemy_velocity_set(s,target.velocity);
    qa_vec3 displacement=qa_vec_sub(target.origin,s->player.origin);
    bot_source_inventory inventory={b,s};
    if(!bot_ai_source_inventory_write(&inventory,QA_BOT_INV_ENEMY_HEIGHT,
        source_inventory_integer(displacement.z),e) ||
       !bot_ai_source_inventory_write(&inventory,QA_BOT_INV_ENEMY_DISTANCE,
        source_inventory_integer(hypotf(displacement.x,displacement.y)),e)) return false;
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    qa_bot_weapon_knowledge selected={0};bool exists=false;
    for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==s->view.weapon) {selected=weapons[i];exists=true;break;}
    b->services.arsenal_end(b->services.context,lease);
    if(!exists || s->retired || !bot_ai_live(b,s->view.actor)) return true;
    qa_bot_weapon_tactics tactics=qa_bot_weapon_tactics_for(&selected);
    float accuracy,skill,reaction,throttle;
    if(!bot_ai_character_float(b,s,tactics.accuracy_characteristic<0?BOT_C_ACCURACY:(uint32_t)tactics.accuracy_characteristic,0,1,&accuracy,e) ||
       !bot_ai_character_float(b,s,tactics.skill_characteristic<0?BOT_C_AIM_SKILL:(uint32_t)tactics.skill_characteristic,0,1,&skill,e) ||
       !bot_ai_character_float(b,s,BOT_C_REACTION,0,5,&reaction,e) ||
       !bot_ai_character_float(b,s,BOT_C_FIRE_THROTTLE,0,1,&throttle,e)) return false;
    if(skill>.95f && bot_ai_enemy_sight_time(s)>b->time-reaction*.5f) return true;
    float random;
    if(target.invisible) {
        if(!bot_ai_random(b,&random,e)) return false;
        if(random>.1f) accuracy*=.4f;
    }
    qa_vec3 aim=qa_vec_add(target.origin,qa_v3(0,0,8));
    float distance=qa_vec_length(qa_vec_sub(aim,s->player.eye));
    if(selected.weapon.speed>0 && skill>.4f) {
        float flight=distance/selected.weapon.speed+selected.launch_delay;
        bool predicted=false;
        int32_t weapon_state=0;
        if(skill>.8f && !bot_ai_source_player_word(b,s,BOT_PS_WEAPON_STATE,&weapon_state,e)) return false;
        if(skill>.8f && weapon_state==0 && b->services.predict_motion) {
            qa_bot_movement_prediction_query query={.origin=qa_vec_add(target.origin,qa_v3(0,0,1)),
                .velocity=target.velocity,.presence=target.presence==4?4:2,.on_ground=target.grounded,
                .maximum_frames=(int32_t)fminf(ceilf(flight*10),200),.frame_time=.1f};
            qa_bot_movement_prediction result;
            if(!b->services.predict_motion(b->services.context,enemy,&query,&result,&predicted,e)) return false;
            if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,enemy)) return true;
            if(predicted) aim=qa_vec_add(result.end,qa_v3(0,0,8));
        }
        if(!predicted) {aim.x+=target.velocity.x*flight;aim.y+=target.velocity.y*flight;}
    }
    qa_trace_result hit;
    qa_bounds shot_bounds={qa_v3(-4,-4,-4),qa_v3(4,4,4)};
    qa_vec3 muzzle=selected.muzzle_count?weapon_muzzle(&selected,&s->player,bot_ai_view_angles(s)):
        qa_vec_add(s->player.eye,qa_v3(0,0,selected.weapon.offset.z));
    if(!trace(b,s,muzzle,aim,&shot_bounds,s->view.actor,BOT_SHOT,&hit,e)) return false;
    if(hit.fraction<1 && !qa_actor_id_equal(hit.actor,enemy)) aim.z+=16;
    if(skill>.6f && (selected.projectile.damage_type&BOT_RADIAL) && target.origin.z<s->player.origin.z+16) {
        qa_trace_result floor,impact,visible;
        if(!trace(b,s,target.origin,qa_vec_add(target.origin,qa_v3(0,0,-64)),NULL,enemy,BOT_SHOT,&floor,e)) return false;
        qa_vec3 ground=qa_v3(aim.x,aim.y,floor.start_solid?target.origin.z-16:floor.end.z-8);
        if(!trace(b,s,muzzle,ground,NULL,s->view.actor,BOT_SHOT,&impact,e)) return false;
        qa_vec3 error=qa_vec_sub(impact.end,ground),self=qa_vec_sub(impact.end,muzzle);
        if(fabsf(error.z)<50 && qa_vec_dot(error,error)<3600 && qa_vec_dot(self,self)>10000) {
            if(!trace(b,s,qa_vec_add(impact.end,qa_v3(0,0,1)),target.origin,NULL,enemy,BOT_SHOT,&visible,e)) return false;
            if(visible.fraction==1) aim=ground;
        }
    }
    if(!bot_ai_random(b,&random,e)) return false;
    aim.x+=20*(random*2-1)*(1-accuracy);
    if(!bot_ai_random(b,&random,e)) return false;
    aim.y+=20*(random*2-1)*(1-accuracy);
    if(!bot_ai_random(b,&random,e)) return false;
    aim.z+=10*(random*2-1)*(1-accuracy);
    if(!trace(b,s,s->player.eye,aim,NULL,s->view.actor,BOT_SHOT,&hit,e)) return false;
    bot_ai_aim_target_set(s,hit.end);
    qa_vec3 direction=qa_vec_sub(aim,selected.muzzle_count?muzzle:s->player.eye);
    if(!tactics.melee && selected.weapon.speed==0) accuracy=accuracy*.6f+fminf(distance,150)/150*.4f;
    if(accuracy<.8f) {
        direction=qa_vec_normalize(direction);
        if(!bot_ai_random(b,&random,e)) return false;
        direction.x+=.3f*(random*2-1)*(1-accuracy);
        if(!bot_ai_random(b,&random,e)) return false;
        direction.y+=.3f*(random*2-1)*(1-accuracy);
        if(!bot_ai_random(b,&random,e)) return false;
        direction.z+=.3f*(random*2-1)*(1-accuracy);
    }
    qa_vec3 ideal=bot_ai_angles(direction);
    if(!bot_ai_random(b,&random,e)) return false;
    ideal.x=qa_builtin_angle_mod(ideal.x+6*selected.weapon.vertical_spread*(random*2-1)*(1-accuracy));
    if(!bot_ai_random(b,&random,e)) return false;
    ideal.y=qa_builtin_angle_mod(ideal.y+6*selected.weapon.horizontal_spread*(random*2-1)*(1-accuracy));
    bot_ai_view_ideal_set(s,ideal);
    if(b->controls.challenge && accuracy>.9f && bot_ai_enemy_sight_time(s)<b->time-1) {
        bot_ai_view_prepare(s);
        bot_ai_view_angles_set(s,bot_ai_view_ideal(s));
        if(!qa_bot_actions_view(qa_bot_runtime_actions(b->runtime),s->view.client,bot_ai_view_angles(s),e)) return false;
    }
    if(bot_ai_enemy_sight_time(s)>b->time-reaction || bot_ai_teleport_time(s)>b->time-reaction ||
       bot_ai_weapon_change_time(s)>b->time-.1f || bot_ai_fire_wait_time(s)>b->time) return true;
    if(bot_ai_fire_shoot_time(s)<b->time) {
        if(!bot_ai_random(b,&random,e)) return false;
        if(random>throttle) {bot_ai_fire_wait_time_set(s,b->time+throttle);bot_ai_fire_shoot_time_set(s,0);}
        else {bot_ai_fire_shoot_time_set(s,b->time+1-throttle);bot_ai_fire_wait_time_set(s,0);}
    }
    qa_vec3 to_enemy=qa_vec_sub(target.origin,s->player.origin);
    if(tactics.ranged_limit && qa_vec_dot(to_enemy,to_enemy)>tactics.maximum_range*tactics.maximum_range) return true;
    if(!in_view(bot_ai_view_angles(s),qa_vec_sub(bot_ai_aim_target(s),s->player.eye),distance<100?120:50)) return true;
    if(!trace(b,s,s->player.eye,bot_ai_aim_target(s),NULL,s->view.actor,BOT_SOLID|BOT_PLAYERCLIP,&hit,e)) return false;
    if(hit.fraction<1 && !qa_actor_id_equal(hit.actor,enemy)) return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(bot_ai_view_angles(s),&forward,NULL,NULL);
    muzzle=weapon_muzzle(&selected,&s->player,bot_ai_view_angles(s));
    qa_vec3 end=qa_vec_add(muzzle,qa_vec_scale(forward,1000));
    muzzle=qa_vec_add(muzzle,qa_vec_scale(forward,-12));
    shot_bounds=(qa_bounds){qa_v3(-8,-8,-8),qa_v3(8,8,8)};
    if(!trace(b,s,muzzle,end,&shot_bounds,s->view.actor,BOT_SHOT,&hit,e)) return false;
    if(hit.actor.registry && !qa_actor_id_equal(hit.actor,enemy)) {
        qa_builtin_player_info info;
        if(b->services.shared.player_info(b->services.shared.context,hit.actor,&info) && info.connected) {
            bool same;if(!bot_ai_same_team(b,s,hit.actor,&same,e)) return false;
            if(same) return true;
        }
        if((selected.projectile.damage_type&BOT_RADIAL) && hit.fraction*1000<selected.projectile.radius &&
            (selected.selected_projectile_damage-.5f*hit.fraction*1000)*.5f>0) return true;
    } else if(!hit.actor.registry && hit.fraction<1 && (selected.projectile.damage_type&BOT_RADIAL) &&
              hit.fraction*1000<selected.projectile.radius &&
              (selected.selected_projectile_damage-.5f*hit.fraction*1000)*.5f>0) return true;
    bool fire=!(selected.weapon.flags&BOT_FIRE_RELEASED)||bot_ai_flag(s,BOT_AI_ATTACKED);
    if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,enemy)) return true;
    if(fire && !qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_ATTACK,e)) return false;
    bot_ai_flag_toggle(s,BOT_AI_ATTACKED);return true;
}
