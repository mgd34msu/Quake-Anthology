#include "internal.h"
#include "source_event_state.h"
#include "source_selectors.h"
#include "source_timers.h"
#include "source_inventory.h"
#include "source_events.h"
#include "source_orders.h"
#include "source_player.h"
#include <stdio.h>

enum {
    SOURCE_EVENT_TELEPORT_IN=42, SOURCE_EVENT_GENERAL_SOUND=45,
    SOURCE_EVENT_GLOBAL_SOUND=46, SOURCE_EVENT_TEAM_SOUND=47,
    SOURCE_EVENT_OBITUARY=60
};
static bool live(qa_bots *b,bot_ai_state *s) {
    return !s->retired && bot_ai_live(b,s->view.actor);
}
static int32_t increment(int32_t value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));++bits;
    memcpy(&value,&bits,sizeof(value));return value;
}
static qa_vec3 vector(const float value[3]) {
    return qa_v3(value[0],value[1],value[2]);
}
static bool dont_avoid(qa_bots *b,bot_ai_state *s,const char *name,qa_error *e) {
    int32_t index=-1;qa_bot_goal goal={0};
    for(;;) {
        bool found;
        if(!qa_bot_goals_level_item(qa_bot_runtime_goals(b->runtime),index,name,&goal,&found,e)) return false;
        if(!found || !live(b,s)) return true;
        if(!qa_bot_goals_avoid_remove(qa_bot_runtime_goals(b->runtime),s->goals,goal.number,e)) return false;
        index=goal.number;
    }
}
static bool sound_name(qa_bots *b,const char *event,int32_t parameter,
                        char name[128],bool *valid,qa_error *e) {
    *valid=parameter>=0 && parameter<=256;
    if(!*valid) {
        char message[128];snprintf(message,sizeof(message),"%s: eventParm (%d) out of range\n",event,parameter);
        return bot_ai_source_print(b,message,e);
    }
    if(!b->services.configstring) return bot_ai_fail(e,"Source bot events require actual GAME configstrings");
    return b->services.configstring(b->services.context,288+(uint32_t)parameter,name,128,e);
}
static bool obituary(qa_bots *b,bot_ai_state *s,const qa_q3_entity *entity,qa_error *e) {
    int32_t target=entity->otherEntityNum,attacker=entity->otherEntityNum2;
    int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    if(!live(b,s)) return true;
    if(target==self) {
        bot_ai_bot_death_type_set(s,entity->eventParm);bot_ai_last_killed_by_set(s,attacker);
        bot_ai_bot_suicide_set(s,target==attacker || target==QA_Q3_ENTITY_NONE || target==QA_Q3_ENTITY_WORLD);
        bot_ai_num_deaths_set(s,increment(bot_ai_num_deaths(s)));
    } else if(attacker==self) {
        bot_ai_enemy_death_type_set(s,entity->eventParm);bot_ai_last_killed_player_set(s,target);
        bot_ai_killed_enemy_time_set(s,b->time);bot_ai_num_kills_set(s,increment(bot_ai_num_kills(s)));
    } else if(attacker==bot_ai_enemy_number(s) && target==attacker) bot_ai_enemy_suicide_set(s,true);
    if(s->team_arena && b->source_goals.game_type==5) {
        qa_bot_entity_info observed;bool found;
        if(!qa_bot_runtime_entity(b->runtime,target,&observed,&found,e)) return false;
        if(found && (observed.state.powerups&(1<<9))) {
            bool same_team;if(!bot_ai_source_same_team(b,s,target,&same_team,e)) return false;
            if(!same_team && live(b,s)) {
                s->source_order.neutral_flag_status=3;s->source_order.flag_status_changed=true;
            }
        }
    }
    return true;
}
static bool team_sound(qa_bots *b,bot_ai_state *s,int32_t sound,qa_error *e) {
    bot_source_order_state *order=&s->source_order;
    if(b->source_goals.game_type==4) switch(sound) {
    case 0:case 1:order->blue_flag_status=0;order->red_flag_status=0;order->flag_status_changed=true;break;
    case 2:order->blue_flag_status=0;order->flag_status_changed=true;break;
    case 3:order->red_flag_status=0;order->flag_status_changed=true;break;
    case 4:order->blue_flag_status=1;order->flag_status_changed=true;break;
    case 5:order->red_flag_status=1;order->flag_status_changed=true;break;
    default:break;
    } else if(s->team_arena && b->source_goals.game_type==5) switch(sound) {
    case 0:case 1:case 2:case 3:order->neutral_flag_status=0;order->flag_status_changed=true;break;
    case 4:case 5: {
        int32_t self,team;
        if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_source_team(b,self,&team,e)) return false;
        if(live(b,s)) {
            order->neutral_flag_status=team==(sound==4?1:2)?2:1;order->flag_status_changed=true;
        }
        break;
    }
    default:break;
    }
    return true;
}
bool bot_ai_source_check_event(qa_bots *b,bot_ai_state *s,const qa_q3_entity *entity,qa_error *e) {
    if(entity->number<0 || entity->number>=BOT_SOURCE_EVENT_ENTITIES)
        return bot_ai_fail(e,"Source bot event entity is outside its actual allocation");
    if(!b->services.source_event_time)
        return bot_ai_fail(e,"Source bot event has no actual fixed GAME eventTime reader");
    int32_t now;
    if(!b->services.source_event_time(b->services.context,entity->number,&now,e)) return false;
    if(!live(b,s)) return true;
    if(bot_ai_entity_event_time(s,(uint32_t)entity->number)==now) return true;
    bot_ai_entity_event_time_set(s,(uint32_t)entity->number,now);
    int32_t event=(entity->eType>13?entity->eType-13:entity->event)&~INT32_C(0x300);
    switch(event) {
    case SOURCE_EVENT_OBITUARY:return obituary(b,s,entity,e);
    case SOURCE_EVENT_GLOBAL_SOUND: {
        char sound[128];bool valid;
        if(!sound_name(b,"EV_GLOBAL_SOUND",entity->eventParm,sound,&valid,e)) return false;
        if(!valid || !live(b,s)) return true;
        if(s->team_arena && !strcmp(sound,"sound/items/kamikazerespawn.wav")) return dont_avoid(b,s,"Kamikaze",e);
        if(!strcmp(sound,"sound/items/poweruprespawn.wav")) {
            static const char *const names[]={"Quad Damage","Regeneration","Battle Suit","Speed","Invisibility"};
            for(size_t i=0;i<5 && live(b,s);++i) if(!dont_avoid(b,s,names[i],e)) return false;
            if(live(b,s)) bot_ai_long_term_until_set(s,0);
        }
        return true;
    }
    case SOURCE_EVENT_TEAM_SOUND:return team_sound(b,s,entity->eventParm,e);
    case SOURCE_EVENT_TELEPORT_IN:
        b->source_event_globals.last_teleport_origin=vector(entity->origin);
        b->source_event_globals.last_teleport_time=b->time;return true;
    case SOURCE_EVENT_GENERAL_SOUND: {
        int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
        if(entity->number!=self || !live(b,s)) return true;
        char sound[128];bool valid;
        if(!sound_name(b,"EV_GENERAL_SOUND",entity->eventParm,sound,&valid,e)) return false;
        if(valid && live(b,s) && !strcmp(sound,"*falling1.wav") && bot_ai_inventory_value(s,QA_BOT_INV_TELEPORTER)>0)
            return qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_USE,e);
        return true;
    }
    default:return true;
    }
}
static bool snapshot_avoid(qa_bots *b,bot_ai_state *s,const qa_q3_entity *entity,qa_error *e) {
    if(entity->eType==3 && entity->weapon==4) {
        qa_bot_avoid_spot spot={.origin=vector(entity->pos.base),.radius=160,.type=1};
        if(!qa_bot_moves_avoid_spot(qa_bot_runtime_moves(b->runtime),s->movement,&spot,e)) return false;
    }
    if(!s->team_arena || !live(b,s)) return true;
    if(entity->eType==3 && entity->weapon==12) {
        int32_t self,team;
        if(!bot_ai_source_client(b,s,&self,e) || !bot_ai_source_team(b,self,&team,e)) return false;
        if(!live(b,s)) return true;
        if(team!=1 && team!=2) team=0;
        if(entity->generic1!=team) {
            bool armed=(bot_ai_inventory_value(s,QA_BOT_INV_PLASMA)>0 && bot_ai_inventory_value(s,QA_BOT_INV_CELLS)>0) ||
                (bot_ai_inventory_value(s,QA_BOT_INV_ROCKET)>0 && bot_ai_inventory_value(s,QA_BOT_INV_ROCKETS)>0) ||
                (bot_ai_inventory_value(s,QA_BOT_INV_BFG)>0 && bot_ai_inventory_value(s,QA_BOT_INV_BFG_AMMO)>0);
            if(armed) {
                qa_bot_avoid_spot spot={.origin=vector(entity->pos.base),.radius=160,.type=1};
                if(!qa_bot_moves_avoid_spot(qa_bot_runtime_moves(b->runtime),s->movement,&spot,e)) return false;
                if(live(b,s) && bot_ai_num_prox_mines(s)<BOT_SOURCE_PROX_MINES) {
                    int32_t index=bot_ai_num_prox_mines(s);
                    if(index<0) return bot_ai_fail(e,"Source proximity count is outside its actual BotState array");
                    bot_ai_prox_mine_set(s,(uint32_t)index,entity->number);
                    bot_ai_num_prox_mines_set(s,index+1);
                }
            }
        }
    }
    if(live(b,s) && (entity->eFlags&0x200) && (entity->eFlags&1)) bot_ai_kamikaze_body_set(s,entity->number);
    return true;
}
static bool current_entity(qa_bots *b,int32_t number,qa_q3_entity *entity,qa_error *e) {
    if(number<0 || number>=BOT_SOURCE_EVENT_ENTITIES)
        return bot_ai_fail(e,"Source bot snapshot number is outside its actual GAME allocation");
    if(!b->services.source_entity)
        return bot_ai_fail(e,"Source bot snapshot has no actual current GAME entity reader");
    bool available;
    if(!b->services.source_entity(b->services.context,number,entity,&available,e)) return false;
    if(!available) *entity=(qa_q3_entity){0};
    return true;
}
bool bot_ai_source_check_snapshot(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(!b->services.snapshot_entity)
        return bot_ai_fail(e,"Source bot snapshot lacks its actual admitted PS or native snapshot owner");
    qa_bot_avoid_spot clear={.origin={0},.radius=0,.type=0};
    if(!qa_bot_moves_avoid_spot(qa_bot_runtime_moves(b->runtime),s->movement,&clear,e)) return false;
    if(!live(b,s)) return true;
    bot_ai_kamikaze_body_set(s,0);bot_ai_num_prox_mines_set(s,0);
    int32_t sequence=0;
    for(;;) {
        int32_t number;bool present;
        if(!b->services.snapshot_entity(b->services.context,s->view.actor,sequence,&number,&present,e)) return false;
        if(!live(b,s)) return true;
        if(!present) break;
        sequence=increment(sequence);
        qa_q3_entity entity;
        if(!current_entity(b,number,&entity,e)) return false;
        if(!live(b,s)) return true;
        if(!bot_ai_source_check_event(b,s,&entity,e)) return false;
        if(!live(b,s)) return true;
        if(!snapshot_avoid(b,s,&entity,e)) return false;
        if(!live(b,s)) return true;
    }
    int32_t self;if(!bot_ai_source_client(b,s,&self,e)) return false;
    qa_q3_entity player;if(!current_entity(b,self,&player,e)) return false;
    if(!live(b,s)) return true;
    if(!bot_ai_source_player_word(b,s,BOT_PS_EXTERNAL_EVENT,&player.event,e) ||
       !bot_ai_source_player_word(b,s,BOT_PS_EXTERNAL_EVENT_PARAMETER,&player.eventParm,e)) return false;
    return bot_ai_source_check_event(b,s,&player,e);
}
bool bot_ai_source_set_teleport_time(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(!live(b,s)) return true;
    int32_t flags;
    if(!bot_ai_source_player_word(b,s,BOT_PS_ENTITY_FLAGS,&flags,e)) return false;
    if((flags^bot_ai_last_e_flags(s))&4) bot_ai_teleport_time_set(s,b->time);
    bot_ai_last_e_flags_set(s,flags);return true;
}
