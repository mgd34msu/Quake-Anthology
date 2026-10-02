#include "internal.h"
#include "source_goal_record.h"
#include "source_activation.h"
#include "source_event_state.h"
#include "source_team_state.h"
#include "source_inventory.h"
#include "source_player.h"
#include "source_view.h"
#include "source_combat_vectors.h"
#include "source_timers.h"
#include "source_behavior_state.h"
#include "source_selectors.h"
#include "source_flags.h"

enum { BOT_AIR_GOAL=128, BOT_DEFAULT_TRAVEL=0x011c0fbe, BOT_LIQUID=8|16|32 };
static qa_bot_goals *goals(qa_bots *b) { return qa_bot_runtime_goals(b->runtime); }
static qa_bot_moves *moves(qa_bots *b) { return qa_bot_runtime_moves(b->runtime); }
static qa_bot_navigation *navigation(qa_bots *b, bot_ai_state *s) {
    return qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
}
static bool live(qa_bots *b,bot_ai_state *s) {return !s->retired && bot_ai_live(b,s->view.actor);}
#define DECISION_CALL(call) do {if(!(call)) return false;if(!live(b,s)) return true;} while(0)
static bool enter(qa_bots *b, bot_ai_state *s, qa_bot_decision decision,qa_error *e) {
    if(s->view.decision==QA_BOT_ACTIVATING && decision!=QA_BOT_ACTIVATING &&
       !bot_ai_activation_clear(b,s,e)) return false;
    if(decision==QA_BOT_FIGHTING && !qa_bot_moves_reset_avoid(moves(b),s->movement,true,e)) return false;
    s->view.decision=decision;s->state_time=b->time;
    if(decision==QA_BOT_CHASING) bot_ai_chase_time_set(s,b->time);
    if(decision==QA_BOT_SEEK_LONG_TERM) bot_ai_check_time_set(s,0);
    if(decision==QA_BOT_STANDING) bot_ai_stand_enemy_time_set(s,b->time+1);
    return true;
}
#define ENTER(decision) do {if(!enter(b,s,(decision),e)) return false;} while(0)
bool bot_ai_source_reached_goal(qa_bots *b, bot_ai_state *s, const qa_bot_goal *goal,
                      bool *done, qa_error *e) {
    qa_bot_source_goal_status status;
    *done=false;
    if(!qa_bot_goals_source_status(goals(b),(int32_t)s->view.client,goal,&status,e)) return false;
    if(status!=QA_BOT_GOAL_NATIVE) {*done=status==QA_BOT_GOAL_UNAVAILABLE;return true;}
    bool touching=qa_bot_goal_touching(bot_ai_origin(s),goal);
    if(goal->flags&QA_BOT_GOAL_ITEM) {
        if(touching) {
            *done=true;
            return (goal->flags&QA_BOT_GOAL_DROPPED) ||
                qa_bot_goals_avoid_set(goals(b),s->goals,goal->number,-1,e);
        }
        if(!qa_bot_goals_missing_visible(goals(b),(int32_t)s->view.client,bot_ai_eye(s),goal,done,e)) return false;
        if(*done) return true;
        if(bot_ai_area(s)==(uint32_t)goal->area && bot_ai_origin(s).x>goal->origin.x+goal->mins.x &&
           bot_ai_origin(s).x<goal->origin.x+goal->maxs.x && bot_ai_origin(s).y>goal->origin.y+goal->mins.y &&
           bot_ai_origin(s).y<goal->origin.y+goal->maxs.y) {
            bool swimming;
            if(!qa_bot_navigation_swimming(navigation(b,s),bot_ai_origin(s),&swimming,e)) return false;
            *done=!swimming;
        }
    } else *done=touching || ((goal->flags&BOT_AIR_GOAL) && bot_ai_last_air_time(s)>b->time-1);
    return true;
}
static bool choose(qa_bots *b, bot_ai_state *s, bool nearby, const qa_bot_goal *long_term,
                     float range, bool *found, qa_error *e) {
    bot_source_inventory inventory={b,s};
    qa_bot_inventory_view source=bot_ai_source_inventory_view(&inventory);
    qa_bot_goal_choice query={.origin=bot_ai_origin(s),.inventory_source=&source,
        .travel_flags=bot_ai_travel_flags(s),
        .nearby=nearby,.long_term=long_term,.maximum_time=range};
    return qa_bot_goals_choose(goals(b),s->goals,&query,found,e);
}
bool bot_ai_source_go_for_air(qa_bots *b, bot_ai_state *s, const qa_bot_goal *long_term,
                     float range, bool *found, qa_error *e) {
    *found=false;
    if(bot_ai_last_air_time(s)<b->time-6) {
        qa_bot_navigation *nav=navigation(b,s);
        qa_bounds bounds={qa_v3(-15,-15,-2),qa_v3(15,15,2)};
        qa_trace_result ceiling,surface;
        qa_vec3 above=qa_vec_add(bot_ai_origin(s),qa_v3(0,0,1000));
        if(!qa_bot_navigation_trace(nav,bot_ai_origin(s),above,&bounds,s->view.actor,0x10001,&ceiling,e) ||
           !qa_bot_navigation_trace(nav,ceiling.end,bot_ai_origin(s),&bounds,s->view.actor,BOT_LIQUID,&surface,e)) return false;
        if(surface.fraction>0) {
            uint32_t area;
            if(!bot_ai_point_area(b,s,surface.end,&area,e)) return false;
            if(area) {
                qa_bot_goal air={.origin=qa_vec_add(surface.end,qa_v3(0,0,-2)),.area=(int32_t)area,
                    .mins=qa_v3(-15,-15,-1),.maxs=qa_v3(15,15,1),.flags=BOT_AIR_GOAL};
                if(!qa_bot_goals_push(goals(b),s->goals,&air,found,e)) return false;
                if(*found) return true;
            }
        }
        for(;;) {
            if(!choose(b,s,true,long_term,range,found,e)) return false;
            if(!*found) break;
            qa_bot_goal goal;bool top;int32_t contents;
            if(!qa_bot_goals_top(goals(b),s->goals,false,&goal,&top,e)) return false;
            if(!top) return bot_ai_fail(e,"nearby bot item selection returned no goal");
            if(!qa_bot_navigation_contents(nav,goal.origin,&contents,e)) return false;
            if(!(contents&BOT_LIQUID)) return true;
            if(!qa_bot_goals_pop(goals(b),s->goals,e)) return false;
        }
        if(!qa_bot_goals_avoid_clear(goals(b),s->goals,e)) return false;
    }
    *found=false;return true;
}
static bool nearby(qa_bots *b,bot_ai_state *s,const qa_bot_goal *long_term,
    float range,bool *found,qa_error *e) {
    if(!bot_ai_source_go_for_air(b,s,long_term,range,found,e)) return false;
    if(*found || s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0) {
        qa_bot_nav_route_query query={.area=bot_ai_area(s),.origin=bot_ai_origin(s),.has_origin=true,
            .goal_area=(uint32_t)bot_ai_team_goal(s).area,.travel_flags=BOT_DEFAULT_TRAVEL};
        qa_bot_nav_route route;
        if(!qa_bot_navigation_route(navigation(b,s),&query,&route,e)) return false;
        if(route.travel_time<300) range=50;
    }
    return choose(b,s,true,long_term,range,found,e);
}
static float objective_nearby_range(qa_bots *b,bot_ai_state *s,float range) {
    int32_t type=b->source_goals.game_type;
    if(type==4 && (bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0)) return 50;
    if(s->team_arena) {
        if(type==5 && bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0) return 50;
        if(type==7 && (bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0)) return 80;
    }
    return range;
}
bool bot_ai_source_item_goal(qa_bots *b, bot_ai_state *s, qa_bot_goal *goal, bool *found, qa_error *e) {
    if(!qa_bot_goals_top(goals(b),s->goals,false,goal,found,e)) return false;
    if(!*found) bot_ai_long_term_until_set(s,0);
    else {
        bool done;
        if(!bot_ai_source_reached_goal(b,s,goal,&done,e)) return false;
        if(done) {
            if(!bot_ai_choose_weapon(b,s,e)) return false;
            bot_ai_long_term_until_set(s,0);
        }
    }
    if(bot_ai_long_term_until(s)<b->time) {
        if(!qa_bot_goals_pop(goals(b),s->goals,e) || !choose(b,s,false,NULL,0,found,e)) return false;
        if(*found) bot_ai_long_term_until_set(s,b->time+20);
        else if(!qa_bot_goals_avoid_clear(goals(b),s->goals,e) ||
                !qa_bot_moves_reset_avoid(moves(b),s->movement,false,e)) return false;
        if(!qa_bot_goals_top(goals(b),s->goals,false,goal,found,e)) return false;
    } else *found=true;
    return true;
}
static bool travel(qa_bots *b, bot_ai_state *s, qa_error *e) {
    bot_ai_travel_flags_set(s,BOT_DEFAULT_TRAVEL);
    int32_t contents;
    if(!qa_bot_navigation_contents(navigation(b,s),qa_vec_add(bot_ai_origin(s),qa_v3(0,0,-23)),&contents,e)) return false;
    if(contents&(16|32)) bot_ai_travel_flags_set(s,bot_ai_travel_flags(s)|0x600000);
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,e)) return false;
    bool grapple=qa_bot_knowledge_travel(weapons,count,bot_ai_inventory(s),QA_NAV_GRAPPLE)>=0;
    bool rocket=false,bfg=false;
    for(size_t i=0;i<count;++i) {
        const qa_bot_weapon_knowledge *weapon=weapons+i;
        int32_t own=weapon->weapon.weapon_inventory,ammo=weapon->weapon.ammo_inventory;
        if(!weapon->weapon.valid || own<0 || own>=QA_BOT_INVENTORY_SIZE || !bot_ai_inventory_value(s,own) ||
           ammo<0 || ammo>=QA_BOT_INVENTORY_SIZE || bot_ai_inventory_value(s,ammo)<3 ||
           bot_ai_inventory_value(s,ammo)<weapon->weapon.ammo_amount) continue;
        rocket|=(weapon->travel_modes&QA_NAV_CAPABILITY(QA_NAV_ROCKET_JUMP))!=0;
        bfg|=(weapon->travel_modes&QA_NAV_CAPABILITY(QA_NAV_BFG_JUMP))!=0;
    }
    b->services.arsenal_end(b->services.context,lease);
    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
    if(b->controls.grapple && grapple) bot_ai_travel_flags_set(s,bot_ai_travel_flags(s)|0x4000);
    if(b->controls.rocket_jump && s->view.decision!=QA_BOT_RETREATING &&
       s->view.decision!=QA_BOT_BATTLE_NEARBY && (rocket || bfg) &&
       !bot_ai_inventory_value(s,QA_BOT_INV_QUAD) && bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)>=60 &&
       (bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)>=90 || bot_ai_inventory_value(s,QA_BOT_INV_ARMOR)>=40)) {
        float propensity;
        if(!bot_ai_character_float(b,s,BOT_C_WEAPON_JUMP,0,1,&propensity,e)) return false;
        if(propensity>=.5f) bot_ai_travel_flags_set(s,bot_ai_travel_flags(s)|(rocket?0x1000u:0u)|(bfg?0x2000u:0u));
    }
    return true;
}
static bool activation_weapon(qa_bots *b,bot_ai_state *s,bool mine,int32_t *out,qa_error *e) {
    *out=0;
    const qa_bot_weapon_knowledge *weapons=NULL;size_t count=0;void *lease=NULL;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,e)) return false;
    if(count && !weapons) {
        b->services.arsenal_end(b->services.context,lease);
        return bot_ai_fail(e,"activation arsenal returned absent weapon storage");
    }
    if(live(b,s)) {
        if(!mine) *out=qa_bot_knowledge_activation(weapons,count,bot_ai_inventory(s),s->team_arena);
        else {
            static const int32_t roles[]={8,5,9};
            for(size_t r=0;r<sizeof(roles)/sizeof(*roles) && !*out;++r) {
                for(size_t i=0;i<count;++i) {
                    if(qa_bot_weapon_role(weapons+i)!=roles[r]) continue;
                    int32_t selected=qa_bot_knowledge_activation(weapons+i,1,bot_ai_inventory(s),s->team_arena);
                    if(selected>0) {*out=selected;break;}
                }
            }
        }
    }
    b->services.arsenal_end(b->services.context,lease);
    if(*out<0) *out=0;
    return true;
}
static bool clear_path_attack(qa_bots *b,bot_ai_state *s,const qa_q3_entity *entity,
                               qa_vec3 target,qa_bot_move_result *result,qa_error *e) {
    if(!result->weapon) return true;
    result->flags|=QA_BOT_MOVE_VIEW|QA_BOT_MOVE_WEAPON;
    int32_t held;DECISION_CALL(bot_ai_source_player_word(b,s,BOT_PS_WEAPON,&held,e));
    if(held!=result->weapon || !qa_bot_field_of_vision(bot_ai_view_angles(s),20,result->ideal_view_angles)) return true;
    int32_t viewer=bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENTITY);
    qa_trace_result shot;
    DECISION_CALL(qa_bot_navigation_trace(navigation(b,s),bot_ai_eye(s),target,NULL,
        b->services.entity_actor(b->services.context,viewer),0x6000001,&shot,e));
    qa_actor_id hit=b->services.entity_actor(b->services.context,entity->number);
    if(shot.fraction>=1 || (hit.registry && qa_actor_id_equal(shot.actor,hit)))
        DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_ATTACK,e));
    return true;
}
static bool clear_path(qa_bots *b,bot_ai_state *s,qa_bot_move_result *result,qa_error *e) {
    const uint32_t movement_aim=QA_BOT_MOVE_VIEW|QA_BOT_MOVE_WEAPON;
    if(bot_ai_kamikaze_body(s)!=0 && !(result->flags&movement_aim)) {
        qa_q3_entity body;DECISION_CALL(bot_ai_source_current_entity(b,bot_ai_kamikaze_body(s),&body,e));
        qa_vec3 target=qa_v3(body.pos.base[0],body.pos.base[1],body.pos.base[2]+8);
        result->ideal_view_angles=bot_ai_angles(qa_vec_sub(target,bot_ai_eye(s)));
        DECISION_CALL(activation_weapon(b,s,false,&result->weapon,e));
        DECISION_CALL(clear_path_attack(b,s,&body,target,result,e));
    }
    if(result->flags&QA_BOT_MOVE_AVOID_SPOT) bot_ai_blocked_avoid_time_set(s,b->time+5);
    if(bot_ai_blocked_avoid_time(s)>b->time && !(result->flags&movement_aim)) {
        float best_distance=300;int32_t best=-1;
        for(int32_t i=0;i<bot_ai_num_prox_mines(s);++i) {
            if(i>=BOT_SOURCE_PROX_MINES)
                return bot_ai_fail(e,"clear path proximity index exceeds its actual BotState array");
            qa_q3_entity mine;DECISION_CALL(bot_ai_source_current_entity(b,bot_ai_prox_mine(s,(uint32_t)i),&mine,e));
            qa_vec3 origin=qa_v3(mine.pos.base[0],mine.pos.base[1],mine.pos.base[2]);
            float distance=qa_vec_length(qa_vec_sub(origin,bot_ai_origin(s)));
            if(distance<best_distance) {best_distance=distance;best=i;}
        }
        if(best>=0) {
            qa_q3_entity mine;DECISION_CALL(bot_ai_source_current_entity(b,bot_ai_prox_mine(s,(uint32_t)best),&mine,e));
            qa_vec3 target=qa_v3(mine.pos.base[0],mine.pos.base[1],mine.pos.base[2]+2);
            result->ideal_view_angles=bot_ai_angles(qa_vec_sub(target,bot_ai_eye(s)));
            DECISION_CALL(activation_weapon(b,s,true,&result->weapon,e));
            DECISION_CALL(clear_path_attack(b,s,&mine,target,result,e));
        }
    }
    return true;
}
typedef struct activation_query_state {
    qa_bots *bots;
    bot_ai_state *state;
} activation_query_state;
static bool activation_query_current(const activation_query_state *query,qa_error *e) {
    return live(query->bots,query->state) || bot_ai_fail(e,"activation query lost its actual bot owner");
}
static bool activation_query_origin(void *opaque,qa_vec3 *out,qa_error *e) {
    activation_query_state *query=opaque;
    if(!activation_query_current(query,e)) return false;
    *out=bot_ai_origin(query->state);return true;
}
static bool activation_query_eye(void *opaque,qa_vec3 *out,qa_error *e) {
    activation_query_state *query=opaque;
    if(!activation_query_current(query,e)) return false;
    *out=bot_ai_eye(query->state);return true;
}
static bool activation_query_area(void *opaque,int32_t *out,qa_error *e) {
    activation_query_state *query=opaque;
    if(!activation_query_current(query,e)) return false;
    *out=bot_source_i32_read(query->state->source_span.data+QA_BOT_SOURCE_AREA);return true;
}
static bool activation_query_travel(void *opaque,uint32_t *out,qa_error *e) {
    activation_query_state *query=opaque;
    if(!activation_query_current(query,e)) return false;
    *out=bot_ai_travel_flags(query->state);return true;
}
static bool activation_query_top(void *opaque,qa_bot_source_activation *out,bool *found,qa_error *e) {
    activation_query_state *query=opaque;uint32_t index;
    if(!activation_query_current(query,e) || !bot_ai_activation_peek_top(query->state,&index,found,e)) return false;
    *out=*found?bot_ai_activation_read(query->state,index):(qa_bot_source_activation){0};return true;
}
static bool activation_query_developer(void *opaque,bool *out,qa_error *e) {
    activation_query_state *query=opaque;
    if(!activation_query_current(query,e)) return false;
    *out=query->bots->source_match.cvars[BOT_SOURCE_DEVELOPER].integer_value!=0;return true;
}
static bool source_activation(qa_bots *b,bot_ai_state *s,int32_t blocker,
                              qa_bot_source_activation *out,int32_t *bsp,qa_error *e) {
    activation_query_state state={b,s};
    qa_bot_activation_query query={.context=&state,.origin=activation_query_origin,.eye=activation_query_eye,
        .area=activation_query_area,.travel_flags=activation_query_travel,.top=activation_query_top,
        .developer=activation_query_developer,.time=b->time};
    return b->services.source_activation(b->services.context,s->view.actor,blocker,&query,out,bsp,e);
}
static bool activation_enable(qa_bots *b,bot_ai_state *s,qa_bot_source_activation *row,qa_error *e) {
    if(!row->areas_disabled) return true;
    if(row->area_count<0 || row->area_count>32)
        return bot_ai_fail(e,"activation routing count exceeds its actual Source row");
    for(int32_t i=0;i<row->area_count;++i) {
        bool previous;
        DECISION_CALL(qa_bot_navigation_enable(navigation(b,s),(uint32_t)row->areas[i],true,&previous,e));
    }
    row->areas_disabled=false;return true;
}
static bool activation_go(qa_bots *b,bot_ai_state *s,qa_bot_source_activation *row,qa_error *e) {
    row->inuse=true;
    if(!row->time) row->time=b->time+10;
    row->start_time=b->time;
    qa_bot_entity_info info;bool found;
    DECISION_CALL(qa_bot_runtime_entity(b->runtime,row->goal.entity,&info,&found,e));
    row->origin=info.state.origin;
    bool pushed;
    if(!bot_ai_activation_push(s,row,b->time,&pushed,e)) return false;
    if(pushed) ENTER(QA_BOT_ACTIVATING);
    else DECISION_CALL(activation_enable(b,s,row,e));
    return true;
}
static bool predict_obstacles(qa_bots *b,bot_ai_state *s,const qa_bot_goal *goal,bool *handled,qa_error *e) {
    *handled=false;
    if(!b->services.source_activation || !b->services.source_model_bounds ||
       !b->source_match.cvars[BOT_SOURCE_PREDICT_OBSTACLES].integer_value) return true;
    if(bot_ai_predict_obstacles_area(s)==goal->area && bot_ai_predict_obstacles_time(s)>b->time-6) return true;
    bot_ai_predict_obstacles_area_set(s,goal->area);bot_ai_predict_obstacles_time_set(s,b->time);
    qa_bot_route_prediction_query query={.route={.area=bot_ai_area(s),.origin=bot_ai_origin(s),
        .has_origin=true,.goal_area=(uint32_t)goal->area,.travel_flags=bot_ai_travel_flags(s)},
        .maximum_areas=100,.maximum_time=1000,.stop_events=QA_BOT_ROUTE_TRAVEL|QA_BOT_ROUTE_CONTENTS,
        .stop_contents=1024,.stop_travel_flags=0x04000000};
    qa_bot_route_prediction route;
    DECISION_CALL(qa_bot_navigation_predict_route(navigation(b,s),&query,&route,e));
    if((route.stop_event&QA_BOT_ROUTE_CONTENTS) && (route.end_contents&1024u)) {
        int32_t model=(int32_t)(route.end_contents>>24);
        if(model>=128) model-=256;
        if(model) {
            int32_t entity;
            DECISION_CALL(b->services.source_model_bounds(b->services.context,model,4,0,NULL,NULL,&entity,e));
            if(entity) {
                qa_bot_source_activation row;int32_t bsp;
                DECISION_CALL(source_activation(b,s,entity,&row,&bsp,e));
                if(bsp) {
                    bool duplicate;
                    if(!bot_ai_activation_drop_inactive_top(s,e) ||
                       !bot_ai_activation_contains(s,row.goal.entity,b->time,&duplicate,e)) return false;
                    if(!duplicate) {
                        DECISION_CALL(activation_go(b,s,&row,e));
                        *handled=true;return true;
                    }
                    DECISION_CALL(activation_enable(b,s,&row,e));
                }
            }
        }
    }
    return true;
}
static bool move_goal(qa_bots *b, bot_ai_state *s, const qa_bot_goal *goal,bool activate,
                        qa_bot_move_result *result, qa_error *e) {
    memset(result,0,sizeof(*result));
    DECISION_CALL(bot_ai_move_setup(b,s,e));
    DECISION_CALL(qa_bot_moves_goal(moves(b),s->movement,goal,bot_ai_travel_flags(s),result,e));
    if(result->failure) {
        DECISION_CALL(qa_bot_moves_reset_avoid(moves(b),s->movement,false,e));
        if(s->view.decision==QA_BOT_ACTIVATING) {
            uint32_t index;bool have;
            if(!bot_ai_activation_top(s,&index,&have,e)) return false;
            if(have) bot_ai_activation_time_set(s,index,0);
        } else if(s->view.decision==QA_BOT_SEEK_NEARBY || s->view.decision==QA_BOT_BATTLE_NEARBY)
            bot_ai_nearby_until_set(s,0);
        else bot_ai_long_term_until_set(s,0);
        if(bot_ai_order_active(s)) s->view.order.status=QA_BOT_ORDER_ERROR;
    }
    if(result->blocked) {
        if(result->type==8) {
            float random;DECISION_CALL(bot_ai_random(b,&random,e));
            qa_vec3 direction;qa_builtin_angle_vectors(qa_v3(0,random*360,0),&direction,NULL,NULL);
            bool moved;DECISION_CALL(qa_bot_moves_direction(moves(b),s->movement,direction,400,
                QA_BOT_DIRECTION_WALK,&moved,e));
            result->failure=false;result->direction=direction;return true;
        }
        qa_bot_entity_info blocker;bool observed;
        DECISION_CALL(qa_bot_runtime_entity(b->runtime,result->block_entity,&blocker,&observed,e));
        if(activate && b->services.source_activation && blocker.state.model_index>0 &&
           blocker.state.model_index<=b->source_goals.max_bsp_model_index) {
            qa_bot_source_activation row;int32_t bsp;
            DECISION_CALL(source_activation(b,s,blocker.number,&row,&bsp,e));
            if(bsp) {
                bool duplicate;
                if(!bot_ai_activation_drop_inactive_top(s,e) ||
                   !bot_ai_activation_contains(s,row.goal.entity,b->time,&duplicate,e)) return false;
                if(!duplicate) DECISION_CALL(activation_go(b,s,&row,e));
                if(!(result->flags&QA_BOT_MOVE_ON_OBSTACLE) &&
                   qa_bot_navigation_area(navigation(b,s),bot_ai_area(s)).reach_count) return true;
            } else DECISION_CALL(activation_enable(b,s,&row,e));
        } else if(activate && !b->services.source_activation && b->services.activation) {
            qa_bot_activation activation;bool found;
            DECISION_CALL(b->services.activation(b->services.context,s->view.actor,blocker.number,&activation,&found,e));
            if(found && bot_ai_live(b,activation.target)) {
                qa_bot_entity target;
                if(!b->services.entity(b->services.context,activation.target,&target,e)) return false;
                if(!live(b,s)) return true;
                if(target.present && bot_ai_live(b,activation.target) &&
                   qa_actor_id_equal(b->services.entity_actor(b->services.context,target.number),activation.target)) {
                    bool duplicate;
                    if(!bot_ai_activation_contains(s,target.number,b->time,&duplicate,e)) return false;
                    if(!duplicate) {
                        qa_bot_entity_info target_info;bool target_found;
                        if(!qa_bot_runtime_entity(b->runtime,target.number,&target_info,&target_found,e)) return false;
                        qa_bot_source_activation row={.goal=activation.goal,.time=b->time+10,.start_time=b->time,
                            .shoot=activation.shoot,.target=activation.aim,.origin=target_info.state.origin};
                        row.goal.entity=target.number;
                        bool pushed;if(!bot_ai_activation_push(s,&row,b->time,&pushed,e)) return false;
                        if(pushed) ENTER(QA_BOT_ACTIVATING);
                    }
                }
                if(!(result->flags&QA_BOT_MOVE_ON_OBSTACLE) &&
                   qa_bot_navigation_area(navigation(b,s),bot_ai_area(s)).reach_count) return true;
            }
        }
        qa_vec3 direction=qa_v3(result->direction.x,result->direction.y,0);
        float length=qa_vec_length(direction);
        if(length!=0) direction=qa_vec_scale(direction,1.0f/length);
        if(length<.1f) {
            float random;DECISION_CALL(bot_ai_random(b,&random,e));
            qa_builtin_angle_vectors(qa_v3(0,random*360,0),&direction,NULL,NULL);
        }
        qa_vec3 sideward=qa_vec_cross(direction,qa_v3(0,0,1));
        if(bot_ai_flag(s,BOT_AI_AVOID_RIGHT)) sideward=qa_vec_scale(sideward,-1);
        bool moved;DECISION_CALL(qa_bot_moves_direction(moves(b),s->movement,sideward,400,
            QA_BOT_DIRECTION_WALK,&moved,e));
        if(!moved) {
            bot_ai_flag_toggle(s,BOT_AI_AVOID_RIGHT);sideward=qa_vec_sub(sideward,direction);
            DECISION_CALL(qa_bot_moves_direction(moves(b),s->movement,sideward,400,
                QA_BOT_DIRECTION_WALK,&moved,e));
        }
        if(bot_ai_not_blocked_time(s)<b->time-.4f) {
            if(s->view.decision==QA_BOT_SEEK_NEARBY) bot_ai_nearby_until_set(s,0);
            else if(s->view.decision==QA_BOT_SEEK_LONG_TERM) bot_ai_long_term_until_set(s,0);
        }
    } else bot_ai_not_blocked_time_set(s,b->time);
    return true;
}
static bool movement_view(qa_bots *b,bot_ai_state *s,const qa_bot_goal *goal,
                            const qa_bot_move_result *result,qa_bot_decision node,qa_error *e) {
    bool seeking=node==QA_BOT_SEEK_LONG_TERM || node==QA_BOT_SEEK_NEARBY || node==QA_BOT_ACTIVATING;
    if(result->flags&(QA_BOT_MOVE_VIEW|QA_BOT_MOVE_SWIM_VIEW|QA_BOT_MOVE_VIEW_SET))
        bot_ai_view_ideal_set(s,result->ideal_view_angles);
    else if(seeking && (result->flags&QA_BOT_MOVE_WAITING)) {
        float random;DECISION_CALL(bot_ai_random(b,&random,e));
        if(random<bot_ai_think_time(s)*.8f) {
            qa_vec3 target;DECISION_CALL(bot_ai_source_roam_goal(b,s,&target,e));
            qa_vec3 ideal=bot_ai_angles(qa_vec_sub(target,bot_ai_origin(s)));ideal.z*=.5f;
            bot_ai_view_ideal_set(s,ideal);
        }
    } else if(!seeking || !bot_ai_flag(s,BOT_AI_IDEAL_VIEW_SET)) {
        qa_vec3 target;bool found;
        qa_bot_goal view_goal=*goal;
        if(node==QA_BOT_SEEK_NEARBY) {
            DECISION_CALL(qa_bot_goals_top(goals(b),s->goals,true,&view_goal,&found,e));
            if(!found) {DECISION_CALL(qa_bot_goals_top(goals(b),s->goals,false,&view_goal,&found,e));}
            if(!found) view_goal=*goal;
        }
        DECISION_CALL(qa_bot_moves_view_target(moves(b),s->movement,&view_goal,bot_ai_travel_flags(s),300,&target,&found,e));
        if(found) bot_ai_view_ideal_set(s,bot_ai_angles(qa_vec_sub(target,seeking?bot_ai_origin(s):bot_ai_eye(s))));
        else if(node==QA_BOT_SEEK_LONG_TERM && qa_vec_dot(result->direction,result->direction)==0) {
            float random;DECISION_CALL(bot_ai_random(b,&random,e));
            if(random<bot_ai_think_time(s)*.8f) {
                DECISION_CALL(bot_ai_source_roam_goal(b,s,&target,e));
                qa_vec3 ideal=bot_ai_angles(qa_vec_sub(target,bot_ai_origin(s)));ideal.z*=.5f;
                bot_ai_view_ideal_set(s,ideal);
            }
        } else if(seeking || qa_vec_length(result->direction)>0)
            bot_ai_view_ideal_set(s,bot_ai_angles(result->direction));
        qa_vec3 ideal=bot_ai_view_ideal(s);bot_ai_view_ideal_axis_set(s,2,ideal.z*.5f);
    }
    if(result->flags&QA_BOT_MOVE_WEAPON) bot_ai_weapon_number_set(s,result->weapon);
    return true;
}
static bool remember_enemy_position(qa_bots *b,bot_ai_state *s,qa_vec3 origin,qa_error *e) {
    int32_t enemy=bot_ai_enemy_number(s);
    if(enemy>=64 && s->team_arena &&
       (enemy==b->source_goals.red_obelisk.entity || enemy==b->source_goals.blue_obelisk.entity)) origin.z+=16;
    uint32_t area;
    DECISION_CALL(bot_ai_point_area(b,s,origin,&area,e));
    if(area && qa_bot_navigation_area(navigation(b,s),area).reach_count) {
        bot_ai_last_enemy_area_set(s,area);bot_ai_last_enemy_origin_set(s,origin);
    }
    return true;
}
static bool enemy_state(qa_bots *b, bot_ai_state *s, bool remember,
                         bool *alive, bool *visible, qa_error *e) {
    *alive=false;*visible=false;
    qa_actor_id enemy_actor=bot_ai_enemy_actor(b,s);
    s->view.enemy=enemy_actor;
    if(!bot_ai_live(b,enemy_actor)) return true;
    qa_bot_player enemy;bool present;
    if(!bot_ai_target(b,s,enemy_actor,&enemy,&present,e)) return false;
    if(s->retired || !bot_ai_live(b,s->view.actor) || !bot_ai_live(b,enemy_actor)) return true;
    *alive=present && !enemy.dead && !enemy.observer && enemy.connected;
    if(!*alive) return true;
    float visibility;
    DECISION_CALL(bot_ai_source_entity_visible(b,s,bot_ai_enemy_number(s),360,&visibility,e));
    *visible=visibility>0;
    if(*visible && remember) {
        bot_ai_enemy_visible_time_set(s,b->time);
        qa_bot_entity_info info;bool observed;
        DECISION_CALL(qa_bot_runtime_entity(b->runtime,bot_ai_enemy_number(s),&info,&observed,e));
        return remember_enemy_position(b,s,info.state.origin,e);
    }
    return true;
}
static bool battle_items(qa_bots *b,bot_ai_state *s,qa_error *e) {
    bool carrying=s->player.carrying_objective;
    if((bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<40 && bot_ai_inventory_value(s,QA_BOT_INV_TELEPORTER)>0 && !carrying) ||
       (bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<60 && bot_ai_inventory_value(s,QA_BOT_INV_MEDKIT)>0))
        DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_USE,e));
    return true;
}
static bool battle(qa_bots *b, bot_ai_state *s, bool moving, qa_error *e) {
    DECISION_CALL(battle_items(b,s,e));
    DECISION_CALL(bot_ai_choose_weapon(b,s,e));
    return bot_ai_attack(b,s,moving,e);
}
static bool lifecycle(qa_bots *b, bot_ai_state *s, bool *handled, qa_error *e) {
    *handled=true;
    if(s->view.decision==QA_BOT_INTERMISSION) {
        bool intermission;DECISION_CALL(bot_ai_source_intermission(b,s,&intermission,e));
        if(!intermission) {
            bool chat;DECISION_CALL(bot_ai_source_chat_start_level(b,s,&chat,e));
            float duration=2;
            if(chat) {DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));}
            bot_ai_stand_until_set(s,b->time+duration);ENTER(QA_BOT_STANDING);
        }
        return true;
    }
    if(s->view.decision==QA_BOT_OBSERVER) {
        bool observer;DECISION_CALL(bot_ai_source_observer(b,s,&observer,e));
        if(!observer) ENTER(QA_BOT_STANDING);
        return true;
    }
    if(s->view.decision==QA_BOT_RESPAWNING) {
        if(bot_ai_respawn_wait(s)) {
            int32_t move_type;
            DECISION_CALL(bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&move_type,e));
            if(move_type!=3) ENTER(QA_BOT_SEEK_LONG_TERM);
            else {DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_RESPAWN,e));}
        } else if(bot_ai_respawn_time(s)<b->time) {
            bot_ai_respawn_wait_set(s,true);
            DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_RESPAWN,e));
            if(bot_ai_respawn_chat_time(s)!=0) {
                DECISION_CALL(qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),0,
                    (qa_bot_chat_destination)bot_ai_chat_to(s),e));
                bot_ai_enemy_number_set(s,-1);s->view.enemy=(qa_actor_id){0};
            }
        }
        if(bot_ai_respawn_chat_time(s)!=0 && bot_ai_respawn_chat_time(s)<b->time-.5f) {
            DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_TALK,e));
        }
        return true;
    }
    bool observer,intermission=false;DECISION_CALL(bot_ai_source_observer(b,s,&observer,e));
    if(!observer) {DECISION_CALL(bot_ai_source_intermission(b,s,&intermission,e));}
    int32_t move_type=0;
    if(!observer && !intermission) {
        DECISION_CALL(bot_ai_source_player_word(b,s,BOT_PS_MOVE_TYPE,&move_type,e));
    }
    qa_bot_decision next=observer?QA_BOT_OBSERVER:
        intermission?QA_BOT_INTERMISSION:move_type==3?QA_BOT_RESPAWNING:QA_BOT_SEEK_LONG_TERM;
    if(next!=QA_BOT_SEEK_LONG_TERM) {
        if(next==QA_BOT_RESPAWNING) {
            DECISION_CALL(qa_bot_moves_reset(moves(b),s->movement,e));
            DECISION_CALL(qa_bot_goals_reset(goals(b),s->goals,e));
            DECISION_CALL(qa_bot_goals_avoid_clear(goals(b),s->goals,e));
            DECISION_CALL(qa_bot_moves_reset_avoid(moves(b),s->movement,false,e));
            bool chat;DECISION_CALL(bot_ai_source_chat_death(b,s,&chat,e));
            if(chat) {
                float duration;DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                bot_ai_respawn_time_set(s,b->time+duration);bot_ai_respawn_chat_time_set(s,b->time);
            } else {
                float random;DECISION_CALL(bot_ai_random(b,&random,e));
                volatile float base=b->time+1;bot_ai_respawn_time_set(s,base+random);bot_ai_respawn_chat_time_set(s,0);
            }
            bot_ai_respawn_wait_set(s,false);
        } else {
            DECISION_CALL(bot_ai_reset(b,s,e));
            if(next==QA_BOT_INTERMISSION) {
                bool chat;DECISION_CALL(bot_ai_source_chat_end_level(b,s,&chat,e));
                if(chat) {
                    DECISION_CALL(qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),0,
                        (qa_bot_chat_destination)bot_ai_chat_to(s),e));
                }
            }
        }
        ENTER(next);
        /* The new source node runs in the same deathmatch dispatch loop. */
        return lifecycle(b,s,handled,e);
    }
    *handled=false;return true;
}
bool bot_ai_decide(qa_bots *b, bot_ai_state *s, qa_error *e) {
    bool handled;
    if(!lifecycle(b,s,&handled,e)) return false;
    if(handled || s->retired) return true;
    if(bot_ai_order_active(s) && s->view.decision!=QA_BOT_ACTIVATING)
        ENTER(QA_BOT_SEEK_LONG_TERM);
    for(unsigned switches=0;switches<50;++switches) {
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        qa_bot_decision node=s->view.decision;
        if(node==QA_BOT_STANDING) {
            if(bot_ai_last_frame_health(s)>bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)) {
                bool chat;DECISION_CALL(bot_ai_source_chat_hit_talking(b,s,&chat,e));
                if(chat) {
                    float duration;DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                    volatile float until=b->time+duration;bot_ai_stand_enemy_time_set(s,until+.1f);
                    DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                    until=b->time+duration;bot_ai_stand_until_set(s,until+.1f);
                }
            }
            if(bot_ai_stand_enemy_time(s)<b->time) {
                bool found;DECISION_CALL(bot_ai_find_enemy(b,s,-1,&found,e));
                if(found) {ENTER(QA_BOT_FIGHTING);continue;}
                bot_ai_stand_enemy_time_set(s,b->time+1);
            }
            DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_TALK,e));
            if(bot_ai_stand_until(s)<b->time) {
                DECISION_CALL(qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),0,
                    (qa_bot_chat_destination)bot_ai_chat_to(s),e));
                ENTER(QA_BOT_SEEK_LONG_TERM);continue;
            }
            return true;
        }
        if(node==QA_BOT_SEEK_LONG_TERM || node==QA_BOT_SEEK_NEARBY || node==QA_BOT_ACTIVATING) {
            qa_bot_goal goal={0};bool found,done=false;
            bool ordered=bot_ai_order_active(s);
            if(node==QA_BOT_SEEK_LONG_TERM && !ordered) {
                bool chat;DECISION_CALL(bot_ai_source_chat_random(b,s,&chat,e));
                if(chat) {
                    float duration;DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                    bot_ai_stand_until_set(s,b->time+duration);ENTER(QA_BOT_STANDING);continue;
                }
            }
            DECISION_CALL(travel(b,s,e));
            bot_ai_enemy_number_set(s,-1);s->view.enemy=(qa_actor_id){0};
            if(node==QA_BOT_ACTIVATING) {
                uint32_t index;bool have;
                if(!bot_ai_activation_top(s,&index,&have,e)) return false;
                if(!have) {ENTER(QA_BOT_SEEK_NEARBY);continue;}
                qa_bot_source_activation activation=bot_ai_activation_read(s,index);
                bool visible=false;
                if(activation.shoot) {
                    qa_trace_result shot;
                    DECISION_CALL(qa_bot_navigation_trace(navigation(b,s),bot_ai_eye(s),activation.target,
                        NULL,b->services.entity_actor(b->services.context,
                            bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENTITY)),0x6000001,&shot,e));
                    if(!bot_ai_activation_top(s,&index,&have,e)) return false;
                    if(!have) continue;
                    activation=bot_ai_activation_read(s,index);
                    qa_actor_id target=b->services.entity_actor(b->services.context,activation.goal.entity);
                    visible=shot.fraction>=1 || (target.registry && qa_actor_id_equal(shot.actor,target));
                    if(visible) {
                        int32_t held;
                        DECISION_CALL(bot_ai_source_player_word(b,s,BOT_PS_WEAPON,&held,e));
                        activation=bot_ai_activation_read(s,index);
                        if(held==activation.weapon) {
                            qa_vec3 aim=bot_ai_angles(qa_vec_sub(activation.target,bot_ai_eye(s)));
                            if(qa_bot_field_of_vision(bot_ai_view_angles(s),20,aim))
                                DECISION_CALL(qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_ATTACK,e));
                        }
                        qa_bot_entity_info target_info;bool target_found;
                        DECISION_CALL(qa_bot_runtime_entity(b->runtime,activation.goal.entity,&target_info,&target_found,e));
                        if(!bot_ai_activation_top(s,&index,&have,e)) return false;
                        if(!have) continue;
                        activation=bot_ai_activation_read(s,index);
                        if(activation.origin.x!=target_info.state.origin.x ||
                           activation.origin.y!=target_info.state.origin.y || activation.origin.z!=target_info.state.origin.z)
                            bot_ai_activation_time_set(s,index,0);
                    }
                } else {
                    goal=activation.goal;
                    if(qa_bot_goal_touching(bot_ai_origin(s),&goal)) bot_ai_activation_time_set(s,index,0);
                }
                activation=bot_ai_activation_read(s,index);
                if(activation.time<b->time) {
                    if(!bot_ai_activation_pop(b,s,e) || !bot_ai_activation_top(s,&index,&have,e)) return false;
                    if(have) {bot_ai_activation_time_set(s,index,b->time+10);continue;}
                    ENTER(QA_BOT_SEEK_NEARBY);continue;
                }
                qa_bot_move_result result={0};
                if(!visible) {
                    goal=activation.goal;
                    bool predicted;DECISION_CALL(predict_obstacles(b,s,&goal,&predicted,e));
                    if(predicted) continue;
                    DECISION_CALL(move_goal(b,s,&goal,true,&result,e));
                }
                DECISION_CALL(clear_path(b,s,&result,e));
                if(!bot_ai_activation_top(s,&index,&have,e)) return false;
                if(!have) return bot_ai_fail(e,"activation movement cleared the actual source stack");
                activation=bot_ai_activation_read(s,index);
                if(activation.shoot) {
                    if(!(result.flags&QA_BOT_MOVE_VIEW)) {
                        result.ideal_view_angles=bot_ai_angles(qa_vec_sub(activation.target,bot_ai_eye(s)));
                        result.flags|=QA_BOT_MOVE_VIEW;
                    }
                    if(!(result.flags&QA_BOT_MOVE_WEAPON)) {
                        int32_t weapon;DECISION_CALL(activation_weapon(b,s,false,&weapon,e));
                        if(!bot_ai_activation_top(s,&index,&have,e)) return false;
                        if(!have) return true;
                        bot_ai_activation_weapon_set(s,index,weapon);
                        result.flags|=QA_BOT_MOVE_WEAPON;result.weapon=weapon;
                    }
                }
                DECISION_CALL(movement_view(b,s,&goal,&result,node,e));
                bool enemy_found;DECISION_CALL(bot_ai_find_enemy(b,s,-1,&enemy_found,e));
                if(enemy_found) {
                    bool retreat;DECISION_CALL(bot_ai_retreat(b,s,&retreat,e));
                    if(retreat) ENTER(QA_BOT_BATTLE_NEARBY);
                    else {
                        DECISION_CALL(qa_bot_moves_reset_avoid(moves(b),s->movement,true,e));
                        DECISION_CALL(qa_bot_goals_empty(goals(b),s->goals,e));
                        ENTER(QA_BOT_FIGHTING);
                    }
                }
                return true;
            }
            if(node==QA_BOT_SEEK_LONG_TERM) {
                if(!ordered) {
                    if(!bot_ai_find_enemy(b,s,-1,&found,e)) return false;
                    if(found) {
                        bool retreat;if(!bot_ai_retreat(b,s,&retreat,e)) return false;
                        ENTER(retreat?QA_BOT_RETREATING:QA_BOT_FIGHTING);continue;
                    }
                }
                if(ordered) {if(!bot_ai_order_goal(b,s,&goal,&found,e)) return false;}
                else {
                    if(!bot_ai_source_team_goals(b,s,false,e)) return false;
                    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                    if(!bot_ai_source_long_term_goal(b,s,false,&goal,&found,e)) return false;
                }
                if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                if(!found) return true;
                if(!ordered && bot_ai_check_time(s)<b->time) {
                    bot_ai_check_time_set(s,b->time+.5f);
                    bool accepted,nearby_found;
                    if(!bot_ai_source_wants_camp(b,s,&accepted,e)) return false;
                    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                    float range=objective_nearby_range(b,s,bot_ai_long_term_goal(s)==BOT_LTG_DEFEND?400:150);
                    if(!nearby(b,s,&goal,range,&nearby_found,e)) return false;
                    if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                    if(nearby_found) {
                        if(!qa_bot_moves_reset_avoid(moves(b),s->movement,true,e)) return false;
                        volatile float added=range*.01f,until=b->time+4;bot_ai_nearby_until_set(s,until+added);
                        ENTER(QA_BOT_SEEK_NEARBY);continue;
                    }
                }
            } else {
                if(!qa_bot_goals_top(goals(b),s->goals,false,&goal,&found,e)) return false;
                if(found && !bot_ai_source_reached_goal(b,s,&goal,&done,e)) return false;
                if(!found || done || bot_ai_nearby_until(s)<b->time) {
                    if(found && !qa_bot_goals_pop(goals(b),s->goals,e)) return false;
                    ENTER(QA_BOT_SEEK_LONG_TERM);bot_ai_check_time_set(s,b->time+.05f);continue;
                }
            }
            bool predicted;DECISION_CALL(predict_obstacles(b,s,&goal,&predicted,e));
            if(predicted) continue;
            qa_bot_move_result result;
            DECISION_CALL(move_goal(b,s,&goal,true,&result,e));
            DECISION_CALL(clear_path(b,s,&result,e));
            DECISION_CALL(movement_view(b,s,&goal,&result,node,e));
            bool enemy_found;
            if(!bot_ai_find_enemy(b,s,-1,&enemy_found,e)) return false;
            if(ordered) {
                if(bot_ai_enemy_number(s)>=0 && !(result.flags&(QA_BOT_MOVE_VIEW|QA_BOT_MOVE_SWIM_VIEW|QA_BOT_MOVE_VIEW_SET|QA_BOT_MOVE_WEAPON))) {
                    DECISION_CALL(bot_ai_battle_inventory(b,s,bot_ai_enemy_number(s),e));
                    return battle(b,s,true,e);
                }
            } else if(enemy_found) {
                bool retreat;if(!bot_ai_retreat(b,s,&retreat,e)) return false;
                if(node==QA_BOT_SEEK_NEARBY && retreat) ENTER(QA_BOT_BATTLE_NEARBY);
                else {if(!qa_bot_goals_empty(goals(b),s->goals,e)) return false;ENTER(QA_BOT_FIGHTING);}
                continue;
            }
            return true;
        }
        if(node==QA_BOT_FIGHTING || node==QA_BOT_CHASING || node==QA_BOT_RETREATING || node==QA_BOT_BATTLE_NEARBY) {
            bool found,alive,visible;
            if(node==QA_BOT_FIGHTING || node==QA_BOT_RETREATING) {
                DECISION_CALL(bot_ai_find_enemy(b,s,bot_ai_enemy_number(s),&found,e));
            }
            if(node==QA_BOT_FIGHTING) {
                if(bot_ai_enemy_number(s)<0) {ENTER(QA_BOT_SEEK_LONG_TERM);continue;}
                qa_bot_entity_info info;bool observed;
                DECISION_CALL(qa_bot_runtime_entity(b->runtime,bot_ai_enemy_number(s),&info,&observed,e));
                if(bot_ai_enemy_death_time(s)!=0) {
                    if(bot_ai_enemy_death_time(s)<b->time-1) {
                        bot_ai_enemy_death_time_set(s,0);
                        bool chat=false;
                        if(bot_ai_enemy_suicide(s)) {
                            DECISION_CALL(bot_ai_source_chat_enemy_suicide(b,s,&chat,e));
                        }
                        chat=false;
                        if(bot_ai_last_killed_player(s)==bot_ai_enemy_number(s)) {
                            DECISION_CALL(bot_ai_source_chat_kill(b,s,&chat,e));
                        }
                        if(chat) {
                            float duration;DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                            bot_ai_stand_until_set(s,b->time+duration);ENTER(QA_BOT_STANDING);
                        } else {bot_ai_long_term_until_set(s,0);ENTER(QA_BOT_SEEK_LONG_TERM);}
                        continue;
                    }
                } else {
                    if(info.number>=0 && info.number<64) {
                        if(!b->services.source_player_state)
                            return bot_ai_fail(e,"bot enemy death requires the actual fixed source player state");
                        qa_bot_source_player_state enemy;
                        DECISION_CALL(b->services.source_player_state(b->services.context,info.number,&enemy,e));
                        if(enemy.has_player && enemy.pm_type!=0) bot_ai_enemy_death_time_set(s,b->time);
                    }
                }
                uint32_t flags=(1u<<7)|(1u<<8)|(b->services.team_arena?(1u<<9):0);
                if(!((uint32_t)info.state.powerups&flags) && ((uint32_t)info.state.powerups&(1u<<4)) &&
                   !(info.state.flags&0x100)) {
                    float random;DECISION_CALL(bot_ai_random(b,&random,e));
                    if(random<.2f) {ENTER(QA_BOT_SEEK_LONG_TERM);continue;}
                }
                DECISION_CALL(remember_enemy_position(b,s,info.state.origin,e));
                DECISION_CALL(bot_ai_battle_inventory(b,s,bot_ai_enemy_number(s),e));
                bool chat=false;
                if(bot_ai_last_frame_health(s)>bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)) {
                    DECISION_CALL(bot_ai_source_chat_hit_no_death(b,s,&chat,e));
                }
                if(!chat) {
                    int32_t hit_count;
                    DECISION_CALL(bot_ai_source_player_slot(b,s,BOT_PS_PERSISTENT,1,&hit_count,e));
                    if(hit_count>bot_ai_last_hit_count(s)) {
                        DECISION_CALL(bot_ai_source_chat_hit_no_kill(b,s,&chat,e));
                    }
                }
                if(chat) {
                    float duration;DECISION_CALL(bot_ai_source_chat_time(b,s,&duration,e));
                    bot_ai_stand_until_set(s,b->time+duration);ENTER(QA_BOT_STANDING);continue;
                }
            }
            DECISION_CALL(enemy_state(b,s,node==QA_BOT_RETREATING || node==QA_BOT_BATTLE_NEARBY,
                &alive,&visible,e));
            if(!alive) {
                if(node==QA_BOT_FIGHTING && bot_ai_enemy_death_time(s)!=0) return true;
                s->view.enemy=(qa_actor_id){0};
                ENTER(node==QA_BOT_BATTLE_NEARBY?QA_BOT_SEEK_NEARBY:QA_BOT_SEEK_LONG_TERM);continue;
            }
            bool retreat=false,chase=false;
            if(node==QA_BOT_RETREATING) {
                DECISION_CALL(bot_ai_battle_inventory(b,s,bot_ai_enemy_number(s),e));
            }
            if(node==QA_BOT_RETREATING || (node==QA_BOT_FIGHTING && !visible)) {
                DECISION_CALL(bot_ai_chase(b,s,&chase,e));
            }
            if(node==QA_BOT_FIGHTING) {
                if(!visible) {ENTER(chase?QA_BOT_CHASING:QA_BOT_SEEK_LONG_TERM);continue;}
                DECISION_CALL(battle_items(b,s,e));
                DECISION_CALL(travel(b,s,e));
                DECISION_CALL(bot_ai_choose_weapon(b,s,e));
                DECISION_CALL(bot_ai_attack_move(b,s,e));
                DECISION_CALL(bot_ai_attack(b,s,false,e));
                if(!bot_ai_flag(s,BOT_AI_FIGHT_SUICIDAL)) {
                    DECISION_CALL(bot_ai_retreat(b,s,&retreat,e));
                    if(retreat) ENTER(QA_BOT_RETREATING);
                }
                return true;
            }
            if(node==QA_BOT_CHASING) {
                if(visible) {ENTER(QA_BOT_FIGHTING);continue;}
                DECISION_CALL(bot_ai_find_enemy(b,s,-1,&found,e));
                if(found) {ENTER(QA_BOT_FIGHTING);continue;}
            }
            DECISION_CALL(travel(b,s,e));
            if(node==QA_BOT_RETREATING && chase) {
                if(!qa_bot_goals_empty(goals(b),s->goals,e)) return false;
                ENTER(QA_BOT_CHASING);continue;
            }
            if(node==QA_BOT_RETREATING && !visible && bot_ai_enemy_visible_time(s)<b->time-4) {
                ENTER(QA_BOT_SEEK_LONG_TERM);continue;
            }
            if(node==QA_BOT_RETREATING && !visible) {
                DECISION_CALL(bot_ai_find_enemy(b,s,-1,&found,e));
                if(found) {ENTER(QA_BOT_FIGHTING);continue;}
            }
            qa_bot_goal goal={0};bool goal_found=true;
            if(node==QA_BOT_CHASING) {
                if(!bot_ai_last_enemy_area(s) || !bot_ai_chase_time(s) || bot_ai_chase_time(s)<b->time-10) {ENTER(QA_BOT_SEEK_LONG_TERM);continue;}
                goal=(qa_bot_goal){.origin=bot_ai_last_enemy_origin(s),.area=(int32_t)bot_ai_last_enemy_area(s),
                    .mins=qa_v3(-8,-8,-8),.maxs=qa_v3(8,8,8),.entity=-1};
                if(qa_bot_goal_touching(bot_ai_origin(s),&goal)) {bot_ai_chase_time_set(s,0);ENTER(QA_BOT_SEEK_LONG_TERM);continue;}
            } else if(node==QA_BOT_BATTLE_NEARBY) {
                bool done=false;
                if(!qa_bot_goals_top(goals(b),s->goals,false,&goal,&goal_found,e)) return false;
                if(goal_found && !bot_ai_source_reached_goal(b,s,&goal,&done,e)) return false;
                if(!goal_found || done || bot_ai_nearby_until(s)<b->time) {
                    if(goal_found && !qa_bot_goals_pop(goals(b),s->goals,e)) return false;
                    if(!qa_bot_goals_top(goals(b),s->goals,false,&goal,&goal_found,e)) return false;
                    ENTER(goal_found?QA_BOT_RETREATING:QA_BOT_FIGHTING);continue;
                }
            } else {
                if(!bot_ai_source_team_goals(b,s,true,e)) return false;
                if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                if(!bot_ai_source_long_term_goal(b,s,true,&goal,&goal_found,e)) return false;
            }
            if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
            if(!goal_found) {ENTER(QA_BOT_FIGHTING);bot_ai_flag_set(s,BOT_AI_FIGHT_SUICIDAL,true);continue;}
            if(node!=QA_BOT_BATTLE_NEARBY && bot_ai_check_time(s)<b->time) {
                bot_ai_check_time_set(s,b->time+1);
                bool nearby_found;
                float range=node==QA_BOT_RETREATING?objective_nearby_range(b,s,150):150;
                if(!nearby(b,s,&goal,range,&nearby_found,e)) return false;
                if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
                if(nearby_found) {
                    if(!qa_bot_moves_reset_avoid(moves(b),s->movement,true,e)) return false;
                    volatile float added=range/100,until=b->time+added;
                    bot_ai_nearby_until_set(s,node==QA_BOT_CHASING?b->time+16:until+1);
                    ENTER(QA_BOT_BATTLE_NEARBY);continue;
                }
            }
            if(node==QA_BOT_CHASING) {
                DECISION_CALL(bot_ai_battle_inventory(b,s,bot_ai_enemy_number(s),e));
            }
            qa_bot_move_result result;
            DECISION_CALL(move_goal(b,s,&goal,false,&result,e));
            DECISION_CALL(movement_view(b,s,&goal,&result,node,e));
            if(node==QA_BOT_BATTLE_NEARBY) {
                DECISION_CALL(bot_ai_battle_inventory(b,s,bot_ai_enemy_number(s),e));
            }
            if(visible && !(result.flags&(QA_BOT_MOVE_VIEW|QA_BOT_MOVE_SWIM_VIEW|QA_BOT_MOVE_VIEW_SET|QA_BOT_MOVE_WEAPON)))
                return battle(b,s,true,e);
            if(node==QA_BOT_CHASING) {
                DECISION_CALL(bot_ai_retreat(b,s,&retreat,e));
                if(retreat) ENTER(QA_BOT_RETREATING);
            }
            return true;
        }
        return bot_ai_fail(e,"invalid native bot decision continuation");
    }
    if(b->services.diagnostic) b->services.diagnostic(b->services.context,QA_SCRIPT_WARNING,
        "native bot exceeded 50 decision transitions in one think");
    return true;
}
