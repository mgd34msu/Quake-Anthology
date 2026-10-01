/* cg_newdraw.c owner draws, with the Team Arena stat schema. GPL-2.0-or-later. */
#include "mission_hud_internal.h"
#include "qa/application_equipment.h"
#include <float.h>

const q3n_client_info *q3nm_client(q3n_mission_hud *o,int index)
{
    const q3n_client_info *ci=index>=0&&index<64?q3n_clients_get(o->frame->clients,(uint32_t)index):NULL;
    if(!ci)q3nm_result(q3ne_fail(o->menus->error,QA_ERROR_FORMAT,"Mission owner draw requires an actual source client row"));
    return ci;
}
const char *q3nm_location(q3n_mission_hud *o,int index)
{
    const char *text=NULL; uint64_t revision;
    q3nm_result(qa_native_q3_wire_reader_configstring(o->frame->reader,
        (uint32_t)(608+index),&text,&revision,o->menus->error)); return text&&*text?text:"unknown";
}
int q3nm_selected(q3n_mission_hud *o)
{
    int selected=q3nm_integer(o,"cg_currentSelectedPlayer"),count=o->commands?o->commands->num_sorted_team_players:0;
    if(selected<0||selected>=count) { q3nm_result(qa_native_q3_client_cvar_integer(o->options.client,"cg_currentSelectedPlayer",0,o->menus->error)); selected=0; }
    return selected;
}
static int selected_index(q3n_mission_hud *o) { return o->commands?o->commands->sorted_team_players[q3nm_selected(o)]:0; }
static const q3n_client_info *selected(q3n_mission_hud *o) { return q3nm_client(o,selected_index(o)); }
int q3nm_status(q3n_mission_hud *o,int task)
{
    const q3n_media_view *m=q3n_media_read(o->frame->media);
    const q3n_graphic status[]={Q3N_G_ASSAULT,Q3N_G_ASSAULT,Q3N_G_DEFEND,Q3N_G_PATROL,Q3N_G_FOLLOW,Q3N_G_RETRIEVE,Q3N_G_ESCORT,Q3N_G_CAMP};
    return m->graphics[task>=0&&task<8?status[task]:Q3N_G_ASSAULT];
}
static int entity_weapon(q3n_mission_hud *o)
{
    qa_application_native_q3_entity entity;
    if(!qa_application_native_q3_presentation_entity(o->options.application,&o->frame->source,
        (uint32_t)o->frame->local_player.clientNum,&entity,o->menus->error)) { q3nm_result(false); return 0; }
    return entity.state.weapon;
}
float q3nm_value(int id)
{
    q3n_mission_hud *o=q3nm_active(); const qa_q3_player *p=&o->frame->local_player; const q3n_client_info *ci;
    switch(id) {
    case CG_SELECTEDPLAYER_ARMOR:ci=selected(o); return ci?(float)ci->dynamic.armor:-1;
    case CG_SELECTEDPLAYER_HEALTH:ci=selected(o); return ci?(float)ci->dynamic.health:-1;
    case CG_PLAYER_ARMOR_VALUE:return (float)p->stats[4];
    case CG_PLAYER_AMMO_VALUE:{
        if(o->options.shared_weapon_hud) {
            qa_application_equipment_view equipment;
            if(!qa_application_equipment_read(o->options.application,o->frame->viewing_actor,&equipment,o->menus->error)||
                !qa_application_equipment_current(o->options.application,&equipment)||!q3nm_current(o,o->frame,o->menus->error)) {
                q3nm_result(false); return -1; }
            if(!equipment.has_weapon_status||!equipment.finite_ammo)return -1;
            if(!isfinite(equipment.ammo_count)||fabs(equipment.ammo_count)>FLT_MAX) {
                q3nm_result(q3ne_fail(o->menus->error,QA_ERROR_FORMAT,"Shared Mission HUD ammo leaves source float extent")); return -1; }
            return (float)equipment.ammo_count;
        }
        int weapon=entity_weapon(o); return weapon>0&&weapon<16?(float)p->ammo[weapon]:-1; }
    case CG_PLAYER_SCORE:return (float)p->persistant[0]; case CG_PLAYER_HEALTH:return (float)p->stats[0];
    case CG_RED_SCORE:return o->commands?(float)o->commands->scores1:-1;
    case CG_BLUE_SCORE:return o->commands?(float)o->commands->scores2:-1; default:return -1;
    }
}
static bool team_flag(q3n_mission_hud *o,bool yours)
{
    const q3n_command_state *s=o->commands; if(!s)return false; int team=o->frame->local_player.persistant[3];
    if(s->game_type==5)return team==1&&s->flag_status==(yours?2:3)||team==2&&s->flag_status==(yours?3:2);
    if(s->game_type==4)return team==1&&(yours?s->blue_flag:s->red_flag)==1||team==2&&(yours?s->red_flag:s->blue_flag)==1;
    return false;
}
qboolean q3nm_visible(int flags)
{
    q3n_mission_hud *o=q3nm_active(); const q3n_command_state *s=o->commands; if(!s)return qfalse;
    int type=s->game_type; const qa_q3_player *p=&o->frame->local_player;
    if(flags&CG_SHOW_TEAMINFO)return q3nm_integer(o,"cg_currentSelectedPlayer")==s->num_sorted_team_players;
    if(flags&CG_SHOW_NOTEAMINFO)return q3nm_integer(o,"cg_currentSelectedPlayer")!=s->num_sorted_team_players;
    if(flags&CG_SHOW_OTHERTEAMHASFLAG)return team_flag(o,false);
    if(flags&CG_SHOW_YOURTEAMHASENEMYFLAG)return team_flag(o,true);
    if(flags&(CG_SHOW_BLUE_TEAM_HAS_REDFLAG|CG_SHOW_RED_TEAM_HAS_BLUEFLAG))return
        (flags&CG_SHOW_BLUE_TEAM_HAS_REDFLAG)&&(s->red_flag==1||s->flag_status==2)||
        (flags&CG_SHOW_RED_TEAM_HAS_BLUEFLAG)&&(s->blue_flag==1||s->flag_status==3);
    if((flags&CG_SHOW_ANYTEAMGAME)&&type>=3)return qtrue; if((flags&CG_SHOW_ANYNONTEAMGAME)&&type<3)return qtrue;
    if(flags&CG_SHOW_HARVESTER)return type==7; if(flags&CG_SHOW_ONEFLAG)return type==5;
    if((flags&CG_SHOW_CTF)&&type==4)return qtrue; if(flags&CG_SHOW_OBELISK)return type==6;
    if((flags&CG_SHOW_HEALTHCRITICAL)&&p->stats[0]<25)return qtrue; if((flags&CG_SHOW_HEALTHOK)&&p->stats[0]>=25)return qtrue;
    if((flags&CG_SHOW_SINGLEPLAYER)&&type==2)return qtrue; if((flags&CG_SHOW_TOURNAMENT)&&type==1)return qtrue;
    if(flags&CG_SHOW_IF_PLAYER_HAS_FLAG)return p->powerups[7]||p->powerups[8]||p->powerups[9]; return qfalse;
}
static const char *type_text(q3n_mission_hud *o)
{ if(!o->commands)return ""; switch(o->commands->game_type) { case 0:return "Free For All"; case 3:return "Team Deathmatch";
    case 4:return "Capture the Flag"; case 5:return "One Flag CTF"; case 6:return "Overload"; case 7:return "Harvester"; default:return ""; } }
static const char *status_text(q3n_mission_hud *o)
{
    const qa_q3_player *p=&o->frame->local_player; const q3n_command_state *s=o->commands; if(!s)return "";
    if(s->game_type<3) { if(p->persistant[3]==3)return ""; int rank=q3ne_plus(p->persistant[2],1); bool tied=(rank&0x4000)!=0; rank&=~0x4000;
        const char *place=rank==1?"^41st^7":rank==2?"^12nd^7":rank==3?"^33rd^7":q3menu_format("%d%s",rank,
            rank==11||rank==12||rank==13?"th":rank%10==1?"st":rank%10==2?"nd":rank%10==3?"rd":"th");
        return q3menu_format("%s%s place with %d",tied?"Tied for ":"",place,p->persistant[0]); }
    int red=s->team_scores[0],blue=s->team_scores[1];
    return red==blue?q3menu_format("Teams are tied at %d",red):red>=blue?q3menu_format("Red leads Blue, %d to %d",red,blue):q3menu_format("Blue leads Red, %d to %d",blue,red);
}
static const char *killer(q3n_mission_hud *o)
{ const q3n_event_state *s=q3n_events_state(o->frame->events); return s&&s->killer_name[0]?q3menu_format("Fragged by %s",s->killer_name):""; }
static const char *team_name(q3n_mission_hud *o,bool blue)
{ qa_native_q3_client_cvar v={0}; q3nm_result(qa_native_q3_client_cvar_read(o->options.client,blue?"cg_blueTeamName":"cg_redTeamName",&v,o->menus->error)); return q3menu_format("%s",v.value); }
int q3nm_owner_width(int id,float scale)
{ q3n_mission_hud *o=q3nm_active(); const char *text=id==CG_GAME_TYPE?type_text(o):id==CG_GAME_STATUS?status_text(o):
    id==CG_KILLER?killer(o):id==CG_RED_NAME?team_name(o,false):id==CG_BLUE_NAME?team_name(o,true):""; return q3nm_width(text,scale,0); }
static void pic(q3n_mission_hud *o,rectDef_t r,int asset) { if(!o->menus->failed)q3nm_result(q3nh_picture(&o->draw,r.x,r.y,r.w,r.h,asset)); }
static void tint(q3n_mission_hud *o,const float *color) { if(!o->menus->failed)q3nm_result(q3nh_color(&o->draw,color)); }
static void text(rectDef_t r,float scale,float color[4],const char *value,int style)
{ q3nm_text(r.x,r.y+r.h,scale,color,value,0,0,style); }
static void number(q3n_mission_hud *o,rectDef_t r,float scale,float color[4],int value,int picture,int style)
{ if(picture) { tint(o,color); pic(o,r,picture); tint(o,NULL); } else { const char *s=q3menu_format("%d",value); q3nm_text(r.x+(r.w-q3nm_width(s,scale,0))/2,r.y+r.h,scale,color,s,0,0,style); } }
static const qa_q3_item *powerup(int index)
{ size_t count; const qa_q3_item *items=qa_q3_items(QA_Q3_TEAM_ARENA,&count); for(size_t i=0;i<count;++i)
    if((items[i].kind==QA_Q3_ITEM_POWERUP||items[i].kind==QA_Q3_ITEM_PERSISTENT||items[i].kind==QA_Q3_ITEM_TEAM)&&items[i].tag==index)return &items[i]; return NULL; }
static void item_picture(q3n_mission_hud *o,rectDef_t r,const qa_q3_item *item)
{ int handle=0; if(o->menus->failed)return; if(item&&item->icon)q3nm_result(qa_q3_register_shader(o->options.assets,item->icon,true,&handle,o->menus->error)); pic(o,r,handle); }
static void armor(q3n_mission_hud *o,rectDef_t r,bool flat)
{
    const q3n_media_view *m=q3n_media_read(o->frame->media);
    if(flat||(!o->settings.draw_3d_icons&&o->settings.draw_icons)) { r.y+=r.h/2+1; pic(o,r,m->graphics[Q3N_G_ARMOR_ICON]); }
    else if(o->settings.draw_3d_icons)q3nm_result(q3nh_model(&o->draw,r.x,r.y,r.w,r.h,m->graphics[Q3N_G_ARMOR],0,qa_v3(90,0,-10),qa_v3(0,(o->frame->time&2047)*360.0f/2048,0)));
}
static void ammo(q3n_mission_hud *o,rectDef_t r,bool flat)
{
    if(o->options.shared_weapon_hud)return;
    const q3n_media_view *m=q3n_media_read(o->frame->media); int weapon;
    if(flat||(!o->settings.draw_3d_icons&&o->settings.draw_icons)) { weapon=o->frame->local_player.weapon;
        if(weapon>=0&&weapon<16&&m->weapons[weapon].ammo_icon)pic(o,r,m->weapons[weapon].ammo_icon); }
    else if(o->settings.draw_3d_icons) { weapon=entity_weapon(o); if(weapon>0&&weapon<16&&m->weapons[weapon].ammo_model)
        q3nm_result(q3nh_model(&o->draw,r.x,r.y,r.w,r.h,m->weapons[weapon].ammo_model,0,qa_v3(70,0,0),qa_v3(0,90+20*sinf(o->frame->time/1000.0f),0))); }
}
static void player_head(q3n_mission_hud *o,rectDef_t r)
{
    q3n_hud_state *s=&o->hud->state; const q3n_player_feedback *p=q3n_player_state_feedback(o->frame->player_state); int time=o->frame->time;
    if(p&&p->damage_time&&q3ne_sub(time,p->damage_time)<500) {
        float frac=(float)q3ne_sub(time,p->damage_time)/500,size=r.w*1.25f*(1.5f-frac*0.5f),stretch=size-r.w*1.25f;
        r.x-=stretch*0.5f+p->damage_x*stretch*0.5f; s->head_start_yaw=180+p->damage_x*45;
        s->head_end_yaw=180+20*cosf(q3n_events_crandom(o->frame->events)*(float)M_PI);
        s->head_end_pitch=5*cosf(q3n_events_crandom(o->frame->events)*(float)M_PI); s->head_start_time=time;
        s->head_end_time=q3ne_int(q3ne_plus(time,100)+q3n_events_random(o->frame->events)*2000);
    } else if(time>=s->head_end_time) {
        s->head_start_yaw=s->head_end_yaw; s->head_start_pitch=s->head_end_pitch; s->head_start_time=s->head_end_time;
        s->head_end_time=q3ne_int(q3ne_plus(time,100)+q3n_events_random(o->frame->events)*2000);
        s->head_end_yaw=180+20*cosf(q3n_events_crandom(o->frame->events)*(float)M_PI);
        s->head_end_pitch=5*cosf(q3n_events_crandom(o->frame->events)*(float)M_PI);
    }
    if(s->head_start_time>time)s->head_start_time=time;
    float frac=(float)q3ne_sub(time,s->head_start_time)/(float)q3ne_sub(s->head_end_time,s->head_start_time); frac=frac*frac*(3-2*frac);
    q3nm_result(q3nh_head(&o->draw,r.x,r.y,r.w,r.h,o->frame->local_player.clientNum,
        qa_v3(s->head_start_pitch+(s->head_end_pitch-s->head_start_pitch)*frac,s->head_start_yaw+(s->head_end_yaw-s->head_start_yaw)*frac,0)));
}
static bool order_blink(q3n_mission_hud *o) { return o->order_pending&&o->frame->time>q3ne_sub(o->order_time,2500)&&((o->frame->time>>9)&1); }
static int carrier(q3n_mission_hud *o,bool blue)
{ for(int i=0;i<o->commands->max_clients;++i) { const q3n_client_info *ci=q3nm_client(o,i);
    if(ci&&ci->info_valid&&ci->team==(blue?1:2)&&(ci->dynamic.powerups&(1<<(blue?8:7))))return i; } return -1; }
static void flag_status(q3n_mission_hud *o,rectDef_t r,bool blue,int background)
{
    const q3n_command_state *s=o->commands; const q3n_media_view *m=q3n_media_read(o->frame->media); float color[4]={blue?0:1,0,blue?1:0,1};
    if(s->game_type!=4&&s->game_type!=5) { if(s->game_type==7) { tint(o,color); pic(o,r,m->graphics[blue?Q3N_G_BLUE_CUBE_ICON:Q3N_G_RED_CUBE_ICON]); tint(o,NULL); } return; }
    if(background)pic(o,r,background); else if(powerup(blue?8:7)) { int status=blue?s->blue_flag:s->red_flag;
        tint(o,color); pic(o,r,m->flag_status_shaders[status>=0&&status<=2?status:0]); tint(o,NULL); }
}
static void skulls(q3n_mission_hud *o,rectDef_t r,float scale,float color[4],bool flat,int style)
{
    if(o->commands->game_type!=7)return; const char *value=q3menu_format("%d",o->frame->local_player.generic1>99?99:o->frame->local_player.generic1);
    q3nm_text(r.x+r.w-q3nm_width(value,scale,0),r.y+r.h,scale,color,value,0,0,style);
    if(!o->settings.draw_icons)return; bool red=o->frame->local_player.persistant[3]==2; const q3n_media_view *m=q3n_media_read(o->frame->media);
    if(!flat&&o->settings.draw_3d_icons)q3nm_result(q3nh_model(&o->draw,r.x,r.y,35,35,m->graphics[red?Q3N_G_RED_CUBE:Q3N_G_BLUE_CUBE],0,
        qa_v3(90,0,-10),qa_v3(0,(o->frame->time&2047)*360.0f/2048,0)));
    else pic(o,(rectDef_t){r.x+3,r.y+16,20,20},m->graphics[red?Q3N_G_RED_CUBE_ICON:Q3N_G_BLUE_CUBE_ICON]);
}
static void flag(q3n_mission_hud *o,rectDef_t r,bool flat)
{ int adj=flat?0:2; r.x+=adj; r.y+=adj; r.w-=adj; r.h-=adj; const qa_q3_player *p=&o->frame->local_player;
    int team=p->powerups[7]?1:p->powerups[8]?2:p->powerups[9]?0:-1;
    if(team>=0)q3nm_result(q3nh_flag(&o->draw,r.x,r.y,r.w,r.h,team,flat)); }
static void held_item(q3n_mission_hud *o,rectDef_t r,bool persistent)
{ if(persistent&&o->commands->game_type<4)return; int index=o->frame->local_player.stats[persistent?2:1]; if(!index)return;
    q3nm_result(q3n_media_register_item(o->frame->media,(uint32_t)index,o->menus->error));
    if(!persistent)q3nm_result(q3n_media_register_item(o->frame->media,(uint32_t)index,o->menus->error));
    if(index>=0&&index<256)pic(o,r,q3n_media_read(o->frame->media)->items[index].icon); }
static void area_powerups(q3n_mission_hud *o,rectDef_t r,int align,float special,float scale,float color[4])
{
    const qa_q3_player *p=&o->frame->local_player; if(p->stats[0]<=0)return; int sorted[16],remaining[16],count=0;
    for(int i=0;i<16;++i) { int rem=q3ne_sub(p->powerups[i],o->frame->time); if(!p->powerups[i]||rem<=0||rem>=999000)continue;
        int j=0; while(j<count&&remaining[j]<rem)++j; for(int k=count;k>j;--k) { sorted[k]=sorted[k-1]; remaining[k]=remaining[k-1]; }
        sorted[j]=i; remaining[j]=rem; ++count; }
    float x=r.x,y=r.y;
    for(int i=0;i<count;++i) { const qa_q3_item *item=powerup(sorted[i]); if(!item)continue;
        if(remaining[i]>=5000)tint(o,NULL); else { float alpha=remaining[i]/1000.0f; alpha-=truncf(alpha); float c[4]={alpha,alpha,alpha,alpha}; tint(o,c); }
        item_picture(o,(rectDef_t){x,y,r.w*0.75f,r.h},item);
        q3nm_text(x+r.w*0.75f+3,y+r.h,scale,color,q3menu_format("%d",remaining[i]/1000),0,0,0);
        if(!align)y+=r.w+special; else x+=r.w+special;
    }
    tint(o,NULL);
}
static void team_info(q3n_mission_hud *o,rectDef_t r,float text_y,float scale,float color[4])
{
    int count=o->commands->num_sorted_team_players,team=o->frame->local_player.persistant[3]; if(count>8)count=8;
    for(int i=0;i<count;++i) { const q3n_client_info *ci=q3nm_client(o,o->commands->sorted_team_players[i]); if(ci&&ci->info_valid&&ci->team==team)(void)q3nm_width(ci->name,scale,0); }
    for(int i=1;i<64;++i) { const char *value; uint64_t rev; if(qa_native_q3_wire_reader_configstring(o->frame->reader,608u+(uint32_t)i,&value,&rev,o->menus->error)) {
        if(*value)(void)q3nm_width(value,scale,0); } else q3nm_result(false); }
    float y=r.y;
    for(int i=0;i<count;++i) {
        const q3n_client_info *ci=q3nm_client(o,o->commands->sorted_team_players[i]); if(!ci||!ci->info_valid||ci->team!=team)continue;
        int x=(int)(r.x+1); for(int j=0;j<=16;++j)if(ci->dynamic.powerups&(1<<j)) { const qa_q3_item *item=powerup(j);
            if(item) { item_picture(o,(rectDef_t){(float)x,y,12,12},item); x+=12; } }
        x=(int)(r.x+38); float health[4]; q3nh_health(ci->dynamic.health,ci->dynamic.armor,health); tint(o,health);
        pic(o,(rectDef_t){(float)x,y+1,10,10},q3n_media_read(o->frame->media)->graphics[Q3N_G_HEART]); x+=13; tint(o,NULL);
        if(!order_blink(o))pic(o,(rectDef_t){(float)x,y,12,12},q3nm_status(o,o->order_pending?o->current_order:ci->team_task)); x+=13;
        float left=r.w-x,max=x+left/3; q3nm_limit(ci->name,(float)x,y+text_y,scale,color,max,0); x=(int)(x+left/3+2);
        q3nm_limit(q3nm_location(o,ci->dynamic.location),(float)x,y+text_y,scale,color,r.w-4,0);
        y+=text_y+2; if(y+text_y+2>r.y+r.h)break;
    }
}
static void spectators(q3n_mission_hud *o,rectDef_t r,float scale,float color[4])
{
    int length=o->commands->spectator_length; if(!length)return;
    if(o->spectator_length!=length) { o->spectator_length=length; o->spectator_width=-1; }
    if(o->spectator_width==-1) { o->spectator_width=0; o->spectator_paint_x=(int)(r.x+1); o->spectator_paint_x2=-1; }
    if(o->spectator_offset>length) { o->spectator_offset=0; o->spectator_paint_x=(int)(r.x+1); o->spectator_paint_x2=-1; }
    const char *value=o->commands->spectator_list;
    if(o->frame->time>o->spectator_time) {
        o->spectator_time=q3ne_plus(o->frame->time,10);
        if(o->spectator_paint_x<=r.x+2) { if(o->spectator_offset<length) { o->spectator_paint_x+=q3nm_width(value+o->spectator_offset,scale,1)-1; ++o->spectator_offset; }
            else { o->spectator_offset=0; o->spectator_paint_x=o->spectator_paint_x2>=0?o->spectator_paint_x2:(int)(r.x+r.w-2); o->spectator_paint_x2=-1; } }
        else { --o->spectator_paint_x; if(o->spectator_paint_x2>=0)--o->spectator_paint_x2; }
    }
    float maximum=r.x+r.w-2,baseline=r.y+r.h-3;
    float end=q3nm_limit(value+o->spectator_offset,(float)o->spectator_paint_x,baseline,scale,color,maximum,0);
    if(o->spectator_paint_x2>=0)q3nm_limit(value,(float)o->spectator_paint_x2,baseline,scale,color,maximum,o->spectator_offset);
    if(o->spectator_offset&&end>0) { if(o->spectator_paint_x2==-1)o->spectator_paint_x2=(int)maximum; } else o->spectator_paint_x2=-1;
}
static void medal(q3n_mission_hud *o,int id,rectDef_t r,float scale,const float input[4],int picture)
{
    if(o->selected_score<0||o->selected_score>=64) { q3nm_result(q3ne_fail(o->menus->error,QA_ERROR_FORMAT,"Mission medal selected score is outside source array")); return; }
    const q3n_command_score *s=&o->commands->scores[o->selected_score]; int value=id==CG_ACCURACY?s->accuracy:id==CG_ASSISTS?s->assist:
        id==CG_DEFEND?s->defend:id==CG_EXCELLENT?s->excellent:id==CG_IMPRESSIVE?s->impressive:id==CG_PERFECT?s->perfect:id==CG_GAUNTLET?s->gauntlet:s->captures;
    float color[4]; memcpy(color,input,sizeof(color)); color[3]=0.25f; const char *value_text=NULL;
    if(value>0) { value_text=id==CG_PERFECT?"Wow":id==CG_ACCURACY?q3menu_format("%d%%",value):q3menu_format("%d",value);
        if(id!=CG_ACCURACY||value>50)color[3]=1; }
    tint(o,color); pic(o,r,picture);
    if(value_text) { color[3]=1; q3nm_text(r.x+(r.w-q3nm_width(value_text,scale,0))/2,r.y+r.h+10,scale,color,value_text,0,0,0); }
    tint(o,NULL);
}
void q3nm_owner(float x,float y,float w,float h,float text_x,float text_y,int id,int flags,int align,
    float special,float scale,float color[4],int picture,int style)
{
    (void)text_x; q3n_mission_hud *o=q3nm_active(); if(o->menus->failed||!o->settings.draw_status||!o->commands)return;
    rectDef_t r={x,y,w,h}; bool flat=(flags&CG_SHOW_2DONLY)!=0; const q3n_media_view *m=q3n_media_read(o->frame->media);
    const qa_q3_player *p=&o->frame->local_player; const q3n_client_info *ci; const char *value;
    switch(id) {
    case CG_PLAYER_ARMOR_ICON:armor(o,r,flat); break; case CG_PLAYER_ARMOR_ICON2D:armor(o,r,true); break;
    case CG_PLAYER_AMMO_ICON:ammo(o,r,flat); break; case CG_PLAYER_AMMO_ICON2D:ammo(o,r,true); break;
    case CG_PLAYER_AMMO_VALUE:if(!o->options.shared_weapon_hud&&entity_weapon(o)&&q3nm_value(id)>-1)number(o,r,scale,color,(int)q3nm_value(id),picture,style); break;
    case CG_PLAYER_ARMOR_VALUE:case CG_PLAYER_HEALTH:case CG_PLAYER_SCORE:case CG_SELECTEDPLAYER_HEALTH:number(o,r,scale,color,(int)q3nm_value(id),picture,style); break;
    case CG_SELECTEDPLAYER_ARMOR:if(q3nm_value(id)>0)number(o,r,scale,color,(int)q3nm_value(id),picture,style); break;
    case CG_SELECTEDPLAYER_HEAD:case CG_VOICE_HEAD:q3nm_result(q3nh_head(&o->draw,x,y,w,h,id==CG_VOICE_HEAD?o->commands->current_voice_client:selected_index(o),qa_v3(0,180,0))); break;
    case CG_SELECTEDPLAYER_NAME:case CG_VOICE_NAME:ci=q3nm_client(o,id==CG_VOICE_NAME?o->commands->current_voice_client:selected_index(o)); if(ci)text(r,scale,color,ci->name,style); break;
    case CG_SELECTEDPLAYER_LOCATION:ci=selected(o); if(ci)text(r,scale,color,q3nm_location(o,ci->dynamic.location),style); break;
    case CG_PLAYER_LOCATION:ci=q3nm_client(o,p->clientNum); if(ci)text(r,scale,color,q3nm_location(o,ci->dynamic.location),style); break;
    case CG_SELECTEDPLAYER_STATUS:ci=selected(o); if(ci&&!order_blink(o))pic(o,r,q3nm_status(o,o->order_pending?o->current_order:ci->team_task)); break;
    case CG_PLAYER_STATUS:ci=q3nm_client(o,p->clientNum); if(ci)pic(o,r,q3nm_status(o,ci->team_task)); break;
    case CG_SELECTEDPLAYER_WEAPON:ci=selected(o); if(ci) { int weapon=ci->dynamic.cur_weapon; int icon=weapon>=0&&weapon<16?m->weapons[weapon].weapon_icon:0; pic(o,r,icon?icon:m->graphics[Q3N_G_DEFER]); } break;
    case CG_SELECTEDPLAYER_POWERUP:ci=selected(o); if(ci)for(int j=0;j<16;++j)if(ci->dynamic.powerups&(1<<j)) { const qa_q3_item *item=powerup(j); if(item) { item_picture(o,r,item); break; } } break;
    case CG_PLAYER_HEAD:player_head(o,r); break; case CG_PLAYER_ITEM:held_item(o,r,false); break; case CG_CTF_POWERUP:held_item(o,r,true); break;
    case CG_RED_SCORE:case CG_BLUE_SCORE:{ int score=id==CG_RED_SCORE?o->commands->scores1:o->commands->scores2; value=score==-9999?"-":q3menu_format("%d",score);
        q3nm_text(x+w-q3nm_width(value,scale,0),y+h,scale,color,value,0,0,style); break; }
    case CG_RED_NAME:case CG_BLUE_NAME:text(r,scale,color,team_name(o,id==CG_BLUE_NAME),style); break;
    case CG_BLUE_FLAGHEAD:case CG_RED_FLAGHEAD:if(carrier(o,id==CG_BLUE_FLAGHEAD)>=0)q3nm_result(q3nh_head(&o->draw,x,y,w,h,0,qa_v3(0,180+20*sinf(o->frame->time/650.0f),0))); break;
    case CG_BLUE_FLAGSTATUS:case CG_RED_FLAGSTATUS:flag_status(o,r,id==CG_BLUE_FLAGSTATUS,picture); break;
    case CG_BLUE_FLAGNAME:case CG_RED_FLAGNAME:{ int n=carrier(o,id==CG_BLUE_FLAGNAME); if(n>=0) { ci=q3nm_client(o,n); if(ci)text(r,scale,color,ci->name,style); } break; }
    case CG_HARVESTER_SKULLS:case CG_HARVESTER_SKULLS2D:skulls(o,r,scale,color,id==CG_HARVESTER_SKULLS2D,style); break;
    case CG_ONEFLAG_STATUS:{ int status=o->commands->flag_status; if(o->commands->game_type==5&&powerup(9)&&status>=0&&status<=4) {
        float c[4]={status==3?0:1,status==2||status==3?0:1,status==2?0:1,1}; tint(o,c); pic(o,r,m->flag_status_shaders[status==2||status==3?1:status==4?2:0]); } break; }
    case CG_TEAM_COLOR:q3nm_result(q3nh_team_background(&o->draw,x,y,w,h,color[3],p->persistant[3])); break;
    case CG_AREA_POWERUP:area_powerups(o,r,align,special,scale,color); break;
    case CG_PLAYER_HASFLAG:case CG_PLAYER_HASFLAG2D:flag(o,r,id==CG_PLAYER_HASFLAG2D); break;
    case CG_AREA_SYSTEMCHAT:text(r,scale,color,o->system_chat,0); break; case CG_AREA_TEAMCHAT:text(r,scale,color,o->team_chat[0],0); break; case CG_AREA_CHAT:text(r,scale,color,o->team_chat[1],0); break;
    case CG_GAME_TYPE:text(r,scale,color,type_text(o),style); break; case CG_GAME_STATUS:text(r,scale,color,status_text(o),style); break;
    case CG_KILLER:value=killer(o); if(*value)q3nm_text((int)(x+w/2)-q3nm_width(value,scale,0)/2,y+h,scale,color,value,0,0,style); break;
    case CG_ACCURACY:case CG_ASSISTS:case CG_DEFEND:case CG_EXCELLENT:case CG_IMPRESSIVE:case CG_PERFECT:case CG_GAUNTLET:case CG_CAPTURES:medal(o,id,r,scale,color,picture); break;
    case CG_SPECTATORS:spectators(o,r,scale,color); break;
    case CG_TEAMINFO:if(q3nm_integer(o,"cg_currentSelectedPlayer")==o->commands->num_sorted_team_players)team_info(o,r,text_y,scale,color); break;
    case CG_CAPFRAGLIMIT:q3nm_text(x,y,scale,color,q3menu_format("%2d",o->commands->game_type>=4?o->commands->capturelimit:o->commands->fraglimit),0,0,style); break;
    case CG_1STPLACE:case CG_2NDPLACE:{ int score=id==CG_1STPLACE?o->commands->scores1:o->commands->scores2; if(score!=-9999)q3nm_text(x,y,scale,color,q3menu_format("%2d",score),0,0,style); break; }
    }
}
static bool pending_order(q3n_mission_hud *o,qa_error *e)
{
    if(!o->commands)return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission order requires actual Team command state");
    if(o->commands->game_type<4||!o->order_pending)return true; bool ok=true;
    static const char *team[]={"offense","defend","patrol","followme","returnflag","followflagcarrier","camp"};
    static const char *personal[]={"onoffense","ondefense","onpatrol","onfollow","ongetflag","onfollowcarrier","oncamping"};
    static const char *buttons[]={"+button7; wait; -button7","+button8; wait; -button8","+button9; wait; -button9","+button10; wait; -button10",NULL,NULL,NULL};
    int index=q3nm_integer(o,"cg_currentSelectedPlayer");
    {
        int order=o->current_order-1; bool valid=order>=0&&order<7; const char *command=NULL;
        if(index==o->commands->num_sorted_team_players) { if(valid)command=q3menu_format("cmd vsay_team %s\n",team[order]); else ok=q3ne_fail(e,QA_ERROR_FORMAT,"Everyone order has no source voice command"); }
        else if(index>=0&&index<8) { int client=o->commands->sorted_team_players[index];
            if(client==o->frame->local_player.clientNum&&valid) { ok=qa_native_q3_client_console(o->options.client,q3menu_format("teamtask %d\n",o->current_order),e); command=q3menu_format("cmd vsay_team %s\n",personal[order]); }
            else if(valid)command=q3menu_format("cmd vtell %d %s\n",client,team[order]); }
        else ok=q3ne_fail(e,QA_ERROR_FORMAT,"Mission order selected player leaves source team array");
        if(ok&&command)ok=qa_native_q3_client_console(o->options.client,command,e);
        if(ok&&valid&&buttons[order])ok=qa_native_q3_client_console(o->options.client,buttons[order],e); if(ok)o->order_pending=false;
    }
    return ok;
}
bool q3n_mission_hud_check_order(q3n_mission_hud *o,const q3n_frame *f,qa_error *e)
{ q3menu_context *previous; if(!q3nm_begin(o,f,e,&previous))return false;
    bool ok=!o->order_pending||f->time<=o->order_time||pending_order(o,e); return q3nm_end(o,previous,ok); }
bool q3n_mission_hud_next_order(q3n_mission_hud *o,const q3n_frame *f,qa_error *e)
{
    q3menu_context *previous; if(!q3nm_begin(o,f,e,&previous))return false;
    if(!o->commands)return q3nm_end(o,previous,q3ne_fail(e,QA_ERROR_ARGUMENT,"Next Mission order requires actual Team command state"));
    const q3n_client_info *ci=q3nm_client(o,f->local_player.clientNum); if(!ci)return q3nm_end(o,previous,false);
    if(!ci->team_leader) { int index=q3nm_integer(o,"cg_currentSelectedPlayer");
        if(index<0||index>=8)return q3nm_end(o,previous,q3ne_fail(e,QA_ERROR_FORMAT,"Next Mission order selected player leaves source team array"));
        if(o->commands->sorted_team_players[index]!=f->local_player.clientNum)return q3nm_end(o,previous,true); }
    if(o->current_order<7) { o->current_order=q3ne_plus(o->current_order,1);
        if(o->current_order==5&&!team_flag(o,false))o->current_order=q3ne_plus(o->current_order,1);
        if(o->current_order==6&&!team_flag(o,true))o->current_order=q3ne_plus(o->current_order,1); }
    else o->current_order=1;
    o->order_pending=true; o->order_time=q3ne_plus(f->time,3000); return q3nm_end(o,previous,true);
}
bool q3n_mission_hud_select(q3n_mission_hud *o,const q3n_frame *f,bool next,qa_error *e)
{
    q3menu_context *previous; if(!q3nm_begin(o,f,e,&previous))return false; bool ok=pending_order(o,e);
    int index=q3nm_integer(o,"cg_currentSelectedPlayer");
    if(ok) {
        int count=o->commands->num_sorted_team_players; index=next?(index>=0&&index<count?index+1:0):(index>0&&index<count?index-1:count);
        ok=qa_native_q3_client_cvar_integer(o->options.client,"cg_currentSelectedPlayer",index,e);
        if(ok&&index>=0&&index<count) { int client=o->commands->sorted_team_players[index]; const q3n_client_info *ci=q3nm_client(o,client);
            ok=ci&&q3nm_set(o,"cg_selectedPlayerName",ci->name)&&q3nm_set(o,"cg_selectedPlayer",q3menu_format("%d",client)); if(ci)o->current_order=ci->team_task; }
        else if(ok)ok=q3nm_set(o,"cg_selectedPlayerName","Everyone");
    }
    return q3nm_end(o,previous,ok);
}
