/* CG scoreboard, id Software 1999-2005, GPL-2.0-or-later. */
#include "hud_internal.h"

static void place_string(int32_t value,char out[64])
{
    bool tied=(value&0x4000)!=0; value&=~0x4000; char place[32];
    if(value==1)snprintf(place,sizeof(place),"^41st^7");
    else if(value==2)snprintf(place,sizeof(place),"^12nd^7");
    else if(value==3)snprintf(place,sizeof(place),"^33rd^7");
    else { const char *suffix=value==11 || value==12 || value==13?"th":value%10==1?"st":value%10==2?"nd":value%10==3?"rd":"th";
        snprintf(place,sizeof(place),"%i%s",value,suffix); }
    snprintf(out,64,"%s%s",tied?"Tied for ":"",place);
}
static bool client_score(q3n_hud_draw *d,float y,const q3n_command_score *score,const float color[4],float fade,bool large,bool *local)
{
    const q3n_command_state *c=q3n_server_commands_state(d->commands);
    if(score->client<0 || score->client>=c->max_clients) {
        char text[64]; snprintf(text,sizeof(text),"Bad score->client: %i\n",score->client);
        d->player->options.print(d->player->options.context,text);
        return q3nh_current(d->owner,d->frame,d->error);
    }
    const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)score->client); if(!ci)return false;
    const q3n_media_view *m=q3n_media_read(d->frame->media); float icon_y=large?y-8:y,icon_size=large?32:16; char text[256];
    if(ci->dynamic.powerups&(1<<9)) { if(!q3nh_flag(d,80,icon_y,icon_size,icon_size,0,false))return false; }
    else if(ci->dynamic.powerups&(1<<7)) { if(!q3nh_flag(d,80,icon_y,icon_size,icon_size,1,false))return false; }
    else if(ci->dynamic.powerups&(1<<8)) { if(!q3nh_flag(d,80,icon_y,icon_size,icon_size,2,false))return false; }
    else {
        if(ci->bot_skill>0 && ci->bot_skill<=5) {
            if(d->settings->draw_icons && !q3nh_picture(d,80,icon_y,icon_size,icon_size,m->bot_skill_shaders[ci->bot_skill-1]))return false;
        } else if(ci->handicap<100) {
            snprintf(text,sizeof(text),"%i",ci->handicap);
            if(!q3nh_text(d,80,c->game_type==1?y-8:y,text,8,16,color,true,false,0))return false;
        }
        if(c->game_type==1) { snprintf(text,sizeof(text),"%i/%i",ci->wins,ci->losses);
            if(!q3nh_text(d,80,ci->handicap<100 && !ci->bot_skill?y+8:y,text,8,16,color,true,false,0))return false; }
    }
    if(!q3nh_head(d,112,large?y-16:y,large?48:16,large?48:16,score->client,qa_v3(0,180,0)))return false;
    if(score->ping==-1)snprintf(text,sizeof(text)," connecting    %s",ci->name);
    else if(ci->team==3)snprintf(text,sizeof(text)," SPECT %3i %4i %s",score->ping,score->time,ci->name);
    else snprintf(text,sizeof(text),"%5i %4i %4i %s",score->score,score->ping,score->time,ci->name);
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame);
    if(score->client==p->clientNum) {
        *local=true; int32_t rank=p->persistant[3]==3 || c->game_type>=3?-1:p->persistant[2]&~0x4000;
        float highlight[4]={rank==0?0:0.7f,rank==0 || rank==1?0:0.7f,rank==1 || rank==2?0:0.7f,q3ne_mul(fade,0.7f)};
        if(!q3nh_fill(d,176,y,512,17*d->frame->preferences.text_scale,highlight))return false;
    }
    if(!q3nh_big(d,160,y,text,fade))return false;
    unsigned ready=d->owner->product==QA_Q3_TEAM_ARENA?6u:5u;
    if((uint32_t)p->stats[ready]&(1u<<((uint32_t)score->client&31u)))return q3nh_text(d,80,y,"READY",16,16,color,true,true,0);
    return true;
}
static bool team_scores(q3n_hud_draw *d,int32_t y,int32_t team,float fade,int32_t maximum,int32_t line,bool large,bool *local,int32_t *count)
{
    const q3n_command_state *c=q3n_server_commands_state(d->commands); float color[4]={1,1,1,fade}; *count=0;
    for(int32_t i=0;i<c->num_scores && *count<maximum;++i) {
        const q3n_command_score *score=&c->scores[i]; if(score->client<0 || score->client>=64)return false;
        const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)score->client); if(!ci)return false;
        if(ci->team!=team)continue;
        if(!client_score(d,(float)(y+line * *count),score,color,fade,large,local))return false; ++*count;
    }
    return true;
}
bool q3nh_scoreboard(q3n_hud_draw *d,bool *showing)
{
    q3nh_anchor(d,320,240);
    q3n_hud_state *s=&d->owner->state; const q3n_command_state *c=q3n_server_commands_state(d->commands);
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame),*predicted=q3n_frame_predicted_player(d->frame); *showing=false;
    if(d->settings->paused || (c->game_type==2 && predicted->pmType==5)) {
        s->deferred_player_loading=0; s->scoreboard_first_time=true; return true;
    }
    if(c->warmup && !s->show_scores)return true;
    float color[4]={1,1,1,1};
    if(!s->show_scores && predicted->pmType!=3 && predicted->pmType!=5 && !q3nh_fade(d->frame->time,s->score_fade_time,200,color)) {
        s->deferred_player_loading=0; s->scoreboard_first_time=true; q3n_events_clear_killer(d->frame->events); return true;
    }
    if(d->owner->product==QA_Q3_TEAM_ARENA) {
        if(!d->owner->options.mission_paint(d->owner->options.context,d->frame,true,s->scoreboard_first_time,d->error) ||
           !q3nh_current(d->owner,d->frame,d->error))return false;
        s->scoreboard_first_time=false;
    } else {
        /* The original scoreboard uses the first RGB component as row fade. */
        float fade=color[0]; char text[256],place[64]; const q3n_event_state *events=q3n_events_state(d->frame->events);
        if(events->killer_name[0]) { snprintf(text,sizeof(text),"Fragged by %s",events->killer_name); if(!q3nh_center(d,40,text,fade))return false; }
        if(c->game_type<3) {
            if(p->persistant[3]!=3) { place_string(q3ne_plus(p->persistant[2],1),place);
                snprintf(text,sizeof(text),"%s place with %i",place,p->persistant[0]); if(!q3nh_center(d,60,text,fade))return false; }
        } else {
            if(c->team_scores[0]==c->team_scores[1])snprintf(text,sizeof(text),"Teams are tied at %i",c->team_scores[0]);
            else if(c->team_scores[0]>=c->team_scores[1])snprintf(text,sizeof(text),"Red leads %i to %i",c->team_scores[0],c->team_scores[1]);
            else snprintf(text,sizeof(text),"Blue leads %i to %i",c->team_scores[1],c->team_scores[0]);
            if(!q3nh_center(d,60,text,fade))return false;
        }
        const q3n_media_view *m=q3n_media_read(d->frame->media);
        if(!q3nh_picture(d,176,86,64,32,m->graphics[Q3N_G_SCOREBOARD_SCORE]) ||
           !q3nh_picture(d,264,86,64,32,m->graphics[Q3N_G_SCOREBOARD_PING]) ||
           !q3nh_picture(d,344,86,64,32,m->graphics[Q3N_G_SCOREBOARD_TIME]) ||
           !q3nh_picture(d,416,86,64,32,m->graphics[Q3N_G_SCOREBOARD_NAME]))return false;
        bool compact=c->num_scores>7,local=false; int32_t line=compact?16:40,top=compact?8:16,maximum=compact?17:7,y=118,count;
        if(d->frame->preferences.text_scale>1) {
            line=q3ne_int((float)line*d->frame->preferences.text_scale);
            int32_t fitting=346/line; if(maximum>fitting)maximum=fitting;
        }
        if(c->game_type>=3) {
            y+=line/2; int32_t first=c->team_scores[0]>=c->team_scores[1]?1:2;
            for(unsigned i=0;i<2;++i) {
                int32_t team=i==0?first:first==1?2:1;
                if(!team_scores(d,y,team,fade,maximum,line,!compact,&local,&count) ||
                   !q3nh_team_background(d,0,(float)(y-top),640,(float)(count*line+16),0.33f,team))return false;
                y+=count*line+16; maximum-=count;
            }
            if(!team_scores(d,y,3,fade,maximum,line,!compact,&local,&count))return false; y+=count*line+16;
        } else {
            if(!team_scores(d,y,0,fade,maximum,line,!compact,&local,&count))return false; y+=count*line+16; maximum-=count;
            if(!team_scores(d,y,3,fade,maximum,line,!compact,&local,&count))return false; y+=count*line+16;
        }
        if(!local)for(int32_t i=0;i<c->num_scores;++i)if(c->scores[i].client==p->clientNum) {
            if(!client_score(d,(float)y,&c->scores[i],color,fade,!compact,&local))return false; break;
        }
    }
    s->deferred_player_loading=q3ne_plus(s->deferred_player_loading,1);
    if(s->deferred_player_loading>10 && (!d->owner->options.load_deferred(d->owner->options.context,d->frame,d->error) ||
       !q3nh_current(d->owner,d->frame,d->error)))return false;
    *showing=true; return true;
}
static bool giant(q3n_hud_draw *d,float y,const char *text)
{ float width; return q3nh_width(d,text,32,48,0,&width) && q3nh_text(d,(640-width)*0.5f,y,text,32,48,q3nh_white,true,true,0); }
static bool tourney_line(q3n_hud_draw *d,float y,const char *name,int32_t score)
{
    const float black[4]={0,0,0,1}; char text[32]; snprintf(text,sizeof(text),"%i",score);
    return q3nh_text(d,8,y,name,32,48,black,true,true,0) &&
        q3nh_text(d,632-32*(float)strlen(text),y,text,32,48,black,true,true,0);
}
bool q3nh_tourney(q3n_hud_draw *d)
{
    q3nh_anchor(d,320,240);
    if(d->owner->product==QA_Q3_TEAM_ARENA)return true;
    q3n_hud *o=d->owner; const q3n_command_state *c=q3n_server_commands_state(d->commands);
    if(q3ne_plus(o->scores_request_time,2000)<d->frame->time) {
        o->scores_request_time=d->frame->time;
        if(!o->options.client_command(o->options.context,d->frame,"score",d->error) || !q3nh_current(o,d->frame,d->error))return false;
    }
    const float black[4]={0,0,0,1}; const char *motd; uint64_t revision;
    if(!q3nh_fill(d,0,0,640,480,black) || !q3n_frame_configstring(d->frame,
       4,&motd,&revision,d->error) || !giant(d,8,*motd?motd:"Scoreboard"))return false;
    int32_t seconds=d->frame->time/1000,minutes=seconds/60; seconds%=60; char text[64];
    snprintf(text,sizeof(text),"%i:%i%i",minutes,seconds/10,seconds%10); if(!giant(d,64,text))return false;
    if(c->game_type>=3)return tourney_line(d,160,"Red Team",c->team_scores[0]) && tourney_line(d,224,"Blue Team",c->team_scores[1]);
    float y=160;
    for(uint32_t i=0;i<64;++i) {
        const q3n_client_info *ci=q3n_clients_get(d->frame->clients,i); if(!ci || !ci->info_valid || ci->team!=0)continue;
        if(!tourney_line(d,y,ci->name,ci->dynamic.score))return false; y+=64;
    }
    return true;
}
