/* CG playerstate transitions, id Software 1999-2005, GPL-2.0-or-later. */
#include "player_state_internal.h"

static bool q3n_player_state_transition_received(q3n_player_state *,const q3n_frame *,
    const qa_q3_player *,const qa_q3_player *,const q3n_player_state_context *,
    qa_q3_entity,q3n_entity *,qa_q3_entity,q3n_entity *,int32_t,qa_error *);

static bool current(q3n_player_state *o,const q3n_frame *f,qa_error *e)
{
    return o && f && !o->busy && o->options.application==f->application &&
        o->options.assets==f->assets && o->options.seat==f->seat &&
        o->product==q3n_frame_product(f) && q3n_frame_predicted_player(f) &&
        (f->compiled?o->options.compiled_source==f->compiled->source.owner:
         f->remote?o->options.remote_client==f->remote->client && !o->options.compiled_source:
            !o->options.remote_client && !o->options.compiled_source && o->source_game==f->source.source_game && f->time==f->source.source_time_ms) &&
        q3ne_current(f,e)?true:
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 player transition requires its exact completed source and viewing seat");
}
bool q3n_player_state_create(const q3n_player_state_options *options,q3n_player_state **out,qa_error *e)
{
    if(!options || !out || *out || !options->application || !options->source || !options->assets || options->remote_client || options->compiled_source || !options->print ||
       !qa_application_native_q3_presentation_current(options->application,options->source))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 playerstate requires actual GAME, seat and registered assets");
    q3n_player_state *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating native Q3 playerstate");
    o->options=*options; o->options.source=NULL;
    o->source_game=options->source->source_game; o->product=options->source->product;
    *out=o; return true;
}
bool q3n_player_state_create_restored(const q3n_player_state_options *options,q3n_player_state **out,qa_error *e)
{
    qa_native_q3_client_basis basis;
    if(!options || !out || *out || !options->assets || options->remote_client || options->compiled_source || !options->print || !options->client ||
       !qa_native_q3_client_basis_read(options->client,&basis,e) || basis.application!=options->application || basis.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Restored Q3 playerstate requires its actual installed source and client basis");
    q3n_player_state *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating restored native Q3 playerstate");
    o->options=*options; o->options.source=NULL; o->source_game=basis.source_game; o->product=basis.product; *out=o; return true;
}
bool q3n_player_state_create_remote(const q3n_player_state_options *options,q3n_player_state **out,qa_error *e)
{
    qa_native_q3_remote_client_basis basis;
    if(!options || !out || *out || !options->assets || options->source || options->client || options->compiled_source || !options->print ||
       !options->remote_client || !qa_native_q3_remote_client_basis_read(options->remote_client,&basis,e) ||
       basis.application!=options->application || basis.client.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote Q3 playerstate requires its actual retained CLIENT basis");
    q3n_player_state *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating remote native Q3 playerstate");
    o->options=*options; o->product=basis.product; *out=o; return true;
}
bool q3n_player_state_create_compiled(const q3n_player_state_options *options,q3n_player_state **out,qa_error *e)
{
    q3n_compiled_source_view source;
    if(!options || !out || *out || !options->compiled_source || options->source || options->client || options->remote_client ||
       !options->print || !q3n_compiled_source_checkpoint_read(options->compiled_source,&source,e) ||
       source.basis.application!=options->application || source.basis.assets!=options->assets || source.basis.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled playerstate requires its actual CLIENT source and assets");
    q3n_player_state *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating compiled Q3 playerstate");
    o->options=*options; o->product=source.basis.product; *out=o; return true;
}
void q3n_player_state_destroy(q3n_player_state *o) { if(o && !o->busy)free(o); }
bool q3n_player_state_idle(const q3n_player_state *o) { return o && !o->busy; }
const q3n_player_feedback *q3n_player_state_feedback(const q3n_player_state *o) { return o?&o->feedback:NULL; }
static bool sound(const q3n_frame *f,q3n_sound s,int32_t channel,qa_error *e)
{ return q3ne_sound(f,q3n_media_read(f->media)->sounds[s],NULL,q3n_frame_predicted_player(f)->clientNum,channel,true,e); }
static bool buffered(const q3n_frame *f,q3n_sound s,qa_error *e)
{ return q3n_events_buffer(f->events,q3n_media_read(f->media)->sounds[s],e); }
static void remember(q3n_transition_history *h,const q3n_frame *f,const qa_q3_player *p,qa_actor_id followed)
{
    h->viewing_actor=f->remote?(qa_actor_id){0}:f->viewing_actor;
    h->viewing_client=f->viewing_client; h->followed_actor=followed;
    h->source_frame=f->compiled?f->compiled->revision:f->remote?f->remote->snapshots.revision:f->source.source_frame.number;
    h->source_time=f->time; h->client_num=p->clientNum;
    h->damage_event=p->damageEvent; h->viewheight=p->viewheight; h->external_event=p->externalEvent;
    h->event_sequence=p->eventSequence; h->health=p->stats[0]; h->e_flags=p->eFlags;
    memcpy(h->events,p->events,sizeof(h->events)); memcpy(h->persistant,p->persistant,sizeof(h->persistant));
    memcpy(h->powerups,p->powerups,sizeof(h->powerups)); h->valid=true;
}
static void respawn(q3n_player_state *o,const q3n_frame *f)
{ o->feedback.this_frame_teleport=true; q3n_weapons_set_selected(f->weapons,q3n_frame_snapshot_player(f)->weapon,f->time); }
static void damage(q3n_player_state *o,const q3n_frame *f,const qa_q3_player *p,const qa_q3_player *snapshot,int32_t server_time)
{
    q3n_player_feedback *g=&o->feedback;
    g->attacker_time=f->time;
    float scale=snapshot->stats[0]<40?1:q3ne_div(40,(float)snapshot->stats[0]);
    float kick=q3nh_clamp(q3ne_mul((float)p->damageCount,scale),5,10);
    if(p->damageYaw==255 && p->damagePitch==255) {
        g->damage_x=g->damage_y=g->damage_roll=0; g->damage_pitch=-kick;
    } else {
        qa_vec3 dir; q3nh_vectors(qa_v3(q3ne_mul(q3ne_div((float)p->damagePitch,255),360),
            q3ne_mul(q3ne_div((float)p->damageYaw,255),360),0),&dir,NULL,NULL);
        dir=q3ne_scale(dir,-1);
        float front=q3ne_dot(dir,f->refdef.axis[0]),left=q3ne_dot(dir,f->refdef.axis[1]),up=q3ne_dot(dir,f->refdef.axis[2]);
        float distance=fmaxf(0.1f,q3ne_length(qa_v3(front,left,0)));
        g->damage_roll=q3ne_mul(kick,left); g->damage_pitch=-q3ne_mul(kick,front);
        front=fmaxf(front,0.1f); g->damage_x=q3ne_div(-left,front); g->damage_y=q3ne_div(up,distance);
    }
    g->damage_x=q3nh_clamp(g->damage_x,-1,1); g->damage_y=q3nh_clamp(g->damage_y,-1,1);
    g->damage_value=kick; g->damage_kick_end_time=q3ne_plus(f->time,500); g->damage_time=(float)server_time;
}
static bool selected_ammo(q3n_player_state *o,const q3n_frame *f,bool *selected,qa_error *e)
{
    *selected=false;
    if(o->options.weapon_warning) {
        q3n_weapon_hud hud={0};
        if(!o->options.weapon_warning(o->options.context,f,&hud,e) || !q3ne_current(f,e))return false;
        if(hud.selected) {
            *selected=true;
            if(hud.warning<0 || hud.warning>2)return q3ne_fail(e,QA_ERROR_FORMAT,"Shared arsenal warning is outside its actual domain");
            int32_t previous=o->feedback.low_ammo_warning; o->feedback.low_ammo_warning=hud.warning;
            return !hud.warning || hud.warning==previous || sound(f,Q3N_S_NOAMMO,6,e);
        }
    }
    return true;
}
static bool ammo(q3n_player_state *o,const q3n_frame *f,const qa_q3_player *p,qa_error *e)
{
    bool selected;
    if(!selected_ammo(o,f,&selected,e))return false;
    if(selected)return true;
    int32_t total=0;
    int32_t extent=o->product==QA_Q3_TEAM_ARENA?14:11;
    for(int32_t i=2;i<extent;++i) {
        if(!(p->stats[q3nh_weapons_stat(o->product)]&(1<<i)))continue;
        bool slow=i==3 || i==4 || i==5 || i==7 || (o->product==QA_Q3_TEAM_ARENA && i==12);
        total=q3ne_plus(total,q3ne_word((uint32_t)p->ammo[i]*(slow?1000u:200u)));
        if(total>=5000) { o->feedback.low_ammo_warning=0; return true; }
    }
    int32_t previous=o->feedback.low_ammo_warning; o->feedback.low_ammo_warning=total==0?2:1;
    return previous==o->feedback.low_ammo_warning || sound(f,Q3N_S_NOAMMO,6,e);
}
static void award(q3n_player_state *o,const q3n_frame *f,q3n_sound s,q3n_graphic medal,int32_t count)
{
    q3n_player_feedback *g=&o->feedback; const q3n_media_view *m=q3n_media_read(f->media);
    if(g->reward_stack<9)g->rewards[++g->reward_stack]=(q3n_reward){m->sounds[s],m->graphics[medal],count};
}
static bool local_sounds(q3n_player_state *o,const q3n_frame *f,const qa_q3_player *p,
    q3n_entity *predicted,const q3n_transition_history *h,const q3n_player_state_context *c,qa_error *e)
{
    q3n_player_feedback *g=&o->feedback;
    if(p->persistant[3]!=h->persistant[3])return true;
    if(p->persistant[1]>h->persistant[1]) {
        int32_t armor=p->persistant[7]&255,health=p->persistant[7]>>8;
        q3n_sound s=o->product==QA_Q3_TEAM_ARENA && armor>50?Q3N_S_HIT_HIGH_ARMOR:
            o->product==QA_Q3_TEAM_ARENA && (armor || health>100)?Q3N_S_HIT_LOW_ARMOR:Q3N_S_HIT;
        if(!sound(f,s,6,e))return false;
    } else if(p->persistant[1]<h->persistant[1] && !sound(f,Q3N_S_HIT_TEAM,6,e))return false;
    if(p->stats[0]<q3ne_sub(h->health,1) && p->stats[0]>0 &&
       !q3n_events_pain(f,predicted,f->compiled?f->compiled->predicted_state->number:f->remote?f->remote->predicted_state->number:p->clientNum,p->stats[0],e))return false;
    if(c->intermission_started)return true;
    bool reward=false;
    const int32_t fields[6]={14,9,10,13,11,12};
    const q3n_graphic medals[6]={Q3N_G_MEDAL_CAPTURE,Q3N_G_MEDAL_IMPRESSIVE,Q3N_G_MEDAL_EXCELLENT,Q3N_G_MEDAL_GAUNTLET,Q3N_G_MEDAL_DEFEND,Q3N_G_MEDAL_ASSIST};
    q3n_sound sounds[6]={Q3N_S_CAPTURE_AWARD,Q3N_S_IMPRESSIVE,Q3N_S_EXCELLENT,Q3N_S_HUMILIATION,Q3N_S_DEFEND,Q3N_S_ASSIST};
    if(o->product==QA_Q3_TEAM_ARENA) {
        if(p->persistant[9]==1)sounds[1]=Q3N_S_FIRST_IMPRESSIVE;
        if(p->persistant[10]==1)sounds[2]=Q3N_S_FIRST_EXCELLENT;
        if(h->persistant[13]==1)sounds[3]=Q3N_S_FIRST_HUMILIATION;
    }
    for(unsigned i=0;i<6;++i)if(p->persistant[fields[i]]!=h->persistant[fields[i]]) {
        award(o,f,sounds[i],medals[i],p->persistant[fields[i]]); reward=true;
    }
    int32_t bits=p->persistant[5]^h->persistant[5];
    if(bits) {
        if((bits&1) && !sound(f,Q3N_S_DENIED,7,e))return false;
        else if(!(bits&1) && (bits&2) && !sound(f,Q3N_S_HUMILIATION,7,e))return false;
        else if(!(bits&3) && (bits&4) && !sound(f,Q3N_S_HOLY_SHIT,7,e))return false;
        reward=true;
    }
    if(q3n_frame_game_type(f)>=3)for(unsigned i=7;i<=9;++i)if(p->powerups[i]!=h->powerups[i] && p->powerups[i]) {
        if(!sound(f,Q3N_S_YOU_HAVE_FLAG,7,e))return false;
        break;
    }
    if(!reward && !c->warmup && p->persistant[2]!=h->persistant[2] && q3n_frame_game_type(f)<3) {
        if(p->persistant[2]==0) { if(!buffered(f,Q3N_S_TAKEN_LEAD,e))return false; }
        else if(p->persistant[2]==0x4000) { if(!buffered(f,Q3N_S_TIED_LEAD,e))return false; }
        else if(!(h->persistant[2]&~0x4000) && !buffered(f,Q3N_S_LOST_LEAD,e))return false;
    }
    int32_t elapsed=q3ne_sub(f->time,q3n_frame_match_start_time(f));
    if(c->timelimit>0) {
        int32_t sudden=q3ne_word(((uint32_t)c->timelimit*60u+2u)*1000u);
        int32_t minute=q3ne_word(((uint32_t)c->timelimit-1u)*60000u);
        int32_t five=q3ne_word(((uint32_t)c->timelimit-5u)*60000u);
        if(!(g->timelimit_warnings&4) && elapsed>sudden) {
            g->timelimit_warnings|=7; if(!sound(f,Q3N_S_SUDDEN_DEATH,7,e))return false;
        } else if(!(g->timelimit_warnings&2) && elapsed>minute) {
            g->timelimit_warnings|=3; if(!sound(f,Q3N_S_ONE_MINUTE,7,e))return false;
        } else if(c->timelimit>5 && !(g->timelimit_warnings&1) && elapsed>five) {
            g->timelimit_warnings|=1; if(!sound(f,Q3N_S_FIVE_MINUTES,7,e))return false;
        }
    }
    if(c->fraglimit>0 && q3n_frame_game_type(f)<4) {
        if(!(g->fraglimit_warnings&4) && c->scores1==q3ne_sub(c->fraglimit,1)) {
            g->fraglimit_warnings|=7; if(!buffered(f,Q3N_S_ONE_FRAG,e))return false;
        } else if(c->fraglimit>2 && !(g->fraglimit_warnings&2) && c->scores1==q3ne_sub(c->fraglimit,2)) {
            g->fraglimit_warnings|=3; if(!buffered(f,Q3N_S_TWO_FRAGS,e))return false;
        } else if(c->fraglimit>3 && !(g->fraglimit_warnings&1) && c->scores1==q3ne_sub(c->fraglimit,3)) {
            g->fraglimit_warnings|=1; if(!buffered(f,Q3N_S_THREE_FRAGS,e))return false;
        }
    }
    return true;
}
static bool player_events(q3n_player_state *o,const q3n_frame *f,const qa_q3_player *p,
    qa_q3_entity s,q3n_entity *cent,qa_q3_entity predicted_s,q3n_entity *predicted,
    const q3n_transition_history *h,qa_error *e)
{
    if(p->externalEvent && p->externalEvent!=h->external_event) {
        s.event=p->externalEvent; s.eventParm=p->externalEventParm;
        if(f->remote && !q3n_remote_frame_entity_event(f->remote,(uint32_t)p->clientNum,s.event,s.eventParm,e))return false;
        if(f->compiled && !q3n_compiled_frame_entity_event(f->compiled,(uint32_t)p->clientNum,s.event,s.eventParm,e))return false;
        if(!q3n_events_player(f,&s,cent,e))return false;
    }
    for(unsigned offset=2;offset>0;--offset) {
        int32_t i=q3ne_sub(p->eventSequence,(int32_t)offset); unsigned slot=(uint32_t)i&1u;
        if(i>=h->event_sequence || (i>q3ne_sub(h->event_sequence,2) && p->events[slot]!=h->events[slot])) {
            predicted_s.event=p->events[slot]; predicted_s.eventParm=p->eventParms[slot];
            if(f->remote) {
                f->remote->predicted_state->event=predicted_s.event;
                f->remote->predicted_state->eventParm=predicted_s.eventParm;
            }
            if(f->compiled) {
                f->compiled->predicted_state->event=predicted_s.event;
                f->compiled->predicted_state->eventParm=predicted_s.eventParm;
            }
            if(!q3n_events_player(f,&predicted_s,predicted,e))return false;
            o->predictable_events[(uint32_t)i&15u]=predicted_s.event; o->event_sequence=q3ne_plus(o->event_sequence,1);
        }
    }
    return true;
}
bool q3n_player_state_transition(q3n_player_state *o,const q3n_frame *f,const q3n_player_state_context *context,qa_error *e)
{
    if(!context || !current(o,f,e) || f->remote || f->compiled)return false;
    const qa_q3_player *p=&f->local_player;
    if(p->clientNum<0 || (uint32_t)p->clientNum>=f->source.max_clients || !f->entities)
        return q3ne_fail(e,QA_ERROR_FORMAT,"Playerstate followed client is outside its actual GAME extent");
    qa_application_native_q3_entity actual;
    if(!qa_application_native_q3_presentation_entity(f->application,&f->source,(uint32_t)p->clientNum,&actual,e) || !actual.present)
        return q3ne_fail(e,QA_ERROR_FORMAT,"Playerstate followed client has no actual source S");
    q3n_entity *cent=&f->entities[p->clientNum];
    if(!cent->valid || !qa_actor_id_equal(cent->actor,actual.binding.actor))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Playerstate events require their qualified real centity");
    q3n_transition_history *h=&o->history;
    if(h->valid && !qa_actor_id_equal(h->viewing_actor,f->viewing_actor)) {
        memset(h,0,sizeof(*h)); memset(&o->feedback,0,sizeof(o->feedback));
        o->event_sequence=0; memset(o->predictable_events,0,sizeof(o->predictable_events));
    }
    if(h->valid && h->source_frame==f->source.source_frame.number && h->source_time==f->time && !o->map_restart) {
        if(!o->options.weapon_warning)return true;
        /* The admitted arsenal has its own completed clock. A repeated world
         * cut retains primary transitions while its selected warning advances. */
        o->busy=true; bool selected;
        bool ok=selected_ammo(o,f,&selected,e);
        o->busy=false; return ok;
    }
    o->busy=true; o->feedback.this_frame_teleport=false;
    bool initial=!h->valid;
    if(initial)remember(h,f,p,actual.binding.actor);
    if((p->eFlags^h->e_flags)&4)o->feedback.this_frame_teleport=true;
    if(h->valid && (p->clientNum!=h->client_num || !qa_actor_id_equal(h->followed_actor,actual.binding.actor))) {
        o->feedback.this_frame_teleport=true; remember(h,f,p,actual.binding.actor);
    }
    bool ok=true;
    if(p->damageEvent!=h->damage_event && p->damageCount)damage(o,f,p,p,f->source.source_time_ms);
    if(initial || p->persistant[4]!=h->persistant[4])respawn(o,f);
    if(o->map_restart) { respawn(o,f); o->map_restart=false; }
    if(p->pmType!=5 && p->persistant[3]!=3)ok=local_sounds(o,f,p,cent,h,context,e);
    if(ok)ok=ammo(o,f,p,e) && player_events(o,f,p,actual.state,cent,actual.state,cent,h,e);
    if(ok && p->viewheight!=h->viewheight) {
        o->feedback.duck_change=(float)q3ne_sub(p->viewheight,h->viewheight); o->feedback.duck_time=f->time;
    }
    if(ok)remember(h,f,p,actual.binding.actor);
    o->busy=false; return ok;
}
bool q3n_player_state_respawn_remote(q3n_player_state *o,const q3n_frame *f,qa_error *e)
{
    if(!current(o,f,e) || !f->remote)return false;
    respawn(o,f); return q3ne_current(f,e);
}
bool q3n_player_state_respawn_compiled(q3n_player_state *o,const q3n_frame *f,qa_error *e)
{
    if(!current(o,f,e) || !f->compiled || !q3n_frame_snapshot_player(f))return false;
    respawn(o,f); return q3ne_current(f,e);
}
bool q3n_player_state_compiled_teleport_take(q3n_player_state *o,bool *out,qa_error *e)
{
    q3n_compiled_source_view source;
    if(!o || !out || o->busy || !o->options.compiled_source ||
       !q3n_compiled_source_read(o->options.compiled_source,&source,e))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled teleport feedback requires its returned CLIENT callback");
    *out=o->feedback.this_frame_teleport; o->feedback.this_frame_teleport=false; return true;
}
bool q3n_player_state_remote_teleport_take(q3n_player_state *o,bool *out,qa_error *e)
{
    if(!o || !out || o->busy || !o->options.remote_client ||
       !qa_native_q3_remote_client_current(o->options.remote_client))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote teleport feedback requires its returned actual CLIENT callback");
    *out=o->feedback.this_frame_teleport; o->feedback.this_frame_teleport=false; return true;
}
bool q3n_player_state_transition_remote(q3n_player_state *o,const q3n_frame *f,
    const qa_q3_player *p,const qa_q3_player *previous,const q3n_player_state_context *context,qa_error *e)
{
    if(!context || !current(o,f,e) || !f->remote || p!=f->remote->transition_player ||
       previous!=f->remote->previous_player || !p || !previous ||
       !f->remote->predicted_state || !f->remote->predicted_entity ||
       p->clientNum<0 || p->clientNum>=64 ||
       (f->remote->snapshots.stage!=Q3N_REMOTE_SNAPSHOT_CALLBACK &&
        f->remote->snapshots.stage!=Q3N_REMOTE_PREDICTION_CALLBACK))return false;
    qa_q3_entity actual; q3n_entity *cent; bool published;
    if(!q3n_frame_entity(f,(uint32_t)p->clientNum,&actual,&cent,&published,e) || !published)return false;
    q3n_remote_entity predicted;
    if(!q3n_remote_frame_predicted(f->remote,&predicted,e))return false;
    return q3n_player_state_transition_received(o,f,p,previous,context,actual,cent,
        *predicted.current,predicted.presentation,f->remote->snapshots.snapshot->server_time,e);
}
bool q3n_player_state_transition_compiled(q3n_player_state *o,const q3n_frame *f,
    const qa_q3_player *p,const qa_q3_player *previous,const q3n_player_state_context *context,qa_error *e)
{
    if(!context || !current(o,f,e) || !f->compiled || f->compiled->stage!=Q3N_COMPILED_PLAYER_TRANSITION ||
       p!=f->compiled->transition_player || previous!=f->compiled->previous_player || !p || !previous ||
       !f->compiled->snapshot || p->clientNum<0 || p->clientNum>=64)return false;
    qa_q3_entity actual; q3n_entity *cent; bool published;
    q3n_compiled_entity predicted;
    if(!q3n_frame_entity(f,(uint32_t)p->clientNum,&actual,&cent,&published,e) || !published ||
       !q3n_compiled_frame_predicted(f->compiled,&predicted,e))return false;
    return q3n_player_state_transition_received(o,f,p,previous,context,actual,cent,
        *predicted.current,predicted.presentation,f->compiled->snapshot->server_time,e);
}
static bool q3n_player_state_transition_received(q3n_player_state *o,const q3n_frame *f,
    const qa_q3_player *p,const qa_q3_player *previous,const q3n_player_state_context *context,
    qa_q3_entity actual,q3n_entity *cent,qa_q3_entity predicted_state,q3n_entity *predicted,int32_t server_time,qa_error *e)
{
    q3n_transition_history h={0};
    remember(&h,f,p->clientNum==previous->clientNum?previous:p,(qa_actor_id){0});
    o->busy=true;
    if(p->clientNum!=previous->clientNum)o->feedback.this_frame_teleport=true;
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(f);
    if(p->damageEvent!=h.damage_event && p->damageCount)
        damage(o,f,p,snapshot,server_time);
    if(p->persistant[4]!=h.persistant[4])respawn(o,f);
    if(o->map_restart) { respawn(o,f); o->map_restart=false; }
    bool ok=true;
    if(snapshot->pmType!=5 && p->persistant[3]!=3)
        ok=local_sounds(o,f,p,predicted,&h,context,e);
    if(ok)ok=ammo(o,f,snapshot,e) && player_events(o,f,p,actual,cent,
        predicted_state,predicted,&h,e);
    if(ok && p->viewheight!=h.viewheight) {
        o->feedback.duck_change=(float)q3ne_sub(p->viewheight,h.viewheight); o->feedback.duck_time=f->time;
    }
    if(ok)ok=q3ne_current(f,e);
    o->busy=false; return ok;
}
bool q3n_player_state_changed_events(q3n_player_state *o,const q3n_frame *f,bool show_miss,qa_error *e)
{
    if(!current(o,f,e))return false;
    const qa_q3_player *p=q3n_frame_predicted_player(f); qa_q3_entity state; q3n_entity *cent;
    if(f->remote) {
        q3n_remote_entity predicted;
        if(!q3n_remote_frame_predicted(f->remote,&predicted,e) ||
           (f->remote->snapshots.stage!=Q3N_REMOTE_COMPLETED_FRAME &&
            f->remote->snapshots.stage!=Q3N_REMOTE_PREDICTION_CALLBACK))return false;
        state=*predicted.current; cent=predicted.presentation;
    } else if(f->compiled) {
        q3n_compiled_entity predicted;
        if(!q3n_compiled_frame_predicted(f->compiled,&predicted,e) ||
           (f->compiled->stage!=Q3N_COMPILED_COMPLETED_FRAME && f->compiled->stage!=Q3N_COMPILED_PLAYER_TRANSITION))return false;
        state=*predicted.current; cent=predicted.presentation;
    } else {
        qa_application_native_q3_entity actual;
        if(p->clientNum<0 || (uint32_t)p->clientNum>=f->source.max_clients ||
           !qa_application_native_q3_presentation_entity(f->application,&f->source,(uint32_t)p->clientNum,&actual,e) || !actual.present)return false;
        cent=&f->entities[p->clientNum]; state=actual.state;
        if(!cent->valid || !qa_actor_id_equal(cent->actor,actual.binding.actor))return false;
    }
    o->busy=true; bool ok=true;
    for(unsigned offset=2;ok && offset>0;--offset) {
        int32_t i=q3ne_sub(p->eventSequence,(int32_t)offset); unsigned ring=(uint32_t)i&15u,slot=(uint32_t)i&1u;
        if(i<o->event_sequence && i>q3ne_sub(o->event_sequence,16) && p->events[slot]!=o->predictable_events[ring]) {
            qa_q3_entity scratch=state; scratch.event=p->events[slot]; scratch.eventParm=p->eventParms[slot];
            if(f->remote) {
                f->remote->predicted_state->event=scratch.event;
                f->remote->predicted_state->eventParm=scratch.eventParm;
            }
            if(f->compiled) {
                f->compiled->predicted_state->event=scratch.event;
                f->compiled->predicted_state->eventParm=scratch.eventParm;
            }
            ok=q3n_events_player(f,&scratch,cent,e);
            if(ok) { o->predictable_events[ring]=scratch.event;
                if(show_miss)o->options.print(o->options.context,"WARNING: changed predicted event\n"); }
        }
    }
    if(ok)ok=q3ne_current(f,e);
    o->busy=false; return ok;
}
bool q3n_player_state_prediction_finish(q3n_player_state *o,const q3n_frame *f,bool show_miss,qa_error *e)
{
    if(!current(o,f,e) || (f->compiled?f->compiled->stage!=Q3N_COMPILED_PLAYER_TRANSITION:
       !f->remote || f->remote->snapshots.stage!=Q3N_REMOTE_PREDICTION_CALLBACK))return false;
    const qa_q3_player *p=q3n_frame_predicted_player(f);
    if(!show_miss || o->event_sequence<=p->eventSequence)return true;
    o->busy=true; o->options.print(o->options.context,"WARNING: double event\n");
    bool ok=q3ne_current(f,e);
    if(ok)o->event_sequence=p->eventSequence;
    o->busy=false; return ok;
}
bool q3n_player_state_reward(q3n_player_state *o,const q3n_frame *f,q3n_reward *reward,float *alpha,bool *visible,qa_error *e)
{
    if(!reward || !alpha || !visible || !current(o,f,e))return false;
    q3n_player_feedback *g=&o->feedback; int32_t elapsed=q3ne_sub(f->time,g->reward_time); *visible=false;
    if(!g->reward_time || elapsed>=3000) {
        if(g->reward_stack<=0)return true;
        for(int32_t i=0;i<g->reward_stack;++i)g->rewards[i]=g->rewards[i+1];
        g->reward_time=f->time; --g->reward_stack; elapsed=0;
        if(!q3ne_sound(f,g->rewards[0].sound,NULL,q3n_frame_predicted_player(f)->clientNum,7,true,e))return false;
    }
    *reward=g->rewards[0]; *alpha=3000-elapsed<200?q3ne_div((float)(3000-elapsed),200):1; *visible=true; return true;
}
void q3n_player_state_round(q3n_player_state *o)
{ if(o && !o->busy) { o->feedback.fraglimit_warnings=0; o->feedback.timelimit_warnings=0; o->map_restart=true; } }
