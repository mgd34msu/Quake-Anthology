/* CG corner overlays, id Software 1999-2005, GPL-2.0-or-later. */
#include "qa/game_type.h"
#include "hud_internal.h"

static bool team_overlay(q3n_hud_draw *d,float *y,bool right,bool upper)
{
    if(!d->settings->team_overlay)return true;
    const q3n_command_state *c=q3n_server_commands_state(d->commands); int32_t team=q3n_frame_snapshot_player(d->frame)->persistant[3];
    if(team!=1 && team!=2)return true;
    int32_t count=c->num_sorted_team_players<8?c->num_sorted_team_players:8,players=0;
    float player_width=0,location_width=0,cell=8*d->frame->preferences.text_scale;
    for(int32_t i=0;i<count;++i) {
        int32_t number=c->sorted_team_players[i]; if(number<0 || number>=64)return false;
        const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)number); if(!ci)return false;
        if(ci->info_valid && ci->team==team) { ++players; float width;
            if(!q3nh_width(d,ci->name,8,8,12,&width))return false;
            if(width>player_width)player_width=width; }
    }
    if(!players)return true;
    for(int32_t i=1;i<64;++i) { const char *location; if(!q3nh_location(d,i,&location))return false;
        float width; if(!q3nh_width(d,location,8,8,16,&width))return false;
        if(width>location_width)location_width=width; }
    float width=player_width+location_width+11*cell,x=right?640-width:0,height=(float)players*cell;
    float return_y=upper?*y+height:*y-height; if(!upper)*y-=height;
    if(!q3nh_team_background(d,x,*y,width,height,0.33f,team))return false;
    const q3n_media_view *m=q3n_media_read(d->frame->media);
    for(int32_t i=0;i<count;++i) {
        const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)c->sorted_team_players[i]);
        if(!ci || !ci->info_valid || ci->team!=team)continue;
        if(!q3nh_text(d,x+cell,*y,ci->name,8,8,q3nh_white,false,false,12))return false;
        if(location_width!=0.0f) { const char *location; if(!q3nh_location(d,ci->dynamic.location,&location))return false;
            if(!*location)location="unknown";
            if(!q3nh_text(d,x+2*cell+player_width,*y,location,8,8,q3nh_white,false,false,16))return false; }
        float color[4]; q3nh_health(ci->dynamic.health,ci->dynamic.armor,color); char text[32];
        snprintf(text,sizeof(text),"%3i %3i",ci->dynamic.health,ci->dynamic.armor);
        float xx=x+3*cell+player_width+location_width;
        if(!q3nh_text(d,xx,*y,text,8,8,color,false,false,0))return false;
        xx+=3*cell; int32_t weapon=ci->dynamic.cur_weapon;
        if(weapon<0 || weapon>=16)return q3ne_fail(d->error,QA_ERROR_FORMAT,"Team overlay source weapon is invalid");
        if(!q3nh_picture(d,xx,*y,cell,cell,m->weapons[weapon].weapon_icon?m->weapons[weapon].weapon_icon:m->graphics[Q3N_G_DEFER]))return false;
        xx=right?x:x+width-cell;
        size_t item_count; const qa_q3_item *items=qa_q3_items(d->owner->product,&item_count);
        for(unsigned powerup=0;powerup<16;++powerup)if(ci->dynamic.powerups&(1<<powerup)) {
            for(size_t j=1;j<item_count;++j)if((items[j].kind==QA_Q3_ITEM_POWERUP || items[j].kind==QA_Q3_ITEM_TEAM ||
                items[j].kind==QA_Q3_ITEM_PERSISTENT) && items[j].tag==(int32_t)powerup) {
                int32_t shader; if(!qa_q3_register_shader(d->frame->assets,items[j].icon,false,&shader,d->error) ||
                   !q3nh_picture(d,xx,*y,cell,cell,shader))return false;
                xx+=right?-cell:cell; break;
            }
        }
        *y+=cell;
    }
    *y=return_y; return true;
}
static bool upper(q3n_hud_draw *d)
{
    q3nh_anchor(d,640,0);
    const q3n_command_state *c=q3n_server_commands_state(d->commands); float y=0; char text[128];
    if(qa_game_type_is_team(c->game_type) && d->settings->team_overlay==1 && !team_overlay(d,&y,true,true))return false;
    if(d->settings->draw_snapshot) {
        if(d->frame->remote)snprintf(text,sizeof(text),"time:%i snap:%i cmd:%i",
            d->frame->remote->snapshots.snapshot->server_time,
            d->frame->remote->source.publication.latest_message,d->frame->remote->snapshots.command_sequence);
        else snprintf(text,sizeof(text),"time:%i frame:%llu cmd:%i",d->frame->source.source_time_ms,
            (unsigned long long)d->frame->source.source_frame.number,c->server_command_sequence);
        if(!q3nh_right(d,635,y+2,text,1))return false;
        y+=20*d->frame->preferences.text_scale;
    }
    if(d->settings->draw_fps) {
        q3n_hud *o=d->owner; int32_t time=o->options.milliseconds(o->options.context);
        o->previous_times[(uint32_t)o->fps_index&3u]=q3ne_sub(time,o->previous_milliseconds);
        o->previous_milliseconds=time; o->fps_index=q3ne_plus(o->fps_index,1);
        if(o->fps_index>4) {
            int32_t total=0; for(unsigned i=0;i<4;++i)total=q3ne_plus(total,o->previous_times[i]); if(!total)total=1;
            snprintf(text,sizeof(text),"%ifps",4000/total);
            if(!q3nh_right(d,635,y+2,text,1))return false;
        }
        y+=20*d->frame->preferences.text_scale;
    }
    if(d->settings->draw_timer) {
        int32_t seconds=q3ne_sub(d->frame->time,c->level_start_time)/1000,minutes=seconds/60; seconds%=60;
        snprintf(text,sizeof(text),"%i:%i%i",minutes,seconds/10,seconds%10);
        if(!q3nh_right(d,635,y+2,text,1))return false;
        y+=20*d->frame->preferences.text_scale;
    }
    if(d->settings->draw_attacker) {
        const qa_q3_player *p=q3n_frame_predicted_player(d->frame); q3n_player_feedback *g=&d->player->feedback;
        int32_t client=p->persistant[6];
        if(p->stats[0]>0 && g->attacker_time && client>=0 && client<64 && client!=q3n_frame_snapshot_player(d->frame)->clientNum) {
            if(q3ne_sub(d->frame->time,g->attacker_time)>10000)g->attacker_time=0;
            else {
                if(!q3nh_head(d,580,y,60,60,client,qa_v3(0,180,0)))return false;
                const char *info; uint64_t revision; char name[64];
                if(!q3n_frame_configstring(d->frame,544u+(uint32_t)client,
                    &info,&revision,d->error) || !qa_q3_info_value(info,"n",name,sizeof(name),d->error) ||
                   !q3nh_right(d,640,y+60,name,0.5f))return false;
            }
        }
    }
    return true;
}
static bool free_score(q3n_hud_draw *d,float *x,float y,int32_t score,bool selected,bool first)
{
    char text[32]; snprintf(text,sizeof(text),"%2i",score); float width;
    if(!q3nh_width(d,text,16,16,0,&width))return false;
    width+=8*d->frame->preferences.text_scale; *x-=width;
    float color[4]={selected?first?0:1:0.5f,selected?0:0.5f,selected?first?1:0:0.5f,0.33f};
    if(!q3nh_fill(d,*x,y-4,width,24*d->frame->preferences.text_scale,color))return false;
    if(selected && !q3nh_picture(d,*x,y-4,width,24*d->frame->preferences.text_scale,q3n_media_read(d->frame->media)->graphics[Q3N_G_SELECT]))return false;
    return q3nh_big(d,*x+4,y,text,1);
}
static bool scores(q3n_hud_draw *d,float *y)
{
    const q3n_command_state *c=q3n_server_commands_state(d->commands); const qa_q3_player *p=q3n_frame_snapshot_player(d->frame);
    int32_t first=c->scores1,second=c->scores2; float row=24*d->frame->preferences.text_scale;
    *y-=row; float y1=*y,x=640; char text[32]; const q3n_media_view *m=q3n_media_read(d->frame->media);
    if(qa_game_type_is_team(c->game_type)) {
        for(unsigned i=0;i<2;++i) {
            int32_t team=i==0?2:1,value=i==0?second:first; snprintf(text,sizeof(text),"%2i",value);
            float width; if(!q3nh_width(d,text,16,16,0,&width))return false;
            width+=8*d->frame->preferences.text_scale; x-=width; float color[4]={team==1?1:0,0,team==2?1:0,0.33f};
            if(!q3nh_fill(d,x,*y-4,width,row,color) || (p->persistant[3]==team && !q3nh_picture(d,x,*y-4,width,row,m->graphics[Q3N_G_SELECT])) ||
               !q3nh_big(d,x+4,*y,text,1))return false;
            if(c->game_type==4) { y1=*y-row; int32_t flag=i==0?c->blue_flag:c->red_flag;
                if(flag>=0 && flag<=2 && !q3nh_picture(d,x,y1-4,width,row,i==0?m->blue_flag_shaders[flag]:m->red_flag_shaders[flag]))return false; }
        }
    } else {
        int32_t score=p->persistant[0]; bool spectator=p->persistant[3]==3; if(first!=score)second=score;
        if(second!=-9999 && !free_score(d,&x,*y,second,!spectator && score==second && score!=first,false))return false;
        if(first!=-9999 && !free_score(d,&x,*y,first,!spectator && score==first,true))return false;
    }
    int32_t limit=qa_game_type_is_objective(c->game_type)?c->capturelimit:c->fraglimit;
    if(limit) { snprintf(text,sizeof(text),"%2i",limit); float width;
        if(!q3nh_width(d,text,16,16,0,&width))return false;
        x-=width+8*d->frame->preferences.text_scale;
        if(!q3nh_big(d,x+4,*y,text,1))return false; }
    *y=y1-8; return true;
}
static bool powerups(q3n_hud_draw *d,float y)
{
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame); if(p->stats[0]<=0)return true;
    unsigned sorted[16],count=0; int32_t remaining[16];
    for(unsigned i=0;i<16;++i)if(p->powerups[i]) {
        int32_t time=q3ne_sub(p->powerups[i],d->frame->time); if(time<0 || time>999000)continue;
        unsigned j=0; while(j<count && remaining[j]<time)++j;
        for(unsigned k=count;k>j;--k) { sorted[k]=sorted[k-1]; remaining[k]=remaining[k-1]; }
        sorted[j]=i; remaining[j]=time; ++count;
    }
    size_t item_count; const qa_q3_item *items=qa_q3_items(d->owner->product,&item_count); const q3n_event_state *g=q3n_events_state(d->frame->events);
    for(unsigned i=0;i<count;++i) {
        const qa_q3_item *item=NULL;
        for(size_t j=1;j<item_count;++j)if((items[j].kind==QA_Q3_ITEM_POWERUP || items[j].kind==QA_Q3_ITEM_TEAM || items[j].kind==QA_Q3_ITEM_PERSISTENT) &&
           items[j].tag==(int32_t)sorted[i]) { item=&items[j]; break; }
        if(!item)continue;
        y-=48; const float red[4]={1,0.2f,0.2f,1};
        if(!q3nh_color(d,red) || !q3nh_field(d,528,y,2,remaining[i]/1000))return false;
        float color[4],*modulation=NULL;
        if(!d->frame->preferences.reduced_flashes && remaining[i]<5000) { float fraction=((float)remaining[i] / 1000); fraction=(fraction + -(float)q3ne_int(fraction));
            for(unsigned j=0;j<4;++j)color[j]=fraction;
            modulation=color; }
        if(!q3nh_color(d,modulation))return false;
        float size=48;
        if(!d->frame->preferences.reduced_flashes && g->powerup_active==(int32_t)sorted[i] && q3ne_sub(d->frame->time,g->powerup_time)<200) {
            float pulse=(1 + -(((float)d->frame->time + -(float)g->powerup_time) / 200));
            size=(48 * (1 + (0.5f * pulse)));
        }
        int32_t shader;
        if(!qa_q3_register_shader(d->frame->assets,item->icon,false,&shader,d->error) || !q3nh_picture(d,640-size,y+24-size/2,size,size,shader))return false;
    }
    return q3nh_color(d,NULL);
}
static bool pickup(q3n_hud_draw *d,float y)
{
    if(q3n_frame_snapshot_player(d->frame)->stats[0]<=0)return true;
    y-=48;
    const qa_hud_pickup_state *g=qa_hud_pickup_read(d->owner->options.messages); float color[4];
    if(!g || g->family!=QA_GAME_Q3 || !g->source_item ||
       !q3nh_fade(d->frame->time,(int32_t)(uint32_t)(g->starts_ns/UINT64_C(1000000)),3000,color))return true;
    size_t count; qa_q3_items(d->owner->product,&count);
    if(g->source_item>=count)return true;
    if(!q3n_media_register_item(d->frame->media,g->source_item,d->error) || !q3nh_color(d,color) ||
       !q3nh_picture(d,8,y,48,48,q3n_media_read(d->frame->media)->items[g->source_item].icon) ||
       !q3nh_big(d,64,y+16,g->text,color[0]))return false;
    return q3nh_color(d,NULL);
}
bool q3nh_corners(q3n_hud_draw *d)
{
    if((d->owner->product==QA_Q3_ARENA || !d->settings->paused) && !upper(d))return false;
    if(d->owner->product==QA_Q3_TEAM_ARENA)return true;
    q3nh_anchor(d,640,480);
    const q3n_command_state *c=q3n_server_commands_state(d->commands); float y=432;
    if(qa_game_type_is_team(c->game_type) && d->settings->team_overlay==2 && !team_overlay(d,&y,true,false))return false;
    if(!scores(d,&y) || !powerups(d,y))return false;
    y=432; q3nh_anchor(d,0,480);
    if(qa_game_type_is_team(c->game_type) && d->settings->team_overlay==3 && !team_overlay(d,&y,false,false))return false;
    return pickup(d,y);
}
bool q3nh_team_chat(q3n_hud_draw *d)
{
    q3nh_anchor(d,0,480);
    int32_t height=d->settings->team_chat_height<8?d->settings->team_chat_height:8; if(height<=0)return true;
    const q3n_command_state *c=q3n_server_commands_state(d->commands); int32_t last=c->team_chat_last_position;
    if(last==c->team_chat_position)return true;
    unsigned slot=(uint32_t)last%(uint32_t)height;
    if(q3ne_sub(d->frame->time,c->team_chat_times[slot])>d->settings->team_chat_time) {
        last=q3ne_plus(last,1); if(!q3n_server_commands_chat_drawn(d->commands,d->frame,last,d->error))return false;
    }
    float row=8*d->frame->preferences.text_scale,h=(float)q3ne_sub(c->team_chat_position,last)*row;
    int32_t team=q3n_frame_snapshot_player(d->frame)->persistant[3];
    float color[4]={team==1?1:0,team!=1 && team!=2?1:0,team==2?1:0,0.33f};
    if(!q3nh_color(d,color) || !q3nh_picture(d,0,420-h,640,h,q3n_media_read(d->frame->media)->graphics[Q3N_G_TEAM_STATUS_BAR]) || !q3nh_color(d,NULL))return false;
    for(int32_t i=q3ne_sub(c->team_chat_position,1);i>=last;i=q3ne_sub(i,1)) {
        if(!q3nh_text(d,8,420-(float)q3ne_sub(c->team_chat_position,i)*row,c->team_chat[(uint32_t)i%(uint32_t)height],8,8,q3nh_white,false,false,0))return false;
        if(i==INT32_MIN)break;
    }
    return true;
}
