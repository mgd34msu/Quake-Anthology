#include "internal.h"
#include "source_event_state.h"
#include "source_inventory.h"
#include "source_player.h"
#include "source_view.h"
#include "source_combat_vectors.h"
#include "source_timers.h"
#include "source_flags.h"
#include "source_storage.h"
#include "source_selectors.h"
#include "source_team_state.h"
#include "source_goal.h"
#include "source_orders.h"
#include "qa/bot_movement_source.h"

enum { BOT_SOLID=1, BOT_LIQUID=8|16|32, BOT_FOG=64, BOT_PLAYERCLIP=0x10000,
       BOT_SHOT=1|0x2000000|0x4000000, BOT_FIRE_RELEASED=1, BOT_RADIAL=2 };

static qa_vec3 weapon_muzzle(const qa_bot_weapon_knowledge *weapon,
                             const bot_ai_state *state,qa_vec3 angles) {
    if(weapon->muzzle_count) return qa_vec_add(bot_ai_origin(state),weapon->muzzle_offsets[0]);
    qa_vec3 forward,right;
    qa_builtin_angle_vectors(angles,&forward,&right,NULL);
    qa_vec3 muzzle=qa_vec_add(bot_ai_eye(state),qa_vec_add(qa_vec_scale(forward,weapon->weapon.offset.x),
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
    if (!nav || !qa_bot_navigation_contents(nav, bot_ai_eye(s), &eye_contents, e)) return false;
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
        qa_vec3 start = bot_ai_eye(s), finish = end;
        qa_actor_id pass = s->view.actor;
        if (eye_contents & BOT_LIQUID) {
            if (!(contents & BOT_LIQUID)) { start = end; finish = bot_ai_eye(s); pass = actor; }
            mask ^= BOT_LIQUID;
        }
        qa_trace_result hit;
        if (!trace(b, s, start, finish, NULL, pass, mask, &hit, e)) return false;
        if (s->retired || !bot_ai_live(b, s->view.actor) || !bot_ai_live(b, actor)) return true;
        if (hit.fraction < 1 && !qa_actor_id_equal(hit.actor, actor)) continue;
        float seen = 1;
        if ((eye_contents | contents) & BOT_FOG) {
            qa_vec3 fog_start = bot_ai_eye(s), fog_end = end;
            if (!(eye_contents & BOT_FOG)) {
                if (!trace(b, s, end, bot_ai_eye(s), NULL, actor, BOT_FOG, &hit, e)) return false;
                fog_start = hit.end;
            } else if (!(contents & BOT_FOG)) {
                if (!trace(b, s, bot_ai_eye(s), end, NULL, s->view.actor, BOT_FOG, &hit, e)) return false;
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
    int32_t choice = bot_ai_weapon_number(s);
    bool ok = s->retired || !bot_ai_live(b, s->view.actor) ||
        qa_bot_knowledge_choose(b->runtime, s->weapons, weapons, count,
            bot_ai_inventory(s), b->inventory_scratch, &choice, e);
    b->services.arsenal_end(b->services.context, lease);
    if (ok && !s->retired && bot_ai_live(b, s->view.actor)) {
        if (bot_ai_weapon_number(s) != choice) bot_ai_weapon_change_time_set(s,b->time);
        bot_ai_weapon_number_set(s,choice);
    }
    return ok;
}
static bool aggression(qa_bots *b,bot_ai_state *s,float *out,qa_error *e) {
    const qa_bot_weapon_knowledge *weapons; size_t count; void *lease;
    if (!arsenal(b, s, &weapons, &count, &lease, e)) return false;
    *out=qa_bot_knowledge_aggression(weapons,count,bot_ai_weapon_number(s),bot_ai_inventory(s));
    b->services.arsenal_end(b->services.context, lease);
    return true;
}
static int32_t source_inventory_integer(float value) {
    return value >= -2147483648.0f && value < 2147483648.0f ? (int32_t)value : INT32_MIN;
}
bool bot_ai_battle_inventory(qa_bots *b,bot_ai_state *s,int32_t enemy,qa_error *e) {
    qa_bot_entity_info info;bool found;
    if(!qa_bot_runtime_entity(b->runtime,enemy,&info,&found,e)) return false;
    qa_vec3 direction=qa_vec_sub(info.state.origin,bot_ai_origin(s));
    bot_source_inventory inventory={b,s};
    if(!bot_ai_source_inventory_write(&inventory,QA_BOT_INV_ENEMY_HEIGHT,
        source_inventory_integer(direction.z),e)) return false;
    direction.z=0;
    return bot_ai_source_inventory_write(&inventory,QA_BOT_INV_ENEMY_DISTANCE,
        source_inventory_integer(qa_vec_length(direction)),e);
}
static bool enemy_carries_flag(const qa_bots *b,const qa_bot_entity_info *info) {
    uint32_t flags=(1u<<7)|(1u<<8)|(b->services.team_arena?(1u<<9):0);
    return ((uint32_t)info->state.powerups&flags)!=0;
}
static bool carrying_source_objective(const qa_bots *b,const bot_ai_state *s) {
    int32_t type=b->source_goals.game_type;
    if(type==4) return bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 ||
        bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0;
    if(s->team_arena && type==5) return bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0;
    if(s->team_arena && type==7) return bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 ||
        bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0;
    return false;
}
bool bot_ai_feeling_bad(qa_bots *b,bot_ai_state *s,float *out,qa_error *e) {
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    qa_bot_weapon_tactics tactics=qa_bot_weapon_tactics_for(NULL);
    if(!s->retired && bot_ai_live(b,s->view.actor)) {
        for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==bot_ai_weapon_number(s)) {
            tactics=qa_bot_weapon_tactics_for(weapons+i);break;
        }
    }
    b->services.arsenal_end(b->services.context,lease);
    *out=tactics.melee || bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<40?100:
        tactics.weakness>0?tactics.weakness:bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<60?80:0;
    return true;
}
bool bot_ai_retreat(qa_bots *b,bot_ai_state *s,bool *retreat,qa_error *e) {
    *retreat=false;
    if(carrying_source_objective(b,s)) {*retreat=true;return true;}
    if(s->team_arena && b->source_goals.game_type==6) {
        if(bot_ai_long_term_goal(s)==BOT_LTG_ATTACK_BASE &&
           (bot_ai_enemy_number(s)!=b->source_goals.red_obelisk.entity ||
            bot_ai_enemy_number(s)!=b->source_goals.blue_obelisk.entity)) {
            *retreat=true;return true;
        }
        float feeling;
        if(!bot_ai_feeling_bad(b,s,&feeling,e)) return false;
        *retreat=feeling>50;return true;
    }
    int32_t enemy=bot_ai_enemy_number(s);
    if(enemy>=0) {
        qa_bot_entity_info info;bool found;
        if(!qa_bot_runtime_entity(b->runtime,enemy,&info,&found,e)) return false;
        if(enemy_carries_flag(b,&info)) return true;
    }
    if(bot_ai_long_term_goal(s)==BOT_LTG_GET_FLAG) {*retreat=true;return true;}
    float level;
    if(!aggression(b,s,&level,e)) return false;
    *retreat=level<50;return true;
}
bool bot_ai_chase(qa_bots *b,bot_ai_state *s,bool *chase,qa_error *e) {
    *chase=false;
    if(carrying_source_objective(b,s)) return true;
    int32_t type=b->source_goals.game_type;
    if(type==4 || (s->team_arena && type==5)) {
        qa_bot_entity_info info;bool found;
        if(!qa_bot_runtime_entity(b->runtime,bot_ai_enemy_number(s),&info,&found,e)) return false;
        if(enemy_carries_flag(b,&info)) {*chase=true;return true;}
    }
    if(s->team_arena && type==6 && bot_ai_long_term_goal(s)==BOT_LTG_ATTACK_BASE &&
       (bot_ai_enemy_number(s)!=b->source_goals.red_obelisk.entity ||
        bot_ai_enemy_number(s)!=b->source_goals.blue_obelisk.entity)) return true;
    if(bot_ai_long_term_goal(s)==BOT_LTG_GET_FLAG) return true;
    float level;
    if(!aggression(b,s,&level,e)) return false;
    *chase=level>50;return true;
}
bool bot_ai_source_enemy_dead(qa_bots *b,bot_ai_state *s,const qa_bot_entity_info *info,
                              bool *dead,qa_error *e) {
    *dead=false;
    if(info->number<0 || info->number>=64) return true;
    if(!b->services.source_player_state)
        return bot_ai_fail(e,"Source enemy death requires its actual fixed player state");
    qa_bot_source_player_state player;
    if(!b->services.source_player_state(b->services.context,info->number,&player,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    *dead=player.has_player && player.pm_type!=0;return true;
}
static bool source_enemy_same_team(qa_bots *b,bot_ai_state *s,int32_t client,bool *same,qa_error *e) {
    int32_t self=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT);
    *same=false;
    if(self<0 || self>=64 || client<0 || client>=64 || b->source_goals.game_type<3) return true;
    int32_t own,other;
    if(!bot_ai_source_team(b,self,&own,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(!bot_ai_source_team(b,client,&other,e)) return false;
    *same=own==other;return true;
}
static void enemy_select(bot_ai_state *s,int32_t number,qa_actor_id actor,float time,int32_t current) {
    bot_ai_enemy_number_set(s,number);bot_ai_enemy_suicide_set(s,false);
    s->view.enemy=actor;bot_ai_enemy_sight_time_set(s,current>=0?time-2:time);
    bot_ai_enemy_visible_time_set(s,time);bot_ai_enemy_death_time_set(s,0);
}
bool bot_ai_find_enemy(qa_bots *b,bot_ai_state *s,int32_t current_enemy,bool *found,qa_error *e) {
    *found = false;
    float alertness, easy;
    if (!bot_ai_character_float(b, s, BOT_C_ALERTNESS, 0, 1, &alertness, e) ||
        !bot_ai_character_float(b, s, BOT_C_EASY_FRAGGER, 0, 1, &easy, e)) return false;
    int32_t last_health;
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_HEALTH,&last_health,false,e)) return false;
    bool hurt = last_health > bot_ai_inventory_value(s,QA_BOT_INV_HEALTH);
    last_health=bot_ai_inventory_value(s,QA_BOT_INV_HEALTH);
    if(!bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_HEALTH,&last_health,true,e)) return false;
    float best=0;
    if(current_enemy>=0) {
        qa_bot_entity_info info;bool observed;
        if(!qa_bot_runtime_entity(b->runtime,current_enemy,&info,&observed,e)) return false;
        if(enemy_carries_flag(b,&info)) return true;
        qa_vec3 direction=qa_vec_sub(info.state.origin,bot_ai_origin(s));
        best=qa_vec_dot(direction,direction);
    }
    if(s->team_arena && b->source_goals.game_type==6) {
        int32_t self=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT),team;
        if(!bot_ai_source_team(b,self,&team,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        qa_bot_goal goal=team==1?b->source_goals.blue_obelisk:b->source_goals.red_obelisk;
        qa_vec3 target=goal.origin;target.z+=1;
        qa_actor_id pass=b->services.entity_actor(b->services.context,self);
        qa_trace_result hit;
        if(!trace(b,s,bot_ai_eye(s),target,NULL,pass,BOT_SOLID,&hit,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        qa_actor_id actor=b->services.entity_actor(b->services.context,goal.entity);
        if(hit.fraction>=1 || (actor.registry && qa_actor_id_equal(hit.actor,actor))) {
            if(goal.entity==bot_ai_enemy_number(s)) return true;
            enemy_select(s,goal.entity,actor,b->time,-1);*found=true;return true;
        }
    }
    for(int32_t client=0;client<b->source_goals.max_clients && client<64;++client) {
        int32_t self=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT);
        if(client==self || client==current_enemy) continue;
        qa_bot_entity_info info;bool observed;
        if(!qa_bot_runtime_entity(b->runtime,client,&info,&observed,e)) return false;
        if(!info.valid) continue;
        bool dead;
        if(!bot_ai_source_enemy_dead(b,s,&info,&dead,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        int32_t entity=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENTITY);
        if(dead || info.number==entity) continue;
        bool carrying=enemy_carries_flag(b,&info),firing=(info.state.flags&0x100)!=0;
        if(!carrying && ((uint32_t)info.state.powerups&(1u<<BOT_SOURCE_PW_INVIS)) && !firing) continue;
        if(easy<.5f && (info.state.flags&0x1000)) continue;
        qa_vec3 direction=qa_vec_sub(info.state.origin,b->source_event_globals.last_teleport_origin);
        volatile float recent=b->time-3;
        if(b->source_event_globals.last_teleport_time>recent && qa_vec_dot(direction,direction)<70*70) continue;
        direction=qa_vec_sub(info.state.origin,bot_ai_origin(s));
        float distance=qa_vec_dot(direction,direction);
        if(!carrying && current_enemy>=0 && distance>best) continue;
        volatile float alert_distance=alertness*4000,limit=900+alert_distance,squared_limit=limit*limit;
        if(distance>squared_limit) continue;
        bool same;
        if(!source_enemy_same_team(b,s,client,&same,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(same) continue;
        volatile float scaled=fminf(distance,810*810)/(810*9),remaining=90-scaled;
        float fov=current_enemy<0 && (hurt || firing)?360:180-remaining;
        float visible;
        if(!bot_ai_source_entity_visible(b,s,client,fov,&visible,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        qa_actor_id actor=b->services.entity_actor(b->services.context,info.number);
        if(visible<=0 || !bot_ai_live(b,actor)) continue;
        if(current_enemy<0 && distance>100*100 && !hurt && !firing &&
           !qa_bot_field_of_vision(info.state.angles,90,
               bot_ai_angles(qa_vec_sub(bot_ai_origin(s),info.state.origin)))) {
            if(!bot_ai_battle_inventory(b,s,client,e)) return false;
            bool retreat;
            if(!bot_ai_retreat(b,s,&retreat,e)) return false;
            if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
            if(retreat) continue;
        }
        if(!bot_ai_live(b,actor)) continue;
        enemy_select(s,info.number,actor,b->time,current_enemy);*found=true;return true;
    }
    /* Foreign damageable actors keep their canonical providers. Actual Source
     * client aliases were considered only by the physical client loop above. */
    qa_actor_id source_actors[64];
    for(int32_t client=0;client<64;++client) source_actors[client]=bot_ai_source_actor(b,client);
    qa_actor_id current_actor=current_enemy>=0?
        b->services.entity_actor(b->services.context,current_enemy):(qa_actor_id){0};
    for (size_t i = 0; i < b->entities.count; ++i) {
        qa_actor_id actor = b->entities.ids[i];
        if(!bot_ai_live(b,actor) || qa_actor_id_equal(actor,s->view.actor) ||
           qa_actor_id_equal(actor,current_actor)) continue;
        bool source_client=false;
        for(size_t client=0;client<64;++client) {
            if(qa_actor_id_equal(source_actors[client],actor)) {source_client=true;break;}
        }
        if(source_client) continue;
        qa_bot_player player;bool present;
        if (!bot_ai_target(b,s,actor,&player,&present,e)) return false;
        if (s->retired || !bot_ai_live(b, s->view.actor)) return true;
        if (!present || !bot_ai_live(b, actor) || !player.connected || player.dead || player.observer ||
            (player.invisible && !player.firing) || (easy < .5f && player.chatting)) continue;
        bool same;
        if (!bot_ai_same_team(b, s, actor, &same, e)) return false;
        if (same) continue;
        qa_vec3 d = qa_vec_sub(player.origin, bot_ai_origin(s));
        float distance = qa_vec_dot(d,d), limit = 900+alertness*4000;
        qa_vec3 teleport_delta=qa_vec_sub(player.origin,b->source_event_globals.last_teleport_origin);
        volatile float recent_teleport=b->time-3;
        if ((current_enemy>=0 && distance > best) || distance > limit*limit ||
            (b->source_event_globals.last_teleport_time>recent_teleport &&
             qa_vec_dot(teleport_delta,teleport_delta)<70*70)) continue;
        float fov=current_enemy<0 && (hurt || player.firing)?360:
            180-(90-fminf(distance,810*810)/(810*9));
        if (!in_view(bot_ai_view_angles(s),d,fov)) continue;
        float visible;
        if (!bot_ai_enemy_visible(b,s,actor,&visible,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(visible<=0 || !bot_ai_live(b,actor)) continue;
        qa_bot_entity source_enemy;
        if(!b->services.entity(b->services.context,actor,&source_enemy,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(!source_enemy.present || !bot_ai_live(b,actor) || source_enemy.number==current_enemy) continue;
        if (current_enemy<0 && distance > 100*100 && !hurt && !player.firing &&
            !in_view(player.view_angles,qa_vec_scale(d,-1),90)) {
            if(!qa_actor_id_equal(b->services.entity_actor(b->services.context,source_enemy.number),actor)) continue;
            if(!bot_ai_battle_inventory(b,s,source_enemy.number,e)) return false;
            bool retreat;
            if (!bot_ai_retreat(b,s,&retreat,e)) return false;
            if (retreat) continue;
        }
        if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,actor)) return true;
        if(!qa_actor_id_equal(b->services.entity_actor(b->services.context,source_enemy.number),actor)) continue;
        enemy_select(s,source_enemy.number,actor,b->time,current_enemy);
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
bool bot_ai_attack_move(qa_bots *b, bot_ai_state *s, uint32_t travel_flags,
                        qa_bot_move_result *result, qa_error *e) {
    bool source=bot_ai_source_enemy(b,s);
    int32_t attack_entity=bot_ai_enemy_number(s);
    memset(result,0,sizeof(*result));
    if(source && bot_ai_attack_chase_time(s)>b->time) {
        qa_bot_goal goal={.entity=attack_entity,.area=(int32_t)bot_ai_last_enemy_area(s),
            .origin=bot_ai_last_enemy_origin(s),.mins=qa_v3(-8,-8,-8),.maxs=qa_v3(8,8,8)};
        if(!bot_ai_move_setup(b,s,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        return qa_bot_moves_goal(qa_bot_runtime_moves(b->runtime),s->movement,&goal,
            travel_flags,result,e);
    }
    float skill,jumper,croucher;
    if (!bot_ai_character_float(b,s,BOT_C_ATTACK,0,1,&skill,e) ||
        !bot_ai_character_float(b,s,BOT_C_JUMPER,0,1,&jumper,e) ||
        !bot_ai_character_float(b,s,BOT_C_CROUCHER,0,1,&croucher,e)) return false;
    if (skill < .2f) return true;
    if (!bot_ai_move_setup(b,s,e)) return false;
    qa_bot_entity_info info;bool observed;
    if(!qa_bot_runtime_entity(b->runtime,source?attack_entity:bot_ai_enemy_number(s),&info,&observed,e)) return false;
    qa_vec3 toward=qa_vec_sub(info.state.origin,bot_ai_origin(s));
    float distance=qa_vec_length(toward);
    qa_vec3 forward=qa_vec_normalize(toward),backward=qa_vec_scale(forward,-1);
    uint32_t type=QA_BOT_DIRECTION_WALK;
    if (bot_ai_attack_crouch_time(s) < b->time-1) {
        float random;if(!bot_ai_random(b,&random,e)) return false;
        if (random<jumper) type=QA_BOT_DIRECTION_JUMP;
        else if(!source || bot_ai_attack_crouch_time(s)<b->time-1) {
            if(!bot_ai_random(b,&random,e)) return false;
            if(random<croucher) bot_ai_attack_crouch_time_set(s,b->time+croucher*5);
        }
    }
    if (bot_ai_attack_crouch_time(s)>b->time) type=QA_BOT_DIRECTION_CROUCH;
    if (type==QA_BOT_DIRECTION_JUMP) {
        if (bot_ai_attack_jump_time(s)>b->time) type=QA_BOT_DIRECTION_WALK;
        else bot_ai_attack_jump_time_set(s,b->time+1);
    }
    int32_t held=bot_ai_weapon_number(s);
    if(source && !bot_ai_source_player_word(b,s,BOT_PS_WEAPON,&held,e)) return false;
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if (!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    bool melee=false;
    for(size_t i=0;i<count;++i)
        if(weapons[i].weapon.number==(source?held:bot_ai_weapon_number(s))) melee=weapons[i].melee;
    b->services.arsenal_end(b->services.context,lease);
    float desired=melee?0:140,range=melee?0:40;
    qa_bot_moves *moves=qa_bot_runtime_moves(b->runtime);bool moved;
    if(skill<=.4f) {
        if(distance>desired+range) return qa_bot_moves_direction(moves,s->movement,forward,400,type,&moved,e);
        if(distance<desired-range) return qa_bot_moves_direction(moves,s->movement,backward,400,type,&moved,e);
        return true;
    }
    bot_ai_attack_strafe_time_set(s,bot_ai_attack_strafe_time(s)+bot_ai_think_time(s));
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
static bool canonical_attack(qa_bots *b, bot_ai_state *s, bool moving, qa_error *e) {
    (void)moving;
    qa_actor_id enemy=bot_ai_enemy_actor(b,s);
    s->view.enemy=enemy;
    if(!bot_ai_live(b,enemy)) return true;
    qa_bot_player target;bool present;
    if(!bot_ai_target(b,s,enemy,&target,&present,e)) return false;
    if(!present || s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,enemy) || target.dead) return true;
    bot_ai_enemy_origin_set(s,target.origin);bot_ai_enemy_velocity_set(s,target.velocity);
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    qa_bot_weapon_knowledge selected={0};bool exists=false;
    for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==bot_ai_weapon_number(s)) {selected=weapons[i];exists=true;break;}
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
    float distance=qa_vec_length(qa_vec_sub(aim,bot_ai_eye(s)));
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
    qa_vec3 muzzle=selected.muzzle_count?weapon_muzzle(&selected,s,bot_ai_view_angles(s)):
        qa_vec_add(bot_ai_eye(s),qa_v3(0,0,selected.weapon.offset.z));
    if(!trace(b,s,muzzle,aim,&shot_bounds,s->view.actor,BOT_SHOT,&hit,e)) return false;
    if(hit.fraction<1 && !qa_actor_id_equal(hit.actor,enemy)) aim.z+=16;
    if(skill>.6f && (selected.projectile.damage_type&BOT_RADIAL) && target.origin.z<bot_ai_origin(s).z+16) {
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
    if(!trace(b,s,bot_ai_eye(s),aim,NULL,s->view.actor,BOT_SHOT,&hit,e)) return false;
    bot_ai_aim_target_set(s,hit.end);
    qa_vec3 direction=qa_vec_sub(aim,selected.muzzle_count?muzzle:bot_ai_eye(s));
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
    qa_vec3 to_enemy=qa_vec_sub(target.origin,bot_ai_origin(s));
    if(tactics.ranged_limit && qa_vec_dot(to_enemy,to_enemy)>tactics.maximum_range*tactics.maximum_range) return true;
    if(!in_view(bot_ai_view_angles(s),qa_vec_sub(bot_ai_aim_target(s),bot_ai_eye(s)),distance<100?120:50)) return true;
    if(!trace(b,s,bot_ai_eye(s),bot_ai_aim_target(s),NULL,s->view.actor,BOT_SOLID|BOT_PLAYERCLIP,&hit,e)) return false;
    if(hit.fraction<1 && !qa_actor_id_equal(hit.actor,enemy)) return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(bot_ai_view_angles(s),&forward,NULL,NULL);
    muzzle=weapon_muzzle(&selected,s,bot_ai_view_angles(s));
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
static bool source_attack_live(qa_bots *b,bot_ai_state *s) {
    return !s->retired && bot_ai_live(b,s->view.actor);
}
#define SOURCE_ATTACK_CALL(call) do {if(!(call)) return false;if(!source_attack_live(b,s)) return true;} while(0)
static bool source_use(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(!b->services.source_action_client)
        return bot_ai_fail(e,"Source holdable use requires its actual action-client namespace");
    int32_t source=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT);
    uint32_t client;
    if(!b->services.source_action_client(b->services.context,source,&client,e)) return false;
    if(!source_attack_live(b,s)) return true;
    return qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),client,QA_BOT_USE,e);
}
static bool source_carrier_near(qa_bots *b,bot_ai_state *s,int32_t carrier,bool *near,qa_error *e) {
    *near=false;if(carrier<0) return true;
    qa_bot_entity_info info;bool observed;
    SOURCE_ATTACK_CALL(qa_bot_runtime_entity(b->runtime,carrier,&info,&observed,e));
    qa_vec3 direction=qa_vec_sub(info.state.origin,bot_ai_origin(s));
    *near=qa_vec_dot(direction,direction)<1024*1024;return true;
}
static bool source_goal_visible_near(qa_bots *b,bot_ai_state *s,const qa_bot_goal *goal,
                                      float distance,bool *visible,qa_error *e) {
    *visible=false;
    qa_vec3 target=qa_vec_add(goal->origin,qa_v3(0,0,1));
    qa_vec3 direction=qa_vec_sub(bot_ai_origin(s),target);
    if(!(qa_vec_dot(direction,direction)<distance*distance)) return true;
    int32_t client=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT);
    qa_actor_id pass=b->services.entity_actor(b->services.context,client);
    qa_trace_result hit;
    SOURCE_ATTACK_CALL(trace(b,s,bot_ai_eye(s),target,NULL,pass,BOT_SOLID,&hit,e));
    qa_actor_id expected=b->services.entity_actor(b->services.context,goal->entity);
    *visible=hit.fraction>=1 || (expected.registry && qa_actor_id_equal(hit.actor,expected));
    return true;
}
static bool source_visible_carriers(qa_bots *b,bot_ai_state *s,int32_t *team,int32_t *enemy,qa_error *e) {
    *team=0;*enemy=0;
    for(int32_t client=0;client<b->source_goals.max_clients && client<64;++client) {
        if(client==bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT)) continue;
        qa_bot_entity_info info;bool observed;
        SOURCE_ATTACK_CALL(qa_bot_runtime_entity(b->runtime,client,&info,&observed,e));
        if(!info.valid || !enemy_carries_flag(b,&info)) continue;
        qa_vec3 direction=qa_vec_sub(info.state.origin,bot_ai_origin(s));
        if(qa_vec_dot(direction,direction)>1024*1024) continue;
        float visibility;
        SOURCE_ATTACK_CALL(bot_ai_source_entity_visible(b,s,client,360,&visibility,e));
        if(visibility<=0) continue;
        bool same;
        SOURCE_ATTACK_CALL(bot_ai_source_same_team(b,s,client,&same,e));
        if(same) ++*team;else ++*enemy;
    }
    return true;
}
static bool source_kamikaze(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(bot_ai_inventory_value(s,QA_BOT_INV_KAMIKAZE)<=0 || bot_ai_kamikaze_time(s)>b->time) return true;
    bot_ai_kamikaze_time_set(s,b->time+.2f);
    int32_t type=b->source_goals.game_type;
    if(type==4 || type==5 || type==7) {
        if(carrying_source_objective(b,s)) return true;
        bool cubes=type==7,near;int32_t carrier;
        SOURCE_ATTACK_CALL(bot_ai_source_flag_carrier(b,s,true,true,cubes,&carrier,e));
        SOURCE_ATTACK_CALL(source_carrier_near(b,s,carrier,&near,e));
        if(near) return true;
        SOURCE_ATTACK_CALL(bot_ai_source_flag_carrier(b,s,false,true,cubes,&carrier,e));
        SOURCE_ATTACK_CALL(source_carrier_near(b,s,carrier,&near,e));
        if(near) return source_use(b,s,e);
    } else if(type==6) {
        int32_t client=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT),team;
        SOURCE_ATTACK_CALL(bot_ai_source_team(b,client,&team,e));
        const qa_bot_goal *goal=team==1?&b->source_goals.blue_obelisk:&b->source_goals.red_obelisk;
        bool visible;
        SOURCE_ATTACK_CALL(source_goal_visible_near(b,s,goal,1024*.9f,&visible,e));
        if(visible) return source_use(b,s,e);
    }
    int32_t team,enemy;
    SOURCE_ATTACK_CALL(source_visible_carriers(b,s,&team,&enemy,e));
    return !(enemy>2 && enemy>team+1) || source_use(b,s,e);
}
static bool source_invulnerability(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(bot_ai_inventory_value(s,QA_BOT_INV_INVULNERABILITY)<=0 || bot_ai_invulnerability_time(s)>b->time) return true;
    bot_ai_invulnerability_time_set(s,b->time+.2f);
    int32_t type=b->source_goals.game_type;
    const qa_bot_goal *goal;
    if(type==4 || type==5 || type==7) {
        if(carrying_source_objective(b,s)) return true;
        int32_t carrier;
        SOURCE_ATTACK_CALL(bot_ai_source_flag_carrier(b,s,false,true,type==7,&carrier,e));
        if(carrier>=0) return true;
    }
    if(type!=4 && type!=5 && type!=6 && type!=7) return true;
    int32_t client=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT),team;
    SOURCE_ATTACK_CALL(bot_ai_source_team(b,client,&team,e));
    if(type==4 || type==5) goal=team==1?&b->source_goals.blue_flag:&b->source_goals.red_flag;
    else goal=team==1?&b->source_goals.blue_obelisk:&b->source_goals.red_obelisk;
    bool visible;
    SOURCE_ATTACK_CALL(source_goal_visible_near(b,s,goal,type==6?300:200,&visible,e));
    return !visible || source_use(b,s,e);
}
bool bot_ai_source_battle_items(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<40 &&
       bot_ai_inventory_value(s,QA_BOT_INV_TELEPORTER)>0 && !carrying_source_objective(b,s))
        SOURCE_ATTACK_CALL(source_use(b,s,e));
    if(bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<60 && bot_ai_inventory_value(s,QA_BOT_INV_MEDKIT)>0)
        SOURCE_ATTACK_CALL(source_use(b,s,e));
    if(s->team_arena) {
        SOURCE_ATTACK_CALL(source_kamikaze(b,s,e));
        SOURCE_ATTACK_CALL(source_invulnerability(b,s,e));
    }
    return true;
}
static bool source_weapon(qa_bots *b,bot_ai_state *s,qa_bot_weapon_knowledge *out,bool *found,qa_error *e) {
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    *found=false;
    if(!arsenal(b,s,&weapons,&count,&lease,e)) return false;
    if(source_attack_live(b,s)) for(size_t i=0;i<count;++i)
        if(weapons[i].weapon.number==bot_ai_weapon_number(s)) {*out=weapons[i];*found=true;break;}
    b->services.arsenal_end(b->services.context,lease);return true;
}
static bool source_trace_hits(qa_bots *b,const qa_trace_result *trace_result,int32_t number) {
    qa_actor_id expected=b->services.entity_actor(b->services.context,number);
    return expected.registry && qa_actor_id_equal(trace_result->actor,expected);
}
static qa_actor_id source_viewer(qa_bots *b,const bot_ai_state *s) {
    return b->services.entity_actor(b->services.context,
        bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENTITY));
}
bool bot_ai_source_aim(qa_bots *b,bot_ai_state *s,qa_error *e) {
    int32_t enemy=bot_ai_enemy_number(s);
    if(enemy<0) return true;
    qa_bot_entity_info info;bool observed;
    SOURCE_ATTACK_CALL(qa_bot_runtime_entity(b->runtime,enemy,&info,&observed,e));
    if(bot_ai_enemy_number(s)>=64) {
        qa_vec3 target=info.state.origin;
        if(s->team_arena && (bot_ai_enemy_number(s)==b->source_goals.red_obelisk.entity ||
                            bot_ai_enemy_number(s)==b->source_goals.blue_obelisk.entity)) target.z+=32;
        bot_ai_view_ideal_set(s,bot_ai_angles(qa_vec_sub(target,bot_ai_eye(s))));
        bot_ai_aim_target_set(s,target);return true;
    }
    float skill,accuracy;
    SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,BOT_C_AIM_SKILL,0,1,&skill,e));
    SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,BOT_C_ACCURACY,0,1,&accuracy,e));
    if(skill>.95f) {
        float reaction;
        SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,BOT_C_REACTION,0,1,&reaction,e));
        reaction*=.5f;
        if(bot_ai_enemy_sight_time(s)>b->time-reaction || bot_ai_teleport_time(s)>b->time-reaction) return true;
    }
    qa_bot_weapon_knowledge selected;bool exists;
    SOURCE_ATTACK_CALL(source_weapon(b,s,&selected,&exists,e));
    if(!exists) return true;
    qa_bot_weapon_tactics tactics=qa_bot_weapon_tactics_for(&selected);
    if(tactics.accuracy_characteristic>=0)
        SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,(uint32_t)tactics.accuracy_characteristic,0,1,&accuracy,e));
    if(tactics.skill_characteristic>=0)
        SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,(uint32_t)tactics.skill_characteristic,0,1,&skill,e));
    if(accuracy<=0) accuracy=.0001f;
    SOURCE_ATTACK_CALL(qa_bot_runtime_entity(b->runtime,bot_ai_enemy_number(s),&info,&observed,e));
    float random;
    if(!enemy_carries_flag(b,&info) && ((uint32_t)info.state.powerups&(1u<<BOT_SOURCE_PW_INVIS))) {
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));
        if(random>.1f) accuracy*=.4f;
    }
    qa_vec3 velocity=qa_vec_scale(qa_vec_sub(info.state.origin,info.last_visible_origin),1.0f/info.update_interval);
    if(bot_ai_enemy_position_time(s)<b->time) {
        bot_ai_enemy_position_time_set(s,b->time+.5f);
        bot_ai_enemy_velocity_set(s,velocity);bot_ai_enemy_origin_set(s,info.state.origin);
    }
    qa_vec3 movement=qa_vec_sub(info.state.origin,bot_ai_enemy_origin(s));
    if(skill<.9f && qa_vec_dot(movement,movement)>48*48 &&
       qa_vec_dot(bot_ai_enemy_velocity(s),velocity)<0) accuracy*=.7f;
    float visibility;
    SOURCE_ATTACK_CALL(bot_ai_source_entity_visible(b,s,bot_ai_enemy_number(s),360,&visibility,e));
    bool visible=source_inventory_integer(visibility)!=0;
    qa_vec3 best;
    if(visible) {
        best=info.state.origin;best.z+=8;
        int32_t view_height;
        SOURCE_ATTACK_CALL(bot_ai_source_player_word(b,s,BOT_PS_VIEW_HEIGHT,&view_height,e));
        qa_vec3 start=bot_ai_origin(s);start.z+=(float)view_height;start.z+=selected.weapon.offset.z;
        qa_bounds shot_bounds={qa_v3(-4,-4,-4),qa_v3(4,4,4)};qa_trace_result hit;
        SOURCE_ATTACK_CALL(trace(b,s,start,best,&shot_bounds,source_viewer(b,s),BOT_SHOT,&hit,e));
        if(hit.fraction<=1 && !source_trace_hits(b,&hit,info.number)) best.z+=16;
        if(selected.weapon.speed!=0) {
            float distance=qa_vec_length(qa_vec_sub(best,bot_ai_origin(s)));
            movement=qa_vec_sub(info.state.origin,bot_ai_enemy_origin(s));
            if(!(distance>100 && qa_vec_dot(movement,movement)<32*32)) {
                int32_t weapon_state=0;
                if(skill>.8f) SOURCE_ATTACK_CALL(bot_ai_source_player_word(b,s,BOT_PS_WEAPON_STATE,&weapon_state,e));
                if(skill>.8f && weapon_state==0) {
                    if(!b->services.predict_motion)
                        return bot_ai_fail(e,"Source aim requires the target's actual movement prediction");
                    distance=qa_vec_length(qa_vec_sub(info.state.origin,bot_ai_origin(s)));
                    qa_vec3 origin=info.state.origin;origin.z+=1;
                    qa_bot_movement_prediction_query query={.origin=origin,.velocity=velocity,.presence=4,
                        .maximum_frames=source_inventory_integer((distance*10)/selected.weapon.speed),.frame_time=.1f};
                    qa_bot_movement_prediction prediction;bool available;
                    qa_actor_id actor=bot_ai_enemy_actor(b,s);
                    SOURCE_ATTACK_CALL(b->services.predict_motion(b->services.context,actor,&query,&prediction,&available,e));
                    if(!available) return bot_ai_fail(e,"Source aim target has no retained movement prediction owner");
                    best=prediction.end;
                } else if(skill>.4f) {
                    distance=qa_vec_length(qa_vec_sub(info.state.origin,bot_ai_origin(s)));
                    qa_vec3 horizontal=qa_vec_sub(info.state.origin,info.last_visible_origin);horizontal.z=0;
                    float length=qa_vec_length(horizontal),speed=length/info.update_interval;
                    if(length!=0) horizontal=qa_vec_scale(horizontal,1.0f/length);
                    best=qa_vec_add(info.state.origin,qa_vec_scale(horizontal,(distance/selected.weapon.speed)*speed));
                }
            }
        }
        if(skill>.6f && (selected.projectile.damage_type&BOT_RADIAL) && info.state.origin.z<bot_ai_origin(s).z+16) {
            qa_vec3 end=info.state.origin;end.z-=64;
            SOURCE_ATTACK_CALL(trace(b,s,info.state.origin,end,NULL,
                b->services.entity_actor(b->services.context,info.number),BOT_SHOT,&hit,e));
            qa_vec3 ground=best;ground.z=hit.start_solid?info.state.origin.z-16:hit.end.z-8;
            SOURCE_ATTACK_CALL(trace(b,s,start,ground,NULL,source_viewer(b,s),BOT_SHOT,&hit,e));
            qa_vec3 delta=qa_vec_sub(hit.end,ground),self=qa_vec_sub(hit.end,start);
            if(fabsf(hit.end.z-ground.z)<50 && qa_vec_dot(delta,delta)<60*60 && qa_vec_dot(self,self)>100*100) {
                end=hit.end;end.z+=1;
                SOURCE_ATTACK_CALL(trace(b,s,end,info.state.origin,NULL,
                    b->services.entity_actor(b->services.context,info.number),BOT_SHOT,&hit,e));
                if(hit.fraction>=1) best=ground;
            }
        }
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));best.x+=(20*(random*2-1))*(1-accuracy);
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));best.y+=(20*(random*2-1))*(1-accuracy);
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));best.z+=(10*(random*2-1))*(1-accuracy);
    } else {
        best=bot_ai_last_enemy_origin(s);best.z+=8;
        if(skill>.5f && tactics.predict_occluded_splash) {
            qa_bot_goal goal={.entity=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT),
                .area=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_AREA),.origin=bot_ai_eye(s),
                .mins={-8,-8,-8},.maxs={8,8,8}};
            qa_vec3 target;bool found;
            SOURCE_ATTACK_CALL(qa_bot_moves_visible_position(qa_bot_runtime_moves(b->runtime),(int32_t)s->view.client,
                bot_ai_last_enemy_origin(s),bot_ai_last_enemy_area(s),&goal,0x011c0fbe,&target,&found,e));
            if(found) {
                qa_vec3 direction=qa_vec_sub(target,bot_ai_eye(s));
                if(qa_vec_dot(direction,direction)>80*80) {best=target;best.z-=20;}
            }
            accuracy=1;
        }
    }
    if(visible) {
        qa_trace_result hit;
        SOURCE_ATTACK_CALL(trace(b,s,bot_ai_eye(s),best,NULL,source_viewer(b,s),BOT_SHOT,&hit,e));
        bot_ai_aim_target_set(s,hit.end);
    } else bot_ai_aim_target_set(s,best);
    qa_vec3 direction=qa_vec_sub(best,bot_ai_eye(s));
    if(selected.weapon.speed==0 && !tactics.melee) {
        float distance=qa_vec_length(direction);if(distance>150) distance=150;
        accuracy*=.6f+(distance/150)*.4f;
    }
    if(accuracy<.8f) {
        float length=qa_vec_length(direction);
        if(length!=0) direction=qa_vec_scale(direction,1.0f/length);
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));direction.x+=(.3f*(random*2-1))*(1-accuracy);
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));direction.y+=(.3f*(random*2-1))*(1-accuracy);
        SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));direction.z+=(.3f*(random*2-1))*(1-accuracy);
    }
    bot_ai_view_ideal_set(s,bot_ai_angles(direction));
    SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));
    float pitch=bot_ai_view_ideal(s).x+((6*selected.weapon.vertical_spread)*(random*2-1))*(1-accuracy);
    bot_ai_view_ideal_axis_set(s,0,qa_builtin_angle_mod(pitch));
    SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));
    float yaw=bot_ai_view_ideal(s).y+((6*selected.weapon.horizontal_spread)*(random*2-1))*(1-accuracy);
    bot_ai_view_ideal_axis_set(s,1,qa_builtin_angle_mod(yaw));
    if(b->source_match.cvars[BOT_SOURCE_CHALLENGE].integer_value && accuracy>.9f &&
       bot_ai_enemy_sight_time(s)<b->time-1) {
        bot_ai_view_prepare(s);bot_ai_view_angles_set(s,bot_ai_view_ideal(s));
        SOURCE_ATTACK_CALL(qa_bot_actions_view(qa_bot_runtime_actions(b->runtime),s->view.client,bot_ai_view_angles(s),e));
    }
    return true;
}
bool bot_ai_source_check_attack(qa_bots *b,bot_ai_state *s,qa_error *e) {
    int32_t enemy=bot_ai_enemy_number(s);
    qa_bot_entity_info info;bool observed;
    SOURCE_ATTACK_CALL(qa_bot_runtime_entity(b->runtime,enemy,&info,&observed,e));
    if(enemy>=64 && s->team_arena && (info.number==b->source_goals.red_obelisk.entity ||
                                    info.number==b->source_goals.blue_obelisk.entity) &&
       b->services.source_activator_frame) {
        int32_t frame;bool present;
        SOURCE_ATTACK_CALL(b->services.source_activator_frame(b->services.context,info.number,&frame,&present,e));
        if(present && frame==2) return true;
    }
    float reaction;
    SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,BOT_C_REACTION,0,1,&reaction,e));
    if(bot_ai_enemy_sight_time(s)>b->time-reaction || bot_ai_teleport_time(s)>b->time-reaction ||
       bot_ai_weapon_change_time(s)>b->time-.1f || bot_ai_fire_wait_time(s)>b->time) return true;
    float throttle;
    SOURCE_ATTACK_CALL(bot_ai_character_float(b,s,BOT_C_FIRE_THROTTLE,0,1,&throttle,e));
    if(bot_ai_fire_shoot_time(s)<b->time) {
        float random;SOURCE_ATTACK_CALL(bot_ai_random(b,&random,e));
        if(random>throttle) {bot_ai_fire_wait_time_set(s,b->time+throttle);bot_ai_fire_shoot_time_set(s,0);}
        else {bot_ai_fire_shoot_time_set(s,(b->time+1)-throttle);bot_ai_fire_wait_time_set(s,0);}
    }
    qa_vec3 direction=qa_vec_sub(bot_ai_aim_target(s),bot_ai_eye(s));
    float distance=qa_vec_dot(direction,direction);
    qa_bot_weapon_knowledge selected;bool exists;
    SOURCE_ATTACK_CALL(source_weapon(b,s,&selected,&exists,e));
    if(!exists) return true;
    qa_bot_weapon_tactics tactics=qa_bot_weapon_tactics_for(&selected);
    if(tactics.ranged_limit && distance>tactics.maximum_range*tactics.maximum_range) return true;
    if(!qa_bot_field_of_vision(bot_ai_view_angles(s),distance<100*100?120:50,bot_ai_angles(direction))) return true;
    qa_trace_result hit;
    qa_actor_id client=b->services.entity_actor(b->services.context,
        bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CLIENT));
    SOURCE_ATTACK_CALL(trace(b,s,bot_ai_eye(s),bot_ai_aim_target(s),NULL,client,BOT_SOLID|BOT_PLAYERCLIP,&hit,e));
    if(hit.fraction<1 && !source_trace_hits(b,&hit,enemy)) return true;
    SOURCE_ATTACK_CALL(source_weapon(b,s,&selected,&exists,e));
    if(!exists) return true;
    int32_t view_height;
    SOURCE_ATTACK_CALL(bot_ai_source_player_word(b,s,BOT_PS_VIEW_HEIGHT,&view_height,e));
    qa_vec3 start=bot_ai_origin(s);start.z+=(float)view_height;
    qa_vec3 forward,right;qa_builtin_angle_vectors(bot_ai_view_angles(s),&forward,&right,NULL);
    start.x+=forward.x*selected.weapon.offset.x+right.x*selected.weapon.offset.y;
    start.y+=forward.y*selected.weapon.offset.x+right.y*selected.weapon.offset.y;
    start.z+=(forward.z*selected.weapon.offset.x+right.z*selected.weapon.offset.y)+selected.weapon.offset.z;
    qa_vec3 end=qa_vec_add(start,qa_vec_scale(forward,1000));start=qa_vec_add(start,qa_vec_scale(forward,-12));
    qa_bounds bounds={qa_v3(-8,-8,-8),qa_v3(8,8,8)};
    SOURCE_ATTACK_CALL(trace(b,s,start,end,&bounds,source_viewer(b,s),BOT_SHOT,&hit,e));
    if(!source_trace_hits(b,&hit,enemy)) for(int32_t number=1;number<=64;++number) {
        if(!source_trace_hits(b,&hit,number)) continue;
        bool same;SOURCE_ATTACK_CALL(bot_ai_source_same_team(b,s,number,&same,e));
        if(same) return true;
        break;
    }
    if((!source_trace_hits(b,&hit,enemy) || enemy>=64) && (selected.projectile.damage_type&BOT_RADIAL) &&
       hit.fraction*1000<selected.projectile.radius &&
       (selected.selected_projectile_damage-((.5f*hit.fraction)*1000))*.5f>0) return true;
    if(!(selected.weapon.flags&BOT_FIRE_RELEASED) || bot_ai_flag(s,BOT_AI_ATTACKED))
        SOURCE_ATTACK_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_ATTACK,e));
    bot_ai_flag_toggle(s,BOT_AI_ATTACKED);return true;
}
bool bot_ai_source_enemy(qa_bots *b,const bot_ai_state *s) {
    qa_actor_id enemy=bot_ai_enemy_actor(b,s);
    qa_actor_id source=bot_ai_source_actor(b,bot_ai_enemy_number(s));
    return source.registry && qa_actor_id_equal(source,enemy);
}
bool bot_ai_attack(qa_bots *b,bot_ai_state *s,bool moving,qa_error *e) {
    qa_actor_id enemy=bot_ai_enemy_actor(b,s);
    s->view.enemy=enemy;
    if(bot_ai_source_enemy(b,s)) {
        if(!source_attack_live(b,s) || !bot_ai_live(b,enemy)) return true;
        SOURCE_ATTACK_CALL(bot_ai_source_aim(b,s,e));
        return bot_ai_source_check_attack(b,s,e);
    }
    return canonical_attack(b,s,moving,e);
}
#undef SOURCE_ATTACK_CALL
