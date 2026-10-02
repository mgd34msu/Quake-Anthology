/* CG active HUD composition, id Software 1999-2005, GPL-2.0-or-later. */
#include "hud_internal.h"

bool q3n_hud_create(const q3n_hud_options *options,q3n_hud **out,qa_error *e)
{
    if(!options || !out || *out || !options->assets || !options->source || !options->application ||
       options->remote_client || options->compiled_source || !options->ui || !options->milliseconds || !options->load_deferred || !options->client_command ||
       !qa_application_native_q3_presentation_current(options->application,options->source) ||
       (options->source->product==QA_Q3_TEAM_ARENA && (!options->mission_paint || !options->mission_order || !options->mission_timed ||
        !options->mission_text || !options->mission_center_line)))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 HUD requires actual source, registered assets and authored drawing owners");
    q3n_hud *o=calloc(1,sizeof(*o)); if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating native Q3 HUD");
    o->options=*options; o->options.source=NULL; o->source_game=options->source->source_game; o->product=options->source->product;
    o->state.scoreboard_first_time=true; *out=o; return true;
}
bool q3n_hud_create_restored(const q3n_hud_options *options,q3n_hud **out,qa_error *e)
{
    qa_native_q3_client_basis basis;
    if(!options || !out || *out || !options->assets || options->remote_client || options->compiled_source || !options->ui || !options->milliseconds || !options->load_deferred ||
       !options->client_command || !options->client || !qa_native_q3_client_basis_read(options->client,&basis,e) ||
       basis.application!=options->application || basis.seat!=options->seat ||
       (basis.product==QA_Q3_TEAM_ARENA && (!options->mission_paint || !options->mission_order || !options->mission_timed || !options->mission_text || !options->mission_center_line)))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Restored Q3 HUD requires its actual installed source, client and drawing owners");
    q3n_hud *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating restored native Q3 HUD");
    o->options=*options; o->options.source=NULL; o->source_game=basis.source_game; o->product=basis.product; *out=o; return true;
}
bool q3n_hud_create_remote(const q3n_hud_options *options,q3n_hud **out,qa_error *e)
{
    qa_native_q3_remote_client_basis basis;
    if(!options || !out || *out || !options->assets || options->source || options->client || options->compiled_source || !options->ui ||
       !options->milliseconds || !options->load_deferred || !options->client_command || !options->oldest_command || !options->remote_client ||
       !qa_native_q3_remote_client_basis_read(options->remote_client,&basis,e) ||
       basis.application!=options->application || basis.client.seat!=options->seat ||
       (basis.product==QA_Q3_TEAM_ARENA && (!options->mission_paint || !options->mission_order || !options->mission_timed ||
        !options->mission_text || !options->mission_center_line)))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote Q3 HUD requires its actual retained CLIENT and drawing owners");
    q3n_hud *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating remote native Q3 HUD");
    o->options=*options; o->product=basis.product; o->state.scoreboard_first_time=true; *out=o; return true;
}
bool q3n_hud_create_compiled(const q3n_hud_options *options,q3n_hud **out,qa_error *e)
{
    q3n_compiled_source_view source;
    if(!options || !out || *out || !options->compiled_source || options->source || options->client || options->remote_client ||
       !options->ui || !options->milliseconds || !options->load_deferred || !options->client_command || !options->compiled_oldest_command ||
       !q3n_compiled_source_checkpoint_read(options->compiled_source,&source,e) || source.basis.application!=options->application ||
       source.basis.assets!=options->assets || source.basis.seat!=options->seat ||
       (source.basis.product==QA_Q3_TEAM_ARENA && (!options->mission_paint || !options->mission_order ||
        !options->mission_timed || !options->mission_text || !options->mission_center_line)))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled HUD requires its actual CLIENT source and drawing owners");
    q3n_hud *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating compiled Q3 HUD");
    o->options=*options; o->product=source.basis.product; o->state.scoreboard_first_time=true; *out=o; return true;
}
void q3n_hud_destroy(q3n_hud *o) { if(o && !o->busy)free(o); }
bool q3n_hud_idle(const q3n_hud *o) { return o && !o->busy; }
const q3n_hud_state *q3n_hud_read(const q3n_hud *o) { return o?&o->state:NULL; }
bool q3n_hud_weapon_read(q3n_hud *o,const q3n_frame *f,q3n_weapon_hud *out,qa_error *e)
{
    if(!out || !q3nh_current(o,f,e))return false;
    q3n_weapon_hud result={0};
    if(o->options.weapon_warning && (!o->options.weapon_warning(o->options.context,f,&result,e) ||
       !q3nh_current(o,f,e)))return false;
    if(result.selected && (result.warning<0 || result.warning>2))
        return q3ne_fail(e,QA_ERROR_FORMAT,"Shared arsenal warning is outside its actual domain");
    *out=result; return true;
}
static bool center_current(q3n_hud *o,const q3n_frame *f,qa_error *e)
{
    if(f && f->compiled)return o && o->options.compiled_source==f->compiled->source.owner &&
        o->options.application==f->application && o->options.assets==f->assets && o->options.seat==f->seat &&
        o->product==q3n_frame_product(f) && f->compiled->source.basis.initialized && q3ne_current(f,e);
    if(!f || !f->remote)return q3nh_current(o,f,e);
    qa_native_q3_remote_client_basis actual;
    return o && o->options.application==f->application && o->options.assets==f->assets &&
        o->options.seat==f->seat && o->product==q3n_frame_product(f) &&
        o->options.remote_client==f->remote->client && f->remote->source.basis.client.initialized &&
        qa_native_q3_remote_client_basis_read(f->remote->client,&actual,e) && actual.client.initialized &&
        q3n_frame_current(f) && q3ne_current(f,e)?true:
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Center print requires its actual initialized CLIENT and entered frame");
}
bool q3n_hud_center_print(q3n_hud *o,const q3n_frame *f,const char *text,int32_t y,int32_t width,qa_error *e)
{
    if(!text || !o || o->busy || !center_current(o,f,e))return false;
    q3n_hud_state *s=&o->state; snprintf(s->center_print,sizeof(s->center_print),"%s",text);
    s->center_print_time=f->time; s->center_print_y=y; s->center_print_char_width=width; s->center_print_lines=1;
    for(const char *p=s->center_print;*p;++p)if(*p=='\n')++s->center_print_lines;
    return true;
}
void q3n_hud_scores(q3n_hud *o,bool show,int32_t time)
{ if(o && !o->busy) { if(o->state.show_scores && !show)o->state.score_fade_time=time; o->state.show_scores=show; } }
bool q3n_hud_scores_request(q3n_hud *o,const q3n_frame *f,bool *due,qa_error *e)
{
    if(!o || o->busy || !f || !due || o->options.application!=f->application ||
       o->options.assets!=f->assets || o->options.seat!=f->seat || o->product!=q3n_frame_product(f) ||
       (f->compiled?o->options.compiled_source!=f->compiled->source.owner:
        f->remote?o->options.remote_client!=f->remote->client || o->options.compiled_source:
        o->options.remote_client || o->options.compiled_source || o->source_game!=f->source.source_game || f->time!=f->source.source_time_ms) ||
       !q3n_frame_current(f) || !q3ne_current(f,e))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Score request requires its idle HUD and actual entered CLIENT clock");
    *due=q3ne_plus(o->scores_request_time,2000)<f->time;
    if(*due)o->scores_request_time=f->time;
    return true;
}
void q3n_hud_frame_sample(q3n_hud *o,int32_t offset)
{ if(o && !o->busy) { o->frame_samples[(uint32_t)o->frame_count&127u]=offset; o->frame_count=q3ne_plus(o->frame_count,1); } }
void q3n_hud_snapshot_sample(q3n_hud *o,bool dropped,int32_t ping,int32_t flags)
{ if(o && !o->busy) { unsigned i=(uint32_t)o->snapshot_count&127u; o->snapshot_samples[i]=dropped?-1:ping;
    if(!dropped)o->snapshot_flags[i]=flags;
    o->snapshot_count=q3ne_plus(o->snapshot_count,1); } }
void q3n_hud_disconnect_command(q3n_hud *o,int32_t time)
{ if(o && !o->busy) { o->oldest_command_time=time; o->has_oldest_command=true; } }
static bool center_string(q3n_hud_draw *d)
{
    q3n_hud_state *s=&d->owner->state; float color[4];
    if(!q3nh_fade(d->frame->time,s->center_print_time,q3ne_int(q3ne_mul(1000,d->settings->center_time)),color))return true;
    q3nh_anchor(d,320,240);
    float y=(float)s->center_print_y-(float)s->center_print_lines*8*d->frame->preferences.text_scale; const char *start=s->center_print;
    for(;;) {
        const char *end=start; while(*end && *end!='\n')++end;
        size_t length=(size_t)(end-start); if(length>50)length=50; char line[51]; memcpy(line,start,length); line[length]=0;
        if(d->owner->product==QA_Q3_TEAM_ARENA) {
            float height;
            if(!d->owner->options.mission_center_line(d->owner->options.context,d->frame,line,y,color,&height,d->error) ||
               !q3nh_current(d->owner,d->frame,d->error))return false;
            y=q3ne_add(y,q3ne_add(height,6));
        } else {
            int32_t height=q3ne_int(q3ne_mul((float)s->center_print_char_width,1.5f));
            float width;
            if(!q3nh_width(d,line,(float)s->center_print_char_width,(float)height,0,&width) ||
               !q3nh_text(d,(640-width)/2,y,line,
               (float)s->center_print_char_width,(float)height,color,false,true,0))return false;
            y=(float)q3ne_int(q3ne_add(y,q3ne_mul(q3ne_mul((float)s->center_print_char_width,1.5f),d->frame->preferences.text_scale)));
        }
        if(!*end)break;
        start=end+1;
    }
    return q3nh_color(d,NULL);
}
static bool status_head(q3n_hud_draw *d,float x)
{
    q3n_hud_state *s=&d->owner->state; const q3n_player_feedback *g=q3n_player_state_feedback(d->player);
    int32_t time=d->frame->time; float size=60;
    if(g->damage_time && q3ne_add((float)time,-g->damage_time)<500) {
        float fraction=q3ne_div(q3ne_add((float)time,-g->damage_time),500);
        size=q3ne_mul(60,q3ne_add(1.5f,-q3ne_mul(fraction,0.5f))); float stretch=q3ne_add(size,-60);
        x=q3ne_add(x,-q3ne_add(q3ne_mul(stretch,0.5f),q3ne_mul(q3ne_mul(g->damage_x,stretch),0.5f)));
        s->head_start_yaw=q3ne_add(180,q3ne_mul(g->damage_x,45));
        s->head_end_yaw=q3ne_add(180,q3ne_mul(20,(float)cos((double)q3ne_mul(q3n_events_crandom(d->frame->events),3.14159274101257324219f))));
        s->head_end_pitch=q3ne_mul(5,(float)cos((double)q3ne_mul(q3n_events_crandom(d->frame->events),3.14159274101257324219f)));
        s->head_start_time=time; s->head_end_time=q3ne_int(q3ne_add((float)q3ne_plus(time,100),q3ne_mul(q3n_events_random(d->frame->events),2000)));
    } else if(time>=s->head_end_time) {
        s->head_start_yaw=s->head_end_yaw; s->head_start_pitch=s->head_end_pitch; s->head_start_time=s->head_end_time;
        s->head_end_time=q3ne_int(q3ne_add((float)q3ne_plus(time,100),q3ne_mul(q3n_events_random(d->frame->events),2000)));
        s->head_end_yaw=q3ne_add(180,q3ne_mul(20,(float)cos((double)q3ne_mul(q3n_events_crandom(d->frame->events),3.14159274101257324219f))));
        s->head_end_pitch=q3ne_mul(5,(float)cos((double)q3ne_mul(q3n_events_crandom(d->frame->events),3.14159274101257324219f)));
    }
    if(s->head_start_time>time)s->head_start_time=time;
    float fraction=q3ne_div((float)q3ne_sub(time,s->head_start_time),(float)q3ne_sub(s->head_end_time,s->head_start_time));
    fraction=q3ne_mul(q3ne_mul(fraction,fraction),q3ne_add(3,-q3ne_mul(2,fraction)));
    qa_vec3 angles=qa_v3(q3ne_add(s->head_start_pitch,q3ne_mul(q3ne_add(s->head_end_pitch,-s->head_start_pitch),fraction)),
        q3ne_add(s->head_start_yaw,q3ne_mul(q3ne_add(s->head_end_yaw,-s->head_start_yaw),fraction)),0);
    return q3nh_head(d,x,q3ne_add(480,-size),size,size,q3n_frame_snapshot_player(d->frame)->clientNum,angles);
}
static bool status_bar(q3n_hud_draw *d)
{
    q3nh_anchor(d,320,480);
    if(!d->settings->draw_status)return true;
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame),*predicted=q3n_frame_predicted_player(d->frame);
    const q3n_media_view *m=q3n_media_read(d->frame->media);
    qa_q3_entity actual; q3n_entity *cent; bool present;
    if(p->clientNum<0 || p->clientNum>=64 || !q3n_frame_entity(d->frame,
        (uint32_t)p->clientNum,&actual,&cent,&present,d->error) || !present)return false;
    int32_t weapon=actual.weapon;
    if(weapon<0 || weapon>=16 || predicted->weapon<0 || predicted->weapon>=16)return q3ne_fail(d->error,QA_ERROR_FORMAT,"HUD actual source weapon is invalid");
    if(!q3nh_team_background(d,0,420,640,60,0.33f,p->persistant[3]))return false;
    if(!d->weapon_hud.selected && weapon && m->weapons[weapon].ammo_model && !q3nh_model(d,100,432,48,48,m->weapons[weapon].ammo_model,0,
       qa_v3(70,0,0),qa_v3(0,q3ne_add(90,q3ne_mul(20,(float)sin((double)q3ne_div((float)d->frame->time,1000)))),0)))return false;
    if(!status_head(d,285))return false;
    if(predicted->powerups[7]) { if(!q3nh_flag(d,333,432,48,48,1,false))return false; }
    else if(predicted->powerups[8]) { if(!q3nh_flag(d,333,432,48,48,2,false))return false; }
    else if(predicted->powerups[9] && !q3nh_flag(d,333,432,48,48,0,false))return false;
    int32_t armor=p->stats[q3nh_armor_stat(d->owner->product)];
    if(armor && !q3nh_model(d,470,432,48,48,m->graphics[Q3N_G_ARMOR],0,qa_v3(90,0,-10),
       qa_v3(0,q3ne_div(q3ne_mul((float)(d->frame->time&2047),360),2048),0)))return false;
    if(!d->weapon_hud.selected && weapon && p->ammo[weapon]>-1) {
        const float firing[4]={0.5f,0.5f,0.5f,1};
        if(!q3nh_color(d,predicted->weaponState==3 && predicted->weaponTime>100?firing:q3nh_normal) ||
           !q3nh_field(d,0,432,3,p->ammo[weapon]) || !q3nh_color(d,NULL))return false;
        if(!d->settings->draw_3d_icons && d->settings->draw_icons && m->weapons[predicted->weapon].ammo_icon &&
           !q3nh_picture(d,100,432,48,48,m->weapons[predicted->weapon].ammo_icon))return false;
    }
    float low[4]={1,0.2f,0.2f,1},health_color[4]; int32_t health=p->stats[0];
    const float *color=health>100?q3nh_white:health>25?q3nh_normal:health>0?
        d->frame->preferences.reduced_flashes?low:((d->frame->time>>8)&1)?low:q3nh_normal:low;
    if(!q3nh_color(d,color) || !q3nh_field(d,185,432,3,health))return false;
    q3nh_health(health,armor,health_color); if(!q3nh_color(d,health_color))return false;
    if(armor>0) {
        if(!q3nh_color(d,q3nh_normal) || !q3nh_field(d,370,432,3,armor) || !q3nh_color(d,NULL))return false;
        if(!d->settings->draw_3d_icons && d->settings->draw_icons && !q3nh_picture(d,470,432,48,48,m->graphics[Q3N_G_ARMOR_ICON]))return false;
    }
    return true;
}
static bool crosshair(q3n_hud_draw *d)
{
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame); const q3n_hud_settings *s=d->settings;
    if(!d->frame->preferences.crosshair || !s->crosshair || p->persistant[3]==3 || d->frame->third_person)return true;
    float color[4]; q3nh_health(p->stats[0],p->stats[q3nh_armor_stat(d->owner->product)],color);
    if(!q3nh_color(d,s->crosshair_health?color:NULL))return false;
    float size=q3ne_mul(s->crosshair_size,q3ne_div(d->frame->preferences.crosshair_size,8));
    int32_t elapsed=q3ne_sub(d->frame->time,q3n_events_state(d->frame->events)->item_pickup_blend_time);
    if(!d->frame->preferences.reduced_flashes && elapsed>0 && elapsed<200)size=q3ne_mul(size,q3ne_add(1,q3ne_div((float)elapsed,200)));
    float w=q3ne_mul(size,q3ne_div((float)d->viewport.width,640)),h=q3ne_mul(size,q3ne_div((float)d->viewport.height,480));
    float x=q3ne_add(q3ne_add(q3ne_mul((float)s->crosshair_x,q3ne_div((float)d->viewport.width,640)),(float)d->frame->refdef.x),
        q3ne_mul(0.5f,q3ne_add((float)d->frame->refdef.width,-w)));
    float y=q3ne_add(q3ne_add(q3ne_mul((float)s->crosshair_y,q3ne_div((float)d->viewport.height,480)),(float)d->frame->refdef.y),
        q3ne_mul(0.5f,q3ne_add((float)d->frame->refdef.height,-h)));
    int32_t index=(s->crosshair<0?0:s->crosshair)%10;
    if(d->frame->preferences.high_contrast) {
        float black[4]={0,0,0,1};
        if(!q3nh_color(d,black) || !q3nh_pixels(d,x-2,y-2,w+4,h+4,q3n_media_read(d->frame->media)->crosshairs[index],
           (qa_scene_vec4){0,0,1,1}) || !q3nh_color(d,q3nh_white))return false;
    }
    return q3nh_pixels(d,x,y,w,h,q3n_media_read(d->frame->media)->crosshairs[index],(qa_scene_vec4){0,0,1,1});
}
static bool crosshair_names(q3n_hud_draw *d)
{
    if(!d->frame->preferences.crosshair || !d->settings->crosshair || !d->settings->draw_crosshair_names || d->frame->third_person)return true;
    q3nh_anchor(d,320,240);
    qa_vec3 start=d->frame->refdef.origin,end=q3ne_sum(start,q3ne_scale(d->frame->refdef.axis[0],131072));
    qa_trace_result trace; qa_bounds zero={0}; int32_t number;
    if(!q3n_events_trace(d->frame,start,end,zero,q3n_frame_snapshot_player(d->frame)->clientNum,1|0x2000000,&trace,d->error) ||
       !q3n_events_trace_number(d->frame,&trace,&number,d->error))return false;
    if(number>=0 && number<64) {
        uint32_t contents; qa_q3_entity actual; q3n_entity *cent; bool present;
        if(!q3n_events_point_contents(d->frame,trace.end,0,&contents,d->error) ||
           !q3n_frame_entity(d->frame,(uint32_t)number,&actual,&cent,&present,d->error))return false;
        if(!(contents&64) && present && !(actual.powerups&(1<<4))) {
            d->owner->state.crosshair_client=number; d->owner->state.crosshair_client_time=d->frame->time;
        }
    }
    float color[4]; if(!q3nh_fade(d->frame->time,d->owner->state.crosshair_client_time,1000,color))return q3nh_color(d,NULL);
    const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)d->owner->state.crosshair_client); if(!ci)return false;
    color[3]=q3ne_mul(color[3],0.5f);
    bool ok=d->owner->product==QA_Q3_TEAM_ARENA?
        d->owner->options.mission_text(d->owner->options.context,d->frame,ci->name,190,0.3f,color,3,false,d->error):
        q3nh_center(d,170,ci->name,color[3]);
    return ok && q3nh_color(d,NULL);
}
static bool reward(q3n_hud_draw *d)
{
    q3nh_anchor(d,320,0);
    if(!d->settings->draw_rewards)return true;
    q3n_reward r; float alpha; bool visible;
    if(!q3n_player_state_reward(d->player,d->frame,&r,&alpha,&visible,d->error))return false;
    if(!visible)return true;
    float color[4]={1,1,1,alpha}; if(!q3nh_color(d,color))return false;
    if(r.count>=10) {
        if(!q3nh_picture(d,296,56,44,44,r.shader))return false;
        char text[32]; snprintf(text,sizeof(text),"%i",r.count);
        if(!q3nh_text(d,(640-8*(float)q3nh_strlen(text))/2,104,text,8,16,color,false,true,0))return false;
    } else for(int32_t i=0;i<r.count;++i)if(!q3nh_picture(d,(float)(320-r.count*24+i*48),56,44,44,r.shader))return false;
    return q3nh_color(d,NULL);
}
static bool votes(q3n_hud_draw *d)
{
    q3nh_anchor(d,0,0);
    const q3n_command_state *c=q3n_server_commands_state(d->commands); char text[1200];
    if(c->vote_time) {
        if(c->vote_modified && (!q3nh_sound(d,Q3N_S_TALK,6) || !q3n_server_commands_vote_drawn(d->commands,d->frame,-1,d->error)))return false;
        int32_t seconds=q3ne_sub(30000,q3ne_sub(d->frame->time,c->vote_time))/1000; if(seconds<0)seconds=0;
        snprintf(text,sizeof(text),"VOTE(%i):%s yes:%i no:%i",seconds,c->vote_string,c->vote_yes,c->vote_no);
        if(!q3nh_text(d,0,58,text,8,16,q3nh_white,false,false,0))return false;
        if(d->owner->product==QA_Q3_TEAM_ARENA && !q3nh_text(d,0,76,"or press ESC then click Vote",8,16,q3nh_white,false,false,0))return false;
    }
    /* Source uses clientinfo[0].team for the team-vote slot. */
    const q3n_client_info *ci=q3n_clients_get(d->frame->clients,0); if(!ci)return false;
    int32_t team=ci->team==1?0:ci->team==2?1:-1;
    if(team>=0 && c->team_vote_time[team]) {
        if(c->team_vote_modified[team] && (!q3nh_sound(d,Q3N_S_TALK,6) || !q3n_server_commands_vote_drawn(d->commands,d->frame,team,d->error)))return false;
        int32_t seconds=q3ne_sub(30000,q3ne_sub(d->frame->time,c->team_vote_time[team]))/1000; if(seconds<0)seconds=0;
        snprintf(text,sizeof(text),"TEAMVOTE(%i):%s yes:%i no:%i",seconds,c->team_vote_string[team],c->team_vote_yes[team],c->team_vote_no[team]);
        if(!q3nh_text(d,0,90,text,8,16,q3nh_white,false,false,0))return false;
    }
    return true;
}
static bool follow(q3n_hud_draw *d,bool *shown)
{
    q3nh_anchor(d,320,0);
    const qa_q3_player *p=q3n_frame_snapshot_player(d->frame);
    *shown=(p->pmFlags&4096)!=0; if(!*shown)return true;
    if(!q3nh_big(d,248,24,"following",1))return false;
    const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)p->clientNum); if(!ci)return false;
    float width; return q3nh_width(d,ci->name,32,48,0,&width) &&
        q3nh_text(d,0.5f*(640-width),40,ci->name,32,48,q3nh_white,true,true,0);
}
static bool warmup(q3n_hud_draw *d)
{
    q3nh_anchor(d,320,0);
    const q3n_command_state *c=q3n_server_commands_state(d->commands); if(!c->warmup)return true;
    if(c->warmup<0)return q3n_server_commands_warmup_drawn(d->commands,d->frame,c->warmup,0,d->error) && q3nh_center(d,24,"Waiting for players",1);
    char heading[160]={0},text[64]; bool draw_heading=true;
    if(c->game_type==1) {
        const q3n_client_info *first=NULL,*second=NULL;
        for(int32_t i=0;i<c->max_clients;++i) {
            const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)i);
            if(ci && ci->info_valid && ci->team==0) { if(!first)first=ci; else second=ci; }
        }
        if(first && second)snprintf(heading,sizeof(heading),"%s vs %s",first->name,second->name); else draw_heading=false;
    } else {
        const char *name=c->game_type==0?"Free For All":c->game_type==3?"Team Deathmatch":c->game_type==4?"Capture the Flag":
            d->owner->product==QA_Q3_TEAM_ARENA?c->game_type==5?"One Flag CTF":c->game_type==6?"Overload":c->game_type==7?"Harvester":"":"";
        snprintf(heading,sizeof(heading),"%s",name);
    }
    if(draw_heading) {
        if(d->owner->product==QA_Q3_TEAM_ARENA) {
            if(!d->owner->options.mission_text(d->owner->options.context,d->frame,heading,c->game_type==1?60:90,0.6f,q3nh_white,6,true,d->error))return false;
        } else {
            size_t length=q3nh_strlen(heading); int32_t cw=length>20?640/(int32_t)length:32;
            if(!q3nh_text(d,(float)(320-(int32_t)length*cw/2),c->game_type==1?20:25,heading,(float)cw,
               (float)q3ne_int(q3ne_mul((float)cw,c->game_type==1?1.5f:1.1f)),q3nh_white,false,true,0))return false;
        }
    }
    int32_t seconds=q3ne_sub(c->warmup,d->frame->time)/1000,new_warmup=c->warmup;
    if(seconds<0) { new_warmup=0; seconds=0; }
    snprintf(text,sizeof(text),"Starts in: %i",q3ne_plus(seconds,1));
    if(seconds!=c->warmup_count) {
        if(!q3n_server_commands_warmup_drawn(d->commands,d->frame,new_warmup,seconds,d->error))return false;
        if(seconds>=0 && seconds<=2 && !q3nh_sound(d,seconds==0?Q3N_S_COUNT1:seconds==1?Q3N_S_COUNT2:Q3N_S_COUNT3,7))return false;
    } else if(new_warmup!=c->warmup && !q3n_server_commands_warmup_drawn(d->commands,d->frame,new_warmup,seconds,d->error))return false;
    int32_t cw=seconds==0?28:seconds==1?24:seconds==2?20:16;
    if(d->owner->product==QA_Q3_TEAM_ARENA)return d->owner->options.mission_text(d->owner->options.context,d->frame,text,125,
        seconds==0?0.54f:seconds==1?0.51f:seconds==2?0.48f:0.45f,q3nh_white,6,true,d->error);
    float width;
    return q3nh_width(d,text,(float)cw,(float)(cw*3/2),0,&width) &&
        q3nh_text(d,320-width*0.5f,70,text,(float)cw,(float)(cw*3/2),q3nh_white,false,true,0);
}
static bool disconnect(q3n_hud_draw *d)
{
    q3n_hud *o=d->owner;
    int32_t time=o->oldest_command_time;
    if(d->frame->compiled) {
        bool available;
        if(!o->options.compiled_oldest_command || !q3nh_current(o,d->frame,d->error) ||
           !o->options.compiled_oldest_command(o->options.context,d->frame,&time,&available,d->error) ||
           !q3nh_current(o,d->frame,d->error))return false;
        if(!available)return true;
    } else if(d->frame->remote) {
        qa_q3_usercmd command;
        if(!o->options.oldest_command || !q3nh_current(o,d->frame,d->error) ||
           !o->options.oldest_command(o->options.context,d->frame,&command,d->error) ||
           !q3nh_current(o,d->frame,d->error))return false;
        time=command.serverTime;
    } else if(!o->has_oldest_command)return true;
    if(time<=q3n_frame_snapshot_player(d->frame)->commandTime || time>d->frame->time)return true;
    q3nh_anchor(d,320,0);
    if(!q3nh_center(d,100,"Connection Interrupted",1))return false;
    q3nh_anchor(d,640,480);
    return (!d->frame->preferences.reduced_flashes && ((d->frame->time>>9)&1)) ||
        q3nh_picture(d,592,432,48,48,q3n_media_read(d->frame->media)->graphics[Q3N_G_CONNECTION]);
}
static bool lagometer(q3n_hud_draw *d)
{
    q3nh_anchor(d,640,480);
    /* Local native GAME has no network latency graph. Its command receipt can
     * still report an actual stalled oldest command. */
    if(!d->settings->lagometer || d->settings->local_server)return disconnect(d);
    q3n_hud *o=d->owner; if(!d->frame->remote && !o->snapshot_count)return disconnect(d);
    float y=o->product==QA_Q3_TEAM_ARENA?336:432;
    if(!q3nh_color(d,NULL) || !q3nh_picture(d,592,y,48,48,q3n_media_read(d->frame->media)->graphics[Q3N_G_LAGOMETER]))return false;
    qa_scene_rect_f graph=q3nh_rect(d,(qa_scene_rect_f){592,y,48,48});
    float ax=graph.x,ay=graph.y,aw=graph.width,ah=graph.height;
    const float yellow[4]={1,1,0,1},blue[4]={0,0,1,1},green[4]={0,1,0,1};
    int32_t shader=q3n_media_read(d->frame->media)->graphics[Q3N_G_WHITE];
    float range=q3ne_div(ah,3),mid=q3ne_add(ay,range),scale=q3ne_div(range,300);
    for(int32_t a=0;(float)a<aw;++a) {
        float value=q3ne_mul((float)o->frame_samples[(uint32_t)q3ne_sub(q3ne_sub(o->frame_count,1),a)&127u],scale);
        if(!value)continue;
        float h=fminf(fabsf(value),range);
        if(!q3nh_color(d,value>0?yellow:blue) || !q3nh_pixels(d,ax+aw-(float)a,value>0?mid-h:mid,1,h,shader,(qa_scene_vec4){0}))return false;
    }
    range=q3ne_div(ah,2); scale=q3ne_div(range,900);
    for(int32_t a=0;(float)a<aw;++a) {
        unsigned i=(uint32_t)q3ne_sub(q3ne_sub(o->snapshot_count,1),a)&127u; int32_t value=o->snapshot_samples[i];
        if(!value)continue;
        float h=value<0?range:fminf(q3ne_mul((float)value,scale),range);
        if(!q3nh_color(d,value<0?q3nh_red:o->snapshot_flags[i]&1?yellow:green) ||
           !q3nh_pixels(d,ax+aw-(float)a,ay+ah-h,1,h,shader,(qa_scene_vec4){0}))return false;
    }
    if(!q3nh_color(d,NULL))return false;
    if((d->settings->no_predict || d->settings->synchronous_clients) && !q3nh_big(d,592,y,"snc",1))return false;
    return disconnect(d);
}
static bool weapon_fade(void *context,int32_t start,int32_t duration,float color[4],bool *visible,qa_error *error)
{ q3n_hud_draw *d=context; (void)error; *visible=q3nh_fade(d->frame->time,start,duration,color); return true; }
static bool weapon_color(void *context,const float color[4],qa_error *error)
{ q3n_hud_draw *d=context; (void)error; return q3nh_color(d,color); }
static bool weapon_picture(void *context,float x,float y,float w,float h,int32_t shader,qa_error *error)
{ q3n_hud_draw *d=context; (void)error; return q3nh_picture(d,x,y,w,h,shader); }
static size_t weapon_strlen(void *context,const char *text) { (void)context; return q3nh_strlen(text); }
static bool weapon_string(void *context,int32_t x,int32_t y,const char *text,const float color[4],qa_error *error)
{ q3n_hud_draw *d=context; (void)error; return q3nh_text(d,(float)x,(float)y,text,16,16,color,false,true,0); }
static bool active_status(q3n_hud_draw *d)
{
    if(d->owner->product==QA_Q3_TEAM_ARENA) {
        if(d->settings->draw_status && (!d->owner->options.mission_paint(d->owner->options.context,d->frame,false,false,d->error) ||
           !d->owner->options.mission_timed(d->owner->options.context,d->frame,d->error)))return false;
    } else if(!status_bar(d))return false;
    q3nh_anchor(d,320,0);
    int32_t warning=q3n_player_state_feedback(d->player)->low_ammo_warning;
    if(!d->weapon_hud.selected && d->settings->draw_ammo_warning && warning &&
       !q3nh_center(d,64,warning==2?"OUT OF AMMO":"LOW AMMO WARNING",1))return false;
    if(d->owner->product==QA_Q3_TEAM_ARENA) {
        q3n_hud_state *s=&d->owner->state;
        if(!(q3n_frame_snapshot_player(d->frame)->eFlags&2))s->prox_time=0;
        else {
            if(!s->prox_time) { s->prox_time=q3ne_plus(d->frame->time,5000); s->prox_counter=5; s->prox_tick=0; }
            if(d->frame->time>s->prox_time) { s->prox_tick=s->prox_counter; s->prox_counter=q3ne_sub(s->prox_counter,1); s->prox_time=q3ne_plus(d->frame->time,1000); }
            char text[64]; if(s->prox_tick)snprintf(text,sizeof(text),"INTERNAL COMBUSTION IN: %i",s->prox_tick); else snprintf(text,sizeof(text),"YOU HAVE BEEN MINED");
            if(!q3nh_text(d,320-(float)q3nh_strlen(text)*8,80,text,16,16,q3nh_red,true,true,0))return false;
        }
    }
    if(!crosshair(d) || !crosshair_names(d))return false;
    q3nh_anchor(d,320,480);
    q3n_weapon_drawing drawing={d,weapon_fade,weapon_color,weapon_picture,weapon_strlen,weapon_string};
    if(!d->weapon_hud.selected && !q3n_weapons_draw_selection(d->frame,&drawing,d->error))return false;
    if(d->owner->product==QA_Q3_ARENA) {
        q3nh_anchor(d,640,240);
        int32_t item=q3n_frame_snapshot_player(d->frame)->stats[1];
        if(item) { if(item<0 || item>=256 || !q3n_media_register_item(d->frame->media,(uint32_t)item,d->error) ||
            !q3nh_picture(d,592,216,48,48,q3n_media_read(d->frame->media)->items[item].icon))return false; }
    }
    return reward(d);
}
bool q3n_hud_frame(q3n_hud *o,const q3n_frame *f,const q3n_hud_settings *settings,q3n_server_commands *commands,
    q3n_player_state *player,qa_scene_rect viewport,qa_error *e)
{
    if(!settings || !commands || !player || !o || o->busy || !q3nh_current(o,f,e) ||
       !q3n_server_commands_idle(commands) || !q3n_player_state_idle(player) || !viewport.width || !viewport.height ||
       player->source_game!=o->source_game || player->options.application!=o->options.application ||
       player->options.assets!=o->options.assets || player->options.seat!=o->options.seat ||
       player->options.remote_client!=o->options.remote_client ||
       player->options.compiled_source!=o->options.compiled_source ||
       (f->remote && f->remote->snapshots.stage!=Q3N_REMOTE_COMPLETED_FRAME))return false;
    q3n_hud_draw d={.owner=o,.frame=f,.settings=settings,.commands=commands,.player=player,.viewport=viewport,.error=e};
    if(!q3nh_preferences(&d) || !q3n_hud_weapon_read(o,f,&d.weapon_hud,e))return false;
    o->busy=true; q3nh_anchor(&d,320,240);
    const q3n_command_state *c=q3n_server_commands_state(commands); const qa_q3_player *p=q3n_frame_snapshot_player(f);
    bool ok=true;
    if(p->persistant[3]==3 && (p->pmFlags&8192)) { ok=q3nh_tourney(&d); goto end; }
    if(o->product==QA_Q3_TEAM_ARENA && (!o->options.mission_order(o->options.context,f,e) ||
       !q3nh_current(o,f,e))) { ok=false; goto end; }
    if(c->level_shot || !settings->draw_2d)goto end;
    if(p->pmType==5) {
        if(o->product==QA_Q3_ARENA && c->game_type==2)ok=center_string(&d);
        else { o->state.score_fade_time=f->time; ok=q3nh_scoreboard(&d,&o->state.scoreboard_showing); }
        goto end;
    }
    if(p->persistant[3]==3) {
        q3nh_anchor(&d,320,480);
        ok=q3nh_big(&d,248,440,"SPECTATOR",1);
        if(ok && c->game_type==1)ok=q3nh_big(&d,200,460,"waiting to play",1);
        else if(ok && c->game_type>=3)ok=q3nh_big(&d,8,460,"press ESC and use the JOIN menu to play",1);
        if(ok)ok=crosshair(&d) && crosshair_names(&d);
    } else {
        if(!o->state.show_scores && p->stats[0]>0)ok=active_status(&d);
        if(ok && c->game_type>=3 && o->product==QA_Q3_ARENA)ok=q3nh_team_chat(&d);
    }
    if(ok)ok=votes(&d) && lagometer(&d) && q3nh_corners(&d);
    bool following=false; if(ok)ok=follow(&d,&following); if(ok && !following)ok=warmup(&d);
    if(ok)ok=q3nh_scoreboard(&d,&o->state.scoreboard_showing);
    if(ok && !o->state.scoreboard_showing)ok=center_string(&d);
end:
    if(ok)ok=q3nh_color(&d,NULL) && q3nh_current(o,f,e);
    o->busy=false; return ok;
}
bool q3n_hud_tile_clear(q3n_hud *o,const q3n_frame *f,qa_scene_rect viewport,qa_error *e)
{
    if(!o || o->busy || !q3nh_current(o,f,e))return false;
    const qa_q3_refdef *r=&f->refdef;
    if(!r->x && !r->y && r->width==(int32_t)viewport.width && r->height==(int32_t)viewport.height)return true;
    q3n_hud_draw d={.owner=o,.frame=f,.viewport=viewport,.error=e};
    float top=(float)r->y,bottom=(float)(r->y+r->height-1),left=(float)r->x,right=(float)(r->x+r->width-1);
    float boxes[4][4]={{0,0,(float)viewport.width,top},{0,bottom,(float)viewport.width,(float)viewport.height-bottom},
        {0,top,left,bottom-top+1},{right,top,(float)viewport.width-right,bottom-top+1}};
    o->busy=true; bool ok=q3nh_color(&d,NULL);
    for(unsigned i=0;ok && i<4;++i) { float *b=boxes[i];
        ok=q3nh_pixels(&d,b[0],b[1],b[2],b[3],q3n_media_read(f->media)->graphics[Q3N_G_BACK_TILE],
            (qa_scene_vec4){b[0]/64,b[1]/64,(b[0]+b[2])/64,(b[1]+b[3])/64}); }
    o->busy=false; return ok;
}
void q3n_hud_round(q3n_hud *o)
{ (void)o; /* CG_MapRestart retains HUD, reward, head, FPS and warning timers. */ }
