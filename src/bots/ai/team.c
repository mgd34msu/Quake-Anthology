#include "internal.h"

static bool same_mode(qa_mode_id a, qa_mode_id b) {
    return a.slot==b.slot && a.generation==b.generation;
}
bool bot_ai_carrying(qa_bots *b,bot_ai_state *s,bool *carrying,qa_error *e) {
    *carrying=s->player.inventory[QA_BOT_INV_RED_FLAG]>0 || s->player.inventory[QA_BOT_INV_BLUE_FLAG]>0 ||
        s->player.inventory[QA_BOT_INV_NEUTRAL_FLAG]>0 || s->player.inventory[QA_BOT_INV_RED_CUBE]>0 ||
        s->player.inventory[QA_BOT_INV_BLUE_CUBE]>0;
    if(!b->services.modes || !s->view.mode.generation) return true;
    *carrying=false;
    qa_mode_statistics statistics;
    if(!qa_modes_statistics(b->services.modes,s->view.mode,s->view.actor,&statistics,e)) return false;
    if(statistics.tokens>0) *carrying=true;
    const qa_actor_registry *actors=qa_session_actors(b->services.shared.session);
    uint32_t cursor=0;const qa_actor_record *record;
    while(!*carrying && qa_actors_next(actors,&cursor,&record)) {
        qa_mode_object_view object;
        if(qa_modes_object_read(b->services.modes,record->id,&object) && same_mode(object.mode,s->view.mode) &&
           object.kind==QA_MODE_OBJECT_FLAG && object.phase==QA_OBJECTIVE_CARRIED &&
           qa_actor_id_equal(object.carrier,s->view.actor))
            *carrying=true;
    }
    return true;
}
static bool point_goal(qa_bots *b, bot_ai_state *s, qa_vec3 origin,
                         int32_t entity, qa_bot_goal *goal, bool *found, qa_error *e) {
    uint32_t area;
    if(!bot_ai_point_area(b,s,origin,&area,e)) return false;
    *found=area!=0;
    if(*found) *goal=(qa_bot_goal){.origin=origin,.area=(int32_t)area,.entity=entity,
        .mins=qa_v3(-8,-8,-8),.maxs=qa_v3(8,8,8)};
    return true;
}
bool bot_ai_team_goal(qa_bots *b, bot_ai_state *s, qa_bot_goal *goal, bool *found, qa_error *e) {
    *found=false;
    if(!b->services.modes || !s->view.mode.generation) return true;
    qa_mode_view mode;qa_team_id team;
    if(!qa_modes_read(b->services.modes,s->view.mode,&mode,e) ||
       !qa_modes_team(b->services.modes,s->view.mode,s->view.actor,&team,e)) return false;
    if(s->team_task_until && s->team_task_until<b->time) s->team_task=BOT_TEAM_NONE;
    if(s->team_task==BOT_TEAM_CAMP) return point_goal(b,s,s->camp_origin,-1,goal,found,e);
    if(mode.rules.kind!=QA_MODE_CTF && mode.rules.kind!=QA_MODE_ONE_FLAG &&
       mode.rules.kind!=QA_MODE_OVERLOAD && mode.rules.kind!=QA_MODE_HARVESTER &&
       mode.rules.kind!=QA_MODE_TAG && mode.rules.kind!=QA_MODE_DEATHBALL) return true;
    bool carrying=s->player.carrying_objective;
    float best=INFINITY;
    for(size_t i=0;i<b->entities.count;++i) {
        qa_actor_id actor=b->entities.ids[i];qa_mode_object_view object;
        if(!bot_ai_live(b,actor) || !qa_modes_object_read(b->services.modes,actor,&object) ||
           !same_mode(object.mode,s->view.mode) || object.phase==QA_OBJECTIVE_DISABLED) continue;
        bool own=object.team && object.team==team;
        bool home=false;
        int priority=0;
        if(mode.rules.kind==QA_MODE_CTF || mode.rules.kind==QA_MODE_ONE_FLAG) {
            bool flag=object.kind==QA_MODE_OBJECT_FLAG;
            bool base=object.kind==QA_MODE_OBJECT_FLAG_BASE;
            if(carrying) {
                bool destination=mode.rules.kind==QA_MODE_ONE_FLAG?!own:own;
                if(destination && (base || flag)) {priority=5;home=true;}
            } else if(flag && own && object.phase==QA_OBJECTIVE_DROPPED) priority=5;
            else if(flag && !own && object.visible && object.phase!=QA_OBJECTIVE_CARRIED &&
                    s->team_task!=BOT_TEAM_DEFENSE && s->team_task!=BOT_TEAM_RETURN) priority=3;
            else if(flag && object.phase==QA_OBJECTIVE_CARRIED && bot_ai_live(b,object.carrier)) {
                bool teammate;
                if(!bot_ai_same_team(b,s,object.carrier,&teammate,e)) return false;
                if(teammate && (s->team_task==BOT_TEAM_ESCORT || s->team_task==BOT_TEAM_DEFENSE)) {
                    actor=object.carrier;priority=4;
                } else if(!teammate && own) {actor=object.carrier;priority=5;}
            } else if(own && (base || flag) && s->team_task==BOT_TEAM_DEFENSE) {priority=2;home=true;}
        } else if(mode.rules.kind==QA_MODE_OVERLOAD || mode.rules.kind==QA_MODE_HARVESTER) {
            if(object.kind==QA_MODE_OBJECT_OBELISK) {
                bool defend=s->team_task==BOT_TEAM_DEFENSE;
                if(own==defend) priority=carrying?5:2;
            } else if(object.kind==QA_MODE_OBJECT_CUBE && !own && object.visible && !carrying) priority=3;
        } else if(object.kind==QA_MODE_OBJECT_TAG) {
            if(object.phase==QA_OBJECTIVE_CARRIED) {
                if(qa_actor_id_equal(object.carrier,s->view.actor) || !bot_ai_live(b,object.carrier)) continue;
                actor=object.carrier;
            }
            priority=3;
        } else if(object.kind==QA_MODE_OBJECT_BALL && object.visible) priority=3;
        else if(object.kind==QA_MODE_OBJECT_GOAL && carrying && !own) priority=5;
        if(!priority) continue;
        qa_body_state body;
        if(home) {
            if(!qa_modes_object_home(b->services.modes,actor,&body.origin,&body.bounds,e)) return false;
        } else if(!qa_world_body_read(b->services.shared.world,actor,&body,e)) return false;
        if(s->retired || !bot_ai_live(b,s->view.actor)) return true;
        if(!bot_ai_live(b,actor)) continue;
        float score=qa_vec_length(qa_vec_sub(body.origin,s->player.origin))-(float)priority*10000;
        if(score>=best) continue;
        qa_bot_entity observed;
        if(!b->services.entity(b->services.context,actor,&observed,e)) return false;
        if(!bot_ai_live(b,actor) || !observed.present) continue;
        bool reachable;
        if(!point_goal(b,s,body.origin,observed.number,goal,&reachable,e)) return false;
        if(reachable) {best=score;*found=true;}
    }
    return true;
}
static bool command_word(const char *text,const char *word) {
    while(*text && *word) {
        unsigned char c=(unsigned char)*text++;
        if(c>='A' && c<='Z') c+='a'-'A';
        if(c!=(unsigned char)*word++) return false;
    }
    return !*text && !*word;
}
static int32_t token_number(const char **text) {
    while(**text && (unsigned char)**text<=32) ++*text;
    const char *start=*text;
    while(**text && (unsigned char)**text>32) ++*text;
    char number[32];size_t size=(size_t)(*text-start);
    if(size>=sizeof(number)) return -1;
    memcpy(number,start,size);number[size]=0;
    char *end;long parsed=strtol(number,&end,10);
    return end==number || parsed<INT32_MIN || parsed>INT32_MAX?-1:(int32_t)parsed;
}
bool bot_ai_voice(qa_bots *b, bot_ai_state *s, int32_t channel, const char *text, qa_error *e) {
    if(!text || !channel || !b->services.modes || !s->view.mode.generation) return true;
    token_number(&text);int32_t client=token_number(&text);token_number(&text);
    while(*text && (unsigned char)*text<=32) ++text;
    qa_actor_id requester={0};
    for(size_t i=0;i<b->players.count;++i) {
        qa_builtin_player_info info;qa_actor_id actor=b->players.ids[i];
        if(bot_ai_live(b,actor) && b->services.shared.player_info(b->services.shared.context,actor,&info) &&
           info.connected && info.slot==(uint32_t)client) {requester=actor;break;}
    }
    bool teammate;
    if(!requester.registry || !bot_ai_same_team(b,s,requester,&teammate,e)) return !requester.registry;
    if(!teammate) return true;
    if(command_word(text,"startleader")) {s->team_leader=requester;return true;}
    if(command_word(text,"stopleader")) {
        if(qa_actor_id_equal(s->team_leader,requester)) s->team_leader=(qa_actor_id){0};
        return true;
    }
    if(command_word(text,"patrol")) {
        s->team_task=BOT_TEAM_NONE;s->view.order=(qa_bot_order){0};return true;
    }
    if(command_word(text,"followme")) {
        s->view.order=(qa_bot_order){.kind=QA_BOT_ORDER_FOLLOW,.status=QA_BOT_ORDER_ACTIVE,.target=requester};
        s->team_task=BOT_TEAM_ESCORT;
    } else if(command_word(text,"followflagcarrier")) s->team_task=BOT_TEAM_ESCORT;
    else if(command_word(text,"getflag") || command_word(text,"offense")) s->team_task=BOT_TEAM_OFFENSE;
    else if(command_word(text,"defend") || command_word(text,"defendflag")) s->team_task=BOT_TEAM_DEFENSE;
    else if(command_word(text,"returnflag")) s->team_task=BOT_TEAM_RETURN;
    else if(command_word(text,"camp")) {
        qa_body_state body;
        if(!qa_world_body_read(b->services.shared.world,requester,&body,e)) return false;
        s->camp_origin=body.origin;s->team_task=BOT_TEAM_CAMP;
    } else if(command_word(text,"wantondefense") || command_word(text,"wantonoffense")) {
        bot_ai_state *other=bot_ai_actor(b,requester);
        if(other) other->team_task=command_word(text,"wantondefense")?BOT_TEAM_DEFENSE:BOT_TEAM_OFFENSE;
        return qa_bot_actions_add(qa_bot_runtime_actions(b->runtime),s->view.client,QA_BOT_AFFIRMATIVE,e);
    } else return true;
    s->team_requester=requester;s->team_task_until=b->time+120;
    s->activation_count=0;s->view.decision=QA_BOT_SEEK_LONG_TERM;s->long_term_until=0;
    return qa_bot_moves_reset_avoid(qa_bot_runtime_moves(b->runtime),s->movement,false,e);
}
