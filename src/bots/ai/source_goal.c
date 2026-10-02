/* Native source ai_dmnet.c LTG consumers and ai_dmq3.c camp decisions.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include "source_timers.h"
#include "source_inventory.h"
#include "source_goal.h"
#include "source_orders.h"
#include "source_team_policy.h"
#include "source_events.h"
#include "source_storage.h"
#include "source_view.h"

enum { SOURCE_DEFAULT_TRAVEL=0x011c0fbe, SOURCE_LIQUID=8|16|32, SOURCE_FOG=64 };

static bool alive(const qa_bots *b,const bot_ai_state *s) {
    return !s->retired && bot_ai_live(b,s->view.actor);
}
#define SOURCE_CALL(operation) do { if(!(operation)) return false; \
    if(!alive(b,s)) return true; } while(0)

static qa_bot_navigation *navigation(qa_bots *b,bot_ai_state *s) {
    return qa_bot_runtime_navigation(b->runtime,(int32_t)s->view.client);
}
static qa_bot_goals *goals(qa_bots *b) { return qa_bot_runtime_goals(b->runtime); }
static bool action(qa_bots *b,bot_ai_state *s,uint32_t flag,qa_error *e) {
    return !alive(b,s) || qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,flag,e);
}
static bool reset_avoid(qa_bots *b,bot_ai_state *s,qa_error *e) {
    return !alive(b,s) || qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime),s->movement,false,e);
}
static bool chat(qa_bots *b,bot_ai_state *s,const char *type,const char *variable,
                  int32_t recipient,qa_bot_chat_destination destination,qa_error *e) {
    const char *variables[8]={variable,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    SOURCE_CALL(bot_ai_source_initial_chat(b,s,type,variables,e));
    return qa_bot_chat_enter(qa_bot_runtime_chat(b->runtime,s->chat),recipient,destination,e);
}
static bool team_chat(qa_bots *b,bot_ai_state *s,const char *type,const char *voice,qa_error *e) {
    SOURCE_CALL(chat(b,s,type,NULL,0,QA_BOT_CHAT_TEAM,e));
    SOURCE_CALL(bot_ai_source_voice(b,s,-1,voice,true,e));
    bot_ai_team_message_time_set(s,0);return true;
}
static bool acknowledge(qa_bots *b,bot_ai_state *s,const char *type,const char *variable,qa_error *e) {
    SOURCE_CALL(chat(b,s,type,variable,s->decisionmaker,QA_BOT_CHAT_TELL,e));
    SOURCE_CALL(bot_ai_source_voice(b,s,s->decisionmaker,"yes",true,e));
    SOURCE_CALL(action(b,s,QA_BOT_AFFIRMATIVE,e));
    bot_ai_team_message_time_set(s,0);return true;
}
static bool companion_name(qa_bots *b,int32_t client,char out[36],qa_error *e) {
    return bot_ai_easy_name(b,client,out,36,e);
}
static bool companion_goal(qa_bots *b,bot_ai_state *s,qa_bot_goal *goal,int32_t client,
                            qa_vec3 origin,qa_error *e) {
    uint32_t area;
    SOURCE_CALL(bot_ai_point_area(b,s,origin,&area,e));
    if(area && qa_bot_navigation_area(navigation(b,s),area).reach_count) {
        goal->entity=client;goal->area=(int32_t)area;goal->origin=origin;
        goal->mins=qa_v3(-8,-8,-8);goal->maxs=qa_v3(8,8,8);
    }
    return true;
}
static bool team_base(qa_bots *b,bot_ai_state *s,qa_bot_goal *goal,bool enemy,
                        bool obelisk,bool *found,qa_error *e) {
    int32_t self,team;*found=false;
    SOURCE_CALL(bot_ai_source_client(b,s,&self,e));
    SOURCE_CALL(bot_ai_source_team(b,self,&team,e));
    if(team!=1 && team!=2) return true;
    bool red=(team==1)!=enemy;
    *goal=obelisk?(red?b->source_goals.red_obelisk:b->source_goals.blue_obelisk):
                    (red?b->source_goals.red_flag:b->source_goals.blue_flag);
    *found=true;return true;
}
static bool travel_time(qa_bots *b,bot_ai_state *s,const qa_bot_goal *goal,uint32_t flags,
                          uint32_t *time,qa_error *e) {
    qa_bot_nav_route_query query={.area=s->area,.goal_area=(uint32_t)goal->area,
        .origin=s->player.origin,.has_origin=true,.travel_flags=flags};
    qa_bot_nav_route route;
    SOURCE_CALL(qa_bot_navigation_route(navigation(b,s),&query,&route,e));
    *time=route.travel_time;return true;
}
static void look_at(bot_ai_state *s,qa_vec3 target) {
    qa_vec3 ideal=bot_ai_angles(qa_vec_sub(target,s->player.origin));ideal.z*=.5f;
    bot_ai_view_ideal_set(s,ideal);
}
static bool crouch(qa_bots *b,bot_ai_state *s,qa_error *e) {
    if(bot_ai_attack_crouch_time(s)<b->time-5.0f) {
        float propensity,random;
        SOURCE_CALL(bot_ai_character_float(b,s,BOT_C_CROUCHER,0,1,&propensity,e));
        SOURCE_CALL(bot_ai_random(b,&random,e));
        if(random<s->view.think_time*propensity) {
            float span=propensity*15.0f;
            bot_ai_attack_crouch_time_set(s,(b->time+5.0f)+span);
        }
    }
    return true;
}
static bool swimming(qa_bots *b,bot_ai_state *s,bool *out,qa_error *e) {
    return qa_bot_navigation_swimming(navigation(b,s),s->player.origin,out,e);
}
static bool observation(qa_bots *b,int32_t client,qa_bot_entity_info *out,qa_error *e) {
    bool found;
    return qa_bot_runtime_entity(b->runtime,client,out,&found,e);
}
static bool contents(qa_bots *b,qa_vec3 point,qa_actor_id pass,int32_t *out,qa_error *e) {
    qa_point_query query={.point=point,.pass_actor=pass,.q3_server_entities=true,
        .policy={.family=QA_COLLISION_Q3,.q1_hull=-1}};
    qa_point_contents result;
    if(!qa_world_point_contents(b->services.shared.world,&query,&result,e)) return false;
    *out=result.contents;return true;
}

void bot_ai_source_goal_init(bot_source_goal_state *state) {
    *state=(bot_source_goal_state){0};
}

bool bot_ai_source_entity_visible(qa_bots *b,bot_ai_state *s,int32_t entity,
                                  float *out,qa_error *e) {
    *out=0;if(!alive(b,s)) return true;
    qa_bot_entity_info info;
    SOURCE_CALL(observation(b,entity,&info,e));
    qa_vec3 middle=qa_vec_add(info.state.origin,
        qa_vec_scale(qa_vec_add(info.state.mins,info.state.maxs),.5f));
    qa_bot_navigation *nav=navigation(b,s);
    int32_t point_contents;
    SOURCE_CALL(contents(b,s->player.eye,(qa_actor_id){0},&point_contents,e));
    bool in_fog=(point_contents&SOURCE_FOG)!=0,in_water=(point_contents&SOURCE_LIQUID)!=0;
    qa_actor_id target=bot_ai_source_actor(b,entity);
    qa_actor_id viewer=bot_ai_source_actor(b,s->view.entity);
    for(unsigned i=0;i<3;++i) {
        uint32_t mask=1|0x10000;
        qa_vec3 start=s->player.eye,end=middle;
        qa_actor_id pass=viewer,hit=target;
        SOURCE_CALL(contents(b,middle,(qa_actor_id){0},&point_contents,e));
        if(point_contents&SOURCE_LIQUID) mask|=SOURCE_LIQUID;
        if(in_water) {
            if(!(mask&SOURCE_LIQUID)) {pass=target;hit=viewer;start=middle;end=s->player.eye;}
            mask^=SOURCE_LIQUID;
        }
        qa_trace_result trace;
        SOURCE_CALL(qa_bot_navigation_trace(nav,start,end,NULL,pass,mask,&trace,e));
        /* BotAITrace clears contents in its bsp_trace_t result. */
        if(trace.fraction>=1 || (hit.registry && qa_actor_id_equal(trace.actor,hit))) {
            SOURCE_CALL(contents(b,middle,(qa_actor_id){0},&point_contents,e));
            bool other_fog=(point_contents&SOURCE_FOG)!=0;
            float fog_distance=0;
            if(in_fog && other_fog) {
                qa_vec3 direction=qa_vec_sub(trace.end,s->player.eye);
                fog_distance=qa_vec_dot(direction,direction);
            } else if(in_fog) {
                SOURCE_CALL(qa_bot_navigation_trace(nav,trace.end,s->player.eye,NULL,
                    viewer,SOURCE_FOG,&trace,e));
                qa_vec3 direction=qa_vec_sub(s->player.eye,trace.end);
                fog_distance=qa_vec_dot(direction,direction);
            } else if(other_fog) {
                end=trace.end;
                SOURCE_CALL(qa_bot_navigation_trace(nav,s->player.eye,end,NULL,
                    viewer,SOURCE_FOG,&trace,e));
                qa_vec3 direction=qa_vec_sub(end,trace.end);
                fog_distance=qa_vec_dot(direction,direction);
            }
            float visibility=1.0f/fmaxf(1.0f,fog_distance*.001f);
            if(visibility>*out) *out=visibility;
            if(*out>=.95f) return true;
        }
        if(i==0) middle.z+=info.state.mins.z;
        else if(i==1) middle.z+=info.state.maxs.z-info.state.mins.z;
    }
    return true;
}

bool bot_ai_source_roam_goal(qa_bots *b,bot_ai_state *s,qa_vec3 *out,qa_error *e) {
    *out=qa_v3(0,0,0);if(!alive(b,s)) return true;
    qa_bot_navigation *nav=navigation(b,s);
    for(unsigned attempt=0;attempt<10;++attempt) {
        qa_vec3 best=s->player.origin;
        float random,sign,magnitude;
        SOURCE_CALL(bot_ai_random(b,&random,e));
        if(random>.25f) {
            SOURCE_CALL(bot_ai_random(b,&sign,e));
            SOURCE_CALL(bot_ai_random(b,&magnitude,e));
            best.x+=(sign<.5f?-1.0f:1.0f)*(800.0f*magnitude+100.0f);
        }
        if(random<.75f) {
            SOURCE_CALL(bot_ai_random(b,&sign,e));
            SOURCE_CALL(bot_ai_random(b,&magnitude,e));
            best.y+=(sign<.5f?-1.0f:1.0f)*(800.0f*magnitude+100.0f);
        }
        SOURCE_CALL(bot_ai_random(b,&random,e));
        best.z+=96.0f*(2.0f*(random-.5f));
        qa_trace_result trace;
        SOURCE_CALL(qa_bot_navigation_trace(nav,s->player.origin,best,NULL,s->view.actor,1,&trace,e));
        qa_vec3 direction=qa_vec_sub(trace.end,s->player.origin);
        float distance=qa_vec_length(direction);
        if(distance>200.0f) {
            best=qa_vec_add(s->player.origin,
                qa_vec_scale(qa_vec_normalize(direction),distance*trace.fraction-40.0f));
            qa_vec3 below=best;below.z-=800.0f;
            SOURCE_CALL(qa_bot_navigation_trace(nav,best,below,NULL,s->view.actor,1,&trace,e));
            if(!trace.start_solid && !trace.all_solid) {
                qa_vec3 point=trace.end;point.z+=1.0f;int32_t contents;
                SOURCE_CALL(qa_bot_navigation_contents(nav,point,&contents,e));
                if(!(contents&(8|16))) {*out=best;return true;}
            }
        }
        *out=best;
    }
    return true;
}

static bool persistent_equipment(bot_ai_state *s) {
    if(s->team_arena && !bot_ai_inventory_value(s,41) && !bot_ai_inventory_value(s,42) &&
       !bot_ai_inventory_value(s,43) && !bot_ai_inventory_value(s,44)) return false;
    if(bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<60 ||
       (bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<80 && bot_ai_inventory_value(s,QA_BOT_INV_ARMOR)<40)) return false;
    static const struct {uint8_t weapon,ammo,minimum;} equipment[]={
        {13,25,7},{10,24,5},{9,22,50},{8,23,5},{15,26,5},{16,27,5},{17,28,40},{11,21,20}};
    for(size_t i=0;i<sizeof(equipment)/sizeof(*equipment);++i)
        if(bot_ai_inventory_value(s,equipment[i].weapon)>0 &&
           bot_ai_inventory_value(s,equipment[i].ammo)>equipment[i].minimum) return true;
    return false;
}
static bool weakness(qa_bots *b,bot_ai_state *s,float *out,qa_error *e) {
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,e)) return false;
    if(!alive(b,s)) {b->services.arsenal_end(b->services.context,lease);return true;}
    qa_bot_weapon_tactics tactics=qa_bot_weapon_tactics_for(NULL);
    for(size_t i=0;i<count;++i) if(weapons[i].weapon.number==s->view.weapon) {
        tactics=qa_bot_weapon_tactics_for(weapons+i);break;
    }
    b->services.arsenal_end(b->services.context,lease);
    if(!alive(b,s)) return true;
    *out=tactics.melee || bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<40?100:
        tactics.weakness>0?tactics.weakness:bot_ai_inventory_value(s,QA_BOT_INV_HEALTH)<60?80:0;
    return true;
}

bool bot_ai_source_wants_camp(qa_bots *b,bot_ai_state *s,bool *accepted,qa_error *e) {
    *accepted=false;if(!alive(b,s)) return true;
    float camper;
    SOURCE_CALL(bot_ai_character_float(b,s,BOT_C_CAMPER,0,1,&camper,e));
    if(camper<.1f) return true;
    switch(s->long_term_goal) {
    case BOT_LTG_TEAM_HELP:case BOT_LTG_TEAM_ACCOMPANY:case BOT_LTG_DEFEND:
    case BOT_LTG_GET_FLAG:case BOT_LTG_RUSH_BASE:case BOT_LTG_CAMP:
    case BOT_LTG_CAMP_ORDER:case BOT_LTG_PATROL:return true;
    default:break;
    }
    if(s->source_goal.camp_time>(b->time-60.0f)+300.0f*(1.0f-camper)) return true;
    float random;
    SOURCE_CALL(bot_ai_random(b,&random,e));
    if(random>camper) {s->source_goal.camp_time=b->time;return true;}
    const qa_bot_weapon_knowledge *weapons;size_t count;void *lease;
    if(!b->services.arsenal(b->services.context,s->view.actor,&weapons,&count,&lease,e)) return false;
    float aggression=alive(b,s)?qa_bot_knowledge_aggression(weapons,count,s->view.weapon,bot_ai_inventory(s)):0;
    b->services.arsenal_end(b->services.context,lease);
    if(!alive(b,s) || aggression<50) return true;
    /* Source ai_dmq3.c indexes inventory[INVENTORY_ROCKETS < 10]. */
    if((bot_ai_inventory_value(s,QA_BOT_INV_ROCKET)<=0 || bot_ai_inventory_value(s,0)!=0) &&
       (bot_ai_inventory_value(s,QA_BOT_INV_RAIL)<=0 || bot_ai_inventory_value(s,QA_BOT_INV_SLUGS)<10) &&
       (bot_ai_inventory_value(s,QA_BOT_INV_BFG)<=0 || bot_ai_inventory_value(s,QA_BOT_INV_BFG_AMMO)<10)) return true;
    qa_bot_navigation *nav=navigation(b,s);
    const qa_nav_graph_view *graph=nav?qa_navigation_graph(qa_bot_navigation_runtime(nav)):NULL;
    if(!graph || !graph->node_count) return true;
    uint32_t best_time=99999;qa_bot_goal best_goal={0};
    int32_t cursor=0,next;
    for(;;) {
        qa_bot_goal spot;
        SOURCE_CALL(qa_bot_goals_camp(goals(b),cursor,&spot,&next,e));
        if(!next) break;
        uint32_t time;
        SOURCE_CALL(travel_time(b,s,&spot,SOURCE_DEFAULT_TRAVEL,&time,e));
        if(time && time<best_time) {best_time=time;best_goal=spot;}
        cursor=next;
    }
    if(best_time>150) return true;
    int32_t self;
    SOURCE_CALL(bot_ai_source_client(b,s,&self,e));
    s->decisionmaker=self;bot_ai_team_message_time_set(s,0);s->long_term_goal=BOT_LTG_CAMP;
    s->team_goal=best_goal;
    SOURCE_CALL(bot_ai_character_float(b,s,BOT_C_CAMPER,0,1,&camper,e));
    if(camper>.99f) bot_ai_team_goal_time_set(s,b->time+99999.0f);
    else {
        SOURCE_CALL(bot_ai_random(b,&random,e));
        bot_ai_team_goal_time_set(s,((b->time+120.0f)+180.0f*camper)+random*15.0f);
    }
    s->source_goal.camp_time=b->time;s->teammate=0;bot_ai_arrive_time_set(s,1);
    s->ordered=false;*accepted=true;return true;
}

static bool companion_chat(qa_bots *b,bot_ai_state *s,const char *type,int32_t client,
                            int32_t recipient,qa_error *e) {
    char name[36];
    SOURCE_CALL(companion_name(b,client,name,e));
    return chat(b,s,type,name,recipient,QA_BOT_CHAT_TELL,e);
}
static bool companion_acknowledge(qa_bots *b,bot_ai_state *s,const char *type,qa_error *e) {
    char name[36];
    SOURCE_CALL(companion_name(b,s->teammate,name,e));
    return acknowledge(b,s,type,name,e);
}
static bool roam_view(qa_bots *b,bot_ai_state *s,qa_error *e) {
    qa_vec3 target;
    SOURCE_CALL(bot_ai_source_roam_goal(b,s,&target,e));
    look_at(s,target);return true;
}
static bool accompany(qa_bots *b,bot_ai_state *s,qa_bot_goal *out,bool *found,qa_error *e) {
    if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
        SOURCE_CALL(companion_acknowledge(b,s,"accompany_start",e));
    if(bot_ai_team_goal_time(s)<b->time) {
        SOURCE_CALL(companion_chat(b,s,"accompany_stop",s->teammate,s->teammate,e));
        s->long_term_goal=BOT_LTG_NONE;
    }
    qa_bot_entity_info info;
    SOURCE_CALL(observation(b,s->teammate,&info,e));
    float visibility;
    SOURCE_CALL(bot_ai_source_entity_visible(b,s,s->teammate,&visibility,e));
    if(visibility!=0) {
        bot_ai_teammate_visible_time_set(s,b->time);
        qa_vec3 direction=qa_vec_sub(info.state.origin,s->player.origin);
        if(qa_vec_dot(direction,direction)<bot_ai_formation_distance(s)*bot_ai_formation_distance(s)) {
            qa_bot_entity_info self;
            SOURCE_CALL(observation(b,s->view.entity,&self,e));
            const qa_bot_entity_update *a=&self.state,*c=&info.state;
            if(a->origin.z+a->maxs.z>c->origin.z+c->mins.z &&
               a->origin.x+a->maxs.x>(c->origin.x+c->mins.x)-4.0f &&
               a->origin.x+a->mins.x<(c->origin.x+c->maxs.x)+4.0f &&
               a->origin.y+a->maxs.y>(c->origin.y+c->mins.y)-4.0f &&
               a->origin.y+a->mins.y<(c->origin.y+c->maxs.y)+4.0f &&
               a->origin.z+a->maxs.z>(c->origin.z+c->mins.z)-4.0f &&
               a->origin.z+a->mins.z<(c->origin.z+c->maxs.z)+4.0f) {
                qa_vec3 forward;
                qa_builtin_angle_vectors(c->angles,&forward,NULL,NULL);forward.z=0;
                qa_vec3 horizontal=qa_vec_normalize(forward);
                qa_vec3 away=qa_vec_normalize(qa_vec_sub(s->player.origin,c->origin));
                if(qa_vec_dot(horizontal,away)>.7f) {
                    bool moved;
                    SOURCE_CALL(bot_ai_move_setup(b,s,e));
                    SOURCE_CALL(qa_bot_moves_direction(qa_bot_runtime_moves(b->runtime),s->movement,
                        away,400,QA_BOT_DIRECTION_WALK,&moved,e));
                }
            }
            SOURCE_CALL(crouch(b,s,e));
            bool in_water;
            SOURCE_CALL(swimming(b,s,&in_water,e));
            if(in_water) bot_ai_attack_crouch_time_set(s,b->time-1.0f);
            if(bot_ai_arrive_time(s)<b->time-2.0f) {
                if(!bot_ai_arrive_time(s)) {
                    SOURCE_CALL(action(b,s,QA_BOT_GESTURE,e));
                    SOURCE_CALL(companion_chat(b,s,"accompany_arrive",s->teammate,s->teammate,e));
                    bot_ai_arrive_time_set(s,b->time);
                } else if(bot_ai_attack_crouch_time(s)>b->time) SOURCE_CALL(action(b,s,QA_BOT_CROUCH,e));
                else {
                    float random;
                    SOURCE_CALL(bot_ai_random(b,&random,e));
                    if(random<s->view.think_time*.05f) SOURCE_CALL(action(b,s,QA_BOT_GESTURE,e));
                }
            }
            if(bot_ai_arrive_time(s)>b->time-2.0f) look_at(s,info.state.origin);
            else {
                float random;
                SOURCE_CALL(bot_ai_random(b,&random,e));
                if(random<s->view.think_time*.8f) SOURCE_CALL(roam_view(b,s,e));
            }
            bool air;
            SOURCE_CALL(bot_ai_source_go_for_air(b,s,&s->team_goal,400,&air,e));
            if(air) {
                SOURCE_CALL(qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime),s->movement,true,e));
                bot_ai_nearby_until_set(s,b->time+8.0f);s->view.decision=QA_BOT_SEEK_NEARBY;s->state_time=b->time;
                return true;
            }
            return reset_avoid(b,s,e);
        }
    }
    if(info.valid) SOURCE_CALL(companion_goal(b,s,&s->team_goal,s->teammate,info.state.origin,e));
    *out=s->team_goal;*found=true;
    if(bot_ai_teammate_visible_time(s)<b->time-60.0f) {
        SOURCE_CALL(companion_chat(b,s,"accompany_cannotfind",s->teammate,s->teammate,e));
        s->long_term_goal=BOT_LTG_NONE;bot_ai_teammate_visible_time_set(s,b->time);
    }
    return true;
}

static bool patrol(qa_bots *b,bot_ai_state *s,qa_bot_goal *out,bool *found,qa_error *e) {
    bot_source_order_state *order=&s->source_order;
    if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time) {
        char route[BOT_SOURCE_WAYPOINTS*36];size_t at=0;
        for(int32_t point=order->patrol_points;point>=0;point=b->source_orders.points[point].next) {
            const bot_source_waypoint *waypoint=&b->source_orders.points[point];
            size_t length=strlen(waypoint->name);memcpy(route+at,waypoint->name,length);at+=length;
            if(waypoint->next>=0) {memcpy(route+at," to ",4);at+=4;}
        }
        route[at]=0;
        SOURCE_CALL(acknowledge(b,s,"patrol_start",route,e));
    }
    int32_t point=order->current_patrol_point;
    if(point<0) {s->long_term_goal=BOT_LTG_NONE;return true;}
    bot_source_waypoint *current=&b->source_orders.points[point];
    if(qa_bot_goal_touching(s->player.origin,&current->goal)) {
        if(order->patrol_flags&4) {
            if(current->prev>=0) order->current_patrol_point=current->prev;
            else {order->current_patrol_point=current->next;order->patrol_flags&=~4;}
        } else if(current->next>=0) order->current_patrol_point=current->next;
        else {order->current_patrol_point=current->prev;order->patrol_flags|=4;}
    }
    if(bot_ai_team_goal_time(s)<b->time) {
        SOURCE_CALL(chat(b,s,"patrol_stop",NULL,s->decisionmaker,QA_BOT_CHAT_TELL,e));
        s->long_term_goal=BOT_LTG_NONE;
    }
    if(order->current_patrol_point<0) {s->long_term_goal=BOT_LTG_NONE;return true;}
    *out=b->source_orders.points[order->current_patrol_point].goal;*found=true;return true;
}

static bool camp(qa_bots *b,bot_ai_state *s,qa_bot_goal *out,bool *found,qa_error *e) {
    if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time) {
        if(s->long_term_goal==BOT_LTG_CAMP_ORDER) SOURCE_CALL(companion_acknowledge(b,s,"camp_start",e));
        bot_ai_team_message_time_set(s,0);
    }
    *out=s->team_goal;
    if(bot_ai_team_goal_time(s)<b->time) {
        if(s->long_term_goal==BOT_LTG_CAMP_ORDER)
            SOURCE_CALL(chat(b,s,"camp_stop",NULL,s->decisionmaker,QA_BOT_CHAT_TELL,e));
        s->long_term_goal=BOT_LTG_NONE;
    }
    qa_vec3 direction=qa_vec_sub(out->origin,s->player.origin);
    if(!(qa_vec_dot(direction,direction)<60.0f*60.0f)) {*found=true;return true;}
    if(!bot_ai_arrive_time(s)) {
        if(s->long_term_goal==BOT_LTG_CAMP_ORDER) {
            SOURCE_CALL(companion_chat(b,s,"camp_arrive",s->teammate,s->decisionmaker,e));
            SOURCE_CALL(bot_ai_source_voice(b,s,s->decisionmaker,"inposition",true,e));
        }
        bot_ai_arrive_time_set(s,b->time);
    }
    float random;
    SOURCE_CALL(bot_ai_random(b,&random,e));
    if(random<s->view.think_time*.8f) SOURCE_CALL(roam_view(b,s,e));
    SOURCE_CALL(crouch(b,s,e));
    if(bot_ai_attack_crouch_time(s)>b->time) SOURCE_CALL(action(b,s,QA_BOT_CROUCH,e));
    bool in_water;
    SOURCE_CALL(swimming(b,s,&in_water,e));
    if(in_water) bot_ai_attack_crouch_time_set(s,b->time-1.0f);
    int32_t point_contents;
    SOURCE_CALL(contents(b,s->player.eye,bot_ai_source_actor(b,s->view.entity),&point_contents,e));
    if(point_contents&SOURCE_LIQUID) {
        if(s->long_term_goal==BOT_LTG_CAMP_ORDER) {
            SOURCE_CALL(chat(b,s,"camp_stop",NULL,s->decisionmaker,QA_BOT_CHAT_TELL,e));
            int32_t last_type;
            SOURCE_CALL(bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,false,e));
            if(last_type==BOT_LTG_CAMP_ORDER) {
                last_type=BOT_LTG_NONE;
                SOURCE_CALL(bot_ai_storage_i32(b,s,QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE,&last_type,true,e));
            }
        }
        s->long_term_goal=BOT_LTG_NONE;
    }
    return reset_avoid(b,s,e);
}

static bool carrying_flag(const bot_ai_state *s,bool neutral) {
    return neutral?bot_ai_inventory_value(s,QA_BOT_INV_NEUTRAL_FLAG)>0:
        bot_ai_inventory_value(s,QA_BOT_INV_RED_FLAG)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_FLAG)>0;
}
static bool carrying_cubes(const bot_ai_state *s) {
    return bot_ai_inventory_value(s,QA_BOT_INV_RED_CUBE)>0 || bot_ai_inventory_value(s,QA_BOT_INV_BLUE_CUBE)>0;
}
static bool objective(qa_bots *b,bot_ai_state *s,qa_bot_goal *out,bool *found,qa_error *e) {
    int32_t mode=b->source_goals.game_type,type=s->long_term_goal;
    if(mode==4 && (type==BOT_LTG_GET_FLAG || type==BOT_LTG_RETURN_FLAG ||
                    (type==BOT_LTG_RUSH_BASE && bot_ai_rush_base_away_time(s)<b->time))) {
        if(type!=BOT_LTG_RUSH_BASE && bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
            SOURCE_CALL(team_chat(b,s,type==BOT_LTG_GET_FLAG?"captureflag_start":"returnflag_start",
                type==BOT_LTG_GET_FLAG?"ongetflag":"onreturnflag",e));
        SOURCE_CALL(team_base(b,s,out,type!=BOT_LTG_RUSH_BASE,false,found,e));
        if(!*found) {s->long_term_goal=BOT_LTG_NONE;return true;}
        if(type==BOT_LTG_RUSH_BASE && !carrying_flag(s,false)) s->long_term_goal=BOT_LTG_NONE;
        if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
        if(qa_bot_goal_touching(s->player.origin,out)) {
            if(type==BOT_LTG_RUSH_BASE && carrying_flag(s,false)) {
                SOURCE_CALL(reset_avoid(b,s,e));float random;
                SOURCE_CALL(bot_ai_random(b,&random,e));
                bot_ai_rush_base_away_time_set(s,(b->time+5.0f)+10.0f*random);
            } else {
                if(type==BOT_LTG_GET_FLAG) {
                    int32_t self,team;
                    SOURCE_CALL(bot_ai_source_client(b,s,&self,e));
                    SOURCE_CALL(bot_ai_source_team(b,self,&team,e));
                    if(team==1) s->source_order.blue_flag_status=1;
                    else if(team==2) s->source_order.red_flag_status=1;
                }
                s->long_term_goal=BOT_LTG_NONE;
            }
        }
        return bot_ai_source_route_goal(b,s,out,e);
    }
    if(!s->team_arena) return bot_ai_source_item_goal(b,s,out,found,e);
    if(mode==5) {
        if(type==BOT_LTG_GET_FLAG) {
            if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
                SOURCE_CALL(team_chat(b,s,"captureflag_start","ongetflag",e));
            *out=b->source_goals.neutral_flag;*found=true;
            if(qa_bot_goal_touching(s->player.origin,out) || bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
            return true;
        }
        if(type==BOT_LTG_RUSH_BASE) {
            SOURCE_CALL(team_base(b,s,out,true,false,found,e));
            if(!*found) {s->long_term_goal=BOT_LTG_NONE;return true;}
            if(!carrying_flag(s,true) || bot_ai_team_goal_time(s)<b->time || qa_bot_goal_touching(s->player.origin,out))
                s->long_term_goal=BOT_LTG_NONE;
            return bot_ai_source_route_goal(b,s,out,e);
        }
        if(type==BOT_LTG_RETURN_FLAG) {
            if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
                SOURCE_CALL(team_chat(b,s,"returnflag_start","onreturnflag",e));
            if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
            return bot_ai_source_item_goal(b,s,out,found,e);
        }
    }
    if(mode==7 && type==BOT_LTG_RUSH_BASE) {
        SOURCE_CALL(team_base(b,s,out,true,true,found,e));
        if(!*found || !carrying_cubes(s) || bot_ai_team_goal_time(s)<b->time || qa_bot_goal_touching(s->player.origin,out)) {
            *found=false;return bot_ai_source_go_harvest(b,s,e);
        }
        return bot_ai_source_route_goal(b,s,out,e);
    }
    if((mode==5 || mode==6 || mode==7) && type==BOT_LTG_ATTACK_BASE && bot_ai_attack_away_time(s)<b->time) {
        if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
            SOURCE_CALL(team_chat(b,s,"attackenemybase_start","onoffense",e));
        SOURCE_CALL(team_base(b,s,out,true,mode!=5,found,e));
        if(!*found) {s->long_term_goal=BOT_LTG_NONE;return true;}
        if(mode==6) {
            float feeling;
            SOURCE_CALL(weakness(b,s,&feeling,e));
            if(feeling>50) return bot_ai_source_item_goal(b,s,out,found,e);
            if(qa_bot_goal_touching(s->player.origin,out)) {
                float random;SOURCE_CALL(bot_ai_random(b,&random,e));
                bot_ai_attack_away_time_set(s,(b->time+3.0f)+5.0f*random);
            }
            qa_vec3 direction=qa_vec_sub(s->player.origin,out->origin);
            if(qa_vec_dot(direction,direction)<60.0f*60.0f) {
                float random;SOURCE_CALL(bot_ai_random(b,&random,e));
                bot_ai_attack_away_time_set(s,(b->time+3.0f)+5.0f*random);
            }
            if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
            return bot_ai_source_route_goal(b,s,out,e);
        }
        if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
        if(qa_bot_goal_touching(s->player.origin,out)) {
            float random;SOURCE_CALL(bot_ai_random(b,&random,e));
            bot_ai_attack_away_time_set(s,(b->time+2.0f)+5.0f*random);
        }
        return true;
    }
    if(mode==7 && type==BOT_LTG_HARVEST && bot_ai_harvest_away_time(s)<b->time) {
        if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
            SOURCE_CALL(team_chat(b,s,"harvest_start","onoffense",e));
        *out=b->source_goals.neutral_obelisk;*found=true;
        if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
        if(qa_bot_goal_touching(s->player.origin,out)) {
            float random;SOURCE_CALL(bot_ai_random(b,&random,e));
            bot_ai_harvest_away_time_set(s,(b->time+4.0f)+3.0f*random);
        }
        return true;
    }
    return bot_ai_source_item_goal(b,s,out,found,e);
}

static bool get_long_term_goal(qa_bots *b,bot_ai_state *s,bool retreat,
                               qa_bot_goal *out,bool *found,qa_error *e) {
    int32_t type=s->long_term_goal;
    if(type==BOT_LTG_TEAM_HELP && !retreat) {
        if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
            SOURCE_CALL(companion_acknowledge(b,s,"help_start",e));
        if(bot_ai_team_goal_time(s)<b->time || bot_ai_teammate_visible_time(s)<b->time-10.0f) s->long_term_goal=BOT_LTG_NONE;
        qa_bot_entity_info info;float visibility;
        SOURCE_CALL(observation(b,s->teammate,&info,e));
        SOURCE_CALL(bot_ai_source_entity_visible(b,s,s->teammate,&visibility,e));
        if(visibility!=0) {
            qa_vec3 direction=qa_vec_sub(info.state.origin,s->player.origin);
            if(qa_vec_dot(direction,direction)<100.0f*100.0f) return reset_avoid(b,s,e);
        } else bot_ai_teammate_visible_time_set(s,b->time);
        if(info.valid) SOURCE_CALL(companion_goal(b,s,&s->team_goal,s->teammate,info.state.origin,e));
        *out=s->team_goal;*found=true;return true;
    }
    if(type==BOT_LTG_TEAM_ACCOMPANY && !retreat) return accompany(b,s,out,found,e);
    if(type==BOT_LTG_DEFEND) {
        uint32_t time;
        SOURCE_CALL(travel_time(b,s,&s->team_goal,SOURCE_DEFAULT_TRAVEL,&time,e));
        if((float)time>s->source_goal.defend_away_range) bot_ai_defend_away_time_set(s,0);
        if(!retreat && bot_ai_defend_away_time(s)<b->time) {
            const char *name;
            SOURCE_CALL(qa_bot_goals_name_read(goals(b),s->team_goal.number,&name,e));
            if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time) {
                SOURCE_CALL(chat(b,s,"defend_start",name,0,QA_BOT_CHAT_TEAM,e));
                SOURCE_CALL(bot_ai_source_voice(b,s,-1,"ondefense",true,e));
                bot_ai_team_message_time_set(s,0);
            }
            *out=s->team_goal;*found=true;
            if(bot_ai_team_goal_time(s)<b->time) {
                SOURCE_CALL(chat(b,s,"defend_stop",name,0,QA_BOT_CHAT_TEAM,e));
                s->long_term_goal=BOT_LTG_NONE;
            }
            qa_vec3 direction=qa_vec_sub(out->origin,s->player.origin);
            if(qa_vec_dot(direction,direction)<70.0f*70.0f) {
                SOURCE_CALL(reset_avoid(b,s,e));float random;
                SOURCE_CALL(bot_ai_random(b,&random,e));
                bot_ai_defend_away_time_set(s,(b->time+3.0f)+3.0f*random);
                s->source_goal.defend_away_range=persistent_equipment(s)?100:350;
            }
            return true;
        }
    }
    if(type==BOT_LTG_KILL && !retreat) {
        if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time) {
            SOURCE_CALL(companion_chat(b,s,"kill_start",s->team_goal.entity,s->decisionmaker,e));
            bot_ai_team_message_time_set(s,0);
        }
        if(s->source_events.last_killed_player==s->team_goal.entity) {
            SOURCE_CALL(companion_chat(b,s,"kill_done",s->team_goal.entity,s->decisionmaker,e));
            s->source_events.last_killed_player=-1;s->long_term_goal=BOT_LTG_NONE;
        }
        if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
        return bot_ai_source_item_goal(b,s,out,found,e);
    }
    if(type==BOT_LTG_GET_ITEM && !retreat) {
        const char *name;
        SOURCE_CALL(qa_bot_goals_name_read(goals(b),s->team_goal.number,&name,e));
        if(bot_ai_team_message_time(s) && bot_ai_team_message_time(s)<b->time)
            SOURCE_CALL(acknowledge(b,s,"getitem_start",name,e));
        *out=s->team_goal;
        if(bot_ai_team_goal_time(s)<b->time) s->long_term_goal=BOT_LTG_NONE;
        qa_bot_source_goal_status status;
        SOURCE_CALL(qa_bot_goals_source_status(goals(b),(int32_t)s->view.client,out,&status,e));
        if(status==QA_BOT_GOAL_UNAVAILABLE) {s->long_term_goal=BOT_LTG_NONE;return true;}
        *found=true;if(status==QA_BOT_GOAL_AVAILABLE) return true;
        bool missing;
        SOURCE_CALL(qa_bot_goals_missing_visible(goals(b),(int32_t)s->view.client,s->player.eye,out,&missing,e));
        if(missing) {
            SOURCE_CALL(chat(b,s,"getitem_notthere",name,s->decisionmaker,QA_BOT_CHAT_TELL,e));
            s->long_term_goal=BOT_LTG_NONE;
        } else {
            bool reached;
            SOURCE_CALL(bot_ai_source_reached_goal(b,s,out,&reached,e));
            if(reached) {
                SOURCE_CALL(chat(b,s,"getitem_gotit",name,s->decisionmaker,QA_BOT_CHAT_TELL,e));
                s->long_term_goal=BOT_LTG_NONE;
            }
        }
        return true;
    }
    if((type==BOT_LTG_CAMP || type==BOT_LTG_CAMP_ORDER) && !retreat) return camp(b,s,out,found,e);
    if(type==BOT_LTG_PATROL && !retreat) return patrol(b,s,out,found,e);
    return objective(b,s,out,found,e);
}

static bool long_term_goal(qa_bots *b,bot_ai_state *s,bool retreat,
                            qa_bot_goal *out,bool *found,qa_error *e) {
    *found=false;if(!alive(b,s)) return true;
    bot_source_order_state *order=&s->source_order;
    if(bot_ai_lead_time(s)>0 && !retreat) {
        if(bot_ai_lead_time(s)<b->time) {
            SOURCE_CALL(companion_chat(b,s,"lead_stop",order->lead_teammate,s->teammate,e));
            bot_ai_lead_time_set(s,0);return get_long_term_goal(b,s,retreat,out,found,e);
        }
        if(order->lead_message_time<0 && -order->lead_message_time<b->time) {
            SOURCE_CALL(companion_chat(b,s,"followme",order->lead_teammate,s->teammate,e));
            order->lead_message_time=b->time;
        }
        qa_bot_entity_info info;
        SOURCE_CALL(observation(b,order->lead_teammate,&info,e));
        if(info.valid) SOURCE_CALL(companion_goal(b,s,&order->lead_goal,order->lead_teammate,info.state.origin,e));
        float visibility;
        SOURCE_CALL(bot_ai_source_entity_visible(b,s,order->lead_teammate,&visibility,e));
        if(visibility!=0) order->lead_visible_time=b->time;
        if(order->lead_visible_time<b->time-1.0f) order->lead_backup_time=b->time+2.0f;
        qa_vec3 direction=qa_vec_sub(s->player.origin,order->lead_goal.origin);
        float distance=qa_vec_dot(direction,direction);
        if(order->lead_backup_time>b->time) {
            if(order->lead_message_time<b->time-20.0f) {
                SOURCE_CALL(companion_chat(b,s,"followme",order->lead_teammate,s->teammate,e));
                order->lead_message_time=b->time;
            }
            if(distance<100.0f*100.0f) order->lead_backup_time=0;
            *out=order->lead_goal;*found=true;return true;
        }
        if(distance>500.0f*500.0f) {
            if(order->lead_message_time<b->time-20.0f) {
                SOURCE_CALL(companion_chat(b,s,"followme",order->lead_teammate,s->teammate,e));
                order->lead_message_time=b->time;
            }
            look_at(s,info.state.origin);return true;
        }
    }
    return get_long_term_goal(b,s,retreat,out,found,e);
}

bool bot_ai_source_long_term_goal(qa_bots *b,bot_ai_state *s,bool retreat,
                                   qa_bot_goal *out,bool *found,qa_error *e) {
    bool result=long_term_goal(b,s,retreat,out,found,e);
    if(!alive(b,s)) *found=false;
    return result;
}

#undef SOURCE_CALL
